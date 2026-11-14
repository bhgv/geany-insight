/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Expose SSH terminal actions, tabs and interface lifecycle.
 * RU: Предоставляет действия, вкладки и жизненный цикл SSH. */

static void
identity_choose (GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new (
        "SSH identity file", GTK_WINDOW (main_widgets.window), GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel",
        GTK_RESPONSE_CANCEL, "Choose", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
        gchar *path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
        gtk_entry_set_text (GTK_ENTRY (remote_ui.identity), path);
        g_free (path);
    }
    gtk_widget_destroy (dialog);
}
/* EN: Return whether endpoint r matches the current host, login and port fields.
 * RU: Возвращает совпадение r с текущими адресом, логином и портом формы. */
static gboolean
endpoint_matches (const InsightRemote *r)
{
    return !strcmp (r->host, gtk_entry_get_text (GTK_ENTRY (remote_ui.host)))
           && !strcmp (r->user, gtk_entry_get_text (GTK_ENTRY (remote_ui.user)))
           && r->port == (guint)gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (remote_ui.port));
}
#include "profile.inc"
/* EN: Reveal the SSH panel and focus its host; widget/data are unused.
 * RU: Показывает панель SSH и переводит фокус на адрес; widget/data не используются. */
static void
show_remote (GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    insight_terminal_ui_reveal (remote_ui.page);
    gtk_widget_grab_focus (remote_ui.host);
}
/* EN: Send command to session data only if its connection is active; return whether sent.
 * RU: Отправляет command в сеанс data только при активном соединении; возвращает успех. */
static gboolean
ai_insert (const gchar *command, gpointer data)
{
    ShellSession *s = data;
    if (remote_ui.closing || !s->input)
        return FALSE;
    if (!s->process)
        return FALSE;
    insight_terminal_send (s->output, command);
    return TRUE;
}
/* EN: Return owned diagnostic text for session s using terminal output and nonsecret form fields.
 * RU: Возвращает диагностический текст сеанса s из вывода терминала и несекретных полей. */
static gchar *
session_context (ShellSession *s)
{
    GtkTextBuffer *b = gtk_text_view_get_buffer (GTK_TEXT_VIEW (s->output));
    GtkTextIter a, z;
    if (!gtk_text_buffer_get_selection_bounds (b, &a, &z))
    {
        gtk_text_buffer_get_end_iter (b, &z);
        a = z;
        gtk_text_iter_backward_chars (&a, 12000);
    }
    gchar *log
        = gtk_text_buffer_get_text (b, &a, &z, TRUE),
        *context = g_strdup_printf (
            "SSH session %u\nConnected endpoint: %s\nConnection form: host=%s user=%s port=%d\nTransfer "
            "remote path: %s\nThe shell uses a VT100/xterm-compatible screen. Analyze connection/transfer "
            "issues and remote diagnostics; relate file:line errors to explicitly attached local source. "
            "Commands execute only on explicit user action.\n\n%s",
            s->number, s->endpoint ? s->endpoint : "not connected",
            gtk_entry_get_text (GTK_ENTRY (remote_ui.host)), gtk_entry_get_text (GTK_ENTRY (remote_ui.user)),
            gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (remote_ui.port)),
            gtk_entry_get_text (GTK_ENTRY (remote_ui.path)), log);
    g_free (log);
    return context;
}
/* EN: Execute an explicitly activated command for session data; widget identifies the selected answer.
 * RU: Выполняет явно выбранную команду сеанса data; widget задаёт выбранный ответ. */
static void
ai_execute (GtkWidget *widget, gpointer data)
{
    (void)widget;
    ShellSession *s = data;
    if (!s->process)
    {
        session_status (s, "Connect this SSH session first");
        return;
    }
    gchar *command = insight_ai_suggested_command (s->chat);
    if (!command)
    {
        session_status (s, "Select a complete AI answer with a valid suggested command first");
        return;
    }
    gchar *line = g_strconcat (command, "\r", NULL);
    terminal_write (line, strlen (line), s);
    g_free (line);
    g_free (command);
    session_status (s, "Reviewed AI command sent to this SSH session");
}
/* EN: Release the AI controller reference for session data; controller identifies the closing window.
 * RU: Освобождает ссылку помощника сеанса data; controller задаёт закрываемое окно. */
static void
ai_closed (GtkWidget *controller, gpointer data)
{
    ShellSession *s = data;
    if (s->chat == controller)
    {
        g_object_remove_weak_pointer (G_OBJECT (controller), (gpointer *)&s->chat);
        s->chat = NULL;
    }
}
/* EN: Open SSH assistance for session data or the selected session; widget is unused.
 * RU: Открывает помощник SSH для сеанса data либо выбранного; widget не используется. */
static void
ai_console (GtkWidget *widget, gpointer data)
{
    (void)widget;
    ShellSession *s = data ? data : remote_ui.selected;
    if (!s)
        return;
    gchar *context = session_context (s);
    GtkWidget *window = insight_terminal_ui_assistant (
        "terminal-ssh", "SSH assistant — connection, transfer, errors and local code", context, s->output,
        ai_insert, s);
    g_free (context);
    if (s->chat != window)
    {
        s->chat = window;
        g_object_add_weak_pointer (G_OBJECT (window), (gpointer *)&s->chat);
        g_signal_connect (window, "destroy", G_CALLBACK (ai_closed), s);
        g_object_set_data_full (G_OBJECT (window), "insight-ssh-session-owner", session_ref (s),
                                session_unref);
        GtkWidget *box = g_object_get_data (G_OBJECT (window), "insight-ai-actions-page"),
                  *run = gtk_button_new_with_label ("Run reviewed command in this SSH session");
        gtk_widget_set_name (run, "insight-ssh-ai-run");
        gtk_box_pack_start (GTK_BOX (box), run, FALSE, FALSE, 0);
        g_signal_connect (run, "clicked", G_CALLBACK (ai_execute), s);
        gtk_widget_show (run);
    }
}
/* EN: Append page with visible label and tooltip hint to the SSH notebook.
 * RU: Добавляет page с подписью label и подсказкой hint во вкладки SSH. */
static void
ssh_tab (GtkWidget *page, const gchar *label, const gchar *hint)
{
    guint number = 0;
    guint kind = !strcmp (label, "Connection") ? 0 : !strcmp (label, "Transfer") ? 1 : 2;
    if (kind == 2)
        sscanf (label, "SSH %u", &number);
    gchar *tooltip = kind == 2 ? g_strdup_printf ("SSH terminal %u — %s", number, hint) : g_strdup (hint);
    GtkWidget *tab = insight_terminal_tab (kind, number, tooltip);
    g_free (tooltip);
    gtk_notebook_append_page (GTK_NOTEBOOK (remote_ui.tabs), page, tab);
}
/* EN: Select the session attached to page; notebook/number/data are unused.
 * RU: Выбирает сеанс страницы page; notebook/number/data не используются. */
static void
session_selected (GtkNotebook *notebook, GtkWidget *page, guint number, gpointer data)
{
    (void)notebook;
    (void)number;
    (void)data;
    ShellSession *s = g_object_get_data (G_OBJECT (page), "insight-ssh-session");
    if (s)
    {
        remote_ui.selected = s;
        remote_ui.output = s->output;
        remote_ui.input = s->input;
    }
}
/* EN: Open the selected session data's file:line in matching local sources; widget is unused.
 * RU: Открывает file:line выбранного сеанса data в локальных исходниках; widget не используется. */
static void
open_diagnostic (GtkWidget *widget, gpointer data)
{
    (void)widget;
    ShellSession *s = data ? data : remote_ui.selected;
    if (!s)
        return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (s->output));
    GtkTextIter a, b;
    if (!gtk_text_buffer_get_selection_bounds (buffer, &a, &b))
    {
        gtk_text_buffer_get_iter_at_mark (buffer, &a, gtk_text_buffer_get_insert (buffer));
        gtk_text_iter_set_line_offset (&a, 0);
        b = a;
        gtk_text_iter_forward_to_line_end (&b);
    }
    gchar *text = gtk_text_buffer_get_text (buffer, &a, &b, FALSE);
    if (strlen (text) > 16384)
    {
        session_status (s, "Select one compiler diagnostic line (up to 16 KiB)");
        g_free (text);
        return;
    }
    GRegex *regex = g_regex_new ("^\\s*(.+?):([0-9]+)(?::[0-9]+)?(?::|\\s)", 0, 0, NULL);
    GMatchInfo *match = NULL;
    gchar *path = NULL, *line = NULL;
    if (g_regex_match (regex, text, 0, &match))
    {
        path = g_match_info_fetch (match, 1);
        line = g_match_info_fetch (match, 2);
    }
    g_match_info_free (match);
    g_regex_unref (regex);
    g_free (text);
    if (!path)
    {
        session_status (s, "Select a file:line[:column] diagnostic to open its local source");
        return;
    }
    g_strstrip (path);
    gchar *local = NULL;
    guint64 number = g_ascii_strtoull (line, NULL, 10);
    g_free (line);
    if (s->remote)
        for (guint i = 0; i < remote_ui.roots->len && !local; i++)
        {
            InsightRemote *r = ((RemoteRoot *)g_ptr_array_index (remote_ui.roots, i))->remote;
            gsize n = strlen (r->remote);
            if (!g_strcmp0 (r->host, s->remote->host) && !g_strcmp0 (r->user, s->remote->user)
                && r->port == s->remote->port && g_str_has_prefix (path, r->remote)
                && (path[n] == '/' || (n == 1 && r->remote[0] == '/')))
            {
                const gchar *relative = path + n;
                while (*relative == '/')
                    relative++;
                gchar *candidate = insight_canonicalize_filename (relative, r->local);
                gsize prefix = strlen (r->local);
                if (g_str_has_prefix (candidate, r->local) && G_IS_DIR_SEPARATOR (candidate[prefix])
                    && g_file_test (candidate, G_FILE_TEST_IS_REGULAR))
                    local = candidate;
                else
                    g_free (candidate);
            }
        }
    if (!local && g_path_is_absolute (path) && g_file_test (path, G_FILE_TEST_IS_REGULAR))
        local = g_strdup (path);
    if (!local && !g_path_is_absolute (path))
    {
        gchar *base = project_get_base_path ();
        GeanyDocument *current = document_get_current ();
        if (!base && current && current->file_name)
            base = g_path_get_dirname (current->file_name);
        if (base)
        {
            gchar *candidate = insight_canonicalize_filename (path, base);
            if (g_file_test (candidate, G_FILE_TEST_IS_REGULAR))
                local = candidate;
            else
                g_free (candidate);
            g_free (base);
        }
    }
    if (local && number > 0 && number <= G_MAXINT)
    {
        GeanyDocument *old = document_get_current (), *doc = document_open_file (local, FALSE, NULL, NULL);
        if (doc)
            navqueue_goto_line (old, doc, (gint)number);
    }
    else
        session_status (s, "No existing local source matches this diagnostic. Open the remote project via "
                           "SFTP or select the corresponding local project.");
    g_free (local);
    g_free (path);
}
/* EN: Add an independent SSH terminal up to the tab limit; widget/data are unused.
 * RU: Добавляет независимый SSH-терминал до предела вкладок; widget/data не используются. */
static void
session_add (GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    if (remote_ui.sessions->len >= 16)
    {
        status ("Up to 16 SSH terminals can be open");
        return;
    }
    ShellSession *s = g_new0 (ShellSession, 1);
    s->refs = 1;
    s->number = remote_ui.sessions->len + 1;
    s->page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    g_object_ref_sink (s->page);
    g_object_set_data (G_OBJECT (s->page), "insight-ssh-session", s);
    s->stack = gtk_stack_new ();
    gtk_stack_set_homogeneous (GTK_STACK (s->stack), FALSE);
    gtk_stack_set_transition_type (GTK_STACK (s->stack), GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_box_pack_start (GTK_BOX (s->page), s->stack, TRUE, TRUE, 0);
    GtkWidget *terminal = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    gtk_stack_add_named (GTK_STACK (s->stack), terminal, "terminal");
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    s->output = insight_terminal_new (terminal_write, s);
    insight_terminal_set_resize (s->output, shell_resize, s);
    s->input = s->output;
    gtk_container_add (GTK_CONTAINER (scroll), s->output);
    s->overlay = insight_terminal_ui_overlay (scroll, "insight-ssh-ai", G_CALLBACK (ai_console), s);
    gtk_box_pack_start (GTK_BOX (terminal), s->overlay, TRUE, TRUE, 0);
    gchar *name = g_strdup_printf ("insight-ssh-session-%u", s->number);
    gtk_widget_set_name (s->page, name);
    g_free (name);
    name = s->number == 1 ? g_strdup ("insight-ssh-output")
                          : g_strdup_printf ("insight-ssh-output-%u", s->number);
    gtk_widget_set_name (s->output, name);
    g_free (name);
    g_ptr_array_add (remote_ui.sessions, s);
    name = g_strdup_printf ("SSH %u", s->number);
    ssh_tab (s->page, name, "Independent SSH terminal");
    g_free (name);
    gtk_widget_show_all (s->page);
    gtk_notebook_set_current_page (GTK_NOTEBOOK (remote_ui.tabs),
                                   gtk_notebook_page_num (GTK_NOTEBOOK (remote_ui.tabs), s->page));
}
/* EN: Attach caption label and field to grid at row with an expanding input.
 * RU: Добавляет подпись label и растягиваемое поле field в grid на row. */
static void
form_row (GtkWidget *grid, gint row, const gchar *label, GtkWidget *field)
{
    GtkWidget *title = gtk_label_new (label);
    gtk_label_set_xalign (GTK_LABEL (title), 0);
    gtk_grid_attach (GTK_GRID (grid), title, 0, row, 1, 1);
    gtk_widget_set_hexpand (field, TRUE);
    if (GTK_IS_ENTRY (field))
        gtk_entry_set_width_chars (GTK_ENTRY (field), 1);
    gtk_grid_attach (GTK_GRID (grid), field, 1, row, 1, 1);
}
/* EN: Create and return a scrollable SSH form page with caption label.
 * RU: Создаёт и возвращает прокручиваемую страницу формы SSH с подписью label. */
static GtkWidget *
form_page (const gchar *label)
{
    GtkWidget *body = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4),
              *scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_set_border_width (GTK_CONTAINER (body), 5);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (scroll), body);
    ssh_tab (scroll, label, label);
    return body;
}
/* EN: Add a button with label/name to box and connect cb with data; return GTK-owned widget.
 * RU: Добавляет кнопку label/name в box и связывает cb с data; возвращает виджет GTK. */
static GtkWidget *
form_button (GtkWidget *box, const gchar *label, const gchar *name, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new_with_label (label);
    if (name)
        gtk_widget_set_name (b, name);
    gtk_box_pack_start (GTK_BOX (box), b, FALSE, FALSE, 0);
    g_signal_connect (b, "clicked", cb, data);
    return b;
}
#include "interface.inc"
/* EN: Save preferences and release SSH sessions, transfers and application signal handlers.
 * RU: Сохраняет параметры и освобождает SSH-сеансы, передачи и обработчики приложения. */
void
insight_remote_ui_finalize (void)
{
    insight_serial_finalize ();
    profile_save ();
    remote_ui.closing = TRUE;
    for (guint i = 0; i < 4; i++)
        if (remote_ui.signals[i])
            g_signal_handler_disconnect (geany_object, remote_ui.signals[i]);
    stop_transfer (NULL, NULL);
    g_clear_pointer (&remote_ui.upload_versions, g_hash_table_destroy);
    for (guint i = 0; i < remote_ui.sessions->len; i++)
    {
        ShellSession *s = g_ptr_array_index (remote_ui.sessions, i);
        session_disconnect (s);
        gtk_widget_destroy (s->page);
    }
    g_ptr_array_free (remote_ui.sessions, TRUE);
    g_ptr_array_free (remote_ui.roots, TRUE);
    g_free (remote_ui.config);
    g_free (remote_ui.directory);
}
