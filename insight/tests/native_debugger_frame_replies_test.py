#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Regress late GDB frame replies with production parsers and real GTK models, without ptrace or a display.
# RU: Проверяет поздние ответы кадров GDB производственными обработчиками и моделями GTK без ptrace и дисплея.
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import ROOT, function


SEND_STATE = r'''/* EN: Exercise real asynchronous GIO writes to a local echo process without GTK widgets.
 * RU: Проверяет настоящую асинхронную запись GIO в локальный эхо-процесс без виджетов GTK. */
#include <gtk/gtk.h>
#include <assert.h>
#include <string.h>
enum {NATIVE_REMOTE_SYSROOT, NATIVE_REMOTE_CONNECT};
typedef struct {int pending;} NativeRemote;
typedef struct {
    gboolean closed, pending, writing;
    guint value_generation, frame_pending_generation;
    NativeRemote *remote;
    GSubprocess *process;
    GCancellable *cancel;
} NativeDebug;
static NativeDebug *active;
static guint update_count;
static gboolean interrupted, completed, timed_out;
static GMainLoop *loop;

/* EN: Model an SSH-owned interrupt for session/command when interrupted is set; otherwise use GIO normally.
 * RU: Моделирует собственное SSH-прерывание session/command при interrupted; иначе использует обычную запись GIO. */
static gboolean native_attach_interrupt(NativeDebug *session, const gchar *command)
{
    (void)session;
    (void)command;
    return interrupted;
}

/* EN: Ignore profile tracking of session/command; persistence is covered by the profile tests.
 * RU: Пропускает учёт session/command в профиле; сохранение проверяется тестами профиля.
 */
static void native_profile_command_sent(NativeDebug *session, const gchar *command)
{
    (void)session;
    (void)command;
}

/* EN: Return command unchanged for session; command scoping is covered by the model tests.
 * RU: Возвращает неизменённую command для session; контекст команд проверяется тестами моделей. */
static gchar *native_debug_scoped(NativeDebug *session, const gchar *command)
{
    (void)session;
    return g_strdup(command);
}

/* EN: Count session's control update without constructing widgets.
 * RU: Считает обновления элементов session без создания виджетов. */
static void native_debug_update(NativeDebug *session)
{
    (void)session;
    update_count++;
}

/* EN: Complete the actual GIO write from stream/result, release data's retained object and stop the fixture loop.
 * RU: Завершает настоящую запись GIO из stream/result, освобождает удержанный объект data и останавливает цикл фикстуры. */
static void native_debug_written(GObject *stream, GAsyncResult *result, gpointer data)
{
    GError *error = NULL;
    assert(g_output_stream_write_all_finish(G_OUTPUT_STREAM(stream), result, NULL, &error));
    assert(!error);
    active->writing = FALSE;
    completed = TRUE;
    g_object_unref(data);
    g_main_loop_quit(loop);
}

/* EN: Stop the fixture loop on timeout; data is unused and the source removes itself.
 * RU: Останавливает цикл фикстуры по тайм-ауту; data не используется, источник удаляется. */
static gboolean write_timeout(gpointer data)
{
    (void)data;
    timed_out = TRUE;
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}
'''

SEND_HARNESS = r'''
/* EN: Verify dispatch guards or real GIO command generations using cat from argv[1] and scenario argv[2]; argc is three.
 * RU: Проверяет ограничения отправки либо поколения настоящих команд GIO с cat из argv[1] и сценарием argv[2]; argc равен трём. */
int main(int argc, char **argv)
{
    assert(argc == 3);
    NativeDebug session = {0};
    active = &session;
    session.value_generation = 7;
    session.frame_pending_generation = 99;
    GObject *dialog = g_object_new(G_TYPE_OBJECT, NULL);
    g_object_set_data(dialog, "native-debug", &session);
    if (!strcmp(argv[2], "guards"))
    {
        gboolean *flags[] = {&session.closed, &session.pending, &session.writing};
        for (guint index = 0; index < G_N_ELEMENTS(flags); index++)
        {
            *flags[index] = TRUE;
            native_debug_send((GtkWidget *)dialog, "-stack-list-variables --all-values");
            assert(session.frame_pending_generation == 99 && !update_count);
            *flags[index] = FALSE;
        }
        interrupted = TRUE;
        native_debug_send((GtkWidget *)dialog, "-exec-interrupt");
        assert(session.frame_pending_generation == 99 && !update_count && !session.pending);
    }
    else
    {
        GError *error = NULL;
        session.cancel = g_cancellable_new();
        session.process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE,
            &error, argv[1], NULL);
        assert(session.process && !error);
        GDataInputStream *output = g_data_input_stream_new(g_subprocess_get_stdout_pipe(session.process));
        loop = g_main_loop_new(NULL, FALSE);
        for (guint index = 0; index < 2; index++)
        {
            completed = FALSE;
            session.pending = FALSE;
            guint generation = session.value_generation;
            native_debug_send((GtkWidget *)dialog, "-stack-list-variables --all-values");
            assert(session.frame_pending_generation == generation && session.pending && session.writing);
            session.value_generation++;
            guint timeout = g_timeout_add_seconds(3, write_timeout, NULL);
            g_main_loop_run(loop);
            assert(completed && !timed_out && !session.writing);
            g_source_remove(timeout);
            assert(session.frame_pending_generation == generation);
            gchar *line = g_data_input_stream_read_line(output, NULL, NULL, &error);
            assert(line && !error && !strcmp(line, "-stack-list-variables --all-values"));
            g_free(line);
        }
        assert(update_count == 2);
        g_subprocess_force_exit(session.process);
        assert(g_subprocess_wait(session.process, NULL, &error) && !error);
        g_object_unref(output);
        g_object_unref(session.process);
        g_object_unref(session.cancel);
        g_main_loop_unref(loop);
    }
    g_object_unref(dialog);
    return 0;
}
'''


STATE = r'''/* EN: Host production reply helpers in a nonvisual GTK model fixture.
 * RU: Размещает производственные обработчики ответов в невизуальной фикстуре модели GTK. */
#include <gtk/gtk.h>
#include <assert.h>
#include <string.h>
#define insight_tr(text) (text)
typedef struct {gchar *expression, *value;} NativeWatch;
typedef enum {
    NATIVE_CONTEXT_NONE, NATIVE_CONTEXT_THREADS, NATIVE_CONTEXT_STACK,
    NATIVE_CONTEXT_SELECT_THREAD, NATIVE_CONTEXT_SELECT_FRAME, NATIVE_CONTEXT_ALL_STACK
} NativeContextRequest;
typedef struct {
    NativeContextRequest context_pending;
    gboolean stopped, locals_pending, locals_needed, threads_needed, stack_needed;
    guint value_generation, frame_pending_generation, watch_next;
    GPtrArray *locals, *expressions;
    GtkListStore *local_rows;
    NativeWatch *watch_pending;
} NativeDebug;
static guint context_count, graph_count;

/* EN: Free the NativeWatch in data and its owned strings; NULL is not supplied by the fixture.
 * RU: Освобождает NativeWatch из data и его строки; фикстура не передаёт NULL. */
static void watch_free(gpointer data)
{
    NativeWatch *watch = data;
    g_free(watch->expression);
    g_free(watch->value);
    g_free(watch);
}

/* EN: Return an owned copy of text for session; address annotation is outside this reply fixture.
 * RU: Возвращает копию text для session; аннотации адресов находятся вне этой фикстуры ответов. */
static gchar *native_name_addresses(NativeDebug *session, const gchar *text)
{
    (void)session;
    return g_strdup(text);
}

/* EN: Count a confirmed context reply for session/record and consume its request; no widgets are constructed.
 * RU: Считает подтверждённый ответ контекста session/record и завершает запрос; виджеты не создаются. */
static void native_context_reply(NativeDebug *session, const gchar *record)
{
    (void)record;
    context_count++;
    session->context_pending = NATIVE_CONTEXT_NONE;
}

/* EN: Count graph publication for session; compare must identify a completed stopped snapshot.
 * RU: Считает публикации графа session; compare должен обозначать завершённый снимок остановки. */
static void native_watch_refresh(NativeDebug *session, gboolean compare)
{
    (void)session;
    assert(compare);
    graph_count++;
}
'''

HARNESS = r'''
/* EN: Assert that session's sole local has name/value; abort if a late reply replaces the confirmed snapshot.
 * RU: Проверяет name/value единственной переменной session; прерывает тест при замене снимка поздним ответом. */
static void check_local(NativeDebug *session, const gchar *name, const gchar *value)
{
    assert(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(session->local_rows), NULL) == 1);
    GtkTreeIter row;
    assert(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(session->local_rows), &row));
    gchar *actual_name = NULL, *actual_value = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(session->local_rows), &row,
        0, &actual_name, 1, &actual_value, -1);
    assert(!g_strcmp0(actual_name, name) && !g_strcmp0(actual_value, value));
    g_free(actual_name);
    g_free(actual_value);
}

/* EN: Run the frame-reply scenario in argv[1]; argc must be two and no inferior is launched.
 * RU: Запускает сценарий ответа кадра из argv[1]; argc должен быть равен двум, отлаживаемый процесс не запускается. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    NativeDebug session = {0};
    session.stopped = TRUE;
    session.value_generation = session.frame_pending_generation = 7;
    session.locals = g_ptr_array_new_with_free_func(watch_free);
    session.expressions = g_ptr_array_new_with_free_func(watch_free);
    session.local_rows = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    gtk_list_store_insert_with_values(session.local_rows, NULL, -1,
        0, "fresh", 1, "99", 2, "Local variable", -1);
    NativeWatch *watch = g_new0(NativeWatch, 1);
    watch->expression = g_strdup("value");
    watch->value = g_strdup("fresh");
    g_ptr_array_add(session.expressions, watch);
    gboolean late = g_str_has_prefix(argv[1], "late-");
    const gchar *record = NULL;
    if (late)
    {
        session.value_generation++;
        session.locals_needed = session.threads_needed = session.stack_needed = TRUE;
        if (!strcmp(argv[1], "late-locals") || !strcmp(argv[1], "late-exit"))
        {
            session.locals_pending = TRUE;
            record = "^done,variables=[{name=\"old\",value=\"1\"}]";
            if (!strcmp(argv[1], "late-exit"))
            {
                session.stopped = FALSE;
                gtk_list_store_clear(session.local_rows);
            }
        }
        else if (!strcmp(argv[1], "late-context"))
        {
            session.context_pending = NATIVE_CONTEXT_STACK;
            record = "^done,stack=[frame={level=\"0\",func=\"old\"}]";
        }
        else
        {
            session.watch_pending = watch;
            session.watch_next = 1;
            record = !strcmp(argv[1], "late-error") ?
                "^error,msg=\"Old frame disappeared\"" : "^done,value=\"old\"";
            if (!strcmp(argv[1], "late-resumed"))
            {
                session.stopped = FALSE;
                session.frame_pending_generation = session.value_generation;
            }
        }
        native_context_data_reply(&session, record);
        assert(!session.locals_pending && !session.watch_pending);
        assert(session.context_pending == NATIVE_CONTEXT_NONE && !context_count && !graph_count);
        assert(!strcmp(watch->value, "fresh"));
        assert(session.locals_needed && session.threads_needed && session.stack_needed);
        if (!strcmp(argv[1], "late-exit"))
            assert(!gtk_tree_model_iter_n_children(GTK_TREE_MODEL(session.local_rows), NULL));
        else
            check_local(&session, "fresh", "99");
    }
    else if (!strcmp(argv[1], "locals"))
    {
        g_ptr_array_set_size(session.expressions, 0);
        session.locals_pending = TRUE;
        native_context_data_reply(&session,
            "^done,variables=[{name=\"argument\",arg=\"1\",value=\"42\"}]");
        assert(!session.locals_pending && graph_count == 1 && session.locals->len == 1);
        check_local(&session, "argument", "42");
    }
    else if (!strcmp(argv[1], "context"))
    {
        session.context_pending = NATIVE_CONTEXT_STACK;
        native_context_data_reply(&session, "^done,stack=[]");
        assert(context_count == 1 && session.context_pending == NATIVE_CONTEXT_NONE);
        assert(!graph_count);
        check_local(&session, "fresh", "99");
    }
    else
    {
        session.watch_pending = watch;
        session.watch_next = 1;
        record = !strcmp(argv[1], "watch-error") ? "^error,msg=\"Not in scope\"" :
            !strcmp(argv[1], "watch-empty") ? "^done" : "^done,value=\"42\"";
        native_context_data_reply(&session, record);
        const gchar *expected = !strcmp(argv[1], "watch-error") ? "Not in scope" :
            !strcmp(argv[1], "watch-empty") ? "Unavailable" : "42";
        assert(!strcmp(watch->value, expected) && !session.watch_pending && graph_count == 1);
        check_local(&session, "fresh", "99");
    }
    g_ptr_array_unref(session.locals);
    g_ptr_array_unref(session.expressions);
    g_object_unref(session.local_rows);
    return 0;
}
'''


class NativeFrameReplies(unittest.TestCase):
    # EN: Compile production handlers for self with GTK flags and run scenario; skip only missing compiler/headers.
    # RU: Компилирует производственные обработчики self с параметрами GTK и запускает scenario; пропускает лишь без средств сборки.
    def exercise(self, scenario):
        if not shutil.which('cc') or not shutil.which('pkg-config'):
            self.skipTest('Needs cc, pkg-config and GTK development headers.')
        result = subprocess.run(['pkg-config', '--cflags', '--libs', 'gtk+-3.0'],
                                capture_output=True, text=True)
        if result.returncode:
            self.skipTest('Needs GTK development headers.')
        helpers = [function('src/insight-debuggers/gdb-context.inc', name)
                   for name in ('native_debug_field', 'native_debug_locals', 'native_context_data_reply')]
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / 'frame-replies.c'
            source.write_text(STATE + '\n'.join(helpers) + HARNESS)
            executable = folder / 'frame-replies'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(source),
                            '-o', str(executable), *shlex.split(result.stdout)], check=True)
            subprocess.run([str(executable), scenario], check=True)

    # EN: Verify self's old locals reply preserves a newer stopped snapshot.
    # RU: Проверяет сохранение нового снимка остановки self при позднем ответе переменных.
    def test_late_locals(self):
        self.exercise('late-locals')

    # EN: Verify self's old watch result preserves its newer value.
    # RU: Проверяет сохранение нового значения наблюдения self при позднем результате.
    def test_late_watch(self):
        self.exercise('late-watch')

    # EN: Verify self's old watch error cannot replace a newer value with an obsolete diagnostic.
    # RU: Проверяет, что старая ошибка наблюдения self не заменяет новое значение устаревшей диагностикой.
    def test_late_watch_error(self):
        self.exercise('late-error')

    # EN: Verify self's old stack result cannot select or publish a frame from an earlier stop.
    # RU: Проверяет, что старый ответ стека self не выбирает и не публикует кадр прежней остановки.
    def test_late_context(self):
        self.exercise('late-context')

    # EN: Verify self's running target rejects frame data even if the generation happens to match.
    # RU: Проверяет отбрасывание данных кадра работающей цели self даже при совпадении поколения.
    def test_reply_while_running(self):
        self.exercise('late-resumed')

    # EN: Verify self's exited target stays empty after a late successful locals reply.
    # RU: Проверяет, что завершённая цель self остаётся пустой после позднего успешного ответа переменных.
    def test_reply_after_exit(self):
        self.exercise('late-exit')

    # EN: Verify self's current locals/arguments still populate the real model and publish its graph.
    # RU: Проверяет заполнение настоящей модели и публикацию графа текущих переменных/аргументов self.
    def test_current_locals(self):
        self.exercise('locals')

    # EN: Verify self's current stack request still reaches its context handler.
    # RU: Проверяет передачу текущего запроса стека self обработчику контекста.
    def test_current_context(self):
        self.exercise('context')

    # EN: Verify self's current watch replaces its value and publishes the completed snapshot.
    # RU: Проверяет замену текущего значения наблюдения self и публикацию завершённого снимка.
    def test_current_watch(self):
        self.exercise('watch')

    # EN: Verify self's current watch errors remain visible rather than being mistaken for stale replies.
    # RU: Проверяет видимость текущих ошибок наблюдения self, не принимаемых за устаревшие ответы.
    def test_current_watch_error(self):
        self.exercise('watch-error')

    # EN: Verify self's current watch with no result gets the explicit unavailable fallback.
    # RU: Проверяет явную подпись недоступности текущего наблюдения self без результата.
    def test_current_watch_without_value(self):
        self.exercise('watch-empty')

    # EN: Compile actual dispatch for self and verify scenario against a local cat process, with an eight-second limit.
    # RU: Компилирует настоящую отправку для self и проверяет scenario с локальным cat с пределом восемь секунд.
    def exercise_dispatch(self, scenario):
        cat = shutil.which('cat')
        if not cat or not shutil.which('cc') or not shutil.which('pkg-config'):
            self.skipTest('Needs cc, cat, pkg-config and GTK development headers.')
        result = subprocess.run(['pkg-config', '--cflags', '--libs', 'gtk+-3.0'],
                                capture_output=True, text=True)
        if result.returncode:
            self.skipTest('Needs GTK development headers.')
        production = (ROOT / 'src/insight-debuggers/gdb-session.inc').read_text()
        position = production.rindex('\nstatic void native_debug_send(') + 1
        helper = production[position:production.index('\n}', position) + 2]
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / 'dispatch.c'
            source.write_text(SEND_STATE + helper + SEND_HARNESS)
            executable = folder / 'dispatch'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(source),
                            '-o', str(executable), *shlex.split(result.stdout)], check=True)
            subprocess.run([str(executable), cat, scenario], check=True, timeout=8)

    # EN: Verify self's real asynchronous writes capture the sending generation and never overwrite it on completion.
    # RU: Проверяет фиксацию поколения при настоящей асинхронной записи self и его сохранение при завершении.
    def test_dispatch_generation(self):
        self.exercise_dispatch('write')

    # EN: Verify self's ignored busy/closed/SSH-owned requests cannot overwrite the outstanding generation.
    # RU: Проверяет сохранение поколения self при игнорировании занятых/закрытых запросов и собственных SSH-прерываний.
    def test_dispatch_guards(self):
        self.exercise_dispatch('guards')


if __name__ == '__main__':
    unittest.main()
