#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile production Selection.cxx while making any charconv include fatal."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="selection-parse-", dir=root) as directory:
    work = Path(directory)
    (work / "charconv").write_text('#error "This test requires building without <charconv>"\n')
    executable = work / ("selection-parse.exe" if os.name == "nt" else "selection-parse")
    command = shlex.split(os.environ.get("CXX", "c++"))
    command += ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-I", str(work)]
    if os.environ.get("SANITIZE"):
        command += ["-fsanitize=" + os.environ["SANITIZE"], "-fno-sanitize-recover=all"]
    command += [str(root / "insight/tests/selection_parse_test.cxx"), "-o", str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
