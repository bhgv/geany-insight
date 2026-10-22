#define INSIGHT_LOCALIZE_FORMATS
#include "insight-i18n.h"
#include "insight-help.h"
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "insight-window-list.h"
#include "../insight/desktop/lookup_filter.h"
#include "document.h"
#include "dialogs.h"
#include "editor.h"
#include "main.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include "sciwrappers.h"
#include "ui_utils.h"
#include "tagmanager/tm_source_file.h"
#include "tagmanager/tm_tag.h"
#include <string.h>
enum { ID,NAME,PATH,STATE,LINES,SYMBOL,RECENT,ACTIVE,COLUMNS };
static struct {
 gboolean alive,refreshing,press_previewed,sort_syncing,busy;GtkWidget *window,*search,*view,*sort,*activate,*save,*close,*status,*save_as,*scope;GtkListStore *model;
 GBytes *snapshot;guint shown,total;GHashTable *recent;guint64 serial;guint timer;InsightWindowPreview preview;
 gchar *settings,*filter; gint x,y,width,height,column,direction,widths[6],order[6]; gboolean positioned,maximized,docked,moving; GtkWidget *content,*dock_page,*dock_toggle;
} list;
static gint bounded_integer(GKeyFile *key,const gchar *name,gint low,gint high,gint fallback) {
 GError *error=NULL; gint value=g_key_file_get_integer(key,"window-list",name,&error); gboolean valid=!error && value>=low && value<=high; g_clear_error(&error); return valid ? value : fallback;
}
static void settings_read(void) {
 list.width=850; list.height=430; list.column=RECENT; list.direction=GTK_SORT_DESCENDING; list.filter=g_strdup("");
 const gint widths[]={55,160,90,60,170,280}; for(guint i=0;i<6;i++) { list.widths[i]=widths[i]; list.order[i]=(gint)i; }
 list.settings=g_build_filename(app->configdir,"insight","window-list.conf",NULL); GKeyFile *key=g_key_file_new();
 if(g_key_file_load_from_file(key,list.settings,G_KEY_FILE_NONE,NULL) && bounded_integer(key,"version",1,1,0)==1) {
  list.width=bounded_integer(key,"width",400,4000,850); list.height=bounded_integer(key,"height",220,3000,430); list.x=bounded_integer(key,"x",-30000,30000,0); list.y=bounded_integer(key,"y",-30000,30000,0); list.positioned=g_key_file_get_boolean(key,"window-list","positioned",NULL); list.maximized=g_key_file_get_boolean(key,"window-list","maximized",NULL);
  list.docked=g_key_file_get_boolean(key,"window-list","docked",NULL);
  list.column=bounded_integer(key,"sort-column",NAME,ACTIVE,RECENT); list.direction=bounded_integer(key,"sort-direction",GTK_SORT_ASCENDING,GTK_SORT_DESCENDING,GTK_SORT_DESCENDING);
  gchar *filter=g_key_file_get_string(key,"window-list","filter",NULL); if(filter && strlen(filter)<=2048 && g_utf8_validate(filter,-1,NULL)) { g_free(list.filter); list.filter=filter; } else g_free(filter);
  gsize count=0; gint *sizes=g_key_file_get_integer_list(key,"window-list","column-widths",&count,NULL); if(sizes && count==6) for(guint i=0;i<6;i++) if(sizes[i]>=32 && sizes[i]<=2000) list.widths[i]=sizes[i]; g_free(sizes);
  gint *order=g_key_file_get_integer_list(key,"window-list","column-order",&count,NULL); guint mask=0; if(order && count==6) for(guint i=0;i<6;i++) { if(order[i]<0 || order[i]>=6 || (mask&(1u<<order[i]))) { mask=0; break; } mask|=1u<<order[i]; } if(mask==63) memcpy(list.order,order,sizeof list.order); g_free(order);
 }
 g_key_file_unref(key);
}
static void settings_capture(void) {
 if(!list.window || !list.view) return;
 GtkSortType direction; gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(list.model),&list.column,&direction); list.direction=direction;
 g_free(list.filter); list.filter=g_strdup(gtk_entry_get_text(GTK_ENTRY(list.search))); GList *columns=gtk_tree_view_get_columns(GTK_TREE_VIEW(list.view)); guint position=0;
 for(GList *p=columns;p && position<6;p=p->next,position++) { guint id=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(p->data),"insight-list-column"))-1; if(id>=6) continue; list.order[position]=(gint)id; gint width=gtk_tree_view_column_get_width(p->data); if(width>=32 && width<=2000) list.widths[id]=width; }
 g_list_free(columns);
}
static void settings_save(void) {
 if(!list.settings) return;
 settings_capture(); GKeyFile *key=g_key_file_new(); const gchar *group="window-list";
 g_key_file_set_boolean(key,group,"docked",list.docked); g_key_file_set_integer(key,group,"version",1); g_key_file_set_integer(key,group,"width",list.width); g_key_file_set_integer(key,group,"height",list.height); g_key_file_set_integer(key,group,"x",list.x); g_key_file_set_integer(key,group,"y",list.y); g_key_file_set_boolean(key,group,"positioned",list.positioned); g_key_file_set_boolean(key,group,"maximized",list.maximized); g_key_file_set_integer(key,group,"sort-column",list.column); g_key_file_set_integer(key,group,"sort-direction",list.direction); g_key_file_set_string(key,group,"filter",list.filter ? list.filter : ""); g_key_file_set_integer_list(key,group,"column-widths",list.widths,6); g_key_file_set_integer_list(key,group,"column-order",list.order,6);
 gsize size=0; gchar *text=g_key_file_to_data(key,&size,NULL),*directory=g_path_get_dirname(list.settings); GError *error=NULL;
 if(!g_mkdir_with_parents(directory,0700)) { GFile *file=g_file_new_for_path(list.settings); g_file_replace_contents(file,text,size,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,&error); g_object_unref(file); }
 else g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot create Window List settings directory");
 if(error && !main_status.quitting) ui_set_statusbar(TRUE,"Window List settings: %s",insight_tr(error->message));
 g_clear_error(&error); g_free(directory); g_free(text); g_key_file_unref(key);
}
static gboolean configured(GtkWidget *widget,GdkEventConfigure *event,gpointer data) {
 (void)data; if(list.docked || list.moving) return FALSE; GdkWindow *window=gtk_widget_get_window(widget); if(window && !(gdk_window_get_state(window)&(GDK_WINDOW_STATE_MAXIMIZED|GDK_WINDOW_STATE_FULLSCREEN|GDK_WINDOW_STATE_ICONIFIED))) { list.width=CLAMP(event->width,400,4000); list.height=CLAMP(event->height,220,3000); gtk_window_get_position(GTK_WINDOW(widget),&list.x,&list.y); list.positioned=TRUE; } return FALSE;
}
static gboolean window_state(GtkWidget *widget,GdkEventWindowState *event,gpointer data) { (void)widget; (void)data; if(!list.docked && !list.moving) list.maximized=(event->new_window_state&GDK_WINDOW_STATE_MAXIMIZED)!=0; return FALSE; }
static void hide_list(void) { settings_save(); if(list.docked && list.dock_page) gtk_widget_hide(list.dock_page); else if(list.window) gtk_widget_hide(list.window); }
static gboolean close_window(GtkWidget *widget,GdkEvent *event,gpointer data) { (void)widget; (void)event; (void)data; hide_list(); return TRUE; }
static void restore_geometry(void) {
 gint width=list.width,height=list.height,x=list.x,y=list.y; GdkDisplay *display=gtk_widget_get_display(main_widgets.window); GdkMonitor *monitor=list.positioned ? gdk_display_get_monitor_at_point(display,x,y) : gdk_display_get_primary_monitor(display);
 if(!monitor && gdk_display_get_n_monitors(display)) monitor=gdk_display_get_monitor(display,0);
 if(monitor) { GdkRectangle area; gdk_monitor_get_workarea(monitor,&area); width=MIN(width,MAX(1,area.width)); height=MIN(height,MAX(1,area.height)); x=CLAMP(x,area.x,area.x+MAX(0,area.width-width)); y=CLAMP(y,area.y,area.y+MAX(0,area.height-height)); }
 gtk_window_set_default_size(GTK_WINDOW(list.window),width,height); gtk_window_resize(GTK_WINDOW(list.window),width,height); if(list.positioned) gtk_window_move(GTK_WINDOW(list.window),x,y); if(list.maximized) gtk_window_maximize(GTK_WINDOW(list.window)); else gtk_window_unmaximize(GTK_WINDOW(list.window));
}
/* Capture IDs, never row paths or document pointers, across native dialogs. */
static GArray *selected_ids(void) {
 GArray *ids=g_array_new(FALSE,FALSE,sizeof(guint));if(!list.view) return ids;
 GtkTreeModel *model=NULL;GList *paths=gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(list.view)),&model);
 for(GList *p=paths;p;p=p->next) { GtkTreeIter iter;if(gtk_tree_model_get_iter(model,&iter,p->data)) { guint id=0;gtk_tree_model_get(model,&iter,ID,&id,-1);g_array_append_val(ids,id); } }
 g_list_free_full(paths,(GDestroyNotify)gtk_tree_path_free);return ids;
}
static GeanyDocument *selected_document(void) {
 GArray *ids=selected_ids();GeanyDocument *doc=ids->len==1 ? document_find_by_id(g_array_index(ids,guint,0)) : NULL;g_array_unref(ids);return doc;
}
static void selection_changed(GtkTreeSelection *selection,gpointer data) {
 (void)selection;(void)data;if(!list.alive || list.moving || !list.view) return;
 GArray *ids=selected_ids();gboolean writable=FALSE;for(guint i=0;i<ids->len;i++) { GeanyDocument *doc=document_find_by_id(g_array_index(ids,guint,i));if(DOC_VALID(doc) && !doc->readonly) writable=TRUE; }
 gtk_widget_set_sensitive(list.activate,!list.busy && ids->len==1);gtk_widget_set_sensitive(list.close,!list.busy && ids->len>0);gtk_widget_set_sensitive(list.save,!list.busy && writable);gtk_widget_set_sensitive(list.save_as,!list.busy && writable);
 gchar *status=g_strdup_printf("%u of %u open files • %u selected",list.shown,list.total,ids->len);gtk_label_set_text(GTK_LABEL(list.status),status);g_free(status);g_array_unref(ids);
 GeanyDocument *doc=selected_document();if(doc && !list.refreshing && !list.busy && list.preview) { list.preview(doc->id);list.press_previewed=TRUE; }
}
static gboolean button_press(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)widget;(void)data;if(event->button==1) list.press_previewed=FALSE;return FALSE;
}
static gboolean button_release(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)data;if(event->button!=1 || event->window!=gtk_tree_view_get_bin_window(GTK_TREE_VIEW(widget))) return FALSE;
 GtkTreePath *path=NULL;
 if(!list.press_previewed && gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),(gint)event->x,(gint)event->y,&path,NULL,NULL,NULL)) {
  GeanyDocument *doc=selected_document();if(doc && list.preview) list.preview(doc->id);gtk_tree_path_free(path);
 }
 return FALSE;
}
static gchar *nearest_symbol(GeanyDocument *doc) {
 TMTag *best=NULL;gulong line=(gulong)sci_get_current_line(doc->editor->sci)+1;
 if(doc->tm_file && doc->tm_file->tags_array) for(guint i=0;i<doc->tm_file->tags_array->len;i++) {
  TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,i);
  if(tag && tag->line && tag->line<=line && tag->name && g_utf8_validate(tag->name,-1,NULL) && (!best || tag->line>best->line)) best=tag;
 }
 if(!best) return g_strdup("");
 return best->scope && g_utf8_validate(best->scope,-1,NULL) ? g_strdup_printf("%s::%s",best->scope,best->name) : g_strdup(best->name);
}
typedef struct { guint id;gchar *name,*path,*symbol,*status;gint lines;guint64 recent;const gchar *active; } WindowRow;
static void row_free(gpointer data) { WindowRow *r=data;g_free(r->name);g_free(r->path);g_free(r->symbol);g_free(r->status);g_free(r); }
static void refresh(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(!list.window || list.refreshing || list.busy) return;
 GArray *chosen=selected_ids();GeanyDocument *current=document_get_current();GPtrArray *rows=g_ptr_array_new_with_free_func(row_free);GString *signature=g_string_new(NULL);
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(list.search));gint scope=gtk_combo_box_get_active(GTK_COMBO_BOX(list.scope));list.total=0;
 for(guint i=0;i<documents_array->len;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(!DOC_VALID(doc)) continue;list.total++;
  if((scope==1 && !doc->changed) || (scope==2 && !doc->readonly) || (scope==3 && doc->real_path)) continue;
  WindowRow *r=g_new0(WindowRow,1);r->id=doc->id;r->name=doc->file_name ? g_path_get_basename(doc->file_name) : g_strdup_printf("untitled-%u",doc->id);r->path=g_strdup(doc->file_name ? doc->file_name : "");r->symbol=nearest_symbol(doc);
  gchar *all=g_strjoin("\n",r->name,r->path,r->symbol,NULL);gboolean match=insight_lookup_text_match(all,query);g_free(all);if(!match) { row_free(r);continue; }
  guint64 *recent=g_hash_table_lookup(list.recent,GUINT_TO_POINTER(doc->id));r->recent=recent ? *recent : 0;r->lines=sci_get_line_count(doc->editor->sci);r->active=doc==current ? "●" : "";r->status=g_strdup(insight_tr(doc->changed ? (doc->readonly ? "Modified • Read only" : "Modified") : (doc->readonly ? "Read only" : "Saved")));
  g_string_append_len(signature,(const gchar *)&r->id,sizeof r->id);g_string_append_len(signature,(const gchar *)&r->recent,sizeof r->recent);g_string_append_len(signature,(const gchar *)&r->lines,sizeof r->lines);
  const gchar *fields[]={r->name,r->path,r->symbol,r->status,r->active};for(guint n=0;n<G_N_ELEMENTS(fields);n++) g_string_append_len(signature,fields[n],strlen(fields[n])+1);g_ptr_array_add(rows,r);
 }
 list.shown=rows->len;GBytes *snapshot=g_bytes_new(signature->str,signature->len);g_string_free(signature,TRUE);
 if(!list.snapshot || !g_bytes_equal(list.snapshot,snapshot)) {
  list.refreshing=TRUE;gtk_list_store_clear(list.model);g_clear_pointer(&list.snapshot,g_bytes_unref);list.snapshot=g_bytes_ref(snapshot);
  for(guint n=0;n<rows->len;n++) { WindowRow *r=g_ptr_array_index(rows,n);GtkTreeIter iter;gtk_list_store_append(list.model,&iter);gtk_list_store_set(list.model,&iter,ID,r->id,NAME,r->name,PATH,r->path,STATE,r->status,LINES,r->lines,SYMBOL,r->symbol,RECENT,r->recent,ACTIVE,r->active,-1); }
  GtkTreeIter iter;gboolean valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list.model),&iter),matched=FALSE;
  while(valid) { guint id=0;gtk_tree_model_get(GTK_TREE_MODEL(list.model),&iter,ID,&id,-1);for(guint n=0;n<chosen->len;n++) if(id==g_array_index(chosen,guint,n)) { gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(list.view)),&iter);matched=TRUE;break; }valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(list.model),&iter); }
  if(!matched) { valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list.model),&iter);while(valid) { guint id=0;gtk_tree_model_get(GTK_TREE_MODEL(list.model),&iter,ID,&id,-1);if(current && id==current->id) break;valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(list.model),&iter); }if(valid || gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list.model),&iter)) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(list.view)),&iter); }
  list.refreshing=FALSE;
 }
 list.refreshing=TRUE;selection_changed(NULL,NULL);list.refreshing=FALSE;g_bytes_unref(snapshot);g_array_unref(chosen);g_ptr_array_unref(rows);
}
void insight_window_list_locale_changed(void) { if(list.alive && list.window) refresh(NULL,NULL); }
static gboolean refresh_idle(gpointer data) { (void)data;list.timer=0;if(list.content && gtk_widget_get_mapped(list.content)) refresh(NULL,NULL);return G_SOURCE_REMOVE; }
void insight_window_list_editor_updated(void) { if(list.alive && list.content && gtk_widget_get_mapped(list.content) && !list.timer) list.timer=g_timeout_add(150,refresh_idle,NULL); }
static void visit(guint id) {
 if(!id) return;
 guint64 *serial=g_new(guint64,1);*serial=++list.serial;g_hash_table_replace(list.recent,GUINT_TO_POINTER(id),serial);
}
void insight_window_list_document_event(guint id,const gchar *event) {
 if(!list.alive) return;
 if(!strcmp(event,"document_close")) g_hash_table_remove(list.recent,GUINT_TO_POINTER(id));
 else if(!strcmp(event,"document_activate") || !strcmp(event,"document_open")) visit(id);
 insight_window_list_editor_updated();
}
static void action(GtkWidget *widget,gpointer data) {
 (void)widget;if(list.busy || !list.alive) return;gint which=GPOINTER_TO_INT(data);GArray *ids=selected_ids();
 if(which==0) { GeanyDocument *doc=selected_document();if(doc) { document_show_tab(doc);document_grab_focus(doc); } }
 else {
  list.busy=TRUE;selection_changed(NULL,NULL);
  for(guint i=0;i<ids->len && list.alive && list.window;i++) {
   GeanyDocument *doc=document_find_by_id(g_array_index(ids,guint,i));if(!DOC_VALID(doc)) continue;
   if(which==2) { if(!document_close(doc)) break; }
   else if(!doc->readonly) {
    if(which==3) { document_show_tab(doc);if(!dialogs_show_save_as()) break; }
    else if((doc->changed || !doc->real_path) && !document_save_file(doc,FALSE)) break;
   }
  }
  list.busy=FALSE;
 }
 g_array_unref(ids);refresh(NULL,NULL);
}
static void selection_tool(GtkWidget *widget,gpointer data) {
 (void)widget;if(list.busy || !list.view) return;gint which=GPOINTER_TO_INT(data);GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(list.view));
 if(which==0) gtk_tree_selection_select_all(selection);
 else if(which==1) { list.refreshing=TRUE;GtkTreeIter iter;gboolean valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list.model),&iter);while(valid) { if(gtk_tree_selection_iter_is_selected(selection,&iter)) gtk_tree_selection_unselect_iter(selection,&iter);else gtk_tree_selection_select_iter(selection,&iter);valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(list.model),&iter); }list.refreshing=FALSE;selection_changed(NULL,NULL); }
 else { GArray *ids=selected_ids();GString *paths=g_string_new(NULL);for(guint i=0;i<ids->len;i++) { GeanyDocument *doc=document_find_by_id(g_array_index(ids,guint,i));if(DOC_VALID(doc) && doc->file_name) { if(paths->len) g_string_append_c(paths,'\n');g_string_append(paths,doc->file_name); } }if(paths->len>2u*1024u*1024u) ui_set_statusbar(TRUE,"%s",insight_tr("Selected paths exceed the 2 MiB clipboard limit"));else if(paths->len) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),paths->str,paths->len);g_string_free(paths,TRUE);g_array_unref(ids); }
}
static void activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) { (void)view;(void)path;(void)column;(void)data;action(NULL,NULL); }
static gboolean key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;(void)data;if(!list.window) return FALSE;if(event->keyval==GDK_KEY_Escape) { hide_list();return TRUE; }
 if((event->keyval==GDK_KEY_Return || event->keyval==GDK_KEY_KP_Enter) && (gtk_window_get_focus(GTK_WINDOW(gtk_widget_get_toplevel(list.content)))==list.search || gtk_window_get_focus(GTK_WINDOW(gtk_widget_get_toplevel(list.content)))==list.view)) { action(NULL,NULL);return TRUE; }
 return FALSE;
}
static void sort_changed(GtkComboBox *combo,gpointer data) {
 (void)data;if(!list.window || list.sort_syncing) return;gint sort=gtk_combo_box_get_active(combo);if(sort<0 || sort>2) return;gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list.model),sort==0 ? RECENT : (sort==1 ? NAME : PATH),sort==0 ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
}
static void sort_column_changed(GtkTreeSortable *model,gpointer data) {
 (void)data;if(!list.window || !list.sort) return;gint column=-1;GtkSortType direction;gtk_tree_sortable_get_sort_column_id(model,&column,&direction);
 gint preset=column==RECENT && direction==GTK_SORT_DESCENDING ? 0 : (column==NAME && direction==GTK_SORT_ASCENDING ? 1 : (column==PATH && direction==GTK_SORT_ASCENDING ? 2 : 3));
 list.sort_syncing=TRUE;gtk_combo_box_set_active(GTK_COMBO_BOX(list.sort),preset);list.sort_syncing=FALSE;
}
static void dock_close(GtkWidget *widget,gpointer data) { (void)widget; (void)data; hide_list(); }
static void dock_changed(GtkToggleButton *toggle,gpointer data) {
 (void)data; if(!list.alive || list.moving || !list.content) return;
 gboolean docked=gtk_toggle_button_get_active(toggle);
 list.moving=TRUE; list.refreshing=TRUE;
 if(docked && !list.dock_page) {
  list.dock_page=gtk_scrolled_window_new(NULL,NULL); gtk_widget_set_name(list.dock_page,"insight-window-list-dock");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list.dock_page),GTK_POLICY_AUTOMATIC,GTK_POLICY_NEVER);
  GtkWidget *tab=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*label=gtk_label_new("Window List"),*close=gtk_button_new_from_icon_name("window-close",GTK_ICON_SIZE_MENU);
  gtk_button_set_relief(GTK_BUTTON(close),GTK_RELIEF_NONE); gtk_widget_set_tooltip_text(close,"Hide Window List");
  gtk_box_pack_start(GTK_BOX(tab),label,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(tab),close,FALSE,FALSE,0); gtk_widget_show_all(tab);
  gtk_notebook_append_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),list.dock_page,tab); g_signal_connect(close,"clicked",G_CALLBACK(dock_close),NULL);
 }
 g_object_ref(list.content); GtkWidget *parent=gtk_widget_get_parent(list.content); if(parent) gtk_container_remove(GTK_CONTAINER(parent),list.content);
 /* GtkScrolledWindow adds a viewport for the non-scrollable content box. */
 if(!docked && list.dock_page) { GtkWidget *child=gtk_bin_get_child(GTK_BIN(list.dock_page)); if(child) gtk_widget_destroy(child); }
 list.docked=docked; gtk_container_add(GTK_CONTAINER(docked ? list.dock_page : list.window),list.content); g_object_unref(list.content);
 if(docked) {
  gtk_widget_hide(list.window); gtk_widget_show_all(list.dock_page); ui_prefs.sidebar_visible=TRUE; ui_sidebar_show_hide();
  gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),gtk_notebook_page_num(GTK_NOTEBOOK(main_widgets.sidebar_notebook),list.dock_page));
 } else {
  if(list.dock_page) { gtk_widget_hide(list.dock_page); } restore_geometry(); gtk_widget_show_all(list.window); gtk_window_present(GTK_WINDOW(list.window));
 }
 list.refreshing=FALSE; list.moving=FALSE; refresh(NULL,NULL); gtk_widget_grab_focus(list.search); settings_save();
}
static void destroyed(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;settings_save();list.moving=TRUE;if(list.dock_page) { gtk_widget_destroy(list.dock_page); list.dock_page=NULL; } list.content=list.dock_toggle=NULL;if(list.timer) { g_source_remove(list.timer);list.timer=0; }
 list.window=list.search=list.view=list.sort=list.activate=list.save=list.close=list.status=list.save_as=list.scope=NULL;g_clear_pointer(&list.snapshot,g_bytes_unref);g_clear_object(&list.model);list.moving=FALSE;
}
void insight_window_list_show(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(!list.alive) return;
 if(list.window) { refresh(NULL,NULL); if(list.docked) { gtk_widget_show_all(list.dock_page); ui_prefs.sidebar_visible=TRUE; ui_sidebar_show_hide(); gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),gtk_notebook_page_num(GTK_NOTEBOOK(main_widgets.sidebar_notebook),list.dock_page)); } else { gtk_widget_show(list.window);gtk_window_present(GTK_WINDOW(list.window)); } gtk_widget_grab_focus(list.search);return; }
 list.window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(list.window),"Window List");gtk_widget_set_name(list.window,"insight-window-list-window");restore_geometry();gtk_window_set_transient_for(GTK_WINDOW(list.window),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(list.window),TRUE);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,5),*bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_container_add(GTK_CONTAINER(list.window),box);
 list.content=box; g_signal_connect(box,"map",G_CALLBACK(refresh),NULL); g_signal_connect(box,"key-press-event",G_CALLBACK(key),NULL); insight_panel_help_attach(box,"window-list.md");
 list.search=gtk_search_entry_new();gtk_widget_set_tooltip_text(list.search,"Prefix with ~ for fuzzy subsequence matching");gtk_entry_set_max_length(GTK_ENTRY(list.search),512);gtk_entry_set_text(GTK_ENTRY(list.search),list.filter ? list.filter : "");gtk_entry_set_placeholder_text(GTK_ENTRY(list.search),"Filter open files, paths or nearest symbol…");gtk_widget_set_name(list.search,"insight-window-filter");gtk_box_pack_start(GTK_BOX(box),list.search,FALSE,FALSE,0);
 list.model=gtk_list_store_new(COLUMNS,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_INT,G_TYPE_STRING,G_TYPE_UINT64,G_TYPE_STRING);list.view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(list.model));gtk_widget_set_name(list.view,"insight-window-list");
 gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(list.view)),GTK_SELECTION_MULTIPLE);
 const gchar *titles[]={"Active","File","State","Lines","Nearest symbol","Path"};const gint fields[]={ACTIVE,NAME,STATE,LINES,SYMBOL,PATH};
 for(guint position=0;position<G_N_ELEMENTS(fields);position++) { guint i=(guint)list.order[position]; GtkCellRenderer *cell=gtk_cell_renderer_text_new();g_object_set(cell,"ellipsize",PANGO_ELLIPSIZE_END,NULL);GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(titles[i],cell,"text",fields[i],NULL);gtk_tree_view_column_set_sort_column_id(column,fields[i]);gtk_tree_view_column_set_resizable(column,TRUE);gtk_tree_view_column_set_sizing(column,GTK_TREE_VIEW_COLUMN_FIXED);gtk_tree_view_column_set_fixed_width(column,list.widths[i]);gtk_tree_view_column_set_reorderable(column,TRUE);g_object_set_data(G_OBJECT(column),"insight-list-column",GUINT_TO_POINTER(i+1));gtk_tree_view_append_column(GTK_TREE_VIEW(list.view),column); }
 gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(list.view),PATH);gtk_container_add(GTK_CONTAINER(scroll),list.view);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 list.sort=gtk_combo_box_text_new();gtk_widget_set_name(list.sort,"insight-window-sort");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(list.sort),"Most recently used");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(list.sort),"Name");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(list.sort),"Path");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(list.sort),"Column sort");gtk_combo_box_set_active(GTK_COMBO_BOX(list.sort),0);gtk_box_pack_start(GTK_BOX(bar),list.sort,FALSE,FALSE,0);
 GtkWidget *tools=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);list.scope=gtk_combo_box_text_new();gtk_widget_set_name(list.scope,"insight-window-scope");const gchar *scopes[]={"All open files","Modified files","Read-only files","Unsaved files"};for(guint i=0;i<G_N_ELEMENTS(scopes);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(list.scope),insight_tr(scopes[i]));gtk_combo_box_set_active(GTK_COMBO_BOX(list.scope),0);gtk_box_pack_start(GTK_BOX(tools),list.scope,FALSE,FALSE,0);g_signal_connect(list.scope,"changed",G_CALLBACK(refresh),NULL);
 const gchar *tools_labels[]={"Select all","Invert selection","Copy paths"},*tools_names[]={"all","invert","paths"};for(guint i=0;i<3;i++) { GtkWidget *button=gtk_button_new_with_label(insight_tr(tools_labels[i]));gchar *name=g_strconcat("insight-window-",tools_names[i],NULL);gtk_widget_set_name(button,name);g_free(name);gtk_box_pack_start(GTK_BOX(tools),button,FALSE,FALSE,0);g_signal_connect(button,"clicked",G_CALLBACK(selection_tool),GINT_TO_POINTER(i)); }gtk_box_pack_start(GTK_BOX(box),tools,FALSE,FALSE,0);
 list.save_as=gtk_button_new_with_label(insight_tr("Save As…"));gtk_widget_set_name(list.save_as,"insight-window-save-as");g_signal_connect(list.save_as,"clicked",G_CALLBACK(action),GINT_TO_POINTER(3));gtk_box_pack_start(GTK_BOX(bar),list.save_as,FALSE,FALSE,0);
 list.activate=gtk_button_new_with_label("Activate");list.save=gtk_button_new_with_label("Save");list.close=gtk_button_new_with_label("Close file");GtkWidget *reload=gtk_button_new_from_icon_name("view-refresh",GTK_ICON_SIZE_BUTTON);gtk_widget_set_tooltip_text(reload,"Refresh open file list");
 gtk_widget_set_name(list.save,"insight-window-save");gtk_widget_set_name(list.close,"insight-window-close");gtk_widget_set_name(list.activate,"insight-window-activate");
 GtkWidget *buttons[]={list.activate,list.save,list.close};for(guint i=0;i<G_N_ELEMENTS(buttons);i++) { gtk_box_pack_start(GTK_BOX(bar),buttons[i],FALSE,FALSE,0);g_signal_connect(buttons[i],"clicked",G_CALLBACK(action),GINT_TO_POINTER(i)); }
 list.dock_toggle=gtk_check_button_new_with_label("Dock in sidebar"); gtk_widget_set_name(list.dock_toggle,"insight-window-list-dock-toggle"); gtk_box_pack_start(GTK_BOX(box),list.dock_toggle,FALSE,FALSE,0); g_signal_connect(list.dock_toggle,"toggled",G_CALLBACK(dock_changed),NULL);
 gtk_box_pack_start(GTK_BOX(bar),reload,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);list.status=gtk_label_new("");gtk_box_pack_start(GTK_BOX(box),list.status,FALSE,FALSE,0);
 g_signal_connect(list.search,"key-press-event",G_CALLBACK(key),NULL);g_signal_connect(list.view,"key-press-event",G_CALLBACK(key),NULL);g_signal_connect(list.search,"changed",G_CALLBACK(refresh),NULL);g_signal_connect(list.sort,"changed",G_CALLBACK(sort_changed),NULL);g_signal_connect(list.model,"sort-column-changed",G_CALLBACK(sort_column_changed),NULL);g_signal_connect(reload,"clicked",G_CALLBACK(refresh),NULL);g_signal_connect(list.view,"row-activated",G_CALLBACK(activated),NULL);g_signal_connect(list.view,"button-press-event",G_CALLBACK(button_press),NULL);g_signal_connect(list.view,"button-release-event",G_CALLBACK(button_release),NULL);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(list.view)),"changed",G_CALLBACK(selection_changed),NULL);g_signal_connect(list.window,"key-press-event",G_CALLBACK(key),NULL);g_signal_connect(list.window,"configure-event",G_CALLBACK(configured),NULL);g_signal_connect(list.window,"window-state-event",G_CALLBACK(window_state),NULL);g_signal_connect(list.window,"delete-event",G_CALLBACK(close_window),NULL);g_signal_connect(list.window,"destroy",G_CALLBACK(destroyed),NULL);
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list.model),list.column,(GtkSortType)list.direction);refresh(NULL,NULL);gtk_widget_show_all(list.window);gtk_widget_grab_focus(list.search); if(list.docked) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(list.dock_toggle),TRUE);
}
void insight_window_list_init(InsightWindowPreview preview) {
 settings_read();list.alive=TRUE;list.preview=preview;list.recent=g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,g_free);
 for(guint i=0;i<documents_array->len;i++) { GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(doc->is_valid) visit(doc->id); }
 GeanyDocument *doc=document_get_current();if(doc) visit(doc->id);
}
void insight_window_list_layout_capture(InsightLayout *record) {
 if(!record || !list.alive) return;
 gint *v=record->values;v[IL_WL_SET]=TRUE;v[IL_WL_VISIBLE]=list.window && gtk_widget_get_visible(list.docked ? list.dock_page : list.window);v[IL_WL_DOCKED]=list.docked;v[IL_WL_POSITIONED]=list.positioned;v[IL_WL_MAXIMIZED]=list.maximized;
 v[IL_WL_X]=list.x;v[IL_WL_Y]=list.y;v[IL_WL_WIDTH]=list.width;v[IL_WL_HEIGHT]=list.height;
}
void insight_window_list_layout_apply(const InsightLayout *record) {
 if(!list.alive || !insight_layout_valid(record) || !record->values[IL_WL_SET]) return;
 const gint *v=record->values;
 if(list.window && list.docked!=v[IL_WL_DOCKED]) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(list.dock_toggle),v[IL_WL_DOCKED]);
 list.moving=TRUE;list.docked=v[IL_WL_DOCKED];list.positioned=v[IL_WL_POSITIONED];list.maximized=v[IL_WL_MAXIMIZED];list.x=v[IL_WL_X];list.y=v[IL_WL_Y];list.width=v[IL_WL_WIDTH];list.height=v[IL_WL_HEIGHT];
 if(list.window) restore_geometry();
 list.moving=FALSE;
 if(v[IL_WL_VISIBLE]) insight_window_list_show(NULL,NULL);else hide_list();
 settings_save();
}
void insight_window_list_finalize(void) {
 settings_save();list.alive=FALSE;if(list.timer) { g_source_remove(list.timer);list.timer=0; }if(list.window) gtk_widget_destroy(list.window);g_clear_pointer(&list.recent,g_hash_table_destroy);g_free(list.settings);g_free(list.filter);memset(&list,0,sizeof list);
}
