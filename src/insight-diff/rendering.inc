/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Line gutters, whitespace, overview maps and aligned scrolling.
 * RU: Номера строк, пробелы, карты обзора и выровненная прокрутка. */

static void gutter_size(GtkTextBuffer *buffer,gpointer data) {
 Merge *m=data;gint lines=gtk_text_buffer_get_line_count(buffer),digits=1;
 while(lines>=10) { digits++;lines/=10; }
 for(int i=0;i<3;i++) if(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[i]))==buffer) {
  PangoLayout *layout=gtk_widget_create_pango_layout(m->views[i],"0");gint width;
  pango_layout_get_pixel_size(layout,&width,NULL);g_object_unref(layout);
  gtk_text_view_set_border_window_size(GTK_TEXT_VIEW(m->views[i]),GTK_TEXT_WINDOW_LEFT,
   gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->numbers)) ? digits*width+14 : 0);
  gtk_widget_queue_draw(m->views[i]);
 }
}
static void number_toggle(GtkToggleButton *button,gpointer data) {
 (void)button;Merge *m=data;for(int i=0;i<3;i++) gutter_size(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[i])),m);
}
static gboolean draw_numbers(GtkWidget *widget,cairo_t *cr,gpointer data) {
 Merge *m=data;if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->numbers))) return FALSE;
 GtkTextView *view=GTK_TEXT_VIEW(widget);GdkWindow *window=gtk_text_view_get_window(view,GTK_TEXT_WINDOW_LEFT);
 if(!window || !gtk_cairo_should_draw_window(cr,window)) return FALSE;
 cairo_save(cr);gtk_cairo_transform_to_window(cr,widget,window);
 GtkStyleContext *style=gtk_widget_get_style_context(widget);GdkRGBA color;gtk_style_context_get_color(style,gtk_style_context_get_state(style),&color);
 gint width=gdk_window_get_width(window);gtk_render_background(style,cr,0,0,width,gdk_window_get_height(window));gdk_cairo_set_source_rgba(cr,&color);
 GdkRectangle rect;GtkTextIter it;gtk_text_view_get_visible_rect(view,&rect);gtk_text_view_get_line_at_y(view,&it,rect.y,NULL);
 PangoLayout *layout=gtk_widget_create_pango_layout(widget,NULL);
 do {
  gint y,x,wy;GdkRectangle glyph;gtk_text_view_get_iter_location(view,&it,&glyph);y=glyph.y;if(y>rect.y+rect.height) break;
  gchar label[32];g_snprintf(label,sizeof label,"%d",gtk_text_iter_get_line(&it)+1);pango_layout_set_text(layout,label,-1);
  gint text_width;pango_layout_get_pixel_size(layout,&text_width,NULL);
  gtk_text_view_buffer_to_window_coords(view,GTK_TEXT_WINDOW_LEFT,0,y,&x,&wy);
  cairo_move_to(cr,width-text_width-7,wy);pango_cairo_show_layout(cr,layout);
  gint current=gtk_text_iter_get_line(&it);gtk_text_iter_forward_line(&it);
  if(gtk_text_iter_get_line(&it)==current) break;
 } while(TRUE);
 g_object_unref(layout);cairo_restore(cr);return FALSE;
}
static const gchar *whitespace_symbol(GtkTextIter at,gboolean spaces,gboolean endings) {
 gunichar c=gtk_text_iter_get_char(&at);
 if(spaces && c==' ') return "·";
 if(spaces && c=='\t') return "→";
 if(endings && c=='\r') return "¶";
 if(endings && c=='\n') { GtkTextIter before=at;if(gtk_text_iter_backward_char(&before) && gtk_text_iter_get_char(&before)=='\r') return NULL;return "¶"; }
 return NULL;
}
static gboolean draw_whitespace(GtkWidget *widget,cairo_t *cr,gpointer data) {
 Merge *m=data;gboolean spaces=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->visible_space)),endings=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->visible_eol));if(!spaces && !endings) return FALSE;
 GtkTextView *view=GTK_TEXT_VIEW(widget);GdkWindow *window=gtk_text_view_get_window(view,GTK_TEXT_WINDOW_TEXT);if(!window || !gtk_cairo_should_draw_window(cr,window)) return FALSE;
 cairo_save(cr);gtk_cairo_transform_to_window(cr,widget,window);cairo_rectangle(cr,0,0,gdk_window_get_width(window),gdk_window_get_height(window));cairo_clip(cr);
 GdkRGBA color;GtkStyleContext *style=gtk_widget_get_style_context(widget);gtk_style_context_get_color(style,gtk_style_context_get_state(style),&color);color.alpha*=0.55;gdk_cairo_set_source_rgba(cr,&color);
 GdkRectangle visible;gtk_text_view_get_visible_rect(view,&visible);GtkTextIter at;gtk_text_view_get_iter_at_location(view,&at,visible.x,visible.y);
 PangoLayout *layout=gtk_widget_create_pango_layout(widget,NULL);
 /* Bound work even for pathological unwrapped lines. Glyphs never enter buffers. */
 for(guint scanned=0;scanned<20000 && !gtk_text_iter_is_end(&at);scanned++) {
  GdkRectangle location;gtk_text_view_get_iter_location(view,&at,&location);if(location.y>visible.y+visible.height) break;
  const gchar *symbol=whitespace_symbol(at,spaces,endings);
  if(symbol && location.x+MAX(location.width,8)>=visible.x && location.x<visible.x+visible.width && location.y+location.height>=visible.y) {
   gint x,y,width,height;gtk_text_view_buffer_to_window_coords(view,GTK_TEXT_WINDOW_TEXT,location.x,location.y,&x,&y);pango_layout_set_text(layout,symbol,-1);pango_layout_get_pixel_size(layout,&width,&height);
   cairo_move_to(cr,x+MAX(0,(location.width-width)/2),y+MAX(0,(location.height-height)/2));pango_cairo_show_layout(cr,layout);
  }
  if(!gtk_text_iter_forward_char(&at)) break;
 }
 g_object_unref(layout);cairo_restore(cr);return FALSE;
}
static gint selected(Merge *m);
static void map_redraw(Merge *m) { for(gint i=0;i<2;i++) if(m->maps[i]) gtk_widget_queue_draw(m->maps[i]); }
static guint map_line(const gchar *text,gsize *offset,guint *line,gsize target) {
 while(*offset<target) { gchar c=text[(*offset)++];if(c=='\r') { if(*offset<target && text[*offset]=='\n') (*offset)++;(*line)++; }else if(c=='\n') (*line)++; }
 return *line;
}
static void map_build(Merge *m) {
 for(gint side=0;side<2;side++) {
  g_clear_pointer(&m->map_blocks[side],g_array_unref);m->map_blocks[side]=g_array_new(FALSE,FALSE,sizeof(MapBlock));gsize offset=0;guint line=0;
  for(gsize i=0;i<m->diff.count;i++) { InsightDiffHunk *h=&m->diff.hunks[i];MapBlock block;block.begin=map_line(m->sources[side],&offset,&line,side ? h->b_begin : h->a_begin);block.end=map_line(m->sources[side],&offset,&line,side ? h->b_end : h->a_end);gsize begin=side ? h->b_begin : h->a_begin,end=side ? h->b_end : h->a_end;if(end>begin && m->sources[side][end-1]!='\r' && m->sources[side][end-1]!='\n') block.end++;g_array_append_val(m->map_blocks[side],block); }
  m->map_lines[side]=map_line(m->sources[side],&offset,&line,strlen(m->sources[side]))+1;
 }
 map_redraw(m);
}
static void map_extent(MapBlock block,guint lines,gdouble height,gdouble *top,gdouble *span) {
 gdouble total=MAX(lines,1);height=MAX(height,1.);
 *top=CLAMP(block.begin/total*height,0.,MAX(0.,height-3.));*span=MIN(height-*top,MAX(3.,(block.end-block.begin)/total*height));
}
static gboolean map_draw(GtkWidget *widget,cairo_t *cr,gpointer data) {
 Merge *m=data;gint side=widget==m->maps[1],width=gtk_widget_get_allocated_width(widget),height=gtk_widget_get_allocated_height(widget);
 cairo_set_source_rgb(cr,.94,.95,.97);cairo_paint(cr);
 GArray *blocks=m->map_blocks[side];gint current=selected(m);
 for(guint pass=0;pass<2;pass++) for(guint i=0;blocks && i<blocks->len;i++) { if(((gint)i==current)!=(pass==1)) continue;MapBlock b=g_array_index(blocks,MapBlock,i);gdouble y,h;map_extent(b,m->map_lines[side],height,&y,&h);
  if((gint)i==current) cairo_set_source_rgb(cr,.2,.38,.75);else if(side) cairo_set_source_rgb(cr,.3,.65,.4);else cairo_set_source_rgb(cr,.85,.5,.25);
  cairo_rectangle(cr,3,y,MAX(1,width-6),h);cairo_fill(cr);
 }
 GtkAdjustment *a=m->adjustments[side];gdouble extent=gtk_adjustment_get_upper(a)-gtk_adjustment_get_lower(a);
 if(extent>0) { gdouble y=(gtk_adjustment_get_value(a)-gtk_adjustment_get_lower(a))/extent*height,h=MAX(4.,gtk_adjustment_get_page_size(a)/extent*height);cairo_set_source_rgba(cr,.12,.2,.35,.7);cairo_set_line_width(cr,1.);cairo_rectangle(cr,.5,y+.5,MAX(1,width-1),MAX(1,MIN(h,height-y)-1));cairo_stroke(cr); }
 return FALSE;
}
static gboolean map_click(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 Merge *m=data;if(event->button!=1 || m->busy || !m->sources[0]) return FALSE;gint side=widget==m->maps[1];gdouble height=MAX(gtk_widget_get_allocated_height(widget),1),total=MAX(m->map_lines[side],1),y=CLAMP(event->y,0.,height);
 GArray *blocks=m->map_blocks[side];gint best=-1;gdouble nearest=G_MAXDOUBLE;
 for(guint i=0;blocks && i<blocks->len;i++) { MapBlock b=g_array_index(blocks,MapBlock,i);gdouble top,span;map_extent(b,m->map_lines[side],height,&top,&span);gdouble end=top+span,distance=ABS(y-(top+end)/2);
  if(y>=top-2 && y<=end+2 && distance<nearest) { best=(gint)i;nearest=distance; }
 }
 if(best>=0) { GtkTreePath *path=gtk_tree_path_new_from_indices(best,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(m->hunk_view),path,NULL,FALSE);gtk_tree_path_free(path);map_redraw(m);return TRUE; }
 GtkTextIter iter;GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));gtk_text_buffer_get_iter_at_line(buffer,&iter,CLAMP((gint)(y/height*total),0,gtk_text_buffer_get_line_count(buffer)-1));gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(m->views[side]),&iter,0.,TRUE,0.,.5);return TRUE;
}
static void map_adjustment(GtkAdjustment *adjustment,gpointer data) { (void)adjustment;map_redraw(data); }
/* Visual padding belongs to paragraph tags, never to source text. It aligns
 * unchanged anchors after insertions/deletions without changing copy/search. */
static void alignment_clear(Merge *m) {
 for(guint side=0;side<2;side++) if(m->alignment[side]) {
  GtkTextTagTable *table=gtk_text_buffer_get_tag_table(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side])));
  for(guint n=0;n<m->alignment[side]->len;n++) gtk_text_tag_table_remove(table,g_ptr_array_index(m->alignment[side],n));g_ptr_array_set_size(m->alignment[side],0);
 }m->aligned=FALSE;
}
static gint anchor_y(Merge *m,guint side,gsize offset,GtkTextIter *it) {
 GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));gint pos=g_utf8_strlen(m->sources[side],offset);gtk_text_buffer_get_iter_at_offset(b,it,pos);GdkRectangle rect;gtk_text_view_get_iter_location(GTK_TEXT_VIEW(m->views[side]),it,&rect);return rect.y;
}
static void pad_anchor(Merge *m,guint side,GtkTextIter *it,gint pixels) {
 if(pixels<=0) return;GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter a=*it,z=*it;const gchar *property="pixels-above-lines";
 if(gtk_text_iter_is_end(&z)&&gtk_text_iter_get_line_offset(&z)==0&&gtk_text_iter_get_line(&z)>0) { gtk_text_iter_backward_char(&a);gtk_text_iter_set_line_offset(&a,0);property="pixels-below-lines"; }
 else if(!gtk_text_iter_forward_char(&z)) { if(gtk_text_iter_equal(&a,&z)&&gtk_text_iter_get_offset(&a)>0) gtk_text_iter_backward_char(&a); }
 GtkTextTag *tag=gtk_text_buffer_create_tag(b,NULL,property,pixels,NULL);g_ptr_array_add(m->alignment[side],g_object_ref(tag));gtk_text_buffer_apply_tag(b,tag,&a,&z);
}
static gboolean align_sources(gpointer data) {
 Merge *m=data;m->alignment_idle=0;if(!m->sources[0]||m->busy) return G_SOURCE_REMOVE;m->scrolling=TRUE;alignment_clear(m);
 /* Unchanged anchors after each hunk define the same visual row. Equal line
  * metrics and no source wrapping preserve all intermediate equal rows. */
 for(gsize n=0;n<m->diff.count;n++) {
  InsightDiffHunk *h=&m->diff.hunks[n];GtkTextIter it[2];gint y[2]={anchor_y(m,0,h->a_end,&it[0]),anchor_y(m,1,h->b_end,&it[1])};if(y[0]<y[1]) pad_anchor(m,0,&it[0],y[1]-y[0]);else if(y[1]<y[0]) pad_anchor(m,1,&it[1],y[0]-y[1]);
 }
 m->aligned=TRUE;m->scrolling=FALSE;return G_SOURCE_REMOVE;
}
static void alignment_queue(Merge *m) { if(!m->alignment_idle) m->alignment_idle=g_idle_add_full(G_PRIORITY_LOW,align_sources,m,NULL); }
static void source_allocated(GtkWidget *widget,GtkAllocation *allocation,gpointer data) { Merge *m=data;guint side=widget==m->views[1];if(m->source_width[side]!=allocation->width) { m->source_width[side]=allocation->width;alignment_queue(m); } }
static void horizontal_scroll(GtkAdjustment *adjustment,gpointer data) { Merge *m=data;if(m->scrolling||!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->sync))) return;m->scrolling=TRUE;guint side=adjustment==m->horizontal[1];gtk_adjustment_set_value(m->horizontal[1-side],gtk_adjustment_get_value(adjustment));m->scrolling=FALSE; }
static void display_changed(GtkWidget *widget,gpointer data) {
 (void)widget;Merge *m=data;alignment_queue(m);gint zoom=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->zoom));
 gchar size[G_ASCII_DTOSTR_BUF_SIZE];g_ascii_dtostr(size,sizeof size,(gdouble)INSIGHT_COMPARE_CODE_POINTS*zoom/100.0);gchar *css=g_strdup_printf("textview { font-size: %spt; }",size);gtk_css_provider_load_from_data(m->font_css,css,-1,NULL);g_free(css);
 for(gint i=0;i<3;i++) {
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(m->views[i]),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->mono)));
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m->scrollers[i]),GTK_POLICY_AUTOMATIC,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->vertical)) ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER);
 }
 number_toggle(NULL,m);
 for(gint i=0;i<2;i++) if(m->maps[i]) gtk_widget_set_visible(m->maps[i],gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->overview)));
}
static gboolean zoom_scroll(GtkWidget *widget,GdkEventScroll *event,gpointer data) {
 (void)widget;if(!(event->state&GDK_CONTROL_MASK)) return FALSE;Merge *m=data;gdouble delta=0;
 if(event->direction==GDK_SCROLL_UP) delta=10;else if(event->direction==GDK_SCROLL_DOWN) delta=-10;else if(event->direction==GDK_SCROLL_SMOOTH) delta=event->delta_y<0 ? 10 : event->delta_y>0 ? -10 : 0;
 if(delta) gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),gtk_spin_button_get_value(GTK_SPIN_BUTTON(m->zoom))+delta);
 return TRUE;
}
static void view_options(GtkWidget *widget,gpointer data) {
 (void)widget;Merge *m=data;GtkWidget *window=m->window;g_object_ref(window);
 GtkWidget *dialog=gtk_dialog_new_with_buttons("File Compare Options",GTK_WINDOW(window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Apply",GTK_RESPONSE_APPLY,NULL);gtk_widget_set_name(dialog,"insight-diff-options");
 g_object_ref(dialog);GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));gtk_container_set_border_width(GTK_CONTAINER(box),10);
 GtkWidget *sources[]={m->mono,m->numbers,m->list_toggle,m->wrap_toggle,m->sync,m->vertical,m->overview,m->visible_space,m->visible_eol};GtkWidget *checks[G_N_ELEMENTS(sources)];
 for(guint i=0;i<G_N_ELEMENTS(sources);i++) { checks[i]=gtk_check_button_new_with_label(gtk_button_get_label(GTK_BUTTON(sources[i])));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checks[i]),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sources[i])));gtk_box_pack_start(GTK_BOX(box),checks[i],FALSE,FALSE,2);gchar *name=g_strdup_printf("insight-diff-options-%u",i);gtk_widget_set_name(checks[i],name);g_free(name); }
 GtkWidget *scale=gtk_spin_button_new_with_range(50,250,10);gtk_spin_button_set_value(GTK_SPIN_BUTTON(scale),gtk_spin_button_get_value(GTK_SPIN_BUTTON(m->zoom)));gtk_widget_set_name(scale,"insight-diff-options-zoom");gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Scale (%)"),FALSE,FALSE,2);gtk_box_pack_start(GTK_BOX(box),scale,FALSE,FALSE,2);gtk_widget_show_all(dialog);
 gint response=gtk_dialog_run(GTK_DIALOG(dialog));m=g_object_get_data(G_OBJECT(window),"insight-diff-state");
 if(response==GTK_RESPONSE_APPLY && m) { for(guint i=0;i<G_N_ELEMENTS(sources);i++) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sources[i]),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checks[i])));gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),gtk_spin_button_get_value(GTK_SPIN_BUTTON(scale))); }
 gtk_widget_destroy(dialog);g_object_unref(dialog);g_object_unref(window);
}
