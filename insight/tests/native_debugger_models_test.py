#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Check production GDB scoping and thread-tree parsing against GTK and real GDB.
# RU: Проверяет контекст команд GDB и дерево потоков на GTK и настоящем GDB.
import ctypes.util
import os
from pathlib import Path
import re
import select
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]


# EN: Extract function name from production path, retaining its complete body.
# RU: Извлекает функцию name из производственного path вместе с полным телом.
def function(path, name):
    source = (ROOT / path).read_text()
    position = source.index(name + '(')
    start = source.rfind('\nstatic ', 0, position) + 1
    return source[start:source.index('\n}', position) + 2]


DECLARATIONS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef char gchar;
typedef size_t gsize;
typedef long gssize;
typedef unsigned int guint;
typedef int gboolean;
typedef struct GHashTable GHashTable;
typedef struct GtkTreeStore GtkTreeStore;
typedef GtkTreeStore GtkTreeModel;
typedef void GtkTreeView;
typedef void GtkTreePath;
typedef struct {int stamp; void *a, *b, *c;} GtkTreeIter;
#define FALSE 0
#define G_TYPE_STRING 64UL
#define G_N_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))
#define GTK_TREE_MODEL(a) (a)
#define GTK_TREE_VIEW(a) (a)
#define insight_tr(a) (a)
extern void g_free(void *);
extern char *g_strdup(const char *);
extern char *g_strndup(const char *, size_t);
extern char *g_strdup_printf(const char *, ...);
extern char *g_strcompress(const char *);
extern char *g_filename_display_name(const char *);
extern char *g_utf8_make_valid(const char *, gssize);
extern int g_strcmp0(const char *, const char *);
extern int g_str_has_prefix(const char *, const char *);
extern guint g_str_hash(const void *);
extern int g_str_equal(const void *, const void *);
extern GHashTable *g_hash_table_new(guint (*)(const void *), int (*)(const void *, const void *));
extern int g_hash_table_insert(GHashTable *, void *, void *);
extern void *g_hash_table_lookup(GHashTable *, const void *);
extern void g_hash_table_unref(GHashTable *);
extern GtkTreeStore *gtk_tree_store_new(int, ...);
extern void gtk_tree_store_insert_with_values(GtkTreeStore *, GtkTreeIter *, GtkTreeIter *, int, ...);
extern void gtk_tree_store_set(GtkTreeStore *, GtkTreeIter *, ...);
extern int gtk_tree_model_iter_n_children(GtkTreeModel *, GtkTreeIter *);
extern int gtk_tree_model_iter_children(GtkTreeModel *, GtkTreeIter *, GtkTreeIter *);
extern void gtk_tree_model_get(GtkTreeModel *, GtkTreeIter *, ...);
extern void g_object_unref(void *);
/* EN/RU: No view is constructed in this headless model fixture. / В фикстуре нет виджета. */
/* EN: Ignore model and row in the nonvisual fixture; no path is needed.
 * RU: Игнорирует model и row в невизуальной фикстуре; путь не нужен. */
static GtkTreePath *gtk_tree_model_get_path(GtkTreeModel *model, GtkTreeIter *row)
{
    (void)model;
    (void)row;
    return NULL;
}
/* EN: Ignore view, path and recursive in this model-only fixture.
 * RU: Игнорирует view, path и recursive в фикстуре только модели. */
static void gtk_tree_view_expand_row(GtkTreeView *view, GtkTreePath *path, int recursive)
{
    (void)view;
    (void)path;
    (void)recursive;
}
/* EN: Ignore the fixture's absent path.
 * RU: Игнорирует отсутствующий path фикстуры. */
static void gtk_tree_path_free(GtkTreePath *path)
{
    (void)path;
}
typedef struct {
    GtkTreeStore *all_stack_rows;
    GHashTable *thread_roots;
    char *all_stack_thread, *selected_thread, *selected_frame;
    GtkTreeView *all_stacks_view;
} NativeDebug;
'''

HARNESS = r'''
/* EN: Verify production command scoping and parsed record from argv[1].
 * RU: Проверяет контекст команд и разбор записи из argv[1]. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    NativeDebug session = {0};
    session.selected_thread = "2";
    session.selected_frame = "1";
    const char *commands[] = {"-data-evaluate-expression \"worker_value\"",
        "-stack-list-variables --all-values", "-stack-list-frames 0 63",
        "-stack-select-frame 1", "-exec-next", "-interpreter-exec console \"print worker_value\""};
    for (unsigned i = 0; i < G_N_ELEMENTS(commands); i++)
    {
        char *scoped = native_debug_scoped(&session, commands[i]);
        assert(strstr(scoped, " --thread 2 --frame 1"));
        puts(scoped);
        g_free(scoped);
    }
    char *explicit = native_debug_scoped(&session, "-stack-list-frames --thread 7 0 63");
    assert(!strcmp(explicit, "-stack-list-frames --thread 7 0 63"));
    g_free(explicit);
    explicit = native_debug_scoped(&session, "-data-evaluate-expression \" --thread text\"");
    assert(strstr(explicit, "--thread 2 --frame 1"));
    g_free(explicit);
    session.selected_thread = "bad\n-exec-run";
    explicit = native_debug_scoped(&session, "-exec-next");
    assert(!strcmp(explicit, "-exec-next"));
    g_free(explicit);
    session.all_stack_thread = "2";
    session.thread_roots = g_hash_table_new(g_str_hash, g_str_equal);
    session.all_stack_rows = gtk_tree_store_new(7, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkTreeIter root, child;
    gtk_tree_store_insert_with_values(session.all_stack_rows, &root, NULL, -1, 0, "Thread 2", -1);
    g_hash_table_insert(session.thread_roots, "2", &root);
    native_all_stacks_reply(&session, argv[1]);
    assert(gtk_tree_model_iter_n_children(session.all_stack_rows, &root) >= 3);
    assert(gtk_tree_model_iter_children(session.all_stack_rows, &child, &root));
    char *level = NULL, *name = NULL, *thread = NULL;
    gtk_tree_model_get(session.all_stack_rows, &child, 6, &level, 1, &name, 5, &thread, -1);
    assert(!strcmp(level, "0") && !strcmp(name, "checkpoint") && !strcmp(thread, "2"));
    g_free(level);
    g_free(name);
    g_free(thread);
    native_all_stacks_reply(&session, "^error,msg=\"Thread exited\"");
    gtk_tree_model_get(session.all_stack_rows, &root, 1, &name, -1);
    assert(!strcmp(name, "Stack unavailable"));
    g_free(name);
    g_hash_table_unref(session.thread_roots);
    g_object_unref(session.all_stack_rows);
    return 0;
}
'''

FIXTURE = r'''
#include <pthread.h>
#include <unistd.h>
/* EN/RU: Stop with live worker and parent frames. / Остановка с живым стеком потока. */
void checkpoint(void)
{
    sleep(1);
}
/* EN/RU: Give the worker a distinct local. / Отдельная локальная переменная потока. */
void *worker(void *unused)
{
    (void)unused;
    volatile int worker_value = 73;
    checkpoint();
    return (void *)(long)worker_value;
}
/* EN/RU: Keep the main thread alive. / Сохраняет основной поток. */
int main(void)
{
    pthread_t thread;
    pthread_create(&thread, 0, worker, 0);
    pthread_join(thread, 0);
    return 0;
}
'''


class NativeModels(unittest.TestCase):
    # EN: Verify real stopped-thread MI replies and production GTK tree/scoping helpers.
    # RU: Проверяет MI настоящего остановленного потока и производственные функции GTK/контекста.
    def exercise(self, real_gdb):
        gdb = os.environ.get('GDB', shutil.which('gdb'))
        libraries = [ctypes.util.find_library(n) for n in ('gtk-3', 'gobject-2.0', 'glib-2.0')]
        if (real_gdb and not gdb) or not shutil.which('cc') or not all(libraries):
            self.skipTest('Needs GDB, cc and GTK 3 runtime libraries.')
        helpers = [function('src/insight-debuggers/gdb-context.inc', name) for name in
                   ('native_debug_field', 'native_next_tuple', 'native_context_id')]
        helpers += [function('src/insight-debuggers/gdb-session.inc', 'native_debug_scoped'),
                    function('src/insight-debuggers/gdb-backtraces.inc', 'native_all_stacks_reply')]
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / 'target.c').write_text(FIXTURE)
            subprocess.run(['cc', '-g', '-O0', '-pthread', str(folder / 'target.c'),
                            '-o', str(folder / 'target')], check=True)
            (folder / 'model.c').write_text(DECLARATIONS + '\n'.join(helpers) + HARNESS)
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(folder / 'model.c'),
                            '-o', str(folder / 'model'), *['-l:' + n for n in libraries]], check=True)
            if not real_gdb:
                record = '^done,stack=[frame={level="0",func="checkpoint",fullname="/tmp/main.c",line="4"},frame={level="1",func="worker"},frame={level="2",func="start_thread"}]'
                subprocess.run([str(folder / 'model'), record], check=True, capture_output=True)
                return
            with subprocess.Popen([gdb, '--nx', '--quiet', '--interpreter=mi2',
                                   str(folder / 'target')], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0) as process:
                transcript = []
                # EN: Wait for the requested MI record, failing with the complete recent output.
                # RU: Ждёт нужную запись MI; при ошибке показывает накопленный вывод.
                def read_until(prefix):
                    lines = []
                    deadline = time.monotonic() + 15
                    while time.monotonic() < deadline:
                        if not select.select([process.stdout], [], [], 0.1)[0]:
                            continue
                        line = process.stdout.readline().decode().strip()
                        lines.append(line)
                        transcript.append(line)
                        if line.startswith(prefix):
                            return line
                        if not line:
                            break
                    self.fail('Missing ' + prefix + ': ' + '\n'.join(lines))

                # EN: Send command and collect its matching MI result.
                # RU: Отправляет command и получает соответствующий результат MI.
                def command(text):
                    process.stdin.write((text + '\n').encode())
                    process.stdin.flush()
                    return read_until('^')

                try:
                    read_until('(gdb)')
                    self.assertTrue(command('-gdb-set startup-with-shell off').startswith('^done'))
                    self.assertTrue(command('-break-insert checkpoint').startswith('^done'))
                    running = command('-exec-run')
                    if any('ptrace: Operation not permitted' in line for line in transcript):
                        self.skipTest('Sandbox blocks ptrace; run this test on a normal Linux host.')
                    self.assertEqual(running, '^running')
                    stopped = read_until('*stopped')
                    thread = re.search(r'thread-id="(\d+)"', stopped).group(1)
                    stack = command(f'-stack-list-frames --thread {thread} 0 63')
                    result = subprocess.run([str(folder / 'model'), stack], check=True,
                                            capture_output=True, text=True)
                    self.assertTrue(command('-thread-select 1').startswith('^done'))
                    for scoped in result.stdout.splitlines()[:4]:
                        scoped = scoped.replace('--thread 2', '--thread ' + thread)
                        reply = command(scoped)
                        self.assertTrue(reply.startswith('^done'), reply)
                        if scoped.startswith('-data-evaluate'):
                            self.assertIn('value="73"', reply)
                        if scoped.startswith('-stack-list-variables'):
                            self.assertIn('name="worker_value",value="73"', reply)
                    self.assertTrue(command('-gdb-exit').startswith('^exit'))
                finally:
                    if process.poll() is None:
                        process.kill()
                    process.wait(timeout=5)

    # EN: Check production scoping and GTK models without debugger permissions.
    # RU: Проверяет контекст и GTK-модели без разрешения на отладку процесса.
    def test_models(self):
        self.exercise(False)

    # EN: Check the same helpers against a stopped multi-threaded executable.
    # RU: Проверяет те же функции на остановленной многопоточной программе.
    def test_live_gdb(self):
        self.exercise(True)


if __name__ == '__main__':
    unittest.main()
