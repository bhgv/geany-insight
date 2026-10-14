#define INSIGHT_LOCALIZE_FORMATS
#include "insight-i18n.h"
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "insight-window-list.h"
#include "document.h"
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
 gboolean alive,refreshing,press_previewed,sort_syncing;GtkWidget *window,*search,*view,*sort,*activate,*save,*close,*status;GtkListStore *model;
 GHashTable *recent;guint64 serial;guint timer;InsightWindowPreview preview;
 gchar *settings,*filter; gint x,y,width,height,column,direction,widths[6],order[6]; gboolean positioned,maximized;
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
 g_key_file_set_integer(key,group,"version",1); g_key_file_set_integer(key,group,"width",list.width); g_key_file_set_integer(key,group,"height",list.height); g_key_file_set_integer(key,group,"x",list.x); g_key_file_set_integer(key,group,"y",list.y); g_key_file_set_boolean(key,group,"positioned",list.positioned); g_key_file_set_boolean(key,group,"maximized",list.maximized); g_key_file_set_integer(key,group,"sort-column",list.column); g_key_file_set_integer(key,group,"sort-direction",list.direction); g_key_file_set_string(key,group,"filter",list.filter ? list.filter : ""); g_key_file_set_integer_list(key,group,"column-widths",list.widths,6); g_key_file_set_integer_list(key,group,"column-order",list.order,6);
 gsize size=0; gchar *text=g_key_file_to_data(key,&size,NULL),*directory=g_path_get_dirname(list.settings); GError *error=NULL;
 if(!g_mkdir_with_parents(directory,0700)) { GFile *file=g_file_new_for_path(list.settings); g_file_replace_contents(file,text,size,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,&error); g_object_unref(file); }
 else g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot create Window List settings directory");
 if(error && !main_status.quitting) ui_set_statusbar(TRUE,"Window List settings: %s",insight_tr(error->message));
 g_clear_error(&error); g_free(directory); g_free(text); g_key_file_unref(key);
}
static gboolean configured(GtkWidget *widget,GdkEventConfigure *event,gpointer data) {
 (void)data; GdkWindow *window=gtk_widget_get_window(widget); if(window && !(gdk_window_get_state(window)&(GDK_WINDOW_STATE_MAXIMIZED|GDK_WINDOW_STATE_FULLSCREEN|GDK_WINDOW_STATE_ICONIFIED))) { list.width=CLAMP(event->width,400,4000); list.height=CLAMP(event->height,220,3000); gtk_window_get_position(GTK_WINDOW(widget),&list.x,&list.y); list.positioned=TRUE; } return FALSE;
}
static gboolean window_state(GtkWidget *widget,GdkEventWindowState *event,gpointer data) { (void)widget; (void)data; list.maximized=(event->new_window_state&GDK_WINDOW_STATE_MAXIMIZED)!=0; return FALSE; }
static gboolean close_window(GtkWidget *widget,GdkEvent *event,gpointer data) { (void)event; (void)data; settings_save(); gtk_widget_hide(widget); return TRUE; }
static void restore_geometry(void) {
 gint width=list.width,height=list.height,x=list.x,y=list.y; GdkDisplay *display=gtk_widget_get_display(main_widgets.window); GdkMonitor *monitor=list.positioned ? gdk_display_get_monitor_at_point(display,x,y) : gdk_display_get_primary_monitor(display);
 if(!monitor && gdk_display_get_n_monitors(display)) monitor=gdk_display_get_monitor(display,0);
 if(monitor) { GdkRectangle area; gdk_monitor_get_workarea(monitor,&area); width=MIN(width,MAX(1,area.width)); height=MIN(height,MAX(1,area.height)); x=CLAMP(x,area.x,area.x+MAX(0,area.width-width)); y=CLAMP(y,area.y,area.y+MAX(0,area.height-height)); }
 gtk_window_set_default_size(GTK_WINDOW(list.window),width,height); if(list.positioned) gtk_window_move(GTK_WINDOW(list.window),x,y); if(list.maximized) gtk_window_maximize(GTK_WINDOW(list.window));
}
static GeanyDocument *selected_document(void) {
 if(!list.window || !list.view) return NULL;
 GtkTreeModel *model;GtkTreeIter iter;guint id=0;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(list.view)),&model,&iter)) gtk_tree_model_get(model,&iter,ID,&id,-1);
 return id ? document_find_by_id(id) : NULL;
}
static void selection_changed(GtkTreeSelection *selection,gpointer data) {
 (void)selection;(void)data;if(!list.window) return;
 GeanyDocument *doc=selected_document();gtk_widget_set_sensitive(list.activate,doc!=NULL);gtk_widget_set_sensitive(list.close,doc!=NULL);gtk_widget_set_sensitive(list.save,doc && !doc->readonly);
 if(doc && !list.refreshing && list.preview) { list.preview(doc->id);list.press_previewed=TRUE; }
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
static void refresh(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(!list.window || list.refreshing) return;
 GeanyDocument *selected=selected_document(),*current=document_get_current();guint chosen=selected ? selected->id : (current ? current->id : 0);
 list.refreshing=TRUE;gchar *needle=g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(list.search)),-1);gtk_list_store_clear(list.model);guint shown=0,total=0;
 for(guint i=0;i<documents_array->len;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(!doc->is_valid) continue;total++;
  gchar *name=doc->file_name ? g_path_get_basename(doc->file_name) : g_strdup_printf("untitled-%u",doc->id),*path=g_strdup(doc->file_name ? doc->file_name : ""),*symbol=nearest_symbol(doc);
  gchar *all=g_strjoin("\n",name,path,symbol,NULL),*folded=g_utf8_casefold(all,-1);g_free(all);
  if(!*needle || strstr(folded,needle)) {
   guint64 *recent=g_hash_table_lookup(list.recent,GUINT_TO_POINTER(doc->id));GtkTreeIter iter;gtk_list_store_append(list.model,&iter);
   gtk_list_store_set(list.model,&iter,ID,doc->id,NAME,name,PATH,path,STATE,insight_tr(doc->changed ? (doc->readonly ? "Modified • Read only" : "Modified") : (doc->readonly ? "Read only" : "Saved")),LINES,sci_get_line_count(doc->editor->sci),SYMBOL,symbol,RECENT,recent ? *recent : 0,ACTIVE,doc==current ? "●" : "",-1);shown++;
  }
  g_free(folded);g_free(name);g_free(path);g_free(symbol);
 }
 g_free(needle);GtkTreeIter iter;gboolean valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list.model),&iter),matched=FALSE;
 while(valid) { guint id=0;gtk_tree_model_get(GTK_TREE_MODEL(list.model),&iter,ID,&id,-1);if(id==chosen) { gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(list.view)),&iter);matched=TRUE;break; }valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(list.model),&iter); }
 if(!matched && gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list.model),&iter)) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(list.view)),&iter);
 gchar *status=g_strdup_printf("%u of %u open files • double click / Enter: activate",shown,total);gtk_label_set_text(GTK_LABEL(list.status),status);g_free(status);
 selection_changed(NULL,NULL);list.refreshing=FALSE;
}
void insight_window_list_locale_changed(void) { if(list.alive && list.window) refresh(NULL,NULL); }
static gboolean refresh_idle(gpointer data) { (void)data;list.timer=0;if(list.window && gtk_widget_get_visible(list.window)) refresh(NULL,NULL);return G_SOURCE_REMOVE; }
void insight_window_list_editor_updated(void) { if(list.alive && list.window && gtk_widget_get_visible(list.window) && !list.timer) list.timer=g_timeout_add(150,refresh_idle,NULL); }
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
 (void)widget;GeanyDocument *doc=selected_document();if(!doc) return;gint which=GPOINTER_TO_INT(data);
 if(which==1) document_save_file(doc,FALSE);
 else if(which==2) document_close(doc);
 else { gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.notebook),document_get_notebook_page(doc));document_grab_focus(doc); }
 refresh(NULL,NULL);
}
static void activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) { (void)view;(void)path;(void)column;(void)data;action(NULL,NULL); }
static gboolean key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;(void)data;if(!list.window) return FALSE;if(event->keyval==GDK_KEY_Escape) { settings_save(); gtk_widget_hide(list.window);return TRUE; }
 if((event->keyval==GDK_KEY_Return || event->keyval==GDK_KEY_KP_Enter) && (gtk_window_get_focus(GTK_WINDOW(list.window))==list.search || gtk_window_get_focus(GTK_WINDOW(list.window))==list.view)) { action(NULL,NULL);return TRUE; }
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
static void destroyed(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;settings_save();if(list.timer) { g_source_remove(list.timer);list.timer=0; }
 list.window=list.search=list.view=list.sort=list.activate=list.save=list.close=list.status=NULL;g_clear_object(&list.model);
}
void insight_window_list_show(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(!list.alive) return;
 if(list.window) { refresh(NULL,NULL);gtk_widget_show(list.window);gtk_window_present(GTK_WINDOW(list.window));gtk_widget_grab_focus(list.search);return; }
 list.window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(list.window),"Window List");gtk_widget_set_name(list.window,"insight-window-list-window");restore_geometry();gtk_window_set_transient_for(GTK_WINDOW(list.window),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(list.window),TRUE);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,5),*bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_container_add(GTK_CONTAINER(list.window),box);
 list.search=gtk_search_entry_new();gtk_entry_set_max_length(GTK_ENTRY(list.search),512);gtk_entry_set_text(GTK_ENTRY(list.search),list.filter ? list.filter : "");gtk_entry_set_placeholder_text(GTK_ENTRY(list.search),"Filter open files, paths or nearest symbol…");gtk_widget_set_name(list.search,"insight-window-filter");gtk_box_pack_start(GTK_BOX(box),list.search,FALSE,FALSE,0);
 list.model=gtk_list_store_new(COLUMNS,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_INT,G_TYPE_STRING,G_TYPE_UINT64,G_TYPE_STRING);list.view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(list.model));gtk_widget_set_name(list.view,"insight-window-list");
 const gchar *titles[]={"Active","File","State","Lines","Nearest symbol","Path"};const gint fields[]={ACTIVE,NAME,STATE,LINES,SYMBOL,PATH};
 for(guint position=0;position<G_N_ELEMENTS(fields);position++) { guint i=(guint)list.order[position]; GtkCellRenderer *cell=gtk_cell_renderer_text_new();g_object_set(cell,"ellipsize",PANGO_ELLIPSIZE_END,NULL);GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(titles[i],cell,"text",fields[i],NULL);gtk_tree_view_column_set_sort_column_id(column,fields[i]);gtk_tree_view_column_set_resizable(column,TRUE);gtk_tree_view_column_set_sizing(column,GTK_TREE_VIEW_COLUMN_FIXED);gtk_tree_view_column_set_fixed_width(column,list.widths[i]);gtk_tree_view_column_set_reorderable(column,TRUE);g_object_set_data(G_OBJECT(column),"insight-list-column",GUINT_TO_POINTER(i+1));gtk_tree_view_append_column(GTK_TREE_VIEW(list.view),column); }
 gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(list.view),PATH);gtk_container_add(GTK_CONTAINER(scroll),list.view);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 list.sort=gtk_combo_box_text_new();gtk_widget_set_name(list.sort,"insight-window-sort");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(list.sort),"Most recently used");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(list.sort),"Name");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(list.sort),"Path");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(list.sort),"Column sort");gtk_combo_box_set_active(GTK_COMBO_BOX(list.sort),0);gtk_box_pack_start(GTK_BOX(bar),list.sort,FALSE,FALSE,0);
 list.activate=gtk_button_new_with_label("Activate");list.save=gtk_button_new_with_label("Save");list.close=gtk_button_new_with_label("Close file");GtkWidget *reload=gtk_button_new_from_icon_name("view-refresh",GTK_ICON_SIZE_BUTTON);gtk_widget_set_tooltip_text(reload,"Refresh open file list");
 GtkWidget *buttons[]={list.activate,list.save,list.close};for(guint i=0;i<G_N_ELEMENTS(buttons);i++) { gtk_box_pack_start(GTK_BOX(bar),buttons[i],FALSE,FALSE,0);g_signal_connect(buttons[i],"clicked",G_CALLBACK(action),GINT_TO_POINTER(i)); }
 gtk_box_pack_start(GTK_BOX(bar),reload,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);list.status=gtk_label_new("");gtk_box_pack_start(GTK_BOX(box),list.status,FALSE,FALSE,0);
 g_signal_connect(list.search,"changed",G_CALLBACK(refresh),NULL);g_signal_connect(list.sort,"changed",G_CALLBACK(sort_changed),NULL);g_signal_connect(list.model,"sort-column-changed",G_CALLBACK(sort_column_changed),NULL);g_signal_connect(reload,"clicked",G_CALLBACK(refresh),NULL);g_signal_connect(list.view,"row-activated",G_CALLBACK(activated),NULL);g_signal_connect(list.view,"button-press-event",G_CALLBACK(button_press),NULL);g_signal_connect(list.view,"button-release-event",G_CALLBACK(button_release),NULL);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(list.view)),"changed",G_CALLBACK(selection_changed),NULL);g_signal_connect(list.window,"key-press-event",G_CALLBACK(key),NULL);g_signal_connect(list.window,"configure-event",G_CALLBACK(configured),NULL);g_signal_connect(list.window,"window-state-event",G_CALLBACK(window_state),NULL);g_signal_connect(list.window,"delete-event",G_CALLBACK(close_window),NULL);g_signal_connect(list.window,"destroy",G_CALLBACK(destroyed),NULL);
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list.model),list.column,(GtkSortType)list.direction);refresh(NULL,NULL);gtk_widget_show_all(list.window);gtk_widget_grab_focus(list.search);
}
void insight_window_list_init(InsightWindowPreview preview) {
 settings_read();list.alive=TRUE;list.preview=preview;list.recent=g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,g_free);
 for(guint i=0;i<documents_array->len;i++) { GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(doc->is_valid) visit(doc->id); }
 GeanyDocument *doc=document_get_current();if(doc) visit(doc->id);
}
void insight_window_list_finalize(void) {
 list.alive=FALSE;if(list.timer) { g_source_remove(list.timer);list.timer=0; }if(list.window) gtk_widget_destroy(list.window);g_clear_pointer(&list.recent,g_hash_table_destroy);g_free(list.settings);g_free(list.filter);memset(&list,0,sizeof list);
}
