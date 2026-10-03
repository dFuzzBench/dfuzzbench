"""
A tool to integrate the LLM API with the fuzzing engine.
Will instrument the fuzzing engine to use the LLM generated inputs instead of the random data.
"""

import json
import logging
import os
import re
from openai import OpenAI, BadRequestError
import random
import string
import time
from const import HARNESS_PROMPTS, TARGETS, VIBE_TARGETS
from enum import Enum, auto

DFUZZBENCH_DIR=os.path.dirname(os.path.dirname(os.path.realpath(__file__)))

# The evaluated models, plus the undated OpenAI aliases the original whitelist accepted. The
# provider follows from the prefix: gemini-* -> Google, gpt-* -> OpenAI, Qwen3-* -> a local vLLM server.
MODELS = ("gemini-3.1-pro-preview", "gemini-3-flash-preview", "gemini-2.5-pro",
          "gpt-5.2-2025-12-11", "gpt-5.2", "gpt-4o-2024-11-20", "gpt-4o",
          "Qwen3-30B-A3B-Thinking-2507-FP8")
VLLM_BASE_URL = os.environ.get("VLLM_BASE_URL", "http://localhost:8000/v1")


def api_key(model):
    """The provider key of `model`, from the environment only."""
    if model.startswith("gemini"):
        return os.environ.get("GEMINI_API_KEY") or os.environ.get("GOOGLE_API_KEY")
    if model.startswith("gpt"):
        return os.environ.get("OPENAI_API_KEY")
    if model.startswith("Qwen3"):
        return os.environ.get("VLLM_API_KEY", "N/A")  # only checked if the server was started with --api-key
    return None


def require_api_key(model):
    if not api_key(model):
        var = "GEMINI_API_KEY (or GOOGLE_API_KEY)" if model.startswith("gemini") else "OPENAI_API_KEY"
        raise SystemExit(f"{model} needs {var} in the environment")


def random_string():
    """The random baseline's input: 10 random alphanumeric characters."""
    return ''.join(random.choices(string.ascii_letters + string.digits, k=10))

class ErrorCode(Enum):
    # Define error codes
    SUCCESS = 0
    JSON_DECODE_ERROR = auto()
    EXCEED_CONTEXT_WINDOW_ERROR = auto()
    PATTERN_UNMATCH_ERROR = auto()
    INCOMPATIBLE_DATA_ERROR = auto()
    NO_LOG_ERROR = auto()
    NO_COVERAGE_ERROR = auto()
    COPYRIGHT_ERROR = auto()
    RATE_LIMIT_ERROR = auto()
    OTHER_ERROR = auto()

HARNESS_LANGUAGE=None
TARGET_COMMENT=None
COVERED_COMMENT=None

SUCCESS = 0

logger = logging.getLogger(__name__)

def build_prompt(args, target):
    """
    Build the prompt for the LLM API request
    
    @return: the prompt string
    """
    difficulty, target_path, target_line = target
    project_name = args.testcase.rstrip("/").split("/")[-3]
    target_function = VIBE_TARGETS[project_name][difficulty][f"{target_path.replace('/', ':')}:{target_line}"][-1] if args.vibe else TARGETS[project_name][difficulty][f"{target_path.replace('/', ':')}:{target_line}"][-1]
    fuzzing_harness_filename = args.harness.rstrip('/').split('/')[-1]
    if HARNESS_LANGUAGE != "Python":
        target_line += 2  # adjust for C/C++ line number shift due to instrumentation
    fuzzing_harness_path = os.path.join(args.testcase, fuzzing_harness_filename)
    fuzzing_harness_code = f'[start of {fuzzing_harness_filename}]\n'
    with open(fuzzing_harness_path, 'r', encoding='utf-8', errors='ignore') as file:
        fuzzing_harness_code += file.read()
    fuzzing_harness_code += f'\n[end of {fuzzing_harness_filename}]\n'

    starting_prompt = (
        f'You will be given a {HARNESS_LANGUAGE} project consisting of several files. ' 
        f'The target line is located in file `{target_path}`, line `{target_line}` in function `{target_function}`, '
        f'which is a line marked with the comment "{TARGET_COMMENT}". ' 
        'This line represents the target that must be executed. ' 
        'Your task is to analyze the programs and generate input that guarantees execution of the target line. ' 
        f'The program named "{fuzzing_harness_filename}" serves as the entry point for execution, accepting random input to test and exercise the project.\n'
        "The source code is as follows, which is the entry point for execution, your input will be assigned to the variable which stores the fuzzed input string:"
        f'{fuzzing_harness_code}\n'
        f"This program requires {'only one input.' if HARNESS_PROMPTS[project_name][0] == 1 else str(HARNESS_PROMPTS[project_name][0]) + ' distinct input strings. Please provide these inputs, separated by a single blank line.'}. "
        f'For example, {HARNESS_PROMPTS[project_name][1]}\n'
        f'Below are the {HARNESS_LANGUAGE} programs of target software:\n'
    )
    example_single_input = (
        "The following is an example of your generated input string. It contains the required input to reach the target line in the code. "
        "Use '\\n' to separate different lines. Your input should be wrapped by ``` as follows:\n"
        '```\ninput1\\ninput2\\ninput3\\n...\n```\n\n'
    )
    example_multi_input = (
    f"The following is an example of your generated input strings. It contains the required {HARNESS_PROMPTS[project_name][0]} input strings to reach the target line in the code. "
    f"Use '\\n\\n' to separate different lines. Since it requires {HARNESS_PROMPTS[project_name][0]} inputs, you need to have at least {HARNESS_PROMPTS[project_name][0] - 1} '\\n\\n' in your generated input. The inputs should be wrapped by ``` as follows:\n"
    f'```\ninput1\\n\\ninput2\\n\\n...\\n\\ninput{HARNESS_PROMPTS[project_name][0] - 1}\n```\n'
    )
    example_input = example_single_input if HARNESS_PROMPTS[project_name][0] == 1 else example_multi_input

    ending_prompt = (
        'I need you to generate the necessary input string to reach the target line in the code. '
        f'We will substitute the random data in the "{fuzzing_harness_filename}" program with your input directly, and it should function seamlessly.'
        'Please provide only the input string without any other explanation.\nRespond below:'
    )

    code = "<code>\n"
    testcase_dir = args.testcase
    filenames = os.listdir(testcase_dir)
    for filename in filenames:
        if ".patch" not in filename and ".log" not in filename and ".json" not in filename and ".sh" not in filename:
            if "fuzzer_instrumented" in filename or "fuzzer." in filename:
                continue
            # print(f"filename: {filename}")
            with open(os.path.join(testcase_dir, filename), 'r', encoding='utf-8', errors='ignore') as file:
                code += f"[start of {filename}]\n{file.read()}\n[end of {filename}]\n"
    code += "</code>\n\n"
    sourcecode_prompt = code

    prompt = starting_prompt + sourcecode_prompt + example_input + ending_prompt
    logger.debug(f"Final Prompt: {prompt}")

    # timestamp = time.strftime("%Y%m%d-%H%M%S")
    # with open(f"prompt_{project_name}_{timestamp}.txt", "w") as f:
    #     f.write(prompt)
    return prompt

def build_prompt_no_context(args, target):
    difficulty, target_path, target_line = target
    project_name = args.testcase.rstrip("/").split("/")[-3]
    target_function = VIBE_TARGETS[project_name][difficulty][f"{target_path.replace('/', ':')}:{target_line}"][-1] if args.vibe else TARGETS[project_name][difficulty][f"{target_path.replace('/', ':')}:{target_line}"][-1]
    fuzzing_harness_filename = args.harness.rstrip('/').split('/')[-1]
    if HARNESS_LANGUAGE != "Python":
        target_line += 2  # adjust for C/C++ line number shift due to instrumentation
    fuzzing_harness_path = os.path.join(args.testcase, fuzzing_harness_filename)
    with open(fuzzing_harness_path, 'r', encoding='utf-8', errors='ignore') as file:
        fuzzing_harness_code = file.read()

    prompt = no_context_prompt(project_name, target_path, target_line, target_function, fuzzing_harness_filename,
                               fuzzing_harness_code, *HARNESS_PROMPTS[project_name])
    logger.debug(f"Final Prompt: {prompt}")
    return prompt

def no_context_prompt(project_name, target_path, target_line, target_function, fuzzing_harness_filename,
                      fuzzing_harness_code, input_count, input_example):
    """The no-context prompt: the project, where the target is and the harness, but none of the project's source."""
    fuzzing_harness_code = f'[start of {fuzzing_harness_filename}]\n{fuzzing_harness_code}\n[end of {fuzzing_harness_filename}]\n'

    starting_prompt = (
        f"You are generating the required input to reach the target line of code in the project {project_name} based on your traning data. "
        f'The target line is located in file `{target_path}`, line `{target_line}` in function `{target_function}`. '
        f'The program named "{fuzzing_harness_filename}" serves as the entry point for execution, accepting random input to test and exercise the project.\n'
        "The source code is as follows, which is the entry point for execution, your input will be assigned to the variable which stores the fuzzed input string:"
        f'{fuzzing_harness_code}\n'
        f"This program requires {'only one input.' if input_count == 1 else str(input_count) + ' distinct input strings. Please provide these inputs, separated by a single blank line.'}. "
        f'For example, {input_example}\n'
        f'Below are the {HARNESS_LANGUAGE} programs of target software:\n'
    )
    example_single_input = (
        "The following is an example of your generated input string. It contains the required input to reach the target line in the code. "
        "Use '\\n' to separate different lines. Your input should be wrapped by ``` as follows:\n"
        '```\ninput1\\ninput2\\ninput3\\n...\n```\n\n'
    )
    example_multi_input = (
    f"The following is an example of your generated input strings. It contains the required {input_count} input strings to reach the target line in the code. "
    f"Use '\\n\\n' to separate different lines. Since it requires {input_count} inputs, you need to have at least {input_count - 1} '\\n\\n' in your generated input. The inputs should be wrapped by ``` as follows:\n"
    f'```\ninput1\\n\\ninput2\\n\\n...\\n\\ninput{input_count - 1}\n```\n'
    )
    example_input = example_single_input if input_count == 1 else example_multi_input

    ending_prompt = (
        'I need you to generate the necessary input string to reach the target line in the code. '
        f'We will substitute the random data in the "{fuzzing_harness_filename}" program with your input directly, and it should function seamlessly.'
        'Please provide only the input string without any other explanation.\nRespond below:'
    )

    return starting_prompt + example_input + ending_prompt

# --no_context [variant], original if none given: the No-source (original) and No-source (perturbed) settings
NO_CONTEXT_VARIANTS = ("original", "placeholder")

# the identifier renamings of --no_context placeholder (func1, var1, ...), written by obfuscate_no_context.py
PLACEHOLDER_DIR = os.path.join(DFUZZBENCH_DIR, "data", "obfuscated-placeholder")

def load_placeholder(dataset, name):
    """The renamed identifiers of a project (dataset target-latest) or CVE (target-arvo), with its renamed
    harness."""
    obf_dir = os.path.join(PLACEHOLDER_DIR, dataset, name)
    with open(os.path.join(obf_dir, "prompt.json"), encoding="utf-8") as f:
        obf = json.load(f)
    with open(os.path.join(obf_dir, obf["harness_file"]), encoding="utf-8") as f:
        obf["harness_code"] = f.read()
    return obf

def build_prompt_placeholder(args, target):
    """--no_context placeholder: the no-context prompt with every identifier -- the project, the target's file
    path and function, the harness file and all names in the harness -- replaced by a numbered placeholder such
    as func1 or var2."""
    if args.vibe:
        raise ValueError(f"--no_context {args.no_context} covers target-latest only")
    difficulty, target_path, target_line = target
    project_name = args.testcase.rstrip("/").split("/")[-3]
    obf = load_placeholder("target-latest", project_name)
    obf_target = obf["targets"][difficulty][f"{target_path.replace('/', ':')}:{target_line}"]
    if HARNESS_LANGUAGE != "Python":
        target_line += 2  # adjust for C/C++ line number shift due to instrumentation
    prompt = no_context_prompt(obf["project"], obf_target["path"], target_line, obf_target["function"],
                               obf["harness_name"], obf["harness_code"], HARNESS_PROMPTS[project_name][0],
                               obf["input_example"])
    logger.debug(f"Final Prompt: {prompt}")
    return prompt

def build_no_context_prompt(args, target):
    """The prompt for --no_context [original|placeholder]."""
    if args.no_context == "placeholder":
        return build_prompt_placeholder(args, target)
    return build_prompt_no_context(args, target)


def make_API_request(prompt, args):
    """
    Invoke the API to get the response
    @param prompt: the prompt string
    @return: the response from the LLM API
    """
    query_duration = None
    if args.llm_model.startswith("gpt"):
        client = OpenAI(api_key=api_key(args.llm_model))
        try:
            start_time = time.perf_counter()
            response = client.chat.completions.create(
                model=args.llm_model,
                messages = [
                    {"role": "system", "content": "You are an expert in software testing and fuzzing."},
                    {
                        "role": "user",
                        "content": prompt,
                    }
                ]
            )
            end_time = time.perf_counter()
        except json.decoder.JSONDecodeError as e:
            logger.warning(f"Invalid JSON response: {e}")
            return None, None, ErrorCode.JSON_DECODE_ERROR
        except BadRequestError as e:
            logger.warning(f"[{args.testcase}] Bad request error: {e}")
            e = str(e)
            if "context_length_exceeded" in e or "maximum context length" in e:
                return None, None, ErrorCode.EXCEED_CONTEXT_WINDOW_ERROR
            return None, None, ErrorCode.OTHER_ERROR
        except Exception as e:
            logger.warning(f"[{args.testcase}] API request error: {e}")
            return None, None, ErrorCode.OTHER_ERROR
        
        content = response.choices[0].message.content
    elif args.llm_model.startswith("gemini"):
        import google.generativeai as genai  # imported here so that runs without a model need no Google SDK
        from google.api_core import exceptions
        genai.configure(api_key=api_key(args.llm_model))

        model = genai.GenerativeModel(args.llm_model)
        try:
            start_time = time.perf_counter()
            response = model.generate_content(f"{prompt}")
            end_time = time.perf_counter()
        except json.decoder.JSONDecodeError as e:
            logger.warning(f"Invalid JSON response: {e}")
            return None, None, ErrorCode.JSON_DECODE_ERROR
        except BadRequestError as e:
            logger.warning(f"[{args.testcase}] Bad request error: {e}")
            e = str(e)
            if "context_length_exceeded" in e or "maximum context length" in e:
                return None, None, ErrorCode.EXCEED_CONTEXT_WINDOW_ERROR
            return None, None, ErrorCode.OTHER_ERROR
        except exceptions.ResourceExhausted as e:
            logger.warning(f"[{args.testcase}] Quota exceeded (429): {e}")
            time.sleep(45) 
            return None, None, ErrorCode.RATE_LIMIT_ERROR # Ensure this exists in your Enum
        except exceptions.InvalidArgument as e:
            logger.warning(f"[{args.testcase}] Invalid arguments (400): {e}")
            return None, None, ErrorCode.OTHER_ERROR
            
        except Exception as e:
            logger.warning(f"[{args.testcase}] API request error: {e}")
            return None, None, ErrorCode.OTHER_ERROR

        if not response.candidates:
            return None, None, ErrorCode.OTHER_ERROR
        candidate = response.candidates[0]
        if candidate.finish_reason == 4:
            logger.warning("Content generation blocked due to copyright restrictions.")
            return None, None, ErrorCode.COPYRIGHT_ERROR
        if not candidate.content or not candidate.content.parts:
            return None, None, ErrorCode.OTHER_ERROR

        content = response.text
    elif args.llm_model.startswith("Qwen3"):
        # served by vLLM as Qwen/<model>, e.g. `vllm serve Qwen/Qwen3-30B-A3B-Thinking-2507-FP8`
        client = OpenAI(api_key=api_key(args.llm_model), base_url=VLLM_BASE_URL)
        try:
            start_time = time.perf_counter()
            response = client.chat.completions.create(
                model=f"Qwen/{args.llm_model}", 
                messages = [
                    {"role": "system", "content": "You are an expert in software testing and fuzzing."},
                    {
                        "role": "user",
                        "content": prompt,
                    }
                ]
            )
            end_time = time.perf_counter()
        except json.decoder.JSONDecodeError as e:
            logger.warning(f"Invalid JSON response: {e}")
            return None, None, ErrorCode.JSON_DECODE_ERROR
        except BadRequestError as e:
            logger.warning(f"[{args.testcase}] Bad request error: {e}")
            e = str(e)
            if "context_length_exceeded" in e or "maximum context length" in e:
                return None, None, ErrorCode.EXCEED_CONTEXT_WINDOW_ERROR
            return None, None, ErrorCode.OTHER_ERROR
        except Exception as e:
            logger.warning(f"[{args.testcase}] API request error: {e}")
            return None, None, ErrorCode.OTHER_ERROR
        
        content = response.choices[0].message.content        
    else:
        raise ValueError(f"Unsupported LLM model: {args.llm_model}")
    
    query_duration = end_time - start_time
    logger.debug(f"API response message:\n{content}")
    logger.debug(f"Query duration: {query_duration}")
    return content, query_duration, ErrorCode.SUCCESS

def process_response(response):
    """
    Process the response to extract the input for the program execution

    @param response: the response from the LLM API
    @return data: the input string list for the fuzz program execution
    """
    if "```" not in response:
        return None, ErrorCode.PATTERN_UNMATCH_ERROR

    pattern = r"```\n((?:(?!```).)*?)```"
    match = re.search(pattern, response, re.DOTALL)
    if match:
        input_content = match.group(1).strip()
        logger.debug(f"Extracted input content: {input_content}")
        data = input_content.split("\n\n")
        data.insert(1, "")
        logger.debug(f"Extracted input data: {data}")
        return data, ErrorCode.SUCCESS
    else:
        logger.warning("Could not find the input content in response")
        return None, ErrorCode.PATTERN_UNMATCH_ERROR

def instrumented_harness_path(harness_file, args):
    """
    The instrumented harness is written straight into this run's exec-env dir (args.testcase),
    never next to the shared base-env harness, so concurrent runs cannot pick up each other's input.
    e.g. fuzzer.cc -> <testcase>/fuzzer_instrumented.cc
    """
    name, ext = os.path.splitext(os.path.basename(harness_file))
    return os.path.join(args.testcase, f"{name}_instrumented{ext}")

def instrument_fuzzing_engine_c(data, args):
    logger.debug("Instrumenting the C/C++ fuzzing engine")
    harness_file = args.harness
    with open(harness_file, 'r') as file:
        lines = file.readlines()
    
    if len(data) < 1:
        return ErrorCode.INCOMPATIBLE_DATA_ERROR

    new_lines = []
    inst_flag = False
    skip_flag = False
    cap_data_flag = False

    # data = "\n".join(str(item) for item in data)
    input_data = "\n".join(str(item) for item in data)
    input_data = input_data.replace("\n", "\\n")

    lines.insert(0, "#include <string.h>\n")
    for line in lines:
        if "LLVMFuzzerTestOneInput(" in line and ';' not in line:
            if "data," in line:
                cap_data_flag = False
            elif "Data," in line:
                cap_data_flag = True
                assert "Size" in line
            inst_flag = True
            skip_flag = "{" not in line
            new_lines.append(line)
        elif inst_flag:
            if not skip_flag:
                # Replace the LLVMFuzzer data with the LLM data
                # if data[0] has a double quote, use single quote, otherwise use double quote
                if "\"" in input_data:
                    # replace all the " with '
                    input_data = input_data.replace("\"", "'")
                if cap_data_flag:
                    new_lines.append(f"    Data = (const uint8_t*)\"{input_data}\";\n")
                    new_lines.append("    Size = strlen((const char*)Data);\n")                    
                else:
                    new_lines.append(f"    data = (const uint8_t*)\"{input_data}\";\n")
                    new_lines.append("    size = strlen((const char*)data);\n")
                inst_flag = False
            else:
                skip_flag = False
            new_lines.append(line)
        else:
            new_lines.append(line)


    new_harness_file = instrumented_harness_path(harness_file, args)
    logger.debug(f"Writing the instrumented harness to {new_harness_file}")

    with open(new_harness_file, 'w') as file:
        file.writelines(new_lines)
    return ErrorCode.SUCCESS

def to_string_literal(s):
    """
    Helper to safely format a string as a Python string literal.
    Handles escaping of quotes.
    """
    if '"' in s:
        escaped = s.replace("'", "\\'")
        return f"'{escaped}'"
    else:
        escaped = s.replace('"', '\\"')
        return f'"{escaped}"'

def find_consume_spans(line):
    """
    Scans a line and returns a list of (start_index, end_index) tuples
    for every valid 'fdp.Consume...' function call.
    Uses parenthesis balancing to handle nested arguments correctly.
    """
    spans = []
    search_start = 0
    
    while True:
        # Find the start of the call: fdp.ConsumeSomething(
        match = re.search(r"fdp\.Consume\w*\(", line[search_start:])
        if not match:
            break
            
        # Calculate absolute indices in the line
        abs_start = search_start + match.start()
        abs_open_paren = search_start + match.end() - 1
        
        # Balance parentheses to find the matching closing ')'
        depth = 1
        found_end = False
        for i in range(abs_open_paren + 1, len(line)):
            if line[i] == '(':
                depth += 1
            elif line[i] == ')':
                depth -= 1
                if depth == 0:
                    # Found the closing parenthesis for this call
                    abs_end = i + 1
                    spans.append((abs_start, abs_end))
                    search_start = abs_end
                    found_end = True
                    break
        
        if not found_end:
            # Could not find a closing paren (likely malformed or multi-line), stop searching this line
            break
            
    return spans

def instrument_fuzzing_engine_python(data, args):
    logger.debug("Instrumenting the Python fuzzing engine")
    harness_file = args.harness
    with open(harness_file, 'r') as file:
        lines = file.readlines()

    # --- STEP 1: Count total calls using the robust parser ---
    consume_count = 0
    for line in lines:
        spans = find_consume_spans(line)
        consume_count += len(spans)

    logger.debug(f"Total fdp.Consume calls found: {consume_count}")

    # --- STEP 2: Prepare the Data (Your existing logic) ---
    temp = "\n".join(str(item) for item in data)
    temp = temp.replace("\n","\\n")
    temp += "\\n"
    new_data = [temp] # Default for 1 call

    if consume_count > 1:
        if data.count("") < consume_count - 1:
            logger.error("Not enough empty strings in data to split for all consume calls.")
            return ErrorCode.INCOMPATIBLE_DATA_ERROR
    
        current_group = []
        result = []
        combine_rest = False

        for s in data:
            if combine_rest:
                current_group.append(s)
            elif s == "":
                result.append("\\n".join(current_group))
                current_group = []
                if len(result) == consume_count - 1:
                    combine_rest = True
            else:
                current_group.append(s)
        
        result.append("\\n".join(current_group))
        new_data = result

    if len(new_data) != consume_count:
        logger.error(f"Mismatch: Found {consume_count} calls but prepared {len(new_data)} data chunks.")
        return ErrorCode.INCOMPATIBLE_DATA_ERROR

    # --- STEP 3: Instrument Lines Sequentially ---
    new_lines = []
    instrumented_idx = 0

    for line in lines:
        spans = find_consume_spans(line)
        
        if not spans:
            new_lines.append(line)
            continue
            
        # We replace from right to left to avoid invalidating indices of earlier matches
        current_line_content = line
        for start, end in reversed(spans):
            if instrumented_idx >= len(new_data):
                # Should not happen if count logic is consistent
                break
                
            # Get the data chunk for this specific call (logic maps linearly)
            # We need to map the CURRENT loop index to the data array index.
            # Since we are iterating reversed(spans), we need to calculate the forward index.
            # However, `instrumented_idx` is global across lines.
            
            # To fix the order: we must identify which global index corresponds to this span.
            # The spans in `find_consume_spans` are Left-to-Right.
            # So the first span uses `instrumented_idx`, the second `instrumented_idx + 1`.
            
            # Let's process Left-to-Right but build a new string to be safe.
            pass

        # Re-approach for safe replacement: Build the new line from parts
        new_line_parts = []
        last_idx = 0
        
        for start, end in spans:
            # Append text before the match
            new_line_parts.append(current_line_content[last_idx:start])
            
            # Append the replacement data
            val = new_data[instrumented_idx]
            replacement = to_string_literal(val)
            new_line_parts.append(replacement)
            
            instrumented_idx += 1
            last_idx = end
            
        # Append remaining text after the last match
        new_line_parts.append(current_line_content[last_idx:])
        new_lines.append("".join(new_line_parts))

    # --- Output ---
    new_harness_file = instrumented_harness_path(harness_file, args)
    with open(new_harness_file, 'w') as file:
        file.writelines(new_lines)

    return ErrorCode.SUCCESS

def instrument_fuzzing_engine(data, args):
    """
    Instrument the fuzzing engine to use the LLM generated inputs

    @param data: the input string list for the program execution
    """
    logger.debug(f"Data: {data}")
    success = SUCCESS
    global HARNESS_LANGUAGE
    if HARNESS_LANGUAGE == "C" or HARNESS_LANGUAGE == "C++":
        success = instrument_fuzzing_engine_c(data, args)
    elif HARNESS_LANGUAGE == "Python":
        success = instrument_fuzzing_engine_python(data, args)
    else:
        raise ValueError(f"Unsupported harness language: {HARNESS_LANGUAGE}")
    
    return success

def sync_globals(args):
    global HARNESS_LANGUAGE, TARGET_COMMENT, COVERED_COMMENT    
    if args.debug:
        logging.basicConfig(level=logging.DEBUG)
    else:
        logging.basicConfig(level=logging.INFO)
    
    if not os.path.isdir(args.testcase):
        raise NotADirectoryError(f"The provided path {args.testcase} is not a valid directory.")
    if not os.path.isfile(args.harness):
        raise FileNotFoundError(f"The provided path {args.harness} is not a valid file.")

    if args.harness.endswith(".c"):
        HARNESS_LANGUAGE = "C"
        TARGET_COMMENT = "//target"
        COVERED_COMMENT = "//covered"
    elif args.harness.endswith(".cpp") or args.harness.endswith(".cc"):
        HARNESS_LANGUAGE = "C++"
        TARGET_COMMENT = "//target"
        COVERED_COMMENT = "//covered"
    elif args.harness.endswith(".py"):
        HARNESS_LANGUAGE = "Python"
        TARGET_COMMENT = "#target"
        COVERED_COMMENT = "#covered"
    else:
        raise ValueError(f"Unsupported harness language: {args.harness}")

    logger.debug(f"HARNESS_LANGUAGE: {HARNESS_LANGUAGE}")
    logger.debug(f"TARGET_COMMENT: {TARGET_COMMENT}")
    logger.debug(f"COVERED_COMMENT: {COVERED_COMMENT}")


def gen_instrumented_fuzzer(args, target: tuple | None = None):
    sync_globals(args)
    if args.no_context:
        prompt = build_no_context_prompt(args, target)
    else:
        prompt = build_prompt(args, target)

    if args.random:
        data = [random_string()]
        if "lark" in args.testcase:  # lark's harness consumes several inputs
            for i in range(30):
                data.append(random_string())
                if i < 25:
                    data.append('')
        query_duration = None
    else:
        logger.debug(f"sleeping for 15 seconds before making API request...")
        time.sleep(15)
        response, query_duration, err_code = make_API_request(prompt, args)
        if response is None or err_code != ErrorCode.SUCCESS:
            return err_code, None
        data, err_code = process_response(response)
        if data is None or err_code != ErrorCode.SUCCESS:
            return err_code, None
    err_code = instrument_fuzzing_engine(data, args)
    return err_code, query_duration
