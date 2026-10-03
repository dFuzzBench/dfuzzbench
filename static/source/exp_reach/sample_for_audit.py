#!/usr/bin/env python3
"""Build a stratified sample of labeled turns for a blind second-annotator audit of the Gemini
reachability judge. Pairs each sampled turn's rendered content (exactly what the judge saw) with
the judge's own label, stratifying by the judge's category so all six categories are covered.
Writes audit_sample.json (with the judge labels, for scoring) and prints a blind version.

Usage: python exp_reach/sample_for_audit.py <run_dir> [<run_dir> ...] [--per-cat 16] [--out FILE]
       (each run_dir labelled by label_turns.py; a turn's uid is <run name>:<id>:<turn>)
"""
import argparse
import collections
import json
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
from exp_reach.label_turns import extract_turns, render_turn  # noqa: E402

SEED = 20261002


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run_dirs", nargs="+")
    ap.add_argument("--per-cat", type=int, default=16)
    ap.add_argument("--out", help="default: audit_sample.json in the first run dir")
    args = ap.parse_args()
    args.out = args.out or os.path.join(args.run_dirs[0], "audit_sample.json")

    pool = []
    for d in args.run_dirs:
        corpus = os.path.basename(os.path.abspath(d))
        epsd = os.path.join(d, "episodes")
        for cve in sorted(os.listdir(epsd)):
            ep = os.path.join(epsd, cve)
            sess, lab = os.path.join(ep, "session.jsonl"), os.path.join(ep, "labels.jsonl")
            if not (os.path.exists(sess) and os.path.exists(lab)):
                continue
            labels = {r["idx"]: r for r in (json.loads(l) for l in open(lab))}
            for t in extract_turns(sess):
                r = labels.get(t["idx"])
                if not r:
                    continue
                pool.append({"uid": f"{corpus}:{cve}:{t['idx']}", "corpus": corpus, "cve": cve,
                             "idx": t["idx"], "render": render_turn(t),
                             "gemini_cat": int(r["judge_category"]),
                             "gemini_reach": bool(r["reachability_related"])})

    rng = random.Random(SEED)
    bycat = collections.defaultdict(list)
    for p in pool:
        bycat[p["gemini_cat"]].append(p)
    sample = []
    for cat in sorted(bycat):
        items = bycat[cat][:]
        rng.shuffle(items)
        sample += items[: args.per_cat]
    rng.shuffle(sample)

    json.dump(sample, open(args.out, "w"), indent=1)
    # blind version (no judge labels) for the annotator
    blind = [{"uid": s["uid"], "render": s["render"]} for s in sample]
    json.dump(blind, open(args.out.replace(".json", "_blind.json"), "w"))
    print(f"pool turns: {len(pool)} | sample: {len(sample)} | "
          f"per gemini_cat: {dict((c, min(len(bycat[c]), args.per_cat)) for c in sorted(bycat))}")
    print(f"-> {args.out} (+ _blind.json)")


if __name__ == "__main__":
    main()
