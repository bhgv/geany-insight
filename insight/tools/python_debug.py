#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Trace a Python buffer for the native Geany Insight debugger."""

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
    def __init__(self, filename, commands, output):
        self.filename = filename
        self.commands = commands
        self.output = output
        self.mode = "into"
        self.depth = 0
        self.breakpoints = set()

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
            self.breakpoints = {int(value) for value in fields[1:] if value.isdecimal()}
            return

    # Stop only in the captured buffer, preserving the full call depth.
    # Останавливается в исходном буфере с учётом полной глубины стека.
    def trace(self, frame, event, _argument):
        if frame.f_code.co_filename != self.filename:
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
        if (pause or frame.f_lineno in self.breakpoints or self.mode == "into" or
                (self.mode == "over" and depth <= self.depth) or
                (self.mode == "out" and depth < self.depth)):
            graph = WatchSnapshot(frame)
            fields = ["P", str(frame.f_lineno), encode(stack_text(frame))]
            fields.extend(graph.fields())
            self.output.write("\t".join(fields) + "\n")
            self.output.flush()
            self.next_action(depth)
        return self.trace


# Execute the buffer in a separate process; keep script output off the protocol.
# Выполняет буфер в отдельном процессе и отделяет его вывод от протокола.
def main():
    snapshot, filename = sys.argv[1:3]
    output = sys.stdout
    control = sys.stdin
    sys.stdout = sys.__stdout__ = sys.stderr
    sys.stdin = open(os.devnull, "r")
    commands = queue.Queue()
    reader = threading.Thread(target=read_commands, args=(control.fileno(), commands), daemon=True)
    reader.start()
    debugger = Debugger(filename, commands, output)
    try:
        with open(snapshot, encoding="utf-8") as stream:
            program = compile(stream.read(), filename, "exec", dont_inherit=True)
        debugger.next_action(0)
        if os.path.isabs(filename):
            sys.path.insert(0, os.path.dirname(filename))
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
        output.flush()


if __name__ == "__main__":
    main()
