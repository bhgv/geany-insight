/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Native saved external tools, preview, process output and diagnostic source links.
 * RU: Пользовательские внешние инструменты, предпросмотр, вывод процессов и ссылки диагностики. */
#include "geany.h"
#include "insight-custom-commands.h"
#include "app.h"
#include "document.h"
#include "project.h"
#include "main.h"
#include "keybindings.h"
#include "navqueue.h"
#include "highlighting.h"
#include "filetypes.h"
#include "insight-completion.h"
#include "../insight/desktop/text_transfer.h"
#include <gdk/gdkkeysyms.h>
#include "sciwrappers.h"
#include "ui_utils.h"
#include "insight-compare-ui.h"
#include "insight-help.h"
#include "../insight/desktop/custom_command.h"
#include "../insight/desktop/project_files.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define OUTPUT_LIMIT (4u*1024u*1024u)
typedef struct OutputTarget OutputTarget;
typedef struct {
 gint refs;gboolean closed,loading,reading,waiting,limited,stopped;
 GtkWidget *window,*tabs,*profiles,*name,*command,*directory,*run,*stop,*status,*context,*output,*title,*arguments[9],*links_view,*links_status;
 GtkListStore *links;gint output_start;gchar *launch_directory;
 GPtrArray *commands,*locked;gchar *path,*status_key,*context_argv,*context_directory,*context_variables,*context_error;gint exit_code;
 OutputTarget *target;
 GByteArray *stdout_bytes;
 GtkWidget *stdout_open, *stdout_review;
 GtkWidget *diagnostic_pattern, *parse_on_finish;
 GRegex *launch_regex;
 gboolean launch_parse;
 gboolean stdout_lost;
 guint readers, output_serial;
 InsightCustomSelection *selection;
 GSubprocess *process;GCancellable *cancel;gsize received;guint lines,line_bytes,follow_idle;gdouble follow_baseline;
} CommandView;
enum {LINK_FILE,LINK_LINE,LINK_COLUMN,LINK_MESSAGE,LINK_FIELDS};
static void link_count(CommandView *);
static void parse_links(GtkWidget *, gpointer);
static void parse_links_using(CommandView *, GRegex *, gboolean);
static GtkWidget *command_window;
#include "insight-custom-commands/output-target.inc"
static CommandView *view_ref(CommandView *v) {v->refs++;return v;}
/* EN: Release retained view data; final ownership also removes any leftover selection snapshot.
 * RU: Освобождает удерживаемые данные data; последний владелец также удаляет оставшийся снимок выделения. */
static void view_unref(gpointer data)
{
    CommandView *v = data;
    if (--v->refs)
        return;
    g_ptr_array_unref(v->commands);
    g_ptr_array_unref(v->locked);
    g_free(v->path);
    g_free(v->status_key);
    g_free(v->context_argv);
    g_free(v->context_directory);
    g_free(v->context_variables);
    g_free(v->context_error);
    g_free(v->launch_directory);
    insight_custom_selection_free(v->selection);
    g_clear_object(&v->process);
    g_clear_object(&v->cancel);
    output_target_unref(v->target);
    g_clear_pointer(&v->stdout_bytes, g_byte_array_unref);
    g_clear_pointer(&v->launch_regex, g_regex_unref);
    g_free(v);
}
/* EN: Retain status key for v and repaint the stable selectable status label; closed views retain state only.
 * RU: Сохраняет ключ key для v и обновляет стабильную выделяемую строку статуса; закрытые окна сохраняют лишь состояние. */
static void note(CommandView *v, const gchar *key)
{
    g_free(v->status_key);
    v->status_key = g_strdup(key);
    if (v->closed)
        return;
    gchar *formatted = !strcmp(key, "Command exited with status %d") ?
        g_strdup_printf(insight_tr(key), v->exit_code) : g_strdup(insight_tr(key));
    gtk_widget_set_name(v->status, "insight-custom-command-status");
    gtk_label_set_text(GTK_LABEL(v->status), formatted);
    g_free(formatted);
}
/* EN: Update command and stdout controls for v; closed windows are untouched.
 * RU: Обновляет команды и действия stdout для v; закрытые окна не затрагиваются. */
static void controls(CommandView *v)
{
    if (v->closed)
        return;
    gboolean busy = v->process != NULL;
    for (guint i = 0; i < v->locked->len; i++)
        gtk_widget_set_sensitive(g_ptr_array_index(v->locked, i), !busy);
    gtk_widget_set_sensitive(v->run, !busy);
    gtk_widget_set_sensitive(v->stop, busy && !v->stopped);
    if (v->stdout_open)
        gtk_widget_set_sensitive(v->stdout_open, output_ready(v));
    if (v->stdout_review)
        gtk_widget_set_sensitive(v->stdout_review, output_ready(v) && v->target != NULL);
    g_object_set_data(G_OBJECT(v->window), "insight-custom-command-busy", GINT_TO_POINTER(busy));
}
static void cancel_follow(CommandView *v) {if(v->follow_idle) {guint id=v->follow_idle;v->follow_idle=0;g_source_remove(id);}}
static void stop_scroll(GtkWidget *view) {GtkAdjustment *a=gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));if(a) gtk_adjustment_set_value(a,gtk_adjustment_get_value(a));}
static void destroyed(GtkWidget *widget,gpointer data) {
 CommandView *v=data;if(command_window==widget) command_window=NULL;v->closed=TRUE;cancel_follow(v);if(v->process) g_subprocess_force_exit(v->process);if(v->cancel) g_cancellable_cancel(v->cancel);
}
static void value(GHashTable *map,const gchar *key,const gchar *text) {if(text && g_utf8_validate(text,-1,NULL) && strlen(text)<=16384 && !strpbrk(text,"\r\n")) g_hash_table_replace(map,g_strdup(key),g_strdup(text));}
static GHashTable *context_values(void) {
 GHashTable *map=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);GeanyDocument *doc=document_get_current();
 const gchar *file=DOC_VALID(doc) ? doc->file_name:NULL;gchar *dir=file ? g_path_get_dirname(file):g_strdup(g_get_home_dir());
 const gchar *project=app->project ? app->project->file_name:NULL;gchar *root=app->project ? project_get_base_path():g_strdup(dir);
 if(!root) root=g_strdup(dir);
 value(map,"j",root);value(map,"PROJECT_SOURCE_DIR",root);value(map,"d",dir);
 if(file) {gchar *name=g_path_get_basename(file),*stem=g_strdup(name),*dot=strrchr(stem,'.');value(map,"f",file);value(map,"n",name);if(dot && dot!=stem) {*dot=0;value(map,"e",dot+1);}else value(map,"e","");value(map,"b",stem);
  GFile *base=g_file_new_for_path(root),*source=g_file_new_for_path(file);gchar *relative=g_file_get_relative_path(base,source);value(map,"r",relative ? relative:file);g_free(relative);g_object_unref(base);g_object_unref(source);g_free(name);g_free(stem);}
 if(project) {gchar *pdir=g_path_get_dirname(project),*name=g_path_get_basename(project),*dot=strrchr(name,'.');if(dot && dot!=name) *dot=0;value(map,"J",pdir);value(map,"o",name);value(map,"PROJECT_DATA_DIR",pdir);value(map,"PROJECT_NAME",name);gchar *project_stem=g_build_filename(pdir,name,NULL);value(map,"p",project_stem);g_free(project_stem);g_free(name);g_free(pdir);}
 if(DOC_VALID(doc)) {ScintillaObject *s=doc->editor->sci;gint pos=SEND(s,SCI_GETCURRENTPOS,0,0),start=SEND(s,SCI_GETSELECTIONSTART,0,0),end=SEND(s,SCI_GETSELECTIONEND,0,0);gchar *line=g_strdup_printf("%d",sci_get_line_from_position(s,pos)+1);value(map,"l",line);g_free(line);
  if(start==end) {start=SEND(s,SCI_WORDSTARTPOSITION,pos,TRUE);end=SEND(s,SCI_WORDENDPOSITION,pos,TRUE);}if(end-start<=4096 && SEND(s,SCI_GETSELECTIONS,0,0)==1 && SEND(s,SCI_GETSELECTIONMODE,0,0)==SC_SEL_STREAM && !SEND(s,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0) && !SEND(s,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0)) {gchar *word=sci_get_contents_range(s,start,end);if(strlen(word)==(gsize)(end-start) && g_utf8_validate(word,-1,NULL)) {gchar *first=word;while(*first && g_unichar_isspace(g_utf8_get_char(first))) first=g_utf8_next_char(first);gchar *last=first;while(*last && !g_unichar_isspace(g_utf8_get_char(last))) last=g_utf8_next_char(last);*last=0;value(map,"w",first);}g_free(word);}}
 GDateTime *now=g_date_time_new_now_local();gchar *date=g_date_time_format(now,"%Y-%m-%d"),*time=g_date_time_format(now,"%H:%M:%S");value(map,"a",date);value(map,"t",time);g_free(date);g_free(time);g_date_time_unref(now);
 const gchar *names[]={"APPDATA_DIR","LOCAL_APPDATA_DIR","DESKTOP_DIR","MYDOCUMENTS_DIR","PROGRAM_DIR","TEMP_DIR","PROGRAMFILES_DIR","PROGRAMFILESX86_DIR","WINDOWS_DIR"};
 for(guint i=0;i<G_N_ELEMENTS(names);i++) {gchar *path=insight_project_path_variable(names[i],root,project,NULL);value(map,names[i],path);g_free(path);}
 g_free(root);g_free(dir);return map;
}
#include "insight-custom-commands/selection.inc"
#include "insight-custom-commands/files.inc"

/* EN: Quote borrowed argv for display only; return owned complete text bounded by the launch argument limits.
 * RU: Экранирует заимствованный argv только для показа; возвращает полный собственный текст в пределах границ запуска. */
static gchar *display_argv(gchar **argv)
{
    GString *text = g_string_new(NULL);
    for (guint i = 0; argv && argv[i]; i++)
    {
        gchar *quoted = g_shell_quote(argv[i]);
        if (i)
            g_string_append_c(text, ' ');
        g_string_append(text, quoted);
        g_free(quoted);
    }
    return g_string_free(text, FALSE);
}
/* EN: Repaint v's captured preview and bulk help, without resampling files or starting processes; ignore closed views.
 * RU: Перерисовывает сохранённый предпросмотр v и справку без повторного сбора файлов или запуска; закрытые окна пропускаются. */
static void render_context(CommandView *v)
{
    if (v->closed || !v->context_variables)
        return;
    GString *text = g_string_new(insight_tr("Expanded arguments (display only):"));
    g_string_append_c(text, '\n');
    g_string_append(text, v->context_argv ? v->context_argv :
        insight_tr(v->context_error ? v->context_error : "Invalid custom command template"));
    if (v->context_directory)
        g_string_append_printf(text, "\n\n%s\n%s", insight_tr("Working directory"), v->context_directory);
    g_string_append_printf(text, "\n\n%s\n%s", insight_tr("Available context variables:"), v->context_variables);
    g_string_append_printf(text, "\n%s\n", insight_tr("Bulk file arguments: %f%o = named open files; %f%m = named modified files"));
    g_string_append(text, insight_tr("Also supports %r, %n, %b and %e. Files are read from disk; buffers are not saved."));
    g_string_append_printf(text, "\n%s", insight_tr("Selection snapshot: %s is UTF-8. Preview creates no file; Run captures the current selection."));
    stop_scroll(v->context);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->context)), text->str, -1);
    g_string_free(text, TRUE);
}
/* EN: Capture v's preview only, releasing any selection plan without materializing it; button is unused.
 * RU: Фиксирует только предпросмотр v, освобождая план выделения без создания файла; button не используется. */
static void preview(GtkWidget *button, gpointer data)
{
    (void)button;
    CommandView *v = data;
    gchar **argv = NULL;
    gchar *directory = NULL;
    GHashTable *map = NULL;
    InsightCustomSelection *selection = NULL;
    GRegex *regex = NULL;
    GError *error = NULL;
    gboolean ok = prepare(v, &argv, &directory, &map, &selection, &regex, &error);
    g_free(v->context_argv);
    g_free(v->context_directory);
    g_free(v->context_variables);
    g_free(v->context_error);
    v->context_argv = ok ? display_argv(argv) : NULL;
    v->context_directory = ok ? g_filename_display_name(directory) : NULL;
    v->context_error = ok ? NULL : g_strdup(error ? error->message : "Invalid custom command template");
    GString *variables = g_string_new(NULL);
    GList *keys = g_hash_table_get_keys(map);
    keys = g_list_sort(keys, (GCompareFunc)g_strcmp0);
    for (GList *p = keys; p; p = p->next)
    {
        const gchar *key = p->data;
        g_string_append_printf(variables, strlen(key) == 1 ? "%%%s = %s\n" : "%%%s%% = %s\n",
            key, (gchar *)g_hash_table_lookup(map, key));
    }
    g_list_free(keys);
    v->context_variables = g_string_free(variables, FALSE);
    render_context(v);
    insight_compare_ui_reveal(v->context);
    insight_custom_selection_free(selection);
    if (regex)
        g_regex_unref(regex);
    g_hash_table_unref(map);
    g_strfreev(argv);
    g_free(directory);
    g_clear_error(&error);
}
static gboolean follow_output(gpointer data) {
 CommandView *v=data;v->follow_idle=0;if(v->closed) return G_SOURCE_REMOVE;
 GtkAdjustment *a=gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(v->output));
 /* Preserve a manual scroll made since this follow request was queued. */
 if(ABS(gtk_adjustment_get_value(a)-v->follow_baseline)<1)
  gtk_adjustment_set_value(a,MAX(gtk_adjustment_get_lower(a),gtk_adjustment_get_upper(a)-gtk_adjustment_get_page_size(a)));
 return G_SOURCE_REMOVE;
}
static void append(CommandView *v,const gchar *text,gint length) {
 if(v->closed) return;GtkAdjustment *a=gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(v->output));
 gboolean follow=gtk_adjustment_get_value(a)+gtk_adjustment_get_page_size(a)>=gtk_adjustment_get_upper(a)-16;
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->output));GtkTextIter end;gtk_text_buffer_get_end_iter(buffer,&end);gtk_text_buffer_insert(buffer,&end,text,length);
 /* Coalesce after GTK layout, without animated scroll-to-mark requests. GTK 3
  * animation teardown can reenter adjustment callbacks while unmapping after
  * replacing a large log. Direct updates have no pending animation to finish. */
 if(follow && !v->follow_idle) {v->follow_baseline=gtk_adjustment_get_value(a);v->follow_idle=g_idle_add_full(G_PRIORITY_LOW,follow_output,view_ref(v),view_unref);}
}
/* EN: Finish v only after both child wait and output drain complete; remove its snapshot even after window closure.
 * RU: Завершает v лишь после ожидания процесса и чтения вывода; удаляет снимок и после закрытия окна. */
static void finished(CommandView *v)
{
    if (v->reading || v->waiting)
        return;
    g_clear_pointer(&v->selection, insight_custom_selection_free);
    g_clear_object(&v->process);
    g_clear_object(&v->cancel);
    if (!v->closed)
    {
        if (v->limited)
            note(v, "Output limit reached; command stopped");
        else if (v->stopped)
            note(v, "Command stopped");
        else
            note(v, "Command exited with status %d");
        controls(v);
        if (v->launch_parse && !v->stopped && !v->limited)
            parse_links_using(v, v->launch_regex, FALSE);
    }
}
static void waited(GObject *process,GAsyncResult *result,gpointer data) {
 CommandView *v=data;GError *error=NULL;gboolean ok=g_subprocess_wait_finish(G_SUBPROCESS(process),result,&error);v->exit_code=ok && g_subprocess_get_if_exited(G_SUBPROCESS(process)) ? g_subprocess_get_exit_status(G_SUBPROCESS(process)):-1;g_clear_error(&error);v->waiting=FALSE;finished(v);view_unref(v);
}
#include "insight-custom-commands/output-io.inc"

/* EN: Sample current command/context, materialize its optional selection, and launch one process; button is unused.
 * data is the retained view. Failed validation/write/spawn releases the plan; successful runs own it until finished.
 * RU: Фиксирует команду/контекст, создаёт необязательный снимок и запускает один процесс; button не используется.
 * data — удерживаемое окно. Ошибка проверки/записи/запуска освобождает план; успешный запуск владеет им до finished.
 */
static void run(GtkWidget *button, gpointer data)
{
    (void)button;
    CommandView *v = data;
    if (v->process)
        return;
    output_reset(v);
    controls(v);
    gchar **argv = NULL;
    gchar *directory = NULL;
    GHashTable *map = NULL;
    InsightCustomSelection *selection = NULL;
    GRegex *regex = NULL;
    GError *error = NULL;
    if (!prepare(v, &argv, &directory, &map, &selection, &regex, &error))
    {
        note(v, error ? error->message : "Invalid custom command template");
        goto done;
    }
    if (selection && !insight_custom_selection_create(selection, &error))
    {
        note(v, error->message);
        goto done;
    }
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    g_subprocess_launcher_set_cwd(launcher, directory);
    v->process = g_subprocess_launcher_spawnv(launcher, (const gchar *const *)argv, &error);
    g_object_unref(launcher);
    if (!v->process)
    {
        note(v, error->message);
        goto done;
    }
    g_clear_pointer(&v->launch_regex, g_regex_unref);
    v->launch_regex = regex;
    regex = NULL;
    v->launch_parse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(v->parse_on_finish));
    v->target = output_target_capture();
    v->selection = selection;
    selection = NULL;
    cancel_follow(v);
    stop_scroll(v->output);
    stop_scroll(v->links_view);
    gtk_list_store_clear(v->links);
    link_count(v);
    g_free(v->launch_directory);
    v->launch_directory = g_filename_to_utf8(directory, -1, NULL, NULL, NULL);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->output)), "", 0);
    gchar *shown = display_argv(argv);
    append(v, shown, -1);
    append(v, "\n", 1);
    v->output_start = gtk_text_buffer_get_char_count(gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->output)));
    g_free(shown);
    v->received = 0;
    v->lines = 0;
    v->line_bytes = 0;
    v->limited = FALSE;
    v->stopped = FALSE;
    v->cancel = g_cancellable_new();
    v->readers = 2;
    v->reading = TRUE;
    v->waiting = TRUE;
    note(v, "Command running");
    controls(v);
    g_subprocess_wait_async(v->process, NULL, waited, view_ref(v));
    output_read_start(v, g_subprocess_get_stdout_pipe(v->process), TRUE);
    output_read_start(v, g_subprocess_get_stderr_pipe(v->process), FALSE);
done:
    if (regex)
        g_regex_unref(regex);
    insight_custom_selection_free(selection);
    g_clear_error(&error);
    g_strfreev(argv);
    g_free(directory);
    if (map)
        g_hash_table_unref(map);
}
static void stop(GtkWidget *button,gpointer data) {(void)button;CommandView *v=data;if(v->process && !v->stopped) {v->stopped=TRUE;g_subprocess_force_exit(v->process);g_cancellable_cancel(v->cancel);note(v,"Stopping command");controls(v);}}
static void link_count(CommandView *v) {
 if(v->closed || !v->links_status) return;guint count=gtk_tree_model_iter_n_children(GTK_TREE_MODEL(v->links),NULL);
 gchar *text=g_strdup_printf(insight_tr("Parsed %u source links (limit 1000)"),count);gtk_label_set_text(GTK_LABEL(v->links_status),text);g_free(text);
}
static void open_link(GtkWidget *button,gpointer data) {
 (void)button;CommandView *v=data;GtkTreeModel *model;GtkTreeIter row;
 if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->links_view)),&model,&row)) {note(v,"Select a source link");return;}
 gchar *path=NULL;guint line=0;gtk_tree_model_get(model,&row,LINK_FILE,&path,LINK_LINE,&line,-1);GError *error=NULL;gchar *native=g_filename_from_utf8(path,-1,NULL,NULL,&error);g_free(path);
 if(!native || !g_file_test(native,G_FILE_TEST_IS_REGULAR)) {note(v,error ? error->message:"Source file is unavailable");g_free(native);g_clear_error(&error);return;}
 GeanyDocument *old=document_get_current();guint old_id=DOC_VALID(old) ? old->id:0;GeanyDocument *doc=document_open_file(native,FALSE,NULL,NULL);g_free(native);old=old_id ? document_find_by_id(old_id):NULL;
 if(DOC_VALID(doc)) {if(line>(guint)sci_get_line_count(doc->editor->sci)) note(v,"Source line is outside the current file");else navqueue_goto_line(old,doc,(gint)line);}
}
static void link_column(GtkTreeViewColumn *column,GtkCellRenderer *cell,GtkTreeModel *model,GtkTreeIter *row,gpointer data) {(void)column;(void)data;guint number=0;gtk_tree_model_get(model,row,LINK_COLUMN,&number,-1);gchar *text=number ? g_strdup_printf("%u",number):g_strdup("");g_object_set(cell,"text",text,NULL);g_free(text);}
static void link_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {(void)view;(void)path;(void)column;open_link(NULL,data);}
static void move_link(CommandView *v,gint direction) {
 gint count=gtk_tree_model_iter_n_children(GTK_TREE_MODEL(v->links),NULL);if(!count) {parse_links(NULL,v);count=gtk_tree_model_iter_n_children(GTK_TREE_MODEL(v->links),NULL);if(count) open_link(NULL,v);return;}
 GtkTreePath *path=NULL;gtk_tree_view_get_cursor(GTK_TREE_VIEW(v->links_view),&path,NULL);gint current=path ? gtk_tree_path_get_indices(path)[0]:(direction<0 ? count: -1);if(path) gtk_tree_path_free(path);
 gint next=direction==0 ? 0:CLAMP(current+direction,0,count-1);path=gtk_tree_path_new_from_indices(next,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(v->links_view),path,NULL,FALSE);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(v->links_view),path,NULL,FALSE,0,0);gtk_tree_path_free(path);open_link(NULL,v);
}
static void link_button(GtkWidget *button,gpointer data) {move_link(data,GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),"link-direction")));}
static gboolean command_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;if(event->keyval!=GDK_KEY_F4 || (event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SUPER_MASK|GDK_META_MASK))) return FALSE;move_link(data,event->state&GDK_SHIFT_MASK ? -1:1);return TRUE;
}
#include "insight-custom-commands/profiles.inc"

/* EN: Handle Clear/Copy/Save log button for view data; Clear also invalidates retained stdout editor actions.
 * RU: Выполняет Clear/Copy/Save журнала из button для окна data; Clear также отменяет действия с сохранённым stdout. */
static void output_action(GtkWidget *button, gpointer data)
{
    CommandView *v = data;
    gint action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "output-action"));
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->output));
    if (action == 0)
    {
        output_reset(v);
        controls(v);
        cancel_follow(v);
        stop_scroll(v->output);
        stop_scroll(v->links_view);
        gtk_list_store_clear(v->links);
        link_count(v);
        v->output_start = 0;
        gtk_text_buffer_set_text(buffer, "", 0);
        return;
    }
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    if (action == 1)
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
    else
    {
        view_ref(v);
        GtkWidget *dialog = gtk_file_chooser_dialog_new(insight_tr("Save command output"), GTK_WINDOW(v->window),
            GTK_FILE_CHOOSER_ACTION_SAVE, insight_tr("Cancel"), GTK_RESPONSE_CANCEL, insight_tr("Save"), GTK_RESPONSE_ACCEPT, NULL);
        g_object_ref_sink(dialog);
        gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
        gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (!v->closed && response == GTK_RESPONSE_ACCEPT)
        {
            gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            GError *error = NULL;
            GFile *file = g_file_new_for_path(path);
            if (!g_file_replace_contents(file, text, strlen(text), NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL, &error))
            {
                note(v, error->message);
                g_clear_error(&error);
            }
            g_object_unref(file);
            g_free(path);
        }
        gtk_widget_destroy(dialog);
        g_object_unref(dialog);
        view_unref(v);
    }
    g_free(text);
}
static GtkWidget *button(GtkWidget *row,const gchar *label,const gchar *name,GCallback action,CommandView *v,gboolean lock) {GtkWidget *b=gtk_button_new_with_label("");gtk_widget_set_name(b,name);insight_i18n_key(G_OBJECT(b),"label",label);gtk_box_pack_start(GTK_BOX(row),b,FALSE,FALSE,0);g_signal_connect(b,"clicked",action,v);if(lock) g_ptr_array_add(v->locked,b);return b;}
static GtkWidget *entry(GtkWidget *page,const gchar *label,const gchar *name,const gchar *tip,CommandView *v) {GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6),*l=gtk_label_new(NULL),*e=gtk_entry_new();insight_i18n_key(G_OBJECT(l),"label",label);gtk_widget_set_name(e,name);insight_ai_ui_entry(e,tip,1);insight_i18n_key(G_OBJECT(e),"tooltip-text",tip);gtk_box_pack_start(GTK_BOX(row),l,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),e,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(page),row,FALSE,FALSE,0);g_ptr_array_add(v->locked,e);return e;}
static GtkWidget *text_view(GtkWidget *page,const gchar *name,gboolean expand) {GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL),*view=gtk_text_view_new();gtk_widget_set_name(view,name);gtk_text_view_set_editable(GTK_TEXT_VIEW(view),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(view),TRUE);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view),GTK_WRAP_WORD_CHAR);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_box_pack_start(GTK_BOX(page),scroll,expand,expand,0);return view;}
#include "insight-custom-commands/diagnostics.inc"

static void locale(gpointer data) {CommandView *v=data;if(!v->closed) {render_context(v);link_count(v);insight_i18n_key(G_OBJECT(v->window),"title","Custom Commands");if(v->title) insight_i18n_key(G_OBJECT(v->title),"label","Custom Commands");}if(!v->closed && v->status_key) {gchar *key=g_strdup(v->status_key);note(v,key);g_free(key);}}
#include "insight-custom-commands/output-review.inc"

/* EN: Create or present the saved command tool window; item/unused are menu callback arguments only.
 * RU: Создаёт либо показывает окно сохранённых команд; item/unused используются только как аргументы меню. */
static void show(GtkMenuItem *item,gpointer unused) {
 (void)item;(void)unused;if(command_window) {gtk_window_present(GTK_WINDOW(command_window));return;}
 CommandView *v=g_new0(CommandView,1);v->refs=1;v->locked=g_ptr_array_new();v->path=g_build_filename(app->configdir,"insight","custom-commands.conf",NULL);GError *error=NULL;v->commands=insight_custom_commands_load(v->path,&error);if(!v->commands) v->commands=g_ptr_array_new_with_free_func(insight_custom_command_free);
 v->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);command_window=v->window;gtk_widget_set_name(v->window,"insight-custom-commands");gtk_window_set_transient_for(GTK_WINDOW(v->window),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(v->window),TRUE);insight_i18n_key(G_OBJECT(v->window),"title","Custom Commands");g_object_set_data_full(G_OBJECT(v->window),"insight-custom-state",v,view_unref);g_signal_connect(v->window,"destroy",G_CALLBACK(destroyed),v);
 GtkWidget *root=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);gtk_container_add(GTK_CONTAINER(v->window),root);v->tabs=insight_compare_ui_tabs(root,"insight-custom-command-tabs");GtkWidget *commands=insight_compare_ui_page(v->tabs,"Commands"),*context=insight_compare_ui_page(v->tabs,"Context"),*output=insight_compare_ui_page(v->tabs,"Output"),*arguments=insight_compare_ui_page(v->tabs,"Arguments"),*diagnostics=insight_compare_ui_page(v->tabs,"Diagnostics");
 GtkWidget *arg_grid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(arg_grid),6);gtk_grid_set_row_spacing(GTK_GRID(arg_grid),3);gtk_box_pack_start(GTK_BOX(arguments),arg_grid,FALSE,FALSE,0);
 for(guint i=0;i<9;i++) {gchar *label=g_strdup_printf("%%%u",i+1),*name=g_strdup_printf("insight-custom-command-argument-%u",i+1);GtkWidget *l=gtk_label_new(label);v->arguments[i]=gtk_entry_new();gtk_widget_set_name(v->arguments[i],name);insight_ai_ui_entry(v->arguments[i],"Value substituted for this numbered command argument",1);insight_i18n_key(G_OBJECT(v->arguments[i]),"tooltip-text","Value substituted for this numbered command argument");gtk_grid_attach(GTK_GRID(arg_grid),l,(i%3)*2,i/3,1,1);gtk_grid_attach(GTK_GRID(arg_grid),v->arguments[i],(i%3)*2+1,i/3,1,1);g_ptr_array_add(v->locked,v->arguments[i]);g_free(label);g_free(name);}
 v->profiles=gtk_combo_box_text_new();gtk_widget_set_name(v->profiles,"insight-custom-command-profiles");gtk_box_pack_start(GTK_BOX(commands),v->profiles,FALSE,FALSE,0);g_ptr_array_add(v->locked,v->profiles);g_signal_connect(v->profiles,"changed",G_CALLBACK(select_profile),v);
 v->name=entry(commands,"Name","insight-custom-command-name","Name of the saved external tool",v);v->command=entry(commands,"Command","insight-custom-command-template","Executable and quoted arguments; context variables expand inside each argument",v);v->directory=entry(commands,"Working directory","insight-custom-command-directory","Absolute working directory; empty uses the current project or file directory",v);
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(commands),row,FALSE,FALSE,0);const gchar *labels[]={"New command","Save command","Delete command","Move up","Move down"};for(guint i=0;i<5;i++) {gchar *name=g_strdup_printf("insight-custom-command-edit-%u",i);GtkWidget *b=button(row,labels[i],name,G_CALLBACK(edit_profile),v,TRUE);g_object_set_data(G_OBJECT(b),"custom-action",GINT_TO_POINTER(i));g_free(name);}
 row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(commands),row,FALSE,FALSE,0);v->run=button(row,"Run","insight-custom-command-run",G_CALLBACK(run),v,FALSE);v->stop=button(row,"Stop","insight-custom-command-stop",G_CALLBACK(stop),v,FALSE);button(row,"Preview command","insight-custom-command-preview",G_CALLBACK(preview),v,FALSE);
 GtkWidget *refresh=gtk_button_new_with_label("");insight_i18n_key(G_OBJECT(refresh),"label","Refresh context");gtk_box_pack_start(GTK_BOX(context),refresh,FALSE,FALSE,0);g_signal_connect(refresh,"clicked",G_CALLBACK(preview),v);v->context=text_view(context,"insight-custom-command-context",FALSE);GtkWidget *sc=gtk_widget_get_parent(v->context);gtk_widget_set_size_request(sc,-1,180);
 row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(output),row,FALSE,FALSE,0);const gchar *outs[]={"Clear output","Copy output","Save command output"};for(guint i=0;i<3;i++) {gchar *name=g_strdup_printf("insight-custom-command-output-%u",i);GtkWidget *b=button(row,outs[i],name,G_CALLBACK(output_action),v,FALSE);g_object_set_data(G_OBJECT(b),"output-action",GINT_TO_POINTER(i));g_free(name);}
 output_editor_buttons(v,output);
 diagnostic_options(v,diagnostics);
 row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(diagnostics),row,FALSE,FALSE,0);button(row,"Parse Source Links","insight-custom-command-parse-links",G_CALLBACK(parse_links),v,FALSE);const gchar *link_labels[]={"First link","Previous link","Next link"};const gint directions[]={0,-1,1};for(guint i=0;i<3;i++) {gchar *name=g_strdup_printf("insight-custom-command-link-%u",i);GtkWidget *b=button(row,link_labels[i],name,G_CALLBACK(link_button),v,FALSE);g_object_set_data(G_OBJECT(b),"link-direction",GINT_TO_POINTER(directions[i]));g_free(name);}button(row,"Open source","insight-custom-command-open-link",G_CALLBACK(open_link),v,FALSE);
 v->links_status=gtk_label_new(NULL);gtk_label_set_xalign(GTK_LABEL(v->links_status),0);g_object_set_data(G_OBJECT(v->links_status),"insight-no-i18n",GINT_TO_POINTER(1));gtk_box_pack_start(GTK_BOX(diagnostics),v->links_status,FALSE,FALSE,0);
 v->links=gtk_list_store_new(LINK_FIELDS,G_TYPE_STRING,G_TYPE_UINT,G_TYPE_UINT,G_TYPE_STRING);v->links_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(v->links));g_object_unref(v->links);gtk_widget_set_name(v->links_view,"insight-custom-command-links");gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(v->links_view),LINK_FILE);gtk_tree_view_set_search_column(GTK_TREE_VIEW(v->links_view),LINK_MESSAGE);
 const gchar *headings[]={"File","Line","Column","Message"};for(guint i=0;i<LINK_FIELDS;i++) {GtkCellRenderer *cell=gtk_cell_renderer_text_new();if(i==LINK_FILE || i==LINK_MESSAGE) g_object_set(cell,"ellipsize",PANGO_ELLIPSIZE_END,"width-chars",1,"max-width-chars",50,NULL);GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes("",cell,"text",i,NULL);insight_i18n_key(G_OBJECT(column),"title",headings[i]);if(i==LINK_COLUMN) gtk_tree_view_column_set_cell_data_func(column,cell,link_column,NULL,NULL);gtk_tree_view_column_set_resizable(column,TRUE);gtk_tree_view_column_set_expand(column,i==LINK_FILE || i==LINK_MESSAGE);gtk_tree_view_column_set_sort_column_id(column,i);gtk_tree_view_append_column(GTK_TREE_VIEW(v->links_view),column);}
 GtkWidget *link_scroll=gtk_scrolled_window_new(NULL,NULL);gtk_widget_set_size_request(link_scroll,-1,160);gtk_container_add(GTK_CONTAINER(link_scroll),v->links_view);gtk_box_pack_start(GTK_BOX(diagnostics),link_scroll,FALSE,FALSE,0);g_signal_connect(v->links_view,"row-activated",G_CALLBACK(link_activated),v);g_signal_connect(v->window,"key-press-event",G_CALLBACK(command_key),v);link_count(v);
 v->status=gtk_label_new(NULL);gtk_label_set_xalign(GTK_LABEL(v->status),0);gtk_label_set_selectable(GTK_LABEL(v->status),TRUE);gtk_label_set_ellipsize(GTK_LABEL(v->status),PANGO_ELLIPSIZE_END);gtk_box_pack_start(GTK_BOX(root),v->status,FALSE,FALSE,0);v->output=text_view(root,"insight-custom-command-output",TRUE);insight_panel_help_notebook(GTK_NOTEBOOK(v->tabs),"custom-commands.md");
 rebuild(v,v->commands->len ? 0:-1);if(!v->commands->len) gtk_entry_set_text(GTK_ENTRY(v->directory),"%j");note(v,error ? error->message:"Ready to run an external tool");g_clear_error(&error);insight_i18n_on_refresh(G_OBJECT(v->window),locale,v);controls(v);insight_ai_ui_window(v->window,"Minimize window","Maximize / restore window","Close window");GtkWidget *header=gtk_window_get_titlebar(GTK_WINDOW(v->window));if(GTK_IS_HEADER_BAR(header)) {v->title=gtk_label_new(NULL);gtk_widget_set_name(v->title,"insight-custom-command-title");gtk_style_context_add_class(gtk_widget_get_style_context(v->title),"title");gtk_label_set_ellipsize(GTK_LABEL(v->title),PANGO_ELLIPSIZE_END);gtk_label_set_width_chars(GTK_LABEL(v->title),1);insight_i18n_key(G_OBJECT(v->title),"label","Custom Commands");gtk_header_bar_set_custom_title(GTK_HEADER_BAR(header),v->title);}gtk_widget_show_all(v->window);
}
void insight_custom_commands_show(void) {show(NULL,NULL);}
void insight_custom_commands_menu(GtkWidget *menu) {GtkWidget *item=gtk_menu_item_new_with_label("");gtk_widget_set_name(item,"insight-custom-commands-menu");insight_i18n_key(G_OBJECT(item),"label","Custom Commands…");gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(show),NULL);keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_CUSTOMCOMMANDS,item);gtk_widget_show(item);}
