#define INSIGHT_LOCALIZE_FORMATS
#include "insight-i18n.h"
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "keybindings.h"
#include "insight-disk-browser.h"
#include "../insight/desktop/lookup_filter.h"
#include <gio/gio.h>
#include <string.h>
#define DISK_LIMIT 2048u
enum { NAME,PATH,DIRECTORY,LINK,COLUMNS };
typedef struct { gchar *name,*path;gboolean directory,link; } DiskEntry;
typedef struct {
 GtkWidget *box,*path,*query,*view,*status,*hidden,*kind_toggle;GtkTreeViewColumn *name_column,*kind_column;GtkListStore *store;GtkTreeModelFilter *model;
 InsightLookupFilter *filter;GCancellable *cancel;guint64 generation;gboolean alive,setting;
 gchar *root,*failure,*valid_query;guint count;gboolean loading,limited,cancelled;InsightDiskOpen open;gpointer data;
} DiskBrowser;
typedef struct { GWeakRef box;guint64 generation;gchar *root;gboolean hidden,limited; } DiskJob;
static void entry_free(gpointer data) { DiskEntry *e=data;g_free(e->name);g_free(e->path);g_free(e); }
static void job_free(gpointer data) { DiskJob *j=data;g_weak_ref_clear(&j->box);g_free(j->root);g_free(j); }
static gint compare(gconstpointer a,gconstpointer b) { const DiskEntry *x=*(DiskEntry *const *)a,*y=*(DiskEntry *const *)b;if(x->directory!=y->directory) return x->directory ? -1 : 1;return g_utf8_collate(x->name,y->name); }
static void read_directory(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source;DiskJob *j=data;GError *error=NULL;GFile *folder=g_file_new_for_path(j->root);
 GFileEnumerator *reader=g_file_enumerate_children(folder,"standard::name,standard::type,standard::is-symlink,standard::is-hidden",G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel,&error);g_object_unref(folder);
 GPtrArray *entries=g_ptr_array_new_with_free_func(entry_free);guint visited=0;
 if(reader) {
  while(!g_cancellable_is_cancelled(cancel)) {
   GFileInfo *info=g_file_enumerator_next_file(reader,cancel,&error);if(!info) break;
   if(++visited>100000u) { j->limited=TRUE;g_object_unref(info);break; }
   const gchar *name=g_file_info_get_name(info);GFileType type=g_file_info_get_file_type(info);
   if(name && g_utf8_validate(name,-1,NULL) && (j->hidden || (!g_file_info_get_is_hidden(info) && name[0]!='.')) &&
      (type==G_FILE_TYPE_DIRECTORY || type==G_FILE_TYPE_REGULAR || type==G_FILE_TYPE_SYMBOLIC_LINK)) {
    if(entries->len==DISK_LIMIT) { j->limited=TRUE;g_object_unref(info);break; }
    DiskEntry *e=g_new0(DiskEntry,1);e->name=g_strdup(name);e->path=g_build_filename(j->root,name,NULL);e->link=g_file_info_get_is_symlink(info);e->directory=type==G_FILE_TYPE_DIRECTORY && !e->link;g_ptr_array_add(entries,e);
   }
   g_object_unref(info);
  }
  g_object_unref(reader);
 }
 if(g_cancellable_is_cancelled(cancel) && !error) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_CANCELLED,"Operation cancelled");
 if(error) { g_ptr_array_unref(entries);g_task_return_error(task,error);return; }
 g_ptr_array_sort(entries,compare);g_task_return_pointer(task,entries,(GDestroyNotify)g_ptr_array_unref);
}
static gboolean visible(GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 DiskBrowser *b=data;gboolean directory=FALSE;gchar *path=NULL;gtk_tree_model_get(model,iter,PATH,&path,DIRECTORY,&directory,-1);
 gboolean match=directory || insight_lookup_filter_match(b->filter,NULL,NULL,path,NULL);g_free(path);return match;
}
static void status(DiskBrowser *b) {
 gchar *label=b->cancelled ? g_strdup(insight_tr("Directory read cancelled")) : (b->loading ? g_strdup(insight_tr("Reading directory…")) : (b->failure ? insight_i18n_printf("Cannot browse directory: %s",b->failure) : insight_i18n_printf("Disk entries: %u%s",b->count,b->limited ? insight_tr(" (directory limit reached)") : "")));gtk_label_set_text(GTK_LABEL(b->status),label);g_free(label);
}
void insight_disk_browser_cancel(GtkWidget *box) {
 DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");if(!b || !b->alive || !b->cancel) return;
 b->generation++;g_cancellable_cancel(b->cancel);g_clear_object(&b->cancel);b->loading=FALSE;b->cancelled=TRUE;status(b);
}
void insight_disk_browser_locale_changed(GtkWidget *box) { DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");if(!b || !b->alive) return;if(b->root) status(b);if(b->kind_toggle) insight_i18n_apply(gtk_widget_get_parent(b->kind_toggle));gtk_widget_queue_draw(b->view); }
static void read_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source;(void)data;GTask *task=G_TASK(result);DiskJob *j=g_task_get_task_data(task);GError *error=NULL;GPtrArray *entries=g_task_propagate_pointer(task,&error);
 GtkWidget *box=g_weak_ref_get(&j->box);DiskBrowser *b=box ? g_object_get_data(G_OBJECT(box),"insight-disk-browser") : NULL;
 if(b && b->alive && b->generation==j->generation) {
  g_clear_object(&b->cancel);b->setting=TRUE;gtk_list_store_clear(b->store);
  if(entries) for(guint i=0;i<entries->len;i++) { DiskEntry *e=g_ptr_array_index(entries,i);GtkTreeIter iter;gtk_list_store_append(b->store,&iter);gtk_list_store_set(b->store,&iter,NAME,e->name,PATH,e->path,DIRECTORY,e->directory,LINK,e->link,-1); }
  b->setting=FALSE;
  b->loading=FALSE;b->cancelled=FALSE;b->count=entries ? entries->len : 0;b->limited=j->limited;g_free(b->failure);b->failure=error ? g_strdup(error->message) : NULL;status(b);
 }
 if(box) g_object_unref(box);
 if(entries) g_ptr_array_unref(entries);
 g_clear_error(&error);
}
static void load(DiskBrowser *b,const gchar *path) {
 if(!b->alive || !path || !*path || !g_utf8_validate(path,-1,NULL) || strlen(path)>16384) return;
 gchar *root=g_canonicalize_filename(path,b->root);b->generation++;if(b->cancel) g_cancellable_cancel(b->cancel);g_clear_object(&b->cancel);b->cancel=g_cancellable_new();
 g_free(b->root);b->root=root;gtk_entry_set_text(GTK_ENTRY(b->path),root);b->setting=TRUE;gtk_list_store_clear(b->store);b->setting=FALSE;b->loading=TRUE;b->cancelled=FALSE;g_clear_pointer(&b->failure,g_free);status(b);
 DiskJob *j=g_new0(DiskJob,1);g_weak_ref_init(&j->box,b->box);j->generation=b->generation;j->root=g_strdup(root);j->hidden=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b->hidden));
 GTask *task=g_task_new(NULL,b->cancel,read_done,NULL);g_task_set_task_data(task,j,job_free);g_task_run_in_thread(task,read_directory);g_object_unref(task);
}
void insight_disk_browser_set_root(GtkWidget *box,const gchar *path) { DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");if(b) load(b,path); }
void insight_disk_browser_resume(GtkWidget *box,const gchar *root) { DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");if(b) load(b,b->root ? b->root : root); }
static void refresh(GtkWidget *widget,gpointer data) { (void)widget;DiskBrowser *b=data;if(b->setting) return;gchar *path=g_strdup(gtk_entry_get_text(GTK_ENTRY(b->path)));load(b,path);g_free(path); }
static void parent(GtkWidget *widget,gpointer data) { (void)widget;DiskBrowser *b=data;if(b->root) { gchar *path=g_path_get_dirname(b->root);load(b,path);g_free(path); } }
static void filter_changed(GtkEditable *entry,gpointer data) {
 DiskBrowser *b=data;GError *error=NULL;InsightLookupFilter *filter=insight_lookup_filter_new(gtk_entry_get_text(GTK_ENTRY(entry)),TRUE,&error);
 if(!filter) { gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(entry)),"error");gtk_widget_set_tooltip_text(GTK_WIDGET(entry),error ? error->message : "Invalid filter");g_clear_error(&error);return; }
 insight_lookup_filter_free(b->filter);b->filter=filter;g_free(b->valid_query);b->valid_query=g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(entry)),"error");gtk_widget_set_tooltip_text(GTK_WIDGET(entry),"Filter loaded files: fragments, glob, ?regex or ~fuzzy; folders stay visible");gtk_tree_model_filter_refilter(b->model);
}
static void selected(GtkTreeSelection *selection,gpointer data) {
 DiskBrowser *b=data;if(b->setting || !b->alive || !b->open) return;GtkTreeModel *model;GtkTreeIter iter;
 if(gtk_tree_selection_get_selected(selection,&model,&iter)) { gchar *path=NULL;gboolean directory=FALSE;gtk_tree_model_get(model,&iter,PATH,&path,DIRECTORY,&directory,-1);if(path && !directory) b->open(path,FALSE,b->data);g_free(path); }
}
static void activated(GtkTreeView *view,GtkTreePath *row,GtkTreeViewColumn *column,gpointer data) {
 (void)column;DiskBrowser *b=data;GtkTreeModel *model=gtk_tree_view_get_model(view);GtkTreeIter iter;if(!b->alive || !gtk_tree_model_get_iter(model,&iter,row)) return;
 gchar *path=NULL;gboolean directory=FALSE;gtk_tree_model_get(model,&iter,PATH,&path,DIRECTORY,&directory,-1);if(directory) load(b,path);else if(path && b->open) b->open(path,TRUE,b->data);g_free(path);
}
static void kind_cell(GtkTreeViewColumn *column,GtkCellRenderer *cell,GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 (void)column;(void)data;gboolean directory=FALSE,link=FALSE;gtk_tree_model_get(model,iter,DIRECTORY,&directory,LINK,&link,-1);g_object_set(cell,"text",insight_tr(link ? "Link" : (directory ? "Folder" : "File")),NULL);
}
static gboolean key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;DiskBrowser *b=data;guint mods=event->state&gtk_accelerator_get_default_mod_mask();
 if(event->keyval==GDK_KEY_F5 && !mods) { refresh(NULL,b);return TRUE; }
 if(event->keyval==GDK_KEY_Up && mods==GDK_MOD1_MASK) { parent(NULL,b);return TRUE; }
 if(mods==GEANY_PRIMARY_MOD_MASK && (event->keyval==GDK_KEY_l || event->keyval==GDK_KEY_f)) { gtk_widget_grab_focus(event->keyval==GDK_KEY_l ? b->path : b->query);return TRUE; }
 return FALSE;
}
static void destroyed(GtkWidget *widget,gpointer data) { (void)widget;DiskBrowser *b=data;b->alive=FALSE;b->generation++;b->open=NULL;if(b->cancel) g_cancellable_cancel(b->cancel); }
static void freed(gpointer data) { DiskBrowser *b=data;g_clear_object(&b->cancel);insight_lookup_filter_free(b->filter);g_object_unref(b->model);g_object_unref(b->store);g_free(b->root);g_free(b->failure);g_free(b->valid_query);g_free(b); }
static void kind_toggled(GtkCheckMenuItem *item,gpointer data) { DiskBrowser *b=data;gtk_tree_view_column_set_visible(b->kind_column,gtk_check_menu_item_get_active(item)); }
void insight_disk_browser_write(GtkWidget *box,GKeyFile *settings) {
 DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");if(!b || !settings) return;const gchar *group="disk-browser";
 g_key_file_set_integer(settings,group,"version",1);g_key_file_set_string(settings,group,"directory",b->root && strlen(b->root)<=16384 ? b->root : "");g_key_file_set_string(settings,group,"query",b->valid_query ? b->valid_query : "");g_key_file_set_boolean(settings,group,"hidden",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b->hidden)));
 g_key_file_set_integer(settings,group,"name-width",CLAMP(gtk_tree_view_column_get_fixed_width(b->name_column),32,2000));g_key_file_set_integer(settings,group,"kind-width",CLAMP(gtk_tree_view_column_get_fixed_width(b->kind_column),32,2000));g_key_file_set_boolean(settings,group,"show-kind",gtk_tree_view_column_get_visible(b->kind_column));g_key_file_set_boolean(settings,group,"kind-first",gtk_tree_view_get_column(GTK_TREE_VIEW(b->view),0)==b->kind_column);
}
static gboolean saved_boolean(GKeyFile *settings,const gchar *key,gboolean fallback) { GError *error=NULL;gboolean value=g_key_file_get_boolean(settings,"disk-browser",key,&error);if(error) { g_error_free(error);return fallback; }return value; }
gboolean insight_disk_browser_read(GtkWidget *box,GKeyFile *settings) {
 DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");const gchar *group="disk-browser";if(!b || !settings || g_key_file_get_integer(settings,group,"version",NULL)!=1) return FALSE;
 insight_disk_browser_cancel(box);b->setting=TRUE;gtk_list_store_clear(b->store);gchar *root=g_key_file_get_string(settings,group,"directory",NULL),*query=g_key_file_get_string(settings,group,"query",NULL);
 if(root && *root && strlen(root)<=16384 && g_utf8_validate(root,-1,NULL) && g_path_is_absolute(root)) { g_free(b->root);b->root=g_canonicalize_filename(root,NULL);gtk_entry_set_text(GTK_ENTRY(b->path),b->root); }
 InsightLookupFilter *filter=query ? insight_lookup_filter_new(query,TRUE,NULL) : NULL;if(filter) { gtk_entry_set_text(GTK_ENTRY(b->query),query);insight_lookup_filter_free(filter); }
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b->hidden),saved_boolean(settings,"hidden",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b->hidden))));
 gint name=g_key_file_get_integer(settings,group,"name-width",NULL),kind=g_key_file_get_integer(settings,group,"kind-width",NULL);if(name>=32 && name<=2000) gtk_tree_view_column_set_fixed_width(b->name_column,name);if(kind>=32 && kind<=2000) gtk_tree_view_column_set_fixed_width(b->kind_column,kind);
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(b->kind_toggle),saved_boolean(settings,"show-kind",gtk_tree_view_column_get_visible(b->kind_column)));
 gtk_tree_view_move_column_after(GTK_TREE_VIEW(b->view),saved_boolean(settings,"kind-first",gtk_tree_view_get_column(GTK_TREE_VIEW(b->view),0)==b->kind_column) ? b->kind_column : b->name_column,NULL);
 b->setting=FALSE;g_free(root);g_free(query);return b->root!=NULL;
}
GtkWidget *insight_disk_browser_new(InsightDiskOpen open,gpointer data) {
 DiskBrowser *b=g_new0(DiskBrowser,1);b->alive=TRUE;b->open=open;b->data=data;b->filter=insight_lookup_filter_new("",TRUE,NULL);b->valid_query=g_strdup("");b->box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_widget_set_name(b->box,"insight-disk-browser");g_object_set_data_full(G_OBJECT(b->box),"insight-disk-browser",b,freed);g_signal_connect(b->box,"destroy",G_CALLBACK(destroyed),b);
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*up=gtk_button_new_with_label("Up"),*go=gtk_button_new_with_label("Refresh");gtk_widget_set_name(up,"insight-disk-up");gtk_widget_set_name(go,"insight-disk-refresh");b->path=gtk_entry_new();gtk_widget_set_name(b->path,"insight-disk-root");gtk_entry_set_max_length(GTK_ENTRY(b->path),16384);gtk_box_pack_start(GTK_BOX(row),up,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),b->path,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(row),go,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(b->box),row,FALSE,FALSE,0);
 b->query=gtk_search_entry_new();gtk_widget_set_name(b->query,"insight-disk-query");gtk_entry_set_max_length(GTK_ENTRY(b->query),1024);gtk_entry_set_placeholder_text(GTK_ENTRY(b->query),"Filter disk files…");gtk_box_pack_start(GTK_BOX(b->box),b->query,FALSE,FALSE,0);
 b->hidden=gtk_check_button_new_with_label("Show hidden disk entries");gtk_widget_set_name(b->hidden,"insight-disk-hidden");gtk_box_pack_start(GTK_BOX(b->box),b->hidden,FALSE,FALSE,0);
 b->store=gtk_list_store_new(COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN,G_TYPE_BOOLEAN);b->model=GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(b->store),NULL));gtk_tree_model_filter_set_visible_func(b->model,visible,b,NULL);b->view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(b->model));gtk_widget_set_name(b->view,"insight-disk-files");b->name_column=gtk_tree_view_column_new_with_attributes("Disk file or folder",gtk_cell_renderer_text_new(),"text",NAME,NULL);gtk_tree_view_append_column(GTK_TREE_VIEW(b->view),b->name_column);GtkCellRenderer *kind=gtk_cell_renderer_text_new();GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes("Kind",kind,NULL);b->kind_column=column;gtk_tree_view_column_set_cell_data_func(column,kind,kind_cell,NULL,NULL);gtk_tree_view_append_column(GTK_TREE_VIEW(b->view),column);gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(b->view),PATH);
 GtkTreeViewColumn *columns[]={b->name_column,b->kind_column};for(guint i=0;i<2;i++) { gtk_tree_view_column_set_sizing(columns[i],GTK_TREE_VIEW_COLUMN_FIXED);gtk_tree_view_column_set_fixed_width(columns[i],i ? 90 : 240);gtk_tree_view_column_set_resizable(columns[i],TRUE);gtk_tree_view_column_set_reorderable(columns[i],TRUE); }
 GtkWidget *menu=gtk_menu_new(),*choose=gtk_menu_button_new();gtk_button_set_label(GTK_BUTTON(choose),"Columns");gtk_widget_set_name(choose,"insight-disk-columns");b->kind_toggle=gtk_check_menu_item_new_with_label("Kind");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(b->kind_toggle),TRUE);gtk_menu_shell_append(GTK_MENU_SHELL(menu),b->kind_toggle);gtk_menu_button_set_popup(GTK_MENU_BUTTON(choose),menu);gtk_box_pack_start(GTK_BOX(row),choose,FALSE,FALSE,0);g_signal_connect(b->kind_toggle,"toggled",G_CALLBACK(kind_toggled),b);g_signal_connect_swapped(menu,"show",G_CALLBACK(insight_i18n_apply),menu);gtk_widget_show_all(menu);

 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),b->view);gtk_box_pack_start(GTK_BOX(b->box),scroll,TRUE,TRUE,0);b->status=gtk_label_new("Choose a disk directory");gtk_widget_set_name(b->status,"insight-disk-status");gtk_label_set_line_wrap(GTK_LABEL(b->status),TRUE);gtk_box_pack_start(GTK_BOX(b->box),b->status,FALSE,FALSE,0);
 g_signal_connect(up,"clicked",G_CALLBACK(parent),b);g_signal_connect(go,"clicked",G_CALLBACK(refresh),b);g_signal_connect(b->path,"activate",G_CALLBACK(refresh),b);g_signal_connect(b->hidden,"toggled",G_CALLBACK(refresh),b);g_signal_connect(b->query,"changed",G_CALLBACK(filter_changed),b);g_signal_connect(b->view,"row-activated",G_CALLBACK(activated),b);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(b->view)),"changed",G_CALLBACK(selected),b);g_signal_connect(b->box,"key-press-event",G_CALLBACK(key),b);filter_changed(GTK_EDITABLE(b->query),b);return b->box;
}
