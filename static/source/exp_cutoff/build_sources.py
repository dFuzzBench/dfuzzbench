#!/usr/bin/env python3
"""Build the source images of the knowledge-cutoff cases that do not come from ARVO.

Nine of the 20 cases of static/data/post-cutoff/benchmark.json (ids gh-<fix commit>) were taken from the
projects' own fix commits, so no n132/arvo image exists for them; their entries name a locally built
`source_image` instead. This script builds those images, each from the newest ARVO image of its project
(`base_image`: the same OSS-Fuzz build environment and build.sh):

  1. a container from base_image; /src/<project> is checked out at the vulnerable commit (`commit_id`, the fix
     commit's parent; fetched from origin or `repo_link` when the image's clone lacks it) and cleaned with
     `git clean -ffdx`;
  2. the image's `arvo` script is set to SANITIZER=address and the case's fuzz target, the case's PoC (the
     entry's `poc`, latin-1) goes to /tmp/poc, and /out is emptied: the image is committed uncompiled, since a
     stale build can leave the fuzzers unlinked after the eval build patches the tree;
  3. the container is committed as source_image with ENTRYPOINT ["/usr/bin/env"] and CMD ["sleep", "infinity"]
     (`docker commit` ignores an empty ENTRYPOINT, and the working container's own entrypoint must not be
     inherited), so that `docker run <image> <cmd>` runs <cmd> as with ARVO's images.

build_arvo_docker.py then builds the eval images from these as from any ARVO image (it honors source_image):

    python exp_cutoff/build_sources.py [--ids gh-17ec36b2 ...] [--dry-run] [--force]
    python build_arvo_docker.py --benchmark ../data/post-cutoff/benchmark.json --compile-timeout 3600
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
import time

STATIC_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BENCHMARK = os.path.join(STATIC_DIR, "data", "post-cutoff", "benchmark.json")
# the base images the cases were built from, for entries that do not name theirs
BASE_IMAGE = {"assimp": "n132/arvo:470183468-vul", "opensc": "n132/arvo:467161860-vul"}

PREP = r'''
set -e
cd /src/{project}
git config --global --add safe.directory '*'
if ! git cat-file -e {commit}^{{commit}} 2>/dev/null; then
    git fetch -q origin {commit} || git fetch -q {repo} {commit} || git fetch -q --unshallow origin || git fetch -q origin
fi
git checkout -q -f {commit}
git clean -q -ffdx
arvo=$(command -v arvo || echo /bin/arvo)
sed -i -e 's/^export SANITIZER=.*/export SANITIZER=address/' -e 's#/out/[A-Za-z0-9_.-]* /tmp/poc#/out/{fuzzer} /tmp/poc#g' "$arvo"
grep -q "^export SANITIZER=address" "$arvo"
grep -q "/out/{fuzzer} /tmp/poc" "$arvo"
rm -rf /out/*
git rev-parse HEAD
'''


def cases(benchmark, ids):
    with open(benchmark) as f:
        entries = [e for e in json.load(f) if e.get("source_image")]
    if ids:
        entries = [e for e in entries if e["cve_id"] in ids]
        missing = set(ids) - {e["cve_id"] for e in entries}
        if missing:
            sys.exit(f"not in {benchmark} (or not built from a fix commit): {sorted(missing)}")
    return [{"id": e["cve_id"], "project": e["project_name"],
             "base_image": e.get("base_image") or BASE_IMAGE[e["project_name"]], "image": e["source_image"],
             "repo": e["repo_link"], "commit": e["commit_id"], "fix_commit": e.get("fix_commit"),
             "fuzzer": e["fuzz_target"], "poc": e["poc"].encode("latin-1"), "poc_source": e.get("poc_source")}
            for e in entries]


def image_exists(image):
    return subprocess.run(["docker", "image", "inspect", image], capture_output=True).returncode == 0


def run(cmd, timeout=None):
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=timeout)


def build(c, prefix):
    name = prefix + c["id"]
    run(["docker", "rm", "-f", name])
    try:
        r = run(["docker", "run", "-d", "--name", name, "--entrypoint", "sleep", c["base_image"], "infinity"])
        if r.returncode:
            return "container_failed", r.stderr.strip()[-300:]
        r = run(["docker", "exec", name, "bash", "-c", PREP.format(**c)], timeout=1800)
        if r.returncode or not r.stdout.strip().endswith(c["commit"]):
            return "checkout_failed", (r.stdout + r.stderr).strip()[-500:]
        with tempfile.NamedTemporaryFile(suffix=".poc", delete=False) as f:
            f.write(c["poc"])
        try:
            r = run(["docker", "cp", f.name, f"{name}:/tmp/poc"])
        finally:
            os.unlink(f.name)
        if r.returncode:
            return "poc_copy_failed", r.stderr.strip()[-300:]
        r = run(["docker", "commit", "--change", f"LABEL dfuzzbench.source={c['id']}",
                 "--change", 'ENTRYPOINT ["/usr/bin/env"]', "--change", 'CMD ["sleep", "infinity"]',
                 name, c["image"]], timeout=7200)
        if r.returncode:
            return "commit_failed", r.stderr.strip()[-300:]
        return "built", None
    finally:
        run(["docker", "rm", "-f", name])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--benchmark", default=BENCHMARK)
    ap.add_argument("--ids", nargs="+", help="only these cases")
    ap.add_argument("--dry-run", action="store_true", help="print the plan of every case; change nothing")
    ap.add_argument("--force", action="store_true", help="rebuild images that exist")
    ap.add_argument("--container-prefix", default="dfuzzbench-src-", help="name prefix of the working containers")
    args = ap.parse_args()
    todo = cases(args.benchmark, args.ids)
    failed = []
    for c in todo:
        exists, base = image_exists(c["image"]), image_exists(c["base_image"])
        if args.dry_run:
            print(f"{c['id']} ({c['project']}): {c['image']} {'exists' if exists else 'to build'} from {c['base_image']}"
                  f"{'' if base else ' (not present locally: docker pull it first)'}\n"
                  f"    /src/{c['project']} at {c['commit']} ({c['repo']}; fix commit {c['fix_commit']}), "
                  f"git clean -ffdx, arvo: SANITIZER=address, /out/{c['fuzzer']}, /out emptied\n"
                  f"    /tmp/poc: {len(c['poc'])} bytes ({c['poc_source']})")
            continue
        if exists and not args.force:
            print(json.dumps({"id": c["id"], "status": "exists", "image": c["image"]}), flush=True)
            continue
        start = time.time()
        status, detail = build(c, args.container_prefix)
        print(json.dumps({"id": c["id"], "status": status, "image": c["image"], "seconds": round(time.time() - start),
                          **({"detail": detail} if detail else {})}), flush=True)
        if status != "built":
            failed.append(c["id"])
    if args.dry_run:
        print(f"{len(todo)} cases; nothing changed (dry run)")
    elif failed:
        sys.exit(f"failed: {failed}")


if __name__ == "__main__":
    main()
