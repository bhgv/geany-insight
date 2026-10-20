/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define sci_set_selection(s,a,b) scintilla_send_message(s,SCI_SETSEL,a,b)
#define sci_set_readonly(s,v) scintilla_send_message(s,SCI_SETREADONLY,v,0)
#define sci_undo(s) scintilla_send_message(s,SCI_UNDO,0,0)
#define sci_redo(s) scintilla_send_message(s,SCI_REDO,0,0)
#define document_grab_focus(d) gtk_widget_grab_focus(GTK_WIDGET((d)->editor->sci))
static GtkWidget *window,*local;static gchar *source;static guint phase;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static void command(const gchar *id) { GtkWidget *item=named(window,id);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void check(ScintillaObject *sci,const gchar *expected) { gchar *text=sci_get_contents(sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void reset(ScintillaObject *sci,const gchar *text,gint caret) { sci_set_readonly(sci,FALSE);sci_set_text(sci,text);scintilla_send_message(sci,SCI_EMPTYUNDOBUFFER,0,0);sci_set_selection(sci,caret,caret); }
static void verify_edits(GeanyDocument *doc,const gchar *text,gint caret,const gchar *symbol,const gchar *rest,const gchar *duplicate) {
 ScintillaObject *sci=doc->editor->sci;reset(sci,text,caret);
 gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"sentinel",-1);
 command("insight-copy-symbol");gchar *copied=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));gchar **parts=g_strsplit(symbol,"\r\n",-1);gchar *normalized=g_strjoinv("\n",parts);g_assert_cmpstr(copied,==,normalized);g_strfreev(parts);g_free(copied);
 check(sci,text);g_assert_cmpint(sci_get_current_position(sci),==,caret);g_assert_false(scintilla_send_message(sci,SCI_CANUNDO,0,0));
 command("insight-duplicate-symbol");check(sci,duplicate);gchar *preserved=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(preserved,==,normalized);g_free(preserved);g_free(normalized);sci_undo(sci);check(sci,text);g_assert_false(scintilla_send_message(sci,SCI_CANUNDO,0,0));sci_redo(sci);check(sci,duplicate);
 reset(sci,text,caret);command("insight-cut-symbol");check(sci,rest);sci_undo(sci);check(sci,text);g_assert_false(scintilla_send_message(sci,SCI_CANUNDO,0,0));sci_redo(sci);check(sci,rest);
 reset(sci,text,caret);sci_set_readonly(sci,TRUE);command("insight-copy-symbol");command("insight-cut-symbol");command("insight-duplicate-symbol");check(sci,text);g_assert_false(scintilla_send_message(sci,SCI_CANUNDO,0,0));sci_set_readonly(sci,FALSE);
}
static gboolean select_row(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
 gchar *name=NULL;gulong line=0;gtk_tree_model_get(model,iter,0,&name,3,&line,-1);gboolean match=line && !g_strcmp0(name,"target");g_free(name);if(match) { *(GtkTreePath**)data=gtk_tree_path_copy(path);return TRUE; }return FALSE;
}
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;
 ScintillaObject *sci=doc->editor->sci;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(sci));
 if(!phase) { document_open_file(source,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(phase==1) {
  verify_edits(doc,"int target(void) {\n return 1;\n}\nint other;\n",22,"int target(void) {\n return 1;\n}\n","int other;\n","int target(void) {\n return 1;\n}\nint target(void) {\n return 1;\n}\nint other;\n");
  verify_edits(doc,"int target(void) { return 2; }",23,"int target(void) { return 2; }","","int target(void) { return 2; }\nint target(void) { return 2; }");
  scintilla_send_message(sci,SCI_SETEOLMODE,SC_EOL_CRLF,0);
  verify_edits(doc,"int target(void) {\r\n return 3;\r\n}\r\nint other;\r\n",24,"int target(void) {\r\n return 3;\r\n}\r\n","int other;\r\n","int target(void) {\r\n return 3;\r\n}\r\nint target(void) {\r\n return 3;\r\n}\r\nint other;\r\n");
  scintilla_send_message(sci,SCI_SETEOLMODE,SC_EOL_LF,0);
  const gchar *invalid[]={"int target(void);\n","int target(void) {\n","int first(void) { return 1; } int target(void) { return 2; }\n"};
  for(guint i=0;i<G_N_ELEMENTS(invalid);i++) { reset(sci,invalid[i],strlen(invalid[i])-2);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"sentinel",-1);command("insight-copy-symbol");command("insight-cut-symbol");command("insight-duplicate-symbol");check(sci,invalid[i]);g_assert_false(scintilla_send_message(sci,SCI_CANUNDO,0,0));gchar *clip=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(clip,==,"sentinel");g_free(clip); }
  reset(sci,"int target(void) { return 1; }\n",20);scintilla_send_message(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);command("insight-duplicate-symbol");check(sci,"int target(void) { return 1; }\n");scintilla_send_message(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);
  reset(sci,"int target(void) { return 1; }\n",20);
  scintilla_send_message(sci,SCI_SETMULTIPLESELECTION,1,0);scintilla_send_message(sci,SCI_ADDSELECTION,2,1);command("insight-cut-symbol");check(sci,"int target(void) { return 1; }\n");scintilla_send_message(sci,SCI_CLEARSELECTIONS,0,0);
  GString *large=g_string_new("int target(void) {\n/*");for(guint i=0;i<1024*1024;i++) g_string_append_c(large,'x');g_string_append(large,"*/\nreturn 1;\n}\n");reset(sci,large->str,20);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"sentinel",-1);command("insight-copy-symbol");command("insight-duplicate-symbol");check(sci,large->str);g_assert_false(scintilla_send_message(sci,SCI_CANUNDO,0,0));g_string_free(large,TRUE);
  document_set_filetype(doc,filetypes_lookup_by_name("Python"));
  verify_edits(doc,"def outer():\n    def inner():\n        return 'α'\n    return inner()\n",42,"    def inner():\n        return 'α'\n","def outer():\n    return inner()\n","def outer():\n    def inner():\n        return 'α'\n    def inner():\n        return 'α'\n    return inner()\n");
  document_set_filetype(doc,filetypes_lookup_by_name("Lua"));
  verify_edits(doc,"function target()\n if true then\n  return 1\n end\nend\nother()\n",35,"function target()\n if true then\n  return 1\n end\nend\n","other()\n","function target()\n if true then\n  return 1\n end\nend\nfunction target()\n if true then\n  return 1\n end\nend\nother()\n");
  document_set_filetype(doc,filetypes_lookup_by_name("C"));reset(sci,"int target(void) { return 1; }\nint other(void) { return 2; }\n",45);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) {
  local=named(window,"insight-local-symbols");if(!local || !gtk_widget_get_sensitive(local)) return G_SOURCE_CONTINUE;
  GtkTreePath *path=NULL;gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(local)),select_row,&path);if(!path) return G_SOURCE_CONTINUE;
  gtk_tree_view_expand_to_path(GTK_TREE_VIEW(local),path);gtk_tree_view_set_cursor(GTK_TREE_VIEW(local),path,NULL,FALSE);gtk_tree_path_free(path);
  g_signal_emit_by_name(local,"popup-menu",&(gboolean){FALSE});GtkWidget *menu=g_object_get_data(G_OBJECT(local),"insight-symbol-popup");g_assert_nonnull(menu);gtk_menu_popdown(GTK_MENU(menu));
  GtkWidget *copy=named(menu,"insight-symbol-command-26"),*duplicate=named(menu,"insight-symbol-command-28");g_assert_nonnull(copy);g_assert_nonnull(duplicate);
  gtk_menu_item_activate(GTK_MENU_ITEM(copy));gchar *clip=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(clip,==,"int target(void) { return 1; }\n");g_free(clip);
  /* Keep the old outline selected while changing its parser line. */
  scintilla_send_message(sci,SCI_INSERTTEXT,0,(sptr_t)"// shifted\n");
  gchar *shifted=sci_get_contents(sci,-1);gtk_menu_item_activate(GTK_MENU_ITEM(duplicate));check(sci,shifted);g_free(shifted);sci_undo(sci);
  gtk_menu_item_activate(GTK_MENU_ITEM(duplicate));check(sci,"int target(void) { return 1; }\nint target(void) { return 1; }\nint other(void) { return 2; }\n");
  gtk_window_resize(GTK_WINDOW(window),1400,900);phase++;return G_SOURCE_CONTINUE;
 }
 if(g_getenv("GEANY_SYMBOL_EDIT_CAPTURE")) { GdkWindow *w=gtk_widget_get_window(GTK_WIDGET(sci));GdkPixbuf *image=gdk_pixbuf_get_from_window(w,0,0,MIN(760,gdk_window_get_width(w)),MIN(230,gdk_window_get_height(w)));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_SYMBOL_EDIT_CAPTURE"),"jpeg",NULL,"quality","90",NULL));g_object_unref(image); }
 gchar *disk=NULL;g_assert_true(g_file_get_contents(source,&disk,NULL,NULL));g_assert_cmpstr(disk,==,"int target(void) { return 1; }\n");g_free(disk);
 scintilla_send_message(sci,SCI_SETSAVEPOINT,0,0);g_print("Symbol editing: brace/Python/Lua, smallest function, EOF, clipboard, outline target independent of caret, read-only, unsupported ranges, Undo/Redo passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Symbol edit timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("symbol-edit-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_mkdir_with_parents(profile,0700);source=g_build_filename(dir,"symbols.c",NULL);
 g_assert_true(g_file_set_contents(source,"int target(void) { return 1; }\n",-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
 gchar *args[]={"symbol-edit","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(500,step,NULL);g_timeout_add_seconds(30,timeout,NULL);return main_lib(5,args);
}
