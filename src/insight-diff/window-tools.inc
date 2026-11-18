/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Comparison toolbar construction and source-selection helpers.
 * RU: Создание панели команд сравнения и работа с выделением исходников. */

static GtkWidget *button(GtkWidget *box,const gchar *label,GCallback callback,Merge *m) {
 GtkWidget *w=gtk_button_new_with_label(label);gtk_box_pack_start(GTK_BOX(box),w,FALSE,FALSE,0);g_signal_connect(w,"clicked",callback,m);return w;
}
static GtkWidget *scroll(GtkWidget *child) {
 GtkWidget *w=gtk_scrolled_window_new(NULL,NULL);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(w),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);gtk_container_add(GTK_CONTAINER(w),child);return w;
}
static void show_directory(GtkWidget *widget,gpointer data) { (void)widget;Merge *m=data;insight_directory_show(m->window); }
static GtkWidget *toolbar_page(Merge *m,const gchar *label) {
 return insight_compare_ui_page(m->toolbar,label);
}
static void move_widget(GtkWidget *widget,GtkWidget *parent) { g_object_ref(widget);GtkWidget *old=gtk_widget_get_parent(widget);if(old) gtk_container_remove(GTK_CONTAINER(old),widget);gtk_box_pack_start(GTK_BOX(parent),widget,FALSE,FALSE,0);g_object_unref(widget); }
static void toolbar_build(Merge *m,GtkWidget *root,GtkWidget **rows) {
 m->toolbar=insight_compare_ui_tabs(root,"insight-diff-toolbar-tabs");const gchar *labels[]={"Files and comparison","Merge and history","View","Search","Replace","Navigation"};GtkWidget *pages[6];for(guint i=0;i<6;i++) pages[i]=toolbar_page(m,labels[i]);const guint groups[]={0,0,1,2,0,2,2,3,3,4,4,5};for(guint i=0;i<12;i++) move_widget(rows[i],pages[groups[i]]);
 GtkWidget *actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);gtk_box_pack_start(GTK_BOX(pages[0]),actions,FALSE,FALSE,0);move_widget(m->compare_button,actions);move_widget(m->cancel_button,actions);move_widget(m->external,actions);

}
static void binary_mode(GtkWidget *w,gpointer data) {
 (void)w;Merge *m=data;if(m->busy || m->memory) return;
 gchar *paths[2];for(guint i=0;i<2;i++) paths[i]=g_filename_from_utf8(gtk_entry_get_text(GTK_ENTRY(m->paths[i])),-1,NULL,NULL,NULL);
 insight_hex_show(m->window,paths[0],paths[1]);for(guint i=0;i<2;i++) g_free(paths[i]);
}
static void remember_source_selection(GtkTextBuffer *buffer,GtkTextIter *location,GtkTextMark *mark,gpointer data) {
 (void)location;Merge *m=data;if(m->busy || m->changing || (mark!=gtk_text_buffer_get_insert(buffer) && mark!=gtk_text_buffer_get_selection_bound(buffer))) return;
 GtkTextIter a,z;if(!gtk_text_buffer_get_selection_bounds(buffer,&a,&z)) return;guint side=buffer==m->syntax_buffers[1] ? 1:0;g_free(m->picked[side]);m->picked[side]=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);
}
static void forget_source_selection(GtkTextBuffer *buffer,gpointer data) { Merge *m=data;guint side=buffer==m->syntax_buffers[1] ? 1:0;g_clear_pointer(&m->picked[side],g_free); }
static void merge_source_selection(GtkWidget *w,gpointer data) {
 Merge *m=data;if(m->busy || m->review_locked) return;guint side=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w),"side"));GtkTextIter a,z;
 GtkTextBuffer *source=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));gchar *text=gtk_text_buffer_get_selection_bounds(source,&a,&z) ? gtk_text_buffer_get_text(source,&a,&z,TRUE):g_strdup(m->picked[side]);if(!text) { message(m,"Select source text first.");return; }
 gtk_text_buffer_begin_user_action(result(m));gtk_text_buffer_delete_selection(result(m),TRUE,TRUE);gtk_text_buffer_insert_at_cursor(result(m),text,-1);gtk_text_buffer_end_user_action(result(m));g_free(text);selection(NULL,m);
}
/* EN: Refresh data's comparison captions and cached symbol labels after a locale change; snapshots remain intact.
 * RU: Обновляет подписи сравнения data и сохранённых символов после смены языка, сохраняя снимки. */
static void diff_locale_changed(gpointer data)
{
    Merge *m = data;
    file_headers(m);
    resolution_rows(m);
    diff_symbols_refresh(m);
    state(m);
}
static gchar *diff_history_label(gpointer data,guint index) { Merge *m=data;if(!index) return g_strdup(insight_tr("Initial Result"));Snapshot *a=g_ptr_array_index(m->history,index-1),*b=g_ptr_array_index(m->history,index);return insight_history_text_label(a->text,b->text); }
static guint diff_history_count(gpointer data) { return ((Merge*)data)->history->len; }
static guint diff_history_position(gpointer data) { return ((Merge*)data)->position; }
static void diff_history_jump(gpointer data,guint index) { Merge *m=data;if(!m->busy && !m->review_locked && index<m->history->len) restore(m,index); }
static void diff_history_tip(gpointer data,guint index,gchar **a,gchar **b) { Merge *m=data;if(index>=m->history->len) return;Snapshot *before=g_ptr_array_index(m->history,index ? index-1:0),*after=g_ptr_array_index(m->history,index);insight_history_text_preview(before->text,after->text,a,b); }
