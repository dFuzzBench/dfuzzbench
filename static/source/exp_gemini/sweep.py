#!/usr/bin/env python3
"""Gemini sweep over the static benchmark: the driver of the Gemini 3.1 Pro memorization controls (No-source,
No-source perturbed, Random), the knowledge-cutoff comparison and the T6 inputs behind the PoC-similarity check.

Everything that defines a result is the artifact's own code, called unchanged: the prompt
(evaluation_project.preprocess_dir + llm_fuzz_integration.build_prompt), response parsing
(process_response), harness instrumentation (instrument_fuzzing_engine), and the docker
build / compile / run commands of docker_utils. Around that, this driver adds only what the
experiment needs:
  generation    per-model RPM/TPM limiting; retries of transient API errors (429 forever,
                5xx/timeouts 10x, then deferred to the end); responses the API withheld (blocked
                prompt, filtered answer) asked again until the model answers; the rounds of a
                target sent close together so the repeated prompt hits Gemini's implicit cache.
  verification  one run per unique (target, instrumented harness), shared by all rounds and
                models; a bounded number of executions instead of the 60 s window (the harness
                input is fixed, so every execution is identical); truncated logs; concurrency
                that follows the machine's load.
  bookkeeping   resumable JSONL state, a status line every minute, and a report in the
                artifact's CSV format.

    sweep.py run    --name NAME --models M [M ...] [--setting S] [--rounds 20] [--projects P ...] [--targets T ...]
                    [--benchmark JSON]   (ARVO-style settings t6_*: default static/data/target-arvo/benchmark.json)
    sweep.py status --name NAME
    sweep.py report --name NAME [--projects P ...]

Everything a run writes is under $DFUZZBENCH_RESULTS/<name>/ (default static/exp_results/<name>/). The API key
comes from $GEMINI_API_KEY (or $GOOGLE_API_KEY) and is dropped from the environment at start-up, so no child
process (docker) ever sees it.
"""
import argparse
import collections
import dataclasses
import fcntl
import glob
import hashlib
import itertools
import json
import logging
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import traceback
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
from math import comb

SOURCE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATIC_DIR = os.path.dirname(SOURCE_DIR)
DATA_DIR = os.path.join(STATIC_DIR, "data", "target-latest")
T6_BENCHMARK = os.path.join(STATIC_DIR, "data", "target-arvo", "benchmark.json")
RESULTS_ROOT = os.path.abspath(os.environ.get("DFUZZBENCH_RESULTS") or os.path.join(STATIC_DIR, "exp_results"))
# This checkout's artifact code must win over any other checkout installed in the environment.
sys.path[:0] = [SOURCE_DIR, os.path.join(STATIC_DIR, "docker-utils")]

import evaluation_project as ep  # noqa: E402  (artifact)
import llm_fuzz_integration as lfi  # noqa: E402  (artifact)
import docker_utils  # noqa: E402  (artifact)
import evaluation_arvo as ea  # noqa: E402  (artifact)
from docker_utils import RUNNER_IMAGE, _env_to_docker_args, cleanup_run, image_name_for, out_dir_for  # noqa: E402

HARNESS_FILES = {  # as in evaluation_multi_projects_conc.py
    "bleach": "fuzzer.py", "clib": "fuzzer.c", "cmark": "fuzzer.c", "cpp-httplib": "fuzzer.cc",
    "exiv2": "fuzzer.cpp", "filesystem_spec": "fuzzer.py", "guetzli": "fuzzer.cc",
    "html5lib-python": "fuzzer.py", "lark-parser": "fuzzer.py", "libbpf": "fuzzer.c",
    "libpng": "fuzzer.cc", "md4c": "fuzzer.c", "rich": "fuzzer.py", "varnish": "fuzzer.c", "wamr": "fuzzer.cc",
}
TIERS = ["easy", "medium", "hard", "extreme_hard", "unreachable"]  # T1..T5
SETTINGS = {  # --setting -> (ARVO-style?, the artifact's --no_context value, the context dir the targets come from)
    "realistic": (False, None, "realistic"), "oracle": (False, None, "oracle"), "bm25": (False, None, "bm25"),
    "no_context": (False, "original", "realistic"),                  # No-source
    "no_context_placeholder": (False, "placeholder", "realistic"),   # No-source (perturbed)
    "t6_realistic": (True, None, "realistic"), "t6_no_context": (True, "original", "realistic"),
    "t6_no_context_placeholder": (True, "placeholder", "realistic"),
}
LANGUAGE = {p: ("Python" if h.endswith(".py") else "C" if h.endswith(".c") else "C++") for p, h in HARNESS_FILES.items()}
PRICES = {  # USD per 1M tokens, paid tier, prompts <= 200K: (input, cached input, output incl. thinking)
    "gemini-3.1-pro-preview": (2.00, 0.20, 12.00),
    "gemini-3.8-flash": (0.75, 0.075, 3.75),
    "gemini-3-flash-preview": (0.50, 0.05, 3.00),
}
ERROR_FIELDS = ["json_decode_err", "exceed_context_window_err", "pattern_unmatch_err", "incompatible_data_err",
                "copyright_err", "rate_limit_err", "other_err"]  # the artifact's per-round error columns

# Per-request timeout: --api-timeout (default 1800 s). 600 s cut off 3-flash's long thinking on hard targets.
MAX_TRANSIENT_ATTEMPTS = 10
# A response the API withheld (prompt blocked, or the answer filtered) is asked again until the model
# answers. Guards against targets that are (nearly) always refused, per (model, target):
ALWAYS_BLOCKED_AFTER = 40      # refusals without a single answer -> stop asking, report the target
MAX_REFUSALS_PER_TARGET = 200  # ~10x the rounds -> stop asking, report the rest of the target
# --retry-unanswered: a response without an input the artifact can parse -- a refusal in text, an empty answer,
# a recitation stop -- is kept in attempts.jsonl and its round is asked again, until --max-requests-per-target.
# An input of the wrong shape (incompatible_data_err) is an answer: it counts as a miss and is not retried.
RETRY_ERRORS = {"pattern_unmatch_err", "other_err", "copyright_err"}
REFUSED_TARGET_INFLIGHT = 2   # a target refused >= half the time (10+ refusals) gets at most this many requests
                              # in flight, so its retries cannot crowd out every other target
REFUSAL_FINISH = {3: "SAFETY", 4: "RECITATION", 7: "BLOCKLIST", 8: "PROHIBITED_CONTENT", 9: "SPII", 11: "IMAGE_SAFETY"}
BUILD_TIMEOUT, COMPILE_TIMEOUT, RUN_TIMEOUT = 1800, 3600, 900  # run: above run_fuzzer_new's own 600 s cap
LOG_HEAD, LOG_TAIL = 256 * 1024, 64 * 1024
CONTAINER = os.environ.get("DFUZZ_CONTAINER_PREFIX") or "sweep"  # name prefix of every container a run starts

ARTIFACT_LOCK = threading.Lock()  # llm_fuzz_integration keeps the harness language in module globals
log = logging.getLogger("sweep")


def now_iso():
    return datetime.now().isoformat(timespec="seconds")


# ----------------------------------------------------------------------------------------------- targets

@dataclasses.dataclass(frozen=True)
class Target:
    project: str
    level: str
    name: str      # target dir name, e.g. src:common:clib-package.c:184
    setting: str

    @property
    def key(self):
        return f"{self.project}::{self.level}-{self.name}"

    @property
    def dir(self):
        return os.path.join(DATA_DIR, self.project, self.setting, self.level, self.name)

    @property
    def harness(self):
        return os.path.join(DATA_DIR, self.project, "base-env", HARNESS_FILES[self.project])

    @property
    def spec(self):  # (difficulty, target_path, target_line), built as evaluation_project.process_dir does
        return self.level, "/".join(self.name.split(":")[:-1]), int(self.name.split(":")[-1])


def load_arvo_entries(path=None):
    """cve_id -> entry of an ARVO-style benchmark json: T6 (static/data/target-arvo/benchmark.json) unless
    `path` names another one, such as the knowledge-cutoff study set static/data/post-cutoff/benchmark.json."""
    with open(path or ARVO_BENCHMARK) as f:
        return {e["cve_id"]: e for e in json.load(f)}


def use_benchmark(path):
    """Evaluate the cases of `path`; their context dirs are <project>/<retrieval>/<id>/ next to it."""
    global ARVO_BENCHMARK, ARVO_ROOT, ARVO_ENTRIES
    ARVO_BENCHMARK = os.path.abspath(path)
    ARVO_ROOT = os.path.dirname(ARVO_BENCHMARK)
    ARVO_ENTRIES = load_arvo_entries(ARVO_BENCHMARK)


ARVO_BENCHMARK = ARVO_ROOT = None
ARVO_ENTRIES = {}
use_benchmark(T6_BENCHMARK)
# CVEs whose own PoC reaches the target in the eval image but cannot reproduce the crash there, so no input can:
# their reproduction is not measurable, and reproduce rates are taken over the other CVEs (reach counts all).
# Found by running every CVE's PoC through its eval image.
POC_UNREPRODUCIBLE = {
    "42492610": "the HIT TARGET print before AK/Format.h:227 keeps UBSan's object-size check there from firing; "
                "the unpatched ARVO image reports it",
}


@dataclasses.dataclass(frozen=True)
class ArvoTarget:
    """A case of the ARVO-style benchmark in use (a T6 CVE by default): evaluation_arvo's prompt, input and
    container check."""
    project: str
    name: str  # the ARVO / OSS-Fuzz issue id (gh-<fix commit> for the cases built from fix commits)
    level = "T6"

    @property
    def key(self):
        return f"{self.project}::T6-{self.name}"

    @property
    def dir(self):  # the context dir evaluate_cve reads (also required by its no-context prompts)
        return os.path.join(ARVO_ROOT, self.project, "realistic", self.name)

    @property
    def entry(self):
        return ARVO_ENTRIES[self.name]


def arvo_spec(project, cve):
    """(target path, line, function, sanitizer pattern) of an ARVO-style case, as evaluation_arvo reads it."""
    return ea.arvo_target(ARVO_ENTRIES[cve])


def list_targets(setting, projects=None, names=None):
    """All targets, ordered so that any prefix is balanced across projects and tiers."""
    tier6, _, setting = SETTINGS[setting]
    if tier6:
        by_project = collections.defaultdict(list)
        for cve, e in sorted(ARVO_ENTRIES.items()):
            if (not projects or e["project_name"] in projects) and (
                    not names or cve in names or f"{e['project_name']}::{cve}" in names):
                by_project[e["project_name"]].append(ArvoTarget(e["project_name"], cve))
        return interleave([by_project[p] for p in sorted(by_project)])
    per_project = []
    for project in sorted(HARNESS_FILES):
        if projects and project not in projects:
            continue
        by_tier = []
        for level in TIERS:
            level_dir = os.path.join(DATA_DIR, project, setting, level)
            if os.path.isdir(level_dir):
                by_tier.append([Target(project, level, n, setting) for n in sorted(os.listdir(level_dir))
                                if not names or n in names or f"{project}::{n}" in names])
        per_project.append(interleave(by_tier))
    return interleave(per_project)


def interleave(lists):
    out = []
    for i in range(max(map(len, lists), default=0)):
        out += [lst[i] for lst in lists if i < len(lst)]
    return out


def artifact_args(t, testcase, no_context=None):
    return argparse.Namespace(debug=False, testcase=testcase, harness=t.harness, llm_model="gemini-sweep",
                              no_context=no_context, random=False, vibe=False)


class Prompts:
    """The artifact's prompt for each target, built once: every round and model sends the same text. T1-T5
    prompts are read from the copy of the target dir that preprocess_dir makes under `out_dir` (<run>/prompts)."""

    def __init__(self, setting="realistic", out_dir=None):
        self.cache, self.lock = {}, threading.Lock()
        self.tier6, self.no_context, _ = SETTINGS[setting]
        self.out_dir = out_dir

    def get(self, t):
        with self.lock:
            if t.key in self.cache:
                return self.cache[t.key]
        if self.tier6:
            prompt = ea.build_arvo_prompt(t.entry, t.dir, no_context=self.no_context)
        else:
            with ARTIFACT_LOCK:
                ep.EVAL_TIME = "prompts"
                args = argparse.Namespace(dir=t.dir, project_dir=os.path.join(DATA_DIR, t.project, t.setting),
                                          llm_model="gemini-sweep", no_context=self.no_context, out_dir=self.out_dir)
                ep.preprocess_dir(args)  # -> <out_dir>/<project>/exec-env-<label>-gemini-sweep-prompts/<level>-<target>
                iargs = artifact_args(t, testcase=args.dir, no_context=self.no_context)
                lfi.sync_globals(iargs)
                prompt = (lfi.build_no_context_prompt(iargs, t.spec) if self.no_context else
                          lfi.build_prompt(iargs, t.spec))
        entry = (prompt, hashlib.sha256(prompt.encode()).hexdigest())
        with self.lock:
            self.cache[t.key] = entry
        return entry


def instrument(t, data, results_dir):
    """Instrument the artifact's harness with `data`. Returns (error_field | None, sha256, stored path)."""
    tmp = tempfile.mkdtemp(prefix="instr-", dir=results_dir)
    try:
        with ARTIFACT_LOCK:
            iargs = artifact_args(t, testcase=tmp)
            lfi.sync_globals(iargs)
            code = lfi.instrument_fuzzing_engine(data, iargs)
            out = lfi.instrumented_harness_path(t.harness, iargs)
        if code != lfi.ErrorCode.SUCCESS:
            return "incompatible_data_err", None, None
        with open(out, "rb") as f:
            sha = hashlib.sha256(f.read()).hexdigest()
        dest_dir = os.path.join(results_dir, "harnesses", t.project, f"{t.level}-{t.name}")
        os.makedirs(dest_dir, exist_ok=True)
        dest = os.path.join(dest_dir, sha[:16] + os.path.splitext(out)[1])
        if not os.path.exists(dest):
            shutil.copy(out, dest)
        return None, sha, dest
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def arvo_input(data):
    """The input evaluation_arvo.evaluate_cve writes for a parsed response, byte for byte."""
    input_str = "\n\n".join(str(item) for item in data if item)
    try:
        return input_str.encode('utf-8').decode('unicode_escape').encode('latin-1')
    except Exception:
        return input_str.encode('utf-8')


def store_arvo_input(t, data, results_dir):
    """-> (sha256, stored path) of the T6 input for `data` (kept for the PoC-overlap analysis)."""
    blob = arvo_input(data)
    sha = hashlib.sha256(blob).hexdigest()
    dest_dir = os.path.join(results_dir, "inputs", t.project, t.name)
    os.makedirs(dest_dir, exist_ok=True)
    dest = os.path.join(dest_dir, sha[:16] + ".bin")
    if not os.path.exists(dest):
        with open(dest, "wb") as f:
            f.write(blob)
    return sha, dest


# ----------------------------------------------------------------------------------------------- storage

class Store:
    """Append-only JSONL files; together they are the whole resumable state of a run."""

    def __init__(self, root):
        self.root = root
        os.makedirs(root, exist_ok=True)
        self.locks = collections.defaultdict(threading.Lock)

    def path(self, name):
        return os.path.join(self.root, name)

    def append(self, name, record):
        line = json.dumps(record, ensure_ascii=False)
        with self.locks[name]:
            with open(self.path(name), "a") as f:
                f.write(line + "\n")

    def load(self, name):
        records = []
        if os.path.exists(self.path(name)):
            with open(self.path(name)) as f:
                for line in f:
                    try:
                        records.append(json.loads(line))
                    except json.JSONDecodeError:
                        pass  # a line cut short by a crash; that round is simply redone
        return records


# ----------------------------------------------------------------------------------------------- API

def classify(e):
    """429 | 5xx | timeout | network | permanent | other"""
    from google.api_core import exceptions as gexc
    if GENAI_CLIENT is not None:  # --include-thoughts: google-genai raises its own errors, on httpx
        import httpx
        from google.genai import errors as nerr
        if isinstance(e, nerr.APIError):
            e = gexc.from_http_status(e.code or 0, str(e))  # same code, classified below
        elif isinstance(e, httpx.TimeoutException):
            return "timeout"
        elif isinstance(e, httpx.TransportError):
            return "network"
    if isinstance(e, gexc.GoogleAPICallError):
        code = getattr(e, "code", None)
        if code == 429:
            return "429"
        if code in (500, 502, 503):
            return "5xx"
        if code == 504:
            return "timeout"
        if code in (400, 401, 403, 404):
            return "permanent"
        return "other"
    if isinstance(e, (gexc.RetryError, TimeoutError)):
        return "timeout"
    if isinstance(e, (ConnectionError, OSError)):
        return "network"
    return "other"


def retry_delay_of(e):
    for detail in getattr(e, "details", None) or []:
        delay = getattr(detail, "retry_delay", None)
        if delay is not None and hasattr(delay, "seconds"):
            return delay.seconds + delay.nanos / 1e9
    m = re.search(r"retry(?:_delay|Delay)?[^0-9]{0,20}([0-9]+(?:\.[0-9]+)?)\s*s", str(e), re.I)
    return float(m.group(1)) if m else None


def interpret(response):
    """The artifact's checks (make_API_request + process_response). Returns (error_field | None, text, data)."""
    if not response.candidates:
        return "other_err", None, None
    candidate = response.candidates[0]
    if candidate.finish_reason == 4:  # RECITATION
        return "copyright_err", None, None
    if not candidate.content or not candidate.content.parts:
        return "other_err", None, None
    try:
        text = response.text
    except ValueError:
        return "other_err", None, None
    data, code = lfi.process_response(text)
    if data is None or code != lfi.ErrorCode.SUCCESS:
        return "pattern_unmatch_err", text, None
    return None, text, data


def refusal_of(response):
    """Why the API withheld the answer -- a blocked prompt or a filtered candidate -- else None."""
    if not response.candidates:
        reason = getattr(getattr(response, "prompt_feedback", None), "block_reason", None)
        return f"prompt_block:{int(reason)}" if reason else None
    finish = response.candidates[0].finish_reason
    finish = int(finish) if finish is not None else None
    return f"finish:{REFUSAL_FINISH[finish]}" if finish in REFUSAL_FINISH else None


def block_reason_of(response):
    """prompt_feedback.block_reason of a response without candidates (2 = OTHER), else None."""
    if response.candidates:
        return None
    reason = getattr(getattr(response, "prompt_feedback", None), "block_reason", None)
    return int(reason) if reason else None


def usage_of(response):
    u = getattr(response, "usage_metadata", None)

    def get(field):
        return int(getattr(u, field, 0) or 0) if u is not None else 0
    prompt, total = get("prompt_token_count"), get("total_token_count")
    return {"prompt": prompt, "cached": get("cached_content_token_count"),
            "candidates": get("candidates_token_count"), "total": total,
            "output": max(total - prompt, get("candidates_token_count"))}  # output incl. thinking


def cost_of(model, usage):
    p_in, p_cached, p_out = PRICES.get(model, (0, 0, 0))
    return ((usage["prompt"] - usage["cached"]) * p_in + usage["cached"] * p_cached + usage["output"] * p_out) / 1e6


class FakeResponse:
    """--fake-response: exercises the whole pipeline without calling the API."""

    class _Part:
        pass

    def __init__(self, text, refused=False):
        candidate = argparse.Namespace(finish_reason=1, content=argparse.Namespace(parts=[self._Part()]))
        self.candidates, self.text, self.usage_metadata = ([] if refused else [candidate]), text, None
        self.prompt_feedback = argparse.Namespace(block_reason=2 if refused else 0)


GENAI_CLIENT = None  # --include-thoughts: a google-genai client (google-generativeai cannot return thoughts)


class ThoughtsResponse:
    """--include-thoughts: a google-genai response in the shape of the google-generativeai response the rest of
    the driver reads (candidates[0].finish_reason / content.parts, .text, prompt_feedback, usage_metadata). The
    thought summaries are kept apart in .thoughts and never reach .text, so the answer is parsed as before."""

    def __init__(self, r):
        from google.ai.generativelanguage_v1beta.types import Candidate, GenerateContentResponse

        def legacy(enum, value, fallback):
            if value is None:
                return None
            name = getattr(value, "name", str(value))
            return enum[name] if name in enum.__members__ else fallback

        self.usage_metadata = r.usage_metadata
        block = getattr(getattr(r, "prompt_feedback", None), "block_reason", None)
        BlockReason = GenerateContentResponse.PromptFeedback.BlockReason
        self.prompt_feedback = argparse.Namespace(block_reason=legacy(BlockReason, block, BlockReason.OTHER) or 0)
        self.candidates, self.thoughts, self._answer = [], None, []
        if r.candidates:
            c = r.candidates[0]
            parts = (c.content.parts if c.content else None) or []
            self.thoughts = "\n\n".join(p.text for p in parts if p.thought and p.text) or None
            self._answer = [p for p in parts if not p.thought]
            finish = legacy(Candidate.FinishReason, c.finish_reason, Candidate.FinishReason.OTHER)
            self.candidates = [argparse.Namespace(finish_reason=finish, content=argparse.Namespace(parts=self._answer))]

    @property
    def text(self):
        texts = [p.text for p in self._answer if p.text is not None]
        if not texts:
            raise ValueError("no text part")  # as google-generativeai's .text
        return "".join(texts)


QUOTA_DIR = os.path.join(RESULTS_ROOT, "quota")


class QuotaLedger:
    """A model's API usage over the last 60 s, shared by every process that calls the API with this key -- the
    sweep, the queued jobs -- through a file under --quota-dir guarded by flock: the quota belongs to
    the project and model, so limiters kept per process would each believe they had all of it.

    Tokens are booked the way the quota meets them: a request's prompt tokens when it is sent (exact: prompts
    are fixed, and their token counts come from earlier responses), its output tokens (thinking included) when
    it comes back. A request that fails (429, 5xx, timeout) keeps its booked prompt tokens: it reached the
    quota too. A request is sent only when
      - the tokens of the last 60 s, plus its prompt and expected output, stay under target x TPM,
      - the requests of the last 60 s stay under target x RPM, and
      - at least 60 s x (prompt + expected output) / (target x TPM) have passed since the previous send, so the
        tokens arrive evenly instead of in bursts at the start of each minute.
    A 429, or a 503 ("this model is currently experiencing high demand"), pauses sending for the model in every
    process: 30 s, doubling to 5 min while they continue, back to 30 s after the next answer. (Left to itself,
    the client library resends a 503 every few seconds for up to 10 minutes, each time with the whole prompt;
    such requests, unseen by the ledger, push the quota's RPM and TPM over.) The target drops by 0.05 (down
    to 0.5) only when a 429 comes while the ledger shows the quota at least 70% used -- Google then counts more
    than we do; a 429 at lower usage, or a 503, is Google's own throttling, which a lower target would not
    help. It climbs back after 30 minutes without a 429."""

    WINDOW = 60.0

    def __init__(self, model, rpm, tpm, target, directory):
        self.model, self.rpm, self.tpm, self.max_target = model, rpm, tpm, target
        os.makedirs(directory, exist_ok=True)
        self.path = os.path.join(directory, f"{model}.json")
        self.lock_path = os.path.join(directory, f"{model}.lock")
        self.thread_lock = threading.Lock()
        self.ids = itertools.count()

    def _load(self, now):
        try:
            with open(self.path) as f:
                state = json.load(f)
        except (OSError, ValueError):
            state = {}
        for key, default in (("events", []), ("pause_until", 0.0), ("backoff", 0.0), ("last_send", 0.0),
                             ("target", self.max_target), ("last_429", 0.0), ("last_cut", 0.0), ("last_raise", 0.0)):
            state.setdefault(key, default)
        state["events"] = [e for e in state["events"] if now - e[0] < 2 * self.WINDOW]  # [t, tokens, request?, id]
        return state

    def _update(self, change):
        """Apply change(state, now) under the lock, save, and return its result."""
        with self.thread_lock, open(self.lock_path, "a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            try:
                now = time.time()
                state = self._load(now)
                result = change(state, now)
                tmp = f"{self.path}.{os.getpid()}.tmp"
                with open(tmp, "w") as f:
                    json.dump(state, f)
                os.replace(tmp, self.path)
                return result
            finally:
                fcntl.flock(lock, fcntl.LOCK_UN)

    def _window(self, state, now):
        recent = [e for e in state["events"] if now - e[0] < self.WINDOW]
        return sum(e[2] for e in recent), sum(e[1] for e in recent)

    def acquire(self, prompt_tokens, output_estimate):
        """Book a request about to be sent, or None if it must wait."""
        need = prompt_tokens + output_estimate

        def change(state, now):
            if now < state["pause_until"]:
                return None
            requests, tokens = self._window(state, now)
            budget = self.tpm * state["target"]
            if requests + 1 > self.rpm * state["target"]:
                return None
            if tokens and tokens + need > budget:  # a request bigger than the budget still goes into an empty minute
                return None
            if now - state["last_send"] < self.WINDOW * min(need, budget) / budget:
                return None
            rid = f"{os.getpid()}-{next(self.ids)}"
            state["events"].append([now, prompt_tokens, 1, rid])
            state["last_send"] = now
            return {"id": rid, "sent": now, "booked": prompt_tokens, "window_requests": requests,
                    "window_tokens": tokens, "target": state["target"]}
        return self._update(change)

    def settle(self, entry, prompt_tokens, output_tokens):
        """An answer came back: book its output now, and correct the prompt tokens if they were estimated."""
        def change(state, now):
            extra = output_tokens
            if prompt_tokens and prompt_tokens != entry["booked"]:
                for e in state["events"]:
                    if e[3] == entry["id"] and now - e[0] < self.WINDOW:
                        e[1] = prompt_tokens
                        break
                else:
                    extra += max(0, prompt_tokens - entry["booked"])
            if extra:
                state["events"].append([now, extra, 0, entry["id"]])
            state["backoff"] = 0.0
        self._update(change)

    def back_off(self, delay, quota):
        """Pause the model after a 429 (quota=True) or a 503; returns the requests and tokens of the last
        60 s, for the log."""
        def change(state, now):
            requests, tokens = self._window(state, now)
            state["backoff"] = min(300.0, max(30.0, state["backoff"] * 2))
            state["pause_until"] = max(state["pause_until"], now + max(delay or 0, state["backoff"]))
            if not quota:
                return requests, tokens
            state["last_429"] = now
            if tokens >= 0.7 * self.tpm and now - state["last_cut"] >= 300:
                state["target"] = max(0.5, state["target"] - 0.05)
                state["last_cut"] = now
            return requests, tokens
        return self._update(change)

    def recover(self):  # once a minute
        def change(state, now):
            if (state["target"] < self.max_target and now - state["last_429"] > 1800
                    and now - state["last_raise"] > 1800):
                state["target"] = min(self.max_target, state["target"] + 0.05)
                state["last_raise"] = now
        self._update(change)

    def usage(self):
        """(requests, tokens) of the last 60 s over all processes, the target, and seconds of pause left."""
        def change(state, now):
            requests, tokens = self._window(state, now)
            return requests, tokens, state["target"], max(0.0, state["pause_until"] - now)
        return self._update(change)


@dataclasses.dataclass(eq=False)
class RoundTask:
    target: Target
    round: int
    attempts: int = 0      # transient failures in the current pass
    passes: int = 1        # 2 once deferred to the end
    deferred: bool = False
    not_before: float = 0.0
    inflight: bool = False
    first_dispatch: float = 0.0
    refusals: int = 0      # times the API withheld the answer for this round
    tries: int = 0         # responses to this round so far, answered or not (--retry-unanswered)
    done: bool = False
    given_up: bool = False


class ModelStats:
    def __init__(self):
        self.lock = threading.Lock()
        self.completions = collections.deque()  # (t, prompt, cached, output)
        self.errors = collections.deque()       # (t, kind)
        self.error_total = collections.Counter()
        self.model_errors = collections.Counter()
        self.done = self.blocked = self.refused_total = self.prompt_total = self.cached_total = self.output_total = 0
        self.unanswered_total = 0
        self.output_ema = 5000.0
        self.spend = 0.0

    def completed(self, usage, cost, error, live=True, blocked=False):
        with self.lock:
            self.done += 1
            self.blocked += blocked
            self.spend += cost
            self.prompt_total += usage["prompt"]
            self.cached_total += usage["cached"]
            self.output_total += usage["output"]
            self.output_ema = 0.8 * self.output_ema + 0.2 * usage["output"]
            if error:
                self.model_errors[error] += 1
            if live:
                self.completions.append((time.time(), usage["prompt"], usage["cached"], usage["output"]))

    def output_estimate(self):
        """Recent output tokens per call (EMA; follows the jump in thinking on harder targets)."""
        with self.lock:
            return int(self.output_ema)

    def retried(self, usage, cost, error, live=True):
        """A response without an input under --retry-unanswered: billed and counted, but not a round."""
        with self.lock:
            self.spend += cost
            self.prompt_total += usage["prompt"]
            self.cached_total += usage["cached"]
            self.output_total += usage["output"]
            self.output_ema = 0.8 * self.output_ema + 0.2 * usage["output"]
            self.unanswered_total += 1
            self.model_errors[error] += 1
            if live:
                self.completions.append((time.time(), usage["prompt"], usage["cached"], usage["output"]))

    def refused(self, usage, cost, live=True):
        with self.lock:
            self.spend += cost
            self.prompt_total += usage["prompt"]
            self.cached_total += usage["cached"]
            self.output_total += usage["output"]
            self.output_ema = 0.8 * self.output_ema + 0.2 * usage["output"]
            self.refused_total += 1
            if live:
                self.completions.append((time.time(), usage["prompt"], usage["cached"], usage["output"]))

    def error(self, kind):
        with self.lock:
            self.errors.append((time.time(), kind))
            self.error_total[kind] += 1

    def snapshot(self):
        with self.lock:
            now = time.time()
            while self.completions and now - self.completions[0][0] >= 60:
                self.completions.popleft()
            while self.errors and now - self.errors[0][0] >= 60:
                self.errors.popleft()
            last = list(self.completions)
            return {
                "completed_1m": len(last),
                "cached_1m_k": round(sum(c[2] for c in last) / 1e3, 1),
                "prompt_done_1m_k": round(sum(c[1] for c in last) / 1e3, 1),
                "output_1m_k": round(sum(c[3] for c in last) / 1e3, 1),
                "errors_1m": dict(collections.Counter(k for _, k in self.errors)),
                "errors_total": dict(self.error_total),
                "model_errors": dict(self.model_errors),
                "blocked": self.blocked,
                "refused_total": self.refused_total,
                "unanswered_retried": self.unanswered_total,
                "done": self.done,
                "spend_usd": round(self.spend, 2),
                "cache_hit_pct": round(100 * self.cached_total / self.prompt_total, 1) if self.prompt_total else None,
                "avg_output_tokens": round(self.output_total / (self.done + self.refused_total))
                if self.done + self.refused_total else None,
            }


class Generator:
    def __init__(self, model, targets, rounds, done, ctx):
        self.model, self.ctx = model, ctx
        self.tasks = [RoundTask(t, r) for t in targets for r in range(1, rounds + 1)]
        self.first = {}
        for task in self.tasks:
            task.done = (model, task.target.key, task.round) in done
            if task.round == 1:
                self.first[task.target.key] = task
        a = ctx.args
        self.limiter = QuotaLedger(model, a.rpm, a.tpm, a.limit_factor, a.quota_dir)
        # After a restart the previous process's last requests are still coming back: let them land first.
        self.start_after = time.time() + a.startup_delay
        self.outputs = collections.defaultdict(list)  # target key -> output tokens of its responses so far
        self.pool = ThreadPoolExecutor(max_workers=a.max_inflight, thread_name_prefix=f"api-{model}")
        self.refusals = collections.Counter()  # target key -> refused responses
        self.answers = collections.Counter()   # target key -> recorded rounds
        self.requests = collections.Counter()  # target key -> responses of any kind (--max-requests-per-target)
        self.inflight_by_target = collections.Counter()
        self.cursor = 0           # tasks before this index are all done
        self.inflight = 0
        self.permanent_errors = 0
        self.stopped = None
        self.lock = threading.Lock()
        self.stats = ModelStats()

    def remaining(self):
        return sum(1 for t in self.tasks if not (t.done or t.given_up))

    def finished(self):
        return self.stopped is not None or (self.remaining() == 0 and self.inflight == 0)

    def released(self, task, now):
        """Rounds 2.. of a target wait for round 1 (or 90 s), so they find its prompt in the implicit cache."""
        if task.round == 1:
            return True
        first = self.first[task.target.key]
        return first.done or first.given_up or (first.first_dispatch and now - first.first_dispatch > self.ctx.args.release_after)

    def output_estimate(self, key):
        """Expected output tokens (thinking included) of a request for this target: the mean of its earlier
        responses, else the model's recent mean."""
        seen = self.outputs.get(key)
        return int(sum(seen) / len(seen)) if seen else self.stats.output_estimate()

    def throttled(self, key):
        return (self.refusals[key] >= 10 and self.refusals[key] >= self.answers[key]
                and self.inflight_by_target[key] >= REFUSED_TARGET_INFLIGHT)

    def capped(self, key):
        """--max-requests-per-target counts the requests still in flight too, so the cap is never overshot."""
        cap = self.ctx.args.max_requests_per_target
        return bool(cap) and self.requests[key] + self.inflight_by_target[key] >= cap

    def next_task(self, now):
        while self.cursor < len(self.tasks) and (self.tasks[self.cursor].done or self.tasks[self.cursor].given_up):
            self.cursor += 1  # rounds finish roughly in order, so the scan below stays short
        normal_pending = False
        for task in self.tasks[self.cursor:]:
            if task.done or task.given_up or task.inflight or task.deferred:
                continue
            normal_pending = True
            if (task.not_before <= now and self.released(task, now) and not self.throttled(task.target.key)
                    and not self.capped(task.target.key)):
                return task
        if normal_pending:
            return None
        for task in self.tasks:  # only deferred rounds are left: give them a second pass
            if (task.deferred and not (task.done or task.given_up or task.inflight) and task.not_before <= now
                    and not self.throttled(task.target.key) and not self.capped(task.target.key)):
                return task
        return None

    def run(self):
        while not self.ctx.stop.is_set() and not self.finished():
            if self.ctx.draining.is_set() or time.time() < self.start_after:
                time.sleep(0.5)
                continue
            with self.lock:
                task = self.next_task(time.time()) if self.inflight < self.ctx.args.max_inflight else None
            if task is None:
                time.sleep(0.5)
                continue
            prompt, _ = self.ctx.prompts.get(task.target)
            entry = self.limiter.acquire(self.ctx.token_estimate(task.target, prompt),
                                         self.output_estimate(task.target.key))
            if entry is None:
                time.sleep(0.25)
                continue
            with self.lock:
                task.inflight = True
                task.first_dispatch = task.first_dispatch or time.time()
                self.inflight += 1
                self.inflight_by_target[task.target.key] += 1
            self.pool.submit(self.execute, task, entry)
        self.pool.shutdown(wait=False, cancel_futures=True)

    def execute(self, task, entry):
        t = task.target
        recorded = False
        try:
            prompt, prompt_sha = self.ctx.prompts.get(t)
            t0 = time.time()
            try:
                if self.ctx.args.random_input:
                    # the artifact's random baseline (evaluation_project.py --random): a 10-character alphanumeric
                    # string per round, sent through the same response parser and input channel as a model answer
                    import random
                    import string
                    response = FakeResponse("```\n" + "".join(random.choices(string.ascii_letters + string.digits, k=10))
                                            + "\n```")
                elif self.ctx.args.fake_response is not None:
                    import random
                    unanswered = random.random() < self.ctx.args.fake_unanswered_rate
                    time.sleep(self.ctx.args.fake_latency)
                    response = FakeResponse("Sorry, I cannot help with that." if unanswered else self.ctx.args.fake_response,
                                            refused=random.random() < self.ctx.args.fake_refusal_rate)
                elif GENAI_CLIENT is not None:
                    from google.genai import types
                    # the same prompt and default generation settings; only the thought summaries come back too
                    # (the client is built without retry_options: it never resends a request by itself)
                    response = ThoughtsResponse(GENAI_CLIENT.models.generate_content(
                        model=self.model, contents=prompt,
                        config=types.GenerateContentConfig(thinking_config=types.ThinkingConfig(include_thoughts=True))))
                else:
                    import google.generativeai as genai
                    # retry=None: the client library would otherwise resend a 503 by itself for up to 10 min,
                    # requests that neither the ledger nor the logs would see; our own retry books every one.
                    response = genai.GenerativeModel(self.model).generate_content(
                        prompt, request_options={"timeout": self.ctx.args.api_timeout, "retry": None})
            except Exception as e:  # noqa: BLE001 - every API failure is classified below
                self.on_api_error(task, e, elapsed=time.time() - t0, entry=entry)
                return
            latency = round(time.time() - t0, 2)
            usage = usage_of(response)
            self.limiter.settle(entry, usage["prompt"], usage["output"])
            if usage["output"]:
                self.outputs[t.key].append(usage["output"])
            if usage["prompt"]:
                self.ctx.learn_tokens(t, prompt, usage["prompt"])
            refusal = refusal_of(response)
            if refusal is not None:
                self.on_refusal(task, refusal, usage)
                return
            error, text, data = interpret(response)
            if self.ctx.args.random_input and data:
                # the artifact's --random passes [string] straight to the harness, without the "" that
                # process_response inserts (which would add a newline to a T1-T5 input; T6 drops empty items anyway)
                data = [s for s in data if s]
            blocked = block_reason_of(response)
            sha = harness_path = None
            if error is None and isinstance(t, ArvoTarget):
                sha, harness_path = store_arvo_input(t, data, self.ctx.store.root)
            elif error is None:
                error, sha, harness_path = instrument(t, data, self.ctx.store.root)
            candidate = response.candidates[0] if response.candidates else None
            finish = getattr(getattr(candidate, "finish_reason", None), "name", str(getattr(candidate, "finish_reason", None)))
            thoughts = getattr(response, "thoughts", None)  # --include-thoughts only
            if self.ctx.args.retry_unanswered and error in RETRY_ERRORS:
                self.on_unanswered(task, error, text, usage, latency, finish, blocked, prompt_sha, thoughts)
                recorded = True
                return
            record = {
                "ts": now_iso(), "model": self.model, "target_key": t.key, "project": t.project, "level": t.level,
                "target": t.name, "round": task.round, "status": "model_error" if error else "input", "error": error,
                "harness_sha": sha, "harness_path": harness_path, "latency_s": latency, "usage": usage,
                "finish_reason": finish, "block_reason": blocked, "refusals_before": task.refusals,
                "transient_attempts": task.attempts,
                "prompt_sha": prompt_sha,
                "response_text": text,
                "attempt": task.tries + 1,
            }
            if GENAI_CLIENT is not None:
                record["thoughts"] = thoughts
            self.ctx.store.append("responses.jsonl", record)
            recorded = True
            with self.lock:
                task.done = True
                task.tries += 1
                self.answers[t.key] += 1
                self.requests[t.key] += 1
                self.check_request_cap(t.key)
            self.stats.completed(usage, cost_of(self.model, usage), error, blocked=blocked is not None)
            if error:
                self.ctx.resolve(self.model, False)
            else:
                self.ctx.verifier.submit(t, sha, harness_path, self.model, task.round)
        except Exception:  # a bug in this driver: keep going, but make it visible
            self.ctx.alert(f"[{self.model}] driver error on {t.key} r{task.round}:\n{traceback.format_exc()}")
            if not recorded:
                self.on_api_error(task, RuntimeError("driver error"), count_only=True)
        finally:
            with self.lock:
                task.inflight = False
                self.inflight -= 1
                self.inflight_by_target[t.key] -= 1

    def on_unanswered(self, task, error, text, usage, latency, finish, blocked, prompt_sha, thoughts=None):
        """--retry-unanswered: the response has no input to run. It goes to attempts.jsonl and the round is
        asked again, unless the target has used up --max-requests-per-target."""
        t = task.target
        record = {
            "ts": now_iso(), "model": self.model, "target_key": t.key, "project": t.project, "level": t.level,
            "target": t.name, "round": task.round, "attempt": task.tries + 1, "status": "model_error",
            "error": error, "latency_s": latency, "usage": usage, "finish_reason": finish, "block_reason": blocked,
            "prompt_sha": prompt_sha, "response_text": text}
        if GENAI_CLIENT is not None:
            record["thoughts"] = thoughts
        self.ctx.store.append("attempts.jsonl", record)
        self.stats.retried(usage, cost_of(self.model, usage), error)
        with self.lock:
            task.tries += 1
            self.requests[t.key] += 1
            task.not_before = time.time() + 2
            self.check_request_cap(t.key)

    def check_request_cap(self, key):
        """Under self.lock: give up a target's open rounds once it has had --max-requests-per-target responses."""
        cap = self.ctx.args.max_requests_per_target
        if not cap or self.requests[key] < cap:
            return
        left = [x for x in self.tasks if x.target.key == key and not (x.done or x.given_up)]
        for x in left:
            x.given_up = True
        if left:
            self.ctx.alert(f"[{self.model}] GAVE UP on {key}: {self.requests[key]} requests, {self.answers[key]} "
                           f"rounds answered; {len(left)} rounds left unanswered")

    def on_refusal(self, task, reason, usage):
        """The API withheld the answer: not a result, so the round is asked again."""
        t = task.target
        self.stats.refused(usage, cost_of(self.model, usage))
        self.stats.error("blocked")
        self.ctx.store.append("api_events.jsonl", {
            "ts": now_iso(), "model": self.model, "target_key": t.key, "round": task.round, "kind": "blocked",
            "type": reason, "code": None, "message": "", "usage": usage})
        with self.lock:
            task.refusals += 1
            self.refusals[t.key] += 1
            self.requests[t.key] += 1
            task.not_before = time.time() + 2
            self.check_request_cap(t.key)
            n, answered = self.refusals[t.key], self.answers[t.key]
            stop = None
            if n >= ALWAYS_BLOCKED_AFTER and answered == 0:
                stop = f"refused {n} times without a single answer"
            elif n >= MAX_REFUSALS_PER_TARGET:
                stop = f"refused {n} times ({answered} rounds answered)"
            left = [x for x in self.tasks if x.target.key == t.key and not (x.done or x.given_up)] if stop else []
            for x in left:
                x.given_up = True
            if left:  # requests still in flight when the target was given up come back refused too: stay quiet
                self.ctx.alert(f"[{self.model}] GAVE UP on {t.key}: {stop}; {len(left)} rounds left unanswered")

    def on_api_error(self, task, e, count_only=False, elapsed=None, entry=None):
        kind = "other" if count_only else classify(e)
        self.stats.error(kind)
        delay = retry_delay_of(e) if kind == "429" else None
        overloaded = kind == "5xx" and getattr(e, "code", None) == 503
        at_error = self.limiter.back_off(delay, quota=kind == "429") if kind == "429" or overloaded else None
        if not count_only:
            event = {
                "ts": now_iso(), "model": self.model, "target_key": task.target.key, "round": task.round,
                "kind": kind, "type": type(e).__name__, "code": getattr(e, "code", None), "message": str(e)[:500],
                "elapsed_s": round(elapsed, 1) if elapsed is not None else None}
            if entry is not None:  # the quota's use when the request went out, and when the 429/503 came
                event["ledger_at_send"] = [entry["window_requests"], entry["window_tokens"]]
            if at_error is not None:
                event["ledger_at_error"] = list(at_error)
            self.ctx.store.append("api_events.jsonl", event)
        now = time.time()
        with self.lock:
            if kind == "429":  # quota: retried for as long as it takes
                task.not_before = now + max(delay or 0, 15)
                return
            if kind == "permanent":
                self.permanent_errors += 1
                task.given_up = True
                self.ctx.alert(f"[{self.model}] permanent API error on {task.target.key} r{task.round}: "
                               f"{type(e).__name__}: {str(e)[:300]}")
                if self.permanent_errors >= 5 and self.stats.done == 0:
                    self.stopped = f"{type(e).__name__}: {str(e)[:200]}"
                    self.ctx.alert(f"[{self.model}] STOPPED: every request failed permanently ({self.stopped})")
                return
            task.attempts += 1
            if task.attempts < MAX_TRANSIENT_ATTEMPTS:
                task.not_before = now + min(600, 15 * 2 ** (task.attempts - 1))
            elif task.passes == 1:
                task.deferred, task.passes, task.attempts, task.not_before = True, 2, 0, now
                self.ctx.alert(f"[{self.model}] deferred {task.target.key} r{task.round} after "
                               f"{MAX_TRANSIENT_ATTEMPTS} transient failures (last: {kind} {str(e)[:200]})")
            else:
                task.given_up = True
                self.ctx.alert(f"[{self.model}] GAVE UP on {task.target.key} r{task.round}: transient failures "
                               f"persisted after being deferred (last: {kind} {str(e)[:200]})")


# ----------------------------------------------------------------------------------------------- verification

def sh(cmd, timeout, container=None):
    try:
        return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                              timeout=timeout)
    except subprocess.TimeoutExpired:
        if container:
            subprocess.run(["docker", "rm", "-f", container], capture_output=True)
        return None


def fuzzer_ran(data):
    """Whether libFuzzer got as far as running inputs: it prints its seed before the first one. A log with
    neither that nor HIT TARGET is a harness that never ran -- a dictionary libFuzzer cannot load, a patched
    Python module that no longer imports, a start-up cut off by a timeout -- so it says nothing about the input."""
    return b"HIT TARGET" in data or b"INFO: Seed:" in data


def valid_verdict(v):
    """A verification result to use (resume, reuse, report): ok or compile_fail, but not an 'ok' miss whose log
    shows the fuzzer never ran (possible only in a record without the fuzzer_ran flag, since build_and_run returns
    'ok' only after the fuzzer ran). Those are verified again. T6 verdicts come from evaluation_arvo, which has no
    libFuzzer banner."""
    if v.get("status") not in ("ok", "compile_fail"):
        return False
    if v["status"] == "compile_fail" or v.get("hit") or v.get("fuzzer_ran") or v.get("level") == "T6" \
            or not v.get("log"):
        return True
    ran = _ran_cache.get(v["log"])
    if ran is None:
        try:
            with open(v["log"], "rb") as f:
                ran = fuzzer_ran(f.read(LOG_HEAD))
        except OSError:
            ran = True  # log gone: keep the verdict
        _ran_cache[v["log"]] = ran
    return ran


_ran_cache = {}


def read_log(path):
    """(hit, truncated log bytes). Streams large logs instead of loading them."""
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        if size <= LOG_HEAD + LOG_TAIL:
            data = f.read()
            return b"HIT TARGET" in data, data
        head = f.read(LOG_HEAD)
        hit, carry = b"HIT TARGET" in head, head[-16:]
        while True:
            chunk = f.read(1 << 20)
            if not chunk:
                break
            hit = hit or b"HIT TARGET" in carry + chunk
            carry = chunk[-16:]
        f.seek(size - LOG_TAIL)
        return hit, head + f"\n...[truncated {size - LOG_HEAD - LOG_TAIL} bytes]...\n".encode() + f.read()


SRC = "/src"  # $SRC, and the WORKDIR the oss-fuzz base-builder images start in


def dockerfile_steps(dockerfile):
    """(first line, last line, text) of each instruction, continuation lines joined."""
    lines, steps, i = dockerfile.splitlines(), [], 0
    while i < len(lines):
        if lines[i].strip() and not lines[i].lstrip().startswith("#"):
            first, text = i, lines[i]
            while text.rstrip().endswith("\\") and i + 1 < len(lines):
                i += 1
                text = text.rstrip()[:-1] + " " + lines[i]
            steps.append((first, i, text))
        i += 1
    return steps


def harness_copy_last(dockerfile, harness):
    """The Dockerfile with `harness` taken out of its COPY and copied on its own as the last step.

    Most base-env Dockerfiles COPY the harness together with build.sh and target.patch *before*
    `git fetch` + `git reset --hard`, so every new harness re-fetched the project and rewrote its
    whole checkout -- the bulk of a verification's disk writes. None of those steps reads the harness,
    so copying it last builds the same image, with everything before it cached per target. Anything
    unexpected (another step names the harness, a relative destination) keeps the original order."""
    lines, steps = dockerfile.splitlines(), dockerfile_steps(dockerfile)
    for n, (first, last, text) in enumerate(steps):
        words = text.split()
        if words[0].upper() == "COPY" and harness in words[1:-1]:
            break
    else:
        return dockerfile
    later = steps[n + 1:]
    dest = words[-1].rstrip("/")
    if first != last or not later or dest not in ("$SRC", "${SRC}", "/src") \
            or any(harness in text for _, _, text in later):
        return dockerfile
    rest = [w for w in words[1:-1] if w != harness]
    split = ["COPY " + " ".join(rest) + " " + dest + "/"] if rest else []
    return "\n".join(lines[:first] + split + lines[first + 1:] + [f"COPY {harness} {dest}/"]) + "\n"


def split_harness_copy(dockerfile, harness):
    """(the Dockerfile without its final `COPY <harness> <dest>`, the harness's path in the image), or
    None if that COPY is not the last step."""
    steps = dockerfile_steps(dockerfile)
    first, last, text = steps[-1]
    words = text.split()
    if words[0].upper() != "COPY" or words[1:-1] != [harness]:
        return None
    expand = lambda p: p.replace("${SRC}", SRC).replace("$SRC", SRC)  # noqa: E731
    workdir = SRC
    for _, _, step in steps[:-1]:
        if step.split()[0].upper() == "WORKDIR":
            workdir = os.path.normpath(os.path.join(workdir, expand(step.split()[1])))
    dest = expand(words[-1])
    path = os.path.normpath(os.path.join(workdir, dest + (harness if dest.endswith("/") else "")))
    lines = dockerfile.splitlines()
    return "\n".join(lines[:first] + lines[last + 1:]) + "\n", path


def copy_build_context(t, directory):
    """The build context evaluation_project.preprocess_dir + run_fuzzer assemble, minus the harness."""
    shutil.rmtree(directory, ignore_errors=True)
    os.makedirs(directory)
    base_env = os.path.join(DATA_DIR, t.project, "base-env")
    for f in os.listdir(base_env):
        if "fuzzer_instrumented" not in f:
            shutil.copy(os.path.join(base_env, f), directory)
    for f in os.listdir(t.dir):
        shutil.copy(os.path.join(t.dir, f), directory)
    with open(os.path.join(directory, "Dockerfile")) as f:
        return f.read()


def prepare_verify_dir(t, harness_path, vdir):
    """A build context with the harness, copied last in the Dockerfile (harness_copy_last): one image per
    harness, as the artifact builds it. The sweep itself uses BaseImages; this is for comparing the two."""
    dockerfile = copy_build_context(t, vdir)
    harness = "fuzzer_instrumented" + os.path.splitext(t.harness)[1]
    shutil.copy(harness_path, os.path.join(vdir, harness))
    with open(os.path.join(vdir, "Dockerfile"), "w") as f:
        f.write(harness_copy_last(dockerfile, harness))


class BaseImages:
    """One image per target: its Dockerfile up to, not including, the harness COPY. A verification mounts
    the harness read-only where that COPY puts it and compiles in a container of this image -- the same
    files at the same paths as the artifact's per-harness image, without building, exporting and deleting
    an image per harness, which is slow even when fully cached."""

    def __init__(self, prefix, scratch):
        self.prefix, self.root = prefix, os.path.join(scratch, "base")
        self.lock, self.locks, self.ready = threading.Lock(), {}, {}

    def get(self, t):
        """-> (image, harness path in it), building the image the first time; (None, why) on failure."""
        with self.lock:
            lock = self.locks.setdefault(t.key, threading.Lock())
        with lock:
            if t.key in self.ready:
                return self.ready[t.key]
            harness = "fuzzer_instrumented" + os.path.splitext(t.harness)[1]
            bdir = os.path.join(self.root, t.project, f"{t.level}-{t.name}")
            split = split_harness_copy(harness_copy_last(copy_build_context(t, bdir), harness), harness)
            if split is None:
                return None, "the Dockerfile does not end with the harness COPY"
            dockerfile, path = split
            with open(os.path.join(bdir, "Dockerfile"), "w") as f:
                f.write(dockerfile)
            # shared by every run (a restart or another setting reuses it); cleanup_orphans never removes it
            image = image_name_for(t.project, "vbase-" + hashlib.sha1(t.key.encode()).hexdigest()[:12])
            r = sh(["docker", "build", "-t", image, "--file", os.path.join(bdir, "Dockerfile"), bdir], BUILD_TIMEOUT)
            if r is None or r.returncode != 0:
                return None, "timeout" if r is None else r.stdout[-1500:]
            self.ready[t.key] = image, path
            return self.ready[t.key]


class Janitor:
    """Deletes /out dirs -- root-owned, since containers write them -- through one long-lived container,
    rather than starting a container per verification as docker_utils.cleanup_run does."""

    def __init__(self, prefix, scratch):
        self.name, self.runs = f"{CONTAINER}-{prefix}janitor", os.path.join(scratch, "out", "runs")
        os.makedirs(self.runs, exist_ok=True)
        subprocess.run(["docker", "rm", "-f", self.name], capture_output=True)
        subprocess.run(["docker", "run", "-d", "--pull", "never", "--name", self.name, "-v", f"{self.runs}:/runs",
                        RUNNER_IMAGE, "sleep", "infinity"], capture_output=True)

    def remove(self, project, run_id):
        r = subprocess.run(["docker", "exec", self.name, "rm", "-rf", f"/runs/{project}/{run_id}"],
                           capture_output=True)
        if r.returncode != 0:  # the janitor is gone
            cleanup_run(project, run_id)


def build_and_run(t, harness_file, run_id, runs, stages, bases):
    """docker_utils' build_fuzzer + docker_target_check_new commands on the target's base image with the
    harness mounted (BaseImages), with timeouts, captured output, and `-runs=N` appended to
    run_fuzzer_new. -> (status, hit, detail, log bytes); the seconds each stage took go into `stages`."""
    project = t.project
    language = "python" if t.harness.endswith(".py") else "c"
    out = out_dir_for(project, run_id)
    start = time.time()
    image, where = bases.get(t)
    stages["build"] = round(time.time() - start, 1)
    if image is None:
        return "infra_fail", False, "docker build: " + where, None
    env = ["FUZZING_ENGINE=libfuzzer", "SANITIZER=address", "ARCHITECTURE=x86_64", f"PROJECT_NAME={project}",
           "HELPER=True", f"FUZZING_LANGUAGE={language}"]
    docker_run = ["docker", "run", "--privileged", "--rm", "--shm-size=2g", "--platform", "linux/amd64", "--pull", "never"]
    name = f"{CONTAINER}-{run_id}-compile"
    start = time.time()
    r = sh(docker_run + ["--name", name] + _env_to_docker_args(env) +
           ["-v", f"{out}:/out", "--mount", f"type=bind,source={harness_file},target={where},readonly", image],
           COMPILE_TIMEOUT, name)
    stages["compile"] = round(time.time() - start, 1)
    if r is None:
        return "infra_fail", False, "compile timed out", None
    if r.returncode != 0:
        text = r.stdout[-4000:]
        transient = r.returncode in (125, 137) or any(s in text for s in (
            "Killed", "No space left", "Cannot connect to the Docker daemon", "OCI runtime"))
        return ("infra_fail" if transient else "compile_fail"), False, f"compile rc={r.returncode}: {text[-1500:]}", None
    name = f"{CONTAINER}-{run_id}-run"
    start = time.time()
    r = sh(docker_run + ["--name", name] + _env_to_docker_args(env + ["RUN_FUZZER_MODE=interactive"]) +
           ["-v", f"{out}:/out", RUNNER_IMAGE, "run_fuzzer_new", "fuzzer_instrumented", f"-runs={runs}"],
           RUN_TIMEOUT, name)
    stages["run"] = round(time.time() - start, 1)
    log_path = os.path.join(out, "fuzzer_output.log")
    if not os.path.exists(log_path):
        return "infra_fail", False, "no fuzzer_output.log " + (
            "(timeout)" if r is None else f"(rc={r.returncode}): {r.stdout.strip()[-600:]}"), None
    hit, data = read_log(log_path)
    if not fuzzer_ran(data):
        last = data.strip().splitlines()[-1][-300:].decode("utf-8", "replace") if data.strip() else "(empty log)"
        return "infra_fail", False, "the fuzzer never ran the input: " + last, data
    return "ok", hit, None, data


class Verifier:
    """Runs each unique (target, harness) once; rounds from any model that produce the same harness
    share the result. Picks work round-robin across models so partial results stay balanced."""

    def __init__(self, ctx, models):
        self.ctx, self.order, self.turn = ctx, list(models), 0
        self.cv = threading.Condition()
        self.fifo = {m: collections.deque() for m in models}
        self.info, self.state, self.result = {}, {}, {}
        self.refs = collections.defaultdict(list)  # key -> [(model, round)]
        self.pending = self.active = 0
        self.allowed = ctx.args.verify_workers
        self.counts = collections.Counter()
        self.durations = collections.deque(maxlen=300)
        self.retried_infra = False
        for i in range(ctx.args.max_verify_workers):
            threading.Thread(target=self.worker, name=f"verify-{i}", daemon=True).start()

    def preload(self, record):
        key = (record["target_key"], record["harness_sha"])
        self.state[key], self.result[key] = "done", record
        self.counts[record["status"]] += 1
        self.counts["hit"] += bool(record["hit"])

    def submit(self, t, sha, harness_path, model, rnd):
        key = (t.key, sha)
        with self.cv:
            self.refs[key].append((model, rnd))
            state = self.state.get(key)
            if state == "done" and self.result[key]["status"] != "infra_fail":
                hit = self.result[key]["hit"]
            elif state == "done":  # an infrastructure failure resolves nothing; it is retried at the end
                return
            else:
                if state is None:
                    self.state[key], self.info[key] = "pending", (t, sha, harness_path)
                    self.fifo[model].append(key)
                    self.pending += 1
                    self.cv.notify()
                return
        self.ctx.resolve(model, hit)

    def requeue_infra_failures(self):
        """At the end, verify once more whatever failed for infrastructure reasons."""
        with self.cv:
            for key, record in self.result.items():
                if record["status"] == "infra_fail" and key in self.info:
                    self.state[key] = "pending"
                    self.fifo[self.refs[key][0][0]].append(key)
                    self.pending += 1
                    self.counts["infra_fail"] -= 1
            self.retried_infra = True
            self.cv.notify_all()

    def idle(self):
        with self.cv:
            return self.pending == 0 and self.active == 0

    def _next_key(self):
        for _ in range(len(self.order)):
            q = self.fifo[self.order[self.turn]]
            self.turn = (self.turn + 1) % len(self.order)
            while q:
                key = q.popleft()
                if self.state.get(key) == "pending":
                    return key
        return None

    def worker(self):
        while not self.ctx.stop.is_set():
            with self.cv:
                if self.ctx.draining.is_set() or not (self.active < self.allowed and self.pending > 0):
                    self.cv.wait(5)
                    continue
                key = self._next_key()
                if key is None:
                    log.warning("verifier: pending=%d but no pending key in any queue; resetting", self.pending)
                    self.pending = 0
                    continue
                self.state[key] = "running"
                self.pending -= 1
                self.active += 1
            t0 = time.time()
            try:
                record = self.verify(key)
            except Exception:
                record = {"target_key": key[0], "harness_sha": key[1], "status": "infra_fail", "hit": False,
                          "detail": traceback.format_exc()[-1500:]}
                self.ctx.alert(f"verification error on {key[0]}: {record['detail']}")
            record["duration_s"] = round(time.time() - t0, 1)
            self.ctx.store.append("verifications.jsonl", record)
            with self.cv:
                self.active -= 1
                self.state[key], self.result[key] = "done", record
                self.counts[record["status"]] += 1
                self.counts["hit"] += bool(record["hit"])
                self.durations.append(record["duration_s"])
                refs = list(self.refs[key]) if record["status"] != "infra_fail" else []
                self.cv.notify_all()
            if record["status"] == "infra_fail":
                self.ctx.alert(f"verification infra failure on {key[0]} ({key[1][:12]}): {str(record.get('detail'))[-300:]}")
            for model, _ in refs:
                self.ctx.resolve(model, record["hit"])

    def verify(self, key):
        t, sha, harness_path = self.info[key]
        if isinstance(t, ArvoTarget):
            return self.verify_arvo(t, sha, harness_path)
        prefix = resource_prefix(self.ctx.args.name)
        # no ':' (target names have them; docker's -v splits on it) or ',' (--mount splits on it)
        vdir = os.path.join(self.ctx.args.scratch, "verify", t.project,
                            f"{hashlib.sha1(t.key.encode()).hexdigest()[:12]}--{sha[:12]}")
        record = {"ts": now_iso(), "target_key": t.key, "project": t.project, "level": t.level, "target": t.name,
                  "harness_sha": sha}
        status = None
        for attempt in range(1, 4):
            shutil.rmtree(vdir, ignore_errors=True)
            os.makedirs(vdir)
            harness_file = os.path.join(vdir, "fuzzer_instrumented" + os.path.splitext(t.harness)[1])
            shutil.copy(harness_path, harness_file)
            run_id = prefix + hashlib.sha1(os.path.abspath(vdir).encode()).hexdigest()[:12]
            try:
                previous = status
                stages = {}
                status, hit, detail, data = build_and_run(t, harness_file, run_id, self.ctx.args.runs, stages,
                                                          self.ctx.bases)
            finally:
                self.ctx.janitor.remove(t.project, run_id)
            if status == "ok" or (status == "compile_fail" and previous == "compile_fail"):
                break  # a compile failure seen twice is the input's doing: it counts as a miss
            time.sleep(20 * attempt)
        record.update(status=status, hit=bool(hit), attempts=attempt, detail=detail, stage_s=stages)
        if status == "ok":
            record["fuzzer_ran"] = True  # checked by build_and_run; valid_verdict need not read the log
        if data is not None:
            log_dir = os.path.join(self.ctx.store.root, "logs", t.project, f"{t.level}-{t.name}")
            os.makedirs(log_dir, exist_ok=True)
            record["log"] = os.path.join(log_dir, sha[:16] + ".log")
            with open(record["log"], "wb") as f:
                f.write(data)
        shutil.rmtree(vdir, ignore_errors=True)
        return record

    def verify_arvo(self, t, sha, input_path):
        """evaluation_arvo.run_arvo_container on the stored input -- the artifact's own check: HIT TARGET reaches
        the crash line, HIT TARGET plus the CVE's sanitizer message reproduces it. A container that never ran the
        input (no output, or docker cp failing, which evaluate_cve skips) is an infrastructure failure."""
        with open(input_path, "rb") as f:
            blob = f.read()
        pattern = arvo_spec(t.project, t.name)[3]
        tag = f"{arvo_tag(self.ctx.args.name)}-{sha[:12]}"  # container arvo_eval_<cve>_<tag>_<pid>: unique per input
        record = {"ts": now_iso(), "target_key": t.key, "project": t.project, "level": t.level, "target": t.name,
                  "harness_sha": sha}
        for attempt in range(1, 4):
            start = time.time()
            hit, reproduced, output = ea.run_arvo_container(t.name, blob, pattern, timeout=30, tag=tag)
            stages = {"run": round(time.time() - start, 1)}
            status = "infra_fail" if output == "" or output.startswith("docker cp failed:") else "ok"
            if status == "ok":
                break
            time.sleep(20 * attempt)
        record.update(status=status, hit=bool(hit), reproduced=bool(reproduced), attempts=attempt,
                      detail=None if status == "ok" else (output or "container did not start")[-1500:],
                      stage_s=stages)
        if status == "ok":
            log_dir = os.path.join(self.ctx.store.root, "logs", t.project, f"{t.level}-{t.name}")
            os.makedirs(log_dir, exist_ok=True)
            record["log"] = os.path.join(log_dir, sha[:16] + ".log")
            data = output.encode("utf-8", errors="replace")
            with open(record["log"], "wb") as f:
                f.write(data if len(data) <= LOG_HEAD + LOG_TAIL else data[:LOG_HEAD] + b"\n[...]\n" + data[-LOG_TAIL:])
        return record


def arvo_tag(name):
    """The part of evaluation_arvo's container names (arvo_eval_<cve>_<tag>_<pid>) that marks this run."""
    return "sw" + re.sub(r"[^a-z0-9]+", "", name.lower())


def resource_prefix(name):
    """Prefix of every container, image tag, /out dir and build dir a run creates. The trailing `--` ends
    it, so cleaning up one run never touches another run -- even one whose name starts the same."""
    return "v-" + re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") + "--"


def use_scratch(scratch):
    """Per-verification build dirs and /out dirs go to `scratch` (fast local storage) rather than next to
    the data. docker_utils derives /out dirs from its DFUZZBENCH_DIR, so point that there too."""
    os.makedirs(os.path.join(scratch, "verify"), exist_ok=True)
    docker_utils.DFUZZBENCH_DIR = scratch


def cleanup_orphans(prefix, scratch):
    """Containers, images and dirs left by an interrupted run with this prefix (never other runs')."""
    ids = subprocess.run(["docker", "ps", "-aq", "--filter", f"name={CONTAINER}-{prefix}"],
                         capture_output=True, text=True).stdout.split()
    tag = arvo_tag(prefix[2:-2])  # prefix is v-<slug>--
    arvo_prefix = getattr(ea, "CONTAINER_PREFIX", "arvo_eval")  # evaluation_arvo's containers: <prefix>_<id>_<tag>_<pid>
    names = subprocess.run(["docker", "ps", "-a", "--filter", f"name={arvo_prefix}_", "--format", "{{.Names}}"],
                           capture_output=True, text=True).stdout.split()
    ids += [n for n in names
            if re.fullmatch(rf"{re.escape(arvo_prefix)}_[A-Za-z0-9-]+_{re.escape(tag)}-[0-9a-f]{{12}}_\d+", n)]
    if ids:
        subprocess.run(["docker", "rm", "-f", *ids], capture_output=True)
    images = subprocess.run(["docker", "images", "--format", "{{.Repository}}:{{.Tag}}"],
                            capture_output=True, text=True).stdout.split()
    repo = image_name_for("")  # docker_utils' per-run images: <repo><project>:<run id>
    stale = [i for i in images if i.startswith(repo) and i.split(":", 1)[-1].startswith(prefix)
             and not i.split(":", 1)[-1].startswith(prefix + "base-")]  # base images outlive a restart
    if stale:
        subprocess.run(["docker", "rmi", "-f", *stale], capture_output=True)
    runs_root = os.path.join(scratch, "out", "runs")
    for project in os.listdir(runs_root) if os.path.isdir(runs_root) else []:
        for run_id in os.listdir(os.path.join(runs_root, project)):
            if run_id.startswith(prefix):
                cleanup_run(project, run_id)
    for vdir in glob.glob(os.path.join(scratch, "verify", "*", "*")):
        shutil.rmtree(vdir, ignore_errors=True)
    return len(ids), len(stale)


# ----------------------------------------------------------------------------------------------- run

class Context:
    def __init__(self, args, store):
        self.args, self.store = args, store
        self.stop = threading.Event()
        self.draining = threading.Event()  # first signal: finish what is in flight, start nothing new
        self.prompts = Prompts(args.setting, store.path("prompts"))
        self.lock = threading.Lock()
        self.resolved = collections.Counter()
        self.hits = collections.Counter()
        self.resolution_times = collections.deque()
        self.verifier = None
        self.prompt_tokens = {}           # target key -> actual prompt tokens (same for every round and model)
        self.tokens_per_char = 1 / 2.8    # learned: the largest ratio seen so far (Gemini ~2.9 chars/token here)

    def token_estimate(self, t, prompt):
        with self.lock:
            return self.prompt_tokens.get(t.key) or int(len(prompt) * self.tokens_per_char)

    def learn_tokens(self, t, prompt, tokens):
        with self.lock:
            self.prompt_tokens[t.key] = tokens
            self.tokens_per_char = max(self.tokens_per_char, tokens / len(prompt))

    def resolve(self, model, hit, live=True):
        with self.lock:
            self.resolved[model] += 1
            self.hits[model] += bool(hit)
            if live:
                self.resolution_times.append(time.time())

    def alert(self, message):
        log.warning(message)
        with open(self.store.path("alerts.log"), "a") as f:
            f.write(f"{now_iso()} {message}\n")


def meminfo_gb(field="MemAvailable"):
    with open("/proc/meminfo") as f:
        for line in f:
            if line.startswith(field + ":"):
                return int(line.split()[1]) / 1024 / 1024
    return None


def io_stall_pct():
    """Share of the last minute in which all non-idle tasks were stalled on IO (/proc/pressure/io, full avg60)."""
    try:
        with open("/proc/pressure/io") as f:
            for line in f:
                if line.startswith("full"):
                    return float(line.split("avg60=")[1].split()[0])
    except (OSError, IndexError, ValueError):
        pass
    return None


class BackgroundLoad:
    """The part of the 1-minute load average that comes from containers at the lowest CPU weight (cpu.weight 1),
    such as fuzzing campaigns run beside the sweep. They yield the CPU to everything else, so they must not make
    adjust_verify_workers hold verification at its floor. Counts their runnable (and uninterruptible) threads
    every 5 s and averages them the way the kernel averages the load."""

    PERIOD = 5

    def __init__(self):
        self.value = None
        threading.Thread(target=self.run, name="bgload", daemon=True).start()

    @staticmethod
    def sample():
        n = 0
        for scope in glob.glob("/sys/fs/cgroup/system.slice/docker-*.scope"):
            try:
                with open(os.path.join(scope, "cpu.weight")) as f:
                    if f.read().strip() != "1":
                        continue
                with open(os.path.join(scope, "cgroup.threads")) as f:
                    tids = f.read().split()
            except OSError:
                continue
            for tid in tids:
                try:
                    with open(f"/proc/{tid}/stat") as f:
                        n += f.read().rsplit(")", 1)[1].split()[0] in ("R", "D")
                except (OSError, IndexError):
                    pass
        return n

    def run(self):
        decay = 2.718281828 ** (-self.PERIOD / 60)
        while True:
            n = self.sample()
            self.value = n if self.value is None else self.value * decay + n * (1 - decay)
            time.sleep(self.PERIOD)


class DiskBusy:
    """Share of the last minute in which the disk under Docker's data-root was busy (/proc/diskstats io_ticks).
    Every verification creates and removes two containers, and Docker writes their metadata synchronously: on a
    spinning disk that churn can saturate the disk before CPU runs out, and then more workers only make each
    container start slower. The IO stall signal (PSI full) misses it while CPU-bound work, such as a fuzzing
    campaign, runs beside the sweep, because some task is always runnable."""

    PERIOD, WINDOW = 5, 60

    def __init__(self):
        self.value, self.samples = None, collections.deque()
        r = subprocess.run(["docker", "info", "-f", "{{.DockerRootDir}}"], capture_output=True, text=True)
        self.disk = self.disk_of(r.stdout.strip() or "/var/lib/docker")
        if self.disk:
            threading.Thread(target=self.run, name="diskbusy", daemon=True).start()

    @staticmethod
    def disk_of(path):
        """The name of the whole disk (sda, not sda2) holding path, or None."""
        try:
            dev = os.stat(path).st_dev
            sysdir = os.path.realpath(f"/sys/dev/block/{os.major(dev)}:{os.minor(dev)}")
        except OSError:
            return None
        if os.path.exists(os.path.join(sysdir, "partition")):
            sysdir = os.path.dirname(sysdir)
        return os.path.basename(sysdir)

    def io_ticks(self):
        with open("/proc/diskstats") as f:
            for line in f:
                parts = line.split()
                if parts[2] == self.disk:
                    return int(parts[12])  # milliseconds spent doing I/O
        return None

    def run(self):
        while True:
            try:
                ticks = self.io_ticks()
            except (OSError, IndexError, ValueError):
                ticks = None
            now = time.time()
            if ticks is not None:
                self.samples.append((now, ticks))
                while now - self.samples[0][0] > self.WINDOW:
                    self.samples.popleft()
                (t0, k0), (t1, k1) = self.samples[0], self.samples[-1]
                if t1 > t0:
                    self.value = min(100.0, (k1 - k0) / (t1 - t0) / 10)
            time.sleep(self.PERIOD)


def adjust_verify_workers(ctx):
    """Verification is bound by whichever of CPU, memory and disk runs out first. On a spinning disk, past its
    limit, more concurrent builds only make every build slower. The CPU check leaves out the load of
    lowest-weight containers (BackgroundLoad); the disk check uses how busy Docker's disk is (DiskBusy) besides
    the IO stall."""
    v, a = ctx.verifier, ctx.args
    load1, ncpu, mem, io = os.getloadavg()[0], os.cpu_count(), meminfo_gb(), io_stall_pct()
    bg = getattr(ctx, "bgload", None)
    background = (bg.value or 0) if bg else 0
    load = max(0.0, load1 - background)
    dk = getattr(ctx, "diskbusy", None)
    busy = dk.value if dk else None
    # Given the disk's busy share, that alone judges the disk. The IO stall (PSI full) reads high on an idle
    # machine, where one task waiting on IO is already a full stall, and would hold verification at its floor
    # while the disk still has room.
    io_high, io_low = (io is not None and io > 3, io is None or io < 1.5) if busy is None else (False, True)
    with v.cv:
        if (mem is not None and mem < 12) or (io is not None and io > 8) or (busy is not None and busy > 95):
            v.allowed = max(a.min_verify_workers, v.allowed - 4)
        elif load > ncpu * 1.25 or io_high or (busy is not None and busy > 85):
            v.allowed = max(a.min_verify_workers, v.allowed - 1)
        elif (load < ncpu * 0.95 and io_low and (busy is None or busy < 70) and v.pending > 0
              and v.active >= v.allowed):
            v.allowed = min(a.max_verify_workers, v.allowed + 2)
        v.cv.notify_all()
    return load1, background, mem, io, busy


def write_status(ctx, generators, start, total_rounds):
    v = ctx.verifier
    load1, background, mem, io, busy = adjust_verify_workers(ctx)
    now = time.time()
    with ctx.lock:
        while ctx.resolution_times and now - ctx.resolution_times[0] > 1800:
            ctx.resolution_times.popleft()
        rate = len(ctx.resolution_times) / min(1800, max(60, now - start))  # rounds/s over the last 30 min
        resolved, hits = dict(ctx.resolved), dict(ctx.hits)
    models = {}
    for g in generators:
        g.limiter.recover()
        rpm, tokens, target, paused = g.limiter.usage()  # over every process using the key
        s = g.stats.snapshot()
        s.update(rpm=rpm, tpm_k=round(tokens / 1e3, 1), limit_factor=round(target, 3),
                 limit_ceiling=round(g.limiter.max_target, 3), paused_s=round(paused),
                 inflight=g.inflight, total=len(g.tasks), resolved=resolved.get(g.model, 0),
                 hits=hits.get(g.model, 0), deferred=sum(t.deferred and not (t.done or t.given_up) for t in g.tasks),
                 given_up=sum(t.given_up for t in g.tasks), stopped=g.stopped, api_finished=g.finished())
        models[g.model] = s
    with v.cv:
        refs = sum(len(r) for r in v.refs.values())
        verify = {"allowed": v.allowed, "active": v.active, "queue": v.pending, "unique_verified": v.counts["ok"],
                  "unique_hit": v.counts["hit"], "compile_fail": v.counts["compile_fail"],
                  "infra_fail": v.counts["infra_fail"], "rounds_sharing_a_result": refs - len(v.refs),
                  "avg_s": round(sum(v.durations) / len(v.durations), 1) if v.durations else None}
    remaining = total_rounds - sum(resolved.values())
    disk = shutil.disk_usage(STATIC_DIR)
    status = {"ts": now_iso(), "elapsed_s": int(now - start), "models": models, "verify": verify,
              "system": {"load1": round(load1, 1), "load1_background": round(background, 1),
                         "mem_avail_gb": round(mem, 1) if mem else None, "io_stall_pct": io,
                         "disk_busy_pct": round(busy, 1) if busy is not None else None,
                         "disk_free_gb": round(disk.free / 1e9)},
              "rounds_total": total_rounds, "rounds_resolved": sum(resolved.values()),
              "eta_h": round(remaining / rate / 3600, 1) if rate > 0 else None}
    ctx.store.append("status.jsonl", status)
    return status


def seed_run(ctx, store, targets, args):
    """--seed-from RUN ...: start from the responses of earlier runs that sent the very same prompt (same
    prompt_sha) to the same model. Their answered rounds become this run's rounds; their rounds without an input
    become first attempts in attempts.jsonl, asked again under --retry-unanswered. Done once per run
    (seeded.json); a target whose prompt changed since starts fresh."""
    marker = store.path("seeded.json")
    if os.path.exists(marker):
        return
    by_key = {t.key: t for t in targets}
    sha = {k: ctx.prompts.get(t)[1] for k, t in by_key.items()}
    counts, taken = collections.Counter(), set()
    for run in args.seed_from:
        src = Store(os.path.join(RESULTS_ROOT, run))
        for name in ("responses.jsonl", "attempts.jsonl"):
            for r in src.load(name):
                k = r["target_key"]
                if r["model"] not in args.models or k not in by_key or r["round"] > args.rounds:
                    continue
                if r.get("prompt_sha") != sha[k]:
                    counts["prompt changed"] += 1
                    continue
                slot = (r["model"], k, r["round"])
                answered = name == "responses.jsonl" and not (r["status"] == "model_error" and r["error"] in RETRY_ERRORS)
                if answered and slot in taken:
                    counts["duplicate round"] += 1
                    continue
                r = dict(r, seeded_from=run, attempt=r.get("attempt", 1))
                if answered:
                    taken.add(slot)
                    store.append("responses.jsonl", r)
                    counts["answered"] += 1
                else:
                    store.append("attempts.jsonl", dict(r, status="model_error"))
                    counts["unanswered"] += 1
    with open(marker, "w") as f:
        json.dump({"ts": now_iso(), "runs": args.seed_from, "counts": dict(counts)}, f)
    log.info("seeded from %s: %s", args.seed_from, dict(counts))
    ctx.alert(f"seeded from {', '.join(args.seed_from)}: {dict(counts)}")


def cmd_run(args):
    key = os.environ.pop("GEMINI_API_KEY", "") or os.environ.get("GOOGLE_API_KEY", "")
    os.environ.pop("GOOGLE_API_KEY", None)
    offline = args.fake_response is not None or args.random_input
    if not offline:
        if not key:
            sys.exit("set GEMINI_API_KEY (or GOOGLE_API_KEY) in the environment")
        import google.generativeai as genai
        genai.configure(api_key=key)
        if args.include_thoughts:
            global GENAI_CLIENT
            from google import genai as google_genai
            from google.genai import types
            GENAI_CLIENT = google_genai.Client(api_key=key, http_options=types.HttpOptions(
                timeout=int(args.api_timeout * 1000)))  # ms; no retry_options = never resent by the client
    del key
    store = Store(os.path.join(RESULTS_ROOT, args.name))
    if offline and args.quota_dir == QUOTA_DIR:
        args.quota_dir = store.path("quota")  # a pipeline test or the random baseline must not eat into the real quota
    logging.basicConfig(filename=store.path("driver.log"), level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(threadName)s %(message)s")
    use_benchmark(args.benchmark)
    targets = list_targets(args.setting, args.projects, args.targets)
    if not targets:
        sys.exit("no targets selected")
    by_key = {t.key: t for t in targets}
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=SOURCE_DIR, capture_output=True, text=True).stdout.strip()
    store.append("runs.jsonl", {"ts": now_iso(), "pid": os.getpid(), "commit": commit, "targets": len(targets),
                                "args": {k: v for k, v in vars(args).items() if k != "func"}})
    ctx = Context(args, store)

    def on_signal(signum, _frame):
        if ctx.draining.is_set():
            ctx.alert(f"received signal {signum} again; stopping now (in-flight work is redone on resume)")
            ctx.stop.set()
        else:
            ctx.alert(f"received signal {signum}; draining: nothing new starts, exiting once in-flight requests and "
                      f"verifications finish (signal again to stop now; resume by starting again)")
            ctx.draining.set()
    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)

    args.scratch = os.path.abspath(os.path.expanduser(args.scratch or store.path("scratch")))
    use_scratch(args.scratch)
    removed = cleanup_orphans(resource_prefix(args.name), args.scratch)
    log.info("start: %d targets x %d rounds x %d models; removed orphans %s", len(targets), args.rounds,
             len(args.models), removed)
    ctx.bases = BaseImages(resource_prefix(args.name), args.scratch)
    ctx.janitor = Janitor(resource_prefix(args.name), args.scratch)
    ctx.verifier = Verifier(ctx, args.models)
    ctx.bgload, ctx.diskbusy = BackgroundLoad(), DiskBusy()
    # Resume: rounds already answered are not asked again; verified harnesses are not run again.
    for record in store.load("verifications.jsonl"):
        if valid_verdict(record):
            ctx.verifier.preload(record)
    # A verdict depends only on the target and the harness (or T6 input), not on the setting or model that
    # produced it: reuse what other runs verified.
    shared = 0
    for other in sorted(glob.glob(os.path.join(RESULTS_ROOT, "*", "verifications.jsonl"))):
        if os.path.dirname(other) == store.root:
            continue
        for record in Store(os.path.dirname(other)).load("verifications.jsonl"):
            if record["target_key"] in by_key and (record["target_key"], record["harness_sha"]) not in ctx.verifier.state \
                    and valid_verdict(record):
                ctx.verifier.preload(record)
                shared += 1
    log.info("reusing %d verifications from other runs", shared)
    if args.seed_from:
        seed_run(ctx, store, targets, args)
    responses = [r for r in store.load("responses.jsonl") if r["model"] in args.models and r["target_key"] in by_key
                 and r["round"] <= args.rounds]
    done = {(r["model"], r["target_key"], r["round"]) for r in responses}
    attempts = [r for r in store.load("attempts.jsonl") if r["model"] in args.models and r["target_key"] in by_key
                and r["round"] <= args.rounds]
    gens = [Generator(m, targets, args.rounds, done, ctx) for m in args.models]
    by_model = {g.model: g for g in gens}
    for e in store.load("api_events.jsonl"):
        if e["kind"] == "blocked" and e["model"] in by_model and e["target_key"] in by_key:
            by_model[e["model"]].refusals[e["target_key"]] += 1
            if (e.get("usage") or {}).get("output"):
                by_model[e["model"]].outputs[e["target_key"]].append(e["usage"]["output"])
            if e.get("usage"):  # refused calls are billed too
                by_model[e["model"]].stats.refused(e["usage"], cost_of(e["model"], e["usage"]), live=False)
    for r in responses:
        by_model[r["model"]].answers[r["target_key"]] += 1
        by_model[r["model"]].requests[r["target_key"]] += 1
    tries = collections.Counter((r["model"], r["target_key"], r["round"]) for r in responses + attempts)
    for r in attempts:
        g = by_model[r["model"]]
        g.requests[r["target_key"]] += 1
        g.stats.retried(r["usage"], cost_of(r["model"], r["usage"]), r["error"], live=False)
    for g in gens:
        for task in g.tasks:
            task.tries = tries[(g.model, task.target.key, task.round)]
        for key in set(g.requests) | set(g.refusals):
            g.requests[key] += g.refusals[key]  # API refusals are requests too
            with g.lock:
                g.check_request_cap(key)
    for g in gens:  # targets given up for refusals before the restart stay given up
        for key, n in g.refusals.items():
            if (n >= ALWAYS_BLOCKED_AFTER and g.answers[key] == 0) or n >= MAX_REFUSALS_PER_TARGET:
                for task in g.tasks:
                    if task.target.key == key and not task.done:
                        task.given_up = True
    for r in responses:
        if r["usage"]["prompt"]:
            ctx.prompt_tokens[r["target_key"]] = r["usage"]["prompt"]
        if r["usage"].get("output"):
            by_model[r["model"]].outputs[r["target_key"]].append(r["usage"]["output"])
        by_model[r["model"]].stats.completed(r["usage"], cost_of(r["model"], r["usage"]), r["error"], live=False,
                                             blocked=r.get("block_reason") is not None)
        if r["status"] == "input":
            ctx.verifier.submit(by_key[r["target_key"]], r["harness_sha"], r["harness_path"], r["model"], r["round"])
        else:
            ctx.resolve(r["model"], False, live=False)
    with ctx.lock:
        ctx.resolution_times.clear()  # rate estimates only count this process's work

    for g in gens:
        threading.Thread(target=g.run, name=f"dispatch-{g.model}", daemon=True).start()
    start, total_rounds = time.time(), len(targets) * args.rounds * len(args.models)
    next_status = 0
    drain_deadline = None
    while not ctx.stop.is_set():
        if time.time() >= next_status:
            write_status(ctx, gens, start, total_rounds)
            next_status = time.time() + 60
        if ctx.draining.is_set():
            drain_deadline = drain_deadline or time.time() + args.api_timeout + 600
            if (all(g.inflight == 0 for g in gens) and ctx.verifier.active == 0) or time.time() > drain_deadline:
                break
            time.sleep(5)
            continue
        if all(g.finished() for g in gens) and ctx.verifier.idle():
            if not ctx.verifier.retried_infra and ctx.verifier.counts["infra_fail"] > 0:
                ctx.verifier.requeue_infra_failures()
                continue
            break
        time.sleep(5)
    write_status(ctx, gens, start, total_rounds)
    ctx.stop.set()
    ctx.alert("run finished" if not any(g.remaining() for g in gens) else "run stopped")
    subprocess.run(["docker", "rm", "-f", ctx.janitor.name], capture_output=True)
    os._exit(0)  # in-flight API threads are abandoned; their rounds are redone on resume


# ----------------------------------------------------------------------------------------------- status/report

def cmd_status(args):
    path = os.path.join(RESULTS_ROOT, args.name, "status.jsonl")
    if not os.path.exists(path):
        sys.exit(f"no status yet: {path}")
    with open(path, "rb") as f:
        f.seek(max(0, os.path.getsize(path) - 2_000_000))
        history = [json.loads(line) for line in f.read().decode(errors="replace").strip().splitlines()[1:]]
    s = history[-1]
    # per-model ETA from the last 30 minutes of the current process (elapsed_s restarts with the driver)
    start = max((i for i in range(1, len(history)) if history[i]["elapsed_s"] < history[i - 1]["elapsed_s"]),
                default=0)
    past = next((h for h in history[start:] if s["elapsed_s"] - h["elapsed_s"] <= 1800), s)
    span = s["elapsed_s"] - past["elapsed_s"]
    etas = {}
    for m, x in s["models"].items():
        rate = (x["resolved"] - past["models"].get(m, x)["resolved"]) / span if span > 0 else 0
        etas[m] = f"{(x['total'] - x['resolved']) / rate / 3600:.1f}h" if rate > 0 else "?"
    el = s["elapsed_s"]
    print(f"{s['ts']}  elapsed {el // 3600}h{el % 3600 // 60:02d}m  resolved {s['rounds_resolved']}/{s['rounds_total']}"
          f"  ETA {s['eta_h']}h")
    print(f"{'model':24s} {'RPM':>4s} {'TPM(k)':>7s} {'fac':>5s} {'infl':>4s} {'answered':>11s} {'resolved':>11s} "
          f"{'hits':>5s} {'cache%':>6s} {'out/call':>8s} {'429/5xx/t.o./net/oth/blocked (1m | total)':>42s} {'$':>8s}")
    for m, x in s["models"].items():
        e1, et = x["errors_1m"], x["errors_total"]
        kinds = ("429", "5xx", "timeout", "network", "other", "blocked")
        errs = "/".join(str(e1.get(k, 0)) for k in kinds) + " | " + "/".join(str(et.get(k, 0)) for k in kinds)
        print(f"{m:24s} {x['rpm']:>4d} {x['tpm_k']:>7.0f} {x['limit_factor']:>5.2f} {x['inflight']:>4d} "
              f"{x['done']:>5d}/{x['total']:<5d} {x['resolved']:>5d}/{x['total']:<5d} {x['hits']:>5d} "
              f"{str(x['cache_hit_pct']):>6s} {str(x['avg_output_tokens']):>8s} {errs:>42s} {x['spend_usd']:>8.2f}")
        print(f"{'':24s} ETA {etas[m]} (at the resolution rate of the last {span // 60} min)")
        if x["model_errors"] or x["deferred"] or x["given_up"] or x["stopped"]:
            print(f"{'':24s} model errors {x['model_errors']} (of which blocked: {x.get('blocked', 0)})  "
                  f"deferred {x['deferred']}  given up {x['given_up']}"
                  + (f"  STOPPED: {x['stopped']}" if x["stopped"] else ""))
    v, sy = s["verify"], s["system"]
    print(f"verify: {v['active']}/{v['allowed']} running, queue {v['queue']}, unique verified {v['unique_verified']} "
          f"(hit {v['unique_hit']}), rounds sharing a result {v['rounds_sharing_a_result']}, "
          f"compile_fail {v['compile_fail']}, infra_fail {v['infra_fail']}, avg {v['avg_s']}s")
    background = f" (lowest-weight containers {sy['load1_background']})" if sy.get("load1_background") else ""
    busy = f", Docker disk busy {sy['disk_busy_pct']}%" if sy.get("disk_busy_pct") is not None else ""
    print(f"system: load {sy['load1']}{background}, IO stall {sy.get('io_stall_pct')}%{busy}, "
          f"mem avail {sy['mem_avail_gb']}G, disk free {sy['disk_free_gb']}G")


def outcomes(root):
    """(model, target_key) -> {round: (outcome, latency)}; outcome = hit | miss | <error field> | compile_fail |
    pending, and for T6 repro (reached and reproduced) | hit (reached only) | miss."""
    store = Store(root)
    verified = {}
    others = [p for p in sorted(glob.glob(os.path.join(RESULTS_ROOT, "*", "verifications.jsonl")))
              if os.path.dirname(p) != os.path.abspath(root)]
    unbuildable = set()  # targets whose patch does not apply to the pinned commit: every image build fails
    never_ran = set()  # targets verified since the check whose fuzzer never ran an input (a broken patch)
    for path in others + [store.path("verifications.jsonl")]:  # this run's own verdicts win; others fill gaps
        for v in Store(os.path.dirname(path)).load("verifications.jsonl"):
            if valid_verdict(v):
                verified[(v["target_key"], v["harness_sha"])] = v
            elif v.get("status") == "infra_fail" and "git apply" in (v.get("detail") or "") \
                    and "did not complete successfully" in (v.get("detail") or ""):
                unbuildable.add(v["target_key"])
            elif v.get("status") == "infra_fail" and (v.get("detail") or "").startswith("the fuzzer never ran"):
                never_ran.add(v["target_key"])
    table, meta = collections.defaultdict(dict), {}
    for r in store.load("responses.jsonl"):
        if r["status"] == "model_error":
            outcome = "blocked" if r.get("block_reason") is not None else r["error"]
        else:
            v = verified.get((r["target_key"], r["harness_sha"]))
            if v is None and r["target_key"] in unbuildable:
                outcome = "build_fail"  # the artifact records such a round as run without a hit
            elif v is None and r["target_key"] in never_ran:
                outcome = "not_run"  # likewise
            else:
                outcome = "pending" if v is None else ("repro" if v.get("reproduced") else "hit") if v["hit"] else (
                    "compile_fail" if v["status"] == "compile_fail" else "miss")
        table[(r["model"], r["target_key"])][r["round"]] = (outcome, r.get("latency_s"))
        meta[r["target_key"]] = (r["project"], r["level"], r["target"])
    return table, meta


def outcomes_of(root, projects):
    table, meta = outcomes(root)
    if projects:
        table = {(m, k): v for (m, k), v in table.items() if meta[k][0] in projects}
    return table, meta


def cmd_report(args):
    root = os.path.join(RESULTS_ROOT, args.name)
    runs = Store(root).load("runs.jsonl")
    setting = runs[-1]["args"].get("setting", "realistic") if runs else "realistic"
    if SETTINGS[setting][0]:
        return report_t6(args.name, root, setting, args.projects)
    no_context = SETTINGS[setting][1]
    table, meta = outcomes_of(root, args.projects)
    models = sorted({m for m, _ in table})
    lines = []
    # 1) the artifact's per-project CSVs
    csv_dir = os.path.join(root, "csv")
    os.makedirs(csv_dir, exist_ok=True)
    cwd = os.getcwd()
    os.chdir(csv_dir)
    try:
        ep.EVAL_TIME = args.name
        for model in models:
            for project in sorted({meta[k][0] for (m, k) in table if m == model}):
                result = collections.defaultdict(dict)
                for (m, k), rounds in table.items():
                    if m != model or meta[k][0] != project:
                        continue
                    done = [o for o, _ in rounds.values() if o != "pending"]
                    entry = {f: sum(o == f or (f == "other_err" and o == "blocked") for o in done) for f in ERROR_FIELDS}
                    entry.update(hit_target_count=sum(o == "hit" for o in done), executed_rounds=len(done),
                                 query_durations=[rounds[r][1] for r in sorted(rounds)])
                    result[meta[k][1]][meta[k][2]] = entry
                ns = argparse.Namespace(project_dir=os.path.join(DATA_DIR, project, SETTINGS[setting][2]),
                                        llm_model=model, vibe=False, no_context=no_context)
                ep.write_to_csv(ns, dict(result))
    finally:
        os.chdir(cwd)
    # 2) summary by model x tier x language
    for model in models:
        lines.append(f"\n## {model}\n")
        lines.append("| group | " + " | ".join(f"T{i + 1}" for i in range(5)) + " | all |")
        lines.append("|---|" + "---|" * 6)
        for group in ("Python", "C/C++", "all"):
            cells = []
            for tier in TIERS + [None]:
                keys = [k for (m, k) in table if m == model and (tier is None or meta[k][1] == tier)
                        and (group == "all" or (LANGUAGE[meta[k][0]] == "Python") == (group == "Python"))]
                if not keys:
                    cells.append("-")
                    continue
                rounds = [o for k in keys for o, _ in table[(model, k)].values() if o != "pending"]
                reached = sum(any(o in ("hit", "repro") for o, _ in table[(model, k)].values()) for k in keys)
                cells.append(f"{reached}/{len(keys)} tgt, {sum(o in ('hit', 'repro') for o in rounds)}/{len(rounds)} rnd")
            lines.append(f"| {group} | " + " | ".join(cells) + " |")
        lines.append("\nmean pass@5 over the targets (per target over its resolved rounds):\n")
        lines.append("| group | " + " | ".join(f"T{i + 1}" for i in range(5)) + " |")
        lines.append("|---|" + "---|" * 5)
        for group in ("Python", "C/C++"):
            cells = []
            for tier in TIERS:
                keys = [k for (m, k) in table if m == model and meta[k][1] == tier
                        and (LANGUAGE[meta[k][0]] == "Python") == (group == "Python")]
                cells.append(f"{100 * mean_pass5(table, model, keys, ('hit', 'repro')):.1f}% ({len(keys)})"
                             if keys else "-")
            lines.append(f"| {group} | " + " | ".join(cells) + " |")
        all_rounds = [o for (m, k), rs in table.items() if m == model for o, _ in rs.values()]
        counts = collections.Counter(all_rounds)
        lines.append(f"\nrounds: {len(all_rounds)}; outcomes: {dict(counts)}; format failures "
                     f"(pattern_unmatch_err): {100 * counts['pattern_unmatch_err'] / max(1, len(all_rounds)):.1f}%; "
                     f"blocked by the API (block_reason set; counted as other_err): "
                     f"{100 * counts['blocked'] / max(1, len(all_rounds)):.1f}%")
    refused = collections.Counter()
    for e in Store(root).load("api_events.jsonl"):
        if e["kind"] == "blocked":
            refused[(e["model"], e["target_key"])] += 1
    for model in models:
        per_target = {k: n for (m, k), n in refused.items() if m == model}
        answered = sum(1 for (m, k), rs in table.items() if m == model for _ in rs)
        lines.append(f"\n{model}: {sum(per_target.values())} responses withheld by the API and asked again "
                     f"(vs {answered} answered rounds); most refused targets: "
                     f"{sorted(per_target.items(), key=lambda x: -x[1])[:5]}")
    unbuildable = sorted({k for (m, k), rs in table.items() for o, _ in rs.values() if o == "build_fail"})
    if unbuildable:
        lines.append(f"\ntargets whose target.patch does not apply to the pinned commit (every build fails; "
                     f"the artifact counts their rounds as misses): {unbuildable}")
    not_run = sorted({k for (m, k), rs in table.items() for o, _ in rs.values() if o == "not_run"})
    if not_run:
        lines.append(f"\ntargets whose fuzzer never runs an input (the patched harness does not start; "
                     f"the artifact counts their rounds as misses): {not_run}")
    text = "# " + args.name + "\n" + "\n".join(lines) + "\n\ncell: targets reached (>=1 hit) / targets | hit rounds / resolved rounds\n"
    with open(os.path.join(root, "summary.md"), "w") as f:
        f.write(text)
    print(text)
    print(f"CSVs (artifact format): {csv_dir}")


def pass_at_k(n, c, k=5):
    """Unbiased pass@k of a target with c successes in n rounds; with fewer than k rounds, whether any succeeded."""
    return 1 - comb(n - c, k) / comb(n, k) if n >= k else float(c > 0)


def mean_pass5(table, model, keys, success):
    """Mean over `keys` of each target's pass@5, a round counting when its outcome is in `success`."""
    vals = []
    for k in keys:
        done = [o for o, _ in table[(model, k)].values() if o != "pending"]
        vals.append(pass_at_k(len(done), sum(o in success for o in done)))
    return sum(vals) / len(vals) if vals else 0.0


def report_t6(name, root, setting, projects=None):
    """evaluation_arvo's per-project CSVs (hit/rounds (reproduced)) and a summary per project, with the mean
    pass@5 of reaching the target line and of reproducing the crash."""
    table, meta = outcomes_of(root, projects)
    models = sorted({m for m, _ in table})
    variant = SETTINGS[setting][1]
    retrieval = "realistic" if variant is None else "no_context" if variant == "original" else f"no_context_{variant}"
    csv_dir = os.path.join(root, "csv")
    os.makedirs(csv_dir, exist_ok=True)
    lines = []
    for model in models:
        keys = sorted(k for (m, k) in table if m == model)
        for project in sorted({meta[k][0] for k in keys}):
            with open(os.path.join(csv_dir, f"arvo-{project}-{retrieval}-{model}-{name}.csv"), "w") as f:
                for k in keys:
                    if meta[k][0] != project:
                        continue
                    rounds = table[(model, k)]
                    done = [o for o, _ in rounds.values() if o != "pending"]
                    errors = collections.Counter("other_err" if o == "blocked" else o for o in done
                                                 if o not in ("hit", "repro", "miss"))
                    err_str = "; ".join(f"{e}: {n}" for e, n in sorted(errors.items()))
                    f.write(f"{project},{meta[k][2]},{sum(o in ('hit', 'repro') for o in done)}/{len(done)} "
                            f"({sum(o == 'repro' for o in done)}),({err_str}),{[rounds[r][1] for r in sorted(rounds)]}\n")
        lines.append(f"\n## {model}\n")
        lines.append("| CVEs | reached (>=1 round) | reproduced (>=1 round) | hit rounds | reproducing rounds "
                     "| reached pass@5 | reproduced pass@5 |")
        lines.append("|---|---|---|---|---|---|---|")
        for group in sorted({meta[k][0] for k in keys}) + ["all"]:
            gkeys = [k for k in keys if group == "all" or meta[k][0] == group]
            per = [[o for o, _ in table[(model, k)].values() if o != "pending"] for k in gkeys]
            rounds = [o for p in per for o in p]
            # reproduction only where it is measurable (POC_UNREPRODUCIBLE)
            rkeys = [k for k in gkeys if meta[k][2] not in POC_UNREPRODUCIBLE]
            rper = [p for k, p in zip(gkeys, per) if meta[k][2] not in POC_UNREPRODUCIBLE]
            rrounds = [o for p in rper for o in p]
            lines.append(f"| {group} ({len(gkeys)}) | {sum(any(o in ('hit', 'repro') for o in p) for p in per)} | "
                         f"{sum('repro' in p for p in rper)}/{len(rper)} | "
                         f"{sum(o in ('hit', 'repro') for o in rounds)}/{len(rounds)} | "
                         f"{rrounds.count('repro')}/{len(rrounds)} | "
                         f"{100 * mean_pass5(table, model, gkeys, ('hit', 'repro')):.0f}% | "
                         f"{100 * mean_pass5(table, model, rkeys, ('repro',)):.0f}% |")
        counts = collections.Counter(o for k in keys for o, _ in table[(model, k)].values())
        lines.append(f"\noutcomes: {dict(counts)}")
        excluded = sorted({meta[k][2] for k in keys} & set(POC_UNREPRODUCIBLE))
        if excluded:
            lines.append("reproduction not measurable, left out of the reproduce columns: "
                         + "; ".join(f"{c} ({POC_UNREPRODUCIBLE[c]})" for c in excluded))
    text = f"# {name} ({setting})\n" + "\n".join(lines) + "\n"
    with open(os.path.join(root, "summary.md"), "w") as f:
        f.write(text)
    print(text)
    print(f"CSVs (evaluation_arvo format): {csv_dir}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    run = sub.add_parser("run")
    run.add_argument("--name", required=True)
    run.add_argument("--models", nargs="+", required=True)
    run.add_argument("--setting", default="realistic", choices=list(SETTINGS),
                     help="realistic / oracle / bm25 source context or no_context[_placeholder] for T1-T5; "
                          "t6_realistic / t6_no_context[_placeholder] for the cases of --benchmark")
    run.add_argument("--benchmark", default=T6_BENCHMARK,
                     help="ARVO-style benchmark json of the t6_* settings; the cases' context dirs are "
                          "<project>/<retrieval>/<id>/ next to it (default: T6, static/data/target-arvo/benchmark.json; "
                          "the knowledge-cutoff study set is static/data/post-cutoff/benchmark.json)")
    run.add_argument("--rounds", type=int, default=20)
    run.add_argument("--projects", nargs="+")
    run.add_argument("--targets", nargs="+", help="target dir names, optionally as <project>::<name>")
    run.add_argument("--rpm", type=float, default=25)
    run.add_argument("--tpm", type=float, default=1_000_000)
    run.add_argument("--limit-factor", type=float, default=0.85, help="fraction of the RPM/TPM quota to aim for")
    run.add_argument("--quota-dir", default=QUOTA_DIR,
                     help="where the per-model usage ledger shared by all processes on this key lives")
    run.add_argument("--max-inflight", type=int, default=64, help="concurrent requests per model")
    run.add_argument("--release-after", type=float, default=120,
                     help="seconds after a target's first request before its other rounds go out anyway")
    run.add_argument("--api-timeout", type=float, default=1800, help="seconds per API request")
    run.add_argument("--startup-delay", type=float, default=60,
                     help="seconds to wait before the first request, so a restart does not burst over the quota")
    run.add_argument("--runs", type=int, default=100, help="executions of the fixed input per verification")
    run.add_argument("--verify-workers", type=int, default=12, help="initial concurrent verifications")
    run.add_argument("--min-verify-workers", type=int, default=4)
    run.add_argument("--max-verify-workers", type=int, default=32)
    run.add_argument("--scratch", help="fast local dir for build and /out dirs (default <results>/NAME/scratch)")
    run.add_argument("--retry-unanswered", action="store_true",
                     help="ask a round again when the response has no input to run (text refusal, empty answer)")
    run.add_argument("--max-requests-per-target", type=int, default=0,
                     help="stop asking a target after this many responses of any kind (0: no cap)")
    run.add_argument("--seed-from", nargs="+", help="earlier runs whose responses to the same prompts start this run")
    run.add_argument("--include-thoughts", action="store_true",
                     help="send the same prompt through google-genai with thought summaries on; the summaries are "
                          "stored as 'thoughts' in responses.jsonl / attempts.jsonl, the answer is scored as before")
    run.add_argument("--fake-response", help="use this text instead of calling the API (pipeline test)")
    run.add_argument("--random-input", action="store_true",
                     help="the artifact's random baseline: every round answers with a fresh 10-character alphanumeric "
                          "string instead of calling the API (use --models random and a high --rpm/--tpm)")
    run.add_argument("--fake-refusal-rate", type=float, default=0.0, help="with --fake-response: share refused")
    run.add_argument("--fake-unanswered-rate", type=float, default=0.0,
                     help="with --fake-response: share answered in text without an input")
    run.add_argument("--fake-latency", type=float, default=0.0, help="with --fake-response: seconds per response")
    run.set_defaults(func=cmd_run)
    for name, func in (("status", cmd_status), ("report", cmd_report)):
        p = sub.add_parser(name)
        p.add_argument("--name", required=True)
        if name == "report":
            p.add_argument("--projects", nargs="+", help="only these projects (the 'all' row then pools them)")
        p.set_defaults(func=func)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
