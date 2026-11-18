/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Public file-comparison and in-memory review entry points.
 * RU: Точки входа сравнения файлов и проверки изменений в памяти. */

/* EN: Open a read-only File Compare window for parent; ownership ends when the window closes.
 * RU: Открывает просмотр File Compare для parent; владение заканчивается при закрытии окна. */
void insight_diff_show(GtkWidget *parent)
{
    Merge *m=merge_new(parent);
    file_compare_layout(m);
}

/* EN: Compare native left/right files for parent using text_flags; initialize File Compare mode, report unsupported inputs, and retain the existing large-file fallback.
 * RU: Сравнивает native-файлы left/right для parent с text_flags; включает просмотр, сообщает неподдерживаемый ввод и сохраняет прежний режим больших файлов. */
void insight_diff_show_files(GtkWidget *parent,const gchar *left,const gchar *right,guint text_flags)
{
    if(insight_stream_ui_large(left,right,1024*1024))
    {
        insight_stream_ui_show(parent,left,right,TRUE);
        return;
    }
    Merge *m=merge_new(parent);
    file_compare_layout(m);
    const gchar *paths[]=
    {
        left,right
    }
    ;
    for(guint side=0;side<2;side++)
    {
        GError *error=NULL;
        gchar *text=g_filename_to_utf8(paths[side],-1,NULL,NULL,&error);
        if(!text)
        {
            message(m,error->message);
            g_clear_error(&error);
            return;
        }
        gtk_entry_set_text(GTK_ENTRY(m->paths[side]),text);
        g_free(text);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->ignore_space),(text_flags&INSIGHT_DIFF_IGNORE_SPACE)!=0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->ignore_eol),(text_flags&INSIGHT_DIFF_IGNORE_EOL)!=0);
    compare(NULL,m);
}

static void review_apply(GtkWidget *widget,gpointer data) {
 (void)widget;Merge *m=data;if(m->busy || m->review_locked || !m->review_ready || !m->apply_cb) return;gchar *text=contents(result(m));GError *error=NULL;gboolean ok=m->apply_cb(text,m->review_data,&error);message(m,ok ? "Result applied to the source buffer in one Undo. Disk is unchanged.":error ? error->message:"Cannot apply this result.");g_clear_error(&error);g_free(text);
}
static void review_ask(GtkWidget *widget,gpointer data) {
 GtkWidget *entry=g_object_get_data(G_OBJECT(widget),"review-question");Merge *m=data;const gchar *question=gtk_entry_get_text(GTK_ENTRY(entry));if(m->ask_cb && *question) m->ask_cb(question,m->review_data);
}
static void review_tools(GtkToggleButton *toggle,gpointer data) {
 GPtrArray *rows=data;for(guint i=0;i<rows->len;i++) gtk_widget_set_visible(g_ptr_array_index(rows,i),TRUE);(void)toggle;
}
/* EN: Create parent's owned in-memory before/after review with explanation and apply/ask callbacks using data/destroy_data; retain merge mode and return its window.
 * RU: Создаёт review снимков before/after для parent с explanation, apply/ask и data/destroy_data; сохраняет режим сведения и возвращает окно. */
GtkWidget *insight_diff_review_create(GtkWidget *parent,const gchar *before,const gchar *after,const gchar *explanation,InsightDiffApply apply,InsightDiffAsk ask,gpointer data,GDestroyNotify destroy_data)
{
    if(!before || !after || strlen(before)>1024*1024 || strlen(after)>1024*1024 || !g_utf8_validate(before,-1,NULL) || !g_utf8_validate(after,-1,NULL))
    {
        if(destroy_data) destroy_data(data);
        return NULL;
    }
    Merge *m=merge_new(parent);
    m->memory=TRUE;
    gtk_widget_set_no_show_all(m->mode,TRUE);
    gtk_widget_hide(m->mode);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(m->toolbar),1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->ignore_space),FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->ignore_eol),FALSE);
    m->apply_cb=apply;
    m->ask_cb=ask;
    m->review_data=data;
    m->review_destroy=destroy_data;
    gtk_window_set_title(GTK_WINDOW(m->window),"AI proposal — Diff / Merge review");
    gtk_entry_set_text(GTK_ENTRY(m->paths[0]),"Captured source buffer");
    gtk_entry_set_text(GTK_ENTRY(m->paths[1]),"AI proposed replacement");
    GtkWidget *box=gtk_bin_get_child(GTK_BIN(m->window)),*panel=gtk_box_new(GTK_ORIENTATION_VERTICAL,3),*note=gtk_text_view_new(),*scroll=gtk_scrolled_window_new(NULL,NULL);
    gtk_widget_set_name(panel,"insight-diff-ai-panel");
    gtk_widget_set_name(note,"insight-diff-ai-explanation");
    g_object_set_data(G_OBJECT(m->window),"insight-diff-review-note",note);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(note),FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(note),GTK_WRAP_WORD_CHAR);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(note)),explanation ? explanation:"",-1);
    gtk_container_add(GTK_CONTAINER(scroll),note);
    gtk_widget_set_size_request(scroll,-1,100);
    gtk_box_pack_start(GTK_BOX(panel),scroll,FALSE,TRUE,0);
    GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*entry=gtk_entry_new(),*send=gtk_button_new_with_label("Ask about this change");
    gtk_widget_set_name(entry,"insight-diff-ai-question");
    gtk_entry_set_max_length(GTK_ENTRY(entry),4096);
    gtk_widget_set_name(send,"insight-diff-ai-ask");
    g_object_set_data(G_OBJECT(send),"review-question",entry);
    gtk_box_pack_start(GTK_BOX(bar),entry,TRUE,TRUE,0);
    gtk_box_pack_start(GTK_BOX(bar),send,FALSE,FALSE,0);
    g_signal_connect(send,"clicked",G_CALLBACK(review_ask),m);
    gtk_widget_set_sensitive(send,ask!=NULL);
    m->apply=gtk_button_new_with_label("Apply Result to Source");
    gtk_widget_set_name(m->apply,"insight-diff-ai-apply");
    gtk_box_pack_start(GTK_BOX(bar),m->apply,FALSE,FALSE,0);
    g_signal_connect(m->apply,"clicked",G_CALLBACK(review_apply),m);
    gtk_box_pack_start(GTK_BOX(panel),bar,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(box),panel,FALSE,FALSE,0);
    gtk_box_reorder_child(GTK_BOX(box),panel,1);
    gtk_widget_show_all(panel);
    /* Keep proposal code visible; the full comparison tools remain available. */ m->review_tools=gtk_check_button_new_with_label("More comparison tools");
    gtk_widget_set_name(m->review_tools,"insight-diff-ai-tools");
    gtk_box_pack_start(GTK_BOX(panel),m->review_tools,FALSE,FALSE,0);
    GtkWidget *anchors[]=
    {
        m->paths[0],m->paths[1],m->ignore_space,m->visible_space,m->find_regex,m->replace_entry,m->replace_scope
    }
    ;
    GPtrArray *rows=g_ptr_array_new_with_free_func(g_object_unref);
    for(guint i=0;i<G_N_ELEMENTS(anchors);i++)
    {
        GtkWidget *row=gtk_widget_get_parent(anchors[i]);
        g_ptr_array_add(rows,g_object_ref(row));
    }
    GtkWidget *review_page=toolbar_page(m,"AI review");
    move_widget(bar,review_page);
    move_widget(m->review_tools,review_page);
    gtk_widget_show_all(gtk_widget_get_ancestor(review_page,GTK_TYPE_SCROLLED_WINDOW));
    insight_compare_ui_reveal(m->apply);
    g_object_set_data_full(G_OBJECT(m->review_tools),"review-tool-rows",rows,(GDestroyNotify)g_ptr_array_unref);
    g_signal_connect(m->review_tools,"toggled",G_CALLBACK(review_tools),rows);
    gtk_widget_show(m->review_tools);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->numbers),TRUE);
    gtk_window_resize(GTK_WINDOW(m->window),INSIGHT_COMPARE_WIDTH,INSIGHT_COMPARE_HEIGHT);
    Comparison *job=g_new0(Comparison,1);
    job->text[0]=g_strdup(before);
    job->text[1]=g_strdup(after);
    m->cancel=g_cancellable_new();
    busy(m,TRUE);
    GTask *task=g_task_new(m->window,m->cancel,comparison_done,NULL);
    g_task_set_task_data(task,job,comparison_free);
    g_task_run_in_thread(task,comparison_worker);
    g_object_unref(task);
    return m->window;
}
void insight_diff_review(GtkWidget *parent,const gchar *before,const gchar *after,const gchar *explanation,InsightDiffApply apply,InsightDiffAsk ask,gpointer data,GDestroyNotify destroy_data) {
 (void)insight_diff_review_create(parent,before,after,explanation,apply,ask,data,destroy_data);
}
gchar *insight_diff_review_result(GtkWidget *window) {
 Merge *m=g_object_get_data(G_OBJECT(window),"insight-diff-state");return m && m->memory && !m->busy && m->review_ready ? contents(result(m)):NULL;
}
gboolean insight_diff_review_update(GtkWidget *window,const gchar *before,const gchar *after,const gchar *explanation,const gchar *staged) {
 Merge *m=g_object_get_data(G_OBJECT(window),"insight-diff-state");if(!m || !m->memory || m->busy || !before || !after || !staged || strcmp(after,staged)) return FALSE;
 const gchar *texts[]={before,after,staged};for(guint i=0;i<3;i++) if(strlen(texts[i])>1024*1024 || !g_utf8_validate(texts[i],-1,NULL)) return FALSE;
 Comparison *job=g_new0(Comparison,1);job->text[0]=g_strdup(before);job->text[1]=g_strdup(after);job->initial_result=g_strdup(staged);
 /* The explanation widget is owned by the comparison window. */
 GtkWidget *note=g_object_get_data(G_OBJECT(window),"insight-diff-review-note");if(note) gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(note)),explanation ? explanation:"",-1);
 m->cancel=g_cancellable_new();busy(m,TRUE);GTask *task=g_task_new(window,m->cancel,comparison_done,NULL);g_task_set_task_data(task,job,comparison_free);g_task_run_in_thread(task,comparison_worker);g_object_unref(task);return TRUE;
}
void insight_diff_review_lock(GtkWidget *window,gboolean locked) {
 Merge *m=g_object_get_data(G_OBJECT(window),"insight-diff-state");if(!m) return;m->review_locked=locked;
 GtkWidget *box=gtk_bin_get_child(GTK_BIN(window));GList *rows=gtk_container_get_children(GTK_CONTAINER(box));for(GList *p=rows;p;p=p->next) {
  if(p->data==m->toolbar) { for(gint i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(m->toolbar));i++) { GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(m->toolbar),i);if(!g_object_get_data(G_OBJECT(page),"insight-editor-ai-controls")) gtk_widget_set_sensitive(page,!locked); } }
  else if(!g_object_get_data(G_OBJECT(p->data),"insight-editor-ai-controls")) gtk_widget_set_sensitive(p->data,!locked);
 }g_list_free(rows);
 busy(m,m->busy);
}
void insight_diff_review_compact(GtkWidget *window) {
 Merge *m=g_object_get_data(G_OBJECT(window),"insight-diff-state");if(!m || !m->review_tools) return;GPtrArray *rows=g_object_get_data(G_OBJECT(m->review_tools),"review-tool-rows");GtkWidget *anchors[]={m->mono,m->find_entry,m->line_number};
 for(guint i=0;i<G_N_ELEMENTS(anchors);i++) { GtkWidget *row=gtk_widget_get_parent(anchors[i]);g_ptr_array_add(rows,g_object_ref(row)); }
 gtk_paned_set_position(GTK_PANED(gtk_widget_get_parent(m->list_scroll)),70);
}
void insight_diff_review_language(GtkWidget *window,const gchar *filename) { Merge *m=g_object_get_data(G_OBJECT(window),"insight-diff-state");if(!m) return;g_free(m->syntax_path);m->syntax_path=g_strdup(filename);file_headers(m);syntax_changed(NULL,m); }
static void answer_changed(GtkComboBox *combo,gpointer data) {
 (void)combo;Merge *m=data;if(m->answer_sync || !m->answers) return;gint left=gtk_combo_box_get_active(GTK_COMBO_BOX(m->answer_left)),right=gtk_combo_box_get_active(GTK_COMBO_BOX(m->answer_right));
 if(m->busy || m->review_locked || left<0 || right<0 || (guint)left>m->answers->len || (guint)right>=m->answers->len) { m->answer_sync=TRUE;gtk_combo_box_set_active(GTK_COMBO_BOX(m->answer_left),m->answer_l);gtk_combo_box_set_active(GTK_COMBO_BOX(m->answer_right),m->answer_r);m->answer_sync=FALSE;return; }
 gchar *working=insight_diff_review_result(m->window);if(working) { AnswerChoice *old=g_ptr_array_index(m->answers,m->answer_r);g_free(old->working);old->working=working; }AnswerChoice *target=g_ptr_array_index(m->answers,right);const gchar *before=left ? ((AnswerChoice *)g_ptr_array_index(m->answers,left-1))->code:m->answer_base;
 if(insight_diff_review_update(m->window,before,target->working,target->explanation,target->working)) { m->answer_l=left;m->answer_r=right; }
}
void insight_diff_review_choices(GtkWidget *window,const gchar *base,const gchar *const *names,const gchar *const *codes,const gchar *const *explanations,guint count,guint chosen) {
 Merge *m=g_object_get_data(G_OBJECT(window),"insight-diff-state");if(!m || !m->memory || count>4) return;
 m->answer_sync=TRUE;if(m->answer_page) { GtkWidget *page=gtk_widget_get_ancestor(m->answer_page,GTK_TYPE_SCROLLED_WINDOW);if(page) gtk_widget_destroy(page);m->answer_page=m->answer_left=m->answer_right=NULL; }g_clear_pointer(&m->answers,g_ptr_array_unref);g_clear_pointer(&m->answer_base,g_free);if(!count || chosen>=count || !base) { m->answer_sync=FALSE;return; }
 for(guint i=0;i<count;i++) if(!codes[i] || strlen(codes[i])>1024*1024 || !g_utf8_validate(codes[i],-1,NULL)) { m->answer_sync=FALSE;return; }
 m->answers=g_ptr_array_new_with_free_func(answer_free);m->answer_base=g_strdup(base);m->answer_l=0;m->answer_r=chosen;m->answer_sync=TRUE;m->answer_page=toolbar_page(m,"AI answers");gtk_widget_set_name(m->answer_page,"insight-diff-answer-controls");GtkWidget *label=gtk_label_new(insight_tr("Compare each answer with current code or with another answer. Manual Result edits stay with the right answer."));gtk_label_set_line_wrap(GTK_LABEL(label),TRUE);gtk_box_pack_start(GTK_BOX(m->answer_page),label,FALSE,FALSE,0);
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);m->answer_left=gtk_combo_box_text_new();m->answer_right=gtk_combo_box_text_new();gtk_widget_set_name(m->answer_left,"insight-diff-answer-left");gtk_widget_set_name(m->answer_right,"insight-diff-answer-right");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->answer_left),insight_tr("Current source"));gtk_box_pack_start(GTK_BOX(row),gtk_label_new(insight_tr("Left")),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),m->answer_left,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(row),gtk_label_new(insight_tr("Right answer")),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),m->answer_right,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(m->answer_page),row,FALSE,FALSE,0);
 for(guint i=0;i<count;i++) { AnswerChoice *a=g_new0(AnswerChoice,1);a->name=g_strdup(names[i]);a->code=g_strdup(codes[i]);a->working=g_strdup(codes[i]);a->explanation=g_strdup(explanations[i]);g_ptr_array_add(m->answers,a);gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->answer_left),a->name);gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->answer_right),a->name); }gtk_combo_box_set_active(GTK_COMBO_BOX(m->answer_left),0);gtk_combo_box_set_active(GTK_COMBO_BOX(m->answer_right),chosen);g_signal_connect(m->answer_left,"changed",G_CALLBACK(answer_changed),m);g_signal_connect(m->answer_right,"changed",G_CALLBACK(answer_changed),m);m->answer_sync=FALSE;gtk_widget_show_all(m->answer_page);GtkWidget *scroll=gtk_widget_get_ancestor(m->answer_page,GTK_TYPE_SCROLLED_WINDOW);gtk_widget_show(scroll);gtk_notebook_set_current_page(GTK_NOTEBOOK(m->toolbar),gtk_notebook_page_num(GTK_NOTEBOOK(m->toolbar),scroll));
}
