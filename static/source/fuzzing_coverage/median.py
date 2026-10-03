"""Per-line first-hit time over campaigns: each --reports dir holds one campaign's <project>_coverage.json
(corpus_coverage.py). Writes <project>_median.json (per line, the FHT of every campaign that reached it and their
median), <project>_median_final.json (lines grouped by median) and <project>_executed_lines.json into --out_dir.

    python median.py --reports campaign1/ campaign2/ ... [--projects bleach clib ...] [--out_dir DIR]
"""
import argparse
import json

import os
import logging

import concurrent.futures

logger = logging.getLogger(__name__)

# the 15 benchmark applications
PYTHON_PROJECTS = ["bleach", "filesystem_spec", "html5lib-python", "lark-parser", "rich"]
C_CPP_PROJECTS = ["clib", "cmark", "cpp-httplib", "exiv2", "guetzli", "libbpf", "libpng", "md4c", "varnish", "wamr"]
PROJECTS = PYTHON_PROJECTS + C_CPP_PROJECTS
RESULTS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__)))), "results", "fht")


def get_project_median(project, coverage_report_dirs, out_dir="."):
    logger.info(f"Processing {project} coverage report...")
    
    line_time_dict = {}

    for report_path in coverage_report_dirs:
        report_file = os.path.join(report_path, f"{project}_coverage.json")
        if not os.path.exists(report_file):
            logger.error(f"Report file {report_file} does not exist")
            continue
        
        with open(report_file, "r") as f:
            report = json.load(f)
            if project == 'bleach':
                fuzzer = 'sanitize_fuzzer' # TODO: double check
            else:
                # assert there's only one fuzzer in project
                assert len(report.keys()) == 1
                fuzzer = list(report.keys())[0]
            
            for duration, info in report[fuzzer].items():
                if info['newly_covered_lines_count'] == 0:
                    continue
                duration = int(duration)
                for file, lines in info['newly_covered_lines'].items():
                    if file not in line_time_dict:
                        line_time_dict[file] = {}
                    for line in lines:
                        if line not in line_time_dict[file]:
                            line_time_dict[file][line] = {}
                            line_time_dict[file][line]["FHT"] = []
                            line_time_dict[file][line]["median"] = -1
                        line_time_dict[file][line]["FHT"].append(duration)
    for file, lines_info in line_time_dict.items():
        for line, fht_info in lines_info.items():
            fht_info["FHT"].sort()
            # find the median duration
            sorted_durations = sorted(fht_info["FHT"])
            if len(sorted_durations) % 2 == 0:
                median = (sorted_durations[len(sorted_durations) // 2] + sorted_durations[len(sorted_durations) // 2 - 1]) / 2
            else:
                median = sorted_durations[len(sorted_durations) // 2]
            # print(f"file: {file}, line: {line}, median: {median}")
            fht_info["median"] = median
    line_time_dict = dict(sorted(line_time_dict.items()))

    with open(os.path.join(out_dir, f"{project}_median.json"), "w") as f:
        json.dump(line_time_dict, f, indent=4)

    final_result = {}
    for file, lines_info in line_time_dict.items():
        for line, fht_info in lines_info.items():
            median = fht_info["median"]
            if median not in final_result:
                final_result[median] = {}
            if file not in final_result[median]:
                final_result[median][file] = []
            final_result[median][file].append(line)

    for fht, file_info in final_result.items():
        for file, lines in file_info.items():
            final_result[fht][file] = sorted(lines)

    final_result = dict(sorted(final_result.items()))
    with open(os.path.join(out_dir, f"{project}_median_final.json"), "w") as f:
        json.dump(final_result, f, indent=4)

    executed_lines = {}
    for fht, file_info in final_result.items():
        for file, lines in file_info.items():
            if file not in executed_lines:
                executed_lines[file] = set()
            executed_lines[file].update(lines)

    for file, lines in executed_lines.items():
        executed_lines[file] = sorted(list(lines))
    
    with open(os.path.join(out_dir, f"{project}_executed_lines.json"), "w") as f:
        json.dump(executed_lines, f, indent=4)

    return f"{project}: success"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--reports", nargs="+", required=True, help="one dir per campaign")
    parser.add_argument("--projects", nargs="+", default=PROJECTS)
    parser.add_argument("--out_dir", default=RESULTS_DIR, help="default: static/results/fht")
    args = parser.parse_args()
    logging.basicConfig(level=logging.DEBUG)
    os.makedirs(args.out_dir, exist_ok=True)
    max_workers = min(len(args.projects), os.cpu_count() or 1)
    with concurrent.futures.ProcessPoolExecutor(max_workers=max_workers) as executor:
        futures = []
        for project in args.projects:
            futures.append(executor.submit(get_project_median, project, args.reports, args.out_dir))
        
        for future in concurrent.futures.as_completed(futures):
            try:
                result = future.result()
                logger.info(f"Result: {result}")
            except ValueError as e:
                logger.error(f"Value error: {e}")
            except AssertionError as e:
                logger.error(f"Assertion error: {e}")
            except Exception as e:
                logger.error(f"Exception processing project: {e}")


if __name__ == "__main__":
    main()
