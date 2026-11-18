#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Exercise production script-frame rejection paths without GTK headers or a running VM.
# RU: Проверяет производственные ветви отказа выбора кадров скриптов без заголовков GTK и работающей VM.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import function

STATE = r'''
#include <assert.h>
#include <string.h>
typedef char gchar;
typedef int gint, gboolean;
typedef unsigned guint;
typedef unsigned long long guint64;
typedef void GtkComboBox;
typedef void *gpointer;
typedef struct GError GError;
struct GError {guint domain; gint code; gchar *message;};
typedef struct {
    gboolean updating_stack_frames, python, inspecting_frame;
    unsigned long paused_line;
    guint inspection_frame;
    void *stack_frames, *step_controls, *status, *engine, *python_engine;
} ScriptDebugWindow;
#define TRUE 1
#define FALSE 0
#define GTK_COMBO_BOX(value) (value)
#define GTK_LABEL(value) (value)
#define insight_tr(value) (value)
extern int g_snprintf(char *, unsigned long, const char *, ...);
extern gboolean g_ascii_string_to_unsigned(const char *, guint, guint64, guint64, guint64 *, GError **);
extern void g_clear_error(GError **);
static ScriptDebugWindow *active;
static const char *selection = "1";
static guint sends, restores;
static const char *message;

/* EN: Model an idle paused view; execution-state guards are exercised by the GUI lifecycle scenario.
 * RU: Моделирует свободный приостановленный view; ограничения исполнения проверяются сценарием GUI. */
static gboolean debug_is_running(ScriptDebugWindow *view)
{
    (void)view;
    return FALSE;
}

/* EN: Return the requested selector row for frames without GTK storage.
 * RU: Возвращает запрошенную строку frames без хранилища GTK. */
static const char *gtk_combo_box_get_active_id(GtkComboBox *frames)
{
    (void)frames;
    return selection;
}

/* EN: Restore frames to id only under the production recursion guard; count restores.
 * RU: Восстанавливает frames в id только под производственной защитой от рекурсии; считает возвраты. */
static gboolean gtk_combo_box_set_active_id(GtkComboBox *frames, const char *id)
{
    (void)frames;
    assert(active->updating_stack_frames && !strcmp(id, "0"));
    selection = "0";
    restores++;
    return TRUE;
}

/* EN: Reject Python command for engine without allocating error; count attempted sends.
 * RU: Отклоняет Python-команду command движка engine без создания error; считает попытки отправки. */
static gboolean insight_python_debug_send(void *engine, const char *command, GError **error)
{
    (void)engine;
    (void)error;
    assert(!strcmp(command, "frame 1\n"));
    sends++;
    return FALSE;
}

/* EN: Reject unavailable Lua frame level for engine without changing output coordinates.
 * RU: Отклоняет недоступный Lua-кадр level движка engine без изменения выходных координат. */
static gboolean insight_lua_debug_select_frame(void *engine, guint level, const char **filename, unsigned long *line)
{
    (void)engine;
    (void)filename;
    (void)line;
    assert(level == 1);
    sends++;
    return FALSE;
}

/* EN: Capture label's diagnostic text; no GTK object is required.
 * RU: Запоминает диагностический text метки label; объект GTK не нужен. */
static void gtk_label_set_text(void *label, const char *text)
{
    (void)label;
    message = text;
}

/* EN: Reject unexpected control-state changes after a failed request; widget/value are unused.
 * RU: Отклоняет неожиданные изменения элементов после отказа запроса; widget/value не используются. */
static void gtk_widget_set_sensitive(void *widget, gboolean value)
{
    (void)widget;
    (void)value;
    assert(FALSE);
}

/* EN: Reject unexpected reply reads after failed dispatch for view.
 * RU: Отклоняет неожиданное чтение ответа после отказа отправки view. */
static void debug_python_read_reply(ScriptDebugWindow *view)
{
    (void)view;
    assert(FALSE);
}

/* EN: Reject unexpected source navigation for view/filename after failed selection.
 * RU: Отклоняет неожиданный переход view/filename после отказа выбора кадра. */
static void debug_show_script_file(ScriptDebugWindow *view, const char *filename)
{
    (void)view;
    (void)filename;
    assert(FALSE);
}

/* EN: Reject unexpected result replacement for view/result/line/error on a rejected frame selection.
 * RU: Отклоняет неожиданную замену результата view/result/line/error при отказе выбора кадра. */
static void debug_show_result(ScriptDebugWindow *view, int result, unsigned long line, const char *error)
{
    (void)view;
    (void)result;
    (void)line;
    (void)error;
    assert(FALSE);
}
'''
MAIN = r'''
/* EN: Verify Lua and Python rejection restore the accepted frame once and retain the paused context.
 * RU: Проверяет однократный возврат принятого кадра и сохранение паузы при отказах Lua и Python. */
int main(void)
{
    ScriptDebugWindow view = {0};
    active = &view;
    view.paused_line = 7;
    for (guint python = 0; python < 2; python++)
    {
        view.python = python;
        selection = "1";
        debug_stack_frame_changed(NULL, &view);
        assert(sends == python + 1 && restores == python + 1);
        assert(!strcmp(selection, "0") && view.inspection_frame == 0 && view.paused_line == 7);
        assert(!view.inspecting_frame && !view.updating_stack_frames && message && *message);
    }
    view.updating_stack_frames = TRUE;
    selection = "1";
    debug_stack_frame_changed(NULL, &view);
    assert(sends == 2 && restores == 2);
    return 0;
}
'''


class ScriptFrameSelection(unittest.TestCase):
    # EN: Compile production helpers for self and verify rejected selection plus programmatic recursion guards.
    # RU: Компилирует настоящие обработчики для self и проверяет отказы выбора и защиту программных изменений.
    def test_rejected_frame_restores_accepted_selection(self):
        library = ctypes.util.find_library('glib-2.0')
        if not shutil.which('cc') or not library:
            self.skipTest('Needs cc and GLib runtime.')
        helpers = function('src/insight-debuggers/scripts/execution-results.inc', 'debug_restore_frame_selection')
        helpers += function('src/insight-debuggers/scripts/python-transport.inc', 'debug_stack_frame_changed')
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'frames.c'
            source.write_text(STATE + helpers + MAIN)
            binary = Path(directory) / 'frames'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary),
                            '-l:' + library], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
