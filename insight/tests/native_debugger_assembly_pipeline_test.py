#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Drive the production asynchronous naming pipeline against real GDB without ptrace or GTK.
# RU: Выполняет настоящий асинхронный конвейер имён с GDB без ptrace и GTK.
import ctypes.util
import os
from pathlib import Path
import select
import shutil
import subprocess
import tempfile
import time
import unittest

from native_debugger_names_fixture import model_source

SESSION = r'''
typedef struct {NativeAssembly *assembly; gboolean stopped; void *window; char *selected_thread, *selected_frame;} NativeDebug;
'''
STUBS = r'''
extern int g_queue_is_empty(GQueue *);
#define g_clear_pointer(p,f) do {if (*(p)) f(*(p)); *(p)=NULL;} while (0)
/* EN: Clear work queue using destroy, matching the production compatibility helper.
 * RU: Очищает очередь queue через destroy аналогично производственной совместимой функции. */
static void insight_queue_clear_full(GQueue *queue, void (*destroy)(void *))
{
    void *item;
    while ((item = g_queue_pop_head(queue)))
        destroy(item);
}
/* EN: Emit command for the driver to send to real GDB; dialog is unused in this headless transport.
 * RU: Выводит command для передачи настоящему GDB; dialog не используется в невизуальном транспорте. */
static void native_debug_send(void *dialog, const char *command)
{
    (void)dialog;
    printf("COMMAND %s\n", command);
    fflush(stdout);
}
/* EN: Keep the real final formatter but replace GTK output with stdout for session's driver.
 * RU: Сохраняет настоящий форматтер, заменяя GTK выводом в stdout для драйвера session. */
static void native_assembly_finish(NativeDebug *session)
{
    char *text = native_assembly_render(session, session->assembly->record);
    assert(text && strstr(text, "packet.inner.count"));
    printf("RESULT %s\n", text);
    g_free(text);
    fflush(stdout);
}
/* EN: Ignore UI refresh for session; no GUI behavior is claimed by this transport fixture.
 * RU: Пропускает обновление UI session; эта транспортная фикстура не проверяет GUI. */
static void native_debug_update(NativeDebug *session)
{
    (void)session;
}
'''
MAIN = r'''
/* EN: Create one typed-data rule, process MI records from stdin and emit the actual pipeline commands.
 * RU: Создаёт правило типа данных, обрабатывает записи MI из stdin и выдаёт настоящие команды конвейера. */
int main(void)
{
    NativeAssembly *assembly = g_new0(NativeAssembly, 1);
    assembly->rules = g_ptr_array_new_with_free_func(native_name_rule_free);
    assembly->ranges = g_ptr_array_new_with_free_func(native_name_range_free);
    assembly->objects = g_ptr_array_new_with_free_func(g_free);
    assembly->registers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    NativeDebug session = {.assembly = assembly, .stopped = TRUE};
    NativeNameRule *rule = g_new0(NativeNameRule, 1);
    rule->kind = 2;
    rule->anchor = g_strdup("&object");
    rule->name = g_strdup("packet");
    rule->type = g_strdup("struct Packet");
    rule->scope = g_strdup("");
    g_ptr_array_add(assembly->rules, rule);
    native_assembly_prepare(&session, native_assembly_command("main"), "main");
    assert(native_assembly_next(&session));
    char line[65536];
    while (fgets(line, sizeof line, stdin))
    {
        line[strcspn(line, "\r\n")] = 0;
        native_assembly_console(&session, line);
        if (line[0] == '^')
        {
            assert(native_assembly_reply(&session, line));
            if (!native_assembly_next(&session))
                break;
        }
    }
    assert(!assembly->active && !assembly->objects->len && !assembly->work.length);
    native_assembly_free(assembly);
    return 0;
}
'''


class NativeAssemblyPipeline(unittest.TestCase):
    # EN: Relay real MI command/reply traffic through production C helpers and verify nested fields and cleanup.
    # RU: Передаёт настоящий обмен MI через производственные функции C и проверяет вложенные поля и очистку.
    def test_live_metadata_pipeline(self):
        gdb = os.environ.get('GDB') or shutil.which('gdb')
        libraries = [ctypes.util.find_library(name) for name in ('gio-2.0', 'glib-2.0')]
        if not gdb or not all(libraries) or not shutil.which('cc'):
            self.skipTest('Needs GDB, cc and GLib/GIO runtimes.')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = model_source(SESSION) + STUBS
            code += Path('src/insight-debuggers/disassembly/pipeline.inc').read_text() + MAIN
            source = root / 'pipeline.c'
            source.write_text(code)
            helper = root / 'pipeline'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(source), '-o', str(helper), *['-l:' + name for name in libraries]], check=True)
            target_source = root / 'layout.c'
            target_source.write_text('struct Inner { int count; }; struct Packet { char tag; struct Inner inner; };\n'
                                     'struct Packet object; int main(void) { return object.inner.count; }\n'
)
            target = root / 'layout'
            subprocess.run(['cc', '-g', '-O0', str(target_source), '-o', str(target)], check=True)
            self.relay(helper, gdb, target)

    # EN: Forward commands and records between helper and debugger, with a 20-second bound and guaranteed cleanup.
    # RU: Передаёт команды и записи между helper и debugger с ограничением 20 секунд и гарантированной очисткой.
    def relay(self, helper, gdb, target):
        driver = subprocess.Popen([str(helper)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, bufsize=0)
        debugger = subprocess.Popen([gdb, '-q', '-nx', '--interpreter=mi2', str(target)],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        buffers = {driver.stdout: b'', debugger.stdout: b''}
        commands, result = [], b''
        deadline = time.monotonic() + 20
        try:
            while time.monotonic() < deadline:
                ready, _, _ = select.select(list(buffers), [], [], 0.1)
                for stream in ready:
                    chunk = os.read(stream.fileno(), 65536)
                    if not chunk:
                        buffers.pop(stream)
                        continue
                    buffers[stream] += chunk
                    while b'\n' in buffers[stream]:
                        line, buffers[stream] = buffers[stream].split(b'\n', 1)
                        if stream is driver.stdout and line.startswith(b'COMMAND '):
                            command = line[8:]
                            commands.append(command)
                            debugger.stdin.write(command + b'\n')
                        elif stream is driver.stdout:
                            result += line + b'\n'
                        elif line.startswith((b'^', b'~')):
                            driver.stdin.write(line + b'\n')
                if driver.poll() is not None:
                    break
            self.assertEqual(driver.wait(timeout=2), 0, driver.stderr.read().decode())
            self.assertIn(b'packet.inner.count', result)
            self.assertGreater(sum(command.startswith(b'-data-disassemble') for command in commands), 1)
            self.assertTrue(any(b'-var-delete' in command for command in commands))
            self.assertTrue(any(b'-gdb-set disassembly-flavor intel' in command for command in commands))
        finally:
            for process in (driver, debugger):
                if process.poll() is None:
                    process.kill()
                process.wait()
                for stream in (process.stdin, process.stdout, process.stderr):
                    stream.close()


if __name__ == '__main__':
    unittest.main()
