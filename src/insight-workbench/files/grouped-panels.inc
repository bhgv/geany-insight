/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Grouped file panels and leaf navigation.
 * RU: Панели групп файлов и навигация по элементам. */

static gboolean grouped_first(GtkTreeModel *model,GtkTreePath *row,GtkTreeIter *iter,gpointer data) {
 GroupedVisit *v=data; gchar *name=NULL,*path=NULL; gulong line=0; gboolean leaf=grouped_row(model,iter,v->panel,&name,&path,&line); g_free(name); g_free(path); if(leaf) v->found=gtk_tree_path_copy(row); return leaf;
}
static gboolean grouped_key_press(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 GroupedPanel *p=data; guint key=gdk_keyval_to_lower(event->keyval),mods=event->state&gtk_accelerator_get_default_mod_mask();
 if(widget==p->entry) {
  if(key==GDK_KEY_Escape && !mods) { if(*gtk_entry_get_text(GTK_ENTRY(widget))) gtk_entry_set_text(GTK_ENTRY(widget),""); else editor_focus(); return TRUE; }
  if((key==GDK_KEY_Down || key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) && !mods) {
   grouped_flush(p); if(gtk_style_context_has_class(gtk_widget_get_style_context(p->entry),"error")) return TRUE;
   GroupedVisit v={.panel=p}; gtk_tree_model_foreach(GTK_TREE_MODEL(p->store),grouped_first,&v);
   if(v.found) { gtk_tree_view_expand_to_path(GTK_TREE_VIEW(p->view),v.found); gtk_tree_view_set_cursor(GTK_TREE_VIEW(p->view),v.found,NULL,FALSE); gtk_tree_path_free(v.found); gtk_widget_grab_focus(p->view); if(key!=GDK_KEY_Down) grouped_action(p,GP_OPEN); } return TRUE;
  } return FALSE;
 }
 if((key==GDK_KEY_Menu && !mods) || (key==GDK_KEY_F10 && mods==GDK_SHIFT_MASK)) return grouped_popup(widget,p);
 if(key==GDK_KEY_f && mods==GEANY_PRIMARY_MOD_MASK) { gtk_widget_grab_focus(p->entry); return TRUE; }
 if(key==GDK_KEY_c && (mods==GEANY_PRIMARY_MOD_MASK || mods==(GEANY_PRIMARY_MOD_MASK|GDK_SHIFT_MASK))) { grouped_action(p,mods&GDK_SHIFT_MASK ? GP_LOCATION : GP_NAME); return TRUE; }
 if((key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) && mods==GEANY_PRIMARY_MOD_MASK) { grouped_action(p,GP_OPEN); return TRUE; }
 if((key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) && mods==GDK_MOD1_MASK && p->symbols) { grouped_action(p,GP_INFO); return TRUE; }
 if(key==GDK_KEY_Escape && !mods) { editor_focus(); return TRUE; } return FALSE;
}
static void grouped_mapped(GtkWidget *widget,gpointer data) { (void)widget; grouped_refresh(data); }
static void grouped_more(GtkButton *button,gpointer data) { (void)button; grouped_action(data,GP_MORE); }
static void grouped_attach(GroupedPanel *p,GtkWidget *box) {
 gtk_entry_set_max_length(GTK_ENTRY(p->entry),1024); gtk_widget_set_name(p->entry,p->symbols ? "insight-category-query" : "insight-type-query"); gtk_widget_set_name(p->view,p->symbols ? "insight-symbol-categories" : "insight-file-types");
 g_object_set_data(G_OBJECT(p->view),"insight-grouped-panel",p); g_object_set_data(G_OBJECT(p->entry),"insight-grouped-panel",p); p->limit=p->symbols ? 5000 : 10000;
 p->status=gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(p->status),0); gtk_widget_set_name(p->status,p->symbols ? "insight-category-count" : "insight-type-count"); gtk_box_pack_start(GTK_BOX(box),p->status,FALSE,FALSE,0); grouped_count(p,0,0);
 if(p->symbols) { p->more=gtk_button_new_with_label("Load more category symbols"); gtk_widget_set_name(p->more,"insight-category-more"); gtk_widget_set_no_show_all(p->more,TRUE); gtk_box_pack_start(GTK_BOX(box),p->more,FALSE,FALSE,0); g_signal_connect(p->more,"clicked",G_CALLBACK(grouped_more),p); }
 g_signal_connect(p->entry,"changed",G_CALLBACK(grouped_changed),p); g_signal_connect(p->entry,"key-press-event",G_CALLBACK(grouped_key_press),p); g_signal_connect(p->view,"key-press-event",G_CALLBACK(grouped_key_press),p); g_signal_connect(p->view,"popup-menu",G_CALLBACK(grouped_popup),p); g_signal_connect(p->view,"button-press-event",G_CALLBACK(grouped_button),p); g_signal_connect(p->view,"map",G_CALLBACK(grouped_mapped),p); g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)),"changed",G_CALLBACK(grouped_selected),p);
}
static void grouped_write(GroupedPanel *p,GKeyFile *settings) {
 insight_symbol_view_columns_write(GTK_TREE_VIEW(p->view),settings,grouped_prefix(p)); gint column; GtkSortType order;
 if(gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(p->store),&column,&order)) { gchar *key=g_strconcat(grouped_prefix(p),"-sort-column",NULL); g_key_file_set_integer(settings,"workbench",key,column); g_free(key); key=g_strconcat(grouped_prefix(p),"-sort-descending",NULL); g_key_file_set_boolean(settings,"workbench",key,order==GTK_SORT_DESCENDING); g_free(key); }
}
static void grouped_read(GroupedPanel *p,GKeyFile *settings) {
 insight_symbol_view_columns_read(GTK_TREE_VIEW(p->view),settings,grouped_prefix(p)); gchar *key=g_strconcat(grouped_prefix(p),"-sort-column",NULL); GError *error=NULL; gint column=g_key_file_get_integer(settings,"workbench",key,&error); g_free(key);
 if(!error && column>=0 && column<(p->symbols ? SYMBOL_COLUMNS : 2)) { key=g_strconcat(grouped_prefix(p),"-sort-descending",NULL); gboolean reverse=g_key_file_get_boolean(settings,"workbench",key,NULL); g_free(key); gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(p->store),column,reverse ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING); } g_clear_error(&error);
}
static InsightLookupFilter *grouped_filter(GroupedPanel *p) {
 InsightLookupFilter *filter=entry_filter(p->entry,!p->symbols);
 if(filter) gtk_style_context_remove_class(gtk_widget_get_style_context(p->entry),"error"); else gtk_style_context_add_class(gtk_widget_get_style_context(p->entry),"error");
 return filter;
}
static void file_types_refresh(void) {
 GroupedPanel *p=&wb.type_panel; if(!p->store || !gtk_widget_get_mapped(p->view)) return;
 InsightLookupFilter *filter=grouped_filter(p); if(!filter) return;
 GHashTable *expanded=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL); gchar *selected=grouped_capture(p,expanded);
 gtk_tree_store_clear(p->store); GHashTable *groups=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free); guint shown=0;
 for(guint i=0;i<wb.files->len && i<10000;i++) {
  const char *path=g_ptr_array_index(wb.files,i); if(!insight_lookup_filter_match(filter,NULL,NULL,path,NULL)) continue;
  GeanyDocument *doc=document_find_by_filename(path); GeanyFiletype *ft=doc ? doc->file_type : insight_languages_detect(path); if(!ft) ft=filetypes_detect_from_extension(path);
  gchar *name=insight_languages_profile_name(doc ? doc->editor->sci : NULL,path,ft); if(!*name) { g_free(name); name=g_strdup("Other"); }
  GtkTreeIter *group=g_hash_table_lookup(groups,name);
  if(!group) { group=g_new(GtkTreeIter,1); gtk_tree_store_append(p->store,group,NULL); gtk_tree_store_set(p->store,group,0,name,2,TRUE,-1); g_hash_table_insert(groups,g_strdup(name),group); }
  gchar *base=g_path_get_basename(path); GtkTreeIter row; gtk_tree_store_append(p->store,&row,group); gtk_tree_store_set(p->store,&row,0,base,1,path,2,FALSE,-1); shown++; g_free(base); g_free(name);
 }
 g_hash_table_destroy(groups); insight_lookup_filter_free(filter); grouped_count(p,shown,wb.files->len); grouped_restore(p,expanded,selected);
}
static void create_file_types(void) {
 GroupedPanel *p=&wb.type_panel; GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3); p->entry=gtk_search_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(p->entry),"Filter files by path…"); gtk_widget_set_tooltip_text(p->entry,"Use path fragments, glob, ?regex or ~fuzzy; empty groups are hidden"); gtk_box_pack_start(GTK_BOX(box),p->entry,FALSE,FALSE,0);
 wb.file_types=p->store=gtk_tree_store_new(3,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN); wb.types_view=p->view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(p->store));
 const gchar *labels[]={"File / type","Path"}; for(guint i=0;i<G_N_ELEMENTS(labels);i++) { GtkTreeViewColumn *c=gtk_tree_view_column_new_with_attributes(labels[i],gtk_cell_renderer_text_new(),"text",i,NULL); gtk_tree_view_column_set_sort_column_id(c,i); gtk_tree_view_append_column(GTK_TREE_VIEW(p->view),c); }
 const gint widths[]={220,300}; insight_symbol_view_columns_configure(GTK_TREE_VIEW(p->view),widths,G_N_ELEMENTS(widths)); gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(p->view),1); gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(p->store),0,GTK_SORT_ASCENDING);
 grouped_attach(p,box); GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),p->view); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0); gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),box,gtk_label_new("File Types"));
 g_signal_connect(p->view,"row-activated",G_CALLBACK(tree_activated),NULL); gtk_widget_show_all(box); file_types_refresh();
}
static InsightNotes *notes_for(InsightNoteKind kind) { if(kind==INSIGHT_BOOKMARK && wb.marks_project) return wb.project_marks;return kind==INSIGHT_CLIP && wb.clips_project ? wb.project_notes : wb.notes; }
static guint64 notes_generation(InsightNoteKind kind) { return kind==INSIGHT_BOOKMARK ? wb.marks_generation : wb.clips_generation; }
static gboolean bookmarks_writable(void) {
 if(!wb.marks_stale && (!wb.marks_project || wb.marks_valid)) return TRUE;
 ui_set_statusbar(TRUE,"Project bookmarks unavailable; repair or reload the store");return FALSE;
}
static gboolean notes_scope_current(InsightNoteKind kind,guint64 generation) {
 if(kind==INSIGHT_BOOKMARK) { if(generation==wb.marks_generation) return TRUE;ui_set_statusbar(TRUE,"Bookmark store changed; try the operation again");return FALSE; }
 if(kind!=INSIGHT_CLIP || (generation==wb.clips_generation && (!wb.clips_project || (wb.project_notes_valid && wb.project_notes_path)))) return TRUE;
 ui_set_statusbar(TRUE,"Template store changed; try the operation again");return FALSE;
}
typedef struct { gchar *root,*etag; } BookmarkSessionStore;
static void bookmark_session_store_free(gpointer data) { BookmarkSessionStore *store=data;g_free(store->root);g_free(store->etag);g_free(store); }
/* Capture before loading, or use the digest/etag token from our atomic write. */
static void bookmark_session_remember(const gchar *path,const gchar *root,const gchar *etag) {
 if(!wb.marks_session_stores || !path) return;
 gchar *captured=etag ? NULL : insight_notes_file_version(path,NULL);if(!etag) etag=captured;
 if(etag) { BookmarkSessionStore *store=g_new0(BookmarkSessionStore,1);store->root=g_strdup(root);store->etag=g_strdup(etag);g_hash_table_replace(wb.marks_session_stores,g_strdup(path),store); }
 g_free(captured);
}
static void bookmark_session_finish(void) {
 if(wb.marks_preserve || !wb.marks_session_stores) return;
 GHashTableIter iter;gpointer path,value;g_hash_table_iter_init(&iter,wb.marks_session_stores);
 while(g_hash_table_iter_next(&iter,&path,&value)) {
  BookmarkSessionStore *store=value;GError *error=NULL;
  InsightNotes *notes=insight_notes_load_portable(path,store->root,&error),*clips=notes ? insight_notes_select(notes,INSIGHT_CLIP,&error) : NULL;
  if(clips && clips->items->len!=notes->items->len) insight_notes_save_portable_checked(clips,path,store->root,store->etag,NULL,&error);
  if(error) { g_warning("Bookmarks retained in %s at exit: %s",(gchar *)path,error->message);g_clear_error(&error); }
  insight_notes_free(clips);insight_notes_free(notes);
 }
}
static gboolean notes_save_kind(InsightNoteKind kind) {
 gboolean marks=kind==INSIGHT_BOOKMARK && wb.marks_project;gboolean project=kind==INSIGHT_CLIP && wb.clips_project;const gchar *path=marks ? wb.project_marks_path : project ? wb.project_notes_path : wb.notes_path;
 if(kind==INSIGHT_BOOKMARK && !bookmarks_writable()) return FALSE;
 if(!path || (project && !wb.project_notes_valid)) { ui_set_statusbar(TRUE,"Open a project with a valid template store first");return FALSE; }
 gchar *directory=g_path_get_dirname(path); GError *error=NULL; gboolean saved=FALSE;gchar *etag=NULL;
 if(g_mkdir_with_parents(directory,0700)) ui_set_statusbar(TRUE,"Notes directory could not be created");
 else { saved=insight_notes_save_portable_checked(notes_for(kind),path,marks ? wb.project_marks_root : NULL,NULL,&etag,&error);if(saved && !project) bookmark_session_remember(path,marks ? wb.project_marks_root : NULL,etag); if(!saved) ui_set_statusbar(TRUE,"Notes not saved: %s",error ? error->message : "write failed"); }
 g_clear_error(&error);g_free(etag);g_free(directory);if(kind==INSIGHT_BOOKMARK) wb.marks_save_failed=!saved;return saved;
}
static gboolean notes_save(void) { return notes_save_kind(INSIGHT_BOOKMARK); }
static InsightNotes *notes_clone(InsightNoteKind kind,GError **error) {
 InsightNotes *source=notes_for(kind),*out=insight_notes_new();
 for(guint i=0;i<source->items->len;i++) { InsightNote *note=g_ptr_array_index(source->items,i);if(!insight_notes_set(out,note->kind,note->name,note->path,note->line,note->text,error) || (note->kind==INSIGHT_CLIP && !insight_notes_set_languages(out,note->name,(const gchar *const *)note->languages,error))) { insight_notes_free(out);return NULL; }if(note->anchor && !insight_notes_set_anchor(out,note->name,note->anchor,error)) { insight_notes_free(out);return NULL; } }
 return out;
}
static gboolean notes_commit(InsightNotes *,InsightNoteKind);
typedef struct { guint doc;gint handle; } BookmarkHandle;
static gchar *selected_note(InsightNoteKind);
static GPtrArray *selected_notes(InsightNoteKind);
static void note_selected(GtkTreeSelection *,gpointer);
static gchar *clip_current_language(void) { GeanyDocument *doc=document_get_current(); return doc ? insight_languages_profile_name(doc->editor->sci,doc->file_name,doc->file_type) : g_strdup(""); }
static void notes_filter(GtkEditable *,gpointer);
static void bookmark_list_reload(GtkWidget *,gpointer);
static gchar *bookmark_line_text(ScintillaObject *sci,gint line) {
 gchar *text=sci_get_line(sci,line);gsize n=strlen(text);
 while(n && (text[n-1]=='\r' || text[n-1]=='\n')) text[--n]=0;
 return text;
}
static gboolean bookmark_tags_ready(GeanyDocument *doc) {
 return doc->priv && (!doc->file_type || doc->file_type->lang==TM_PARSER_NONE || doc->priv->tag_snapshot_generation==doc->priv->tag_parse_generation);
}
static void bookmark_prepare_tags(GeanyDocument *doc) {
 if(!doc || !doc->is_valid || sci_get_length(doc->editor->sci)>2*1024*1024 || bookmark_tags_ready(doc)) return;
 if(doc->priv->tag_list_update_source) { g_source_remove(doc->priv->tag_list_update_source);doc->priv->tag_list_update_source=0; }
 document_update_tags(doc);
}
static GArray *bookmark_symbols(GeanyDocument *doc) {
 if(!bookmark_tags_ready(doc)) return NULL;
 if(doc->tm_file && doc->tm_file->tags_array && doc->tm_file->tags_array->len>250000) return NULL;
 GArray *symbols=g_array_new(FALSE,FALSE,sizeof(InsightBookmarkSymbol));
 if(doc->tm_file && doc->tm_file->tags_array && doc->tm_file->tags_array->len<=250000) for(guint i=0;i<doc->tm_file->tags_array->len;i++) {
  TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,i);guint kind=tag->type&(tm_tag_function_t|tm_tag_method_t) ? INSIGHT_BOOKMARK_FUNCTION : tag->type&(tm_tag_class_t|tm_tag_struct_t|tm_tag_interface_t) ? INSIGHT_BOOKMARK_CLASS : 0;
  if(kind && tag->name && tag->line) { InsightBookmarkSymbol symbol={tag->name,tag->scope ? tag->scope : "",kind,tag->line};g_array_append_val(symbols,symbol); }
 }
 return symbols;
}
static gboolean bookmark_capture_symbol(const gchar *name,GArray *symbols) {
 if(!symbols) return FALSE;
 const InsightNote *note=insight_notes_get(notes_for(INSIGHT_BOOKMARK),INSIGHT_BOOKMARK,name);if(!note) return FALSE;
 InsightBookmarkAnchor previous={note->anchor ? g_strdup(note->anchor->name) : NULL,note->anchor ? g_strdup(note->anchor->scope) : NULL,note->anchor ? note->anchor->kind : 0,note->anchor ? note->anchor->offset : 0};
 insight_notes_capture_anchor(notes_for(INSIGHT_BOOKMARK),name,(const InsightBookmarkSymbol *)symbols->data,symbols->len,NULL);note=insight_notes_get(notes_for(INSIGHT_BOOKMARK),INSIGHT_BOOKMARK,name);
 gboolean changed=g_strcmp0(previous.name,note->anchor ? note->anchor->name : NULL) || g_strcmp0(previous.scope,note->anchor ? note->anchor->scope : NULL) || previous.kind!=(note->anchor ? note->anchor->kind : 0) || previous.offset!=(note->anchor ? note->anchor->offset : 0);
 g_free(previous.name);g_free(previous.scope);return changed;
}
static void bookmark_restore_document(GeanyDocument *doc) {
 if(wb.marks_stale || (wb.marks_project && !wb.marks_valid)) return;
 if(!doc || !doc->is_valid || !doc->file_name || !wb.notes || !wb.bookmark_handles || sci_get_length(doc->editor->sci)>2*1024*1024) return;
 gchar *text=NULL;GArray *symbols=bookmark_symbols(doc);
 for(guint i=0;i<notes_for(INSIGHT_BOOKMARK)->items->len;i++) {
  InsightNote *note=g_ptr_array_index(notes_for(INSIGHT_BOOKMARK)->items,i);
  if(note->kind!=INSIGHT_BOOKMARK || g_strcmp0(note->path,doc->file_name)) continue;
  BookmarkHandle *handle=g_hash_table_lookup(wb.bookmark_handles,note->name);
  if(handle && handle->doc==doc->id) continue;
  if(!text) text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1);
  guint64 line=0;gboolean ambiguous=FALSE;
  if((note->anchor && !symbols) || !insight_bookmark_resolve_symbols(note,text,strlen(text),symbols ? (const InsightBookmarkSymbol *)symbols->data : NULL,symbols ? symbols->len : 0,&line,&ambiguous) || !line || line>(guint64)sci_get_line_count(doc->editor->sci)) continue;
  handle=g_new0(BookmarkHandle,1);handle->doc=doc->id;handle->handle=(gint)SSM(doc->editor->sci,SCI_MARKERADD,(uptr_t)(line-1),1);
  g_hash_table_replace(wb.bookmark_handles,g_strdup(note->name),handle);
 }
 g_free(text);if(symbols) g_array_unref(symbols);
}
static void bookmark_clear_handles(void) {
 GHashTableIter it;gpointer value;g_hash_table_iter_init(&it,wb.bookmark_handles);
 while(g_hash_table_iter_next(&it,NULL,&value)) { BookmarkHandle *handle=value;GeanyDocument *doc=document_find_by_id(handle->doc);if(doc && doc->is_valid) SSM(doc->editor->sci,SCI_MARKERDELETEHANDLE,handle->handle,0); }
 g_hash_table_remove_all(wb.bookmark_handles);
}
static gboolean bookmark_capture_document(GeanyDocument *doc) {
 if(!doc || !doc->is_valid || !doc->file_name) return FALSE;
 GArray *symbols=bookmark_symbols(doc);gboolean changed=FALSE;GHashTable *covered=g_hash_table_new(g_direct_hash,g_direct_equal);
 for(gint i=(gint)notes_for(INSIGHT_BOOKMARK)->items->len-1;i>=0;i--) {
  InsightNote *note=g_ptr_array_index(notes_for(INSIGHT_BOOKMARK)->items,(guint)i);
  if(note->kind!=INSIGHT_BOOKMARK || g_strcmp0(note->path,doc->file_name)) continue;
  BookmarkHandle *handle=g_hash_table_lookup(wb.bookmark_handles,note->name);
  if(!handle || handle->doc!=doc->id) continue;
  gint line=(gint)SSM(doc->editor->sci,SCI_MARKERLINEFROMHANDLE,handle->handle,0);
  if(line<0) {
   gchar *name=g_strdup(note->name);insight_notes_remove(notes_for(INSIGHT_BOOKMARK),INSIGHT_BOOKMARK,name);g_hash_table_remove(wb.bookmark_handles,name);g_free(name);changed=TRUE;continue;
  }
  g_hash_table_add(covered,GINT_TO_POINTER(line+1));
  gchar *text=bookmark_line_text(doc->editor->sci,line);
  if(strlen(text)<=65536 && (note->line!=(guint64)line+1 || g_strcmp0(note->text,text))) {
   note->line=(guint64)line+1;g_free(note->text);note->text=g_strdup(text);changed=TRUE;
  }
  g_free(text);changed=bookmark_capture_symbol(note->name,symbols) || changed;
 }
 for(gint line=(gint)SSM(doc->editor->sci,SCI_MARKERNEXT,0,2);line>=0;line=(gint)SSM(doc->editor->sci,SCI_MARKERNEXT,line+1,2)) {
  if(g_hash_table_contains(covered,GINT_TO_POINTER(line+1))) continue;
  gchar *text=bookmark_line_text(doc->editor->sci,line),*name=g_strdup_printf("LineBookmark-%u-%d-%"G_GINT64_FORMAT,doc->id,line,g_get_real_time());GError *error=NULL;
  if(insight_notes_set(notes_for(INSIGHT_BOOKMARK),INSIGHT_BOOKMARK,name,doc->file_name,(guint64)line+1,text,&error)) {
   BookmarkHandle *handle=g_new0(BookmarkHandle,1);handle->doc=doc->id;handle->handle=(gint)SSM(doc->editor->sci,SCI_MARKERADD,line,1);
   g_hash_table_replace(wb.bookmark_handles,g_strdup(name),handle);bookmark_capture_symbol(name,symbols);changed=TRUE;
  } else ui_set_statusbar(TRUE,"Bookmark not persisted: %s",error ? error->message : "invalid line");
  g_clear_error(&error);g_free(name);g_free(text);
 }
 g_hash_table_destroy(covered);if(symbols) g_array_unref(symbols);return changed;
}
void insight_workbench_bookmarks_changed(void) {
 if(!wb.alive || !wb.notes || !wb.bookmark_handles || wb.bookmark_syncing || wb.marks_stale || (wb.marks_project && !wb.marks_valid)) return;
 wb.bookmark_syncing=TRUE;gboolean changed=FALSE;
 for(guint i=0;i<documents_array->len;i++) { GeanyDocument *doc=g_ptr_array_index(documents_array,i);bookmark_restore_document(doc);changed=bookmark_capture_document(doc) || changed; }
 if(changed) { notes_save();notes_filter(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK)); }
 bookmark_list_reload(NULL,NULL);wb.bookmark_syncing=FALSE;
}
static gboolean bookmark_sync_idle(gpointer data) { (void)data;wb.bookmark_sync_timer=0;insight_workbench_bookmarks_changed();return G_SOURCE_REMOVE; }
static gboolean bookmark_open_note(const InsightNote *note) {
 if(!note) return FALSE;
 gchar *path=g_strdup(note->path),*name=g_strdup(note->name);GeanyDocument *old=document_get_current(),*doc=document_find_by_filename(path);
 if(!doc) { gchar *native=utils_get_locale_from_utf8(path);doc=document_open_file(native,FALSE,NULL,NULL);g_free(native); }
 g_free(path);
 if(!doc) { g_free(name);return FALSE; }
 /* Opening can emit document events; look up the note again afterwards. */
 note=insight_notes_get(notes_for(INSIGHT_BOOKMARK),INSIGHT_BOOKMARK,name);BookmarkHandle *handle=g_hash_table_lookup(wb.bookmark_handles,name);g_free(name);
 gint line=handle && handle->doc==doc->id ? (gint)SSM(doc->editor->sci,SCI_MARKERLINEFROMHANDLE,handle->handle,0) : -1;
 if(line<0 && note && sci_get_length(doc->editor->sci)<=2*1024*1024) {
  bookmark_prepare_tags(doc);GArray *symbols=bookmark_symbols(doc);
  gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1);guint64 resolved=0;gboolean ambiguous=FALSE;
  if(insight_bookmark_resolve_symbols(note,text,strlen(text),symbols ? (const InsightBookmarkSymbol *)symbols->data : NULL,symbols ? symbols->len : 0,&resolved,&ambiguous)) line=(gint)resolved-1;
  g_free(text);if(symbols) g_array_unref(symbols);
 }
 if(line<0) { ui_set_statusbar(TRUE,"Bookmark source is unavailable or its line is ambiguous");return FALSE; }
 navqueue_goto_line(old,doc,line+1);SSM(doc->editor->sci,SCI_VERTICALCENTRECARET,0,0);document_grab_focus(doc);return TRUE;
}
static void bookmark_list_add(const gchar *path,guint64 line,const gchar *text,guint doc,gint handle,const gchar *name,GHashTable *seen) {
 gchar *key=g_strdup_printf("%s\001%"G_GUINT64_FORMAT,path,line);
 if(g_hash_table_contains(seen,key)) { g_free(key);return; }g_hash_table_add(seen,key);
 gchar *base=g_path_get_basename(path),*label=g_strdup_printf("%s:%"G_GUINT64_FORMAT"> %s",base,line,text);g_free(base);
 if(insight_lookup_text_match(label,gtk_entry_get_text(GTK_ENTRY(wb.bookmark_search)))) { GtkTreeIter it;gtk_list_store_append(wb.bookmark_list,&it);gtk_list_store_set(wb.bookmark_list,&it,0,label,1,path,2,line,3,doc,4,handle,5,name,-1); }
 g_free(label);
}
static void bookmark_list_reload(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(!wb.bookmark_list) return;
 gtk_list_store_clear(wb.bookmark_list);GHashTable *seen=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 for(guint i=0;i<documents_array->len;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(!doc->is_valid) continue;
  for(gint line=(gint)SSM(doc->editor->sci,SCI_MARKERNEXT,0,2);line>=0;line=(gint)SSM(doc->editor->sci,SCI_MARKERNEXT,line+1,2)) {
   gchar *text=bookmark_line_text(doc->editor->sci,line),*path=doc->file_name ? g_strdup(doc->file_name) : g_strdup_printf("Untitled-%u",doc->id);
   gint marker=-1;
   for(gint index=0;;index++) {
    gint number=(gint)SSM(doc->editor->sci,SCI_MARKERNUMBERFROMLINE,line,index);if(number<0) break;
    if(number==1) { marker=(gint)SSM(doc->editor->sci,SCI_MARKERHANDLEFROMLINE,line,index);break; }
   }
   bookmark_list_add(path,(guint64)line+1,text,doc->id,marker,NULL,seen);g_free(text);g_free(path);
  }
 }
 for(guint i=0;i<notes_for(INSIGHT_BOOKMARK)->items->len;i++) { InsightNote *note=g_ptr_array_index(notes_for(INSIGHT_BOOKMARK)->items,i);if(note->kind==INSIGHT_BOOKMARK) bookmark_list_add(note->path,note->line,note->text,0,-1,note->name,seen); }
 g_hash_table_destroy(seen);
}
static void bookmark_list_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data;GtkTreeModel *model;GtkTreeIter it;
 if(!gtk_tree_selection_get_selected(selection,&model,&it)) return;
 gchar *path=NULL,*name=NULL;guint id=0;guint64 line=0;gint marker=-1;gtk_tree_model_get(model,&it,1,&path,2,&line,3,&id,4,&marker,5,&name,-1);
 if(name) bookmark_open_note(insight_notes_get(notes_for(INSIGHT_BOOKMARK),INSIGHT_BOOKMARK,name));
 else {
  GeanyDocument *old=document_get_current(),*doc=id ? document_find_by_id(id) : document_find_by_filename(path);
  if(!doc && !id) { gchar *native=utils_get_locale_from_utf8(path);doc=document_open_file(native,FALSE,NULL,NULL);g_free(native); }
  if(doc && marker>=0) { gint moved=(gint)SSM(doc->editor->sci,SCI_MARKERLINEFROMHANDLE,marker,0);line=moved>=0 ? (guint64)moved+1 : 0; }
  if(doc && line && line<=(guint64)sci_get_line_count(doc->editor->sci)) { navqueue_goto_line(old,doc,(gint)line);SSM(doc->editor->sci,SCI_VERTICALCENTRECARET,0,0);document_grab_focus(doc); }
 }
 g_free(path);g_free(name);
}
