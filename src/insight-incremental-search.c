/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-incremental-search.h"
#include "insight-selection-search.h"
#include "document.h"
#include "geanyobject.h"
#include "keybindings.h"
#include "keybindingsprivate.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "insight-help.h"
#include "insight-i18n.h"
#include <gdk/gdkkeysyms.h>
#include <string.h>

#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define INPUT_LIMIT 4096u
#define SOURCE_LIMIT (8u*1024u*1024u)
static GtkWidget *strip,*entry,*status,*wrap;
static gboolean active,backwards,changing;
static guint source_id;
static gint original_anchor,original_caret,original_line,original_x,origin;
static gchar *previous_pattern,*successful_pattern;
static gboolean successful_case,successful_direction;

static GeanyDocument *source(void)
{
 GeanyDocument *doc=document_find_by_id(source_id);
 return DOC_VALID(doc) && doc==document_get_current() ? doc:NULL;
}
static gboolean normal(GeanyDocument *doc)
{
 if(!DOC_VALID(doc)) return FALSE;
 ScintillaObject *sci=doc->editor->sci;
 return SEND(sci,SCI_GETSELECTIONS,0,0)==1 && sci_get_selection_mode(sci)==SC_SEL_STREAM &&
  !SEND(sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) && !SEND(sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0);
}
static gboolean sensitive(const gchar *pattern)
{
 for(const gchar *p=pattern;*p;p=g_utf8_next_char(p)) if(g_unichar_isupper(g_utf8_get_char(p))) return TRUE;
 return FALSE;
}
static void finish(gboolean restore,gboolean focus)
{
 if(!active) return;
 GeanyDocument *doc=source();active=FALSE;
 if(successful_pattern) { g_free(previous_pattern);previous_pattern=g_strdup(successful_pattern); }
 if(doc && restore) {
  ScintillaObject *sci=doc->editor->sci;
  SEND(sci,SCI_SETSEL,original_anchor,original_caret);
  SEND(sci,SCI_CHOOSECARETX,0,0);
  SEND(sci,SCI_SETFIRSTVISIBLELINE,SEND(sci,SCI_VISIBLEFROMDOCLINE,original_line,0),0);
  SEND(sci,SCI_SETXOFFSET,original_x,0);
 } else if(doc && successful_pattern) {
  insight_selection_search_set_literal(successful_pattern,successful_case,successful_direction);
 }
 gtk_widget_hide(strip);
 if(doc && focus) gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
 g_clear_pointer(&successful_pattern,g_free);
}
static gboolean find_range(ScintillaObject *sci,const gchar *pattern,gint from,gint to,gboolean match_case,gint *begin,gint *end)
{
 struct Sci_TextToFind query={{from,to},(gchar *)pattern,{0,0}};
 if(SEND(sci,SCI_FINDTEXT,match_case ? SCFIND_MATCHCASE:0,(sptr_t)&query)<0) return FALSE;
 *begin=query.chrgText.cpMin;*end=query.chrgText.cpMax;
 return *end>*begin;
}
static void find_pattern(gboolean again)
{
 GeanyDocument *doc=source();if(!active || !normal(doc)) { finish(FALSE,FALSE);return; }
 const gchar *pattern=gtk_entry_get_text(GTK_ENTRY(entry));
 if(!*pattern) { gtk_label_set_text(GTK_LABEL(status),insight_tr("Type to search • Enter accepts • Esc restores"));return; }
 if(strlen(pattern)>INPUT_LIMIT || !g_utf8_validate(pattern,-1,NULL) || strchr(pattern,'\r') || strchr(pattern,'\n')) {
  gtk_label_set_text(GTK_LABEL(status),insight_tr("Use a single literal line up to 4096 bytes"));return;
 }
 ScintillaObject *sci=doc->editor->sci;gint length=sci_get_length(sci),begin=0,end=0;
 gboolean match_case=sensitive(pattern),wrapped=FALSE;
 gint from=origin;
 if(again) from=SEND(sci,backwards ? SCI_GETSELECTIONSTART:SCI_GETSELECTIONEND,0,0);
 gboolean found=find_range(sci,pattern,from,backwards ? 0:length,match_case,&begin,&end);
 if(!found && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wrap))) {
  wrapped=TRUE;found=find_range(sci,pattern,backwards ? length:0,from,match_case,&begin,&end);
 }
 if(!found) { gtk_label_set_text(GTK_LABEL(status),insight_tr("No match — previous selection retained"));return; }
 SEND(sci,SCI_SETSEL,backwards ? end:begin,backwards ? begin:end);
 SEND(sci,SCI_CHOOSECARETX,0,0);SEND(sci,SCI_ENSUREVISIBLE,sci_get_line_from_position(sci,begin),0);SEND(sci,SCI_SCROLLCARET,0,0);
 g_free(successful_pattern);successful_pattern=g_strdup(pattern);successful_case=match_case;successful_direction=backwards;
 gchar *message=g_strdup_printf("%s • %s%s",insight_tr(backwards ? "Backward":"Forward"),insight_tr(match_case ? "Match case":"Ignore case"),wrapped ? insight_tr(" • wrapped"):"");gtk_label_set_text(GTK_LABEL(status),message);g_free(message);
}
static void pattern_changed(GtkEditable *editable,gpointer data)
{
 (void)editable;(void)data;if(active && !changing) find_pattern(FALSE);
}
static void repeat(gboolean direction)
{
 backwards=direction;
 if(!*gtk_entry_get_text(GTK_ENTRY(entry)) && previous_pattern) {
  changing=TRUE;gtk_entry_set_text(GTK_ENTRY(entry),previous_pattern);gtk_editable_set_position(GTK_EDITABLE(entry),-1);changing=FALSE;find_pattern(FALSE);
 } else find_pattern(TRUE);
}
void insight_incremental_search_command(gboolean direction)
{
 if(!strip) return;
 if(active) { repeat(direction);gtk_widget_grab_focus(entry);return; }
 GeanyDocument *doc=document_get_current();if(!normal(doc)) return;
 ScintillaObject *sci=doc->editor->sci;gint length=sci_get_length(sci);
 if(length<0 || (guint)length>SOURCE_LIMIT) { ui_set_statusbar(TRUE,"Incremental search supports source buffers up to 8 MiB");return; }
 gchar *text=sci_get_contents(sci,-1);gboolean valid=text && strlen(text)==(gsize)length && g_utf8_validate(text,length,NULL);g_free(text);
 if(!valid) { ui_set_statusbar(TRUE,"Incremental search requires complete UTF-8 without NUL");return; }
 source_id=doc->id;original_anchor=SEND(sci,SCI_GETANCHOR,0,0);original_caret=sci_get_current_position(sci);origin=original_caret;
 original_line=SEND(sci,SCI_DOCLINEFROMVISIBLE,SEND(sci,SCI_GETFIRSTVISIBLELINE,0,0),0);original_x=SEND(sci,SCI_GETXOFFSET,0,0);
 backwards=direction;active=TRUE;changing=TRUE;gtk_entry_set_text(GTK_ENTRY(entry),"");changing=FALSE;
 gtk_label_set_text(GTK_LABEL(status),insight_tr("Type to search • Enter accepts • Esc restores"));
 gtk_widget_show(strip);gtk_widget_grab_focus(entry);
}
static gboolean own_key(GdkEventKey *event)
{
 const guint ids[]={GEANY_KEYS_INSIGHT_INCREMENTAL,GEANY_KEYS_INSIGHT_INCREMENTALBACK};
 for(guint n=0;n<G_N_ELEMENTS(ids);n++) { GeanyKeyBinding *kb=keybindings_get_item(keybindings_get_core_group(GEANY_KEY_GROUP_SEARCH),ids[n]);if(kb && kb->key && keybindings_check_event(event,kb)) return TRUE; }
 return FALSE;
}
gboolean insight_incremental_search_key(GdkEventKey *event)
{
 if(!active || !entry || !gtk_widget_is_focus(entry)) return FALSE;
 if(own_key(event)) return FALSE;
 guint state=keybindings_get_modifiers(event->state),key=event->keyval;
 if(key==GDK_KEY_Escape) { finish(TRUE,TRUE);return TRUE; }
 if(key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) { finish(FALSE,TRUE);return TRUE; }
 /* Keep normal text/IME input and entry clipboard/edit shortcuts in GtkEntry.
  * Command keys first accept the search and restore native source focus. */
 if((state==GEANY_PRIMARY_MOD_MASK && (key==GDK_KEY_a || key==GDK_KEY_c || key==GDK_KEY_v || key==GDK_KEY_x || key==GDK_KEY_BackSpace)) || key==GDK_KEY_BackSpace) return FALSE;
 /* AltGr belongs to the input method, including layouts reporting Ctrl+Alt. */
 if(event->state&GDK_MOD5_MASK) return FALSE;
 gboolean mapped=FALSE;
 for(guint n=0;keybinding_groups && n<keybinding_groups->len && !mapped;n++) {
  GeanyKeyGroup *group=g_ptr_array_index(keybinding_groups,n);
  for(guint k=0;k<group->key_items->len;k++) { GeanyKeyBinding *binding=g_ptr_array_index(group->key_items,k);if(binding->key && keybindings_check_event(event,binding)) { mapped=TRUE;break; } }
 }
 if(mapped || (state&(GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SUPER_MASK|GDK_META_MASK)) ||
    (key>=GDK_KEY_F1 && key<=GDK_KEY_F35) || key==GDK_KEY_Left || key==GDK_KEY_Right || key==GDK_KEY_Up || key==GDK_KEY_Down || key==GDK_KEY_Home || key==GDK_KEY_End || key==GDK_KEY_Page_Up || key==GDK_KEY_Page_Down || key==GDK_KEY_Tab || key==GDK_KEY_ISO_Left_Tab || key==GDK_KEY_Delete) finish(FALSE,TRUE);
 return FALSE;
}
void insight_incremental_search_notify(GeanyEditor *editor,SCNotification *notification)
{
 if(active && editor->document->id==source_id && notification->nmhdr.code==SCN_MODIFIED && (notification->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) {
  /* The original positions are stale now: finish without restoring or changing
   * shared search state from inside an edit notification. */
  active=FALSE;gtk_widget_hide(strip);g_clear_pointer(&successful_pattern,g_free);
 }
}
static void document_changed(GObject *object,GeanyDocument *doc,gpointer data)
{
 (void)object;(void)data;if(active && (!doc || doc->id!=source_id)) finish(FALSE,FALSE);
}
static void document_closed(GObject *object,GeanyDocument *doc,gpointer data)
{
 (void)object;(void)data;if(active && doc->id==source_id) { active=FALSE;gtk_widget_hide(strip);g_clear_pointer(&successful_pattern,g_free); }
}
static void focus_changed(GtkWindow *window,GtkWidget *focus,gpointer data)
{
 (void)window;(void)data;if(active && focus && focus!=entry && !gtk_widget_is_ancestor(focus,strip)) finish(FALSE,FALSE);
}
static void menu_command(GtkWidget *widget,gpointer data) { (void)widget;insight_incremental_search_command(GPOINTER_TO_INT(data)); }
static void accept_clicked(GtkWidget *widget,gpointer data) { (void)widget;finish(GPOINTER_TO_INT(data),TRUE); }
static void direction_clicked(GtkWidget *widget,gpointer data) { (void)widget;if(active) repeat(GPOINTER_TO_INT(data)); }
static void wrap_changed(GtkToggleButton *button,gpointer data) { (void)button;(void)data;if(active) find_pattern(FALSE); }
static void destroyed(GtkWidget *widget,gpointer data)
{
 (void)widget;(void)data;active=FALSE;strip=entry=status=wrap=NULL;g_clear_pointer(&previous_pattern,g_free);g_clear_pointer(&successful_pattern,g_free);
}
void insight_incremental_search_init(void)
{
 GtkWidget *root=ui_lookup_widget(main_widgets.window,"vbox1");strip=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_widget_set_name(strip,"insight-incremental-strip");
 gtk_box_pack_end(GTK_BOX(root),strip,FALSE,FALSE,0);
 GtkWidget *label=gtk_label_new("Incremental:");gtk_box_pack_start(GTK_BOX(strip),label,FALSE,FALSE,2);
 entry=gtk_entry_new();gtk_widget_set_name(entry,"insight-incremental-query");gtk_entry_set_max_length(GTK_ENTRY(entry),1024);gtk_entry_set_width_chars(GTK_ENTRY(entry),16);gtk_widget_set_hexpand(entry,TRUE);gtk_box_pack_start(GTK_BOX(strip),entry,TRUE,TRUE,0);
 wrap=gtk_check_button_new_with_label("Wrap around");gtk_widget_set_name(wrap,"insight-incremental-wrap");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wrap),TRUE);gtk_box_pack_start(GTK_BOX(strip),wrap,FALSE,FALSE,0);
 const gchar *labels[]={"Previous","Next","Accept","Cancel"},*names[]={"previous","next","accept","cancel"};
 for(guint n=0;n<4;n++) { GtkWidget *button=gtk_button_new_with_label(labels[n]);gchar *name=g_strconcat("insight-incremental-",names[n],NULL);gtk_widget_set_name(button,name);g_free(name);gtk_widget_set_can_focus(button,FALSE);gtk_box_pack_start(GTK_BOX(strip),button,FALSE,FALSE,0);g_signal_connect(button,"clicked",n<2 ? G_CALLBACK(direction_clicked):G_CALLBACK(accept_clicked),GINT_TO_POINTER(n==0 || n==3)); }
 status=gtk_label_new("");gtk_widget_set_name(status,"insight-incremental-status");gtk_label_set_ellipsize(GTK_LABEL(status),PANGO_ELLIPSIZE_END);gtk_label_set_width_chars(GTK_LABEL(status),1);gtk_widget_set_tooltip_text(entry,"Literal incremental search; uppercase enables Match case. Enter accepts, Esc restores. Repeat the command to find again.");gtk_box_pack_start(GTK_BOX(strip),status,TRUE,TRUE,0);
 insight_panel_help_attach(strip,"incremental-search.md");
 g_signal_connect(entry,"changed",G_CALLBACK(pattern_changed),NULL);g_signal_connect(wrap,"toggled",G_CALLBACK(wrap_changed),NULL);g_signal_connect(strip,"destroy",G_CALLBACK(destroyed),NULL);
 g_signal_connect_object(geany_object,"document-activate",G_CALLBACK(document_changed),strip,0);g_signal_connect_object(geany_object,"document-close",G_CALLBACK(document_closed),strip,0);g_signal_connect_object(main_widgets.window,"set-focus",G_CALLBACK(focus_changed),strip,0);
 GtkWidget *search_menu=ui_lookup_widget(main_widgets.window,"search1_menu");
 const gchar *commands[]={"Incremental Search","Incremental Search Backward"};const guint keys[]={GEANY_KEYS_INSIGHT_INCREMENTAL,GEANY_KEYS_INSIGHT_INCREMENTALBACK};
 for(guint n=0;n<2;n++) { GtkWidget *item=gtk_menu_item_new_with_label(commands[n]);gtk_widget_set_name(item,n ? "insight-incremental-back-command":"insight-incremental-command");keybindings_attach_menu(GEANY_KEY_GROUP_SEARCH,keys[n],item);g_signal_connect(item,"activate",G_CALLBACK(menu_command),GINT_TO_POINTER(n));gtk_menu_shell_append(GTK_MENU_SHELL(search_menu),item);gtk_widget_show(item); }
 gtk_widget_show_all(strip);gtk_widget_hide(strip);gtk_widget_set_no_show_all(strip,TRUE);
}
