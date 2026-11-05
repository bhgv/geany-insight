#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Trace a Python buffer for the native Geany Insight debugger."""

import importlib.machinery
import io
import json
import os
import queue
import sys
import threading
import types

MAX_NODES = 96
MAX_EDGES = 224
MAX_DEPTH = 6
MAX_MEMBERS = 16


# Encode a protocol field without allowing a value to introduce delimiters.
# Кодирует поле протокола, чтобы значение не могло добавить разделители.
def encode(text):
    return text.encode("utf-8", errors="replace").hex()


# Format built-in scalars without calling user-defined repr or properties.
# Форматирует встроенные скаляры без пользовательских repr и свойств.
def value_text(value):
    kind = type(value)
    if kind is str:
        return repr(value[:64])[:100]
    if kind is bytes:
        return repr(value[:32])[:100]
    if kind is int:
        return str(value) if value.bit_length() <= 256 else "<large int>"
    if any(kind is item for item in (type(None), bool, float, complex)):
        return repr(value)
    if any(kind is item for item in (dict, list, tuple)):
        return "{} ({})".format(kind.__name__, len(value))
    if kind is types.FunctionType:
        return "function (line {})".format(value.__code__.co_firstlineno)
    return "<object>"


# Describe a key without running methods of a user-defined key type.
# Описывает ключ без вызова методов пользовательского типа.
def key_text(key):
    if type(key) is str:
        return json.dumps(key[:64], ensure_ascii=True)[:94]
    if any(type(key) is item for item in (int, float, bool, type(None))):
        return "[{}]".format(value_text(key))
    return "[object {:x}]".format(id(key))


class WatchSnapshot:
    # Start the graph at the selected Python frame.
    # Начинает граф с выбранного кадра Python.
    def __init__(self, frame):
        self.nodes = [("Current frame", "", "")]
        self.edges = []
        self.identities = {}
        self.limited = False
        locals_map = frame.f_locals
        for prefix, names in (("local ", locals_map), ("global ", frame.f_globals)):
            count = 0
            for name, value in names.items():
                if type(name) is not str or name.startswith("__"):
                    continue
                if prefix == "global " and name in locals_map:
                    continue
                if count == 32:
                    self.limited = True
                    break
                self.add(0, prefix + name, value, 0)
                count += 1

    # Record shared references and expand only inspectable built-in objects.
    # Сохраняет общие ссылки и раскрывает только безопасные встроенные объекты.
    def add(self, parent, label, value, depth):
        if len(self.edges) >= MAX_EDGES:
            self.limited = True
            return
        kind = type(value)
        expandable = any(kind is item for item in (dict, list, tuple, types.FunctionType))
        identity = id(value) if expandable else None
        known = self.identities.get(identity) if expandable else None
        label = label.replace("\x1f", "\\x1f").encode("utf-8", "replace")[:94].decode("utf-8", "ignore")
        if known is not None:
            self.edges.append((parent, known, label))
            return
        if len(self.nodes) >= MAX_NODES:
            self.limited = True
            return

        target = len(self.nodes)
        parent_path = self.nodes[parent][2]
        path = parent_path + "\x1f" + label if parent_path or parent == 0 else ""
        if len(path.encode("utf-8", errors="replace")) >= 640:
            path = ""
        self.nodes.append((label, value_text(value), path))
        self.edges.append((parent, target, label))
        if not expandable:
            return
        self.identities[identity] = target
        if depth >= MAX_DEPTH:
            self.limited = True
            return

        if kind is dict:
            members = ((key_text(key), member) for key, member in value.items())
        elif kind is list or kind is tuple:
            members = (("[{}]".format(index), member) for index, member in enumerate(value))
        else:
            members = self.closure_members(value)
        for index, (member_label, member) in enumerate(members):
            if index == MAX_MEMBERS:
                self.limited = True
                break
            self.add(target, member_label, member, depth + 1)

    # Read closure cells directly; empty cells have no value to display.
    # Читает ячейки замыкания напрямую, пропуская пустые ячейки.
    @staticmethod
    def closure_members(function):
        for name, cell in zip(function.__code__.co_freevars, function.__closure__ or ()):
            try:
                yield "upvalue " + name, cell.cell_contents
            except ValueError:
                continue

    # Serialize one bounded snapshot as a single protocol line.
    # Сериализует ограниченный снимок в одну строку протокола.
    def fields(self):
        yield str(len(self.nodes))
        yield str(len(self.edges))
        yield str(int(self.limited))
        for label, value, path in self.nodes:
            yield encode(label)
            yield encode(value)
            yield encode(path)
        for source, target, label in self.edges:
            yield str(source)
            yield str(target)
            yield encode(label)


# Read control commands in parallel so Pause can interrupt a Python loop.
# Читает команды параллельно, чтобы Pause мог остановить цикл Python.
def read_commands(descriptor, commands):
    pending = b""
    while True:
        chunk = os.read(descriptor, 4096)
        if not chunk:
            break
        lines = (pending + chunk).split(b"\n")
        pending = lines.pop()
        for line in lines:
            commands.put(line.decode("ascii", errors="replace").rstrip("\r"))
    commands.put("quit")


# Collect frame names and raw values for the editor's stack pane.
# Собирает имена кадров и значения для панели стека редактора.
def stack_text(frame):
    rows = []
    for level in range(16):
        if frame is None:
            break
        rows.append("#{} {} ({}:{})".format(
            level, frame.f_code.co_name, frame.f_code.co_filename, frame.f_lineno))
        if level == 0:
            for index, (name, value) in enumerate(frame.f_locals.items()):
                if index == 64:
                    break
                rows.append("    {} = {}".format(name, value_text(value)))
        frame = frame.f_back
    return "\n".join(rows)[:8191]


class Debugger:
    # Track the current step action and the original source filename.
    # Хранит текущий шаг и имя исходного файла.
    def __init__(self, filename, project_root, commands, output):
        self.filename = filename
        self.project_root = self.file_key(project_root) if project_root else None
        self.file_keys = {filename: self.file_key(filename)}
        self.traceable = {filename: True}
        self.commands = commands
        self.output = output
        self.mode = "into"
        self.depth = 0
        self.breakpoints = set()
        self.breakpoint_files = set()

    # Normalize filenames the same way for trace frames and breakpoints.
    # Нормализует имена файлов одинаково для кадров и точек останова.
    @staticmethod
    def file_key(filename):
        if not filename or filename.startswith("<"):
            return filename
        return os.path.normcase(os.path.abspath(filename))

    # Trace the entry file, the project directory and explicitly selected files.
    # Отслеживает точку входа, каталог проекта и явно выбранные файлы.
    def should_trace(self, filename):
        known = self.traceable.get(filename)
        if known is not None:
            return known
        path = self.file_key(filename)
        self.file_keys[filename] = path
        traced = path in self.breakpoint_files
        if not traced and self.project_root and path and not path.startswith("<"):
            try:
                traced = os.path.commonpath((self.project_root, path)) == self.project_root
            except ValueError:
                pass
        self.traceable[filename] = traced
        return traced

    # Apply the next step command at a source stop.
    # Применяет следующую команду шага на остановке в исходнике.
    def next_action(self, depth):
        while True:
            fields = self.commands.get().split()
            if not fields or fields[0] == "pause":
                continue
            if fields[0] == "quit":
                raise SystemExit(0)
            self.mode = fields[0]
            self.depth = depth
            self.breakpoints = set()
            for value in fields[1:]:
                path_hex, separator, line = value.partition(":")
                if not separator or not line.isdecimal():
                    continue
                try:
                    path = bytes.fromhex(path_hex).decode("utf-8")
                    self.breakpoints.add((self.file_key(path), int(line)))
                except (ValueError, UnicodeError):
                    continue
            self.breakpoint_files = {path for path, _line in self.breakpoints}
            self.traceable = {self.filename: True}
            return

    # Stop in tracked files while preserving the full call depth.
    # Останавливается в отслеживаемых файлах с учётом полной глубины стека.
    def trace(self, frame, event, _argument):
        if not self.should_trace(frame.f_code.co_filename):
            return None
        if event != "line":
            return self.trace
        pause = False
        try:
            command = self.commands.get_nowait()
            if command == "quit":
                raise SystemExit(0)
            pause = command == "pause"
        except queue.Empty:
            pass

        depth, parent = 0, frame
        while parent is not None:
            depth += 1
            parent = parent.f_back
        filename = frame.f_code.co_filename
        position = (self.file_keys[filename], frame.f_lineno)
        if (pause or position in self.breakpoints or self.mode == "into" or
                (self.mode == "over" and depth <= self.depth) or
                (self.mode == "out" and depth < self.depth)):
            graph = WatchSnapshot(frame)
            fields = ["P", str(frame.f_lineno), encode(filename), encode(stack_text(frame))]
            fields.extend(graph.fields())
            self.output.write("\t".join(fields) + "\n")
            self.next_action(depth)
        return self.trace


# Read private length-prefixed snapshots created by the editor.
# Читает приватные снимки редактора с явно указанной длиной полей.
def read_overlays(filename):
    overlays = {}
    if not filename:
        return overlays
    with open(filename, "rb") as stream:
        while True:
            header = stream.readline(128)
            if not header:
                break
            path_length, source_length = map(int, header.split())
            if not 0 < path_length <= 32768 or not 0 <= source_length <= 2 * 1024 * 1024:
                raise ValueError("invalid Python overlay length")
            path = stream.read(path_length)
            source = stream.read(source_length)
            if len(path) != path_length or len(source) != source_length:
                raise ValueError("truncated Python overlay")
            overlays[Debugger.file_key(path.decode("utf-8"))] = source.decode("utf-8")
    return overlays


class OverlaySourceLoader(importlib.machinery.SourceFileLoader):
    # Keep the module's real path and compile the captured text instead of its pyc.
    # Сохраняет реальный путь модуля и компилирует снимок вместо его pyc.
    def __init__(self, name, filename, source):
        super().__init__(name, filename)
        self.source = source

    # Supply the captured code without touching the on-disk cache.
    # Возвращает код снимка без записи кэша на диск.
    def get_code(self, fullname):
        return compile(self.source, self.get_filename(fullname), "exec", dont_inherit=True)

    # Let standard source inspection see the same text that was executed.
    # Возвращает инструментам просмотра тот же текст, который выполнялся.
    def get_source(self, fullname):
        return self.source


class OverlayFinder:
    # Keep the standard import search and substitute only matching source loaders.
    # Сохраняет обычный поиск импорта и подменяет только совпавшие загрузчики.
    def __init__(self, overlays):
        self.overlays = overlays

    # Replace the loader of a matching module or package, preserving its spec.
    # Подменяет загрузчик совпавшего модуля или пакета, сохраняя его spec.
    def find_spec(self, fullname, path=None, target=None):
        spec = importlib.machinery.PathFinder.find_spec(fullname, path, target)
        if spec and type(spec.loader) is importlib.machinery.SourceFileLoader:
            source = self.overlays.get(Debugger.file_key(spec.origin))
            if source is not None:
                spec.loader = OverlaySourceLoader(fullname, spec.origin, source)
                return spec
        return None


class Protocol:
    # Serialize output and debugger packets, including writes from other threads.
    # Упорядочивает вывод и пакеты отладчика, включая записи из других потоков.
    def __init__(self, stream):
        self.stream = stream
        self.lock = threading.Lock()
        self.remaining = 256 * 1024

    # Send one complete debugger packet.
    # Отправляет один целый пакет отладчика.
    def write(self, packet):
        with self.lock:
            self.stream.write(packet)
            self.stream.flush()

    # Forward a bounded amount of user output as separate packets.
    # Передаёт ограниченный объём вывода программы отдельными пакетами.
    def console(self, text):
        with self.lock:
            if not self.remaining:
                return
            encoded = text[:self.remaining].encode("utf-8", errors="replace")
            clipped = len(encoded) > self.remaining or len(text) > self.remaining
            payload = encoded[:self.remaining].decode("utf-8", errors="ignore")
            self.remaining -= min(len(encoded), self.remaining)
            for start in range(0, len(payload), 2048):
                self.stream.write("O\t" + encode(payload[start:start + 2048]) + "\n")
            if clipped or not self.remaining:
                self.remaining = 0
                self.stream.write("O\t" + encode("\n[Python output limit reached]\n") + "\n")
            self.stream.flush()


class DebugConsole(io.TextIOBase):
    # Connect a Python text stream to the editor console.
    # Подключает текстовый поток Python к консоли редактора.
    def __init__(self, protocol):
        self.protocol = protocol

    # Report the stream as writable for Python's text I/O callers.
    # Объявляет поток доступным для записи через текстовый ввод-вывод Python.
    def writable(self):
        return True

    # Keep print() and logging output separate from debugger control packets.
    # Отделяет print() и журналирование от управляющих пакетов отладчика.
    def write(self, text):
        if not isinstance(text, str):
            raise TypeError("expected text")
        if text:
            self.protocol.console(text)
        return len(text)

# Execute the buffer in a separate process; keep script output off the protocol.
# Выполняет буфер в отдельном процессе и отделяет его вывод от протокола.
def main():
    snapshot, filename, project_root, overlay_file = sys.argv[1:5]
    output = Protocol(sys.stdout)
    control = sys.stdin
    console = DebugConsole(output)
    sys.stdout = sys.__stdout__ = console
    sys.stderr = sys.__stderr__ = console
    sys.stdin = open(os.devnull, "r")
    commands = queue.Queue()
    reader = threading.Thread(target=read_commands, args=(control.fileno(), commands), daemon=True)
    reader.start()
    debugger = Debugger(filename, project_root, commands, output)
    try:
        with open(snapshot, encoding="utf-8") as stream:
            program = compile(stream.read(), filename, "exec", dont_inherit=True)
        debugger.next_action(0)
        if os.path.isabs(filename):
            sys.path.insert(0, os.path.dirname(filename))
        overlays = read_overlays(overlay_file)
        if overlays:
            sys.meta_path.insert(0, OverlayFinder(overlays))
        sys.argv = [filename]
        scope = {"__name__": "__main__", "__file__": filename, "__package__": None}
        sys.settrace(debugger.trace)
        exec(program, scope)
        output.write("D\n")
    except SystemExit:
        output.write("D\n")
    except BaseException as error:
        name = type(error).__name__
        if type(error) is SyntaxError:
            name += ": " + error.msg
        elif type(error) in (NameError, TypeError, ValueError, RuntimeError, ZeroDivisionError):
            if error.args and type(error.args[0]) is str:
                name += ": " + error.args[0][:1024]
        output.write("E\t" + encode(name) + "\n")
    finally:
        sys.settrace(None)


if __name__ == "__main__":
    main()
