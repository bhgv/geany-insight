/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-typing.h"
#include "document.h"
#include "geanyobject.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "insight-completion.h"
#include "insight-help.h"
#include "insight-i18n.h"
#include "../insight/core/repeat_typing.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define HISTORY_LIMIT 20u
typedef struct { GString *text;gchar *source; } TypedRun;
typedef struct { guint document;gint begin,end;gboolean extend;gchar *text; } Pending;
typedef struct { guint document;guint64 generation;gint anchor,caret;gboolean available,readonly;gchar *text; } Target;
static GPtrArray *runs;
static Pending pending;
static guint chain_document;
static gint expected_position;
static gboolean chain,blocked,initialized;
static guint64 history_serial;
static guint64 *generation(ScintillaObject *sci)
{
    guint64 *value=g_object_get_data(G_OBJECT(sci),"insight-typing-generation");
    if(!value) { value=g_new0(guint64,1);g_object_set_data_full(G_OBJECT(sci),"insight-typing-generation",value,g_free); }
    return value;
}
static void pending_clear(void) { g_free(pending.text);memset(&pending,0,sizeof pending); }
static void run_free(gpointer data) { TypedRun *run=data;g_string_free(run->text,TRUE);g_free(run->source);g_free(run); }
static gboolean normal(GeanyDocument *doc)
{
    if(!doc || !doc->is_valid) return FALSE;ScintillaObject *sci=doc->editor->sci;
    return SEND(sci,SCI_GETSELECTIONS,0,0)==1 && sci_get_selection_mode(sci)==SC_SEL_STREAM &&
        !SEND(sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) && !SEND(sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0);
}
static gboolean payload_valid(const gchar *text,gsize length)
{
    if(!length || length>INSIGHT_REPEAT_TEXT || memchr(text,0,length) || !g_utf8_validate(text,length,NULL)) return FALSE;
    for(const gchar *at=text;at<text+length;at=g_utf8_next_char(at))
    {
        gunichar code=g_utf8_get_char(at);GUnicodeType type=g_unichar_type(code);
        if((g_unichar_iscntrl(code) && code!='\t') || type==G_UNICODE_LINE_SEPARATOR || type==G_UNICODE_PARAGRAPH_SEPARATOR) return FALSE;
    }
    return TRUE;
}
static void break_chain(GObject *object,GeanyDocument *doc,gpointer data) { (void)object;(void)doc;(void)data;chain=FALSE;blocked=FALSE;pending_clear(); }
void insight_typing_notify(GeanyEditor *editor,SCNotification *nt)
{
    ScintillaObject *sci=editor->sci;GeanyDocument *doc=editor->document;
    if(nt->nmhdr.code==SCN_MODIFIED && (nt->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT)))
    {
        (*generation(sci))++;
        if(doc!=document_get_current()) { if(chain_document==doc->id) chain=FALSE;return; }
        gboolean extend=chain && chain_document==doc->id && nt->position==expected_position;
        pending_clear();chain=FALSE;
        if((nt->modificationType&SC_MOD_INSERTTEXT) && !(nt->modificationType&(SC_PERFORMED_UNDO|SC_PERFORMED_REDO)) &&
            (nt->modificationType&SC_PERFORMED_USER) && nt->length>0 && (guint)nt->length<=INSIGHT_REPEAT_TEXT && normal(doc) && payload_valid(nt->text,nt->length))
        {
            pending.document=doc->id;pending.begin=nt->position;pending.end=nt->position+nt->length;pending.extend=extend;pending.text=g_strndup(nt->text,nt->length);
        }
        else blocked=FALSE;
        return;
    }
    if(nt->nmhdr.code==SCN_UPDATEUI && chain && chain_document==doc->id &&
       (!normal(doc) || sci_has_selection(sci) || sci_get_current_position(sci)!=expected_position))
    { chain=FALSE;blocked=FALSE; }
    if(nt->nmhdr.code!=SCN_CHARADDED) return;
    if(!initialized || doc!=document_get_current() || !normal(doc) || doc->readonly || SEND(sci,SCI_GETREADONLY,0,0) ||
        (nt->characterSource!=SC_CHARACTERSOURCE_DIRECT_INPUT && nt->characterSource!=SC_CHARACTERSOURCE_IME_RESULT) || !pending.text || pending.document!=doc->id ||
        nt->ch!=(gint)g_utf8_get_char(pending.text) || sci_has_selection(sci) || sci_get_current_position(sci)!=pending.end)
    { pending_clear();chain=FALSE;blocked=FALSE;return; }
    gsize length=strlen(pending.text);gchar *actual=sci_get_contents_range(sci,pending.begin,pending.end);
    gboolean equal=actual && !strcmp(actual,pending.text);g_free(actual);
    if(!equal) { pending_clear();chain=FALSE;blocked=FALSE;return; }
    if(!pending.extend) blocked=FALSE;
    TypedRun *run=runs->len ? g_ptr_array_index(runs,runs->len-1):NULL;
    if(pending.extend && run && !blocked && run->text->len+length>INSIGHT_REPEAT_TEXT)
    {
        g_ptr_array_remove_index(runs,runs->len-1);history_serial++;blocked=TRUE;run=NULL;
    }
    if(!blocked)
    {
        if(!pending.extend || !run)
        {
            if(runs->len==HISTORY_LIMIT) g_ptr_array_remove_index(runs,0);
            run=g_new0(TypedRun,1);run->text=g_string_new(NULL);run->source=doc->file_name ? g_path_get_basename(doc->file_name):g_strdup(insight_tr("Untitled"));g_ptr_array_add(runs,run);
        }
        g_string_append(run->text,pending.text);history_serial++;
    }
    chain=TRUE;chain_document=doc->id;expected_position=pending.end;pending_clear();
}
static gboolean boundary(const gchar *text,gint length,gint pos)
{
    return pos>=0 && pos<=length && (pos==length || ((guchar)text[pos]&0xc0)!=0x80) &&
        !(pos>0 && pos<length && text[pos-1]=='\r' && text[pos]=='\n');
}
static void capture_target(Target *target)
{
    GeanyDocument *doc=document_get_current();if(!normal(doc)) return;ScintillaObject *sci=doc->editor->sci;gint length=sci_get_length(sci);
    if(length<0 || (guint)length>INSIGHT_REPEAT_DOCUMENT) return;
    target->text=sci_get_contents(sci,-1);target->anchor=SEND(sci,SCI_GETANCHOR,0,0);target->caret=sci_get_current_position(sci);
    if(!target->text || strlen(target->text)!=(gsize)length || !g_utf8_validate(target->text,length,NULL) ||
       !boundary(target->text,length,target->anchor) || !boundary(target->text,length,target->caret)) { g_clear_pointer(&target->text,g_free);return; }
    target->document=doc->id;target->generation=*generation(sci);target->readonly=doc->readonly || SEND(sci,SCI_GETREADONLY,0,0);target->available=TRUE;
}
static gboolean target_valid(const Target *target)
{
    GeanyDocument *doc=document_find_by_id(target->document);
    return target->available && !target->readonly && doc && doc==document_get_current() && normal(doc) && !doc->readonly &&
        !SEND(doc->editor->sci,SCI_GETREADONLY,0,0) && *generation(doc->editor->sci)==target->generation &&
        SEND(doc->editor->sci,SCI_GETANCHOR,0,0)==target->anchor && sci_get_current_position(doc->editor->sci)==target->caret;
}
static void apply(const Target *target,const gchar *text,guint count)
{
    if(!target_valid(target)) { ui_set_statusbar(TRUE,"%s",insight_tr("Repeat cancelled: the target, selection or permissions changed or are unsupported"));return; }
    InsightRepeatPlan plan={0};int result=insight_repeat_plan(target->text,target->anchor,target->caret,text,count,&plan);
    if(result) { ui_set_statusbar(TRUE,"%s",insight_tr("Repeat exceeded its text, count or document limit"));return; }
    if(plan.changed)
    {
        GeanyDocument *doc=document_get_current();ScintillaObject *sci=doc->editor->sci;chain=FALSE;blocked=FALSE;pending_clear();
        insight_completion_cancel(doc->editor);SEND(sci,SCI_AUTOCCANCEL,0,0);sci_start_undo_action(sci);
        SEND(sci,SCI_SETTARGETRANGE,plan.begin,plan.end);SEND(sci,SCI_REPLACETARGET,plan.length,(sptr_t)plan.replacement);SEND(sci,SCI_SETSEL,plan.caret,plan.caret);sci_end_undo_action(sci);
        SEND(sci,SCI_CHOOSECARETX,0,0);SEND(sci,SCI_SCROLLCARET,0,0);gtk_widget_grab_focus(GTK_WIDGET(sci));pending_clear();
    }
    insight_repeat_clear(&plan);
}
static gchar *last_text(void) { return runs && runs->len ? g_strdup(((TypedRun *)g_ptr_array_index(runs,runs->len-1))->text->str):NULL; }
typedef struct { GtkWidget *count,*view,*label;const gchar *text; } RepeatPreview;
static void preview_changed(GtkSpinButton *spin,gpointer data)
{
    (void)spin;RepeatPreview *preview=data;guint count=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(preview->count));GString *text=g_string_new(NULL);
    for(guint i=0;i<count;i++) g_string_append(text,preview->text);
    gsize shown=MIN(text->len,8192u);while(shown<text->len && shown && ((guchar)text->str[shown]&0xc0)==0x80) shown--;
    gchar *display=g_strndup(text->str,shown);gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(preview->view)),display,-1);g_free(display);
    gchar *label=insight_i18n_printf("%u repetitions · %u output bytes · preview up to 8 KiB",count,(guint)text->len);gtk_label_set_text(GTK_LABEL(preview->label),label);g_free(label);g_string_free(text,TRUE);
}
static void show_preview(const gchar *text)
{
    Target target={0};capture_target(&target);chain=FALSE;blocked=FALSE;pending_clear();
    GtkWidget *dialog=gtk_dialog_new_with_buttons(insight_tr("Repeat Typing Preview"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
        insight_tr("Copy Text"),201,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Repeat"),GTK_RESPONSE_ACCEPT,NULL);
    g_object_ref_sink(dialog);gtk_widget_set_name(dialog,"insight-repeat-typing-dialog");gtk_window_set_default_size(GTK_WINDOW(dialog),760,370);GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hint=gtk_label_new(insight_tr("Repeat a literal single-line typing fragment at the current selection. Clipboard, auto-indent and auto-pairs are not replayed. One Undo restores the target."));gtk_label_set_line_wrap(GTK_LABEL(hint),TRUE);gtk_box_pack_start(GTK_BOX(box),hint,FALSE,FALSE,6);
    RepeatPreview preview={0};preview.text=text;preview.count=gtk_spin_button_new_with_range(1,INSIGHT_REPEAT_COUNT,1);gtk_widget_set_name(preview.count,"insight-repeat-count");gtk_box_pack_start(GTK_BOX(box),preview.count,FALSE,FALSE,4);
    preview.label=gtk_label_new(NULL);gtk_widget_set_name(preview.label,"insight-repeat-summary");gtk_box_pack_start(GTK_BOX(box),preview.label,FALSE,FALSE,4);
    preview.view=gtk_text_view_new();gtk_widget_set_name(preview.view,"insight-repeat-preview");g_object_set_data(G_OBJECT(preview.view),"insight-no-i18n",GINT_TO_POINTER(1));gtk_text_view_set_editable(GTK_TEXT_VIEW(preview.view),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(preview.view),TRUE);
    GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),preview.view);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,4);g_signal_connect(preview.count,"value-changed",G_CALLBACK(preview_changed),&preview);preview_changed(NULL,&preview);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT,target_valid(&target));gtk_widget_show_all(dialog);gint response;guint count;
    do {
        response=gtk_dialog_run(GTK_DIALOG(dialog));count=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(preview.count));
        if(response==201) { GString *copy=g_string_new(NULL);for(guint i=0;i<count;i++) g_string_append(copy,text);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),copy->str,copy->len);g_string_free(copy,TRUE); }
    } while(response==201);
    gtk_widget_destroy(dialog);g_object_unref(dialog);if(response==GTK_RESPONSE_ACCEPT) apply(&target,text,count);g_free(target.text);
}
static void clear_history(void) { chain=FALSE;blocked=FALSE;pending_clear();g_ptr_array_set_size(runs,0);history_serial++; }
static void history_fill(GtkListStore *store,GtkTreeView *tree,GtkWidget *count)
{
    gtk_list_store_clear(store);
    for(guint row=0;row<runs->len;row++)
    {
        guint index=runs->len-row-1;TypedRun *run=g_ptr_array_index(runs,index);gsize shown=MIN(run->text->len,160u);while(shown<run->text->len && shown && ((guchar)run->text->str[shown]&0xc0)==0x80) shown--;
        gchar *excerpt=g_strndup(run->text->str,shown);g_strdelimit(excerpt,"\t",' ');GtkTreeIter iter;gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,row+1,1,run->source,2,(guint)run->text->len,3,excerpt,4,index,-1);g_free(excerpt);
    }
    gchar *label=insight_i18n_printf("Typing fragments: %u / 20 · memory only",runs->len);gtk_label_set_text(GTK_LABEL(count),label);g_free(label);
    GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1);gtk_tree_view_set_cursor(tree,path,NULL,FALSE);gtk_tree_path_free(path);
}
static void row_activated(GtkTreeView *tree,GtkTreePath *path,GtkTreeViewColumn *column,gpointer dialog) { (void)tree;(void)path;(void)column;gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT); }
static void show_history(void)
{
    chain=FALSE;blocked=FALSE;pending_clear();GtkWidget *dialog=gtk_dialog_new_with_buttons(insight_tr("Typing History"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
        insight_tr("Refresh"),201,insight_tr("Copy Text"),202,insight_tr("Clear History"),203,insight_tr("Close"),GTK_RESPONSE_CLOSE,insight_tr("Repeat…"),GTK_RESPONSE_ACCEPT,NULL);
    g_object_ref_sink(dialog);gtk_widget_set_name(dialog,"insight-typing-history-dialog");gtk_window_set_default_size(GTK_WINDOW(dialog),820,400);GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hint=gtk_label_new(insight_tr("Recent direct/committed typing fragments, newest first. Paste and programmatic edits are excluded. Select a fragment to preview repetition."));gtk_label_set_line_wrap(GTK_LABEL(hint),TRUE);gtk_box_pack_start(GTK_BOX(box),hint,FALSE,FALSE,6);
    GtkWidget *count=gtk_label_new(NULL);gtk_widget_set_name(count,"insight-typing-history-count");gtk_box_pack_start(GTK_BOX(box),count,FALSE,FALSE,4);
    GtkListStore *store=gtk_list_store_new(5,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_UINT);GtkWidget *tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));gtk_widget_set_name(tree,"insight-typing-history-list");g_object_set_data(G_OBJECT(tree),"insight-no-i18n",GINT_TO_POINTER(1));const gchar *titles[]={"Fragment","Source","Bytes","Text preview"};
    for(guint i=0;i<4;i++) { GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(insight_tr(titles[i]),gtk_cell_renderer_text_new(),"text",i,NULL);gtk_tree_view_column_set_resizable(column,TRUE);gtk_tree_view_append_column(GTK_TREE_VIEW(tree),column); }
    GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),tree);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,4);g_signal_connect(tree,"row-activated",G_CALLBACK(row_activated),dialog);
    history_fill(store,GTK_TREE_VIEW(tree),count);guint64 serial=history_serial;gchar *selected=NULL;
    for(;;)
    {
        gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),202,runs->len>0);gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),203,runs->len>0);gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT,runs->len>0);gtk_widget_show_all(dialog);gint response=gtk_dialog_run(GTK_DIALOG(dialog));
        if(response==201 || response==203) { if(response==203) clear_history();history_fill(store,GTK_TREE_VIEW(tree),count);serial=history_serial;continue; }
        if(response!=202 && response!=GTK_RESPONSE_ACCEPT) break;
        if(serial!=history_serial) { ui_set_statusbar(TRUE,"%s",insight_tr("Typing history changed; use Refresh"));continue; }
        GtkTreeModel *model=NULL;GtkTreeIter iter;guint index;
        if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)),&model,&iter)) continue;
        gtk_tree_model_get(model,&iter,4,&index,-1);if(index>=runs->len) continue;TypedRun *run=g_ptr_array_index(runs,index);
        if(response==202) { gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),run->text->str,run->text->len);continue; }
        selected=g_strdup(run->text->str);break;
    }
    gtk_widget_destroy(dialog);g_object_unref(dialog);g_object_unref(store);if(selected) { show_preview(selected);g_free(selected); }
}
void insight_typing_command(guint action)
{
    if(!initialized || action>4) return;
    if(action==2) { show_history();return; }
    if(action==4) { clear_history();return; }
    gchar *text=action==3 ? g_strdup("\t"):last_text();if(!text) { ui_set_statusbar(TRUE,"%s",insight_tr("No recorded typing fragment"));return; }
    if(action==1) show_preview(text);
    else { Target target={0};capture_target(&target);apply(&target,text,1);g_free(target.text); }
    g_free(text);
}
static void command(GtkMenuItem *item,gpointer data) { (void)item;insight_typing_command(GPOINTER_TO_UINT(data)); }
static void menu_show(GtkWidget *menu,gpointer data)
{
    (void)data;GeanyDocument *doc=document_get_current();gboolean writable=normal(doc) && !doc->readonly && !SEND(doc->editor->sci,SCI_GETREADONLY,0,0);GList *children=gtk_container_get_children(GTK_CONTAINER(menu));
    for(GList *p=children;p;p=p->next) { guint kind=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(p->data),"insight-typing-command"));if(kind) gtk_widget_set_sensitive(p->data,kind==1 ? runs->len && writable:kind==2 || kind==5 ? runs->len>0:kind==4 ? writable:TRUE); }g_list_free(children);
}
void insight_typing_init(GtkWidget *edit_menu)
{
    if(initialized) return;initialized=TRUE;runs=g_ptr_array_new_with_free_func(run_free);g_signal_connect_after(geany_object,"document-activate",G_CALLBACK(break_chain),NULL);
    GtkWidget *parent=gtk_menu_item_new_with_label("Typing and replay"),*menu=gtk_menu_new();gtk_widget_set_name(parent,"insight-typing-menu");gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent),menu);
    const gchar *labels[]={"Repeat Typing","Repeat Typing Preview…","Typing History…","Simple Tab","Clear Typing History"};const gchar *names[]={"repeat","preview","history","simple-tab","clear"};
    const guint keys[]={GEANY_KEYS_INSIGHT_REPEATTYPING,GEANY_KEYS_INSIGHT_REPEATPREVIEW,GEANY_KEYS_INSIGHT_TYPINGHISTORY,GEANY_KEYS_INSIGHT_SIMPLETAB,GEANY_KEYS_INSIGHT_CLEARTYPING};
    for(guint i=0;i<5;i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);gchar *name=g_strconcat("insight-typing-",names[i],NULL);gtk_widget_set_name(item,name);g_free(name);g_object_set_data(G_OBJECT(item),"insight-typing-command",GUINT_TO_POINTER(i+1));keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,keys[i],item);g_signal_connect(item,"activate",G_CALLBACK(command),GUINT_TO_POINTER(i));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item); }
    GtkWidget *help=gtk_menu_item_new_with_label("Typing Replay Help");g_signal_connect(help,"activate",G_CALLBACK(insight_help_show),"typing-replay.md");gtk_menu_shell_append(GTK_MENU_SHELL(menu),help);g_signal_connect(menu,"show",G_CALLBACK(menu_show),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu),parent);gtk_widget_show_all(parent);
}
