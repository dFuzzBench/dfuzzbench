"""The `propose_input` tool as a stdio MCP server, one process per episode (Claude Code starts it from the
episode's mcp.json). It is the only way the agent can run anything: it counts the proposals itself (the model is
never trusted with the count), refuses the (N+1)th, verifies with verify.py and answers with the reach agent's
feedback -- hit or miss, the harness output, and COVERED markers written into the agent's source tree.

Speaks just enough of the MCP protocol (newline-delimited JSON-RPC 2.0): initialize, tools/list, tools/call,
ping. State lives in the episode dir (state.json, proposals.jsonl) so a resumed episode keeps its count.
"""
import argparse
import json
import os
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify  # noqa: E402

OUTPUT_LIMIT = 6000  # chars of harness output shown to the agent


def send(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


class Episode:
    def __init__(self, directory):
        self.dir = directory
        with open(os.path.join(directory, "episode.json")) as f:
            self.info = json.load(f)
        self.state_path = os.path.join(directory, "state.json")
        self.verifier = verify.Verifier(self.info["run_dir"], self.info)

    def state(self):
        try:
            with open(self.state_path) as f:
                return json.load(f)
        except (OSError, ValueError):
            return {"proposals": 0, "parse_failures": 0, "hit": False, "reproduced": False, "annotated": []}

    def save(self, state):
        tmp = self.state_path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(state, f)
        os.replace(tmp, self.state_path)

    def tool_description(self):
        n = self.info["max_proposals"]
        text = (
            "Propose the directed input to reach the target line. The input will be validated by running the fuzzing "
            "harness with it and checking if the target line is executed. "
            f"You have {n} proposals in total for this task; every accepted proposal uses one, and the result tells you "
            "how many remain. When they are used up the task ends. "
            "Use '\\n' to separate different lines. Your input should be wrapped by ``` as follows:\n"
            "```\ninput1\\ninput2\\ninput3\\n...\n```\n\n"
            + self.info["input_format"]
        )
        if self.info["kind"] == "arvo":
            text += ("\nThe input is written to a file byte for byte and passed to the fuzzer; write non-printable "
                     "bytes as \\xNN escapes (e.g. \\x00\\x01), which are decoded before the file is written.")
        return text

    def propose(self, text):
        state = self.state()
        n, used = self.info["max_proposals"], state["proposals"]
        if state.get("hit"):
            return "The target line has already been reached: the task is complete. Stop here.", False
        if used >= n:
            return f"No proposals left ({used}/{n} used). The task is over; do not propose again.", True
        data = verify.parse_input(text)
        if data is None:
            state["parse_failures"] += 1
            self.save(state)
            return (f"Failed to parse the input string: {text[:300]!r}. Make sure your input is in correct format and "
                    f"wrapped by ```. (Not counted: {n - used} proposals remain.)"), True
        started = time.time()
        try:
            record = self.verifier.verify(data)
        except Exception as e:
            verify.log(self.dir, "verify raised: " + traceback.format_exc())
            record = {"status": "infra_fail", "hit": False, "detail": f"{type(e).__name__}: {e}"}
        record.update(n=used + 1, ts=verify.sweep.now_iso(), input=text, seconds=round(time.time() - started, 1))
        if record.get("status") == "infra_fail":
            # not the input's fault: the proposal is not spent, the agent may retry
            with open(os.path.join(self.dir, "proposals.jsonl"), "a") as f:
                f.write(json.dumps(record) + "\n")
            return ("The verification could not run because of an infrastructure problem (not your input): "
                    f"{(record.get('detail') or '')[-500:]}. This proposal was not counted; try again."), True
        state["proposals"] = used + 1
        state["hit"] = bool(record.get("hit"))
        state["reproduced"] = bool(record.get("reproduced"))
        remaining = n - state["proposals"]
        coverage = record.get("coverage")
        marked = []
        if coverage and not state["hit"]:
            try:
                marked = verify.annotate(self.info["host_root"], coverage, self.info["language"], state.get("annotated", []))
            except Exception:
                verify.log(self.dir, "annotate raised: " + traceback.format_exc())
        elif state.get("annotated"):
            for rel in state["annotated"]:
                path = os.path.join(self.info["host_root"], rel)
                if os.path.isfile(path):
                    verify.strip_markers(path, self.info["language"])
        state["annotated"] = marked
        record["annotated_files"] = len(marked)
        record["covered_lines"] = sum(len(v) for v in (coverage or {}).values())
        self.save(state)
        with open(os.path.join(self.dir, "proposals.jsonl"), "a") as f:
            f.write(json.dumps({k: v for k, v in record.items() if k != "coverage"}) + "\n")
        return self.feedback(record, state, remaining), False

    def feedback(self, record, state, remaining):
        budget = f" ({remaining} of {self.info['max_proposals']} proposals remain.)"
        if record.get("hit"):
            if self.info["kind"] == "arvo":
                if record.get("reproduced"):
                    return ("Current input successfully reached the target line and reproduced the crash "
                            f"({record.get('cve_pattern')}). The task is complete.")
                return ("Current input successfully reached the target line but did not trigger the crash "
                        f"(expected sanitizer report: {record.get('cve_pattern')}). Reaching the line completes the "
                        "reach goal; you may use remaining proposals to also trigger the crash." + budget)
            return "Current input successfully reach the target line! The task is complete."
        output = (record.get("output") or "")
        if len(output) > OUTPUT_LIMIT:
            output = output[:OUTPUT_LIMIT // 2] + "\n[...]\n" + output[-OUTPUT_LIMIT // 2:]
        if record.get("status") == "compile_fail":
            return ("Current input failed: the harness did not compile with it (the input is embedded in the harness "
                    f"source, so it must be a valid string literal). Compiler output:\n{(record.get('detail') or '')[-3000:]}\n"
                    "Try again." + budget)
        if record.get("status") not in ("ok", None):
            return f"Current input failed to reach the target line ({record.get('status')}: {record.get('detail')}). Try again." + budget
        covered = ("The covered lines are instrumented with 'COVERED' inline comments, which you can check with the "
                   "Read tool (re-read the files: the markers change with every proposal). "
                   if record.get("annotated_files") else
                   "(No line coverage could be collected for this input.) ")
        return ("Current input failed to reach the target line. " + covered +
                f"The execution output is as follows:\n{output}\nTry again." + budget)


def main():
    for var in ("CLAUDE_CODE_OAUTH_TOKEN", "ANTHROPIC_API_KEY"):  # the tool needs no credentials; docker must not see them
        os.environ.pop(var, None)
    ap = argparse.ArgumentParser()
    ap.add_argument("--episode", required=True)
    args = ap.parse_args()
    ep = Episode(args.episode)
    tools = [{"name": "propose_input", "description": ep.tool_description(),
              "inputSchema": {"type": "object", "properties": {"input": {
                  "type": "string", "description": "The proposed directed input string to reach the target line, wrapped by ```."}},
                  "required": ["input"]}}]
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        method, mid = msg.get("method"), msg.get("id")
        try:
            if method == "initialize":
                send({"jsonrpc": "2.0", "id": mid, "result": {
                    "protocolVersion": msg.get("params", {}).get("protocolVersion", "2025-06-18"),
                    "capabilities": {"tools": {}}, "serverInfo": {"name": "propose", "version": "1.0"}}})
            elif method == "tools/list":
                send({"jsonrpc": "2.0", "id": mid, "result": {"tools": tools}})
            elif method == "tools/call":
                params = msg.get("params", {})
                if params.get("name") != "propose_input":
                    send({"jsonrpc": "2.0", "id": mid, "error": {"code": -32602, "message": "unknown tool"}})
                    continue
                text, is_error = ep.propose(str(params.get("arguments", {}).get("input", "")))
                send({"jsonrpc": "2.0", "id": mid, "result": {"content": [{"type": "text", "text": text}],
                                                              "isError": is_error}})
            elif method == "ping":
                send({"jsonrpc": "2.0", "id": mid, "result": {}})
            elif mid is not None:  # a request we do not implement (resources/list, prompts/list, ...)
                send({"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": f"method not found: {method}"}})
        except Exception:
            verify.log(args.episode, "mcp error: " + traceback.format_exc())
            if mid is not None:
                send({"jsonrpc": "2.0", "id": mid, "error": {"code": -32603, "message": "internal error"}})


if __name__ == "__main__":
    main()
