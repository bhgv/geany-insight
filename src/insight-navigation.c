/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-navigation.h"
#include "insight-command-list.h"
#include "document.h"
#include "geanyobject.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "navqueue.h"
#include "insight-i18n.h"
#include "insight-completion.h"
#include "insight-help.h"
#include "../insight/desktop/text_boundary.h"
#include "main.h"
#include <gdk/gdkkeysyms.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *extend_item;
#define MODE "insight-navigation-extend-mode"
static gboolean requested(ScintillaObject *sci)
{
    return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sci), MODE));
}
static void set_mode(ScintillaObject *sci, gboolean active)
{
    g_object_set_data(G_OBJECT(sci), MODE, GINT_TO_POINTER(active));
    SEND(sci, SCI_SETMOVEEXTENDSSELECTION, active, 0);
}
static GeanyDocument *for_sci(ScintillaObject *sci)
{
    for (guint i = 0; documents_array && i < documents_array->len; i++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, i);
        if (doc->is_valid && doc->editor->sci == sci) return doc;
    }
    return NULL;
}
static gboolean normal(ScintillaObject *sci)
{
    return sci && for_sci(sci) && SEND(sci, SCI_GETSELECTIONS, 0, 0) == 1 &&
        sci_get_selection_mode(sci) == SC_SEL_STREAM &&
        !SEND(sci, SCI_GETSELECTIONNCARETVIRTUALSPACE, 0, 0) &&
        !SEND(sci, SCI_GETSELECTIONNANCHORVIRTUALSPACE, 0, 0);
}
void insight_navigation_notify(GeanyEditor *editor, SCNotification *nt)
{
    ScintillaObject *sci = editor->sci;
    if (nt->nmhdr.code == SCN_MODIFIED &&
        (nt->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)))
        set_mode(sci, FALSE);
    else if (nt->nmhdr.code == SCN_UPDATEUI && requested(sci) &&
        editor->document == document_get_current())
    {
        if (!normal(sci)) set_mode(sci, FALSE);
        else if (!SEND(sci, SCI_GETMOVEEXTENDSSELECTION, 0, 0))
            SEND(sci, SCI_SETMOVEEXTENDSSELECTION, TRUE, 0);
    }
}
static gboolean mouse(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)event;(void)data;set_mode(SCINTILLA(widget), FALSE);return FALSE;
}
void insight_navigation_attach(ScintillaObject *sci)
{
    g_signal_connect(sci, "button-press-event", G_CALLBACK(mouse), NULL);
}
static void activated(GObject *object, GeanyDocument *doc, gpointer data)
{
    (void)object;(void)data;
    if (doc && doc->is_valid && normal(doc->editor->sci))
        SEND(doc->editor->sci, SCI_SETMOVEEXTENDSSELECTION, requested(doc->editor->sci), 0);
}
static void select_to(ScintillaObject *sci, gint anchor, gint caret, gboolean mode)
{
    gint length = sci_get_length(sci);
    SEND(sci, SCI_SETSEL, CLAMP(anchor, 0, length), CLAMP(caret, 0, length));
    set_mode(sci, mode);
    SEND(sci, SCI_CHOOSECARETX, 0, 0);
    SEND(sci, SCI_ENSUREVISIBLE, sci_get_current_line(sci), 0);
    SEND(sci, SCI_SCROLLCARET, 0, 0);
}
gboolean insight_navigation_extend_to(ScintillaObject *sci, gint position)
{
    if (!normal(sci) || !requested(sci)) return FALSE;
    select_to(sci, SEND(sci, SCI_GETANCHOR, 0, 0), position, TRUE);
    return TRUE;
}
static void move_to(ScintillaObject *sci, gint position)
{
    if (!insight_navigation_extend_to(sci, position)) select_to(sci, position, position, FALSE);
}
static void toggle(GtkCheckMenuItem *item, gpointer data)
{
    (void)data;
    GeanyDocument *doc = document_get_current();
    if (!doc || !normal(doc->editor->sci)) return;
    gboolean active = gtk_check_menu_item_get_active(item);
    set_mode(doc->editor->sci, active);
    ui_set_statusbar(FALSE, active ? "Extend Mode: ON" : "Extend Mode: OFF");
    gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
}
static void menu_show(GtkWidget *menu, gpointer data)
{
    (void)menu;(void)data;
    GeanyDocument *doc = document_get_current();
    gboolean ready = doc && normal(doc->editor->sci);
    g_signal_handlers_block_by_func(extend_item, toggle, NULL);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(extend_item),
        ready && requested(doc->editor->sci));
    gtk_widget_set_sensitive(extend_item, ready);
    g_signal_handlers_unblock_by_func(extend_item, toggle, NULL);
}
gboolean insight_navigation_key(GeanyEditor *editor, GdkEventKey *event)
{
    if (event->keyval != GDK_KEY_Escape ||
        (event->state & gtk_accelerator_get_default_mod_mask()) ||
        !requested(editor->sci)) return FALSE;
    set_mode(editor->sci, FALSE);
    ui_set_statusbar(FALSE, "Extend Mode: OFF");
    return TRUE;
}
static void blank_line(ScintillaObject *sci, gint direction)
{
    gint current = sci_get_current_line(sci), total = sci_get_line_count(sci);
    gint origin = sci_get_position_from_line(sci, current);
    guint visited = 0;
    for (gint line = current + direction; line >= 0 && line < total; line += direction)
    {
        gint start = sci_get_position_from_line(sci, line);
        gint end = SEND(sci, SCI_GETLINEENDPOSITION, line, 0);
        if (++visited > 100000 || ABS((gint64)end - origin) > 4 * 1024 * 1024 ||
            ABS((gint64)start - origin) > 4 * 1024 * 1024)
        {
            ui_set_statusbar(FALSE, "Blank line search limit reached.");
            return;
        }
        if (SEND(sci, SCI_GETLINEINDENTPOSITION, line, 0) == end)
        {
            move_to(sci, start);return;
        }
    }
}
static void command(GtkMenuItem *item, gpointer data)
{
    (void)item;
    GeanyDocument *doc = document_get_current();
    if (!doc || !normal(doc->editor->sci)) return;
    ScintillaObject *sci = doc->editor->sci;
    gint anchor = SEND(sci, SCI_GETANCHOR, 0, 0);
    gboolean mode = requested(sci);
    switch (GPOINTER_TO_INT(data))
    {
        case 0: if (sci_has_selection(sci)) select_to(sci, sci_get_selection_start(sci), sci_get_selection_start(sci), FALSE);break;
        case 1: if (sci_has_selection(sci)) select_to(sci, sci_get_selection_end(sci), sci_get_selection_end(sci), FALSE);break;
        case 2: blank_line(sci, -1);break;
        case 3: blank_line(sci, 1);break;
        case 4: editor_select_word(doc->editor);set_mode(sci, mode);break;
        case 5:
        {
            gint line = sci_get_current_line(sci), total = sci_get_line_count(sci);
            select_to(sci, sci_get_position_from_line(sci, line),
                line + 1 < total ? sci_get_position_from_line(sci, line + 1) : sci_get_length(sci), mode);
            break;
        }
        case 6: select_to(sci, anchor, SEND(sci, SCI_GETLINEENDPOSITION, sci_get_current_line(sci), 0), mode);break;
        case 7: select_to(sci, anchor, 0, mode);break;
        case 8: select_to(sci, anchor, sci_get_length(sci), mode);break;
        case 9: move_to(sci, 0);break;
        case 10: move_to(sci, sci_get_length(sci));break;
        default: return;
    }
    gtk_widget_grab_focus(GTK_WIDGET(sci));
}
/* Native word-part commands preserve Scintilla's identifier/UTF-8 rules;
 * smart line edges use SI's additional line-to-file step. */
static void text_boundary_command(GtkMenuItem *item,gpointer data)
{
 (void)item;GeanyDocument *doc=document_get_current();if(!doc || !normal(doc->editor->sci)) return;
 ScintillaObject *sci=doc->editor->sci;gint action=GPOINTER_TO_INT(data);gboolean mode=requested(sci);
 if(action>=5) {
  gboolean left=action==5 || action==7,extend=mode || action>=7;
  SEND(sci,left ? (extend ? SCI_WORDPARTLEFTEXTEND : SCI_WORDPARTLEFT) : (extend ? SCI_WORDPARTRIGHTEXTEND : SCI_WORDPARTRIGHT),0,0);set_mode(sci,mode);
 } else {
  gint length=sci_get_length(sci);if(length>8*1024*1024) { ui_set_statusbar(TRUE,"Text boundary commands are limited to 8 MiB");return; }
  gchar *text=sci_get_contents(sci,-1);gint target=0,position=sci_get_current_position(sci);GError *error=NULL;
  gint operation=action==0 || action==3 ? 0 : action==1 || action==4 ? 1 : 2;
  gboolean found=insight_text_boundary(text,length,position,operation,&target,&error);g_free(text);
  if(!found) { ui_set_statusbar(TRUE,"%s",insight_tr(error->message));g_clear_error(&error);return; }
  if(action>=2) select_to(sci,SEND(sci,SCI_GETANCHOR,0,0),target,mode);else move_to(sci,target);
 }
 gtk_widget_grab_focus(GTK_WIDGET(sci));
}
static void text_boundary_menu_show(GtkWidget *menu,gpointer data)
{
 (void)data;GeanyDocument *doc=document_get_current();gboolean ready=doc && normal(doc->editor->sci);GList *children=gtk_container_get_children(GTK_CONTAINER(menu));
 for(GList *p=children;p;p=p->next) if(g_object_get_data(G_OBJECT(p->data),"insight-boundary-command")) gtk_widget_set_sensitive(p->data,ready);
 g_list_free(children);
}
static void text_boundary_menu(GtkWidget *menu)
{
 GtkWidget *parent=gtk_menu_item_new_with_label("Text boundaries"),*sub=gtk_menu_new();gtk_widget_set_name(parent,"insight-text-boundaries");gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent),sub);
 const gchar *labels[]={"Smart Beginning of Line","Smart End of Line","Select Sentence","Select To Smart Beginning of Line","Select To Smart End of Line","Word Fragment Left","Word Fragment Right","Select Word Fragment Left","Select Word Fragment Right"};
 const gchar *ids[]={"smart-line-start","smart-line-end","select-sentence","select-smart-line-start","select-smart-line-end","fragment-left","fragment-right","select-fragment-left","select-fragment-right"};
 for(guint i=0;i<G_N_ELEMENTS(labels);i++) {
  if(i==2 || i==5) gtk_menu_shell_append(GTK_MENU_SHELL(sub),gtk_separator_menu_item_new());
  GtkWidget *entry=gtk_menu_item_new_with_label(labels[i]);gchar *name=g_strconcat("insight-nav-",ids[i],NULL);gtk_widget_set_name(entry,name);g_free(name);g_object_set_data(G_OBJECT(entry),"insight-boundary-command",GINT_TO_POINTER(1));g_signal_connect(entry,"activate",G_CALLBACK(text_boundary_command),GINT_TO_POINTER(i));gtk_menu_shell_append(GTK_MENU_SHELL(sub),entry);
 }
 GtkWidget *help=gtk_menu_item_new_with_label("Text Boundaries Help");gtk_widget_set_name(help,"insight-text-boundaries-help");g_signal_connect(help,"activate",G_CALLBACK(insight_help_show),"text-boundaries.md");gtk_menu_shell_append(GTK_MENU_SHELL(sub),gtk_separator_menu_item_new());gtk_menu_shell_append(GTK_MENU_SHELL(sub),help);
 g_signal_connect(sub,"show",G_CALLBACK(text_boundary_menu_show),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),parent);gtk_widget_show_all(parent);
}

static void unit_selection_command(GtkMenuItem *item, gpointer data)
{
    (void)item;
    GeanyDocument *doc = document_get_current();
    if (!doc || !normal(doc->editor->sci)) return;
    static const guint messages[] = { SCI_CHARLEFTEXTEND, SCI_CHARRIGHTEXTEND,
        SCI_WORDLEFTEXTEND, SCI_WORDRIGHTENDEXTEND, SCI_LINEUPEXTEND, SCI_LINEDOWNEXTEND };
    guint action = GPOINTER_TO_UINT(data);
    if (action >= G_N_ELEMENTS(messages)) return;
    ScintillaObject *sci = doc->editor->sci;
    gboolean mode = requested(sci);
    SEND(sci, messages[action], 0, 0);
    set_mode(sci, mode);
    gtk_widget_grab_focus(GTK_WIDGET(sci));
}
static void unit_selection_menu(GtkWidget *menu)
{
    GtkWidget *parent = gtk_menu_item_new_with_label("Extend selection");
    GtkWidget *sub = gtk_menu_new();
    gtk_widget_set_name(parent, "insight-unit-selection");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), sub);
    const gchar *labels[] = {"Select Char Left", "Select Char Right", "Select Word Left", "Select Word Right", "Select Line Up", "Select Line Down"};
    const gchar *ids[] = {"select-char-left", "select-char-right", "select-word-left", "select-word-right", "select-line-up", "select-line-down"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *entry = gtk_menu_item_new_with_label(labels[i]);
        gchar *name = g_strconcat("insight-nav-", ids[i], NULL);
        gtk_widget_set_name(entry, name);g_free(name);
        g_object_set_data(G_OBJECT(entry), "insight-boundary-command", GINT_TO_POINTER(1));
        g_signal_connect(entry, "activate", G_CALLBACK(unit_selection_command), GUINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(sub), entry);
    }
    g_signal_connect(sub, "show", G_CALLBACK(text_boundary_menu_show), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), parent);gtk_widget_show_all(parent);
}

/* Viewport-only operations deliberately allow rectangular/multiple selections.
 * SCI_LINESCROLL operates on display rows, including wrapped/folded text,
 * without moving any selection or revealing the off-screen caret. */
static void viewport_command(GtkMenuItem *item, gpointer data)
{
    (void)item;
    GeanyDocument *doc = document_get_current();
    if (!doc || !doc->is_valid) return;
    ScintillaObject *sci = doc->editor->sci;
    gint action = GPOINTER_TO_INT(data);
    if (action >= 6)
    {
        if (!normal(sci)) return;
        gint display = SEND(sci, SCI_GETFIRSTVISIBLELINE, 0, 0);
        if (action == 7) display += MAX(1, SEND(sci, SCI_LINESONSCREEN, 0, 0)) - 1;
        gint last = sci_get_line_count(sci) - 1;
        gint final_display = SEND(sci, SCI_VISIBLEFROMDOCLINE, last, 0) +
            MAX(1, SEND(sci, SCI_WRAPCOUNT, last, 0)) - 1;
        gint line = SEND(sci, SCI_DOCLINEFROMVISIBLE, MIN(display, final_display), 0);
        /* Wrapped rows share a logical line. Use its beginning consistently;
         * never interpret a display-row number as a document-line number. */
        move_to(sci, sci_get_position_from_line(sci, CLAMP(line, 0, last)));
    }
    else if (action < 4)
    {
        gint rows = action < 2 ? 1 : MAX(1, SEND(sci, SCI_LINESONSCREEN, 0, 0) / 2);
        SEND(sci, SCI_LINESCROLL, 0, action % 2 ? rows : -rows);
    }
    else
    {
        if (SEND(sci, SCI_GETWRAPMODE, 0, 0) != SC_WRAP_NONE) return;
        gint64 step = (gint64)MAX(1, SEND(sci, SCI_GETTABWIDTH, 0, 0)) *
            MAX(1, SEND(sci, SCI_TEXTWIDTH, STYLE_DEFAULT, (sptr_t)" "));
        gint64 offset = SEND(sci, SCI_GETXOFFSET, 0, 0);
        SEND(sci, SCI_SETXOFFSET, CLAMP(offset + (action == 4 ? -step : step), 0, G_MAXINT), 0);
    }
    gtk_widget_grab_focus(GTK_WIDGET(sci));
}
static void viewport_menu_show(GtkWidget *menu, gpointer data)
{
    (void)data;
    GeanyDocument *doc = document_get_current();
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *p = children; p; p = p->next)
        if (g_object_get_data(G_OBJECT(p->data), "insight-viewport-command"))
            gtk_widget_set_sensitive(p->data, doc && doc->is_valid &&
                (!g_object_get_data(G_OBJECT(p->data), "insight-viewport-edge") || normal(doc->editor->sci)));
    g_list_free(children);
}
static void viewport_menu(GtkWidget *menu)
{
    GtkWidget *parent = gtk_menu_item_new_with_label("Window scrolling");
    GtkWidget *sub = gtk_menu_new();
    gtk_widget_set_name(parent, "insight-window-scrolling");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), sub);
    const gchar *labels[] = {"Scroll Line Up", "Scroll Line Down", "Scroll Half Page Up", "Scroll Half Page Down", "Scroll Left", "Scroll Right", "Top of Window", "Bottom of Window"};
    const gchar *ids[] = {"scroll-line-up", "scroll-line-down", "scroll-half-up", "scroll-half-down", "scroll-left", "scroll-right", "window-top", "window-bottom"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        if (i == 6) gtk_menu_shell_append(GTK_MENU_SHELL(sub), gtk_separator_menu_item_new());
        GtkWidget *entry = gtk_menu_item_new_with_label(labels[i]);
        gchar *name = g_strconcat("insight-nav-", ids[i], NULL);
        gtk_widget_set_name(entry, name);g_free(name);
        if (i >= 6) g_object_set_data(G_OBJECT(entry), "insight-viewport-edge", GINT_TO_POINTER(1));
        g_object_set_data(G_OBJECT(entry), "insight-viewport-command", GINT_TO_POINTER(1));
        g_signal_connect(entry, "activate", G_CALLBACK(viewport_command), GINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(sub), entry);
    }
    GtkWidget *help = gtk_menu_item_new_with_label("Window Scrolling Help");
    g_signal_connect(help, "activate", G_CALLBACK(insight_help_show), "window-scrolling.md");
    gtk_menu_shell_append(GTK_MENU_SHELL(sub), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(sub), help);
    g_signal_connect(sub, "show", G_CALLBACK(viewport_menu_show), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), parent);gtk_widget_show_all(parent);
}

typedef struct {
 guint document,source;
 ScintillaObject *sci;
 GtkWidget *dialog,*progress;
 gboolean redo;
 gint start,total,current;
} BulkUndo;
static gboolean bulk_undo_step(gpointer data)
{
 BulkUndo *job=data;GeanyDocument *doc=document_find_by_id(job->document);
 if(!doc || doc!=document_get_current() || doc->editor->sci!=job->sci || doc->readonly ||
    SEND(job->sci,SCI_GETREADONLY,0,0) || SEND(job->sci,SCI_GETUNDOCURRENT,0,0)!=job->current ||
    SEND(job->sci,SCI_GETUNDOACTIONS,0,0)!=job->total || SEND(job->sci,SCI_GETUNDOSEQUENCE,0,0)) {
  ui_set_statusbar(TRUE,"Bulk Undo/Redo stopped because the document or history changed");
  job->source=0;gtk_dialog_response(GTK_DIALOG(job->dialog),GTK_RESPONSE_REJECT);return G_SOURCE_REMOVE;
 }
 for(guint i=0;i<32;i++) {
  doc=document_find_by_id(job->document);
  if(!doc || doc!=document_get_current() || doc->editor->sci!=job->sci || doc->readonly ||
     SEND(job->sci,SCI_GETREADONLY,0,0) || SEND(job->sci,SCI_GETUNDOCURRENT,0,0)!=job->current ||
     SEND(job->sci,SCI_GETUNDOACTIONS,0,0)!=job->total || SEND(job->sci,SCI_GETUNDOSEQUENCE,0,0)) {
   ui_set_statusbar(TRUE,"Bulk Undo/Redo stopped because the document or history changed");
   job->source=0;gtk_dialog_response(GTK_DIALOG(job->dialog),GTK_RESPONSE_REJECT);return G_SOURCE_REMOVE;
  }
  if(!SEND(job->sci,job->redo ? SCI_CANREDO : SCI_CANUNDO,0,0)) {
   job->source=0;gtk_dialog_response(GTK_DIALOG(job->dialog),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
  }
  gint before=SEND(job->sci,SCI_GETUNDOCURRENT,0,0);
  SEND(job->sci,job->redo ? SCI_REDO : SCI_UNDO,0,0);
  doc=document_find_by_id(job->document);
  if(!doc || doc->editor->sci!=job->sci) { job->source=0;gtk_dialog_response(GTK_DIALOG(job->dialog),GTK_RESPONSE_REJECT);return G_SOURCE_REMOVE; }
  job->current=SEND(job->sci,SCI_GETUNDOCURRENT,0,0);
  if(job->current==before || (job->redo ? job->current<before : job->current>before)) {
   ui_set_statusbar(TRUE,"Bulk Undo/Redo stopped because the document or history changed");
   job->source=0;gtk_dialog_response(GTK_DIALOG(job->dialog),GTK_RESPONSE_REJECT);return G_SOURCE_REMOVE;
  }
 }
 gint distance=job->redo ? job->total-job->start : job->start;
 gdouble fraction=distance ? (gdouble)ABS(job->current-job->start)/distance : 1;
 gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(job->progress),CLAMP(fraction,0,1));return G_SOURCE_CONTINUE;
}
static void bulk_undo_command(GtkMenuItem *item,gpointer data)
{
 (void)item;GeanyDocument *doc=document_get_current();if(!doc || doc->readonly) return;
 ScintillaObject *sci=doc->editor->sci;gboolean redo=GPOINTER_TO_INT(data);
 if(SEND(sci,SCI_GETREADONLY,0,0) || SEND(sci,SCI_GETUNDOSEQUENCE,0,0) ||
    !SEND(sci,redo ? SCI_CANREDO : SCI_CANUNDO,0,0)) return;
 insight_completion_cancel(doc->editor);SEND(sci,SCI_AUTOCCANCEL,0,0);
 /* Resolve the document id before using sci; do not prolong a destroyed
  * Scintilla widget beyond its scrollbar lifetime. */
 BulkUndo job={0};job.document=doc->id;job.sci=sci;job.redo=redo;
 job.start=job.current=SEND(sci,SCI_GETUNDOCURRENT,0,0);job.total=SEND(sci,SCI_GETUNDOACTIONS,0,0);
 job.dialog=gtk_dialog_new_with_buttons(insight_tr(redo ? "Redo All" : "Undo All"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,NULL);
 g_object_ref_sink(job.dialog);gtk_widget_set_name(job.dialog,"insight-bulk-undo");gtk_window_set_default_size(GTK_WINDOW(job.dialog),380,-1);
 GtkWidget *label=gtk_label_new(insight_tr("Processing retained edit history. Cancel keeps completed steps."));gtk_label_set_line_wrap(GTK_LABEL(label),TRUE);
 job.progress=gtk_progress_bar_new();gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(job.progress),TRUE);gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(job.dialog))),label,FALSE,FALSE,8);gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(job.dialog))),job.progress,FALSE,FALSE,8);
 gtk_widget_show_all(job.dialog);job.source=g_idle_add(bulk_undo_step,&job);gtk_dialog_run(GTK_DIALOG(job.dialog));
 if(job.source) g_source_remove(job.source);
 gtk_widget_destroy(job.dialog);g_object_unref(job.dialog);
 GeanyDocument *remaining=document_find_by_id(job.document);
 if(remaining && remaining==document_get_current()) gtk_widget_grab_focus(GTK_WIDGET(sci));
}
static void bulk_undo_menu_show(GtkWidget *menu,gpointer data)
{
 (void)data;GeanyDocument *doc=document_get_current();gboolean ready=doc && !doc->readonly && !SEND(doc->editor->sci,SCI_GETREADONLY,0,0) && !SEND(doc->editor->sci,SCI_GETUNDOSEQUENCE,0,0);
 for(guint i=0;i<2;i++) { GtkWidget *entry=g_object_get_data(G_OBJECT(menu),i ? "redo-all" : "undo-all");gtk_widget_set_sensitive(entry,ready && SEND(doc->editor->sci,i ? SCI_CANREDO : SCI_CANUNDO,0,0)); }
}
static void show_file_status(GtkMenuItem *item,gpointer data)
{
 (void)item;(void)data;GeanyDocument *doc=document_get_current();if(!doc) return;ScintillaObject *sci=doc->editor->sci;
 gchar *name=doc->file_name ? g_path_get_basename(doc->file_name) : g_strdup(insight_tr("Untitled"));
 gchar *lines=g_strdup_printf("%" G_GINT64_FORMAT,(gint64)SEND(sci,SCI_GETLINECOUNT,0,0)),*bytes=g_strdup_printf("%" G_GINT64_FORMAT,(gint64)SEND(sci,SCI_GETLENGTH,0,0));
 gchar *status=insight_i18n_printf("%s — lines: %s, bytes: %s — %s — %s",name,lines,bytes,insight_tr(doc->changed || SEND(sci,SCI_GETMODIFY,0,0) ? "Modified" : "Saved"),insight_tr(doc->readonly || SEND(sci,SCI_GETREADONLY,0,0) ? "Read-only" : "Writable"));
 ui_set_statusbar(TRUE,"%s",status);g_free(status);g_free(bytes);g_free(lines);g_free(name);
}
static void file_status_menu_show(GtkWidget *menu,gpointer data) { (void)menu;gtk_widget_set_sensitive(GTK_WIDGET(data),document_get_current()!=NULL); }

static void show_history(GtkMenuItem *item,gpointer data) { (void)item;(void)data;navqueue_show_history(); }
void insight_navigation_menu(GtkWidget *menu)
{
    insight_command_list_menu();
    GtkWidget *file_menu=ui_lookup_widget(main_widgets.window,"file1_menu"),*status=gtk_menu_item_new_with_label("Show File Status");gtk_widget_set_name(status,"insight-show-file-status");g_signal_connect(status,"activate",G_CALLBACK(show_file_status),NULL);GList *file_children=gtk_container_get_children(GTK_CONTAINER(file_menu));gint status_position=g_list_index(file_children,ui_lookup_widget(main_widgets.window,"properties1"));g_list_free(file_children);gtk_menu_shell_insert(GTK_MENU_SHELL(file_menu),status,status_position);g_signal_connect(file_menu,"show",G_CALLBACK(file_status_menu_show),status);gtk_widget_show(status);
    GtkWidget *parent = gtk_menu_item_new_with_label("Selection and navigation");
    gtk_widget_set_name(parent, "insight-selection-navigation");
    GtkWidget *sub = gtk_menu_new();gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), sub);
    extend_item = gtk_check_menu_item_new_with_label("Toggle Extend Mode");
    gtk_widget_set_name(extend_item, "insight-extend-mode");
    g_signal_connect(extend_item, "toggled", G_CALLBACK(toggle), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(sub), extend_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(sub), gtk_separator_menu_item_new());
    const gchar *labels[] = {"Beginning of Selection", "End of Selection", "Blank Line Up", "Blank Line Down", "Select Word", "Select Line", "Select To End Of Line", "Select To Top Of File", "Select To End Of File", "Top of File", "Bottom of File"};
    const gchar *ids[] = {"selection-begin", "selection-end", "blank-line-up", "blank-line-down", "select-word", "select-line", "select-line-end", "select-file-start", "select-file-end", "file-start", "file-end"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        if (i == 4 || i == 9) gtk_menu_shell_append(GTK_MENU_SHELL(sub), gtk_separator_menu_item_new());
        GtkWidget *entry = gtk_menu_item_new_with_label(labels[i]);
        gchar *name = g_strconcat("insight-nav-", ids[i], NULL);gtk_widget_set_name(entry, name);g_free(name);
        g_signal_connect(entry, "activate", G_CALLBACK(command), GINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(sub), entry);
    }
    text_boundary_menu(sub);
    viewport_menu(sub);
    unit_selection_menu(sub);
    GtkWidget *history=gtk_menu_item_new_with_label("Selection History…");gtk_widget_set_name(history,"insight-selection-history-command");g_signal_connect(history,"activate",G_CALLBACK(show_history),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(sub),gtk_separator_menu_item_new());gtk_menu_shell_append(GTK_MENU_SHELL(sub),history);
    g_signal_connect(sub, "show", G_CALLBACK(menu_show), NULL);
    g_signal_connect_object(geany_object, "document-activate", G_CALLBACK(activated), parent, G_CONNECT_AFTER);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), parent);gtk_widget_show_all(parent);
    for(guint i=0;i<2;i++) {
        GtkWidget *entry=gtk_menu_item_new_with_label(i ? "Redo All" : "Undo All");
        gtk_widget_set_name(entry,i ? "insight-redo-all" : "insight-undo-all");
        g_object_set_data(G_OBJECT(menu),i ? "redo-all" : "undo-all",entry);
        g_signal_connect(entry,"activate",G_CALLBACK(bulk_undo_command),GINT_TO_POINTER(i));gtk_menu_shell_append(GTK_MENU_SHELL(menu),entry);gtk_widget_show(entry);
    }
    g_signal_connect(menu,"show",G_CALLBACK(bulk_undo_menu_show),NULL);
}
