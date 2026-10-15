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
typedef struct { const gchar *expression,*word,*declaration;gboolean module; } Case;
static GtkWidget *window;static GeanyDocument *doc;static gchar *path,*module_path,*code;static guint phase,current;static GArray *cases;
static const gchar *module_code="class External:\n    field: int\ndef exported() -> External:\n    return External()\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL; }
static gint position(Case *c) { const gchar *expression=strstr(code,c->expression);g_assert_nonnull(expression);const gchar *word=g_strrstr(c->expression,c->word);g_assert_nonnull(word);return expression-code+(word-c->expression); }
static gint line_of(const gchar *text,const gchar *needle) { const gchar *end=strstr(text,needle);g_assert_nonnull(end);gint line=0;for(const gchar *p=text;p<end;p++) if(*p=='\n') line++;return line; }
static gboolean target(ScintillaObject *preview,Case *c) {
 const gchar *text=c->module ? module_code : code,*file=c->module ? module_path : path;
 const gchar *title=gtk_label_get_text(GTK_LABEL(named(window,"insight-context-title")));
 return sci_get_length(preview)==(gint)strlen(text) && strstr(title,file) && (c->declaration ? sci_get_current_line(preview)==line_of(text,c->declaration) : strstr(title,"no indexed definition")!=NULL);
}
static void follow(gint at) { sci_set_current_position(doc->editor->sci,at,FALSE);gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-context-follow")),TRUE); }
static gboolean step(gpointer unused) {
 (void)unused;
 if(!phase) { document_open_file(module_path,FALSE,NULL,NULL);doc=document_open_file(path,FALSE,NULL,NULL);g_assert_nonnull(doc);window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));phase=1;return G_SOURCE_CONTINUE; }
 ScintillaObject *preview=SCINTILLA(named(window,"insight-context-code"));
 if(current==cases->len) { g_print("Python 30-step series GUI: %u F12 and Follow cases passed\n",cases->len);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 Case *c=&g_array_index(cases,Case,current);
 if(phase==1) { follow(2);phase=2; }
 else if(phase==2) {
  const gchar *title=gtk_label_get_text(GTK_LABEL(named(window,"insight-context-title")));
  if(sci_get_length(preview)!=(gint)strlen(code) || !strstr(title,"no indexed definition") || !strstr(title,path)) return G_SOURCE_CONTINUE;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-context-follow")),FALSE);gint at=position(c);sci_set_current_position(preview,at,FALSE);
  GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(preview)));event->key.keyval=GDK_KEY_F12;gtk_widget_event(GTK_WIDGET(preview),event);gdk_event_free(event);
  if(c->declaration) g_assert_true(target(preview,c));else g_assert_cmpint(sci_get_current_position(preview),==,at);
  follow(at);phase=3;
 }else if(phase==3) { if(!target(preview,c)) return G_SOURCE_CONTINUE;current++;phase=1; }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer unused) { (void)unused;g_error("Python series GUI timeout case=%u phase=%u",current,phase);return G_SOURCE_REMOVE; }
static void add(const gchar *expression,const gchar *word,const gchar *declaration,gboolean module) { Case c={expression,word,declaration,module};g_array_append_val(cases,c); }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 code=g_strdup("# anchor\nclass Model:\n    field: int\n    def setup(self):\n        self.typed: int\n    def run(self):\n        local: Model\n        return local.field + self.typed\nmodule: Model\nmodule.field\n");
 gchar *expanded=g_strconcat(code,"import builtins\nclass Named:\n    named_field: int\n    def run(me):\n        return me.named_field\n    @builtins.staticmethod\n    def static(self):\n        return self.named_field # qualified_static\n    @staticmethod\n    @identity(\n        1,\n    )\n    def stacked(self):\n        return self.named_field # stacked_static\n",NULL);g_free(code);code=expanded;
 expanded=g_strconcat(code,"class Result:\n    result_field: int\ndef make_result() -> Result:\n    return Result()\ndef unknown_result():\n    return Result()\nasync def async_result() -> Result:\n    return Result()\nResult().result_field\nmake_result().result_field\nunknown_result().result_field\nasync_result().result_field\n",NULL);g_free(code);code=expanded;
 expanded=g_strconcat(code,"import nested.provider\nimport nested.provider as lib\nnested.provider.exported()\nnested.provider.External.field\nlib.External.field\nnested.missing.exported()\n",NULL);g_free(code);code=expanded;
 expanded=g_strconcat(code,"from nested.provider import External as Alias\ntyped: Alias\ntyped.field\nqualified: lib.External\nqualified.field\ncreated = Alias()\ncreated.field\nqualified_created = lib.External()\nqualified_created.field\nAlias().field\nlib.External().field\ndef imported_parameter(obj: Alias):\n    Alias = 1\n    return obj.field\n",NULL);g_free(code);code=expanded;
 cases=g_array_new(FALSE,FALSE,sizeof(Case));add("return local.field","field","field: int",FALSE);add("+ self.typed","typed","self.typed: int",FALSE);add("module.field","field","field: int",FALSE);
 add("return me.named_field","named_field","named_field: int",FALSE);add("self.named_field # qualified_static","named_field",NULL,FALSE);add("self.named_field # stacked_static","named_field",NULL,FALSE);
 add("Result().result_field","result_field","result_field: int",FALSE);add("make_result().result_field","result_field","result_field: int",FALSE);add("unknown_result().result_field","result_field",NULL,FALSE);add("async_result().result_field","result_field",NULL,FALSE);
 add("nested.provider.exported()","exported","def exported()",TRUE);add("nested.provider.External.field","field","field: int",TRUE);add("lib.External.field","field","field: int",TRUE);add("nested.missing.exported()","exported",NULL,FALSE);
 add("typed.field","field","field: int",TRUE);add("qualified.field","field","field: int",TRUE);add("created.field","field","field: int",TRUE);add("qualified_created.field","field","field: int",TRUE);add("Alias().field","field","field: int",TRUE);add("lib.External().field","field","field: int",TRUE);add("return obj.field","field","field: int",TRUE);
 gchar *directory=g_dir_make_tmp("python-series-gui-XXXXXX",NULL),*profile=g_build_filename(directory,"profile",NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));path=g_build_filename(directory,"series.py",NULL);gchar *package=g_build_filename(directory,"nested",NULL);g_assert_cmpint(g_mkdir_with_parents(package,0700),==,0);module_path=g_build_filename(package,"provider.py",NULL);g_free(package);g_assert_true(g_file_set_contents(path,code,-1,NULL));g_assert_true(g_file_set_contents(module_path,module_code,-1,NULL));
 gchar *argv[]={"python-series-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(100,step,NULL);g_timeout_add_seconds(90,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_array_unref(cases);g_free(code);g_free(path);g_free(module_path);g_free(directory);g_free(profile);g_free(config);return result;
}
