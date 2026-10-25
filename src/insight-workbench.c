#define INSIGHT_LOCALIZE_FORMATS
#include "insight-i18n.h"
#include "../insight/desktop/glib_compat.h"
#include "../insight/desktop/window_motion.h"
#include "insight-languages.h"
#include "insight-completion.h"
#include "insight-paste.h"
#include "insight-navigation.h"
#include "insight-search-links.h"
#include "insight-list-tools.h"
#include "insight-text-print.h"
#include "insight-formatting.h"
#include "insight-themes.h"
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "insight-workbench.h"
#include "insight-scroll-links.h"
#include "insight-tree-expand.h"
#include "insight-appearance.h"
#include "insight-side-tabs.h"
#include "insight-symbol-edit.h"
#include "insight-project-report.h"
#include "insight-diff.h"
#include "insight-hex.h"
#include "insight-diff-syntax.h"
#include "insight-directory.h"
#include "insight-relation-graph.h"
#include "insight-relation-tree.h"
#include "insight-disk-browser.h"
#include "insight-git.h"
#include "insight-list-style.h"
#include "insight-window-list.h"
#include "insight-overview.h"
#include "insight-help.h"
#include "insight-symbol-view.h"
#include "toolbar.h"
#include "search.h"
#include "sidebar.h"
#include "symbols.h"
#include "ui_utils.h"
#include "document.h"
#include "main.h"
#include "documentprivate.h"
#include "editor.h"
#include "filetypes.h"
#include "projectprivate.h"
#include "encodingsprivate.h"
#include "highlighting.h"
#include "sciwrappers.h"
#include "utils.h"
#include "navqueue.h"
#include "tagmanager/tm_workspace.h"
#include "tagmanager/tm_parser.h"
#include <string.h>
#include <gio/gio.h>
#include "../insight/core/index.h"
#include "../insight/core/occurrences.h"
#include <glib/gstdio.h>
#include <sys/stat.h>
#include "../insight/desktop/index_cache.h"
#include "../insight/desktop/project_db.h"
#include <stdlib.h>
#include <stdint.h>
#include "insight-index.h"
#include "insight-template-fields.h"
#include "geanyobject.h"
#include "keybindings.h"
#include "../insight/core/context.h"
#include "../insight/core/runtime.h"
#include "../insight/core/structure_select.h"
#include "../insight/desktop/extensions.h"
#include "../insight/desktop/lookup_filter.h"
#include "../insight/desktop/layout_state.h"
#include "../insight/desktop/context_search.h"
#include "../insight/desktop/notebook_store.h"
#include "../insight/desktop/snippet.h"
#include "../insight/desktop/keyword_search.h"
#include "../insight/desktop/search_state.h"
#include "../insight/desktop/project_files.h"
#include "../insight/desktop/master_sync.h"
#include "msgwindow.h"
#include "insight-remote.h"
#include "../insight/desktop/workspace_watch.h"
#include "../insight/desktop/source_mask.h"
#include "../insight/desktop/relation_query.h"

typedef struct { gchar *path; GDir *dir; } ScanFrame;
static void frame_free(gpointer data) { ScanFrame *p=data; if(p->dir) g_dir_close(p->dir); g_free(p->path); g_free(p); }
typedef struct { GtkWidget *page,*home,*window,*dock_button,*maximize_button,*minimize_button,*tray_button,*opacity_button,*group_button,*ungroup_item,*drag_handle,*drag_target; gchar *label; guint order; gboolean pane,restore,visible,maximized,minimized; gint x,y,width,height,opacity,saved_group,saved_group_order,saved_group_active; } PanelHost;
typedef struct { GtkWidget *view,*entry,*status,*more; GtkTreeStore *store; gboolean symbols,setting; guint limit,shown,total,timer; } GroupedPanel;
typedef struct { gchar *path; guint id; gulong line; } Location;
static void location_free(gpointer data) { Location *p=data; g_free(p->path); g_free(p); }
typedef struct { gchar *path;gint lang;GPtrArray *tags; } OutlineFile;
static OutlineFile *outline_capture(TMSourceFile *file,const gchar *path) { OutlineFile *o=g_new0(OutlineFile,1);o->path=g_strdup(path);o->lang=file ? file->lang : -1;o->tags=g_ptr_array_new_with_free_func((GDestroyNotify)tm_tag_unref);if(file && file->tags_array) for(guint n=0;n<file->tags_array->len;n++) { TMTag *tag=g_ptr_array_index(file->tags_array,n);if(tag) g_ptr_array_add(o->tags,tm_tag_ref(tag)); }return o; }
static void outline_free(gpointer data) { OutlineFile *o=data;if(!o) return;g_free(o->path);g_ptr_array_unref(o->tags);g_free(o); }
static OutlineFile *outline_copy(OutlineFile *o) { OutlineFile *copy=g_new0(OutlineFile,1);copy->path=g_strdup(o->path);copy->lang=o->lang;copy->tags=g_ptr_array_ref(o->tags);return copy; }
typedef struct {
 GHashTable *declarations;
 GtkWidget *pane,*box,*title,*follow,*choices,*browser,*symbol_entry,*symbol_view,*outline,*file_entry,*file_view;
 GtkWidget *back,*forward,*open,*wrap,*project_notebook,*project_host,*right_pane;
 gint initial_right_split;
 GtkWidget *local_entry,*local_view,*local_label,*tree_view,*file_status,*tree_entry,*tree_status,*tree_follow,*folder_mode,*folder_stack,*disk_browser;
 guint tree_filter_idle; gboolean tree_setting;
 GtkListStore *local_symbols; GtkTreeStore *project_tree,*categories,*file_types;
 GtkWidget *category_entry,*category_view,*types_view,*float_window,*project_toggle,*local_toggle;
 GroupedPanel category_panel,type_panel;
 gint float_x,float_y,float_width,float_height; gboolean restore_float;
 InsightLayout layouts[4]; InsightPanelLayout main_bounds,main_target;guint main_apply_source,main_apply_stage,main_apply_ticks;gulong main_geometry_handlers[2]; PanelHost hosts[10]; guint host_count; GtkWidget *panel_commands,*tray,*tray_buttons,*tray_toggle,*transparency_toggle; gboolean tray_enabled,transparent_panels,panel_group_setting;
 GtkWidget *find_entry,*find_case,*find_word,*find_regex,*find_status,*context_find_row,*context_definitions_row,*context_find_toggle;
 gchar *find_last_query,*find_last_digest; gsize find_last_begin,find_last_end; guint find_last_flags;
 InsightNotes *project_marks;gchar *project_marks_path,*project_marks_root;GtkWidget *marks_scope,*marks_reload;gboolean marks_project,marks_valid,marks_stale,marks_initialized,marks_save_failed,marks_confirm_delete,marks_delete_on_close,marks_preserve;GHashTable *marks_session_stores;guint64 marks_generation;
 InsightNotes *notes,*project_notes; gchar *notes_path,*project_notes_path;gboolean clips_project,clips_prefer_project,clips_scope_setting,project_notes_valid;guint64 clips_generation;guint template_doc;gint template_begin,template_end;guint64 template_generation,template_version;gchar *template_prefix;gboolean template_pending;
 GHashTable *bookmark_handles;GtkWidget *bookmark_window,*bookmark_search;GtkListStore *bookmark_list;guint bookmark_sync_timer;gboolean bookmark_syncing;
 GtkWidget *marks_entry,*marks_view,*clips_entry,*clips_view,*clips_all,*clips_scope; gchar *clips_language; GtkListStore *marks_store,*clips_store;
 InsightWorkspaceWatch *watch; guint watch_idle; gboolean watch_changed,watch_incomplete;
 guint local_doc_id,local_symbol_limit,project_symbol_limit,file_event_idle; gulong symbol_key_signal;
 gchar *project_page_root,*project_page_profile;
 GeanyFiletype *preview_filetype; gboolean preview_styles_ready;
 GtkListStore *file_store,*matches;
 GtkListStore *search_matches,*usage_target;
 GtkWidget *search_view,*search_status,*search_page,*search_scope,*search_mask,*search_keywords,*search_variations,*search_context,*search_region;
 InsightSearchLinks *search_links; gchar *search_state_path; GPtrArray *search_files,*search_history; gboolean usage_keywords; guint usage_context; InsightSourceRegion project_region;
 GtkWidget *usage_entry,*usage_status,*usage_buffer,*usage_view,*usage_code,*usage_comments,*relation_transitive,*relation_mode,*relation_follow,*relation_enclosing,*relation_filter_entry;
 GtkTreeModelFilter *relation_filter;
 GtkWidget *relation_tree,*relation_tree_toggle,*relation_back,*relation_forward,*relation_stack,*relation_graph_toggle,*relation_graph,*relation_tree_graph;GPtrArray *relation_history,*relation_future;
 gchar *relation_word; gint relation_last_mode; gboolean relation_dirty,relation_setting,reference_setting;InsightSourceRegion usage_region;
 gint usage_mode; guint usage_flags;
 GtkWidget *local_stack,*local_mode,*native_symbol_query;
 GtkWidget *editor_history_combo;
 GtkWidget *editor_wrap,*editor_select,*editor_incremental,*editor_load_word;gboolean editor_setting;
 GtkWidget *project_query,*project_case,*project_word,*project_regex,*editor_query,*editor_case,*editor_word,*editor_regex,*project_search_row,*editor_search_row;
 gchar *editor_find_query,*editor_find_digest; gsize editor_find_begin,editor_find_end,editor_find_match_end; guint editor_find_id,editor_find_flags;
 GPtrArray *usage_paths; gchar *usage_name; guint usage_idle,usage_position,usage_shown,usage_skipped; GHashTable *usage_existing; GtkTreeIter usage_iter; gboolean usage_collecting,usage_iter_valid;
 GtkListStore *symbols;
 ScintillaObject *preview;
 GHashTable *preview_python_scopes;guint64 preview_python_version;
 InsightNativePythonBindings *preview_python_bindings;guint64 preview_python_binding_version;
 InsightNativeScopes *preview_scopes;gchar *preview_scopes_path;guint64 preview_scopes_version;gboolean preview_scopes_ready;
 GHashTable *preview_names,*preview_candidates; gsize preview_names_position; guint preview_links_idle; gchar *preview_links_text,*preview_code_mask,*preview_links_path; GeanyFiletype *preview_links_filetype; guint64 preview_links_version; gboolean preview_links_valid;gsize preview_links_length;gboolean preview_calls,preview_python; gint preview_links_position,preview_styled_position;
 guint editor_follow_id; gint editor_follow_position,editor_follow_anchor;
 gboolean editor_follow_focused;
 gchar *follow_word; guint follow_doc; gulong follow_line;
 guint preview_click_timer;
 guint refresh,source_id,panel_idle,index_event_idle; GHashTable *index_event_paths,*index_event_names; gboolean context_visible,index_dirty,scan_failed,report_scan_completed;
 gulong event_handlers[4];
 gulong project_handlers[3]; gchar *geany_root; gboolean workspace_from_project;
 gchar *prefs_path;
 InsightExtensions *extensions; gboolean lua_running;
 GtkWidget *lua_menu,*lua_commands,*lua_item,*palette_item;
 GtkAccelGroup *lua_accels,*navigation_accels;
 GtkCssProvider *css; GtkWidget *context_toggle;
 gboolean alive; guint64 scan_generation,usage_generation,definition_generation; guint definition_timer; GCancellable *scan_worker,*search_worker,*definition_worker,*snapshot_worker,*cache_worker,*preview_worker; guint64 preview_generation; guint64 index_generation,index_version; gint64 publish_due;
 InsightIndex *base,*index;
 gboolean info_parse_local,info_across_languages;gint info_scope;
 InsightProjectFiles *membership; gchar *membership_project; gboolean membership_valid;InsightMasterSync *master_sync;guint64 membership_generation;
 GPtrArray *frames,*files,*scan_files,*scan_trees,*scan_members; gboolean scan_keep_files; GHashTable *scan_seen; guint scan_file_position,scan_tree_position; gchar *root,*cache_path; gchar *db_identity,*db_policy;GHashTable *file_stamps,*directory_stamps;InsightIndex *disk_calls,*disk_declarations,*scan_backup;gboolean force_parse,cache_validation;guint reused; guint scan,parsed,skipped;
 gchar *source_path,*source_digest; gulong source_line;
 GPtrArray *history,*definitions; gint history_position; gboolean populating;
} Workbench;
static Workbench wb;
static guint64 workbench_epoch;
static void open_context(GtkButton *,gpointer);
static gboolean python_language(GeanyFiletype *);
static void show_diff(GtkWidget *,gpointer);
static void show_hex(GtkWidget *w,gpointer p) { (void)w;(void)p;GeanyDocument *doc=document_get_current();insight_hex_show(main_widgets.window,doc ? doc->real_path:NULL,NULL); }
static void diff_source_open(const gchar *path,guint line) {
 GeanyDocument *previous=document_get_current(),*doc=document_open_file(path,FALSE,NULL,NULL);if(!doc) return;
 navqueue_goto_line(previous,doc,CLAMP((gint)line,1,sci_get_line_count(doc->editor->sci)));document_grab_focus(doc);gtk_window_present(GTK_WINDOW(main_widgets.window));
}
static gboolean directory_path_open(const gchar *path) {
 gchar *real=utils_get_real_path(path);GFile *target=g_file_new_for_path(real ? real : path);g_free(real);gboolean found=FALSE;
 for(guint i=0;documents_array && i<documents_array->len && !found;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(!DOC_VALID(doc) || !doc->file_name) continue;
  gchar *native=doc->real_path ? g_strdup(doc->real_path) : g_filename_from_utf8(doc->file_name,-1,NULL,NULL,NULL);
  if(native) { GFile *file=g_file_new_for_path(native);found=g_file_equal(file,target);g_object_unref(file);g_free(native); }
 }
 g_object_unref(target);return found;
}
static gchar **directory_known_types(void) {
 InsightMasterTypes *types=insight_languages_master_types();GPtrArray *patterns=g_ptr_array_new_with_free_func(g_free);
 for(guint i=0;types->patterns && types->patterns[i];i++) g_ptr_array_add(patterns,g_strdup(types->patterns[i]));
 for(guint i=0;types->extensions && types->extensions[i];i++) g_ptr_array_add(patterns,g_strconcat("*.",types->extensions[i],NULL));
 insight_master_types_free(types);g_ptr_array_add(patterns,NULL);return (gchar **)g_ptr_array_free(patterns,FALSE);
}
static void show_directory_compare(GtkWidget *widget,gpointer data) { (void)widget;(void)data;insight_directory_show(main_widgets.window); }
static void browser_filter(GtkEditable *,gpointer);
static void files_filter(GtkEditable *,gpointer);
static void local_filter(GtkEditable *,gpointer);
static void tree_refresh(void);
static gboolean folder_locate(gboolean);
static gboolean folder_key(GtkWidget *,GdkEventKey *,gpointer);
static gboolean grouped_key_press(GtkWidget *,GdkEventKey *,gpointer);
static void category_activated(GtkTreeView *,GtkTreePath *,GtkTreeViewColumn *,gpointer);
static void categories_filter(GtkEditable *,gpointer);
static void file_types_refresh(void);
static GtkWidget *sort_selector(GtkListStore *);
static void stop_usage(void);
static void invalidate_search_context(void);
static void stop_definition(void);
static void stop_file_preview(void);
static void queue_definition(gboolean);
static gchar *cursor_query(GeanyDocument *);
static void mark_index_dirty(void);
static void native_commands_rebuild(void);
static void info_show_query(const gchar *,const gchar *,gulong);
static void symbol_text_from_outline(GtkTreeView *,guint);
static void save_workbench_settings(void);
static void save_search_settings(void);
static void focus_search_results(GtkWidget *,gpointer);
static void search_result_move(GtkWidget *,gpointer);
static void dock_all_panels(void);
static void detach_panel(guint);
static void panel_tray_sync(void);
static void panel_restore(guint);
static void panel_minimize(guint);
static void focus_widget_panel(GtkWidget *);
static void select_project_tab(gint);
static gboolean start_scan(const char *);
static void manage_project_files(GtkWidget *,gpointer);
static InsightReportSnapshot *capture_project_report(GError **);
static void project_files_drop(GtkWidget *,GdkDragContext *,gint,gint,GtkSelectionData *,guint,guint,gpointer);
static void workspace_changed(gpointer);
static void create_search_toolbars(void);
static guint text_search_flags(GtkWidget *,GtkWidget *,GtkWidget *);
static char *read_disk_source(const char *,gboolean);
enum { SP_OPEN,SP_INFO,SP_REFERENCES,SP_NAME,SP_LOCATION,SP_ONLY,SP_HIDE,SP_ALL,SP_GROUP,SP_EXPAND,SP_COLLAPSE,SP_HELP,SP_MORE,SP_LOCATE,SP_COLUMNS,SP_QUALIFIED,SP_PATH,SP_LINE,SP_FILTER_NAME,SP_FILTER_SCOPE,SP_FILTER_FILE,SP_CLEAR,SP_EXPAND_ONE,SP_COLLAPSE_ONE,SP_INVERT,SP_VISIBILITY,SP_COPY_TEXT,SP_CUT_TEXT,SP_DUPLICATE,SP_PASTE_TEXT,SP_COUNT };
enum { FP_OPEN,FP_NAME,FP_PATH,FP_FILTER,FP_CLEAR,FP_RESET,FP_COLUMNS,FP_HELP,FP_REVEAL,FP_COUNT };
static void file_action(guint);
static gboolean file_popup_key(GtkWidget *,gpointer);
static void symbol_action(GtkTreeView *,guint);
static void browser_symbol_info(GtkWidget *,gpointer);
static gboolean symbol_popup_key(GtkWidget *,gpointer);
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
static void decorate_match(ScintillaObject *sci,gsize begin,gsize end);
typedef struct {
 guint timer,apply; guint64 generation,text_version,requested_version;
 gint begin,end,length; gboolean alive,highlighted;
 gsize match_begin,match_end; guint64 match_version; gboolean match_ready;
 GCancellable *cancel; InsightSearchMatch *matches; gsize count,position;
} SelectionState;
typedef struct { GWeakRef sci; gchar *text,*query; InsightSearchMatch *matches; gsize count; guint64 generation; } SelectionJob;
static void selection_stop(SelectionState *s) {
 s->generation++;
 if(s->timer) { g_source_remove(s->timer); s->timer=0; }
 if(s->apply) { g_source_remove(s->apply); s->apply=0; }
 if(s->cancel) { g_cancellable_cancel(s->cancel); g_clear_object(&s->cancel); }
 g_clear_pointer(&s->matches,g_free); s->count=s->position=0;
}
static void selection_state_free(gpointer data) { SelectionState *s=data; selection_stop(s); g_free(s); }
static void selection_destroy(GtkWidget *widget,gpointer data) { (void)widget; SelectionState *s=data; s->alive=FALSE; selection_stop(s); }
static void selection_modified(ScintillaObject *sci,gint message,SCNotification *n,gpointer data) {
 (void)sci; (void)message; SelectionState *s=data;
 if(n->nmhdr.code==SCN_MODIFIED && (n->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) { s->text_version++; selection_stop(s); }
}
static SelectionState *selection_state(ScintillaObject *sci) {
 SelectionState *s=g_object_get_data(G_OBJECT(sci),"insight-selection-worker");
 if(!s) { s=g_new0(SelectionState,1); s->alive=TRUE; s->begin=s->end=-1; g_object_set_data_full(G_OBJECT(sci),"insight-selection-worker",s,selection_state_free); g_signal_connect(sci,"sci-notify",G_CALLBACK(selection_modified),s); g_signal_connect(sci,"destroy",G_CALLBACK(selection_destroy),s); }
 return s;
}
static void decorate_match(ScintillaObject *sci,gsize begin,gsize end) {
 SelectionState *state=selection_state(sci);
 if(state->match_ready && state->match_begin==begin && state->match_end==end && state->match_version==state->text_version) return;
 state->match_ready=TRUE;state->match_begin=begin;state->match_end=end;state->match_version=state->text_version;
 decorate_setup(sci);
 /* The normal blue selection must not tint a selected search hit green. */
 SSM(sci,SCI_SETSELBACK,TRUE,0x00ffff); SSM(sci,SCI_SETSELFORE,FALSE,0); SSM(sci,SCI_SETSELALPHA,SC_ALPHA_NOALPHA,0);
 for(gint indicator=INSIGHT_MATCH_FILL;indicator<=INSIGHT_MATCH_FRAME;indicator++) {
  SSM(sci,SCI_SETINDICATORCURRENT,indicator,0); SSM(sci,SCI_INDICATORCLEARRANGE,0,sci_get_length(sci));
  if(end>begin && end<=(gsize)sci_get_length(sci)) SSM(sci,SCI_INDICATORFILLRANGE,begin,end-begin);
 }
}
static gboolean selection_apply(gpointer data) {
 ScintillaObject *sci=data; SelectionState *s=selection_state(sci); gint64 deadline=g_get_monotonic_time()+3000;
 if(!s->alive) { s->apply=0; return G_SOURCE_REMOVE; }
 SSM(sci,SCI_SETINDICATORCURRENT,INSIGHT_SAME_TEXT,0);
 while(s->position<s->count) { InsightSearchMatch *hit=&s->matches[s->position++]; SSM(sci,SCI_INDICATORFILLRANGE,hit->start,hit->end-hit->start); s->highlighted=TRUE; if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE; }
 s->apply=0; g_clear_pointer(&s->matches,g_free); return G_SOURCE_REMOVE;
}
static void selection_job_free(gpointer data) { SelectionJob *j=data; g_weak_ref_clear(&j->sci); g_free(j->text); g_free(j->query); g_free(j->matches); g_free(j); }
static void selection_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; SelectionJob *j=data; j->matches=g_new(InsightSearchMatch,10000); gsize total=0;
 if(!g_cancellable_is_cancelled(cancel) && insight_context_collect(j->text,strlen(j->text),j->query,INSIGHT_SEARCH_CASE|INSIGHT_SEARCH_WORD,j->matches,10000,&total,NULL)) j->count=MIN(total,10000);
 g_task_return_boolean(task,TRUE);
}
static void selection_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); SelectionJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL); ScintillaObject *sci=g_weak_ref_get(&j->sci); if(!sci) return;
 SelectionState *s=selection_state(sci);
 if(ok && s->alive && s->generation==j->generation) {
  g_clear_object(&s->cancel); s->matches=j->matches; j->matches=NULL; s->count=j->count; s->position=0;
  s->apply=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,selection_apply,g_object_ref(sci),g_object_unref);
 }
 g_object_unref(sci);
}
static gboolean selection_timeout(gpointer data) {
 ScintillaObject *sci=data; SelectionState *s=selection_state(sci); s->timer=0;
 if(!s->alive) return G_SOURCE_REMOVE;
 SelectionJob *j=g_new0(SelectionJob,1); g_weak_ref_init(&j->sci,sci); j->generation=s->generation; j->text=sci_get_contents(sci,s->length+1); j->query=g_strndup(j->text+s->begin,(gsize)(s->end-s->begin));
 s->cancel=g_cancellable_new(); GTask *task=g_task_new(NULL,s->cancel,selection_job_done,NULL); g_task_set_task_data(task,j,selection_job_free); g_task_run_in_thread(task,selection_job_run); g_object_unref(task); return G_SOURCE_REMOVE;
}
static void decorate_selection(ScintillaObject *sci) {
 SelectionState *s=selection_state(sci); gint length=sci_get_length(sci),begin=(gint)SSM(sci,SCI_GETSELECTIONSTART,0,0),end=(gint)SSM(sci,SCI_GETSELECTIONEND,0,0);
 if(!s->alive || (s->length==length && s->begin==begin && s->end==end && s->requested_version==s->text_version)) return;
 selection_stop(s); s->length=length; s->begin=begin; s->end=end; s->requested_version=s->text_version;
 if(s->highlighted) { SSM(sci,SCI_SETINDICATORCURRENT,INSIGHT_SAME_TEXT,0); SSM(sci,SCI_INDICATORCLEARRANGE,0,MAX(0,length)); s->highlighted=FALSE; }
 if(length<0 || length>2*1024*1024 || begin<0 || end>length || end<=begin || end-begin>1024) return;
 decorate_setup(sci); s->timer=g_timeout_add_full(G_PRIORITY_DEFAULT,150,selection_timeout,g_object_ref(sci),g_object_unref);
}
static void editor_mark_symbol(GeanyDocument *doc,gulong line,const gchar *name) {
 ScintillaObject *sci=doc->editor->sci; SSM(sci,SCI_ENSUREVISIBLE,line-1,0);SSM(sci,SCI_GOTOLINE,line-1,0);
 gchar *text=sci_get_line(sci,(gint)line-1);InsightSearchMatch match;gsize total=0;gint start=sci_get_position_from_line(sci,(gint)line-1);
 if(name && *name && insight_context_collect(text,strlen(text),name,INSIGHT_SEARCH_CASE|INSIGHT_SEARCH_WORD,&match,1,&total,NULL) && total) { SSM(sci,SCI_SETSEL,start+match.start,start+match.end);decorate_match(sci,start+match.start,start+match.end); }else decorate_match(sci,0,0);g_free(text);
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
static gboolean bookmark_key(GtkWidget *,GdkEventKey *,gpointer);
static void browser_activated(GtkTreeView *,GtkTreePath *,GtkTreeViewColumn *,gpointer);
static gboolean browser_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)data;
 if(widget==wb.file_view) {
  guint modifiers=event->state&gtk_accelerator_get_default_mod_mask(),key=gdk_keyval_to_lower(event->keyval);
  if((key==GDK_KEY_Menu && !modifiers) || (key==GDK_KEY_F10 && modifiers==GDK_SHIFT_MASK)) return file_popup_key(widget,NULL);
  if(key==GDK_KEY_f && modifiers==GEANY_PRIMARY_MOD_MASK) { gtk_widget_grab_focus(wb.file_entry);return TRUE; }
  if(key==GDK_KEY_c && (modifiers==GEANY_PRIMARY_MOD_MASK || modifiers==(GEANY_PRIMARY_MOD_MASK|GDK_SHIFT_MASK))) { file_action(modifiers&GDK_SHIFT_MASK ? FP_PATH : FP_NAME);return TRUE; }
  if((key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) && modifiers==GEANY_PRIMARY_MOD_MASK) { file_action(FP_OPEN);return TRUE; }
 }
 if(widget==wb.symbol_view || widget==wb.local_view) {
  guint modifiers=event->state&gtk_accelerator_get_default_mod_mask(); guint key=gdk_keyval_to_lower(event->keyval);
  if((key==GDK_KEY_Menu && !modifiers) || (key==GDK_KEY_F10 && modifiers==GDK_SHIFT_MASK)) return symbol_popup_key(widget,NULL);
  if(key==GDK_KEY_f && modifiers==GEANY_PRIMARY_MOD_MASK) { gtk_widget_grab_focus(widget==wb.local_view ? wb.local_entry : wb.symbol_entry); return TRUE; }
  if(!modifiers && (key==GDK_KEY_space || key==GDK_KEY_Return || key==GDK_KEY_KP_Enter)) { GtkTreeModel *model=NULL;GtkTreeIter iter;if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)),&model,&iter)) { GtkTreePath *row=gtk_tree_model_get_path(model,&iter);browser_activated(GTK_TREE_VIEW(widget),row,NULL,NULL);gtk_tree_path_free(row); }return TRUE; }
  if(key==GDK_KEY_c && (modifiers==GEANY_PRIMARY_MOD_MASK || modifiers==(GEANY_PRIMARY_MOD_MASK|GDK_SHIFT_MASK))) { symbol_action(GTK_TREE_VIEW(widget),modifiers&GDK_SHIFT_MASK ? SP_LOCATION : SP_NAME); return TRUE; }
  if((key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) && (modifiers==GEANY_PRIMARY_MOD_MASK || modifiers==GDK_MOD1_MASK)) { symbol_action(GTK_TREE_VIEW(widget),modifiers==GDK_MOD1_MASK ? SP_INFO : SP_OPEN); return TRUE; }
 }
 if(event->keyval==GDK_KEY_Escape) { editor_focus(); return TRUE; }
 if(event->keyval==GDK_KEY_Down && (widget==wb.symbol_entry || widget==wb.file_entry || widget==wb.local_entry)) {
  GtkWidget *view=widget==wb.local_entry ? wb.local_view : (widget==wb.symbol_entry ? wb.symbol_view : wb.file_view);
  if(!gtk_widget_get_sensitive(view)) return TRUE;
  GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view)); GtkTreeIter iter;
  if(gtk_tree_model_get_iter_first(model,&iter)) {
   GtkTreeIter child; while(gtk_tree_model_iter_children(model,&child,&iter)) iter=child;
   GtkTreePath *path=gtk_tree_model_get_path(model,&iter);
   gtk_tree_view_expand_to_path(GTK_TREE_VIEW(view),path); gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),path,NULL,FALSE); gtk_tree_path_free(path); gtk_widget_grab_focus(view);
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
/* Keep the lexer and language profile stable while following the same source. */
static GeanyFiletype *preview_filetype(const gchar *path) {
 GeanyFiletype *ft=path ? insight_languages_detect(path) : NULL;
 return ft ? ft : (path ? filetypes_detect_from_file(path) : NULL);
}
static void render_source(const char *text,GeanyFiletype *filetype,gulong line,const gchar *path) {
 gchar *previous=sci_get_contents(wb.preview,sci_get_length(wb.preview)+1);
 gboolean unchanged=!strcmp(previous,text);
 gboolean restyle=!wb.preview_styles_ready || wb.preview_filetype!=filetype || g_strcmp0(wb.source_path,path);
 g_free(previous);
 if(restyle) {
  wb.preview_filetype=filetype;wb.preview_styles_ready=TRUE;wb.preview_links_valid=FALSE;
  highlighting_set_styles(wb.preview,filetype ? filetype : filetypes[GEANY_FILETYPES_NONE]);
  insight_languages_apply(wb.preview,path,filetype);
 }
 if(!unchanged) {
  sci_set_readonly(wb.preview,FALSE);sci_set_text(wb.preview,text);
  sci_set_line_numbers(wb.preview,TRUE);
  SSM(wb.preview,SCI_SETCARETLINEVISIBLE,TRUE,0);
  SSM(wb.preview,SCI_EMPTYUNDOBUFFER,0,0);sci_set_readonly(wb.preview,TRUE);
  decorate_match(wb.preview,0,0);
 }
 /* Colour the first chunk before symbol-link indexing, even for a large index.
  * Scintilla styles the remaining visible range on demand. */
 if(restyle || !unchanged) SSM(wb.preview,SCI_COLOURISE,0,MIN(sci_get_length(wb.preview),16384));
 gint target=(gint)MIN(line ? line-1 : 0,(gulong)MAX(0,sci_get_line_count(wb.preview)-1));
 if(unchanged && sci_get_current_line(wb.preview)==target) return;
 SSM(wb.preview,SCI_GOTOLINE,target,0);
 SSM(wb.preview,SCI_VERTICALCENTRECARET,0,0);
}
static void refresh_index(void);
static void usage_start(GtkWidget *,gpointer);
static void relation_view_update(void);
static void relation_report_show(GtkWidget *,gpointer);
static void relation_print_show(GtkWidget *,gpointer);
typedef struct { InsightIndex *base,*overlay,*result,*previous; GPtrArray *paths; GHashTable *changed,*names,*all_names; guint64 epoch,generation; gboolean valid; } SnapshotJob;
static void retire_index_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) { (void)source; (void)cancel; insight_index_free(data); g_task_return_boolean(task,TRUE); }
static void retire_index(InsightIndex *index) { if(!index) return; GTask *task=g_task_new(NULL,NULL,NULL,NULL); g_task_set_task_data(task,index,NULL); g_task_run_in_thread(task,retire_index_run); g_object_unref(task); }
static void retire_names_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) { (void)source; (void)cancel; g_hash_table_unref(data); g_task_return_boolean(task,TRUE); }
static void retire_names(GHashTable *names) { if(!names) return; GTask *task=g_task_new(NULL,NULL,NULL,NULL); g_task_set_task_data(task,names,NULL); g_task_run_in_thread(task,retire_names_run); g_object_unref(task); }
static void snapshot_job_free(gpointer data) { SnapshotJob *j=data; retire_index(j->base); retire_index(j->overlay); retire_index(j->result); retire_index(j->previous); retire_names(j->changed); retire_names(j->names); retire_names(j->all_names); g_ptr_array_free(j->paths,TRUE); g_free(j); }
static void mark_index_dirty(void) { wb.index_generation++; wb.index_dirty=TRUE; if(wb.snapshot_worker) { g_cancellable_cancel(wb.snapshot_worker); g_clear_object(&wb.snapshot_worker); } }
/* Worker-owned per-file fingerprints: publication reports only files whose
 * symbol records actually changed, including removals and empty overlays. */
static GHashTable *index_file_fingerprints(const InsightIndex *index) {
 GHashTable *table=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)g_checksum_free);
 for(gsize i=0;i<insight_index_count(index);i++) {
  const InsightSymbol *s=insight_index_at(index,i); GChecksum *sum=g_hash_table_lookup(table,s->path);
  if(!sum) { sum=g_checksum_new(G_CHECKSUM_SHA256); g_hash_table_insert(table,g_strdup(s->path),sum); }
  const gchar *fields[]={s->name,s->scope,s->kind,s->signature,s->type,s->inheritance};
  for(guint k=0;k<G_N_ELEMENTS(fields);k++) { const gchar *v=fields[k] ? fields[k] : ""; g_checksum_update(sum,(const guchar *)v,strlen(v)+1); }
  gchar line[32]; g_snprintf(line,sizeof line,"%lu",s->line); g_checksum_update(sum,(const guchar *)line,strlen(line)+1);
 }
 return table;
}
static void index_collect_changes(SnapshotJob *j) {
 GHashTable *old=index_file_fingerprints(j->previous),*next=index_file_fingerprints(j->result);
 j->changed=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 GHashTableIter it; gpointer path,value; g_hash_table_iter_init(&it,next);
 while(g_hash_table_iter_next(&it,&path,&value)) { GChecksum *before=g_hash_table_lookup(old,path); if(!before || strcmp(g_checksum_get_string(before),g_checksum_get_string(value))) g_hash_table_add(j->changed,g_strdup(path)); }
 g_hash_table_iter_init(&it,old); while(g_hash_table_iter_next(&it,&path,&value)) if(!g_hash_table_contains(next,path)) g_hash_table_add(j->changed,g_strdup(path));
 j->names=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 j->all_names=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 const InsightIndex *snapshots[]={j->previous,j->result};
 for(guint k=0;k<G_N_ELEMENTS(snapshots);k++) for(gsize n=0;n<insight_index_count(snapshots[k]);n++) {
  const InsightSymbol *tag=insight_index_at(snapshots[k],n);
  if(k==1 && !g_hash_table_contains(j->all_names,tag->name)) g_hash_table_add(j->all_names,g_strdup(tag->name));
  if(g_hash_table_contains(j->changed,tag->path)) g_hash_table_add(j->names,g_strdup(tag->name));
 }
 g_hash_table_destroy(old); g_hash_table_destroy(next);
}
/* Coalesced change event. Consumers read the latest published snapshot;
 * a local-file view is not invalidated by changes to another file. */
static gboolean index_event_dispatch(gpointer data) {
 (void)data; wb.index_event_idle=0; if(!wb.alive) return G_SOURCE_REMOVE;
 GHashTable *paths=wb.index_event_paths,*names=wb.index_event_names; wb.index_event_paths=wb.index_event_names=NULL;
 if(paths && g_hash_table_size(paths)) {
  browser_filter(NULL,NULL); categories_filter(NULL,NULL);
  GeanyDocument *doc=document_get_current();
  if(doc && doc->file_name && g_hash_table_contains(paths,doc->file_name)) local_filter(NULL,NULL);
  wb.preview_links_position=0; preview_links();
  /* Definitions depend on the queried name, not every project file.
   * Owner/inheritance queries retain broader invalidation until their
   * dependency sets are recorded explicitly. Python aliases likewise require
   * broad invalidation until imported-module dependencies are recorded. */
  gint mode=wb.relation_mode ? gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)) : -1;
  const gchar *relation=wb.usage_entry ? gtk_entry_get_text(GTK_ENTRY(wb.usage_entry)) : NULL;
  while(relation && g_str_has_prefix(relation,"::")) relation+=2;
  if(mode!=1 || (relation && names && g_hash_table_contains(names,relation))) wb.relation_dirty=TRUE;
  gchar *query=doc ? cursor_query(doc) : NULL;
  gboolean definition_changed=(doc && doc->file_name && g_hash_table_contains(paths,doc->file_name)) || (query && names && g_hash_table_contains(names,query)) || (doc && python_language(doc->file_type));
  g_free(query);
  if(definition_changed) { wb.follow_doc=0; queue_definition(TRUE); }
  if(wb.relation_dirty || definition_changed) insight_workbench_refresh();
  /* A manual tag query may have completed against the previous snapshot
   * while the asynchronous document overlay was still being published. */
  if(wb.relation_dirty && relation && *relation && (mode==1 || mode==2 || mode==4 || mode==5) &&
     !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_follow))) { wb.relation_dirty=FALSE;usage_start(NULL,NULL); }
 }
 retire_names(paths);
 retire_names(names);
 return G_SOURCE_REMOVE;
}
static void index_event_publish(GHashTable *changed,GHashTable *names) {
 if(!changed || !g_hash_table_size(changed)) return;
 /* The common single publication transfers references in constant time.
  * Only overlapping queued events need to merge their dependency sets. */
 GHashTableIter it; gpointer key;
 if(!wb.index_event_paths) wb.index_event_paths=g_hash_table_ref(changed);
 else { g_hash_table_iter_init(&it,changed); while(g_hash_table_iter_next(&it,&key,NULL)) g_hash_table_add(wb.index_event_paths,g_strdup(key)); }
 if(names) {
  if(!wb.index_event_names) wb.index_event_names=g_hash_table_ref(names);
  else { g_hash_table_iter_init(&it,names); while(g_hash_table_iter_next(&it,&key,NULL)) g_hash_table_add(wb.index_event_names,g_strdup(key)); }
 }
 if(!wb.index_event_idle) wb.index_event_idle=g_idle_add(index_event_dispatch,NULL);
}
static void snapshot_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; SnapshotJob *j=data; if(g_cancellable_is_cancelled(cancel)) { g_task_return_boolean(task,FALSE); return; }
 j->result=insight_index_clone(j->base); gboolean ok=j->valid && j->result!=NULL; gsize count=j->paths->len;
 gchar **paths=(gchar **)j->paths->pdata;
 for(gsize n=0;ok && n<count && !g_cancellable_is_cancelled(cancel);n++) {
  gsize size=insight_index_query(j->overlay,"",FALSE,NULL,NULL,paths[n],0,NULL,0); const InsightSymbol **tags=g_new(const InsightSymbol *,size); InsightSymbol *rows=g_new(InsightSymbol,size); insight_index_query(j->overlay,"",FALSE,NULL,NULL,paths[n],0,tags,size); for(gsize k=0;k<size;k++) rows[k]=*tags[k];
  gsize old=insight_index_query(j->result,"",FALSE,NULL,NULL,paths[n],0,NULL,0); ok=insight_index_count(j->result)-old+size<=250000 && insight_index_update(j->result,paths[n],rows,size); g_free(rows); g_free(tags);
 }
 if(ok && !g_cancellable_is_cancelled(cancel)) index_collect_changes(j);
 g_task_return_boolean(task,ok && !g_cancellable_is_cancelled(cancel));
}
static void snapshot_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); SnapshotJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.index_generation) return;
 g_clear_object(&wb.snapshot_worker);
 if(!ok) { ui_set_statusbar(TRUE,"Index snapshot could not be rebuilt; previous snapshot retained"); return; }
 /* No symbol changes: keep the existing version and borrowed GUI pointers. */
 if(!g_hash_table_size(j->changed)) return;
 /* Stop decoration before atomically replacing its owned name snapshot. */
 if(wb.preview_links_idle) { g_source_remove(wb.preview_links_idle); wb.preview_links_idle=0; }
 /* Candidate arrays borrow symbols from the current index. Drop them before
  * retirement, including the gap before the coalesced decoration refresh. */
 if(wb.preview_candidates) g_hash_table_remove_all(wb.preview_candidates);
 retire_names(wb.preview_names); wb.preview_names=j->all_names; j->all_names=NULL;
 retire_index(wb.index); wb.index=j->result; j->result=NULL; wb.index_version++; wb.preview_names_position=insight_index_count(wb.index);
 index_event_publish(j->changed,j->names);
}
static void refresh_index(void) {
 if(!wb.alive || wb.scan_worker || wb.snapshot_worker || !wb.base || !wb.index_dirty) return;
 SnapshotJob *j=g_new0(SnapshotJob,1); j->base=insight_index_ref(wb.base); j->previous=insight_index_ref(wb.index); j->overlay=insight_index_new(); j->paths=g_ptr_array_new_with_free_func(g_free); j->valid=j->overlay!=NULL; j->epoch=workbench_epoch; j->generation=wb.index_generation;
 gchar *root=wb.workspace_from_project && wb.root ? utils_get_utf8_from_locale(wb.root) : NULL;
 for(guint n=0;n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n); if(doc->is_valid && doc->file_name && (!root || insight_project_files_contains(wb.membership,root,doc->file_name))) { g_ptr_array_add(j->paths,g_strdup(doc->file_name)); if(!insight_native_import(j->overlay,doc->tm_file,doc->file_name)) j->valid=FALSE; } }
 g_free(root); wb.index_dirty=FALSE; wb.snapshot_worker=g_cancellable_new(); GTask *task=g_task_new(NULL,wb.snapshot_worker,snapshot_job_done,NULL); g_task_set_task_data(task,j,snapshot_job_free); g_task_run_in_thread(task,snapshot_job_run); g_object_unref(task);
}
static gboolean definition_language(const gchar *source,GeanyFiletype *from,const gchar *path) {
 return insight_languages_compatible(source,from,path);
}
typedef struct { const gchar *path;GeanyFiletype *type; } ScopeLanguage;
static int scope_language_eligible(const InsightSymbol *symbol,void *data) { ScopeLanguage *scope=data;return definition_language(scope->path,scope->type,symbol->path); }
static gboolean python_language(GeanyFiletype *type) { return type && type->lang==TM_PARSER_PYTHON; }
static gboolean member_calls_language(GeanyFiletype *type) { return type && (type->lang==TM_PARSER_C || type->lang==TM_PARSER_CPP || type->lang==TM_PARSER_CUDA); }
static gboolean definition_keyword(GeanyFiletype *ft,const gchar *word) {
 if(ft && (ft->lang==TM_PARSER_C || ft->lang==TM_PARSER_CPP || ft->lang==TM_PARSER_CUDA) && (!strcmp(word,"false") || !strcmp(word,"true") || !strcmp(word,"nullptr"))) return TRUE;
 return highlighting_is_primary_keyword(ft,word);
}
static gboolean definition_code(ScintillaObject *sci,gint position) {
 gint lexer=SSM(sci,SCI_GETLEXER,0,0),style=SSM(sci,SCI_GETSTYLEAT,position,0);
 return !highlighting_is_comment_style(lexer,style) && !highlighting_is_string_style(lexer,style);
}
static void preview_scopes_clear(void) {
 g_clear_pointer(&wb.preview_python_bindings,insight_native_python_bindings_free);g_clear_pointer(&wb.preview_python_scopes,g_hash_table_unref);g_clear_pointer(&wb.preview_scopes,insight_native_scopes_free);g_clear_pointer(&wb.preview_scopes_path,g_free);wb.preview_scopes_ready=FALSE;
}
static InsightNativeSource preview_python_source(void) {
 InsightNativeSource source={wb.preview_links_text,wb.preview_code_mask,wb.preview_links_length,NULL};
 if(!wb.preview_python_bindings || wb.preview_python_binding_version!=wb.index_version) {
  g_clear_pointer(&wb.preview_python_bindings,insight_native_python_bindings_free);
  wb.preview_python_bindings=insight_native_python_bindings_new(wb.index,wb.source_path,&source);wb.preview_python_binding_version=wb.index_version;
 }
 source.python=wb.preview_python_bindings;return source;
}
static gchar *preview_function_scope(gulong line,gint position,gboolean *uncertain) {
 *uncertain=FALSE;
 if(python_language(wb.preview_filetype)) {
  if(!wb.preview_python_scopes || wb.preview_python_version!=wb.index_version) { g_clear_pointer(&wb.preview_python_scopes,g_hash_table_unref);wb.preview_python_scopes=g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,g_free);wb.preview_python_version=wb.index_version; }
  gpointer cached=NULL;if(g_hash_table_lookup_extended(wb.preview_python_scopes,GUINT_TO_POINTER((guint)line),NULL,&cached)) return *(gchar *)cached ? g_strdup(cached) : NULL;
  InsightNativeSource source={wb.preview_links_text,wb.preview_code_mask,wb.preview_links_length,NULL};gchar *scope=insight_native_python_scope(wb.index,wb.source_path,&source,(gsize)position);g_hash_table_insert(wb.preview_python_scopes,GUINT_TO_POINTER((guint)line),g_strdup(scope ? scope : ""));return scope;
 }
 if(!member_calls_language(wb.preview_filetype)) return insight_native_function_scope(wb.index,wb.source_path,line);
 if(!wb.preview_scopes_ready || wb.preview_scopes_version!=wb.index_version || g_strcmp0(wb.preview_scopes_path,wb.source_path)) {
  preview_scopes_clear();InsightNativeSource source={wb.preview_links_text,wb.preview_code_mask,wb.preview_links_length,NULL};wb.preview_scopes=insight_native_scopes_new(wb.index,wb.source_path,&source);wb.preview_scopes_path=g_strdup(wb.source_path);wb.preview_scopes_version=wb.index_version;wb.preview_scopes_ready=TRUE;
 }
 return insight_native_scopes_at(wb.preview_scopes,line,(gsize)position,uncertain);
}
static gboolean preview_resolvable(const gchar *word,gint position) {
 if(insight_languages_keyword(wb.preview,word) || definition_keyword(wb.preview_filetype,word) || !definition_code(wb.preview,position)) return FALSE;
 GPtrArray *tags=g_hash_table_lookup(wb.preview_candidates,word);if(!tags && !python_language(wb.preview_filetype)) return FALSE;
 const gchar *lexical=member_calls_language(wb.preview_filetype) ? wb.preview_code_mask : NULL;
 if(lexical && position>=0 && (gsize)position<wb.preview_links_length && lexical[position]!=wb.preview_links_text[position]) return FALSE;
 gboolean scoped=FALSE,member=FALSE;gint at=position;while(at>0 && g_ascii_isspace(lexical ? lexical[at-1] : sci_get_char_at(wb.preview,at-1))) at--;
 gchar before=at>0 ? (lexical ? lexical[at-1] : sci_get_char_at(wb.preview,at-1)) : 0,previous=at>1 ? (lexical ? lexical[at-2] : sci_get_char_at(wb.preview,at-2)) : 0;
 gboolean qualified=before=='.' || (at>1 && ((previous=='-' && before=='>') || (previous==':' && before==':')));
 for(guint i=0;tags && i<tags->len;i++) { const InsightSymbol *tag=g_ptr_array_index(tags,i);if(!strcmp(tag->kind,"l") || !strcmp(tag->kind,"z")) scoped=TRUE; }
 gulong line=(gulong)SSM(wb.preview,SCI_LINEFROMPOSITION,position,0)+1;
 gboolean uncertain_scope=FALSE;gchar *scope=(scoped || qualified || python_language(wb.preview_filetype)) ? preview_function_scope(line,position,&uncertain_scope) : NULL,*owner=NULL;
 if(uncertain_scope) { g_free(scope);return FALSE; }
 if(python_language(wb.preview_filetype)) {
  InsightNativeSource text=preview_python_source();gboolean imported=FALSE;const InsightSymbol *hit;ScopeLanguage language={wb.source_path,wb.preview_filetype};
  gsize count=insight_native_python_import_query(wb.index,wb.source_path,scope,&text,position,word,scope_language_eligible,&language,&hit,1,&imported);
  if(imported || !tags) { g_free(scope);return count>0; }
 }
 if(qualified) { member=TRUE;if(wb.preview_links_text) { InsightNativeSource reference={wb.preview_links_text,wb.preview_code_mask,wb.preview_links_length,NULL};if(python_language(wb.preview_filetype)) reference=preview_python_source();ScopeLanguage language={wb.source_path,wb.preview_filetype};owner=python_language(wb.preview_filetype) ? insight_native_python_owner(wb.index,wb.source_path,scope,line,&reference,position,&member,scope_language_eligible,&language) : insight_native_member_owner_source_filtered(wb.index,wb.source_path,scope,line,&reference,position,&member,member_calls_language(wb.preview_filetype) && wb.preview_code_mask!=NULL,scope_language_eligible,&language); } }
 gboolean found=FALSE;
 if(!member && python_language(wb.preview_filetype)) {
  InsightNativeSource text=preview_python_source();gchar *binding=insight_native_python_binding_scope(wb.index,wb.source_path,scope,word,&text);g_free(scope);scope=binding;
  if(!scope) { g_free(owner);return FALSE; }
 }
 if(member) { ScopeLanguage language={wb.source_path,wb.preview_filetype};const InsightSymbol *hit;found=insight_member_query(wb.index,owner,word,wb.source_path,scope_language_eligible,&language,&hit,1,NULL,NULL)>0; }
 else for(guint i=0;i<tags->len;i++) { const InsightSymbol *tag=g_ptr_array_index(tags,i);if((python_language(wb.preview_filetype) ? insight_native_python_rank(wb.index,tag,wb.source_path,scope,line) : insight_native_definition_rank_at(tag,wb.source_path,scope,line))>=0) { found=TRUE;break; } }
 g_free(scope);g_free(owner);return found;
}
static gboolean preview_links_step(gpointer data) {
 (void)data; gint64 deadline=g_get_monotonic_time()+6000;
 gsize count=insight_index_count(wb.index);
 while(wb.preview_names_position<count) {
  const InsightSymbol *symbol=insight_index_at(wb.index,wb.preview_names_position++);
  if(symbol && definition_language(wb.source_path,wb.preview_filetype,symbol->path) && !definition_keyword(wb.preview_filetype,symbol->name)) {
   GPtrArray *tags=g_hash_table_lookup(wb.preview_candidates,symbol->name);
   if(!tags) { tags=g_ptr_array_new();g_hash_table_insert(wb.preview_candidates,g_strdup(symbol->name),tags); }
   g_ptr_array_add(tags,(gpointer)symbol);
  }
  if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
 }
 SSM(wb.preview,SCI_SETINDICATORCURRENT,INSIGHT_LINK,0);
 gint length=(gint)wb.preview_links_length;
 while(wb.preview_links_position<length) {
  gint position=wb.preview_links_position;if(position>=wb.preview_styled_position) { wb.preview_styled_position=MIN(length,position+16384);SSM(wb.preview,SCI_COLOURISE,position,wb.preview_styled_position); }gint end=(gint)SSM(wb.preview,SCI_WORDENDPOSITION,position,TRUE);
  if(end<=position) wb.preview_links_position=(gint)SSM(wb.preview,SCI_POSITIONAFTER,position,0);
  else {
   gint start=(gint)SSM(wb.preview,SCI_WORDSTARTPOSITION,position,TRUE);
   if(start>=0 && end<=length && end-start<=1024) { gchar *name=g_strndup(wb.preview_links_text+start,(gsize)(end-start)); if(preview_resolvable(name,start)) { SSM(wb.preview,SCI_SETINDICATORCURRENT,INSIGHT_LINK,0);SSM(wb.preview,SCI_INDICATORFILLRANGE,start,end-start); } g_free(name); }
   wb.preview_links_position=end;
  }
  if(wb.preview_links_position<=position) wb.preview_links_position=position+1;
  if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
 }
 wb.preview_links_idle=0; return G_SOURCE_REMOVE;
}
static void preview_links(void) {
 if(!wb.preview) return;
 refresh_index();
 gchar *text=sci_get_contents(wb.preview,sci_get_length(wb.preview)+1);
 if(wb.preview_links_valid && wb.preview_links_text && !strcmp(text,wb.preview_links_text) && !g_strcmp0(wb.source_path,wb.preview_links_path) && wb.preview_filetype==wb.preview_links_filetype && wb.index_version==wb.preview_links_version) { g_free(text);return; }
 if(wb.preview_links_idle) { g_source_remove(wb.preview_links_idle); wb.preview_links_idle=0; }
 g_free(wb.preview_links_path);wb.preview_links_path=g_strdup(wb.source_path);wb.preview_links_filetype=wb.preview_filetype;wb.preview_links_version=wb.index_version;wb.preview_links_valid=TRUE;
 g_clear_pointer(&wb.preview_candidates,g_hash_table_destroy);wb.preview_candidates=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)g_ptr_array_unref);wb.preview_names_position=0;
 SSM(wb.preview,SCI_INDICSETSTYLE,INSIGHT_LINK,INDIC_PLAIN); SSM(wb.preview,SCI_INDICSETFORE,INSIGHT_LINK,0xcc5500);
 SSM(wb.preview,SCI_SETINDICATORCURRENT,INSIGHT_LINK,0); SSM(wb.preview,SCI_INDICATORCLEARRANGE,0,sci_get_length(wb.preview));
 preview_scopes_clear();gboolean calls=member_calls_language(wb.preview_filetype),python=python_language(wb.preview_filetype);
 if(!wb.preview_links_text || strcmp(wb.preview_links_text,text) || calls!=wb.preview_calls || python!=wb.preview_python) { g_clear_pointer(&wb.preview_code_mask,g_free);if(calls || python) wb.preview_code_mask=insight_source_code_mask(text,strlen(text),python ? INSIGHT_CODE_PYTHON : INSIGHT_CODE_C,NULL); }
 g_free(wb.preview_links_text);wb.preview_links_text=text;wb.preview_links_length=strlen(text);wb.preview_calls=calls;wb.preview_python=python; wb.preview_links_position=0;wb.preview_styled_position=0;
 wb.preview_links_idle=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,preview_links_step,NULL,NULL);
}
static gboolean preview_motion(GtkWidget *widget,GdkEventMotion *event,gpointer data) {
 (void)widget; (void)data; gint position=(gint)SSM(wb.preview,SCI_POSITIONFROMPOINTCLOSE,(uptr_t)event->x,(sptr_t)event->y);
 /* Embedded Scintilla Platform::Window::Cursor::hand is 8. */
 SSM(wb.preview,SCI_SETCURSOR,position>=0 && SSM(wb.preview,SCI_INDICATORVALUEAT,INSIGHT_LINK,position) ? 8 : SC_CURSORNORMAL,0); return FALSE;
}
static void preview_notify(ScintillaObject *sci,gint message,SCNotification *notification,gpointer data) {
 (void)message; (void)data;
 if(notification->nmhdr.code==SCN_UPDATEUI || notification->nmhdr.code==SCN_PAINTED) decorate_selection(sci);
}
static void stop_scan(void) {
 if(wb.scan || wb.scan_worker) wb.report_scan_completed=FALSE;
 stop_definition(); stop_file_preview();
 wb.scan_generation++; if(wb.cache_worker) { g_cancellable_cancel(wb.cache_worker); g_clear_object(&wb.cache_worker); } if(wb.scan_worker) { g_cancellable_cancel(wb.scan_worker); g_clear_object(&wb.scan_worker); if(wb.alive && !wb.base) wb.base=wb.scan_backup?insight_index_ref(wb.scan_backup):insight_index_new(); }
 retire_index(wb.scan_backup);wb.scan_backup=NULL;
 if(wb.scan) { g_source_remove(wb.scan); wb.scan=0; }
 if(wb.frames) g_ptr_array_set_size(wb.frames,0);
 if(wb.scan_files) g_ptr_array_set_size(wb.scan_files,0);
 if(wb.scan_trees) g_ptr_array_set_size(wb.scan_trees,0);
 if(wb.scan_seen) g_hash_table_remove_all(wb.scan_seen);
}
static gboolean source_dialect(GeanyFiletype *,InsightCodeDialect *);
static gchar *reference_snippet(const char *,gsize);
static gboolean rescan_changed(gpointer data) {
 (void)data; wb.watch_idle=0; workspace_changed(NULL); return G_SOURCE_REMOVE;
}
static void workspace_changed(gpointer data) {
 (void)data;
 if(!wb.root) return;
 if(wb.scan || wb.scan_worker) { wb.watch_changed=TRUE; return; }
 GPtrArray *paths=insight_workspace_watch_take_changes(wb.watch);gboolean membership=FALSE;
 for(guint n=0;n<paths->len;n++) { const gchar *native=g_ptr_array_index(paths,n);gchar *utf8=utils_get_utf8_from_locale(native);if(!g_hash_table_contains(wb.file_stamps,utf8)||!g_file_test(native,G_FILE_TEST_IS_REGULAR)) membership=TRUE;g_free(utf8); }
 g_ptr_array_unref(paths);if(!membership&&g_hash_table_size(wb.file_stamps)) wb.cache_validation=TRUE;
 if(!start_scan(wb.root)) ui_set_statusbar(TRUE,"Workspace changed but source folder is unavailable; previous index retained");
}
static gchar *project_policy(const gchar *native_root) {
 GKeyFile *key=g_key_file_new();gchar *root=native_root?utils_get_utf8_from_locale(native_root):NULL;
 if(wb.workspace_from_project&&wb.membership&&root) insight_project_files_write(wb.membership,key,root,NULL);
 gchar *data=g_key_file_to_data(key,NULL,NULL),*digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,data,-1);g_free(data);g_free(root);g_key_file_unref(key);return digest;
}
static gboolean database_identity(const gchar *root) {
 gchar *identity=wb.workspace_from_project&&app->project?g_strdup(app->project->file_name):g_strconcat("workspace:",root,NULL);
 gchar *policy=project_policy(root);gboolean changed=g_strcmp0(identity,wb.db_identity)!=0;
 if(g_strcmp0(identity,wb.db_identity)||g_strcmp0(policy,wb.db_policy)) {
  g_free(wb.cache_path);wb.cache_path=insight_project_db_path(app->configdir,identity);g_free(wb.db_identity);wb.db_identity=g_strdup(identity);g_free(wb.db_policy);wb.db_policy=g_strdup(policy);
 }
 g_free(identity);g_free(policy);return changed;
}
static gchar *source_stamp(const gchar *native,const gchar *language,gint encoding) {
 gchar *stamp=insight_project_db_stamp(native,FALSE);if(!stamp) return NULL;gchar *value=g_strdup_printf("%s:%d:%s",language?language:"",encoding,stamp);g_free(stamp);return value;
}
static void restore_project_database(const gchar *root) {
 InsightProjectDb db={0};GError *error=NULL;
 if(!insight_project_db_load(wb.cache_path,wb.db_identity,root,NULL,&db,&error)) { g_clear_error(&error);return; }
 retire_index(wb.base);wb.base=db.symbols;db.symbols=NULL;retire_index(wb.disk_calls);wb.disk_calls=db.calls;db.calls=NULL;retire_index(wb.disk_declarations);wb.disk_declarations=db.declarations;db.declarations=NULL;
 g_hash_table_unref(wb.file_stamps);wb.file_stamps=db.files;db.files=NULL;g_hash_table_unref(wb.directory_stamps);wb.directory_stamps=db.directories;db.directories=NULL;
 g_free(wb.root);wb.root=g_strdup(root);g_ptr_array_set_size(wb.files,0);GHashTableIter it;gpointer path;g_hash_table_iter_init(&it,wb.file_stamps);while(g_hash_table_iter_next(&it,&path,NULL)) g_ptr_array_add(wb.files,g_strdup(path));
 /* Rehydrate declaration rows without invoking a parser or opening source files. */
 g_hash_table_remove_all(wb.declarations);
 for(gsize n=0;n<insight_index_count(wb.disk_declarations);n++) {
  const InsightSymbol *row=insight_index_at(wb.disk_declarations,n);OutlineFile *o=g_hash_table_lookup(wb.declarations,row->path);
  if(!o) { GeanyFiletype *ft=filetypes_detect_from_extension(row->path);o=outline_capture(NULL,row->path);o->lang=ft?ft->lang:-1;g_hash_table_insert(wb.declarations,g_strdup(row->path),o); }
  if(!g_str_has_prefix(row->kind,"@decl:")) continue;gchar *end=NULL;guint type=g_ascii_strtoull(row->kind+6,&end,10);if(!end||*end!=':'||!end[1]) continue;
  TMTag *tag=tm_tag_new();tag->name=g_strdup(row->name);tag->type=type;tag->kind_letter=end[1];tag->line=row->line;tag->scope=g_strdup(row->scope);tag->arglist=g_strdup(row->signature);tag->var_type=g_strdup(row->type);tag->inheritance=g_strdup(row->inheritance);g_ptr_array_add(o->tags,tag);
 }
 wb.cache_validation=!g_strcmp0(db.policy,wb.db_policy);insight_project_db_clear(&db);
}
static void watch_directory(const char *path) {
 gchar *stamp=insight_project_db_stamp(path,TRUE);if(stamp) g_hash_table_replace(wb.directory_stamps,g_strdup(path),stamp);
 GError *error=NULL;
 if(!insight_workspace_watch_add(wb.watch,path,&error)) wb.watch_incomplete=TRUE;
 g_clear_error(&error);
}
typedef struct { InsightIndex *index;InsightProjectDb db; gchar *path,*root,*identity,*policy; guint64 epoch,generation; } CacheJob;
static GMutex cache_write_mutex;
static void cache_job_free(gpointer data) { CacheJob *j=data; retire_index(j->index);insight_project_db_clear(&j->db); g_free(j->path); g_free(j->root);g_free(j->identity);g_free(j->policy); g_free(j); }
static void cache_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; CacheJob *j=data; GError *error=NULL;
 /* A cancelled older writer cannot replace a newer project's cache. */
 g_mutex_lock(&cache_write_mutex);
 gboolean ok=!g_cancellable_set_error_if_cancelled(cancel,&error);
 if(ok) {
  gchar *directory=g_path_get_dirname(j->path);
  if(g_mkdir_with_parents(directory,0700)) { ok=FALSE; g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_FAILED,"Unable to create index cache directory"); }
  g_free(directory);
 }
 if(ok) ok=insight_project_db_save(j->path,j->identity,j->root,j->policy,&j->db,cancel,&error);
 g_mutex_unlock(&cache_write_mutex);
 if(error) g_task_return_error(task,error); else g_task_return_boolean(task,ok);
}
static void cache_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); CacheJob *j=g_task_get_task_data(task); GError *error=NULL; gboolean ok=g_task_propagate_boolean(task,&error);
 if(wb.alive && j->epoch==workbench_epoch && j->generation==wb.scan_generation) {
  if(wb.cache_worker==g_task_get_cancellable(task)) g_clear_object(&wb.cache_worker);
  if(ok) g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-saved",GINT_TO_POINTER(1));
  if(!ok && error && !g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED)) ui_set_statusbar(TRUE,"Workspace cache not saved: %s",error->message);
 }
 g_clear_error(&error);
}
static void save_index_cache(void) {
 if(!wb.alive || !wb.base || !wb.root || !wb.cache_path) return;
 if(wb.cache_worker) { g_cancellable_cancel(wb.cache_worker); g_clear_object(&wb.cache_worker); }
 CacheJob *j=g_new0(CacheJob,1); j->index=NULL;j->db.symbols=insight_index_ref(wb.base);j->db.calls=insight_index_ref(wb.disk_calls);j->db.declarations=insight_index_ref(wb.disk_declarations);j->db.files=insight_project_db_copy_table(wb.file_stamps);j->db.directories=insight_project_db_copy_table(wb.directory_stamps);j->identity=g_strdup(wb.db_identity);j->policy=g_strdup(wb.db_policy); j->path=g_strdup(wb.cache_path); j->root=g_strdup(wb.root); j->epoch=workbench_epoch; j->generation=wb.scan_generation;
 wb.cache_worker=g_cancellable_new(); GTask *task=g_task_new(NULL,wb.cache_worker,cache_job_done,NULL); g_task_set_task_data(task,j,cache_job_free); g_task_run_in_thread(task,cache_job_run); g_object_unref(task);
}
static char *read_disk_source_full(const char *,gboolean,gint,GCancellable *);
static gboolean scan_step(gpointer);
typedef struct { InsightIndex *index,*calls,*outline; OutlineFile *declarations;gchar *stamp,*path,*utf8,*language; gint lang,encoding; guint64 generation,epoch; gboolean read,parsed,failed,skipped,regular; } IndexJob;
static void index_job_free(gpointer data) { IndexJob *j=data;outline_free(j->declarations); retire_index(j->index);retire_index(j->calls);retire_index(j->outline);g_free(j->stamp); g_free(j->path); g_free(j->utf8); g_free(j->language); g_free(j); }
static void index_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; IndexJob *j=data; GStatBuf st;
 if(g_cancellable_is_cancelled(cancel)) { g_task_return_boolean(task,FALSE); return; }
 if(!insight_index_exclusive(j->index)) { InsightIndex *copy=insight_index_clone(j->index); if(!copy) { j->failed=TRUE; g_task_return_boolean(task,TRUE); return; } insight_index_free(j->index); j->index=copy; }
 if(!insight_index_exclusive(j->calls)) { InsightIndex *copy=insight_index_clone(j->calls);insight_index_free(j->calls);j->calls=copy; }
 if(!insight_index_exclusive(j->outline)) { InsightIndex *copy=insight_index_clone(j->outline);insight_index_free(j->outline);j->outline=copy; }
 if(!j->calls||!j->outline) { j->failed=TRUE;g_task_return_boolean(task,TRUE);return; }
 insight_index_update(j->index,j->utf8,NULL,0);insight_index_update(j->calls,j->utf8,NULL,0);insight_index_update(j->outline,j->utf8,NULL,0);
 j->regular=!g_lstat(j->path,&st) && S_ISREG(st.st_mode);
 if(!j->regular || st.st_size>2*1024*1024) j->skipped=TRUE;
 else if(j->read) {
  gchar *text=read_disk_source_full(j->utf8,TRUE,j->encoding,cancel);
  if(!text) j->failed=TRUE;
  else {
   j->parsed=TRUE;
   if(j->lang>=0 && !g_cancellable_is_cancelled(cancel)) {
    TMSourceFile *file=tm_source_file_new(j->path,j->language);
    if(file) { file->lang=j->lang; if(!insight_native_parse(j->index,file,j->utf8,text,strlen(text))) j->failed=TRUE;else { j->declarations=outline_capture(file,j->utf8);if(!insight_native_outline_import(j->outline,file,j->utf8)) j->failed=TRUE; } tm_source_file_free(file); }
    else j->failed=TRUE;
   }
   InsightCodeDialect dialect;
   GeanyFiletype *ft=filetypes_lookup_by_name(j->language?j->language:"");
   if(!j->failed&&source_dialect(ft,&dialect)) {
    gchar *mask=insight_source_code_mask(text,strlen(text),dialect,NULL),*digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);GArray *rows=g_array_new(FALSE,FALSE,sizeof(InsightSymbol));GPtrArray *owned=g_ptr_array_new_with_free_func(g_free);size_t cursor=0,a=0,b=0,line=1,line_at=0;
    while(mask&&rows->len<32000&&insight_next_call(mask,strlen(text),&cursor,&a,&b)==1) {
     while(line_at<a) { if(text[line_at]=='\n') line++;else if(text[line_at]=='\r'&&(line_at+1>=a||text[line_at+1]!='\n')) line++;line_at++; }
     size_t start=a;while(start&&text[start-1]!='\n'&&text[start-1]!='\r') start--;
     gchar *name=g_strndup(text+a,b-a),*positions=g_strdup_printf("%zu:%zu:%lu",a,b,(gulong)g_utf8_strlen(text+start,a-start)+1),*snippet=reference_snippet(text,a);g_ptr_array_add(owned,name);g_ptr_array_add(owned,positions);g_ptr_array_add(owned,snippet);InsightSymbol row={name,j->utf8,"","@call",line,positions,snippet,digest};g_array_append_val(rows,row);
    }
    InsightSymbol coverage={"@file",j->utf8,"","@file",1,"","",digest};g_array_append_val(rows,coverage);
    if(!mask||rows->len>=32001||!insight_index_update(j->calls,j->utf8,(InsightSymbol *)rows->data,rows->len)) j->failed=TRUE;
    g_ptr_array_unref(owned);g_array_unref(rows);g_free(mask);g_free(digest);
   }
   gchar *after=source_stamp(j->path,j->language,j->encoding);if(g_strcmp0(after,j->stamp)) j->failed=TRUE;g_free(after);g_free(text);
  }
 }
 g_task_return_boolean(task,TRUE);
}
static void index_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); IndexJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.scan_generation) return;
 g_clear_object(&wb.scan_worker);
 if(j->failed||!ok) { retire_index(wb.base);wb.base=wb.scan_backup?insight_index_ref(wb.scan_backup):insight_index_new(); }
 else { retire_index(wb.base);wb.base=j->index;j->index=NULL;retire_index(wb.disk_calls);wb.disk_calls=j->calls;j->calls=NULL;retire_index(wb.disk_declarations);wb.disk_declarations=j->outline;j->outline=NULL;wb.index_dirty=TRUE; }
 retire_index(wb.scan_backup);wb.scan_backup=NULL;
 if(!j->failed&&j->stamp) g_hash_table_replace(wb.file_stamps,g_strdup(j->utf8),g_strdup(j->stamp));
 if(j->declarations && !j->failed) { g_hash_table_replace(wb.declarations,g_strdup(j->utf8),j->declarations);j->declarations=NULL; }
 if(!j->parsed&&!j->failed) g_hash_table_remove(wb.declarations,j->utf8);
 if(j->parsed) wb.parsed++;
 if(j->regular) {
  g_ptr_array_add(wb.scan_members,g_strdup(j->utf8));
  if(!wb.scan_keep_files) g_ptr_array_add(wb.files,g_strdup(j->utf8));
 }
 if(j->failed || j->skipped || !ok) wb.skipped++;
 if(j->failed) wb.scan_failed=TRUE;
 if(wb.scan_members->len && wb.scan_members->len%25==0) { if(!wb.scan_keep_files) files_filter(NULL,NULL); ui_set_statusbar(TRUE,"Workspace: indexing… %u files",wb.parsed); }
 if(g_get_monotonic_time()>=wb.publish_due) { wb.publish_due=g_get_monotonic_time()+1000000; refresh_index(); }
 wb.scan=g_idle_add(scan_step,NULL);
}
static gboolean scan_continue(void) { if(wb.scan_worker) { wb.scan=0; return G_SOURCE_REMOVE; } return G_SOURCE_CONTINUE; }
static void scan_source_file(const char *path) {
 if(g_hash_table_contains(wb.scan_seen,path)) return;
 g_hash_table_add(wb.scan_seen,g_strdup(path));
 gchar *utf8=utils_get_utf8_from_locale(path),*root=utils_get_utf8_from_locale(wb.root);
 if(wb.workspace_from_project && !insight_project_files_contains(wb.membership,root,utf8)) { g_free(root); g_free(utf8); return; }
 g_free(root); GeanyFiletype *ft=filetypes_detect_from_extension(utf8);
 /* A cached shebang language avoids reopening extensionless source on every
  * validation. Detect content only for a genuinely new/changed file. */
 const gchar *stored=g_hash_table_lookup(wb.file_stamps,utf8);
 if(ft&&ft->id==GEANY_FILETYPES_NONE&&stored) { const gchar *colon=strchr(stored,':');if(colon) { gchar *language=g_strndup(stored,colon-stored);GeanyFiletype *cached=filetypes_lookup_by_name(language);if(cached) ft=cached;g_free(language); } }
 gchar *stamp=source_stamp(path,ft?ft->name:NULL,file_prefs.default_open_encoding);
 if(!wb.force_parse&&stamp&&!g_strcmp0(stamp,g_hash_table_lookup(wb.file_stamps,utf8))) { g_ptr_array_add(wb.scan_members,g_strdup(utf8));wb.reused++;g_free(stamp);g_free(utf8);return; }
 ft=filetypes_detect_from_file(utf8);g_free(stamp);stamp=source_stamp(path,ft?ft->name:NULL,file_prefs.default_open_encoding);
 IndexJob *j=g_new0(IndexJob,1);wb.scan_backup=insight_index_ref(wb.base); j->index=wb.base; wb.base=NULL;j->calls=insight_index_ref(wb.disk_calls);j->outline=insight_index_ref(wb.disk_declarations);j->stamp=stamp; j->path=g_strdup(path); j->utf8=utf8; j->language=g_strdup(ft ? ft->name : NULL); j->lang=ft ? ft->lang : -1; j->read=ft && ft->id!=GEANY_FILETYPES_NONE; j->encoding=file_prefs.default_open_encoding; j->generation=wb.scan_generation; j->epoch=workbench_epoch;
 wb.scan_worker=g_cancellable_new(); GTask *task=g_task_new(NULL,wb.scan_worker,index_job_done,NULL); g_task_set_task_data(task,j,index_job_free); g_task_run_in_thread(task,index_job_run); g_object_unref(task);
}
static gboolean scan_step(gpointer data) {
 (void)data;
 if((!wb.frames->len && wb.scan_file_position>=wb.scan_files->len && wb.scan_tree_position>=wb.scan_trees->len) || wb.scan_members->len>=10000) {
  gboolean limited=wb.scan_members->len>=10000;
  wb.scan=0; wb.relation_dirty=TRUE;wb.report_scan_completed=!wb.scan_failed;
  /* Project membership is separate from the symbol index. Publish a complete
   * replacement only after enumeration; do not narrow the visible list while
   * a same-root rescan is still in progress. */
  if(!wb.scan_failed) {
   gboolean detached=FALSE;
   GHashTable *live=g_hash_table_new(g_str_hash,g_str_equal);for(guint n=0;n<wb.scan_members->len;n++) g_hash_table_add(live,g_ptr_array_index(wb.scan_members,n));
   GHashTableIter it;gpointer path;g_hash_table_iter_init(&it,wb.file_stamps);while(g_hash_table_iter_next(&it,&path,NULL)) if(!g_hash_table_contains(live,path)) {
    if(!detached) { InsightIndex *copy=insight_index_clone(wb.base);retire_index(wb.base);wb.base=copy;copy=insight_index_clone(wb.disk_calls);retire_index(wb.disk_calls);wb.disk_calls=copy;copy=insight_index_clone(wb.disk_declarations);retire_index(wb.disk_declarations);wb.disk_declarations=copy;detached=TRUE; }
    insight_index_update(wb.base,path,NULL,0);insight_index_update(wb.disk_calls,path,NULL,0);insight_index_update(wb.disk_declarations,path,NULL,0);g_hash_table_remove(wb.declarations,path);g_hash_table_iter_remove(&it); }g_hash_table_unref(live);
  }
  wb.force_parse=FALSE;
  if(!wb.scan_failed) { GPtrArray *old=wb.files; wb.files=wb.scan_members; wb.scan_members=old; g_ptr_array_set_size(wb.scan_members,0); }
  g_ptr_array_set_size(wb.frames,0); g_ptr_array_set_size(wb.scan_files,0); g_ptr_array_set_size(wb.scan_trees,0); g_hash_table_remove_all(wb.scan_seen); refresh_index();
  g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-parsed",GUINT_TO_POINTER(wb.parsed+1));g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-reused",GUINT_TO_POINTER(wb.reused+1));g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-members",GUINT_TO_POINTER(wb.files->len+1));g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-scanning",NULL);
  if(!limited && !wb.scan_failed) save_index_cache();
  ui_set_statusbar(TRUE,"Workspace: %u parsed, %u reused, %zu symbols, %u skipped%s",wb.parsed,wb.reused,insight_index_count(wb.index),wb.skipped,limited ? "; file limit reached" : (wb.watch_incomplete ? "; directory monitoring incomplete" : ""));
  browser_filter(NULL,NULL); files_filter(NULL,NULL); tree_refresh(); file_types_refresh(); categories_filter(NULL,NULL); local_filter(NULL,NULL); insight_workbench_refresh();
  if(wb.watch_changed && !wb.watch_idle) { wb.watch_changed=FALSE; wb.watch_idle=g_timeout_add(2000,rescan_changed,NULL); }
  return G_SOURCE_REMOVE;
 }
 if(wb.scan_file_position<wb.scan_files->len) { scan_source_file(g_ptr_array_index(wb.scan_files,wb.scan_file_position++)); return scan_continue(); }
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
  gchar *profile=insight_canonicalize_filename(app->configdir,NULL); gboolean own_profile=!g_strcmp0(path,profile); g_free(profile);
  if(own_profile || g_hash_table_contains(wb.scan_seen,path)) { g_free(path); return G_SOURCE_CONTINUE; }
  GDir *dir=wb.frames->len<64 ? g_dir_open(path,0,NULL) : NULL;
  if(dir) { g_hash_table_add(wb.scan_seen,g_strdup(path)); watch_directory(path); ScanFrame *next=g_new0(ScanFrame,1); next->path=path; next->dir=dir; g_ptr_array_add(wb.frames,next); }
  else { wb.scan_failed=TRUE; wb.skipped++; g_free(path); }
  return G_SOURCE_CONTINUE;
 }
 scan_source_file(path); g_free(path); return scan_continue();
}
static gboolean start_scan(const char *root) {
 gchar *requested_root=g_strdup(root);stop_scan();root=requested_root;
 if(wb.workspace_from_project && insight_git_active()) {
  stop_scan();GError *error=NULL;InsightProjectFiles *policy=insight_git_membership(&error);
  if(!policy) { g_free(requested_root);ui_set_statusbar(TRUE,"Cannot read Git project files: %s",error ? error->message : "Git is busy");g_clear_error(&error);return FALSE; }
  insight_project_files_free(wb.membership);wb.membership=policy;
 }
 gchar *owned=insight_canonicalize_filename(root,NULL);g_free(requested_root); GDir *dir=g_dir_open(owned,0,NULL); InsightIndex *next=NULL;
 gboolean context_changed=database_identity(owned)||g_strcmp0(wb.root,owned)!=0;
 if(context_changed) { g_hash_table_remove_all(wb.file_stamps);g_hash_table_remove_all(wb.directory_stamps);g_hash_table_remove_all(wb.declarations);retire_index(wb.base);wb.base=insight_index_new();retire_index(wb.disk_calls);wb.disk_calls=insight_index_new();retire_index(wb.disk_declarations);wb.disk_declarations=insight_index_new(); }
 if(!wb.force_parse&&(context_changed||!g_hash_table_size(wb.file_stamps))) restore_project_database(owned);
 next=wb.base?insight_index_ref(wb.base):insight_index_new();
 if(!dir || !next) { if(dir) g_dir_close(dir); insight_index_free(next); g_free(owned); return FALSE; }
 gboolean same_root=!g_strcmp0(wb.root,owned)&&(!context_changed||g_hash_table_size(wb.file_stamps)>0);
 invalidate_search_context(); stop_scan(); wb.scan_keep_files=same_root; g_ptr_array_set_size(wb.scan_members,0);
 if(wb.watch_idle) { g_source_remove(wb.watch_idle); wb.watch_idle=0; }
 insight_workspace_watch_clear(wb.watch); wb.watch_changed=FALSE; wb.watch_incomplete=FALSE; if(!wb.cache_validation) watch_directory(owned);
 retire_index(wb.base); wb.base=next; g_free(wb.root); wb.root=g_strdup(owned);
 if(!same_root) g_ptr_array_set_size(wb.files,0);
 ScanFrame *frame=g_new0(ScanFrame,1); frame->path=owned; frame->dir=dir;
 gboolean known_only=wb.cache_validation;wb.cache_validation=FALSE;
 if(known_only) { GHashTableIter it;gpointer path,value;g_hash_table_iter_init(&it,wb.directory_stamps);while(g_hash_table_iter_next(&it,&path,&value)) { gchar *now=insight_project_db_stamp(path,TRUE);if(g_strcmp0(now,value)) known_only=FALSE;g_free(now); } }
 if(!known_only) { g_hash_table_remove_all(wb.directory_stamps);watch_directory(owned); }
 if(known_only) {
  GHashTableIter it;gpointer path;g_hash_table_iter_init(&it,wb.file_stamps);while(g_hash_table_iter_next(&it,&path,NULL)) { gchar *native=utils_get_locale_from_utf8(path);if(g_file_test(native,G_FILE_TEST_IS_REGULAR)) g_ptr_array_add(wb.scan_files,native);else g_free(native); }
  g_hash_table_iter_init(&it,wb.directory_stamps);while(g_hash_table_iter_next(&it,&path,NULL)) { GError *error=NULL;insight_workspace_watch_add(wb.watch,path,&error);g_clear_error(&error); }
  frame_free(frame);
 }else if(!wb.workspace_from_project || wb.membership->automatic) { g_ptr_array_add(wb.frames,frame); g_hash_table_add(wb.scan_seen,g_strdup(owned)); } else frame_free(frame);
 if(wb.workspace_from_project&&!known_only) {
  for(guint i=0;i<wb.membership->files->len;i++) { const char *path=g_ptr_array_index(wb.membership->files,i); gchar *native=utils_get_locale_from_utf8(path),*parent=g_path_get_dirname(native); g_ptr_array_add(wb.scan_files,native); watch_directory(parent); g_free(parent); }
  for(guint i=0;i<wb.membership->trees->len;i++) g_ptr_array_add(wb.scan_trees,utils_get_locale_from_utf8(g_ptr_array_index(wb.membership->trees,i)));
 }
 g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-saved",NULL);g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-scanning",GINT_TO_POINTER(1));
 wb.scan_file_position=wb.scan_tree_position=0; wb.parsed=wb.skipped=wb.reused=0;wb.report_scan_completed=FALSE; mark_index_dirty(); wb.scan_failed=FALSE; wb.scan=g_idle_add(scan_step,NULL); ui_set_statusbar(TRUE,wb.force_parse?"Rebuilding project symbols…":known_only?"Project database restored; checking file stamps…":"Synchronizing project membership and changed files…"); return TRUE;
}
static void choose_root(GtkButton *button,gpointer data) {
 (void)button; (void)data;
 GtkWidget *dialog=gtk_file_chooser_dialog_new("Open source workspace",GTK_WINDOW(main_widgets.window),GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)); gboolean previous=wb.workspace_from_project; wb.workspace_from_project=FALSE; if(!start_scan(path)) { wb.workspace_from_project=previous; ui_set_statusbar(TRUE,"Cannot index source folder"); } g_free(path); }
 gtk_widget_destroy(dialog);
}
static void cancel_scan(GtkButton *button,gpointer data) { (void)button; (void)data;insight_master_sync_pause(wb.master_sync); stop_scan(); insight_workspace_watch_clear(wb.watch); wb.watch_changed=FALSE; if(wb.watch_idle) { g_source_remove(wb.watch_idle); wb.watch_idle=0; } refresh_index(); browser_filter(NULL,NULL); files_filter(NULL,NULL); tree_refresh(); file_types_refresh(); categories_filter(NULL,NULL); local_filter(NULL,NULL); insight_workbench_refresh(); ui_set_statusbar(TRUE,"Workspace scan cancelled; partial index retained"); }
static const InsightSymbol *word_definition(GeanyDocument *doc,const char *word,const char *preferred,gulong reference_line,ScintillaObject *reference_sci,gint reference_position) {
 if(!wb.definitions) return NULL;
 if(!preferred && doc) preferred=doc->file_name;
 refresh_index();
 GeanyFiletype *reference_type=reference_sci==wb.preview ? wb.preview_filetype : (doc ? doc->file_type : NULL);
 gboolean valid_word=word && *word && !definition_keyword(reference_type,word) && !insight_languages_keyword(reference_sci,word) && (!reference_sci || (sci_get_length(reference_sci)<=2*1024*1024 && definition_code(reference_sci,reference_position)));
 wb.populating=TRUE; g_ptr_array_set_size(wb.definitions,0); gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(wb.choices));
 const InsightSymbol *found[256],*chosen=NULL;gboolean imported=FALSE;gsize import_count=0;
 gchar *scope=NULL,*mask=NULL;gboolean uncertain_scope=FALSE;
 gboolean cached=reference_sci==wb.preview && wb.preview_links_text && reference_position>=0 && (gsize)reference_position<=wb.preview_links_length;
 gboolean python=python_language(reference_type);gboolean calls=member_calls_language(reference_type) && reference_sci && sci_get_length(reference_sci)<=2*1024*1024;gint reference_begin=(calls || python) ? 0 : MAX(0,reference_position-4096);
 gchar *reference=!cached && reference_sci ? ((calls || python) ? sci_get_contents(reference_sci,sci_get_length(reference_sci)+1) : sci_get_contents_range(reference_sci,reference_begin,reference_position)) : NULL;gboolean member=FALSE;
 gsize reference_length=reference ? ((calls || python) ? (gsize)sci_get_length(reference_sci) : (gsize)(reference_position-reference_begin)) : 0;
 if(reference && (!g_utf8_validate(reference,reference_length,NULL) || memchr(reference,0,reference_length))) valid_word=FALSE;
 ScopeLanguage language={preferred,reference_type};gchar *owner=NULL;
 if(python) {
  const gchar *bytes=cached ? wb.preview_links_text : reference;gsize length=cached ? wb.preview_links_length : reference_length;
  mask=!cached && bytes ? insight_source_code_mask(bytes,length,INSIGHT_CODE_PYTHON,NULL) : NULL;InsightNativeSource text={bytes,mask,length,NULL};
  if(cached) text=preview_python_source();else text.python=insight_native_python_bindings_new(wb.index,preferred,&text);
  scope=insight_native_python_scope(wb.index,preferred,&text,reference_position);
  import_count=insight_native_python_import_query(wb.index,preferred,scope,&text,reference_position,word,scope_language_eligible,&language,found,G_N_ELEMENTS(found),&imported);
  if(!imported) owner=insight_native_python_owner(wb.index,preferred,scope,reference_line,&text,reference_position,&member,scope_language_eligible,&language);
  if(!member && !imported) { gchar *binding=insight_native_python_binding_scope(wb.index,preferred,scope,word,&text);g_free(scope);scope=binding;if(!scope) valid_word=FALSE; }
  if(!cached) insight_native_python_bindings_free(text.python);
 } else if(cached) {
  scope=preview_function_scope(reference_line,reference_position,&uncertain_scope);InsightNativeSource text={wb.preview_links_text,wb.preview_code_mask,wb.preview_links_length,NULL};
  if(!uncertain_scope) owner=insight_native_member_owner_source_filtered(wb.index,preferred,scope,reference_line,&text,reference_position,&member,calls && wb.preview_code_mask!=NULL,scope_language_eligible,&language);
 } else if(calls && reference && valid_word) {
  mask=insight_source_code_mask(reference,reference_length,INSIGHT_CODE_C,NULL);InsightNativeSource text={reference,mask,reference_length,NULL};InsightNativeScopes *scopes=insight_native_scopes_new(wb.index,preferred,&text);scope=insight_native_scopes_at(scopes,reference_line,(gsize)reference_position,&uncertain_scope);
  if(!uncertain_scope) { owner=insight_native_member_owner_source_filtered(wb.index,preferred,scope,reference_line,&text,reference_position,&member,mask!=NULL,scope_language_eligible,&language); }insight_native_scopes_free(scopes);
 } else {
  scope=insight_native_function_scope(wb.index,preferred,reference_line);owner=insight_native_member_owner_calls_filtered(wb.index,preferred,scope,reference_line,reference,reference_position-reference_begin,&member,calls,scope_language_eligible,&language);
 }
 if(uncertain_scope) { valid_word=FALSE; }g_free(mask);g_free(reference);
 size_t count=valid_word ? (imported ? import_count : member ? insight_member_query(wb.index,owner,word,preferred,scope_language_eligible,&language,found,256,NULL,NULL) : insight_index_find(wb.index,word,TRUE,found,256)) : 0; gint selected=0;
 for(size_t i=0;i<count && i<256;i++) {
  const InsightSymbol *tag=found[i]; if(!definition_language(preferred,reference_type,tag->path)) continue; if((python && !member && !imported ? insight_native_python_rank(wb.index,tag,preferred,scope,reference_line) : insight_native_definition_rank_at(tag,preferred,scope,reference_line))<0) continue; gboolean duplicate=FALSE;
  for(guint j=0;j<wb.definitions->len;j++) { Location *old=g_ptr_array_index(wb.definitions,j); if(old->line==tag->line && !g_strcmp0(old->path,tag->path)) { duplicate=TRUE; break; } }
  if(duplicate) continue;
  Location *location=g_new0(Location,1); location->path=g_strdup(tag->path); location->line=tag->line;
  gchar *label=g_strdup_printf("%s %s%s • %s — %s:%lu",tag->type,tag->name,tag->signature,tag->scope,tag->path,tag->line);
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.choices),label); g_free(label);
  if(!chosen || (python && !member && !imported ? insight_native_python_rank(wb.index,tag,preferred,scope,reference_line) : insight_native_definition_rank_at(tag,preferred,scope,reference_line))>(python && !member && !imported ? insight_native_python_rank(wb.index,chosen,preferred,scope,reference_line) : insight_native_definition_rank_at(chosen,preferred,scope,reference_line))) { chosen=tag; selected=(gint)wb.definitions->len; }
  g_ptr_array_add(wb.definitions,location);
 }
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.choices),wb.definitions->len ? selected : -1);
 wb.populating=FALSE; g_free(scope); g_free(owner); return chosen;
}
static char *read_disk_source_full(const char *utf8,gboolean decode,gint preferred,GCancellable *cancel) {
 gchar *native=utils_get_locale_from_utf8(utf8); GFile *file=g_file_new_for_path(native); g_free(native);
 GFileInputStream *stream=g_file_read(file,cancel,NULL); g_object_unref(file);
 if(!stream) return NULL;
 GString *text=g_string_sized_new(16384); char chunk[16384]; gboolean ok=TRUE;
 for(;;) {
  gssize got=g_input_stream_read(G_INPUT_STREAM(stream),chunk,sizeof chunk,cancel,NULL);
  if(got<0 || (got>0 && text->len+(gsize)got>2u*1024u*1024u)) { ok=FALSE; break; }
  if(!got) break;
  g_string_append_len(text,chunk,got);
 }
 g_object_unref(stream);
 if(!ok) { g_string_free(text,TRUE); return NULL; }
 gsize length=text->len; gchar *buffer=g_string_free(text,FALSE);
 if(decode) {
  gboolean nul=FALSE;
  if(!(preferred==G_MININT ? encodings_convert_to_utf8_auto(&buffer,&length,NULL,NULL,NULL,&nul,NULL) : encodings_convert_to_utf8_worker(&buffer,&length,preferred,&nul)) || nul) { g_free(buffer); return NULL; }
 }
 if(length>2u*1024u*1024u || !g_utf8_validate(buffer,(gssize)length,NULL) || memchr(buffer,0,length)) { g_free(buffer); return NULL; }
 return buffer;
}
static char *read_disk_source(const char *utf8,gboolean decode) { return read_disk_source_full(utf8,decode,G_MININT,NULL); }
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
static void reveal_context(void) {
 if(!wb.box || !wb.context_visible) return;
 if(gtk_widget_get_parent(wb.box)==main_widgets.message_window_notebook) {
  GtkWidget *focus=gtk_window_get_focus(GTK_WINDOW(main_widgets.window));if(focus) g_object_ref(focus);
  if(!gtk_widget_get_visible(main_widgets.message_window_notebook)) msgwin_show_hide(TRUE);
  gint page=gtk_notebook_page_num(GTK_NOTEBOOK(main_widgets.message_window_notebook),wb.box);
  if(gtk_notebook_get_current_page(GTK_NOTEBOOK(main_widgets.message_window_notebook))!=page) gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),page);
  if(focus) { if(gtk_window_get_focus(GTK_WINDOW(main_widgets.window))!=focus && gtk_widget_get_mapped(focus) && gtk_widget_get_sensitive(focus)) gtk_widget_grab_focus(focus);g_object_unref(focus); }
 }
}
typedef struct { gchar *path,*text,*digest; guint id; gint encoding,length; guint64 epoch,generation,text_version; } FilePreviewJob;
static void request_file_preview(const char *);
static void stop_file_preview(void) { wb.preview_generation++; if(wb.preview_worker) { g_cancellable_cancel(wb.preview_worker); g_clear_object(&wb.preview_worker); } }
static void file_preview_free(gpointer data) { FilePreviewJob *j=data; g_free(j->path); g_free(j->text); g_free(j->digest); g_free(j); }
static void file_preview_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; FilePreviewJob *j=data;
 if(!j->id && !g_cancellable_is_cancelled(cancel)) j->text=read_disk_source_full(j->path,TRUE,j->encoding,cancel);
 if(j->text && !g_cancellable_is_cancelled(cancel) && g_utf8_validate(j->text,-1,NULL) && (!j->id || !memchr(j->text,0,(gsize)j->length))) j->digest=source_digest(j->text);
 g_task_return_boolean(task,TRUE);
}
static void file_preview_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); FilePreviewJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.preview_generation) return;
 g_clear_object(&wb.preview_worker); if(!ok) return;
 GeanyDocument *doc=document_find_by_filename(j->path);
 if((doc && (doc->id!=j->id || selection_state(doc->editor->sci)->text_version!=j->text_version)) || (!doc && j->id)) { request_file_preview(j->path); return; }
 if(!j->digest) { render_source("",NULL,1,NULL); gtk_label_set_text(GTK_LABEL(wb.title),"Context unavailable: requires a decodable text source up to 2 MiB"); g_clear_pointer(&wb.source_path,g_free); wb.source_id=0; wb.source_line=0; preview_links();navigation_state(); return; }
 if(wb.source_line) remember_location(wb.source_path,wb.source_id,(gulong)sci_get_current_line(wb.preview)+1);
 render_source(j->text,doc ? doc->file_type : preview_filetype(j->path),1,j->path);
 g_free(wb.source_digest); wb.source_digest=g_strdup(j->digest); g_free(wb.source_path); wb.source_path=g_strdup(j->path); wb.source_id=doc ? doc->id : 0; wb.source_line=1;preview_links();
 gchar *label=g_strdup_printf("Context — %s:1",j->path); gtk_label_set_text(GTK_LABEL(wb.title),label); g_free(label); remember_location(wb.source_path,wb.source_id,1); navigation_state();
}
static void request_file_preview(const char *path) {
 stop_file_preview(); FilePreviewJob *j=g_new0(FilePreviewJob,1); j->path=g_strdup(path); j->encoding=file_prefs.default_open_encoding; j->epoch=workbench_epoch; j->generation=wb.preview_generation;
 GeanyDocument *doc=document_find_by_filename(path);
 if(doc) { j->id=doc->id; j->text_version=selection_state(doc->editor->sci)->text_version; j->length=sci_get_length(doc->editor->sci); if(j->length>=0 && j->length<=2*1024*1024) j->text=sci_get_contents(doc->editor->sci,j->length+1); }
 wb.preview_worker=g_cancellable_new(); GTask *task=g_task_new(NULL,wb.preview_worker,file_preview_done,NULL); g_task_set_task_data(task,j,file_preview_free); g_task_run_in_thread(task,file_preview_run); g_object_unref(task);
}
static gboolean show_location(const char *path,guint id,gulong line,gboolean remember) {
 stop_file_preview();
 if(wb.context_toggle && !wb.context_visible) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.context_toggle),TRUE);
 GeanyDocument *doc=id ? document_find_by_id(id) : (path ? document_find_by_filename(path) : NULL);
 char *text=NULL; GeanyFiletype *ft=NULL;
 if(doc) {
  gint length=sci_get_length(doc->editor->sci);
  if(length>=0 && length<=2*1024*1024) text=sci_get_contents(doc->editor->sci,length+1);
  if(text && (!g_utf8_validate(text,length,NULL) || memchr(text,0,(gsize)length))) g_clear_pointer(&text,g_free);
  ft=doc->file_type;
 } else if(path) { text=read_disk_source(path,TRUE); ft=preview_filetype(path); }
 if(!text) {
  render_source("",NULL,1,NULL); gtk_label_set_text(GTK_LABEL(wb.title),"Context unavailable: requires a decodable text source up to 2 MiB");
  g_clear_pointer(&wb.source_path,g_free); wb.source_id=0; wb.source_line=0; preview_links();navigation_state(); return FALSE;
 }
 gchar *owned_path=g_strdup(path ? path : (doc ? doc->file_name : NULL));
 size_t begin,end,target_begin,target_end; unsigned long first;
 gboolean valid=insight_context_range(text,strlen(text),line,0,&begin,&end,&first,&target_begin,&target_end);
 if(remember && valid && wb.source_line) remember_location(wb.source_path,wb.source_id,(gulong)sci_get_current_line(wb.preview)+1);
 render_source(text,ft,valid ? line : 1,path ? path : (doc ? doc->file_name : NULL));
 g_free(wb.source_digest); wb.source_digest=source_digest(text); g_free(text);
 if(!valid) line=0;
 g_free(wb.source_path); wb.source_path=owned_path; wb.source_id=doc ? doc->id : 0; wb.source_line=line;preview_links();
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
typedef struct { gchar *name,*path,*directory; guint64 size; gboolean stat_valid; } FileListRow;
static void file_list_row_free(gpointer data) { FileListRow *r=data; g_free(r->name); g_free(r->path); g_free(r->directory); g_free(r); }
typedef struct { guint64 generation; GCancellable *cancel; guint apply; InsightIndex *index; const InsightSymbol **rows; GPtrArray *files; gchar *local_path; guint count,position; gboolean local,collecting,iter_valid,has_more; GtkTreeIter iter; GHashTable *existing; } SymbolListState;
typedef struct { GWeakRef store; InsightIndex *index; gchar *query,*path; const InsightSymbol **rows; GPtrArray *paths,*files,*tags,*outlines;gint lang; guint count,limit; guint64 generation,epoch; gboolean local,has_more;InsightLanguageFilter *language; } SymbolListJob;
static GtkWidget *symbol_list_view(GtkListStore *store) { return store==wb.file_store ? wb.file_view : (store==wb.local_symbols ? wb.local_view : wb.symbol_view); }
static void symbol_iter_queue_free(gpointer data) { GQueue *q=data; g_queue_free_full(q,g_free); }
static gchar *symbol_row_key(GtkTreeModel *model,GtkTreeIter *iter,gboolean files) {
 gchar *name=NULL,*path=NULL,*scope=NULL,*kind=NULL; gulong line=0;
 if(files) gtk_tree_model_get(model,iter,0,&name,1,&path,-1);
 else gtk_tree_model_get(model,iter,SYMBOL_NAME,&name,SYMBOL_SCOPE,&scope,SYMBOL_PATH,&path,SYMBOL_LINE,&line,SYMBOL_KIND,&kind,-1);
 /* Length-prefixed fields avoid collisions for paths containing separators. */
 GString *key=g_string_new(NULL); const gchar *fields[]={name,path,scope,kind};
 for(guint i=0;i<G_N_ELEMENTS(fields);i++) { const gchar *v=fields[i] ? fields[i] : ""; g_string_append_printf(key,"%" G_GSIZE_FORMAT ":%s",strlen(v),v); }
 g_string_append_printf(key,":%lu",line); g_free(name); g_free(path); g_free(scope); g_free(kind); return g_string_free(key,FALSE);
}
static void symbol_list_stop(SymbolListState *s) {
 s->generation++; if(s->cancel) { g_cancellable_cancel(s->cancel); g_clear_object(&s->cancel); }
 if(s->apply) { g_source_remove(s->apply); s->apply=0; }
 retire_index(s->index); s->index=NULL; g_clear_pointer(&s->rows,g_free); g_clear_pointer(&s->files,g_ptr_array_unref); g_clear_pointer(&s->local_path,g_free); s->count=s->position=0; g_clear_pointer(&s->existing,g_hash_table_destroy); s->collecting=s->iter_valid=FALSE;
}
static void symbol_list_state_free(gpointer data) { SymbolListState *s=data; symbol_list_stop(s); g_free(s); }
static SymbolListState *symbol_list_state(GtkListStore *store) {
 SymbolListState *s=g_object_get_data(G_OBJECT(store),"insight-symbol-worker"); if(!s) { s=g_new0(SymbolListState,1); g_object_set_data_full(G_OBJECT(store),"insight-symbol-worker",s,symbol_list_state_free); } return s;
}
static gboolean symbol_list_apply(gpointer data) {
 GtkListStore *store=data; SymbolListState *s=symbol_list_state(store); gint64 deadline=g_get_monotonic_time()+3000;
 if(!wb.alive) { s->apply=0; return G_SOURCE_REMOVE; }
 if(s->collecting) {
  while(s->iter_valid) {
   gchar *key=symbol_row_key(GTK_TREE_MODEL(store),&s->iter,s->files!=NULL);
   GQueue *q=g_hash_table_lookup(s->existing,key);
   if(!q) { q=g_queue_new(); g_hash_table_insert(s->existing,key,q); } else g_free(key);
   GtkTreeIter *copy=g_new(GtkTreeIter,1); *copy=s->iter; g_queue_push_tail(q,copy);
   s->iter_valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&s->iter);
   if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
  }
  s->collecting=FALSE;
 }
 while(s->position<s->count) {
  guint n=s->position++; GtkTreeIter iter;
  /* Build the same key without inserting a temporary row into the model. */
  const InsightSymbol *tag=s->files ? NULL : s->rows[n];
  FileListRow *file=s->files ? g_ptr_array_index(s->files,n) : NULL;
  const gchar *fields[]={file ? file->name : tag->name,file ? file->path : (s->local ? s->local_path : tag->path),tag ? tag->scope : NULL,tag ? tag->kind : NULL};
  GString *key=g_string_new(NULL);
  for(guint k=0;k<G_N_ELEMENTS(fields);k++) { const gchar *v=fields[k] ? fields[k] : ""; g_string_append_printf(key,"%" G_GSIZE_FORMAT ":%s",strlen(v),v); }
  g_string_append_printf(key,":%lu",tag ? tag->line : 0);
  GQueue *q=g_hash_table_lookup(s->existing,key->str); GtkTreeIter *old=q ? g_queue_pop_head(q) : NULL;
  if(old) { iter=*old; g_free(old); if(file) gtk_list_store_set(store,&iter,2,file->directory,3,file->size,4,file->stat_valid,-1); } /* Preserve the selected row while refreshing metadata. */
  else {
   gtk_list_store_append(store,&iter);
   if(file) gtk_list_store_set(store,&iter,0,file->name,1,file->path,2,file->directory,3,file->size,4,file->stat_valid,-1);
   else gtk_list_store_set(store,&iter,SYMBOL_NAME,tag->name,SYMBOL_SCOPE,tag->scope,SYMBOL_PATH,s->local ? s->local_path : tag->path,SYMBOL_LINE,(gulong)tag->line,SYMBOL_KIND,tag->kind,-1);
  }
  g_string_free(key,TRUE);
  if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
 }
 /* Delete only obsolete rows. The iter remains valid when the model is sorted. */
 GHashTableIter pending; gpointer value; g_hash_table_iter_init(&pending,s->existing);
 while(g_hash_table_iter_next(&pending,NULL,&value)) {
  GQueue *q=value; GtkTreeIter *old;
  while((old=g_queue_pop_head(q))) { gtk_list_store_remove(store,old); g_free(old); if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE; }
  g_hash_table_iter_remove(&pending);
 }
 g_clear_pointer(&s->existing,g_hash_table_destroy);
 GtkWidget *view=symbol_list_view(store); if(view) { insight_symbol_view_page_state(GTK_TREE_VIEW(view),s->has_more); gtk_widget_set_sensitive(view,TRUE); insight_symbol_view_loading(GTK_TREE_VIEW(view),FALSE); }
 if(store==wb.file_store && wb.file_status) { gchar *text=insight_i18n_printf(s->has_more ? "Files: %u+" : "Files: %u",s->count);gtk_label_set_text(GTK_LABEL(wb.file_status),text);g_free(text);g_object_set_data(G_OBJECT(wb.file_status),"insight-files-count",GUINT_TO_POINTER(s->count));g_object_set_data(G_OBJECT(wb.file_status),"insight-files-more",GINT_TO_POINTER(s->has_more)); }
 s->apply=0; retire_index(s->index); s->index=NULL; g_clear_pointer(&s->rows,g_free); g_clear_pointer(&s->files,g_ptr_array_unref); return G_SOURCE_REMOVE;
}
static void symbol_list_job_free(gpointer data) { SymbolListJob *j=data;insight_language_filter_free(j->language); g_weak_ref_clear(&j->store); retire_index(j->index); g_free(j->query); g_free(j->path); g_free(j->rows); g_clear_pointer(&j->paths,g_ptr_array_unref); g_clear_pointer(&j->files,g_ptr_array_unref); g_clear_pointer(&j->tags,g_ptr_array_unref);g_clear_pointer(&j->outlines,g_ptr_array_unref); g_free(j); }
static void symbol_list_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; SymbolListJob *j=data; InsightLookupFilter *filter=insight_lookup_filter_new(j->query,j->paths!=NULL,NULL);
 if(j->paths) {
  j->files=g_ptr_array_new_with_free_func(file_list_row_free); GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);
  for(guint n=0;filter && n<j->paths->len && !g_cancellable_is_cancelled(cancel);n++) {
   const char *path=g_ptr_array_index(j->paths,n);if(!insight_language_filter_match(j->language,path)) continue; if(!g_utf8_validate(path,-1,NULL) || g_hash_table_contains(seen,path)) continue; g_hash_table_add(seen,(gpointer)path);
   if(insight_lookup_filter_match(filter,NULL,NULL,path,NULL)) { if(j->count==j->limit) { j->has_more=TRUE;break; } FileListRow *r=g_new0(FileListRow,1); r->path=g_strdup(path); r->name=g_path_get_basename(path); r->directory=g_path_get_dirname(path); gchar *native=utils_get_locale_from_utf8(path); GStatBuf st; r->stat_valid=native && g_stat(native,&st)==0 && S_ISREG(st.st_mode); if(r->stat_valid) r->size=(guint64)st.st_size; g_free(native); g_ptr_array_add(j->files,r); j->count++; }
  }
  g_hash_table_destroy(seen);
 } else {
  if(j->local && !g_cancellable_is_cancelled(cancel)) { j->index=insight_index_new(); TMSourceFile captured={.tags_array=j->tags,.lang=j->lang}; if(!insight_native_outline_import(j->index,&captured,j->path ? j->path : "@untitled")) { insight_lookup_filter_free(filter); g_task_return_boolean(task,FALSE); return; } }
  if(j->outlines) { j->index=insight_index_new();for(guint n=0;n<j->outlines->len && !g_cancellable_is_cancelled(cancel);n++) { OutlineFile *o=g_ptr_array_index(j->outlines,n);TMSourceFile file={.tags_array=o->tags,.lang=o->lang};if(!insight_native_outline_import(j->index,&file,o->path)) { insight_lookup_filter_free(filter);g_task_return_boolean(task,FALSE);return; } } }
  j->rows=g_new(const InsightSymbol *,j->limit);
  if(filter) for(gsize n=0;n<insight_index_count(j->index);n++) {
   if(g_cancellable_is_cancelled(cancel)) break;
   const InsightSymbol *tag=insight_index_at(j->index,n);if(!insight_language_filter_match(j->language,tag->path)) continue; if(j->path && g_strcmp0(j->path,tag->path)) continue;
   if(insight_lookup_filter_match(filter,tag->name,tag->scope,tag->path,g_str_has_prefix(tag->kind,"@decl:") ? strrchr(tag->kind,':')+1 : tag->kind)) { if(j->count==j->limit) { j->has_more=TRUE;break; } j->rows[j->count++]=tag; }
  }
 }
 insight_lookup_filter_free(filter); g_task_return_boolean(task,TRUE);
}
static void symbol_list_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); SymbolListJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL); GtkListStore *store=g_weak_ref_get(&j->store); if(!store) return;
 SymbolListState *s=symbol_list_state(store);
 if(wb.alive && j->epoch==workbench_epoch && j->generation==s->generation) {
  g_clear_object(&s->cancel); s->has_more=ok && j->has_more;
  if(ok) { s->index=insight_index_ref(j->index); s->rows=j->rows; j->rows=NULL; s->files=j->files; j->files=NULL; s->count=j->count; s->position=0; s->local=j->local; s->local_path=g_strdup(j->path); }
  else { if(j->paths) s->files=g_ptr_array_new_with_free_func(file_list_row_free); ui_set_statusbar(TRUE,"Symbol snapshot unavailable or exceeds its limit"); }
  s->existing=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,symbol_iter_queue_free);
  s->collecting=TRUE; s->iter_valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&s->iter);
  GtkWidget *view=symbol_list_view(store); if(view) { gtk_widget_set_sensitive(view,FALSE); insight_symbol_view_loading(GTK_TREE_VIEW(view),TRUE); }
  if(!s->apply) s->apply=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,symbol_list_apply,g_object_ref(store),g_object_unref);
 }
 g_object_unref(store);
}
static SymbolListJob *symbol_list_begin(GtkListStore *store,GtkWidget *entry,guint limit,gboolean files) {
 SymbolListState *s=symbol_list_state(store); symbol_list_stop(s); GtkWidget *view=symbol_list_view(store); if(view) { gtk_widget_set_sensitive(view,TRUE); insight_symbol_view_loading(GTK_TREE_VIEW(view),FALSE); }
 InsightLookupFilter *filter=entry_filter(entry,files); if(!filter) {
  s->has_more=FALSE;
  if(view) { gtk_widget_set_sensitive(view,FALSE); insight_symbol_view_loading(GTK_TREE_VIEW(view),TRUE); }
  s->files=files ? g_ptr_array_new_with_free_func(file_list_row_free) : NULL;
  s->existing=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,symbol_iter_queue_free); s->collecting=TRUE;
  s->iter_valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&s->iter);
  s->apply=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,symbol_list_apply,g_object_ref(store),g_object_unref); return NULL;
 } insight_lookup_filter_free(filter);
 SymbolListJob *j=g_new0(SymbolListJob,1); g_weak_ref_init(&j->store,store); j->query=g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))); j->limit=limit; j->generation=s->generation; j->epoch=workbench_epoch;GeanyDocument *active=document_get_current();if(active && (store==wb.symbols || store==wb.file_store)) j->language=insight_language_filter_new(active->file_name,active->file_type); return j;
}
static void symbol_list_launch(GtkListStore *store,SymbolListJob *j) {
 GtkWidget *view=symbol_list_view(store); if(view) { gtk_widget_set_sensitive(view,FALSE); insight_symbol_view_loading(GTK_TREE_VIEW(view),TRUE); }
 SymbolListState *s=symbol_list_state(store); s->cancel=g_cancellable_new(); GTask *task=g_task_new(NULL,s->cancel,symbol_list_job_done,NULL); g_task_set_task_data(task,j,symbol_list_job_free); g_task_run_in_thread(task,symbol_list_job_run); g_object_unref(task);
}
static void fill_symbol_list(GtkListStore *store,GtkWidget *entry,const char *path,guint limit) {
 refresh_index(); SymbolListJob *j=symbol_list_begin(store,entry,limit,FALSE); if(!j) return;
 if(store==wb.symbols) {
  j->outlines=g_ptr_array_new_with_free_func(outline_free);GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);gchar *root=wb.root ? utils_get_utf8_from_locale(wb.root) : NULL;
  for(guint n=0;n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n);if(!doc->is_valid || !doc->file_name || (wb.workspace_from_project && root && !insight_project_files_contains(wb.membership,root,doc->file_name))) continue;g_ptr_array_add(j->outlines,outline_capture(doc->tm_file,doc->file_name));g_hash_table_add(seen,doc->file_name); }
  GHashTableIter iter;gpointer key,value;g_hash_table_iter_init(&iter,wb.declarations);while(g_hash_table_iter_next(&iter,&key,&value)) if(!g_hash_table_contains(seen,key)) g_ptr_array_add(j->outlines,outline_copy(value));g_hash_table_unref(seen);g_free(root);
 }else j->index=insight_index_ref(wb.index);j->path=g_strdup(path);symbol_list_launch(store,j);
}
static void browser_filter(GtkEditable *entry,gpointer data) {
 (void)data; if(!wb.symbols) return;
 g_object_set_data_full(G_OBJECT(wb.symbol_view),"insight-project-root",wb.root ? utils_get_utf8_from_locale(wb.root) : NULL,g_free);
 GeanyDocument *doc=document_get_current();gchar *profile=doc ? insight_languages_profile_name(doc->editor->sci,doc->file_name,doc->file_type) : g_strdup("");
 if(entry || !wb.project_symbol_limit || g_strcmp0(wb.project_page_root,wb.root) || g_strcmp0(wb.project_page_profile,profile)) wb.project_symbol_limit=500;
 g_free(wb.project_page_root);wb.project_page_root=g_strdup(wb.root);g_free(wb.project_page_profile);wb.project_page_profile=profile;
 fill_symbol_list(wb.symbols,wb.symbol_entry,NULL,wb.project_symbol_limit);
}
static void browser_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data; if(!wb.preview || !gtk_widget_get_sensitive(GTK_WIDGET(gtk_tree_selection_get_tree_view(selection)))) return;
 GtkWidget *view=GTK_WIDGET(gtk_tree_selection_get_tree_view(selection));if((view==wb.local_view || view==wb.symbol_view) && !gtk_widget_has_focus(view)) return;
 GtkTreeIter iter; GtkTreeModel *model;
 if(!gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 gchar *path,*name; gulong line; gtk_tree_model_get(model,&iter,SYMBOL_PATH,&path,SYMBOL_NAME,&name,SYMBOL_LINE,&line,-1);
 if(!line) { g_free(path); g_free(name); return; }
 reveal_context(); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); if(show_location(path,gtk_tree_selection_get_tree_view(selection)==GTK_TREE_VIEW(wb.local_view) ? wb.local_doc_id : 0,line,TRUE)) preview_mark_symbol(name); g_free(path); g_free(name);
}
static gboolean browser_symbol_clicked(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)data;if(event->button!=1 || !gtk_widget_has_focus(widget)) return FALSE;
 GtkTreePath *row=NULL;if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),(gint)event->x,(gint)event->y,&row,NULL,NULL,NULL)) { gtk_tree_path_free(row);browser_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)),NULL); }return FALSE;
}
static void browser_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; (void)data;
 GtkTreeSelection *selection=gtk_tree_view_get_selection(view);
 gtk_tree_selection_select_path(selection,path);
 if(GTK_WIDGET(view)==wb.symbol_view || GTK_WIDGET(view)==wb.local_view) { GtkTreeModel *model=gtk_tree_view_get_model(view); GtkTreeIter iter; gulong line=0; if(!gtk_tree_model_get_iter(model,&iter,path)) return; gtk_tree_model_get(model,&iter,SYMBOL_LINE,&line,-1); if(!line) { if(gtk_tree_view_row_expanded(view,path)) gtk_tree_view_collapse_row(view,path); else gtk_tree_view_expand_row(view,path,FALSE); return; } }
 if(GTK_WIDGET(view)==wb.local_view || GTK_WIDGET(view)==wb.symbol_view) {
  GtkTreeModel *model=gtk_tree_view_get_model(view);GtkTreeIter iter;gchar *file=NULL,*name=NULL;gulong line=0;
  if(gtk_tree_model_get_iter(model,&iter,path)) { gtk_tree_model_get(model,&iter,SYMBOL_PATH,&file,SYMBOL_NAME,&name,SYMBOL_LINE,&line,-1);GeanyDocument *doc=GTK_WIDGET(view)==wb.local_view ? document_find_by_id(wb.local_doc_id) : NULL;
   if(!doc && file && line) { gchar *native=utils_get_locale_from_utf8(file);doc=document_open_file(native,FALSE,NULL,NULL);g_free(native); }
   if(doc && line) { editor_mark_symbol(doc,line,name);document_grab_focus(doc); }
  }g_free(file);g_free(name);return;
 }
 if(GTK_WIDGET(view)==wb.symbol_view || GTK_WIDGET(view)==wb.local_view || GTK_WIDGET(view)==wb.category_view) browser_selected(selection,NULL);
 else if(GTK_WIDGET(view)==wb.file_view) {
  GtkTreeIter iter; GtkTreeModel *model=gtk_tree_view_get_model(view);
  if(!gtk_tree_model_get_iter(model,&iter,path)) return;
  gchar *filename; gtk_tree_model_get(model,&iter,1,&filename,-1);
  gchar *native=utils_get_locale_from_utf8(filename); GeanyDocument *doc=document_open_file(native,FALSE,NULL,NULL); g_free(native); g_free(filename);
  if(doc) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),TRUE); document_grab_focus(doc); insight_workbench_refresh(); }
  return;
 }
 else return;
 open_context(NULL,NULL);
}
static gboolean symbol_query_valid(const gchar *query,gboolean files) {
 if(!query || !*query || strlen(query)>256 || !g_utf8_validate(query,-1,NULL)) return FALSE;
 gchar *trimmed=g_strdup(query);g_strstrip(trimmed);gboolean empty=!*trimmed;g_free(trimmed);if(empty) return FALSE;
 InsightLookupFilter *filter=insight_lookup_filter_new(query,files,NULL);if(!filter) return FALSE;insight_lookup_filter_free(filter);return TRUE;
}
static void symbol_history_record(GtkWidget *entry) {
 GPtrArray *history=g_object_get_data(G_OBJECT(entry),"insight-symbol-history");const gchar *query=gtk_entry_get_text(GTK_ENTRY(entry));if(!history || !symbol_query_valid(query,entry==wb.file_entry)) return;
 insight_search_history_record(history,query);if(history->len>16) g_ptr_array_set_size(history,16);save_workbench_settings();
}
static void symbol_history_remember(GtkMenuItem *item,gpointer data) { (void)item;symbol_history_record(GTK_WIDGET(data)); }
static void symbol_history_pick(GtkMenuItem *item,gpointer data) {
 GtkWidget *entry=data;const gchar *query=g_object_get_data(G_OBJECT(item),"query");gtk_entry_set_text(GTK_ENTRY(entry),query);gtk_widget_grab_focus(entry);
}
static void symbol_history_clear(GtkMenuItem *item,gpointer data) {
 (void)item;GPtrArray *history=g_object_get_data(G_OBJECT(data),"insight-symbol-history");g_ptr_array_set_size(history,0);save_workbench_settings();
}
static GtkWidget *symbol_history_item(const gchar *label,const gchar *icon) {
 GtkWidget *item=gtk_image_menu_item_new_with_label(label);gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),insight_command_icon(icon));gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item),TRUE);return item;
}
static void symbol_history_show(GtkWidget *menu,gpointer data) {
 GtkWidget *entry=data;GPtrArray *history=g_object_get_data(G_OBJECT(entry),"insight-symbol-history");GList *children=gtk_container_get_children(GTK_CONTAINER(menu));for(GList *p=children;p;p=p->next) gtk_widget_destroy(p->data);g_list_free(children);
 GtkWidget *remember=symbol_history_item(entry==wb.file_entry ? "Remember current file filter" : "Remember current symbol filter","document-save-symbolic");gtk_widget_set_name(remember,"insight-symbol-history-remember");gtk_widget_set_sensitive(remember,symbol_query_valid(gtk_entry_get_text(GTK_ENTRY(entry)),entry==wb.file_entry));g_signal_connect_object(remember,"activate",G_CALLBACK(symbol_history_remember),entry,0);gtk_menu_shell_append(GTK_MENU_SHELL(menu),remember);gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());
 for(guint i=0;i<history->len;i++) { const gchar *query=g_ptr_array_index(history,i);gchar *label=g_utf8_substring(query,0,MIN((glong)64,g_utf8_strlen(query,-1)));GtkWidget *item=symbol_history_item(label,"edit-find");g_free(label);gchar *name=g_strdup_printf("insight-symbol-history-%u",i);gtk_widget_set_name(item,name);g_free(name);g_object_set_data(G_OBJECT(item),"insight-no-i18n",GINT_TO_POINTER(1));gtk_widget_set_tooltip_text(item,query);g_object_set_data_full(G_OBJECT(item),"query",g_strdup(query),g_free);g_signal_connect_object(item,"activate",G_CALLBACK(symbol_history_pick),entry,0);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item); }
 GtkWidget *clear=symbol_history_item(history->len ? "Clear history" : "History is empty","edit-clear");gtk_widget_set_name(clear,"insight-symbol-history-clear");gtk_widget_set_sensitive(clear,history->len>0);g_signal_connect_object(clear,"activate",G_CALLBACK(symbol_history_clear),entry,0);if(history->len) gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());gtk_menu_shell_append(GTK_MENU_SHELL(menu),clear);insight_i18n_apply(menu);gtk_widget_show_all(menu);
}
/* A narrow sidebar must shrink text, not allocate controls beyond its edge.
 * Keep native button/arrow/icon requisitions and ellipsize only their labels. */
static void panel_scroll_minimum(GtkWidget *widget) {
 if(GTK_IS_SCROLLED_WINDOW(widget)) gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(widget),48);
 if(GTK_IS_CONTAINER(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget));for(GList *p=children;p;p=p->next) panel_scroll_minimum(p->data);g_list_free(children); }
}
static void compact_panel_control(GtkWidget *widget);
static void compact_button_label(GObject *object,GParamSpec *spec,gpointer data) {
 (void)spec;(void)data;
 /* GtkButton may rebuild its label when its translated caption changes. */
 GtkWidget *label=gtk_bin_get_child(GTK_BIN(object));if(label) compact_panel_control(label);
}
static void compact_panel_control(GtkWidget *widget) {
 if(GTK_IS_BUTTON(widget)) {
  if(!g_object_get_data(G_OBJECT(widget),"insight-compact-label")) {
   g_object_set_data(G_OBJECT(widget),"insight-compact-label",GINT_TO_POINTER(1));
   g_signal_connect(widget,"notify::label",G_CALLBACK(compact_button_label),NULL);
  }
  compact_button_label(G_OBJECT(widget),NULL,NULL);
 }
 if(GTK_IS_SCROLLED_WINDOW(widget)) gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(widget),48);
 if(GTK_IS_ENTRY(widget)) { gtk_style_context_add_class(gtk_widget_get_style_context(widget),"insight-shrink-control");gtk_entry_set_width_chars(GTK_ENTRY(widget),1);gtk_entry_set_max_width_chars(GTK_ENTRY(widget),18); }
 if(GTK_IS_COMBO_BOX(widget)) {
  gtk_style_context_add_class(gtk_widget_get_style_context(widget),"insight-shrink-control");
  GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(widget));for(GList *p=cells;p;p=p->next) if(GTK_IS_CELL_RENDERER_TEXT(p->data)) g_object_set(p->data,"ellipsize",PANGO_ELLIPSIZE_END,"width-chars",1,NULL);g_list_free(cells);
 }
 if(GTK_IS_LABEL(widget)) {
  const gchar *text=gtk_label_get_text(GTK_LABEL(widget));if(text && *text && !gtk_widget_get_has_tooltip(widget)) gtk_widget_set_tooltip_text(widget,text);
  gtk_label_set_ellipsize(GTK_LABEL(widget),PANGO_ELLIPSIZE_END);gtk_label_set_width_chars(GTK_LABEL(widget),1);gtk_label_set_max_width_chars(GTK_LABEL(widget),24);
 }
 if(GTK_IS_CONTAINER(widget) && !GTK_IS_COMBO_BOX(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget));for(GList *p=children;p;p=p->next) compact_panel_control(p->data);g_list_free(children); }
}
static GtkWidget *symbol_query_controls(GtkWidget *entry,gboolean local) {
 compact_panel_control(entry);
 gtk_widget_set_tooltip_text(entry,"Use ~term for fuzzy search (characters in order); @~path and scope=~owner filter fields");
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*button=gtk_menu_button_new(),*menu=gtk_menu_new();g_object_set_data_full(G_OBJECT(entry),"insight-symbol-history",g_ptr_array_new_with_free_func(g_free),(GDestroyNotify)g_ptr_array_unref);
 gtk_widget_set_name(button,entry==wb.file_entry ? "insight-files-query-history" : (local ? "insight-local-query-history" : "insight-project-query-history"));gtk_button_set_image(GTK_BUTTON(button),insight_command_icon("document-open-recent"));gtk_widget_set_tooltip_text(button,entry==wb.file_entry ? "File filter history: 16 valid queries, up to 256 bytes each" : "Symbol filter history: 16 valid queries, up to 256 bytes each");gtk_button_set_relief(GTK_BUTTON(button),GTK_RELIEF_NONE);gtk_menu_button_set_popup(GTK_MENU_BUTTON(button),menu);g_signal_connect_object(menu,"show",G_CALLBACK(symbol_history_show),entry,0);
 gtk_box_pack_start(GTK_BOX(row),entry,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(row),button,FALSE,FALSE,0);return row;
}
static void symbol_history_write(GKeyFile *settings,GtkWidget *entry,const gchar *prefix) {
 GPtrArray *history=g_object_get_data(G_OBJECT(entry),"insight-symbol-history");gchar *key=g_strconcat(prefix,"-query-history",NULL);g_key_file_set_string_list(settings,"workbench",key,(const gchar *const *)history->pdata,history->len);g_free(key);
}
static void symbol_history_read(GKeyFile *settings,GtkWidget *entry,const gchar *prefix) {
 gchar *key=g_strconcat(prefix,"-query-history",NULL);gsize count=0;gchar **queries=g_key_file_get_string_list(settings,"workbench",key,&count,NULL);g_free(key);GPtrArray *history=g_object_get_data(G_OBJECT(entry),"insight-symbol-history");
 if(queries && count<=16) for(gsize i=count;i>0;i--) if(symbol_query_valid(queries[i-1],entry==wb.file_entry)) insight_search_history_record(history,queries[i-1]);
 g_strfreev(queries);
}
static gboolean file_browse_command(GtkEntry *entry) {
 const gchar *query=gtk_entry_get_text(entry);if(!wb.disk_browser || (strcmp(query,".") && strcmp(query,"..") && strcmp(query,"**"))) return FALSE;
 gboolean project=!strcmp(query,"**"),up=!strcmp(query,"..");
 gtk_entry_set_text(entry,"");
 if(project) { gtk_combo_box_set_active(GTK_COMBO_BOX(wb.folder_mode),0);focus_widget_panel(wb.file_entry);return TRUE; }
 gchar *root=wb.root ? utils_get_utf8_from_locale(wb.root) : g_get_current_dir(),*path=up ? g_path_get_dirname(root) : g_strdup(root);
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.folder_mode),1);insight_disk_browser_set_root(wb.disk_browser,path);g_free(path);g_free(root);
 focus_widget_panel(wb.disk_browser);gtk_widget_child_focus(wb.disk_browser,GTK_DIR_TAB_FORWARD);return TRUE;
}
static gboolean file_wildcard_command(GtkEntry *entry) {
 const gchar *query=gtk_entry_get_text(entry);gchar *trimmed=g_strdup(query);g_strstrip(trimmed);gboolean clear=!strcmp(trimmed,"*");g_free(trimmed);
 if(clear) { gtk_entry_set_text(entry,"");return TRUE; }
 InsightLookupFilter *filter=insight_lookup_filter_new(query,TRUE,NULL);gboolean wildcard=insight_lookup_filter_has_wildcards(filter);insight_lookup_filter_free(filter);
 if(wildcard) symbol_history_record(GTK_WIDGET(entry));
 return wildcard;
}
static void browser_entry_activate(GtkEntry *entry,gpointer data) {
 (void)data;if(GTK_WIDGET(entry)==wb.local_entry && wb.local_mode && gtk_combo_box_get_active(GTK_COMBO_BOX(wb.local_mode))==0) { GtkWidget *tree=gtk_bin_get_child(GTK_BIN(ui_lookup_widget(main_widgets.window,"scrolledwindow2")));if(GTK_IS_TREE_VIEW(tree)) { GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(tree));GtkTreeIter iter;if(gtk_tree_model_get_iter_first(model,&iter)) { GtkTreeIter child;while(gtk_tree_model_iter_children(model,&child,&iter)) iter=child;GtkTreePath *path=gtk_tree_model_get_path(model,&iter);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_path_free(path);gtk_widget_grab_focus(tree); } }return; }if(GTK_WIDGET(entry)==wb.file_entry && (file_browse_command(entry) || file_wildcard_command(entry))) return;if(GTK_WIDGET(entry)==wb.local_entry || GTK_WIDGET(entry)==wb.symbol_entry || GTK_WIDGET(entry)==wb.file_entry) symbol_history_record(GTK_WIDGET(entry)); GtkTreeView *view=GTK_TREE_VIEW(GTK_WIDGET(entry)==wb.local_entry ? wb.local_view : (GTK_WIDGET(entry)==wb.symbol_entry ? wb.symbol_view : wb.file_view));
 if(!gtk_widget_get_sensitive(GTK_WIDGET(view))) return;
 GtkTreeModel *model=gtk_tree_view_get_model(view); GtkTreeIter iter;
 if(gtk_tree_model_get_iter_first(model,&iter)) { GtkTreeIter child; while(gtk_tree_model_iter_children(model,&child,&iter)) iter=child; GtkTreePath *path=gtk_tree_model_get_path(model,&iter); gtk_tree_view_expand_to_path(view,path); browser_activated(view,path,NULL,NULL); gtk_tree_path_free(path); }
}
static void outline_changed(GtkToggleButton *button,gpointer data) { (void)button; (void)data; browser_filter(NULL,NULL); }
static gint project_path_compare(gconstpointer a,gconstpointer b) { return g_strcmp0(*(gchar *const *)a,*(gchar *const *)b); }
static void files_filter(GtkEditable *entry,gpointer data) {
 (void)entry; (void)data; if(!wb.file_store) return;
 g_ptr_array_sort(wb.files,project_path_compare);GChecksum *sum=g_checksum_new(G_CHECKSUM_SHA256);const gchar *query=gtk_entry_get_text(GTK_ENTRY(wb.file_entry));g_checksum_update(sum,(const guchar *)query,strlen(query)+1);if(wb.root) g_checksum_update(sum,(const guchar *)wb.root,strlen(wb.root)+1);
 for(guint n=0;n<wb.files->len;n++) { const gchar *path=g_ptr_array_index(wb.files,n);g_checksum_update(sum,(const guchar *)path,strlen(path)+1); }
 GeanyDocument *active=document_get_current();gchar *profile=active?insight_languages_profile_name(active->editor->sci,active->file_name,active->file_type):g_strdup("");g_checksum_update(sum,(const guchar *)profile,strlen(profile)+1);g_free(profile);if(active&&active->file_type) g_checksum_update(sum,(const guchar *)active->file_type->name,strlen(active->file_type->name)+1);
 if(!wb.workspace_from_project) for(guint n=0;n<documents_array->len;n++) { GeanyDocument *d=g_ptr_array_index(documents_array,n);if(d->is_valid&&d->file_name) g_checksum_update(sum,(const guchar *)d->file_name,strlen(d->file_name)+1); }
 const gchar *key=g_checksum_get_string(sum);gboolean same=!g_strcmp0(key,g_object_get_data(G_OBJECT(wb.file_store),"insight-request-key"));if(!same) g_object_set_data_full(G_OBJECT(wb.file_store),"insight-request-key",g_strdup(key),g_free);g_checksum_free(sum);if(same) return;
 SymbolListJob *j=symbol_list_begin(wb.file_store,wb.file_entry,10000,TRUE); if(!j) return;
 j->paths=g_ptr_array_new_with_free_func(g_free);
 for(guint n=0;n<wb.files->len;n++) g_ptr_array_add(j->paths,g_strdup(g_ptr_array_index(wb.files,n)));
 if(!wb.workspace_from_project) for(guint n=0;n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n); if(doc->is_valid && doc->file_name) g_ptr_array_add(j->paths,g_strdup(doc->file_name)); }
 symbol_list_launch(wb.file_store,j);
}
static void file_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data; GtkTreeIter iter; GtkTreeModel *model;
 if(!wb.preview || !gtk_widget_get_sensitive(GTK_WIDGET(gtk_tree_selection_get_tree_view(selection))) || !gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 gchar *path; gtk_tree_model_get(model,&iter,1,&path,-1); reveal_context(); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); request_file_preview(path); g_free(path);
}
static void file_action(guint action) {
 if(action==FP_HELP) { insight_help_show(NULL,"panel-project-files.md");return; }if(!gtk_widget_get_sensitive(wb.file_view)) return;
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(wb.file_view));GtkTreeIter iter;gboolean selected=gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.file_view)),NULL,&iter);gchar *name=NULL,*path=NULL;if(selected) gtk_tree_model_get(model,&iter,0,&name,1,&path,-1);
 switch(action) {
 case FP_REVEAL: if(path) ui_reveal_file(path);break;
 case FP_OPEN: if(selected) { GtkTreePath *row=gtk_tree_model_get_path(model,&iter);browser_activated(GTK_TREE_VIEW(wb.file_view),row,NULL,NULL);gtk_tree_path_free(row); }break;
 case FP_NAME: case FP_PATH: if(selected) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),action==FP_NAME ? name : path,-1);break;
 case FP_FILTER: if(path) { gchar *query=g_strdup_printf("file:\"%s\"",path);if(strlen(query)<=1024) { gtk_entry_set_text(GTK_ENTRY(wb.file_entry),query);gtk_widget_grab_focus(wb.file_entry); }else ui_set_statusbar(TRUE,"Selected value is too long for a lookup filter");g_free(query); }break;
 case FP_CLEAR: gtk_entry_set_text(GTK_ENTRY(wb.file_entry),"");gtk_widget_grab_focus(wb.file_entry);break;
 case FP_RESET: insight_symbol_view_columns_reset(GTK_TREE_VIEW(wb.file_view));break;
 default:break;
 }g_free(name);g_free(path);
}
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
 gtk_box_pack_start(GTK_BOX(box),symbol_query_controls(wb.file_entry,FALSE),FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 gtk_box_pack_start(GTK_BOX(box),sort_selector(wb.file_store),FALSE,FALSE,0);wb.file_status=gtk_label_new("");gtk_widget_set_name(wb.file_status,"insight-files-count");gtk_label_set_xalign(GTK_LABEL(wb.file_status),0);gtk_widget_set_tooltip_text(wb.file_status,"Loaded matching files; + means more remain beyond the 10000-file list limit");gtk_box_pack_start(GTK_BOX(box),wb.file_status,FALSE,FALSE,0);
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
 g_clear_pointer(&wb.usage_name,g_free);
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
typedef struct { gchar *path,*query,*text,*digest,*cached_stamp,*cached_language;InsightIndex *cached_calls; InsightOccurrence *found; GPtrArray *snippets; gulong *columns; gsize total,apply_position; guint id,flags,context,capacity; gint mode,encoding; InsightCodeDialect dialect; gboolean keywords,have_dialect,valid;InsightSourceRegion region; guint64 generation,epoch; } SearchJob;
static gboolean usage_step(gpointer);
static void search_job_free(gpointer data) { SearchJob *j=data; g_free(j->path); g_free(j->query); g_free(j->text); g_free(j->digest);g_free(j->cached_stamp);g_free(j->cached_language);retire_index(j->cached_calls); g_free(j->found); g_clear_pointer(&j->snippets,g_ptr_array_unref); g_free(j->columns); g_free(j); }
static void search_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; SearchJob *j=data;
 if(j->mode==7&&!j->text&&j->cached_calls&&j->cached_stamp) {
  gchar *native=utils_get_locale_from_utf8(j->path);gchar *now=source_stamp(native,j->cached_language,j->encoding);g_free(native);
  const InsightSymbol *coverage=NULL;gboolean usable=!g_strcmp0(now,j->cached_stamp)&&insight_index_query(j->cached_calls,"@file",TRUE,NULL,"@file",j->path,0,&coverage,1)==1;g_free(now);
  if(usable) {
   j->digest=g_strdup(coverage->inheritance);j->total=insight_index_query(j->cached_calls,j->query,TRUE,NULL,"@call",j->path,0,NULL,0);gsize count=MIN(j->total,j->capacity);const InsightSymbol **rows=g_new(const InsightSymbol *,count);insight_index_query(j->cached_calls,j->query,TRUE,NULL,"@call",j->path,0,rows,count);j->found=g_new0(InsightOccurrence,count);j->columns=g_new0(gulong,count);j->snippets=g_ptr_array_new_with_free_func(g_free);j->valid=TRUE;
   for(gsize n=0;n<count;n++) { gchar **parts=g_strsplit(rows[n]->signature,":",3);if(g_strv_length(parts)!=3) j->valid=FALSE;else { j->found[n]=(InsightOccurrence){g_ascii_strtoull(parts[0],NULL,10),g_ascii_strtoull(parts[1],NULL,10),rows[n]->line,0};j->columns[n]=g_ascii_strtoull(parts[2],NULL,10);g_ptr_array_add(j->snippets,g_strdup(rows[n]->type)); }g_strfreev(parts); }g_free(rows);g_task_return_boolean(task,TRUE);return;
  }
 }
 if(!j->text && j->path && !g_cancellable_is_cancelled(cancel)) j->text=read_disk_source_full(j->path,TRUE,j->encoding,cancel);
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
  wb.usage_idle=0; gchar *label=g_strdup_printf("%u results • %u files scanned • %u skipped%s",wb.usage_shown,wb.usage_position,wb.usage_skipped,wb.usage_shown>=2000 ? " • result limit reached" : ""); set_usage_status(label); g_free(label); return G_SOURCE_REMOVE;
 }
 Location *location=g_ptr_array_index(wb.usage_paths,wb.usage_position++); GeanyDocument *doc=location->id ? document_find_by_id(location->id) : document_find_by_filename(location->path);
 SearchJob *j=g_new0(SearchJob,1); j->path=g_strdup(location->path); j->query=g_strdup(wb.usage_name); j->id=doc ? doc->id : 0; j->mode=wb.usage_mode; j->flags=wb.usage_flags; j->keywords=wb.usage_keywords; j->context=wb.usage_context; j->capacity=2000-wb.usage_shown; j->encoding=file_prefs.default_open_encoding; j->generation=wb.usage_generation; j->epoch=workbench_epoch;
 if(doc) { gint length=sci_get_length(doc->editor->sci); if(length>=0 && length<=2*1024*1024) { j->text=sci_get_contents(doc->editor->sci,length+1); if(j->text && (!g_utf8_validate(j->text,length,NULL) || memchr(j->text,0,length))) g_clear_pointer(&j->text,g_free); } }
 if(j->mode==7&&!doc&&j->path) { j->cached_calls=insight_index_ref(wb.disk_calls);j->cached_stamp=g_strdup(g_hash_table_lookup(wb.file_stamps,j->path)); }
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
static gchar *relation_row_key(const gchar *display,const gchar *path,const gchar *name,const gchar *description,gulong line) {
 GString *key=g_string_new(NULL); const gchar *fields[]={display,path,name,description};
 for(guint k=0;k<G_N_ELEMENTS(fields);k++) {
  if(fields[k]) g_string_append_printf(key,"%" G_GSIZE_FORMAT ":%s",strlen(fields[k]),fields[k]);
  else g_string_append(key,"-1:");
 }
 g_string_append_printf(key,":%lu",line); return g_string_free(key,FALSE);
}
static gchar *relation_existing_key(GtkTreeModel *model,GtkTreeIter *iter) {
 gchar *display=NULL,*path=NULL,*name=NULL,*description=NULL,*digest=NULL; gulong line=0,start=0,end=0,column=0; guint id=0;
 gtk_tree_model_get(model,iter,USAGE_DISPLAY,&display,USAGE_PATH,&path,USAGE_SYMBOL,&name,USAGE_SNIPPET,&description,USAGE_LINE,&line,USAGE_START,&start,USAGE_END,&end,USAGE_ID,&id,USAGE_COLUMN,&column,USAGE_DIGEST,&digest,-1);
 gchar *key=relation_row_key(display,path,name,description,line);
 /* Reference rows also carry hit coordinates/digests and must not be reused
  * as definition rows even when their visible strings happen to coincide. */
 if(start || end || id || column || digest) { gchar *old=key; key=g_strconcat("reference:",old,NULL); g_free(old); }
 g_free(display); g_free(path); g_free(name); g_free(description); g_free(digest); return key;
}
static gboolean relation_results_apply(gpointer data) {
 GTask *task=data; RelationJob *j=g_task_get_task_data(task);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.usage_generation) return G_SOURCE_REMOVE;
 if(j->index_version!=wb.index_version) { wb.usage_idle=0; usage_start(NULL,NULL); return G_SOURCE_REMOVE; }
 gint64 deadline=g_get_monotonic_time()+3000;
 if(j->collecting) {
  while(j->iter_valid) {
   gchar *key=relation_existing_key(GTK_TREE_MODEL(wb.matches),&j->iter); GQueue *q=g_hash_table_lookup(j->existing,key);
   if(!q) { q=g_queue_new(); g_hash_table_insert(j->existing,key,q); } else g_free(key);
   GtkTreeIter *copy=g_new(GtkTreeIter,1); *copy=j->iter; g_queue_push_tail(q,copy);
   j->iter_valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(wb.matches),&j->iter);
   if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
  }
  j->collecting=FALSE;
 }
 while(j->apply_position<MIN(j->total,G_N_ELEMENTS(j->found))) {
  gsize n=j->apply_position++; const InsightSymbol *tag=j->found[n]; const gchar *description=g_ptr_array_index(j->descriptions,n);
  gchar *key=relation_row_key(tag->path,tag->path,tag->name,description,tag->line); GQueue *q=g_hash_table_lookup(j->existing,key); GtkTreeIter *old=q ? g_queue_pop_head(q) : NULL; g_free(key);
  if(old) g_free(old);
  else { GtkTreeIter iter; gtk_list_store_append(wb.matches,&iter); gtk_list_store_set(wb.matches,&iter,USAGE_DISPLAY,tag->path,USAGE_PATH,tag->path,USAGE_LINE,tag->line,USAGE_SYMBOL,tag->name,USAGE_SNIPPET,description,-1); }
  if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
 }
 GHashTableIter pending; gpointer value; g_hash_table_iter_init(&pending,j->existing);
 while(g_hash_table_iter_next(&pending,NULL,&value)) {
  GQueue *q=value; GtkTreeIter *old;
  while((old=g_queue_pop_head(q))) { gtk_list_store_remove(wb.matches,old); g_free(old); if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE; }
  g_hash_table_iter_remove(&pending);
 }
 gchar *label=g_strdup_printf("%zu tag-based %s%s",j->total,j->mode==1 ? "definitions/declarations" : (j->mode==2 ? (j->transitive ? "direct and inherited members" : "direct members") : (j->mode==4 ? "base definitions" : "derived types")),j->limited ? " • showing first 2000 matches" : (j->depth_limited ? " • inheritance depth limit reached" : (j->ambiguous ? " • ambiguous class names omitted" : ""))); set_usage_status(j->invalid ? "Enter a symbol name (or qualified owner for Members)" : label); g_free(label);
 wb.usage_idle=0; return G_SOURCE_REMOVE;
}
static void relation_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); RelationJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.usage_generation) return;
 g_clear_object(&wb.search_worker);
 if(!ok) return;
 if(j->index_version!=wb.index_version) { usage_start(NULL,NULL); return; }
 if(j->total==SIZE_MAX) { j->invalid=TRUE; j->total=0; }
 if((j->mode==2 || j->mode==4 || j->mode==5) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle))) {
  GeanyDocument *doc=document_get_current();insight_relation_tree_query(wb.relation_tree,j->index,j->query,j->mode==2 ? INSIGHT_RELATION_MEMBERS : (j->mode==4 ? INSIGHT_RELATION_BASES : INSIGHT_RELATION_DERIVED),doc ? doc->file_name : NULL,doc ? doc->file_type : NULL);
  insight_relation_tree_filter(wb.relation_tree,gtk_entry_get_text(GTK_ENTRY(wb.relation_filter_entry)));
 }
 j->existing=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,symbol_iter_queue_free); j->collecting=TRUE; j->iter_valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(wb.matches),&j->iter); wb.usage_idle=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,relation_results_apply,g_object_ref(task),g_object_unref);
}
static void usage_start(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; stop_usage();
 wb.usage_region=reference_region();
 gint search_region=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.search_region));wb.project_region=search_region==1 ? INSIGHT_REGION_SKIP_COMMENTS : search_region==2 ? INSIGHT_REGION_COMMENTS : search_region==3 ? INSIGHT_REGION_CODE : INSIGHT_REGION_ALL;
 const char *query=gtk_entry_get_text(GTK_ENTRY(wb.usage_entry)); size_t total;
 gint mode=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)); wb.usage_mode=mode;
 wb.usage_target=mode==3 ? wb.search_matches : wb.matches;
 if(mode==6) { relation_calls(query);return; }
 if(mode==1 || mode==2 || mode==4 || mode==5) {
  refresh_index(); RelationJob *j=g_new0(RelationJob,1); j->index=insight_index_ref(wb.index); j->query=g_strdup(query); j->mode=mode; j->transitive=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_transitive)); j->epoch=workbench_epoch; j->generation=wb.usage_generation; j->index_version=wb.index_version;GeanyDocument *active=document_get_current();if(active) j->language=insight_language_filter_new(active->file_name,active->file_type);
  wb.search_worker=g_cancellable_new(); GTask *task=g_task_new(NULL,wb.search_worker,relation_job_done,NULL); g_task_set_task_data(task,j,relation_job_free); g_task_run_in_thread(task,relation_job_run); g_object_unref(task); set_usage_status("Searching symbol relations…"); return;
 }
 wb.usage_flags=(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.project_case)) ? INSIGHT_SEARCH_CASE : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.project_word)) ? INSIGHT_SEARCH_WORD : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.project_regex)) ? INSIGHT_SEARCH_REGEX : 0);
 GError *query_error=NULL;
 wb.usage_keywords=mode==3 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.search_keywords)); wb.usage_context=(guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(wb.search_context));
 if(wb.usage_keywords && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.search_variations))) wb.usage_flags|=INSIGHT_KEYWORD_VARIATIONS;
 if(mode==3 && !(wb.usage_keywords ? insight_keyword_collect("",0,query,wb.usage_flags,wb.usage_context,NULL,0,&total,&query_error) : insight_context_collect("",0,query,wb.usage_flags,NULL,0,&total,&query_error))) { set_usage_status(query_error ? query_error->message : "Invalid text search query"); g_clear_error(&query_error); return; }
 if(mode!=3 && !insight_occurrences_find("",0,query,strlen(query),0,NULL,0,&total)) { set_usage_status("Enter one UTF-8 identifier"); return; }
 if(mode==3) insight_search_links_clear(wb.search_links);
 wb.usage_existing=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,symbol_iter_queue_free); wb.usage_collecting=TRUE;
 wb.usage_iter_valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(wb.usage_target),&wb.usage_iter);
 if(mode==3) { gtk_entry_set_text(GTK_ENTRY(wb.project_query),query); insight_search_history_record(wb.search_history,query); save_search_settings(); if(widget) focus_search_results(NULL,NULL); }
 wb.usage_name=g_strdup(query); wb.usage_position=wb.usage_shown=wb.usage_skipped=0;
 GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);
 gint scope=mode==3 ? gtk_combo_box_get_active(GTK_COMBO_BOX(wb.search_scope)) : (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_buffer)) ? 2 : 0);
 GPtrArray *paths=mode==3 && scope==3 ? wb.search_files : wb.files;
 if(scope==0 || scope==3) {
  for(guint i=0;i<paths->len && wb.usage_paths->len<10000;i++) {
   const char *path=g_ptr_array_index(paths,i);GeanyDocument *active=document_get_current();if(active && !insight_languages_search_path(active->file_name,active->file_type,path)) continue; if(g_hash_table_contains(seen,path) || (mode==3 && !search_path_matches(path))) continue;
   Location *next=g_new0(Location,1); next->path=g_strdup(path); g_ptr_array_add(wb.usage_paths,next); g_hash_table_add(seen,next->path);
  }
 }
 for(guint i=0;i<documents_array->len && wb.usage_paths->len<10000;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);
  if(!doc->is_valid || (scope==2 && doc!=document_get_current()) || (mode==3 && (scope==0 || scope==3)) || (mode==3 && doc->file_name && !search_path_matches(doc->file_name))) continue;
  GeanyDocument *active=document_get_current();if(active && doc->file_name && !insight_languages_search_path(active->file_name,active->file_type,doc->file_name)) continue;
  if(doc->file_name && g_hash_table_contains(seen,doc->file_name)) continue;
  Location *next=g_new0(Location,1); next->path=g_strdup(doc->file_name); next->id=doc->id; g_ptr_array_add(wb.usage_paths,next);
  if(next->path) g_hash_table_add(seen,next->path);
 }
 g_hash_table_destroy(seen); wb.usage_idle=g_idle_add(usage_step,NULL);
}
static void usage_cancel(GtkWidget *widget,gpointer data) { (void)widget; (void)data; stop_usage();if(wb.relation_tree) insight_relation_tree_cancel(wb.relation_tree); set_usage_status("Search cancelled; partial results retained"); }
static void usage_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data; GtkTreeIter iter; GtkTreeModel *model;
 if(!gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 if(wb.search_links && model==GTK_TREE_MODEL(wb.search_matches)) { GtkTreePath *row=gtk_tree_model_get_path(model,&iter);insight_search_links_refresh(wb.search_links,row);gtk_tree_path_free(row); }
 gchar *path,*digest; gulong line,start,end; guint id;
 gtk_tree_model_get(model,&iter,USAGE_PATH,&path,USAGE_LINE,&line,USAGE_START,&start,USAGE_END,&end,USAGE_ID,&id,USAGE_DIGEST,&digest,-1);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
 if(show_location(path,id,line,TRUE)) {
  gchar *text=sci_get_contents(wb.preview,sci_get_length(wb.preview)+1),*actual=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);
  if(!digest) { /* Definitions and members navigate by current tag location. */ }
  else if(!g_strcmp0(actual,digest)) { SSM(wb.preview,SCI_SETSEL,end,start); decorate_match(wb.preview,start,end); SSM(wb.preview,SCI_VERTICALCENTRECARET,0,0); }
  else { decorate_match(wb.preview,0,0); wb.source_line=0; navigation_state(); ui_set_statusbar(TRUE,"Source changed after search; run the usage search again"); }
  g_free(actual); g_free(text);
 }
 g_free(path); g_free(digest);
}
static void usage_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; (void)data;
 if(view==GTK_TREE_VIEW(wb.search_view) && insight_search_links_open(wb.search_links,path)) return;
 GtkTreeSelection *selection=gtk_tree_view_get_selection(view); gtk_tree_selection_select_path(selection,path);
 usage_selected(selection,NULL); if(wb.source_line) open_context(NULL,NULL);
}
static void relation_controls(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; if(wb.relation_setting) return; wb.relation_dirty=TRUE; stop_usage();
 gint relation=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode));
 gtk_widget_set_sensitive(wb.relation_transitive,relation==2 || relation==4 || relation==5);
 relation_view_update();
 gboolean references=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==0;
 gtk_widget_set_sensitive(wb.usage_code,references);gtk_widget_set_sensitive(wb.usage_comments,references); gtk_widget_set_sensitive(wb.usage_buffer,references || gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==3 || gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==7);
 if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_follow))) insight_workbench_refresh();
 else if(*gtk_entry_get_text(GTK_ENTRY(wb.usage_entry))) usage_start(NULL,NULL);
 else { gtk_list_store_clear(wb.matches); set_usage_status("Enter a symbol for Relations"); }
}
/* Disabling tracking freezes the displayed result set. It must not be
 * treated as a query-options change, which clears and starts the query. */
static void relation_follow_changed(GtkToggleButton *button,gpointer data) {
 (void)data;if(wb.relation_setting) return;
 if(gtk_toggle_button_get_active(button)) { wb.relation_dirty=TRUE;insight_workbench_refresh(); }
 else if(wb.usage_mode!=3) stop_usage();
}
static void relation_refresh_command(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;
 /* A panel command never dispatches the unrelated project text search. */
 if(gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==3) {
  set_usage_status("Select a symbol relation mode before refreshing Relations");return;
 }
 usage_start(NULL,NULL);
}
static void relation_update_from_editor(gboolean dirty) {
 if((wb.usage_idle || wb.search_worker) && wb.usage_mode==3) return;
 if(!wb.relation_follow || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_follow))) return;
 GeanyDocument *doc=document_get_current(); gchar word[1025]={0};
 if(doc) {
  refresh_index();
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_enclosing))) {
   TMTag *tag=relation_enclosing_function(doc);if(tag && tag->name) g_strlcpy(word,tag->name,sizeof word);
  } else editor_find_current_word_sciwc(doc->editor,sci_get_current_position(doc->editor->sci),word,sizeof word);
  if(!*word && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_enclosing))) {
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
static gboolean relation_row_visible(GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 (void)data;const gchar *query=gtk_entry_get_text(GTK_ENTRY(wb.relation_filter_entry));if(!*query) return TRUE;
 gboolean match=FALSE;const gint fields[]={USAGE_SYMBOL,USAGE_DISPLAY,USAGE_SNIPPET};
 for(guint i=0;i<G_N_ELEMENTS(fields) && !match;i++) {
  gchar *value=NULL;gtk_tree_model_get(model,iter,fields[i],&value,-1);
  match=insight_lookup_text_match(value,query);g_free(value);
 }
 return match;
}
static void relation_filter_changed(GtkEditable *entry,gpointer data) {
 (void)entry;(void)data;if(wb.relation_filter) gtk_tree_model_filter_refilter(wb.relation_filter);if(wb.relation_tree) insight_relation_tree_filter(wb.relation_tree,gtk_entry_get_text(GTK_ENTRY(wb.relation_filter_entry)));
}
typedef struct { gchar *query,*filter;gint mode,comment_mode,zoom;gboolean buffer,code,transitive,tree,graph,vertical; } RelationVisit;
static void relation_visit_free(gpointer data) { RelationVisit *visit=data;if(!visit) return;g_free(visit->query);g_free(visit->filter);g_free(visit); }
static RelationVisit *relation_visit_current(void) {
 RelationVisit *visit=g_new0(RelationVisit,1);visit->query=g_strdup(gtk_entry_get_text(GTK_ENTRY(wb.usage_entry)));visit->filter=g_strdup(gtk_entry_get_text(GTK_ENTRY(wb.relation_filter_entry)));visit->mode=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode));visit->buffer=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_buffer));visit->code=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_code));visit->comment_mode=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.usage_comments));visit->transitive=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_transitive));visit->tree=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle));visit->graph=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle));
 const gchar *shown=gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));insight_relation_graph_get_options(shown && !strcmp(shown,"treegraph") ? wb.relation_tree_graph : wb.relation_graph,&visit->vertical,&visit->zoom);return visit;
}
static void relation_history_state(void) {
 gtk_widget_set_sensitive(wb.relation_back,wb.relation_history->len>0);gtk_widget_set_sensitive(wb.relation_forward,wb.relation_future->len>0);
}
static void relation_visit_push(GPtrArray *stack,RelationVisit *visit) { if(stack->len>=32) g_ptr_array_remove_index(stack,0);g_ptr_array_add(stack,visit); }
static RelationVisit *relation_visit_pop(GPtrArray *stack) {
 RelationVisit *visit=g_ptr_array_index(stack,stack->len-1);g_ptr_array_index(stack,stack->len-1)=NULL;g_ptr_array_remove_index(stack,stack->len-1);return visit;
}
static void relation_visit_restore(RelationVisit *visit) {
 wb.relation_setting=TRUE;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE);
 gtk_entry_set_text(GTK_ENTRY(wb.usage_entry),visit->query);gtk_entry_set_text(GTK_ENTRY(wb.relation_filter_entry),visit->filter);
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),visit->mode);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_buffer),visit->buffer);gtk_combo_box_set_active(GTK_COMBO_BOX(wb.usage_comments),visit->comment_mode);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_code),visit->code);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_transitive),visit->transitive);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle),visit->tree);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle),visit->graph);
 /* Toggle callbacks can synchronize the two canvases. Restore geometry last. */
 insight_relation_graph_set_options(wb.relation_graph,visit->vertical,visit->zoom);insight_relation_graph_set_options(wb.relation_tree_graph,visit->vertical,visit->zoom);
 wb.relation_setting=FALSE;relation_visit_free(visit);relation_history_state();relation_controls(NULL,NULL);
}
static void relation_travel(gboolean forward,guint distance) {
 GPtrArray *from=forward ? wb.relation_future : wb.relation_history,*to=forward ? wb.relation_history : wb.relation_future;
 if(!distance || distance>from->len) return;
 relation_visit_push(to,relation_visit_current());
 /* Move intervening visits without issuing intermediate asynchronous queries. */
 while(distance-->1) relation_visit_push(to,relation_visit_pop(from));
 relation_visit_restore(relation_visit_pop(from));
}
static void relation_back(GtkWidget *widget,gpointer data) { (void)widget;(void)data;relation_travel(FALSE,1); }
static void relation_forward(GtkWidget *widget,gpointer data) { (void)widget;(void)data;relation_travel(TRUE,1); }
static void relation_history_pick(GtkMenuItem *item,gpointer data) {
 (void)data;relation_travel(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item),"forward")),GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"distance")));
}
static void relation_history_clear(GtkMenuItem *item,gpointer data) {
 (void)item;(void)data;g_ptr_array_set_size(wb.relation_history,0);g_ptr_array_set_size(wb.relation_future,0);relation_history_state();
}
static void relation_history_show(GtkWidget *menu,gpointer data) {
 (void)data;GList *children=gtk_container_get_children(GTK_CONTAINER(menu));for(GList *p=children;p;p=p->next) gtk_widget_destroy(p->data);g_list_free(children);
 GPtrArray *stacks[]={wb.relation_history,wb.relation_future};const gchar *headings[]={"Back","Forward"};
 for(guint side=0;side<2;side++) {
  GtkWidget *heading=gtk_menu_item_new_with_label(headings[side]);gtk_widget_set_sensitive(heading,FALSE);gtk_menu_shell_append(GTK_MENU_SHELL(menu),heading);
  GPtrArray *stack=stacks[side];for(guint i=0;i<stack->len;i++) {
   RelationVisit *visit=g_ptr_array_index(stack,stack->len-1-i);GtkTreeModel *modes=gtk_combo_box_get_model(GTK_COMBO_BOX(wb.relation_mode));GtkTreeIter mode_iter;gchar *method=NULL;
   if(gtk_tree_model_iter_nth_child(modes,&mode_iter,NULL,visit->mode)) gtk_tree_model_get(modes,&mode_iter,0,&method,-1);
   gchar *short_query=g_utf8_substring(visit->query,0,MIN((glong)64,g_utf8_strlen(visit->query,-1))),*label=g_strdup_printf("%u. %s — %s",i+1,short_query,method ? method : "");g_free(short_query);g_free(method);
   GtkWidget *item=gtk_menu_item_new_with_label(label);g_free(label);gchar *name=g_strdup_printf("insight-relation-history-%s-%u",side ? "forward" : "back",i+1);gtk_widget_set_name(item,name);g_free(name);gtk_widget_set_tooltip_text(item,visit->query);g_object_set_data(G_OBJECT(item),"insight-no-i18n",GINT_TO_POINTER(1));g_object_set_data(G_OBJECT(item),"forward",GUINT_TO_POINTER(side));g_object_set_data(G_OBJECT(item),"distance",GUINT_TO_POINTER(i+1));g_signal_connect(item,"activate",G_CALLBACK(relation_history_pick),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
  }
 }
 gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());GtkWidget *clear=gtk_menu_item_new_with_label("Clear relation history");gtk_widget_set_name(clear,"insight-relation-history-clear");gtk_widget_set_sensitive(clear,wb.relation_history->len || wb.relation_future->len);g_signal_connect(clear,"activate",G_CALLBACK(relation_history_clear),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),clear);insight_i18n_apply(menu);gtk_widget_show_all(menu);
}
static void relation_selected_query(GtkWidget *widget,gpointer data) {
 (void)widget;GtkTreeModel *model;GtkTreeIter iter;
 gboolean tree_view=g_str_has_prefix(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)),"tree");GtkTreeView *view=tree_view ? insight_relation_tree_view(wb.relation_tree) : GTK_TREE_VIEW(wb.usage_view);
 if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view),&model,&iter)) { ui_set_statusbar(TRUE,"Select a relation row first");return; }
 gchar *name=NULL;gulong line=0;gtk_tree_model_get(model,&iter,USAGE_SYMBOL,&name,USAGE_LINE,&line,-1);if(tree_view && !line) { g_free(name);return; }
 if(!name || !*name) { g_free(name);return; }
 relation_visit_push(wb.relation_history,relation_visit_current());g_ptr_array_set_size(wb.relation_future,0);relation_history_state();
 wb.relation_setting=TRUE;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE);
 gtk_entry_set_text(GTK_ENTRY(wb.relation_filter_entry),"");gtk_entry_set_text(GTK_ENTRY(wb.usage_entry),name);
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),GPOINTER_TO_INT(data));wb.relation_setting=FALSE;
 g_free(name);relation_controls(NULL,NULL);
}
static gboolean relation_source_selected(GtkTreeView *view) {
 GtkTreeModel *model;GtkTreeIter iter;gulong line=0;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view),&model,&iter)) return FALSE;gtk_tree_model_get(model,&iter,USAGE_LINE,&line,-1);return line>0;
}
static void relation_node_action(gboolean restore,gpointer data) {
 (void)data;if(restore) { usage_start(NULL,NULL);return; }
 const gchar *shown=gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));gboolean tree=shown && g_str_has_prefix(shown,"tree");GtkTreeView *view=tree ? insight_relation_tree_view(wb.relation_tree) : GTK_TREE_VIEW(wb.usage_view);
 if(!relation_source_selected(view)) { set_usage_status("Select a source node; query roots and notices cannot be removed");return; }
 wb.relation_setting=TRUE;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE);wb.relation_setting=FALSE;stop_usage();
 gboolean removed=FALSE;
 if(tree) removed=insight_relation_tree_remove_selected(wb.relation_tree);
 else {
  GtkTreeModel *model;GtkTreeIter iter;if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view),&model,&iter)) {
   /* Selection coordinates belong to the visible sorted/filtered model. */
   while(GTK_IS_TREE_MODEL_SORT(model) || GTK_IS_TREE_MODEL_FILTER(model)) { GtkTreeIter child;
    if(GTK_IS_TREE_MODEL_SORT(model)) { gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(model),&child,&iter);model=gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(model)); }
    else { gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model),&child,&iter);model=gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model)); }iter=child;
   }
   if(model==GTK_TREE_MODEL(wb.matches)) { gtk_list_store_remove(wb.matches,&iter);removed=TRUE; }
  }
 }
 if(removed) set_usage_status("Node removed from view; refresh to restore");
}
static void relation_node_button(GtkWidget *widget,gpointer data) { (void)widget;relation_node_action(GPOINTER_TO_INT(data),NULL); }
static void relation_node_menu_free(gpointer data) { gtk_widget_destroy(GTK_WIDGET(data));g_object_unref(data); }
static GtkWidget *relation_node_menu(GtkTreeView *view) {
 GtkWidget *menu=g_object_get_data(G_OBJECT(view),"insight-node-menu");
 if(!menu) {
  menu=gtk_menu_new();g_object_ref_sink(menu);const gchar *labels[]={"Remove node from view","Restore nodes (refresh)"};
  for(guint i=0;i<2;i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(item,i ? "insight-relation-node-restore-menu" : "insight-relation-node-remove-menu");gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(relation_node_button),GUINT_TO_POINTER(i)); }
  const gchar *reports[]={"View Relation Outline…","Create Bookmarks from Relation Items…"};for(guint i=0;i<2;i++) { GtkWidget *item=gtk_menu_item_new_with_label(reports[i]);gtk_widget_set_name(item,i ? "insight-relation-bookmark-menu":"insight-relation-outline-menu");gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(relation_report_show),GUINT_TO_POINTER(i)); }
  GtkWidget *print=gtk_menu_item_new_with_label("Print Relation Window…");gtk_widget_set_name(print,"insight-relation-print-menu");gtk_menu_shell_append(GTK_MENU_SHELL(menu),print);g_signal_connect(print,"activate",G_CALLBACK(relation_print_show),NULL);
  insight_list_tools_menu(view,menu);g_object_set_data_full(G_OBJECT(view),"insight-node-menu",menu,relation_node_menu_free);
 }
 GList *items=gtk_container_get_children(GTK_CONTAINER(menu));gtk_widget_set_sensitive(items->data,relation_source_selected(view));g_list_free(items);insight_i18n_apply(menu);gtk_widget_show_all(menu);return menu;
}
static gboolean relation_node_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)data;if(!(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SHIFT_MASK)) && (event->keyval==GDK_KEY_Delete || event->keyval==GDK_KEY_KP_Delete || event->keyval==GDK_KEY_F5)) { relation_node_action(event->keyval==GDK_KEY_F5,NULL);return TRUE; }
 if(event->keyval==GDK_KEY_Menu || (event->keyval==GDK_KEY_F10 && (event->state&GDK_SHIFT_MASK))) { gtk_menu_popup_at_widget(GTK_MENU(relation_node_menu(GTK_TREE_VIEW(widget))),widget,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,(GdkEvent *)event);return TRUE; }return FALSE;
}
static gboolean relation_node_popup(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)data;if(event->type!=GDK_BUTTON_PRESS || event->button!=3) return FALSE;GtkTreePath *path=NULL;
 if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),(gint)event->x,(gint)event->y,&path,NULL,NULL,NULL)) { gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget),path,NULL,FALSE);gtk_tree_path_free(path); }else gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)));
 gtk_widget_grab_focus(widget);gtk_menu_popup_at_pointer(GTK_MENU(relation_node_menu(GTK_TREE_VIEW(widget))),(GdkEvent *)event);return TRUE;
}
static void relation_graph_query(gint mode,gpointer data) {
 (void)data;relation_selected_query(NULL,GINT_TO_POINTER(mode));
}
static void relation_tree_selected(GtkTreeSelection *selection,gpointer data) {
 GtkTreeModel *model;GtkTreeIter iter;gulong line=0;if(gtk_tree_selection_get_selected(selection,&model,&iter)) gtk_tree_model_get(model,&iter,USAGE_LINE,&line,-1);if(line) usage_selected(selection,data);
}
static void relation_tree_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 GtkTreeModel *model=gtk_tree_view_get_model(view);GtkTreeIter iter;gulong line=0;if(!gtk_tree_model_get_iter(model,&iter,path)) return;gtk_tree_model_get(model,&iter,USAGE_LINE,&line,-1);
 if(line) usage_activated(view,path,column,data);else if(gtk_tree_view_row_expanded(view,path)) gtk_tree_view_collapse_row(view,path);else gtk_tree_view_expand_row(view,path,FALSE);
}
static void relation_view_update(void) {
 if(!wb.relation_tree_toggle || !wb.relation_stack) return;
 gint mode=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode));gboolean supported=mode==2 || mode==4 || mode==5;
 gtk_widget_set_sensitive(wb.relation_tree_toggle,supported);gboolean tree=supported && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle));
 const gchar *old=gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));gboolean graph=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle));
 if(old && wb.relation_tree_graph && (!strcmp(old,"treegraph") || !strcmp(old,"graph"))) {
  gboolean vertical=FALSE;gint zoom=100;gboolean old_tree=!strcmp(old,"treegraph");insight_relation_graph_get_options(old_tree ? wb.relation_tree_graph : wb.relation_graph,&vertical,&zoom);insight_relation_graph_set_options(old_tree ? wb.relation_graph : wb.relation_tree_graph,vertical,zoom);
 }
 gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),tree ? (graph ? "treegraph" : "tree") : (graph ? "graph" : "list"));
}
/* Presentation changes deliberately do not dispatch usage_start: keep the
 * current query, loaded branches, filter and selection intact. */
static void relation_next_view(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(!wb.relation_stack || !wb.relation_graph_toggle) return;
 gboolean graph=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle));
 gboolean tree=!g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)),"treegraph");
 gboolean vertical=FALSE;gint zoom=100;
 insight_relation_graph_get_options(tree ? wb.relation_tree_graph : wb.relation_graph,&vertical,&zoom);
 if(!graph) {
  /* Read the displayed outline's graph settings before switching. */
  gboolean outline_tree=!g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)),"tree");
  insight_relation_graph_get_options(outline_tree ? wb.relation_tree_graph : wb.relation_graph,NULL,&zoom);
  insight_relation_graph_set_options(wb.relation_graph,FALSE,zoom);
  insight_relation_graph_set_options(wb.relation_tree_graph,FALSE,zoom);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle),TRUE);
 } else if(!vertical) {
  insight_relation_graph_set_options(wb.relation_graph,TRUE,zoom);
  insight_relation_graph_set_options(wb.relation_tree_graph,TRUE,zoom);
 } else gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle),FALSE);
}
static void relation_select_view(GtkWidget *widget,gpointer data) {
 (void)widget;gint mode=GPOINTER_TO_INT(data);if(!wb.relation_stack) return;
 const gchar *shown=gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));
 gboolean tree=shown && g_str_has_prefix(shown,"tree");gint zoom=100;
 insight_relation_graph_get_options(tree ? wb.relation_tree_graph:wb.relation_graph,NULL,&zoom);
 insight_relation_graph_set_options(wb.relation_graph,mode==2,zoom);
 insight_relation_graph_set_options(wb.relation_tree_graph,mode==2,zoom);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle),mode!=0);
}
static void window_lock_item(GtkWidget *menu,GtkWidget *follow,const gchar *label,const gchar *name) {
 GtkWidget *item=gtk_check_menu_item_new_with_label(label);gtk_widget_set_name(item,name);
 /* One authoritative state; selection previews and saved preferences update
  * the menu too. Bindings disconnect automatically when either side dies. */
 g_object_bind_property(follow,"active",item,"active",G_BINDING_BIDIRECTIONAL|G_BINDING_SYNC_CREATE|G_BINDING_INVERT_BOOLEAN);
 gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
}
static void relation_graph_toggle(GtkToggleButton *button,gpointer data) { (void)button;(void)data;relation_view_update(); }
static void relation_tree_toggle(GtkToggleButton *button,gpointer data) { (void)data;relation_view_update();if(!wb.relation_setting && gtk_toggle_button_get_active(button) && *gtk_entry_get_text(GTK_ENTRY(wb.usage_entry))) usage_start(NULL,NULL); }
static void reference_region_changed(GtkWidget *widget,gpointer data) {
 (void)data;if(wb.reference_setting) return;wb.reference_setting=TRUE;
 if(widget==wb.usage_code && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_code))) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.usage_comments),0);
 else if(widget==wb.usage_comments && gtk_combo_box_get_active(GTK_COMBO_BOX(wb.usage_comments))>0) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_code),FALSE);
 wb.reference_setting=FALSE;relation_controls(NULL,NULL);
}
static void create_usage_browser(void) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4),*controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);
 wb.usage_entry=gtk_search_entry_new(); gtk_widget_set_name(wb.usage_entry,"insight-relation-query"); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.usage_entry),"Symbol or qualified owner…");
 wb.usage_status=gtk_label_new("Comments and strings included; no semantic scope resolution"); gtk_widget_set_name(wb.usage_status,"insight-relation-status"); gtk_label_set_line_wrap(GTK_LABEL(wb.usage_status),TRUE);
 wb.usage_buffer=gtk_check_button_new_with_label("Current buffer only");
 wb.usage_code=gtk_check_button_new_with_label("References: code only");gtk_widget_set_name(wb.usage_code,"insight-references-code-only");
 wb.usage_comments=gtk_combo_box_text_new();gtk_widget_set_name(wb.usage_comments,"insight-references-comments");
 gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.usage_comments),"References: all text");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.usage_comments),"References: skip comments");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.usage_comments),"References: comments only");gtk_combo_box_set_active(GTK_COMBO_BOX(wb.usage_comments),0);
 gtk_widget_set_tooltip_text(wb.usage_comments,"Lexical comment regions; skip comments retains strings, code only excludes both. Unknown languages are skipped in filtered modes.");
 wb.relation_follow=gtk_check_button_new_with_label("Follow symbol in editor");gtk_widget_set_name(wb.relation_follow,"insight-relations-follow");
 wb.relation_enclosing=gtk_check_button_new_with_label("Track enclosing function");
 wb.relation_transitive=gtk_check_button_new_with_label("Include indirect inheritance (tags)");gtk_widget_set_name(wb.relation_transitive,"insight-relation-transitive");gtk_widget_set_sensitive(wb.relation_transitive,FALSE);
 wb.relation_mode=gtk_combo_box_text_new(); gtk_widget_set_name(wb.relation_mode,"insight-relation-mode");
 const char *modes[]={"References (lexical)","Definitions / declarations","Direct members (tags)","Text search (project)","Base types (tags)","Derived types (tags)","Calls (current function, lexical)","Call sites (project, lexical)"};
 for(guint i=0;i<G_N_ELEMENTS(modes);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.relation_mode),modes[i]);
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),0);
 GtkWidget *search=gtk_button_new_with_label("Search"),*cancel=gtk_button_new_with_label("Stop");
 gtk_box_pack_start(GTK_BOX(controls),search,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(controls),cancel,FALSE,FALSE,0);
 const gchar *report_icons[]={"view-list-symbolic","bookmark-new-symbolic"},*report_tips[]={"View Relation Outline…","Create Bookmarks from Relation Items…"};for(guint i=0;i<2;i++) { GtkWidget *button=gtk_button_new_from_icon_name(report_icons[i],GTK_ICON_SIZE_MENU);gtk_widget_set_name(button,i ? "insight-relation-bookmarks":"insight-relation-outline");gtk_widget_set_tooltip_text(button,report_tips[i]);gtk_box_pack_start(GTK_BOX(controls),button,FALSE,FALSE,0);g_signal_connect(button,"clicked",G_CALLBACK(relation_report_show),GUINT_TO_POINTER(i)); }

 wb.matches=gtk_list_store_new(USAGE_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG);
 wb.relation_filter_entry=gtk_search_entry_new();gtk_widget_set_tooltip_text(wb.relation_filter_entry,"Prefix with ~ for fuzzy subsequence matching");gtk_widget_set_name(wb.relation_filter_entry,"insight-relation-filter");gtk_entry_set_placeholder_text(GTK_ENTRY(wb.relation_filter_entry),"Filter results: symbol, file or source…");
 GtkTreeModel *filtered=gtk_tree_model_filter_new(GTK_TREE_MODEL(wb.matches),NULL);wb.relation_filter=GTK_TREE_MODEL_FILTER(filtered);
 gtk_tree_model_filter_set_visible_func(wb.relation_filter,relation_row_visible,NULL,NULL);
 GtkTreeModel *sorted=gtk_tree_model_sort_new_with_model(filtered);
 wb.usage_view=gtk_tree_view_new_with_model(sorted);g_object_unref(sorted);g_object_unref(filtered); gtk_widget_set_name(wb.usage_view,"insight-relations");
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
 GtkWidget *drill=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2),*definitions=gtk_button_new_with_label("Definitions"),*sites=gtk_button_new_with_label("Call sites");
 gtk_widget_set_name(definitions,"insight-relation-definitions");
 gtk_widget_set_tooltip_text(definitions,"Show definitions for the selected result; choose among overloads");gtk_widget_set_tooltip_text(sites,"Search call sites for the selected result");
 wb.relation_history=g_ptr_array_new_with_free_func(relation_visit_free);wb.relation_future=g_ptr_array_new_with_free_func(relation_visit_free);wb.relation_back=gtk_button_new_from_icon_name("go-previous",GTK_ICON_SIZE_BUTTON);gtk_widget_set_name(wb.relation_back,"insight-relation-back");gtk_widget_set_tooltip_text(wb.relation_back,"Back to the previous relation query");gtk_widget_set_sensitive(wb.relation_back,FALSE);g_signal_connect(wb.relation_back,"clicked",G_CALLBACK(relation_back),NULL);gtk_box_pack_start(GTK_BOX(drill),wb.relation_back,FALSE,FALSE,0);
 wb.relation_forward=gtk_button_new_from_icon_name("go-next",GTK_ICON_SIZE_BUTTON);gtk_widget_set_name(wb.relation_forward,"insight-relation-forward");gtk_widget_set_tooltip_text(wb.relation_forward,"Forward to the next relation query");gtk_widget_set_sensitive(wb.relation_forward,FALSE);g_signal_connect(wb.relation_forward,"clicked",G_CALLBACK(relation_forward),NULL);gtk_box_pack_start(GTK_BOX(drill),wb.relation_forward,FALSE,FALSE,0);
 GtkWidget *history=gtk_menu_button_new(),*history_menu=gtk_menu_new();gtk_widget_set_name(history,"insight-relation-history");gtk_button_set_image(GTK_BUTTON(history),gtk_image_new_from_icon_name("document-open-recent",GTK_ICON_SIZE_BUTTON));gtk_widget_set_tooltip_text(history,"Relation query history");gtk_menu_button_set_popup(GTK_MENU_BUTTON(history),history_menu);g_signal_connect(history_menu,"show",G_CALLBACK(relation_history_show),NULL);gtk_box_pack_start(GTK_BOX(drill),history,FALSE,FALSE,0);
 const gchar *node_icons[]={"list-remove","view-refresh"},*node_tips[]={"Remove selected node from view (Delete)","Restore nodes by refreshing the query (F5)"},*node_names[]={"insight-relation-node-remove","insight-relation-node-restore"};
 for(guint i=0;i<2;i++) { GtkWidget *button=gtk_button_new_from_icon_name(node_icons[i],GTK_ICON_SIZE_BUTTON);gtk_widget_set_name(button,node_names[i]);gtk_widget_set_tooltip_text(button,node_tips[i]);gtk_box_pack_start(GTK_BOX(drill),button,FALSE,FALSE,0);g_signal_connect(button,"clicked",G_CALLBACK(relation_node_button),GUINT_TO_POINTER(i)); }
 gtk_box_pack_start(GTK_BOX(drill),definitions,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(drill),sites,TRUE,TRUE,0);
 g_signal_connect(definitions,"clicked",G_CALLBACK(relation_selected_query),GINT_TO_POINTER(1));g_signal_connect(sites,"clicked",G_CALLBACK(relation_selected_query),GINT_TO_POINTER(7));
 g_signal_connect(wb.relation_filter_entry,"changed",G_CALLBACK(relation_filter_changed),NULL);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.usage_view);
 wb.relation_stack=gtk_stack_new();gtk_stack_add_named(GTK_STACK(wb.relation_stack),scroll,"list");
 wb.relation_tree=insight_relation_tree_new();gtk_stack_add_named(GTK_STACK(wb.relation_stack),wb.relation_tree,"tree");
 GtkTreeView *tree_view=insight_relation_tree_view(wb.relation_tree);g_signal_connect(gtk_tree_view_get_selection(tree_view),"changed",G_CALLBACK(relation_tree_selected),NULL);g_signal_connect(tree_view,"row-activated",G_CALLBACK(relation_tree_activated),NULL);
 wb.relation_tree_toggle=gtk_check_button_new_with_label("Expandable relation tree");gtk_widget_set_name(wb.relation_tree_toggle,"insight-relation-tree-toggle");gtk_widget_set_sensitive(wb.relation_tree_toggle,FALSE);gtk_box_pack_start(GTK_BOX(controls),wb.relation_tree_toggle,FALSE,FALSE,0);g_signal_connect(wb.relation_tree_toggle,"toggled",G_CALLBACK(relation_tree_toggle),NULL);
 wb.relation_graph=insight_relation_graph_new(GTK_TREE_VIEW(wb.usage_view),GTK_ENTRY(wb.usage_entry),USAGE_SYMBOL,USAGE_DISPLAY,USAGE_LINE);insight_relation_graph_set_query_handler(wb.relation_graph,relation_graph_query,NULL);insight_relation_graph_set_node_handler(wb.relation_graph,relation_node_action,NULL);gtk_stack_add_named(GTK_STACK(wb.relation_stack),wb.relation_graph,"graph");gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),"list");
 wb.relation_tree_graph=insight_relation_graph_new_tree(tree_view,GTK_ENTRY(wb.usage_entry),USAGE_SYMBOL,USAGE_DISPLAY,USAGE_LINE);insight_relation_graph_set_query_handler(wb.relation_tree_graph,relation_graph_query,NULL);insight_relation_graph_set_node_handler(wb.relation_tree_graph,relation_node_action,NULL);gtk_stack_add_named(GTK_STACK(wb.relation_stack),wb.relation_tree_graph,"treegraph");
 wb.relation_graph_toggle=gtk_check_button_new_with_label("Graph view");gtk_widget_set_name(wb.relation_graph_toggle,"insight-relation-graph-toggle");gtk_box_pack_start(GTK_BOX(controls),wb.relation_graph_toggle,FALSE,FALSE,0);g_signal_connect(wb.relation_graph_toggle,"toggled",G_CALLBACK(relation_graph_toggle),NULL);
 GtkWidget *next_view=gtk_button_new_from_icon_name("view-refresh-symbolic",GTK_ICON_SIZE_MENU);gtk_widget_set_name(next_view,"insight-relation-next-view");gtk_widget_set_tooltip_text(next_view,"Next Relation Window View");gtk_box_pack_start(GTK_BOX(controls),next_view,FALSE,FALSE,0);g_signal_connect(next_view,"clicked",G_CALLBACK(relation_next_view),NULL);
 gtk_box_pack_start(GTK_BOX(box),wb.usage_entry,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.relation_mode,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.relation_transitive,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.relation_follow,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.relation_enclosing,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.usage_code,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),wb.usage_comments,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.usage_buffer,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),controls,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.usage_status,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.relation_filter_entry,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),drill,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.relation_stack,TRUE,TRUE,0);
 gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),box,gtk_label_new("Relations"));
 g_signal_connect(search,"clicked",G_CALLBACK(usage_start),NULL); g_signal_connect(cancel,"clicked",G_CALLBACK(usage_cancel),NULL); g_signal_connect(wb.usage_entry,"activate",G_CALLBACK(usage_start),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.usage_view)),"changed",G_CALLBACK(usage_selected),NULL); g_signal_connect(wb.usage_view,"row-activated",G_CALLBACK(usage_activated),NULL);
 g_signal_connect(wb.usage_entry,"key-press-event",G_CALLBACK(browser_key),NULL);
 g_signal_connect(wb.usage_view,"key-press-event",G_CALLBACK(relation_node_key),NULL);g_signal_connect(wb.usage_view,"key-press-event",G_CALLBACK(browser_key),NULL);g_signal_connect(wb.usage_view,"button-press-event",G_CALLBACK(relation_node_popup),NULL);g_signal_connect(tree_view,"key-press-event",G_CALLBACK(relation_node_key),NULL);g_signal_connect(tree_view,"button-press-event",G_CALLBACK(relation_node_popup),NULL);
 g_signal_connect(wb.relation_transitive,"toggled",G_CALLBACK(relation_controls),NULL);
 g_signal_connect(wb.relation_mode,"changed",G_CALLBACK(relation_controls),NULL);
 g_signal_connect(wb.relation_follow,"toggled",G_CALLBACK(relation_follow_changed),NULL);
 g_signal_connect(wb.relation_enclosing,"toggled",G_CALLBACK(relation_controls),NULL);
 g_signal_connect(wb.usage_code,"toggled",G_CALLBACK(reference_region_changed),NULL);g_signal_connect(wb.usage_comments,"changed",G_CALLBACK(reference_region_changed),NULL);
 g_signal_connect(wb.usage_buffer,"toggled",G_CALLBACK(relation_controls),NULL);
 gtk_widget_show_all(box);
}
static void browser_symbol_info(GtkWidget *widget,gpointer data) {
 (void)widget;GtkTreeView *view=GTK_TREE_VIEW(data);GtkTreeModel *model;GtkTreeIter iter;
 if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view),&model,&iter)) { ui_set_statusbar(TRUE,"Select a symbol first");return; }
 gchar *name=NULL,*path=NULL;gulong line=0;gtk_tree_model_get(model,&iter,SYMBOL_NAME,&name,SYMBOL_PATH,&path,SYMBOL_LINE,&line,-1);
 if(!line) { g_free(name); g_free(path); ui_set_statusbar(TRUE,"Select a symbol, not a type group"); return; }
 if(GTK_WIDGET(view)==wb.local_view && (!path || !strcmp(path,"@untitled"))) { g_free(path);path=g_strdup_printf("@buffer:%u",wb.local_doc_id); }
 if(name) info_show_query(name,path,line);
 g_free(name);g_free(path);
}
typedef struct { const TMTag *tag; GtkTreePath *path; } SymbolLocate;
static gboolean symbol_locate_row(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
 SymbolLocate *target=data;gchar *name=NULL;gulong line=0;gtk_tree_model_get(model,iter,SYMBOL_NAME,&name,SYMBOL_LINE,&line,-1);
 gboolean match=line==target->tag->line && !g_strcmp0(name,target->tag->name);g_free(name);if(match) target->path=gtk_tree_path_copy(path);return match;
}
static void symbols_locate(GtkWidget *button,gpointer data) {
 (void)button;(void)data;GeanyDocument *doc=document_get_current();
 if(!doc || !wb.local_view || !gtk_widget_get_sensitive(wb.local_view) || doc->id!=wb.local_doc_id) { ui_set_statusbar(TRUE,"File symbols are still updating");return; }
 TMTag *tag=relation_enclosing_function(doc);
 if(!tag && doc->tm_file && doc->tm_file->tags_array) {
  gulong line=(gulong)sci_get_current_line(doc->editor->sci)+1;
  for(guint i=0;i<doc->tm_file->tags_array->len;i++) { TMTag *candidate=g_ptr_array_index(doc->tm_file->tags_array,i);if(candidate && candidate->line && candidate->line<=line && (!tag || candidate->line>tag->line)) tag=candidate; }
 }
 if(!tag) { ui_set_statusbar(TRUE,"No indexed declaration at or before the caret");return; }
 SymbolLocate target={.tag=tag};gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(wb.local_view)),symbol_locate_row,&target);
 if(!target.path) { ui_set_statusbar(TRUE,"The caret symbol is outside the loaded outline or hidden by a filter");return; }
 GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.local_view));guint signal=g_signal_lookup("changed",GTK_TYPE_TREE_SELECTION);
 g_signal_handlers_block_matched(selection,G_SIGNAL_MATCH_ID,signal,0,NULL,NULL,NULL);
 gtk_tree_view_expand_to_path(GTK_TREE_VIEW(wb.local_view),target.path);gtk_tree_view_set_cursor(GTK_TREE_VIEW(wb.local_view),target.path,NULL,FALSE);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(wb.local_view),target.path,NULL,TRUE,0.5,0);
 g_signal_handlers_unblock_matched(selection,G_SIGNAL_MATCH_ID,signal,0,NULL,NULL,NULL);gtk_tree_path_free(target.path);
}
static void symbols_load_more(GtkWidget *button,gpointer data) {
 (void)button;GtkWidget *view=data;if(!gtk_widget_get_sensitive(view)) return;
 if(view==wb.local_view) { wb.local_symbol_limit=MIN(250000u,wb.local_symbol_limit+2000u);local_filter(NULL,NULL); }
 else if(view==wb.symbol_view) { wb.project_symbol_limit=MIN(250000u,wb.project_symbol_limit+500u);browser_filter(NULL,NULL); }
}
static void symbol_action(GtkTreeView *view,guint action) {
 if(action!=SP_HELP && !gtk_widget_get_sensitive(GTK_WIDGET(view))) return;
 GtkTreeModel *model=gtk_tree_view_get_model(view); GtkTreeIter iter;
 gboolean selected=gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view),NULL,&iter);
 gchar *name=NULL,*path=NULL,*scope=NULL; gulong line=0;
 if(selected) gtk_tree_model_get(model,&iter,SYMBOL_NAME,&name,SYMBOL_PATH,&path,SYMBOL_SCOPE,&scope,SYMBOL_LINE,&line,-1);
 switch(action) {
 case SP_COPY_TEXT: case SP_CUT_TEXT: case SP_DUPLICATE: case SP_PASTE_TEXT: symbol_text_from_outline(view,action-SP_COPY_TEXT);break;
 case SP_OPEN: if(selected) { GtkTreePath *row=gtk_tree_model_get_path(model,&iter); browser_activated(view,row,NULL,NULL); gtk_tree_path_free(row); } break;
 case SP_INFO: if(line) browser_symbol_info(NULL,view); break;
 case SP_REFERENCES:
  if(line && name) { wb.relation_setting=TRUE; gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_buffer),FALSE); gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),0); gtk_entry_set_text(GTK_ENTRY(wb.usage_entry),name); gtk_entry_set_text(GTK_ENTRY(wb.relation_filter_entry),""); wb.relation_setting=FALSE; relation_controls(NULL,NULL); focus_widget_panel(wb.usage_entry); } break;
 case SP_QUALIFIED:
  if(line && name) { gchar *native=path && path[0]!='@' ? utils_get_locale_from_utf8(path) : NULL;GeanyDocument *owner=native ? document_find_by_filename(native) : document_get_current();g_free(native);GeanyFiletype *ft=owner ? owner->file_type : (path ? filetypes_detect_from_extension(path) : NULL);const gchar *separator=owner && owner->tm_file ? tm_parser_scope_separator_printable(owner->tm_file->lang) : (ft ? tm_parser_scope_separator_printable(ft->lang) : NULL);gchar *qualified=scope && *scope ? g_strconcat(scope,separator && *separator ? separator : "::",name,NULL) : g_strdup(name);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),qualified,-1);g_free(qualified); }break;
 case SP_PATH: if(line && path && path[0]!='@') gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),path,-1);break;
 case SP_LINE: if(line) { gchar *number=g_strdup_printf("%lu",line);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),number,-1);g_free(number); }break;
 case SP_FILTER_NAME: case SP_FILTER_SCOPE: case SP_FILTER_FILE:
  if(line) { const gchar *value=action==SP_FILTER_NAME ? name : (action==SP_FILTER_SCOPE ? scope : path);if(value && (action!=SP_FILTER_FILE || value[0]!='@')) { gchar *query=g_strdup_printf("%s\"%s\"",action==SP_FILTER_SCOPE ? "scope:" : (action==SP_FILTER_FILE ? "file:" : ""),value);if(strlen(query)<=1024) { GtkWidget *entry=GTK_WIDGET(view)==wb.local_view ? wb.local_entry : wb.symbol_entry;gtk_entry_set_text(GTK_ENTRY(entry),query);gtk_widget_grab_focus(entry); }else ui_set_statusbar(TRUE,"Selected value is too long for a lookup filter");g_free(query); } }break;
 case SP_CLEAR: { GtkWidget *entry=GTK_WIDGET(view)==wb.local_view ? wb.local_entry : wb.symbol_entry;gtk_entry_set_text(GTK_ENTRY(entry),"");gtk_widget_grab_focus(entry); }break;
 case SP_EXPAND_ONE: case SP_COLLAPSE_ONE:
  if(selected && insight_symbol_view_grouped(view)) { GtkTreePath *row=gtk_tree_model_get_path(model,&iter);while(gtk_tree_path_get_depth(row)>1) gtk_tree_path_up(row);if(action==SP_EXPAND_ONE) gtk_tree_view_expand_row(view,row,TRUE);else gtk_tree_view_collapse_row(view,row);gtk_tree_path_free(row); }break;
 case SP_INVERT: insight_symbol_view_set_types(view,~insight_symbol_view_types_mask(view));break;
 case SP_NAME: if(name) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),name,-1); break;
 case SP_LOCATION: if(line && path && path[0]!='@') { gchar *location=g_strdup_printf("%s:%lu",path,line); gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),location,-1); g_free(location); } break;
 case SP_ONLY: case SP_HIDE: insight_symbol_view_filter_selected(view,action==SP_ONLY); break;
 case SP_ALL: insight_symbol_view_set_types(view,G_MAXUINT); break;
 case SP_EXPAND: gtk_tree_view_expand_all(view); break;
 case SP_COLLAPSE: gtk_tree_view_collapse_all(view); break;
 case SP_MORE: insight_symbol_view_load_more(view); break;
 case SP_COLUMNS: insight_symbol_view_columns_reset(view);break;
 case SP_LOCATE: if(GTK_WIDGET(view)==wb.local_view) symbols_locate(NULL,NULL); break;
 case SP_HELP: insight_help_show(NULL,GTK_WIDGET(view)==wb.local_view ? "panel-file-symbols.md" : "panel-project-symbols.md"); break;
 default: break;
 }
 g_free(name); g_free(path);g_free(scope);
}
static void symbol_menu_action(GtkMenuItem *item,gpointer data) { guint action=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"insight-symbol-action")); if(action==SP_GROUP) insight_symbol_view_set_grouped(GTK_TREE_VIEW(data),gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))); else symbol_action(GTK_TREE_VIEW(data),action); }
static GtkWidget *symbol_menu(GtkTreeView *view) {
 GtkWidget *menu=g_object_get_data(G_OBJECT(view),"insight-symbol-popup");
 if(!menu) {
  menu=gtk_menu_new(); gtk_widget_set_name(menu,"insight-symbol-popup"); gtk_menu_attach_to_widget(GTK_MENU(menu),GTK_WIDGET(view),NULL); g_object_set_data_full(G_OBJECT(view),"insight-symbol-popup",g_object_ref_sink(menu),g_object_unref);
  const gchar *labels[]={"Open definition","Symbol Info…","Find references to this symbol","Copy symbol name","Copy source location","Show only this category","Hide this category","Show all symbol types","Group by symbol type","Expand all symbol groups","Collapse all symbol groups","Help for this panel","Load more symbols","Locate symbol at caret","Reset symbol columns","Copy qualified symbol name","Copy source file path","Copy source line number","Filter by this symbol name","Filter by this scope","Filter by this source file","Clear symbol query","Expand selected symbol group","Collapse selected symbol group","Invert symbol type selection","Visible symbol columns","Copy Symbol","Cut Symbol","Duplicate Symbol","Paste Symbol…"};
  const gchar *icons[]={"go-jump","dialog-information","relations","edit-copy","edit-copy","view-filter-symbolic","view-filter-symbolic","view-list-symbolic","view-list-symbolic","list-add","list-remove","panel-help","list-add-symbolic","find-location-symbolic","view-refresh","edit-copy","edit-copy","edit-copy","edit-find","view-filter-symbolic","view-filter-symbolic","edit-clear","list-add","list-remove","object-flip-horizontal","view-list-symbolic","edit-copy","edit-cut","edit-copy","edit-paste"};
  for(guint i=0;i<SP_COUNT;i++) {
   if(i==SP_ONLY || i==SP_GROUP || i==SP_HELP) gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());
   GtkWidget *item;
   if(i==SP_GROUP) { item=gtk_check_menu_item_new_with_label(labels[i]); GtkWidget *label=gtk_bin_get_child(GTK_BIN(item)),*box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); g_object_ref(label); gtk_container_remove(GTK_CONTAINER(item),label); gtk_box_pack_start(GTK_BOX(box),insight_command_icon(icons[i]),FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),label,TRUE,TRUE,0); g_object_unref(label); gtk_container_add(GTK_CONTAINER(item),box); g_object_set_data(G_OBJECT(item),"insight-icon-added",GINT_TO_POINTER(1)); }
   else if(i==SP_VISIBILITY) { item=gtk_image_menu_item_new_with_label(labels[i]);gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),insight_command_icon(icons[i]));gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item),TRUE);gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),insight_symbol_view_columns_menu(view)); }
   else { item=gtk_image_menu_item_new_with_label(labels[i]); gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),insight_command_icon(icons[i])); gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item),TRUE); }
   gchar *key=g_strdup_printf("insight-symbol-command-%u",i); gtk_widget_set_name(item,key); g_object_set_data(G_OBJECT(menu),key,item); g_free(key); g_object_set_data(G_OBJECT(item),"insight-symbol-action",GUINT_TO_POINTER(i)); g_signal_connect_object(item,i==SP_GROUP ? "toggled" : "activate",G_CALLBACK(symbol_menu_action),view,0); if(i>=SP_MORE) { GList *children=gtk_container_get_children(GTK_CONTAINER(menu));gint position=(gint)g_list_length(children)-2;g_list_free(children);gtk_menu_shell_insert(GTK_MENU_SHELL(menu),item,position); }else gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
  }
 }
 insight_list_tools_menu(view,menu);
 GtkTreeModel *model=gtk_tree_view_get_model(view); GtkTreeIter iter; gboolean selected=gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view),NULL,&iter); gchar *path=NULL;gulong line=0;
 if(selected) gtk_tree_model_get(model,&iter,SYMBOL_PATH,&path,SYMBOL_LINE,&line,-1);
 gboolean ready=gtk_widget_get_sensitive(GTK_WIDGET(view)),grouped=insight_symbol_view_grouped(view);
 for(guint i=0;i<SP_COUNT;i++) { gchar *key=g_strdup_printf("insight-symbol-command-%u",i); GtkWidget *item=g_object_get_data(G_OBJECT(menu),key);g_free(key); gboolean enabled=ready;
  if(i==SP_OPEN || i==SP_INFO || i==SP_REFERENCES) enabled &= line!=0;
  if(i==SP_NAME || i==SP_ONLY || i==SP_HIDE) enabled &= selected;
  if(i==SP_LOCATION || i==SP_PATH || i==SP_FILTER_FILE) enabled &= line && path && path[0]!='@';
  if(i==SP_QUALIFIED || i==SP_LINE || i==SP_FILTER_NAME || i==SP_FILTER_SCOPE) enabled &= line!=0;
  if(i==SP_EXPAND_ONE || i==SP_COLLAPSE_ONE) enabled &= grouped && selected;
  if(i==SP_EXPAND || i==SP_COLLAPSE) enabled &= grouped;
  if(i==SP_MORE) enabled &= insight_symbol_view_has_more(view);
  if(i==SP_LOCATE) { enabled &= GTK_WIDGET(view)==wb.local_view;gtk_widget_set_no_show_all(item,GTK_WIDGET(view)!=wb.local_view);gtk_widget_set_visible(item,GTK_WIDGET(view)==wb.local_view); }
  if(i>=SP_COPY_TEXT) { gboolean local=GTK_WIDGET(view)==wb.local_view;gtk_widget_set_no_show_all(item,!local);gtk_widget_set_visible(item,local);enabled &= local && line && document_get_current() && document_get_current()->id==wb.local_doc_id;if(i!=SP_COPY_TEXT && document_get_current()) enabled &= !document_get_current()->readonly && !SSM(document_get_current()->editor->sci,SCI_GETREADONLY,0,0); }
  if(i==SP_HELP) enabled=TRUE;
  gtk_widget_set_sensitive(item,enabled); if(i==SP_GROUP) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),grouped);
 }
 g_free(path); insight_i18n_apply(menu); gtk_widget_show_all(menu); return menu;
}
static gboolean symbol_popup_key(GtkWidget *widget,gpointer data) { (void)data;GtkWidget *menu=symbol_menu(GTK_TREE_VIEW(widget));GdkEvent *event=gtk_get_current_event();gtk_menu_popup_at_widget(GTK_MENU(menu),widget,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,event);if(event) gdk_event_free(event); return TRUE; }
static gboolean symbol_popup_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)data; if(event->button!=3 || event->type!=GDK_BUTTON_PRESS) return FALSE;
 GtkTreeView *view=GTK_TREE_VIEW(widget);GtkTreeSelection *selection=gtk_tree_view_get_selection(view); GtkTreePath *path=NULL; guint signal=g_signal_lookup("changed",GTK_TYPE_TREE_SELECTION);g_signal_handlers_block_matched(selection,G_SIGNAL_MATCH_ID,signal,0,NULL,NULL,NULL);
 if(gtk_tree_view_get_path_at_pos(view,(gint)event->x,(gint)event->y,&path,NULL,NULL,NULL)) { gtk_tree_view_set_cursor(view,path,NULL,FALSE);gtk_tree_path_free(path); }else gtk_tree_selection_unselect_all(selection);
 g_signal_handlers_unblock_matched(selection,G_SIGNAL_MATCH_ID,signal,0,NULL,NULL,NULL); gtk_widget_grab_focus(widget); gtk_menu_popup_at_pointer(GTK_MENU(symbol_menu(view)),(GdkEvent*)event); return TRUE;
}
static gboolean symbol_key_global(GObject *object,GdkEventKey *event,gpointer data) { (void)object; (void)data;if(!wb.alive) return FALSE;if(insight_workbench_source_link_key(event)) return TRUE;GtkWidget *relation_view=wb.usage_view && gtk_widget_has_focus(wb.usage_view) ? wb.usage_view : (wb.relation_tree && gtk_widget_has_focus(GTK_WIDGET(insight_relation_tree_view(wb.relation_tree))) ? GTK_WIDGET(insight_relation_tree_view(wb.relation_tree)) : NULL);if(relation_view && relation_node_key(relation_view,event,NULL)) return TRUE;if(wb.marks_view && gtk_widget_has_focus(wb.marks_view)) return bookmark_key(wb.marks_view,event,NULL);if(wb.marks_entry && gtk_widget_has_focus(wb.marks_entry)) return bookmark_key(wb.marks_entry,event,NULL);if(wb.tree_view && gtk_widget_has_focus(wb.tree_view)) return folder_key(wb.tree_view,event,NULL);if(wb.tree_entry && gtk_widget_has_focus(wb.tree_entry)) return folder_key(wb.tree_entry,event,NULL);GroupedPanel *panels[]={&wb.type_panel,&wb.category_panel};for(guint i=0;i<G_N_ELEMENTS(panels);i++) { GroupedPanel *p=panels[i];if(p->view && gtk_widget_has_focus(p->view)) return grouped_key_press(p->view,event,p);if(p->entry && gtk_widget_has_focus(p->entry)) return grouped_key_press(p->entry,event,p); }GtkWidget *view=wb.local_view && gtk_widget_has_focus(wb.local_view) ? wb.local_view : (wb.symbol_view && gtk_widget_has_focus(wb.symbol_view) ? wb.symbol_view : (wb.file_view && gtk_widget_has_focus(wb.file_view) ? wb.file_view : NULL));return view ? browser_key(view,event,NULL) : FALSE; }
static void symbol_context_attach(GtkWidget *view) { g_signal_connect_after(view,"button-release-event",G_CALLBACK(browser_symbol_clicked),NULL); g_signal_connect(view,"button-press-event",G_CALLBACK(symbol_popup_button),NULL); g_signal_connect(view,"popup-menu",G_CALLBACK(symbol_popup_key),NULL); }
static void create_browser(void) {
 wb.browser=gtk_box_new(GTK_ORIENTATION_VERTICAL,4); wb.symbol_entry=gtk_search_entry_new(); gtk_widget_set_name(wb.symbol_entry,"insight-symbol-query");
 gtk_entry_set_placeholder_text(GTK_ENTRY(wb.symbol_entry),"Find project symbol…");
 wb.symbols=gtk_list_store_new(SYMBOL_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING);
 wb.symbol_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.symbols)); gtk_widget_set_name(wb.symbol_view,"insight-project-symbols");g_object_set_data(G_OBJECT(wb.symbol_view),"insight-bold-symbol-names",GINT_TO_POINTER(1));
 const char *titles[]={"Symbol","Scope","Project-relative file","Line","Kind"};
 for(int i=0;i<SYMBOL_COLUMNS;i++) {
  GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(titles[i],gtk_cell_renderer_text_new(),"text",i,NULL);
  gtk_tree_view_column_set_sort_column_id(column,i); gtk_tree_view_append_column(GTK_TREE_VIEW(wb.symbol_view),column);
 }
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.symbol_view);
 wb.outline=gtk_check_button_new_with_label("Current file outline");
 gtk_widget_set_no_show_all(wb.outline,TRUE);
 g_signal_connect(wb.outline,"toggled",G_CALLBACK(outline_changed),NULL);
 gtk_box_pack_start(GTK_BOX(wb.browser),wb.outline,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(wb.browser),symbol_query_controls(wb.symbol_entry,FALSE),FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(wb.browser),scroll,TRUE,TRUE,0);
 gtk_box_pack_start(GTK_BOX(wb.browser),sort_selector(wb.symbols),FALSE,FALSE,0);
 GtkWidget *group=insight_symbol_view_attach(GTK_TREE_VIEW(wb.symbol_view),wb.symbols,NULL); gtk_widget_set_name(group,"insight-project-group-types"); gtk_box_pack_start(GTK_BOX(wb.browser),group,FALSE,FALSE,0);
 GtkWidget *scopes=insight_symbol_view_scopes_control(GTK_TREE_VIEW(wb.symbol_view));gtk_widget_set_name(scopes,"insight-project-scope-hierarchy");gtk_box_pack_start(GTK_BOX(wb.browser),scopes,FALSE,FALSE,0);
 GtkWidget *types=insight_symbol_view_types_control(GTK_TREE_VIEW(wb.symbol_view)); gtk_widget_set_name(types,"insight-project-type-filters"); gtk_box_pack_start(GTK_BOX(wb.browser),types,FALSE,FALSE,0); insight_symbol_view_more_loader(GTK_TREE_VIEW(wb.symbol_view),G_CALLBACK(symbols_load_more),500);
 GtkWidget *info=gtk_button_new_with_label("Symbol Info…");g_signal_connect(info,"clicked",G_CALLBACK(browser_symbol_info),wb.symbol_view);gtk_box_pack_start(GTK_BOX(wb.browser),info,FALSE,FALSE,0);
 gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),wb.browser,gtk_label_new("Project Symbols"));
 g_signal_connect(wb.symbol_entry,"changed",G_CALLBACK(browser_filter),NULL);
 g_signal_connect(wb.symbol_entry,"activate",G_CALLBACK(browser_entry_activate),NULL);
 g_signal_connect(wb.symbol_entry,"key-press-event",G_CALLBACK(browser_key),NULL);
 g_signal_connect(wb.symbol_view,"key-press-event",G_CALLBACK(browser_key),NULL); symbol_context_attach(wb.symbol_view);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.symbol_view)),"changed",G_CALLBACK(browser_selected),NULL);
 g_signal_connect(wb.symbol_view,"row-activated",G_CALLBACK(browser_activated),NULL);
 gtk_widget_show_all(wb.browser); browser_filter(NULL,NULL);
}
static void local_structure_query(GtkEditable *entry,gpointer data) {
 (void)data;if(wb.local_mode && gtk_combo_box_get_active(GTK_COMBO_BOX(wb.local_mode))==0 && g_strcmp0(gtk_entry_get_text(GTK_ENTRY(entry)),gtk_entry_get_text(GTK_ENTRY(wb.local_entry)))) gtk_entry_set_text(GTK_ENTRY(wb.local_entry),gtk_entry_get_text(GTK_ENTRY(entry)));
}
static void local_mode_changed(GtkComboBox *combo,gpointer data) {
 (void)data;gboolean details=gtk_combo_box_get_active(combo)==1;gtk_stack_set_visible_child_name(GTK_STACK(wb.local_stack),details ? "details" : "structure");local_filter(GTK_EDITABLE(wb.local_entry),NULL);
}
static void local_native_sort(GtkButton *button,gpointer data) {
 (void)button;GeanyDocument *doc=document_get_current();if(doc) symbols_recreate_tag_list(doc,GPOINTER_TO_INT(data));
}
static void local_filter(GtkEditable *entry,gpointer data) {
 (void)data; if(!wb.local_symbols) return; GeanyDocument *doc=document_get_current();
 if(wb.native_symbol_query && wb.local_mode && gtk_combo_box_get_active(GTK_COMBO_BOX(wb.local_mode))==0 && g_strcmp0(gtk_entry_get_text(GTK_ENTRY(wb.local_entry)),gtk_entry_get_text(GTK_ENTRY(wb.native_symbol_query)))) gtk_entry_set_text(GTK_ENTRY(wb.native_symbol_query),gtk_entry_get_text(GTK_ENTRY(wb.local_entry)));
 if(entry || !wb.local_symbol_limit || wb.local_doc_id!=(doc ? doc->id : 0)) wb.local_symbol_limit=2000;
 wb.local_doc_id=doc ? doc->id : 0;
 gchar *file_label=doc && doc->file_name ? g_path_get_basename(doc->file_name) : g_strdup("Current file symbols");gtk_label_set_text(GTK_LABEL(wb.local_label),file_label);gtk_widget_set_tooltip_text(wb.local_label,doc ? doc->file_name : NULL);g_free(file_label);
 insight_symbol_view_filetype(GTK_TREE_VIEW(wb.local_view),doc ? doc->file_type : NULL);
 SymbolListJob *j=symbol_list_begin(wb.local_symbols,wb.local_entry,wb.local_symbol_limit,FALSE); if(!j) return; j->local=TRUE;j->lang=doc && doc->tm_file ? doc->tm_file->lang : -1; j->path=g_strdup(doc ? doc->file_name : NULL); j->tags=g_ptr_array_new_with_free_func((GDestroyNotify)tm_tag_unref);
 /* Tags are immutable after publication and their reference count is atomic.
  * Capture references, not GTK or a mutable TMSourceFile, for the worker. */
 if(doc && doc->tm_file && doc->tm_file->tags_array) for(guint n=0;n<doc->tm_file->tags_array->len;n++) { TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,n); if(tag) g_ptr_array_add(j->tags,tm_tag_ref(tag)); }
 symbol_list_launch(wb.local_symbols,j);
}
typedef struct { GtkTreeSortable *model; GtkWidget *field,*direction; gboolean syncing;const gint *fields;guint count; } SymbolSort;
static const gint file_sort_fields[]={0,1,2,3};
static const gint symbol_sort_fields[]={SYMBOL_NAME,SYMBOL_LINE,SYMBOL_KIND,SYMBOL_SCOPE,SYMBOL_PATH};
static void symbol_sort_sync(GtkTreeSortable *model,gpointer data) {
 SymbolSort *s=g_object_get_data(G_OBJECT(data),"insight-symbol-sort-state");gint field=-1;GtkSortType direction;
 if(!s || !gtk_tree_sortable_get_sort_column_id(model,&field,&direction)) return;
 s->syncing=TRUE;
 for(guint i=0;i<s->count;i++) if(s->fields[i]==field) gtk_combo_box_set_active(GTK_COMBO_BOX(s->field),i);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->direction),direction==GTK_SORT_DESCENDING);
 gtk_button_set_image(GTK_BUTTON(s->direction),gtk_image_new_from_icon_name(direction==GTK_SORT_DESCENDING ? "view-sort-descending" : "view-sort-ascending",GTK_ICON_SIZE_MENU));
 s->syncing=FALSE;
}
static void symbol_sort_changed(GtkWidget *widget,gpointer data) {
 (void)widget;SymbolSort *s=data;if(s->syncing) return;gint mode=gtk_combo_box_get_active(GTK_COMBO_BOX(s->field));
 if(mode>=0 && mode<(gint)s->count) gtk_tree_sortable_set_sort_column_id(s->model,s->fields[mode],gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->direction)) ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
}
static GtkWidget *sort_selector(GtkListStore *model) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);SymbolSort *s=g_new0(SymbolSort,1);s->model=GTK_TREE_SORTABLE(model);s->fields=model==wb.file_store ? file_sort_fields : symbol_sort_fields;s->count=model==wb.file_store ? G_N_ELEMENTS(file_sort_fields) : G_N_ELEMENTS(symbol_sort_fields);s->field=gtk_combo_box_text_new();s->direction=gtk_toggle_button_new();
 gtk_widget_set_name(box,model==wb.file_store ? "insight-files-sort" : (model==wb.local_symbols ? "insight-local-sort" : "insight-project-sort"));
 gtk_widget_set_name(s->field,"insight-symbol-sort-field");gtk_widget_set_name(s->direction,"insight-symbol-sort-descending");
 const char *labels[]={"Sort: name","Sort: source line","Sort: symbol type","Sort: scope","Sort: file path"};
 const gchar *file_labels[]={"Sort: file name","Sort: full path","Sort: directory","Sort: size"};for(guint i=0;i<s->count;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->field),model==wb.file_store ? file_labels[i] : labels[i]);
 gtk_widget_set_tooltip_text(s->direction,"Descending order");gtk_button_set_relief(GTK_BUTTON(s->direction),GTK_RELIEF_NONE);
 gtk_box_pack_start(GTK_BOX(box),s->field,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(box),s->direction,FALSE,FALSE,0);
 g_object_set_data_full(G_OBJECT(box),"insight-symbol-sort-state",s,g_free);
 g_signal_connect(s->field,"changed",G_CALLBACK(symbol_sort_changed),s);g_signal_connect(s->direction,"toggled",G_CALLBACK(symbol_sort_changed),s);
 g_signal_connect_object(model,"sort-column-changed",G_CALLBACK(symbol_sort_sync),box,0);
 gtk_combo_box_set_active(GTK_COMBO_BOX(s->field),0);compact_panel_control(s->field);return box;
}
static void symbol_sort_write(GKeyFile *settings,GtkListStore *model,const gchar *prefix) {
 gint column=-1;GtkSortType direction;if(!gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(model),&column,&direction)) return;
 gchar *key=g_strconcat(prefix,"-sort-column",NULL);g_key_file_set_integer(settings,"workbench",key,column);g_free(key);
 key=g_strconcat(prefix,"-sort-descending",NULL);g_key_file_set_boolean(settings,"workbench",key,direction==GTK_SORT_DESCENDING);g_free(key);
}
static void symbol_sort_read(GKeyFile *settings,GtkListStore *model,const gchar *prefix) {
 gchar *key=g_strconcat(prefix,"-sort-column",NULL);GError *error=NULL;gint column=g_key_file_get_integer(settings,"workbench",key,&error);g_free(key);
 if(error || column<0 || column>=gtk_tree_model_get_n_columns(GTK_TREE_MODEL(model))) { g_clear_error(&error);return; }
 key=g_strconcat(prefix,"-sort-descending",NULL);gboolean descending=g_key_file_get_boolean(settings,"workbench",key,NULL);g_free(key);
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model),column,descending ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
}
static void create_local_browser(void) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3); wb.local_label=gtk_label_new("Current file symbols");
 gtk_label_set_ellipsize(GTK_LABEL(wb.local_label),PANGO_ELLIPSIZE_MIDDLE);PangoAttrList *title_style=pango_attr_list_new();pango_attr_list_insert(title_style,pango_attr_weight_new(PANGO_WEIGHT_BOLD));gtk_label_set_attributes(GTK_LABEL(wb.local_label),title_style);pango_attr_list_unref(title_style);
 wb.local_entry=gtk_search_entry_new(); gtk_widget_set_name(wb.local_entry,"insight-local-query"); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.local_entry),"Symbol Name…");
 wb.local_symbols=gtk_list_store_new(SYMBOL_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING);
 wb.local_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.local_symbols)); gtk_widget_set_name(wb.local_view,"insight-local-symbols");g_object_set_data(G_OBJECT(wb.local_view),"insight-source-nesting",GINT_TO_POINTER(1)); g_object_set_data(G_OBJECT(wb.local_view),"insight-bold-symbol-names",GINT_TO_POINTER(1)); gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.local_view)),GTK_SELECTION_BROWSE);
 gint columns[]={SYMBOL_NAME,SYMBOL_LINE,SYMBOL_KIND}; const char *labels[]={"Symbol","Line","Type"};
 for(guint i=0;i<3;i++) { GtkTreeViewColumn *c=gtk_tree_view_column_new_with_attributes(labels[i],gtk_cell_renderer_text_new(),"text",columns[i],NULL); gtk_tree_view_column_set_sort_column_id(c,columns[i]); gtk_tree_view_append_column(GTK_TREE_VIEW(wb.local_view),c); }
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.local_view);
 gtk_box_pack_start(GTK_BOX(box),wb.local_label,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),symbol_query_controls(wb.local_entry,TRUE),FALSE,FALSE,0);
 GtkWidget *details=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_box_pack_start(GTK_BOX(details),scroll,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(details),sort_selector(wb.local_symbols),FALSE,FALSE,0);
 GtkWidget *group=insight_symbol_view_attach(GTK_TREE_VIEW(wb.local_view),wb.local_symbols,NULL); gtk_widget_set_name(group,"insight-local-group-types");compact_panel_control(group); gtk_box_pack_start(GTK_BOX(details),group,FALSE,FALSE,0);
 GtkWidget *scopes=insight_symbol_view_scopes_control(GTK_TREE_VIEW(wb.local_view));gtk_widget_set_name(scopes,"insight-local-scope-hierarchy");compact_panel_control(scopes);gtk_box_pack_start(GTK_BOX(details),scopes,FALSE,FALSE,0);
 GtkWidget *types=insight_symbol_view_types_control(GTK_TREE_VIEW(wb.local_view)); gtk_widget_set_name(types,"insight-local-type-filters"); gtk_box_pack_start(GTK_BOX(details),types,FALSE,FALSE,0); insight_symbol_view_more_loader(GTK_TREE_VIEW(wb.local_view),G_CALLBACK(symbols_load_more),2000);
 GtkWidget *locate=gtk_button_new();gtk_widget_set_name(locate,"insight-local-locate");gtk_button_set_image(GTK_BUTTON(locate),insight_command_icon("find-location-symbolic"));gtk_widget_set_tooltip_text(locate,"Locate symbol at caret");g_signal_connect(locate,"clicked",G_CALLBACK(symbols_locate),NULL);gtk_box_pack_start(GTK_BOX(types),locate,FALSE,FALSE,0);
 compact_panel_control(types);
 GtkWidget *info=gtk_button_new_with_label("Symbol Info…");compact_panel_control(info);g_signal_connect(info,"clicked",G_CALLBACK(browser_symbol_info),wb.local_view);gtk_box_pack_start(GTK_BOX(details),info,FALSE,FALSE,0);
 GtkWidget *home=gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),0),*native=ui_lookup_widget(main_widgets.window,"scrolledwindow2");
 wb.native_symbol_query=ui_lookup_widget(main_widgets.window,"entry_tagfilter");gtk_widget_set_no_show_all(wb.native_symbol_query,TRUE);gtk_widget_hide(wb.native_symbol_query);
 wb.local_mode=gtk_combo_box_text_new();gtk_widget_set_name(wb.local_mode,"insight-file-symbol-mode");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.local_mode),"Structure");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.local_mode),"Detailed symbols");gtk_combo_box_set_active(GTK_COMBO_BOX(wb.local_mode),0);compact_panel_control(wb.local_mode);gtk_box_pack_start(GTK_BOX(box),wb.local_mode,FALSE,FALSE,0);
 wb.local_stack=gtk_stack_new();gtk_widget_set_name(wb.local_stack,"insight-file-symbol-stack");gtk_stack_set_homogeneous(GTK_STACK(wb.local_stack),FALSE);
 GtkWidget *structure=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);g_object_ref(native);gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(native)),native);gtk_box_pack_start(GTK_BOX(structure),native,TRUE,TRUE,0);g_object_unref(native);
 GtkWidget *sortbar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);const gchar *sorts[]={"Name","Source order"};for(guint i=0;i<2;i++) { GtkWidget *sort=gtk_button_new_with_label(sorts[i]);compact_panel_control(sort);gtk_box_pack_start(GTK_BOX(sortbar),sort,TRUE,TRUE,0);g_signal_connect(sort,"clicked",G_CALLBACK(local_native_sort),GINT_TO_POINTER(i ? SYMBOLS_SORT_BY_APPEARANCE : SYMBOLS_SORT_BY_NAME)); }gtk_box_pack_start(GTK_BOX(structure),sortbar,FALSE,FALSE,0);
 gtk_stack_add_named(GTK_STACK(wb.local_stack),structure,"structure");gtk_stack_add_named(GTK_STACK(wb.local_stack),details,"details");gtk_box_pack_start(GTK_BOX(box),wb.local_stack,TRUE,TRUE,0);gtk_stack_set_visible_child_name(GTK_STACK(wb.local_stack),"structure");gtk_box_pack_start(GTK_BOX(home),box,TRUE,TRUE,0);gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(main_widgets.sidebar_notebook),home,"File Symbols");
 g_signal_connect(wb.local_mode,"changed",G_CALLBACK(local_mode_changed),NULL);g_signal_connect(wb.native_symbol_query,"changed",G_CALLBACK(local_structure_query),NULL);
 gtk_notebook_set_show_tabs(GTK_NOTEBOOK(main_widgets.sidebar_notebook),TRUE);
 g_signal_connect(wb.local_entry,"changed",G_CALLBACK(local_filter),NULL); g_signal_connect(wb.local_entry,"activate",G_CALLBACK(browser_entry_activate),NULL);
 g_signal_connect(wb.local_entry,"key-press-event",G_CALLBACK(browser_key),NULL); g_signal_connect(wb.local_view,"key-press-event",G_CALLBACK(browser_key),NULL); symbol_context_attach(wb.local_view);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.local_view)),"changed",G_CALLBACK(browser_selected),NULL);
 g_signal_connect(wb.local_view,"row-activated",G_CALLBACK(browser_activated),NULL); gtk_widget_show_all(box);
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.local_symbols),SYMBOL_LINE,GTK_SORT_ASCENDING);
 gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),0); local_filter(NULL,NULL);
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
/* Keep the selected source and expanded folders stable across manifest/filter
 * rebuilds. Model paths alone are not stable when siblings are sorted. */
typedef struct { const gchar *wanted; GtkTreePath *found; GHashTable *expanded; } FolderVisit;
static gboolean folder_visit(GtkTreeModel *model,GtkTreePath *row,GtkTreeIter *iter,gpointer data) {
 FolderVisit *visit=data; gchar *path=NULL; gtk_tree_model_get(model,iter,1,&path,-1);
 if(visit->expanded && gtk_tree_view_row_expanded(GTK_TREE_VIEW(wb.tree_view),row)) g_hash_table_add(visit->expanded,g_strdup(path));
 if(visit->wanted && !g_strcmp0(path,visit->wanted)) { visit->found=gtk_tree_path_copy(row); g_free(path); return TRUE; }
 g_free(path); return FALSE;
}
static gboolean folder_restore_expanded(GtkTreeModel *model,GtkTreePath *row,GtkTreeIter *iter,gpointer data) {
 gchar *path=NULL; gtk_tree_model_get(model,iter,1,&path,-1);
 if(g_hash_table_contains(data,path)) gtk_tree_view_expand_row(GTK_TREE_VIEW(wb.tree_view),row,FALSE);
 g_free(path); return FALSE;
}
static gchar *folder_selected_path(gboolean *directory) {
 GtkTreeModel *model; GtkTreeIter iter;
 if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.tree_view)),&model,&iter)) return NULL;
 gchar *path=NULL; gtk_tree_model_get(model,&iter,1,&path,2,directory,-1); return path;
}
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
static void bookmark_window_destroyed(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;wb.bookmark_window=NULL;wb.bookmark_search=NULL;g_clear_object(&wb.bookmark_list);
}
static void show_bookmarks(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;insight_workbench_bookmarks_changed();
 if(wb.bookmark_window) { bookmark_list_reload(NULL,NULL);gtk_window_present(GTK_WINDOW(wb.bookmark_window));return; }
 wb.bookmark_window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(wb.bookmark_window),"Bookmarks");gtk_window_set_default_size(GTK_WINDOW(wb.bookmark_window),750,420);
 gtk_window_set_transient_for(GTK_WINDOW(wb.bookmark_window),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(wb.bookmark_window),TRUE);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4),*view;gtk_container_set_border_width(GTK_CONTAINER(box),6);gtk_container_add(GTK_CONTAINER(wb.bookmark_window),box);
 wb.bookmark_search=gtk_search_entry_new();gtk_widget_set_tooltip_text(wb.bookmark_search,"Prefix with ~ for fuzzy subsequence matching");gtk_widget_set_name(wb.bookmark_search,"insight-bookmark-list-query");gtk_entry_set_placeholder_text(GTK_ENTRY(wb.bookmark_search),"Filter bookmarks…");gtk_box_pack_start(GTK_BOX(box),wb.bookmark_search,FALSE,FALSE,0);
 wb.bookmark_list=gtk_list_store_new(6,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_UINT64,G_TYPE_UINT,G_TYPE_INT,G_TYPE_STRING);view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.bookmark_list));
 gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),-1,"File:line > source",gtk_cell_renderer_text_new(),"text",0,NULL);gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(view),1);
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.bookmark_list),0,GTK_SORT_ASCENDING);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 GtkWidget *refresh=gtk_button_new_with_label("Refresh");gtk_box_pack_start(GTK_BOX(box),refresh,FALSE,FALSE,0);
 g_signal_connect(refresh,"clicked",G_CALLBACK(bookmark_list_reload),NULL);g_signal_connect(wb.bookmark_search,"changed",G_CALLBACK(bookmark_list_reload),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),"changed",G_CALLBACK(bookmark_list_selected),NULL);
 insight_panel_help_attach(gtk_bin_get_child(GTK_BIN(wb.bookmark_window)),"panel-bookmarks.md");
 g_signal_connect(wb.bookmark_window,"destroy",G_CALLBACK(bookmark_window_destroyed),NULL);bookmark_list_reload(NULL,NULL);gtk_widget_show_all(wb.bookmark_window);gtk_widget_grab_focus(wb.bookmark_search);
}
static void bookmark_scope_label(void) {
 if(!wb.marks_scope) return;
 gchar *base=wb.project_marks_path ? g_path_get_basename(wb.project_marks_path) : NULL,*label=wb.marks_stale ? g_strdup("Previous bookmark store (unsaved)") : wb.marks_project ? g_strdup_printf(wb.marks_valid ? "Project bookmarks: %s" : "Project bookmarks unavailable: %s",base ? base : "") : g_strdup("Global bookmarks (no project)");
 gtk_label_set_text(GTK_LABEL(wb.marks_scope),label);gtk_widget_set_tooltip_text(wb.marks_scope,wb.project_marks_path ? wb.project_marks_path : wb.notes_path);gtk_widget_set_sensitive(wb.marks_reload,wb.marks_project || wb.marks_stale);g_free(label);g_free(base);
}
static gboolean bookmarks_project_load(const gchar *project,const gchar *root,gboolean force) {
 if(!wb.project_marks) return FALSE;
 gchar *native=project ? utils_get_locale_from_utf8(project) : NULL,*path=native ? g_strconcat(native,".bookmarks.conf",NULL) : NULL;g_free(native);
 gboolean same=!g_strcmp0(path,wb.project_marks_path) && !g_strcmp0(root,wb.project_marks_root);
 if(wb.marks_initialized && !force && same && !wb.marks_stale) { g_free(path);return TRUE; }
 if(wb.marks_initialized) {
  if(force && same && wb.marks_save_failed) { ui_set_statusbar(TRUE,"Unsaved bookmarks remain in memory; export them before reloading");g_free(path);return FALSE; }
  if(!(force && same)) {
   gboolean stale=wb.marks_stale;wb.marks_stale=FALSE;
   if(!stale) {
    for(guint i=0;i<documents_array->len;i++) { GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(!doc->is_valid || !doc->file_name) continue;for(guint n=0;n<notes_for(INSIGHT_BOOKMARK)->items->len;n++) { const InsightNote *note=g_ptr_array_index(notes_for(INSIGHT_BOOKMARK)->items,n);if(note->kind==INSIGHT_BOOKMARK && !g_strcmp0(note->path,doc->file_name)) { bookmark_prepare_tags(doc);break; } } }
    insight_workbench_bookmarks_changed();
   }
   if(wb.marks_save_failed && !notes_save()) { wb.marks_stale=TRUE;wb.marks_generation++;bookmark_scope_label();g_free(path);return FALSE; }
  }
  bookmark_clear_handles();
 }
 wb.marks_initialized=TRUE;wb.marks_generation++;wb.marks_project=project!=NULL;wb.marks_valid=path!=NULL && root && g_path_is_absolute(root);wb.marks_stale=FALSE;wb.marks_save_failed=FALSE;
 insight_notes_free(wb.project_marks);wb.project_marks=insight_notes_new();g_free(wb.project_marks_path);wb.project_marks_path=path;g_free(wb.project_marks_root);wb.project_marks_root=g_strdup(root);
 if(path && wb.marks_valid && g_file_test(path,G_FILE_TEST_EXISTS)) {
  bookmark_session_remember(path,root,NULL);
  GError *error=NULL;InsightNotes *loaded=insight_notes_load_portable(path,root,&error),*marks=loaded ? insight_notes_select(loaded,INSIGHT_BOOKMARK,&error) : NULL;
  if(marks && marks->items->len!=loaded->items->len) { insight_notes_free(marks);marks=NULL;g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Project bookmark store contains non-bookmark notes"); }
  insight_notes_free(loaded);
  if(marks) { insight_notes_free(wb.project_marks);wb.project_marks=marks; }else { g_hash_table_remove(wb.marks_session_stores,path);wb.marks_valid=FALSE;ui_set_statusbar(TRUE,"Project bookmarks ignored: %s",error ? error->message : "invalid snapshot"); }g_clear_error(&error);
 }
 bookmark_scope_label();if(wb.marks_view) gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.marks_view)));
 notes_filter(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK));
 for(guint i=0;i<documents_array->len;i++) bookmark_restore_document(g_ptr_array_index(documents_array,i));
 bookmark_list_reload(NULL,NULL);return TRUE;
}
static void bookmarks_project_reload(GtkButton *button,gpointer data) { (void)button;(void)data;gchar *root=app->project ? project_get_base_path() : NULL;bookmarks_project_load(app->project ? app->project->file_name : NULL,root,TRUE);g_free(root); }
static void notes_filter(GtkEditable *entry,gpointer data) {
 (void)entry; InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data); GtkListStore *store=kind==INSIGHT_BOOKMARK ? wb.marks_store : wb.clips_store; GtkWidget *field=kind==INSIGHT_BOOKMARK ? wb.marks_entry : wb.clips_entry;
 if(!store) return;
 GPtrArray *selected=selected_notes(kind);gchar *language=kind==INSIGHT_CLIP ? clip_current_language() : NULL;GHashTable *wanted=g_hash_table_new(g_str_hash,g_str_equal);
 for(guint i=0;i<selected->len;i++) g_hash_table_add(wanted,g_ptr_array_index(selected,i));
 GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(kind==INSIGHT_CLIP ? wb.clips_view : wb.marks_view));
 if(selection) g_signal_handlers_block_by_func(selection,note_selected,GINT_TO_POINTER(kind));
 gtk_list_store_clear(store); const gchar *query=gtk_entry_get_text(GTK_ENTRY(field));
 InsightNotes *source=notes_for(kind);
 for(guint i=0;i<source->items->len;i++) { InsightNote *note=g_ptr_array_index(source->items,i); if(note->kind!=kind || (kind==INSIGHT_CLIP && !(wb.clips_all && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.clips_all))) && !insight_note_matches_language(note,language))) continue;
  gchar *line=g_strdup_printf("%"G_GUINT64_FORMAT,note->line),*search=g_strconcat(note->name," ",note->path," ",line," ",note->anchor ? note->anchor->name : "",NULL); gboolean match=insight_lookup_text_match(search,query); g_free(line); g_free(search); if(!match) continue;
  GtkTreeIter iter; gtk_list_store_append(store,&iter); gtk_list_store_set(store,&iter,0,note->name,1,note->path,2,(gulong)note->line,3,note->text,4,note->anchor ? note->anchor->name : "",-1); if(g_hash_table_contains(wanted,note->name)) gtk_tree_selection_select_iter(selection,&iter);
 }
 if(selection) g_signal_handlers_unblock_by_func(selection,note_selected,GINT_TO_POINTER(kind));
 if(kind==INSIGHT_CLIP) { g_free(wb.clips_language); wb.clips_language=language; } else g_free(language);g_hash_table_unref(wanted);g_ptr_array_unref(selected);
}
static void clips_scope_changed(GtkComboBox *combo,gpointer data) {
 (void)data;gboolean project=gtk_combo_box_get_active(combo)==1;
 if(project && (!wb.project_notes_path || !wb.project_notes_valid)) { ui_set_statusbar(TRUE,"Open a project with a valid template store first");gtk_combo_box_set_active(combo,0);return; }
 if(!wb.clips_scope_setting) wb.clips_prefer_project=project;
 if(project==wb.clips_project) return;
 wb.clips_project=project;wb.clips_generation++;gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.clips_view)));notes_filter(NULL,GINT_TO_POINTER(INSIGHT_CLIP));
}
static void clips_project_load(const gchar *project,gboolean force) {
 if(!wb.project_notes) return;
 gchar *native=project ? utils_get_locale_from_utf8(project) : NULL,*path=native ? g_strconcat(native,".templates.conf",NULL) : NULL;g_free(native);
 if(!force && !g_strcmp0(path,wb.project_notes_path)) { g_free(path);return; }
 wb.clips_generation++;insight_notes_free(wb.project_notes);wb.project_notes=insight_notes_new();g_free(wb.project_notes_path);wb.project_notes_path=path;wb.project_notes_valid=path!=NULL;
 if(path && g_file_test(path,G_FILE_TEST_EXISTS)) { GError *error=NULL;InsightNotes *loaded=insight_notes_load(path,&error),*clips=loaded ? insight_notes_select(loaded,INSIGHT_CLIP,&error) : NULL;insight_notes_free(loaded);if(clips) { insight_notes_free(wb.project_notes);wb.project_notes=clips; }else { wb.project_notes_valid=FALSE;ui_set_statusbar(TRUE,"Project templates ignored: %s",error ? error->message : "invalid snapshot"); }g_clear_error(&error); }
 gtk_widget_set_tooltip_text(wb.clips_scope,path ? path : "Open a project to use project templates");
 wb.clips_scope_setting=TRUE;gtk_combo_box_set_active(GTK_COMBO_BOX(wb.clips_scope),wb.clips_prefer_project && wb.project_notes_valid ? 1 : 0);wb.clips_scope_setting=FALSE;
 gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.clips_view)));notes_filter(NULL,GINT_TO_POINTER(INSIGHT_CLIP));
}
static void clips_project_reload(GtkButton *button,gpointer data) { (void)button;(void)data;clips_project_load(app->project ? app->project->file_name : NULL,TRUE); }
static GPtrArray *selected_notes(InsightNoteKind kind) {
 GtkWidget *view=kind==INSIGHT_BOOKMARK ? wb.marks_view : wb.clips_view;GtkTreeModel *model=NULL;
 GList *rows=gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&model);GPtrArray *names=g_ptr_array_new_with_free_func(g_free);
 for(GList *p=rows;p;p=p->next) { GtkTreeIter iter;if(gtk_tree_model_get_iter(model,&iter,p->data)) { gchar *name=NULL;gtk_tree_model_get(model,&iter,0,&name,-1);g_ptr_array_add(names,name); } }
 g_list_free_full(rows,(GDestroyNotify)gtk_tree_path_free);return names;
}
static gchar *selected_note(InsightNoteKind kind) {
 GPtrArray *names=selected_notes(kind);gchar *name=names->len==1 ? g_strdup(g_ptr_array_index(names,0)) : NULL;g_ptr_array_unref(names);return name;
}
static gchar *bookmark_cursor_note(void) {
 GtkTreePath *path=NULL;GtkTreeIter iter;gchar *name=NULL;gtk_tree_view_get_cursor(GTK_TREE_VIEW(wb.marks_view),&path,NULL);
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(wb.marks_view));
 if(path && gtk_tree_selection_path_is_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.marks_view)),path) && gtk_tree_model_get_iter(model,&iter,path)) gtk_tree_model_get(model,&iter,0,&name,-1);
 if(path) gtk_tree_path_free(path);
 return name;
}
static void note_selected(GtkTreeSelection *selection,gpointer data) {
 (void)selection; InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data); gchar *name=selected_note(kind); if(!name || !wb.preview) { g_free(name); return; }
 const InsightNote *note=insight_notes_get(notes_for(kind),kind,name);
 if(note && kind==INSIGHT_BOOKMARK) bookmark_open_note(note);
 else if(note) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.context_toggle),TRUE); GeanyDocument *doc=document_get_current(); render_source(note->text,doc ? doc->file_type : NULL,1,NULL); g_clear_pointer(&wb.source_path,g_free); g_clear_pointer(&wb.source_digest,g_free); wb.source_id=0; wb.source_line=0; preview_links();navigation_state(); gchar *label=g_strdup_printf("Clip — %s",note->name); gtk_label_set_text(GTK_LABEL(wb.title),label); g_free(label); }
 g_free(name);
}
static void bookmark_add(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(!bookmarks_writable()) return;GeanyDocument *doc=document_get_current();if(!doc || !doc->file_name) { ui_set_statusbar(TRUE,"Save the file before adding a persistent bookmark");return; }
 bookmark_prepare_tags(doc);insight_workbench_bookmarks_changed();gint line=sci_get_current_line(doc->editor->sci);gchar *text=bookmark_line_text(doc->editor->sci,line);
 const gchar *input=gtk_entry_get_text(GTK_ENTRY(wb.marks_entry));gchar *name=*input ? g_strdup(input) : NULL;
 for(guint n=0;!name;n++) { gchar *candidate=n<26 ? g_strdup_printf("%c",'A'+n) : g_strdup_printf("%c%u",'A'+n%26,n/26);if(!insight_notes_get(notes_for(INSIGHT_BOOKMARK),INSIGHT_BOOKMARK,candidate)) name=candidate;else g_free(candidate); }GError *error=NULL;InsightNotes *candidate=notes_clone(INSIGHT_BOOKMARK,&error);
 if(candidate && insight_notes_set(candidate,INSIGHT_BOOKMARK,name,doc->file_name,(guint64)line+1,text,&error)) { GArray *symbols=bookmark_symbols(doc);if(symbols) { insight_notes_capture_anchor(candidate,name,(const InsightBookmarkSymbol *)symbols->data,symbols->len,NULL);g_array_unref(symbols); }notes_commit(candidate,INSIGHT_BOOKMARK); }
 else { insight_notes_free(candidate);ui_set_statusbar(TRUE,"Bookmark: %s",error ? error->message : "not stored"); }
 g_clear_error(&error);g_free(name);g_free(text);
}
/* Snapshots of the loaded, filtered relation model. No traversal loads branches. */
typedef struct { gchar *name,*path,*source,*digest;gulong line;guint id;gboolean borrowed; } RelationNoteRow;
typedef struct { GString *text;GPtrArray *rows;guint visited;gboolean invalid; } RelationSnapshot;
typedef struct { GtkWidget *dialog,*preview,*status;RelationSnapshot *snapshot;GCancellable *cancel;guint epoch;guint64 scope;gboolean busy,closed; } RelationReport;
typedef struct { GPtrArray *rows;InsightNotes *candidate;GHashTable *versions;gchar *outline,*store_digest;guint epoch;guint64 scope;gint encoding; } RelationNoteJob;
static void relation_note_row_free(gpointer data) { RelationNoteRow *r=data;g_free(r->name);g_free(r->path);if(!r->borrowed) g_free(r->source);g_free(r->digest);g_free(r); }
static void relation_snapshot_free(RelationSnapshot *s) { if(s) { g_string_free(s->text,TRUE);g_ptr_array_unref(s->rows);g_free(s); } }
static gchar *relation_location_key(const gchar *path,gulong line) { return g_strdup_printf("%" G_GSIZE_FORMAT ":%s:%lu",strlen(path),path,line); }
static gchar *relation_escape(const gchar *text) {
 GString *s=g_string_new(NULL);for(const gchar *p=text ? text:"";*p;p++) { if(*p=='\\') g_string_append(s,"\\\\");else if(*p=='\n') g_string_append(s,"\\n");else if(*p=='\r') g_string_append(s,"\\r");else if(*p=='\t') g_string_append(s,"\\t");else g_string_append_c(s,*p); }return g_string_free(s,FALSE);
}
static gboolean relation_snapshot_row(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
 RelationSnapshot *s=data;gint depth=gtk_tree_path_get_depth(path);if(++s->visited>4000 || depth>64) { s->invalid=TRUE;return TRUE; }
 gchar *name=NULL,*file=NULL;gulong line=0;guint id=0;gtk_tree_model_get(model,iter,USAGE_SYMBOL,&name,USAGE_PATH,&file,USAGE_LINE,&line,USAGE_ID,&id,-1);
 if((name && (!g_utf8_validate(name,-1,NULL) || strlen(name)>8192)) || (file && (!g_utf8_validate(file,-1,NULL) || strlen(file)>16384))) s->invalid=TRUE;
 if(!s->invalid && name && *name) {
  gchar *n=relation_escape(name),*f=relation_escape(file);for(gint i=1;i<depth;i++) g_string_append(s->text,"  ");g_string_append(s->text,n);
  if(file && *file && line) g_string_append_printf(s->text,"\t%s:%lu",f,line);g_string_append_c(s->text,'\n');g_free(n);g_free(f);
  if(file && g_path_is_absolute(file) && line && line<=2147483647) { RelationNoteRow *r=g_new0(RelationNoteRow,1);r->name=g_strdup(name);r->path=g_strdup(file);r->line=line;r->id=id;g_ptr_array_add(s->rows,r); }
 }
 g_free(name);g_free(file);if(s->text->len>1024u*1024u || s->rows->len>2000) s->invalid=TRUE;return s->invalid;
}
static RelationSnapshot *relation_snapshot_new(void) {
 if(wb.search_worker || wb.usage_idle || gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==3 || strlen(gtk_entry_get_text(GTK_ENTRY(wb.usage_entry)))>8192) return NULL;
 const gchar *shown=gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));gboolean tree=!g_strcmp0(shown,"tree") || !g_strcmp0(shown,"treegraph");GtkTreeView *view=tree ? insight_relation_tree_view(wb.relation_tree):GTK_TREE_VIEW(wb.usage_view);
 RelationSnapshot *s=g_new0(RelationSnapshot,1);s->rows=g_ptr_array_new_with_free_func(relation_note_row_free);gchar *query=relation_escape(gtk_entry_get_text(GTK_ENTRY(wb.usage_entry)));s->text=g_string_new("Relations: ");g_string_append_printf(s->text,"%s\nMode: %d; %s\n",query,gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)),tree ? "loaded tree":"filtered list");g_free(query);
 gtk_tree_model_foreach(gtk_tree_view_get_model(view),relation_snapshot_row,s);if(s->invalid) { relation_snapshot_free(s);return NULL; }return s;
}
static void relation_print_show(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(wb.search_worker || wb.usage_idle || gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==3) { ui_set_statusbar(TRUE,"Wait for symbol Relations to finish before printing");return; }
 const gchar *view=gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));GtkPrintOperation *op=NULL;
 if(!g_strcmp0(view,"graph") || !g_strcmp0(view,"treegraph")) { op=insight_relation_graph_print_operation(!strcmp(view,"treegraph") ? wb.relation_tree_graph:wb.relation_graph);if(!op) { ui_set_statusbar(TRUE,"Graph print snapshot could not be created");return; } }
 else { RelationSnapshot *snapshot=relation_snapshot_new();if(snapshot) { op=insight_text_print_operation(insight_tr("Relations"),snapshot->text->str,snapshot->text->len);relation_snapshot_free(snapshot); } }
 insight_text_print_run(op,GTK_WINDOW(main_widgets.window));
}
static void relation_report_free(gpointer data) { RelationReport *s=data;relation_snapshot_free(s->snapshot);g_clear_object(&s->cancel);g_free(s); }
static void relation_report_destroy(GtkWidget *widget,gpointer data) { (void)widget;RelationReport *s=data;s->closed=TRUE;if(s->cancel) g_cancellable_cancel(s->cancel); }
static void relation_note_job_free(gpointer data) { RelationNoteJob *j=data;g_ptr_array_unref(j->rows);insight_notes_free(j->candidate);g_hash_table_unref(j->versions);g_free(j->outline);g_free(j->store_digest);g_free(j); }
static gchar *relation_file_version(const gchar *path,GCancellable *cancel) {
 gchar *native=utils_get_locale_from_utf8(path);GFile *file=g_file_new_for_path(native);g_free(native);GFileInfo *info=g_file_query_info(file,"standard::type,standard::size,time::modified,time::modified-usec,etag::value,id::file",G_FILE_QUERY_INFO_NONE,cancel,NULL);g_object_unref(file);
 if(!info) return NULL;gchar *version=NULL;if(g_file_info_get_file_type(info)==G_FILE_TYPE_REGULAR) version=g_strdup_printf("%" G_GOFFSET_FORMAT ":%" G_GUINT64_FORMAT ":%u:%s:%s",g_file_info_get_size(info),g_file_info_get_attribute_uint64(info,G_FILE_ATTRIBUTE_TIME_MODIFIED),g_file_info_get_attribute_uint32(info,G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC),g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ETAG_VALUE) ? g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ETAG_VALUE):"",g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE) ? g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE):"");g_object_unref(info);return version;
}
static gchar *relation_source_line(const gchar *text,gulong line) {
 const gchar *p=text;for(gulong n=1;n<line;n++) { while(*p && *p!='\n' && *p!='\r') p++;if(!*p) return NULL;gchar c=*p++;if(c=='\r' && *p=='\n') p++; }
 const gchar *end=p;while(*end && *end!='\n' && *end!='\r') end++;if(end-p>65536) return NULL;return g_strndup(p,(gsize)(end-p));
}
static void relation_notes_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source;RelationNoteJob *j=data;GHashTable *files=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);gsize bytes=0;GError *error=NULL;
 for(guint i=0;i<j->rows->len && !error;i++) {
  RelationNoteRow *r=g_ptr_array_index(j->rows,i);if(g_cancellable_set_error_if_cancelled(cancel,&error)) break;const gchar *text=g_hash_table_lookup(files,r->path);
  if(!text) {
   gchar *loaded=r->source ? g_strdup(r->source):NULL,*version=NULL;
   if(!r->source) { version=relation_file_version(r->path,cancel);if(version) loaded=read_disk_source_full(r->path,TRUE,j->encoding,cancel);gchar *after=loaded ? relation_file_version(r->path,cancel):NULL;if(!version || !after || strcmp(version,after)) g_clear_pointer(&loaded,g_free);g_free(after); }
   if(!loaded || bytes+strlen(loaded)>16u*1024u*1024u) { g_free(loaded);g_free(version);g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Cannot read a stable source snapshot (2 MiB per file, 16 MiB total)");break; }
   bytes+=strlen(loaded);g_hash_table_insert(files,g_strdup(r->path),loaded);text=loaded;if(version) g_hash_table_insert(j->versions,g_strdup(r->path),version);
  }
  gchar *line=relation_source_line(text,r->line);if(!line) { g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"A result line is unavailable or exceeds 64 KiB; refresh Relations");break; }
  gchar *base=g_utf8_substring(r->name,0,MIN((glong)32,g_utf8_strlen(r->name,-1))),*name=NULL;for(guint n=1;!name;n++) { gchar *candidate=g_strdup_printf("%s #%u",base,n);if(!insight_notes_get(j->candidate,INSIGHT_BOOKMARK,candidate)) name=candidate;else g_free(candidate); }
  insight_notes_set(j->candidate,INSIGHT_BOOKMARK,name,r->path,r->line,line,&error);g_free(base);g_free(name);g_free(line);
 }
 g_hash_table_unref(files);if(error) g_task_return_error(task,error);else g_task_return_boolean(task,TRUE);
}
static void relation_digest_field(GChecksum *sum,const gchar *text) { const gchar *value=text ? text:"";g_checksum_update(sum,(const guchar *)value,strlen(value)+1); }
static gchar *relation_notes_digest(const InsightNotes *notes) {
 GChecksum *sum=g_checksum_new(G_CHECKSUM_SHA256);for(guint i=0;i<notes->items->len;i++) { const InsightNote *n=g_ptr_array_index(notes->items,i);gchar *numbers=g_strdup_printf("%u:%" G_GUINT64_FORMAT ":%u:%" G_GUINT64_FORMAT ":%u",n->kind,n->line,n->anchor ? n->anchor->kind:0,n->anchor ? n->anchor->offset:0,n->languages ? g_strv_length(n->languages):0);relation_digest_field(sum,numbers);g_free(numbers);relation_digest_field(sum,n->name);relation_digest_field(sum,n->path);relation_digest_field(sum,n->text);relation_digest_field(sum,n->anchor ? n->anchor->name:NULL);relation_digest_field(sum,n->anchor ? n->anchor->scope:NULL);if(n->languages) for(guint k=0;n->languages[k];k++) relation_digest_field(sum,n->languages[k]);relation_digest_field(sum,"/"); }gchar *result=g_strdup(g_checksum_get_string(sum));g_checksum_free(sum);return result;
}
static gboolean relation_notes_current(RelationNoteJob *j) {
 if(!wb.alive || j->epoch!=workbench_epoch || !notes_scope_current(INSIGHT_BOOKMARK,j->scope)) return FALSE;
 gchar *store=relation_notes_digest(notes_for(INSIGHT_BOOKMARK));gboolean same_store=!strcmp(store,j->store_digest);g_free(store);if(!same_store) return FALSE;
 RelationSnapshot *current=relation_snapshot_new();gboolean valid=current && !strcmp(current->text->str,j->outline);relation_snapshot_free(current);if(!valid) return FALSE;
 GHashTable *checked=g_hash_table_new(g_str_hash,g_str_equal);for(guint i=0;i<j->rows->len && valid;i++) { RelationNoteRow *r=g_ptr_array_index(j->rows,i);if(g_hash_table_contains(checked,r->path)) continue;g_hash_table_add(checked,r->path);GeanyDocument *doc=document_find_by_filename(r->path);
  if(r->source) { if(!doc || doc->id!=r->id || sci_get_length(doc->editor->sci)>2*1024*1024) valid=FALSE;else { gchar *text=sci_get_contents(doc->editor->sci,-1),*digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);valid=!strcmp(digest,r->digest);g_free(text);g_free(digest); } }else if(doc) valid=FALSE;
 }
 g_hash_table_unref(checked);GHashTableIter it;gpointer path,version;g_hash_table_iter_init(&it,j->versions);while(valid && g_hash_table_iter_next(&it,&path,&version)) { gchar *current_version=relation_file_version(path,NULL);valid=current_version && !strcmp(current_version,version);g_free(current_version); }return valid;
}
static void relation_notes_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)data;RelationReport *s=g_object_get_data(source,"insight-relation-report");RelationNoteJob *j=g_task_get_task_data(G_TASK(result));GError *error=NULL;gboolean ok=g_task_propagate_boolean(G_TASK(result),&error);
 if(s->closed) { g_clear_error(&error);return; }s->busy=FALSE;g_object_set_data(source,"insight-relation-busy",NULL);g_clear_object(&s->cancel);
 for(gint response=1;response<=5;response++) gtk_dialog_set_response_sensitive(GTK_DIALOG(source),response,TRUE);
 if(ok && !relation_notes_current(j)) { ok=FALSE;g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_BUSY,"Relations, source or bookmark store changed; refresh before creating bookmarks"); }
 if(ok) { InsightNotes *candidate=j->candidate;j->candidate=NULL;ok=notes_commit(candidate,INSIGHT_BOOKMARK); }
 if(ok) { gchar *text=g_strdup_printf("%u bookmarks created; existing locations were kept",j->rows->len);gtk_label_set_text(GTK_LABEL(s->status),text);g_free(text); }else gtk_label_set_text(GTK_LABEL(s->status),error ? error->message:"Bookmarks were not saved");g_clear_error(&error);
}
static void relation_notes_start(RelationReport *s) {
 if(!s->snapshot || !bookmarks_writable()) return;if(s->epoch!=workbench_epoch || !notes_scope_current(INSIGHT_BOOKMARK,s->scope)) { gtk_label_set_text(GTK_LABEL(s->status),"Relations changed; refresh the outline first");return; }insight_workbench_bookmarks_changed();
 RelationSnapshot *current=relation_snapshot_new();gboolean fresh=current && !strcmp(current->text->str,s->snapshot->text->str);relation_snapshot_free(current);if(!fresh) { gtk_label_set_text(GTK_LABEL(s->status),"Relations changed; refresh the outline first");return; }
 GError *error=NULL;RelationNoteJob *j=g_new0(RelationNoteJob,1);j->rows=g_ptr_array_new_with_free_func(relation_note_row_free);j->versions=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);j->candidate=notes_clone(INSIGHT_BOOKMARK,&error);j->outline=g_strdup(s->snapshot->text->str);j->epoch=workbench_epoch;j->scope=notes_generation(INSIGHT_BOOKMARK);j->store_digest=relation_notes_digest(notes_for(INSIGHT_BOOKMARK));j->encoding=file_prefs.default_open_encoding;
 GHashTable *seen=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);InsightNotes *notes=notes_for(INSIGHT_BOOKMARK);for(guint i=0;i<notes->items->len;i++) { InsightNote *n=g_ptr_array_index(notes->items,i);if(n->kind==INSIGHT_BOOKMARK) g_hash_table_add(seen,relation_location_key(n->path,(gulong)n->line)); }
 GHashTable *opened=g_hash_table_new(g_str_hash,g_str_equal);gsize bytes=0;for(guint i=0;j->candidate && !error && i<s->snapshot->rows->len;i++) {
  RelationNoteRow *r=g_ptr_array_index(s->snapshot->rows,i);gchar *key=relation_location_key(r->path,r->line);if(g_hash_table_contains(seen,key)) { g_free(key);continue; }g_hash_table_add(seen,key);
  if(j->candidate->items->len+j->rows->len>=64) { g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"The batch exceeds the remaining note capacity (64 total); no bookmarks were created");break; }
  RelationNoteRow *copy=g_new0(RelationNoteRow,1);copy->name=g_strdup(r->name);copy->path=g_strdup(r->path);copy->line=r->line;GeanyDocument *doc=document_find_by_filename(r->path);
  if(doc) { if(sci_get_length(doc->editor->sci)>2*1024*1024) { relation_note_row_free(copy);g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"A source exceeds 2 MiB; no bookmarks were created");break; }copy->id=doc->id;RelationNoteRow *first=g_hash_table_lookup(opened,r->path);if(first) { copy->source=first->source;copy->digest=g_strdup(first->digest);copy->borrowed=TRUE; }else { copy->source=sci_get_contents(doc->editor->sci,-1);if(strlen(copy->source)!=(gsize)sci_get_length(doc->editor->sci) || !g_utf8_validate(copy->source,-1,NULL)) { relation_note_row_free(copy);g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Source contains NUL or invalid UTF-8; no bookmarks were created");break; }bytes+=strlen(copy->source);copy->digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,copy->source,-1);g_hash_table_insert(opened,copy->path,copy); } }
  g_ptr_array_add(j->rows,copy);if(bytes>16u*1024u*1024u) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Source snapshots exceed 16 MiB; no bookmarks were created");
 }
 g_hash_table_unref(opened);g_hash_table_unref(seen);if(error || !j->candidate || !j->rows->len) { gtk_label_set_text(GTK_LABEL(s->status),error ? error->message:"No new source locations; existing bookmarks were kept");g_clear_error(&error);relation_note_job_free(j);return; }
 s->busy=TRUE;s->cancel=g_cancellable_new();g_object_set_data(G_OBJECT(s->dialog),"insight-relation-busy",GINT_TO_POINTER(1));for(gint response=1;response<=5;response++) gtk_dialog_set_response_sensitive(GTK_DIALOG(s->dialog),response,FALSE);gtk_label_set_text(GTK_LABEL(s->status),"Preparing bookmarks from source snapshots…");
 GTask *task=g_task_new(s->dialog,s->cancel,relation_notes_done,NULL);g_task_set_task_data(task,j,relation_note_job_free);g_task_run_in_thread(task,relation_notes_run);g_object_unref(task);
}
static void relation_report_refresh(RelationReport *s) {
 s->epoch=workbench_epoch;s->scope=notes_generation(INSIGHT_BOOKMARK);relation_snapshot_free(s->snapshot);s->snapshot=relation_snapshot_new();gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->preview)),s->snapshot ? s->snapshot->text->str:"",-1);
 gchar *text=s->snapshot ? g_strdup_printf("%u source items. Loaded/filtered rows only; unloaded branches are not fetched. Existing bookmark locations are skipped.",s->snapshot->rows->len):g_strdup("Wait for Relations to finish, or reduce results (2000 source items / 1 MiB outline)");gchar *destination=g_strconcat(text,"\n",gtk_label_get_text(GTK_LABEL(wb.marks_scope)),NULL);gtk_label_set_text(GTK_LABEL(s->status),destination);g_free(destination);g_free(text);
 for(gint response=2;response<=5;response++) gtk_dialog_set_response_sensitive(GTK_DIALOG(s->dialog),response,s->snapshot!=NULL);
}
static void relation_report_show(GtkWidget *widget,gpointer data) {
 (void)widget;RelationReport *s=g_new0(RelationReport,1);s->dialog=gtk_dialog_new_with_buttons(insight_tr(GPOINTER_TO_INT(data) ? "Create Bookmarks from Relation Items":"View Relation Outline"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Close"),GTK_RESPONSE_CLOSE,insight_tr("Refresh"),1,insight_tr("Copy outline"),2,insight_tr("Open outline buffer"),3,insight_tr("Create bookmarks"),4,insight_tr("Print outline…"),5,NULL);g_object_ref_sink(s->dialog);gtk_widget_set_name(s->dialog,"insight-relation-report");gtk_window_set_default_size(GTK_WINDOW(s->dialog),820,520);g_object_set_data_full(G_OBJECT(s->dialog),"insight-relation-report",s,relation_report_free);g_signal_connect(s->dialog,"destroy",G_CALLBACK(relation_report_destroy),s);
 const gchar *names[]={"insight-relation-report-refresh","insight-relation-report-copy","insight-relation-report-open","insight-relation-report-create","insight-relation-report-print"};for(gint i=0;i<5;i++) gtk_widget_set_name(gtk_dialog_get_widget_for_response(GTK_DIALOG(s->dialog),i+1),names[i]);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(s->dialog)),*scroll=gtk_scrolled_window_new(NULL,NULL);s->preview=gtk_text_view_new();gtk_widget_set_name(s->preview,"insight-relation-report-text");gtk_text_view_set_editable(GTK_TEXT_VIEW(s->preview),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(s->preview),TRUE);g_object_set_data(G_OBJECT(s->preview),"insight-no-i18n",GINT_TO_POINTER(1));gtk_container_add(GTK_CONTAINER(scroll),s->preview);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,4);s->status=gtk_label_new("");gtk_widget_set_name(s->status,"insight-relation-report-status");gtk_label_set_line_wrap(GTK_LABEL(s->status),TRUE);gtk_label_set_selectable(GTK_LABEL(s->status),TRUE);gtk_box_pack_start(GTK_BOX(box),s->status,FALSE,FALSE,4);insight_panel_help_attach(box,"relation-results.md");relation_report_refresh(s);gtk_widget_show_all(s->dialog);
 for(;;) { gint response=gtk_dialog_run(GTK_DIALOG(s->dialog));if(response<0 || s->closed) break;if(s->busy) continue;if(response==1) relation_report_refresh(s);else if(response==2 && s->snapshot) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),s->snapshot->text->str,(gint)s->snapshot->text->len);else if(response==3 && s->snapshot) { GeanyDocument *doc=document_new_file(NULL,NULL,s->snapshot->text->str);if(doc) document_set_text_changed(doc,TRUE); }else if(response==4) relation_notes_start(s);else if(response==5 && s->snapshot) insight_text_print_run(insight_text_print_operation(insight_tr("Relations"),s->snapshot->text->str,s->snapshot->text->len),GTK_WINDOW(s->dialog)); }
 GtkWidget *dialog=s->dialog;gtk_widget_destroy(dialog);g_object_unref(dialog);
}

static void bookmark_delete_names(GPtrArray *names,gboolean all) {
 if(!names->len || !bookmarks_writable()) return;
 guint64 generation=notes_generation(INSIGHT_BOOKMARK);
 if(all || wb.marks_confirm_delete) {
  GtkWidget *dialog=gtk_message_dialog_new(GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,GTK_MESSAGE_QUESTION,GTK_BUTTONS_NONE,"%s",insight_tr(all ? "Delete all bookmarks?" : "Delete selected bookmarks?"));
  gtk_window_set_title(GTK_WINDOW(dialog),insight_tr("Delete bookmarks"));gtk_widget_set_name(dialog,"insight-confirm-bookmark-delete");
  gchar *text=g_strdup_printf(insight_tr("Bookmarks to delete: %u. Source files and clips are kept."),names->len);gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),"%s",text);g_free(text);
  gtk_dialog_add_buttons(GTK_DIALOG(dialog),insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Delete"),GTK_RESPONSE_ACCEPT,NULL);gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);
  gint response=gtk_dialog_run(GTK_DIALOG(dialog));gtk_widget_destroy(dialog);if(response!=GTK_RESPONSE_ACCEPT || !notes_scope_current(INSIGHT_BOOKMARK,generation)) return;
 }
 GError *error=NULL;InsightNotes *candidate=notes_clone(INSIGHT_BOOKMARK,&error);
 if(candidate) { for(guint i=0;i<names->len;i++) insight_notes_remove(candidate,INSIGHT_BOOKMARK,g_ptr_array_index(names,i));notes_commit(candidate,INSIGHT_BOOKMARK); }
 if(error) ui_set_statusbar(TRUE,"Note: %s",error->message);
 g_clear_error(&error);
}
static void bookmark_delete_all(void) {
 GPtrArray *names=g_ptr_array_new_with_free_func(g_free);InsightNotes *source=notes_for(INSIGHT_BOOKMARK);
 for(guint i=0;i<source->items->len;i++) { InsightNote *note=g_ptr_array_index(source->items,i);if(note->kind==INSIGHT_BOOKMARK) g_ptr_array_add(names,g_strdup(note->name)); }
 bookmark_delete_names(names,TRUE);g_ptr_array_unref(names);
}
static void note_delete(GtkWidget *widget,gpointer data) {
 (void)widget;InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data);GPtrArray *names=selected_notes(kind);
 if(kind==INSIGHT_BOOKMARK) bookmark_delete_names(names,FALSE);
 else if(names->len) { GError *error=NULL;InsightNotes *candidate=notes_clone(kind,&error);
  if(candidate) { for(guint i=0;i<names->len;i++) insight_notes_remove(candidate,kind,g_ptr_array_index(names,i));notes_commit(candidate,kind); }
  if(error) ui_set_statusbar(TRUE,"Note: %s",error->message);
  g_clear_error(&error);
 }g_ptr_array_unref(names);
}
static void bookmark_go(GtkWidget *widget,gpointer data) {
 (void)data;const gchar *query=gtk_entry_get_text(GTK_ENTRY(wb.marks_entry));gchar *name=NULL;
 if(widget==wb.marks_entry) {
  if(!*query) { bookmark_add(NULL,NULL);return; }
  if(insight_notes_get(notes_for(INSIGHT_BOOKMARK),INSIGHT_BOOKMARK,query)) name=g_strdup(query);
  else if(strspn(query,"0123456789")==strlen(query)) {
   guint64 line=g_ascii_strtoull(query,NULL,10);GeanyDocument *doc=document_get_current();
   if(doc && line>0 && line<=(guint64)sci_get_line_count(doc->editor->sci)) { navqueue_goto_line(doc,doc,(gint)line);SSM(doc->editor->sci,SCI_VERTICALCENTRECARET,0,0);document_grab_focus(doc); }
   else ui_set_statusbar(TRUE,"Bookmark line number is outside the current file");
   return;
  } else {
   InsightNotes *source=notes_for(INSIGHT_BOOKMARK);guint count=0;
   for(guint i=0;i<source->items->len;i++) { InsightNote *note=g_ptr_array_index(source->items,i);if(note->kind==INSIGHT_BOOKMARK && insight_lookup_text_match(note->name,query)) { count++;g_free(name);name=g_strdup(note->name); } }
   if(count>1) { g_free(name);ui_set_statusbar(TRUE,"Several bookmarks match; select one in the list");return; }
  }
 } else { name=selected_note(INSIGHT_BOOKMARK);if(!name) name=bookmark_cursor_note();if(!name && gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.marks_view)))>1) return;
  if(!name && insight_notes_get(notes_for(INSIGHT_BOOKMARK),INSIGHT_BOOKMARK,query)) name=g_strdup(query);
 }
 if(name) { bookmark_open_note(insight_notes_get(notes_for(INSIGHT_BOOKMARK),INSIGHT_BOOKMARK,name));g_free(name); }else bookmark_add(NULL,NULL);
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
 InsightNotes *candidate=notes_clone(INSIGHT_CLIP,&error);gboolean saved=FALSE;
 if(candidate) { if(insight_notes_set(candidate,INSIGHT_CLIP,name,NULL,0,text,&error)) saved=notes_commit(candidate,INSIGHT_CLIP);else insight_notes_free(candidate); }
 if(cut && saved) { sci_start_undo_action(doc->editor->sci); sci_replace_sel(doc->editor->sci,""); sci_end_undo_action(doc->editor->sci); }
 if(error) ui_set_statusbar(TRUE,"Clip: %s",error->message);
 g_clear_error(&error); g_free(text); g_free(name);
}
static void clip_paste(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; gchar *name=selected_note(INSIGHT_CLIP); const InsightNote *note=name ? insight_notes_get(notes_for(INSIGHT_CLIP),INSIGHT_CLIP,name) : NULL; GeanyDocument *doc=document_get_current();
 if(note && doc && !SSM(doc->editor->sci,SCI_GETREADONLY,0,0)) { if(!insight_paste_text(doc->editor->sci,note->text)) { sci_start_undo_action(doc->editor->sci); sci_replace_sel(doc->editor->sci,note->text); sci_end_undo_action(doc->editor->sci); } document_grab_focus(doc); }
 else ui_set_statusbar(TRUE,"Select a clip and a writable source document");
 g_free(name);
}
static void clip_insert_template(const InsightNote *note) {
 guint64 generation=wb.clips_generation;
 GeanyDocument *doc=document_get_current();
 if(!doc || !note || SSM(doc->editor->sci,SCI_GETREADONLY,0,0)) { ui_set_statusbar(TRUE,"Select a clip and a writable source document"); return; }
 ScintillaObject *sci=doc->editor->sci;
 if(sci_get_selection_mode(sci)!=SC_SEL_STREAM || SSM(sci,SCI_GETSELECTIONS,0,0)!=1) { ui_set_statusbar(TRUE,"Template insertion requires a single normal selection"); return; }
 if(sci_get_selected_text_length2(sci)>65536) { ui_set_statusbar(TRUE,"Clip selection exceeds 64 KiB"); return; }
 gchar *language=clip_current_language(); gboolean compatible=insight_note_matches_language(note,language); g_free(language);
 if(!compatible) { ui_set_statusbar(TRUE,"This template is not available for the current language profile"); return; }
 gchar **allowed_languages=g_strdupv(note->languages); gchar *body=g_strdup(note->text),*selection=sci_get_selection_contents(sci); GError *error=NULL;
 if(!selection || strlen(selection)!=(gsize)sci_get_selected_text_length2(sci)) { g_free(selection); g_free(body); g_strfreev(allowed_languages); ui_set_statusbar(TRUE,"Clips cannot contain NUL bytes"); return; }
 guint id=doc->id; guint64 version=selection_state(sci)->text_version; gint begin=sci_get_selection_start(sci),end=sci_get_selection_end(sci),eol_mode=sci_get_eol_mode(sci);
 GHashTable *values=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free); GDateTime *now=g_date_time_new_now_local();
 g_hash_table_insert(values,g_strdup("date"),g_date_time_format(now,"%Y-%m-%d")); g_hash_table_insert(values,g_strdup("time"),g_date_time_format(now,"%H:%M:%S")); g_date_time_unref(now);
 g_hash_table_insert(values,g_strdup("file"),g_strdup(doc->file_name ? doc->file_name : "")); g_hash_table_insert(values,g_strdup("filename"),doc->file_name ? g_path_get_basename(doc->file_name) : g_strdup(""));
 g_hash_table_insert(values,g_strdup("project"),g_strdup(app->project && app->project->name ? app->project->name : "")); g_hash_table_insert(values,g_strdup("selection"),selection);
 GPtrArray *variables=insight_snippet_variables(body,&error); gboolean accepted=variables!=NULL,keep_fields=FALSE; GtkWidget *dialog=NULL,*editable=NULL; GPtrArray *entries=g_ptr_array_new();
 if(variables) {
  for(guint i=0;i<variables->len;i++) { const gchar *variable=g_ptr_array_index(variables,i); if(g_hash_table_contains(values,variable)) continue;
   if(!dialog) { dialog=gtk_dialog_new_with_buttons("Template parameters",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Insert",GTK_RESPONSE_ACCEPT,NULL); gtk_window_set_default_size(GTK_WINDOW(dialog),500,350); GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL),*grid=gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(grid),6); gtk_grid_set_column_spacing(GTK_GRID(grid),12); gtk_container_set_border_width(GTK_CONTAINER(grid),12); gtk_container_add(GTK_CONTAINER(scroll),grid); gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),scroll,TRUE,TRUE,0); g_object_set_data(G_OBJECT(dialog),"grid",grid); }
   GtkWidget *grid=g_object_get_data(G_OBJECT(dialog),"grid"),*label=gtk_label_new(variable),*entry=gtk_entry_new(); g_object_set_data(G_OBJECT(label),"insight-no-i18n",GINT_TO_POINTER(1)); gchar *widget_name=g_strconcat("insight-template-variable-",variable,NULL); gtk_widget_set_name(entry,widget_name); g_free(widget_name); gtk_entry_set_max_length(GTK_ENTRY(entry),65536); gtk_entry_set_activates_default(GTK_ENTRY(entry),TRUE); gtk_widget_set_hexpand(entry,TRUE); g_object_set_data_full(G_OBJECT(entry),"variable",g_strdup(variable),g_free); gtk_grid_attach(GTK_GRID(grid),label,0,(gint)entries->len,1,1); gtk_grid_attach(GTK_GRID(grid),entry,1,(gint)entries->len,1,1); g_ptr_array_add(entries,entry);
  }
  if(dialog) { editable=gtk_check_button_new_with_label("Keep template fields editable (Tab / Shift+Tab)");gtk_widget_set_name(editable,"insight-template-editable-fields");gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),editable,FALSE,FALSE,6);gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT); gtk_widget_show_all(dialog); gtk_widget_grab_focus(g_ptr_array_index(entries,0)); accepted=gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT;
   if(accepted) for(guint i=0;i<entries->len;i++) { GtkWidget *entry=g_ptr_array_index(entries,i); g_hash_table_insert(values,g_strdup(g_object_get_data(G_OBJECT(entry),"variable")),g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)))); }
   keep_fields=accepted && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(editable));gtk_widget_destroy(dialog);
  }
 }
 if(accepted && notes_scope_current(INSIGHT_CLIP,generation)) {
  doc=document_find_by_id(id);
  gchar *current_language=clip_current_language(); InsightNote allowed_snapshot={.languages=allowed_languages}; gboolean language_allowed=insight_note_matches_language(&allowed_snapshot,current_language); g_free(current_language);
  if(!language_allowed) ui_set_statusbar(TRUE,"This template is not available for the current language profile");
  else if(!doc || doc!=document_get_current() || selection_state(doc->editor->sci)->text_version!=version || sci_get_selection_start(doc->editor->sci)!=begin || sci_get_selection_end(doc->editor->sci)!=end || sci_get_selection_mode(doc->editor->sci)!=SC_SEL_STREAM || SSM(doc->editor->sci,SCI_GETSELECTIONS,0,0)!=1 || SSM(doc->editor->sci,SCI_GETREADONLY,0,0) || sci_get_eol_mode(doc->editor->sci)!=eol_mode) ui_set_statusbar(TRUE,"Source changed while entering template parameters; insert again");
  else { gsize cursor=0;GArray *fields=NULL;GHashTable *editable_names=g_hash_table_new(g_str_hash,g_str_equal);
   if(keep_fields) for(guint i=0;i<variables->len;i++) { const gchar *variable=g_ptr_array_index(variables,i);if(strcmp(variable,"date") && strcmp(variable,"time") && strcmp(variable,"file") && strcmp(variable,"filename") && strcmp(variable,"project") && strcmp(variable,"selection")) g_hash_table_add(editable_names,(gpointer)variable); }
   gchar *text=insight_snippet_expand_fields(body,values,eol_mode==SC_EOL_CRLF ? "\r\n" : (eol_mode==SC_EOL_CR ? "\r" : "\n"),editable_names,&cursor,&fields,&error);g_hash_table_unref(editable_names);
   if(text) { sci=doc->editor->sci;insight_template_fields_begin(sci,NULL,0,0); sci_start_undo_action(sci); sci_replace_sel(sci,text); sci_set_current_position(sci,begin+(gint)cursor,FALSE); sci_end_undo_action(sci);if(keep_fields) insight_template_fields_begin(sci,fields,(gsize)begin,cursor); document_grab_focus(doc); g_free(text); }
   g_clear_pointer(&fields,g_array_unref);
  }
 }
 if(error) ui_set_statusbar(TRUE,"Template: %s",insight_tr(error->message));
 g_clear_error(&error); g_clear_pointer(&variables,g_ptr_array_unref); g_ptr_array_unref(entries); g_hash_table_unref(values); g_strfreev(allowed_languages); g_free(body);
}
static void clip_template(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;gchar *name=selected_note(INSIGHT_CLIP);const InsightNote *note=name ? insight_notes_get(notes_for(INSIGHT_CLIP),INSIGHT_CLIP,name) : NULL;clip_insert_template(note);g_free(name);
}
void insight_workbench_template_selected(GeanyEditor *editor,const gchar *name) {
 if(!wb.template_pending || !editor) return;
 wb.template_pending=FALSE;const InsightFiletypeOptions *typing=insight_languages_editing_options(editor);if(typing && !typing->allow_snippets) return;GeanyDocument *doc=document_get_current();ScintillaObject *sci=editor->sci;
 if(!doc || doc->editor!=editor || doc->id!=wb.template_doc || wb.template_generation!=wb.clips_generation || selection_state(sci)->text_version!=wb.template_version || sci_get_current_position(sci)!=wb.template_end || sci_get_selection_start(sci)!=wb.template_end || sci_get_selection_end(sci)!=wb.template_end || SSM(sci,SCI_GETREADONLY,0,0)) return;
 gchar *prefix=sci_get_contents_range(sci,wb.template_begin,wb.template_end);gboolean unchanged=!g_strcmp0(prefix,wb.template_prefix);g_free(prefix);if(!unchanged || !name || !g_str_has_prefix(name,wb.template_prefix)) return;
 const InsightNote *note=insight_notes_get(notes_for(INSIGHT_CLIP),INSIGHT_CLIP,name);gchar *language=clip_current_language();gboolean eligible=insight_note_matches_language(note,language);g_free(language);if(!eligible) return;
 gint before=wb.template_end;guint id=doc->id;guint64 version=wb.template_version;
 SSM(sci,SCI_SETSEL,wb.template_begin,wb.template_end);clip_insert_template(note);
 doc=document_find_by_id(id);if(doc && doc==document_get_current() && selection_state(doc->editor->sci)->text_version==version) sci_set_current_position(doc->editor->sci,before,FALSE);
}
gboolean insight_workbench_complete_template(GeanyEditor *editor,gint position) {
 if(!wb.alive || !wb.notes || !editor) return FALSE;
 const InsightFiletypeOptions *typing=insight_languages_editing_options(editor);if(typing && !typing->allow_snippets) return FALSE;
 GeanyDocument *doc=document_get_current();ScintillaObject *sci=editor->sci;
 if(!doc || doc->editor!=editor || SSM(sci,SCI_GETREADONLY,0,0) || sci_get_selection_start(sci)!=position || sci_get_selection_end(sci)!=position || SSM(sci,SCI_GETSELECTIONS,0,0)!=1 || position<1) return FALSE;
 gint begin=(gint)SSM(sci,SCI_WORDSTARTPOSITION,position,TRUE);if(position-begin<1 || position-begin>256 || SSM(sci,SCI_WORDENDPOSITION,position,TRUE)>position) return FALSE;
 gchar *prefix=sci_get_contents_range(sci,begin,position);if(!prefix || !g_utf8_validate(prefix,-1,NULL)) { g_free(prefix);return FALSE; }
 GString *names=g_string_new(NULL);const InsightNote *exact=NULL;gchar *language=clip_current_language();InsightNotes *notes=notes_for(INSIGHT_CLIP);
 for(guint i=0;i<notes->items->len;i++) { InsightNote *note=g_ptr_array_index(notes->items,i);if(note->kind!=INSIGHT_CLIP || !g_str_has_prefix(note->name,prefix) || strpbrk(note->name,"\n\r\t") || !insight_note_matches_language(note,language)) continue;if(names->len) g_string_append_c(names,'\n');g_string_append(names,note->name);if(!strcmp(note->name,prefix)) exact=note; }
 g_free(language);if(!names->len) { g_string_free(names,TRUE);g_free(prefix);return FALSE; }
 wb.template_pending=TRUE;wb.template_doc=doc->id;wb.template_begin=begin;wb.template_end=position;wb.template_version=selection_state(sci)->text_version;wb.template_generation=wb.clips_generation;g_free(wb.template_prefix);wb.template_prefix=prefix;
 if(exact) insight_workbench_template_selected(editor,exact->name);
 else { gint separator=(gint)SSM(sci,SCI_AUTOCGETSEPARATOR,0,0);SSM(sci,SCI_AUTOCSETSEPARATOR,'\n',0);SSM(sci,SCI_USERLISTSHOW,INSIGHT_TEMPLATE_COMPLETION_LIST,(sptr_t)names->str);SSM(sci,SCI_AUTOCSETSEPARATOR,separator,0); }
 g_string_free(names,TRUE);return TRUE;
}
static void template_language_toggled(GtkCellRendererToggle *renderer,gchar *path,gpointer data) {
 (void)renderer; GtkListStore *store=data; GtkTreeIter iter; if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store),&iter,path)) { gboolean active=FALSE; gtk_tree_model_get(GTK_TREE_MODEL(store),&iter,0,&active,-1); gtk_list_store_set(store,&iter,0,!active,-1); }
}
static GtkWidget *template_language_editor(GtkWidget *box,const InsightNote *note) {
 GtkListStore *store=gtk_list_store_new(2,G_TYPE_BOOLEAN,G_TYPE_STRING); GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store)); gtk_widget_set_name(view,"insight-template-languages");
 GtkCellRenderer *toggle=gtk_cell_renderer_toggle_new(); g_signal_connect_object(toggle,"toggled",G_CALLBACK(template_language_toggled),store,0); gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Allowed",toggle,"active",0,NULL)); gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Language profile",gtk_cell_renderer_text_new(),"text",1,NULL));
 gchar **names=insight_languages_names(); GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);
 for(guint pass=0;pass<2;pass++) { gchar **list=pass ? (note ? note->languages : NULL) : names;
  for(guint i=0;list && list[i];i++) { if(g_hash_table_contains(seen,list[i])) continue; gboolean active=FALSE; for(guint k=0;note && note->languages && note->languages[k];k++) if(!strcmp(note->languages[k],list[i])) active=TRUE;
   GtkTreeIter iter; gtk_list_store_append(store,&iter); gtk_list_store_set(store,&iter,0,active,1,list[i],-1); g_hash_table_add(seen,list[i]);
  }
 }
 g_hash_table_unref(seen); g_strfreev(names); g_object_unref(store); GtkWidget *label=gtk_label_new("Allowed language profiles — no selection means all languages"),*scroll=gtk_scrolled_window_new(NULL,NULL); gtk_label_set_line_wrap(GTK_LABEL(label),TRUE); gtk_box_pack_start(GTK_BOX(box),label,FALSE,FALSE,4); gtk_widget_set_size_request(scroll,-1,120); gtk_container_add(GTK_CONTAINER(scroll),view); gtk_box_pack_start(GTK_BOX(box),scroll,FALSE,TRUE,0); return view;
}
static gchar **template_language_values(GtkWidget *view) {
 GPtrArray *values=g_ptr_array_new(); GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view)); GtkTreeIter iter;
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gboolean active=FALSE; gchar *name=NULL; gtk_tree_model_get(model,&iter,0,&active,1,&name,-1); if(active) g_ptr_array_add(values,name); else g_free(name); }
 g_ptr_array_add(values,NULL); return (gchar **)g_ptr_array_free(values,FALSE);
}
static gboolean notes_commit(InsightNotes *,InsightNoteKind);
static void clip_new_template(GtkWidget *widget,gpointer data) {
 guint64 generation=wb.clips_generation;
 (void)widget; (void)data; GtkWidget *dialog=gtk_dialog_new_with_buttons("New code template",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Save",GTK_RESPONSE_ACCEPT,NULL),*box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)),*name=gtk_entry_new(),*view=gtk_text_view_new(),*scroll=gtk_scrolled_window_new(NULL,NULL);
 gtk_widget_set_name(name,"insight-template-name"); gtk_widget_set_name(view,"insight-template-body"); gtk_entry_set_max_length(GTK_ENTRY(name),256); gtk_entry_set_placeholder_text(GTK_ENTRY(name),"Template name"); gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),"// $date$ — $filename$\n$selection$$cursor$",-1); gtk_container_add(GTK_CONTAINER(scroll),view); gtk_widget_set_size_request(scroll,600,300); gtk_box_pack_start(GTK_BOX(box),name,FALSE,FALSE,6); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0); GtkWidget *hint=gtk_label_new("Variables: $date$, $time$, $file$, $filename$, $project$, $selection$, $cursor$. Custom: $name$. Literal dollar: $$."); gtk_label_set_line_wrap(GTK_LABEL(hint),TRUE); gtk_label_set_max_width_chars(GTK_LABEL(hint),70); gtk_box_pack_start(GTK_BOX(box),hint,FALSE,FALSE,6); GtkWidget *languages=template_language_editor(box,NULL); gtk_widget_show_all(dialog);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT && notes_scope_current(INSIGHT_CLIP,generation)) {
  GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)); GtkTextIter a,b; gtk_text_buffer_get_bounds(buffer,&a,&b); gchar *text=gtk_text_buffer_get_text(buffer,&a,&b,FALSE); const gchar *key=gtk_entry_get_text(GTK_ENTRY(name)); GError *error=NULL;
  if(insight_notes_get(notes_for(INSIGHT_CLIP),INSIGHT_CLIP,key)) ui_set_statusbar(TRUE,"A clip with this name already exists");
  else { InsightNotes *added=insight_notes_new();
   gchar **allowed=template_language_values(languages);
   if(insight_notes_set(added,INSIGHT_CLIP,key,NULL,0,text,&error) && insight_notes_set_languages(added,key,(const gchar *const *)allowed,&error)) { InsightNotes *candidate=insight_notes_merge(notes_for(INSIGHT_CLIP),added,INSIGHT_CLIP,&error); if(candidate) notes_commit(candidate,INSIGHT_CLIP); }
   g_strfreev(allowed); insight_notes_free(added);
  }
  if(error) ui_set_statusbar(TRUE,"Template: %s",insight_tr(error->message));
  g_clear_error(&error); g_free(text);
 }
 gtk_widget_destroy(dialog);
}
static void clip_edit(GtkWidget *widget,gpointer data) {
 guint64 generation=wb.clips_generation;
 (void)widget; (void)data; gchar *name=selected_note(INSIGHT_CLIP); const InsightNote *note=name ? insight_notes_get(notes_for(INSIGHT_CLIP),INSIGHT_CLIP,name) : NULL; if(!note) { g_free(name); return; }
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Edit clip",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Save",GTK_RESPONSE_ACCEPT,NULL),*view=gtk_text_view_new(),*scroll=gtk_scrolled_window_new(NULL,NULL);
 gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),note->text,-1); gtk_container_add(GTK_CONTAINER(scroll),view); gtk_widget_set_size_request(scroll,600,350); gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),scroll); GtkWidget *languages=template_language_editor(gtk_dialog_get_content_area(GTK_DIALOG(dialog)),note); gtk_widget_show_all(dialog);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT && notes_scope_current(INSIGHT_CLIP,generation)) { GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)); GtkTextIter a,b; gtk_text_buffer_get_bounds(buffer,&a,&b); gchar *text=gtk_text_buffer_get_text(buffer,&a,&b,FALSE); GError *error=NULL;
  InsightNotes *candidate=insight_notes_rename(notes_for(INSIGHT_CLIP),INSIGHT_CLIP,name,name,&error); gchar **allowed=template_language_values(languages);
  if(candidate) { if(insight_notes_set(candidate,INSIGHT_CLIP,name,NULL,0,text,&error) && insight_notes_set_languages(candidate,name,(const gchar *const *)allowed,&error)) notes_commit(candidate,INSIGHT_CLIP); else insight_notes_free(candidate); }
  if(error) ui_set_statusbar(TRUE,"Clip: %s",insight_tr(error->message));
  g_strfreev(allowed); g_clear_error(&error); g_free(text); }
 gtk_widget_destroy(dialog); g_free(name);
}
static void note_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data); gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(view));gtk_tree_view_set_cursor(view,path,NULL,FALSE);
 if(kind==INSIGHT_BOOKMARK) bookmark_go(NULL,NULL); else clip_paste(NULL,NULL);
}
/* Commit a prepared snapshot before refreshing the panel. On write failure the
 * previous in-memory notes and selection remain available. Consumes candidate. */
static gboolean notes_commit(InsightNotes *candidate,InsightNoteKind kind) {
 if(kind==INSIGHT_BOOKMARK && !bookmarks_writable()) { insight_notes_free(candidate);return FALSE; }
 InsightNotes **slot=kind==INSIGHT_BOOKMARK && wb.marks_project ? &wb.project_marks : kind==INSIGHT_CLIP && wb.clips_project ? &wb.project_notes : &wb.notes;InsightNotes *previous=*slot;gboolean previous_failure=wb.marks_save_failed;*slot=candidate;
 if(!notes_save_kind(kind)) { *slot=previous;if(kind==INSIGHT_BOOKMARK) wb.marks_save_failed=previous_failure; insight_notes_free(candidate); return FALSE; }
 insight_notes_free(previous);
 if(kind==INSIGHT_BOOKMARK) { bookmark_clear_handles();for(guint i=0;i<documents_array->len;i++) bookmark_restore_document(g_ptr_array_index(documents_array,i));bookmark_list_reload(NULL,NULL); }
 notes_filter(NULL,GINT_TO_POINTER(kind));return TRUE;
}
/* document-close is emitted after the unsaved-file prompt accepts and before
 * the buffer is destroyed. Do not run the interactive deletion command here. */
static void bookmark_file_closed(GeanyDocument *doc) {
 if(!wb.alive || main_status.quitting || !wb.marks_delete_on_close || !doc || !doc->is_valid || !doc->file_name || !wb.notes || !bookmarks_writable()) return;
 InsightNotes *source=notes_for(INSIGHT_BOOKMARK);gboolean found=FALSE;
 for(guint i=0;i<source->items->len;i++) { InsightNote *note=g_ptr_array_index(source->items,i);if(note->kind==INSIGHT_BOOKMARK && !g_strcmp0(note->path,doc->file_name)) { found=TRUE;break; } }
 if(!found) return;
 GError *error=NULL;InsightNotes *candidate=notes_clone(INSIGHT_BOOKMARK,&error);
 if(candidate) {
  for(guint i=candidate->items->len;i>0;i--) { InsightNote *note=g_ptr_array_index(candidate->items,i-1);if(note->kind==INSIGHT_BOOKMARK && !g_strcmp0(note->path,doc->file_name)) insight_notes_remove(candidate,INSIGHT_BOOKMARK,note->name); }
  /* Commit clears registered handles. Also clear any native bookmark markers
   * so a reentrant refresh cannot capture the closing file's marks again. */
  if(notes_commit(candidate,INSIGHT_BOOKMARK)) SSM(doc->editor->sci,SCI_MARKERDELETEALL,1,0);
 }
 if(error) ui_set_statusbar(TRUE,"Note: %s",error->message);
 g_clear_error(&error);
}
static void note_rename(GtkWidget *widget,gpointer data) {
 (void)widget; InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data); gchar *name=selected_note(kind);
 if(!name) { ui_set_statusbar(TRUE,"Select a note to rename"); return; }
 guint64 generation=notes_generation(kind);
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Rename note",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Rename",GTK_RESPONSE_ACCEPT,NULL),*entry=gtk_entry_new();
 gtk_entry_set_text(GTK_ENTRY(entry),name); gtk_entry_set_activates_default(GTK_ENTRY(entry),TRUE); gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT); gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),entry,FALSE,FALSE,8); gtk_widget_show(entry);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT && notes_scope_current(kind,generation)) {
  GError *error=NULL; InsightNotes *candidate=insight_notes_rename(notes_for(kind),kind,name,gtk_entry_get_text(GTK_ENTRY(entry)),&error);
  if(candidate) { if(notes_commit(candidate,kind)) ui_set_statusbar(TRUE,"Note renamed"); }
  else ui_set_statusbar(TRUE,"Rename: %s",error ? error->message : "failed");
  g_clear_error(&error);
 }
 gtk_widget_destroy(dialog); g_free(name);
}
static void notes_transfer(GtkWidget *widget,gpointer data) {
 (void)widget; gint action=GPOINTER_TO_INT(data); gboolean exporting=(action&256)!=0;
 InsightNoteKind kind=(InsightNoteKind)(action&255);
 guint64 generation=notes_generation(kind);GError *prepare_error=NULL;InsightNotes *prepared=exporting ? insight_notes_select(notes_for(kind),kind,&prepare_error) : NULL;
 if(exporting && prepared && kind==INSIGHT_BOOKMARK && !(action&512)) {
  GPtrArray *names=selected_notes(kind);GHashTable *wanted=g_hash_table_new(g_str_hash,g_str_equal);
  for(guint i=0;i<names->len;i++) g_hash_table_add(wanted,g_ptr_array_index(names,i));
  for(guint i=prepared->items->len;i>0;i--) { InsightNote *note=g_ptr_array_index(prepared->items,i-1);if(!g_hash_table_contains(wanted,note->name)) insight_notes_remove(prepared,kind,note->name); }
  g_hash_table_unref(wanted);g_ptr_array_unref(names);
  if(!prepared->items->len) { insight_notes_free(prepared);ui_set_statusbar(TRUE,"Select bookmarks to export");return; }
 }
 if(exporting && !prepared) { ui_set_statusbar(TRUE,"Notes export: %s",prepare_error ? prepare_error->message : "failed");g_clear_error(&prepare_error);return; }
 gchar *export_root=kind==INSIGHT_BOOKMARK && wb.marks_project ? g_strdup(wb.project_marks_root) : kind==INSIGHT_BOOKMARK && wb.root ? utils_get_utf8_from_locale(wb.root) : NULL;
 GtkWidget *dialog=gtk_file_chooser_dialog_new(exporting ? "Export notes" : "Import notes",GTK_WINDOW(main_widgets.window),
  exporting ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,
  exporting ? "Export" : "Import",GTK_RESPONSE_ACCEPT,NULL);
 if(exporting) { gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),TRUE); gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),kind==INSIGHT_BOOKMARK ? "bookmarks.conf" : "clips.conf"); }
 GtkFileFilter *filter=gtk_file_filter_new(); gtk_file_filter_set_name(filter,"Insight notes (*.conf)"); gtk_file_filter_add_pattern(filter,"*.conf"); gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);
 filter=gtk_file_filter_new(); gtk_file_filter_set_name(filter,"All files"); gtk_file_filter_add_pattern(filter,"*"); gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT && notes_scope_current(kind,generation)) {
  gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)); GError *error=NULL; InsightNotes *snapshot=NULL; gboolean ok=FALSE;
  if(exporting) { snapshot=prepared;prepared=NULL;ok=insight_notes_save_portable(snapshot,path,export_root,&error); }
  else {
   gchar *root=kind==INSIGHT_BOOKMARK && wb.marks_project ? g_strdup(wb.project_marks_root) : wb.root ? utils_get_utf8_from_locale(wb.root) : NULL;snapshot=insight_notes_load_portable(path,root,&error);g_free(root);
   if(snapshot) {
    InsightNotes *merged=insight_notes_merge(notes_for(kind),snapshot,kind,&error);
    if(merged) ok=notes_commit(merged,kind);
   }
  }
  if(ok) ui_set_statusbar(TRUE,exporting ? "Notes exported" : "Notes imported; existing notes preserved");
  else if(error) ui_set_statusbar(TRUE,"Notes %s: %s",exporting ? "export" : "import",error->message);
  insight_notes_free(snapshot); g_clear_error(&error); g_free(path);
 }
 gtk_widget_destroy(dialog);insight_notes_free(prepared);g_free(export_root);
}
/* Save only these options: keep layout and per-panel appearance intact. */
static gboolean bookmark_options_save(gboolean confirm,gboolean close,gboolean preserve,GError **error) {
 GKeyFile *key=g_key_file_new();GStatBuf info;gboolean ok=TRUE;
 if(!g_stat(wb.prefs_path,&info)) {
  if(info.st_size>65536 || !g_key_file_load_from_file(key,wb.prefs_path,G_KEY_FILE_KEEP_COMMENTS,error)) ok=FALSE;
  else if(g_key_file_get_integer(key,"workbench","version",NULL)!=1) ok=FALSE;
 }
 if(!ok && !*error) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Workbench settings are invalid or too large");
 if(ok) {
  g_key_file_set_integer(key,"workbench","version",1);
  g_key_file_set_boolean(key,"workbench","confirm-bookmark-delete",confirm);
  g_key_file_set_boolean(key,"workbench","delete-bookmarks-on-close",close);
  g_key_file_set_boolean(key,"workbench","preserve-bookmarks",preserve);
  gsize length=0;gchar *text=g_key_file_to_data(key,&length,NULL),*directory=g_path_get_dirname(wb.prefs_path);
  if(length>65536) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Workbench settings are invalid or too large");ok=FALSE; }
  else if(g_mkdir_with_parents(directory,0700)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Workbench settings directory could not be created");ok=FALSE; }
  else { GFile *file=g_file_new_for_path(wb.prefs_path);ok=g_file_replace_contents(file,text,length,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,error);g_object_unref(file); }
  g_free(text);g_free(directory);
 }
 g_key_file_unref(key);return ok;
}
static void bookmark_options(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Bookmark Options",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Apply",GTK_RESPONSE_APPLY,NULL);
 gtk_widget_set_name(dialog,"insight-bookmark-options");gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)),*confirm=gtk_check_button_new_with_label("Confirm bookmark deletion"),*close=gtk_check_button_new_with_label("Delete bookmarks when file closes"),*preserve=gtk_check_button_new_with_label("Preserve bookmarks between sessions"),*font=insight_list_style_button(wb.marks_view),*help=insight_panel_help_button("panel-bookmarks.md"),*status=gtk_label_new("");
 gtk_widget_set_name(preserve,"insight-bookmark-options-preserve");gtk_widget_set_name(confirm,"insight-bookmark-options-confirm");gtk_widget_set_name(close,"insight-bookmark-options-close");gtk_widget_set_name(font,"insight-bookmark-options-font");gtk_widget_set_name(status,"insight-bookmark-options-status");
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preserve),wb.marks_preserve);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(confirm),wb.marks_confirm_delete);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(close),wb.marks_delete_on_close);
 gtk_label_set_line_wrap(GTK_LABEL(status),TRUE);gtk_box_pack_start(GTK_BOX(box),preserve,FALSE,FALSE,6);gtk_box_pack_start(GTK_BOX(box),confirm,FALSE,FALSE,6);gtk_box_pack_start(GTK_BOX(box),close,FALSE,FALSE,6);
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_box_pack_start(GTK_BOX(row),font,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),help,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,6);gtk_box_pack_start(GTK_BOX(box),status,FALSE,FALSE,6);
 insight_i18n_apply(dialog);gtk_widget_show_all(dialog);
 while(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_APPLY) {
  gboolean a=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(confirm)),b=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(close));GError *error=NULL;
  gboolean c=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(preserve));
  if(bookmark_options_save(a,b,c,&error)) { wb.marks_confirm_delete=a;wb.marks_delete_on_close=b;wb.marks_preserve=c;break; }
  gtk_label_set_text(GTK_LABEL(status),insight_tr(error ? error->message : "Workbench settings not saved"));g_clear_error(&error);
 }
 gtk_widget_destroy(dialog);
}
static void bookmark_menu_action(GtkMenuItem *item,gpointer data) {
 (void)item;guint action=GPOINTER_TO_UINT(data);
 if(action==0) bookmark_go(NULL,NULL);
 else if(action==1) note_rename(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK));
 else if(action==2) note_delete(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK));
 else if(action==3 || action==4 || action==5) notes_transfer(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK|(action==3 ? 256 : action==4 ? 768 : 0)));
 else if(action==6) gtk_tree_selection_select_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.marks_view)));
 else if(action==7) gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.marks_view)));
 else if(action==8) bookmark_delete_all();
 else if(action==9) wb.marks_confirm_delete=gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item));
 else if(action==10) wb.marks_delete_on_close=gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item));
 else if(action==11) bookmark_options(NULL,NULL);
}
static GtkWidget *bookmark_menu(void) {
 GtkWidget *menu=g_object_get_data(G_OBJECT(wb.marks_view),"insight-bookmark-popup");
 const gchar *labels[]={"Go to","Rename…","Delete selected bookmarks","Export selected…","Export all bookmarks…","Import…","Select all bookmarks","Clear bookmark selection","Delete all bookmarks…","Confirm bookmark deletion","Delete bookmarks when file closes","Bookmark Options…"};
 const gchar *icons[]={"go-jump","document-edit","edit-delete","document-save","document-save-as","document-open","edit-select-all","edit-clear","edit-delete","dialog-question","window-close","preferences-system"};
 if(!menu) { menu=gtk_menu_new();gtk_menu_attach_to_widget(GTK_MENU(menu),wb.marks_view,NULL);g_object_set_data_full(G_OBJECT(wb.marks_view),"insight-bookmark-popup",g_object_ref_sink(menu),g_object_unref);
  for(guint i=0;i<G_N_ELEMENTS(labels);i++) { GtkWidget *item=(i==9 || i==10) ? gtk_check_menu_item_new_with_label(labels[i]) : symbol_history_item(labels[i],icons[i]);if(i==9 || i==10) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),i==9 ? wb.marks_confirm_delete : wb.marks_delete_on_close);gchar *name=g_strdup_printf("insight-bookmark-command-%u",i);gtk_widget_set_name(item,name);g_object_set_data(G_OBJECT(menu),name,item);g_free(name);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(bookmark_menu_action),GUINT_TO_POINTER(i)); }
  gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());
  GtkWidget *appearance=insight_list_style_menu_item(wb.marks_view);gtk_widget_set_name(appearance,"insight-bookmark-appearance");gtk_menu_shell_append(GTK_MENU_SHELL(menu),appearance);
 }
 for(guint i=9;i<=10;i++) { gchar *name=g_strdup_printf("insight-bookmark-command-%u",i);GtkWidget *item=g_object_get_data(G_OBJECT(menu),name);g_free(name);g_signal_handlers_block_by_func(item,bookmark_menu_action,GUINT_TO_POINTER(i));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),i==9 ? wb.marks_confirm_delete : wb.marks_delete_on_close);g_signal_handlers_unblock_by_func(item,bookmark_menu_action,GUINT_TO_POINTER(i)); }
 gint count=gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.marks_view)));
 for(guint i=0;i<G_N_ELEMENTS(labels);i++) { gchar *name=g_strdup_printf("insight-bookmark-command-%u",i);GtkWidget *item=g_object_get_data(G_OBJECT(menu),name);g_free(name);gtk_widget_set_sensitive(item,i<2 ? count==1 : i==2 || i==3 || i==7 ? count>0 : TRUE); }
 insight_i18n_apply(menu);gtk_widget_show_all(menu);return menu;
}
static gboolean bookmark_popup_key(GtkWidget *widget,gpointer data) {
 (void)data;GdkEvent *event=gtk_get_current_event();gtk_menu_popup_at_widget(GTK_MENU(bookmark_menu()),widget,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,event);if(event) gdk_event_free(event);return TRUE;
}
static gboolean bookmark_popup_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)data;if(event->type!=GDK_BUTTON_PRESS || event->button!=3) return FALSE;GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));GtkTreePath *path=NULL;
 g_signal_handlers_block_by_func(selection,note_selected,GINT_TO_POINTER(INSIGHT_BOOKMARK));
 if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),(gint)event->x,(gint)event->y,&path,NULL,NULL,NULL)) { if(!gtk_tree_selection_path_is_selected(selection,path)) { gtk_tree_selection_unselect_all(selection);gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget),path,NULL,FALSE); }gtk_tree_path_free(path); }
 g_signal_handlers_unblock_by_func(selection,note_selected,GINT_TO_POINTER(INSIGHT_BOOKMARK));gtk_widget_grab_focus(widget);gtk_menu_popup_at_pointer(GTK_MENU(bookmark_menu()),(GdkEvent *)event);return TRUE;
}
static gboolean bookmark_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)data;guint modifiers=event->state&gtk_accelerator_get_default_mod_mask(),key=gdk_keyval_to_lower(event->keyval);
 if(widget==wb.marks_view) {
  if((key==GDK_KEY_Menu && !modifiers) || (key==GDK_KEY_F10 && modifiers==GDK_SHIFT_MASK)) return bookmark_popup_key(widget,NULL);
  if(key==GDK_KEY_a && modifiers==GEANY_PRIMARY_MOD_MASK) { gtk_tree_selection_select_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)));return TRUE; }
  if(key==GDK_KEY_Delete && !modifiers) { note_delete(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK));return TRUE; }
 }
 if(modifiers==GEANY_PRIMARY_MOD_MASK) {
  if(key==GDK_KEY_q) { bookmark_options(NULL,NULL);return TRUE; }
  if(key==GDK_KEY_n) { bookmark_add(NULL,NULL);return TRUE; }
  if(key==GDK_KEY_x) { note_delete(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK));return TRUE; }
  if(key==GDK_KEY_o || key==GDK_KEY_s) { notes_transfer(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK|(key==GDK_KEY_s ? 256 : 0)));return TRUE; }
 }
 if(widget==wb.marks_view && !modifiers && (key==GDK_KEY_Return || key==GDK_KEY_KP_Enter)) { bookmark_go(NULL,NULL);return TRUE; }
 return browser_key(widget,event,NULL);
}
static void bookmark_file_cell(GtkTreeViewColumn *column,GtkCellRenderer *renderer,GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 (void)column;(void)data;gchar *path=NULL,*relative=NULL;gtk_tree_model_get(model,iter,1,&path,-1);
 if(path && wb.marks_project && wb.project_marks_root) { GFile *root=g_file_new_for_path(wb.project_marks_root),*file=g_file_new_for_path(path);relative=g_file_get_relative_path(root,file);g_object_unref(root);g_object_unref(file); }
 g_object_set(renderer,"text",relative ? relative : path,NULL);g_free(relative);g_free(path);
}
static void create_note_panel(InsightNoteKind kind) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3),*field=gtk_search_entry_new(),*controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2); GtkListStore *store=gtk_list_store_new(5,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING,G_TYPE_STRING); GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
 if(kind==INSIGHT_BOOKMARK) { GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);wb.marks_scope=gtk_label_new("Global bookmarks (no project)");gtk_widget_set_name(wb.marks_scope,"insight-bookmark-scope");gtk_label_set_ellipsize(GTK_LABEL(wb.marks_scope),PANGO_ELLIPSIZE_MIDDLE);gtk_label_set_xalign(GTK_LABEL(wb.marks_scope),0);wb.marks_reload=gtk_button_new_with_label("Reload bookmarks");gtk_widget_set_name(wb.marks_reload,"insight-reload-project-bookmarks");g_signal_connect(wb.marks_reload,"clicked",G_CALLBACK(bookmarks_project_reload),NULL);gtk_box_pack_start(GTK_BOX(row),wb.marks_scope,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(row),wb.marks_reload,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);gtk_widget_set_name(view,"insight-bookmarks");gtk_widget_set_name(field,"insight-bookmarks-query");wb.marks_entry=field; wb.marks_store=store; wb.marks_view=view; } else { wb.clips_entry=field; wb.clips_store=store; wb.clips_view=view; gtk_widget_set_name(view,"insight-clips"); gtk_widget_set_name(field,"insight-clips-query"); }
 gtk_widget_set_tooltip_text(field,"Prefix with ~ for fuzzy subsequence matching");gtk_entry_set_placeholder_text(GTK_ENTRY(field),kind==INSIGHT_BOOKMARK ? "Bookmark name or filter…" : "Clip name or filter…"); gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Name",gtk_cell_renderer_text_new(),"text",0,NULL));
 if(kind==INSIGHT_BOOKMARK) { gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),GTK_SELECTION_MULTIPLE);GtkCellRenderer *file=gtk_cell_renderer_text_new();GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes("File",file,NULL);gtk_tree_view_column_set_cell_data_func(column,file,bookmark_file_cell,NULL,NULL);gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Symbol",gtk_cell_renderer_text_new(),"text",4,NULL)); gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Line",gtk_cell_renderer_text_new(),"text",2,NULL)); gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(view),1); }
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),0,GTK_SORT_ASCENDING); GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),view);
 gtk_box_pack_start(GTK_BOX(box),field,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),controls,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 const char *marks[]={"Add/update","Go to","Delete"},*clips[]={"Copy selection","Cut selection","Paste","Edit","Delete"}; guint count=kind==INSIGHT_BOOKMARK ? 3 : 5;
 for(guint i=0;i<count;i++) { const char *icons[]={"edit-copy","edit-cut","edit-paste","document-edit","edit-delete"}; GtkWidget *button=kind==INSIGHT_BOOKMARK ? gtk_button_new_with_label(marks[i]) : gtk_button_new_from_icon_name(icons[i],GTK_ICON_SIZE_SMALL_TOOLBAR); gtk_widget_set_tooltip_text(button,kind==INSIGHT_BOOKMARK ? marks[i] : clips[i]); gtk_box_pack_start(GTK_BOX(controls),button,FALSE,FALSE,0);
  GCallback callback; gpointer argument=NULL;
  if(kind==INSIGHT_BOOKMARK) { callback=i==0 ? G_CALLBACK(bookmark_add) : (i==1 ? G_CALLBACK(bookmark_go) : G_CALLBACK(note_delete)); if(i==2) argument=GINT_TO_POINTER(kind); }
  else { const gchar *ids[]={"insight-clip-copy","insight-clip-cut","insight-clip-paste","insight-clip-edit","insight-clip-delete"};gtk_widget_set_name(button,ids[i]);callback=i<2 ? G_CALLBACK(clip_capture) : (i==2 ? G_CALLBACK(clip_paste) : (i==3 ? G_CALLBACK(clip_edit) : G_CALLBACK(note_delete))); argument=i==1 ? GINT_TO_POINTER(1) : (i==4 ? GINT_TO_POINTER(kind) : NULL); }
  g_signal_connect(button,"clicked",callback,argument);
 }
 if(kind==INSIGHT_CLIP) { GtkWidget *create=gtk_button_new_from_icon_name("document-new",GTK_ICON_SIZE_SMALL_TOOLBAR),*insert=gtk_button_new(); gtk_button_set_image(GTK_BUTTON(insert),insight_command_icon("snippets")); gtk_widget_set_name(create,"insight-new-template"); gtk_widget_set_name(insert,"insight-insert-template"); gtk_widget_set_tooltip_text(create,"New code template"); gtk_widget_set_tooltip_text(insert,"Insert as code template — expand variables"); gtk_box_pack_start(GTK_BOX(controls),create,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(controls),insert,FALSE,FALSE,0); g_signal_connect(create,"clicked",G_CALLBACK(clip_new_template),NULL); g_signal_connect(insert,"clicked",G_CALLBACK(clip_template),NULL); }
 GtkWidget *transfer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);
 GtkWidget *rename=gtk_button_new_with_label("Rename…"); gtk_box_pack_start(GTK_BOX(transfer),rename,FALSE,FALSE,0); g_signal_connect(rename,"clicked",G_CALLBACK(note_rename),GINT_TO_POINTER(kind));
 for(guint i=0;i<2;i++) { GtkWidget *button=gtk_button_new_with_label(i && kind==INSIGHT_BOOKMARK ? "Export selected…" : i ? "Export…" : "Import…"); gtk_box_pack_start(GTK_BOX(transfer),button,FALSE,FALSE,0); g_signal_connect(button,"clicked",G_CALLBACK(notes_transfer),GINT_TO_POINTER((gint)kind|(i ? 256 : 0))); }
 if(kind==INSIGHT_BOOKMARK) { GtkWidget *options=gtk_button_new_with_label("Bookmark Options…");gtk_widget_set_name(options,"insight-bookmark-options-button");gtk_widget_set_tooltip_text(options,"Bookmark Options (Ctrl+Q in Bookmarks)");gtk_widget_set_halign(options,GTK_ALIGN_START);gtk_box_pack_start(GTK_BOX(box),options,FALSE,FALSE,0);g_signal_connect(options,"clicked",G_CALLBACK(bookmark_options),NULL); }
 if(kind==INSIGHT_CLIP) { GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*reload=gtk_button_new_from_icon_name("view-refresh",GTK_ICON_SIZE_SMALL_TOOLBAR);wb.clips_scope=gtk_combo_box_text_new();gtk_widget_set_name(wb.clips_scope,"insight-clips-store");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.clips_scope),"Global templates");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.clips_scope),"Project templates");gtk_combo_box_set_active(GTK_COMBO_BOX(wb.clips_scope),0);gtk_widget_set_name(reload,"insight-reload-project-templates");gtk_widget_set_tooltip_text(reload,"Reload project templates");gtk_box_pack_start(GTK_BOX(row),wb.clips_scope,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(row),reload,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);g_signal_connect(wb.clips_scope,"changed",G_CALLBACK(clips_scope_changed),NULL);g_signal_connect(reload,"clicked",G_CALLBACK(clips_project_reload),NULL); }
 if(kind==INSIGHT_CLIP) { wb.clips_all=gtk_check_button_new_with_label("Show all languages"); gtk_widget_set_name(wb.clips_all,"insight-clips-all-languages"); gtk_box_pack_start(GTK_BOX(box),wb.clips_all,FALSE,FALSE,0); g_signal_connect(wb.clips_all,"toggled",G_CALLBACK(notes_filter),GINT_TO_POINTER(INSIGHT_CLIP)); }
 gtk_box_pack_start(GTK_BOX(box),transfer,FALSE,FALSE,0); gtk_box_reorder_child(GTK_BOX(box),transfer,2);
 gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),box,gtk_label_new(kind==INSIGHT_BOOKMARK ? "Bookmarks" : "Clips")); g_signal_connect(field,"changed",G_CALLBACK(notes_filter),GINT_TO_POINTER(kind)); g_signal_connect(field,"key-press-event",kind==INSIGHT_BOOKMARK ? G_CALLBACK(bookmark_key) : G_CALLBACK(browser_key),NULL); g_signal_connect(view,"key-press-event",kind==INSIGHT_BOOKMARK ? G_CALLBACK(bookmark_key) : G_CALLBACK(browser_key),NULL);
 if(kind==INSIGHT_BOOKMARK) { g_signal_connect(field,"activate",G_CALLBACK(bookmark_go),NULL);g_signal_connect(view,"popup-menu",G_CALLBACK(bookmark_popup_key),NULL);g_signal_connect(view,"button-press-event",G_CALLBACK(bookmark_popup_button),NULL); }
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),"changed",G_CALLBACK(note_selected),GINT_TO_POINTER(kind)); g_signal_connect(view,"row-activated",G_CALLBACK(note_activated),GINT_TO_POINTER(kind)); gtk_widget_show_all(box); notes_filter(NULL,GINT_TO_POINTER(kind));
}
static void create_notes(void) {
 wb.bookmark_handles=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
 wb.marks_session_stores=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,bookmark_session_store_free);
 wb.notes_path=g_build_filename(app->configdir,"insight","notes.conf",NULL); wb.notes=insight_notes_new();wb.project_notes=insight_notes_new();wb.project_marks=insight_notes_new();
 if(g_file_test(wb.notes_path,G_FILE_TEST_EXISTS)) { bookmark_session_remember(wb.notes_path,NULL,NULL);GError *error=NULL; InsightNotes *loaded=insight_notes_load(wb.notes_path,&error); if(loaded) { insight_notes_free(wb.notes); wb.notes=loaded; } else { g_hash_table_remove(wb.marks_session_stores,wb.notes_path);ui_set_statusbar(TRUE,"Notes ignored: %s",error ? error->message : "invalid snapshot"); } g_clear_error(&error); }
 create_note_panel(INSIGHT_BOOKMARK); create_note_panel(INSIGHT_CLIP);gchar *marks_root=app->project ? project_get_base_path() : NULL;bookmarks_project_load(app->project ? app->project->file_name : NULL,marks_root,TRUE);g_free(marks_root);clips_project_load(app->project ? app->project->file_name : NULL,FALSE);
 for(guint i=0;i<documents_array->len;i++) bookmark_restore_document(g_ptr_array_index(documents_array,i));
}
static void category_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; (void)data; GtkTreeIter iter; GtkTreeModel *model=gtk_tree_view_get_model(view);
 if(!gtk_tree_model_get_iter(model,&iter,path)) return;
 gulong line; gtk_tree_model_get(model,&iter,SYMBOL_LINE,&line,-1);
 if(!line) { if(gtk_tree_view_row_expanded(view,path)) gtk_tree_view_collapse_row(view,path); else gtk_tree_view_expand_row(view,path,FALSE); return; }
 browser_activated(view,path,NULL,NULL);
}
static gint category_language(const gchar *path) {
 GeanyDocument *doc=path && path[0]!='@' ? document_find_by_filename(path) : NULL;
 if(doc && doc->tm_file) return doc->tm_file->lang;
 GeanyFiletype *ft=insight_languages_detect(path); if(!ft) ft=filetypes_detect_from_extension(path); return ft ? ft->lang : -1;
}
static void category_append(GtkTreeIter *row,GtkTreeIter *parent,const InsightSymbol *symbol) {
 gtk_tree_store_append(wb.categories,row,parent); gtk_tree_store_set(wb.categories,row,SYMBOL_NAME,symbol->name,SYMBOL_SCOPE,symbol->scope,SYMBOL_PATH,symbol->path,SYMBOL_LINE,(gulong)symbol->line,SYMBOL_KIND,symbol->kind,-1);
}
static void categories_filter(GtkEditable *entry,gpointer data) {
 (void)entry; (void)data; GroupedPanel *p=&wb.category_panel; if(!p->store || !gtk_widget_get_mapped(p->view)) return;
 InsightLookupFilter *filter=grouped_filter(p); if(!filter) return; refresh_index();
 GHashTable *expanded=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL); gchar *selected=grouped_capture(p,expanded); gtk_tree_store_clear(p->store);
 gsize count=insight_index_count(wb.index); const InsightSymbol **symbols=g_new(const InsightSymbol *,count); insight_index_query(wb.index,"",FALSE,NULL,NULL,NULL,0,symbols,count);
 GHashTable *groups=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free),*owners=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free); GPtrArray *loaded=g_ptr_array_new(); guint shown=0,total=0;
 GeanyDocument *active=document_get_current(); InsightLanguageFilter *language=insight_language_filter_new(active ? active->file_name : NULL,active ? active->file_type : NULL);
 for(gsize i=0;i<count;i++) {
  const InsightSymbol *symbol=symbols[i]; if(!insight_language_filter_match(language,symbol->path) || !insight_lookup_filter_match(filter,symbol->name,symbol->scope,symbol->path,symbol->kind)) continue;
  total++; if(shown>=p->limit) continue;
  guint type=insight_symbol_kind_type(symbol->path,symbol->kind,NULL); gint lang=category_language(symbol->path); gchar *key=g_strdup_printf("type:%u",type); GtkTreeIter *category=g_hash_table_lookup(groups,key);
  if(!category) { category=g_new(GtkTreeIter,1); gtk_tree_store_append(p->store,category,NULL); gchar *kind=g_strdup_printf("@type:%u",type); gtk_tree_store_set(p->store,category,SYMBOL_NAME,insight_symbol_type_name(type,TRUE),SYMBOL_KIND,kind,-1); g_free(kind); g_hash_table_insert(groups,g_strdup(key),category); } g_free(key);
  GtkTreeIter *parent=category; const gchar *separator=lang>=0 ? tm_parser_scope_separator_printable(lang) : "::"; if(!separator || !*separator) separator="::";
  if(*symbol->scope) {
   gchar **parts=g_strsplit(symbol->scope,separator,64); GString *scope=g_string_new(NULL);
   for(guint part=0;parts[part];part++) {
    if(!*parts[part]) continue;
    if(scope->len) g_string_append(scope,separator);
    g_string_append(scope,parts[part]);
    key=g_strdup_printf("scope:%u:%d:%s",type,lang,scope->str); GtkTreeIter *node=g_hash_table_lookup(groups,key);
    if(!node) { node=g_new(GtkTreeIter,1); gtk_tree_store_append(p->store,node,parent); gchar *kind=g_strdup_printf("@type:%u:%d",type,lang); gtk_tree_store_set(p->store,node,SYMBOL_NAME,parts[part],SYMBOL_SCOPE,scope->str,SYMBOL_KIND,kind,-1); g_free(kind); g_hash_table_insert(groups,g_strdup(key),node); }
    parent=node; g_free(key);
   } g_string_free(scope,TRUE); g_strfreev(parts);
  }
  GtkTreeIter row; category_append(&row,parent,symbol); g_ptr_array_add(loaded,(gpointer)symbol); shown++;
  if(type&(tm_tag_class_t|tm_tag_struct_t|tm_tag_union_t|tm_tag_interface_t)) {
   gchar *qualified=*symbol->scope ? g_strconcat(symbol->scope,separator,symbol->name,NULL) : g_strdup(symbol->name); key=g_strdup_printf("%d:%s",lang,qualified); g_free(qualified);
   if(g_hash_table_contains(owners,key)) { g_hash_table_replace(owners,key,NULL); } else { GtkTreeIter *owner=g_new(GtkTreeIter,1); *owner=row; g_hash_table_insert(owners,key,owner); }
  }
 }
 /* Supplemental direct members under unambiguous indexed type declarations.
  * Their canonical category rows remain available; this is scope-based, not
  * overload/type-alias resolution. Both passes obey the same query and limit. */
 for(guint i=0;i<loaded->len;i++) {
  const InsightSymbol *symbol=g_ptr_array_index(loaded,i); if(!*symbol->scope) continue;
  gchar *key=g_strdup_printf("%d:%s",category_language(symbol->path),symbol->scope); GtkTreeIter *owner=g_hash_table_lookup(owners,key); g_free(key);
  if(owner) { GtkTreeIter row; category_append(&row,owner,symbol); }
 }
 insight_language_filter_free(language); g_ptr_array_free(loaded,TRUE); g_hash_table_destroy(owners); g_hash_table_destroy(groups); insight_lookup_filter_free(filter); g_free(symbols);
 grouped_count(p,shown,total); gtk_widget_set_visible(p->more,shown<total && p->limit<250000); grouped_restore(p,expanded,selected);
}
static gboolean refresh_visible_project_panel(gpointer data) {
 (void)data; wb.panel_idle=0; if(!wb.alive) return G_SOURCE_REMOVE; categories_filter(NULL,NULL); tree_refresh(); file_types_refresh(); return G_SOURCE_REMOVE;
}
static void project_panel_changed(GtkNotebook *book,GtkWidget *page,guint number,gpointer data) {
 (void)book; (void)page; (void)number; (void)data; if(!wb.panel_idle) wb.panel_idle=g_idle_add(refresh_visible_project_panel,NULL);
}
static void create_categories(void) {
 GroupedPanel *p=&wb.category_panel; p->symbols=TRUE; GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3); wb.category_entry=p->entry=gtk_search_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(p->entry),"Find symbol category members…"); gtk_box_pack_start(GTK_BOX(box),p->entry,FALSE,FALSE,0);
 wb.categories=p->store=gtk_tree_store_new(SYMBOL_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING); wb.category_view=p->view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(p->store));
 const char *titles[]={"Symbol / category","Scope","File","Line","Kind"}; for(guint i=0;i<SYMBOL_COLUMNS;i++) { GtkTreeViewColumn *c=gtk_tree_view_column_new_with_attributes(titles[i],gtk_cell_renderer_text_new(),"text",i,NULL); gtk_tree_view_column_set_sort_column_id(c,i); gtk_tree_view_append_column(GTK_TREE_VIEW(p->view),c); }
 const gint widths[]={220,160,300,70,140}; insight_symbol_view_columns_configure(GTK_TREE_VIEW(p->view),widths,G_N_ELEMENTS(widths)); insight_symbol_tree_format(GTK_TREE_VIEW(p->view)); gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(p->store),SYMBOL_NAME,GTK_SORT_ASCENDING); gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(p->view),SYMBOL_PATH);
 grouped_attach(p,box); GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),p->view); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0); gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),box,gtk_label_new("Categories"));
 g_signal_connect(p->view,"row-activated",G_CALLBACK(category_activated),NULL); gtk_widget_show_all(box); categories_filter(NULL,NULL);
}
typedef struct { InsightIndex *index; gchar *query,*preferred,*path,*text,*digest; const InsightSymbol *matches[256]; gsize count; gint chosen,position,anchor,encoding; guint document; gulong line; GHashTable *allowed_paths;InsightLanguageFilter *language;gboolean valid_word,nominal_calls,python; guint64 generation,epoch,index_version; } DefinitionJob;
static gchar *cursor_query(GeanyDocument *doc) {
 gint a=(gint)SSM(doc->editor->sci,SCI_GETSELECTIONSTART,0,0),b=(gint)SSM(doc->editor->sci,SCI_GETSELECTIONEND,0,0);
 if(b>a && b-a<=1024) { gchar *query=sci_get_selection_contents(doc->editor->sci); return g_strstrip(query); }
 gchar word[1025]={0}; editor_find_current_word_sciwc(doc->editor,sci_get_current_position(doc->editor->sci),word,sizeof word); return g_strdup(word);
}
static void stop_definition(void) {
 wb.definition_generation++; if(wb.definition_timer) { g_source_remove(wb.definition_timer); wb.definition_timer=0; }
 if(wb.definition_worker) { g_cancellable_cancel(wb.definition_worker); g_clear_object(&wb.definition_worker); }
}
static void definition_job_free(gpointer data) { DefinitionJob *j=data;insight_language_filter_free(j->language); retire_index(j->index);g_hash_table_destroy(j->allowed_paths); g_free(j->query); g_free(j->preferred); g_free(j->path); g_free(j->text); g_free(j->digest); g_free(j); }
static void definition_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; DefinitionJob *j=data;
 if(j->valid_word && *j->query && !g_cancellable_is_cancelled(cancel)) {
  j->count=insight_relation_query(j->index,j->query,INSIGHT_RELATION_DEFINITIONS,0,j->matches,G_N_ELEMENTS(j->matches)); if(j->count==SIZE_MAX) j->count=0; j->count=MIN(j->count,G_N_ELEMENTS(j->matches));
  gchar *scope=NULL,*mask=NULL;gboolean member=FALSE,uncertain_scope=FALSE,imported=FALSE;gchar *owner=NULL;
  if(j->python && j->text) {
   gsize length=strlen(j->text);mask=insight_source_code_mask(j->text,length,INSIGHT_CODE_PYTHON,NULL);InsightNativeSource text={j->text,mask,length,NULL};text.python=insight_native_python_bindings_new(j->index,j->preferred,&text);scope=insight_native_python_scope(j->index,j->preferred,&text,j->position);
   gsize imported_count=MIN(insight_native_python_import_query(j->index,j->preferred,scope,&text,j->position,j->query,relation_eligible,j->language,j->matches,G_N_ELEMENTS(j->matches),&imported),G_N_ELEMENTS(j->matches));if(imported) j->count=imported_count;
   if(!imported) owner=insight_native_python_owner(j->index,j->preferred,scope,j->line,&text,j->position,&member,relation_eligible,j->language);
   if(!member && !imported) { gchar *binding=insight_native_python_binding_scope(j->index,j->preferred,scope,j->query,&text);g_free(scope);scope=binding;uncertain_scope=scope==NULL; }
   insight_native_python_bindings_free(text.python);
  } else if(j->nominal_calls && j->text) {
   gsize length=strlen(j->text);mask=insight_source_code_mask(j->text,length,INSIGHT_CODE_C,NULL);InsightNativeSource text={j->text,mask,length,NULL};InsightNativeScopes *scopes=insight_native_scopes_new(j->index,j->preferred,&text);scope=insight_native_scopes_at(scopes,j->line,(gsize)j->position,&uncertain_scope);
   if(!uncertain_scope) { owner=insight_native_member_owner_source_filtered(j->index,j->preferred,scope,j->line,&text,j->position,&member,mask!=NULL,relation_eligible,j->language); }insight_native_scopes_free(scopes);
  } else { scope=insight_native_function_scope(j->index,j->preferred,j->line);owner=insight_native_member_owner_calls_filtered(j->index,j->preferred,scope,j->line,j->text,j->position,&member,j->nominal_calls,relation_eligible,j->language); }
  g_free(mask);if(uncertain_scope) j->count=0;gsize eligible=0;
  if(member) j->count=MIN(insight_member_query(j->index,owner,j->query,j->preferred,relation_eligible,j->language,j->matches,G_N_ELEMENTS(j->matches),NULL,NULL),G_N_ELEMENTS(j->matches));
  for(gsize n=0;n<j->count;n++) if((imported || g_hash_table_contains(j->allowed_paths,j->matches[n]->path)) && (j->python && !member && !imported ? insight_native_python_rank(j->index,j->matches[n],j->preferred,scope,j->line) : insight_native_definition_rank_at(j->matches[n],j->preferred,scope,j->line))>=0) j->matches[eligible++]=j->matches[n];
  j->count=eligible;
  for(gsize n=0;n<j->count;n++) if(n==0 || (j->python && !member && !imported ? insight_native_python_rank(j->index,j->matches[n],j->preferred,scope,j->line) : insight_native_definition_rank_at(j->matches[n],j->preferred,scope,j->line))>(j->python && !member && !imported ? insight_native_python_rank(j->index,j->matches[j->chosen],j->preferred,scope,j->line) : insight_native_definition_rank_at(j->matches[j->chosen],j->preferred,scope,j->line))) j->chosen=(gint)n;
  g_free(scope); g_free(owner);
  if(j->count) { const InsightSymbol *hit=j->matches[j->chosen]; j->path=g_strdup(hit->path); j->line=hit->line; }
 }
 if(!j->path) j->path=g_strdup(j->preferred);
 if(g_strcmp0(j->path,j->preferred)) { g_clear_pointer(&j->text,g_free); if(!g_cancellable_is_cancelled(cancel)) j->text=read_disk_source_full(j->path,TRUE,j->encoding,cancel); }
 if(j->text) j->digest=source_digest(j->text);
 g_task_return_boolean(task,TRUE);
}
static void definition_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); DefinitionJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.definition_generation) return;
 g_clear_object(&wb.definition_worker); if(j->index_version!=wb.index_version) { queue_definition(TRUE); return; } GeanyDocument *current=document_get_current();
 if(!ok || !current || current->id!=j->document || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow)) || sci_get_current_position(current->editor->sci)!=j->position || SSM(current->editor->sci,SCI_GETANCHOR,0,0)!=j->anchor) return;
 GeanyDocument *doc=j->path ? document_find_by_filename(j->path) : current;
 if(doc) { gint length=sci_get_length(doc->editor->sci); if(length>=0 && length<=2*1024*1024) { g_free(j->text); j->text=sci_get_contents(doc->editor->sci,length+1); g_free(j->digest); j->digest=source_digest(j->text); } }
 if(!j->text || !g_utf8_validate(j->text,-1,NULL)) { ui_set_statusbar(TRUE,"Definition source unavailable or exceeds 2 MiB"); return; }
 /* Do not empty/repopulate the visible combo when Follow finds the same candidates. */
 GPtrArray *labels=g_ptr_array_new_with_free_func(g_free);
 GtkTreeModel *model=gtk_combo_box_get_model(GTK_COMBO_BOX(wb.choices));
 gboolean same=gtk_tree_model_iter_n_children(model,NULL)==(gint)j->count;
 for(gsize n=0;n<j->count;n++) {
  const InsightSymbol *hit=j->matches[n];
  gchar *label=g_strdup_printf("%s %s%s • %s — %s:%lu",hit->type,hit->name,hit->signature,hit->scope,hit->path,hit->line);g_ptr_array_add(labels,label);
  if(same) { GtkTreeIter iter;gchar *old=NULL;gtk_tree_model_iter_nth_child(model,&iter,NULL,(gint)n);gtk_tree_model_get(model,&iter,0,&old,-1);same=!g_strcmp0(old,label);g_free(old); }
 }
 wb.populating=TRUE;
 if(!same) {
  g_ptr_array_set_size(wb.definitions,0);gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(wb.choices));
  for(gsize n=0;n<j->count;n++) { const InsightSymbol *hit=j->matches[n];Location *location=g_new0(Location,1);location->path=g_strdup(hit->path);location->line=hit->line;g_ptr_array_add(wb.definitions,location);gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.choices),g_ptr_array_index(labels,n)); }
 }
 gint chosen=j->count ? (gint)j->chosen : -1;
 if(gtk_combo_box_get_active(GTK_COMBO_BOX(wb.choices))!=chosen) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.choices),chosen);
 wb.populating=FALSE;g_ptr_array_free(labels,TRUE);
 GeanyFiletype *ft=doc ? doc->file_type : preview_filetype(j->path); render_source(j->text,ft,j->line,j->path);
 g_free(wb.source_path); wb.source_path=g_strdup(j->path); wb.source_id=doc ? doc->id : 0; wb.source_line=j->line<=(gulong)sci_get_line_count(wb.preview) ? j->line : 0; g_free(wb.source_digest); wb.source_digest=g_strdup(j->digest);preview_links();
 gchar *label=j->count ? g_strdup_printf("Context — %s:%lu",j->path ? j->path : "untitled",j->line) : g_strdup_printf("Context — no indexed definition: %s • %s:%lu",j->query,j->path ? j->path : "untitled",j->line); if(g_strcmp0(gtk_label_get_text(GTK_LABEL(wb.title)),label)) gtk_label_set_text(GTK_LABEL(wb.title),label); g_free(label); if(j->count) preview_mark_symbol(j->matches[j->chosen]->name); navigation_state();
 wb.follow_doc=j->document; wb.follow_line=(gulong)sci_get_current_line(current->editor->sci)+1; g_free(wb.follow_word); wb.follow_word=g_strdup(j->query);
 if(gtk_widget_has_focus(GTK_WIDGET(current->editor->sci)) && gtk_widget_get_parent(wb.box)==main_widgets.message_window_notebook) { reveal_context(); }
}
static gboolean definition_timeout(gpointer data) {
 (void)data; wb.definition_timer=0; if(!wb.alive || !wb.preview || !wb.context_visible || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow))) return G_SOURCE_REMOVE;
 GeanyDocument *doc=document_get_current(); if(!doc) return G_SOURCE_REMOVE; refresh_index();
 DefinitionJob *j=g_new0(DefinitionJob,1); j->index=insight_index_ref(wb.index); j->query=cursor_query(doc); j->preferred=g_strdup(doc->file_name); j->document=doc->id; j->position=sci_get_current_position(doc->editor->sci); j->anchor=(gint)SSM(doc->editor->sci,SCI_GETANCHOR,0,0); j->line=(gulong)sci_get_current_line(doc->editor->sci)+1; j->encoding=file_prefs.default_open_encoding; j->generation=wb.definition_generation; j->epoch=workbench_epoch; j->index_version=wb.index_version;
 j->language=insight_language_filter_new(doc->file_name,doc->file_type);j->nominal_calls=member_calls_language(doc->file_type);j->python=python_language(doc->file_type);
 j->valid_word=!definition_keyword(doc->file_type,j->query) && !insight_languages_keyword(doc->editor->sci,j->query) && definition_code(doc->editor->sci,j->position);
 j->allowed_paths=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);const InsightSymbol *candidates[4096];gsize candidates_count=insight_index_find(j->index,j->query,TRUE,candidates,G_N_ELEMENTS(candidates));
 for(gsize n=0;n<MIN(candidates_count,G_N_ELEMENTS(candidates));n++) if(definition_language(doc->file_name,doc->file_type,candidates[n]->path)) g_hash_table_add(j->allowed_paths,g_strdup(candidates[n]->path));
 gint length=sci_get_length(doc->editor->sci); if(length>=0 && length<=2*1024*1024) { j->text=sci_get_contents(doc->editor->sci,length+1);if(!g_utf8_validate(j->text,length,NULL) || memchr(j->text,0,(gsize)length)) g_clear_pointer(&j->text,g_free); }
 if(!j->text) j->valid_word=FALSE;
 wb.definition_worker=g_cancellable_new(); GTask *task=g_task_new(NULL,wb.definition_worker,definition_job_done,NULL); g_task_set_task_data(task,j,definition_job_free); g_task_run_in_thread(task,definition_job_run); g_object_unref(task); return G_SOURCE_REMOVE;
}
static void queue_definition(gboolean force) {
 if(!wb.alive || !wb.preview || !wb.follow || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow))) return;
 GeanyDocument *doc=document_get_current(); if(!doc) return; gchar *query=cursor_query(doc); gboolean same=wb.follow_doc==doc->id && wb.follow_line==(gulong)sci_get_current_line(doc->editor->sci)+1 && !g_strcmp0(wb.follow_word,query); g_free(query);
 if(!force && (same || wb.definition_timer || wb.definition_worker)) return;
 stop_definition(); wb.definition_timer=g_timeout_add(500,definition_timeout,NULL);
}
static gboolean refresh_now(gpointer data) {
 (void)data; wb.refresh=0;
 GeanyDocument *active=document_get_current(); gboolean dirty=wb.index_dirty;
 if(wb.clips_store) { gchar *language=clip_current_language(); if(g_strcmp0(language,wb.clips_language)) notes_filter(NULL,GINT_TO_POINTER(INSIGHT_CLIP)); g_free(language); }
 if(active) { decorate_selection(active->editor->sci); gsize a=(gsize)SSM(active->editor->sci,SCI_GETSELECTIONSTART,0,0),b=(gsize)SSM(active->editor->sci,SCI_GETSELECTIONEND,0,0); if(b>a) decorate_match(active->editor->sci,a,b); else decorate_match(active->editor->sci,0,0); }
 if(wb.preview) decorate_selection(wb.preview);
 /* Project models are refreshed when a completed snapshot is published,
  * rather than repeatedly while the same snapshot is still being built. */
 if(wb.local_doc_id!=(active ? active->id : 0)) local_filter(NULL,NULL);
 relation_update_from_editor(dirty);
 if(!wb.preview || !wb.context_visible || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow))) return G_SOURCE_REMOVE;
 GeanyDocument *doc=document_get_current();
 if(!doc) { render_source("",NULL,1,NULL); g_clear_pointer(&wb.source_path,g_free); wb.source_id=0; wb.source_line=0; preview_links();navigation_state(); gtk_label_set_text(GTK_LABEL(wb.title),"Context — no active source"); return G_SOURCE_REMOVE; }
 queue_definition(dirty);
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
  gsize a=(gsize)SSM(wb.preview,SCI_GETSELECTIONSTART,0,0),b=(gsize)SSM(wb.preview,SCI_GETSELECTIONEND,0,0),mapped_a=0,mapped_b=0;gboolean selected=FALSE;
  if(b>a) {
   gint first=(gint)SSM(wb.preview,SCI_LINEFROMPOSITION,a,0),last=(gint)SSM(wb.preview,SCI_LINEFROMPOSITION,b,0);
   mapped_a=(gsize)sci_get_position_from_line(doc->editor->sci,first)+a-(gsize)sci_get_position_from_line(wb.preview,first);
   mapped_b=(gsize)sci_get_position_from_line(doc->editor->sci,last)+b-(gsize)sci_get_position_from_line(wb.preview,last);
   selected=mapped_a<=mapped_b && mapped_b<=(gsize)sci_get_length(doc->editor->sci);
  }
  /* Record the final selection caret, not an intermediate line start.
   * Otherwise Back first returns to that line start instead of the caller. */
  navqueue_goto_position(old,doc,selected ? (gint)mapped_b:sci_get_position_from_line(doc->editor->sci,(gint)line-1));
  if(selected) { SSM(doc->editor->sci,SCI_SETSEL,mapped_a,mapped_b);decorate_match(doc->editor->sci,mapped_a,mapped_b); }
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
 const InsightSymbol *definition=word_definition(source,word,wb.source_path,(gulong)sci_get_current_line(wb.preview)+1,wb.preview,position);
 if(definition) {
  gchar *path=g_strdup(definition->path),*resolved_name=g_strdup(definition->name); gulong line=definition->line;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
  if(show_location(path,0,line,TRUE)) preview_mark_symbol(resolved_name);
  g_free(path);g_free(resolved_name);
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
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); wb.context_find_row=row;gtk_widget_set_name(row,"insight-context-search-row"); wb.find_entry=gtk_search_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.find_entry),"Find in Context…"); gtk_widget_set_tooltip_text(wb.find_entry,"Use ~term for fuzzy search when Regex is off");
 gtk_box_pack_start(GTK_BOX(row),wb.find_entry,TRUE,TRUE,0); GtkWidget *previous=gtk_button_new_with_label("Previous"),*next=gtk_button_new_with_label("Next");
 gtk_box_pack_start(GTK_BOX(row),previous,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),next,FALSE,FALSE,0);
 wb.find_case=gtk_check_button_new_with_label("Case"); wb.find_word=gtk_check_button_new_with_label("Word"); wb.find_regex=gtk_check_button_new_with_label("Regex");
 gtk_box_pack_start(GTK_BOX(row),wb.find_case,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),wb.find_word,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),wb.find_regex,FALSE,FALSE,0);
 wb.find_status=gtk_label_new(""); gtk_label_set_ellipsize(GTK_LABEL(wb.find_status),PANGO_ELLIPSIZE_END); gtk_widget_set_size_request(wb.find_status,80,-1); gtk_box_pack_start(GTK_BOX(row),wb.find_status,FALSE,FALSE,0);
 g_signal_connect(previous,"clicked",G_CALLBACK(context_find),GINT_TO_POINTER(-1)); g_signal_connect(next,"clicked",G_CALLBACK(context_find),NULL); g_signal_connect(wb.find_entry,"activate",G_CALLBACK(context_find),NULL); g_signal_connect(wb.find_entry,"key-press-event",G_CALLBACK(browser_key),NULL);
 gtk_box_pack_start(GTK_BOX(wb.box),row,FALSE,FALSE,0);
 gtk_widget_show_all(row);gtk_widget_set_no_show_all(row,TRUE);gtk_widget_hide(row);
}
static gboolean preview_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget; (void)data;
 if(event->keyval==GDK_KEY_Return && !(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK))) { open_context(NULL,NULL); return TRUE; }
 if(event->keyval==GDK_KEY_Escape) { editor_focus(); return TRUE; }
 if(event->keyval==GDK_KEY_f && (event->state&GEANY_PRIMARY_MOD_MASK)) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.context_find_toggle),TRUE);gtk_widget_grab_focus(wb.find_entry); return TRUE; }
 if(event->keyval==GDK_KEY_F3) { context_find(NULL,GINT_TO_POINTER(event->state&GDK_SHIFT_MASK ? -1 : 1)); return TRUE; }
 if(event->keyval==GDK_KEY_F12 && !(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SHIFT_MASK))) return preview_definition(sci_get_current_position(wb.preview));
 return FALSE;
}
typedef struct { gint position;gchar *path,*digest; } PreviewClick;
static void preview_click_free(gpointer data) { PreviewClick *click=data;g_free(click->path);g_free(click->digest);g_free(click); }
static gboolean preview_single_click(gpointer data) {
 PreviewClick *click=data;wb.preview_click_timer=0;
 if(wb.alive && !g_strcmp0(click->path,wb.source_path) && !g_strcmp0(click->digest,wb.source_digest)) preview_definition(click->position);
 return G_SOURCE_REMOVE;
}
static void reveal_context_file(GtkMenuItem *item,gpointer data) { (void)item;(void)data;if(wb.source_path && wb.source_path[0]!='@') ui_reveal_file(wb.source_path); }
static void context_location_popup(GtkLabel *label,GtkMenu *menu,gpointer data) {
 (void)label;(void)data;GtkWidget *item=gtk_menu_item_new_with_label(insight_tr("Show in file manager"));gtk_widget_set_name(item,"insight-context-location-reveal");gtk_widget_set_sensitive(item,wb.source_path && wb.source_path[0]!='@');g_signal_connect(item,"activate",G_CALLBACK(reveal_context_file),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item);
}
static void context_popup_edit(GtkMenuItem *item,gpointer command) { (void)item;SSM(wb.preview,GPOINTER_TO_UINT(command),0,0); }
static gboolean preview_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 if(event->button==3) { GtkWidget *menu=gtk_menu_new(),*item=gtk_menu_item_new_with_label("Show in file manager");
  const gchar *labels[]={"Copy","Select All"};const guint commands[]={SCI_COPY,SCI_SELECTALL};for(guint i=0;i<G_N_ELEMENTS(commands);i++) { GtkWidget *edit=gtk_menu_item_new_with_label(labels[i]);if(i==0) gtk_widget_set_sensitive(edit,SSM(wb.preview,SCI_GETSELECTIONSTART,0,0)!=SSM(wb.preview,SCI_GETSELECTIONEND,0,0));g_signal_connect(edit,"activate",G_CALLBACK(context_popup_edit),GUINT_TO_POINTER(commands[i]));gtk_menu_shell_append(GTK_MENU_SHELL(menu),edit); }gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());gtk_widget_set_sensitive(item,wb.source_path && wb.source_path[0]!='@');gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(reveal_context_file),NULL);g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu);insight_i18n_apply(menu);gtk_widget_show_all(menu);gtk_menu_popup_at_pointer(GTK_MENU(menu),(GdkEvent *)event);return TRUE; }
 (void)widget; (void)data;if(event->button!=1) return FALSE;
 if(wb.preview_click_timer) { g_source_remove(wb.preview_click_timer);wb.preview_click_timer=0; }
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
 gint position=(gint)SSM(wb.preview,SCI_POSITIONFROMPOINT,(uptr_t)event->x,(sptr_t)event->y);
 if(event->type==GDK_2BUTTON_PRESS) {
  if(position>=0) { sci_set_selection(wb.preview,position,position);open_context(NULL,NULL); }
  return TRUE;
 }
 if(event->type!=GDK_BUTTON_PRESS || position<0) return FALSE;
 if(!SSM(wb.preview,SCI_INDICATORVALUEAT,INSIGHT_LINK,position)) return FALSE;
 gint delay=250;g_object_get(gtk_settings_get_default(),"gtk-double-click-time",&delay,NULL);
 PreviewClick *click=g_new0(PreviewClick,1);click->position=position;click->path=g_strdup(wb.source_path);click->digest=g_strdup(wb.source_digest);
 sci_set_selection(wb.preview,position,position);gtk_widget_grab_focus(GTK_WIDGET(wb.preview));
 wb.preview_click_timer=g_timeout_add_full(G_PRIORITY_DEFAULT,(guint)MAX(1,delay)+20,preview_single_click,click,preview_click_free);
 return TRUE;
}
void insight_workbench_editor_updated(gboolean content) {
 if(!wb.alive) return;
 insight_window_list_editor_updated();
 GeanyDocument *doc=document_get_current(); if(!wb.preview || !doc) return;
 if(content && wb.bookmark_handles && !wb.bookmark_sync_timer) wb.bookmark_sync_timer=g_timeout_add(350,bookmark_sync_idle,NULL);
 gint position=sci_get_current_position(doc->editor->sci),anchor=(gint)SSM(doc->editor->sci,SCI_GETANCHOR,0,0);
 gboolean focused=gtk_widget_has_focus(GTK_WIDGET(doc->editor->sci));
 /* An edit/caret update may arrive before focus-in. Do not cache that as an
  * already-followed focused position. Unchanged focus-out keeps manual locks. */
 if(!content && wb.editor_follow_id==doc->id && wb.editor_follow_position==position && wb.editor_follow_anchor==anchor && (!focused || wb.editor_follow_focused)) return;
 stop_definition(); wb.follow_doc=0;
 wb.editor_follow_id=doc->id; wb.editor_follow_position=position; wb.editor_follow_anchor=anchor;wb.editor_follow_focused=focused;
 if(focused) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),TRUE);
 queue_definition(FALSE); insight_workbench_refresh();
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
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(entry));
 InsightSession *session=insight_extensions_session(wb.extensions);
 for(size_t i=0;i<insight_session_command_count(session);i++) {
  const char *id=insight_session_command_name(session,i),*label=insight_session_command_label(session,i);
  if(!g_utf8_validate(label,-1,NULL)) label=id;
  gchar *text=g_strconcat(label," ",id,NULL);
  if(insight_lookup_text_match(text,query)) { GtkTreeIter iter; gtk_list_store_append(palette->store,&iter); gtk_list_store_set(palette->store,&iter,0,label,1,id,-1); }
  g_free(text);
 }
 GtkTreeIter first;
 if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(palette->store),&first)) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(palette->view)),&first);
}
static void palette_activate(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)view; (void)path; (void)column; gtk_dialog_response(GTK_DIALOG(data),GTK_RESPONSE_ACCEPT);
}
static void show_native_palette(GtkMenuItem *item,gpointer data) {
 (void)item; (void)data; if(wb.lua_running) return;
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Lua command palette",GTK_WINDOW(main_widgets.window),GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Run",GTK_RESPONSE_ACCEPT,NULL);
 gtk_window_set_default_size(GTK_WINDOW(dialog),650,400); gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);
 GtkWidget *entry=gtk_search_entry_new(),*box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));gtk_widget_set_name(entry,"insight-command-query");gtk_widget_set_tooltip_text(entry,"Prefix with ~ for fuzzy subsequence matching"); gtk_entry_set_activates_default(GTK_ENTRY(entry),TRUE);
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
  GtkWidget *item=gtk_menu_item_new_with_label(g_utf8_validate(label,-1,NULL) ? label : id);g_object_set_data(G_OBJECT(item),"insight-no-i18n",GINT_TO_POINTER(1));
  g_object_set_data_full(G_OBJECT(item),"native-command-id",g_strdup(id),g_free); gtk_container_add(GTK_CONTAINER(wb.lua_commands),item); g_signal_connect(item,"activate",G_CALLBACK(invoke_native_command),NULL);
  guint key; GdkModifierType mods; gtk_accelerator_parse(shortcut,&key,&mods); gboolean duplicate=key==palette_key && mods==palette_mods;
  for(size_t j=0;j<i;j++) if(keys[j]==key && modifiers[j]==mods) duplicate=TRUE;
  if(key && !duplicate && gtk_accelerator_valid(key,mods)) { gtk_widget_add_accelerator(item,"activate",wb.lua_accels,key,mods,GTK_ACCEL_VISIBLE); keys[i]=key; modifiers[i]=mods; }
 }
 gtk_widget_show_all(wb.lua_commands);
}
static gboolean file_event_refresh(gpointer data) { (void)data;wb.file_event_idle=0;if(wb.alive) { files_filter(NULL,NULL); tree_refresh(); file_types_refresh(); categories_filter(NULL,NULL);disk_current_file(); }return G_SOURCE_REMOVE; }
static void native_document_event(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object;
 if(!strcmp(data,"document_close")) insight_search_links_closed(wb.search_links,doc);
 if(!strcmp(data,"document_close") && doc && doc->is_valid && doc->file_name && wb.notes) for(guint i=0;i<notes_for(INSIGHT_BOOKMARK)->items->len;i++) { const InsightNote *note=g_ptr_array_index(notes_for(INSIGHT_BOOKMARK)->items,i);if(note->kind==INSIGHT_BOOKMARK && !g_strcmp0(note->path,doc->file_name)) { bookmark_prepare_tags(doc);break; } }
 insight_window_list_document_event(doc ? doc->id : 0,data); if(!strcmp(data,"document_close") || !strcmp(data,"document_save")) insight_workbench_bookmarks_changed();
 if(!strcmp(data,"document_close")) bookmark_file_closed(doc);
 if(!strcmp(data,"document_open") || !strcmp(data,"document_activate")) bookmark_restore_document(doc);
 if(wb.alive&&!strcmp(data,"document_activate")) files_filter(NULL,NULL);
 if(wb.alive && strcmp(data,"document_activate") && !wb.file_event_idle) wb.file_event_idle=g_idle_add(file_event_refresh,NULL);
 if(!strcmp(data,"document_save") && doc && doc->file_name && wb.membership && !g_strcmp0(doc->file_name,wb.membership->master_file)) insight_master_sync_poll(wb.master_sync);
 if(strcmp(data,"document_activate")) mark_index_dirty();
 insight_workbench_refresh();
 /* Disk snapshots are worker-owned/immutable; saving schedules a coalesced rescan. */
 if(!strcmp(data,"document_save") && doc && doc->is_valid && doc->file_name && wb.root && g_ptr_array_find_with_equal_func(wb.files,doc->file_name,(GEqualFunc)g_str_equal,NULL)) {
  wb.watch_changed=TRUE;
  if(!wb.scan && !wb.scan_worker && !wb.watch_idle) wb.watch_idle=g_timeout_add(2000,rescan_changed,NULL);
 }
 if(!wb.extensions || wb.lua_running || !doc || !doc->is_valid || insight_session_is_running(insight_extensions_session(wb.extensions))) return;

 refresh_index(); InsightIndex *snapshot=insight_index_clone(wb.index); if(!snapshot) return;
 NativeHost h={.doc=doc}; InsightHost host={.context=&h,.text=native_get_text,.selection=native_get_selection,.replace_selection=native_replace_selection,.index=snapshot,.api=&native_host_api};
 char error[512]; wb.lua_running=TRUE;
 int result=insight_session_emit(insight_extensions_session(wb.extensions),data,doc->file_name,&host,error,sizeof error);
 wb.lua_running=FALSE; g_free(h.scratch); insight_index_free(snapshot); if(result) ui_set_statusbar(TRUE,"Lua event: %s",error);
 if(!strcmp(data,"document_close")) mark_index_dirty();
}
static void native_project_update(GObject *object,GKeyFile *config,gpointer data) {
 (void)object; if(!wb.preview || !app->project) return;
 gchar *utf8=project_get_base_path(); if(!utf8) return;
 gchar *native=utils_get_locale_from_utf8(utf8),*root=insight_canonicalize_filename(native,NULL); g_free(native);
 gboolean changed=GPOINTER_TO_INT(data) || g_strcmp0(root,wb.geany_root) || g_strcmp0(wb.membership_project,app->project->file_name);
 if(changed) {
  bookmarks_project_load(app->project->file_name,utf8,FALSE);clips_project_load(app->project->file_name,FALSE);
  invalidate_search_context();
  GKeyFile *loaded=NULL; GError *error=NULL;
  if(!config) { loaded=g_key_file_new(); gchar *filename=utils_get_locale_from_utf8(app->project->file_name); if(!g_key_file_load_from_file(loaded,filename,G_KEY_FILE_NONE,&error) && g_error_matches(error,G_FILE_ERROR,G_FILE_ERROR_NOENT)) g_clear_error(&error); g_free(filename); config=loaded; }
  insight_git_bind(config,root);
  InsightProjectFiles *policy=error ? NULL : (insight_git_active() ? insight_git_membership(&error) : insight_project_files_read_project(config,utf8,app->project->file_name,&error));
  wb.membership_valid=policy!=NULL;
  if(policy) insight_scroll_links_membership(policy,root);
  if(!policy) { policy=insight_project_files_new(); policy->automatic=FALSE; ui_set_statusbar(TRUE,"Project membership could not be restored: %s",error ? error->message : "invalid settings"); }
  insight_project_files_free(wb.membership); wb.membership=policy; g_free(wb.membership_project); wb.membership_project=g_strdup(app->project->file_name); g_clear_error(&error); if(loaded) { g_key_file_unref(loaded); config=NULL; }
  wb.workspace_from_project=TRUE;wb.membership_generation++;insight_master_sync_set(wb.master_sync,app->project->file_name,utf8,wb.membership);
  if(start_scan(root)) { g_free(wb.geany_root); wb.geany_root=g_strdup(root); browser_filter(NULL,NULL); files_filter(NULL,NULL); tree_refresh(); categories_filter(NULL,NULL); file_types_refresh(); focus_widget_panel(wb.file_entry); }
  else ui_set_statusbar(TRUE,"Project source folder cannot be indexed: %s",root);
 }
 if(!GPOINTER_TO_INT(data) && config && wb.membership_valid) { GError *error=NULL; if(!insight_project_files_write(wb.membership,config,utf8,&error)) ui_set_statusbar(TRUE,"Cannot save project membership: %s",error ? error->message : "invalid rules"); g_clear_error(&error); }
 g_free(root); g_free(utf8);
}
static gboolean membership_store(const InsightProjectFiles *candidate) {
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
 if(ok) { insight_scroll_links_membership(candidate,root);if(wb.usage_idle || wb.search_worker) { stop_usage(); gtk_label_set_text(GTK_LABEL(wb.search_status),"Project files changed; run the search again"); } insight_project_files_free(wb.membership); wb.membership=insight_project_files_clone(candidate); wb.membership_valid=TRUE;wb.membership_generation++; start_scan(wb.root); files_filter(NULL,NULL); }
 else ui_set_statusbar(TRUE,"Project files not changed: %s",error ? error->message : "cannot save configuration");
 g_clear_error(&error); g_key_file_unref(config); g_object_unref(file); g_free(filename); g_free(root); return ok;
}
static gboolean master_membership_applied(const InsightProjectFiles *policy,gpointer data) { (void)data;return wb.alive && membership_store(policy); }
static void master_sync_report(const GError *error,gpointer data) { (void)data;if(wb.alive) ui_set_statusbar(TRUE,"Master file list: %s",insight_tr(error->message)); }
static gboolean membership_apply(InsightProjectFiles *candidate) {
 if(insight_git_active()) { ui_set_statusbar(TRUE,"Use Add to Git or Add and Remove Git Project Files for this project");return FALSE; }
 if(!app->project || !wb.workspace_from_project) { ui_set_statusbar(TRUE,"Open a Geany project before changing its files");return FALSE; }
 if(candidate->master_file && *candidate->master_file) {
  GeanyDocument *master=document_find_by_filename(candidate->master_file);if(master && master->changed) { ui_set_statusbar(TRUE,"Save or reload the open master file list before changing project membership");return FALSE; }
  GError *error=NULL;gboolean attach=g_strcmp0(candidate->master_file,wb.membership->master_file)!=0;
  gboolean queued=attach ? insight_master_sync_attach(wb.master_sync,candidate,&error) : insight_master_sync_write(wb.master_sync,candidate,&error);
  if(queued) ui_set_statusbar(TRUE,attach ? "Loading master file list…" : "Saving master file list…");else if(error) ui_set_statusbar(TRUE,"Master file list: %s",insight_tr(error->message));g_clear_error(&error);return queued;
 }
 gboolean saved=membership_store(candidate);if(saved) { gchar *root=project_get_base_path();insight_master_sync_set(wb.master_sync,app->project->file_name,root,wb.membership);g_free(root); }return saved;
}
static void project_master_command(GtkMenuItem *item,gpointer data) {
 (void)item;if(!app->project || !wb.workspace_from_project || !wb.membership->master_file || !*wb.membership->master_file) { ui_set_statusbar(TRUE,"Choose a master file list in Add and Remove Project Files first");return; }
 if(GPOINTER_TO_INT(data)) { gchar *path=utils_get_locale_from_utf8(wb.membership->master_file);document_open_file(path,FALSE,NULL,NULL);g_free(path); }
 else { insight_master_sync_poll(wb.master_sync);ui_set_statusbar(TRUE,"Synchronizing master file list…"); }
}
typedef struct { GtkWidget *dialog,*query,*automatic,*masks,*master,*master_status,*view; GtkListStore *store; InsightProjectFiles *candidate; GPtrArray *paths,*trees; gchar *root,*project_file;guint64 generation;gboolean master_valid; } MembershipDialog;
static void membership_dialog_refresh(MembershipDialog *d) {
 gtk_list_store_clear(d->store); const gchar *query=gtk_entry_get_text(GTK_ENTRY(d->query));
 for(guint kind=0;kind<2;kind++) { GPtrArray *paths=kind ? d->trees : d->paths;
  for(guint i=0;i<paths->len;i++) { const char *path=g_ptr_array_index(paths,i); if(kind ? !g_ptr_array_find_with_equal_func(d->candidate->trees,path,(GEqualFunc)g_str_equal,NULL) : !insight_project_files_contains(d->candidate,d->root,path)) continue;
   if(!insight_lookup_text_match(path,query)) continue;
   GtkTreeIter iter; gtk_list_store_append(d->store,&iter); gchar *name=g_path_get_basename(path); gtk_list_store_set(d->store,&iter,0,name,1,path,2,kind!=0,3,kind ? "Folder (indexed on Apply)" : "File",-1); g_free(name);
  }
 }
}
static void membership_dialog_changed(GtkWidget *widget,gpointer data) {
 (void)widget; MembershipDialog *d=data;const gchar *master=gtk_entry_get_text(GTK_ENTRY(d->master));GError *error=NULL;gboolean valid=insight_project_files_set_master(d->candidate,master,d->root,d->project_file,&error);
 d->master_valid=valid;gtk_dialog_set_response_sensitive(GTK_DIALOG(d->dialog),GTK_RESPONSE_APPLY,valid);gtk_label_set_text(GTK_LABEL(d->master_status),error ? insight_tr(error->message) : "");gtk_widget_set_visible(d->master_status,error!=NULL);g_clear_error(&error);
 d->candidate->automatic=!*master && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->automatic));gtk_widget_set_sensitive(d->automatic,!*master);if(*master) { g_signal_handlers_block_by_func(d->automatic,membership_dialog_changed,d);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->automatic),FALSE);g_signal_handlers_unblock_by_func(d->automatic,membership_dialog_changed,d); }g_free(d->candidate->masks); d->candidate->masks=g_strdup(gtk_entry_get_text(GTK_ENTRY(d->masks))); membership_dialog_refresh(d);
}
static void membership_master_choose(GtkButton *button,gpointer data) {
 (void)button;MembershipDialog *d=data;GtkWidget *chooser=gtk_file_chooser_dialog_new("Choose master file list",GTK_WINDOW(d->dialog),GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(chooser))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser)),*utf8=utils_get_utf8_from_locale(path);gchar *escaped=insight_project_files_escape_master(utf8);gtk_entry_set_text(GTK_ENTRY(d->master),escaped);g_free(escaped);g_free(utf8);g_free(path); }gtk_widget_destroy(chooser);
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
     gchar *p=insight_canonicalize_filename(lines[i],d->root),*native=utils_get_locale_from_utf8(p); gboolean tree=g_file_test(native,G_FILE_TEST_IS_DIR); g_free(native);
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
 if(mode==2) { gchar *master=g_strdup(d->candidate->master_file),*expression=g_strdup(d->candidate->master_expression);insight_project_files_free(d->candidate); d->candidate=insight_project_files_new();g_free(d->candidate->master_file);d->candidate->master_file=master;g_free(d->candidate->master_expression);d->candidate->master_expression=expression; d->candidate->automatic=FALSE; gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->automatic),FALSE); gtk_entry_set_text(GTK_ENTRY(d->masks),""); g_ptr_array_set_size(d->paths,0); g_ptr_array_set_size(d->trees,0); membership_dialog_refresh(d); return; }
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
 if(insight_git_active()) { insight_git_manage(widget,data);return; }
 (void)widget; (void)data; if(!app->project || !wb.workspace_from_project) { ui_set_statusbar(TRUE,"Open a Geany project before changing its files"); return; }
 MembershipDialog d={0};d.generation=wb.membership_generation; d.candidate=insight_project_files_clone(wb.membership); d.paths=g_ptr_array_new_with_free_func(g_free); d.trees=g_ptr_array_new_with_free_func(g_free); d.root=project_get_base_path();d.project_file=g_strdup(app->project->file_name);
 for(guint i=0;i<wb.files->len;i++) g_ptr_array_add(d.paths,g_strdup(g_ptr_array_index(wb.files,i)));
 for(guint i=0;i<d.candidate->files->len;i++) { const char *path=g_ptr_array_index(d.candidate->files,i); if(!g_ptr_array_find_with_equal_func(d.paths,path,(GEqualFunc)g_str_equal,NULL)) g_ptr_array_add(d.paths,g_strdup(path)); }
 for(guint i=0;i<d.candidate->trees->len;i++) g_ptr_array_add(d.trees,g_strdup(g_ptr_array_index(d.candidate->trees,i)));
 d.dialog=gtk_dialog_new_with_buttons("Add and Remove Project Files",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Apply",GTK_RESPONSE_APPLY,NULL); gtk_window_set_default_size(GTK_WINDOW(d.dialog),760,520);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d.dialog)); gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Files are removed from the project only. Added folders are indexed after Apply."),FALSE,FALSE,4);
 d.query=gtk_search_entry_new();gtk_widget_set_tooltip_text(d.query,"Prefix with ~ for fuzzy subsequence matching");gtk_widget_set_name(d.query,"insight-project-membership-query"); gtk_entry_set_placeholder_text(GTK_ENTRY(d.query),"Filter project paths…"); gtk_box_pack_start(GTK_BOX(box),d.query,FALSE,FALSE,3);
 d.store=gtk_list_store_new(4,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN,G_TYPE_STRING); d.view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(d.store)); gtk_widget_set_name(d.view,"insight-project-membership-files");
 const char *labels[]={"Name","Path","Kind"}; const gint columns[]={0,1,3};
 for(guint i=0;i<3;i++) { GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(labels[i],gtk_cell_renderer_text_new(),"text",columns[i],NULL); gtk_tree_view_column_set_sort_column_id(column,columns[i]); gtk_tree_view_column_set_resizable(column,TRUE); gtk_tree_view_append_column(GTK_TREE_VIEW(d.view),column); }
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(d.store),0,GTK_SORT_ASCENDING); gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(d.view)),GTK_SELECTION_MULTIPLE); GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),d.view); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,3);
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); const char *buttons[]={"Add files…","Add tree…","Remove selected","Remove tree…","Remove all"};
 for(guint i=0;i<5;i++) { GtkWidget *button=gtk_button_new_with_label(buttons[i]); gtk_box_pack_start(GTK_BOX(row),button,FALSE,FALSE,0); if(i<2) { g_object_set_data(G_OBJECT(button),"membership-tree",GINT_TO_POINTER(i)); g_signal_connect(button,"clicked",G_CALLBACK(membership_dialog_add),&d); } else { g_object_set_data(G_OBJECT(button),"membership-remove",GINT_TO_POINTER(i-2)); g_signal_connect(button,"clicked",G_CALLBACK(membership_dialog_remove),&d); } }
 gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,3); d.automatic=gtk_check_button_new_with_label("Automatically include known source files under the project base folder"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d.automatic),d.candidate->automatic); gtk_box_pack_start(GTK_BOX(box),d.automatic,FALSE,FALSE,3);
 GtkWidget *master_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*master_choose=gtk_button_new_with_label("Browse…");d.master=gtk_entry_new();gtk_widget_set_name(d.master,"insight-project-master-list");gtk_widget_set_tooltip_text(d.master,"Path variables: %PROJECT_SOURCE_DIR%, %PROJECT_DATA_DIR%, %PROJECT_NAME%, %TEMP_DIR% and environment names; use %% for a literal percent sign");gtk_entry_set_placeholder_text(GTK_ENTRY(d.master),"Master file list (optional; existing UTF-8 file)");gchar *master_text=*d.candidate->master_expression ? g_strdup(d.candidate->master_expression) : insight_project_files_escape_master(d.candidate->master_file);gtk_entry_set_text(GTK_ENTRY(d.master),master_text);g_free(master_text);gtk_widget_set_hexpand(d.master,TRUE);gtk_box_pack_start(GTK_BOX(master_row),d.master,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(master_row),master_choose,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),master_row,FALSE,FALSE,3);g_signal_connect(master_choose,"clicked",G_CALLBACK(membership_master_choose),&d);gtk_widget_set_sensitive(d.automatic,!*d.candidate->master_file);d.master_status=gtk_label_new("");gtk_widget_set_name(d.master_status,"insight-project-master-error");gtk_label_set_line_wrap(GTK_LABEL(d.master_status),TRUE);gtk_widget_set_no_show_all(d.master_status,TRUE);gtk_box_pack_start(GTK_BOX(box),d.master_status,FALSE,FALSE,0);
 GtkWidget *lists=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); for(guint i=0;i<2;i++) { GtkWidget *button=gtk_button_new_with_label(i ? "Export file list…" : "Add from list…"); g_object_set_data(G_OBJECT(button),"membership-export",GUINT_TO_POINTER(i)); g_signal_connect(button,"clicked",G_CALLBACK(membership_dialog_file_list),&d); gtk_box_pack_start(GTK_BOX(lists),button,FALSE,FALSE,0); } gtk_box_pack_start(GTK_BOX(box),lists,FALSE,FALSE,3);
 d.masks=gtk_entry_new(); gtk_entry_set_max_length(GTK_ENTRY(d.masks),4096); gtk_entry_set_placeholder_text(GTK_ENTRY(d.masks),"Exclude masks: *.generated.c;vendor/* (case sensitive)"); gtk_entry_set_text(GTK_ENTRY(d.masks),d.candidate->masks); gtk_box_pack_start(GTK_BOX(box),d.masks,FALSE,FALSE,3);
 g_signal_connect(d.master,"changed",G_CALLBACK(membership_dialog_changed),&d);g_signal_connect(d.query,"changed",G_CALLBACK(membership_dialog_changed),&d); g_signal_connect(d.masks,"changed",G_CALLBACK(membership_dialog_changed),&d); g_signal_connect(d.automatic,"toggled",G_CALLBACK(membership_dialog_changed),&d); membership_dialog_changed(NULL,&d); gtk_widget_show_all(d.dialog);
 while(gtk_dialog_run(GTK_DIALOG(d.dialog))==GTK_RESPONSE_APPLY) { if(!d.master_valid) continue;if(d.generation!=wb.membership_generation) { ui_set_statusbar(TRUE,"Project files changed while the dialog was open; reopen it before applying");break; }if(membership_apply(d.candidate)) break; }
 gtk_widget_destroy(d.dialog); g_object_unref(d.store); insight_project_files_free(d.candidate); g_ptr_array_free(d.paths,TRUE); g_ptr_array_free(d.trees,TRUE); g_free(d.root);g_free(d.project_file);
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
 insight_git_close();
 wb.membership_generation++;insight_master_sync_set(wb.master_sync,NULL,NULL,NULL);
 bookmarks_project_load(NULL,NULL,FALSE);clips_project_load(NULL,FALSE);
 (void)object; (void)data; g_clear_pointer(&wb.geany_root,g_free); g_clear_pointer(&wb.membership_project,g_free); wb.membership_valid=FALSE;
 if(!wb.workspace_from_project) return;
 wb.workspace_from_project=FALSE; invalidate_search_context(); stop_scan(); insight_workspace_watch_clear(wb.watch); wb.watch_changed=FALSE; if(wb.watch_idle) { g_source_remove(wb.watch_idle); wb.watch_idle=0; } retire_index(wb.base); wb.base=insight_index_new();g_hash_table_remove_all(wb.declarations); mark_index_dirty(); g_ptr_array_set_size(wb.files,0); g_clear_pointer(&wb.root,g_free);
 /* Project databases survive project-close and application restart. */
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
 GtkWidget *owner=gtk_widget_get_toplevel(widget);for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window==owner && wb.hosts[i].minimized) panel_restore(i); if(GTK_IS_WINDOW(owner) && owner!=main_widgets.window) { gtk_widget_show(owner); gtk_window_present(GTK_WINDOW(owner)); }
 GtkWidget *page=widget; while(page && gtk_widget_get_parent(page) && !GTK_IS_NOTEBOOK(gtk_widget_get_parent(page)) && !GTK_IS_WINDOW(gtk_widget_get_parent(page))) page=gtk_widget_get_parent(page);
 if(page && GTK_IS_NOTEBOOK(gtk_widget_get_parent(page))) { gtk_widget_show(page);GtkWidget *book=gtk_widget_get_parent(page); gtk_widget_show(book); gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),page)); }
 gtk_widget_grab_focus(widget);
}
static void panel_position_guard(GObject *object,GParamSpec *spec,gpointer data) {
 (void)spec;(void)data;GtkWidget *widget=GTK_WIDGET(object);gint extent=gtk_orientable_get_orientation(GTK_ORIENTABLE(widget))==GTK_ORIENTATION_HORIZONTAL ? gtk_widget_get_allocated_width(widget) : gtk_widget_get_allocated_height(widget);
 if(extent<100) return;
 GtkWidget *first=gtk_paned_get_child1(GTK_PANED(widget)),*second=gtk_paned_get_child2(GTK_PANED(widget));
 /* A detached/hidden child has no split to protect. Forcing 16px against
  * GTK's zero-width side repeatedly schedules allocation/position updates. */
 if(!first || !second || !gtk_widget_get_visible(first) || !gtk_widget_get_visible(second)) return;
 gint minimum=0,maximum=0;g_object_get(widget,"min-position",&minimum,"max-position",&maximum,NULL);
 minimum=MAX(minimum,16);maximum=MIN(maximum,extent-16);if(minimum>maximum) return;
 gint position=gtk_paned_get_position(GTK_PANED(widget)),safe=CLAMP(position,minimum,maximum);if(position!=safe) gtk_paned_set_position(GTK_PANED(widget),safe);
}
static void project_split_allocated(GtkWidget *widget,GtkAllocation *allocation,gpointer data) {
 (void)data;if(!wb.alive || wb.initial_right_split==-1 || allocation->width<400) return;
 gint minimum=0,maximum=0;g_object_get(widget,"min-position",&minimum,"max-position",&maximum,NULL);
 if(wb.initial_right_split>maximum && maximum-minimum<120) return;
 gint requested=wb.initial_right_split;wb.initial_right_split=-1;
 if(requested==-2) { gtk_paned_set_position(GTK_PANED(widget),MAX(0,allocation->width-100));return; }
 if(requested>maximum) { gint project=CLAMP(allocation->width/3,120,450);gtk_paned_set_position(GTK_PANED(widget),CLAMP(allocation->width-project,minimum,maximum)); }
}
static void focus_templates(GtkWidget *widget,gpointer data) { (void)widget; (void)data; focus_widget_panel(wb.clips_entry); }
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
 const InsightSymbol *definition=word_definition(doc,word,NULL,(gulong)sci_get_current_line(doc->editor->sci)+1,doc->editor->sci,sci_get_current_position(doc->editor->sci));
 if(!definition) return FALSE;
 gchar *path=g_strdup(definition->path); gulong line=definition->line;
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); gboolean shown=show_location(path,0,line,TRUE); if(shown) open_context(NULL,NULL); g_free(path); return shown;
}
static void go_definition(GtkMenuItem *item,gpointer data) { (void)item; (void)data; if(!insight_workbench_goto_definition()) ui_set_statusbar(TRUE,"Known definition not found"); }
static void context_visible(GtkCheckMenuItem *item,gpointer data) {
 (void)data; wb.context_visible=gtk_check_menu_item_get_active(item);
 if(wb.context_visible) { gtk_widget_show(wb.box); reveal_context(); insight_workbench_refresh(); } else gtk_widget_hide(wb.box);
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].page==wb.box && wb.hosts[i].window) gtk_widget_set_visible(wb.hosts[i].window,wb.context_visible && !wb.hosts[i].minimized);
}
static void focus_relations(GtkMenuItem *item,gpointer data) { (void)item; (void)data; focus_widget_panel(wb.usage_entry); }
gboolean insight_workbench_activate(guint action) {
 if(!wb.alive || action>=15) return FALSE;
 if(action==14) { GeanyDocument *doc=document_get_current();if(!DOC_VALID(doc)) return FALSE;gtk_window_present(GTK_WINDOW(main_widgets.window));document_grab_focus(doc);return TRUE; }
 if(action==10) { insight_window_list_show(NULL,NULL);return TRUE; }
 if(action==13) { focus_search_results(NULL,NULL);gtk_window_present(GTK_WINDOW(main_widgets.window));return TRUE; }
 GtkWidget *targets[]={wb.file_entry,wb.tree_entry,wb.symbol_entry,wb.usage_entry,wb.marks_entry,wb.clips_entry,wb.type_panel.entry,wb.category_panel.entry,wb.local_entry,GTK_WIDGET(wb.preview),NULL,wb.editor_query,wb.project_query};
 GtkWidget *target=targets[action];if(!target) return FALSE;
 if(action==8 && !wb.hosts[8].window) { ui_prefs.sidebar_visible=TRUE;interface_prefs.sidebar_symbol_visible=TRUE;ui_sidebar_show_hide(); }
 if(action==9) { wb.context_visible=TRUE;gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.context_toggle),TRUE);gtk_widget_show(wb.box);reveal_context(); }
 if(action==11) gtk_widget_show(wb.editor_search_row);
 if(action==12) { gtk_widget_show(wb.project_search_row);focus_search_results(NULL,NULL); }
 focus_widget_panel(target);GtkWidget *owner=gtk_widget_get_toplevel(target);if(GTK_IS_WINDOW(owner)) gtk_window_present(GTK_WINDOW(owner));return TRUE;
}
static void activate_panel(GtkMenuItem *item,gpointer data) { (void)item;insight_workbench_activate(GPOINTER_TO_UINT(data)); }
static void expand_panel(GtkMenuItem *item,gpointer data) {
 (void)item; gint action=GPOINTER_TO_INT(data);if(action<2) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.folder_mode),0); GtkTreeView *view=GTK_TREE_VIEW(action<2 ? wb.tree_view : (action<4 ? wb.category_view : wb.types_view));
 if(action%2) gtk_tree_view_collapse_all(view); else gtk_tree_view_expand_all(view);
}
/* A dialog may outlive project refreshes: any model change invalidates its target. */
static void tree_target_changed(gboolean *changed) { *changed=TRUE; }
static void tree_dialog_destroyed(GtkWidget *widget,gpointer data) { (void)widget;*(gboolean *)data=TRUE; }
static void tree_branch_command(GtkMenuItem *item,gpointer data) {
 (void)item;guint command=GPOINTER_TO_UINT(data),panel=command/3,action=command%3;
 GtkWidget *widget=panel==0 ? wb.tree_view : (panel==1 ? wb.category_view : wb.types_view);
 if(panel==0) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.folder_mode),0);
 if(!GTK_IS_TREE_VIEW(widget)) return;
 GtkTreeView *view=GTK_TREE_VIEW(widget);GtkTreePath *path=NULL;
 gtk_tree_view_get_cursor(view,&path,NULL);
 if(!path) { ui_set_statusbar(TRUE,"%s",insight_tr("Select a tree row first"));return; }
 GtkTreeModel *model=gtk_tree_view_get_model(view);g_object_ref(model);g_object_ref(view);
 GtkTreeRowReference *row=gtk_tree_row_reference_new(model,path);gtk_tree_path_free(path);
 guint levels=32;gboolean changed=FALSE,accepted=TRUE;
 if(action==0) {
  const gchar *signals[]={"row-changed","row-inserted","row-deleted","rows-reordered"};gulong handlers[4];
  for(guint i=0;i<4;i++) handlers[i]=g_signal_connect_swapped(model,signals[i],G_CALLBACK(tree_target_changed),&changed);
  GtkWidget *dialog=gtk_dialog_new_with_buttons(insight_tr("Expand Special"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Expand"),GTK_RESPONSE_ACCEPT,NULL);
  gtk_widget_set_name(dialog,"insight-tree-expand-dialog");
  GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *label=gtk_label_new(insight_tr("Levels below the selected row (1–32)")),*spin=gtk_spin_button_new_with_range(1,32,1);
  gtk_widget_set_name(spin,"insight-tree-expand-depth");gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin),1);
  gtk_container_set_border_width(GTK_CONTAINER(box),12);gtk_box_pack_start(GTK_BOX(box),label,FALSE,FALSE,4);gtk_box_pack_start(GTK_BOX(box),spin,FALSE,FALSE,4);
  gboolean destroyed=FALSE;g_object_ref(dialog);g_signal_connect(dialog,"destroy",G_CALLBACK(tree_dialog_destroyed),&destroyed);
  gtk_widget_show_all(dialog);accepted=gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT && !destroyed;
  if(!destroyed) { levels=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));gtk_widget_destroy(dialog); }
  g_object_unref(dialog);
  for(guint i=0;i<4;i++) g_signal_handler_disconnect(model,handlers[i]);
 }
 path=gtk_tree_row_reference_get_path(row);
 if(accepted && (changed || !path || !wb.alive || gtk_widget_in_destruction(widget) || gtk_tree_view_get_model(view)!=model)) ui_set_statusbar(TRUE,"%s",insight_tr("Tree changed; select the row again"));
 else if(accepted) {
  if(action==2) gtk_tree_view_collapse_row(view,path);
  else if(!insight_tree_expand_levels(view,path,levels)) ui_set_statusbar(TRUE,"%s",insight_tr("Tree expansion stopped at its safety limit or the tree changed"));
 }
 if(path) gtk_tree_path_free(path);gtk_tree_row_reference_free(row);g_object_unref(model);g_object_unref(view);
}
static void project_visible(GtkCheckMenuItem *item,gpointer data) {
 (void)data; gboolean visible=gtk_check_menu_item_get_active(item); gtk_widget_set_visible(wb.project_notebook,visible); if(wb.float_window) gtk_widget_set_visible(wb.float_window,visible);
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].home==wb.project_notebook && wb.hosts[i].window) gtk_widget_set_visible(wb.hosts[i].window,visible && !wb.hosts[i].minimized);
}
static void local_visible(GtkCheckMenuItem *item,gpointer data) { (void)data; ui_prefs.sidebar_visible=gtk_check_menu_item_get_active(item); ui_sidebar_show_hide();
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].home==main_widgets.sidebar_notebook && wb.hosts[i].window) gtk_widget_set_visible(wb.hosts[i].window,ui_prefs.sidebar_visible && !wb.hosts[i].minimized); }
static void dock_project(GtkMenuItem *item,gpointer data) {
 (void)item; (void)data; if(!wb.float_window) return;
 gtk_window_get_position(GTK_WINDOW(wb.float_window),&wb.float_x,&wb.float_y); gtk_window_get_size(GTK_WINDOW(wb.float_window),&wb.float_width,&wb.float_height);
 GtkWidget *window=wb.float_window; wb.float_window=NULL; gboolean visible=gtk_widget_get_visible(wb.project_notebook);
 g_object_ref(wb.project_host); gtk_container_remove(GTK_CONTAINER(window),wb.project_host); gtk_paned_pack2(GTK_PANED(wb.right_pane),wb.project_host,FALSE,TRUE); g_object_unref(wb.project_host); gtk_widget_set_visible(wb.project_notebook,visible); gtk_widget_destroy(window);
}
static gboolean float_closed(GtkWidget *widget,GdkEvent *event,gpointer data) { (void)widget; (void)event; dock_project(NULL,data); return TRUE; }
static void float_project(GtkMenuItem *item,gpointer data) {
 (void)item; (void)data; if(wb.float_window) { gtk_window_present(GTK_WINDOW(wb.float_window)); return; }
 wb.float_window=gtk_window_new(GTK_WINDOW_TOPLEVEL); gtk_window_set_title(GTK_WINDOW(wb.float_window),"Geany Insight — Project"); gtk_window_set_transient_for(GTK_WINDOW(wb.float_window),GTK_WINDOW(main_widgets.window));
 gtk_window_set_default_size(GTK_WINDOW(wb.float_window),wb.float_width>=300 ? wb.float_width : 440,wb.float_height>=200 ? wb.float_height : 620);
 g_object_ref(wb.project_host); gtk_container_remove(GTK_CONTAINER(wb.right_pane),wb.project_host); gtk_container_add(GTK_CONTAINER(wb.float_window),wb.project_host); g_object_unref(wb.project_host);
 g_signal_connect(wb.float_window,"delete-event",G_CALLBACK(float_closed),NULL);insight_appearance_decorate(wb.float_window); gtk_widget_show_all(wb.float_window);
 if(wb.float_width>=300) {
  GdkMonitor *monitor=gdk_display_get_monitor_at_point(gdk_display_get_default(),wb.float_x,wb.float_y);
  if(monitor) { GdkRectangle area; gdk_monitor_get_workarea(monitor,&area); wb.float_width=MIN(wb.float_width,MAX(300,area.width)); wb.float_height=MIN(wb.float_height,MAX(200,area.height)); wb.float_x=CLAMP(wb.float_x,area.x,area.x+MAX(0,area.width-wb.float_width)); wb.float_y=CLAMP(wb.float_y,area.y,area.y+MAX(0,area.height-wb.float_height)); gtk_window_resize(GTK_WINDOW(wb.float_window),wb.float_width,wb.float_height); }
  gtk_window_move(GTK_WINDOW(wb.float_window),wb.float_x,wb.float_y);
 }
}
/* Floating groups share one window; page ownership always follows its parent. */
static guint panel_window_owner(GtkWidget *window) {
 for(guint i=0;i<wb.host_count;i++) if(window && wb.hosts[i].window==window) return i;
 return wb.host_count;
}
static guint panel_window_count(GtkWidget *window) {
 guint count=0;for(guint i=0;i<wb.host_count;i++) if(window && wb.hosts[i].window==window) count++;return count;
}
/* Keep controls on the page so they survive reparenting and layout restore. */
static void panel_controls_update(PanelHost *host) {
 if(!host->dock_button) return;
 gboolean floating=host->window!=NULL;
 gboolean maximized=floating && gtk_window_is_maximized(GTK_WINDOW(host->window));
 gtk_button_set_image(GTK_BUTTON(host->dock_button),gtk_image_new_from_icon_name(floating ? "go-home-symbolic" : "window-new-symbolic",GTK_ICON_SIZE_MENU));
 gtk_widget_set_tooltip_text(host->dock_button,insight_tr(floating ? "Dock this panel" : "Float this panel"));
 gtk_widget_set_sensitive(host->maximize_button,floating);gtk_widget_set_visible(host->maximize_button,floating);
 if(host->minimize_button) { gtk_widget_set_sensitive(host->minimize_button,floating);gtk_widget_set_visible(host->minimize_button,floating); }
 if(host->group_button) gtk_widget_set_visible(host->group_button,floating);
 if(host->drag_handle) gtk_widget_set_visible(host->drag_handle,floating);
 if(host->ungroup_item) gtk_widget_set_sensitive(host->ungroup_item,floating && panel_window_count(host->window)>1);
 if(host->opacity_button) { gtk_widget_set_visible(host->opacity_button,floating && wb.transparent_panels);gchar *tip=insight_i18n_printf("Floating panel opacity: %d%%",host->opacity);gtk_widget_set_tooltip_text(host->opacity_button,tip);g_free(tip); }
 if(host->window) insight_appearance_panel_opacity(host->window,wb.transparent_panels ? host->opacity:100);
 if(host->opacity_button) gtk_widget_set_sensitive(host->opacity_button,insight_appearance_panel_opacity_available() && (!host->window || !g_object_get_data(G_OBJECT(host->window),"insight-independent-opacity")));
 gtk_button_set_image(GTK_BUTTON(host->maximize_button),gtk_image_new_from_icon_name(maximized ? "view-restore-symbolic" : "view-fullscreen-symbolic",GTK_ICON_SIZE_MENU));
 gtk_widget_set_tooltip_text(host->maximize_button,insight_tr(maximized ? "Restore panel size" : "Maximize floating panel"));
}
static void panel_geometry_capture(PanelHost *host) {
 if(!host->window || host->minimized) return;
 GdkWindow *window=gtk_widget_get_window(host->window);
 if(window && !(gdk_window_get_state(window)&(GDK_WINDOW_STATE_MAXIMIZED|GDK_WINDOW_STATE_FULLSCREEN|GDK_WINDOW_STATE_ICONIFIED))) {
  gtk_window_get_position(GTK_WINDOW(host->window),&host->x,&host->y);gtk_window_get_size(GTK_WINDOW(host->window),&host->width,&host->height);
 }
}
static void panel_window_refresh(GtkWidget *window) {
 if(!window) return;
 GString *title=g_string_new("Geany Insight — ");guint count=0;
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window==window) { if(count++) g_string_append(title," / ");g_string_append(title,insight_tr(wb.hosts[i].label));panel_controls_update(&wb.hosts[i]); }
 if(count) gtk_window_set_title(GTK_WINDOW(window),title->str);
 g_string_free(title,TRUE);panel_tray_sync();
}
static gboolean panel_configured(GtkWidget *widget,GdkEventConfigure *event,gpointer data) {
 (void)event;(void)data;for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window==widget) panel_geometry_capture(&wb.hosts[i]);return FALSE;
}
static gboolean panel_window_state(GtkWidget *widget,GdkEventWindowState *event,gpointer data) {
 (void)data;guint owner=panel_window_owner(widget);if(owner>=wb.host_count) return FALSE;
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window==widget) { if(!wb.hosts[i].minimized) wb.hosts[i].maximized=(event->new_window_state&GDK_WINDOW_STATE_MAXIMIZED)!=0;panel_controls_update(&wb.hosts[i]); }
 if(event->new_window_state&GDK_WINDOW_STATE_ICONIFIED) panel_minimize(owner);
 return FALSE;
}
static void dock_panel(guint index) {
 if(index>=wb.host_count) return;
 PanelHost *host=&wb.hosts[index]; if(!host->window) return;
 panel_geometry_capture(host); host->visible=gtk_widget_get_visible(host->window);
 GtkWidget *window=host->window; host->window=NULL;host->minimized=FALSE;panel_tray_sync(); g_object_ref(host->page); gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(host->page)),host->page);
 if(host->pane) gtk_paned_pack2(GTK_PANED(host->home),host->page,FALSE,FALSE);
 else {
  GtkWidget *placeholder=g_object_get_data(G_OBJECT(host->page),"insight-dock-placeholder");if(placeholder) { gtk_widget_destroy(placeholder);g_object_set_data(G_OBJECT(host->page),"insight-dock-placeholder",NULL); }
  gint position=0; for(gint n=0;n<gtk_notebook_get_n_pages(GTK_NOTEBOOK(host->home));n++) { GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(host->home),n); guint order=(guint)n;
   for(guint j=0;j<wb.host_count;j++) if(wb.hosts[j].page==page && wb.hosts[j].home==host->home) { order=wb.hosts[j].order; break; }
   if(order<host->order) position++;
  }
  gtk_notebook_insert_page(GTK_NOTEBOOK(host->home),host->page,gtk_label_new(host->label),position);
 }
 g_object_unref(host->page); gtk_widget_show(host->page);if(!panel_window_count(window)) gtk_widget_destroy(window);else panel_window_refresh(window);
 panel_controls_update(host);
}
static void dock_all_panels(void) { for(guint i=0;i<wb.host_count;i++) dock_panel(i); }
static void dock_panel_selected(guint index) {
 dock_panel(index);if(index>=wb.host_count) return;
 PanelHost *host=&wb.hosts[index];
 if(!host->pane) gtk_notebook_set_current_page(GTK_NOTEBOOK(host->home),gtk_notebook_page_num(GTK_NOTEBOOK(host->home),host->page));
}
static gboolean panel_closed(GtkWidget *widget,GdkEvent *event,gpointer data) {
 (void)event;(void)data;for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window==widget) dock_panel_selected(i);return TRUE;
}
static void detach_panel(guint index) {
 if(index>=wb.host_count) return;
 PanelHost *host=&wb.hosts[index]; if(host->window) { panel_restore(index); return; }
 GtkWidget *parent=gtk_widget_get_parent(host->page); if(parent!=host->home) return;host->minimized=FALSE;
 host->window=gtk_window_new(GTK_WINDOW_TOPLEVEL); gchar *title=g_strdup_printf("Geany Insight — %s",host->label); gtk_window_set_title(GTK_WINDOW(host->window),title); g_free(title);
 gtk_window_set_transient_for(GTK_WINDOW(host->window),GTK_WINDOW(main_widgets.window)); gtk_window_set_default_size(GTK_WINDOW(host->window),host->width>=300 ? host->width : (host->pane ? 900 : 480),host->height>=200 ? host->height : 600);
 g_object_ref(host->page); gtk_container_remove(GTK_CONTAINER(host->home),host->page); gtk_container_add(GTK_CONTAINER(host->window),host->page); g_object_unref(host->page);
 if(index==8) { GtkWidget *placeholder=gtk_label_new("File Symbols is in a floating window");gtk_notebook_insert_page(GTK_NOTEBOOK(host->home),placeholder,gtk_label_new("File Symbols"),0);g_object_set_data(G_OBJECT(host->page),"insight-dock-placeholder",placeholder);gtk_widget_show(placeholder); }
 if(wb.navigation_accels) gtk_window_add_accel_group(GTK_WINDOW(host->window),wb.navigation_accels);
 if(wb.lua_accels) gtk_window_add_accel_group(GTK_WINDOW(host->window),wb.lua_accels);
 g_signal_connect(host->window,"delete-event",G_CALLBACK(panel_closed),GUINT_TO_POINTER(index));
 g_signal_connect(host->window,"window-state-event",G_CALLBACK(panel_window_state),GUINT_TO_POINTER(index));
 g_signal_connect(host->window,"configure-event",G_CALLBACK(panel_configured),GUINT_TO_POINTER(index));
 insight_appearance_decorate(host->window);panel_controls_update(host); gtk_widget_show_all(host->window);
 if(host->width>=300) { GdkMonitor *monitor=gdk_display_get_monitor_at_point(gdk_display_get_default(),host->x,host->y); if(monitor) { GdkRectangle area; gdk_monitor_get_workarea(monitor,&area); host->width=MIN(host->width,MAX(300,area.width)); host->height=MIN(host->height,MAX(200,area.height)); host->x=CLAMP(host->x,area.x,area.x+MAX(0,area.width-host->width)); host->y=CLAMP(host->y,area.y,area.y+MAX(0,area.height-host->height)); gtk_window_resize(GTK_WINDOW(host->window),host->width,host->height); } gtk_window_move(GTK_WINDOW(host->window),host->x,host->y); }
 if(host->maximized) gtk_window_maximize(GTK_WINDOW(host->window));
}
static void panel_restore_command(GtkButton *button,gpointer data) { (void)button;panel_restore(GPOINTER_TO_UINT(data)); }
static void panel_restore(guint index) {
 if(index>=wb.host_count) return;
 PanelHost *host=&wb.hosts[index];if(!host->window) return;
 gboolean maximized=host->maximized;for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window==host->window) wb.hosts[i].minimized=FALSE;gtk_window_deiconify(GTK_WINDOW(host->window));gtk_widget_show(host->window);if(maximized) gtk_window_maximize(GTK_WINDOW(host->window));gtk_window_present(GTK_WINDOW(host->window));panel_tray_sync();
}
static void panel_minimize(guint index) {
 if(index>=wb.host_count) return;
 PanelHost *host=&wb.hosts[index];if(!host->window || host->minimized) return;
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window==host->window) { panel_geometry_capture(&wb.hosts[i]);wb.hosts[i].minimized=TRUE; }
 gtk_window_deiconify(GTK_WINDOW(host->window));gtk_widget_hide(host->window);panel_tray_sync();
}
static void panel_minimize_command(GtkButton *button,gpointer data) { (void)button;panel_minimize(GPOINTER_TO_UINT(data)); }
static void panel_tray_sync(void) {
 if(!wb.tray) return;
 guint count=0;
 for(guint i=0;i<wb.host_count;i++) {
  PanelHost *host=&wb.hosts[i];gboolean minimized=host->window && host->minimized && panel_window_owner(host->window)==i;
  if(minimized && !host->tray_button) {
   host->tray_button=gtk_button_new_with_label(host->label);gchar *name=g_strdup_printf("insight-panel-tray-%u",i);gtk_widget_set_name(host->tray_button,name);g_free(name);
   gtk_widget_set_tooltip_text(host->tray_button,"Restore this floating panel");gtk_box_pack_start(GTK_BOX(wb.tray_buttons),host->tray_button,FALSE,FALSE,0);g_signal_connect(host->tray_button,"clicked",G_CALLBACK(panel_restore_command),GUINT_TO_POINTER(i));
  }
  if(host->tray_button) { guint members=panel_window_count(host->window);gchar *label=members>1 ? g_strdup_printf("%s +%u",insight_tr(host->label),members-1) : g_strdup(insight_tr(host->label));gtk_button_set_label(GTK_BUTTON(host->tray_button),label);g_free(label);gtk_widget_set_visible(host->tray_button,minimized); }
  if(minimized) count++;
 }
 gtk_widget_set_visible(wb.tray,wb.tray_enabled && count>0);
}
static void panel_tray_toggled(GtkCheckMenuItem *item,gpointer data) { (void)data;wb.tray_enabled=gtk_check_menu_item_get_active(item);panel_tray_sync(); }
static void panel_restore_all(GtkMenuItem *item,gpointer data) { (void)item;(void)data;for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].minimized) panel_restore(i); }
static void panel_tray_create(void) {
 wb.tray=gtk_scrolled_window_new(NULL,NULL);gtk_widget_set_name(wb.tray,"insight-panel-tab-tray");gtk_widget_set_no_show_all(wb.tray,TRUE);
 gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(wb.tray),GTK_POLICY_AUTOMATIC,GTK_POLICY_NEVER);
 wb.tray_buttons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);gtk_container_add(GTK_CONTAINER(wb.tray),wb.tray_buttons);gtk_widget_show(wb.tray_buttons);
 GtkWidget *box=ui_lookup_widget(main_widgets.window,"vbox1");gtk_box_pack_start(GTK_BOX(box),wb.tray,FALSE,FALSE,0);gtk_box_reorder_child(GTK_BOX(box),wb.tray,2);
 wb.tray_toggle=gtk_check_menu_item_new_with_label("Tab Tray");gtk_widget_set_name(wb.tray_toggle,"insight-panel-tab-tray-toggle");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.tray_toggle),wb.tray_enabled);g_signal_connect(wb.tray_toggle,"toggled",G_CALLBACK(panel_tray_toggled),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(wb.panel_commands),wb.tray_toggle);
 GtkWidget *restore=gtk_menu_item_new_with_label("Restore minimized panels");gtk_widget_set_name(restore,"insight-panel-restore-all");g_signal_connect(restore,"activate",G_CALLBACK(panel_restore_all),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(wb.panel_commands),restore);gtk_widget_show(wb.tray_toggle);gtk_widget_show(restore);
}
static void panel_toggle_docking(GtkButton *button,gpointer data) {
 (void)button;guint index=GPOINTER_TO_UINT(data);
 if(wb.hosts[index].window) dock_panel_selected(index);else detach_panel(index);
}
static void panel_toggle_maximized(GtkButton *button,gpointer data) {
 (void)button;PanelHost *host=&wb.hosts[GPOINTER_TO_UINT(data)];if(!host->window) return;
 if(gtk_window_is_maximized(GTK_WINDOW(host->window))) gtk_window_unmaximize(GTK_WINDOW(host->window));
 else gtk_window_maximize(GTK_WINDOW(host->window));
}
static void panel_opacity_changed(GtkScaleButton *button,gdouble value,gpointer data) {
 (void)button;if(wb.panel_group_setting) return;PanelHost *host=&wb.hosts[GPOINTER_TO_UINT(data)];gint opacity=CLAMP((gint)(value+0.5),40,100);
 wb.panel_group_setting=TRUE;
 for(guint i=0;i<wb.host_count;i++) if(i==GPOINTER_TO_UINT(data) || (host->window && wb.hosts[i].window==host->window)) { wb.hosts[i].opacity=opacity;gtk_scale_button_set_value(GTK_SCALE_BUTTON(wb.hosts[i].opacity_button),opacity);panel_controls_update(&wb.hosts[i]); }
 wb.panel_group_setting=FALSE;
}
static void panel_transparency_toggled(GtkCheckMenuItem *item,gpointer data) {
 (void)data;wb.transparent_panels=gtk_check_menu_item_get_active(item);for(guint i=0;i<wb.host_count;i++) panel_controls_update(&wb.hosts[i]);
}
static void panel_group_join(guint source,guint target) {
 if(source>=8 || target>=8 || source==target) return;
 PanelHost *from=&wb.hosts[source],*to=&wb.hosts[target];
 if(!from->window) detach_panel(source);
 if(!to->window) detach_panel(target);
 if(!from->window || !to->window) return;
 if(from->window==to->window) { GtkWidget *book=gtk_widget_get_parent(from->page);if(GTK_IS_NOTEBOOK(book)) gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),from->page));return; }
 GtkWidget *old=from->window,*window=to->window,*book=g_object_get_data(G_OBJECT(window),"insight-floating-group");
 if(!book) {
  book=gtk_notebook_new();gtk_widget_set_name(book,"insight-floating-group");gtk_notebook_set_scrollable(GTK_NOTEBOOK(book),TRUE);
  g_object_ref(to->page);gtk_container_remove(GTK_CONTAINER(window),to->page);gtk_container_add(GTK_CONTAINER(window),book);gtk_notebook_append_page(GTK_NOTEBOOK(book),to->page,gtk_label_new(to->label));gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(book),to->page,TRUE);g_object_unref(to->page);g_object_set_data(G_OBJECT(window),"insight-floating-group",book);
 }
 g_object_ref(from->page);gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(from->page)),from->page);from->window=window;from->minimized=to->minimized;from->maximized=to->maximized;from->opacity=to->opacity;from->x=to->x;from->y=to->y;from->width=to->width;from->height=to->height;
 gtk_notebook_append_page(GTK_NOTEBOOK(book),from->page,gtk_label_new(from->label));gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(book),from->page,TRUE);g_object_unref(from->page);gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),from->page));
 gtk_scale_button_set_value(GTK_SCALE_BUTTON(from->opacity_button),to->opacity);
 if(!panel_window_count(old)) gtk_widget_destroy(old);else panel_window_refresh(old);
 gtk_widget_show_all(book);panel_geometry_capture(from);panel_window_refresh(window);panel_restore(target);
}
static void panel_group_capture(guint index,InsightLayout *record) {
 PanelHost *host=&wb.hosts[index];if(index>=8 || panel_window_count(host->window)<2) return;
 GtkWidget *book=gtk_widget_get_parent(host->page);if(!GTK_IS_NOTEBOOK(book)) return;
 record->groups[index]=(gint)panel_window_owner(host->window)+1;record->group_order[index]=gtk_notebook_page_num(GTK_NOTEBOOK(book),host->page);
 GtkWidget *active=gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),gtk_notebook_get_current_page(GTK_NOTEBOOK(book)));
 for(guint i=0;i<8;i++) if(wb.hosts[i].page==active) record->group_active[index]=(gint)i+1;
}
static void panel_groups_apply(const InsightLayout *record) {
 for(guint i=0;i<8;i++) if(record->groups[i] && record->groups[i]!=(gint)i+1) panel_group_join(i,(guint)record->groups[i]-1);
 for(guint root=0;root<8;root++) if(record->groups[root]==(gint)root+1) {
  GtkWidget *book=gtk_widget_get_parent(wb.hosts[root].page);
  for(guint rank=0;rank<8;rank++) for(guint i=0;i<8;i++) if(record->groups[i]==(gint)root+1 && record->group_order[i]==(gint)rank) gtk_notebook_reorder_child(GTK_NOTEBOOK(book),wb.hosts[i].page,(gint)rank);
  guint active=(guint)record->group_active[root]-1;gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),wb.hosts[active].page));
 }
}
static void panel_group_command(GtkMenuItem *item,gpointer data) { (void)item;guint pair=GPOINTER_TO_UINT(data);panel_group_join(pair/16,pair%16); }
static void panel_ungroup_command(GtkMenuItem *item,gpointer data) { (void)item;guint index=GPOINTER_TO_UINT(data);if(index>=8 || panel_window_count(wb.hosts[index].window)<2) return;dock_panel(index);detach_panel(index); }
typedef struct { guint source,target;guint64 epoch; } PanelDrop;
static gboolean panel_drop_apply(gpointer data) {
 PanelDrop *drop=data;if(wb.alive && drop->epoch==workbench_epoch) panel_group_join(drop->source,drop->target);return G_SOURCE_REMOVE;
}
static void panel_drag_data(GtkWidget *widget,GdkDragContext *context,GtkSelectionData *selection,guint info,guint time,gpointer data) {
 (void)widget;(void)context;(void)info;(void)time;guchar index=(guchar)GPOINTER_TO_UINT(data);
 gtk_selection_data_set(selection,gdk_atom_intern_static_string("application/x-geany-insight-panel"),8,&index,1);
}
static void panel_drag_received(GtkWidget *widget,GdkDragContext *context,gint x,gint y,GtkSelectionData *selection,guint info,guint time,gpointer data) {
 (void)widget;(void)x;(void)y;(void)info;guint target=GPOINTER_TO_UINT(data);const guchar *bytes=gtk_selection_data_get_data(selection);gboolean valid=FALSE;guint source=8;
 if(wb.alive && target<8 && gtk_selection_data_get_length(selection)==1 && gtk_selection_data_get_format(selection)==8 && gtk_selection_data_get_data_type(selection)==gdk_atom_intern_static_string("application/x-geany-insight-panel") && bytes) {
  source=bytes[0];valid=source<8 && source!=target && wb.hosts[source].window && gtk_drag_get_source_widget(context)==wb.hosts[source].drag_handle;
 }
 gtk_drag_finish(context,valid,FALSE,time);
 if(valid) { PanelDrop *drop=g_new(PanelDrop,1);*drop=(PanelDrop){source,target,workbench_epoch};g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,panel_drop_apply,drop,g_free); }
}
static void panel_drag_attach(guint index,GtkWidget *header) {
 if(index>=8) return;
 PanelHost *host=&wb.hosts[index];GtkTargetEntry targets[]={{"application/x-geany-insight-panel",GTK_TARGET_SAME_APP,0}};
 host->drag_handle=gtk_event_box_new();gtk_widget_set_name(host->drag_handle,"insight-panel-drag-handle");gtk_widget_set_no_show_all(host->drag_handle,TRUE);gtk_widget_set_size_request(host->drag_handle,22,22);gtk_widget_set_tooltip_text(host->drag_handle,"Drag this panel onto a project panel header");
 GtkWidget *image=gtk_image_new_from_icon_name("view-grid-symbolic",GTK_ICON_SIZE_MENU);gtk_container_add(GTK_CONTAINER(host->drag_handle),image);gtk_widget_show(image);gtk_box_pack_start(GTK_BOX(header),host->drag_handle,FALSE,FALSE,0);
 gtk_drag_source_set(host->drag_handle,GDK_BUTTON1_MASK,targets,G_N_ELEMENTS(targets),GDK_ACTION_MOVE);gtk_drag_source_set_icon_name(host->drag_handle,"tab-new-symbolic");g_signal_connect(host->drag_handle,"drag-data-get",G_CALLBACK(panel_drag_data),GUINT_TO_POINTER(index));
 host->drag_target=gtk_event_box_new();gtk_event_box_set_visible_window(GTK_EVENT_BOX(host->drag_target),TRUE);gtk_widget_set_name(host->drag_target,"insight-panel-drag-target");
 g_object_ref(header);gtk_container_remove(GTK_CONTAINER(host->page),header);gtk_container_add(GTK_CONTAINER(host->drag_target),header);g_object_unref(header);gtk_box_pack_start(GTK_BOX(host->page),host->drag_target,FALSE,FALSE,0);gtk_box_reorder_child(GTK_BOX(host->page),host->drag_target,0);
 gtk_drag_dest_set(host->drag_target,GTK_DEST_DEFAULT_ALL,targets,G_N_ELEMENTS(targets),GDK_ACTION_MOVE);g_signal_connect(host->drag_target,"drag-data-received",G_CALLBACK(panel_drag_received),GUINT_TO_POINTER(index));
 GtkCssProvider *style=gtk_css_provider_new();gtk_css_provider_load_from_data(style,"#insight-panel-drag-target:drop(active) { background-color: @theme_selected_bg_color; }",-1,NULL);gtk_style_context_add_provider(gtk_widget_get_style_context(host->drag_target),GTK_STYLE_PROVIDER(style),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);g_object_unref(style);
 gtk_widget_show_all(host->drag_target);
}
static void panel_controls_attach(guint index) {
 PanelHost *host=&wb.hosts[index];GtkWidget *header=NULL;
 GList *children=gtk_container_get_children(GTK_CONTAINER(host->page));
 for(GList *p=children;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-panel-help-header")) { header=p->data;break; }
 g_list_free(children);
 if(index==9) header=g_object_get_data(G_OBJECT(wb.box),"insight-context-rail");
 if(!header) { header=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);gtk_widget_set_name(header,"insight-panel-controls-header");gtk_box_pack_start(GTK_BOX(host->page),header,FALSE,FALSE,0);gtk_box_reorder_child(GTK_BOX(host->page),header,0); }
 const gchar *opacity_icons[]={"display-brightness-symbolic",NULL};
 host->opacity_button=gtk_scale_button_new(GTK_ICON_SIZE_MENU,40,100,5,opacity_icons);gtk_widget_set_name(host->opacity_button,"insight-panel-opacity");gtk_widget_set_tooltip_text(host->opacity_button,"Floating panel opacity");gtk_widget_set_no_show_all(host->opacity_button,TRUE);gtk_scale_button_set_value(GTK_SCALE_BUTTON(host->opacity_button),host->opacity);gtk_button_set_relief(GTK_BUTTON(host->opacity_button),GTK_RELIEF_NONE);gtk_widget_set_size_request(host->opacity_button,22,22);gtk_box_pack_end(GTK_BOX(header),host->opacity_button,FALSE,FALSE,0);g_signal_connect(host->opacity_button,"value-changed",G_CALLBACK(panel_opacity_changed),GUINT_TO_POINTER(index));
 host->dock_button=gtk_button_new();host->maximize_button=gtk_button_new();host->minimize_button=gtk_button_new();gtk_widget_set_no_show_all(host->maximize_button,TRUE);gtk_widget_set_no_show_all(host->minimize_button,TRUE);
 GtkWidget *buttons[]={host->dock_button,host->maximize_button,host->minimize_button};
 for(guint n=0;n<G_N_ELEMENTS(buttons);n++) { gtk_button_set_relief(GTK_BUTTON(buttons[n]),GTK_RELIEF_NONE);gtk_widget_set_size_request(buttons[n],22,22);gtk_box_pack_end(GTK_BOX(header),buttons[n],FALSE,FALSE,0); }
 gtk_widget_set_name(host->minimize_button,"insight-panel-minimize");gtk_button_set_image(GTK_BUTTON(host->minimize_button),gtk_image_new_from_icon_name("window-minimize-symbolic",GTK_ICON_SIZE_MENU));gtk_widget_set_tooltip_text(host->minimize_button,"Minimize panel to Tab Tray");
 gtk_widget_set_name(host->dock_button,"insight-panel-toggle-dock");gtk_widget_set_name(host->maximize_button,"insight-panel-toggle-maximize");
 g_signal_connect(host->minimize_button,"clicked",G_CALLBACK(panel_minimize_command),GUINT_TO_POINTER(index));
 g_signal_connect(host->dock_button,"clicked",G_CALLBACK(panel_toggle_docking),GUINT_TO_POINTER(index));
 g_signal_connect(host->maximize_button,"clicked",G_CALLBACK(panel_toggle_maximized),GUINT_TO_POINTER(index));
 if(index<8) {
  host->group_button=gtk_menu_button_new();gtk_widget_set_name(host->group_button,"insight-panel-group-button");gtk_widget_set_tooltip_text(host->group_button,"Group this floating panel");gtk_button_set_image(GTK_BUTTON(host->group_button),gtk_image_new_from_icon_name("tab-new-symbolic",GTK_ICON_SIZE_MENU));gtk_button_set_relief(GTK_BUTTON(host->group_button),GTK_RELIEF_NONE);gtk_widget_set_no_show_all(host->group_button,TRUE);gtk_box_pack_end(GTK_BOX(header),host->group_button,FALSE,FALSE,0);
  GtkWidget *menu=gtk_menu_new();gtk_menu_button_set_popup(GTK_MENU_BUTTON(host->group_button),menu);
  for(guint target=0;target<8;target++) if(target!=index) { GtkWidget *item=gtk_menu_item_new_with_label(wb.hosts[target].label);gchar *name=g_strdup_printf("insight-panel-group-with-%u",target);gtk_widget_set_name(item,name);g_free(name);g_signal_connect(item,"activate",G_CALLBACK(panel_group_command),GUINT_TO_POINTER(index*16+target));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item); }
  host->ungroup_item=gtk_menu_item_new_with_label("Float this panel separately");gtk_widget_set_name(host->ungroup_item,"insight-panel-ungroup");g_signal_connect(host->ungroup_item,"activate",G_CALLBACK(panel_ungroup_command),GUINT_TO_POINTER(index));gtk_menu_shell_append(GTK_MENU_SHELL(menu),host->ungroup_item);gtk_widget_show_all(menu);
 }
 panel_drag_attach(index,header);panel_controls_update(host);gtk_widget_show_all(header);
}
static void float_panel_command(GtkMenuItem *item,gpointer data) { (void)item; detach_panel(GPOINTER_TO_UINT(data)); }
static void dock_panels_command(GtkMenuItem *item,gpointer data) { (void)item; (void)data; dock_all_panels(); }
static GtkWidget *narrow_panel_host(GtkWidget *child);
static void constrain_panel_rows(GtkWidget *widget);
static void register_panels(void) {
 wb.host_count=10;panel_tray_create();
 wb.transparency_toggle=gtk_check_menu_item_new_with_label("Enable transparent panels");gtk_widget_set_name(wb.transparency_toggle,"insight-panel-transparency-toggle");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.transparency_toggle),wb.transparent_panels);g_signal_connect(wb.transparency_toggle,"toggled",G_CALLBACK(panel_transparency_toggled),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(wb.panel_commands),wb.transparency_toggle);gtk_widget_show(wb.transparency_toggle);
 for(guint i=0;i<8;i++) { PanelHost *host=&wb.hosts[i]; host->home=wb.project_notebook; host->page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(host->home),(gint)i); host->order=i; host->label=g_strdup(gtk_notebook_get_tab_label_text(GTK_NOTEBOOK(host->home),host->page)); }
 PanelHost *left=&wb.hosts[8]; left->home=main_widgets.sidebar_notebook; left->page=wb.local_entry; while(gtk_widget_get_parent(left->page)!=left->home) left->page=gtk_widget_get_parent(left->page); left->order=(guint)gtk_notebook_page_num(GTK_NOTEBOOK(left->home),left->page); left->label=g_strdup("File Symbols");
 PanelHost *context=&wb.hosts[9]; context->home=main_widgets.message_window_notebook; context->page=wb.box; context->order=(guint)gtk_notebook_page_num(GTK_NOTEBOOK(context->home),wb.box); context->pane=FALSE; context->label=g_strdup("Context");
 const gchar *topics[]={"panel-project-files.md","panel-project-symbols.md","panel-project-folders.md","panel-relations.md","panel-categories.md","panel-file-types.md","panel-bookmarks.md","panel-clips.md","panel-file-symbols.md","panel-context.md"};
 for(guint i=0;i<wb.host_count;i++) { PanelHost *host=&wb.hosts[i]; if(i<9) {
   /* Wide controls must not impose their minimum on either side splitter.
    * Keep panel help outside the viewport so it stays at the visible corner. */
   GtkWidget *old=host->page,*box=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
   gint position=gtk_notebook_page_num(GTK_NOTEBOOK(host->home),old);
   g_object_ref(old); gtk_container_remove(GTK_CONTAINER(host->home),old);
   compact_panel_control(old);constrain_panel_rows(old);gtk_box_pack_start(GTK_BOX(box),narrow_panel_host(old),TRUE,TRUE,0);
   g_object_unref(old);gtk_notebook_insert_page(GTK_NOTEBOOK(host->home),box,gtk_label_new(host->label),position);
   GtkWidget *tab=gtk_notebook_get_tab_label(GTK_NOTEBOOK(host->home),box);if(GTK_IS_LABEL(tab)) { gtk_label_set_ellipsize(GTK_LABEL(tab),PANGO_ELLIPSIZE_END);gtk_label_set_width_chars(GTK_LABEL(tab),1); }host->page=box; gtk_widget_show_all(box);
  } if(i!=9) insight_panel_help_attach(host->page,topics[i]);g_object_set_data(G_OBJECT(host->page),"insight-panel-id",GUINT_TO_POINTER(i+1)); panel_controls_attach(i); }
 insight_panel_help_attach(wb.search_page,"panel-search-results.md");
 insight_panel_help_notebook(GTK_NOTEBOOK(main_widgets.sidebar_notebook),"panel-native-symbols.md"); insight_panel_help_notebook(GTK_NOTEBOOK(main_widgets.message_window_notebook),"panel-messages.md"); insight_panel_help_notebook(GTK_NOTEBOOK(main_widgets.notebook),"panel-editor.md");
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
static void main_geometry_capture(void) {
 if(wb.main_apply_source) return;
 GtkWindow *window=GTK_WINDOW(main_widgets.window);GdkWindow *native=gtk_widget_get_window(GTK_WIDGET(window));
 if(native && !(gdk_window_get_state(native)&(GDK_WINDOW_STATE_MAXIMIZED|GDK_WINDOW_STATE_FULLSCREEN|GDK_WINDOW_STATE_ICONIFIED))) {
  gint width,height;gtk_window_get_size(window,&width,&height);
  if(width>=400 && height>=220) { gtk_window_get_position(window,&wb.main_bounds.x,&wb.main_bounds.y);wb.main_bounds.width=width;wb.main_bounds.height=height;wb.main_bounds.set=TRUE; }
 }
 wb.main_bounds.maximized=gtk_window_is_maximized(window);
}
static gboolean main_geometry_changed(GtkWidget *widget,GdkEvent *event,gpointer data) { (void)widget;(void)event;(void)data;main_geometry_capture();return FALSE; }
/* Window managers apply maximize requests asynchronously. Resize the normal
 * window first so a later Restore uses the saved bounds, not the previous layout. */
static gboolean main_geometry_apply_step(gpointer data) {
 (void)data;GtkWindow *window=GTK_WINDOW(main_widgets.window);InsightPanelLayout *target=&wb.main_target;
 if(wb.main_apply_stage==1) {
  if(gtk_window_is_maximized(window) && wb.main_apply_ticks++<40) return G_SOURCE_CONTINUE;
  gtk_window_resize(window,target->width,target->height);gtk_window_move(window,target->x,target->y);wb.main_apply_stage=2;wb.main_apply_ticks=0;return G_SOURCE_CONTINUE;
 }
 gint width,height;gtk_window_get_size(window,&width,&height);GtkRequisition minimum;gtk_widget_get_preferred_size(GTK_WIDGET(window),&minimum,NULL);
 if((width!=MAX(target->width,minimum.width) || height!=MAX(target->height,minimum.height)) && wb.main_apply_ticks++<20) return G_SOURCE_CONTINUE;
 wb.main_apply_source=0;main_geometry_capture();wb.main_bounds.maximized=target->maximized;
 if(target->maximized) gtk_window_maximize(window);
 return G_SOURCE_REMOVE;
}
static void main_geometry_apply(const InsightPanelLayout *saved) {
 if(!saved->set) return;
 InsightPanelLayout next=*saved;GdkDisplay *display=gtk_widget_get_display(main_widgets.window);GdkMonitor *monitor=gdk_display_get_monitor_at_point(display,next.x,next.y);
 if(!monitor && gdk_display_get_n_monitors(display)) monitor=gdk_display_get_monitor(display,0);
 if(monitor) { GdkRectangle area;gdk_monitor_get_workarea(monitor,&area);next.width=MIN(next.width,MAX(1,area.width));next.height=MIN(next.height,MAX(1,area.height));next.x=CLAMP(next.x,area.x,area.x+MAX(0,area.width-next.width));next.y=CLAMP(next.y,area.y,area.y+MAX(0,area.height-next.height)); }
 if(wb.main_apply_source) g_source_remove(wb.main_apply_source);
 wb.main_bounds=next;wb.main_target=next;wb.main_apply_stage=1;wb.main_apply_ticks=0;
 gtk_window_unmaximize(GTK_WINDOW(main_widgets.window));wb.main_apply_source=g_timeout_add(25,main_geometry_apply_step,NULL);
}
static gboolean capture_layout(InsightLayout *out) {
 if(wb.float_window) { gtk_window_get_position(GTK_WINDOW(wb.float_window),&wb.float_x,&wb.float_y); gtk_window_get_size(GTK_WINDOW(wb.float_window),&wb.float_width,&wb.float_height); }
 main_geometry_capture();InsightLayout record={.valid=TRUE};gint *v=record.values;
 record.main_window=wb.main_bounds;
 if(!record.main_window.set) { record.main_window.set=TRUE;record.main_window.width=900;record.main_window.height=700; }
 record.main_window.x=CLAMP(record.main_window.x,-30000,30000);record.main_window.y=CLAMP(record.main_window.y,-30000,30000);
 v[IL_LEFT]=ui_prefs.sidebar_visible; v[IL_PROJECT]=gtk_widget_get_visible(wb.project_notebook); v[IL_CONTEXT]=wb.context_visible;
 v[IL_FOLLOW]=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow)); v[IL_WRAP]=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.wrap)); v[IL_FLOAT]=wb.float_window!=NULL;
 v[IL_LEFT_SPLIT]=gtk_paned_get_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"hpaned1"))); v[IL_RIGHT_SPLIT]=gtk_paned_get_position(GTK_PANED(wb.right_pane)); v[IL_CONTEXT_SPLIT]=gtk_paned_get_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"vpaned1"))); v[IL_TAB]=project_tab_identity(); v[IL_DETACHED]=(gint)detached_mask(); v[IL_HOST_VISIBLE]=(gint)visible_host_mask();
 v[IL_TRANSPARENT_PANELS]=wb.transparent_panels;v[IL_TRAY_VISIBLE]=wb.tray_enabled;for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window && wb.hosts[i].minimized) v[IL_HOST_MINIMIZED]|=1<<i;
 v[IL_X]=CLAMP(wb.float_x,-30000,30000); v[IL_Y]=CLAMP(wb.float_y,-30000,30000); v[IL_WIDTH]=wb.float_width>=300 ? MIN(wb.float_width,4000) : 440; v[IL_HEIGHT]=wb.float_height>=200 ? MIN(wb.float_height,4000) : 620;
 for(guint i=0;i<wb.host_count;i++) {
  PanelHost *host=&wb.hosts[i];panel_geometry_capture(host);
  panel_group_capture(i,&record);record.opacity[i]=host->opacity;record.panels[i]=(InsightPanelLayout){TRUE,host->maximized,CLAMP(host->x,-30000,30000),CLAMP(host->y,-30000,30000),host->width>=300 ? MIN(host->width,4000) : 480,host->height>=200 ? MIN(host->height,4000) : 600};
 }
 insight_window_list_layout_capture(&record);
 if(!insight_layout_valid(&record)) return FALSE;
 *out=record;return TRUE;
}
static void save_layout(GtkMenuItem *item,gpointer data) {
 (void)item;guint slot=GPOINTER_TO_UINT(data);if(slot>=4) return;
 InsightLayout record;if(!capture_layout(&record)) { ui_set_statusbar(TRUE,"Layout cannot be saved: invalid panel geometry");return; }
 wb.layouts[slot]=record;ui_set_statusbar(TRUE,"Layout %c stored",'A'+(gint)slot);save_workbench_settings();
}
static void reset_layouts(GtkMenuItem *item,gpointer data) { (void)item; (void)data; memset(wb.layouts,0,sizeof wb.layouts); ui_set_statusbar(TRUE,"Saved layouts reset; A–D now use presets"); save_workbench_settings(); }
static void apply_layout_record(const InsightLayout *record) {
 wb.initial_right_split=-1;dock_all_panels();dock_project(NULL,NULL);main_geometry_apply(&record->main_window);
  const gint *v=record->values;
  wb.float_x=v[IL_X]; wb.float_y=v[IL_Y]; wb.float_width=v[IL_WIDTH]; wb.float_height=v[IL_HEIGHT];
  gtk_paned_set_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"hpaned1")),v[IL_LEFT_SPLIT]); gtk_paned_set_position(GTK_PANED(wb.right_pane),v[IL_RIGHT_SPLIT]); gtk_paned_set_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"vpaned1")),v[IL_CONTEXT_SPLIT]);
  select_project_tab(v[IL_TAB]); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),v[IL_FOLLOW]); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.wrap),v[IL_WRAP]);
  if(v[IL_FLOAT]) float_project(NULL,NULL);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.transparency_toggle),v[IL_TRANSPARENT_PANELS]);
  for(guint i=0;i<wb.host_count;i++) {
   PanelHost *host=&wb.hosts[i];const InsightPanelLayout *p=&record->panels[i];host->opacity=record->opacity[i] ? record->opacity[i] : 100;gtk_scale_button_set_value(GTK_SCALE_BUTTON(host->opacity_button),host->opacity);
   if(p->set) { host->x=p->x;host->y=p->y;host->width=p->width;host->height=p->height;host->maximized=p->maximized; }
   if(v[IL_DETACHED]&(1<<i)) detach_panel(i);
  }
  panel_groups_apply(record);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.tray_toggle),v[IL_TRAY_VISIBLE]);
  for(guint i=0;i<wb.host_count;i++) if(v[IL_HOST_MINIMIZED]&(1<<i)) panel_minimize(i);
  insight_window_list_layout_apply(record);
  set_panel_visibility(v[IL_LEFT],v[IL_PROJECT],v[IL_CONTEXT]); for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window) gtk_widget_set_visible(wb.hosts[i].window,!wb.hosts[i].minimized && (v[IL_HOST_VISIBLE]&(1<<i))!=0); return;
}
/* Preserve native source-window focus and document lifetimes. Only the tools
 * palette and already detachable panel hosts move to separate top-levels. */
typedef struct { GWeakRef widget;gint position;gboolean expand,fill;guint padding;GtkPackType pack; } PaletteRow;
typedef struct { GWeakRef sci;gulong allocated;gint first,x; } PaletteView;
typedef struct { GWeakRef window;InsightWindowMotion motion;gint chrome_width,chrome_height;InsightWindowRect last; } PaletteMotion;
static struct {
 gboolean active,joining,message_visible,animate,motions_ready;
 GtkWidget *window,*bottom,*bottom_parent,*bottom_content;gboolean bottom_resize,bottom_shrink;
 GPtrArray *rows,*views;guint view_timer,view_settle;InsightLayout before;
 InsightWindowRect area,origins[INSIGHT_FLOAT_COUNT],destinations[INSIGHT_FLOAT_COUNT];
 PaletteMotion motions[INSIGHT_FLOAT_COUNT];
 guint timer,settle_ticks,arrival_ticks;gint64 deadline,started;gdouble speed;
} palette;
/* Resizing Scintilla may scroll its caret into view. Preserve each document's
 * viewport through allocation, including inactive tabs, without moving carets. */
static void palette_view_restore(GtkWidget *widget,GtkAllocation *allocation,gpointer data) {
 (void)allocation;PaletteView *view=data;ScintillaObject *sci=SCINTILLA(widget);
 scintilla_send_message(sci,SCI_SETFIRSTVISIBLELINE,view->first,0);scintilla_send_message(sci,SCI_SETXOFFSET,view->x,0);
}
static void palette_view_free(gpointer data) {
 PaletteView *view=data;GtkWidget *sci=g_weak_ref_get(&view->sci);
 if(sci) {g_signal_handler_disconnect(sci,view->allocated);g_object_unref(sci);}g_weak_ref_clear(&view->sci);g_free(view);
}
static gboolean palette_views_settle(gpointer data) {
 (void)data;
 if(palette.timer || wb.main_apply_source) palette.view_settle=0;else palette.view_settle++;
 if(palette.view_settle<3) return G_SOURCE_CONTINUE;
 palette.view_timer=0;g_clear_pointer(&palette.views,g_ptr_array_unref);return G_SOURCE_REMOVE;
}
static void palette_views_capture(void) {
 if(palette.views) return;
 palette.views=g_ptr_array_new_with_free_func(palette_view_free);palette.view_settle=0;
 guint i;foreach_document(i) {
  ScintillaObject *sci=documents[i]->editor->sci;PaletteView *view=g_new0(PaletteView,1);g_weak_ref_init(&view->sci,sci);
  view->first=scintilla_send_message(sci,SCI_GETFIRSTVISIBLELINE,0,0);view->x=scintilla_send_message(sci,SCI_GETXOFFSET,0,0);
  view->allocated=g_signal_connect_after(sci,"size-allocate",G_CALLBACK(palette_view_restore),view);g_ptr_array_add(palette.views,view);
 }
 palette.view_timer=g_timeout_add(32,palette_views_settle,NULL);
}
static void palette_compact_title(GtkWidget *window,gboolean compact) {
 GtkStyleContext *style=gtk_widget_get_style_context(window);
 if(compact) gtk_style_context_add_class(style,"insight-independent-chrome");else gtk_style_context_remove_class(style,"insight-independent-chrome");
 GtkWidget *header=gtk_window_get_titlebar(GTK_WINDOW(window));if(!GTK_IS_HEADER_BAR(header)) return;
 if(compact) {
  if(!g_object_get_data(G_OBJECT(header),"insight-original-subtitle-space")) g_object_set_data(G_OBJECT(header),"insight-original-subtitle-space",GUINT_TO_POINTER(gtk_header_bar_get_has_subtitle(GTK_HEADER_BAR(header)) ? 2:1));
  gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(header),FALSE);
 } else {
  guint previous=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(header),"insight-original-subtitle-space"));if(previous) gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(header),previous==2);g_object_set_data(G_OBJECT(header),"insight-original-subtitle-space",NULL);
 }
}
static void palette_row_free(gpointer data) { PaletteRow *row=data;g_weak_ref_clear(&row->widget);g_free(row); }
static GtkWidget *palette_part_window(guint part) {
 switch(part) {case INSIGHT_FLOAT_TOOLS:return palette.window;case INSIGHT_FLOAT_LEFT:return wb.hosts[8].window;case INSIGHT_FLOAT_RIGHT:return wb.float_window;case INSIGHT_FLOAT_BOTTOM:return palette.bottom;default:return main_widgets.window;}
}
static GtkWidget *palette_part_content(guint part) {
 switch(part) {case INSIGHT_FLOAT_TOOLS:return palette.window ? gtk_bin_get_child(GTK_BIN(palette.window)):NULL;case INSIGHT_FLOAT_LEFT:return wb.hosts[8].page;case INSIGHT_FLOAT_RIGHT:return wb.project_host;case INSIGHT_FLOAT_BOTTOM:return main_widgets.message_window_notebook;default:return main_widgets.window;}
}
static InsightWindowRect palette_frame(GtkWidget *widget) {
 InsightWindowRect rect={0,0,1,1};if(!GTK_IS_WINDOW(widget)) return rect;
 gtk_window_get_position(GTK_WINDOW(widget),&rect.x,&rect.y);gtk_window_get_size(GTK_WINDOW(widget),&rect.width,&rect.height);
 GdkWindow *native=gtk_widget_get_window(widget);
 /* Native CSD extents include the CSS shadow (84px per side with the material
  * theme). Only the visible titlebar/body frame belongs inside the workarea. */
 GtkWidget *body=gtk_bin_get_child(GTK_BIN(widget)),*title=gtk_window_get_titlebar(GTK_WINDOW(widget));
 if(native && title && body && gtk_widget_get_mapped(body)) {
  gint x=0,y=0,root_x=0,root_y=0;if(gtk_widget_translate_coordinates(body,widget,0,0,&x,&y)) {
   gdk_window_get_origin(native,&root_x,&root_y);
   return insight_window_visible_frame(rect.x,rect.y,(InsightWindowRect){root_x+x,root_y+y,gtk_widget_get_allocated_width(body),gtk_widget_get_allocated_height(body)});
  }
 }
 if(native) {GdkRectangle frame;gdk_window_get_frame_extents(native,&frame);if(frame.width>1 && frame.height>1) rect=(InsightWindowRect){frame.x,frame.y,frame.width,frame.height};}return rect;
}
static InsightWindowRect palette_widget_rect(GtkWidget *widget,InsightWindowRect fallback) {
 if(!widget || !gtk_widget_get_mapped(widget) || gtk_widget_get_allocated_width(widget)<2 || gtk_widget_get_allocated_height(widget)<2) return fallback;
 GtkWidget *top=gtk_widget_get_toplevel(widget);GdkWindow *native=gtk_widget_get_window(top);gint x=0,y=0,tx=0,ty=0;
 if(!native || !gtk_widget_translate_coordinates(widget,top,0,0,&x,&y)) return fallback;
 gdk_window_get_origin(native,&tx,&ty);return (InsightWindowRect){tx+x,ty+y,gtk_widget_get_allocated_width(widget),gtk_widget_get_allocated_height(widget)};
}
static void palette_cancel_motion(void) {
 if(palette.timer) {g_source_remove(palette.timer);palette.timer=0;}
 if(palette.motions_ready) {for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) g_weak_ref_clear(&palette.motions[n].window);palette.motions_ready=FALSE;}
 g_object_set_data(G_OBJECT(main_widgets.window),"insight-independent-motion",NULL);
}
static gboolean palette_closed(GtkWidget *widget,GdkEvent *event,gpointer data) {
 (void)widget;(void)event;(void)data;insight_appearance_join_windows();return TRUE;
}
static void palette_restore_now(void) {
 palette_cancel_motion();if(!palette.active) return;palette.active=FALSE;palette.joining=FALSE;
 GtkWidget *root=ui_lookup_widget(main_widgets.window,"vbox1");
 for(guint i=0;palette.rows && i<palette.rows->len;i++) {
  PaletteRow *row=g_ptr_array_index(palette.rows,i);GtkWidget *widget=g_weak_ref_get(&row->widget);if(!widget) continue;
  GtkWidget *parent=gtk_widget_get_parent(widget);if(parent) gtk_container_remove(GTK_CONTAINER(parent),widget);
  if(row->pack==GTK_PACK_END) gtk_box_pack_end(GTK_BOX(root),widget,row->expand,row->fill,row->padding);else gtk_box_pack_start(GTK_BOX(root),widget,row->expand,row->fill,row->padding);
  gtk_box_reorder_child(GTK_BOX(root),widget,row->position);g_object_unref(widget);
 }
 if(palette.window) {gtk_widget_destroy(palette.window);palette.window=NULL;}g_clear_pointer(&palette.rows,g_ptr_array_unref);
 if(palette.bottom) {
  GtkWidget *content=palette.bottom_content;g_object_ref(content);GtkWidget *parent=gtk_widget_get_parent(content);if(parent) gtk_container_remove(GTK_CONTAINER(parent),content);
  gtk_paned_set_position(GTK_PANED(palette.bottom_parent),palette.before.values[IL_CONTEXT_SPLIT]);gtk_paned_pack2(GTK_PANED(palette.bottom_parent),content,palette.bottom_resize,palette.bottom_shrink);g_object_unref(content);gtk_widget_destroy(palette.bottom);palette.bottom=NULL;palette.bottom_content=NULL;g_clear_object(&palette.bottom_parent);
 }
 for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) {GtkWidget *window=palette_part_window(n);if(window) palette_compact_title(window,FALSE);if(window && window!=main_widgets.window) gtk_window_set_transient_for(GTK_WINDOW(window),GTK_WINDOW(main_widgets.window));if(window) g_object_set_data(G_OBJECT(window),"insight-independent-opacity",NULL);}
 insight_appearance_opacity_tick(TRUE);apply_layout_record(&palette.before);gtk_widget_set_visible(main_widgets.message_window_notebook,palette.message_visible);
 /* Restore the global alpha after the main window loses its temporary tag. */
 insight_appearance_panel_opacity(main_widgets.window,100);
}
static void palette_move_frame(GtkWidget *window,PaletteMotion *motion,InsightWindowRect rect) {
 if(rect.width!=motion->last.width || rect.height!=motion->last.height) gtk_window_resize(GTK_WINDOW(window),MAX(1,rect.width-motion->chrome_width),MAX(1,rect.height-motion->chrome_height));
 if(rect.x!=motion->last.x || rect.y!=motion->last.y) gtk_window_move(GTK_WINDOW(window),rect.x,rect.y);motion->last=rect;
}
static void palette_prepare_motion(void) {
 InsightWindowRect from[INSIGHT_FLOAT_COUNT],to[INSIGHT_FLOAT_COUNT],sizes[INSIGHT_FLOAT_COUNT];
 for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) {
  GtkWidget *window=palette_part_window(n);PaletteMotion *motion=&palette.motions[n];g_weak_ref_init(&motion->window,window);
  from[n]=palette_frame(window);sizes[n]=from[n];gint w=from[n].width,h=from[n].height;
  if(GTK_IS_WINDOW(window)) {gtk_window_get_size(GTK_WINDOW(window),&w,&h);}
  motion->chrome_width=from[n].width-w;motion->chrome_height=from[n].height-h;motion->last=from[n];
 }
 if(palette.joining) {
  for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) {
   to[n]=palette.origins[n];GtkWidget *content=palette_part_content(n);
   if(n!=INSIGHT_FLOAT_EDITOR && content) {InsightWindowRect actual=palette_widget_rect(content,from[n]);to[n].x-=actual.x-from[n].x;to[n].y-=actual.y-from[n].y;to[n].width+=from[n].width-actual.width;to[n].height+=from[n].height-actual.height;}
   to[n]=insight_window_rect_clamp(to[n],palette.area);
  }
 } else {
  insight_window_edge_layout(palette.area,sizes,to);
  for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) palette.destinations[n]=to[n];
 }
 /* Same vector speed, with the longest path completing in 0.8 seconds. */
 InsightWindowMotion planned[INSIGHT_FLOAT_COUNT];insight_window_motion_plan(planned,from,to,INSIGHT_FLOAT_COUNT,.8,1);
 gdouble longest=0;for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) longest=MAX(longest,planned[n].distance);
 palette.speed=MAX(palette.speed,longest/.8);
 palette.speed=insight_window_motion_plan(planned,from,to,INSIGHT_FLOAT_COUNT,.8,palette.speed);
 for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) palette.motions[n].motion=planned[n];palette.motions_ready=TRUE;palette.started=g_get_monotonic_time();
}
static gboolean palette_motion_step(gpointer data) {
 (void)data;if(!wb.alive || !palette.active) {palette.timer=0;return G_SOURCE_REMOVE;}
 if(!palette.motions_ready) {
  insight_appearance_opacity_tick(FALSE);
  if(g_get_monotonic_time()<palette.deadline) return G_SOURCE_CONTINUE;
  if(!palette.joining && gtk_window_is_maximized(GTK_WINDOW(main_widgets.window)) && palette.settle_ticks++<40) return G_SOURCE_CONTINUE;
  palette_prepare_motion();
 }
 gdouble elapsed=(g_get_monotonic_time()-palette.started)/1000000.0;gboolean done=TRUE;
 for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) {
  PaletteMotion *motion=&palette.motions[n];GtkWidget *window=g_weak_ref_get(&motion->window);if(!window) continue;
  InsightWindowRect rect;gboolean arrived=insight_window_motion_sample_eased(&motion->motion,palette.animate ? elapsed:1000000,&rect);if(!arrived) done=FALSE;palette_move_frame(window,motion,rect);g_object_unref(window);
 }
 if(insight_appearance_opacity_tick(!palette.animate)) done=FALSE;
 if(!done) return G_SOURCE_CONTINUE;
 /* A move request is asynchronous. Re-read decorated frame bounds rather than
  * treating the last requested position as the window manager's final result.
  * Recompute anchors from actual sizes (CSD and theme minima can change them). */
 if(!palette.joining && palette.arrival_ticks++<12) {
  InsightWindowRect measured[INSIGHT_FLOAT_COUNT],targets[INSIGHT_FLOAT_COUNT];
  for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) measured[n]=palette_frame(palette_part_window(n));
  insight_window_edge_layout(palette.area,measured,targets);gboolean pending=FALSE;
  for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) {
   GtkWidget *window=palette_part_window(n);if(!GTK_IS_WINDOW(window)) continue;
   gint dx=targets[n].x-measured[n].x,dy=targets[n].y-measured[n].y;
   if(ABS(dx)>1 || ABS(dy)>1) {gtk_window_move(GTK_WINDOW(window),targets[n].x,targets[n].y);pending=TRUE;}
  }
  if(pending || palette.arrival_ticks<3) return G_SOURCE_CONTINUE;
 }
 palette.timer=0;gboolean joining=palette.joining;palette_cancel_motion();if(joining) palette_restore_now();return G_SOURCE_REMOVE;
}
static void palette_queue_motion(gboolean joining,gboolean animate) {
 palette_views_capture();palette_cancel_motion();palette.joining=joining;palette.animate=animate;palette.settle_ticks=0;palette.arrival_ticks=0;palette.deadline=g_get_monotonic_time()+32000;
 g_object_set_data(G_OBJECT(main_widgets.window),"insight-independent-motion",GINT_TO_POINTER(1));palette.timer=g_timeout_add(16,palette_motion_step,NULL);
}
void insight_workbench_set_independent(gboolean active) {
 if(!wb.alive || wb.host_count!=10) return;
 for(guint n=0;n<wb.host_count;n++) panel_controls_update(&wb.hosts[n]);
 if(palette.active) {
  if(active) {if(palette.joining) palette_queue_motion(FALSE,TRUE);return;}
  if(!palette.joining) palette_queue_motion(TRUE,TRUE);return;
 }
 if(!active) return;
 if(wb.main_apply_source) {g_source_remove(wb.main_apply_source);wb.main_apply_source=0;}
 if(!capture_layout(&palette.before)) {ui_set_statusbar(TRUE,"Cannot capture layout for independent windows");return;}
 palette_views_capture();gboolean animate=gtk_widget_get_mapped(main_widgets.window);InsightWindowRect original=palette_frame(main_widgets.window);
 GdkDisplay *display=gtk_widget_get_display(main_widgets.window);GdkWindow *native=gtk_widget_get_window(main_widgets.window);GdkMonitor *monitor=native ? gdk_display_get_monitor_at_window(display,native):NULL;
 if(!monitor && gdk_display_get_n_monitors(display)) monitor=gdk_display_get_monitor(display,0);
 GdkRectangle work={0,0,1024,768};if(monitor) gdk_monitor_get_workarea(monitor,&work);palette.area=(InsightWindowRect){work.x,work.y,work.width,work.height};original=insight_window_rect_clamp(original,palette.area);
 GtkWidget *menu_row=ui_lookup_widget(main_widgets.window,"hbox_menubar"),*book=main_widgets.message_window_notebook;
 palette.origins[0]=palette_widget_rect(menu_row,original);
 if(main_widgets.toolbar && gtk_widget_get_mapped(main_widgets.toolbar)) {InsightWindowRect tool=palette_widget_rect(main_widgets.toolbar,palette.origins[0]);gint right=MAX(tool.x+tool.width,palette.origins[0].x+palette.origins[0].width),bottom=MAX(tool.y+tool.height,palette.origins[0].y+palette.origins[0].height);palette.origins[0].x=MIN(tool.x,palette.origins[0].x);palette.origins[0].y=MIN(tool.y,palette.origins[0].y);palette.origins[0].width=right-palette.origins[0].x;palette.origins[0].height=bottom-palette.origins[0].y;}
 palette.origins[1]=palette_widget_rect(wb.hosts[8].page,palette_widget_rect(main_widgets.sidebar_notebook,original));palette.origins[2]=palette_widget_rect(wb.project_host,original);palette.origins[3]=palette_widget_rect(book,original);palette.origins[4]=original;
 palette.active=TRUE;palette.message_visible=gtk_widget_get_visible(book);palette.speed=0;palette.rows=g_ptr_array_new_with_free_func(palette_row_free);
 GtkWidget *root=ui_lookup_widget(main_widgets.window,"vbox1");palette.window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(palette.window,"insight-tools-palette");gtk_window_set_title(GTK_WINDOW(palette.window),insight_tr("Geany Insight — Menu and Tools"));gtk_window_set_transient_for(GTK_WINDOW(palette.window),GTK_WINDOW(main_widgets.window));g_object_set_data(G_OBJECT(palette.window),"GladeParentKey",main_widgets.window);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_container_add(GTK_CONTAINER(palette.window),box);GList *children=gtk_container_get_children(GTK_CONTAINER(root));gint position=0;
 for(GList *p=children;p;p=p->next,position++) {
  GtkWidget *widget=p->data;if(widget!=menu_row && widget!=main_widgets.toolbar) continue;PaletteRow *row=g_new0(PaletteRow,1);row->position=position;g_weak_ref_init(&row->widget,widget);gtk_box_query_child_packing(GTK_BOX(root),widget,&row->expand,&row->fill,&row->padding,&row->pack);g_ptr_array_add(palette.rows,row);g_object_ref(widget);gtk_container_remove(GTK_CONTAINER(root),widget);gtk_box_pack_start(GTK_BOX(box),widget,FALSE,FALSE,0);g_object_unref(widget);
 }
 g_list_free(children);GSList *accels=gtk_accel_groups_from_object(G_OBJECT(main_widgets.window));for(GSList *p=accels;p;p=p->next) gtk_window_add_accel_group(GTK_WINDOW(palette.window),p->data);
 g_signal_connect(palette.window,"delete-event",G_CALLBACK(palette_closed),NULL);insight_appearance_decorate(palette.window);gtk_widget_show(box);
 dock_all_panels();dock_project(NULL,NULL);detach_panel(8);float_project(NULL,NULL);
 /* Move the complete bottom notebook, not just the Context page: Local/SSH/TTY,
  * messages and the selected tab keep their widget and process identities. */
 /* Move the clipping host too: leaving it behind creates a live empty
  * splitter. Its collapse callback would otherwise hide the detached notebook. */
 GtkWidget *split=ui_lookup_widget(main_widgets.window,"vpaned1");palette.bottom_content=gtk_paned_get_child2(GTK_PANED(split));palette.bottom_parent=g_object_ref(split);
 gtk_container_child_get(GTK_CONTAINER(split),palette.bottom_content,"resize",&palette.bottom_resize,"shrink",&palette.bottom_shrink,NULL);
 palette.bottom=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(palette.bottom,"insight-bottom-palette");gtk_window_set_title(GTK_WINDOW(palette.bottom),insight_tr("Geany Insight — Context and Terminals"));gtk_window_set_transient_for(GTK_WINDOW(palette.bottom),GTK_WINDOW(main_widgets.window));g_object_set_data(G_OBJECT(palette.bottom),"GladeParentKey",main_widgets.window);g_object_ref(palette.bottom_content);gtk_container_remove(GTK_CONTAINER(palette.bottom_parent),palette.bottom_content);gtk_container_add(GTK_CONTAINER(palette.bottom),palette.bottom_content);g_object_unref(palette.bottom_content);g_signal_connect(palette.bottom,"delete-event",G_CALLBACK(palette_closed),NULL);insight_appearance_decorate(palette.bottom);gtk_widget_show(book);
 ui_prefs.sidebar_visible=FALSE;ui_sidebar_show_hide();gtk_window_unmaximize(GTK_WINDOW(main_widgets.window));
 InsightWindowRect targets[INSIGHT_FLOAT_COUNT];insight_window_edge_layout(palette.area,NULL,targets);
 for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) {
  GtkWidget *window=palette_part_window(n);if(!window) continue;palette_compact_title(window,TRUE);if(window!=main_widgets.window) gtk_window_set_transient_for(GTK_WINDOW(window),NULL);gtk_window_set_gravity(GTK_WINDOW(window),GDK_GRAVITY_NORTH_WEST);gtk_window_set_position(GTK_WINDOW(window),GTK_WIN_POS_NONE);gtk_window_unmaximize(GTK_WINDOW(window));g_object_set_data(G_OBJECT(window),"insight-independent-opacity",GINT_TO_POINTER(1));gint wanted_h=targets[n].height;
  if(n==INSIGHT_FLOAT_TOOLS) {gint minimum,natural;gtk_widget_get_preferred_height(window,&minimum,&natural);wanted_h=MAX(1,natural);}
  gtk_window_set_default_size(GTK_WINDOW(window),targets[n].width,wanted_h);gtk_window_resize(GTK_WINDOW(window),targets[n].width,wanted_h);
  InsightWindowRect start=insight_window_rect_clamp(palette.origins[n],palette.area);gtk_window_move(GTK_WINDOW(window),start.x,start.y);gtk_widget_show(window);insight_appearance_panel_opacity(window,100);
 }
 palette_queue_motion(FALSE,animate);
}
void insight_workbench_tools_changed(void) {
 if(!palette.active || !palette.window || !main_widgets.toolbar) return;
 GtkWidget *root=ui_lookup_widget(main_widgets.window,"vbox1"),*widget=main_widgets.toolbar;
 if(gtk_widget_get_parent(widget)!=root) return;
 PaletteRow *row=g_new0(PaletteRow,1);g_weak_ref_init(&row->widget,widget);
 GList *children=gtk_container_get_children(GTK_CONTAINER(root));row->position=g_list_index(children,widget);g_list_free(children);
 gtk_box_query_child_packing(GTK_BOX(root),widget,&row->expand,&row->fill,&row->padding,&row->pack);g_ptr_array_add(palette.rows,row);
 g_object_ref(widget);gtk_container_remove(GTK_CONTAINER(root),widget);gtk_box_pack_start(GTK_BOX(gtk_bin_get_child(GTK_BIN(palette.window))),widget,FALSE,FALSE,0);g_object_unref(widget);
}
static void apply_layout(GtkMenuItem *item,gpointer data) {
 (void)item;gint layout=GPOINTER_TO_INT(data);if(layout<0 || layout>=4) return;
 if(insight_layout_valid(&wb.layouts[layout])) { apply_layout_record(&wb.layouts[layout]);return; }
 wb.initial_right_split=-1;dock_all_panels();dock_project(NULL,NULL);
 set_panel_visibility(layout!=1,layout!=1,layout!=1);
 gtk_paned_set_position(GTK_PANED(wb.pane),layout==2 ? 330 : 500); gtk_paned_set_position(GTK_PANED(wb.right_pane),layout==2 ? 650 : 850);
 gtk_paned_set_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"hpaned1")),layout==3 ? 360 : 240);
 if(layout==3) float_project(NULL,NULL);
}
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
static void panel_menu(GtkWidget *view_menu) {
 GtkWidget *item=gtk_menu_item_new_with_label("Panels"),*menu=gtk_menu_new(); gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),menu); gtk_container_add(GTK_CONTAINER(view_menu),item); wb.panel_commands=menu;
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
static void git_project_refresh(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(app->project) native_project_update(NULL,NULL,GINT_TO_POINTER(1));
}
static void project_synchronize(GtkMenuItem *item,gpointer data) {
 (void)item;if(!wb.root) { ui_set_statusbar(TRUE,"Open a project first");return; }wb.force_parse=GPOINTER_TO_INT(data)!=0;wb.cache_validation=FALSE;start_scan(wb.root);
}
static void create_workbench_menus(void) {
 insight_git_install(G_CALLBACK(git_project_refresh));
 GtkWidget *overview=gtk_check_menu_item_new_with_label("Overview"); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overview),insight_overview_get_visible()); g_signal_connect(overview,"toggled",G_CALLBACK(overview_visible),NULL); gtk_container_add(GTK_CONTAINER(ui_lookup_widget(main_widgets.window,"menu_view1_menu")),overview); gtk_widget_show(overview);
 GtkWidget *project_menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"menu_project1"))),*manage=gtk_menu_item_new_with_label("Add and Remove Project Files…"); gtk_container_add(GTK_CONTAINER(project_menu),manage); g_signal_connect(manage,"activate",G_CALLBACK(manage_project_files),NULL); gtk_widget_show(manage);
 const gchar *master_labels[]={"Synchronize Master File List","Open Project Master File List"};for(guint i=0;i<2;i++) { GtkWidget *command=gtk_menu_item_new_with_label(master_labels[i]);gtk_widget_set_name(command,i ? "insight-open-master-list" : "insight-sync-master-list");gtk_menu_shell_append(GTK_MENU_SHELL(project_menu),command);g_signal_connect(command,"activate",G_CALLBACK(project_master_command),GINT_TO_POINTER(i));gtk_widget_show(command); }
 const gchar *sync_labels[]={"Synchronize Files","Rebuild Project"};for(guint n=0;n<2;n++) { GtkWidget *command=gtk_menu_item_new_with_label(sync_labels[n]);gtk_widget_set_name(command,n?"insight-project-rebuild":"insight-project-synchronize");g_signal_connect(command,"activate",G_CALLBACK(project_synchronize),GINT_TO_POINTER(n));gtk_menu_shell_append(GTK_MENU_SHELL(project_menu),command);gtk_widget_show(command); }
 insight_project_report_init(project_menu,capture_project_report);
 GtkWidget *menubar=ui_lookup_widget(main_widgets.window,"menubar1"),*navigate=gtk_menu_item_new_with_mnemonic("_Navigate"),*menu=gtk_menu_new();
 gtk_menu_item_set_submenu(GTK_MENU_ITEM(navigate),menu); gtk_menu_shell_insert(GTK_MENU_SHELL(menubar),navigate,4);
 g_object_set_data(G_OBJECT(main_widgets.window),"insight-navigate",navigate);
 window_lock_item(menu,wb.follow,"Lock Context Window","insight-context-lock-command");
 window_lock_item(menu,wb.relation_follow,"Lock Relation Window","insight-relation-lock-command");
 GtkWidget *refresh_relation=gtk_menu_item_new_with_label("Refresh Relation Window");gtk_widget_set_name(refresh_relation,"insight-relation-refresh-command");gtk_menu_shell_append(GTK_MENU_SHELL(menu),refresh_relation);g_signal_connect(refresh_relation,"activate",G_CALLBACK(relation_refresh_command),NULL);
 const gchar *view_labels[]={"Relation List / Tree View","Horizontal Relation Graph","Vertical Relation Graph"};
 const gchar *view_names[]={"insight-relation-list-command","insight-relation-horizontal-command","insight-relation-vertical-command"};
 for(guint i=0;i<G_N_ELEMENTS(view_labels);i++) { GtkWidget *item=gtk_menu_item_new_with_label(view_labels[i]);gtk_widget_set_name(item,view_names[i]);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(relation_select_view),GUINT_TO_POINTER(i)); }
 GtkWidget *print_relation=gtk_menu_item_new_with_label("Print Relation Window…");gtk_widget_set_name(print_relation,"insight-relation-print-command");gtk_menu_shell_append(GTK_MENU_SHELL(menu),print_relation);g_signal_connect(print_relation,"activate",G_CALLBACK(relation_print_show),NULL);
 GtkWidget *next_view_command=gtk_menu_item_new_with_label("Next Relation Window View");gtk_widget_set_name(next_view_command,"insight-relation-next-view-command");gtk_menu_shell_append(GTK_MENU_SHELL(menu),next_view_command);g_signal_connect(next_view_command,"activate",G_CALLBACK(relation_next_view),NULL);
 const gchar *relation_reports[]={"View Relation Outline…","Create Bookmarks from Relation Items…"};for(guint i=0;i<2;i++) { GtkWidget *item=gtk_menu_item_new_with_label(relation_reports[i]);gtk_widget_set_name(item,i ? "insight-relation-bookmark-command":"insight-relation-outline-command");gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(relation_report_show),GUINT_TO_POINTER(i)); }

 GtkWidget *diff_item=gtk_menu_item_new_with_label("Diff / Merge…");gtk_widget_set_name(diff_item,"insight-diff-menu");gtk_container_add(GTK_CONTAINER(main_widgets.tools_menu),diff_item);g_signal_connect(diff_item,"activate",G_CALLBACK(show_diff),NULL);gtk_widget_show(diff_item);
 GtkWidget *hex_item=gtk_menu_item_new_with_label("Hex editor…");gtk_widget_set_name(hex_item,"insight-hex-menu");gtk_container_add(GTK_CONTAINER(main_widgets.tools_menu),hex_item);g_signal_connect(hex_item,"activate",G_CALLBACK(show_hex),NULL);gtk_widget_show(hex_item);
 GtkWidget *directory_item=gtk_menu_item_new_with_label("Directory Compare…");gtk_widget_set_name(directory_item,"insight-directory-menu");gtk_container_add(GTK_CONTAINER(main_widgets.tools_menu),directory_item);g_signal_connect(directory_item,"activate",G_CALLBACK(show_directory_compare),NULL);gtk_widget_show(directory_item);
 wb.navigation_accels=gtk_accel_group_new(); gtk_window_add_accel_group(GTK_WINDOW(main_widgets.window),wb.navigation_accels);
 GtkWidget *templates=gtk_image_menu_item_new_with_label("Code templates / Clips…"); gtk_widget_set_name(templates,"insight-templates-command"); gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(templates),insight_command_icon("snippets")); gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(templates),TRUE); gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.tools_menu),templates); g_signal_connect(templates,"activate",G_CALLBACK(focus_templates),NULL); gtk_widget_show(templates);
 create_structural_selection_menu(); insight_overview_install_commands();
 const gchar *target_labels[]={"Jump To Prototype…","Jump To Implementation…","Jump To Base Type…"},*target_ids[]={"insight-jump-prototype-command","insight-jump-implementation-command","insight-jump-base-type-command"},*target_popups[]={"insight-jump-prototype-popup","insight-jump-implementation-popup","insight-jump-base-type-popup"};for(guint i=0;i<3;i++) { GtkWidget *item=gtk_menu_item_new_with_label(target_labels[i]);gtk_widget_set_name(item,target_ids[i]);keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_JUMPPROTOTYPE+i,item);g_signal_connect(item,"activate",G_CALLBACK(show_function_target),GUINT_TO_POINTER(i+1));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);GtkWidget *popup=gtk_menu_item_new_with_label(target_labels[i]);gtk_widget_set_name(popup,target_popups[i]);g_signal_connect(popup,"activate",G_CALLBACK(show_function_target),GUINT_TO_POINTER(i+1));gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.editor_menu),popup); }
 GtkWidget *info=gtk_menu_item_new_with_label("Symbol Info…");gtk_widget_set_name(info,"insight-symbol-info-command");keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_SYMBOLINFO,info);g_signal_connect(info,"activate",G_CALLBACK(show_symbol_info),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),info);
 GtkWidget *lookup_options=gtk_menu_item_new_with_label("Symbol Info Lookup Options…");gtk_widget_set_name(lookup_options,"insight-symbol-info-options-command");g_signal_connect(lookup_options,"activate",G_CALLBACK(show_symbol_lookup_options),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),lookup_options);
 GtkWidget *search_menu=ui_lookup_widget(main_widgets.window,"search1_menu");
 const char *names[]={"Go to definition","Find symbol…","Find file…","Find usages…","Context back","Context forward"};
 const char *shortcuts[]={"F12","<Primary><Alt>s","<Primary><Alt>o","<Primary><Alt>r","<Primary><Alt>Left","<Primary><Alt>Right"};
 for(int i=0;i<6;i++) {
  GtkWidget *entry=gtk_menu_item_new_with_label(names[i]); gtk_container_add(GTK_CONTAINER(i>=1 && i<=3 ? search_menu : menu),entry);gtk_widget_show(entry);
  if(!i) g_signal_connect(entry,"activate",G_CALLBACK(go_definition),NULL);
  else if(i<4) g_signal_connect(entry,"activate",G_CALLBACK(focus_browser),GINT_TO_POINTER(i-1));
  else g_signal_connect(entry,"activate",G_CALLBACK(history_move),GINT_TO_POINTER(i==4 ? -1 : 1));
  guint key; GdkModifierType mods; gtk_accelerator_parse(shortcuts[i],&key,&mods); gtk_widget_add_accelerator(entry,"activate",wb.navigation_accels,key,mods,GTK_ACCEL_VISIBLE);
 }
 const char *function_names[]={"Function Up","Function Down"};
 const char *function_keys[]={"<Primary><Alt>Page_Up","<Primary><Alt>Page_Down"};
 for(guint i=0;i<2;i++) {
  GtkWidget *entry=gtk_menu_item_new_with_label(function_names[i]);gtk_menu_shell_append(GTK_MENU_SHELL(menu),entry);
  g_signal_connect(entry,"activate",G_CALLBACK(function_navigation),GINT_TO_POINTER(i ? 1 : -1));
  guint key;GdkModifierType mods;gtk_accelerator_parse(function_keys[i],&key,&mods);gtk_widget_add_accelerator(entry,"activate",wb.navigation_accels,key,mods,GTK_ACCEL_VISIBLE);
 }
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
 GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(wb.choices)); for(GList *p=cells;p;p=p->next) g_object_set(p->data,"ellipsize",PANGO_ELLIPSIZE_MIDDLE,"max-width-chars",24,"width-chars",24,NULL); g_list_free(cells);
 gtk_tree_view_column_set_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(wb.symbol_view),SYMBOL_KIND),FALSE); gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.symbol_view),SYMBOL_PATH);
 gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.file_view),1);
 wb.css=gtk_css_provider_new(); gtk_css_provider_load_from_data(wb.css,"#insight-context { border-top: 2px solid @theme_selected_bg_color; } #insight-context-title { font-weight: bold; padding: 3px; } #insight-workspace { padding: 3px; }",-1,NULL);
 gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(wb.css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
 gtk_widget_hide(ui_lookup_widget(main_widgets.window,"menu_build1"));
 if(!g_file_test(wb.prefs_path,G_FILE_TEST_EXISTS)) {
  ui_prefs.sidebar_visible=TRUE; ui_sidebar_show_hide(); gtk_paned_set_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"hpaned1")),100);
  gtk_notebook_set_current_page(GTK_NOTEBOOK(wb.project_notebook),0);
 }
 ui_set_window_title(document_get_current());
}
static void save_workbench_settings(void) {
 if(!wb.prefs_path || !wb.pane) return;
 GKeyFile *settings=g_key_file_new();
 insight_overview_write(settings);insight_directory_write(settings);insight_diff_write(settings);
 grouped_write(&wb.type_panel,settings); grouped_write(&wb.category_panel,settings);
 g_key_file_set_boolean(settings,"workbench","project-templates",wb.clips_prefer_project);
 g_key_file_set_boolean(settings,"workbench","folder-follow-current-file",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.tree_follow)));
 insight_list_style_write(wb.tree_view,settings,"project-folders-style");
 insight_list_style_write(wb.marks_view,settings,"bookmarks-style");
 insight_disk_browser_write(wb.disk_browser,settings);g_key_file_set_integer(settings,"workbench","folder-view-mode",gtk_combo_box_get_active(GTK_COMBO_BOX(wb.folder_mode)));
 symbol_sort_write(settings,wb.file_store,"project-files");insight_symbol_view_columns_write(GTK_TREE_VIEW(wb.file_view),settings,"project-files");symbol_history_write(settings,wb.file_entry,"project-files");
 g_key_file_set_boolean(settings,"workbench","symbol-locals-opt-in-v1",TRUE);g_key_file_set_boolean(settings,"workbench","project-relative-path-v1",TRUE);
 symbol_history_write(settings,wb.local_entry,"file-symbols");symbol_history_write(settings,wb.symbol_entry,"project-symbols");
 insight_symbol_view_columns_write(GTK_TREE_VIEW(wb.local_view),settings,"file-symbols");insight_symbol_view_columns_write(GTK_TREE_VIEW(wb.symbol_view),settings,"project-symbols");
 symbol_sort_write(settings,wb.local_symbols,"file-symbols");symbol_sort_write(settings,wb.symbols,"project-symbols");
 for(guint i=0;i<4;i++) { gchar group[16]; g_snprintf(group,sizeof group,"layout-%c",'A'+i); insight_layout_write(settings,group,&wb.layouts[i]); }
 g_key_file_set_boolean(settings,"workbench","transparent-panels",wb.transparent_panels);g_key_file_set_boolean(settings,"workbench","tab-tray-visible",wb.tray_enabled);
 g_key_file_set_integer(settings,"workbench","version",1);
 for(guint i=0;i<wb.host_count;i++) { PanelHost *host=&wb.hosts[i]; gchar group[32]; g_snprintf(group,sizeof group,"panel-%u",i); if(host->window) { panel_geometry_capture(host); host->visible=gtk_widget_get_visible(host->window); }
  InsightLayout grouping={0};panel_group_capture(i,&grouping);g_key_file_set_integer(settings,group,"group",grouping.groups[i]);g_key_file_set_integer(settings,group,"group-order",grouping.group_order[i]);g_key_file_set_integer(settings,group,"group-active",grouping.group_active[i]);
  g_key_file_set_integer(settings,group,"version",5); g_key_file_set_integer(settings,group,"opacity",host->opacity); g_key_file_set_boolean(settings,group,"minimized",host->minimized); g_key_file_set_boolean(settings,group,"maximized",host->maximized); g_key_file_set_boolean(settings,group,"floating",host->window!=NULL); g_key_file_set_boolean(settings,group,"visible",host->visible); g_key_file_set_integer(settings,group,"x",host->x); g_key_file_set_integer(settings,group,"y",host->y); g_key_file_set_integer(settings,group,"width",host->width); g_key_file_set_integer(settings,group,"height",host->height);
 }
 g_key_file_set_boolean(settings,"workbench","project-visible",gtk_widget_get_visible(wb.project_notebook));
 g_key_file_set_boolean(settings,"workbench","floating",wb.float_window!=NULL);
 if(wb.float_window) { gtk_window_get_position(GTK_WINDOW(wb.float_window),&wb.float_x,&wb.float_y); gtk_window_get_size(GTK_WINDOW(wb.float_window),&wb.float_width,&wb.float_height); }
 g_key_file_set_integer(settings,"workbench","float-x",wb.float_x); g_key_file_set_integer(settings,"workbench","float-y",wb.float_y);
 g_key_file_set_integer(settings,"workbench","float-width",wb.float_width); g_key_file_set_integer(settings,"workbench","float-height",wb.float_height);
 g_key_file_set_integer(settings,"workbench","split",gtk_paned_get_position(GTK_PANED(wb.pane)));
 g_key_file_set_boolean(settings,"workbench","follow",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow)));
 g_key_file_set_boolean(settings,"workbench","visible",wb.context_visible);
 g_key_file_set_boolean(settings,"workbench","file-symbols-scopes",insight_symbol_view_scopes(GTK_TREE_VIEW(wb.local_view)));g_key_file_set_boolean(settings,"workbench","project-symbols-scopes",insight_symbol_view_scopes(GTK_TREE_VIEW(wb.symbol_view)));
 g_key_file_set_boolean(settings,"workbench","file-symbols-grouped",insight_symbol_view_grouped(GTK_TREE_VIEW(wb.local_view))); g_key_file_set_boolean(settings,"workbench","project-symbols-grouped",insight_symbol_view_grouped(GTK_TREE_VIEW(wb.symbol_view)));
 g_key_file_set_integer(settings,"workbench","file-symbol-types",insight_symbol_view_types_mask(GTK_TREE_VIEW(wb.local_view))); g_key_file_set_integer(settings,"workbench","project-symbol-types",insight_symbol_view_types_mask(GTK_TREE_VIEW(wb.symbol_view)));
 g_key_file_set_integer(settings,"workbench","info-source-scope",wb.info_scope);
 g_key_file_set_boolean(settings,"workbench","info-parse-local",wb.info_parse_local);
 g_key_file_set_boolean(settings,"workbench","info-across-languages",wb.info_across_languages);
 g_key_file_set_integer(settings,"workbench","right-split",gtk_paned_get_position(GTK_PANED(wb.right_pane)));
 g_key_file_set_integer(settings,"workbench","project-tab",project_tab_identity());
 g_key_file_set_integer(settings,"workbench","project-search-flags",text_search_flags(wb.project_case,wb.project_word,wb.project_regex));
 g_key_file_set_integer(settings,"workbench","editor-search-flags",text_search_flags(wb.editor_case,wb.editor_word,wb.editor_regex));
 g_key_file_set_boolean(settings,"workbench","relation-transitive",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_transitive)));
 g_key_file_set_integer(settings,"workbench","relation-mode",gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)));
 gboolean graph_vertical=FALSE;gint graph_zoom=100;insight_relation_graph_get_options(!strcmp(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)),"treegraph") ? wb.relation_tree_graph : wb.relation_graph,&graph_vertical,&graph_zoom);
 g_key_file_set_boolean(settings,"workbench","graph-vertical",graph_vertical);g_key_file_set_integer(settings,"workbench","graph-zoom",graph_zoom);
 g_key_file_set_boolean(settings,"workbench","relation-tree",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle)));
 g_key_file_set_boolean(settings,"workbench","relation-graph",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle)));
 g_key_file_set_boolean(settings,"workbench","relation-enclosing",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_enclosing)));
 g_key_file_set_boolean(settings,"workbench","relation-follow",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_follow)));
 g_key_file_set_boolean(settings,"workbench","confirm-bookmark-delete",wb.marks_confirm_delete);
 g_key_file_set_boolean(settings,"workbench","delete-bookmarks-on-close",wb.marks_delete_on_close);
 g_key_file_set_boolean(settings,"workbench","preserve-bookmarks",wb.marks_preserve);
 g_key_file_set_integer(settings,"workbench","references-comment-mode",gtk_combo_box_get_active(GTK_COMBO_BOX(wb.usage_comments)));
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
 /* Directory defaults can include two long UTF-8 paths and masks. */
 if(strlen(text)<=262144 && g_key_file_load_from_data(settings,text,-1,G_KEY_FILE_NONE,NULL) && g_key_file_get_integer(settings,"workbench","version",NULL)==1) {
  if(g_key_file_has_key(settings,"workbench","info-parse-local",NULL)) { GError *option_error=NULL;gboolean enabled=g_key_file_get_boolean(settings,"workbench","info-parse-local",&option_error);if(!option_error) wb.info_parse_local=enabled;g_clear_error(&option_error); }
  wb.info_across_languages=g_key_file_get_boolean(settings,"workbench","info-across-languages",NULL);
  gint info_scope=g_key_file_get_integer(settings,"workbench","info-source-scope",NULL);wb.info_scope=info_scope>=0 && info_scope<=2 ? info_scope:0;
  insight_overview_read(settings);
 grouped_read(&wb.type_panel,settings); grouped_read(&wb.category_panel,settings);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.tree_follow),g_key_file_get_boolean(settings,"workbench","folder-follow-current-file",NULL));
 insight_directory_read(settings);insight_diff_read(settings);
 insight_list_style_read(wb.tree_view,settings,"project-folders-style");
 insight_list_style_read(wb.marks_view,settings,"bookmarks-style");
 gboolean disk_saved=insight_disk_browser_read(wb.disk_browser,settings);if(disk_saved && g_key_file_get_integer(settings,"workbench","folder-view-mode",NULL)==1) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.folder_mode),1);
 symbol_sort_read(settings,wb.file_store,"project-files");insight_symbol_view_columns_read(GTK_TREE_VIEW(wb.file_view),settings,"project-files");symbol_history_read(settings,wb.file_entry,"project-files");
  symbol_history_read(settings,wb.local_entry,"file-symbols");symbol_history_read(settings,wb.symbol_entry,"project-symbols");
  insight_symbol_view_columns_read(GTK_TREE_VIEW(wb.local_view),settings,"file-symbols");insight_symbol_view_columns_read(GTK_TREE_VIEW(wb.symbol_view),settings,"project-symbols");
  if(!g_key_file_get_boolean(settings,"workbench","project-relative-path-v1",NULL)) gtk_tree_view_column_set_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(wb.symbol_view),SYMBOL_PATH),TRUE);
  symbol_sort_read(settings,wb.local_symbols,"file-symbols");symbol_sort_read(settings,wb.symbols,"project-symbols");
  if(g_key_file_has_key(settings,"workbench","file-symbol-types",NULL)) { guint mask=(guint)g_key_file_get_integer(settings,"workbench","file-symbol-types",NULL);if(!g_key_file_get_boolean(settings,"workbench","symbol-locals-opt-in-v1",NULL)) mask &= ~(1u<<7);insight_symbol_view_set_types(GTK_TREE_VIEW(wb.local_view),mask); }
  if(g_key_file_has_key(settings,"workbench","project-symbol-types",NULL)) { guint mask=(guint)g_key_file_get_integer(settings,"workbench","project-symbol-types",NULL);if(!g_key_file_get_boolean(settings,"workbench","symbol-locals-opt-in-v1",NULL)) mask &= ~(1u<<7);insight_symbol_view_set_types(GTK_TREE_VIEW(wb.symbol_view),mask); }
  insight_symbol_view_set_scopes(GTK_TREE_VIEW(wb.local_view),g_key_file_get_boolean(settings,"workbench","file-symbols-scopes",NULL));insight_symbol_view_set_scopes(GTK_TREE_VIEW(wb.symbol_view),g_key_file_get_boolean(settings,"workbench","project-symbols-scopes",NULL));
  insight_symbol_view_set_grouped(GTK_TREE_VIEW(wb.local_view),g_key_file_get_boolean(settings,"workbench","file-symbols-grouped",NULL)); insight_symbol_view_set_grouped(GTK_TREE_VIEW(wb.symbol_view),g_key_file_get_boolean(settings,"workbench","project-symbols-grouped",NULL));
  for(guint i=0;i<4;i++) { gchar group[16]; g_snprintf(group,sizeof group,"layout-%c",'A'+i); insight_layout_read(settings,group,&wb.layouts[i]); }
  for(guint i=0;i<10;i++) { PanelHost *host=&wb.hosts[i]; gchar group[32]; g_snprintf(group,sizeof group,"panel-%u",i); GError *error=NULL; gint version=g_key_file_get_integer(settings,group,"version",&error);
   if(!error && (version>=1 && version<=5)) { if(version>=5) { host->saved_group=g_key_file_get_integer(settings,group,"group",&error);if(!error) host->saved_group_order=g_key_file_get_integer(settings,group,"group-order",&error);if(!error) host->saved_group_active=g_key_file_get_integer(settings,group,"group-active",&error); }if(!error && version>=4) host->opacity=g_key_file_get_integer(settings,group,"opacity",&error);if(!error && version>=3) host->minimized=g_key_file_get_boolean(settings,group,"minimized",&error); if(!error && version>=2) host->maximized=g_key_file_get_boolean(settings,group,"maximized",&error); if(!error) host->restore=g_key_file_get_boolean(settings,group,"floating",&error); if(!error) host->visible=g_key_file_get_boolean(settings,group,"visible",&error); if(!error) host->x=g_key_file_get_integer(settings,group,"x",&error); if(!error) host->y=g_key_file_get_integer(settings,group,"y",&error); if(!error) host->width=g_key_file_get_integer(settings,group,"width",&error); if(!error) host->height=g_key_file_get_integer(settings,group,"height",&error); }
   if(error || (version<1 || version>5) || host->opacity<40 || host->opacity>100 || host->width<300 || host->width>4000 || host->height<200 || host->height>4000 || host->x < -30000 || host->x>30000 || host->y < -30000 || host->y>30000) { host->restore=FALSE;host->opacity=100;host->maximized=FALSE;host->minimized=FALSE;host->saved_group=0;host->saved_group_order=0;host->saved_group_active=0; }
   g_clear_error(&error);
  }
  wb.transparent_panels=g_key_file_get_boolean(settings,"workbench","transparent-panels",NULL);
  if(g_key_file_has_key(settings,"workbench","tab-tray-visible",NULL)) wb.tray_enabled=g_key_file_get_boolean(settings,"workbench","tab-tray-visible",NULL);
  wb.clips_prefer_project=g_key_file_get_boolean(settings,"workbench","project-templates",NULL);clips_project_load(app->project ? app->project->file_name : NULL,TRUE);
  wb.restore_float=g_key_file_get_boolean(settings,"workbench","floating",NULL);
  wb.float_x=CLAMP(g_key_file_get_integer(settings,"workbench","float-x",NULL),-30000,30000); wb.float_y=CLAMP(g_key_file_get_integer(settings,"workbench","float-y",NULL),-30000,30000);
  gint fw=g_key_file_get_integer(settings,"workbench","float-width",NULL),fh=g_key_file_get_integer(settings,"workbench","float-height",NULL);
  wb.float_width=fw>=300 ? MIN(fw,4000) : 0; wb.float_height=fh>=200 ? MIN(fh,4000) : 0;
  if(g_key_file_has_key(settings,"workbench","project-visible",NULL)) gtk_widget_set_visible(wb.project_notebook,g_key_file_get_boolean(settings,"workbench","project-visible",NULL));
  gint split=g_key_file_get_integer(settings,"workbench","split",NULL); if(split>=150 && split<=10000) gtk_paned_set_position(GTK_PANED(wb.pane),split);
  if(g_key_file_has_key(settings,"workbench","follow",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),g_key_file_get_boolean(settings,"workbench","follow",NULL));
  if(g_key_file_has_key(settings,"workbench","visible",NULL)) wb.context_visible=g_key_file_get_boolean(settings,"workbench","visible",NULL);
  gint right=g_key_file_get_integer(settings,"workbench","right-split",NULL); if(g_key_file_has_key(settings,"workbench","right-split",NULL) && right>=0 && right<=10000) { wb.initial_right_split=right;gtk_paned_set_position(GTK_PANED(wb.right_pane),right); }
  /* Search controls are opened explicitly; old persisted visibility is ignored. */
  gint project_flags=g_key_file_get_integer(settings,"workbench","project-search-flags",NULL),editor_flags=g_key_file_get_integer(settings,"workbench","editor-search-flags",NULL);
  if(project_flags>=0 && project_flags<=7) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_case),project_flags&INSIGHT_SEARCH_CASE); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_word),project_flags&INSIGHT_SEARCH_WORD); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_regex),project_flags&INSIGHT_SEARCH_REGEX); }
  if(editor_flags>=0 && editor_flags<=7) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_case),editor_flags&INSIGHT_SEARCH_CASE); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_word),editor_flags&INSIGHT_SEARCH_WORD); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_regex),editor_flags&INSIGHT_SEARCH_REGEX); }
  if(g_key_file_has_key(settings,"workbench","relation-transitive",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_transitive),g_key_file_get_boolean(settings,"workbench","relation-transitive",NULL));
  gint relation=g_key_file_get_integer(settings,"workbench","relation-mode",NULL); if(relation>=0 && relation<8) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),relation);
  insight_relation_graph_set_options(wb.relation_graph,g_key_file_get_boolean(settings,"workbench","graph-vertical",NULL),g_key_file_has_key(settings,"workbench","graph-zoom",NULL) ? g_key_file_get_integer(settings,"workbench","graph-zoom",NULL) : 100);
  insight_relation_graph_set_options(wb.relation_tree_graph,g_key_file_get_boolean(settings,"workbench","graph-vertical",NULL),g_key_file_has_key(settings,"workbench","graph-zoom",NULL) ? g_key_file_get_integer(settings,"workbench","graph-zoom",NULL) : 100);
  if(g_key_file_has_key(settings,"workbench","relation-tree",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle),g_key_file_get_boolean(settings,"workbench","relation-tree",NULL));
  if(g_key_file_has_key(settings,"workbench","relation-graph",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle),g_key_file_get_boolean(settings,"workbench","relation-graph",NULL));
  if(g_key_file_has_key(settings,"workbench","relation-enclosing",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_enclosing),g_key_file_get_boolean(settings,"workbench","relation-enclosing",NULL));
  if(g_key_file_has_key(settings,"workbench","relation-follow",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),g_key_file_get_boolean(settings,"workbench","relation-follow",NULL));
  if(g_key_file_has_key(settings,"workbench","references-code-only",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_code),g_key_file_get_boolean(settings,"workbench","references-code-only",NULL));
  wb.marks_confirm_delete=g_key_file_get_boolean(settings,"workbench","confirm-bookmark-delete",NULL);
  wb.marks_delete_on_close=g_key_file_get_boolean(settings,"workbench","delete-bookmarks-on-close",NULL);
  if(g_key_file_has_key(settings,"workbench","preserve-bookmarks",NULL)) { GError *error=NULL;gboolean preserve=g_key_file_get_boolean(settings,"workbench","preserve-bookmarks",&error);if(!error) wb.marks_preserve=preserve;g_clear_error(&error); }
  gint comments=g_key_file_get_integer(settings,"workbench","references-comment-mode",NULL);if(comments>=0 && comments<=2 && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_code))) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.usage_comments),comments);
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
 wb.force_parse=TRUE;if(!wb.root || !start_scan(wb.root)) ui_set_statusbar(TRUE,"Choose a readable source folder first");
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
 focus_search_results(NULL,NULL);
}
static void editor_history_record(const gchar *query) {
 if(!wb.editor_history_combo || !query || !*query) return;
 gchar *saved=g_strdup(query);GtkTreeModel *model=gtk_combo_box_get_model(GTK_COMBO_BOX(wb.editor_history_combo));GtkTreeIter iter;gint position=0;wb.editor_setting=TRUE;
 for(gboolean ok=gtk_tree_model_get_iter_first(model,&iter);ok;ok=gtk_tree_model_iter_next(model,&iter),position++) { gchar *old=NULL;gtk_tree_model_get(model,&iter,0,&old,-1);gboolean same=!g_strcmp0(old,saved);g_free(old);if(same) { gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(wb.editor_history_combo),position);break; } }
 gtk_combo_box_text_prepend_text(GTK_COMBO_BOX_TEXT(wb.editor_history_combo),saved);while(gtk_tree_model_iter_n_children(model,NULL)>32) gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(wb.editor_history_combo),32);
 gtk_entry_set_text(GTK_ENTRY(wb.editor_query),saved);wb.editor_setting=FALSE;g_free(saved);
}
static void editor_text_search(GtkWidget *,gpointer);
static void editor_search_sync(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(wb.editor_setting || !wb.editor_query) return;
 g_free(search_data.text);g_free(search_data.original_text);search_data.text=g_strdup(gtk_entry_get_text(GTK_ENTRY(wb.editor_query)));search_data.original_text=g_strdup(search_data.text);
 search_data.flags=(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_case)) ? GEANY_FIND_MATCHCASE : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_word)) ? GEANY_FIND_WHOLEWORD : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_regex)) ? GEANY_FIND_REGEXP : 0);search_data.search_bar=FALSE;
 if(GTK_IS_ENTRY(widget) && wb.editor_incremental && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_incremental)) && *search_data.text) editor_text_search(NULL,GINT_TO_POINTER(2));
}
static void editor_search_all(GtkButton *button,gpointer data) {
 (void)button;editor_search_sync(NULL,NULL);GeanyDocument *doc=document_get_current();if(!doc || !search_data.text || !*search_data.text) return;
 if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_regex)) && search_data.text[0]=='~') { ui_set_statusbar(TRUE,"Find All and Mark use literal text or regular expressions; disable fuzzy matching first");return; }
 gchar *pattern=g_strdup(search_data.text),*label=g_strdup(search_data.original_text);GeanyFindFlags flags=search_data.flags;
 if(flags&GEANY_FIND_REGEXP) {
  flags|=GEANY_FIND_MULTILINE;
  if(flags&GEANY_FIND_WHOLEWORD) { gchar *bounded=g_strconcat("(?<![\\p{L}\\p{N}\\p{M}_])(?:",pattern,")(?![\\p{L}\\p{N}\\p{M}_])",NULL);g_free(pattern);pattern=bounded;flags&=~GEANY_FIND_WHOLEWORD; }
 }
 if(GPOINTER_TO_INT(data)==2) search_mark_all(doc,pattern,flags);
 else search_find_usage(pattern,label,flags,GPOINTER_TO_INT(data)==1);
 g_free(pattern);g_free(label);
}
gboolean insight_workbench_show_file_search(void) {
 if(!wb.alive || !wb.editor_query) return FALSE;
 GeanyDocument *doc=document_get_current();gchar *selection=doc ? editor_get_default_selection(doc->editor,search_prefs.use_current_word,NULL) : NULL;
 if(selection && !strchr(selection,'\n') && !strchr(selection,'\r')) { gtk_entry_set_text(GTK_ENTRY(wb.editor_query),selection); }
 g_free(selection);
 gtk_widget_show(wb.editor_search_row);gtk_widget_grab_focus(wb.editor_query);gtk_editable_select_region(GTK_EDITABLE(wb.editor_query),0,-1);return TRUE;
}
gboolean insight_workbench_file_find_again(gboolean backwards) {
 if(!wb.alive || !wb.editor_query) return FALSE;
 if(search_data.text) {
  wb.editor_setting=TRUE;gtk_entry_set_text(GTK_ENTRY(wb.editor_query),search_data.text);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_case),!!(search_data.flags&GEANY_FIND_MATCHCASE));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_word),!!(search_data.flags&GEANY_FIND_WHOLEWORD));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_regex),!!(search_data.flags&GEANY_FIND_REGEXP));wb.editor_setting=FALSE;
 }
 editor_text_search(NULL,GINT_TO_POINTER(backwards ? -1 : 1));return TRUE;
}
static void editor_text_search(GtkWidget *widget,gpointer data) {
 gboolean forward=GPOINTER_TO_INT(data)!=-1; GeanyDocument *doc=document_get_current();
 if(GTK_IS_BUTTON(widget) && doc && wb.editor_load_word && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_load_word))) { gchar *word=editor_get_default_selection(doc->editor,TRUE,NULL);if(word) { wb.editor_setting=TRUE;gtk_entry_set_text(GTK_ENTRY(wb.editor_query),word);wb.editor_setting=FALSE;g_free(word); } }
 editor_search_sync(NULL,NULL);
 if(!doc) { ui_set_statusbar(TRUE,"No active document"); return; }
 gint length=sci_get_length(doc->editor->sci);
 if(length<0) { ui_set_statusbar(TRUE,"Cannot read the active document"); return; }
 gchar *text=sci_get_contents(doc->editor->sci,length+1),*digest=g_compute_checksum_for_data(G_CHECKSUM_SHA256,(const guchar *)text,(gsize)length);
 guint flags=text_search_flags(wb.editor_case,wb.editor_word,wb.editor_regex);
 const char *query=gtk_entry_get_text(GTK_ENTRY(wb.editor_query)); gsize begin,end,a=(gsize)SSM(doc->editor->sci,SCI_GETSELECTIONSTART,0,0),b=(gsize)SSM(doc->editor->sci,SCI_GETSELECTIONEND,0,0),from=GPOINTER_TO_INT(data)==2 ? a : (forward ? b : a); gboolean wrapped=FALSE;
 gboolean repeat=wb.editor_find_id==doc->id && wb.editor_find_flags==flags && !g_strcmp0(query,wb.editor_find_query) && !g_strcmp0(digest,wb.editor_find_digest) && a==wb.editor_find_begin && b==wb.editor_find_end;
 gboolean forced_wrap=FALSE;
 if(repeat && a==b) {
  if(wb.editor_find_match_end>b) { if(forward) from=wb.editor_find_match_end; }
  else if((forward && b==(gsize)length) || (!forward && !a)) { forced_wrap=TRUE;from=forward ? 0 : length; }
  else from=(gsize)SSM(doc->editor->sci,forward ? SCI_POSITIONAFTER : SCI_POSITIONBEFORE,forward ? b : a,0);
 }
 if(forced_wrap && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_wrap))) { ui_set_statusbar(TRUE,"Find: no match before file boundary");g_free(text);g_free(digest);return; }
 GError *error=NULL;
 if(insight_context_find_file(text,(gsize)length,query,from,forward,flags,&begin,&end,&wrapped,&error)) {
  wrapped|=forced_wrap;
  if(wrapped && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_wrap))) { ui_set_statusbar(TRUE,"Find: no match before file boundary");g_clear_error(&error);g_free(text);g_free(digest);return; }
  SSM(doc->editor->sci,SCI_SETSEL,begin,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_select)) ? end : begin); decorate_match(doc->editor->sci,begin,end); SSM(doc->editor->sci,SCI_VERTICALCENTRECARET,0,0);
  g_free(wb.editor_find_query); g_free(wb.editor_find_digest); wb.editor_find_query=g_strdup(query); wb.editor_find_digest=g_strdup(digest); wb.editor_find_begin=begin;wb.editor_find_match_end=end; wb.editor_find_end=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.editor_select)) ? end : begin; wb.editor_find_id=doc->id; wb.editor_find_flags=flags;
  ui_set_statusbar(TRUE,wrapped ? "Find: wrapped" : "Find: match");editor_history_record(query); insight_workbench_refresh();
 } else ui_set_statusbar(TRUE,"Find: %s",error ? error->message : "no match");
 g_clear_error(&error); g_free(text); g_free(digest);
}
static void search_toolbar_focus(GtkMenuItem *item,gpointer data) {
 (void)item; GtkWidget *entry=GPOINTER_TO_INT(data) ? wb.editor_query : wb.project_query;
 gtk_widget_show(GPOINTER_TO_INT(data) ? wb.editor_search_row : wb.project_search_row);if(!GPOINTER_TO_INT(data)) focus_search_results(NULL,NULL);gtk_widget_grab_focus(entry);
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
 (void)widget; (void)data; gtk_widget_show(wb.search_page);msgwin_show_hide(TRUE);
 gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),gtk_notebook_page_num(GTK_NOTEBOOK(main_widgets.message_window_notebook),wb.search_page)); gtk_widget_grab_focus(wb.search_view);
}
void insight_workbench_source_link_notify(GeanyEditor *editor,SCNotification *nt) {
 if(wb.alive) insight_search_links_notify(wb.search_links,editor,nt);
}
gboolean insight_workbench_source_link_key(GdkEventKey *event) {
 if(!wb.alive || !wb.search_links || (event->state & gtk_accelerator_get_default_mod_mask())!=GEANY_PRIMARY_MOD_MASK || (event->keyval!=GDK_KEY_l && event->keyval!=GDK_KEY_L)) return FALSE;
 GeanyDocument *doc=document_get_current();if(!doc || !gtk_widget_has_focus(GTK_WIDGET(doc->editor->sci))) return FALSE;
 if(!insight_search_links_return(wb.search_links,doc)) return FALSE;
 focus_search_results(NULL,NULL);return TRUE;
}
static void search_jump_to_link(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;GeanyDocument *doc=document_get_current();
 if(doc && gtk_widget_has_focus(GTK_WIDGET(doc->editor->sci))) { if(insight_search_links_return(wb.search_links,doc)) focus_search_results(NULL,NULL);else ui_set_statusbar(TRUE,"No source link on this editor line");return; }
 GtkTreeModel *model;GtkTreeIter iter;if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.search_view)),&model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter);insight_search_links_open(wb.search_links,path);gtk_tree_path_free(path); }else ui_set_statusbar(TRUE,"Select a search result with a source link");
}
static gboolean search_result_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)data;
 if((event->state & gtk_accelerator_get_default_mod_mask())==GDK_MOD1_MASK && (event->keyval==GDK_KEY_Up || event->keyval==GDK_KEY_Down)) { search_result_move(widget,GINT_TO_POINTER(event->keyval==GDK_KEY_Up ? -2 : 2)); return TRUE; }
 if((event->state & gtk_accelerator_get_default_mod_mask())!=GEANY_PRIMARY_MOD_MASK || (event->keyval!=GDK_KEY_l && event->keyval!=GDK_KEY_L)) return FALSE;
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
 (void)widget; gint direction=GPOINTER_TO_INT(data),step=direction<0 ? -1 : 1,count=gtk_tree_model_iter_n_children(GTK_TREE_MODEL(wb.search_matches),NULL); gboolean open=direction==-1 || direction==1 || direction==-3 || direction==3; if(!count) return;
 GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.search_view)); GtkTreeModel *model; GtkTreeIter iter; gint index=step<0 ? count : -1;
 if(gtk_tree_selection_get_selected(selection,&model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter); index=gtk_tree_path_get_indices(path)[0]; gtk_tree_path_free(path); }
 index=direction==-3 ? 0 : direction==3 ? count-1 : (index+step+count)%count; GtkTreePath *path=gtk_tree_path_new_from_indices(index,-1);
 gtk_tree_view_set_cursor(GTK_TREE_VIEW(wb.search_view),path,NULL,FALSE); gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(wb.search_view),path,NULL,TRUE,0.5f,0); if(open) usage_activated(GTK_TREE_VIEW(wb.search_view),path,NULL,NULL); else usage_selected(selection,NULL); gtk_tree_path_free(path);
}
static void search_copy_results(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; GString *text=g_string_new(NULL); GtkTreeIter iter; GtkTreeModel *model=GTK_TREE_MODEL(wb.search_matches);
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gchar *path,*snippet; gulong line,column; gtk_tree_model_get(model,&iter,USAGE_DISPLAY,&path,USAGE_LINE,&line,USAGE_COLUMN,&column,USAGE_SNIPPET,&snippet,-1); g_string_append_printf(text,"%s:%lu:%lu: %s\n",path,line,column,snippet); g_free(path); g_free(snippet); }
 gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text->str,(gint)text->len); g_string_free(text,TRUE);
}
static void create_search_results(void) {
 wb.search_state_path=g_build_filename(app->configdir,"insight","search-state.conf",NULL);
 wb.search_files=g_ptr_array_new_with_free_func(g_free); wb.search_page=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); wb.search_scope=gtk_combo_box_text_new(); gtk_widget_set_name(wb.search_scope,"insight-search-scope");
 const char *scopes[]={"Project files","Open documents","Current document","Chosen files / folder"}; for(guint i=0;i<G_N_ELEMENTS(scopes);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.search_scope),scopes[i]); gtk_combo_box_set_active(GTK_COMBO_BOX(wb.search_scope),0);
 gtk_box_pack_start(GTK_BOX(row),wb.search_scope,FALSE,FALSE,0); wb.search_mask=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.search_mask),"File masks: *.c;*.h;*.py"); gtk_box_pack_start(GTK_BOX(row),wb.search_mask,TRUE,TRUE,0);
 wb.search_keywords=gtk_check_button_new_with_label("Keywords / Boolean"); gtk_widget_set_name(wb.search_keywords,"insight-search-keywords"); gtk_widget_set_tooltip_text(wb.search_keywords,"Implicit AND, OR, NOT/!, parentheses, ~fuzzy, =case, ?\"regex\"; word fragments unless Word is enabled"); gtk_box_pack_start(GTK_BOX(row),wb.search_keywords,FALSE,FALSE,0);
 wb.search_variations=gtk_check_button_new_with_label("Word variations"); gtk_widget_set_name(wb.search_variations,"insight-search-variations"); gtk_widget_set_tooltip_text(wb.search_variations,"Regular English endings: open/opens/opened/opening, save/saving; base keywords only, not a dictionary stemmer"); gtk_box_pack_start(GTK_BOX(row),wb.search_variations,FALSE,FALSE,0);
 wb.search_context=gtk_spin_button_new_with_range(0,50,1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(wb.search_context),3); gtk_widget_set_tooltip_text(wb.search_context,"Maximum keyword distance in lines (0 = same line)"); gtk_box_pack_start(GTK_BOX(row),wb.search_context,FALSE,FALSE,0);
 wb.search_region=gtk_combo_box_text_new();gtk_widget_set_name(wb.search_region,"insight-search-result-region");const gchar *regions[]={"All text","Skip Comments","Comments only","Code only"};for(guint i=0;i<G_N_ELEMENTS(regions);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.search_region),regions[i]);gtk_combo_box_set_active(GTK_COMBO_BOX(wb.search_region),0);gtk_box_pack_start(GTK_BOX(row),wb.search_region,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(wb.search_page),row,FALSE,FALSE,0); GtkWidget *controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);
 const char *buttons[]={"Files…","Folder…","Search","Stop","Previous","Next","Copy results"};
 for(guint i=0;i<G_N_ELEMENTS(buttons);i++) { GtkWidget *button=gtk_button_new_with_label(buttons[i]); gtk_box_pack_start(GTK_BOX(controls),button,FALSE,FALSE,0); GCallback cb=i<2 ? G_CALLBACK(search_choose_files) : (i==2 ? G_CALLBACK(project_text_search) : (i==3 ? G_CALLBACK(usage_cancel) : (i<6 ? G_CALLBACK(search_result_move) : G_CALLBACK(search_copy_results)))); g_signal_connect(button,"clicked",cb,GINT_TO_POINTER(i==1 ? 1 : (i==4 ? -1 : 1))); }
 GtkWidget *jump=gtk_button_new_with_label("Jump To Link");gtk_widget_set_name(jump,"insight-jump-source-link");gtk_box_pack_start(GTK_BOX(controls),jump,FALSE,FALSE,0);g_signal_connect(jump,"clicked",G_CALLBACK(search_jump_to_link),NULL);gtk_box_pack_start(GTK_BOX(wb.search_page),controls,FALSE,FALSE,0); wb.search_status=gtk_label_new("Search results — Alt+Up/Down previews; Enter/double click opens the match"); gtk_widget_set_name(wb.search_status,"insight-search-status"); gtk_label_set_xalign(GTK_LABEL(wb.search_status),0); gtk_box_pack_start(GTK_BOX(wb.search_page),wb.search_status,FALSE,FALSE,0);
 wb.search_matches=gtk_list_store_new(USAGE_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG); wb.search_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.search_matches)); gtk_widget_set_name(wb.search_view,"insight-search-results"); gtk_widget_set_tooltip_text(wb.search_view,"Alt+Up/Down: browse matches in Context without moving the editor; Enter/double click or Ctrl+L: open the selected match; Shift+F8/F9: previous/next match");
 const char *labels[]={"File","Line","Column","Source"}; const gint columns[]={USAGE_DISPLAY,USAGE_LINE,USAGE_COLUMN,USAGE_SNIPPET};
 for(guint i=0;i<4;i++) { GtkCellRenderer *cell=gtk_cell_renderer_text_new(); g_object_set(cell,"ellipsize",PANGO_ELLIPSIZE_END,NULL); GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(labels[i],cell,"text",columns[i],NULL); gtk_tree_view_column_set_sort_column_id(column,columns[i]); gtk_tree_view_column_set_resizable(column,TRUE); gtk_tree_view_column_set_sizing(column,GTK_TREE_VIEW_COLUMN_FIXED); gtk_tree_view_column_set_fixed_width(column,i==0 ? 260 : (i<3 ? 70 : 450)); gtk_tree_view_column_set_expand(column,i==3); gtk_tree_view_append_column(GTK_TREE_VIEW(wb.search_view),column); }
 g_object_set_data(G_OBJECT(wb.search_view),"insight-list-title","Search Results");insight_list_tools_popup_attach(GTK_TREE_VIEW(wb.search_view));
 wb.search_links=insight_search_links_new(GTK_TREE_VIEW(wb.search_view),(InsightSearchLinkColumns){USAGE_DISPLAY,USAGE_PATH,USAGE_LINE,USAGE_START,USAGE_END,USAGE_ID,USAGE_DIGEST,USAGE_COLUMN});
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.search_view); gtk_box_pack_start(GTK_BOX(wb.search_page),scroll,TRUE,TRUE,0);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.search_view)),"changed",G_CALLBACK(usage_selected),NULL); g_signal_connect(wb.search_view,"row-activated",G_CALLBACK(usage_activated),NULL);
 g_signal_connect(wb.search_view,"key-press-event",G_CALLBACK(search_result_key),NULL);
 g_signal_connect(wb.search_keywords,"toggled",G_CALLBACK(keyword_mode_changed),NULL);
 gtk_notebook_append_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),wb.search_page,gtk_label_new("Search Results")); gtk_widget_show_all(wb.search_page);gtk_widget_set_no_show_all(wb.search_page,TRUE);gtk_widget_hide(wb.search_page);
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
static void search_panel_close(GtkWidget *widget,gpointer data) {
 (void)widget;if(data==wb.search_page) insight_search_links_clear(wb.search_links);gtk_widget_hide(GTK_WIDGET(data));editor_focus();
}
static gboolean search_panel_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;if(event->keyval!=GDK_KEY_Escape) return FALSE;search_panel_close(NULL,data);return TRUE;
}
static void search_native_command(GtkWidget *widget,gpointer data) {
 (void)widget;GtkWidget *item=ui_lookup_widget(main_widgets.window,data);if(GTK_IS_MENU_ITEM(item)) gtk_menu_item_activate(GTK_MENU_ITEM(item));
}
/* Both commands share widgets and execution, but never their persisted state. */
typedef struct {
 GtkWidget *dialog,*query,*scope,*method,*sensitive,*word,*variations,*context,*comments,*mask;
 gboolean references;
} SearchCommandDialog;
static void search_command_method(GtkComboBox *combo,gpointer data) {
 SearchCommandDialog *d=data;gint method=gtk_combo_box_get_active(combo);
 gboolean keywords=method==2,reference=method==3;
 gtk_widget_set_sensitive(d->variations,keywords);gtk_widget_set_sensitive(d->context,keywords);
 gtk_widget_set_sensitive(d->word,!reference);if(reference) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->word),TRUE);
}
static GtkWidget *search_command_check(GtkWidget *box,const gchar *label,const gchar *name) {
 GtkWidget *check=gtk_check_button_new_with_label(label);gtk_widget_set_name(check,name);gtk_box_pack_start(GTK_BOX(box),check,FALSE,FALSE,0);return check;
}
static void search_command_unavailable(GtkWidget *box,const gchar *label,const gchar *reason) {
 GtkWidget *check=search_command_check(box,label,"insight-search-unavailable");gtk_widget_set_sensitive(check,FALSE);gtk_widget_set_tooltip_text(check,reason);
}
static void search_command_response(GtkButton *button,gpointer data) { gtk_dialog_response(GTK_DIALOG(data),GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),"response"))); }
static void search_command_dialog(GtkWidget *widget,gpointer data) {
 (void)widget;SearchCommandDialog d={0};d.references=GPOINTER_TO_INT(data)!=0;
 gchar *path=g_build_filename(app->configdir,"insight",d.references ? "lookup-references.conf" : "search-project-dialog.conf",NULL);
 GKeyFile *state=g_key_file_new();g_key_file_load_from_file(state,path,G_KEY_FILE_NONE,NULL);
 d.dialog=gtk_dialog_new_with_buttons(d.references ? "Lookup References" : "Search Project",GTK_WINDOW(main_widgets.window),GTK_DIALOG_DESTROY_WITH_PARENT,NULL);
 gtk_widget_set_name(d.dialog,d.references ? "insight-lookup-references-dialog" : "insight-search-project-dialog");
 gtk_window_set_default_size(GTK_WINDOW(d.dialog),740,420);gtk_window_set_resizable(GTK_WINDOW(d.dialog),TRUE);
 GtkWidget *outer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12),*left=gtk_box_new(GTK_ORIENTATION_VERTICAL,8),*buttons=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);
 gtk_container_set_border_width(GTK_CONTAINER(outer),12);gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d.dialog))),outer,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(outer),left,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(outer),buttons,FALSE,FALSE,0);
 GtkWidget *label=gtk_label_new(d.references ? "Find References To:" : "Find Keywords:");gtk_label_set_xalign(GTK_LABEL(label),0);gtk_box_pack_start(GTK_BOX(left),label,FALSE,FALSE,0);
 GtkWidget *history=gtk_combo_box_text_new_with_entry();d.query=gtk_bin_get_child(GTK_BIN(history));gtk_widget_set_name(d.query,"insight-search-command-query");gtk_entry_set_max_length(GTK_ENTRY(d.query),1024);gtk_box_pack_start(GTK_BOX(left),history,FALSE,FALSE,0);
 gsize count=0;gchar **queries=g_key_file_get_string_list(state,"Search","History",&count,NULL);for(gsize i=0;queries && i<MIN(count,32);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(history),queries[i]);
 gchar *query=g_key_file_get_string(state,"Search","Query",NULL);gtk_entry_set_text(GTK_ENTRY(d.query),query ? query : "");g_free(query);
 if(d.references) { GeanyDocument *doc=document_get_current();gchar *word=doc ? editor_get_default_selection(doc->editor,TRUE,NULL) : NULL;if(word && !strchr(word,'\n') && !strchr(word,'\r')) gtk_entry_set_text(GTK_ENTRY(d.query),word);g_free(word); }
 label=gtk_label_new("Search In:");gtk_label_set_xalign(GTK_LABEL(label),0);gtk_box_pack_start(GTK_BOX(left),label,FALSE,FALSE,0);
 d.scope=gtk_combo_box_text_new();gtk_widget_set_name(d.scope,"insight-search-command-scope");const gchar *scopes[]={"All Project Files","Open Documents","Current File","Chosen Files / Folder"};for(guint i=0;i<G_N_ELEMENTS(scopes);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d.scope),scopes[i]);gtk_combo_box_set_active(GTK_COMBO_BOX(d.scope),CLAMP(g_key_file_get_integer(state,"Search","Scope",NULL),0,3));gtk_box_pack_start(GTK_BOX(left),d.scope,FALSE,FALSE,0);
 d.mask=gtk_entry_new();gtk_widget_set_name(d.mask,"insight-search-command-mask");gtk_entry_set_placeholder_text(GTK_ENTRY(d.mask),"File masks: *.c;*.h (empty = all)");gchar *mask=g_key_file_get_string(state,"Search","Mask",NULL);gtk_entry_set_text(GTK_ENTRY(d.mask),mask ? mask : "");g_free(mask);gtk_box_pack_start(GTK_BOX(left),d.mask,FALSE,FALSE,0);
 GtkWidget *frame=gtk_frame_new("Options"),*options=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,18),*checks=gtk_box_new(GTK_ORIENTATION_VERTICAL,6),*methods=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_container_set_border_width(GTK_CONTAINER(options),10);gtk_container_add(GTK_CONTAINER(frame),options);gtk_box_pack_start(GTK_BOX(options),checks,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(options),methods,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(left),frame,TRUE,TRUE,0);
 d.sensitive=search_command_check(checks,"Case Sensitive","insight-search-command-case");d.word=search_command_check(checks,"Whole Words Only","insight-search-command-word");
 search_command_unavailable(checks,"Skip Inactive Code","Preprocessor condition evaluation is not implemented.");
 label=gtk_label_new("Comment regions:");gtk_label_set_xalign(GTK_LABEL(label),0);gtk_box_pack_start(GTK_BOX(checks),label,FALSE,FALSE,0);d.comments=gtk_combo_box_text_new();gtk_widget_set_name(d.comments,"insight-search-command-comments");const gchar *regions[]={"All text","Skip Comments","Search Only Comments","Code only (skip strings too)"};for(guint i=0;i<G_N_ELEMENTS(regions);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d.comments),regions[i]);gtk_combo_box_set_active(GTK_COMBO_BOX(d.comments),CLAMP(g_key_file_get_integer(state,"Search","Comments",NULL),0,3));gtk_box_pack_start(GTK_BOX(checks),d.comments,FALSE,FALSE,0);
 search_command_unavailable(checks,"Smart Reference Matching","References are lexical whole identifiers; overload and type resolution is not implemented.");search_command_unavailable(checks,"Touch files and cause recompile","Search is read-only; touching project files is not implemented.");
 label=gtk_label_new("Search Method:");gtk_label_set_xalign(GTK_LABEL(label),0);gtk_box_pack_start(GTK_BOX(methods),label,FALSE,FALSE,0);d.method=gtk_combo_box_text_new();gtk_widget_set_name(d.method,"insight-search-command-method");const gchar *names[]={"Simple String","Regular Expression","Keyword Expression","Lookup Reference (lexical)"};for(guint i=0;i<G_N_ELEMENTS(names);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d.method),names[i]);gtk_box_pack_start(GTK_BOX(methods),d.method,FALSE,FALSE,0);
 d.variations=search_command_check(methods,"Find word variations","insight-search-command-variations");label=gtk_label_new("Lines of Context:");gtk_label_set_xalign(GTK_LABEL(label),0);gtk_box_pack_start(GTK_BOX(methods),label,FALSE,FALSE,0);d.context=gtk_spin_button_new_with_range(0,50,1);gtk_widget_set_name(d.context,"insight-search-command-context");gtk_box_pack_start(GTK_BOX(methods),d.context,FALSE,FALSE,0);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d.sensitive),g_key_file_has_key(state,"Search","Case",NULL) ? g_key_file_get_boolean(state,"Search","Case",NULL) : d.references);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d.word),g_key_file_get_boolean(state,"Search","Word",NULL));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d.variations),g_key_file_get_boolean(state,"Search","Variations",NULL));gtk_spin_button_set_value(GTK_SPIN_BUTTON(d.context),g_key_file_has_key(state,"Search","Context",NULL) ? CLAMP(g_key_file_get_integer(state,"Search","Context",NULL),0,50) : 5);
 gtk_combo_box_set_active(GTK_COMBO_BOX(d.method),g_key_file_has_key(state,"Search","Method",NULL) ? CLAMP(g_key_file_get_integer(state,"Search","Method",NULL),0,3) : d.references ? 3 : 2);g_signal_connect(d.method,"changed",G_CALLBACK(search_command_method),&d);search_command_method(GTK_COMBO_BOX(d.method),&d);
 const gchar *actions[]={"Search","Close","Help"};const gint responses[]={GTK_RESPONSE_ACCEPT,GTK_RESPONSE_CLOSE,GTK_RESPONSE_HELP};for(guint i=0;i<3;i++) { GtkWidget *button=gtk_button_new_with_label(actions[i]);gtk_box_pack_start(GTK_BOX(buttons),button,FALSE,FALSE,0);g_object_set_data(G_OBJECT(button),"response",GINT_TO_POINTER(responses[i]));g_signal_connect(button,"clicked",G_CALLBACK(search_command_response),d.dialog);if(!i) { gtk_widget_set_can_default(button,TRUE);gtk_widget_grab_default(button); } }
 gtk_entry_set_activates_default(GTK_ENTRY(d.query),TRUE);label=gtk_label_new("Lexical references; comment filters skip unsupported languages.\nKeyword fragments: turn Whole Words Only off.");gtk_label_set_line_wrap(GTK_LABEL(label),TRUE);gtk_label_set_xalign(GTK_LABEL(label),0);gtk_box_pack_start(GTK_BOX(left),label,FALSE,FALSE,0);
 gtk_widget_show_all(d.dialog);gtk_widget_grab_focus(d.query);gtk_editable_select_region(GTK_EDITABLE(d.query),0,-1);gint response;
 for(;;) { response=gtk_dialog_run(GTK_DIALOG(d.dialog));if(response==GTK_RESPONSE_HELP) { insight_help_show(NULL,"search-command-dialogs.md");continue; }if(response!=GTK_RESPONSE_ACCEPT) break;
  const gchar *q=gtk_entry_get_text(GTK_ENTRY(d.query));gint m=gtk_combo_box_get_active(GTK_COMBO_BOX(d.method));guint flags=(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.sensitive)) ? INSIGHT_SEARCH_CASE : 0)|(m==3 || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.word)) ? INSIGHT_SEARCH_WORD : 0)|(m==1 ? INSIGHT_SEARCH_REGEX : 0)|(m==2 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.variations)) ? INSIGHT_KEYWORD_VARIATIONS : 0);size_t total=0;GError *validation=NULL;
  gboolean valid=*q && (m==3 ? insight_occurrences_find("",0,q,strlen(q),0,NULL,0,&total) : m==2 ? insight_keyword_collect("",0,q,flags,gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d.context)),NULL,0,&total,&validation) : insight_context_collect("",0,q,flags,NULL,0,&total,&validation));
  if(valid) { g_clear_error(&validation);break; }gtk_label_set_text(GTK_LABEL(label),validation ? validation->message : "Enter a query; Lookup Reference requires one UTF-8 identifier.");g_clear_error(&validation);gtk_widget_grab_focus(d.query);
 }
 const gchar *text=gtk_entry_get_text(GTK_ENTRY(d.query));gint method=gtk_combo_box_get_active(GTK_COMBO_BOX(d.method)),scope=gtk_combo_box_get_active(GTK_COMBO_BOX(d.scope)),comments=gtk_combo_box_get_active(GTK_COMBO_BOX(d.comments));
 g_key_file_set_string(state,"Search","Mask",gtk_entry_get_text(GTK_ENTRY(d.mask)));g_key_file_set_string(state,"Search","Query",text);g_key_file_set_integer(state,"Search","Scope",scope);g_key_file_set_integer(state,"Search","Method",method);g_key_file_set_integer(state,"Search","Comments",comments);g_key_file_set_integer(state,"Search","Context",gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d.context)));
 g_key_file_set_boolean(state,"Search","Case",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.sensitive)));g_key_file_set_boolean(state,"Search","Word",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.word)));g_key_file_set_boolean(state,"Search","Variations",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.variations)));
 GPtrArray *saved=g_ptr_array_new_with_free_func(g_free);if(response==GTK_RESPONSE_ACCEPT && *text) g_ptr_array_add(saved,g_strdup(text));for(gsize i=0;queries && i<count && saved->len<32;i++) if(g_strcmp0(text,queries[i]) || response!=GTK_RESPONSE_ACCEPT) g_ptr_array_add(saved,g_strdup(queries[i]));g_key_file_set_string_list(state,"Search","History",(const gchar *const *)saved->pdata,saved->len);
 gchar *parent=g_path_get_dirname(path),*contents=g_key_file_to_data(state,NULL,NULL);GError *error=NULL;if(g_mkdir_with_parents(parent,0700)<0 || !g_file_set_contents(path,contents,-1,&error)) ui_set_statusbar(TRUE,"Cannot save search dialog settings: %s",error ? error->message : "cannot create directory");g_clear_error(&error);g_free(parent);g_free(contents);
 if(response==GTK_RESPONSE_ACCEPT && *text) {
  wb.relation_setting=TRUE;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE);gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),3);gtk_entry_set_text(GTK_ENTRY(wb.usage_entry),text);gtk_entry_set_text(GTK_ENTRY(wb.search_mask),gtk_entry_get_text(GTK_ENTRY(d.mask)));gtk_combo_box_set_active(GTK_COMBO_BOX(wb.search_scope),scope);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_case),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.sensitive)));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_word),method==3 || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.word)));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_regex),method==1);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.search_keywords),method==2);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.search_variations),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d.variations)));gtk_spin_button_set_value(GTK_SPIN_BUTTON(wb.search_context),gtk_spin_button_get_value(GTK_SPIN_BUTTON(d.context)));gtk_combo_box_set_active(GTK_COMBO_BOX(wb.search_region),comments);wb.relation_setting=FALSE;
  gtk_widget_hide(d.dialog);if(scope==3 && !wb.search_files->len) search_choose_files(NULL,NULL);if(scope!=3 || wb.search_files->len) { usage_start(NULL,NULL);focus_search_results(NULL,NULL); }
 }
 gtk_widget_destroy(d.dialog);g_ptr_array_unref(saved);g_strfreev(queries);g_key_file_unref(state);g_free(path);
}
static void search_scope_command(GtkWidget *widget,gpointer data) {
 (void)widget;gint scope=GPOINTER_TO_INT(data);if(scope==0) { search_command_dialog(NULL,NULL);return; }if(scope<2) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.search_scope),scope);search_toolbar_focus(NULL,NULL);if(scope>=2) search_choose_files(NULL,GINT_TO_POINTER(scope==2));
}
static void create_search_toolbars(void) {
 wb.search_history=g_ptr_array_new_with_free_func(g_free);
 GtkWidget *parent=ui_lookup_widget(main_widgets.window,"vbox1");
 gint position=gtk_widget_get_parent(main_widgets.toolbar)==parent ? 2 : 1;
 for(guint i=0;i<2;i++) {
  GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*field=i ? NULL : gtk_search_entry_new(),*sensitive=gtk_check_button_new_with_label("Match case"),*word=gtk_check_button_new_with_label("Whole word"),*regex=gtk_check_button_new_with_label("Regex");
  if(i) { wb.editor_history_combo=gtk_combo_box_text_new_with_entry();gtk_widget_set_name(wb.editor_history_combo,"insight-file-search-history");field=gtk_bin_get_child(GTK_BIN(wb.editor_history_combo));ui_entry_add_clear_icon(GTK_ENTRY(field)); }
  gtk_widget_set_tooltip_text(field,"Use ~term for fuzzy search when Regex is off; characters match in order within a line"); gtk_entry_set_max_length(GTK_ENTRY(field),1024); gtk_widget_set_hexpand(field,TRUE); gtk_widget_set_size_request(field,180,-1);
  gtk_box_pack_start(GTK_BOX(row),gtk_label_new(i ? "File Search:" : "Search project:"),FALSE,FALSE,3); gtk_box_pack_start(GTK_BOX(row),i ? wb.editor_history_combo : field,TRUE,TRUE,0);
  if(i) {
   wb.editor_search_row=row; wb.editor_query=field;gtk_widget_set_name(field,"insight-file-search-query"); wb.editor_case=sensitive; wb.editor_word=word; wb.editor_regex=regex;
   GtkWidget *previous=gtk_button_new_with_label("Previous"),*next=gtk_button_new_with_label("Next");
   gtk_widget_set_name(previous,"insight-file-search-previous");gtk_widget_set_name(next,"insight-file-search-next");gtk_button_set_always_show_image(GTK_BUTTON(next),TRUE);gtk_button_set_always_show_image(GTK_BUTTON(previous),TRUE);gtk_button_set_image(GTK_BUTTON(next),gtk_image_new_from_icon_name("go-down",GTK_ICON_SIZE_MENU));gtk_button_set_image(GTK_BUTTON(previous),gtk_image_new_from_icon_name("go-up",GTK_ICON_SIZE_MENU));gtk_box_pack_start(GTK_BOX(row),next,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),previous,FALSE,FALSE,0);
   GtkWidget *files=gtk_button_new_with_label("Search Files…");gtk_widget_set_name(files,"insight-file-search-files");gtk_button_set_image(GTK_BUTTON(files),gtk_image_new_from_icon_name("system-search",GTK_ICON_SIZE_MENU));gtk_button_set_always_show_image(GTK_BUTTON(files),TRUE);gtk_box_pack_start(GTK_BOX(row),files,FALSE,FALSE,0);g_signal_connect(files,"clicked",G_CALLBACK(search_scope_command),GINT_TO_POINTER(0));
   g_signal_connect(previous,"clicked",G_CALLBACK(editor_text_search),GINT_TO_POINTER(-1)); g_signal_connect(next,"clicked",G_CALLBACK(editor_text_search),NULL); g_signal_connect(field,"activate",G_CALLBACK(editor_text_search),NULL);
  } else {
   wb.project_search_row=row; wb.project_query=field; gtk_widget_set_name(field,"insight-project-query"); wb.project_case=sensitive; wb.project_word=word; wb.project_regex=regex;
   GtkWidget *history=gtk_button_new_with_label("History"); gtk_widget_set_name(history,"insight-search-history"); gtk_widget_set_tooltip_text(history,"Last 32 valid search queries; choosing a query retains the current scope and options"); gtk_box_pack_start(GTK_BOX(row),history,FALSE,FALSE,0); g_signal_connect(history,"clicked",G_CALLBACK(search_history_menu),NULL);
   GtkWidget *search=gtk_button_new_with_label("Search"); gtk_box_pack_start(GTK_BOX(row),search,FALSE,FALSE,0); g_signal_connect(search,"clicked",G_CALLBACK(project_text_search),NULL); g_signal_connect(field,"activate",G_CALLBACK(project_text_search),NULL);
  }
  if(i) {
   GtkWidget *options=gtk_menu_button_new();gtk_button_set_label(GTK_BUTTON(options),"Options");gtk_button_set_image(GTK_BUTTON(options),gtk_image_new_from_icon_name("preferences-system",GTK_ICON_SIZE_MENU));gtk_button_set_always_show_image(GTK_BUTTON(options),TRUE);gtk_widget_set_name(options,"insight-file-search-options");GtkWidget *popover=gtk_popover_new(options),*checks=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);gtk_container_set_border_width(GTK_CONTAINER(checks),8);gtk_container_add(GTK_CONTAINER(popover),checks);gtk_menu_button_set_popover(GTK_MENU_BUTTON(options),popover);gtk_box_pack_start(GTK_BOX(row),options,FALSE,FALSE,0);
   wb.editor_wrap=gtk_check_button_new_with_label("Wrap around");wb.editor_select=gtk_check_button_new_with_label("Select text when found");wb.editor_incremental=gtk_check_button_new_with_label("Search while typing");wb.editor_load_word=gtk_check_button_new_with_label("Load word at cursor when buttons are pressed");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_wrap),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_select),TRUE);
   GtkWidget *items[]={sensitive,word,regex,wb.editor_wrap,wb.editor_select,wb.editor_incremental,wb.editor_load_word};const gchar *names[]={"case","word","regex","wrap","select","incremental","load-word"};for(guint n=0;n<G_N_ELEMENTS(items);n++) { gtk_box_pack_start(GTK_BOX(n==0 || n==3 ? row : checks),items[n],FALSE,FALSE,0);gchar *name=g_strconcat("insight-file-search-",names[n],NULL);gtk_widget_set_name(items[n],name);g_free(name);if(n<3) g_signal_connect(items[n],"toggled",G_CALLBACK(editor_search_sync),NULL); }const gchar *actions[]={"Find All in Document","Find All in Open Documents","Mark all matches"};for(guint n=0;n<3;n++) { GtkWidget *action=gtk_button_new_with_label(actions[n]);gtk_box_pack_start(GTK_BOX(checks),action,FALSE,FALSE,0);g_signal_connect(action,"clicked",G_CALLBACK(editor_search_all),GINT_TO_POINTER(n)); }gtk_box_reorder_child(GTK_BOX(row),sensitive,-1);gtk_widget_show_all(checks);g_signal_connect(field,"changed",G_CALLBACK(editor_search_sync),NULL);
  } else { gtk_box_pack_start(GTK_BOX(row),sensitive,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),word,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),regex,FALSE,FALSE,0); }
  gtk_box_pack_end(GTK_BOX(row),insight_panel_help_button(i ? "panel-find-file.md" : "panel-search-results.md"),FALSE,FALSE,0);
  GtkWidget *close=gtk_button_new_from_icon_name("window-close",GTK_ICON_SIZE_BUTTON);gtk_widget_set_tooltip_text(close,"Close search panel (Escape)");gtk_box_pack_end(GTK_BOX(row),close,FALSE,FALSE,0);g_signal_connect(close,"clicked",G_CALLBACK(search_panel_close),row);
  g_signal_connect(field,"key-press-event",G_CALLBACK(search_panel_key),row);gtk_box_pack_start(GTK_BOX(parent),row,FALSE,FALSE,0);gtk_box_reorder_child(GTK_BOX(parent),row,position+(gint)i);gtk_widget_set_name(row,i ? "insight-editor-search-panel" : "insight-project-search-panel");gtk_widget_show_all(row);gtk_widget_set_no_show_all(row,TRUE);gtk_widget_hide(row);
 }
 GtkWidget *search_menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"search1")));
 GtkWidget *quick=gtk_menu_item_new_with_label("Quick find in file…");gtk_menu_shell_append(GTK_MENU_SHELL(search_menu),quick);g_signal_connect(quick,"activate",G_CALLBACK(search_toolbar_focus),GINT_TO_POINTER(1));gtk_widget_show(quick);
 create_search_results();
 const gchar *link_names[]={"Jump To Link","Go To First Link","Go To Previous Link","Go To Next Link","Go To Last Link"};const gint link_directions[]={0,-3,-1,1,3};for(guint i=0;i<G_N_ELEMENTS(link_names);i++) { GtkWidget *item=gtk_menu_item_new_with_label(link_names[i]);gchar *name=g_strdup_printf("insight-source-link-command-%u",i);gtk_widget_set_name(item,name);g_free(name);gtk_menu_shell_append(GTK_MENU_SHELL(search_menu),item);g_signal_connect(item,"activate",i ? G_CALLBACK(search_result_move) : G_CALLBACK(search_jump_to_link),GINT_TO_POINTER(link_directions[i]));gtk_widget_show(item); }
 GtkWidget *lookup=gtk_menu_item_new_with_label("Lookup References…");gtk_widget_set_name(lookup,"insight-lookup-references-command");gtk_menu_shell_append(GTK_MENU_SHELL(search_menu),lookup);g_signal_connect(lookup,"activate",G_CALLBACK(search_command_dialog),GINT_TO_POINTER(1));gtk_widget_show(lookup);
 gtk_menu_item_set_label(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"search1")),"Поиск/замена");
 const gchar *scope_names[]={"Search project…","Search open documents…","Search folder…","Search selected files…"};
 for(guint i=0;i<4;i++) { GtkWidget *item=gtk_menu_item_new_with_label(scope_names[i]);gtk_widget_set_name(item,i==0 ? "insight-search-project-command" : (i==2 ? "insight-search-folder-command" : "insight-search-scope-command"));gtk_menu_shell_append(GTK_MENU_SHELL(search_menu),item);g_signal_connect(item,"activate",G_CALLBACK(search_scope_command),GINT_TO_POINTER(i));gtk_widget_show(item); }
 GtkWidget *close=gtk_button_new_from_icon_name("window-close",GTK_ICON_SIZE_BUTTON);gtk_widget_set_tooltip_text(close,"Close search results");gtk_box_pack_start(GTK_BOX(wb.search_page),close,FALSE,FALSE,0);g_signal_connect(close,"clicked",G_CALLBACK(search_panel_close),wb.search_page);gtk_widget_show(close);
}
static void toolbar_relations(GtkToolButton *button,gpointer data) { (void)button;focus_relations(NULL,data); }
static void add_insight_tool(GtkToolbar *toolbar,GtkToolItem *item,gint position) {
 toolbar_item_ref(item);gtk_toolbar_insert(toolbar,item,position);
}
static void create_browse_toolbar(void) {
 insight_git_toolbar();
 GtkToolbar *toolbar=GTK_TOOLBAR(main_widgets.toolbar);
 GtkWidget *search_field=toolbar_get_widget_by_name("SearchEntry");if(search_field) { gtk_widget_set_no_show_all(search_field,TRUE);gtk_widget_hide(search_field); }
 add_insight_tool(toolbar,gtk_separator_tool_item_new(),0);
 const char *icons[]={"go-previous","go-next","go-jump","edit-find","system-search","folder-open","view-refresh"};
 const char *labels[]={"Context back","Context forward","Go to definition","Project symbols","Project files","Open source workspace","Reindex workspace"};
 for(guint i=0;i<7;i++) {
  GtkToolItem *item=gtk_tool_button_new(NULL,NULL); gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(item),icons[i]); gtk_widget_set_tooltip_text(GTK_WIDGET(item),labels[i]);
  if(i<2) g_signal_connect(item,"clicked",G_CALLBACK(history_move),GINT_TO_POINTER(i ? 1 : -1));
  else if(i==2) g_signal_connect(item,"clicked",G_CALLBACK(toolbar_definition),NULL);
  else if(i<5) g_signal_connect(item,"clicked",G_CALLBACK(toolbar_focus),GINT_TO_POINTER(i==4 ? 1 : 0));
  else if(i==5) g_signal_connect(item,"clicked",G_CALLBACK(choose_root),NULL);
  else g_signal_connect(item,"clicked",G_CALLBACK(reindex_root),NULL);
  add_insight_tool(toolbar,item,(gint)i);
 }
 gtk_toolbar_set_style(toolbar,GTK_TOOLBAR_ICONS);gtk_toolbar_set_icon_size(toolbar,GTK_ICON_SIZE_SMALL_TOOLBAR); gtk_widget_show_all(GTK_WIDGET(toolbar));
 GtkToolItem *templates=gtk_tool_button_new(insight_command_icon("snippets"),NULL); gtk_widget_set_name(GTK_WIDGET(templates),"insight-templates-tool"); gtk_widget_set_tooltip_text(GTK_WIDGET(templates),"Code templates / Clips"); g_signal_connect(templates,"clicked",G_CALLBACK(focus_templates),NULL); add_insight_tool(toolbar,templates,-1);
 GtkToolItem *book=gtk_tool_button_new(insight_command_icon("bookmarks"),NULL);gtk_widget_set_tooltip_text(GTK_WIDGET(book),"Bookmarks — all files and lines");g_signal_connect(book,"clicked",G_CALLBACK(show_bookmarks),NULL);add_insight_tool(toolbar,book,-1);
 GtkToolItem *relations=gtk_tool_button_new(insight_command_icon("relations"),NULL);gtk_widget_set_tooltip_text(GTK_WIDGET(relations),"Relation Window — calls, call sites, members and inheritance");g_signal_connect(relations,"clicked",G_CALLBACK(toolbar_relations),NULL);add_insight_tool(toolbar,relations,-1);
 GtkToolItem *diff=gtk_tool_button_new(insight_command_icon("diff"),NULL);gtk_widget_set_name(GTK_WIDGET(diff),"insight-diff-tool");gtk_widget_set_tooltip_text(GTK_WIDGET(diff),"Diff / Merge — internal comparison and merging");g_signal_connect(diff,"clicked",G_CALLBACK(show_diff),NULL);add_insight_tool(toolbar,diff,-1);
 const gchar *search_icons[]={"edit-find","edit-find-replace","system-search"};const gchar *search_tips[]={"Find in file (Ctrl+F)","Replace in file (Ctrl+H)","Search project / folder / open files"};
 for(guint i=0;i<3;i++) { GtkToolItem *button=gtk_tool_button_new(insight_command_icon(search_icons[i]),NULL);gtk_widget_set_tooltip_text(GTK_WIDGET(button),search_tips[i]);gtk_widget_set_name(GTK_WIDGET(button),i==0 ? "insight-find-tool" : (i==1 ? "insight-replace-tool" : "insight-project-search-tool"));g_signal_connect(button,"clicked",i<2 ? G_CALLBACK(search_native_command) : G_CALLBACK(search_scope_command),i<2 ? (gpointer)(i ? "replace1" : "find1") : GINT_TO_POINTER(0));add_insight_tool(toolbar,button,-1); }
 const gchar *help_icons[]={"manual","tutorial"};const gchar *help_tips[]={"Geany Insight — documentation","Geany Insight — tutorials"};
 for(guint i=0;i<2;i++) { GtkToolItem *button=gtk_tool_button_new(insight_command_icon(help_icons[i]),NULL);gtk_widget_set_name(GTK_WIDGET(button),i ? "insight-tutorial-tool" : "insight-help-tool");gtk_widget_set_tooltip_text(GTK_WIDGET(button),help_tips[i]);g_signal_connect(button,"clicked",G_CALLBACK(insight_help_show),(gpointer)(i ? "tutorials.md" : "guide.md"));add_insight_tool(toolbar,button,-1); }
 GtkToolItem *preview=gtk_tool_button_new(insight_command_icon("text-x-generic"),NULL);gtk_widget_set_name(GTK_WIDGET(preview),"insight-markup-preview-tool");gtk_widget_set_tooltip_text(GTK_WIDGET(preview),"Preview current Markdown / RST — including unsaved edits");g_signal_connect(preview,"clicked",G_CALLBACK(insight_markup_preview),NULL);add_insight_tool(toolbar,preview,-1);
 add_insight_tool(toolbar,gtk_separator_tool_item_new(),-1);
 const char *layouts[]={"layout-a","layout-b","layout-c","layout-d"};
 const char *tips[]={"Load layout A","Load layout B","Load layout C","Load layout D"};
 for(guint i=0;i<4;i++) { GtkToolItem *item=gtk_tool_button_new(insight_command_icon(layouts[i]),NULL); gtk_widget_set_tooltip_text(GTK_WIDGET(item),tips[i]); g_signal_connect(item,"clicked",G_CALLBACK(apply_layout),GINT_TO_POINTER(i)); add_insight_tool(toolbar,item,-1); }
 gtk_widget_show_all(GTK_WIDGET(toolbar));
}
static void create_help_menu(void) {
 GtkWidget *menu=ui_lookup_widget(main_widgets.window,"menu_help1_menu");const gchar *labels[]={"Geany Insight — Documentation…","Geany Insight — Tutorials…","Lua scripting guide…","Markdown / reStructuredText viewer…","Preview current Markdown / RST…"};
 const gchar *topics[]={"guide.md","tutorials.md","scripting.md",NULL,NULL};
 for(guint i=0;i<5;i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(item,i==0 ? "insight-help-command" : (i==1 ? "insight-tutorial-command" : (i==4 ? "insight-markup-preview-command" : "insight-guide-command")));gtk_menu_shell_insert(GTK_MENU_SHELL(menu),item,i);g_signal_connect(item,"activate",i==3 ? G_CALLBACK(insight_markup_open) : (i==4 ? G_CALLBACK(insight_markup_preview) : G_CALLBACK(insight_help_show)),(gpointer)topics[i]);gtk_widget_show(item); }
}
static void window_list_preview(guint id) {
 GeanyDocument *doc=document_find_by_id(id);if(!doc || !wb.preview) return;
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);show_location(doc->file_name,doc->id,sci_get_current_line(doc->editor->sci)+1,TRUE);
}
static void window_activate(GtkMenuItem *item,gpointer data) {
 (void)item; GeanyDocument *doc=document_find_by_id(GPOINTER_TO_UINT(data));
 if(doc) { gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.notebook),document_get_notebook_page(doc)); document_grab_focus(doc); }
}
static void window_menu_refresh(GtkWidget *menu,gpointer data) {
 (void)data; GList *items=gtk_container_get_children(GTK_CONTAINER(menu)); for(GList *p=items;p;p=p->next) if(!g_object_get_data(G_OBJECT(p->data),"insight-window-persistent")) gtk_widget_destroy(p->data); g_list_free(items);
 GtkWidget *windows=gtk_menu_item_new_with_label("Window List…");gtk_widget_set_name(windows,"insight-window-list-command");g_signal_connect(windows,"activate",G_CALLBACK(insight_window_list_show),NULL);gtk_menu_shell_insert(GTK_MENU_SHELL(menu),windows,0);gtk_widget_show(windows);
 GtkWidget *separator=gtk_separator_menu_item_new();gtk_menu_shell_insert(GTK_MENU_SHELL(menu),separator,1);gtk_widget_show(separator);
 for(guint i=0;i<documents_array->len;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i); if(!doc->is_valid) continue;
  GtkWidget *item=gtk_check_menu_item_new_with_label(doc->file_name ? doc->file_name : "untitled");g_object_set_data(G_OBJECT(item),"insight-no-i18n",GINT_TO_POINTER(1));
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),doc==document_get_current());
  g_signal_connect(item,"activate",G_CALLBACK(window_activate),GUINT_TO_POINTER(doc->id)); gtk_container_add(GTK_CONTAINER(menu),item); gtk_widget_show(item);
 }
}
static void arrange_main_menus(void) {
 GtkWidget *bar=ui_lookup_widget(main_widgets.window,"menubar1"),*options=gtk_menu_item_new_with_mnemonic("_Options"),*menu=gtk_menu_new();
 gtk_menu_item_set_submenu(GTK_MENU_ITEM(options),menu); gtk_menu_shell_insert(GTK_MENU_SHELL(bar),options,4);
 GtkWidget *preferences=ui_lookup_widget(main_widgets.window,"preferences1");
 g_object_ref(preferences); gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(preferences)),preferences); gtk_container_add(GTK_CONTAINER(menu),preferences); g_object_unref(preferences); gtk_widget_set_name(menu,"insight-options-menu");insight_i18n_menu(menu);GtkWidget *languages_item=gtk_menu_item_new_with_label("Language profiles…");gtk_widget_set_name(languages_item,"insight-language-options");g_signal_connect(languages_item,"activate",G_CALLBACK(insight_languages_show),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),languages_item);GtkWidget *filetype_options=gtk_menu_item_new_with_label("File Type Options…");gtk_widget_set_name(filetype_options,"insight-filetype-options");g_signal_connect(filetype_options,"activate",G_CALLBACK(insight_languages_show),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),filetype_options);insight_formatting_menu(ui_lookup_widget(main_widgets.window,"menu_view1_menu"),menu);insight_themes_menu(menu);insight_completion_menu(menu);insight_paste_menu(menu);insight_paste_line_menu(ui_lookup_widget(main_widgets.window,"edit1_menu"));insight_paste_tools_menu(ui_lookup_widget(main_widgets.window,"edit1_menu"));insight_navigation_menu(ui_lookup_widget(main_widgets.window,"edit1_menu"));gtk_widget_show_all(options);
 GtkWidget *document_menu=ui_lookup_widget(main_widgets.window,"menu_document1");
 g_object_ref(document_menu); gtk_container_remove(GTK_CONTAINER(bar),document_menu); gtk_container_add(GTK_CONTAINER(menu),document_menu); g_object_unref(document_menu);
 GtkWidget *windows=gtk_menu_item_new_with_mnemonic("_Window"),*window_menu=gtk_menu_new();
 insight_scroll_links_init(window_menu);
 gtk_widget_set_name(window_menu,"insight-window-menu");gtk_menu_item_set_submenu(GTK_MENU_ITEM(windows),window_menu); g_signal_connect(window_menu,"show",G_CALLBACK(window_menu_refresh),NULL); gtk_menu_shell_insert(GTK_MENU_SHELL(bar),windows,7); gtk_widget_show(windows);
 GtkWidget *items[]={ui_lookup_widget(main_widgets.window,"file1"),ui_lookup_widget(main_widgets.window,"edit1"),ui_lookup_widget(main_widgets.window,"search1"),ui_lookup_widget(main_widgets.window,"menu_project1"),options,ui_lookup_widget(main_widgets.window,"tools1"),ui_lookup_widget(main_widgets.window,"menu_view1"),windows,ui_lookup_widget(main_widgets.window,"menu_help1"),g_object_get_data(G_OBJECT(main_widgets.window),"insight-navigate"),wb.lua_item};
 for(guint i=0;i<G_N_ELEMENTS(items);i++) {
  GtkWidget *item=items[i];
  g_object_ref(item); gtk_container_remove(GTK_CONTAINER(bar),item); gtk_menu_shell_insert(GTK_MENU_SHELL(bar),item,(gint)i); g_object_unref(item);
 }
}
void insight_workbench_index_changed(void) { if(wb.alive && wb.bookmark_handles && !wb.bookmark_sync_timer) wb.bookmark_sync_timer=g_idle_add(bookmark_sync_idle,NULL);stop_definition(); wb.follow_doc=0; mark_index_dirty(); insight_workbench_refresh(); }
static gboolean remote_workspace(const char *path) { gboolean previous=wb.workspace_from_project; wb.workspace_from_project=FALSE; if(start_scan(path)) return TRUE; wb.workspace_from_project=previous; return FALSE; }
static void follow_changed(GtkToggleButton *button,gpointer data) {
 (void)data;
 if(gtk_toggle_button_get_active(button)) { stop_file_preview(); wb.follow_doc=0; queue_definition(TRUE); insight_workbench_refresh(); }
 else stop_definition();
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
static void show_diff(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; insight_diff_show(main_widgets.window);
}
static void context_tools_toggle(GtkToggleButton *button,gpointer row) {
 gboolean visible=gtk_toggle_button_get_active(button);gtk_widget_set_visible(GTK_WIDGET(row),visible);
 if(visible && row==wb.context_find_row) gtk_widget_grab_focus(wb.find_entry);
 GtkWidget *focus=gtk_window_get_focus(GTK_WINDOW(main_widgets.window));
 if(!visible && focus && gtk_widget_is_ancestor(focus,GTK_WIDGET(row))) gtk_widget_grab_focus(GTK_WIDGET(wb.preview));
}
static void context_icon(GtkWidget *button,const gchar *icon,const gchar *tip,const gchar *name) {
 gtk_button_set_label(GTK_BUTTON(button),NULL);gtk_button_set_image(GTK_BUTTON(button),gtk_image_new_from_icon_name(icon,GTK_ICON_SIZE_MENU));
 gtk_button_set_always_show_image(GTK_BUTTON(button),TRUE);gtk_button_set_relief(GTK_BUTTON(button),GTK_RELIEF_NONE);gtk_widget_set_tooltip_text(button,tip);gtk_widget_set_name(button,name);
}
static InsightReportSnapshot *capture_project_report(GError **error)
{
 if(!wb.alive || !app->project || !wb.workspace_from_project || !wb.root) {
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_INITIALIZED,"Open a Geany project before generating a project report.");return NULL;
 }
 refresh_index();
 if(wb.scan || wb.scan_worker || wb.snapshot_worker || wb.index_dirty || !wb.index || wb.scan_failed || !wb.report_scan_completed) {
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_BUSY,"Project indexing is not ready. Wait for a completed scan before generating a report.");return NULL;
 }
 gchar *root=utils_get_utf8_from_locale(wb.root);
 InsightReportSnapshot *snapshot=insight_report_snapshot_new(app->project->name,root,app->project->file_name,wb.files,wb.index,wb.skipped,wb.files->len>=10000 || wb.skipped>0 || wb.watch_incomplete,error);g_free(root);return snapshot;
}
/* GtkPaned shifts an undersized first child left to preserve its minimum.
 * A width-constrained host keeps the source workspace at x=0 and forwards the
 * available width to its vertical/editor splitters. Preserve height requests:
 * allowing the same underallocation vertically breaks notebook tab gadgets. */
typedef struct { GtkBin parent; } InsightWidthHost;
typedef struct { GtkBinClass parent; } InsightWidthHostClass;
static GType insight_width_host_get_type(void);
G_DEFINE_TYPE(InsightWidthHost,insight_width_host,GTK_TYPE_BIN)
static void width_host_width(GtkWidget *widget,gint *minimum,gint *natural) {
 GtkWidget *child=gtk_bin_get_child(GTK_BIN(widget));gint ignored=0;*minimum=1;*natural=1;if(child && gtk_widget_get_visible(child)) gtk_widget_get_preferred_width(child,&ignored,natural);*natural=MAX(*minimum,*natural);
}
static void width_host_height(GtkWidget *widget,gint *minimum,gint *natural) {
 GtkWidget *child=gtk_bin_get_child(GTK_BIN(widget));*minimum=*natural=0;if(child && gtk_widget_get_visible(child)) gtk_widget_get_preferred_height(child,minimum,natural);if(g_object_get_data(G_OBJECT(widget),"insight-collapse-height")) *minimum=1;*natural=MAX(*minimum,*natural);
}
static void width_host_width_for_height(GtkWidget *widget,gint height,gint *minimum,gint *natural) {
 GtkWidget *child=gtk_bin_get_child(GTK_BIN(widget));gint ignored=0;*minimum=1;*natural=1;if(child && gtk_widget_get_visible(child)) { gint child_height=0;if(g_object_get_data(G_OBJECT(widget),"insight-collapse-height")) gtk_widget_get_preferred_height(child,&child_height,NULL);gtk_widget_get_preferred_width_for_height(child,MAX(height,child_height),&ignored,natural); }*natural=MAX(*minimum,*natural);
}
static void width_host_height_for_width(GtkWidget *widget,gint width,gint *minimum,gint *natural) {
 GtkWidget *child=gtk_bin_get_child(GTK_BIN(widget));*minimum=*natural=0;if(child && gtk_widget_get_visible(child)) { gint child_width=1;gtk_widget_get_preferred_width(child,&child_width,NULL);gtk_widget_get_preferred_height_for_width(child,MAX(width,child_width),minimum,natural); }if(g_object_get_data(G_OBJECT(widget),"insight-collapse-height")) *minimum=1;*natural=MAX(*minimum,*natural);
}
static void width_host_allocate(GtkWidget *widget,GtkAllocation *allocation) {
 gtk_widget_set_allocation(widget,allocation);GtkWidget *child=gtk_bin_get_child(GTK_BIN(widget));if(child && gtk_widget_get_visible(child)) gtk_widget_size_allocate(child,allocation);
}
static void insight_width_host_class_init(InsightWidthHostClass *klass) {
 GtkWidgetClass *widget=GTK_WIDGET_CLASS(klass);widget->get_preferred_width=width_host_width;widget->get_preferred_height=width_host_height;widget->get_preferred_width_for_height=width_host_width_for_height;widget->get_preferred_height_for_width=width_host_height_for_width;widget->size_allocate=width_host_allocate;
}
static void insight_width_host_init(InsightWidthHost *host) { gtk_widget_set_has_window(GTK_WIDGET(host),FALSE);gtk_widget_set_name(GTK_WIDGET(host),"insight-editor-width-host"); }
static GtkWidget *narrow_panel_host(GtkWidget *child) {
 GtkWidget *host=g_object_new(insight_width_host_get_type(),NULL);gtk_widget_set_name(host,"insight-panel-width-host");GtkWidget *viewport=gtk_viewport_new(NULL,NULL);gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport),GTK_SHADOW_NONE);gtk_container_add(GTK_CONTAINER(viewport),child);gtk_container_add(GTK_CONTAINER(host),viewport);return host;
}
/* Give each row its own clipping boundary: a wide checkbox must not force
 * an unrelated entry/button row beyond the shelf's visible edge. */
static void constrain_panel_rows(GtkWidget *widget) {
 if(g_object_get_data(G_OBJECT(widget),"insight-rows-constrained")) return;
 gboolean vertical=GTK_IS_BOX(widget) && gtk_orientable_get_orientation(GTK_ORIENTABLE(widget))==GTK_ORIENTATION_VERTICAL;
 if(!vertical && !GTK_IS_STACK(widget)) return;
 g_object_set_data(G_OBJECT(widget),"insight-rows-constrained",GINT_TO_POINTER(1));
 GList *children=gtk_container_get_children(GTK_CONTAINER(widget));
 for(GList *p=children;p;p=p->next) {
  GtkWidget *child=p->data;
  gboolean nested=GTK_IS_STACK(child) || (GTK_IS_BOX(child) && gtk_orientable_get_orientation(GTK_ORIENTABLE(child))==GTK_ORIENTATION_VERTICAL);
  if(nested) constrain_panel_rows(child);
  else if(vertical && !GTK_IS_SCROLLED_WINDOW(child)) {
   gboolean expand,fill;guint padding;GtkPackType pack;gint position;
   gtk_box_query_child_packing(GTK_BOX(widget),child,&expand,&fill,&padding,&pack);
   gtk_container_child_get(GTK_CONTAINER(widget),child,"position",&position,NULL);
   g_object_ref(child);gtk_container_remove(GTK_CONTAINER(widget),child);
   GtkWidget *host=narrow_panel_host(child);
   if(pack==GTK_PACK_START) gtk_box_pack_start(GTK_BOX(widget),host,expand,fill,padding);else gtk_box_pack_end(GTK_BOX(widget),host,expand,fill,padding);
   gtk_box_reorder_child(GTK_BOX(widget),host,position);g_object_unref(child);gtk_widget_show_all(host);
  }
 }
 g_list_free(children);
}
static void constrain_message_width(GtkWidget *page) {
 if(!GTK_IS_BOX(page) || g_object_get_data(G_OBJECT(page),"insight-width-constrained")) return;
 GtkWidget *body=gtk_box_new(gtk_orientable_get_orientation(GTK_ORIENTABLE(page)),gtk_box_get_spacing(GTK_BOX(page))),*host=g_object_new(insight_width_host_get_type(),NULL);
 gtk_widget_set_name(host,"insight-message-width-host");gtk_box_set_homogeneous(GTK_BOX(body),gtk_box_get_homogeneous(GTK_BOX(page)));
 GList *children=gtk_container_get_children(GTK_CONTAINER(page));
 for(GList *p=children;p;p=p->next) { GtkWidget *child=p->data;gboolean expand,fill;guint padding;GtkPackType pack;gtk_box_query_child_packing(GTK_BOX(page),child,&expand,&fill,&padding,&pack);g_object_ref(child);gtk_container_remove(GTK_CONTAINER(page),child);if(pack==GTK_PACK_START) gtk_box_pack_start(GTK_BOX(body),child,expand,fill,padding);else gtk_box_pack_end(GTK_BOX(body),child,expand,fill,padding);g_object_unref(child); }
 g_list_free(children);panel_scroll_minimum(body);GtkWidget *viewport=gtk_viewport_new(NULL,NULL);gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport),GTK_SHADOW_NONE);gtk_container_add(GTK_CONTAINER(viewport),body);gtk_container_add(GTK_CONTAINER(host),viewport);gtk_widget_show(viewport);gtk_box_pack_start(GTK_BOX(page),host,TRUE,TRUE,0);g_object_set_data(G_OBJECT(page),"insight-width-constrained",GINT_TO_POINTER(1));gtk_widget_show(body);gtk_widget_show(host);
}
void insight_workbench_init(void) {
 insight_diff_set_source_opener(diff_source_open);insight_diff_set_colorizer(insight_diff_native_colorize);
 insight_directory_set_types_provider(directory_known_types);insight_directory_set_open_guard(directory_path_open);
 wb.main_geometry_handlers[0]=g_signal_connect(main_widgets.window,"configure-event",G_CALLBACK(main_geometry_changed),NULL);
 wb.main_geometry_handlers[1]=g_signal_connect(main_widgets.window,"window-state-event",G_CALLBACK(main_geometry_changed),NULL);
 for(guint i=0;i<10;i++) wb.hosts[i].opacity=100;
 wb.alive=TRUE;wb.tray_enabled=TRUE;wb.marks_preserve=TRUE; workbench_epoch++; wb.symbol_key_signal=g_signal_connect(geany_object,"key-press",G_CALLBACK(symbol_key_global),NULL);insight_window_list_init(window_list_preview);
 GtkWidget *existing=ui_lookup_widget(main_widgets.window,"vpaned1");
 GtkWidget *left_split=ui_lookup_widget(main_widgets.window,"hpaned1");gtk_container_child_set(GTK_CONTAINER(left_split),gtk_paned_get_child1(GTK_PANED(left_split)),"resize",FALSE,"shrink",TRUE,NULL);gtk_notebook_set_scrollable(GTK_NOTEBOOK(main_widgets.sidebar_notebook),TRUE);for(gint i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(main_widgets.sidebar_notebook));i++) { GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),i),*label=gtk_notebook_get_tab_label(GTK_NOTEBOOK(main_widgets.sidebar_notebook),page);if(GTK_IS_LABEL(label)) { gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_END);gtk_label_set_width_chars(GTK_LABEL(label),1); } }
 GtkWidget *sidebar=gtk_paned_get_child1(GTK_PANED(left_split));g_object_ref(sidebar);gtk_container_remove(GTK_CONTAINER(left_split),sidebar);GtkWidget *sidebar_host=narrow_panel_host(sidebar);gtk_widget_set_name(sidebar_host,"insight-sidebar-width-host");gtk_paned_pack1(GTK_PANED(left_split),sidebar_host,FALSE,TRUE);g_object_bind_property(sidebar,"visible",sidebar_host,"visible",G_BINDING_SYNC_CREATE);g_object_unref(sidebar);
 GtkWidget *parent=gtk_widget_get_parent(existing); gint position=0;
 gtk_container_child_get(GTK_CONTAINER(parent),existing,"position",&position,NULL);
 wb.usage_paths=g_ptr_array_new_with_free_func(location_free);
 wb.info_parse_local=TRUE;wb.context_visible=TRUE; wb.prefs_path=g_build_filename(app->configdir,"insight","workbench.conf",NULL);
 wb.watch=insight_workspace_watch_new(1500,workspace_changed,NULL);
 wb.membership=insight_project_files_new();wb.master_sync=insight_master_sync_new(3000,master_membership_applied,master_sync_report,NULL);InsightMasterTypes *master_types=insight_languages_master_types();insight_master_sync_set_types(wb.master_sync,master_types);insight_master_types_free(master_types); wb.scan_members=g_ptr_array_new_with_free_func(g_free); wb.scan_files=g_ptr_array_new_with_free_func(g_free); wb.scan_trees=g_ptr_array_new_with_free_func(g_free); wb.scan_seen=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 wb.frames=g_ptr_array_new_with_free_func(frame_free); wb.files=g_ptr_array_new_with_free_func(g_free);
 wb.declarations=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,outline_free);wb.base=insight_index_new(); wb.index=insight_index_new(); mark_index_dirty();
 wb.file_stamps=insight_project_db_copy_table(NULL);wb.directory_stamps=insight_project_db_copy_table(NULL);wb.disk_calls=insight_index_new();wb.disk_declarations=insight_index_new();
 wb.definitions=g_ptr_array_new_with_free_func(location_free);
 wb.history=g_ptr_array_new_with_free_func(location_free); wb.history_position=-1;
 wb.project_notebook=gtk_notebook_new(); g_signal_connect(wb.project_notebook,"switch-page",G_CALLBACK(project_panel_changed),NULL); gtk_notebook_set_scrollable(GTK_NOTEBOOK(wb.project_notebook),TRUE);
 create_browser(); create_files_browser(); create_project_tree(); create_usage_browser(); create_categories(); create_file_types(); create_notes(); create_local_browser();
 wb.pane=gtk_paned_new(GTK_ORIENTATION_VERTICAL);
 wb.box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
 wb.title=gtk_label_new("");gtk_widget_set_name(wb.title,"insight-context-location");gtk_label_set_selectable(GTK_LABEL(wb.title),TRUE);g_signal_connect(wb.title,"populate-popup",G_CALLBACK(context_location_popup),NULL);
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

 gtk_box_pack_start(GTK_BOX(header),wb.title,TRUE,TRUE,4);gtk_label_set_ellipsize(GTK_LABEL(wb.title),PANGO_ELLIPSIZE_MIDDLE);
 wb.follow=gtk_toggle_button_new();
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),TRUE);
 g_signal_connect(wb.follow,"toggled",G_CALLBACK(follow_changed),NULL);

 wb.wrap=gtk_toggle_button_new();
 g_signal_connect(wb.wrap,"toggled",G_CALLBACK(wrap_changed),NULL);

 GtkWidget *copy=gtk_button_new_with_label("Copy location");
 gtk_widget_set_tooltip_text(copy,"Copy the preview caret address as file:line");
 g_signal_connect(copy,"clicked",G_CALLBACK(copy_location),NULL);

 g_signal_connect(open,"clicked",G_CALLBACK(open_context),NULL);
 gtk_box_pack_start(GTK_BOX(wb.box),header,FALSE,FALSE,2);
 wb.choices=gtk_combo_box_text_new();
 g_signal_connect(wb.choices,"changed",G_CALLBACK(definition_changed),NULL);
 GtkWidget *definitions_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*definitions_label=gtk_label_new("Definitions:");gtk_widget_set_name(definitions_label,"insight-context-definitions-label");gtk_widget_set_name(wb.choices,"insight-context-definitions");gtk_widget_set_tooltip_text(wb.choices,"Indexed definitions compatible with the source language");gtk_box_pack_start(GTK_BOX(definitions_row),definitions_label,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(definitions_row),wb.choices,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(wb.box),definitions_row,FALSE,FALSE,0);
 wb.context_definitions_row=definitions_row;gtk_widget_set_name(definitions_row,"insight-context-definitions-row");gtk_widget_show_all(definitions_row);gtk_widget_set_no_show_all(definitions_row,TRUE);gtk_widget_hide(definitions_row);
 create_context_find();
 wb.preview=SCINTILLA(scintilla_new()); gtk_widget_set_name(GTK_WIDGET(wb.preview),"insight-context-code");
 sci_set_codepage(wb.preview,SC_CP_UTF8);
 SSM(wb.preview,SCI_SETUNDOCOLLECTION,0,0);
 SSM(wb.preview,SCI_SETMARGINWIDTHN,0,0); SSM(wb.preview,SCI_SETMARGINWIDTHN,2,0);
 SSM(wb.preview,SCI_SETWRAPMODE,SC_WRAP_NONE,0);
 gtk_widget_set_direction(GTK_WIDGET(wb.preview),GTK_TEXT_DIR_LTR);
 g_signal_connect(wb.preview,"button-press-event",G_CALLBACK(preview_button),NULL);
 g_signal_connect(wb.preview,"motion-notify-event",G_CALLBACK(preview_motion),NULL);
 g_signal_connect(wb.preview,"sci-notify",G_CALLBACK(preview_notify),NULL);
 g_signal_connect(wb.preview,"key-press-event",G_CALLBACK(preview_key),NULL);
 gtk_widget_set_tooltip_text(GTK_WIDGET(wb.preview),"Single click / F12: definition in Context; double click / Enter: open this line in editor");
 GtkWidget *content=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0),*rail=gtk_box_new(GTK_ORIENTATION_VERTICAL,0),*rail_scroll=gtk_scrolled_window_new(NULL,NULL);
 g_object_set_data(G_OBJECT(wb.box),"insight-context-rail",rail);gtk_widget_set_name(rail,"insight-context-tools");gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(rail_scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);gtk_container_add(GTK_CONTAINER(rail_scroll),rail);
 GtkWidget *find_toggle=gtk_toggle_button_new(),*definitions_toggle=gtk_toggle_button_new();wb.context_find_toggle=find_toggle;
 context_icon(back,"go-previous-symbolic","Previous Context location","insight-context-back");context_icon(forward,"go-next-symbolic","Next Context location","insight-context-forward");
 context_icon(open,"document-open-symbolic","Open the preview caret line in the editor (Enter)","insight-context-open");
 context_icon(wb.follow,"find-location-symbolic","Follow cursor — update Context from the editor caret","insight-context-follow");
 context_icon(wb.wrap,"format-justify-fill-symbolic","Wrap long lines in Context","insight-context-wrap");context_icon(copy,"edit-copy-symbolic","Copy the preview caret address as file:line","insight-context-copy");
 context_icon(find_toggle,"edit-find-symbolic","Show or hide Context search (Ctrl+F)","insight-context-search-toggle");
 context_icon(definitions_toggle,"view-list-symbolic","Show or hide indexed definition candidates","insight-context-definitions-toggle");
 g_signal_connect(find_toggle,"toggled",G_CALLBACK(context_tools_toggle),wb.context_find_row);g_signal_connect(definitions_toggle,"toggled",G_CALLBACK(context_tools_toggle),definitions_row);
 GtkWidget *commands[]={back,forward,open,find_toggle,definitions_toggle,wb.follow,wb.wrap,copy};for(guint i=0;i<G_N_ELEMENTS(commands);i++) gtk_box_pack_start(GTK_BOX(rail),commands[i],FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(rail),insight_panel_help_button("panel-context.md"),FALSE,FALSE,0);
 /* Metadata and preview share the column to the left of the tool rail. */
 GtkWidget *source_column=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
 GtkWidget *metadata[]={header,definitions_row,wb.context_find_row};
 for(guint i=0;i<G_N_ELEMENTS(metadata);i++) { g_object_ref(metadata[i]);gtk_container_remove(GTK_CONTAINER(wb.box),metadata[i]);gtk_box_pack_start(GTK_BOX(source_column),metadata[i],FALSE,FALSE,0);g_object_unref(metadata[i]); }
 GList *choice_cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(wb.choices));for(GList *p=choice_cells;p;p=p->next) if(GTK_IS_CELL_RENDERER_TEXT(p->data)) g_object_set(p->data,"ellipsize",PANGO_ELLIPSIZE_MIDDLE,"width-chars",12,NULL);g_list_free(choice_cells);
 gtk_box_pack_start(GTK_BOX(source_column),GTK_WIDGET(wb.preview),TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(content),source_column,TRUE,TRUE,0);gtk_box_pack_end(GTK_BOX(content),rail_scroll,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(wb.box),content,TRUE,TRUE,0);
 gtk_widget_set_size_request(wb.box,-1,0);
 /* The two side shelves surround the entire editor/Context column.
  * Keep the original split widgets so saved widths and Context height work. */
 GtkWidget *editor_book=gtk_paned_get_child2(GTK_PANED(left_split));
 g_object_ref(existing);g_object_ref(left_split);g_object_ref(editor_book);
 gtk_container_remove(GTK_CONTAINER(parent),existing);
 gtk_container_remove(GTK_CONTAINER(existing),left_split);
 gtk_container_remove(GTK_CONTAINER(left_split),editor_book);
 gtk_paned_pack1(GTK_PANED(existing),editor_book,TRUE,TRUE);
 GtkWidget *source_width_host=g_object_new(insight_width_host_get_type(),NULL);gtk_widget_set_name(source_width_host,"insight-source-width-host");gtk_container_add(GTK_CONTAINER(source_width_host),existing);gtk_paned_pack2(GTK_PANED(left_split),source_width_host,TRUE,TRUE);
 /* Clip the message notebook at its native minimum instead of allocating
  * its tabs/scrollbars an invalid height when the bottom shelf is collapsed. */
 GtkWidget *messages=gtk_paned_get_child2(GTK_PANED(existing));g_object_ref(messages);gtk_container_remove(GTK_CONTAINER(existing),messages);
 GtkWidget *message_host=narrow_panel_host(messages);gtk_widget_set_name(message_host,"insight-message-height-host");g_object_set_data(G_OBJECT(message_host),"insight-collapse-height",GINT_TO_POINTER(1));
 gtk_paned_pack2(GTK_PANED(existing),message_host,FALSE,TRUE);g_object_bind_property(messages,"visible",message_host,"visible",G_BINDING_SYNC_CREATE);gtk_widget_show(message_host);g_object_unref(messages);
 g_object_unref(editor_book);g_object_unref(existing);
 wb.right_pane=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
 /* Wide editor/message controls must not collapse the Project pane. */
 GtkWidget *width_host=g_object_new(insight_width_host_get_type(),NULL);gtk_container_add(GTK_CONTAINER(width_host),left_split);gtk_paned_pack1(GTK_PANED(wb.right_pane),width_host,TRUE,TRUE);g_object_unref(left_split);
 wb.project_host=narrow_panel_host(wb.project_notebook);gtk_widget_set_name(wb.project_host,"insight-project-width-host");g_object_bind_property(wb.project_notebook,"visible",wb.project_host,"visible",G_BINDING_SYNC_CREATE);gtk_paned_pack2(GTK_PANED(wb.right_pane),wb.project_host,FALSE,TRUE);
 gtk_widget_set_size_request(wb.project_notebook,0,-1);
 gtk_widget_set_name(wb.right_pane,"insight-project-split"); gtk_widget_set_name(wb.project_notebook,"insight-project-notebook");
 wb.initial_right_split=-2;g_signal_connect(wb.right_pane,"size-allocate",G_CALLBACK(project_split_allocated),NULL);gtk_paned_set_position(GTK_PANED(wb.right_pane),850);
 gtk_paned_pack1(GTK_PANED(wb.pane),wb.right_pane,TRUE,FALSE);
 gtk_notebook_append_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),wb.box,gtk_label_new("Context")); gtk_widget_show_all(wb.box);
 gtk_box_pack_start(GTK_BOX(parent),wb.pane,TRUE,TRUE,0);
 gtk_box_reorder_child(GTK_BOX(parent),wb.pane,position);
 gtk_paned_set_position(GTK_PANED(wb.pane),430);
 gtk_widget_show_all(wb.pane);
 create_search_toolbars();
 const gchar *list_titles[]={"File Symbols","Project Symbols","Project Files","Relations","Relations tree"};
 GtkWidget *list_views[]={wb.local_view,wb.symbol_view,wb.file_view,wb.usage_view,GTK_WIDGET(insight_relation_tree_view(wb.relation_tree))};for(guint i=0;i<G_N_ELEMENTS(list_views);i++) { insight_list_tools_attach(GTK_TREE_VIEW(list_views[i]));g_object_set_data(G_OBJECT(list_views[i]),"insight-list-title",(gpointer)list_titles[i]); }
 restore_workbench_settings(); restore_search_settings();
 create_native_lua();
 const char *signals[]={"document-open","document-save","document-activate","document-close"};
 const char *events[]={"document_open","document_save","document_activate","document_close"};
 for(guint i=0;i<4;i++) wb.event_handlers[i]=g_signal_connect_after(geany_object,signals[i],G_CALLBACK(native_document_event),(gpointer)events[i]);
 g_object_set_data(G_OBJECT(main_widgets.window),"insight-editor-menu",main_widgets.editor_menu);GtkWidget *language_binding=gtk_menu_item_new_with_label("File language / profile…");gtk_widget_set_name(language_binding,"insight-language-binding-command");g_signal_connect(language_binding,"activate",G_CALLBACK(insight_languages_show),GINT_TO_POINTER(1));gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.editor_menu),language_binding);gtk_widget_show(language_binding);GtkWidget *filetype_options=gtk_menu_item_new_with_label("File Type Options…");gtk_widget_set_name(filetype_options,"insight-filetype-options-command");g_signal_connect(filetype_options,"activate",G_CALLBACK(insight_languages_show),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.editor_menu),filetype_options);gtk_widget_show(filetype_options);create_workbench_menus(); arrange_main_menus(); create_browse_toolbar(); create_help_menu();insight_menu_icons(ui_lookup_widget(main_widgets.window,"menubar1"));insight_menu_icons(main_widgets.editor_menu);register_panels();
 insight_remote_ui_init(remote_workspace);
 wb.project_handlers[0]=g_signal_connect_after(geany_object,"project-open",G_CALLBACK(native_project_update),GINT_TO_POINTER(1));
 wb.project_handlers[1]=g_signal_connect_after(geany_object,"project-save",G_CALLBACK(native_project_update),NULL);
 wb.project_handlers[2]=g_signal_connect_after(geany_object,"project-close",G_CALLBACK(native_project_close),NULL);
 native_project_update(NULL,NULL,GINT_TO_POINTER(1));
 if(wb.restore_float) { gboolean visible=gtk_widget_get_visible(wb.project_notebook); float_project(NULL,NULL); gtk_widget_set_visible(wb.project_notebook,visible); gtk_widget_set_visible(wb.float_window,visible); }
 InsightLayout session_groups={0};
 for(guint i=0;i<wb.host_count;i++) { PanelHost *host=&wb.hosts[i];session_groups.groups[i]=host->saved_group;session_groups.group_order[i]=host->saved_group_order;session_groups.group_active[i]=host->saved_group_active;session_groups.opacity[i]=host->opacity;if(host->restore) { session_groups.values[IL_DETACHED]|=1<<i;if(host->minimized) session_groups.values[IL_HOST_MINIMIZED]|=1<<i;else if(host->visible) session_groups.values[IL_HOST_VISIBLE]|=1<<i; } }
 if(!insight_layout_groups_valid(&session_groups)) { memset(session_groups.groups,0,sizeof session_groups.groups);memset(session_groups.group_order,0,sizeof session_groups.group_order);memset(session_groups.group_active,0,sizeof session_groups.group_active); }
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].restore) detach_panel(i);
 panel_groups_apply(&session_groups);
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window) { if(session_groups.values[IL_HOST_MINIMIZED]&(1<<i)) panel_minimize(i);else gtk_widget_set_visible(wb.hosts[i].window,(session_groups.values[IL_HOST_VISIBLE]&(1<<i))!=0); }
 GtkWidget *side_books[]={main_widgets.sidebar_notebook,wb.project_notebook};
 for(guint i=0;i<G_N_ELEMENTS(side_books);i++) {GtkWidget *tabs=insight_side_tabs_wrap(GTK_NOTEBOOK(side_books[i]),insight_tr);if(tabs) insight_i18n_on_refresh(G_OBJECT(tabs),insight_side_tabs_refresh,tabs);}
 insight_workbench_set_independent(insight_appearance_independent());
 if(wb.root && !wb.scan && !wb.report_scan_completed) start_scan(wb.root);
 /* Notebook stack minima include inactive pages as well as Context. Keep
  * auxiliary rows from forcing the entire source workspace wider than its slot. */
 for(gint i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(main_widgets.message_window_notebook));i++) constrain_message_width(gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),i));
 constrain_message_width(wb.box);
 g_signal_connect(wb.right_pane,"notify::position",G_CALLBACK(panel_position_guard),NULL);g_signal_connect(ui_lookup_widget(main_widgets.window,"hpaned1"),"notify::position",G_CALLBACK(panel_position_guard),NULL);g_signal_connect(existing,"notify::position",G_CALLBACK(panel_position_guard),NULL);
 ui_sidebar_show_hide();
 if(ui_prefs.msgwindow_visible && !g_file_test(wb.prefs_path,G_FILE_TEST_EXISTS)) reveal_context();
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
 palette_restore_now();
 if(palette.view_timer) {g_source_remove(palette.view_timer);palette.view_timer=0;}g_clear_pointer(&palette.views,g_ptr_array_unref);
 if(wb.main_apply_source) { g_source_remove(wb.main_apply_source);wb.main_apply_source=0; }
 for(guint i=0;i<G_N_ELEMENTS(wb.main_geometry_handlers);i++) if(wb.main_geometry_handlers[i]) { g_signal_handler_disconnect(main_widgets.window,wb.main_geometry_handlers[i]);wb.main_geometry_handlers[i]=0; }
 if(wb.symbol_key_signal) { g_signal_handler_disconnect(geany_object,wb.symbol_key_signal); wb.symbol_key_signal=0; }
 if(symbol_info) gtk_widget_destroy(symbol_info->window);
 insight_window_list_finalize();
 insight_search_links_free(wb.search_links);wb.search_links=NULL;
 insight_workbench_bookmarks_changed();
 if(wb.bookmark_sync_timer) { g_source_remove(wb.bookmark_sync_timer);wb.bookmark_sync_timer=0; }
 if(wb.bookmark_window) gtk_widget_destroy(wb.bookmark_window);
 g_clear_pointer(&wb.bookmark_handles,g_hash_table_destroy);
 wb.alive=FALSE;if(wb.type_panel.timer) { g_source_remove(wb.type_panel.timer);wb.type_panel.timer=0; }if(wb.category_panel.timer) { g_source_remove(wb.category_panel.timer);wb.category_panel.timer=0; }if(wb.tree_filter_idle) { g_source_remove(wb.tree_filter_idle);wb.tree_filter_idle=0; }if(wb.file_event_idle) { g_source_remove(wb.file_event_idle);wb.file_event_idle=0; } if(wb.preview_click_timer) { g_source_remove(wb.preview_click_timer);wb.preview_click_timer=0; } stop_definition(); stop_file_preview(); mark_index_dirty();
 insight_remote_ui_finalize();
 save_search_settings(); save_workbench_settings();bookmark_session_finish(); dock_all_panels(); dock_project(NULL,NULL);
 insight_disk_browser_shutdown(wb.disk_browser);
 for(guint i=0;i<wb.host_count;i++) g_free(wb.hosts[i].label);
 for(guint i=0;i<3;i++) if(wb.project_handlers[i]) g_signal_handler_disconnect(geany_object,wb.project_handlers[i]);
 g_free(wb.geany_root);
 for(guint i=0;i<4;i++) if(wb.event_handlers[i]) g_signal_handler_disconnect(geany_object,wb.event_handlers[i]);
 if(wb.lua_accels) { remove_group_accelerators(main_widgets.window,wb.lua_accels); gtk_window_remove_accel_group(GTK_WINDOW(main_widgets.window),wb.lua_accels); g_object_unref(wb.lua_accels); }
 if(wb.navigation_accels) { remove_group_accelerators(main_widgets.window,wb.navigation_accels); gtk_window_remove_accel_group(GTK_WINDOW(main_widgets.window),wb.navigation_accels); g_object_unref(wb.navigation_accels); }
 if(wb.css) { gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(wb.css)); g_object_unref(wb.css); }
 insight_extensions_free(wb.extensions);
 if(wb.refresh) g_source_remove(wb.refresh);
 if(wb.panel_idle) g_source_remove(wb.panel_idle);
 if(wb.index_event_idle) g_source_remove(wb.index_event_idle);
 g_clear_pointer(&wb.index_event_paths,g_hash_table_destroy);
 g_clear_pointer(&wb.index_event_names,g_hash_table_destroy);
 if(wb.symbols) symbol_list_stop(symbol_list_state(wb.symbols));
 if(wb.file_store) symbol_list_stop(symbol_list_state(wb.file_store));
 if(wb.local_symbols) symbol_list_stop(symbol_list_state(wb.local_symbols));
 g_free(wb.prefs_path);g_free(wb.project_page_root);g_free(wb.project_page_profile);
 stop_usage(); g_ptr_array_free(wb.usage_paths,TRUE);
 g_free(wb.search_state_path); g_ptr_array_free(wb.search_files,TRUE); g_ptr_array_free(wb.search_history,TRUE); g_object_unref(wb.search_matches);
 if(wb.watch_idle) g_source_remove(wb.watch_idle);
 insight_workspace_watch_free(wb.watch);insight_master_sync_free(wb.master_sync);wb.master_sync=NULL;
 stop_scan(); g_ptr_array_free(wb.scan_members,TRUE); insight_project_files_free(wb.membership); g_free(wb.membership_project); g_ptr_array_free(wb.scan_files,TRUE); g_ptr_array_free(wb.scan_trees,TRUE); g_hash_table_destroy(wb.scan_seen); g_ptr_array_free(wb.frames,TRUE); g_ptr_array_free(wb.files,TRUE); g_free(wb.root); g_free(wb.cache_path);g_free(wb.db_identity);g_free(wb.db_policy);g_hash_table_unref(wb.file_stamps);g_hash_table_unref(wb.directory_stamps);retire_index(wb.disk_calls);retire_index(wb.disk_declarations);retire_index(wb.scan_backup);
 g_free(wb.editor_find_query); g_free(wb.editor_find_digest);
 g_free(wb.follow_word); g_free(wb.relation_word);
 g_free(wb.source_path); g_free(wb.source_digest); g_free(wb.find_last_query); g_free(wb.find_last_digest);
 if(wb.preview_links_idle) { g_source_remove(wb.preview_links_idle); wb.preview_links_idle=0; }
 preview_scopes_clear();g_clear_pointer(&wb.preview_links_text,g_free);g_clear_pointer(&wb.preview_code_mask,g_free);g_clear_pointer(&wb.preview_links_path,g_free); g_clear_pointer(&wb.preview_names,g_hash_table_destroy);g_clear_pointer(&wb.preview_candidates,g_hash_table_destroy);
 g_hash_table_unref(wb.declarations);insight_index_free(wb.base); insight_index_free(wb.index);
 if(wb.symbols) g_object_unref(wb.symbols);
 if(wb.local_symbols) g_object_unref(wb.local_symbols);
 if(wb.project_tree) g_object_unref(wb.project_tree);
 if(wb.categories) g_object_unref(wb.categories);
 if(wb.file_types) g_object_unref(wb.file_types);
 if(wb.marks_store) g_object_unref(wb.marks_store);
 if(wb.clips_store) g_object_unref(wb.clips_store);
 g_clear_pointer(&wb.marks_session_stores,g_hash_table_destroy);insight_notes_free(wb.project_marks);g_free(wb.project_marks_path);g_free(wb.project_marks_root);insight_notes_free(wb.notes);insight_notes_free(wb.project_notes);g_free(wb.project_notes_path);g_free(wb.template_prefix); g_free(wb.notes_path); g_free(wb.clips_language);
 if(wb.file_store) g_object_unref(wb.file_store);
 if(wb.relation_history) g_ptr_array_unref(wb.relation_history);
 if(wb.relation_future) g_ptr_array_unref(wb.relation_future);
 if(wb.matches) g_object_unref(wb.matches);
 if(wb.history) g_ptr_array_free(wb.history,TRUE);
 if(wb.definitions) g_ptr_array_free(wb.definitions,TRUE);
 insight_languages_finalize();insight_i18n_finalize();memset(&wb,0,sizeof wb);
}

void insight_workbench_languages_changed(void) { if(!wb.alive) return;wb.preview_styles_ready=FALSE;wb.preview_links_valid=FALSE;InsightMasterTypes *types=insight_languages_master_types();insight_master_sync_set_types(wb.master_sync,types);insight_master_types_free(types);stop_definition();wb.follow_doc=0;if(wb.root) { wb.force_parse=TRUE;gchar *path=g_strdup(wb.root);start_scan(path);g_free(path); }else mark_index_dirty();stop_usage();browser_filter(NULL,NULL);files_filter(NULL,NULL);categories_filter(NULL,NULL);preview_links();insight_workbench_refresh(); }

void insight_workbench_locale_changed(void) { if(!wb.alive) return;for(guint i=0;i<wb.host_count;i++) { panel_controls_update(&wb.hosts[i]);if(panel_window_owner(wb.hosts[i].window)==i) panel_window_refresh(wb.hosts[i].window); }if(wb.disk_browser) insight_disk_browser_locale_changed(wb.disk_browser);if(wb.relation_tree) insight_relation_tree_locale_changed(wb.relation_tree);if(wb.type_panel.status) grouped_count(&wb.type_panel,wb.type_panel.shown,wb.type_panel.total);if(wb.category_panel.status) { grouped_count(&wb.category_panel,wb.category_panel.shown,wb.category_panel.total);categories_filter(NULL,NULL); }if(wb.tree_status) folder_count(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wb.tree_status),"insight-folder-count")),GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wb.tree_status),"insight-folder-total")));if(wb.file_status) { gchar *text=insight_i18n_printf(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wb.file_status),"insight-files-more")) ? "Files: %u+" : "Files: %u",GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wb.file_status),"insight-files-count")));gtk_label_set_text(GTK_LABEL(wb.file_status),text);g_free(text); }}


gboolean insight_workbench_focus_file_symbols(void) {
 if(!wb.alive || !wb.local_entry) return FALSE;
 ui_prefs.sidebar_visible=TRUE;interface_prefs.sidebar_symbol_visible=TRUE;ui_sidebar_show_hide();focus_widget_panel(wb.local_entry);
 GtkWidget *target=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.local_mode))==0 ? gtk_bin_get_child(GTK_BIN(ui_lookup_widget(main_widgets.window,"scrolledwindow2"))) : wb.local_view;
 if(target && gtk_widget_get_sensitive(target)) { gtk_widget_grab_focus(target); }
 return TRUE;
}
