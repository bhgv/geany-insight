/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#undef SSM
#define SSM(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;static gchar *directory,*profile;static guint phase;static GeanyDocument *source;
static const gchar *code="int main(void) {\n native_header(); native_asm(); foreign_shell(); foreign_vhdl(); unknown_name();\n bool x = false; return x;\n}\n/* native_header */\nconst char *text = \"native_header\";\n";
static GtkWidget *named(GtkWidget *root,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL; }
static void open_source(const gchar *name,const gchar *text,const gchar *type) { gchar *path=g_build_filename(directory,name,NULL);g_assert_true(g_file_set_contents(path,text,-1,NULL));g_assert_nonnull(document_open_file(path,FALSE,filetypes_lookup_by_name(type),NULL));g_free(path); }
static void key(ScintillaObject *sci,guint keyval) { GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(sci)));event->key.keyval=keyval;gtk_widget_event(GTK_WIDGET(sci),event);gdk_event_free(event); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(phase==0) { window=gtk_widget_get_toplevel(GTK_WIDGET(document_get_current()->editor->sci));open_source("native.hpp","void native_header(void);\n#define false 0\n","C++");open_source("native.S","native_asm:\n nop\n","ASM");open_source("foreign.sh","foreign_shell() { echo test; }\nnative_header() { echo wrong; }\n","Sh");open_source("foreign.vhdl","entity foreign_vhdl is\nend foreign_vhdl;\n","VHDL");open_source("main.cpp",code,"C++");source=document_get_current();phase++;return G_SOURCE_CONTINUE; }
 ScintillaObject *preview=SCINTILLA(named(window,"insight-context-code"));g_assert_nonnull(preview);
 if(phase==1) {
  if(sci_get_length(preview)!=(gint)strlen(code)) return G_SOURCE_CONTINUE;
  gint header=strstr(code,"native_header")-code,assembly=strstr(code,"native_asm")-code;
  if(!SSM(preview,SCI_INDICATORVALUEAT,31,header) || !SSM(preview,SCI_INDICATORVALUEAT,31,assembly)) return G_SOURCE_CONTINUE;
  const gchar *blocked[]={"foreign_shell","foreign_vhdl","unknown_name","false","return","/* native_header","\"native_header"};
  for(guint n=0;n<G_N_ELEMENTS(blocked);n++) { gint offset=strstr(code,blocked[n])-code;if(n==5) offset+=3;if(n==6) offset++;g_assert_cmpint(SSM(preview,SCI_INDICATORVALUEAT,31,offset),==,0); }
  g_assert_nonnull(named(window,"insight-context-definitions-label"));
  gint before=sci_get_current_position(source->editor->sci);sci_set_current_position(preview,strstr(code,"false")-code,FALSE);key(preview,GDK_KEY_F12);g_assert_cmpint(sci_get_length(preview),==,strlen(code));g_assert_cmpint(sci_get_current_position(source->editor->sci),==,before);
  sci_set_current_position(preview,header,FALSE);key(preview,GDK_KEY_F12);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) { gchar *text=sci_get_contents(preview,sci_get_length(preview)+1);g_assert_nonnull(strstr(text,"void native_header"));g_free(text);GtkWidget *choices=named(window,"insight-context-definitions");g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(choices)),NULL),==,1);g_assert_true(document_get_current()==source);
  g_print("Context GUI: resolved C++/header/assembly links, blocked shell/VHDL/unknown/keywords/comments/strings, labelled candidates, inert false and preview-only definition jump passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Context GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("context-links-%u",g_random_int());directory=g_build_filename(cwd,name,NULL);profile=g_build_filename(directory,"profile",NULL);g_free(cwd);g_free(name);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *argv[]={"context-links-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(35,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(directory);g_free(profile);return result;
}
