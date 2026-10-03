"""Line coverage of one run of an instrumented Python harness, without libFuzzer: import the harness as a
module (its `if __name__ == "__main__"` guard keeps atheris.Fuzz() out) and call TestOneInput once. The
instrumented harness ignores the argument and uses the input written into it. Executed lines of files under
COV_ROOT (the project checkout, put first on sys.path so the checkout, not the pip-installed copy, is what
runs) go to COV_OUT as {file: [lines]}.

    COV_ROOT=/src/bleach COV_OUT=/out/coverage.json python3 tracer.py /src/fuzzer_instrumented.py
"""
import json
import os
import runpy
import signal
import sys
import threading

ROOT = os.environ.get("COV_ROOT", "/src").rstrip("/") + "/"
OUT = os.environ.get("COV_OUT", "/out/coverage.json")
lines = {}


def tracer(frame, event, arg):
    if event == "call":
        return tracer if frame.f_code.co_filename.startswith(ROOT) or "site-packages" in frame.f_code.co_filename else None
    if event == "line":
        lines.setdefault(frame.f_code.co_filename, set()).add(frame.f_lineno)
    return tracer


def dump(*_):
    with open(OUT, "w") as f:
        json.dump({k: sorted(v) for k, v in lines.items()}, f)


def main():
    harness = sys.argv[1]
    sys.path.insert(0, ROOT.rstrip("/"))
    sys.argv = [harness]
    signal.signal(signal.SIGTERM, lambda *a: (dump(), os._exit(143)))
    module = runpy.run_path(harness, run_name="__coverage__")
    sys.settrace(tracer)
    threading.settrace(tracer)
    try:
        module["TestOneInput"](b"")
    except SystemExit:
        pass
    except Exception as e:  # the input crashed the harness: still coverage
        print(f"[tracer] TestOneInput raised {type(e).__name__}: {e}", file=sys.stderr)
    finally:
        sys.settrace(None)
        dump()


if __name__ == "__main__":
    main()
