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
#include "../insight/core/diff.h"
#include "navqueue.h"
#include "insight-navigation.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define BASELINE "insight-original-text"
#define TOTAL_LIMIT (64u * 1024u * 1024u)
typedef struct { gchar *text;gsize length;guint64 generation;gboolean initialized; } Baseline;
typedef struct { guint document;ScintillaObject *sci;guint64 generation;gint anchor,caret; } Request;
static gsize baseline_bytes;
static gboolean initialized;
static guint64 original_view_serial;
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
static void restore_preview(gboolean whole)
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
    InsightRestorePlan plan={0};gint result=0;
    if(whole)
    {
        if(strcmp(base->text,current))
        {
            plan.replacement=g_strdup(base->text);plan.length=base->length;plan.blocks=1;
            plan.current_end=length;plan.original_end=base->length;
        }
    }
    else result=insight_restore_lines_plan(base->text,current,request.anchor,request.caret,&plan);
    if(result || !plan.blocks)
    {
        ui_set_statusbar(TRUE,"%s",insight_tr(result ? "Restore Lines comparison exceeded its work or size limit" : "No original-line changes touch this selection"));
        g_free(current);insight_restore_lines_clear(&plan);return;
    }
    GtkWidget *dialog=gtk_dialog_new_with_buttons(insight_tr(whole ? "Restore File" : "Restore Lines"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
        insight_tr("Copy Original Block"),201,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Restore"),GTK_RESPONSE_ACCEPT,NULL);
    g_object_ref_sink(dialog);gtk_widget_set_name(dialog,whole ? "insight-restore-file-dialog" : "insight-restore-lines-dialog");gtk_window_set_default_size(GTK_WINDOW(dialog),1000,540);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hint=gtk_label_new(insight_tr(whole ? "Restore the entire buffer to its original contents from opening, including changes already saved. The disk file is unchanged." : "Restore complete changed blocks touching the selected lines. Review both ranges before restoring."));
    gtk_label_set_line_wrap(GTK_LABEL(hint),TRUE);gtk_box_pack_start(GTK_BOX(box),hint,FALSE,FALSE,6);
    gchar *summary=whole ? g_strdup(insight_tr("Whole file restoration")) : insight_i18n_printf("Changed blocks: %u",(guint)plan.blocks);GtkWidget *count=gtk_label_new(summary);g_free(summary);gtk_widget_set_name(count,"insight-restore-count");gtk_box_pack_start(GTK_BOX(box),count,FALSE,FALSE,4);
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
void insight_revisions_show(void) { restore_preview(FALSE); }
void insight_revisions_file(void) { restore_preview(TRUE); }

/* A current-text snapshot owns its comparison. Never keep pointers into a
 * document baseline across a nested GTK loop or a document-close signal. */
typedef struct { Request request;gchar *original,*current;InsightDiff diff; } Changes;
static void changes_clear(Changes *changes)
{
    insight_diff_clear(&changes->diff);g_free(changes->original);g_free(changes->current);memset(changes,0,sizeof *changes);
}
static gboolean changes_capture(Changes *changes)
{
    if(!capture_request(&changes->request,FALSE)) return FALSE;
    Baseline *base=baseline(changes->request.sci);gint length=sci_get_length(changes->request.sci);
    if(!base->text || length<0 || (guint)length>INSIGHT_RESTORE_LIMIT) goto unavailable;
    changes->current=sci_get_contents(changes->request.sci,-1);
    if(!changes->current || strlen(changes->current)!=(gsize)length || !g_utf8_validate(changes->current,length,NULL) ||
        !byte_boundary(changes->current,length,changes->request.caret) || !byte_boundary(changes->current,length,changes->request.anchor)) goto unavailable;
    changes->original=g_strdup(base->text);
    if(insight_diff_compute(changes->original,changes->current,&changes->diff)) goto unavailable;
    return TRUE;
unavailable:
    ui_set_statusbar(TRUE,"%s",insight_tr("Original changes unavailable: snapshot, UTF-8, size or comparison work limit"));
    changes_clear(changes);return FALSE;
}
static gboolean changes_valid(const Changes *changes)
{
    GeanyDocument *doc=document_find_by_id(changes->request.document);Request current={0};
    return doc && doc==document_get_current() && doc->editor->sci==changes->request.sci &&
        capture_request(&current,FALSE) && current.generation==changes->request.generation;
}
static void changes_select(const Changes *changes,guint index)
{
    GeanyDocument *doc=document_find_by_id(changes->request.document);ScintillaObject *sci=doc->editor->sci;
    InsightDiffHunk *h=&changes->diff.hunks[index];
    navqueue_goto_line(doc,doc,sci_get_line_from_position(sci,h->b_begin)+1);
    SEND(sci,SCI_SETSEL,h->b_begin,h->b_end);SEND(sci,SCI_CHOOSECARETX,0,0);
    SEND(sci,SCI_ENSUREVISIBLE,sci_get_line_from_position(sci,h->b_begin),0);
    SEND(sci,SCI_SCROLLCARET,0,0);gtk_widget_grab_focus(GTK_WIDGET(sci));
}
void insight_revisions_change(gboolean forward)
{
    Changes changes={0};if(!changes_capture(&changes)) return;
    gsize caret=changes.request.caret;gssize target=-1;
    for(gsize i=0;i<changes.diff.count;i++)
    {
        gsize begin=changes.diff.hunks[i].b_begin;
        if(forward && begin>caret) { target=i;break; }
        if(!forward && begin<caret) target=i;
    }
    if(target>=0)
    {
        GeanyDocument *doc=document_get_current();gint position=changes.diff.hunks[target].b_begin;
        navqueue_goto_line(doc,doc,sci_get_line_from_position(doc->editor->sci,position)+1);
        if(!insight_navigation_extend_to(doc->editor->sci,position)) SEND(doc->editor->sci,SCI_SETSEL,position,position);
        SEND(doc->editor->sci,SCI_ENSUREVISIBLE,sci_get_line_from_position(doc->editor->sci,position),0);
        SEND(doc->editor->sci,SCI_SCROLLCARET,0,0);gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
    }
    else ui_set_statusbar(FALSE,"%s",insight_tr(forward ? "No next original-text change" : "No previous original-text change"));
    changes_clear(&changes);
}
static guint advance_line(const gchar *text,gsize *position,guint *line,gsize target)
{
    while(*position<target)
    {
        gchar c=text[(*position)++];
        if(c=='\r') { (*line)++;if(*position<target && text[*position]=='\n') (*position)++; }
        else if(c=='\n') (*line)++;
    }
    return *line;
}
static gchar *change_range(const gchar *text,gsize begin,gsize end,gsize *position,guint *line)
{
    guint first=advance_line(text,position,line,begin);
    if(begin==end) return insight_i18n_printf("At line %u (empty)",first);
    gsize last=end;if(text[last-1]=='\n') { last--;if(last>begin && text[last-1]=='\r') last--; }
    else if(text[last-1]=='\r') last--;
    guint final=advance_line(text,position,line,last);advance_line(text,position,line,end);
    return g_strdup_printf("%u–%u",first,final);
}
static void changes_row_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer dialog)
{
    (void)view;(void)path;(void)column;gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);
}
void insight_revisions_changes(void)
{
    Changes changes={0};if(!changes_capture(&changes)) return;
    GtkWidget *dialog=gtk_dialog_new_with_buttons(insight_tr("Original Changes"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
        insight_tr("Refresh"),201,insight_tr("Restore Lines…"),202,insight_tr("Close"),GTK_RESPONSE_CLOSE,insight_tr("Go To"),GTK_RESPONSE_ACCEPT,NULL);
    g_object_ref_sink(dialog);gtk_widget_set_name(dialog,"insight-original-changes-dialog");gtk_window_set_default_size(GTK_WINDOW(dialog),900,460);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hint=gtk_label_new(insight_tr("Changes since opening, including saved edits. Select a block; Restore Lines opens a separate confirmation. Refresh after editing."));
    gtk_label_set_line_wrap(GTK_LABEL(hint),TRUE);gtk_box_pack_start(GTK_BOX(box),hint,FALSE,FALSE,6);
    GtkWidget *count=gtk_label_new(NULL);gtk_widget_set_name(count,"insight-changes-count");gtk_box_pack_start(GTK_BOX(box),count,FALSE,FALSE,4);
    GtkListStore *store=gtk_list_store_new(5,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);
    GtkWidget *tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));gtk_widget_set_name(tree,"insight-changes-list");g_object_set_data(G_OBJECT(tree),"insight-no-i18n",GINT_TO_POINTER(1));
    const gchar *titles[]={"Block","Change","Original lines","Current lines","Text preview"};
    for(guint i=0;i<5;i++) { GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(insight_tr(titles[i]),gtk_cell_renderer_text_new(),"text",i,NULL);gtk_tree_view_column_set_resizable(column,TRUE);gtk_tree_view_append_column(GTK_TREE_VIEW(tree),column); }
    GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),tree);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,4);
    g_signal_connect(tree,"row-activated",G_CALLBACK(changes_row_activated),dialog);
    gboolean refresh=TRUE,restore=FALSE,done=FALSE;guint selected=0;
    while(!done)
    {
        if(refresh)
        {
            gtk_list_store_clear(store);guint shown=MIN(changes.diff.count,2000u);
            gsize a_position=0,b_position=0;guint a_line=1,b_line=1;
            for(guint i=0;i<shown;i++)
            {
                InsightDiffHunk *h=&changes.diff.hunks[i];gchar *a=change_range(changes.original,h->a_begin,h->a_end,&a_position,&a_line),*b=change_range(changes.current,h->b_begin,h->b_end,&b_position,&b_line);
                const gchar *text=h->b_begin==h->b_end ? changes.original+h->a_begin : changes.current+h->b_begin;
                gsize bytes=h->b_begin==h->b_end ? h->a_end-h->a_begin : h->b_end-h->b_begin;
                gsize n=MIN(bytes,160u);while(n && ((guchar)text[n]&0xc0)==0x80) n--;
                gchar *excerpt=g_strndup(text,n);g_strdelimit(excerpt,"\r\n\t",' ');
                GtkTreeIter iter;gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,i+1,1,insight_tr(h->a_begin==h->a_end ? "Inserted" : h->b_begin==h->b_end ? "Deleted" : "Modified"),2,a,3,b,4,excerpt,-1);g_free(a);g_free(b);g_free(excerpt);
            }
            gchar *label=insight_i18n_printf("Showing %u of %u changed blocks",shown,(guint)changes.diff.count);gtk_label_set_text(GTK_LABEL(count),label);g_free(label);
            GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_path_free(path);
            gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT,shown>0);gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),202,shown>0);refresh=FALSE;
        }
        gtk_widget_show_all(dialog);gint response=gtk_dialog_run(GTK_DIALOG(dialog));
        if(response==201)
        {
            /* Refresh belongs to the same document; never silently retarget a
             * list to a newly active document. */
            if(!document_find_by_id(changes.request.document) || !document_get_current() || document_get_current()->id!=changes.request.document) { ui_set_statusbar(TRUE,"%s",insight_tr("Changes list expired; reopen it for the active document"));break; }
            changes_clear(&changes);if(!changes_capture(&changes)) break;refresh=TRUE;continue;
        }
        if(response!=GTK_RESPONSE_ACCEPT && response!=202) break;
        if(!changes_valid(&changes)) { ui_set_statusbar(TRUE,"%s",insight_tr("Changes list is stale; use Refresh before navigating"));continue; }
        GtkTreeIter iter;GtkTreeModel *model=NULL;
        if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)),&model,&iter)) continue;
        gtk_tree_model_get(model,&iter,0,&selected,-1);if(!selected || selected>changes.diff.count) continue;selected--;
        restore=response==202;done=TRUE;
    }
    gtk_widget_destroy(dialog);g_object_unref(dialog);g_object_unref(store);
    if(done && changes_valid(&changes)) { changes_select(&changes,selected);changes_clear(&changes);if(restore) insight_revisions_show(); }
    else changes_clear(&changes);
}
void insight_revisions_original(void)
{
    GeanyDocument *source=document_get_current();if(!source) return;Baseline *base=baseline(source->editor->sci);
    if(!base->text) { ui_set_statusbar(TRUE,"%s",insight_tr("Original text unavailable: 8 MiB per document / 64 MiB total, valid UTF-8 required"));return; }
    gchar *text=g_strdup(base->text);GeanyFiletype *type=source->file_type;
    gchar *base_name=source->file_name ? g_path_get_basename(source->file_name) : g_strdup(insight_tr("Untitled"));
    gchar *name=g_strdup_printf("%s — %s (%" G_GUINT64_FORMAT ").txt",insight_tr("Original Contents"),base_name,++original_view_serial);g_free(base_name);
    GeanyDocument *view=document_new_file(name,type,NULL);g_free(name);
    if(view)
    {
        ScintillaObject *sci=view->editor->sci;SEND(sci,SCI_SETUNDOCOLLECTION,FALSE,0);sci_set_text(sci,text);SEND(sci,SCI_SETUNDOCOLLECTION,TRUE,0);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);
        view->readonly=TRUE;sci_set_readonly(sci,TRUE);document_set_text_changed(view,FALSE);ui_document_show_hide(view);
        g_object_set_data(G_OBJECT(sci),BASELINE,NULL);baseline(sci)->initialized=TRUE;
    }
    g_free(text);
}
static void revisions_command(GtkMenuItem *item,gpointer data) { (void)item;switch(GPOINTER_TO_INT(data)) { case 0: insight_revisions_show();break;case 1: insight_revisions_original();break;case 2: insight_revisions_file();break;case 3: insight_revisions_change(TRUE);break;case 4: insight_revisions_change(FALSE);break;case 5: insight_revisions_changes();break; } }
static void revisions_menu_show(GtkWidget *menu,gpointer data)
{
    (void)data;GeanyDocument *doc=document_get_current();
    gboolean available=doc && doc->is_valid && baseline(doc->editor->sci)->text;
    Request request={0};gboolean normal=available && capture_request(&request,FALSE);
    GList *children=gtk_container_get_children(GTK_CONTAINER(menu));
    for(GList *p=children;p;p=p->next)
    {
        GtkWidget *item=p->data;guint kind=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"insight-revision-kind"));
        if(!kind) continue;
        gtk_widget_set_sensitive(item,kind==2 ? available : normal);
        const guint keys[]={GEANY_KEYS_INSIGHT_RESTORELINES,GEANY_KEYS_INSIGHT_ORIGINALCONTENTS,GEANY_KEYS_INSIGHT_RESTOREFILE,GEANY_KEYS_INSIGHT_NEXTCHANGE,GEANY_KEYS_INSIGHT_PREVCHANGE,GEANY_KEYS_INSIGHT_ORIGINALCHANGES};
        guint key_id=keys[kind-1];
        GeanyKeyBinding *key=keybindings_lookup_item(kind==4 || kind==5 ? GEANY_KEY_GROUP_GOTO : GEANY_KEY_GROUP_EDITOR,key_id);
        GtkWidget *label=gtk_bin_get_child(GTK_BIN(item));
        if(key && GTK_IS_ACCEL_LABEL(label)) gtk_accel_label_set_accel(GTK_ACCEL_LABEL(label),key->key,key->mods);
    }
    g_list_free(children);
}
void insight_revisions_init(GtkWidget *edit_menu)
{
    if(initialized) return;
    initialized=TRUE;
    g_signal_connect_after(geany_object,"document-open",G_CALLBACK(capture_baseline),NULL);
    g_signal_connect_after(geany_object,"document-new",G_CALLBACK(capture_baseline),NULL);
    for(guint i=0;documents_array && i<documents_array->len;i++) { GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(doc->is_valid) capture_baseline(NULL,doc,NULL); }
    const gchar *labels[]={"Restore Lines…","Original Contents…","Restore File…","Go To Next Change","Go To Previous Change","Original Changes…"};const gchar *names[]={"insight-restore-lines","insight-original-contents","insight-restore-file","insight-next-change","insight-prev-change","insight-original-changes"};
    const guint keys[]={GEANY_KEYS_INSIGHT_RESTORELINES,GEANY_KEYS_INSIGHT_ORIGINALCONTENTS,GEANY_KEYS_INSIGHT_RESTOREFILE,GEANY_KEYS_INSIGHT_NEXTCHANGE,GEANY_KEYS_INSIGHT_PREVCHANGE,GEANY_KEYS_INSIGHT_ORIGINALCHANGES};
    for(guint i=0;i<G_N_ELEMENTS(keys);i++) {
        guint group=i==3 || i==4 ? GEANY_KEY_GROUP_GOTO : GEANY_KEY_GROUP_EDITOR;
        GtkWidget *entry=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(entry,names[i]);
        g_object_set_data(G_OBJECT(entry),"insight-revision-kind",GUINT_TO_POINTER(i+1));
        keybindings_attach_menu(group,keys[i],entry);g_signal_connect(entry,"activate",G_CALLBACK(revisions_command),GINT_TO_POINTER(i));gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu),entry);gtk_widget_show(entry);
        GtkWidget *popup=gtk_menu_item_new_with_label(labels[i]);gchar *popup_name=g_strdup_printf("insight-popup-%s",names[i]+8);gtk_widget_set_name(popup,popup_name);g_free(popup_name);
        g_object_set_data(G_OBJECT(popup),"insight-revision-kind",GUINT_TO_POINTER(i+1));g_signal_connect(popup,"activate",G_CALLBACK(revisions_command),GINT_TO_POINTER(i));gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.editor_menu),popup);gtk_widget_show(popup);
    }
    g_signal_connect(edit_menu,"show",G_CALLBACK(revisions_menu_show),NULL);
    g_signal_connect(main_widgets.editor_menu,"show",G_CALLBACK(revisions_menu_show),NULL);
    GtkWidget *help=gtk_menu_item_new_with_label("Restore Lines Help");g_signal_connect(help,"activate",G_CALLBACK(insight_help_show),"restore-lines.md");gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu),help);gtk_widget_show(help);
}
