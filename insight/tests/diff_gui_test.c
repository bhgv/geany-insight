/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Isolated fixture using the production window and callbacks, not libgeany. */
#include <stdarg.h>
#include <glib/gstdio.h>
#include "../../src/insight-diff.c"
void insight_directory_show(GtkWidget *parent) { (void)parent; }
gchar *insight_i18n_printf(const gchar *format,...) {
 va_list args;va_start(args,format);gchar *s=g_strdup_vprintf(format,args);va_end(args);return s;
}
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
static void replace_test(void) {
 Merge *m=merge_new(NULL);GtkTextBuffer *b=result(m);GtkTextIter a,z;
 const gchar *initial="foo foo_bar FOO\r\nfoo foo\nяfoo foo";gtk_text_buffer_set_text(b,initial,-1);
 gtk_entry_set_text(GTK_ENTRY(m->find_entry),"foo");gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"ёж");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_word),TRUE);
 guint history=m->history->len;replace_result(NULL,m);assert_text(m,initial);g_assert_cmpuint(m->history->len,==,history);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->replace_status)),==,"Preview: 5 replacements");
 replace_result(m->replace_apply,m);assert_text(m,"ёж foo_bar ёж\r\nёж ёж\nяfoo ёж");g_assert_cmpuint(m->history->len,==,history+1);undo(NULL,m);assert_text(m,initial);redo(NULL,m);undo(NULL,m);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->replace_once),TRUE);replace_result(m->replace_apply,m);assert_text(m,"ёж foo_bar FOO\r\nёж foo\nяfoo ёж");undo(NULL,m);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->replace_once),FALSE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->replace_selection),TRUE);
 gtk_text_buffer_get_iter_at_line(b,&a,1);z=a;gtk_text_iter_forward_to_line_end(&z);gtk_text_buffer_select_range(b,&a,&z);
 gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"");replace_result(m->replace_apply,m);assert_text(m,"foo foo_bar FOO\r\n \nяfoo foo");undo(NULL,m);
 gtk_text_buffer_get_start_iter(b,&a);gtk_text_buffer_place_cursor(b,&a);replace_result(m->replace_apply,m);assert_text(m,initial);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->replace_status)),==,"Select result text first.");
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->replace_selection),FALSE);GString *many=g_string_new(NULL);for(guint i=0;i<100;i++) g_string_append(many,"foo ");gtk_text_buffer_set_text(b,many->str,-1);gchar *huge=g_strnfill(60000,'x');gtk_entry_set_text(GTK_ENTRY(m->replace_entry),huge);g_free(huge);replace_result(m->replace_apply,m);assert_text(m,many->str);g_string_free(many,TRUE);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->replace_status)),"Nothing changed"));gtk_text_buffer_set_text(b,initial,-1);
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
int main(int argc,char **argv) {
 gtk_init(&argc,&argv);line_navigation_test();search_options_test();replace_test();case_test();line_edit_test();insight_diff_set_source_opener(source_opened);
 GError *error=NULL;gchar *dir=g_dir_make_tmp("insight-diff-XXXXXX",&error);g_assert_no_error(error);
 gchar *left=g_build_filename(dir,"left.txt",NULL),*right=g_build_filename(dir,"right.txt",NULL);
 const char *a="start\nleft\ncommon\nlast left\nend\n",*b="start\nright\ncommon\nlast right\nextra\nend\n";
 g_assert_true(g_file_set_contents(left,a,-1,&error));g_assert_no_error(error);
 g_assert_true(g_file_set_contents(right,b,-1,&error));g_assert_no_error(error);
 Merge *m=merge_new(NULL);flush();g_assert_false(gtk_widget_get_sensitive(m->next));
 gtk_entry_set_text(GTK_ENTRY(m->paths[0]),left);gtk_entry_set_text(GTK_ENTRY(m->paths[1]),right);
 compare(NULL,m);wait_comparison(m);g_assert_cmpuint(m->diff.count,==,2);g_assert_cmpint(selected(m),==,0);
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
 navigate(m,-1);take(m->take[1],m);assert_text(m,b);undo(NULL,m);undo(NULL,m);assert_text(m,a);
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
 g_idle_add(options_response,GINT_TO_POINTER(GTK_RESPONSE_CANCEL));view_options(NULL,m);g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->zoom)),==,100);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->numbers)));
 g_idle_add(options_response,GINT_TO_POINTER(GTK_RESPONSE_APPLY));view_options(NULL,m);g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->zoom)),==,130);g_assert_false(gtk_widget_get_visible(m->list_scroll));g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->numbers)));g_assert_cmpuint(m->position,==,display_history);assert_text(m,a);
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
 for(int i=0;i<3;i++) g_assert_cmpint(gtk_text_view_get_wrap_mode(GTK_TEXT_VIEW(m->views[i])),==,GTK_WRAP_WORD_CHAR);
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
 g_remove(left);g_remove(right);g_rmdir(dir);g_free(left);g_free(right);g_free(dir);g_free(editor_path);
 g_print("Production Diff/Merge fixture: navigation, search, gutters, merge history, options, worker cancellation and close passed.\n");return 0;
}
