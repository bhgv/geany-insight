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
static GtkWidget *window;static gchar *directory,*profile;static guint phase,item;static GeanyDocument *source;
static const gchar *code=
 "namespace A {\nstruct Cell { int value; };\nusing First = Cell;\ntypedef First Second;\nint use(Second *p) { return p->value; }\nstruct Box { int field; int method() { return this->field; } };\n}\n"
 "namespace B {\nstruct Cell { long value; };\nusing First = Cell;\nint use(First *p) { return p->value; }\n}\n"
 "template<class T> struct Holder { int entry; };\nusing IntHolder = Holder<int>;\nint take(IntHolder *p) { return p->entry; }\n"
 "namespace Chain {\nstruct Leaf { int end; };\nstruct Base { Leaf *child; int inherited; Leaf *get(); };\nstruct Derived : Base { static Leaf *shared; static Leaf *create(); Leaf *same(); Leaf *same(int); int method() { return this->child->end; } };\nusing Alias = Derived;\nDerived *global;\nLeaf *factory();\nint use(Derived *p) { return p->inherited + p->child->end + Chain::global->child->end + Alias::shared->end + factory()->end + p->get()->end + p->same(1)->end + Alias::create()->end + p /* obj */ ->get/* args */() /* result */ ->end; }\nstruct Other { Leaf *child; int end; };\nLeaf *mixed(int); Other *mixed(const char*);\nint bad_result() { return mixed(1)->end + mixed(1)-> /* reject */ end; }\nstruct Ambiguous : Base, Other {};\nint blocked(Ambiguous *p) { return p->child->end; }\nint good_tail() { return factory()->end; }\n}\n"
 "namespace Nested {\nstruct Owner { struct Cell { int stamp; }; using Own = Cell; };\nusing Alias = Owner;\nAlias::Cell *factory();\nint use(Alias::Cell *p, Alias::Own *q) { return p->stamp + q->stamp + factory()->stamp; }\n}\n"
 "namespace Short = Nested;\nnamespace Shorter = Short;\nShorter::Alias::Own *external;\nint use_short() { return external->stamp + Shorter::factory()->stamp; }\n"
 "using LoopA = LoopB;\nusing LoopB = LoopA;\nint bad(LoopA *p) { return p->value; }\n";
static gchar *custom_code;
static const struct { const char *reference,*declaration,*owner;guint occurrence; } cases[]={
 {"p->value","struct Cell { int value;","A::Cell",0},
 {"p->value","struct Cell { long value;","B::Cell",1},
 {"this->field","struct Box { int field;","A::Box",0},
 {"p->entry","template<class T> struct Holder","Holder",0},
 {"p->inherited","struct Base { Leaf","Chain::Base",0},
 {"this->child->end","struct Leaf { int end;","Chain::Leaf",0},
 {"p->child->end","struct Leaf { int end;","Chain::Leaf",0},
 {"Chain::global->child->end","struct Leaf { int end;","Chain::Leaf",0},
 {"Alias::shared->end","struct Leaf { int end;","Chain::Leaf",0},
 {"factory()->end","struct Leaf { int end;","Chain::Leaf",0},
 {"p->get()->end","struct Leaf { int end;","Chain::Leaf",0},
 {"p->same(1)->end","struct Leaf { int end;","Chain::Leaf",0},
 {"Alias::create()->end","struct Leaf { int end;","Chain::Leaf",0},
 {"p /* obj */ ->get/* args */() /* result */ ->end","struct Leaf { int end;","Chain::Leaf",0},
 {"p->stamp","struct Owner { struct Cell { int stamp;","Nested::Owner::Cell",0},
 {"q->stamp","struct Owner { struct Cell { int stamp;","Nested::Owner::Cell",0},
 {"factory()->stamp","struct Owner { struct Cell { int stamp;","Nested::Owner::Cell",0},
 {"external->stamp","struct Owner { struct Cell { int stamp;","Nested::Owner::Cell",0},
 {"Shorter::factory()->stamp","struct Owner { struct Cell { int stamp;","Nested::Owner::Cell",0}
};
static GtkWidget *named(GtkWidget *root,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL; }
static void open_source(const gchar *name,const gchar *text,const gchar *type) { gchar *path=g_build_filename(directory,name,NULL);g_assert_true(g_file_set_contents(path,text,-1,NULL));g_assert_nonnull(document_open_file(path,FALSE,filetypes_lookup_by_name(type),NULL));g_free(path); }
static gint reference(void) { const gchar *p=code;for(guint n=0;n<=cases[item].occurrence;n++) { p=strstr(p,cases[item].reference);g_assert_nonnull(p);if(n<cases[item].occurrence) p++; }return p-code+strlen(cases[item].reference)-2; }
static void key(ScintillaObject *sci) { GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(sci)));event->key.keyval=GDK_KEY_F12;gtk_widget_event(GTK_WIDGET(sci),event);gdk_event_free(event); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(phase==0) { window=gtk_widget_get_toplevel(GTK_WIDGET(document_get_current()->editor->sci));open_source("foreign.py","class Cell:\n    value = 0\nclass Holder:\n    entry = 0\n","Python");open_source("main.cpp",code,"C++");source=document_get_current();phase++;return G_SOURCE_CONTINUE; }
 ScintillaObject *preview=SCINTILLA(named(window,"insight-context-code"));g_assert_nonnull(preview);
 if(phase==1) {
  if(sci_get_length(preview)!=(gint)strlen(code) || !SSM(preview,SCI_INDICATORVALUEAT,31,reference())) return G_SOURCE_CONTINUE;
  gint before=sci_get_current_position(source->editor->sci);sci_set_current_position(preview,reference(),FALSE);key(preview);g_assert_cmpint(sci_get_current_position(source->editor->sci),==,before);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) {
  if(item+1==G_N_ELEMENTS(cases)) { gint tail=g_strrstr(code,"factory()->end")-code+strlen("factory()->end")-2;if(!SSM(preview,SCI_INDICATORVALUEAT,31,tail)) return G_SOURCE_CONTINUE; }
  GtkWidget *choices=named(window,"insight-context-definitions");g_assert_nonnull(choices);g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(choices)),NULL),==,1);
  gchar *label=gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(choices));g_assert_nonnull(label);g_assert_nonnull(strstr(label,cases[item].owner));g_free(label);
  gint target=sci_get_line_from_position(preview,strstr(code,cases[item].declaration)-code);g_assert_cmpint(sci_get_current_line(preview),==,target);g_assert_true(document_get_current()==source);
  if(++item<G_N_ELEMENTS(cases)) { phase=1;return G_SOURCE_CONTINUE; }
  gint bad=strstr(code,"int bad")-code;bad=strstr(code+bad,"p->value")-code+4;g_assert_cmpint(SSM(preview,SCI_INDICATORVALUEAT,31,bad),==,0);
  gint clash=strstr(code,"int blocked")-code;clash=strstr(code+clash,"p->child->end")-code+11;g_assert_cmpint(SSM(preview,SCI_INDICATORVALUEAT,31,clash),==,0);
  gint result=strstr(code,"mixed(1)->end")-code+10;g_assert_cmpint(SSM(preview,SCI_INDICATORVALUEAT,31,result),==,0);gint commented=strstr(code,"/* reject */ end")-code+strlen("/* reject */ ");g_assert_cmpint(SSM(preview,SCI_INDICATORVALUEAT,31,commented),==,0);
  /* Exercise the asynchronous editor-follow definition path too. */
  item=10;sci_set_current_position(source->editor->sci,reference(),FALSE);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==3) {
  /* F12 disabled Follow, so explicitly resume it through its visible control. */
  GList *queue=g_list_append(NULL,window);GtkWidget *follow=NULL;while(queue) { GtkWidget *w=queue->data;queue=g_list_delete_link(queue,queue);if(GTK_IS_CHECK_BUTTON(w) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(w)),"Follow cursor")) { follow=w;break; }if(GTK_IS_CONTAINER(w)) queue=g_list_concat(queue,gtk_container_get_children(GTK_CONTAINER(w))); }g_list_free(queue);g_assert_nonnull(follow);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(follow),TRUE);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==4) {
  GtkWidget *choices=named(window,"insight-context-definitions");gchar *label=gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(choices));gboolean correct=label && strstr(label,"Chain::Leaf");g_free(label);if(!correct) return G_SOURCE_CONTINUE;
  g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(choices)),NULL),==,1);
  open_source("calls.custom",custom_code,"C++");source=document_get_current();sci_set_current_position(source->editor->sci,strstr(custom_code,"->mark")-custom_code+3,FALSE);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==5) {
  GtkWidget *choices=named(window,"insight-context-definitions");gchar *label=gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(choices));gboolean correct=label && strstr(label,"ProfileCell");g_free(label);if(!correct) return G_SOURCE_CONTINUE;
  gint at=strstr(custom_code,"->mark")-custom_code+3,tail=g_strrstr(custom_code,"->mark")-custom_code+3;if(!SSM(preview,SCI_INDICATORVALUEAT,31,at) || !SSM(preview,SCI_INDICATORVALUEAT,31,tail)) return G_SOURCE_CONTINUE;gint long_comment=strstr(custom_code,"*/)->mark")-custom_code+5;g_assert_cmpint(SSM(preview,SCI_INDICATORVALUEAT,31,long_comment),==,0);g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(choices)),NULL),==,1);gint before=sci_get_current_position(source->editor->sci);sci_set_current_position(preview,at,FALSE);key(preview);g_assert_cmpint(sci_get_current_position(source->editor->sci),==,before);g_assert_cmpint(sci_get_current_line(preview),==,0);
  g_print("Type aliases GUI: scoped/inherited field and call receivers, same-type overload results, mixed-result rejection, parser profile on custom extension, preview F12 and asynchronous editor follow passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Type aliases GUI timeout at phase %u item %u",phase,item);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 GString *custom=g_string_new("struct ProfileCell { int mark; };\nstruct WrongCell { int mark; };\nProfileCell *profile_factory(); WrongCell *fake_profile_call();\nint profile_use() { return profile_factory()->mark; }\nint long_args() { return profile_factory(/* ");for(guint i=0;i<5000;i++) g_string_append_c(custom,' ');g_string_append(custom," fake_profile_call( */)->mark; }\nint tail() { return profile_factory()->mark; }\n");custom_code=g_string_free(custom,FALSE);
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("type-alias-%u",g_random_int());directory=g_build_filename(cwd,name,NULL);profile=g_build_filename(directory,"profile",NULL);g_free(cwd);g_free(name);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *argv[]={"type-alias-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(35,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(directory);g_free(profile);g_free(custom_code);return result;
}
