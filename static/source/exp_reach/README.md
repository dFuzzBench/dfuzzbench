# Where a coding agent's effort goes in crash reproduction (`exp_reach/`)

Measures how much of a coding agent's reasoning goes into reaching the vulnerable code. Claude Code (Claude
Opus 5) runs on 50 CyberGym tasks, each asking it to reproduce a known crash in a named target function; a
Gemini 3.1 Pro judge labels every step of the 50 trajectories with one primary activity, and the share of
reasoning (thinking) tokens per activity is computed per task (then averaged) and pooled over all tasks.

| file | role |
|---|---|
| `data/corpus_cybergym50.json` | the 50 tasks: CyberGym id (`arvo:<id>`), project, fuzz target and target (file, line, function, sanitizer class) |
| `data/corpus_cybergym_candidates.json` | the 100 CyberGym candidates the corpus was built from, in order |
| `data/codebook.txt` | the judge's instructions (read by `label_turns.py`), also the codebook for a second annotator |
| `derive_target.py`, `build_marker.py`, `batch_cybergym.py` | corpus construction: derive each task's target from its image's own crash report, build a marker image `local/dfuzzbench-arvo:<id>` that prints `HIT TARGET` at that line |
| `run_pilot.py`, `agent_mcp.py`, `start_pilot.sh` | the episodes: one Claude Code session per task with a `shell` and a `submit` tool in the task's container |
| `label_turns.py` | per-turn labels and the reachability shares |
| `recall_check.py` | robustness: the reachability share with and without episodes that may have recalled a PoC |
| `sample_for_audit.py`, `score_audit.py` | the blind second-annotator check of the judge |

## Requirements

- Docker and the task images `n132/arvo:<id>-vul` from Docker Hub (about 200 GB for the 50 tasks).
- Claude Code (`claude` on PATH, or `CLAUDE_BIN`), in a version that accepts the flags `run_pilot.py` passes
  (`--thinking-display`, `--max-budget-usd`, `--effort`, `--strict-mcp-config`). Credentials in the environment:
  `CLAUDE_CODE_OAUTH_TOKEN` (from `claude setup-token`) or `ANTHROPIC_API_KEY`; only the host claude processes
  receive it. Claude Code uses its own config dir (`DFUZZ_CLAUDE_CONFIG_DIR`, default
  `static/exp_results/exp_reach/config`), where it also writes the session files the labelling reads.
- `GEMINI_API_KEY` (or `GOOGLE_API_KEY`) for the labelling; Python with `static/requirements.txt`.

Run everything from `static/source`. Outputs go to `static/exp_results/exp_reach/<name>/` (git-ignored;
`DFUZZBENCH_RESULTS` moves the root).

## Pipeline

1. **Marker images** for the 50 tasks (pulls each task image, derives the target, builds and checks the marker):

   ```bash
   python exp_reach/batch_cybergym.py --candidates exp_reach/data/corpus_cybergym50.json \
       --out ../exp_results/exp_reach/corpus-rebuilt.json --workers 2
   ```

   `data/corpus_cybergym50.json` was built the same way from `data/corpus_cybergym_candidates.json` (one task
   per project, stopping at 50 verified markers). No new build starts once `--max-seconds` have passed; run the command again
   to continue, since images that exist are kept.

2. **Episodes** (the defaults are the experiment's settings: `claude-opus-5`, effort high, at most 120 turns and a
   $10 cost cap per episode, one episode per task; `--episode-timeout` sets a wall-clock limit per episode):

   ```bash
   exp_reach/start_pilot.sh --name cybergym --corpus exp_reach/data/corpus_cybergym50.json --concurrency 5
   python exp_reach/run_pilot.py status --name cybergym
   python exp_reach/run_pilot.py report --name cybergym
   ```

   Each episode runs in a `--network none` container from the task image with the stored reproducer, git
   history and seed corpora removed. The agent has no native Claude Code tools; it works through `shell` (a
   command in the container) and `submit` (runs a candidate input file; success = the target line is reached
   and the task's sanitizer class is reported, judged out of band on the marker image). Per episode:
   `transcript.jsonl` (stream-json), `session.jsonl` (per-step tokens and timestamps), `submissions.jsonl`,
   `result.json`.

3. **Labelling** (Gemini 3.1 Pro, temperature 0, the whole trajectory in one request; deterministic command rules
   take precedence on tool-only turns):

   ```bash
   python exp_reach/label_turns.py ../exp_results/exp_reach/cybergym --workers 32
   ```

   Writes `episodes/<id>/labels.jsonl` and `reach_share.json`, and prints the shares by turns, output tokens and
   thinking tokens (narrow = Reachability, broad = Reachability or reachability-related) with bootstrap 95% CIs,
   and the share of thinking tokens per category, both as the mean over tasks and pooled over all tasks.

   `python exp_reach/recall_check.py <run_dir>` then reports the thinking-token share with and without the
   reproduced episodes that show little surfaced reachability work (a check that PoC recall does not drive it).

4. **Judge check**: a blind sample stratified by the judge's category, labelled by a second annotator with
   `data/codebook.txt`, then scored (agreement and Cohen's kappa on the 6-way category, narrow and broad):

   ```bash
   python exp_reach/sample_for_audit.py ../exp_results/exp_reach/cybergym      # audit_sample.json + _blind.json
   python exp_reach/score_audit.py ../exp_results/exp_reach/cybergym/audit_sample.json labels.json
   ```

   The annotator labels `audit_sample_blind.json` as `[{"uid", "category", "reachability_related"}]`.
   `sample_for_audit.py` takes any number of labelled runs and draws `--per-cat` turns per judge category with a
   fixed seed.

## Cheap checks

```bash
python exp_reach/label_turns.py <run_dir> --only <id> --dry   # turn extraction and rule tags, no API call
python exp_reach/label_turns.py <run_dir> --only <id>         # one Gemini request; writes that episode's labels.jsonl
```

With `--only`, the run-level `reach_share.json` is left as it is.
