#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Exercise saved memory reads,  draft isolation and pending metadata using production callbacks.
# RU: Проверяет сохранённые чтения памяти,  независимость черновиков и метаданные настоящими обработчиками.
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
#include <stddef.h>
typedef char gchar;
typedef unsigned int guint;
typedef int gint;
typedef int gboolean;
typedef long glong;
typedef long gssize;
typedef void GtkWidget;
#define G_TYPE_OBJECT 80UL
#define TRUE 1
#define FALSE 0
#define MIN(a, b) ((a)<(b)?(a):(b))
#define insight_tr(text) (text)
#define GTK_TEXT_VIEW(value) (value)
#define GTK_LABEL(value) (value)
#define GTK_SPIN_BUTTON(value) (value)
#define GTK_TOGGLE_BUTTON(value) (value)
#define G_OBJECT(value) (value)
#define g_clear_pointer(p,  destroy) do { destroy(*(p)); *(p)=NULL; } while (0)
extern void g_free(void *);
extern gchar *g_strdup(const gchar *);
extern gchar *g_strdup_printf(const gchar *,  ...);
extern gchar *g_strescape(const gchar *,  const gchar *);
extern gchar *g_strconcat(const gchar *,  ...);
extern gchar *g_utf8_make_valid(const gchar *,  gssize);
extern gchar *g_utf8_substring(const gchar *,  glong,  glong);
extern glong g_utf8_strlen(const gchar *,  gssize);
extern gboolean g_utf8_validate(const gchar *,  gssize,  const gchar **);
extern void *g_object_new(unsigned long,  const gchar *,  ...);
extern void g_object_unref(void *);
extern void g_object_set_data(void *,  const gchar *,  void *);
extern void g_object_set_data_full(void *,  const gchar *,  void *,  void (*)(void *));
extern void *g_object_get_data(void *,  const gchar *);
typedef struct {
    guint memory_generation,  value_generation,  memory_pending_generation,  memory_count,  memory_read_count;
    gboolean stopped,  memory_needed,  memory_pending;
    gchar *memory_address,  *memory_read_expression,  *memory_pending_expression;
    GtkWidget *memory_text,  *memory_status,  *memory_size,  *memory_auto;
} NativeDebug;
static gchar *displayed,  *status;
/* EN: Return the nonvisual buffer token represented by view.
 * RU: Возвращает невизуальный идентификатор буфера view. */
static void *gtk_text_view_get_buffer(void *view)
{
    return view;
}
/* EN: Capture all text assigned to buffer,  with length required to be -1.
 * RU: Сохраняет весь text буфера buffer; length должен быть -1. */
static void gtk_text_buffer_set_text(void *buffer,  const gchar *text,  gint length)
{
    (void)buffer;
    assert(length  ==  -1);
    g_free(displayed);
    displayed = g_strdup(text);
}
/* EN: Capture text for label without constructing a display-dependent widget.
 * RU: Сохраняет text для label без создания виджета,  требующего дисплея. */
static void gtk_label_set_text(void *label,  const gchar *text)
{
    (void)label;
    g_free(status);
    status = g_strdup(text);
}
/* EN: Ignore tooltip rendering for widget/text; snapshot data remains real GObject storage.
 * RU: Пропускает отрисовку подсказки widget/text; снимок хранится настоящим GObject. */
static void gtk_widget_set_tooltip_text(void *widget,  const gchar *text)
{
    (void)widget;
    (void)text;
}
/* EN: Read the plain boolean flag represented by toggle.
 * RU: Читает булев флажок,  представленный toggle. */
static gboolean gtk_toggle_button_get_active(void *toggle)
{
    return *(gboolean *)toggle;
}
'''

MAIN = r'''
/* EN: Validate argv[1]'s memory policy with real GLib allocation and GObject snapshot ownership.
 * RU: Проверяет политику памяти argv[1] с настоящими выделениями GLib и владением снимка GObject. */
int main(int argc,  char **argv)
{
    assert(argc  ==  2);
    if (!strcmp(argv[1],  "labels"))
    {
        gchar long_name[201];
        for (guint i = 0; i < 100; i++)
        {
            long_name[i * 2] = (gchar)0xd1;
            long_name[i * 2 + 1] = (gchar)0x8f;
        }
        long_name[200] = 0;
        gchar *label = native_memory_snapshot_label(long_name, 4096);
        assert(g_utf8_validate(label, -1, NULL) && strstr(label, "…") && strstr(label, "4096"));
        assert(g_utf8_strlen(label, -1)<140);
        g_free(label);
        label = native_memory_snapshot_label("\xff", 1);
        assert(g_utf8_validate(label, -1, NULL));
        g_free(label);
        label = native_memory_snapshot_label(NULL, 7);
        assert(strstr(label, "?") && strstr(label, "7"));
        g_free(label);
        return 0;
    }
    gboolean automatic = strcmp(argv[1], "off") && strcmp(argv[1], "manual");
    gint draft_size = 4;
    NativeDebug session = {.stopped = TRUE,  .value_generation = 1,  .memory_generation = 1};
    session.memory_text = g_object_new(G_TYPE_OBJECT,  NULL);
    session.memory_auto = &automatic;
    session.memory_size = &draft_size;
    if (!strcmp(argv[1], "first"))
    {
        session.value_generation++;
        native_memory_sync(&session);
        assert(!session.memory_needed && !native_memory_next(&session));
        goto done;
    }
    assert(native_memory_queue_read(&session, "&value", draft_size));
    gchar *command = native_memory_next(&session);
    assert(command && !strcmp(command, "-data-read-memory-bytes \"&value\" 4"));
    g_free(command);
    assert(session.memory_pending && session.memory_pending_generation == 1);
    assert(!strcmp(session.memory_pending_expression, "&value"));
    session.memory_pending = FALSE;
    session.memory_address = g_strdup("0x1000");
    g_object_set_data_full(session.memory_text, "native-memory-raw", g_strdup("old raw"), g_free);
    g_free(displayed);
    displayed = g_strdup("old display");
    draft_size = 128;
    if (!strcmp(argv[1], "invalid"))
    {
        draft_size = 4097;
        assert(!native_memory_queue_read(&session, "new", draft_size));
        draft_size = 4;
        assert(!native_memory_queue_read(&session, "", draft_size));
        assert(!strcmp(session.memory_read_expression, "&value") && session.memory_read_count == 4);
        assert(!session.memory_needed);
        goto done;
    }
    if (!strcmp(argv[1], "metadata"))
    {
        session.memory_pending = TRUE;
        draft_size = 8;
        assert(native_memory_queue_read(&session, "&shared_value", draft_size));
        assert(!native_memory_next(&session) && session.memory_needed);
        assert(session.memory_count == 4 && !strcmp(session.memory_pending_expression, "&value"));
        session.memory_pending = FALSE;
    }
    else if (!strcmp(argv[1], "manual"))
    {
        assert(native_memory_queue_read(&session, "&shared_value", draft_size));
    }
    else
    {
        if (!strcmp(argv[1], "unchanged"))
        {
            native_memory_sync(&session);
            assert(!session.memory_needed && !strcmp(displayed, "old display"));
            assert(g_object_get_data(session.memory_text, "native-memory-raw"));
            goto done;
        }
        if (!strcmp(argv[1],  "running"))
            session.stopped = FALSE;
        session.value_generation++;
        native_memory_sync(&session);
        assert(!session.memory_address && !*displayed);
        assert(!g_object_get_data(session.memory_text, "native-memory-raw"));
        if (!automatic || !session.stopped)
        {
            assert(!session.memory_needed && !native_memory_next(&session));
            goto done;
        }
        if (!strcmp(argv[1], "pending"))
        {
            session.memory_pending = TRUE;
            assert(!native_memory_next(&session) && session.memory_needed);
            assert(session.memory_pending_generation == 1);
            session.memory_pending = FALSE;
        }
    }
    command = native_memory_next(&session);
    assert(command);
    if (!strcmp(argv[1], "metadata"))
        assert(!strcmp(command, "-data-read-memory-bytes \"&shared_value\" 8"));
    else if (!strcmp(argv[1], "manual"))
        assert(!strcmp(command, "-data-read-memory-bytes \"&shared_value\" 128"));
    else
        assert(!strcmp(command, "-data-read-memory-bytes \"&value\" 4"));
    assert(!session.memory_address && !*displayed);
    assert(!g_object_get_data(session.memory_text, "native-memory-raw"));
    assert(session.memory_pending_generation == session.value_generation);
    g_free(command);
done:
    g_free(session.memory_read_expression);
    g_free(session.memory_pending_expression);
    g_free(session.memory_address);
    g_free(displayed);
    g_free(status);
    g_object_unref(session.memory_text);
    return 0;
}
'''


class NativeMemoryFollow(unittest.TestCase):
    # EN: Compile production queue/snapshot helpers and run scenario without a display or debugger.
    # RU: Компилирует настоящую очередь/снимки и выполняет scenario без дисплея и отладчика.
    def exercise(self, scenario):
        libraries = [ctypes.util.find_library(name) for name in ('gobject-2.0', 'glib-2.0')]
        if not shutil.which('cc') or not all(libraries):
            self.skipTest('Needs cc and GLib/GObject runtime libraries.')
        helpers = function('src/insight-debuggers/gdb-memory-model.inc', 'native_memory_snapshot_label')
        helpers += '\n'.join(function('src/insight-debuggers/gdb-memory-ui.inc', name)
                             for name in ('native_memory_clear_snapshot', 'native_memory_queue_read',
                                          'native_memory_sync', 'native_memory_next'))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / 'memory-follow.c'
            source.write_text(STATE + helpers + MAIN)
            executable = folder / 'memory-follow'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(executable),
                            *['-l:' + name for name in libraries]], check=True)
            subprocess.run([str(executable), scenario], check=True)

    # EN: Exercise opt-in,  explicit reads,  drafts,  pending replies,  metadata and UTF-8 labels.
    # RU: Проверяет флажок,  явные чтения,  черновики,  поздние ответы,  метаданные и подписи UTF-8.
    def test_saved_read_policy(self):
        for scenario in ('first', 'follow', 'off', 'running', 'unchanged', 'pending',
                         'manual', 'metadata', 'invalid', 'labels'):
            with self.subTest(scenario=scenario):
                self.exercise(scenario)


if __name__  ==  '__main__':
    unittest.main()
