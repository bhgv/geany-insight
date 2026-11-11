#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Replay persisted exception recipes with production helpers and fresh real GDB breakpoint identifiers.
# RU: Восстанавливает рецепты исключений производственными функциями с новыми настоящими идентификаторами GDB.
import ctypes.util
import os
import re
import select
import shutil
import subprocess
import tempfile
import time
import unittest

from native_debugger_models_test import ROOT, function
from native_debugger_profile_test import DECLARATIONS
from native_debugger_profile_locations_test import EXTRA, SESSION

UI_STUB = r'''
#define GTK_LABEL(a) (a)
#define g_clear_pointer(p,f) do {f(*(p)); *(p)=NULL;} while(0)
extern int g_queue_is_empty(GQueue *);
/* EN: Ignore label/text in the model fixture; the actual snapshot still exercises restoration completion.
 * RU: Игнорирует label/text в фикстуре модели; настоящий снимок проверяет завершение восстановления. */
static void gtk_label_set_text(void *label, const char *text)
{
    (void)label;
    (void)text;
}
'''

MAIN = r'''
/* EN: Build a mixed source/exception profile with a disabled conditional throw point and ignored hits.
 * RU: Создаёт смешанный профиль с выключенной условной точкой throw и пропусками срабатываний. */
static GKeyFile *fixture_profile(void)
{
    GKeyFile *profile = g_key_file_new();
    const char *kinds[] = {"source", "throw", "catch", "rethrow"};
    g_key_file_set_integer(profile, "debugger", "version", 4);
    g_key_file_set_integer(profile, "debugger", "point-count", 4);
    g_key_file_set_integer(profile, "debugger", "watch-count", 0);
    for (guint i = 0; i < 4; i++)
    {
        char *group = g_strdup_printf("breakpoint-%u", i);
        g_key_file_set_string(profile, group, "kind", kinds[i]);
        g_key_file_set_string(profile, group, i ? "filter" : "location", i == 1 ? "std::.*" : i ? "" : "main");
        g_key_file_set_string(profile, group, "condition", i == 1 ? "1" : "");
        g_key_file_set_boolean(profile, group, "enabled", i != 1);
        g_key_file_set_integer(profile, group, "ignore", i == 1 ? 3 : 0);
        g_key_file_set_integer(profile, group, "disabled-count", 0);
        g_free(group);
    }
    return profile;
}

/* EN: Validate the schema, emit a creation command, or replay settings from argv's confirmed insertion reply.
 * RU: Проверяет схему, выводит команду создания либо восстанавливает настройки по подтверждённому ответу argv. */
int main(int argc, char **argv)
{
    assert(argc == 3 || argc == 4);
    GKeyFile *profile = fixture_profile();
    GError *error = NULL;
    assert(native_profile_debug_validate(profile, &error) && !error);
    g_key_file_set_string(profile, "breakpoint-1", "kind", "shell");
    assert(!native_profile_debug_validate(profile, &error));
    g_clear_error(&error);
    g_key_file_set_string(profile, "breakpoint-1", "kind", "throw");
    g_key_file_set_integer(profile, "breakpoint-1", "disabled-count", 1);
    assert(!native_profile_debug_validate(profile, &error));
    g_clear_error(&error);
    g_key_file_set_integer(profile, "breakpoint-1", "disabled-count", 0);
    g_key_file_set_string(profile, "breakpoint-1", "filter", "a\nb");
    assert(!native_profile_debug_validate(profile, &error));
    g_clear_error(&error);
    g_key_file_set_string(profile, "breakpoint-1", "filter", "std::.*");
    if (!strcmp(argv[1], "snapshot"))
    {
        NativeRemote remote = {0};
        NativeDebug session = {.remote = &remote};
        session.saved_points = g_ptr_array_new_with_free_func(native_saved_point_free);
        session.profile_disabled_pending = g_ptr_array_new_with_free_func(native_location_override_free);
        session.expressions = g_ptr_array_new_with_free_func(NULL);
        native_profile_points_snapshot(&session, argv[2]);
        assert(session.saved_points->len == 5);
        guint exceptions = 0;
        for (guint i = 0; i < session.saved_points->len; i++)
        {
            NativeSavedPoint *point = session.saved_points->pdata[i];
            if (point->catch_kind)
                exceptions++;
            if (point->catch_kind == 1)
            {
                assert(!point->enabled && point->ignore == 3);
                assert(!strcmp(point->filter, "std::.*") && !strcmp(point->condition, "1"));
            }
        }
        assert(exceptions == 3);
        GKeyFile *saved = g_key_file_new();
        g_key_file_set_integer(saved, "debugger", "version", 4);
        native_profile_debug_store(&session, saved);
        assert(native_profile_debug_validate(saved, &error) && !error);
        g_key_file_unref(saved);
        g_ptr_array_unref(session.expressions);
        g_ptr_array_unref(session.profile_disabled_pending);
        g_ptr_array_unref(session.saved_points);
        g_key_file_unref(profile);
        return 0;
    }
    guint index = (guint)atoi(argv[2]);
    assert(index < 4);
    char *command = native_profile_point_command(profile, index);
    if (!strcmp(argv[1], "create"))
        puts(command);
    else
    {
        assert(argc == 4);
        NativeDebug session = {0};
        session.profile_restore_data = profile;
        session.profile_restoring = TRUE;
        session.profile_insert_next = index;
        session.profile_insert_count = 4;
        session.profile_disabled_pending = g_ptr_array_new_with_free_func(native_location_override_free);
        native_profile_command_sent(&session, command);
        assert(session.profile_insert_pending && session.profile_insert_index == index);
        native_profile_location_parent_reply(&session, argv[3]);
        assert(!session.profile_insert_pending);
        char *setting;
        while ((setting = g_queue_pop_head(&session.launch_commands)))
        {
            puts(setting);
            g_free(setting);
        }
        g_ptr_array_unref(session.profile_disabled_pending);
    }
    g_free(command);
    g_key_file_unref(profile);
    return 0;
}
'''


class CatchpointProfiles(unittest.TestCase):
    # EN: Compile the real model and replay a mixed profile twice, without starting an inferior or requiring ptrace.
    # RU: Компилирует настоящую модель и дважды восстанавливает смешанный профиль без запуска программы и ptrace.
    def test_mixed_profile_replay(self):
        gdb = os.environ.get('GDB', shutil.which('gdb'))
        libraries = [ctypes.util.find_library(name) for name in ('gio-2.0', 'glib-2.0')]
        if not gdb or not shutil.which('cc') or not all(libraries):
            self.skipTest('Needs GDB, cc and GLib/GIO runtime.')
        state = (ROOT / 'src/insight-debuggers/gdb-profile-location-state.inc').read_text()
        helpers = '\n'.join(function('src/insight-debuggers/gdb-context.inc', name)
                            for name in ('native_debug_field', 'native_next_tuple', 'native_context_id'))
        helpers += '\n' + function('src/insight-debuggers/gdb-breakpoints.inc', 'native_point_number')
        helpers += '\n' + function('src/insight-debuggers/gdb-catchpoints-model.inc', 'native_catch_command')
        for name in ('catch-model', 'debug-model', 'locations-model', 'locations-snapshot', 'locations-storage'):
            helpers += '\n' + (ROOT / f'src/insight-debuggers/gdb-profile-{name}.inc').read_text()
        for name in ('native_saved_point_free', 'native_profile_points_snapshot', 'native_profile_debug_store'):
            helpers += '\n' + function('src/insight-debuggers/gdb-profile-debug-ui.inc', name)
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            root = os.path.join(directory, 'replay')
            with open(root + '.c', 'w') as source:
                source.write(DECLARATIONS + EXTRA + state + SESSION + UI_STUB + helpers + MAIN)
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            root + '.c', '-o', root, *['-l:' + name for name in libraries]], check=True)
            with subprocess.Popen([gdb, '--nx', '--quiet', '--interpreter=mi2'], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0) as process:
                # EN: Send command and return its MI result within a bounded wait; include output on failure.
                # RU: Отправляет command и получает MI-ответ с ограниченным ожиданием; при ошибке показывает вывод.
                def command(text):
                    process.stdin.write((text + '\n').encode())
                    process.stdin.flush()
                    lines = []
                    deadline = time.monotonic() + 10
                    while time.monotonic() < deadline:
                        if not select.select([process.stdout], [], [], 0.1)[0]:
                            continue
                        line = process.stdout.readline().decode().strip()
                        lines.append(line)
                        if line.startswith('^'):
                            self.assertFalse(line.startswith('^error'), '\n'.join(lines))
                            return line
                    self.fail('Missing MI result: ' + '\n'.join(lines))

                try:
                    command('-break-insert -f unrelated')
                    previous = []
                    for _ in range(2):
                        identifiers = []
                        for index in range(4):
                            creation = subprocess.check_output([root, 'create', str(index)], text=True).strip()
                            reply = command(creation)
                            number = re.search(r'number="(\d+)"', reply).group(1)
                            identifiers.append(number)
                            settings = subprocess.check_output([root, 'reply', str(index), reply], text=True).splitlines()
                            if index == 1:
                                self.assertEqual(settings, [f'-break-disable {number}',
                                    f'-break-condition {number} "1"', f'-break-after {number} 3'])
                            for setting in settings:
                                command(setting)
                        snapshot = command('-break-list')
                        self.assertIn(f'number="{identifiers[1]}",type="catchpoint",disp="keep",enabled="n"', snapshot)
                        self.assertIn('regexp="std::.*"', snapshot)
                        self.assertIn('cond="1"', snapshot)
                        self.assertIn('ignore="3"', snapshot)
                        subprocess.run([root, 'snapshot', snapshot], check=True)
                        self.assertNotEqual(previous, identifiers)
                        previous = identifiers
                        command('-break-delete ' + ' '.join(identifiers))
                finally:
                    if process.poll() is None:
                        process.stdin.write(b'-gdb-exit\n')
                        process.stdin.flush()
                        process.communicate(timeout=5)


if __name__ == '__main__':
    unittest.main()
