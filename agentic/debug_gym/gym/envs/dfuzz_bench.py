import os
import re
import docker
from pathlib import Path

from debug_gym.gym.entities import EvalOutput
from debug_gym.gym.envs.env import RepoEnv
from debug_gym.gym.envs.local_dataset import load_local_dataset

from debug_gym.constants import DEBUG_GYM_CACHE_DIR
from debug_gym.gym.terminal import DockerTerminal, Terminal
from debug_gym.gym.utils import filter_problems

# The T1-T5 dataset, written by `python data/dfuzzbench/build_dataset.py --out-dir data/dfuzzbench/dataset`.
DEFAULT_DATASET_DIR = "data/dfuzzbench/dataset"


class DFuzzBenchEnv(RepoEnv):
    CACHE = DEBUG_GYM_CACHE_DIR / "dfuzz-bench"

    def __init__(
        self,
        dataset_id: str = DEFAULT_DATASET_DIR,
        entrypoint: str = "pwd", # TODO: change to proper entrypoint
        debug_entrypoint: str = "python3 -m pdb instrumented_fuzzer.py", # TODO: change to proper debug entrypoint
        split: str = "test",
        terminal: Terminal | None = None,
        provide_call_paths: bool = False,
        **kwargs,
    ):
        terminal = terminal or DockerTerminal(logger=kwargs.get("logger"))
        if not isinstance(terminal, DockerTerminal):
            raise ValueError("DFuzzBenchEnv only supports DockerTerminal.")
        
        self.dataset_id = dataset_id
        self.split = split
        # False (default): get_callpaths reports that no call path was found; True exposes the dataset's call paths.
        self.provide_call_paths = provide_call_paths
        self.current_score = 0

        super().__init__(entrypoint=entrypoint, debug_entrypoint=debug_entrypoint, terminal=terminal, **kwargs)

    def load_dataset(self, problems: str | list[str] | None = None):
        # dataset_id is the local dataset directory (relative to the working directory or to agentic/).
        self.ds = load_local_dataset(
            self.dataset_id,
            self.split,
            build_command=f"python data/dfuzzbench/build_dataset.py --out-dir {self.dataset_id}",
        )
        dataset = {id: i for i, id in enumerate(self.ds["target_id"])}
        problems = filter_problems(dataset, problems)
        dataset = {id: i for id, i in dataset.items() if id in problems}

        instance_ids = [id for id in dataset]
        image_names = set(
            f"{id.lower().replace("::", ".").replace(":", ".").replace("._", ".0_").replace("_.", "_0.")}" for id in instance_ids
        )

        client = docker.from_env()
        tagged_image_names = set(f"local/dfuzzbench:{id}" for id in image_names)

        existing_images = set(
            tag for image in client.images.list() for tag in image.tags
        )
        missing_images = tagged_image_names - existing_images
        if missing_images:
            # local/ images exist on no registry: build them, do not pull.
            raise RuntimeError(
                f"{len(missing_images)} Docker image(s) missing, e.g. `{sorted(missing_images)[0]}`. "
                "Build each target's image with scripts/build_dfuzzbench_image.sh (see README.md)."
            )

        return dataset
    
    def setup_task(self, task_name: str, options: dict = None):
        if task_name not in self.dataset:
            raise ValueError(
                f"Task `{task_name}` was not found in dataset. The available tasks are: {sorted(self.dataset)}.\n"
                "Please provide a valid task or initialize the environment without problems to load all tasks."
            )
        
        self.task_name = task_name
        self.ds_row = self.ds[self.dataset[self.task_name]]
        # self.logger.debug(f"Setting up task {self.task_name}: {self.ds_row}")
        self.target_id = self.ds_row["target_id"]
        self.image_name = f"local/dfuzzbench:{self.target_id.lower().replace('::', '.').replace(':', '.').replace('._', '.0_').replace('_.', '_0.')}"
        self.harness_name = self.ds_row["harness_name"]
        self.project_name = self.target_id.split("::")[0]
        self.realistic_call_paths = self.ds_row["realistic_call_paths"] if self.provide_call_paths else None
        self.realistic_context = self.ds_row["realistic_context"]
        self.working_dir = Path("/src/testbed")

    def setup_workspace(self):
        self.terminal.base_image = self.image_name
        self.logger.debug(f"Setting up dfuzzBench workspace with image {self.terminal.base_image}...")
        # Ignore hidden files (dotfiles) and any contents under hidden directories
        self.workspace.reset(ignore_patterns=["**/.*"])
        self.set_entrypoints(self.entrypoint, self.debug_entrypoint)
    
    def setup_terminal(self):
        # TODO: check what setup is needed to complete here
        self.logger.info(f"Configuring {self.terminal}...")
        
        # Install tree for listdir
        self.terminal.run("apt update && apt install -y tree")

        if self.harness_name.endswith('.py'):
            self.logger.debug("Solving targets in Python projects")
            self.terminal.run("pip3 install coverage")
            self.terminal.run("pip3 install --upgrade coverage", raises=True)
            if "html5lib-python::" in self.task_name :
                self.terminal.run("pip3 install -e . --no-build-isolation", raises=True)
            else:
                self.terminal.run("pip3 install -e .", raises=True)
        else:
            self.logger.debug("Solving targets in C/C++ projects")
            self.terminal.run("apt install -y lldb-18")
            self.terminal.run("ln -sf /usr/bin/lldb-18 /usr/bin/lldb")
            self.terminal.run("lldb --version")

    def set_entrypoints(self, entrypoint: str, debug_entrypoint: str | None = None):
        pass

    def calculate_max_score(self, eval_output):
        # Only one target needs to be covered per task
        return 1
    
    def calculate_score(self, eval_output: EvalOutput) -> int:
        self.logger.debug(f"[calculate_score] eval_output: {eval_output}")

        return self.current_score
    
    def validate(self) -> tuple[bool, str]:
        success, exec_output, coverage = self.workspace.validate_reachability(self.harness_name, self.project_name)
        if success:
            self.current_score = 1
            output = "Current input successfully reach the target line!"
        else:
            # instrument the covered lines with inline comments
            self.workspace.instrument_coverage(coverage, self.harness_name)
            output = f"Current input failed to reach the target line. The covered lines are instrumented with 'COVERED' inline comments, which you can check with the 'view_code' tool. The execution output is as follows:\n{exec_output}\n"
        return success, output

    def eval(self, **kwargs) -> EvalOutput:
        success, output = self.validate()
        self.last_eval = EvalOutput(success, output)
        return self.last_eval
        


