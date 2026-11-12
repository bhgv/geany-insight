#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Regress profile watch normalization, validation and removal using production callbacks without a display.
# RU: Проверяет нормализацию, проверку и удаление наблюдений профиля производственными обработчиками без дисплея.
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import function


STATE = r'''/* EN: Host production profile/watch callbacks on real GLib data with nonvisual entry tokens.
 * RU: Размещает обработчики профиля/наблюдений на настоящих данных GLib с невизуальными полями. */
#include <gio/gio.h>
#include <assert.h>
#include <string.h>
#define insight_tr(text) (text)
typedef GObject GtkWidget;
typedef GObject GtkButton;
typedef GObject GtkEntry;
#define GTK_WIDGET(value) (value)
#define GTK_ENTRY(value) (value)
#define GTK_LABEL(value) (value)
typedef struct {gchar *expression, *value;} NativeWatch;
typedef struct {gchar *number; guint catch_kind;} NativeSavedPoint;
typedef struct {gpointer status;} NativeRemote;
typedef struct {
    NativeRemote *remote;
    GPtrArray *saved_points, *expressions;
    GQueue launch_commands;
    GKeyFile *profile_restore_data;
    GtkWidget *window, *watch_entry, *watch_remove;
    guint watch_next;
    gboolean points_needed, profile_restoring, profile_restore_failed;
} NativeDebug;
static guint resume_count, graph_count;

/* EN: Treat the empty point section of profile as valid; index is unused because these tests contain only watches.
 * RU: Считает пустую секцию точек profile допустимой; index не используется, тесты содержат только наблюдения. */
static guint native_profile_point_kind(GKeyFile *profile, guint index)
{
    (void)profile;
    (void)index;
    return 0;
}

/* EN: Accept profile's empty locations section; points must be zero and error is untouched.
 * RU: Принимает пустую секцию подпозиций profile; points должен равняться нулю, error не меняется. */
static gboolean native_profile_locations_validate(GKeyFile *profile, gint points, GError **error)
{
    (void)profile;
    (void)error;
    assert(points == 0);
    return TRUE;
}

/* EN: Model loading zero source points into session; profile is unused and points must be zero.
 * RU: Моделирует загрузку нуля точек в session; profile не используется, points равен нулю. */
static void native_profile_locations_load(NativeDebug *session, GKeyFile *profile, guint points)
{
    (void)session;
    (void)profile;
    assert(points == 0);
}

/* EN: Reject an unexpected source point at index in profile; watch-only fixtures cannot request its command.
 * RU: Запрещает неожиданную точку index из profile; фикстура наблюдений не должна запрашивать такую команду. */
static gchar *native_profile_point_command(GKeyFile *profile, guint index)
{
    (void)profile;
    (void)index;
    assert(FALSE);
    return NULL;
}

/* EN: Return entry's borrowed fixture text, preserving the real callback's normalization path.
 * RU: Возвращает заимствованный текст фикстуры entry, сохраняя производственный путь нормализации обработчика. */
static const gchar *gtk_entry_get_text(GtkEntry *entry)
{
    return g_object_get_data(entry, "text");
}

/* EN: Ignore label/text rendering; validation errors are checked through their real GError values.
 * RU: Пропускает отрисовку label/text; ошибки проверки анализируются по настоящим значениям GError. */
static void gtk_label_set_text(gpointer label, const gchar *text)
{
    (void)label;
    (void)text;
}

/* EN: Count a scheduler wakeup for dialog/session without sending commands to GDB.
 * RU: Считает пробуждение очереди dialog/session без отправки команд GDB. */
static void native_debug_resume(GtkWidget *dialog, NativeDebug *session)
{
    (void)dialog;
    (void)session;
    resume_count++;
}

/* EN: Count session's graph refresh; compare is unused because this fixture checks expression identity, not rendering.
 * RU: Считает обновления графа session; compare не используется, фикстура проверяет выражения, а не отрисовку. */
static void native_watch_refresh(NativeDebug *session, gboolean compare)
{
    (void)session;
    (void)compare;
    graph_count++;
}
'''

HARNESS = r'''
/* EN: Build an owned profile with version, count and first expression for watch-only test scenarios.
 * RU: Создаёт профиль с version, count и первым expression для сценариев только с наблюдениями. */
static GKeyFile *watch_profile(gint version, gint count, const gchar *expression)
{
    GKeyFile *profile = g_key_file_new();
    g_key_file_set_integer(profile, "debugger", "version", version);
    g_key_file_set_integer(profile, "debugger", "point-count", 0);
    g_key_file_set_integer(profile, "debugger", "watch-count", count);
    if (expression)
        g_key_file_set_string(profile, "watches", "expression-0", expression);
    return profile;
}

/* EN: Validate profile against expected and release its error; return no value and leave profile owned by the caller.
 * RU: Сравнивает проверку profile с expected и освобождает ошибку; ничего не возвращает, профиль остаётся у вызывающего. */
static void check_valid(GKeyFile *profile, gboolean expected)
{
    GError *error = NULL;
    assert(native_profile_debug_validate(profile, &error) == expected);
    assert(expected ? error == NULL : error != NULL);
    g_clear_error(&error);
}

/* EN: Run argv[1]'s watch-profile scenario; argc is two and all GUI endpoints are plain GObjects.
 * RU: Запускает сценарий наблюдений профиля argv[1]; argc равен двум, вместо элементов GUI используются GObject.
 */
int main(int argc, char **argv)
{
    assert(argc == 2);
    GKeyFile *profile = watch_profile(4, 1, "value");
    if (!strcmp(argv[1], "legacy1"))
    {
        g_key_file_set_integer(profile, "debugger", "version", 1);
        NativeDebug session = {0};
        session.expressions = g_ptr_array_new_with_free_func(native_watch_free);
        NativeWatch *watch = g_new0(NativeWatch, 1);
        watch->expression = g_strdup("existing");
        watch->value = g_strdup("99");
        g_ptr_array_add(session.expressions, watch);
        native_profile_debug_load(&session, profile);
        assert(session.expressions->len == 1 && !graph_count && !resume_count);
        assert(!strcmp(watch->expression, "existing") && !strcmp(watch->value, "99"));
        assert(!session.profile_restore_data && g_queue_is_empty(&session.launch_commands));
        g_ptr_array_unref(session.expressions);
    }
    else if (!strcmp(argv[1], "blank") || !strcmp(argv[1], "duplicate") ||
        !strcmp(argv[1], "padded-duplicate") || !strcmp(argv[1], "newline") ||
        !strcmp(argv[1], "invalid-utf8") || !strcmp(argv[1], "missing"))
    {
        if (!strcmp(argv[1], "blank"))
            g_key_file_set_string(profile, "watches", "expression-0", " \t ");
        else if (!strcmp(argv[1], "newline"))
            g_key_file_set_string(profile, "watches", "expression-0", "value\n");
        else if (!strcmp(argv[1], "invalid-utf8"))
            g_key_file_set_string(profile, "watches", "expression-0", "value\377");
        else if (!strcmp(argv[1], "missing"))
            g_key_file_set_integer(profile, "debugger", "watch-count", 2);
        else
        {
            g_key_file_set_integer(profile, "debugger", "watch-count", 2);
            g_key_file_set_string(profile, "watches", "expression-1",
                !strcmp(argv[1], "duplicate") ? "value" : " \tvalue\t ");
        }
        check_valid(profile, FALSE);
    }
    else if (!strcmp(argv[1], "limits") || !strcmp(argv[1], "length"))
    {
        if (!strcmp(argv[1], "limits"))
        {
            g_key_file_set_integer(profile, "debugger", "watch-count", 32);
            for (guint index = 0; index < 32; index++)
            {
                gchar *key = g_strdup_printf("expression-%u", index);
                gchar *value = g_strdup_printf("value_%u", index);
                g_key_file_set_string(profile, "watches", key, value);
                g_free(key);
                g_free(value);
            }
            check_valid(profile, TRUE);
            g_key_file_set_integer(profile, "debugger", "watch-count", 33);
            check_valid(profile, FALSE);
        }
        else
        {
            gchar *value = g_strnfill(1025, 'x');
            g_key_file_set_string(profile, "watches", "expression-0", value);
            check_valid(profile, FALSE);
            value[1024] = '\0';
            g_key_file_set_string(profile, "watches", "expression-0", value);
            check_valid(profile, TRUE);
            g_free(value);
        }
    }
    else
    {
        const gchar *expected = !strcmp(argv[1], "quoted") ? "\"  a b  \"" :
            !strcmp(argv[1], "unicode") ? "объект.поле" : "marker";
        gchar *padded = g_strdup_printf(" \t%s\t ", expected);
        gint version = !strcmp(argv[1], "legacy2") ? 2 : !strcmp(argv[1], "legacy3") ? 3 : 4;
        g_key_file_set_integer(profile, "debugger", "version", version);
        g_key_file_set_string(profile, "watches", "expression-0", padded);
        g_free(padded);
        check_valid(profile, TRUE);
        NativeRemote remote = {0};
        NativeDebug session = {.remote = &remote};
        session.saved_points = g_ptr_array_new();
        session.expressions = g_ptr_array_new_with_free_func(native_watch_free);
        session.window = g_object_new(G_TYPE_OBJECT, NULL);
        session.watch_entry = g_object_new(G_TYPE_OBJECT, NULL);
        session.watch_remove = g_object_new(G_TYPE_OBJECT, NULL);
        g_object_set_data(session.window, "native-debug", &session);
        native_profile_debug_load(&session, profile);
        assert(session.expressions->len == 1 && g_queue_is_empty(&session.launch_commands));
        assert(session.points_needed && session.profile_restoring && !session.profile_restore_failed);
        g_object_set_data_full(session.watch_entry, "text", g_strdup(expected), g_free);
        native_watch_edit(session.watch_remove, session.window);
        assert(session.expressions->len == 0 && session.watch_next == 0);
        assert(graph_count == 2 && resume_count == 2);
        native_profile_debug_load(&session, profile);
        NativeWatch *watch = session.expressions->pdata[0];
        assert(!strcmp(watch->expression, expected) && !watch->value && session.watch_next == 0);
        g_clear_pointer(&session.profile_restore_data, g_key_file_unref);
        g_ptr_array_unref(session.expressions);
        g_ptr_array_unref(session.saved_points);
        g_object_unref(session.watch_remove);
        g_object_unref(session.watch_entry);
        g_object_unref(session.window);
    }
    g_key_file_unref(profile);
    return 0;
}
'''


class NativeProfileWatches(unittest.TestCase):
    # EN: Compile production watch validation/load/edit for self and run scenario without a GUI or GDB process.
    # RU: Компилирует проверку/загрузку/изменение наблюдений self и запускает scenario без GUI и процесса GDB.
    def exercise(self, scenario):
        if not shutil.which('cc') or not shutil.which('pkg-config'):
            self.skipTest('Needs cc, pkg-config and GLib development headers.')
        flags = subprocess.run(['pkg-config', '--cflags', '--libs', 'gio-2.0'], capture_output=True, text=True)
        if flags.returncode:
            self.skipTest('Needs GLib development headers.')
        helpers = [function('src/insight-debuggers/gdb-context.inc', 'native_context_id'),
                   function('src/insight-debuggers/gdb-watches.inc', 'native_watch_free')]
        helpers += [function('src/insight-debuggers/gdb-profile-debug-model.inc', name)
                    for name in ('native_profile_text_valid', 'native_profile_watch_expression', 'native_profile_debug_validate')]
        helpers += [function('src/insight-debuggers/gdb-profile-debug-ui.inc', 'native_profile_debug_load'),
                    function('src/insight-debuggers/gdb-actions.inc', 'native_watch_edit')]
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / 'profile-watches.c'
            source.write_text(STATE + '\n'.join(helpers) + HARNESS)
            executable = folder / 'profile-watches'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', str(source),
                            '-o', str(executable), *shlex.split(flags.stdout)], check=True)
            subprocess.run([str(executable), scenario], check=True)

    # EN: Verify self can remove an outer-padded profile expression through the real watch-edit callback.
    # RU: Проверяет удаление выражения профиля self с внешними пробелами настоящим обработчиком наблюдений.
    def test_remove_padded_watch(self):
        self.exercise('remove')

    # EN: Verify self preserves inner spaces in a quoted C++ string while trimming only outer whitespace.
    # RU: Проверяет сохранение внутренних пробелов строки C++ self при удалении только внешних пробелов.
    def test_quoted_whitespace(self):
        self.exercise('quoted')

    # EN: Verify self's UTF-8 expressions normalize and remain removable without byte corruption.
    # RU: Проверяет нормализацию и удаление выражений UTF-8 self без повреждения байтов.
    def test_unicode_watch(self):
        self.exercise('unicode')

    # EN: Verify self normalizes watches in legacy profile versions two and three as well.
    # RU: Проверяет нормализацию наблюдений self и в старых профилях версий два и три.
    def test_legacy_profiles(self):
        for scenario in ('legacy2', 'legacy3'):
            with self.subTest(scenario=scenario):
                self.exercise(scenario)

    # EN: Verify self's version-one form-only profile leaves current watches and values untouched.
    # RU: Проверяет сохранение текущих наблюдений/значений self профилем первой версии, содержащим только форму.
    def test_version_one_profile(self):
        self.exercise('legacy1')

    # EN: Verify self rejects blank-only expressions before profile application.
    # RU: Проверяет отклонение пустых после обрезки выражений self до применения профиля.
    def test_blank_watch(self):
        self.exercise('blank')

    # EN: Verify self rejects repeated watch identities.
    # RU: Проверяет отклонение повторяющихся выражений наблюдения self.
    def test_duplicate_watch(self):
        self.exercise('duplicate')

    # EN: Verify self also rejects duplicate identities disguised by outer whitespace.
    # RU: Проверяет отклонение дубликатов self, различающихся лишь внешними пробелами.
    def test_padded_duplicate(self):
        self.exercise('padded-duplicate')

    # EN: Verify self does not turn trailing line breaks into an accepted single-line expression.
    # RU: Проверяет запрет преобразования self завершающего перевода строки в допустимое однострочное выражение.
    def test_multiline_watch(self):
        self.exercise('newline')

    # EN: Verify self rejects malformed UTF-8 without loading a broken watch.
    # RU: Проверяет отклонение повреждённого UTF-8 self без загрузки некорректного наблюдения.
    def test_invalid_utf8(self):
        self.exercise('invalid-utf8')

    # EN: Verify self rejects a missing expression in the declared watch count.
    # RU: Проверяет отклонение отсутствующего выражения self в объявленном числе наблюдений.
    def test_missing_watch(self):
        self.exercise('missing')

    # EN: Verify self accepts 32 unique watches and rejects a declared 33rd watch.
    # RU: Проверяет принятие 32 уникальных наблюдений self и отклонение объявленного 33-го.
    def test_watch_count_limit(self):
        self.exercise('limits')

    # EN: Verify self accepts 1024-character expressions and rejects 1025 characters.
    # RU: Проверяет принятие выражений self из 1024 символов и отклонение 1025 символов.
    def test_expression_length(self):
        self.exercise('length')


if __name__ == '__main__':
    unittest.main()
