#!/usr/bin/env python3
"""Label Claude Code trajectory turns for the reachability-effort study.

The turn unit is ONE assistant message (a unique ``message.id`` in the session JSONL), which
bundles its summarized-thinking block, assistant text, and tool call(s); its billed
``output_tokens`` / ``thinking_tokens`` are read off that message (the authoritative per-step
record, deduped by id -- the stream-json transcript's per-message tokens are placeholders).

Each turn is classified into one of six effort categories and flagged
reachability-related for the broad tier:

  1 Reachability       entry-point / attack-surface ID, call-chain tracing, path & guard
                       reasoning, reach-directed input construction, dynamic reach checks
  2 Bug/trigger        why it is a bug + what makes it fire once the site is reached
  3 Code comprehension general reading not tied to a reach question
  4 Pattern/API search grep/ripgrep/find for APIs or call sites
  5 Build/run/tooling  compile, run the target, run a candidate input, debuggers
  6 Meta               planning, summarising, reporting

Labeling is hybrid: deterministic command rules tag tool-only turns (grep -> 4, build/run
-> 5); Gemini 3.1-pro judges every turn from the summarized thinking + text + tool calls +
truncated tool results. The judge runs on ALL turns so we can report (a) the hybrid label set
(rule wins on tool-only turns, judge elsewhere), (b) the pure-judge label set, and (c)
judge-vs-rule agreement on tool-only turns as a validation signal. Shares are reported three
ways -- share of turns, of output_tokens, of thinking_tokens -- with the two-tier reachability
measure (narrow = category 1; broad = category 1 OR reachability_related).

Usage:
  GEMINI_API_KEY (or GOOGLE_API_KEY) from the environment (dropped at start-up, like the sweep driver).
  python exp_reach/label_turns.py <run_dir> [--model gemini-3.1-pro-preview] [--workers 32] [--only ID ...] [--dry]
  <run_dir> holds episodes/<id>/{session.jsonl,result.json}; writes labels.jsonl per episode and, unless --only
  restricts the episodes, reach_share.json at the run root. The shares of reasoning (thinking) tokens per task and
  pooled over all turns are the effort study's result.
"""
import argparse
import json
import os
import re
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

MODEL = "gemini-3.1-pro-preview"
CATS = {
    1: "Reachability",
    2: "Bug/trigger semantics",
    3: "Code comprehension",
    4: "Pattern/API search",
    5: "Build/run/tooling",
    6: "Meta",
}


# ----------------------------------------------------------------------------- turn extraction
def _text_of(content):
    """Normalise a message.content (str or list of blocks) to a plain string."""
    if isinstance(content, str):
        return content
    out = []
    for b in content or []:
        if isinstance(b, dict):
            if b.get("type") == "text":
                out.append(b.get("text", ""))
            elif b.get("type") == "tool_result":
                out.append(_text_of(b.get("content")))
    return "\n".join(x for x in out if x)


def extract_turns(session_path):
    """Return turns in order. A turn = one assistant message.id; it collects every content block
    across the (possibly several) assistant rows that share that id, plus the tool results that
    answer its tool calls (looked up from the following user rows)."""
    rows = [json.loads(l) for l in open(session_path) if l.strip()]

    # tool_use_id -> truncated result text (from user rows' tool_result blocks)
    results = {}
    for r in rows:
        if r.get("type") != "user":
            continue
        for b in r.get("message", {}).get("content", []) or []:
            if isinstance(b, dict) and b.get("type") == "tool_result":
                results[b.get("tool_use_id")] = _text_of(b.get("content"))

    turns = {}        # msgid -> turn dict
    order = []
    for r in rows:
        if r.get("type") != "assistant":
            continue
        m = r.get("message", {})
        mid = m.get("id")
        if mid is None:
            continue
        if mid not in turns:
            u = m.get("usage", {})
            turns[mid] = {
                "msgid": mid,
                "thinking": [],
                "text": [],
                "tools": [],          # list of {name, input}
                "output_tokens": u.get("output_tokens", 0),
                "thinking_tokens": u.get("output_tokens_details", {}).get("thinking_tokens", 0),
                "timestamp": r.get("timestamp"),
                "model": m.get("model"),
            }
            order.append(mid)
        t = turns[mid]
        for b in m.get("content", []) or []:
            bt = b.get("type")
            if bt == "thinking":
                t["thinking"].append(b.get("thinking", "") or b.get("text", ""))
            elif bt == "text":
                t["text"].append(b.get("text", ""))
            elif bt == "tool_use":
                t["tools"].append({"name": b.get("name"), "input": b.get("input", {}),
                                   "result": results.get(b.get("id"), "")})
    out = []
    for i, mid in enumerate(order, 1):
        t = turns[mid]
        t["idx"] = i
        t["thinking"] = "\n".join(x for x in t["thinking"] if x).strip()
        t["text"] = "\n".join(x for x in t["text"] if x).strip()
        out.append(t)
    return out


# ------------------------------------------------------------------------- deterministic rules
_RE_SEARCH = re.compile(r"\b(grep|rg|egrep|fgrep|ripgrep|ag|ack)\b|\bgit\s+grep\b|\bfind\b[^|]*-name")
# compiler/build tools must sit at a command boundary, so a C++ source name like foo.cc
# (where "cc" follows a ".") is NOT mistaken for the cc compiler.
_RE_BUILD = re.compile(r"(?:^|[\s;&|])(make|cmake|ninja|configure|clang\+\+|clang|gcc|g\+\+|cc)\b")
_RE_RUN = re.compile(r"/out/|\bgdb\b|\bvalgrind\b|\bstrace\b|\bltrace\b|ASAN_OPTIONS|\brun_fuzzer\b|(^|\s)\./")
_RE_READ = re.compile(r"\b(cat|sed|head|tail|less|more|\bnl\b|wc|ls|nm|readelf|objdump|strings|file|awk|od|xxd)\b")


def rule_label(turn):
    """A bucket for a TOOL-ONLY turn (no thinking, no text) with a single unambiguous command;
    else None (defer to the judge). This deterministic tagging is also a cross-check."""
    if turn["thinking"] or turn["text"]:
        return None
    if len(turn["tools"]) != 1:
        return None
    tool = turn["tools"][0]
    if tool["name"] == "mcp__agent__submit":
        return 5
    if tool["name"] != "mcp__agent__shell":
        return None
    cmd = str(tool["input"].get("command", ""))
    # a line that only searches / only builds / only runs / only reads -> a clear bucket;
    # anything mixing an input-construction verb (printf/python/xxd writing a file) is ambiguous.
    if re.search(r"\b(printf|perl|dd|base64)\b|python3?\s+-c|echo\s+-n", cmd):
        return None
    hits = []
    if _RE_SEARCH.search(cmd):
        hits.append(4)
    if _RE_BUILD.search(cmd) or _RE_RUN.search(cmd):
        hits.append(5)
    if _RE_READ.search(cmd) and not hits:
        hits.append(3)
    return hits[0] if len(set(hits)) == 1 else None


# -------------------------------------------------------------------------------- Gemini judge
# The judge's instructions; the same text is the codebook for a second (human) annotator.
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "codebook.txt"), encoding="utf-8") as _f:
    CODEBOOK = _f.read()


def _clip(s, n):
    s = s or ""
    return s if len(s) <= n else s[: n - 120] + "\n...[clip]...\n" + s[-100:]


def render_turn(t):
    parts = [f"=== TURN {t['idx']} ==="]
    if t["thinking"]:
        parts.append("thinking_summary: " + _clip(t["thinking"], 1200))
    if t["text"]:
        parts.append("message: " + _clip(t["text"], 600))
    for tool in t["tools"]:
        if tool["name"] == "mcp__agent__shell":
            parts.append("shell: " + _clip(str(tool["input"].get("command", "")), 500))
        elif tool["name"] == "mcp__agent__submit":
            parts.append("submit input: " + str(tool["input"].get("path", "")))
        else:
            parts.append(f"tool {tool['name']}: " + _clip(json.dumps(tool['input']), 200))
        if tool["result"]:
            parts.append("  -> result: " + _clip(tool["result"], 500))
    if not (t["thinking"] or t["text"] or t["tools"]):
        parts.append("(empty)")
    return "\n".join(parts)


def judge_episode(turns, model, retries=7):
    import google.generativeai as genai
    body = "\n\n".join(render_turn(t) for t in turns)
    prompt = CODEBOOK + f"\n\nThere are {len(turns)} turns.\n\n" + body
    last = None
    for attempt in range(retries):
        try:
            resp = genai.GenerativeModel(model).generate_content(
                prompt, generation_config={"temperature": 0.0},
                request_options={"timeout": 600, "retry": None})
            raw = resp.text.strip()
            raw = re.sub(r"^```(json)?|```$", "", raw, flags=re.MULTILINE).strip()
            data = json.loads(raw[raw.index("["): raw.rindex("]") + 1])
            by_turn = {int(d["turn"]): d for d in data}
            if all(t["idx"] in by_turn for t in turns):
                return by_turn
            last = f"missing turns: got {sorted(by_turn)} want {[t['idx'] for t in turns]}"
        except Exception as e:  # noqa: BLE001
            last = f"{type(e).__name__}: {e}"
        # exponential backoff so high worker counts can saturate the rate limit: when we get 429s
        # the losers back off and retry rather than failing, keeping the pipe full.
        time.sleep(min(90, 5 * 2 ** attempt))
    raise RuntimeError(f"judge failed after {retries}: {last}")


# ----------------------------------------------------------------------------------- aggregate
WEIGHTS = ["turns", "output_tokens", "thinking_tokens"]


def episode_shares(labeled):
    """labeled: list of turn dicts carrying final_category + reachability_related + weights.
    Returns {weight: {cat_share..., narrow, broad}} as fractions of that weight's total."""
    out = {}
    for w in WEIGHTS:
        tot = sum((1 if w == "turns" else t[w]) for t in labeled) or 1
        share = {c: 0.0 for c in CATS}
        narrow = broad = 0.0
        for t in labeled:
            wt = (1 if w == "turns" else t[w]) / tot
            share[t["final_category"]] += wt
            if t["final_category"] == 1:
                narrow += wt
            if t["final_category"] == 1 or t["reachability_related"]:
                broad += wt
        out[w] = {"by_category": {CATS[c]: round(v, 4) for c, v in share.items()},
                  "reach_narrow": round(narrow, 4), "reach_broad": round(broad, 4),
                  "total": tot}
    return out


def bootstrap_ci(values, n=2000, seed=0, lo=2.5, hi=97.5):
    import random
    if len(values) < 2:
        return [None, None]
    rng = random.Random(seed)
    means = []
    for _ in range(n):
        s = [values[rng.randrange(len(values))] for _ in values]
        means.append(sum(s) / len(s))
    means.sort()
    return [round(means[int(lo / 100 * n)], 4), round(means[int(hi / 100 * n)], 4)]


# ---------------------------------------------------------------------------------------- main
def process_episode(ep_dir, model, dry):
    cve = os.path.basename(ep_dir.rstrip("/"))
    sess = os.path.join(ep_dir, "session.jsonl")
    if not os.path.exists(sess):
        return None
    result = json.load(open(os.path.join(ep_dir, "result.json")))
    turns = extract_turns(sess)
    rules = {t["idx"]: rule_label(t) for t in turns}
    if dry:
        for t in turns:
            print(f"[{cve}] turn {t['idx']:3d} rule={rules[t['idx']]} out={t['output_tokens']:5d} "
                  f"think={t['thinking_tokens']:5d} tools={[x['name'].split('__')[-1] for x in t['tools']]} "
                  f"think:{(t['thinking'][:70] or '').strip()!r}")
        return None

    judged = judge_episode(turns, model)
    labeled = []
    tool_only_agree = tool_only_total = 0
    for t in turns:
        j = judged[t["idx"]]
        jc = int(j["category"])
        rl = rules[t["idx"]]
        final = rl if rl is not None else jc        # hybrid: rule wins on tool-only turns
        if rl is not None:
            tool_only_total += 1
            tool_only_agree += int(rl == jc)
        labeled.append({**{k: t[k] for k in ("idx", "output_tokens", "thinking_tokens", "timestamp")},
                        "judge_category": jc, "rule_category": rl, "final_category": final,
                        "reachability_related": bool(j["reachability_related"]),
                        "rationale": j.get("rationale", ""),
                        "tools": [x["name"].split("__")[-1] for x in t["tools"]]})
    # write per-turn labels
    with open(os.path.join(ep_dir, "labels.jsonl"), "w") as f:
        for row in labeled:
            f.write(json.dumps(row) + "\n")
    shares = episode_shares(labeled)
    # a pure-judge variant for robustness
    for row in labeled:
        row["final_category"] = row["judge_category"]
    shares_judge = episode_shares(labeled)
    return {"cve": cve, "project": result.get("project"), "outcome": result.get("outcome"),
            "reached": result.get("reached"), "reproduced": result.get("reproduced"),
            "turns": len(turns), "cost_usd": result.get("cost_usd"),
            "tool_only_agreement": [tool_only_agree, tool_only_total],
            "shares_hybrid": shares, "shares_judge": shares_judge}


def pooled_shares(episodes, eps_root):
    """Shares over all labelled turns of the episodes together (the "pooled" entry of reach_share.json)."""
    rows = []
    for e in episodes:
        with open(os.path.join(eps_root, e["cve"], "labels.jsonl")) as f:
            rows += [json.loads(line) for line in f if line.strip()]
    return episode_shares(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run_dir")
    ap.add_argument("--model", default=MODEL)
    ap.add_argument("--dry", action="store_true", help="extract + rule-tag only, no Gemini")
    ap.add_argument("--workers", type=int, default=16,
                    help="parallel Gemini-judge workers (one episode per worker); raise to saturate the rate limit")
    ap.add_argument("--only", nargs="+", help="label only these episodes (ids); reach_share.json is not written")
    args = ap.parse_args()

    if not args.dry:
        key = os.environ.pop("GEMINI_API_KEY", "") or os.environ.get("GOOGLE_API_KEY", "")
        os.environ.pop("GOOGLE_API_KEY", None)
        if not key:
            sys.exit("set GEMINI_API_KEY (or GOOGLE_API_KEY) in the environment")
        import google.generativeai as genai
        genai.configure(api_key=key)
        del key

    eps_root = os.path.join(args.run_dir, "episodes")
    ep_dirs = sorted(os.path.join(eps_root, d) for d in os.listdir(eps_root)
                     if os.path.isdir(os.path.join(eps_root, d)) and (not args.only or d in args.only))
    if args.dry:
        for ep in ep_dirs:
            process_episode(ep, args.model, True)
        return

    episodes = []
    plock = threading.Lock()
    done = [0]

    def run_one(ep):
        name = os.path.basename(ep.rstrip("/"))
        try:
            r = process_episode(ep, args.model, False)
        except Exception as e:  # noqa: BLE001 - one bad episode must not abort the parallel run
            with plock:
                done[0] += 1
                print(f"[{done[0]}/{len(ep_dirs)}] {name:12s} ERROR {type(e).__name__}: {str(e)[:160]}")
            return
        if r:
            h = r["shares_hybrid"]
            with plock:
                episodes.append(r)
                done[0] += 1
                print(f"[{done[0]}/{len(ep_dirs)}] {r['cve']:12s} {r['project']:9s} {r['outcome']:11s} "
                      f"turns={r['turns']:3d} | narrow turns={h['turns']['reach_narrow']:.2f} "
                      f"tok={h['output_tokens']['reach_narrow']:.2f} think={h['thinking_tokens']['reach_narrow']:.2f} "
                      f"| broad turns={h['turns']['reach_broad']:.2f} | rule~judge "
                      f"{r['tool_only_agreement'][0]}/{r['tool_only_agreement'][1]}")
                sys.stdout.flush()

    with ThreadPoolExecutor(max_workers=args.workers) as ex:
        list(ex.map(run_one, ep_dirs))
    if not episodes:
        return

    # study-level: mean share + bootstrap CI across episodes, per weight, narrow & broad
    agg = {"n_episodes": len(episodes), "model": args.model, "weights": {}}
    for w in WEIGHTS:
        agg["weights"][w] = {}
        for tier in ("reach_narrow", "reach_broad"):
            vals = [e["shares_hybrid"][w][tier] for e in episodes]
            agg["weights"][w][tier] = {
                "mean": round(sum(vals) / len(vals), 4),
                "ci95": bootstrap_ci(vals), "per_episode": vals}
        cats = {}
        for c in CATS.values():
            vals = [e["shares_hybrid"][w]["by_category"][c] for e in episodes]
            cats[c] = round(sum(vals) / len(vals), 4)
        agg["weights"][w]["by_category_mean"] = cats
    ta = [e["tool_only_agreement"] for e in episodes]
    agg["tool_only_agreement"] = [sum(x[0] for x in ta), sum(x[1] for x in ta)]
    agg["pooled"] = pooled_shares(episodes, eps_root)
    out = {"episodes": episodes, "aggregate": agg}
    outp = os.path.join(args.run_dir, "reach_share.json")
    if not args.only:
        json.dump(out, open(outp, "w"), indent=1)

    print("\n=== REACHABILITY SHARE (hybrid labels; n=%d episodes, %d turns) ==="
          % (len(episodes), sum(e["turns"] for e in episodes)))
    for w in WEIGHTS:
        nn = agg["weights"][w]["reach_narrow"]
        bb = agg["weights"][w]["reach_broad"]
        print(f"  by {w:15s}: narrow {nn['mean']*100:4.1f}% (95% CI {nn['ci95']})  broad {bb['mean']*100:4.1f}%")
    print(f"  rule~judge agreement on tool-only turns: {agg['tool_only_agreement'][0]}/{agg['tool_only_agreement'][1]}")
    cols = [1, 2, 4, 3, 5, 6]
    print("\n  share of reasoning (thinking) tokens, %: " + " | ".join(CATS[c] for c in cols) + " | broad")
    w = agg["weights"]["thinking_tokens"]
    print("    per task (mean): " + " | ".join(f"{100 * w['by_category_mean'][CATS[c]]:.1f}" for c in cols)
          + f" | {100 * w['reach_broad']['mean']:.1f}")
    p = agg["pooled"]["thinking_tokens"]
    print("    pooled         : " + " | ".join(f"{100 * p['by_category'][CATS[c]]:.1f}" for c in cols)
          + f" | {100 * p['reach_broad']:.1f}")
    if not args.only:
        print(f"  -> {outp}")


if __name__ == "__main__":
    main()
