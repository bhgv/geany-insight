/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "highlighting.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#undef SSM
#define SSM(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;static gchar *directory,*profile;static guint phase;static GeanyDocument *source;
static const gchar *code="int main(int argc,char **args) {
 (void)argc;\n native_header(); native_asm(); foreign_shell(); foreign_vhdl(); unknown_name();\n bool x = false; return x;\n}\n/* native_header */\nconst char *text = \"native_header\";\n";
static GtkWidget *named(GtkWidget *root,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL; }
static void open_source(const gchar *name,const gchar *text,const gchar *type) { gchar *path=g_build_filename(directory,name,NULL);g_assert_true(g_file_set_contents(path,text,-1,NULL));g_assert_nonnull(document_open_file(path,FALSE,filetypes_lookup_by_name(type),NULL));g_free(path); }
static void key(ScintillaObject *sci,guint keyval) { GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(sci)));event->key.keyval=keyval;gtk_widget_event(GTK_WIDGET(sci),event);gdk_event_free(event); }
static guint text_changes,style_changes,indicator_changes,stable_ticks;static gboolean watching;
static void changed(ScintillaObject *sci,gint message,SCNotification *n,gpointer data) {
 (void)sci;(void)message;(void)data;if(n->nmhdr.code!=SCN_MODIFIED) return;
 if(n->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT)) text_changes++;
 if(n->modificationType&SC_MOD_CHANGESTYLE) style_changes++;
 if(n->modificationType&SC_MOD_CHANGEINDICATOR) indicator_changes++;
}
static void colored(ScintillaObject *preview) {
 const gchar *samples[]={"int main","return x","/* native_header","\"native_header"};
 for(guint i=0;i<G_N_ELEMENTS(samples);i++) { gint pos=strstr(code,samples[i])-code;gint style=SSM(preview,SCI_GETSTYLEAT,pos,0),editor_style=SSM(source->editor->sci,SCI_GETSTYLEAT,pos,0);g_assert_cmpint(style,==,editor_style);g_assert_cmpint(SSM(preview,SCI_STYLEGETFORE,style,0),==,SSM(source->editor->sci,SCI_STYLEGETFORE,editor_style,0)); }
 gint keyword=SSM(preview,SCI_GETSTYLEAT,strstr(code,"return x")-code,0),comment=SSM(preview,SCI_GETSTYLEAT,strstr(code,"/* native_header")-code,0);
 g_assert_cmpint(SSM(preview,SCI_STYLEGETFORE,keyword,0),!=,SSM(preview,SCI_STYLEGETFORE,comment,0));
}
static void legacy_seed(void) {
 ScintillaObject *plain=SCINTILLA(scintilla_new());g_object_ref_sink(plain);highlighting_set_styles(plain,filetypes[GEANY_FILETYPES_NONE]);
 GString *css=g_string_new("editor { font-family: \"Monospace\"; font-size: 13pt; }\n");
 for(gint style=0;style<=STYLE_MAX;style++) { gint fore=SSM(plain,SCI_STYLEGETFORE,style,0),back=SSM(plain,SCI_STYLEGETBACK,style,0);g_string_append_printf(css,".style-%d { color: #%02x%02x%02x; background-color: #%02x%02x%02x; font-weight: %s; font-style: %s; }\n",style,fore&255,(fore>>8)&255,(fore>>16)&255,back&255,(back>>8)&255,(back>>16)&255,SSM(plain,SCI_STYLEGETBOLD,style,0) ? "bold" : "normal",SSM(plain,SCI_STYLEGETITALIC,style,0) ? "italic" : "normal"); }
 gchar *saved=g_build_filename(directory,"legacy-original.css",NULL);g_assert_true(g_file_set_contents(saved,css->str,-1,NULL));g_free(saved);
 const gchar *names[]={"C++","User copy"};
 for(guint n=0;n<2;n++) { gchar *folder=g_build_filename(profile,"languages",names[n],NULL);g_assert_cmpint(g_mkdir_with_parents(folder,0700),==,0);gchar *path=g_build_filename(folder,"style.css",NULL);g_assert_true(g_file_set_contents(path,css->str,-1,NULL));g_free(path);if(n) { path=g_build_filename(folder,"language.conf",NULL);g_assert_true(g_file_set_contents(path,"[language]\nname=User copy\nparser=C++\nextensions=manualcustom\nseeded=false\ntheme=current\n",-1,NULL));g_free(path); }g_free(folder); }
 g_string_free(css,TRUE);gtk_widget_destroy(GTK_WIDGET(plain));g_object_unref(plain);
}
static void check_seed(void) {
 gchar *path=g_build_filename(directory,"legacy-original.css",NULL),*legacy=NULL,*current=NULL;g_assert_true(g_file_get_contents(path,&legacy,NULL,NULL));g_free(path);
 path=g_build_filename(profile,"languages","C++","style.css",NULL);g_assert_true(g_file_get_contents(path,&current,NULL,NULL));g_assert_cmpstr(current,!=,legacy);g_assert_true(g_str_has_prefix(current,"editor { font-family: \"Monospace\"; font-size: 13pt; }\n"));g_free(path);g_free(current);
 path=g_build_filename(profile,"languages","User copy","style.css",NULL);g_assert_true(g_file_get_contents(path,&current,NULL,NULL));g_assert_cmpstr(current,==,legacy);g_free(path);g_free(current);g_free(legacy);
 g_print("Legacy generated C++ palette repaired; font preserved; user profile untouched\n");
}
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(phase==0) { if(!g_strcmp0(g_getenv("INSIGHT_CONTEXT_STAGE"),"produce")) { legacy_seed();gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"menu_quit1")));return G_SOURCE_REMOVE; }if(!g_strcmp0(g_getenv("INSIGHT_CONTEXT_STAGE"),"check")) check_seed();window=gtk_widget_get_toplevel(GTK_WIDGET(document_get_current()->editor->sci));open_source("native.hpp","void native_header(void);\n#define false 0\n","C++");open_source("native.S","native_asm:\n nop\n","ASM");open_source("foreign.sh","foreign_shell() { echo test; }\nnative_header() { echo wrong; }\n","Sh");open_source("foreign.vhdl","entity foreign_vhdl is\nend foreign_vhdl;\n","VHDL");open_source("main.cpp",code,"C++");source=document_get_current();phase++;return G_SOURCE_CONTINUE; }
 ScintillaObject *preview=SCINTILLA(named(window,"insight-context-code"));g_assert_nonnull(preview);
 if(phase==1) {
  if(sci_get_length(preview)!=(gint)strlen(code)) return G_SOURCE_CONTINUE;
  gint header=strstr(code,"native_header")-code,assembly=strstr(code,"native_asm")-code;
  if(!SSM(preview,SCI_INDICATORVALUEAT,31,header) || !SSM(preview,SCI_INDICATORVALUEAT,31,assembly)) return G_SOURCE_CONTINUE;
  const gchar *blocked[]={"foreign_shell","foreign_vhdl","unknown_name","false","return","/* native_header","\"native_header"};
  for(guint n=0;n<G_N_ELEMENTS(blocked);n++) { gint offset=strstr(code,blocked[n])-code;if(n==5) offset+=3;if(n==6) offset++;g_assert_cmpint(SSM(preview,SCI_INDICATORVALUEAT,31,offset),==,0); }
  colored(preview);
  if(!watching) { g_signal_connect(preview,"sci-notify",G_CALLBACK(changed),NULL);watching=TRUE; }
  /* Repeated Follow requests for the same word must not reset text/styles/links. */
  if(stable_ticks++<16) { if(stable_ticks==4) text_changes=style_changes=indicator_changes=0;if(stable_ticks%4==0) sci_set_current_position(source->editor->sci,(stable_ticks/4)%2,FALSE);gtk_widget_grab_focus(GTK_WIDGET(source->editor->sci));return G_SOURCE_CONTINUE; }
  g_assert_cmpuint(text_changes,==,0);g_assert_cmpuint(style_changes,==,0);g_assert_cmpuint(indicator_changes,==,0);
  g_assert_nonnull(named(window,"insight-context-definitions-label"));
  gint before=sci_get_current_position(source->editor->sci);sci_set_current_position(preview,strstr(code,"false")-code,FALSE);key(preview,GDK_KEY_F12);g_assert_cmpint(sci_get_length(preview),==,strlen(code));g_assert_cmpint(sci_get_current_position(source->editor->sci),==,before);
  sci_set_current_position(preview,header,FALSE);key(preview,GDK_KEY_F12);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) { gchar *text=sci_get_contents(preview,sci_get_length(preview)+1);g_assert_nonnull(strstr(text,"void native_header"));g_free(text);GtkWidget *choices=named(window,"insight-context-definitions");g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(choices)),NULL),==,1);g_assert_true(document_get_current()==source);
  g_print("Context GUI: matching syntax palette, stable Follow without text/style/indicator resets, resolved C++/header/assembly links, blocked shell/VHDL/unknown/keywords/comments/strings, labelled candidates, inert false and preview-only definition jump passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Context GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
int main(int argc,char **args) {
 (void)argc;
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 if(g_getenv("INSIGHT_TEST_LEGACY_SEED") && !g_getenv("INSIGHT_CONTEXT_STAGE")) {
  gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("legacy-context-%u",g_random_int()),*dir=g_build_filename(cwd,name,NULL);g_free(cwd);g_free(name);
  gchar **env=g_get_environ();env=g_environ_setenv(env,"INSIGHT_CONTEXT_DIRECTORY",dir,TRUE);gchar *child[]={args[0],NULL};const gchar *stages[]={"produce","check"};
  for(guint n=0;n<2;n++) { env=g_environ_setenv(env,"INSIGHT_CONTEXT_STAGE",stages[n],TRUE);gint status;GError *error=NULL;g_assert_true(g_spawn_sync(NULL,child,env,0,NULL,NULL,NULL,NULL,&status,&error));g_assert_no_error(error);g_assert_true(g_spawn_check_wait_status(status,&error));g_assert_no_error(error); }
  g_strfreev(env);g_free(dir);return 0;
 }
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("context-links-%u",g_random_int());directory=g_getenv("INSIGHT_CONTEXT_DIRECTORY") ? g_strdup(g_getenv("INSIGHT_CONTEXT_DIRECTORY")) : g_build_filename(cwd,name,NULL);profile=g_build_filename(directory,"profile",NULL);g_free(cwd);g_free(name);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *argv[]={"context-links-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(35,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(directory);g_free(profile);return result;
}
