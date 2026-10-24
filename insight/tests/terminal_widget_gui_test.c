/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/insight-terminal.h"
#include <gdk/gdkkeysyms.h>
#include <string.h>
static GtkWidget *window,*view;static GString *sent;static guint phase,ticks,writes;
static void write_bytes(const gchar *text,gsize size,gpointer data) { (void)data;writes++;g_string_append_len(sent,text,size); }
static gchar *contents(void) { GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));GtkTextIter a,z;gtk_text_buffer_get_bounds(b,&a,&z);return gtk_text_buffer_get_text(b,&a,&z,FALSE); }
static void button(guint number) { GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS);event->button.window=g_object_ref(gtk_widget_get_window(view));event->button.button=number;gboolean handled=FALSE;g_signal_emit_by_name(view,"button-press-event",event,&handled);g_assert_true(handled);gdk_event_free(event); }
static void key(guint val,guint modifiers) { GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(view));event->key.keyval=val;event->key.state=modifiers;g_assert_true(insight_terminal_key(view,&event->key));gdk_event_free(event); }
static gboolean step(gpointer data) {
 (void)data;gchar *text=contents();GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));GtkTextIter a,z;
 if(phase==0) {
  g_assert_nonnull(strstr(text,"READY"));g_assert_null(strchr(text,27));g_assert_nonnull(strstr(text,"Привет"));g_assert_false(gtk_text_view_get_editable(GTK_TEXT_VIEW(view)));
  gtk_text_buffer_get_start_iter(buffer,&a);GSList *tags=gtk_text_iter_get_tags(&a);gboolean red=FALSE;for(GSList *p=tags;p;p=p->next) { GdkRGBA *color=NULL;gboolean set=FALSE;g_object_get(p->data,"foreground-set",&set,NULL);if(set) { g_object_get(p->data,"foreground-rgba",&color,NULL);if(color && color->red>color->green) red=TRUE;if(color) gdk_rgba_free(color); } }g_slist_free(tags);g_assert_true(red);
  gtk_text_buffer_get_start_iter(buffer,&a);z=a;gtk_text_iter_forward_chars(&z,5);gtk_text_buffer_select_range(buffer,&a,&z);key(GDK_KEY_C,GDK_CONTROL_MASK|GDK_SHIFT_MASK);gchar *copy=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(copy,==,"READY");g_free(copy);g_string_truncate(sent,0);button(2);g_assert_cmpstr(sent->str,==,"READY");
  gtk_text_buffer_place_cursor(buffer,&z);gtk_clipboard_clear(gtk_clipboard_get(GDK_SELECTION_PRIMARY));gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"FALLBACK",-1);g_string_truncate(sent,0);button(2);phase++;
 } else if(phase==1 && !strcmp(sent->str,"FALLBACK")) {
  g_string_truncate(sent,0);key(GDK_KEY_Up,0);g_assert_cmpstr(sent->str,==,"\033[A");g_string_truncate(sent,0);key(GDK_KEY_c,GDK_CONTROL_MASK);g_assert_cmpuint(sent->len,==,1);g_assert_cmpint(sent->str[0],==,3);
  insight_terminal_feed(view,"\033[?1049h\033[HALTERNATE",strlen("\033[?1049h\033[HALTERNATE"));phase++;
 } else if(phase==2) { g_assert_nonnull(strstr(text,"ALTERNATE"));g_assert_null(strstr(text,"READY"));insight_terminal_feed(view,"\033[?1049l",8);phase++; }
 else if(phase==3) { g_assert_nonnull(strstr(text,"READY"));gtk_text_buffer_get_iter_at_offset(buffer,&a,2);gtk_text_buffer_create_mark(buffer,"terminal-stable-mark",&a,TRUE);gtk_text_buffer_get_start_iter(buffer,&a);z=a;gtk_text_iter_forward_chars(&z,5);gtk_text_buffer_select_range(buffer,&a,&z);insight_terminal_feed(view,"\r\nSTREAM",8);phase++; }
 else if(phase==4) { gtk_text_buffer_get_iter_at_mark(buffer,&a,gtk_text_buffer_get_mark(buffer,"terminal-stable-mark"));g_assert_cmpint(gtk_text_iter_get_offset(&a),==,2);g_assert_true(gtk_text_buffer_get_selection_bounds(buffer,&a,&z));gchar *selected=gtk_text_buffer_get_text(buffer,&a,&z,FALSE);g_assert_cmpstr(selected,==,"READY");g_free(selected);insight_terminal_feed(view,"\033[?2004h",8);g_string_truncate(sent,0);writes=0;insight_terminal_send(view,"paste");g_assert_cmpuint(writes,==,1);g_assert_cmpstr(sent->str,==,"\033[200~paste\033[201~");GtkWidget *tab=insight_terminal_tab(0,0,"Connection");g_assert_true(GTK_IS_IMAGE(tab));g_assert_nonnull(gtk_image_get_pixbuf(GTK_IMAGE(tab)));g_object_ref_sink(tab);gtk_widget_destroy(tab);g_object_unref(tab);g_print("Production terminal widget: SGR tags, overwrite/erase, UTF-8, alternate screen, copy, selected middle paste, clipboard fallback, VT key bytes and SVG icons passed\n");gtk_widget_destroy(window);gtk_main_quit();g_free(text);return G_SOURCE_REMOVE; }
 g_free(text);if(++ticks>100) g_error("Terminal widget timeout phase %u",phase);return G_SOURCE_CONTINUE;
}
int main(int argc,char **argv) {
 if(!g_getenv("DISPLAY")) return 77;gtk_init(&argc,&argv);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);sent=g_string_new(NULL);window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_default_size(GTK_WINDOW(window),850,420);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);view=insight_terminal_new(write_bytes,NULL);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_container_add(GTK_CONTAINER(window),scroll);gtk_widget_show_all(window);
 insight_terminal_feed(view,"\033[31",4);const gchar *bytes="mOLD\rREADY\033[K\033[0m\r\nПривет\r\n";insight_terminal_feed(view,bytes,strlen(bytes));g_timeout_add(80,step,NULL);gtk_main();g_string_free(sent,TRUE);return 0;
}
