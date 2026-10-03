#!/usr/bin/env python3
"""Build a derived HIT-TARGET marker image for a target whose crash line was auto-derived by
derive_target.py (the CyberGym path; build_arvo_docker.py does the same for curated ARVO
targets from a hand-written target.patch).

Steps, per arvo id:
  1. reproduce the crash on n132/arvo:<id>-vul and derive (file, line, func, pattern);
  2. in a container from that image, insert `fprintf(stderr, "HIT TARGET\\n");` just before the
     derived line (matching indentation) and a stdio include at the top of the file;
  3. `arvo compile`; `docker commit` to <ns>/dfuzzbench-arvo:<id>;
  4. verify on the image's baked PoC: "HIT TARGET" in output = reached, and the sanitizer
     pattern still present = reproduced.

Usage: python exp_reach/build_marker.py --id <arvo_id> --project <project> [--ns local]
"""
import argparse
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
from exp_reach.derive_target import reproduce, parse, WORKDIR_MAP  # noqa: E402

MARKER = 'fprintf(stderr, "HIT TARGET\\n"); // reach-marker'


def dexec(container, cmd, **kw):
    return subprocess.run(["docker", "exec", container, "bash", "-lc", cmd],
                          capture_output=True, text=True, errors="replace", **kw)


def build(arvo_id, project, repo, ns, keep=False):
    vul = f"{repo}:{arvo_id}-vul"
    tgt = parse(reproduce(vul), project)
    if not tgt.get("file"):
        return {"ok": False, "stage": "derive", "err": "no target frame"}
    abspath, line = tgt["abs"], tgt["line"]
    workdir = WORKDIR_MAP.get(project, f"/src/{project}")
    dst = f"{ns}/dfuzzbench-arvo:{arvo_id}"
    container = f"mk_{arvo_id}"
    subprocess.run(["docker", "rm", "-f", container], capture_output=True)
    # in-container python: splice the marker in at the derived line, add a stdio include
    # Insert the marker at the START of the statement that contains the crash line, so we never
    # split a multi-line expression (e.g. a crash line inside a multi-line if-condition). Walk
    # back from the target line to the nearest statement boundary (;, {, }, blank, or comment).
    pyedit = (
        f"p={abspath!r}; ln={line}\n"
        'L=open(p,encoding="utf-8",errors="replace").read().splitlines(keepends=True)\n'
        'def boundary(s):\n'
        '    s=s.rstrip()\n'
        '    return (s=="" or s.endswith((";","{","}")) or s.endswith("*/")\n'
        '            or s.lstrip().startswith(("//","/*","#")))\n'
        'j=ln-1\n'
        'while j>0 and not boundary(L[j-1]): j-=1\n'
        'tl=L[j]; indent=tl[:len(tl)-len(tl.lstrip())]\n'
        f'L.insert(j, indent+{MARKER!r}+"\\n")\n'
        'head="".join(L[:60])\n'
        'if "#include <stdio.h>" not in head: L.insert(0,"#include <stdio.h>\\n")\n'
        'open(p,"w",encoding="utf-8").write("".join(L))\n'
        'print("patched",p,"stmt-start",j+1,"target",ln)\n'
    )
    try:
        r = subprocess.run(["docker", "run", "-d", "--name", container, vul, "sleep", "infinity"],
                           capture_output=True, text=True)
        if r.returncode:
            return {"ok": False, "stage": "start", "err": r.stderr[-300:]}
        r = dexec(container, "python3 - <<'PYEOF'\n" + pyedit + "PYEOF")
        if r.returncode or "patched" not in r.stdout:
            return {"ok": False, "stage": "patch", "err": (r.stdout + r.stderr)[-400:], "target": tgt}
        # `arvo compile`'s working dir varies per image (some build.sh use paths relative to
        # the project dir, some to $SRC=/src). Try the project dir first, then /src.
        cands = []
        for w in (workdir, f"/src/{project}", "/src"):
            if w and w not in cands:
                cands.append(w)
        cerr = ""
        for i, w in enumerate(cands):
            r = dexec(container, f"cd {w} && arvo compile", timeout=900)
            if r.returncode == 0:
                break
            cerr = r.stderr[-500:]
        else:
            return {"ok": False, "stage": "compile", "err": cerr, "tried": cands, "target": tgt}
        r = subprocess.run(["docker", "commit", container, dst], capture_output=True, text=True)
        if r.returncode:
            return {"ok": False, "stage": "commit", "err": r.stderr[-300:], "target": tgt}
    finally:
        if not keep:
            subprocess.run(["docker", "rm", "-f", container], capture_output=True)
    # verify on the baked PoC
    v = subprocess.run(["docker", "run", "--rm", "--pull", "never", "--network", "none", dst, "arvo"],
                       capture_output=True, text=True, errors="replace", timeout=180)
    out = (v.stdout or "") + (v.stderr or "")
    reached = "HIT TARGET" in out
    pat = (tgt["pattern"] or "").split(": ")[-1]
    reproduced = reached and bool(pat) and pat in out
    return {"ok": True, "dst": dst, "target": tgt, "reached": reached, "reproduced": reproduced}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--id", required=True)
    ap.add_argument("--project", required=True)
    ap.add_argument("--repo", default="n132/arvo")
    ap.add_argument("--ns", default=os.environ.get("DFUZZ_DOCKER_NS", "local"))
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()
    res = build(args.id, args.project, args.repo, args.ns, args.keep)
    print(json.dumps(res, indent=1))
    sys.exit(0 if res.get("ok") and res.get("reached") else 1)


if __name__ == "__main__":
    main()
