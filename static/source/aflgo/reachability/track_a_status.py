#!/usr/bin/env python3
"""Track A live status — read-only, safe to run anytime during the campaign.

The analog of source/aflgo/vulnerability/track_b_status.py, for the C/C++ T1-T5
targets of const.TARGETS (10 projects). Reports:
  * active containers (how many AFLGo build+fuzz slots are running now), and
  * progress from the orchestrator's incremental CSV (results/aflgo/track_a/aflgo-baseline-*.csv):
      - done / total targets (total = all of those targets, or those outside the
        unreachable tier with --exclude-unreachable)
      - by status: HIT (crash replay printed "HIT TARGET") / timeout / failed
        (afl_died, *_failed, no_patch, error)
      - by difficulty and by project, plus hit-rate over completed targets.

Track A has a single metric — reachability (HIT TARGET) — so there is no
reproduction count (that is Track B / ARVO-only).

  # one-shot snapshot (on-demand)
  python3 source/aflgo/reachability/track_a_status.py

  # hourly monitor (the analog of Track B's watchdog snapshot loop): prints and
  # appends a row to results/aflgo/track_a/aflgo-baseline-progress.csv every hour
  python3 source/aflgo/reachability/track_a_status.py --loop --interval 3600 [--exclude-unreachable]

Status/snapshot only — no auto-resume. Track A's orchestrator supervises each
target synchronously, so a dead afl is recorded (afl_died) and the slot advances;
there is nothing to resurrect (unlike Track B's detached afl). Recovery = re-run
the remaining targets via aflgo_baseline.py --project/--target.
"""
import argparse
import collections
import csv
import datetime
import glob
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.realpath(__file__))    # .../source/aflgo/reachability
SOURCE = os.path.dirname(os.path.dirname(HERE))       # .../source (const.py lives here)
RESULTS_DIR = os.path.join(
    os.environ.get("AFLGO_RESULTS_DIR") or os.path.join(os.path.dirname(SOURCE), "results", "aflgo"),
    "track_a")
PROGRESS = os.path.join(RESULTS_DIR, "aflgo-baseline-progress.csv")
NAME_PREFIX = os.environ.get("AFLGO_CONTAINER_PREFIX", "") + "aflgo_"

# Mirrors AFLGO_PROJECTS in aflgo_baseline.py and PROJECTS in run_track_a.sh —
# the 10 C/C++ projects AFLGo can instrument (the 5 Python projects are excluded).
AFLGO_PROJECTS = ["cmark", "libpng", "exiv2", "guetzli", "md4c",
                  "cpp-httplib", "varnish", "wamr", "clib", "libbpf"]
DIFF_ORDER = ["easy", "medium", "hard", "extreme_hard", "unreachable"]

sys.path.insert(0, SOURCE)
import const  # noqa: E402  (pure data module: TARGETS dict)


def expected_total(exclude_unreachable):
    n = 0
    for p in AFLGO_PROJECTS:
        for diff, targets in const.TARGETS.get(p, {}).items():
            if exclude_unreachable and diff == "unreachable":
                continue
            n += len(targets)
    return n


def latest_csv():
    """Newest aflgo-baseline-<ts>.csv, excluding our own progress file."""
    cands = [c for c in glob.glob(os.path.join(RESULTS_DIR, "aflgo-baseline-*.csv"))
             if "progress" not in os.path.basename(c)]
    if not cands:
        return None
    return max(cands, key=os.path.getmtime)


def active_containers():
    """Count live aflgo_* containers (current build+fuzz slots). Best-effort."""
    try:
        r = subprocess.run(["docker", "ps", "--format", "{{.Names}}"],
                           capture_output=True, text=True, timeout=30)
        # Track B's image builds are also named aflgo_* (aflgo_arvo_build_<cve>)
        return sum(1 for ln in r.stdout.splitlines() if ln.startswith(NAME_PREFIX)
                   and not ln.startswith(NAME_PREFIX + "arvo_build_"))
    except Exception:
        return -1


def tally(csv_path):
    by_status = collections.Counter()
    diff_done, diff_hit = collections.Counter(), collections.Counter()
    proj_done = collections.Counter()
    with open(csv_path) as f:
        for row in csv.DictReader(f):
            st, diff, proj = row["status"], row["difficulty"], row["project"]
            by_status[st] += 1
            diff_done[diff] += 1
            proj_done[proj] += 1
            if st == "hit":
                diff_hit[diff] += 1
    return by_status, diff_done, diff_hit, proj_done


def snapshot(total, exclude_unreachable):
    """Print a status block; return the row dict for the progress CSV."""
    ts = datetime.datetime.now().isoformat(timespec="seconds")
    alive = active_containers()
    csv_path = latest_csv()

    print(f"[{ts}] Track A status")
    print(f"active containers (build+fuzz slots): "
          f"{alive if alive >= 0 else '?'}")

    if not csv_path:
        print("baseline CSV not present yet (campaign writes it at start).")
        return {"ts": ts, "csv": "", "done": 0, "total": total,
                "hit": 0, "timeout": 0, "failed": 0, "alive": alive}

    by_status, diff_done, diff_hit, proj_done = tally(csv_path)
    done = sum(by_status.values())
    hit = by_status.get("hit", 0)
    timeout = by_status.get("timeout", 0)
    failed = done - hit - timeout

    print(f"csv: {os.path.basename(csv_path)}")
    print(f"targets done: {done}/{total}")
    print(f"  HIT (reached):  {hit}")
    print(f"  timeout:        {timeout}")
    print(f"  failed:         {failed}   (afl_died / *_failed / no_patch / error)")
    if done:
        print(f"  hit-rate:       {hit / done * 100:.0f}% of completed")
    print("per difficulty (hit/done):")
    for diff in DIFF_ORDER:
        if diff in diff_done:
            print(f"  {diff:<14} {diff_hit.get(diff, 0)}/{diff_done[diff]}")
    print("per project (done):")
    for p in sorted(proj_done):
        print(f"  {p:<12} {proj_done[p]}")

    return {"ts": ts, "csv": os.path.basename(csv_path), "done": done,
            "total": total, "hit": hit, "timeout": timeout, "failed": failed,
            "alive": alive}


def append_progress(row):
    os.makedirs(RESULTS_DIR, exist_ok=True)
    new = not os.path.exists(PROGRESS)
    with open(PROGRESS, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["ts", "csv", "done", "total",
                                          "hit", "timeout", "failed", "alive"])
        if new:
            w.writeheader()
        w.writerow(row)
        f.flush()
        os.fsync(f.fileno())


def main():
    ap = argparse.ArgumentParser(description="Track A (AFLGo C/C++) live status")
    ap.add_argument("--loop", action="store_true",
                    help="Keep running: snapshot every --interval seconds and "
                         "append a row to aflgo-baseline-progress.csv")
    ap.add_argument("--interval", type=int, default=3600,
                    help="Loop interval in seconds (default 3600 = 1 hour)")
    ap.add_argument("--exclude-unreachable", action="store_true",
                    help="Leave the unreachable (T5) targets out of the total. "
                         "Match the same flag passed to the campaign.")
    args = ap.parse_args()

    total = expected_total(args.exclude_unreachable)

    if not args.loop:
        snapshot(total, args.exclude_unreachable)
        return

    print(f"Track A monitor: snapshot every {args.interval}s → {PROGRESS}")
    while True:
        row = snapshot(total, args.exclude_unreachable)
        append_progress(row)
        print(f"(next snapshot in {args.interval}s)", flush=True)
        time.sleep(args.interval)


if __name__ == "__main__":
    main()
