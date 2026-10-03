"""Executed lines per coverage snapshot of a campaign: OSS-Fuzz coverage reports (Python: coverage.py JSON
reports in textcov_reports/; C/C++: llvm-cov JSON exports in fuzzer_stats/) under <oss-fuzz>/build/out/<project>,
turned into <out_dir>/<project>_coverage.json, {fuzzer: {minute: newly covered lines}}.

    python coverage.py --oss_fuzz_dir ~/oss-fuzz [--projects bleach clib ...] [--out_dir DIR]
"""
import os
import json
import argparse
import logging
from datetime import datetime, timedelta
from bs4 import BeautifulSoup

import concurrent.futures

logger = logging.getLogger(__name__)

# the 15 benchmark applications
PYTHON_PROJECTS = ["bleach", "filesystem_spec", "html5lib-python", "lark-parser", "rich"]
C_CPP_PROJECTS = ["clib", "cmark", "cpp-httplib", "exiv2", "guetzli", "libbpf", "libpng", "md4c", "varnish", "wamr"]
PROJECTS = PYTHON_PROJECTS + C_CPP_PROJECTS
RESULTS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__)))), "results", "fht")


def report2time(report_name):
    logger.debug(f"Converting report name to timestamp: {report_name}")
    timestamp_str = report_name.split(".")[0]
    date_time = datetime.strptime(timestamp_str, "%Y%m%d%H%M%S")
    formatted_date_time = date_time.strftime("%Y-%m-%dT%H:%M:%S")
    return formatted_date_time


def dump_coverage(coverage, project, out_dir="."):
    logger.info(f"Dumping coverage for project: {project}")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, f"{project}_coverage.json"), 'w') as f:
        json.dump(coverage, f, indent=4)


def collect_python_coverage(project_dir):
    logger.info(f"Collecting coverage for Python project: {project_dir}")
    report_dir = os.path.join(project_dir, "textcov_reports")
    assert os.path.exists(report_dir)
    
    fuzzer_coverage = {}
    covered_lines = {}

    for _, dirs, _ in os.walk(report_dir):
        for fuzzer in dirs:
            start_timestamp = None
            current_duration = None    # duration in minutes
            fuzzer_coverage[fuzzer] = {}
            fuzzer_dir = os.path.join(report_dir, fuzzer)
            for _, _, files in os.walk(fuzzer_dir):

                json_files = [file for file in files if file.endswith(".json")]
                sorted_reports = sorted(json_files)
                # logger.debug(f"Sorted reports: {sorted_reports}")
                # Process the files in time order
                for report in sorted_reports:
                    timestamp_str = report2time(report)
                    if start_timestamp is None:
                        current_duration = 1
                        # get the start timestamp by subtracting 1 minute from the current timestamp
                        dt = datetime.strptime(timestamp_str, "%Y-%m-%dT%H:%M:%S")
                        dt_before_1_min = dt - timedelta(minutes=1)
                        start_timestamp = dt_before_1_min.timestamp()
                        logger.debug(f"Start timestamp: {start_timestamp}")
                    else:
                        timstamp_dt = datetime.strptime(timestamp_str, "%Y-%m-%dT%H:%M:%S")
                        current_duration = int((timstamp_dt.timestamp() - start_timestamp) / 60)

                        
                    file_path = os.path.join(fuzzer_dir, report)
                    with open(file_path, 'r') as f:
                        fuzzer_coverage[fuzzer][current_duration] = {}
                        fuzzer_coverage[fuzzer][current_duration]["timestamp"] = timestamp_str
                        # # assert the difference between the current timestamp and the start timestamp is around current duration
                        # dt = datetime.strptime(timestamp_str, "%Y-%m-%dT%H:%M:%S")
                        # assert abs(dt.timestamp() - start_timestamp - current_duration * 60) < 60

                        data = json.load(f)
                        # line coverage summary
                        percent_covered = data["totals"]["percent_covered"]
                        logger.debug(f"Fuzzer: {fuzzer}, Timestamp: {timestamp_str}, Coverage: {percent_covered}")
                        fuzzer_coverage[fuzzer][current_duration]["percent_covered"] = percent_covered
                        # newly covered lines
                        delta_lines = {}
                        for file in data["files"]:
                            hit_lines = data["files"][file]["executed_lines"]
                            update_coverage_record(file, hit_lines, covered_lines, delta_lines)
                    newly_covered_lines_count = sum([len(delta_lines[file]) for file in delta_lines])
                    fuzzer_coverage[fuzzer][current_duration]["newly_covered_lines_count"] = newly_covered_lines_count
                    fuzzer_coverage[fuzzer][current_duration]["newly_covered_lines"] = delta_lines

    return fuzzer_coverage
                
def extract_coverage(dir, covered_lines, delta_lines):
    logger.debug(f"Extracting coverage from: {dir}")
    for root, dirs, files in os.walk(dir):
        extract_coverage_at_dir(root, covered_lines, delta_lines)
        for dir in dirs:
            extract_coverage(os.path.join(root, dir), covered_lines, delta_lines)

def extract_coverage_from_file(filepath):
    lines_w_hits = []
    with open(filepath, 'r') as f:
        html_content = f.read()
        soup = BeautifulSoup(html_content, 'html.parser')
        src_name = soup.find("div", class_="source-name-title").get_text(strip=True)

        for row in soup.find_all('tr'):
            line_number_tag = row.find('td', class_='line-number')
            hit_count_tag = row.find('td', class_='covered-line')
            
            if line_number_tag and hit_count_tag:
                line_number = int(line_number_tag.get_text(strip=True))
                hit_count = hit_count_tag.get_text(strip=True)
                
                # Filter lines where the hit count is greater than 0
                if hit_count and hit_count != '0':
                    lines_w_hits.append((line_number, hit_count))

    hit_lines = [line for line, _ in lines_w_hits]
    return src_name, hit_lines

def update_coverage_record(src_name, hit_lines, covered_lines, delta_lines):
    if src_name not in covered_lines:
        covered_lines[src_name] = []
    
    for line in hit_lines:
        if line not in covered_lines[src_name]:
            covered_lines[src_name].append(line)
            if src_name not in delta_lines:
                delta_lines[src_name] = []
            delta_lines[src_name].append(line)

def extract_coverage_at_dir(dir, covered_lines, delta_lines):
    logger.debug(f"Extracting coverage from single-level dir: {dir}")
    for root, _, files in os.walk(dir):
        for file in files:
            if file.endswith(".c.html") or file.endswith(".h.html") or file.endswith(".cpp.html") or file.endswith(".hpp.html"):
                filepath = os.path.join(root, file)
                src_name, hit_lines = extract_coverage_from_file(filepath)
                update_coverage_record(src_name, hit_lines, covered_lines, delta_lines)

def executed_lines_from_segments(segments):
    """Lines with a non-zero execution count, from llvm-cov export segments
    [line, col, count, has_count, is_region_entry, is_gap_region], computed as llvm-cov's LineCoverageStats does:
    a line takes the count of the segment wrapping it and of the regions that start on it."""
    by_line = {}
    for segment in segments:
        by_line.setdefault(segment[0], []).append(segment)
    if not by_line:
        return []
    hit_lines, wrapped = [], None
    for line in range(min(by_line), max(by_line) + 1):
        line_segments = by_line.get(line, [])
        region_starts = [s for s in line_segments if s[3] and s[4] and not (len(s) > 5 and s[5])]
        starts_skipped_region = bool(line_segments) and not line_segments[0][3] and line_segments[0][4]
        mapped = not starts_skipped_region and ((wrapped is not None and wrapped[3]) or bool(region_starts))
        if mapped:
            count = wrapped[2] if wrapped is not None else 0
            for segment in region_starts:
                count = max(count, segment[2])
            if count > 0:
                hit_lines.append(line)
        if line_segments:
            wrapped = line_segments[-1]
    return hit_lines

def extract_line_coverage_from_json(json_file, covered_lines, delta_lines):
    logger.debug(f"Extracting line coverage from json file: {json_file}")
    with open(json_file, 'r') as f:
        data = json.load(f)
    # Access the coverage data
    files_data = data['data'][0]['files']

    # Iterate over each file
    for file_entry in files_data:
        filename = file_entry['filename']
        hit_lines = executed_lines_from_segments(file_entry.get('segments', []))
        logger.debug(f"filename: {filename}, hit_lines: {hit_lines}")
        update_coverage_record(filename, hit_lines, covered_lines, delta_lines)

    line_coverage = data['data'][0]['totals']['lines']['percent']
    logger.debug(f"Line coverage: {line_coverage}")
    return line_coverage

def collect_cpp_coverage_new(project_dir):
    logger.info(f"Collecting coverage for C/C++ project: {project_dir}")
    report_dir = os.path.join(project_dir, "fuzzer_stats")
    assert os.path.exists(report_dir)

    json_files = [f for f in os.listdir(report_dir) if f.endswith(".json")]
    fuzzers = list(set(json_file.split("_backup_")[0] for json_file in json_files))

    logger.debug(f"Fuzzers: {fuzzers}")

    covered_lines = {}
    fuzzer_coverage = {}

    for fuzzer in fuzzers:
        start_timestamp = None
        current_duration = None    # duration in minutes
        fuzzer_reports = [f for f in json_files if f.startswith(fuzzer)]
        sorted_fuzzer_reports = sorted(fuzzer_reports)
        fuzzer_coverage[fuzzer] = {}
        for report in sorted_fuzzer_reports:
            timestamp_str = report2time(report.split("_backup_")[-1])

            if start_timestamp is None:
                current_duration = 1
                # get the start timestamp by subtracting 1 minute from the current timestamp
                dt = datetime.strptime(timestamp_str, "%Y-%m-%dT%H:%M:%S")
                dt_before_1_min = dt - timedelta(minutes=1)
                start_timestamp = dt_before_1_min.timestamp()
                logger.debug(f"Start timestamp: {start_timestamp}")
            else:
                timstamp_dt = datetime.strptime(timestamp_str, "%Y-%m-%dT%H:%M:%S")
                current_duration = int((timstamp_dt.timestamp() - start_timestamp) / 60)

            delta_lines = {}
            fuzzer_coverage[fuzzer][current_duration] = {}
            fuzzer_coverage[fuzzer][current_duration]["timestamp"] = timestamp_str
            file_path = os.path.join(report_dir, report)
            line_coverage = extract_line_coverage_from_json(file_path, covered_lines, delta_lines)
            fuzzer_coverage[fuzzer][current_duration]["percent_covered"] = line_coverage
            newly_covered_lines_count = sum([len(delta_lines[file]) for file in delta_lines])
            fuzzer_coverage[fuzzer][current_duration]["newly_covered_lines_count"] = newly_covered_lines_count
            fuzzer_coverage[fuzzer][current_duration]["newly_covered_lines"] = delta_lines

    return fuzzer_coverage


def collect_cpp_coverage(project_dir):
    logger.info(f"Collecting coverage for C/C++ project: {project_dir}")
    report_dir = os.path.join(project_dir, "report_target")
    assert os.path.exists(report_dir)

    fuzzer_coverage = {}
    covered_lines = {}

    fuzzer_dirs = [d for d in os.listdir(report_dir) if os.path.isdir(os.path.join(report_dir, d))]
    for fuzzer in fuzzer_dirs:
        fuzzer_coverage[fuzzer] = {}
        fuzzer_dir = os.path.join(report_dir, fuzzer)
        backup_dirs = [d for d in os.listdir(fuzzer_dir) if os.path.isdir(os.path.join(fuzzer_dir, d))]
        for backup_dir in backup_dirs:
            if not backup_dir.startswith("backup_"):
                logger.debug(f"Skipping non-backup dir: {backup_dir}")
                break
            html_dir = backup_dir
            timestamp = report2time(html_dir.split("_")[-1])
            fuzzer_coverage[fuzzer][timestamp] = {}

            # line coverage summary
            summary_path = os.path.join(fuzzer_dir, html_dir, "linux", "summary.json")
            logger.debug(f"Checking summary file: {summary_path}")
            assert os.path.exists(summary_path)
            with open(summary_path, 'r') as f:
                data = json.load(f)
                percent_covered = data["data"][0]["totals"]["lines"]["percent"]
            fuzzer_coverage[fuzzer][timestamp]["percent_covered"] = percent_covered
            # newly covered lines
            delta_lines = {}
            target_dir = os.path.join(fuzzer_dir, html_dir, "linux", "src")
            extract_coverage(target_dir, covered_lines, delta_lines)
            newly_covered_lines_count = sum([len(delta_lines[file]) for file in delta_lines])
            fuzzer_coverage[fuzzer][timestamp]["newly_covered_lines_count"] = newly_covered_lines_count
            fuzzer_coverage[fuzzer][timestamp]["newly_covered_lines"] = delta_lines

    return fuzzer_coverage

def collect_coverage(project, oss_fuzz_dir):
    project_dir = os.path.join(oss_fuzz_dir, "build", "out", project)
    if project in PYTHON_PROJECTS:
        return collect_python_coverage(project_dir)
    return collect_cpp_coverage_new(project_dir)

def process_project(project, oss_fuzz_dir, out_dir):
    logger.info(f"Processing project: {project}")
    coverage = collect_coverage(project, oss_fuzz_dir)
    dump_coverage(coverage, project, out_dir)
    return f"Successfully processed project: {project}"

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--oss_fuzz_dir", default=os.environ.get("OSS_FUZZ_DIR"), required=not os.environ.get("OSS_FUZZ_DIR"),
                        help="the OSS-Fuzz checkout (default: $OSS_FUZZ_DIR)")
    parser.add_argument("--projects", nargs="+", default=PROJECTS)
    parser.add_argument("--out_dir", default=os.path.join(RESULTS_DIR, "coverage"),
                        help="where <project>_coverage.json goes, one dir per campaign (default: static/results/fht/coverage)")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO)
    max_workers = min(17, os.cpu_count() or 1)
    with concurrent.futures.ProcessPoolExecutor(max_workers=max_workers) as executor:
        futures = {executor.submit(process_project, project, os.path.abspath(args.oss_fuzz_dir), os.path.abspath(args.out_dir)): project
                   for project in args.projects}
        for future in concurrent.futures.as_completed(futures):
            project = futures[future]
            try:
                result = future.result()
                logger.debug(result)
            except ValueError as e:
                logging.error(f"[{project}] Value error: {e}")
            except AssertionError as e:
                logging.error(f"[{project}] Assertion error: {e}")
            except Exception as e:
                logging.error(f"[{project}] Exception: {e}")


if __name__ == "__main__":
    main()
