/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "insight-clang.h"
#include "insight-i18n.h"
#include "insight-workbench.h"
#include "insight-help.h"
#include "document.h"
#include "geanyobject.h"
#include "sciwrappers.h"
#include "project.h"
#include "utils.h"
#include "ui_utils.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define SEMANTIC_INDICATOR 25
static gboolean alive,enabled;static GtkWidget *clang_menu;
static guint timer,paint_source;static guint64 serial;
static GCancellable *pending;
static gulong handlers[2];
typedef struct {gchar *path,*text,*root,*resources,*digest,*overlay_digest,*project;GHashTable *buffers;guint document;guint64 serial;InsightClangResult *result;InsightIndex *previous;gboolean force;} Job;
static void job_free(gpointer data) {Job *j=data;g_free(j->path);g_free(j->text);g_free(j->root);g_free(j->resources);g_free(j->digest);g_free(j->overlay_digest);g_free(j->project);g_hash_table_unref(j->buffers);insight_index_free(j->previous);insight_clang_result_free(j->result);g_free(j);}
InsightClangResult *insight_clang_ui_result(ScintillaObject *sci) {return sci ? g_object_get_data(G_OBJECT(sci),"insight-clang-result"):NULL;}
static void clear_paint(ScintillaObject *sci) {SEND(sci,SCI_SETINDICATORCURRENT,SEMANTIC_INDICATOR,0);SEND(sci,SCI_INDICATORCLEARRANGE,0,sci_get_length(sci));}
static void clear(ScintillaObject *sci) {clear_paint(sci);g_object_set_data(G_OBJECT(sci),"insight-clang-result",NULL);}
static void modified(ScintillaObject *sci,gint unused,SCNotification *n,gpointer data) {
 (void)unused;(void)data;if(!alive || n->nmhdr.code!=SCN_MODIFIED || !(n->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) return;
 serial++;if(pending) {g_cancellable_cancel(pending);g_clear_object(&pending);}GeanyDocument *changed=document_find_by_id(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(sci),"insight-clang-document")));
 for(guint i=0;changed && changed->file_name && i<documents_array->len;i++) {GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(!DOC_VALID(doc) || doc==changed) continue;InsightClangResult *r=insight_clang_ui_result(doc->editor->sci);if(r && insight_index_query(r->relations,changed->file_name,TRUE,NULL,"@clanginclude",doc->file_name,0,NULL,0)) clear(doc->editor->sci);}
 clear(sci);
}
static void watch(GeanyDocument *doc) {g_object_set_data(G_OBJECT(doc->editor->sci),"insight-clang-document",GUINT_TO_POINTER(doc->id));if(!g_object_get_data(G_OBJECT(doc->editor->sci),"insight-clang-watched")) {g_object_set_data(G_OBJECT(doc->editor->sci),"insight-clang-watched",GINT_TO_POINTER(1));g_signal_connect(doc->editor->sci,"sci-notify",G_CALLBACK(modified),NULL);}}
static void worker(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {(void)source;Job *j=data;GError *error=NULL;if(!j->force && j->previous && insight_clang_dependencies_fresh(j->previous,j->path)) {g_task_return_boolean(task,FALSE);return;}j->result=insight_clang_parse(j->path,j->text,j->root,j->resources,j->buffers,cancel,&error);if(j->result) g_task_return_boolean(task,TRUE);else if(error) g_task_return_error(task,error);else g_task_return_boolean(task,FALSE);}
static gboolean paint(gpointer data) {
 ScintillaObject *sci=data;InsightClangResult *r=insight_clang_ui_result(sci);GeanyDocument *doc=document_find_by_id(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(sci),"insight-clang-document")));if(!alive || !enabled || !r || !DOC_VALID(doc) || doc->editor->sci!=sci) {paint_source=0;return G_SOURCE_REMOVE;}
 guint position=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(sci),"insight-clang-paint-position"));gint64 deadline=g_get_monotonic_time()+3000;glong background=SEND(sci,SCI_STYLEGETBACK,STYLE_DEFAULT,0);gboolean dark=((background&255)+((background>>8)&255)+((background>>16)&255))<384;
 if(!position) g_object_set_data(G_OBJECT(sci),"insight-clang-painted-bg",GUINT_TO_POINTER((guint)background+1));
 const guint light[]={0x8b3c66,0x9b5b18,0x825210,0x405d9c,0x624332,0x925c77,0x467c35},night[]={0xd9a0d5,0xe8cf8c,0xe0bf85,0x8fc9d5,0xe5d6c4,0xd5a7bd,0x8fcaa0};
 SEND(sci,SCI_SETINDICATORCURRENT,SEMANTIC_INDICATOR,0);SEND(sci,SCI_INDICSETSTYLE,SEMANTIC_INDICATOR,INDIC_TEXTFORE);SEND(sci,SCI_INDICSETFLAGS,SEMANTIC_INDICATOR,SC_INDICFLAG_VALUEFORE);
 while(position<r->tokens->len) {InsightClangToken token=g_array_index(r->tokens,InsightClangToken,position++);if(token.role<=INSIGHT_CLANG_MACRO) {SEND(sci,SCI_SETINDICATORVALUE,SC_INDICVALUEBIT|(dark ? night[token.role]:light[token.role]),0);SEND(sci,SCI_INDICATORFILLRANGE,token.begin,token.end-token.begin);}if(g_get_monotonic_time()>=deadline) {g_object_set_data(G_OBJECT(sci),"insight-clang-paint-position",GUINT_TO_POINTER(position));return G_SOURCE_CONTINUE;}}
 paint_source=0;return G_SOURCE_REMOVE;
}
static gchar *command_origin(const InsightClangResult *r) {
 if(r->ambiguous_command) return g_strdup(insight_tr("Ambiguous C/C++ header settings; default arguments"));
 if(r->inferred_command) return insight_i18n_printf("Settings inferred from %s",r->command_source);
 return g_strdup(insight_tr(r->compilation_database ? "Exact compilation database command":"Default compiler arguments"));
}
static void details(GtkWidget *widget,gpointer unused) {
 (void)widget;(void)unused;GeanyDocument *doc=document_get_current();InsightClangResult *r=DOC_VALID(doc) ? insight_clang_ui_result(doc->editor->sci):NULL;
 if(!r) {ui_set_statusbar(TRUE,"%s",insight_tr("Clang analysis is not ready; reanalyze the current source"));return;}
 gchar *origin=command_origin(r),*report=g_strdup_printf("%s: %s\n%s: %s\n%s: %s\n%s: %s\n%s: %zu\n%s: %u\n%s: %s\n%s: %s\n\n%s\n%s",insight_tr("Source"),doc->file_name ? doc->file_name:"",insight_tr("Compiler settings"),origin,insight_tr("Settings source"),r->command_source ? r->command_source:"—",insight_tr("Parse language"),!g_strcmp0(r->parse_language,"configured") ? insight_tr("As configured"):r->parse_language ? r->parse_language:"—",insight_tr("Symbols"),insight_index_count(r->symbols),insight_tr("Semantic tokens"),r->tokens->len,insight_tr("Source diagnostics present"),insight_tr(r->errors ? "Yes":"No"),insight_tr("Analysis limit reached"),insight_tr(r->limited ? "Yes":"No"),insight_tr("Compiler diagnostics"),r->diagnostics && *r->diagnostics ? r->diagnostics:insight_tr("No compiler diagnostics"));g_free(origin);
 GtkWidget *parent=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci)),*dialog=gtk_dialog_new_with_buttons(insight_tr("Clang analysis details"),GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent):NULL,GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Close"),GTK_RESPONSE_CLOSE,NULL),*scroll=gtk_scrolled_window_new(NULL,NULL),*text=gtk_text_view_new();
 gtk_widget_set_name(dialog,"insight-clang-details");gtk_widget_set_name(text,"insight-clang-details-text");gtk_text_view_set_editable(GTK_TEXT_VIEW(text),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(text),TRUE);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text),GTK_WRAP_WORD_CHAR);gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text)),report,-1);g_free(report);gtk_container_add(GTK_CONTAINER(scroll),text);gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),scroll,TRUE,TRUE,4);
 GdkMonitor *monitor=gdk_display_get_monitor_at_window(gtk_widget_get_display(parent),gtk_widget_get_window(parent));GdkRectangle area={0,0,1000,700};if(monitor) gdk_monitor_get_workarea(monitor,&area);gtk_window_set_default_size(GTK_WINDOW(dialog),MIN(760,MAX(1,area.width*85/100)),MIN(460,MAX(1,area.height*85/100)));g_signal_connect_swapped(dialog,"response",G_CALLBACK(gtk_widget_destroy),dialog);gtk_widget_show_all(dialog);
}
static void done(GObject *source,GAsyncResult *result,gpointer unused) {
 (void)source;(void)unused;Job *j=g_task_get_task_data(G_TASK(result));GError *error=NULL;gboolean ok=g_task_propagate_boolean(G_TASK(result),&error);
 if(!alive || j->serial!=serial) {g_clear_error(&error);return;}g_clear_object(&pending);GeanyDocument *doc=document_find_by_id(j->document);
 if(ok && DOC_VALID(doc) && !g_strcmp0(doc->file_name,j->path) && !g_strcmp0(j->project,app->project ? app->project->file_name:NULL) && sci_get_length(doc->editor->sci)>=0 && sci_get_length(doc->editor->sci)<=2*1024*1024) {gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1),*digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);g_free(text);if(!strcmp(digest,j->digest)) {if(paint_source) {g_source_remove(paint_source);paint_source=0;}clear(doc->editor->sci);g_object_set_data_full(G_OBJECT(doc->editor->sci),"insight-clang-result",j->result,(GDestroyNotify)insight_clang_result_free);j->result=NULL;document_update_semantic_tags(doc);insight_workbench_semantic_changed(doc->id);g_object_set_data_full(G_OBJECT(doc->editor->sci),"insight-clang-overlay-digest",g_strdup(j->overlay_digest),g_free);g_object_set_data(G_OBJECT(doc->editor->sci),"insight-clang-paint-position",NULL);if(enabled) paint_source=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,paint,g_object_ref(doc->editor->sci),g_object_unref);if(j->force) {InsightClangResult *r=insight_clang_ui_result(doc->editor->sci);gchar *origin=command_origin(r),*suffix=g_strconcat("; ",origin,NULL);ui_set_statusbar(TRUE,insight_tr("Clang: %zu symbols, %u semantic tokens%s%s"),insight_index_count(r->symbols),r->tokens->len,r->errors ? insight_tr("; source diagnostics present"):"",suffix);g_free(origin);g_free(suffix);}}g_free(digest);}
 else if(j->force && error) ui_set_statusbar(TRUE,"Clang: %s",error->message);g_clear_error(&error);
}
static gboolean analyze(gpointer force_data) {
 gboolean force=GPOINTER_TO_INT(force_data);GeanyDocument *doc=document_get_current();if(!alive || !insight_clang_available() || !DOC_VALID(doc) || !doc->file_name || !insight_clang_source(doc->file_name)) return G_SOURCE_CONTINUE;
 watch(doc);gint length=sci_get_length(doc->editor->sci);if(length<0 || length>2*1024*1024) return G_SOURCE_CONTINUE;gchar *text=sci_get_contents(doc->editor->sci,length+1);if(memchr(text,0,length) || !g_utf8_validate(text,length,NULL)) {g_free(text);return G_SOURCE_CONTINUE;}InsightClangResult *previous=insight_clang_ui_result(doc->editor->sci);gchar *digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);
 if(previous && enabled && GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(doc->editor->sci),"insight-clang-painted-bg"))!=(guint)SEND(doc->editor->sci,SCI_STYLEGETBACK,STYLE_DEFAULT,0)+1) {if(paint_source) g_source_remove(paint_source);clear_paint(doc->editor->sci);g_object_set_data(G_OBJECT(doc->editor->sci),"insight-clang-paint-position",NULL);paint_source=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,paint,g_object_ref(doc->editor->sci),g_object_unref);}
 if(!force && pending) {g_free(text);g_free(digest);return G_SOURCE_CONTINUE;}
 if(pending) {g_cancellable_cancel(pending);g_clear_object(&pending);}Job *j=g_new0(Job,1);j->project=g_strdup(app->project ? app->project->file_name:NULL);if(previous && !strcmp(previous->digest,digest)) j->previous=insight_index_ref(previous->relations);j->serial=++serial;j->document=doc->id;j->force=force;j->path=g_strdup(doc->file_name);j->text=text;j->digest=digest;j->resources=g_build_filename(app->datadir,"insight","clang","resources",NULL);gchar *native=app->project ? project_get_base_path():g_path_get_dirname(doc->file_name);j->root=app->project ? utils_get_utf8_from_locale(native):g_strdup(native);g_free(native);
 j->buffers=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);gsize total=0;for(guint i=0;i<documents_array->len;i++) {GeanyDocument *d=g_ptr_array_index(documents_array,i);if(!DOC_VALID(d) || !d->file_name) continue;gint n=sci_get_length(d->editor->sci);if(n<0 || n>2*1024*1024 || total+(gsize)n>32u*1024u*1024u) continue;gchar *contents=sci_get_contents(d->editor->sci,n+1);if(!memchr(contents,0,n) && g_utf8_validate(contents,n,NULL)) {g_hash_table_insert(j->buffers,g_strdup(d->file_name),contents);total+=n;}else g_free(contents);}
 GChecksum *overlay=g_checksum_new(G_CHECKSUM_SHA256);g_checksum_update(overlay,(const guchar *)j->root,strlen(j->root)+1);GList *keys=g_hash_table_get_keys(j->buffers);keys=g_list_sort(keys,(GCompareFunc)g_strcmp0);for(GList *p=keys;p;p=p->next) {const gchar *contents=g_hash_table_lookup(j->buffers,p->data);g_checksum_update(overlay,p->data,strlen(p->data)+1);g_checksum_update(overlay,(const guchar *)contents,strlen(contents)+1);}g_list_free(keys);j->overlay_digest=g_strdup(g_checksum_get_string(overlay));g_checksum_free(overlay);
 if(g_strcmp0(j->overlay_digest,g_object_get_data(G_OBJECT(doc->editor->sci),"insight-clang-overlay-digest"))) g_clear_pointer(&j->previous,insight_index_free);
 pending=g_cancellable_new();GTask *task=g_task_new(NULL,pending,done,NULL);g_task_set_task_data(task,j,job_free);g_task_run_in_thread(task,worker);g_object_unref(task);return G_SOURCE_CONTINUE;
}
static void document_event(GObject *object,GeanyDocument *doc,gpointer data) {(void)object;(void)data;if(DOC_VALID(doc)) watch(doc);analyze(NULL);}
static void reanalyze(GtkWidget *widget,gpointer data) {(void)widget;(void)data;analyze(GINT_TO_POINTER(1));}
static void toggle(GtkCheckMenuItem *item,gpointer unused) {
 (void)unused;enabled=gtk_check_menu_item_get_active(item);gchar *path=g_build_filename(app->configdir,"insight-clang.conf",NULL);GKeyFile *key=g_key_file_new();g_key_file_load_from_file(key,path,G_KEY_FILE_NONE,NULL);g_key_file_set_boolean(key,"clang","semantic-colors",enabled);gsize length=0;gchar *text=g_key_file_to_data(key,&length,NULL);g_file_set_contents(path,text,length,NULL);g_free(text);g_key_file_unref(key);g_free(path);if(paint_source) {g_source_remove(paint_source);paint_source=0;}
 for(guint i=0;i<documents_array->len;i++) {GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(DOC_VALID(doc)) clear_paint(doc->editor->sci);}
 if(enabled) analyze(GINT_TO_POINTER(1));
}
GtkWidget *insight_clang_ui_init(GtkWidget *parent) {
 if(alive) return clang_menu;alive=TRUE;GtkWidget *group=gtk_menu_item_new_with_label(insight_tr("Clang C/C++"));insight_i18n_key(G_OBJECT(group),"label","Clang C/C++");clang_menu=gtk_menu_new();gtk_menu_item_set_submenu(GTK_MENU_ITEM(group),clang_menu);gtk_menu_shell_append(GTK_MENU_SHELL(parent),group);gtk_widget_show(group);GtkWidget *menu=clang_menu;enabled=insight_clang_available();gchar *path=g_build_filename(app->configdir,"insight-clang.conf",NULL);GKeyFile *key=g_key_file_new();if(g_key_file_load_from_file(key,path,G_KEY_FILE_NONE,NULL) && g_key_file_has_key(key,"clang","semantic-colors",NULL)) enabled=insight_clang_available() && g_key_file_get_boolean(key,"clang","semantic-colors",NULL);g_key_file_unref(key);g_free(path);GtkWidget *item=gtk_check_menu_item_new_with_label(insight_tr("Clang C/C++ semantic colors"));insight_i18n_key(G_OBJECT(item),"label","Clang C/C++ semantic colors");gtk_widget_set_name(item,"insight-clang-colors");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),enabled);gtk_widget_set_sensitive(item,insight_clang_available());g_signal_connect(item,"toggled",G_CALLBACK(toggle),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item);
 item=gtk_menu_item_new_with_label(insight_tr("Reanalyze C/C++ buffer with Clang"));insight_i18n_key(G_OBJECT(item),"label","Reanalyze C/C++ buffer with Clang");gtk_widget_set_name(item,"insight-clang-reanalyze");gtk_widget_set_sensitive(item,insight_clang_available());g_signal_connect(item,"activate",G_CALLBACK(reanalyze),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item);
 item=gtk_menu_item_new_with_label(insight_tr("Clang analysis details…"));insight_i18n_key(G_OBJECT(item),"label","Clang analysis details…");gtk_widget_set_name(item,"insight-clang-details-command");gtk_widget_set_sensitive(item,insight_clang_available());g_signal_connect(item,"activate",G_CALLBACK(details),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item);
 item=gtk_menu_item_new_with_label(insight_tr("Clang C/C++ help…"));insight_i18n_key(G_OBJECT(item),"label","Clang C/C++ help…");g_signal_connect(item,"activate",G_CALLBACK(insight_help_show),"clang.md");gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item);
 handlers[0]=g_signal_connect_after(geany_object,"document-open",G_CALLBACK(document_event),NULL);handlers[1]=g_signal_connect_after(geany_object,"document-activate",G_CALLBACK(document_event),NULL);timer=g_timeout_add(2000,analyze,NULL);analyze(NULL);return clang_menu;
}
void insight_clang_ui_finalize(void) {alive=FALSE;serial++;if(timer) g_source_remove(timer);timer=0;if(paint_source) g_source_remove(paint_source);paint_source=0;if(pending) {g_cancellable_cancel(pending);g_clear_object(&pending);}for(guint i=0;i<2;i++) if(handlers[i]) g_signal_handler_disconnect(geany_object,handlers[i]);}
