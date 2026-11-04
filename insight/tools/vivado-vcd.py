#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Render selected digital VCD signals and link them to slang declarations."""
import argparse
import html
import json
import re
import subprocess
from pathlib import Path
from itertools import islice

MAX_EVENTS = 20000


def tokens(path):
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            yield from line.split()


def read_vcd(path, names):
    words = iter(tokens(path))
    scopes, signals, aliases = [], {}, {}
    timescale = "VCD ticks"
    for token in words:
        if not token.startswith("$"):
            raise ValueError("Invalid VCD header")
        fields = []
        for word in words:
            if word == "$end":
                break
            fields.append(word)
        else:
            raise ValueError("Unterminated VCD header directive")
        if token == "$scope" and len(fields) == 2:
            scopes.append(fields[1])
        elif token == "$upscope" and scopes:
            scopes.pop()
        elif token == "$timescale":
            timescale = " ".join(fields)
        elif token == "$var" and len(fields) >= 4:
            name = ".".join(scopes + [fields[3]])
            if name in names:
                if name in signals:
                    raise ValueError("Ambiguous VCD name: " + name)
                width = int(fields[1])
                if not 1 <= width <= 4096:
                    raise ValueError("Unsupported signal width: " + name)
                code = fields[2]
                if code in aliases and aliases[code][0] != width:
                    raise ValueError("Inconsistent VCD alias width: " + name)
                signals[name] = aliases.setdefault(code, (width, []))
        elif token == "$enddefinitions":
            break
    else:
        raise ValueError("Missing VCD enddefinitions")
    missing = set(names) - signals.keys()
    if missing:
        raise ValueError("Signals not found: " + ", ".join(sorted(missing)))

    time, events = 0, 0
    for token in words:
        if token.startswith("#"):
            new_time = int(token[1:])
            if new_time < time:
                raise ValueError("VCD timestamps must be nondecreasing")
            time = new_time
            continue
        if token in ("$dumpvars", "$dumpall", "$dumpon", "$dumpoff", "$end"):
            continue
        if token.startswith("$"):
            for word in words:
                if word == "$end":
                    break
            continue
        if token[0] in "bBrRsS":
            code = next(words, None)
            value = token[1:].lower()
        else:
            code, value = token[1:], token[0].lower()
        if code not in aliases:
            continue
        width, changes = aliases[code]
        if not re.fullmatch(r"[01xz]+", value) or len(value) > width or token[0] in "rRsS":
            raise ValueError("Unsupported VCD value for " + code)
        value = value.rjust(width, value[0] if value[0] in "xz" else "0")
        if changes and changes[-1][0] == time:
            changes[-1] = (time, value)
        elif not changes or changes[-1][1] != value:
            events += 1
            if events > MAX_EVENTS:
                raise ValueError("Too many transitions. Export a shorter interval.")
            changes.append((time, value))
    return signals, time, timescale


def declaration(service, sources, name):
    if not service:
        return ""
    try:
        result = subprocess.run(
            [str(service), "--lookup", name, "--"] + sources,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True, timeout=30)
        records = json.loads(result.stdout)["records"]
        if len(records) != 1:
            return "<p>HDL declaration unavailable.</p>"
        loc = records[0]["declaration"]
        path = Path(loc["path"]).resolve()
        line = int(loc["line"])
        if line < 1:
            raise ValueError("Invalid declaration line")
        with path.open(encoding="utf-8", errors="replace") as source:
            excerpt = "".join(islice(source, max(0, line - 4), line + 3))
        return ('<details><summary>' + html.escape(str(path)) + ':' + str(line) +
                '</summary><a href="' + html.escape(path.as_uri(), quote=True) +
                '">Open source file</a><pre>' + html.escape(excerpt) + '</pre></details>')
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError):
        return "<p>HDL declaration unavailable.</p>"


def render(signals, end, timescale, service, sources, strip_scope):
    parts = ['<!doctype html><meta charset="utf-8"><title>VCD waveforms</title>',
             '<style>body{font:16px system-ui;margin:2rem}svg{width:100%;min-width:900px}'
             '.wave{overflow-x:auto}a{color:#1462ad}pre{white-space:pre-wrap}</style>',
             '<h1>VCD waveforms</h1><p>Tick scale: ' + html.escape(timescale) +
             '. Drag the time slider to inspect values.</p>',
             '<label>Time <input id="time" type="range" min="0" max="' +
             str(min(end, 9007199254740991)) + '"> <output id="tick"></output></label> '
             '<button id="prev">Previous transition</button> '
             '<button id="next">Next transition</button>']
    for index, (name, (width, events)) in enumerate(signals.items()):
        key = "signal" + str(index)
        parts.append('<section><h2>' + html.escape(name) + ' [' + str(width) +
                     ']</h2><output id="' + key + '"></output><div class="wave">'
                     '<svg viewBox="0 0 1000 52" role="img" aria-label="Signal timeline">')
        intervals = events if events and events[0][0] == 0 else [(0, "x" * width)] + events
        for i, (time, value) in enumerate(intervals):
            next_time = intervals[i + 1][0] if i + 1 < len(intervals) else end
            x = 998 * time / max(end, 1) + 1
            length = max(1, 998 * (next_time - time) / max(end, 1))
            color = "#8e5100" if "x" in value or "z" in value else "#176c8c"
            y = 4 if width > 1 or value == "1" else 27
            height = 22 if width > 1 else 8
            parts.append('<rect x="%.3f" y="%d" width="%.3f" height="%d" fill="%s">'
                         '<title>%d: %s</title></rect>' % (x, y, length, height, color, time, value))
        parts.append('</svg></div>')
        scope = name.split(".")[strip_scope:]
        parts.append(declaration(service, sources, ".".join(scope)) if scope else "")
        parts.append('</section>')
    data = [[["%d" % t, v] for t, v in events] for _, events in signals.values()]
    parts.append('<script>const traces=' + json.dumps(data) + r''';
const slider=document.getElementById("time");
function show(){
  const time=BigInt(slider.value);
  document.getElementById("tick").value=slider.value;
  traces.forEach((changes,i)=>{
    let lo=0,hi=changes.length;
    while(lo<hi){const mid=Math.floor((lo+hi)/2);
      if(BigInt(changes[mid][0])<=time)lo=mid+1;else hi=mid;}
    document.getElementById("signal"+i).value=lo?changes[lo-1][1]:"unknown";
  });
}
const edges=[...new Set(traces.flatMap(changes=>changes.map(change=>Number(change[0]))))].sort((a,b)=>a-b);
function move(forward){
  const now=Number(slider.value);
  const edge=forward?edges.find(t=>t>now):edges.slice().reverse().find(t=>t<now);
  if(edge!==undefined){slider.value=edge;show();}
}
document.getElementById("prev").onclick=()=>move(false);
document.getElementById("next").onclick=()=>move(true);
slider.value=0; slider.addEventListener("input",show); show();
</script>''')
    return "\n".join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vcd", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--signal", action="append", required=True)
    parser.add_argument("--slang", type=Path)
    parser.add_argument("--source", action="append", default=[])
    parser.add_argument("--strip-scope", type=int, default=0,
                        help="Number of outer VCD scopes to drop for HDL lookup")
    args = parser.parse_args()
    if bool(args.slang) != bool(args.source) or args.strip_scope < 0:
        parser.error("Provide --slang and --source together and a nonnegative --strip-scope")
    if len(set(args.signal)) > 16:
        parser.error("Select at most 16 signals per report")
    try:
        signals, end, timescale = read_vcd(args.vcd, args.signal)
        if end > 9007199254740991:
            raise ValueError("Time range exceeds the browser slider's integer precision")
        result = render(signals, end, timescale, args.slang, args.source, args.strip_scope)
        with args.output.open("x", encoding="utf-8") as output:
            output.write(result)
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
