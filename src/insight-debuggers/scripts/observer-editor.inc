/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Observer draft editing and configuration.
 * RU: Редактирование черновиков и настройка наблюдателей. */

/* Ask for a commit message without changing the active debugger settings. */
/* Запрашивает сообщение коммита без изменения настроек текущей отладки. */
static gchar *debug_observer_commit_message(GtkWindow *parent)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Commit Observer Draft", parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Commit", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), 200);
    gtk_entry_set_text(GTK_ENTRY(entry), "Update debugger observer");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, TRUE, TRUE, 8);
    gtk_widget_show_all(dialog);
    gchar *message = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT ?
        g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))) : NULL;
    gtk_widget_destroy(dialog);
    if (message && !*g_strstrip(message))
        g_clear_pointer(&message, g_free);
    return message;
}

enum {
    DEBUG_OBSERVER_LOAD = 1,
    DEBUG_OBSERVER_RELOAD,
    DEBUG_OBSERVER_SAVE,
    DEBUG_OBSERVER_HISTORY,
    DEBUG_OBSERVER_COMMIT
};

/* Import a UTF-8 Lua file into the draft without applying or running it. */
/* Загружает файл Lua в UTF-8 в черновик без применения и запуска. */
static gboolean debug_load_observer_draft(GtkTextBuffer *buffer, const gchar *filename, GtkLabel *status)
{
    gsize length = 0;
    gchar *source = debug_read_source_file(filename, &length);
    if (!source || length > 64 * 1024)
    {
        gtk_label_set_text(status, "Choose a UTF-8 Lua source file of at most 64 KiB.");
        g_free(source);
        return FALSE;
    }

    gtk_text_buffer_set_text(buffer, source, (gint)length);
    gchar *display = g_filename_display_name(filename);
    gchar *message = g_strdup_printf("Loaded %s. Choose Apply to use this draft.", display);
    gtk_label_set_text(status, message);
    g_free(message);
    g_free(display);
    g_free(source);
    return TRUE;
}

/* Write the current draft to a Lua file selected by the user. */
/* Записывает текущий черновик в выбранный пользователем файл Lua. */
static gchar *debug_save_observer_draft(GtkWindow *parent, GtkTextBuffer *buffer,
                                       const gchar *filename, const gchar *root, GtkLabel *status)
{
    GtkTextIter first, last;
    gtk_text_buffer_get_bounds(buffer, &first, &last);
    gchar *source = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
    gsize length = strlen(source);
    if (length > 64 * 1024)
    {
        gtk_label_set_text(status, "Observer script exceeds 64 KiB.");
        g_free(source);
        return NULL;
    }

    GtkWidget *chooser = gtk_file_chooser_dialog_new("Save Lua Observer", parent,
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileChooser *file_chooser = GTK_FILE_CHOOSER(chooser);
    gtk_file_chooser_set_do_overwrite_confirmation(file_chooser, TRUE);
    if (filename)
    {
        gchar *directory = g_path_get_dirname(filename);
        gchar *basename = g_filename_display_basename(filename);
        gtk_file_chooser_set_current_folder(file_chooser, directory);
        gtk_file_chooser_set_current_name(file_chooser, basename);
        g_free(directory);
        g_free(basename);
    }
    else
    {
        if (root && g_path_is_absolute(root))
            gtk_file_chooser_set_current_folder(file_chooser, root);
        gtk_file_chooser_set_current_name(file_chooser, "observer.lua");
    }
    gchar *destination = gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT ?
        gtk_file_chooser_get_filename(file_chooser) : NULL;
    gtk_widget_destroy(chooser);

    if (destination)
    {
        GFile *file = g_file_new_for_path(destination);
        GError *error = NULL;
        if (g_file_replace_contents(file, source, length, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL, &error))
        {
            gchar *display = g_filename_display_name(destination);
            gchar *message = g_strdup_printf("Saved %s.", display);
            gtk_label_set_text(status, message);
            g_free(message);
            g_free(display);
        }
        else
        {
            gtk_label_set_text(status, error ? error->message : "Cannot save Lua observer.");
            g_clear_pointer(&destination, g_free);
        }
        g_clear_error(&error);
        g_object_unref(file);
    }
    g_free(source);
    return destination;
}

/* Confirm replacement of a locally edited observer draft. */
/* Подтверждает замену черновика обработчика с локальными правками. */
static gboolean debug_replace_observer_draft(GtkWindow *parent, gboolean modified)
{
    if (!modified)
        return TRUE;

    GtkWidget *confirmation = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Replace the edited observer draft?");
    gtk_dialog_add_buttons(GTK_DIALOG(confirmation), "_Keep Editing", GTK_RESPONSE_CANCEL,
                           "_Replace", GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(confirmation), GTK_RESPONSE_CANCEL);
    gboolean replace = gtk_dialog_run(GTK_DIALOG(confirmation)) == GTK_RESPONSE_ACCEPT;
    gtk_widget_destroy(confirmation);
    return replace;
}

/* Edit the stop condition and optional Lua callback for either debugger. */
/* Настраивает условие остановки и обработчик Lua для любого из двух отладчиков. */
static void debug_configure_observer(GtkButton *button, gpointer data)
{
    (void)button;
    ScriptDebugWindow *view = data;
    if (debug_is_running(view))
        return;

    GHashTable *draft_points = NULL;
    gchar *draft_condition = NULL;
    gchar *draft_source = NULL;
    gchar *filename = NULL;
    gboolean restored = !view->stop_condition && !view->observer_source;
    if (restored)
        debug_read_observer_settings(view, &draft_condition, &draft_source, &filename);
    else
    {
        draft_condition = g_strdup(view->stop_condition);
        draft_source = g_strdup(view->observer_source);
        filename = g_strdup(view->observer_filename);
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Stop Condition and Lua Observer",
        GTK_WINDOW(view->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Load Lua…", DEBUG_OBSERVER_LOAD, "_Reload File", DEBUG_OBSERVER_RELOAD,
        "_Save Lua As…", DEBUG_OBSERVER_SAVE, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Apply", GTK_RESPONSE_ACCEPT, NULL);
    gchar *history_path = debug_observer_git_path(view);
    const gchar *history_name = view->python ? "python.conf" : "lua.conf";
    if (history_path)
    {
        gtk_dialog_add_button(GTK_DIALOG(dialog), "_Git History…", DEBUG_OBSERVER_HISTORY);
        gtk_dialog_add_button(GTK_DIALOG(dialog), "_Commit Draft…", DEBUG_OBSERVER_COMMIT);
    }
    gtk_window_set_default_size(GTK_WINDOW(dialog), history_path ? 850 : 650, 420);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), 1024);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "event.locals.counter == 10");
    gtk_entry_set_text(GTK_ENTRY(entry), draft_condition ? draft_condition : "");
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Stop when true on a source line. Leave empty to disable."),
                       FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Optional Lua script returning function(event)."),
                       FALSE, FALSE, 4);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *editor = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(editor), TRUE);
    GtkTextBuffer *script = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
    gtk_text_buffer_set_text(script, draft_source ? draft_source : "", -1);
    gtk_container_add(GTK_CONTAINER(scroll), editor);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    GtkWidget *status = gtk_label_new(NULL);
    gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 4);
    if (restored && ((draft_condition && *draft_condition) || (draft_source && *draft_source)))
        gtk_label_set_text(GTK_LABEL(status), "Saved draft loaded. Choose Apply to activate it.");
    gchar *initial_condition = g_steal_pointer(&draft_condition);
    g_free(draft_source);
    gtk_text_buffer_set_modified(script, FALSE);
    gtk_widget_show_all(dialog);
    for (;;)
    {
        gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), DEBUG_OBSERVER_RELOAD, filename != NULL);
        gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response == DEBUG_OBSERVER_LOAD || response == DEBUG_OBSERVER_RELOAD)
        {
            if (!debug_replace_observer_draft(GTK_WINDOW(dialog), gtk_text_buffer_get_modified(script)))
                continue;
            gchar *selected = NULL;
            if (response == DEBUG_OBSERVER_RELOAD)
                selected = g_strdup(filename);
            else
            {
                gchar *directory = filename ? g_path_get_dirname(filename) : NULL;
                selected = debug_choose_script_file(GTK_WINDOW(dialog), "Load Lua Observer",
                                                    directory ? directory : view->project_root, FALSE);
                g_free(directory);
            }
            if (selected && debug_load_observer_draft(script, selected, GTK_LABEL(status)))
            {
                g_free(filename);
                filename = g_steal_pointer(&selected);
                gtk_text_buffer_set_modified(script, FALSE);
            }
            g_free(selected);
            continue;
        }
        if (response == DEBUG_OBSERVER_SAVE)
        {
            gchar *saved = debug_save_observer_draft(GTK_WINDOW(dialog), script, filename,
                                                    view->project_root, GTK_LABEL(status));
            if (saved)
            {
                g_free(filename);
                filename = saved;
                gtk_text_buffer_set_modified(script, FALSE);
            }
            continue;
        }
        if (response == DEBUG_OBSERVER_HISTORY || response == DEBUG_OBSERVER_COMMIT)
        {
            GError *error = NULL;
            gboolean modified = gtk_text_buffer_get_modified(script) ||
                strcmp(initial_condition ? initial_condition : "", gtk_entry_get_text(GTK_ENTRY(entry)));
            if (response == DEBUG_OBSERVER_HISTORY && debug_replace_observer_draft(GTK_WINDOW(dialog), modified))
            {
                GKeyFile *revision = debug_choose_observer_revision(GTK_WINDOW(dialog), history_path,
                                                                    history_name, &error);
                if (revision)
                {
                    gchar *condition = g_key_file_get_string(revision, "Observer", "Condition", NULL);
                    gchar *source = g_key_file_get_string(revision, "Observer", "Source", NULL);
                    if (condition && source && g_utf8_strlen(condition, -1) <= 1024 &&
                        strlen(source) <= 64 * 1024)
                    {
                        g_clear_pointer(&draft_points, g_hash_table_unref);
                        guint unresolved = 0;
                        if (g_key_file_get_boolean(revision, "RuleSet", "Present", NULL))
                        {
                            draft_points = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                                                debug_conditional_point_free);
                            debug_read_conditional_points(view, revision, draft_points, &unresolved);
                        }
                        gtk_entry_set_text(GTK_ENTRY(entry), condition);
                        gtk_text_buffer_set_text(script, source, -1);
                        gtk_text_buffer_set_modified(script, TRUE);
                        g_clear_pointer(&filename, g_free);
                        if (unresolved)
                        {
                            g_clear_pointer(&draft_points, g_hash_table_unref);
                            gtk_label_set_text(GTK_LABEL(status),
                                "Condition and script loaded. Rules were kept because some historical points could not be located.");
                        }
                        else
                            gtk_label_set_text(GTK_LABEL(status), "Revision loaded as a draft. Choose Apply to use it.");
                    }
                    else
                        gtk_label_set_text(GTK_LABEL(status), "Invalid observer revision.");
                    g_free(condition);
                    g_free(source);
                    g_key_file_unref(revision);
                }
            }
            else if (response == DEBUG_OBSERVER_COMMIT)
            {
                gchar *message = debug_observer_commit_message(GTK_WINDOW(dialog));
                if (message)
                {
                    GtkTextIter first, last;
                    gtk_text_buffer_get_bounds(script, &first, &last);
                    gchar *source = gtk_text_buffer_get_text(script, &first, &last, FALSE);
                    const gchar *condition = gtk_entry_get_text(GTK_ENTRY(entry));
                    if (debug_commit_observer_draft(view, draft_points ? draft_points : view->conditional_points,
                                                    history_path, history_name, condition, source, message, &error))
                        gtk_label_set_text(GTK_LABEL(status), "Draft committed to the project's observer history.");
                    g_free(source);
                    g_free(message);
                }
            }
            if (error)
            {
                gchar *message = g_utf8_make_valid(error->message, -1);
                gtk_label_set_text(GTK_LABEL(status), message);
                g_free(message);
                g_clear_error(&error);
            }
            continue;
        }
        if (response != GTK_RESPONSE_ACCEPT)
            break;
        gchar *condition = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
        g_strstrip(condition);
        GtkTextIter first, last;
        gtk_text_buffer_get_bounds(script, &first, &last);
        gchar *source = gtk_text_buffer_get_text(script, &first, &last, FALSE);
        g_strstrip(source);
        gchar error[1024] = "";
        GHashTable *points = draft_points ? draft_points : view->conditional_points;
        gboolean valid = !source || strlen(source) <= 64 * 1024;
        if (!valid)
            g_strlcpy(error, "Observer script exceeds 64 KiB", sizeof error);
        else if (view->python)
            valid = debug_apply_python_observer(&view->python_observer, condition, source, points, error, sizeof error);
        else if (view->engine)
            valid = debug_apply_lua_observer(view->engine, condition, source, points, error, sizeof error);
        if (valid)
        {
            if (draft_points)
            {
                g_hash_table_unref(view->conditional_points);
                view->conditional_points = g_steal_pointer(&draft_points);
                debug_save_conditional_set(view);
                debug_mark_breakpoints(GTK_EDITABLE(view->breakpoints), view);
            }
            g_free(view->stop_condition);
            g_free(view->observer_source);
            view->stop_condition = condition;
            view->observer_source = source;
            g_free(view->observer_filename);
            view->observer_filename = g_steal_pointer(&filename);
            debug_save_observer_settings(view);
            if (view->observer_log)
                debug_set_text(view->observer_log, insight_lua_debug_observer_log(
                    view->python ? view->python_observer : view->engine));
            break;
        }
        gchar *message = g_utf8_make_valid(error, -1);
        gtk_label_set_text(GTK_LABEL(status), message);
        g_free(message);
        g_free(condition);
        g_free(source);
    }
    g_free(filename);
    g_clear_pointer(&draft_points, g_hash_table_unref);
    g_free(history_path);
    g_free(initial_condition);
    gtk_widget_destroy(dialog);
}
