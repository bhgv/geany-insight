/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "navigation.h"
#include "tm_tag.h"
#include "index_cache.h"
#include "context.h"
#include "extensions.h"
#include <stdlib.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <limits.h>
#include <string.h>
typedef struct { gchar *path; GDir *dir; } Frame;
struct Navigation {
 GeanyPlugin *plugin;
 InsightIndex *index, *base;
 gchar *root, *cache_path;
 GtkWidget *menu, *find_item;
 GPtrArray *frames;
 guint idle, files, skipped;
};
static void frame_free(gpointer data) {
 Frame *f=data; if(f->dir) g_dir_close(f->dir); g_free(f->path); g_free(f);
}
static int import_tags(InsightIndex *index,TMSourceFile *file,const char *path) {
 if(!file || !file->tags_array) return 1;
 guint count=file->tags_array->len,used=0;
 InsightSymbol *symbols=g_new0(InsightSymbol,count);
 gchar (*kinds)[2]=g_malloc0((gsize)count*2);
 for(guint j=0;j<count;j++) {
  TMTag *tag=g_ptr_array_index(file->tags_array,j);
  if(!tag->name || !*tag->name || !tag->line) continue;
  kinds[used][0]=tag->kind_letter;
  symbols[used]=(InsightSymbol){tag->name,path,tag->scope,kinds[used],tag->line}; used++;
 }
 int ok=insight_index_update(index,path,symbols,used);
 g_free(kinds); g_free(symbols); return ok;
}
static void refresh_open(Navigation *n) {
 InsightIndex *live=insight_index_clone(n->base);
 if(!live) { ui_set_statusbar(TRUE,"Insight: index allocation failed"); return; }
 GPtrArray *docs=n->plugin->geany_data->documents_array;
 for(guint j=0;j<docs->len;j++) {
  GeanyDocument *doc=g_ptr_array_index(docs,j);
  if(doc->is_valid && doc->file_name && doc->tm_file)
   if(!import_tags(live,doc->tm_file,doc->file_name)) {
    insight_index_free(live); ui_set_statusbar(TRUE,"Insight: index allocation failed"); return;
   }
 }
 insight_index_free(n->index); n->index=live;
}
const InsightIndex *navigation_index(Navigation *n) {
 if(!n) return NULL;
 refresh_open(n); return n->index;
}
static void stop_scan(Navigation *n) {
 if(n->idle) { g_source_remove(n->idle); n->idle=0; }
 g_ptr_array_set_size(n->frames,0);
}
static void save_cache(Navigation *n) {
 if(!n->root) return;
 gchar *directory=g_path_get_dirname(n->cache_path);
 if(g_mkdir_with_parents(directory,0700)!=0) {
  ui_set_statusbar(TRUE,"Insight: unable to create index cache directory"); g_free(directory); return;
 }
 g_free(directory); GError *error=NULL;
 if(!insight_cache_save(n->cache_path,n->base,n->root,&error)) {
  ui_set_statusbar(TRUE,"Insight: index cache not saved: %s",error->message); g_error_free(error);
 }
}
static gboolean scan_step(gpointer data) {
 Navigation *n=data;
 if(!n->frames->len || n->files>=10000) {
  gboolean limited=n->files>=10000;
  n->idle=0; g_ptr_array_set_size(n->frames,0); refresh_open(n);
  ui_set_statusbar(TRUE,"Insight: %u files, %zu symbols, %u skipped%s",n->files,
   insight_index_count(n->index),n->skipped,limited ? " (file limit reached)" : "");
  if(!limited) save_cache(n);
  return G_SOURCE_REMOVE;
 }
 Frame *frame=g_ptr_array_index(n->frames,n->frames->len-1);
 const char *entry=g_dir_read_name(frame->dir);
 if(!entry) { g_ptr_array_remove_index(n->frames,n->frames->len-1); return G_SOURCE_CONTINUE; }
 if(entry[0]=='.' || !strcmp(entry,"node_modules") || !strcmp(entry,"__pycache__"))
  return G_SOURCE_CONTINUE;
 gchar *path=g_build_filename(frame->path,entry,NULL);
 GStatBuf st;
 if(g_lstat(path,&st)!=0) { n->skipped++; g_free(path); return G_SOURCE_CONTINUE; }
 if(S_ISDIR(st.st_mode)) {
  GDir *dir=n->frames->len<64 ? g_dir_open(path,0,NULL) : NULL;
  if(dir) { Frame *next=g_new0(Frame,1); next->path=path; next->dir=dir; g_ptr_array_add(n->frames,next); }
  else { n->skipped++; g_free(path); }
  return G_SOURCE_CONTINUE;
 }
 if(!S_ISREG(st.st_mode) || st.st_size>2*1024*1024) {
  n->skipped++; g_free(path); return G_SOURCE_CONTINUE;
 }
 gchar *utf8=g_filename_to_utf8(path,-1,NULL,NULL,NULL);
 GeanyFiletype *ft=utf8 ? filetypes_detect_from_file(utf8) : NULL;
 if(ft && ft->lang>=0) {
  TMSourceFile *file=tm_source_file_new(path,ft->name);
  if(file) {
   /* Custom filetypes may have a different name from their parser. */
   file->lang=ft->lang;
   /* Public Geany API parses on add; remove before releasing our source file. */
   tm_workspace_add_source_file(file);
   if(!import_tags(n->base,file,utf8)) {
    ui_set_statusbar(TRUE,"Insight: index allocation failed; scan stopped");
    tm_workspace_remove_source_file(file); tm_source_file_free(file);
    g_free(utf8); g_free(path); n->idle=0;
    g_ptr_array_set_size(n->frames,0); return G_SOURCE_REMOVE;
   }
   tm_workspace_remove_source_file(file); n->files++;
  } else n->skipped++;
  if(file) tm_source_file_free(file);
 } else n->skipped++;
 g_free(utf8); g_free(path); return G_SOURCE_CONTINUE;
}
static gboolean start_scan(Navigation *n,const char *path) {
 gchar *owned=g_strdup(path);
 GDir *dir=g_dir_open(owned,0,NULL); InsightIndex *next=insight_index_new();
 InsightIndex *live=next ? insight_index_clone(next) : NULL;
 if(!dir || !next || !live) {
  if(dir) g_dir_close(dir);
  insight_index_free(next); insight_index_free(live); g_free(owned);
  ui_set_statusbar(TRUE,"Insight: cannot index directory"); return FALSE;
 }
 stop_scan(n); insight_index_free(n->index); insight_index_free(n->base);
 n->base=next; n->index=live; g_free(n->root); n->root=g_strdup(owned);
 Frame *frame=g_new0(Frame,1); frame->path=owned; frame->dir=dir;
 g_ptr_array_add(n->frames,frame); n->files=0; n->skipped=0;
 n->idle=g_idle_add(scan_step,n); ui_set_statusbar(TRUE,"Insight: indexing…"); return TRUE;
}
static void choose_root(GtkMenuItem *item,gpointer data) {
 (void)item; Navigation *n=data;
 GtkWidget *dialog=gtk_file_chooser_dialog_new("Index source directory",
 GTK_WINDOW(n->plugin->geany_data->main_widgets->window),GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
 "Cancel",GTK_RESPONSE_CANCEL,"Index",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
  gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
  start_scan(n,path); g_free(path);
 }
 gtk_widget_destroy(dialog);
}
static void reindex(GtkMenuItem *item,gpointer data) {
 (void)item; Navigation *n=data;
 if(n->root && *n->root) start_scan(n,n->root);
 else ui_set_statusbar(TRUE,"Insight: choose a source directory first");
}
static void cancel(GtkMenuItem *item,gpointer data) {
 (void)item; Navigation *n=data; stop_scan(n);
 ui_set_statusbar(TRUE,"Insight: scan cancelled; partial index retained");
}
enum { NAME, SCOPE, PATH, LINE, KIND, COLUMNS };
typedef struct { Navigation *n; GtkListStore *store; GtkWidget *status, *preview, *entry, *scope, *kind, *exact, *view; } Search;
static void filter(GtkEditable *entry,gpointer data) {
 (void)entry; Search *s=data; if(s->n->idle) refresh_open(s->n);
 const char *query=gtk_entry_get_text(GTK_ENTRY(s->entry));
 const InsightSymbol *found[500];
 size_t total=insight_index_find_filtered(s->n->index,query,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->exact)),gtk_entry_get_text(GTK_ENTRY(s->scope)),gtk_entry_get_text(GTK_ENTRY(s->kind)),found,500);
 gtk_list_store_clear(s->store);
 for(size_t j=0;j<total && j<500;j++) {
  GtkTreeIter iter; gtk_list_store_append(s->store,&iter);
  gtk_list_store_set(s->store,&iter,NAME,found[j]->name,SCOPE,found[j]->scope,
   PATH,found[j]->path,LINE,(gulong)found[j]->line,KIND,found[j]->kind,-1);
 }
 gchar *label=g_strdup_printf("%zu matches%s%s",total,total>500 ? " (first 500 shown)" : "",
  s->n->idle ? " — indexing in progress" : "");
 gtk_label_set_text(GTK_LABEL(s->status),label); g_free(label);
 GtkTreeIter first;
 if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(s->store),&first)) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(s->view)),&first);
}
static void filter_toggle(GtkToggleButton *toggle,gpointer data) {
 (void)toggle; Search *s=data; filter(GTK_EDITABLE(s->entry),s);
}
static void preview_selection(GtkTreeSelection *selection,gpointer data) {
 Search *s=data; GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->preview));
 GtkTreeIter iter; GtkTreeModel *model;
 if(!gtk_tree_selection_get_selected(selection,&model,&iter)) { gtk_text_buffer_set_text(buffer,"",0); return; }
 gchar *path; gulong line; gtk_tree_model_get(model,&iter,PATH,&path,LINE,&line,-1);
 gchar *locale=utils_get_locale_from_utf8(path); g_free(path);
 char *source=NULL; gsize length=0; GError *error=NULL;
 size_t start,end,target_start,target_end; unsigned long first;
 if(!insight_read_source_file(locale,&source,&length,&error)) {
  gtk_text_buffer_set_text(buffer,error->message,-1); g_error_free(error);
 } else if(!g_utf8_validate(source,(gssize)length,NULL) || memchr(source,0,length)) {
  gtk_text_buffer_set_text(buffer,"Context requires UTF-8 text without NUL",-1);
 } else if(!insight_context_range(source,length,line,5,&start,&end,&first,&target_start,&target_end)) {
  gtk_text_buffer_set_text(buffer,"Symbol line is unavailable; reindex the project",-1);
 } else if(end-start>65536) gtk_text_buffer_set_text(buffer,"Context exceeds 64 KiB; open the definition",-1);
 else gtk_text_buffer_set_text(buffer,source+start,(gint)(end-start));
 g_free(source); g_free(locale);
}
static void row_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)view; (void)path; (void)column; gtk_dialog_response(GTK_DIALOG(data),GTK_RESPONSE_ACCEPT);
}
static void show_search(Navigation *n,const char *initial,int exact) {
 refresh_open(n);
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Find project symbol",
 GTK_WINDOW(n->plugin->geany_data->main_widgets->window),GTK_DIALOG_DESTROY_WITH_PARENT,
 "Cancel",GTK_RESPONSE_CANCEL,"Go to",GTK_RESPONSE_ACCEPT,NULL);
 gtk_window_set_default_size(GTK_WINDOW(dialog),900,500);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
 GtkWidget *entry=gtk_entry_new(),*status=gtk_label_new("");
 gtk_entry_set_placeholder_text(GTK_ENTRY(entry),"Symbol prefix (case-sensitive)");
 GtkListStore *store=gtk_list_store_new(COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING);
 GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
 const char *titles[]={"Name","Scope","File","Line","Kind"};
 for(int j=0;j<COLUMNS;j++) {
  GtkCellRenderer *renderer=gtk_cell_renderer_text_new();
  GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(titles[j],renderer,"text",j,NULL);
  gtk_tree_view_column_set_sort_column_id(column,j); gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);
 }
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),view);
 gtk_box_pack_start(GTK_BOX(box),entry,FALSE,FALSE,4);
 gtk_box_pack_start(GTK_BOX(box),status,FALSE,FALSE,4);
 gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,4);
 gtk_entry_set_text(GTK_ENTRY(entry),initial ? initial : "");
 GtkWidget *preview=gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(preview),FALSE);
 gtk_text_view_set_monospace(GTK_TEXT_VIEW(preview),TRUE);
 GtkWidget *preview_scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(preview_scroll),preview);
 gtk_widget_set_size_request(preview_scroll,-1,150); gtk_box_pack_start(GTK_BOX(box),preview_scroll,TRUE,TRUE,4);
 GtkWidget *filters=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*exact_button=gtk_check_button_new_with_label("Exact name");
 GtkWidget *scope=gtk_entry_new(),*kind=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(scope),"Scope contains…");
 gtk_entry_set_placeholder_text(GTK_ENTRY(kind),"Kind letter"); gtk_entry_set_width_chars(GTK_ENTRY(kind),8);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(exact_button),exact);
 gtk_box_pack_start(GTK_BOX(filters),exact_button,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(filters),scope,TRUE,TRUE,0);
 gtk_box_pack_start(GTK_BOX(filters),kind,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),filters,FALSE,FALSE,4);
 Search search={.n=n,.store=store,.status=status,.preview=preview,.entry=entry,.scope=scope,.kind=kind,.exact=exact_button,.view=view};
 g_signal_connect(scope,"changed",G_CALLBACK(filter),&search); g_signal_connect(kind,"changed",G_CALLBACK(filter),&search);
 g_signal_connect(exact_button,"toggled",G_CALLBACK(filter_toggle),&search);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),"changed",G_CALLBACK(preview_selection),&search); g_signal_connect(entry,"changed",G_CALLBACK(filter),&search);
 g_signal_connect(view,"row-activated",G_CALLBACK(row_activated),dialog);
 filter(GTK_EDITABLE(entry),&search); gtk_widget_show_all(dialog);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
  GtkTreeIter iter;
  if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),NULL,&iter)) {
   gchar *utf8; gulong line;
   gtk_tree_model_get(GTK_TREE_MODEL(store),&iter,PATH,&utf8,LINE,&line,-1);
   gchar *locale=utils_get_locale_from_utf8(utf8);
   GeanyDocument *old=document_get_current();
   GeanyDocument *doc=document_open_file(locale,FALSE,NULL,NULL);
   if(doc && line<=INT_MAX) navqueue_goto_line(old,doc,(gint)line);
   else ui_set_statusbar(TRUE,"Insight: unable to navigate to symbol");
   g_free(locale); g_free(utf8);
  }
 }
 gtk_widget_destroy(dialog); g_object_unref(store);
}
static void find_symbol(GtkMenuItem *item,gpointer data) {
 (void)item; show_search(data,NULL,0);
}
void navigation_find_current(Navigation *n) {
 GeanyDocument *current=document_get_current();
 if(!current) { ui_set_statusbar(TRUE,"Insight: no active document"); return; }
 gchar *text=sci_get_contents(current->editor->sci,sci_get_length(current->editor->sci)+1);
 size_t start,end;
 if(!insight_word_at(text,strlen(text),(size_t)sci_get_current_position(current->editor->sci),&start,&end) || end-start>255) {
  g_free(text); ui_set_statusbar(TRUE,"Insight: no identifier under cursor"); return;
 }
 gchar *name=g_strndup(text+start,end-start); g_free(text); refresh_open(n);
 const InsightSymbol *found[1]; size_t count=insight_index_find(n->index,name,1,found,1);
 if(count==1) {
  gchar *path=utils_get_locale_from_utf8(found[0]->path); gulong line=found[0]->line;
  GeanyDocument *next=document_open_file(path,FALSE,NULL,NULL);
  if(next && line<=INT_MAX) navqueue_goto_line(current,next,(gint)line);
  else ui_set_statusbar(TRUE,"Insight: unable to navigate to symbol");
  g_free(path);
 } else if(count>1) show_search(n,name,1);
 else ui_set_statusbar(TRUE,"Insight: symbol not found: %s",name);
 g_free(name);
}
static void find_current(GtkMenuItem *item,gpointer data) { (void)item; navigation_find_current(data); }
Navigation *navigation_new(GeanyPlugin *plugin) {
 Navigation *n=g_new0(Navigation,1); n->plugin=plugin;
 n->base=insight_index_new(); n->frames=g_ptr_array_new_with_free_func(frame_free);
 n->cache_path=g_build_filename(plugin->geany_data->app->configdir,"plugins","insight","project.idx",NULL);
 if(n->base && g_file_test(n->cache_path,G_FILE_TEST_EXISTS)) {
  char *root=NULL; GError *error=NULL;
  InsightIndex *loaded=insight_cache_load(n->cache_path,&root,&error);
  if(loaded) {
   insight_index_free(n->base); n->base=loaded; n->root=g_strdup(root); free(root);
  } else { ui_set_statusbar(TRUE,"Insight: cached index ignored: %s",error->message); g_error_free(error); }
 }
 n->index=insight_index_clone(n->base);
 if(!n->index) { navigation_free(n); return NULL; }
 n->menu=gtk_menu_item_new_with_label("Insight: Project navigation");
 GtkWidget *submenu=gtk_menu_new(); gtk_menu_item_set_submenu(GTK_MENU_ITEM(n->menu),submenu);
 const char *labels[]={"Index source directory…","Reindex current directory","Find symbol…","Find identifier under cursor","Cancel indexing"};
 GCallback callbacks[]={G_CALLBACK(choose_root),G_CALLBACK(reindex),G_CALLBACK(find_symbol),G_CALLBACK(find_current),G_CALLBACK(cancel)};
 for(guint j=0;j<G_N_ELEMENTS(labels);j++) {
  GtkWidget *item=gtk_menu_item_new_with_label(labels[j]);
  gtk_container_add(GTK_CONTAINER(submenu),item); g_signal_connect(item,"activate",callbacks[j],n);
 }
 gtk_container_add(GTK_CONTAINER(plugin->geany_data->main_widgets->tools_menu),n->menu);
 gtk_widget_show_all(n->menu); return n;
}
void navigation_free(Navigation *n) {
 if(!n) return;
 stop_scan(n);
 if(n->menu) gtk_widget_destroy(n->menu);
 g_ptr_array_free(n->frames,TRUE); insight_index_free(n->index); insight_index_free(n->base);
 g_free(n->root); g_free(n->cache_path); g_free(n);
}
