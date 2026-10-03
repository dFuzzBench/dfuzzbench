#!/usr/bin/env python3
"""AFLGo directed greybox fuzzing baseline for benchmark targets.

For each C/C++ target: builds AFLGo-instrumented binary with distance guidance,
runs afl-fuzz, records time-to-exposure (TTE) when target line is first reached.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime

# This script lives in static/source/aflgo/reachability/; static/ is 3 levels up.
# The shared const.py is in static/source/, so put it on the path.
REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO_ROOT, "source"))

from const import TARGETS  # noqa: E402

DATA_ROOT = os.path.join(REPO_ROOT, "data", "target-latest")
# Outputs (CSV, crash tarballs) go to <results>/track_a; <results> defaults to the
# git-ignored static/results/aflgo.
RESULTS_DIR = os.path.join(
    os.environ.get("AFLGO_RESULTS_DIR") or os.path.join(REPO_ROOT, "results", "aflgo"),
    "track_a")
# Images built by source/aflgo/build_images.sh; IMAGE_REPO selects another repo.
DOCKER_REPO = os.environ.get("IMAGE_REPO") or "local/dfuzzbench-aflgo"
CONTAINER_PREFIX = os.environ.get("AFLGO_CONTAINER_PREFIX", "")

# C/C++ projects only (AFLGo doesn't work with Python)
AFLGO_PROJECTS = ["cmark", "libpng", "exiv2", "guetzli", "md4c",
                  "cpp-httplib", "varnish", "wamr", "clib", "libbpf"]


def _auto_mem_cap_mb(parallel):
    """Hard per-container memory cap (MB), sized so the sum of caps stays within
    80% of host RAM at the chosen concurrency. Mirrors Track B's compute_limits()
    (aflgo/vulnerability/fuzzing_arvo_aflgo.py). Floor 2048 MB because AFLGo's two-stage
    LTO build needs ~GB; a smaller cap would OOM-kill the build itself."""
    mem_mb = 0
    try:
        for ln in open("/proc/meminfo"):
            if ln.startswith("MemTotal:"):
                mem_mb = int(ln.split()[1]) // 1024
                break
    except OSError:
        return 0  # can't size; caller treats 0 as "no cap"
    return max(2048, int(0.80 * mem_mb / max(1, parallel)))


def find_target_patch(project, difficulty, target_key):
    """Find the target.patch file for a given target."""
    # Try different retrieval modes for the patch
    for mode in ["bm25", "realistic", "oracle"]:
        patch_dir = os.path.join(DATA_ROOT, project, mode, difficulty, target_key)
        patch_file = os.path.join(patch_dir, "target.patch")
        if os.path.isfile(patch_file):
            return patch_file
    return None


def modify_patch_for_afl(patch_content):
    """Replace exit(0) with abort() in target patch so AFL detects crashes."""
    return patch_content.replace("exit(0);", "abort();")


def run_aflgo_target(project, difficulty, target_key, filepath, line, func_name,
                     max_time, round_idx=1, mem_cap_mb=0, cpuset=""):
    """Run AFLGo on a single target. Returns result dict."""
    container = (f"{CONTAINER_PREFIX}aflgo_{project}_"
                 f"{target_key.replace(':', '_').replace('/', '_')}_r{round_idx}_{os.getpid()}")
    image = f"{DOCKER_REPO}:{project}"

    # +2 for the two #include lines added by target.patch at the top of the file
    adjusted_line = line + 2

    print(f"[{project}/{target_key} R{round_idx}] Starting AFLGo (target={filepath}:{adjusted_line}, max_time={max_time}s)")

    # Find and modify target patch
    patch_file = find_target_patch(project, difficulty, target_key)
    if not patch_file:
        print(f"[{project}/{target_key}] No target.patch found")
        return _make_result(project, difficulty, target_key, filepath, line, func_name,
                           round_idx, status="no_patch")

    with open(patch_file, 'r') as f:
        patch_content = modify_patch_for_afl(f.read())

    # Write modified patch to temp file
    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix=".patch") as tmp:
        tmp.write(patch_content)
        tmp_patch = tmp.name

    try:
        # Cleanup any leftover container
        subprocess.run(["docker", "rm", "-f", container], capture_output=True)

        # Start container. A hard --memory cap (== --memory-swap, so no swap)
        # converts a host OOM from the concurrent in-container LTO builds into a
        # localized container OOM. Mirrors Track B (aflgo/vulnerability/fuzzing_arvo_aflgo.py).
        docker_run = ["docker", "run", "-d", "--name", container, "--shm-size=128m"]
        if cpuset:
            # Pin to specific cores: isolates CPU when co-located beside another
            # campaign, and auto-throttles the build since `make -j$(nproc)` reads
            # nproc from the cpuset mask.
            docker_run += [f"--cpuset-cpus={cpuset}"]
        if mem_cap_mb and mem_cap_mb > 0:
            docker_run += [f"--memory={mem_cap_mb}m", f"--memory-swap={mem_cap_mb}m"]
        docker_run += [image, "sleep", "infinity"]
        r = subprocess.run(docker_run, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"[{project}/{target_key}] Failed to start container: {r.stderr}")
            return _make_result(project, difficulty, target_key, filepath, line, func_name,
                               round_idx, status="container_failed")

        # Copy modified patch into container
        subprocess.run(["docker", "cp", tmp_patch, f"{container}:/src/target.patch"],
                      capture_output=True, text=True)

        # Copy the host fuzz_target.sh into the container, overriding any stale version
        # baked into the docker image. (Earlier images had a broken crash-detection check.)
        host_script = os.path.join(os.path.dirname(os.path.realpath(__file__)),
                                   "fuzz_target.sh")
        subprocess.run(["docker", "cp", host_script, f"{container}:/aflgo/fuzz_target.sh"],
                      capture_output=True, text=True)

        # Run fuzz_target.sh inside the container
        # Timeout = max_time + 30 min for build overhead
        build_timeout = max_time + 1800
        r = subprocess.run(
            ["docker", "exec", container, "bash", "/aflgo/fuzz_target.sh",
             project, filepath, str(adjusted_line), str(max_time)],
            capture_output=True, text=True, timeout=build_timeout
        )
        output = r.stdout + r.stderr

    except subprocess.TimeoutExpired:
        print(f"[{project}/{target_key}] Container timeout (safety limit)")
        output = "RESULT:tte=0,build_time=0,total_iters=0,status=timeout"
    except Exception as e:
        print(f"[{project}/{target_key}] Error: {e}")
        output = f"RESULT:tte=0,build_time=0,total_iters=0,status=error"
    finally:
        # Preserve crash inputs from /tmp/crashes-export.tar.gz before docker rm
        # so the run can be retroactively re-verified. Best-effort: silently skip
        # if the tarball wasn't produced (target didn't crash).
        crash_dir = os.path.join(RESULTS_DIR, "crashes", project)
        os.makedirs(crash_dir, exist_ok=True)
        crash_dest = os.path.join(
            crash_dir,
            f"{target_key.replace(':', '_').replace('/', '_')}_r{round_idx}.tar.gz",
        )
        subprocess.run(
            ["docker", "cp", f"{container}:/tmp/crashes-export.tar.gz", crash_dest],
            capture_output=True, text=True,
        )
        subprocess.run(["docker", "rm", "-f", container], capture_output=True)
        os.unlink(tmp_patch)

    # Parse RESULT line
    result = _make_result(project, difficulty, target_key, filepath, line, func_name, round_idx)

    match = re.search(
        r"RESULT:tte=(\d+),build_time=(\d+),total_iters=(\d+),status=(\w+)",
        output
    )
    if match:
        result["tte"] = int(match.group(1))
        result["build_time"] = int(match.group(2))
        result["total_iters"] = int(match.group(3))
        result["status"] = match.group(4)

    tte_str = f"{result['tte']}s" if result['status'] == 'hit' else result['status']
    print(f"[{project}/{target_key} R{round_idx}] Done: {tte_str} | "
          f"iters={result['total_iters']} | build={result['build_time']}s")

    return result


def _make_result(project, difficulty, target_key, filepath, line, func_name,
                 round_idx, status="unknown", tte=0, build_time=0, total_iters=0):
    return {
        "project": project,
        "difficulty": difficulty,
        "target_key": target_key,
        "filepath": filepath,
        "line": line,
        "function": func_name,
        "round": round_idx,
        "tte": tte,
        "build_time": build_time,
        "total_iters": total_iters,
        "status": status,
    }


def main():
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")

    parser = argparse.ArgumentParser(description="AFLGo directed fuzzing baseline")
    parser.add_argument("--max_time", type=int, default=1814400,
                       help="Max fuzzing time per target in seconds (default: 3 weeks)")
    parser.add_argument("--project", type=str, help="Only evaluate this project")
    parser.add_argument("--target", type=str, help="Only evaluate this target key (e.g., src:blocks.c:1035)")
    parser.add_argument("--difficulty", type=str, help="Only evaluate this difficulty level")
    parser.add_argument("--rounds", type=int, default=1, help="Number of rounds per target")
    parser.add_argument("--parallel", type=int, default=1, help="Number of concurrent containers")
    parser.add_argument("--exclude_unreachable", action="store_true",
                       help="Skip unreachable targets")
    parser.add_argument("--mem_cap_mb", type=int,
                       default=int(os.environ.get("MEM_CAP_MB", "0") or "0"),
                       help="Hard per-container memory cap in MB (docker --memory/"
                            "--memory-swap). 0 = auto-size from host RAM / --parallel. "
                            "Prevents a host OOM from concurrent in-container builds.")
    parser.add_argument("--cpuset", type=str, default=os.environ.get("CPUSET", ""),
                       help="Pin each container to these host cores (docker "
                            "--cpuset-cpus), e.g. '26-31'. Confines CPU when co-located "
                            "beside another campaign AND auto-throttles the build, since "
                            "make -j$(nproc) reads nproc from the cpuset mask.")
    args = parser.parse_args()

    # Hard per-container memory cap. Track A's containers run a heavy two-stage
    # LTO build; without a cap, N concurrent builds can exhaust host RAM and the
    # OOM-killer hits random processes (possibly this orchestrator). 0 => auto.
    mem_cap_mb = args.mem_cap_mb or _auto_mem_cap_mb(args.parallel)

    # Build task list from TARGETS dict
    tasks = []
    for project in AFLGO_PROJECTS:
        if args.project and project != args.project:
            continue
        if project not in TARGETS:
            continue

        for difficulty, targets in TARGETS[project].items():
            if args.difficulty and difficulty != args.difficulty:
                continue
            if args.exclude_unreachable and difficulty == "unreachable":
                continue

            for target_key, (filepath, line, func_name) in targets.items():
                if args.target and target_key != args.target:
                    continue
                for round_idx in range(1, args.rounds + 1):
                    tasks.append((project, difficulty, target_key, filepath, line,
                                 func_name, round_idx))

    missing = sorted(p for p in {t[0] for t in tasks}
                     if subprocess.run(["docker", "image", "inspect", f"{DOCKER_REPO}:{p}"],
                                       capture_output=True).returncode != 0)
    if missing:
        sys.exit("missing images (build them with source/aflgo/build_images.sh): "
                 + ", ".join(f"{DOCKER_REPO}:{p}" for p in missing))

    total_tasks = len(tasks)
    print(f"AFLGo baseline: {total_tasks} tasks "
          f"(max_time={args.max_time}s, parallel={args.parallel}, "
          f"mem_cap={str(mem_cap_mb) + 'm' if mem_cap_mb else 'none'})")

    # Open the CSV up front and fsync each row as it completes, so an
    # interruption mid-campaign (SSH drop / OOM / kill) preserves every
    # finished target. Rows arrive in completion order; plotting scripts
    # sort post-hoc.
    os.makedirs(RESULTS_DIR, exist_ok=True)
    csv_file = os.path.join(RESULTS_DIR, f"aflgo-baseline-{timestamp}.csv")
    csv_lock = threading.Lock()
    csv_fp = open(csv_file, "w")
    csv_fp.write("project,target_key,difficulty,filepath,line,function,round,"
                 "tte_seconds,build_time_seconds,total_iters,status\n")
    csv_fp.flush()
    os.fsync(csv_fp.fileno())

    def write_row(r):
        row = (f"{r['project']},{r['target_key']},{r['difficulty']},"
               f"{r['filepath']},{r['line']},{r['function']},{r['round']},"
               f"{r['tte']},{r['build_time']},{r['total_iters']},{r['status']}\n")
        with csv_lock:
            csv_fp.write(row)
            csv_fp.flush()
            os.fsync(csv_fp.fileno())

    results = []

    if args.parallel <= 1:
        for project, difficulty, target_key, filepath, line, func_name, round_idx in tasks:
            result = run_aflgo_target(project, difficulty, target_key, filepath, line,
                                     func_name, args.max_time, round_idx, mem_cap_mb,
                                     cpuset=args.cpuset)
            results.append(result)
            write_row(result)
    else:
        with ThreadPoolExecutor(max_workers=args.parallel) as executor:
            futures = {}
            for project, difficulty, target_key, filepath, line, func_name, round_idx in tasks:
                fut = executor.submit(run_aflgo_target, project, difficulty, target_key,
                                     filepath, line, func_name, args.max_time, round_idx,
                                     mem_cap_mb, cpuset=args.cpuset)
                futures[fut] = (project, target_key, round_idx)

            for fut in as_completed(futures):
                result = fut.result()
                results.append(result)
                write_row(result)

    csv_fp.close()

    # Summary
    print(f"\n{'='*60}")
    from collections import defaultdict
    by_project = defaultdict(list)
    for r in results:
        by_project[r["project"]].append(r)

    for project, proj_results in sorted(by_project.items()):
        hit = sum(1 for r in proj_results if r["status"] == "hit")
        total = len(proj_results)
        print(f"  {project}: {hit}/{total} targets reached")

    total_hit = sum(1 for r in results if r["status"] == "hit")
    print(f"TOTAL: {total_hit}/{len(results)} targets reached")
    print(f"CSV: {csv_file}")


if __name__ == "__main__":
    main()
