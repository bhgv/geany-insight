/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Isolated fixture using the production window and callbacks, not libgeany. */
#include <stdarg.h>
#include <glib/gstdio.h>
#include "../../src/insight-diff.c"
const gchar *insight_tr(const gchar *text) { return text; }
void insight_hex_show(GtkWidget *parent,const gchar *left,const gchar *right) { (void)parent;(void)left;(void)right; }
void insight_directory_show(GtkWidget *parent) { (void)parent; }
gchar *insight_i18n_printf(const gchar *format,...) {
 va_list args;va_start(args,format);gchar *s=g_strdup_vprintf(format,args);va_end(args);return s;
}
GtkWidget *insight_panel_help_button(const gchar *topic) { (void)topic;return gtk_button_new_with_label("?"); }
void insight_panel_help_attach(GtkWidget *widget,const gchar *topic) { (void)widget;(void)topic; }
static gchar *editor_path;static guint editor_line;
static void source_opened(const gchar *path,guint line) { g_free(editor_path);editor_path=g_strdup(path);editor_line=line; }
static void flush(void) { while(gtk_events_pending()) gtk_main_iteration(); }
static void wait_comparison(Merge *m) {
 gint64 until=g_get_monotonic_time()+10*G_TIME_SPAN_SECOND;
 while(m->busy && g_get_monotonic_time()<until) { flush();g_usleep(1000); }
 g_assert_false(m->busy);flush();
}
static gboolean heartbeat(gpointer data) { (*(guint*)data)++;return G_SOURCE_CONTINUE; }
static void finalized(gpointer data,GObject *object) { (void)object;*(gboolean*)data=TRUE; }
static void assert_text(Merge *m,const char *expected) {
 gchar *s=contents(result(m));g_assert_cmpstr(s,==,expected);g_free(s);
}
static gchar *random_text(GRand *random) {
 const gchar *lines[]={"a\n","b\r\n","\n","я\r","tail"};GString *text=g_string_new(NULL);
 guint count=g_rand_int_range(random,0,15);for(guint i=0;i<count;i++) g_string_append(text,lines[g_rand_int_range(random,0,G_N_ELEMENTS(lines))]);return g_string_free(text,FALSE);
}
static void random_batches(const gchar *left,const gchar *right) {
 Merge *m=merge_new(NULL);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->ignore_space),FALSE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->ignore_eol),FALSE);GRand *random=g_rand_new_with_seed(1093);GError *error=NULL;
 gtk_entry_set_text(GTK_ENTRY(m->paths[0]),left);gtk_entry_set_text(GTK_ENTRY(m->paths[1]),right);
 for(guint i=0;i<150;i++) {
  gchar *a=random_text(random),*b=random_text(random);g_assert_true(g_file_set_contents(left,a,-1,&error));g_assert_no_error(error);g_assert_true(g_file_set_contents(right,b,-1,&error));g_assert_no_error(error);
  compare(NULL,m);wait_comparison(m);for(guint side=0;side<2;side++) { g_assert_cmpuint(m->map_lines[side],==,(guint)gtk_text_buffer_get_line_count(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]))));for(gsize k=0;k<m->diff.count;k++) { InsightDiffHunk *h=&m->diff.hunks[k];MapBlock block=g_array_index(m->map_blocks[side],MapBlock,k);if((side ? h->b_end>h->b_begin : h->a_end>h->a_begin)) g_assert_cmpuint(block.end,>,block.begin);g_assert_cmpuint(block.end,<=,m->map_lines[side]); } }assert_text(m,a);take_all(m->take_all[1],m);assert_text(m,b);undo(NULL,m);assert_text(m,a);
  g_free(a);g_free(b);
 }
 g_rand_free(random);gtk_widget_destroy(m->window);flush();
}
static GtkWidget *named(GtkWidget *widget,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(widget),name)) return widget;
 if(GTK_IS_CONTAINER(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static gboolean close_options_parent(gpointer window) { gtk_widget_destroy(GTK_WIDGET(window));return G_SOURCE_REMOVE; }
static gboolean options_response(gpointer data) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p && !dialog;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-diff-options")) dialog=p->data;
 g_list_free(windows);g_assert_nonnull(dialog);
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(named(dialog,"insight-diff-options-zoom")),130);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog,"insight-diff-options-1")),FALSE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog,"insight-diff-options-2")),FALSE);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog,"insight-diff-options-7")),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog,"insight-diff-options-8")),TRUE);
 const gchar *capture=g_getenv("INSIGHT_DIFF_OPTIONS_CAPTURE");
 if(capture && GPOINTER_TO_INT(data)==GTK_RESPONSE_APPLY) { gint64 until=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<until) { flush();g_usleep(1000); }GdkWindow *window=gtk_widget_get_window(dialog);GdkPixbuf *image=gdk_pixbuf_get_from_window(window,0,0,gdk_window_get_width(window),gdk_window_get_height(window));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,capture,"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
 gtk_dialog_response(GTK_DIALOG(dialog),GPOINTER_TO_INT(data));return G_SOURCE_REMOVE;
}
static void search_options_test(void) {
 Merge *m=merge_new(NULL);GtkTextBuffer *b=result(m);GtkTextIter a,z;
 gtk_text_buffer_set_text(b,"foobar foo_bar foo2 αfoo foó foo FOO",-1);
 gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),2);
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo");
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_word),TRUE);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_wrap),FALSE);
 gtk_text_buffer_get_start_iter(b,&a);gtk_text_buffer_place_cursor(b,&a);
 find_text(m,FALSE);g_assert_true(gtk_text_buffer_get_selection_bounds(b,&a,&z));g_assert_cmpint(gtk_text_iter_get_offset(&a),==,30);
 find_text(m,FALSE);g_assert_true(gtk_text_buffer_get_selection_bounds(b,&a,&z));g_assert_cmpint(gtk_text_iter_get_offset(&a),==,34);
 find_text(m,FALSE);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->find_status)),==,"Text not found.");
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_wrap),TRUE);find_text(m,FALSE);
 g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->find_status)),"Wrapped"));
 find_text(m,TRUE);g_assert_true(gtk_text_buffer_get_selection_bounds(b,&a,&z));g_assert_cmpint(gtk_text_iter_get_offset(&a),==,34);
 gtk_widget_grab_focus(m->views[2]);focus_find(m);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(m->find_entry)),==,"FOO");
 assert_text(m,"foobar foo_bar foo2 αfoo foó foo FOO");
 gtk_widget_destroy(m->window);flush();
}
static void line_navigation_test(void) {
 Merge *m=merge_new(NULL);flush();GtkTextBuffer *b=result(m);GtkTextIter a,z;GdkEventKey event={0};
 gtk_text_buffer_set_text(b,"one\r\nдва\rthree\n",-1);guint history=m->history->len;
 gtk_widget_grab_focus(m->views[2]);event.state=GDK_CONTROL_MASK;event.keyval=GDK_KEY_g;
 g_assert_true(key(m->views[2],&event,m));g_assert_true(gtk_window_get_focus(GTK_WINDOW(m->window))==m->line_number);g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(m->line_side)),==,2);
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->line_number),2);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->line_select),TRUE);
 go_line(NULL,m);g_assert_true(gtk_text_buffer_get_selection_bounds(b,&a,&z));gchar *text=gtk_text_buffer_get_text(b,&a,&z,TRUE);g_assert_cmpstr(text,==,"два");g_free(text);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->wrap_toggle),TRUE);gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->line_number),G_MAXINT);go_line(NULL,m);
 gtk_text_buffer_get_iter_at_mark(b,&a,gtk_text_buffer_get_insert(b));g_assert_cmpint(gtk_text_iter_get_line(&a),==,3);g_assert_false(gtk_text_buffer_get_has_selection(b));
 line_begin(g_object_get_data(G_OBJECT(m->window),"line-first"),m);gtk_text_buffer_get_selection_bounds(b,&a,&z);g_assert_cmpint(gtk_text_iter_get_line(&a),==,0);
 event.state=0;event.keyval=GDK_KEY_F6;g_assert_true(key(m->views[2],&event,m));g_assert_true(gtk_widget_has_focus(m->views[0]));
 event.state=GDK_SHIFT_MASK;g_assert_true(key(m->views[0],&event,m));g_assert_true(gtk_widget_has_focus(m->views[2]));
 g_assert_cmpuint(m->history->len,==,history);assert_text(m,"one\r\nдва\rthree\n");
 gtk_text_buffer_set_text(b,"",0);gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->line_number),100);go_line(NULL,m);g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->line_number)),==,1);
 gtk_widget_destroy(m->window);flush();
}
static void replace_scope_test(void) {
 Merge *m=merge_new(NULL);GtkTextBuffer *b=result(m);GtkTextIter a,z;GdkEventKey event={0};
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo");gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"bar");
 const gchar *initial="ёж foo foo\r\nfoo foo";gtk_text_buffer_set_text(b,initial,-1);
 gtk_text_buffer_get_iter_at_offset(b,&a,7);gtk_text_buffer_place_cursor(b,&a);gtk_combo_box_set_active(GTK_COMBO_BOX(m->replace_scope),2);
 guint history=m->position;replace_result(NULL,m);assert_text(m,initial);g_assert_cmpuint(m->position,==,history);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->replace_status)),==,"Preview: 3 replacements");
 replace_result(m->replace_apply,m);assert_text(m,"ёж foo bar\r\nbar bar");g_assert_cmpuint(m->position,==,history+1);undo(NULL,m);assert_text(m,initial);redo(NULL,m);assert_text(m,"ёж foo bar\r\nbar bar");undo(NULL,m);
 /* Backwards selections start at their lower bound, not the insertion mark. */
 gtk_text_buffer_get_iter_at_offset(b,&a,3);gtk_text_buffer_get_iter_at_offset(b,&z,6);gtk_text_buffer_select_range(b,&z,&a);replace_result(m->replace_apply,m);assert_text(m,"ёж bar bar\r\nbar bar");undo(NULL,m);
 gtk_text_buffer_get_iter_at_offset(b,&a,8);gtk_text_buffer_place_cursor(b,&a);replace_result(m->replace_apply,m);assert_text(m,"ёж foo foo\r\nbar bar");undo(NULL,m);
 gtk_text_buffer_get_end_iter(b,&a);gtk_text_buffer_place_cursor(b,&a);history=m->position;replace_result(m->replace_apply,m);assert_text(m,initial);g_assert_cmpuint(m->position,==,history);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->replace_once),TRUE);gtk_text_buffer_get_iter_at_offset(b,&a,7);gtk_text_buffer_place_cursor(b,&a);replace_result(m->replace_apply,m);assert_text(m,"ёж foo bar\r\nbar foo");undo(NULL,m);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->replace_once),FALSE);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_regex),TRUE);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"(foo)");gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"[\\1]");gtk_text_buffer_get_iter_at_offset(b,&a,7);gtk_text_buffer_place_cursor(b,&a);replace_result(m->replace_apply,m);assert_text(m,"ёж foo [foo]\r\n[foo] [foo]");undo(NULL,m);
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"$");gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"!");gtk_text_buffer_get_end_iter(b,&a);gtk_text_buffer_place_cursor(b,&a);replace_result(m->replace_apply,m);assert_text(m,"ёж foo foo\r\nfoo foo!");undo(NULL,m);
 gtk_combo_box_set_active(GTK_COMBO_BOX(m->replace_scope),1);gtk_text_buffer_get_start_iter(b,&a);gtk_text_buffer_place_cursor(b,&a);replace_result(m->replace_apply,m);assert_text(m,initial);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->replace_status)),==,"Select result text first.");
 event.state=GDK_CONTROL_MASK;event.keyval=GDK_KEY_h;g_assert_true(key(m->views[2],&event,m));g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(m->replace_scope)),==,0);
 gtk_text_buffer_get_iter_at_offset(b,&a,3);gtk_text_buffer_get_iter_at_offset(b,&z,6);gtk_text_buffer_select_range(b,&a,&z);g_assert_true(key(m->views[2],&event,m));g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(m->replace_scope)),==,1);
 gtk_widget_destroy(m->window);flush();g_clear_pointer(&view_defaults,g_key_file_unref);
}
typedef struct { Merge *m;guint step;guint mode; } ConfirmAction;
static gboolean confirm_response(gpointer data) {
 ConfirmAction *action=data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-diff-confirm-replace")) dialog=p->data;
 g_list_free(windows);g_assert_nonnull(dialog);
 if(action->mode==3) { gtk_widget_destroy(action->m->window);return G_SOURCE_REMOVE; }
 if(action->mode==2) { gtk_text_buffer_set_text(result(action->m),"external change",-1);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_YES);return G_SOURCE_REMOVE; }
 gint response=action->step==0 ? GTK_RESPONSE_YES : action->step==1 ? GTK_RESPONSE_NO : action->mode==1 ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_APPLY;
 action->step++;gtk_dialog_response(GTK_DIALOG(dialog),response);return action->step<3 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}
static void confirm_replace_test(void) {
 for(guint mode=0;mode<5;mode++) {
  Merge *m=merge_new(NULL);gtk_text_buffer_set_text(result(m),"foo foo foo foo",-1);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo");gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"bar");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->replace_confirm),TRUE);
  if(mode==4) { GtkTextIter caret;gtk_text_buffer_get_iter_at_offset(result(m),&caret,4);gtk_text_buffer_place_cursor(result(m),&caret);gtk_combo_box_set_active(GTK_COMBO_BOX(m->replace_scope),2); }
  guint history=m->position;ConfirmAction action={m,0,mode};g_timeout_add(5,confirm_response,&action);replace_result(m->replace_apply,m);
  if(mode==3) continue;
  if(mode==0) { assert_text(m,"bar foo bar bar");g_assert_cmpuint(m->position,==,history+1);undo(NULL,m);assert_text(m,"foo foo foo foo");redo(NULL,m);assert_text(m,"bar foo bar bar"); }
  else if(mode==4) { assert_text(m,"foo bar foo bar");undo(NULL,m);assert_text(m,"foo foo foo foo"); }
  else if(mode==1) { assert_text(m,"foo foo foo foo");g_assert_cmpuint(m->position,==,history); }
  else assert_text(m,"external change");
  gtk_widget_destroy(m->window);
 }
 g_clear_pointer(&view_defaults,g_key_file_unref);
}
static void replace_test(void) {
 Merge *m=merge_new(NULL);GtkTextBuffer *b=result(m);GtkTextIter a,z;
 const gchar *initial="foo foo_bar FOO\r\nfoo foo\nяfoo foo";gtk_text_buffer_set_text(b,initial,-1);
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo");gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"ёж");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_word),TRUE);
 guint history=m->history->len;replace_result(NULL,m);assert_text(m,initial);g_assert_cmpuint(m->history->len,==,history);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->replace_status)),==,"Preview: 5 replacements");
 replace_result(m->replace_apply,m);assert_text(m,"ёж foo_bar ёж\r\nёж ёж\nяfoo ёж");g_assert_cmpuint(m->history->len,==,history+1);undo(NULL,m);assert_text(m,initial);redo(NULL,m);undo(NULL,m);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->replace_once),TRUE);replace_result(m->replace_apply,m);assert_text(m,"ёж foo_bar FOO\r\nёж foo\nяfoo ёж");undo(NULL,m);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->replace_once),FALSE);gtk_combo_box_set_active(GTK_COMBO_BOX(m->replace_scope),1);
 gtk_text_buffer_get_iter_at_line(b,&a,1);z=a;gtk_text_iter_forward_to_line_end(&z);gtk_text_buffer_select_range(b,&a,&z);
 gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"");replace_result(m->replace_apply,m);assert_text(m,"foo foo_bar FOO\r\n \nяfoo foo");undo(NULL,m);
 gtk_text_buffer_get_start_iter(b,&a);gtk_text_buffer_place_cursor(b,&a);replace_result(m->replace_apply,m);assert_text(m,initial);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->replace_status)),==,"Select result text first.");
 gtk_combo_box_set_active(GTK_COMBO_BOX(m->replace_scope),0);GString *many=g_string_new(NULL);for(guint i=0;i<100;i++) g_string_append(many,"foo ");gtk_text_buffer_set_text(b,many->str,-1);gchar *huge=g_strnfill(60000,'x');gtk_entry_set_text(GTK_ENTRY(m->replace_entry),huge);g_free(huge);replace_result(m->replace_apply,m);assert_text(m,many->str);g_string_free(many,TRUE);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->replace_status)),"Nothing changed"));gtk_text_buffer_set_text(b,initial,-1);
 gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"foo");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_case),TRUE);history=m->history->len;replace_result(m->replace_apply,m);g_assert_cmpuint(m->history->len,==,history);assert_text(m,initial);
 m->busy=TRUE;gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"blocked");replace_result(m->replace_apply,m);assert_text(m,initial);m->busy=FALSE;
 gtk_widget_destroy(m->window);flush();
}
static void case_test(void) {
 const gchar *old[]={"foo","FOO","Foo","fooBar","123","_ЁЖ","Ёж","ß"};
 const gchar *expected[]={"ёж","ЁЖ","Ёж","ёЖ","ёЖ","ЁЖ","Ёж","ёж"};
 for(guint i=0;i<G_N_ELEMENTS(old);i++) { gchar *text=preserve_case(old[i],"ёЖ");g_assert_cmpstr(text,==,expected[i]);g_free(text); }
 gchar *text=preserve_case("FOO","straße");g_assert_cmpstr(text,==,"STRASSE");g_free(text);
 Merge *m=merge_new(NULL);GtkTextBuffer *b=result(m);GtkTextIter a,z;
 const gchar *initial="foo FOO Foo fooBar\r\nёж ЁЖ Ёж";gtk_text_buffer_set_text(b,initial,-1);
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo");gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"straße");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_word),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->replace_case),TRUE);
 guint history=m->history->len;replace_result(NULL,m);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->replace_status)),==,"Preview: 3 replacements");g_assert_cmpuint(m->history->len,==,history);
 replace_result(m->replace_apply,m);assert_text(m,"straße STRASSE Straße fooBar\r\nёж ЁЖ Ёж");g_assert_cmpuint(m->history->len,==,history+1);undo(NULL,m);assert_text(m,initial);redo(NULL,m);undo(NULL,m);
 gtk_text_buffer_set_text(b,"Straße ёЖ 42\r\n",-1);gtk_text_buffer_get_bounds(b,&a,&z);gtk_text_buffer_select_range(b,&a,&z);history=m->history->len;GtkWidget *menu=gtk_menu_new();g_object_ref_sink(menu);source_menu(GTK_TEXT_VIEW(m->views[2]),menu,m);GList *items=gtk_container_get_children(GTK_CONTAINER(menu));gboolean activated=FALSE;for(GList *p=items;p;p=p->next) if(GTK_IS_MENU_ITEM(p->data) && !g_strcmp0(gtk_menu_item_get_label(p->data),"Uppercase selection")) { gtk_menu_item_activate(p->data);activated=TRUE;break; }g_list_free(items);g_assert_true(activated);gtk_widget_destroy(menu);g_object_unref(menu);assert_text(m,"STRASSE ЁЖ 42\r\n");g_assert_true(gtk_text_buffer_get_has_selection(b));g_assert_cmpuint(m->history->len,==,history+1);undo(NULL,m);assert_text(m,"Straße ёЖ 42\r\n");
 gtk_text_buffer_get_bounds(b,&a,&z);gtk_text_buffer_select_range(b,&a,&z);selection_case(m,1);assert_text(m,"straße ёж 42\r\n");undo(NULL,m);
 gtk_text_buffer_get_bounds(b,&a,&z);gtk_text_buffer_select_range(b,&a,&z);selection_case(m,2);assert_text(m,"sTRASSE Ёж 42\r\n");undo(NULL,m);
 gtk_text_buffer_get_start_iter(b,&a);gtk_text_buffer_place_cursor(b,&a);history=m->history->len;selection_case(m,0);g_assert_cmpuint(m->history->len,==,history);
 gtk_text_buffer_get_bounds(b,&a,&z);gtk_text_buffer_select_range(b,&a,&z);m->busy=TRUE;selection_case(m,0);assert_text(m,"Straße ёЖ 42\r\n");m->busy=FALSE;
 GString *large=g_string_new(NULL);for(guint i=0;i<180000;i++) { g_string_append(large,"ΐ");if(i%50==49) g_string_append_c(large,'\n'); }gtk_text_buffer_set_text(b,large->str,-1);gtk_text_buffer_get_bounds(b,&a,&z);gtk_text_buffer_select_range(b,&a,&z);history=m->history->len;selection_case(m,0);g_assert_cmpuint(m->history->len,==,history);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->replace_status)),"Nothing changed"));g_assert_cmpint(gtk_text_buffer_get_char_count(b),==,183600);g_string_free(large,TRUE);
 gtk_widget_destroy(m->window);flush();
}
static void line_edit_test(void) {
 Merge *m=merge_new(NULL);GtkTextBuffer *b=result(m);GtkTextIter a,z;
 const gchar *initial="#define X\r\n  ёж\t x  \r\n\tline\t\nend  ";gtk_text_buffer_set_text(b,initial,-1);gtk_text_buffer_get_bounds(b,&a,&z);gtk_text_buffer_select_range(b,&a,&z);guint history=m->history->len;
 edit_lines(m,0);assert_text(m,"#define X\r\n      ёж\t x  \r\n    \tline\t\n    end  ");g_assert_cmpuint(m->history->len,==,history+1);undo(NULL,m);assert_text(m,initial);
 gtk_text_buffer_get_bounds(b,&a,&z);gtk_text_buffer_select_range(b,&a,&z);edit_lines(m,1);assert_text(m,"#define X\r\nёж\t x  \r\nline\t\nend  ");undo(NULL,m);
 gtk_text_buffer_get_bounds(b,&a,&z);gtk_text_buffer_select_range(b,&a,&z);edit_lines(m,2);assert_text(m,"#define X\r\n  ёж     x  \r\n    line    \nend  ");undo(NULL,m);
 gtk_text_buffer_get_bounds(b,&a,&z);gtk_text_buffer_select_range(b,&a,&z);edit_lines(m,3);assert_text(m,"#define X\r\n  ёж\t x\r\n\tline\nend");undo(NULL,m);
 gtk_text_buffer_get_iter_at_line(b,&a,1);gtk_text_buffer_get_iter_at_line(b,&z,2);gtk_text_buffer_select_range(b,&a,&z);edit_lines(m,3);assert_text(m,"#define X\r\n  ёж\t x\r\n\tline\t\nend  ");undo(NULL,m);
 gtk_text_buffer_get_iter_at_line(b,&a,3);gtk_text_buffer_place_cursor(b,&a);edit_lines(m,3);assert_text(m,"#define X\r\n  ёж\t x  \r\n\tline\t\nend");undo(NULL,m);
 GtkWidget *button=named(m->window,"insight-diff-result-menu");GtkWidget *menu=GTK_WIDGET(gtk_menu_button_get_popup(GTK_MENU_BUTTON(button)));gtk_widget_show(menu);GList *items=gtk_container_get_children(GTK_CONTAINER(menu));g_assert_cmpint(g_list_length(items),>,4);g_list_free(items);gtk_widget_hide(menu);
 m->busy=TRUE;edit_lines(m,0);assert_text(m,initial);m->busy=FALSE;
 gtk_text_buffer_set_text(b,"",0);history=m->history->len;edit_lines(m,3);g_assert_cmpuint(m->history->len,==,history);
 GString *large=g_string_new(NULL);for(guint i=0;i<70000;i++) { g_string_append_c(large,'\t');if(i%50==49) g_string_append_c(large,'\n'); }gtk_text_buffer_set_text(b,large->str,-1);gtk_text_buffer_get_bounds(b,&a,&z);gtk_text_buffer_select_range(b,&a,&z);gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->edit_tab_width),16);history=m->history->len;edit_lines(m,2);g_assert_cmpuint(m->history->len,==,history);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->replace_status)),"Nothing changed"));g_assert_cmpint(gtk_text_buffer_get_char_count(b),==,71400);g_string_free(large,TRUE);
 gtk_widget_destroy(m->window);flush();
}
typedef struct { Merge *m;guint index;gint response;gboolean mutate; } HistoryResponse;
static gboolean history_response(gpointer data) {
 HistoryResponse *action=data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-diff-history")) dialog=p->data;
 g_list_free(windows);g_assert_nonnull(dialog);
 flush();GtkWidget *tree=named(dialog,"insight-diff-history-list");GtkTreePath *path=gtk_tree_path_new_from_indices(action->index,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_path_free(path);flush();
 GtkTextBuffer *preview=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(dialog,"insight-diff-history-preview")));gchar *text=contents(preview);g_assert_cmpstr(text,==,((Snapshot*)g_ptr_array_index(action->m->history,action->index))->text);g_free(text);
 const gchar *capture=g_getenv("INSIGHT_DIFF_HISTORY_CAPTURE");if(capture && action->response==GTK_RESPONSE_CANCEL) { gint64 until=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<until) { flush();g_usleep(1000); }GdkWindow *window=gtk_widget_get_window(dialog);GdkPixbuf *pixbuf=gdk_pixbuf_get_from_window(window,0,0,gdk_window_get_width(window),gdk_window_get_height(window));g_assert_nonnull(pixbuf);g_assert_true(gdk_pixbuf_save(pixbuf,capture,"jpeg",NULL,"quality","88",NULL));g_object_unref(pixbuf); }
 if(action->mutate) gtk_text_buffer_set_text(result(action->m),"changed while open",-1);
 gtk_dialog_response(GTK_DIALOG(dialog),action->response);return G_SOURCE_REMOVE;
}
static void history_test(void) {
 Merge *m=merge_new(NULL);gtk_text_buffer_begin_user_action(result(m));gtk_text_buffer_set_text(result(m),"port = 3000\n",-1);gtk_text_buffer_end_user_action(result(m));gtk_text_buffer_begin_user_action(result(m));gtk_text_buffer_set_text(result(m),"port = 3333\n",-1);gtk_text_buffer_end_user_action(result(m));g_free(m->saved);m->saved=g_strdup("port = 3000\n");
 guint count=m->history->len;HistoryResponse response={m,1,GTK_RESPONSE_CANCEL,FALSE};g_timeout_add(100,history_response,&response);history_dialog(NULL,m);assert_text(m,"port = 3333\n");
 response.response=GTK_RESPONSE_APPLY;g_timeout_add(100,history_response,&response);history_dialog(NULL,m);assert_text(m,"port = 3000\n");g_assert_cmpuint(m->history->len,==,count);history_end(m,TRUE);assert_text(m,"port = 3333\n");history_end(m,FALSE);assert_text(m,"");
 history_end(m,TRUE);response.mutate=TRUE;g_timeout_add(100,history_response,&response);history_dialog(NULL,m);assert_text(m,"changed while open");
 m->busy=TRUE;guint position=m->position;undo(NULL,m);redo(NULL,m);history_end(m,FALSE);history_dialog(NULL,m);g_assert_cmpuint(m->position,==,position);m->busy=FALSE;
 gtk_text_buffer_set_text(result(m),"",0);for(guint i=0;i<520;i++) gtk_text_buffer_insert_at_cursor(result(m),"x",1);g_assert_cmpuint(m->history->len,==,512);history_end(m,FALSE);g_assert_cmpint(gtk_text_buffer_get_char_count(result(m)),>,0);history_end(m,TRUE);g_assert_cmpint(gtk_text_buffer_get_char_count(result(m)),==,520);
 gboolean gone=FALSE;g_object_weak_ref(G_OBJECT(m->window),finalized,&gone);g_idle_add(close_options_parent,m->window);history_dialog(NULL,m);flush();g_assert_true(gone);
}
typedef struct { Merge *m;const gchar *path;gint response;gboolean mutate,destroy; } ExportResponse;
static GtkWidget *save_dialog(void) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-diff-save-dialog")) dialog=p->data;
 g_list_free(windows);return dialog;
}
static gboolean export_respond(gpointer data) {
 ExportResponse *action=data;GtkWidget *dialog=save_dialog();g_assert_nonnull(dialog);
 if(action->destroy) { gtk_widget_destroy(action->m->window);return G_SOURCE_REMOVE; }
 if(action->mutate) gtk_text_buffer_set_text(result(action->m),"changed while saving",-1);
 const gchar *capture=g_getenv("INSIGHT_DIFF_EXPORT_CAPTURE");if(capture && !action->mutate && action->response==GTK_RESPONSE_ACCEPT && !g_strcmp0(gtk_window_get_title(GTK_WINDOW(dialog)),"Save a copy of Result")) { gint64 until=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<until) { flush();g_usleep(1000); }GdkWindow *window=gtk_widget_get_window(dialog);GdkPixbuf *pixbuf=gdk_pixbuf_get_from_window(window,0,0,gdk_window_get_width(window),gdk_window_get_height(window));g_assert_nonnull(pixbuf);g_assert_true(gdk_pixbuf_save(pixbuf,capture,"jpeg",NULL,"quality","88",NULL));g_object_unref(pixbuf); }
 gtk_dialog_response(GTK_DIALOG(dialog),action->response);return G_SOURCE_REMOVE;
}
static gboolean export_choose(gpointer data) {
 ExportResponse *action=data;GtkWidget *dialog=save_dialog();g_assert_nonnull(dialog);gchar *folder=g_path_get_dirname(action->path),*name=g_path_get_basename(action->path);g_assert_true(gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),folder));gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),name);g_free(folder);g_free(name);g_timeout_add(250,export_respond,action);return G_SOURCE_REMOVE;
}
static void export_test(void) {
 GError *error=NULL;gchar *folder=g_dir_make_tmp("insight-export-XXXXXX",&error);g_assert_no_error(error);gchar *path=g_build_filename(folder,"копия.txt",NULL);Merge *m=merge_new(NULL);GtkTextBuffer *b=result(m);const gchar *initial="first\r\nёж\r\nlast";gtk_text_buffer_set_text(b,initial,-1);guint history=m->history->len;gchar *saved=g_strdup(m->saved);
 ExportResponse action={m,path,GTK_RESPONSE_ACCEPT,FALSE,FALSE};g_timeout_add(100,export_choose,&action);write_result(m,1);gchar *text=NULL;g_assert_true(g_file_get_contents(path,&text,NULL,&error));g_assert_no_error(error);g_assert_cmpstr(text,==,initial);g_free(text);g_assert_cmpstr(m->saved,==,saved);g_assert_cmpuint(m->history->len,==,history);g_remove(path);
 GtkTextIter a,z;gtk_text_buffer_get_iter_at_line(b,&a,1);z=a;gtk_text_iter_forward_line(&z);gtk_text_buffer_select_range(b,&a,&z);InsightDiffOpen previous=source_opener;source_opener=source_opened;g_timeout_add(100,export_choose,&action);write_result(m,2);g_assert_true(g_file_get_contents(path,&text,NULL,&error));g_assert_cmpstr(text,==,"ёж\r\n");g_free(text);g_assert_cmpstr(editor_path,==,path);g_assert_cmpuint(editor_line,==,1);source_opener=previous;g_assert_cmpstr(m->saved,==,saved);g_remove(path);
 gtk_text_buffer_get_start_iter(b,&a);gtk_text_buffer_place_cursor(b,&a);write_result(m,2);g_assert_null(save_dialog());m->busy=TRUE;write_result(m,1);g_assert_null(save_dialog());m->busy=FALSE;
 action.response=GTK_RESPONSE_CANCEL;g_timeout_add(100,export_choose,&action);write_result(m,1);g_assert_false(g_file_test(path,G_FILE_TEST_EXISTS));
 action.response=GTK_RESPONSE_ACCEPT;g_timeout_add(100,export_choose,&action);write_result(m,0);g_assert_cmpstr(m->saved,==,initial);g_assert_cmpuint(m->history->len,==,history);g_remove(path);
 action.mutate=TRUE;g_timeout_add(100,export_choose,&action);write_result(m,1);g_assert_false(g_file_test(path,G_FILE_TEST_EXISTS));g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->status)),"Nothing written"));
 action.mutate=FALSE;action.destroy=TRUE;gboolean gone=FALSE;g_object_weak_ref(G_OBJECT(m->window),finalized,&gone);g_timeout_add(100,export_choose,&action);write_result(m,1);flush();g_assert_true(gone);g_assert_false(g_file_test(path,G_FILE_TEST_EXISTS));g_free(saved);g_rmdir(folder);g_free(path);g_free(folder);
}
static GdkPixbuf *text_capture(GtkWidget *view) {
 gint64 until=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<until) { flush();g_usleep(1000); }
 GdkWindow *window=gtk_text_view_get_window(GTK_TEXT_VIEW(view),GTK_TEXT_WINDOW_TEXT);GdkPixbuf *pixbuf=gdk_pixbuf_get_from_window(window,0,0,gdk_window_get_width(window),gdk_window_get_height(window));g_assert_nonnull(pixbuf);return pixbuf;
}
static void whitespace_test(void) {
 Merge *m=merge_new(NULL);GtkTextBuffer *buffer=result(m);const gchar *text="я \t\r\nx\ry\n\n";gtk_text_buffer_set_text(buffer,text,-1);guint history=m->history->len;
 GtkTextIter at;gtk_text_buffer_get_start_iter(buffer,&at);guint markers=0,endings=0;
 while(!gtk_text_iter_is_end(&at)) { const gchar *symbol=whitespace_symbol(at,TRUE,TRUE);if(symbol) { markers++;if(!strcmp(symbol,"¶")) endings++; }g_assert_null(whitespace_symbol(at,FALSE,FALSE));gtk_text_iter_forward_char(&at); }
 g_assert_cmpuint(markers,==,6);g_assert_cmpuint(endings,==,4);gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(m->views[2]),FALSE);
 GdkPixbuf *plain=text_capture(m->views[2]);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->visible_space),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->visible_eol),TRUE);GdkPixbuf *marked=text_capture(m->views[2]);g_assert_cmpint(gdk_pixbuf_get_byte_length(plain),==,gdk_pixbuf_get_byte_length(marked));g_assert_true(memcmp(gdk_pixbuf_get_pixels(plain),gdk_pixbuf_get_pixels(marked),gdk_pixbuf_get_byte_length(plain))!=0);g_object_unref(plain);g_object_unref(marked);assert_text(m,text);g_assert_cmpuint(m->history->len,==,history);
 GKeyFile *prefs=g_key_file_new();view_save(m,prefs);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->visible_space),FALSE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->visible_eol),FALSE);view_restore(m,prefs);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->visible_space)));g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->visible_eol)));g_key_file_unref(prefs);gtk_widget_destroy(m->window);flush();
}
typedef struct { Merge *m;guint rows,index;gint response;gboolean mutate; } SearchResponse;
static gboolean search_response(gpointer data) {
 SearchResponse *action=data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-diff-search-results")) dialog=p->data;
 g_list_free(windows);g_assert_nonnull(dialog);GtkWidget *tree=named(dialog,"insight-diff-search-results-list");g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),NULL),==,action->rows);
 if(action->rows) { GtkTreePath *path=gtk_tree_path_new_from_indices(action->index,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_path_free(path); }
 const gchar *capture=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(action->m->find_regex)) ? (strstr(gtk_entry_get_text(GTK_ENTRY(action->m->find_entry)),"\\d") ? g_getenv("INSIGHT_DIFF_REGEX_CAPTURE") : NULL) : g_getenv("INSIGHT_DIFF_SEARCH_CAPTURE");if(capture && action->rows==3) { gint64 until=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<until) { flush();g_usleep(1000); }GdkWindow *window=gtk_widget_get_window(dialog);GdkPixbuf *pixbuf=gdk_pixbuf_get_from_window(window,0,0,gdk_window_get_width(window),gdk_window_get_height(window));g_assert_nonnull(pixbuf);g_assert_true(gdk_pixbuf_save(pixbuf,capture,"jpeg",NULL,"quality","88",NULL));g_object_unref(pixbuf); }
 if(action->mutate) gtk_text_buffer_set_text(result(action->m),"changed while searching",-1);
 if(action->response==GTK_RESPONSE_APPLY && action->rows && !action->mutate) { GtkTreePath *path=gtk_tree_path_new_from_indices(action->index,-1);gtk_tree_view_row_activated(GTK_TREE_VIEW(tree),path,gtk_tree_view_get_column(GTK_TREE_VIEW(tree),0));gtk_tree_path_free(path); }else gtk_dialog_response(GTK_DIALOG(dialog),action->response);return G_SOURCE_REMOVE;
}
static void search_actions_test(void) {
 Merge *m=merge_new(NULL);GtkTextBuffer *buffer=result(m);const gchar *initial="foo foobar foo\r\nяfoo foo\n";gtk_text_buffer_set_text(buffer,initial,-1);GtkTextIter a,z;
 gtk_text_buffer_get_iter_at_offset(buffer,&a,1);gtk_text_buffer_place_cursor(buffer,&a);selection_search(m,2,0);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(m->find_entry)),==,"foo");g_assert_cmpint(gtk_text_iter_get_offset(&a),==,1);
 selection_search(m,2,1);g_assert_true(gtk_text_buffer_get_selection_bounds(buffer,&a,&z));g_assert_cmpint(gtk_text_iter_get_offset(&a),==,11);selection_search(m,2,-1);gtk_text_buffer_get_selection_bounds(buffer,&a,&z);g_assert_cmpint(gtk_text_iter_get_offset(&a),==,0);
 guint history=m->history->len;SearchResponse response={m,3,2,GTK_RESPONSE_CANCEL,FALSE};g_timeout_add(100,search_response,&response);find_all(m,2);g_assert_cmpuint(m->history->len,==,history);assert_text(m,initial);
 response.response=GTK_RESPONSE_APPLY;g_timeout_add(100,search_response,&response);find_all(m,2);gtk_text_buffer_get_selection_bounds(buffer,&a,&z);g_assert_cmpint(gtk_text_iter_get_line(&a),==,1);g_assert_cmpint(gtk_text_iter_get_line_offset(&a),==,5);
 response.rows=0;response.response=GTK_RESPONSE_CANCEL;gtk_entry_set_text(GTK_ENTRY(m->find_entry),"missing");g_timeout_add(100,search_response,&response);find_all(m,2);
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo");response.rows=3;response.response=GTK_RESPONSE_APPLY;response.mutate=TRUE;g_timeout_add(100,search_response,&response);find_all(m,2);assert_text(m,"changed while searching");g_assert_false(gtk_text_buffer_get_has_selection(buffer));
 gtk_text_buffer_set_text(buffer,"ёж_2 ёж_2",-1);gtk_text_buffer_get_start_iter(buffer,&a);gtk_text_buffer_place_cursor(buffer,&a);selection_search(m,2,1);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(m->find_entry)),==,"ёж_2");gtk_text_buffer_get_selection_bounds(buffer,&a,&z);g_assert_cmpint(gtk_text_iter_get_offset(&a),==,5);
 gchar *long_word=g_strnfill(4097,'x');gtk_text_buffer_set_text(buffer,long_word,-1);g_free(long_word);gtk_text_buffer_get_start_iter(buffer,&a);gtk_text_buffer_place_cursor(buffer,&a);selection_search(m,2,0);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(m->find_entry)),==,"ёж_2");gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo");
 GString *many=g_string_new(NULL);for(guint i=0;i<5100;i++) g_string_append(many,"foo\n");gtk_text_buffer_set_text(buffer,many->str,-1);g_string_free(many,TRUE);response.rows=5000;response.response=GTK_RESPONSE_CANCEL;response.mutate=FALSE;g_timeout_add(100,search_response,&response);find_all(m,2);
 gboolean gone=FALSE;g_object_weak_ref(G_OBJECT(m->window),finalized,&gone);g_idle_add(close_options_parent,m->window);find_all(m,2);flush();g_assert_true(gone);
}
static void output_key(GtkWidget *,guint,guint);
typedef struct { gboolean source; } OutputResponse;
static gboolean output_response(gpointer data) {
 OutputResponse *action=data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-diff-search-results")) dialog=p->data;
 g_list_free(windows);g_assert_nonnull(dialog);GtkWidget *tree=named(dialog,"insight-diff-search-results-list"),*filter=named(dialog,"insight-diff-search-filter"),*copy=named(dialog,"insight-diff-search-copy");GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(tree));gtk_entry_set_text(GTK_ENTRY(filter),"ёж");g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,2);gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model),2,GTK_SORT_DESCENDING);gtk_button_clicked(GTK_BUTTON(copy));gchar *text=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(text,==,action->source ? "File\tLine\tColumn\tText\n/tmp/a\\tb.c\t2\t1\tfoo ЁЖ\n/tmp/a\\tb.c\t1\t1\tfoo ёж\n" : "File\tLine\tColumn\tText\nResult\t2\t1\tfoo ЁЖ\nResult\t1\t1\tfoo ёж\n");g_free(text);
 for(guint i=0;i<4;i++) { gchar *name=g_strdup_printf("insight-diff-search-field-%u",i);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog,name)),i==1);g_free(name); }g_assert_false(gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(tree),1)));gtk_button_clicked(GTK_BUTTON(copy));text=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(text,==,"Line\n2\n1\n");g_free(text);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog,"insight-diff-search-field-1")),FALSE);gtk_button_clicked(GTK_BUTTON(copy));text=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(text,==,"Line\n2\n1\n");g_free(text);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog,"insight-diff-search-field-1")),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog,"insight-diff-search-field-3")),TRUE);gtk_entry_set_text(GTK_ENTRY(filter),"missing");g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,0);g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog),GTK_RESPONSE_APPLY)));gtk_entry_set_text(GTK_ENTRY(filter),"ёж");GtkTreePath *path=gtk_tree_path_new_first();gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_path_free(path);
 const gchar *capture=g_getenv("INSIGHT_DIFF_SEARCH_CAPTURE");if(capture && !action->source) { gint64 until=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<until) { flush();g_usleep(1000); }GdkWindow *window=gtk_widget_get_window(dialog);GdkPixbuf *pixbuf=gdk_pixbuf_get_from_window(window,0,0,gdk_window_get_width(window),gdk_window_get_height(window));g_assert_true(gdk_pixbuf_save(pixbuf,capture,"jpeg",NULL,"quality","88",NULL));g_object_unref(pixbuf); }
 if(action->source) gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_YES);else output_key(dialog,GDK_KEY_l,GDK_CONTROL_MASK);return G_SOURCE_REMOVE;
}
static void output_key(GtkWidget *dialog,guint key,guint modifiers) {
 GdkEventKey event={0};event.type=GDK_KEY_PRESS;event.keyval=key;event.state=modifiers;gboolean handled=FALSE;g_signal_emit_by_name(dialog,"key-press-event",&event,&handled);g_assert_true(handled);
}
static guint output_line(GtkWidget *tree) {
 GtkTreeIter iter;GtkTreeModel *model;g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)),&model,&iter));guint line;gtk_tree_model_get(model,&iter,2,&line,-1);return line;
}
static gboolean preview_response(gpointer data) {
 Merge *m=data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-diff-search-results")) dialog=p->data;g_list_free(windows);g_assert_nonnull(dialog);
 GtkWidget *tree=named(dialog,"insight-diff-search-results-list"),*filter=named(dialog,"insight-diff-search-filter"),*view=named(dialog,"insight-diff-search-preview"),*copy=named(dialog,"insight-diff-search-copy-selected");
 GtkTextBuffer *preview=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));g_assert_false(gtk_text_view_get_editable(GTK_TEXT_VIEW(view)));gchar *text=contents(preview);g_assert_cmpstr(text,==,"before\r\nfoo ёж\r\ncontext\nfoo other\n");g_free(text);
 GtkTextIter it;gtk_text_buffer_get_iter_at_offset(preview,&it,8);g_assert_true(gtk_text_iter_has_tag(&it,gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(preview),"match")));
 output_key(dialog,GDK_KEY_F3,0);g_assert_cmpuint(output_line(tree),==,4);output_key(dialog,GDK_KEY_F3,0);g_assert_cmpuint(output_line(tree),==,2);output_key(dialog,GDK_KEY_F3,GDK_SHIFT_MASK);g_assert_cmpuint(output_line(tree),==,4);
 gtk_button_clicked(GTK_BUTTON(copy));text=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(text,==,"Line\tColumn\tText\n4\t1\tfoo other\n");g_free(text);
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree))),2,GTK_SORT_DESCENDING);output_key(dialog,GDK_KEY_F3,0);g_assert_cmpuint(output_line(tree),==,2);output_key(dialog,GDK_KEY_F3,0);g_assert_cmpuint(output_line(tree),==,4);
 GtkTextIter caret;gtk_text_buffer_get_iter_at_mark(result(m),&caret,gtk_text_buffer_get_insert(result(m)));g_assert_cmpint(gtk_text_iter_get_offset(&caret),==,0);
 output_key(dialog,GDK_KEY_f,GDK_CONTROL_MASK);g_assert_true(gtk_window_get_focus(GTK_WINDOW(dialog))==filter);gtk_entry_set_text(GTK_ENTRY(filter),"missing");g_assert_false(gtk_widget_get_sensitive(copy));g_assert_cmpint(gtk_text_buffer_get_char_count(preview),==,0);output_key(dialog,GDK_KEY_F3,0);
 gtk_entry_set_text(GTK_ENTRY(filter),"ёж");output_key(dialog,GDK_KEY_Down,0);g_assert_true(gtk_window_get_focus(GTK_WINDOW(dialog))==tree);g_assert_cmpuint(output_line(tree),==,2);
 /* Snapshot preview survives edits while stale navigation remains rejected. */
 gtk_text_buffer_set_text(result(m),"changed while searching",-1);output_key(dialog,GDK_KEY_F3,0);text=contents(preview);g_assert_nonnull(strstr(text,"foo ёж"));g_free(text);output_key(dialog,GDK_KEY_Return,GDK_CONTROL_MASK);return G_SOURCE_REMOVE;
}
static gboolean preview_limit_response(gpointer data) {
 (void)data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-diff-search-results")) dialog=p->data;g_list_free(windows);g_assert_nonnull(dialog);
 GtkTextBuffer *preview=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(dialog,"insight-diff-search-preview")));g_assert_cmpint(gtk_text_buffer_get_char_count(preview),<=,4096);gchar *text=contents(preview);g_assert_nonnull(strstr(text,"foo"));g_free(text);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE;
}
static void search_preview_test(void) {
 Merge *m=merge_new(NULL);m->search_fields[0]=FALSE;for(guint i=1;i<4;i++) m->search_fields[i]=TRUE;gtk_text_buffer_set_text(result(m),"before\r\nfoo ёж\r\ncontext\nfoo other\nafter\n",-1);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo");GtkTextIter caret;gtk_text_buffer_get_start_iter(result(m),&caret);gtk_text_buffer_place_cursor(result(m),&caret);g_timeout_add(100,preview_response,m);find_all(m,2);assert_text(m,"changed while searching");g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->find_status)),==,"Text changed; run Find all again.");
 GString *long_line=g_string_new(NULL);for(guint i=0;i<10000;i++) g_string_append(long_line,"я");g_string_append(long_line,"foo");for(guint i=0;i<10000;i++) g_string_append(long_line,"я");gtk_text_buffer_set_text(result(m),long_line->str,-1);g_string_free(long_line,TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_word),FALSE);guint history=m->history->len;g_timeout_add(100,preview_limit_response,NULL);find_all(m,2);g_assert_cmpuint(m->history->len,==,history);gtk_widget_destroy(m->window);flush();
}
static void search_output_test(void) {
 GString *escaped=g_string_new(NULL);tsv_field(escaped,"a\tb\nc\\d\r");g_assert_cmpstr(escaped->str,==,"a\\tb\\nc\\\\d\\r");g_string_free(escaped,TRUE);
 InsightDiffOpen previous=source_opener;source_opener=source_opened;
 for(guint side=0;side<2;side++) { Merge *m=merge_new(NULL);for(guint i=0;i<4;i++) m->search_fields[i]=TRUE;guint pane=side ? 0 : 2;GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[pane]));gtk_text_buffer_set_text(buffer,"foo ёж\nfoo ЁЖ\nfoo other\n",-1);if(side) m->loaded_paths[0]=g_strdup("/tmp/a\tb.c");gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo");guint history=m->history->len;OutputResponse response={side!=0};g_timeout_add(100,output_response,&response);find_all(m,pane);GtkTextIter a,z;g_assert_true(gtk_text_buffer_get_selection_bounds(buffer,&a,&z));g_assert_cmpint(gtk_text_iter_get_line(&a),==,1);g_assert_cmpuint(m->history->len,==,history);g_assert_false(m->search_fields[0]);g_assert_true(m->search_fields[1]);g_assert_false(m->search_fields[2]);g_assert_true(m->search_fields[3]);if(side) { g_assert_cmpstr(editor_path,==,"/tmp/a\tb.c");g_assert_cmpuint(editor_line,==,2); }
 GKeyFile *prefs=g_key_file_new();view_save(m,prefs);m->search_fields[0]=TRUE;view_restore(m,prefs);g_assert_false(m->search_fields[0]);g_key_file_unref(prefs);gtk_widget_destroy(m->window);flush(); }
 source_opener=previous;
}
static gboolean output_limit_response(gpointer data) {
 (void)data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-diff-search-results")) dialog=p->data;
 g_list_free(windows);g_assert_nonnull(dialog);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"sentinel",-1);gtk_button_clicked(GTK_BUTTON(named(dialog,"insight-diff-search-copy")));gchar *text=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(text,==,"sentinel");g_free(text);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE;
}
static void output_limit_test(void) {
 Merge *m=merge_new(NULL);for(guint i=0;i<4;i++) m->search_fields[i]=TRUE;GString *text=g_string_new(NULL);for(guint i=0;i<5000;i++) g_string_append(text,"foo\n");gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[0])),text->str,-1);g_string_free(text,TRUE);m->loaded_paths[0]=g_strnfill(1024,'a');gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo");g_timeout_add(100,output_limit_response,NULL);find_all(m,0);gtk_widget_destroy(m->window);flush();
}
static void regex_selection(Merge *m,const gchar *expected) {
 GtkTextIter a,z;GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[gtk_combo_box_get_active(GTK_COMBO_BOX(m->find_side))]));g_assert_true(gtk_text_buffer_get_selection_bounds(buffer,&a,&z));gchar *text=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);g_assert_cmpstr(text,==,expected);g_free(text);
}
static void regex_search_test(void) {
 Merge *m=merge_new(NULL);GtkTextBuffer *b=result(m);GtkTextIter a,z;gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),2);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_regex),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_wrap),FALSE);
 const gchar *initial="α foo1\r\nFOO22\nfoobar foo3";gtk_text_buffer_set_text(b,initial,-1);gtk_text_buffer_get_start_iter(b,&a);gtk_text_buffer_place_cursor(b,&a);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo(\\d+)");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_word),TRUE);guint history=m->history->len;
 find_text(m,FALSE);regex_selection(m,"foo1");gtk_text_buffer_get_selection_bounds(b,&a,&z);g_assert_cmpint(gtk_text_iter_get_offset(&a),==,2);find_text(m,FALSE);regex_selection(m,"FOO22");find_text(m,TRUE);regex_selection(m,"foo1");
 SearchResponse response={m,3,2,GTK_RESPONSE_APPLY,FALSE};g_timeout_add(100,search_response,&response);find_all(m,2);regex_selection(m,"foo3");g_assert_cmpuint(m->history->len,==,history);
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo1.*FOO22");gtk_text_buffer_get_start_iter(b,&a);gtk_text_buffer_place_cursor(b,&a);find_text(m,FALSE);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->find_status)),==,"Text not found.");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_dotall),TRUE);find_text(m,FALSE);regex_selection(m,"foo1\r\nFOO22");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_dotall),FALSE);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_word),FALSE);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"^");gtk_text_buffer_get_start_iter(b,&a);gtk_text_buffer_place_cursor(b,&a);find_text(m,FALSE);find_text(m,FALSE);gtk_text_buffer_get_iter_at_mark(b,&a,gtk_text_buffer_get_insert(b));g_assert_cmpint(gtk_text_iter_get_line(&a),==,1);find_text(m,TRUE);gtk_text_buffer_get_iter_at_mark(b,&a,gtk_text_buffer_get_insert(b));g_assert_cmpint(gtk_text_iter_get_line(&a),==,0);
 find_text(m,FALSE);find_text(m,FALSE);find_text(m,FALSE);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->find_status)),==,"Text not found.");find_text(m,FALSE);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->find_status)),==,"Text not found.");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_wrap),TRUE);find_text(m,FALSE);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->find_status)),"Wrapped"));
 response.rows=3;response.index=1;g_timeout_add(100,search_response,&response);find_all(m,2);gtk_text_buffer_get_iter_at_mark(b,&a,gtk_text_buffer_get_insert(b));g_assert_cmpint(gtk_text_iter_get_line(&a),==,1);g_assert_false(gtk_text_buffer_get_has_selection(b));
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"(");gint offset=gtk_text_iter_get_offset(&a);find_text(m,FALSE);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->find_status)),"Invalid regular expression"));find_all(m,2);gtk_text_buffer_get_iter_at_mark(b,&a,gtk_text_buffer_get_insert(b));g_assert_cmpint(gtk_text_iter_get_offset(&a),==,offset);g_assert_cmpuint(m->history->len,==,history);
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"(?<=α )foo\\d");gtk_text_buffer_get_start_iter(b,&a);gtk_text_buffer_place_cursor(b,&a);find_text(m,FALSE);regex_selection(m,"foo1");
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_case),TRUE);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo\\d+");GPtrArray *hits=regex_collect(m,b,100000,NULL,NULL,m->find_status);g_assert_nonnull(hits);g_assert_cmpuint(hits->len,==,2);g_ptr_array_unref(hits);
 gtk_text_buffer_get_iter_at_line(b,&a,1);gtk_text_buffer_place_cursor(b,&a);selection_search(m,2,0);g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_regex)));g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(m->find_entry)),==,"FOO22");assert_text(m,initial);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_regex),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_dotall),TRUE);GKeyFile *prefs=g_key_file_new();view_save(m,prefs);GKeyFile *copied=g_key_file_new();view_copy(prefs,copied);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_regex),FALSE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_dotall),FALSE);view_restore(m,copied);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_regex)));g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_dotall)));g_key_file_unref(copied);g_key_file_unref(prefs);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_regex),FALSE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_dotall),FALSE);gtk_widget_destroy(m->window);flush();
}
static void regex_replace_test(void) {
 Merge *m=merge_new(NULL);GtkTextBuffer *b=result(m);GtkTextIter a,z;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_regex),TRUE);
 const gchar *initial="item=ёж12 item=AB3\r\nitem=х4";gtk_text_buffer_set_text(b,initial,-1);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"item=(?<name>\\p{L}+)(\\d+)");gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"\\g<2>:\\g<name>");guint history=m->history->len;
 replace_result(NULL,m);assert_text(m,initial);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->replace_status)),==,"Preview: 3 replacements");g_assert_cmpuint(m->history->len,==,history);replace_result(m->replace_apply,m);assert_text(m,"12:ёж 3:AB\r\n4:х");g_assert_cmpuint(m->history->len,==,history+1);undo(NULL,m);assert_text(m,initial);redo(NULL,m);assert_text(m,"12:ёж 3:AB\r\n4:х");undo(NULL,m);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->replace_once),TRUE);replace_result(m->replace_apply,m);assert_text(m,"12:ёж item=AB3\r\n4:х");undo(NULL,m);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->replace_once),FALSE);
 gtk_combo_box_set_active(GTK_COMBO_BOX(m->replace_scope),1);gtk_text_buffer_get_iter_at_line(b,&a,0);z=a;gtk_text_iter_forward_to_line_end(&z);gtk_text_buffer_select_range(b,&a,&z);replace_result(m->replace_apply,m);assert_text(m,"12:ёж 3:AB\r\nitem=х4");undo(NULL,m);gtk_combo_box_set_active(GTK_COMBO_BOX(m->replace_scope),0);
 gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"\\0");history=m->history->len;replace_result(m->replace_apply,m);assert_text(m,initial);g_assert_cmpuint(m->history->len,==,history);
 const gchar *bad[]={"\\q","\\9","\\g<missing>","\\x00","\\00","\\"};for(guint i=0;i<G_N_ELEMENTS(bad);i++) { gtk_entry_set_text(GTK_ENTRY(m->replace_entry),bad[i]);replace_result(m->replace_apply,m);assert_text(m,initial);g_assert_cmpuint(m->history->len,==,history);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->replace_status)),"Invalid replacement")); }
 gtk_text_buffer_set_text(b,"FOO foo Foo",-1);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"(?<stem>foo)");gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"\\g<stem>x");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->replace_case),TRUE);replace_result(m->replace_apply,m);assert_text(m,"FOOX foox Foox");undo(NULL,m);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->replace_case),FALSE);
 gtk_text_buffer_set_text(b,"A\r\nёж",-1);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"^");gtk_entry_set_text(GTK_ENTRY(m->replace_entry),">");history=m->history->len;replace_result(m->replace_apply,m);assert_text(m,">A\r\n>ёж");g_assert_cmpuint(m->history->len,==,history+1);undo(NULL,m);
 gtk_combo_box_set_active(GTK_COMBO_BOX(m->replace_scope),1);gtk_text_buffer_get_start_iter(b,&a);gtk_text_buffer_get_iter_at_line(b,&z,1);gtk_text_buffer_select_range(b,&a,&z);replace_result(m->replace_apply,m);assert_text(m,">A\r\nёж");undo(NULL,m);gtk_combo_box_set_active(GTK_COMBO_BOX(m->replace_scope),0);
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"$");replace_result(m->replace_apply,m);assert_text(m,"A>\r\nёж>");undo(NULL,m);
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"ёж");gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"\\t\\0\\n\\\\");replace_result(m->replace_apply,m);assert_text(m,"A\r\n\tёж\n\\");undo(NULL,m);
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"(\\C)\\C");gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"\\1");history=m->history->len;replace_result(m->replace_apply,m);assert_text(m,"A\r\nёж");g_assert_cmpuint(m->history->len,==,history);
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"(");history=m->history->len;replace_result(m->replace_apply,m);assert_text(m,"A\r\nёж");g_assert_cmpuint(m->history->len,==,history);
 gtk_text_buffer_set_text(b,"foo foo foo foo foo foo foo foo foo foo foo foo foo foo foo foo foo foo foo foo",-1);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo");gchar *large=g_strnfill(60000,'x');gtk_entry_set_text(GTK_ENTRY(m->replace_entry),large);g_free(large);history=m->history->len;replace_result(m->replace_apply,m);g_assert_cmpuint(m->history->len,==,history);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->replace_status)),"Nothing changed"));
 m->busy=TRUE;gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"blocked");replace_result(m->replace_apply,m);g_assert_cmpuint(m->history->len,==,history);m->busy=FALSE;
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_regex),FALSE);gtk_widget_destroy(m->window);flush();
}
static void regex_limits_test(void) {
 Merge *m=merge_new(NULL);GtkTextBuffer *b=result(m);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_regex),TRUE);gtk_text_buffer_set_text(b,"ёж foo",-1);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"\\C");GPtrArray *hits=regex_collect(m,b,100000,NULL,NULL,m->find_status);g_assert_null(hits);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->find_status)),"UTF-8"));
 gchar *large=g_strnfill(8193,'x');gtk_entry_set_text(GTK_ENTRY(m->find_entry),large);g_free(large);hits=regex_collect(m,b,100000,NULL,NULL,m->find_status);g_assert_null(hits);
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"x");large=g_strnfill(1024*1024+1,'x');gtk_text_buffer_set_text(b,large,-1);g_free(large);hits=regex_collect(m,b,100000,NULL,NULL,m->find_status);g_assert_null(hits);
 large=g_strnfill(100001,'x');gtk_text_buffer_set_text(b,large,-1);g_free(large);hits=regex_collect(m,b,100000,NULL,NULL,m->find_status);g_assert_null(hits);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->find_status)),"match limit"));gboolean truncated=FALSE;hits=regex_collect(m,b,5000,NULL,&truncated,m->find_status);g_assert_nonnull(hits);g_assert_cmpuint(hits->len,==,5000);g_assert_true(truncated);g_ptr_array_unref(hits);
 large=g_strnfill(5000,'a');gchar *hard=g_strconcat(large,"!",NULL);gtk_text_buffer_set_text(b,hard,-1);g_free(hard);g_free(large);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"(*NO_AUTO_POSSESS)(a+)+$");hits=regex_collect(m,b,100000,NULL,NULL,m->find_status);g_assert_null(hits);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->find_status)),"work limit"));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_regex),FALSE);gtk_widget_destroy(m->window);flush();
}
static void aligned_geometry_test(void) {
 Merge *m=merge_new(NULL);gtk_window_resize(GTK_WINDOW(m->window),1200,1000);flush();
 GtkWidget *split=named(m->window,"insight-diff-source-split"),*merge=named(m->window,"insight-diff-merge-split");g_assert_true(GTK_IS_PANED(split));g_assert_true(GTK_IS_PANED(merge));g_assert_cmpint(gtk_orientable_get_orientation(GTK_ORIENTABLE(split)),==,GTK_ORIENTATION_HORIZONTAL);g_assert_cmpint(gtk_orientable_get_orientation(GTK_ORIENTABLE(merge)),==,GTK_ORIENTATION_VERTICAL);
 gint x[3],y[3];for(guint n=0;n<3;n++) g_assert_true(gtk_widget_translate_coordinates(m->views[n],m->window,0,0,&x[n],&y[n]));g_assert_cmpint(x[0],<,x[1]);g_assert_cmpint(ABS(y[0]-y[1]),<=,2);g_assert_cmpint(y[2],>,y[0]);
 GString *left=g_string_new("start\n"),*right=g_string_new("start\n");for(guint n=0;n<15;n++) g_string_append_printf(right,"inserted %u\n",n);g_string_append(left,"anchor\n");g_string_append(right,"anchor\n");for(guint n=0;n<100;n++) { g_string_append_printf(left,"equal %u\n",n);g_string_append_printf(right,"equal %u\n",n); }
 Comparison *job=g_new0(Comparison,1);job->text[0]=g_strdup(left->str);job->text[1]=g_strdup(right->str);m->cancel=g_cancellable_new();busy(m,TRUE);GTask *task=g_task_new(m->window,m->cancel,comparison_done,NULL);g_task_set_task_data(task,job,comparison_free);g_task_run_in_thread(task,comparison_worker);g_object_unref(task);wait_comparison(m);flush();g_assert_true(m->aligned);
 GtkTextIter it[2];GdkRectangle rect[2];for(guint n=0;n<2;n++) { GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[n]));gtk_text_buffer_get_iter_at_line(b,&it[n],n?16:1);gtk_text_view_get_iter_location(GTK_TEXT_VIEW(m->views[n]),&it[n],&rect[n]);gchar *text=contents(b);g_assert_cmpstr(text,==,n?right->str:left->str);g_free(text); }g_assert_cmpint(ABS(rect[0].y-rect[1].y),<=,1);
 gtk_adjustment_set_value(m->adjustments[1],rect[1].y+100);flush();g_assert_cmpfloat(ABS(gtk_adjustment_get_value(m->adjustments[0])-gtk_adjustment_get_value(m->adjustments[1])),<,2.0);
 gtk_adjustment_set_value(m->adjustments[0],rect[0].y+200);flush();g_assert_cmpfloat(ABS(gtk_adjustment_get_value(m->adjustments[0])-gtk_adjustment_get_value(m->adjustments[1])),<,2.0);
 gtk_adjustment_set_value(m->adjustments[0],0);flush();if(g_getenv("INSIGHT_ALIGNED_DIFF_CAPTURE")) { gtk_widget_queue_draw(m->window);gint64 until=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<until) { flush();g_usleep(1000); }GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(m->window),0,0,gtk_widget_get_allocated_width(m->window),gtk_widget_get_allocated_height(m->window));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("INSIGHT_ALIGNED_DIFF_CAPTURE"),"jpeg",NULL,"quality","90",NULL));g_object_unref(image); }
 g_string_free(left,TRUE);g_string_free(right,TRUE);gtk_widget_destroy(m->window);flush();g_clear_pointer(&view_defaults,g_key_file_unref);g_print("Aligned diff geometry, equal-line pixel anchors, bidirectional scrolling and exact source bytes passed\n");
}
int main(int argc,char **argv) {
 g_setenv("GSETTINGS_BACKEND","memory",TRUE);gtk_init(&argc,&argv);line_navigation_test();search_options_test();replace_test();replace_scope_test();confirm_replace_test();case_test();line_edit_test();history_test();export_test();whitespace_test();search_actions_test();search_output_test();search_preview_test();output_limit_test();regex_search_test();regex_replace_test();regex_limits_test();insight_diff_set_source_opener(source_opened);
 GError *error=NULL;gchar *dir=g_dir_make_tmp("insight-diff-XXXXXX",&error);g_assert_no_error(error);
 gchar *left=g_build_filename(dir,"left.txt",NULL),*right=g_build_filename(dir,"right.txt",NULL);
 const char *a="start\nleft\ncommon\nlast left\nend\n",*b="start\nright\ncommon\nlast right\nextra\nend\n";
 g_assert_true(g_file_set_contents(left,a,-1,&error));g_assert_no_error(error);
 g_assert_true(g_file_set_contents(right,b,-1,&error));g_assert_no_error(error);
 Merge *m=merge_new(NULL);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->visible_space),FALSE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->visible_eol),FALSE);flush();g_assert_false(gtk_widget_get_sensitive(m->next));
 gtk_entry_set_text(GTK_ENTRY(m->paths[0]),left);gtk_entry_set_text(GTK_ENTRY(m->paths[1]),right);
 compare(NULL,m);wait_comparison(m);g_assert_cmpuint(m->diff.count,==,2);g_assert_cmpint(selected(m),==,0);
 g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->headers[0])),left));g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->headers[1])),right));g_assert_nonnull(strstr(gtk_tree_view_column_get_title(m->file_columns[0]),left));g_assert_cmpint(gtk_tree_view_get_n_columns(GTK_TREE_VIEW(m->hunk_view)),==,3);g_assert_cmpuint(m->resolutions[0],==,0);
 take(m->take[0],m);g_assert_cmpuint(m->resolutions[0],==,1);undo(NULL,m);g_assert_cmpuint(m->resolutions[0],==,0);take(m->take[1],m);g_assert_cmpuint(m->resolutions[0],==,2);GtkTextIter edit;gtk_text_buffer_get_iter_at_mark(result(m),&edit,m->marks[0]);gtk_text_buffer_insert(result(m),&edit,"manual ",-1);g_assert_cmpuint(m->resolutions[0],==,3);g_assert_cmpuint(m->resolutions[1],==,0);undo(NULL,m);g_assert_cmpuint(m->resolutions[0],==,2);undo(NULL,m);g_assert_cmpuint(m->resolutions[0],==,0);history_end(m,FALSE);assert_text(m,a);
 gtk_combo_box_set_active(GTK_COMBO_BOX(m->destination_kind),1);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(m->destination)),==,right);gtk_combo_box_set_active(GTK_COMBO_BOX(m->destination_kind),2);gtk_entry_set_text(GTK_ENTRY(m->destination),"separate-output.txt");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->headers[2])),"separate-output.txt"));gtk_entry_set_text(GTK_ENTRY(m->destination),"");

 g_assert_false(gtk_widget_get_sensitive(m->previous));g_assert_true(gtk_widget_get_sensitive(m->next));
 step(m->next,m);g_assert_cmpint(selected(m),==,1);step(m->next,m);g_assert_cmpint(selected(m),==,1);
 g_assert_false(gtk_widget_get_sensitive(m->next));step(m->previous,m);g_assert_cmpint(selected(m),==,0);
 GtkTextIter click;GtkTextBuffer *left_buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[0]));
 gtk_text_buffer_get_iter_at_line(left_buffer,&click,3);GdkRectangle where;gtk_text_view_get_iter_location(GTK_TEXT_VIEW(m->views[0]),&click,&where);
 gint x,y;gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(m->views[0]),GTK_TEXT_WINDOW_TEXT,where.x,where.y+where.height/2,&x,&y);
 GdkEventButton mouse={0};mouse.button=1;mouse.window=gtk_text_view_get_window(GTK_TEXT_VIEW(m->views[0]),GTK_TEXT_WINDOW_TEXT);mouse.x=x;mouse.y=y;
 g_assert_true(pick_block(m->views[0],&mouse,m));g_assert_cmpint(selected(m),==,1);navigate(m,-1);
 GdkEventKey event={0};event.state=GDK_CONTROL_MASK;event.keyval=GDK_KEY_Down;
 g_assert_true(key(m->views[0],&event,m));g_assert_cmpint(selected(m),==,1);
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"left");gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"replacement");
 replace_result(m->replace_apply,m);g_assert_cmpuint(m->history->len,==,2);undo(NULL,m);assert_text(m,a);
 take(m->take[1],m);g_assert_cmpuint(m->history->len,==,2);undo(NULL,m);assert_text(m,a);redo(NULL,m);
 navigate(m,-1);take(m->take[1],m);assert_text(m,b);undo(NULL,m);undo(NULL,m);assert_text(m,a);history_end(m,TRUE);assert_text(m,b);history_end(m,FALSE);assert_text(m,a);
 gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),1);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"RIGHT");
 GtkTextBuffer *right_buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[1]));GtkTextIter start,end;
 gtk_text_buffer_get_start_iter(right_buffer,&start);gtk_text_buffer_place_cursor(right_buffer,&start);
 find_text(m,FALSE);g_assert_true(gtk_text_buffer_get_selection_bounds(right_buffer,&start,&end));
 gchar *match=gtk_text_buffer_get_text(right_buffer,&start,&end,TRUE);g_assert_cmpstr(match,==,"right");g_free(match);
 g_assert_cmpint(gtk_text_iter_get_line(&start),==,1);find_text(m,TRUE);
 g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->find_status)),"Wrapped"));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_case),TRUE);find_text(m,FALSE);
 g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->find_status)),==,"Text not found.");
 gtk_widget_grab_focus(m->views[2]);event.keyval=GDK_KEY_f;event.state=GDK_CONTROL_MASK;
 g_assert_true(key(m->views[2],&event,m));g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(m->find_side)),==,2);
 g_assert_true(gtk_widget_has_focus(m->find_entry));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->numbers),FALSE);
 for(int i=0;i<3;i++) g_assert_cmpint(gtk_text_view_get_border_window_size(GTK_TEXT_VIEW(m->views[i]),GTK_TEXT_WINDOW_LEFT),==,0);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->numbers),TRUE);flush();
 for(int i=0;i<3;i++) g_assert_cmpint(gtk_text_view_get_border_window_size(GTK_TEXT_VIEW(m->views[i]),GTK_TEXT_WINDOW_LEFT),>,0);
 copy_difference(m,1);gchar *copied=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(copied,==,"right\n");g_free(copied);
 GtkWidget *popup=gtk_menu_new();source_menu(GTK_TEXT_VIEW(m->views[0]),popup,m);g_assert_nonnull(popup);gtk_widget_destroy(popup);
 gtk_entry_set_text(GTK_ENTRY(m->paths[1]),"not-the-loaded-source");GtkTextIter source_line;gtk_text_buffer_get_iter_at_line(right_buffer,&source_line,2);gtk_text_buffer_place_cursor(right_buffer,&source_line);edit_source(m,1);g_assert_cmpstr(editor_path,==,right);g_assert_cmpuint(editor_line,==,3);gtk_entry_set_text(GTK_ENTRY(m->paths[1]),right);
 guint display_history=m->position;gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),120);flush();
 for(gint i=0;i<3;i++) { PangoFontDescription *font=NULL;gtk_style_context_get(gtk_widget_get_style_context(m->views[i]),GTK_STATE_FLAG_NORMAL,"font",&font,NULL);g_assert_cmpint(pango_font_description_get_size(font),==,12*PANGO_SCALE);pango_font_description_free(font); }
 GdkEventScroll wheel={0};wheel.state=GDK_CONTROL_MASK;wheel.direction=GDK_SCROLL_SMOOTH;wheel.delta_y=-1;g_assert_true(zoom_scroll(m->views[0],&wheel,m));g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->zoom)),==,130);wheel.state=0;g_assert_false(zoom_scroll(m->views[0],&wheel,m));
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),300);g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->zoom)),==,250);gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),100);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->vertical),FALSE);for(gint i=0;i<3;i++) { GtkPolicyType h,v;gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(m->scrollers[i]),&h,&v);g_assert_cmpint(v,==,GTK_POLICY_NEVER); }gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->vertical),TRUE);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->mono),FALSE);g_assert_false(gtk_text_view_get_monospace(GTK_TEXT_VIEW(m->views[0])));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->mono),TRUE);
 g_idle_add(options_response,GINT_TO_POINTER(GTK_RESPONSE_CANCEL));view_options(NULL,m);g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->zoom)),==,100);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->numbers)));g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->visible_space)));
 g_idle_add(options_response,GINT_TO_POINTER(GTK_RESPONSE_APPLY));view_options(NULL,m);g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->zoom)),==,130);g_assert_false(gtk_widget_get_visible(m->list_scroll));g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->numbers)));g_assert_cmpuint(m->position,==,display_history);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->visible_space)));g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->visible_eol)));assert_text(m,a);
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),100);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->numbers),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->list_toggle),TRUE);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->overview),TRUE);gint64 map_deadline=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<map_deadline) { flush();g_usleep(1000); }g_assert_cmpint(gtk_widget_get_allocated_height(m->maps[0]),>,20);for(gint i=0;i<2;i++) { g_assert_true(gtk_widget_get_visible(m->maps[i]));g_assert_cmpuint(m->map_blocks[i]->len,==,m->diff.count); }
 /* EOF changes in a long file must remain visible and clickable, rather
  * than having their minimum-height marker clipped below the overview. */
 gdouble marker_top,marker_span;map_extent((MapBlock){999999,1000000},1000000,200,&marker_top,&marker_span);g_assert_cmpfloat(marker_top+marker_span,<=,200);g_assert_cmpfloat(marker_span,>=,3);
 map_extent((MapBlock){1000000,1000000},1000001,200,&marker_top,&marker_span);g_assert_cmpfloat(marker_top+marker_span,<=,200);g_assert_cmpfloat(marker_span,>=,3);
 MapBlock marker=g_array_index(m->map_blocks[0],MapBlock,1);GdkEventButton map_event={0};map_event.button=1;map_event.y=(marker.begin+.2)/m->map_lines[0]*gtk_widget_get_allocated_height(m->maps[0]);g_assert_true(map_click(m->maps[0],&map_event,m));g_assert_cmpint(selected(m),==,1);navigate(m,-1);g_assert_cmpuint(m->position,==,display_history);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->overview),FALSE);g_assert_false(gtk_widget_get_visible(m->maps[0]));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->overview),TRUE);
 const gchar *capture=g_getenv("INSIGHT_DIFF_CAPTURE");
 if(capture) {
  gtk_entry_set_text(GTK_ENTRY(m->find_entry),"right");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_case),FALSE);
  gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),1);find_text(m,FALSE);flush();
  gint64 drawn=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<drawn) { flush();g_usleep(1000); }
  GdkWindow *window=gtk_widget_get_window(m->window);
  GdkPixbuf *pixels=gdk_pixbuf_get_from_window(window,0,0,gdk_window_get_width(window),gdk_window_get_height(window));g_assert_nonnull(pixels);
  g_assert_true(gdk_pixbuf_save(pixels,capture,"jpeg",&error,"quality","88",NULL));g_assert_no_error(error);g_object_unref(pixels);
 }
 guint before_all=m->position;take_all(m->take_all[0],m);g_assert_cmpuint(m->position,==,before_all);
 take_all(m->take_all[1],m);assert_text(m,b);g_assert_cmpuint(m->position,==,before_all+1);undo(NULL,m);assert_text(m,a);redo(NULL,m);assert_text(m,b);
 take_all(m->take_all[0],m);assert_text(m,a);undo(NULL,m);assert_text(m,b);undo(NULL,m);assert_text(m,a);
 event.state=GDK_MOD1_MASK|GDK_SHIFT_MASK;event.keyval=GDK_KEY_Right;g_assert_true(key(m->views[2],&event,m));assert_text(m,b);undo(NULL,m);assert_text(m,a);
 /* Edits outside the blocks survive a batch. */
 GtkTextIter insertion;gtk_text_buffer_get_end_iter(result(m),&insertion);gtk_text_buffer_insert(result(m),&insertion,"manual tail\n",-1);
 take_all(m->take_all[1],m);gchar *with_tail=g_strconcat(b,"manual tail\n",NULL);assert_text(m,with_tail);g_free(with_tail);undo(NULL,m);undo(NULL,m);assert_text(m,a);
 /* Collapsing boundaries must reject a batch without mutating text/history. */
 GtkTextIter zero,last;gtk_text_buffer_get_bounds(result(m),&zero,&last);gtk_text_buffer_delete(result(m),&zero,&last);
 guint collapsed=m->position;take_all(m->take_all[1],m);assert_text(m,"");g_assert_cmpuint(m->position,==,collapsed);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->status)),"boundaries"));undo(NULL,m);assert_text(m,a);
 GtkWidget *toggle=gtk_check_button_new();g_object_ref_sink(toggle);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle),TRUE);wrap(GTK_TOGGLE_BUTTON(toggle),m);
 for(int i=0;i<3;i++) g_assert_cmpint(gtk_text_view_get_wrap_mode(GTK_TEXT_VIEW(m->views[i])),==,i==2?GTK_WRAP_WORD_CHAR:GTK_WRAP_NONE);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle),FALSE);show_list(GTK_TOGGLE_BUTTON(toggle),m);
 g_assert_false(gtk_widget_get_visible(m->list_scroll));navigate(m,1);g_assert_cmpint(selected(m),==,1);
 gtk_tree_selection_unselect_all(m->selection);navigate(m,-1);g_assert_cmpint(selected(m),==,1);
 g_object_unref(toggle);
 gtk_entry_set_text(GTK_ENTRY(m->paths[0]),left);gtk_entry_set_text(GTK_ENTRY(m->paths[1]),right);
 g_assert_true(g_file_set_contents(left,"a b\r\nя\r\n",-1,&error));g_assert_no_error(error);
 g_assert_true(g_file_set_contents(right,"ab\nя\n",-1,&error));g_assert_no_error(error);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->ignore_space),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->ignore_eol),TRUE);
 compare(NULL,m);wait_comparison(m);g_assert_cmpuint(m->diff.count,==,0);assert_text(m,"a b\r\nя\r\n");
 gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),2);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"Я");
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_case),FALSE);find_text(m,FALSE);
 g_assert_true(gtk_text_buffer_get_selection_bounds(result(m),&start,&end));
 match=gtk_text_buffer_get_text(result(m),&start,&end,TRUE);g_assert_cmpstr(match,==,"я");g_free(match);
 guint history=m->position;event.keyval=GDK_KEY_F3;event.state=GDK_SHIFT_MASK;g_assert_true(key(m->find_entry,&event,m));
 g_assert_cmpuint(history,==,m->position);
 compare(NULL,m);g_assert_true(m->busy);cancel_compare(NULL,m);wait_comparison(m);
 assert_text(m,"a b\r\nя\r\n");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->status)),"cancel"));
 gtk_entry_set_text(GTK_ENTRY(m->paths[1]),dir);compare(NULL,m);wait_comparison(m);
 assert_text(m,"a b\r\nя\r\n");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->status)),"regular"));
 gtk_entry_set_text(GTK_ENTRY(m->paths[1]),right);
 g_assert_true(g_file_set_contents(right,"\xff",1,&error));g_assert_no_error(error);compare(NULL,m);wait_comparison(m);
 assert_text(m,"a b\r\nя\r\n");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->status)),"UTF-8"));
 gchar *oversized=g_strnfill(1024*1024+1,'x');g_assert_true(g_file_set_contents(right,oversized,-1,&error));g_assert_no_error(error);g_free(oversized);
 compare(NULL,m);g_assert_false(gtk_widget_get_sensitive(m->views[2]));g_assert_false(gtk_widget_get_sensitive(m->save_button));wait_comparison(m);g_assert_true(gtk_widget_get_sensitive(m->save_button));
 assert_text(m,"a b\r\nя\r\n");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->status)),"1 MiB"));
 GString *scroll_left=g_string_new(NULL),*scroll_right=g_string_new(NULL);
 for(int i=0;i<300;i++) {
  if(i==150) for(int j=0;j<20;j++) g_string_append(scroll_right,"inserted\n");
  g_string_append_printf(scroll_left,"common %d\n",i);g_string_append_printf(scroll_right,"common %d\n",i);
 }
 g_assert_true(g_file_set_contents(left,scroll_left->str,scroll_left->len,&error));g_assert_no_error(error);
 g_assert_true(g_file_set_contents(right,scroll_right->str,scroll_right->len,&error));g_assert_no_error(error);
 compare(NULL,m);wait_comparison(m);g_string_free(scroll_left,TRUE);g_string_free(scroll_right,TRUE);
 gtk_text_buffer_get_iter_at_line(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[0])),&start,220);
 gint scroll_y,line_height;gtk_text_view_get_line_yrange(GTK_TEXT_VIEW(m->views[0]),&start,&scroll_y,&line_height);
 gtk_adjustment_set_value(m->adjustments[0],scroll_y);flush();
 GdkRectangle visible;gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(m->views[1]),&visible);
 gtk_text_view_get_line_at_y(GTK_TEXT_VIEW(m->views[1]),&start,visible.y,NULL);g_assert_cmpint(gtk_text_iter_get_line(&start),==,240);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->sync),FALSE);gdouble independent=gtk_adjustment_get_value(m->adjustments[1]);
 gtk_adjustment_set_value(m->adjustments[0],0);flush();g_assert_cmpfloat(gtk_adjustment_get_value(m->adjustments[1]),==,independent);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->sync),TRUE);
 GString *large_left=g_string_new(NULL),*large_right=g_string_new(NULL);
 for(int i=0;i<1998;i++) { g_string_append(large_left,"left line\n");g_string_append(large_right,"right line\n"); }
 g_assert_true(g_file_set_contents(left,large_left->str,large_left->len,&error));g_assert_no_error(error);
 g_assert_true(g_file_set_contents(right,large_right->str,large_right->len,&error));g_assert_no_error(error);
 guint beats=0;guint timer=g_timeout_add(1,heartbeat,&beats);compare(NULL,m);wait_comparison(m);g_source_remove(timer);
 g_assert_cmpuint(beats,>,0);g_assert_cmpuint(m->diff.count,==,1);assert_text(m,large_left->str);
 g_string_free(large_left,TRUE);g_string_free(large_right,TRUE);
 insight_diff_show_files(NULL,left,right,3);GList *windows=gtk_window_list_toplevels();Merge *opened=NULL;
 for(GList *p=windows;p;p=p->next) { Merge *candidate=g_object_get_data(G_OBJECT(p->data),"insight-diff-state");if(candidate && candidate!=m) opened=candidate; }
 g_list_free(windows);g_assert_nonnull(opened);wait_comparison(opened);
 g_assert_cmpstr(opened->loaded_paths[0],==,left);g_assert_cmpstr(opened->loaded_paths[1],==,right);
 g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opened->ignore_space)));gtk_widget_destroy(opened->window);flush();
 gboolean gone=FALSE;g_object_weak_ref(G_OBJECT(m->window),finalized,&gone);
 compare(NULL,m);gtk_widget_destroy(m->window);
 gint64 until=g_get_monotonic_time()+10*G_TIME_SPAN_SECOND;
 while(!gone && g_get_monotonic_time()<until) { flush();g_usleep(1000); }
 g_assert_true(gone);flush();random_batches(left,right);
 GKeyFile *prefs=g_key_file_new();Merge *custom=merge_new(NULL);gtk_spin_button_set_value(GTK_SPIN_BUTTON(custom->zoom),140);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(custom->overview),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(custom->vertical),FALSE);insight_diff_write(prefs);gtk_widget_destroy(custom->window);insight_diff_read(prefs);
 custom=merge_new(NULL);g_assert_false(custom->busy);g_assert_null(custom->sources[0]);g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(custom->zoom)),==,140);g_assert_true(gtk_widget_get_visible(custom->maps[0]));g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(custom->vertical)));gtk_widget_destroy(custom->window);
 g_key_file_set_integer(prefs,view_group,"zoom",999);g_key_file_set_value(prefs,view_group,"numbers","broken");g_key_file_set_integer(prefs,view_group,"width",-1);insight_diff_read(prefs);custom=merge_new(NULL);g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(custom->zoom)),==,100);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(custom->numbers)));
 gboolean options_closed=FALSE;g_object_weak_ref(G_OBJECT(custom->window),finalized,&options_closed);g_idle_add(close_options_parent,custom->window);view_options(NULL,custom);flush();g_assert_true(options_closed);g_assert_null(open_compares);g_key_file_unref(prefs);g_clear_pointer(&view_defaults,g_key_file_unref);
 /* Arbitrary Unicode source selection can replace a Result selection in AI review too. */
 Merge *partial=merge_new(NULL);partial->memory=TRUE;GtkTextBuffer *partial_source=gtk_text_view_get_buffer(GTK_TEXT_VIEW(partial->views[1]));gtk_text_buffer_set_text(partial_source,"alpha идея omega",-1);gtk_text_buffer_set_text(result(partial),"old code",-1);GtkTextIter pa,pz;gtk_text_buffer_get_iter_at_offset(partial_source,&pa,6);gtk_text_buffer_get_iter_at_offset(partial_source,&pz,10);gtk_text_buffer_select_range(partial_source,&pa,&pz);g_assert_cmpstr(partial->picked[1],==,"идея");gtk_text_buffer_place_cursor(partial_source,&pz);gtk_text_buffer_get_iter_at_offset(result(partial),&pa,0);gtk_text_buffer_get_iter_at_offset(result(partial),&pz,3);gtk_text_buffer_select_range(result(partial),&pa,&pz);GtkWidget *partial_button=gtk_button_new();g_object_ref_sink(partial_button);g_object_set_data(G_OBJECT(partial_button),"side",GUINT_TO_POINTER(1));guint prior=partial->position;merge_source_selection(partial_button,partial);assert_text(partial,"идея code");g_assert_cmpuint(partial->position,==,prior+1);undo(NULL,partial);assert_text(partial,"old code");redo(NULL,partial);assert_text(partial,"идея code");partial->review_locked=TRUE;merge_source_selection(partial_button,partial);assert_text(partial,"идея code");gtk_widget_destroy(partial_button);g_object_unref(partial_button);gtk_widget_destroy(partial->window);
 aligned_geometry_test();
 g_remove(left);g_remove(right);g_rmdir(dir);g_free(left);g_free(right);g_free(dir);g_free(editor_path);
 g_print("Production Diff/Merge fixture: navigation, search, gutters, merge history, options, worker cancellation and close passed.\n");return 0;
}
