/* SPDX-License-Identifier: GPL-2.0-or-later
 * Private implementation of the project membership browser. Included only by
 * insight-workbench.c: membership persistence stays in the workbench. */
typedef struct { gchar *path;gboolean directory; } MembershipEntry;
static void membership_entry_free(gpointer p) { MembershipEntry *e=p;g_free(e->path);g_free(e); }
typedef struct { GPtrArray *roots,*entries;InsightMasterTypes *types;gchar *pattern;guint64 serial;gboolean recursive,adding,known; } MembershipRead;
static void membership_read_free(gpointer p) { MembershipRead *r=p;g_ptr_array_unref(r->roots);g_ptr_array_unref(r->entries);insight_master_types_free(r->types);g_free(r->pattern);g_free(r); }
static gboolean membership_glob(const gchar *name,const gchar *patterns) {
 if(!patterns || !*patterns) return TRUE;gchar **parts=g_strsplit_set(patterns,";",-1);gboolean match=FALSE;
 for(guint n=0;parts[n] && !match;n++) { gchar *p=g_strstrip(parts[n]);if(!*p) continue;
#ifdef G_OS_WIN32
 gchar *folded=g_utf8_casefold(p,-1),*value=g_utf8_casefold(name,-1);match=g_pattern_match_simple(folded,value);g_free(folded);g_free(value);
#else
 match=g_pattern_match_simple(p,name);
#endif
 }g_strfreev(parts);return match;
}
static gboolean membership_known(const MembershipRead *r,const gchar *path,const gchar *name) {
 if(!r->known) return TRUE;const InsightMasterTypes *t=r->types;
 for(guint n=0;t->patterns && t->patterns[n];n++) if(membership_glob(name,t->patterns[n])) return TRUE;
 const gchar *dot=strrchr(name,'.');for(guint n=0;dot && t->extensions && t->extensions[n];n++) if(!g_ascii_strcasecmp(dot+1,t->extensions[n])) return TRUE;
 for(guint n=0;t->files && t->files[n];n++) if(!g_strcmp0(path,t->files[n])) return TRUE;return FALSE;
}
static void membership_read_worker(GTask *task,gpointer source,gpointer input,GCancellable *cancel) {
 (void)source;MembershipRead *r=input;GError *error=NULL;GPtrArray *queue=g_ptr_array_new_with_free_func(g_free);guint visited=0;
 for(guint n=0;n<r->roots->len;n++) g_ptr_array_add(queue,g_strdup(g_ptr_array_index(r->roots,n)));
 for(guint n=0;n<queue->len && !error;n++) {
  if(g_cancellable_set_error_if_cancelled(cancel,&error)) break;
  gchar *path=g_ptr_array_index(queue,n),*native=utils_get_locale_from_utf8(path);GFile *folder=g_file_new_for_path(native);g_free(native);gchar *root_pattern=NULL,*root_directory=NULL;
  if(r->adding && strpbrk(path,"*?")) {root_pattern=g_path_get_basename(path);root_directory=g_path_get_dirname(path);if(strpbrk(root_directory,"*?")) {g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"File list masks require a literal directory");g_free(root_pattern);g_free(root_directory);g_object_unref(folder);break;}g_object_unref(folder);native=utils_get_locale_from_utf8(root_directory);folder=g_file_new_for_path(native);g_free(native);path=root_directory;}
  GFileType type=g_file_query_file_type(folder,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel);
  if(type==G_FILE_TYPE_REGULAR && r->adding) { gchar *name=g_path_get_basename(path);if(membership_glob(name,r->pattern) && membership_known(r,path,name)) { MembershipEntry *e=g_new0(MembershipEntry,1);e->path=g_strdup(path);g_ptr_array_add(r->entries,e); }g_free(name);g_object_unref(folder);continue; }
  if(type!=G_FILE_TYPE_DIRECTORY) { g_set_error(&error,G_IO_ERROR,G_IO_ERROR_NOT_DIRECTORY,"Cannot read directory: %s",path);g_object_unref(folder);g_free(root_pattern);g_free(root_directory);break; }
  GFileEnumerator *it=g_file_enumerate_children(folder,"standard::name,standard::type",G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel,&error);GFileInfo *info;
  while(it && (info=g_file_enumerator_next_file(it,cancel,&error))) {
   if(++visited>100000 || r->entries->len>=10000 || queue->len>4096) { g_object_unref(info);g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Selection exceeds 10000 files, 4096 folders or 100000 directory entries; select a smaller tree");break; }
   gchar *name=utils_get_utf8_from_locale(g_file_info_get_name(info)),*child=g_build_filename(path,name,NULL);GFileType kind=g_file_info_get_file_type(info);
   if(kind==G_FILE_TYPE_DIRECTORY && r->recursive && !root_pattern && name[0]!='.' && strcmp(name,"node_modules") && strcmp(name,"__pycache__")) g_ptr_array_add(queue,g_strdup(child));
   if((kind==G_FILE_TYPE_DIRECTORY && !r->adding) || (kind==G_FILE_TYPE_REGULAR && membership_glob(name,root_pattern) && membership_glob(name,r->pattern) && membership_known(r,child,name))) { MembershipEntry *e=g_new0(MembershipEntry,1);e->path=g_strdup(child);e->directory=kind==G_FILE_TYPE_DIRECTORY;g_ptr_array_add(r->entries,e); }
   g_free(child);g_free(name);g_object_unref(info);
  }
  g_clear_object(&it);g_object_unref(folder);g_free(root_pattern);g_free(root_directory);
 }
 g_ptr_array_unref(queue);if(error) g_task_return_error(task,error);else g_task_return_boolean(task,TRUE);
}
static void membership_browser_refresh(MembershipDialog *d) {
 if(!d->available_store) return;gtk_list_store_clear(d->available_store);
 for(guint n=0;d->entries && n<d->entries->len;n++) { MembershipEntry *e=g_ptr_array_index(d->entries,n);if(!e->directory && insight_project_files_contains(d->candidate,d->root,e->path)) continue;
  gchar *name=g_path_get_basename(e->path);const gchar *query=gtk_entry_get_text(GTK_ENTRY(d->filename));if(!e->directory && *query && !strpbrk(query,"*?;") && !insight_lookup_text_match(name,query)) {g_free(name);continue;}GtkTreeIter it;gtk_list_store_append(d->available_store,&it);gtk_list_store_set(d->available_store,&it,0,name,1,e->path,2,e->directory,3,e->directory ? "folder":"text-x-generic",-1);g_free(name);
 }
}
static void membership_read_done(GObject *source,GAsyncResult *result,gpointer unused) {
 (void)unused;MembershipDialog *d=g_object_get_data(source,"membership-browser");GTask *task=G_TASK(result);MembershipRead *r=g_task_get_task_data(task);GError *error=NULL;gboolean ok=g_task_propagate_boolean(task,&error);
 if(d && r->serial==d->read_serial) { g_clear_object(&d->read_cancel);gtk_widget_set_sensitive(d->browser_controls,TRUE);gtk_dialog_set_response_sensitive(GTK_DIALOG(d->dialog),GTK_RESPONSE_APPLY,d->master_valid);
  if(!ok) gtk_label_set_text(GTK_LABEL(d->browser_status),error ? insight_tr(error->message):"");
  else if(r->adding) { InsightProjectFiles *next=insight_project_files_clone(d->candidate);for(guint n=0;n<r->entries->len && !error;n++) { MembershipEntry *e=g_ptr_array_index(r->entries,n);insight_project_files_set(next,e->path,FALSE,TRUE,&error); }
   if(error) { insight_project_files_free(next);gtk_label_set_text(GTK_LABEL(d->browser_status),error->message); }
   else { insight_project_files_free(d->candidate);d->candidate=next;for(guint n=0;n<r->entries->len;n++) { MembershipEntry *e=g_ptr_array_index(r->entries,n);if(!g_ptr_array_find_with_equal_func(d->paths,e->path,(GEqualFunc)g_str_equal,NULL)) g_ptr_array_add(d->paths,g_strdup(e->path)); }membership_dialog_refresh(d);gchar *count=g_strdup_printf(insight_tr("Added %u files to the selection"),r->entries->len);gtk_label_set_text(GTK_LABEL(d->browser_status),count);g_free(count); }
  } else { g_clear_pointer(&d->entries,g_ptr_array_unref);d->entries=g_ptr_array_ref(r->entries);
   for(guint n=0;n<d->entries->len;n++) { MembershipEntry *e=g_ptr_array_index(d->entries,n);if(e->directory) {gchar *name=g_path_get_basename(e->path);GtkTreeIter it;gtk_tree_store_append(d->directory_store,&it,&d->directory_node);gtk_tree_store_set(d->directory_store,&it,0,name,1,e->path,2,TRUE,3,"folder",-1);g_free(name);} }
   gtk_tree_view_expand_all(GTK_TREE_VIEW(d->directory_view));membership_browser_refresh(d);gtk_label_set_text(GTK_LABEL(d->browser_status),""); }
 }
 g_clear_error(&error);
}
static void membership_read_start(MembershipDialog *d,GPtrArray *roots,gboolean adding,gboolean recursive) {
 if(d->read_cancel) {g_cancellable_cancel(d->read_cancel);g_clear_object(&d->read_cancel);}d->read_cancel=g_cancellable_new();
 MembershipRead *r=g_new0(MembershipRead,1);r->serial=++d->read_serial;r->roots=roots;r->entries=g_ptr_array_new_with_free_func(membership_entry_free);r->types=insight_languages_master_types();r->adding=adding;r->recursive=recursive;r->known=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->known));
 const gchar *filter=gtk_entry_get_text(GTK_ENTRY(d->filename));r->pattern=g_strdup(strpbrk(filter,"*?;") ? filter : "*");
 gtk_widget_set_sensitive(d->browser_controls,FALSE);gtk_dialog_set_response_sensitive(GTK_DIALOG(d->dialog),GTK_RESPONSE_APPLY,FALSE);gtk_label_set_text(GTK_LABEL(d->browser_status),insight_tr("Reading project selection…"));
 GTask *task=g_task_new(d->dialog,d->read_cancel,membership_read_done,NULL);g_task_set_task_data(task,r,membership_read_free);g_task_run_in_thread(task,membership_read_worker);g_object_unref(task);
}
static void membership_browse(MembershipDialog *d,const gchar *path) {
 gchar *next=insight_canonicalize_filename(path,d->directory ? d->directory:d->root);g_free(d->directory);d->directory=next;gtk_label_set_text(GTK_LABEL(d->directory_label),d->directory);
 g_clear_pointer(&d->entries,g_ptr_array_unref);gtk_list_store_clear(d->available_store);
 /* Ancestor chain plus the selected folder; children are loaded with the file
  * list. This works for roots and paths outside the project on every platform. */
 gtk_tree_store_clear(d->directory_store);GPtrArray *parents=g_ptr_array_new_with_free_func(g_free);gchar *p=g_strdup(next);
 for(guint n=0;n<128;n++) { g_ptr_array_add(parents,p);gchar *up=g_path_get_dirname(p);if(!strcmp(p,up)) {g_free(up);break;}p=up; }
 for(guint n=parents->len;n>0;n--) { const gchar *path=g_ptr_array_index(parents,n-1);gchar *name=g_path_get_basename(path);GtkTreeIter it;gtk_tree_store_append(d->directory_store,&it,n==parents->len ? NULL:&d->directory_node);gtk_tree_store_set(d->directory_store,&it,0,name,1,path,2,TRUE,3,"folder",-1);d->directory_node=it;g_free(name); }g_ptr_array_unref(parents);gtk_tree_view_expand_all(GTK_TREE_VIEW(d->directory_view));
 GPtrArray *roots=g_ptr_array_new_with_free_func(g_free);g_ptr_array_add(roots,g_strdup(next));membership_read_start(d,roots,FALSE,FALSE);
}
static void membership_browser_activate(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column;MembershipDialog *d=data;GtkTreeModel *model=gtk_tree_view_get_model(view);GtkTreeIter it;gchar *name=NULL;gboolean directory=FALSE;if(!gtk_tree_model_get_iter(model,&it,path)) return;gtk_tree_model_get(model,&it,1,&name,2,&directory,-1);
 if(directory) membership_browse(d,name);else { GError *error=NULL;if(insight_project_files_set(d->candidate,name,FALSE,TRUE,&error)) {if(!g_ptr_array_find_with_equal_func(d->paths,name,(GEqualFunc)g_str_equal,NULL)) g_ptr_array_add(d->paths,g_strdup(name));membership_dialog_refresh(d);}if(error) gtk_label_set_text(GTK_LABEL(d->browser_status),error->message);g_clear_error(&error); }g_free(name);
}
static void membership_filename_activate(GtkEntry *entry,gpointer data) {
 MembershipDialog *d=data;const gchar *text=gtk_entry_get_text(entry);gchar *path=insight_canonicalize_filename(text,d->directory),*native=utils_get_locale_from_utf8(path);
 if(*text && g_file_test(native,G_FILE_TEST_IS_DIR)) {gtk_entry_set_text(entry,"");membership_browse(d,path);}else membership_browse(d,d->directory);g_free(native);g_free(path);
}
static void membership_known_changed(GtkToggleButton *button,gpointer data) { (void)button;MembershipDialog *d=data;if(d->directory) membership_browse(d,d->directory); }
static void membership_browser_add(GtkButton *button,gpointer data) {
 MembershipDialog *d=data;gint mode=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),"browser-add"));GPtrArray *roots=g_ptr_array_new_with_free_func(g_free);GtkTreeModel *model=GTK_TREE_MODEL(d->available_store);GList *rows=gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(d->available)),NULL);gboolean directories=FALSE;
 if(mode==1) {g_list_free_full(rows,(GDestroyNotify)gtk_tree_path_free);rows=NULL;GtkTreeIter it;for(gboolean valid=gtk_tree_model_get_iter_first(model,&it);valid;valid=gtk_tree_model_iter_next(model,&it)) rows=g_list_append(rows,gtk_tree_model_get_path(model,&it));}
 for(GList *p=rows;p;p=p->next) {GtkTreeIter it;gchar *path=NULL;gboolean directory=FALSE;if(!gtk_tree_model_get_iter(model,&it,p->data)) continue;gtk_tree_model_get(model,&it,1,&path,2,&directory,-1);if(!mode && directory) {membership_browse(d,path);g_free(path);g_list_free_full(rows,(GDestroyNotify)gtk_tree_path_free);g_ptr_array_unref(roots);return;}if(directory) directories=TRUE;if(mode!=2 || directory) g_ptr_array_add(roots,path);else g_free(path);}
 g_list_free_full(rows,(GDestroyNotify)gtk_tree_path_free);
 if(mode==2 && !roots->len) g_ptr_array_add(roots,g_strdup(d->directory));
 if(!mode && !roots->len) {const gchar *text=gtk_entry_get_text(GTK_ENTRY(d->filename));if(*text && !strpbrk(text,"*?;")) g_ptr_array_add(roots,insight_canonicalize_filename(text,d->directory));}
 gboolean recursive=mode==2;
 if(mode==1 && directories) { GtkWidget *ask=gtk_message_dialog_new(GTK_WINDOW(d->dialog),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,GTK_MESSAGE_QUESTION,GTK_BUTTONS_NONE,"%s",insight_tr("Include subdirectories?"));gtk_dialog_add_buttons(GTK_DIALOG(ask),insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Files only"),GTK_RESPONSE_NO,insight_tr("Include subdirectories"),GTK_RESPONSE_YES,NULL);gint response=gtk_dialog_run(GTK_DIALOG(ask));gtk_widget_destroy(ask);if(response==GTK_RESPONSE_CANCEL || response==GTK_RESPONSE_DELETE_EVENT) {g_ptr_array_unref(roots);return;}recursive=response==GTK_RESPONSE_YES;if(!recursive) for(guint n=roots->len;n>0;n--) {gchar *native=utils_get_locale_from_utf8(g_ptr_array_index(roots,n-1));if(g_file_test(native,G_FILE_TEST_IS_DIR)) g_ptr_array_remove_index(roots,n-1);g_free(native);} }
 if(roots->len) membership_read_start(d,roots,TRUE,recursive);else g_ptr_array_unref(roots);
}
static void membership_remove_special(GtkButton *button,gpointer data) {
 (void)button;MembershipDialog *d=data;GtkWidget *dialog=gtk_dialog_new_with_buttons(insight_tr("Remove Special"),GTK_WINDOW(d->dialog),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Remove from project"),GTK_RESPONSE_ACCEPT,NULL),*entry=gtk_entry_new();gtk_entry_set_text(GTK_ENTRY(entry),"*.h");gtk_widget_set_tooltip_text(entry,insight_tr("Filename masks separated by semicolons; files remain on disk"));gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),entry,FALSE,FALSE,8);gtk_widget_show_all(dialog);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {const gchar *patterns=gtk_entry_get_text(GTK_ENTRY(entry));for(guint n=0;n<d->paths->len;n++) {const gchar *path=g_ptr_array_index(d->paths,n);gchar *base=g_path_get_basename(path);if(*patterns && membership_glob(base,patterns)) insight_project_files_set(d->candidate,path,FALSE,FALSE,NULL);g_free(base);}membership_dialog_refresh(d);}gtk_widget_destroy(dialog);
}
static GtkWidget *membership_list(GtkListStore **store,GtkWidget **view,const gchar *name,const gchar *title) {
 *store=gtk_list_store_new(4,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN,G_TYPE_STRING);*view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(*store));gtk_widget_set_name(*view,name);GtkTreeViewColumn *column=gtk_tree_view_column_new();gtk_tree_view_column_set_title(column,title);GtkCellRenderer *icon=gtk_cell_renderer_pixbuf_new(),*text=gtk_cell_renderer_text_new();gtk_tree_view_column_pack_start(column,icon,FALSE);gtk_tree_view_column_add_attribute(column,icon,"icon-name",3);gtk_tree_view_column_pack_start(column,text,TRUE);gtk_tree_view_column_add_attribute(column,text,"text",0);gtk_tree_view_append_column(GTK_TREE_VIEW(*view),column);gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(*store),0,GTK_SORT_ASCENDING);gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(*view)),GTK_SELECTION_MULTIPLE);gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(*view),1);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);gtk_container_add(GTK_CONTAINER(scroll),*view);gtk_widget_set_hexpand(scroll,TRUE);gtk_widget_set_vexpand(scroll,TRUE);return scroll;
}

static void membership_filename_changed(GtkEditable *entry,gpointer data) { (void)entry;membership_browser_refresh(data); }
static void membership_filetypes(GtkButton *button,gpointer data) { (void)button;MembershipDialog *d=data;insight_languages_show(NULL,NULL);membership_browse(d,d->directory); }

static void membership_close(GtkButton *button,gpointer data) { (void)button;MembershipDialog *d=data;if(!d->read_cancel && d->master_valid) gtk_dialog_response(GTK_DIALOG(d->dialog),GTK_RESPONSE_APPLY); }

static void membership_browser_import(GtkButton *button,gpointer data) {
 (void)button;MembershipDialog *d=data;GtkWidget *chooser=gtk_file_chooser_dialog_new(insight_tr("Add from file list"),GTK_WINDOW(d->dialog),GTK_FILE_CHOOSER_ACTION_OPEN,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Add"),GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(chooser))==GTK_RESPONSE_ACCEPT) {gchar *native=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser)),*path=utils_get_utf8_from_locale(native),*text=read_disk_source(path,TRUE);g_free(native);g_free(path);
  if(!text) gtk_label_set_text(GTK_LABEL(d->browser_status),insight_tr("File list must be decodable text, at most 2 MiB"));
  else {GPtrArray *roots=g_ptr_array_new_with_free_func(g_free);gchar **lines=g_strsplit(g_str_has_prefix(text,"\xEF\xBB\xBF") ? text+3:text,"\n",-1);gboolean valid=TRUE;
   for(guint n=0;lines[n];n++) {gsize size=strlen(lines[n]);if(size && lines[n][size-1]=='\r') lines[n][--size]=0;if(!size || lines[n][0]==';') continue;if(roots->len>=10000 || size>16384) {valid=FALSE;break;}g_ptr_array_add(roots,insight_canonicalize_filename(lines[n],d->root));}
   if(valid && roots->len) membership_read_start(d,roots,TRUE,TRUE);else {if(!valid) gtk_label_set_text(GTK_LABEL(d->browser_status),insight_tr("File list exceeds 10000 entries or contains an overlong path"));g_ptr_array_unref(roots);}g_strfreev(lines);g_free(text);
  }
 }gtk_widget_destroy(chooser);
}

static GtkWidget *membership_directory_list(MembershipDialog *d) {
 d->directory_store=gtk_tree_store_new(4,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN,G_TYPE_STRING);d->directory_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(d->directory_store));gtk_widget_set_name(d->directory_view,"insight-project-membership-directories");GtkTreeViewColumn *column=gtk_tree_view_column_new();gtk_tree_view_column_set_title(column,insight_tr("Directories"));GtkCellRenderer *icon=gtk_cell_renderer_pixbuf_new(),*text=gtk_cell_renderer_text_new();gtk_tree_view_column_pack_start(column,icon,FALSE);gtk_tree_view_column_add_attribute(column,icon,"icon-name",3);gtk_tree_view_column_pack_start(column,text,TRUE);gtk_tree_view_column_add_attribute(column,text,"text",0);gtk_tree_view_append_column(GTK_TREE_VIEW(d->directory_view),column);gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(d->directory_view),1);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);gtk_container_add(GTK_CONTAINER(scroll),d->directory_view);gtk_widget_set_hexpand(scroll,TRUE);gtk_widget_set_vexpand(scroll,TRUE);return scroll;
}
