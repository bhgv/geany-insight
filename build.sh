#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
driver="$source_dir/insight/tools/build_editor.py"
if [[ $# -eq 0 ]]; then
    set -- --backend meson --install
fi

for argument in "$@"; do
    case "$argument" in
        --plan|-h|--help)
            exec python3 "$driver" "$@"
            ;;
        --)
            break
            ;;
    esac
done

log_dir="$source_dir/_editor-build/logs"
mkdir -p -- "$log_dir"
log=$(mktemp "$log_dir/build-$(date +%Y%m%d-%H%M%S).XXXXXXXX")
printf '[Build log] %s\n' "$log" >&2
set +e
(
    printf '[Source revision] %s\n' "$(git -C "$source_dir" rev-parse HEAD 2>/dev/null || printf unknown)"
    python3 "$driver" "$@"
) 2>&1 | tee -- "$log"
statuses=("${PIPESTATUS[@]}")
set -e
if (( statuses[0] != 0 )); then
    printf 'Build failed. Build log: %s\n' "$log" >&2
    exit "${statuses[0]}"
fi
exit "${statuses[1]}"
