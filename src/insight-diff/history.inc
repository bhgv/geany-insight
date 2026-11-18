/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Comparison preferences, result history and undo state.
 * RU: Параметры сравнения, история результата и состояние отмены. */

static void answer_free(gpointer data) { AnswerChoice *a=data;g_free(a->name);g_free(a->code);g_free(a->working);g_free(a->explanation);g_free(a); }
static gboolean syntax_idle(gpointer data) { Merge *m=data;if(m->busy) return G_SOURCE_CONTINUE;m->syntax_idle=0;if(colorizer) for(guint i=0;i<3;i++) colorizer(m->syntax_buffers[i],m->syntax_path ? m->syntax_path:m->loaded_paths[i<2 ? i:1]);return G_SOURCE_REMOVE; }
static void syntax_changed(GtkTextBuffer *buffer,gpointer data) { (void)buffer;Merge *m=data;if(!m->syntax_idle) m->syntax_idle=g_timeout_add(150,syntax_idle,m); }
static GKeyFile *view_defaults;
static GList *open_compares;
static const gchar *view_group="file-compare";
static const gchar *view_checks[]={"mono","numbers","list","wrap","sync","vertical","overview","ignore-space","ignore-eol","visible-space","visible-eol","search-regex","search-dotall","confirm-replacements"};
static void view_controls(Merge *m,GtkWidget **controls) {
 GtkWidget *items[]={m->mono,m->numbers,m->list_toggle,m->wrap_toggle,m->sync,m->vertical,m->overview,m->ignore_space,m->ignore_eol,m->visible_space,m->visible_eol,m->find_regex,m->find_dotall,m->replace_confirm};for(guint i=0;i<G_N_ELEMENTS(items);i++) controls[i]=items[i];
}
static void view_copy(GKeyFile *from,GKeyFile *to) {
 g_key_file_remove_group(to,view_group,NULL);const gchar *extra[]={"version","zoom","width","height","search-file","search-line","search-column","search-text"};
 for(guint i=0;i<G_N_ELEMENTS(view_checks)+G_N_ELEMENTS(extra);i++) { const gchar *name=i<G_N_ELEMENTS(view_checks) ? view_checks[i] : extra[i-G_N_ELEMENTS(view_checks)];gchar *value=g_key_file_get_value(from,view_group,name,NULL);if(value && strlen(value)<=128) g_key_file_set_value(to,view_group,name,value);g_free(value); }
}
static void view_save(Merge *m,GKeyFile *settings) {
 GtkWidget *controls[G_N_ELEMENTS(view_checks)];view_controls(m,controls);g_key_file_set_integer(settings,view_group,"version",1);
 for(guint i=0;i<G_N_ELEMENTS(view_checks);i++) g_key_file_set_boolean(settings,view_group,view_checks[i],gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls[i])));
 const gchar *fields[]={"search-file","search-line","search-column","search-text"};for(guint i=0;i<4;i++) g_key_file_set_boolean(settings,view_group,fields[i],m->search_fields[i]);
 gint width,height;gtk_window_get_size(GTK_WINDOW(m->window),&width,&height);g_key_file_set_integer(settings,view_group,"width",CLAMP(width,640,4000));g_key_file_set_integer(settings,view_group,"height",CLAMP(height,400,2400));g_key_file_set_integer(settings,view_group,"zoom",gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->zoom)));
}
static gboolean view_integer(GKeyFile *settings,const gchar *name,gint low,gint high,gint *out) {
 GError *error=NULL;gint value=g_key_file_get_integer(settings,view_group,name,&error);gboolean ok=!error && value>=low && value<=high;g_clear_error(&error);if(ok) *out=value;return ok;
}
static void view_restore(Merge *m,GKeyFile *settings) {
 if(!settings || g_key_file_get_integer(settings,view_group,"version",NULL)!=1) return;
 GtkWidget *controls[G_N_ELEMENTS(view_checks)];view_controls(m,controls);
 for(guint i=0;i<G_N_ELEMENTS(view_checks);i++) { GError *error=NULL;gboolean value=g_key_file_get_boolean(settings,view_group,view_checks[i],&error);if(!error) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls[i]),value);g_clear_error(&error); }
 const gchar *fields[]={"search-file","search-line","search-column","search-text"};for(guint i=0;i<4;i++) { GError *error=NULL;gboolean value=g_key_file_get_boolean(settings,view_group,fields[i],&error);if(!error) m->search_fields[i]=value;g_clear_error(&error); }
 gint value;if(view_integer(settings,"zoom",50,250,&value)) gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),value);gint width=INSIGHT_COMPARE_WIDTH,height=INSIGHT_COMPARE_HEIGHT;view_integer(settings,"width",640,4000,&width);view_integer(settings,"height",400,2400,&height);gtk_window_resize(GTK_WINDOW(m->window),width,height);
}
void insight_diff_read(GKeyFile *settings) { if(!view_defaults) view_defaults=g_key_file_new();view_copy(settings,view_defaults); }
void insight_diff_write(GKeyFile *settings) { if(!view_defaults) view_defaults=g_key_file_new();if(open_compares) view_save(open_compares->data,view_defaults);view_copy(view_defaults,settings); }
static GtkTextBuffer *result(Merge *m) { return m->output; }
static gchar *contents(GtkTextBuffer *b) {
 GtkTextIter a,z; gtk_text_buffer_get_bounds(b,&a,&z); return gtk_text_buffer_get_text(b,&a,&z,TRUE);
}
static void snapshot_free(gpointer data) { Snapshot *s=data;g_free(s->text);g_free(s->positions);g_free(s->resolutions);g_free(s); }
static void resolution_rows(Merge *m) {
 const gchar *labels[]={"Not merged","Merged — Left","Merged — Right","Edited manually"};GtkTreeIter row;
 for(guint i=0;i<m->diff.count;i++) if(gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(m->list),&row,NULL,i)) gtk_list_store_set(m->list,&row,4,insight_tr(labels[m->resolutions[i]]),-1);
}
static void manual_range(Merge *m,gint begin,gint end) {
 if(m->changing || m->busy) return;
 for(guint i=0;i<m->diff.count;i++) { GtkTextIter a,z;gtk_text_buffer_get_iter_at_mark(result(m),&a,m->marks[2*i]);gtk_text_buffer_get_iter_at_mark(result(m),&z,m->marks[2*i+1]);gint low=gtk_text_iter_get_offset(&a),high=gtk_text_iter_get_offset(&z);if(begin==end ? begin>=low && begin<=high : begin<high && end>low) m->resolutions[i]=3; }
}
static void manual_insert(GtkTextBuffer *b,GtkTextIter *where,gchar *text,gint length,gpointer data) { (void)b;(void)text;(void)length;manual_range(data,gtk_text_iter_get_offset(where),gtk_text_iter_get_offset(where)); }
static void manual_delete(GtkTextBuffer *b,GtkTextIter *a,GtkTextIter *z,gpointer data) { (void)b;manual_range(data,gtk_text_iter_get_offset(a),gtk_text_iter_get_offset(z)); }
static void file_headers(Merge *m) {
 for(guint i=0;i<2;i++) { gchar *path=m->loaded_paths[i] ? g_filename_display_name(m->loaded_paths[i]):g_strdup(gtk_entry_get_text(GTK_ENTRY(m->paths[i])));if(m->memory && m->syntax_path && !m->loaded_paths[i]) { gchar *display=g_filename_display_name(m->syntax_path),*named=g_strdup_printf("%s [%s]",path,display);g_free(display);g_free(path);path=named; }gchar *title=g_strdup_printf("%s — %s",insight_tr(i ? "Right (read only)":"Left (read only)"),*path ? path:insight_tr("No file"));insight_i18n_text(G_OBJECT(m->headers[i]),"label",i ? "Right (read only) — {value}":"Left (read only) — {value}",*path ? path:insight_tr("No file"));insight_i18n_text(G_OBJECT(m->headers[i]),"tooltip-text",i ? "Right (read only) — {value}":"Left (read only) — {value}",*path ? path:insight_tr("No file"));GtkWidget *label=gtk_label_new(NULL);insight_i18n_text(G_OBJECT(label),"label",i ? "Right (read only) — {value}":"Left (read only) — {value}",*path ? path:insight_tr("No file"));gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_MIDDLE);gtk_widget_set_tooltip_text(label,title);gtk_tree_view_column_set_title(m->file_columns[i],title);gtk_tree_view_column_set_widget(m->file_columns[i],label);gtk_widget_show(label);g_free(title);g_free(path); }
 const gchar *path=gtk_entry_get_text(GTK_ENTRY(m->destination));gchar *title=g_strdup_printf("%s — %s",insight_tr("Result (editable)"),*path ? path:insight_tr("Choose output file"));insight_i18n_text(G_OBJECT(m->headers[2]),"label","Result (editable) — {value}",*path ? path:insight_tr("Choose output file"));insight_i18n_text(G_OBJECT(m->headers[2]),"tooltip-text","Result (editable) — {value}",*path ? path:insight_tr("Choose output file"));g_free(title);
}
static void destination_changed(GtkWidget *widget,gpointer data) {
 (void)widget;Merge *m=data;if(!g_object_get_data(G_OBJECT(m->destination),"insight-output-selecting")) { gint choice=2;const gchar *target=gtk_entry_get_text(GTK_ENTRY(m->destination));for(guint side=0;side<2;side++) if(m->loaded_paths[side]) { gchar *path=g_filename_display_name(m->loaded_paths[side]);if(*target && !strcmp(target,path)) choice=side;g_free(path); }g_object_set_data(G_OBJECT(m->destination),"insight-output-selecting",GINT_TO_POINTER(1));gtk_combo_box_set_active(GTK_COMBO_BOX(m->destination_kind),choice);g_object_set_data(G_OBJECT(m->destination),"insight-output-selecting",NULL); }file_headers(m);
}
static void destination_choice(GtkComboBox *choice,gpointer data) {
 Merge *m=data;if(g_object_get_data(G_OBJECT(m->destination),"insight-output-selecting")) return;g_object_set_data(G_OBJECT(m->destination),"insight-output-selecting",GINT_TO_POINTER(1));gint side=gtk_combo_box_get_active(choice);if(side<2 && side>=0) { gchar *path=m->loaded_paths[side] ? g_filename_display_name(m->loaded_paths[side]):g_strdup(gtk_entry_get_text(GTK_ENTRY(m->paths[side])));gtk_entry_set_text(GTK_ENTRY(m->destination),path);g_free(path); }else if(side==2) gtk_entry_set_text(GTK_ENTRY(m->destination),"");g_object_set_data(G_OBJECT(m->destination),"insight-output-selecting",NULL);
}
static void destination_browse(GtkWidget *widget,gpointer data) {
 (void)widget;Merge *m=data;GtkWidget *window=g_object_ref(m->window),*d=gtk_file_chooser_dialog_new(insight_tr("Choose output file"),GTK_WINDOW(window),GTK_FILE_CHOOSER_ACTION_SAVE,"Cancel",GTK_RESPONSE_CANCEL,"Choose",GTK_RESPONSE_ACCEPT,NULL);g_object_ref(d);gtk_window_set_destroy_with_parent(GTK_WINDOW(d),TRUE);gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d),TRUE);
 if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT && (m=g_object_get_data(G_OBJECT(window),"insight-diff-state"))) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d)),*display=g_filename_to_utf8(path,-1,NULL,NULL,NULL);if(display) { gtk_combo_box_set_active(GTK_COMBO_BOX(m->destination_kind),2);gtk_entry_set_text(GTK_ENTRY(m->destination),display); }g_free(display);g_free(path); }gtk_widget_destroy(d);g_object_unref(d);g_object_unref(window);
}
static GtkWidget *help_overlay(GtkWidget *content,const gchar *topic) {
 GtkWidget *overlay=gtk_overlay_new(),*help=insight_panel_help_button(topic);gtk_container_add(GTK_CONTAINER(overlay),content);gtk_widget_set_opacity(help,0.8);gtk_widget_set_halign(help,GTK_ALIGN_END);gtk_widget_set_valign(help,GTK_ALIGN_START);gtk_overlay_add_overlay(GTK_OVERLAY(overlay),help);return overlay;
}
/* EN: Refresh m's history controls and mode-aware dirty title without changing text.
 * RU: Обновляет историю и заголовок режима m с признаком изменений без замены текста. */
static void state(Merge *m)
{
    insight_history_update(m->history_ui,!m->busy && !m->review_locked && !m->browse);
    if(m->replace_apply) gtk_widget_set_sensitive(m->replace_apply,!m->busy);
    gtk_widget_set_sensitive(m->undo,!m->busy && !m->review_locked && !m->browse && m->position>0);
    gtk_widget_set_sensitive(m->redo,!m->busy && !m->review_locked && !m->browse && m->position+1<m->history->len);
    gchar *text=contents(result(m));
    gboolean dirty=g_strcmp0(text,m->saved)!=0;
    g_free(text);
    gtk_window_set_title(GTK_WINDOW(m->window),insight_tr(m->browse ? (dirty ? "File Compare — unsaved result":"File Compare") : (dirty ? "Diff / Merge — unsaved result":"Diff / Merge")));
}
static void record(Merge *m) {
 m->history_serial++;
 while(m->history->len>m->position+1) g_ptr_array_remove_index(m->history,m->history->len-1);
 Snapshot *s=g_new0(Snapshot,1);s->text=contents(result(m));s->positions=g_new(gint,2*m->diff.count);s->resolutions=g_new(guchar,m->diff.count);
 for(size_t i=0;i<2*m->diff.count;i++) { GtkTextIter it;gtk_text_buffer_get_iter_at_mark(result(m),&it,m->marks[i]);s->positions[i]=gtk_text_iter_get_offset(&it); }
 if(m->history->len) { Snapshot *old=g_ptr_array_index(m->history,m->position);for(guint i=0;i<m->diff.count;i++) if(m->resolutions[i]==old->resolutions[i]) { const gchar *a=g_utf8_offset_to_pointer(s->text,s->positions[2*i]),*z=g_utf8_offset_to_pointer(s->text,s->positions[2*i+1]),*oa=g_utf8_offset_to_pointer(old->text,old->positions[2*i]),*oz=g_utf8_offset_to_pointer(old->text,old->positions[2*i+1]);if(z-a!=oz-oa || memcmp(a,oa,z-a)) m->resolutions[i]=3; } }
 if(m->diff.count) memcpy(s->resolutions,m->resolutions,m->diff.count);
 g_ptr_array_add(m->history,s);m->position=m->history->len-1;
 /* Bound full-buffer history memory to roughly 32 MiB. */
 size_t bytes=0;
 for(guint i=0;i<m->history->len;i++) bytes+=sizeof(Snapshot)+strlen(((Snapshot*)g_ptr_array_index(m->history,i))->text)+1+m->diff.count*(2*sizeof(gint)+sizeof(guchar));
 while((bytes>32*1024*1024 || m->history->len>512) && m->history->len>1) {
  Snapshot *first=g_ptr_array_index(m->history,0);bytes-=sizeof(Snapshot)+strlen(first->text)+1+m->diff.count*(2*sizeof(gint)+sizeof(guchar));
  g_ptr_array_remove_index(m->history,0);m->position--;
 }
 resolution_rows(m);state(m);
}
static void changed(GtkTextBuffer *buffer,gpointer data) {
 (void)buffer;Merge *m=data;
 if(m->changing) return;
 if(m->user_action) m->pending=TRUE;else record(m);
}
static void action_begin(GtkTextBuffer *buffer,gpointer data) { (void)buffer;((Merge*)data)->user_action=TRUE; }
static void action_end(GtkTextBuffer *buffer,gpointer data) {
 (void)buffer;Merge *m=data;m->user_action=FALSE;
 if(m->pending) { m->pending=FALSE;if(!m->changing) record(m); }
}
static void restore(Merge *m,guint position) {
 m->history_serial++;
 Snapshot *s=g_ptr_array_index(m->history,position);m->changing=TRUE;
 gtk_text_buffer_set_text(result(m),s->text,-1);
 for(size_t i=0;i<2*m->diff.count;i++) { GtkTextIter it;gtk_text_buffer_get_iter_at_offset(result(m),&it,s->positions[i]);gtk_text_buffer_move_mark(result(m),m->marks[i],&it); }
 if(m->diff.count) memcpy(m->resolutions,s->resolutions,m->diff.count);m->position=position;m->changing=FALSE;resolution_rows(m);state(m);
}
static void undo(GtkWidget *w,gpointer data) { (void)w;Merge *m=data;if(!m->busy && m->position) restore(m,m->position-1); }
static void redo(GtkWidget *w,gpointer data) { (void)w;Merge *m=data;if(!m->busy && m->position+1<m->history->len) restore(m,m->position+1); }
static void history_end(Merge *m,gboolean redo_all) {
 if(!m->busy && m->history->len) { guint target=redo_all ? m->history->len-1 : 0;if(target!=m->position) restore(m,target); }
}
static void history_preview(GtkTreeSelection *selection,gpointer view) {
 GtkTreeModel *model;GtkTreeIter iter;if(!gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 gchar *text=NULL;gtk_tree_model_get(model,&iter,4,&text,-1);gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),text ? text : "",-1);g_free(text);
}
static void history_dialog(GtkWidget *widget,gpointer data) {
 (void)widget;Merge *m=data;if(m->busy || !m->history->len) return;
 GtkWidget *window=m->window;g_object_ref(window);guint64 serial=m->history_serial;
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Result History",GTK_WINDOW(window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Restore selected state",GTK_RESPONSE_APPLY,NULL);g_object_ref(dialog);gtk_widget_set_name(dialog,"insight-diff-history");gtk_window_set_default_size(GTK_WINDOW(dialog),720,500);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));gtk_container_set_border_width(GTK_CONTAINER(box),8);
 GtkWidget *note=gtk_label_new("Select a retained state to preview. Restore keeps later states available for Redo.");gtk_label_set_line_wrap(GTK_LABEL(note),TRUE);gtk_box_pack_start(GTK_BOX(box),note,FALSE,FALSE,4);
 GtkListStore *model=gtk_list_store_new(5,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);
 for(guint i=0;i<m->history->len;i++) {
  Snapshot *snapshot=g_ptr_array_index(m->history,i);gchar *number=g_strdup_printf("%u",i+1),*bytes=g_strdup_printf("%zu",strlen(snapshot->text));
  const gchar *end=g_utf8_offset_to_pointer(snapshot->text,MIN((glong)80,g_utf8_strlen(snapshot->text,-1)));gchar *summary=g_strndup(snapshot->text,end-snapshot->text);for(gchar *p=summary;*p;p++) if(*p=='\r'||*p=='\n'||*p=='\t') *p=' ';
  gboolean saved=!strcmp(snapshot->text,m->saved);const gchar *status=i==m->position ? (saved ? "Current / Saved" : "Current") : (saved ? "Saved" : "");
  gchar *label=g_strconcat(number," — ",insight_tr(status),NULL);GtkTreeIter iter;gtk_list_store_append(model,&iter);gtk_list_store_set(model,&iter,0,i,1,label,2,bytes,3,summary,4,snapshot->text,-1);g_free(label);g_free(number);g_free(bytes);g_free(summary);
 }
 GtkWidget *tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));g_object_unref(model);gtk_widget_set_name(tree,"insight-diff-history-list");
 const gchar *titles[]={"State","Bytes","Text preview"};for(guint i=0;i<3;i++) gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree),-1,titles[i],gtk_cell_renderer_text_new(),"text",i+1,NULL);
 GtkWidget *list_scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(list_scroll),tree);gtk_box_pack_start(GTK_BOX(box),list_scroll,TRUE,TRUE,4);
 GtkWidget *preview=gtk_text_view_new();gtk_text_view_set_editable(GTK_TEXT_VIEW(preview),FALSE);gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(preview),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(preview),TRUE);gtk_widget_set_name(preview,"insight-diff-history-preview");
 GtkWidget *preview_scroll=gtk_scrolled_window_new(NULL,NULL);gtk_widget_set_size_request(preview_scroll,-1,150);gtk_container_add(GTK_CONTAINER(preview_scroll),preview);gtk_box_pack_start(GTK_BOX(box),preview_scroll,TRUE,TRUE,4);
 GtkTreeSelection *selection=g_object_ref(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)));g_signal_connect_object(selection,"changed",G_CALLBACK(history_preview),preview,0);GtkTreePath *path=gtk_tree_path_new_from_indices(m->position,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree),path,NULL,FALSE,0,0);gtk_tree_path_free(path);gtk_widget_show_all(dialog);
 gint response=gtk_dialog_run(GTK_DIALOG(dialog));m=g_object_get_data(G_OBJECT(window),"insight-diff-state");
 if(response==GTK_RESPONSE_APPLY && m && !m->busy && serial==m->history_serial) { GtkTreeModel *selected_model;GtkTreeIter iter;if(gtk_tree_selection_get_selected(selection,&selected_model,&iter)) { guint index;gtk_tree_model_get(selected_model,&iter,0,&index,-1);if(index<m->history->len && index!=m->position) restore(m,index); } }
 if(response==GTK_RESPONSE_APPLY && m && serial!=m->history_serial) gtk_label_set_text(GTK_LABEL(m->replace_status),"History changed; reopen Result History.");
 gtk_widget_destroy(dialog);g_object_unref(selection);g_object_unref(dialog);g_object_unref(window);
}
static void navigate(Merge *m,gint direction);
static void take(GtkWidget *,gpointer);
static void take_all(GtkWidget *,gpointer);
static void find_text(Merge *m,gboolean backwards);
static void view_options(GtkWidget *,gpointer);
static void edit_source(Merge *,guint);
