/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-selection-search.h"
#include "document.h"
#include "search.h"
#include "keybindings.h"
#include "main.h"
#include "navqueue.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "insight-help.h"
#include "insight-i18n.h"
#include <string.h>
#define QS_LIMIT (8u*1024u*1024u)
#define QS_PATTERN 4096u
#define QS_ROWS 5000u
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
typedef struct { gint begin,end; } QuickMatch;
typedef struct { guint document;guint64 generation;gint length,anchor,caret;gchar *text; } QuickSource;
static gchar *owned_pattern;
static GeanyFindFlags owned_flags;
static guint64 *generation(ScintillaObject *sci)
{
    guint64 *value=g_object_get_data(G_OBJECT(sci),"insight-selection-search-generation");
    if(!value) { value=g_new0(guint64,1);g_object_set_data_full(G_OBJECT(sci),"insight-selection-search-generation",value,g_free); }
    return value;
}
void insight_selection_search_notify(GeanyEditor *editor,SCNotification *nt)
{
    if(nt->nmhdr.code==SCN_MODIFIED && (nt->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) (*generation(editor->sci))++;
}
static gboolean normal(GeanyDocument *doc)
{
    if(!doc || !doc->is_valid) return FALSE;ScintillaObject *sci=doc->editor->sci;
    return SEND(sci,SCI_GETSELECTIONS,0,0)==1 && sci_get_selection_mode(sci)==SC_SEL_STREAM &&
        !SEND(sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) && !SEND(sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0);
}
static gboolean boundary(const gchar *text,gint length,gint pos)
{
    return pos>=0 && pos<=length && (pos==length || ((guchar)text[pos]&0xc0)!=0x80) &&
        !(pos>0 && pos<length && text[pos-1]=='\r' && text[pos]=='\n');
}
static gboolean capture(QuickSource *source)
{
    GeanyDocument *doc=document_get_current();if(!normal(doc)) return FALSE;ScintillaObject *sci=doc->editor->sci;
    source->length=sci_get_length(sci);if(source->length<0 || (guint)source->length>QS_LIMIT) goto invalid;
    source->text=sci_get_contents(sci,-1);source->anchor=SEND(sci,SCI_GETANCHOR,0,0);source->caret=sci_get_current_position(sci);
    if(!source->text || strlen(source->text)!=(gsize)source->length || !g_utf8_validate(source->text,source->length,NULL) ||
       !boundary(source->text,source->length,source->anchor) || !boundary(source->text,source->length,source->caret)) goto invalid;
    source->document=doc->id;source->generation=*generation(sci);return TRUE;
invalid:
    g_clear_pointer(&source->text,g_free);ui_set_statusbar(TRUE,"%s",insight_tr("Selection search requires complete UTF-8 without NUL, normal positions and documents up to 8 MiB"));return FALSE;
}
static gboolean valid(const QuickSource *source)
{
    GeanyDocument *doc=document_find_by_id(source->document);
    return doc && doc==document_get_current() && normal(doc) && *generation(doc->editor->sci)==source->generation;
}
static gboolean pattern_valid(const gchar *pattern)
{
    return pattern && *pattern && strlen(pattern)<=QS_PATTERN && g_utf8_validate(pattern,-1,NULL) && !strchr(pattern,'\r') && !strchr(pattern,'\n');
}
static gchar *selection_pattern(const QuickSource *source)
{
    gint first=MIN(source->anchor,source->caret),last=MAX(source->anchor,source->caret);
    if(first==last || (guint)(last-first)>QS_PATTERN) return NULL;
    gchar *text=g_strndup(source->text+first,last-first);if(!pattern_valid(text)) g_clear_pointer(&text,g_free);return text;
}
static gchar *word_pattern(const QuickSource *source,gint *first,gint *last)
{
    gint at=MIN(source->anchor,source->caret),limit=MAX(source->anchor,source->caret);guint scanned=0;
    if(at==limit) limit=source->length;
    while(at<limit)
    {
        gunichar code=g_utf8_get_char(source->text+at);
        if(g_unichar_isalnum(code) || code=='_') break;
        if(source->anchor==source->caret || ++scanned>QS_PATTERN) return NULL;
        at=g_utf8_next_char(source->text+at)-source->text;
    }
    if(at>=limit) return NULL;ScintillaObject *sci=document_get_current()->editor->sci;
    *first=SEND(sci,SCI_WORDSTARTPOSITION,at,TRUE);*last=SEND(sci,SCI_WORDENDPOSITION,at,TRUE);
    if(*first<0 || *last<=*first || *last>source->length || (guint)(*last-*first)>QS_PATTERN) return NULL;
    gchar *text=g_strndup(source->text+*first,*last-*first);if(!pattern_valid(text)) g_clear_pointer(&text,g_free);return text;
}
static GtkWidget *named(GtkWidget *widget,const gchar *name)
{
    if(!g_strcmp0(gtk_widget_get_name(widget),name)) return widget;
    if(GTK_IS_CONTAINER(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static gboolean option(const gchar *name,gboolean fallback)
{
    GtkWidget *widget=named(main_widgets.window,name);return GTK_IS_TOGGLE_BUTTON(widget) ? gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)):fallback;
}
static void sync_pattern(const gchar *pattern,gboolean sensitive,gboolean word,gboolean backwards)
{
    /* The native pattern is also used by F3. Update the existing file bar
     * without incremental searching or altering its wrap/select preferences. */
    GtkWidget *incremental=named(main_widgets.window,"insight-file-search-incremental");gboolean old=GTK_IS_TOGGLE_BUTTON(incremental) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(incremental));
    if(incremental) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(incremental),FALSE);
    gboolean escaped=pattern[0]=='~';gchar *bar_pattern=escaped ? g_regex_escape_string(pattern,-1):g_strdup(pattern);
    const gchar *names[]={"insight-file-search-case","insight-file-search-word","insight-file-search-regex"};gboolean values[]={sensitive,word,escaped};
    for(guint i=0;i<3;i++) { GtkWidget *widget=named(main_widgets.window,names[i]);if(widget) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),values[i]); }
    GtkWidget *entry=named(main_widgets.window,"insight-file-search-query");if(entry) gtk_entry_set_text(GTK_ENTRY(entry),bar_pattern);g_free(bar_pattern);
    search_insight_set_pattern(pattern,(sensitive ? GEANY_FIND_MATCHCASE:0)|(word ? GEANY_FIND_WHOLEWORD:0),backwards);
    g_free(owned_pattern);owned_pattern=g_strdup(pattern);owned_flags=search_data.flags;
    if(incremental) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(incremental),old);
}
static gboolean find_range(ScintillaObject *sci,const gchar *pattern,gboolean sensitive,gboolean word,gint begin,gint end,QuickMatch *match)
{
    struct Sci_TextToFind ttf={{begin,end},(gchar *)pattern,{0,0}};
    gint flags=(sensitive ? SCFIND_MATCHCASE:0)|(word ? SCFIND_WHOLEWORD:0);
    gint found=SEND(sci,SCI_FINDTEXT,flags,(sptr_t)&ttf);if(found<0) return FALSE;
    match->begin=ttf.chrgText.cpMin;match->end=ttf.chrgText.cpMax;return match->end>match->begin;
}
static void go_match(GeanyDocument *doc,const QuickMatch *match,gboolean backwards)
{
    ScintillaObject *sci=doc->editor->sci;navqueue_goto_position(doc,doc,backwards ? match->begin:match->end);
    SEND(sci,SCI_SETSEL,backwards ? match->end:match->begin,backwards ? match->begin:match->end);
    SEND(sci,SCI_CHOOSECARETX,0,0);SEND(sci,SCI_ENSUREVISIBLE,sci_get_line_from_position(sci,match->begin),0);SEND(sci,SCI_SCROLLCARET,0,0);gtk_widget_grab_focus(GTK_WIDGET(sci));
}
gboolean insight_selection_search_owns_pattern(void)
{
    return owned_pattern && !g_strcmp0(search_data.text,owned_pattern) && search_data.flags==owned_flags;
}
gboolean insight_selection_search_show_loaded(void)
{
    if(!document_get_current() || !insight_selection_search_owns_pattern()) return FALSE;
    GtkWidget *entry=named(main_widgets.window,"insight-file-search-query"),*row=named(main_widgets.window,"insight-editor-search-panel");
    if(!entry || !row) return FALSE;
    gtk_widget_show(row);gtk_widget_grab_focus(entry);gtk_editable_select_region(GTK_EDITABLE(entry),0,-1);return TRUE;
}
gboolean insight_selection_search_find_again(gboolean change_direction)
{
    if(!insight_selection_search_owns_pattern()) return FALSE;
    QuickSource source={0};if(!capture(&source)) return TRUE;
    gboolean backwards=change_direction ? !search_data.backwards:search_data.backwards;
    gint first=MIN(source.anchor,source.caret),last=MAX(source.anchor,source.caret);
    gboolean sensitive=!!(owned_flags&GEANY_FIND_MATCHCASE),word=!!(owned_flags&GEANY_FIND_WHOLEWORD);QuickMatch match;
    GeanyDocument *doc=document_get_current();
    gboolean found=find_range(doc->editor->sci,owned_pattern,sensitive,word,backwards ? first:last,backwards ? 0:source.length,&match);
    if(!found && option("insight-file-search-wrap",TRUE)) found=find_range(doc->editor->sci,owned_pattern,sensitive,word,backwards ? source.length:0,backwards ? last:first,&match);
    if(found) go_match(doc,&match,backwards);else ui_set_statusbar(FALSE,"%s",insight_tr("No other match for the loaded literal pattern"));
    g_free(source.text);return TRUE;
}
typedef struct { GtkWidget *dialog,*entry,*sensitive,*word,*tree,*count;GtkListStore *store;GArray *matches;QuickSource source;gchar *pattern;gboolean match_case,whole,dirty,truncated; } QuickList;
static void dirty(GtkWidget *widget,gpointer data)
{
    (void)widget;QuickList *list=data;list->dirty=TRUE;gtk_list_store_clear(list->store);g_array_set_size(list->matches,0);
    gtk_label_set_text(GTK_LABEL(list->count),insight_tr("Options changed; use Refresh"));gtk_dialog_set_response_sensitive(GTK_DIALOG(list->dialog),GTK_RESPONSE_ACCEPT,FALSE);gtk_dialog_set_response_sensitive(GTK_DIALOG(list->dialog),202,FALSE);
}
static gchar *excerpt(ScintillaObject *sci,gint position)
{
    gint line=sci_get_line_from_position(sci,position),begin=sci_get_position_from_line(sci,line),end=SEND(sci,SCI_GETLINEENDPOSITION,line,0);gint length=MIN(end-begin,160);
    gchar *text=sci_get_contents_range(sci,begin,begin+length);while(length && !g_utf8_validate(text,length,NULL)) text[--length]=0;g_strdelimit(text,"\t\r\n",' ');return text;
}
static gboolean refresh_list(QuickList *list)
{
    GeanyDocument *doc=document_get_current();if(!doc || doc->id!=list->source.document) { ui_set_statusbar(TRUE,"%s",insight_tr("Selection matches expired; reopen for the active document"));return FALSE; }
    const gchar *pattern=gtk_entry_get_text(GTK_ENTRY(list->entry));if(!pattern_valid(pattern)) { ui_set_statusbar(TRUE,"%s",insight_tr("Use a nonempty single-line UTF-8 search pattern up to 4096 bytes"));return TRUE; }
    QuickSource next={0};if(!capture(&next)) return FALSE;g_free(list->source.text);list->source=next;g_free(list->pattern);list->pattern=g_strdup(pattern);
    list->match_case=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(list->sensitive));list->whole=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(list->word));list->truncated=FALSE;
    gtk_list_store_clear(list->store);g_array_set_size(list->matches,0);ScintillaObject *sci=doc->editor->sci;QuickMatch match;gint at=0;
    while(at<next.length && find_range(sci,pattern,list->match_case,list->whole,at,next.length,&match))
    {
        if(list->matches->len==QS_ROWS) { list->truncated=TRUE;break; }
        g_array_append_val(list->matches,match);gint line=sci_get_line_from_position(sci,match.begin)+1,column=SEND(sci,SCI_GETCOLUMN,match.begin,0)+1;gchar *text=excerpt(sci,match.begin);
        GtkTreeIter iter;gtk_list_store_append(list->store,&iter);gtk_list_store_set(list->store,&iter,0,list->matches->len,1,line,2,column,3,text,-1);g_free(text);at=match.end;
    }
    gchar *label=insight_i18n_printf(list->truncated ? "Matches: %u (limit reached)":"Matches: %u",list->matches->len);gtk_label_set_text(GTK_LABEL(list->count),label);g_free(label);list->dirty=FALSE;
    GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(list->tree),path,NULL,FALSE);gtk_tree_path_free(path);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(list->dialog),GTK_RESPONSE_ACCEPT,list->matches->len>0);gtk_dialog_set_response_sensitive(GTK_DIALOG(list->dialog),202,list->matches->len>0);sync_pattern(pattern,list->match_case,list->whole,FALSE);return TRUE;
}
static void row_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer dialog) { (void)view;(void)path;(void)column;gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT); }
static void copy_list(QuickList *list)
{
    GString *text=g_string_new("Line\tColumn\tText\n");GtkTreeIter iter;gboolean next=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list->store),&iter);
    while(next) { gint line,column;gchar *excerpt=NULL;gtk_tree_model_get(GTK_TREE_MODEL(list->store),&iter,1,&line,2,&column,3,&excerpt,-1);g_string_append_printf(text,"%d\t%d\t%s\n",line,column,excerpt);g_free(excerpt);next=gtk_tree_model_iter_next(GTK_TREE_MODEL(list->store),&iter); }
    if(text->len<=1024u*1024u) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text->str,text->len);g_string_free(text,TRUE);
}
static void show_list(QuickSource *source,gchar *pattern,gboolean whole)
{
    QuickList list={0};list.source=*source;source->text=NULL;list.matches=g_array_new(FALSE,FALSE,sizeof(QuickMatch));
    list.dialog=gtk_dialog_new_with_buttons(insight_tr("Selection Matches"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Refresh"),201,insight_tr("Copy List"),202,insight_tr("Close"),GTK_RESPONSE_CLOSE,insight_tr("Go To"),GTK_RESPONSE_ACCEPT,NULL);
    g_object_ref_sink(list.dialog);gtk_widget_set_name(list.dialog,"insight-selection-matches-dialog");gtk_window_set_default_size(GTK_WINDOW(list.dialog),880,460);GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(list.dialog));
    GtkWidget *hint=gtk_label_new(insight_tr("Literal matches in the active document. Edit the pattern/options, then Refresh. Text changes require Refresh before Go To or Copy List."));gtk_label_set_line_wrap(GTK_LABEL(hint),TRUE);gtk_box_pack_start(GTK_BOX(box),hint,FALSE,FALSE,6);
    GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);list.entry=gtk_entry_new();gtk_widget_set_name(list.entry,"insight-selection-matches-pattern");gtk_entry_set_text(GTK_ENTRY(list.entry),pattern);g_object_set_data(G_OBJECT(list.entry),"insight-no-i18n",GINT_TO_POINTER(1));gtk_box_pack_start(GTK_BOX(row),list.entry,TRUE,TRUE,0);
    list.sensitive=gtk_check_button_new_with_label(insight_tr("Case sensitive"));list.word=gtk_check_button_new_with_label(insight_tr("Whole word"));gtk_widget_set_name(list.sensitive,"insight-selection-matches-case");gtk_widget_set_name(list.word,"insight-selection-matches-word");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(list.sensitive),option("insight-file-search-case",FALSE));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(list.word),whole);gtk_box_pack_start(GTK_BOX(row),list.sensitive,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),list.word,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,4);
    list.count=gtk_label_new(NULL);gtk_widget_set_name(list.count,"insight-selection-matches-count");gtk_box_pack_start(GTK_BOX(box),list.count,FALSE,FALSE,4);
    list.store=gtk_list_store_new(4,G_TYPE_UINT,G_TYPE_INT,G_TYPE_INT,G_TYPE_STRING);list.tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(list.store));gtk_widget_set_name(list.tree,"insight-selection-matches-list");g_object_set_data(G_OBJECT(list.tree),"insight-no-i18n",GINT_TO_POINTER(1));const gchar *titles[]={"Match","Line","Column","Text preview"};
    for(guint i=0;i<4;i++) { GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(insight_tr(titles[i]),gtk_cell_renderer_text_new(),"text",i,NULL);gtk_tree_view_column_set_resizable(column,TRUE);gtk_tree_view_append_column(GTK_TREE_VIEW(list.tree),column); }
    GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),list.tree);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,4);g_signal_connect(list.tree,"row-activated",G_CALLBACK(row_activated),list.dialog);
    g_signal_connect(list.entry,"changed",G_CALLBACK(dirty),&list);g_signal_connect(list.sensitive,"toggled",G_CALLBACK(dirty),&list);g_signal_connect(list.word,"toggled",G_CALLBACK(dirty),&list);gboolean ready=refresh_list(&list),go=FALSE;QuickMatch selected={0};
    while(ready)
    {
        gtk_widget_show_all(list.dialog);gint response=gtk_dialog_run(GTK_DIALOG(list.dialog));
        if(response==201) { ready=refresh_list(&list);continue; }
        if(response!=202 && response!=GTK_RESPONSE_ACCEPT) break;
        if(list.dirty || !valid(&list.source)) { ui_set_statusbar(TRUE,"%s",insight_tr("Selection matches are stale; use Refresh"));continue; }
        if(response==202) { copy_list(&list);continue; }
        GtkTreeModel *model=NULL;GtkTreeIter iter;guint index;
        if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(list.tree)),&model,&iter)) continue;
        gtk_tree_model_get(model,&iter,0,&index,-1);if(!index || index>list.matches->len) continue;selected=g_array_index(list.matches,QuickMatch,index-1);go=TRUE;break;
    }
    gtk_widget_destroy(list.dialog);g_object_unref(list.dialog);g_object_unref(list.store);
    if(go && valid(&list.source)) go_match(document_get_current(),&selected,FALSE);
    g_array_unref(list.matches);g_free(list.source.text);g_free(list.pattern);
}
void insight_selection_search_command(guint action)
{
    if(action>3) return;QuickSource source={0};if(!capture(&source)) return;gchar *pattern=NULL;gint first=0,last=0;
    if(action==0 || action==3) pattern=selection_pattern(&source);
    gboolean word=pattern==NULL;
    if(action && !pattern && (action!=3 || source.anchor==source.caret)) pattern=word_pattern(&source,&first,&last);
    if(!pattern) { ui_set_statusbar(TRUE,"%s",insight_tr("Select single-line text or place the caret inside a word; maximum 4096 bytes"));g_free(source.text);return; }
    if(action==3) { show_list(&source,pattern,word); }
    else
    {
        gboolean sensitive=option("insight-file-search-case",FALSE),backwards=action==2;
        sync_pattern(pattern,sensitive,word,backwards);
        if(action)
        {
            GeanyDocument *doc=document_get_current();QuickMatch match;gboolean found=find_range(doc->editor->sci,pattern,sensitive,TRUE,backwards ? first:last,backwards ? 0:source.length,&match);
            if(!found && option("insight-file-search-wrap",TRUE)) found=find_range(doc->editor->sci,pattern,sensitive,TRUE,backwards ? source.length:0,backwards ? last:first,&match);
            if(found) go_match(doc,&match,backwards);else ui_set_statusbar(FALSE,"%s",insight_tr("No other occurrence of this word"));
        }
        else ui_set_statusbar(FALSE,"%s",insight_tr("Selection loaded as the literal search pattern"));
    }
    g_free(pattern);g_free(source.text);
}
static void command(GtkMenuItem *item,gpointer data) { (void)item;insight_selection_search_command(GPOINTER_TO_UINT(data)); }
static void menu_show(GtkWidget *menu,gpointer data)
{
    (void)data;gboolean ready=normal(document_get_current());GList *children=gtk_container_get_children(GTK_CONTAINER(menu));for(GList *p=children;p;p=p->next) if(g_object_get_data(G_OBJECT(p->data),"insight-selection-search-command")) gtk_widget_set_sensitive(p->data,ready);g_list_free(children);
}
void insight_selection_search_init(void)
{
    GtkWidget *search=ui_lookup_widget(main_widgets.window,"search1_menu"),*parent=gtk_menu_item_new_with_label("Selection search"),*menu=gtk_menu_new();gtk_widget_set_name(parent,"insight-selection-search-menu");gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent),menu);
    const gchar *labels[]={"Load Search String","Search Forward for Selection","Search Backward for Selection","Find All for Selection…"};const gchar *names[]={"load","forward","backward","all"};const guint keys[]={GEANY_KEYS_INSIGHT_LOADSEARCHSTRING,GEANY_KEYS_INSIGHT_SELECTIONSEARCHFORWARD,GEANY_KEYS_INSIGHT_SELECTIONSEARCHBACKWARD,GEANY_KEYS_INSIGHT_SELECTIONMATCHES};
    for(guint i=0;i<4;i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);gchar *name=g_strconcat("insight-selection-search-",names[i],NULL);gtk_widget_set_name(item,name);g_free(name);g_object_set_data(G_OBJECT(item),"insight-selection-search-command",GINT_TO_POINTER(1));keybindings_attach_menu(GEANY_KEY_GROUP_SEARCH,keys[i],item);g_signal_connect(item,"activate",G_CALLBACK(command),GUINT_TO_POINTER(i));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item); }
    GtkWidget *help=gtk_menu_item_new_with_label("Selection Search Help");g_signal_connect(help,"activate",G_CALLBACK(insight_help_show),"selection-search.md");gtk_menu_shell_append(GTK_MENU_SHELL(menu),help);g_signal_connect(menu,"show",G_CALLBACK(menu_show),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(search),parent);gtk_widget_show_all(parent);
}
