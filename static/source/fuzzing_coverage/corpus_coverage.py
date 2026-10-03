"""Coverage reports of every corpus snapshot of a finished campaign (`infra/helper.py corpus-coverage` of the
extended OSS-Fuzz checkout, see incremental_fuzzing.py), then the executed lines per snapshot
(coverage.py) -> <out_dir>/<project>_coverage.json.

    python corpus_coverage.py --oss_fuzz_dir ~/oss-fuzz [--projects bleach clib ...] [--out_dir DIR]
"""
import os
import sys
import argparse
import threading
import subprocess
import concurrent.futures
import time
import logging

from coverage import collect_coverage, dump_coverage, PROJECTS, RESULTS_DIR

logging.basicConfig(level=logging.INFO, format='%(asctime)s [%(levelname)s] %(message)s')

progress = []
progress_lock = threading.Lock()

def gen_covreport(project, oss_fuzz_dir):
    logging.info(f"Generating coverage report for {project}")
    full_cmd = [
                "python3", "infra/helper.py", "corpus-coverage", project]
    try:
        subprocess.run(full_cmd, shell=False, check=True, stdout=sys.stdout, stderr=sys.stderr, cwd=oss_fuzz_dir)
    except Exception as e:
        logging.error(f"An error occurred while generating coverage report for {project}: {e}")
        return

def gen_json(project, oss_fuzz_dir, out_dir):
    coverage = collect_coverage(project, oss_fuzz_dir)
    dump_coverage(coverage, project, out_dir)

def get_coverage(project, oss_fuzz_dir, out_dir):
    logging.info(f"Start processing {project}")
    # Update progress
    with progress_lock:
        progress.append(project)
    gen_covreport(project, oss_fuzz_dir)
    gen_json(project, oss_fuzz_dir, out_dir)

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
    parser.add_argument("--out_dir", default=os.path.join(RESULTS_DIR, "coverage"),
                        help="where <project>_coverage.json goes, one dir per campaign (default: static/results/fht/coverage)")
    args = parser.parse_args()
    max_workers = 1
    monitor_thread = threading.Thread(target=monitor_progress)
    monitor_thread.start()
    with concurrent.futures.ProcessPoolExecutor(max_workers=max_workers) as executor:
        futures = []
        for project in args.projects:
            future = executor.submit(get_coverage, project, os.path.abspath(args.oss_fuzz_dir), os.path.abspath(args.out_dir))
            futures.append(future)
        # Wait for all futures to complete
        concurrent.futures.wait(futures)
    # Wait for the monitor thread to finish
    monitor_thread.join()

if __name__ == "__main__":
    main()
