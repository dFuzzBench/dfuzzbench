#!/usr/bin/env python3
"""Run one model's jobs back to back: each job starts once the model's API work in the previous run is
finished (that run may still be verifying). A T6 job needs the eval image of every case of its benchmark
(--benchmark, default T6); while one is missing, the next job goes first and T6 is tried again after it.

    python exp_gemini/jobqueue.py --model gemini-3.1-pro-preview --after realistic \\
        --job t6-realistic:t6_realistic --job perturbed:no_context_placeholder ...

Each job is started with run.sh as
    --name <job> --models <model> --setting <setting> --rounds 20 plus the per-kind options below.
Log: <results>/queue-<model>.log. Safe to restart: finished jobs are skipped, running ones awaited.
"""
import argparse
import json
import os
import shlex
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sweep  # noqa: E402

OPTIONS = {  # per job kind: T6 verification is a container run, T1-T5 a compile
    "t6": ["--verify-workers", "4", "--min-verify-workers", "2", "--max-inflight", "64"],
    "t1-5": ["--verify-workers", "6", "--min-verify-workers", "4", "--max-inflight", "128"],
}


def log(model, message):
    line = f"{sweep.now_iso()} {message}"
    print(line, flush=True)
    os.makedirs(sweep.RESULTS_ROOT, exist_ok=True)
    with open(os.path.join(sweep.RESULTS_ROOT, f"queue-{model}.log"), "a") as f:
        f.write(line + "\n")


def last_status(name):
    path = os.path.join(sweep.RESULTS_ROOT, name, "status.jsonl")
    if not os.path.exists(path):
        return None
    with open(path, "rb") as f:
        f.seek(max(0, os.path.getsize(path) - 65536))
        lines = f.read().decode(errors="replace").splitlines()
    for line in reversed(lines):
        try:
            return json.loads(line)
        except json.JSONDecodeError:
            continue
    return None


def running(name):
    return subprocess.run(["pgrep", "-f", f"exp_gemini/sweep.py run --name {name}( |$)"],
                          capture_output=True).returncode == 0


def api_finished(name, model):
    status = last_status(name)
    m = status and status["models"].get(model)
    if not m:
        return False
    if "api_finished" in m:
        return m["api_finished"]
    return m["done"] + m["given_up"] >= m["total"] and m["inflight"] == 0


def t6_ready(benchmark):
    return all(subprocess.run(["docker", "image", "inspect", f"{sweep.ea.DOCKER_REPO}:{cve}"],
                              capture_output=True).returncode == 0 for cve in sweep.load_arvo_entries(benchmark))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--model", required=True)
    parser.add_argument("--after", required=True, help="the run whose API work for this model comes first")
    parser.add_argument("--job", action="append", required=True,
                        help="<name>:<setting>[:<seed run>,...], in order; seed runs go to --seed-from")
    parser.add_argument("--rounds", default="20")
    parser.add_argument("--extra", default="", help="more sweep.py run options for every job, e.g. --targets (tests)")
    parser.add_argument("--benchmark", default=sweep.T6_BENCHMARK, help="the benchmark json of the t6_* jobs")
    args = parser.parse_args()
    jobs = [tuple(j.split(":", 1)) for j in args.job]
    seeds = {}
    for i, (name, setting) in enumerate(jobs):
        setting, _, seed = setting.partition(":")
        jobs[i] = (name, setting)
        if seed:
            seeds[name] = seed.split(",")
            for run in seeds[name]:
                assert os.path.exists(os.path.join(sweep.RESULTS_ROOT, run, "responses.jsonl")), run
        assert setting in sweep.SETTINGS, setting
    log(args.model, f"queue: {jobs} after {args.after}; seeds {seeds}; extra {args.extra!r}")
    while not api_finished(args.after, args.model):
        time.sleep(120)
    log(args.model, f"{args.after}: API work done")
    pending = list(jobs)
    while pending:
        ready = [j for j in pending if not sweep.SETTINGS[j[1]][0] or t6_ready(args.benchmark)]
        if not ready:
            time.sleep(300)
            continue
        name, setting = ready[0]
        if not api_finished(name, args.model):
            if not running(name):
                kind = "t6" if sweep.SETTINGS[setting][0] else "t1-5"
                cmd = [os.path.join(os.path.dirname(os.path.abspath(__file__)), "run.sh"), "--name", name,
                       "--models", args.model, "--setting", setting, "--rounds", args.rounds] + OPTIONS[kind] \
                    + (["--benchmark", args.benchmark] if kind == "t6" else []) \
                    + shlex.split(args.extra) + (["--seed-from"] + seeds[name] if name in seeds else [])
                out = subprocess.run(cmd, capture_output=True, text=True)
                log(args.model, f"started {name} ({setting}): {out.stdout.strip()} {out.stderr.strip()}")
            while not api_finished(name, args.model):
                time.sleep(120)
                if not running(name) and not api_finished(name, args.model):
                    log(args.model, f"{name} is not running and not finished; starting it again")
                    break
            else:
                log(args.model, f"{name}: API work done")
                pending.remove((name, setting))
            continue
        pending.remove((name, setting))
    log(args.model, "queue done")


if __name__ == "__main__":
    main()
