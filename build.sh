#!/bin/sh
# One command for compiler preparation, fresh configuration and editor build.
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$source_dir/insight/tools/build_editor.py" "$@"
