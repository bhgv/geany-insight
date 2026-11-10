/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Bookmark navigation and editing.
 * RU: Навигация по закладкам и редактирование. */

static void bookmark_go(GtkWidget *widget,gpointer data) {
 (void)data;const gchar *query=gtk_entry_get_text(GTK_ENTRY(wb.marks_entry));gchar *name=NULL;
 if(widget==wb.marks_entry) {
  if(!*query) { bookmark_add(NULL,NULL);return; }
  if(insight_notes_get(notes_for(INSIGHT_BOOKMARK),INSIGHT_BOOKMARK,query)) name=g_strdup(query);
  else if(strspn(query,"0123456789")==strlen(query)) {
   guint64 line=g_ascii_strtoull(query,NULL,10);GeanyDocument *doc=document_get_current();
   if(doc && line>0 && line<=(guint64)sci_get_line_count(doc->editor->sci)) { navqueue_goto_line(doc,doc,(gint)line);SSM(doc->editor->sci,SCI_VERTICALCENTRECARET,0,0);document_grab_focus(doc); }
   else ui_set_statusbar(TRUE,"Bookmark line number is outside the current file");
   return;
  } else {
   InsightNotes *source=notes_for(INSIGHT_BOOKMARK);guint count=0;
   for(guint i=0;i<source->items->len;i++) { InsightNote *note=g_ptr_array_index(source->items,i);if(note->kind==INSIGHT_BOOKMARK && insight_lookup_text_match(note->name,query)) { count++;g_free(name);name=g_strdup(note->name); } }
   if(count>1) { g_free(name);ui_set_statusbar(TRUE,"Several bookmarks match; select one in the list");return; }
  }
 } else { name=selected_note(INSIGHT_BOOKMARK);if(!name) name=bookmark_cursor_note();if(!name && gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.marks_view)))>1) return;
  if(!name && insight_notes_get(notes_for(INSIGHT_BOOKMARK),INSIGHT_BOOKMARK,query)) name=g_strdup(query);
 }
 if(name) { bookmark_open_note(insight_notes_get(notes_for(INSIGHT_BOOKMARK),INSIGHT_BOOKMARK,name));g_free(name); }else bookmark_add(NULL,NULL);
}
static void clip_capture(GtkWidget *widget,gpointer data) {
 (void)widget; gboolean cut=GPOINTER_TO_INT(data); GeanyDocument *doc=document_get_current(); if(!doc) return;
 if(cut && SSM(doc->editor->sci,SCI_GETREADONLY,0,0)) { ui_set_statusbar(TRUE,"Current source is read-only"); return; }
 gsize selected_length=sci_get_selected_text_length2(doc->editor->sci);
 if(selected_length>65536) { ui_set_statusbar(TRUE,"Clip selection exceeds 64 KiB"); return; }
 gchar *text=sci_get_selection_contents(doc->editor->sci);
 if(text && strlen(text)!=selected_length) { g_free(text); ui_set_statusbar(TRUE,"Clips cannot contain NUL bytes"); return; }
 if(!text || !*text) { g_free(text); ui_set_statusbar(TRUE,"Select source text to copy into a clip"); return; }
 const char *input=gtk_entry_get_text(GTK_ENTRY(wb.clips_entry)); gchar *name=*input ? g_strdup(input) : g_strdup_printf("Clip-%"G_GINT64_FORMAT,g_get_real_time()); GError *error=NULL;
 InsightNotes *candidate=notes_clone(INSIGHT_CLIP,&error);gboolean saved=FALSE;
 if(candidate) { if(insight_notes_set(candidate,INSIGHT_CLIP,name,NULL,0,text,&error)) saved=notes_commit(candidate,INSIGHT_CLIP);else insight_notes_free(candidate); }
 if(cut && saved) { sci_start_undo_action(doc->editor->sci); sci_replace_sel(doc->editor->sci,""); sci_end_undo_action(doc->editor->sci); }
 if(error) ui_set_statusbar(TRUE,"Clip: %s",error->message);
 g_clear_error(&error); g_free(text); g_free(name);
}
static void clip_paste(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; gchar *name=selected_note(INSIGHT_CLIP); const InsightNote *note=name ? insight_notes_get(notes_for(INSIGHT_CLIP),INSIGHT_CLIP,name) : NULL; GeanyDocument *doc=document_get_current();
 if(note && doc && !SSM(doc->editor->sci,SCI_GETREADONLY,0,0)) { if(!insight_paste_text(doc->editor->sci,note->text)) { sci_start_undo_action(doc->editor->sci); sci_replace_sel(doc->editor->sci,note->text); sci_end_undo_action(doc->editor->sci); } document_grab_focus(doc); }
 else ui_set_statusbar(TRUE,"Select a clip and a writable source document");
 g_free(name);
}
static void clip_insert_template(const InsightNote *note) {
 guint64 generation=wb.clips_generation;
 GeanyDocument *doc=document_get_current();
 if(!doc || !note || SSM(doc->editor->sci,SCI_GETREADONLY,0,0)) { ui_set_statusbar(TRUE,"Select a clip and a writable source document"); return; }
 ScintillaObject *sci=doc->editor->sci;
 if(sci_get_selection_mode(sci)!=SC_SEL_STREAM || SSM(sci,SCI_GETSELECTIONS,0,0)!=1) { ui_set_statusbar(TRUE,"Template insertion requires a single normal selection"); return; }
 if(sci_get_selected_text_length2(sci)>65536) { ui_set_statusbar(TRUE,"Clip selection exceeds 64 KiB"); return; }
 gchar *language=clip_current_language(); gboolean compatible=insight_note_matches_language(note,language); g_free(language);
 if(!compatible) { ui_set_statusbar(TRUE,"This template is not available for the current language profile"); return; }
 gchar **allowed_languages=g_strdupv(note->languages); gchar *body=g_strdup(note->text),*selection=sci_get_selection_contents(sci); GError *error=NULL;
 if(!selection || strlen(selection)!=(gsize)sci_get_selected_text_length2(sci)) { g_free(selection); g_free(body); g_strfreev(allowed_languages); ui_set_statusbar(TRUE,"Clips cannot contain NUL bytes"); return; }
 guint id=doc->id; guint64 version=selection_state(sci)->text_version; gint begin=sci_get_selection_start(sci),end=sci_get_selection_end(sci),eol_mode=sci_get_eol_mode(sci);
 GHashTable *values=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free); GDateTime *now=g_date_time_new_now_local();
 g_hash_table_insert(values,g_strdup("date"),g_date_time_format(now,"%Y-%m-%d")); g_hash_table_insert(values,g_strdup("time"),g_date_time_format(now,"%H:%M:%S")); g_date_time_unref(now);
 g_hash_table_insert(values,g_strdup("file"),g_strdup(doc->file_name ? doc->file_name : "")); g_hash_table_insert(values,g_strdup("filename"),doc->file_name ? g_path_get_basename(doc->file_name) : g_strdup(""));
 g_hash_table_insert(values,g_strdup("project"),g_strdup(app->project && app->project->name ? app->project->name : "")); g_hash_table_insert(values,g_strdup("selection"),selection);
 GPtrArray *variables=insight_snippet_variables(body,&error); gboolean accepted=variables!=NULL,keep_fields=FALSE; GtkWidget *dialog=NULL,*editable=NULL; GPtrArray *entries=g_ptr_array_new();
 if(variables) {
  for(guint i=0;i<variables->len;i++) { const gchar *variable=g_ptr_array_index(variables,i); if(g_hash_table_contains(values,variable)) continue;
   if(!dialog) { dialog=gtk_dialog_new_with_buttons("Template parameters",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Insert",GTK_RESPONSE_ACCEPT,NULL); gtk_window_set_default_size(GTK_WINDOW(dialog),500,350); GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL),*grid=gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(grid),6); gtk_grid_set_column_spacing(GTK_GRID(grid),12); gtk_container_set_border_width(GTK_CONTAINER(grid),12); gtk_container_add(GTK_CONTAINER(scroll),grid); gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),scroll,TRUE,TRUE,0); g_object_set_data(G_OBJECT(dialog),"grid",grid); }
   GtkWidget *grid=g_object_get_data(G_OBJECT(dialog),"grid"),*label=gtk_label_new(variable),*entry=gtk_entry_new(); g_object_set_data(G_OBJECT(label),"insight-no-i18n",GINT_TO_POINTER(1)); gchar *widget_name=g_strconcat("insight-template-variable-",variable,NULL); gtk_widget_set_name(entry,widget_name); g_free(widget_name); gtk_entry_set_max_length(GTK_ENTRY(entry),65536); gtk_entry_set_activates_default(GTK_ENTRY(entry),TRUE); gtk_widget_set_hexpand(entry,TRUE); g_object_set_data_full(G_OBJECT(entry),"variable",g_strdup(variable),g_free); gtk_grid_attach(GTK_GRID(grid),label,0,(gint)entries->len,1,1); gtk_grid_attach(GTK_GRID(grid),entry,1,(gint)entries->len,1,1); g_ptr_array_add(entries,entry);
  }
  if(dialog) { editable=gtk_check_button_new_with_label("Keep template fields editable (Tab / Shift+Tab)");gtk_widget_set_name(editable,"insight-template-editable-fields");gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),editable,FALSE,FALSE,6);gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT); gtk_widget_show_all(dialog); gtk_widget_grab_focus(g_ptr_array_index(entries,0)); accepted=gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT;
   if(accepted) for(guint i=0;i<entries->len;i++) { GtkWidget *entry=g_ptr_array_index(entries,i); g_hash_table_insert(values,g_strdup(g_object_get_data(G_OBJECT(entry),"variable")),g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)))); }
   keep_fields=accepted && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(editable));gtk_widget_destroy(dialog);
  }
 }
 if(accepted && notes_scope_current(INSIGHT_CLIP,generation)) {
  doc=document_find_by_id(id);
  gchar *current_language=clip_current_language(); InsightNote allowed_snapshot={.languages=allowed_languages}; gboolean language_allowed=insight_note_matches_language(&allowed_snapshot,current_language); g_free(current_language);
  if(!language_allowed) ui_set_statusbar(TRUE,"This template is not available for the current language profile");
  else if(!doc || doc!=document_get_current() || selection_state(doc->editor->sci)->text_version!=version || sci_get_selection_start(doc->editor->sci)!=begin || sci_get_selection_end(doc->editor->sci)!=end || sci_get_selection_mode(doc->editor->sci)!=SC_SEL_STREAM || SSM(doc->editor->sci,SCI_GETSELECTIONS,0,0)!=1 || SSM(doc->editor->sci,SCI_GETREADONLY,0,0) || sci_get_eol_mode(doc->editor->sci)!=eol_mode) ui_set_statusbar(TRUE,"Source changed while entering template parameters; insert again");
  else { gsize cursor=0;GArray *fields=NULL;GHashTable *editable_names=g_hash_table_new(g_str_hash,g_str_equal);
   if(keep_fields) for(guint i=0;i<variables->len;i++) { const gchar *variable=g_ptr_array_index(variables,i);if(strcmp(variable,"date") && strcmp(variable,"time") && strcmp(variable,"file") && strcmp(variable,"filename") && strcmp(variable,"project") && strcmp(variable,"selection")) g_hash_table_add(editable_names,(gpointer)variable); }
   gchar *text=insight_snippet_expand_fields(body,values,eol_mode==SC_EOL_CRLF ? "\r\n" : (eol_mode==SC_EOL_CR ? "\r" : "\n"),editable_names,&cursor,&fields,&error);g_hash_table_unref(editable_names);
   if(text) { sci=doc->editor->sci;insight_template_fields_begin(sci,NULL,0,0); sci_start_undo_action(sci); sci_replace_sel(sci,text); sci_set_current_position(sci,begin+(gint)cursor,FALSE); sci_end_undo_action(sci);if(keep_fields) insight_template_fields_begin(sci,fields,(gsize)begin,cursor); document_grab_focus(doc); g_free(text); }
   g_clear_pointer(&fields,g_array_unref);
  }
 }
 if(error) ui_set_statusbar(TRUE,"Template: %s",insight_tr(error->message));
 g_clear_error(&error); g_clear_pointer(&variables,g_ptr_array_unref); g_ptr_array_unref(entries); g_hash_table_unref(values); g_strfreev(allowed_languages); g_free(body);
}
static void clip_template(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;gchar *name=selected_note(INSIGHT_CLIP);const InsightNote *note=name ? insight_notes_get(notes_for(INSIGHT_CLIP),INSIGHT_CLIP,name) : NULL;clip_insert_template(note);g_free(name);
}
void insight_workbench_template_selected(GeanyEditor *editor,const gchar *name) {
 if(!wb.template_pending || !editor) return;
 wb.template_pending=FALSE;const InsightFiletypeOptions *typing=insight_languages_editing_options(editor);if(typing && !typing->allow_snippets) return;GeanyDocument *doc=document_get_current();ScintillaObject *sci=editor->sci;
 if(!doc || doc->editor!=editor || doc->id!=wb.template_doc || wb.template_generation!=wb.clips_generation || selection_state(sci)->text_version!=wb.template_version || sci_get_current_position(sci)!=wb.template_end || sci_get_selection_start(sci)!=wb.template_end || sci_get_selection_end(sci)!=wb.template_end || SSM(sci,SCI_GETREADONLY,0,0)) return;
 gchar *prefix=sci_get_contents_range(sci,wb.template_begin,wb.template_end);gboolean unchanged=!g_strcmp0(prefix,wb.template_prefix);g_free(prefix);if(!unchanged || !name || !g_str_has_prefix(name,wb.template_prefix)) return;
 const InsightNote *note=insight_notes_get(notes_for(INSIGHT_CLIP),INSIGHT_CLIP,name);gchar *language=clip_current_language();gboolean eligible=insight_note_matches_language(note,language);g_free(language);if(!eligible) return;
 gint before=wb.template_end;guint id=doc->id;guint64 version=wb.template_version;
 SSM(sci,SCI_SETSEL,wb.template_begin,wb.template_end);clip_insert_template(note);
 doc=document_find_by_id(id);if(doc && doc==document_get_current() && selection_state(doc->editor->sci)->text_version==version) sci_set_current_position(doc->editor->sci,before,FALSE);
}
GPtrArray *insight_workbench_completion_templates(GeanyEditor *editor,const gchar *prefix,gboolean sensitive,guint64 *generation) {
 GPtrArray *names=g_ptr_array_new_with_free_func(g_free);if(generation) *generation=wb.clips_generation;
 const InsightFiletypeOptions *typing=editor ? insight_languages_editing_options(editor):NULL;if(!wb.alive || !editor || editor->document!=document_get_current() || !prefix || !*prefix || strlen(prefix)>256 || (typing && !typing->allow_snippets)) return names;
 gchar *language=clip_current_language(),*folded=g_utf8_casefold(prefix,-1);InsightNotes *notes=notes_for(INSIGHT_CLIP);
 for(guint i=0;notes && i<notes->items->len && names->len<64;i++) {InsightNote *note=g_ptr_array_index(notes->items,i);if(note->kind!=INSIGHT_CLIP || strpbrk(note->name,"\n\r\t") || !insight_note_matches_language(note,language)) continue;gchar *name=g_utf8_casefold(note->name,-1);if(g_str_has_prefix(sensitive ? note->name:name,sensitive ? prefix:folded)) g_ptr_array_add(names,g_strdup(note->name));g_free(name);}
 g_free(language);g_free(folded);return names;
}
gboolean insight_workbench_completion_template_insert(GeanyEditor *editor,const gchar *name,gint begin,gint end,guint64 generation) {
 if(!wb.alive || !editor || generation!=wb.clips_generation || begin<0 || end<begin || end-begin>256) return FALSE;GeanyDocument *doc=document_get_current();ScintillaObject *sci=editor->sci;const InsightFiletypeOptions *typing=insight_languages_editing_options(editor);
 if(!doc || doc->editor!=editor || (typing && !typing->allow_snippets) || sci_get_current_position(sci)!=end || sci_has_selection(sci) || SSM(sci,SCI_GETSELECTIONS,0,0)!=1 || SSM(sci,SCI_GETREADONLY,0,0)) return FALSE;
 const InsightNote *note=insight_notes_get(notes_for(INSIGHT_CLIP),INSIGHT_CLIP,name);gchar *language=clip_current_language();gboolean allowed=note && insight_note_matches_language(note,language);g_free(language);if(!allowed) return FALSE;
 guint id=doc->id;guint64 version=selection_state(sci)->text_version;SSM(sci,SCI_SETSEL,begin,end);clip_insert_template(note);doc=document_find_by_id(id);if(doc && doc==document_get_current() && selection_state(doc->editor->sci)->text_version==version) sci_set_current_position(doc->editor->sci,end,FALSE);return TRUE;
}
gboolean insight_workbench_complete_template(GeanyEditor *editor,gint position) {
 if(!wb.alive || !wb.notes || !editor) return FALSE;
 const InsightFiletypeOptions *typing=insight_languages_editing_options(editor);if(typing && !typing->allow_snippets) return FALSE;
 GeanyDocument *doc=document_get_current();ScintillaObject *sci=editor->sci;
 if(!doc || doc->editor!=editor || SSM(sci,SCI_GETREADONLY,0,0) || sci_get_selection_start(sci)!=position || sci_get_selection_end(sci)!=position || SSM(sci,SCI_GETSELECTIONS,0,0)!=1 || position<1) return FALSE;
 gint begin=(gint)SSM(sci,SCI_WORDSTARTPOSITION,position,TRUE);if(position-begin<1 || position-begin>256 || SSM(sci,SCI_WORDENDPOSITION,position,TRUE)>position) return FALSE;
 gchar *prefix=sci_get_contents_range(sci,begin,position);if(!prefix || !g_utf8_validate(prefix,-1,NULL)) { g_free(prefix);return FALSE; }
 GString *names=g_string_new(NULL);const InsightNote *exact=NULL;gchar *language=clip_current_language();InsightNotes *notes=notes_for(INSIGHT_CLIP);
 for(guint i=0;i<notes->items->len;i++) { InsightNote *note=g_ptr_array_index(notes->items,i);if(note->kind!=INSIGHT_CLIP || !g_str_has_prefix(note->name,prefix) || strpbrk(note->name,"\n\r\t") || !insight_note_matches_language(note,language)) continue;if(names->len) g_string_append_c(names,'\n');g_string_append(names,note->name);if(!strcmp(note->name,prefix)) exact=note; }
 g_free(language);if(!names->len) { g_string_free(names,TRUE);g_free(prefix);return FALSE; }
 wb.template_pending=TRUE;wb.template_doc=doc->id;wb.template_begin=begin;wb.template_end=position;wb.template_version=selection_state(sci)->text_version;wb.template_generation=wb.clips_generation;g_free(wb.template_prefix);wb.template_prefix=prefix;
 if(exact) insight_workbench_template_selected(editor,exact->name);
 else { gint separator=(gint)SSM(sci,SCI_AUTOCGETSEPARATOR,0,0);SSM(sci,SCI_AUTOCSETSEPARATOR,'\n',0);SSM(sci,SCI_USERLISTSHOW,INSIGHT_TEMPLATE_COMPLETION_LIST,(sptr_t)names->str);SSM(sci,SCI_AUTOCSETSEPARATOR,separator,0); }
 g_string_free(names,TRUE);return TRUE;
}
static void template_language_toggled(GtkCellRendererToggle *renderer,gchar *path,gpointer data) {
 (void)renderer; GtkListStore *store=data; GtkTreeIter iter; if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store),&iter,path)) { gboolean active=FALSE; gtk_tree_model_get(GTK_TREE_MODEL(store),&iter,0,&active,-1); gtk_list_store_set(store,&iter,0,!active,-1); }
}
static GtkWidget *template_language_editor(GtkWidget *box,const InsightNote *note) {
 GtkListStore *store=gtk_list_store_new(2,G_TYPE_BOOLEAN,G_TYPE_STRING); GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store)); gtk_widget_set_name(view,"insight-template-languages");
 GtkCellRenderer *toggle=gtk_cell_renderer_toggle_new(); g_signal_connect_object(toggle,"toggled",G_CALLBACK(template_language_toggled),store,0); gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Allowed",toggle,"active",0,NULL)); gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Language profile",gtk_cell_renderer_text_new(),"text",1,NULL));
 gchar **names=insight_languages_names(); GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);
 for(guint pass=0;pass<2;pass++) { gchar **list=pass ? (note ? note->languages : NULL) : names;
  for(guint i=0;list && list[i];i++) { if(g_hash_table_contains(seen,list[i])) continue; gboolean active=FALSE; for(guint k=0;note && note->languages && note->languages[k];k++) if(!strcmp(note->languages[k],list[i])) active=TRUE;
   GtkTreeIter iter; gtk_list_store_append(store,&iter); gtk_list_store_set(store,&iter,0,active,1,list[i],-1); g_hash_table_add(seen,list[i]);
  }
 }
 g_hash_table_unref(seen); g_strfreev(names); g_object_unref(store); GtkWidget *label=gtk_label_new("Allowed language profiles — no selection means all languages"),*scroll=gtk_scrolled_window_new(NULL,NULL); gtk_label_set_line_wrap(GTK_LABEL(label),TRUE); gtk_box_pack_start(GTK_BOX(box),label,FALSE,FALSE,4); gtk_widget_set_size_request(scroll,-1,120); gtk_container_add(GTK_CONTAINER(scroll),view); gtk_box_pack_start(GTK_BOX(box),scroll,FALSE,TRUE,0); return view;
}
static gchar **template_language_values(GtkWidget *view) {
 GPtrArray *values=g_ptr_array_new(); GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view)); GtkTreeIter iter;
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gboolean active=FALSE; gchar *name=NULL; gtk_tree_model_get(model,&iter,0,&active,1,&name,-1); if(active) g_ptr_array_add(values,name); else g_free(name); }
 g_ptr_array_add(values,NULL); return (gchar **)g_ptr_array_free(values,FALSE);
}
static gboolean notes_commit(InsightNotes *,InsightNoteKind);
static void clip_new_template(GtkWidget *widget,gpointer data) {
 guint64 generation=wb.clips_generation;
 (void)widget; (void)data; GtkWidget *dialog=gtk_dialog_new_with_buttons("New code template",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Save",GTK_RESPONSE_ACCEPT,NULL),*box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)),*name=gtk_entry_new(),*view=gtk_text_view_new(),*scroll=gtk_scrolled_window_new(NULL,NULL);
 gtk_widget_set_name(name,"insight-template-name"); gtk_widget_set_name(view,"insight-template-body"); gtk_entry_set_max_length(GTK_ENTRY(name),256); gtk_entry_set_placeholder_text(GTK_ENTRY(name),"Template name"); gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),"// $date$ — $filename$\n$selection$$cursor$",-1); gtk_container_add(GTK_CONTAINER(scroll),view); gtk_widget_set_size_request(scroll,600,300); gtk_box_pack_start(GTK_BOX(box),name,FALSE,FALSE,6); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0); GtkWidget *hint=gtk_label_new("Variables: $date$, $time$, $file$, $filename$, $project$, $selection$, $cursor$. Custom: $name$. Literal dollar: $$."); gtk_label_set_line_wrap(GTK_LABEL(hint),TRUE); gtk_label_set_max_width_chars(GTK_LABEL(hint),70); gtk_box_pack_start(GTK_BOX(box),hint,FALSE,FALSE,6); GtkWidget *languages=template_language_editor(box,NULL); gtk_widget_show_all(dialog);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT && notes_scope_current(INSIGHT_CLIP,generation)) {
  GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)); GtkTextIter a,b; gtk_text_buffer_get_bounds(buffer,&a,&b); gchar *text=gtk_text_buffer_get_text(buffer,&a,&b,FALSE); const gchar *key=gtk_entry_get_text(GTK_ENTRY(name)); GError *error=NULL;
  if(insight_notes_get(notes_for(INSIGHT_CLIP),INSIGHT_CLIP,key)) ui_set_statusbar(TRUE,"A clip with this name already exists");
  else { InsightNotes *added=insight_notes_new();
   gchar **allowed=template_language_values(languages);
   if(insight_notes_set(added,INSIGHT_CLIP,key,NULL,0,text,&error) && insight_notes_set_languages(added,key,(const gchar *const *)allowed,&error)) { InsightNotes *candidate=insight_notes_merge(notes_for(INSIGHT_CLIP),added,INSIGHT_CLIP,&error); if(candidate) notes_commit(candidate,INSIGHT_CLIP); }
   g_strfreev(allowed); insight_notes_free(added);
  }
  if(error) ui_set_statusbar(TRUE,"Template: %s",insight_tr(error->message));
  g_clear_error(&error); g_free(text);
 }
 gtk_widget_destroy(dialog);
}
static void clip_edit(GtkWidget *widget,gpointer data) {
 guint64 generation=wb.clips_generation;
 (void)widget; (void)data; gchar *name=selected_note(INSIGHT_CLIP); const InsightNote *note=name ? insight_notes_get(notes_for(INSIGHT_CLIP),INSIGHT_CLIP,name) : NULL; if(!note) { g_free(name); return; }
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Edit clip",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Save",GTK_RESPONSE_ACCEPT,NULL),*view=gtk_text_view_new(),*scroll=gtk_scrolled_window_new(NULL,NULL);
 gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),note->text,-1); gtk_container_add(GTK_CONTAINER(scroll),view); gtk_widget_set_size_request(scroll,600,350); gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),scroll); GtkWidget *languages=template_language_editor(gtk_dialog_get_content_area(GTK_DIALOG(dialog)),note); gtk_widget_show_all(dialog);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT && notes_scope_current(INSIGHT_CLIP,generation)) { GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)); GtkTextIter a,b; gtk_text_buffer_get_bounds(buffer,&a,&b); gchar *text=gtk_text_buffer_get_text(buffer,&a,&b,FALSE); GError *error=NULL;
  InsightNotes *candidate=insight_notes_rename(notes_for(INSIGHT_CLIP),INSIGHT_CLIP,name,name,&error); gchar **allowed=template_language_values(languages);
  if(candidate) { if(insight_notes_set(candidate,INSIGHT_CLIP,name,NULL,0,text,&error) && insight_notes_set_languages(candidate,name,(const gchar *const *)allowed,&error)) notes_commit(candidate,INSIGHT_CLIP); else insight_notes_free(candidate); }
  if(error) ui_set_statusbar(TRUE,"Clip: %s",insight_tr(error->message));
  g_strfreev(allowed); g_clear_error(&error); g_free(text); }
 gtk_widget_destroy(dialog); g_free(name);
}
static void note_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data); gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(view));gtk_tree_view_set_cursor(view,path,NULL,FALSE);
 if(kind==INSIGHT_BOOKMARK) bookmark_go(NULL,NULL); else clip_paste(NULL,NULL);
}
/* Commit a prepared snapshot before refreshing the panel. On write failure the
 * previous in-memory notes and selection remain available. Consumes candidate. */
static gboolean notes_commit(InsightNotes *candidate,InsightNoteKind kind) {
 if(kind==INSIGHT_BOOKMARK && !bookmarks_writable()) { insight_notes_free(candidate);return FALSE; }
 InsightNotes **slot=kind==INSIGHT_BOOKMARK && wb.marks_project ? &wb.project_marks : kind==INSIGHT_CLIP && wb.clips_project ? &wb.project_notes : &wb.notes;InsightNotes *previous=*slot;gboolean previous_failure=wb.marks_save_failed;*slot=candidate;
 if(!notes_save_kind(kind)) { *slot=previous;if(kind==INSIGHT_BOOKMARK) wb.marks_save_failed=previous_failure; insight_notes_free(candidate); return FALSE; }
 insight_notes_free(previous);
 if(kind==INSIGHT_BOOKMARK) { bookmark_clear_handles();for(guint i=0;i<documents_array->len;i++) bookmark_restore_document(g_ptr_array_index(documents_array,i));bookmark_list_reload(NULL,NULL); }
 notes_filter(NULL,GINT_TO_POINTER(kind));return TRUE;
}
/* document-close is emitted after the unsaved-file prompt accepts and before
 * the buffer is destroyed. Do not run the interactive deletion command here. */
static void bookmark_file_closed(GeanyDocument *doc) {
 if(!wb.alive || main_status.quitting || !wb.marks_delete_on_close || !doc || !doc->is_valid || !doc->file_name || !wb.notes || !bookmarks_writable()) return;
 InsightNotes *source=notes_for(INSIGHT_BOOKMARK);gboolean found=FALSE;
 for(guint i=0;i<source->items->len;i++) { InsightNote *note=g_ptr_array_index(source->items,i);if(note->kind==INSIGHT_BOOKMARK && !g_strcmp0(note->path,doc->file_name)) { found=TRUE;break; } }
 if(!found) return;
 GError *error=NULL;InsightNotes *candidate=notes_clone(INSIGHT_BOOKMARK,&error);
 if(candidate) {
  for(guint i=candidate->items->len;i>0;i--) { InsightNote *note=g_ptr_array_index(candidate->items,i-1);if(note->kind==INSIGHT_BOOKMARK && !g_strcmp0(note->path,doc->file_name)) insight_notes_remove(candidate,INSIGHT_BOOKMARK,note->name); }
  /* Commit clears registered handles. Also clear any native bookmark markers
   * so a reentrant refresh cannot capture the closing file's marks again. */
  if(notes_commit(candidate,INSIGHT_BOOKMARK)) SSM(doc->editor->sci,SCI_MARKERDELETEALL,1,0);
 }
 if(error) ui_set_statusbar(TRUE,"Note: %s",error->message);
 g_clear_error(&error);
}
static void note_rename(GtkWidget *widget,gpointer data) {
 (void)widget; InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data); gchar *name=selected_note(kind);
 if(!name) { ui_set_statusbar(TRUE,"Select a note to rename"); return; }
 guint64 generation=notes_generation(kind);
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Rename note",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Rename",GTK_RESPONSE_ACCEPT,NULL),*entry=gtk_entry_new();
 gtk_entry_set_text(GTK_ENTRY(entry),name); gtk_entry_set_activates_default(GTK_ENTRY(entry),TRUE); gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT); gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),entry,FALSE,FALSE,8); gtk_widget_show(entry);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT && notes_scope_current(kind,generation)) {
  GError *error=NULL; InsightNotes *candidate=insight_notes_rename(notes_for(kind),kind,name,gtk_entry_get_text(GTK_ENTRY(entry)),&error);
  if(candidate) { if(notes_commit(candidate,kind)) ui_set_statusbar(TRUE,"Note renamed"); }
  else ui_set_statusbar(TRUE,"Rename: %s",error ? error->message : "failed");
  g_clear_error(&error);
 }
 gtk_widget_destroy(dialog); g_free(name);
}
static void notes_transfer(GtkWidget *widget,gpointer data) {
 (void)widget; gint action=GPOINTER_TO_INT(data); gboolean exporting=(action&256)!=0;
 InsightNoteKind kind=(InsightNoteKind)(action&255);
 guint64 generation=notes_generation(kind);GError *prepare_error=NULL;InsightNotes *prepared=exporting ? insight_notes_select(notes_for(kind),kind,&prepare_error) : NULL;
 if(exporting && prepared && kind==INSIGHT_BOOKMARK && !(action&512)) {
  GPtrArray *names=selected_notes(kind);GHashTable *wanted=g_hash_table_new(g_str_hash,g_str_equal);
  for(guint i=0;i<names->len;i++) g_hash_table_add(wanted,g_ptr_array_index(names,i));
  for(guint i=prepared->items->len;i>0;i--) { InsightNote *note=g_ptr_array_index(prepared->items,i-1);if(!g_hash_table_contains(wanted,note->name)) insight_notes_remove(prepared,kind,note->name); }
  g_hash_table_unref(wanted);g_ptr_array_unref(names);
  if(!prepared->items->len) { insight_notes_free(prepared);ui_set_statusbar(TRUE,"Select bookmarks to export");return; }
 }
 if(exporting && !prepared) { ui_set_statusbar(TRUE,"Notes export: %s",prepare_error ? prepare_error->message : "failed");g_clear_error(&prepare_error);return; }
 gchar *export_root=kind==INSIGHT_BOOKMARK && wb.marks_project ? g_strdup(wb.project_marks_root) : kind==INSIGHT_BOOKMARK && wb.root ? utils_get_utf8_from_locale(wb.root) : NULL;
 GtkWidget *dialog=gtk_file_chooser_dialog_new(exporting ? "Export notes" : "Import notes",GTK_WINDOW(main_widgets.window),
  exporting ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,
  exporting ? "Export" : "Import",GTK_RESPONSE_ACCEPT,NULL);
 if(exporting) { gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),TRUE); gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),kind==INSIGHT_BOOKMARK ? "bookmarks.conf" : "clips.conf"); }
 GtkFileFilter *filter=gtk_file_filter_new(); gtk_file_filter_set_name(filter,"Insight notes (*.conf)"); gtk_file_filter_add_pattern(filter,"*.conf"); gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);
 filter=gtk_file_filter_new(); gtk_file_filter_set_name(filter,"All files"); gtk_file_filter_add_pattern(filter,"*"); gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT && notes_scope_current(kind,generation)) {
  gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)); GError *error=NULL; InsightNotes *snapshot=NULL; gboolean ok=FALSE;
  if(exporting) { snapshot=prepared;prepared=NULL;ok=insight_notes_save_portable(snapshot,path,export_root,&error); }
  else {
   gchar *root=kind==INSIGHT_BOOKMARK && wb.marks_project ? g_strdup(wb.project_marks_root) : wb.root ? utils_get_utf8_from_locale(wb.root) : NULL;snapshot=insight_notes_load_portable(path,root,&error);g_free(root);
   if(snapshot) {
    InsightNotes *merged=insight_notes_merge(notes_for(kind),snapshot,kind,&error);
    if(merged) ok=notes_commit(merged,kind);
   }
  }
  if(ok) ui_set_statusbar(TRUE,exporting ? "Notes exported" : "Notes imported; existing notes preserved");
  else if(error) ui_set_statusbar(TRUE,"Notes %s: %s",exporting ? "export" : "import",error->message);
  insight_notes_free(snapshot); g_clear_error(&error); g_free(path);
 }
 gtk_widget_destroy(dialog);insight_notes_free(prepared);g_free(export_root);
}
/* Save only these options: keep layout and per-panel appearance intact. */
static gboolean bookmark_options_save(gboolean confirm,gboolean close,gboolean preserve,GError **error) {
 GKeyFile *key=g_key_file_new();GStatBuf info;gboolean ok=TRUE;
 if(!g_stat(wb.prefs_path,&info)) {
  if(info.st_size>65536 || !g_key_file_load_from_file(key,wb.prefs_path,G_KEY_FILE_KEEP_COMMENTS,error)) ok=FALSE;
  else if(g_key_file_get_integer(key,"workbench","version",NULL)!=1) ok=FALSE;
 }
 if(!ok && !*error) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Workbench settings are invalid or too large");
 if(ok) {
  g_key_file_set_integer(key,"workbench","version",1);
  g_key_file_set_boolean(key,"workbench","confirm-bookmark-delete",confirm);
  g_key_file_set_boolean(key,"workbench","delete-bookmarks-on-close",close);
  g_key_file_set_boolean(key,"workbench","preserve-bookmarks",preserve);
  gsize length=0;gchar *text=g_key_file_to_data(key,&length,NULL),*directory=g_path_get_dirname(wb.prefs_path);
  if(length>65536) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Workbench settings are invalid or too large");ok=FALSE; }
  else if(g_mkdir_with_parents(directory,0700)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Workbench settings directory could not be created");ok=FALSE; }
  else { GFile *file=g_file_new_for_path(wb.prefs_path);ok=g_file_replace_contents(file,text,length,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,error);g_object_unref(file); }
  g_free(text);g_free(directory);
 }
 g_key_file_unref(key);return ok;
}
