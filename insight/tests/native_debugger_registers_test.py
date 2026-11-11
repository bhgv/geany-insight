#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Run production register snapshot rendering against real GTK models without a display.
# RU: Выполняет производственное отображение регистров на настоящих моделях GTK без дисплея.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_names_fixture import model_source
from native_debugger_models_test import function

SESSION = '\ntypedef struct {NativeAssembly *assembly; char *selected_thread, *selected_frame;} NativeDebug;\n'
EXTRA = r'''
typedef void GtkTreeModel;
typedef struct {gint stamp; void *user_data, *user_data2, *user_data3;} GtkTreeIter;
typedef struct {void *dummy1, *dummy2, *dummy3; int dummy4, dummy5; void *dummy6;} GHashTableIter;
#define GTK_TREE_MODEL(a) (a)
#define G_GUINT64_FORMAT "lu"
extern void g_hash_table_iter_init(GHashTableIter *, GHashTable *);
extern int g_hash_table_iter_next(GHashTableIter *, void **, void **);
extern void *gtk_list_store_new(int, ...);
extern void gtk_list_store_clear(void *);
extern void gtk_list_store_insert_with_values(void *, GtkTreeIter *, int, ...);
extern int gtk_tree_model_get_iter_first(void *, GtkTreeIter *);
extern void gtk_tree_model_get(void *, GtkTreeIter *, ...);
extern int gtk_tree_model_iter_n_children(void *, GtkTreeIter *);
extern void g_object_unref(void *);
'''
MAIN = r'''
/* EN: Set register name to value in assembly's actual cache.
 * RU: Задаёт значение value регистра name в настоящем кэше assembly. */
static void fixture_register(NativeAssembly *assembly, const char *name, guint64 value)
{
    guint64 *stored = g_new(guint64, 1);
    *stored = value;
    g_hash_table_replace(assembly->registers, g_strdup(name), stored);
}

/* EN: Check the first model row against name/value/label/change expectations, freeing GTK-owned returned strings.
 * RU: Проверяет первую строку модели по name/value/label/changed и освобождает возвращённые GTK строки. */
static void fixture_row(NativeAssembly *assembly, const char *expected, const char *label, gboolean changed)
{
    GtkTreeIter row;
    assert(gtk_tree_model_get_iter_first(assembly->register_rows, &row));
    char *name = NULL, *hex = NULL, *symbol = NULL, *change = NULL;
    gtk_tree_model_get(assembly->register_rows, &row, 0, &name, 1, &hex, 3, &symbol, 4, &change, -1);
    assert(!strcmp(name, "rax") && !strcmp(hex, expected) && !strcmp(symbol, label));
    assert(!strcmp(change, changed ? "Changed" : ""));
    g_free(change);
    g_free(symbol);
    g_free(hex);
    g_free(name);
}

/* EN: Verify actual GTK rows, type-field aliases, x86/ARM/RISC-V names and thread/frame change baselines.
 * RU: Проверяет настоящие строки GTK, имена полей, регистры x86/ARM/RISC-V и границы сравнения потоков/кадров. */
int main(void)
{
    NativeAssembly assembly = {0};
    assembly.ranges = g_ptr_array_new_with_free_func(native_name_range_free);
    assembly.register_names = g_ptr_array_new_with_free_func(g_free);
    assembly.registers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    assembly.previous_registers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    assembly.register_rows = gtk_list_store_new(6, 64UL, 64UL, 64UL, 64UL, 64UL, 64UL);
    NativeDebug session = {.assembly = &assembly, .selected_thread = "1", .selected_frame = "0"};
    NativeNamedRange *range = g_new0(NativeNamedRange, 1);
    range->address = 0x4008;
    range->size = 4;
    range->name = g_strdup("packet.count");
    range->manual = TRUE;
    g_ptr_array_add(assembly.ranges, range);
    const char *names[] = {"rax", "x0", "a0", "sp", "xmm0"};
    for (guint i = 0; i < G_N_ELEMENTS(names); i++)
    {
        g_ptr_array_add(assembly.register_names, g_strdup(names[i]));
        fixture_register(&assembly, names[i], 0x4008);
    }
    native_assembly_register_rows(&session);
    assert(gtk_tree_model_iter_n_children(assembly.register_rows, NULL) == 4);
    fixture_row(&assembly, "0x0000000000004008", "packet.count", FALSE);
    fixture_register(&assembly, "rax", 0x4009);
    native_assembly_register_rows(&session);
    fixture_row(&assembly, "0x0000000000004009", "packet.count+0x1", TRUE);
    native_assembly_register_rows(&session);
    fixture_row(&assembly, "0x0000000000004009", "packet.count+0x1", FALSE);
    session.selected_frame = "1";
    fixture_register(&assembly, "rax", 0x4008);
    native_assembly_register_rows(&session);
    fixture_row(&assembly, "0x0000000000004008", "packet.count", FALSE);
    session.selected_thread = "2";
    fixture_register(&assembly, "rax", 0x4009);
    native_assembly_register_rows(&session);
    fixture_row(&assembly, "0x0000000000004009", "packet.count+0x1", FALSE);
    g_object_unref(assembly.register_rows);
    g_hash_table_unref(assembly.previous_registers);
    g_hash_table_unref(assembly.registers);
    g_ptr_array_unref(assembly.register_names);
    g_ptr_array_unref(assembly.ranges);
    g_free(assembly.register_thread);
    g_free(assembly.register_frame);
    return 0;
}
'''


class NativeRegisters(unittest.TestCase):
    # EN: Compile the real row builder and assert snapshot/alias semantics using GTK/GLib runtime libraries.
    # RU: Компилирует настоящий построитель строк и проверяет снимки/имена на библиотеках GTK/GLib.
    def test_register_snapshots(self):
        libraries = [ctypes.util.find_library(name) for name in ('gtk-3', 'gobject-2.0', 'gio-2.0', 'glib-2.0')]
        if not all(libraries) or not shutil.which('cc'):
            self.skipTest('Needs cc and GTK/GLib runtime libraries.')
        code = model_source(SESSION) + EXTRA
        code += function('src/insight-debuggers/disassembly/registers-ui.inc', 'native_assembly_register_rows') + MAIN
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'registers.c'
            source.write_text(code)
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(source), '-o', str(root / 'registers'),
                            *['-l:' + name for name in libraries]], check=True)
            subprocess.run([str(root / 'registers')], check=True)


if __name__ == '__main__':
    unittest.main()
