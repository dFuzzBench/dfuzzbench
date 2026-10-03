import sys
import os
import argparse
import logging
import subprocess

logger = logging.getLogger(__name__)
CURR_DIR = os.path.dirname(os.path.abspath(__file__))

def check_global_var_target(lines, line_number):
    # Ensure the line_number is valid
    if line_number < 0 or line_number >= len(lines):
        raise ValueError("Invalid line_number: Out of range.")

    line = lines[line_number]

    if 'def' in line or 'class' in line or ':' in line:
        return False

    # Check 1: No indentation (no leading whitespace)
    if line.startswith((' ', '\t')):
        return False

    # Check 2: Contains '='
    if '=' not in line:
        return False

    # # Check 3: The variable name is capitalized
    # # Split the line at '=' and check the left-hand side (variable name)
    # parts = line.split('=', 1)
    # if len(parts) < 2:
    #     return False  # Malformed line

    # variable_name = parts[0].strip()  # Remove extra spaces around the variable name
    # if not variable_name.isupper():
    #     return False

    # All checks passed
    return True    

def instrument_target(file_path, line_number):
    original_target_line_str = None
    print("Instrumenting target...")
    # Check file extension to determine comment style
    _, ext = os.path.splitext(file_path)
    if ext not in ['.c', ".cc", '.cpp', '.h', '.hpp', '.py']:
        print(f"Unsupported file type: {ext}")
        return

    # Determine comment symbol and target line string based on file type
    if ext in ['.c','.h']:
        comment_symbol = "//"
        target_line_str = 'fprintf(stderr, "HIT TARGET\\n"); '
        import_str = "#include <stdio.h>\n#include <stdlib.h>\n"
        exit_str = "exit(0);\n"
    elif ext in [ ".cc", '.cpp', ".hpp"]:
        comment_symbol = "//"
        target_line_str = 'fprintf(stderr, "HIT TARGET\\n"); '
        import_str = "#include <stdio.h>\n#include <cstdlib>\n"
        exit_str = "exit(0);\n"
    elif ext == '.py':
        comment_symbol = "#"
        target_line_str = 'print("HIT TARGET")'
        import_str = None
        exit_str = "exit(0)\n"
    else:
        comment_symbol = None
        print(f"Unsupported file type: {ext}")
        return  # Exit if file type is unsupported

    target_line_str += f" {comment_symbol} target\n"

    try:
        # Read the file contents
        with open(file_path, 'r') as file:
            lines = file.readlines()

        # Check if line number is within the file's length
        if line_number < 1 or line_number > len(lines) + 1:
            logger.error(f"Line number {line_number} is out of range for file {file_path}")
            raise Exception(f"Line number {line_number} is out of range for file {file_path}")

        if check_global_var_target(lines, line_number - 1):
            logger.error(f"Target line is a global variable declaration, handling it manually")
            raise Exception(f"Target line is a global variable declaration, handling it manually")

        # if "switch" in lines[line_number - 1] or "case" in lines[line_number - 1]:
        #     logger.error(f"Target line is a switch-case statement, handling it manually")
        #     raise Exception(f"Target line is a switch-case statement, handling it manually")

        # Get indentation for Python files
        # if ext == '.py':
        # Determine the indentation level at the target line
        if line_number - 1 < len(lines) and lines[line_number - 1].strip():
            # Use the indentation of the current line
            indentation = lines[line_number - 1][:len(lines[line_number - 1]) - len(lines[line_number - 1].lstrip())]
        elif line_number - 2 >= 0:
            # Use the indentation of the previous line
            indentation = lines[line_number - 2][:len(lines[line_number - 2]) - len(lines[line_number - 2].lstrip())]
        else:
            # No indentation
            indentation = ''
        
        def func_def_end(line):
            if '):' in line:
                return True
            elif ')' in line and '->' in line and ':' in line:
                return True
            else:
                return False
        
        # check if the target line is a python function definition
        if ext == '.py' and "def " in lines[line_number - 1]:
            logger.info(f"Target line is a function definition, handling it differently")
            # the insert_line_number should be the next line after the function delcaration
            insert_line_number = line_number - 1
            while insert_line_number < len(lines) and not func_def_end(lines[insert_line_number]):
                insert_line_number += 1
            insert_line_number += 1
            # the indentation should be the next line after the function definition
            assert insert_line_number < len(lines)
            indentation = lines[insert_line_number][:len(lines[insert_line_number]) - len(lines[insert_line_number].lstrip())]
            line_number = insert_line_number + 1

        # Add indentation to the target line
        target_line_str = indentation + target_line_str + indentation + exit_str
        original_target_line_str = lines[line_number - 1] + target_line_str
        print(f"Original target line: {original_target_line_str}")

        # Insert the HIT TARGET line at the specified line number
        lines.insert(line_number - 1, target_line_str)
        if import_str is not None:
            lines.insert(0, import_str)

        # Write the modified content back to the file
        with open(file_path, 'w') as file:
            file.writelines(lines)

    except Exception as e:
        print(f"Error instrumenting target: {e}")
        exit(-1)

    logger.info(f"Target instrumented at {file_path}:{line_number}")
    return original_target_line_str

def generate_patch(project_dir, data_dir):
    print("Generating patch...")
    os.makedirs(data_dir, exist_ok=True)
    try:
        subprocess.run(["git", "-C", project_dir, "diff"], check=True, stdout=open(f"{data_dir}/target.patch", "w"), stderr=sys.stderr)
    except subprocess.CalledProcessError as e:
        print(f"Error generating patch: {e}")
        return
    logger.info(f"Patch generated: {data_dir}/target.patch")

def preprocess_args(args):
    if args.debug:
        logging.basicConfig(level=logging.DEBUG)
    else:
        logging.basicConfig(level=logging.INFO)

    logger.debug(f"Arguments: {args}")

    assert args.retrieval_mode == "oracle", "Only oracle; BM25 and Realistic contexts are built by retrieve_new.py"
    assert args.github_link or args.project_dir, "Either GitHub link or project directory must be provided."

    # if clone the project from github
    if args.github_link:
        project_dir = os.path.join(CURR_DIR, "src_projects", args.github_link.split("/")[-1])
        # check if the project directory already exists
        if os.path.isdir(project_dir):
            logger.warning(f"Project directory {project_dir} already exists.")
        else:
            try:
                subprocess.run(["git", "clone", args.github_link, project_dir], check=True)
            except subprocess.CalledProcessError as e:
                logger.error(f"Error cloning project: {e}")
                exit(-1)

        # reset to the specified commit hash
        try:
            subprocess.run(["git", "-C", project_dir, "reset", "--hard", args.commit_hash], check=True)
        except subprocess.CalledProcessError as e:
            logger.error(f"Error resetting to commit hash: {e}")
            exit(-1)

        args.project_dir = project_dir
    else:
        # reset to the specified commit hash if the project directory is provided
        try:
            subprocess.run(["git", "-C", args.project_dir, "reset", "--hard", args.commit_hash], check=True)
        except subprocess.CalledProcessError as e:
            logger.error(f"Error resetting to commit hash: {e}")
            exit(-1)  

    args.target_filepath = os.path.join(args.project_dir, args.target_filepath)
    if not os.path.isdir(args.project_dir):
        raise NotADirectoryError(f"The provided path {args.project_dir} is not a valid directory.")
    if not os.path.isfile(args.target_filepath):
        raise FileNotFoundError(f"The provided path {args.target_filepath} is not a valid file.")

    os.makedirs(args.data_dir, exist_ok=True)
    return args

def parse_args():
    parser = argparse.ArgumentParser(description="Transform a project into a benchmark dataset.")
    parser.add_argument("--github_link", type=str, required=False, help="The GitHub link to the project")
    parser.add_argument("--commit_hash", type=str, required=True, help="The commit hash of the project")
    parser.add_argument("--project_dir", type=str, required=False, help="The path to the project directory")
    parser.add_argument("--target_filepath", type=str, required=True, help="Relative path to the target file (from the project base directory)")
    parser.add_argument("--target_line", type=int, required=True, help="The line number of the target")
    parser.add_argument("--data_dir", type=str, required=True, help="The path to the dataset directory")
    parser.add_argument("--retrieval_mode", type=str, default="oracle", help="oracle (BM25 and Realistic: retrieve_new.py)")
    parser.add_argument("--debug", action="store_true", help="Enable debug logging")

    args = parser.parse_args()
    args = preprocess_args(args)

    return args

if __name__ == "__main__":
    args = parse_args()

    # 1. target instrumentation
    instrument_target(args.target_filepath, args.target_line)
    # 2. generate git patch
    generate_patch(args.project_dir, args.data_dir)
    # 3. the oracle context: the target file; the other files on the executed path are added by hand
    try:
        subprocess.run(["cp", args.target_filepath, args.data_dir], check=True)
    except subprocess.CalledProcessError as e:
        print(f"Error copying file: {e}")
    logger.warning("Please retrieve the oracle files other than the target file manually.")
    
