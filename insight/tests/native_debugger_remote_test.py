#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Execute production SSH argument and source-mapping builders against real GLib without network access.
# RU: Выполняет производственные построители аргументов SSH и сопоставлений путей с GLib без сети.
import ctypes.util
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time
import unittest

from native_debugger_models_test import function

DECLARATIONS = r'''
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
typedef char gchar;
typedef unsigned int guint;
typedef unsigned long guint64;
#define G_MAXINT 2147483647
extern guint64 g_ascii_strtoull(const char *, char **, unsigned);
typedef int gboolean;
typedef struct {void **pdata; guint len;} GPtrArray;
typedef struct {void *head, *tail; guint length;} GQueue;
#define FALSE 0
#define TRUE 1
#define G_N_ELEMENTS(a) (sizeof(a)/sizeof((a)[0]))
#define g_ascii_isalnum(c) isalnum((unsigned char)(c))
extern void g_free(void *);
extern char *g_strdup(const char *);
extern char *g_strdup_printf(const char *, ...);
extern char *g_strescape(const char *, const char *);
extern GPtrArray *g_ptr_array_new_with_free_func(void (*)(void *));
extern void g_ptr_array_add(GPtrArray *, void *);
extern void **g_ptr_array_free(GPtrArray *, int);
extern void g_strfreev(char **);
extern void g_queue_push_tail(GQueue *, void *);
extern void *g_queue_pop_head(GQueue *);
'''

STOP_DECLARATIONS = r'''
typedef size_t gsize;
typedef long gssize;
#define GTK_LABEL(a) (a)
#define insight_tr(a) (a)
extern char *g_strndup(const char *, size_t);
extern char *g_strcompress(const char *);
extern int g_strcmp0(const char *, const char *);
typedef enum {NATIVE_REMOTE_CLEAR_STOP, NATIVE_REMOTE_DETACH, NATIVE_REMOTE_DISCONNECT} NativeRemoteRequest;
typedef struct {
    guint attached_pid;
    gboolean pause_requested, interrupt_pending, clear_stop_signal, busy, detach_needed, disconnect_needed;
    NativeRemoteRequest pending;
    void *status;
} NativeRemote;
typedef struct {NativeRemote *remote;} NativeDebug;
/* EN: Ignore label and text in the headless state fixture; the production helper still takes its real branch.
 * RU: Игнорирует label и text в невизуальной фикстуре; производственная функция выполняет настоящую ветвь. */
static void gtk_label_set_text(void *label, const char *text)
{
    (void)label;
    (void)text;
}
'''

MAIN = r'''
/* EN: Validate argv boundaries, IPv6 forwarding, non-shell quoting and private mapping cleanup.
 * RU: Проверяет границы argv, IPv6, отсутствие оболочки и очистку только собственного сопоставления. */
int main(void)
{
    NativeRemote remote = {.attached_pid = 123, .pause_requested = TRUE, .interrupt_pending = TRUE};
    NativeDebug session = {.remote = &remote};
    native_attach_stopped(&session, "*stopped,signal-name=\"SIGUSR1\"");
    assert(!remote.interrupt_pending && remote.pause_requested && !remote.clear_stop_signal);
    native_attach_stopped(&session, "*stopped,signal-name=\"SIGSTOP\"");
    assert(!remote.pause_requested && remote.clear_stop_signal);
    remote.detach_needed = TRUE;
    char *queued = native_remote_next(&session);
    assert(!strcmp(queued, "-interpreter-exec console \"queue-signal 0\""));
    g_free(queued);
    queued = native_remote_next(&session);
    assert(!strcmp(queued, "-target-detach"));
    g_free(queued);
    native_attach_stopped(&session, "*stopped,signal-name=\"SIGSTOP\"");
    assert(!remote.clear_stop_signal);
    assert(!native_attach_signal_line(0, "127.0.0.1"));
    assert(!native_attach_signal_line(123, "other-host"));
    assert(!native_attach_signal_line(123, NULL));
    char *signal_line = native_attach_signal_line(123, "::1");
    assert(signal_line && !strcmp(signal_line, "pause:123\n"));
    g_free(signal_line);
    const char *invalid_pids[] = {NULL, "", "0", "-1", "+1", "1;quit", "1\n", " 1", "2147483648", "99999999999999999999"};
    for (unsigned i = 0; i < G_N_ELEMENTS(invalid_pids); i++)
        assert(!native_attach_command(invalid_pids[i]));
    char *attach = native_attach_command("0000000123");
    assert(attach && !strcmp(attach, "-target-attach 123"));
    g_free(attach);
    attach = native_attach_command("2147483647");
    assert(attach && !strcmp(attach, "-target-attach 2147483647"));
    g_free(attach);
    const char *invalid[] = {"", "-oProxyCommand=evil", "host;touch file", "host\nother", "user@host", "a b", "$(cmd)"};
    for (unsigned i = 0; i < G_N_ELEMENTS(invalid); i++)
        assert(!native_remote_host_valid(invalid[i]));
    assert(native_remote_host_valid("build-host"));
    assert(native_remote_host_valid("2001:db8::1"));
    char **args = native_remote_argv("/tools with spaces/ssh", "build-host", "domain\\user", 2222,
        "/key with spaces", "/config with spaces", "::1", 2345, 4567, FALSE);
    assert(args && !strcmp(args[0], "/tools with spaces/ssh"));
    int found = 0;
    for (unsigned i = 0; args[i]; i++)
    {
        found += !strcmp(args[i], "-oBatchMode=yes");
        found += !strcmp(args[i], "127.0.0.1:4567:[::1]:2345");
        found += !strcmp(args[i], "/key with spaces");
        found += !strcmp(args[i], "/config with spaces");
        found += !strcmp(args[i], "-oControlPath=none");
        found += !strcmp(args[i], "-oRemoteCommand=none");
        if (!strcmp(args[i], "--"))
        {
            assert(!strcmp(args[i + 1], "build-host"));
            assert(strstr(args[i + 2], "GEANY-GDB-SSH-READY") && strstr(args[i + 2], "kill -STOP"));
            assert(!strstr(args[i + 2], "eval"));
            assert(!args[i + 3]);
        }
    }
    assert(found == 6);
    g_strfreev(args);
    args = native_remote_argv("ssh", "host", "", 22, "", "", "127.0.0.1", 2345, 4567, TRUE);
    for (unsigned i = 0; args[i]; i++)
        assert(strcmp(args[i], "-oBatchMode=yes"));
    g_strfreev(args);
    args = native_remote_argv("ssh", "host", "", 0, "", "", "localhost", 2345, 4567, TRUE);
    assert(args);
    for (unsigned i = 0; args[i]; i++)
        assert(strcmp(args[i], "-p"));
    g_strfreev(args);
    assert(!native_remote_argv("ssh", "host", "", 22, "", "", "localhost", 65536, 4567, TRUE));
    GQueue queue = {0};
    native_remote_mapping(&queue, "/build with spaces", "C:\\local source");
    char *command = g_queue_pop_head(&queue);
    assert(!strcmp(command, "-interpreter-exec console \"set substitute-path \\\"/build with spaces\\\" \\\"C:\\\\\\\\local source\\\"\""));
    g_free(command);
    native_remote_mapping(&queue, "/build with spaces", NULL);
    command = g_queue_pop_head(&queue);
    assert(strstr(command, "unset substitute-path") && strstr(command, "/build with spaces"));
    g_free(command);
    assert(!g_queue_pop_head(&queue));
    command = native_remote_setting("remote exec-file", "/remote/path with spaces/app");
    assert(command);
    puts(command);
    g_free(command);
    puts("-gdb-show remote exec-file");
    command = native_remote_setting("sysroot", "target:");
    puts(command);
    g_free(command);
    puts("-gdb-show sysroot");
    command = native_remote_setting("sysroot", "");
    puts(command);
    g_free(command);
    puts("-gdb-show sysroot");
    puts("-interpreter-exec console \"help queue-signal\"");
    assert(!native_remote_setting("sysroot", "/path\nquit"));
    assert(!native_remote_setting("shell", "touch file"));
    return 0;
}
'''


class NativeRemote(unittest.TestCase):
    # EN: Compile and execute the same model used by the editor with strict C warnings.
    # RU: Компилирует и выполняет используемую редактором модель со строгими предупреждениями C.
    def test_ssh_arguments_and_source_paths(self):
        library = ctypes.util.find_library('glib-2.0')
        if not library or not shutil.which('cc'):
            self.skipTest('Needs cc and the GLib runtime library.')
        helpers = [function('src/insight-debuggers/gdb-context.inc', 'native_context_id'),
                   function('src/insight-debuggers/gdb-context.inc', 'native_debug_field'),
                   function('src/insight-debuggers/gdb-attach-interrupt.inc', 'native_attach_stopped'),
                   function('src/insight-debuggers/gdb-remote-connection.inc', 'native_remote_next'),
                   function('src/insight-debuggers/gdb-attach-model.inc', 'native_attach_command'),
                   function('src/insight-debuggers/gdb-attach-model.inc', 'native_attach_signal_line')]
        helpers += [function('src/insight-debuggers/gdb-remote-model.inc', name) for name in
                   ('native_remote_host_valid', 'native_remote_argv', 'native_remote_console', 'native_remote_mapping', 'native_remote_setting')]
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as temporary:
            root = Path(temporary)
            (root / 'remote.c').write_text(DECLARATIONS + STOP_DECLARATIONS + '\n'.join(helpers) + MAIN)
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(root / 'remote.c'),
                            '-o', str(root / 'remote'), '-l:' + library], check=True)
            commands = subprocess.check_output([str(root / 'remote')], text=True)
            gdb = os.environ.get('GDB', shutil.which('gdb'))
            if gdb:
                result = subprocess.run([gdb, '--nx', '--quiet', '--interpreter=mi2'],
                                        input=commands + '-gdb-exit\n', text=True,
                                        capture_output=True, check=True, timeout=15)
                self.assertNotIn('^error', result.stdout)
                self.assertIn('^done,value="/remote/path with spaces/app"', result.stdout)
                self.assertIn('^done,value="target:"', result.stdout)
                self.assertIn('^done,value=""', result.stdout)

    # EN: Execute the actual SSH control script against a non-group-leader child and reject non-PID input.
    # RU: Выполняет настоящий управляющий скрипт SSH для процесса вне своей группы и отклоняет ввод не-PID.
    def test_pid_specific_pause_protocol(self):
        library = ctypes.util.find_library('glib-2.0')
        if os.name != 'posix' or not Path('/proc/self/status').exists() or not library or not shutil.which('cc'):
            self.skipTest('Needs Linux, cc and GLib runtime.')
        helpers = '\n'.join(function('src/insight-debuggers/gdb-remote-model.inc', name)
                            for name in ('native_remote_host_valid', 'native_remote_argv'))
        main = r'''/* EN: Print the production SSH command without executing a remote connection.
 * RU: Печатает производственную команду SSH без удалённого подключения. */
int main(void)
{
    char **args = native_remote_argv("ssh", "host", "", 0, "", "", "127.0.0.1", 2345, 4567, TRUE);
    unsigned last = 0;
    while (args[last + 1])
        last++;
    puts(args[last]);
    g_strfreev(args);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as temporary:
            root = Path(temporary)
            (root / 'protocol.c').write_text(DECLARATIONS + helpers + main)
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(root / 'protocol.c'),
                            '-o', str(root / 'protocol'), '-l:' + library], check=True)
            script = subprocess.check_output([str(root / 'protocol')], text=True)
            child = subprocess.Popen(['sleep', '30'])
            try:
                marker = root / 'must-not-exist'
                payload = f"pause:0\npause:0000000000\npause:99999999999999999999\npause:-1\npause:{child.pid}; touch {marker}\npause:{child.pid}\n"
                result = subprocess.run(['sh', '-c', script], input=payload, text=True,
                                        capture_output=True, check=True, timeout=5)
                self.assertEqual(result.stdout.splitlines(), ['GEANY-GDB-SSH-READY', 'GEANY-GDB-SSH-STOPPED'])
                self.assertFalse(marker.exists())
                for _ in range(100):
                    state = next(line for line in Path(f'/proc/{child.pid}/status').read_text().splitlines()
                                 if line.startswith('State:'))
                    if 'T' in state:
                        break
                    time.sleep(0.01)
                self.assertIn('T', state)
            finally:
                os.kill(child.pid, signal.SIGCONT)
                child.terminate()
                child.wait(timeout=5)


if __name__ == '__main__':
    unittest.main()
