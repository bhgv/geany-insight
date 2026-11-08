/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Simulation state, resource ownership and saved pins.
 * RU: Состояние симуляции, владение ресурсами и закреплённые сигналы.
 */

typedef struct
{
    guint64 number, tick;
    gboolean has_tick;
    gchar *location;
    GHashTable *values;
} SignalHistoryStop;

/* EN: Free the history stop in data. RU: Освобождает снимок data. */
static void signal_history_stop_free(gpointer data);

typedef struct
{
    GWeakRef parent;
    GtkWidget *dialog;
    gchar *compiler, *runtime, *include_dir;
    gchar **sources;
    gchar *port_settings, *port_harness;
    GPtrArray *port_model_sources;
    GHashTable *port_images;
    GtkWidget *live_devices;
    GtkWidget *live_port_input, *live_port_value, *live_port_send, *live_port_display;
    GHashTable *live_port_samples, *live_indicators;
    GPtrArray *live_scopes;
    GPtrArray *rasters;
    gint scope_console_prefix;
    gchar console_prefix[24];
    GtkWidget *uart_window, *uart_view, *uart_entry;
    GString *uart_log;
    guint uart_send_sequence;
    InsightWaveSource navigate;
    InsightWaveContext show_context;
    gboolean interactive;
    GMutex lock;
    GSubprocess *live_process;
    gchar *live_directory;
    GByteArray *live_output;
    GCond live_output_space;
    GtkWidget *console, *live_controls;
    guint refresh_source;
    gchar prompt_tail[2];
    gboolean preview_pending;
    GtkWidget *signal_name;
    gboolean register_write_ready, signal_stops_ready;
    GtkListStore *signal_stop_store;
    GtkTreeSelection *signal_stop_selection;
    GtkWidget *signal_stop_remove, *signal_stop_edit, *signal_stop_toggle, *pause_reason;
    gchar *signal_stop_path, *signal_stop_hit;
    GQueue signal_stop_restore;
    gboolean signal_stop_restoring, signal_stop_disabling, signal_manual_stop;
    GtkListStore *signal_tables[4];
    GtkTreeStore *frame_values;
    GtkWidget *frame_status;
    GtkWidget *frame_view;
    GtkTreeIter frame_group;
    gboolean frame_has_group;
    GtkTreePath *frame_reopen_path;
    GtkTreeIter frame_field_parents[9];
    guint frame_field_parent_count;
    gboolean frame_field_editable;
    gboolean frame_ready;
    gboolean frame_refresh;
    gboolean frame_receiving;
    GtkWidget *signal_search, *signal_status, *signal_context, *signal_state;
    GtkWidget *signal_changed_only;
    GtkTreeSelection *signal_selection[4];
    gboolean signal_refilling;
    GtkCellRenderer *signal_register_renderer;
    GPtrArray *signal_rows;
    GHashTable *signal_previous_values, *signal_pins;
    gchar *signal_pins_path;
    GQueue signal_history;
    guint64 signal_stop_number, signal_snapshot_tick;
    gboolean signal_snapshot_has_tick;
    gboolean signal_history_replace;
    gchar *signal_file;
    guint signal_format;
    gboolean signal_tables_ready, signal_collecting, signal_limited, signal_refresh_needed;
    gboolean signal_snapshot_received;
    GtkWidget *pause_button, *source_button, *breakpoint_controls, *breakpoint_file, *breakpoint_line;
    GtkWidget *rule_signal, *rule_bits, *rule_hits, *rule_log, *rule_script;
    GtkWidget *lua_watches, *reload_script;
    gchar *settings_path, *observer_file;
    gboolean restored_rules;
    InsightLuaDebug *lua_observer;
    gchar **pending_event;
    gsize observer_log_offset;
    gboolean bridge_ready, pause_requested;
    gboolean source_step_ready, processes_ready;
    GtkWidget *process_controls, *process_number;
    GtkWidget *frame_controls, *frame_depth, *memory_word;
    gboolean frames_ready;
    GtkWidget *source_step_controls;
    GString *protocol_line;
    GHashTable *breakpoints, *point_anchors;
    GPtrArray *unresolved_records, *unresolved_anchors;
    guint moved_points;
    gchar *trace_file;
    guint trace_line;
    InsightWaveLocation open_location;
} WaveSimulation;

/* EN: Release WaveSimulation data after its dialog and worker have finished.
 * RU: Освобождает WaveSimulation из data после закрытия окна и завершения задачи. */
static void simulation_free(gpointer data)
{
    WaveSimulation *run = data;
    if (run->refresh_source)
        g_source_remove(run->refresh_source);
    g_clear_object(&run->live_process);
    g_mutex_clear(&run->lock);
    g_cond_clear(&run->live_output_space);
    g_clear_pointer(&run->live_output, g_byte_array_unref);
    g_free(run->live_directory);
    g_free(run->trace_file);
    g_clear_pointer(&run->signal_rows, g_ptr_array_unref);
    g_clear_pointer(&run->frame_reopen_path, gtk_tree_path_free);
    g_clear_pointer(&run->signal_previous_values, g_hash_table_unref);
    g_clear_pointer(&run->signal_pins, g_hash_table_unref);
    g_free(run->signal_pins_path);
    g_clear_object(&run->signal_stop_store);
    g_free(run->signal_stop_path);
    g_free(run->signal_stop_hit);
    while (!g_queue_is_empty(&run->signal_stop_restore))
        g_free(g_queue_pop_head(&run->signal_stop_restore));
    while (!g_queue_is_empty(&run->signal_history))
        signal_history_stop_free(g_queue_pop_head(&run->signal_history));
    g_free(run->signal_file);
    for (guint i = 0; i < G_N_ELEMENTS(run->signal_tables); i++)
        g_clear_object(&run->signal_tables[i]);
    g_free(run->settings_path);
    g_free(run->port_settings);
    g_free(run->port_harness);
    g_clear_pointer(&run->port_model_sources, g_ptr_array_unref);
    g_clear_pointer(&run->port_images, g_hash_table_unref);
    g_clear_pointer(&run->live_port_samples, g_hash_table_unref);
    g_clear_pointer(&run->live_indicators, g_hash_table_unref);
    g_clear_pointer(&run->live_scopes, g_ptr_array_unref);
    g_clear_pointer(&run->rasters, g_ptr_array_unref);
    if (run->uart_log)
        g_string_free(run->uart_log, TRUE);
    g_free(run->observer_file);
    insight_lua_debug_free(run->lua_observer);
    g_strfreev(run->pending_event);
    if (run->protocol_line)
        g_string_free(run->protocol_line, TRUE);
    g_clear_pointer(&run->breakpoints, g_hash_table_unref);
    g_clear_pointer(&run->point_anchors, g_hash_table_unref);
    g_clear_pointer(&run->unresolved_records, g_ptr_array_unref);
    g_clear_pointer(&run->unresolved_anchors, g_ptr_array_unref);
    g_weak_ref_clear(&run->parent);
    g_clear_object(&run->dialog);
    g_strfreev(run->sources);
    g_free(run->compiler);
    g_free(run->runtime);
    g_free(run->include_dir);
    g_free(run);
}

/* EN: Build an owned settings path below directory keyed by project or the selected sources.
 * RU: Возвращает путь в directory по ключу проекта project или набора sources. */
static gchar *simulation_settings_path(const gchar *directory, const gchar *project, gchar **sources)
{
    if (!directory)
        return NULL;
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    if (project)
        g_checksum_update(checksum, (const guchar *)project, strlen(project));
    GSList *ordered = NULL;
    for (guint i = 0; sources[i]; i++)
        ordered = g_slist_prepend(ordered, sources[i]);
    ordered = g_slist_sort(ordered, (GCompareFunc)g_strcmp0);
    for (GSList *item = ordered; item; item = item->next)
    {
        const gchar *source = item->data;
        g_checksum_update(checksum, (const guchar *)"\0", 1);
        g_checksum_update(checksum, (const guchar *)source, strlen(source));
    }
    gchar *name = g_strconcat(g_checksum_get_string(checksum), ".conf", NULL);
    gchar *path = g_build_filename(directory, name, NULL);
    g_slist_free(ordered);
    g_checksum_free(checksum);
    g_free(name);
    return path;
}

/* EN: Load a bounded set of pin keys from path. A missing file is empty.
 * RU: Загружает ограниченный набор закреплений из path. Отсутствующий файл пуст. */
static GHashTable *signal_pins_load(const gchar *path)
{
    GHashTable *pins = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GStatBuf file_info;
    if (!path || g_stat(path, &file_info) || file_info.st_size > 512 * 1024)
        return pins;
    GKeyFile *config = g_key_file_new();
    if (g_key_file_load_from_file(config, path, G_KEY_FILE_NONE, NULL))
    {
        gsize length = 0;
        gchar **entries = g_key_file_get_string_list(config, "signals", "pins", &length, NULL);
        for (gsize i = 0; i < length && i < 256; i++)
        {
            const gchar *key = entries[i];
            if (strchr("PNR", key[0]) && key[0] && key[1] == '\t' && key[2] && strlen(key) <= 1026 &&
                !strpbrk(key + 2, "\t\r\n"))
                g_hash_table_add(pins, g_strdup(key));
        }
        g_strfreev(entries);
    }
    g_key_file_unref(config);
    return pins;
}

/* EN: Write pins to path and report failure through error.
 * RU: Сохраняет pins по path и возвращает ошибку через error. */
static gboolean signal_pins_save(const gchar *path, GHashTable *pins, GError **error)
{
    gchar *directory = g_path_get_dirname(path);
    gint made = g_mkdir_with_parents(directory, 0700);
    g_free(directory);
    if (made)
    {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                            insight_tr("Could not create the pinned signal settings folder."));
        return FALSE;
    }
    guint count;
    gchar **keys = (gchar **)g_hash_table_get_keys_as_array(pins, &count);
    GKeyFile *config = g_key_file_new();
    g_key_file_set_string_list(config, "signals", "pins", (const gchar *const *)keys, count);
    gboolean saved = g_key_file_save_to_file(config, path, error);
    g_key_file_unref(config);
    g_free(keys);
    return saved;
}
