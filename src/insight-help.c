/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "app.h"
#include "document.h"
#include "editor.h"
#include "sciwrappers.h"
#include "utils.h"
#include "ui_utils.h"
#include "insight-help.h"
#include <glib/gstdio.h>
#include <string.h>

/* A portable GTK renderer. No web engine, network fetches or raw HTML scripts
 * are needed for documentation. The HTML media view delegates codecs to the
 * user's browser rather than assuming a video backend exists in GTK3. */
typedef struct {
 GtkWidget *window,*body,*status,*back,*forward,*scroll;GPtrArray *history;guint position;
 gchar *path,*html;GPtrArray *temporary;
} Help;
static Help *help;
static gboolean icon_draw(GtkWidget *widget,cairo_t *cr,gpointer data) {
 const gchar *id=data;GdkRGBA color;gtk_style_context_get_color(gtk_widget_get_style_context(widget),gtk_widget_get_state_flags(widget),&color);gdk_cairo_set_source_rgba(cr,&color);cairo_set_line_width(cr,1.2);
 if(g_str_has_prefix(id,"layout-")) { cairo_rectangle(cr,1,2,14,12);cairo_stroke(cr);cairo_set_source_rgb(cr,.2,.55,.85);if(id[7]=='a' || id[7]=='d') cairo_rectangle(cr,2,3,4,10);if(id[7]=='b') cairo_rectangle(cr,2,10,12,3);if(id[7]=='c' || id[7]=='d') cairo_rectangle(cr,10,3,4,10);cairo_fill(cr); }
 else if(!strcmp(id,"media")) { cairo_arc(cr,8,8,6,0,2*G_PI);cairo_move_to(cr,2,8);cairo_line_to(cr,14,8);cairo_move_to(cr,8,2);cairo_curve_to(cr,3,6,3,10,8,14);cairo_curve_to(cr,13,10,13,6,8,2);cairo_stroke(cr); }
 else if(!strcmp(id,"diff")) { cairo_rectangle(cr,1,2,5,12);cairo_rectangle(cr,10,2,5,12);cairo_stroke(cr);cairo_set_source_rgb(cr,.2,.55,.85);cairo_move_to(cr,4,5);cairo_line_to(cr,12,5);cairo_move_to(cr,9,3);cairo_line_to(cr,12,5);cairo_line_to(cr,9,7);cairo_move_to(cr,12,11);cairo_line_to(cr,4,11);cairo_stroke(cr); }
 else if(!strcmp(id,"relations")) { cairo_move_to(cr,4,8);cairo_line_to(cr,9,8);cairo_move_to(cr,9,3);cairo_line_to(cr,9,13);cairo_move_to(cr,9,3);cairo_line_to(cr,13,3);cairo_move_to(cr,9,13);cairo_line_to(cr,13,13);cairo_stroke(cr);cairo_set_source_rgb(cr,.2,.55,.85);cairo_rectangle(cr,1,6,4,4);cairo_rectangle(cr,12,1,3,4);cairo_rectangle(cr,12,11,3,4);cairo_fill(cr); }
 else { cairo_move_to(cr,8,3);cairo_curve_to(cr,4,0,1,2,1,3);cairo_line_to(cr,1,13);cairo_curve_to(cr,4,11,6,12,8,14);cairo_curve_to(cr,10,12,12,11,15,13);cairo_line_to(cr,15,3);cairo_curve_to(cr,12,1,10,1,8,3);cairo_line_to(cr,8,14);cairo_stroke(cr);cairo_set_source_rgb(cr,.2,.55,.85);if(!strcmp(id,"tutorial")) { cairo_move_to(cr,10,5);cairo_line_to(cr,14,8);cairo_line_to(cr,10,11);cairo_close_path(cr); }else if(!strcmp(id,"bookmarks")) { cairo_move_to(cr,10,1);cairo_line_to(cr,13,1);cairo_line_to(cr,13,8);cairo_line_to(cr,11.5,6.5);cairo_line_to(cr,10,8);cairo_close_path(cr); }else { cairo_rectangle(cr,3,5,3,1);cairo_rectangle(cr,3,8,3,1);cairo_rectangle(cr,10,5,3,1);cairo_rectangle(cr,10,8,3,1); }cairo_fill(cr); }
 return FALSE;
}
GtkWidget *insight_command_icon(const gchar *command) {
 if(g_str_has_prefix(command,"layout-") || !strcmp(command,"media") || !strcmp(command,"manual") || !strcmp(command,"tutorial") || !strcmp(command,"bookmarks") || !strcmp(command,"diff") || !strcmp(command,"relations")) { GtkWidget *icon=gtk_drawing_area_new();gtk_widget_set_size_request(icon,16,16);g_object_set_data_full(G_OBJECT(icon),"insight-icon-name",g_strdup(command),g_free);g_signal_connect(icon,"draw",G_CALLBACK(icon_draw),g_object_get_data(G_OBJECT(icon),"insight-icon-name"));return icon; }
 return gtk_image_new_from_icon_name(command,GTK_ICON_SIZE_MENU);
}
static const gchar *command_icon(const gchar *label) {
 const gchar *layout=strstr(label,"layout ");if(layout) { switch(layout[7]) { case 'A':return "layout-a";case 'B':return "layout-b";case 'C':return "layout-c";case 'D':return "layout-d";default:break; } }
 if(strstr(label,"Markdown")) return "text-x-generic";
 if(strstr(label,"Diff") || strstr(label,"Merge")) return "diff";
 if(strstr(label,"Tutorial") || strstr(label,"тутори") || strstr(label,"урок")) return "tutorial";
 if(strstr(label,"Documentation") || strstr(label,"справ") || strstr(label,"Manual")) return "manual";
 if(strstr(label,"Bookmark") || strstr(label,"заклад")) return "bookmarks";
 if(strstr(label,"Relation") || strstr(label,"Call") || strstr(label,"Members") || strstr(label,"Inheritance")) return "relations";
 if(strstr(label,"Search project") || strstr(label,"Search folder") || strstr(label,"Search open documents") || strstr(label,"Search selected files")) return "system-search";
 if(strstr(label,"Replace") || strstr(label,"замен")) return "edit-find-replace";
 if(strstr(label,"Previous") || strstr(label,"back") || strstr(label,"Up")) return "go-previous";
 if(strstr(label,"Next") || strstr(label,"forward") || strstr(label,"Down")) return "go-next";
 if(strstr(label,"definition") || strstr(label,"Jump")) return "go-jump";
 if(strstr(label,"Symbol Info")) return "dialog-information";
 if(strstr(label,"Find file") || strstr(label,"Project files")) return "system-search";
 if(strstr(label,"Find") || strstr(label,"Search") || strstr(label,"поиск") || strstr(label,"symbols")) return "edit-find";
 if(strstr(label,"Reindex") || strstr(label,"Refresh") || strstr(label,"Reload")) return "view-refresh";
 if(strstr(label,"workspace") || strstr(label,"Open") || strstr(label,"Load")) return "folder-open";
 if(strstr(label,"Save")) return "document-save";
 if(strstr(label,"Close") || strstr(label,"Unload")) return "window-close";
 if(strstr(label,"Copy")) return "edit-copy";
 if(strstr(label,"Cut")) return "edit-cut";
 if(strstr(label,"Paste")) return "edit-paste";
 if(strstr(label,"Undo")) return "edit-undo";
 if(strstr(label,"Redo")) return "edit-redo";
 if(strstr(label,"Script") || strstr(label,"command") || strstr(label,"Run")) return "system-run";
 if(strstr(label,"Preferences") || strstr(label,"Options")) return "preferences-system";
 return "applications-development";
}
static void menu_icon_space(GtkMenuItem *item,gint *size,gpointer data) { (void)item;(void)data;*size=MAX(*size,40); }
static gboolean menu_icon_draw(GtkWidget *widget,cairo_t *cr,gpointer data) {
 const gchar *id=data;cairo_save(cr);cairo_translate(cr,23,(gtk_widget_get_allocated_height(widget)-16)/2);
 if(g_str_has_prefix(id,"layout-") || !strcmp(id,"media") || !strcmp(id,"manual") || !strcmp(id,"tutorial") || !strcmp(id,"bookmarks") || !strcmp(id,"diff") || !strcmp(id,"relations")) icon_draw(widget,cr,data);
 else { GdkPixbuf *pixbuf=gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),id,16,GTK_ICON_LOOKUP_USE_BUILTIN,NULL);if(pixbuf) { gdk_cairo_set_source_pixbuf(cr,pixbuf,0,0);cairo_paint(cr);g_object_unref(pixbuf); } }
 cairo_restore(cr);return FALSE;
}
static gboolean menus_shown(GSignalInvocationHint *hint,guint count,const GValue *values,gpointer data) {
 (void)hint;(void)data;if(count) { GtkWidget *widget=g_value_get_object(values);if(GTK_IS_MENU(widget)) insight_menu_icons(widget); }return TRUE;
}
static void icons_on_show(GtkWidget *menu,gpointer data) { (void)data;insight_menu_icons(menu); }
void insight_menu_icons(GtkWidget *menu) {
 if(!GTK_IS_CONTAINER(menu)) return;
 if(GTK_IS_MENU(menu) && GTK_IS_COMBO_BOX(gtk_menu_get_attach_widget(GTK_MENU(menu)))) return;
 /* Catch subsequently created command popups and plugin menus as well. */
 static gulong hook;if(!hook) hook=g_signal_add_emission_hook(g_signal_lookup("show",GTK_TYPE_WIDGET),0,menus_shown,NULL,NULL);
 if(GTK_IS_MENU(menu) && !g_object_get_data(G_OBJECT(menu),"insight-icons-watched")) { g_object_set_data(G_OBJECT(menu),"insight-icons-watched",GINT_TO_POINTER(1));g_signal_connect_after(menu,"show",G_CALLBACK(icons_on_show),NULL); }
 GList *children=gtk_container_get_children(GTK_CONTAINER(menu));
 for(GList *p=children;p;p=p->next) {
  GtkWidget *item=p->data;if(!GTK_IS_MENU_ITEM(item) || GTK_IS_SEPARATOR_MENU_ITEM(item)) continue;
  GtkWidget *submenu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));if(submenu) insight_menu_icons(submenu);
  if(GTK_IS_MENU_BAR(menu) || g_object_get_data(G_OBJECT(item),"insight-icon-added")) continue;
  const gchar *label=gtk_menu_item_get_label(GTK_MENU_ITEM(item));const gchar *id=command_icon(label ? label : "");
  /* Keep native GtkAccelLabel/accelerator association and the original action
   * widget intact. GtkImageMenuItem needs always-show-image on GTK themes. */
  if(GTK_IS_IMAGE_MENU_ITEM(item)) { if(!gtk_image_menu_item_get_image(GTK_IMAGE_MENU_ITEM(item))) gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),insight_command_icon(id));gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item),TRUE); }
  else { /* Preserve GtkAccelLabel: Geany also reads labels for MRU sorting. */
   g_object_set_data_full(G_OBJECT(item),"insight-menu-icon",g_strdup(id),g_free);g_signal_connect(item,"toggle-size-request",G_CALLBACK(menu_icon_space),NULL);g_signal_connect_after(item,"draw",G_CALLBACK(menu_icon_draw),g_object_get_data(G_OBJECT(item),"insight-menu-icon"));
  }
  g_object_set_data(G_OBJECT(item),"insight-icon-added",GINT_TO_POINTER(1));
 }
 g_list_free(children);
}
static gchar *resolve(const gchar *path,const gchar *base) {
 if(g_str_has_prefix(path,"file://")) return g_filename_from_uri(path,NULL,NULL);
 if(strstr(path,"://") || g_str_has_prefix(path,"data:") || g_str_has_prefix(path,"javascript:")) return NULL;
 return g_canonicalize_filename(path,base);
}
static gchar *uri_for(const gchar *path,const gchar *base) {
 if(g_str_has_prefix(path,"https://") || g_str_has_prefix(path,"http://") || g_str_has_prefix(path,"mailto:")) return g_strdup(path);
 if(path[0]=='#') return g_strdup(path);
 gchar *native=resolve(path,base),*uri=native ? g_filename_to_uri(native,NULL,NULL) : NULL;g_free(native);return uri ? uri : g_strdup("");
}
/* Small deliberately bounded subset shared by the GTK and HTML renderers.
 * Everything else remains readable literal text; raw HTML is never executed. */
static gchar *inline_text(const gchar *text,const gchar *base,gboolean html) {
 GString *out=g_string_new(NULL);const gchar *at=text;
 while(*at) {
  const gchar *end=NULL;guint n=1;const gchar *open=NULL,*close=NULL;
  if(!strncmp(at,"**",2)) { end=strstr(at+2,"**");n=2;open="<b>";close="</b>"; }
  else if(!strncmp(at,"``",2)) { end=strstr(at+2,"``");n=2;open=html ? "<code>" : "<tt>";close=html ? "</code>" : "</tt>"; }
  else if(*at=='`') { end=strchr(at+1,'`');open=html ? "<code>" : "<tt>";close=html ? "</code>" : "</tt>"; }
  else if(*at=='*') { end=strchr(at+1,'*');open="<i>";close="</i>"; }
  if(*at=='`' && end && end[1]=='_' && at[1]!='`') {
   const gchar *angle=memchr(at+1,'<',end-at-1);if(angle && end[-1]=='>') { gchar *caption=g_strndup(at+1,angle-at-1);g_strchomp(caption);gchar *escaped_caption=g_markup_escape_text(caption,-1),*target=g_strndup(angle+1,end-angle-2),*uri=uri_for(target,base),*escaped=g_markup_escape_text(uri,-1);g_string_append_printf(out,"<a href=\"%s\">%s</a>",escaped,escaped_caption);g_free(caption);g_free(escaped_caption);g_free(target);g_free(uri);g_free(escaped);at=end+2;continue; }
  }
  if(open && end) { gchar *piece=g_markup_escape_text(at+n,end-at-n);g_string_append(out,open);g_string_append(out,piece);g_string_append(out,close);g_free(piece);at=end+n;continue; }
  if(*at=='[' && (end=strchr(at+1,']')) && end[1]=='(') {
   const gchar *stop=strchr(end+2,')');if(stop) { gchar *caption=g_markup_escape_text(at+1,end-at-1),*target=g_strndup(end+2,stop-end-2),*uri=uri_for(target,base),*escaped=g_markup_escape_text(uri,-1);g_string_append_printf(out,"<a href=\"%s\">%s</a>",escaped,caption);g_free(caption);g_free(target);g_free(uri);g_free(escaped);at=stop+1;continue; }
  }
  const gchar *next=g_utf8_next_char(at);gchar *piece=g_markup_escape_text(at,next-at);g_string_append(out,piece);g_free(piece);at=next;
 }
 return g_string_free(out,FALSE);
}
static gboolean load_document(Help *h,const gchar *path,gboolean remember);
static gboolean link_clicked(GtkLabel *label,const gchar *uri,gpointer data) {
 (void)label;Help *h=data;if(uri[0]=='#') { gtk_label_set_text(GTK_LABEL(h->status),"Anchor navigation is not available in the compact view; use the multimedia view");return TRUE; }
 gchar *path=g_filename_from_uri(uri,NULL,NULL);
 if(path && (g_str_has_suffix(path,".markdown") || g_str_has_suffix(path,".md") || g_str_has_suffix(path,".rst") || g_str_has_suffix(path,".rest"))) load_document(h,path,TRUE);
 else utils_open_browser(uri);
 g_free(path);return TRUE;
}
static void add_label(Help *h,const gchar *markup,gboolean code) {
 GtkWidget *label=gtk_label_new(NULL);gtk_label_set_markup(GTK_LABEL(label),markup);gtk_label_set_selectable(GTK_LABEL(label),TRUE);gtk_label_set_xalign(GTK_LABEL(label),0);gtk_label_set_line_wrap(GTK_LABEL(label),TRUE);gtk_label_set_line_wrap_mode(GTK_LABEL(label),PANGO_WRAP_WORD_CHAR);gtk_widget_set_hexpand(label,TRUE);gtk_widget_set_halign(label,GTK_ALIGN_FILL);gtk_widget_set_margin_bottom(label,code ? 10 : 5);g_signal_connect(label,"activate-link",G_CALLBACK(link_clicked),h);gtk_box_pack_start(GTK_BOX(h->body),label,FALSE,FALSE,0);
}
static gboolean video_file(const gchar *path) {
 gchar *lower=g_ascii_strdown(path,-1);gboolean video=g_str_has_suffix(lower,".mp4") || g_str_has_suffix(lower,".webm") || g_str_has_suffix(lower,".ogv") || g_str_has_suffix(lower,".ogg") || g_str_has_suffix(lower,".mov");g_free(lower);return video;
}
static void media_view(GtkWidget *widget,gpointer data) {
 (void)widget;Help *h=data;if(!h->html) return;GError *error=NULL;gchar *path=NULL;gint fd=g_file_open_tmp("geany-insight-preview-XXXXXX.html",&path,&error);
 if(fd>=0) { g_close(fd,NULL);if(g_file_set_contents(path,h->html,-1,&error)) { g_object_set_data_full(G_OBJECT(h->window),"insight-media-path",g_strdup(path),g_free);g_ptr_array_add(h->temporary,path);path=NULL;gchar *uri=g_filename_to_uri(g_ptr_array_index(h->temporary,h->temporary->len-1),NULL,NULL);utils_open_browser(uri);g_free(uri); } }
 if(error) { gtk_label_set_text(GTK_LABEL(h->status),error->message);g_error_free(error); }g_free(path);
}
static void add_media(Help *h,GString *html,const gchar *target,const gchar *caption,const gchar *base) {
 gchar *path=resolve(target,base),*uri=uri_for(target,base),*escaped=g_markup_escape_text(uri,-1),*alt=g_markup_escape_text(caption,-1);
 if(video_file(target)) {
  g_string_append_printf(html,"<figure><video controls preload=\"metadata\" src=\"%s\"></video><figcaption>%s</figcaption></figure>",escaped,alt);
  GtkWidget *button=gtk_button_new_with_label("▶ Video — open multimedia view");gtk_widget_set_tooltip_text(button,"Play videos in the browser media view; codec support depends on the browser");g_signal_connect(button,"clicked",G_CALLBACK(media_view),h);gtk_box_pack_start(GTK_BOX(h->body),button,FALSE,FALSE,0);add_label(h,alt,FALSE);
 } else {
  g_string_append_printf(html,"<figure><img src=\"%s\" alt=\"%s\"><figcaption>%s</figcaption></figure>",escaped,alt,alt);
  gint width=0,height=0;GStatBuf st;GError *error=NULL;GdkPixbufAnimation *animation=NULL;
  if(path && !g_stat(path,&st) && st.st_size<=32*1024*1024 && gdk_pixbuf_get_file_info(path,&width,&height) && width>0 && height>0 && (guint64)width*height<=16000000) animation=gdk_pixbuf_animation_new_from_file(path,&error);
  if(animation) { GtkWidget *image;
   if(gdk_pixbuf_animation_is_static_image(animation) && width>800) { GdkPixbuf *scaled=gdk_pixbuf_scale_simple(gdk_pixbuf_animation_get_static_image(animation),800,MAX(1,(gint)((gint64)height*800/width)),GDK_INTERP_BILINEAR);image=gtk_image_new_from_pixbuf(scaled);g_object_unref(scaled); }
   else image=gtk_image_new_from_animation(animation);
   gtk_box_pack_start(GTK_BOX(h->body),image,FALSE,FALSE,3);g_object_unref(animation);
  } else { gchar *message=g_strdup_printf("Image: %s — %s",caption,error ? error->message : "local file missing, unsupported format or image exceeds preview limits");gchar *escaped_message=g_markup_escape_text(message,-1);add_label(h,escaped_message,FALSE);g_free(message);g_free(escaped_message); }
  g_clear_error(&error);
 }
 g_free(path);g_free(uri);g_free(escaped);g_free(alt);
}
static gboolean table_separator(const gchar *line) {
 gboolean dash=FALSE;for(const gchar *at=line;*at;at++) { if(*at=='-') dash=TRUE;else if(*at!='|' && *at!=':' && !g_ascii_isspace(*at)) return FALSE; }return dash && strchr(line,'|');
}
static void table_row(Help *h,GtkWidget *grid,GString *html,const gchar *line,const gchar *base,guint row,gboolean header) {
 gchar **cells=g_strsplit(line,"|",-1);guint column=0;g_string_append(html,"<tr>");
 for(guint i=0;cells[i];i++) { gchar *cell=g_strstrip(cells[i]);if((i==0 || !cells[i+1]) && !*cell) continue;if(column>=32) break;gchar *markup=inline_text(cell,base,FALSE),*web=inline_text(cell,base,TRUE),*bold=header ? g_strdup_printf("<b>%s</b>",markup) : g_strdup(markup);GtkWidget *label=gtk_label_new(NULL);gtk_label_set_markup(GTK_LABEL(label),bold);gtk_label_set_selectable(GTK_LABEL(label),TRUE);gtk_label_set_xalign(GTK_LABEL(label),0);gtk_label_set_line_wrap(GTK_LABEL(label),TRUE);gtk_label_set_line_wrap_mode(GTK_LABEL(label),PANGO_WRAP_WORD_CHAR);gtk_widget_set_hexpand(label,TRUE);g_signal_connect(label,"activate-link",G_CALLBACK(link_clicked),h);gtk_grid_attach(GTK_GRID(grid),label,column++,row,1,1);g_string_append_printf(html,"<%s>%s</%s>",header ? "th" : "td",web,header ? "th" : "td");g_free(markup);g_free(web);g_free(bold); }
 g_string_append(html,"</tr>");g_strfreev(cells);
}
static gboolean underline(const gchar *line) { gsize len=strlen(line);if(len<3 || !strchr("=-~^",line[0])) return FALSE;for(gsize i=1;i<len;i++) if(line[i]!=line[0]) return FALSE;return TRUE; }
static gchar *media_target(const gchar *line,gchar **caption) {
 if(g_str_has_prefix(line,"![")) { const gchar *end=strstr(line,"]("),*stop=end ? strchr(end+2,')') : NULL;if(stop) { *caption=g_strndup(line+2,end-line-2);return g_strndup(end+2,stop-end-2); } }
 if(g_str_has_prefix(line,".. image:: ") || g_str_has_prefix(line,".. figure:: ") || g_str_has_prefix(line,".. video:: ")) { const gchar *at=strstr(line,":: ")+3;*caption=g_strdup(at);return g_strdup(at); }
 return NULL;
}
static gboolean load_document(Help *h,const gchar *filename,gboolean remember) {
 gchar *path=g_canonicalize_filename(filename,NULL);gchar *source=NULL;gsize length=0;GError *error=NULL;GStatBuf st;
 if(g_stat(path,&st) || st.st_size>8*1024*1024 || !g_file_get_contents(path,&source,&length,&error) || !g_utf8_validate(source,length,NULL) || memchr(source,0,length)) { gtk_label_set_text(GTK_LABEL(h->status),error ? error->message : "Cannot preview: expected UTF-8 text up to 8 MiB");g_clear_error(&error);g_free(source);g_free(path);return FALSE; }
 if(remember) { while(h->history->len && h->position+1<h->history->len) g_ptr_array_remove_index(h->history,h->history->len-1);g_ptr_array_add(h->history,g_strdup(path));h->position=h->history->len-1; }
 g_free(h->path);h->path=g_strdup(path);g_object_set_data(G_OBJECT(h->window),"insight-rendered-html",NULL);g_free(h->html);h->html=NULL;GList *children=gtk_container_get_children(GTK_CONTAINER(h->body));for(GList *p=children;p;p=p->next) gtk_widget_destroy(p->data);g_list_free(children);
 gchar *base=g_path_get_dirname(path),*name=g_path_get_basename(path),*title=g_strdup_printf("Geany Insight — %s",name);gtk_window_set_title(GTK_WINDOW(h->window),title);g_free(title);g_free(name);
 GString *html=g_string_new("<!doctype html><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\"><title>Geany Insight</title><style>body{font:17px system-ui;max-width:960px;margin:32px auto;padding:0 20px;line-height:1.55;color:#26384b}pre{background:#eef3f8;padding:16px;overflow:auto}code{font-family:monospace}a{color:#1766a6}img,video{max-width:100%}table{border-collapse:collapse}td,th{padding:8px;border:1px solid #b5c6d8}figure{margin:16px 0}</style><main>");
 gchar **lines=g_strsplit(source,"\n",-1);gboolean fenced=FALSE,rstcode=FALSE;GString *code=g_string_new(NULL);
 for(guint i=0;lines[i] && i<10000;i++) {
  gchar *line=lines[i];gsize len=strlen(line);if(len && line[len-1]=='\r') line[len-1]=0;
  gboolean marker=g_str_has_prefix(line,"```") || g_str_has_prefix(line,"~~~");
  if(marker || (rstcode && *line && !g_str_has_prefix(line,"   "))) { if(fenced || rstcode) { gchar *escaped=g_markup_escape_text(code->str,-1),*markup=g_strdup_printf("<tt>%s</tt>",escaped);add_label(h,markup,TRUE);g_string_append_printf(html,"<pre><code>%s</code></pre>",escaped);g_free(escaped);g_free(markup);g_string_truncate(code,0);fenced=rstcode=FALSE;if(marker) continue; }else { fenced=TRUE;continue; } }
  if(fenced || rstcode) { g_string_append(code,rstcode && g_str_has_prefix(line,"   ") ? line+3 : line);g_string_append_c(code,'\n');continue; }
  if(g_str_has_prefix(line,".. code-block::") || g_str_has_prefix(line,".. code::")) { rstcode=TRUE;continue; }
  if(!*line) continue;
  gchar *caption=NULL,*target=media_target(line,&caption);if(target) { add_media(h,html,target,caption,base);g_free(target);g_free(caption);continue; }
  if(strchr(line,'|') && lines[i+1] && table_separator(lines[i+1])) {
   GtkWidget *grid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(grid),16);gtk_grid_set_row_spacing(GTK_GRID(grid),6);gtk_widget_set_margin_bottom(grid,12);gtk_box_pack_start(GTK_BOX(h->body),grid,FALSE,FALSE,0);g_string_append(html,"<table>");table_row(h,grid,html,line,base,0,TRUE);i++;guint row=1;
   while(lines[i+1] && strchr(lines[i+1],'|') && row<256) { i++;table_row(h,grid,html,lines[i],base,row++,FALSE); }g_string_append(html,"</table>");continue;
  }
  guint heading=0;while(line[heading]=='#' && heading<6) heading++;if(heading && line[heading]!=' ') heading=0;
  if(!heading && lines[i+1] && underline(lines[i+1])) { heading=lines[i+1][0]=='=' ? 1 : 2;lines[++i][0]=0; }
  const gchar *text=line+(line[0]=='#' && heading ? heading+1 : 0);gchar *markup=inline_text(text,base,FALSE),*web=inline_text(text,base,TRUE);
  if(heading) { gchar *large=g_strdup_printf("<span size=\"%s\" weight=\"bold\">%s</span>",heading==1 ? "xx-large" : (heading==2 ? "x-large" : "large"),markup);add_label(h,large,FALSE);g_string_append_printf(html,"<h%u>%s</h%u>",heading,web,heading);g_free(large); }
  else if(g_str_has_prefix(text,"- ") || g_str_has_prefix(text,"* ")) { gchar *item=inline_text(text+2,base,FALSE),*item_web=inline_text(text+2,base,TRUE),*bullet=g_strdup_printf("• %s",item);add_label(h,bullet,FALSE);g_string_append_printf(html,"<ul><li>%s</li></ul>",item_web);g_free(item);g_free(item_web);g_free(bullet); }
  else { add_label(h,markup,FALSE);g_string_append_printf(html,"<p>%s</p>\n",web); }
  g_free(markup);g_free(web);
 }
 if(code->len) { gchar *escaped=g_markup_escape_text(code->str,-1),*markup=g_strdup_printf("<tt>%s</tt>",escaped);add_label(h,markup,TRUE);g_string_append_printf(html,"<pre><code>%s</code></pre>",escaped);g_free(escaped);g_free(markup); }
 g_string_free(code,TRUE);g_strfreev(lines);g_string_append(html,"</main>");h->html=g_string_free(html,FALSE);g_object_set_data(G_OBJECT(h->window),"insight-rendered-html",h->html);g_free(base);g_free(source);
 gtk_widget_show_all(h->body);gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(h->scroll)),0);gtk_widget_set_sensitive(h->back,h->position>0);gtk_widget_set_sensitive(h->forward,h->position+1<h->history->len);gtk_label_set_text(GTK_LABEL(h->status),path);g_free(path);return TRUE;
}
static void history(GtkWidget *widget,gpointer data) { (void)widget;Help *h=help;if(!h) return;gint next=(gint)h->position+GPOINTER_TO_INT(data);if(next<0 || next>=(gint)h->history->len) return;h->position=(guint)next;load_document(h,g_ptr_array_index(h->history,h->position),FALSE); }
static void refresh(GtkWidget *widget,gpointer data) { (void)widget;Help *h=data;if(h->path) load_document(h,h->path,FALSE); }
static void destroy(GtkWidget *widget,gpointer data) { (void)widget;Help *h=data;g_ptr_array_free(h->history,TRUE);/* Media pages may still be open in the browser, so preserve them for this session. */g_ptr_array_free(h->temporary,TRUE);g_free(h->path);g_free(h->html);help=NULL;g_free(h); }
static Help *ensure_help(void) {
 if(help) return help;
 Help *h=g_new0(Help,1);help=h;h->history=g_ptr_array_new_with_free_func(g_free);h->temporary=g_ptr_array_new_with_free_func(g_free);h->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(h->window,"insight-help-viewer");gtk_window_set_default_size(GTK_WINDOW(h->window),900,700);gtk_window_set_transient_for(GTK_WINDOW(h->window),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(h->window),TRUE);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6),*bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,5);gtk_container_add(GTK_CONTAINER(h->window),box);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
 const gchar *icons[]={"go-previous","go-next","folder-open","view-refresh","media"};const gchar *tips[]={"Previous page","Next page","Open Markdown / reStructuredText…","Reload document","Multimedia view — render in browser, including video"};
 for(guint i=0;i<5;i++) { GtkWidget *button=gtk_button_new();gtk_button_set_image(GTK_BUTTON(button),insight_command_icon(icons[i]));gtk_widget_set_tooltip_text(button,tips[i]);gtk_box_pack_start(GTK_BOX(bar),button,FALSE,FALSE,0);if(i<2) { if(i) h->forward=button;else h->back=button;g_signal_connect(button,"clicked",G_CALLBACK(history),GINT_TO_POINTER(i ? 1 : -1)); }else g_signal_connect(button,"clicked",i==2 ? G_CALLBACK(insight_markup_open) : (i==3 ? G_CALLBACK(refresh) : G_CALLBACK(media_view)),h); }
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);h->scroll=scroll;h->body=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);gtk_widget_set_margin_start(h->body,20);gtk_widget_set_margin_end(h->body,20);gtk_widget_set_margin_top(h->body,16);gtk_container_add(GTK_CONTAINER(scroll),h->body);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);h->status=gtk_label_new("");gtk_label_set_ellipsize(GTK_LABEL(h->status),PANGO_ELLIPSIZE_MIDDLE);gtk_box_pack_start(GTK_BOX(box),h->status,FALSE,FALSE,3);g_signal_connect(h->window,"destroy",G_CALLBACK(destroy),h);gtk_widget_show_all(h->window);return h;
}
GEANY_API_SYMBOL
void insight_markup_open_path(const gchar *path) {
 Help *h=ensure_help();load_document(h,path,TRUE);gtk_window_present(GTK_WINDOW(h->window));
}
void insight_help_show(GtkWidget *widget,gpointer topic) {
 (void)widget;const gchar *name=topic ? topic : "guide.md";gchar *path=g_build_filename(app->docdir,"insight",name,NULL);
 if(!g_file_test(path,G_FILE_TEST_IS_REGULAR)) { g_free(path);path=g_build_filename(app->datadir,"insight","docs",name,NULL); }
 insight_markup_open_path(path);g_free(path);
}
void insight_markup_open(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;GtkWidget *dialog=gtk_file_chooser_dialog_new("Open Markdown / reStructuredText",GTK_WINDOW(main_widgets.window),GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);GtkFileFilter *filter=gtk_file_filter_new();gtk_file_filter_set_name(filter,"Markdown / reStructuredText (*.md, *.rst, *.rest)");gtk_file_filter_add_pattern(filter,"*.md");gtk_file_filter_add_pattern(filter,"*.markdown");gtk_file_filter_add_pattern(filter,"*.rst");gtk_file_filter_add_pattern(filter,"*.rest");gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);GtkFileFilter *all=gtk_file_filter_new();gtk_file_filter_set_name(all,"All files");gtk_file_filter_add_pattern(all,"*");gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),all);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));insight_markup_open_path(path);g_free(path); }gtk_widget_destroy(dialog);
}
