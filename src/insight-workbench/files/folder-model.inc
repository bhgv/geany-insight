/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Folder trees and file counts.
 * RU: Деревья каталогов и подсчёт файлов. */

static void folder_count(guint shown,guint total) {
 g_object_set_data(G_OBJECT(wb.tree_status),"insight-folder-count",GUINT_TO_POINTER(shown));
 g_object_set_data(G_OBJECT(wb.tree_status),"insight-folder-total",GUINT_TO_POINTER(total));
 gchar *label=insight_i18n_printf("Folder files: %u / %u",shown,total); gtk_label_set_text(GTK_LABEL(wb.tree_status),label); g_free(label);
}
static gboolean folder_locate(gboolean report) {
 GeanyDocument *doc=document_get_current();
 if(!doc || !doc->file_name) { if(report) ui_set_statusbar(TRUE,"Current file has no project path"); return FALSE; }
 FolderVisit visit={.wanted=doc->file_name}; gtk_tree_model_foreach(GTK_TREE_MODEL(wb.project_tree),folder_visit,&visit);
 if(!visit.found) { if(report) ui_set_statusbar(TRUE,"Current file is outside the project tree or hidden by its filter"); return FALSE; }
 wb.tree_setting=TRUE; gtk_tree_view_expand_to_path(GTK_TREE_VIEW(wb.tree_view),visit.found);
 gtk_tree_view_set_cursor(GTK_TREE_VIEW(wb.tree_view),visit.found,NULL,FALSE);
 gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(wb.tree_view),visit.found,NULL,TRUE,0.5,0);
 wb.tree_setting=FALSE; gtk_tree_path_free(visit.found); return TRUE;
}
static void tree_refresh(void) {
 if(!wb.project_tree || !gtk_widget_get_mapped(wb.tree_view)) return;
 GError *error=NULL; InsightLookupFilter *filter=insight_lookup_filter_new(gtk_entry_get_text(GTK_ENTRY(wb.tree_entry)),TRUE,&error);
 if(!filter) { gtk_widget_set_tooltip_text(wb.tree_entry,error ? error->message : "Invalid folder filter"); gtk_style_context_add_class(gtk_widget_get_style_context(wb.tree_entry),"error"); g_clear_error(&error); return; }
 gtk_style_context_remove_class(gtk_widget_get_style_context(wb.tree_entry),"error");
 gtk_widget_set_tooltip_text(wb.tree_entry,"Filter project paths: fragments, glob, ?regex or ~fuzzy; ancestors stay visible");
 /* Text edits do not change the folder model. Keep expanded rows and selection
  * intact unless membership, root or the path filter actually changed. */
 g_ptr_array_sort(wb.files,project_path_compare);GChecksum *sum=g_checksum_new(G_CHECKSUM_SHA256);const gchar *query=gtk_entry_get_text(GTK_ENTRY(wb.tree_entry));g_checksum_update(sum,(const guchar *)query,strlen(query)+1);if(wb.root) g_checksum_update(sum,(const guchar *)wb.root,strlen(wb.root)+1);
 for(guint n=0;n<wb.files->len;n++) { const gchar *path=g_ptr_array_index(wb.files,n);g_checksum_update(sum,(const guchar *)path,strlen(path)+1); }
 const gchar *key=g_checksum_get_string(sum);gboolean same=!g_strcmp0(key,g_object_get_data(G_OBJECT(wb.project_tree),"insight-request-key"));if(!same) g_object_set_data_full(G_OBJECT(wb.project_tree),"insight-request-key",g_strdup(key),g_free);g_checksum_free(sum);
 if(same) { insight_lookup_filter_free(filter);if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.tree_follow))) folder_locate(FALSE);return; }
 gboolean directory=FALSE; gchar *selected=folder_selected_path(&directory);
 GHashTable *expanded=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL); FolderVisit visit={.expanded=expanded};
 gtk_tree_model_foreach(GTK_TREE_MODEL(wb.project_tree),folder_visit,&visit);
 wb.tree_setting=TRUE; gtk_tree_store_clear(wb.project_tree);
 GHashTable *nodes=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free); gchar *root=wb.root ? utils_get_utf8_from_locale(wb.root) : g_strdup(""); guint shown=0;
 for(guint i=0;i<wb.files->len && i<10000;i++) {
  const char *path=g_ptr_array_index(wb.files,i); if(!insight_lookup_filter_match(filter,NULL,NULL,path,NULL)) continue;
  gchar *parent=g_path_get_dirname(path),*name=g_path_get_basename(path);
  GtkTreeIter *above=tree_directory(nodes,parent,root,0),iter; gtk_tree_store_append(wb.project_tree,&iter,above);
  gtk_tree_store_set(wb.project_tree,&iter,0,name,1,path,2,FALSE,-1); shown++; g_free(parent); g_free(name);
 }
 gtk_tree_model_foreach(GTK_TREE_MODEL(wb.project_tree),folder_restore_expanded,expanded);
 if(*gtk_entry_get_text(GTK_ENTRY(wb.tree_entry))) gtk_tree_view_expand_all(GTK_TREE_VIEW(wb.tree_view));
 if(selected) { FolderVisit restore={.wanted=selected}; gtk_tree_model_foreach(GTK_TREE_MODEL(wb.project_tree),folder_visit,&restore); if(restore.found) { gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.tree_view)),restore.found); gtk_tree_path_free(restore.found); } }
 wb.tree_setting=FALSE; folder_count(shown,wb.files->len);
 g_free(selected); g_free(root); g_hash_table_destroy(nodes); g_hash_table_destroy(expanded); insight_lookup_filter_free(filter);
 if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.tree_follow))) folder_locate(FALSE);
}
static gboolean folder_filter_idle(gpointer data) { (void)data; wb.tree_filter_idle=0; if(wb.alive) tree_refresh(); return G_SOURCE_REMOVE; }
static void folder_filter_changed(GtkEditable *entry,gpointer data) {
 (void)entry; (void)data; if(wb.tree_filter_idle) g_source_remove(wb.tree_filter_idle); wb.tree_filter_idle=g_timeout_add(120,folder_filter_idle,NULL);
}
static void tree_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data; if(wb.tree_setting && gtk_tree_selection_get_tree_view(selection)==GTK_TREE_VIEW(wb.tree_view)) return;
 GtkTreeModel *model; GtkTreeIter iter; if(!gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 gchar *path; gboolean directory; gtk_tree_model_get(model,&iter,1,&path,2,&directory,-1);
 if(!directory) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); show_location(path,0,1,TRUE); } g_free(path);
}
static void tree_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; (void)data; GtkTreeIter iter; GtkTreeModel *model=gtk_tree_view_get_model(view); if(!gtk_tree_model_get_iter(model,&iter,path)) return;
 gboolean directory; gtk_tree_model_get(model,&iter,2,&directory,-1);
 if(directory) { if(gtk_tree_view_row_expanded(view,path)) gtk_tree_view_collapse_row(view,path); else gtk_tree_view_expand_row(view,path,FALSE); return; }
 GtkTreeSelection *selection=gtk_tree_view_get_selection(view); gboolean was_setting=wb.tree_setting; GroupedPanel *grouped=g_object_get_data(G_OBJECT(view),"insight-grouped-panel"); gboolean grouped_setting=grouped ? grouped->setting : FALSE; if(grouped) grouped->setting=TRUE;
 if(GTK_WIDGET(view)==wb.tree_view) wb.tree_setting=TRUE;
 gtk_tree_selection_select_path(selection,path); wb.tree_setting=was_setting; if(grouped) grouped->setting=grouped_setting;
 gchar *source=NULL; gtk_tree_model_get(model,&iter,1,&source,-1);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
 if(source && show_location(source,0,1,TRUE)) open_context(NULL,NULL);
 g_free(source);
}
enum { FOLDER_OPEN,FOLDER_PATH,FOLDER_RELATIVE,FOLDER_LOCATE,FOLDER_EXPAND,FOLDER_COLLAPSE,FOLDER_EXPAND_ALL,FOLDER_COLLAPSE_ALL,FOLDER_CLEAR,FOLDER_HELP };
static void folder_action(guint action) {
 gboolean directory=FALSE; gchar *path=folder_selected_path(&directory);
 switch(action) {
 case FOLDER_OPEN:
  if(path && !directory) { FolderVisit visit={.wanted=path}; gtk_tree_model_foreach(GTK_TREE_MODEL(wb.project_tree),folder_visit,&visit); if(visit.found) { tree_activated(GTK_TREE_VIEW(wb.tree_view),visit.found,NULL,NULL); gtk_tree_path_free(visit.found); } } break;
 case FOLDER_PATH: case FOLDER_RELATIVE:
  if(path) {
   gchar *copy=g_strdup(path);
   if(action==FOLDER_RELATIVE && wb.root) { gchar *root=utils_get_utf8_from_locale(wb.root); GFile *base=g_file_new_for_path(root),*file=g_file_new_for_path(path); gchar *relative=g_file_get_relative_path(base,file); if(relative) { g_free(copy); copy=relative; } g_object_unref(base); g_object_unref(file); g_free(root); }
   gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),copy,-1); g_free(copy);
  } break;
 case FOLDER_LOCATE: folder_locate(TRUE); break;
 case FOLDER_EXPAND: case FOLDER_COLLAPSE:
  if(path && directory) { FolderVisit visit={.wanted=path}; gtk_tree_model_foreach(GTK_TREE_MODEL(wb.project_tree),folder_visit,&visit); if(visit.found) { if(action==FOLDER_EXPAND) gtk_tree_view_expand_row(GTK_TREE_VIEW(wb.tree_view),visit.found,TRUE); else gtk_tree_view_collapse_row(GTK_TREE_VIEW(wb.tree_view),visit.found); gtk_tree_path_free(visit.found); } } break;
 case FOLDER_EXPAND_ALL: gtk_tree_view_expand_all(GTK_TREE_VIEW(wb.tree_view)); break;
 case FOLDER_COLLAPSE_ALL: gtk_tree_view_collapse_all(GTK_TREE_VIEW(wb.tree_view)); break;
 case FOLDER_CLEAR: gtk_entry_set_text(GTK_ENTRY(wb.tree_entry),""); gtk_widget_grab_focus(wb.tree_entry); break;
 case FOLDER_HELP: insight_help_show(NULL,"panel-project-folders.md"); break;
 }
 g_free(path);
}
static void folder_menu_action(GtkMenuItem *item,gpointer data) { (void)item; folder_action(GPOINTER_TO_UINT(data)); }
static GtkWidget *folder_menu(void) {
 GtkWidget *menu=gtk_menu_new(); gtk_menu_attach_to_widget(GTK_MENU(menu),wb.tree_view,NULL); g_object_set_data(G_OBJECT(wb.tree_view),"insight-folder-popup",menu);
 gboolean directory=FALSE; gchar *path=folder_selected_path(&directory);
 const gchar *labels[]={"Open project file","Copy full folder/file path","Copy project-relative path","Locate current file in folders","Expand selected folder branch","Collapse selected folder branch","Expand all folders","Collapse all folders","Clear folder filter","Panel help"};
 for(guint i=0;i<G_N_ELEMENTS(labels);i++) {
  GtkWidget *item=gtk_menu_item_new_with_label(labels[i]); gchar *name=g_strdup_printf("insight-folder-action-%u",i); gtk_widget_set_name(item,name); g_free(name);
  gtk_widget_set_sensitive(item,i==FOLDER_OPEN ? path && !directory : (i==FOLDER_PATH || i==FOLDER_RELATIVE ? path!=NULL : (i==FOLDER_EXPAND || i==FOLDER_COLLAPSE ? path && directory : TRUE)));
  g_signal_connect(item,"activate",G_CALLBACK(folder_menu_action),GUINT_TO_POINTER(i)); gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
 }
 g_free(path); g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu); insight_i18n_apply(menu); gtk_widget_show_all(menu); return menu;
}
static gboolean folder_popup_key(GtkWidget *widget,gpointer data) { (void)widget; (void)data; gtk_menu_popup_at_widget(GTK_MENU(folder_menu()),wb.tree_view,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,NULL); return TRUE; }
static gboolean folder_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)data; if(event->type!=GDK_BUTTON_PRESS || event->button!=3) return FALSE;
 wb.tree_setting=TRUE; GtkTreePath *row=NULL; GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
 if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),(gint)event->x,(gint)event->y,&row,NULL,NULL,NULL)) { gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget),row,NULL,FALSE); gtk_tree_path_free(row); } else gtk_tree_selection_unselect_all(selection);
 wb.tree_setting=FALSE; gtk_widget_grab_focus(widget); gtk_menu_popup_at_pointer(GTK_MENU(folder_menu()),(GdkEvent *)event); return TRUE;
}
static gboolean folder_first_file(GtkTreeModel *model,GtkTreePath *row,GtkTreeIter *iter,gpointer data) {
 gboolean directory=FALSE; gtk_tree_model_get(model,iter,2,&directory,-1); if(directory) return FALSE;
 *(GtkTreePath **)data=gtk_tree_path_copy(row); return TRUE;
}
static gboolean folder_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)data; guint mods=event->state&gtk_accelerator_get_default_mod_mask(),key=gdk_keyval_to_lower(event->keyval);
 if(widget==wb.tree_entry) {
  if(key==GDK_KEY_Escape && !mods) { if(*gtk_entry_get_text(GTK_ENTRY(widget))) gtk_entry_set_text(GTK_ENTRY(widget),""); else editor_focus(); return TRUE; }
  if((key==GDK_KEY_Down || key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) && !mods) {
   if(wb.tree_filter_idle) { g_source_remove(wb.tree_filter_idle); wb.tree_filter_idle=0; } tree_refresh();
   if(gtk_style_context_has_class(gtk_widget_get_style_context(wb.tree_entry),"error")) return TRUE;
   GtkTreePath *row=NULL; gtk_tree_model_foreach(GTK_TREE_MODEL(wb.project_tree),folder_first_file,&row);
   if(row) { gtk_tree_view_expand_to_path(GTK_TREE_VIEW(wb.tree_view),row); gtk_tree_view_set_cursor(GTK_TREE_VIEW(wb.tree_view),row,NULL,FALSE); gtk_tree_path_free(row); gtk_widget_grab_focus(wb.tree_view); if(key!=GDK_KEY_Down) folder_action(FOLDER_OPEN); } return TRUE;
  } return FALSE;
 }
 if((key==GDK_KEY_Menu && !mods) || (key==GDK_KEY_F10 && mods==GDK_SHIFT_MASK)) return folder_popup_key(widget,NULL);
 if(key==GDK_KEY_f && mods==GEANY_PRIMARY_MOD_MASK) { gtk_widget_grab_focus(wb.tree_entry); return TRUE; }
 if(key==GDK_KEY_c && (mods==GEANY_PRIMARY_MOD_MASK || mods==(GEANY_PRIMARY_MOD_MASK|GDK_SHIFT_MASK))) { folder_action(mods&GDK_SHIFT_MASK ? FOLDER_RELATIVE : FOLDER_PATH); return TRUE; }
 if((key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) && mods==GEANY_PRIMARY_MOD_MASK) { folder_action(FOLDER_OPEN); return TRUE; }
 if(key==GDK_KEY_l && mods==GEANY_PRIMARY_MOD_MASK) { folder_action(FOLDER_LOCATE); return TRUE; }
 if(key==GDK_KEY_Escape && !mods) { editor_focus(); return TRUE; } return FALSE;
}
static void folder_locate_clicked(GtkButton *button,gpointer data) { (void)button; (void)data; folder_locate(TRUE); }
static void folder_follow_changed(GtkToggleButton *button,gpointer data) { (void)data; if(gtk_toggle_button_get_active(button)) folder_locate(FALSE); }
static void folder_mapped(GtkWidget *widget,gpointer data) { (void)widget; (void)data; tree_refresh(); }
static void disk_file_open(const gchar *path,gboolean activate,gpointer data) {
 (void)data;if(!wb.alive) return;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);if(show_location(path,0,1,TRUE) && activate) open_context(NULL,NULL);
}
static void disk_current_file(void) {
 if(!wb.alive || !wb.disk_browser || !wb.folder_mode) return;
 GeanyDocument *doc=document_get_current();
 insight_disk_browser_current_file(wb.disk_browser,doc && doc->is_valid ? doc->file_name : NULL,gtk_combo_box_get_active(GTK_COMBO_BOX(wb.folder_mode))==1);
}
static void folder_mode_changed(GtkComboBox *combo,gpointer data) {
 (void)data;gboolean disk=gtk_combo_box_get_active(combo)==1;gtk_stack_set_visible_child_name(GTK_STACK(wb.folder_stack),disk ? "disk" : "project");
 if(disk) { gchar *root=wb.root ? utils_get_utf8_from_locale(wb.root) : g_get_current_dir();insight_disk_browser_resume(wb.disk_browser,root);g_free(root); }else insight_disk_browser_cancel(wb.disk_browser);
 disk_current_file();
}
static void create_project_tree(void) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3),*row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);
 wb.tree_entry=gtk_search_entry_new(); gtk_widget_set_name(wb.tree_entry,"insight-folder-query"); gtk_entry_set_max_length(GTK_ENTRY(wb.tree_entry),1024); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.tree_entry),"Filter project folders/files…"); gtk_box_pack_start(GTK_BOX(row),wb.tree_entry,TRUE,TRUE,0);
 GtkWidget *locate=gtk_button_new(); gtk_widget_set_name(locate,"insight-folder-locate"); gtk_button_set_image(GTK_BUTTON(locate),insight_command_icon("mark-location-symbolic")); gtk_widget_set_tooltip_text(locate,"Locate current file in folders"); gtk_box_pack_start(GTK_BOX(row),locate,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);
 wb.tree_follow=gtk_check_button_new_with_label("Follow current file"); gtk_widget_set_name(wb.tree_follow,"insight-folder-follow"); gtk_box_pack_start(GTK_BOX(box),wb.tree_follow,FALSE,FALSE,0);
 wb.tree_status=gtk_label_new(""); gtk_widget_set_name(wb.tree_status,"insight-folder-count"); gtk_label_set_xalign(GTK_LABEL(wb.tree_status),0); gtk_box_pack_start(GTK_BOX(box),wb.tree_status,FALSE,FALSE,0);
 wb.project_tree=gtk_tree_store_new(3,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN); wb.tree_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.project_tree)); gtk_widget_set_name(wb.tree_view,"insight-project-folders");
 gtk_tree_view_append_column(GTK_TREE_VIEW(wb.tree_view),gtk_tree_view_column_new_with_attributes("Project folders",gtk_cell_renderer_text_new(),"text",0,NULL));
 gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(wb.project_tree),folder_compare,NULL,NULL); gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.project_tree),GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,GTK_SORT_ASCENDING);
 gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.tree_view),1); GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.tree_view); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 GtkWidget *appearance=insight_list_style_button(wb.tree_view);gtk_widget_set_name(appearance,"insight-folder-appearance");gtk_box_pack_start(GTK_BOX(box),appearance,FALSE,FALSE,0);
 GtkWidget *host=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);wb.folder_mode=gtk_combo_box_text_new();gtk_widget_set_name(wb.folder_mode,"insight-folder-mode");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.folder_mode),"Project files");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.folder_mode),"Browse disk");gtk_combo_box_set_active(GTK_COMBO_BOX(wb.folder_mode),0);gtk_box_pack_start(GTK_BOX(host),wb.folder_mode,FALSE,FALSE,0);
 wb.folder_stack=gtk_stack_new();wb.disk_browser=insight_disk_browser_new(disk_file_open,NULL);gtk_stack_add_named(GTK_STACK(wb.folder_stack),box,"project");gtk_stack_add_named(GTK_STACK(wb.folder_stack),wb.disk_browser,"disk");gtk_stack_set_visible_child_name(GTK_STACK(wb.folder_stack),"project");gtk_box_pack_start(GTK_BOX(host),wb.folder_stack,TRUE,TRUE,0);gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),host,gtk_label_new("Folders"));g_signal_connect(wb.folder_mode,"changed",G_CALLBACK(folder_mode_changed),NULL);gtk_widget_show_all(host);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.tree_view)),"changed",G_CALLBACK(tree_selected),NULL); g_signal_connect(wb.tree_view,"row-activated",G_CALLBACK(tree_activated),NULL);
 g_signal_connect(wb.tree_view,"key-press-event",G_CALLBACK(folder_key),NULL); g_signal_connect(wb.tree_entry,"key-press-event",G_CALLBACK(folder_key),NULL); g_signal_connect(wb.tree_entry,"changed",G_CALLBACK(folder_filter_changed),NULL);
 g_signal_connect(wb.tree_view,"button-press-event",G_CALLBACK(folder_button),NULL); g_signal_connect(wb.tree_view,"popup-menu",G_CALLBACK(folder_popup_key),NULL); g_signal_connect(wb.tree_view,"map",G_CALLBACK(folder_mapped),NULL);
 g_signal_connect(locate,"clicked",G_CALLBACK(folder_locate_clicked),NULL); g_signal_connect(wb.tree_follow,"toggled",G_CALLBACK(folder_follow_changed),NULL); folder_count(0,0); gtk_widget_show_all(box);
}
/* Shared interaction/state for the two grouped project browsers. */
static const gchar *grouped_prefix(GroupedPanel *p) { return p->symbols ? "symbol-categories" : "file-types"; }
static gboolean grouped_row(GtkTreeModel *model,GtkTreeIter *iter,GroupedPanel *p,gchar **name,gchar **path,gulong *line) {
 if(p->symbols) { gtk_tree_model_get(model,iter,SYMBOL_NAME,name,SYMBOL_PATH,path,SYMBOL_LINE,line,-1); return *line!=0; }
 gboolean group=FALSE; gtk_tree_model_get(model,iter,0,name,1,path,2,&group,-1); *line=group ? 0 : 1; return !group;
}
static gchar *grouped_key(GtkTreeModel *model,GtkTreeIter *iter,GroupedPanel *p) {
 GString *key=g_string_new(NULL); GtkTreeIter current=*iter;
 for(guint depth=0;depth<64;depth++) {
  gchar *name=NULL,*path=NULL,*scope=NULL,*kind=NULL; gulong line=0; grouped_row(model,&current,p,&name,&path,&line);
  if(p->symbols) gtk_tree_model_get(model,&current,SYMBOL_SCOPE,&scope,SYMBOL_KIND,&kind,-1);
  gchar *header=p->symbols && !line ? g_strdup_printf("%s:%s",kind ? kind : "",scope ? scope : "") : NULL;
  const gchar *value=header ? header : name;
  g_string_append_printf(key,"%lu:%zu:%s:%zu:%s;",line,value ? strlen(value) : 0,value ? value : "",path ? strlen(path) : 0,path ? path : "");
  g_free(header); g_free(name); g_free(path); g_free(scope); g_free(kind); GtkTreeIter parent; if(!gtk_tree_model_iter_parent(model,&parent,&current)) break; current=parent;
 }
 return g_string_free(key,FALSE);
}
typedef struct { GroupedPanel *panel; GHashTable *expanded; const gchar *wanted; GtkTreePath *found; gboolean restore; } GroupedVisit;
static gboolean grouped_visit(GtkTreeModel *model,GtkTreePath *row,GtkTreeIter *iter,gpointer data) {
 GroupedVisit *v=data; gchar *key=grouped_key(model,iter,v->panel);
 if(v->expanded) {
  if(v->restore && g_hash_table_contains(v->expanded,key)) { gtk_tree_view_expand_to_path(GTK_TREE_VIEW(v->panel->view),row); gtk_tree_view_expand_row(GTK_TREE_VIEW(v->panel->view),row,FALSE); }
  else if(!v->restore && gtk_tree_view_row_expanded(GTK_TREE_VIEW(v->panel->view),row)) g_hash_table_add(v->expanded,g_strdup(key));
 }
 if(v->wanted && !strcmp(key,v->wanted)) v->found=gtk_tree_path_copy(row);
 g_free(key); return v->found!=NULL;
}
static gchar *grouped_capture(GroupedPanel *p,GHashTable *expanded) {
 GtkTreeIter iter; GtkTreeModel *model; gchar *selected=NULL;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)),&model,&iter)) selected=grouped_key(model,&iter,p);
 GroupedVisit v={.panel=p,.expanded=expanded}; gtk_tree_model_foreach(GTK_TREE_MODEL(p->store),grouped_visit,&v); p->setting=TRUE; return selected;
}
static void grouped_restore(GroupedPanel *p,GHashTable *expanded,gchar *selected) {
 GroupedVisit v={.panel=p,.expanded=expanded,.restore=TRUE}; gtk_tree_model_foreach(GTK_TREE_MODEL(p->store),grouped_visit,&v);
 if(*gtk_entry_get_text(GTK_ENTRY(p->entry))) gtk_tree_view_expand_all(GTK_TREE_VIEW(p->view));
 if(selected) { v=(GroupedVisit){.panel=p,.wanted=selected}; gtk_tree_model_foreach(GTK_TREE_MODEL(p->store),grouped_visit,&v); if(v.found) { gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)),v.found); gtk_tree_path_free(v.found); } }
 p->setting=FALSE; g_free(selected); g_hash_table_destroy(expanded);
}
static void grouped_count(GroupedPanel *p,guint shown,guint total) {
 p->shown=shown; p->total=total;
 gchar *text=insight_i18n_printf(p->symbols ? "Category symbols: %u / %u" : "Type files: %u / %u",shown,total); gtk_label_set_text(GTK_LABEL(p->status),text); g_free(text);
}
static void grouped_refresh(GroupedPanel *p) { if(p->symbols) categories_filter(NULL,NULL); else file_types_refresh(); }
static gboolean grouped_idle(gpointer data) { GroupedPanel *p=data; p->timer=0; if(wb.alive) grouped_refresh(p); return G_SOURCE_REMOVE; }
static void grouped_changed(GtkEditable *entry,gpointer data) { (void)entry; GroupedPanel *p=data; p->limit=p->symbols ? 5000 : 10000; if(p->timer) g_source_remove(p->timer); p->timer=g_timeout_add(120,grouped_idle,p); }
static void grouped_flush(GroupedPanel *p) { if(p->timer) { g_source_remove(p->timer); p->timer=0; grouped_refresh(p); } }
static void grouped_selected(GtkTreeSelection *selection,gpointer data) { GroupedPanel *p=data; if(p->setting) return; if(p->symbols) browser_selected(selection,NULL); else tree_selected(selection,NULL); }
enum { GP_OPEN,GP_NAME,GP_PATH,GP_LOCATION,GP_INFO,GP_REFERENCES,GP_FILTER,GP_CLEAR,GP_EXPAND,GP_COLLAPSE,GP_EXPAND_ALL,GP_COLLAPSE_ALL,GP_COLUMNS,GP_RESET,GP_MORE,GP_HELP };
static void grouped_action(GroupedPanel *p,guint action) {
 grouped_flush(p); GtkTreeModel *model=NULL; GtkTreeIter iter; gchar *name=NULL,*path=NULL; gulong line=0;
 gboolean selected=gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)),&model,&iter),leaf=selected && grouped_row(model,&iter,p,&name,&path,&line);
 switch(action) {
 case GP_OPEN: if(leaf) { GtkTreePath *row=gtk_tree_model_get_path(model,&iter); if(p->symbols) category_activated(GTK_TREE_VIEW(p->view),row,NULL,NULL); else tree_activated(GTK_TREE_VIEW(p->view),row,NULL,NULL); gtk_tree_path_free(row); } break;
 case GP_NAME: if(leaf) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),name,-1); break;
 case GP_PATH: if(leaf && path && path[0]!='@') gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),path,-1); break;
 case GP_LOCATION: if(leaf && path && path[0]!='@') { gchar *text=p->symbols ? g_strdup_printf("%s:%lu",path,line) : g_strdup(path); gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text,-1); g_free(text); } break;
 case GP_INFO: if(leaf && p->symbols) symbol_action(GTK_TREE_VIEW(p->view),SP_INFO); break;
 case GP_REFERENCES: if(leaf && p->symbols) symbol_action(GTK_TREE_VIEW(p->view),SP_REFERENCES); break;
 case GP_FILTER: if(leaf) { gchar *query=g_strdup_printf(p->symbols ? "\"%s\"" : "file:\"%s\"",p->symbols ? name : path); if(strlen(query)<=1024) gtk_entry_set_text(GTK_ENTRY(p->entry),query); else ui_set_statusbar(TRUE,"Selected value is too long for a lookup filter"); g_free(query); } break;
 case GP_CLEAR: gtk_entry_set_text(GTK_ENTRY(p->entry),""); gtk_widget_grab_focus(p->entry); break;
 case GP_EXPAND: case GP_COLLAPSE:
  if(selected) { GtkTreePath *row=gtk_tree_model_get_path(model,&iter); if(leaf && !gtk_tree_model_iter_has_child(model,&iter)) gtk_tree_path_up(row); if(gtk_tree_path_get_depth(row)) { if(action==GP_EXPAND) gtk_tree_view_expand_row(GTK_TREE_VIEW(p->view),row,TRUE); else gtk_tree_view_collapse_row(GTK_TREE_VIEW(p->view),row); } gtk_tree_path_free(row); } break;
 case GP_EXPAND_ALL: gtk_tree_view_expand_all(GTK_TREE_VIEW(p->view)); break;
 case GP_COLLAPSE_ALL: gtk_tree_view_collapse_all(GTK_TREE_VIEW(p->view)); break;
 case GP_RESET: insight_symbol_view_columns_reset(GTK_TREE_VIEW(p->view)); break;
 case GP_MORE: if(p->symbols && p->limit<250000) { p->limit=MIN(250000,p->limit+5000); grouped_refresh(p); } break;
 case GP_HELP: insight_help_show(NULL,p->symbols ? "panel-categories.md" : "panel-file-types.md"); break;
 default: break;
 }
 g_free(name); g_free(path);
}
static void grouped_command(GtkMenuItem *item,gpointer data) { grouped_action(data,GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"insight-grouped-action"))); }
static GtkWidget *grouped_menu(GroupedPanel *p) {
 grouped_flush(p); GtkWidget *menu=gtk_menu_new(); gtk_menu_attach_to_widget(GTK_MENU(menu),p->view,NULL); g_object_set_data(G_OBJECT(p->view),"insight-grouped-popup",menu);
 GtkTreeModel *model=NULL; GtkTreeIter iter; gchar *name=NULL,*path=NULL; gulong line=0;
 gboolean selected=gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)),&model,&iter),leaf=selected && grouped_row(model,&iter,p,&name,&path,&line);
 const gchar *labels[]={"Open selected source","Copy selected name","Copy source path","Copy source location","Symbol Info…","Find usages…","Filter to selected item","Clear panel filter","Expand selected branch","Collapse selected branch","Expand all groups","Collapse all groups","Visible columns","Reset panel columns","Load more category symbols","Panel help"};
 for(guint i=0;i<G_N_ELEMENTS(labels);i++) {
  if(!p->symbols && (i==GP_INFO || i==GP_REFERENCES || i==GP_MORE)) continue;
  GtkWidget *item=gtk_menu_item_new_with_label(labels[i]); gchar *id=g_strdup_printf("insight-grouped-action-%u",i); gtk_widget_set_name(item,id); g_free(id); g_object_set_data(G_OBJECT(item),"insight-grouped-action",GUINT_TO_POINTER(i));
  gboolean enabled=i<=GP_FILTER ? leaf : (i==GP_EXPAND || i==GP_COLLAPSE ? selected : (i==GP_MORE ? p->shown<p->total && p->limit<250000 : TRUE));
  if((i==GP_PATH || i==GP_LOCATION) && (!path || path[0]=='@')) enabled=FALSE;
  gtk_widget_set_sensitive(item,enabled);
  if(i==GP_COLUMNS) gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),insight_symbol_view_columns_menu(GTK_TREE_VIEW(p->view))); else g_signal_connect(item,"activate",G_CALLBACK(grouped_command),p);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
 }
 g_free(name); g_free(path); g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu); insight_i18n_apply(menu); gtk_widget_show_all(menu); return menu;
}
static gboolean grouped_popup(GtkWidget *widget,gpointer data) { (void)widget; GroupedPanel *p=data; gtk_menu_popup_at_widget(GTK_MENU(grouped_menu(p)),p->view,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,NULL); return TRUE; }
static gboolean grouped_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 GroupedPanel *p=data; if(event->type!=GDK_BUTTON_PRESS || event->button!=3) return FALSE;
 grouped_flush(p); p->setting=TRUE; GtkTreePath *row=NULL;
 if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),(gint)event->x,(gint)event->y,&row,NULL,NULL,NULL)) { gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget),row,NULL,FALSE); gtk_tree_path_free(row); } else gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)));
 p->setting=FALSE; gtk_widget_grab_focus(widget); gtk_menu_popup_at_pointer(GTK_MENU(grouped_menu(p)),(GdkEvent *)event); return TRUE;
}
