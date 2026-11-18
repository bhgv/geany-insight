#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Exercise opt-in expression reloads and draft isolation with production code and GTK storage.
# RU: Проверяет автообновление выражений и независимость черновиков настоящим кодом и GTK-хранилищем.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import DECLARATIONS, function
from native_debugger_values_test import EXTRA, STATE

CONTROLS = r'''
#define GTK_ENTRY(value) (value)
#define GTK_TOGGLE_BUTTON(value) (value)
/* EN: Read the fixture's checkbox value; widget points to a plain boolean, without a display.
 * RU: Читает флажок фикстуры; widget указывает на булево значение без графического дисплея. */
static gboolean gtk_toggle_button_get_active(GtkWidget *widget)
{
    return *(gboolean *)widget;
}
/* EN: Return the editable draft stored through widget; no GTK entry is constructed.
 * RU: Возвращает черновик, переданный через widget; виджет поля GTK не создаётся. */
static const gchar *gtk_entry_get_text(GtkWidget *widget)
{
    return *(const gchar **)widget;
}
'''

MAIN = r'''
/* EN: Run argv[1]'s reload scenario against real GTK rows and GLib queue ownership.
 * RU: Проверяет сценарий argv[1] на настоящих строках GTK и владении очередью GLib. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    gboolean enabled = strcmp(argv[1], "off") && strcmp(argv[1], "manual");
    const gchar *draft = "draft_call()";
    NativeDebug session = {.stopped = strcmp(argv[1], "running") != 0};
    session.value_auto = (GtkWidget *)&enabled;
    session.value_expression = (GtkWidget *)&draft;
    session.value_rows = gtk_tree_store_new(1, G_TYPE_STRING);
    gtk_tree_store_insert_with_values(session.value_rows, NULL, NULL, -1, 0, "old value", -1);
    if (strcmp(argv[1], "first"))
        session.value_pending_expression = g_strdup("value");
    if (strcmp(argv[1], "first") && strcmp(argv[1], "retry"))
        session.value_root = g_strdup("old_root");
    native_values_context_reset(&session);
    assert(session.value_generation == 1 && !session.value_root);
    assert(!gtk_tree_model_iter_n_children(session.value_rows, NULL));
    gboolean expected = enabled && session.stopped && session.value_pending_expression;
    assert(session.value_reload == expected);
    if (!strcmp(argv[1], "twice"))
    {
        native_values_context_reset(&session);
        assert(session.value_reload && session.value_generation == 2);
    }
    if (!strcmp(argv[1], "manual"))
        session.value_reload = TRUE;
    if (session.value_cleanup.length)
    {
        gchar *command = native_values_next(&session, TRUE);
        assert(!strcmp(command, "-var-delete \"old_root\""));
        g_free(command);
        assert(!native_values_next(&session, FALSE));
        session.value_request = NATIVE_VALUE_NONE;
    }
    if (session.value_reload)
    {
        if (!strcmp(argv[1], "pending"))
        {
            session.value_request = NATIVE_VALUE_CHILDREN;
            assert(!native_values_next(&session, FALSE) && session.value_reload);
            session.value_request = NATIVE_VALUE_NONE;
        }
        gchar *command = native_values_next(&session, FALSE);
        assert(command && !strcmp(command, "-var-create insight_value_1 * \"value\""));
        assert(!strstr(command, "draft_call"));
        assert(!session.value_reload && session.value_request == NATIVE_VALUE_CREATE);
        assert(session.value_pending_generation == session.value_generation);
        assert(!strcmp(draft, "draft_call()"));
        assert(!strcmp(session.value_pending_expression, "value"));
        g_free(command);
    }
    g_free(session.value_pending_expression);
    g_object_unref(session.value_rows);
    return 0;
}
'''


class NativeValueFollow(unittest.TestCase):
    # EN: Compile production reset/dispatch helpers and execute scenario without editor headers or ptrace.
    # RU: Компилирует сброс/отправку производственного кода и выполняет scenario без заголовков редактора и ptrace.
    def exercise(self, scenario):
        libraries = [ctypes.util.find_library(name) for name in ('gtk-3', 'gobject-2.0', 'glib-2.0')]
        if not shutil.which('cc') or not all(libraries):
            self.skipTest('Needs cc and GTK runtime libraries.')
        state = STATE.replace('GtkWidget *value_status;',
                              'GtkWidget *value_status, *value_auto, *value_expression;\n    guint value_serial;')
        declarations = DECLARATIONS.replace('typedef struct {\n    GtkTreeStore *all_stack_rows;', EXTRA.replace('static guint mutation_count;\n', '') + state)
        declarations += CONTROLS
        helpers = '\n'.join(function('src/insight-debuggers/gdb-values-model.inc', name)
                            for name in ('native_values_reset', 'native_values_context_reset', 'native_values_next'))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / 'follow.c'
            source.write_text(declarations + helpers + MAIN)
            executable = folder / 'follow'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(source), '-o', str(executable),
                            *['-l:' + name for name in libraries]], check=True)
            subprocess.run([str(executable), scenario], check=True)

    # EN: Check opt-in and no-first-inspection behavior without evaluating draft input.
    # RU: Проверяет флажок и отсутствие первого исследования без вычисления черновика.
    def test_reload_policy(self):
        for scenario in ('off', 'first', 'running', 'on', 'retry', 'twice', 'pending', 'manual'):
            with self.subTest(scenario=scenario):
                self.exercise(scenario)


if __name__ == '__main__':
    unittest.main()
