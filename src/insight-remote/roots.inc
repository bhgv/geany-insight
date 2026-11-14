/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Manage SSH cache roots and opening remote files.
 * RU: Управляет кэшем SSH и открытием удалённых файлов. */

static void
root_metadata (RemoteRoot *root)
{
    GKeyFile *key = g_key_file_new ();
    InsightRemote *r = root->remote;
    g_key_file_set_string (key, "ssh", "host", r->host);
    g_key_file_set_string (key, "ssh", "user", r->user);
    g_key_file_set_integer (key, "ssh", "port", (gint)r->port);
    g_key_file_set_string (key, "ssh", "identity", r->identity);
    g_key_file_set_string (key, "ssh", "remote", r->remote);
    g_key_file_set_string (key, "ssh", "local", r->local);
    g_key_file_set_string (key, "ssh", "config", root->config);
    g_key_file_set_boolean (key, "ssh", "auto-upload", root->auto_upload);
    g_key_file_set_string (key, "ssh", "ssh-config", root->ssh_config ? root->ssh_config : "");
    gsize length;
    gchar *data = g_key_file_to_data (key, &length, NULL),
          *path = g_build_filename (root->directory, "connection.conf", NULL);
    GFile *file = g_file_new_for_path (path);
    g_file_replace_contents (file, data, length, NULL, FALSE, G_FILE_CREATE_PRIVATE, NULL, NULL, NULL);
    g_object_unref (file);
    g_free (path);
    g_free (data);
    g_key_file_unref (key);
}
/* EN: Persist root metadata and transfer ownership to the root list.
 * RU: Сохраняет параметры root и передаёт владение списку корней. */
static void
root_register (RemoteRoot *root)
{
    root_metadata (root);
    g_ptr_array_add (remote_ui.roots, root);
}
/* EN: Restore at most 128 valid existing local SSH caches from private metadata.
 * RU: Восстанавливает до 128 существующих кэшей SSH из закрытых параметров. */
static void
restore_roots (void)
{
    GDir *dir = g_dir_open (remote_ui.directory, 0, NULL);
    if (!dir)
        return;
    const gchar *name;
    while ((name = g_dir_read_name (dir)) && remote_ui.roots->len < 128)
    {
        gchar *directory = g_build_filename (remote_ui.directory, name, NULL),
              *path = g_build_filename (directory, "connection.conf", NULL);
        GKeyFile *key = g_key_file_new ();
        if (g_key_file_load_from_file (key, path, G_KEY_FILE_NONE, NULL))
        {
            gchar *host = g_key_file_get_string (key, "ssh", "host", NULL),
                  *user = g_key_file_get_string (key, "ssh", "user", NULL),
                  *identity = g_key_file_get_string (key, "ssh", "identity", NULL),
                  *server = g_key_file_get_string (key, "ssh", "remote", NULL),
                  *local = g_key_file_get_string (key, "ssh", "local", NULL),
                  *config = g_key_file_get_string (key, "ssh", "config", NULL);
            gint port = g_key_file_get_integer (key, "ssh", "port", NULL);
            InsightRemote *r
                = local && config && g_file_test (local, G_FILE_TEST_IS_DIR)
                      ? insight_remote_new (host, user, (guint)port, identity, server, local, NULL)
                      : NULL;
            if (r)
            {
                RemoteRoot *root = g_new0 (RemoteRoot, 1);
                root->remote = r;
                root->directory = g_strdup (directory);
                root->config = g_strdup (config);
                root->ssh_config = g_key_file_get_string (key, "ssh", "ssh-config", NULL);
                root->auto_upload = g_key_file_get_boolean (key, "ssh", "auto-upload", NULL);
                g_ptr_array_add (remote_ui.roots, root);
            }
            g_free (host);
            g_free (user);
            g_free (identity);
            g_free (server);
            g_free (local);
            g_free (config);
        }
        g_key_file_unref (key);
        g_free (path);
        g_free (directory);
    }
    g_dir_close (dir);
}
/* EN: Clear the active transfer and start the next queued operation unless closing.
 * RU: Освобождает активное место и запускает следующее задание, кроме закрытия. */
static void
next_transfer (void)
{
    remote_ui.active = NULL;
    if (!remote_ui.closing && !g_queue_is_empty (&remote_ui.pending))
        transfer_run (g_queue_pop_head (&remote_ui.pending));
}
#include "transfer-results.inc"
/* EN: Start asynchronous SFTP transfer t; route setup errors through normal completion.
 * RU: Запускает асинхронное SFTP-задание t; передаёт ошибки обычному завершению. */
static void
transfer_run (Transfer *t)
{
    remote_ui.active = t;
    GError *error = NULL;
    gchar *batch = NULL;
    if (t->kind == UPLOAD)
    {
        gchar *nonce = g_uuid_string_random ();
        batch = insight_remote_upload (t->snapshot, t->target, nonce, &error);
        g_free (nonce);
    }
    else
        batch = insight_remote_download (
            t->kind == OPEN_FILE || t->kind == PROJECT_READ ? t->origin : t->root->remote->remote,
            t->kind == OPEN_FILE || t->kind == PROJECT_READ ? t->target : t->root->remote->local,
            t->kind == OPEN_FOLDER || t->kind == PROJECT_FOLDER, &error);
    if (batch)
    {
        GSubprocessLauncher *spawn = launcher (t->root, &t->auth);
        t->process = spawn_client (spawn, t->root, TRUE, &error);
        g_object_unref (spawn);
    }
    if (!t->process)
    {
        status (error ? error->message : "SFTP unavailable");
        g_clear_error (&error);
        g_free (batch);
        transfer_free (t);
        next_transfer ();
        return;
    }
    t->cancel = g_cancellable_new ();
    status (t->kind == UPLOAD ? "Uploading saved snapshot…" : "Downloading via SFTP…");
    g_subprocess_communicate_utf8_async (t->process, batch, t->cancel, transfer_done, t);
    g_free (batch);
}
/* EN: Validate form endpoint and remote/local paths; return owned cache root in directory or error.
 * RU: Проверяет сервер формы и пути remote/local; возвращает корень в directory либо error. */
static RemoteRoot *
form_root (const char *remote, const char *local, const char *directory, GError **error)
{
    RemoteRoot *root = g_new0 (RemoteRoot, 1);
    gchar *host = g_strdup (gtk_entry_get_text (GTK_ENTRY (remote_ui.host))),
          *user = g_strdup (gtk_entry_get_text (GTK_ENTRY (remote_ui.user)));
    g_strstrip (host);
    g_strstrip (user);
    root->remote = insight_remote_new (
        host, user, (guint)gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (remote_ui.port)),
        gtk_entry_get_text (GTK_ENTRY (remote_ui.identity)), remote, local, error);
    g_free (host);
    g_free (user);
    if (!root->remote)
    {
        g_free (root);
        return NULL;
    }
    root->directory = g_strdup (directory);
    root->config = g_strdup (remote_ui.config);
    root->ssh_config = g_strdup (gtk_entry_get_text (GTK_ENTRY (remote_ui.ssh_config)));
    root->auto_upload = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (remote_ui.automatic));
    return root;
}
/* EN: Open the location from the form for transfer kind data; widget is unused.
 * RU: Открывает путь формы для вида задания data; widget не используется. */
static void
open_remote (GtkWidget *widget, gpointer data)
{
    (void)widget;
    if (remote_ui.active)
    {
        status ("A transfer is active; stop it or wait before opening another location");
        return;
    }
    profile_save ();
    TransferKind kind = (TransferKind)GPOINTER_TO_INT (data);
    const char *path = gtk_entry_get_text (GTK_ENTRY (remote_ui.path));
    if (*path != '/'
        || (kind != OPEN_FOLDER
            && (g_str_has_suffix (path, "/") || g_str_has_suffix (path, "/.")
                || g_str_has_suffix (path, "/.."))))
    {
        status ("Enter an absolute remote file or folder path");
        return;
    }
    gchar *uuid = g_uuid_string_random (), *directory = g_build_filename (remote_ui.directory, uuid, NULL),
          *local = g_build_filename (directory, "data", NULL), *base = g_strdup (path);
    gchar *slash = strrchr (base, '/');
    if (kind != OPEN_FOLDER)
    {
        if (slash == base)
            slash[1] = 0;
        else
            *slash = 0;
    }
    g_free (uuid);
    GError *error = NULL;
    RemoteRoot *root = form_root (base, local, directory, &error);
    g_free (base);
    if (!root || g_mkdir_with_parents (directory, 0700))
    {
        status (error ? error->message : "Cannot create SSH cache");
        g_clear_error (&error);
        if (root)
            root_free (root);
        g_free (local);
        g_free (directory);
        return;
    }
    Transfer *t = g_new0 (Transfer, 1);
    t->root = root;
    t->kind = kind;
    t->origin = g_strdup (path);
    if (kind == OPEN_FILE)
    {
        g_mkdir (local, 0700);
        const char *name = strrchr (path, '/') + 1;
        t->target = g_build_filename (local, name, NULL);
    }
    else if (kind == PROJECT_READ)
        t->target = g_build_filename (directory, "project.geany", NULL);
    g_free (local);
    g_free (directory);
    transfer_run (t);
}
/* EN: Find cached local path and return its borrowed root; remote receives an owned server path.
 * RU: Ищет локальный path и возвращает корень; remote получает путь сервера. */
static RemoteRoot *
root_for_file (const char *path, gchar **remote)
{
    for (gint i = (gint)remote_ui.roots->len - 1; i >= 0; i--)
    {
        RemoteRoot *root = g_ptr_array_index (remote_ui.roots, (guint)i);
        *remote = insight_remote_path (root->remote, path);
        if (*remote)
            return root;
    }
    return NULL;
}
typedef struct
{
    Transfer *transfer;
    gchar *source, *key;
    guint64 sequence, epoch;
} UploadSnapshot;
/* Independent SCP/archive job. Each child is owned directly, so Stop also
 * cancels packing and never touches an interactive shell process. */
struct _ScpJob
{
    RemoteRoot *root;
    gchar *local, *temporary, *stage, *auth, *client, *python, *helper;
    gint mode;
    gboolean upload, folder;
    GSubprocessLauncher *spawn;
    GSubprocess *process;
    GCancellable *cancel;
    GMutex lock;
};
