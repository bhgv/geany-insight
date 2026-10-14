/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#undef SSM
#define SSM(sci,msg,w,l) scintilla_send_message((sci),(msg),(w),(l))
static GtkWidget *window,*insert,*clips;
static ScintillaObject *sci;
static gchar *profile,*expected,*note_path;
static guint phase;
static gchar *source_path;
static GtkWidget *find(GtkWidget *root,const char *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root)); GtkWidget *found=NULL; for(GList *p=children;p && !found;p=p->next) found=find(p->data,name); g_list_free(children); if(found) return found; } return NULL;
}
static GtkWidget *dialog_named(const char *title) {
 GList *windows=gtk_window_list_toplevels(); GtkWidget *dialog=NULL; for(GList *p=windows;p;p=p->next) if(GTK_IS_DIALOG(p->data) && !g_strcmp0(gtk_window_get_title(GTK_WINDOW(p->data)),title)) dialog=p->data; g_list_free(windows); g_assert_nonnull(dialog); return dialog;
}
static void text_is(const gchar *expected_text) { gchar *actual=sci_get_contents(sci,sci_get_length(sci)+1); g_assert_cmpstr(actual,==,expected_text); g_free(actual); }
static gboolean parameters(gpointer data) {
 gint action=GPOINTER_TO_INT(data); GtkWidget *dialog=dialog_named("Template parameters"),*entry=find(dialog,"insight-template-variable-name"); g_assert_nonnull(entry); gtk_entry_set_text(GTK_ENTRY(entry),"имя");
 if(action==2) SSM(sci,SCI_INSERTTEXT,0,(sptr_t)"changed:");
 gtk_dialog_response(GTK_DIALOG(dialog),action==1 ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_ACCEPT); return G_SOURCE_REMOVE;
}
static gboolean create(gpointer data) {
 gboolean duplicate=GPOINTER_TO_INT(data); GtkWidget *dialog=dialog_named("New code template"),*name=find(dialog,"insight-template-name"),*body=find(dialog,"insight-template-body"); g_assert_nonnull(name); g_assert_nonnull(body); gtk_entry_set_text(GTK_ENTRY(name),duplicate ? "Template" : "Additional"); gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(body)),"C-only $cursor$",-1);
 if(!duplicate) { GtkWidget *languages=find(dialog,"insight-template-languages"); g_assert_nonnull(languages); GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(languages)); GtkTreeIter iter; guint selected=0;
  GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(gtk_tree_view_get_column(GTK_TREE_VIEW(languages),0))); g_assert_nonnull(cells);
  for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gchar *profile_name=NULL; gtk_tree_model_get(model,&iter,1,&profile_name,-1); if(!strcmp(profile_name,"C") || !strcmp(profile_name,"C++")) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter); gchar *index=gtk_tree_path_to_string(path); g_signal_emit_by_name(cells->data,"toggled",index); g_free(index); gtk_tree_path_free(path); selected++; } g_free(profile_name); }
  g_list_free(cells); g_assert_cmpuint(selected,==,2);
 } gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT); return G_SOURCE_REMOVE;
}
static gboolean step(gpointer data) {
 (void)data; GeanyDocument *doc=document_get_current(); g_assert_nonnull(doc);
 if(phase==0) {
  sci=doc->editor->sci; window=gtk_widget_get_toplevel(GTK_WIDGET(sci)); g_assert_cmpint(SSM(sci,SCI_GETEOLMODE,0,0),==,SC_EOL_CRLF);
  GtkWidget *item=find(ui_lookup_widget(window,"tools1_menu"),"insight-templates-command"); g_assert_nonnull(item); gtk_menu_item_activate(GTK_MENU_ITEM(item)); GtkWidget *tool=find(window,"insight-templates-tool"); g_assert_nonnull(tool); g_assert_cmpstr(g_object_get_data(G_OBJECT(gtk_image_menu_item_get_image(GTK_IMAGE_MENU_ITEM(item))),"insight-icon-name"),==,"snippets"); g_assert_cmpstr(g_object_get_data(G_OBJECT(gtk_tool_button_get_icon_widget(GTK_TOOL_BUTTON(tool))),"insight-icon-name"),==,"snippets");
  clips=find(window,"insight-clips"); insert=find(window,"insight-insert-template"); g_assert_nonnull(clips); g_assert_nonnull(insert); g_assert_cmpstr(g_object_get_data(G_OBJECT(gtk_button_get_image(GTK_BUTTON(insert))),"insight-icon-name"),==,"snippets"); g_assert_true(gtk_widget_get_mapped(clips)); GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(clips)); g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,1); GtkTreeIter iter; g_assert_true(gtk_tree_model_get_iter_first(model,&iter)); gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(clips)),&iter);
  SSM(sci,SCI_SETSEL,0,4); g_idle_add(parameters,NULL); gtk_button_clicked(GTK_BUTTON(insert));
 } else if(phase==1) {
  text_is(expected); g_assert_cmpint(sci_get_current_position(sci),==,(gint)strlen("source.c:имя\r\nbody")); SSM(sci,SCI_UNDO,0,0); text_is("body\r\n"); SSM(sci,SCI_REDO,0,0); text_is(expected);
  g_idle_add(parameters,GINT_TO_POINTER(1)); gtk_button_clicked(GTK_BUTTON(insert)); text_is(expected);
 } else if(phase==2) {
  SSM(sci,SCI_SETREADONLY,TRUE,0); gtk_button_clicked(GTK_BUTTON(insert)); text_is(expected); SSM(sci,SCI_SETREADONLY,FALSE,0);
  g_idle_add(parameters,GINT_TO_POINTER(2)); gtk_button_clicked(GTK_BUTTON(insert)); gchar *changed=g_strconcat("changed:",expected,NULL); text_is(changed); g_free(changed);
 } else if(phase==3) {
  GtkWidget *button=find(window,"insight-new-template"); g_assert_nonnull(button); g_idle_add(create,NULL); gtk_button_clicked(GTK_BUTTON(button)); g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(clips)),NULL),==,2);
  g_idle_add(create,GINT_TO_POINTER(1)); gtk_button_clicked(GTK_BUTTON(button)); g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(clips)),NULL),==,2);
 } else if(phase==4) {
  gchar *path=g_build_filename(profile,"python.py",NULL); g_file_set_contents(path,"print('python')\n",-1,NULL); g_assert_nonnull(document_open_file(path,FALSE,NULL,NULL)); g_free(path);
 } else if(phase==5) {
  sci=doc->editor->sci; g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(clips)),NULL),==,1);
  GtkWidget *all=find(window,"insight-clips-all-languages"); g_assert_nonnull(all); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(all),TRUE); GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(clips)); g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,2); GtkTreeIter iter; gboolean selected=FALSE;
  for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gchar *name=NULL; gtk_tree_model_get(model,&iter,0,&name,-1); if(!strcmp(name,"Additional")) { gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(clips)),&iter); selected=TRUE; } g_free(name); }
  g_assert_true(selected); gtk_button_clicked(GTK_BUTTON(insert)); text_is("print('python')\n");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(all),FALSE); g_assert_nonnull(document_open_file(source_path,FALSE,NULL,NULL));
 } else if(phase==6) {
  sci=doc->editor->sci; g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(clips)),NULL),==,2);
  GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(clips)); GtkTreeIter iter;
  for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gchar *name=NULL; gtk_tree_model_get(model,&iter,0,&name,-1); if(!strcmp(name,"Additional")) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(clips)),&iter); g_free(name); }
  sci_set_current_position(sci,0,FALSE); gtk_button_clicked(GTK_BUTTON(insert)); gchar *actual=sci_get_contents(sci,sci_get_length(sci)+1); g_assert_true(g_str_has_prefix(actual,"C-only ")); g_free(actual);
 } else {
  document_set_text_changed(doc,FALSE); gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1"))); return G_SOURCE_REMOVE;
 }
 phase++; return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data; g_error("Template GUI timeout at phase %u",phase); return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("snippets-gui-%u",g_random_int()); profile=g_build_filename(cwd,name,NULL); g_free(cwd); g_free(name); g_mkdir_with_parents(profile,0700); gchar *config=g_build_filename(profile,"geany.conf",NULL); g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL); g_free(config);
 gchar *folder=g_build_filename(profile,"insight",NULL); g_mkdir_with_parents(folder,0700); note_path=g_build_filename(folder,"notes.conf",NULL); g_free(folder); GKeyFile *notes=g_key_file_new(); g_key_file_set_integer(notes,"notes","version",1); g_key_file_set_integer(notes,"notes","count",1); g_key_file_set_integer(notes,"note-0","kind",2); g_key_file_set_string(notes,"note-0","name","Template"); g_key_file_set_string(notes,"note-0","path",""); g_key_file_set_uint64(notes,"note-0","line",0); g_key_file_set_string(notes,"note-0","text","$filename$:$name$\n$selection$$cursor$\n$date$\n$$name$$"); gsize length=0; gchar *serialized=g_key_file_to_data(notes,&length,NULL); g_file_set_contents(note_path,serialized,length,NULL); g_free(serialized); g_key_file_unref(notes);
 GDateTime *now=g_date_time_new_now_local(); gchar *date=g_date_time_format(now,"%Y-%m-%d"); expected=g_strconcat("source.c:имя\r\nbody\r\n",date,"\r\n$name$\r\n",NULL); g_free(date); g_date_time_unref(now);
 gchar *path=g_build_filename(profile,"source.c",NULL); source_path=g_strdup(path); g_file_set_contents(path,"body\r\n",-1,NULL); gchar *argv[]={"snippet-gui","--new-instance","--no-session","--config",profile,path,NULL}; g_timeout_add(300,step,NULL); g_timeout_add_seconds(20,timeout,NULL); g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL); gint result=main_lib(6,argv);
 notes=g_key_file_new(); g_assert_true(g_key_file_load_from_file(notes,note_path,G_KEY_FILE_NONE,NULL)); g_assert_cmpint(g_key_file_get_integer(notes,"notes","count",NULL),==,2); gchar *body=g_key_file_get_string(notes,"note-0","text",NULL); g_assert_cmpstr(body,==,"$filename$:$name$\n$selection$$cursor$\n$date$\n$$name$$"); g_free(body); g_key_file_unref(notes); g_free(profile); g_free(path); g_free(note_path); g_free(expected); g_free(source_path); g_print("Code template GUI passed: menu/tool, variables, UTF-8, selection, cursor, CRLF, one undo/redo, cancel, read-only, stale source, create/persist, duplicate rejection and language restrictions\n"); return result;
}
