#!/usr/bin/env python3
"""Derive a reach target (file, line, func, sanitizer-pattern) from an ARVO -vul image.

Reproduces the baked-in crash with `arvo` and parses the sanitizer report:
  * pattern = the "SUMMARY: <Sanitizer>: <type>" class (matches const.ARVO_TARGETS form);
  * (file, line, func) = the TOPMOST stack frame inside the project source tree
    (/src/<project>/...), skipping libc, the sanitizer runtime (/src/llvm-project/...),
    C++ stdlib headers (/usr/...), and the like.
The target line is where build_arvo_docker.py inserts the "HIT TARGET" marker.

Validation mode (--validate): run over the known const.ARVO_TARGETS ids using their
n132/arvo:<id>-vul images (docker pulls a missing one) and report how often the auto-derived target matches
the curated one -- this certifies the parser before it is pointed at CyberGym ids.

Usage:
  python exp_reach/derive_target.py --validate [--sample N] [--project P]
  python exp_reach/derive_target.py --id <arvo_id> --project <project>   # derive one
"""
import argparse
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
import const  # noqa: E402

WORKDIR_MAP = {"serenity": "/src", "assimp": "/src/assimp", "openh264": "/src/openh264",
               "opensc": "/src/opensc", "wasm3": "/src/wasm3"}
# System / toolchain / fuzzer-runtime frames to skip. Prefixes are ANCHORED (so a project
# path like /src/ndpi/src/lib/... is NOT mistaken for the system /lib/); substrings catch the
# sanitizer runtime and the libFuzzer/AFL driver wherever they live.
SYS_PREFIX = ("/usr/", "/lib/", "/lib64/", "/build/")
SYS_SUBSTR = ("/llvm-project/", "/llvm/projects/", "compiler-rt", "/libfuzzer/",
              "/afl/", "afl_driver", "FuzzerMain", "FuzzerLoop", "FuzzerDriver")
FRAME = re.compile(r"#\d+\s+0x[0-9a-fA-F]+\s+in\s+(.*?)\s+(/\S+?):(\d+)(?::\d+)?\s*$")
SUMMARY = re.compile(r"SUMMARY:\s+(\w*Sanitizer):\s+([A-Za-z0-9_-]+)")
ERRLINE = re.compile(r"ERROR:\s+(\w*Sanitizer):\s+([A-Za-z0-9_-]+)")
OUTBIN = re.compile(r"/out/([A-Za-z0-9_.\-]+?)\+0x")   # from a stack frame: /out/<bin>+0x...
OUTBIN2 = re.compile(r"/out/([A-Za-z0-9_][A-Za-z0-9_.\-]*)")  # looser fallback (no +0x suffix)


def reproduce(image, timeout=90):
    """Run the image's baked-in crash repro; return combined output (or '' on failure)."""
    # crash dumps can carry raw non-UTF-8 bytes, so decode leniently
    r = subprocess.run(["docker", "run", "--rm", "--network", "none", image, "arvo"],
                       capture_output=True, text=True, errors="replace", timeout=timeout)
    return (r.stdout or "") + "\n" + (r.stderr or "")


def src_prefixes(project):
    p = WORKDIR_MAP.get(project, f"/src/{project}")
    pre = [p.rstrip("/") + "/"]
    if p != "/src":
        pre.append(f"/src/{project}/")
    else:
        pre.append("/src/")
    return pre


def _is_system(path):
    return path.startswith(SYS_PREFIX) or any(s in path for s in SYS_SUBSTR)


def _is_harness(path, func):
    base = path.rsplit("/", 1)[-1].lower()
    return ("/fuzz/" in path or func.strip() == "LLVMFuzzerTestOneInput"
            or base.startswith("fuzz") or "fuzzer" in base)


def parse(output, project):
    """-> dict(pattern, file, line, func, ...). Target = topmost project-source frame that is
    not system/toolchain and not the fuzz harness (the harness is used only as a last resort)."""
    m = SUMMARY.search(output) or ERRLINE.search(output)
    pattern = f"{m.group(1)}: {m.group(2)}" if m else None
    fb = OUTBIN.search(output) or OUTBIN2.search(output)
    fuzz_target = fb.group(1) if fb else None
    pre = src_prefixes(project)
    frames = []
    for line in output.splitlines():
        fm = FRAME.search(line)
        if not fm:
            continue
        func, path, ln = fm.group(1).strip(), fm.group(2), int(fm.group(3))
        if _is_system(path) or not any(path.startswith(p) for p in pre):
            continue
        rel = path
        for p in pre:
            if path.startswith(p):
                rel = path[len(p):]
                break
        frames.append({"pattern": pattern, "file": rel, "line": ln, "func": func, "abs": path,
                       "harness": _is_harness(path, func)})
    if not frames:
        return {"pattern": pattern, "file": None, "line": None, "func": None, "abs": None,
                "fuzz_target": fuzz_target, "harness_only": False}
    nonh = [f for f in frames if not f["harness"]]
    pick = nonh[0] if nonh else frames[0]
    pick["fuzz_target"] = fuzz_target
    pick["harness_only"] = not nonh      # flag targets whose only project frame is the harness
    return pick


def const_target(cve):
    for proj, d in const.ARVO_TARGETS.items():
        if cve in d:
            return proj, d[cve]  # (file, line, func, pattern)
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--validate", action="store_true")
    ap.add_argument("--sample", type=int, default=0, help="limit validation to N ids per project")
    ap.add_argument("--project")
    ap.add_argument("--id")
    ap.add_argument("--repo", default="n132/arvo")
    ap.add_argument("--out")
    args = ap.parse_args()

    if args.id:
        out = reproduce(f"{args.repo}:{args.id}-vul")
        print(json.dumps(parse(out, args.project), indent=1))
        return

    if not args.validate:
        ap.error("pass --validate or --id")

    ids = []
    for proj, d in const.ARVO_TARGETS.items():
        if args.project and proj != args.project:
            continue
        pids = sorted(d)
        if args.sample:
            pids = pids[: args.sample]
        ids += [(c, proj) for c in pids]

    rows = []
    agg = {"n": 0, "pattern_ok": 0, "line_ok": 0, "func_ok": 0, "file_ok": 0, "all_ok": 0, "parse_fail": 0}
    for cve, proj in ids:
        img = f"{args.repo}:{cve}-vul"
        try:
            out = reproduce(img)
        except subprocess.TimeoutExpired:
            out = ""
        got = parse(out, proj)
        _, known = const_target(cve)
        kfile, kline, kfunc, kpat = known
        file_ok = bool(got["file"]) and (got["file"].endswith(kfile) or kfile.endswith(got["file"]))
        line_ok = got["line"] == kline
        # function: compare the bare symbol name (const stores full C++ signatures)
        kname = re.split(r"[(<]", kfunc)[0].split("::")[-1].split()[-1] if kfunc else ""
        gname = re.split(r"[(<]", got["func"] or "")[0].split("::")[-1].split()[-1] if got["func"] else ""
        func_ok = bool(kname) and kname == gname
        # const stores the pattern inconsistently (bare "undefined-behavior" vs
        # "AddressSanitizer: SEGV"); compare the sanitizer type after the last ": ".
        pat_ok = bool(got["pattern"]) and got["pattern"].split(": ")[-1] == kpat.split(": ")[-1]
        allok = file_ok and line_ok and pat_ok
        agg["n"] += 1
        agg["pattern_ok"] += pat_ok; agg["line_ok"] += line_ok; agg["func_ok"] += func_ok
        agg["file_ok"] += file_ok; agg["all_ok"] += allok
        if got["file"] is None:
            agg["parse_fail"] += 1
        rows.append({"cve": cve, "project": proj, "match": {"file": file_ok, "line": line_ok,
                     "func": func_ok, "pattern": pat_ok, "all": allok},
                     "derived": {k: got[k] for k in ("file", "line", "func", "pattern")},
                     "known": {"file": kfile, "line": kline, "func": kfunc, "pattern": kpat}})
        mk = "OK  " if allok else ("P/L/F/Pat=%d%d%d%d" % (file_ok, line_ok, func_ok, pat_ok))
        print(f"{cve:12s} {proj:9s} {mk}  derived {got['file']}:{got['line']} [{got['pattern']}]  "
              f"vs known {kfile}:{kline} [{kpat}]")
        sys.stdout.flush()

    print("\n=== PARSER VALIDATION vs const.ARVO_TARGETS ===")
    for k in ("n", "all_ok", "file_ok", "line_ok", "func_ok", "pattern_ok", "parse_fail"):
        print(f"  {k:12s}: {agg[k]}")
    if args.out:
        json.dump({"aggregate": agg, "rows": rows}, open(args.out, "w"), indent=1)
        print("  ->", args.out)


if __name__ == "__main__":
    main()
