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
packed_patch="$script_dir/../patches/geany-iverilog-packed.patch"
windows_patch="$script_dir/../patches/geany-iverilog-windows.patch"
device_source="$script_dir/geany-vpi-devices.c"
lua_dir="$script_dir/../vendor/lua"
lua_sources=("$lua_dir"/l*.c "$lua_dir"/*.h)
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

stamp_id="$sha $(file_digest "$bridge_patch") $(file_digest "$packed_patch") $(file_digest "$device_source") $(file_digest "$script_dir/../../src/insight-waveforms/memory-values.h") $(file_digest "$script_dir/geany-ddr4.h") $(file_digest "$script_dir/geany-ddr5.h") $(file_digest "$script_dir/geany-raster.h") $(file_digest "$script_dir/geany-hdmi-ddc.h") $(file_digest "$script_dir/geany-spi-memory.h") $(file_digest "$script_dir/geany-i2c-eeprom.h") $(file_digest "$script_dir/geany-sdram.h") $(file_digest "$script_dir/geany-bus-controller.h") $(file_digest "$script_dir/geany-usart.h") $(file_digest "$script_dir/geany-parallel-psram.h")"
for lua_source in "${lua_sources[@]}" "$lua_dir/onelua.c" "$script_dir"/geany-lua-*.h; do
    stamp_id+=" $(file_digest "$lua_source")"
done
if [[ -n "$executable_suffix" ]]; then
    stamp_id+=" $(file_digest "$windows_patch")"
fi

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
    reg [3:0] address = 3;
    reg chip_n = 0, write_n = 0, output_n = 1;
    reg [7:0] drive = 'z;
    wire [7:0] data = write_n ? 8'bz : 8'ha5;
    assign data = drive;
    always @(address or chip_n or write_n or output_n or data)
        $geany_sram(address, data, chip_n, write_n, output_n, drive);
    initial begin
        $dumpfile("geany_smoke.vcd");
        $dumpvars(0, geany_smoke);
        #1 signal = 1;
        write_n = 1;
        output_n = 0;
        #1 if (data !== 8'ha5) $fatal(1, "SRAM round trip failed");
        address = 4;
        #1 if (data !== 8'hxx) $fatal(1, "Unwritten SRAM is not unknown");
        output_n = 1;
        #1 if (data !== 8'hzz) $fatal(1, "SRAM did not release the bus");
        $finish;
    end
endmodule
VERILOG
    # EN: Exercise source-relative includes with a native Windows input path.
    # RU: Проверяем include рядом с исходником, используя родной путь Windows.
    local relative_source="$work/include-check/conditional.v"
    mkdir -p -- "$work/include-check" || return
    printf '`define GEANY_INCLUDE_CHECK\n' > "$work/include-check/widths.vh" || return
    cat > "$relative_source" <<'VERILOG'
`include "widths.vh"
`ifdef GEANY_INCLUDE_CHECK
module geany_relative_include;
endmodule
`endif
VERILOG
    if [[ -n "$executable_suffix" ]]; then
        relative_source=$(cygpath -w "$relative_source") || return
    fi
    (
        cd "$work" &&
        rm -f -- geany_smoke.vcd geany_smoke.vvp &&
        export PATH="$prefix/bin:$PATH" &&
        export IVERILOG_VPI_MODULE_PATH="$prefix/lib/ivl" &&
        test -f "$prefix/lib/ivl/geany_devices.vpi" &&
        "$prefix/bin/iverilog$executable_suffix" -g2012 -E -o - geany_smoke.v > geany_smoke.preprocessed &&
        grep -q "^module geany_smoke;" geany_smoke.preprocessed &&
        "$prefix/bin/iverilog$executable_suffix" -g2012 -grelative-include -E -o - "$relative_source" > geany_include.preprocessed &&
        grep -q "^module geany_relative_include;" geany_include.preprocessed &&
        "$prefix/bin/iverilog$executable_suffix" -g2012 -s geany_smoke -o geany_smoke.vvp geany_smoke.v &&
        printf 'cont\n' | GEANY_VVP_DEBUG=1 "$prefix/bin/vvp$executable_suffix" -M "$prefix/lib/ivl" -m geany_devices -i -s geany_smoke.vvp > geany_smoke.log &&
        grep -q '^GEANY-VVP-READY' geany_smoke.log &&
        test -s geany_smoke.vcd
    )
}

# EN: Confirm Windows ivl saves syntax diagnostics without an inherited console.
# RU: Проверяет, что ivl в Windows сохраняет диагностику без унаследованной консоли.
check_windows_diagnostics() {
    local prefix=$1
    [[ -n "$executable_suffix" ]] || return 0
    (
        cd "$work"
        printf '%s\n' 'module geany_invalid(;' > geany-invalid.sv
        rm -f -- geany-invalid.stderr
        if GEANY_IVL_DIAGNOSTICS=geany-invalid.stderr \
            "$prefix/lib/ivl/ivl.exe" -- geany-invalid.sv >/dev/null 2>&1; then
            printf '%s\n' 'Icarus unexpectedly accepted invalid HDL.' >&2
            exit 1
        fi
        if ! grep -qi 'syntax error' geany-invalid.stderr; then
            printf '%s\n' 'Icarus did not save its syntax diagnostics.' >&2
            exit 1
        fi
        cat > geany-legacy-port.v <<'VERILOG'
module geany_legacy_port(source, mirror, zero);
    input wire source;
    output wire mirror = source, zero = 0;
endmodule
module geany_legacy_port_test;
    reg source = 0;
    wire mirror, zero;
    geany_legacy_port dut(source, mirror, zero);
    initial begin
        #1 if (mirror !== 0 || zero !== 0) $fatal(1, "Incorrect initial net drive");
        source = 1;
        #1 if (mirror !== 1 || zero !== 0) $fatal(1, "Net initializer did not drive continuously");
        $finish;
    end
endmodule
VERILOG
        "$prefix/bin/iverilog.exe" -g2012 -s geany_legacy_port_test -o geany-legacy-port.vvp geany-legacy-port.v &&
            "$prefix/bin/vvp.exe" geany-legacy-port.vvp
    )
}

if [[ -f "$stamp" && $(<"$stamp") == "$stamp_id" &&
      -x "$install_dir/bin/iverilog$executable_suffix" && -x "$install_dir/bin/vvp$executable_suffix" ]]; then
    if check_simulator "$install_dir" && check_windows_diagnostics "$install_dir"; then
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
patch --batch -d "$work/src" -p1 < "$packed_patch"
if [[ -n "$executable_suffix" ]]; then
    patch --batch -d "$work/src" -p1 < "$windows_patch"
fi
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
device_flags=(-shared -pthread -DMAKE_LIB)
if [[ -z "$executable_suffix" ]]; then
    device_flags+=(-fPIC)
fi
"${CC:-gcc}" "${device_flags[@]}" -I "$staged/include/iverilog" \
    -I "$lua_dir" "$device_source" "$lua_dir/onelua.c" -L "$staged/lib" -lvpi -lm -o "$staged/lib/ivl/geany_devices.vpi"
if [[ -n "$executable_suffix" ]]; then
    # These helpers communicate over inherited pipes, without a console window.
    for helper in bin/iverilog.exe bin/vvp.exe lib/ivl/ivl.exe lib/ivl/ivlpp.exe; do
        objcopy --subsystem windows "$staged/$helper"
    done
fi
install -Dm644 "$work/src/COPYING" "$staged/share/licenses/iverilog/COPYING"
check_simulator "$staged"
check_windows_diagnostics "$staged"
mkdir -p -- "$install_dir"
rsync -a -- "$staged/" "$install_dir/"
check_simulator "$install_dir"
mkdir -p -- "$(dirname "$stamp")"
printf '%s\n' "$stamp_id" > "$stamp"
printf '[Simulator] Installed Icarus Verilog %s in %s.\n' "$version" "$install_dir"
