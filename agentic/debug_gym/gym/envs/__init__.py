from debug_gym.gym.envs.env import RepoEnv, TooledEnv
from debug_gym.gym.envs.dfuzz_bench import DFuzzBenchEnv

def select_env(env_type: str = None) -> type[RepoEnv]:
    match env_type:
        case None:
            return RepoEnv
        case "dfuzzbench":
            return DFuzzBenchEnv
        case _:
            raise ValueError(f"Unknown benchmark {env_type}")
