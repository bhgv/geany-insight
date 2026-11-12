#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Exercise production C++ location persistence against real GDB and ambiguous/missing symbol records.
# RU: Проверяет сохранение подпозиций C++ с настоящим GDB и неоднозначными/отсутствующими символами.
import ctypes.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import ROOT, function
from native_debugger_profile_test import DECLARATIONS

EXTRA = r'''
#include <stdlib.h>
typedef void *gpointer;
typedef unsigned long guint64;
typedef struct {void **pdata; guint len;} GPtrArray;
typedef struct {void *head, *tail; guint length;} GQueue;
typedef struct GHashTable GHashTable;
#define G_MAXINT 2147483647
#define g_new0(t,n) ((t *)calloc((n), sizeof(t)))
#define g_steal_pointer(p) g_steal_pointer_impl((void **)(p))
/* EN: Transfer the owned pointer at p to the caller, clearing its old slot.
 * RU: Передаёт указатель p вызывающему и очищает прежнее место хранения. */
static void *g_steal_pointer_impl(void **p)
{
    void *value = *p;
    *p = NULL;
    return value;
}
extern char *g_strdup(const char *);
extern char *g_strndup(const char *, gsize);
extern char *g_strcompress(const char *);
extern int g_strcmp0(const char *, const char *);
extern int g_str_has_prefix(const char *, const char *);
extern guint g_str_hash(const void *);
extern int g_str_equal(const void *, const void *);
extern GHashTable *g_hash_table_new_full(guint (*)(const void *), int (*)(const void *, const void *),
                                       void (*)(void *), void (*)(void *));
extern int g_hash_table_insert(GHashTable *, void *, void *);
extern void *g_hash_table_lookup(GHashTable *, const void *);
extern void g_hash_table_unref(GHashTable *);
extern GPtrArray *g_ptr_array_new_with_free_func(void (*)(void *));
extern void g_ptr_array_add(GPtrArray *, void *);
extern void g_ptr_array_unref(GPtrArray *);
extern void g_ptr_array_set_size(GPtrArray *, guint);
extern void g_queue_push_tail(GQueue *, void *);
extern void g_queue_push_head(GQueue *, void *);
extern void *g_queue_pop_head(GQueue *);
extern int g_ascii_string_to_unsigned(const char *, guint, guint64, guint64, guint64 *, GError **);
'''

SESSION = r'''
typedef struct {
    char *location, *condition, *number, *filter;
    guint catch_kind, ignore;
    gboolean enabled;
    GPtrArray *disabled_locations;
} NativeSavedPoint;
typedef struct {char *expression, *value;} NativeWatch;
typedef struct {void *status;} NativeRemote;
typedef struct {
    NativeRemote *remote;
    GPtrArray *saved_points, *expressions;
    gboolean profile_restore_failed;
    GKeyFile *profile_restore_data;
    GPtrArray *profile_disabled_pending;
    GQueue launch_commands;
    guint profile_insert_next, profile_insert_count, profile_insert_index;
    gboolean profile_insert_pending, profile_restoring, points_needed;
} NativeDebug;
'''

MAIN = r'''
/* EN: Validate restoration using enabled record argv[1] and optional confirmed record argv[2]; argc is two or three.
 * Preserve queued selectors until the real GDB snapshot confirms the disable.
 * RU: Проверяет восстановление по включённому снимку argv[1] и необязательному подтверждению argv[2]; argc равен двум либо трём.
 * Сохраняет ожидающие селекторы до подтверждения выключения настоящим снимком GDB. */
int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 3);
    GKeyFile *profile = g_key_file_new();
    g_key_file_set_integer(profile, "debugger", "version", 3);
    g_key_file_set_integer(profile, "breakpoint-0", "disabled-count", 1);
    g_key_file_set_string(profile, "breakpoint-0-location-0", "file", "sample.cpp");
    g_key_file_set_string(profile, "breakpoint-0-location-0", "function", "sample<int>(int)");
    g_key_file_set_integer(profile, "breakpoint-0-location-0", "line", 1);
    NativeDebug session = {0};
    session.profile_disabled_pending = g_ptr_array_new_with_free_func(native_location_override_free);
    native_profile_locations_load(&session, profile, 1);
    session.profile_restoring = TRUE;
    native_profile_command_sent(&session, "-break-insert -f sample");
    native_profile_location_parent_reply(&session, "^done,bkpt={number=\"2\"}");
    NativeLocationOverride *override = session.profile_disabled_pending->pdata[0];
    assert(!strcmp(override->parent_number, "2"));
    assert(native_profile_locations_apply(&session, "^done,BreakpointTable={body=[]}") == 1);
    assert(!override->applied && !session.launch_commands.length);
    const char *ambiguous = "^done,BreakpointTable={body=[bkpt={number=\"2\",locations=["
        "{number=\"2.1\",enabled=\"y\",file=\"sample.cpp\",func=\"sample<int>(int)\",line=\"1\"},"
        "{number=\"2.2\",enabled=\"y\",file=\"sample.cpp\",func=\"sample<int>(int)\",line=\"1\"}]}]}";
    assert(native_profile_locations_apply(&session, ambiguous) == 1);
    assert(!override->applied && !session.launch_commands.length);
    NativeSavedPoint point = {0};
    point.number = "2";
    point.disabled_locations = g_ptr_array_new_with_free_func(native_saved_location_free);
    native_profile_locations_store(&session, profile, &point, 0);
    assert(g_key_file_get_integer(profile, "breakpoint-0", "disabled-count", NULL) == 1);
    assert(native_profile_locations_apply(&session, argv[1]) == 0);
    char *command = g_queue_pop_head(&session.launch_commands);
    assert(command && !strcmp(command, "-break-disable 2.1"));
    puts(command);
    g_free(command);
    assert(native_profile_locations_apply(&session, argv[1]) == 0);
    assert(!session.launch_commands.length);
    assert(!override->applied && override->queued);
    if (argc == 3)
    {
        assert(native_profile_locations_apply(&session, argv[2]) == 0 && override->applied);
        native_profile_location_capture(&point,
            "{number=\"2.1\",enabled=\"n\",file=\"sample.cpp\",func=\"sample<int>(int)\",line=\"1\"}");
    }
    native_profile_locations_store(&session, profile, &point, 0);
    GError *error = NULL;
    assert(native_profile_locations_validate(profile, 1, &error) && !error);
    assert(g_key_file_get_integer(profile, "breakpoint-0", "disabled-count", NULL) == 1);
    g_ptr_array_unref(point.disabled_locations);
    g_ptr_array_unref(session.profile_disabled_pending);
    g_key_file_unref(profile);
    return 0;
}
'''


class NativeProfileLocations(unittest.TestCase):
    # EN: Compile production helpers for self, send their disable to real GDB and confirm it from the returned snapshot.
    # RU: Компилирует обработчики self, отправляет выключение настоящему GDB и подтверждает его ответным снимком.
    def test_template_location_roundtrip(self):
        gdb = os.environ.get('GDB', shutil.which('gdb'))
        libraries = [ctypes.util.find_library(name) for name in ('gio-2.0', 'glib-2.0')]
        if not gdb or not shutil.which('c++') or not all(libraries):
            self.skipTest('Needs GDB, C++ and GLib/GIO runtime libraries.')
        helpers = '\n'.join(function('src/insight-debuggers/gdb-context.inc', name)
                            for name in ('native_debug_field', 'native_next_tuple', 'native_context_id'))
        helpers += '\n' + function('src/insight-debuggers/gdb-breakpoints.inc', 'native_point_number')
        helpers += '\n' + function('src/insight-debuggers/gdb-profile-debug-model.inc', 'native_profile_text_valid')
        helpers += '\n' + (ROOT / 'src/insight-debuggers/gdb-profile-catch-model.inc').read_text()
        state = (ROOT / 'src/insight-debuggers/gdb-profile-location-state.inc').read_text()
        for name in ('model', 'snapshot', 'storage'):
            helpers += '\n' + (ROOT / f'src/insight-debuggers/gdb-profile-locations-{name}.inc').read_text()
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            root = Path(directory)
            (root / 'sample.cpp').write_text(
                'template<typename T> __attribute__((noinline)) int sample(T x) { return (int)x; }\n'
                'int main() { return sample(1) + sample(2.0); }\n')
            subprocess.run(['c++', '-g', '-O0', 'sample.cpp', '-o', 'program'], cwd=root, check=True)
            commands = '-break-insert main\n-break-delete 1\n-break-insert sample\n-break-list\n-gdb-exit\n'
            result = subprocess.run([gdb, '--nx', '--quiet', '--interpreter=mi2', './program'],
                                    input=commands, cwd=root, text=True, capture_output=True, check=True, timeout=15)
            self.assertNotIn('^error', result.stdout)
            record = next(line for line in result.stdout.splitlines() if line.startswith('^done,BreakpointTable='))
            (root / 'fixture.c').write_text(DECLARATIONS + EXTRA + state + SESSION + helpers + MAIN)
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(root / 'fixture.c'),
                            '-o', str(root / 'fixture'), *['-l:' + name for name in libraries]], check=True)
            command = subprocess.check_output([str(root / 'fixture'), record], text=True)
            result = subprocess.run([gdb, '--nx', '--quiet', '--interpreter=mi2', './program'],
                                    input=commands.replace('-break-list\n-gdb-exit\n', command + '-break-list\n-gdb-exit\n'),
                                    cwd=root, text=True, capture_output=True, check=True, timeout=15)
            self.assertNotIn('^error', result.stdout)
            self.assertIn('number="2.1",enabled="n"', result.stdout)
            self.assertIn('number="2.2",enabled="y"', result.stdout)
            confirmed = next(line for line in result.stdout.splitlines()
                             if line.startswith('^done,BreakpointTable='))
            replay = subprocess.check_output([str(root / 'fixture'), record, confirmed], text=True)
            self.assertEqual(replay, command)


if __name__ == '__main__':
    unittest.main()
