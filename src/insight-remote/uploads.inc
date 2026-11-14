/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Queue immutable upload snapshots for saved documents.
 * RU: Ставит неизменяемые снимки сохранённых документов в очередь передачи. */

static void
upload_snapshot_free (gpointer data)
{
    UploadSnapshot *j = data;
    transfer_free (j->transfer);
    g_free (j->source);
    g_free (j->key);
    g_free (j);
}
/* EN: Copy job data's source for task; source is unused and cancel interrupts copying.
 * RU: Копирует исходник задания data для task; source не используется, cancel прерывает копирование. */
static void
upload_snapshot_run (GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    UploadSnapshot *j = data;
    GFile *from = g_file_new_for_path (j->source), *to = g_file_new_for_path (j->transfer->snapshot);
    GError *error = NULL;
    gboolean ok = g_file_copy (from, to, G_FILE_COPY_NONE, cancel, NULL, NULL, &error);
    g_object_unref (from);
    g_object_unref (to);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, ok);
}
/* EN: Consume result for the snapshot task; source/data are unused; enqueue only the latest saved version.
 * RU: Принимает result задания снимка; source/data не используются; ставит в очередь только последнюю версию. */
static void
upload_snapshot_done (GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    (void)data;
    GTask *task = G_TASK (result);
    UploadSnapshot *j = g_task_get_task_data (task);
    GError *error = NULL;
    gboolean copied = g_task_propagate_boolean (task, &error);
    if (remote_ui.closing || j->epoch != remote_epoch)
    {
        g_clear_error (&error);
        return;
    }
    guint64 *latest = g_hash_table_lookup (remote_ui.upload_versions, j->key);
    if (!latest || *latest != j->sequence)
    {
        g_clear_error (&error);
        return;
    }
    g_hash_table_remove (remote_ui.upload_versions, j->key);
    if (!copied)
    {
        status (error ? error->message : "Could not snapshot saved source");
        g_clear_error (&error);
        return;
    }
    Transfer *t = j->transfer;
    j->transfer = NULL;
    InsightRemote *root = t->root->remote;
    for (GList *item = remote_ui.pending.head, *next; item; item = next)
    {
        next = item->next;
        Transfer *old = item->data;
        if (old->kind == UPLOAD && !strcmp (old->root->remote->host, root->host)
            && !strcmp (old->root->remote->user, root->user) && old->root->remote->port == root->port
            && !strcmp (old->target, t->target))
        {
            g_queue_delete_link (&remote_ui.pending, item);
            transfer_free (old);
        }
    }
    if (remote_ui.active)
    {
        g_queue_push_tail (&remote_ui.pending, t);
        status ("Saved locally; latest SSH upload queued");
    }
    else
        transfer_run (t);
}
/* EN: Snapshot saved document doc and asynchronously enqueue its latest remote upload.
 * RU: Создаёт снимок сохранённого doc и асинхронно ставит последнюю версию в очередь. */
static void
upload_document (GeanyDocument *doc)
{
    if (!doc || !doc->file_name)
        return;
    gchar *path = NULL;
    RemoteRoot *root = root_for_file (doc->file_name, &path);
    if (!root)
    {
        status ("Current file is not associated with an SSH source folder");
        return;
    }
    Transfer *t = g_new0 (Transfer, 1);
    t->kind = UPLOAD;
    t->root = root_copy (root);
    t->target = path;
    gchar *uuid = g_uuid_string_random ();
    t->snapshot = g_build_filename (remote_ui.directory, uuid, NULL);
    g_free (uuid);
    UploadSnapshot *j = g_new0 (UploadSnapshot, 1);
    j->transfer = t;
    j->source = utils_get_locale_from_utf8 (doc->file_name);
    j->epoch = remote_epoch;
    j->sequence = ++upload_sequence;
    gchar *target = credentials_target (root->remote);
    j->key = g_strconcat (target, ":", path, NULL);
    g_free (target);
    guint64 *version = g_new (guint64, 1);
    *version = j->sequence;
    g_hash_table_replace (remote_ui.upload_versions, g_strdup (j->key), version);
    GTask *task = g_task_new (NULL, remote_ui.snapshot_cancel, upload_snapshot_done, NULL);
    g_task_set_task_data (task, j, upload_snapshot_free);
    g_task_run_in_thread (task, upload_snapshot_run);
    g_object_unref (task);
    status ("Saved locally; preparing SSH upload snapshot…");
}
/* EN: Schedule auto-upload for saved doc when enabled; object/data are unused.
 * RU: Планирует автоматическую передачу сохранённого doc при включении; object/data не используются. */
static void
saved_document (GObject *object, GeanyDocument *doc, gpointer data)
{
    (void)object;
    (void)data;
    if (!doc || !doc->file_name)
        return;
    gchar *path = NULL;
    RemoteRoot *root = root_for_file (doc->file_name, &path);
    g_free (path);
    if (root && root->auto_upload)
        upload_document (doc);
}
/* EN: Save and upload the current remote-backed document; widget/data are unused.
 * RU: Сохраняет и передаёт текущий удалённый документ; widget/data не используются. */
static void
save_remote (GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    GeanyDocument *doc = document_get_current ();
    if (!doc)
        return;
    gchar *path = NULL;
    RemoteRoot *root = doc->file_name ? root_for_file (doc->file_name, &path) : NULL;
    g_free (path);
    if (!root)
    {
        status ("Open a file through SSH first");
        return;
    }
    if (!doc->changed)
        upload_document (doc);
    else if (document_save_file (doc, FALSE) && !root->auto_upload)
        upload_document (doc);
}
/* EN: Cancel packing, snapshots and queued transfers while retaining edits; widget/data are unused.
 * RU: Отменяет упаковку, снимки и передачи, сохраняя правки; widget/data не используются. */
static void
stop_transfer (GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    scp_cancel ();
    remote_epoch++;
    if (remote_ui.snapshot_cancel)
    {
        g_cancellable_cancel (remote_ui.snapshot_cancel);
        g_clear_object (&remote_ui.snapshot_cancel);
    }
    if (!remote_ui.closing)
        remote_ui.snapshot_cancel = g_cancellable_new ();
    if (remote_ui.upload_versions)
        g_hash_table_remove_all (remote_ui.upload_versions);
    if (remote_ui.active)
    {
        g_cancellable_cancel (remote_ui.active->cancel);
        g_subprocess_force_exit (remote_ui.active->process);
    }
    while (!g_queue_is_empty (&remote_ui.pending))
        transfer_free (g_queue_pop_head (&remote_ui.pending));
    status ("Transfers stopped; local edits retained");
}
