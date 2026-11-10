/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Symbol details and source navigation.
 * RU: Сведения о символах и переходы к исходникам. */

static void info_open(SymbolInfo *info,gboolean jump) {
 if(!info->path || !info->digest || !info->line) return;
 gchar *path=g_strdup(info->path),*digest=g_strdup(info->digest);gulong line=info->line;GtkWidget *window=g_object_ref(info->window);gchar *text=info_source(path,NULL),*actual=text ? source_digest(text) : NULL;gboolean valid=actual && !strcmp(actual,digest);g_free(text);g_free(actual);
 if(valid) {
  GeanyDocument *old=document_get_current(),*doc=info_document(path);guint old_id=old ? old->id : 0;gboolean newly_opened=doc==NULL;
  if(!doc && !g_str_has_prefix(path,"@buffer:")) { gchar *native=utils_get_locale_from_utf8(path);doc=document_open_file(native,FALSE,NULL,NULL);g_free(native); }
  if(doc && line<=(gulong)sci_get_line_count(doc->editor->sci)) {
   gint length=sci_get_length(doc->editor->sci);text=length<=2*1024*1024 ? sci_get_contents(doc->editor->sci,length+1) : NULL;actual=text ? source_digest(text) : NULL;valid=actual && !strcmp(actual,digest);g_free(text);g_free(actual);
   if(valid && jump) { old=old_id ? document_find_by_id(old_id) : NULL;navqueue_goto_line(old,doc,line);SSM(doc->editor->sci,SCI_VERTICALCENTRECARET,0,0);document_grab_focus(doc);if(symbol_info && symbol_info->window==window) gtk_widget_destroy(window); }
   else if(!jump) {
    if(valid && newly_opened) SSM(doc->editor->sci,SCI_GOTOLINE,line-1,0);
    old=old_id ? document_find_by_id(old_id) : NULL;if(old) { gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.notebook),document_get_notebook_page(old));document_grab_focus(old); }
    if(symbol_info && symbol_info->window==window) { gtk_widget_set_sensitive(symbol_info->leave,FALSE);if(valid) gtk_label_set_text(GTK_LABEL(symbol_info->status),"File left open in an editor buffer"); }
   }
  } else valid=FALSE;
 }
 if(!valid && symbol_info && symbol_info->window==window) gtk_label_set_text(GTK_LABEL(symbol_info->status),"Source changed or unavailable; Refresh before jumping");
 g_free(path);g_free(digest);g_object_unref(window);
}
static void info_jump(GtkWidget *widget,gpointer data) { (void)widget;info_open(data,TRUE); }
static void info_leave(GtkWidget *widget,gpointer data) { (void)widget;info_open(data,FALSE); }
static void info_references(GtkWidget *widget,gpointer data) {
 (void)widget;SymbolInfo *info=data;if(!info->name) return;gchar *name=g_strdup(info->name);wb.relation_setting=TRUE;
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_buffer),FALSE);gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),0);gtk_entry_set_text(GTK_ENTRY(wb.usage_entry),name);gtk_entry_set_text(GTK_ENTRY(wb.relation_filter_entry),"");wb.relation_setting=FALSE;g_free(name);relation_controls(NULL,NULL);focus_widget_panel(wb.usage_entry);gtk_widget_hide(info->window);
}
static void info_copy(GtkWidget *widget,gpointer data) {
 (void)widget;SymbolInfo *info=data;GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(info->details));GtkTextIter first,last;gtk_text_buffer_get_bounds(buffer,&first,&last);gchar *text=gtk_text_buffer_get_text(buffer,&first,&last,FALSE);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text,-1);g_free(text);
}
static void info_close(GtkWidget *widget,gpointer data) { (void)widget;SymbolInfo *info=data;gtk_widget_destroy(info->window); }
static void info_disconnect(GtkWidget *widget,gpointer data) {
 g_signal_handlers_disconnect_by_data(widget,data);if(GTK_IS_CONTAINER(widget)) gtk_container_foreach(GTK_CONTAINER(widget),info_disconnect,data);
}
static void info_destroy(GtkWidget *widget,gpointer data) {
 SymbolInfo *info=data;info_disconnect(widget,info);if(info->center) g_source_remove(info->center);g_object_unref(info->model);g_free(info->path);g_free(info->digest);g_free(info->name);g_free(info->preferred);g_free(info->preferred_candidate);g_free(info->base_name);g_free(info->base_path);insight_index_free(info->index);g_ptr_array_unref(info->visits);g_clear_pointer(&info->sources,g_hash_table_unref);symbol_info=NULL;g_free(info);
}
static gboolean info_key(GtkWidget *widget,GdkEventKey *event,gpointer data) { (void)widget;SymbolInfo *info=data;
 if((event->state&gtk_accelerator_get_default_mod_mask())==GDK_MOD1_MASK) {
  GtkWidget *target=event->keyval==GDK_KEY_Left ? info->back:event->keyval==GDK_KEY_Right ? info->forward:event->keyval==GDK_KEY_Up ? info->previous:event->keyval==GDK_KEY_Down ? info->next:NULL;
  if(target) { if(gtk_widget_get_sensitive(target)) gtk_button_clicked(GTK_BUTTON(target));return TRUE; }
 }
 if(event->keyval==GDK_KEY_Escape) { info_close(NULL,data);return TRUE; }
 if((event->state&gtk_accelerator_get_default_mod_mask())==GEANY_PRIMARY_MOD_MASK) {
  if(event->keyval==GDK_KEY_Return || event->keyval==GDK_KEY_KP_Enter) { info_jump(NULL,data);return TRUE; }
  if(event->keyval==GDK_KEY_r || event->keyval==GDK_KEY_R) { info_search(NULL,data);return TRUE; }
 }
 return FALSE; }
static void info_show_query_kind(const gchar *query,const gchar *path,gulong line,gint category) {
 if(!symbol_info) {
  SymbolInfo *info=g_new0(SymbolInfo,1);info->visits=g_ptr_array_new_with_free_func(info_visit_free);symbol_info=info;info->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(info->window,"insight-symbol-info-window");gtk_window_set_title(GTK_WINDOW(info->window),"Symbol Info");gtk_window_set_default_size(GTK_WINDOW(info->window),850,600);gtk_window_set_transient_for(GTK_WINDOW(info->window),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(info->window),TRUE);
  GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,5),*bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*buttons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_container_add(GTK_CONTAINER(info->window),box);
  GtkWidget *navigation=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);GtkWidget **controls[]={&info->back,&info->forward,&info->previous,&info->next};
  const gchar *nav_icons[]={"go-previous-symbolic","go-next-symbolic","go-up-symbolic","go-down-symbolic"},*nav_names[]={"insight-symbol-info-back","insight-symbol-info-forward","insight-symbol-info-previous","insight-symbol-info-next"},*nav_tips[]={"Previous symbol query (Alt+Left)","Next symbol query (Alt+Right)","Previous candidate (Alt+Up)","Next candidate (Alt+Down)"};
  for(guint i=0;i<4;i++) { *controls[i]=gtk_button_new_from_icon_name(nav_icons[i],GTK_ICON_SIZE_MENU);gtk_widget_set_name(*controls[i],nav_names[i]);gtk_widget_set_tooltip_text(*controls[i],nav_tips[i]);gtk_widget_set_sensitive(*controls[i],FALSE);gtk_box_pack_start(GTK_BOX(navigation),*controls[i],FALSE,FALSE,0);g_signal_connect(*controls[i],"clicked",i<2 ? G_CALLBACK(info_history_step):G_CALLBACK(info_candidate_step),info); }
  g_object_set_data(G_OBJECT(info->forward),"insight-info-forward",GINT_TO_POINTER(1));g_object_set_data(G_OBJECT(info->previous),"insight-info-delta",GINT_TO_POINTER(-1));g_object_set_data(G_OBJECT(info->next),"insight-info-delta",GINT_TO_POINTER(1));gtk_box_pack_start(GTK_BOX(bar),navigation,FALSE,FALSE,0);
  info->entry=gtk_search_entry_new();gtk_widget_set_name(info->entry,"insight-symbol-info-query");gtk_entry_set_placeholder_text(GTK_ENTRY(info->entry),"Symbol or qualified name…");GtkWidget *search=gtk_button_new_with_label("Refresh");gtk_box_pack_start(GTK_BOX(bar),info->entry,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(bar),search,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
  info->model=gtk_list_store_new(INFO_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);info->choice=gtk_combo_box_new_with_model(GTK_TREE_MODEL(info->model));gtk_widget_set_name(info->choice,"insight-symbol-info-candidates");GtkCellRenderer *cell=gtk_cell_renderer_text_new();g_object_set(cell,"ellipsize",PANGO_ELLIPSIZE_END,NULL);gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(info->choice),cell,TRUE);gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(info->choice),cell,"text",INFO_DISPLAY,NULL);gtk_box_pack_start(GTK_BOX(box),info->choice,FALSE,FALSE,0);
  GtkWidget *kinds=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);info->kind=gtk_combo_box_text_new();gtk_widget_set_name(info->kind,"insight-symbol-info-kind");const gchar *categories[]={"All indexed symbols","Function prototypes","Function implementations","Types and namespaces","Variables and members"};for(guint i=0;i<G_N_ELEMENTS(categories);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(info->kind),categories[i]);gtk_combo_box_set_active(GTK_COMBO_BOX(info->kind),0);GList *kind_cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(info->kind));for(GList *c=kind_cells;c;c=c->next) if(GTK_IS_CELL_RENDERER_TEXT(c->data)) g_object_set(c->data,"ellipsize",PANGO_ELLIPSIZE_END,"width-chars",1,NULL);g_list_free(kind_cells);gtk_box_pack_start(GTK_BOX(kinds),info->kind,TRUE,TRUE,0);
  info->count=gtk_label_new("0");gtk_widget_set_name(info->count,"insight-symbol-info-count");gtk_widget_set_tooltip_text(info->count,"Shown / matching candidates after category and language filtering");gtk_box_pack_start(GTK_BOX(kinds),info->count,FALSE,FALSE,4);
  GtkWidget **peers[]={&info->prototype,&info->implementation,&info->base};const gchar *icons[]={"text-x-generic-symbolic","go-jump-symbolic","go-up-symbolic"},*tips[]={"Browse function prototypes","Browse function implementations","Browse underlying nominal type"},*names[]={"insight-symbol-info-prototype","insight-symbol-info-implementation","insight-symbol-info-base"};for(guint i=0;i<3;i++) { *peers[i]=gtk_button_new_from_icon_name(icons[i],GTK_ICON_SIZE_MENU);gtk_widget_set_name(*peers[i],names[i]);gtk_widget_set_tooltip_text(*peers[i],tips[i]);g_object_set_data(G_OBJECT(*peers[i]),"insight-info-companion",GUINT_TO_POINTER(i+1));g_signal_connect(*peers[i],"clicked",G_CALLBACK(info_companion),info);gtk_box_pack_start(GTK_BOX(kinds),*peers[i],FALSE,FALSE,0); }
  gtk_box_pack_start(GTK_BOX(box),kinds,FALSE,FALSE,0);g_signal_connect(info->kind,"changed",G_CALLBACK(info_kind_changed),info);
  info->options=gtk_expander_new("Symbol Info lookup options");gtk_widget_set_name(info->options,"insight-symbol-info-options");GtkWidget *options=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
  info->parse_local=gtk_check_button_new_with_label("Parse locally before lookup");gtk_widget_set_name(info->parse_local,"insight-symbol-info-parse-local");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(info->parse_local),wb.info_parse_local);gtk_widget_set_tooltip_text(info->parse_local,"Update stale current-buffer tags before lookup; reuse unchanged tags");
  info->across_languages=gtk_check_button_new_with_label("Find symbols across different language types");gtk_widget_set_name(info->across_languages,"insight-symbol-info-across-languages");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(info->across_languages),wb.info_across_languages);
  GtkWidget *scope_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*scope_label=gtk_label_new("Lookup sources");info->scope=gtk_combo_box_text_new();gtk_widget_set_name(info->scope,"insight-symbol-info-scope");const gchar *scopes[]={"Project and open files","Project files only","Current buffer only"};for(guint i=0;i<G_N_ELEMENTS(scopes);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(info->scope),scopes[i]);gtk_combo_box_set_active(GTK_COMBO_BOX(info->scope),wb.info_scope);gtk_box_pack_start(GTK_BOX(scope_row),scope_label,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(scope_row),info->scope,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(options),scope_row,FALSE,FALSE,0);
  GList *scope_cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(info->scope));for(GList *c=scope_cells;c;c=c->next) if(GTK_IS_CELL_RENDERER_TEXT(c->data)) g_object_set(c->data,"ellipsize",PANGO_ELLIPSIZE_END,"width-chars",1,NULL);g_list_free(scope_cells);g_signal_connect(info->scope,"changed",G_CALLBACK(info_scope_changed),info);
  gtk_box_pack_start(GTK_BOX(options),info->parse_local,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(options),info->across_languages,FALSE,FALSE,0);gtk_container_add(GTK_CONTAINER(info->options),options);gtk_box_pack_start(GTK_BOX(box),info->options,FALSE,FALSE,0);
  g_signal_connect(info->parse_local,"toggled",G_CALLBACK(info_options_changed),info);g_signal_connect(info->across_languages,"toggled",G_CALLBACK(info_options_changed),info);
  insight_panel_help_attach(box,"symbol-info.md");
  info->details=gtk_text_view_new();gtk_widget_set_name(info->details,"insight-symbol-info-details");gtk_text_view_set_editable(GTK_TEXT_VIEW(info->details),FALSE);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(info->details),GTK_WRAP_WORD_CHAR);gtk_container_add(GTK_CONTAINER(scroll),info->details);gtk_widget_set_size_request(scroll,-1,125);gtk_box_pack_start(GTK_BOX(box),scroll,FALSE,TRUE,0);
  info->code=scintilla_new();gtk_widget_set_name(info->code,"insight-symbol-info-code");SSM(SCINTILLA(info->code),SCI_SETCODEPAGE,SC_CP_UTF8,0);sci_set_symbol_margin(SCINTILLA(info->code),FALSE);SSM(SCINTILLA(info->code),SCI_SETMARGINWIDTHN,2,0);gtk_box_pack_start(GTK_BOX(box),info->code,TRUE,TRUE,0);
  info->jump=gtk_button_new_with_label("Jump"),info->references=gtk_button_new_with_label("References");info->leave=gtk_button_new_with_label("Leave file open");gtk_widget_set_tooltip_text(info->jump,"Jump to displayed definition (Ctrl+Enter)");GtkWidget *copy=gtk_button_new_with_label("Copy details"),*close=gtk_button_new_with_label("Close");GtkWidget *items[]={info->jump,info->references,info->leave,copy,close};for(guint i=0;i<G_N_ELEMENTS(items);i++) gtk_box_pack_start(GTK_BOX(buttons),items[i],FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),buttons,FALSE,FALSE,0);info->status=gtk_label_new("");gtk_widget_set_name(info->status,"insight-symbol-info-status");gtk_label_set_line_wrap(GTK_LABEL(info->status),TRUE);gtk_box_pack_start(GTK_BOX(box),info->status,FALSE,FALSE,0);
  g_signal_connect(info->choice,"changed",G_CALLBACK(info_choice),info);g_signal_connect(search,"clicked",G_CALLBACK(info_search),info);g_signal_connect(info->entry,"activate",G_CALLBACK(info_search),info);g_signal_connect(info->jump,"clicked",G_CALLBACK(info_jump),info);g_signal_connect(info->leave,"clicked",G_CALLBACK(info_leave),info);g_signal_connect(info->references,"clicked",G_CALLBACK(info_references),info);g_signal_connect(copy,"clicked",G_CALLBACK(info_copy),info);g_signal_connect(close,"clicked",G_CALLBACK(info_close),info);g_signal_connect(info->window,"destroy",G_CALLBACK(info_destroy),info);g_signal_connect(info->window,"key-press-event",G_CALLBACK(info_key),info);
 }
 SymbolInfo *info=symbol_info;info->filling=TRUE;gtk_combo_box_set_active(GTK_COMBO_BOX(info->kind),category);info->filling=FALSE;g_clear_pointer(&info->preferred_candidate,g_free);g_free(info->preferred);info->preferred=g_strdup(path);info->preferred_line=line;gtk_entry_set_text(GTK_ENTRY(info->entry),query);gtk_widget_show_all(info->window);info_search(NULL,info);gtk_window_present(GTK_WINDOW(info->window));gtk_widget_grab_focus(info->entry);
}
static void info_show_query(const gchar *query,const gchar *path,gulong line) { info_show_query_kind(query,path,line,0); }
static void info_kind_changed(GtkComboBox *combo,gpointer data) { (void)combo;SymbolInfo *info=data;if(!info->filling) info_search(NULL,info); }
static void info_companion(GtkWidget *widget,gpointer data) {
 SymbolInfo *info=data;guint mode=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget),"insight-info-companion"));
 if(mode==3) { if(!info->base_name || !info->base_path) return;gchar *name=g_strdup(info->base_name),*path=g_strdup(info->base_path);gulong line=info->base_line;info_show_query_kind(name,path,line,3);g_free(name);g_free(path);return; }
 GtkTreeIter iter;if(!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(info->choice),&iter)) return;gchar *name=NULL,*scope=NULL,*path=NULL;gulong line=0;
 gtk_tree_model_get(GTK_TREE_MODEL(info->model),&iter,INFO_NAME,&name,INFO_SCOPE,&scope,INFO_PATH,&path,INFO_LINE,&line,-1);
 GeanyDocument *doc=info_document(path);GeanyFiletype *ft=doc ? doc->file_type:insight_languages_detect(path);const gchar *separator=ft ? tm_parser_scope_separator_printable(ft->lang):"::";if(!separator || !*separator) separator="::";
 gchar *query=scope && *scope ? g_strconcat(scope,separator,name,NULL):g_strdup(name);info_show_query_kind(query,path,line,mode==1 ? 1:2);g_free(query);g_free(name);g_free(scope);g_free(path);
}
gboolean insight_workbench_browser_definition(gint position) {
 GeanyDocument *doc=document_get_current();if(!doc || position<0 || position>=sci_get_length(doc->editor->sci) || sci_get_length(doc->editor->sci)>2*1024*1024) return FALSE;
 ScintillaObject *sci=doc->editor->sci;if(sci_get_selection_mode(sci)!=SC_SEL_STREAM || SSM(sci,SCI_GETSELECTIONS,0,0)!=1) return FALSE;
 gint start=(gint)SSM(sci,SCI_WORDSTARTPOSITION,position,TRUE),end=(gint)SSM(sci,SCI_WORDENDPOSITION,position,TRUE);if(start>=end || position>=end) return FALSE;
 SSM(sci,SCI_COLOURISE,start,end);if(!definition_code(sci,position)) return FALSE;
 sci_set_current_position(sci,position,FALSE);if(!insight_workbench_goto_definition()) ui_set_statusbar(TRUE,"Known definition not found");return TRUE;
}
static void show_function_target(GtkWidget *widget,gpointer data) {
 (void)widget;GeanyDocument *doc=document_get_current();if(!doc) return;ScintillaObject *sci=doc->editor->sci;
 if(sci_get_length(sci)>2*1024*1024 || sci_get_selection_mode(sci)!=SC_SEL_STREAM || SSM(sci,SCI_GETSELECTIONS,0,0)!=1) { ui_set_statusbar(TRUE,"Function target lookup requires one normal selection in a buffer up to 2 MiB");return; }
 SSM(sci,SCI_COLOURISE,0,-1);gint position=sci_get_current_position(sci);gchar word[1025]={0};editor_find_current_word_sciwc(doc->editor,position,word,sizeof word);
 if(!*word || !definition_code(sci,position)) { ui_set_statusbar(TRUE,"Place the caret on a function name in code");return; }
 gint mode=GPOINTER_TO_INT(data);gchar *path=doc->file_name ? g_strdup(doc->file_name):g_strdup_printf("@buffer:%u",doc->id);info_show_query_kind(word,path,sci_get_current_line(sci)+1,mode==3 ? 0:mode);g_free(path);
 if(mode==3 && symbol_info) {
  SymbolInfo *info=symbol_info;gint candidates=gtk_tree_model_iter_n_children(GTK_TREE_MODEL(info->model),NULL);
  if(candidates==1 && info->base_name) info_companion(info->base,info);
  else gtk_label_set_text(GTK_LABEL(info->status),candidates>1 ? "Select a declaration, then browse its underlying nominal type":"No unambiguous indexed nominal type; inspect or qualify the declaration");
 }
}
static void show_symbol_info(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;GeanyDocument *doc=document_get_current();if(!doc) { info_show_query("",NULL,0);return; }gchar word[1025]={0};editor_find_current_word_sciwc(doc->editor,sci_get_current_position(doc->editor->sci),word,sizeof word);
 gchar *path=doc->file_name ? g_strdup(doc->file_name) : g_strdup_printf("@buffer:%u",doc->id);info_show_query(word,path,sci_get_current_line(doc->editor->sci)+1);g_free(path);
}
static void show_symbol_lookup_options(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(!symbol_info) show_symbol_info(NULL,NULL);
 if(symbol_info) { gtk_widget_show(symbol_info->window);gtk_window_present(GTK_WINDOW(symbol_info->window));gtk_expander_set_expanded(GTK_EXPANDER(symbol_info->options),TRUE);gtk_widget_grab_focus(symbol_info->parse_local); }
}
void insight_workbench_symbol_navigation(guint action) {
 if(action==0) show_symbol_info(NULL,NULL);
 else if(action<=3) show_function_target(NULL,GUINT_TO_POINTER(action));
}
static void structural_selection(GtkMenuItem *item,gpointer data) {
 (void)item; GeanyDocument *doc=document_get_current(); if(!doc) return;
 gint mode=GPOINTER_TO_INT(data); ScintillaObject *sci=doc->editor->sci;
 if(mode==2) { document_grab_focus(doc); keybindings_send_command(GEANY_KEY_GROUP_SELECT,GEANY_KEYS_SELECT_LINE); return; }
 if(sci_get_selection_mode(sci)!=SC_SEL_STREAM || SSM(sci,SCI_GETSELECTIONS,0,0)>1) {
  ui_set_statusbar(TRUE,"Structural selection requires a single normal selection"); return;
 }
 gint length=sci_get_length(sci);
 if(length>2*1024*1024) { ui_set_statusbar(TRUE,"Structural selection supports files up to 2 MiB"); return; }
 InsightCodeDialect dialect;
 if(!source_dialect(doc->file_type,&dialect)) { ui_set_statusbar(TRUE,"Structural selection: this language has no lexical mask yet"); return; }
 gchar *text=sci_get_contents(sci,length+1); GError *error=NULL;
 gchar *mask=insight_source_code_mask(text,(gsize)length,dialect,&error);
 if(!mask) { g_free(text);ui_set_statusbar(TRUE,"Structural selection: %s",error ? error->message : "invalid text"); g_clear_error(&error); return; }
 size_t first=0,last=0;
 int found=mode==0 ? insight_select_suite(text,mask,(size_t)length,(size_t)sci_get_selection_start(sci),(size_t)sci_get_selection_end(sci),function_style(dialect),&first,&last) :
  insight_select_match(mask,(size_t)length,(size_t)sci_get_current_position(sci),&first,&last);
 g_free(mask);g_free(text);
 if(found==1) { sci_set_selection(sci,(gint)first,(gint)last); SSM(sci,SCI_SCROLLCARET,0,0); document_grab_focus(doc); }
 else ui_set_statusbar(TRUE,found<0 ? "Structural selection could not be computed" : (mode==0 ? "No larger enclosing block" : "Place the caret on or immediately after a code bracket"));
}
static gboolean function_tag(const TMTag *tag) { return tag && tag->line && (tag->type&(tm_tag_function_t|tm_tag_method_t)); }
static void function_navigation(GtkMenuItem *item,gpointer data) {
 (void)item; GeanyDocument *doc=document_get_current(); if(!doc) return;
 if(sci_get_length(doc->editor->sci)>2*1024*1024) { ui_set_statusbar(TRUE,"Function navigation supports files up to 2 MiB"); return; }
 document_update_tags(doc);
 if(!doc->tm_file || !doc->tm_file->tags_array) { ui_set_statusbar(TRUE,"No function symbols in this document"); return; }
 GPtrArray *tags=doc->tm_file->tags_array;size_t *positions=g_new(size_t,tags->len);size_t count=0;
 gint lines=sci_get_line_count(doc->editor->sci);
 for(guint i=0;i<tags->len;i++) { TMTag *tag=g_ptr_array_index(tags,i);if(function_tag(tag) && tag->line<=(gulong)lines) positions[count++]=(size_t)sci_get_position_from_line(doc->editor->sci,(gint)tag->line-1); }
 size_t next=0;int found=insight_neighbor_position(positions,count,(size_t)sci_get_current_position(doc->editor->sci),GPOINTER_TO_INT(data),&next);g_free(positions);
 if(found==1) { navqueue_goto_line(doc,doc,sci_get_line_from_position(doc->editor->sci,(gint)next)+1); document_grab_focus(doc); }
 else ui_set_statusbar(TRUE,GPOINTER_TO_INT(data)>0 ? "No next function definition" : "No previous function definition");
}
static void select_function(GtkMenuItem *item,gpointer data) {
 (void)item;(void)data;GeanyDocument *doc=document_get_current();if(!doc) return;
 ScintillaObject *sci=doc->editor->sci;gint length=sci_get_length(sci);InsightCodeDialect dialect;
 if(length>2*1024*1024 || !source_dialect(doc->file_type,&dialect)) { ui_set_statusbar(TRUE,"Select Function supports parsed brace, Python and Lua functions in files up to 2 MiB");return; }
 if(sci_get_selection_mode(sci)!=SC_SEL_STREAM || SSM(sci,SCI_GETSELECTIONS,0,0)>1) { ui_set_statusbar(TRUE,"Select Function requires a single normal selection");return; }
 document_update_tags(doc);if(!doc->tm_file || !doc->tm_file->tags_array) { ui_set_statusbar(TRUE,"No function symbols in this document");return; }
 gchar *text=sci_get_contents(sci,length+1),*mask=insight_source_code_mask(text,(gsize)length,dialect,NULL);
 if(!mask) { g_free(text);ui_set_statusbar(TRUE,"Select Function: source could not be masked");return; }
 size_t begin=(size_t)sci_get_selection_start(sci),end=(size_t)sci_get_selection_end(sci),best=SIZE_MAX,first=0,last=0;
 gint lines=sci_get_line_count(sci);
 for(guint i=0;i<doc->tm_file->tags_array->len;i++) {
  TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,i);if(!function_tag(tag) || tag->line>(gulong)lines) continue;
  size_t start=(size_t)sci_get_position_from_line(sci,(gint)tag->line-1),finish=0,body=0;
  if(start>begin) continue;
  if(insight_function_range(text,mask,(size_t)length,start,function_style(dialect),&body,&finish)==1 && end<=finish && (begin!=end || begin<finish) && finish-start<best) { best=finish-start;first=start;last=finish; }
 }
 g_free(mask);g_free(text);
 if(best!=SIZE_MAX) { sci_set_selection(sci,(gint)first,(gint)last);SSM(sci,SCI_SCROLLCARET,0,0);document_grab_focus(doc); }
 else ui_set_statusbar(TRUE,"No complete enclosing function body");
}
/* A symbol edit uses a fresh parser snapshot and accepts only complete,
 * standalone function or nominal definitions. Never use a stale outline range. */
static gboolean symbol_text_range(GeanyDocument *doc,const gchar *name,gulong line,gint *first,gint *last) {
 ScintillaObject *sci=doc->editor->sci;gint length=sci_get_length(sci);InsightCodeDialect dialect;
 if(length>2*1024*1024 || !source_dialect(doc->file_type,&dialect) ||
    sci_get_selection_mode(sci)!=SC_SEL_STREAM || SSM(sci,SCI_GETSELECTIONS,0,0)!=1 ||
    SSM(sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) || SSM(sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0)) return FALSE;
 document_update_tags(doc);
 if(!doc->tm_file || !doc->tm_file->tags_array) return FALSE;
 gchar *text=sci_get_contents(sci,length+1);
 if(!text || strlen(text)!=(gsize)length || !g_utf8_validate(text,length,NULL)) { g_free(text);return FALSE; }
 gchar *mask=insight_source_code_mask(text,length,dialect,NULL);
 if(!mask) { g_free(text);return FALSE; }
 size_t begin=sci_get_selection_start(sci),end=sci_get_selection_end(sci),best=SIZE_MAX;
 for(guint i=0;i<doc->tm_file->tags_array->len;i++) {
  TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,i);
  if(!tag) continue;
  const gchar *nominal=tag->type&tm_tag_class_t?"class":tag->type&tm_tag_struct_t?"struct":tag->type&tm_tag_union_t?"union":tag->type&tm_tag_enum_t?"enum":tag->type&tm_tag_namespace_t?"namespace":tag->type&tm_tag_interface_t?"interface":NULL;
  if((!function_tag(tag)&&!nominal) || !tag->line || !tag->name || !*tag->name || tag->line>(gulong)sci_get_line_count(sci) ||
     (name && (line!=tag->line || strcmp(name,tag->name)))) continue;
  size_t start=sci_get_position_from_line(sci,tag->line-1),body=0,finish=0;
  if(!name && start>begin) continue;
  if((nominal ? insight_nominal_range(text,mask,length,start,tag->name,nominal,function_style(dialect),&body,&finish) : insight_function_range(text,mask,length,start,function_style(dialect),&body,&finish))!=1 || finish<=start) continue;
  if(!name && (end>finish || (begin==end && begin>=finish))) continue;
  /* Confirm the parser's name before the first body. Reject another
   * declaration/block on that line rather than duplicating neighboring code. */
  gboolean named=nominal!=NULL,unsafe=FALSE;gsize size=strlen(tag->name);
  for(size_t at=start;!nominal&&at<body;at++) {
   if(mask[at]=='(' || mask[at]=='[') { size_t a=0,b=0;if(insight_select_match(mask,length,at,&a,&b)!=1 || b>body) { unsafe=TRUE;break; }at=b-1;continue; }
   if(mask[at]==';' || mask[at]=='}' ||
      (mask[at]=='{' && !(function_style(dialect)==INSIGHT_FUNCTION_BRACES && at+1==body))) { unsafe=TRUE;break; }
   if(at+size<body && !memcmp(mask+at,tag->name,size) &&
      (at==start || !(g_ascii_isalnum(mask[at-1]) || mask[at-1]=='_' || (guchar)mask[at-1]>=128))) {
    size_t after=at+size;while(after<body && g_ascii_isspace(mask[after])) after++;
    if(after<body && mask[after]=='(') named=TRUE;
   }
  }
  if(unsafe || !named) continue;
  gint final_line=sci_get_line_from_position(sci,finish-1);
  size_t complete=final_line+1<sci_get_line_count(sci) ? (size_t)sci_get_position_from_line(sci,final_line+1) : (size_t)length;
  if(nominal&&function_style(dialect)==INSIGHT_FUNCTION_BRACES) {
   size_t at=finish;while(at<complete&&(mask[at]==' '||mask[at]=='\t')) at++;
   gboolean terminated=at<complete&&mask[at]==';';if(terminated) finish=at+1;
   if(!terminated&&strcmp(nominal,"namespace")&&(!g_strcmp0(doc->file_type->name,"C")||!g_strcmp0(doc->file_type->name,"C++"))) continue;
  }
  for(size_t at=finish;at<complete;at++) if(!g_ascii_isspace(mask[at])) { unsafe=TRUE;break; }
  if(unsafe || complete-start>1024*1024 || finish-start>=best) continue;
  best=finish-start;*first=start;*last=complete;
 }
 g_free(mask);g_free(text);return best!=SIZE_MAX;
}
static void symbol_text_edit(GeanyDocument *doc,const gchar *name,gulong line,guint command) {
 if(!doc || !doc->is_valid) return;
 ScintillaObject *sci=doc->editor->sci;
 if(command && (doc->readonly || SSM(sci,SCI_GETREADONLY,0,0))) { ui_set_statusbar(TRUE,"Symbol editing requires a writable document");return; }
 gint first=0,last=0;
 if(!symbol_text_range(doc,name,line,&first,&last)) { ui_set_statusbar(TRUE,"Symbol operation requires a complete standalone function or supported nominal definition (up to 1 MiB)");return; }
 insight_symbol_edit_range(doc->id,first,last,command);
}
void insight_workbench_symbol_edit(guint action) { symbol_text_edit(document_get_current(),NULL,0,action); }
static void symbol_text_command(GtkMenuItem *item,gpointer data) { (void)item;symbol_text_edit(document_get_current(),NULL,0,GPOINTER_TO_UINT(data)); }
static void symbol_text_from_outline(GtkTreeView *view,guint command) {
 GeanyDocument *doc=document_get_current();
 if(GTK_WIDGET(view)!=wb.local_view || !gtk_widget_get_sensitive(GTK_WIDGET(view)) || !doc || doc->id!=wb.local_doc_id) return;
 GtkTreeModel *model;GtkTreeIter iter;
 if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view),&model,&iter)) return;
 gchar *name=NULL;gulong line=0;gtk_tree_model_get(model,&iter,SYMBOL_NAME,&name,SYMBOL_LINE,&line,-1);
 if(line && name) symbol_text_edit(doc,name,line,command);
 g_free(name);
}
static void create_structural_selection_menu(void) {
 GtkWidget *edit=ui_lookup_widget(main_widgets.window,"edit1_menu");
 GtkWidget *item=gtk_menu_item_new_with_label("Structural selection"),*menu=gtk_menu_new();
 gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),menu); gtk_menu_shell_append(GTK_MENU_SHELL(edit),item);
 const char *labels[]={"Select enclosing block / expand","Select matching brackets","Select line"};
 const char *keys[]={"<Primary><Alt>b","<Primary><Alt>m","<Primary><Alt>l"};
 for(guint i=0;i<3;i++) {
  GtkWidget *entry=gtk_menu_item_new_with_label(labels[i]);if(i==0) gtk_widget_set_name(entry,"insight-select-enclosing-block"); gtk_menu_shell_append(GTK_MENU_SHELL(menu),entry);
  g_signal_connect(entry,"activate",G_CALLBACK(structural_selection),GINT_TO_POINTER((gint)i));
  guint key; GdkModifierType modifiers; gtk_accelerator_parse(keys[i],&key,&modifiers);
  gtk_widget_add_accelerator(entry,"activate",wb.navigation_accels,key,modifiers,GTK_ACCEL_VISIBLE);
 }
 GtkWidget *function=gtk_menu_item_new_with_label("Select Function");gtk_widget_set_name(function,"insight-select-function");gtk_menu_shell_append(GTK_MENU_SHELL(menu),function);
 g_signal_connect(function,"activate",G_CALLBACK(select_function),NULL);
 gtk_widget_add_accelerator(function,"activate",wb.navigation_accels,GDK_KEY_f,GEANY_PRIMARY_MOD_MASK|GDK_MOD1_MASK,GTK_ACCEL_VISIBLE);
 gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());
 const gchar *ops[]={"Copy Symbol","Cut Symbol","Duplicate Symbol","Paste Symbol…"};
 const gchar *ids[]={"insight-copy-symbol","insight-cut-symbol","insight-duplicate-symbol","insight-paste-symbol"};
 for(guint i=0;i<G_N_ELEMENTS(ops);i++) { GtkWidget *entry=gtk_menu_item_new_with_label(ops[i]);gtk_widget_set_name(entry,ids[i]);keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_COPYSYMBOL+i,entry);g_signal_connect(entry,"activate",G_CALLBACK(symbol_text_command),GUINT_TO_POINTER(i));gtk_menu_shell_append(GTK_MENU_SHELL(menu),entry); }
 gtk_widget_show_all(item);
}
