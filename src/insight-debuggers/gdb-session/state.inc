/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Declare shared native debugger state and cross-module entry points.
 * RU: Объявляет общее состояние отладчика и точки входа между модулями. */

typedef enum {
    NATIVE_RUN, NATIVE_CONTINUE, NATIVE_INTO, NATIVE_OVER, NATIVE_OUT,
    NATIVE_PAUSE, NATIVE_STACK, NATIVE_LOCALS, NATIVE_STOP, NATIVE_RESTART, NATIVE_UNTIL, NATIVE_ACTION_COUNT
} NativeDebugAction;

typedef struct {
    gchar *expression, *value;
} NativeWatch;

typedef struct
{
    gchar *location, *condition, *number, *filter;
    guint catch_kind;
    GPtrArray *disabled_locations;
    gboolean enabled;
    guint ignore;
} NativeSavedPoint;

typedef enum
{
    NATIVE_CONTEXT_NONE, NATIVE_CONTEXT_THREADS, NATIVE_CONTEXT_STACK,
    NATIVE_CONTEXT_SELECT_THREAD, NATIVE_CONTEXT_SELECT_FRAME, NATIVE_CONTEXT_ALL_STACK
} NativeContextRequest;

typedef enum
{
    NATIVE_DATA_INSPECT, NATIVE_DATA_WATCH, NATIVE_DATA_ASSIGN,
    NATIVE_DATA_WRITE, NATIVE_DATA_READ, NATIVE_DATA_ACCESS, NATIVE_DATA_MEMORY
} NativeDataAction;

typedef enum
{
    NATIVE_VALUE_NONE, NATIVE_VALUE_CREATE, NATIVE_VALUE_CHILDREN,
    NATIVE_VALUE_ASSIGN, NATIVE_VALUE_DELETE, NATIVE_VALUE_PATH
} NativeValueRequest;

typedef struct {
    NativeRemote *remote;
    GSubprocess *process;
    GDataInputStream *output;
    GCancellable *cancel;
    GtkWidget *status, *command, *location, *breakpoint, *load_lua, *unload_lua, *source, *buttons[NATIVE_ACTION_COUNT];
    GtkTextView *console, *watches;
    NativeAssembly *assembly;
    GtkWidget *inspection, *observer_log, *tabs, *watch_entry, *watch_add, *watch_remove;
    GPtrArray *expressions, *locals, *saved_points, *profile_disabled_pending;
    guint profile_insert_next, profile_insert_count, profile_insert_index;
    GKeyFile *profile_restore_data;
    gboolean profile_insert_pending;
    gboolean profile_restoring, profile_restore_failed;
    GtkListStore *local_rows, *point_rows, *stack_rows;
    GtkWidget *threads, *stack;
    GtkWidget *window;
    GPtrArray *paused_controls;
    GtkWidget *memory_expression, *memory_size, *memory_auto, *memory_text, *memory_status;
    gchar *memory_address;
    guint memory_generation, memory_pending_generation, memory_count;
    gboolean memory_needed, memory_pending;
    GtkTreeStore *value_rows;
    GtkWidget *value_tree, *value_expression, *value_status, *locals_view;
    gchar *value_root, *value_pending_name, *value_pending_expression, *stop_detail, *data_command;
    GtkTreeRowReference *value_pending_row;
    GQueue value_cleanup;
    NativeValueRequest value_request;
    guint value_generation, value_pending_generation, value_serial, value_offset, value_nodes;
    NativeDataAction value_path_action;
    gboolean value_reload, data_refresh_pending;
    GtkWidget *launch_args, *launch_directory, *launch_status, *point_controls, *points_view;
    GQueue launch_commands;
    gboolean stop_requested, kill_pending, points_needed, points_pending, console_pending;
    GPtrArray *point_markers, *stop_markers;
    NativeContextRequest context_pending;
    GtkTreeStore *all_stack_rows;
    GtkWidget *all_stacks_page, *all_stacks_view, *all_stacks_status;
    GPtrArray *thread_ids;
    GHashTable *thread_roots;
    guint all_stack_next;
    gboolean all_stacks_visible, all_stacks_needed, frame_select_needed;
    gchar *all_stack_thread, *requested_frame, *selected_thread, *selected_frame;
    gboolean threads_needed, stack_needed, filling_context;
    gboolean locals_needed, locals_pending;
    ScriptDebugWindow *graph_view;
    InsightLuaWatchGraph *graph;
    guint watch_next;
    NativeWatch *watch_pending;
    gboolean inspecting;
    InsightLuaDebug *observer;
    gsize log_offset;
    gchar *stop_file;
    guint stop_line;
    gboolean resume;
    GHashTable *editor_points;
    gchar *editor_point_pending;
    gboolean closed, ready, running, stopped, pending, writing;
} NativeDebug;

static void native_assembly_prepare(NativeDebug *session, gchar *request, const gchar *expression);
static void native_assembly_follow(NativeDebug *session);
static void native_assembly_context(NativeDebug *session);
static void native_assembly_console(NativeDebug *session, const gchar *line);
static void native_assembly_free(NativeAssembly *assembly);
static gboolean native_assembly_next(NativeDebug *session);
static void native_profile_save(NativeDebug *session);
static gchar *native_name_line_addresses(NativeDebug *session, const gchar *text);
static gchar *native_name_addresses(NativeDebug *session, const gchar *text);
static gboolean native_assembly_reply(NativeDebug *session, const gchar *record);

static void native_profile_points_snapshot(NativeDebug *session, const gchar *record);
static void native_profile_debug_reply(NativeDebug *session, const gchar *record);
static void native_remote_drop(NativeDebug *session);
static void native_remote_update(NativeDebug *session);
static void native_remote_restore(NativeDebug *session);
static gboolean native_remote_selected(NativeDebug *session);
static gboolean native_remote_launch(NativeDebug *session);
static gchar *native_remote_next(NativeDebug *session);
static gboolean native_remote_reply(NativeDebug *session, const gchar *record);
static gchar *native_launch_arguments(const gchar *text, GError **error);
static void native_debug_resume(GtkWidget *dialog, NativeDebug *session);
static void native_memory_sync(NativeDebug *session);
static gchar *native_memory_next(NativeDebug *session);
static gboolean native_memory_reply(NativeDebug *session, const gchar *record);
static void native_memory_inspect(NativeDebug *session, const gchar *expression);
static void native_target_clear(NativeDebug *session);
static void native_values_reset(NativeDebug *session);
static gchar *native_values_next(NativeDebug *session, gboolean cleanup);
static gboolean native_values_reply(NativeDebug *session, const gchar *record);
static void native_data_refresh(NativeDebug *session);
static void native_data_use_expression(NativeDebug *session, const gchar *expression, NativeDataAction action);
static void native_assign_dialog(NativeDebug *session, const gchar *expression, const gchar *value,
                                 const gchar *object);
static void native_run_to_cursor(GtkWidget *dialog);
static gchar *native_stop_detail(const gchar *record);
static gchar *native_launch_next(NativeDebug *session);
static void native_source_stopped(NativeDebug *session);
static gchar *native_point_key(const gchar *filename, guint line);
static gboolean native_attach_interrupt(NativeDebug *session, const gchar *command);
static void native_attach_stopped(NativeDebug *session, const gchar *record);
static gboolean native_attach_interrupt_reply(NativeDebug *session, const gchar *line);
static void native_profile_command_sent(NativeDebug *session, const gchar *command);
static void native_debug_send(GtkWidget *dialog, const gchar *command);
static void native_all_stacks_reply(NativeDebug *session, const gchar *record);
static gboolean native_context_id(const gchar *value);

static void native_instruction_refresh(NativeDebug *session);
static void native_instruction_point(NativeDebug *session, const gchar *tuple, const gchar *parent);

static void native_instruction_points_reset(NativeDebug *session);
static void native_instruction_source_reply(NativeDebug *session, NativeAssemblyWork *work, const gchar *record);
