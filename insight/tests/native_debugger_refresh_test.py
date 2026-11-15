#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Execute production paused refresh orchestration against real GTK models and counted transport hooks.
# RU: Выполняет настоящее обновление на паузе с моделями GTK и счётчиками обращений к транспорту.
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import function

STATE = r'''
#include <gtk/gtk.h>
#include <assert.h>
#include <string.h>
typedef struct {
    gboolean active, follow_needed;
    GtkListStore *register_rows;
} NativeAssembly;
typedef struct {
    gboolean closed, ready, stopped, running, pending, writing, value_reload, locals_needed;
    gchar *value_root;
    guint value_generation, watch_next;
    NativeAssembly *assembly;
    GtkWidget *window;
} NativeDebug;
static guint invalidated, reset, updated, resumed;
/* EN: Count session's value reset and dispose its root; real variable-object teardown has separate tests.
 * RU: Считает сброс значений session и удаляет корень; настоящая очистка объектов проверяется отдельно. */
static void native_values_reset(NativeDebug *session)
{
    reset++;
    session->value_generation++;
    session->value_reload = FALSE;
    g_clear_pointer(&session->value_root, g_free);
}
/* EN: Count cache invalidation for session; the production cache teardown is tested independently.
 * RU: Считает сброс кэшей session; настоящая очистка кэшей проверяется независимо. */
static void native_assembly_invalidate(NativeDebug *session)
{
    (void)session;
    invalidated++;
}
/* EN: Count a UI update for session without constructing a display-dependent window.
 * RU: Считает обновление интерфейса session без создания окна, требующего дисплея. */
static void native_debug_update(NativeDebug *session)
{
    (void)session;
    updated++;
}
/* EN: Count queue scheduling for window/session; do not execute any target command in this fixture.
 * RU: Считает запуск очереди window/session; фикстура не исполняет команды программы. */
static void native_debug_resume(GtkWidget *window, NativeDebug *session)
{
    (void)window;
    (void)session;
    resumed++;
}
'''
MAIN = r'''
/* EN: Run argv[1]'s refresh scenario with real register rows; argc must be two.
 * RU: Выполняет сценарий обновления argv[1] с настоящими строками регистров; argc равен двум. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    NativeAssembly assembly = {0};
    NativeDebug session = {.ready = TRUE, .stopped = TRUE, .assembly = &assembly, .watch_next = 7};
    assembly.register_rows = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_list_store_insert_with_values(assembly.register_rows, NULL, -1, 0, "stale address", -1);
    if (!strcmp(argv[1], "guards"))
    {
        gboolean *disabled[] = {&session.ready, &session.stopped};
        for (guint i = 0; i < G_N_ELEMENTS(disabled); i++)
        {
            *disabled[i] = FALSE;
            native_data_refresh_clicked(NULL, &session);
            *disabled[i] = TRUE;
        }
        gboolean *busy[] = {&session.closed, &session.running, &session.pending, &session.writing, &assembly.active};
        for (guint i = 0; i < G_N_ELEMENTS(busy); i++)
        {
            *busy[i] = TRUE;
            native_data_refresh_clicked(NULL, &session);
            *busy[i] = FALSE;
        }
        assert(!reset && !invalidated && !updated && !resumed);
        assert(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(assembly.register_rows), NULL) == 1);
    }
    else
    {
        gboolean root = !strcmp(argv[1], "tree");
        session.value_root = root ? g_strdup("owned_var") : NULL;
        native_data_refresh_clicked(NULL, &session);
        assert(reset == 1 && invalidated == 1 && updated == 1 && resumed == 1);
        assert(session.value_generation == 1 && session.value_reload == root);
        assert(!session.value_root && session.locals_needed && !session.watch_next);
        assert(assembly.follow_needed);
        assert(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(assembly.register_rows), NULL) == 0);
        session.assembly = NULL;
        native_data_refresh(&session);
        assert(reset == 2 && !session.value_reload);
    }
    g_object_unref(assembly.register_rows);
    return 0;
}
'''


class NativeRefresh(unittest.TestCase):
    # EN: Build and run production refresh/callback with valid, missing-root and guarded contexts.
    # RU: Собирает и выполняет настоящее обновление/обработчик с корнем, без корня и при запрете действий.
    def test_refresh_contract(self):
        if not shutil.which('cc') or not shutil.which('pkg-config'):
            self.skipTest('Needs cc and GTK development files.')
        flags = subprocess.run(['pkg-config', '--cflags', '--libs', 'gtk+-3.0'],
                               text=True, capture_output=True)
        if flags.returncode:
            self.skipTest('Needs GTK development files.')
        source = STATE
        for name in ('native_data_refresh', 'native_data_refresh_clicked'):
            source += function('src/insight-debuggers/gdb-data-actions.inc', name)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'refresh.c'
            path.write_text(source + MAIN)
            executable = root / 'refresh'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(executable),
                            *shlex.split(flags.stdout)], check=True)
            for scenario in ('tree', 'empty', 'guards'):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(executable), scenario], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
