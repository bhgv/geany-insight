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
#include "encodingsprivate.h"
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
#include <stdint.h>
#include "insight-index.h"
#include "geanyobject.h"
#include "keybindings.h"
#include "../insight/core/context.h"
#include "../insight/core/runtime.h"
#include "../insight/desktop/extensions.h"

typedef struct { gchar *path; GDir *dir; } ScanFrame;
static void frame_free(gpointer data) { ScanFrame *p=data; if(p->dir) g_dir_close(p->dir); g_free(p->path); g_free(p); }
typedef struct { gchar *path; guint id; gulong line; } Location;
static void location_free(gpointer data) { Location *p=data; g_free(p->path); g_free(p); }
typedef struct {
 GtkWidget *pane,*box,*title,*follow,*choices,*browser,*symbol_entry,*symbol_view,*outline,*file_entry,*file_view;
 GtkWidget *back,*forward,*open,*wrap,*project_notebook,*right_pane;
 GtkWidget *local_entry,*local_view,*local_label,*tree_view;
 GtkListStore *local_symbols; GtkTreeStore *project_tree;
 guint local_doc_id;
 GeanyFiletype *preview_filetype;
 GtkListStore *file_store,*matches;
 GtkWidget *usage_entry,*usage_status,*usage_buffer,*usage_view;
 GPtrArray *usage_paths; gchar *usage_name; guint usage_idle,usage_position,usage_shown,usage_skipped;
 GtkListStore *symbols;
 ScintillaObject *preview;
 guint refresh,source_id; gboolean context_visible,index_dirty,scan_failed;
 gulong event_handlers[4];
 gchar *prefs_path;
 InsightExtensions *extensions; gboolean lua_running;
 GtkWidget *lua_menu,*lua_commands,*lua_item,*palette_item;
 GtkAccelGroup *lua_accels,*navigation_accels;
 GtkCssProvider *css; GtkWidget *context_toggle;
 InsightIndex *base,*index;
 GPtrArray *frames,*files; gchar *root,*cache_path; guint scan,parsed,skipped;
 gchar *source_path,*source_digest; gulong source_line;
 GPtrArray *history,*definitions; gint history_position; gboolean populating;
} Workbench;
static Workbench wb;
static void open_context(GtkButton *,gpointer);
static void browser_filter(GtkEditable *,gpointer);
static void files_filter(GtkEditable *,gpointer);
static void local_filter(GtkEditable *,gpointer);
static void tree_refresh(void);
static GtkWidget *sort_selector(GtkListStore *);
static void stop_usage(void);
static void native_commands_rebuild(void);
static gchar *source_digest(const char *text) {
 /* Geany can remove a UTF-8 BOM and normalize CRLF while opening a file.
  * Compare logical source content; navigation here uses line numbers. */
 if(g_str_has_prefix(text,"\357\273\277")) text+=3;
 GString *normalized=g_string_sized_new(strlen(text));
 for(const char *p=text;*p;p++) if(*p!='\r' || p[1]!='\n') g_string_append_c(normalized,*p);
 gchar *digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,normalized->str,(gssize)normalized->len);
 g_string_free(normalized,TRUE); return digest;
}
static void editor_focus(void) {
 GeanyDocument *doc=document_get_current(); if(doc) document_grab_focus(doc);
}
static gboolean browser_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)data;
 if(event->keyval==GDK_KEY_Escape) { editor_focus(); return TRUE; }
 if(event->keyval==GDK_KEY_Down && (widget==wb.symbol_entry || widget==wb.file_entry || widget==wb.local_entry)) {
  GtkWidget *view=widget==wb.local_entry ? wb.local_view : (widget==wb.symbol_entry ? wb.symbol_view : wb.file_view);
  GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view)); GtkTreeIter iter;
  if(gtk_tree_model_get_iter_first(model,&iter)) {
   GtkTreePath *path=gtk_tree_model_get_path(model,&iter);
   gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),path,NULL,FALSE); gtk_tree_path_free(path); gtk_widget_grab_focus(view);
  }
  return TRUE;
 }
 return FALSE;
}
static void navigation_state(void) {
 if(wb.back) gtk_widget_set_sensitive(wb.back,wb.history_position>0);
 if(wb.forward) gtk_widget_set_sensitive(wb.forward,wb.history && wb.history_position+1<(gint)wb.history->len);
 if(wb.open) gtk_widget_set_sensitive(wb.open,wb.source_line!=0);
}
static void render_source(const char *text,GeanyFiletype *filetype,gulong line) {
 gchar *previous=sci_get_contents(wb.preview,sci_get_length(wb.preview)+1);
 gboolean unchanged=wb.preview_filetype==filetype && !strcmp(previous,text);
 g_free(previous);
 gint target=(gint)MIN(line ? line-1 : 0,(gulong)MAX(0,sci_get_line_count(wb.preview)-1));
 if(unchanged && sci_get_current_line(wb.preview)==target) return;
 if(!unchanged) {
 wb.preview_filetype=filetype;
 highlighting_set_styles(wb.preview,filetype ? filetype : filetypes[GEANY_FILETYPES_NONE]);
 sci_set_readonly(wb.preview,FALSE); sci_set_text(wb.preview,text);
 SSM(wb.preview,SCI_COLOURISE,0,-1);
 sci_set_line_numbers(wb.preview,TRUE);
 SSM(wb.preview,SCI_SETCARETLINEVISIBLE,TRUE,0);
 SSM(wb.preview,SCI_EMPTYUNDOBUFFER,0,0); sci_set_readonly(wb.preview,TRUE);
 }
 target=(gint)MIN(line ? line-1 : 0,(gulong)MAX(0,sci_get_line_count(wb.preview)-1));
 SSM(wb.preview,SCI_GOTOLINE,target,0);
 SSM(wb.preview,SCI_VERTICALCENTRECARET,0,0);
}
static void refresh_index(void) {
 if(!wb.index_dirty) return;
 InsightIndex *next=insight_index_clone(wb.base); if(!next) return;
 for(guint i=0;i<documents_array->len;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);
  if(doc->is_valid && doc->file_name && !insight_native_import(next,doc->tm_file,doc->file_name)) { insight_index_free(next); return; }
 }
 insight_index_free(wb.index); wb.index=next; wb.index_dirty=FALSE;
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
 gboolean valid=root && g_path_is_absolute(root); const InsightSymbol *page[256]; size_t total=insight_index_count(index);
 for(size_t offset=0;valid && offset<total;offset+=256) {
  insight_index_query(index,"",FALSE,NULL,NULL,NULL,offset,page,256);
  for(size_t i=0;i<256 && offset+i<total;i++) if(!g_utf8_validate(page[i]->name,-1,NULL) || !g_utf8_validate(page[i]->path,-1,NULL) || !g_utf8_validate(page[i]->scope,-1,NULL) || !g_utf8_validate(page[i]->kind,-1,NULL) || !g_path_is_absolute(page[i]->path)) { valid=FALSE; break; }
 }
 if(!valid) { insight_index_free(index); free(root); ui_set_statusbar(TRUE,"Workspace cache contains invalid paths or text"); return; }
 insight_index_free(wb.base); wb.base=index; wb.root=g_strdup(root); free(root);
 size_t count=insight_index_file_count(index);
 if(count!=SIZE_MAX && count<=10000) {
  const char **paths=g_new(const char *,count); insight_index_files(index,"",0,paths,count);
  for(size_t i=0;i<count;i++) g_ptr_array_add(wb.files,g_strdup(paths[i]));
  g_free(paths);
 }
}
static gboolean scan_step(gpointer data) {
 (void)data;
 if(!wb.frames->len || wb.parsed>=10000) {
  wb.scan=0; g_ptr_array_set_size(wb.frames,0); refresh_index();
  if(wb.parsed<10000 && !wb.scan_failed) save_index_cache();
  ui_set_statusbar(TRUE,"Workspace: %u files, %zu symbols, %u skipped%s",wb.parsed,insight_index_count(wb.index),wb.skipped,wb.parsed>=10000 ? "; file limit reached" : "");
  browser_filter(NULL,NULL); files_filter(NULL,NULL); tree_refresh(); local_filter(NULL,NULL); insight_workbench_refresh(); return G_SOURCE_REMOVE;
 }
 ScanFrame *frame=g_ptr_array_index(wb.frames,wb.frames->len-1); const char *name=g_dir_read_name(frame->dir);
 if(!name) { g_ptr_array_remove_index(wb.frames,wb.frames->len-1); return G_SOURCE_CONTINUE; }
 if(name[0]=='.' || !strcmp(name,"node_modules") || !strcmp(name,"__pycache__")) return G_SOURCE_CONTINUE;
 gchar *path=g_build_filename(frame->path,name,NULL); GStatBuf st;
 if(g_lstat(path,&st)) { wb.scan_failed=TRUE; wb.skipped++; g_free(path); return G_SOURCE_CONTINUE; }
 if(S_ISDIR(st.st_mode)) {
  GDir *dir=wb.frames->len<64 ? g_dir_open(path,0,NULL) : NULL;
  if(dir) { ScanFrame *next=g_new0(ScanFrame,1); next->path=path; next->dir=dir; g_ptr_array_add(wb.frames,next); }
  else { wb.scan_failed=TRUE; wb.skipped++; g_free(path); }
  return G_SOURCE_CONTINUE;
 }
 if(!S_ISREG(st.st_mode) || st.st_size>2*1024*1024) { wb.skipped++; g_free(path); return G_SOURCE_CONTINUE; }
 gchar *utf8=utils_get_utf8_from_locale(path); GeanyFiletype *ft=filetypes_detect_from_file(utf8);
 if(ft && ft->id!=GEANY_FILETYPES_NONE) { wb.parsed++; g_ptr_array_add(wb.files,g_strdup(utf8)); }
 if(ft && ft->lang>=0) {
  TMSourceFile *file=tm_source_file_new(path,ft->name);
  if(file) {
   file->lang=ft->lang;
   if(tm_source_file_parse(file,NULL,0,FALSE) && insight_native_import(wb.base,file,utf8)) wb.index_dirty=TRUE;
   else { wb.scan_failed=TRUE; wb.skipped++; }
   tm_source_file_free(file);
  } else { wb.scan_failed=TRUE; wb.skipped++; }
 }
 g_free(utf8); g_free(path); return G_SOURCE_CONTINUE;
}
static gboolean start_scan(const char *root) {
 gchar *owned=g_strdup(root); GDir *dir=g_dir_open(owned,0,NULL); InsightIndex *next=insight_index_new();
 if(!dir || !next) { if(dir) g_dir_close(dir); insight_index_free(next); g_free(owned); return FALSE; }
 stop_scan(); insight_index_free(wb.base); wb.base=next; g_free(wb.root); wb.root=g_strdup(owned);
 g_ptr_array_set_size(wb.files,0); ScanFrame *frame=g_new0(ScanFrame,1); frame->path=owned; frame->dir=dir; g_ptr_array_add(wb.frames,frame);
 wb.parsed=wb.skipped=0; wb.index_dirty=TRUE; wb.scan_failed=FALSE; wb.scan=g_idle_add(scan_step,NULL); ui_set_statusbar(TRUE,"Workspace: indexing source folder…"); return TRUE;
}
static void choose_root(GtkButton *button,gpointer data) {
 (void)button; (void)data;
 GtkWidget *dialog=gtk_file_chooser_dialog_new("Open source workspace",GTK_WINDOW(main_widgets.window),GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)); if(!start_scan(path)) ui_set_statusbar(TRUE,"Cannot index source folder"); g_free(path); }
 gtk_widget_destroy(dialog);
}
static void cancel_scan(GtkButton *button,gpointer data) { (void)button; (void)data; stop_scan(); refresh_index(); browser_filter(NULL,NULL); files_filter(NULL,NULL); tree_refresh(); local_filter(NULL,NULL); insight_workbench_refresh(); ui_set_statusbar(TRUE,"Workspace scan cancelled; partial index retained"); }
static const InsightSymbol *word_definition(GeanyDocument *doc,const char *word,const char *preferred) {
 if(!wb.definitions) return NULL;
 if(!preferred && doc) preferred=doc->file_name;
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
  if(!chosen || insight_native_definition_rank(tag,preferred)>insight_native_definition_rank(chosen,preferred)) { chosen=tag; selected=(gint)wb.definitions->len; }
  g_ptr_array_add(wb.definitions,location);
 }
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.choices),wb.definitions->len ? selected : -1);
 wb.populating=FALSE; return chosen;
}
static char *read_disk_source(const char *utf8,gboolean decode) {
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
 if(!ok) { g_string_free(text,TRUE); return NULL; }
 gsize length=text->len; gchar *buffer=g_string_free(text,FALSE);
 if(decode) {
  gboolean nul=FALSE;
  if(!encodings_convert_to_utf8_auto(&buffer,&length,NULL,NULL,NULL,&nul,NULL) || nul) { g_free(buffer); return NULL; }
 }
 if(length>2u*1024u*1024u || !g_utf8_validate(buffer,(gssize)length,NULL) || memchr(buffer,0,length)) { g_free(buffer); return NULL; }
 return buffer;
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
 navigation_state();
}
static gboolean show_location(const char *path,guint id,gulong line,gboolean remember) {
 if(wb.context_toggle && !wb.context_visible) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.context_toggle),TRUE);
 GeanyDocument *doc=id ? document_find_by_id(id) : (path ? document_find_by_filename(path) : NULL);
 char *text=NULL; GeanyFiletype *ft=NULL;
 if(doc) {
  gint length=sci_get_length(doc->editor->sci);
  if(length>=0 && length<=2*1024*1024) text=sci_get_contents(doc->editor->sci,length+1);
  if(text && (!g_utf8_validate(text,length,NULL) || memchr(text,0,(gsize)length))) g_clear_pointer(&text,g_free);
  ft=doc->file_type;
 } else if(path) { text=read_disk_source(path,TRUE); ft=filetypes_detect_from_file(path); }
 if(!text) {
  render_source("",NULL,1); gtk_label_set_text(GTK_LABEL(wb.title),"Context unavailable: requires a decodable text source up to 2 MiB");
  g_clear_pointer(&wb.source_path,g_free); wb.source_id=0; wb.source_line=0; navigation_state(); return FALSE;
 }
 gchar *owned_path=g_strdup(path ? path : (doc ? doc->file_name : NULL));
 size_t begin,end,target_begin,target_end; unsigned long first;
 gboolean valid=insight_context_range(text,strlen(text),line,0,&begin,&end,&first,&target_begin,&target_end);
 if(remember && valid && wb.source_line) remember_location(wb.source_path,wb.source_id,(gulong)sci_get_current_line(wb.preview)+1);
 render_source(text,ft,valid ? line : 1);
 g_free(wb.source_digest); wb.source_digest=source_digest(text); g_free(text);
 if(!valid) line=0;
 g_free(wb.source_path); wb.source_path=owned_path; wb.source_id=doc ? doc->id : 0; wb.source_line=line;
 gchar *label=g_strdup_printf("Context — %s:%lu%s",owned_path ? owned_path : "untitled",line,line ? "" : " • definition line changed; reindex");
 gtk_label_set_text(GTK_LABEL(wb.title),label); g_free(label);
 if(remember) remember_location(wb.source_path,wb.source_id,line);
 navigation_state();
 return valid;
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
static void fill_symbol_list(GtkListStore *store,const char *query,const char *path,guint limit) {
 refresh_index(); gtk_list_store_clear(store); gchar *needle=g_utf8_casefold(query,-1); guint shown=0;
 size_t total=insight_index_query(wb.index,"",FALSE,NULL,NULL,path,0,NULL,0);
 const InsightSymbol **found=g_new(const InsightSymbol *,total);
 insight_index_query(wb.index,"",FALSE,NULL,NULL,path,0,found,total);
 for(size_t i=0;i<total && shown<limit;i++) {
  gchar *folded=g_utf8_casefold(found[i]->name,-1); gboolean match=strstr(folded,needle)!=NULL; g_free(folded); if(!match) continue;
  GtkTreeIter iter; gtk_list_store_append(store,&iter); gtk_list_store_set(store,&iter,SYMBOL_NAME,found[i]->name,SYMBOL_SCOPE,found[i]->scope,SYMBOL_PATH,found[i]->path,SYMBOL_LINE,(gulong)found[i]->line,SYMBOL_KIND,found[i]->kind,-1); shown++;
 }
 g_free(found); g_free(needle);
}
static void browser_filter(GtkEditable *entry,gpointer data) {
 (void)entry; (void)data; if(!wb.symbols) return;
 fill_symbol_list(wb.symbols,gtk_entry_get_text(GTK_ENTRY(wb.symbol_entry)),NULL,500);
}
static void browser_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data; if(!wb.preview) return;
 GtkTreeIter iter; GtkTreeModel *model;
 if(!gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 gchar *path; gulong line; gtk_tree_model_get(model,&iter,SYMBOL_PATH,&path,SYMBOL_LINE,&line,-1);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); show_location(path,model==GTK_TREE_MODEL(wb.local_symbols) ? wb.local_doc_id : 0,line,TRUE); g_free(path);
}
static void browser_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; (void)data;
 GtkTreeSelection *selection=gtk_tree_view_get_selection(view);
 gtk_tree_selection_select_path(selection,path);
 if(GTK_WIDGET(view)==wb.symbol_view || GTK_WIDGET(view)==wb.local_view) browser_selected(selection,NULL);
 else if(GTK_WIDGET(view)==wb.file_view) {
  GtkTreeIter iter; GtkTreeModel *model=gtk_tree_view_get_model(view);
  if(!gtk_tree_model_get_iter(model,&iter,path)) return;
  gchar *filename; gtk_tree_model_get(model,&iter,1,&filename,-1);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
  gboolean shown=show_location(filename,0,1,TRUE); g_free(filename); if(!shown) return;
 }
 else return;
 open_context(NULL,NULL);
}
static void browser_entry_activate(GtkEntry *entry,gpointer data) {
 (void)data; GtkTreeView *view=GTK_TREE_VIEW(GTK_WIDGET(entry)==wb.local_entry ? wb.local_view : (GTK_WIDGET(entry)==wb.symbol_entry ? wb.symbol_view : wb.file_view));
 GtkTreeModel *model=gtk_tree_view_get_model(view); GtkTreeIter iter;
 if(gtk_tree_model_get_iter_first(model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter); browser_activated(view,path,NULL,NULL); gtk_tree_path_free(path); }
}
static void outline_changed(GtkToggleButton *button,gpointer data) { (void)button; (void)data; browser_filter(NULL,NULL); }
static void files_filter(GtkEditable *entry,gpointer data) {
 (void)entry; (void)data; if(!wb.file_store) return;
 gchar *needle=g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(wb.file_entry)),-1);
 GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal); gtk_list_store_clear(wb.file_store); guint shown=0;
 for(guint i=0;i<wb.files->len+documents_array->len && shown<10000;i++) {
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
static void file_sort(GtkComboBox *combo,gpointer data) {
 (void)data; gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.file_store),gtk_combo_box_get_active(combo)==1 ? 1 : 0,GTK_SORT_ASCENDING);
}
static void create_files_browser(void) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4); wb.file_entry=gtk_search_entry_new();
 gtk_entry_set_placeholder_text(GTK_ENTRY(wb.file_entry),"Find workspace file…");
 wb.file_store=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_STRING); wb.file_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.file_store));
 for(int i=0;i<2;i++) { GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(i ? "Path" : "File",gtk_cell_renderer_text_new(),"text",i,NULL); gtk_tree_view_column_set_sort_column_id(column,i); gtk_tree_view_append_column(GTK_TREE_VIEW(wb.file_view),column); }
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.file_store),0,GTK_SORT_ASCENDING);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.file_view);
 gtk_box_pack_start(GTK_BOX(box),wb.file_entry,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 GtkWidget *sort=gtk_combo_box_text_new(); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sort),"Sort: file name"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sort),"Sort: full path");
 g_signal_connect(sort,"changed",G_CALLBACK(file_sort),NULL); gtk_combo_box_set_active(GTK_COMBO_BOX(sort),0); gtk_box_pack_start(GTK_BOX(box),sort,FALSE,FALSE,0);
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
 if(doc) { gint length=sci_get_length(doc->editor->sci); if(length>=0 && length<=2*1024*1024) { text=sci_get_contents(doc->editor->sci,length+1); if(text && (!g_utf8_validate(text,length,NULL) || memchr(text,0,(gsize)length))) g_clear_pointer(&text,g_free); } }
 else if(location->path) text=read_disk_source(location->path,TRUE);
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
  else { wb.source_line=0; navigation_state(); ui_set_statusbar(TRUE,"Source changed after search; run the usage search again"); }
  g_free(actual); g_free(text);
 }
 g_free(path); g_free(digest);
}
static void usage_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; (void)data;
 GtkTreeSelection *selection=gtk_tree_view_get_selection(view); gtk_tree_selection_select_path(selection,path);
 usage_selected(selection,NULL); if(wb.source_line) open_context(NULL,NULL);
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
 gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),box,gtk_label_new("Usages"));
 g_signal_connect(search,"clicked",G_CALLBACK(usage_start),NULL); g_signal_connect(cancel,"clicked",G_CALLBACK(usage_cancel),NULL); g_signal_connect(wb.usage_entry,"activate",G_CALLBACK(usage_start),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.usage_view)),"changed",G_CALLBACK(usage_selected),NULL); g_signal_connect(wb.usage_view,"row-activated",G_CALLBACK(usage_activated),NULL);
 g_signal_connect(wb.usage_entry,"key-press-event",G_CALLBACK(browser_key),NULL);
 g_signal_connect(wb.usage_view,"key-press-event",G_CALLBACK(browser_key),NULL);
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
 gtk_widget_set_no_show_all(wb.outline,TRUE);
 g_signal_connect(wb.outline,"toggled",G_CALLBACK(outline_changed),NULL);
 gtk_box_pack_start(GTK_BOX(wb.browser),wb.outline,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(wb.browser),wb.symbol_entry,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(wb.browser),scroll,TRUE,TRUE,0);
 gtk_box_pack_start(GTK_BOX(wb.browser),sort_selector(wb.symbols),FALSE,FALSE,0);
 gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),wb.browser,gtk_label_new("Project Symbols"));
 g_signal_connect(wb.symbol_entry,"changed",G_CALLBACK(browser_filter),NULL);
 g_signal_connect(wb.symbol_entry,"activate",G_CALLBACK(browser_entry_activate),NULL);
 g_signal_connect(wb.symbol_entry,"key-press-event",G_CALLBACK(browser_key),NULL);
 g_signal_connect(wb.symbol_view,"key-press-event",G_CALLBACK(browser_key),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.symbol_view)),"changed",G_CALLBACK(browser_selected),NULL);
 g_signal_connect(wb.symbol_view,"row-activated",G_CALLBACK(browser_activated),NULL);
 gtk_widget_show_all(wb.browser); browser_filter(NULL,NULL);
}
static void local_filter(GtkEditable *entry,gpointer data) {
 (void)entry; (void)data; if(!wb.local_symbols) return;
 refresh_index(); GeanyDocument *doc=document_get_current(); wb.local_doc_id=doc ? doc->id : 0;
 gtk_label_set_text(GTK_LABEL(wb.local_label),doc && doc->file_name ? doc->file_name : "Current file symbols");
 gtk_list_store_clear(wb.local_symbols); if(!doc || !doc->tm_file || !doc->tm_file->tags_array) return;
 gchar *needle=g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(wb.local_entry)),-1); guint shown=0;
 for(guint i=0;i<doc->tm_file->tags_array->len && shown<2000;i++) {
  TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,i); if(!tag || !tag->name || !tag->line || !g_utf8_validate(tag->name,-1,NULL)) continue;
  gchar *folded=g_utf8_casefold(tag->name,-1); gboolean match=strstr(folded,needle)!=NULL; g_free(folded); if(!match) continue;
  gchar kind[2]={tag->kind_letter,0}; GtkTreeIter iter; gtk_list_store_append(wb.local_symbols,&iter);
  gtk_list_store_set(wb.local_symbols,&iter,SYMBOL_NAME,tag->name,SYMBOL_SCOPE,tag->scope && g_utf8_validate(tag->scope,-1,NULL) ? tag->scope : "",SYMBOL_PATH,doc->file_name,SYMBOL_LINE,(gulong)tag->line,SYMBOL_KIND,kind,-1); shown++;
 }
 g_free(needle);
}
static void symbol_sort(GtkComboBox *combo,gpointer data) {
 GtkTreeSortable *model=GTK_TREE_SORTABLE(data); gint mode=gtk_combo_box_get_active(combo);
 gtk_tree_sortable_set_sort_column_id(model,mode==1 ? SYMBOL_LINE : (mode==2 ? SYMBOL_KIND : SYMBOL_NAME),GTK_SORT_ASCENDING);
}
static GtkWidget *sort_selector(GtkListStore *model) {
 GtkWidget *sort=gtk_combo_box_text_new(); const char *labels[]={"Sort: name","Sort: source line","Sort: symbol type"};
 for(guint i=0;i<3;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sort),labels[i]);
 g_signal_connect(sort,"changed",G_CALLBACK(symbol_sort),model); gtk_combo_box_set_active(GTK_COMBO_BOX(sort),0); return sort;
}
static void create_local_browser(void) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3); wb.local_label=gtk_label_new("Current file symbols");
 gtk_label_set_ellipsize(GTK_LABEL(wb.local_label),PANGO_ELLIPSIZE_MIDDLE);
 wb.local_entry=gtk_search_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.local_entry),"Symbol Name…");
 wb.local_symbols=gtk_list_store_new(SYMBOL_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING);
 wb.local_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.local_symbols));
 gint columns[]={SYMBOL_NAME,SYMBOL_LINE,SYMBOL_KIND}; const char *labels[]={"Symbol","Line","Type"};
 for(guint i=0;i<3;i++) { GtkTreeViewColumn *c=gtk_tree_view_column_new_with_attributes(labels[i],gtk_cell_renderer_text_new(),"text",columns[i],NULL); gtk_tree_view_column_set_sort_column_id(c,columns[i]); gtk_tree_view_append_column(GTK_TREE_VIEW(wb.local_view),c); }
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.local_view);
 gtk_box_pack_start(GTK_BOX(box),wb.local_label,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.local_entry,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(box),sort_selector(wb.local_symbols),FALSE,FALSE,0);
 gtk_notebook_append_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),box,gtk_label_new("File Symbols"));
 gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),gtk_notebook_page_num(GTK_NOTEBOOK(main_widgets.sidebar_notebook),box));
 gtk_notebook_set_show_tabs(GTK_NOTEBOOK(main_widgets.sidebar_notebook),TRUE);
 g_signal_connect(wb.local_entry,"changed",G_CALLBACK(local_filter),NULL); g_signal_connect(wb.local_entry,"activate",G_CALLBACK(browser_entry_activate),NULL);
 g_signal_connect(wb.local_entry,"key-press-event",G_CALLBACK(browser_key),NULL); g_signal_connect(wb.local_view,"key-press-event",G_CALLBACK(browser_key),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.local_view)),"changed",G_CALLBACK(browser_selected),NULL);
 g_signal_connect(wb.local_view,"row-activated",G_CALLBACK(browser_activated),NULL); gtk_widget_show_all(box); local_filter(NULL,NULL);
}
static gint folder_compare(GtkTreeModel *model,GtkTreeIter *a,GtkTreeIter *b,gpointer data) {
 (void)data; gchar *an,*bn; gboolean ad,bd; gtk_tree_model_get(model,a,0,&an,2,&ad,-1); gtk_tree_model_get(model,b,0,&bn,2,&bd,-1);
 gint result=ad!=bd ? (ad ? -1 : 1) : g_utf8_collate(an,bn); g_free(an); g_free(bn); return result;
}
static GtkTreeIter *tree_directory(GHashTable *nodes,const char *path,const char *root,guint depth) {
 GtkTreeIter *iter=g_hash_table_lookup(nodes,path); if(iter) return iter;
 if(depth>=64 || !strcmp(path,root)) return NULL;
 gchar *parent=g_path_get_dirname(path); GtkTreeIter *above=strcmp(parent,path) ? tree_directory(nodes,parent,root,depth+1) : NULL;
 iter=g_new(GtkTreeIter,1); gtk_tree_store_append(wb.project_tree,iter,above); gchar *name=g_path_get_basename(path);
 gtk_tree_store_set(wb.project_tree,iter,0,name,1,path,2,TRUE,-1); g_hash_table_insert(nodes,g_strdup(path),iter); g_free(name); g_free(parent); return iter;
}
static void tree_refresh(void) {
 if(!wb.project_tree) return;
 gtk_tree_store_clear(wb.project_tree);
 GHashTable *nodes=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free); gchar *root=wb.root ? utils_get_utf8_from_locale(wb.root) : g_strdup("");
 for(guint i=0;i<wb.files->len && i<10000;i++) { const char *path=g_ptr_array_index(wb.files,i); gchar *parent=g_path_get_dirname(path),*name=g_path_get_basename(path);
  GtkTreeIter *directory=tree_directory(nodes,parent,root,0),iter; gtk_tree_store_append(wb.project_tree,&iter,directory); gtk_tree_store_set(wb.project_tree,&iter,0,name,1,path,2,FALSE,-1); g_free(parent); g_free(name); }
 g_free(root); g_hash_table_destroy(nodes);
}
static void tree_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data; GtkTreeModel *model; GtkTreeIter iter; if(!gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 gchar *path; gboolean directory; gtk_tree_model_get(model,&iter,1,&path,2,&directory,-1);
 if(!directory) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); show_location(path,0,1,TRUE); } g_free(path);
}
static void tree_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; (void)data; GtkTreeIter iter; GtkTreeModel *model=gtk_tree_view_get_model(view); if(!gtk_tree_model_get_iter(model,&iter,path)) return;
 gboolean directory; gtk_tree_model_get(model,&iter,2,&directory,-1);
 if(directory) { if(gtk_tree_view_row_expanded(view,path)) gtk_tree_view_collapse_row(view,path); else gtk_tree_view_expand_row(view,path,FALSE); return; }
 GtkTreeSelection *selection=gtk_tree_view_get_selection(view); gtk_tree_selection_select_path(selection,path); tree_selected(selection,NULL); if(wb.source_line) open_context(NULL,NULL);
}
static void create_project_tree(void) {
 wb.project_tree=gtk_tree_store_new(3,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN); wb.tree_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.project_tree));
 gtk_tree_view_append_column(GTK_TREE_VIEW(wb.tree_view),gtk_tree_view_column_new_with_attributes("Project folders",gtk_cell_renderer_text_new(),"text",0,NULL));
 gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(wb.project_tree),folder_compare,NULL,NULL);
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.project_tree),GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,GTK_SORT_ASCENDING);
 gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.tree_view),1); GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.tree_view);
 gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),scroll,gtk_label_new("Folders"));
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.tree_view)),"changed",G_CALLBACK(tree_selected),NULL); g_signal_connect(wb.tree_view,"row-activated",G_CALLBACK(tree_activated),NULL);
 g_signal_connect(wb.tree_view,"key-press-event",G_CALLBACK(browser_key),NULL); gtk_widget_show_all(scroll); tree_refresh();
}
static gboolean refresh_now(gpointer data) {
 (void)data; wb.refresh=0;
 GeanyDocument *active=document_get_current(); gboolean dirty=wb.index_dirty;
 if(dirty || wb.local_doc_id!=(active ? active->id : 0)) local_filter(NULL,NULL);
 if(dirty) browser_filter(NULL,NULL);
 if(!wb.preview || !wb.context_visible || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow))) return G_SOURCE_REMOVE;
 GeanyDocument *doc=document_get_current();
 if(!doc) { render_source("",NULL,1); g_clear_pointer(&wb.source_path,g_free); wb.source_id=0; wb.source_line=0; navigation_state(); gtk_label_set_text(GTK_LABEL(wb.title),"Context — no active source"); return G_SOURCE_REMOVE; }
 gchar word[1025]={0}; editor_find_current_word_sciwc(doc->editor,sci_get_current_position(doc->editor->sci),word,sizeof word);
 const InsightSymbol *definition=word_definition(doc,word,NULL);
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
 navigation_state();
}
static void open_context(GtkButton *button,gpointer data) {
 (void)button; (void)data;
 if(!wb.source_line) { ui_set_statusbar(TRUE,"Context has no valid source location"); return; }
 guint id=wb.source_id; gchar *path=g_strdup(wb.source_path);
 gulong line=wb.source_line ? (gulong)sci_get_current_line(wb.preview)+1 : 0;
 GeanyDocument *old=document_get_current(),*doc=id ? document_find_by_id(id) : NULL;
 if(!doc && path) { gchar *native=utils_get_locale_from_utf8(path); doc=document_open_file(native,FALSE,NULL,NULL); g_free(native); }
 if(doc && line && line<=(gulong)sci_get_line_count(doc->editor->sci)) {
  gint length=sci_get_length(doc->editor->sci); gchar *current=length>=0 && length<=2*1024*1024 ? sci_get_contents(doc->editor->sci,length+1) : NULL;
  gchar *digest=current && g_utf8_validate(current,length,NULL) && !memchr(current,0,(gsize)length) ? source_digest(current) : NULL;
  gboolean unchanged=wb.source_digest && !g_strcmp0(digest,wb.source_digest); g_free(current); g_free(digest);
  if(!unchanged) { ui_set_statusbar(TRUE,"Context source changed; refresh the preview before opening"); g_free(path); return; }
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),TRUE);
  navqueue_goto_line(old,doc,(gint)line); SSM(doc->editor->sci,SCI_VERTICALCENTRECARET,0,0); document_grab_focus(doc);
 } else ui_set_statusbar(TRUE,"Context source is unavailable or changed; refresh the index");
 g_free(path);
}
static gboolean preview_definition(gint position) {
 if(position<0) return FALSE;
 gint start=(gint)SSM(wb.preview,SCI_WORDSTARTPOSITION,position,TRUE),end=(gint)SSM(wb.preview,SCI_WORDENDPOSITION,position,TRUE);
 if(end<=start || end-start>1024) return TRUE;
 char word[1025]; for(gint i=start;i<end;i++) word[i-start]=(char)sci_get_char_at(wb.preview,i); word[end-start]=0;
 GeanyDocument *source=wb.source_id ? document_find_by_id(wb.source_id) : document_get_current();
 const InsightSymbol *definition=word_definition(source,word,wb.source_path);
 if(definition) {
  gchar *path=g_strdup(definition->path); gulong line=definition->line;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
  show_location(path,0,line,TRUE); g_free(path);
 } else ui_set_statusbar(TRUE,"Definition not found: %s",word);
 return TRUE;
}
static gboolean preview_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget; (void)data;
 if(event->keyval==GDK_KEY_Return && !(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK))) { open_context(NULL,NULL); return TRUE; }
 if(event->keyval==GDK_KEY_Escape) { editor_focus(); return TRUE; }
 if(event->keyval==GDK_KEY_F12 && !(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SHIFT_MASK))) return preview_definition(sci_get_current_position(wb.preview));
 return FALSE;
}
static gboolean preview_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)widget; (void)data;
 if(event->button!=1) return FALSE;
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
 if(!(event->state&GEANY_PRIMARY_MOD_MASK)) return FALSE;
 gint position=(gint)SSM(wb.preview,SCI_POSITIONFROMPOINTCLOSE,(uptr_t)event->x,(sptr_t)event->y);
 return preview_definition(position);
}
void insight_workbench_refresh(void) {
 if(!wb.preview) return;
 if(wb.refresh) g_source_remove(wb.refresh);
 wb.refresh=g_timeout_add(60,refresh_now,NULL);
}
typedef struct { GeanyDocument *doc; gchar *scratch; GeanyDocument *undo_doc; int message_set; } NativeHost;
static const char *native_get_text(void *ctx) {
 NativeHost *h=ctx; g_free(h->scratch);
 h->scratch=sci_get_contents(h->doc->editor->sci,sci_get_length(h->doc->editor->sci)+1);
 return h->scratch;
}
static const char *native_get_selection(void *ctx) {
 NativeHost *h=ctx; g_free(h->scratch);
 h->scratch=sci_get_selection_contents(h->doc->editor->sci); return h->scratch;
}
static int native_replace_selection(void *ctx,const char *text) {
 NativeHost *h=ctx; sci_replace_sel(h->doc->editor->sci,text); return 1;
}
static unsigned long native_get_position(void *ctx) {
 NativeHost *h=ctx; return (unsigned long)sci_get_current_position(h->doc->editor->sci);
}
static int native_go_line(void *ctx,unsigned long line) {
 NativeHost *h=ctx;
 if(!line || line>(unsigned long)sci_get_line_count(h->doc->editor->sci)) return 0;
 return navqueue_goto_line(h->doc,h->doc,(gint)line);
}
static int native_open_file(void *ctx,const char *path) {
 NativeHost *h=ctx; if(!g_utf8_validate(path,-1,NULL) || !g_path_is_absolute(path)) return 0;
 gchar *locale=utils_get_locale_from_utf8(path); GeanyDocument *next=document_open_file(locale,FALSE,NULL,NULL); g_free(locale);
 if(!next) return 0;
 if(h->undo_doc && h->undo_doc!=next) { sci_end_undo_action(h->undo_doc->editor->sci); sci_start_undo_action(next->editor->sci); h->undo_doc=next; }
 h->doc=next; return 1;
}
static int native_save_file(void *ctx) { NativeHost *h=ctx; return h->doc->file_name && document_save_file(h->doc,FALSE); }
static const char *native_filename(void *ctx) { return ((NativeHost *)ctx)->doc->file_name; }
static int native_selection_range(void *ctx,unsigned long *start,unsigned long *end) {
 NativeHost *h=ctx; *start=(unsigned long)sci_get_selection_start(h->doc->editor->sci); *end=(unsigned long)sci_get_selection_end(h->doc->editor->sci); return 1;
}
static int native_valid_range(NativeHost *h,unsigned long start,unsigned long end) {
 ScintillaObject *sci=h->doc->editor->sci; unsigned long length=(unsigned long)sci_get_length(sci);
 if(start>end || end>length) return 0;
 return (start==length || (((unsigned char)sci_get_char_at(sci,(gint)start)&0xc0)!=0x80)) &&
  (end==length || (((unsigned char)sci_get_char_at(sci,(gint)end)&0xc0)!=0x80));
}
static int native_set_selection(void *ctx,unsigned long start,unsigned long end) {
 NativeHost *h=ctx; if(!native_valid_range(h,start,end)) return 0; sci_set_selection_start(h->doc->editor->sci,(gint)start); sci_set_selection_end(h->doc->editor->sci,(gint)end); return 1;
}
static unsigned long native_line_count(void *ctx) { return (unsigned long)sci_get_line_count(((NativeHost *)ctx)->doc->editor->sci); }
static const char *native_line_text(void *ctx,unsigned long line) {
 NativeHost *h=ctx; if(!line || line>(unsigned long)sci_get_line_count(h->doc->editor->sci)) return NULL;
 g_free(h->scratch); h->scratch=sci_get_line(h->doc->editor->sci,(gint)line-1); return h->scratch;
}
static int native_replace_range(void *ctx,unsigned long start,unsigned long end,const char *text) {
 NativeHost *h=ctx; if(!native_valid_range(h,start,end) || !g_utf8_validate(text,-1,NULL)) return 0;
 sci_set_target_start(h->doc->editor->sci,(gint)start); sci_set_target_end(h->doc->editor->sci,(gint)end);
 return sci_replace_target(h->doc->editor->sci,text,FALSE)>=0;
}
static int native_insert(void *ctx,unsigned long at,const char *text) { return native_replace_range(ctx,at,at,text); }
static int native_message(void *ctx,const char *text) {
 NativeHost *h=ctx; if(!g_utf8_validate(text,-1,NULL)) return 0; h->message_set=1; ui_set_statusbar(TRUE,"%s",text); return 1;
}
static const InsightEditorAPI native_host_api={.position=native_get_position,.goto_line=native_go_line,.message=native_message,.insert=native_insert,.replace_range=native_replace_range,.line_text=native_line_text,.line_count=native_line_count,.set_selection=native_set_selection,.selection_range=native_selection_range,.filename=native_filename,.save_file=native_save_file,.open_file=native_open_file};
static void execute_native(const char *id,const char *source,gsize length) {
 if(wb.lua_running || insight_session_is_running(insight_extensions_session(wb.extensions))) { ui_set_statusbar(TRUE,"Lua command already running"); return; }
 GeanyDocument *doc=document_get_current(); if(!doc) return;
 refresh_index(); InsightIndex *snapshot=insight_index_clone(wb.index);
 if(!snapshot) { ui_set_statusbar(TRUE,"Lua: unable to snapshot the project index"); return; }
 NativeHost h={.doc=doc,.undo_doc=doc};
 InsightHost host={.context=&h,.text=native_get_text,.selection=native_get_selection,.replace_selection=native_replace_selection,.index=snapshot,.api=&native_host_api};
 char error[512]; wb.lua_running=TRUE; sci_start_undo_action(doc->editor->sci);
 int result=id ? insight_session_invoke(insight_extensions_session(wb.extensions),id,&host,error,sizeof error) : insight_run(&host,source,length,error,sizeof error);
 if(h.undo_doc && h.undo_doc->is_valid) sci_end_undo_action(h.undo_doc->editor->sci);
 wb.lua_running=FALSE; g_free(h.scratch); insight_index_free(snapshot);
 if(result || !h.message_set) ui_set_statusbar(TRUE,"Lua: %s",result ? (error[0] ? error : "runtime unavailable") : "completed");
 insight_workbench_refresh();
}
static void choose_native_script(GtkMenuItem *item,gpointer data) {
 (void)item;
 if(wb.lua_running) return;
 GtkWidget *dialog=gtk_file_chooser_dialog_new(data ? "Run Lua script" : "Load Lua extension",GTK_WINDOW(main_widgets.window),GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);
 gchar *path=NULL; if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
 gtk_widget_destroy(dialog); if(!path) return;
 GError *error=NULL;
 if(data) {
  char *source; gsize length;
  if(insight_read_lua_file(path,&source,&length,&error)) { execute_native(NULL,source,length); g_free(source); }
 } else if(insight_extensions_select(wb.extensions,path,&error)) native_commands_rebuild();
 if(error) { ui_set_statusbar(TRUE,"Lua: %s",error->message); g_error_free(error); }
 g_free(path);
}
static void reload_native_script(GtkMenuItem *item,gpointer data) {
 (void)item; GError *error=NULL;
 gboolean ok=data ? insight_extensions_unload(wb.extensions,&error) : insight_extensions_restore(wb.extensions,&error);
 if(ok) native_commands_rebuild();
 else { ui_set_statusbar(TRUE,"Lua: %s",error ? error->message : "unable to load extension"); g_clear_error(&error); }
}
static void invoke_native_command(GtkMenuItem *item,gpointer data) {
 (void)data; execute_native(g_object_get_data(G_OBJECT(item),"native-command-id"),NULL,0);
}
typedef struct { GtkListStore *store; GtkWidget *view; } NativePalette;
static void native_palette_filter(GtkEditable *entry,gpointer data) {
 NativePalette *palette=data; gtk_list_store_clear(palette->store);
 gchar *needle=g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(entry)),-1);
 InsightSession *session=insight_extensions_session(wb.extensions);
 for(size_t i=0;i<insight_session_command_count(session);i++) {
  const char *id=insight_session_command_name(session,i),*label=insight_session_command_label(session,i);
  if(!g_utf8_validate(label,-1,NULL)) label=id;
  gchar *text=g_strconcat(label," ",id,NULL),*folded=g_utf8_casefold(text,-1);
  if(strstr(folded,needle)) { GtkTreeIter iter; gtk_list_store_append(palette->store,&iter); gtk_list_store_set(palette->store,&iter,0,label,1,id,-1); }
  g_free(text); g_free(folded);
 }
 g_free(needle); GtkTreeIter first;
 if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(palette->store),&first)) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(palette->view)),&first);
}
static void palette_activate(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)view; (void)path; (void)column; gtk_dialog_response(GTK_DIALOG(data),GTK_RESPONSE_ACCEPT);
}
static void show_native_palette(GtkMenuItem *item,gpointer data) {
 (void)item; (void)data; if(wb.lua_running) return;
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Lua command palette",GTK_WINDOW(main_widgets.window),GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Run",GTK_RESPONSE_ACCEPT,NULL);
 gtk_window_set_default_size(GTK_WINDOW(dialog),650,400); gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);
 GtkWidget *entry=gtk_search_entry_new(),*box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)); gtk_entry_set_activates_default(GTK_ENTRY(entry),TRUE);
 GtkListStore *store=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_STRING); GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
 for(int i=0;i<2;i++) gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes(i ? "ID" : "Command",gtk_cell_renderer_text_new(),"text",i,NULL));
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),view);
 gtk_box_pack_start(GTK_BOX(box),entry,FALSE,FALSE,4); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,4);
 NativePalette palette={store,view}; g_signal_connect(entry,"changed",G_CALLBACK(native_palette_filter),&palette); g_signal_connect(view,"row-activated",G_CALLBACK(palette_activate),dialog);
 native_palette_filter(GTK_EDITABLE(entry),&palette); gtk_widget_show_all(dialog); gchar *id=NULL;
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { GtkTreeIter iter; if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),NULL,&iter)) gtk_tree_model_get(GTK_TREE_MODEL(store),&iter,1,&id,-1); }
 gtk_widget_destroy(dialog); g_object_unref(store); if(id) execute_native(id,NULL,0); g_free(id);
}
static void native_commands_rebuild(void) {
 if(!wb.lua_commands) return;
 if(wb.lua_accels) { guint old_key; GdkModifierType old_mods; gtk_accelerator_parse("<Primary><Shift>p",&old_key,&old_mods); gtk_widget_remove_accelerator(wb.palette_item,wb.lua_accels,old_key,old_mods); gtk_window_remove_accel_group(GTK_WINDOW(main_widgets.window),wb.lua_accels); g_object_unref(wb.lua_accels); }
 wb.lua_accels=gtk_accel_group_new(); gtk_window_add_accel_group(GTK_WINDOW(main_widgets.window),wb.lua_accels);
 guint palette_key; GdkModifierType palette_mods; gtk_accelerator_parse("<Primary><Shift>p",&palette_key,&palette_mods);
 gtk_widget_add_accelerator(wb.palette_item,"activate",wb.lua_accels,palette_key,palette_mods,GTK_ACCEL_VISIBLE);
 GList *children=gtk_container_get_children(GTK_CONTAINER(wb.lua_commands)); for(GList *p=children;p;p=p->next) gtk_widget_destroy(p->data); g_list_free(children);
 guint keys[128]={0}; GdkModifierType modifiers[128]={0}; InsightSession *session=insight_extensions_session(wb.extensions);
 for(size_t i=0;i<insight_session_command_count(session);i++) {
  const char *id=insight_session_command_name(session,i),*label=insight_session_command_label(session,i),*shortcut=insight_session_command_shortcut(session,i);
  GtkWidget *item=gtk_menu_item_new_with_label(g_utf8_validate(label,-1,NULL) ? label : id);
  g_object_set_data_full(G_OBJECT(item),"native-command-id",g_strdup(id),g_free); gtk_container_add(GTK_CONTAINER(wb.lua_commands),item); g_signal_connect(item,"activate",G_CALLBACK(invoke_native_command),NULL);
  guint key; GdkModifierType mods; gtk_accelerator_parse(shortcut,&key,&mods); gboolean duplicate=key==palette_key && mods==palette_mods;
  for(size_t j=0;j<i;j++) if(keys[j]==key && modifiers[j]==mods) duplicate=TRUE;
  if(key && !duplicate && gtk_accelerator_valid(key,mods)) { gtk_widget_add_accelerator(item,"activate",wb.lua_accels,key,mods,GTK_ACCEL_VISIBLE); keys[i]=key; modifiers[i]=mods; }
 }
 gtk_widget_show_all(wb.lua_commands);
}
static void native_document_event(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object; if(strcmp(data,"document_activate")) wb.index_dirty=TRUE; insight_workbench_refresh();
 if(!wb.extensions || wb.lua_running || !doc || !doc->is_valid || insight_session_is_running(insight_extensions_session(wb.extensions))) return;
 if(!strcmp(data,"document_save") && doc->file_name && !wb.scan && g_ptr_array_find_with_equal_func(wb.files,doc->file_name,(GEqualFunc)g_str_equal,NULL)) insight_native_import(wb.base,doc->tm_file,doc->file_name);
 refresh_index(); InsightIndex *snapshot=insight_index_clone(wb.index); if(!snapshot) return;
 NativeHost h={.doc=doc}; InsightHost host={.context=&h,.text=native_get_text,.selection=native_get_selection,.replace_selection=native_replace_selection,.index=snapshot,.api=&native_host_api};
 char error[512]; wb.lua_running=TRUE;
 int result=insight_session_emit(insight_extensions_session(wb.extensions),data,doc->file_name,&host,error,sizeof error);
 wb.lua_running=FALSE; g_free(h.scratch); insight_index_free(snapshot); if(result) ui_set_statusbar(TRUE,"Lua event: %s",error);
 if(!strcmp(data,"document_close")) wb.index_dirty=TRUE;
}
static void create_native_lua(void) {
 gchar *settings=g_build_filename(app->configdir,"insight","lua-extension.conf",NULL);
 wb.extensions=insight_extensions_new(settings); g_free(settings); GError *error=NULL;
 if(!insight_extensions_restore(wb.extensions,&error)) { ui_set_statusbar(TRUE,"Lua startup: %s",error->message); g_clear_error(&error); }
 GtkWidget *item=gtk_menu_item_new_with_label("Lua scripting"); wb.lua_item=item; wb.lua_menu=gtk_menu_new(); gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),wb.lua_menu);
 const char *labels[]={"Load extension…","Reload saved extension","Unload extension","Run script…"};
 GCallback actions[]={G_CALLBACK(choose_native_script),G_CALLBACK(reload_native_script),G_CALLBACK(reload_native_script),G_CALLBACK(choose_native_script)};
 for(guint i=0;i<4;i++) { GtkWidget *entry=gtk_menu_item_new_with_label(labels[i]); gtk_container_add(GTK_CONTAINER(wb.lua_menu),entry); g_signal_connect(entry,"activate",actions[i],GINT_TO_POINTER(i>=2)); }
 GtkWidget *commands=gtk_menu_item_new_with_label("Commands"); wb.lua_commands=gtk_menu_new(); gtk_menu_item_set_submenu(GTK_MENU_ITEM(commands),wb.lua_commands); gtk_container_add(GTK_CONTAINER(wb.lua_menu),commands);
 wb.palette_item=gtk_menu_item_new_with_label("Command palette…"); g_signal_connect(wb.palette_item,"activate",G_CALLBACK(show_native_palette),NULL); gtk_container_add(GTK_CONTAINER(wb.lua_menu),wb.palette_item);
 gtk_container_add(GTK_CONTAINER(main_widgets.tools_menu),item); gtk_widget_show_all(item); native_commands_rebuild();
}
static void focus_browser(GtkMenuItem *item,gpointer data) {
 (void)item; gint which=GPOINTER_TO_INT(data); GtkWidget *widget=which==1 ? wb.file_entry : (which==2 ? wb.usage_entry : wb.symbol_entry);
 GtkWidget *page=widget; while(page && gtk_widget_get_parent(page)!=wb.project_notebook) page=gtk_widget_get_parent(page);
 if(!page) return;
 gtk_widget_show(wb.project_notebook);
 ui_prefs.sidebar_visible=TRUE; ui_sidebar_show_hide();
 gtk_notebook_set_current_page(GTK_NOTEBOOK(wb.project_notebook),gtk_notebook_page_num(GTK_NOTEBOOK(wb.project_notebook),page));
 if(which==1) files_filter(NULL,NULL); else if(which==0) browser_filter(NULL,NULL);
 gtk_widget_grab_focus(widget);
}
gboolean insight_workbench_goto_definition(void) {
 if(wb.preview && gtk_widget_has_focus(GTK_WIDGET(wb.preview)))
  return preview_definition(sci_get_current_position(wb.preview));
 GeanyDocument *doc=document_get_current(); if(!wb.preview || !doc) return FALSE;
 char word[1025]={0}; editor_find_current_word_sciwc(doc->editor,sci_get_current_position(doc->editor->sci),word,sizeof word);
 const InsightSymbol *definition=word_definition(doc,word,NULL);
 if(!definition) return FALSE;
 gchar *path=g_strdup(definition->path); gulong line=definition->line;
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); gboolean shown=show_location(path,0,line,TRUE); if(shown) open_context(NULL,NULL); g_free(path); return shown;
}
static void go_definition(GtkMenuItem *item,gpointer data) { (void)item; (void)data; if(!insight_workbench_goto_definition()) ui_set_statusbar(TRUE,"Known definition not found"); }
static void context_visible(GtkCheckMenuItem *item,gpointer data) {
 (void)data; wb.context_visible=gtk_check_menu_item_get_active(item);
 if(wb.context_visible) { gtk_widget_show(wb.box); insight_workbench_refresh(); } else gtk_widget_hide(wb.box);
}
static void create_workbench_menus(void) {
 GtkWidget *menubar=ui_lookup_widget(main_widgets.window,"menubar1"),*navigate=gtk_menu_item_new_with_mnemonic("_Navigate"),*menu=gtk_menu_new();
 gtk_menu_item_set_submenu(GTK_MENU_ITEM(navigate),menu); gtk_menu_shell_insert(GTK_MENU_SHELL(menubar),navigate,4);
 g_object_set_data(G_OBJECT(main_widgets.window),"insight-navigate",navigate);
 wb.navigation_accels=gtk_accel_group_new(); gtk_window_add_accel_group(GTK_WINDOW(main_widgets.window),wb.navigation_accels);
 const char *names[]={"Go to definition","Find symbol…","Find file…","Find usages…","Context back","Context forward"};
 const char *shortcuts[]={"F12","<Primary><Alt>s","<Primary><Alt>o","<Primary><Alt>r","<Primary><Alt>Left","<Primary><Alt>Right"};
 for(int i=0;i<6;i++) {
  GtkWidget *entry=gtk_menu_item_new_with_label(names[i]); gtk_container_add(GTK_CONTAINER(menu),entry);
  if(!i) g_signal_connect(entry,"activate",G_CALLBACK(go_definition),NULL);
  else if(i<4) g_signal_connect(entry,"activate",G_CALLBACK(focus_browser),GINT_TO_POINTER(i-1));
  else g_signal_connect(entry,"activate",G_CALLBACK(history_move),GINT_TO_POINTER(i==4 ? -1 : 1));
  guint key; GdkModifierType mods; gtk_accelerator_parse(shortcuts[i],&key,&mods); gtk_widget_add_accelerator(entry,"activate",wb.navigation_accels,key,mods,GTK_ACCEL_VISIBLE);
 }
 g_object_ref(wb.lua_item); gtk_container_remove(GTK_CONTAINER(main_widgets.tools_menu),wb.lua_item); gtk_menu_item_set_label(GTK_MENU_ITEM(wb.lua_item),"Scripts"); gtk_menu_shell_insert(GTK_MENU_SHELL(menubar),wb.lua_item,5); g_object_unref(wb.lua_item);
 wb.context_toggle=gtk_check_menu_item_new_with_label("Context window"); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.context_toggle),wb.context_visible);
 g_signal_connect(wb.context_toggle,"toggled",G_CALLBACK(context_visible),NULL); gtk_container_add(GTK_CONTAINER(ui_lookup_widget(main_widgets.window,"menu_view1_menu")),wb.context_toggle);
 gtk_widget_show_all(navigate); gtk_widget_show(wb.context_toggle);
 gtk_widget_set_name(wb.box,"insight-context"); gtk_widget_set_name(wb.title,"insight-context-title"); gtk_widget_set_name(wb.browser,"insight-workspace");
 gtk_label_set_ellipsize(GTK_LABEL(wb.title),PANGO_ELLIPSIZE_MIDDLE);
 GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(wb.choices)); for(GList *p=cells;p;p=p->next) g_object_set(p->data,"ellipsize",PANGO_ELLIPSIZE_MIDDLE,"max-width-chars",80,NULL); g_list_free(cells);
 gtk_tree_view_column_set_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(wb.symbol_view),SYMBOL_PATH),FALSE); gtk_tree_view_column_set_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(wb.symbol_view),SYMBOL_KIND),FALSE); gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.symbol_view),SYMBOL_PATH);
 gtk_tree_view_column_set_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(wb.file_view),1),FALSE); gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.file_view),1);
 wb.css=gtk_css_provider_new(); gtk_css_provider_load_from_data(wb.css,"#insight-context { border-top: 2px solid @theme_selected_bg_color; } #insight-context-title { font-weight: bold; padding: 3px; } #insight-workspace { padding: 3px; }",-1,NULL);
 gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(wb.css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
 gtk_widget_hide(ui_lookup_widget(main_widgets.window,"menu_build1"));
 if(!g_file_test(wb.prefs_path,G_FILE_TEST_EXISTS)) {
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"menu_show_messages_window1")),FALSE);
  ui_prefs.sidebar_visible=TRUE; ui_sidebar_show_hide(); gtk_paned_set_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"hpaned1")),280);
  gtk_notebook_set_current_page(GTK_NOTEBOOK(wb.project_notebook),0);
 }
 ui_set_window_title(document_get_current());
}
static void save_workbench_settings(void) {
 if(!wb.prefs_path || !wb.pane) return;
 GKeyFile *settings=g_key_file_new();
 g_key_file_set_integer(settings,"workbench","version",1);
 g_key_file_set_integer(settings,"workbench","split",gtk_paned_get_position(GTK_PANED(wb.pane)));
 g_key_file_set_boolean(settings,"workbench","follow",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow)));
 g_key_file_set_boolean(settings,"workbench","visible",wb.context_visible);
 g_key_file_set_integer(settings,"workbench","right-split",gtk_paned_get_position(GTK_PANED(wb.right_pane)));
 g_key_file_set_integer(settings,"workbench","project-tab",gtk_notebook_get_current_page(GTK_NOTEBOOK(wb.project_notebook)));
 g_key_file_set_boolean(settings,"workbench","usage-buffer",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_buffer)));
 g_key_file_set_boolean(settings,"workbench","wrap",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.wrap)));
 gchar *encoded=wb.root ? g_base64_encode((const guchar *)wb.root,strlen(wb.root)) : g_strdup("");
 g_key_file_set_string(settings,"workbench","root-base64",encoded); g_free(encoded);
 gsize length; gchar *text=g_key_file_to_data(settings,&length,NULL),*directory=g_path_get_dirname(wb.prefs_path);
 if(!g_mkdir_with_parents(directory,0700)) {
  GFile *file=g_file_new_for_path(wb.prefs_path); GError *error=NULL;
  if(!g_file_replace_contents(file,text,length,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,&error)) { ui_set_statusbar(TRUE,"Workbench settings not saved: %s",error->message); g_clear_error(&error); }
  g_object_unref(file);
 }
 g_free(directory); g_free(text); g_key_file_free(settings);
}
static void restore_workbench_settings(void) {
 gchar *prefs_utf8=utils_get_utf8_from_locale(wb.prefs_path); char *text=read_disk_source(prefs_utf8,FALSE); g_free(prefs_utf8); if(!text) return;
 GKeyFile *settings=g_key_file_new();
 if(strlen(text)<=32768 && g_key_file_load_from_data(settings,text,-1,G_KEY_FILE_NONE,NULL) && g_key_file_get_integer(settings,"workbench","version",NULL)==1) {
  gint split=g_key_file_get_integer(settings,"workbench","split",NULL); if(split>=150 && split<=10000) gtk_paned_set_position(GTK_PANED(wb.pane),split);
  if(g_key_file_has_key(settings,"workbench","follow",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),g_key_file_get_boolean(settings,"workbench","follow",NULL));
  if(g_key_file_has_key(settings,"workbench","visible",NULL)) wb.context_visible=g_key_file_get_boolean(settings,"workbench","visible",NULL);
  gint right=g_key_file_get_integer(settings,"workbench","right-split",NULL); if(right>=200 && right<=10000) gtk_paned_set_position(GTK_PANED(wb.right_pane),right);
  gint tab=g_key_file_get_integer(settings,"workbench","project-tab",NULL); if(tab>=0 && tab<4) gtk_notebook_set_current_page(GTK_NOTEBOOK(wb.project_notebook),tab);
  if(g_key_file_has_key(settings,"workbench","usage-buffer",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_buffer),g_key_file_get_boolean(settings,"workbench","usage-buffer",NULL));
  if(g_key_file_has_key(settings,"workbench","wrap",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.wrap),g_key_file_get_boolean(settings,"workbench","wrap",NULL));
  gchar *encoded=g_key_file_get_string(settings,"workbench","root-base64",NULL);
  if(encoded && !wb.root) {
   gsize length; guchar *decoded=g_base64_decode(encoded,&length); gchar *canonical=g_base64_encode(decoded,length);
   if(length && length<=16384 && !memchr(decoded,0,length) && !strcmp(encoded,canonical)) { gchar *root=g_strndup((const gchar *)decoded,length); if(g_path_is_absolute(root)) wb.root=root; else g_free(root); }
   g_free(canonical); g_free(decoded);
  }
  g_free(encoded);
 }
 g_free(text); g_key_file_free(settings);
 if(!wb.context_visible) gtk_widget_hide(wb.box);
}
static void reindex_root(GtkButton *button,gpointer data) {
 (void)button; (void)data;
 if(!wb.root || !start_scan(wb.root)) ui_set_statusbar(TRUE,"Choose a readable source folder first");
}
static void toolbar_definition(GtkToolButton *button,gpointer data) { (void)button; go_definition(NULL,data); }
static void toolbar_focus(GtkToolButton *button,gpointer data) { (void)button; focus_browser(NULL,data); }
static void create_browse_toolbar(void) {
 GtkToolbar *toolbar=GTK_TOOLBAR(main_widgets.toolbar);
 gtk_toolbar_insert(toolbar,gtk_separator_tool_item_new(),0);
 const char *icons[]={"go-previous","go-next","go-jump","edit-find","system-search","folder-open","view-refresh"};
 const char *labels[]={"Context back","Context forward","Go to definition","Project symbols","Project files","Open source workspace","Reindex workspace"};
 for(guint i=0;i<7;i++) {
  GtkToolItem *item=gtk_tool_button_new(NULL,labels[i]); gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(item),icons[i]); gtk_widget_set_tooltip_text(GTK_WIDGET(item),labels[i]);
  if(i<2) g_signal_connect(item,"clicked",G_CALLBACK(history_move),GINT_TO_POINTER(i ? 1 : -1));
  else if(i==2) g_signal_connect(item,"clicked",G_CALLBACK(toolbar_definition),NULL);
  else if(i<5) g_signal_connect(item,"clicked",G_CALLBACK(toolbar_focus),GINT_TO_POINTER(i==4 ? 1 : 0));
  else if(i==5) g_signal_connect(item,"clicked",G_CALLBACK(choose_root),NULL);
  else g_signal_connect(item,"clicked",G_CALLBACK(reindex_root),NULL);
  gtk_toolbar_insert(toolbar,item,(gint)i);
 }
 gtk_toolbar_set_icon_size(toolbar,GTK_ICON_SIZE_SMALL_TOOLBAR); gtk_widget_show_all(GTK_WIDGET(toolbar));
}
static void window_activate(GtkMenuItem *item,gpointer data) {
 (void)item; GeanyDocument *doc=document_find_by_id(GPOINTER_TO_UINT(data));
 if(doc) { gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.notebook),document_get_notebook_page(doc)); document_grab_focus(doc); }
}
static void window_menu_refresh(GtkWidget *menu,gpointer data) {
 (void)data; GList *items=gtk_container_get_children(GTK_CONTAINER(menu)); for(GList *p=items;p;p=p->next) gtk_widget_destroy(p->data); g_list_free(items);
 for(guint i=0;i<documents_array->len;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i); if(!doc->is_valid) continue;
  GtkWidget *item=gtk_check_menu_item_new_with_label(doc->file_name ? doc->file_name : "untitled");
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),doc==document_get_current());
  g_signal_connect(item,"activate",G_CALLBACK(window_activate),GUINT_TO_POINTER(doc->id)); gtk_container_add(GTK_CONTAINER(menu),item); gtk_widget_show(item);
 }
}
static void arrange_main_menus(void) {
 GtkWidget *bar=ui_lookup_widget(main_widgets.window,"menubar1"),*options=gtk_menu_item_new_with_mnemonic("_Options"),*menu=gtk_menu_new();
 gtk_menu_item_set_submenu(GTK_MENU_ITEM(options),menu); gtk_menu_shell_insert(GTK_MENU_SHELL(bar),options,4);
 GtkWidget *preferences=ui_lookup_widget(main_widgets.window,"preferences1");
 g_object_ref(preferences); gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(preferences)),preferences); gtk_container_add(GTK_CONTAINER(menu),preferences); g_object_unref(preferences); gtk_widget_show_all(options);
 GtkWidget *document_menu=ui_lookup_widget(main_widgets.window,"menu_document1");
 g_object_ref(document_menu); gtk_container_remove(GTK_CONTAINER(bar),document_menu); gtk_container_add(GTK_CONTAINER(menu),document_menu); g_object_unref(document_menu);
 GtkWidget *windows=gtk_menu_item_new_with_mnemonic("_Window"),*window_menu=gtk_menu_new();
 gtk_menu_item_set_submenu(GTK_MENU_ITEM(windows),window_menu); g_signal_connect(window_menu,"show",G_CALLBACK(window_menu_refresh),NULL); gtk_menu_shell_insert(GTK_MENU_SHELL(bar),windows,7); gtk_widget_show(windows);
 GtkWidget *items[]={ui_lookup_widget(main_widgets.window,"file1"),ui_lookup_widget(main_widgets.window,"edit1"),ui_lookup_widget(main_widgets.window,"search1"),ui_lookup_widget(main_widgets.window,"menu_project1"),options,ui_lookup_widget(main_widgets.window,"tools1"),ui_lookup_widget(main_widgets.window,"menu_view1"),windows,ui_lookup_widget(main_widgets.window,"menu_help1"),g_object_get_data(G_OBJECT(main_widgets.window),"insight-navigate"),wb.lua_item};
 for(guint i=0;i<G_N_ELEMENTS(items);i++) {
  GtkWidget *item=items[i];
  g_object_ref(item); gtk_container_remove(GTK_CONTAINER(bar),item); gtk_menu_shell_insert(GTK_MENU_SHELL(bar),item,(gint)i); g_object_unref(item);
 }
}
void insight_workbench_index_changed(void) { wb.index_dirty=TRUE; insight_workbench_refresh(); }
static void follow_changed(GtkToggleButton *button,gpointer data) {
 (void)data;
 if(gtk_toggle_button_get_active(button)) insight_workbench_refresh();
 else if(wb.refresh) { g_source_remove(wb.refresh); wb.refresh=0; }
}
static void wrap_changed(GtkToggleButton *button,gpointer data) {
 (void)data; if(wb.preview) SSM(wb.preview,SCI_SETWRAPMODE,gtk_toggle_button_get_active(button) ? SC_WRAP_WORD : SC_WRAP_NONE,0);
}
static void copy_location(GtkButton *button,gpointer data) {
 (void)button; (void)data;
 if(!wb.source_path || !wb.source_line) { ui_set_statusbar(TRUE,"Context has no saved file location"); return; }
 gchar *address=g_strdup_printf("%s:%d",wb.source_path,sci_get_current_line(wb.preview)+1);
 gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),address,-1); g_free(address);
 ui_set_statusbar(TRUE,"Context file:line copied");
}
void insight_workbench_init(void) {
 GtkWidget *existing=ui_lookup_widget(main_widgets.window,"vpaned1");
 GtkWidget *parent=gtk_widget_get_parent(existing); gint position=0;
 gtk_container_child_get(GTK_CONTAINER(parent),existing,"position",&position,NULL);
 wb.usage_paths=g_ptr_array_new_with_free_func(location_free);
 wb.context_visible=TRUE; wb.prefs_path=g_build_filename(app->configdir,"insight","workbench.conf",NULL);
 wb.frames=g_ptr_array_new_with_free_func(frame_free); wb.files=g_ptr_array_new_with_free_func(g_free);
 wb.base=insight_index_new(); wb.index=insight_index_new(); wb.index_dirty=TRUE;
 wb.cache_path=g_build_filename(app->configdir,"insight","workspace.idx",NULL); restore_index_cache();
 wb.definitions=g_ptr_array_new_with_free_func(location_free);
 wb.history=g_ptr_array_new_with_free_func(location_free); wb.history_position=-1;
 wb.project_notebook=gtk_notebook_new(); gtk_notebook_set_scrollable(GTK_NOTEBOOK(wb.project_notebook),TRUE);
 create_browser(); create_files_browser(); create_project_tree(); create_usage_browser(); create_local_browser();
 wb.pane=gtk_paned_new(GTK_ORIENTATION_VERTICAL);
 wb.box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
 wb.title=gtk_label_new("Context — integrated source navigation");
 gtk_label_set_xalign(GTK_LABEL(wb.title),0.0f);
 GtkWidget *header=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*open=gtk_button_new_with_label("Open source");
 GtkWidget *folder=gtk_button_new_with_label("Source folder…"),*cancel=gtk_button_new_with_label("Cancel scan");
 g_signal_connect(folder,"clicked",G_CALLBACK(choose_root),NULL); g_signal_connect(cancel,"clicked",G_CALLBACK(cancel_scan),NULL);
 GtkWidget *project_controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);
 GtkWidget *reindex=gtk_button_new_with_label("Reindex"); g_signal_connect(reindex,"clicked",G_CALLBACK(reindex_root),NULL);
 gtk_box_pack_start(GTK_BOX(project_controls),reindex,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(project_controls),folder,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(project_controls),cancel,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(wb.browser),project_controls,FALSE,FALSE,0); gtk_box_reorder_child(GTK_BOX(wb.browser),project_controls,0); gtk_widget_show_all(project_controls);
 GtkWidget *back=gtk_button_new_with_label("←"),*forward=gtk_button_new_with_label("→");
 wb.back=back; wb.forward=forward; wb.open=open;
 gtk_widget_set_tooltip_text(back,"Previous Context location");
 gtk_widget_set_tooltip_text(forward,"Next Context location");
 gtk_widget_set_tooltip_text(open,"Open the preview caret line in the editor (Enter)");
 navigation_state();
 g_signal_connect(back,"clicked",G_CALLBACK(history_move),GINT_TO_POINTER(-1));
 g_signal_connect(forward,"clicked",G_CALLBACK(history_move),GINT_TO_POINTER(1));
 gtk_box_pack_start(GTK_BOX(header),back,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(header),forward,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(header),wb.title,TRUE,TRUE,4); gtk_box_pack_end(GTK_BOX(header),open,FALSE,FALSE,4);
 wb.follow=gtk_check_button_new_with_label("Follow cursor");
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),TRUE);
 g_signal_connect(wb.follow,"toggled",G_CALLBACK(follow_changed),NULL);
 gtk_box_pack_end(GTK_BOX(header),wb.follow,FALSE,FALSE,2);
 wb.wrap=gtk_check_button_new_with_label("Wrap");
 g_signal_connect(wb.wrap,"toggled",G_CALLBACK(wrap_changed),NULL);
 gtk_box_pack_end(GTK_BOX(header),wb.wrap,FALSE,FALSE,2);
 GtkWidget *copy=gtk_button_new_with_label("Copy location");
 gtk_widget_set_tooltip_text(copy,"Copy the preview caret address as file:line");
 g_signal_connect(copy,"clicked",G_CALLBACK(copy_location),NULL);
 gtk_box_pack_end(GTK_BOX(header),copy,FALSE,FALSE,2);
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
 gtk_widget_set_tooltip_text(GTK_WIDGET(wb.preview),"Ctrl+click or F12: follow definition; Enter: open caret line");
 gtk_box_pack_start(GTK_BOX(wb.box),GTK_WIDGET(wb.preview),TRUE,TRUE,0);
 gtk_widget_set_size_request(wb.box,-1,170);
 g_object_ref(existing); gtk_container_remove(GTK_CONTAINER(parent),existing);
 wb.right_pane=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
 gtk_paned_pack1(GTK_PANED(wb.right_pane),existing,TRUE,FALSE); g_object_unref(existing);
 gtk_paned_pack2(GTK_PANED(wb.right_pane),wb.project_notebook,FALSE,FALSE);
 gtk_widget_set_size_request(wb.project_notebook,300,-1);
 gtk_paned_set_position(GTK_PANED(wb.right_pane),850);
 gtk_paned_pack1(GTK_PANED(wb.pane),wb.right_pane,TRUE,FALSE);
 gtk_paned_pack2(GTK_PANED(wb.pane),wb.box,FALSE,FALSE);
 gtk_box_pack_start(GTK_BOX(parent),wb.pane,TRUE,TRUE,0);
 gtk_box_reorder_child(GTK_BOX(parent),wb.pane,position);
 gtk_paned_set_position(GTK_PANED(wb.pane),430);
 gtk_widget_show_all(wb.pane);
 restore_workbench_settings();
 create_native_lua();
 const char *signals[]={"document-open","document-save","document-activate","document-close"};
 const char *events[]={"document_open","document_save","document_activate","document_close"};
 for(guint i=0;i<4;i++) wb.event_handlers[i]=g_signal_connect_after(geany_object,signals[i],G_CALLBACK(native_document_event),(gpointer)events[i]);
 create_workbench_menus(); arrange_main_menus(); create_browse_toolbar();
 ui_prefs.sidebar_visible=TRUE; ui_sidebar_show_hide();
 insight_workbench_refresh();
}
void insight_workbench_finalize(void) {
 save_workbench_settings();
 for(guint i=0;i<4;i++) if(wb.event_handlers[i]) g_signal_handler_disconnect(geany_object,wb.event_handlers[i]);
 if(wb.lua_accels) { gtk_window_remove_accel_group(GTK_WINDOW(main_widgets.window),wb.lua_accels); g_object_unref(wb.lua_accels); }
 if(wb.navigation_accels) { gtk_window_remove_accel_group(GTK_WINDOW(main_widgets.window),wb.navigation_accels); g_object_unref(wb.navigation_accels); }
 if(wb.css) { gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(wb.css)); g_object_unref(wb.css); }
 insight_extensions_free(wb.extensions);
 if(wb.refresh) g_source_remove(wb.refresh);
 g_free(wb.prefs_path);
 stop_usage(); g_ptr_array_free(wb.usage_paths,TRUE);
 stop_scan(); g_ptr_array_free(wb.frames,TRUE); g_ptr_array_free(wb.files,TRUE); g_free(wb.root); g_free(wb.cache_path);
 g_free(wb.source_path); g_free(wb.source_digest);
 insight_index_free(wb.base); insight_index_free(wb.index);
 if(wb.symbols) g_object_unref(wb.symbols);
 if(wb.local_symbols) g_object_unref(wb.local_symbols);
 if(wb.project_tree) g_object_unref(wb.project_tree);
 if(wb.file_store) g_object_unref(wb.file_store);
 if(wb.matches) g_object_unref(wb.matches);
 if(wb.history) g_ptr_array_free(wb.history,TRUE);
 if(wb.definitions) g_ptr_array_free(wb.definitions,TRUE);
 memset(&wb,0,sizeof wb);
}
