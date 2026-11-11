/* EN: Connects Insight tools, project panels, and debugger entry points to Geany.
 * RU: Подключает инструменты Insight, панели проекта и запуск отладчиков к Geany. */
#define INSIGHT_LOCALIZE_FORMATS
#include "insight-i18n.h"
#include "../insight/desktop/glib_compat.h"
#include "../insight/desktop/window_motion.h"
#include "insight-languages.h"
#include "insight-completion.h"
#include "insight-paste.h"
#include "insight-navigation.h"
#include "insight-revisions.h"
#include "insight-compare-ui.h"
#include "insight-custom-commands.h"
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
#include "SciLexer.h"
#include <pango/pangocairo.h>
#include <gdk/gdkkeysyms.h>
#include "insight-workbench.h"
#include "insight-python-debug.h"
#include "insight-vivado-project.h"
#include "insight-waveforms.h"
#include "insight-virtual-list.h"
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
#include "insight-source-routes.h"
#include "insight-clang.h"
#include "../insight/desktop/clang_backend.h"
#include "../insight/desktop/slang_completion.h"
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
#include "../insight/desktop/remote_io.h"
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
 GtkWidget *relation_options,*relation_routes,*relation_tree,*relation_tree_toggle,*relation_back,*relation_forward,*relation_stack,*relation_graph_toggle,*relation_graph,*relation_tree_graph;GPtrArray *relation_history,*relation_future;
 gchar *relation_word; gint relation_last_mode; gboolean relation_dirty,relation_setting,reference_setting;InsightSourceRegion usage_region;
 gint usage_mode; guint usage_flags;gchar *usage_usr;
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
 gulong project_handlers[3]; gchar *geany_root; gboolean workspace_from_project,git_manual;
 gchar *prefs_path;
 InsightExtensions *extensions; gboolean lua_running;
 GtkWidget *lua_menu,*lua_commands,*palette_item;
 GtkAccelGroup *lua_accels,*navigation_accels;
 GtkCssProvider *css; GtkWidget *context_toggle;
 gboolean alive; guint64 scan_generation,usage_generation,definition_generation; guint definition_timer; GCancellable *restore_worker,*scan_worker,*search_worker,*definition_worker,*snapshot_worker,*cache_worker,*preview_worker; guint64 preview_generation; guint64 index_generation,index_version; gint64 publish_due;
 InsightIndex *base,*index;
 gboolean info_parse_local,info_across_languages;gint info_scope;
 InsightProjectFiles *membership; gchar *membership_project; gboolean membership_valid;InsightMasterSync *master_sync;guint64 membership_generation;
 GPtrArray *frames,*files,*scan_files,*scan_trees,*scan_members; gboolean scan_keep_files,scan_indexing; guint scan_index_position; GHashTable *scan_seen; guint scan_file_position,scan_tree_position; gchar *root,*cache_path; gchar *db_identity,*db_policy,*restore_attempt_identity;GHashTable *file_stamps,*directory_stamps;InsightIndex *disk_calls,*disk_declarations,*scan_backup;gboolean force_parse,cache_validation;guint reused; guint scan,parsed,skipped;
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
static void relation_stack_changed(GObject *,GParamSpec *,gpointer);
static void select_project_tab(gint);
static gboolean start_scan(const char *);
static void hdl_schedule_colors(void);
static void hdl_invalidate_colors(void);
static void manage_project_files(GtkWidget *,gpointer);
static InsightReportSnapshot *capture_project_report(GError **);
static gboolean initial_membership_pending;

/* EN: Collect the first membership selection before scheduling indexing.
 * RU: Собирает начальный список файлов перед запуском индексации. */
void insight_workbench_manage_project_files(void)
{
 manage_project_files(NULL, NULL);
 if(initial_membership_pending && wb.workspace_from_project && wb.geany_root) {
  initial_membership_pending=FALSE;
  start_scan(wb.geany_root);
 }
}
static void project_files_drop(GtkWidget *,GdkDragContext *,gint,gint,GtkSelectionData *,guint,guint,gpointer);
static void workspace_changed(gpointer);
static void create_search_toolbars(void);
static guint text_search_flags(GtkWidget *,GtkWidget *,GtkWidget *);
static char *read_disk_source(const char *,gboolean);
enum { SP_OPEN,SP_INFO,SP_REFERENCES,SP_NAME,SP_LOCATION,SP_ONLY,SP_HIDE,SP_ALL,SP_GROUP,SP_EXPAND,SP_COLLAPSE,SP_HELP,SP_MORE,SP_LOCATE,SP_COLUMNS,SP_QUALIFIED,SP_PATH,SP_LINE,SP_FILTER_NAME,SP_FILTER_SCOPE,SP_FILTER_FILE,SP_CLEAR,SP_EXPAND_ONE,SP_COLLAPSE_ONE,SP_INVERT,SP_VISIBILITY,SP_COPY_TEXT,SP_CUT_TEXT,SP_DUPLICATE,SP_PASTE_TEXT,SP_COUNT };
enum { FP_OPEN,FP_NAME,FP_PATH,FP_FILTER,FP_CLEAR,FP_RESET,FP_COLUMNS,FP_HELP,FP_REVEAL,FP_COUNT };
#include "insight-workbench/workspace/panel-state.inc"
#include "insight-workbench/workspace/index-events.inc"
#include "insight-workbench/workspace/directory-watch.inc"
#include "insight-workbench/symbols/source-lookup.inc"
#include "insight-workbench/symbols/symbol-lists.inc"
#include "insight-workbench/files/file-actions.inc"
#include "insight-workbench/relations/relation-rows.inc"
#include "insight-workbench/relations/source-context.inc"
#include "insight-workbench/relations/relation-navigation.inc"
#include "insight-workbench/symbols/symbol-navigation.inc"
#include "insight-workbench/files/folder-model.inc"
#include "insight-workbench/files/grouped-panels.inc"
#include "insight-workbench/bookmarks/bookmark-list.inc"
#include "insight-workbench/bookmarks/bookmark-actions.inc"
#include "insight-workbench/bookmarks/bookmark-preferences.inc"
#include "insight-workbench/source/definition-preview.inc"
#include "insight-workbench/waveforms/dependency-tree.inc"
#include "insight-workbench/waveforms/source-actions.inc"
#include "insight-workbench/waveforms/wave-navigation.inc"
#include "insight-workbench/extensions/native-extensions.inc"
#include "insight-workbench/workspace/membership-and-syntax.inc"

#include "insight-debuggers/scripts/session.inc"
#include "insight-debuggers/scripts/watch-graph.inc"
#include "insight-debuggers/scripts/execution-results.inc"
#include "insight-debuggers/scripts/breakpoint-storage.inc"
#include "insight-debuggers/scripts/source-and-plain-points.inc"
#include "insight-debuggers/scripts/python-transport.inc"
#include "insight-debuggers/scripts/execution-controls.inc"
#include "insight-debuggers/scripts/breakpoint-controls.inc"
#include "insight-debuggers/scripts/source-inspection.inc"
#include "insight-debuggers/scripts/observer-history.inc"
#include "insight-debuggers/scripts/observer-editor.inc"
#include "insight-debuggers/scripts/window.inc"

#include "insight-debuggers/gdb-remote-state.inc"
#include "insight-debuggers/gdb-profile-location-state.inc"
#include "insight-debuggers/gdb-session.inc"
#include "insight-debuggers/gdb-watches.inc"
#include "insight-debuggers/gdb-context.inc"
#include "insight-debuggers/gdb-backtraces.inc"
#include "insight-debuggers/gdb-source-markers.inc"
#include "insight-debuggers/gdb-lifecycle.inc"
#include "insight-debuggers/gdb-breakpoints.inc"
#include "insight-debuggers/gdb-actions.inc"
#include "insight-debuggers/gdb-values-model.inc"
#include "insight-debuggers/gdb-values-ui.inc"
#include "insight-debuggers/gdb-data-actions.inc"
#include "insight-debuggers/gdb-memory-model.inc"
#include "insight-debuggers/gdb-memory-ui.inc"
#include "insight-debuggers/gdb-remote-model.inc"
#include "insight-debuggers/gdb-remote-process.inc"
#include "insight-debuggers/gdb-attach-model.inc"
#include "insight-debuggers/gdb-attach-ui.inc"
#include "insight-debuggers/gdb-remote-connection.inc"
#include "insight-debuggers/gdb-profile-debug-model.inc"
#include "insight-debuggers/gdb-profile-locations-model.inc"
#include "insight-debuggers/gdb-profile-locations-snapshot.inc"
#include "insight-debuggers/gdb-profile-locations-storage.inc"
#include "insight-debuggers/gdb-profile-model.inc"
#include "insight-debuggers/gdb-profile-debug-ui.inc"
#include "insight-debuggers/gdb-profile-ui.inc"
#include "insight-debuggers/gdb-remote-ui.inc"
#include "insight-debuggers/gdb-window.inc"

static GtkWidget *tool_submenu(const gchar *label)
{
    GtkWidget *item = gtk_menu_item_new_with_label(insight_tr(label));
    GtkWidget *menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.tools_menu), item);
    gtk_widget_show(item);
    return menu;
}

#include "insight-workbench/panels/tool-menus.inc"
#include "insight-workbench/panels/docking-and-palette.inc"
#include "insight-workbench/panels/layout-slots.inc"
#include "insight-workbench/panels/layout-files.inc"
#include "insight-workbench/symbols/symbol-info.inc"
#include "insight-workbench/workspace/git-and-project.inc"
#include "insight-workbench/search/search-controls.inc"
#include "insight-workbench/search/search-and-toolbar.inc"
#include "insight-workbench/panels/workbench-lifecycle.inc"
