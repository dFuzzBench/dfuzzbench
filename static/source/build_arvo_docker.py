#!/usr/bin/env python3
"""Build instrumented Docker images for ARVO CVEs.

For each CVE of a benchmark json (default: T6, data/target-arvo/benchmark.json):
1. Apply <json dir>/<project>/bm25/<cve_id>/target.patch to the source code inside the Docker container
2. Compile with `arvo compile`
3. Commit as local/dfuzzbench-arvo:<cve_id> (pushed only with --push)

The source image is n132/arvo:<cve_id>-vul, or the entry's source_image for cases outside ARVO (the
post-cutoff study's gh-* cases), which is built locally beforehand and never pulled.
"""

import json
import os
import subprocess
import argparse
import sys

WORKDIR_MAP = {
    "serenity": "/src",
    "assimp": "/src/assimp",
    "openh264": "/src/openh264",
    "opensc": "/src/opensc",
    "wasm3": "/src/wasm3",
}

DATA_ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), "data", "target-arvo")
TARGET_REPO = os.environ.get("DFUZZ_DOCKER_NS", "local") + "/dfuzzbench-arvo"
CONTAINER_PREFIX = os.environ.get("DFUZZ_CONTAINER_PREFIX", "arvo")


def run(cmd, **kwargs):
    print(f"  $ {' '.join(cmd)}")
    return subprocess.run(cmd, capture_output=True, text=True, **kwargs)


def fix_serenity_build_sh(container):
    """Some serenity images have a broken build.sh that tries to build the full OS.
    Replace with one that only builds Lagom fuzzers (matching working serenity images)."""
    r = subprocess.run(
        ["docker", "exec", container, "bash", "-c", "head -1 /src/build.sh"],
        capture_output=True, text=True
    )
    first_line = r.stdout.strip()
    if first_line.startswith("cd "):
        return  # Already correct (e.g. "cd serenity/Meta/Lagom")
    # Override with Lagom-only build
    build_sh = (
        'cd serenity/Meta/Lagom\n'
        'mkdir build\n'
        'cd build\n'
        'cmake -GNinja '
        '-DBUILD_LAGOM=ON '
        '-DENABLE_OSS_FUZZ=ON '
        '-DCMAKE_C_COMPILER=$CC '
        '-DCMAKE_CXX_COMPILER=$CXX '
        '-DCMAKE_CXX_FLAGS="$CXXFLAGS -DOSS_FUZZ=ON" '
        '-DLINKER_FLAGS="$LIB_FUZZING_ENGINE" '
        '..\n'
        'ninja\n'
        'cp Fuzzers/Fuzz* $OUT/\n'
    )
    subprocess.run(
        ["docker", "exec", container, "bash", "-c", f"cat > /src/build.sh << 'BUILDEOF'\n{build_sh}BUILDEOF"],
        capture_output=True, text=True
    )
    print("  Fixed serenity build.sh for Lagom-only build")


def process_cve(entry, data_root=DATA_ROOT, dry_run=False, force=False, push=False, compile_timeout=600):
    cve_id = entry["cve_id"]
    project = entry["project_name"]
    # cases outside ARVO (the post-cutoff gh-* cases) name their own locally built source image
    src_image = entry.get("source_image") or f"n132/arvo:{cve_id}-vul"
    dst_image = f"{TARGET_REPO}:{cve_id}"
    container = f"{CONTAINER_PREFIX}_{cve_id}"
    patch_path = os.path.join(data_root, project, "bm25", cve_id, "target.patch")

    print(f"\n{'='*60}")
    print(f"CVE: {cve_id} | Project: {project}")
    print(f"Source: {src_image} | Target: {dst_image}")

    # Check if target image already exists (skip unless --force)
    if not force:
        r = run(["docker", "images", "-q", dst_image])
        if r.stdout.strip():
            print(f"  SKIP: {dst_image} already exists (use --force to rebuild)")
            return True

    local_source = entry.get("source_image") is not None
    if local_source and not run(["docker", "images", "-q", src_image]).stdout.strip():
        print(f"  FAIL: source image {src_image} not found; build it first (see the README next to the benchmark json)")
        return False

    # Clean up any leftover container
    subprocess.run(["docker", "rm", "-f", container], capture_output=True)

    try:
        # 1. Start container (a locally built source image is never pulled)
        r = run(["docker", "run", "-d"] + (["--pull", "never"] if local_source else []) +
                ["--name", container, src_image, "sleep", "infinity"])
        if r.returncode != 0:
            print(f"  FAIL: Could not start container: {r.stderr}")
            return False

        # 2. Copy patch into container
        r = run(["docker", "cp", patch_path, f"{container}:/tmp/target.patch"])
        if r.returncode != 0:
            print(f"  FAIL: Could not copy patch: {r.stderr}")
            return False

        # 3. Apply patch
        r = run(["docker", "exec", container, "bash", "-c",
                  f"cd /src/{project} && git apply /tmp/target.patch"])
        if r.returncode != 0:
            print(f"  FAIL: Patch apply failed: {r.stderr}{r.stdout}")
            return False
        print("  Patch applied OK")

        # Fix serenity build.sh if needed, and set workdir
        if project == "serenity":
            fix_serenity_build_sh(container)
            workdir = "/src"  # Always /src for serenity (build.sh handles cd)
        else:
            workdir = WORKDIR_MAP[project]
        print(f"  Workdir: {workdir}")

        if dry_run:
            print("  DRY RUN: skipping compile and push")
            return True

        # 4. Compile
        try:
            r = run(["docker", "exec", "-w", workdir, container, "arvo", "compile"],
                    timeout=compile_timeout)
        except subprocess.TimeoutExpired:
            print(f"  FAIL: Compile did not finish within {compile_timeout} s")
            return False
        if r.returncode != 0:
            print(f"  FAIL: Compile failed: {r.stderr[-500:]}")
            return False
        print("  Compile OK")

        # 5. Commit
        r = run(["docker", "commit", container, dst_image])
        if r.returncode != 0:
            print(f"  FAIL: Commit failed: {r.stderr}")
            return False
        print(f"  Committed as {dst_image}")

        # 6. Push (opt-in; users rebuilding locally do NOT need this)
        if push:
            r = run(["docker", "push", dst_image], timeout=300)
            if r.returncode != 0:
                print(f"  FAIL: Push failed: {r.stderr}")
                return False
            print(f"  Pushed {dst_image}")

        return True

    finally:
        # 7. Cleanup
        subprocess.run(["docker", "rm", "-f", container], capture_output=True)


def main():
    parser = argparse.ArgumentParser(description="Build instrumented ARVO Docker images")
    parser.add_argument("--benchmark", default=f"{DATA_ROOT}/benchmark.json",
                        help="The CVE list (default: T6); patches are read from <its dir>/<project>/bm25/<cve_id>/")
    parser.add_argument("--dry-run", action="store_true",
                        help="Only validate patches, skip compile and push")
    parser.add_argument("--push", action="store_true",
                        help="Also push built images to a registry (requires docker login; off by default)")
    parser.add_argument("--force", action="store_true",
                        help="Rebuild even if target image exists")
    parser.add_argument("--project", type=str,
                        help="Only process CVEs for this project")
    parser.add_argument("--cve", type=str,
                        help="Only process this specific CVE ID")
    parser.add_argument("--compile-timeout", type=int, default=600,
                        help="Seconds allowed for `arvo compile`")
    args = parser.parse_args()

    with open(args.benchmark) as f:
        data = json.load(f)
    data_root = os.path.dirname(os.path.abspath(args.benchmark))

    if args.cve:
        data = [e for e in data if e["cve_id"] == args.cve]
    elif args.project:
        data = [e for e in data if e["project_name"] == args.project]
    if not data:
        parser.error(f"no CVE of {args.benchmark} matches")

    print(f"Processing {len(data)} CVEs (dry_run={args.dry_run}, force={args.force})")

    results = {"success": [], "fail": []}
    for entry in data:
        ok = process_cve(entry, data_root, dry_run=args.dry_run, force=args.force, push=args.push,
                         compile_timeout=args.compile_timeout)
        if ok:
            results["success"].append(entry["cve_id"])
        else:
            results["fail"].append(entry["cve_id"])

    print(f"\n{'='*60}")
    print(f"DONE: {len(results['success'])} success, {len(results['fail'])} fail")
    if results["fail"]:
        print(f"Failed CVEs: {results['fail']}")
        sys.exit(1)


if __name__ == "__main__":
    main()
