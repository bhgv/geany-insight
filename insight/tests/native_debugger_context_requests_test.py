#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Regress confirmed GDB thread/frame selection and failed picker rollback with real GTK models.
# RU: Проверяет подтверждение потока/кадра GDB и откат выбора при ошибках с настоящими моделями GTK.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import DECLARATIONS, function


STATE = r'''typedef void GtkWidget;
typedef void *gpointer;
typedef void GtkTreeSelection;
typedef void GtkTreeViewColumn;
typedef unsigned long long guint64;
typedef struct {void **pdata; guint len;} GPtrArray;
typedef enum {
    NATIVE_CONTEXT_NONE, NATIVE_CONTEXT_THREADS, NATIVE_CONTEXT_STACK,
    NATIVE_CONTEXT_SELECT_THREAD, NATIVE_CONTEXT_SELECT_FRAME, NATIVE_CONTEXT_ALL_STACK
} NativeContextRequest;
typedef struct {
    GtkTreeStore *stack_rows;
    GtkTreeView *stack;
    GtkWidget *threads;
    char *requested_frame, *selected_frame, *selected_thread, *stop_file;
    guint stop_line, watch_next;
    gboolean frame_select_needed, filling_context, stack_needed, locals_needed, stop_requested;
    gboolean closed, ready, stopped, pending, writing;
    NativeContextRequest context_pending;
    GPtrArray *stop_markers;
} NativeDebug;
#define TRUE 1
#define GTK_COMBO_BOX(value) (value)
#define G_OBJECT(value) (value)
#define G_MAXINT 2147483647
#define g_clear_pointer(pointer, destroy) do {destroy(*(pointer)); *(pointer) = NULL;} while (0)
#define g_steal_pointer(pointer) __extension__ ({void *value = *(pointer); *(pointer) = NULL; value;})
extern int gtk_tree_model_get_iter_first(GtkTreeModel *, GtkTreeIter *);
extern int gtk_tree_model_iter_next(GtkTreeModel *, GtkTreeIter *);
extern int g_ascii_string_to_unsigned(const char *, unsigned, guint64, guint64, guint64 *, void *);
extern GPtrArray *g_ptr_array_new(void);
extern void g_ptr_array_add(GPtrArray *, void *);
extern void g_ptr_array_set_size(GPtrArray *, guint);
extern void g_ptr_array_unref(GPtrArray *);
static NativeDebug *active;
static char *picked_frame, *picked_thread;
static guint reset_count, source_count, fill_count, all_stacks_count, sent_count;
extern GtkTreePath *gtk_tree_path_new_from_indices(int, ...);
extern int gtk_tree_model_get_iter(GtkTreeModel *, GtkTreeIter *, GtkTreePath *);
/* EN: Return the nonvisual dialog's session; object/key identify the normal GObject association.
 * RU: Возвращает сеанс невизуального окна; object/key обозначают обычную связь GObject. */
static void *g_object_get_data(void *object, const char *key)
{
    (void)object;
    (void)key;
    return active;
}
/* EN: Return view's real GTK model without constructing its display-dependent tree view.
 * RU: Возвращает настоящую модель GTK для view без создания виджета дерева, требующего дисплея. */
static GtkTreeModel *gtk_tree_view_get_model(GtkTreeView *view)
{
    (void)view;
    return active->stack_rows;
}
/* EN: Count command dispatch for dialog; require an explicit frame-selection request.
 * RU: Считает отправку command для dialog; требует явный запрос выбора кадра. */
static void native_debug_send(GtkWidget *dialog, const char *command)
{
    (void)dialog;
    assert(!strcmp(command, "-stack-select-frame 1"));
    sent_count++;
}


/* EN: Return the fixture selection for view; display-dependent GTK widgets are not constructed.
 * RU: Возвращает выбор фикстуры view; виджеты GTK, требующие дисплея, не создаются. */
static GtkTreeSelection *gtk_tree_view_get_selection(GtkTreeView *view)
{
    (void)view;
    return active;
}

/* EN: Resolve selection's picked frame to a real model row; set model/row on success and return FALSE if absent.
 * RU: Находит выбранный кадр selection в настоящей модели; задаёт model/row при успехе, иначе возвращает FALSE. */
static gboolean gtk_tree_selection_get_selected(GtkTreeSelection *selection,
    GtkTreeModel **model, GtkTreeIter *row)
{
    (void)selection;
    *model = active->stack_rows;
    for (gboolean valid = gtk_tree_model_get_iter_first(*model, row); valid;
         valid = gtk_tree_model_iter_next(*model, row))
    {
        char *level = NULL;
        gtk_tree_model_get(*model, row, 0, &level, -1);
        gboolean matches = picked_frame && !g_strcmp0(level, picked_frame);
        g_free(level);
        if (matches)
            return TRUE;
    }
    return FALSE;
}

/* EN: Clear selection's fixture frame only while GDB-command callbacks are suppressed.
 * RU: Снимает кадр фикстуры selection только при подавленных обработчиках команд GDB. */
static void gtk_tree_selection_unselect_all(GtkTreeSelection *selection)
{
    (void)selection;
    assert(active->filling_context);
    g_clear_pointer(&picked_frame, g_free);
}

/* EN: Select row in selection's real model; require suppression of recursive GDB requests.
 * RU: Выбирает row в настоящей модели selection; требует подавления рекурсивных запросов GDB. */
static void gtk_tree_selection_select_iter(GtkTreeSelection *selection, GtkTreeIter *row)
{
    (void)selection;
    assert(active->filling_context);
    g_free(picked_frame);
    gtk_tree_model_get(active->stack_rows, row, 0, &picked_frame, -1);
}

/* EN: Set combo's fixture thread to id if present; return FALSE without changing it when the thread disappeared.
 * RU: Устанавливает поток id фикстуры combo, если он есть; при исчезнувшем потоке возвращает FALSE без изменения выбора. */
static gboolean gtk_combo_box_set_active_id(GtkWidget *combo, const char *id)
{
    (void)combo;
    assert(active->filling_context);
    if (g_strcmp0(id, "2") && g_strcmp0(id, "3"))
        return FALSE;
    g_free(picked_thread);
    picked_thread = g_strdup(id);
    return TRUE;
}

/* EN: Clear combo's fixture selection for index -1; other indices are not used by rollback.
 * RU: Снимает выбор фикстуры combo при index -1; другие индексы при откате не используются. */
static void gtk_combo_box_set_active(GtkWidget *combo, int index)
{
    (void)combo;
    assert(index == -1 && active->filling_context);
    g_clear_pointer(&picked_thread, g_free);
}

/* EN: Return combo's borrowed fixture selection without modifying it.
 * RU: Возвращает заимствованный выбор фикстуры combo без изменения. */
static const char *gtk_combo_box_get_active_id(GtkWidget *combo)
{
    (void)combo;
    return picked_thread;
}

/* EN: Count session's requested model fill from record; threads identifies the picker but no UI is rendered.
 * RU: Считает запрос заполнения модели session из record; threads определяет список, интерфейс не рисуется. */
static void native_fill_context(NativeDebug *session, const char *record, gboolean threads)
{
    (void)session;
    (void)record;
    (void)threads;
    fill_count++;
}

/* EN: Count invalidation of session's value context after a confirmed selection.
 * RU: Считает сброс контекста значений session после подтверждённого выбора. */
static void native_values_reset(NativeDebug *session)
{
    (void)session;
    reset_count++;
}

/* EN: Copy filename for the fixture; no locale conversion is needed for its ASCII paths.
 * RU: Копирует filename фикстуры; её ASCII-пути не требуют перекодировки локали. */
static char *utils_get_locale_from_utf8(const char *filename)
{
    return g_strdup(filename);
}

/* EN: Count a source opening for filename/line without constructing the editor.
 * RU: Считает открытие исходника filename/line без создания редактора. */
static void diff_source_open(const char *filename, guint line)
{
    assert(filename && line);
    source_count++;
}

/* EN: Record a valid stopped-line marker in owned; filename/line/type/color describe the requested marker.
 * RU: Запоминает допустимый маркер остановки в owned; filename/line/type/color описывают запрошенный маркер. */
static void native_marker_add(GPtrArray *owned, const char *filename, guint line, int type, int color)
{
    assert(filename && line && type == 19 && color == 0x80dfff);
    g_ptr_array_add(owned, (void *)1);
}

/* EN: Count delegation of record to session's all-thread stack handler.
 * RU: Считает передачу record обработчику всех стеков session. */
static void native_all_stacks_reply(NativeDebug *session, const char *record)
{
    (void)session;
    (void)record;
    all_stacks_count++;
}
'''

HARNESS = r'''
/* EN: Exercise the context-result scenario in argv[1]; argc must be two and assertions describe the visible selection.
 * RU: Проверяет сценарий ответа контекста из argv[1]; argc должен равняться двум, проверки описывают видимый выбор. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    NativeDebug session = {.ready = TRUE, .stopped = TRUE};
    active = &session;
    session.stop_markers = g_ptr_array_new();
    g_ptr_array_add(session.stop_markers, (void *)1);
    session.stack_rows = gtk_tree_store_new(5, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_store_insert_with_values(session.stack_rows, NULL, NULL, -1,
        0, "0", 3, "11", 4, "/frame0.cpp", -1);
    gtk_tree_store_insert_with_values(session.stack_rows, NULL, NULL, -1,
        0, "1", 3, "21", 4, "/frame1.cpp", -1);
    session.selected_thread = g_strdup("2");
    session.selected_frame = g_strdup("0");
    session.requested_frame = g_strdup("1");
    session.frame_select_needed = TRUE;
    session.stop_file = g_strdup("/frame0.cpp");
    session.stop_line = 11;
    session.watch_next = 9;
    picked_frame = g_strdup("1");
    picked_thread = g_strdup("3");
    gboolean thread = strstr(argv[1], "thread") != NULL;
    gboolean missing = strstr(argv[1], "missing") != NULL;
    gboolean no_source = strstr(argv[1], "nosource") != NULL;
    gboolean stack = strstr(argv[1], "stack") != NULL;
    gboolean success = strstr(argv[1], "success") != NULL || no_source || stack;
    if (no_source)
    {
        GtkTreeIter row;
        assert(gtk_tree_model_get_iter_first(session.stack_rows, &row));
        assert(gtk_tree_model_iter_next(session.stack_rows, &row));
        gtk_tree_store_set(session.stack_rows, &row, 3, NULL, 4, NULL, -1);
    }
    if (missing)
    {
        char **confirmed = thread ? &session.selected_thread : &session.selected_frame;
        g_free(*confirmed);
        *confirmed = g_strdup("99");
    }
    if (strstr(argv[1], "navigation"))
    {
        session.pending = strstr(argv[1], "busy") != NULL;
        GtkTreePath *path = gtk_tree_path_new_from_indices(1, -1);
        native_open_frame((GtkTreeView *)session.stack_rows, path, NULL, (GtkWidget *)&session);
        gtk_tree_path_free(path);
        assert(!source_count && !reset_count && !strcmp(session.selected_frame, "0"));
        if (session.pending)
        {
            assert(!sent_count);
            goto cleanup;
        }
        assert(sent_count == 1 && session.context_pending == NATIVE_CONTEXT_SELECT_FRAME);
    }
    session.context_pending = stack ? NATIVE_CONTEXT_STACK :
        thread ? NATIVE_CONTEXT_SELECT_THREAD : NATIVE_CONTEXT_SELECT_FRAME;
    native_context_reply(&session, success ? "^done" : "^error,msg=\"No such context\"");
    if (stack)
    {
        assert(!strcmp(session.selected_frame, "0"));
        assert(!strcmp(session.stop_file, "/frame0.cpp") && session.stop_line == 11);
        assert(!reset_count && !source_count && !session.locals_needed && session.watch_next == 9);
        assert(session.frame_select_needed);
        session.frame_select_needed = FALSE;
        session.context_pending = NATIVE_CONTEXT_SELECT_FRAME;
        success = strstr(argv[1], "rejected") == NULL;
        native_context_reply(&session, success ? "^done" : "^error,msg=\"No such frame\"");
    }
    assert(session.context_pending == NATIVE_CONTEXT_NONE && !session.filling_context);
    if (!success)
    {
        assert(!session.requested_frame && !session.frame_select_needed);
        assert(!reset_count && !source_count && !session.locals_needed && session.watch_next == 9);
        assert(session.stop_markers->len == 1);
        assert(!strcmp(session.stop_file, "/frame0.cpp") && session.stop_line == 11);
        if (thread)
            assert(missing ? !picked_thread : !g_strcmp0(picked_thread, "2"));
        else
            assert(missing ? !picked_frame : !g_strcmp0(picked_frame, "0"));
    }
    else
    {
        assert(reset_count == 1 && session.locals_needed && session.watch_next == 0);
        if (thread)
        {
            assert(!strcmp(session.selected_thread, "3") && !session.selected_frame);
            assert(!session.stop_file && !session.stop_line && session.stack_needed);
            assert(!session.stop_markers->len && !source_count);
        }
        else
        {
            assert(!strcmp(session.selected_frame, "1"));
            if (!stack)
                assert(!session.requested_frame);
            if (no_source)
            {
                assert(!session.stop_file && !session.stop_line && !source_count);
                assert(!session.stop_markers->len);
            }
            else
            {
                assert(!strcmp(session.stop_file, "/frame1.cpp") && session.stop_line == 21);
                assert(source_count == 1 && session.stop_markers->len == 1);
            }
        }
    }
    assert(fill_count == (stack ? 1u : 0u) && !all_stacks_count);
cleanup:
    g_free(session.selected_thread);
    g_free(session.selected_frame);
    g_free(session.requested_frame);
    g_free(session.stop_file);
    g_free(picked_frame);
    g_free(picked_thread);
    g_ptr_array_unref(session.stop_markers);
    g_object_unref(session.stack_rows);
    return 0;
}
'''


class NativeContextRequests(unittest.TestCase):
    # EN: Compile production selection/reply helpers for self and run scenario with real GTK model storage.
    # RU: Компилирует обработчики выбора/ответов для self и запускает scenario с настоящим хранилищем модели GTK.
    def exercise(self, scenario):
        libraries = [ctypes.util.find_library(name) for name in ('gtk-3', 'gobject-2.0', 'glib-2.0')]
        if not shutil.which('cc') or not all(libraries):
            self.skipTest('Needs cc and GTK runtime libraries.')
        declarations = DECLARATIONS[:DECLARATIONS.index('typedef struct {\n    GtkTreeStore *all_stack_rows;')]
        declarations += STATE
        helpers = [function('src/insight-debuggers/gdb-source-markers.inc', 'native_source_stopped')]
        helpers += [function('src/insight-debuggers/gdb-context.inc', name)
                   for name in ('native_selected_frame', 'native_context_restore', 'native_context_reply', 'native_context_id')]
        helpers += [function('src/insight-debuggers/gdb-context-navigation.inc', name)
                    for name in ('native_select_frame', 'native_open_frame')]
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / 'context.c'
            source.write_text(declarations + '\n'.join(helpers) + HARNESS)
            executable = folder / 'context'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(source), '-o', str(executable),
                            *['-l:' + name for name in libraries]], check=True)
            subprocess.run([str(executable), scenario], check=True)

    # EN: Verify self's rejected frame selection returns the highlight to the confirmed frame.
    # RU: Проверяет возврат выделения self к подтверждённому кадру после отказа выбора.
    def test_frame_failure(self):
        self.exercise('frame-failure')

    # EN: Verify self's rejected frame selection clears the highlight if the confirmed frame row disappeared.
    # RU: Проверяет снятие выделения self при отказе выбора, если строка подтверждённого кадра исчезла.
    def test_missing_frame(self):
        self.exercise('frame-missing')

    # EN: Verify self's rejected thread selection preserves its confirmed thread and value context.
    # RU: Проверяет сохранение подтверждённого потока и контекста значений self при отказе выбора.
    def test_thread_failure(self):
        self.exercise('thread-failure')

    # EN: Verify self's rejected thread selection clears a picker whose confirmed thread disappeared.
    # RU: Проверяет снятие выбора self при отказе, если подтверждённый поток исчез из списка.
    def test_missing_thread(self):
        self.exercise('thread-missing')

    # EN: Verify self's confirmed frame selection updates values, source coordinates and watch scheduling.
    # RU: Проверяет обновление значений, координат исходника и очереди наблюдений self после подтверждения кадра.
    def test_frame_success(self):
        self.exercise('frame-success')

    # EN: Verify self's confirmed thread selection clears the old frame and requests its new stack and values.
    # RU: Проверяет сброс старого кадра self и запрос нового стека/значений после подтверждения потока.
    def test_thread_success(self):
        self.exercise('thread-success')

    # EN: Verify self's confirmed source-less frame removes the previous source-line highlight.
    # RU: Проверяет удаление прежней подсветки исходника self после подтверждения кадра без исходника.
    def test_frame_without_source(self):
        self.exercise('frame-nosource')

    # EN: Verify self's refreshed source-less stack removes the previous source-line highlight too.
    # RU: Проверяет удаление прежней подсветки исходника self и при обновлении стека без исходника.
    def test_stack_without_source(self):
        self.exercise('stack-nosource')

    # EN: Reject a frame after its stack row was fetched; preserve the previous confirmed source and values.
    # RU: Отклоняет кадр после загрузки строки стека; сохраняет прежние подтверждённые исходник и значения.
    def test_stack_selection_rejected(self):
        self.exercise('stack-rejected')

    # EN: Commit a requested frame only after its separate GDB frame-selection result succeeds.
    # RU: Подтверждает запрошенный кадр только после отдельного успешного ответа выбора кадра GDB.
    def test_stack_selection_confirmed(self):
        self.exercise('stack-confirmed')

    # EN: Confirm source navigation only after GDB accepts the requested frame.
    # RU: Подтверждает переход к исходнику только после принятия кадра GDB.
    def test_navigation_success(self):
        self.exercise('navigation-success')

    # EN: Reject navigation without opening the failed frame's source or replacing its locals.
    # RU: Отклоняет переход без открытия исходника отвергнутого кадра или замены переменных.
    def test_navigation_failure(self):
        self.exercise('navigation-failure')

    # EN: Ignore a queued double click while a context command is already pending.
    # RU: Игнорирует двойной щелчок, пока команда контекста ожидает ответа.
    def test_navigation_busy(self):
        self.exercise('navigation-busy')


if __name__ == '__main__':
    unittest.main()
