#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Check production Stop/Restart scheduling and GDB failure recovery without ptrace or a display.
# RU: Проверяет производственную очередь Stop/Restart и восстановление после ошибок GDB без ptrace и дисплея.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import DECLARATIONS, function


STATE = r'''typedef void GtkWidget;
typedef struct {void *head, *tail; guint length;} GQueue;
typedef struct {
    gboolean pending, writing, ready, running, stopped, stop_requested, kill_pending;
    GQueue launch_commands;
    GtkWidget *launch_status;
} NativeDebug;
#define TRUE 1
#define GTK_LABEL(value) (value)
#define G_OBJECT(value) (value)
extern void g_queue_push_tail(GQueue *, void *);
extern void *g_queue_pop_head(GQueue *);
static NativeDebug *active;
static gboolean launch_valid = TRUE;
static guint prepare_count, send_count, resume_count, clear_count;
static char *sent_command, *error_message;

/* EN: Return the active fixture session; object/key stand for the absent GTK dialog association.
 * RU: Возвращает активный сеанс фикстуры; object/key обозначают связь с отсутствующим диалогом GTK. */
static void *g_object_get_data(void *object, const char *key)
{
    (void)object;
    (void)key;
    return active;
}

/* EN: Drain queue with destroy for each item, preserving production queue ownership semantics.
 * RU: Очищает queue, вызывая destroy для каждого элемента, сохраняя производственные правила владения очередью. */
static void insight_queue_clear_full(GQueue *queue, void (*destroy)(void *))
{
    void *item;
    while ((item = g_queue_pop_head(queue)))
        destroy(item);
}

/* EN: Count validation for dialog and return launch_valid; the launch-form implementation is tested separately.
 * RU: Считает проверку dialog и возвращает launch_valid; реализация формы запуска проверяется отдельно. */
static gboolean native_launch_prepare(GtkWidget *dialog)
{
    (void)dialog;
    prepare_count++;
    return launch_valid;
}

/* EN: Capture command for dialog without starting GDB; retain only the latest command.
 * RU: Запоминает command для dialog без запуска GDB; сохраняет только последнюю команду. */
static void native_debug_send(GtkWidget *dialog, const char *command)
{
    (void)dialog;
    g_free(sent_command);
    sent_command = g_strdup(command);
    send_count++;
}

/* EN: Count a scheduler wakeup for dialog/session without consuming commands prematurely.
 * RU: Считает пробуждение очереди dialog/session без преждевременного извлечения команд. */
static void native_debug_resume(GtkWidget *dialog, NativeDebug *session)
{
    (void)dialog;
    (void)session;
    resume_count++;
}

/* EN: Model successful target teardown for session; detailed widget cleanup is outside this scheduler fixture.
 * RU: Моделирует успешное завершение цели session; очистка виджетов находится вне этой фикстуры очереди. */
static void native_target_clear(NativeDebug *session)
{
    session->stopped = session->running = FALSE;
    clear_count++;
}

/* EN: Capture text assigned to label so error replies can be verified without rendering GTK.
 * RU: Запоминает text для label, позволяя проверить ошибки без отрисовки GTK. */
static void gtk_label_set_text(GtkWidget *label, const char *text)
{
    (void)label;
    g_free(error_message);
    error_message = g_strdup(text);
}
'''

HARNESS = r'''
/* EN: Verify scenario from argv[1] on production lifecycle functions; argc must be two.
 * RU: Проверяет сценарий argv[1] на производственных функциях жизненного цикла; argc должен равняться двум. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    NativeDebug session = {0};
    active = &session;
    session.ready = TRUE;
    if (!strcmp(argv[1], "running"))
    {
        session.running = TRUE;
        native_lifecycle_request(NULL, TRUE);
        assert(prepare_count == 1 && send_count == 1 && !resume_count);
        assert(!strcmp(sent_command, "-exec-interrupt --all") && session.stop_requested);
        native_lifecycle_reply(&session, "^done");
        assert(session.running && !clear_count && session.stop_requested);
        session.running = FALSE;
        session.stopped = TRUE;
        char *command = native_launch_next(&session);
        assert(!strcmp(command, "-interpreter-exec console \"kill\"") && session.kill_pending);
        g_free(command);
    }
    else if (!strcmp(argv[1], "paused"))
    {
        session.stopped = TRUE;
        native_lifecycle_request(NULL, FALSE);
        assert(!prepare_count && !send_count && resume_count == 1 && session.stop_requested);
        char *command = native_launch_next(&session);
        assert(!strcmp(command, "-interpreter-exec console \"kill\""));
        g_free(command);
        native_lifecycle_reply(&session, "^done");
        assert(clear_count == 1 && !session.kill_pending && !session.stop_requested && !session.stopped);
    }
    else if (!strcmp(argv[1], "restart") || !strcmp(argv[1], "kill-error"))
    {
        const char *launch[] = {"-environment-cd \"/project\"", "-exec-arguments \"a b\"", "-exec-run"};
        for (guint index = 0; index < G_N_ELEMENTS(launch); index++)
            g_queue_push_tail(&session.launch_commands, g_strdup(launch[index]));
        session.stopped = session.stop_requested = TRUE;
        char *command = native_launch_next(&session);
        assert(!strcmp(command, "-interpreter-exec console \"kill\""));
        g_free(command);
        gboolean failure = !strcmp(argv[1], "kill-error");
        native_lifecycle_reply(&session, failure ? "^error,msg=\"Cannot kill target\"" : "^done");
        assert(!session.kill_pending && !session.stop_requested);
        if (failure)
        {
            assert(!clear_count && session.stopped && !native_launch_next(&session));
            assert(!strcmp(error_message, "Cannot kill target"));
        }
        else
        {
            assert(clear_count == 1 && !session.stopped);
            for (guint index = 0; index < G_N_ELEMENTS(launch); index++)
            {
                command = native_launch_next(&session);
                assert(command && !strcmp(command, launch[index]));
                g_free(command);
                native_lifecycle_reply(&session, index == 2 ? "^running" : "^done");
            }
            assert(!native_launch_next(&session));
        }
    }
    else if (!strcmp(argv[1], "launch-error"))
    {
        g_queue_push_tail(&session.launch_commands, g_strdup("-exec-run"));
        native_lifecycle_reply(&session, "^error,msg=\"Bad working directory\"");
        assert(!native_launch_next(&session) && !clear_count);
        assert(!strcmp(error_message, "Bad working directory"));
    }
    else if (!strcmp(argv[1], "invalid-form"))
    {
        session.running = TRUE;
        launch_valid = FALSE;
        native_lifecycle_request(NULL, TRUE);
        assert(prepare_count == 1 && !send_count && !resume_count && !session.stop_requested);
        assert(session.running);
    }
    else if (!strcmp(argv[1], "busy"))
    {
        gboolean *flags[] = {&session.ready, &session.pending, &session.writing};
        session.running = TRUE;
        for (guint index = 0; index < G_N_ELEMENTS(flags); index++)
        {
            gboolean original = *flags[index];
            *flags[index] = !original;
            native_lifecycle_request(NULL, TRUE);
            assert(!prepare_count && !send_count && !resume_count && !session.stop_requested);
            *flags[index] = original;
        }
    }
    else
        assert(FALSE);
    insight_queue_clear_full(&session.launch_commands, g_free);
    g_free(sent_command);
    g_free(error_message);
    return 0;
}
'''


class NativeLifecycle(unittest.TestCase):
    # EN: Compile production scheduling for self and run scenario; skip missing compiler or GLib runtime.
    # RU: Компилирует производственную очередь для self и запускает scenario; пропускает при отсутствии компилятора либо GLib.
    def exercise(self, scenario):
        library = ctypes.util.find_library('glib-2.0')
        if not shutil.which('cc') or not library:
            self.skipTest('Needs cc and GLib runtime.')
        declarations = DECLARATIONS[:DECLARATIONS.index('typedef struct {\n    GtkTreeStore *all_stack_rows;')]
        declarations += STATE
        helpers = [function('src/insight-debuggers/gdb-context.inc', 'native_debug_field')]
        helpers += [function('src/insight-debuggers/gdb-lifecycle.inc', name)
                    for name in ('native_lifecycle_request', 'native_launch_next', 'native_lifecycle_reply')]
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / 'lifecycle.c'
            source.write_text(declarations + '\n'.join(helpers) + HARNESS)
            executable = folder / 'lifecycle'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(source), '-o', str(executable), '-l:' + library], check=True)
            subprocess.run([str(executable), scenario], check=True)

    # EN: Verify self's running restart interrupts the target before scheduling kill.
    # RU: Проверяет, что перезапуск работающей цели self прерывает её перед командой завершения.
    def test_running_restart(self):
        self.exercise('running')

    # EN: Verify self's paused Stop goes directly to kill and clears state only after acknowledgement.
    # RU: Проверяет, что Stop на паузе self сразу завершает цель и очищает состояние лишь после подтверждения.
    def test_paused_stop(self):
        self.exercise('paused')

    # EN: Verify self's restart preserves directory/arguments/run order after acknowledged kill.
    # RU: Проверяет порядок каталога/аргументов/запуска self после подтверждённого завершения.
    def test_restart_order(self):
        self.exercise('restart')

    # EN: Verify self's rejected kill abandons restart commands and retains the stopped target.
    # RU: Проверяет отмену очереди перезапуска self и сохранение остановленной цели при отказе завершения.
    def test_kill_error(self):
        self.exercise('kill-error')

    # EN: Verify self's failed launch setting prevents subsequent execution and exposes GDB's explanation.
    # RU: Проверяет отмену последующего запуска self и показ пояснения GDB при ошибке настройки.
    def test_launch_error(self):
        self.exercise('launch-error')

    # EN: Verify self's invalid restart form leaves the currently running program untouched.
    # RU: Проверяет сохранение работающей программы self при неверной форме перезапуска.
    def test_invalid_restart_form(self):
        self.exercise('invalid-form')

    # EN: Verify self's lifecycle actions wait for command/write completion and debugger readiness.
    # RU: Проверяет ожидание завершения команды/записи и готовности отладчика перед действиями self.
    def test_busy_session(self):
        self.exercise('busy')


if __name__ == '__main__':
    unittest.main()
