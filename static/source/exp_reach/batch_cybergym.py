#!/usr/bin/env python3
"""Batch-build the CyberGym-N corpus.

For each candidate (exp_reach candidate list): pull its n132/arvo:<id>-vul image, derive the
target, and build + verify a HIT-TARGET marker (exp_reach.build_marker). Candidates that fail
any stage (image unavailable, parse-fail, uncompilable, marker didn't fire) are skipped and
logged; the run stops once --target markers have landed. Progress is appended to
<out>.progress.jsonl as it goes (so an interrupted run is resumable), and the final corpus is
written to --out.

Usage: python exp_reach/batch_cybergym.py --candidates <json> --out <json>
                                          [--target 50] [--workers 4] [--repo n132/arvo] [--ns local]
--candidates takes the candidate list (rows: task_id, monorail, project; data/corpus_cybergym_candidates.json
is the one the study's corpus came from) or a corpus (rows: task_id, cve, project; data/corpus_cybergym50.json
rebuilds exactly the study's 50 marker images).
"""
import argparse
import json
import os
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
from exp_reach.derive_target import reproduce, parse  # noqa: E402
from exp_reach import build_marker  # noqa: E402


def img_exists(ref):
    return subprocess.run(["docker", "image", "inspect", ref], capture_output=True).returncode == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--candidates", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--target", type=int, default=50)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--max-seconds", type=int, default=5400,
                    help="stop starting new builds after this long so the run exits cleanly "
                         "(in-flight builds drain) before any external kill orphans a compile")
    ap.add_argument("--repo", default="n132/arvo")
    ap.add_argument("--ns", default="local")
    args = ap.parse_args()

    cands = json.load(open(args.candidates))
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    prog_path = args.out + ".progress.jsonl"
    prog = open(prog_path, "a", buffering=1)
    lock = threading.Lock()
    state = {"landed": 0}
    deadline = time.time() + args.max_seconds

    def record(r):
        with lock:
            prog.write(json.dumps(r) + "\n")
        print(f"  [{state['landed']:2d}/{args.target}] {r['task']:16s} {r['status']:7s} "
              f"{r.get('reason', r.get('target', {}).get('file', '') if r.get('target') else '')}")
        sys.stdout.flush()

    def do_one(c):
        mid, proj, task = c.get("monorail") or c["cve"], c["project"], c["task_id"]
        vul, dst = f"{args.repo}:{mid}-vul", f"{args.ns}/dfuzzbench-arvo:{mid}"
        with lock:
            if state["landed"] >= args.target:
                return {"task": task, "status": "skip", "reason": "quota"}
        if time.time() > deadline:
            return {"task": task, "status": "skip", "reason": "deadline"}
        try:
            if img_exists(dst):                      # already built (e.g. the prototypes)
                tgt = parse(reproduce(vul), proj) if img_exists(vul) else None
                res = {"ok": True, "reached": True, "target": tgt, "reproduced": None, "prebuilt": True}
            else:
                p = subprocess.run(["docker", "pull", vul], capture_output=True, text=True, timeout=1200)
                if p.returncode:
                    return {"task": task, "status": "skip", "reason": "pull"}
                res = build_marker.build(mid, proj, args.repo, args.ns)
        except Exception as e:  # noqa: BLE001
            return {"task": task, "status": "skip", "reason": f"exc:{type(e).__name__}"}
        if not (res.get("ok") and res.get("reached")):
            return {"task": task, "status": "skip", "reason": res.get("stage", "build"),
                    "err": (res.get("err") or "")[:200]}
        row = {"task": task, "mid": mid, "project": proj, "status": "landed",
               "target": res.get("target"), "reproduced": res.get("reproduced"),
               "prebuilt": res.get("prebuilt", False)}
        with lock:
            if state["landed"] >= args.target:
                row["status"] = "extra"              # finished after the quota filled; keep as spare
            else:
                state["landed"] += 1
        return row

    results = []
    with ThreadPoolExecutor(max_workers=args.workers) as ex:
        futs = [ex.submit(do_one, c) for c in cands]
        for f in as_completed(futs):
            r = f.result()
            results.append(r)
            record(r)
    prog.close()

    landed = [r for r in results if r["status"] in ("landed", "extra") and r.get("target")]
    corpus = []
    for r in landed:
        t = r["target"]
        corpus.append({"cve": r["mid"], "task_id": r["task"], "project": r["project"],
                       "source": "cybergym",
                       "target": [t["file"], t["line"], t["func"], t["pattern"]]})
    json.dump(corpus, open(args.out, "w"), indent=1)

    by_reason = {}
    for r in results:
        if r["status"] == "skip":
            by_reason[r["reason"]] = by_reason.get(r["reason"], 0) + 1
    print(f"\n=== CyberGym batch done: landed {len([r for r in results if r['status']=='landed'])}, "
          f"extra {len([r for r in results if r['status']=='extra'])}, "
          f"skipped {sum(by_reason.values())} {by_reason} ===")
    print(f"  corpus -> {args.out} ({len(corpus)} targets)")


if __name__ == "__main__":
    main()
