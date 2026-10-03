import json
import time
import datetime

from debug_gym.agents.base_agent import BaseAgent, register_agent
from debug_gym.gym.envs.env import RepoEnv
from debug_gym.llms.base import LLM
from debug_gym.logger import DebugGymLogger

# TODO: think of a better way to pass the fuzzing harness filename
FUZZING_HARNESS = {
    "bleach": "instrumented_fuzzer.py",
    "clib": "instrumented_fuzzer.c",
    "cmark": "instrumented_fuzzer.c",
    "connexion": "instrumented_fuzzer.py",
    "cpp-httplib": "instrumented_fuzzer.cc",
    "exiv2": "instrumented_fuzzer.cpp",
    "filesystem_spec": "instrumented_fuzzer.py",
    "glslang": "instrumented_fuzzer.cc",
    "guetzli": "instrumented_fuzzer.cc",
    "html2text": "instrumented_fuzzer.py",
    "html5lib-python": "instrumented_fuzzer.py",
    "lark-parser": "instrumented_fuzzer.py",
    "libbpf": "instrumented_fuzzer.c",
    "libpg_query": "instrumented_fuzzer.c",
    "libpng": "instrumented_fuzzer.cc",
    "md4c": "instrumented_fuzzer.c",
    "ninja": "instrumented_fuzzer.cc",
    "openh264": "instrumented_fuzzer.cpp",
    "protobuf-c": "instrumented_fuzzer.cpp",
    "python-markdown": "instrumented_fuzzer.py",
    "requests": "instrumented_fuzzer.py",
    "varnish": "instrumented_fuzzer.c",
    "w3m": "instrumented_fuzzer.c",
    "wamr": "instrumented_fuzzer.cc",
    "rich": "instrumented_fuzzer.py",
}

@register_agent
class ReachAgent(BaseAgent):
    name = "reach_agent"
    # TODO: add location of the target line in the prompt target-wise.

    def __init__(
        self,
        config: dict,
        env: RepoEnv,
        llm: LLM | None = None,
        logger: DebugGymLogger | None = None,
        problem: str | None = None,
    ):
        super().__init__(config, env, llm, logger)

        target_project = problem.split("::")[0]
        path_part, line_number_str = problem.split("::")[1].rsplit(":", 1)
        target_line_number = int(line_number_str)
        target_path = path_part.replace(":", "/")

        harness_file = FUZZING_HARNESS[target_project]
        is_python_program = harness_file.endswith('.py')

        if is_python_program:
            debugger_tool = "pdb"
            debugger_start_instruction = "" # pdb usually starts automatically or via the tool wrapper
        else:
            debugger_tool = "lldb"
            # LLDB specifically requires the user to launch the process after loading
            debugger_start_instruction = (
                "    * **Crucial LLDB Step:** Unlike pdb, lldb loads the binary but does NOT start it immediately. "
                "You must set your breakpoints first, then issue the `run` (or `r`) command to begin execution.\n"
            )
            # Increment the target line number since we will instrument two include directives (i.e., stdio.h and stdlib.h)
            target_line_number += 2

        self.system_prompt = (
            "### Task\n"
            "You are an agent specialized in generating directed inputs to reach the target line in a given project. "
            "You will be given a project with multiple programs and one of which contains a line marked with the comment 'target'. "
            f"The target line is located at '{target_path}' at line {target_line_number}. This line represents the target that must be executed. "
            "Your task is to analyze the programs and generate input that guarantees execution of the target line. "
            f"The program named '{harness_file}' serves as the entry point for execution, accepting input strings to test and exercise the project. "
            f"You have to generate input that will replace the 'EXAMPLE_INPUT' placeholder in the '{harness_file}' to cover the target line. "
            "You have access to a set of tools to help you understand the code before proposing an input. \n\n"

            "### Core Strategy\n"
            "Your primary goal is to **find the control flow path** from the entry point to the target line. "
            "You must identify all conditions (e.g., `if` statements, data checks) required to stay on this path. Your generated input must satisfy all of these conditions.\n\n"

            "### Workflow\n"
            "**Step 1: Initial Analysis & Plan**\n"
            f"* **Use `get_context` first.** This tool will provide the source code context that are likely relevant to reaching the target line. Then you can try to propose inputs based on the context. \n"
            f"* **If initial attempt failed:** Try `get_callpaths` which may provide one or more known execution paths to the target. These paths are structured as a list of steps, each with a file, function, and optional class.\n"
            f"* **If call paths are found:** This is your primary guide. Your plan should be to analyze this path. Use `view_code` and `grep` to examine the files and functions listed in the call path to understand the logic and identify the specific conditions (e.g., `if` statements) at each step.\n"
            f"* **If no call paths are found:** You must discover the path manually. Start by using `view_code` and `grep` on the entry point (`{harness_file}`) and the target file (`{target_path}`) to understand the high-level connection.\n"
            "* **After your initial analysis,** use `update_plan` to create your plan. This plan should detail the steps you will take to verify the conditions along the path.\n"
            "**Plan Guidelines:** Focus only on necessary actions you can perform. Skip planning for simple tasks.\n\n"
            
            f"**Step 2: Investigate with {debugger_tool.upper()}**\n"
            "* This is your primary tool for discovering the path conditions.\n"
            f"{debugger_start_instruction}"
            "* **Debugging Workflow:**\n"
            "    1.  Identify relevant files and line numbers (e.g., conditional branches on the path). If you have a call path, use it to select what to inspect.\n"
            "    2.  Set breakpoints (`b file:line`) at these key locations.\n"
            "    3.  Continue execution (`c`) or Run (`r`) to reach your breakpoint.\n"
            "    4.  Use `print` (or `p`) to inspect variable values and understand the program's state.\n"
            "* Your goal is to determine *what values* are needed to pass each checkpoint.\n\n"
            
            "**Step 3: Update Plan As You Go**\n"
            "* Before running a command, consider whether or not you have completed the previous step. "
            f"* After completing a step or gaining a key insight (e.g., from {debugger_tool}), you must call update_plan to mark the step 'completed' and record your 'findings' (e.g., 'Findings: To pass line 42, input must contain an HTML table element')\n"
            "* If your strategy changes, update the plan and explain your rationale.\n\n"
            
            "**Step 4: Propose Input**\n"
            "* Once you are confident your input satisfies *all* conditions on the path, call `propose_input` with the input string.\n"
            "* The tool will verify if the target line was executed.\n\n"
            
            "**Step 5: Iterate on Feedback**\n"
            "* If your input fails, `propose_input` will provide feedback. The executed lines will be updated with 'COVERED' comments indicating the lines your input *did* execute.\n"
            "* Use `view_code` to see the 'COVERED' lines.\n"
            f"* Analyze where your input went wrong. Update your plan, return to `{debugger_tool}` to investigate the divergence, and propose a *new* input.\n\n"            

            "### Rules & Constraints\n"
            "* You can only call **one tool at a time**.\n"
            "* Output your thinking process (if any) and then the tool call.\n"
            "* **Do not assume** you know the code, even if it looks familiar. Use the tools to investigate.\n"
            "* **Do not repeat** a tool call with the exact same parameters if it failed (especially `propose_input`). Re-evaluate your strategy first.\n"
            f"* If stuck, re-evaluate your plan and use `{debugger_tool}` to gather more information.\n"
        )

    def run(self, task_name=None, debug=False):
        start_time = time.perf_counter()
        step = 0
        info = None
        max_steps = self.config["max_steps"]
        curr_plan = None
        try:
            self.history.reset()
            info = self.env.reset(options={"task_name": task_name})
            # initial state does not have prompt and response
            self.history.step(info, None)

            if info.done is True:
                self.logger.report_progress(
                    problem_id=task_name,
                    step=1,
                    total_steps=1,
                    score=info.score,
                    max_score=info.max_score,
                    status="resolved",
                )
                return True

            self.logger.info(
                "Available tools (in LLM's tool calling format):\n"
                f"{json.dumps(self.llm.define_tools(info.tools), indent=4)}\n"
            )

            highscore = info.score
            for step in range(max_steps):
                self.logger.info(f"\n{'='*20} STEP {step+1} {'='*20}\n")
                highscore = max(highscore, info.score)
                self.logger.info(
                    f"[{task_name[:10]:<10}] | Step: {step:<4} | Score: {info.score:>4}/{info.max_score:<4} ({info.score/info.max_score:.1%}) [Best: {highscore}]"
                )

                messages = self.build_prompt(info)
                llm_response = self.llm(messages, info.tools)

                if llm_response.tool.name == "update_plan":
                    curr_plan = llm_response.tool.arguments["plan"]

                if debug:
                    breakpoint()

                info = self.env.step(
                    llm_response.tool,
                    llm_response.response,
                    llm_response.reasoning_response,
                    curr_plan,
                )
                self.history.step(info, llm_response)

                if (
                    info.done
                    or info.propose_counter >= self.config["max_propose_steps"]
                ):
                    reason = "done" if info.done else "max_propose_steps reached"
                    self.logger.info(
                        f"Step: {step} | Score: {info.score}/{info.max_score} ({info.score/info.max_score:.1%}) | Reason: {reason}"
                    )
                    # early stop, set current step and total steps to be the same
                    self.logger.report_progress(
                        problem_id=task_name,
                        step=step + 1,
                        total_steps=step + 1,
                        score=info.score,
                        max_score=info.max_score,
                        status="resolved" if info.done else "unresolved",
                    )
                    break
                # keep progress bar running until max_steps is reached
                self.logger.report_progress(
                    problem_id=task_name,
                    step=step + 1,
                    total_steps=max_steps + 1,
                    score=info.score,
                    max_score=info.max_score,
                    status="running",
                )
            # max_steps was reached, task was either resolved or unresolved
            self.logger.report_progress(
                problem_id=task_name,
                step=step + 1,
                total_steps=step + 1,
                score=info.score,
                max_score=info.max_score,
                status="resolved" if info.done else "unresolved",
            )
            return info.done
        except Exception:
            # report any error that happens during the run
            self.logger.report_progress(
                problem_id=task_name,
                step=step + 1,
                total_steps=step + 1,
                score=info.score if info else 0,
                max_score=info.max_score if info else 1,
                status="error",
            )
            raise
        finally:
            end_time = time.perf_counter()
            total_latency_str = str(datetime.timedelta(seconds=int(end_time - start_time)))
            self.logger.info(f"Task {task_name} E2E latency: {total_latency_str}")
            self.logger.info(f"Closing environment for task {task_name}")
            self.env.close()
