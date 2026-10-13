/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "insight-workbench.h"
#include "insight-overview.h"
#include "ui_utils.h"
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "projectprivate.h"
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
#include "../insight/desktop/lookup_filter.h"
#include "../insight/desktop/layout_state.h"
#include "../insight/desktop/context_search.h"
#include "../insight/desktop/notebook_store.h"
#include "../insight/desktop/keyword_search.h"
#include "../insight/desktop/search_state.h"
#include "../insight/desktop/project_files.h"
#include "msgwindow.h"
#include "insight-remote.h"
#include "../insight/desktop/workspace_watch.h"
#include "../insight/desktop/source_mask.h"
#include "../insight/desktop/relation_query.h"

typedef struct { gchar *path; GDir *dir; } ScanFrame;
static void frame_free(gpointer data) { ScanFrame *p=data; if(p->dir) g_dir_close(p->dir); g_free(p->path); g_free(p); }
typedef struct { GtkWidget *page,*home,*window; gchar *label; guint order; gboolean pane,restore,visible; gint x,y,width,height; } PanelHost;
typedef struct { gchar *path; guint id; gulong line; } Location;
static void location_free(gpointer data) { Location *p=data; g_free(p->path); g_free(p); }
typedef struct {
 GtkWidget *pane,*box,*title,*follow,*choices,*browser,*symbol_entry,*symbol_view,*outline,*file_entry,*file_view;
 GtkWidget *back,*forward,*open,*wrap,*project_notebook,*right_pane;
 GtkWidget *local_entry,*local_view,*local_label,*tree_view;
 GtkListStore *local_symbols; GtkTreeStore *project_tree,*categories,*file_types;
 GtkWidget *category_entry,*category_view,*types_view,*float_window,*project_toggle,*local_toggle;
 gint float_x,float_y,float_width,float_height; gboolean restore_float;
 InsightLayout layouts[4]; PanelHost hosts[10]; guint host_count; GtkWidget *panel_commands;
 GtkWidget *find_entry,*find_case,*find_word,*find_regex,*find_status;
 gchar *find_last_query,*find_last_digest; gsize find_last_begin,find_last_end; guint find_last_flags;
 InsightNotes *notes; gchar *notes_path;
 GtkWidget *marks_entry,*marks_view,*clips_entry,*clips_view; GtkListStore *marks_store,*clips_store;
 InsightWorkspaceWatch *watch; guint watch_idle; gboolean watch_changed,watch_incomplete;
 guint local_doc_id;
 GeanyFiletype *preview_filetype;
 GtkListStore *file_store,*matches;
 GtkListStore *search_matches,*usage_target;
 GtkWidget *search_view,*search_status,*search_page,*search_scope,*search_mask,*search_keywords,*search_variations,*search_context;
 gchar *search_state_path; GPtrArray *search_files,*search_history; gboolean usage_keywords; guint usage_context;
 GtkWidget *usage_entry,*usage_status,*usage_buffer,*usage_view,*usage_code,*relation_mode,*relation_follow;
 gchar *relation_word; gint relation_last_mode; gboolean relation_dirty,usage_code_only,relation_setting;
 gint usage_mode; guint usage_flags;
 GtkWidget *project_query,*project_case,*project_word,*project_regex,*editor_query,*editor_case,*editor_word,*editor_regex,*project_search_row,*editor_search_row;
 gchar *editor_find_query,*editor_find_digest; gsize editor_find_begin,editor_find_end; guint editor_find_id,editor_find_flags; GtkWidget *search_toolbars_toggle;
 GPtrArray *usage_paths; gchar *usage_name; guint usage_idle,usage_position,usage_shown,usage_skipped;
 GtkListStore *symbols;
 ScintillaObject *preview;
 guint refresh,source_id; gboolean context_visible,index_dirty,scan_failed;
 gulong event_handlers[4];
 gulong project_handlers[3]; gchar *geany_root; gboolean workspace_from_project;
 gchar *prefs_path;
 InsightExtensions *extensions; gboolean lua_running;
 GtkWidget *lua_menu,*lua_commands,*lua_item,*palette_item;
 GtkAccelGroup *lua_accels,*navigation_accels;
 GtkCssProvider *css; GtkWidget *context_toggle;
 InsightIndex *base,*index;
 InsightProjectFiles *membership; gchar *membership_project; gboolean membership_valid;
 GPtrArray *frames,*files,*scan_files,*scan_trees; GHashTable *scan_seen; guint scan_file_position,scan_tree_position; gchar *root,*cache_path; guint scan,parsed,skipped;
 gchar *source_path,*source_digest; gulong source_line;
 GPtrArray *history,*definitions; gint history_position; gboolean populating;
} Workbench;
static Workbench wb;
static void open_context(GtkButton *,gpointer);
static void browser_filter(GtkEditable *,gpointer);
static void files_filter(GtkEditable *,gpointer);
static void local_filter(GtkEditable *,gpointer);
static void tree_refresh(void);
static void categories_filter(GtkEditable *,gpointer);
static void file_types_refresh(void);
static GtkWidget *sort_selector(GtkListStore *);
static void stop_usage(void);
static void native_commands_rebuild(void);
static void save_workbench_settings(void);
static void save_search_settings(void);
static void focus_search_results(GtkWidget *,gpointer);
static void search_result_move(GtkWidget *,gpointer);
static void dock_all_panels(void);
static void detach_panel(guint);
static void focus_widget_panel(GtkWidget *);
static void select_project_tab(gint);
static gboolean start_scan(const char *);
static void manage_project_files(GtkWidget *,gpointer);
static void project_files_drop(GtkWidget *,GdkDragContext *,gint,gint,GtkSelectionData *,guint,guint,gpointer);
static void workspace_changed(gpointer);
static void create_search_toolbars(void);
static guint text_search_flags(GtkWidget *,GtkWidget *,GtkWidget *);
static char *read_disk_source(const char *,gboolean);
static void set_usage_status(const char *text) {
 GtkWidget *label=wb.usage_mode==3 && wb.search_status ? wb.search_status : wb.usage_status;
 gtk_label_set_text(GTK_LABEL(label),text);
}
/* Private decoration slots; Geany's error/search/snippet indicators retain
 * their original indices and styles. */
enum { INSIGHT_MATCH_FILL=28, INSIGHT_MATCH_FRAME=29, INSIGHT_SAME_TEXT=30, INSIGHT_LINK=31 };
static void preview_links(void);
static void decorate_setup(ScintillaObject *sci) {
 SSM(sci,SCI_INDICSETSTYLE,INSIGHT_MATCH_FILL,INDIC_STRAIGHTBOX); SSM(sci,SCI_INDICSETFORE,INSIGHT_MATCH_FILL,0x00ffff);
 SSM(sci,SCI_INDICSETALPHA,INSIGHT_MATCH_FILL,110); SSM(sci,SCI_INDICSETOUTLINEALPHA,INSIGHT_MATCH_FILL,0);
 SSM(sci,SCI_INDICSETSTYLE,INSIGHT_MATCH_FRAME,INDIC_BOX); SSM(sci,SCI_INDICSETFORE,INSIGHT_MATCH_FRAME,0x0000ff);
 SSM(sci,SCI_INDICSETSTROKEWIDTH,INSIGHT_MATCH_FRAME,100); SSM(sci,SCI_INDICSETUNDER,INSIGHT_MATCH_FRAME,FALSE);
 SSM(sci,SCI_INDICSETSTYLE,INSIGHT_SAME_TEXT,INDIC_STRAIGHTBOX); SSM(sci,SCI_INDICSETFORE,INSIGHT_SAME_TEXT,0x00ffff);
 SSM(sci,SCI_INDICSETALPHA,INSIGHT_SAME_TEXT,90); SSM(sci,SCI_INDICSETOUTLINEALPHA,INSIGHT_SAME_TEXT,0); SSM(sci,SCI_INDICSETUNDER,INSIGHT_SAME_TEXT,TRUE);
}
static void decorate_match(ScintillaObject *sci,gsize begin,gsize end) {
 decorate_setup(sci);
 /* The normal blue selection must not tint a selected search hit green. */
 SSM(sci,SCI_SETSELBACK,TRUE,0x00ffff); SSM(sci,SCI_SETSELFORE,FALSE,0); SSM(sci,SCI_SETSELALPHA,SC_ALPHA_NOALPHA,0);
 for(gint indicator=INSIGHT_MATCH_FILL;indicator<=INSIGHT_MATCH_FRAME;indicator++) {
  SSM(sci,SCI_SETINDICATORCURRENT,indicator,0); SSM(sci,SCI_INDICATORCLEARRANGE,0,sci_get_length(sci));
  if(end>begin && end<=(gsize)sci_get_length(sci)) SSM(sci,SCI_INDICATORFILLRANGE,begin,end-begin);
 }
}
static void decorate_selection(ScintillaObject *sci) {
 gint length=sci_get_length(sci),begin=(gint)SSM(sci,SCI_GETSELECTIONSTART,0,0),end=(gint)SSM(sci,SCI_GETSELECTIONEND,0,0);
 SSM(sci,SCI_SETINDICATORCURRENT,INSIGHT_SAME_TEXT,0);
 if(length<0 || length>2*1024*1024 || end<=begin || end-begin>1024) { SSM(sci,SCI_INDICATORCLEARRANGE,0,MAX(0,length)); g_object_set_data(G_OBJECT(sci),"insight-selection-state",NULL); return; }
 gchar *text=sci_get_contents(sci,length+1),*query=g_strndup(text+begin,(gsize)(end-begin));
 gchar *digest=g_compute_checksum_for_data(G_CHECKSUM_SHA256,(const guchar *)text,(gsize)length),*state=g_strdup_printf("%s:%d:%d",digest,begin,end); g_free(digest);
 if(g_strcmp0(state,g_object_get_data(G_OBJECT(sci),"insight-selection-state"))) {
  decorate_setup(sci); SSM(sci,SCI_SETINDICATORCURRENT,INSIGHT_SAME_TEXT,0); SSM(sci,SCI_INDICATORCLEARRANGE,0,length);
  InsightSearchMatch matches[10000]; gsize total=0;
  if(insight_context_collect(text,(gsize)length,query,INSIGHT_SEARCH_CASE,matches,G_N_ELEMENTS(matches),&total,NULL))
   for(gsize i=0;i<MIN(total,G_N_ELEMENTS(matches));i++) SSM(sci,SCI_INDICATORFILLRANGE,matches[i].start,matches[i].end-matches[i].start);
  g_object_set_data_full(G_OBJECT(sci),"insight-selection-state",state,g_free); state=NULL;
 }
 g_free(state); g_free(query); g_free(text);
}
static void preview_mark_symbol(const char *name) {
 if(!name || !*name) return;
 gint line=sci_get_current_line(wb.preview),begin=sci_get_position_from_line(wb.preview,line); gchar *text=sci_get_line(wb.preview,line);
 InsightSearchMatch match; gsize total=0;
 if(insight_context_collect(text,strlen(text),name,INSIGHT_SEARCH_CASE|INSIGHT_SEARCH_WORD,&match,1,&total,NULL) && total) { SSM(wb.preview,SCI_SETSEL,(gsize)begin+match.start,(gsize)begin+match.end); decorate_match(wb.preview,(gsize)begin+match.start,(gsize)begin+match.end); }
 g_free(text);
}
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
 decorate_match(wb.preview,0,0); preview_links();
 }
 target=(gint)MIN(line ? line-1 : 0,(gulong)MAX(0,sci_get_line_count(wb.preview)-1));
 SSM(wb.preview,SCI_GOTOLINE,target,0);
 SSM(wb.preview,SCI_VERTICALCENTRECARET,0,0);
}
static void refresh_index(void) {
 if(!wb.index_dirty) return;
 InsightIndex *next=insight_index_clone(wb.base); if(!next) return;
 gchar *root=wb.workspace_from_project && wb.root ? utils_get_utf8_from_locale(wb.root) : NULL;
 for(guint i=0;i<documents_array->len;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);
  if(doc->is_valid && doc->file_name && (!root || insight_project_files_contains(wb.membership,root,doc->file_name)) && !insight_native_import(next,doc->tm_file,doc->file_name)) { insight_index_free(next); g_free(root); return; }
 }
 g_free(root); insight_index_free(wb.index); wb.index=next; wb.index_dirty=FALSE;
}
static void preview_links(void) {
 if(!wb.preview) return;
 refresh_index();
 SSM(wb.preview,SCI_INDICSETSTYLE,INSIGHT_LINK,INDIC_PLAIN); SSM(wb.preview,SCI_INDICSETFORE,INSIGHT_LINK,0xcc5500);
 SSM(wb.preview,SCI_SETINDICATORCURRENT,INSIGHT_LINK,0); SSM(wb.preview,SCI_INDICATORCLEARRANGE,0,sci_get_length(wb.preview));
 GHashTable *names=g_hash_table_new(g_str_hash,g_str_equal); const InsightSymbol *page[256]; gsize total=insight_index_count(wb.index);
 for(gsize offset=0;offset<total;offset+=G_N_ELEMENTS(page)) {
  insight_index_query(wb.index,"",FALSE,NULL,NULL,NULL,offset,page,G_N_ELEMENTS(page));
  for(gsize i=0;i<MIN(total-offset,G_N_ELEMENTS(page));i++) g_hash_table_add(names,(gpointer)page[i]->name);
 }
 gchar *text=sci_get_contents(wb.preview,sci_get_length(wb.preview)+1);
 /* Use exactly the same word boundaries as Ctrl+click/F12, including the
  * language's configurable wordchars and Scintilla's Unicode categories. */
 for(gint position=0,length=sci_get_length(wb.preview);position<length;) {
  gint end=(gint)SSM(wb.preview,SCI_WORDENDPOSITION,position,TRUE);
  if(end<=position) { position=(gint)SSM(wb.preview,SCI_POSITIONAFTER,position,0); continue; }
  gint start=(gint)SSM(wb.preview,SCI_WORDSTARTPOSITION,position,TRUE); gchar *name=g_strndup(text+start,(gsize)(end-start));
  if(g_hash_table_contains(names,name)) SSM(wb.preview,SCI_INDICATORFILLRANGE,start,end-start);
  g_free(name);
  position=end;
 }
 g_free(text); g_hash_table_destroy(names);
}
static gboolean preview_motion(GtkWidget *widget,GdkEventMotion *event,gpointer data) {
 (void)widget; (void)data; gint position=(gint)SSM(wb.preview,SCI_POSITIONFROMPOINTCLOSE,(uptr_t)event->x,(sptr_t)event->y);
 /* Embedded Scintilla Platform::Window::Cursor::hand is 8. */
 SSM(wb.preview,SCI_SETCURSOR,position>=0 && SSM(wb.preview,SCI_INDICATORVALUEAT,INSIGHT_LINK,position) ? 8 : SC_CURSORNORMAL,0); return FALSE;
}
static void preview_notify(ScintillaObject *sci,gint message,SCNotification *notification,gpointer data) {
 (void)message; (void)data;
 if(notification->nmhdr.code==SCN_UPDATEUI && (notification->updated&SC_UPDATE_SELECTION)) decorate_selection(sci);
}
static void stop_scan(void) {
 if(wb.scan) { g_source_remove(wb.scan); wb.scan=0; }
 if(wb.frames) g_ptr_array_set_size(wb.frames,0);
 if(wb.scan_files) g_ptr_array_set_size(wb.scan_files,0);
 if(wb.scan_trees) g_ptr_array_set_size(wb.scan_trees,0);
 if(wb.scan_seen) g_hash_table_remove_all(wb.scan_seen);
}
static gboolean rescan_changed(gpointer data) {
 (void)data; wb.watch_idle=0; workspace_changed(NULL); return G_SOURCE_REMOVE;
}
static void workspace_changed(gpointer data) {
 (void)data;
 if(!wb.root) return;
 if(wb.scan) { wb.watch_changed=TRUE; return; }
 if(!start_scan(wb.root)) ui_set_statusbar(TRUE,"Workspace changed but source folder is unavailable; previous index retained");
}
static void watch_directory(const char *path) {
 GError *error=NULL;
 if(!insight_workspace_watch_add(wb.watch,path,&error)) wb.watch_incomplete=TRUE;
 g_clear_error(&error);
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
  for(size_t i=0;i<256 && offset+i<total;i++) if(!g_utf8_validate(page[i]->name,-1,NULL) || !g_utf8_validate(page[i]->path,-1,NULL) || !g_utf8_validate(page[i]->scope,-1,NULL) || !g_utf8_validate(page[i]->kind,-1,NULL) || !g_utf8_validate(page[i]->signature,-1,NULL) || !g_utf8_validate(page[i]->type,-1,NULL) || !g_utf8_validate(page[i]->inheritance,-1,NULL) || !g_path_is_absolute(page[i]->path)) { valid=FALSE; break; }
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
static void scan_source_file(const char *path,gboolean explicit_file) {
 if(g_hash_table_contains(wb.scan_seen,path)) return;
 g_hash_table_add(wb.scan_seen,g_strdup(path));
 GStatBuf st; if(g_lstat(path,&st) || !S_ISREG(st.st_mode) || st.st_size>2*1024*1024) { wb.skipped++; return; }
 gchar *utf8=utils_get_utf8_from_locale(path),*root=utils_get_utf8_from_locale(wb.root);
 if(wb.workspace_from_project && !insight_project_files_contains(wb.membership,root,utf8)) { g_free(root); g_free(utf8); return; }
 g_free(root); GeanyFiletype *ft=filetypes_detect_from_file(utf8); gboolean known=ft && ft->id!=GEANY_FILETYPES_NONE;
 gchar *decoded=(known || explicit_file) ? read_disk_source(utf8,TRUE) : NULL;
 if((known || explicit_file) && decoded) { wb.parsed++; g_ptr_array_add(wb.files,g_strdup(utf8)); }
 else if(known || explicit_file) { wb.skipped++; wb.scan_failed=TRUE; }
 if(decoded && ft && ft->lang>=0) {
  TMSourceFile *file=tm_source_file_new(path,ft->name);
  if(file) { file->lang=ft->lang; if(insight_native_parse(wb.base,file,utf8,decoded,strlen(decoded))) wb.index_dirty=TRUE; else { wb.scan_failed=TRUE; wb.skipped++; } tm_source_file_free(file); }
  else { wb.scan_failed=TRUE; wb.skipped++; }
 }
 if(wb.parsed && wb.parsed%25==0) { files_filter(NULL,NULL); ui_set_statusbar(TRUE,"Workspace: indexing… %u files",wb.parsed); }
 g_free(decoded); g_free(utf8);
}
static gboolean scan_step(gpointer data) {
 (void)data;
 if((!wb.frames->len && wb.scan_file_position>=wb.scan_files->len && wb.scan_tree_position>=wb.scan_trees->len) || wb.parsed>=10000) {
  wb.scan=0; wb.relation_dirty=TRUE; g_ptr_array_set_size(wb.frames,0); g_ptr_array_set_size(wb.scan_files,0); g_ptr_array_set_size(wb.scan_trees,0); g_hash_table_remove_all(wb.scan_seen); refresh_index();
  if(wb.parsed<10000 && !wb.scan_failed) save_index_cache();
  ui_set_statusbar(TRUE,"Workspace: %u files, %zu symbols, %u skipped%s",wb.parsed,insight_index_count(wb.index),wb.skipped,wb.parsed>=10000 ? "; file limit reached" : (wb.watch_incomplete ? "; directory monitoring incomplete" : ""));
  browser_filter(NULL,NULL); files_filter(NULL,NULL); tree_refresh(); file_types_refresh(); categories_filter(NULL,NULL); local_filter(NULL,NULL); insight_workbench_refresh();
  if(wb.watch_changed && !wb.watch_idle) { wb.watch_changed=FALSE; wb.watch_idle=g_timeout_add(2000,rescan_changed,NULL); }
  return G_SOURCE_REMOVE;
 }
 if(wb.scan_file_position<wb.scan_files->len) { scan_source_file(g_ptr_array_index(wb.scan_files,wb.scan_file_position++),TRUE); return G_SOURCE_CONTINUE; }
 if(!wb.frames->len) {
  const char *path=g_ptr_array_index(wb.scan_trees,wb.scan_tree_position++);
  if(!g_hash_table_contains(wb.scan_seen,path)) { GDir *dir=g_dir_open(path,0,NULL); if(dir) { ScanFrame *extra=g_new0(ScanFrame,1); extra->path=g_strdup(path); extra->dir=dir; g_hash_table_add(wb.scan_seen,g_strdup(path)); g_ptr_array_add(wb.frames,extra); watch_directory(path); } else { wb.scan_failed=TRUE; wb.skipped++; } }
  return G_SOURCE_CONTINUE;
 }
 ScanFrame *frame=g_ptr_array_index(wb.frames,wb.frames->len-1); const char *name=g_dir_read_name(frame->dir);
 if(!name) { g_ptr_array_remove_index(wb.frames,wb.frames->len-1); return G_SOURCE_CONTINUE; }
 if(name[0]=='.' || !strcmp(name,"node_modules") || !strcmp(name,"__pycache__")) return G_SOURCE_CONTINUE;
 gchar *path=g_build_filename(frame->path,name,NULL); GStatBuf st;
 if(g_lstat(path,&st)) { wb.scan_failed=TRUE; wb.skipped++; g_free(path); return G_SOURCE_CONTINUE; }
 if(S_ISDIR(st.st_mode)) {
  gchar *profile=g_canonicalize_filename(app->configdir,NULL); gboolean own_profile=!g_strcmp0(path,profile); g_free(profile);
  if(own_profile || g_hash_table_contains(wb.scan_seen,path)) { g_free(path); return G_SOURCE_CONTINUE; }
  GDir *dir=wb.frames->len<64 ? g_dir_open(path,0,NULL) : NULL;
  if(dir) { g_hash_table_add(wb.scan_seen,g_strdup(path)); watch_directory(path); ScanFrame *next=g_new0(ScanFrame,1); next->path=path; next->dir=dir; g_ptr_array_add(wb.frames,next); }
  else { wb.scan_failed=TRUE; wb.skipped++; g_free(path); }
  return G_SOURCE_CONTINUE;
 }
 scan_source_file(path,FALSE); g_free(path); return G_SOURCE_CONTINUE;
}
static gboolean start_scan(const char *root) {
 gchar *owned=g_canonicalize_filename(root,NULL); GDir *dir=g_dir_open(owned,0,NULL); InsightIndex *next=insight_index_new();
 if(!dir || !next) { if(dir) g_dir_close(dir); insight_index_free(next); g_free(owned); return FALSE; }
 stop_scan();
 if(wb.watch_idle) { g_source_remove(wb.watch_idle); wb.watch_idle=0; }
 insight_workspace_watch_clear(wb.watch); wb.watch_changed=FALSE; wb.watch_incomplete=FALSE; watch_directory(owned);
 insight_index_free(wb.base); wb.base=next; g_free(wb.root); wb.root=g_strdup(owned);
 g_ptr_array_set_size(wb.files,0); ScanFrame *frame=g_new0(ScanFrame,1); frame->path=owned; frame->dir=dir;
 if(!wb.workspace_from_project || wb.membership->automatic) { g_ptr_array_add(wb.frames,frame); g_hash_table_add(wb.scan_seen,g_strdup(owned)); } else frame_free(frame);
 if(wb.workspace_from_project) {
  for(guint i=0;i<wb.membership->files->len;i++) { const char *path=g_ptr_array_index(wb.membership->files,i); gchar *native=utils_get_locale_from_utf8(path),*parent=g_path_get_dirname(native); g_ptr_array_add(wb.scan_files,native); watch_directory(parent); g_free(parent); }
  for(guint i=0;i<wb.membership->trees->len;i++) g_ptr_array_add(wb.scan_trees,utils_get_locale_from_utf8(g_ptr_array_index(wb.membership->trees,i)));
 }
 wb.scan_file_position=wb.scan_tree_position=0; wb.parsed=wb.skipped=0; wb.index_dirty=TRUE; wb.scan_failed=FALSE; wb.scan=g_idle_add(scan_step,NULL); ui_set_statusbar(TRUE,"Workspace: indexing source folder…"); return TRUE;
}
static void choose_root(GtkButton *button,gpointer data) {
 (void)button; (void)data;
 GtkWidget *dialog=gtk_file_chooser_dialog_new("Open source workspace",GTK_WINDOW(main_widgets.window),GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)); gboolean previous=wb.workspace_from_project; wb.workspace_from_project=FALSE; if(!start_scan(path)) { wb.workspace_from_project=previous; ui_set_statusbar(TRUE,"Cannot index source folder"); } g_free(path); }
 gtk_widget_destroy(dialog);
}
static void cancel_scan(GtkButton *button,gpointer data) { (void)button; (void)data; stop_scan(); insight_workspace_watch_clear(wb.watch); wb.watch_changed=FALSE; if(wb.watch_idle) { g_source_remove(wb.watch_idle); wb.watch_idle=0; } refresh_index(); browser_filter(NULL,NULL); files_filter(NULL,NULL); tree_refresh(); file_types_refresh(); categories_filter(NULL,NULL); local_filter(NULL,NULL); insight_workbench_refresh(); ui_set_statusbar(TRUE,"Workspace scan cancelled; partial index retained"); }
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
  gchar *label=g_strdup_printf("%s %s%s • %s — %s:%lu",tag->type,word,tag->signature,tag->scope,tag->path,tag->line);
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
static InsightLookupFilter *entry_filter(GtkWidget *entry,gboolean files) {
 GError *error=NULL; InsightLookupFilter *filter=insight_lookup_filter_new(gtk_entry_get_text(GTK_ENTRY(entry)),files,&error);
 gtk_entry_set_icon_from_icon_name(GTK_ENTRY(entry),GTK_ENTRY_ICON_SECONDARY,error ? "dialog-warning" : (*gtk_entry_get_text(GTK_ENTRY(entry)) ? "edit-clear-symbolic" : NULL));
 gtk_entry_set_icon_tooltip_text(GTK_ENTRY(entry),GTK_ENTRY_ICON_SECONDARY,error ? error->message : NULL);
 g_clear_error(&error); return filter;
}
static void fill_symbol_list(GtkListStore *store,GtkWidget *entry,const char *path,guint limit) {
 refresh_index(); gtk_list_store_clear(store); InsightLookupFilter *filter=entry_filter(entry,FALSE); if(!filter) return; guint shown=0;
 size_t total=insight_index_query(wb.index,"",FALSE,NULL,NULL,path,0,NULL,0);
 const InsightSymbol **found=g_new(const InsightSymbol *,total);
 insight_index_query(wb.index,"",FALSE,NULL,NULL,path,0,found,total);
 for(size_t i=0;i<total && shown<limit;i++) {
  if(!insight_lookup_filter_match(filter,found[i]->name,found[i]->scope,found[i]->path,found[i]->kind)) continue;
  GtkTreeIter iter; gtk_list_store_append(store,&iter); gtk_list_store_set(store,&iter,SYMBOL_NAME,found[i]->name,SYMBOL_SCOPE,found[i]->scope,SYMBOL_PATH,found[i]->path,SYMBOL_LINE,(gulong)found[i]->line,SYMBOL_KIND,found[i]->kind,-1); shown++;
 }
 g_free(found); insight_lookup_filter_free(filter);
}
static void browser_filter(GtkEditable *entry,gpointer data) {
 (void)entry; (void)data; if(!wb.symbols) return;
 fill_symbol_list(wb.symbols,wb.symbol_entry,NULL,500);
}
static void browser_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data; if(!wb.preview) return;
 GtkTreeIter iter; GtkTreeModel *model;
 if(!gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 gchar *path,*name; gulong line; gtk_tree_model_get(model,&iter,SYMBOL_PATH,&path,SYMBOL_NAME,&name,SYMBOL_LINE,&line,-1);
 if(!line) { g_free(path); g_free(name); return; }
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); if(show_location(path,model==GTK_TREE_MODEL(wb.local_symbols) ? wb.local_doc_id : 0,line,TRUE)) preview_mark_symbol(name); g_free(path); g_free(name);
}
static void browser_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; (void)data;
 GtkTreeSelection *selection=gtk_tree_view_get_selection(view);
 gtk_tree_selection_select_path(selection,path);
 if(GTK_WIDGET(view)==wb.symbol_view || GTK_WIDGET(view)==wb.local_view || GTK_WIDGET(view)==wb.category_view) browser_selected(selection,NULL);
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
 InsightLookupFilter *filter=entry_filter(wb.file_entry,TRUE); if(!filter) { gtk_list_store_clear(wb.file_store); return; }
 GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal); gtk_list_store_clear(wb.file_store); guint shown=0;
 for(guint i=0;i<wb.files->len+(wb.workspace_from_project ? 0 : documents_array->len) && shown<10000;i++) {
  const gchar *path=NULL;
  if(i<wb.files->len) path=g_ptr_array_index(wb.files,i);
  else { GeanyDocument *doc=g_ptr_array_index(documents_array,i-wb.files->len); if(doc->is_valid) path=doc->file_name; }
  if(!path || !g_utf8_validate(path,-1,NULL) || g_hash_table_contains(seen,path)) continue;
  g_hash_table_add(seen,(gpointer)path);
  if(insight_lookup_filter_match(filter,NULL,NULL,path,NULL)) { GtkTreeIter iter; gchar *name=g_path_get_basename(path); gtk_list_store_append(wb.file_store,&iter); gtk_list_store_set(wb.file_store,&iter,0,name,1,path,-1); g_free(name); shown++; }
 }
 g_hash_table_destroy(seen); insight_lookup_filter_free(filter);
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
 wb.file_store=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_STRING); wb.file_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.file_store)); gtk_widget_set_name(wb.file_view,"insight-project-files");
 for(int i=0;i<2;i++) { GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(i ? "Path" : "File",gtk_cell_renderer_text_new(),"text",i,NULL); gtk_tree_view_column_set_sort_column_id(column,i); gtk_tree_view_append_column(GTK_TREE_VIEW(wb.file_view),column); }
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.file_store),0,GTK_SORT_ASCENDING);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.file_view);
 gtk_box_pack_start(GTK_BOX(box),wb.file_entry,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 GtkWidget *sort=gtk_combo_box_text_new(); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sort),"Sort: file name"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sort),"Sort: full path");
 g_signal_connect(sort,"changed",G_CALLBACK(file_sort),NULL); gtk_combo_box_set_active(GTK_COMBO_BOX(sort),0); gtk_box_pack_start(GTK_BOX(box),sort,FALSE,FALSE,0);
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
enum { USAGE_DISPLAY,USAGE_PATH,USAGE_LINE,USAGE_START,USAGE_END,USAGE_ID,USAGE_DIGEST,USAGE_SYMBOL,USAGE_SNIPPET,USAGE_COLUMNS };
static void stop_usage(void) {
 if(wb.usage_idle) { g_source_remove(wb.usage_idle); wb.usage_idle=0; }
 if(wb.usage_paths) g_ptr_array_set_size(wb.usage_paths,0);
 g_clear_pointer(&wb.usage_name,g_free);
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
static gboolean usage_step(gpointer data) {
 (void)data;
 if(wb.usage_position>=wb.usage_paths->len || wb.usage_shown>=2000) {
  wb.usage_idle=0;
  gchar *label=g_strdup_printf("%u results • %u files scanned • %u skipped%s",wb.usage_shown,wb.usage_position,wb.usage_skipped,wb.usage_shown>=2000 ? " • result limit reached" : "");
  set_usage_status(label); g_free(label); return G_SOURCE_REMOVE;
 }
 Location *location=g_ptr_array_index(wb.usage_paths,wb.usage_position++);
 GeanyDocument *doc=location->id ? document_find_by_id(location->id) : document_find_by_filename(location->path);
 char *text=NULL; guint id=doc ? doc->id : 0;
 if(doc) { gint length=sci_get_length(doc->editor->sci); if(length>=0 && length<=2*1024*1024) { text=sci_get_contents(doc->editor->sci,length+1); if(text && (!g_utf8_validate(text,length,NULL) || memchr(text,0,(gsize)length))) g_clear_pointer(&text,g_free); } }
 else if(location->path) text=read_disk_source(location->path,TRUE);
 InsightOccurrence *found=g_new(InsightOccurrence,2000-wb.usage_shown); size_t total=0;
 char *mask=NULL; gboolean code=wb.usage_mode==0 && wb.usage_code_only;
 if(text && code) {
  GeanyFiletype *ft=doc ? doc->file_type : (location->path ? filetypes_detect_from_file(location->path) : NULL); InsightCodeDialect dialect;
  if(source_dialect(ft,&dialect)) mask=insight_source_code_mask(text,strlen(text),dialect,NULL);
 }
 const char *search_text=code ? mask : text;
 gboolean valid=FALSE;
 if(search_text && wb.usage_mode==3) {
  InsightSearchMatch *matches=g_new(InsightSearchMatch,2000-wb.usage_shown);
  valid=wb.usage_keywords ? insight_keyword_collect(search_text,strlen(search_text),wb.usage_name,wb.usage_flags,wb.usage_context,matches,2000-wb.usage_shown,&total,NULL) : insight_context_collect(search_text,strlen(search_text),wb.usage_name,wb.usage_flags,matches,2000-wb.usage_shown,&total,NULL);
  if(valid) for(size_t i=0;i<MIN(total,2000-wb.usage_shown);i++) found[i]=(InsightOccurrence){matches[i].start,matches[i].end,matches[i].line,matches[i].column};
  g_free(matches);
 } else if(search_text) valid=insight_occurrences_find(search_text,strlen(search_text),wb.usage_name,strlen(wb.usage_name),0,found,2000-wb.usage_shown,&total);
 if(valid) {
  gchar *digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);
  for(size_t i=0;i<total && wb.usage_shown<2000;i++) {
   GtkTreeIter iter; gtk_list_store_append(wb.usage_target,&iter);
   gtk_list_store_set(wb.usage_target,&iter,USAGE_DISPLAY,location->path ? location->path : "untitled",USAGE_PATH,location->path,USAGE_LINE,found[i].line,USAGE_START,(gulong)found[i].start,USAGE_END,(gulong)found[i].end,USAGE_ID,id,USAGE_DIGEST,digest,USAGE_SYMBOL,wb.usage_name,-1);
   gchar *snippet=reference_snippet(text,found[i].start); gtk_list_store_set(wb.usage_target,&iter,USAGE_SNIPPET,snippet,-1); g_free(snippet); wb.usage_shown++;
  }
  g_free(digest);
 } else wb.usage_skipped++;
 g_free(mask); g_free(found); g_free(text);
 gchar *label=g_strdup_printf("Searching… %u results • %u/%u files",wb.usage_shown,wb.usage_position,wb.usage_paths->len);
 set_usage_status(label); g_free(label); return G_SOURCE_CONTINUE;
}
static gboolean search_path_matches(const char *path) {
 const char *input=gtk_entry_get_text(GTK_ENTRY(wb.search_mask)); if(!*input) return TRUE;
 gchar **patterns=g_strsplit(input,";",-1),*name=g_path_get_basename(path),*folded=g_utf8_casefold(name,-1); gboolean matched=FALSE;
 for(guint i=0;patterns[i];i++) { gchar *pattern=g_utf8_casefold(g_strstrip(patterns[i]),-1); if(*pattern && g_pattern_match_simple(pattern,folded)) matched=TRUE; g_free(pattern); }
 g_free(folded); g_free(name); g_strfreev(patterns); return matched;
}
static void usage_start(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; stop_usage();
 wb.usage_code_only=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_code));
 const char *query=gtk_entry_get_text(GTK_ENTRY(wb.usage_entry)); size_t total;
 gint mode=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)); wb.usage_mode=mode;
 wb.usage_target=mode==3 ? wb.search_matches : wb.matches;
 if(mode==1 || mode==2 || mode==4 || mode==5) {
  gtk_list_store_clear(wb.matches); refresh_index(); const InsightSymbol *found[2000];
  total=insight_relation_query(wb.index,query,mode==1 ? INSIGHT_RELATION_DEFINITIONS : (mode==2 ? INSIGHT_RELATION_MEMBERS : (mode==4 ? INSIGHT_RELATION_BASES : INSIGHT_RELATION_DERIVED)),0,found,G_N_ELEMENTS(found));
  if(total==SIZE_MAX) { set_usage_status("Enter a symbol name (or qualified owner for Members)"); return; }
  for(size_t i=0;i<MIN(total,G_N_ELEMENTS(found));i++) { const InsightSymbol *tag=found[i]; GtkTreeIter iter; gtk_list_store_append(wb.matches,&iter); gchar *description=g_strdup_printf("%s • %s %s%s • %s • bases: %s",tag->kind,tag->type,tag->name,tag->signature,tag->scope,tag->inheritance);
   gtk_list_store_set(wb.matches,&iter,USAGE_DISPLAY,tag->path,USAGE_PATH,tag->path,USAGE_LINE,tag->line,USAGE_SYMBOL,tag->name,USAGE_SNIPPET,description,-1); g_free(description);
  }
  gchar *label=g_strdup_printf("%zu tag-based %s%s",total,mode==1 ? "definitions/declarations" : (mode==2 ? "direct members" : (mode==4 ? "base definitions" : "derived types")),total>2000 ? " • showing first 2000" : ""); set_usage_status(label); g_free(label); return;
 }
 wb.usage_flags=(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.project_case)) ? INSIGHT_SEARCH_CASE : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.project_word)) ? INSIGHT_SEARCH_WORD : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.project_regex)) ? INSIGHT_SEARCH_REGEX : 0);
 GError *query_error=NULL;
 wb.usage_keywords=mode==3 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.search_keywords)); wb.usage_context=(guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(wb.search_context));
 if(wb.usage_keywords && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.search_variations))) wb.usage_flags|=INSIGHT_KEYWORD_VARIATIONS;
 if(mode==3 && !(wb.usage_keywords ? insight_keyword_collect("",0,query,wb.usage_flags,wb.usage_context,NULL,0,&total,&query_error) : insight_context_collect("",0,query,wb.usage_flags,NULL,0,&total,&query_error))) { set_usage_status(query_error ? query_error->message : "Invalid text search query"); g_clear_error(&query_error); return; }
 if(mode!=3 && !insight_occurrences_find("",0,query,strlen(query),0,NULL,0,&total)) { set_usage_status("Enter one UTF-8 identifier"); return; }
 gtk_list_store_clear(wb.usage_target);
 if(mode==3) { gtk_entry_set_text(GTK_ENTRY(wb.project_query),query); insight_search_history_record(wb.search_history,query); save_search_settings(); focus_search_results(NULL,NULL); }
 wb.usage_name=g_strdup(query); wb.usage_position=wb.usage_shown=wb.usage_skipped=0;
 GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);
 gint scope=mode==3 ? gtk_combo_box_get_active(GTK_COMBO_BOX(wb.search_scope)) : (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_buffer)) ? 2 : 0);
 GPtrArray *paths=mode==3 && scope==3 ? wb.search_files : wb.files;
 if(scope==0 || scope==3) {
  for(guint i=0;i<paths->len && wb.usage_paths->len<10000;i++) {
   const char *path=g_ptr_array_index(paths,i); if(g_hash_table_contains(seen,path) || (mode==3 && !search_path_matches(path))) continue;
   Location *next=g_new0(Location,1); next->path=g_strdup(path); g_ptr_array_add(wb.usage_paths,next); g_hash_table_add(seen,next->path);
  }
 }
 for(guint i=0;i<documents_array->len && wb.usage_paths->len<10000;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);
  if(!doc->is_valid || (scope==2 && doc!=document_get_current()) || (mode==3 && (scope==0 || scope==3)) || (mode==3 && doc->file_name && !search_path_matches(doc->file_name))) continue;
  if(doc->file_name && g_hash_table_contains(seen,doc->file_name)) continue;
  Location *next=g_new0(Location,1); next->path=g_strdup(doc->file_name); next->id=doc->id; g_ptr_array_add(wb.usage_paths,next);
  if(next->path) g_hash_table_add(seen,next->path);
 }
 g_hash_table_destroy(seen); wb.usage_idle=g_idle_add(usage_step,NULL);
}
static void usage_cancel(GtkWidget *widget,gpointer data) { (void)widget; (void)data; stop_usage(); set_usage_status("Search cancelled; partial results retained"); }
static void usage_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data; GtkTreeIter iter; GtkTreeModel *model;
 if(!gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 gchar *path,*digest; gulong line,start,end; guint id;
 gtk_tree_model_get(model,&iter,USAGE_PATH,&path,USAGE_LINE,&line,USAGE_START,&start,USAGE_END,&end,USAGE_ID,&id,USAGE_DIGEST,&digest,-1);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
 if(show_location(path,id,line,TRUE)) {
  gchar *text=sci_get_contents(wb.preview,sci_get_length(wb.preview)+1),*actual=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);
  if(!digest) { /* Definitions and members navigate by current tag location. */ }
  else if(!g_strcmp0(actual,digest)) { SSM(wb.preview,SCI_SETSEL,end,start); decorate_match(wb.preview,start,end); SSM(wb.preview,SCI_VERTICALCENTRECARET,0,0); }
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
static void relation_controls(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; if(wb.relation_setting) return; wb.relation_dirty=TRUE; stop_usage();
 gboolean references=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==0;
 gtk_widget_set_sensitive(wb.usage_code,references); gtk_widget_set_sensitive(wb.usage_buffer,references || gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==3);
 if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_follow))) insight_workbench_refresh();
 else if(*gtk_entry_get_text(GTK_ENTRY(wb.usage_entry))) usage_start(NULL,NULL);
 else { gtk_list_store_clear(wb.matches); set_usage_status("Enter a symbol for Relations"); }
}
static void relation_update_from_editor(gboolean dirty) {
 if(wb.usage_idle && wb.usage_mode==3) return;
 if(!wb.relation_follow || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_follow))) return;
 GeanyDocument *doc=document_get_current(); gchar word[1025]={0};
 if(doc) {
  editor_find_current_word_sciwc(doc->editor,sci_get_current_position(doc->editor->sci),word,sizeof word);
  refresh_index();
  if(!*word || !insight_index_find(wb.index,word,TRUE,NULL,0)) {
   gulong line=(gulong)sci_get_current_line(doc->editor->sci)+1;TMTag *selected=NULL;guint best=0;
   if(doc->tm_file && doc->tm_file->tags_array) for(guint i=0;i<doc->tm_file->tags_array->len;i++) {
    TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,i);
    if(!tag || tag->line!=line || !tag->name || !g_utf8_validate(tag->name,-1,NULL) || strlen(tag->name)>=sizeof word) continue;
    guint rank=(tag->kind_letter && strchr("fmcsi",tag->kind_letter)) ? 2 : 1;
    if(!selected || rank>best) { selected=tag;best=rank; }
   }
   if(selected) g_strlcpy(word,selected->name,sizeof word);
  }
 }
 gint mode=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode));
 if(!dirty && !wb.relation_dirty && mode==wb.relation_last_mode && !g_strcmp0(word,wb.relation_word)) return;
 wb.relation_dirty=FALSE; wb.relation_last_mode=mode; g_free(wb.relation_word); wb.relation_word=g_strdup(word);
 gtk_entry_set_text(GTK_ENTRY(wb.usage_entry),word);
 if(*word) usage_start(NULL,NULL);
 else { stop_usage(); gtk_list_store_clear(wb.matches); set_usage_status("Relations — no selected identifier"); }
}
static void create_usage_browser(void) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4),*controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);
 wb.usage_entry=gtk_search_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.usage_entry),"Symbol or qualified owner…");
 wb.usage_status=gtk_label_new("Comments and strings included; no semantic scope resolution"); gtk_label_set_line_wrap(GTK_LABEL(wb.usage_status),TRUE);
 wb.usage_buffer=gtk_check_button_new_with_label("Current buffer only");
 wb.usage_code=gtk_check_button_new_with_label("References: code only");
 wb.relation_follow=gtk_check_button_new_with_label("Follow symbol in editor");
 wb.relation_mode=gtk_combo_box_text_new();
 const char *modes[]={"References (lexical)","Definitions / declarations","Direct members (tags)","Text search (project)","Base types (tags)","Derived types (tags)"};
 for(guint i=0;i<G_N_ELEMENTS(modes);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.relation_mode),modes[i]);
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),0);
 GtkWidget *search=gtk_button_new_with_label("Search"),*cancel=gtk_button_new_with_label("Stop");
 gtk_box_pack_start(GTK_BOX(controls),search,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(controls),cancel,FALSE,FALSE,0);
 wb.matches=gtk_list_store_new(USAGE_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);
 wb.usage_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.matches));
 gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view),gtk_tree_view_column_new_with_attributes("Symbol",gtk_cell_renderer_text_new(),"text",USAGE_SYMBOL,NULL));
 gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view),gtk_tree_view_column_new_with_attributes("File",gtk_cell_renderer_text_new(),"text",USAGE_DISPLAY,NULL));
 gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view),gtk_tree_view_column_new_with_attributes("Line",gtk_cell_renderer_text_new(),"text",USAGE_LINE,NULL));
 gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view),gtk_tree_view_column_new_with_attributes("Source / kind • scope",gtk_cell_renderer_text_new(),"text",USAGE_SNIPPET,NULL));
 const gint columns[]={USAGE_SYMBOL,USAGE_DISPLAY,USAGE_LINE,USAGE_SNIPPET};
 const gint widths[]={100,150,55,260};
 for(guint i=0;i<G_N_ELEMENTS(columns);i++) {
  GtkTreeViewColumn *column=gtk_tree_view_get_column(GTK_TREE_VIEW(wb.usage_view),(gint)i);
  gtk_tree_view_column_set_sort_column_id(column,columns[i]); gtk_tree_view_column_set_sizing(column,GTK_TREE_VIEW_COLUMN_FIXED); gtk_tree_view_column_set_fixed_width(column,widths[i]); gtk_tree_view_column_set_resizable(column,TRUE);
  GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column)); for(GList *cell=cells;cell;cell=cell->next) g_object_set(cell->data,"ellipsize",PANGO_ELLIPSIZE_END,NULL); g_list_free(cells);
 }
 gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.usage_view),USAGE_SNIPPET);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.usage_view);
 gtk_box_pack_start(GTK_BOX(box),wb.usage_entry,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.relation_mode,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.relation_follow,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.usage_code,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.usage_buffer,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),controls,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.usage_status,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),box,gtk_label_new("Relations"));
 g_signal_connect(search,"clicked",G_CALLBACK(usage_start),NULL); g_signal_connect(cancel,"clicked",G_CALLBACK(usage_cancel),NULL); g_signal_connect(wb.usage_entry,"activate",G_CALLBACK(usage_start),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.usage_view)),"changed",G_CALLBACK(usage_selected),NULL); g_signal_connect(wb.usage_view,"row-activated",G_CALLBACK(usage_activated),NULL);
 g_signal_connect(wb.usage_entry,"key-press-event",G_CALLBACK(browser_key),NULL);
 g_signal_connect(wb.usage_view,"key-press-event",G_CALLBACK(browser_key),NULL);
 g_signal_connect(wb.relation_mode,"changed",G_CALLBACK(relation_controls),NULL);
 g_signal_connect(wb.relation_follow,"toggled",G_CALLBACK(relation_controls),NULL);
 g_signal_connect(wb.usage_code,"toggled",G_CALLBACK(relation_controls),NULL);
 g_signal_connect(wb.usage_buffer,"toggled",G_CALLBACK(relation_controls),NULL);
 gtk_widget_show_all(box);
}
static void create_browser(void) {
 wb.browser=gtk_box_new(GTK_ORIENTATION_VERTICAL,4); wb.symbol_entry=gtk_search_entry_new();
 gtk_entry_set_placeholder_text(GTK_ENTRY(wb.symbol_entry),"Find project symbol…");
 wb.symbols=gtk_list_store_new(SYMBOL_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING);
 wb.symbol_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.symbols)); gtk_widget_set_name(wb.symbol_view,"insight-project-symbols");
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
 InsightLookupFilter *filter=entry_filter(wb.local_entry,FALSE); if(!filter) return; guint shown=0;
 for(guint i=0;i<doc->tm_file->tags_array->len && shown<2000;i++) {
  TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,i); if(!tag || !tag->name || !tag->line || !g_utf8_validate(tag->name,-1,NULL)) continue;
  gchar kind[2]={tag->kind_letter,0}; if(!insight_lookup_filter_match(filter,tag->name,tag->scope && g_utf8_validate(tag->scope,-1,NULL) ? tag->scope : "",doc->file_name,kind)) continue; GtkTreeIter iter; gtk_list_store_append(wb.local_symbols,&iter);
  gtk_list_store_set(wb.local_symbols,&iter,SYMBOL_NAME,tag->name,SYMBOL_SCOPE,tag->scope && g_utf8_validate(tag->scope,-1,NULL) ? tag->scope : "",SYMBOL_PATH,doc->file_name,SYMBOL_LINE,(gulong)tag->line,SYMBOL_KIND,kind,-1); shown++;
 }
 insight_lookup_filter_free(filter);
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
 gtk_notebook_set_show_tabs(GTK_NOTEBOOK(main_widgets.sidebar_notebook),TRUE);
 g_signal_connect(wb.local_entry,"changed",G_CALLBACK(local_filter),NULL); g_signal_connect(wb.local_entry,"activate",G_CALLBACK(browser_entry_activate),NULL);
 g_signal_connect(wb.local_entry,"key-press-event",G_CALLBACK(browser_key),NULL); g_signal_connect(wb.local_view,"key-press-event",G_CALLBACK(browser_key),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.local_view)),"changed",G_CALLBACK(browser_selected),NULL);
 g_signal_connect(wb.local_view,"row-activated",G_CALLBACK(browser_activated),NULL); gtk_widget_show_all(box);
 gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),gtk_notebook_page_num(GTK_NOTEBOOK(main_widgets.sidebar_notebook),box)); local_filter(NULL,NULL);
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
static void file_types_refresh(void) {
 if(!wb.file_types) return;
 gtk_tree_store_clear(wb.file_types); GHashTable *groups=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
 for(guint i=0;i<wb.files->len && i<10000;i++) {
  const char *path=g_ptr_array_index(wb.files,i); GeanyFiletype *type=filetypes_detect_from_extension(path); const char *name=type ? type->name : "Other";
  GtkTreeIter *group=g_hash_table_lookup(groups,name);
  if(!group) { group=g_new(GtkTreeIter,1); gtk_tree_store_append(wb.file_types,group,NULL); gtk_tree_store_set(wb.file_types,group,0,name,2,TRUE,-1); g_hash_table_insert(groups,g_strdup(name),group); }
  gchar *base=g_path_get_basename(path); GtkTreeIter row; gtk_tree_store_append(wb.file_types,&row,group); gtk_tree_store_set(wb.file_types,&row,0,base,1,path,2,FALSE,-1); g_free(base);
 }
 g_hash_table_destroy(groups);
}
static void create_file_types(void) {
 wb.file_types=gtk_tree_store_new(3,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN); GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.file_types)); wb.types_view=view;
 gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("File Types",gtk_cell_renderer_text_new(),"text",0,NULL)); gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(view),1);
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.file_types),0,GTK_SORT_ASCENDING);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),view); gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),scroll,gtk_label_new("File Types"));
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),"changed",G_CALLBACK(tree_selected),NULL); g_signal_connect(view,"row-activated",G_CALLBACK(tree_activated),NULL); g_signal_connect(view,"key-press-event",G_CALLBACK(browser_key),NULL); gtk_widget_show_all(scroll); file_types_refresh();
}
static gboolean notes_save(void) {
 gchar *directory=g_path_get_dirname(wb.notes_path); GError *error=NULL; gboolean saved=FALSE;
 if(g_mkdir_with_parents(directory,0700)) ui_set_statusbar(TRUE,"Notes directory could not be created");
 else { saved=insight_notes_save(wb.notes,wb.notes_path,&error); if(!saved) ui_set_statusbar(TRUE,"Notes not saved: %s",error ? error->message : "write failed"); }
 g_clear_error(&error); g_free(directory); return saved;
}
static void notes_filter(GtkEditable *entry,gpointer data) {
 (void)entry; InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data); GtkListStore *store=kind==INSIGHT_BOOKMARK ? wb.marks_store : wb.clips_store; GtkWidget *field=kind==INSIGHT_BOOKMARK ? wb.marks_entry : wb.clips_entry;
 if(!store) return;
 gtk_list_store_clear(store); gchar *needle=g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(field)),-1);
 for(guint i=0;i<wb.notes->items->len;i++) { InsightNote *note=g_ptr_array_index(wb.notes->items,i); if(note->kind!=kind) continue;
  gchar *line=g_strdup_printf("%"G_GUINT64_FORMAT,note->line),*search=g_strconcat(note->name," ",note->path," ",line,NULL),*folded=g_utf8_casefold(search,-1); gboolean match=strstr(folded,needle)!=NULL; g_free(line); g_free(search); g_free(folded); if(!match) continue;
  GtkTreeIter iter; gtk_list_store_append(store,&iter); gtk_list_store_set(store,&iter,0,note->name,1,note->path,2,(gulong)note->line,3,note->text,-1);
 }
 g_free(needle);
}
static gchar *selected_note(InsightNoteKind kind) {
 GtkWidget *view=kind==INSIGHT_BOOKMARK ? wb.marks_view : wb.clips_view; GtkTreeModel *model; GtkTreeIter iter;
 if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&model,&iter)) return NULL;
 gchar *name; gtk_tree_model_get(model,&iter,0,&name,-1); return name;
}
static gboolean bookmark_preview(const InsightNote *note) {
 if(!note || !wb.preview) return FALSE;
 GeanyDocument *doc=document_find_by_filename(note->path); gchar *text=NULL;
 if(doc && sci_get_length(doc->editor->sci)<=2*1024*1024) text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1);
 else if(!doc) text=read_disk_source(note->path,TRUE);
 guint64 line=0; gboolean ambiguous=FALSE; gboolean found=text && insight_bookmark_resolve(note,text,strlen(text),&line,&ambiguous); g_free(text);
 if(!found) { g_clear_pointer(&wb.source_path,g_free); wb.source_id=0; wb.source_line=0; navigation_state(); ui_set_statusbar(TRUE,ambiguous ? "Bookmark line is ambiguous after edits" : "Bookmark source changed or is unavailable"); return FALSE; }
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); return show_location(note->path,doc ? doc->id : 0,(gulong)line,TRUE);
}
static void note_selected(GtkTreeSelection *selection,gpointer data) {
 (void)selection; InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data); gchar *name=selected_note(kind); if(!name || !wb.preview) { g_free(name); return; }
 const InsightNote *note=insight_notes_get(wb.notes,kind,name);
 if(note && kind==INSIGHT_BOOKMARK) bookmark_preview(note);
 else if(note) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.context_toggle),TRUE); GeanyDocument *doc=document_get_current(); render_source(note->text,doc ? doc->file_type : NULL,1); g_clear_pointer(&wb.source_path,g_free); g_clear_pointer(&wb.source_digest,g_free); wb.source_id=0; wb.source_line=0; navigation_state(); gchar *label=g_strdup_printf("Clip — %s",note->name); gtk_label_set_text(GTK_LABEL(wb.title),label); g_free(label); }
 g_free(name);
}
static void bookmark_add(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; GeanyDocument *doc=document_get_current(); if(!doc || !doc->file_name) { ui_set_statusbar(TRUE,"Save the file before adding a persistent bookmark"); return; }
 gint line=sci_get_current_line(doc->editor->sci); gchar *anchor=sci_get_line(doc->editor->sci,line); gsize n=strlen(anchor); while(n && (anchor[n-1]=='\r' || anchor[n-1]=='\n')) anchor[--n]=0;
 const char *input=gtk_entry_get_text(GTK_ENTRY(wb.marks_entry)); gchar *name=*input ? g_strdup(input) : g_strdup_printf("Bookmark-%"G_GINT64_FORMAT,g_get_real_time()); GError *error=NULL;
 if(insight_notes_set(wb.notes,INSIGHT_BOOKMARK,name,doc->file_name,(guint64)line+1,anchor,&error)) { notes_filter(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK)); notes_save(); }
 else ui_set_statusbar(TRUE,"Bookmark: %s",error ? error->message : "not stored");
 g_clear_error(&error); g_free(anchor); g_free(name);
}
static void note_delete(GtkWidget *widget,gpointer data) {
 (void)widget; InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data); gchar *name=selected_note(kind); if(name && insight_notes_remove(wb.notes,kind,name)) { notes_filter(NULL,data); notes_save(); } g_free(name);
}
static void bookmark_go(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; gchar *name=selected_note(INSIGHT_BOOKMARK); if(!name) { const char *query=gtk_entry_get_text(GTK_ENTRY(wb.marks_entry)); if(insight_notes_get(wb.notes,INSIGHT_BOOKMARK,query)) name=g_strdup(query); }
 if(name) { if(bookmark_preview(insight_notes_get(wb.notes,INSIGHT_BOOKMARK,name))) open_context(NULL,NULL); g_free(name); } else bookmark_add(NULL,NULL);
}
static void clip_capture(GtkWidget *widget,gpointer data) {
 (void)widget; gboolean cut=GPOINTER_TO_INT(data); GeanyDocument *doc=document_get_current(); if(!doc) return;
 if(cut && SSM(doc->editor->sci,SCI_GETREADONLY,0,0)) { ui_set_statusbar(TRUE,"Current source is read-only"); return; }
 gsize selected_length=sci_get_selected_text_length2(doc->editor->sci);
 if(selected_length>65536) { ui_set_statusbar(TRUE,"Clip selection exceeds 64 KiB"); return; }
 gchar *text=sci_get_selection_contents(doc->editor->sci);
 if(text && strlen(text)!=selected_length) { g_free(text); ui_set_statusbar(TRUE,"Clips cannot contain NUL bytes"); return; }
 if(!text || !*text) { g_free(text); ui_set_statusbar(TRUE,"Select source text to copy into a clip"); return; }
 const char *input=gtk_entry_get_text(GTK_ENTRY(wb.clips_entry)); gchar *name=*input ? g_strdup(input) : g_strdup_printf("Clip-%"G_GINT64_FORMAT,g_get_real_time()); GError *error=NULL;
 if(insight_notes_set(wb.notes,INSIGHT_CLIP,name,NULL,0,text,&error)) { gboolean saved=notes_save(); if(cut && saved) { sci_start_undo_action(doc->editor->sci); sci_replace_sel(doc->editor->sci,""); sci_end_undo_action(doc->editor->sci); } notes_filter(NULL,GINT_TO_POINTER(INSIGHT_CLIP)); }
 else ui_set_statusbar(TRUE,"Clip: %s",error ? error->message : "not stored");
 g_clear_error(&error); g_free(text); g_free(name);
}
static void clip_paste(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; gchar *name=selected_note(INSIGHT_CLIP); const InsightNote *note=name ? insight_notes_get(wb.notes,INSIGHT_CLIP,name) : NULL; GeanyDocument *doc=document_get_current();
 if(note && doc && !SSM(doc->editor->sci,SCI_GETREADONLY,0,0)) { sci_start_undo_action(doc->editor->sci); sci_replace_sel(doc->editor->sci,note->text); sci_end_undo_action(doc->editor->sci); document_grab_focus(doc); }
 else ui_set_statusbar(TRUE,"Select a clip and a writable source document");
 g_free(name);
}
static void clip_edit(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; gchar *name=selected_note(INSIGHT_CLIP); const InsightNote *note=name ? insight_notes_get(wb.notes,INSIGHT_CLIP,name) : NULL; if(!note) { g_free(name); return; }
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Edit clip",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Save",GTK_RESPONSE_ACCEPT,NULL),*view=gtk_text_view_new(),*scroll=gtk_scrolled_window_new(NULL,NULL);
 gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),note->text,-1); gtk_container_add(GTK_CONTAINER(scroll),view); gtk_widget_set_size_request(scroll,600,350); gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),scroll); gtk_widget_show_all(dialog);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)); GtkTextIter a,b; gtk_text_buffer_get_bounds(buffer,&a,&b); gchar *text=gtk_text_buffer_get_text(buffer,&a,&b,FALSE); GError *error=NULL;
  if(insight_notes_set(wb.notes,INSIGHT_CLIP,name,NULL,0,text,&error)) { notes_filter(NULL,GINT_TO_POINTER(INSIGHT_CLIP)); notes_save(); } else ui_set_statusbar(TRUE,"Clip: %s",error ? error->message : "not stored"); g_clear_error(&error); g_free(text); }
 gtk_widget_destroy(dialog); g_free(name);
}
static void note_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data); gtk_tree_selection_select_path(gtk_tree_view_get_selection(view),path);
 if(kind==INSIGHT_BOOKMARK) bookmark_go(NULL,NULL); else clip_paste(NULL,NULL);
}
/* Commit a prepared snapshot before refreshing the panel. On write failure the
 * previous in-memory notes and selection remain available. Consumes candidate. */
static gboolean notes_commit(InsightNotes *candidate,InsightNoteKind kind) {
 InsightNotes *previous=wb.notes; wb.notes=candidate;
 if(!notes_save()) { wb.notes=previous; insight_notes_free(candidate); return FALSE; }
 insight_notes_free(previous); notes_filter(NULL,GINT_TO_POINTER(kind)); return TRUE;
}
static void note_rename(GtkWidget *widget,gpointer data) {
 (void)widget; InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data); gchar *name=selected_note(kind);
 if(!name) { ui_set_statusbar(TRUE,"Select a note to rename"); return; }
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Rename note",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Rename",GTK_RESPONSE_ACCEPT,NULL),*entry=gtk_entry_new();
 gtk_entry_set_text(GTK_ENTRY(entry),name); gtk_entry_set_activates_default(GTK_ENTRY(entry),TRUE); gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT); gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),entry,FALSE,FALSE,8); gtk_widget_show(entry);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
  GError *error=NULL; InsightNotes *candidate=insight_notes_rename(wb.notes,kind,name,gtk_entry_get_text(GTK_ENTRY(entry)),&error);
  if(candidate) { if(notes_commit(candidate,kind)) ui_set_statusbar(TRUE,"Note renamed"); }
  else ui_set_statusbar(TRUE,"Rename: %s",error ? error->message : "failed");
  g_clear_error(&error);
 }
 gtk_widget_destroy(dialog); g_free(name);
}
static void notes_transfer(GtkWidget *widget,gpointer data) {
 (void)widget; gint action=GPOINTER_TO_INT(data); gboolean exporting=(action&256)!=0;
 InsightNoteKind kind=(InsightNoteKind)(action&255);
 GtkWidget *dialog=gtk_file_chooser_dialog_new(exporting ? "Export notes" : "Import notes",GTK_WINDOW(main_widgets.window),
  exporting ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,
  exporting ? "Export" : "Import",GTK_RESPONSE_ACCEPT,NULL);
 if(exporting) { gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),TRUE); gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),kind==INSIGHT_BOOKMARK ? "bookmarks.conf" : "clips.conf"); }
 GtkFileFilter *filter=gtk_file_filter_new(); gtk_file_filter_set_name(filter,"Insight notes (*.conf)"); gtk_file_filter_add_pattern(filter,"*.conf"); gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);
 filter=gtk_file_filter_new(); gtk_file_filter_set_name(filter,"All files"); gtk_file_filter_add_pattern(filter,"*"); gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
  gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)); GError *error=NULL; InsightNotes *snapshot=NULL; gboolean ok=FALSE;
  if(exporting) { snapshot=insight_notes_select(wb.notes,kind,&error); if(snapshot) ok=insight_notes_save(snapshot,path,&error); }
  else {
   snapshot=insight_notes_load(path,&error);
   if(snapshot) {
    InsightNotes *merged=insight_notes_merge(wb.notes,snapshot,kind,&error);
    if(merged) ok=notes_commit(merged,kind);
   }
  }
  if(ok) ui_set_statusbar(TRUE,exporting ? "Notes exported" : "Notes imported; existing notes preserved");
  else if(error) ui_set_statusbar(TRUE,"Notes %s: %s",exporting ? "export" : "import",error->message);
  insight_notes_free(snapshot); g_clear_error(&error); g_free(path);
 }
 gtk_widget_destroy(dialog);
}
static void create_note_panel(InsightNoteKind kind) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3),*field=gtk_search_entry_new(),*controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2); GtkListStore *store=gtk_list_store_new(4,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING); GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
 if(kind==INSIGHT_BOOKMARK) { wb.marks_entry=field; wb.marks_store=store; wb.marks_view=view; } else { wb.clips_entry=field; wb.clips_store=store; wb.clips_view=view; }
 gtk_entry_set_placeholder_text(GTK_ENTRY(field),kind==INSIGHT_BOOKMARK ? "Bookmark name or filter…" : "Clip name or filter…"); gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Name",gtk_cell_renderer_text_new(),"text",0,NULL));
 if(kind==INSIGHT_BOOKMARK) { gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Line",gtk_cell_renderer_text_new(),"text",2,NULL)); gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(view),1); }
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),0,GTK_SORT_ASCENDING); GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),view);
 gtk_box_pack_start(GTK_BOX(box),field,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),controls,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 const char *marks[]={"Add/update","Go to","Delete"},*clips[]={"Copy selection","Cut selection","Paste","Edit","Delete"}; guint count=kind==INSIGHT_BOOKMARK ? 3 : 5;
 for(guint i=0;i<count;i++) { const char *icons[]={"edit-copy","edit-cut","edit-paste","document-edit","edit-delete"}; GtkWidget *button=kind==INSIGHT_BOOKMARK ? gtk_button_new_with_label(marks[i]) : gtk_button_new_from_icon_name(icons[i],GTK_ICON_SIZE_SMALL_TOOLBAR); gtk_widget_set_tooltip_text(button,kind==INSIGHT_BOOKMARK ? marks[i] : clips[i]); gtk_box_pack_start(GTK_BOX(controls),button,FALSE,FALSE,0);
  GCallback callback; gpointer argument=NULL;
  if(kind==INSIGHT_BOOKMARK) { callback=i==0 ? G_CALLBACK(bookmark_add) : (i==1 ? G_CALLBACK(bookmark_go) : G_CALLBACK(note_delete)); if(i==2) argument=GINT_TO_POINTER(kind); }
  else { callback=i<2 ? G_CALLBACK(clip_capture) : (i==2 ? G_CALLBACK(clip_paste) : (i==3 ? G_CALLBACK(clip_edit) : G_CALLBACK(note_delete))); argument=i==1 ? GINT_TO_POINTER(1) : (i==4 ? GINT_TO_POINTER(kind) : NULL); }
  g_signal_connect(button,"clicked",callback,argument);
 }
 GtkWidget *transfer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);
 GtkWidget *rename=gtk_button_new_with_label("Rename…"); gtk_box_pack_start(GTK_BOX(transfer),rename,FALSE,FALSE,0); g_signal_connect(rename,"clicked",G_CALLBACK(note_rename),GINT_TO_POINTER(kind));
 for(guint i=0;i<2;i++) { GtkWidget *button=gtk_button_new_with_label(i ? "Export…" : "Import…"); gtk_box_pack_start(GTK_BOX(transfer),button,FALSE,FALSE,0); g_signal_connect(button,"clicked",G_CALLBACK(notes_transfer),GINT_TO_POINTER((gint)kind|(i ? 256 : 0))); }
 gtk_box_pack_start(GTK_BOX(box),transfer,FALSE,FALSE,0); gtk_box_reorder_child(GTK_BOX(box),transfer,2);
 gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),box,gtk_label_new(kind==INSIGHT_BOOKMARK ? "Bookmarks" : "Clips")); g_signal_connect(field,"changed",G_CALLBACK(notes_filter),GINT_TO_POINTER(kind)); g_signal_connect(field,"key-press-event",G_CALLBACK(browser_key),NULL); g_signal_connect(view,"key-press-event",G_CALLBACK(browser_key),NULL);
 if(kind==INSIGHT_BOOKMARK) g_signal_connect(field,"activate",G_CALLBACK(bookmark_go),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),"changed",G_CALLBACK(note_selected),GINT_TO_POINTER(kind)); g_signal_connect(view,"row-activated",G_CALLBACK(note_activated),GINT_TO_POINTER(kind)); gtk_widget_show_all(box); notes_filter(NULL,GINT_TO_POINTER(kind));
}
static void create_notes(void) {
 wb.notes_path=g_build_filename(app->configdir,"insight","notes.conf",NULL); wb.notes=insight_notes_new();
 if(g_file_test(wb.notes_path,G_FILE_TEST_EXISTS)) { GError *error=NULL; InsightNotes *loaded=insight_notes_load(wb.notes_path,&error); if(loaded) { insight_notes_free(wb.notes); wb.notes=loaded; } else ui_set_statusbar(TRUE,"Notes ignored: %s",error ? error->message : "invalid snapshot"); g_clear_error(&error); }
 create_note_panel(INSIGHT_BOOKMARK); create_note_panel(INSIGHT_CLIP);
}
static void category_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; (void)data; GtkTreeIter iter; GtkTreeModel *model=gtk_tree_view_get_model(view);
 if(!gtk_tree_model_get_iter(model,&iter,path)) return;
 gulong line; gtk_tree_model_get(model,&iter,SYMBOL_LINE,&line,-1);
 if(!line) { if(gtk_tree_view_row_expanded(view,path)) gtk_tree_view_collapse_row(view,path); else gtk_tree_view_expand_row(view,path,FALSE); return; }
 browser_activated(view,path,NULL,NULL);
}
static void categories_filter(GtkEditable *entry,gpointer data) {
 (void)entry; (void)data; if(!wb.categories) return;
 refresh_index(); gtk_tree_store_clear(wb.categories);
 size_t count=insight_index_count(wb.index); const InsightSymbol **symbols=g_new(const InsightSymbol *,count);
 insight_index_query(wb.index,"",FALSE,NULL,NULL,NULL,0,symbols,count);
 InsightLookupFilter *filter=entry_filter(wb.category_entry,FALSE); if(!filter) { g_free(symbols); return; }
 GHashTable *groups=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free); guint shown=0;
 for(size_t i=0;i<count && shown<5000;i++) {
  const InsightSymbol *symbol=symbols[i]; if(!insight_lookup_filter_match(filter,symbol->name,symbol->scope,symbol->path,symbol->kind)) continue;
  GtkTreeIter *category=g_hash_table_lookup(groups,symbol->kind);
  if(!category) { category=g_new(GtkTreeIter,1); gtk_tree_store_append(wb.categories,category,NULL); gchar *label=g_strdup_printf("Symbol kind: %s",symbol->kind); gtk_tree_store_set(wb.categories,category,SYMBOL_NAME,label,-1); g_free(label); g_hash_table_insert(groups,g_strdup(symbol->kind),category); }
  GtkTreeIter *parent=category; gchar *key=g_strdup_printf("%s:%s",symbol->kind,symbol->scope);
  if(*symbol->scope) {
   parent=g_hash_table_lookup(groups,key);
   if(!parent) { parent=g_new(GtkTreeIter,1); gtk_tree_store_append(wb.categories,parent,category); gtk_tree_store_set(wb.categories,parent,SYMBOL_NAME,symbol->scope,-1); g_hash_table_insert(groups,g_strdup(key),parent); }
  }
  g_free(key); GtkTreeIter row; gtk_tree_store_append(wb.categories,&row,parent); gtk_tree_store_set(wb.categories,&row,SYMBOL_NAME,symbol->name,SYMBOL_SCOPE,symbol->scope,SYMBOL_PATH,symbol->path,SYMBOL_LINE,(gulong)symbol->line,SYMBOL_KIND,symbol->kind,-1); shown++;
 }
 g_hash_table_destroy(groups); insight_lookup_filter_free(filter); g_free(symbols);
}
static void create_categories(void) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3); wb.category_entry=gtk_search_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.category_entry),"Find symbol category members…");
 wb.categories=gtk_tree_store_new(SYMBOL_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING); wb.category_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.categories));
 gtk_tree_view_append_column(GTK_TREE_VIEW(wb.category_view),gtk_tree_view_column_new_with_attributes("Symbol Categories",gtk_cell_renderer_text_new(),"text",SYMBOL_NAME,NULL));
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.categories),SYMBOL_NAME,GTK_SORT_ASCENDING); gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.category_view),SYMBOL_PATH);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.category_view); gtk_box_pack_start(GTK_BOX(box),wb.category_entry,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),box,gtk_label_new("Symbol Categories"));
 g_signal_connect(wb.category_entry,"changed",G_CALLBACK(categories_filter),NULL); g_signal_connect(wb.category_entry,"key-press-event",G_CALLBACK(browser_key),NULL); g_signal_connect(wb.category_view,"key-press-event",G_CALLBACK(browser_key),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.category_view)),"changed",G_CALLBACK(browser_selected),NULL); g_signal_connect(wb.category_view,"row-activated",G_CALLBACK(category_activated),NULL); gtk_widget_show_all(box); categories_filter(NULL,NULL);
}
static gboolean refresh_now(gpointer data) {
 (void)data; wb.refresh=0;
 GeanyDocument *active=document_get_current(); gboolean dirty=wb.index_dirty;
 if(active) { decorate_selection(active->editor->sci); gsize a=(gsize)SSM(active->editor->sci,SCI_GETSELECTIONSTART,0,0),b=(gsize)SSM(active->editor->sci,SCI_GETSELECTIONEND,0,0); if(b>a) decorate_match(active->editor->sci,a,b); else decorate_match(active->editor->sci,0,0); }
 if(wb.preview) decorate_selection(wb.preview);
 if(dirty || wb.local_doc_id!=(active ? active->id : 0)) local_filter(NULL,NULL);
 if(dirty) { browser_filter(NULL,NULL); categories_filter(NULL,NULL); }
 if(dirty) preview_links();
 relation_update_from_editor(dirty);
 if(!wb.preview || !wb.context_visible || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow))) return G_SOURCE_REMOVE;
 GeanyDocument *doc=document_get_current();
 if(!doc) { render_source("",NULL,1); g_clear_pointer(&wb.source_path,g_free); wb.source_id=0; wb.source_line=0; navigation_state(); gtk_label_set_text(GTK_LABEL(wb.title),"Context — no active source"); return G_SOURCE_REMOVE; }
 gchar word[1025]={0}; editor_find_current_word_sciwc(doc->editor,sci_get_current_position(doc->editor->sci),word,sizeof word);
 const InsightSymbol *definition=word_definition(doc,word,NULL);
 if(definition) {
  gchar *path=g_strdup(definition->path);
  gulong line=definition->line; if(show_location(path,0,line,FALSE)) preview_mark_symbol(word); g_free(path);
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
  navqueue_goto_line(old,doc,(gint)line);
  gsize a=(gsize)SSM(wb.preview,SCI_GETSELECTIONSTART,0,0),b=(gsize)SSM(wb.preview,SCI_GETSELECTIONEND,0,0);
  if(b>a) {
   gint first=(gint)SSM(wb.preview,SCI_LINEFROMPOSITION,a,0),last=(gint)SSM(wb.preview,SCI_LINEFROMPOSITION,b,0);
   gsize mapped_a=(gsize)sci_get_position_from_line(doc->editor->sci,first)+a-(gsize)sci_get_position_from_line(wb.preview,first);
   gsize mapped_b=(gsize)sci_get_position_from_line(doc->editor->sci,last)+b-(gsize)sci_get_position_from_line(wb.preview,last);
   if(mapped_b<=(gsize)sci_get_length(doc->editor->sci)) { SSM(doc->editor->sci,SCI_SETSEL,mapped_a,mapped_b); decorate_match(doc->editor->sci,mapped_a,mapped_b); }
  }
  SSM(doc->editor->sci,SCI_VERTICALCENTRECARET,0,0); document_grab_focus(doc);
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
  if(show_location(path,0,line,TRUE)) { preview_mark_symbol(word); open_context(NULL,NULL); } g_free(path);
 } else ui_set_statusbar(TRUE,"Definition not found: %s",word);
 return TRUE;
}
static void context_find(GtkWidget *widget,gpointer data) {
 (void)widget; gboolean forward=GPOINTER_TO_INT(data)!=-1; gchar *text=sci_get_contents(wb.preview,sci_get_length(wb.preview)+1); gsize size=strlen(text),begin,end; gboolean wrapped;
 const char *query=gtk_entry_get_text(GTK_ENTRY(wb.find_entry)); gchar *digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);
 gsize a=(gsize)SSM(wb.preview,SCI_GETSELECTIONSTART,0,0),b=(gsize)SSM(wb.preview,SCI_GETSELECTIONEND,0,0),from=forward ? b : a;
 guint flags=(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.find_case)) ? INSIGHT_SEARCH_CASE : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.find_word)) ? INSIGHT_SEARCH_WORD : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.find_regex)) ? INSIGHT_SEARCH_REGEX : 0);
 gboolean repeat=wb.find_last_flags==flags && !g_strcmp0(query,wb.find_last_query) && !g_strcmp0(digest,wb.find_last_digest) && a==wb.find_last_begin && b==wb.find_last_end;
 gboolean edge_wrap=FALSE; if(repeat && a==b) { edge_wrap=forward ? b==size : a==0; from=forward ? (b<size ? b+1 : 0) : (a ? a-1 : size); }

 GError *error=NULL; if(insight_context_find(text,size,query,from,forward,flags,&begin,&end,&wrapped,&error)) {
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); SSM(wb.preview,SCI_SETSEL,begin,end); decorate_match(wb.preview,begin,end); SSM(wb.preview,SCI_VERTICALCENTRECARET,0,0);
  gtk_label_set_text(GTK_LABEL(wb.find_status),(wrapped || edge_wrap) ? "Wrapped" : "Match"); g_free(wb.find_last_query); wb.find_last_query=g_strdup(query); g_free(wb.find_last_digest); wb.find_last_digest=g_strdup(digest); wb.find_last_begin=begin; wb.find_last_end=end; wb.find_last_flags=flags;
 } else gtk_label_set_text(GTK_LABEL(wb.find_status),error ? error->message : "No match");
 g_clear_error(&error); g_free(digest); g_free(text);
}
static void create_context_find(void) {
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); wb.find_entry=gtk_search_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.find_entry),"Find in Context…");
 gtk_box_pack_start(GTK_BOX(row),wb.find_entry,TRUE,TRUE,0); GtkWidget *previous=gtk_button_new_with_label("Previous"),*next=gtk_button_new_with_label("Next");
 gtk_box_pack_start(GTK_BOX(row),previous,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),next,FALSE,FALSE,0);
 wb.find_case=gtk_check_button_new_with_label("Case"); wb.find_word=gtk_check_button_new_with_label("Word"); wb.find_regex=gtk_check_button_new_with_label("Regex");
 gtk_box_pack_start(GTK_BOX(row),wb.find_case,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),wb.find_word,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),wb.find_regex,FALSE,FALSE,0);
 wb.find_status=gtk_label_new(""); gtk_label_set_ellipsize(GTK_LABEL(wb.find_status),PANGO_ELLIPSIZE_END); gtk_widget_set_size_request(wb.find_status,80,-1); gtk_box_pack_start(GTK_BOX(row),wb.find_status,FALSE,FALSE,0);
 g_signal_connect(previous,"clicked",G_CALLBACK(context_find),GINT_TO_POINTER(-1)); g_signal_connect(next,"clicked",G_CALLBACK(context_find),NULL); g_signal_connect(wb.find_entry,"activate",G_CALLBACK(context_find),NULL); g_signal_connect(wb.find_entry,"key-press-event",G_CALLBACK(browser_key),NULL);
 gtk_box_pack_start(GTK_BOX(wb.box),row,FALSE,FALSE,0);
}
static gboolean preview_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget; (void)data;
 if(event->keyval==GDK_KEY_Return && !(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK))) { open_context(NULL,NULL); return TRUE; }
 if(event->keyval==GDK_KEY_Escape) { editor_focus(); return TRUE; }
 if(event->keyval==GDK_KEY_f && (event->state&GEANY_PRIMARY_MOD_MASK)) { gtk_widget_grab_focus(wb.find_entry); return TRUE; }
 if(event->keyval==GDK_KEY_F3) { context_find(NULL,GINT_TO_POINTER(event->state&GDK_SHIFT_MASK ? -1 : 1)); return TRUE; }
 if(event->keyval==GDK_KEY_F12 && !(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SHIFT_MASK))) return preview_definition(sci_get_current_position(wb.preview));
 return FALSE;
}
static gboolean preview_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)widget; (void)data;
 if(event->button!=1) return FALSE;
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
 gint position=(gint)SSM(wb.preview,SCI_POSITIONFROMPOINTCLOSE,(uptr_t)event->x,(sptr_t)event->y);
 if(!(event->state&GEANY_PRIMARY_MOD_MASK) && (position<0 || !SSM(wb.preview,SCI_INDICATORVALUEAT,INSIGHT_LINK,position))) return FALSE;
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
 GList *children=gtk_container_get_children(GTK_CONTAINER(wb.lua_commands)); for(GList *p=children;p;p=p->next) gtk_widget_destroy(p->data); g_list_free(children);
 if(wb.lua_accels) { guint old_key; GdkModifierType old_mods; gtk_accelerator_parse("<Primary><Shift>p",&old_key,&old_mods); gtk_widget_remove_accelerator(wb.palette_item,wb.lua_accels,old_key,old_mods); gtk_window_remove_accel_group(GTK_WINDOW(main_widgets.window),wb.lua_accels); g_object_unref(wb.lua_accels); }
 wb.lua_accels=gtk_accel_group_new(); gtk_window_add_accel_group(GTK_WINDOW(main_widgets.window),wb.lua_accels);
 guint palette_key; GdkModifierType palette_mods; gtk_accelerator_parse("<Primary><Shift>p",&palette_key,&palette_mods);
 gtk_widget_add_accelerator(wb.palette_item,"activate",wb.lua_accels,palette_key,palette_mods,GTK_ACCEL_VISIBLE);
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
static void native_project_update(GObject *object,GKeyFile *config,gpointer data) {
 (void)object; if(!wb.preview || !app->project) return;
 gchar *utf8=project_get_base_path(); if(!utf8) return;
 gchar *native=utils_get_locale_from_utf8(utf8),*root=g_canonicalize_filename(native,NULL); g_free(native);
 gboolean changed=GPOINTER_TO_INT(data) || g_strcmp0(root,wb.geany_root) || g_strcmp0(wb.membership_project,app->project->file_name);
 if(changed) {
  GKeyFile *loaded=NULL; GError *error=NULL;
  if(!config) { loaded=g_key_file_new(); gchar *filename=utils_get_locale_from_utf8(app->project->file_name); if(!g_key_file_load_from_file(loaded,filename,G_KEY_FILE_NONE,&error) && g_error_matches(error,G_FILE_ERROR,G_FILE_ERROR_NOENT)) g_clear_error(&error); g_free(filename); config=loaded; }
  InsightProjectFiles *policy=error ? NULL : insight_project_files_read(config,utf8,&error);
  wb.membership_valid=policy!=NULL;
  if(!policy) { policy=insight_project_files_new(); policy->automatic=FALSE; ui_set_statusbar(TRUE,"Project membership could not be restored: %s",error ? error->message : "invalid settings"); }
  insight_project_files_free(wb.membership); wb.membership=policy; g_free(wb.membership_project); wb.membership_project=g_strdup(app->project->file_name); g_clear_error(&error); if(loaded) { g_key_file_unref(loaded); config=NULL; }
  wb.workspace_from_project=TRUE;
  if(start_scan(root)) { g_free(wb.geany_root); wb.geany_root=g_strdup(root); browser_filter(NULL,NULL); files_filter(NULL,NULL); tree_refresh(); categories_filter(NULL,NULL); file_types_refresh(); focus_widget_panel(wb.file_entry); }
  else ui_set_statusbar(TRUE,"Project source folder cannot be indexed: %s",root);
 }
 if(!GPOINTER_TO_INT(data) && config && wb.membership_valid) { GError *error=NULL; if(!insight_project_files_write(wb.membership,config,utf8,&error)) ui_set_statusbar(TRUE,"Cannot save project membership: %s",error ? error->message : "invalid rules"); g_clear_error(&error); }
 g_free(root); g_free(utf8);
}
static gboolean membership_apply(InsightProjectFiles *candidate) {
 if(!app->project || !wb.workspace_from_project) { ui_set_statusbar(TRUE,"Open a Geany project before changing its files"); return FALSE; }
 gchar *root=project_get_base_path(),*filename=utils_get_locale_from_utf8(app->project->file_name); GError *error=NULL;
 GFile *file=g_file_new_for_path(filename); GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_STANDARD_SIZE,G_FILE_QUERY_INFO_NONE,NULL,&error);
 gboolean ok=info && g_file_info_get_size(info)<=8*1024*1024;
 if(info) g_object_unref(info);
 if(!ok && !error) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Project configuration exceeds 8 MiB");
 GKeyFile *config=g_key_file_new();
 if(ok) ok=g_key_file_load_from_file(config,filename,G_KEY_FILE_KEEP_COMMENTS,&error);
 if(ok) ok=insight_project_files_write(candidate,config,root,&error);
 if(ok) { gsize length; gchar *data=g_key_file_to_data(config,&length,&error); ok=data!=NULL && length<=8*1024*1024; if(!ok && !error) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Project configuration exceeds 8 MiB"); if(ok) ok=g_file_replace_contents(file,data,length,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,&error); g_free(data); }
 if(ok) { if(wb.usage_idle) { stop_usage(); gtk_label_set_text(GTK_LABEL(wb.search_status),"Project files changed; run the search again"); } insight_project_files_free(wb.membership); wb.membership=insight_project_files_clone(candidate); wb.membership_valid=TRUE; start_scan(wb.root); files_filter(NULL,NULL); }
 else ui_set_statusbar(TRUE,"Project files not changed: %s",error ? error->message : "cannot save configuration");
 g_clear_error(&error); g_key_file_unref(config); g_object_unref(file); g_free(filename); g_free(root); return ok;
}
typedef struct { GtkWidget *dialog,*query,*automatic,*masks,*view; GtkListStore *store; InsightProjectFiles *candidate; GPtrArray *paths,*trees; gchar *root; } MembershipDialog;
static void membership_dialog_refresh(MembershipDialog *d) {
 gtk_list_store_clear(d->store); gchar *needle=g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(d->query)),-1);
 for(guint kind=0;kind<2;kind++) { GPtrArray *paths=kind ? d->trees : d->paths;
  for(guint i=0;i<paths->len;i++) { const char *path=g_ptr_array_index(paths,i); if(kind ? !g_ptr_array_find_with_equal_func(d->candidate->trees,path,(GEqualFunc)g_str_equal,NULL) : !insight_project_files_contains(d->candidate,d->root,path)) continue;
   gchar *folded=g_utf8_casefold(path,-1); gboolean match=strstr(folded,needle)!=NULL; g_free(folded); if(!match) continue;
   GtkTreeIter iter; gtk_list_store_append(d->store,&iter); gchar *name=g_path_get_basename(path); gtk_list_store_set(d->store,&iter,0,name,1,path,2,kind!=0,3,kind ? "Folder (indexed on Apply)" : "File",-1); g_free(name);
  }
 }
 g_free(needle);
}
static void membership_dialog_changed(GtkWidget *widget,gpointer data) {
 (void)widget; MembershipDialog *d=data; d->candidate->automatic=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->automatic)); g_free(d->candidate->masks); d->candidate->masks=g_strdup(gtk_entry_get_text(GTK_ENTRY(d->masks))); membership_dialog_refresh(d);
}
static void membership_dialog_add(GtkWidget *widget,gpointer data) {
 MembershipDialog *d=data; gboolean tree=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"membership-tree"));
 GtkWidget *chooser=gtk_file_chooser_dialog_new(tree ? "Add source tree" : "Add project files",GTK_WINDOW(d->dialog),tree ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,"Add",GTK_RESPONSE_ACCEPT,NULL);
 if(!tree) gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser),TRUE);
 if(gtk_dialog_run(GTK_DIALOG(chooser))==GTK_RESPONSE_ACCEPT) {
  GSList *paths=gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(chooser));
  for(GSList *p=paths;p;p=p->next) { gchar *utf8=utils_get_utf8_from_locale(p->data); GError *error=NULL;
   if(insight_project_files_set(d->candidate,utf8,tree,TRUE,&error)) { GPtrArray *target=tree ? d->trees : d->paths; if(!g_ptr_array_find_with_equal_func(target,utf8,(GEqualFunc)g_str_equal,NULL)) g_ptr_array_add(target,g_strdup(utf8)); }
   else ui_set_statusbar(TRUE,"Cannot add project path: %s",error ? error->message : "invalid path");
   g_clear_error(&error); g_free(utf8);
  }
  g_slist_free_full(paths,g_free); membership_dialog_refresh(d);
 }
 gtk_widget_destroy(chooser);
}
static void membership_dialog_file_list(GtkWidget *widget,gpointer data) {
 MembershipDialog *d=data; gboolean export=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"membership-export"));
 GtkWidget *chooser=gtk_file_chooser_dialog_new(export ? "Export project file list" : "Add from file list",GTK_WINDOW(d->dialog),export ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,export ? "Export" : "Add",GTK_RESPONSE_ACCEPT,NULL);
 if(export) { gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser),TRUE); gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser),"project-files.txt"); }
 if(gtk_dialog_run(GTK_DIALOG(chooser))==GTK_RESPONSE_ACCEPT) {
  gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser)); GError *error=NULL;
  if(export) {
   GString *text=g_string_new(NULL); GFile *base=g_file_new_for_path(d->root); GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);
   for(guint kind=0;kind<2;kind++) { GPtrArray *paths=kind ? d->candidate->trees : d->paths; for(guint i=0;i<paths->len;i++) { const char *p=g_ptr_array_index(paths,i); if(g_hash_table_contains(seen,p) || (!kind && !insight_project_files_contains(d->candidate,d->root,p))) continue; g_hash_table_add(seen,(gpointer)p); GFile *file=g_file_new_for_path(p); gchar *relative=g_file_get_relative_path(base,file); if(relative) for(gchar *c=relative;*c;c++) if(G_IS_DIR_SEPARATOR(*c)) *c='/'; g_string_append_printf(text,"%s\n",relative ? relative : (g_file_equal(base,file) ? "." : p)); g_free(relative); g_object_unref(file); } }
   GFile *file=g_file_new_for_path(path); if(!g_file_replace_contents(file,text->str,text->len,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,&error)) ui_set_statusbar(TRUE,"Cannot export project list: %s",error ? error->message : "write failed"); g_object_unref(file); g_hash_table_destroy(seen); g_object_unref(base); g_string_free(text,TRUE);
  } else {
   gchar *utf8=utils_get_utf8_from_locale(path),*text=read_disk_source(utf8,TRUE); g_free(utf8);
   if(!text) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"File list must be decodable text, at most 2 MiB");
   else {
    InsightProjectFiles *next=insight_project_files_clone(d->candidate); GPtrArray *added=g_ptr_array_new_with_free_func(g_free),*trees=g_ptr_array_new_with_free_func(g_free); gchar **lines=g_strsplit(g_str_has_prefix(text,"\xEF\xBB\xBF") ? text+3 : text,"\n",-1); guint count=0;
    for(guint i=0;lines[i] && !error;i++) { gsize length=strlen(lines[i]); if(length && lines[i][length-1]=='\r') lines[i][--length]=0; if(!length) continue;
     if(++count>10000) { g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"File list exceeds 10000 entries"); break; }
     gchar *p=g_canonicalize_filename(lines[i],d->root),*native=utils_get_locale_from_utf8(p); gboolean tree=g_file_test(native,G_FILE_TEST_IS_DIR); g_free(native);
     if(insight_project_files_set(next,p,tree,TRUE,&error)) g_ptr_array_add(tree ? trees : added,p); else g_free(p);
    }
    if(!error) { insight_project_files_free(d->candidate); d->candidate=next; next=NULL; GPtrArray *from[]={added,trees},*to[]={d->paths,d->trees}; for(guint kind=0;kind<2;kind++) for(guint i=0;i<from[kind]->len;i++) { const char *p=g_ptr_array_index(from[kind],i); if(!g_ptr_array_find_with_equal_func(to[kind],p,(GEqualFunc)g_str_equal,NULL)) g_ptr_array_add(to[kind],g_strdup(p)); } membership_dialog_refresh(d); }
    insight_project_files_free(next); g_ptr_array_free(added,TRUE); g_ptr_array_free(trees,TRUE); g_strfreev(lines); g_free(text);
   }
   if(error) ui_set_statusbar(TRUE,"Project list not imported: %s",error->message);
  }
  g_clear_error(&error); g_free(path);
 }
 gtk_widget_destroy(chooser);
}
static void membership_dialog_remove(GtkWidget *widget,gpointer data) {
 MembershipDialog *d=data; gint mode=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"membership-remove"));
 if(mode==2) { insight_project_files_free(d->candidate); d->candidate=insight_project_files_new(); d->candidate->automatic=FALSE; gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->automatic),FALSE); gtk_entry_set_text(GTK_ENTRY(d->masks),""); g_ptr_array_set_size(d->paths,0); g_ptr_array_set_size(d->trees,0); membership_dialog_refresh(d); return; }
 if(mode==1) {
  GtkWidget *chooser=gtk_file_chooser_dialog_new("Remove source tree from project",GTK_WINDOW(d->dialog),GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,"Cancel",GTK_RESPONSE_CANCEL,"Remove from project",GTK_RESPONSE_ACCEPT,NULL);
  if(gtk_dialog_run(GTK_DIALOG(chooser))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser)),*utf8=utils_get_utf8_from_locale(path); GError *error=NULL; if(!insight_project_files_set(d->candidate,utf8,TRUE,FALSE,&error)) ui_set_statusbar(TRUE,"Cannot remove project tree: %s",error ? error->message : "invalid path"); g_clear_error(&error); g_free(path); g_free(utf8); }
  gtk_widget_destroy(chooser);
 } else {
  GtkTreeModel *model; GList *rows=gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(d->view)),&model);
  for(GList *p=rows;p;p=p->next) { GtkTreeIter iter; if(gtk_tree_model_get_iter(model,&iter,p->data)) { gchar *path; gboolean tree; gtk_tree_model_get(model,&iter,1,&path,2,&tree,-1); GError *error=NULL; if(!insight_project_files_set(d->candidate,path,tree,FALSE,&error)) ui_set_statusbar(TRUE,"Cannot remove project path: %s",error ? error->message : "invalid path"); g_clear_error(&error); if(tree) for(guint i=d->trees->len;i>0;i--) if(!g_strcmp0(path,g_ptr_array_index(d->trees,i-1))) g_ptr_array_remove_index(d->trees,i-1); g_free(path); } }
  g_list_free_full(rows,(GDestroyNotify)gtk_tree_path_free);
 }
 membership_dialog_refresh(d);
}
static void manage_project_files(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; if(!app->project || !wb.workspace_from_project) { ui_set_statusbar(TRUE,"Open a Geany project before changing its files"); return; }
 MembershipDialog d={0}; d.candidate=insight_project_files_clone(wb.membership); d.paths=g_ptr_array_new_with_free_func(g_free); d.trees=g_ptr_array_new_with_free_func(g_free); d.root=project_get_base_path();
 for(guint i=0;i<wb.files->len;i++) g_ptr_array_add(d.paths,g_strdup(g_ptr_array_index(wb.files,i)));
 for(guint i=0;i<d.candidate->files->len;i++) { const char *path=g_ptr_array_index(d.candidate->files,i); if(!g_ptr_array_find_with_equal_func(d.paths,path,(GEqualFunc)g_str_equal,NULL)) g_ptr_array_add(d.paths,g_strdup(path)); }
 for(guint i=0;i<d.candidate->trees->len;i++) g_ptr_array_add(d.trees,g_strdup(g_ptr_array_index(d.candidate->trees,i)));
 d.dialog=gtk_dialog_new_with_buttons("Add and Remove Project Files",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Apply",GTK_RESPONSE_APPLY,NULL); gtk_window_set_default_size(GTK_WINDOW(d.dialog),760,520);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d.dialog)); gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Files are removed from the project only. Added folders are indexed after Apply."),FALSE,FALSE,4);
 d.query=gtk_search_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(d.query),"Filter project paths…"); gtk_box_pack_start(GTK_BOX(box),d.query,FALSE,FALSE,3);
 d.store=gtk_list_store_new(4,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN,G_TYPE_STRING); d.view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(d.store)); gtk_widget_set_name(d.view,"insight-project-membership-files");
 const char *labels[]={"Name","Path","Kind"}; const gint columns[]={0,1,3};
 for(guint i=0;i<3;i++) { GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(labels[i],gtk_cell_renderer_text_new(),"text",columns[i],NULL); gtk_tree_view_column_set_sort_column_id(column,columns[i]); gtk_tree_view_column_set_resizable(column,TRUE); gtk_tree_view_append_column(GTK_TREE_VIEW(d.view),column); }
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(d.store),0,GTK_SORT_ASCENDING); gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(d.view)),GTK_SELECTION_MULTIPLE); GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),d.view); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,3);
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); const char *buttons[]={"Add files…","Add tree…","Remove selected","Remove tree…","Remove all"};
 for(guint i=0;i<5;i++) { GtkWidget *button=gtk_button_new_with_label(buttons[i]); gtk_box_pack_start(GTK_BOX(row),button,FALSE,FALSE,0); if(i<2) { g_object_set_data(G_OBJECT(button),"membership-tree",GINT_TO_POINTER(i)); g_signal_connect(button,"clicked",G_CALLBACK(membership_dialog_add),&d); } else { g_object_set_data(G_OBJECT(button),"membership-remove",GINT_TO_POINTER(i-2)); g_signal_connect(button,"clicked",G_CALLBACK(membership_dialog_remove),&d); } }
 gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,3); d.automatic=gtk_check_button_new_with_label("Automatically include known source files under the project base folder"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d.automatic),d.candidate->automatic); gtk_box_pack_start(GTK_BOX(box),d.automatic,FALSE,FALSE,3);
 GtkWidget *lists=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); for(guint i=0;i<2;i++) { GtkWidget *button=gtk_button_new_with_label(i ? "Export file list…" : "Add from list…"); g_object_set_data(G_OBJECT(button),"membership-export",GUINT_TO_POINTER(i)); g_signal_connect(button,"clicked",G_CALLBACK(membership_dialog_file_list),&d); gtk_box_pack_start(GTK_BOX(lists),button,FALSE,FALSE,0); } gtk_box_pack_start(GTK_BOX(box),lists,FALSE,FALSE,3);
 d.masks=gtk_entry_new(); gtk_entry_set_max_length(GTK_ENTRY(d.masks),4096); gtk_entry_set_placeholder_text(GTK_ENTRY(d.masks),"Exclude masks: *.generated.c;vendor/* (case sensitive)"); gtk_entry_set_text(GTK_ENTRY(d.masks),d.candidate->masks); gtk_box_pack_start(GTK_BOX(box),d.masks,FALSE,FALSE,3);
 g_signal_connect(d.query,"changed",G_CALLBACK(membership_dialog_changed),&d); g_signal_connect(d.masks,"changed",G_CALLBACK(membership_dialog_changed),&d); g_signal_connect(d.automatic,"toggled",G_CALLBACK(membership_dialog_changed),&d); membership_dialog_refresh(&d); gtk_widget_show_all(d.dialog);
 while(gtk_dialog_run(GTK_DIALOG(d.dialog))==GTK_RESPONSE_APPLY) if(membership_apply(d.candidate)) break;
 gtk_widget_destroy(d.dialog); g_object_unref(d.store); insight_project_files_free(d.candidate); g_ptr_array_free(d.paths,TRUE); g_ptr_array_free(d.trees,TRUE); g_free(d.root);
}
static void project_files_drop(GtkWidget *widget,GdkDragContext *context,gint x,gint y,GtkSelectionData *selection,guint info,guint time,gpointer data) {
 (void)widget; (void)x; (void)y; (void)info; (void)data; gboolean ok=FALSE;
 if(app->project && wb.workspace_from_project) { InsightProjectFiles *candidate=insight_project_files_clone(wb.membership); gchar **uris=gtk_selection_data_get_uris(selection); guint count=0; GError *error=NULL;
  for(guint i=0;uris && uris[i] && !error;i++) { gchar *path=g_filename_from_uri(uris[i],NULL,&error); if(path) { gchar *utf8=utils_get_utf8_from_locale(path); if(insight_project_files_set(candidate,utf8,g_file_test(path,G_FILE_TEST_IS_DIR),TRUE,&error)) count++; g_free(utf8); g_free(path); } }
  if(!error && count) ok=membership_apply(candidate); else if(error) ui_set_statusbar(TRUE,"Cannot add dropped project files: %s",error->message);
  g_clear_error(&error); g_strfreev(uris); insight_project_files_free(candidate);
 } else ui_set_statusbar(TRUE,"Open a Geany project before dropping project files");
 gtk_drag_finish(context,ok,FALSE,time);
}
static void native_project_close(GObject *object,gpointer data) {
 (void)object; (void)data; g_clear_pointer(&wb.geany_root,g_free); g_clear_pointer(&wb.membership_project,g_free); wb.membership_valid=FALSE;
 if(!wb.workspace_from_project) return;
 wb.workspace_from_project=FALSE; stop_scan(); insight_workspace_watch_clear(wb.watch); wb.watch_changed=FALSE; if(wb.watch_idle) { g_source_remove(wb.watch_idle); wb.watch_idle=0; } insight_index_free(wb.base); wb.base=insight_index_new(); wb.index_dirty=TRUE; g_ptr_array_set_size(wb.files,0); g_clear_pointer(&wb.root,g_free);
 if(wb.cache_path) g_unlink(wb.cache_path);
 refresh_index(); browser_filter(NULL,NULL); files_filter(NULL,NULL); tree_refresh(); categories_filter(NULL,NULL); file_types_refresh(); insight_workbench_refresh();
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
static void focus_widget_panel(GtkWidget *widget) {
 GtkWidget *owner=gtk_widget_get_toplevel(widget); if(GTK_IS_WINDOW(owner) && owner!=main_widgets.window) { gtk_widget_show(owner); gtk_window_present(GTK_WINDOW(owner)); }
 GtkWidget *page=widget; while(page && gtk_widget_get_parent(page) && !GTK_IS_NOTEBOOK(gtk_widget_get_parent(page)) && !GTK_IS_WINDOW(gtk_widget_get_parent(page))) page=gtk_widget_get_parent(page);
 if(page && GTK_IS_NOTEBOOK(gtk_widget_get_parent(page))) { GtkWidget *book=gtk_widget_get_parent(page); gtk_widget_show(book); gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),page)); }
 gtk_widget_grab_focus(widget);
}
static void focus_browser(GtkMenuItem *item,gpointer data) {
 (void)item; gint which=GPOINTER_TO_INT(data); GtkWidget *widget=which==1 ? wb.file_entry : (which==2 ? wb.usage_entry : wb.symbol_entry);
 if(which==1) files_filter(NULL,NULL); else if(which==0) browser_filter(NULL,NULL); else if(which==2) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE); gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),0); }
 focus_widget_panel(widget);
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
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].page==wb.box && wb.hosts[i].window) gtk_widget_set_visible(wb.hosts[i].window,wb.context_visible);
}
static void focus_relations(GtkMenuItem *item,gpointer data) { (void)item; (void)data; focus_widget_panel(wb.usage_entry); }
static void focus_panel(GtkMenuItem *item,gpointer data) {
 (void)item; gint panel=GPOINTER_TO_INT(data); GtkWidget *view=panel==0 ? wb.local_entry : (panel==1 ? wb.tree_view : (panel==2 ? wb.category_entry : wb.types_view));
 if(!panel && !wb.hosts[8].window) { ui_prefs.sidebar_visible=TRUE; ui_sidebar_show_hide(); }
 focus_widget_panel(view);
}
static void expand_panel(GtkMenuItem *item,gpointer data) {
 (void)item; gint action=GPOINTER_TO_INT(data); GtkTreeView *view=GTK_TREE_VIEW(action<2 ? wb.tree_view : (action<4 ? wb.category_view : wb.types_view));
 if(action%2) gtk_tree_view_collapse_all(view); else gtk_tree_view_expand_all(view);
}
static void project_visible(GtkCheckMenuItem *item,gpointer data) {
 (void)data; gboolean visible=gtk_check_menu_item_get_active(item); gtk_widget_set_visible(wb.project_notebook,visible); if(wb.float_window) gtk_widget_set_visible(wb.float_window,visible);
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].home==wb.project_notebook && wb.hosts[i].window) gtk_widget_set_visible(wb.hosts[i].window,visible);
}
static void local_visible(GtkCheckMenuItem *item,gpointer data) { (void)data; ui_prefs.sidebar_visible=gtk_check_menu_item_get_active(item); ui_sidebar_show_hide();
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].home==main_widgets.sidebar_notebook && wb.hosts[i].window) gtk_widget_set_visible(wb.hosts[i].window,ui_prefs.sidebar_visible); }
static void dock_project(GtkMenuItem *item,gpointer data) {
 (void)item; (void)data; if(!wb.float_window) return;
 gtk_window_get_position(GTK_WINDOW(wb.float_window),&wb.float_x,&wb.float_y); gtk_window_get_size(GTK_WINDOW(wb.float_window),&wb.float_width,&wb.float_height);
 GtkWidget *window=wb.float_window; wb.float_window=NULL; gboolean visible=gtk_widget_get_visible(wb.project_notebook);
 g_object_ref(wb.project_notebook); gtk_container_remove(GTK_CONTAINER(window),wb.project_notebook); gtk_paned_pack2(GTK_PANED(wb.right_pane),wb.project_notebook,FALSE,FALSE); g_object_unref(wb.project_notebook); gtk_widget_set_visible(wb.project_notebook,visible); gtk_widget_destroy(window);
}
static gboolean float_closed(GtkWidget *widget,GdkEvent *event,gpointer data) { (void)widget; (void)event; dock_project(NULL,data); return TRUE; }
static void float_project(GtkMenuItem *item,gpointer data) {
 (void)item; (void)data; if(wb.float_window) { gtk_window_present(GTK_WINDOW(wb.float_window)); return; }
 wb.float_window=gtk_window_new(GTK_WINDOW_TOPLEVEL); gtk_window_set_title(GTK_WINDOW(wb.float_window),"Geany Insight — Project"); gtk_window_set_transient_for(GTK_WINDOW(wb.float_window),GTK_WINDOW(main_widgets.window));
 gtk_window_set_default_size(GTK_WINDOW(wb.float_window),wb.float_width>=300 ? wb.float_width : 440,wb.float_height>=200 ? wb.float_height : 620);
 g_object_ref(wb.project_notebook); gtk_container_remove(GTK_CONTAINER(wb.right_pane),wb.project_notebook); gtk_container_add(GTK_CONTAINER(wb.float_window),wb.project_notebook); g_object_unref(wb.project_notebook);
 g_signal_connect(wb.float_window,"delete-event",G_CALLBACK(float_closed),NULL); gtk_widget_show_all(wb.float_window);
 if(wb.float_width>=300) {
  GdkMonitor *monitor=gdk_display_get_monitor_at_point(gdk_display_get_default(),wb.float_x,wb.float_y);
  if(monitor) { GdkRectangle area; gdk_monitor_get_workarea(monitor,&area); wb.float_width=MIN(wb.float_width,MAX(300,area.width)); wb.float_height=MIN(wb.float_height,MAX(200,area.height)); wb.float_x=CLAMP(wb.float_x,area.x,area.x+MAX(0,area.width-wb.float_width)); wb.float_y=CLAMP(wb.float_y,area.y,area.y+MAX(0,area.height-wb.float_height)); gtk_window_resize(GTK_WINDOW(wb.float_window),wb.float_width,wb.float_height); }
  gtk_window_move(GTK_WINDOW(wb.float_window),wb.float_x,wb.float_y);
 }
}
static void dock_panel(guint index) {
 if(index>=wb.host_count) return;
 PanelHost *host=&wb.hosts[index]; if(!host->window) return;
 gtk_window_get_position(GTK_WINDOW(host->window),&host->x,&host->y); gtk_window_get_size(GTK_WINDOW(host->window),&host->width,&host->height); host->visible=gtk_widget_get_visible(host->window);
 GtkWidget *window=host->window; host->window=NULL; g_object_ref(host->page); gtk_container_remove(GTK_CONTAINER(window),host->page);
 if(host->pane) gtk_paned_pack2(GTK_PANED(host->home),host->page,FALSE,FALSE);
 else {
  gint position=0; for(gint n=0;n<gtk_notebook_get_n_pages(GTK_NOTEBOOK(host->home));n++) { GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(host->home),n); guint order=(guint)n;
   for(guint j=0;j<wb.host_count;j++) if(wb.hosts[j].page==page && wb.hosts[j].home==host->home) { order=wb.hosts[j].order; break; }
   if(order<host->order) position++;
  }
  gtk_notebook_insert_page(GTK_NOTEBOOK(host->home),host->page,gtk_label_new(host->label),position);
 }
 g_object_unref(host->page); gtk_widget_show(host->page); gtk_widget_destroy(window);
}
static void dock_all_panels(void) { for(guint i=0;i<wb.host_count;i++) dock_panel(i); }
static gboolean panel_closed(GtkWidget *widget,GdkEvent *event,gpointer data) { (void)widget; (void)event; dock_panel(GPOINTER_TO_UINT(data)); return TRUE; }
static void detach_panel(guint index) {
 if(index>=wb.host_count) return;
 PanelHost *host=&wb.hosts[index]; if(host->window) { gtk_window_present(GTK_WINDOW(host->window)); return; }
 GtkWidget *parent=gtk_widget_get_parent(host->page); if(parent!=host->home) return;
 host->window=gtk_window_new(GTK_WINDOW_TOPLEVEL); gchar *title=g_strdup_printf("Geany Insight — %s",host->label); gtk_window_set_title(GTK_WINDOW(host->window),title); g_free(title);
 gtk_window_set_transient_for(GTK_WINDOW(host->window),GTK_WINDOW(main_widgets.window)); gtk_window_set_default_size(GTK_WINDOW(host->window),host->width>=300 ? host->width : (host->pane ? 900 : 480),host->height>=200 ? host->height : 600);
 g_object_ref(host->page); gtk_container_remove(GTK_CONTAINER(host->home),host->page); gtk_container_add(GTK_CONTAINER(host->window),host->page); g_object_unref(host->page);
 if(wb.navigation_accels) gtk_window_add_accel_group(GTK_WINDOW(host->window),wb.navigation_accels);
 if(wb.lua_accels) gtk_window_add_accel_group(GTK_WINDOW(host->window),wb.lua_accels);
 g_signal_connect(host->window,"delete-event",G_CALLBACK(panel_closed),GUINT_TO_POINTER(index)); gtk_widget_show_all(host->window);
 if(host->width>=300) { GdkMonitor *monitor=gdk_display_get_monitor_at_point(gdk_display_get_default(),host->x,host->y); if(monitor) { GdkRectangle area; gdk_monitor_get_workarea(monitor,&area); host->width=MIN(host->width,MAX(300,area.width)); host->height=MIN(host->height,MAX(200,area.height)); host->x=CLAMP(host->x,area.x,area.x+MAX(0,area.width-host->width)); host->y=CLAMP(host->y,area.y,area.y+MAX(0,area.height-host->height)); gtk_window_resize(GTK_WINDOW(host->window),host->width,host->height); } gtk_window_move(GTK_WINDOW(host->window),host->x,host->y); }
}
static void float_panel_command(GtkMenuItem *item,gpointer data) { (void)item; detach_panel(GPOINTER_TO_UINT(data)); }
static void dock_panels_command(GtkMenuItem *item,gpointer data) { (void)item; (void)data; dock_all_panels(); }
static void register_panels(void) {
 wb.host_count=10;
 for(guint i=0;i<8;i++) { PanelHost *host=&wb.hosts[i]; host->home=wb.project_notebook; host->page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(host->home),(gint)i); host->order=i; host->label=g_strdup(gtk_notebook_get_tab_label_text(GTK_NOTEBOOK(host->home),host->page)); }
 PanelHost *left=&wb.hosts[8]; left->home=main_widgets.sidebar_notebook; left->page=wb.local_entry; while(gtk_widget_get_parent(left->page)!=left->home) left->page=gtk_widget_get_parent(left->page); left->order=(guint)gtk_notebook_page_num(GTK_NOTEBOOK(left->home),left->page); left->label=g_strdup("File Symbols");
 PanelHost *context=&wb.hosts[9]; context->home=wb.pane; context->page=wb.box; context->pane=TRUE; context->label=g_strdup("Context");
 GtkWidget *floating=gtk_menu_item_new_with_label("Float individual panel"),*menu=gtk_menu_new(); gtk_menu_item_set_submenu(GTK_MENU_ITEM(floating),menu); gtk_container_add(GTK_CONTAINER(wb.panel_commands),floating);
 for(guint i=0;i<wb.host_count;i++) { GtkWidget *item=gtk_menu_item_new_with_label(wb.hosts[i].label); gtk_container_add(GTK_CONTAINER(menu),item); g_signal_connect(item,"activate",G_CALLBACK(float_panel_command),GUINT_TO_POINTER(i)); }
 GtkWidget *dock=gtk_menu_item_new_with_label("Dock all individual panels"); gtk_container_add(GTK_CONTAINER(wb.panel_commands),dock); g_signal_connect(dock,"activate",G_CALLBACK(dock_panels_command),NULL); gtk_widget_show_all(floating); gtk_widget_show(dock);
}
static guint visible_host_mask(void) { guint mask=0; for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window && gtk_widget_get_visible(wb.hosts[i].window)) mask|=1u<<i; return mask; }
static guint detached_mask(void) { guint mask=0; for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window) mask|=1u<<i; return mask; }
static gint project_tab_identity(void) {
 gint page=gtk_notebook_get_current_page(GTK_NOTEBOOK(wb.project_notebook)); GtkWidget *widget=page>=0 ? gtk_notebook_get_nth_page(GTK_NOTEBOOK(wb.project_notebook),page) : NULL;
 for(guint i=0;i<MIN(8u,wb.host_count);i++) if(wb.hosts[i].page==widget) return (gint)i;
 return MAX(0,page);
}
static void select_project_tab(gint identity) {
 if(wb.host_count && identity>=0 && identity<8) { GtkWidget *page=wb.hosts[identity].page; gint index=gtk_notebook_page_num(GTK_NOTEBOOK(wb.project_notebook),page); if(index>=0) gtk_notebook_set_current_page(GTK_NOTEBOOK(wb.project_notebook),index); }
 else gtk_notebook_set_current_page(GTK_NOTEBOOK(wb.project_notebook),identity);
}
static void set_panel_visibility(gboolean left,gboolean project,gboolean context) {
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.local_toggle),left); local_visible(GTK_CHECK_MENU_ITEM(wb.local_toggle),NULL);
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.project_toggle),project); project_visible(GTK_CHECK_MENU_ITEM(wb.project_toggle),NULL);
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.context_toggle),context); context_visible(GTK_CHECK_MENU_ITEM(wb.context_toggle),NULL);
}
static void save_layout(GtkMenuItem *item,gpointer data) {
 (void)item; guint slot=GPOINTER_TO_UINT(data); if(slot>=4) return;
 if(wb.float_window) { gtk_window_get_position(GTK_WINDOW(wb.float_window),&wb.float_x,&wb.float_y); gtk_window_get_size(GTK_WINDOW(wb.float_window),&wb.float_width,&wb.float_height); }
 InsightLayout record={.valid=TRUE}; gint *v=record.values;
 v[IL_LEFT]=ui_prefs.sidebar_visible; v[IL_PROJECT]=gtk_widget_get_visible(wb.project_notebook); v[IL_CONTEXT]=wb.context_visible;
 v[IL_FOLLOW]=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow)); v[IL_WRAP]=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.wrap)); v[IL_FLOAT]=wb.float_window!=NULL;
 v[IL_LEFT_SPLIT]=gtk_paned_get_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"hpaned1"))); v[IL_RIGHT_SPLIT]=gtk_paned_get_position(GTK_PANED(wb.right_pane)); v[IL_CONTEXT_SPLIT]=gtk_paned_get_position(GTK_PANED(wb.pane)); v[IL_TAB]=project_tab_identity(); v[IL_DETACHED]=(gint)detached_mask(); v[IL_HOST_VISIBLE]=(gint)visible_host_mask();
 v[IL_X]=CLAMP(wb.float_x,-30000,30000); v[IL_Y]=CLAMP(wb.float_y,-30000,30000); v[IL_WIDTH]=wb.float_width>=300 ? MIN(wb.float_width,4000) : 440; v[IL_HEIGHT]=wb.float_height>=200 ? MIN(wb.float_height,4000) : 620;
 if(!insight_layout_valid(&record)) { ui_set_statusbar(TRUE,"Layout cannot be saved: invalid panel geometry"); return; }
 wb.layouts[slot]=record; ui_set_statusbar(TRUE,"Layout %c stored",'A'+(gint)slot); save_workbench_settings();
}
static void reset_layouts(GtkMenuItem *item,gpointer data) { (void)item; (void)data; memset(wb.layouts,0,sizeof wb.layouts); ui_set_statusbar(TRUE,"Saved layouts reset; A–D now use presets"); save_workbench_settings(); }
static void apply_layout(GtkMenuItem *item,gpointer data) {
 (void)item; gint layout=GPOINTER_TO_INT(data); if(layout<0 || layout>=4) return;
 dock_all_panels(); dock_project(NULL,NULL);
 if(insight_layout_valid(&wb.layouts[layout])) {
  const gint *v=wb.layouts[layout].values;
  wb.float_x=v[IL_X]; wb.float_y=v[IL_Y]; wb.float_width=v[IL_WIDTH]; wb.float_height=v[IL_HEIGHT];
  gtk_paned_set_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"hpaned1")),v[IL_LEFT_SPLIT]); gtk_paned_set_position(GTK_PANED(wb.right_pane),v[IL_RIGHT_SPLIT]); gtk_paned_set_position(GTK_PANED(wb.pane),v[IL_CONTEXT_SPLIT]);
  select_project_tab(v[IL_TAB]); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),v[IL_FOLLOW]); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.wrap),v[IL_WRAP]);
  if(v[IL_FLOAT]) float_project(NULL,NULL);
  for(guint i=0;i<wb.host_count;i++) if(v[IL_DETACHED]&(1<<i)) detach_panel(i);
  set_panel_visibility(v[IL_LEFT],v[IL_PROJECT],v[IL_CONTEXT]); for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window) gtk_widget_set_visible(wb.hosts[i].window,(v[IL_HOST_VISIBLE]&(1<<i))!=0); return;
 }
 set_panel_visibility(layout!=1,layout!=1,layout!=1);
 gtk_paned_set_position(GTK_PANED(wb.pane),layout==2 ? 330 : 500); gtk_paned_set_position(GTK_PANED(wb.right_pane),layout==2 ? 650 : 850);
 gtk_paned_set_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"hpaned1")),layout==3 ? 360 : 240);
 if(layout==3) float_project(NULL,NULL);
}
static void panel_menu_sync(GtkWidget *menu,gpointer data) {
 (void)menu; (void)data; gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.project_toggle),gtk_widget_get_visible(wb.project_notebook)); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.local_toggle),ui_prefs.sidebar_visible);
}
static void panel_menu(GtkWidget *view_menu) {
 GtkWidget *item=gtk_menu_item_new_with_label("Panels"),*menu=gtk_menu_new(); gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),menu); gtk_container_add(GTK_CONTAINER(view_menu),item); wb.panel_commands=menu;
 const char *focus[]={"Activate File Symbols","Activate Folders","Activate Symbol Categories","Activate File Types"};
 for(guint i=0;i<4;i++) { GtkWidget *entry=gtk_menu_item_new_with_label(focus[i]); g_signal_connect(entry,"activate",G_CALLBACK(focus_panel),GINT_TO_POINTER(i)); gtk_container_add(GTK_CONTAINER(menu),entry); }
 const char *expand[]={"Expand Folders","Collapse Folders","Expand Symbol Categories","Collapse Symbol Categories","Expand File Types","Collapse File Types"};
 for(guint i=0;i<6;i++) { GtkWidget *entry=gtk_menu_item_new_with_label(expand[i]); g_signal_connect(entry,"activate",G_CALLBACK(expand_panel),GINT_TO_POINTER(i)); gtk_container_add(GTK_CONTAINER(menu),entry); }
 GtkWidget *relations=gtk_menu_item_new_with_label("Relation Window"); gtk_container_add(GTK_CONTAINER(menu),relations); g_signal_connect(relations,"activate",G_CALLBACK(focus_relations),NULL);
 GtkWidget *project=gtk_check_menu_item_new_with_label("Project Window"),*local=gtk_check_menu_item_new_with_label("File Symbols Window"); wb.project_toggle=project; wb.local_toggle=local;
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(project),gtk_widget_get_visible(wb.project_notebook)); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(local),ui_prefs.sidebar_visible);
 g_signal_connect(project,"toggled",G_CALLBACK(project_visible),NULL); g_signal_connect(local,"toggled",G_CALLBACK(local_visible),NULL); gtk_container_add(GTK_CONTAINER(menu),project); gtk_container_add(GTK_CONTAINER(menu),local);
 GtkWidget *floating=gtk_menu_item_new_with_label("Float Project Window"),*docking=gtk_menu_item_new_with_label("Dock Project Window"); g_signal_connect(floating,"activate",G_CALLBACK(float_project),NULL); g_signal_connect(docking,"activate",G_CALLBACK(dock_project),NULL); gtk_container_add(GTK_CONTAINER(menu),floating); gtk_container_add(GTK_CONTAINER(menu),docking);
 GtkWidget *layouts=gtk_menu_item_new_with_label("Layouts"),*layout_menu=gtk_menu_new(); gtk_menu_item_set_submenu(GTK_MENU_ITEM(layouts),layout_menu); gtk_container_add(GTK_CONTAINER(view_menu),layouts);
 const char *names[]={"Load layout A","Load layout B","Load layout C","Load layout D"};
 for(guint i=0;i<4;i++) { GtkWidget *entry=gtk_menu_item_new_with_label(names[i]); g_signal_connect(entry,"activate",G_CALLBACK(apply_layout),GINT_TO_POINTER(i)); gtk_container_add(GTK_CONTAINER(layout_menu),entry); }
 GtkWidget *save=gtk_menu_item_new_with_label("Save current layout"),*save_menu=gtk_menu_new(); gtk_menu_item_set_submenu(GTK_MENU_ITEM(save),save_menu); gtk_container_add(GTK_CONTAINER(layout_menu),save);
 const char *slots[]={"Save as A","Save as B","Save as C","Save as D"}; for(guint i=0;i<4;i++) { GtkWidget *entry=gtk_menu_item_new_with_label(slots[i]); g_signal_connect(entry,"activate",G_CALLBACK(save_layout),GUINT_TO_POINTER(i)); gtk_container_add(GTK_CONTAINER(save_menu),entry); }
 GtkWidget *reset=gtk_menu_item_new_with_label("Reset saved layouts to presets"); g_signal_connect(reset,"activate",G_CALLBACK(reset_layouts),NULL); gtk_container_add(GTK_CONTAINER(layout_menu),reset);
 g_signal_connect(menu,"show",G_CALLBACK(panel_menu_sync),NULL); gtk_widget_show_all(item); gtk_widget_show_all(layouts);
}
static void overview_visible(GtkCheckMenuItem *item,gpointer data) { (void)data; insight_overview_set_visible(gtk_check_menu_item_get_active(item)); }
static void create_workbench_menus(void) {
 GtkWidget *overview=gtk_check_menu_item_new_with_label("Overview"); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overview),insight_overview_get_visible()); g_signal_connect(overview,"toggled",G_CALLBACK(overview_visible),NULL); gtk_container_add(GTK_CONTAINER(ui_lookup_widget(main_widgets.window,"menu_view1_menu")),overview); gtk_widget_show(overview);
 GtkWidget *project_menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"menu_project1"))),*manage=gtk_menu_item_new_with_label("Add and Remove Project Files…"); gtk_container_add(GTK_CONTAINER(project_menu),manage); g_signal_connect(manage,"activate",G_CALLBACK(manage_project_files),NULL); gtk_widget_show(manage);
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
 GtkWidget *search_menu=ui_lookup_widget(main_widgets.window,"search1_menu");
 const char *result_names[]={"Search results","Previous search result","Next search result"};
 for(guint i=0;i<3;i++) {
  GtkWidget *entry=gtk_menu_item_new_with_label(result_names[i]); gtk_container_add(GTK_CONTAINER(search_menu),entry);
  g_signal_connect(entry,"activate",i ? G_CALLBACK(search_result_move) : G_CALLBACK(focus_search_results),GINT_TO_POINTER(i==1 ? -1 : 1));
  if(i) gtk_widget_add_accelerator(entry,"activate",wb.navigation_accels,i==1 ? GDK_KEY_F8 : GDK_KEY_F9,GDK_SHIFT_MASK,GTK_ACCEL_VISIBLE);
  gtk_widget_show(entry);
 }
 g_object_ref(wb.lua_item); gtk_container_remove(GTK_CONTAINER(main_widgets.tools_menu),wb.lua_item); gtk_menu_item_set_label(GTK_MENU_ITEM(wb.lua_item),"Scripts"); gtk_menu_shell_insert(GTK_MENU_SHELL(menubar),wb.lua_item,5); g_object_unref(wb.lua_item);
 wb.context_toggle=gtk_check_menu_item_new_with_label("Context window"); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.context_toggle),wb.context_visible);
 g_signal_connect(wb.context_toggle,"toggled",G_CALLBACK(context_visible),NULL); gtk_container_add(GTK_CONTAINER(ui_lookup_widget(main_widgets.window,"menu_view1_menu")),wb.context_toggle);
 gtk_widget_show_all(navigate); gtk_widget_show(wb.context_toggle);
 panel_menu(ui_lookup_widget(main_widgets.window,"menu_view1_menu"));
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
 insight_overview_write(settings);
 for(guint i=0;i<4;i++) { gchar group[16]; g_snprintf(group,sizeof group,"layout-%c",'A'+i); insight_layout_write(settings,group,&wb.layouts[i]); }
 g_key_file_set_integer(settings,"workbench","version",1);
 for(guint i=0;i<wb.host_count;i++) { PanelHost *host=&wb.hosts[i]; gchar group[32]; g_snprintf(group,sizeof group,"panel-%u",i); if(host->window) { gtk_window_get_position(GTK_WINDOW(host->window),&host->x,&host->y); gtk_window_get_size(GTK_WINDOW(host->window),&host->width,&host->height); host->visible=gtk_widget_get_visible(host->window); }
  g_key_file_set_integer(settings,group,"version",1); g_key_file_set_boolean(settings,group,"floating",host->window!=NULL); g_key_file_set_boolean(settings,group,"visible",host->visible); g_key_file_set_integer(settings,group,"x",host->x); g_key_file_set_integer(settings,group,"y",host->y); g_key_file_set_integer(settings,group,"width",host->width); g_key_file_set_integer(settings,group,"height",host->height);
 }
 g_key_file_set_boolean(settings,"workbench","project-visible",gtk_widget_get_visible(wb.project_notebook));
 g_key_file_set_boolean(settings,"workbench","floating",wb.float_window!=NULL);
 if(wb.float_window) { gtk_window_get_position(GTK_WINDOW(wb.float_window),&wb.float_x,&wb.float_y); gtk_window_get_size(GTK_WINDOW(wb.float_window),&wb.float_width,&wb.float_height); }
 g_key_file_set_integer(settings,"workbench","float-x",wb.float_x); g_key_file_set_integer(settings,"workbench","float-y",wb.float_y);
 g_key_file_set_integer(settings,"workbench","float-width",wb.float_width); g_key_file_set_integer(settings,"workbench","float-height",wb.float_height);
 g_key_file_set_integer(settings,"workbench","split",gtk_paned_get_position(GTK_PANED(wb.pane)));
 g_key_file_set_boolean(settings,"workbench","follow",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow)));
 g_key_file_set_boolean(settings,"workbench","visible",wb.context_visible);
 g_key_file_set_integer(settings,"workbench","right-split",gtk_paned_get_position(GTK_PANED(wb.right_pane)));
 g_key_file_set_integer(settings,"workbench","project-tab",project_tab_identity());
 g_key_file_set_boolean(settings,"workbench","search-toolbars",gtk_widget_get_visible(wb.project_search_row));
 g_key_file_set_integer(settings,"workbench","project-search-flags",text_search_flags(wb.project_case,wb.project_word,wb.project_regex));
 g_key_file_set_integer(settings,"workbench","editor-search-flags",text_search_flags(wb.editor_case,wb.editor_word,wb.editor_regex));
 g_key_file_set_integer(settings,"workbench","relation-mode",gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)));
 g_key_file_set_boolean(settings,"workbench","relation-follow",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_follow)));
 g_key_file_set_boolean(settings,"workbench","references-code-only",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_code)));
 g_key_file_set_boolean(settings,"workbench","usage-buffer",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_buffer)));
 g_key_file_set_boolean(settings,"workbench","wrap",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.wrap)));
 gchar *encoded=wb.root ? g_base64_encode((const guchar *)wb.root,strlen(wb.root)) : g_strdup("");
 g_key_file_set_string(settings,"workbench","root-base64",encoded); g_free(encoded);
 gsize length; gchar *text=g_key_file_to_data(settings,&length,NULL),*directory=g_path_get_dirname(wb.prefs_path);
 if(!g_mkdir_with_parents(directory,0700)) {
  GFile *file=g_file_new_for_path(wb.prefs_path); GError *error=NULL;
  if(!g_file_replace_contents(file,text,length,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,&error)) { ui_set_statusbar(TRUE,"Workbench settings not saved: %s",error->message); g_clear_error(&error); }
  g_object_unref(file);
 } else ui_set_statusbar(TRUE,"Workbench settings directory could not be created");
 g_free(directory); g_free(text); g_key_file_free(settings);
}
static void restore_workbench_settings(void) {
 gchar *prefs_utf8=utils_get_utf8_from_locale(wb.prefs_path); char *text=read_disk_source(prefs_utf8,FALSE); g_free(prefs_utf8); if(!text) return;
 GKeyFile *settings=g_key_file_new();
 if(strlen(text)<=32768 && g_key_file_load_from_data(settings,text,-1,G_KEY_FILE_NONE,NULL) && g_key_file_get_integer(settings,"workbench","version",NULL)==1) {
  insight_overview_read(settings);
  for(guint i=0;i<4;i++) { gchar group[16]; g_snprintf(group,sizeof group,"layout-%c",'A'+i); insight_layout_read(settings,group,&wb.layouts[i]); }
  for(guint i=0;i<10;i++) { PanelHost *host=&wb.hosts[i]; gchar group[32]; g_snprintf(group,sizeof group,"panel-%u",i); GError *error=NULL; gint version=g_key_file_get_integer(settings,group,"version",&error);
   if(!error && version==1) { host->restore=g_key_file_get_boolean(settings,group,"floating",&error); if(!error) host->visible=g_key_file_get_boolean(settings,group,"visible",&error); if(!error) host->x=g_key_file_get_integer(settings,group,"x",&error); if(!error) host->y=g_key_file_get_integer(settings,group,"y",&error); if(!error) host->width=g_key_file_get_integer(settings,group,"width",&error); if(!error) host->height=g_key_file_get_integer(settings,group,"height",&error); }
   if(error || version!=1 || host->width<300 || host->width>4000 || host->height<200 || host->height>4000 || host->x < -30000 || host->x>30000 || host->y < -30000 || host->y>30000) host->restore=FALSE;
   g_clear_error(&error);
  }
  wb.restore_float=g_key_file_get_boolean(settings,"workbench","floating",NULL);
  wb.float_x=CLAMP(g_key_file_get_integer(settings,"workbench","float-x",NULL),-30000,30000); wb.float_y=CLAMP(g_key_file_get_integer(settings,"workbench","float-y",NULL),-30000,30000);
  gint fw=g_key_file_get_integer(settings,"workbench","float-width",NULL),fh=g_key_file_get_integer(settings,"workbench","float-height",NULL);
  wb.float_width=fw>=300 ? MIN(fw,4000) : 0; wb.float_height=fh>=200 ? MIN(fh,4000) : 0;
  if(g_key_file_has_key(settings,"workbench","project-visible",NULL)) gtk_widget_set_visible(wb.project_notebook,g_key_file_get_boolean(settings,"workbench","project-visible",NULL));
  gint split=g_key_file_get_integer(settings,"workbench","split",NULL); if(split>=150 && split<=10000) gtk_paned_set_position(GTK_PANED(wb.pane),split);
  if(g_key_file_has_key(settings,"workbench","follow",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),g_key_file_get_boolean(settings,"workbench","follow",NULL));
  if(g_key_file_has_key(settings,"workbench","visible",NULL)) wb.context_visible=g_key_file_get_boolean(settings,"workbench","visible",NULL);
  gint right=g_key_file_get_integer(settings,"workbench","right-split",NULL); if(right>=200 && right<=10000) gtk_paned_set_position(GTK_PANED(wb.right_pane),right);
  if(g_key_file_has_key(settings,"workbench","search-toolbars",NULL)) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.search_toolbars_toggle),g_key_file_get_boolean(settings,"workbench","search-toolbars",NULL));
  gint project_flags=g_key_file_get_integer(settings,"workbench","project-search-flags",NULL),editor_flags=g_key_file_get_integer(settings,"workbench","editor-search-flags",NULL);
  if(project_flags>=0 && project_flags<=7) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_case),project_flags&INSIGHT_SEARCH_CASE); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_word),project_flags&INSIGHT_SEARCH_WORD); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_regex),project_flags&INSIGHT_SEARCH_REGEX); }
  if(editor_flags>=0 && editor_flags<=7) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_case),editor_flags&INSIGHT_SEARCH_CASE); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_word),editor_flags&INSIGHT_SEARCH_WORD); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_regex),editor_flags&INSIGHT_SEARCH_REGEX); }
  gint relation=g_key_file_get_integer(settings,"workbench","relation-mode",NULL); if(relation>=0 && relation<6) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),relation);
  if(g_key_file_has_key(settings,"workbench","relation-follow",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),g_key_file_get_boolean(settings,"workbench","relation-follow",NULL));
  if(g_key_file_has_key(settings,"workbench","references-code-only",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_code),g_key_file_get_boolean(settings,"workbench","references-code-only",NULL));
  gint tab=g_key_file_get_integer(settings,"workbench","project-tab",NULL); if(tab>=0 && tab<8) gtk_notebook_set_current_page(GTK_NOTEBOOK(wb.project_notebook),tab);
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
static guint text_search_flags(GtkWidget *sensitive,GtkWidget *word,GtkWidget *regex) {
 return (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sensitive)) ? INSIGHT_SEARCH_CASE : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(word)) ? INSIGHT_SEARCH_WORD : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(regex)) ? INSIGHT_SEARCH_REGEX : 0);
}
static void project_text_search(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; wb.relation_setting=TRUE;
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE);
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),3);
 gtk_entry_set_text(GTK_ENTRY(wb.usage_entry),gtk_entry_get_text(GTK_ENTRY(wb.project_query)));
 wb.relation_setting=FALSE; relation_controls(NULL,NULL);
 msgwin_show_hide(TRUE); gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),gtk_notebook_page_num(GTK_NOTEBOOK(main_widgets.message_window_notebook),wb.search_page)); gtk_widget_grab_focus(wb.search_view);
}
static void editor_text_search(GtkWidget *widget,gpointer data) {
 (void)widget; gboolean forward=GPOINTER_TO_INT(data)!=-1; GeanyDocument *doc=document_get_current();
 if(!doc) { ui_set_statusbar(TRUE,"No active document"); return; }
 gint length=sci_get_length(doc->editor->sci);
 if(length<0 || length>2*1024*1024) { ui_set_statusbar(TRUE,"Toolbar search supports documents up to 2 MiB; use Geany Find for larger files"); return; }
 gchar *text=sci_get_contents(doc->editor->sci,length+1),*digest=g_compute_checksum_for_data(G_CHECKSUM_SHA256,(const guchar *)text,(gsize)length);
 guint flags=text_search_flags(wb.editor_case,wb.editor_word,wb.editor_regex);
 const char *query=gtk_entry_get_text(GTK_ENTRY(wb.editor_query)); gsize begin,end,a=(gsize)SSM(doc->editor->sci,SCI_GETSELECTIONSTART,0,0),b=(gsize)SSM(doc->editor->sci,SCI_GETSELECTIONEND,0,0),from=forward ? b : a; gboolean wrapped=FALSE;
 gboolean repeat=wb.editor_find_id==doc->id && wb.editor_find_flags==flags && !g_strcmp0(query,wb.editor_find_query) && !g_strcmp0(digest,wb.editor_find_digest) && a==wb.editor_find_begin && b==wb.editor_find_end;
 if(repeat && a==b) from=forward ? (b<(gsize)length ? b+1 : 0) : (a ? a-1 : (gsize)length);
 GError *error=NULL;
 if(insight_context_find(text,(gsize)length,query,from,forward,flags,&begin,&end,&wrapped,&error)) {
  SSM(doc->editor->sci,SCI_SETSEL,begin,end); decorate_match(doc->editor->sci,begin,end); SSM(doc->editor->sci,SCI_VERTICALCENTRECARET,0,0);
  g_free(wb.editor_find_query); g_free(wb.editor_find_digest); wb.editor_find_query=g_strdup(query); wb.editor_find_digest=g_strdup(digest); wb.editor_find_begin=begin; wb.editor_find_end=end; wb.editor_find_id=doc->id; wb.editor_find_flags=flags;
  ui_set_statusbar(TRUE,wrapped ? "Find: wrapped" : "Find: match"); insight_workbench_refresh();
 } else ui_set_statusbar(TRUE,"Find: %s",error ? error->message : "no match");
 g_clear_error(&error); g_free(text); g_free(digest);
}
static void search_toolbar_focus(GtkMenuItem *item,gpointer data) {
 (void)item; GtkWidget *entry=GPOINTER_TO_INT(data) ? wb.editor_query : wb.project_query;
 gtk_widget_show(wb.project_search_row); gtk_widget_show(wb.editor_search_row); gtk_widget_grab_focus(entry);
}
static void search_toolbars_visible(GtkCheckMenuItem *item,gpointer data) {
 (void)data; gboolean visible=gtk_check_menu_item_get_active(item); gtk_widget_set_visible(wb.project_search_row,visible); gtk_widget_set_visible(wb.editor_search_row,visible);
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
 (void)widget; (void)data; msgwin_show_hide(TRUE);
 gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),gtk_notebook_page_num(GTK_NOTEBOOK(main_widgets.message_window_notebook),wb.search_page)); gtk_widget_grab_focus(wb.search_view);
}
static gboolean search_result_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)data;
 if((event->state & gtk_accelerator_get_default_mod_mask())==GDK_MOD1_MASK && (event->keyval==GDK_KEY_Up || event->keyval==GDK_KEY_Down)) { search_result_move(widget,GINT_TO_POINTER(event->keyval==GDK_KEY_Up ? -2 : 2)); return TRUE; }
 if((event->state & gtk_accelerator_get_default_mod_mask())!=GDK_CONTROL_MASK || (event->keyval!=GDK_KEY_l && event->keyval!=GDK_KEY_L)) return FALSE;
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
 (void)widget; gint direction=GPOINTER_TO_INT(data),step=direction<0 ? -1 : 1,count=gtk_tree_model_iter_n_children(GTK_TREE_MODEL(wb.search_matches),NULL); gboolean open=direction==-1 || direction==1; if(!count) return;
 GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.search_view)); GtkTreeModel *model; GtkTreeIter iter; gint index=step<0 ? count : -1;
 if(gtk_tree_selection_get_selected(selection,&model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter); index=gtk_tree_path_get_indices(path)[0]; gtk_tree_path_free(path); }
 index=(index+step+count)%count; GtkTreePath *path=gtk_tree_path_new_from_indices(index,-1);
 gtk_tree_view_set_cursor(GTK_TREE_VIEW(wb.search_view),path,NULL,FALSE); gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(wb.search_view),path,NULL,TRUE,0.5f,0); if(open) usage_activated(GTK_TREE_VIEW(wb.search_view),path,NULL,NULL); else usage_selected(selection,NULL); gtk_tree_path_free(path);
}
static void search_copy_results(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; GString *text=g_string_new(NULL); GtkTreeIter iter; GtkTreeModel *model=GTK_TREE_MODEL(wb.search_matches);
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gchar *path,*snippet; gulong line; gtk_tree_model_get(model,&iter,USAGE_DISPLAY,&path,USAGE_LINE,&line,USAGE_SNIPPET,&snippet,-1); g_string_append_printf(text,"%s:%lu: %s\n",path,line,snippet); g_free(path); g_free(snippet); }
 gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text->str,(gint)text->len); g_string_free(text,TRUE);
}
static void create_search_results(void) {
 wb.search_state_path=g_build_filename(app->configdir,"insight","search-state.conf",NULL);
 wb.search_files=g_ptr_array_new_with_free_func(g_free); wb.search_page=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); wb.search_scope=gtk_combo_box_text_new(); gtk_widget_set_name(wb.search_scope,"insight-search-scope");
 const char *scopes[]={"Project files","Open documents","Current document","Chosen files / folder"}; for(guint i=0;i<G_N_ELEMENTS(scopes);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.search_scope),scopes[i]); gtk_combo_box_set_active(GTK_COMBO_BOX(wb.search_scope),0);
 gtk_box_pack_start(GTK_BOX(row),wb.search_scope,FALSE,FALSE,0); wb.search_mask=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.search_mask),"File masks: *.c;*.h;*.py"); gtk_box_pack_start(GTK_BOX(row),wb.search_mask,TRUE,TRUE,0);
 wb.search_keywords=gtk_check_button_new_with_label("Keywords / Boolean"); gtk_widget_set_name(wb.search_keywords,"insight-search-keywords"); gtk_widget_set_tooltip_text(wb.search_keywords,"Implicit AND, OR, NOT/!, parentheses, =case, ?\"regex\"; word fragments unless Word is enabled"); gtk_box_pack_start(GTK_BOX(row),wb.search_keywords,FALSE,FALSE,0);
 wb.search_variations=gtk_check_button_new_with_label("Word variations"); gtk_widget_set_name(wb.search_variations,"insight-search-variations"); gtk_widget_set_tooltip_text(wb.search_variations,"Regular English endings: open/opens/opened/opening, save/saving; base keywords only, not a dictionary stemmer"); gtk_box_pack_start(GTK_BOX(row),wb.search_variations,FALSE,FALSE,0);
 wb.search_context=gtk_spin_button_new_with_range(0,50,1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(wb.search_context),3); gtk_widget_set_tooltip_text(wb.search_context,"Maximum keyword distance in lines (0 = same line)"); gtk_box_pack_start(GTK_BOX(row),wb.search_context,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(wb.search_page),row,FALSE,FALSE,0); GtkWidget *controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);
 const char *buttons[]={"Files…","Folder…","Search","Stop","Previous","Next","Copy results"};
 for(guint i=0;i<G_N_ELEMENTS(buttons);i++) { GtkWidget *button=gtk_button_new_with_label(buttons[i]); gtk_box_pack_start(GTK_BOX(controls),button,FALSE,FALSE,0); GCallback cb=i<2 ? G_CALLBACK(search_choose_files) : (i==2 ? G_CALLBACK(project_text_search) : (i==3 ? G_CALLBACK(usage_cancel) : (i<6 ? G_CALLBACK(search_result_move) : G_CALLBACK(search_copy_results)))); g_signal_connect(button,"clicked",cb,GINT_TO_POINTER(i==1 ? 1 : (i==4 ? -1 : 1))); }
 gtk_box_pack_start(GTK_BOX(wb.search_page),controls,FALSE,FALSE,0); wb.search_status=gtk_label_new("Search results — Alt+Up/Down previews; Enter/double click opens the match"); gtk_label_set_xalign(GTK_LABEL(wb.search_status),0); gtk_box_pack_start(GTK_BOX(wb.search_page),wb.search_status,FALSE,FALSE,0);
 wb.search_matches=gtk_list_store_new(USAGE_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING); wb.search_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.search_matches)); gtk_widget_set_name(wb.search_view,"insight-search-results"); gtk_widget_set_tooltip_text(wb.search_view,"Alt+Up/Down: browse matches in Context without moving the editor; Enter/double click or Ctrl+L: open the selected match; Shift+F8/F9: previous/next match");
 const char *labels[]={"File","Line","Source"}; const gint columns[]={USAGE_DISPLAY,USAGE_LINE,USAGE_SNIPPET};
 for(guint i=0;i<3;i++) { GtkCellRenderer *cell=gtk_cell_renderer_text_new(); g_object_set(cell,"ellipsize",PANGO_ELLIPSIZE_END,NULL); GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(labels[i],cell,"text",columns[i],NULL); gtk_tree_view_column_set_sort_column_id(column,columns[i]); gtk_tree_view_column_set_resizable(column,TRUE); gtk_tree_view_column_set_sizing(column,GTK_TREE_VIEW_COLUMN_FIXED); gtk_tree_view_column_set_fixed_width(column,i==0 ? 260 : (i==1 ? 70 : 450)); gtk_tree_view_column_set_expand(column,i==2); gtk_tree_view_append_column(GTK_TREE_VIEW(wb.search_view),column); }
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.search_view); gtk_box_pack_start(GTK_BOX(wb.search_page),scroll,TRUE,TRUE,0);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.search_view)),"changed",G_CALLBACK(usage_selected),NULL); g_signal_connect(wb.search_view,"row-activated",G_CALLBACK(usage_activated),NULL);
 g_signal_connect(wb.search_view,"key-press-event",G_CALLBACK(search_result_key),NULL);
 g_signal_connect(wb.search_keywords,"toggled",G_CALLBACK(keyword_mode_changed),NULL);
 gtk_notebook_append_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),wb.search_page,gtk_label_new("Search Results")); gtk_widget_show_all(wb.search_page);
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
static void create_search_toolbars(void) {
 wb.search_history=g_ptr_array_new_with_free_func(g_free);
 GtkWidget *parent=ui_lookup_widget(main_widgets.window,"vbox1");
 gint position=gtk_widget_get_parent(main_widgets.toolbar)==parent ? 2 : 1;
 for(guint i=0;i<2;i++) {
  GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*field=gtk_search_entry_new(),*sensitive=gtk_check_button_new_with_label("Match case"),*word=gtk_check_button_new_with_label("Whole word"),*regex=gtk_check_button_new_with_label("Regex");
  gtk_entry_set_max_length(GTK_ENTRY(field),1024); gtk_widget_set_hexpand(field,TRUE); gtk_widget_set_size_request(field,180,-1);
  gtk_box_pack_start(GTK_BOX(row),gtk_label_new(i ? "Find:" : "Search project:"),FALSE,FALSE,3); gtk_box_pack_start(GTK_BOX(row),field,TRUE,TRUE,0);
  if(i) {
   wb.editor_search_row=row; wb.editor_query=field; wb.editor_case=sensitive; wb.editor_word=word; wb.editor_regex=regex;
   GtkWidget *previous=gtk_button_new_with_label("Previous"),*next=gtk_button_new_with_label("Next");
   gtk_box_pack_start(GTK_BOX(row),previous,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),next,FALSE,FALSE,0);
   g_signal_connect(previous,"clicked",G_CALLBACK(editor_text_search),GINT_TO_POINTER(-1)); g_signal_connect(next,"clicked",G_CALLBACK(editor_text_search),NULL); g_signal_connect(field,"activate",G_CALLBACK(editor_text_search),NULL);
  } else {
   wb.project_search_row=row; wb.project_query=field; gtk_widget_set_name(field,"insight-project-query"); wb.project_case=sensitive; wb.project_word=word; wb.project_regex=regex;
   GtkWidget *history=gtk_button_new_with_label("History"); gtk_widget_set_name(history,"insight-search-history"); gtk_widget_set_tooltip_text(history,"Last 32 valid search queries; choosing a query retains the current scope and options"); gtk_box_pack_start(GTK_BOX(row),history,FALSE,FALSE,0); g_signal_connect(history,"clicked",G_CALLBACK(search_history_menu),NULL);
   GtkWidget *search=gtk_button_new_with_label("Search"); gtk_box_pack_start(GTK_BOX(row),search,FALSE,FALSE,0); g_signal_connect(search,"clicked",G_CALLBACK(project_text_search),NULL); g_signal_connect(field,"activate",G_CALLBACK(project_text_search),NULL);
  }
  gtk_box_pack_start(GTK_BOX(row),sensitive,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),word,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),regex,FALSE,FALSE,0);
  g_signal_connect(field,"key-press-event",G_CALLBACK(browser_key),NULL); gtk_box_pack_start(GTK_BOX(parent),row,FALSE,FALSE,0); gtk_box_reorder_child(GTK_BOX(parent),row,position+(gint)i); gtk_widget_show_all(row);
 }
 GtkWidget *search_menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"search1")));
 const char *labels[]={"Search project toolbar…","Find in editor toolbar…"};
 for(guint i=0;i<2;i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]); gtk_container_add(GTK_CONTAINER(search_menu),item); g_signal_connect(item,"activate",G_CALLBACK(search_toolbar_focus),GINT_TO_POINTER(i)); gtk_widget_show(item); }
 GtkWidget *visible=gtk_check_menu_item_new_with_label("Search toolbars"); wb.search_toolbars_toggle=visible; gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(visible),TRUE); g_signal_connect(visible,"toggled",G_CALLBACK(search_toolbars_visible),NULL); gtk_container_add(GTK_CONTAINER(ui_lookup_widget(main_widgets.window,"menu_view1_menu")),visible); gtk_widget_show(visible);
 create_search_results();
}
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
 gtk_toolbar_insert(toolbar,gtk_separator_tool_item_new(),-1);
 const char *layouts[]={"A","B","C","D"};
 const char *tips[]={"Load layout A","Load layout B","Load layout C","Load layout D"};
 for(guint i=0;i<4;i++) { GtkToolItem *item=gtk_tool_button_new(gtk_label_new(layouts[i]),tips[i]); gtk_widget_set_tooltip_text(GTK_WIDGET(item),tips[i]); g_signal_connect(item,"clicked",G_CALLBACK(apply_layout),GINT_TO_POINTER(i)); gtk_toolbar_insert(toolbar,item,-1); }
 gtk_widget_show_all(GTK_WIDGET(toolbar));
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
static gboolean remote_workspace(const char *path) { gboolean previous=wb.workspace_from_project; wb.workspace_from_project=FALSE; if(start_scan(path)) return TRUE; wb.workspace_from_project=previous; return FALSE; }
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
 wb.watch=insight_workspace_watch_new(1500,workspace_changed,NULL);
 wb.membership=insight_project_files_new(); wb.scan_files=g_ptr_array_new_with_free_func(g_free); wb.scan_trees=g_ptr_array_new_with_free_func(g_free); wb.scan_seen=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 wb.frames=g_ptr_array_new_with_free_func(frame_free); wb.files=g_ptr_array_new_with_free_func(g_free);
 wb.base=insight_index_new(); wb.index=insight_index_new(); wb.index_dirty=TRUE;
 wb.cache_path=g_build_filename(app->configdir,"insight","workspace.idx",NULL); restore_index_cache();
 wb.definitions=g_ptr_array_new_with_free_func(location_free);
 wb.history=g_ptr_array_new_with_free_func(location_free); wb.history_position=-1;
 wb.project_notebook=gtk_notebook_new(); gtk_notebook_set_scrollable(GTK_NOTEBOOK(wb.project_notebook),TRUE);
 create_browser(); create_files_browser(); create_project_tree(); create_usage_browser(); create_categories(); create_file_types(); create_notes(); create_local_browser();
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
 create_context_find();
 wb.preview=SCINTILLA(scintilla_new()); gtk_widget_set_name(GTK_WIDGET(wb.preview),"insight-context-code");
 sci_set_codepage(wb.preview,SC_CP_UTF8);
 SSM(wb.preview,SCI_SETUNDOCOLLECTION,0,0);
 SSM(wb.preview,SCI_SETMARGINWIDTHN,1,0); SSM(wb.preview,SCI_SETMARGINWIDTHN,2,0);
 SSM(wb.preview,SCI_SETWRAPMODE,SC_WRAP_NONE,0);
 gtk_widget_set_direction(GTK_WIDGET(wb.preview),GTK_TEXT_DIR_LTR);
 g_signal_connect(wb.preview,"button-press-event",G_CALLBACK(preview_button),NULL);
 g_signal_connect(wb.preview,"motion-notify-event",G_CALLBACK(preview_motion),NULL);
 g_signal_connect(wb.preview,"sci-notify",G_CALLBACK(preview_notify),NULL);
 g_signal_connect(wb.preview,"key-press-event",G_CALLBACK(preview_key),NULL);
 gtk_widget_set_tooltip_text(GTK_WIDGET(wb.preview),"Click an underlined name or F12: open definition; Enter: open caret line");
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
 create_search_toolbars();
 restore_workbench_settings(); restore_search_settings();
 create_native_lua();
 const char *signals[]={"document-open","document-save","document-activate","document-close"};
 const char *events[]={"document_open","document_save","document_activate","document_close"};
 for(guint i=0;i<4;i++) wb.event_handlers[i]=g_signal_connect_after(geany_object,signals[i],G_CALLBACK(native_document_event),(gpointer)events[i]);
 create_workbench_menus(); arrange_main_menus(); create_browse_toolbar(); register_panels();
 insight_remote_ui_init(remote_workspace);
 wb.project_handlers[0]=g_signal_connect_after(geany_object,"project-open",G_CALLBACK(native_project_update),GINT_TO_POINTER(1));
 wb.project_handlers[1]=g_signal_connect_after(geany_object,"project-save",G_CALLBACK(native_project_update),NULL);
 wb.project_handlers[2]=g_signal_connect_after(geany_object,"project-close",G_CALLBACK(native_project_close),NULL);
 native_project_update(NULL,NULL,GINT_TO_POINTER(1));
 if(wb.restore_float) { gboolean visible=gtk_widget_get_visible(wb.project_notebook); float_project(NULL,NULL); gtk_widget_set_visible(wb.project_notebook,visible); gtk_widget_set_visible(wb.float_window,visible); }
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].restore) { detach_panel(i); gtk_widget_set_visible(wb.hosts[i].window,wb.hosts[i].visible); }
 if(wb.root && !wb.scan) start_scan(wb.root);
 ui_sidebar_show_hide();
 insight_workbench_refresh();
}
static gboolean accelerator_closure_matches(GtkAccelKey *key,GClosure *closure,gpointer data) { (void)key; return closure==data; }
static void remove_group_accelerators(GtkWidget *widget,GtkAccelGroup *group) {
 if(GTK_IS_MENU_ITEM(widget)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget)); if(menu) remove_group_accelerators(menu,group); }
 if(GTK_IS_CONTAINER(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget)); for(GList *p=children;p;p=p->next) remove_group_accelerators(p->data,group); g_list_free(children); }
 GList *closures=gtk_widget_list_accel_closures(widget);
 for(GList *p=closures;p;p=p->next) { GtkAccelKey *key=gtk_accel_group_find(group,accelerator_closure_matches,p->data); if(key) gtk_widget_remove_accelerator(widget,group,key->accel_key,key->accel_mods); }
 g_list_free(closures);
}
void insight_workbench_finalize(void) {
 insight_remote_ui_finalize();
 save_search_settings(); save_workbench_settings(); dock_all_panels(); dock_project(NULL,NULL);
 for(guint i=0;i<wb.host_count;i++) g_free(wb.hosts[i].label);
 for(guint i=0;i<3;i++) if(wb.project_handlers[i]) g_signal_handler_disconnect(geany_object,wb.project_handlers[i]);
 g_free(wb.geany_root);
 for(guint i=0;i<4;i++) if(wb.event_handlers[i]) g_signal_handler_disconnect(geany_object,wb.event_handlers[i]);
 if(wb.lua_accels) { remove_group_accelerators(main_widgets.window,wb.lua_accels); gtk_window_remove_accel_group(GTK_WINDOW(main_widgets.window),wb.lua_accels); g_object_unref(wb.lua_accels); }
 if(wb.navigation_accels) { remove_group_accelerators(main_widgets.window,wb.navigation_accels); gtk_window_remove_accel_group(GTK_WINDOW(main_widgets.window),wb.navigation_accels); g_object_unref(wb.navigation_accels); }
 if(wb.css) { gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(wb.css)); g_object_unref(wb.css); }
 insight_extensions_free(wb.extensions);
 if(wb.refresh) g_source_remove(wb.refresh);
 g_free(wb.prefs_path);
 stop_usage(); g_ptr_array_free(wb.usage_paths,TRUE);
 g_free(wb.search_state_path); g_ptr_array_free(wb.search_files,TRUE); g_ptr_array_free(wb.search_history,TRUE); g_object_unref(wb.search_matches);
 if(wb.watch_idle) g_source_remove(wb.watch_idle);
 insight_workspace_watch_free(wb.watch);
 stop_scan(); insight_project_files_free(wb.membership); g_free(wb.membership_project); g_ptr_array_free(wb.scan_files,TRUE); g_ptr_array_free(wb.scan_trees,TRUE); g_hash_table_destroy(wb.scan_seen); g_ptr_array_free(wb.frames,TRUE); g_ptr_array_free(wb.files,TRUE); g_free(wb.root); g_free(wb.cache_path);
 g_free(wb.editor_find_query); g_free(wb.editor_find_digest);
 g_free(wb.relation_word);
 g_free(wb.source_path); g_free(wb.source_digest); g_free(wb.find_last_query); g_free(wb.find_last_digest);
 insight_index_free(wb.base); insight_index_free(wb.index);
 if(wb.symbols) g_object_unref(wb.symbols);
 if(wb.local_symbols) g_object_unref(wb.local_symbols);
 if(wb.project_tree) g_object_unref(wb.project_tree);
 if(wb.categories) g_object_unref(wb.categories);
 if(wb.file_types) g_object_unref(wb.file_types);
 if(wb.marks_store) g_object_unref(wb.marks_store);
 if(wb.clips_store) g_object_unref(wb.clips_store);
 insight_notes_free(wb.notes); g_free(wb.notes_path);
 if(wb.file_store) g_object_unref(wb.file_store);
 if(wb.matches) g_object_unref(wb.matches);
 if(wb.history) g_ptr_array_free(wb.history,TRUE);
 if(wb.definitions) g_ptr_array_free(wb.definitions,TRUE);
 memset(&wb,0,sizeof wb);
}
