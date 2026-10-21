/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-html-export.h"
#include "app.h"
#include "document.h"
#include "main.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "insight-i18n.h"
#include "insight-help.h"
#include "../insight/desktop/html_export.h"
#include "../insight/desktop/text_transfer.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define STATE "insight-html-export-state"
typedef struct { GtkWidget *dialog,*numbers,*status,*markup;ScintillaObject *preview;InsightHtmlSnapshot *snapshot;gchar *html,*source;GCancellable *cancel;guint64 serial;gboolean busy,closed; } View;
typedef struct { View *view;guint64 serial;gboolean numbers;gchar *html; } Job;
static gboolean initialized,command_busy;
static guint export_serial;
static void job_free(gpointer p) { Job *j=p;g_free(j->html);g_free(j); }
static void view_free(gpointer p) { View *v=p;insight_html_snapshot_free(v->snapshot);g_free(v->html);g_free(v->source);g_clear_object(&v->cancel);g_free(v); }
static void destroy(GtkWidget *widget,gpointer p) { (void)widget;View *v=p;v->closed=TRUE;if(v->cancel) g_cancellable_cancel(v->cancel); }
static void controls(View *v) {
 const gint outputs[]={GTK_RESPONSE_ACCEPT,202,203};for(guint i=0;i<G_N_ELEMENTS(outputs);i++) gtk_widget_set_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(v->dialog),outputs[i]),!v->busy && v->html);
 gtk_widget_set_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(v->dialog),201),!v->busy);gtk_widget_set_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(v->dialog),204),v->busy);gtk_widget_set_sensitive(v->numbers,!v->busy);g_object_set_data(G_OBJECT(v->dialog),"insight-html-busy",GINT_TO_POINTER(v->busy));
}
static void dirty(GtkWidget *widget,gpointer p) { (void)widget;View *v=p;v->serial++;if(v->cancel) g_cancellable_cancel(v->cancel);g_clear_pointer(&v->html,g_free);gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->markup)),"",0);gtk_label_set_text(GTK_LABEL(v->status),insight_tr("HTML options changed. Generate a new preview."));controls(v); }
static void worker(GTask *task,gpointer source,gpointer p,GCancellable *cancel) { (void)source;Job *j=p;GError *e=NULL;j->html=insight_html_export_build(j->view->snapshot,j->numbers,cancel,&e);if(j->html) g_task_return_boolean(task,TRUE);else g_task_return_error(task,e); }
static void completed(GObject *object,GAsyncResult *result,gpointer data) {
 (void)data;View *v=g_object_get_data(object,STATE);Job *j=g_task_get_task_data(G_TASK(result));GError *e=NULL;gboolean ok=g_task_propagate_boolean(G_TASK(result),&e);if(v->closed) { g_clear_error(&e);return; }v->busy=FALSE;g_clear_object(&v->cancel);
 if(v->serial!=j->serial) { g_clear_error(&e);controls(v);return; }
 if(!ok) { gtk_label_set_text(GTK_LABEL(v->status),insight_tr(e->message));g_clear_error(&e);controls(v);return; }
 v->html=j->html;j->html=NULL;gsize n=MIN(strlen(v->html),32768u);while(n && ((guchar)v->html[n]&0xc0)==0x80) n--;gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->markup)),v->html,n);sci_set_line_numbers(v->preview,j->numbers);gtk_label_set_text(GTK_LABEL(v->status),insight_tr("HTML ready. Markup preview is limited to 32 KiB; Copy, Open and Save use the full output."));controls(v);
}
static void generate(View *v) {
 if(v->busy) return;dirty(NULL,v);v->busy=TRUE;v->cancel=g_cancellable_new();controls(v);gtk_label_set_text(GTK_LABEL(v->status),insight_tr("Generating styled HTML…"));Job *j=g_new0(Job,1);j->view=v;j->serial=v->serial;j->numbers=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(v->numbers));GTask *task=g_task_new(v->dialog,v->cancel,completed,NULL);g_task_set_task_data(task,j,job_free);g_task_run_in_thread(task,worker);g_object_unref(task);
}
static InsightHtmlSnapshot *capture(GeanyDocument *doc,gboolean selection,GError **error) {
 ScintillaObject *s=doc->editor->sci;gint length=SEND(s,SCI_GETLENGTH,0,0),begin=0,end=length;
 if(selection) { begin=SEND(s,SCI_GETSELECTIONSTART,0,0);end=SEND(s,SCI_GETSELECTIONEND,0,0);if(begin==end || SEND(s,SCI_GETSELECTIONS,0,0)!=1 || SEND(s,SCI_GETSELECTIONMODE,0,0)!=SC_SEL_STREAM || SEND(s,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) || SEND(s,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0) || (begin>0 && SEND(s,SCI_GETCHARAT,begin,0)=='\n' && SEND(s,SCI_GETCHARAT,begin-1,0)=='\r') || (end<length && SEND(s,SCI_GETCHARAT,end,0)=='\n' && SEND(s,SCI_GETCHARAT,end-1,0)=='\r')) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Select one normal text range without virtual spaces or split CRLF endings.");return NULL; } }
 if(end<begin || end-begin>INSIGHT_HTML_SOURCE_LIMIT) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"HTML source capture is limited to 1 MiB.");return NULL; }
 InsightHtmlSnapshot *out=g_new0(InsightHtmlSnapshot,1);out->length=end-begin;out->first_line=SEND(s,SCI_LINEFROMPOSITION,begin,0)+1;out->tab_width=SEND(s,SCI_GETTABWIDTH,0,0);out->title=doc->file_name ? g_path_get_basename(doc->file_name):g_strdup("untitled");out->text=g_malloc0(out->length+1);out->styles=g_malloc0(out->length+1);
 SEND(s,SCI_COLOURISE,begin,end);gchar *styled=g_malloc0(2*out->length+2);struct Sci_TextRange range={{begin,end},styled};SEND(s,SCI_GETSTYLEDTEXT,0,(sptr_t)&range);for(gsize i=0;i<out->length;i++) { out->text[i]=styled[2*i];out->styles[i]=(guchar)styled[2*i+1]; }g_free(styled);
 for(guint i=0;i<256;i++) { InsightHtmlStyle *p=&out->palette[i];p->fore=SEND(s,SCI_STYLEGETFORE,i,0);p->back=SEND(s,SCI_STYLEGETBACK,i,0);p->size=SEND(s,SCI_STYLEGETSIZE,i,0);p->bold=SEND(s,SCI_STYLEGETBOLD,i,0);p->italic=SEND(s,SCI_STYLEGETITALIC,i,0);p->underline=SEND(s,SCI_STYLEGETUNDERLINE,i,0);gint n=SEND(s,SCI_STYLEGETFONT,i,0);if(n>=0 && n<=256) SEND(s,SCI_STYLEGETFONT,i,(sptr_t)p->font);else g_strlcpy(p->font,"monospace",sizeof p->font); }
 return out;
}
static void preview(View *v) {
 ScintillaObject *s=v->preview;InsightHtmlSnapshot *x=v->snapshot;SEND(s,SCI_SETCODEPAGE,SC_CP_UTF8,0);SEND(s,SCI_SETILEXER,0,0);SEND(s,SCI_SETTEXT,0,(sptr_t)x->text);SEND(s,SCI_SETTABWIDTH,x->tab_width,0);
 for(guint i=0;i<256;i++) { InsightHtmlStyle *p=&x->palette[i];SEND(s,SCI_STYLESETFORE,i,p->fore);SEND(s,SCI_STYLESETBACK,i,p->back);SEND(s,SCI_STYLESETSIZE,i,p->size);SEND(s,SCI_STYLESETBOLD,i,p->bold);SEND(s,SCI_STYLESETITALIC,i,p->italic);SEND(s,SCI_STYLESETUNDERLINE,i,p->underline);SEND(s,SCI_STYLESETFONT,i,(sptr_t)p->font); }
 SEND(s,SCI_STARTSTYLING,0,0);for(gsize i=0;i<x->length;) { gsize end=i+1;while(end<x->length && x->styles[end]==x->styles[i]) end++;SEND(s,SCI_SETSTYLING,end-i,x->styles[i]);i=end; }sci_set_symbol_margin(s,FALSE);SEND(s,SCI_SETMARGINWIDTHN,2,0);sci_set_line_numbers(s,TRUE);SEND(s,SCI_EMPTYUNDOBUFFER,0,0);SEND(s,SCI_SETREADONLY,TRUE,0);
}
static void save(View *v) {
 GtkWidget *d=gtk_file_chooser_dialog_new(insight_tr("Save HTML Export"),GTK_WINDOW(v->dialog),GTK_FILE_CHOOSER_ACTION_SAVE,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Save"),GTK_RESPONSE_ACCEPT,NULL);g_object_ref_sink(d);gtk_widget_set_name(d,"insight-html-save-dialog");gtk_window_set_destroy_with_parent(GTK_WINDOW(d),TRUE);gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(d),TRUE);gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d),TRUE);gchar *name=g_strconcat(v->snapshot->title,".html",NULL);gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d),name);g_free(name);
 if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT) { GPtrArray *paths=g_ptr_array_new_with_free_func(g_free);GError *e=NULL;if(v->source) g_ptr_array_add(paths,g_strdup(v->source));
  for(guint i=0;documents_array && i<documents_array->len;i++) { GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(doc->is_valid && doc->file_name && g_path_is_absolute(doc->file_name)) { gchar *native=g_filename_from_utf8(doc->file_name,-1,NULL,NULL,&e);if(!native) break;g_ptr_array_add(paths,native); } }
  if(!e && app->project && app->project->file_name) { gchar *native=g_filename_from_utf8(app->project->file_name,-1,NULL,NULL,&e);if(native) g_ptr_array_add(paths,native); }
  gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));gboolean ok=!e && path && insight_text_file_write_protected(path,v->html,strlen(v->html),paths,&e);gtk_label_set_text(GTK_LABEL(v->status),insight_tr(ok ? "HTML export saved. Source documents are unchanged.":e ? e->message:"Cannot save HTML export."));g_free(path);g_clear_error(&e);g_ptr_array_unref(paths);
 }
 gtk_widget_destroy(d);g_object_unref(d);
}
void insight_html_export_command(guint selection) {
 if(command_busy) return;GeanyDocument *doc=document_get_current();if(!doc) return;GError *e=NULL;InsightHtmlSnapshot *snapshot=capture(doc,selection!=0,&e);if(!snapshot) { ui_set_statusbar(TRUE,"%s",insight_tr(e->message));g_clear_error(&e);return; }command_busy=TRUE;View *v=g_new0(View,1);v->snapshot=snapshot;if(doc->file_name && g_path_is_absolute(doc->file_name)) v->source=g_filename_from_utf8(doc->file_name,-1,NULL,NULL,NULL);
 v->dialog=gtk_dialog_new_with_buttons(insight_tr(selection ? "Export Selection as HTML":"Export File as HTML"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Close"),GTK_RESPONSE_CLOSE,insight_tr("Generate"),201,insight_tr("Cancel generation"),204,insight_tr("Copy HTML"),202,insight_tr("Open HTML Source"),203,insight_tr("Save…"),GTK_RESPONSE_ACCEPT,NULL);g_object_ref_sink(v->dialog);gtk_widget_set_name(v->dialog,"insight-html-export-dialog");gtk_window_set_default_size(GTK_WINDOW(v->dialog),980,650);g_object_set_data_full(G_OBJECT(v->dialog),STATE,v,view_free);g_signal_connect(v->dialog,"destroy",G_CALLBACK(destroy),v);GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(v->dialog));insight_panel_help_attach(box,"html-export.md");
 GtkWidget *label=gtk_label_new(snapshot->title);g_object_set_data(G_OBJECT(label),"insight-no-i18n",GINT_TO_POINTER(1));gtk_box_pack_start(GTK_BOX(box),label,FALSE,FALSE,4);v->numbers=gtk_check_button_new_with_label(insight_tr("Line Numbers"));gtk_widget_set_name(v->numbers,"insight-html-lines");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(v->numbers),TRUE);gtk_box_pack_start(GTK_BOX(box),v->numbers,FALSE,FALSE,4);
 GtkWidget *tabs=gtk_notebook_new();gtk_box_pack_start(GTK_BOX(box),tabs,TRUE,TRUE,0);v->preview=SCINTILLA(scintilla_new());gtk_widget_set_name(GTK_WIDGET(v->preview),"insight-html-source-preview");preview(v);gtk_notebook_append_page(GTK_NOTEBOOK(tabs),GTK_WIDGET(v->preview),gtk_label_new(insight_tr("Captured source")));v->markup=gtk_text_view_new();gtk_widget_set_name(v->markup,"insight-html-markup-preview");gtk_text_view_set_editable(GTK_TEXT_VIEW(v->markup),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(v->markup),TRUE);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),v->markup);gtk_notebook_append_page(GTK_NOTEBOOK(tabs),scroll,gtk_label_new(insight_tr("HTML source")));
 v->status=gtk_label_new("");gtk_widget_set_name(v->status,"insight-html-status");gtk_label_set_line_wrap(GTK_LABEL(v->status),TRUE);gtk_box_pack_start(GTK_BOX(box),v->status,FALSE,FALSE,6);g_signal_connect(v->numbers,"toggled",G_CALLBACK(dirty),v);gtk_widget_show_all(v->dialog);generate(v);
 for(;;) { gint response=gtk_dialog_run(GTK_DIALOG(v->dialog));if(response==201) { generate(v);continue; }if(response==204) { if(v->cancel) g_cancellable_cancel(v->cancel);continue; }if((response==202 || response==203 || response==GTK_RESPONSE_ACCEPT) && (!v->html || v->busy)) continue;if(response==202) { gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),v->html,-1);continue; }if(response==GTK_RESPONSE_ACCEPT) { save(v);continue; }if(response==203) { gchar *name=g_strdup_printf("HTML export (%u).html",++export_serial);document_new_file(name,NULL,v->html);g_free(name);break; }break; }
 GtkWidget *d=v->dialog;gtk_widget_destroy(d);g_object_unref(d);command_busy=FALSE;
}
static void activate(GtkMenuItem *item,gpointer data) { (void)item;insight_html_export_command(GPOINTER_TO_UINT(data)); }
void insight_html_export_init(void) {
 if(initialized) return;initialized=TRUE;GtkWidget *menu=ui_lookup_widget(main_widgets.window,"file1_menu");if(!menu) menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"file1")));
 const gchar *labels[]={"Export File as HTML…","Export Selection as HTML…"},*names[]={"insight-export-html","insight-export-selection-html"};const guint keys[]={GEANY_KEYS_INSIGHT_EXPORTHTML,GEANY_KEYS_INSIGHT_EXPORTSELECTIONHTML};for(guint i=0;i<2;i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(item,names[i]);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(activate),GUINT_TO_POINTER(i));keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,keys[i],item);gtk_widget_show(item); }
}
