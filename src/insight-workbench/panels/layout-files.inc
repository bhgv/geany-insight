/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Layout import and export, and panel commands.
 * RU: Импорт и экспорт раскладок и команды панелей. */

static void layout_file_dialog_destroyed(GtkWidget *widget,gpointer data) { (void)widget;*(gboolean *)data=TRUE; }
static void layout_file_command(GtkMenuItem *item,gpointer data) {
 (void)item;gboolean save=GPOINTER_TO_INT(data)!=0;
 GtkWidget *dialog=gtk_file_chooser_dialog_new(save ? "Save layout to file…" : "Load layout from file…",GTK_WINDOW(main_widgets.window),save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr(save ? "Save" : "Open"),GTK_RESPONSE_ACCEPT,NULL);
 gtk_widget_set_name(dialog,"insight-layout-file-dialog");gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog),TRUE);gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog),TRUE);
 GtkFileFilter *filter=gtk_file_filter_new();gtk_file_filter_set_name(filter,insight_tr("Geany Insight layouts"));gtk_file_filter_add_pattern(filter,"*.geany-layout");gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);
 GtkFileFilter *all=gtk_file_filter_new();gtk_file_filter_set_name(all,insight_tr("All files"));gtk_file_filter_add_pattern(all,"*");gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),all);
 if(save) { gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),TRUE);gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),"workspace.geany-layout"); }
 gboolean destroyed=FALSE;g_object_ref(dialog);g_signal_connect(dialog,"destroy",G_CALLBACK(layout_file_dialog_destroyed),&destroyed);
 gint response=gtk_dialog_run(GTK_DIALOG(dialog));gchar *path=!destroyed && response==GTK_RESPONSE_ACCEPT ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)) : NULL;
 if(!destroyed) gtk_widget_destroy(dialog);
 g_object_unref(dialog);
 if(!path || !wb.alive) { g_free(path);return; }
 InsightLayout record;GError *error=NULL;gboolean ok;
 if(save) {
  ok=capture_layout(&record);if(ok) ok=insight_layout_save_file(path,&record,&error);else g_set_error_literal(&error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Layout cannot be saved: invalid panel geometry");
 } else {
  ok=insight_layout_load_file(path,&record,&error);if(ok) { apply_layout_record(&record);save_workbench_settings(); }
 }
 if(!ok) ui_set_statusbar(TRUE,"%s",insight_tr(error ? error->message : "Invalid or incomplete layout file"));
 else ui_set_statusbar(TRUE,"%s",insight_tr(save ? "Layout saved to file" : "Layout loaded from file"));
 g_clear_error(&error);g_free(path);
}
static void panel_menu_sync(GtkWidget *menu,gpointer data) {
 (void)menu; (void)data; gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.project_toggle),gtk_widget_get_visible(wb.project_notebook)); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.local_toggle),ui_prefs.sidebar_visible);
}
static void panel_original_changes(GtkMenuItem *item,gpointer data) {(void)item;(void)data;insight_revisions_changes();}
static void panel_menu(GtkWidget *view_menu) {
 GtkWidget *item=gtk_menu_item_new_with_label("Panels"),*menu=gtk_menu_new(); gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),menu); gtk_container_add(GTK_CONTAINER(view_menu),item); wb.panel_commands=menu;
 GtkWidget *changes=gtk_menu_item_new_with_label("Original Changes…");gtk_widget_set_name(changes,"insight-panel-original-changes");gtk_widget_set_tooltip_text(changes,"Changes since opening, including saved edits. Select a block; Restore Lines opens a separate confirmation. Refresh after editing.");g_signal_connect(changes,"activate",G_CALLBACK(panel_original_changes),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),changes);
 GtkWidget *activation=gtk_menu_item_new_with_label("Activate panel"),*activation_menu=gtk_menu_new();gtk_menu_item_set_submenu(GTK_MENU_ITEM(activation),activation_menu);gtk_container_add(GTK_CONTAINER(menu),activation);
 const gchar *focus[]={"Activate Project File List","Activate Project Folder Browser","Activate Project Symbol List","Activate Relation Window","Activate Bookmarks","Activate Clip Window","Activate File Types","Activate Symbol Categories","Activate File Symbols","Activate Context Window","Activate Window List","Activate File Search Bar","Activate Project Search Bar","Activate Search Results","Activate Source Editor"};
 for(guint i=0;i<G_N_ELEMENTS(focus);i++) { GtkWidget *entry=gtk_menu_item_new_with_label(focus[i]);gchar *name=g_strdup_printf("insight-activate-panel-%u",i);gtk_widget_set_name(entry,name);g_free(name);g_signal_connect(entry,"activate",G_CALLBACK(activate_panel),GUINT_TO_POINTER(i));gtk_container_add(GTK_CONTAINER(activation_menu),entry);keybindings_attach_menu(GEANY_KEY_GROUP_FOCUS,GEANY_KEYS_INSIGHT_ACTIVATEPROJECTFILES+i,entry); }
 const char *expand[]={"Expand Folders","Collapse Folders","Expand Symbol Categories","Collapse Symbol Categories","Expand File Types","Collapse File Types"};
 for(guint i=0;i<6;i++) { GtkWidget *entry=gtk_menu_item_new_with_label(expand[i]); g_signal_connect(entry,"activate",G_CALLBACK(expand_panel),GINT_TO_POINTER(i)); gtk_container_add(GTK_CONTAINER(menu),entry); }
 const gchar *branches[]={"Selected Folder Branch","Selected Symbol Category Branch","Selected File Type Branch"};
 const gchar *actions[]={"Expand Special…","Expand Selected Branch","Collapse Selected Branch"};
 for(guint p=0;p<3;p++) {
  GtkWidget *parent=gtk_menu_item_new_with_label(branches[p]),*submenu=gtk_menu_new();gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent),submenu);gtk_container_add(GTK_CONTAINER(menu),parent);
  for(guint a=0;a<3;a++) { GtkWidget *entry=gtk_menu_item_new_with_label(actions[a]);gchar *name=g_strdup_printf("insight-tree-branch-%u-%u",p,a);gtk_widget_set_name(entry,name);g_free(name);g_signal_connect(entry,"activate",G_CALLBACK(tree_branch_command),GUINT_TO_POINTER(p*3+a));gtk_container_add(GTK_CONTAINER(submenu),entry); }
 }
 GtkWidget *relations=gtk_menu_item_new_with_label("Relation Window"); gtk_container_add(GTK_CONTAINER(menu),relations); g_signal_connect(relations,"activate",G_CALLBACK(focus_relations),NULL);
 GtkWidget *project=gtk_check_menu_item_new_with_label("Project Window"),*local=gtk_check_menu_item_new_with_label("File Symbols Window"); wb.project_toggle=project; wb.local_toggle=local;
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(project),gtk_widget_get_visible(wb.project_notebook)); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(local),ui_prefs.sidebar_visible);
 g_signal_connect(project,"toggled",G_CALLBACK(project_visible),NULL); g_signal_connect(local,"toggled",G_CALLBACK(local_visible),NULL); gtk_container_add(GTK_CONTAINER(menu),project); gtk_container_add(GTK_CONTAINER(menu),local);
 GtkWidget *floating=gtk_menu_item_new_with_label("Float Project Window"),*docking=gtk_menu_item_new_with_label("Dock Project Window"); g_signal_connect(floating,"activate",G_CALLBACK(float_project),NULL); g_signal_connect(docking,"activate",G_CALLBACK(dock_project),NULL); gtk_container_add(GTK_CONTAINER(menu),floating); gtk_container_add(GTK_CONTAINER(menu),docking);
 GtkWidget *layouts=gtk_menu_item_new_with_label("Layouts"),*layout_menu=gtk_menu_new(); gtk_menu_item_set_submenu(GTK_MENU_ITEM(layouts),layout_menu); gtk_container_add(GTK_CONTAINER(view_menu),layouts);
 const char *names[]={"Load layout A","Load layout B","Load layout C","Load layout D"};
 for(guint i=0;i<4;i++) { GtkWidget *entry=gtk_menu_item_new_with_label(names[i]); gchar *id=g_strdup_printf("insight-layout-load-%u",i);gtk_widget_set_name(entry,id);g_free(id);g_signal_connect(entry,"activate",G_CALLBACK(apply_layout),GINT_TO_POINTER(i)); gtk_container_add(GTK_CONTAINER(layout_menu),entry); }
 GtkWidget *save=gtk_menu_item_new_with_label("Save current layout"),*save_menu=gtk_menu_new(); gtk_menu_item_set_submenu(GTK_MENU_ITEM(save),save_menu); gtk_container_add(GTK_CONTAINER(layout_menu),save);
 const char *slots[]={"Save as A","Save as B","Save as C","Save as D"}; for(guint i=0;i<4;i++) { GtkWidget *entry=gtk_menu_item_new_with_label(slots[i]); gchar *id=g_strdup_printf("insight-layout-save-%u",i);gtk_widget_set_name(entry,id);g_free(id);g_signal_connect(entry,"activate",G_CALLBACK(save_layout),GUINT_TO_POINTER(i)); gtk_container_add(GTK_CONTAINER(save_menu),entry); }
 const gchar *file_labels[]={"Load layout from file…","Save layout to file…"},*file_ids[]={"insight-layout-import","insight-layout-export"};
 for(guint i=0;i<2;i++) { GtkWidget *entry=gtk_menu_item_new_with_label(file_labels[i]);gtk_widget_set_name(entry,file_ids[i]);g_signal_connect(entry,"activate",G_CALLBACK(layout_file_command),GINT_TO_POINTER(i));gtk_container_add(GTK_CONTAINER(layout_menu),entry); }
 GtkWidget *help=gtk_menu_item_new_with_label("Window layouts and Tab Tray…");gtk_widget_set_name(help,"insight-layout-help");g_signal_connect(help,"activate",G_CALLBACK(insight_help_show),"window-layouts.md");gtk_container_add(GTK_CONTAINER(layout_menu),help);
 GtkWidget *reset=gtk_menu_item_new_with_label("Reset saved layouts to presets"); g_signal_connect(reset,"activate",G_CALLBACK(reset_layouts),NULL); gtk_container_add(GTK_CONTAINER(layout_menu),reset);
 g_signal_connect(menu,"show",G_CALLBACK(panel_menu_sync),NULL); gtk_widget_show_all(item); gtk_widget_show_all(layouts);
}
static void overview_visible(GtkCheckMenuItem *item,gpointer data) { (void)data; insight_overview_set_visible(gtk_check_menu_item_get_active(item)); }
enum { INFO_NAME,INFO_PATH,INFO_LINE,INFO_KIND,INFO_SCOPE,INFO_SIGNATURE,INFO_TYPE,INFO_INHERITANCE,INFO_DISPLAY,INFO_COLUMNS };
typedef struct { gchar *query,*path,*candidate;gulong line;gint kind; } InfoVisit;
typedef struct {
 GtkWidget *window,*entry,*choice,*details,*code,*status,*jump,*references,*leave,*kind,*count,*prototype,*implementation,*base,*back,*forward,*previous,*next,*options,*parse_local,*across_languages,*scope;GtkListStore *model;GHashTable *sources;
 GPtrArray *visits;guint visit;gboolean replaying;
 gchar *path,*digest,*name,*preferred,*preferred_candidate,*base_name,*base_path;gulong line,preferred_line,base_line;gboolean filling;guint center;InsightIndex *index;
} SymbolInfo;
static SymbolInfo *symbol_info;
static void info_show_query_kind(const gchar *,const gchar *,gulong,gint);
static void info_companion(GtkWidget *,gpointer);
static void info_kind_changed(GtkComboBox *,gpointer);
static void info_search(GtkWidget *,gpointer);
static void info_visit_free(gpointer data) { InfoVisit *visit=data;g_free(visit->query);g_free(visit->path);g_free(visit->candidate);g_free(visit); }
static void info_navigation_update(SymbolInfo *info) {
 gtk_widget_set_sensitive(info->back,info->visits->len && info->visit>0);
 gtk_widget_set_sensitive(info->forward,info->visits->len && info->visit+1<info->visits->len);
 gint active=gtk_combo_box_get_active(GTK_COMBO_BOX(info->choice)),count=gtk_tree_model_iter_n_children(GTK_TREE_MODEL(info->model),NULL);
 gtk_widget_set_sensitive(info->previous,active>0);gtk_widget_set_sensitive(info->next,active>=0 && active+1<count);
}
static void info_visit_selection(SymbolInfo *info) {
 if(info->replaying || !info->visits->len) return;
 InfoVisit *visit=g_ptr_array_index(info->visits,info->visit);
 if(strcmp(visit->query,gtk_entry_get_text(GTK_ENTRY(info->entry))) || visit->kind!=gtk_combo_box_get_active(GTK_COMBO_BOX(info->kind))) return;
 g_free(visit->path);visit->path=g_strdup(info->preferred);visit->line=info->preferred_line;
 g_free(visit->candidate);visit->candidate=g_strdup(info->preferred_candidate);
}
static void info_visit_record(SymbolInfo *info) {
 if(info->replaying) { info_navigation_update(info);return; }
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(info->entry));gint kind=gtk_combo_box_get_active(GTK_COMBO_BOX(info->kind));
 if(!*query || strlen(query)>1024) { info_navigation_update(info);return; }
 InfoVisit *current=info->visits->len ? g_ptr_array_index(info->visits,info->visit):NULL;
 if(!current || strcmp(current->query,query) || current->kind!=kind) {
  if(current) g_ptr_array_set_size(info->visits,info->visit+1);
  if(info->visits->len==64) g_ptr_array_remove_index(info->visits,0);
  InfoVisit *visit=g_new0(InfoVisit,1);visit->query=g_strdup(query);visit->kind=kind;g_ptr_array_add(info->visits,visit);info->visit=info->visits->len-1;
 }
 info_visit_selection(info);info_navigation_update(info);
}
static void info_history_step(GtkWidget *widget,gpointer data) {
 SymbolInfo *info=data;gboolean forward=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"insight-info-forward"));
 if(!info->visits->len || (forward ? info->visit+1>=info->visits->len:!info->visit)) return;
 info->visit=forward ? info->visit+1:info->visit-1;InfoVisit *visit=g_ptr_array_index(info->visits,info->visit);info->replaying=TRUE;info->filling=TRUE;
 gtk_entry_set_text(GTK_ENTRY(info->entry),visit->query);gtk_combo_box_set_active(GTK_COMBO_BOX(info->kind),visit->kind);
 g_free(info->preferred);info->preferred=g_strdup(visit->path);info->preferred_line=visit->line;g_free(info->preferred_candidate);info->preferred_candidate=g_strdup(visit->candidate);
 info->filling=FALSE;info_search(NULL,info);info->replaying=FALSE;info_navigation_update(info);
}
static void info_options_changed(GtkToggleButton *button,gpointer data) {
 (void)button;SymbolInfo *info=data;if(info->filling) return;
 wb.info_parse_local=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(info->parse_local));wb.info_across_languages=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(info->across_languages));wb.info_scope=gtk_combo_box_get_active(GTK_COMBO_BOX(info->scope));
 save_workbench_settings();info_search(NULL,info);
}
static void info_scope_changed(GtkComboBox *combo,gpointer data) { (void)combo;info_options_changed(NULL,data); }
static void info_candidate_step(GtkWidget *widget,gpointer data) {
 SymbolInfo *info=data;gint delta=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"insight-info-delta")),active=gtk_combo_box_get_active(GTK_COMBO_BOX(info->choice)),count=gtk_tree_model_iter_n_children(GTK_TREE_MODEL(info->model),NULL);
 if(active>=0 && active+delta>=0 && active+delta<count) gtk_combo_box_set_active(GTK_COMBO_BOX(info->choice),active+delta);
}
static GeanyDocument *info_document(const gchar *path) {
 if(g_str_has_prefix(path,"@buffer:")) { gchar *end=NULL;guint64 id=g_ascii_strtoull(path+8,&end,10);if(!end || *end || !id || id>G_MAXUINT) return NULL;GeanyDocument *doc=document_find_by_id((guint)id);return doc && !doc->file_name ? doc : NULL; }
 return document_find_by_filename(path);
}
static gchar *info_source(const gchar *path,GeanyFiletype **filetype) {
 GeanyDocument *doc=info_document(path);gchar *text=NULL;
 if(doc) { gint length=sci_get_length(doc->editor->sci);if(length>=0 && length<=2*1024*1024) { text=sci_get_contents(doc->editor->sci,length+1);if(!g_utf8_validate(text,length,NULL) || memchr(text,0,(gsize)length)) g_clear_pointer(&text,g_free); }if(filetype) *filetype=doc->file_type; }
 else if(!g_str_has_prefix(path,"@buffer:")) { text=read_disk_source(path,TRUE);if(filetype) *filetype=filetypes_detect_from_extension(path); }
 return text;
}
static gchar *info_candidate_identity(const gchar *path,gulong line,const gchar *name,const gchar *kind,const gchar *scope,const gchar *signature) {
 GVariant *value=g_variant_ref_sink(g_variant_new("(stssss)",path,(guint64)line,name,kind,scope,signature));gchar *key=g_variant_print(value,TRUE);g_variant_unref(value);return key;
}
static gboolean info_center(gpointer data) { SymbolInfo *info=data;info->center=0;SSM(SCINTILLA(info->code),SCI_VERTICALCENTRECARET,0,0);return G_SOURCE_REMOVE; }
static void info_choice(GtkComboBox *combo,gpointer data) {
 SymbolInfo *info=data;if(info->filling) return;GtkTreeIter iter;
 g_clear_pointer(&info->path,g_free);g_clear_pointer(&info->digest,g_free);g_clear_pointer(&info->name,g_free);info->line=0;
 g_clear_pointer(&info->base_name,g_free);g_clear_pointer(&info->base_path,g_free);info->base_line=0;gtk_widget_set_sensitive(info->prototype,FALSE);gtk_widget_set_sensitive(info->implementation,FALSE);gtk_widget_set_sensitive(info->base,FALSE);
 gtk_widget_set_sensitive(info->jump,FALSE);gtk_widget_set_sensitive(info->references,FALSE);gtk_widget_set_sensitive(info->leave,FALSE);
 if(!gtk_combo_box_get_active_iter(combo,&iter)) { info_navigation_update(info);return; }
 gchar *kind,*scope,*signature,*type,*inheritance;
 gtk_tree_model_get(GTK_TREE_MODEL(info->model),&iter,INFO_NAME,&info->name,INFO_PATH,&info->path,INFO_LINE,&info->line,INFO_KIND,&kind,INFO_SCOPE,&scope,INFO_SIGNATURE,&signature,INFO_TYPE,&type,INFO_INHERITANCE,&inheritance,-1);
 gchar *details=g_strdup_printf("%s%s\nKind: %s    Type: %s\nScope: %s\nLocation: %s:%lu\nInheritance: %s",info->name,signature,kind,type,scope,g_str_has_prefix(info->path,"@buffer:") ? "untitled" : info->path,info->line,inheritance);
 GString *description=g_string_new(details);g_free(details);
 g_free(info->preferred);info->preferred=g_strdup(info->path);info->preferred_line=info->line;g_free(info->preferred_candidate);info->preferred_candidate=info_candidate_identity(info->path,info->line,info->name,kind,scope,signature);
 GeanyFiletype *filetype=NULL;gchar *text=info_source(info->path,&filetype);ScintillaObject *code=SCINTILLA(info->code);
 guint callable_type=insight_symbol_kind_type(info->path,kind,filetype);gboolean function=(callable_type&(tm_tag_function_t|tm_tag_method_t|tm_tag_prototype_t))!=0;gtk_widget_set_sensitive(info->prototype,function);gtk_widget_set_sensitive(info->implementation,function);
 if(info->index && *type) {
  ScopeLanguage language={info->path,filetype};InsightSymbol declaration={info->name,info->path,scope,kind,info->line,signature,type,inheritance};
  gboolean callable=!strcmp(kind,"f") || !strcmp(kind,"p") || (!strcmp(kind,"m") && *signature);
  InsightTypeResult resolved=callable && member_calls_language(filetype) ? insight_type_resolve_return(info->index,&declaration,scope_language_eligible,&language) : insight_type_resolve(info->index,type,scope,info->path,scope_language_eligible,&language);
  if(resolved.status==INSIGHT_TYPE_FOUND) { gchar *line=insight_i18n_printf("\nNominal type: %s (alias hops: %u)",resolved.owner,resolved.aliases);g_string_append(description,line);g_free(line); }
  if(resolved.status==INSIGHT_TYPE_FOUND && resolved.symbol && info->sources && g_hash_table_contains(info->sources,resolved.symbol->path)) { info->base_name=g_strdup(resolved.owner);info->base_path=g_strdup(resolved.symbol->path);info->base_line=resolved.symbol->line;gtk_widget_set_sensitive(info->base,TRUE); }
  insight_type_result_clear(&resolved);
 }
 highlighting_set_styles(code,filetype ? filetype : filetypes[GEANY_FILETYPES_NONE]);sci_set_readonly(code,FALSE);sci_set_text(code,text ? text : "");sci_set_line_numbers(code,TRUE);SSM(code,SCI_EMPTYUNDOBUFFER,0,0);sci_set_readonly(code,TRUE);
 if(text && info->line && info->line<=(gulong)sci_get_line_count(code)) {
  InsightCodeDialect dialect;
  if((!strcmp(kind,"f") || (!strcmp(kind,"m") && *signature)) && source_dialect(filetype,&dialect)) {
   gsize length=strlen(text),start=(gsize)sci_get_position_from_line(code,info->line-1),body=0,end=0;
   gchar *mask=insight_source_code_mask(text,length,dialect,NULL);
   if(mask && insight_function_range(text,mask,length,start,function_style(dialect),&body,&end)==1 && end>start) {
    /* An inline line may contain several symbols: never measure the first
     * function's body for a different name on that same parser line. */
    gboolean named=FALSE;gsize size=strlen(info->name);
    for(gsize at=start;size && at+size<=body;at++) {
     if(mask[at]=='(' || mask[at]=='[') { size_t first=0,last=0;if(insight_select_match(mask,length,at,&first,&last)!=1 || last>body) break;at=last-1;continue; }
     if(!memcmp(mask+at,info->name,size) && (at==start || !(g_ascii_isalnum(mask[at-1]) || mask[at-1]=='_' || (guchar)mask[at-1]>=128)) && (at+size==length || !(g_ascii_isalnum(mask[at+size]) || mask[at+size]=='_' || (guchar)mask[at+size]>=128))) {
      gsize after=at+size;while(after<body && g_ascii_isspace(mask[after])) after++;
      if(after<body && mask[after]=='(') { named=TRUE;break; }
     }
    }
    if(named) { gulong last=(gulong)sci_get_line_from_position(code,(gint)end-1)+1;gchar *metrics=insight_i18n_printf("\nFunction size (lines): %lu (range %lu-%lu; lexical body)",last-info->line+1,info->line,last);g_string_append(description,metrics);g_free(metrics); }
   }
   g_free(mask);
  }
  guint tag_type=insight_symbol_kind_type(info->path,kind,filetype);
  const gchar *nominal=tag_type&tm_tag_class_t ? "class" : tag_type&tm_tag_struct_t ? "struct" : tag_type&tm_tag_union_t ? "union" : tag_type&tm_tag_enum_t ? "enum" : tag_type&tm_tag_namespace_t ? "namespace" : tag_type&tm_tag_interface_t ? "interface" : NULL;
  if(nominal && source_dialect(filetype,&dialect) && (dialect==INSIGHT_CODE_C || dialect==INSIGHT_CODE_PYTHON)) {
   gsize length=strlen(text),start=(gsize)sci_get_position_from_line(code,info->line-1),body=0,end=0;gchar *mask=insight_source_code_mask(text,length,dialect,NULL);
   if(mask && insight_nominal_range(text,mask,length,start,info->name,nominal,function_style(dialect),&body,&end)==1 && end>start) {
    gulong last=(gulong)sci_get_line_from_position(code,(gint)end-1)+1;gchar *metrics=insight_i18n_printf("\nDefinition size (lines): %lu (range %lu-%lu; lexical body)",last-info->line+1,info->line,last);g_string_append(description,metrics);g_free(metrics);
   }
   g_free(mask);
  }
  info->digest=source_digest(text);SSM(code,SCI_GOTOLINE,info->line-1,0);gtk_widget_set_sensitive(info->jump,TRUE);gtk_widget_set_sensitive(info->leave,info_document(info->path)==NULL && !g_str_has_prefix(info->path,"@buffer:"));
  gchar *status=g_strdup_printf("%d candidates • tag-based information • source snapshot",gtk_tree_model_iter_n_children(GTK_TREE_MODEL(info->model),NULL));gtk_label_set_text(GTK_LABEL(info->status),status);g_free(status);
  if(info->center) g_source_remove(info->center);
  info->center=g_idle_add(info_center,info);
 } else gtk_label_set_text(GTK_LABEL(info->status),"Source unavailable, larger than 2 MiB, or tag line out of range");
 gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(info->details)),description->str,-1);g_string_free(description,TRUE);
 g_free(kind);g_free(scope);g_free(signature);g_free(type);g_free(inheritance);
 gtk_widget_set_sensitive(info->references,TRUE);g_free(text);info_visit_selection(info);info_navigation_update(info);
}
static gboolean info_kind_matches(const InsightSymbol *symbol,gint category,GeanyFiletype *fallback) {
 if(!category) return TRUE;guint type=insight_symbol_kind_type(symbol->path,symbol->kind,fallback);
 switch(category) {
 case 1:return (type&tm_tag_prototype_t)!=0;
 case 2:return (type&(tm_tag_function_t|tm_tag_method_t))!=0 && !(type&tm_tag_prototype_t);
 case 3:return (type&(tm_tag_class_t|tm_tag_struct_t|tm_tag_union_t|tm_tag_interface_t|tm_tag_typedef_t|tm_tag_enum_t|tm_tag_namespace_t|tm_tag_package_t))!=0;
 case 4:return (type&(tm_tag_variable_t|tm_tag_externvar_t|tm_tag_local_var_t|tm_tag_field_t|tm_tag_member_t))!=0;
 default:return FALSE;
 }
}
/* Definitions in the shared Relations API use short names. Symbol Info also
 * accepts exact qualified owners; use sorted leaf lookup and exact scope so
 * unrelated namespaces never become counterpart candidates. */
static size_t info_lookup(const InsightIndex *index,const gchar *query,GHashTable *sources,const InsightSymbol **out,size_t capacity) {
 if(!query || !*query || strlen(query)>1024) return SIZE_MAX;
 while(g_str_has_prefix(query,"::")) query+=2;
 if(!*query) return SIZE_MAX;
 const gchar *leaf=query,*separator=NULL;
 for(const gchar *p=query;*p;p++) {
  if(p[0]==':' && p[1]==':') { separator=p;leaf=p+2;p++; }
  else if(*p=='.') { separator=p;leaf=p+1; }
 }
 if(!*leaf) return 0;
 size_t left=0,right=insight_index_count(index),scope_length=separator ? (size_t)(separator-query):0,total=0;
 while(left<right) { size_t mid=left+(right-left)/2;const InsightSymbol *symbol=insight_index_at(index,mid);if(strcmp(symbol->name,leaf)<0) left=mid+1;else right=mid; }
 for(size_t n=left;n<insight_index_count(index);n++) {
  const InsightSymbol *symbol=insight_index_at(index,n);if(strcmp(symbol->name,leaf)) break;
  if(separator && (strlen(symbol->scope)!=scope_length || memcmp(symbol->scope,query,scope_length))) continue;
  if(sources && !g_hash_table_contains(sources,symbol->path)) continue;
  if(out && total<capacity) out[total]=symbol;total++;
 }
 return total;
}
static void info_search(GtkWidget *widget,gpointer data) {
 (void)widget;SymbolInfo *info=data;gtk_label_set_text(GTK_LABEL(info->count),"0");gtk_widget_set_sensitive(info->prototype,FALSE);gtk_widget_set_sensitive(info->implementation,FALSE);gtk_widget_set_sensitive(info->base,FALSE);gtk_widget_set_sensitive(info->jump,FALSE);gtk_widget_set_sensitive(info->references,FALSE);gtk_widget_set_sensitive(info->leave,FALSE);
 if(!*gtk_entry_get_text(GTK_ENTRY(info->entry))) {
  info->filling=TRUE;gtk_list_store_clear(info->model);gtk_combo_box_set_active(GTK_COMBO_BOX(info->choice),-1);info->filling=FALSE;info_choice(GTK_COMBO_BOX(info->choice),info);
  gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(info->details)),"",-1);sci_set_readonly(SCINTILLA(info->code),FALSE);sci_set_text(SCINTILLA(info->code),"");sci_set_readonly(SCINTILLA(info->code),TRUE);gtk_label_set_text(GTK_LABEL(info->status),"Enter a symbol or qualified name");return;
 }
 refresh_index();InsightIndex *index=wb.index ? insight_index_clone(wb.index) : insight_index_new();
 if(!index) { gtk_label_set_text(GTK_LABEL(info->status),"Symbol index snapshot could not be created");return; }
 GeanyDocument *current=document_get_current();
 g_clear_pointer(&info->sources,g_hash_table_unref);info->sources=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 gchar *current_path=current ? (current->file_name ? g_strdup(current->file_name):g_strdup_printf("@buffer:%u",current->id)):NULL;
 if(wb.info_scope!=2) for(guint n=0;n<wb.files->len;n++) g_hash_table_add(info->sources,g_strdup(g_ptr_array_index(wb.files,n)));
 /* Without a workspace, project-only lookup uses the current named buffer. */
 if(current_path && (wb.info_scope!=1 || !wb.root)) g_hash_table_add(info->sources,g_strdup(current_path));
 gboolean source_error=FALSE;guint open_count=0;size_t tag_work=insight_index_count(index);
 if(wb.info_scope==0) for(guint n=0;n<documents_array->len;n++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,n);if(!DOC_VALID(doc) || !doc->file_name) continue;
  g_hash_table_add(info->sources,g_strdup(doc->file_name));if(doc==current || !doc->tm_file) continue;
  size_t tags=doc->tm_file->tags_array ? doc->tm_file->tags_array->len:0;
  if(++open_count>128 || sci_get_length(doc->editor->sci)>2*1024*1024 || tag_work>250000 || tags>250000-tag_work) { source_error=TRUE;break; }
  tag_work+=tags;if(!insight_native_import(index,doc->tm_file,doc->file_name)) { source_error=TRUE;break; }
 }
 if(current && current_path && g_hash_table_contains(info->sources,current_path) && sci_get_length(current->editor->sci)<=2*1024*1024) {
  /* The document generation already identifies the parsed buffer. Reusing it
   * avoids cancelling workers, dirtying the shared index and reparsing on every
   * category/history/Refresh action when the source has not changed. */
  if(wb.info_parse_local) bookmark_prepare_tags(current);
  if(current->tm_file) { if(!insight_native_import(index,current->tm_file,current->file_name)) source_error=TRUE; }
  else if(wb.info_parse_local && !current->file_name && current->file_type && current->file_type->lang>=0) {
   /* Untitled buffers have no workspace tag source. Parse a private in-memory
    * source, without creating a file or publishing it into the workspace. */
   TMSourceFile *source=tm_source_file_new(NULL,NULL);
   if(source) { source->file_name=g_strdup("untitled");source->short_name=source->file_name;source->lang=current->file_type->lang;gchar *path=g_strdup_printf("@buffer:%u",current->id);gint length=sci_get_length(current->editor->sci);gchar *text=sci_get_contents(current->editor->sci,length+1);if(!insight_native_parse(index,source,path,text,length)) source_error=TRUE;g_free(text);g_free(path);tm_source_file_free(source); }
  }
 }
 /* Filter BEFORE the 512-row presentation limit, including language filters.
  * Bound broad lookups explicitly instead of silently losing later matches. */
 size_t raw_total=source_error ? SIZE_MAX:info_lookup(index,gtk_entry_get_text(GTK_ENTRY(info->entry)),info->sources,NULL,0),total=0;gboolean broad=raw_total!=SIZE_MAX && raw_total>16384;const InsightSymbol **matches=NULL;
 if(raw_total==SIZE_MAX || broad) total=SIZE_MAX;
 else {
  matches=g_new(const InsightSymbol *,MAX((size_t)1,raw_total));size_t found=info_lookup(index,gtk_entry_get_text(GTK_ENTRY(info->entry)),info->sources,matches,raw_total);
  if(found!=raw_total) total=SIZE_MAX;
  else for(size_t n=0;n<raw_total;n++) if(g_hash_table_contains(info->sources,matches[n]->path) && (wb.info_across_languages || !current || g_str_has_prefix(matches[n]->path,"@buffer:") || insight_languages_search_path(current->file_name,current->file_type,matches[n]->path)) && info_kind_matches(matches[n],gtk_combo_box_get_active(GTK_COMBO_BOX(info->kind)),current ? current->file_type:NULL)) matches[total++]=matches[n];
 }
 gchar *counter=total==SIZE_MAX ? g_strdup("0"):g_strdup_printf("%u / %"G_GSIZE_FORMAT,(guint)MIN(total,(size_t)512),(gsize)total);gtk_label_set_text(GTK_LABEL(info->count),counter);g_free(counter);
 info->filling=TRUE;gtk_list_store_clear(info->model);gint chosen=0,preferred_rank=0;
 if(total!=SIZE_MAX) for(size_t i=0;i<MIN(total,(size_t)512);i++) {
  const InsightSymbol *hit=matches[i];GtkTreeIter iter;gchar *display=g_strdup_printf("%s %s%s • %s — %s:%lu",hit->kind,hit->name,hit->signature,hit->scope,g_str_has_prefix(hit->path,"@buffer:") ? "untitled" : hit->path,hit->line);
  gtk_list_store_append(info->model,&iter);gtk_list_store_set(info->model,&iter,INFO_NAME,hit->name,INFO_PATH,hit->path,INFO_LINE,hit->line,INFO_KIND,hit->kind,INFO_SCOPE,hit->scope,INFO_SIGNATURE,hit->signature,INFO_TYPE,hit->type,INFO_INHERITANCE,hit->inheritance,INFO_DISPLAY,display,-1);g_free(display);
  gint rank=!g_strcmp0(info->preferred,hit->path) ? (hit->line==info->preferred_line ? 2 : 1) : 0;
  if(rank==2 && info->preferred_candidate) { gchar *key=info_candidate_identity(hit->path,hit->line,hit->name,hit->kind,hit->scope,hit->signature);if(!strcmp(key,info->preferred_candidate)) rank=3;g_free(key); }
  if(rank>preferred_rank) { chosen=(gint)i;preferred_rank=rank; }
 }
 insight_index_free(info->index);info->index=index;gtk_combo_box_set_active(GTK_COMBO_BOX(info->choice),total!=SIZE_MAX && total ? chosen : -1);info->filling=FALSE;
 gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(info->details)),"",-1);sci_set_readonly(SCINTILLA(info->code),FALSE);sci_set_text(SCINTILLA(info->code),"");sci_set_readonly(SCINTILLA(info->code),TRUE);info_choice(GTK_COMBO_BOX(info->choice),info);
 g_free(matches);g_free(current_path);
 if(source_error) gtk_label_set_text(GTK_LABEL(info->status),"Symbol lookup snapshot could not be completed; narrow the source scope");
 else if(broad) gtk_label_set_text(GTK_LABEL(info->status),"More than 16384 name matches; use a qualified symbol name");
 else if(total==SIZE_MAX || !total) gtk_label_set_text(GTK_LABEL(info->status),total==SIZE_MAX ? "Symbol query could not be completed" : "No indexed candidates in this category; change the filter or qualified name");
 else if(total>512) gtk_label_set_text(GTK_LABEL(info->status),"Showing first 512 candidates; narrow the qualified symbol name");
 info_visit_record(info);
}
