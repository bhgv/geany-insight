/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-terminal.h"
#include "../vendor/libvterm/include/vterm.h"
#include <gdk/gdkkeysyms.h>
#include <string.h>
#define STATE "insight-terminal-state"
#define HISTORY_BYTES (4u*1024u*1024u)
typedef struct { gint cols;VTermScreenCell cells[]; } TerminalLine;
typedef struct {
 GtkWidget *view;GtkIMContext *im;VTerm *vt;VTermScreen *screen;GQueue history;gsize history_bytes;
 InsightTerminalWrite write;gpointer data;GByteArray *batch;guint render;gboolean closed,rendering,trimmed,cursor_visible,alternate;
 gint rows,cols,cw,ch;GHashTable *tags;
} Terminal;
typedef struct { gint first,last;guint fg,bg,attrs; } PaintRun;
static Terminal *state(GtkWidget *view) { return view ? g_object_get_data(G_OBJECT(view),STATE):NULL; }
static void schedule(Terminal *t);
static void output(const char *bytes,size_t length,void *data) { Terminal *t=data;if(t->closed || !length) return;if(t->batch) g_byte_array_append(t->batch,(const guint8 *)bytes,length);else if(t->write) t->write(bytes,length,t->data); }
static int damage(VTermRect rect,void *data) { (void)rect;schedule(data);return 1; }
static int cursor_moved(VTermPos pos,VTermPos old,int visible,void *data) { (void)pos;(void)old;Terminal *t=data;t->cursor_visible=visible;schedule(t);return 1; }
static int property(VTermProp prop,VTermValue *value,void *data) { Terminal *t=data;if(prop==VTERM_PROP_ALTSCREEN) t->alternate=value->boolean;if(prop==VTERM_PROP_CURSORVISIBLE) t->cursor_visible=value->boolean;schedule(t);return 1; }
static int push_line(int cols,const VTermScreenCell *cells,void *data) {
 Terminal *t=data;gsize bytes=sizeof(TerminalLine)+(gsize)cols*sizeof(*cells);TerminalLine *line=g_malloc(bytes);line->cols=cols;memcpy(line->cells,cells,cols*sizeof(*cells));g_queue_push_tail(&t->history,line);t->history_bytes+=bytes;
 while(t->history.length>2000 || t->history_bytes>HISTORY_BYTES) { TerminalLine *old=g_queue_pop_head(&t->history);t->history_bytes-=sizeof(*old)+(gsize)old->cols*sizeof(*cells);g_free(old);t->trimmed=TRUE; }return 1;
}
static int pop_line(int cols,VTermScreenCell *cells,void *data) {
 Terminal *t=data;TerminalLine *line=g_queue_pop_tail(&t->history);if(!line) return 0;
 t->history_bytes-=sizeof(*line)+(gsize)line->cols*sizeof(*cells);memset(cells,0,cols*sizeof(*cells));memcpy(cells,line->cells,MIN(cols,line->cols)*sizeof(*cells));g_free(line);return 1;
}
static void free_line(gpointer line,gpointer data) { (void)data;g_free(line); }
static int clear_history(void *data) { Terminal *t=data;g_queue_foreach(&t->history,free_line,NULL);g_queue_clear(&t->history);t->history_bytes=0;t->trimmed=TRUE;return 1; }
static guint rgb(Terminal *t,VTermColor color) { vterm_screen_convert_color_to_rgb(t->screen,&color);return (color.rgb.red<<16)|(color.rgb.green<<8)|color.rgb.blue; }
static GtkTextTag *color_tag(Terminal *t,guint color,gboolean background) {
 guint key=(color<<1)|background;GtkTextTag *tag=g_hash_table_lookup(t->tags,GUINT_TO_POINTER(key+1));if(tag) return tag;
 if(g_hash_table_size(t->tags)>=4096) return NULL;
 GdkRGBA rgba={(color>>16)/255.,((color>>8)&255)/255.,(color&255)/255.,1};
 tag=gtk_text_buffer_create_tag(gtk_text_view_get_buffer(GTK_TEXT_VIEW(t->view)),NULL,background ? "background-rgba":"foreground-rgba",&rgba,NULL);g_hash_table_insert(t->tags,GUINT_TO_POINTER(key+1),tag);return tag;
}
static gboolean equal_run(PaintRun a,PaintRun b) { return a.fg==b.fg && a.bg==b.bg && a.attrs==b.attrs && a.last==b.first; }
static void append_row(Terminal *t,GString *text,GArray *runs,const VTermScreenCell *cells,gint cols,gint cursor_col,gint *characters,gint *caret) {
 gint last=cols-1;while(last>=0 && (!cells[last].chars[0] || cells[last].chars[0]==' ' || cells[last].chars[0]==(guint32)-1) && rgb(t,cells[last].bg)==0 && !cells[last].attrs.reverse) last--;
 last=MAX(last,cursor_col);
 for(gint col=0;col<=last && col<cols;col++) {
  VTermScreenCell cell=cells[col];if(col==cursor_col) *caret=*characters;
  if(cell.chars[0]==(guint32)-1) continue;
  PaintRun run={*characters,0,rgb(t,cell.fg),rgb(t,cell.bg),cell.attrs.bold|(cell.attrs.italic<<1)|(cell.attrs.underline ? 4:0)|(cell.attrs.strike<<3)};
  if(cell.attrs.reverse) { guint tmp=run.fg;run.fg=run.bg;run.bg=tmp; }
  if(cell.attrs.conceal) run.fg=run.bg;
  if(!cell.chars[0]) { g_string_append_c(text,' ');(*characters)++; }
  else for(guint k=0;k<VTERM_MAX_CHARS_PER_CELL && cell.chars[k];k++) { gchar utf8[6];gint n=g_unichar_to_utf8(cell.chars[k],utf8);g_string_append_len(text,utf8,n);(*characters)++; }
  run.last=*characters;
  if(runs->len && equal_run(g_array_index(runs,PaintRun,runs->len-1),run)) g_array_index(runs,PaintRun,runs->len-1).last=run.last;else g_array_append_val(runs,run);
 }
 g_string_append_c(text,'\n');(*characters)++;
}
static gboolean render(gpointer data) {
 Terminal *t=data;t->render=0;if(t->closed) return G_SOURCE_REMOVE;
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(t->view));GtkTextIter a,b;gboolean selected=gtk_text_buffer_get_selection_bounds(buffer,&a,&b);gint first=selected ? gtk_text_iter_get_offset(&a):0,last=selected ? gtk_text_iter_get_offset(&b):0;
 GtkAdjustment *adjustment=gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(t->view));gdouble old_scroll=gtk_adjustment_get_value(adjustment);gboolean bottom=old_scroll+gtk_adjustment_get_page_size(adjustment)>=gtk_adjustment_get_upper(adjustment)-2;
 GString *text=g_string_new(NULL);GArray *runs=g_array_new(FALSE,FALSE,sizeof(PaintRun));gint characters=0,caret=0;VTermPos cursor;vterm_state_get_cursorpos(vterm_obtain_state(t->vt),&cursor);
 for(GList *p=t->alternate ? NULL:t->history.head;p;p=p->next) { TerminalLine *line=p->data;append_row(t,text,runs,line->cells,line->cols,-1,&characters,&caret); }
 VTermScreenCell cells[300];gint used=cursor.row+1;
 for(gint row=t->rows-1;row>=used;row--) { gboolean content=FALSE;for(gint col=0;col<t->cols;col++) { VTermScreenCell cell={0};vterm_screen_get_cell(t->screen,(VTermPos){row,col},&cell);if((cell.chars[0] && cell.chars[0]!=' ' && cell.chars[0]!=(guint32)-1) || rgb(t,cell.bg) || cell.attrs.reverse) { content=TRUE;break; } }if(content) { used=row+1;break; } }
 for(gint row=0;row<used;row++) { for(gint col=0;col<t->cols;col++) { memset(&cells[col],0,sizeof(cells[col]));vterm_screen_get_cell(t->screen,(VTermPos){row,col},&cells[col]); }append_row(t,text,runs,cells,t->cols,row==cursor.row ? cursor.col:-1,&characters,&caret); }
 /* Preserve native marks and selection during streaming output. Only replace
  * the changed UTF-8 span, instead of clearing/rebuilding the entire buffer. */
 gtk_text_buffer_get_bounds(buffer,&a,&b);gchar *old=gtk_text_buffer_get_text(buffer,&a,&b,FALSE);gsize old_length=strlen(old),prefix=0,suffix=0;
 while(prefix<old_length && prefix<text->len && old[prefix]==text->str[prefix]) prefix++;
 while(prefix && prefix<text->len && ((guchar)text->str[prefix]&0xc0)==0x80) prefix--;
 while(suffix<old_length-prefix && suffix<text->len-prefix && old[old_length-1-suffix]==text->str[text->len-1-suffix]) suffix++;
 while(suffix && ((guchar)text->str[text->len-suffix]&0xc0)==0x80) suffix--;
 t->rendering=TRUE;
 if(prefix+suffix!=old_length || old_length!=text->len) {
  gtk_text_buffer_get_iter_at_offset(buffer,&a,g_utf8_strlen(old,prefix));gtk_text_buffer_get_iter_at_offset(buffer,&b,g_utf8_strlen(old,old_length-suffix));gtk_text_buffer_delete(buffer,&a,&b);gtk_text_buffer_insert(buffer,&a,text->str+prefix,text->len-prefix-suffix);
 }
 g_free(old);gtk_text_buffer_get_bounds(buffer,&a,&b);gtk_text_buffer_remove_all_tags(buffer,&a,&b);
 for(guint n=0;n<runs->len;n++) {
  PaintRun run=g_array_index(runs,PaintRun,n);gtk_text_buffer_get_iter_at_offset(buffer,&a,run.first);gtk_text_buffer_get_iter_at_offset(buffer,&b,run.last);
  GtkTextTag *tag;if(run.fg!=0xdddddd && (tag=color_tag(t,run.fg,FALSE))) gtk_text_buffer_apply_tag(buffer,tag,&a,&b);if(run.bg && (tag=color_tag(t,run.bg,TRUE))) gtk_text_buffer_apply_tag(buffer,tag,&a,&b);
  const gchar *names[]={"terminal-bold","terminal-italic","terminal-underline","terminal-strike"};for(guint k=0;k<4;k++) if(run.attrs&(1u<<k)) gtk_text_buffer_apply_tag_by_name(buffer,names[k],&a,&b);
 }
 if(selected && !t->trimmed) { gtk_text_buffer_get_iter_at_offset(buffer,&a,MIN(first,characters));gtk_text_buffer_get_iter_at_offset(buffer,&b,MIN(last,characters));gtk_text_buffer_select_range(buffer,&a,&b); }
 else { gtk_text_buffer_get_iter_at_offset(buffer,&a,MIN(caret,characters));gtk_text_buffer_place_cursor(buffer,&a); }
 gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(t->view),t->cursor_visible);t->trimmed=FALSE;t->rendering=FALSE;
 if(bottom && !selected) { gtk_text_buffer_get_iter_at_offset(buffer,&a,MIN(caret,characters));gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(t->view),&a,0,FALSE,0,0); }else gtk_adjustment_set_value(adjustment,old_scroll);
 g_string_free(text,TRUE);g_array_unref(runs);return G_SOURCE_REMOVE;
}
static void schedule(Terminal *t) { if(!t->closed && !t->render) t->render=g_timeout_add(25,render,t); }
void insight_terminal_feed(GtkWidget *view,const gchar *bytes,gsize length) { Terminal *t=state(view);if(!t || t->closed) return;vterm_input_write(t->vt,bytes,length);vterm_screen_flush_damage(t->screen);schedule(t); }
void insight_terminal_reset(GtkWidget *view) { Terminal *t=state(view);if(t) { vterm_screen_reset(t->screen,TRUE);schedule(t); } }
void insight_terminal_send(GtkWidget *view,const gchar *text) { Terminal *t=state(view);if(!t || t->closed || !text || strlen(text)>1024*1024-32 || !g_utf8_validate(text,-1,NULL)) return;
 t->batch=g_byte_array_new();vterm_keyboard_start_paste(t->vt);output(text,strlen(text),t);vterm_keyboard_end_paste(t->vt);GByteArray *batch=t->batch;t->batch=NULL;if(t->write && batch->len) t->write((const gchar *)batch->data,batch->len,t->data);g_byte_array_unref(batch); }
static void insert_text(GtkTextView *view,const gchar *text,gpointer data) { (void)data;insight_terminal_send(GTK_WIDGET(view),text);g_signal_stop_emission_by_name(view,"insert-at-cursor"); }
static gchar *selection_text(GtkWidget *view) { GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));GtkTextIter a,z;return gtk_text_buffer_get_selection_bounds(b,&a,&z) ? gtk_text_buffer_get_text(b,&a,&z,FALSE):NULL; }
static void pasted(GtkClipboard *clipboard,const gchar *text,gpointer data) { (void)clipboard;GtkWidget *view=data;insight_terminal_send(view,text);g_object_unref(view); }
static void primary_pasted(GtkClipboard *clipboard,const gchar *text,gpointer data) { (void)clipboard;if(text && *text) pasted(NULL,text,data);else gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),pasted,data); }
static void paste(GtkWidget *view,gboolean primary) { gchar *selected=primary ? selection_text(view):NULL;if(selected && *selected) insight_terminal_send(view,selected);else gtk_clipboard_request_text(gtk_clipboard_get(primary ? GDK_SELECTION_PRIMARY:GDK_SELECTION_CLIPBOARD),primary ? primary_pasted:pasted,g_object_ref(view));g_free(selected); }
static void popup_action(GtkMenuItem *item,gpointer data) { GtkWidget *view=data;const gchar *action=g_object_get_data(G_OBJECT(item),"terminal-action");if(!strcmp(action,"copy")) gtk_text_buffer_copy_clipboard(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));else paste(view,FALSE); }
static void popup(GtkTextView *view,GtkWidget *menu,gpointer data) { (void)data;GList *children=gtk_container_get_children(GTK_CONTAINER(menu));for(GList *p=children;p;p=p->next) gtk_widget_destroy(p->data);g_list_free(children);const gchar *labels[]={"Copy","Paste"},*actions[]={"copy","paste"};for(guint n=0;n<2;n++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[n]);g_object_set_data(G_OBJECT(item),"terminal-action",(gpointer)actions[n]);g_signal_connect(item,"activate",G_CALLBACK(popup_action),view);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item); } }
static gboolean mouse(GtkWidget *view,GdkEventButton *event,gpointer data) { (void)data;if(event->button!=2) return FALSE;gtk_widget_grab_focus(view);paste(view,TRUE);return TRUE; }
static void commit(GtkIMContext *context,const gchar *text,gpointer data) { (void)context;Terminal *t=data;for(const gchar *p=text;*p;p=g_utf8_next_char(p)) vterm_keyboard_unichar(t->vt,g_utf8_get_char(p),VTERM_MOD_NONE); }
gboolean insight_terminal_key(GtkWidget *focus,GdkEventKey *event) {
 Terminal *t=state(focus);if(!t || t->closed) return FALSE;
 guint mask=event->state&gtk_accelerator_get_default_mod_mask(),key=event->keyval;
 if(mask==(GDK_CONTROL_MASK|GDK_SHIFT_MASK) && (key==GDK_KEY_C || key==GDK_KEY_c)) { gtk_text_buffer_copy_clipboard(gtk_text_view_get_buffer(GTK_TEXT_VIEW(focus)),gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));return TRUE; }
 if((mask==(GDK_CONTROL_MASK|GDK_SHIFT_MASK) && (key==GDK_KEY_V || key==GDK_KEY_v)) || (key==GDK_KEY_Insert && mask==GDK_SHIFT_MASK)) { paste(focus,FALSE);return TRUE; }
 VTermModifier mod=(mask&GDK_SHIFT_MASK ? VTERM_MOD_SHIFT:0)|(mask&GDK_MOD1_MASK ? VTERM_MOD_ALT:0)|(mask&GDK_CONTROL_MASK ? VTERM_MOD_CTRL:0);VTermKey code=VTERM_KEY_NONE;
 switch(key) { case GDK_KEY_Return:case GDK_KEY_KP_Enter:code=VTERM_KEY_ENTER;break;case GDK_KEY_Tab:case GDK_KEY_ISO_Left_Tab:code=VTERM_KEY_TAB;break;case GDK_KEY_BackSpace:code=VTERM_KEY_BACKSPACE;break;case GDK_KEY_Escape:code=VTERM_KEY_ESCAPE;break;case GDK_KEY_Up:code=VTERM_KEY_UP;break;case GDK_KEY_Down:code=VTERM_KEY_DOWN;break;case GDK_KEY_Left:code=VTERM_KEY_LEFT;break;case GDK_KEY_Right:code=VTERM_KEY_RIGHT;break;case GDK_KEY_Home:code=VTERM_KEY_HOME;break;case GDK_KEY_End:code=VTERM_KEY_END;break;case GDK_KEY_Insert:code=VTERM_KEY_INS;break;case GDK_KEY_Delete:code=VTERM_KEY_DEL;break;case GDK_KEY_Page_Up:code=VTERM_KEY_PAGEUP;break;case GDK_KEY_Page_Down:code=VTERM_KEY_PAGEDOWN;break;default:if(key>=GDK_KEY_F1 && key<=GDK_KEY_F35) code=VTERM_KEY_FUNCTION(key-GDK_KEY_F1+1); }
 if(code) vterm_keyboard_key(t->vt,code,mod);
 else if(!(mask&(GDK_CONTROL_MASK|GDK_MOD1_MASK)) && gtk_im_context_filter_keypress(t->im,event)) return TRUE;
 else { gunichar character=gdk_keyval_to_unicode(key);if(character) vterm_keyboard_unichar(t->vt,character,mod); }
 return TRUE;
}
static gboolean key_event(GtkWidget *view,GdkEventKey *event,gpointer data) { (void)data;return insight_terminal_key(view,event); }
static gboolean focused(GtkWidget *view,GdkEventFocus *event,gpointer data) { Terminal *t=data;if(event->in) { gtk_im_context_set_client_window(t->im,gtk_widget_get_window(view));gtk_im_context_focus_in(t->im); }else gtk_im_context_focus_out(t->im);return FALSE; }
static void destroyed(GtkWidget *view,gpointer data) { (void)view;Terminal *t=data;t->closed=TRUE;t->write=NULL;if(t->render) { g_source_remove(t->render);t->render=0; }gtk_im_context_set_client_window(t->im,NULL); }
static void freed(gpointer data) { Terminal *t=data;clear_history(t);g_object_unref(t->im);vterm_free(t->vt);g_hash_table_unref(t->tags);g_free(t); }
GtkWidget *insight_terminal_new(InsightTerminalWrite write,gpointer data) {
 Terminal *t=g_new0(Terminal,1);t->rows=24;t->cols=80;t->cursor_visible=TRUE;t->write=write;t->data=data;t->tags=g_hash_table_new(g_direct_hash,g_direct_equal);t->vt=vterm_new(t->rows,t->cols);vterm_set_utf8(t->vt,TRUE);t->screen=vterm_obtain_screen(t->vt);
 static const VTermScreenCallbacks callbacks={.damage=damage,.movecursor=cursor_moved,.settermprop=property,.sb_pushline=push_line,.sb_popline=pop_line,.sb_clear=clear_history};vterm_screen_set_callbacks(t->screen,&callbacks,t);vterm_screen_enable_altscreen(t->screen,TRUE);vterm_output_set_callback(t->vt,output,t);
 VTermColor fg,bg;vterm_color_rgb(&fg,221,221,221);vterm_color_rgb(&bg,0,0,0);vterm_screen_set_default_colors(t->screen,&fg,&bg);vterm_screen_reset(t->screen,TRUE);
 t->view=gtk_text_view_new();gtk_text_view_set_editable(GTK_TEXT_VIEW(t->view),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(t->view),TRUE);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(t->view),GTK_WRAP_NONE);gtk_text_view_set_left_margin(GTK_TEXT_VIEW(t->view),4);gtk_widget_set_hexpand(t->view,TRUE);gtk_widget_set_vexpand(t->view,TRUE);
 GtkCssProvider *css=gtk_css_provider_new();gtk_css_provider_load_from_data(css,"textview.insight-terminal, textview.insight-terminal text { background-color: #000; color: #ddd; font-family: monospace; font-size: 10pt; } textview.insight-terminal text selection { background-color: #385f91; color: #fff; }",-1,NULL);gtk_style_context_add_class(gtk_widget_get_style_context(t->view),"insight-terminal");gtk_style_context_add_provider(gtk_widget_get_style_context(t->view),GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION+100);g_object_unref(css);
 PangoFontDescription *font=pango_font_description_from_string("Monospace 10");PangoFontMetrics *metrics=pango_context_get_metrics(gtk_widget_get_pango_context(t->view),font,NULL);t->cw=MAX(1,PANGO_PIXELS_CEIL(pango_font_metrics_get_approximate_digit_width(metrics)));t->ch=MAX(1,PANGO_PIXELS_CEIL(pango_font_metrics_get_ascent(metrics)+pango_font_metrics_get_descent(metrics)));pango_font_metrics_unref(metrics);pango_font_description_free(font);
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(t->view));gtk_text_buffer_create_tag(buffer,"terminal-bold","weight",PANGO_WEIGHT_BOLD,NULL);gtk_text_buffer_create_tag(buffer,"terminal-italic","style",PANGO_STYLE_ITALIC,NULL);gtk_text_buffer_create_tag(buffer,"terminal-underline","underline",PANGO_UNDERLINE_SINGLE,NULL);gtk_text_buffer_create_tag(buffer,"terminal-strike","strikethrough",TRUE,NULL);
 t->im=gtk_im_multicontext_new();g_signal_connect(t->im,"commit",G_CALLBACK(commit),t);g_object_set_data_full(G_OBJECT(t->view),STATE,t,freed);g_signal_connect(t->view,"destroy",G_CALLBACK(destroyed),t);g_signal_connect(t->view,"key-press-event",G_CALLBACK(key_event),NULL);g_signal_connect(t->view,"focus-in-event",G_CALLBACK(focused),t);g_signal_connect(t->view,"focus-out-event",G_CALLBACK(focused),t);g_signal_connect(t->view,"button-press-event",G_CALLBACK(mouse),NULL);g_signal_connect(t->view,"populate-popup",G_CALLBACK(popup),NULL);g_signal_connect(t->view,"insert-at-cursor",G_CALLBACK(insert_text),NULL);schedule(t);return t->view;
}
/* GTK deployments need not ship an SVG pixbuf loader. Keep the same vector
 * geometry available through GTK's mandatory Cairo backend as a fallback. */
static GdkPixbuf *tab_fallback(guint kind,guint number) {
 cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,30,30);cairo_t *cr=cairo_create(surface);cairo_scale(cr,30./36,30./36);cairo_set_source_rgb(cr,96./255,115./255,139./255);cairo_set_line_width(cr,2.4);cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
 if(kind==0) {
  cairo_move_to(cr,13,4);cairo_line_to(cr,13,12);cairo_move_to(cr,23,4);cairo_line_to(cr,23,12);cairo_move_to(cr,10,12);cairo_line_to(cr,26,12);cairo_line_to(cr,26,17);cairo_curve_to(cr,26,22,23,25,18,25);cairo_curve_to(cr,13,25,10,22,10,17);cairo_close_path(cr);cairo_move_to(cr,18,25);cairo_line_to(cr,18,29);cairo_curve_to(cr,18,32,22,33,26,33);cairo_stroke(cr);
 } else if(kind==1) {
  cairo_move_to(cr,6,33);cairo_line_to(cr,6,3);cairo_line_to(cr,21,3);cairo_line_to(cr,28,10);cairo_line_to(cr,28,16);cairo_move_to(cr,21,3);cairo_line_to(cr,21,11);cairo_line_to(cr,28,11);cairo_move_to(cr,6,33);cairo_line_to(cr,23,33);cairo_move_to(cr,19,19);cairo_line_to(cr,34,19);cairo_move_to(cr,29,14);cairo_line_to(cr,34,19);cairo_line_to(cr,29,24);cairo_move_to(cr,34,29);cairo_line_to(cr,19,29);cairo_move_to(cr,24,24);cairo_line_to(cr,19,29);cairo_line_to(cr,24,34);cairo_stroke(cr);
 } else {
  cairo_rectangle(cr,3,5,29,26);cairo_set_source_rgb(cr,36./255,52./255,74./255);cairo_fill_preserve(cr);cairo_set_source_rgb(cr,96./255,115./255,139./255);cairo_stroke(cr);cairo_move_to(cr,7,11);cairo_line_to(cr,11,15);cairo_line_to(cr,7,19);cairo_stroke(cr);cairo_select_font_face(cr,"sans-serif",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL);cairo_set_font_size(cr,16);gchar *label=g_strdup_printf("%u",number);cairo_text_extents_t extents;cairo_text_extents(cr,label,&extents);cairo_set_source_rgb(cr,1,1,1);cairo_move_to(cr,22-(extents.width/2+extents.x_bearing),25);cairo_show_text(cr,label);g_free(label);
 }
 cairo_destroy(cr);GdkPixbuf *pixbuf=gdk_pixbuf_get_from_surface(surface,0,0,30,30);cairo_surface_destroy(surface);return pixbuf;
}
GtkWidget *insight_terminal_tab(guint kind,guint number,const gchar *tooltip) {
 const gchar *plug="<path d='M13 4v8m10-8v8M10 12h16v5a8 8 0 0 1-8 8v4c0 3 4 4 8 4'/>";
 const gchar *transfer="<path d='M6 3h15l7 7v10M6 3v30h17M21 3v8h7M19 19h15m-5-5 5 5-5 5M34 29H19m5-5-5 5 5 5'/>";
 gchar *terminal=g_strdup_printf("<rect x='3' y='5' width='29' height='26' rx='3' fill='#24344a'/><path d='m7 11 4 4-4 4'/><text x='21' y='25' fill='white' stroke='none' text-anchor='middle' font-family='sans-serif' font-size='16'>%u</text>",number);
 gchar *svg=g_strdup_printf("<svg xmlns='http://www.w3.org/2000/svg' width='30' height='30' viewBox='0 0 36 36'><g fill='none' stroke='#60738b' stroke-width='2.4' stroke-linecap='round' stroke-linejoin='round'>%s</g></svg>",kind==0 ? plug:kind==1 ? transfer:terminal);g_free(terminal);
 GdkPixbufLoader *loader=gdk_pixbuf_loader_new();gdk_pixbuf_loader_write(loader,(const guchar *)svg,strlen(svg),NULL);gdk_pixbuf_loader_close(loader,NULL);GdkPixbuf *pixbuf=gdk_pixbuf_loader_get_pixbuf(loader);gboolean fallback=pixbuf==NULL;if(fallback) pixbuf=tab_fallback(kind,number);GtkWidget *image=gtk_image_new_from_pixbuf(pixbuf);if(fallback && pixbuf) g_object_unref(pixbuf);gtk_widget_set_tooltip_text(image,tooltip);g_object_unref(loader);g_free(svg);return image;
}
