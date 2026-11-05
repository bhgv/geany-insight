#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

cache_dir=$1
install_dir=$(cygpath -u "$2")
jobs=$3
version=$INSIGHT_IVERILOG_VERSION
sha=$INSIGHT_IVERILOG_SHA256
archive="$cache_dir/iverilog-$version.tar.gz"
stamp="$install_dir/share/geany-insight/iverilog-source.sha256"
work=$(mktemp -d "${TMPDIR:-/tmp}/geany-iverilog.XXXXXXXX")
download=
trap 'rm -rf -- "$work"; if [[ -n "$download" ]]; then rm -f -- "$download"; fi' EXIT

# Verify a downloaded archive before extracting it.
# Проверяет скачанный архив перед распаковкой.
verify_archive() {
    local digest
    digest=$(sha256sum -- "$1")
    [[ ${digest%% *} == "$sha" ]]
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
        "$prefix/bin/iverilog.exe" -s geany_smoke -o geany_smoke.vvp geany_smoke.v &&
        "$prefix/bin/vvp.exe" geany_smoke.vvp &&
        test -s geany_smoke.vcd
    )
}

if [[ -f "$stamp" && $(<"$stamp") == "$sha" &&
      -x "$install_dir/bin/iverilog.exe" && -x "$install_dir/bin/vvp.exe" ]]; then
    if check_simulator "$install_dir"; then
        printf '[Simulator] Icarus Verilog %s is already installed.\n' "$version"
        exit 0
    fi
fi

mkdir -p -- "$cache_dir"
if [[ ! -f "$archive" ]] || ! verify_archive "$archive"; then
    printf '[Simulator] Downloading Icarus Verilog %s sources.\n' "$version"
    download=$(mktemp "$cache_dir/.iverilog-$version.XXXXXXXX")
    curl --fail --location --retry 3 --output "$download" "$INSIGHT_IVERILOG_URL"
    if ! verify_archive "$download"; then
        printf '%s\n' 'Icarus Verilog source archive SHA256 mismatch.' >&2
        exit 1
    fi
    mv -f -- "$download" "$archive"
    download=
fi

mkdir -p -- "$work/src" "$work/build" "$work/stage"
tar -xzf "$archive" -C "$work/src" --strip-components=1
(
    cd "$work/src"
    bash ./autoconf.sh
)
printf '[Simulator] Building Icarus Verilog %s from source.\n' "$version"
(
    cd "$work/build"
    "$work/src/configure" --prefix=/geany-iverilog --libdir=/geany-iverilog/lib
    make -j"$jobs"
    make DESTDIR="$work/stage" install
)
staged="$work/stage/geany-iverilog"
install -Dm644 "$work/src/COPYING" "$staged/share/licenses/iverilog/COPYING"
check_simulator "$staged"
mkdir -p -- "$install_dir"
rsync -a -- "$staged/" "$install_dir/"
check_simulator "$install_dir"
mkdir -p -- "$(dirname "$stamp")"
printf '%s\n' "$sha" > "$stamp"
printf '[Simulator] Installed Icarus Verilog %s in %s.\n' "$version" "$install_dir"
