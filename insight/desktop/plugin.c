/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <geanyplugin.h>
#include "runtime.h"
#include "navigation.h"
#include <string.h>
static GtkWidget *item, *commands_item, *commands_menu;
static InsightSession *session;
static Navigation *navigation;
typedef struct { GeanyDocument *doc; gchar *scratch; } Host;
static const char *get_text(void *ctx) {
 Host *h=ctx; g_free(h->scratch);
 h->scratch=sci_get_contents(h->doc->editor->sci,sci_get_length(h->doc->editor->sci)+1);
 return h->scratch;
}
static const char *get_selection(void *ctx) {
 Host *h=ctx; g_free(h->scratch);
 h->scratch=sci_get_selection_contents(h->doc->editor->sci); return h->scratch;
}
static int replace_selection(void *ctx,const char *text) {
 Host *h=ctx; sci_replace_sel(h->doc->editor->sci,text); return 1;
}
static gboolean read_script(const gchar *path,gchar **source,gsize *size,GError **error) {
 *source=NULL; *size=0;
 GFile *file=g_file_new_for_path(path);
 GFileInputStream *stream=g_file_read(file,NULL,error); g_object_unref(file);
 if(!stream) return FALSE;
 GByteArray *bytes=g_byte_array_new(); char buffer[8192]; gssize count;
 while((count=g_input_stream_read(G_INPUT_STREAM(stream),buffer,sizeof buffer,NULL,error))>0) {
  if((gsize)count>256*1024-bytes->len) {
   g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Lua file exceeds 256 KiB"); count=-1; break;
  }
  g_byte_array_append(bytes,(const guint8 *)buffer,(guint)count);
 }
 g_object_unref(stream);
 if(count<0) { g_byte_array_free(bytes,TRUE); return FALSE; }
 *size=bytes->len; g_byte_array_append(bytes,(const guint8 *)"",1);
 *source=(gchar *)g_byte_array_free(bytes,FALSE); return TRUE;
}
static void invoke_command(GtkMenuItem *menu,gpointer data) {
 (void)data;
 const char *name=g_object_get_data(G_OBJECT(menu),"insight-command-id");
 GeanyDocument *doc=document_get_current();
 if(!doc) { ui_set_statusbar(TRUE,"Lua: no active document"); return; }
 Host h={doc,NULL}; InsightHost host={&h,get_text,get_selection,replace_selection,navigation_index(navigation)};
 char error[512]; sci_start_undo_action(doc->editor->sci);
 int result=insight_session_invoke(session,name,&host,error,sizeof error);
 sci_end_undo_action(doc->editor->sci); g_free(h.scratch);
 ui_set_statusbar(TRUE,"Lua: %s",result ? (error[0] ? error : "runtime unavailable") : "completed");
}
static void rebuild_commands(void) {
 GList *items=gtk_container_get_children(GTK_CONTAINER(commands_menu));
 /* First three rows are load, unload and separator. */
 for(GList *row=g_list_nth(items,3);row;row=row->next) gtk_widget_destroy(GTK_WIDGET(row->data));
 g_list_free(items);
 size_t count=insight_session_command_count(session);
 for(size_t n=0;n<count;n++) {
  const char *name=insight_session_command_name(session,n);
  const char *label=insight_session_command_label(session,n);
  GtkWidget *command=gtk_menu_item_new_with_label(g_utf8_validate(label,-1,NULL) ? label : name);
  g_object_set_data_full(G_OBJECT(command),"insight-command-id",g_strdup(name),g_free);
  g_signal_connect(command,"activate",G_CALLBACK(invoke_command),NULL);
  gtk_container_add(GTK_CONTAINER(commands_menu),command); ui_add_document_sensitive(command); gtk_widget_show(command);
 }
}
static void load_extension(GtkMenuItem *menu,gpointer data) {
 (void)menu; GeanyPlugin *plugin=data;
 GtkWidget *dialog=gtk_file_chooser_dialog_new("Load Lua extension",
 GTK_WINDOW(plugin->geany_data->main_widgets->window),GTK_FILE_CHOOSER_ACTION_OPEN,
 "Cancel",GTK_RESPONSE_CANCEL,"Load",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
  gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
  gchar *source=NULL; gsize size=0; GError *io_error=NULL;
  if(!read_script(path,&source,&size,&io_error)) {
   ui_set_statusbar(TRUE,"Lua: %s",io_error->message); g_error_free(io_error);
  } else {
   char error[512]; int result=insight_session_load(session,source,size,error,sizeof error);
   if(!result) rebuild_commands();
   ui_set_statusbar(TRUE,"Lua: %s",result ? (error[0] ? error : "runtime unavailable") : "extension loaded");
  }
  g_free(source); g_free(path);
 }
 gtk_widget_destroy(dialog);
}
static void unload_extension(GtkMenuItem *menu,gpointer data) {
 (void)menu; (void)data;
 InsightSession *next=insight_session_new();
 if(!next) { ui_set_statusbar(TRUE,"Lua: runtime unavailable"); return; }
 insight_session_free(session); session=next; rebuild_commands();
 ui_set_statusbar(TRUE,"Lua: extension unloaded");
}
static void activate(GtkMenuItem *menu,gpointer data) {
 (void)menu; GeanyPlugin *plugin=data;
 GtkWidget *dialog=gtk_file_chooser_dialog_new("Run Lua script",
 GTK_WINDOW(plugin->geany_data->main_widgets->window),GTK_FILE_CHOOSER_ACTION_OPEN,
 "Cancel",GTK_RESPONSE_CANCEL,"Run",GTK_RESPONSE_ACCEPT,NULL);
 if (gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
  gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
  gchar *source=NULL; gsize size=0; GError *io_error=NULL;
  GeanyDocument *doc=document_get_current();
  if (!doc) ui_set_statusbar(TRUE,"Lua: no active document");
  else if (!read_script(path,&source,&size,&io_error)) {
   ui_set_statusbar(TRUE,"Lua: %s",io_error->message); g_error_free(io_error);
  } else {
   Host h={doc,NULL}; InsightHost host={&h,get_text,get_selection,replace_selection,navigation_index(navigation)};
   char error[512]; sci_start_undo_action(doc->editor->sci);
   int result=insight_run(&host,source,size,error,sizeof error);
   sci_end_undo_action(doc->editor->sci); g_free(h.scratch);
   ui_set_statusbar(TRUE,"Lua: %s",result ? (error[0] ? error : "runtime unavailable") : "completed");
  }
  g_free(source); g_free(path);
 }
 gtk_widget_destroy(dialog);
}
static gboolean init(GeanyPlugin *plugin,gpointer data) {
 (void)data; navigation=navigation_new(plugin);
 if(!navigation) return FALSE;
 session=insight_session_new();
 if(!session) { navigation_free(navigation); navigation=NULL; return FALSE; }
 commands_item=gtk_menu_item_new_with_label("Insight: Lua commands"); commands_menu=gtk_menu_new();
 gtk_menu_item_set_submenu(GTK_MENU_ITEM(commands_item),commands_menu);
 GtkWidget *load=gtk_menu_item_new_with_label("Load extension…");
 GtkWidget *unload=gtk_menu_item_new_with_label("Unload extension");
 gtk_container_add(GTK_CONTAINER(commands_menu),load); gtk_container_add(GTK_CONTAINER(commands_menu),unload);
 gtk_container_add(GTK_CONTAINER(commands_menu),gtk_separator_menu_item_new());
 g_signal_connect(load,"activate",G_CALLBACK(load_extension),plugin);
 g_signal_connect(unload,"activate",G_CALLBACK(unload_extension),plugin);
 gtk_container_add(GTK_CONTAINER(plugin->geany_data->main_widgets->tools_menu),commands_item);
 gtk_widget_show_all(commands_item);
 item=gtk_menu_item_new_with_label("Insight: Run Lua script…");
 gtk_container_add(GTK_CONTAINER(plugin->geany_data->main_widgets->tools_menu),item);
 g_signal_connect(item,"activate",G_CALLBACK(activate),plugin);
 ui_add_document_sensitive(item); gtk_widget_show(item); return TRUE;
}
static void cleanup(GeanyPlugin *plugin,gpointer data) {
 (void)plugin; (void)data; gtk_widget_destroy(item); item=NULL;
 gtk_widget_destroy(commands_item); commands_item=NULL; commands_menu=NULL;
 insight_session_free(session); session=NULL;
 navigation_free(navigation); navigation=NULL;
}
void geany_load_module(GeanyPlugin *plugin) {
 plugin->info->name="Insight Lua"; plugin->info->description="Shared embedded Lua editor runtime";
 plugin->info->version="0.5.0"; plugin->info->author="Insight contributors";
 plugin->funcs->init=init; plugin->funcs->cleanup=cleanup;
 GEANY_PLUGIN_REGISTER(plugin,225);
}
