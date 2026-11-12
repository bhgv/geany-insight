/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Own symbolic disassembly rules, resolved address ranges and bounded MI work.
 * RU: Хранит правила имён дизассемблера, разрешённые диапазоны адресов и ограниченную очередь MI. */
typedef struct
{
    guint kind; /* EN: 0 address, 1 frame offset, 2 typed data. RU: 0 адрес, 1 смещение кадра, 2 типизированные данные. */
    gchar *anchor, *name, *type, *scope;
} NativeNameRule;

typedef struct
{
    guint64 address, size;
    gint64 offset;
    gchar *name, *scope, *base;
    gboolean frame, manual;
} NativeNamedRange;

typedef struct
{
    gchar *command, *expression, *label, *scope, *object, *reply;
    guint action, depth;
    gboolean manual;
} NativeAssemblyWork;

typedef struct
{
    GtkWidget *expression, *status, *text, *rules_view, *page, *follow;
    GtkListStore *rule_rows, *register_rows;
    GtkWidget *register_search, *register_changed;
    gpointer register_filter, rule_filter;
    GtkWidget *rule_search;
    GtkWidget *register_view, *history_back, *history_forward;
    GPtrArray *history;
    guint history_position, history_requested;
    gboolean history_replaying, show_bytes;
    GtkWidget *bytes_toggle;
    GPtrArray *rules, *ranges;
    GQueue work;
    NativeAssemblyWork *pending;
    gchar *record, *function, *request, *last_error, *request_expression;
    gchar *context_thread, *context_frame;
    guint64 temporary_address, instruction_pc;
    gboolean instruction_pc_valid, source_only;
    GHashTable *instruction_points, *instruction_marks;
    gboolean active, visible, follow_needed, follow_enabled;
    GPtrArray *objects, *register_names;
    GHashTable *registers, *previous_registers;
    gchar *register_thread, *register_frame;
    guint collected, unresolved, span;
    gboolean temporary_valid;
} NativeAssembly;
