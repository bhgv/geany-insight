/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "sciwrappers.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window,*routes,*canvas;static GeanyDocument *a,*b;static gchar *directory,*project_file,*path_a,*path_b;static guint phase;static gboolean restore;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) {GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(sub) {GtkWidget *out=named(sub,name);if(out) return out;}}
 if(GTK_IS_CONTAINER(w)) {GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *out=NULL;for(GList *p=children;p && !out;p=p->next) out=named(p->data,name);g_list_free(children);return out;}return NULL;
}
static void command(const gchar *name) {GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item));}
static void button(const gchar *name) {GtkWidget *item=named(routes,name);g_assert_nonnull(item);gtk_button_clicked(GTK_BUTTON(item));}
static guint count(const gchar *key) {return GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(routes),key));}
static void counts(guint nodes,guint edges) {if(count("insight-route-node-count")!=nodes || count("insight-route-edge-count")!=edges) g_printerr("Source graph phase %u: %s\n",phase,gtk_label_get_text(GTK_LABEL(named(routes,"insight-route-status"))));g_assert_cmpuint(count("insight-route-node-count"),==,nodes);g_assert_cmpuint(count("insight-route-edge-count"),==,edges);}
static void click(gdouble y) {GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS);event->button.button=1;event->button.x=50;event->button.y=y;gboolean handled=FALSE;g_signal_emit_by_name(canvas,"button-press-event",event,&handled);gdk_event_free(event);g_assert_true(handled);}
static void disk_counts(gint nodes,gint edges) {GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_file(key,project_file,G_KEY_FILE_NONE,NULL));g_assert_cmpint(g_key_file_get_integer(key,"insight-source-routes","nodes",NULL),==,nodes);g_assert_cmpint(g_key_file_get_integer(key,"insight-source-routes","edges",NULL),==,edges);g_key_file_unref(key);}
static void quit(void) {GeanyDocument *doc=document_get_current();if(DOC_VALID(doc)) document_set_text_changed(doc,FALSE);if(DOC_VALID(a)) document_set_text_changed(a,FALSE);if(DOC_VALID(b)) document_set_text_changed(b,FALSE);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));}

static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));if(!named(window,"insight-route-show-command")) return G_SOURCE_CONTINUE;
 if(!phase) {
  command("insight-route-show-command");routes=named(window,"insight-source-routes");g_assert_nonnull(routes);canvas=named(routes,"insight-route-canvas");g_assert_nonnull(canvas);gtk_window_resize(GTK_WINDOW(window),1400,900);
  if(restore) {counts(2,1);click(130);g_assert_cmpstr(document_get_current()->file_name,==,path_b);g_print("Source routes restored from the project in a fresh process\n");quit();return G_SOURCE_REMOVE;}
  a=document_open_file(path_a,FALSE,NULL,NULL);g_assert_nonnull(a);SEND(a->editor->sci,SCI_GOTOPOS,sci_get_position_from_line(a->editor->sci,2)+1,0);command("insight-route-capture-command");counts(1,0);command("insight-route-capture-command");counts(1,0);
  b=document_open_file(path_b,FALSE,NULL,NULL);g_assert_nonnull(b);SEND(b->editor->sci,SCI_GOTOPOS,sci_get_position_from_line(b->editor->sci,1)+5,0);command("insight-route-capture-command");counts(2,0);
 } else if(phase==1) {
  for(GtkWidget *p=gtk_widget_get_parent(routes);p;p=gtk_widget_get_parent(p)) if(GTK_IS_PANED(p) && gtk_orientable_get_orientation(GTK_ORIENTABLE(p))==GTK_ORIENTATION_HORIZONTAL) {gtk_paned_set_position(GTK_PANED(p),MAX(100,gtk_widget_get_allocated_width(p)-500));break;}
  click(40);g_assert_true(document_get_current()==a);g_assert_cmpint(sci_get_current_line(a->editor->sci),==,2);button("insight-route-link");click(130);counts(2,1);disk_counts(2,1);click(40);button("insight-route-link");click(130);counts(2,1);click(130);g_assert_true(document_get_current()==b);g_assert_cmpint(sci_get_current_line(b->editor->sci),==,1);
  button("insight-route-remove");counts(1,0);disk_counts(1,0);command("insight-route-capture-command");counts(2,0);click(40);button("insight-route-link");click(130);counts(2,1);disk_counts(2,1);
  /* A closed destination reopens. A unique full-line anchor follows edits. */
  g_assert_true(document_close(b));click(130);b=document_get_current();g_assert_cmpstr(b->file_name,==,path_b);SEND(b->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"// prefix\n");click(130);g_assert_cmpint(sci_get_current_line(b->editor->sci),==,2);SEND(b->editor->sci,SCI_UNDO,0,0);document_set_text_changed(b,FALSE);
  click(40);button("insight-route-function");g_assert_cmpuint(count("insight-route-node-count"),>=,9);g_assert_cmpuint(count("insight-route-edge-count"),>=,10);
  SEND(a->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"// changed\n");SEND(a->editor->sci,SCI_GOTOPOS,3,0);click(40);g_assert_cmpint(sci_get_current_position(a->editor->sci),==,3);SEND(a->editor->sci,SCI_UNDO,0,0);document_set_text_changed(a,FALSE);
  gint start=sci_get_position_from_line(a->editor->sci,2),end=sci_get_position_from_line(a->editor->sci,3);SEND(a->editor->sci,SCI_SETSEL,start,end);button("insight-route-selection");counts(5,5);
  gtk_combo_box_set_active(GTK_COMBO_BOX(named(routes,"insight-route-mode")),0);counts(2,1);gtk_combo_box_set_active(GTK_COMBO_BOX(named(routes,"insight-route-mode")),1);counts(5,5);
 } else if(phase==2) {
  g_assert_false(gtk_widget_get_visible(named(window,"insight-relation-toolbar-tabs")));gtk_notebook_set_current_page(GTK_NOTEBOOK(named(routes,"insight-source-routes-options")),3);
  gtk_entry_set_text(GTK_ENTRY(named(routes,"insight-call-from")),"main");gtk_entry_set_text(GTK_ENTRY(named(routes,"insight-call-to")),"create_rule");button("insight-call-find");if(!g_object_get_data(G_OBJECT(routes),"insight-call-search-running")) return G_SOURCE_CONTINUE;
 } else if(phase==3) {
  if(!g_object_get_data(G_OBJECT(routes),"insight-call-search-completed")) return G_SOURCE_CONTINUE;counts(4,3);g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(named(routes,"insight-call-variants"))),NULL),==,1);
  click(316);g_assert_true(document_get_current()==b);g_assert_cmpint(sci_get_current_line(b->editor->sci),==,2);button("insight-call-set-to");click(40);button("insight-call-set-from");button("insight-call-find");if(!g_object_get_data(G_OBJECT(routes),"insight-call-search-running")) {phase=2;return G_SOURCE_CONTINUE;}
 } else if(phase==4) {
  if(!g_object_get_data(G_OBJECT(routes),"insight-call-search-completed")) return G_SOURCE_CONTINUE;counts(4,3);click(316);button("insight-call-site");g_assert_true(document_get_current()==a);g_assert_cmpint(sci_get_current_line(a->editor->sci),==,0);
  SEND(a->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"// changed call snapshot\n");SEND(a->editor->sci,SCI_GOTOPOS,3,0);click(224);g_assert_cmpint(sci_get_current_position(a->editor->sci),==,3);SEND(a->editor->sci,SCI_UNDO,0,0);document_set_text_changed(a,FALSE);gtk_combo_box_set_active(GTK_COMBO_BOX(named(routes,"insight-call-variants")),-1);gtk_combo_box_set_active(GTK_COMBO_BOX(named(routes,"insight-call-variants")),0);
 } else {
  g_assert_false(gtk_widget_get_visible(named(window,"insight-relation-toolbar-tabs")));
  const gchar *capture=g_getenv("INSIGHT_SOURCE_ROUTES_CAPTURE");if(capture) {GdkWindow *native=gtk_widget_get_window(window);GdkPixbuf *image=gdk_pixbuf_get_from_window(native,0,0,gdk_window_get_width(native),gdk_window_get_height(native));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,capture,"jpeg",NULL,"quality","90",NULL));g_object_unref(image);}
  g_print("Source routes GUI: arbitrary files, duplicate guards, arrows, navigation, reopen, unique anchors, persistence, guarded function/selection flow, automatic main-to-create_rule chains, selected endpoints and call-site navigation passed\n");quit();return G_SOURCE_REMOVE;
 }
 phase++;return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) {(void)data;g_error("Source routes timeout phase %u",phase);return G_SOURCE_REMOVE;}
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;restore=argc==3 && !strcmp(argv[1],"--restore");directory=restore ? g_strdup(argv[2]):g_dir_make_tmp("source-routes-XXXXXX",NULL);project_file=g_build_filename(directory,"routes.geany",NULL);path_a=g_build_filename(directory,"a.c",NULL);path_b=g_build_filename(directory,"b.c",NULL);gchar *profile=g_build_filename(directory,"profile",NULL);g_mkdir_with_parents(profile,0700);
 if(!restore) {const gchar *source="int helper(int x) { struct options opts = {.port=1}; return create_rule(x); }\nint run(int x) {\n if (x > 0) { x = helper(x); } else { x = 0; }\n while (x > 1) { if (x == 3) break; --x; }\n return x;\n}\nint main(void) { return run(1); }\n";g_assert_true(g_file_set_contents(path_a,source,-1,NULL));g_assert_true(g_file_set_contents(path_b,"// destination α\nint target = 2;\nint create_rule(int x) { return x; }\n",-1,NULL));GKeyFile *key=g_key_file_new();g_key_file_set_string(key,"project","name","Source routes");g_key_file_set_string(key,"project","base_path",directory);gsize n;gchar *bytes=g_key_file_to_data(key,&n,NULL);g_assert_true(g_file_set_contents(project_file,bytes,n,NULL));g_free(bytes);g_key_file_unref(key);}
 gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\nload_session=false\n",-1,NULL));gchar *args[]={"source-routes","--new-instance","--no-session","--config",profile,project_file,path_a,NULL};g_timeout_add(350,step,NULL);g_timeout_add_seconds(35,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(7,args);
 if(!restore && !result) {gchar *child[]={argv[0],"--restore",directory,NULL};gint status=0;GError *error=NULL;g_assert_true(g_spawn_sync(NULL,child,NULL,0,NULL,NULL,NULL,NULL,&status,&error));g_assert_no_error(error);g_assert_true(g_spawn_check_wait_status(status,NULL));}
 g_free(config);g_free(profile);g_free(project_file);g_free(path_a);g_free(path_b);g_free(directory);return result;
}
