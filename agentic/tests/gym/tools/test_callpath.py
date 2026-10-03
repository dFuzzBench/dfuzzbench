import inspect
import json
from types import SimpleNamespace

from debug_gym.gym.envs.dfuzz_bench import DFuzzBenchEnv
from debug_gym.gym.tools.callpath import CallPathTool
from debug_gym.gym.tools.context import ContextTool

NO_PATHS = "No valid call paths identified from the static call graph."


def test_dfuzzbench_env_withholds_call_paths_by_default():
    # Call paths are withheld unless the env sets provide_call_paths=True.
    params = inspect.signature(DFuzzBenchEnv.__init__).parameters
    assert params["provide_call_paths"].default is False


def test_callpath_from_json_string():
    paths = [
        [
            {"file": "instrumented_fuzzer.py", "function": "TestOneInput"},
            {"file": "lark/lark.py", "class": "Lark", "function": "__init__"},
        ]
    ]
    text = json.dumps(paths, indent=2)
    obs = CallPathTool().use(SimpleNamespace(realistic_call_paths=text))
    assert obs.source == "get_callpaths"
    assert obs.observation == "1 paths are retrieved as follows:\n" + text


def test_callpath_from_list():
    paths = ["fuzz.cc::LLVMFuzzerTestOneInput -> a.c::parse", "fuzz.cc::LLVMFuzzerTestOneInput -> b.c::read"]
    obs = CallPathTool().use(SimpleNamespace(realistic_call_paths=paths))
    assert obs.observation == "2 paths are retrieved as follows:\n" + json.dumps(paths, indent=2)


def test_callpath_none_or_empty():
    for value in [None, "", "[]", []]:
        obs = CallPathTool().use(SimpleNamespace(realistic_call_paths=value))
        assert obs.observation == NO_PATHS
    assert CallPathTool().use(SimpleNamespace()).observation == NO_PATHS


def test_context():
    obs = ContextTool().use(SimpleNamespace(realistic_context="<code>x</code>"))
    assert obs.source == "get_context"
    assert obs.observation == "<code>x</code>"
    obs = ContextTool().use(SimpleNamespace(realistic_context=""))
    assert obs.observation == "No realistic context available for this target."
