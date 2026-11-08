#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the installed Icarus debugger bridge using the editor's protocol."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import time


# EN: Fail with a specific diagnostic even when Python runs with optimization.
# RU: Выводим конкретную ошибку даже при запуске Python с оптимизацией.
def require(condition, message):
    if not condition:
        raise RuntimeError(message)


class Simulator:
    """One paused simulator with a transcript preserved for diagnostics."""

    # EN: Start the installed vvp with the same flags as the editor.
    # RU: Запускаем установленный vvp с теми же ключами, что и редактор.
    def __init__(self, runtime, directory, environment):
        self.transcript = directory / "interactive.log"
        self.offset = 0
        with self.transcript.open("wb") as output:
            self.process = subprocess.Popen(
                [str(runtime), "-i", "-s", "simulation.vvp"],
                cwd=directory, env=environment, stdin=subprocess.PIPE,
                stdout=output, stderr=subprocess.STDOUT,
            )

    # EN: Read through the next prompt with a deadline and a transcript limit.
    # RU: Читаем до следующего приглашения с ограничением времени и журнала.
    def prompt(self):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            with self.transcript.open("rb") as output:
                output.seek(self.offset)
                data = output.read(1024 * 1024 + 1)
            require(len(data) <= 1024 * 1024, "Simulator output exceeded the prompt limit")
            if data.endswith(b"> "):
                self.offset += len(data)
                return data.decode("utf-8", errors="replace")
            if self.process.poll() is not None:
                raise RuntimeError("vvp exited before reaching the next prompt")
            time.sleep(0.025)
        raise RuntimeError("Timed out waiting for the vvp prompt")

    # EN: Send one debugger command while vvp is paused.
    # RU: Передаём одну команду отладки, пока vvp на паузе.
    def send(self, command):
        self.process.stdin.write((command + "\n").encode("ascii"))
        self.process.stdin.flush()

    # EN: Send a command and return its response at the next pause.
    # RU: Передаём команду и возвращаем ответ на следующей паузе.
    def command(self, command):
        self.send(command)
        return self.prompt()

    # EN: Wait for a clean exit and kill a stuck simulator on failure.
    # RU: Ожидаем штатного завершения и останавливаем зависший симулятор.
    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=10)
        self.process.stdin.close()


# EN: Compile the exact fixture paths with source checkpoints enabled.
# RU: Компилируем файлы по точным путям с включёнными точками исходника.
def compile_design(compiler, directory, sources, environment):
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / "compile.log").open("wb") as log:
        subprocess.run(
            [str(compiler), "-g2012", "-pfileline=1", "-o", "simulation.vvp",
             *(str(source) for source in sources)],
            cwd=directory, env=environment, stdout=log, stderr=subprocess.STDOUT,
            timeout=90, check=True,
        )


# EN: Confirm the completed capture has the expected time and signal.
# RU: Проверяем ожидаемый такт и сигнал в завершённой записи.
def check_capture(path, tick, signal):
    capture = path.read_text(encoding="ascii")
    require("$enddefinitions" in capture, f"Incomplete VCD: {path}")
    require(re.search(rf"(?m)^#{tick}$", capture), f"Missing VCD tick {tick}: {path}")
    require(re.search(rf"(?m)^\$var\s+\S+\s+\d+\s+\S+\s+{re.escape(signal)}\b", capture),
            f"Missing VCD signal {signal}: {path}")


# EN: Check source stepping keeps the selected process across a time change.
# RU: Проверяем, что шаг по исходнику сохраняет выбранный процесс при смене такта.
def check_source_steps(compiler, runtime, fixtures, directory, environment):
    source = fixtures / "procedural_flow_tb.v"
    compile_design(compiler, directory, [source], environment)
    simulator = Simulator(runtime, directory, environment)
    try:
        require("GEANY-VVP-SOURCE-STEP" in simulator.prompt(), "Missing HDL stepping capability")
        first = simulator.command("geany-step-source")
        second = simulator.command("geany-step-process")
        process = re.search(r"Geany process (\d+) in ", first)
        require(process and f"Geany process {process.group(1)} in " in second,
                "Current-process stepping changed the selected process")
        for response in (first, second):
            position = re.search(r"GEANY-VVP-STOP\t(\d+)\t([^\r\n]+)", response)
            require(position and Path(position.group(2)).resolve() == source.resolve(),
                    "HDL step did not report the fixture source position")
        require("time is 5 ticks" in second, "Process step did not reach the clock edge")
        simulator.send("cont")
        require(simulator.process.wait(timeout=30) == 0, "HDL stepping simulation failed")
    finally:
        simulator.close()
    check_capture(directory / "procedural_flow.vcd", 5, "sampled")


# EN: Verify the installed bridge stops on a condition before the assignment.
# RU: Проверяем условную остановку установленного моста до присваивания.
def check_breakpoint(compiler, runtime, fixtures, directory, environment):
    source = fixtures / "counter.v"
    target = [number for number, line in enumerate(source.read_text(encoding="utf-8").splitlines(), 1)
              if "count <= count + 1'b1;" in line]
    require(len(target) == 1, "Could not identify the counter assignment")
    line = target[0]
    compile_design(compiler, directory, [source, fixtures / "counter_tb.v"], environment)
    (directory / ".geany-vvp-breakpoints").write_text(
        f"{line}\t{source.as_posix()}\tcounter_tb.count\t0001\t1\t0\t1\n",
        encoding="utf-8",
    )
    simulator = Simulator(runtime, directory, environment)
    try:
        require("GEANY-VVP-READY" in simulator.prompt(), "Missing installed Geany bridge")
        require("1 source breakpoint(s)" in simulator.command("geany-load"), "Rule was not loaded")
        stopped = simulator.command("cont").replace("\\", "/")
        require(f"GEANY-VVP-STOP\t{line}\t{source.as_posix()}".lower() in stopped.lower(),
                "Wrong source position at the conditional stop")
        require(re.search(r"GEANY-VVP-EVENT\t\d+\t[^\r\n]+\t15\t1\tcounter_tb.count\t0001", stopped),
                "Wrong scripted stop event, tick or signal value")
        snapshot = simulator.command("geany-signals").replace("\\", "/")
        require(re.search(r"(?m)^GEANY-SIGNALS-BEGIN\t15\r?$", snapshot),
                "Snapshot did not report the actual simulation tick")
        require(re.search(r"GEANY-SIGNAL\tP\tcounter_tb\.dut\.clk\t[^\r\n]*counter\.v\t1", snapshot),
                "Paused snapshot did not include the port in its defining file")
        require(re.search(r"GEANY-SIGNAL\tN\tcounter_tb\.dut\.odd\t[^\r\n]*counter\.v\t1", snapshot),
                "Paused snapshot did not include the net")
        require(re.search(r"GEANY-SIGNAL\tR\tcounter_tb\.dut\.count\t[^\r\n]*counter\.v\t0001", snapshot),
                "Paused snapshot did not include the register")
        require("expected a static register or integer" in
                simulator.command("geany-write-register counter_tb.dut.odd 1"),
                "A driven net was accepted as a register")
        require("exact bit width" in simulator.command("geany-write-register counter_tb.dut.count 1"),
                "A short register value was accepted")
        require("counter_tb.dut.count = 10xz" in
                simulator.command("geany-write-register counter_tb.dut.count 10xz"),
                "Four-state register write failed at the breakpoint")
        require("counter_tb.dut.count = 0001" in
                simulator.command("geany-write-register counter_tb.dut.count 0001"),
                "Register was not restored while paused")
        simulator.send("cont")
        require(simulator.process.wait(timeout=30) == 0, "Counter simulation failed")
    finally:
        simulator.close()
    check_capture(directory / "counter.vcd", 25, "count")


# EN: Verify each signal stop mode and removal with compiler/runtime and environment.
# fixtures is unused. The generated design and logs stay in directory.
# RU: Проверяет режимы и снятие остановок через compiler/runtime и environment.
# fixtures не используется. Схема и журнал создаются в directory.
def check_signal_stops(compiler, runtime, fixtures, directory, environment):
    directory.mkdir(parents=True, exist_ok=True)
    source = directory / "signal_stops.v"
    source.write_text(
        """module signal_stops;
  reg clk = 0;
  reg [3:0] bus = 0;
  wire net_clk = clk;
  initial begin
    #5 clk = 1;
    #5 clk = 0;
    #5 bus = 4'b1010;
    #5 bus = 4'b10xz;
    #5 bus = 4'b0000;
    #5 $finish;
  end
endmodule
""", encoding="ascii")
    compile_design(compiler, directory, [source.resolve()], environment)
    simulator = Simulator(runtime, directory, environment)
    try:
        require("GEANY-VVP-SIGNAL-STOPS" in simulator.prompt(), "Missing signal stop support")
        missing = simulator.command("geany-watch-signal signal_stops.missing rise")
        require("GEANY-SIGNAL-STOP-MISSING\tsignal_stops.missing" in missing,
                "Missing signal was not reported to the editor")
        rejected = simulator.command("geany-watch-signal signal_stops.bus rise")
        require("GEANY-SIGNAL-STOP-REJECTED\tsignal_stops.bus" in rejected,
                "Invalid edge width was not reported to the editor")
        armed = simulator.command("geany-watch-signal signal_stops.net_clk rise")
        require("GEANY-SIGNAL-STOP-ON\tsignal_stops.net_clk\trise\t" in armed, "Missing armed rule record")
        hit = simulator.command("cont")
        require("signal_stops.net_clk 0 -> 1" in hit, "Missing rising edge")
        require("GEANY-SIGNAL-STOP-HIT\tsignal_stops.net_clk" in hit, "Missing rule hit record")
        require("signal stop armed" in simulator.command("geany-watch-signal signal_stops.clk fall"),
                "Could not arm a register")
        removed = simulator.command("geany-watch-signal signal_stops.net_clk off")
        require("GEANY-SIGNAL-STOP-OFF\tsignal_stops.net_clk" in removed, "Missing rule removal record")
        require("signal_stops.clk 1 -> 0" in simulator.command("cont"), "Missing falling edge")
        require("signal stop armed" in simulator.command("geany-watch-signal signal_stops.bus mask 10??"),
                "Could not arm a masked stop")
        require("signal_stops.bus 0000 -> 1010" in simulator.command("cont"), "Missing mask match")
        require("signal stop armed" in simulator.command("geany-watch-signal signal_stops.bus unknown"),
                "Could not replace the bus stop")
        require("signal_stops.bus 1010 -> 10xz" in simulator.command("cont"), "Missing X/Z transition")
        require("signal stop armed" in simulator.command("geany-watch-signal signal_stops.bus change"),
                "Could not arm any-change stop")
        require("signal_stops.bus 10xz -> 0000" in simulator.command("cont"), "Missing general change")
        simulator.send("cont")
        require(simulator.process.wait(timeout=30) == 0, "Signal stop fixture failed")
    finally:
        simulator.close()


# EN: Check process selection, isolated activations, a parent frame and memory.
# RU: Проверяем выбор процесса, отдельные активации, родительский кадр и память.
def check_processes(compiler, runtime, fixtures, directory, environment):
    compile_design(compiler, directory, [fixtures / "process_values_tb.v"], environment)
    simulator = Simulator(runtime, directory, environment)
    try:
        require("GEANY-VVP-FRAMES" in simulator.prompt(), "Missing frame inspection capability")
        require("time is 1 ticks" in simulator.command("geany-step-tick"), "Wrong next tick")
        processes = simulator.command("geany-processes")
        workers = re.findall(r"(?m)^(\d+)\s+[^\n]+?\bprocess_values_tb\.worker(?:\s|$)", processes)
        require(len(workers) == 2, "Expected two live worker activations")
        values = set()
        for worker in workers:
            require("Frame 0:" in simulator.command(f"geany-select-process {worker}"),
                    "Could not select worker")
            response = simulator.command("geany-process-values")
            saved = re.search(r"(?m)^saved = ([01xz]+)\r?$", response)
            require(saved, "Missing automatic local saved")
            values.add(saved.group(1))
            snapshot = simulator.command("geany-frame-tree")
            require(f"GEANY-FRAME-ROW\tW\tlocal_items\t0\t{saved.group(1)}" in snapshot,
                    "Dynamic array element was not read in the selected activation")
            if worker == workers[0]:
                edited = simulator.command("geany-frame-write local_items 0 10xz")
                require("GEANY-FRAME-ROW\tW\tlocal_items\t0\t10xz" in edited,
                        "Dynamic array write did not read back the four-state value")

        require(values == {"0011", "1001"}, "Task activations did not retain distinct values")
        require("Frame 1:" in simulator.command("geany-select-frame 1"), "Missing parent frame")
        require("samples[-2] = 10xz" in simulator.command("geany-read-memory samples -2"),
                "Wrong four-state memory word")
        require("unavailable" in simulator.command("geany-read-memory samples 99"),
                "Out-of-range memory access was not rejected")
        simulator.send("cont")
        require(simulator.process.wait(timeout=30) == 0, "Process test simulation failed")
    finally:
        simulator.close()


# EN: Check packed layouts and writes in two live task activations with compiler/runtime.
# RU: Проверяет поля и запись в двух живых активациях через compiler/runtime.
def check_packed_frame(compiler, runtime, fixtures, directory, environment):
    compile_design(compiler, directory, [fixtures / "packed_frame_tb.sv"], environment)
    simulator = Simulator(runtime, directory, environment)
    try:
        simulator.prompt()
        simulator.command("geany-step-tick")
        processes = simulator.command("geany-processes")
        workers = re.findall(r"(?m)^(\d+)\s+[^\n]+?\bpacked_frame_tb\.worker(?:\s|$)", processes)
        require(len(workers) == 2, "Expected two packed task activations")
        snapshots = []
        for worker in workers:
            simulator.command(f"geany-select-process {worker}")
            snapshot = simulator.command("geany-frame-tree")
            require("GEANY-FRAME-FIELD\ttag.valid\t7\t" in snapshot, "Missing nested field")
            require("GEANY-FRAME-FIELD\ttag.code\t4\t" in snapshot, "Wrong nested field offset")
            require("GEANY-FRAME-FIELD\tpayload\t0\t" in snapshot, "Missing payload field")
            snapshots.append(snapshot)
        payloads = [re.search(r"GEANY-FRAME-FIELD\tpayload\t0\t([01xz]+)", value)
                    for value in snapshots]
        require(all(payloads) and {value.group(1) for value in payloads} == {"0110", "1001"},
                "Packed task values were mixed")
        simulator.command(f"geany-select-process {workers[0]}")
        before = re.search(r"GEANY-FRAME-ROW\tW\tpacket\t-\t([01xz]+)", snapshots[0])
        require(before, "Missing writable packet")
        updated = simulator.command(f"geany-frame-write packet - {before.group(1)[:4]}10xz")
        require("GEANY-FRAME-FIELD\tpayload\t0\t10xz" in updated, "Packed write not reflected")
        simulator.command(f"geany-select-process {workers[1]}")
        require(simulator.command("geany-frame-tree") == snapshots[1],
                "Packed write affected a different activation")
        simulator.command("geany-select-frame 1")
        parent = simulator.command("geany-frame-tree")
        require("GEANY-FRAME-ROW\tW\tstorage\t0\t11000011" in parent and
                "GEANY-FRAME-FIELD\tpayload\t0\t0011" in parent,
                "Missing packed fields in fixed memory")
        require("GEANY-FRAME-FIELD\tpacket.tag.code\t4\t011" in parent and
                "GEANY-FRAME-FIELD\traw\t0\t00111010" in parent,
                "Packed union members do not share the expected offsets")
    finally:
        simulator.close()


def check_synchronous_steps(compiler, runtime, fixtures, directory, environment):
    source = fixtures / "synchronous_steps_tb.sv"
    lines = source.read_text(encoding="utf-8").splitlines()
    function_line = next(i for i, line in enumerate(lines, 1) if "local_value = argument" in line)
    return_line = next(i for i, line in enumerate(lines, 1) if "calculate = local_value" in line)
    final_line = next(i for i, line in enumerate(lines, 1) if "result = result + 1" in line)
    display_line = next(i for i, line in enumerate(lines, 1) if "Final result:" in line)
    compile_design(compiler, directory, [source], environment)
    (directory / ".geany-vvp-breakpoints").write_text(
        "".join(f"{line}\t{source.as_posix()}\n" for line in (function_line, final_line)),
        encoding="utf-8",
    )
    simulator = Simulator(runtime, directory, environment)
    try:
        simulator.prompt()
        require("2 source breakpoint(s)" in simulator.command("geany-load"), "Rules were not loaded")
        require(f"GEANY-VVP-STOP\t{function_line}\t" in simulator.command("cont"),
                "Function breakpoint did not stop")
        require(f"GEANY-VVP-STOP\t{return_line}\t" in simulator.command("geany-step-process"),
                "Process step did not stay inside the function")
        function_values = simulator.command("geany-process-values")
        require("local_value = " + format(7, "032b") in function_values,
                f"Function local was not read from the active frame: {function_values!r}")
        require("Frame 1:" in simulator.command("geany-select-frame 1"), "Missing caller")
        values = simulator.command("geany-process-values")
        for expected in ('label_text = "ready"', "ratio = 1.25",
                         "samples [dynamic array, 1 elements]", "pending [queue, 1 elements]"):
            require(expected in values, f"Missing SystemVerilog value: {expected}")
        require("samples[0] = 10xz" in simulator.command("geany-read-memory samples 0"),
                "Wrong dynamic array element")
        require("pending[0] = " + format(17, "032b") in
                simulator.command("geany-read-memory pending 0"), "Wrong queue element")
        require(f"GEANY-VVP-STOP\t{final_line}\t" in simulator.command("cont"),
                "Final breakpoint did not stop")
        require(f"GEANY-VVP-STOP\t{display_line}\t" in simulator.command("geany-step-source"),
                "Source step did not stay inside final")
        simulator.send("cont")
        require(simulator.process.wait(timeout=30) == 0, "Synchronous simulation failed")
        require(b"Final result: 15" in simulator.transcript.read_bytes(),
                "Continuing did not preserve function and final execution")
    finally:
        simulator.close()


# EN: Execute an installed program and preserve both output streams.
# RU: Запускаем установленную программу и сохраняем оба потока вывода.
def capture_command(command, directory, name, environment):
    directory.mkdir(parents=True, exist_ok=True)
    stdout = directory / f"{name}.stdout"
    stderr = directory / f"{name}.stderr"
    with stdout.open("wb") as output, stderr.open("wb") as errors:
        subprocess.run(command, cwd=directory, env=environment, stdout=output,
                       stderr=errors, timeout=90, check=True)
    return stdout.read_text(encoding="utf-8")


# EN: Verify the installed editor loads its runtime libraries.
# RU: Проверяем загрузку библиотек установленным редактором.
def check_editor(editor, directory, environment):
    version = capture_command([str(editor), "--version"], directory, "version", environment)
    require("GTK" in version and "GLib" in version, "Unexpected Geany version output")


# EN: Check that a visible Win32 window belongs to this exact editor process.
# RU: Проверяем, что видимое окно Win32 принадлежит запущенному процессу.
def windows_document_visible(pid, filename):
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    found = False

    @callback_type
    def visit(window, _):
        nonlocal found
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
        if owner.value != pid or not user32.IsWindowVisible(window):
            return True
        title = ctypes.create_unicode_buffer(1024)
        user32.GetWindowTextW(window, title, len(title))
        found = filename in title.value
        return not found

    user32.EnumWindows(visit, 0)
    return found


# EN: Launch GTK with an isolated configuration and verify the test document opens.
# RU: Запускаем GTK с отдельной конфигурацией и проверяем открытие тестового файла.
def check_gtk_editor(editor, fixtures, directory, environment):
    directory.mkdir(parents=True, exist_ok=True)
    config = directory / "config"
    config.mkdir(exist_ok=True)
    source = fixtures / "procedural_flow_tb.v"
    base = [str(editor), "--config", str(config), "--no-plugins", "--no-session"]
    if os.name == "nt":
        base.append("--new-instance")
    else:
        base += ["--socket-file", str(directory / "geany.sock")]

    with (directory / "editor.log").open("wb") as log:
        process = subprocess.Popen([*base, str(source)], cwd=directory, env=environment,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                require(process.poll() is None, f"GTK editor exited early ({process.returncode})")
                if os.name == "nt":
                    if windows_document_visible(process.pid, source.name):
                        return
                elif (directory / "geany.sock").exists():
                    listing = subprocess.run([*base, "--list-documents"], cwd=directory,
                                             env=environment, capture_output=True, timeout=5)
                    if listing.returncode == 0 and source.name.encode() in listing.stdout:
                        (directory / "documents.log").write_bytes(listing.stdout)
                        return
                time.sleep(0.2)
            raise RuntimeError("GTK editor did not open the HDL fixture before the deadline")
        finally:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


# EN: Decode a hex field from the Slang service protocol.
# RU: Декодируем шестнадцатеричное поле протокола Slang.
def decode_field(field):
    try:
        return bytes.fromhex(field).decode("utf-8")
    except ValueError as error:
        raise RuntimeError("Malformed field in the installed Slang reply") from error


# EN: Verify the recorded hierarchy resolves to HDL and crosses all edge types.
# RU: Проверяем переход от имени на диаграмме к HDL и все типы связей.
def check_slang(service, fixtures, directory, environment):
    fixture = fixtures / "procedural_flow_tb.v"
    args = ["--", str(fixture)]
    sampled = "procedural_flow_tb.dut.sampled"
    reply = capture_command([str(service), "--editor-wave-definition", sampled, *args],
                            directory, "wave-definition", environment).splitlines()
    require(len(reply) == 2 and reply[0] == "insight-slang-definition-v1",
            "Recorded hierarchy did not resolve to a declaration")
    fields = reply[1].split("\t")
    require(len(fields) == 3 and fields[1].isdigit() and fields[2].isdigit(),
            "Malformed Slang declaration position")
    target = Path(decode_field(fields[0])).resolve()
    line, column = int(fields[1]), int(fields[2])
    source_lines = fixture.read_text(encoding="utf-8").splitlines()
    require(target == fixture.resolve() and 0 < line <= len(source_lines) and column > 0
            and "reg sampled" in source_lines[line - 1], "Wrong HDL declaration for sampled")

    reply = capture_command([str(service), "--editor-index", *args], directory,
                            "project-index", environment).splitlines()
    require(len(reply) > 2 and reply[0] == "insight-slang-index-v2",
            "Missing Slang project index")
    edges = set()
    for record in reply[2:]:
        fields = record.split("\t")
        require(len(fields) == 8, "Malformed Slang index record")
        if fields[0] == "P":
            edges.add(tuple(decode_field(fields[i]) for i in (5, 1, 7)))
    root = "procedural_flow_tb"
    flow = [f"{root}.stimulus", f"{root}.dut.data_in", sampled,
            f"{root}.dut.data_out", f"{root}.result"]
    roles = ["port", "procedural", "assign", "port"]
    for start, end, role in zip(flow, flow[1:], roles):
        require((start, end, f"{role}-destination") in edges
                and (end, start, f"{role}-source") in edges,
                f"Missing Slang dependency between {start} and {end}")


    for name, role in (("enable", "condition"), ("clk", "posedge")):
        control = f"{root}.dut.{name}"
        require((control, sampled, f"{role}-destination") in edges
                and (sampled, control, f"{role}-source") in edges,
                f"Missing {role} dependency between {control} and {sampled}")
        require((control, f"{root}.dut.data_out", f"{role}-destination") not in edges,
                f"{role} leaked out of the procedural assignment")


    reply = capture_command([str(service), "--editor-index", "--", str(fixtures / "slice_flow_tb.v")],
                            directory, "slice-index", environment).splitlines()
    selections = set()
    for record in reply[2:]:
        fields = record.split("\t")
        if len(fields) == 8 and fields[0] == "P":
            selections.add(tuple(decode_field(fields[i]) for i in (5, 1, 6, 7)))
    require(("slice_flow_tb.bus", "slice_flow_tb.dut.part", "[7:4] => [3:0]", "port-destination")
            in selections, "Missing packed slice on the port edge")
    require(("slice_flow_tb.dut.bit_out", "slice_flow_tb.dut.part", "all / [2]", "assign-source")
            in selections, "Missing bit selection on the assignment edge")


    reply = capture_command([str(service), "--editor-index", "--",
                             str(fixtures / "interface_flow_tb.sv")],
                            directory, "interface-index", environment).splitlines()
    require(reply[1] == "0", "Interface fixture failed to elaborate")
    interface_edges = set()
    for record in reply[2:]:
        fields = record.split("\t")
        if len(fields) == 8 and fields[0] == "P":
            interface_edges.add(tuple(decode_field(fields[i]) for i in (5, 1, 6, 7)))

    root = "interface_flow_tb"
    for link, tx, rx in (("active_link", "tx", "rx"), ("other_link", "other_tx", "other_rx")):
        interface = f"{root}.{link}"
        sampled = f"{root}.{rx}.sampled"
        alias = f"{interface}.consumer.high"
        expected = {
            (f"{interface}.data", f"{root}.{tx}.payload", "[7:0] => [7:0]", "assign-source"),
            (alias, f"{interface}.data", "[7:4] => [7:4]", "interface-source"),
            (f"{interface}.tag", f"{interface}.producer.tag_out", "[3:2] => [3:2]",
             "interface-source"),
            (f"{interface}.producer.tag_out", f"{root}.{tx}.payload", "[3:2] => [7:6]",
             "assign-source"),
            (sampled, alias, "all / [5]", "procedural-source"),
            (sampled, f"{interface}.valid", "", "condition-source"),
            (sampled, f"{interface}.clk", "", "posedge-source"),
        }
        require(expected <= interface_edges, f"Missing interface edges: {expected - interface_edges}")
        require((sampled, f"{interface}.valid", "", "procedural-source") not in interface_edges,
                "Case selector was misclassified as data")
        other = "other_link" if link == "active_link" else "active_link"
        require(not any(owner == sampled and peer.startswith(f"{root}.{other}.")
                        for owner, peer, _, _ in interface_edges),
                "A dependency leaked into a different interface instance")


# EN: Run the checks against binaries installed by either native build launcher.
# RU: Запускаем проверки программ, установленных любым штатным сборщиком.
def check_windowless_helpers(prefix, service):
    helpers = [prefix / path for path in (
        "bin/iverilog.exe", "bin/vvp.exe", "lib/ivl/ivl.exe", "lib/ivl/ivlpp.exe"
    )] + [service]
    for helper in helpers:
        with helper.open("rb") as executable:
            require(executable.read(2) == b"MZ", f"Invalid Windows executable: {helper}")
            executable.seek(0x3C)
            header = int.from_bytes(executable.read(4), "little")
            executable.seek(header)
            require(executable.read(4) == b"PE\0\0", f"Invalid PE header: {helper}")
            executable.seek(header + 24 + 68)
            subsystem = int.from_bytes(executable.read(2), "little")
            require(subsystem == 2, f"Console window enabled for {helper.name}")


# EN: Run the DDR5 fixture with compiler/runtime and check its result in directory.
# RU: Запускает тест DDR5 через compiler/runtime и проверяет результат в directory.
def check_ddr5_device(compiler, runtime, fixtures, directory, environment):
    compile_design(compiler, directory, [fixtures / "ddr5_device_tb.sv"], environment)
    (directory / "ddr5.hex").write_text(
        "@091a7420 a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aa ab ac ad ae af\n",
        encoding="ascii",
    )
    modules = runtime.parent.parent / "lib" / "ivl"
    report = capture_command([str(runtime), "-M", str(modules), "-m", "geany_devices",
                              "simulation.vvp"], directory, "ddr5", environment)
    require("DDR5 device passed" in report, "Built-in DDR5 test did not finish")


def check_ddr4_device(compiler, runtime, fixtures, directory, environment):
    compile_design(compiler, directory, [fixtures / "ddr4_device_tb.sv"], environment)
    modules = runtime.parent.parent / "lib" / "ivl"
    report = capture_command([str(runtime), "-M", str(modules), "-m", "geany_devices",
                              "simulation.vvp"], directory, "ddr4", environment)
    require("DDR4 device passed" in report, "Built-in DDR4 test did not finish")


# EN: Run the DDR1 fixture using compiler and runtime with its image in directory.
# RU: Запускает тест DDR1 через compiler и runtime с образом в directory.
def check_ddr1_device(compiler, runtime, fixtures, directory, environment):
    compile_design(compiler, directory, [fixtures / "ddr1_device_tb.sv"], environment)
    (directory / "ddr1.hex").write_text("@48c800 a0 a1 a2 a3\n", encoding="ascii")
    modules = runtime.parent.parent / "lib" / "ivl"
    report = capture_command([str(runtime), "-M", str(modules), "-m", "geany_devices",
                              "simulation.vvp"], directory, "ddr1", environment)
    require("DDR1 device passed" in report, "Built-in DDR1 test did not finish")


def check_memory_image(compiler, runtime, fixtures, directory, environment):
    compile_design(compiler, directory, [fixtures / "memory_image_tb.sv"], environment)
    (directory / "sram.hex").write_text("# two words\n@3 beef 0123\n", encoding="ascii")
    (directory / "ddr2.hex").write_text("@48d400 90 91 92 93 94 95 96 97\n", encoding="ascii")
    (directory / "ddr.hex").write_text("@2048d400 80 81 82 83 84 85 86 87\n", encoding="ascii")
    modules = runtime.parent.parent / "lib" / "ivl"
    report = capture_command([str(runtime), "-M", str(modules), "-m", "geany_devices",
                              "simulation.vvp"], directory, "memory-image", environment)
    require("Memory image test passed" in report, "Built-in memory image test did not finish")


def check_spi_memory(compiler, runtime, fixtures, directory, environment):
    compile_design(compiler, directory, [fixtures / "spi_memory_tb.sv"], environment)
    (directory / "nor.hex").write_text("@10 ba\n", encoding="ascii")
    (directory / "psram.hex").write_text("@7ffffe ba be\n", encoding="ascii")
    (directory / "device.lua").write_bytes((fixtures / "lua_device.lua").read_bytes())
    modules = runtime.parent.parent / "lib" / "ivl"
    report = capture_command([str(runtime), "-M", str(modules), "-m", "geany_devices",
                              "simulation.vvp"], directory, "spi-memory", environment)
    require("SPI memory test passed" in report, "SPI memory test did not finish")


# EN: Verify the built-in and custom EDID profiles with compiler/runtime.
# RU: Проверяет встроенный и пользовательский EDID через compiler/runtime.
def check_hdmi_ddc(compiler, runtime, fixtures, directory, environment):
    compile_design(compiler, directory, [fixtures / "hdmi_ddc_tb.sv"], environment)
    base = bytearray(128)
    base[:8] = bytes.fromhex("00 ff ff ff ff ff ff 00")
    base[126] = 1
    base[127] = (-sum(base)) & 255
    extension = bytearray(128)
    extension[:2] = bytes((2, 3))
    extension[127] = (-sum(extension)) & 255
    (directory / "edid.hex").write_text(
        " ".join(f"{byte:02x}" for byte in base + extension), encoding="ascii")
    modules = runtime.parent.parent / "lib" / "ivl"
    report = capture_command([str(runtime), "-M", str(modules), "-m", "geany_devices",
                              "simulation.vvp"], directory, "hdmi-ddc", environment)
    require("HDMI DDC passed" in report, "HDMI DDC test did not complete")
    bad_checksum = extension.copy()
    bad_checksum[127] ^= 1
    bad_header = base.copy()
    bad_header[0] = 1
    for data, error in (
        (base + bad_checksum, "invalid EDID checksum in block 1"),
        (base, "missing EDID byte at offset 0x80"),
        (bad_header + extension, "invalid EDID header"),
    ):
        (directory / "edid.hex").write_text(
            " ".join(f"{byte:02x}" for byte in data), encoding="ascii")
        rejected = subprocess.run([str(runtime), "-M", str(modules), "-m", "geany_devices",
                                   "simulation.vvp"], cwd=directory, env=environment,
                                  capture_output=True, text=True, timeout=30, check=False)
        require(rejected.returncode != 0 and f"edid.hex: {error}" in rejected.stdout,
                f"Wrong diagnostic for invalid EDID: {error}")



# EN: Run all display modes with compiler/runtime and compare their saved RGB frames.
# RU: Запускает режимы дисплеев через compiler/runtime и сверяет сохранённые кадры RGB.
def check_raster_displays(compiler, runtime, fixtures, directory, environment):
    compile_design(compiler, directory, [fixtures / "raster_displays_tb.sv"], environment)
    modules = runtime.parent.parent / "lib" / "ivl"
    for previous in directory.glob("geany-raster-*.rgb"):
        previous.unlink()
    capture_command([str(runtime), "-M", str(modules), "-m", "geany_devices",
                     "simulation.vvp"], directory, "raster-displays", environment)
    for display in range(7):
        frame = (directory / f"geany-raster-{display}-1.rgb").read_bytes()
        tail = "a65a19" if display in (1, 3, 4, 5) else "a65918"
        expected = (1).to_bytes(4, "little") + bytes.fromhex("ff000000ff000000ff" + tail)
        require(frame == expected, f"Incorrect RGB pixels for display {display}")

    for display, pixels in ((5, "0102030405060708090a0b0c"), (6, "00000004040404080808080c")):
        recovered = (directory / f"geany-raster-{display}-0.rgb").read_bytes()
        require(recovered == (2).to_bytes(4, "little") + bytes.fromhex(pixels),
                f"TMDS display {display} did not skip a corrupt frame and recover")

    dual_frames = {
        7: "ff000000ffff00ff00ff00ff0000ffffff00a65a1959a5e6",
        8: "00ffffff0000ff00ff00ff00ffff000000ff59a6e7a65918",
    }
    for display, pixels in dual_frames.items():
        frame = (directory / f"geany-raster-{display}-1.rgb").read_bytes()
        require(frame == (1).to_bytes(4, "little") + bytes.fromhex(pixels),
                f"Incorrect interleaved LVDS pixels for display {display}")


# EN: Run the parallel PSRAM fixture with compiler/runtime and a high-address image.
# RU: Запускает тест параллельной PSRAM через compiler/runtime с образом по старшему адресу.
def check_parallel_psram(compiler, runtime, fixtures, directory, environment):
    compile_design(compiler, directory, [fixtures / "parallel_psram_tb.sv"], environment)
    (directory / "psram-parallel.hex").write_text("@fffff beef\n", encoding="ascii")
    modules = runtime.parent.parent / "lib" / "ivl"
    report = capture_command([str(runtime), "-M", str(modules), "-m", "geany_devices",
                              "simulation.vvp"], directory, "parallel-psram", environment)
    require("Parallel PSRAM device passed" in report, "Parallel PSRAM test did not complete")


# EN: Run the USART fixture with compiler/runtime and verify both sample edges.
# RU: Запускает тест USART через compiler/runtime и проверяет оба фронта.
def check_usart(compiler, runtime, fixtures, directory, environment):
    compile_design(compiler, directory, [fixtures / "usart_tb.sv"], environment)
    modules = runtime.parent.parent / "lib" / "ivl"
    report = capture_command([str(runtime), "-M", str(modules), "-m", "geany_devices",
                              "simulation.vvp"], directory, "usart", environment)
    for byte in ("a6", "59", "39", "c6"):
        require(f"GEANY-UART\t{byte}" in report, f"Missing USART byte {byte}")
    require("USART device passed" in report, "USART test did not complete")


# EN: Run controller fixtures with compiler/runtime and check replies in directory.
# RU: Запускает тесты ведущих через compiler/runtime и проверяет ответы в directory.
def check_bus_controllers(compiler, runtime, fixtures, directory, environment):
    compile_design(compiler, directory, [fixtures / "bus_controllers_tb.sv"], environment)
    (directory / "nor.hex").write_text("@10 ba\n", encoding="ascii")
    (directory / "i2c.hex").write_text("@1234 be ef\n", encoding="ascii")
    modules = runtime.parent.parent / "lib" / "ivl"
    report = capture_command([str(runtime), "-M", str(modules), "-m", "geany_devices",
                              "simulation.vvp"], directory, "bus-controllers", environment)
    for message in ("SPI controller: RX[4] = ba", "SPI controller: RX[0] = a6",
                    "I2C controller: RX[0] = be", "I2C controller: RX[1] = ef",
                    "I2C controller: NACK, issuing STOP", "Bus controllers test passed"):
        require(message in report, f"Missing controller output: {message}")


def check_i2c_eeprom(compiler, runtime, fixtures, directory, environment):
    compile_design(compiler, directory, [fixtures / "i2c_eeprom_tb.sv"], environment)
    (directory / "i2c.hex").write_text("@1234 be ef\n", encoding="ascii")
    (directory / "device.lua").write_bytes((fixtures / "lua_device.lua").read_bytes())
    modules = runtime.parent.parent / "lib" / "ivl"
    report = capture_command([str(runtime), "-M", str(modules), "-m", "geany_devices",
                              "simulation.vvp"], directory, "i2c-eeprom", environment)
    require("I2C EEPROM test passed" in report, "I2C EEPROM test did not finish")


def check_documented_lua_devices(compiler, runtime, fixtures, directory, environment):
    examples = fixtures.parent.parent / "examples" / "lua-bus-devices"
    modules = runtime.parent.parent / "lib" / "ivl"
    cases = (
        ("spi", "spi-byte-queue", "SPI Lua reply: 47 59 01",
         "wire sck, cs_n, mosi; wire miso = drive; "
         "spi_demo dut(sck, cs_n, mosi, miso); "
         'always @(sck or cs_n or start) $geany_spi_lua(sck, cs_n, mosi, drive, "device.lua");'),
        ("i2c", "i2c-frame", "I2C Lua reply: 10 35",
         "tri1 scl, sda; assign sda = drive; i2c_demo dut(scl, sda); "
         'always @(scl or sda or start) $geany_i2c_lua(scl, sda, 80, drive, "device.lua");'),
    )
    for bus, script, expected, wiring in cases:
        work = directory / bus
        work.mkdir(parents=True, exist_ok=True)
        (work / "device.lua").write_bytes((examples / f"{script}.lua").read_bytes())
        harness = work / "harness.sv"
        harness.write_text(
            "`timescale 1ns/1ns\nmodule harness; reg drive = 1'bz; reg start; "
            f"initial #0 start=1; {wiring} initial #1000 $finish; endmodule\n",
            encoding="ascii",
        )
        compile_design(compiler, work, [examples / f"{bus}-demo.sv", harness], environment)
        report = capture_command([str(runtime), "-M", str(modules), "-m", "geany_devices",
                                  "simulation.vvp"], work, bus, environment)
        require(expected in report, f"Documented {bus} Lua example did not produce {expected}")


def check_sdram(compiler, runtime, fixtures, directory, environment):
    compile_design(compiler, directory, [fixtures / "sdram_tb.sv"], environment)
    (directory / "sdram.hex").write_text("@c04 beef\n", encoding="ascii")
    modules = runtime.parent.parent / "lib" / "ivl"
    report = capture_command([str(runtime), "-M", str(modules), "-m", "geany_devices",
                              "simulation.vvp"], directory, "sdram", environment)
    require("SDRAM test passed" in report, "SDRAM test did not finish")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("logs", type=Path)
    parser.add_argument("--gtk", action="store_true", help="Open a real GTK window; requires a display")
    args = parser.parse_args()
    suffix = ".exe" if os.name == "nt" else ""
    binaries = args.prefix.resolve() / "bin"
    compiler = binaries / f"iverilog{suffix}"
    runtime = binaries / f"vvp{suffix}"
    editor = binaries / f"geany{suffix}"
    service = args.prefix.resolve() / "libexec/geany-insight/slang" / f"insight-slang-semantic{suffix}"
    require(all(path.is_file() for path in (compiler, runtime, editor)),
            f"Expected installed Geany and Icarus executables under {binaries}")
    if os.name == "nt":
        check_windowless_helpers(args.prefix.resolve(), service)
    fixtures = Path(__file__).resolve().parent
    environment = os.environ.copy()
    environment["GEANY_VVP_DEBUG"] = "1"
    environment["PATH"] = str(binaries) + os.pathsep + environment.get("PATH", "")
    logs = args.logs.resolve()
    checks = [
        ("hdmi-ddc", lambda directory: check_hdmi_ddc(compiler, runtime, fixtures, directory, environment)),
        ("raster-displays", lambda directory: check_raster_displays(compiler, runtime, fixtures, directory, environment)),
        ("parallel-psram", lambda directory: check_parallel_psram(compiler, runtime, fixtures, directory, environment)),
        ("usart", lambda directory: check_usart(compiler, runtime, fixtures, directory, environment)),
        ("bus-controllers", lambda directory: check_bus_controllers(compiler, runtime, fixtures, directory, environment)),
        ("i2c-eeprom", lambda directory: check_i2c_eeprom(compiler, runtime, fixtures, directory, environment)),
        ("lua-device-examples", lambda directory: check_documented_lua_devices(compiler, runtime, fixtures, directory, environment)),
        ("sdram", lambda directory: check_sdram(compiler, runtime, fixtures, directory, environment)),
        ("spi-memory", lambda directory: check_spi_memory(compiler, runtime, fixtures, directory, environment)),
        ("memory-image", lambda directory: check_memory_image(compiler, runtime, fixtures, directory, environment)),
        ("ddr1-device", lambda directory: check_ddr1_device(compiler, runtime, fixtures, directory, environment)),
        ("ddr5-device", lambda directory: check_ddr5_device(compiler, runtime, fixtures, directory, environment)),
        ("ddr4-device", lambda directory: check_ddr4_device(compiler, runtime, fixtures, directory, environment)),
        ("editor", lambda directory: check_editor(editor, directory, environment)),
        ("breakpoint", lambda directory: check_breakpoint(compiler, runtime, fixtures, directory, environment)),
        ("signal-stops", lambda directory: check_signal_stops(compiler, runtime, fixtures, directory, environment)),
        ("source-step", lambda directory: check_source_steps(compiler, runtime, fixtures, directory, environment)),
        ("processes", lambda directory: check_processes(compiler, runtime, fixtures, directory, environment)),
        ("packed-frame", lambda directory: check_packed_frame(compiler, runtime, fixtures, directory, environment)),
        ("synchronous-steps", lambda directory: check_synchronous_steps(
            compiler, runtime, fixtures, directory, environment)),
        ("slang", lambda directory: check_slang(service, fixtures, directory, environment)),
    ]
    if args.gtk:
        checks.append(("gtk", lambda directory: check_gtk_editor(editor, fixtures, directory, environment)))
    failed = False
    for name, check in checks:
        directory = logs / name
        try:
            check(directory)
        except (OSError, UnicodeError, RuntimeError, subprocess.SubprocessError) as error:
            failed = True
            print(f"{name}: {error}. See {directory}", flush=True)
        else:
            print(f"{name}: OK", flush=True)
    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
