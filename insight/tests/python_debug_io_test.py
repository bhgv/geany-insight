#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Compare debugger text-input semantics with StringIO and run an installed-layout helper under Python -I.
# RU: Сравнивает текстовый ввод отладчика со StringIO и проверяет установленную структуру helper при Python -I.
import ast
import importlib.util
import io
import os
from pathlib import Path
import queue
import select
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('debug_io', ROOT / 'insight/tools/python-debug/io.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class Protocol:
    # EN: Initialize self's captured input-request packets without a GUI or a subprocess.
    # RU: Инициализирует сохранённые запросы ввода self без GUI и дочернего процесса.
    def __init__(self):
        self.packets = []

    # EN: Capture packet from the production input stream; reject unexpected protocol messages.
    # RU: Запоминает packet настоящего потока ввода; отклоняет неожиданные сообщения протокола.
    def write(self, packet):
        assert packet == 'I\n'
        self.packets.append(packet)


# EN: Return production input, StringIO reference and protocol for text; queue complete lines then EOF.
# RU: Возвращает настоящий ввод, эталон StringIO и протокол для text; помещает в очередь строки и EOF.
def streams(text):
    lines = queue.Queue()
    for line in text.splitlines(keepends=True):
        lines.put(line)
    lines.put(None)
    protocol = Protocol()
    return MODULE.DebugInput(protocol, lines), io.StringIO(text), protocol


class DebugTextInput(unittest.TestCase):
    # EN: Compare Unicode-sized, bounded, unbounded and mixed reads for self against standard text I/O.
    # RU: Сравнивает чтения Unicode, ограниченные, неограниченные и смешанные операции self со стандартным вводом.
    def test_mixed_reads_match_stringio(self):
        operations = [('read', 0), ('read', 2), ('readline', 1), ('read', 5),
                      ('readline', None), ('read', 1), ('read', None), ('readline', -1), ('read', 3)]
        for text in ('абв\n\n東京🙂z\nlast\n', '', '\n\n', 'last', 'a\r\nb\n'):
            with self.subTest(text=text):
                actual, expected, protocol = streams(text)
                for method, size in operations:
                    self.assertEqual(getattr(actual, method)(size), getattr(expected, method)(size))
                requests = len(protocol.packets)
                self.assertEqual(actual.read(), '')
                self.assertEqual(actual.readline(), '')
                self.assertEqual(len(protocol.packets), requests)

    # EN: Verify self's partial read retains a line's remainder without another input request.
    # RU: Проверяет сохранение остатка строки self после частичного чтения без нового запроса ввода.
    def test_partial_unicode_and_newline(self):
        stream, _, protocol = streams('Я🙂x\n東京\n')
        self.assertEqual(stream.read(0), '')
        self.assertEqual(protocol.packets, [])
        self.assertEqual(stream.read(3), 'Я🙂x')
        self.assertEqual(stream.readline(), '\n')
        self.assertEqual(len(protocol.packets), 1)
        self.assertEqual(stream.read(), '東京\n')
        self.assertEqual(len(protocol.packets), 3)

    # EN: Verify inherited iteration/readlines for self preserve blank lines and stop only at EOF.
    # RU: Проверяет сохранение пустых строк self при итерации/readlines и завершение только по EOF.
    def test_iteration_and_readlines(self):
        for operation in (lambda stream: list(stream), lambda stream: stream.readlines(),
                          lambda stream: stream.readlines(3)):
            actual, expected, _ = streams('a\n\n東京\nz\n')
            self.assertEqual(operation(actual), operation(expected))
            self.assertEqual(actual.read(), expected.read())

    # EN: Verify self's invalid sizes request no input and closed streams reject even zero-length reads.
    # RU: Проверяет отсутствие запросов self при неверных размерах и отказ закрытых потоков даже при нулевом размере.
    def test_invalid_sizes_and_closed_stream(self):
        stream, _, protocol = streams('ab\n')
        for method in ('read', 'readline'):
            for size in (1.5, '1', object()):
                with self.subTest(method=method, size=type(size).__name__):
                    with self.assertRaises(TypeError):
                        getattr(stream, method)(size)
        self.assertEqual(protocol.packets, [])
        self.assertTrue(stream.readable())
        stream.close()
        for operation in (lambda: stream.read(), lambda: stream.read(0), stream.readline, stream.readable):
            with self.assertRaises(ValueError):
                operation()
        self.assertEqual(protocol.packets, [])

    # EN: Verify a cross-line read for self finishes under the stream lock and preserves the next newline.
    # RU: Проверяет завершение чтения self через границу строк под блокировкой и сохранение следующего перевода строки.
    def test_cross_line_read_does_not_deadlock(self):
        stream, _, protocol = streams('a\nb\n')
        result = []
        worker = threading.Thread(target=lambda: result.append(stream.read(3)), daemon=True)
        worker.start()
        worker.join(2)
        self.assertFalse(worker.is_alive())
        self.assertEqual(result, ['a\nb'])
        self.assertEqual(stream.readline(), '\n')
        self.assertEqual(len(protocol.packets), 2)

    # EN: Run a copied installed-layout helper for self under -I from a hostile module directory; check real read/EOF packets.
    # RU: Запускает скопированную установленную структуру helper для self под -I из каталога с ложными модулями; проверяет read/EOF.
    def test_isolated_installed_helper(self):
        if os.name == 'nt':
            self.skipTest('Pipe polling runs in Linux CI; text-stream cases are platform independent.')
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            tools = folder / 'installed-tools'
            (tools / 'python-debug').mkdir(parents=True)
            shutil.copyfile(ROOT / 'insight/tools/python_debug.py', tools / 'python_debug.py')
            shutil.copyfile(ROOT / 'insight/tools/python-debug/io.py', tools / 'python-debug/io.py')
            work = folder / 'project'
            work.mkdir()
            for name in ('io.py', 'python_debug_io.py'):
                (work / name).write_text("raise RuntimeError('project helper shadowed debugger')\n")
            source = work / 'fixture.py'
            source.write_text("import sys\nprefix = sys.stdin.read(3)\nline = sys.stdin.readline()\n"
                              "rest = sys.stdin.read()\nprint(repr((prefix, line, rest)))\n")
            process = subprocess.Popen([sys.executable, '-I', '-B', '-u', str(tools / 'python_debug.py'),
                                        str(source), str(source), '', ''], cwd=work,
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            pending = b''
            output = []

            # EN: Send command to the copied helper; flush immediately to avoid test buffering.
            # RU: Отправляет command скопированному helper; сразу сбрасывает буфер теста.
            def send(command):
                process.stdin.write((command + '\n').encode())
                process.stdin.flush()

            # EN: Wait up to five seconds for kind, collecting real console packets and failing on an error/EOF.
            # RU: Ждёт kind до пяти секунд, собирая настоящие пакеты консоли; ошибка/EOF завершает проверку.
            def until(kind):
                nonlocal pending
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    if b'\n' not in pending:
                        ready, _, _ = select.select([process.stdout], [], [], max(0, deadline - time.monotonic()))
                        self.assertTrue(ready, 'Protocol timeout: ' + kind)
                        chunk = os.read(process.stdout.fileno(), 65536)
                        self.assertTrue(chunk, 'Unexpected helper EOF')
                        pending += chunk
                    if b'\n' not in pending:
                        continue
                    line, pending = pending.split(b'\n', 1)
                    fields = line.decode().split('\t')
                    self.assertNotEqual(fields[0], 'E', line.decode())
                    if fields[0] == 'O':
                        output.append(bytes.fromhex(fields[1]).decode())
                    if fields[0] == kind:
                        return
                self.fail('Protocol timeout: ' + kind)

            try:
                send('into')
                until('P')
                send('continue')
                until('I')
                send('input ' + 'Я🙂x'.encode().hex())
                until('I')
                send('input ' + '東京'.encode().hex())
                until('I')
                send('eof')
                until('D')
                self.assertEqual(ast.literal_eval(''.join(output).strip()), ('Я🙂x', '\n', '東京\n'))
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=5)


if __name__ == '__main__':
    unittest.main()
