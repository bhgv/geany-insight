/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "insight-workbench.h"
#include "ui_utils.h"
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "highlighting.h"
#include "sciwrappers.h"
#include "utils.h"
#include "navqueue.h"
#include "tagmanager/tm_workspace.h"
#include <string.h>
#include <gio/gio.h>
#include "../insight/core/index.h"
#include "../insight/core/occurrences.h"
#include <glib/gstdio.h>
#include <sys/stat.h>
#include "../insight/desktop/index_cache.h"
#include <stdlib.h>

typedef struct { gchar *path; GDir *dir; } ScanFrame;
static void frame_free(gpointer data) { ScanFrame *p=data; if(p->dir) g_dir_close(p->dir); g_free(p->path); g_free(p); }
typedef struct { gchar *path; guint id; gulong line; } Location;
static void location_free(gpointer data) { Location *p=data; g_free(p->path); g_free(p); }
typedef struct {
 GtkWidget *pane,*box,*title,*follow,*choices,*browser,*symbol_entry,*symbol_view,*outline,*file_entry,*file_view;
 GtkListStore *file_store,*matches;
 GtkWidget *usage_entry,*usage_status,*usage_buffer,*usage_view;
 GPtrArray *usage_paths; gchar *usage_name; guint usage_idle,usage_position,usage_shown,usage_skipped;
 GtkListStore *symbols;
 ScintillaObject *preview;
 guint refresh,source_id;
 InsightIndex *base,*index;
 GPtrArray *frames,*files; gchar *root,*cache_path; guint scan,parsed,skipped;
 gchar *source_path; gulong source_line;
 GPtrArray *history,*definitions; gint history_position; gboolean populating;
} Workbench;
static Workbench wb;
static void open_context(GtkButton *,gpointer);
static void browser_filter(GtkEditable *,gpointer);
static void files_filter(GtkEditable *,gpointer);
static void stop_usage(void);
static void render_source(const char *text,GeanyFiletype *filetype,gulong line) {
 highlighting_set_styles(wb.preview,filetype ? filetype : filetypes[GEANY_FILETYPES_NONE]);
 sci_set_readonly(wb.preview,FALSE); sci_set_text(wb.preview,text);
 SSM(wb.preview,SCI_COLOURISE,0,-1);
 sci_set_line_numbers(wb.preview,TRUE);
 SSM(wb.preview,SCI_SETCARETLINEVISIBLE,TRUE,0);
 SSM(wb.preview,SCI_SETCARETLINEBACK,0xe9f0ff,0);
 SSM(wb.preview,SCI_EMPTYUNDOBUFFER,0,0); sci_set_readonly(wb.preview,TRUE);
 gint target=(gint)MIN(line ? line-1 : 0,(gulong)MAX(0,sci_get_line_count(wb.preview)-1));
 SSM(wb.preview,SCI_GOTOLINE,target,0);
 SSM(wb.preview,SCI_VERTICALCENTRECARET,0,0);
}
static gboolean import_tags(InsightIndex *index,TMSourceFile *file,const char *path) {
 if(!file || !path || !file->tags_array) return TRUE;
 guint count=file->tags_array->len,used=0;
 if(count>250000 || insight_index_count(index)>250000) return FALSE;
 InsightSymbol *symbols=g_new0(InsightSymbol,count); gchar (*kinds)[2]=g_malloc0((gsize)count*2);
 for(guint i=0;i<count;i++) {
  TMTag *tag=g_ptr_array_index(file->tags_array,i);
  if(!tag->name || !*tag->name || !tag->line) continue;
  kinds[used][0]=tag->kind_letter;
  symbols[used]=(InsightSymbol){tag->name,path,tag->scope,kinds[used],tag->line}; used++;
 }
 gboolean ok=insight_index_update(index,path,symbols,used);
 g_free(kinds); g_free(symbols); return ok;
}
static void refresh_index(void) {
 InsightIndex *next=insight_index_clone(wb.base); if(!next) return;
 for(guint i=0;i<documents_array->len;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);
  if(doc->is_valid && doc->file_name && !import_tags(next,doc->tm_file,doc->file_name)) { insight_index_free(next); return; }
 }
 insight_index_free(wb.index); wb.index=next;
}
static void stop_scan(void) {
 if(wb.scan) { g_source_remove(wb.scan); wb.scan=0; }
 if(wb.frames) g_ptr_array_set_size(wb.frames,0);
}
static void save_index_cache(void) {
 if(!wb.root || !wb.cache_path) return;
 gchar *directory=g_path_get_dirname(wb.cache_path); GError *error=NULL;
 if(!g_mkdir_with_parents(directory,0700) && !insight_cache_save(wb.cache_path,wb.base,wb.root,&error)) { ui_set_statusbar(TRUE,"Workspace cache not saved: %s",error->message); g_clear_error(&error); }
 g_free(directory);
}
static void restore_index_cache(void) {
 if(!g_file_test(wb.cache_path,G_FILE_TEST_EXISTS)) return;
 char *root=NULL; GError *error=NULL; InsightIndex *index=insight_cache_load(wb.cache_path,&root,&error);
 if(!index) { ui_set_statusbar(TRUE,"Workspace cache ignored: %s",error ? error->message : "invalid snapshot"); g_clear_error(&error); return; }
 insight_index_free(wb.base); wb.base=index; wb.root=g_strdup(root); free(root);
 size_t count=insight_index_file_count(index);
 if(count!=SIZE_MAX && count<=10000) {
  const char **paths=g_new(const char *,count); insight_index_files(index,"",0,paths,count);
  for(size_t i=0;i<count;i++) g_ptr_array_add(wb.files,g_strdup(paths[i])); g_free(paths);
 }
}
static gboolean scan_step(gpointer data) {
 (void)data;
 if(!wb.frames->len || wb.parsed>=10000) {
  wb.scan=0; g_ptr_array_set_size(wb.frames,0); refresh_index();
  if(wb.parsed<10000 && !wb.skipped) save_index_cache();
  ui_set_statusbar(TRUE,"Workspace: %u files, %zu symbols, %u skipped%s",wb.parsed,insight_index_count(wb.index),wb.skipped,wb.parsed>=10000 ? "; file limit reached" : "");
  browser_filter(NULL,NULL); files_filter(NULL,NULL); insight_workbench_refresh(); return G_SOURCE_REMOVE;
 }
 ScanFrame *frame=g_ptr_array_index(wb.frames,wb.frames->len-1); const char *name=g_dir_read_name(frame->dir);
 if(!name) { g_ptr_array_remove_index(wb.frames,wb.frames->len-1); return G_SOURCE_CONTINUE; }
 if(name[0]=='.' || !strcmp(name,"node_modules") || !strcmp(name,"__pycache__")) return G_SOURCE_CONTINUE;
 gchar *path=g_build_filename(frame->path,name,NULL); GStatBuf st;
 if(g_lstat(path,&st)) { wb.skipped++; g_free(path); return G_SOURCE_CONTINUE; }
 if(S_ISDIR(st.st_mode)) {
  GDir *dir=wb.frames->len<64 ? g_dir_open(path,0,NULL) : NULL;
  if(dir) { ScanFrame *next=g_new0(ScanFrame,1); next->path=path; next->dir=dir; g_ptr_array_add(wb.frames,next); }
  else { wb.skipped++; g_free(path); }
  return G_SOURCE_CONTINUE;
 }
 if(!S_ISREG(st.st_mode) || st.st_size>2*1024*1024) { wb.skipped++; g_free(path); return G_SOURCE_CONTINUE; }
 gchar *utf8=utils_get_utf8_from_locale(path); GeanyFiletype *ft=filetypes_detect_from_file(utf8);
 if(ft && ft->lang>=0) {
  TMSourceFile *file=tm_source_file_new(path,ft->name);
  if(file) {
   file->lang=ft->lang;
   if(tm_source_file_parse(file,NULL,0,FALSE) && import_tags(wb.base,file,utf8)) { wb.parsed++; g_ptr_array_add(wb.files,g_strdup(utf8)); }
   else wb.skipped++;
   tm_source_file_free(file);
  } else wb.skipped++;
 }
 g_free(utf8); g_free(path); return G_SOURCE_CONTINUE;
}
static gboolean start_scan(const char *root) {
 gchar *owned=g_strdup(root); GDir *dir=g_dir_open(owned,0,NULL); InsightIndex *next=insight_index_new();
 if(!dir || !next) { if(dir) g_dir_close(dir); insight_index_free(next); g_free(owned); return FALSE; }
 stop_scan(); insight_index_free(wb.base); wb.base=next; g_free(wb.root); wb.root=g_strdup(owned);
 g_ptr_array_set_size(wb.files,0); ScanFrame *frame=g_new0(ScanFrame,1); frame->path=owned; frame->dir=dir; g_ptr_array_add(wb.frames,frame);
 wb.parsed=wb.skipped=0; wb.scan=g_idle_add(scan_step,NULL); ui_set_statusbar(TRUE,"Workspace: indexing source folder…"); return TRUE;
}
static void choose_root(GtkButton *button,gpointer data) {
 (void)button; (void)data;
 GtkWidget *dialog=gtk_file_chooser_dialog_new("Open source workspace",GTK_WINDOW(main_widgets.window),GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)); if(!start_scan(path)) ui_set_statusbar(TRUE,"Cannot index source folder"); g_free(path); }
 gtk_widget_destroy(dialog);
}
static void cancel_scan(GtkButton *button,gpointer data) { (void)button; (void)data; stop_scan(); refresh_index(); ui_set_statusbar(TRUE,"Workspace scan cancelled; partial index retained"); }
static const InsightSymbol *word_definition(GeanyDocument *doc,const char *word) {
 if(!wb.definitions || !doc) return NULL;
 refresh_index();
 wb.populating=TRUE; g_ptr_array_set_size(wb.definitions,0); gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(wb.choices));
 const InsightSymbol *found[256],*chosen=NULL;
 size_t count=word && *word ? insight_index_find(wb.index,word,TRUE,found,256) : 0; gint selected=0;
 for(size_t i=0;i<count && i<256;i++) {
  const InsightSymbol *tag=found[i]; gboolean duplicate=FALSE;
  for(guint j=0;j<wb.definitions->len;j++) { Location *old=g_ptr_array_index(wb.definitions,j); if(old->line==tag->line && !g_strcmp0(old->path,tag->path)) { duplicate=TRUE; break; } }
  if(duplicate) continue;
  Location *location=g_new0(Location,1); location->path=g_strdup(tag->path); location->line=tag->line;
  gchar *label=g_strdup_printf("%s • %s — %s:%lu",word,tag->scope,tag->path,tag->line);
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.choices),label); g_free(label);
  if(!chosen || !g_strcmp0(tag->path,doc->file_name)) { chosen=tag; selected=(gint)wb.definitions->len; }
  g_ptr_array_add(wb.definitions,location);
 }
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.choices),wb.definitions->len ? selected : -1);
 wb.populating=FALSE; return chosen;
}
static char *read_disk_source(const char *utf8) {
 gchar *native=utils_get_locale_from_utf8(utf8); GFile *file=g_file_new_for_path(native); g_free(native);
 GFileInputStream *stream=g_file_read(file,NULL,NULL); g_object_unref(file);
 if(!stream) return NULL;
 GString *text=g_string_sized_new(16384); char chunk[16384]; gboolean ok=TRUE;
 for(;;) {
  gssize got=g_input_stream_read(G_INPUT_STREAM(stream),chunk,sizeof chunk,NULL,NULL);
  if(got<0 || (got>0 && text->len+(gsize)got>2u*1024u*1024u)) { ok=FALSE; break; }
  if(!got) break;
  g_string_append_len(text,chunk,got);
 }
 g_object_unref(stream);
 if(!ok || !g_utf8_validate(text->str,(gssize)text->len,NULL) || memchr(text->str,0,text->len)) { g_string_free(text,TRUE); return NULL; }
 return g_string_free(text,FALSE);
}
static void remember_location(const char *path,guint id,gulong line) {
 if(!wb.history || !line) return;
 if(wb.history_position>=0 && wb.history_position<(gint)wb.history->len) {
  Location *old=g_ptr_array_index(wb.history,(guint)wb.history_position);
  if(old->id==id && old->line==line && !g_strcmp0(old->path,path)) return;
 }
 while(wb.history->len && (gint)wb.history->len>wb.history_position+1) g_ptr_array_remove_index(wb.history,wb.history->len-1);
 Location *next=g_new0(Location,1); next->path=g_strdup(path); next->id=id; next->line=line;
 g_ptr_array_add(wb.history,next);
 if(wb.history->len>64) g_ptr_array_remove_index(wb.history,0);
 wb.history_position=(gint)wb.history->len-1;
}
static gboolean show_location(const char *path,guint id,gulong line,gboolean remember) {
 if(remember && wb.history && !wb.history->len && wb.source_line) remember_location(wb.source_path,wb.source_id,wb.source_line);
 GeanyDocument *doc=id ? document_find_by_id(id) : (path ? document_find_by_filename(path) : NULL);
 char *text=NULL; GeanyFiletype *ft=NULL;
 if(doc) {
  gint length=sci_get_length(doc->editor->sci);
  if(length<=2*1024*1024) text=sci_get_contents(doc->editor->sci,length+1);
  if(text && (!g_utf8_validate(text,length,NULL) || memchr(text,0,(gsize)length))) g_clear_pointer(&text,g_free);
  ft=doc->file_type;
 } else if(path) { text=read_disk_source(path); ft=filetypes_detect_from_file(path); }
 if(!text) {
  render_source("",NULL,1); gtk_label_set_text(GTK_LABEL(wb.title),"Context unavailable: requires a UTF-8 source file up to 2 MiB");
  g_clear_pointer(&wb.source_path,g_free); wb.source_id=0; wb.source_line=0; return FALSE;
 }
 gchar *owned_path=g_strdup(path ? path : (doc ? doc->file_name : NULL));
 render_source(text,ft,line); g_free(text);
 g_free(wb.source_path); wb.source_path=owned_path; wb.source_id=doc ? doc->id : 0; wb.source_line=line;
 gchar *label=g_strdup_printf("Context — %s:%lu",owned_path ? owned_path : "untitled",line);
 gtk_label_set_text(GTK_LABEL(wb.title),label); g_free(label);
 if(remember) remember_location(wb.source_path,wb.source_id,line);
 return TRUE;
}
static void definition_changed(GtkComboBox *combo,gpointer data) {
 (void)data; if(wb.populating || !wb.definitions) return;
 gint active=gtk_combo_box_get_active(combo);
 if(active<0 || active>=(gint)wb.definitions->len) return;
 Location *location=g_ptr_array_index(wb.definitions,(guint)active);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
 show_location(location->path,location->id,location->line,TRUE);
}
enum { SYMBOL_NAME,SYMBOL_SCOPE,SYMBOL_PATH,SYMBOL_LINE,SYMBOL_KIND,SYMBOL_COLUMNS };
static void browser_filter(GtkEditable *entry,gpointer data) {
 (void)entry; (void)data; if(!wb.symbols) return;
 refresh_index(); const InsightSymbol *found[500];
 GeanyDocument *doc=document_get_current(); gboolean outline=wb.outline && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.outline));
 const char *path=outline && doc && doc->file_name ? doc->file_name : NULL;
 size_t count=outline && !path ? 0 : insight_index_query(wb.index,gtk_entry_get_text(GTK_ENTRY(wb.symbol_entry)),FALSE,NULL,NULL,path,0,found,500);
 gtk_list_store_clear(wb.symbols);
 for(size_t i=0;i<count && i<500;i++) {
  GtkTreeIter iter; gtk_list_store_append(wb.symbols,&iter);
  gtk_list_store_set(wb.symbols,&iter,SYMBOL_NAME,found[i]->name,SYMBOL_SCOPE,found[i]->scope,SYMBOL_PATH,found[i]->path,SYMBOL_LINE,(gulong)found[i]->line,SYMBOL_KIND,found[i]->kind,-1);
 }
}
static void browser_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data; if(!wb.preview) return;
 GtkTreeIter iter; GtkTreeModel *model;
 if(!gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 gchar *path; gulong line; gtk_tree_model_get(model,&iter,SYMBOL_PATH,&path,SYMBOL_LINE,&line,-1);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); show_location(path,0,line,TRUE); g_free(path);
}
static void browser_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)view; (void)path; (void)column; (void)data; open_context(NULL,NULL);
}
static void outline_changed(GtkToggleButton *button,gpointer data) { (void)button; (void)data; browser_filter(NULL,NULL); }
static void files_filter(GtkEditable *entry,gpointer data) {
 (void)entry; (void)data; if(!wb.file_store) return;
 gchar *needle=g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(wb.file_entry)),-1);
 GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal); gtk_list_store_clear(wb.file_store); guint shown=0;
 for(guint i=0;i<wb.files->len+documents_array->len && shown<500;i++) {
  const gchar *path=NULL;
  if(i<wb.files->len) path=g_ptr_array_index(wb.files,i);
  else { GeanyDocument *doc=g_ptr_array_index(documents_array,i-wb.files->len); if(doc->is_valid) path=doc->file_name; }
  if(!path || !g_utf8_validate(path,-1,NULL) || g_hash_table_contains(seen,path)) continue;
  g_hash_table_add(seen,(gpointer)path); gchar *folded=g_utf8_casefold(path,-1);
  if(strstr(folded,needle)) { GtkTreeIter iter; gchar *name=g_path_get_basename(path); gtk_list_store_append(wb.file_store,&iter); gtk_list_store_set(wb.file_store,&iter,0,name,1,path,-1); g_free(name); shown++; }
  g_free(folded);
 }
 g_hash_table_destroy(seen); g_free(needle);
}
static void file_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data; GtkTreeIter iter; GtkTreeModel *model;
 if(!wb.preview || !gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 gchar *path; gtk_tree_model_get(model,&iter,1,&path,-1); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); show_location(path,0,1,TRUE); g_free(path);
}
static void create_files_browser(void) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4); wb.file_entry=gtk_search_entry_new();
 gtk_entry_set_placeholder_text(GTK_ENTRY(wb.file_entry),"Find workspace file…");
 wb.file_store=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_STRING); wb.file_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.file_store));
 for(int i=0;i<2;i++) gtk_tree_view_append_column(GTK_TREE_VIEW(wb.file_view),gtk_tree_view_column_new_with_attributes(i ? "Path" : "File",gtk_cell_renderer_text_new(),"text",i,NULL));
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.file_view);
 gtk_box_pack_start(GTK_BOX(box),wb.file_entry,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 gtk_notebook_append_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),box,gtk_label_new("Project files"));
 g_signal_connect(wb.file_entry,"changed",G_CALLBACK(files_filter),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.file_view)),"changed",G_CALLBACK(file_selected),NULL);
 g_signal_connect(wb.file_view,"row-activated",G_CALLBACK(browser_activated),NULL);
 gtk_widget_show_all(box); files_filter(NULL,NULL);
}
enum { USAGE_DISPLAY,USAGE_PATH,USAGE_LINE,USAGE_START,USAGE_END,USAGE_ID,USAGE_DIGEST,USAGE_COLUMNS };
static void stop_usage(void) {
 if(wb.usage_idle) { g_source_remove(wb.usage_idle); wb.usage_idle=0; }
 if(wb.usage_paths) g_ptr_array_set_size(wb.usage_paths,0);
 g_clear_pointer(&wb.usage_name,g_free);
}
static gboolean usage_step(gpointer data) {
 (void)data;
 if(wb.usage_position>=wb.usage_paths->len || wb.usage_shown>=2000) {
  wb.usage_idle=0;
  gchar *label=g_strdup_printf("%u results • %u files scanned • %u skipped%s",wb.usage_shown,wb.usage_position,wb.usage_skipped,wb.usage_shown>=2000 ? " • result limit reached" : "");
  gtk_label_set_text(GTK_LABEL(wb.usage_status),label); g_free(label); return G_SOURCE_REMOVE;
 }
 Location *location=g_ptr_array_index(wb.usage_paths,wb.usage_position++);
 GeanyDocument *doc=location->id ? document_find_by_id(location->id) : document_find_by_filename(location->path);
 char *text=NULL; guint id=doc ? doc->id : 0;
 if(doc) { gint length=sci_get_length(doc->editor->sci); if(length<=2*1024*1024) text=sci_get_contents(doc->editor->sci,length+1); }
 else if(location->path) text=read_disk_source(location->path);
 InsightOccurrence *found=g_new(InsightOccurrence,2000-wb.usage_shown); size_t total=0;
 if(text && insight_occurrences_find(text,strlen(text),wb.usage_name,strlen(wb.usage_name),0,found,2000-wb.usage_shown,&total)) {
  gchar *digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);
  for(size_t i=0;i<total && wb.usage_shown<2000;i++) {
   GtkTreeIter iter; gtk_list_store_append(wb.matches,&iter);
   gtk_list_store_set(wb.matches,&iter,USAGE_DISPLAY,location->path ? location->path : "untitled",USAGE_PATH,location->path,USAGE_LINE,found[i].line,USAGE_START,(gulong)found[i].start,USAGE_END,(gulong)found[i].end,USAGE_ID,id,USAGE_DIGEST,digest,-1); wb.usage_shown++;
  }
  g_free(digest);
 } else wb.usage_skipped++;
 g_free(found); g_free(text);
 gchar *label=g_strdup_printf("Searching… %u results • %u/%u files",wb.usage_shown,wb.usage_position,wb.usage_paths->len);
 gtk_label_set_text(GTK_LABEL(wb.usage_status),label); g_free(label); return G_SOURCE_CONTINUE;
}
static void usage_start(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; stop_usage();
 const char *query=gtk_entry_get_text(GTK_ENTRY(wb.usage_entry)); size_t total;
 if(!insight_occurrences_find("",0,query,strlen(query),0,NULL,0,&total)) { gtk_label_set_text(GTK_LABEL(wb.usage_status),"Enter one UTF-8 identifier"); return; }
 gtk_list_store_clear(wb.matches); wb.usage_name=g_strdup(query); wb.usage_position=wb.usage_shown=wb.usage_skipped=0;
 GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);
 if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_buffer))) {
  for(guint i=0;i<wb.files->len && wb.usage_paths->len<10000;i++) {
   const char *path=g_ptr_array_index(wb.files,i); if(g_hash_table_contains(seen,path)) continue;
   Location *next=g_new0(Location,1); next->path=g_strdup(path); g_ptr_array_add(wb.usage_paths,next); g_hash_table_add(seen,next->path);
  }
 }
 for(guint i=0;i<documents_array->len && wb.usage_paths->len<10000;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);
  if(!doc->is_valid || (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_buffer)) && doc!=document_get_current())) continue;
  if(doc->file_name && g_hash_table_contains(seen,doc->file_name)) continue;
  Location *next=g_new0(Location,1); next->path=g_strdup(doc->file_name); next->id=doc->id; g_ptr_array_add(wb.usage_paths,next);
  if(next->path) g_hash_table_add(seen,next->path);
 }
 g_hash_table_destroy(seen); wb.usage_idle=g_idle_add(usage_step,NULL);
}
static void usage_cancel(GtkWidget *widget,gpointer data) { (void)widget; (void)data; stop_usage(); gtk_label_set_text(GTK_LABEL(wb.usage_status),"Search cancelled; partial results retained"); }
static void usage_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data; GtkTreeIter iter; GtkTreeModel *model;
 if(!gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 gchar *path,*digest; gulong line,start,end; guint id;
 gtk_tree_model_get(model,&iter,USAGE_PATH,&path,USAGE_LINE,&line,USAGE_START,&start,USAGE_END,&end,USAGE_ID,&id,USAGE_DIGEST,&digest,-1);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
 if(show_location(path,id,line,TRUE)) {
  gchar *text=sci_get_contents(wb.preview,sci_get_length(wb.preview)+1),*actual=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);
  if(!g_strcmp0(actual,digest)) { SSM(wb.preview,SCI_SETSEL,start,end); SSM(wb.preview,SCI_VERTICALCENTRECARET,0,0); }
  else ui_set_statusbar(TRUE,"Source changed after search; run the usage search again");
  g_free(actual); g_free(text);
 }
 g_free(path); g_free(digest);
}
static void create_usage_browser(void) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4),*controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);
 wb.usage_entry=gtk_search_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.usage_entry),"Identifier usages (lexical)…");
 wb.usage_status=gtk_label_new("Comments and strings included; no semantic scope resolution"); gtk_label_set_line_wrap(GTK_LABEL(wb.usage_status),TRUE);
 wb.usage_buffer=gtk_check_button_new_with_label("Current buffer only");
 GtkWidget *search=gtk_button_new_with_label("Search"),*cancel=gtk_button_new_with_label("Stop");
 gtk_box_pack_start(GTK_BOX(controls),search,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(controls),cancel,FALSE,FALSE,0);
 wb.matches=gtk_list_store_new(USAGE_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_UINT,G_TYPE_STRING);
 wb.usage_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.matches));
 gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view),gtk_tree_view_column_new_with_attributes("File",gtk_cell_renderer_text_new(),"text",USAGE_DISPLAY,NULL));
 gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view),gtk_tree_view_column_new_with_attributes("Line",gtk_cell_renderer_text_new(),"text",USAGE_LINE,NULL));
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.usage_view);
 gtk_box_pack_start(GTK_BOX(box),wb.usage_entry,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.usage_buffer,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),controls,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.usage_status,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 gtk_notebook_append_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),box,gtk_label_new("Usages"));
 g_signal_connect(search,"clicked",G_CALLBACK(usage_start),NULL); g_signal_connect(cancel,"clicked",G_CALLBACK(usage_cancel),NULL); g_signal_connect(wb.usage_entry,"activate",G_CALLBACK(usage_start),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.usage_view)),"changed",G_CALLBACK(usage_selected),NULL); g_signal_connect(wb.usage_view,"row-activated",G_CALLBACK(browser_activated),NULL);
 gtk_widget_show_all(box);
}
static void create_browser(void) {
 wb.browser=gtk_box_new(GTK_ORIENTATION_VERTICAL,4); wb.symbol_entry=gtk_search_entry_new();
 gtk_entry_set_placeholder_text(GTK_ENTRY(wb.symbol_entry),"Find project symbol…");
 wb.symbols=gtk_list_store_new(SYMBOL_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING);
 wb.symbol_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.symbols));
 const char *titles[]={"Symbol","Scope","File","Line","Kind"};
 for(int i=0;i<SYMBOL_COLUMNS;i++) {
  GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(titles[i],gtk_cell_renderer_text_new(),"text",i,NULL);
  gtk_tree_view_column_set_sort_column_id(column,i); gtk_tree_view_append_column(GTK_TREE_VIEW(wb.symbol_view),column);
 }
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.symbol_view);
 wb.outline=gtk_check_button_new_with_label("Current file outline");
 g_signal_connect(wb.outline,"toggled",G_CALLBACK(outline_changed),NULL);
 gtk_box_pack_start(GTK_BOX(wb.browser),wb.outline,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(wb.browser),wb.symbol_entry,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(wb.browser),scroll,TRUE,TRUE,0);
 gtk_notebook_append_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),wb.browser,gtk_label_new("Workspace"));
 g_signal_connect(wb.symbol_entry,"changed",G_CALLBACK(browser_filter),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.symbol_view)),"changed",G_CALLBACK(browser_selected),NULL);
 g_signal_connect(wb.symbol_view,"row-activated",G_CALLBACK(browser_activated),NULL);
 gtk_widget_show_all(wb.browser); browser_filter(NULL,NULL);
}
static gboolean refresh_now(gpointer data) {
 (void)data; wb.refresh=0;
 if(wb.outline && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.outline))) browser_filter(NULL,NULL);
 if(!wb.preview || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow))) return G_SOURCE_REMOVE;
 GeanyDocument *doc=document_get_current();
 if(!doc) { render_source("",NULL,1); return G_SOURCE_REMOVE; }
 gchar word[1025]={0}; editor_find_current_word_sciwc(doc->editor,sci_get_current_position(doc->editor->sci),word,sizeof word);
 const InsightSymbol *definition=word_definition(doc,word);
 if(definition) {
  gchar *path=g_strdup(definition->path);
  gulong line=definition->line; show_location(path,0,line,FALSE); g_free(path);
 } else show_location(doc->file_name,doc->id,(gulong)sci_get_current_line(doc->editor->sci)+1,FALSE);
 return G_SOURCE_REMOVE;
}
static void history_move(GtkButton *button,gpointer data) {
 (void)button; gint next=wb.history_position+GPOINTER_TO_INT(data);
 if(!wb.history || next<0 || next>=(gint)wb.history->len) return;
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
 Location *location=g_ptr_array_index(wb.history,(guint)next);
 if(show_location(location->path,location->id,location->line,FALSE)) wb.history_position=next;
}
static void open_context(GtkButton *button,gpointer data) {
 (void)button; (void)data;
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),TRUE);
 guint id=wb.source_id; gchar *path=g_strdup(wb.source_path); gulong line=wb.source_line;
 GeanyDocument *old=document_get_current(),*doc=id ? document_find_by_id(id) : NULL;
 if(!doc && path) { gchar *native=utils_get_locale_from_utf8(path); doc=document_open_file(native,FALSE,NULL,NULL); g_free(native); }
 if(doc && line && line<=(gulong)sci_get_line_count(doc->editor->sci)) {
  navqueue_goto_line(old,doc,(gint)line); SSM(doc->editor->sci,SCI_VERTICALCENTRECARET,0,0); document_grab_focus(doc);
 } else ui_set_statusbar(TRUE,"Context source is unavailable or changed; refresh the index");
 g_free(path);
}
static gboolean preview_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget; (void)data;
 if(event->keyval==GDK_KEY_Return && !(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK))) { open_context(NULL,NULL); return TRUE; }
 return FALSE;
}
static gboolean preview_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)widget; (void)data;
 if(event->button!=1 || !(event->state&GDK_CONTROL_MASK)) return FALSE;
 gint position=(gint)SSM(wb.preview,SCI_POSITIONFROMPOINTCLOSE,(uptr_t)event->x,(sptr_t)event->y);
 if(position<0) return FALSE;
 gint start=(gint)SSM(wb.preview,SCI_WORDSTARTPOSITION,position,TRUE),end=(gint)SSM(wb.preview,SCI_WORDENDPOSITION,position,TRUE);
 if(end<=start || end-start>1024) return TRUE;
 char word[1025]; for(gint i=start;i<end;i++) word[i-start]=(char)sci_get_char_at(wb.preview,i); word[end-start]=0;
 GeanyDocument *source=wb.source_id ? document_find_by_id(wb.source_id) : document_get_current();
 const InsightSymbol *definition=word_definition(source,word);
 if(definition) {
  gchar *path=g_strdup(definition->path); gulong line=definition->line;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
  show_location(path,0,line,TRUE); g_free(path);
 } else ui_set_statusbar(TRUE,"Definition not found: %s",word);
 return TRUE;
}
void insight_workbench_refresh(void) {
 if(!wb.preview) return;
 if(wb.refresh) g_source_remove(wb.refresh);
 wb.refresh=g_timeout_add(60,refresh_now,NULL);
}
static void follow_changed(GtkToggleButton *button,gpointer data) {
 (void)data;
 if(gtk_toggle_button_get_active(button)) insight_workbench_refresh();
 else if(wb.refresh) { g_source_remove(wb.refresh); wb.refresh=0; }
}
void insight_workbench_init(void) {
 GtkWidget *existing=ui_lookup_widget(main_widgets.window,"vpaned1");
 GtkWidget *parent=gtk_widget_get_parent(existing); gint position=0;
 gtk_container_child_get(GTK_CONTAINER(parent),existing,"position",&position,NULL);
 wb.usage_paths=g_ptr_array_new_with_free_func(location_free);
 wb.frames=g_ptr_array_new_with_free_func(frame_free); wb.files=g_ptr_array_new_with_free_func(g_free);
 wb.base=insight_index_new(); wb.index=insight_index_new();
 wb.cache_path=g_build_filename(app->configdir,"insight","workspace.idx",NULL); restore_index_cache();
 wb.definitions=g_ptr_array_new_with_free_func(location_free);
 wb.history=g_ptr_array_new_with_free_func(location_free); wb.history_position=-1;
 create_browser(); create_files_browser(); create_usage_browser();
 wb.pane=gtk_paned_new(GTK_ORIENTATION_VERTICAL);
 wb.box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
 wb.title=gtk_label_new("Context — integrated source navigation");
 gtk_label_set_xalign(GTK_LABEL(wb.title),0.0f);
 GtkWidget *header=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*open=gtk_button_new_with_label("Open source");
 GtkWidget *folder=gtk_button_new_with_label("Source folder…"),*cancel=gtk_button_new_with_label("Cancel scan");
 g_signal_connect(folder,"clicked",G_CALLBACK(choose_root),NULL); g_signal_connect(cancel,"clicked",G_CALLBACK(cancel_scan),NULL);
 GtkWidget *project_controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);
 gtk_box_pack_start(GTK_BOX(project_controls),folder,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(project_controls),cancel,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(wb.browser),project_controls,FALSE,FALSE,0); gtk_box_reorder_child(GTK_BOX(wb.browser),project_controls,0); gtk_widget_show_all(project_controls);
 GtkWidget *back=gtk_button_new_with_label("←"),*forward=gtk_button_new_with_label("→");
 g_signal_connect(back,"clicked",G_CALLBACK(history_move),GINT_TO_POINTER(-1));
 g_signal_connect(forward,"clicked",G_CALLBACK(history_move),GINT_TO_POINTER(1));
 gtk_box_pack_start(GTK_BOX(header),back,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(header),forward,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(header),wb.title,TRUE,TRUE,4); gtk_box_pack_end(GTK_BOX(header),open,FALSE,FALSE,4);
 wb.follow=gtk_check_button_new_with_label("Follow cursor");
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),TRUE);
 g_signal_connect(wb.follow,"toggled",G_CALLBACK(follow_changed),NULL);
 gtk_box_pack_end(GTK_BOX(header),wb.follow,FALSE,FALSE,2);
 g_signal_connect(open,"clicked",G_CALLBACK(open_context),NULL);
 gtk_box_pack_start(GTK_BOX(wb.box),header,FALSE,FALSE,2);
 wb.choices=gtk_combo_box_text_new();
 g_signal_connect(wb.choices,"changed",G_CALLBACK(definition_changed),NULL);
 gtk_box_pack_start(GTK_BOX(wb.box),wb.choices,FALSE,FALSE,0);
 wb.preview=SCINTILLA(scintilla_new());
 sci_set_codepage(wb.preview,SC_CP_UTF8);
 SSM(wb.preview,SCI_SETUNDOCOLLECTION,0,0);
 SSM(wb.preview,SCI_SETMARGINWIDTHN,1,0); SSM(wb.preview,SCI_SETMARGINWIDTHN,2,0);
 SSM(wb.preview,SCI_SETWRAPMODE,SC_WRAP_NONE,0);
 gtk_widget_set_direction(GTK_WIDGET(wb.preview),GTK_TEXT_DIR_LTR);
 g_signal_connect(wb.preview,"button-press-event",G_CALLBACK(preview_button),NULL);
 g_signal_connect(wb.preview,"key-press-event",G_CALLBACK(preview_key),NULL);
 gtk_widget_set_tooltip_text(GTK_WIDGET(wb.preview),"Ctrl+click an identifier to follow its definition");
 gtk_box_pack_start(GTK_BOX(wb.box),GTK_WIDGET(wb.preview),TRUE,TRUE,0);
 gtk_widget_set_size_request(wb.box,-1,170);
 g_object_ref(existing); gtk_container_remove(GTK_CONTAINER(parent),existing);
 gtk_paned_pack1(GTK_PANED(wb.pane),existing,TRUE,FALSE); g_object_unref(existing);
 gtk_paned_pack2(GTK_PANED(wb.pane),wb.box,FALSE,FALSE);
 gtk_box_pack_start(GTK_BOX(parent),wb.pane,TRUE,TRUE,0);
 gtk_box_reorder_child(GTK_BOX(parent),wb.pane,position);
 gtk_paned_set_position(GTK_PANED(wb.pane),430);
 gtk_widget_show_all(wb.pane);
 insight_workbench_refresh();
}
void insight_workbench_finalize(void) {
 if(wb.refresh) g_source_remove(wb.refresh);
 stop_usage(); g_ptr_array_free(wb.usage_paths,TRUE);
 stop_scan(); g_ptr_array_free(wb.frames,TRUE); g_ptr_array_free(wb.files,TRUE); g_free(wb.root); g_free(wb.cache_path);
 g_free(wb.source_path);
 insight_index_free(wb.base); insight_index_free(wb.index);
 if(wb.symbols) g_object_unref(wb.symbols);
 if(wb.file_store) g_object_unref(wb.file_store);
 if(wb.matches) g_object_unref(wb.matches);
 if(wb.history) g_ptr_array_free(wb.history,TRUE);
 if(wb.definitions) g_ptr_array_free(wb.definitions,TRUE);
 memset(&wb,0,sizeof wb);
}
