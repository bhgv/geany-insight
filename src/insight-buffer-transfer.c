/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-buffer-transfer.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "filetypes.h"
#include "highlighting.h"
#include "ui_utils.h"
#include "insight-completion.h"
#include "insight-i18n.h"
#include "../insight/desktop/text_transfer.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
typedef struct {
 guint id;ScintillaObject *sci;gchar *text,*name;gsize size;sptr_t anchor,caret;gboolean stream;guint64 revision;gulong watch;
} Snapshot;
typedef struct {
 Snapshot source,target;GtkWidget *dialog,*dest,*scope,*mode,*status;ScintillaObject *before,*after,*source_view;
 gchar *result,*payload;gsize begin,end;gboolean closed,busy;
} Review;
static void modified(ScintillaObject *s,gint unused,SCNotification *nt,gpointer data) {
 (void)s;(void)unused;Snapshot *v=data;if(nt->nmhdr.code==SCN_MODIFIED&&(nt->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) v->revision++;
}
static void clear_snapshot(Snapshot *v) {
 if(v->sci&&v->watch&&g_signal_handler_is_connected(v->sci,v->watch)) g_signal_handler_disconnect(v->sci,v->watch);
 g_clear_object(&v->sci);g_free(v->text);g_free(v->name);memset(v,0,sizeof(*v));
}
static gboolean capture(Snapshot *v,guint id) {
 clear_snapshot(v);GeanyDocument *d=document_find_by_id(id);if(!d) return FALSE;
 ScintillaObject *s=d->editor->sci;sptr_t len=SEND(s,SCI_GETLENGTH,0,0);if(len<0||len>INSIGHT_TEXT_TRANSFER_LIMIT) return FALSE;
 v->text=sci_get_contents(s,-1);if(memchr(v->text,0,len)||!g_utf8_validate(v->text,len,NULL)) { clear_snapshot(v);return FALSE; }
 v->id=id;v->sci=g_object_ref(s);v->size=len;v->name=g_strdup(d->file_name);v->anchor=SEND(s,SCI_GETANCHOR,0,0);v->caret=SEND(s,SCI_GETCURRENTPOS,0,0);
 v->stream=SEND(s,SCI_GETSELECTIONS,0,0)==1&&SEND(s,SCI_GETSELECTIONMODE,0,0)==SC_SEL_STREAM&&!SEND(s,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0)&&!SEND(s,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0);
 v->watch=g_signal_connect(s,"sci-notify",G_CALLBACK(modified),v);return TRUE;
}
static gboolean valid(Snapshot *v,gboolean selection,gboolean writable) {
 GeanyDocument *d=document_find_by_id(v->id);if(!d||d->editor->sci!=v->sci||v->revision||g_strcmp0(d->file_name,v->name)) return FALSE;
 if(writable&&(d->readonly||SEND(v->sci,SCI_GETREADONLY,0,0))) return FALSE;
 if(selection&&(!v->stream||SEND(v->sci,SCI_GETSELECTIONS,0,0)!=1||SEND(v->sci,SCI_GETSELECTIONMODE,0,0)!=SC_SEL_STREAM||SEND(v->sci,SCI_GETANCHOR,0,0)!=v->anchor||SEND(v->sci,SCI_GETCURRENTPOS,0,0)!=v->caret||SEND(v->sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0)||SEND(v->sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0))) return FALSE;
 gchar *text=sci_get_contents(v->sci,-1);gboolean same=SEND(v->sci,SCI_GETLENGTH,0,0)==v->size&&!memcmp(text,v->text,v->size);g_free(text);return same;
}
static void code(ScintillaObject *s,const gchar *text,const gchar *name) {
 GeanyFiletype *ft=filetypes_detect_from_extension(name?name:"");highlighting_set_styles(s,ft?ft:filetypes[GEANY_FILETYPES_NONE]);SEND(s,SCI_SETREADONLY,FALSE,0);SEND(s,SCI_SETUNDOCOLLECTION,FALSE,0);sci_set_text(s,text?text:"");SEND(s,SCI_EMPTYUNDOBUFFER,0,0);SEND(s,SCI_SETREADONLY,TRUE,0);sci_set_line_numbers(s,TRUE);SEND(s,SCI_SETMARGINWIDTHN,0,0);SEND(s,SCI_SETMARGINWIDTHN,1,50);SEND(s,SCI_SETMARGINWIDTHN,2,0);SEND(s,SCI_COLOURISE,0,-1);
}
static void status(Review *v,const gchar *text) { if(!v->closed) gtk_label_set_text(GTK_LABEL(v->status),insight_tr(text)); }
static void invalidate(GtkWidget *w,gpointer data) {
 (void)w;Review *v=data;g_clear_pointer(&v->result,g_free);g_clear_pointer(&v->payload,g_free);if(v->closed) return;
 gtk_dialog_set_response_sensitive(GTK_DIALOG(v->dialog),GTK_RESPONSE_APPLY,FALSE);code(v->after,"",NULL);status(v,"Press Preview. Changes stay in the destination buffer until you save it.");
}
static guint destination(Review *v) { const gchar *id=gtk_combo_box_get_active_id(GTK_COMBO_BOX(v->dest));return id?(guint)g_ascii_strtoull(id,NULL,10):0; }
static void destination_changed(GtkWidget *w,gpointer data) {
 Review *v=data;if(v->busy||v->closed) return;capture(&v->target,destination(v));code(v->before,v->target.text,v->target.name);invalidate(w,data);
}
static void populate(Review *v,guint chosen) {
 v->busy=TRUE;gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(v->dest));
 guint i;foreach_document(i) { GeanyDocument *d=documents[i];gchar *id=g_strdup_printf("%u",d->id),*label=g_strdup_printf("%s [%u]",d->file_name?d->file_name:"Untitled",d->id);gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(v->dest),id,label);g_free(id);g_free(label); }
 gchar *id=g_strdup_printf("%u",chosen);gtk_combo_box_set_active_id(GTK_COMBO_BOX(v->dest),id);g_free(id);v->busy=FALSE;destination_changed(NULL,v);
}
static gboolean unchanged(Review *v) {
 return !v->closed&&valid(&v->source,gtk_combo_box_get_active(GTK_COMBO_BOX(v->scope))==0,FALSE)&&valid(&v->target,gtk_combo_box_get_active(GTK_COMBO_BOX(v->mode))>=2,TRUE);
}
static void preview(Review *v) {
 invalidate(NULL,v);if(!unchanged(v)) { status(v,"Source, destination or selection changed, or destination is read-only. Reload snapshots.");return; }
 gint scope=gtk_combo_box_get_active(GTK_COMBO_BOX(v->scope)),mode=gtk_combo_box_get_active(GTK_COMBO_BOX(v->mode));
 gsize a=scope==0?MIN(v->source.anchor,v->source.caret):0,b=scope==0?MAX(v->source.anchor,v->source.caret):v->source.size;
 if(scope==0&&(a==b||(a>0&&a<v->source.size&&v->source.text[a-1]=='\r'&&v->source.text[a]=='\n')||(b>0&&b<v->source.size&&v->source.text[b-1]=='\r'&&v->source.text[b]=='\n'))) { status(v,"Select a nonempty stream fragment, or choose Whole buffer.");return; }
 v->payload=g_strndup(v->source.text+a,b-a);v->begin=mode==0?v->target.size:mode==1?0:mode==2?v->target.caret:MIN(v->target.anchor,v->target.caret);v->end=mode==1?v->target.size:mode==3?MAX(v->target.anchor,v->target.caret):v->begin;
 GError *error=NULL;v->result=insight_text_splice(v->target.text,v->target.size,v->payload,b-a,v->begin,v->end,&error);
 if(!v->result) { status(v,error?error->message:"Cannot build preview.");g_clear_error(&error);return; }
 code(v->after,v->result,v->target.name);gboolean changes=strcmp(v->target.text,v->result)!=0;gtk_dialog_set_response_sensitive(GTK_DIALOG(v->dialog),GTK_RESPONSE_APPLY,changes);status(v,changes?"Preview ready. Apply changes the destination in one Undo action; no disk writes.":"No text changes.");
}
static void destroyed(GtkWidget *w,gpointer data) { (void)w;((Review *)data)->closed=TRUE; }
static GtkWidget *combo(const gchar *name,const gchar **labels,guint n) {
 GtkWidget *w=gtk_combo_box_text_new();gtk_widget_set_name(w,name);for(guint i=0;i<n;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w),insight_tr(labels[i]));gtk_combo_box_set_active(GTK_COMBO_BOX(w),0);return w;
}
static void run(guint target,gboolean selection) {
 GeanyDocument *d=document_get_current();if(!d) return;Review v={0};guint source=d->id;
 if(!capture(&v.source,source)) { ui_set_statusbar(TRUE,"Text transfer requires NUL-free UTF-8 buffers up to 8 MiB.");return; }
 v.dialog=gtk_dialog_new_with_buttons(insight_tr("Copy Text to Buffer"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Reload snapshots"),1,insight_tr("New destination"),2,insight_tr("Preview"),GTK_RESPONSE_HELP,insight_tr("Apply"),GTK_RESPONSE_APPLY,NULL);
 g_object_ref_sink(v.dialog);gtk_widget_set_name(v.dialog,"insight-buffer-transfer-dialog");gtk_window_set_default_size(GTK_WINDOW(v.dialog),1060,650);g_signal_connect(v.dialog,"destroy",G_CALLBACK(destroyed),&v);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(v.dialog));gchar *source_label=g_strdup_printf("%s: %s [%u]",insight_tr("Source"),v.source.name?v.source.name:insight_tr("Untitled"),source);GtkWidget *source_name=gtk_label_new(source_label);gtk_label_set_ellipsize(GTK_LABEL(source_name),PANGO_ELLIPSIZE_MIDDLE);gtk_box_pack_start(GTK_BOX(box),source_name,FALSE,FALSE,4);g_free(source_label);GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,6);
 const gchar *scopes[]={"Selected text","Whole buffer"},*modes[]={"Append at EOF","Replace whole destination","Insert at destination caret","Replace destination selection"};
 v.scope=combo("insight-transfer-scope",scopes,2);gtk_combo_box_set_active(GTK_COMBO_BOX(v.scope),selection?0:1);v.mode=combo("insight-transfer-mode",modes,4);v.dest=gtk_combo_box_text_new();gtk_widget_set_name(v.dest,"insight-transfer-destination");
 gtk_box_pack_start(GTK_BOX(row),v.scope,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),v.dest,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(row),v.mode,FALSE,FALSE,0);
 GtkWidget *notebook=gtk_notebook_new(),*paned=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);gtk_box_pack_start(GTK_BOX(box),notebook,TRUE,TRUE,4);gtk_notebook_append_page(GTK_NOTEBOOK(notebook),paned,gtk_label_new(insight_tr("Destination: before / after")));v.source_view=SCINTILLA(scintilla_new());gtk_notebook_append_page(GTK_NOTEBOOK(notebook),GTK_WIDGET(v.source_view),gtk_label_new(insight_tr("Source snapshot")));code(v.source_view,v.source.text,v.source.name);v.before=SCINTILLA(scintilla_new());v.after=SCINTILLA(scintilla_new());gtk_widget_set_name(GTK_WIDGET(v.before),"insight-transfer-before");gtk_widget_set_name(GTK_WIDGET(v.after),"insight-transfer-after");gtk_paned_pack1(GTK_PANED(paned),GTK_WIDGET(v.before),TRUE,FALSE);gtk_paned_pack2(GTK_PANED(paned),GTK_WIDGET(v.after),TRUE,FALSE);gtk_paned_set_position(GTK_PANED(paned),520);
 v.status=gtk_label_new("");gtk_widget_set_name(v.status,"insight-transfer-status");gtk_label_set_line_wrap(GTK_LABEL(v.status),TRUE);gtk_box_pack_start(GTK_BOX(box),v.status,FALSE,FALSE,6);
 g_signal_connect(v.scope,"changed",G_CALLBACK(invalidate),&v);g_signal_connect(v.mode,"changed",G_CALLBACK(invalidate),&v);g_signal_connect(v.dest,"changed",G_CALLBACK(destination_changed),&v);populate(&v,target?target:source);gtk_widget_show_all(v.dialog);
 guint applied=0;
 while(!v.closed) {
  gint response=gtk_dialog_run(GTK_DIALOG(v.dialog));if(v.closed) break;
  if(response==GTK_RESPONSE_HELP) { preview(&v);continue; }
  if(response==1) { guint chosen=destination(&v);capture(&v.source,source);code(v.source_view,v.source.text,v.source.name);populate(&v,chosen);continue; }
  if(response==2) { GeanyDocument *next=document_new_file(NULL,NULL,"");if(next) populate(&v,next->id);continue; }
  if(response!=GTK_RESPONSE_APPLY) break;
  if(!v.result||!unchanged(&v)) { invalidate(NULL,&v);status(&v,"Snapshot changed. Reload and preview again.");continue; }
  ScintillaObject *s=v.target.sci;gsize amount=strlen(v.payload);guint64 expected=(v.end>v.begin)+(amount>0);
  GeanyDocument *dest=document_find_by_id(v.target.id);insight_completion_cancel(dest->editor);SEND(s,SCI_AUTOCCANCEL,0,0);
  if(!unchanged(&v)) { invalidate(NULL,&v);status(&v,"Snapshot changed. Reload and preview again.");continue; }
  sci_start_undo_action(s);SEND(s,SCI_SETTARGETRANGE,v.begin,v.end);SEND(s,SCI_REPLACETARGET,amount,(sptr_t)v.payload);sci_end_undo_action(s);
  dest=document_find_by_id(v.target.id);gchar *actual=dest&&dest->editor->sci==s?sci_get_contents(s,-1):NULL;
  gboolean ok=!v.closed&&actual&&v.target.revision==expected&&SEND(s,SCI_GETLENGTH,0,0)==strlen(v.result)&&!strcmp(actual,v.result);g_free(actual);
  if(ok) { SEND(s,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);SEND(s,SCI_CLEARSELECTIONS,0,0);SEND(s,SCI_SETSEL,v.begin,v.begin+amount);applied=v.target.id; }
  else ui_set_statusbar(TRUE,"Text transfer interrupted by an editor change. Inspect the destination and use Undo if needed.");
  break;
 }
 gtk_widget_destroy(v.dialog);g_object_unref(v.dialog);clear_snapshot(&v.source);clear_snapshot(&v.target);g_free(v.result);g_free(v.payload);
 d=document_find_by_id(applied);if(d) document_show_tab(d);
}
void insight_buffer_transfer_command(void) {
 GeanyDocument *d=document_get_current();if(d) run(0,sci_has_selection(d->editor->sci));
}
void insight_buffer_transfer_to_path(const gchar *path) {
 GFile *file=g_file_new_for_path(path);GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_ID_FILE,0,NULL,NULL);const gchar *identity=info?g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE):NULL;guint chosen=0,count=0;
 guint i;foreach_document(i) { GeanyDocument *d=documents[i];if(!d->file_name) continue;gchar *native=g_filename_from_utf8(d->file_name,-1,NULL,NULL,NULL);if(!native) continue;GFile *other=g_file_new_for_path(native);GFileInfo *oi=identity?g_file_query_info(other,G_FILE_ATTRIBUTE_ID_FILE,0,NULL,NULL):NULL;gboolean same=g_file_equal(file,other)||(identity&&oi&&!g_strcmp0(identity,g_file_info_get_attribute_string(oi,G_FILE_ATTRIBUTE_ID_FILE)));if(same) { chosen=d->id;count++; }g_clear_object(&oi);g_object_unref(other);g_free(native); }
 g_clear_object(&info);g_object_unref(file);if(count==1) run(chosen,TRUE);else ui_set_statusbar(TRUE,"Open destination is missing or has ambiguous aliases. Use Copy Text to Buffer to choose explicitly.");
}
