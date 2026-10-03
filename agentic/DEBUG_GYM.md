# debug-gym: A Text-Based Environment for Interactive Debugging

The reach agent is built on [`debug-gym`](https://github.com/microsoft/debug-gym) (MIT license,
see [`LICENSE`](LICENSE)), a text-based interactive debugging framework by Microsoft Research
([Technical Report](https://arxiv.org/abs/2503.21557), [Project Page](https://aka.ms/debug-gym/)).
This page keeps the parts of the upstream documentation that still apply to this fork. The fork
keeps the framework (environment, terminals, tools, agents, LLM backends) and the `dfuzzbench`
benchmark; the upstream benchmarks (SWE-bench, SWE-smith, R2E-Gym, Aider, mini_nightmare) and
their data, notebooks and analysis tools are removed. See [`README.md`](README.md) for how to build
the local datasets and run the reach-agent experiments.

## 1. LLM configuration (`llm.yaml`)

An agent run names its model with `llm_name` in the run config; debug-gym looks that name up in an
LLM config file, read from, in this order:

1. `llm_config_file_path` in the run config (e.g. `-p base.llm_config_file_path=path/to/llm.yaml`),
2. the file named by the environment variable `LLM_CONFIG_FILE_PATH`,
3. `$HOME/.config/debug_gym/llm.yaml`.

[`llm.yaml`](llm.yaml) in this directory holds the Gemini entries of the experiment configs
(`gemini-3.1-pro`) and of the smoke tests (`gemini-3.5-flash`), so `export
LLM_CONFIG_FILE_PATH=$PWD/llm.yaml` is enough. A string value written as `"${VAR}"` is read from the
environment variable `VAR` (and is unset if `VAR` is), so keys stay out of the file:

```yaml
gemini-3.1-pro:
  model: gemini-3.1-pro-preview          # model id sent to the endpoint
  tokenizer: gpt-4o                      # tiktoken tokenizer used to count prompt tokens
  endpoint: https://generativelanguage.googleapis.com/v1beta/openai/
  api_key: "${GEMINI_API_KEY}"
  context_limit: 500                     # in thousands of tokens
  generate_kwargs: {...}                 # extra arguments of every chat.completions call
```

An entry without the tags `azure openai`, `anthropic`, `copilot openai` or `copilot claude` is
served by `OpenAILLM`, i.e. any OpenAI-compatible endpoint (Gemini, OpenAI, vLLM). For a vLLM model,
add the tag `vllm`. `python -m debug_gym.llms.configure` writes the upstream template (Azure OpenAI,
vLLM and Anthropic examples) to `$HOME/.config/debug_gym/llm.yaml`. For Azure, `az login` or
Managed Identity authentication can replace `api_key` with `scope`.

## 2. System design

```bash
debug_gym
├── gym
│   ├── envs       # RepoEnv, the dfuzzbench env and its local dataset loader (the arvo env is added by arvo.patch)
│   ├── terminal   # local and Docker terminals
│   └── tools
├── agents
└── llms
```

`debug_gym.gym` is the environment. Given a code repository, an agent interacts with a set of
tools to investigate the code. `RepoEnv` follows the [Gymnasium](https://github.com/Farama-Foundation/Gymnasium)
paradigm: `env.reset()` starts an episode and `env.step(action)` runs one tool call and returns the
next observation. `debug_gym.agents` are the LLM-based agents, and `debug_gym.llms` the LLM
backends (OpenAI-compatible, Azure OpenAI, Anthropic, and a human mode).

> [!WARNING]
> Interactive sessions use a pseudo-terminal (PTY), so the `pdb` and `lldb` tools only work on Linux.

### 2.1. Tools

Tools are registered in a toolbox and added to an environment by name (the `tools` list of a run
config). The name in the config is the left column; the agent sees the name in parentheses.

| Config name | Description |
| :-: | :----- |
| `context` (`get_context`) | Returns the target's realistic context (the statically retrieved context of the single-turn evaluation). |
| `callpath` (`get_callpaths`) | Returns the static-call-graph paths from the harness to the target if the env provides them (env option `provide_call_paths`, off by default). |
| `view` (`view_code`) | Shows a file, optionally a line range, with line numbers and breakpoints. |
| `listdir` | Returns the directory tree at a given subdirectory. |
| `grep` | Searches for a literal or regular-expression pattern in the repository. |
| `plan` (`update_plan`) | Records the agent's step-by-step plan and its findings. |
| `propose` (`propose_input`) | Runs the harness on the proposed input and reports whether the target line was reached; otherwise it marks the covered lines with `COVERED` comments. |
| `pdb` | Interactive Python debugger. With `persistent_breakpoints`, breakpoints survive a restart of the session. |
| `lldb` | Interactive LLDB debugger for C/C++ harnesses. |
| `eval`, `rewrite`, `bash` | Upstream tools (run the entrypoint, edit code, run a shell command); the reach agent does not use them. |

`.debugignore` and `.debugreadonly` files in a repository (same syntax as `.gitignore`) hide files
from the agent or make them read-only; the dfuzzbench images mark the fuzzing harness read-only.

### 2.2. Agents

| Agent name | Description |
| :-: | :----- |
| `reach_agent` | The reach agent: a ReAct loop that is told to call `get_context` first and stops when the target is reached, after `max_steps` LLM calls, or after `max_propose_steps` proposals. |
| `debug_agent`, `rewrite_agent`, `debug_5_agent` | Upstream example agents for program repair; kept as framework examples and for the unit tests. |

## 3. Running an agent

    python scripts/run.py <run config>.yaml --agent <agent name>

Add `-v` for verbose output, `-n <workers>` to run several problems in parallel, `--list` to list
the problems, or `--debug` to break before every action (press `c` to continue).

**Overriding values in the config.** `-p` overrides any key of the run config, e.g.
`-p base.max_steps=5 "base.problems=['<target id>']"`.

**Human mode.** With `llm_name: "human"` (e.g. `-p base.llm_name=human`), the environment expects
each command from the keyboard, in tool-calling format; the `Tab` key lists the tool-calling
templates.

**Custom system prompt.** `system_prompt_template_file` in the agent config points to a
[Jinja](https://jinja.palletsprojects.com/) template that replaces the default system prompt. The
template sees the `agent` and `info` objects and two extra filters: `to_pretty_json` and
`trim_message` (arguments `max_length`, `max_length_percentage` and `where`, which trims a message
to fit a token budget). For example:

```jinja
Task: {{ agent.system_prompt }}

Directory Tree:
{{ info.dir_tree | trim_message(max_length_percentage=0.1, where="end") }}
```

## Citation

```
@article{yuan2025debuggym,
  title={debug-gym: A Text-Based Environment for Interactive Debugging},
  author={Xingdi Yuan, Morgane M Moss, Charbel El Feghali, Chinmay Singh, Darya Moldavskaya, Drew MacPhee, Lucas Caccia, Matheus Pereira, Minseon Kim, Alessandro Sordoni, Marc-Alexandre C\^ot\'e},
  journal={arXiv preprint arXiv:2503.21557},
  year={2025},
  url={https://arxiv.org/abs/2503.21557}
}
```
