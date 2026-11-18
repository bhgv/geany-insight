/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Cancellable snapshot comparison and explicit result saving.
 * RU: Отменяемое сравнение снимков и явное сохранение результата. */

static void choose(GtkWidget *w,gpointer data) {
 Merge *m=data;int side=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w),"side"));
 GtkWidget *d=gtk_file_chooser_dialog_new("Choose source file",GTK_WINDOW(m->window),GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT) { gchar *p=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));GError *error=NULL;gchar *text=g_filename_to_utf8(p,-1,NULL,NULL,&error);if(text) gtk_entry_set_text(GTK_ENTRY(m->paths[side]),text);else message(m,error->message);g_clear_error(&error);g_free(text);g_free(p); }
 gtk_widget_destroy(d);
}
typedef struct { gchar *paths[2],*text[2],*initial_result; unsigned options;InsightDiff diff; } Comparison;
static void comparison_free(gpointer data) {
 Comparison *job=data;for(int i=0;i<2;i++) { g_free(job->paths[i]);g_free(job->text[i]); }
 insight_diff_clear(&job->diff);g_free(job->initial_result);g_free(job);
}
static int comparison_cancelled(void *data) { return g_cancellable_is_cancelled(data); }
static void comparison_worker(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source;Comparison *job=data;GError *error=NULL;
 for(int i=0;i<2;i++) {
  if(job->text[i]) continue;
  GFile *file=g_file_new_for_path(job->paths[i]);
  GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_STANDARD_TYPE,G_FILE_QUERY_INFO_NONE,cancel,&error);
  if(!info) { g_object_unref(file);goto failed; }
  gboolean regular=g_file_info_get_file_type(info)==G_FILE_TYPE_REGULAR;g_object_unref(info);
  if(!regular) { g_object_unref(file);g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Choose regular UTF-8 text files.");goto failed; }
  GFileInputStream *input=g_file_read(file,cancel,&error);g_object_unref(file);if(!input) goto failed;
  const gsize limit=1024*1024;gsize len=0;job->text[i]=g_malloc(limit+2);
  gboolean ok=g_input_stream_read_all(G_INPUT_STREAM(input),job->text[i],limit+1,&len,cancel,&error);
  g_object_unref(input);if(!ok) goto failed;
  if(len>limit || memchr(job->text[i],0,len) || !g_utf8_validate(job->text[i],len,NULL)) {
   g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Supports UTF-8 text files up to 1 MiB, without NUL bytes.");goto failed;
  }
  job->text[i][len]=0;
 }
 int rc=insight_diff_compute_cancellable(job->text[0],job->text[1],job->options,comparison_cancelled,cancel,&job->diff);
 if(rc) {
  g_set_error_literal(&error,G_IO_ERROR,rc==-3 ? G_IO_ERROR_CANCELLED : G_IO_ERROR_FAILED,
   rc==-3 ? "Comparison cancelled." : rc==-2 ? "Too many changed lines (4 million LCS cells)." : "Cannot allocate comparison.");goto failed;
 }
 g_task_return_boolean(task,TRUE);return;
failed:
 g_task_return_error(task,error);
}
/* EN: Set m's asynchronous comparison state to active and refresh mode-aware actions; never save files.
 * RU: Устанавливает асинхронное состояние active для m и обновляет команды текущего режима без записи файлов. */
static void busy(Merge *m,gboolean active)
{
    m->busy=active;
    gtk_widget_set_sensitive(m->save_button,!active && !m->browse);
    if(m->mode) gtk_widget_set_sensitive(m->mode,!active);
    gtk_widget_set_sensitive(m->compare_button,!active && !m->memory);
    if(active && m->memory) m->review_ready=FALSE;
    if(m->apply) gtk_widget_set_sensitive(m->apply,!active && m->review_ready && !m->review_locked);
    gtk_widget_set_sensitive(m->cancel_button,active);
    gtk_widget_set_sensitive(m->views[2],!active);
    for(int i=0;i<2;i++) gtk_widget_set_sensitive(gtk_widget_get_parent(m->paths[i]),!active && !m->memory);
    gtk_widget_set_sensitive(m->ignore_space,!active && !m->memory);
    gtk_widget_set_sensitive(m->ignore_eol,!active && !m->memory);
    state(m);
    selection(NULL,m);
}
/* EN: Finish async comparison from source/async; data is unused. On success publish captured text, definition labels and initial Result; preserve old data on failure.
 * RU: Завершает сравнение source/async; data не используется. При успехе публикует снимки, контекст определений и начальный Result; при ошибке сохраняет прежние данные. */
static void comparison_done(GObject *source,GAsyncResult *async,gpointer data)
{
    (void)data;
    GTask *task=G_TASK(async);
    GError *error=NULL;
    gboolean ok=g_task_propagate_boolean(task,&error);
    Merge *m=g_object_get_data(source,"insight-diff-state");
    if(!m)
    {
        g_clear_error(&error);
        return;
    }
    g_clear_object(&m->cancel);
    busy(m,FALSE);
    if(!ok)
    {
        message(m,error->message);
        g_clear_error(&error);
        return;
    }
    Comparison *job=g_task_get_task_data(task);
    m->changing=TRUE;
    m->scrolling=TRUE;
    clear_diff(m);
    m->diff=job->diff;
    job->diff=(InsightDiff)
    {
        0
    }
    ;
    for(int i=0;i<2;i++)
    {
        g_free(m->sources[i]);
        m->sources[i]=job->text[i];
        job->text[i]=NULL;
        g_free(m->loaded_paths[i]);
        m->loaded_paths[i]=g_strdup(job->paths[i]);
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[i])),m->sources[i],-1);
    }
    gtk_text_buffer_set_text(result(m),m->sources[0],-1);
    g_free(m->saved);
    m->saved=g_strdup(m->sources[0]);
    diff_symbols_collect(m);
    m->resolutions=g_new0(guchar,m->diff.count);
    m->marks=g_new0(GtkTextMark*,2*m->diff.count);
    gtk_list_store_clear(m->list);
    for(size_t i=0;i<m->diff.count;i++)
    {
        InsightDiffHunk *h=&m->diff.hunks[i];
        GtkTextIter a,z;
        gtk_text_buffer_get_iter_at_offset(result(m),&a,chars(m->sources[0],h->a_begin));
        gtk_text_buffer_get_iter_at_offset(result(m),&z,chars(m->sources[0],h->a_end));
        m->marks[2*i]=gtk_text_buffer_create_mark(result(m),NULL,&a,TRUE);
        m->marks[2*i+1]=gtk_text_buffer_create_mark(result(m),NULL,&z,FALSE);
        /* Compute the right line independently of UTF-8 byte width. */ GtkTextIter right;
        gtk_text_buffer_get_iter_at_offset(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[1])),&right,chars(m->sources[1],h->b_begin));
        gchar *label=g_strdup_printf("%zu: Left line %d / Right line %d",i+1,gtk_text_iter_get_line(&a)+1,gtk_text_iter_get_line(&right)+1);
        GtkTreeIter it;
        gtk_list_store_append(m->list,&it);
        GtkTextIter left_end=z,right_end;
        gtk_text_buffer_get_iter_at_offset(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[1])),&right_end,chars(m->sources[1],h->b_end));
        if(h->a_end>h->a_begin && gtk_text_iter_starts_line(&left_end)) gtk_text_iter_backward_char(&left_end);
        if(h->b_end>h->b_begin && gtk_text_iter_starts_line(&right_end)) gtk_text_iter_backward_char(&right_end);
        gchar *left_line=g_strdup_printf("%d–%d%s",gtk_text_iter_get_line(&a)+1,gtk_text_iter_get_line(&left_end)+1,h->a_begin==h->a_end ? " (empty)":""),*right_line=g_strdup_printf("%d–%d%s",gtk_text_iter_get_line(&right)+1,gtk_text_iter_get_line(&right_end)+1,h->b_begin==h->b_end ? " (empty)":"");
        gchar *context=diff_symbols_hunk(m,h);
        gtk_list_store_set(m->list,&it,0,(gint)i,1,label,2,left_line,3,right_line,4,insight_tr("Not merged"),5,context,-1);
        g_free(context);
        g_free(left_line);
        g_free(right_line);
        g_free(label);
        highlight(m,0,h->a_begin,h->a_end);
        highlight(m,1,h->b_begin,h->b_end);
    }
    file_headers(m);
    if(gtk_combo_box_get_active(GTK_COMBO_BOX(m->destination_kind))<2) destination_choice(GTK_COMBO_BOX(m->destination_kind),m);
    map_build(m);
    m->changing=FALSE;
    m->scrolling=FALSE;
    alignment_queue(m);
    record(m);
    selection(NULL,m);
    gchar *label=g_strdup_printf("%zu differences. Result starts with Left; select a block and take Left or Right, or edit the result.",m->diff.count);
    message(m,label);
    g_free(label);
    if(m->memory)
    {
        m->review_ready=TRUE;
        gtk_widget_set_sensitive(m->apply,!m->review_locked);
    }
    gtk_widget_set_sensitive(m->external,!m->memory);
    if(m->diff.count)
    {
        GtkTreePath *p=gtk_tree_path_new_first();
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(m->hunk_view),p,NULL,FALSE);
        gtk_tree_path_free(p);
    }
    if(job->initial_result && !strcmp(job->initial_result,m->sources[1])) take_all(m->take_all[1],m);
}
static void compare(GtkWidget *w,gpointer data) {
 (void)w;Merge *m=data;if(m->busy || !discard(m)) return;
 Comparison *job=g_new0(Comparison,1);
 for(int i=0;i<2;i++) {
  GError *error=NULL;gchar *native=g_filename_from_utf8(gtk_entry_get_text(GTK_ENTRY(m->paths[i])),-1,NULL,NULL,&error);
  if(!native || !*native) { message(m,error ? error->message : "Choose two source files before comparing.");g_clear_error(&error);g_free(native);comparison_free(job);return; }
  job->paths[i]=insight_canonicalize_filename(native,NULL);g_free(native);
 }
 if(insight_stream_ui_large(job->paths[0],job->paths[1],1024*1024)) { insight_stream_ui_show(m->window,job->paths[0],job->paths[1],TRUE);comparison_free(job);return; }
 job->options=(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->ignore_space)) ? INSIGHT_DIFF_IGNORE_SPACE : 0) |
  (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->ignore_eol)) ? INSIGHT_DIFF_IGNORE_EOL : 0);
 m->cancel=g_cancellable_new();busy(m,TRUE);message(m,"Comparing… Cancel keeps the previous result.");
 GTask *task=g_task_new(m->window,m->cancel,comparison_done,NULL);
 g_task_set_task_data(task,job,comparison_free);g_task_run_in_thread(task,comparison_worker);g_object_unref(task);
}
static void cancel_compare(GtkWidget *w,gpointer data) {
 (void)w;Merge *m=data;if(m->cancel) g_cancellable_cancel(m->cancel);
}
/* Capture before the modal chooser; never write stale text after a nested loop. */
static void write_result(Merge *m,guint mode) {
 if(m->busy || m->changing || mode>2) return;
 gchar *text=NULL;
 if(mode==2) { GtkTextIter a,z;if(!gtk_text_buffer_get_selection_bounds(result(m),&a,&z)) { message(m,"Select result text first.");return; }text=gtk_text_buffer_get_text(result(m),&a,&z,TRUE); }
 else text=contents(result(m));
 GtkWidget *window=m->window;g_object_ref(window);guint64 serial=m->history_serial;
 const gchar *titles[]={"Save merge result","Save a copy of Result","Save Result selection"};const gchar *names[]={"merged.txt","merged-copy.txt","selection.txt"};
 GtkWidget *dialog=gtk_file_chooser_dialog_new(titles[mode],GTK_WINDOW(window),GTK_FILE_CHOOSER_ACTION_SAVE,"Cancel",GTK_RESPONSE_CANCEL,"Save",GTK_RESPONSE_ACCEPT,NULL);g_object_ref(dialog);gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog),TRUE);gtk_widget_set_name(dialog,"insight-diff-save-dialog");
 const gchar *notes[]={"Save the full merge result.","Save a separate copy; keep the current result's saved or modified status.","Save the exact selection; keep the current result's saved or modified status."};GtkWidget *note=gtk_label_new(notes[mode]);gtk_label_set_line_wrap(GTK_LABEL(note),TRUE);gtk_label_set_xalign(GTK_LABEL(note),0);gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog),note);gtk_widget_show(note);
 gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),TRUE);gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),names[mode]);
 if(mode==0 && *gtk_entry_get_text(GTK_ENTRY(m->destination))) { gchar *native=g_filename_from_utf8(gtk_entry_get_text(GTK_ENTRY(m->destination)),-1,NULL,NULL,NULL);if(native) { if(g_path_is_absolute(native)) { gchar *folder=g_path_get_dirname(native),*name=g_path_get_basename(native);gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),folder);gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),name);g_free(folder);g_free(name); }else gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),native);g_free(native); } }
 gchar *open_path=NULL;gint response=gtk_dialog_run(GTK_DIALOG(dialog));m=g_object_get_data(G_OBJECT(window),"insight-diff-state");
 if(response==GTK_RESPONSE_ACCEPT && m) {
  if(m->busy || serial!=m->history_serial) message(m,"Result changed; reopen the save command. Nothing written.");
  else {
   gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));GError *error=NULL;
   if(path && g_file_set_contents(path,text,-1,&error)) {
    if(mode==0) { gchar *display=g_filename_to_utf8(path,-1,NULL,NULL,NULL);if(display) gtk_entry_set_text(GTK_ENTRY(m->destination),display);g_free(display);g_free(m->saved);m->saved=g_strdup(text);message(m,"Result saved.");state(m); }
    else { message(m,mode==1 ? "Result copy saved; save state unchanged." : "Result selection saved; save state unchanged.");if(mode==2) open_path=g_strdup(path); }
   } else { message(m,error ? error->message : "Choose a local destination filename.");g_clear_error(&error); }
   g_free(path);
  }
 }
 gtk_widget_destroy(dialog);g_object_unref(dialog);g_object_unref(window);g_free(text);
 if(open_path && source_opener) source_opener(open_path,1);
 g_free(open_path);
}
static void save(GtkWidget *w,gpointer data) { (void)w;write_result(data,0); }
static void external(GtkWidget *w,gpointer data) {
 (void)w;Merge *m=data;gchar *program=g_find_program_in_path("kdiff3");
 if(!program) { message(m,"KDiff3 is not installed. Internal comparison and merge remain available.");return; }
 gchar *paths[2];
 for(int i=0;i<2;i++) {
  const gchar *entry=gtk_entry_get_text(GTK_ENTRY(m->paths[i]));
  if(!*entry || !g_file_test(entry,G_FILE_TEST_IS_REGULAR)) { message(m,"Choose two existing files before launching KDiff3.");g_free(program);return; }
 }
 for(int i=0;i<2;i++) paths[i]=insight_canonicalize_filename(gtk_entry_get_text(GTK_ENTRY(m->paths[i])),NULL);
 gchar *argv[]={program,paths[0],paths[1],NULL};GError *error=NULL;
 if(!g_spawn_async(NULL,argv,NULL,0,NULL,NULL,NULL,&error)) { message(m,error->message);g_clear_error(&error); }
 g_free(paths[0]);g_free(paths[1]);g_free(program);
}
