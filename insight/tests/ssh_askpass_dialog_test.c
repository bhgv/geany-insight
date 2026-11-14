/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Drive the actual password and host-key confirmation dialogs, including native Windows DISPLAY handling.
 * RU: Проверяет настоящие диалоги пароля и доверия ключу сервера, включая DISPLAY в Windows. */
#define main production_askpass_main
#include "../../src/insight-askpass.c"
#undef main

static gboolean answered;
static guint attempts;

/* EN: Find the dialog's entry below widget, returning a borrowed pointer or NULL.
 * RU: Ищет поле диалога в widget, возвращая заимствованный указатель либо NULL. */
static GtkWidget *find_entry(GtkWidget *widget)
{
    if (GTK_IS_ENTRY(widget))
        return widget;
    if (!GTK_IS_CONTAINER(widget))
        return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget *entry = NULL;
    for (GList *item = children; item && !entry; item = item->next)
        entry = find_entry(item->data);
    g_list_free(children);
    return entry;
}

/* EN: Fill a visible password field or accept host-key trust; data is unused and a missing dialog times out.
 * RU: Заполняет видимое поле пароля либо подтверждает доверие; data не используется, отсутствие диалога даёт тайм-аут. */
static gboolean answer_dialog(gpointer data)
{
    (void)data;
    g_assert_cmpuint(++attempts, <, 50);
    GList *windows = gtk_window_list_toplevels();
    for (GList *item = windows; item; item = item->next)
        if (GTK_IS_DIALOG(item->data) && gtk_widget_get_visible(item->data))
        {
            GtkWidget *entry = find_entry(item->data);
            g_assert_nonnull(entry);
            if (gtk_widget_get_visible(entry))
            {
                g_assert_false(gtk_entry_get_visibility(GTK_ENTRY(entry)));
                gtk_entry_set_text(GTK_ENTRY(entry), "window-пароль");
            }
            answered = TRUE;
            gtk_dialog_response(GTK_DIALOG(item->data), GTK_RESPONSE_ACCEPT);
            g_list_free(windows);
            return G_SOURCE_REMOVE;
        }
    g_list_free(windows);
    return G_SOURCE_CONTINUE;
}

/* EN: Execute production askpass with prompt argv[1] and private test credentials; argc validates the invocation.
 * RU: Выполняет настоящий askpass с запросом argv[1] и закрытыми данными теста; argc проверяет вызов. */
int main(int argc, char **argv)
{
    g_assert_cmpint(argc, ==, 2);
    gchar *directory = g_dir_make_tmp("insight dialog тест-XXXXXX", NULL);
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    gchar *session = insight_ssh_auth_prepare(launcher, directory, "fixture", "user", 22, "", NULL, TRUE, NULL);
    const gchar *names[] = {"GEANY_INSIGHT_SSH_CONFIG", "GEANY_INSIGHT_SSH_TARGET",
        "GEANY_INSIGHT_SSH_AUTH_SESSION", "GEANY_INSIGHT_SSH_SAVE_PASSWORD"};
    for (guint i = 0; i < G_N_ELEMENTS(names); i++)
        g_setenv(names[i], g_subprocess_launcher_getenv(launcher, names[i]), TRUE);
#ifdef G_OS_WIN32
    g_setenv("DISPLAY", ":0", TRUE);
#endif
    g_timeout_add(100, answer_dialog, NULL);
    gint result = production_askpass_main(argc, argv);
    g_assert_cmpint(result, ==, 0);
    g_assert_true(answered);
    gchar *path = insight_ssh_auth_path(directory);
    gchar *target = insight_ssh_auth_target("fixture", "user", 22);
    gchar *password = insight_remote_password_load(path, target, NULL);
    if (strstr(argv[1], "yes/no"))
        g_assert_null(password);
    else
        g_assert_cmpstr(password, ==, "window-пароль");
    insight_ssh_auth_cleanup(session);
    g_remove(path);
    gchar *folder = g_path_get_dirname(path);
    g_rmdir(folder);
    g_rmdir(directory);
    g_free(folder);
    g_free(password);
    g_free(target);
    g_free(path);
    g_free(session);
    g_free(directory);
    g_object_unref(launcher);
    return 0;
}
