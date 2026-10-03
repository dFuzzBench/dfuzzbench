#!/usr/bin/env python3
"""T6 leakage check: how close are the generated inputs to the public ARVO reproducers (PoCs)?

For every input sent to a CVE's container -- every answered round of a Gemini sweep run (--runs; the inputs are
in <results>/<run>/inputs/...) and every proposal of a Claude Code run (--claude-runs, exp_claude) -- against
that CVE's PoC:
  exact       the input is the PoC, byte for byte
  similarity  1 - Levenshtein(input, PoC) / max(len): normalized byte edit similarity
  lcs         longest common substring / len(PoC)
A near-duplicate is an exact match or similarity >= 0.9. PoCs of <= 8 bytes are reported apart: matching them
says nothing. The chance level is each input's best similarity to the PoCs of the *other* CVEs of the same fuzz
target (same input format): an input much closer to its own PoC than to those points at that CVE's PoC in
particular. Everything is broken down by run (setting) and by whether the input reproduced the crash.

    python exp_gemini/poc_overlap.py --runs t6-realistic t6-no-source t6-perturbed t6-random --claude-runs claude

The PoCs are the `poc` fields of the benchmark json (latin-1), the bytes the CVEs' images ship as /tmp/poc.
Writes <results>/poc_overlap.{md,csv}.
"""
import argparse
import collections
import csv
import difflib
import glob
import json
import os
import statistics
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sweep  # noqa: E402

NEAR_DUPLICATE = 0.9
TINY_POC = 8  # bytes


def levenshtein(a, b):
    """Byte edit distance, one numpy row per byte of the longer string."""
    if len(a) < len(b):
        a, b = b, a
    if not b:
        return len(a)
    bb = np.frombuffer(b, dtype=np.uint8)
    idx = np.arange(len(b) + 1, dtype=np.int64)
    prev = idx.copy()
    t = np.empty(len(b) + 1, dtype=np.int64)
    for ch in a:
        t[0] = prev[0] + 1
        np.minimum(prev[1:] + 1, prev[:-1] + (bb != ch), out=t[1:])   # deletion, substitution
        prev = np.minimum.accumulate(t - idx) + idx                     # insertions chain along the row
    return int(prev[-1])


def similarity(a, b):
    if not a and not b:
        return 1.0
    return 1 - levenshtein(a, b) / max(len(a), len(b))


def lcs_fraction(inp, poc):
    if not poc:
        return 0.0
    m = difflib.SequenceMatcher(None, inp, poc, autojunk=False).find_longest_match(0, len(inp), 0, len(poc))
    return m.size / len(poc)


def verdicts():
    """(target_key, input sha) -> verification record, from every run."""
    out = {}
    for path in sorted(glob.glob(os.path.join(sweep.RESULTS_ROOT, "*", "verifications.jsonl"))):
        for v in sweep.Store(os.path.dirname(path)).load("verifications.jsonl"):
            if v.get("status") == "ok":
                out[(v["target_key"], v["harness_sha"])] = v
    return out


def summarize(rows, label):
    if not rows:
        return None
    sims = [r["similarity"] for r in rows]
    null = [r["null_similarity"] for r in rows if r["null_similarity"] is not None]
    return {
        "group": label, "inputs": len(rows), "CVEs": len({r["cve"] for r in rows}),
        "exact": sum(r["exact"] for r in rows),
        "exact CVEs": len({r["cve"] for r in rows if r["exact"]}),
        "near-dup (>=0.9)": sum(r["similarity"] >= NEAR_DUPLICATE for r in rows),
        "near-dup CVEs": len({r["cve"] for r in rows if r["similarity"] >= NEAR_DUPLICATE}),
        "median sim own PoC": round(statistics.median(sims), 3),
        "median sim other PoCs (chance)": round(statistics.median(null), 3) if null else "-",
        "closer to own than to others": f"{sum(r['null_similarity'] is not None and r['similarity'] > r['null_similarity'] for r in rows)}/{len(null)}",
        "median lcs/PoC": round(statistics.median(r["lcs"] for r in rows), 3),
    }


class Compare:
    def __init__(self, entries):
        self.entries = entries
        self.pocs = {cve: e["poc"].encode("latin-1") for cve, e in entries.items() if e.get("poc") is not None}
        self.by_fuzzer = collections.defaultdict(list)
        for cve, e in entries.items():
            if cve in self.pocs:
                self.by_fuzzer[(e["project_name"], e["fuzz_target"])].append(cve)

    def row(self, cve, inp, **extra):
        e, poc = self.entries[cve], self.pocs[cve]
        others = [self.pocs[c] for c in self.by_fuzzer[(e["project_name"], e["fuzz_target"])] if c != cve]
        return dict(extra, cve=cve, project=e["project_name"], poc_len=len(poc), input_len=len(inp), exact=inp == poc,
                    similarity=round(similarity(inp, poc), 4), lcs=round(lcs_fraction(inp, poc), 4),
                    null_similarity=round(max(similarity(inp, o) for o in others), 4) if others else None)


def sweep_rows(cmp, run, checked):
    store = sweep.Store(os.path.join(sweep.RESULTS_ROOT, run))
    setting = (store.load("runs.jsonl") or [{"args": {}}])[-1]["args"].get("setting", run)
    rows = []
    for r in store.load("responses.jsonl"):
        if r["status"] != "input" or r["target"] not in cmp.pocs:
            continue
        with open(r["harness_path"], "rb") as f:
            inp = f.read()
        v = checked.get((r["target_key"], r["harness_sha"]), {})
        rows.append(cmp.row(r["target"], inp, run=run, setting=setting, model=r["model"], round=r["round"],
                            hit=bool(v.get("hit")), reproduced=bool(v.get("reproduced")), verified=bool(v)))
    return rows


def claude_rows(cmp, run):
    """Every proposal of the T6 episodes of an exp_claude run (a name under <results>/exp_claude, or a dir)."""
    run_dir = run if os.path.isdir(run) else os.path.join(sweep.RESULTS_ROOT, "exp_claude", run)
    label = "claude " + os.path.basename(run_dir.rstrip("/"))
    rows = []
    for ep in sorted(glob.glob(os.path.join(run_dir, "episodes", "*_T6-*"))):
        cve = os.path.basename(ep).split("_T6-")[1]
        path = os.path.join(ep, "proposals.jsonl")
        if cve not in cmp.pocs or not os.path.exists(path):
            continue
        with open(path) as f:
            proposals = [json.loads(line) for line in f if line.strip()]
        for n, p in enumerate(proposals, 1):
            files = glob.glob(os.path.join(ep, "inputs", "*", "*", (p.get("harness_sha") or "-")[:16] + ".bin"))
            if not files:
                continue
            with open(files[0], "rb") as f:
                inp = f.read()
            rows.append(cmp.row(cve, inp, run=label, setting="claude-code", model="claude", round=n,
                                hit=bool(p.get("hit")), reproduced=bool(p.get("reproduced")),
                                verified=p.get("status") == "ok"))
    return label, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runs", nargs="*", default=[], help="sweep runs (names under the results dir)")
    parser.add_argument("--claude-runs", nargs="*", default=[],
                        help="Claude Code runs: names under <results>/exp_claude, or run directories")
    parser.add_argument("--benchmark", nargs="+", default=[sweep.T6_BENCHMARK],
                        help="the benchmark json(s) the runs evaluated (default: T6); several are pooled")
    args = parser.parse_args()
    if not args.runs and not args.claude_runs:
        parser.error("give --runs and/or --claude-runs")
    cmp = Compare({cve: e for path in args.benchmark for cve, e in sweep.load_arvo_entries(path).items()})
    checked = verdicts()
    runs = []
    for run in args.runs:
        runs.append((run, sweep_rows(cmp, run, checked)))
    for run in args.claude_runs:
        runs.append(claude_rows(cmp, run))
    rows = [r for _, rr in runs for r in rr]
    csv_path = os.path.join(sweep.RESULTS_ROOT, "poc_overlap.csv")
    fields = ["run", "setting", "model", "cve", "project", "round", "poc_len", "input_len", "exact", "similarity",
              "lcs", "null_similarity", "hit", "reproduced", "verified"]
    with open(csv_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)
    lines = ["# T6 input vs ARVO PoC overlap", "",
             f"near-duplicate: exact or byte edit similarity >= {NEAR_DUPLICATE}; PoCs of <= {TINY_POC} bytes apart; "
             "chance: best similarity to the other PoCs of the same fuzz target", ""]
    groups = []
    for label, rr in runs:
        informative = [r for r in rr if r["poc_len"] > TINY_POC]
        measurable = [r for r in informative if r["cve"] not in sweep.POC_UNREPRODUCIBLE]  # see sweep.py
        groups += [summarize(informative, f"{label}: all"),
                   summarize([r for r in measurable if r["reproduced"]], f"{label}: reproduced"),
                   summarize([r for r in measurable if not r["reproduced"]], f"{label}: not reproduced"),
                   summarize([r for r in rr if r["poc_len"] <= TINY_POC], f"{label}: PoC <= {TINY_POC} B (uninformative)")]
    groups = [g for g in groups if g]
    if groups:
        cols = list(groups[0])
        lines += ["| " + " | ".join(cols) + " |", "|" + "---|" * len(cols)]
        lines += ["| " + " | ".join(str(g[c]) for c in cols) + " |" for g in groups]
    exact = sorted({(r["run"], r["cve"], r["poc_len"]) for r in rows if r["exact"]})
    if exact:
        lines += ["", "exact PoC reproductions (run, CVE, PoC bytes): " + ", ".join(map(str, exact))]
    near = sorted({(r["run"], r["cve"], r["poc_len"], r["similarity"]) for r in rows
                   if r["poc_len"] > TINY_POC and r["similarity"] >= NEAR_DUPLICATE})
    if near:
        lines += ["", "near-duplicates of PoCs > 8 B (run, CVE, PoC bytes, similarity): " + ", ".join(map(str, near))]
    text = "\n".join(lines) + "\n"
    with open(os.path.join(sweep.RESULTS_ROOT, "poc_overlap.md"), "w") as f:
        f.write(text)
    print(text)
    print(f"per-input rows: {csv_path}")


if __name__ == "__main__":
    main()
