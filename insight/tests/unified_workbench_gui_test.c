/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "documentprivate.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "search.h"
#include "sciwrappers.h"
#include "sidebar.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window;static gchar *path,*profile;static guint phase;
static const gchar *source="// needle first\nclass Box { public: int value; int run() { return value; } };\nint main() { Box box; return box.run(); }\n// needle last\n";
static GtkWidget *named(GtkWidget *root,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;if(GTK_IS_MENU_BUTTON(root)) { GtkWidget *popover=GTK_WIDGET(gtk_menu_button_get_popover(GTK_MENU_BUTTON(root)));if(popover) { GtkWidget *found=named(popover,name);if(found) return found; } }if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL; }
static gboolean find_tag(GtkTreeModel *model,GtkTreeIter *parent,const gchar *name,guint *depth,guint level) { GtkTreeIter iter;for(gboolean ok=gtk_tree_model_iter_children(model,&iter,parent);ok;ok=gtk_tree_model_iter_next(model,&iter)) { TMTag *tag=NULL;gtk_tree_model_get(model,&iter,SYMBOLS_COLUMN_TAG,&tag,-1);gboolean match=tag && !g_strcmp0(tag->name,name);if(tag) tm_tag_unref(tag);if(match) { *depth=level;return TRUE; }if(find_tag(model,&iter,name,depth,level+1)) return TRUE; }return FALSE; }
static gboolean timeout(gpointer data) { (void)data;g_error("Unified workbench timeout, phase %u",phase);return G_SOURCE_REMOVE; }
static void toggle(const gchar *name,gboolean active) { GtkWidget *widget=named(window,name);g_assert_nonnull(widget);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),active); }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { document_open_file(path,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 GtkWidget *tree=named(window,"insight-file-structure"),*query=named(window,"insight-file-search-query"),*mode=named(window,"insight-file-symbol-mode");
 if(!tree || !mode || !doc->has_tags) return G_SOURCE_CONTINUE;
 if(phase==1) {
  guint box_depth=0,run_depth=0;GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(tree));g_assert_true(find_tag(model,NULL,"Box",&box_depth,0));g_assert_true(find_tag(model,NULL,"run",&run_depth,0));g_assert_cmpuint(run_depth,>,box_depth);
  g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(mode)),==,0);g_assert_false(gtk_widget_get_visible(ui_lookup_widget(window,"entry_tagfilter")));g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(main_widgets.sidebar_notebook)),==,2);
  sci_set_current_position(doc->editor->sci,0,FALSE);search_show_find_dialog();g_assert_true(gtk_widget_get_visible(named(window,"insight-editor-search-panel")));gtk_entry_set_text(GTK_ENTRY(query),"needle");gtk_button_clicked(GTK_BUTTON(named(window,"insight-file-search-next")));g_assert_cmpint(sci_get_selection_start(doc->editor->sci),==,3);
  search_find_again(FALSE);gint last=(gint)(strrchr(source,'/')-source)+2;g_assert_cmpint(sci_get_selection_start(doc->editor->sci),==,last);search_find_again(TRUE);g_assert_cmpint(sci_get_selection_start(doc->editor->sci),==,3);
  toggle("insight-file-search-wrap",FALSE);sci_set_current_position(doc->editor->sci,sci_get_length(doc->editor->sci),FALSE);search_find_again(FALSE);g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,sci_get_length(doc->editor->sci));
  toggle("insight-file-search-wrap",TRUE);toggle("insight-file-search-select",FALSE);sci_set_current_position(doc->editor->sci,0,FALSE);search_find_again(FALSE);g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,3);search_find_again(FALSE);g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,last);
  toggle("insight-file-search-select",TRUE);sci_set_current_position(doc->editor->sci,0,FALSE);toggle("insight-file-search-incremental",TRUE);gtk_entry_set_text(GTK_ENTRY(query),"Box");g_assert_cmpint(sci_get_selection_start(doc->editor->sci),==,(gint)(strstr(source,"Box")-source));toggle("insight-file-search-incremental",FALSE);
  gtk_combo_box_set_active(GTK_COMBO_BOX(mode),1);g_assert_cmpstr(gtk_stack_get_visible_child_name(GTK_STACK(named(window,"insight-file-symbol-stack"))),==,"details");gtk_combo_box_set_active(GTK_COMBO_BOX(mode),0);gtk_entry_set_text(GTK_ENTRY(named(window,"insight-local-query")),"run");phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) { guint depth=0;g_assert_true(find_tag(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),NULL,"run",&depth,0));gtk_entry_set_text(GTK_ENTRY(named(window,"insight-local-query")),"");gtk_tree_view_expand_all(GTK_TREE_VIEW(tree));gtk_entry_set_text(GTK_ENTRY(query),"needle");phase++;return G_SOURCE_CONTINUE; }
 if(phase==3) {
  const gchar *capture=g_getenv("INSIGHT_UNIFIED_CAPTURE");if(capture) { GdkWindow *native=gtk_widget_get_window(window);GdkPixbuf *image=gdk_pixbuf_get_from_window(native,0,0,gdk_window_get_width(native),gdk_window_get_height(native));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,capture,"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
  gchar *text=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(text,==,source);g_free(text);g_assert_false(doc->changed);g_print("Unified File Search and File Symbols: native hierarchy, one page/query, F3 directions, wrap, caret-only, incremental and details mode passed.\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("unified-workbench-%u",g_random_int()),*directory=g_build_filename(cwd,name,NULL);g_free(cwd);g_free(name);profile=g_build_filename(directory,"profile",NULL);path=g_build_filename(directory,"sample.cpp",NULL);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);g_assert_true(g_file_set_contents(path,source,-1,NULL));gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);gchar *argv[]={"unified-workbench","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(directory);g_free(profile);g_free(path);return result;
}
