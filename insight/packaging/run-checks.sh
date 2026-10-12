#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
for name in runtime index document snapshot commands editor_api events context occurrences; do
    "$root/checks/${name}_test"
done
