/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <geanyplugin.h>
#include "runtime.h"
#include "navigation.h"
#include "extensions.h"
#include <string.h>
#include <limits.h>
static GtkWidget *item, *commands_item, *commands_menu;
static InsightExtensions *extensions;
static GtkAccelGroup *accelerators;
static GtkWindow *main_window;
static GtkWidget *palette_item;
static guint palette_key;
static GdkModifierType palette_mods;
static Navigation *navigation;
typedef struct { GeanyDocument *doc; gchar *scratch; GeanyDocument *undo_doc; int message_set; } Host;
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
static unsigned long get_position(void *ctx) {
 Host *h=ctx; return (unsigned long)sci_get_current_position(h->doc->editor->sci);
}
static int go_line(void *ctx,unsigned long line) {
 Host *h=ctx;
 if(!line || line>(unsigned long)sci_get_line_count(h->doc->editor->sci)) return 0;
 return navqueue_goto_line(h->doc,h->doc,(gint)line);
}
static int open_file(void *ctx,const char *path) {
 Host *h=ctx; if(!g_utf8_validate(path,-1,NULL) || !g_path_is_absolute(path)) return 0;
 gchar *locale=utils_get_locale_from_utf8(path); GeanyDocument *next=document_open_file(locale,FALSE,NULL,NULL); g_free(locale);
 if(!next) return 0;
 if(h->undo_doc && h->undo_doc!=next) { sci_end_undo_action(h->undo_doc->editor->sci); sci_start_undo_action(next->editor->sci); h->undo_doc=next; }
 h->doc=next; return 1;
}
static int save_file(void *ctx) { Host *h=ctx; return h->doc->file_name && document_save_file(h->doc,FALSE); }
static const char *filename(void *ctx) { return ((Host *)ctx)->doc->file_name; }
static int selection_range(void *ctx,unsigned long *start,unsigned long *end) {
 Host *h=ctx; *start=(unsigned long)sci_get_selection_start(h->doc->editor->sci); *end=(unsigned long)sci_get_selection_end(h->doc->editor->sci); return 1;
}
static int valid_range(Host *h,unsigned long start,unsigned long end) {
 ScintillaObject *sci=h->doc->editor->sci; unsigned long length=(unsigned long)sci_get_length(sci);
 if(start>end || end>length) return 0;
 return (start==length || (((unsigned char)sci_get_char_at(sci,(gint)start)&0xc0)!=0x80)) &&
  (end==length || (((unsigned char)sci_get_char_at(sci,(gint)end)&0xc0)!=0x80));
}
static int set_selection(void *ctx,unsigned long start,unsigned long end) {
 Host *h=ctx; if(!valid_range(h,start,end)) return 0; sci_set_selection_start(h->doc->editor->sci,(gint)start); sci_set_selection_end(h->doc->editor->sci,(gint)end); return 1;
}
static unsigned long line_count(void *ctx) { return (unsigned long)sci_get_line_count(((Host *)ctx)->doc->editor->sci); }
static const char *line_text(void *ctx,unsigned long line) {
 Host *h=ctx; if(!line || line>(unsigned long)sci_get_line_count(h->doc->editor->sci)) return NULL;
 g_free(h->scratch); h->scratch=sci_get_line(h->doc->editor->sci,(gint)line-1); return h->scratch;
}
static int replace_range(void *ctx,unsigned long start,unsigned long end,const char *text) {
 Host *h=ctx; if(!valid_range(h,start,end) || !g_utf8_validate(text,-1,NULL)) return 0;
 sci_set_target_start(h->doc->editor->sci,(gint)start); sci_set_target_end(h->doc->editor->sci,(gint)end);
 return sci_replace_target(h->doc->editor->sci,text,FALSE)>=0;
}
static int insert(void *ctx,unsigned long at,const char *text) { return replace_range(ctx,at,at,text); }
static int message(void *ctx,const char *text) {
 Host *h=ctx; if(!g_utf8_validate(text,-1,NULL)) return 0; h->message_set=1; ui_set_statusbar(TRUE,"%s",text); return 1;
}
static const InsightEditorAPI host_api={.position=get_position,.goto_line=go_line,.message=message,.insert=insert,.replace_range=replace_range,.line_text=line_text,.line_count=line_count,.set_selection=set_selection,.selection_range=selection_range,.filename=filename,.save_file=save_file,.open_file=open_file};
static void invoke_named_command(const char *name) {
 if(insight_session_is_running(insight_extensions_session(extensions))) {
  ui_set_statusbar(TRUE,"Lua: another command or event is running"); return;
 }
 GeanyDocument *doc=document_get_current();
 if(!doc) { ui_set_statusbar(TRUE,"Lua: no active document"); return; }
 Host h={.doc=doc}; InsightHost host={.context=&h,.text=get_text,.selection=get_selection,.replace_selection=replace_selection,.index=navigation_index(navigation),.api=&host_api};
 char error[512]; h.undo_doc=doc; sci_start_undo_action(doc->editor->sci);
 int result=insight_session_invoke(insight_extensions_session(extensions),name,&host,error,sizeof error);
 if(h.undo_doc) sci_end_undo_action(h.undo_doc->editor->sci);
 g_free(h.scratch);
 if(result || !h.message_set) ui_set_statusbar(TRUE,"Lua: %s",result ? (error[0] ? error : "runtime unavailable") : "completed");
}
static void invoke_command(GtkMenuItem *menu,gpointer data) {
 (void)data; invoke_named_command(g_object_get_data(G_OBJECT(menu),"insight-command-id"));
}
typedef struct { GtkListStore *store; GtkWidget *view; } Palette;
static void palette_filter(GtkEditable *entry,gpointer data) {
 Palette *p=data; gtk_list_store_clear(p->store);
 gchar *needle=g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(entry)),-1);
 InsightSession *state=insight_extensions_session(extensions); gboolean first=TRUE;
 for(size_t n=0;n<insight_session_command_count(state);n++) {
  const char *id=insight_session_command_name(state,n),*label=insight_session_command_label(state,n);
  if(!g_utf8_validate(label,-1,NULL)) label=id;
  gchar *joined=g_strconcat(label,"\n",id,NULL),*folded=g_utf8_casefold(joined,-1);
  if(strstr(folded,needle)) {
   GtkTreeIter iter; gtk_list_store_append(p->store,&iter); gtk_list_store_set(p->store,&iter,0,label,1,id,-1);
   if(first) { gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)),&iter); first=FALSE; }
  }
  g_free(joined); g_free(folded);
 }
 g_free(needle);
}
static void palette_row(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer dialog) {
 (void)view; (void)path; (void)column; gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);
}
static void palette_enter(GtkEntry *entry,gpointer dialog) { (void)entry; gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT); }
static void show_palette(GtkMenuItem *item,gpointer data) {
 (void)item; (void)data;
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Lua command palette",main_window,GTK_DIALOG_DESTROY_WITH_PARENT,
 "Cancel",GTK_RESPONSE_CANCEL,"Run",GTK_RESPONSE_ACCEPT,NULL);
 gtk_window_set_default_size(GTK_WINDOW(dialog),600,400);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)),*entry=gtk_search_entry_new();
 GtkListStore *store=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_STRING);
 GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
 for(int n=0;n<2;n++) gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes(
  n ? "Command ID" : "Command",gtk_cell_renderer_text_new(),"text",n,NULL));
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),view);
 gtk_box_pack_start(GTK_BOX(box),entry,FALSE,FALSE,4); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,4);
 Palette palette={store,view}; g_signal_connect(entry,"changed",G_CALLBACK(palette_filter),&palette);
 g_signal_connect(entry,"activate",G_CALLBACK(palette_enter),dialog); g_signal_connect(view,"row-activated",G_CALLBACK(palette_row),dialog);
 palette_filter(GTK_EDITABLE(entry),&palette); gtk_widget_show_all(dialog); gchar *id=NULL;
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
  GtkTreeIter iter;
  if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),NULL,&iter)) gtk_tree_model_get(GTK_TREE_MODEL(store),&iter,1,&id,-1);
 }
 gtk_widget_destroy(dialog); g_object_unref(store);
 if(id) invoke_named_command(id);
 g_free(id);
}
static void rebuild_commands(void) {
 if(accelerators) {
  if(palette_item) gtk_widget_remove_accelerator(palette_item,accelerators,palette_key,palette_mods);
  gtk_window_remove_accel_group(main_window,accelerators); g_object_unref(accelerators);
 }
 accelerators=gtk_accel_group_new(); gtk_window_add_accel_group(main_window,accelerators);
 guint used_keys[128]={0}; GdkModifierType used_mods[128]={0};
 GList *items=gtk_container_get_children(GTK_CONTAINER(commands_menu));
 /* First four rows are load, reload, unload and separator. */
 for(GList *row=g_list_nth(items,4);row;row=row->next) gtk_widget_destroy(GTK_WIDGET(row->data));
 g_list_free(items);
 size_t count=insight_session_command_count(insight_extensions_session(extensions));
 for(size_t n=0;n<count;n++) {
  const char *name=insight_session_command_name(insight_extensions_session(extensions),n);
  const char *label=insight_session_command_label(insight_extensions_session(extensions),n);
  GtkWidget *command=gtk_menu_item_new_with_label(g_utf8_validate(label,-1,NULL) ? label : name);
  g_object_set_data_full(G_OBJECT(command),"insight-command-id",g_strdup(name),g_free);
  g_signal_connect(command,"activate",G_CALLBACK(invoke_command),NULL);
  gtk_container_add(GTK_CONTAINER(commands_menu),command); ui_add_document_sensitive(command); gtk_widget_show(command);
  const char *shortcut=insight_session_command_shortcut(insight_extensions_session(extensions),n);
  guint key=0; GdkModifierType mods=0; gtk_accelerator_parse(shortcut,&key,&mods);
  gboolean duplicate=key==palette_key && mods==palette_mods;
  for(size_t k=0;k<n;k++) if(key && used_keys[k]==key && used_mods[k]==mods) duplicate=TRUE;
  if(key && gtk_accelerator_valid(key,mods) && !duplicate) {
   used_keys[n]=key; used_mods[n]=mods;
   gtk_widget_add_accelerator(command,"activate",accelerators,key,mods,GTK_ACCEL_VISIBLE);
  }
 }
 if(palette_item) gtk_widget_add_accelerator(palette_item,"activate",accelerators,palette_key,palette_mods,GTK_ACCEL_VISIBLE);
}
static void load_extension(GtkMenuItem *menu,gpointer data) {
 (void)menu; GeanyPlugin *plugin=data;
 GtkWidget *dialog=gtk_file_chooser_dialog_new("Load Lua extension",
 GTK_WINDOW(plugin->geany_data->main_widgets->window),GTK_FILE_CHOOSER_ACTION_OPEN,
 "Cancel",GTK_RESPONSE_CANCEL,"Load",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
  gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
  GError *error=NULL;
  if(insight_extensions_select(extensions,path,&error)) {
   rebuild_commands(); ui_set_statusbar(TRUE,"Lua: extension loaded and saved for startup");
  } else { ui_set_statusbar(TRUE,"Lua: %s",error->message); g_error_free(error); }
  g_free(path);
 }
 gtk_widget_destroy(dialog);
}
static void reload_extension(GtkMenuItem *menu,gpointer data) {
 (void)menu; (void)data; GError *error=NULL;
 if(insight_extensions_restore(extensions,&error)) {
  rebuild_commands(); ui_set_statusbar(TRUE,"Lua: saved extension selection restored");
 } else { ui_set_statusbar(TRUE,"Lua: %s",error->message); g_error_free(error); }
}
static void unload_extension(GtkMenuItem *menu,gpointer data) {
 (void)menu; (void)data;
 GError *error=NULL;
 if(!insight_extensions_unload(extensions,&error)) {
  ui_set_statusbar(TRUE,"Lua: %s",error->message); g_error_free(error); return;
 }
 rebuild_commands(); ui_set_statusbar(TRUE,"Lua: extension unloaded; startup loading disabled");
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
  else if (!insight_read_lua_file(path,&source,&size,&io_error)) {
   ui_set_statusbar(TRUE,"Lua: %s",io_error->message); g_error_free(io_error);
  } else {
   Host h={.doc=doc}; InsightHost host={.context=&h,.text=get_text,.selection=get_selection,.replace_selection=replace_selection,.index=navigation_index(navigation),.api=&host_api};
   char error[512]; h.undo_doc=doc; sci_start_undo_action(doc->editor->sci);
   int result=insight_run(&host,source,size,error,sizeof error);
   if(h.undo_doc) sci_end_undo_action(h.undo_doc->editor->sci);
 g_free(h.scratch);
   if(result || !h.message_set) ui_set_statusbar(TRUE,"Lua: %s",result ? (error[0] ? error : "runtime unavailable") : "completed");
  }
  g_free(source); g_free(path);
 }
 gtk_widget_destroy(dialog);
}
static void emit_document_event(const char *name,GeanyDocument *doc) {
 InsightSession *state=insight_extensions_session(extensions);
 if(!extensions || !DOC_VALID(doc) || insight_session_is_running(state)) return;
 Host h={.doc=doc}; InsightHost host={.context=&h,.text=get_text,.selection=get_selection,
  .replace_selection=replace_selection,.index=navigation_index(navigation),.api=&host_api};
 char error[512];
 int result=insight_session_emit(state,name,doc->file_name,&host,error,sizeof error); g_free(h.scratch);
 if(result) ui_set_statusbar(TRUE,"Lua event: %s",error[0] ? error : "runtime unavailable");
}
static void on_save(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object; (void)data; emit_document_event("document_save",doc);
}
static void on_activate(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object; (void)data; emit_document_event("document_activate",doc);
}
static void on_open(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object; (void)data; emit_document_event("document_open",doc);
}
static void on_close(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object; (void)data; emit_document_event("document_close",doc);
}
static PluginCallback callbacks[]={
 {"document-close",(GCallback)on_close,TRUE,NULL},
 {"document-open",(GCallback)on_open,TRUE,NULL},
 {"document-save",(GCallback)on_save,TRUE,NULL},
 {"document-activate",(GCallback)on_activate,TRUE,NULL},
 {NULL,NULL,FALSE,NULL}
};
static gboolean init(GeanyPlugin *plugin,gpointer data) {
 (void)data; main_window=GTK_WINDOW(plugin->geany_data->main_widgets->window); navigation=navigation_new(plugin);
 if(!navigation) return FALSE;
 gchar *settings=g_build_filename(plugin->geany_data->app->configdir,"plugins","insight","extension.conf",NULL);
 extensions=insight_extensions_new(settings); g_free(settings);
 if(!extensions) { navigation_free(navigation); navigation=NULL; return FALSE; }
 commands_item=gtk_menu_item_new_with_label("Insight: Lua commands"); commands_menu=gtk_menu_new();
 gtk_menu_item_set_submenu(GTK_MENU_ITEM(commands_item),commands_menu);
 GtkWidget *load=gtk_menu_item_new_with_label("Load extension…");
 GtkWidget *reload=gtk_menu_item_new_with_label("Reload saved extension");
 GtkWidget *unload=gtk_menu_item_new_with_label("Unload extension");
 gtk_container_add(GTK_CONTAINER(commands_menu),load); gtk_container_add(GTK_CONTAINER(commands_menu),reload);
 gtk_container_add(GTK_CONTAINER(commands_menu),unload);
 gtk_container_add(GTK_CONTAINER(commands_menu),gtk_separator_menu_item_new());
 g_signal_connect(load,"activate",G_CALLBACK(load_extension),plugin);
 g_signal_connect(reload,"activate",G_CALLBACK(reload_extension),plugin);
 g_signal_connect(unload,"activate",G_CALLBACK(unload_extension),plugin);
 gtk_container_add(GTK_CONTAINER(plugin->geany_data->main_widgets->tools_menu),commands_item);
 gtk_widget_show_all(commands_item);
 item=gtk_menu_item_new_with_label("Insight: Run Lua script…");
 gtk_container_add(GTK_CONTAINER(plugin->geany_data->main_widgets->tools_menu),item);
 g_signal_connect(item,"activate",G_CALLBACK(activate),plugin);
 ui_add_document_sensitive(item); gtk_widget_show(item);
 palette_item=gtk_menu_item_new_with_label("Insight: Command palette…");
 gtk_container_add(GTK_CONTAINER(plugin->geany_data->main_widgets->tools_menu),palette_item);
 gtk_accelerator_parse("<Primary><Shift>p",&palette_key,&palette_mods);
 g_signal_connect(palette_item,"activate",G_CALLBACK(show_palette),NULL);
 ui_add_document_sensitive(palette_item); gtk_widget_show(palette_item);
 GError *error=NULL;
 if(insight_extensions_restore(extensions,&error)) rebuild_commands();
 else { ui_set_statusbar(TRUE,"Lua: startup extension not loaded: %s",error->message); g_error_free(error); }
 return TRUE;
}
static void cleanup(GeanyPlugin *plugin,gpointer data) {
 (void)plugin; (void)data; gtk_widget_destroy(item); item=NULL;
 gtk_widget_destroy(palette_item); palette_item=NULL;
 gtk_widget_destroy(commands_item); commands_item=NULL; commands_menu=NULL;
 if(accelerators) { gtk_window_remove_accel_group(main_window,accelerators); g_object_unref(accelerators); accelerators=NULL; }
 insight_extensions_free(extensions); extensions=NULL;
 navigation_free(navigation); navigation=NULL;
}
void geany_load_module(GeanyPlugin *plugin) {
 plugin->info->name="Insight Lua"; plugin->info->description="Shared embedded Lua editor runtime";
 plugin->info->version="0.7.0"; plugin->info->author="Insight contributors";
 plugin->funcs->callbacks=callbacks;
 plugin->funcs->init=init; plugin->funcs->cleanup=cleanup;
 GEANY_PLUGIN_REGISTER(plugin,225);
}
