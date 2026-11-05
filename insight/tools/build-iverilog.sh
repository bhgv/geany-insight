#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

cache_dir=$1
install_dir=$2
jobs=$3
version=$4
url=$5
sha=$6
executable_suffix=
configure_prefix=$install_dir
if command -v cygpath >/dev/null 2>&1; then
    cache_dir=$(cygpath -u "$cache_dir")
    install_dir=$(cygpath -u "$install_dir")
    executable_suffix=.exe
    configure_prefix=/geany-iverilog
fi
archive="$cache_dir/iverilog-$version.tar.gz"
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
bridge_patch="$script_dir/../patches/geany-vvp.patch"
stamp="$install_dir/share/geany-insight/iverilog-source.sha256"
work=$(mktemp -d "${TMPDIR:-/tmp}/geany-iverilog.XXXXXXXX")
download=
trap 'rm -rf -- "$work"; if [[ -n "$download" ]]; then rm -f -- "$download"; fi' EXIT

# Read the SHA-256 of a source or patch file.
# Читает SHA-256 файла исходников или патча.
file_digest() {
    local digest
    if command -v sha256sum >/dev/null 2>&1; then
        digest=$(sha256sum -- "$1")
    else
        digest=$(shasum -a 256 -- "$1")
    fi
    printf '%s\n' "${digest%% *}"
}

stamp_id="$sha $(file_digest "$bridge_patch")"

# Check that the cached source matches the pinned archive.
# Проверяет, что исходный архив соответствует закреплённой версии.
verify_archive() {
    [[ $(file_digest "$1") == "$sha" ]]
}

# Compile and run a small design using the installed executables.
# Компилирует и запускает небольшой проект установленными программами.
check_simulator() {
    local prefix=$1
    cat > "$work/geany_smoke.v" <<'VERILOG'
module geany_smoke;
    reg signal = 0;
    initial begin
        $dumpfile("geany_smoke.vcd");
        $dumpvars(0, geany_smoke);
        #1 signal = 1;
        #1 $finish;
    end
endmodule
VERILOG
    (
        cd "$work" &&
        rm -f -- geany_smoke.vcd geany_smoke.vvp &&
        export PATH="$prefix/bin:$PATH" &&
        export IVERILOG_VPI_MODULE_PATH="$prefix/lib/ivl" &&
        "$prefix/bin/iverilog$executable_suffix" -s geany_smoke -o geany_smoke.vvp geany_smoke.v &&
        printf 'cont\n' | GEANY_VVP_DEBUG=1 "$prefix/bin/vvp$executable_suffix" -i -s geany_smoke.vvp > geany_smoke.log &&
        grep -q '^GEANY-VVP-READY' geany_smoke.log &&
        test -s geany_smoke.vcd
    )
}

if [[ -f "$stamp" && $(<"$stamp") == "$stamp_id" &&
      -x "$install_dir/bin/iverilog$executable_suffix" && -x "$install_dir/bin/vvp$executable_suffix" ]]; then
    if check_simulator "$install_dir"; then
        printf '[Simulator] Icarus Verilog %s is already installed.\n' "$version"
        exit 0
    fi
fi

mkdir -p -- "$cache_dir"
if [[ ! -f "$archive" ]] || ! verify_archive "$archive"; then
    printf '[Simulator] Downloading Icarus Verilog %s sources.\n' "$version"
    download=$(mktemp "$cache_dir/.iverilog-$version.XXXXXXXX")
    curl --fail --location --retry 3 --output "$download" "$url"
    if ! verify_archive "$download"; then
        printf '%s\n' 'Icarus Verilog source archive SHA256 mismatch.' >&2
        exit 1
    fi
    mv -f -- "$download" "$archive"
    download=
fi

mkdir -p -- "$work/src" "$work/build" "$work/stage"
tar -xzf "$archive" -C "$work/src" --strip-components=1
patch --batch -d "$work/src" -p1 < "$bridge_patch"
# EN: The verified release already contains configure and generated keyword tables.
# RU: Проверенный выпуск уже содержит configure и сгенерированные таблицы ключевых слов.
if [[ ! -x "$work/src/configure" || ! -f "$work/src/lexor_keyword.cc" ||
      ! -f "$work/src/vhdlpp/lexor_keyword.cc" ]]; then
    printf '%s\n' 'Icarus Verilog release archive is missing generated build files.' >&2
    exit 1
fi
printf '[Simulator] Building Icarus Verilog %s from source.\n' "$version"
(
    cd "$work/build"
    "$work/src/configure" --prefix="$configure_prefix" --libdir="$configure_prefix/lib"
    make -j"$jobs"
    make DESTDIR="$work/stage" install
)
staged="$work/stage$configure_prefix"
install -Dm644 "$work/src/COPYING" "$staged/share/licenses/iverilog/COPYING"
check_simulator "$staged"
mkdir -p -- "$install_dir"
rsync -a -- "$staged/" "$install_dir/"
check_simulator "$install_dir"
mkdir -p -- "$(dirname "$stamp")"
printf '%s\n' "$stamp_id" > "$stamp"
printf '[Simulator] Installed Icarus Verilog %s in %s.\n' "$version" "$install_dir"
