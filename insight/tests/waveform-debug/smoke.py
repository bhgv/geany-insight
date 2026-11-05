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


# EN: Verify the installed bridge stops on a condition before the assignment.
# RU: Проверяем условную остановку установленного моста до присваивания.
def check_breakpoint(compiler, runtime, fixtures, directory, environment):
    source = fixtures / "counter.v"
    target = [number for number, line in enumerate(source.read_text().splitlines(), 1)
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


# EN: Run the checks against binaries installed by either native build launcher.
# RU: Запускаем проверки программ, установленных любым штатным сборщиком.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("logs", type=Path)
    args = parser.parse_args()
    suffix = ".exe" if os.name == "nt" else ""
    binaries = args.prefix.resolve() / "bin"
    compiler = binaries / f"iverilog{suffix}"
    runtime = binaries / f"vvp{suffix}"
    require(all(path.is_file() for path in (compiler, runtime, binaries / f"geany{suffix}")),
            f"Expected installed Geany and Icarus executables under {binaries}")
    fixtures = Path(__file__).resolve().parent
    environment = os.environ.copy()
    environment["GEANY_VVP_DEBUG"] = "1"
    environment["PATH"] = str(binaries) + os.pathsep + environment.get("PATH", "")
    logs = args.logs.resolve()
    for name, check in (("breakpoint", check_breakpoint), ("processes", check_processes)):
        directory = logs / name
        try:
            check(compiler, runtime, fixtures, directory, environment)
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            raise SystemExit(f"{name}: {error}. See {directory}") from error
        print(f"{name}: OK")


if __name__ == "__main__":
    main()
