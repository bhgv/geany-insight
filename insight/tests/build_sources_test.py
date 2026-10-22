#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prevent Insight runtime sources disappearing from the Autotools link."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
meson = (root / "meson.build").read_text()
runtime = meson[meson.index("insight_lua_sources = ["):meson.index("dep_libgeany = declare_dependency")]
expected = set(re.findall(r"'((?:src/insight-|insight/)[^']+\.c)'", runtime))
am = (root / "src/Makefile.am").read_text()
blocks = re.findall(r"^libgeany_la_SOURCES\s*\+?=\s*((?:[^\n]*\\\n)*[^\n]*)", am, re.M)
actual = set()
for token in " ".join(blocks).replace("\\\n", " ").split():
    if token.endswith(".c"):
        actual.add(token[3:] if token.startswith("../") else "src/" + token)
missing = sorted(expected - actual)
if missing:
    raise SystemExit("Insight sources missing from src/Makefile.am: " + ", ".join(missing))
assert re.search(r"^bin_PROGRAMS\s*=.*\bgeany-insight-askpass\b", am, re.M)
assert "geany_insight_askpass_SOURCES = insight-askpass.c" in am
print("Autotools includes all %d Meson Insight runtime sources and the SSH askpass helper" % len(expected))
