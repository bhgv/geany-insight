#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Regress queued, failed and confirmed C++ breakpoint profile rules using real GLib queues and key files.
# RU: Проверяет ожидающие, отклонённые и подтверждённые правила профиля точек C++ с очередями и INI-файлами GLib.
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import ROOT, function


STATE = r'''/* EN: Host production profile rules in a nonvisual GLib fixture.
 * RU: Размещает производственные правила профиля в невизуальной фикстуре GLib. */
#include <gio/gio.h>
#include <assert.h>
#include <string.h>
#define insight_tr(text) (text)
'''

SESSION = r'''
typedef struct {
    gchar *number;
    GPtrArray *disabled_locations;
} NativeSavedPoint;
typedef struct {gpointer status;} NativeRemote;
typedef struct {
    NativeRemote *remote;
    GPtrArray *profile_disabled_pending;
    GQueue launch_commands;
    gboolean profile_restoring, points_needed, profile_restore_failed;
    GKeyFile *profile_restore_data;
    guint profile_insert_next, profile_insert_count;
    gboolean profile_insert_pending;
} NativeDebug;
#define GTK_LABEL(value) (value)
static gchar *diagnostic;

/* EN: Capture text for label without constructing a GUI.
 * RU: Запоминает text для label без создания интерфейса. */
static void gtk_label_set_text(gpointer label, const gchar *text)
{
    (void)label;
    g_free(diagnostic);
    diagnostic = g_strdup(text);
}

/* EN: Ignore parent insertion bookkeeping for session/record; these tests begin with an already confirmed parent.
 * RU: Пропускает учёт вставки родителя session/record; тесты начинают с уже подтверждённого родителя. */
static void native_profile_location_parent_reply(NativeDebug *session, const gchar *record)
{
    (void)session;
    (void)record;
}
'''

HARNESS = r'''
/* EN: Assert that storing point in session preserves count source selectors in the actual serialized profile.
 * RU: Проверяет сохранение count селекторов исходника point сеанса session в настоящем сериализованном профиле. */
static void check_saved(NativeDebug *session, NativeSavedPoint *point, guint count)
{
    GKeyFile *profile = g_key_file_new();
    native_profile_locations_store(session, profile, point, 0);
    gsize length = 0;
    gchar *text = g_key_file_to_data(profile, &length, NULL);
    GKeyFile *loaded = g_key_file_new();
    assert(g_key_file_load_from_data(loaded, text, length, G_KEY_FILE_NONE, NULL));
    assert(g_key_file_get_integer(loaded, "breakpoint-0", "disabled-count", NULL) == (gint)count);
    if (count)
    {
        gchar *file = g_key_file_get_string(loaded, "breakpoint-0-location-0", "file", NULL);
        gchar *name = g_key_file_get_string(loaded, "breakpoint-0-location-0", "function", NULL);
        assert(!strcmp(file, "sample.cpp") && !strcmp(name, "sample<int>(int)"));
        assert(g_key_file_get_integer(loaded, "breakpoint-0-location-0", "line", NULL) == 7);
        g_free(name);
        g_free(file);
    }
    g_key_file_unref(loaded);
    g_key_file_unref(profile);
    g_free(text);
}

/* EN: Exercise argv[1]'s rule lifecycle; argc must be two and no GDB process or target is launched.
 * RU: Проверяет жизненный цикл правила argv[1]; argc равен двум, GDB и отлаживаемый процесс не запускаются. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    NativeRemote remote = {0};
    NativeDebug session = {.remote = &remote};
    session.profile_disabled_pending = g_ptr_array_new_with_free_func(native_location_override_free);
    NativeLocationOverride *override = g_new0(NativeLocationOverride, 1);
    override->parent_number = g_strdup("2");
    override->location = native_profile_location_parse(
        "{file=\"sample.cpp\",func=\"sample<int>(int)\",line=\"7\"}");
    assert(override->location);
    g_ptr_array_add(session.profile_disabled_pending, override);
    NativeSavedPoint point = {.number = "2"};
    point.disabled_locations = g_ptr_array_new_with_free_func(native_saved_location_free);
    const gchar *enabled = "^done,BreakpointTable={body=[bkpt={number=\"2.1\",enabled=\"y\","
        "file=\"sample.cpp\",func=\"sample<int>(int)\",line=\"7\"}]}";
    const gchar *disabled = "^done,BreakpointTable={body=[bkpt={number=\"2.1\",enabled=\"n\","
        "file=\"sample.cpp\",func=\"sample<int>(int)\",line=\"7\"}]}";
    const gchar *child = "{number=\"2.1\",enabled=\"n\",file=\"sample.cpp\","
        "func=\"sample<int>(int)\",line=\"7\"}";
    if (!strcmp(argv[1], "unresolved") || !strcmp(argv[1], "ambiguous") || !strcmp(argv[1], "wrong-parent"))
    {
        const gchar *record = !strcmp(argv[1], "unresolved") ? "^done,BreakpointTable={body=[]}" :
            !strcmp(argv[1], "wrong-parent") ?
            "^done,BreakpointTable={body=[bkpt={number=\"3.1\",enabled=\"y\",file=\"sample.cpp\","
            "func=\"sample<int>(int)\",line=\"7\"}]}" :
            "^done,BreakpointTable={body=[bkpt={number=\"2.1\",enabled=\"y\",file=\"sample.cpp\","
            "func=\"sample<int>(int)\",line=\"7\"},bkpt={number=\"2.2\",enabled=\"y\","
            "file=\"sample.cpp\",func=\"sample<int>(int)\",line=\"7\"}]}";
        assert(native_profile_locations_apply(&session, record) == 1);
        assert(!override->applied && g_queue_is_empty(&session.launch_commands));
        check_saved(&session, &point, 1);
    }
    else if (!strcmp(argv[1], "already-disabled"))
    {
        assert(native_profile_locations_apply(&session, disabled) == 0);
        assert(override->applied && g_queue_is_empty(&session.launch_commands));
        native_profile_location_capture(&point, child);
        check_saved(&session, &point, 1);
    }
    else
    {
        NativeLocationOverride *duplicate = NULL;
        if (!strcmp(argv[1], "duplicate-selectors"))
        {
            duplicate = g_new0(NativeLocationOverride, 1);
            duplicate->parent_number = g_strdup("2");
            duplicate->location = native_profile_location_parse(
                "{file=\"sample.cpp\",func=\"sample<int>(int)\",line=\"7\"}");
            g_ptr_array_add(session.profile_disabled_pending, duplicate);
        }
        native_profile_locations_apply(&session, enabled);
        assert(session.launch_commands.length == 1);
        if (duplicate)
            assert(!duplicate->applied && duplicate->queued);
        gchar *command = g_queue_pop_head(&session.launch_commands);
        assert(!strcmp(command, "-break-disable 2.1"));
        g_free(command);
        if (!strcmp(argv[1], "queued") || !strcmp(argv[1], "duplicate"))
        {
            if (!strcmp(argv[1], "duplicate"))
                for (guint update = 0; update < 3; update++)
                    native_profile_locations_apply(&session, enabled);
            assert(g_queue_is_empty(&session.launch_commands));
            check_saved(&session, &point, 1);
            assert(!override->applied);
        }
        else if (!strcmp(argv[1], "failure") || !strcmp(argv[1], "failed-confirmed") || !strcmp(argv[1], "retry"))
        {
            session.profile_restore_data = g_key_file_new();
            native_profile_debug_reply(&session, "^error,msg=\"Cannot disable location\"");
            assert(session.profile_restore_failed && !session.profile_restoring && !session.profile_restore_data);
            assert(diagnostic && strstr(diagnostic, "could not be restored"));
            g_queue_clear_full(&session.launch_commands, g_free);
            native_profile_locations_apply(&session, enabled);
            assert(g_queue_is_empty(&session.launch_commands));
            check_saved(&session, &point, 1);
            if (!strcmp(argv[1], "failed-confirmed"))
            {
                native_profile_locations_apply(&session, disabled);
                assert(override->applied);
                native_profile_location_capture(&point, child);
                check_saved(&session, &point, 1);
            }
            else if (!strcmp(argv[1], "retry"))
            {
                GKeyFile *saved = g_key_file_new();
                g_key_file_set_integer(saved, "debugger", "version", 4);
                native_profile_locations_store(&session, saved, &point, 0);
                native_profile_locations_load(&session, saved, 1);
                assert(session.profile_disabled_pending->len == 1);
                override = session.profile_disabled_pending->pdata[0];
                assert(!override->applied && !override->queued && !override->parent_number);
                override->parent_number = g_strdup("9");
                session.profile_restore_failed = FALSE;
                native_profile_locations_apply(&session,
                    "^done,BreakpointTable={body=[bkpt={number=\"9.1\",enabled=\"y\",file=\"sample.cpp\","
                    "func=\"sample<int>(int)\",line=\"7\"}]}");
                command = g_queue_pop_head(&session.launch_commands);
                assert(command && !strcmp(command, "-break-disable 9.1"));
                g_free(command);
                assert(!override->applied && override->queued);
                g_key_file_unref(saved);
            }
            else
                assert(!override->applied);
        }
        else
        {
            native_profile_locations_apply(&session, disabled);
            assert(override->applied && g_queue_is_empty(&session.launch_commands));
            if (duplicate)
                assert(duplicate->applied && !duplicate->queued);
            if (!strcmp(argv[1], "manual"))
            {
                native_profile_locations_apply(&session, enabled);
                assert(g_queue_is_empty(&session.launch_commands));
                check_saved(&session, &point, 0);
            }
            else
            {
                native_profile_location_capture(&point, child);
                check_saved(&session, &point, 1);
            }
        }
    }
    g_queue_clear_full(&session.launch_commands, g_free);
    g_ptr_array_unref(point.disabled_locations);
    g_ptr_array_unref(session.profile_disabled_pending);
    g_free(diagnostic);
    return 0;
}
'''


class NativePendingProfile(unittest.TestCase):
    # EN: Compile production rule matching/storage for self and run scenario with real GLib ownership and serialization.
    # RU: Компилирует сопоставление/сохранение правил self и запускает scenario с настоящим владением и сериализацией GLib.
    def exercise(self, scenario):
        if not shutil.which('cc') or not shutil.which('pkg-config'):
            self.skipTest('Needs cc, pkg-config and GLib development headers.')
        flags = subprocess.run(['pkg-config', '--cflags', '--libs', 'gio-2.0'], capture_output=True, text=True)
        if flags.returncode:
            self.skipTest('Needs GLib development headers.')
        declarations = STATE + (ROOT / 'src/insight-debuggers/gdb-profile-location-state.inc').read_text() + SESSION
        helpers = [function('src/insight-debuggers/gdb-context.inc', name)
                   for name in ('native_debug_field', 'native_next_tuple', 'native_context_id')]
        helpers += [function('src/insight-debuggers/gdb-breakpoints.inc', 'native_point_number'),
                    function('src/insight-debuggers/gdb-profile-debug-model.inc', 'native_profile_text_valid')]
        helpers += [function('src/insight-debuggers/gdb-profile-locations-model.inc', name)
                    for name in ('native_saved_location_free', 'native_profile_location_parse', 'native_profile_location_key')]
        helpers += [(ROOT / 'src/insight-debuggers/gdb-profile-locations-snapshot.inc').read_text()]
        helpers += [function('src/insight-debuggers/gdb-profile-locations-storage.inc', name)
                    for name in ('native_saved_location_add', 'native_profile_location_capture',
                                 'native_profile_locations_store', 'native_profile_locations_load')]
        helpers += [function('src/insight-debuggers/gdb-profile-debug-ui.inc', 'native_profile_debug_reply')]
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / 'profile-pending.c'
            source.write_text(declarations + '\n'.join(helpers) + HARNESS)
            executable = folder / 'profile-pending'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(executable),
                            *shlex.split(flags.stdout)], check=True)
            subprocess.run([str(executable), scenario], check=True)

    # EN: Verify self's queued rule survives saving before GDB confirms it.
    # RU: Проверяет сохранение ожидающего правила self до подтверждения GDB.
    def test_queued_rule(self):
        self.exercise('queued')

    # EN: Verify self's repeated snapshots do not duplicate the queued disable command or lose its saved selector.
    # RU: Проверяет отсутствие повторных команд выключения self и потери селектора при повторных снимках.
    def test_duplicate_snapshots(self):
        self.exercise('duplicate')

    # EN: Verify self's rejected disable preserves the selector and suspends automatic retries.
    # RU: Проверяет сохранение селектора self и остановку автоматических повторов после отказа выключения.
    def test_failed_rule(self):
        self.exercise('failure')

    # EN: Verify self's confirmed disabled location is serialized exactly once.
    # RU: Проверяет однократное сохранение подтверждённой выключенной подпозиции self.
    def test_confirmed_rule(self):
        self.exercise('confirmed')

    # EN: Verify self's already-disabled location needs no extra GDB command.
    # RU: Проверяет отсутствие лишних команд GDB self для уже выключенной подпозиции.
    def test_already_disabled(self):
        self.exercise('already-disabled')

    # EN: Verify self's missing symbols preserve the unresolved selector without guessing.
    # RU: Проверяет сохранение селектора self без угадывания при отсутствующих символах.
    def test_unresolved_rule(self):
        self.exercise('unresolved')

    # EN: Verify self's ambiguous symbols never disable an arbitrary matching location.
    # RU: Проверяет запрет произвольного выключения self при неоднозначных символах.
    def test_ambiguous_rule(self):
        self.exercise('ambiguous')

    # EN: Verify self's manual enable after confirmation takes precedence over the old saved disable.
    # RU: Проверяет приоритет ручного включения self после подтверждения над прежним сохранённым выключением.
    def test_manual_override(self):
        self.exercise('manual')

    # EN: Verify self still accepts a confirmed disabled snapshot after another restore command failed.
    # RU: Проверяет принятие self подтверждённого выключенного снимка после ошибки другой команды восстановления.
    def test_confirmation_after_failure(self):
        self.exercise('failed-confirmed')

    # EN: Verify self's failed rule survives saving/loading and retries against a fresh parent ID.
    # RU: Проверяет сохранение/загрузку отклонённого правила self и повтор с новым ID родителя.
    def test_explicit_retry(self):
        self.exercise('retry')

    # EN: Verify self's selector never disables another parent with identical source coordinates.
    # RU: Проверяет запрет выключения self у другого родителя с совпадающими координатами исходника.
    def test_wrong_parent(self):
        self.exercise('wrong-parent')

    # EN: Verify self's duplicate selectors share one command and both await confirmation before being consumed.
    # RU: Проверяет общую команду дубликатов self и ожидание подтверждения обоими селекторами до применения.
    def test_duplicate_selectors(self):
        self.exercise('duplicate-selectors')


if __name__ == '__main__':
    unittest.main()
