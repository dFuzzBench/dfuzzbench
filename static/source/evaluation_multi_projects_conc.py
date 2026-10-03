import os
import sys
import subprocess
import argparse
import concurrent.futures
from datetime import datetime

from llm_fuzz_integration import MODELS, NO_CONTEXT_VARIANTS, require_api_key

# Resolve repo paths relative to this file so evaluation runs from any checkout location.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))  # .../source -> static/
SOURCE_DIR = os.path.join(REPO_ROOT, "source")
DATA_ROOT = os.path.join(REPO_ROOT, "data")
DOCKER_UTILS_DIR = os.path.join(REPO_ROOT, "docker-utils")
RESULTS_DIR = os.path.join(REPO_ROOT, "results")

HARNESS_FILES = {  # the 15 benchmark applications
    "bleach": "fuzzer.py",
    "clib": "fuzzer.c",
    "cmark": "fuzzer.c",
    "cpp-httplib": "fuzzer.cc",
    "exiv2": "fuzzer.cpp",
    "filesystem_spec": "fuzzer.py",
    "guetzli": "fuzzer.cc",
    "html5lib-python": "fuzzer.py",
    "lark-parser": "fuzzer.py",
    "libbpf": "fuzzer.c",
    "libpng": "fuzzer.cc",
    "md4c": "fuzzer.c",
    "rich": "fuzzer.py",
    "varnish": "fuzzer.c",
    "wamr": "fuzzer.cc",
}
VIBE_PROJECTS = ["bleach", "html5lib-python", "rich"]


def eval_project(project, args):
    print(f"Evaluating project: {project}")
    dataset = "target-vibe" if args.vibe else "target-latest"
    project_dir = os.path.join(DATA_ROOT, dataset, project, args.retrieval_mode)
    harness_path = os.path.join(DATA_ROOT, dataset, project, "base-env", HARNESS_FILES[project])

    full_cmd = [sys.executable, os.path.join(SOURCE_DIR, "evaluation_project.py"),
                "--project_dir", project_dir, "--harness", harness_path, "--rounds", str(args.rounds),
                "--llm_model", args.llm_model, "--results_dir", args.results_dir]
    if args.random:
        full_cmd.append("--random")
    if args.vibe:
        full_cmd.append("--vibe")
    if args.no_context:
        full_cmd += ["--no_context", args.no_context]
    if args.early_termination:
        full_cmd.append("--early_termination")
    if args.debug:
        full_cmd.append("--debug")

    # this checkout's modules first, whatever docker_utils the interpreter has installed
    env = dict(os.environ, PYTHONPATH=os.pathsep.join(
        [SOURCE_DIR, DOCKER_UTILS_DIR] + ([os.environ["PYTHONPATH"]] if os.environ.get("PYTHONPATH") else [])))
    print(f"Running command: {' '.join(full_cmd)}")
    result = subprocess.run(full_cmd, cwd=SOURCE_DIR, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors="replace")
    if args.debug:
        with open(args.debug_log, "a") as f:
            f.write(f"Project: {project}\n\n")
            f.write(result.stdout)
            f.write("\n\n\n\n\n")
    if result.returncode != 0:
        tail = "\n".join(result.stdout.strip().splitlines()[-20:])
        print(f"An error occurred while evaluating {project} (exit {result.returncode}):\n{tail}")
    else:
        print(f"{project}: " + next((line for line in reversed(result.stdout.splitlines()) if line.startswith("results: ")), "done"))

def parseargs():
    parser = argparse.ArgumentParser(description="Evaluate a model on every project of the benchmark (one process per project)")
    parser.add_argument("--debug", action="store_true", help="Enable debug mode")
    parser.add_argument("--retrieval_mode", type=str, default="realistic", choices=["oracle", "bm25", "realistic"],
                        help="The static context; with --no_context or --random it only selects the target list")
    parser.add_argument("--rounds", type=int, default=1, help="The number of rounds to run")
    parser.add_argument("--llm_model", type=str, help=f"The LLM model to use: {', '.join(MODELS)}")
    parser.add_argument("--early_termination", action="store_true", help="Enable early termination")
    parser.add_argument("--random", action="store_true", help="Random inputs instead of a model (no API key needed)")
    parser.add_argument("--vibe", action="store_true", help="Evaluation vibe coding targets")
    parser.add_argument("--no_context", nargs="?", const="original", choices=NO_CONTEXT_VARIANTS,
                        help="No source context: original (No-source) or placeholder (No-source "
                             "(perturbed): identifiers numbered func1, var1, ...); no value means original")
    parser.add_argument("--projects", nargs="+", choices=list(HARNESS_FILES),
                        help="Projects to evaluate (default: all 15, or the 3 vibe projects)")
    parser.add_argument("--results_dir", default=RESULTS_DIR, help="Where results go (default: static/results)")
    args = parser.parse_args()
    if args.random and args.llm_model is None:
        args.llm_model = "random"
    if args.llm_model is None:
        parser.error("--llm_model is required (unless --random)")
    if not args.random:
        if args.llm_model not in MODELS:
            parser.error(f"unknown --llm_model {args.llm_model}; use one of {', '.join(MODELS)}")
        require_api_key(args.llm_model)
    args.results_dir = os.path.abspath(args.results_dir)
    return args


def main():
    args = parseargs()
    # One debug log per orchestrator, so concurrent sweeps (e.g. different models) don't interleave.
    os.makedirs(args.results_dir, exist_ok=True)
    args.debug_log = os.path.join(args.results_dir, f"eval_concurrent-{args.retrieval_mode}-{args.llm_model}-{datetime.now():%Y-%m-%d_%H-%M-%S}-{os.getpid()}.log")

    if args.projects:
        projects = args.projects
    elif args.vibe:
        projects = VIBE_PROJECTS
    else:
        projects = list(HARNESS_FILES)

    max_workers = min(28, os.cpu_count() or 1)
    with concurrent.futures.ProcessPoolExecutor(max_workers=max_workers) as executor:
        futures = []
        for project in projects:
            future = executor.submit(eval_project, project, args)
            futures.append(future)
        concurrent.futures.wait(futures)
    for project, future in zip(projects, futures):
        if future.exception() is not None:
            print(f"An error occurred while evaluating {project}: {future.exception()!r}")

if __name__ == "__main__":
    main()
