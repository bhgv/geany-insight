#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Execute production SSH argument and source-mapping builders against real GLib without network access.
# RU: Выполняет производственные построители аргументов SSH и сопоставлений путей с GLib без сети.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import function

DECLARATIONS = r'''
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
typedef char gchar;
typedef unsigned int guint;
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

MAIN = r'''
/* EN: Validate argv boundaries, IPv6 forwarding, non-shell quoting and private mapping cleanup.
 * RU: Проверяет границы argv, IPv6, отсутствие оболочки и очистку только собственного сопоставления. */
int main(void)
{
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
        if (!strcmp(args[i], "--"))
        {
            assert(!strcmp(args[i + 1], "build-host"));
            assert(!strcmp(args[i + 2], "printf 'GEANY-GDB-SSH-READY\\n'; cat >/dev/null"));
            assert(!args[i + 3]);
        }
    }
    assert(found == 4);
    g_strfreev(args);
    args = native_remote_argv("ssh", "host", "", 22, "", "", "127.0.0.1", 2345, 4567, TRUE);
    for (unsigned i = 0; args[i]; i++)
        assert(strcmp(args[i], "-oBatchMode=yes"));
    g_strfreev(args);
    assert(!native_remote_argv("ssh", "host", "", 0, "", "", "localhost", 2345, 4567, TRUE));
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
        helpers = [function('src/insight-debuggers/gdb-remote-model.inc', name) for name in
                   ('native_remote_host_valid', 'native_remote_argv', 'native_remote_console', 'native_remote_mapping')]
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as temporary:
            root = Path(temporary)
            (root / 'remote.c').write_text(DECLARATIONS + '\n'.join(helpers) + MAIN)
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(root / 'remote.c'),
                            '-o', str(root / 'remote'), '-l:' + library], check=True)
            subprocess.run([str(root / 'remote')], check=True)


if __name__ == '__main__':
    unittest.main()
