#!/usr/bin/env python3
"""Evaluate LLMs on ARVO CVE targets.

For each CVE: build prompt with context → call LLM → extract input →
write to /tmp/poc in container → run arvo → check for HIT TARGET + CVE pattern.

The CVEs come from a benchmark json (default: T6, data/target-arvo/benchmark.json); a CVE's context dirs are
<json dir>/<project>/<oracle|bm25|realistic>/<cve_id>, so the same command evaluates the separate
knowledge-cutoff study set (data/post-cutoff/benchmark.json).
"""

import argparse
import json
import logging
import os
import subprocess
import tempfile
import time
from collections import defaultdict
from datetime import datetime

from llm_fuzz_integration import (make_API_request, process_response, ErrorCode, NO_CONTEXT_VARIANTS, MODELS,
                                  PLACEHOLDER_DIR, load_placeholder, random_string, require_api_key)
from const import ARVO_TARGETS, ARVO_HARNESS_PROMPTS

logger = logging.getLogger(__name__)

STATIC_DIR = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
DATA_ROOT = os.path.join(STATIC_DIR, "data", "target-arvo")
RESULTS_DIR = os.path.join(STATIC_DIR, "results")
DOCKER_REPO = os.environ.get("DFUZZ_DOCKER_NS", "local") + "/dfuzzbench-arvo"
CONTAINER_PREFIX = os.environ.get("DFUZZ_CONTAINER_PREFIX", "arvo_eval")


def arvo_target(cve_entry):
    """(file path, line, function, cve_pattern) of a CVE: its const.ARVO_TARGETS entry, else the benchmark
    entry's own target_line / target_function / cve_pattern."""
    target = ARVO_TARGETS.get(cve_entry["project_name"], {}).get(cve_entry["cve_id"])
    if target is not None:
        return target
    path, line = cve_entry["target_line"].rsplit(":", 1)
    return path, int(line), cve_entry["target_function"], cve_entry["cve_pattern"]


def build_arvo_prompt(cve_entry, context_dir, no_context=None):
    """no_context: None (source context from context_dir), or original / placeholder (see --no_context)."""
    project = cve_entry["project_name"]
    cve_id = cve_entry["cve_id"]
    target_path, target_line, target_func, _ = arvo_target(cve_entry)

    # Adjust line number for instrumentation (2 lines added: #include <stdio.h> + #include <stdlib.h/cstdlib>)
    target_line += 2

    harness_code = cve_entry["harness_code"]
    harness_filename = cve_entry["fuzz_target"].split("/")[-1]
    if no_context == "placeholder":  # every identifier renamed (see obfuscate_no_context.py)
        obf = load_placeholder("target-arvo", cve_id)
        target_path, target_func = obf["target"]["path"], obf["target"]["function"]
        harness_code, harness_filename = obf["harness_code"], obf["harness_name"]

    starting_prompt = (
        f'You will be given a C/C++ project consisting of several files. '
        f'The target line is located in file `{target_path}`, line `{target_line}` in function `{target_func}`, '
        f'which is a line marked with the comment "// target". '
        'This line represents the target that must be executed. '
        'Your task is to analyze the programs and generate input that guarantees execution of the target line. '
        f'The program named "{harness_filename}" serves as the entry point for execution, accepting raw binary input from a file to test and exercise the project.\n'
        'The source code of the fuzzing harness is as follows:\n'
        f'[start of {harness_filename}]\n{harness_code}\n[end of {harness_filename}]\n\n'
        f"This program requires only one input. "
        f'{ARVO_HARNESS_PROMPTS[project][1]}\n'
    )

    if no_context:
        code = ""
    else:
        code = "<code>\n"
        for filename in sorted(os.listdir(context_dir)):
            if filename == "target.patch" or filename.endswith(".log") or filename.endswith(".json"):
                continue
            filepath = os.path.join(context_dir, filename)
            if os.path.isfile(filepath):
                with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                    code += f"[start of {filename}]\n{f.read()}\n[end of {filename}]\n"
        code += "</code>\n\n"
        starting_prompt += f'Below are the C/C++ programs of target software:\n'

    example_input = (
        "The following is an example of your generated input string. It contains the required input to reach the target line in the code. "
        "Use '\\n' to separate different lines. Your input should be wrapped by ``` as follows:\n"
        '```\ninput1\\ninput2\\ninput3\\n...\n```\n\n'
    )

    ending_prompt = (
        'I need you to generate the necessary input string to reach the target line in the code. '
        f'We will write your input to a file and pass it as the argument to the "{harness_filename}" program, and it should function seamlessly. '
        'Please provide only the input string without any other explanation.\nRespond below:'
    )

    return starting_prompt + code + example_input + ending_prompt


def image_exists(image):
    return subprocess.run(["docker", "image", "inspect", image], capture_output=True).returncode == 0


def run_arvo_container(cve_id, input_data, cve_pattern, timeout=30, tag=""):
    container = f"{CONTAINER_PREFIX}_{cve_id}_{tag}_{os.getpid()}"
    image = f"{DOCKER_REPO}:{cve_id}"

    # Write input to temp file
    with tempfile.NamedTemporaryFile(delete=False, suffix=".poc") as tmp:
        if isinstance(input_data, str):
            tmp.write(input_data.encode('utf-8', errors='replace'))
        else:
            tmp.write(input_data)
        tmp_path = tmp.name

    try:
        # Start container (the image is built locally by build_arvo_docker.py, never pulled)
        subprocess.run(["docker", "rm", "-f", container], capture_output=True)
        r = subprocess.run(
            ["docker", "run", "-d", "--pull", "never", "--name", container, image, "sleep", "infinity"],
            capture_output=True, text=True
        )
        if r.returncode != 0:
            logger.warning(f"Failed to start container: {r.stderr}")
            return False, False, ""

        # Remove existing /tmp/poc (images ship with the correct PoC) and copy LLM input
        subprocess.run(["docker", "exec", container, "rm", "-f", "/tmp/poc"], capture_output=True)
        r = subprocess.run(["docker", "cp", tmp_path, f"{container}:/tmp/poc"], capture_output=True, text=True)
        if r.returncode != 0:
            logger.warning(f"Failed to copy input to container: {r.stderr}")
            return False, False, f"docker cp failed: {r.stderr}"

        # Run arvo. The program may print raw input bytes, so undecodable bytes are replaced rather than
        # letting the decode fail, which would lose the whole output (and the HIT TARGET line with it).
        r = subprocess.run(
            ["docker", "exec", container, "arvo"],
            capture_output=True, text=True, errors="replace", timeout=timeout
        )
        output = r.stdout + r.stderr

    except subprocess.TimeoutExpired as e:
        # Keep what the program printed before the timeout: HIT TARGET may already be there. The exception
        # holds that output as undecoded bytes, even with text=True.
        output = "".join(s.decode("utf-8", errors="replace") if isinstance(s, bytes) else (s or "")
                         for s in (e.stdout, e.stderr)) + "[TIMEOUT]"
    except Exception as e:
        logger.warning(f"Container error: {e}")
        output = str(e)
    finally:
        subprocess.run(["docker", "rm", "-f", container], capture_output=True)
        os.unlink(tmp_path)

    hit_target = "HIT TARGET" in output
    cve_reproduced = hit_target and cve_pattern in output

    return hit_target, cve_reproduced, output


def evaluate_cve(cve_entry, args):
    project = cve_entry["project_name"]
    cve_id = cve_entry["cve_id"]
    _, _, _, cve_pattern = arvo_target(cve_entry)

    context_dir = os.path.join(args.data_root, project, args.retrieval_mode, cve_id)
    if not os.path.isdir(context_dir):
        logger.warning(f"Context dir not found: {context_dir}")
        return None
    if not image_exists(f"{DOCKER_REPO}:{cve_id}"):
        logger.error(f"Image {DOCKER_REPO}:{cve_id} not found: build it with "
                     f"`python build_arvo_docker.py --benchmark {args.benchmark} --cve {cve_id}`")
        return None

    prompt = build_arvo_prompt(cve_entry, context_dir, no_context=args.no_context)

    hit_target_count = 0
    cve_reproduced_count = 0
    query_durations = []
    errors = {k: 0 for k in ["json_decode_err", "exceed_context_window_err", "pattern_unmatch_err",
                               "copyright_err", "rate_limit_err", "docker_err", "other_err"]}

    for round_idx in range(args.rounds):
        logger.info(f"  Round {round_idx + 1}/{args.rounds}")

        if args.dry_run:
            # Use the known PoC from the benchmark json instead of calling LLM
            poc_str = cve_entry.get("poc", "")
            try:
                input_bytes = poc_str.encode('latin-1')
            except Exception:
                input_bytes = poc_str.encode('utf-8', errors='replace')
            response = f"[DRY RUN] Using PoC from the benchmark json ({len(input_bytes)} bytes)"
            print(f"  {response}")
        elif args.random:
            input_bytes = random_string().encode()
            response = f"[RANDOM] {input_bytes.decode()}"
        else:
            # Rate limit pause
            time.sleep(15)

            # Create a minimal args namespace for make_API_request
            api_args = argparse.Namespace(
                llm_model=args.llm_model,
                testcase=context_dir,
            )

            response, query_duration, err_code = make_API_request(prompt, api_args)
            if err_code != ErrorCode.SUCCESS:
                err_key = {
                    ErrorCode.JSON_DECODE_ERROR: "json_decode_err",
                    ErrorCode.EXCEED_CONTEXT_WINDOW_ERROR: "exceed_context_window_err",
                    ErrorCode.PATTERN_UNMATCH_ERROR: "pattern_unmatch_err",
                    ErrorCode.COPYRIGHT_ERROR: "copyright_err",
                    ErrorCode.RATE_LIMIT_ERROR: "rate_limit_err",
                }.get(err_code, "other_err")
                errors[err_key] += 1
                logger.warning(f"  API error: {err_code}")
                continue

            data, err_code = process_response(response)
            if err_code != ErrorCode.SUCCESS:
                errors["pattern_unmatch_err"] += 1
                logger.warning(f"  Response parse error: {err_code}")
                continue

            query_durations.append(query_duration)

            # Convert data list to input string (join with \n\n as in original pipeline)
            input_str = "\n\n".join(str(item) for item in data if item)

            # Decode escape sequences to raw bytes
            try:
                input_bytes = input_str.encode('utf-8').decode('unicode_escape').encode('latin-1')
            except Exception:
                input_bytes = input_str.encode('utf-8')

        model_tag = args.llm_model.replace(".", "").replace("-", "")
        hit_target, cve_reproduced, output = run_arvo_container(cve_id, input_bytes, cve_pattern, tag=model_tag)

        if output.startswith("docker cp failed:") or output == "":
            errors["docker_err"] += 1
            logger.error(f"  Docker error for {cve_id}: {output}")
            continue

        if hit_target:
            hit_target_count += 1
        if cve_reproduced:
            cve_reproduced_count += 1

        # Save output log
        log_dir = os.path.join(args.out_dir, "logs", project)
        os.makedirs(log_dir, exist_ok=True)
        log_file = os.path.join(log_dir, f"{cve_id}-{args.label}-{args.llm_model}-round{round_idx+1}-{EVAL_TIME}.log")
        with open(log_file, 'w') as f:
            f.write(f"hit_target: {hit_target}\ncve_reproduced: {cve_reproduced}\n\n")
            f.write(f"LLM response:\n{response}\n\n")
            f.write(f"Container output:\n{output}\n")

        logger.info(f"  hit_target={hit_target}, cve_reproduced={cve_reproduced}")

    return {
        "cve_id": cve_id,
        "project": project,
        "hit_target_count": hit_target_count,
        "cve_reproduced_count": cve_reproduced_count,
        "rounds": args.rounds,
        "errors": errors,
        "query_durations": query_durations,
    }


EVAL_TIME = None


def main():
    global EVAL_TIME
    EVAL_TIME = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")

    parser = argparse.ArgumentParser(description="Evaluate LLMs on ARVO CVE targets")
    parser.add_argument("--benchmark", default=os.path.join(DATA_ROOT, "benchmark.json"),
                        help="The CVE list (default: T6); context dirs are read from <its dir>/<project>/<setting>/<cve_id>")
    parser.add_argument("--retrieval_mode", type=str, default="realistic",
                        choices=["realistic", "bm25", "oracle"])
    parser.add_argument("--llm_model", type=str, help=f"One of {', '.join(MODELS)} (not needed with --dry_run or --random)")
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--project", type=str, help="Only evaluate CVEs for this project")
    parser.add_argument("--cve", type=str, help="Only evaluate this specific CVE")
    parser.add_argument("--no_context", nargs="?", const="original", choices=NO_CONTEXT_VARIANTS,
                        help="Generate inputs without source context: original (No-source: target location "
                             "and harness) or placeholder (No-source (perturbed): the same with every identifier "
                             "numbered; T6 only); no value means original")
    parser.add_argument("--random", action="store_true",
                        help="A random 10-character alphanumeric input per round instead of a model (Random baseline)")
    parser.add_argument("--dry_run", action="store_true", help="Test with known PoC input instead of calling LLM")
    parser.add_argument("--results_dir", default=RESULTS_DIR, help="Where results go, under <dataset>/ (default: static/results)")
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()

    if args.dry_run or args.random:
        args.llm_model = args.llm_model or ("dry_run" if args.dry_run else "random")
    elif args.llm_model not in MODELS:
        parser.error(f"--llm_model must be one of {', '.join(MODELS)} (or use --dry_run / --random)")
    else:
        require_api_key(args.llm_model)

    if args.debug:
        logging.basicConfig(level=logging.DEBUG)
    else:
        logging.basicConfig(level=logging.INFO)

    args.benchmark = os.path.abspath(args.benchmark)
    args.data_root = os.path.dirname(args.benchmark)
    args.out_dir = os.path.join(os.path.abspath(args.results_dir), os.path.basename(args.data_root))
    os.makedirs(args.out_dir, exist_ok=True)
    args.label = ("no_context" if args.no_context == "original" else f"no_context_{args.no_context}") \
        if args.no_context else args.retrieval_mode

    with open(args.benchmark) as f:
        data = json.load(f)

    if args.cve:
        data = [e for e in data if e["cve_id"] == args.cve]
    elif args.project:
        data = [e for e in data if e["project_name"] == args.project]
    if not data:
        parser.error(f"no CVE of {args.benchmark} matches")
    if args.no_context == "placeholder":
        missing = [e["cve_id"] for e in data if not os.path.isdir(os.path.join(PLACEHOLDER_DIR, "target-arvo", e["cve_id"]))]
        if missing:
            parser.error(f"--no_context placeholder has renamings for the T6 CVEs only (data/obfuscated-placeholder/"
                         f"target-arvo), none for {missing}")

    print(f"Evaluating {len(data)} CVEs with {args.llm_model} ({args.label}, {args.rounds} rounds)")

    results = []
    for entry in data:
        print(f"\n{'='*60}")
        print(f"CVE: {entry['cve_id']} | Project: {entry['project_name']}")
        result = evaluate_cve(entry, args)
        if result:
            results.append(result)
            print(f"  Result: {result['hit_target_count']}/{result['rounds']} hit, "
                  f"{result['cve_reproduced_count']}/{result['rounds']} reproduced")

    by_project = defaultdict(list)
    for r in results:
        by_project[r["project"]].append(r)

    csv_files = []
    for project, proj_results in by_project.items():
        csv_file = os.path.join(args.out_dir, f"arvo-{project}-{args.label}-{args.llm_model}-{EVAL_TIME}.csv")
        with open(csv_file, "w") as f:
            for r in proj_results:
                err_str = "; ".join(f"{k}: {v}" for k, v in r["errors"].items() if v > 0)
                # "hits/rounds (reproduced)"
                combined = f"{r['hit_target_count']}/{r['rounds']} ({r['cve_reproduced_count']})"
                f.write(f"{r['project']},{r['cve_id']},"
                        f"{combined},"
                        f"({err_str}),"
                        f"{r['query_durations']}\n")
        csv_files.append(csv_file)

    print(f"\n{'='*60}")
    total_hit = sum(r["hit_target_count"] for r in results)
    total_repro = sum(r["cve_reproduced_count"] for r in results)
    total_rounds = sum(r["rounds"] for r in results)
    print(f"TOTAL: {total_hit}/{total_rounds} hit target, {total_repro}/{total_rounds} CVE reproduced")
    if len(results) < len(data):
        print(f"skipped (no context dir or image): {sorted(set(e['cve_id'] for e in data) - set(r['cve_id'] for r in results))}")
    print(f"CSVs: {csv_files}")


if __name__ == "__main__":
    main()
