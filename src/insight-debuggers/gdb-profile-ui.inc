/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Save, restore and forget explicit debugger profiles without connecting or executing targets.
 * RU: Сохраняет, восстанавливает и удаляет профили без подключения и запуска программ. */
#include <errno.h>
#include <glib/gstdio.h>

/* EN: Return the entry at index in session's profile schema, matching native_profile_names exactly.
 * RU: Возвращает поле index схемы профиля session в точном порядке native_profile_names. */
static GtkWidget *native_profile_entry(NativeDebug *session, guint index)
{
    NativeRemote *remote = session->remote;
    GtkWidget *entries[] = {remote->host, remote->user, remote->identity, remote->config,
        remote->server_host, remote->executable, remote->remote_source, remote->local_source,
        remote->sysroot, session->launch_args, session->launch_directory};
    return index < G_N_ELEMENTS(entries) ? entries[index] : NULL;
}

/* EN: Load session's whole validated profile; missing files are silent only during automatic opening.
 * RU: Загружает полностью проверенный профиль session; отсутствие файла скрывается только при открытии окна. */
static void native_profile_load(NativeDebug *session, gboolean automatic)
{
    NativeRemote *remote = session->remote;
    GError *error = NULL;
    GStatBuf information;
    if (g_stat(remote->profile_path, &information))
    {
        if (!automatic || errno != ENOENT)
            gtk_label_set_text(GTK_LABEL(remote->status), insight_tr("Debugger profile was not found or cannot be read."));
        return;
    }
    if (information.st_size < 0 || information.st_size > 256 * 1024)
    {
        gtk_label_set_text(GTK_LABEL(remote->status), insight_tr("Debugger profile exceeds the 256 KiB limit."));
        return;
    }
    GKeyFile *profile = g_key_file_new();
    gboolean valid = g_key_file_load_from_file(profile, remote->profile_path, G_KEY_FILE_NONE, &error) &&
        native_profile_validate(profile, &error);
    if (valid)
    {
        for (guint i = 0; i < G_N_ELEMENTS(native_profile_names); i++)
        {
            gchar *value = g_key_file_get_string(profile, "debugger", native_profile_names[i], NULL);
            gtk_entry_set_text(GTK_ENTRY(native_profile_entry(session, i)), value);
            g_free(value);
        }
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(remote->port),
            g_key_file_get_integer(profile, "debugger", "ssh-port", NULL));
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(remote->server_port),
            g_key_file_get_integer(profile, "debugger", "server-port", NULL));
        gtk_combo_box_set_active(GTK_COMBO_BOX(remote->mode),
            g_key_file_get_integer(profile, "debugger", "mode", NULL));
        gtk_label_set_text(GTK_LABEL(remote->status), insight_tr("Debugger profile restored. Connect or Run explicitly."));
        native_profile_debug_load(session, profile);
    }
    else
        gtk_label_set_text(GTK_LABEL(remote->status), error ? error->message : insight_tr("Cannot restore debugger profile."));
    g_clear_error(&error);
    g_key_file_unref(profile);
}

/* EN: Save session's form, source points and watches; preserve the old profile on validation/write failures.
 * RU: Сохраняет форму, точки исходников и наблюдения session; при ошибках проверки/записи прежний профиль сохраняется. */
static void native_profile_save(NativeDebug *session)
{
    NativeRemote *remote = session->remote;
    GKeyFile *profile = g_key_file_new();
    g_key_file_set_integer(profile, "debugger", "version", 2);
    g_key_file_set_integer(profile, "debugger", "mode", gtk_combo_box_get_active(GTK_COMBO_BOX(remote->mode)));
    g_key_file_set_integer(profile, "debugger", "ssh-port", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote->port)));
    g_key_file_set_integer(profile, "debugger", "server-port", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote->server_port)));
    for (guint i = 0; i < G_N_ELEMENTS(native_profile_names); i++)
        g_key_file_set_string(profile, "debugger", native_profile_names[i],
            gtk_entry_get_text(GTK_ENTRY(native_profile_entry(session, i))));
    native_profile_debug_store(session, profile);
    GError *error = NULL;
    gboolean valid = native_profile_validate(profile, &error);
    gchar *directory = g_path_get_dirname(remote->profile_path);
    if (valid && g_mkdir_with_parents(directory, 0700))
    {
        g_set_error_literal(&error, G_IO_ERROR, g_io_error_from_errno(errno), g_strerror(errno));
        valid = FALSE;
    }
    if (valid)
    {
        gsize length = 0;
        gchar *contents = g_key_file_to_data(profile, &length, &error);
        if (contents && length > 256 * 1024)
        {
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                insight_tr("Debugger profile exceeds the 256 KiB limit."));
            valid = FALSE;
        }
        else
        {
            GFile *file = g_file_new_for_path(remote->profile_path);
            valid = contents && g_file_replace_contents(file, contents, length, NULL, FALSE,
                G_FILE_CREATE_PRIVATE, NULL, NULL, &error);
            g_object_unref(file);
        }
        g_free(contents);
    }
    gtk_label_set_text(GTK_LABEL(remote->status), valid ? insight_tr("Debugger profile saved for this project and executable.") :
        error ? error->message : insight_tr("Cannot save debugger profile."));
    g_clear_error(&error);
    g_free(directory);
    g_key_file_unref(profile);
}

/* EN: Save idle/paused session data from button; restoring/forgetting require an inactive target and tunnel.
 * RU: Сохраняет session data по button в простое/на паузе; восстановление/удаление требуют неактивных программы и туннеля. */
static void native_profile_clicked(GtkButton *button, gpointer data)
{
    NativeDebug *session = data;
    NativeRemote *remote = session->remote;
    if (!session->ready || session->pending || session->writing || session->running || remote->busy)
        return;
    guint action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "profile-action"));
    if (action != 0 && (session->stopped || remote->ssh))
        return;
    if (action == 0)
        native_profile_save(session);
    else if (action == 1)
        native_profile_load(session, FALSE);
    else
    {
        gboolean removed = !g_unlink(remote->profile_path) || errno == ENOENT;
        gtk_label_set_text(GTK_LABEL(remote->status), removed ?
            insight_tr("Saved debugger profile forgotten. Current form values are kept.") : g_strerror(errno));
    }
}

/* EN: Build session's explicit profile controls; target and current project determine storage scope.
 * RU: Создаёт явные кнопки профиля session; область хранения определяется target и текущим проектом. */
static GtkWidget *native_profile_widget(NativeDebug *session, const gchar *target)
{
    NativeRemote *remote = session->remote;
    remote->profile_path = native_profile_path(app->configdir,
        app->project ? app->project->file_name : NULL, target);
    GtkWidget *box = remote->profile_controls = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(box), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(box), 3);
    gtk_widget_set_tooltip_text(box, remote->profile_path);
    const gchar *labels[] = {"Save debugger profile", "Restore debugger profile", "Forget saved profile"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(insight_tr(labels[i]));
        g_object_set_data(G_OBJECT(button), "profile-action", GUINT_TO_POINTER(i));
        if (i == 0)
            remote->profile_save = button;
        else if (i == 1)
            remote->profile_restore = button;
        else
            remote->profile_forget = button;
        g_signal_connect(button, "clicked", G_CALLBACK(native_profile_clicked), session);
        gtk_container_add(GTK_CONTAINER(box), button);
    }
    return box;
}
