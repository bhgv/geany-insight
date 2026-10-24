/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-ai-console.h"
#include <string.h>
#include <gdk/gdkkeysyms.h>
typedef struct { GtkWidget *view;GtkTextMark *start;gboolean internal;gchar *transcript;GPtrArray *drafts;guint position,actions; } Console;
static Console *state(GtkWidget *view) { return g_object_get_data(G_OBJECT(view),"insight-ai-console-state"); }
gchar *insight_ai_console_prompt(GtkWidget *view) {
 Console *c=state(view);GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));GtkTextIter a,z;gtk_text_buffer_get_iter_at_mark(b,&a,c->start);gtk_text_buffer_get_end_iter(b,&z);return gtk_text_buffer_get_text(b,&a,&z,TRUE);
}
/* Keep only prompt text: replies, source and request dispatch are never undone. */
static void remember(Console *c) {
 gchar *prompt=insight_ai_console_prompt(c->view);
 if(c->drafts->len && !strcmp(g_ptr_array_index(c->drafts,c->position),prompt)) { g_free(prompt);return; }
 while(c->drafts->len>c->position+1) g_ptr_array_remove_index(c->drafts,c->drafts->len-1);
 g_ptr_array_add(c->drafts,prompt);c->position=c->drafts->len-1;
 while(c->drafts->len>65) { g_ptr_array_remove_index(c->drafts,0);c->position--; }
}
static void rebuild(Console *c,const gchar *prompt) {
 GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(c->view));c->internal=TRUE;gtk_text_buffer_set_text(b,c->transcript ? c->transcript:"",-1);GtkTextIter a,z;gtk_text_buffer_get_end_iter(b,&z);gtk_text_buffer_insert(b,&z,"\nYou> ",-1);gtk_text_buffer_get_start_iter(b,&a);gtk_text_buffer_apply_tag_by_name(b,"ai-transcript",&a,&z);gtk_text_buffer_move_mark(b,c->start,&z);gtk_text_buffer_insert(b,&z,prompt ? prompt:"",-1);gtk_text_buffer_get_iter_at_mark(b,&a,c->start);gtk_text_buffer_remove_tag_by_name(b,"ai-transcript",&a,&z);gtk_text_buffer_place_cursor(b,&z);c->internal=FALSE;gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(c->view),gtk_text_buffer_get_insert(b));
}
static void replace_prompt(Console *c,const gchar *prompt) {
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(c->view));GtkTextIter a,z;c->internal=TRUE;
 gtk_text_buffer_get_iter_at_mark(buffer,&a,c->start);gtk_text_buffer_get_end_iter(buffer,&z);gtk_text_buffer_delete(buffer,&a,&z);gtk_text_buffer_insert(buffer,&a,prompt,-1);
 gtk_text_buffer_get_iter_at_mark(buffer,&z,c->start);gtk_text_buffer_remove_tag_by_name(buffer,"ai-transcript",&z,&a);gtk_text_buffer_place_cursor(buffer,&a);c->internal=FALSE;gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(c->view),gtk_text_buffer_get_insert(buffer));
}
void insight_ai_console_set_prompt(GtkWidget *view,const gchar *prompt) { Console *c=state(view);if(c && prompt && strlen(prompt)<=16384 && g_utf8_validate(prompt,-1,NULL)) { remember(c);replace_prompt(c,prompt);remember(c); } }
void insight_ai_console_transcript(GtkWidget *view,const gchar *text) {
 Console *c=state(view);gchar *prompt=insight_ai_console_prompt(view);gsize size=strlen(text),offset=size>262144 ? size-262144:0;while(offset<size && ((guchar)text[offset]&0xc0)==0x80) offset++;g_free(c->transcript);c->transcript=g_strdup(text+offset);rebuild(c,prompt);g_free(prompt);
}
static void inserting(GtkTextBuffer *buffer,GtkTextIter *at,gchar *text,gint length,gpointer data) {
 Console *c=state(GTK_WIDGET(data));if(c->internal) return;GtkTextIter start;gtk_text_buffer_get_iter_at_mark(buffer,&start,c->start);gchar *prompt=insight_ai_console_prompt(c->view);gboolean allowed=gtk_text_iter_compare(at,&start)>=0 && strlen(prompt)+(gsize)length<=16384 && g_utf8_validate(text,length,NULL);g_free(prompt);if(!allowed) g_signal_stop_emission_by_name(buffer,"insert-text");
}
static void deleting(GtkTextBuffer *buffer,GtkTextIter *a,GtkTextIter *z,gpointer data) { (void)z;Console *c=state(GTK_WIDGET(data));if(c->internal) return;GtkTextIter start;gtk_text_buffer_get_iter_at_mark(buffer,&start,c->start);if(gtk_text_iter_compare(a,&start)<0) g_signal_stop_emission_by_name(buffer,"delete-range"); }
static void changed(GtkTextBuffer *buffer,gpointer data) { Console *c=state(GTK_WIDGET(data));if(c->internal) return;GtkTextIter a,z;gtk_text_buffer_get_iter_at_mark(buffer,&a,c->start);gtk_text_buffer_get_end_iter(buffer,&z);gtk_text_buffer_remove_tag_by_name(buffer,"ai-transcript",&a,&z);if(!c->actions) remember(c); }
static void begin_action(GtkTextBuffer *buffer,gpointer data) { (void)buffer;Console *c=state(GTK_WIDGET(data));if(!c->internal) { if(!c->actions) remember(c);c->actions++; } }
static void end_action(GtkTextBuffer *buffer,gpointer data) { (void)buffer;Console *c=state(GTK_WIDGET(data));if(!c->internal && c->actions) { c->actions--;if(!c->actions) remember(c); } }
static gboolean move_draft(Console *c,gint step) {
 if(!gtk_text_view_get_editable(GTK_TEXT_VIEW(c->view))) return FALSE;
 remember(c);gint64 next=(gint64)c->position+step;if(next<0 || next>=(gint64)c->drafts->len) return FALSE;
 c->position=next;replace_prompt(c,g_ptr_array_index(c->drafts,c->position));return TRUE;
}
static gboolean key(GtkWidget *view,GdkEventKey *event,gpointer data) {
 (void)data;if((event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SUPER_MASK|GDK_META_MASK))!=GDK_CONTROL_MASK) return FALSE;
 if(event->keyval==GDK_KEY_z || event->keyval==GDK_KEY_Z) { move_draft(state(view),(event->state&GDK_SHIFT_MASK) ? 1:-1);return TRUE; }
 if(event->keyval==GDK_KEY_y || event->keyval==GDK_KEY_Y) { move_draft(state(view),1);return TRUE; }return FALSE;
}
static void menu_action(GtkMenuItem *item,gpointer data) {
 Console *c=state(GTK_WIDGET(data));gint step=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item),"ai-draft-step"));
 if(step) move_draft(c,step);else { GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(c->view));GtkTextIter a,z;gtk_text_buffer_get_iter_at_mark(b,&a,c->start);gtk_text_buffer_get_end_iter(b,&z);gtk_text_buffer_select_range(b,&a,&z); }
}
static void popup(GtkTextView *view,GtkWidget *menu,gpointer data) {
 (void)data;Console *c=state(GTK_WIDGET(view));remember(c);GtkWidget *separator=gtk_separator_menu_item_new();gtk_menu_shell_prepend(GTK_MENU_SHELL(menu),separator);
 const gchar *labels[]={"Select current prompt","Redo prompt","Undo prompt"},*names[]={"insight-ai-console-select-prompt","insight-ai-console-redo","insight-ai-console-undo"};gint steps[]={0,1,-1};
 for(guint i=0;i<3;i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(item,names[i]);g_object_set_data(G_OBJECT(item),"ai-draft-step",GINT_TO_POINTER(steps[i]));gtk_widget_set_sensitive(item,gtk_text_view_get_editable(view) && (steps[i]==0 || (steps[i]<0 ? c->position>0:c->position+1<c->drafts->len)));g_signal_connect_object(item,"activate",G_CALLBACK(menu_action),view,0);gtk_menu_shell_prepend(GTK_MENU_SHELL(menu),item); }gtk_widget_show_all(menu);
}
static void freed(gpointer data) { Console *c=data;g_free(c->transcript);g_ptr_array_unref(c->drafts);g_free(c); }
GtkWidget *insight_ai_console_new(void) {
 Console *c=g_new0(Console,1);c->drafts=g_ptr_array_new_with_free_func(g_free);c->view=gtk_text_view_new();gtk_widget_set_name(c->view,"insight-ai-answer");gtk_style_context_add_class(gtk_widget_get_style_context(c->view),"insight-ai-console");gtk_text_view_set_monospace(GTK_TEXT_VIEW(c->view),TRUE);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(c->view),GTK_WRAP_WORD_CHAR);gtk_text_view_set_left_margin(GTK_TEXT_VIEW(c->view),6);gtk_text_view_set_right_margin(GTK_TEXT_VIEW(c->view),6);GtkCssProvider *css=gtk_css_provider_new();gtk_css_provider_load_from_data(css,"textview.insight-ai-console, textview.insight-ai-console text { background: #000; color: #ddd; font-family: monospace; font-size: 10pt; } textview.insight-ai-console text selection { background: #315a8b; color: #fff; }",-1,NULL);gtk_style_context_add_provider(gtk_widget_get_style_context(c->view),GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);g_object_unref(css);GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(c->view));GtkTextIter a;gtk_text_buffer_get_start_iter(b,&a);c->start=gtk_text_buffer_create_mark(b,"insight-ai-prompt-start",&a,TRUE);gtk_text_buffer_create_tag(b,"ai-transcript","editable",FALSE,NULL);g_object_set_data_full(G_OBJECT(c->view),"insight-ai-console-state",c,freed);g_signal_connect_object(b,"insert-text",G_CALLBACK(inserting),c->view,0);g_signal_connect_object(b,"delete-range",G_CALLBACK(deleting),c->view,0);g_signal_connect_object(b,"changed",G_CALLBACK(changed),c->view,0);g_signal_connect_object(b,"begin-user-action",G_CALLBACK(begin_action),c->view,0);g_signal_connect_object(b,"end-user-action",G_CALLBACK(end_action),c->view,0);g_signal_connect(c->view,"key-press-event",G_CALLBACK(key),NULL);g_signal_connect(c->view,"populate-popup",G_CALLBACK(popup),NULL);rebuild(c,"");remember(c);return c->view;
}
