/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*query,*mode,*indirect,*view,*status;static gchar *source;static guint phase;
static GtkWidget *named(GtkWidget *root,const char *name) {
 if(!strcmp(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;
  for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);
  g_list_free(children);return found; }
 return NULL;
}
static gboolean has(const char *name) {
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter it;
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&it);valid;valid=gtk_tree_model_iter_next(model,&it)) {
  gchar *symbol=NULL;gtk_tree_model_get(model,&it,7,&symbol,-1);gboolean match=!g_strcmp0(symbol,name);g_free(symbol);if(match) return TRUE;
 }
 return FALSE;
}
static guint count(void) { return gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL); }
static void request(const char *owner,gint relation,gboolean transitive) {
 gtk_combo_box_set_active(GTK_COMBO_BOX(mode),relation);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(indirect),transitive);
 gtk_entry_set_text(GTK_ENTRY(query),owner);g_signal_emit_by_name(query,"activate");
}
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;
 if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { document_open_file(source,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(!query) { query=named(window,"insight-relation-query");mode=named(window,"insight-relation-mode");indirect=named(window,"insight-relation-transitive");view=named(window,"insight-relations");status=named(window,"insight-relation-status");g_assert_nonnull(indirect);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(indirect))); }
 if(phase==1) { request("N::End",4,FALSE);phase++; }
 else if(phase==2) { if(!has("Middle") || count()!=1) return G_SOURCE_CONTINUE;g_assert_true(gtk_widget_get_sensitive(indirect));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(indirect),TRUE);phase++; }
 else if(phase==3) { if(!has("Root") || count()!=2) return G_SOURCE_CONTINUE;request("N::Root",5,TRUE);phase++; }
 else if(phase==4) { if(!has("Middle") || !has("End") || count()!=2) return G_SOURCE_CONTINUE;request("N::End",2,TRUE);phase++; }
 else if(phase==5) { if(!has("root_member") || !has("middle_member") || !has("end_member") || count()!=3) return G_SOURCE_CONTINUE;g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(status)),"inherited members"));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(indirect),FALSE);phase++; }
 else if(phase==6) { if(!has("end_member") || count()!=1) return G_SOURCE_CONTINUE;request("N::LoopA",4,TRUE);phase++; }
 else if(phase==7) { if(!has("LoopB") || count()!=1) return G_SOURCE_CONTINUE;request("Root",4,TRUE);phase++; }
 else if(phase==8) { if(count() || !strstr(gtk_label_get_text(GTK_LABEL(status)),"ambiguous")) return G_SOURCE_CONTINUE;gtk_combo_box_set_active(GTK_COMBO_BOX(mode),0);g_assert_false(gtk_widget_get_sensitive(indirect));phase++; }
 else { g_print("Transitive Relations GUI: bases, derived, inherited members, cycle, ambiguity, toggle and persistence passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Transitive Relations GUI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("transitive-gui-%u",g_random_int()),*directory=g_build_filename(cwd,name,NULL),*profile=g_build_filename(directory,"profile",NULL),*sd=g_build_filename(profile,"insight",NULL),*settings=g_build_filename(sd,"workbench.conf",NULL);
 g_assert_cmpint(g_mkdir_with_parents(sd,0700),==,0);source=g_build_filename(directory,"types.cpp",NULL);
 g_assert_true(g_file_set_contents(settings,"[workbench]\nversion=1\nfollow=false\nrelation-follow=false\nrelation-transitive=true\nproject-visible=true\n",-1,NULL));
 g_assert_true(g_file_set_contents(source,"namespace N {\nclass Root { public: int root_member; };\nclass Middle : public Root { public: int middle_member; };\nclass End : public Middle { public: int end_member; };\nclass LoopA : public LoopB {};\nclass LoopB : public LoopA {};\n}\nnamespace Other { class Root {}; }\n",-1,NULL));
 gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
 gchar *argv[]={"transitive-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(35,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);
 GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_file(key,settings,G_KEY_FILE_NONE,NULL));g_assert_true(g_key_file_get_boolean(key,"workbench","relation-transitive",NULL));g_key_file_unref(key);
 g_free(cwd);g_free(name);g_free(directory);g_free(profile);g_free(sd);g_free(settings);g_free(config);g_free(source);return result;
}
