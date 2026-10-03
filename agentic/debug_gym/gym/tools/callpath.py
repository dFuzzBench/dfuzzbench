import json

from debug_gym.gym.entities import Observation
from debug_gym.gym.tools.tool import EnvironmentTool
from debug_gym.gym.tools.toolbox import Toolbox

@Toolbox.register()
class CallPathTool(EnvironmentTool):
    name: str = "get_callpaths"
    description = (
        "Retrieves the list of valid function call paths starting from the fuzzing harness to the target function. "
        "The retrieved data is a list, where each item in the list is a single call path. "
        "Each call path is itself a list of steps, ordered from the harness to the target. "
        "Each step is an object the following keys: "
        "- file: The file name (e.g., 'main.c')."
        "- function: The function name (e.g., 'parse_args')."
        "- class (optional): The class name, if the function is a method. If this key is not present, the function is a regular (non-class) function."
    )

    
    # This tool takes no arguments from the agent.
    arguments = {}

    def use(self, environment) -> Observation:
        # A JSON string (dfuzzBench dataset) or a list (ARVO dataset); None when the env withholds them.
        call_paths = getattr(environment, "realistic_call_paths", None)
        if isinstance(call_paths, str):
            paths = json.loads(call_paths) if call_paths.strip() else []
        else:
            paths = list(call_paths) if call_paths is not None else []
            call_paths = json.dumps(paths, indent=2)
        if not paths:
            obs = "No valid call paths identified from the static call graph."
        else:
            obs = f"{len(paths)} paths are retrieved as follows:\n" + call_paths
        return Observation(self.name, obs)
