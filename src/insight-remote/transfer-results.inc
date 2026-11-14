/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Apply successful remote downloads and project imports without propagating
 * credentials. RU: Применяет загрузки и импорт проектов без переноса паролей.
 */
/* EN: Complete result from object for Transfer data, reporting errors and
 * retaining local edits. RU: Завершает result от object для Transfer data,
 * показывая ошибки и сохраняя локальные правки. */
static void
transfer_done (GObject *object, GAsyncResult *result, gpointer data)
{
    Transfer *t = data;
    GError *error = NULL;
    gchar *output = NULL;
    gboolean ok = g_subprocess_communicate_utf8_finish (G_SUBPROCESS (object), result, &output, NULL, &error)
                  && g_subprocess_get_successful (t->process);
    if (remote_ui.closing)
    {
        g_clear_error (&error);
        g_free (output);
        transfer_free (t);
        return;
    }
    if (!ok)
    {
        gchar *message = g_strdup_printf (
            "Transfer failed; local edits retained: %.1200s",
            error ? error->message : (output && *output ? output : "OpenSSH exited unsuccessfully"));
        status (message);
        g_free (message);
    }
    else if (t->kind == PROJECT_READ)
    {
        t->project = g_key_file_new ();
        if (!g_key_file_load_from_file (t->project, t->target, G_KEY_FILE_KEEP_COMMENTS, NULL)
            || !g_key_file_has_group (t->project, "project"))
            status ("Remote file is not a Geany project");
        else
        {
            gchar *base = g_key_file_get_string (t->project, "project", "base_path", NULL),
                  *normalized
                  = insight_remote_normalize (t->root->remote->remote, base && *base ? base : ".");
            g_free (base);
            g_free (t->root->remote->remote);
            t->root->remote->remote = normalized;
            t->kind = PROJECT_FOLDER;
            g_clear_object (&t->process);
            g_clear_object (&t->cancel);
            auth_cleanup (t->auth);
            g_clear_pointer (&t->auth, g_free);
            g_clear_error (&error);
            g_free (output);
            transfer_run (t);
            return;
        }
    }
    else if (t->kind == UPLOAD)
    {
        gchar *message = g_strdup_printf ("Uploaded %s:%s", t->root->remote->host, t->target);
        status (message);
        g_free (message);
    }
    else
    {
        RemoteRoot *root = t->root;
        t->root = NULL;
        if (t->kind == PROJECT_FOLDER)
        {
            g_key_file_set_string (t->project, "project", "base_path", "data");
            g_key_file_remove_group (t->project, "files", NULL);
            g_key_file_set_string (t->project, "insight-ssh", "host", root->remote->host);
            g_key_file_set_string (t->project, "insight-ssh", "user", root->remote->user);
            g_key_file_set_integer (t->project, "insight-ssh", "port", (gint)root->remote->port);
            g_key_file_set_string (t->project, "insight-ssh", "remote-path", t->origin);
            g_key_file_set_string (t->project, "insight-ssh", "remote-base-path", root->remote->remote);
            g_key_file_set_boolean (t->project, "insight-ssh", "auto-upload", root->auto_upload);
            g_key_file_set_boolean (
                t->project, "insight-ssh", "save-password",
                gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (remote_ui.save_password)));
            gchar **groups = g_key_file_get_groups (t->project, NULL);
            for (guint i = 0; groups[i]; i++)
                if (g_str_has_prefix (groups[i], "insight-ssh-password-"))
                    g_key_file_remove_group (t->project, groups[i], NULL);
            g_strfreev (groups);
            gsize length;
            gchar *contents = g_key_file_to_data (t->project, &length, NULL);
            gboolean written = g_file_set_contents (t->target, contents, (gssize)length, NULL);
            g_free (contents);
            g_free (root->config);
            root->config = g_strdup (t->target);
            root_register (root);
            if (written && project_ask_close () && document_close_all () && project_load_file (t->target))
                status ("Remote project opened; source files use the SSH cache");
            else
                status ("Project cached; opening cancelled or project file could not be "
                        "written");
        }
        else
        {
            root_register (root);
            if (t->kind == OPEN_FILE)
            {
                if (document_open_file (t->target, FALSE, NULL, NULL))
                    status ("Remote file opened; Save remote uploads, or enable Auto upload");
                else
                    status ("File downloaded but editor could not open it");
            }
            else
            {
                if (remote_ui.workspace (root->remote->local))
                    status ("Remote source folder opened and indexing started");
                else
                    status ("Downloaded folder could not be indexed");
            }
        }
    }
    g_clear_error (&error);
    g_free (output);
    transfer_free (t);
    next_transfer ();
}
