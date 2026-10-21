/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window,*view;static GeanyDocument *a,*b;static gchar *root,*path_a,*path_b,*sub;static guint phase,confirm_mode,confirm_count;static gint64 after;
static const gchar *original_a="int old = 1; // old\nint old_two = old;\n",*original_b="int old = 3;\n",*replaced_a="int new = 1; // old\nint old_two = old;\n",*replaced_b="int new = 3;\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(menu) { GtkWidget *v=named(menu,name);if(v) return v; } }if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *v=NULL;for(GList *p=list;p&&!v;p=p->next) v=named(p->data,name);g_list_free(list);return v; }return NULL; }
static GtkWidget *top(const gchar *name) { GList *list=gtk_window_list_toplevels();GtkWidget *v=NULL;for(GList *p=list;p;p=p->next) if(gtk_widget_get_visible(p->data)&&!g_strcmp0(gtk_widget_get_name(p->data),name)) v=p->data;g_list_free(list);return v; }
static void click(const gchar *name) { GtkWidget *w=named(view,name);g_assert_nonnull(w);g_assert_true(gtk_widget_get_sensitive(w));gtk_button_clicked(GTK_BUTTON(w)); }
static void entry(const gchar *name,const gchar *value) { gtk_entry_set_text(GTK_ENTRY(named(view,name)),value); }
static void toggle(const gchar *name,gboolean value) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(view,name)),value); }
static void source_is(GeanyDocument *doc,const gchar *expected) { gchar *s=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(s,==,expected);g_free(s); }
static gint files(void) { return gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(named(view,"insight-replace-files-list"))),NULL); }
static void choose_a(void) { GtkTreeView *tree=GTK_TREE_VIEW(named(view,"insight-replace-files-list"));GtkTreeModel *m=gtk_tree_view_get_model(tree);GtkTreeIter it;for(gboolean more=gtk_tree_model_get_iter_first(m,&it);more;more=gtk_tree_model_iter_next(m,&it)) { gchar *path=NULL;gtk_tree_model_get(m,&it,1,&path,-1);gboolean found=!g_strcmp0(path,path_a);g_free(path);if(found) { GtkTreePath *p=gtk_tree_model_get_path(m,&it);gtk_tree_view_set_cursor(tree,p,NULL,FALSE);gtk_tree_path_free(p);return; } }g_error("Missing a.c"); }
static void skip_second_hit(void) { choose_a();GtkTreeViewColumn *column=gtk_tree_view_get_column(GTK_TREE_VIEW(named(view,"insight-replace-files-hits")),0);GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));g_assert_nonnull(cells);g_signal_emit_by_name(cells->data,"toggled","1");g_list_free(cells); }
static gboolean respond(gpointer p) { (void)p;GtkWidget *d=top("insight-replace-files-confirm");if(!d) return G_SOURCE_CONTINUE;gint response=confirm_count++==0 ? (confirm_mode==1 ? GTK_RESPONSE_YES:GTK_RESPONSE_NO):(confirm_mode==1 ? GTK_RESPONSE_CANCEL:GTK_RESPONSE_YES);gtk_dialog_response(GTK_DIALOG(d),response);return G_SOURCE_CONTINUE; }
static gboolean step(gpointer p) {
 (void)p;if(view&&g_object_get_data(G_OBJECT(view),"insight-replace-files-busy")) return G_SOURCE_CONTINUE;
 switch(phase) {
 case 0:
  a=document_get_current();if(!a||g_strcmp0(a->file_name,path_a)) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(a->editor->sci));gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-replace-files-menu")));view=top("insight-replace-files-window");g_assert_nonnull(view);entry("insight-replace-files-query","old");entry("insight-replace-files-new","new");entry("insight-replace-files-masks","*.c");gtk_combo_box_set_active(GTK_COMBO_BOX(named(view,"insight-replace-files-region")),2);toggle("insight-replace-files-option-1",TRUE);phase++;break;
 case 1:
  click("insight-replace-files-preview");phase++;break;
 case 2:
  if(files()<2) { phase=1;break; }g_assert_cmpint(files(),==,2);choose_a();g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(named(view,"insight-replace-files-hits"))),NULL),==,2);skip_second_hit();
  { ScintillaObject *before=SCINTILLA(named(view,"insight-replace-files-before")),*after_code=SCINTILLA(named(view,"insight-replace-files-after"));g_assert_true(SEND(before,SCI_GETREADONLY,0,0));g_assert_true(SEND(after_code,SCI_GETREADONLY,0,0));gchar *text=sci_get_contents(after_code,-1);g_assert_cmpstr(text,==,replaced_a);g_free(text);g_assert_cmpint(SEND(before,SCI_GETMARGINWIDTHN,1,0),>,0);g_assert_cmpint(SEND(before,SCI_GETSTYLEAT,0,0),!=,SEND(before,SCI_GETSTYLEAT,strstr(original_a,"//")-original_a,0)); }
  g_assert_true(g_file_set_contents(path_b,"int later = 9;\n",-1,NULL));click("insight-replace-files-apply");source_is(a,original_a);g_assert_false(gtk_widget_get_sensitive(named(view,"insight-replace-files-apply")));g_assert_true(g_file_set_contents(path_b,original_b,-1,NULL));click("insight-replace-files-preview");phase++;break;
 case 3:
  skip_second_hit();toggle("insight-replace-files-confirm-option",TRUE);confirm_mode=1;confirm_count=0;click("insight-replace-files-apply");g_assert_cmpuint(confirm_count,==,2);source_is(a,original_a);g_assert_null(document_find_by_filename(path_b));confirm_mode=2;confirm_count=0;click("insight-replace-files-apply");g_assert_cmpuint(confirm_count,==,2);source_is(a,original_a);b=document_find_by_filename(path_b);g_assert_nonnull(b);source_is(b,replaced_b);SEND(b->editor->sci,SCI_UNDO,0,0);source_is(b,original_b);toggle("insight-replace-files-confirm-option",FALSE);click("insight-replace-files-preview");phase++;break;
 case 4:
  skip_second_hit();SEND(a->editor->sci,SCI_SETREADONLY,TRUE,0);click("insight-replace-files-apply");source_is(a,original_a);source_is(b,original_b);SEND(a->editor->sci,SCI_SETREADONLY,FALSE,0);click("insight-replace-files-preview");phase++;break;
 case 5:
  skip_second_hit();after=g_get_monotonic_time()+200*G_TIME_SPAN_MILLISECOND;phase++;break;
 case 6:
  if(g_get_monotonic_time()<after) { break; }
  if(g_getenv("GEANY_REPLACE_FILES_CAPTURE")) { GdkPixbuf *im=gdk_pixbuf_get_from_window(gtk_widget_get_window(view),0,0,gtk_widget_get_allocated_width(view),gtk_widget_get_allocated_height(view));g_assert_nonnull(im);g_assert_true(gdk_pixbuf_save(im,g_getenv("GEANY_REPLACE_FILES_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(im); }
  click("insight-replace-files-diff");{ GtkWidget *d=top("insight-diff-window");g_assert_nonnull(d);g_assert_false(gtk_widget_get_visible(named(d,"insight-diff-ai-apply")));
   gtk_widget_destroy(d); }
  click("insight-replace-files-apply");source_is(a,replaced_a);source_is(b,replaced_b);SEND(a->editor->sci,SCI_UNDO,0,0);SEND(b->editor->sci,SCI_UNDO,0,0);source_is(a,original_a);source_is(b,original_b);
  { gchar *disk=NULL;g_assert_true(g_file_get_contents(path_a,&disk,NULL,NULL));g_assert_cmpstr(disk,==,original_a);g_free(disk);g_assert_true(g_file_get_contents(path_b,&disk,NULL,NULL));g_assert_cmpstr(disk,==,original_b);g_free(disk); }
  gtk_combo_box_set_active(GTK_COMBO_BOX(named(view,"insight-replace-files-scope")),4);entry("insight-replace-files-folder",root);toggle("insight-replace-files-recursive",TRUE);click("insight-replace-files-preview");phase++;break;
 case 7:
  g_assert_cmpint(files(),==,3);entry("insight-replace-files-new","other");g_assert_false(gtk_widget_get_sensitive(named(view,"insight-replace-files-apply")));click("insight-replace-files-preview");click("insight-replace-files-stop");phase++;break;
 case 8:
  g_assert_false(gtk_widget_get_sensitive(named(view,"insight-replace-files-apply")));gtk_widget_destroy(view);view=NULL;keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_REPLACEFILES);view=top("insight-replace-files-window");g_assert_nonnull(view);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(view,"insight-replace-files-query"))),==,"old");g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(view,"insight-replace-files-new"))),==,"new");entry("insight-replace-files-query","old");entry("insight-replace-files-new","new");gtk_combo_box_set_active(GTK_COMBO_BOX(named(view,"insight-replace-files-scope")),1);click("insight-replace-files-preview");phase++;break;
 case 9:
  g_assert_cmpint(files(),==,2);gtk_widget_destroy(view);view=NULL;document_set_text_changed(a,FALSE);document_set_text_changed(b,FALSE);g_print("Replace Files native GUI: menu/keys, real indexed project, lexical filtering, masks, colored/numbered read-only previews, individual choices, stale/readonly all-source preflight, per-file Skip/Cancel, one Undo per buffer, unchanged disk, folder recursion, invalidation and preview cancellation passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer p) { (void)p;g_error("Replace Files timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS")||!g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("replace-files-native-XXXXXX",NULL);root=g_build_filename(dir,"source",NULL);g_assert_cmpint(g_mkdir(root,0700),==,0);sub=g_build_filename(root,"sub",NULL);g_assert_cmpint(g_mkdir(sub,0700),==,0);path_a=g_build_filename(root,"a.c",NULL);path_b=g_build_filename(root,"b.c",NULL);gchar *nested=g_build_filename(sub,"nested.c",NULL),*profile=g_build_filename(dir,"profile",NULL),*project=g_build_filename(dir,"fixture.geany",NULL);g_assert_true(g_file_set_contents(path_a,original_a,-1,NULL));g_assert_true(g_file_set_contents(path_b,original_b,-1,NULL));g_assert_true(g_file_set_contents(nested,"int old = 4;\n",-1,NULL));gchar *settings=g_strdup_printf("[project]\nname=Replace Files fixture\nbase_path=%s\n[insight-project-files]\nversion=1\nautomatic=true\nexclude-masks=\nfiles=\ntrees=\nexcluded-files=\nexcluded-trees=%s;\n",root,sub);g_assert_true(g_file_set_contents(project,settings,-1,NULL));g_free(settings);
 gchar *args[]={"replace-files-native","--new-instance","--no-session","--config",profile,project,path_a,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(40,respond,NULL);g_timeout_add(100,step,NULL);g_timeout_add_seconds(50,timeout,NULL);return main_lib(7,args);
}
