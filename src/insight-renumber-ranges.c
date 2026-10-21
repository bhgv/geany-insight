/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-renumber-ranges.h"
#include "insight-completion.h"
#include "insight-help.h"
#include "insight-i18n.h"
#include "document.h"
#include "main.h"
#include "filetypes.h"
#include "highlighting.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "../insight/desktop/renumber.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define LIMIT (8u*1024u*1024u)
typedef struct {
 InsightRenumberRange range;
 sptr_t anchor,caret,av,cv;
 guint index,line,column;
} CapturedRange;
typedef struct {
 ScintillaObject *source,*code[2];
 guint id,n,main;
 gint selection_mode;
 sptr_t ra,rc,rav,rcv;
 guint64 revision;
 gulong watch;
 gboolean closed;
 gchar *text,*filename;
 GArray *ranges;
 InsightRenumberPlan *plan;
 GtkWidget *dialog,*mode,*radix,*value,*status,*table;
 GtkListStore *model;
} Review;
gboolean insight_renumber_ranges_available(void) {
 GeanyDocument *d=document_get_current();if(!d||d->readonly) return FALSE;
 ScintillaObject *s=d->editor->sci;gint mode=SEND(s,SCI_GETSELECTIONMODE,0,0);
 return !SEND(s,SCI_GETREADONLY,0,0)&&(mode==SC_SEL_RECTANGLE||mode==SC_SEL_THIN||(mode==SC_SEL_STREAM&&SEND(s,SCI_GETSELECTIONS,0,0)>1));
}
static void modified(ScintillaObject *s,gint unused,SCNotification *nt,gpointer data) {
 (void)s;(void)unused;Review *v=data;if(nt->nmhdr.code==SCN_MODIFIED&&(nt->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) v->revision++;
}
static void destroyed(GtkWidget *w,gpointer data) { (void)w;((Review *)data)->closed=TRUE; }
static gint compare(gconstpointer a,gconstpointer b) {
 const CapturedRange *x=a,*y=b;
 if(x->range.begin!=y->range.begin) return x->range.begin<y->range.begin ? -1:1;
 if(x->range.end!=y->range.end) return x->range.end<y->range.end ? -1:1;
 return x->index<y->index ? -1:x->index!=y->index;
}
static gboolean live(Review *v) {
 GeanyDocument *d=document_find_by_id(v->id);
 return !v->closed&&d&&d==document_get_current()&&d->editor->sci==v->source&&!d->readonly&&!SEND(v->source,SCI_GETREADONLY,0,0);
}
static gboolean unchanged(Review *v) {
 if(!live(v)||v->revision||SEND(v->source,SCI_GETSELECTIONMODE,0,0)!=v->selection_mode||SEND(v->source,SCI_GETSELECTIONS,0,0)!=v->n||SEND(v->source,SCI_GETMAINSELECTION,0,0)!=v->main) return FALSE;
 if(v->selection_mode!=SC_SEL_STREAM&&(SEND(v->source,SCI_GETRECTANGULARSELECTIONANCHOR,0,0)!=v->ra||SEND(v->source,SCI_GETRECTANGULARSELECTIONCARET,0,0)!=v->rc||SEND(v->source,SCI_GETRECTANGULARSELECTIONANCHORVIRTUALSPACE,0,0)!=v->rav||SEND(v->source,SCI_GETRECTANGULARSELECTIONCARETVIRTUALSPACE,0,0)!=v->rcv)) return FALSE;
 for(guint i=0;i<v->n;i++) {
  CapturedRange *r=&g_array_index(v->ranges,CapturedRange,i);guint n=r->index;
  if(SEND(v->source,SCI_GETSELECTIONNANCHOR,n,0)!=r->anchor||SEND(v->source,SCI_GETSELECTIONNCARET,n,0)!=r->caret||SEND(v->source,SCI_GETSELECTIONNANCHORVIRTUALSPACE,n,0)!=r->av||SEND(v->source,SCI_GETSELECTIONNCARETVIRTUALSPACE,n,0)!=r->cv) return FALSE;
 }
 return TRUE;
}
static gboolean capture(Review *v) {
 if(!insight_renumber_ranges_available()) return FALSE;
 GeanyDocument *d=document_get_current();v->source=g_object_ref(d->editor->sci);v->id=d->id;v->filename=g_strdup(d->file_name);
 sptr_t len=SEND(v->source,SCI_GETLENGTH,0,0),n=SEND(v->source,SCI_GETSELECTIONS,0,0);
 if(len<0||len>LIMIT||n<1||n>4096) { ui_set_statusbar(TRUE,"Renumber ranges requires a document up to 8 MiB and at most 4096 selections");return FALSE; }
 v->text=sci_get_contents(v->source,-1);
 if(memchr(v->text,0,len)||!g_utf8_validate(v->text,len,NULL)) { ui_set_statusbar(TRUE,"Renumber requires NUL-free UTF-8 source");return FALSE; }
 v->n=n;v->main=SEND(v->source,SCI_GETMAINSELECTION,0,0);v->selection_mode=SEND(v->source,SCI_GETSELECTIONMODE,0,0);
 v->ra=SEND(v->source,SCI_GETRECTANGULARSELECTIONANCHOR,0,0);v->rc=SEND(v->source,SCI_GETRECTANGULARSELECTIONCARET,0,0);v->rav=SEND(v->source,SCI_GETRECTANGULARSELECTIONANCHORVIRTUALSPACE,0,0);v->rcv=SEND(v->source,SCI_GETRECTANGULARSELECTIONCARETVIRTUALSPACE,0,0);
 v->ranges=g_array_sized_new(FALSE,FALSE,sizeof(CapturedRange),n);
 for(guint i=0;i<v->n;i++) {
  CapturedRange r={0};r.index=i;r.anchor=SEND(v->source,SCI_GETSELECTIONNANCHOR,i,0);r.caret=SEND(v->source,SCI_GETSELECTIONNCARET,i,0);r.av=SEND(v->source,SCI_GETSELECTIONNANCHORVIRTUALSPACE,i,0);r.cv=SEND(v->source,SCI_GETSELECTIONNCARETVIRTUALSPACE,i,0);r.range.begin=MIN(r.anchor,r.caret);r.range.end=MAX(r.anchor,r.caret);r.line=SEND(v->source,SCI_LINEFROMPOSITION,r.range.begin,0)+1;r.column=SEND(v->source,SCI_GETCOLUMN,r.range.begin,0)+1;
  sptr_t anchors[]={r.anchor,r.caret},virtuals[]={r.av,r.cv};
  for(guint k=0;k<2;k++) if(virtuals[k]&&anchors[k]!=SEND(v->source,SCI_GETLINEENDPOSITION,SEND(v->source,SCI_LINEFROMPOSITION,anchors[k],0),0)) { ui_set_statusbar(TRUE,"Renumber column boundaries inside tabs are unsupported; select complete tab boundaries");return FALSE; }
  g_array_append_val(v->ranges,r);
 }
 g_array_sort(v->ranges,compare);v->watch=g_signal_connect(v->source,"sci-notify",G_CALLBACK(modified),v);return TRUE;
}
static void code(Review *v,guint side,const gchar *text) {
 ScintillaObject *s=v->code[side];GeanyFiletype *ft=filetypes_detect_from_extension(v->filename ? v->filename:"");highlighting_set_styles(s,ft ? ft:filetypes[GEANY_FILETYPES_NONE]);SEND(s,SCI_SETREADONLY,FALSE,0);SEND(s,SCI_SETUNDOCOLLECTION,FALSE,0);sci_set_text(s,text);SEND(s,SCI_EMPTYUNDOBUFFER,0,0);SEND(s,SCI_SETREADONLY,TRUE,0);sci_set_line_numbers(s,TRUE);SEND(s,SCI_SETMARGINWIDTHN,0,0);SEND(s,SCI_SETMARGINWIDTHN,1,MAX(45,SEND(s,SCI_GETMARGINWIDTHN,1,0)));SEND(s,SCI_COLOURISE,0,-1);
}
static gchar *snippet(const gchar *s,gsize len) {
 glong chars=MIN((glong)80,g_utf8_strlen(s,len));const gchar *end=g_utf8_offset_to_pointer(s,chars);gchar *result=g_strndup(s,MIN(len,(gsize)(end-s)));for(gchar *p=result;*p;p++) if(*p=='\r'||*p=='\n'||*p=='\t') *p=' ';return result;
}
static void rows(Review *v) {
 gtk_list_store_clear(v->model);
 for(guint i=0;i<v->n;i++) {
  InsightRenumberRange r=v->plan ? g_array_index(v->plan->ranges,InsightRenumberRange,i):g_array_index(v->ranges,CapturedRange,i).range;
  gchar *old=snippet(v->text+r.begin,r.end-r.begin),*next=v->plan ? snippet(v->plan->text+r.after_begin,r.after_end-r.after_begin):g_strdup("");GtkTreeIter it;gtk_list_store_append(v->model,&it);CapturedRange captured=g_array_index(v->ranges,CapturedRange,i);guint line=captured.line,column=captured.column;
  gtk_list_store_set(v->model,&it,0,line,1,column,2,r.count,3,old,4,next,5,i,-1);g_free(old);g_free(next);
 }
}
static void selected(GtkTreeSelection *selection,gpointer data) {
 Review *v=data;GtkTreeModel *model;GtkTreeIter it;if(v->closed||!gtk_tree_selection_get_selected(selection,&model,&it)) return;guint i;gtk_tree_model_get(model,&it,5,&i,-1);if(i>=v->n) return;
 InsightRenumberRange r=g_array_index(v->ranges,CapturedRange,i).range;SEND(v->code[0],SCI_SETSEL,r.begin,r.end);SEND(v->code[0],SCI_SCROLLCARET,0,0);
 if(v->plan) { r=g_array_index(v->plan->ranges,InsightRenumberRange,i);SEND(v->code[1],SCI_SETSEL,r.after_begin,r.after_end);SEND(v->code[1],SCI_SCROLLCARET,0,0); }
}
static void options_changed(GtkWidget *w,gpointer data) {
 (void)w;Review *v=data;if(v->closed) return;insight_renumber_plan_free(v->plan);v->plan=NULL;rows(v);gtk_widget_set_sensitive(v->value,gtk_combo_box_get_active(GTK_COMBO_BOX(v->mode))!=0);code(v,1,"");gtk_dialog_set_response_sensitive(GTK_DIALOG(v->dialog),GTK_RESPONSE_APPLY,FALSE);gtk_label_set_text(GTK_LABEL(v->status),insight_tr("Press Preview to inspect all selected fragments. No source changes yet."));
}
static void preview(Review *v) {
 if(!unchanged(v)) { gtk_label_set_text(GTK_LABEL(v->status),insight_tr("Source or selection changed. Cancel and run Renumber again."));gtk_dialog_set_response_sensitive(GTK_DIALOG(v->dialog),GTK_RESPONSE_APPLY,FALSE);return; }
 GArray *ranges=g_array_sized_new(FALSE,FALSE,sizeof(InsightRenumberRange),v->n);for(guint i=0;i<v->n;i++) { InsightRenumberRange r=g_array_index(v->ranges,CapturedRange,i).range;g_array_append_val(ranges,r); }
 GError *e=NULL;insight_renumber_plan_free(v->plan);v->plan=insight_renumber_plan(v->text,(InsightRenumberRange *)ranges->data,v->n,gtk_combo_box_get_active(GTK_COMBO_BOX(v->mode)),gtk_combo_box_get_active(GTK_COMBO_BOX(v->radix)),gtk_entry_get_text(GTK_ENTRY(v->value)),&e);g_array_unref(ranges);
 if(!v->plan) { rows(v);code(v,1,"");gtk_label_set_text(GTK_LABEL(v->status),e->message);g_clear_error(&e);gtk_dialog_set_response_sensitive(GTK_DIALOG(v->dialog),GTK_RESPONSE_APPLY,FALSE);return; }
 code(v,1,v->plan->text);rows(v);gboolean changes=strcmp(v->text,v->plan->text)!=0;gchar *message=g_strdup_printf("%u selected fragments, %u integer tokens. %s",v->n,v->plan->count,changes ? "Apply is one Undo action; unselected bytes stay unchanged.":"No text changes. Empty columns never expand to the whole file.");gtk_label_set_text(GTK_LABEL(v->status),message);g_free(message);gtk_dialog_set_response_sensitive(GTK_DIALOG(v->dialog),GTK_RESPONSE_APPLY,changes);
}
static gboolean apply(Review *v) {
 if(!v->plan||!unchanged(v)) return FALSE;
 GeanyDocument *d=document_find_by_id(v->id);insight_completion_cancel(d->editor);SEND(v->source,SCI_AUTOCCANCEL,0,0);if(!unchanged(v)) return FALSE;
 gsize length=strlen(v->text);gboolean ok=TRUE;sci_start_undo_action(v->source);
 for(guint i=v->n;i>0;i--) {
  InsightRenumberRange r=g_array_index(v->plan->ranges,InsightRenumberRange,i-1);gsize before=r.end-r.begin,after=r.after_end-r.after_begin;
  if(before==after&&!memcmp(v->text+r.begin,v->plan->text+r.after_begin,before)) continue;
  if(!live(v)) { ok=FALSE;break; }
  guint64 revision=v->revision;SEND(v->source,SCI_SETTARGETRANGE,r.begin,r.end);SEND(v->source,SCI_REPLACETARGET,after,(sptr_t)(v->plan->text+r.after_begin));length=length-before+after;
  if(!live(v)||v->revision-revision!=(guint64)((before>0)+(after>0))||SEND(v->source,SCI_GETLENGTH,0,0)!=(sptr_t)length) { ok=FALSE;break; }
  /* InsertCheck handlers may legally change text without extra notifications,
   * including substitutions of exactly the same length. */
  gchar *inserted=sci_get_contents_range(v->source,r.begin,r.begin+after);gboolean exact=!memcmp(inserted,v->plan->text+r.after_begin,after);g_free(inserted);if(!exact) { ok=FALSE;break; }
 }
 sci_end_undo_action(v->source);if(!ok||!live(v)) return FALSE;
 gchar *actual=sci_get_contents(v->source,-1);ok=!strcmp(actual,v->plan->text);g_free(actual);if(!ok) return FALSE;
 /* A rectangle cannot describe rows that grew by different amounts. Select the
  * exact transformed fragments as ordinary multiple selections instead. */
 SEND(v->source,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);SEND(v->source,SCI_CLEARSELECTIONS,0,0);guint main=0;
 for(guint i=0;i<v->n;i++) {
  CapturedRange old=g_array_index(v->ranges,CapturedRange,i);InsightRenumberRange r=g_array_index(v->plan->ranges,InsightRenumberRange,i);gboolean reverse=old.anchor>old.caret||(old.anchor==old.caret&&old.av>old.cv);
  SEND(v->source,i ? SCI_ADDSELECTION:SCI_SETSELECTION,reverse ? r.after_begin:r.after_end,reverse ? r.after_end:r.after_begin);if(old.index==v->main) main=i;
 }
 SEND(v->source,SCI_SETMAINSELECTION,main,0);return TRUE;
}
void insight_renumber_ranges_command(void) {
 Review v={0};if(!capture(&v)) goto out;
 v.dialog=gtk_dialog_new_with_buttons(insight_tr("Renumber selected fragments"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Preview"),GTK_RESPONSE_HELP,insight_tr("Apply"),GTK_RESPONSE_APPLY,NULL);g_object_ref_sink(v.dialog);gtk_widget_set_name(v.dialog,"insight-renumber-ranges-dialog");gtk_window_set_default_size(GTK_WINDOW(v.dialog),1080,760);g_signal_connect(v.dialog,"destroy",G_CALLBACK(destroyed),&v);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(v.dialog)),*bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_container_set_border_width(GTK_CONTAINER(box),6);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,4);
 v.mode=gtk_combo_box_text_new();v.radix=gtk_combo_box_text_new();v.value=gtk_entry_new();const gchar *modes[]={"Auto: ascend from first number","Start at value","Offset existing numbers"},*radices[]={"Auto: preserve each number's radix","Decimal","Hexadecimal","Octal"};for(guint i=0;i<3;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(v.mode),insight_tr(modes[i]));for(guint i=0;i<4;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(v.radix),insight_tr(radices[i]));gtk_combo_box_set_active(GTK_COMBO_BOX(v.mode),0);gtk_combo_box_set_active(GTK_COMBO_BOX(v.radix),0);gtk_entry_set_text(GTK_ENTRY(v.value),"1");gtk_entry_set_max_length(GTK_ENTRY(v.value),32);gtk_widget_set_name(v.mode,"insight-renumber-ranges-mode");gtk_widget_set_name(v.radix,"insight-renumber-ranges-radix");gtk_widget_set_name(v.value,"insight-renumber-ranges-value");gtk_box_pack_start(GTK_BOX(bar),v.mode,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(bar),v.radix,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(bar),v.value,TRUE,TRUE,0);
 GtkWidget *scope=gtk_label_new(insight_tr("Scope: selected column or multiple fragments, in source order. Virtual space adds no text."));gtk_label_set_line_wrap(GTK_LABEL(scope),TRUE);gtk_box_pack_start(GTK_BOX(box),scope,FALSE,FALSE,4);
 v.model=gtk_list_store_new(6,G_TYPE_UINT,G_TYPE_UINT,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_UINT);v.table=gtk_tree_view_new_with_model(GTK_TREE_MODEL(v.model));gtk_widget_set_name(v.table,"insight-renumber-ranges-list");const gchar *columns[]={"Line","Column","Integer tokens","Before","After"};for(guint i=0;i<5;i++) gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(v.table),-1,insight_tr(columns[i]),gtk_cell_renderer_text_new(),"text",i,NULL);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(v.table)),"changed",G_CALLBACK(selected),&v);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),v.table);gtk_widget_set_size_request(scroll,-1,130);gtk_box_pack_start(GTK_BOX(box),scroll,FALSE,FALSE,4);
 GtkWidget *panes=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);gtk_box_pack_start(GTK_BOX(box),panes,TRUE,TRUE,4);
 for(guint i=0;i<2;i++) { GtkWidget *col=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_box_pack_start(GTK_BOX(col),gtk_label_new(insight_tr(i ? "After — read only":"Before — read only")),FALSE,FALSE,0);v.code[i]=SCINTILLA(scintilla_new());gtk_widget_set_name(GTK_WIDGET(v.code[i]),i ? "insight-renumber-ranges-after":"insight-renumber-ranges-before");SEND(v.code[i],SCI_SETCODEPAGE,SC_CP_UTF8,0);sci_set_symbol_margin(v.code[i],FALSE);SEND(v.code[i],SCI_SETMARGINWIDTHN,2,0);gtk_box_pack_start(GTK_BOX(col),GTK_WIDGET(v.code[i]),TRUE,TRUE,0);if(i) gtk_paned_pack2(GTK_PANED(panes),col,TRUE,FALSE);else gtk_paned_pack1(GTK_PANED(panes),col,TRUE,FALSE);code(&v,i,i ? "":v.text); }
 v.status=gtk_label_new("");gtk_widget_set_name(v.status,"insight-renumber-ranges-status");gtk_label_set_line_wrap(GTK_LABEL(v.status),TRUE);gtk_box_pack_start(GTK_BOX(box),v.status,FALSE,FALSE,4);insight_panel_help_attach(box,"renumber.md");g_signal_connect(v.mode,"changed",G_CALLBACK(options_changed),&v);g_signal_connect(v.radix,"changed",G_CALLBACK(options_changed),&v);g_signal_connect(v.value,"changed",G_CALLBACK(options_changed),&v);rows(&v);options_changed(NULL,&v);gtk_widget_show_all(v.dialog);
 for(;;) { gint response=gtk_dialog_run(GTK_DIALOG(v.dialog));if(v.closed) break;if(response==GTK_RESPONSE_HELP) preview(&v);else if(response==GTK_RESPONSE_APPLY) { gboolean ok=apply(&v);if(!v.closed) ui_set_statusbar(TRUE,"%s",ok ? "Renumber applied to selected fragments; one Undo, disk unchanged":"Renumber stopped: source, selection or permissions changed. If partial edits occurred, Undo the operation.");break; }else break; }
 gtk_widget_destroy(v.dialog);g_object_unref(v.dialog);g_object_unref(v.model);
out:
 if(v.source) { if(v.watch&&g_signal_handler_is_connected(v.source,v.watch)) g_signal_handler_disconnect(v.source,v.watch);g_object_unref(v.source); }if(v.ranges) g_array_unref(v.ranges);insight_renumber_plan_free(v.plan);g_free(v.text);g_free(v.filename);
}
