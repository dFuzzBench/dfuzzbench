#!/usr/bin/env python3
"""Recall check for the reachability-effort study.

All ARVO/CyberGym CVEs are pre-cutoff, so a model MIGHT reproduce one by recalling a known PoC
rather than genuinely reasoning about reachability. That would deflate the measured reachability
effort on exactly the successful episodes we care about. This flags reproduced episodes that
show little surfaced reachability work, and reports the overall reachability share both WITH
and WITHOUT the flagged set -- if the share barely moves, recall is not driving the result.

Two behavioural signals per reproduced episode (from the turn labels + session tokens):
  * very_fast       -- reproduced in <= --fast-turns turns;
  * low_reach_think -- total thinking tokens on reachability (category 1 or reachability_related)
                       below --floor-think (almost no surfaced path reasoning, yet it reproduced).
An episode is recall-suspect if reproduced AND (very_fast OR low_reach_think). A content-based
check (submitted input vs the baked PoC) is a stronger future signal; this is the behavioural tier.

Usage: python exp_reach/recall_check.py <run_dir> [--fast-turns 4] [--floor-think 400]
Reads episodes/<id>/{labels.jsonl,result.json}; writes recall.json at the run root.
"""
import argparse
import json
import os


def load_episode(ep_dir):
    labs = [json.loads(l) for l in open(os.path.join(ep_dir, "labels.jsonl"))]
    result = json.load(open(os.path.join(ep_dir, "result.json")))
    return labs, result


def reach_flags(labs):
    """(total_think, reach_think_narrow, reach_think_broad) over the turns."""
    tot = sum(t["thinking_tokens"] for t in labs)
    narrow = sum(t["thinking_tokens"] for t in labs
                 if (t["rule_category"] if t["rule_category"] is not None else t["judge_category"]) == 1)
    broad = sum(t["thinking_tokens"] for t in labs
                if ((t["rule_category"] if t["rule_category"] is not None else t["judge_category"]) == 1
                    or t["reachability_related"]))
    return tot, narrow, broad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run_dir")
    ap.add_argument("--fast-turns", type=int, default=4)
    ap.add_argument("--floor-think", type=int, default=400)
    args = ap.parse_args()

    eps_root = os.path.join(args.run_dir, "episodes")
    rows = []
    for d in sorted(os.listdir(eps_root)):
        ep = os.path.join(eps_root, d)
        if not (os.path.isdir(ep) and os.path.exists(os.path.join(ep, "labels.jsonl"))):
            continue
        labs, result = load_episode(ep)
        tot, rn, rb = reach_flags(labs)
        reproduced = bool(result.get("reproduced"))
        turns = result.get("turns", len(labs))
        very_fast = reproduced and turns <= args.fast_turns
        low_reach = reproduced and rb < args.floor_think
        rows.append({"cve": d, "project": result.get("project"), "outcome": result.get("outcome"),
                     "reproduced": reproduced, "turns": turns,
                     "total_think": tot, "reach_think_narrow": rn, "reach_think_broad": rb,
                     "very_fast": very_fast, "low_reach_think": low_reach,
                     "recall_suspect": reproduced and (very_fast or low_reach)})

    repro = [r for r in rows if r["reproduced"]]
    suspect = [r for r in repro if r["recall_suspect"]]
    clean = [r for r in repro if not r["recall_suspect"]]

    def share(group, key):
        # thinking-token-weighted reachability share across a group of episodes (macro mean)
        if not group:
            return None
        vals = [(r[key] / r["total_think"]) for r in group if r["total_think"]]
        return round(sum(vals) / len(vals), 4) if vals else None

    out = {
        "params": {"fast_turns": args.fast_turns, "floor_think": args.floor_think},
        "n_episodes": len(rows), "n_reproduced": len(repro), "n_recall_suspect": len(suspect),
        "suspect_cves": [r["cve"] for r in suspect],
        "reach_share_thinking": {
            "narrow": {"all_reproduced": share(repro, "reach_think_narrow"),
                       "excluding_suspect": share(clean, "reach_think_narrow")},
            "broad": {"all_reproduced": share(repro, "reach_think_broad"),
                      "excluding_suspect": share(clean, "reach_think_broad")},
        },
        "rows": rows,
    }
    json.dump(out, open(os.path.join(args.run_dir, "recall.json"), "w"), indent=1)

    print(f"episodes={len(rows)} reproduced={len(repro)} recall-suspect={len(suspect)} "
          f"{out['suspect_cves']}")
    for r in rows:
        tag = "SUSPECT" if r["recall_suspect"] else ("repro" if r["reproduced"] else "-")
        print(f"  {r['cve']:12s} {r['project']:9s} {r['outcome']:11s} turns={r['turns']:3d} "
              f"reach_think(broad)={r['reach_think_broad']:5d}/{r['total_think']:5d} {tag}")
    s = out["reach_share_thinking"]
    print(f"\nthinking-token reach share (reproduced episodes):")
    print(f"  narrow: all={s['narrow']['all_reproduced']}  excl-suspect={s['narrow']['excluding_suspect']}")
    print(f"  broad : all={s['broad']['all_reproduced']}  excl-suspect={s['broad']['excluding_suspect']}")
    print(f"  -> {os.path.join(args.run_dir, 'recall.json')}")


if __name__ == "__main__":
    main()
