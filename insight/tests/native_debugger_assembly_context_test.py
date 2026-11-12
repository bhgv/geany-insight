#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Exercise production instruction-context invalidation and rendering without constructing GTK widgets.
# RU: Проверяет сброс контекста инструкций и отображение производственного кода без создания виджетов GTK.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import function
from native_debugger_names_fixture import model_source

SESSION = '\ntypedef struct {NativeAssembly *assembly; char *selected_thread, *selected_frame;} NativeDebug;\n'
FIXTURE = r'''
#define GTK_LABEL(value) (value)
#define g_clear_pointer(pointer, destroy) do { destroy(*(pointer)); *(pointer) = NULL; } while (0)
static guint refreshed, rows_cleared, views_refreshed;
static gchar *rendered;

/* EN: Count clearing of the fixture's nonvisual register rows; rows identifies the model token.
 * RU: Считает очистки невизуальных строк регистров; rows обозначает идентификатор модели.
 */
static void gtk_list_store_clear(void *rows)
{
    assert(rows);
    rows_cleared++;
}

/* EN: Check label/text arguments without constructing a GTK label or changing production rendering.
 * RU: Проверяет аргументы label/text без создания GTK label и изменения производственного отображения.
 */
static void gtk_label_set_text(void *label, const gchar *text)
{
    (void)label;
    assert(text && *text);
}

/* EN: Render session's cached instructions through the real naming model when a snapshot exists.
 * RU: Отображает сохранённые инструкции session настоящей моделью имён при наличии снимка.
 */
static void native_instruction_refresh(NativeDebug *session)
{
    if (session->assembly->record)
    {
        g_free(rendered);
        rendered = native_assembly_render(session, session->assembly->record);
    }
    refreshed++;
}

/* EN: Count the refresh of other named views in session without GTK display work.
 * RU: Считает обновления остальных подписанных представлений session без отрисовки GTK.
 */
static void native_name_refresh_views(NativeDebug *session)
{
    assert(session->assembly);
    views_refreshed++;
}
'''
MAIN = r'''
/* EN: Run context scenario argv[1]; argc must be two. Real GLib owns registers/ranges and the renderer owns its output.
 * RU: Выполняет сценарий контекста argv[1]; argc равен двум. GLib владеет регистрами/диапазонами, форматтер — выводом.
 */
int main(int argc, char **argv)
{
    assert(argc == 2);
    gboolean register_only = !strcmp(argv[1], "register-only");
    gboolean unchanged = !strcmp(argv[1], "unchanged");
    gboolean active = !strcmp(argv[1], "active");
    NativeAssembly assembly = {0};
    NativeDebug session = {.assembly = &assembly, .selected_thread = "2", .selected_frame = "1"};
    assembly.context_thread = g_strdup("1");
    assembly.context_frame = g_strdup("0");
    assembly.function = register_only ? NULL : g_strdup("old_function");
    assembly.active = active;
    assembly.instruction_pc_valid = TRUE;
    assembly.register_rows = (void *)1;
    assembly.rules = g_ptr_array_new_with_free_func(native_name_rule_free);
    NativeNameRule *rule = g_new0(NativeNameRule, 1);
    rule->anchor = g_strdup("&global_packet");
    rule->name = g_strdup("manual_rule");
    g_ptr_array_add(assembly.rules, rule);
    assembly.ranges = g_ptr_array_new_with_free_func(native_name_range_free);
    assembly.registers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    guint64 *value = g_new(guint64, 1);
    *value = 0x1000;
    g_hash_table_replace(assembly.registers, g_strdup("rax"), value);
    if (!register_only)
    {
        NativeNamedRange *range = g_new0(NativeNamedRange, 1);
        range->address = 0x1000;
        range->size = 4;
        range->name = g_strdup("global_packet");
        g_ptr_array_add(assembly.ranges, range);
        NativeNamedRange *local = g_new0(NativeNamedRange, 1);
        local->address = 0x2000;
        local->size = 4;
        local->name = g_strdup("old_local");
        local->scope = g_strdup("old_function");
        g_ptr_array_add(assembly.ranges, local);
    }
    assembly.record = g_strdup("^done,asm_insns=[{address=\"0x10\",inst=\"mov eax,[rax]\"},"
        "{address=\"0x11\",inst=\"mov ebx,0x1000\"}]");
    if (unchanged)
    {
        session.selected_thread = "1";
        session.selected_frame = "0";
    }
    else if (!strcmp(argv[1], "thread"))
        session.selected_frame = "0";
    else if (!strcmp(argv[1], "frame"))
        session.selected_thread = "1";
    gboolean running = !strcmp(argv[1], "running") || !strcmp(argv[1], "running-twice");
    if (running)
    {
        native_assembly_running(&session);
        if (!strcmp(argv[1], "running-twice"))
            native_assembly_running(&session);
        assert(refreshed == (!strcmp(argv[1], "running-twice") ? 2u : 1u));
        assert(!rows_cleared && !views_refreshed);
        assert(!assembly.ranges->len && !g_hash_table_size(assembly.registers));
        assert(!assembly.instruction_pc_valid);
        assert(assembly.rules->len == 1 && !strcmp(rule->name, "manual_rule"));
        assert(rendered && !strstr(rendered, "global_packet") && !strstr(rendered, "old_local"));
        assert(strstr(rendered, "mov eax,[rax]") && strstr(rendered, "mov ebx,0x1000"));
    }
    else
        native_assembly_context(&session);
    if (!running && (unchanged || active))
    {
        assert(!refreshed && !rows_cleared && !views_refreshed);
        assert(g_hash_table_size(assembly.registers) == 1 && assembly.instruction_pc_valid);
    }
    else if (!running)
    {
        assert(refreshed == 1 && rows_cleared == 1 && views_refreshed == 1);
        assert(!g_hash_table_size(assembly.registers) && !assembly.instruction_pc_valid && assembly.follow_needed);
        assert(!strcmp(assembly.context_thread, session.selected_thread));
        assert(!strcmp(assembly.context_frame, session.selected_frame));
        assert(rendered && strstr(rendered, "mov eax,[rax]\n"));
        assert(assembly.ranges->len == (register_only ? 0u : 1u));
        if (!register_only)
            assert(strstr(rendered, "mov ebx,0x1000  ; global_packet"));
    }
    g_free(rendered);
    g_free(assembly.record);
    g_free(assembly.function);
    g_free(assembly.context_thread);
    g_free(assembly.context_frame);
    g_hash_table_unref(assembly.registers);
    g_ptr_array_unref(assembly.ranges);
    g_ptr_array_unref(assembly.rules);
    return 0;
}
'''


class NativeAssemblyContext(unittest.TestCase):
    # EN: Compile the real context callback/renderer and exercise frame, thread and guard scenarios without a display.
    # RU: Компилирует настоящий обработчик/форматтер и проверяет кадры, потоки и защитные условия без дисплея.
    def test_context_invalidation(self):
        libraries = [ctypes.util.find_library(name) for name in ('gio-2.0', 'glib-2.0')]
        if not all(libraries) or not shutil.which('cc'):
            self.skipTest('Needs cc and GLib runtime.')
        code = model_source(SESSION) + FIXTURE
        code += function('src/insight-debuggers/disassembly/views.inc', 'native_assembly_context')
        code += function('src/insight-debuggers/disassembly/views.inc', 'native_assembly_running') + MAIN
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'context.c'
            source.write_text(code)
            executable = root / 'context'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(source), '-o', str(executable), *['-l:' + library for library in libraries]], check=True)
            for scenario in ('thread', 'frame', 'register-only', 'unchanged', 'active', 'running', 'running-twice'):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(executable), scenario], check=True)


if __name__ == '__main__':
    unittest.main()
