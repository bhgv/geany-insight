#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tag-record line endings must not leak into decoded metadata."""
import pathlib
import subprocess
import sys

printer = pathlib.Path(__file__).resolve().parents[2] / "scripts" / "print-tags.py"
source = b"# format=tagmanager\nfoo\xcc16\xcd(int value)\xcfvoid\nbar\xcc16384\xcfint\n"

def render(data):
    return subprocess.run([sys.executable, str(printer)], input=data,
                          stdout=subprocess.PIPE, check=True).stdout

lf = render(source)
crlf = render(source.replace(b"\n", b"\r\n"))
assert lf == crlf, "CRLF record terminators leaked into tag fields"
assert b"function:   void foo(int value)\n" in lf
assert render(source.replace(b"\xcfvoid", b"\xcflong")) != lf, "real type changes were hidden"
print("Tag printer: LF/CRLF records agree; real type changes remain visible")
