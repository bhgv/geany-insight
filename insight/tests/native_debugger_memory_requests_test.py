#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Regress GDB memory-reply lifetime and navigation anchors with real GObject ownership, without a display.
# RU: Проверяет срок жизни ответов памяти GDB и адреса навигации с настоящим владением GObject без дисплея.
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import function


STATE = r'''/* EN: Use real GLib/GTK types and nonvisual text controls for production memory callbacks.
 * RU: Использует настоящие типы GLib/GTK и невизуальные текстовые элементы для обработчиков памяти. */
#include <gtk/gtk.h>
#include <assert.h>
#include <string.h>
#define insight_tr(text) (text)
typedef struct {
    gboolean memory_pending, stopped, ready, pending, writing, memory_needed;
    guint memory_pending_generation, value_generation, memory_count;
    gchar *memory_address;
    GtkWidget *memory_text, *memory_status, *memory_size, *memory_expression, *window;
} NativeDebug;
static gchar *displayed, *status, *requested_address;
static guint resume_count;

/* EN: Return view as a nonvisual buffer token; no GTK text widget is constructed.
 * RU: Возвращает view как невизуальный идентификатор буфера; текстовый виджет GTK не создаётся. */
static gpointer fixture_buffer(gpointer view)
{
    return view;
}

/* EN: Store text for buffer; length must request the complete string.
 * RU: Сохраняет text для buffer; length должен запрашивать полную строку. */
static void fixture_text(gpointer buffer, const gchar *text, gint length)
{
    (void)buffer;
    assert(length == -1);
    g_free(displayed);
    displayed = g_strdup(text);
}

/* EN: Store text for label without rendering a widget.
 * RU: Сохраняет text для label без отрисовки виджета. */
static void fixture_label(gpointer label, const gchar *text)
{
    (void)label;
    g_free(status);
    status = g_strdup(text);
}

/* EN: Return the fixture's three-byte read size for button.
 * RU: Возвращает размер чтения три байта для button фикстуры. */
static gint fixture_size(gpointer button)
{
    (void)button;
    return 3;
}

/* EN: Record text assigned to entry as the next requested address.
 * RU: Запоминает text поля entry как следующий запрошенный адрес. */
static void fixture_entry(gpointer entry, const gchar *text)
{
    (void)entry;
    g_free(requested_address);
    requested_address = g_strdup(text);
}

/* EN: Return owned text for session without address annotations; NULL formats as an empty snapshot.
 * RU: Возвращает текст для session без аннотаций адресов; NULL даёт пустой снимок. */
static gchar *native_name_line_addresses(NativeDebug *session, const gchar *text)
{
    (void)session;
    return g_strdup(text ? text : "");
}

/* EN: Count a read-scheduler wakeup for dialog/session without running a debugger.
 * RU: Считает пробуждение очереди чтения dialog/session без запуска отладчика. */
static void native_debug_resume(GtkWidget *dialog, NativeDebug *session)
{
    (void)dialog;
    (void)session;
    resume_count++;
}
#undef GTK_TEXT_VIEW
#undef GTK_LABEL
#undef GTK_SPIN_BUTTON
#undef GTK_ENTRY
#define GTK_TEXT_VIEW(value) (value)
#define GTK_LABEL(value) (value)
#define GTK_SPIN_BUTTON(value) (value)
#define GTK_ENTRY(value) (value)
#define gtk_text_view_get_buffer fixture_buffer
#define gtk_text_buffer_set_text fixture_text
#define gtk_label_set_text fixture_label
#define gtk_spin_button_get_value_as_int fixture_size
#define gtk_entry_set_text fixture_entry
'''

HARNESS = r'''
/* EN: Run argv[1]'s memory scenario; argc must be two and GObject owns the raw snapshot string.
 * RU: Запускает сценарий памяти argv[1]; argc равен двум, GObject владеет исходной строкой снимка. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    NativeDebug session = {0};
    session.stopped = session.ready = session.memory_pending = TRUE;
    session.memory_pending_generation = session.value_generation = 4;
    session.memory_count = 3;
    session.memory_address = g_strdup("0x2000");
    GObject *view = g_object_new(G_TYPE_OBJECT, NULL);
    session.memory_text = (GtkWidget *)view;
    g_object_set_data_full(view, "native-memory-raw", g_strdup("previous raw"), g_free);
    displayed = g_strdup("previous display");
    status = g_strdup("previous status");
    const gchar *record = "^done,memory=[{begin=\"0x1000\",offset=\"0x0\",contents=\"414243\"}]";
    gboolean unchanged = FALSE;
    if (!strcmp(argv[1], "stale"))
    {
        session.value_generation++;
        unchanged = TRUE;
    }
    else if (!strcmp(argv[1], "running"))
    {
        session.stopped = FALSE;
        unchanged = TRUE;
    }
    else if (!strcmp(argv[1], "unrequested"))
    {
        session.memory_pending = FALSE;
        unchanged = TRUE;
    }
    else if (!strcmp(argv[1], "error"))
        record = "^error,msg=\"Cannot access memory\"";
    else if (!strcmp(argv[1], "incomplete"))
        record = "^done,memory=[{begin=\"0x1000\",offset=\"0x0\",contents=\"41\"}]";
    else if (!strcmp(argv[1], "truncated"))
        record = "^done,memory=[{begin=\"0x1000\",offset=\"0x0\",contents=\"414243\"}";
    else if (!strcmp(argv[1], "fragmented"))
        record = "^done,memory=[{begin=\"0x1002\",offset=\"0x2\",contents=\"43\"},"
            "{begin=\"0x1000\",offset=\"0x0\",contents=\"4142\"}]";
    gboolean consumed = native_memory_reply(&session, record);
    assert(consumed == (strcmp(argv[1], "unrequested") != 0));
    assert(!session.memory_pending);
    const gchar *raw = g_object_get_data(view, "native-memory-raw");
    if (unchanged)
    {
        assert(!strcmp(displayed, "previous display") && !strcmp(raw, "previous raw"));
        assert(!strcmp(status, "previous status") && !strcmp(session.memory_address, "0x2000"));
    }
    else if (!strcmp(argv[1], "error") || !strcmp(argv[1], "incomplete") || !strcmp(argv[1], "truncated"))
    {
        assert(!*displayed && !raw && !session.memory_address);
        assert(strstr(status, !strcmp(argv[1], "error") ? "Cannot access memory" : "incomplete"));
    }
    else
    {
        guint64 address = 0;
        assert(native_memory_address(session.memory_address, &address) && address == 0x1000);
        assert(raw && strstr(raw, " | ABC\n") && !strcmp(displayed, raw));
        GObject *button = g_object_new(G_TYPE_OBJECT, NULL);
        g_object_set_data(button, "memory-direction", GINT_TO_POINTER(1));
        native_memory_read((GtkButton *)button, &session);
        assert(session.memory_needed && resume_count == 1 && !strcmp(requested_address, "0x1003"));
        session.memory_needed = FALSE;
        g_object_set_data(button, "memory-direction", GINT_TO_POINTER(-1));
        native_memory_read((GtkButton *)button, &session);
        assert(session.memory_needed && resume_count == 2 && !strcmp(requested_address, "0xffd"));
        g_object_unref(button);
    }
    g_free(session.memory_address);
    g_free(displayed);
    g_free(status);
    g_free(requested_address);
    g_object_unref(view);
    return 0;
}
'''


class NativeMemoryRequests(unittest.TestCase):
    # EN: Compile production parsers/reply/navigation for self and run scenario; skip absent GTK build dependencies.
    # RU: Компилирует разбор/ответы/навигацию для self и запускает scenario; пропускает при отсутствии зависимостей GTK.
    def exercise(self, scenario):
        if not shutil.which('cc') or not shutil.which('pkg-config'):
            self.skipTest('Needs cc, pkg-config and GTK development headers.')
        flags = subprocess.run(['pkg-config', '--cflags', '--libs', 'gtk+-3.0'],
                               capture_output=True, text=True)
        if flags.returncode:
            self.skipTest('Needs GTK development headers.')
        helpers = [function('src/insight-debuggers/gdb-context.inc', name)
                   for name in ('native_debug_field', 'native_next_tuple')]
        helpers += [function('src/insight-debuggers/gdb-memory-model.inc', name)
                    for name in ('native_memory_address', 'native_memory_format')]
        helpers += [function('src/insight-debuggers/gdb-memory-ui.inc', name)
                    for name in ('native_memory_reply', 'native_memory_read')]
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / 'memory-requests.c'
            source.write_text(STATE + '\n'.join(helpers) + HARNESS)
            executable = folder / 'memory-requests'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(executable),
                            *shlex.split(flags.stdout)], check=True)
            subprocess.run([str(executable), scenario], check=True)

    # EN: Verify self's current snapshot supplies the address for exact next/previous block navigation.
    # RU: Проверяет адрес текущего снимка self для точной навигации по следующим/предыдущим блокам.
    def test_current_snapshot(self):
        self.exercise('current')

    # EN: Verify self's reversed MI fragments anchor navigation at the complete snapshot's base.
    # RU: Проверяет привязку навигации self к началу полного снимка при обратном порядке фрагментов MI.
    def test_fragmented_snapshot(self):
        self.exercise('fragmented')

    # EN: Verify self's old memory reply preserves the newer snapshot and its address.
    # RU: Проверяет сохранение нового снимка self и его адреса при позднем ответе памяти.
    def test_stale_reply(self):
        self.exercise('stale')

    # EN: Verify self's running target cannot publish a memory reply from the previous stop.
    # RU: Проверяет запрет публикации памяти предыдущей остановки работающей цели self.
    def test_running_reply(self):
        self.exercise('running')

    # EN: Verify self's memory handler leaves unrelated replies untouched.
    # RU: Проверяет отсутствие изменений self при ответе без ожидающего запроса памяти.
    def test_unrequested_reply(self):
        self.exercise('unrequested')

    # EN: Verify self's current memory errors clear bytes and address while preserving GDB's diagnostic.
    # RU: Проверяет очистку байтов/адреса self и сохранение диагностики GDB при текущей ошибке памяти.
    def test_current_error(self):
        self.exercise('error')

    # EN: Verify self's incomplete snapshot cannot become the base for later navigation.
    # RU: Проверяет запрет использования неполного снимка self как базы дальнейшей навигации.
    def test_incomplete_snapshot(self):
        self.exercise('incomplete')

    # EN: Verify self's unterminated MI list clears complete-looking bytes and their navigation anchor.
    # RU: Проверяет очистку кажущихся полными байтов self и адреса навигации при незакрытом списке MI.
    def test_truncated_snapshot(self):
        self.exercise('truncated')


if __name__ == '__main__':
    unittest.main()
