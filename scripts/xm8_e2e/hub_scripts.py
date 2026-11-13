"""Print thin VMPal host-script definitions; no registration side effects."""
import json
from pathlib import Path
import shlex
import sys

ROOT = Path(__file__).resolve().parents[2]


def definitions(config, python=sys.executable):
    for target in ("ubuntu", "fedora", "windows", "macos", "android21", "android36", "android37"):
        for suite, title, color in (("basic", "基本操作", "green"), ("ra", "RAオンライン", "blue"),
                                     ("ra-network", "RA通信切断・復帰", "orange")):
            yield {"name":f"XM8M {target} {title}", "host":True, "language":"shell", "timeout":1800,
                   "color":color, "text":"#!/bin/sh\nexec " + shlex.join([
                       python, str(ROOT / "scripts/e2e.py"), "run", "--config", str(config),
                       "--target", target, "--suite", suite]) + "\n"}


if __name__ == "__main__":
    print(json.dumps(list(definitions(Path(sys.argv[1]).resolve())), ensure_ascii=False, indent=2))
