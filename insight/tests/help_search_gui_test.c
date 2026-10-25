/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "main.h"
#include "ui_utils.h"
#include "keybindings.h"
#include "sciwrappers.h"
#undef SSM
#define SSM(sci,msg,w,l) scintilla_send_message((sci),(msg),(w),(l))
#include "insight-help.h"
#include <glib/gstdio.h>
#include <string.h>

static GtkWidget *window;static gchar *directory,*profile;static guint phase;
static GtkWidget *named(GtkWidget *root,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_NOTEBOOK(root)) {GtkWidget *action=gtk_notebook_get_action_widget(GTK_NOTEBOOK(root),GTK_PACK_END);if(action) {GtkWidget *found=named(action,name);if(found) return found;}}
 if(GTK_IS_MENU_ITEM(root)) { GtkWidget *submenu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(submenu) { GtkWidget *found=named(submenu,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static GtkWidget *viewer(void) { GList *windows=gtk_window_list_toplevels();GtkWidget *result=NULL;for(GList *p=windows;p;p=p->next) if(!strcmp(gtk_widget_get_name(p->data),"insight-help-viewer")) result=p->data;g_list_free(windows);return result; }
static gboolean has_text(GtkWidget *root,const gchar *text) {
 if(GTK_IS_LABEL(root) && strstr(gtk_label_get_text(GTK_LABEL(root)),text)) return TRUE;
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));gboolean result=FALSE;for(GList *p=children;p && !result;p=p->next) result=has_text(p->data,text);g_list_free(children);return result; }return FALSE;
}
static GtkWidget *media_button(GtkWidget *root) {
 if(GTK_IS_BUTTON(root)) { const gchar *label=gtk_button_get_label(GTK_BUTTON(root));if(label && strstr(label,"Video")) return root; }
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *result=NULL;for(GList *p=children;p && !result;p=p->next) result=media_button(p->data);g_list_free(children);return result; }return NULL;
}
static GtkWidget *animation(GtkWidget *root) {
 if(GTK_IS_IMAGE(root) && gtk_image_get_storage_type(GTK_IMAGE(root))==GTK_IMAGE_ANIMATION) return root;
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *result=NULL;for(GList *p=children;p && !result;p=p->next) result=animation(p->data);g_list_free(children);return result; }return NULL;
}
static gboolean cancel_folder(gpointer data) {
 (void)data;GList *windows=gtk_window_list_toplevels();gboolean found=FALSE;for(GList *p=windows;p;p=p->next) if(GTK_IS_FILE_CHOOSER(p->data) && GTK_IS_DIALOG(p->data) && gtk_widget_get_visible(p->data)) { gtk_dialog_response(GTK_DIALOG(p->data),GTK_RESPONSE_CANCEL);found=TRUE;break; }g_list_free(windows);return found ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}
static void shortcut(guint key) {
 GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(window));event->key.keyval=key;event->key.state=GEANY_PRIMARY_MOD_MASK;gtk_widget_event(window,event);gdk_event_free(event);
}
static void preview_checks(void) {
 gchar *path=g_build_filename(directory,"snapshot.md",NULL);g_assert_true(g_file_set_contents(path,"# Disk title\n",-1,NULL));GeanyDocument *doc=document_open_file(path,FALSE,filetypes_lookup_by_name("Markdown"),NULL);g_assert_nonnull(doc);
 sci_set_text(doc->editor->sci,"# Буфер\nСлово слово .+\n## Второй\n![GIF](demo.gif)\n## Второй\n");document_set_text_changed(doc,TRUE);gint caret=sci_get_current_position(doc->editor->sci);gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-markup-preview-command")));g_assert_true(has_text(viewer(),"Буфер"));g_assert_false(has_text(viewer(),"Disk title"));g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);g_assert_nonnull(animation(viewer()));
 GtkWidget *toc=named(viewer(),"insight-help-toc");g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(toc)),NULL),==,3);const gchar *html=g_object_get_data(G_OBJECT(viewer()),"insight-rendered-html");g_assert_nonnull(strstr(html,"id=\"второй-1\""));gtk_combo_box_set_active(GTK_COMBO_BOX(toc),2);
 GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(viewer()));event->key.keyval=GDK_KEY_f;event->key.state=GEANY_PRIMARY_MOD_MASK;gtk_widget_event(viewer(),event);gdk_event_free(event);g_assert_true(gtk_widget_get_visible(named(viewer(),"insight-help-find-row")));
 GtkWidget *find=named(viewer(),"insight-help-find"),*status=named(viewer(),"insight-help-find-status");gtk_entry_set_text(GTK_ENTRY(find),"слово");g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(status)),==,"1 / 2");g_signal_emit_by_name(find,"activate");g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(status)),==,"2 / 2");g_signal_emit_by_name(find,"activate");g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(status)),==,"1 / 2");gtk_entry_set_text(GTK_ENTRY(find),".+");g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(status)),==,"1 / 1");gtk_entry_set_text(GTK_ENTRY(find),"слово");
 sci_set_text(doc->editor->sci,"# Буфер\nСлово слово .+\n## Третий\nСлово\n");document_set_text_changed(doc,TRUE);g_signal_emit_by_name(named(viewer(),"insight-help-reload"),"clicked");g_assert_true(has_text(viewer(),"Третий"));g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(status)),==,"1 / 3");gchar *disk=NULL;g_assert_true(g_file_get_contents(path,&disk,NULL,NULL));g_assert_cmpstr(disk,==,"# Disk title\n");g_free(disk);g_free(path);document_set_text_changed(doc,FALSE);g_assert_true(document_close(doc));g_signal_emit_by_name(named(viewer(),"insight-help-reload"),"clicked");g_assert_true(has_text(viewer(),"Preview unavailable"));g_assert_true(has_text(viewer(),"Третий"));
 doc=document_new_file(NULL,filetypes_lookup_by_name("Markdown"),"# Без имени\nСЛОВО\n");g_assert_nonnull(doc);g_signal_emit_by_name(named(window,"insight-markup-preview-tool"),"clicked");g_assert_true(has_text(viewer(),"Без имени"));document_set_text_changed(doc,FALSE);g_assert_true(document_close(doc));gtk_widget_destroy(viewer());g_assert_nonnull(document_new_file(NULL,NULL,""));
}
static gboolean timeout(gpointer data) { (void)data;g_error("Help/search GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(phase==0) {
  g_assert_false(gtk_widget_get_visible(named(window,"insight-editor-search-panel")));g_assert_false(gtk_widget_get_visible(named(window,"insight-project-search-panel")));
  const gchar *tools[]={"insight-help-tool","insight-tutorial-tool","insight-diff-tool","insight-find-tool","insight-replace-tool"};
  for(guint i=0;i<G_N_ELEMENTS(tools);i++) { GtkWidget *tool=named(window,tools[i]);g_assert_true(GTK_IS_TOOL_BUTTON(tool));g_assert_null(gtk_tool_button_get_label(GTK_TOOL_BUTTON(tool)));gchar *tip=gtk_widget_get_tooltip_text(tool);g_assert_nonnull(tip);g_free(tip); }
  GtkWidget *command=named(window,"insight-help-command");g_assert_nonnull(gtk_menu_item_get_label(GTK_MENU_ITEM(command)));g_assert_nonnull(g_object_get_data(G_OBJECT(command),"insight-menu-icon"));gtk_menu_item_activate(GTK_MENU_ITEM(command));g_assert_nonnull(viewer());g_assert_true(has_text(viewer(),"user guide"));
  g_assert_false(gtk_expander_get_expanded(GTK_EXPANDER(named(viewer(),"insight-help-ai-prompt-fold"))));
  insight_help_answer("Help fixture question","## Answer fixture\n\n| Command | Effect |\n| --- | --- |\n| Back | Previous location |\n");g_assert_true(has_text(viewer(),"Answer fixture"));const gchar *answer_html=g_object_get_data(G_OBJECT(viewer()),"insight-rendered-html");g_assert_nonnull(strstr(answer_html,"<table>"));
  GtkWidget *recent=named(window,"insight-recent-sources");g_assert_nonnull(recent);GtkWidget *popup=GTK_WIDGET(gtk_menu_button_get_popup(GTK_MENU_BUTTON(recent)));g_signal_emit_by_name(popup,"show");GList *rows=gtk_container_get_children(GTK_CONTAINER(popup));g_assert_nonnull(rows);g_list_free(rows);
  phase++;
 } else if(phase==1) {
  if(g_getenv("GEANY_GUI_SCREENSHOT")) { GdkPixbuf *picture=gdk_pixbuf_get_from_window(gtk_widget_get_window(viewer()),0,0,gtk_widget_get_allocated_width(viewer()),gtk_widget_get_allocated_height(viewer()));g_assert_nonnull(picture);g_assert_true(gdk_pixbuf_save(picture,g_getenv("GEANY_GUI_SCREENSHOT"),"png",NULL,NULL));g_object_unref(picture); }
  gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-tutorial-command")));g_assert_true(has_text(viewer(),"tutorials"));
  g_signal_emit_by_name(named(window,"insight-help-tool"),"clicked");g_assert_true(has_text(viewer(),"user guide"));g_signal_emit_by_name(named(window,"insight-tutorial-tool"),"clicked");g_assert_true(has_text(viewer(),"tutorials"));
  gchar *gif=g_build_filename(directory,"demo.gif",NULL);gsize bytes=0;guchar *image=g_base64_decode("R0lGODlhCAAIAIEAAAAA/wAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQACgAAACwAAAAACAAIAAAIDwABCBxIsKDBgwgTKkwYEAAh+QQBCgABACwAAAAACAAIAIH/AAAAAAAAAAAAAAAIDwABCBxIsKDBgwgTKkwYEAA7",&bytes);g_assert_true(g_file_set_contents(gif,(gchar*)image,bytes,NULL));g_free(image);g_free(gif);
  gchar *md=g_build_filename(directory,"test.md",NULL);g_assert_true(g_file_set_contents(md,"# Heading\n**Bold** and `code` and [RST](test.rst).\n![Animated](demo.gif)\n![Movie](demo.webm)\n| Name | Value |\n| --- | --- |\n| test | 42 |\n```lua\nprint('<raw>')\n```\n<script>ignored()</script>\n",-1,NULL));insight_markup_open_path(md);g_free(md);g_assert_true(has_text(viewer(),"Heading"));g_assert_true(has_text(viewer(),"Video"));GtkWidget *image_widget=animation(viewer());g_assert_nonnull(image_widget);g_assert_false(gdk_pixbuf_animation_is_static_image(gtk_image_get_animation(GTK_IMAGE(image_widget))));const gchar *html=g_object_get_data(G_OBJECT(viewer()),"insight-rendered-html");g_assert_nonnull(strstr(html,"<video controls"));g_assert_nonnull(strstr(html,"<table>"));g_assert_nonnull(strstr(html,"&lt;script&gt;"));g_assert_null(strstr(html,"<script>"));g_signal_emit_by_name(media_button(viewer()),"clicked");const gchar *media_path=g_object_get_data(G_OBJECT(viewer()),"insight-media-path");g_assert_nonnull(media_path);gchar *exported=NULL;g_assert_true(g_file_get_contents(media_path,&exported,NULL,NULL));g_assert_cmpstr(exported,==,html);g_free(exported);g_unlink(media_path);
  gchar *rst=g_build_filename(directory,"test.rst",NULL);g_assert_true(g_file_set_contents(rst,"RST heading\n===========\n\n.. code-block:: lua\n\n   local answer = 42\n\n.. image:: demo.gif\n",-1,NULL));insight_markup_open_path(rst);g_free(rst);g_assert_true(has_text(viewer(),"RST heading"));g_assert_true(has_text(viewer(),"local answer = 42"));g_assert_nonnull(animation(viewer()));preview_checks();phase++;
 } else if(phase==2) {
  GtkWidget *command=named(window,"insight-search-project-command");gtk_menu_item_activate(GTK_MENU_ITEM(command));g_assert_true(gtk_widget_get_visible(named(window,"insight-project-search-panel")));g_assert_false(gtk_widget_get_visible(named(window,"insight-editor-search-panel")));
  GtkWidget *scope=named(window,"insight-search-scope");gint before=gtk_combo_box_get_active(GTK_COMBO_BOX(scope));g_timeout_add(20,cancel_folder,NULL);gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-search-folder-command")));g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(scope)),==,before);
  GtkWidget *entry=named(window,"insight-project-query");GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(entry));event->key.keyval=GDK_KEY_Escape;gtk_widget_event(entry,event);gdk_event_free(event);g_assert_false(gtk_widget_get_visible(named(window,"insight-project-search-panel")));
  shortcut(GDK_KEY_f);phase++;
 } else if(phase==3 || phase==4) {
  GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;for(GList *p=windows;p;p=p->next) if(GTK_IS_DIALOG(p->data) && gtk_widget_get_visible(p->data)) dialog=p->data;g_list_free(windows);g_assert_nonnull(dialog);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);
  if(phase==3) { shortcut(GDK_KEY_h);phase++; }else phase++;
 } else {
  g_signal_emit_by_name(named(window,"insight-help-tool"),"clicked");g_assert_nonnull(viewer());
  g_print("Real editor GUI: hidden search startup, shared menu icons, icon-only toolbar, bundled Help/tutorials, Markdown/RST formatting, GIF animation, video card, TOC/unique anchors, Unicode/literal page search, unsaved and untitled preview/reload, closed-buffer guard, Escape close and native Find/Replace commands passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY"))) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("help-search-gui-%u",g_random_int());directory=g_build_filename(cwd,name,NULL);profile=g_build_filename(directory,"profile",NULL);g_free(cwd);g_free(name);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n[tools]\nbrowser_cmd=/bin/true\n",-1,NULL));g_free(config);gchar *settings_dir=g_build_filename(profile,"insight",NULL);g_assert_cmpint(g_mkdir_with_parents(settings_dir,0700),==,0);gchar *settings=g_build_filename(settings_dir,"workbench.conf",NULL);g_assert_true(g_file_set_contents(settings,"[workbench]\nsearch-toolbars=true\n",-1,NULL));g_free(settings_dir);g_free(settings);
 gchar *argv[]={"help-search-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(35,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(directory);g_free(profile);return result;
}
