/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-symbol-edit.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "highlighting.h"
#include "filetypes.h"
#include "ui_utils.h"
#include "insight-completion.h"
#include "insight-i18n.h"
#include "insight-help.h"
#include "../insight/desktop/symbol_edit.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
typedef struct {
 guint id,action;ScintillaObject *sci;GeanyFiletype *filetype;gchar *text,*name,*eol;gsize size,first,last;sptr_t anchor,caret;guint64 revision;gulong watch;gboolean closed;InsightSymbolPlan plan;
} Edit;
static void modified(ScintillaObject *s,gint unused,SCNotification *nt,gpointer data) {
 (void)s;(void)unused;Edit *v=data;if(nt->nmhdr.code==SCN_MODIFIED&&(nt->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) v->revision++;
}
static void release(Edit *v) {
 if(v->watch&&g_signal_handler_is_connected(v->sci,v->watch)) g_signal_handler_disconnect(v->sci,v->watch);
 g_object_unref(v->sci);g_free(v->text);g_free(v->name);g_free(v->eol);insight_symbol_plan_clear(&v->plan);g_free(v);
}
static gboolean valid(Edit *v) {
 GeanyDocument *d=document_find_by_id(v->id);if(v->closed||!d||d!=document_get_current()||d->editor->sci!=v->sci||v->revision||g_strcmp0(d->file_name,v->name)||d->file_type!=v->filetype) return FALSE;
 ScintillaObject *s=v->sci;if(v->action&&(d->readonly||SEND(s,SCI_GETREADONLY,0,0))) return FALSE;
 if(SEND(s,SCI_GETSELECTIONS,0,0)!=1||SEND(s,SCI_GETSELECTIONMODE,0,0)!=SC_SEL_STREAM||SEND(s,SCI_GETANCHOR,0,0)!=v->anchor||SEND(s,SCI_GETCURRENTPOS,0,0)!=v->caret||SEND(s,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0)||SEND(s,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0)) return FALSE;
 gchar *text=sci_get_contents(s,-1);gboolean same=SEND(s,SCI_GETLENGTH,0,0)==v->size&&!memcmp(text,v->text,v->size);g_free(text);return same;
}
static gboolean apply(Edit *v) {
 if(!valid(v)) { ui_set_statusbar(TRUE,"%s",insight_tr("Symbol edit cancelled because the source, selection or permissions changed."));return FALSE; }
 GeanyDocument *d=document_find_by_id(v->id);insight_completion_cancel(d->editor);SEND(v->sci,SCI_AUTOCCANCEL,0,0);
 if(!valid(v)) return FALSE;
 ScintillaObject *s=v->sci;guint64 expected=(v->plan.end>v->plan.begin)+(strlen(v->plan.payload)>0);
 sci_start_undo_action(s);SEND(s,SCI_SETTARGETRANGE,v->plan.begin,v->plan.end);SEND(s,SCI_REPLACETARGET,strlen(v->plan.payload),(sptr_t)v->plan.payload);sci_end_undo_action(s);
 d=document_find_by_id(v->id);gchar *actual=d&&d->editor->sci==s?sci_get_contents(s,-1):NULL;
 gboolean ok=!v->closed&&actual&&v->revision==expected&&SEND(s,SCI_GETLENGTH,0,0)==strlen(v->plan.text)&&!strcmp(actual,v->plan.text);g_free(actual);
 if(ok) { SEND(s,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);SEND(s,SCI_CLEARSELECTIONS,0,0);SEND(s,SCI_SETSEL,v->plan.selection_begin,v->plan.selection_end);SEND(s,SCI_SCROLLCARET,0,0);gtk_widget_grab_focus(GTK_WIDGET(s)); }
 else ui_set_statusbar(TRUE,"%s",insight_tr("Symbol edit was interrupted by an editor change. Inspect the source and use Undo if needed."));return ok;
}
static ScintillaObject *preview(GtkWidget *parent,Edit *v,const gchar *text,const gchar *name) {
 ScintillaObject *s=SCINTILLA(scintilla_new());gtk_widget_set_name(GTK_WIDGET(s),name);highlighting_set_styles(s,v->filetype?v->filetype:filetypes[GEANY_FILETYPES_NONE]);sci_set_text(s,text);SEND(s,SCI_SETUNDOCOLLECTION,FALSE,0);SEND(s,SCI_EMPTYUNDOBUFFER,0,0);SEND(s,SCI_SETREADONLY,TRUE,0);sci_set_line_numbers(s,TRUE);SEND(s,SCI_SETMARGINWIDTHN,0,0);SEND(s,SCI_SETMARGINWIDTHN,1,50);SEND(s,SCI_SETMARGINWIDTHN,2,0);SEND(s,SCI_COLOURISE,0,-1);gtk_container_add(GTK_CONTAINER(parent),GTK_WIDGET(s));return s;
}
static void destroyed(GtkWidget *w,gpointer data) { (void)w;((Edit *)data)->closed=TRUE; }
static void clipboard_ready(GtkClipboard *clipboard,const gchar *text,gpointer data) {
 (void)clipboard;Edit *v=data;GError *error=NULL;
 if(!valid(v)) { ui_set_statusbar(TRUE,"%s",insight_tr("Symbol edit cancelled because the source, selection or permissions changed."));release(v);return; }
 if(!insight_symbol_plan(v->text,v->size,v->first,v->last,INSIGHT_SYMBOL_PASTE,text,v->eol,&v->plan,&error)) { ui_set_statusbar(TRUE,"%s",error?insight_tr(error->message):insight_tr("Clipboard text is unavailable."));g_clear_error(&error);release(v);return; }
 GtkWidget *dialog=gtk_dialog_new_with_buttons(insight_tr("Paste Symbol"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Insert before symbol"),GTK_RESPONSE_APPLY,NULL);g_object_ref_sink(dialog);gtk_widget_set_name(dialog,"insight-paste-symbol-dialog");gtk_window_set_default_size(GTK_WINDOW(dialog),1060,650);g_signal_connect(dialog,"destroy",G_CALLBACK(destroyed),v);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)),*note=gtk_label_new(insight_tr("Paste clipboard text before the captured definition. A missing final line ending uses the document mode. Apply is one Undo action; no disk writes."));gtk_label_set_line_wrap(GTK_LABEL(note),TRUE);gtk_box_pack_start(GTK_BOX(box),note,FALSE,FALSE,6);
 GtkWidget *paned=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL),*before=gtk_frame_new(insight_tr("Before")),*after=gtk_frame_new(insight_tr("After"));gtk_paned_pack1(GTK_PANED(paned),before,TRUE,FALSE);gtk_paned_pack2(GTK_PANED(paned),after,TRUE,FALSE);gtk_paned_set_position(GTK_PANED(paned),520);gtk_box_pack_start(GTK_BOX(box),paned,TRUE,TRUE,4);
 preview(before,v,v->text,"insight-paste-symbol-before");ScintillaObject *result=preview(after,v,v->plan.text,"insight-paste-symbol-after");SEND(result,SCI_SETSEL,v->plan.selection_begin,v->plan.selection_end);SEND(result,SCI_SCROLLCARET,0,0);insight_panel_help_attach(box,"structural-selection.md");gtk_widget_show_all(dialog);
 gint response=gtk_dialog_run(GTK_DIALOG(dialog));if(!v->closed&&response==GTK_RESPONSE_APPLY) apply(v);gtk_widget_destroy(dialog);g_object_unref(dialog);release(v);
}
void insight_symbol_edit_range(guint id,gint first,gint last,guint action) {
 GeanyDocument *d=document_find_by_id(id);if(!d||d!=document_get_current()||action>INSIGHT_SYMBOL_PASTE) return;ScintillaObject *s=d->editor->sci;gint size=sci_get_length(s);
 if(first<0||last<=first||last>size||size>INSIGHT_SYMBOL_SOURCE_LIMIT||last-first>INSIGHT_SYMBOL_TEXT_LIMIT) return;
 Edit *v=g_new0(Edit,1);v->id=id;v->action=action;v->sci=g_object_ref(s);v->filetype=d->file_type;v->text=sci_get_contents(s,-1);v->size=size;v->first=first;v->last=last;v->name=g_strdup(d->file_name);v->eol=g_strdup(sci_get_eol_mode(s)==SC_EOL_CRLF?"\r\n":sci_get_eol_mode(s)==SC_EOL_CR?"\r":"\n");v->anchor=SEND(s,SCI_GETANCHOR,0,0);v->caret=SEND(s,SCI_GETCURRENTPOS,0,0);
 if(memchr(v->text,0,size)||!g_utf8_validate(v->text,size,NULL)||!valid(v)) { release(v);return; }
 v->watch=g_signal_connect(s,"sci-notify",G_CALLBACK(modified),v);
 if(action==INSIGHT_SYMBOL_PASTE) { gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),clipboard_ready,v);return; }
 if(action!=INSIGHT_SYMBOL_COPY) { GError *error=NULL;if(!insight_symbol_plan(v->text,v->size,first,last,action,NULL,v->eol,&v->plan,&error)) { ui_set_statusbar(TRUE,"%s",error?insight_tr(error->message):"Invalid symbol range");g_clear_error(&error);release(v);return; } }
 if(action<INSIGHT_SYMBOL_DUPLICATE) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),v->text+first,last-first);
 if(action) apply(v);release(v);
}
