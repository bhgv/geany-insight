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
        simulator.send("cont")
        require(simulator.process.wait(timeout=30) == 0, "Counter simulation failed")
    finally:
        simulator.close()
    check_capture(directory / "counter.vcd", 25, "count")


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


# EN: Run the checks against binaries installed by either native build launcher.
# RU: Запускаем проверки программ, установленных любым штатным сборщиком.
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
    fixtures = Path(__file__).resolve().parent
    environment = os.environ.copy()
    environment["GEANY_VVP_DEBUG"] = "1"
    environment["PATH"] = str(binaries) + os.pathsep + environment.get("PATH", "")
    logs = args.logs.resolve()
    checks = [
        ("editor", lambda directory: check_editor(editor, directory, environment)),
        ("breakpoint", lambda directory: check_breakpoint(compiler, runtime, fixtures, directory, environment)),
        ("source-step", lambda directory: check_source_steps(compiler, runtime, fixtures, directory, environment)),
        ("processes", lambda directory: check_processes(compiler, runtime, fixtures, directory, environment)),
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
