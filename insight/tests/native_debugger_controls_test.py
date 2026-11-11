#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Test native launch commands, restart sequencing and GDB breakpoint snapshots without ptrace.
# RU: Проверяет запуск, последовательность перезапуска и снимки точек GDB без ptrace.
import ctypes.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import DECLARATIONS, ROOT, function

EXTRA = r'''
typedef int gint;
#define TRUE 1
#define G_MAXINT 2147483647
#define G_GUINT64_FORMAT "lu"
typedef unsigned long guint64;
typedef struct {void **pdata; guint len;} GPtrArray;
typedef struct {unsigned domain; int code; char *message;} GError;
typedef struct {void *head, *tail; guint length;} GQueue;
typedef struct {char *str; size_t len, allocated_len;} GString;
#define GTK_TREE_MODEL(a) (a)
extern char *g_canonicalize_filename(const char *, const char *);
#define insight_canonicalize_filename(a,b) g_canonicalize_filename(a,b)
extern GString *g_string_new(const char *);
extern void g_string_append_printf(GString *, const char *, ...);
extern char *g_string_free(GString *, int);
extern int g_shell_parse_argv(const char *, int *, char ***, GError **);
extern void g_error_free(GError *);
extern char *g_strchug(char *);
extern char *g_strchomp(char *);
#define g_strstrip(s) g_strchomp(g_strchug(s))
extern char *g_strescape(const char *, const char *);
extern void g_strfreev(char **);
extern void *g_queue_pop_head(GQueue *);
extern void g_queue_push_tail(GQueue *, void *);
extern GPtrArray *g_ptr_array_new(void);
extern void g_ptr_array_set_size(GPtrArray *, guint);
extern void g_ptr_array_unref(GPtrArray *);
extern void g_hash_table_remove_all(GHashTable *);
extern unsigned long g_ascii_strtoull(const char *, char **, unsigned);
extern int g_hash_table_replace(GHashTable *, void *, void *);
extern GtkTreeStore *gtk_list_store_new(int, ...);
extern void gtk_list_store_clear(GtkTreeStore *);
extern void gtk_list_store_insert_with_values(GtkTreeStore *, GtkTreeIter *, int, ...);
/* EN/RU: Record margin changes without opening an editor. / Учитывает маркеры без редактора. */
static unsigned marker_count;
/* EN: Count the requested marker; arguments describe an editor that this fixture does not create.
 * RU: Считает маркер; параметры описывают редактор, который фикстура не создаёт. */
static void native_marker_add(GPtrArray *owned, const char *file, guint line, int type, int color)
{
    (void)owned;
    (void)file;
    (void)line;
    (void)color;
    assert(type == 20 || type == 18);
    marker_count++;
}
'''

MAIN = r'''
/* EN: Check launch argument quoting, kill-before-run and the breakpoint record in argv[1].
 * RU: Проверяет кавычки аргументов, остановку перед запуском и запись точек argv[1]. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    GError *error = NULL;
    char *command = native_launch_arguments("alpha 'two words' \"\" 'a\\b'", &error);
    assert(command && !error);
    assert(!strcmp(command, "-exec-arguments \"alpha\" \"two words\" \"\" \"a\\\\b\""));
    puts(command);
    g_free(command);
    assert(!native_launch_arguments("'unterminated", &error) && error);
    g_error_free(error);
    error = NULL;
    command = native_launch_arguments("", &error);
    assert(!strcmp(command, "-exec-arguments"));
    g_free(command);
    NativeDebug session = {0};
    session.stop_requested = session.stopped = 1;
    g_queue_push_tail(&session.launch_commands, g_strdup("-exec-run"));
    command = native_launch_next(&session);
    assert(!strcmp(command, "-interpreter-exec console \"kill\"") && session.kill_pending);
    g_free(command);
    session.stop_requested = session.stopped = 0;
    command = native_launch_next(&session);
    assert(!strcmp(command, "-exec-run"));
    g_free(command);
    assert(!native_launch_next(&session));
    session.point_rows = gtk_list_store_new(8, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    session.editor_points = g_hash_table_new(g_str_hash, g_str_equal);
    session.point_markers = g_ptr_array_new();
    native_points_snapshot(&session, argv[1]);
    assert(gtk_tree_model_iter_n_children(session.point_rows, NULL) == 1);
    GtkTreeIter row;
    assert(gtk_tree_model_iter_children(session.point_rows, &row, NULL));
    char *enabled = NULL, *condition = NULL, *ignore = NULL, *hits = NULL, *file = NULL;
    gtk_tree_model_get(session.point_rows, &row, 2, &enabled, 3, &condition, 4, &hits, 5, &ignore, 6, &file, -1);
    assert(!strcmp(enabled, "n") && !strcmp(condition, "1") && !strcmp(ignore, "3"));
    assert(!strcmp(hits, "0") && file && marker_count == 1);
    g_free(enabled);
    g_free(condition);
    g_free(ignore);
    g_free(hits);
    g_free(file);
    native_points_snapshot(&session, "^error,msg=\"unavailable\"");
    assert(gtk_tree_model_iter_n_children(session.point_rows, NULL) == 1);
    native_points_snapshot(&session, "^done,BreakpointTable={body=[]}");
    assert(gtk_tree_model_iter_n_children(session.point_rows, NULL) == 0);
    g_object_unref(session.point_rows);
    g_hash_table_unref(session.editor_points);
    g_ptr_array_unref(session.point_markers);
    return 0;
}
'''


class NativeControls(unittest.TestCase):
    # EN: Compile production controls and validate them with real GDB's non-running target.
    # RU: Компилирует производственные функции и проверяет их на незапущенной программе GDB.
    def test_launch_and_breakpoint_controls(self):
        gdb = os.environ.get('GDB', shutil.which('gdb'))
        libraries = [ctypes.util.find_library(n) for n in ('gtk-3', 'gobject-2.0', 'glib-2.0')]
        if not gdb or not shutil.which('cc') or not all(libraries):
            self.skipTest('Needs GDB, cc and GTK runtime libraries.')
        declarations = DECLARATIONS.replace('typedef struct {\n    GtkTreeStore *all_stack_rows;', '''typedef struct {
    GtkTreeStore *point_rows;
    GHashTable *editor_points;
    GPtrArray *point_markers;
    GQueue launch_commands;
    gboolean stop_requested, stopped, kill_pending;
    GtkTreeStore *all_stack_rows;''')
        # EN/RU: Extra ABI types must precede the shared session declaration. / Типы ABI идут перед сеансом.
        position = declarations.index('typedef struct {\n    GtkTreeStore *point_rows;')
        declarations = declarations[:position] + EXTRA + declarations[position:]
        helpers = [function('src/insight-debuggers/gdb-context.inc', n) for n in
                   ('native_debug_field', 'native_next_tuple', 'native_context_id')]
        helpers += [function('src/insight-debuggers/gdb-lifecycle.inc', n) for n in
                    ('native_launch_arguments', 'native_launch_next')]
        helpers += [function('src/insight-debuggers/gdb-breakpoints.inc', n) for n in
                    ('native_point_key', 'native_point_tuple', 'native_points_snapshot')]
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / 'target.c').write_text('int main(void) { return 0; }\n')
            subprocess.run(['cc', '-g', str(folder / 'target.c'), '-o', str(folder / 'target')], check=True)
            commands = ['-break-insert main', '-break-condition 1 "1"', '-break-after 1 3',
                        '-break-disable 1', '-break-list', '-gdb-exit']
            gdb_result = subprocess.run([gdb, '--nx', '--quiet', '--interpreter=mi2', str(folder / 'target')],
                                        input='\n'.join(commands) + '\n', text=True,
                                        capture_output=True, check=True, timeout=15)
            self.assertNotIn('^error', gdb_result.stdout)
            snapshot = next(line for line in gdb_result.stdout.splitlines() if line.startswith('^done,BreakpointTable='))
            (folder / 'controls.c').write_text(declarations + '\n'.join(helpers) + MAIN)
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(folder / 'controls.c'), '-o', str(folder / 'controls'),
                            *['-l:' + n for n in libraries]], check=True)
            subprocess.run([str(folder / 'controls'), snapshot], check=True)


if __name__ == '__main__':
    unittest.main()
