/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static gchar *directory,*profile,*path,*notes_path;static GeanyDocument *doc;static GtkWidget *window,*entry,*page;static guint phase;
static const gchar *original="int first(void) {\n return 1;\n}\nint target(void) {\n return 1;\n}\n";
static const gchar *moved="/* inserted */\nint first(void) {\n /* more */\n /* extra */\n return 1;\n}\nint target(void) {\n return 1;\n}\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL; }
static GtkWidget *button(GtkWidget *w,const gchar *label) { if(GTK_IS_BUTTON(w) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(w)),label)) return w;if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=button(p->data,label);g_list_free(children);return found; }return NULL; }
static GKeyFile *notes(void) { GKeyFile *key=g_key_file_new();if(!g_key_file_load_from_file(key,notes_path,G_KEY_FILE_NONE,NULL)) { g_key_file_unref(key);return NULL; }return key; }
static gboolean step(gpointer unused) {
 (void)unused;
 if(!phase) { doc=document_open_file(path,FALSE,NULL,NULL);g_assert_nonnull(doc);window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));entry=named(window,"insight-bookmarks-query");g_assert_nonnull(entry);page=gtk_widget_get_parent(entry);phase++;return G_SOURCE_CONTINUE; }
 if(phase==1) { if(!doc->tm_file || doc->tm_file->tags_array->len<2) return G_SOURCE_CONTINUE;gtk_entry_set_text(GTK_ENTRY(entry),"symbol");sci_set_current_position(doc->editor->sci,sci_get_position_from_line(doc->editor->sci,4),FALSE);GtkWidget *add=button(page,"Add/update");g_assert_nonnull(add);gtk_button_clicked(GTK_BUTTON(add));phase++; }
 else if(phase==2) { GKeyFile *key=notes();if(!key) return G_SOURCE_CONTINUE;if(!g_key_file_has_key(key,"note-0","anchor-name",NULL)) { g_key_file_unref(key);return G_SOURCE_CONTINUE; }gchar *name=g_key_file_get_string(key,"note-0","anchor-name",NULL);g_assert_cmpstr(name,==,"target");g_free(name);g_assert_cmpuint(g_key_file_get_uint64(key,"note-0","anchor-offset",NULL),==,1);g_key_file_unref(key);g_assert_true(document_close(doc));g_assert_true(g_file_set_contents(path,moved,-1,NULL));doc=document_open_file(path,FALSE,NULL,NULL);phase++; }
 else if(phase==3) { gint marker=SEND(doc->editor->sci,SCI_MARKERNEXT,0,2);if(marker<0) return G_SOURCE_CONTINUE;g_assert_cmpint(marker,==,7);g_assert_cmpint(SEND(doc->editor->sci,SCI_MARKERGET,4,0)&2,==,0);gtk_button_clicked(GTK_BUTTON(button(page,"Go to")));g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,7);
  gint at=sci_get_position_from_line(doc->editor->sci,7);SEND(doc->editor->sci,SCI_INSERTTEXT,at,(sptr_t)" /* local */\n");gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1);g_assert_true(g_file_set_contents(path,text,-1,NULL));document_set_text_changed(doc,FALSE);g_assert_true(document_close(doc));
  GKeyFile *key=notes();g_assert_nonnull(key);g_assert_cmpuint(g_key_file_get_uint64(key,"note-0","anchor-offset",NULL),==,2);g_key_file_unref(key);gchar *again=g_strconcat("/* external */\n",text,NULL);g_free(text);g_assert_true(g_file_set_contents(path,again,-1,NULL));g_free(again);doc=document_open_file(path,FALSE,NULL,NULL);phase++; }
 else if(phase==4) { gint marker=SEND(doc->editor->sci,SCI_MARKERNEXT,0,2);if(marker<0) return G_SOURCE_CONTINUE;g_assert_cmpint(marker,==,9);gtk_button_clicked(GTK_BUTTON(button(page,"Go to")));g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,9);g_print("Bookmark anchors GUI: capture, stale duplicate, external shifts, immediate close after edit and navigation passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer unused) { (void)unused;g_error("Bookmark anchors GUI timeout phase=%u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 directory=g_dir_make_tmp("bookmark-anchors-gui-XXXXXX",NULL);profile=g_build_filename(directory,"profile",NULL);path=g_build_filename(directory,"sample.c",NULL);notes_path=g_build_filename(profile,"insight","notes.conf",NULL);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);g_assert_true(g_file_set_contents(path,original,-1,NULL));gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *argv[]={"bookmark-anchors-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(100,step,NULL);g_timeout_add_seconds(45,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(directory);g_free(profile);g_free(path);g_free(notes_path);return result;
}
