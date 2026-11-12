#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Regress register comparison across context round trips using production callbacks and real GTK row models.
# RU: Проверяет сравнение регистров при возвращении в контекст настоящими обработчиками и моделями строк GTK.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import function
from native_debugger_names_fixture import model_source
from native_debugger_registers_test import SESSION, EXTRA

FIXTURE = r'''
#define GTK_LABEL(value) (value)
#define g_clear_pointer(pointer, destroy)                                                                    \
    do                                                                                                       \
    {                                                                                                        \
        destroy(*(pointer));                                                                                 \
        *(pointer) = NULL;                                                                                   \
    } while (0)
/* EN: Ignore the instruction repaint for session; this test checks actual register models, not GTK text
 * widgets. RU: Пропускает перерисовку инструкций session; тест проверяет настоящие модели регистров, а не
 * текстовые виджеты GTK. */
static void native_instruction_refresh(NativeDebug *session)
{
    (void)session;
}
/* EN: Ignore other named views for session, whose widgets are not constructed by this fixture.
 * RU: Пропускает другие подписанные представления session, виджеты которых фикстура не создаёт. */
static void native_name_refresh_views(NativeDebug *session)
{
    (void)session;
}
/* EN: Accept the context status text for label without constructing a GTK widget.
 * RU: Принимает текст состояния контекста для label без создания виджета GTK. */
static void gtk_label_set_text(void *label, const char *text)
{
    (void)label;
    assert(text && *text);
}
'''
MAIN = r'''
/* EN: Publish value as rax in session's real cache, rebuild GTK rows and check the expected changed flag.
 * RU: Записывает value как rax в настоящий кэш session, перестраивает строки GTK и проверяет признак changed.
 */
static void snapshot(NativeDebug *session, guint64 value, gboolean changed)
{
    guint64 *stored = g_new(guint64, 1);
    *stored = value;
    g_hash_table_replace(session->assembly->registers, g_strdup("rax"), stored);
    native_assembly_register_rows(session);
    GtkTreeIter row;
    assert(gtk_tree_model_get_iter_first(session->assembly->register_rows, &row));
    char *flag = NULL;
    gtk_tree_model_get(session->assembly->register_rows, &row, 4, &flag, -1);
    assert((flag && *flag) == changed);
    g_free(flag);
}

/* EN: Test argv[1]'s context round trip; argc must be two. Own and release all model/cache allocations.
 * RU: Проверяет возвращение в контекст для argv[1]; argc равен двум. Освобождает все выделенные модели и
 * кэши. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    NativeAssembly assembly = {0};
    NativeDebug session = {.assembly = &assembly, .selected_thread = "1", .selected_frame = "0"};
    assembly.context_thread = g_strdup("1");
    assembly.context_frame = g_strdup("0");
    assembly.ranges = g_ptr_array_new_with_free_func(native_name_range_free);
    assembly.register_names = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(assembly.register_names, g_strdup("rax"));
    assembly.registers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    assembly.previous_registers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    assembly.register_rows = gtk_list_store_new(6, 64UL, 64UL, 64UL, 64UL, 64UL, 64UL);
    snapshot(&session, 0x1000, FALSE);
    gboolean unknown_thread = !strcmp(argv[1], "unknown-thread");
    gboolean unknown_frame = !strcmp(argv[1], "unknown-frame");
    if (unknown_thread || unknown_frame)
    {
        if (unknown_thread)
            session.selected_thread = NULL;
        else
            session.selected_frame = NULL;
        snapshot(&session, 0x2000, FALSE);
        snapshot(&session, 0x3000, FALSE);
    }
    else
    {
        gboolean unchanged = !strcmp(argv[1], "unchanged");
        gboolean active = !strcmp(argv[1], "active");
        if (!unchanged)
        {
            if (!strcmp(argv[1], "thread"))
                session.selected_thread = "2";
            else
                session.selected_frame = "1";
        }
        assembly.active = active;
        native_assembly_context(&session);
        session.selected_thread = "1";
        session.selected_frame = "0";
        if (!active)
            native_assembly_context(&session);
        assembly.active = FALSE;
        snapshot(&session, 0x2000, unchanged || active);
        snapshot(&session, 0x3000, TRUE);
    }
    g_object_unref(assembly.register_rows);
    g_ptr_array_unref(assembly.register_names);
    g_ptr_array_unref(assembly.ranges);
    g_hash_table_unref(assembly.registers);
    g_hash_table_unref(assembly.previous_registers);
    g_free(assembly.register_thread);
    g_free(assembly.register_frame);
    g_free(assembly.context_thread);
    g_free(assembly.context_frame);
    return 0;
}
'''


class NativeRegisterContext(unittest.TestCase):
    # EN: Compile production context/row callbacks and check return trips, unchanged/active controls and missing context identifiers.
    # RU: Компилирует настоящие обработчики контекста/строк и проверяет возвращение, защитные условия и отсутствие идентификаторов контекста.
    def test_comparison_round_trips(self):
        libraries = [ctypes.util.find_library(name) for name in ('gtk-3', 'gobject-2.0', 'gio-2.0', 'glib-2.0')]
        if not all(libraries) or not shutil.which('cc'):
            self.skipTest('Needs cc and GTK/GLib runtime libraries.')
        code = model_source(SESSION) + EXTRA + FIXTURE
        code += function('src/insight-debuggers/disassembly/views.inc', 'native_assembly_context')
        code += function('src/insight-debuggers/disassembly/registers-ui.inc', 'native_assembly_register_rows')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'register-context.c'
            source.write_text(code + MAIN)
            executable = root / 'register-context'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(source), '-o', str(executable), *['-l:' + name for name in libraries]], check=True)
            for scenario in ('thread', 'frame', 'unchanged', 'active', 'unknown-thread', 'unknown-frame'):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(executable), scenario], check=True)


if __name__ == '__main__':
    unittest.main()
