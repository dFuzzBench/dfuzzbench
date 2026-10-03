import argparse
import glob
import logging
import os
import re
from datetime import datetime
import shutil
import hashlib
import time
from docker_utils import build_image, docker_target_check_new, build_fuzzer, cleanup_run
from llm_fuzz_integration import gen_instrumented_fuzzer, ErrorCode, NO_CONTEXT_VARIANTS, MODELS, require_api_key

EVAL_TIME = None
RESULTS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), "results")
SETTINGS = ("oracle", "bm25", "realistic", "callgraph")
LEVELS = ["easy", "medium", "hard", "extreme_hard", "unreachable"]  # tiers T1-T5
LOG_HEAD, LOG_TAIL = 256 * 1024, 64 * 1024  # bytes of each round's fuzzer log kept in the exec-env

logger = logging.getLogger(__name__)

def parse_args():
    parser = argparse.ArgumentParser(description="A tool to assess whether LLMs can generate program inputs that trigger the execution of a specified target line.")
    parser.add_argument("--debug", action="store_true", help="Enable debug logging")
    parser.add_argument('--project_dir', type=str, required=True, help='The setting dir of a project, e.g. ../data/target-latest/clib/realistic.')
    parser.add_argument('--harness', type=str, help='The harness that will be instrumented (default: the fuzzer.* file in the project\'s base-env).')
    parser.add_argument('--rounds', type=int, default=1, help='The number of rounds to ask the LLM for input and verify the correctness.')
    parser.add_argument('--llm_model', type=str, required=True, help=f'The LLM model to use: {", ".join(MODELS)}; with --random only a label (random).')
    parser.add_argument('--early_termination', action="store_true",help='Enable early termination of the evaluation process (stop once it reaches the target).')
    parser.add_argument('--random', action="store_true", help='Use random input instead of LLM-generated input.')
    parser.add_argument('--vibe', action="store_true", help='Evaluation vibe-coding targets.')
    parser.add_argument('--no_context', nargs='?', const='original', choices=NO_CONTEXT_VARIANTS,
                        help='Generate inputs without any source code of the project: original (No-source: the '
                             'project, target location and harness) or placeholder (No-source (perturbed): the same with '
                             'every identifier replaced by a numbered placeholder). No value means original.')
    parser.add_argument('--targets', nargs='+', help='Only these targets, as <level>/<target dir> or <target dir>, e.g. easy/src:blocks.c:144.')
    parser.add_argument('--results_dir', default=RESULTS_DIR, help='Where result files and exec-env dirs go, under <dataset>/ (default: static/results).')

    args = parser.parse_args()

    if os.path.basename(os.path.normpath(args.project_dir)) not in SETTINGS or not os.path.isdir(args.project_dir):
        parser.error(f"--project_dir must be a project's {'/'.join(SETTINGS)} dir")
    if args.llm_model not in MODELS and args.llm_model != "random":
        parser.error(f"unknown --llm_model {args.llm_model}; use one of {', '.join(MODELS)}")
    if args.llm_model == "random" and not args.random:
        parser.error("--llm_model random is only a label for --random runs (no LLM is called).")
    if not args.random:
        require_api_key(args.llm_model)
    if args.harness is None:
        base_env = os.path.join(os.path.dirname(os.path.normpath(args.project_dir)), "base-env")
        harnesses = glob.glob(os.path.join(base_env, "fuzzer.*"))
        if len(harnesses) != 1:
            parser.error(f"pass --harness: {base_env} has no single fuzzer.* file")
        args.harness = harnesses[0]

    if args.debug:
        logging.basicConfig(level=logging.DEBUG)
    else:
        logging.basicConfig(level=logging.INFO)

    return args

def run_label(args):
    """Names this run's exec-env dir and result files: the retrieval mode of --project_dir, or no_context /
    no_context_placeholder."""
    if args.no_context:
        return "no_context" if args.no_context == "original" else f"no_context_{args.no_context}"
    return args.project_dir.strip("/").split("/")[-1]

def run_dir(args):
    """--results_dir/<dataset>, where a command-line run writes its result files and exec-env dirs. Callers
    that set no out_dir (exp_gemini/sweep.py) keep the earlier places: result files in the cwd, exec-env dirs
    beside the project's data."""
    return getattr(args, "out_dir", None)

def exec_env_root(args, project_base_dir):
    return os.path.join(run_dir(args), os.path.basename(project_base_dir)) if run_dir(args) else project_base_dir

def result_file(args, ext):
    project_name = args.project_dir.strip("/").split("/")[-2]
    return os.path.join(run_dir(args) or "", f"{project_name}-{run_label(args)}-{args.llm_model}-{EVAL_TIME}.{ext}")

def preprocess_dir(args):
    if not os.path.isdir(args.dir):
        raise NotADirectoryError(f"The provided path {args.dir} is not a valid directory.")

    # clean the exec-env dir
    retrieval_mode = run_label(args)

    tag = args.dir.rstrip('/').split('/')[-2] + '-' + args.dir.rstrip('/').split('/')[-1]
    project_base_dir = "/".join(args.dir.rstrip("/").split("/")[:-3])
    logger.debug(f"Project base directory: {project_base_dir}")
    exec_env_dir = os.path.join(exec_env_root(args, project_base_dir), f"exec-env-{retrieval_mode}-{args.llm_model}-{EVAL_TIME}", f"{tag}")
    logger.debug(f"exec_env_dir: {exec_env_dir}")
    try:
        os.rmdir(exec_env_dir)
    except OSError as e:
        logger.debug(f"Warning: Removing {exec_env_dir} failed. Probably it doesn't exist.")

    os.makedirs(exec_env_dir, exist_ok=True)

    # copy the files to the exec-env dir
    base_env_dir = os.path.join(project_base_dir, "base-env")
    for file in os.listdir(base_env_dir):
        if "fuzzer_instrumented" in file:
            continue
        shutil.copy(os.path.join(base_env_dir, file), exec_env_dir)
    for file in os.listdir(args.dir):
        shutil.copy(os.path.join(args.dir, file), exec_env_dir)

    args.dir = exec_env_dir

def prepare_args_for_instrumentation(args):
    args = argparse.Namespace(
    debug=args.debug,
    no_context=args.no_context,
    testcase=args.dir,
    harness=args.harness,
    llm_model=args.llm_model,
    random=args.random,
    vibe=args.vibe,
    )
    return args

def docker_run_id(llm_model, exec_env_dir):
    """Image tag / out-dir name unique to this exec-env (i.e. to this target under this run)."""
    model = re.sub(r"[^a-z0-9_.-]", "_", llm_model.lower())
    return f"{model}-{hashlib.sha1(os.path.abspath(exec_env_dir).encode()).hexdigest()[:12]}"

def run_fuzzer(args):
    project_name = args.dir.rstrip("/").split("/")[-3]
    run_id = docker_run_id(args.llm_model, args.dir)
    try:
        return run_fuzzer_isolated(args, project_name, run_id)
    finally:
        cleanup_run(project_name, run_id)

def keep_log(src, dest):
    """Copies a round's fuzzer log, keeping its first LOG_HEAD and last LOG_TAIL bytes: the harness runs the same
    input for up to 600 s, and some projects print on every run, so a log can reach gigabytes."""
    size = os.path.getsize(src)
    with open(src, "rb") as f, open(dest, "wb") as out:
        if size <= LOG_HEAD + LOG_TAIL:
            shutil.copyfileobj(f, out)
            return
        out.write(f.read(LOG_HEAD))
        out.write(f"\n...[{size - LOG_HEAD - LOG_TAIL} bytes cut]...\n".encode())
        f.seek(size - LOG_TAIL)
        out.write(f.read())

def run_fuzzer_isolated(args, project_name, run_id):
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    #0. keep a copy of this round's instrumented fuzzer (written into the exec-env by gen_instrumented_fuzzer)
    exec_env_dir = args.dir
    for file in os.listdir(exec_env_dir):
        if file.startswith("fuzzer_instrumented"):
            shutil.copy(os.path.join(exec_env_dir, file), f"{exec_env_dir}/{timestamp}-{file}")

    # 1. build the docker image
    project_dir = args.dir
    logger.debug(f"Project name: {project_name}")
    logger.debug(f"Project directory: {project_dir}")
    if not build_image(project_dir, run_id):
        with open(f"{exec_env_dir}/{timestamp}-fuzzer_output.log", 'w') as f:
            f.write(f"[{project_name}] build image failed\n")
        logger.warning(f"[{project_name}] build image failed")
        return False

    # 2. run the program with the input content inside the docker container
    engine = 'libfuzzer'
    if args.harness.endswith(".py"):
        language = 'python'
    else:
        language = 'c'
    if not build_fuzzer(project_name, engine, language, run_id=run_id):
        with open(f"{exec_env_dir}/{timestamp}-fuzzer_output.log", 'w') as f:
            f.write(f"[{project_name}] build fuzzer failed\n")
        logger.warning(f"[{project_name}] build fuzzer failed")
        return False

    try:
        hit_target, log_path = docker_target_check_new(project_name, engine, language, run_id)
    except Exception as e:
        logger.warning(f"[{project_name}] docker_target_check_new failed: {e}")
        return False
    # 3. copy the instrumented fuzzer execution log to the exec-env
    keep_log(log_path, f"{exec_env_dir}/{timestamp}-fuzzer_output.log")

    return hit_target


def save_record(args, hit_target_count, executed_rounds, errors, result, query_durations):
    target_info = args.dir.rstrip("/").split("/")[-1]
    level, _, target = target_info.partition('-')

    if level not in result:
        result[level] = {}

    result[level][target] = {
        "hit_target_count": hit_target_count,
        "executed_rounds": executed_rounds,
        "json_decode_err": errors["json_decode_err"],
        "exceed_context_window_err": errors["exceed_context_window_err"],
        "pattern_unmatch_err": errors["pattern_unmatch_err"],
        "incompatible_data_err": errors["incompatible_data_err"],
        "copyright_err": errors["copyright_err"],
        "rate_limit_err": errors["rate_limit_err"],
        "other_err": errors["other_err"],
        "query_durations": query_durations
    }


def error_string(errors):
    err_str = ""
    if errors["json_decode_err"] > 0:
        err_str += f"json_decode_error: {errors['json_decode_err']}; "
    if errors["exceed_context_window_err"] > 0:
        err_str += f"exceed_context_window: {errors['exceed_context_window_err']}; "
    if errors["pattern_unmatch_err"] > 0:
        err_str += f"pattern_unmatch_err: {errors['pattern_unmatch_err']}; "
    if errors["incompatible_data_err"] > 0:
        err_str += f"incompatible_data_err: {errors['incompatible_data_err']}; "
    if errors["copyright_err"] > 0:
        err_str += f"copyright_err: {errors['copyright_err']}; "
    if errors["rate_limit_err"] > 0:
        err_str += f"rate_limit_err: {errors['rate_limit_err']}; "
    if errors["other_err"] > 0:
        err_str += f"other_err: {errors['other_err']}; "
    return err_str


def write_to_file(args, hit_target_count, executed_rounds, errors, query_durations):
    err_str = error_string(errors)
    with open(result_file(args, "txt"), "a") as f:
        f.write(f"{args.dir}: {hit_target_count}/{executed_rounds} = {hit_target_count/executed_rounds * 100:.2f}% ({err_str})\tquery_durations(/s): {query_durations}\n")

def write_to_csv(args, result):
    """One line per target: <level>,<target>,<hits>/<rounds> (<errors>),<query durations>."""
    for level in result:
        result[level] = dict(sorted(result[level].items(), key=lambda x: (":".join(x[0].split(":")[:-1]), int(x[0].split(":")[-1]))))

    csv_file = result_file(args, "csv")
    with open(csv_file, "w", newline="") as f:
        for level in LEVELS:
            if level not in result:
                continue

            for target in result[level]:
                hit_target_count = result[level][target]["hit_target_count"]
                executed_rounds = result[level][target]["executed_rounds"]
                err_str = error_string(result[level][target])
                f.write(f"{level},{target},{hit_target_count}/{executed_rounds} ({err_str}),{result[level][target]['query_durations']}\n")
    return csv_file

def process_dir(args, result):
    preprocess_dir(args)
    logger.info(f"evaluating testcase: {args.dir}")

    query_durations = []
    hit_target_count = 0
    json_decode_err = 0
    exceed_context_window_err = 0
    pattern_unmatch_err = 0
    incompatible_data_err = 0
    copyright_err = 0
    rate_limit_err = 0
    other_err = 0
    executed_rounds = args.rounds

    target_info = args.dir.split("/")[-1].split("-", 1)[-1]
    difficulty = args.dir.split("/")[-1].split("-")[0]
    target_line = int(target_info.split(":")[-1])
    target_path = "/".join(target_info.split(":")[:-1])
    target = (difficulty, target_path, target_line)

    for i in range(args.rounds):
        err_code, query_duration = gen_instrumented_fuzzer(prepare_args_for_instrumentation(args), target)
        if err_code == ErrorCode.JSON_DECODE_ERROR:
            json_decode_err += 1
            logger.warning("JSON decode error. Skipping this round.")
            continue
        elif err_code == ErrorCode.EXCEED_CONTEXT_WINDOW_ERROR:
            exceed_context_window_err += 1
            logger.warning("Exceed context window error. Skipping this round.")
            continue
        elif err_code == ErrorCode.PATTERN_UNMATCH_ERROR:
            pattern_unmatch_err += 1
            logger.warning("Pattern unmatch error. Skipping this round.")
            continue
        elif err_code == ErrorCode.INCOMPATIBLE_DATA_ERROR:
            incompatible_data_err += 1
            logger.warning("Incompatible data error. Skipping this round.")
            continue
        elif err_code == ErrorCode.COPYRIGHT_ERROR:
            copyright_err += 1
            logger.warning("Copyright error. Skipping this round.")
            continue
        elif err_code == ErrorCode.RATE_LIMIT_ERROR:
            rate_limit_err += 1
            logger.warning("Rate limit error. Skipping this round.")
            continue
        elif err_code == ErrorCode.OTHER_ERROR:
            other_err += 1
            logger.warning("Other error. Skipping this round.")
            continue

        assert err_code == ErrorCode.SUCCESS
        query_durations.append(query_duration)
        hit_target = run_fuzzer(args)
        if hit_target:
            hit_target_count += 1
            if args.early_termination:
                executed_rounds = i + 1
                break
    errors = {
        "json_decode_err": json_decode_err,
        "exceed_context_window_err": exceed_context_window_err,
        "pattern_unmatch_err": pattern_unmatch_err,
        "incompatible_data_err": incompatible_data_err,
        "copyright_err": copyright_err,
        "rate_limit_err": rate_limit_err,
        "other_err": other_err
    }
    write_to_file(args, hit_target_count, executed_rounds, errors, query_durations)
    save_record(args, hit_target_count, executed_rounds, errors, result, query_durations)

def claim_eval_time(args):
    """
    Pick the EVAL_TIME that names this run's exec-env root and result files, claiming both
    atomically (mkdir / O_EXCL). Concurrent runs of the same project, even with the same model
    and setting started in the same second, therefore never share a directory or result file.
    """
    global EVAL_TIME
    retrieval_mode = run_label(args)
    root = exec_env_root(args, os.path.dirname(args.project_dir.rstrip("/")))
    os.makedirs(root, exist_ok=True)
    while True:
        EVAL_TIME = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        exec_env = os.path.join(root, f"exec-env-{retrieval_mode}-{args.llm_model}-{EVAL_TIME}")
        try:
            os.mkdir(exec_env)
        except FileExistsError:
            time.sleep(1)
            continue
        try:
            os.close(os.open(result_file(args, "txt"), os.O_CREAT | os.O_EXCL | os.O_WRONLY))
        except FileExistsError:
            os.rmdir(exec_env)
            time.sleep(1)
            continue
        return EVAL_TIME

def main():
    args = parse_args()
    dirs = [os.path.join(args.project_dir, d) for d in LEVELS if os.path.isdir(os.path.join(args.project_dir, d))]
    dirs = [os.path.join(d, f) for d in dirs for f in sorted(os.listdir(d)) if os.path.isdir(os.path.join(d, f))]
    if args.targets:
        wanted = set(args.targets)
        dirs = [d for d in dirs if os.path.basename(d) in wanted or "/".join(d.split("/")[-2:]) in wanted]
        if not dirs:
            raise SystemExit(f"none of {args.targets} is a target of {args.project_dir}")

    dataset = os.path.basename(os.path.dirname(os.path.dirname(os.path.abspath(args.project_dir))))
    args.out_dir = os.path.join(os.path.abspath(args.results_dir), dataset)
    claim_eval_time(args)

    result = {}
    for d in dirs:
        args.dir = d
        process_dir(args, result)

    print(f"results: {write_to_csv(args, result)}")

if __name__ == "__main__":
    main()
