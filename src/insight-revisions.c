/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-revisions.h"
#include "document.h"
#include "geanyobject.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "insight-completion.h"
#include "insight-help.h"
#include "insight-i18n.h"
#include "../insight/core/restore_lines.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define BASELINE "insight-original-text"
#define TOTAL_LIMIT (64u * 1024u * 1024u)
typedef struct { gchar *text;gsize length;guint64 generation;gboolean initialized; } Baseline;
typedef struct { guint document;ScintillaObject *sci;guint64 generation;gint anchor,caret; } Request;
static gsize baseline_bytes;
static gboolean initialized;
static void baseline_free(gpointer data)
{
    Baseline *base=data;if(base->text) baseline_bytes-=base->length;
    g_free(base->text);g_free(base);
}
static Baseline *baseline(ScintillaObject *sci)
{
    Baseline *base=g_object_get_data(G_OBJECT(sci),BASELINE);
    if(!base) { base=g_new0(Baseline,1);g_object_set_data_full(G_OBJECT(sci),BASELINE,base,baseline_free); }
    return base;
}
static void capture_baseline(GObject *object,GeanyDocument *doc,gpointer data)
{
    (void)object;(void)data;if(!doc || !doc->is_valid) return;
    ScintillaObject *sci=doc->editor->sci;Baseline *base=baseline(sci);
    if(base->initialized) return;
    base->initialized=TRUE;
    gint length=sci_get_length(sci);
    if(length<0 || (guint)length>INSIGHT_RESTORE_LIMIT || (gsize)length>TOTAL_LIMIT-baseline_bytes ||
        doc->changed || SEND(sci,SCI_GETMODIFY,0,0)) return;
    gchar *text=sci_get_contents(sci,-1);
    if(!text || strlen(text)!=(gsize)length || !g_utf8_validate(text,length,NULL)) { g_free(text);return; }
    base->text=text;base->length=length;baseline_bytes+=length;
}
void insight_revisions_notify(GeanyEditor *editor,SCNotification *nt)
{
    if(nt->nmhdr.code==SCN_MODIFIED && (nt->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) baseline(editor->sci)->generation++;
}
static gboolean capture_request(Request *request,gboolean writable)
{
    GeanyDocument *doc=document_get_current();if(!doc || !doc->is_valid) return FALSE;
    ScintillaObject *sci=doc->editor->sci;
    if(SEND(sci,SCI_GETSELECTIONS,0,0)!=1 || sci_get_selection_mode(sci)!=SC_SEL_STREAM ||
       SEND(sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) || SEND(sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0) ||
       (writable && (doc->readonly || SEND(sci,SCI_GETREADONLY,0,0)))) return FALSE;
    request->document=doc->id;request->sci=sci;request->generation=baseline(sci)->generation;
    request->anchor=SEND(sci,SCI_GETANCHOR,0,0);request->caret=sci_get_current_position(sci);return TRUE;
}
static gboolean request_valid(const Request *request)
{
    GeanyDocument *doc=document_find_by_id(request->document);Request current={0};
    return doc && doc==document_get_current() && doc->editor->sci==request->sci && capture_request(&current,TRUE) &&
        current.generation==request->generation && current.anchor==request->anchor && current.caret==request->caret;
}
static gboolean byte_boundary(const gchar *text,gsize length,gint position)
{
    return position>=0 && (gsize)position<=length &&
        ((gsize)position==length || ((guchar)text[position]&0xc0)!=0x80) &&
        !(position>0 && (gsize)position<length && text[position-1]=='\r' && text[position]=='\n');
}
static guint line_number(const gchar *text,gsize position)
{
    guint line=1;for(gsize i=0;i<position;i++)
        if(text[i]=='\r') { line++;if(i+1<position && text[i+1]=='\n') i++; }
        else if(text[i]=='\n') line++;
    return line;
}
static GtkWidget *preview(const gchar *text,gsize begin,gsize end,const gchar *name)
{
    gsize length=MIN(end-begin,8192u);
    while(length && begin+length<end && ((guchar)text[begin+length]&0xc0)==0x80) length--;
    GString *display=g_string_sized_new(length+64);
    for(gsize i=0;i<length;i++)
    {
        gchar c=text[begin+i];if(c=='\r') { g_string_append_c(display,'\n');if(i+1<length && text[begin+i+1]=='\n') i++; }
        else g_string_append_c(display,c);
    }
    if(begin+length<end) { g_string_append(display,"\n");g_string_append(display,insight_tr("Preview truncated; Restore uses the complete block")); }
    GtkWidget *view=gtk_text_view_new();gtk_widget_set_name(view,name);gtk_text_view_set_editable(GTK_TEXT_VIEW(view),FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(view),TRUE);
    g_object_set_data(G_OBJECT(view),"insight-no-i18n",GINT_TO_POINTER(1));
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),display->str,display->len);g_string_free(display,TRUE);
    GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);gtk_container_add(GTK_CONTAINER(scroll),view);return scroll;
}
void insight_revisions_show(void)
{
    Request request={0};if(!capture_request(&request,FALSE)) return;
    Baseline *base=baseline(request.sci);
    if(!base->text) { ui_set_statusbar(TRUE,"%s",insight_tr("Original text unavailable: 8 MiB per document / 64 MiB total, valid UTF-8 required"));return; }
    gint length=sci_get_length(request.sci);
    if(length<0 || (guint)length>INSIGHT_RESTORE_LIMIT) { ui_set_statusbar(TRUE,"%s",insight_tr("Restore Lines is limited to 8 MiB documents"));return; }
    gchar *current=sci_get_contents(request.sci,-1);
    if(!current || strlen(current)!=(gsize)length || !g_utf8_validate(current,length,NULL) ||
        !byte_boundary(current,length,request.anchor) || !byte_boundary(current,length,request.caret))
    { g_free(current);ui_set_statusbar(TRUE,"%s",insight_tr("Restore Lines requires complete UTF-8 text without NUL bytes"));return; }
    InsightRestorePlan plan={0};gint result=insight_restore_lines_plan(base->text,current,request.anchor,request.caret,&plan);
    if(result || !plan.blocks)
    {
        ui_set_statusbar(TRUE,"%s",insight_tr(result ? "Restore Lines comparison exceeded its work or size limit" : "No original-line changes touch this selection"));
        g_free(current);insight_restore_lines_clear(&plan);return;
    }
    GtkWidget *dialog=gtk_dialog_new_with_buttons(insight_tr("Restore Lines"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
        insight_tr("Copy Original Block"),201,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Restore"),GTK_RESPONSE_ACCEPT,NULL);
    g_object_ref_sink(dialog);gtk_widget_set_name(dialog,"insight-restore-lines-dialog");gtk_window_set_default_size(GTK_WINDOW(dialog),1000,540);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hint=gtk_label_new(insight_tr("Restore complete changed blocks touching the selected lines. Review both ranges before restoring."));
    gtk_label_set_line_wrap(GTK_LABEL(hint),TRUE);gtk_box_pack_start(GTK_BOX(box),hint,FALSE,FALSE,6);
    gchar *summary=insight_i18n_printf("Changed blocks: %u",(guint)plan.blocks);GtkWidget *count=gtk_label_new(summary);g_free(summary);gtk_widget_set_name(count,"insight-restore-count");gtk_box_pack_start(GTK_BOX(box),count,FALSE,FALSE,4);
    GtkWidget *columns=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(box),columns,TRUE,TRUE,4);
    const gchar *sources[]={current,base->text};gsize begins[]={plan.current_begin,plan.original_begin},ends[]={plan.current_end,plan.original_end};
    const gchar *names[]={"insight-restore-current","insight-restore-original"};
    for(guint i=0;i<2;i++)
    {
        gsize last_position=ends[i];
        if(last_position>begins[i] && sources[i][last_position-1]=='\n') { last_position--;if(last_position>begins[i] && sources[i][last_position-1]=='\r') last_position--; }
        else if(last_position>begins[i] && sources[i][last_position-1]=='\r') last_position--;
        guint first=line_number(sources[i],begins[i]),last=line_number(sources[i],last_position);
        gchar *title=insight_i18n_printf(i ? "Original lines: %u–%u" : "Current lines: %u–%u",first,last);
        GtkWidget *frame=gtk_frame_new(title);g_free(title);gtk_container_add(GTK_CONTAINER(frame),preview(sources[i],begins[i],ends[i],names[i]));gtk_box_pack_start(GTK_BOX(columns),frame,TRUE,TRUE,0);
    }
    GtkWidget *note=gtk_label_new(insight_tr("Preview: first 8 KiB per side. Saved files are unchanged. One Undo restores the previous edited text."));
    gtk_label_set_line_wrap(GTK_LABEL(note),TRUE);gtk_box_pack_start(GTK_BOX(box),note,FALSE,FALSE,6);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT,request_valid(&request));
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),201,plan.length>0);gtk_widget_show_all(dialog);
    gint response;
    do {
        response=gtk_dialog_run(GTK_DIALOG(dialog));
        if(response==201) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),plan.replacement,plan.length);
    } while(response==201);
    gtk_widget_destroy(dialog);g_object_unref(dialog);g_free(current);
    if(response==GTK_RESPONSE_ACCEPT)
    {
        if(request_valid(&request))
        {
            GeanyDocument *doc=document_find_by_id(request.document);ScintillaObject *sci=doc->editor->sci;
            insight_completion_cancel(doc->editor);SEND(sci,SCI_AUTOCCANCEL,0,0);
            sci_start_undo_action(sci);SEND(sci,SCI_SETTARGETRANGE,plan.current_begin,plan.current_end);
            SEND(sci,SCI_REPLACETARGET,plan.length,(sptr_t)plan.replacement);
            gboolean forward=request.anchor<=request.caret;
            SEND(sci,SCI_SETSEL,forward ? plan.current_begin : plan.current_begin+plan.length,forward ? plan.current_begin+plan.length : plan.current_begin);
            sci_end_undo_action(sci);SEND(sci,SCI_CHOOSECARETX,0,0);SEND(sci,SCI_SCROLLCARET,0,0);gtk_widget_grab_focus(GTK_WIDGET(sci));
        }
        else ui_set_statusbar(TRUE,"%s",insight_tr("Restore cancelled: the document, selection or permissions changed"));
    }
    insight_restore_lines_clear(&plan);
}
void insight_revisions_original(void)
{
    GeanyDocument *source=document_get_current();if(!source) return;Baseline *base=baseline(source->editor->sci);
    if(!base->text) { ui_set_statusbar(TRUE,"%s",insight_tr("Original text unavailable: 8 MiB per document / 64 MiB total, valid UTF-8 required"));return; }
    gchar *text=g_strdup(base->text);GeanyFiletype *type=source->file_type;
    GeanyDocument *view=document_new_file("Original Contents.txt",type,NULL);
    if(view)
    {
        ScintillaObject *sci=view->editor->sci;SEND(sci,SCI_SETUNDOCOLLECTION,FALSE,0);sci_set_text(sci,text);SEND(sci,SCI_SETUNDOCOLLECTION,TRUE,0);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);
        view->readonly=TRUE;sci_set_readonly(sci,TRUE);document_set_text_changed(view,FALSE);ui_document_show_hide(view);
        g_object_set_data(G_OBJECT(sci),BASELINE,NULL);baseline(sci)->initialized=TRUE;
    }
    g_free(text);
}
static void revisions_command(GtkMenuItem *item,gpointer data) { (void)item;if(GPOINTER_TO_INT(data)) insight_revisions_original();else insight_revisions_show(); }
void insight_revisions_init(GtkWidget *edit_menu)
{
    if(initialized) return;
    initialized=TRUE;
    g_signal_connect_after(geany_object,"document-open",G_CALLBACK(capture_baseline),NULL);
    g_signal_connect_after(geany_object,"document-new",G_CALLBACK(capture_baseline),NULL);
    for(guint i=0;documents_array && i<documents_array->len;i++) { GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(doc->is_valid) capture_baseline(NULL,doc,NULL); }
    const gchar *labels[]={"Restore Lines…","Original Contents…"};const gchar *names[]={"insight-restore-lines","insight-original-contents"};
    const guint keys[]={GEANY_KEYS_INSIGHT_RESTORELINES,GEANY_KEYS_INSIGHT_ORIGINALCONTENTS};
    for(guint i=0;i<2;i++) { GtkWidget *entry=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(entry,names[i]);keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,keys[i],entry);g_signal_connect(entry,"activate",G_CALLBACK(revisions_command),GINT_TO_POINTER(i));gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu),entry);gtk_widget_show(entry); }
    GtkWidget *help=gtk_menu_item_new_with_label("Restore Lines Help");g_signal_connect(help,"activate",G_CALLBACK(insight_help_show),"restore-lines.md");gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu),help);gtk_widget_show(help);
}
