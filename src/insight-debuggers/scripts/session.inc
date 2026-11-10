/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Script debugger state and snapshots.
 * RU: Состояние отладчика скриптов и снимки. */

typedef struct {
    gchar *filename;
    gchar *expression;
    unsigned long line;
    gchar *saved_group;
    guint minimum_hits;
    gboolean log_only;
    gboolean disabled;
} DebugConditionalPoint;

typedef struct {
    InsightLuaDebug *engine;
    InsightPythonDebug *python_engine;
    InsightLuaWatchGraph *python_graph;
    gchar *python_stack;
    GHashTable *python_overlays;
    GtkWidget *python_console;
    GtkWidget *python_input_row;
    GtkWidget *python_input_entry;
    gsize python_console_bytes;
    guint64 python_request;
    gboolean python_diagnostic_seen, python_diagnostic_jumped;
    gboolean python;
    gboolean python_running;
    gboolean closing;
    gchar *project_root;
    gchar *display_filename;
    gchar *display_source;
    gsize display_length;
    GHashTable *file_breakpoints;
    GHashTable *conditional_points;
    GHashTable *disabled_plain_points;
    guint unresolved_breakpoints;
    gchar *breakpoint_settings_path;
    GHashTable *lua_modules;
    gsize lua_module_bytes;
    GtkWidget *file_breakpoint_button;
    GtkWidget *observer_log;
    InsightLuaDebug *python_observer;
    InsightLuaWatchGraph *observer_graph;
    gchar *stop_condition;
    gchar *observer_source;
    gchar *observer_filename;
    GtkWidget *window;
    GtkWidget *source_view;
    GtkWidget *stack_view;
    GtkWidget *stack_frames;
    guint inspection_frame;
    gboolean updating_stack_frames;
    gboolean inspecting_frame;
    GtkWidget *status;
    GtkWidget *breakpoints;
    GtkWidget *step_controls;
    GtkWidget *go_to_source;
    GtkWidget *stop;
    GtkWidget *pause;
    GtkWidget *watch_canvas;
    GtkWidget *watch_status;
    GtkWidget *watch_pin;
    GtkWidget *watch_only;
    const InsightLuaWatchGraph *watch_graph;
    GPtrArray *watch_paths;
    GHashTable *watch_previous_values;
    gboolean watch_changed[INSIGHT_LUA_WATCH_NODES];
    guint watch_change_count;
    gint watch_selected;
    gint watch_column[INSIGHT_LUA_WATCH_NODES];
    gint watch_row[INSIGHT_LUA_WATCH_NODES];
    guint execution_source;
    InsightLuaStep pending_mode;
    unsigned long pending_breakpoints[128];
    InsightLuaBreakpoint pending_lua_breakpoints[128];
    gsize pending_breakpoint_count;
    GtkTextTag *current_line_tag;
    GtkTextTag *breakpoint_tag;
    GtkTextTag *conditional_point_tag;
    gchar *source_snapshot;
    gchar *source_code_mask;
    GArray *source_line_offsets;
    gchar *source_filename;
    gchar *selection_snapshot;
    gchar *project_filename;
    gchar *line_buffer;
    GArray *line_starts;
    gsize source_length;
    unsigned long position;
    unsigned long selection_start;
    unsigned long selection_end;
    guint document_id;
    unsigned long paused_line;
} ScriptDebugWindow;

/* Release the VM and the source snapshot. */
/* Освобождает виртуальную машину и снимок исходника. */
static void debug_window_free(gpointer data)
{
    ScriptDebugWindow *view = data;
    if (view->execution_source)
        g_source_remove(view->execution_source);
    insight_lua_debug_free(view->engine);
    insight_python_debug_free(view->python_engine);
    insight_lua_debug_free(view->python_observer);
    g_free(view->observer_graph);
    g_free(view->python_graph);
    g_free(view->python_stack);
    g_clear_pointer(&view->python_overlays, g_hash_table_unref);
    g_free(view->project_root);
    g_free(view->display_filename);
    g_free(view->display_source);
    g_hash_table_unref(view->file_breakpoints);
    g_hash_table_unref(view->conditional_points);
    g_hash_table_unref(view->disabled_plain_points);
    g_free(view->breakpoint_settings_path);
    g_free(view->stop_condition);
    g_free(view->observer_source);
    g_free(view->observer_filename);
    g_hash_table_unref(view->lua_modules);
    g_free(view->source_snapshot);
    g_free(view->source_code_mask);
    g_array_unref(view->source_line_offsets);
    g_free(view->source_filename);
    g_free(view->selection_snapshot);
    g_free(view->project_filename);
    g_free(view->line_buffer);
    g_array_unref(view->line_starts);
    g_ptr_array_unref(view->watch_paths);
    g_clear_pointer(&view->watch_previous_values, g_hash_table_unref);
    g_free(view);
}

/* Display debugger output even if a Lua string contains invalid UTF-8. */
/* Показывает вывод отладчика, даже если строка Lua содержит некорректный UTF-8. */
static void debug_set_text(GtkWidget *text_view, const gchar *text)
{
    gchar *valid = g_utf8_make_valid(text, -1);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_buffer_set_text(buffer, valid, -1);
    g_free(valid);
}

#include "../editor-controls.inc"

/* Parse up to 128 one-based breakpoint lines from the entry. */
/* Разбирает до 128 номеров строк точек останова, начиная с единицы. */
static gboolean debug_parse_breakpoints(const gchar *text, unsigned long *lines, gsize *count)
{
    gchar **words = g_strsplit_set(text, ",; \t", -1);
    gboolean valid = TRUE;
    *count = 0;
    for (guint i = 0; words[i] && valid; i++)
    {
        if (!*words[i])
            continue;
        guint64 line = 0;
        valid = *count < 128 && g_ascii_string_to_unsigned(words[i], 10, 1, G_MAXINT, &line, NULL);
        if (valid)
            lines[(*count)++] = (unsigned long)line;
    }
    g_strfreev(words);
    return valid;
}

/* Return the exact text supplied to the debug VM. */
/* Возвращает точный текст, переданный виртуальной машине отладчика. */
static const char *debug_snapshot_text(void *context)
{
    ScriptDebugWindow *view = context;
    return view->source_snapshot;
}

/* Return the selection captured before execution. */
/* Возвращает выделение, сохранённое перед выполнением. */
static const char *debug_snapshot_selection(void *context)
{
    ScriptDebugWindow *view = context;
    return view->selection_snapshot;
}

/* Return the original buffer name, which may be absent. */
/* Возвращает имя исходного буфера, если оно было задано. */
static const char *debug_snapshot_filename(void *context)
{
    ScriptDebugWindow *view = context;
    return view->source_filename;
}

/* Return the original editor caret as a byte offset. */
/* Возвращает исходную позицию курсора редактора в байтах. */
static unsigned long debug_snapshot_position(void *context)
{
    ScriptDebugWindow *view = context;
    return view->position;
}

/* Return the byte range of the original selection. */
/* Возвращает диапазон исходного выделения в байтах. */
static int debug_snapshot_selection_range(void *context, unsigned long *start, unsigned long *end)
{
    ScriptDebugWindow *view = context;
    *start = view->selection_start;
    *end = view->selection_end;
    return 1;
}

/* Return the number of lines in the captured buffer. */
/* Возвращает число строк сохранённого буфера. */
static unsigned long debug_snapshot_line_count(void *context)
{
    ScriptDebugWindow *view = context;
    return view->line_starts->len;
}

/* Return a captured line with its original line ending. */
/* Возвращает строку снимка с исходным переводом строки. */
static const char *debug_snapshot_line_text(void *context, unsigned long line)
{
    ScriptDebugWindow *view = context;
    if (line == 0 || line > view->line_starts->len)
        return NULL;

    gsize start = g_array_index(view->line_starts, gsize, line - 1);
    gsize end = line < view->line_starts->len ?
        g_array_index(view->line_starts, gsize, line) : view->source_length;
    g_free(view->line_buffer);
    view->line_buffer = g_strndup(view->source_snapshot + start, end - start);
    return view->line_buffer;
}

static const InsightEditorAPI debug_snapshot_api = {
    .position = debug_snapshot_position,
    .filename = debug_snapshot_filename,
    .selection_range = debug_snapshot_selection_range,
    .line_count = debug_snapshot_line_count,
    .line_text = debug_snapshot_line_text
};

#define WATCH_CARD_WIDTH 188
#define WATCH_CARD_HEIGHT 54
#define WATCH_COLUMN_WIDTH 255
#define WATCH_ROW_HEIGHT 76
#define WATCH_MARGIN 16
