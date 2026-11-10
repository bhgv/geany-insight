/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Observer preferences and version history.
 * RU: Настройки наблюдателей и история версий. */

/* Use the same project or entry-file scope as the breakpoint settings. */
/* Использует ту же привязку к проекту или точке входа, что и точки останова. */
static gchar *debug_observer_settings_path(const ScriptDebugWindow *view)
{
    if (!view->breakpoint_settings_path)
        return NULL;

    gchar *name = g_path_get_basename(view->breakpoint_settings_path);
    gchar *path = g_build_filename(app->configdir, "insight", "observers", name, NULL);
    g_free(name);
    return path;
}

/* Read the saved draft into the dialog without activating its condition or script. */
/* Читает сохранённый черновик в диалог без включения условия или сценария. */
static void debug_read_observer_settings(const ScriptDebugWindow *view,
                                         gchar **condition, gchar **source, gchar **filename)
{
    gchar *path = debug_observer_settings_path(view);
    if (!path)
        return;

    gsize length = 0;
    gchar *contents = debug_read_source_file(path, &length);
    g_free(path);
    if (!contents)
        return;

    GKeyFile *settings = g_key_file_new();
    if (g_key_file_load_from_data(settings, contents, length, G_KEY_FILE_NONE, NULL))
    {
        gchar *saved_condition = g_key_file_get_string(settings, "Observer", "Condition", NULL);
        gchar *saved_source = g_key_file_get_string(settings, "Observer", "Source", NULL);
        gchar *saved_file = g_key_file_get_string(settings, "Observer", "File", NULL);
        if (saved_condition && g_utf8_strlen(saved_condition, -1) <= 1024)
            *condition = g_steal_pointer(&saved_condition);
        if (saved_source && strlen(saved_source) <= 64 * 1024)
            *source = g_steal_pointer(&saved_source);
        if (saved_file)
        {
            gchar *local_file = g_filename_from_utf8(saved_file, -1, NULL, NULL, NULL);
            if (local_file && g_path_is_absolute(local_file))
                *filename = g_steal_pointer(&local_file);
            g_free(local_file);
        }
        g_free(saved_condition);
        g_free(saved_source);
        g_free(saved_file);
    }
    g_key_file_unref(settings);
    g_free(contents);
}

/* Store the last applied text as a private draft for later sessions. */
/* Сохраняет последний применённый текст как личный черновик для следующих сеансов. */
static void debug_save_observer_settings(const ScriptDebugWindow *view)
{
    gchar *path = debug_observer_settings_path(view);
    if (!path)
        return;

    GKeyFile *settings = g_key_file_new();
    g_key_file_set_string(settings, "Observer", "Condition", view->stop_condition);
    g_key_file_set_string(settings, "Observer", "Source", view->observer_source);
    if (view->observer_filename)
    {
        gchar *utf8 = g_filename_to_utf8(view->observer_filename, -1, NULL, NULL, NULL);
        if (utf8)
            g_key_file_set_string(settings, "Observer", "File", utf8);
        g_free(utf8);
    }
    gchar *directory = g_path_get_dirname(path);
    GError *error = NULL;
    if (g_mkdir_with_parents(directory, 0700) == 0)
    {
        gsize length;
        gchar *contents = g_key_file_to_data(settings, &length, NULL);
        GFile *file = g_file_new_for_path(path);
        g_file_replace_contents(file, contents, length, NULL, FALSE, G_FILE_CREATE_PRIVATE,
                                NULL, NULL, &error);
        g_object_unref(file);
        g_free(contents);
    }
    else
        g_set_error_literal(&error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                            "Cannot create debugger observer settings directory");
    if (error)
        ui_set_statusbar(TRUE, "Cannot save debugger observer draft: %s", error->message);
    g_clear_error(&error);
    g_free(directory);
    g_key_file_unref(settings);
    g_free(path);
}

/* Locate the private Git repository for this Geany project. */
/* Находит отдельный Git-репозиторий истории для проекта Geany. */
static gchar *debug_observer_git_path(const ScriptDebugWindow *view)
{
    if (!view->project_filename)
        return NULL;

    gchar *identity = debug_file_key(view->project_filename);
    gchar *scope = g_compute_checksum_for_string(G_CHECKSUM_SHA256, identity, -1);
    gchar *path = g_build_filename(app->configdir, "insight", "observer-history", scope, NULL);
    g_free(scope);
    g_free(identity);
    return path;
}

/* Run Git with explicit repository paths and arguments, without a shell. */
/* Запускает Git с явным путём репозитория и аргументами без командной оболочки. */
static gboolean debug_observer_git(const gchar *directory, const gchar *const arguments[],
                                   gchar **output, GError **error)
{
    gchar *git_dir = g_build_filename(directory, ".git", NULL);
    gchar *argv[24] = {"git", "--git-dir", git_dir, "--work-tree", (gchar *)directory,
        "-c", "user.name=Geany Insight", "-c", "user.email=debugger@geany-insight.local",
        "-c", "commit.gpgsign=false", "-c", "core.hooksPath="};
    guint count = 0;
    while (argv[count])
        count++;
    for (guint i = 0; arguments[i] && count + 1 < G_N_ELEMENTS(argv); i++)
        argv[count++] = (gchar *)arguments[i];

    gchar **environment = g_get_environ();
    const gchar *variables[] = {"GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR",
        "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES", NULL};
    for (guint i = 0; variables[i]; i++)
        environment = g_environ_unsetenv(environment, variables[i]);
    gint status = 0;
    gchar *diagnostic = NULL;
    gboolean ok = g_spawn_sync(directory, argv, environment, G_SPAWN_SEARCH_PATH,
                               NULL, NULL, output, &diagnostic, &status, error);
    if (ok && status != 0)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            diagnostic && *diagnostic ? diagnostic : "Git command failed");
        ok = FALSE;
    }
    g_strfreev(environment);
    g_free(diagnostic);
    g_free(git_dir);
    return ok;
}

/* Commit a condition and script together, keeping the project worktree separate. */
/* Коммитит условие и сценарий вместе в отдельной от проекта истории. */
static gboolean debug_commit_observer_draft(const ScriptDebugWindow *view, GHashTable *points,
                                            const gchar *directory, const gchar *name,
                                            const gchar *condition, const gchar *source,
                                            const gchar *message, GError **error)
{
    if (strlen(source) > 64 * 1024)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Observer script exceeds 64 KiB.");
        return FALSE;
    }
    if (g_mkdir_with_parents(directory, 0700) != 0)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot create observer history directory.");
        return FALSE;
    }

    gchar *git_dir = g_build_filename(directory, ".git", NULL);
    gboolean initialized = g_file_test(git_dir, G_FILE_TEST_IS_DIR);
    g_free(git_dir);
    const gchar *init[] = {"init", "--quiet", "--template=", NULL};
    if (!initialized && !debug_observer_git(directory, init, NULL, error))
        return FALSE;

    GKeyFile *settings = g_key_file_new();
    g_key_file_set_string(settings, "Observer", "Condition", condition);
    g_key_file_set_string(settings, "Observer", "Source", source);
    debug_write_conditional_set(view, settings, points);
    gsize length;
    gchar *contents = g_key_file_to_data(settings, &length, NULL);
    gchar *path = g_build_filename(directory, name, NULL);
    GFile *file = g_file_new_for_path(path);
    gboolean ok = g_file_replace_contents(file, contents, length, NULL, FALSE, G_FILE_CREATE_PRIVATE,
                                          NULL, NULL, error);
    g_object_unref(file);
    g_free(path);
    g_free(contents);
    g_key_file_unref(settings);
    if (!ok)
        return FALSE;

    const gchar *status_args[] = {"status", "--porcelain", "--", name, NULL};
    gchar *status = NULL;
    ok = debug_observer_git(directory, status_args, &status, error);
    if (ok && (!status || !*status))
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "This draft is already the latest version.");
        ok = FALSE;
    }
    g_free(status);
    const gchar *add[] = {"add", "--", name, NULL};
    const gchar *commit[] = {"commit", "--quiet", "--only", "-m", message, "--", name, NULL};
    return ok && debug_observer_git(directory, add, NULL, error) &&
        debug_observer_git(directory, commit, NULL, error);
}

/* Select a revision for this language and read its committed draft. */
/* Выбирает версию для текущего языка и читает черновик из коммита. */
static GKeyFile *debug_choose_observer_revision(GtkWindow *parent, const gchar *directory,
                                                const gchar *name, GError **error)
{
    gchar *git_dir = g_build_filename(directory, ".git", NULL);
    gboolean initialized = g_file_test(git_dir, G_FILE_TEST_IS_DIR);
    g_free(git_dir);
    if (!initialized)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No observer commits for this project.");
        return NULL;
    }

    const gchar *history[] = {"log", "--format=%H%x09%s", "-n", "100", "--", name, NULL};
    gchar *output = NULL;
    if (!debug_observer_git(directory, history, &output, error))
    {
        g_free(output);
        return NULL;
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Observer Git History", parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Load Draft", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *choices = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), choices, TRUE, TRUE, 8);
    gchar **lines = g_strsplit(output, "\n", -1);
    guint count = 0;
    for (guint i = 0; lines[i]; i++)
    {
        gchar *separator = strchr(lines[i], '\t');
        if (!separator)
            continue;
        *separator = 0;
        gchar *label = g_strdup_printf("%.10s  %s", lines[i], separator + 1);
        gchar *valid = g_utf8_make_valid(label, -1);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(choices), lines[i], valid);
        g_free(valid);
        g_free(label);
        count++;
    }
    g_strfreev(lines);
    g_free(output);
    GKeyFile *settings = NULL;
    if (!count)
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No observer commits for this language.");
    else
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(choices), 0);
        gtk_widget_show_all(dialog);
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
        {
            const gchar *revision = gtk_combo_box_get_active_id(GTK_COMBO_BOX(choices));
            gchar *object = g_strdup_printf("%s:%s", revision, name);
            const gchar *show[] = {"show", object, NULL};
            gchar *contents = NULL;
            if (debug_observer_git(directory, show, &contents, error))
            {
                settings = g_key_file_new();
                if (!g_key_file_load_from_data(settings, contents, -1, G_KEY_FILE_NONE, error))
                    g_clear_pointer(&settings, g_key_file_unref);
            }
            g_free(contents);
            g_free(object);
        }
    }
    gtk_widget_destroy(dialog);
    return settings;
}
