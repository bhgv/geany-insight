/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "highlighting.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "SciLexer.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;static GeanyDocument *doc;static gchar *source,*second,*config,*bad_css;static guint phase;
static const gchar *pick;static gboolean cancel_pick,expect_failure,restoring;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *found=named(m,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *out=NULL;for(GList *p=list;p && !out;p=p->next) out=named(p->data,name);g_list_free(list);return out; }return NULL;
}
static GtkWidget *theme_dialog(void) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *out=NULL;for(GList *p=windows;p && !out;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-theme-dialog")) out=p->data;g_list_free(windows);return out;
}
static gboolean failed(gpointer data) { (void)data;GtkWidget *dialog=theme_dialog();g_assert_nonnull(dialog);const gchar *text=gtk_label_get_text(GTK_LABEL(named(dialog,"insight-theme-status")));g_assert_true(*text!=0);g_assert_null(strstr(text,"Themes use editable CSS"));gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *dialog=theme_dialog();g_assert_nonnull(dialog);GtkWidget *choice=named(dialog,"insight-theme-choice");g_assert_true(gtk_combo_box_set_active_id(GTK_COMBO_BOX(choice),pick));
 if(expect_failure) g_timeout_add(50,failed,NULL);
 gtk_dialog_response(GTK_DIALOG(dialog),cancel_pick ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_APPLY);return G_SOURCE_REMOVE;
}
static void choose(const gchar *name,gboolean cancel,gboolean failure) { pick=name;cancel_pick=cancel;expect_failure=failure;g_timeout_add(50,respond,NULL);gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-visual-theme"))); }
static void saved(const gchar *name) { GKeyFile *k=g_key_file_new();g_assert_true(g_key_file_load_from_file(k,config,G_KEY_FILE_NONE,NULL));gchar *value=g_key_file_get_string(k,"theme","name",NULL);g_assert_cmpstr(value,==,name);g_free(value);g_key_file_unref(k); }
static void black(ScintillaObject *sci) {
 g_assert_cmpint(SEND(sci,SCI_STYLEGETBACK,STYLE_DEFAULT,0),==,0);g_assert_cmpint(SEND(sci,SCI_STYLEGETFORE,STYLE_DEFAULT,0),==,0xebebeb);g_assert_cmpint(SEND(sci,SCI_STYLEGETFORE,SCE_C_WORD,0),==,0x008000);g_assert_cmpint(SEND(sci,SCI_STYLEGETFORE,SCE_C_COMMENT,0),==,0xffb7be);
 gchar font[128]={0};g_assert_cmpint(SEND(sci,SCI_STYLEGETFONT,STYLE_DEFAULT,0),<,128);SEND(sci,SCI_STYLEGETFONT,STYLE_DEFAULT,(sptr_t)font);g_test_message("Theme editor font: %s",font);g_assert_true(!strcmp(font,"Noto Sans Mono") || !strcmp(font,"Consolas"));
}
static void unchanged(void) { g_assert_false(doc->changed);g_assert_false(SEND(doc->editor->sci,SCI_CANUNDO,0,0));g_assert_cmpint(SEND(doc->editor->sci,SCI_GETSELECTIONSTART,0,0),==,4);g_assert_cmpint(SEND(doc->editor->sci,SCI_GETSELECTIONEND,0,0),==,10); }
static gboolean tick(gpointer data) {
 (void)data;
 if(phase) {
  ScintillaObject *preview=SCINTILLA(named(window,"insight-context-code"));if(!preview || sci_get_length(preview)<10) return G_SOURCE_CONTINUE;
  black(preview);choose("Wheat",FALSE,FALSE);g_assert_cmpint(SEND(preview,SCI_STYLEGETBACK,STYLE_DEFAULT,0),==,SEND(doc->editor->sci,SCI_STYLEGETBACK,STYLE_DEFAULT,0));g_assert_cmpint(SEND(preview,SCI_STYLEGETBACK,STYLE_DEFAULT,0),!=,0);choose("Black",FALSE,FALSE);black(preview);
  g_print("Visual themes GUI passed: 15 themes, fonts, Context, panel overrides, mono, rollback and restore\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 doc=document_get_current();g_assert_nonnull(doc);window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));ScintillaObject *sci=doc->editor->sci;
 if(restoring) { black(sci);saved("Black");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 gint back=SEND(sci,SCI_STYLEGETBACK,STYLE_DEFAULT,0),fore=SEND(sci,SCI_STYLEGETFORE,SCE_C_WORD,0),size=SEND(sci,SCI_STYLEGETSIZEFRACTIONAL,SCE_C_WORD,0);SEND(sci,SCI_SETSEL,4,10);
 choose("Black",TRUE,FALSE);g_assert_cmpint(SEND(sci,SCI_STYLEGETBACK,STYLE_DEFAULT,0),==,back);unchanged();
 const gchar *names[]={"Black","Bronze","Cocoa","Dark Blue","Dark Gold","Light Blue","Light Green","Lime","Minty","Olive","Sandstone","Silver Blue","Grasshopper","Styles for use with dark backgrounds","Wheat"};
 for(guint i=0;i<G_N_ELEMENTS(names);i++) { choose(names[i],FALSE,FALSE);saved(names[i]);unchanged(); }
 choose("Black",FALSE,FALSE);black(sci);
 GtkWidget *marks=named(window,"insight-bookmarks");GdkRGBA color;gtk_style_context_get_background_color(gtk_widget_get_style_context(marks),GTK_STATE_FLAG_NORMAL,&color);g_assert_cmpfloat(color.red,==,0);g_assert_cmpfloat(color.green,==,g_getenv("GEANY_TEST_THEME_PANEL") ? 1 : 0);g_assert_cmpfloat(color.blue,==,0);
 GtkWidget *mono=named(window,"insight-mono-font-view");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mono),TRUE);g_assert_cmpint(SEND(sci,SCI_STYLEGETFORE,SCE_C_WORD,0),==,0x008000);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mono),FALSE);black(sci);
 GeanyDocument *other=document_open_file(second,FALSE,NULL,NULL);g_assert_nonnull(other);black(other->editor->sci);g_assert_true(document_open_file(source,FALSE,NULL,NULL)==doc);
 GtkWidget *query=named(window,"insight-local-query");gtk_entry_set_text(GTK_ENTRY(query),"sample");
 GtkTreeView *symbols=GTK_TREE_VIEW(named(window,"insight-local-symbols"));GtkTreeModel *model=gtk_tree_view_get_model(symbols);GtkTreeIter iter;
 if(gtk_tree_model_get_iter_first(model,&iter)) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(symbols),&iter);
 choose("Broken",FALSE,TRUE);black(sci);saved("Black");
 gchar *backup=g_strconcat(config,".backup",NULL);g_assert_cmpint(g_rename(config,backup),==,0);g_assert_cmpint(g_mkdir(config,0700),==,0);choose("Wheat",FALSE,TRUE);black(sci);g_assert_cmpint(g_rmdir(config),==,0);g_assert_cmpint(g_rename(backup,config),==,0);g_free(backup);saved("Black");
 choose("",FALSE,FALSE);g_assert_cmpint(SEND(sci,SCI_STYLEGETBACK,STYLE_DEFAULT,0),==,back);g_assert_cmpint(SEND(sci,SCI_STYLEGETFORE,SCE_C_WORD,0),==,fore);g_assert_cmpint(SEND(sci,SCI_STYLEGETSIZEFRACTIONAL,SCE_C_WORD,0),==,size);
 choose("Black",FALSE,FALSE);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(named(window,"insight-visual-theme"))),==,"Визуальная тема…");
 phase=1;return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Visual themes timeout");return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 restoring=g_getenv("GEANY_TEST_THEME_RESTORE")!=NULL;gchar *dir=g_dir_make_tmp("visual-theme-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL),*settings=g_build_filename(profile,"insight",NULL),*broken=g_build_filename(profile,"styles","Broken",NULL);g_mkdir_with_parents(settings,0700);g_mkdir_with_parents(broken,0700);
 gchar *appearance=g_build_filename(settings,"interface-style.conf",NULL);g_assert_true(g_file_set_contents(appearance,"[interface]\nversion=1\nstyle=classic\n",-1,NULL));g_free(appearance);
 config=g_build_filename(settings,"visual-theme.conf",NULL);source=g_build_filename(dir,"source.c",NULL);second=g_build_filename(dir,"other.c",NULL);g_file_set_contents(source,"int sample(void) {\n // comment\n return 1;\n}\n",-1,NULL);g_file_set_contents(second,"int other(void) { return 2; }\n",-1,NULL);
 gchar *file=g_build_filename(profile,"geany.conf",NULL);g_file_set_contents(file,"[geany]\nconfirm_exit=false\n",-1,NULL);g_free(file);bad_css=g_build_filename(broken,"editor.css",NULL);g_file_set_contents(bad_css,"editor { color: definitely-invalid; }\n",-1,NULL);file=g_build_filename(broken,"panels.css",NULL);g_file_set_contents(file,"/* empty */\n",-1,NULL);g_free(file);
 if(g_getenv("GEANY_TEST_THEME_PANEL")) { file=g_build_filename(settings,"workbench.conf",NULL);g_file_set_contents(file,"[workbench]\nversion=1\n[bookmarks-style]\nversion=1\nenabled=true\nfont=Sans 13\nforeground=#123456\nbackground=#00ff00\n",-1,NULL);g_free(file); }
 if(restoring) g_file_set_contents(config,"[theme]\nversion=1\nname=Black\n",-1,NULL);
 gchar *argv[]={"themes-gui","--new-instance","--no-session","--config",profile,source,NULL};g_timeout_add(300,tick,NULL);g_timeout_add_seconds(40,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(6,argv);saved("Black");return result;
}
