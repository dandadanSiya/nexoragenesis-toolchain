#!/usr/bin/env python3
"""Tiny constant preprocessor for NTASM sources.

NTASM v0 only accepts integer literals inside memory displacements, so the
game sources name their structure offsets with lines such as

    %define KART_X 0
    %define PLAYER KARTS + 0 * KART_SIZE

Every later whole-word occurrence of an upper-case name is replaced by its
integer value; `%text NAME words...` substitutes verbatim text (used for the
repeated effect and clobber sets). `%include "file"` inlines another source.
Nothing else is rewritten: the output is plain NTASM checked by the NTASM
compiler.
"""
import re
import sys
from pathlib import Path

NAME = re.compile(r"\b[A-Z][A-Z0-9_]*\b")


def expand(path, defines, out, depth=0, fixed=()):
    if depth > 8:
        raise SystemExit(f"{path}: include depth exceeded")
    for number, line in enumerate(Path(path).read_text().splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("%define"):
            parts = stripped.split(None, 2)
            if len(parts) != 3:
                raise SystemExit(f"{path}:{number}: %define NAME EXPR")
            expression = NAME.sub(lambda m: str(defines[m.group(0)])
                                  if m.group(0) in defines else m.group(0),
                                  parts[2].split("//")[0])
            if parts[1] in fixed:
                continue
            try:
                defines[parts[1]] = int(eval(expression, {"__builtins__": {}}))
            except Exception as error:  # noqa: BLE001 - report and stop
                raise SystemExit(f"{path}:{number}: bad expression: {error}")
            continue
        if stripped.startswith("%text"):
            parts = stripped.split(None, 2)
            if len(parts) != 3:
                raise SystemExit(f"{path}:{number}: %text NAME TEXT")
            defines[parts[1]] = parts[2]
            continue
        if stripped.startswith("%include"):
            target = stripped.split(None, 1)[1].strip().strip('"')
            expand(Path(path).parent / target, defines, out, depth + 1, fixed)
            continue
        out.append(NAME.sub(lambda m: str(defines[m.group(0)])
                            if m.group(0) in defines else m.group(0), line))


def main():
    if len(sys.argv) < 3:
        raise SystemExit("usage: ntpp.py INPUT.ntasm OUTPUT.ntasm [NAME=VALUE...]")
    defines = {}
    for item in sys.argv[3:]:
        name, _, value = item.partition("=")
        defines[name] = int(value, 0)
    out = []
    expand(sys.argv[1], defines, out, 0, frozenset(defines))
    Path(sys.argv[2]).write_text("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
