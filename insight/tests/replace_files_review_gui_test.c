/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *view,*window;
static GeanyDocument *a,*b;
static gchar *root,*path_a,*path_b;
static guint phase,mode,prompts;
static gboolean captured;
static gint64 shown;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=list;p&&!found;p=p->next) found=named(p->data,name);g_list_free(list);return found; }
 return NULL;
}
static GtkWidget *top(const gchar *name) {
 GList *list=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=list;p;p=p->next) if(gtk_widget_get_visible(p->data)&&!g_strcmp0(gtk_widget_get_name(p->data),name)) found=p->data;g_list_free(list);return found;
}
static void click(const gchar *name) {
 GtkWidget *w=named(view,name);g_assert_nonnull(w);if(!gtk_widget_get_sensitive(w)) g_error("Disabled %s at phase %u",name,phase);gtk_button_clicked(GTK_BUTTON(w));
}
static void entry(const gchar *name,const gchar *value) { gtk_entry_set_text(GTK_ENTRY(named(view,name)),value); }
static void toggle(const gchar *name,gboolean value) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(view,name)),value); }
static void text_is(GeanyDocument *d,const gchar *value) { gchar *s=sci_get_contents(d->editor->sci,-1);g_assert_cmpstr(s,==,value);g_free(s); }
static GtkTreeModel *file_model(void) { return gtk_tree_view_get_model(GTK_TREE_VIEW(named(view,"insight-replace-files-list"))); }
static void choose(const gchar *file,gint hit) {
 GtkTreeModel *model=file_model();GtkTreeIter it;gboolean found=FALSE;
 for(gboolean more=gtk_tree_model_get_iter_first(model,&it);more;more=gtk_tree_model_iter_next(model,&it)) {
  gchar *path=NULL;gtk_tree_model_get(model,&it,1,&path,-1);gboolean same=!g_strcmp0(path,file);g_free(path);if(!same) continue;
  GtkTreePath *p=gtk_tree_model_get_path(model,&it);gtk_tree_view_set_cursor(GTK_TREE_VIEW(named(view,"insight-replace-files-list")),p,NULL,FALSE);gtk_tree_path_free(p);found=TRUE;break;
 }
 g_assert_true(found);if(hit>=0) { GtkTreePath *p=gtk_tree_path_new_from_indices(hit,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(named(view,"insight-replace-files-hits")),p,NULL,FALSE);gtk_tree_path_free(p); }
}
static void position_is(const gchar *file,guint hit) {
 GtkTreeModel *model;GtkTreeIter it;GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(named(view,"insight-replace-files-list")));g_assert_true(gtk_tree_selection_get_selected(selection,&model,&it));gchar *path=NULL;gtk_tree_model_get(model,&it,1,&path,-1);g_assert_cmpstr(path,==,file);g_free(path);
 selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(named(view,"insight-replace-files-hits")));g_assert_true(gtk_tree_selection_get_selected(selection,&model,&it));guint index;gtk_tree_model_get(model,&it,5,&index,-1);g_assert_cmpuint(index,==,hit);
}
static void hit_use(guint index,gboolean expected) {
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(named(view,"insight-replace-files-hits")));GtkTreeIter it;g_assert_true(gtk_tree_model_iter_nth_child(model,&it,NULL,index));gboolean use;gtk_tree_model_get(model,&it,0,&use,-1);g_assert_cmpint(use,==,expected);
}
static guint selected_files(void) {
 GtkTreeModel *model=file_model();GtkTreeIter it;guint count=0;for(gboolean more=gtk_tree_model_get_iter_first(model,&it);more;more=gtk_tree_model_iter_next(model,&it)) { gboolean use;gtk_tree_model_get(model,&it,0,&use,-1);count+=use; }return count;
}
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *d=top("insight-replace-files-occurrence-confirm");if(!d) { shown=0;return G_SOURCE_CONTINUE; }
 if(!shown) shown=g_get_monotonic_time();
 if(g_get_monotonic_time()-shown<150*G_TIME_SPAN_MILLISECOND) return G_SOURCE_CONTINUE;
 if(!captured&&g_getenv("GEANY_REPLACE_REVIEW_CAPTURE")) {
  gint x,y;gdk_window_get_origin(gtk_widget_get_window(view),&x,&y);GdkPixbuf *im=gdk_pixbuf_get_from_window(gdk_get_default_root_window(),x,y,gtk_widget_get_allocated_width(view),gtk_widget_get_allocated_height(view));g_assert_nonnull(im);g_assert_true(gdk_pixbuf_save(im,g_getenv("GEANY_REPLACE_REVIEW_CAPTURE"),"jpeg",NULL,"quality","90",NULL));g_object_unref(im);captured=TRUE;
 }
 guint n=prompts++;gint response=GTK_RESPONSE_YES;
 if(mode==1) response=n ? GTK_RESPONSE_CANCEL:GTK_RESPONSE_YES;
 else if(mode==2) response=n ? GTK_RESPONSE_APPLY:GTK_RESPONSE_NO;
 else if(mode==3) { if(!n) SEND(b->editor->sci,SCI_APPENDTEXT,6,(sptr_t)"later\n");response=GTK_RESPONSE_APPLY; }
 else if(mode==4) { toggle("insight-replace-files-option-0",TRUE);response=GTK_RESPONSE_YES; }
 shown=0;gtk_dialog_response(GTK_DIALOG(d),response);return G_SOURCE_CONTINUE;
}
static gboolean step(gpointer data) {
 (void)data;if(view&&g_object_get_data(G_OBJECT(view),"insight-replace-files-busy")) return G_SOURCE_CONTINUE;
 switch(phase) {
 case 0:
  a=document_find_by_filename(path_a);b=document_find_by_filename(path_b);if(!a||!b) break;
  window=gtk_widget_get_toplevel(GTK_WIDGET(a->editor->sci));keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_REPLACEFILES);view=top("insight-replace-files-window");g_assert_nonnull(view);entry("insight-replace-files-query","old");entry("insight-replace-files-new","old_new");click("insight-replace-files-preview");phase++;break;
 case 1:
  g_assert_cmpint(gtk_tree_model_iter_n_children(file_model(),NULL),==,2);choose(path_a,0);toggle("insight-replace-files-confirm-hits",TRUE);
  mode=1;prompts=0;click("insight-replace-files-apply");g_assert_cmpuint(prompts,==,2);text_is(a,"old old\n");text_is(b,"old\n");g_assert_cmpuint(selected_files(),==,2);choose(path_a,0);hit_use(0,TRUE);hit_use(1,TRUE);
  mode=2;prompts=0;click("insight-replace-files-apply");g_assert_cmpuint(prompts,==,2);text_is(a,"old old_new\n");text_is(b,"old_new\n");choose(path_a,0);hit_use(0,FALSE);hit_use(1,TRUE);click("insight-replace-files-next-batch");g_assert_cmpuint(selected_files(),==,0);click("insight-replace-files-all");g_assert_cmpuint(selected_files(),==,0);g_assert_false(gtk_widget_get_sensitive(named(view,"insight-replace-files-apply")));
  choose(path_a,1);click("insight-replace-files-open");g_assert_true(document_get_current()==a);g_assert_true(gtk_widget_get_visible(view));g_assert_cmpint(SEND(a->editor->sci,SCI_GETCURRENTPOS,0,0),==,4);
  SEND(a->editor->sci,SCI_APPENDTEXT,7,(sptr_t)"manual\n");SEND(a->editor->sci,SCI_GOTOPOS,1,0);click("insight-replace-files-open");g_assert_cmpint(SEND(a->editor->sci,SCI_GETCURRENTPOS,0,0),==,1);SEND(a->editor->sci,SCI_UNDO,0,0);SEND(a->editor->sci,SCI_UNDO,0,0);SEND(b->editor->sci,SCI_UNDO,0,0);text_is(a,"old old\n");text_is(b,"old\n");click("insight-replace-files-next-batch");g_assert_cmpuint(selected_files(),==,0);click("insight-replace-files-preview");phase++;break;
 case 2:
  choose(path_a,0);click("insight-replace-files-next");position_is(path_a,1);click("insight-replace-files-next");position_is(path_b,0);click("insight-replace-files-next");position_is(path_a,0);click("insight-replace-files-previous");position_is(path_b,0);
  { GdkEventKey event={0};event.type=GDK_KEY_PRESS;event.keyval=GDK_KEY_F3;gboolean handled=FALSE;g_signal_emit_by_name(view,"key-press-event",&event,&handled);g_assert_true(handled);position_is(path_a,0);event.state=GDK_SHIFT_MASK;g_signal_emit_by_name(view,"key-press-event",&event,&handled);g_assert_true(handled);position_is(path_b,0); }
  choose(path_a,0);mode=3;prompts=0;click("insight-replace-files-apply");g_assert_cmpuint(prompts,==,1);text_is(a,"old old\n");text_is(b,"old\nlater\n");g_assert_false(gtk_widget_get_sensitive(named(view,"insight-replace-files-apply")));SEND(b->editor->sci,SCI_UNDO,0,0);click("insight-replace-files-preview");phase++;break;
 case 3:
  mode=4;prompts=0;click("insight-replace-files-apply");g_assert_cmpuint(prompts,==,1);text_is(a,"old old\n");text_is(b,"old\n");g_assert_false(gtk_widget_get_sensitive(named(view,"insight-replace-files-apply")));
  for(guint i=0;i<129;i++) { gchar *name=g_strdup_printf("bulk-%03u.c",i),*path=g_build_filename(root,name,NULL);g_assert_true(g_file_set_contents(path,"old\n",-1,NULL));g_free(name);g_free(path); }
  toggle("insight-replace-files-confirm-hits",FALSE);gtk_combo_box_set_active(GTK_COMBO_BOX(named(view,"insight-replace-files-scope")),4);entry("insight-replace-files-folder",root);entry("insight-replace-files-masks","*.c");click("insight-replace-files-preview");phase++;break;
 case 4:
  g_assert_cmpint(gtk_tree_model_iter_n_children(file_model(),NULL),==,131);g_assert_cmpuint(selected_files(),==,128);click("insight-replace-files-apply");g_assert_cmpuint(selected_files(),==,0);click("insight-replace-files-next-batch");g_assert_cmpuint(selected_files(),==,3);click("insight-replace-files-apply");click("insight-replace-files-next-batch");g_assert_cmpuint(selected_files(),==,0);text_is(a,"old_new old_new\n");text_is(b,"old_new\n");
  { guint i,count=0;foreach_document(i) { GeanyDocument *d=documents[i];if(!d->file_name||!g_str_has_prefix(d->file_name,root)) continue;count++;if(d!=a) text_is(d,"old_new\n");gchar *disk=NULL;g_assert_true(g_file_get_contents(d->file_name,&disk,NULL,NULL));g_assert_cmpstr(disk,==,d==a ? "old old\n":"old\n");g_free(disk);SEND(d->editor->sci,SCI_UNDO,0,0);text_is(d,d==a ? "old old\n":"old\n");document_set_text_changed(d,FALSE); }g_assert_cmpuint(count,==,131); }
  gtk_widget_destroy(view);view=NULL;g_print("Replace review: staged Cancel/Skip/remaining, source and criteria races, wrapped buttons/F3, editor handoff, immutable applied files, 128+3 batches, one Undo and unchanged disk passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Replacement review timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS")||!g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("replace-review-native-XXXXXX",NULL);root=g_build_filename(dir,"source",NULL);g_assert_cmpint(g_mkdir(root,0700),==,0);path_a=g_build_filename(root,"a.c",NULL);path_b=g_build_filename(root,"b.c",NULL);gchar *profile=g_build_filename(dir,"profile",NULL);g_assert_true(g_file_set_contents(path_a,"old old\n",-1,NULL));g_assert_true(g_file_set_contents(path_b,"old\n",-1,NULL));
 gchar *args[]={"replace-review-native","--new-instance","--no-session","--config",profile,path_a,path_b,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(40,respond,NULL);g_timeout_add(100,step,NULL);g_timeout_add_seconds(85,timeout,NULL);return main_lib(7,args);
}
