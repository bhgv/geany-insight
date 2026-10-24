/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-scroll-links.h"
#include "document.h"
#include "editor.h"
#include "geanyobject.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "insight-help.h"
#include "insight-i18n.h"
#include "project.h"
#include "utils.h"
#include "../insight/desktop/glib_compat.h"
#include "../insight/desktop/project_files.h"
#define VIEW_SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define PAIR_LIMIT 64
#define ALL_LIMIT 128

typedef struct { guint source,target;gboolean two_way; gchar *a,*b; gboolean together; guint uid; } ScrollPair;
typedef struct { gint64 row,x; } ViewSample;
static GArray *link_pairs;
static GHashTable *view_samples;
static gboolean link_all,vertical=TRUE,horizontal,propagating,ui_loading;
static GtkWidget *source_choice,*links_view,*remove_link;
static GtkWidget *all_menu,*link_dialog,*source_label,*target_choice,*two_way,*all_check,*vertical_check,*horizontal_check,*links_status;
static GtkListStore *links_model;
static guint dialog_source;
static gboolean target_closed;
static guint next_uid=1,editing_uid,pending_source;
static GtkWidget *together_check;
static gchar *scope;
static gboolean project_loading,presenting;
static guint close_guard;
static gboolean clear_close_guard(gpointer data) { (void)data;close_guard=0;return G_SOURCE_REMOVE; }
static void persist_links(void);
static void links_changed(void);
static void show_dialog(void);
static void present_links(GeanyDocument *doc);
static void restore_panes(void);
static gchar *doc_path(GeanyDocument *doc) { return DOC_VALID(doc) && doc->file_name ? insight_canonicalize_filename(doc->file_name,NULL):NULL; }
static void remove_pair(guint n) { ScrollPair *p=&g_array_index(link_pairs,ScrollPair,n);g_free(p->a);g_free(p->b);g_array_remove_index(link_pairs,n); }
static void bind_paths(void) {
 for(guint i=0;i<link_pairs->len;i++) { ScrollPair *p=&g_array_index(link_pairs,ScrollPair,i);
  for(guint n=0;documents_array && n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n);gchar *path=doc_path(doc);if(path) { if(!g_strcmp0(path,p->a)) p->source=doc->id;if(!g_strcmp0(path,p->b)) p->target=doc->id; }g_free(path); }
 }
}
static GeanyDocument *reopen(guint id,const gchar *path) {
 GeanyDocument *doc=document_find_by_id(id);if(DOC_VALID(doc)) return doc;
 if(!path) return NULL;gchar *native=utils_get_locale_from_utf8(path);doc=document_open_file(native,FALSE,NULL,NULL);g_free(native);return doc;
}
#define LINK_GROUP "insight-source-links"
static void write_links(GKeyFile *key) {
 g_key_file_remove_group(key,LINK_GROUP,NULL);g_key_file_set_integer(key,LINK_GROUP,"version",1);
 guint count=0;gchar *base=project_get_base_path();GFile *root=base ? g_file_new_for_path(base):NULL;
 for(guint n=0;n<link_pairs->len;n++) { ScrollPair *p=&g_array_index(link_pairs,ScrollPair,n);if(!p->a || !p->b) continue;
  const gchar *paths[]={p->a,p->b};const gchar *names[]={"source","target"};
  for(guint k=0;k<2;k++) { GFile *file=g_file_new_for_path(paths[k]);gchar *relative=root ? g_file_get_relative_path(root,file):NULL,*name=g_strdup_printf("%s-%u",names[k],count);g_key_file_set_string(key,LINK_GROUP,name,relative ? relative:paths[k]);g_free(relative);g_free(name);g_object_unref(file); }
  gchar *name=g_strdup_printf("two-way-%u",count);g_key_file_set_boolean(key,LINK_GROUP,name,p->two_way);g_free(name);name=g_strdup_printf("together-%u",count);g_key_file_set_boolean(key,LINK_GROUP,name,p->together);g_free(name);count++;
 }
 g_key_file_set_integer(key,LINK_GROUP,"count",count);g_key_file_set_boolean(key,LINK_GROUP,"vertical",vertical);g_key_file_set_boolean(key,LINK_GROUP,"horizontal",horizontal);if(root) g_object_unref(root);g_free(base);
}
/* Merge only our group, with an etag: never overwrite concurrent project edits. */
static void persist_links(void) {
 if(project_loading || !scope || !app->project || g_strcmp0(scope,app->project->file_name)) return;
 gchar *native=utils_get_locale_from_utf8(scope),*data=NULL,*etag=NULL;gsize length=0;GError *error=NULL;GFile *file=g_file_new_for_path(native);GKeyFile *key=g_key_file_new();
 gboolean ok=g_file_load_contents(file,NULL,&data,&length,&etag,&error);
 if(ok) ok=g_key_file_load_from_data(key,data,length,G_KEY_FILE_KEEP_COMMENTS,&error);g_free(data);data=NULL;
 if(ok) { write_links(key);data=g_key_file_to_data(key,&length,&error);ok=data && g_file_replace_contents(file,data,length,etag,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,&error); }
 if(!ok) ui_set_statusbar(TRUE,"Cannot save source window links: %s",error ? error->message : "invalid project");
 g_clear_error(&error);g_free(data);g_free(etag);g_free(native);g_key_file_unref(key);g_object_unref(file);
}
static void project_opened(GObject *o,GKeyFile *key,gpointer unused) {
 (void)o;(void)unused;project_loading=TRUE;restore_panes();while(link_pairs->len) remove_pair(link_pairs->len-1);g_free(scope);scope=app->project ? g_strdup(app->project->file_name):NULL;editing_uid=pending_source=0;link_all=FALSE;
 gchar *base=project_get_base_path();gint count=CLAMP(g_key_file_get_integer(key,LINK_GROUP,"count",NULL),0,PAIR_LIMIT);
 for(gint n=0;n<count;n++) { ScrollPair p={0};gchar *name=g_strdup_printf("source-%d",n),*a=g_key_file_get_string(key,LINK_GROUP,name,NULL);g_free(name);name=g_strdup_printf("target-%d",n);gchar *b=g_key_file_get_string(key,LINK_GROUP,name,NULL);g_free(name);
  if(a && *a && b && *b) { p.a=insight_canonicalize_filename(a,base);p.b=insight_canonicalize_filename(b,base);name=g_strdup_printf("two-way-%d",n);p.two_way=g_key_file_get_boolean(key,LINK_GROUP,name,NULL);g_free(name);name=g_strdup_printf("together-%d",n);p.together=g_key_file_get_boolean(key,LINK_GROUP,name,NULL);g_free(name);p.uid=next_uid++;if(g_strcmp0(p.a,p.b)) g_array_append_val(link_pairs,p);else {g_free(p.a);g_free(p.b);} }
  g_free(a);g_free(b);
 }
 g_free(base);vertical=!g_key_file_has_key(key,LINK_GROUP,"vertical",NULL) || g_key_file_get_boolean(key,LINK_GROUP,"vertical",NULL);horizontal=g_key_file_get_boolean(key,LINK_GROUP,"horizontal",NULL);bind_paths();project_loading=FALSE;links_changed();
}
static void project_saved(GObject *o,GKeyFile *key,gpointer unused) { (void)o;(void)unused;if(!scope && app->project) scope=g_strdup(app->project->file_name);write_links(key); }
static void project_closed(GObject *o,gpointer unused) { (void)o;(void)unused;persist_links();project_loading=TRUE;restore_panes();while(link_pairs->len) remove_pair(link_pairs->len-1);g_clear_pointer(&scope,g_free);editing_uid=pending_source=0;link_all=FALSE;project_loading=FALSE;links_changed(); }
void insight_scroll_links_membership(const InsightProjectFiles *policy,const gchar *root) {
 if(!link_pairs || !policy || !scope || !app->project || g_strcmp0(scope,app->project->file_name)) return;gboolean changed=FALSE;
 for(guint n=link_pairs->len;n>0;n--) { ScrollPair *p=&g_array_index(link_pairs,ScrollPair,n-1);if((p->a && !insight_project_files_contains(policy,root,p->a)) || (p->b && !insight_project_files_contains(policy,root,p->b))) { remove_pair(n-1);changed=TRUE; } }
 if(changed) { restore_panes();editing_uid=0;persist_links();links_changed(); }
}
/* Actual editor widgets are moved, not copied: syntax, Undo and saving stay native. */
typedef struct { GtkWidget *parent,*content,*placeholder;guint id;gint position;gulong focus; } LinkedPane;
static GArray *panes;
static GtkWidget *pane_window;
static gboolean pane_focus(GtkWidget *widget,GdkEventFocus *event,gpointer data) {
 (void)widget;(void)event;GeanyDocument *doc=document_find_by_id(GPOINTER_TO_UINT(data));if(DOC_VALID(doc)) { gboolean old=presenting;presenting=TRUE;gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.notebook),document_get_notebook_page(doc));presenting=old; }return FALSE;
}
static void restore_panes(void) {
 gboolean old=presenting;presenting=TRUE;
 if(panes) { for(guint n=0;n<panes->len;n++) { LinkedPane *p=&g_array_index(panes,LinkedPane,n);GeanyDocument *doc=document_find_by_id(p->id);if(DOC_VALID(doc) && p->focus) g_signal_handler_disconnect(doc->editor->sci,p->focus);
   GtkWidget *parent=gtk_widget_get_parent(p->content);if(parent) gtk_container_remove(GTK_CONTAINER(parent),p->content);gtk_box_pack_start(GTK_BOX(p->parent),p->content,TRUE,TRUE,0);gtk_box_reorder_child(GTK_BOX(p->parent),p->content,p->position);gtk_widget_destroy(p->placeholder);gtk_widget_show(p->content);g_object_unref(p->content);g_object_unref(p->parent);
  }g_array_unref(panes);panes=NULL; }
 if(pane_window) { GtkWidget *window=pane_window;pane_window=NULL;gtk_widget_destroy(window); }presenting=old;
}
static gboolean pane_delete(GtkWidget *widget,GdkEvent *event,gpointer data) { (void)widget;(void)event;(void)data;restore_panes();return TRUE; }
static void pane_reveal(GtkWidget *widget,gpointer data) { (void)widget;GeanyDocument *doc=document_find_by_id(GPOINTER_TO_UINT(data));if(DOC_VALID(doc)) present_links(doc); }
static void present_links(GeanyDocument *doc) {
 if(project_loading || presenting || !DOC_VALID(doc) || !link_pairs) return;presenting=TRUE;
 GArray *ids=g_array_new(FALSE,FALSE,sizeof(guint));guint first=doc->id;g_array_append_val(ids,first);
 for(guint at=0;at<ids->len;at++) { guint id=g_array_index(ids,guint,at);for(guint n=0;n<link_pairs->len;n++) { ScrollPair *p=&g_array_index(link_pairs,ScrollPair,n);if(!p->together || (p->source!=id && p->target!=id)) continue;
   GeanyDocument *other=p->source==id ? reopen(p->target,p->b):reopen(p->source,p->a);if(!DOC_VALID(other)) continue;bind_paths();gboolean seen=FALSE;for(guint k=0;k<ids->len;k++) if(g_array_index(ids,guint,k)==other->id) seen=TRUE;if(!seen) { guint value=other->id;g_array_append_val(ids,value); }
  } }
 if(ids->len<2) { g_array_unref(ids);presenting=FALSE;return; }
 gboolean same=panes && panes->len==ids->len;if(same) for(guint n=0;n<ids->len;n++) { gboolean found=FALSE;for(guint k=0;k<panes->len;k++) if(g_array_index(panes,LinkedPane,k).id==g_array_index(ids,guint,n)) found=TRUE;if(!found) same=FALSE; }
 if(same) {gtk_window_present(GTK_WINDOW(pane_window));g_array_unref(ids);presenting=FALSE;return;}
 restore_panes();pane_window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(pane_window,"insight-linked-source-windows");gtk_window_set_title(GTK_WINDOW(pane_window),insight_tr("Linked source windows"));gtk_window_set_transient_for(GTK_WINDOW(pane_window),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(pane_window),TRUE);
 GdkRectangle area={0,0,1000,700};GdkDisplay *display=gtk_widget_get_display(main_widgets.window);GdkMonitor *monitor=gdk_display_get_monitor_at_window(display,gtk_widget_get_window(main_widgets.window));if(monitor) gdk_monitor_get_workarea(monitor,&area);gtk_window_set_default_size(GTK_WINDOW(pane_window),MAX(200,area.width*9/10),MAX(150,area.height*8/10));
 GSList *accels=gtk_accel_groups_from_object(G_OBJECT(main_widgets.window));for(GSList *a=accels;a;a=a->next) gtk_window_add_accel_group(GTK_WINDOW(pane_window),a->data);
 panes=g_array_new(FALSE,FALSE,sizeof(LinkedPane));GtkWidget *slot=pane_window;
 for(guint n=0;n<ids->len;n++) { GeanyDocument *d=document_find_by_id(g_array_index(ids,guint,n));GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_widgets.notebook),document_get_notebook_page(d)),*content=GTK_WIDGET(d->editor->sci);while(gtk_widget_get_parent(content) && gtk_widget_get_parent(content)!=page) content=gtk_widget_get_parent(content);if(gtk_widget_get_parent(content)!=page) continue;
  GList *children=gtk_container_get_children(GTK_CONTAINER(page));gint position=g_list_index(children,content);g_list_free(children);LinkedPane pane={g_object_ref(page),g_object_ref(content),gtk_button_new_with_label(insight_tr("Show linked source windows")),d->id,position,0};g_signal_connect(pane.placeholder,"clicked",G_CALLBACK(pane_reveal),GUINT_TO_POINTER(d->id));
  gtk_container_remove(GTK_CONTAINER(page),content);gtk_box_pack_start(GTK_BOX(page),pane.placeholder,TRUE,TRUE,0);gtk_widget_show(pane.placeholder);
  GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2),*label=gtk_label_new(d->file_name ? d->file_name:insight_tr("Untitled"));g_object_set_data(G_OBJECT(label),"insight-no-i18n",GINT_TO_POINTER(1));gtk_label_set_selectable(GTK_LABEL(label),TRUE);gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_MIDDLE);gtk_widget_set_tooltip_text(label,d->file_name);gtk_box_pack_start(GTK_BOX(box),label,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),content,TRUE,TRUE,0);
  if(n+1<ids->len) { GtkWidget *split=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);gtk_container_add(GTK_CONTAINER(slot),split);gtk_paned_pack1(GTK_PANED(split),box,TRUE,TRUE);GtkWidget *rest=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);gtk_paned_pack2(GTK_PANED(split),rest,TRUE,TRUE);gtk_paned_set_position(GTK_PANED(split),MAX(100,area.width*9/10/(gint)ids->len));slot=rest; }else gtk_container_add(GTK_CONTAINER(slot),box);
  pane.focus=g_signal_connect(d->editor->sci,"focus-in-event",G_CALLBACK(pane_focus),GUINT_TO_POINTER(d->id));g_array_append_val(panes,pane);
 }
 g_signal_connect(pane_window,"delete-event",G_CALLBACK(pane_delete),NULL);gtk_widget_show_all(pane_window);gtk_window_present(GTK_WINDOW(pane_window));gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.notebook),document_get_notebook_page(doc));gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));g_array_unref(ids);presenting=FALSE;
}
static void document_activated(GObject *o,GeanyDocument *doc,gpointer unused) { (void)o;(void)unused;if(!close_guard) present_links(doc); }


static ViewSample sample_view(GeanyDocument *doc) {
 return (ViewSample){VIEW_SEND(doc->editor->sci,SCI_GETFIRSTVISIBLELINE,0,0),VIEW_SEND(doc->editor->sci,SCI_GETXOFFSET,0,0)};
}
static void cache_view(GeanyDocument *doc) {
 ViewSample *sample=g_hash_table_lookup(view_samples,GUINT_TO_POINTER(doc->id));
 if(!sample) { sample=g_new(ViewSample,1);g_hash_table_insert(view_samples,GUINT_TO_POINTER(doc->id),sample); }
 *sample=sample_view(doc);
}
static void reset_baselines(void) {
 g_hash_table_remove_all(view_samples);
 for(guint n=0;documents_array && n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n);if(DOC_VALID(doc)) cache_view(doc); }
}
static guint open_count(void) {
 guint count=0;for(guint n=0;documents_array && n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n);if(DOC_VALID(doc)) count++; }return count;
}
static guint selected_target(void) {
 const gchar *id=target_choice ? gtk_combo_box_get_active_id(GTK_COMBO_BOX(target_choice)):NULL;
 if(!id) return 0;gchar *end=NULL;guint64 value=g_ascii_strtoull(id,&end,10);return end && !*end && value<=G_MAXUINT ? (guint)value:0;
}
static gchar *document_label(guint id) {
 GeanyDocument *doc=document_find_by_id(id);if(!DOC_VALID(doc)) return g_strdup(insight_tr("Closed source"));
 return doc->file_name ? g_strdup(doc->file_name):g_strdup_printf("%s #%u",insight_tr("Untitled"),id);
}
static gboolean selected_pair(guint *source,guint *target) {
 if(!links_view) return FALSE;GtkTreeIter row;GtkTreeModel *model;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(links_view)),&model,&row)) return FALSE;gtk_tree_model_get(model,&row,3,source,4,target,-1);guint uid=0;gtk_tree_model_get(model,&row,7,&uid,-1);return uid!=0;
}
static void refresh_controls(void) {
 gboolean loading=ui_loading;ui_loading=TRUE;gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(all_menu),link_all);
 if(link_dialog) {
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(all_check),link_all);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vertical_check),vertical);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(horizontal_check),horizontal);
  if(g_object_get_data(G_OBJECT(link_dialog),"insight-links-rebuild")) {
   guint selected_uid=0;GtkTreeIter selected_row;GtkTreeModel *selected_model;if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(links_view)),&selected_model,&selected_row)) gtk_tree_model_get(selected_model,&selected_row,7,&selected_uid,-1);gtk_list_store_clear(links_model);
   GtkTreeIter draft;gtk_list_store_append(links_model,&draft);gchar *draft_a=document_label(dialog_source),*draft_b=selected_target() ? document_label(selected_target()):g_strdup(insight_tr("Choose target…"));gtk_list_store_set(links_model,&draft,0,draft_a,1,"→",2,draft_b,3,dialog_source,4,selected_target(),5,FALSE,6,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(together_check)),7,0,-1);g_free(draft_a);g_free(draft_b);
   for(guint n=0;n<link_pairs->len;n++) { ScrollPair *pair=&g_array_index(link_pairs,ScrollPair,n);gchar *a=pair->a ? g_strdup(pair->a):document_label(pair->source),*b=pair->b ? g_strdup(pair->b):document_label(pair->target);GtkTreeIter row;gtk_list_store_append(links_model,&row);gtk_list_store_set(links_model,&row,0,a,1,pair->two_way ? "↔":"→",2,b,3,pair->source,4,pair->target,5,pair->two_way,6,pair->together,7,pair->uid,-1);g_free(a);g_free(b);if(pair->uid==(editing_uid ? editing_uid:selected_uid)) {gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(links_view)),&row);GtkTreePath *path=gtk_tree_model_get_path(GTK_TREE_MODEL(links_model),&row);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(links_view),path,NULL,FALSE,0,0);gtk_tree_path_free(path);} }
   g_object_set_data(G_OBJECT(link_dialog),"insight-links-rebuild",NULL);
  }
  gchar *status=insight_i18n_printf(link_all ? "All source windows linked — individual links: %u / %u":"Individual source links: %u / %u",link_pairs->len,PAIR_LIMIT);if(!vertical && !horizontal) { gchar *paused=g_strconcat(insight_tr("Scrolling paused — enable a scrolling axis")," • ",status,NULL);g_free(status);status=paused; }gtk_label_set_text(GTK_LABEL(links_status),status);g_free(status);
  guint target=selected_target(),a,b;gboolean valid=DOC_VALID(document_find_by_id(dialog_source)) && !target_closed && (!target || (target!=dialog_source && DOC_VALID(document_find_by_id(target))));gtk_dialog_set_response_sensitive(GTK_DIALOG(link_dialog),GTK_RESPONSE_APPLY,valid);gtk_widget_set_sensitive(remove_link,selected_pair(&a,&b));
 }
 ui_loading=loading;
}
static void links_changed(void) {if(link_dialog) g_object_set_data(G_OBJECT(link_dialog),"insight-links-rebuild",GINT_TO_POINTER(1));refresh_controls(); }
static void set_all(gboolean active) {
 if(active && open_count()>ALL_LIMIT) { ui_set_statusbar(TRUE,"Link All supports at most 128 open source buffers");refresh_controls();return; }
 link_all=active;reset_baselines();refresh_controls();
}
static void clear_links(void) { restore_panes();while(link_pairs->len) remove_pair(link_pairs->len-1);editing_uid=pending_source=0;link_all=FALSE;g_hash_table_remove_all(view_samples);persist_links();links_changed(); }
static void add_reachable(GArray *ids,GHashTable *seen,guint id) {
 if(!g_hash_table_contains(seen,GUINT_TO_POINTER(id)) && DOC_VALID(document_find_by_id(id))) { g_hash_table_add(seen,GUINT_TO_POINTER(id));g_array_append_val(ids,id); }
}
static void propagate_scroll(guint source,gint64 rows,gint64 pixels) {
 GArray *ids=g_array_new(FALSE,FALSE,sizeof(guint));GHashTable *seen=g_hash_table_new(g_direct_hash,g_direct_equal);add_reachable(ids,seen,source);
 if(link_all) {
  if(open_count()>ALL_LIMIT) { link_all=FALSE;refresh_controls();ui_set_statusbar(TRUE,"Link All disabled: more than 128 source buffers are open"); }
  else for(guint n=0;n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n);if(DOC_VALID(doc)) add_reachable(ids,seen,doc->id); }
 }
 if(!link_all) for(guint at=0;at<ids->len;at++) {
  guint id=g_array_index(ids,guint,at);
  for(guint n=0;n<link_pairs->len;n++) { ScrollPair pair=g_array_index(link_pairs,ScrollPair,n);if(pair.source==id) add_reachable(ids,seen,pair.target);if(pair.two_way && pair.target==id) add_reachable(ids,seen,pair.source); }
 }
 propagating=TRUE;
 for(guint n=1;n<ids->len;n++) {
  GeanyDocument *doc=document_find_by_id(g_array_index(ids,guint,n));if(!DOC_VALID(doc)) continue;ViewSample before=sample_view(doc);
  if(rows) VIEW_SEND(doc->editor->sci,SCI_SETFIRSTVISIBLELINE,(uptr_t)CLAMP(before.row+rows,(gint64)0,(gint64)G_MAXINT),0);
  if(pixels) VIEW_SEND(doc->editor->sci,SCI_SETXOFFSET,(uptr_t)CLAMP(before.x+pixels,(gint64)0,(gint64)G_MAXINT),0);
  cache_view(doc);
 }
 propagating=FALSE;g_hash_table_unref(seen);g_array_unref(ids);
}
void insight_scroll_links_notify(GeanyEditor *editor,SCNotification *nt) {
 if(!view_samples || !link_pairs || (!link_all && !link_pairs->len) || !editor || !DOC_VALID(editor->document) || nt->nmhdr.code!=SCN_UPDATEUI) return;
 GeanyDocument *doc=editor->document;if(editor->sci!=doc->editor->sci) return;
 ViewSample now=sample_view(doc),*previous=g_hash_table_lookup(view_samples,GUINT_TO_POINTER(doc->id));
 if(!previous) { cache_view(doc);return; }
 ViewSample old=*previous;*previous=now;if(propagating) return;
 gint64 rows=vertical && (nt->updated&SC_UPDATE_V_SCROLL) ? now.row-old.row:0,pixels=horizontal && (nt->updated&SC_UPDATE_H_SCROLL) ? now.x-old.x:0;
 if((rows || pixels) && (link_all || link_pairs->len)) propagate_scroll(doc->id,rows,pixels);
}
static void target_changed(GtkComboBox *combo,gpointer data) { (void)combo;(void)data;if(!ui_loading) { target_closed=FALSE;if(link_dialog) g_object_set_data(G_OBJECT(link_dialog),"insight-links-rebuild",GINT_TO_POINTER(1));refresh_controls(); } }
static void all_menu_changed(GtkCheckMenuItem *item,gpointer data) { (void)data;if(!ui_loading) set_all(gtk_check_menu_item_get_active(item)); }
static void all_dialog_changed(GtkToggleButton *item,gpointer data) { (void)data;if(!ui_loading && link_dialog) set_all(gtk_toggle_button_get_active(item)); }
static void axes_changed(GtkToggleButton *item,gpointer data) {
 (void)item;(void)data;if(ui_loading || !link_dialog || !vertical_check || !horizontal_check) return;vertical=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vertical_check));horizontal=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(horizontal_check));reset_baselines();persist_links();refresh_controls();
}
static void populate_sources(guint skip) {
 gboolean loading=ui_loading;ui_loading=TRUE;gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(source_choice));
 for(guint n=0;documents_array && n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n);if(!DOC_VALID(doc) || doc->id==skip) continue;gchar *id=g_strdup_printf("%u",doc->id),*label=document_label(doc->id);gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(source_choice),id,label);g_free(id);g_free(label); }
 gchar *id=g_strdup_printf("%u",dialog_source);gtk_combo_box_set_active_id(GTK_COMBO_BOX(source_choice),id);g_free(id);ui_loading=loading;
}
static void populate_target(gboolean preserve,guint skip) {
 guint selected=preserve ? selected_target():0;gboolean reverse=preserve && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(two_way));gboolean loading=ui_loading;ui_loading=TRUE;
 if(!preserve) for(guint n=0;n<link_pairs->len;n++) { ScrollPair pair=g_array_index(link_pairs,ScrollPair,n);if(pair.source==dialog_source) { selected=pair.target;reverse=pair.two_way;break; } }
 gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(target_choice));gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target_choice),"0",insight_tr("Choose target…"));
 for(guint n=0;documents_array && n<documents_array->len;n++) { GeanyDocument *target=g_ptr_array_index(documents_array,n);if(!DOC_VALID(target) || target->id==dialog_source || target->id==skip) continue;gchar *id=g_strdup_printf("%u",target->id),*label=document_label(target->id);gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target_choice),id,label);g_free(id);g_free(label); }
 gchar *id=g_strdup_printf("%u",selected),*label=dialog_source ? document_label(dialog_source):g_strdup(insight_tr("Source closed — Refresh to choose the current editor"));if(!gtk_combo_box_set_active_id(GTK_COMBO_BOX(target_choice),id)) gtk_combo_box_set_active_id(GTK_COMBO_BOX(target_choice),"0");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(two_way),reverse);gtk_label_set_text(GTK_LABEL(source_label),label);gtk_widget_set_tooltip_text(source_label,label);g_free(id);g_free(label);ui_loading=loading;
}
static void capture_current(void) { GeanyDocument *doc=document_get_current();dialog_source=DOC_VALID(doc) ? doc->id:0;editing_uid=0;target_closed=FALSE;populate_sources(0);populate_target(FALSE,0);for(guint n=0;n<link_pairs->len;n++) {ScrollPair *p=&g_array_index(link_pairs,ScrollPair,n);if(p->source==dialog_source && p->target==selected_target()) {editing_uid=p->uid;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(together_check),p->together);break;}}links_changed(); }
static void source_changed(GtkComboBox *combo,gpointer data) {
 (void)data;if(ui_loading || !link_dialog || !target_choice) return;const gchar *id=gtk_combo_box_get_active_id(combo);dialog_source=id ? (guint)g_ascii_strtoull(id,NULL,10):0;editing_uid=0;target_closed=FALSE;populate_target(FALSE,0);links_changed();
}
static void pair_selected(GtkTreeSelection *selection,gpointer data) {
 (void)selection;(void)data;if(ui_loading) return;guint source,target;if(!selected_pair(&source,&target)) {refresh_controls();return;}if(selected_pair(&source,&target)) { GtkTreeIter row;GtkTreeModel *model;gtk_tree_selection_get_selected(selection,&model,&row);gtk_tree_model_get(model,&row,7,&editing_uid,-1);
 for(guint n=0;n<link_pairs->len;n++) { ScrollPair *p=&g_array_index(link_pairs,ScrollPair,n);if(p->uid!=editing_uid) continue;gboolean was=ui_loading;ui_loading=TRUE;gboolean old=presenting;presenting=TRUE;GeanyDocument *a=reopen(p->source,p->a),*b=reopen(p->target,p->b);presenting=old;bind_paths();ui_loading=was;if(!DOC_VALID(a) || !DOC_VALID(b)) {ui_set_statusbar(TRUE,"Linked source file cannot be opened");return;}source=a->id;target=b->id;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(together_check),p->together);break; }
 dialog_source=source;target_closed=FALSE;populate_sources(0);populate_target(FALSE,0);gchar *id=g_strdup_printf("%u",target);gboolean was=ui_loading;ui_loading=TRUE;gtk_combo_box_set_active_id(GTK_COMBO_BOX(target_choice),id);g_free(id);for(guint n=0;n<link_pairs->len;n++) {ScrollPair *p=&g_array_index(link_pairs,ScrollPair,n);if(p->uid==editing_uid) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(two_way),p->two_way);}ui_loading=was; }g_object_set_data(G_OBJECT(link_dialog),"insight-links-rebuild",GINT_TO_POINTER(1));refresh_controls();
}
static void row_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) { (void)path;(void)column;(void)data;pair_selected(gtk_tree_view_get_selection(view),NULL); }
static void remove_selected(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;guint source,target;if(!selected_pair(&source,&target)) return;for(guint n=0;n<link_pairs->len;n++) { ScrollPair pair=g_array_index(link_pairs,ScrollPair,n);if(pair.source==source && pair.target==target) { remove_pair(n);break; } }editing_uid=0;restore_panes();reset_baselines();populate_target(FALSE,0);persist_links();links_changed();
}
static void locale_changed(gpointer data) { (void)data;if(!link_dialog) return;populate_sources(0);populate_target(TRUE,0);links_changed(); }
static void apply_pair(void) {
 guint target=selected_target();if(!DOC_VALID(document_find_by_id(dialog_source)) || target_closed || (target && (target==dialog_source || !DOC_VALID(document_find_by_id(target))))) { gtk_label_set_text(GTK_LABEL(links_status),insight_tr("Source or target closed; Refresh before linking"));gtk_dialog_set_response_sensitive(GTK_DIALOG(link_dialog),GTK_RESPONSE_APPLY,FALSE);return; }
 gint existing=-1;for(guint n=0;n<link_pairs->len;n++) { ScrollPair *p=&g_array_index(link_pairs,ScrollPair,n);if(p->uid==editing_uid) existing=(gint)n;
  if(target && ((p->source==dialog_source && p->target==target) || (p->target==dialog_source && p->source==target)) && p->uid!=editing_uid) { editing_uid=p->uid;links_changed();pair_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(links_view)),NULL);return; }
 }
 if(target && existing<0 && link_pairs->len>=PAIR_LIMIT) { gtk_label_set_text(GTK_LABEL(links_status),insight_tr("At most 64 individual source links are supported"));return; }
 if(existing>=0) remove_pair((guint)existing);
 if(target) { ScrollPair pair={dialog_source,target,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(two_way)),doc_path(document_find_by_id(dialog_source)),doc_path(document_find_by_id(target)),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(together_check)),editing_uid ? editing_uid:next_uid++};editing_uid=pair.uid;g_array_append_val(link_pairs,pair); }
 reset_baselines();persist_links();links_changed();restore_panes();present_links(document_find_by_id(dialog_source));
}
static void dialog_destroyed(GtkWidget *widget,gpointer data) { (void)widget;(void)data;link_dialog=NULL;source_choice=links_view=remove_link=NULL;source_label=target_choice=two_way=all_check=vertical_check=horizontal_check=links_status=NULL;together_check=NULL;pending_source=0;g_clear_object(&links_model); }
static void dialog_response(GtkDialog *dialog,gint response,gpointer data) {
 (void)data;if(response==GTK_RESPONSE_APPLY) apply_pair();else if(response==GTK_RESPONSE_HELP) capture_current();else if(response==1) clear_links();else gtk_widget_destroy(GTK_WIDGET(dialog));
}
static void cell_editable(GtkTreeViewColumn *column,GtkCellRenderer *cell,GtkTreeModel *model,GtkTreeIter *row,gpointer data) {
 (void)column;(void)data;guint uid;gtk_tree_model_get(model,row,7,&uid,-1);g_object_set(cell,"editable",uid==0,NULL);
}
typedef struct { guint column;gchar *id; } CellChoice;
static gboolean commit_cell_choice(gpointer data) {
 CellChoice *choice=data;if(link_dialog) gtk_combo_box_set_active_id(GTK_COMBO_BOX(choice->column==0 ? source_choice:target_choice),choice->id);return G_SOURCE_REMOVE;
}
static void free_cell_choice(gpointer data) { CellChoice *choice=data;g_free(choice->id);g_free(choice); }
static void cell_changed(GtkCellRendererCombo *cell,gchar *path,GtkTreeIter *iter,gpointer data) {
 (void)cell;(void)path;CellChoice *choice=g_new0(CellChoice,1);choice->column=GPOINTER_TO_UINT(data);GtkComboBox *combo=GTK_COMBO_BOX(choice->column==0 ? source_choice:target_choice);gtk_tree_model_get(gtk_combo_box_get_model(combo),iter,1,&choice->id,-1);g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,commit_cell_choice,choice,free_cell_choice);
}
static void together_toggled(GtkCellRendererToggle *cell,gchar *path,gpointer data) {
 (void)cell;(void)data;GtkTreeIter row;if(!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(links_model),&row,path)) return;guint uid;gboolean active;gtk_tree_model_get(GTK_TREE_MODEL(links_model),&row,7,&uid,6,&active,-1);
 if(!uid) {gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(together_check),!active);gtk_list_store_set(links_model,&row,6,!active,-1);return;}
 for(guint n=0;n<link_pairs->len;n++) { ScrollPair *p=&g_array_index(link_pairs,ScrollPair,n);if(p->uid==uid) {p->together=!active;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(together_check),p->together);break;} }persist_links();links_changed();restore_panes();present_links(document_get_current());
}
static void show_dialog(void) {
 if(!link_dialog) {
  link_dialog=gtk_dialog_new_with_buttons("Link Source Windows",GTK_WINDOW(main_widgets.window),GTK_DIALOG_DESTROY_WITH_PARENT,"Close",GTK_RESPONSE_CLOSE,"Refresh",GTK_RESPONSE_HELP,"Unlink All",1,"Apply Link",GTK_RESPONSE_APPLY,NULL);gtk_widget_set_name(link_dialog,"insight-scroll-links-dialog");gtk_window_set_default_size(GTK_WINDOW(link_dialog),730,480);gtk_window_set_resizable(GTK_WINDOW(link_dialog),TRUE);
  GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(link_dialog)),*row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_box_set_spacing(GTK_BOX(box),5);
  GtkWidget *description=gtk_label_new(insight_tr("Choose Source and Target in the first row. Show together opens linked editors side by side."));gtk_label_set_line_wrap(GTK_LABEL(description),TRUE);gtk_label_set_xalign(GTK_LABEL(description),0);gtk_box_pack_start(GTK_BOX(box),insight_panel_help_overlay(description,"scroll-links.md"),FALSE,FALSE,0);
  gtk_box_pack_start(GTK_BOX(row),gtk_label_new(insight_tr("Source")),FALSE,FALSE,0);source_choice=gtk_combo_box_text_new();gtk_widget_set_name(source_choice,"insight-scroll-links-source-choice");g_object_set_data(G_OBJECT(source_choice),"insight-no-i18n",GINT_TO_POINTER(1));insight_i18n_key(G_OBJECT(source_choice),"tooltip-text","Choose the source buffer without changing the active editor.");GList *source_cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(source_choice));for(GList *c=source_cells;c;c=c->next) if(GTK_IS_CELL_RENDERER_TEXT(c->data)) g_object_set(c->data,"ellipsize",PANGO_ELLIPSIZE_MIDDLE,"width-chars",1,NULL);g_list_free(source_cells);gtk_box_pack_start(GTK_BOX(row),source_choice,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);gtk_widget_set_no_show_all(row,TRUE);row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
  source_label=gtk_label_new("");gtk_widget_set_name(source_label,"insight-scroll-links-source");g_object_set_data(G_OBJECT(source_label),"insight-no-i18n",GINT_TO_POINTER(1));gtk_label_set_selectable(GTK_LABEL(source_label),TRUE);gtk_label_set_ellipsize(GTK_LABEL(source_label),PANGO_ELLIPSIZE_MIDDLE);gtk_box_pack_start(GTK_BOX(box),source_label,FALSE,FALSE,0);gtk_widget_set_no_show_all(source_label,TRUE);
  gtk_box_pack_start(GTK_BOX(row),gtk_label_new(insight_tr("Target")),FALSE,FALSE,0);target_choice=gtk_combo_box_text_new();g_object_set_data(G_OBJECT(target_choice),"insight-no-i18n",GINT_TO_POINTER(1));insight_i18n_key(G_OBJECT(target_choice),"tooltip-text","Choose the target buffer in the first table row.");gtk_widget_set_name(target_choice,"insight-scroll-links-target");GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(target_choice));for(GList *c=cells;c;c=c->next) if(GTK_IS_CELL_RENDERER_TEXT(c->data)) g_object_set(c->data,"ellipsize",PANGO_ELLIPSIZE_MIDDLE,"width-chars",1,NULL);g_list_free(cells);two_way=gtk_check_button_new_with_label("Two Way");gtk_widget_set_name(two_way,"insight-scroll-links-two-way");gtk_widget_set_tooltip_text(two_way,insight_tr("Two Way lets either file drive scrolling; otherwise only the source drives the target."));gtk_box_pack_start(GTK_BOX(row),target_choice,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(row),two_way,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);gtk_widget_set_no_show_all(row,TRUE);
  row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);together_check=gtk_check_button_new_with_label("Show together");gtk_widget_set_name(together_check,"insight-scroll-links-together");gtk_box_pack_start(GTK_BOX(row),together_check,FALSE,FALSE,0);gtk_widget_set_no_show_all(together_check,TRUE);g_object_ref(two_way);gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(two_way)),two_way);gtk_box_pack_start(GTK_BOX(row),two_way,FALSE,FALSE,0);g_object_unref(two_way);all_check=gtk_check_button_new_with_label("Link All Source Windows");vertical_check=gtk_check_button_new_with_label("Vertical scrolling");horizontal_check=gtk_check_button_new_with_label("Horizontal scrolling");GtkWidget *axes_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);GtkWidget *options[]={all_check,vertical_check,horizontal_check};const gchar *names[]={"insight-scroll-links-all","insight-scroll-links-vertical","insight-scroll-links-horizontal"};for(guint n=0;n<3;n++) { gtk_widget_set_name(options[n],names[n]);gtk_box_pack_start(GTK_BOX(n==0 ? row:axes_row),options[n],FALSE,FALSE,0); }gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),axes_row,FALSE,FALSE,0);gtk_widget_set_tooltip_text(gtk_dialog_get_widget_for_response(GTK_DIALOG(link_dialog),GTK_RESPONSE_HELP),insight_tr("Use the current editor as source and refresh both file lists."));
  links_model=gtk_list_store_new(8,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_UINT,G_TYPE_UINT,G_TYPE_BOOLEAN,G_TYPE_BOOLEAN,G_TYPE_UINT);GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(links_model)),*scroll=gtk_scrolled_window_new(NULL,NULL);links_view=view;gtk_widget_set_name(view,"insight-scroll-links-list");gtk_widget_set_tooltip_text(view,insight_tr("Select a link to edit its source, target and direction, then Apply Link."));g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),"changed",G_CALLBACK(pair_selected),NULL);g_signal_connect(view,"row-activated",G_CALLBACK(row_activated),NULL);const gchar *titles[]={"Source","Direction","Target"};for(guint n=0;n<3;n++) { GtkCellRenderer *cell=n==1 ? gtk_cell_renderer_text_new():gtk_cell_renderer_combo_new();g_object_set(cell,"ellipsize",PANGO_ELLIPSIZE_MIDDLE,NULL);if(n!=1) {g_object_set(cell,"model",gtk_combo_box_get_model(GTK_COMBO_BOX(n==0 ? source_choice:target_choice)),"text-column",0,"has-entry",FALSE,NULL);g_signal_connect(cell,"changed",G_CALLBACK(cell_changed),GUINT_TO_POINTER(n));}GtkTreeViewColumn *col=gtk_tree_view_column_new_with_attributes(titles[n],cell,"text",n,NULL);if(n!=1) gtk_tree_view_column_set_cell_data_func(col,cell,cell_editable,NULL,NULL);gtk_tree_view_column_set_resizable(col,TRUE);gtk_tree_view_column_set_expand(col,n!=1);gtk_tree_view_append_column(GTK_TREE_VIEW(view),col); }GtkCellRenderer *toggle=gtk_cell_renderer_toggle_new();GtkTreeViewColumn *together_col=gtk_tree_view_column_new_with_attributes("Show together",toggle,"active",6,NULL);gtk_tree_view_append_column(GTK_TREE_VIEW(view),together_col);g_signal_connect(toggle,"toggled",G_CALLBACK(together_toggled),NULL);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
  remove_link=gtk_button_new_with_label(insight_tr("Remove selected link"));gtk_widget_set_name(remove_link,"insight-scroll-links-remove-selected");gtk_widget_set_tooltip_text(remove_link,insight_tr("Remove only the selected pair; other links remain active."));gtk_box_pack_start(GTK_BOX(box),remove_link,FALSE,FALSE,0);g_signal_connect(remove_link,"clicked",G_CALLBACK(remove_selected),NULL);
  links_status=gtk_label_new("");gtk_widget_set_name(links_status,"insight-scroll-links-status");gtk_label_set_line_wrap(GTK_LABEL(links_status),TRUE);gtk_box_pack_start(GTK_BOX(box),links_status,FALSE,FALSE,0);insight_i18n_on_refresh(G_OBJECT(link_dialog),locale_changed,NULL);
  g_signal_connect(source_choice,"changed",G_CALLBACK(source_changed),NULL);g_signal_connect(target_choice,"changed",G_CALLBACK(target_changed),NULL);g_signal_connect(all_check,"toggled",G_CALLBACK(all_dialog_changed),NULL);g_signal_connect(vertical_check,"toggled",G_CALLBACK(axes_changed),NULL);g_signal_connect(horizontal_check,"toggled",G_CALLBACK(axes_changed),NULL);g_signal_connect(link_dialog,"response",G_CALLBACK(dialog_response),NULL);g_signal_connect(link_dialog,"destroy",G_CALLBACK(dialog_destroyed),NULL);
 }
 if(!pending_source) capture_current();else {populate_sources(0);populate_target(TRUE,0);links_changed();}gtk_widget_show_all(link_dialog);gtk_window_present(GTK_WINDOW(link_dialog));
}
static void context_link(GtkWidget *widget,gpointer data) {
 (void)widget;GeanyDocument *doc=document_find_by_id(GPOINTER_TO_UINT(data));if(!DOC_VALID(doc)) return;
 if(!pending_source || !DOC_VALID(document_find_by_id(pending_source))) {pending_source=doc->id;show_dialog();dialog_source=doc->id;editing_uid=0;populate_sources(0);populate_target(TRUE,0);gtk_combo_box_set_active_id(GTK_COMBO_BOX(target_choice),"0");links_changed();GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(links_view),path,NULL,TRUE);gtk_tree_path_free(path);return;}
 show_dialog();if(doc->id==pending_source) return;dialog_source=pending_source;editing_uid=0;populate_sources(0);populate_target(TRUE,0);gchar *id=g_strdup_printf("%u",doc->id);gtk_combo_box_set_active_id(GTK_COMBO_BOX(target_choice),id);g_free(id);apply_pair();pending_source=0;
}
void insight_scroll_links_add_popup(GtkWidget *menu,guint document_id) {
 if(!link_pairs || !DOC_VALID(document_find_by_id(document_id))) return;GtkWidget *item=gtk_menu_item_new_with_label(insight_tr("Link source window…"));gtk_widget_set_name(item,"insight-scroll-links-context");g_signal_connect(item,"activate",G_CALLBACK(context_link),GUINT_TO_POINTER(document_id));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item);
}
static void editor_menu_updated(GObject *object,const gchar *word,gint position,GeanyDocument *doc,gpointer data) {
 (void)object;(void)word;(void)position;(void)data;GList *items=gtk_container_get_children(GTK_CONTAINER(main_widgets.editor_menu));for(GList *i=items;i;i=i->next) if(!g_strcmp0(gtk_widget_get_name(i->data),"insight-scroll-links-context")) gtk_widget_destroy(i->data);g_list_free(items);if(DOC_VALID(doc)) insight_scroll_links_add_popup(main_widgets.editor_menu,doc->id);
}
void insight_scroll_links_command(guint action) { if(!link_pairs) return;if(action==0) show_dialog();else if(action==1) set_all(!link_all);else if(action==2) clear_links(); }
static void menu_command(GtkWidget *widget,gpointer data) { (void)widget;insight_scroll_links_command(GPOINTER_TO_UINT(data)); }
static void document_opened(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object;(void)data;if(!view_samples || !DOC_VALID(doc)) return;bind_paths();g_hash_table_remove(view_samples,GUINT_TO_POINTER(doc->id));
 if(link_all && open_count()>ALL_LIMIT) { set_all(FALSE);ui_set_statusbar(TRUE,"Link All disabled: more than 128 source buffers are open"); }
 if(link_dialog) { populate_sources(0);populate_target(TRUE,0);refresh_controls(); }
}
static void document_saved(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object;(void)data;if(!view_samples || !DOC_VALID(doc)) return;for(guint n=0;n<link_pairs->len;n++) { ScrollPair *p=&g_array_index(link_pairs,ScrollPair,n);if(p->source==doc->id) {g_free(p->a);p->a=doc_path(doc);}if(p->target==doc->id) {g_free(p->b);p->b=doc_path(doc);} }persist_links();cache_view(doc);if(link_dialog) { populate_sources(0);populate_target(TRUE,0);links_changed(); }
}
static void document_closed(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object;(void)data;if(!view_samples) return;if(!close_guard) close_guard=g_idle_add(clear_close_guard,NULL);restore_panes();g_hash_table_remove(view_samples,GUINT_TO_POINTER(doc->id));
 for(guint n=link_pairs->len;n>0;n--) { ScrollPair pair=g_array_index(link_pairs,ScrollPair,n-1);if(pair.source==doc->id || pair.target==doc->id) { if(!pair.a || !pair.b) remove_pair(n-1);else {ScrollPair *p=&g_array_index(link_pairs,ScrollPair,n-1);if(p->source==doc->id) p->source=0;if(p->target==doc->id) p->target=0;} } }
 if(link_dialog) { if(dialog_source==doc->id) { dialog_source=0;gtk_label_set_text(GTK_LABEL(source_label),insight_tr("Source closed — Refresh to choose the current editor")); }if(selected_target()==doc->id) target_closed=TRUE; }
 if(link_dialog) {gboolean old=ui_loading;ui_loading=TRUE;gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(links_view)));editing_uid=0;ui_loading=old;populate_sources(doc->id);populate_target(TRUE,doc->id); }links_changed();
}
static void menu_destroyed(GtkWidget *widget,gpointer data) { (void)widget;(void)data;ui_loading=TRUE;if(close_guard) {g_source_remove(close_guard);close_guard=0;}persist_links();restore_panes();if(link_dialog) gtk_widget_destroy(link_dialog);link_all=FALSE;while(link_pairs->len) remove_pair(link_pairs->len-1);g_clear_pointer(&scope,g_free);g_clear_pointer(&link_pairs,g_array_unref);g_clear_pointer(&view_samples,g_hash_table_unref);all_menu=NULL;ui_loading=FALSE; }
void insight_scroll_links_init(GtkWidget *menu) {
 link_pairs=g_array_new(FALSE,FALSE,sizeof(ScrollPair));view_samples=g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,g_free);
 GtkWidget *item=gtk_menu_item_new_with_label("Link Source Windows…");gtk_widget_set_name(item,"insight-scroll-links-command");g_object_set_data(G_OBJECT(item),"insight-window-persistent",GINT_TO_POINTER(1));keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_LINKWINDOW,item);g_signal_connect(item,"activate",G_CALLBACK(menu_command),GUINT_TO_POINTER(0));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item);
 all_menu=gtk_check_menu_item_new_with_label("Link All Source Windows");gtk_widget_set_name(all_menu,"insight-scroll-links-all-command");g_object_set_data(G_OBJECT(all_menu),"insight-window-persistent",GINT_TO_POINTER(1));keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_LINKALLWINDOWS,all_menu);g_signal_connect(all_menu,"toggled",G_CALLBACK(all_menu_changed),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),all_menu);gtk_widget_show(all_menu);
 item=gtk_menu_item_new_with_label("Unlink All Source Windows");gtk_widget_set_name(item,"insight-scroll-links-clear-command");g_object_set_data(G_OBJECT(item),"insight-window-persistent",GINT_TO_POINTER(1));keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_UNLINKWINDOWS,item);g_signal_connect(item,"activate",G_CALLBACK(menu_command),GUINT_TO_POINTER(2));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item);
 GtkWidget *separator=gtk_separator_menu_item_new();g_object_set_data(G_OBJECT(separator),"insight-window-persistent",GINT_TO_POINTER(1));gtk_menu_shell_append(GTK_MENU_SHELL(menu),separator);gtk_widget_show(separator);
 g_signal_connect_object(geany_object,"document-new",G_CALLBACK(document_opened),all_menu,G_CONNECT_AFTER);g_signal_connect_object(geany_object,"document-open",G_CALLBACK(document_opened),all_menu,G_CONNECT_AFTER);g_signal_connect_object(geany_object,"document-close",G_CALLBACK(document_closed),all_menu,0);g_signal_connect_object(geany_object,"document-save",G_CALLBACK(document_saved),all_menu,G_CONNECT_AFTER);g_signal_connect_object(geany_object,"document-activate",G_CALLBACK(document_activated),all_menu,0);g_signal_connect_object(geany_object,"update-editor-menu",G_CALLBACK(editor_menu_updated),all_menu,0);g_signal_connect_object(geany_object,"project-open",G_CALLBACK(project_opened),all_menu,0);g_signal_connect_object(geany_object,"project-save",G_CALLBACK(project_saved),all_menu,0);g_signal_connect_object(geany_object,"project-close",G_CALLBACK(project_closed),all_menu,0);g_signal_connect(all_menu,"destroy",G_CALLBACK(menu_destroyed),NULL);
 if(app->project) { GKeyFile *key=g_key_file_new();gchar *path=utils_get_locale_from_utf8(app->project->file_name);g_key_file_load_from_file(key,path,G_KEY_FILE_NONE,NULL);project_opened(NULL,key,NULL);g_free(path);g_key_file_unref(key); }
}
