# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Own the debugger packet writer, command router and Python text-console streams.
# RU: Реализует запись пакетов отладчика, маршрутизацию команд и текстовые потоки консоли Python.
import io
import operator
import os
import threading


# EN: Return delimiter-safe UTF-8 hex for text, replacing unencodable code points.
# RU: Возвращает UTF-8 hex строки text без разделителей, заменяя некодируемые символы.
def encode(text):
    return text.encode("utf-8", errors="replace").hex()


# EN: Route descriptor commands to commands, decoded input to input_lines and observer decisions to observer_replies.
# EOF wakes all queues; malformed encoded input is routed as EOF. Read errors propagate.
# RU: Направляет команды descriptor в commands, декодированный ввод в input_lines, решения наблюдателя в observer_replies.
# EOF пробуждает все очереди; неверно закодированный ввод считается EOF. Ошибки чтения передаются вызывающему.
def read_commands(descriptor, commands, input_lines, observer_replies):
    pending = b""
    while True:
        chunk = os.read(descriptor, 4096)
        if not chunk:
            break
        lines = (pending + chunk).split(b"\n")
        pending = lines.pop()
        for line in lines:
            command = line.decode("ascii", errors="replace").rstrip("\r")
            if command.startswith("input "):
                try:
                    input_lines.put(bytes.fromhex(command[6:]).decode("utf-8") + "\n")
                except ValueError:
                    input_lines.put(None)
            elif command.startswith("observe "):
                observer_replies.put(command[8:])
            elif command == "eof":
                input_lines.put(None)
            else:
                commands.put(command)
    input_lines.put(None)
    observer_replies.put("off")
    commands.put("quit")


class Protocol:
    # EN: Initialize self with output stream, a serialization lock and a bounded console budget.
    # RU: Инициализирует self потоком stream, блокировкой упорядочивания и ограничением вывода консоли.
    def __init__(self, stream):
        self.stream = stream
        self.lock = threading.Lock()
        self.remaining = 256 * 1024

    # EN: Write and flush packet atomically through self; underlying stream errors propagate.
    # RU: Атомарно записывает и сбрасывает packet через self; ошибки исходного потока передаются вызывающему.
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
    # EN: Connect self to the editor console protocol without writing output.
    # RU: Подключает self к протоколу консоли protocol без записи вывода.
    def __init__(self, protocol):
        self.protocol = protocol

    # EN: Return True for self to advertise text-write support.
    # RU: Возвращает True, объявляя поддержку текстовой записи self.
    def writable(self):
        return True

    # EN: Write text through self and return its character count; non-text values raise TypeError, I/O errors propagate.
    # RU: Записывает text через self и возвращает число символов; нетекстовые значения вызывают TypeError, ошибки I/O передаются.
    def write(self, text):
        if not isinstance(text, str):
            raise TypeError("expected text")
        if text:
            self.protocol.console(text)
        return len(text)

class DebugInput(io.TextIOBase):
    # EN: Connect self to protocol and queued text lines; retain unread characters and sticky EOF per process.
    # RU: Подключает self к protocol и очереди lines; хранит непрочитанные символы и EOF в пределах процесса.
    def __init__(self, protocol, lines):
        self.protocol = protocol
        self.lines = lines
        self.pending = ""
        self.eof = False
        self.lock = threading.Lock()

    # EN: Return True for self's text input; a closed stream raises ValueError.
    # RU: Возвращает True для текстового ввода self; закрытый поток вызывает ValueError.
    def readable(self):
        if self.closed:
            raise ValueError("I/O operation on closed stream")
        return True

    # EN: Read one bounded line from self while its lock is held; size counts characters, not UTF-8 bytes.
    # Request input only when the saved remainder is empty; EOF returns an empty string without another request.
    # RU: Читает ограниченную строку self под его блокировкой; size считает символы, а не байты UTF-8.
    # Запрашивает ввод только при пустом остатке; EOF возвращает пустую строку без нового запроса.
    def _readline_locked(self, size):
        if self.closed:
            raise ValueError("I/O operation on closed stream")
        if size == 0 or self.eof:
            return ""
        if not self.pending:
            self.protocol.write("I\n")
            line = self.lines.get()
            if line is None:
                self.eof = True
                return ""
            self.pending = line
        limit = len(self.pending) if size < 0 else min(size, len(self.pending))
        newline = self.pending.find("\n", 0, limit)
        if newline >= 0:
            limit = newline + 1
        text, self.pending = self.pending[:limit], self.pending[limit:]
        return text

    # EN: Return at most size characters of one line from self; None/negative sizes mean the whole line.
    # Preserve remainders, serialize readers, and reject invalid sizes or closed streams with standard exceptions.
    # RU: Возвращает не более size символов строки self; None/отрицательный размер означает всю строку.
    # Сохраняет остаток, упорядочивает читателей; неверный размер и закрытый поток вызывают стандартные исключения.
    def readline(self, size=-1):
        size = -1 if size is None else operator.index(size)
        with self.lock:
            return self._readline_locked(size)

    # EN: Return size characters from self across lines, or all input through EOF for None/negative sizes.
    # Count Unicode characters, preserve unused text and avoid input requests for read(0); closed streams raise ValueError.
    # RU: Возвращает size символов self через границы строк либо весь ввод до EOF при None/отрицательном размере.
    # Считает символы Unicode, сохраняет остаток; read(0) не запрашивает ввод, закрытый поток вызывает ValueError.
    def read(self, size=-1):
        size = -1 if size is None else operator.index(size)
        with self.lock:
            if self.closed:
                raise ValueError("I/O operation on closed stream")
            parts = []
            remaining = size
            while remaining != 0:
                text = self._readline_locked(remaining)
                if not text:
                    break
                parts.append(text)
                if remaining > 0:
                    remaining -= len(text)
            return "".join(parts)
