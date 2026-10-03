#!/usr/bin/env python3
"""pass@k from the per-target result CSVs of one model and setting.

For a target with n rounds of which c reached it, pass@k = 1 - C(n-c, k) / C(n, k): the unbiased estimator of
Chen et al., i.e. the expected value of the procedure (draw k of the n inputs without replacement, check
whether any reaches the target). A tier's pass@k is the mean over its targets. Rounds that ended before an input
ran (API error, no parsable input) count as misses: n is the number of rounds.

    python pass_at_k.py ../results/target-latest/*-realistic-gemini-3.1-pro-preview-*.csv
    python pass_at_k.py -k 5 ../results/target-arvo/arvo-*-realistic-gemini-3.1-pro-preview-*.csv

T1-T5 CSVs (evaluation_project.py) give pass@k per tier and language; ARVO CSVs (evaluation_arvo.py, T6 or the
post-cutoff set) give pass@k of reaching the target line and of reproducing the CVE.
"""
import argparse
import os
import re
from math import comb

TIERS = {"easy": "T1", "medium": "T2", "hard": "T3", "extreme_hard": "T4", "unreachable": "T5"}
PYTHON_PROJECTS = ("bleach", "filesystem_spec", "html5lib-python", "lark-parser", "rich")


def pass_at_k(n, c, k):
    return 1 - comb(n - c, k) / comb(n, k)


def mean(values):
    return sum(values) / len(values) if values else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="+")
    ap.add_argument("-k", default="1,5", help="comma-separated values of k (default: 1,5)")
    args = ap.parse_args()
    args.k = [int(k) for k in args.k.split(",")]

    static, arvo, short = [], [], []  # (group, tier, n, c) / (project, cve, n, reached, reproduced)
    for path in args.csv:
        name = os.path.basename(path)
        with open(path) as f:
            for line in f:
                first, second, rest = line.rstrip("\n").split(",", 2)
                if name.startswith("arvo-"):
                    n, reached, reproduced = re.match(r"(\d+)/(\d+) \((\d+)\)", rest).group(2, 1, 3)
                    arvo.append((first, second, int(n), int(reached), int(reproduced)))
                else:
                    c, n = re.match(r"(\d+)/(\d+)", rest).groups()
                    group = "Python" if name.startswith(tuple(p + "-" for p in PYTHON_PROJECTS)) else "C/C++"
                    static.append((group, TIERS[first], int(n), int(c)))

    for k in args.k:
        if static:
            print(f"pass@{k}, mean over targets (number of targets)")
            print(f"{'':8}" + "".join(f"{t:>14}" for t in list(TIERS.values()) + ["all"]))
            for group in ("Python", "C/C++", "all"):
                cells = []
                for tier in list(TIERS.values()) + [None]:
                    rows = [(n, c) for g, t, n, c in static
                            if group in ("all", g) and tier in (None, t) and n >= k]
                    value = mean([pass_at_k(n, c, k) for n, c in rows])
                    cells.append("-" if value is None else f"{value:.3f} ({len(rows)})")
                print(f"{group:8}" + "".join(f"{c:>14}" for c in cells))
            short += [f"{t} target with {n} < {k} rounds" for g, t, n, c in static if n < k]
        if arvo:
            rows = [(n, r, p) for _, _, n, r, p in arvo if n >= k]
            if rows:
                print(f"pass@{k} over {len(rows)} CVEs: reached {mean([pass_at_k(n, r, k) for n, r, p in rows]):.3f}, "
                      f"reproduced {mean([pass_at_k(n, p, k) for n, r, p in rows]):.3f}")
            short += [f"{cve} with {n} < {k} rounds" for _, cve, n, _, _ in arvo if n < k]
    if short:
        print(f"left out (too few rounds for k): {sorted(set(short))}")


if __name__ == "__main__":
    main()
