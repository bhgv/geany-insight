/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*relations;static gchar *python,*lua;static guint phase;
static GtkWidget *named(GtkWidget *root,const char *name) { if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;if(GTK_IS_MENU_ITEM(root)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(sub) { GtkWidget *f=named(sub,name);if(f) return f; } }if(GTK_IS_CONTAINER(root)) { GList *c=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *f=NULL;for(GList *p=c;p && !f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL; }
static gboolean has(const char *name) { GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(relations));GtkTreeIter it;for(gboolean v=gtk_tree_model_get_iter_first(model,&it);v;v=gtk_tree_model_iter_next(model,&it)) { gchar *value=NULL;gtk_tree_model_get(model,&it,7,&value,-1);gboolean match=!g_strcmp0(value,name);g_free(value);if(match) return TRUE; }return FALSE; }
static void calls(const char *name) { gtk_combo_box_set_active(GTK_COMBO_BOX(named(window,"insight-relation-mode")),6);GtkWidget *entry=named(window,"insight-relation-query");gtk_entry_set_text(GTK_ENTRY(entry),name);g_signal_emit_by_name(entry,"activate"); }
static void select_at(GeanyDocument *doc,gint line) { ScintillaObject *sci=doc->editor->sci;sci_set_current_position(sci,sci_get_position_from_line(sci,line)+6,FALSE);GtkWidget *command=named(window,"insight-select-function");g_assert_nonnull(command);gtk_menu_item_activate(GTK_MENU_ITEM(command)); }
static void selected(GeanyDocument *doc,gint first,gint last) { ScintillaObject *sci=doc->editor->sci;g_assert_cmpint(sci_get_selection_start(sci),==,sci_get_position_from_line(sci,first));g_assert_cmpint(sci_get_selection_end(sci),==,sci_get_line_end_position(sci,last)); }
static guint alpha(GeanyDocument *doc,gint line) {
 GtkWidget *host=gtk_paned_get_child2(GTK_PANED(gtk_widget_get_parent(GTK_WIDGET(doc->editor->sci)))),*map=gtk_bin_get_child(GTK_BIN(host)),*shade=named(host,"insight-overview-markers");g_assert_nonnull(shade);
 gint height=(gint)scintilla_send_message(SCINTILLA(map),SCI_TEXTHEIGHT,0,0),first=(gint)scintilla_send_message(SCINTILLA(map),SCI_GETFIRSTVISIBLELINE,0,0),y=(line-first)*height+height/2;
 if(y<0 || y>=gtk_widget_get_allocated_height(shade)) return 0;
 cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,gtk_widget_get_allocated_width(shade),gtk_widget_get_allocated_height(shade));cairo_t *cr=cairo_create(surface);gtk_widget_draw(shade,cr);cairo_destroy(cr);cairo_surface_flush(surface);guint32 pixel=*(guint32 *)(cairo_image_surface_get_data(surface)+y*cairo_image_surface_get_stride(surface)+40);cairo_surface_destroy(surface);return pixel>>24;
}
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { gtk_window_resize(GTK_WINDOW(window),1100,800);document_open_file(python,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(!relations) relations=named(window,"insight-relations");
 if(phase==1) { select_at(doc,5);selected(doc,4,5);gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-select-enclosing-block")));selected(doc,0,6);phase++; }
 else if(phase==2) { select_at(doc,6);selected(doc,0,6);sci_set_current_position(doc->editor->sci,sci_get_position_from_line(doc->editor->sci,6)+6,FALSE);phase++; }
 else if(phase==3) { if(alpha(doc,6)<200) return G_SOURCE_CONTINUE;g_assert_cmpuint(alpha(doc,8),==,0);calls("inner");phase++; }
 else if(phase==4) { if(!has("nested")) return G_SOURCE_CONTINUE;g_assert_false(has("invoke"));calls("outer");phase++; }
 else if(phase==5) { if(!has("invoke") || !has("nested")) return G_SOURCE_CONTINUE;g_assert_false(has("ignored"));g_assert_false(has("outside"));document_open_file(lua,FALSE,NULL,NULL);phase++; }
 else if(phase==6) { select_at(doc,3);selected(doc,0,7);sci_set_current_position(doc->editor->sci,sci_get_position_from_line(doc->editor->sci,3)+6,FALSE);gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-select-enclosing-block")));selected(doc,3,3);gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-select-enclosing-block")));selected(doc,2,5);sci_set_current_position(doc->editor->sci,sci_get_position_from_line(doc->editor->sci,3)+6,FALSE);calls("outer");phase++; }
 else if(phase==7) { if(!has("invoke") || !has("nested")) return G_SOURCE_CONTINUE;g_assert_false(has("ignored"));g_assert_false(has("outside"));if(alpha(doc,3)<200) return G_SOURCE_CONTINUE;g_assert_cmpuint(alpha(doc,8),==,0);phase++; }
 else { g_print("Python/Lua native GUI: smallest function selection, multiline def, nested Lua blocks, masked strings, lexical Calls and Overview boundaries passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Indented/end functions GUI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("indented-gui-%u",g_random_int()),*directory=g_build_filename(cwd,name,NULL),*profile=g_build_filename(directory,"profile",NULL),*sd=g_build_filename(profile,"insight",NULL),*settings=g_build_filename(sd,"workbench.conf",NULL);g_assert_cmpint(g_mkdir_with_parents(sd,0700),==,0);python=g_build_filename(directory,"functions.py",NULL);lua=g_build_filename(directory,"functions.lua",NULL);
 g_assert_true(g_file_set_contents(settings,"[workbench]\nversion=1\nfollow=false\nrelation-follow=false\nproject-visible=true\noverview-selection=false\noverview-viewport=false\noverview-bookmarks=false\noverview-changes=false\noverview-function=true\noverview-all-functions=false\noverview-scale=50\n",-1,NULL));
 g_assert_true(g_file_set_contents(python,"async def outer(\n    value,\n):\n    text = \"\"\"ignored()\"\"\"\n    def inner():\n        nested()\n    return invoke(value)\n\ndef next_one():\n    outside()\n",-1,NULL));
 g_assert_true(g_file_set_contents(lua,"local function outer(value)\n local text = [[end ignored()]]\n while value do\n  if value then invoke() end\n  repeat value=value-1 until value==0\n end\n return nested(value)\nend\nfunction next_one() outside() end\n",-1,NULL));gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
 gchar *argv[]={"indented-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(300,step,NULL);g_timeout_add_seconds(35,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(cwd);g_free(name);g_free(directory);g_free(profile);g_free(sd);g_free(settings);g_free(config);g_free(python);g_free(lua);return result;
}
