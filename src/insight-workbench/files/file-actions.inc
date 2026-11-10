/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: File menus and project file actions.
 * RU: Меню и действия над файлами проекта. */

static void file_menu_action(GtkMenuItem *item,gpointer data) { (void)data;file_action(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"insight-file-action"))); }
static GtkWidget *file_menu(void) {
 GtkWidget *menu=g_object_get_data(G_OBJECT(wb.file_view),"insight-file-popup");
 if(!menu) { menu=gtk_menu_new();gtk_menu_attach_to_widget(GTK_MENU(menu),wb.file_view,NULL);g_object_set_data_full(G_OBJECT(wb.file_view),"insight-file-popup",g_object_ref_sink(menu),g_object_unref);const gchar *labels[]={"Open file","Copy file name","Copy full file path","Filter by this file","Clear file query","Reset file columns","Visible file columns","Help for this panel","Show in file manager"},*icons[]={"go-jump","edit-copy","edit-copy","edit-find","edit-clear","view-refresh","view-list-symbolic","panel-help","folder-open"};
  for(guint i=0;i<FP_COUNT;i++) { if(i==FP_FILTER || i==FP_RESET || i==FP_HELP) gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());GtkWidget *item=symbol_history_item(labels[i],icons[i]);gchar *key=g_strdup_printf("insight-file-command-%u",i);gtk_widget_set_name(item,key);g_object_set_data(G_OBJECT(menu),key,item);g_free(key);g_object_set_data(G_OBJECT(item),"insight-file-action",GUINT_TO_POINTER(i));if(i==FP_COLUMNS) gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),insight_symbol_view_columns_menu(GTK_TREE_VIEW(wb.file_view)));else g_signal_connect(item,"activate",G_CALLBACK(file_menu_action),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item); }
 }
 insight_list_tools_menu(GTK_TREE_VIEW(wb.file_view),menu);
 gboolean ready=gtk_widget_get_sensitive(wb.file_view),selected=gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.file_view)))>0;
 for(guint i=0;i<FP_COUNT;i++) { gchar *key=g_strdup_printf("insight-file-command-%u",i);GtkWidget *item=g_object_get_data(G_OBJECT(menu),key);g_free(key);gtk_widget_set_sensitive(item,i==FP_HELP || (ready && ((i>FP_FILTER && i!=FP_REVEAL) || selected))); }insight_i18n_apply(menu);gtk_widget_show_all(menu);return menu;
}
static gboolean file_popup_key(GtkWidget *widget,gpointer data) { (void)data;GdkEvent *event=gtk_get_current_event();gtk_menu_popup_at_widget(GTK_MENU(file_menu()),widget,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,event);if(event) gdk_event_free(event);return TRUE; }
static gboolean file_popup_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)data;if(event->type!=GDK_BUTTON_PRESS || event->button!=3) return FALSE;GtkTreeView *view=GTK_TREE_VIEW(widget);GtkTreeSelection *selection=gtk_tree_view_get_selection(view);GtkTreePath *path=NULL;guint signal=g_signal_lookup("changed",GTK_TYPE_TREE_SELECTION);g_signal_handlers_block_matched(selection,G_SIGNAL_MATCH_ID,signal,0,NULL,NULL,NULL);if(gtk_tree_view_get_path_at_pos(view,(gint)event->x,(gint)event->y,&path,NULL,NULL,NULL)) { gtk_tree_view_set_cursor(view,path,NULL,FALSE);gtk_tree_path_free(path); }else gtk_tree_selection_unselect_all(selection);g_signal_handlers_unblock_matched(selection,G_SIGNAL_MATCH_ID,signal,0,NULL,NULL,NULL);gtk_widget_grab_focus(widget);gtk_menu_popup_at_pointer(GTK_MENU(file_menu()),(GdkEvent *)event);return TRUE;
}
static void project_file_size(GtkTreeViewColumn *column,GtkCellRenderer *renderer,GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 (void)column; (void)data; guint64 size=0; gboolean valid=FALSE;
 gtk_tree_model_get(model,iter,3,&size,4,&valid,-1);
 gchar *text=valid ? g_strdup_printf("%" G_GUINT64_FORMAT,size) : g_strdup("");
 g_object_set(renderer,"text",text,"xalign",1.0f,NULL); g_free(text);
}
static void create_files_browser(void) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4); wb.file_entry=gtk_search_entry_new(); gtk_widget_set_name(wb.file_entry,"insight-file-query");
 gtk_entry_set_placeholder_text(GTK_ENTRY(wb.file_entry),"Find workspace file…");gtk_widget_set_tooltip_text(wb.file_entry,"Enter applies filename masks without opening; * clears the filter");
 wb.file_store=gtk_list_store_new(5,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_UINT64,G_TYPE_BOOLEAN); wb.file_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.file_store)); gtk_widget_set_name(wb.file_view,"insight-project-files");
 const gchar *file_labels[]={"File","Directory","Size"}; const gint file_fields[]={0,2,3};
 for(guint i=0;i<3;i++) { GtkCellRenderer *renderer=gtk_cell_renderer_text_new(); if(i==0) g_object_set(renderer,"weight",PANGO_WEIGHT_BOLD,NULL); GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(file_labels[i],renderer,NULL); if(i==2) gtk_tree_view_column_set_cell_data_func(column,renderer,project_file_size,NULL,NULL); else gtk_tree_view_column_add_attribute(column,renderer,"text",file_fields[i]); gtk_tree_view_column_set_sort_column_id(column,file_fields[i]); gtk_tree_view_append_column(GTK_TREE_VIEW(wb.file_view),column); }
 GtkTreeViewColumn *full_path=gtk_tree_view_column_new_with_attributes("Path",gtk_cell_renderer_text_new(),"text",1,NULL);gtk_tree_view_column_set_sort_column_id(full_path,1);gtk_tree_view_append_column(GTK_TREE_VIEW(wb.file_view),full_path);
 const gint widths[]={220,300,260,90};insight_symbol_view_columns_configure(GTK_TREE_VIEW(wb.file_view),widths,G_N_ELEMENTS(widths));gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.file_view),1);
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.file_store),0,GTK_SORT_ASCENDING);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.file_view);
 scroll=insight_virtual_list_wrap(scroll,GTK_TREE_VIEW(wb.file_view),wb.file_store,virtual_reload,NULL);
 gtk_box_pack_start(GTK_BOX(box),symbol_query_controls(wb.file_entry,FALSE),FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 gtk_box_pack_start(GTK_BOX(box),sort_selector(wb.file_store),FALSE,FALSE,0);wb.file_status=gtk_label_new("");gtk_widget_set_name(wb.file_status,"insight-files-count");gtk_label_set_xalign(GTK_LABEL(wb.file_status),0);gtk_widget_set_tooltip_text(wb.file_status,"Project Files");gtk_box_pack_start(GTK_BOX(box),wb.file_status,FALSE,FALSE,0);
 g_signal_connect(wb.file_view,"button-press-event",G_CALLBACK(file_popup_button),NULL);g_signal_connect(wb.file_view,"popup-menu",G_CALLBACK(file_popup_key),NULL);
 GtkWidget *manage=gtk_button_new_with_label("Add and Remove Project Files…"); g_signal_connect(manage,"clicked",G_CALLBACK(manage_project_files),NULL); gtk_box_pack_start(GTK_BOX(box),manage,FALSE,FALSE,0);
 GtkTargetEntry targets[]={{"text/uri-list",0,0}}; gtk_drag_dest_set(wb.file_view,GTK_DEST_DEFAULT_ALL,targets,G_N_ELEMENTS(targets),GDK_ACTION_COPY); g_signal_connect(wb.file_view,"drag-data-received",G_CALLBACK(project_files_drop),NULL);
 gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),box,gtk_label_new("Project Files"));
 gtk_notebook_reorder_child(GTK_NOTEBOOK(wb.project_notebook),box,0);
 g_signal_connect(wb.file_entry,"changed",G_CALLBACK(files_filter),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.file_view)),"changed",G_CALLBACK(file_selected),NULL);
 g_signal_connect(wb.file_view,"row-activated",G_CALLBACK(browser_activated),NULL);
 g_signal_connect(wb.file_entry,"activate",G_CALLBACK(browser_entry_activate),NULL);
 g_signal_connect(wb.file_entry,"key-press-event",G_CALLBACK(browser_key),NULL);
 g_signal_connect(wb.file_view,"key-press-event",G_CALLBACK(browser_key),NULL);
 gtk_widget_show_all(box); files_filter(NULL,NULL);
}
enum { USAGE_DISPLAY,USAGE_PATH,USAGE_LINE,USAGE_START,USAGE_END,USAGE_ID,USAGE_DIGEST,USAGE_SYMBOL,USAGE_SNIPPET,USAGE_COLUMN,USAGE_COLUMNS };
static void stop_usage(void) {
 g_clear_pointer(&wb.usage_existing,g_hash_table_destroy); wb.usage_collecting=FALSE;
 wb.usage_generation++; if(wb.search_worker) { g_cancellable_cancel(wb.search_worker); g_clear_object(&wb.search_worker); }
 if(wb.usage_idle) { g_source_remove(wb.usage_idle); wb.usage_idle=0; }
 if(wb.usage_paths) g_ptr_array_set_size(wb.usage_paths,0);
 g_clear_pointer(&wb.usage_name,g_free);g_clear_pointer(&wb.usage_usr,g_free);
}
static void invalidate_search_context(void) {
 gboolean active=wb.usage_idle || wb.search_worker; stop_usage();
 if(wb.matches) gtk_list_store_clear(wb.matches);
 if(wb.search_matches) gtk_list_store_clear(wb.search_matches);
 if(active) set_usage_status("Workspace changed; run the search again");
}
static gboolean source_dialect(GeanyFiletype *ft,InsightCodeDialect *out) {
 if(!ft) return FALSE;
 const char *name=ft->name;
 if(!g_strcmp0(name,"Python")) *out=INSIGHT_CODE_PYTHON;
 else if(!g_strcmp0(name,"Lua")) *out=INSIGHT_CODE_LUA;
 else if(!g_strcmp0(name,"JavaScript") || !g_strcmp0(name,"TypeScript")) *out=INSIGHT_CODE_JS;
 else if(!g_strcmp0(name,"Sh")) *out=INSIGHT_CODE_SHELL;
 else if(!g_strcmp0(name,"C") || !g_strcmp0(name,"C++") || !g_strcmp0(name,"C#") || !g_strcmp0(name,"Java")) *out=INSIGHT_CODE_C;
 else return FALSE;
 return TRUE;
}
static InsightFunctionStyle function_style(InsightCodeDialect dialect) {
 return dialect==INSIGHT_CODE_PYTHON ? INSIGHT_FUNCTION_PYTHON : (dialect==INSIGHT_CODE_LUA ? INSIGHT_FUNCTION_LUA : INSIGHT_FUNCTION_BRACES);
}
static gchar *reference_snippet(const char *text,gsize at) {
 gsize begin=at,end=at;
 while(begin && at-begin<320 && text[begin-1]!='\n' && text[begin-1]!='\r') begin--;
 while(((guchar)text[begin]&0xc0)==0x80) begin++;
 while(text[end] && end-at<480 && text[end]!='\n' && text[end]!='\r') end++;
 while(((guchar)text[end]&0xc0)==0x80) end++;
 gchar *line=g_strndup(text+begin,end-begin); g_strstrip(line);
 if(g_utf8_strlen(line,-1)>180) { gchar *shortened=g_utf8_substring(line,0,180); g_free(line); line=g_strconcat(shortened,"…",NULL); g_free(shortened); }
 return line;
}
static TMTag *relation_enclosing_function(GeanyDocument *doc) {
 if(!doc || !doc->tm_file || !doc->tm_file->tags_array || sci_get_length(doc->editor->sci)>2*1024*1024) return NULL;
 InsightCodeDialect dialect;if(!source_dialect(doc->file_type,&dialect)) return NULL;
 gint length=sci_get_length(doc->editor->sci);gchar *text=sci_get_contents(doc->editor->sci,length+1),*mask=insight_source_code_mask(text,length,dialect,NULL);if(!mask) { g_free(text);return NULL; }
 size_t caret=(size_t)sci_get_current_position(doc->editor->sci),best=SIZE_MAX;TMTag *selected=NULL;
 for(guint i=0;i<doc->tm_file->tags_array->len;i++) {
  TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,i);
  if(!tag || !(tag->type&(tm_tag_function_t|tm_tag_method_t)) || !tag->line || tag->line>(gulong)sci_get_line_count(doc->editor->sci)) continue;
  size_t start=(size_t)sci_get_position_from_line(doc->editor->sci,tag->line-1),end=0,body=0;
  if(start<=caret && insight_function_range(text,mask,length,start,function_style(dialect),&body,&end)==1 && caret<end && end-start<best) { selected=tag;best=end-start; }
 }
 g_free(mask);g_free(text);return selected;
}
static void relation_calls(const char *query) {
 gtk_list_store_clear(wb.matches);GeanyDocument *doc=document_get_current();InsightCodeDialect dialect;
 if(!doc || sci_get_length(doc->editor->sci)>2*1024*1024 || !source_dialect(doc->file_type,&dialect)) { set_usage_status("Calls requires a supported function language and a current buffer up to 2 MiB");return; }
 document_update_tags(doc);if(!doc->tm_file || !doc->tm_file->tags_array) { set_usage_status("No parsed function definitions");return; }
 TMTag *chosen=NULL;guint count=0;
 for(guint i=0;i<doc->tm_file->tags_array->len;i++) {
  TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,i);
  if(tag && (tag->type&(tm_tag_function_t|tm_tag_method_t)) && tag->name && !strcmp(tag->name,query)) { chosen=tag;count++; }
 }
 if(count>1) { TMTag *current=relation_enclosing_function(doc);if(current && !strcmp(current->name,query)) chosen=current;else { set_usage_status("Several definitions: place the caret inside the desired function");return; } }
 if(!chosen || !chosen->line || chosen->line>(gulong)sci_get_line_count(doc->editor->sci)) { set_usage_status("Function definition not found in the current buffer");return; }
 gint length=sci_get_length(doc->editor->sci);gchar *text=sci_get_contents(doc->editor->sci,length+1),*mask=insight_source_code_mask(text,length,dialect,NULL);
 size_t start=(size_t)sci_get_position_from_line(doc->editor->sci,chosen->line-1),end=0,body=0;
 if(!mask || insight_function_range(text,mask,length,start,function_style(dialect),&body,&end)!=1) { set_usage_status("No complete function body");g_free(mask);g_free(text);return; }
 gchar *digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);size_t cursor=body,a=0,b=0;guint shown=0;
 while(shown<512 && insight_next_call(mask,end,&cursor,&a,&b)==1) {
  gchar *name=g_strndup(text+a,b-a),*snippet=reference_snippet(text,a);GtkTreeIter iter;gint line=sci_get_line_from_position(doc->editor->sci,(gint)a);
  gtk_list_store_append(wb.matches,&iter);gtk_list_store_set(wb.matches,&iter,USAGE_DISPLAY,doc->file_name ? doc->file_name : "untitled",USAGE_PATH,doc->file_name,USAGE_LINE,(gulong)line+1,USAGE_START,(gulong)a,USAGE_END,(gulong)b,USAGE_ID,doc->id,USAGE_DIGEST,digest,USAGE_SYMBOL,name,USAGE_SNIPPET,snippet,-1);
  g_free(name);g_free(snippet);shown++;
 }
 gchar *status=g_strdup_printf("%u lexical call sites%s • current function; overloads and types are not resolved",shown,shown==512 ? " (limit reached)" : "");set_usage_status(status);g_free(status);g_free(digest);g_free(mask);g_free(text);
}
static InsightSourceRegion reference_region(void) {
 if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_code))) return INSIGHT_REGION_CODE;
 gint mode=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.usage_comments));return mode==1 ? INSIGHT_REGION_SKIP_COMMENTS : mode==2 ? INSIGHT_REGION_COMMENTS : INSIGHT_REGION_ALL;
}
typedef struct { gchar *path,*query,*text,*digest,*cached_stamp,*cached_language,*clang_root,*clang_resources,*usr;InsightIndex *cached_calls; InsightOccurrence *found; GPtrArray *snippets; gulong *columns; gsize total,apply_position; guint id,flags,context,capacity; gint mode,encoding; InsightCodeDialect dialect; gboolean keywords,have_dialect,valid,partial;InsightSourceRegion region; guint64 generation,epoch; } SearchJob;
static gboolean usage_step(gpointer);
static void search_job_free(gpointer data) { SearchJob *j=data; g_free(j->path); g_free(j->query); g_free(j->text); g_free(j->digest);g_free(j->cached_stamp);g_free(j->cached_language);g_free(j->clang_root);g_free(j->clang_resources);g_free(j->usr);retire_index(j->cached_calls); g_free(j->found); g_clear_pointer(&j->snippets,g_ptr_array_unref); g_free(j->columns); g_free(j); }
static void clang_search_result(SearchJob *j,InsightIndex *relations,const gchar *digest) {
 gsize total=insight_index_query(relations,j->query,TRUE,NULL,"@clangref",j->path,0,NULL,0);if(total>100000) return;const InsightSymbol **rows=g_new(const InsightSymbol *,total);insight_index_query(relations,j->query,TRUE,NULL,"@clangref",j->path,0,rows,total);
 j->digest=g_strdup(digest);j->found=g_new0(InsightOccurrence,j->capacity);j->columns=g_new0(gulong,j->capacity);j->snippets=g_ptr_array_new_with_free_func(g_free);j->valid=TRUE;
 for(gsize i=0;i<total;i++) {if(j->usr && strcmp(j->usr,rows[i]->scope)) continue;gsize n=j->total++;if(n>=j->capacity) continue;gchar **parts=g_strsplit(rows[i]->signature,":",3);if(g_strv_length(parts)!=3) j->valid=FALSE;else {j->found[n]=(InsightOccurrence){g_ascii_strtoull(parts[0],NULL,10),g_ascii_strtoull(parts[1],NULL,10),rows[i]->line,0};j->columns[n]=g_ascii_strtoull(parts[2],NULL,10);g_ptr_array_add(j->snippets,g_strdup(rows[i]->type));}g_strfreev(parts);}g_free(rows);
}
static void search_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; SearchJob *j=data;
 if(j->mode==8 && j->cached_calls && !j->text) {
  gchar *native=utils_get_locale_from_utf8(j->path),*now=source_stamp(native,j->cached_language,j->encoding);g_free(native);const InsightSymbol *coverage=NULL;
  gboolean usable=!g_strcmp0(now,j->cached_stamp) && insight_index_query(j->cached_calls,"@clangfile",TRUE,NULL,"@clangfile",j->path,0,&coverage,1)==1 && !strcmp(coverage->type,"complete") && insight_clang_dependencies_fresh(j->cached_calls,j->path);g_free(now);
  if(usable) {clang_search_result(j,j->cached_calls,coverage->inheritance);g_task_return_boolean(task,TRUE);return;}
 }
 if(j->mode==7&&!j->text&&j->cached_calls&&j->cached_stamp) {
  gchar *native=utils_get_locale_from_utf8(j->path);gchar *now=source_stamp(native,j->cached_language,j->encoding);g_free(native);
  const InsightSymbol *coverage=NULL;gboolean usable=!g_strcmp0(now,j->cached_stamp)&&insight_index_query(j->cached_calls,"@file",TRUE,NULL,"@file",j->path,0,&coverage,1)==1;g_free(now);
  if(usable) {
   j->digest=g_strdup(coverage->inheritance);j->total=insight_index_query(j->cached_calls,j->query,TRUE,NULL,"@call",j->path,0,NULL,0);gsize count=MIN(j->total,j->capacity);const InsightSymbol **rows=g_new(const InsightSymbol *,count);insight_index_query(j->cached_calls,j->query,TRUE,NULL,"@call",j->path,0,rows,count);j->found=g_new0(InsightOccurrence,count);j->columns=g_new0(gulong,count);j->snippets=g_ptr_array_new_with_free_func(g_free);j->valid=TRUE;
   for(gsize n=0;n<count;n++) { gchar **parts=g_strsplit(rows[n]->signature,":",3);if(g_strv_length(parts)!=3) j->valid=FALSE;else { j->found[n]=(InsightOccurrence){g_ascii_strtoull(parts[0],NULL,10),g_ascii_strtoull(parts[1],NULL,10),rows[n]->line,0};j->columns[n]=g_ascii_strtoull(parts[2],NULL,10);g_ptr_array_add(j->snippets,g_strdup(rows[n]->type)); }g_strfreev(parts); }g_free(rows);g_task_return_boolean(task,TRUE);return;
  }
 }
 if(!j->text && j->path && !g_cancellable_is_cancelled(cancel)) j->text=read_disk_source_full(j->path,TRUE,j->encoding,cancel);
 if(j->mode==8) {if(j->text && insight_clang_source(j->path)) {InsightClangResult *r=insight_clang_parse(j->path,j->text,j->clang_root,j->clang_resources,NULL,cancel,NULL);if(r && !r->limited) {clang_search_result(j,r->relations,r->digest);j->partial=r->errors || r->ambiguous_command;}insight_clang_result_free(r);}g_task_return_boolean(task,TRUE);return;}
 char *mask=j->text && j->region!=INSIGHT_REGION_ALL && j->have_dialect ? insight_source_region_mask(j->text,strlen(j->text),j->dialect,j->region,NULL) : NULL;
 const char *text=j->region!=INSIGHT_REGION_ALL ? mask : j->text; j->found=g_new(InsightOccurrence,j->capacity);
 if(text && !g_cancellable_is_cancelled(cancel)) {
  if(j->mode==3) {
   InsightSearchMatch *matches=g_new(InsightSearchMatch,j->capacity);
   j->valid=j->keywords ? insight_keyword_collect(text,strlen(text),j->query,j->flags,j->context,matches,j->capacity,&j->total,NULL) : insight_context_collect(text,strlen(text),j->query,j->flags,matches,j->capacity,&j->total,NULL);
   if(j->valid) for(gsize n=0;n<MIN(j->total,j->capacity);n++) j->found[n]=(InsightOccurrence){matches[n].start,matches[n].end,matches[n].line,matches[n].column};
   g_free(matches);
  } else j->valid=insight_occurrences_find(text,strlen(text),j->query,strlen(j->query),0,j->found,j->capacity,&j->total);
  if(j->valid && j->mode==7) {
   gsize kept=0;size_t text_length=strlen(text);for(gsize n=0;n<MIN(j->total,j->capacity);n++) { size_t cursor=j->found[n].start,a=0,b=0;
    if(insight_next_call(text,text_length,&cursor,&a,&b)==1 && a==j->found[n].start && b==j->found[n].end) j->found[kept++]=j->found[n];
   } j->total=kept;
  }
  if(j->valid) {
   j->digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,j->text,-1);
   gsize count=MIN(j->total,j->capacity); j->snippets=g_ptr_array_new_with_free_func(g_free); j->columns=g_new(gulong,count);
   for(gsize n=0;n<count && !g_cancellable_is_cancelled(cancel);n++) {
    InsightOccurrence *hit=&j->found[n]; g_ptr_array_add(j->snippets,reference_snippet(j->text,hit->start));
    j->columns[n]=(gulong)g_utf8_strlen(j->text+hit->start-hit->column,(gssize)hit->column)+1;
   }
   if(g_cancellable_is_cancelled(cancel)) j->valid=FALSE;
  }
 }
 g_free(mask); g_task_return_boolean(task,TRUE);
}
static gchar *search_row_key(const gchar *display,const gchar *path,const gchar *name,const gchar *snippet,const gchar *digest,gulong line,gulong start,gulong end,guint id,gulong column) {
 GString *key=g_string_new(NULL); const gchar *fields[]={display,path,name,snippet,digest};
 for(guint n=0;n<G_N_ELEMENTS(fields);n++) {
  if(fields[n]) g_string_append_printf(key,"%" G_GSIZE_FORMAT ":%s",strlen(fields[n]),fields[n]); else g_string_append(key,"-1:");
 }
 g_string_append_printf(key,":%lu:%lu:%lu:%u:%lu",line,start,end,id,column); return g_string_free(key,FALSE);
}
static gchar *search_existing_key(GtkTreeModel *model,GtkTreeIter *iter) {
 gchar *display=NULL,*path=NULL,*name=NULL,*snippet=NULL,*digest=NULL; gulong line=0,start=0,end=0,column=0; guint id=0;
 gtk_tree_model_get(model,iter,USAGE_DISPLAY,&display,USAGE_PATH,&path,USAGE_SYMBOL,&name,USAGE_SNIPPET,&snippet,USAGE_DIGEST,&digest,USAGE_LINE,&line,USAGE_START,&start,USAGE_END,&end,USAGE_ID,&id,USAGE_COLUMN,&column,-1);
 gchar *key=search_row_key(display,path,name,snippet,digest,line,start,end,id,column);
 g_free(display); g_free(path); g_free(name); g_free(snippet); g_free(digest); return key;
}
static gboolean search_results_apply(gpointer data) {
 GTask *task=data; SearchJob *j=g_task_get_task_data(task);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.usage_generation) return G_SOURCE_REMOVE;
 gint64 deadline=g_get_monotonic_time()+3000;
 while(j->apply_position<MIN(j->total,j->capacity)) {
  gsize n=j->apply_position++; InsightOccurrence *hit=&j->found[n]; GtkTreeIter iter;
  gchar *key=search_row_key(j->path ? j->path : "untitled",j->path,j->query,g_ptr_array_index(j->snippets,n),j->digest,hit->line,hit->start,hit->end,j->id,j->columns[n]);
  GQueue *q=g_hash_table_lookup(wb.usage_existing,key); GtkTreeIter *old=q ? g_queue_pop_head(q) : NULL; g_free(key);
  if(old) { iter=*old;g_free(old); }
  else { gtk_list_store_append(wb.usage_target,&iter); gtk_list_store_set(wb.usage_target,&iter,USAGE_DISPLAY,j->path ? j->path : "untitled",USAGE_PATH,j->path,USAGE_LINE,hit->line,USAGE_COLUMN,j->columns[n],USAGE_START,(gulong)hit->start,USAGE_END,(gulong)hit->end,USAGE_ID,j->id,USAGE_DIGEST,j->digest,USAGE_SYMBOL,j->query,USAGE_SNIPPET,(gchar *)g_ptr_array_index(j->snippets,n),-1); } if(j->mode==3 && wb.search_links) insight_search_links_capture(wb.search_links,&iter); wb.usage_shown++;
  if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
 }
 gchar *label=g_strdup_printf("Searching… %u results • %u/%u files",wb.usage_shown,wb.usage_position,wb.usage_paths->len); set_usage_status(label); g_free(label);
 wb.usage_idle=g_idle_add(usage_step,NULL); return G_SOURCE_REMOVE;
}
static void search_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); SearchJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.usage_generation) return;
 g_clear_object(&wb.search_worker);
 if(j->partial) wb.usage_skipped++;
 if(ok && j->valid) wb.usage_idle=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,search_results_apply,g_object_ref(task),g_object_unref);
 else { wb.usage_skipped++; wb.usage_idle=g_idle_add(usage_step,NULL); }
}
static gboolean usage_step(gpointer data) {
 (void)data; gint64 deadline=g_get_monotonic_time()+3000;
 if(wb.usage_collecting) {
  while(wb.usage_iter_valid) {
   gchar *key=search_existing_key(GTK_TREE_MODEL(wb.usage_target),&wb.usage_iter); GQueue *q=g_hash_table_lookup(wb.usage_existing,key);
   if(!q) { q=g_queue_new(); g_hash_table_insert(wb.usage_existing,key,q); } else g_free(key);
   GtkTreeIter *copy=g_new(GtkTreeIter,1); *copy=wb.usage_iter; g_queue_push_tail(q,copy);
   wb.usage_iter_valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(wb.usage_target),&wb.usage_iter);
   if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
  }
  wb.usage_collecting=FALSE;
 }
 if(wb.usage_position>=wb.usage_paths->len || wb.usage_shown>=2000) {
  GHashTableIter pending; gpointer value; g_hash_table_iter_init(&pending,wb.usage_existing);
  while(g_hash_table_iter_next(&pending,NULL,&value)) {
   GQueue *q=value; GtkTreeIter *old;
   while((old=g_queue_pop_head(q))) { gtk_list_store_remove(wb.usage_target,old); g_free(old); if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE; }
   g_hash_table_iter_remove(&pending);
  }
  g_clear_pointer(&wb.usage_existing,g_hash_table_destroy);
  wb.usage_idle=0; gchar *label=g_strdup_printf("%u results • %u files scanned • %u skipped%s",wb.usage_shown,wb.usage_position,wb.usage_skipped,wb.usage_shown>=2000 ? " • result limit reached" : "");if(wb.usage_mode==8) {gchar *semantic=g_strconcat(insight_tr(wb.usage_usr ? "Clang symbol identity:":"Clang references by name:")," ",label,NULL);g_free(label);label=semantic;} set_usage_status(label); g_free(label); return G_SOURCE_REMOVE;
 }
 Location *location=g_ptr_array_index(wb.usage_paths,wb.usage_position++); GeanyDocument *doc=location->id ? document_find_by_id(location->id) : document_find_by_filename(location->path);
 SearchJob *j=g_new0(SearchJob,1);j->usr=g_strdup(wb.usage_usr);j->clang_root=wb.root ? utils_get_utf8_from_locale(wb.root):NULL;j->clang_resources=g_build_filename(app->datadir,"insight","clang","resources",NULL); j->path=g_strdup(location->path); j->query=g_strdup(wb.usage_name); j->id=doc ? doc->id : 0; j->mode=wb.usage_mode; j->flags=wb.usage_flags; j->keywords=wb.usage_keywords; j->context=wb.usage_context; j->capacity=2000-wb.usage_shown; j->encoding=file_prefs.default_open_encoding; j->generation=wb.usage_generation; j->epoch=workbench_epoch;
 if(doc) { gint length=sci_get_length(doc->editor->sci); if(length>=0 && length<=2*1024*1024) { j->text=sci_get_contents(doc->editor->sci,length+1); if(j->text && (!g_utf8_validate(j->text,length,NULL) || memchr(j->text,0,length))) g_clear_pointer(&j->text,g_free); } }
 if((j->mode==7||j->mode==8)&&!doc&&j->path) { j->cached_calls=insight_index_ref(wb.disk_calls);j->cached_stamp=g_strdup(g_hash_table_lookup(wb.file_stamps,j->path)); }
 j->region=j->mode==7 ? INSIGHT_REGION_CODE : j->mode==0 ? wb.usage_region : j->mode==3 ? wb.project_region : INSIGHT_REGION_ALL; GeanyFiletype *ft=doc ? doc->file_type : (j->path ? filetypes_detect_from_extension(j->path) : NULL); if(j->cached_stamp&&(!ft||ft->id==GEANY_FILETYPES_NONE)) { const gchar *colon=strchr(j->cached_stamp,':');if(colon) { gchar *language=g_strndup(j->cached_stamp,colon-j->cached_stamp);GeanyFiletype *cached=filetypes_lookup_by_name(language);if(cached) ft=cached;g_free(language); } }j->have_dialect=source_dialect(ft,&j->dialect);j->cached_language=g_strdup(ft?ft->name:NULL);
 wb.search_worker=g_cancellable_new(); GTask *task=g_task_new(NULL,wb.search_worker,search_job_done,NULL); g_task_set_task_data(task,j,search_job_free); g_task_run_in_thread(task,search_job_run); g_object_unref(task); wb.usage_idle=0; return G_SOURCE_REMOVE;
}
static gboolean search_path_matches(const char *path) {
 const char *input=gtk_entry_get_text(GTK_ENTRY(wb.search_mask)); if(!*input) return TRUE;
 gchar **patterns=g_strsplit(input,";",-1),*name=g_path_get_basename(path),*folded=g_utf8_casefold(name,-1); gboolean matched=FALSE;
 for(guint i=0;patterns[i];i++) { gchar *pattern=g_utf8_casefold(g_strstrip(patterns[i]),-1); if(*pattern && g_pattern_match_simple(pattern,folded)) matched=TRUE; g_free(pattern); }
 g_free(folded); g_free(name); g_strfreev(patterns); return matched;
}
typedef struct { InsightIndex *index; gchar *query; const InsightSymbol *found[2000]; gsize total,apply_position; GPtrArray *descriptions; GHashTable *existing; GtkTreeIter iter; gboolean collecting,iter_valid,invalid; gint mode; guint64 epoch,generation,index_version;InsightLanguageFilter *language;gboolean limited,transitive;int depth_limited,ambiguous; } RelationJob;
static void usage_start(GtkWidget *,gpointer);
static void relation_job_free(gpointer data) { RelationJob *j=data;insight_language_filter_free(j->language); retire_index(j->index); g_free(j->query); g_clear_pointer(&j->descriptions,g_ptr_array_unref); g_clear_pointer(&j->existing,g_hash_table_destroy); g_free(j); }
static int relation_eligible(const InsightSymbol *symbol,void *data) { return insight_language_filter_match(data,symbol->path); }
static void relation_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; RelationJob *j=data; j->total=SIZE_MAX;
 InsightRelationMode mode=j->mode==1 ? INSIGHT_RELATION_DEFINITIONS : (j->mode==2 ? INSIGHT_RELATION_MEMBERS : (j->mode==4 ? INSIGHT_RELATION_BASES : INSIGHT_RELATION_DERIVED));
 if(!g_cancellable_is_cancelled(cancel)) {
  if(mode!=INSIGHT_RELATION_DEFINITIONS) j->total=insight_relation_query_transitive(j->index,j->query,mode,j->transitive ? 32 : (mode==INSIGHT_RELATION_MEMBERS ? 0 : 1),relation_eligible,j->language,0,j->found,G_N_ELEMENTS(j->found),j->transitive ? &j->depth_limited : NULL,&j->ambiguous);
  else j->total=insight_relation_query(j->index,j->query,mode,0,j->found,G_N_ELEMENTS(j->found));
 }
 if(j->total!=SIZE_MAX) { gsize eligible=0;gsize original=j->total;j->limited=original>G_N_ELEMENTS(j->found);for(gsize n=0;n<MIN(original,G_N_ELEMENTS(j->found));n++) if(insight_language_filter_match(j->language,j->found[n]->path)) j->found[eligible++]=j->found[n];j->total=eligible; }
 j->descriptions=g_ptr_array_new_with_free_func(g_free);
 if(j->total!=SIZE_MAX) for(gsize n=0;n<MIN(j->total,G_N_ELEMENTS(j->found)) && !g_cancellable_is_cancelled(cancel);n++) {
  const InsightSymbol *tag=j->found[n]; g_ptr_array_add(j->descriptions,g_strdup_printf("%s • %s %s%s • %s • bases: %s",tag->kind,tag->type,tag->name,tag->signature,tag->scope,tag->inheritance));
 }
 g_task_return_boolean(task,!g_cancellable_is_cancelled(cancel));
}
