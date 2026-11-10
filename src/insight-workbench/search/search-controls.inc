/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Workspace reindexing and search controls.
 * RU: Переиндексация рабочей области и управление поиском. */

static void reindex_root(GtkButton *button,gpointer data) {
 (void)button; (void)data;
 wb.force_parse=TRUE;if(!wb.root || !start_scan(wb.root)) ui_set_statusbar(TRUE,"Choose a readable source folder first");
}
static void toolbar_definition(GtkToolButton *button,gpointer data) { (void)button; go_definition(NULL,data); }
static void toolbar_focus(GtkToolButton *button,gpointer data) { (void)button; focus_browser(NULL,data); }
static guint text_search_flags(GtkWidget *sensitive,GtkWidget *word,GtkWidget *regex) {
 return (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sensitive)) ? INSIGHT_SEARCH_CASE : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(word)) ? INSIGHT_SEARCH_WORD : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(regex)) ? INSIGHT_SEARCH_REGEX : 0);
}
static void project_text_search(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; wb.relation_setting=TRUE;
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE);
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),3);
 gtk_entry_set_text(GTK_ENTRY(wb.usage_entry),gtk_entry_get_text(GTK_ENTRY(wb.project_query)));
 wb.relation_setting=FALSE; relation_controls(NULL,NULL);
 focus_search_results(NULL,NULL);
}
static void editor_history_record(const gchar *query) {
 if(!wb.editor_history_combo || !query || !*query) return;
 gchar *saved=g_strdup(query);GtkTreeModel *model=gtk_combo_box_get_model(GTK_COMBO_BOX(wb.editor_history_combo));GtkTreeIter iter;gint position=0;wb.editor_setting=TRUE;
 for(gboolean ok=gtk_tree_model_get_iter_first(model,&iter);ok;ok=gtk_tree_model_iter_next(model,&iter),position++) { gchar *old=NULL;gtk_tree_model_get(model,&iter,0,&old,-1);gboolean same=!g_strcmp0(old,saved);g_free(old);if(same) { gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(wb.editor_history_combo),position);break; } }
 gtk_combo_box_text_prepend_text(GTK_COMBO_BOX_TEXT(wb.editor_history_combo),saved);while(gtk_tree_model_iter_n_children(model,NULL)>32) gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(wb.editor_history_combo),32);
 gtk_entry_set_text(GTK_ENTRY(wb.editor_query),saved);wb.editor_setting=FALSE;g_free(saved);
}
static void editor_text_search(GtkWidget *,gpointer);
static void editor_search_sync(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(wb.editor_setting || !wb.editor_query) return;
 g_free(search_data.text);g_free(search_data.original_text);search_data.text=g_strdup(gtk_entry_get_text(GTK_ENTRY(wb.editor_query)));search_data.original_text=g_strdup(search_data.text);
 search_data.flags=(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_case)) ? GEANY_FIND_MATCHCASE : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_word)) ? GEANY_FIND_WHOLEWORD : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_regex)) ? GEANY_FIND_REGEXP : 0);search_data.search_bar=FALSE;
 if(GTK_IS_ENTRY(widget) && wb.editor_incremental && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_incremental)) && *search_data.text) editor_text_search(NULL,GINT_TO_POINTER(2));
}
static void editor_search_all(GtkButton *button,gpointer data) {
 (void)button;editor_search_sync(NULL,NULL);GeanyDocument *doc=document_get_current();if(!doc || !search_data.text || !*search_data.text) return;
 if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_regex)) && search_data.text[0]=='~') { ui_set_statusbar(TRUE,"Find All and Mark use literal text or regular expressions; disable fuzzy matching first");return; }
 gchar *pattern=g_strdup(search_data.text),*label=g_strdup(search_data.original_text);GeanyFindFlags flags=search_data.flags;
 if(flags&GEANY_FIND_REGEXP) {
  flags|=GEANY_FIND_MULTILINE;
  if(flags&GEANY_FIND_WHOLEWORD) { gchar *bounded=g_strconcat("(?<![\\p{L}\\p{N}\\p{M}_])(?:",pattern,")(?![\\p{L}\\p{N}\\p{M}_])",NULL);g_free(pattern);pattern=bounded;flags&=~GEANY_FIND_WHOLEWORD; }
 }
 if(GPOINTER_TO_INT(data)==2) search_mark_all(doc,pattern,flags);
 else search_find_usage(pattern,label,flags,GPOINTER_TO_INT(data)==1);
 g_free(pattern);g_free(label);
}
gboolean insight_workbench_show_file_search(void) {
 if(!wb.alive || !wb.editor_query) return FALSE;
 GeanyDocument *doc=document_get_current();gchar *selection=doc ? editor_get_default_selection(doc->editor,search_prefs.use_current_word,NULL) : NULL;
 if(selection && !strchr(selection,'\n') && !strchr(selection,'\r')) { gtk_entry_set_text(GTK_ENTRY(wb.editor_query),selection); }
 g_free(selection);
 gtk_widget_show(wb.editor_search_row);gtk_widget_grab_focus(wb.editor_query);gtk_editable_select_region(GTK_EDITABLE(wb.editor_query),0,-1);return TRUE;
}
gboolean insight_workbench_file_find_again(gboolean backwards) {
 if(!wb.alive || !wb.editor_query) return FALSE;
 if(search_data.text) {
  wb.editor_setting=TRUE;gtk_entry_set_text(GTK_ENTRY(wb.editor_query),search_data.text);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_case),!!(search_data.flags&GEANY_FIND_MATCHCASE));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_word),!!(search_data.flags&GEANY_FIND_WHOLEWORD));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_regex),!!(search_data.flags&GEANY_FIND_REGEXP));wb.editor_setting=FALSE;
 }
 editor_text_search(NULL,GINT_TO_POINTER(backwards ? -1 : 1));return TRUE;
}
static void editor_text_search(GtkWidget *widget,gpointer data) {
 gboolean forward=GPOINTER_TO_INT(data)!=-1; GeanyDocument *doc=document_get_current();
 if(GTK_IS_BUTTON(widget) && doc && wb.editor_load_word && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_load_word))) { gchar *word=editor_get_default_selection(doc->editor,TRUE,NULL);if(word) { wb.editor_setting=TRUE;gtk_entry_set_text(GTK_ENTRY(wb.editor_query),word);wb.editor_setting=FALSE;g_free(word); } }
 editor_search_sync(NULL,NULL);
 if(!doc) { ui_set_statusbar(TRUE,"No active document"); return; }
 gint length=sci_get_length(doc->editor->sci);
 if(length<0) { ui_set_statusbar(TRUE,"Cannot read the active document"); return; }
 gchar *text=sci_get_contents(doc->editor->sci,length+1),*digest=g_compute_checksum_for_data(G_CHECKSUM_SHA256,(const guchar *)text,(gsize)length);
 guint flags=text_search_flags(wb.editor_case,wb.editor_word,wb.editor_regex);
 const char *query=gtk_entry_get_text(GTK_ENTRY(wb.editor_query)); gsize begin,end,a=(gsize)SSM(doc->editor->sci,SCI_GETSELECTIONSTART,0,0),b=(gsize)SSM(doc->editor->sci,SCI_GETSELECTIONEND,0,0),from=GPOINTER_TO_INT(data)==2 ? a : (forward ? b : a); gboolean wrapped=FALSE;
 gboolean repeat=wb.editor_find_id==doc->id && wb.editor_find_flags==flags && !g_strcmp0(query,wb.editor_find_query) && !g_strcmp0(digest,wb.editor_find_digest) && a==wb.editor_find_begin && b==wb.editor_find_end;
 gboolean forced_wrap=FALSE;
 if(repeat && a==b) {
  if(wb.editor_find_match_end>b) { if(forward) from=wb.editor_find_match_end; }
  else if((forward && b==(gsize)length) || (!forward && !a)) { forced_wrap=TRUE;from=forward ? 0 : length; }
  else from=(gsize)SSM(doc->editor->sci,forward ? SCI_POSITIONAFTER : SCI_POSITIONBEFORE,forward ? b : a,0);
 }
 if(forced_wrap && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_wrap))) { ui_set_statusbar(TRUE,"Find: no match before file boundary");g_free(text);g_free(digest);return; }
 GError *error=NULL;
 if(insight_context_find_file(text,(gsize)length,query,from,forward,flags,&begin,&end,&wrapped,&error)) {
  wrapped|=forced_wrap;
  if(wrapped && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_wrap))) { ui_set_statusbar(TRUE,"Find: no match before file boundary");g_clear_error(&error);g_free(text);g_free(digest);return; }
  SSM(doc->editor->sci,SCI_SETSEL,begin,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_select)) ? end : begin); decorate_match(doc->editor->sci,begin,end); SSM(doc->editor->sci,SCI_VERTICALCENTRECARET,0,0);
  g_free(wb.editor_find_query); g_free(wb.editor_find_digest); wb.editor_find_query=g_strdup(query); wb.editor_find_digest=g_strdup(digest); wb.editor_find_begin=begin;wb.editor_find_match_end=end; wb.editor_find_end=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_select)) ? end : begin; wb.editor_find_id=doc->id; wb.editor_find_flags=flags;
  ui_set_statusbar(TRUE,wrapped ? "Find: wrapped" : "Find: match");editor_history_record(query); insight_workbench_refresh();
 } else ui_set_statusbar(TRUE,"Find: %s",error ? error->message : "no match");
 g_clear_error(&error); g_free(text); g_free(digest);
}
static void search_toolbar_focus(GtkMenuItem *item,gpointer data) {
 (void)item; GtkWidget *entry=GPOINTER_TO_INT(data) ? wb.editor_query : wb.project_query;
 gtk_widget_show(GPOINTER_TO_INT(data) ? wb.editor_search_row : wb.project_search_row);if(!GPOINTER_TO_INT(data)) focus_search_results(NULL,NULL);gtk_widget_grab_focus(entry);
}
static void search_folder_files(const char *root,guint depth,guint *visited) {
 if(depth>=64 || wb.search_files->len>=10000 || *visited>=100000) return;
 GDir *dir=g_dir_open(root,0,NULL); if(!dir) return; const char *name;
 while((name=g_dir_read_name(dir)) && wb.search_files->len<10000 && *visited<100000) {
  (*visited)++; if(name[0]=='.' || !strcmp(name,"node_modules") || !strcmp(name,"__pycache__")) continue;
  gchar *path=g_build_filename(root,name,NULL); GStatBuf st;
  if(!g_lstat(path,&st)) { if(S_ISDIR(st.st_mode)) search_folder_files(path,depth+1,visited); else if(S_ISREG(st.st_mode) && st.st_size<=2*1024*1024) g_ptr_array_add(wb.search_files,utils_get_utf8_from_locale(path)); }
  g_free(path);
 }
 g_dir_close(dir);
}
static void save_search_settings(void) {
 InsightSearchState *state=insight_search_state_new();
 g_free(state->query); state->query=g_strdup(gtk_entry_get_text(GTK_ENTRY(wb.project_query)));
 g_free(state->mask); state->mask=g_strdup(gtk_entry_get_text(GTK_ENTRY(wb.search_mask)));
 state->flags=text_search_flags(wb.project_case,wb.project_word,wb.project_regex); state->scope=(guint)gtk_combo_box_get_active(GTK_COMBO_BOX(wb.search_scope));
 state->variations=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.search_variations)); state->keywords=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.search_keywords)); state->context=(guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(wb.search_context));
 for(guint i=0;i<wb.search_files->len;i++) g_ptr_array_add(state->files,g_strdup(g_ptr_array_index(wb.search_files,i)));
 for(guint i=0;i<wb.search_history->len;i++) g_ptr_array_add(state->history,g_strdup(g_ptr_array_index(wb.search_history,i)));
 gchar *parent=g_path_get_dirname(wb.search_state_path); GError *error=NULL;
 if(g_mkdir_with_parents(parent,0700)<0 || !insight_search_state_save(state,wb.search_state_path,&error)) ui_set_statusbar(TRUE,"Cannot save search settings: %s",error ? error->message : "cannot create settings directory");
 g_clear_error(&error); g_free(parent); insight_search_state_free(state);
}
static void keyword_mode_changed(GtkToggleButton *button,gpointer data) {
 (void)data; gboolean keywords=gtk_toggle_button_get_active(button);
 gtk_widget_set_sensitive(wb.project_regex,!keywords); gtk_widget_set_sensitive(wb.search_context,keywords); gtk_widget_set_sensitive(wb.search_variations,keywords);
 gtk_widget_set_tooltip_text(wb.project_regex,keywords ? "In Keywords mode use ?\"pattern\" for regular expressions" : "Search using a regular expression");
}
static void restore_search_settings(void) {
 if(!g_file_test(wb.search_state_path,G_FILE_TEST_EXISTS)) { keyword_mode_changed(GTK_TOGGLE_BUTTON(wb.search_keywords),NULL); return; }
 GError *error=NULL; InsightSearchState *state=insight_search_state_load(wb.search_state_path,&error);
 if(!state) { ui_set_statusbar(TRUE,"Cannot restore search settings: %s",error ? error->message : "invalid settings"); g_clear_error(&error); keyword_mode_changed(GTK_TOGGLE_BUTTON(wb.search_keywords),NULL); return; }
 gtk_entry_set_text(GTK_ENTRY(wb.project_query),state->query); gtk_entry_set_text(GTK_ENTRY(wb.search_mask),state->mask);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_case),state->flags&INSIGHT_SEARCH_CASE); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_word),state->flags&INSIGHT_SEARCH_WORD); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_regex),state->flags&INSIGHT_SEARCH_REGEX);
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.search_scope),(gint)state->scope); gtk_spin_button_set_value(GTK_SPIN_BUTTON(wb.search_context),state->context); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.search_keywords),state->keywords); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.search_variations),state->variations);
 g_ptr_array_set_size(wb.search_files,0); for(guint i=0;i<state->files->len;i++) g_ptr_array_add(wb.search_files,g_strdup(g_ptr_array_index(state->files,i)));
 g_ptr_array_set_size(wb.search_history,0); for(guint i=0;i<state->history->len;i++) g_ptr_array_add(wb.search_history,g_strdup(g_ptr_array_index(state->history,i)));
 keyword_mode_changed(GTK_TOGGLE_BUTTON(wb.search_keywords),NULL); insight_search_state_free(state);
}
static void focus_search_results(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; gtk_widget_show(wb.search_page);msgwin_show_hide(TRUE);
 gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),gtk_notebook_page_num(GTK_NOTEBOOK(main_widgets.message_window_notebook),wb.search_page)); gtk_widget_grab_focus(wb.search_view);
}
void insight_workbench_source_link_notify(GeanyEditor *editor,SCNotification *nt) {
 if(wb.alive) {
  if(nt->nmhdr.code==SCN_MODIFIED && (nt->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT)) &&
     insight_slang_source(editor->document->file_name)) hdl_invalidate_colors();
  if(nt->nmhdr.code==SCN_UPDATEUI && (nt->updated&SC_UPDATE_V_SCROLL)) hdl_queue_paint(editor->sci);
  if(nt->nmhdr.code==SCN_PAINTED && g_object_get_data(G_OBJECT(editor->sci),"insight-hdl-spans")) {
   GObject *view=G_OBJECT(editor->sci);
   gpointer styled=GINT_TO_POINTER((gint)SSM(editor->sci,SCI_GETENDSTYLED,0,0));
   gpointer height=GINT_TO_POINTER((gint)SSM(editor->sci,SCI_LINESONSCREEN,0,0));
   if(styled!=g_object_get_data(view,"insight-hdl-styled") || height!=g_object_get_data(view,"insight-hdl-height")) {
    g_object_set_data(view,"insight-hdl-styled",styled);g_object_set_data(view,"insight-hdl-height",height);
    hdl_queue_paint(editor->sci);
   }
  }
  insight_search_links_notify(wb.search_links,editor,nt);
 }
}
gboolean insight_workbench_source_link_key(GdkEventKey *event) {
 if(!wb.alive || !wb.search_links || (event->state & gtk_accelerator_get_default_mod_mask())!=GEANY_PRIMARY_MOD_MASK || (event->keyval!=GDK_KEY_l && event->keyval!=GDK_KEY_L)) return FALSE;
 GeanyDocument *doc=document_get_current();if(!doc || !gtk_widget_has_focus(GTK_WIDGET(doc->editor->sci))) return FALSE;
 if(!insight_search_links_return(wb.search_links,doc)) return FALSE;
 focus_search_results(NULL,NULL);return TRUE;
}
static void search_jump_to_link(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;GeanyDocument *doc=document_get_current();
 if(doc && gtk_widget_has_focus(GTK_WIDGET(doc->editor->sci))) { if(insight_search_links_return(wb.search_links,doc)) focus_search_results(NULL,NULL);else ui_set_statusbar(TRUE,"No source link on this editor line");return; }
 GtkTreeModel *model;GtkTreeIter iter;if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.search_view)),&model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter);insight_search_links_open(wb.search_links,path);gtk_tree_path_free(path); }else ui_set_statusbar(TRUE,"Select a search result with a source link");
}
static gboolean search_result_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)data;
 if((event->state & gtk_accelerator_get_default_mod_mask())==GDK_MOD1_MASK && (event->keyval==GDK_KEY_Up || event->keyval==GDK_KEY_Down)) { search_result_move(widget,GINT_TO_POINTER(event->keyval==GDK_KEY_Up ? -2 : 2)); return TRUE; }
 if((event->state & gtk_accelerator_get_default_mod_mask())!=GEANY_PRIMARY_MOD_MASK || (event->keyval!=GDK_KEY_l && event->keyval!=GDK_KEY_L)) return FALSE;
 GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)); GtkTreeModel *model; GtkTreeIter iter;
 if(gtk_tree_selection_get_selected(selection,&model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter); usage_activated(GTK_TREE_VIEW(widget),path,NULL,NULL); gtk_tree_path_free(path); }
 return TRUE;
}
static void search_choose_files(GtkWidget *widget,gpointer data) {
 (void)widget; gboolean folder=GPOINTER_TO_INT(data)!=0;
 GtkWidget *dialog=gtk_file_chooser_dialog_new(folder ? "Search a folder" : "Search selected files",GTK_WINDOW(main_widgets.window),folder ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,"Choose",GTK_RESPONSE_ACCEPT,NULL);
 if(!folder) gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog),TRUE);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
  GSList *paths=gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog)); g_ptr_array_set_size(wb.search_files,0);
  for(GSList *p=paths;p && wb.search_files->len<10000;p=p->next) { if(folder) { guint visited=0; search_folder_files(p->data,0,&visited); } else g_ptr_array_add(wb.search_files,utils_get_utf8_from_locale(p->data)); }
  g_slist_free_full(paths,g_free); gtk_combo_box_set_active(GTK_COMBO_BOX(wb.search_scope),3);
  gchar *status=g_strdup_printf("%u files chosen (up to 10000; hidden folders and files over 2 MiB skipped)",wb.search_files->len); gtk_label_set_text(GTK_LABEL(wb.search_status),status); g_free(status); save_search_settings();
 }
 gtk_widget_destroy(dialog);
}
static void search_result_move(GtkWidget *widget,gpointer data) {
 (void)widget; gint direction=GPOINTER_TO_INT(data),step=direction<0 ? -1 : 1,count=gtk_tree_model_iter_n_children(GTK_TREE_MODEL(wb.search_matches),NULL); gboolean open=direction==-1 || direction==1 || direction==-3 || direction==3; if(!count) return;
 GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.search_view)); GtkTreeModel *model; GtkTreeIter iter; gint index=step<0 ? count : -1;
 if(gtk_tree_selection_get_selected(selection,&model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter); index=gtk_tree_path_get_indices(path)[0]; gtk_tree_path_free(path); }
 index=direction==-3 ? 0 : direction==3 ? count-1 : (index+step+count)%count; GtkTreePath *path=gtk_tree_path_new_from_indices(index,-1);
 gtk_tree_view_set_cursor(GTK_TREE_VIEW(wb.search_view),path,NULL,FALSE); gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(wb.search_view),path,NULL,TRUE,0.5f,0); if(open) usage_activated(GTK_TREE_VIEW(wb.search_view),path,NULL,NULL); else usage_selected(selection,NULL); gtk_tree_path_free(path);
}
static void search_copy_results(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; GString *text=g_string_new(NULL); GtkTreeIter iter; GtkTreeModel *model=GTK_TREE_MODEL(wb.search_matches);
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gchar *path,*snippet; gulong line,column; gtk_tree_model_get(model,&iter,USAGE_DISPLAY,&path,USAGE_LINE,&line,USAGE_COLUMN,&column,USAGE_SNIPPET,&snippet,-1); g_string_append_printf(text,"%s:%lu:%lu: %s\n",path,line,column,snippet); g_free(path); g_free(snippet); }
 gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text->str,(gint)text->len); g_string_free(text,TRUE);
}
static void create_search_results(void) {
 wb.search_state_path=g_build_filename(app->configdir,"insight","search-state.conf",NULL);
 wb.search_files=g_ptr_array_new_with_free_func(g_free); wb.search_page=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); wb.search_scope=gtk_combo_box_text_new(); gtk_widget_set_name(wb.search_scope,"insight-search-scope");
 const char *scopes[]={"Project files","Open documents","Current document","Chosen files / folder"}; for(guint i=0;i<G_N_ELEMENTS(scopes);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.search_scope),scopes[i]); gtk_combo_box_set_active(GTK_COMBO_BOX(wb.search_scope),0);
 gtk_box_pack_start(GTK_BOX(row),wb.search_scope,FALSE,FALSE,0); wb.search_mask=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.search_mask),"File masks: *.c;*.h;*.py"); gtk_box_pack_start(GTK_BOX(row),wb.search_mask,TRUE,TRUE,0);
 wb.search_keywords=gtk_check_button_new_with_label("Keywords / Boolean"); gtk_widget_set_name(wb.search_keywords,"insight-search-keywords"); gtk_widget_set_tooltip_text(wb.search_keywords,"Implicit AND, OR, NOT/!, parentheses, ~fuzzy, =case, ?\"regex\"; word fragments unless Word is enabled"); gtk_box_pack_start(GTK_BOX(row),wb.search_keywords,FALSE,FALSE,0);
 wb.search_variations=gtk_check_button_new_with_label("Word variations"); gtk_widget_set_name(wb.search_variations,"insight-search-variations"); gtk_widget_set_tooltip_text(wb.search_variations,"Regular English endings: open/opens/opened/opening, save/saving; base keywords only, not a dictionary stemmer"); gtk_box_pack_start(GTK_BOX(row),wb.search_variations,FALSE,FALSE,0);
 wb.search_context=gtk_spin_button_new_with_range(0,50,1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(wb.search_context),3); gtk_widget_set_tooltip_text(wb.search_context,"Maximum keyword distance in lines (0 = same line)"); gtk_box_pack_start(GTK_BOX(row),wb.search_context,FALSE,FALSE,0);
 wb.search_region=gtk_combo_box_text_new();gtk_widget_set_name(wb.search_region,"insight-search-result-region");const gchar *regions[]={"All text","Skip Comments","Comments only","Code only"};for(guint i=0;i<G_N_ELEMENTS(regions);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.search_region),regions[i]);gtk_combo_box_set_active(GTK_COMBO_BOX(wb.search_region),0);gtk_box_pack_start(GTK_BOX(row),wb.search_region,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(wb.search_page),row,FALSE,FALSE,0); GtkWidget *controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);
 const char *buttons[]={"Files…","Folder…","Search","Stop","Previous","Next","Copy results"};
 for(guint i=0;i<G_N_ELEMENTS(buttons);i++) { GtkWidget *button=gtk_button_new_with_label(buttons[i]); gtk_box_pack_start(GTK_BOX(controls),button,FALSE,FALSE,0); GCallback cb=i<2 ? G_CALLBACK(search_choose_files) : (i==2 ? G_CALLBACK(project_text_search) : (i==3 ? G_CALLBACK(usage_cancel) : (i<6 ? G_CALLBACK(search_result_move) : G_CALLBACK(search_copy_results)))); g_signal_connect(button,"clicked",cb,GINT_TO_POINTER(i==1 ? 1 : (i==4 ? -1 : 1))); }
 GtkWidget *jump=gtk_button_new_with_label("Jump To Link");gtk_widget_set_name(jump,"insight-jump-source-link");gtk_box_pack_start(GTK_BOX(controls),jump,FALSE,FALSE,0);g_signal_connect(jump,"clicked",G_CALLBACK(search_jump_to_link),NULL);gtk_box_pack_start(GTK_BOX(wb.search_page),controls,FALSE,FALSE,0); wb.search_status=gtk_label_new("Search results — Alt+Up/Down previews; Enter/double click opens the match"); gtk_widget_set_name(wb.search_status,"insight-search-status"); gtk_label_set_xalign(GTK_LABEL(wb.search_status),0); gtk_box_pack_start(GTK_BOX(wb.search_page),wb.search_status,FALSE,FALSE,0);
 wb.search_matches=gtk_list_store_new(USAGE_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG); wb.search_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.search_matches)); gtk_widget_set_name(wb.search_view,"insight-search-results"); gtk_widget_set_tooltip_text(wb.search_view,"Alt+Up/Down: browse matches in Context without moving the editor; Enter/double click or Ctrl+L: open the selected match; Shift+F8/F9: previous/next match");
 const char *labels[]={"File","Line","Column","Source"}; const gint columns[]={USAGE_DISPLAY,USAGE_LINE,USAGE_COLUMN,USAGE_SNIPPET};
 for(guint i=0;i<4;i++) { GtkCellRenderer *cell=gtk_cell_renderer_text_new(); g_object_set(cell,"ellipsize",PANGO_ELLIPSIZE_END,NULL); GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(labels[i],cell,"text",columns[i],NULL); gtk_tree_view_column_set_sort_column_id(column,columns[i]); gtk_tree_view_column_set_resizable(column,TRUE); gtk_tree_view_column_set_sizing(column,GTK_TREE_VIEW_COLUMN_FIXED); gtk_tree_view_column_set_fixed_width(column,i==0 ? 260 : (i<3 ? 70 : 450)); gtk_tree_view_column_set_expand(column,i==3); gtk_tree_view_append_column(GTK_TREE_VIEW(wb.search_view),column); }
 g_object_set_data(G_OBJECT(wb.search_view),"insight-list-title","Search Results");insight_list_tools_popup_attach(GTK_TREE_VIEW(wb.search_view));
 wb.search_links=insight_search_links_new(GTK_TREE_VIEW(wb.search_view),(InsightSearchLinkColumns){USAGE_DISPLAY,USAGE_PATH,USAGE_LINE,USAGE_START,USAGE_END,USAGE_ID,USAGE_DIGEST,USAGE_COLUMN});
 insight_search_links_set_highlight(wb.search_links,decorate_match);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.search_view); gtk_box_pack_start(GTK_BOX(wb.search_page),scroll,TRUE,TRUE,0);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.search_view)),"changed",G_CALLBACK(usage_selected),NULL); g_signal_connect(wb.search_view,"row-activated",G_CALLBACK(usage_activated),NULL);
 g_signal_connect(wb.search_view,"key-press-event",G_CALLBACK(search_result_key),NULL);
 g_signal_connect(wb.search_keywords,"toggled",G_CALLBACK(keyword_mode_changed),NULL);
 gtk_notebook_append_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),wb.search_page,gtk_label_new("Search Results")); gtk_widget_show_all(wb.search_page);gtk_widget_set_no_show_all(wb.search_page,TRUE);gtk_widget_hide(wb.search_page);
}
static void search_history_pick(GtkMenuItem *item,gpointer data) {
 (void)data; const char *query=g_object_get_data(G_OBJECT(item),"query"); gtk_entry_set_text(GTK_ENTRY(wb.project_query),query); gtk_widget_grab_focus(wb.project_query);
}
static void search_history_clear(GtkMenuItem *item,gpointer data) {
 (void)item; (void)data; g_ptr_array_set_size(wb.search_history,0); save_search_settings();
}
static void search_history_menu(GtkButton *button,gpointer data) {
 (void)data; GtkWidget *menu=gtk_menu_new(); gtk_menu_attach_to_widget(GTK_MENU(menu),GTK_WIDGET(button),NULL);
 for(guint i=0;i<wb.search_history->len;i++) { const char *query=g_ptr_array_index(wb.search_history,i); gchar *label=g_utf8_substring(query,0,MIN((glong)96,g_utf8_strlen(query,-1))); GtkWidget *item=gtk_menu_item_new_with_label(label); gchar *name=g_strdup_printf("insight-search-history-%u",i); gtk_widget_set_name(item,name); g_free(name); g_free(label); gtk_widget_set_tooltip_text(item,query); g_object_set_data_full(G_OBJECT(item),"query",g_strdup(query),g_free); g_signal_connect(item,"activate",G_CALLBACK(search_history_pick),NULL); gtk_menu_shell_append(GTK_MENU_SHELL(menu),item); }
 GtkWidget *clear=gtk_menu_item_new_with_label(wb.search_history->len ? "Clear history" : "History is empty"); gtk_widget_set_sensitive(clear,wb.search_history->len>0); g_signal_connect(clear,"activate",G_CALLBACK(search_history_clear),NULL); if(wb.search_history->len) gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new()); gtk_menu_shell_append(GTK_MENU_SHELL(menu),clear);
 g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu); gtk_widget_show_all(menu); gtk_menu_popup_at_widget(GTK_MENU(menu),GTK_WIDGET(button),GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,NULL);
}
static void search_panel_close(GtkWidget *widget,gpointer data) {
 (void)widget;if(data==wb.search_page) insight_search_links_clear(wb.search_links);gtk_widget_hide(GTK_WIDGET(data));editor_focus();
}
static gboolean search_panel_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;if(event->keyval!=GDK_KEY_Escape) return FALSE;search_panel_close(NULL,data);return TRUE;
}
static void search_native_command(GtkWidget *widget,gpointer data) {
 (void)widget;GtkWidget *item=ui_lookup_widget(main_widgets.window,data);if(GTK_IS_MENU_ITEM(item)) gtk_menu_item_activate(GTK_MENU_ITEM(item));
}
/* Both commands share widgets and execution, but never their persisted state. */
typedef struct {
 GtkWidget *dialog,*query,*scope,*method,*sensitive,*word,*variations,*context,*comments,*mask;
 gboolean references;
} SearchCommandDialog;
static void search_command_method(GtkComboBox *combo,gpointer data) {
 SearchCommandDialog *d=data;gint method=gtk_combo_box_get_active(combo);
 gboolean keywords=method==2,reference=method==3;
 gtk_widget_set_sensitive(d->variations,keywords);gtk_widget_set_sensitive(d->context,keywords);
 gtk_widget_set_sensitive(d->word,!reference);if(reference) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->word),TRUE);
}
static GtkWidget *search_command_check(GtkWidget *box,const gchar *label,const gchar *name) {
 GtkWidget *check=gtk_check_button_new_with_label(label);gtk_widget_set_name(check,name);gtk_box_pack_start(GTK_BOX(box),check,FALSE,FALSE,0);return check;
}
static void search_command_unavailable(GtkWidget *box,const gchar *label,const gchar *reason) {
 GtkWidget *check=search_command_check(box,label,"insight-search-unavailable");gtk_widget_set_sensitive(check,FALSE);gtk_widget_set_tooltip_text(check,reason);
}
static void search_command_response(GtkButton *button,gpointer data) { gtk_dialog_response(GTK_DIALOG(data),GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),"response"))); }
static void search_command_dialog(GtkWidget *widget,gpointer data) {
 (void)widget;SearchCommandDialog d={0};d.references=GPOINTER_TO_INT(data)!=0;
 gchar *path=g_build_filename(app->configdir,"insight",d.references ? "lookup-references.conf" : "search-project-dialog.conf",NULL);
 GKeyFile *state=g_key_file_new();g_key_file_load_from_file(state,path,G_KEY_FILE_NONE,NULL);
 d.dialog=gtk_dialog_new_with_buttons(d.references ? "Lookup References" : "Search Project",GTK_WINDOW(main_widgets.window),GTK_DIALOG_DESTROY_WITH_PARENT,NULL);
 gtk_widget_set_name(d.dialog,d.references ? "insight-lookup-references-dialog" : "insight-search-project-dialog");
 gtk_window_set_default_size(GTK_WINDOW(d.dialog),740,420);gtk_window_set_resizable(GTK_WINDOW(d.dialog),TRUE);
 GtkWidget *outer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12),*left=gtk_box_new(GTK_ORIENTATION_VERTICAL,8),*buttons=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);
 gtk_container_set_border_width(GTK_CONTAINER(outer),12);gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d.dialog))),outer,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(outer),left,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(outer),buttons,FALSE,FALSE,0);
 GtkWidget *label=gtk_label_new(d.references ? "Find References To:" : "Find Keywords:");gtk_label_set_xalign(GTK_LABEL(label),0);gtk_box_pack_start(GTK_BOX(left),label,FALSE,FALSE,0);
 GtkWidget *history=gtk_combo_box_text_new_with_entry();d.query=gtk_bin_get_child(GTK_BIN(history));gtk_widget_set_name(d.query,"insight-search-command-query");gtk_entry_set_max_length(GTK_ENTRY(d.query),1024);gtk_box_pack_start(GTK_BOX(left),history,FALSE,FALSE,0);
 gsize count=0;gchar **queries=g_key_file_get_string_list(state,"Search","History",&count,NULL);for(gsize i=0;queries && i<MIN(count,32);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(history),queries[i]);
 gchar *query=g_key_file_get_string(state,"Search","Query",NULL);gtk_entry_set_text(GTK_ENTRY(d.query),query ? query : "");g_free(query);
 if(d.references) { GeanyDocument *doc=document_get_current();gchar *word=doc ? editor_get_default_selection(doc->editor,TRUE,NULL) : NULL;if(word && !strchr(word,'\n') && !strchr(word,'\r')) gtk_entry_set_text(GTK_ENTRY(d.query),word);g_free(word); }
 label=gtk_label_new("Search In:");gtk_label_set_xalign(GTK_LABEL(label),0);gtk_box_pack_start(GTK_BOX(left),label,FALSE,FALSE,0);
 d.scope=gtk_combo_box_text_new();gtk_widget_set_name(d.scope,"insight-search-command-scope");const gchar *scopes[]={"All Project Files","Open Documents","Current File","Chosen Files / Folder"};for(guint i=0;i<G_N_ELEMENTS(scopes);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d.scope),scopes[i]);gtk_combo_box_set_active(GTK_COMBO_BOX(d.scope),CLAMP(g_key_file_get_integer(state,"Search","Scope",NULL),0,3));gtk_box_pack_start(GTK_BOX(left),d.scope,FALSE,FALSE,0);
 d.mask=gtk_entry_new();gtk_widget_set_name(d.mask,"insight-search-command-mask");gtk_entry_set_placeholder_text(GTK_ENTRY(d.mask),"File masks: *.c;*.h (empty = all)");gchar *mask=g_key_file_get_string(state,"Search","Mask",NULL);gtk_entry_set_text(GTK_ENTRY(d.mask),mask ? mask : "");g_free(mask);gtk_box_pack_start(GTK_BOX(left),d.mask,FALSE,FALSE,0);
 GtkWidget *frame=gtk_frame_new("Options"),*options=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,18),*checks=gtk_box_new(GTK_ORIENTATION_VERTICAL,6),*methods=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_container_set_border_width(GTK_CONTAINER(options),10);gtk_container_add(GTK_CONTAINER(frame),options);gtk_box_pack_start(GTK_BOX(options),checks,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(options),methods,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(left),frame,TRUE,TRUE,0);
 d.sensitive=search_command_check(checks,"Case Sensitive","insight-search-command-case");d.word=search_command_check(checks,"Whole Words Only","insight-search-command-word");
 search_command_unavailable(checks,"Skip Inactive Code","Preprocessor condition evaluation is not implemented.");
 label=gtk_label_new("Comment regions:");gtk_label_set_xalign(GTK_LABEL(label),0);gtk_box_pack_start(GTK_BOX(checks),label,FALSE,FALSE,0);d.comments=gtk_combo_box_text_new();gtk_widget_set_name(d.comments,"insight-search-command-comments");const gchar *regions[]={"All text","Skip Comments","Search Only Comments","Code only (skip strings too)"};for(guint i=0;i<G_N_ELEMENTS(regions);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d.comments),regions[i]);gtk_combo_box_set_active(GTK_COMBO_BOX(d.comments),CLAMP(g_key_file_get_integer(state,"Search","Comments",NULL),0,3));gtk_box_pack_start(GTK_BOX(checks),d.comments,FALSE,FALSE,0);
 search_command_unavailable(checks,"Smart Reference Matching","References are lexical whole identifiers; overload and type resolution is not implemented.");search_command_unavailable(checks,"Touch files and cause recompile","Search is read-only; touching project files is not implemented.");
 label=gtk_label_new("Search Method:");gtk_label_set_xalign(GTK_LABEL(label),0);gtk_box_pack_start(GTK_BOX(methods),label,FALSE,FALSE,0);d.method=gtk_combo_box_text_new();gtk_widget_set_name(d.method,"insight-search-command-method");const gchar *names[]={"Simple String","Regular Expression","Keyword Expression","Lookup Reference (lexical)"};for(guint i=0;i<G_N_ELEMENTS(names);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d.method),names[i]);gtk_box_pack_start(GTK_BOX(methods),d.method,FALSE,FALSE,0);
 d.variations=search_command_check(methods,"Find word variations","insight-search-command-variations");label=gtk_label_new("Lines of Context:");gtk_label_set_xalign(GTK_LABEL(label),0);gtk_box_pack_start(GTK_BOX(methods),label,FALSE,FALSE,0);d.context=gtk_spin_button_new_with_range(0,50,1);gtk_widget_set_name(d.context,"insight-search-command-context");gtk_box_pack_start(GTK_BOX(methods),d.context,FALSE,FALSE,0);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d.sensitive),g_key_file_has_key(state,"Search","Case",NULL) ? g_key_file_get_boolean(state,"Search","Case",NULL) : d.references);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d.word),g_key_file_get_boolean(state,"Search","Word",NULL));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d.variations),g_key_file_get_boolean(state,"Search","Variations",NULL));gtk_spin_button_set_value(GTK_SPIN_BUTTON(d.context),g_key_file_has_key(state,"Search","Context",NULL) ? CLAMP(g_key_file_get_integer(state,"Search","Context",NULL),0,50) : 5);
 gtk_combo_box_set_active(GTK_COMBO_BOX(d.method),g_key_file_has_key(state,"Search","Method",NULL) ? CLAMP(g_key_file_get_integer(state,"Search","Method",NULL),0,3) : d.references ? 3 : 2);g_signal_connect(d.method,"changed",G_CALLBACK(search_command_method),&d);search_command_method(GTK_COMBO_BOX(d.method),&d);
 const gchar *actions[]={"Search","Close","Help"};const gint responses[]={GTK_RESPONSE_ACCEPT,GTK_RESPONSE_CLOSE,GTK_RESPONSE_HELP};for(guint i=0;i<3;i++) { GtkWidget *button=gtk_button_new_with_label(actions[i]);gtk_box_pack_start(GTK_BOX(buttons),button,FALSE,FALSE,0);g_object_set_data(G_OBJECT(button),"response",GINT_TO_POINTER(responses[i]));g_signal_connect(button,"clicked",G_CALLBACK(search_command_response),d.dialog);if(!i) { gtk_widget_set_can_default(button,TRUE);gtk_widget_grab_default(button); } }
 gtk_entry_set_activates_default(GTK_ENTRY(d.query),TRUE);label=gtk_label_new("Lexical references; comment filters skip unsupported languages.\nKeyword fragments: turn Whole Words Only off.");gtk_label_set_line_wrap(GTK_LABEL(label),TRUE);gtk_label_set_xalign(GTK_LABEL(label),0);gtk_box_pack_start(GTK_BOX(left),label,FALSE,FALSE,0);
 gtk_widget_show_all(d.dialog);gtk_widget_grab_focus(d.query);gtk_editable_select_region(GTK_EDITABLE(d.query),0,-1);gint response;
 for(;;) { response=gtk_dialog_run(GTK_DIALOG(d.dialog));if(response==GTK_RESPONSE_HELP) { insight_help_show(NULL,"search-command-dialogs.md");continue; }if(response!=GTK_RESPONSE_ACCEPT) break;
  const gchar *q=gtk_entry_get_text(GTK_ENTRY(d.query));gint m=gtk_combo_box_get_active(GTK_COMBO_BOX(d.method));guint flags=(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.sensitive)) ? INSIGHT_SEARCH_CASE : 0)|(m==3 || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.word)) ? INSIGHT_SEARCH_WORD : 0)|(m==1 ? INSIGHT_SEARCH_REGEX : 0)|(m==2 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.variations)) ? INSIGHT_KEYWORD_VARIATIONS : 0);size_t total=0;GError *validation=NULL;
  gboolean valid=*q && (m==3 ? insight_occurrences_find("",0,q,strlen(q),0,NULL,0,&total) : m==2 ? insight_keyword_collect("",0,q,flags,gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d.context)),NULL,0,&total,&validation) : insight_context_collect("",0,q,flags,NULL,0,&total,&validation));
  if(valid) { g_clear_error(&validation);break; }gtk_label_set_text(GTK_LABEL(label),validation ? validation->message : "Enter a query; Lookup Reference requires one UTF-8 identifier.");g_clear_error(&validation);gtk_widget_grab_focus(d.query);
 }
 const gchar *text=gtk_entry_get_text(GTK_ENTRY(d.query));gint method=gtk_combo_box_get_active(GTK_COMBO_BOX(d.method)),scope=gtk_combo_box_get_active(GTK_COMBO_BOX(d.scope)),comments=gtk_combo_box_get_active(GTK_COMBO_BOX(d.comments));
 g_key_file_set_string(state,"Search","Mask",gtk_entry_get_text(GTK_ENTRY(d.mask)));g_key_file_set_string(state,"Search","Query",text);g_key_file_set_integer(state,"Search","Scope",scope);g_key_file_set_integer(state,"Search","Method",method);g_key_file_set_integer(state,"Search","Comments",comments);g_key_file_set_integer(state,"Search","Context",gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d.context)));
 g_key_file_set_boolean(state,"Search","Case",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.sensitive)));g_key_file_set_boolean(state,"Search","Word",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.word)));g_key_file_set_boolean(state,"Search","Variations",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.variations)));
 GPtrArray *saved=g_ptr_array_new_with_free_func(g_free);if(response==GTK_RESPONSE_ACCEPT && *text) g_ptr_array_add(saved,g_strdup(text));for(gsize i=0;queries && i<count && saved->len<32;i++) if(g_strcmp0(text,queries[i]) || response!=GTK_RESPONSE_ACCEPT) g_ptr_array_add(saved,g_strdup(queries[i]));g_key_file_set_string_list(state,"Search","History",(const gchar *const *)saved->pdata,saved->len);
 gchar *parent=g_path_get_dirname(path),*contents=g_key_file_to_data(state,NULL,NULL);GError *error=NULL;if(g_mkdir_with_parents(parent,0700)<0 || !g_file_set_contents(path,contents,-1,&error)) ui_set_statusbar(TRUE,"Cannot save search dialog settings: %s",error ? error->message : "cannot create directory");g_clear_error(&error);g_free(parent);g_free(contents);
 if(response==GTK_RESPONSE_ACCEPT && *text) {
  wb.relation_setting=TRUE;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE);gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),3);gtk_entry_set_text(GTK_ENTRY(wb.usage_entry),text);gtk_entry_set_text(GTK_ENTRY(wb.search_mask),gtk_entry_get_text(GTK_ENTRY(d.mask)));gtk_combo_box_set_active(GTK_COMBO_BOX(wb.search_scope),scope);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_case),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.sensitive)));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_word),method==3 || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.word)));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_regex),method==1);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.search_keywords),method==2);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.search_variations),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.variations)));gtk_spin_button_set_value(GTK_SPIN_BUTTON(wb.search_context),gtk_spin_button_get_value(GTK_SPIN_BUTTON(d.context)));gtk_combo_box_set_active(GTK_COMBO_BOX(wb.search_region),comments);wb.relation_setting=FALSE;
  gtk_widget_hide(d.dialog);if(scope==3 && !wb.search_files->len) search_choose_files(NULL,NULL);if(scope!=3 || wb.search_files->len) { usage_start(NULL,NULL);focus_search_results(NULL,NULL); }
 }
 gtk_widget_destroy(d.dialog);g_ptr_array_unref(saved);g_strfreev(queries);g_key_file_unref(state);g_free(path);
}
