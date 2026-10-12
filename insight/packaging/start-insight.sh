#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export GEANY_INSIGHT_PREFIX="$root"
export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
mkdir -p "$root/profile"
if [ ! -e "$root/profile/geany.conf" ]; then
    # Escape GKeyFile string-list separators and backslashes in the path.
    plugin=$(printf '%s' "$root/lib/geany/insight_lua.so" | sed 's/\\/\\\\/g; s/;/\\;/g')
    printf '[plugins]\nload_plugins=true\nactive_plugins=%s;\n' "$plugin" > "$root/profile/geany.conf"
fi
exec "$root/bin/geany" --new-instance --config "$root/profile" "$@"
