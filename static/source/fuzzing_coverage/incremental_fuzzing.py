"""Undirected libFuzzer campaigns (one week by default) that snapshot coverage as they go, one per project:
the campaigns behind the first-hit times. Runs `infra/helper.py coverage-new` of an OSS-Fuzz checkout that has
that command (an extension of OSS-Fuzz's helper, not part of upstream OSS-Fuzz or of this artifact).

    python incremental_fuzzing.py --oss_fuzz_dir ~/oss-fuzz [--projects bleach clib ...]
"""
import os
import sys
import argparse
import threading
import subprocess
import concurrent.futures
import time
import logging

# the 15 benchmark applications
PYTHON_PROJECTS = ["bleach", "filesystem_spec", "html5lib-python", "lark-parser", "rich"]
C_CPP_PROJECTS = ["clib", "cmark", "cpp-httplib", "exiv2", "guetzli", "libbpf", "libpng", "md4c", "varnish", "wamr"]
PROJECTS = PYTHON_PROJECTS + C_CPP_PROJECTS

logging.basicConfig(level=logging.INFO, format='%(asctime)s [%(levelname)s] %(message)s')

progress = []
progress_lock = threading.Lock()

def run_fuzzer(project, oss_fuzz_dir, fuzzing_time):
    logging.info(f"Running fuzzer for {project} at {fuzzing_time} seconds")
    full_cmd = [
                "python3", "infra/helper.py", "coverage-new",
                "--seconds", str(fuzzing_time), project]
    try:
        subprocess.run(full_cmd, shell=False, check=True, stdout=sys.stdout, stderr=sys.stderr, cwd=oss_fuzz_dir)
    except Exception as e:
        logging.error(f"An error occurred while fuzzing {project}: {e}")
        return

def fuzzing(project, oss_fuzz_dir, fuzzing_time):
    logging.info(f"Fuzzing {project} started")
    # Update progress
    with progress_lock:
        progress.append(project)
    # start fuzzing the project
    run_fuzzer(project, oss_fuzz_dir, fuzzing_time)

    # Remove project from progress
    with progress_lock:
        if project in progress:
            progress.remove(project)
    logging.info(f"Fuzzing {project} terminated")

def monitor_progress():
    time.sleep(10)
    while True:
        with progress_lock:
            if not progress:
                logging.info("All projects completed.")
                break
            else:
                if len(progress) != 0:
                    logging.info(f"Projects in progress: {progress}")
        time.sleep(60)

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--oss_fuzz_dir", default=os.environ.get("OSS_FUZZ_DIR"), required=not os.environ.get("OSS_FUZZ_DIR"),
                        help="the OSS-Fuzz checkout (default: $OSS_FUZZ_DIR)")
    parser.add_argument("--projects", nargs="+", default=PROJECTS)
    parser.add_argument("--seconds", type=int, default=60 * 60 * 24 * 7 + 30 * 60, help="campaign length")
    args = parser.parse_args()
    max_workers = min(27, os.cpu_count() or 1)
    monitor_thread = threading.Thread(target=monitor_progress)
    monitor_thread.start()
    with concurrent.futures.ProcessPoolExecutor(max_workers=max_workers) as executor:
        futures = []
        for project in args.projects:
            future = executor.submit(fuzzing, project, os.path.abspath(args.oss_fuzz_dir), args.seconds)
            futures.append(future)
        # Wait for all futures to complete
        concurrent.futures.wait(futures)
    # Wait for the monitor thread to finish
    monitor_thread.join()

if __name__ == "__main__":
    main()
