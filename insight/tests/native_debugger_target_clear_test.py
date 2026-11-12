#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Execute production target teardown with real GLib caches and the real instruction renderer, without GTK widgets.
# RU: Выполняет очистку цели настоящими кэшами GLib и форматтером инструкций без виджетов GTK.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_assembly_context_test import FIXTURE
from native_debugger_models_test import function
from native_debugger_names_fixture import model_source

SESSION = r'''
typedef struct {
    guint attached_pid;
    gboolean interrupt_pending, pause_requested, clear_stop_signal, release_pending, own_continue_pending;
} Remote;
typedef struct {gchar *value;} NativeWatch;
typedef struct {
    NativeAssembly *assembly;
    Remote *remote;
    gboolean running, stopped, resume, stop_requested, data_refresh_pending;
    gboolean threads_needed, stack_needed, locals_needed, all_stacks_needed, frame_select_needed;
    gchar *stop_detail, *data_command, *selected_thread, *selected_frame, *requested_frame, *stop_file;
    guint stop_line, watch_next, value_generation;
    gpointer local_rows, stack_rows, all_stack_rows, threads;
    GPtrArray *locals, *stop_markers, *expressions;
} NativeDebug;
'''
STUBS = r'''
static guint source_replies;
#define GTK_COMBO_BOX_TEXT(value) (value)
/* EN: Count source replies for session/work/record without opening an editor; stale replies must not reach this handler.
 * RU: Считает ответы исходников session/work/record без открытия редактора; устаревшие ответы сюда попадать не должны. */
static void native_instruction_source_reply(NativeDebug *session, NativeAssemblyWork *work, const gchar *record)
{
    (void)session;
    (void)work;
    (void)record;
    source_replies++;
}
/* EN: Model the already-tested value reset for session without constructing its expression-tree widgets.
 * RU: Моделирует ранее проверенный сброс значений session без создания виджетов дерева выражений. */
static void native_values_reset(NativeDebug *session)
{
    session->value_generation++;
}
/* EN: Ignore tree rows during this cache/renderer test; model identifies the absent GTK tree.
 * RU: Пропускает строки дерева в проверке кэшей/форматтера; model обозначает отсутствующее дерево GTK. */
static void gtk_tree_store_clear(void *model)
{
    (void)model;
}
/* EN: Ignore picker rows for combo without creating a display-dependent GTK widget.
 * RU: Пропускает строки combo без создания виджета GTK, требующего дисплея. */
static void gtk_combo_box_text_remove_all(void *combo)
{
    (void)combo;
}
/* EN: Leave watch UI rendering for its own tests; session and redraw do not affect cached values here.
 * RU: Оставляет отображение наблюдений отдельным тестам; session и redraw здесь не меняют кэш значений. */
static void native_watch_refresh(NativeDebug *session, gboolean redraw)
{
    (void)session;
    (void)redraw;
}
'''
MAIN = r'''
/* EN: Clear argv[1]'s local/remote target; argc must be two. Confirm raw snapshots/rules survive but runtime
 * names do not. RU: Очищает локальную/удалённую цель argv[1]; argc равен двум. Проверяет сохранение
 * снимков/правил и удаление временных имён. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    NativeAssembly assembly = {0};
    NativeDebug session = {.assembly = &assembly, .running = TRUE, .stopped = TRUE, .resume = TRUE};
    Remote remote = {.attached_pid = 123,
                     .interrupt_pending = TRUE,
                     .pause_requested = TRUE,
                     .clear_stop_signal = TRUE,
                     .release_pending = TRUE,
                     .own_continue_pending = TRUE};
    if (!strcmp(argv[1], "remote"))
        session.remote = &remote;
    session.local_rows = (void *)1;
    session.stack_rows = (void *)2;
    session.locals = g_ptr_array_new_with_free_func(NULL);
    session.stop_markers = g_ptr_array_new_with_free_func(NULL);
    session.expressions = g_ptr_array_new_with_free_func(NULL);
    NativeWatch watch = {.value = g_strdup("0x1000")};
    g_ptr_array_add(session.expressions, &watch);
    assembly.active = assembly.source_only = TRUE;
    assembly.request = g_strdup("-data-disassemble old");
    assembly.objects = g_ptr_array_new_with_free_func(g_free);
    native_assembly_queue(&assembly, g_strdup("-stack-info-frame"), 1, NULL, NULL, NULL, NULL, 0, FALSE);
    if (g_str_has_prefix(argv[1], "late-"))
    {
        assembly.pending = g_new0(NativeAssemblyWork, 1);
        assembly.pending->action = g_str_has_prefix(argv[1], "late-object") ? 6
                                   : !strcmp(argv[1], "late-source")        ? 16
                                   : !strcmp(argv[1], "late-instructions")  ? 10
                                                                            : 5;
        assembly.pending->label = g_strdup("stale_alias");
        assembly.temporary_valid = TRUE;
        assembly.temporary_address = 0x1000;
    }
    assembly.rules = g_ptr_array_new_with_free_func(native_name_rule_free);
    NativeNameRule *rule = g_new0(NativeNameRule, 1);
    rule->name = g_strdup("saved_rule");
    rule->anchor = g_strdup("&packet");
    g_ptr_array_add(assembly.rules, rule);
    assembly.ranges = g_ptr_array_new_with_free_func(native_name_range_free);
    NativeNamedRange *range = g_new0(NativeNamedRange, 1);
    range->address = 0x1000;
    range->size = 8;
    range->name = g_strdup("global_packet");
    g_ptr_array_add(assembly.ranges, range);
    assembly.registers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    assembly.previous_registers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    guint64 *value = g_new(guint64, 1);
    *value = 0x1000;
    g_hash_table_replace(assembly.registers, g_strdup("rax"), value);
    assembly.history = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(assembly.history, g_strdup("0x10"));
    assembly.history_replaying = TRUE;
    assembly.instruction_pc_valid = TRUE;
    assembly.register_rows = (void *)1;
    assembly.record = g_strdup("^done,asm_insns=[{address=\"0x10\",inst=\"mov eax,[rax]\"},"
                               "{address=\"0x11\",inst=\"mov ebx,0x1000\"}]");
    gchar *before = native_assembly_render(&session, assembly.record);
    assert(before && strstr(before, "global_packet"));
    g_free(before);
    native_target_clear(&session);
    gboolean repeated = !strcmp(argv[1], "repeated");
    if (repeated)
        native_target_clear(&session);
    assert(!assembly.active && !assembly.source_only && !assembly.request && !assembly.work.length);
    if (assembly.pending)
    {
        const gchar *reply = !strcmp(argv[1], "late-object-error")       ? "^error,name=\"not_created\""
                             : !strcmp(argv[1], "late-object-malformed") ? "^done,name=\"\""
                             : !strcmp(argv[1], "late-object") ? "^done,name=\"old_object\",numchild=\"0\""
                             : !strcmp(argv[1], "late-instructions")
                                 ? "^done,asm_insns=[{address=\"0x9999\",inst=\"nop\"}]"
                                 : "^done,value=\"4\"";
        assert(native_assembly_reply(&session, reply));
        assert(!assembly.pending && !assembly.discard_pending && !assembly.work.length && !source_replies);
        assert(assembly.objects->len == (!strcmp(argv[1], "late-object") ? 1u : 0u));
        assert(strstr(assembly.record, "mov eax,[rax]"));
    }
    assert(!assembly.ranges->len && !g_hash_table_size(assembly.registers));
    assert(rendered && !strstr(rendered, "global_packet"));
    assert(strstr(rendered, "mov eax,[rax]") && strstr(rendered, "mov ebx,0x1000"));
    assert(assembly.rules->len == 1 && !strcmp(rule->name, "saved_rule"));
    assert(!assembly.history->len && !assembly.history_replaying && !assembly.instruction_pc_valid);
    assert(!session.running && !session.stopped && !session.resume &&
           session.value_generation == (repeated ? 2u : 1u));
    assert(!strcmp(watch.value, "Unavailable"));
    if (session.remote)
        assert(!remote.attached_pid && !remote.interrupt_pending && !remote.pause_requested &&
               !remote.clear_stop_signal && !remote.release_pending && !remote.own_continue_pending);
    if (g_str_has_prefix(argv[1], "late-"))
    {
        session.stopped = TRUE;
        assembly.pending = g_new0(NativeAssemblyWork, 1);
        assembly.pending->action = 5;
        assembly.pending->label = g_strdup("fresh_alias");
        assembly.temporary_address = 0x2000;
        assembly.temporary_valid = TRUE;
        assert(native_assembly_reply(&session, "^done,value=\"4\""));
        assert(!assembly.pending && assembly.ranges->len == 1);
        NativeNamedRange *fresh = assembly.ranges->pdata[0];
        assert(fresh->address == 0x2000 && !strcmp(fresh->name, "fresh_alias"));
    }
    g_free(rendered);
    g_free(watch.value);
    g_free(assembly.record);
    g_ptr_array_unref(session.locals);
    g_ptr_array_unref(session.stop_markers);
    g_ptr_array_unref(session.expressions);
    g_ptr_array_unref(assembly.rules);
    g_ptr_array_unref(assembly.ranges);
    g_ptr_array_unref(assembly.history);
    g_ptr_array_unref(assembly.objects);
    g_hash_table_unref(assembly.registers);
    g_hash_table_unref(assembly.previous_registers);
    return 0;
}
'''


class NativeTargetClear(unittest.TestCase):
    # EN: Compile real teardown/annotation code and verify local/remote cleanup without a display or ptrace.
    # RU: Компилирует настоящую очистку/подписи и проверяет локальное/удалённое завершение без дисплея и ptrace.
    def test_target_runtime_names(self):
        libraries = [ctypes.util.find_library(name) for name in ('gio-2.0', 'glib-2.0')]
        if not all(libraries) or not shutil.which('cc'):
            self.skipTest('Needs cc and GLib/GIO runtime libraries.')
        code = model_source(SESSION) + FIXTURE + STUBS
        code += function('src/insight-debuggers/disassembly/navigation-model.inc', 'native_navigation_clear')
        code += function('src/insight-debuggers/disassembly/views.inc', 'native_assembly_invalidate')
        code += function('src/insight-debuggers/gdb-lifecycle.inc', 'native_target_clear')
        code += function('src/insight-debuggers/disassembly/pipeline.inc', 'native_assembly_reply')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'target-clear.c'
            source.write_text(code + MAIN)
            executable = root / 'target-clear'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(source), '-o', str(executable), *['-l:' + name for name in libraries]], check=True)
            scenarios = ('local', 'remote', 'repeated', 'late-metadata', 'late-instructions',
                         'late-source', 'late-object', 'late-object-error', 'late-object-malformed')
            for scenario in scenarios:
                with self.subTest(scenario=scenario):
                    subprocess.run([str(executable), scenario], check=True)


if __name__ == '__main__':
    unittest.main()
