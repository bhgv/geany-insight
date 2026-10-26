#define INSIGHT_LOCALIZE_FORMATS
#include "insight-i18n.h"
#include "../insight/desktop/glib_compat.h"
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
#include "insight-ai.h"
#include "keybindings.h"
#include <glib/gstdio.h>
#include <string.h>

/* A portable GTK renderer. No web engine, network fetches or raw HTML scripts
 * are needed for documentation. The HTML media view delegates codecs to the
 * user's browser rather than assuming a video backend exists in GTK3. */
typedef struct {
 GtkWidget *ai_prompt,*window,*body,*status,*back,*forward,*scroll,*toc,*find_row,*find_entry,*find_status,*scroll_target;GPtrArray *history,*headings,*matches;guint position,scroll_idle;gint scroll_offset;gint match;gboolean filling;
 gchar *path,*html,*key;GPtrArray *temporary;GHashTable *answers;guint answer_serial;
} Help;
static Help *help;
static gboolean icon_draw(GtkWidget *widget,cairo_t *cr,gpointer data) {
 if(GTK_IS_DRAWING_AREA(widget)) cairo_scale(cr,gtk_widget_get_allocated_width(widget)/16.0,gtk_widget_get_allocated_height(widget)/16.0);
 const gchar *id=data;GdkRGBA color;gtk_style_context_get_color(gtk_widget_get_style_context(widget),gtk_widget_get_state_flags(widget),&color);gdk_cairo_set_source_rgba(cr,&color);cairo_set_line_width(cr,1.2);
 if(g_str_has_prefix(id,"layout-")) { cairo_rectangle(cr,1,2,14,12);cairo_stroke(cr);cairo_set_source_rgb(cr,.2,.55,.85);if(id[7]=='a' || id[7]=='d') cairo_rectangle(cr,2,3,4,10);if(id[7]=='b') cairo_rectangle(cr,2,10,12,3);if(id[7]=='c' || id[7]=='d') cairo_rectangle(cr,10,3,4,10);cairo_fill(cr); }
 else if(!strcmp(id,"media")) { cairo_arc(cr,8,8,6,0,2*G_PI);cairo_move_to(cr,2,8);cairo_line_to(cr,14,8);cairo_move_to(cr,8,2);cairo_curve_to(cr,3,6,3,10,8,14);cairo_curve_to(cr,13,10,13,6,8,2);cairo_stroke(cr); }
 else if(!strcmp(id,"diff")) { cairo_rectangle(cr,1,2,5,12);cairo_rectangle(cr,10,2,5,12);cairo_stroke(cr);cairo_set_source_rgb(cr,.2,.55,.85);cairo_move_to(cr,4,5);cairo_line_to(cr,12,5);cairo_move_to(cr,9,3);cairo_line_to(cr,12,5);cairo_line_to(cr,9,7);cairo_move_to(cr,12,11);cairo_line_to(cr,4,11);cairo_stroke(cr); }
 else if(!strcmp(id,"panel-help")) { cairo_set_source_rgb(cr,.2,.55,.85); cairo_arc(cr,8,8,7,0,2*G_PI); cairo_stroke(cr); cairo_select_font_face(cr,"Sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_BOLD); cairo_set_font_size(cr,12); cairo_move_to(cr,4.5,12); cairo_show_text(cr,"?"); }
 else if(!strcmp(id,"snippets")) { cairo_rectangle(cr,2,1,12,14); cairo_stroke(cr); cairo_set_source_rgb(cr,.2,.55,.85); cairo_move_to(cr,6,5); cairo_line_to(cr,3,8); cairo_line_to(cr,6,11); cairo_move_to(cr,10,5); cairo_line_to(cr,13,8); cairo_line_to(cr,10,11); cairo_stroke(cr); }
 else if(!strcmp(id,"relations")) { cairo_move_to(cr,4,8);cairo_line_to(cr,9,8);cairo_move_to(cr,9,3);cairo_line_to(cr,9,13);cairo_move_to(cr,9,3);cairo_line_to(cr,13,3);cairo_move_to(cr,9,13);cairo_line_to(cr,13,13);cairo_stroke(cr);cairo_set_source_rgb(cr,.2,.55,.85);cairo_rectangle(cr,1,6,4,4);cairo_rectangle(cr,12,1,3,4);cairo_rectangle(cr,12,11,3,4);cairo_fill(cr); }
 else { cairo_move_to(cr,8,3);cairo_curve_to(cr,4,0,1,2,1,3);cairo_line_to(cr,1,13);cairo_curve_to(cr,4,11,6,12,8,14);cairo_curve_to(cr,10,12,12,11,15,13);cairo_line_to(cr,15,3);cairo_curve_to(cr,12,1,10,1,8,3);cairo_line_to(cr,8,14);cairo_stroke(cr);cairo_set_source_rgb(cr,.2,.55,.85);if(!strcmp(id,"tutorial")) { cairo_move_to(cr,10,5);cairo_line_to(cr,14,8);cairo_line_to(cr,10,11);cairo_close_path(cr); }else if(!strcmp(id,"bookmarks")) { cairo_move_to(cr,10,1);cairo_line_to(cr,13,1);cairo_line_to(cr,13,8);cairo_line_to(cr,11.5,6.5);cairo_line_to(cr,10,8);cairo_close_path(cr); }else { cairo_rectangle(cr,3,5,3,1);cairo_rectangle(cr,3,8,3,1);cairo_rectangle(cr,10,5,3,1);cairo_rectangle(cr,10,8,3,1); }cairo_fill(cr); }
 return FALSE;
}
GtkWidget *insight_command_icon(const gchar *command) {
 if(!strcmp(command,"snippets") || !strcmp(command,"panel-help") || !strcmp(command,"relations")) { GtkWidget *image=gtk_image_new(); cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,16,16); cairo_t *cr=cairo_create(surface); icon_draw(image,cr,(gpointer)command); cairo_destroy(cr); gtk_image_set_from_surface(GTK_IMAGE(image),surface);g_object_set_data_full(G_OBJECT(image),"insight-icon-surface",cairo_surface_reference(surface),(GDestroyNotify)cairo_surface_destroy); cairo_surface_destroy(surface); g_object_set_data_full(G_OBJECT(image),"insight-icon-name",g_strdup(command),g_free); return image; }
 if(g_str_has_prefix(command,"layout-") || !strcmp(command,"media") || !strcmp(command,"manual") || !strcmp(command,"tutorial") || !strcmp(command,"bookmarks") || !strcmp(command,"diff") || !strcmp(command,"snippets") || !strcmp(command,"relations")) { GtkWidget *icon=gtk_drawing_area_new();gtk_widget_set_size_request(icon,16,16);g_object_set_data_full(G_OBJECT(icon),"insight-icon-name",g_strdup(command),g_free);g_signal_connect(icon,"draw",G_CALLBACK(icon_draw),g_object_get_data(G_OBJECT(icon),"insight-icon-name"));return icon; }
 return gtk_image_new_from_icon_name(command,GTK_ICON_SIZE_MENU);
}
static const gchar *command_icon(const gchar *label) {
 label=insight_i18n_english(label);
 const gchar *layout=strstr(label,"layout ");if(layout) { switch(layout[7]) { case 'A':return "layout-a";case 'B':return "layout-b";case 'C':return "layout-c";case 'D':return "layout-d";default:break; } }
 if(strstr(label,"Markdown")) return "text-x-generic";
 if(strstr(label,"Diff") || strstr(label,"Merge")) return "diff";
 if(strstr(label,"Code templates") || strstr(label,"code template")) return "snippets";
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
 return NULL;
}
static void menu_icon_space(GtkMenuItem *item,gint *size,gpointer data) { (void)item;(void)data;*size=MAX(*size,16); }
static gboolean menu_icon_draw(GtkWidget *widget,cairo_t *cr,gpointer data) {
 const gchar *id=data;GtkStyleContext *style=gtk_widget_get_style_context(widget);GtkBorder padding,border;
 gtk_style_context_get_padding(style,gtk_widget_get_state_flags(widget),&padding);gtk_style_context_get_border(style,gtk_widget_get_state_flags(widget),&border);
 gint x=gtk_widget_get_direction(widget)==GTK_TEXT_DIR_RTL ? gtk_widget_get_allocated_width(widget)-padding.right-border.right-16 : padding.left+border.left;
 cairo_save(cr);cairo_translate(cr,x,(gtk_widget_get_allocated_height(widget)-16)/2);
 if(g_str_has_prefix(id,"layout-") || !strcmp(id,"media") || !strcmp(id,"manual") || !strcmp(id,"tutorial") || !strcmp(id,"bookmarks") || !strcmp(id,"diff") || !strcmp(id,"snippets") || !strcmp(id,"relations")) icon_draw(widget,cr,data);
 else { GdkPixbuf *pixbuf=gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),id,16,GTK_ICON_LOOKUP_USE_BUILTIN,NULL);if(pixbuf) { gdk_cairo_set_source_pixbuf(cr,pixbuf,0,0);cairo_paint(cr);g_object_unref(pixbuf); } }
 cairo_restore(cr);return FALSE;
}
/* Keep the real GTK menu, not an imitation assembled from buttons: native
 * tearoffs preserve icons, accelerator columns, custom rows and activation.
 * GTK3 retains this API, including our minimum supported GTK 3.22. */
static void palette_cleanup_handlers(GtkWidget *menu,gboolean block) {
 GArray *ids=g_object_get_data(G_OBJECT(menu),"insight-palette-cleanup");
 if(block) {
  if(!ids) {ids=g_array_new(FALSE,FALSE,sizeof(gulong));g_object_set_data_full(G_OBJECT(menu),"insight-palette-cleanup",ids,(GDestroyNotify)g_array_unref);}
  guint signal=g_signal_lookup("selection-done",GTK_TYPE_MENU_SHELL);gulong id;
  /* Owners of transient context menus commonly destroy on selection-done.
   * Defer only those connected handlers while GTK keeps the menu torn off. */
  while((id=g_signal_handler_find(menu,G_SIGNAL_MATCH_ID|G_SIGNAL_MATCH_UNBLOCKED,signal,0,NULL,NULL,NULL))) {g_signal_handler_block(menu,id);g_array_append_val(ids,id);}
 } else if(ids) {
  for(guint i=0;i<ids->len;i++) {gulong id=g_array_index(ids,gulong,i);if(g_signal_handler_is_connected(menu,id)) g_signal_handler_unblock(menu,id);}
  g_object_set_data(G_OBJECT(menu),"insight-palette-cleanup",NULL);
 }
}
static gboolean palette_finish_dismissal(gpointer data) {
 GtkWidget *menu=data;g_object_set_data(G_OBJECT(menu),"insight-palette-dismiss",NULL);
 if(!gtk_widget_in_destruction(menu) && !gtk_menu_get_tearoff_state(GTK_MENU(menu))) {
  GList *items=gtk_container_get_children(GTK_CONTAINER(menu));
  for(GList *p=items;p;p=p->next) if(g_object_get_data(G_OBJECT(p->data),"insight-palette-spacer")) gtk_widget_destroy(p->data);
  g_list_free(items);
  palette_cleanup_handlers(menu,FALSE);
  if(!gtk_menu_get_attach_widget(GTK_MENU(menu))) g_signal_emit_by_name(menu,"selection-done");
 }
 return G_SOURCE_REMOVE;
}
static void palette_menu_destroy(GtkWidget *menu,gpointer data) {
 (void)data;guint pending=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(menu),"insight-palette-dismiss"));
 g_object_set_data(G_OBJECT(menu),"insight-palette-dismiss",NULL);if(pending) g_source_remove(pending);
}
static void palette_state(GObject *object,GParamSpec *spec,gpointer data) {
 (void)spec;(void)data;GtkWidget *menu=GTK_WIDGET(object);
 if(gtk_widget_in_destruction(menu)) return;
 if(gtk_menu_get_tearoff_state(GTK_MENU(menu))) {
  palette_cleanup_handlers(menu,TRUE);GtkWidget *window=gtk_widget_get_toplevel(menu);
  if(GTK_IS_WINDOW(window)) {
   /* GTK's legacy tearoff creates an app-paintable RGB toplevel but never
    * paints its backing. Rounded menus/scrollbar gutters then expose black
    * pixels. Let GtkWindow paint the current theme beneath all children;
    * this also works without an alpha visual or a compositing manager. */
   gtk_widget_set_app_paintable(window,FALSE);gtk_widget_queue_draw(window);
   gtk_widget_set_name(window,"insight-menu-palette");g_object_set_data(G_OBJECT(menu),"insight-palette-window",window);
   if(GTK_IS_WINDOW(main_widgets.window)) {gtk_window_set_transient_for(GTK_WINDOW(window),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(window),TRUE);}
  }
 } else {
  g_object_set_data(G_OBJECT(menu),"insight-palette-window",NULL);
  /* GTK still adjusts its tearoff child geometry after notifying the property.
   * Owner cleanup must run after that native setter has completely returned. */
  if(!g_object_get_data(G_OBJECT(menu),"insight-palette-dismiss")) {
   guint pending=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,palette_finish_dismissal,g_object_ref(menu),g_object_unref);
   g_object_set_data(G_OBJECT(menu),"insight-palette-dismiss",GUINT_TO_POINTER(pending));
  }
 }
}
static void menu_palette_open(GtkWidget *menu) {
 if(gtk_widget_in_destruction(menu)) return;
 gboolean opening=!gtk_menu_get_tearoff_state(GTK_MENU(menu));
 gint width=gtk_widget_get_allocated_width(menu),height=gtk_widget_get_allocated_height(menu);
 if(opening) {
  palette_cleanup_handlers(menu,TRUE);
  /* GtkTearoffMenuItem normally runs after GtkMenuShell has deactivated the
   * popup. Our 7px handle intercepts activation to retain transient owners,
   * so perform that popdown before GTK changes its torn-off bookkeeping. */
  gint natural=0;gtk_widget_get_preferred_height(menu,NULL,&natural);
  gtk_menu_popdown(GTK_MENU(menu));
  /* A popup can include spare bottom allocation beyond its requisition.
   * Retain that blank space as a temporary, nonselectable row so GTK's
   * native tearoff size hints preserve it without resize feedback loops. */
  if(height>natural) {
   GtkWidget *spacer=gtk_separator_menu_item_new();
   g_object_set_data(G_OBJECT(spacer),"insight-palette-spacer",GINT_TO_POINTER(1));
   GtkCssProvider *css=gtk_css_provider_new();
   gchar *rules=g_strdup_printf("* { min-height: %dpx; min-width: 0; padding: 0; margin: 0; border-width: 0; background: transparent; background-image: none; }",height-natural);
   gtk_css_provider_load_from_data(css,rules,-1,NULL);g_free(rules);
   gtk_style_context_add_provider(gtk_widget_get_style_context(spacer),GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION+10);g_object_unref(css);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu),spacer);gtk_widget_show(spacer);
  }
 }
 GtkWidget *attach=gtk_menu_get_attach_widget(GTK_MENU(menu));
 const gchar *title=GTK_IS_MENU_ITEM(attach) ? gtk_menu_item_get_label(GTK_MENU_ITEM(attach)):NULL;
 gtk_menu_set_title(GTK_MENU(menu),title ? title:insight_tr("Menu palette"));
 gtk_menu_set_tearoff_state(GTK_MENU(menu),!gtk_menu_get_tearoff_state(GTK_MENU(menu)));
 if(opening && width>1 && height>1) {
  GtkWidget *window=gtk_widget_get_toplevel(menu);
  if(GTK_IS_WINDOW(window)) gtk_window_resize(GTK_WINDOW(window),width,height);
 }
}
static gboolean palette_strip_draw(GtkWidget *item,cairo_t *cr,gpointer data) {
 (void)data;gboolean hover=(gtk_widget_get_state_flags(item)&GTK_STATE_FLAG_PRELIGHT)!=0;
 cairo_set_source_rgb(cr,hover ? .12:.18,hover ? .48:.40,hover ? .90:.76);cairo_rectangle(cr,0,0,gtk_widget_get_allocated_width(item),gtk_widget_get_allocated_height(item));cairo_fill(cr);return FALSE;
}
static void palette_detach(GtkMenuItem *item,gpointer data) { (void)item;menu_palette_open(data); }
static gboolean palette_menu_event(GtkWidget *menu,GdkEvent *event,gpointer data) {
 (void)data;GtkWidget *selected=gtk_menu_shell_get_selected_item(GTK_MENU_SHELL(menu));
 if(!selected || !g_object_get_data(G_OBJECT(selected),"insight-palette-action")) return FALSE;
 gboolean trigger=event->type==GDK_BUTTON_RELEASE && event->button.button==1;
 if(event->type==GDK_KEY_PRESS) trigger=event->key.keyval==GDK_KEY_Return || event->key.keyval==GDK_KEY_KP_Enter || event->key.keyval==GDK_KEY_space;
 if(trigger) {menu_palette_open(menu);return TRUE;}return FALSE;
}
static void menu_palette_offer(GtkWidget *menu) {
 if(!GTK_IS_MENU(menu) || GTK_IS_COMBO_BOX(gtk_menu_get_attach_widget(GTK_MENU(menu)))) return;
 if(!g_object_get_data(G_OBJECT(menu),"insight-palette-events")) {
  g_object_set_data(G_OBJECT(menu),"insight-palette-events",GINT_TO_POINTER(1));
  g_signal_connect(menu,"button-release-event",G_CALLBACK(palette_menu_event),NULL);
  g_signal_connect(menu,"key-press-event",G_CALLBACK(palette_menu_event),NULL);
  g_signal_connect(menu,"notify::tearoff-state",G_CALLBACK(palette_state),NULL);
  g_signal_connect(menu,"destroy",G_CALLBACK(palette_menu_destroy),NULL);
 }
 if(gtk_menu_get_tearoff_state(GTK_MENU(menu))) palette_cleanup_handlers(menu,TRUE);
 /* Dynamic menu rebuilds may remove the old handle: discover it each time. */
 GList *items=gtk_container_get_children(GTK_CONTAINER(menu));gboolean found=FALSE;
 for(GList *p=items;p;p=p->next) if(g_object_get_data(G_OBJECT(p->data),"insight-palette-action")) {found=TRUE;break;}
 g_list_free(items);if(found) return;
 GtkWidget *item=gtk_menu_item_new(),*strip=gtk_drawing_area_new();gtk_widget_set_name(item,"insight-menu-detach");gtk_widget_set_size_request(strip,1,7);gtk_container_add(GTK_CONTAINER(item),strip);
 GtkCssProvider *css=gtk_css_provider_new();gtk_css_provider_load_from_data(css,"* { min-height: 7px; min-width: 0; padding: 0; margin: 0; border-width: 0; }",-1,NULL);gtk_style_context_add_provider(gtk_widget_get_style_context(item),GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION+10);g_object_unref(css);
 insight_i18n_key(G_OBJECT(item),"tooltip-text","Open as palette");atk_object_set_name(gtk_widget_get_accessible(item),insight_tr("Open as palette"));
 g_object_set_data(G_OBJECT(item),"insight-palette-action",GINT_TO_POINTER(1));g_object_set_data(G_OBJECT(item),"insight-icon-added",GINT_TO_POINTER(1));g_signal_connect_after(item,"draw",G_CALLBACK(palette_strip_draw),NULL);g_signal_connect(item,"activate",G_CALLBACK(palette_detach),menu);gtk_menu_shell_prepend(GTK_MENU_SHELL(menu),item);gtk_widget_show_all(item);
}
static gboolean menus_shown(GSignalInvocationHint *hint,guint count,const GValue *values,gpointer data) {
 (void)hint;(void)data;if(count) { GtkWidget *widget=g_value_get_object(values);if(GTK_IS_MENU(widget)) insight_menu_icons(widget); }return TRUE;
}
static void icons_on_show(GtkWidget *menu,gpointer data) { (void)data;insight_menu_icons(menu); }
void insight_menu_icons(GtkWidget *menu) {
 if(!GTK_IS_CONTAINER(menu)) return;
 menu_palette_offer(menu);
 if(GTK_IS_MENU(menu) && GTK_IS_COMBO_BOX(gtk_menu_get_attach_widget(GTK_MENU(menu)))) return;
 /* Catch subsequently created command popups and plugin menus as well. */
 static gulong hook;if(!hook) hook=g_signal_add_emission_hook(g_signal_lookup("show",GTK_TYPE_WIDGET),0,menus_shown,NULL,NULL);
 if(GTK_IS_MENU(menu) && !g_object_get_data(G_OBJECT(menu),"insight-icons-watched")) { g_object_set_data(G_OBJECT(menu),"insight-icons-watched",GINT_TO_POINTER(1));g_signal_connect_after(menu,"show",G_CALLBACK(icons_on_show),NULL); }
 GList *children=gtk_container_get_children(GTK_CONTAINER(menu));
 for(GList *p=children;p;p=p->next) {
  GtkWidget *item=p->data;if(!GTK_IS_MENU_ITEM(item) || GTK_IS_SEPARATOR_MENU_ITEM(item)) continue;
  GtkWidget *submenu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));if(submenu) insight_menu_icons(submenu);
  if(GTK_IS_MENU_BAR(menu) || GTK_IS_CHECK_MENU_ITEM(item) || g_object_get_data(G_OBJECT(item),"insight-icon-added")) continue;
  const gchar *label=gtk_menu_item_get_label(GTK_MENU_ITEM(item));const gchar *id=command_icon(label ? label : "");
  /* Keep native GtkAccelLabel/accelerator association and the original action
   * widget intact. GtkImageMenuItem needs always-show-image on GTK themes. */
  if(GTK_IS_IMAGE_MENU_ITEM(item)) { if(!gtk_image_menu_item_get_image(GTK_IMAGE_MENU_ITEM(item)) && id) gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),insight_command_icon(id));if(gtk_image_menu_item_get_image(GTK_IMAGE_MENU_ITEM(item))) gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item),TRUE); }
  else if(id) { /* Preserve GtkAccelLabel: Geany also reads labels for MRU sorting. */
   g_object_set_data_full(G_OBJECT(item),"insight-menu-icon",g_strdup(id),g_free);g_signal_connect(item,"toggle-size-request",G_CALLBACK(menu_icon_space),NULL);g_signal_connect_after(item,"draw",G_CALLBACK(menu_icon_draw),g_object_get_data(G_OBJECT(item),"insight-menu-icon"));
  }
  g_object_set_data(G_OBJECT(item),"insight-icon-added",GINT_TO_POINTER(1));
 }
 g_list_free(children);
}
static gchar *resolve(const gchar *path,const gchar *base) {
 if(g_str_has_prefix(path,"file://")) return g_filename_from_uri(path,NULL,NULL);
 if(strstr(path,"://") || g_str_has_prefix(path,"data:") || g_str_has_prefix(path,"javascript:")) return NULL;
 return insight_canonicalize_filename(path,base);
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
typedef struct { GtkWidget *label;gint begin,end; } HelpMatch;
typedef struct { Help *h;GRegex *regex; } FindState;
static void free_match(gpointer data) { HelpMatch *match=data;g_object_unref(match->label);g_free(match); }
static void clear_find(Help *h) {
 if(h->match>=0 && (guint)h->match<h->matches->len) { HelpMatch *match=g_ptr_array_index(h->matches,h->match);gtk_label_select_region(GTK_LABEL(match->label),0,0); }
 g_ptr_array_set_size(h->matches,0);h->match=-1;
}
static gboolean scroll_idle(gpointer data) {
 Help *h=data;h->scroll_idle=0;gint x,y;
 if(h->scroll_target && gtk_widget_translate_coordinates(h->scroll_target,h->body,0,0,&x,&y)) { GtkAdjustment *adjustment=gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(h->scroll));gtk_adjustment_set_value(adjustment,CLAMP(y+h->scroll_offset,gtk_adjustment_get_lower(adjustment),MAX(gtk_adjustment_get_lower(adjustment),gtk_adjustment_get_upper(adjustment)-gtk_adjustment_get_page_size(adjustment)))); }
 g_clear_object(&h->scroll_target);return G_SOURCE_REMOVE;
}
static void reveal(Help *h,GtkWidget *widget) {
 if(h->scroll_idle) g_source_remove(h->scroll_idle);
 g_clear_object(&h->scroll_target);h->scroll_target=g_object_ref(widget);h->scroll_offset=0;h->scroll_idle=g_idle_add_full(G_PRIORITY_LOW,scroll_idle,h,NULL);
}
static void toc_changed(GtkComboBox *combo,gpointer data) {
 Help *h=data;if(h->filling) return;gint selected=gtk_combo_box_get_active(combo);if(selected>=0 && (guint)selected<h->headings->len) reveal(h,g_ptr_array_index(h->headings,selected));
}
static void find_collect(GtkWidget *widget,gpointer data) {
 FindState *state=data;if(state->h->matches->len>=2048) return;
 if(GTK_IS_LABEL(widget) && gtk_label_get_selectable(GTK_LABEL(widget))) {
  const gchar *text=gtk_label_get_text(GTK_LABEL(widget));GMatchInfo *info=NULL;g_regex_match(state->regex,text,0,&info);
  while(g_match_info_matches(info) && state->h->matches->len<2048) { gint a,b;g_match_info_fetch_pos(info,0,&a,&b);HelpMatch *match=g_new(HelpMatch,1);match->label=g_object_ref(widget);match->begin=g_utf8_pointer_to_offset(text,text+a);match->end=g_utf8_pointer_to_offset(text,text+b);g_ptr_array_add(state->h->matches,match);g_match_info_next(info,NULL); }g_match_info_free(info);
 }else if(GTK_IS_CONTAINER(widget)) gtk_container_foreach(GTK_CONTAINER(widget),find_collect,data);
}
static void find_select(Help *h,gint direction) {
 if(!h->matches->len) { gtk_label_set_text(GTK_LABEL(h->find_status),"No matches");return; }
 if(h->match>=0) { HelpMatch *old=g_ptr_array_index(h->matches,h->match);gtk_label_select_region(GTK_LABEL(old->label),0,0); }
 h->match=h->match<0 ? (direction<0 ? (gint)h->matches->len-1 : 0) : (h->match+direction+(gint)h->matches->len)%(gint)h->matches->len;
 HelpMatch *match=g_ptr_array_index(h->matches,h->match);gtk_label_select_region(GTK_LABEL(match->label),match->begin,match->end);reveal(h,match->label);PangoRectangle rect;const gchar *text=gtk_label_get_text(GTK_LABEL(match->label));pango_layout_index_to_pos(gtk_label_get_layout(GTK_LABEL(match->label)),g_utf8_offset_to_pointer(text,match->begin)-text,&rect);gint ox,oy;gtk_label_get_layout_offsets(GTK_LABEL(match->label),&ox,&oy);h->scroll_offset=oy+rect.y/PANGO_SCALE;gchar *status=g_strdup_printf("%d / %u%s",h->match+1,h->matches->len,h->matches->len==2048 ? " (limit)" : "");gtk_label_set_text(GTK_LABEL(h->find_status),status);g_free(status);
}
static void find_changed(GtkWidget *widget,gpointer data) {
 (void)widget;Help *h=data;clear_find(h);const gchar *query=gtk_entry_get_text(GTK_ENTRY(h->find_entry));if(!*query) { gtk_label_set_text(GTK_LABEL(h->find_status),"");return; }
 gchar *escaped=g_regex_escape_string(query,-1);GRegex *regex=g_regex_new(escaped,G_REGEX_CASELESS,0,NULL);g_free(escaped);if(!regex) return;
 FindState state={h,regex};find_collect(h->body,&state);g_regex_unref(regex);find_select(h,1);
}
static void find_step(GtkWidget *widget,gpointer data) { (void)widget;if(help) find_select(help,GPOINTER_TO_INT(data)); }
static void find_close(GtkWidget *widget,gpointer data) { (void)widget;Help *h=data;clear_find(h);if(h->scroll_idle) { g_source_remove(h->scroll_idle);h->scroll_idle=0; }g_clear_object(&h->scroll_target);gtk_widget_hide(h->find_row); }
static gboolean help_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;Help *h=data;GdkModifierType mods=event->state & gtk_accelerator_get_default_mod_mask();
 if(mods==GEANY_PRIMARY_MOD_MASK && (event->keyval==GDK_KEY_f || event->keyval==GDK_KEY_F)) { gtk_widget_show(h->find_row);gtk_widget_grab_focus(h->find_entry);find_changed(NULL,h);return TRUE; }
 if(event->keyval==GDK_KEY_F3) { find_select(h,(mods&GDK_SHIFT_MASK) ? -1 : 1);return TRUE; }
 if(event->keyval==GDK_KEY_Escape && gtk_widget_get_visible(h->find_row)) { find_close(NULL,h);return TRUE; }return FALSE;
}
static gboolean load_document(Help *h,const gchar *path,gboolean remember);
static gboolean link_clicked(GtkLabel *label,const gchar *uri,gpointer data) {
 (void)label;Help *h=data;if(uri[0]=='#') { const gchar *id=uri+1;for(guint i=0;i<h->headings->len;i++) { GtkWidget *heading=g_ptr_array_index(h->headings,i);if(!g_strcmp0(id,g_object_get_data(G_OBJECT(heading),"insight-anchor"))) { reveal(h,heading);return TRUE; } }gtk_label_set_text(GTK_LABEL(h->status),"Heading anchor not found");return TRUE; }
 gchar *path=g_filename_from_uri(uri,NULL,NULL);
 if(path && (g_str_has_suffix(path,".markdown") || g_str_has_suffix(path,".md") || g_str_has_suffix(path,".rst") || g_str_has_suffix(path,".rest"))) load_document(h,path,TRUE);
 else utils_open_browser(uri);
 g_free(path);return TRUE;
}
static GtkWidget *add_label(Help *h,const gchar *markup,gboolean code) {
 GtkWidget *label=gtk_label_new(NULL);gtk_label_set_markup(GTK_LABEL(label),markup);gtk_label_set_selectable(GTK_LABEL(label),TRUE);gtk_label_set_xalign(GTK_LABEL(label),0);gtk_label_set_line_wrap(GTK_LABEL(label),TRUE);gtk_label_set_line_wrap_mode(GTK_LABEL(label),PANGO_WRAP_WORD_CHAR);gtk_widget_set_hexpand(label,TRUE);gtk_widget_set_halign(label,GTK_ALIGN_FILL);gtk_widget_set_margin_bottom(label,code ? 10 : 5);g_signal_connect(label,"activate-link",G_CALLBACK(link_clicked),h);gtk_box_pack_start(GTK_BOX(h->body),label,FALSE,FALSE,0);return label;
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
  GtkWidget *button=gtk_button_new_with_label(insight_tr("▶ Video — open multimedia view"));gtk_widget_set_tooltip_text(button,insight_tr("Play videos in the browser media view; codec support depends on the browser"));g_signal_connect(button,"clicked",G_CALLBACK(media_view),h);gtk_box_pack_start(GTK_BOX(h->body),button,FALSE,FALSE,0);add_label(h,alt,FALSE);
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
static void remove_history(Help *h,guint index) {const gchar *key=g_ptr_array_index(h->history,index);if(g_str_has_prefix(key,"@answer:")) g_hash_table_remove(h->answers,key);g_ptr_array_remove_index(h->history,index);}
static gboolean load_document(Help *h,const gchar *filename,gboolean remember) {
 gchar *key=g_strdup(filename),*path=NULL,*source=NULL;gsize length=0;GError *error=NULL;GStatBuf st;
 if(g_str_has_prefix(key,"@editor:")) {
  gchar *end=NULL;guint64 id=g_ascii_strtoull(key+8,&end,10);GeanyDocument *doc=id && id<=G_MAXUINT && end && !*end ? document_find_by_id((guint)id) : NULL;
  if(doc && sci_get_length(doc->editor->sci)<=8*1024*1024) { length=sci_get_length(doc->editor->sci);source=sci_get_contents(doc->editor->sci,length+1);path=doc->file_name ? insight_canonicalize_filename(doc->file_name,NULL) : insight_canonicalize_filename("untitled.md",NULL); }
 }else if(g_str_has_prefix(key,"@answer:")) {source=g_strdup(g_hash_table_lookup(h->answers,key));length=source ? strlen(source):0;path=g_build_filename(app->datadir,"insight","docs","ai-answer.md",NULL);
 }else { path=insight_canonicalize_filename(key,NULL);if(!g_stat(path,&st) && st.st_size<=8*1024*1024) g_file_get_contents(path,&source,&length,&error); }
 if(!source || !g_utf8_validate(source,length,NULL) || memchr(source,0,length)) { gtk_label_set_text(GTK_LABEL(h->status),error ? error->message : "Preview unavailable: source closed, invalid UTF-8, or larger than 8 MiB");g_clear_error(&error);g_free(source);g_free(path);g_free(key);return FALSE; }
 if(remember) { while(h->history->len && h->position+1<h->history->len) remove_history(h,h->history->len-1);g_ptr_array_add(h->history,g_strdup((g_str_has_prefix(key,"@editor:") || g_str_has_prefix(key,"@answer:")) ? key : path));if(h->history->len>128) remove_history(h,0);h->position=h->history->len-1; }
 g_free(h->key);h->key=key;clear_find(h);if(h->scroll_idle) { g_source_remove(h->scroll_idle);h->scroll_idle=0; }g_clear_object(&h->scroll_target);h->filling=TRUE;gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(h->toc));g_ptr_array_set_size(h->headings,0);
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
  if(heading) { gchar *large=g_strdup_printf("<span size=\"%s\" weight=\"bold\">%s</span>",heading==1 ? "xx-large" : (heading==2 ? "x-large" : "large"),markup);GtkWidget *label=add_label(h,large,FALSE);gchar *plain=g_utf8_strdown(gtk_label_get_text(GTK_LABEL(label)),-1);GString *slug=g_string_new(NULL);for(const gchar *at=plain;*at;at=g_utf8_next_char(at)) { gunichar c=g_utf8_get_char(at);if(g_unichar_isalnum(c) || c=='_' || c=='-') g_string_append_unichar(slug,c);else if(g_unichar_isspace(c)) g_string_append_c(slug,'-'); }g_free(plain);gboolean unique=FALSE;gchar *anchor=g_strdup(slug->str);for(guint suffix=1;!unique;suffix++) { unique=TRUE;for(guint n=0;n<h->headings->len;n++) if(!g_strcmp0(anchor,g_object_get_data(G_OBJECT(g_ptr_array_index(h->headings,n)),"insight-anchor"))) unique=FALSE;if(!unique) { g_free(anchor);anchor=g_strdup_printf("%s-%u",slug->str,suffix); } }g_string_free(slug,TRUE);g_object_set_data_full(G_OBJECT(label),"insight-anchor",anchor,g_free);if(h->headings->len<512) { g_ptr_array_add(h->headings,label);gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(h->toc),gtk_label_get_text(GTK_LABEL(label))); }gchar *escaped_anchor=g_markup_escape_text(anchor,-1);g_string_append_printf(html,"<h%u id=\"%s\">%s</h%u>",heading,escaped_anchor,web,heading);g_free(escaped_anchor);g_free(large); }
  else if(g_str_has_prefix(text,"- ") || g_str_has_prefix(text,"* ")) { gchar *item=inline_text(text+2,base,FALSE),*item_web=inline_text(text+2,base,TRUE),*bullet=g_strdup_printf("• %s",item);add_label(h,bullet,FALSE);g_string_append_printf(html,"<ul><li>%s</li></ul>",item_web);g_free(item);g_free(item_web);g_free(bullet); }
  else { add_label(h,markup,FALSE);g_string_append_printf(html,"<p>%s</p>\n",web); }
  g_free(markup);g_free(web);
 }
 if(code->len) { gchar *escaped=g_markup_escape_text(code->str,-1),*markup=g_strdup_printf("<tt>%s</tt>",escaped);add_label(h,markup,TRUE);g_string_append_printf(html,"<pre><code>%s</code></pre>",escaped);g_free(escaped);g_free(markup); }
 g_string_free(code,TRUE);g_strfreev(lines);g_string_append(html,"</main>");h->html=g_string_free(html,FALSE);g_object_set_data(G_OBJECT(h->window),"insight-rendered-html",h->html);g_free(base);g_free(source);
 h->filling=FALSE;gtk_widget_show_all(h->body);gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(h->scroll)),0);gtk_widget_set_sensitive(h->back,h->position>0);gtk_widget_set_sensitive(h->forward,h->position+1<h->history->len);gtk_label_set_text(GTK_LABEL(h->status),g_str_has_prefix(h->key,"@editor:") ? "Editor buffer snapshot — Reload reads unsaved text; relative media uses the file folder (working directory for untitled)" : path);if(gtk_widget_get_visible(h->find_row)) find_changed(NULL,h);g_free(path);return TRUE;
}
static void history(GtkWidget *widget,gpointer data) { (void)widget;Help *h=help;if(!h) return;gint next=(gint)h->position+GPOINTER_TO_INT(data);if(next<0 || next>=(gint)h->history->len) return;guint previous=h->position;h->position=(guint)next;if(!load_document(h,g_ptr_array_index(h->history,h->position),FALSE)) h->position=previous; }
static void refresh(GtkWidget *widget,gpointer data) { (void)widget;Help *h=data;if(h->key) load_document(h,h->key,FALSE); }
static void disconnect_help(GtkWidget *widget,gpointer data) { g_signal_handlers_disconnect_by_data(widget,data);if(GTK_IS_CONTAINER(widget)) gtk_container_foreach(GTK_CONTAINER(widget),disconnect_help,data); }
static void destroy(GtkWidget *widget,gpointer data) { Help *h=data;disconnect_help(widget,h);if(h->scroll_idle) g_source_remove(h->scroll_idle);g_clear_object(&h->scroll_target);clear_find(h);g_ptr_array_free(h->matches,TRUE);g_ptr_array_free(h->headings,TRUE);g_free(h->key);g_ptr_array_free(h->history,TRUE);/* Media pages may still be open in the browser, so preserve them for this session. */g_ptr_array_free(h->temporary,TRUE);g_free(h->path);g_free(h->html);g_hash_table_unref(h->answers);help=NULL;g_free(h); }
typedef struct { GString *all,*selected; } AiHelpText;
static void help_ai_collect(GtkWidget *widget,gpointer data) {
 AiHelpText *text=data;if(GTK_IS_LABEL(widget)) { const gchar *value=gtk_label_get_text(GTK_LABEL(widget));gint a=0,b=0;if(gtk_label_get_selection_bounds(GTK_LABEL(widget),&a,&b)) { const gchar *first=g_utf8_offset_to_pointer(value,MIN(a,b)),*last=g_utf8_offset_to_pointer(value,MAX(a,b));if(text->selected->len+(gsize)(last-first)+1<=65536) { g_string_append_len(text->selected,first,last-first);g_string_append_c(text->selected,'\n'); } }if(text->all->len+strlen(value)+1<=65536) { g_string_append(text->all,value);g_string_append_c(text->all,'\n'); } }else if(GTK_IS_CONTAINER(widget)) gtk_container_foreach(GTK_CONTAINER(widget),help_ai_collect,data);
}
static void help_ai(GtkWidget *widget,gpointer data) { (void)widget;Help *h=data;AiHelpText text={g_string_new(NULL),g_string_new(NULL)};help_ai_collect(h->body,&text);insight_ai_lookup_for("program-help","Program help — review captured text",text.selected->len ? text.selected->str:text.all->str,"Explain this help selection or page and how to use the described editor feature. Answer based on the supplied documentation.");g_string_free(text.all,TRUE);g_string_free(text.selected,TRUE); }
static void help_prompt_send(GtkWidget *widget,gpointer data) {
 (void)widget;Help *h=data;const gchar *question=gtk_entry_get_text(GTK_ENTRY(h->ai_prompt));if(!*question) return;
 AiHelpText text={g_string_new(NULL),g_string_new(NULL)};help_ai_collect(h->body,&text);insight_ai_help_question(text.selected->len ? text.selected->str:text.all->str,question);g_string_free(text.all,TRUE);g_string_free(text.selected,TRUE);
}
static Help *ensure_help(void) {
 if(help) return help;
 Help *h=g_new0(Help,1);help=h;h->answers=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);h->match=-1;h->matches=g_ptr_array_new_with_free_func(free_match);h->headings=g_ptr_array_new();h->history=g_ptr_array_new_with_free_func(g_free);h->temporary=g_ptr_array_new_with_free_func(g_free);h->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(h->window,"insight-help-viewer");gtk_window_set_default_size(GTK_WINDOW(h->window),900,700);gtk_window_set_transient_for(GTK_WINDOW(h->window),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(h->window),TRUE);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6),*bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,5);gtk_container_add(GTK_CONTAINER(h->window),box);insight_panel_help_attach(box,"markup-viewer.rst");gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
 const gchar *icons[]={"go-previous","go-next","folder-open","view-refresh","media"};const gchar *tips[]={"Previous page","Next page","Open Markdown / reStructuredText…","Reload document","Multimedia view — render in browser, including video"};
 for(guint i=0;i<5;i++) { GtkWidget *button=gtk_button_new();gtk_button_set_image(GTK_BUTTON(button),insight_command_icon(icons[i]));gtk_widget_set_tooltip_text(button,tips[i]);if(i==3) gtk_widget_set_name(button,"insight-help-reload");gtk_box_pack_start(GTK_BOX(bar),button,FALSE,FALSE,0);if(i<2) { if(i) h->forward=button;else h->back=button;g_signal_connect(button,"clicked",G_CALLBACK(history),GINT_TO_POINTER(i ? 1 : -1)); }else g_signal_connect(button,"clicked",i==2 ? G_CALLBACK(insight_markup_open) : (i==3 ? G_CALLBACK(refresh) : G_CALLBACK(media_view)),h); }
 GtkWidget *ai=gtk_button_new_with_label(insight_tr("Ask AI…"));gtk_widget_set_name(ai,"insight-help-ai");g_signal_connect(ai,"clicked",G_CALLBACK(help_ai),h);gtk_box_pack_start(GTK_BOX(bar),ai,FALSE,FALSE,0);
 GtkWidget *ai_fold=gtk_expander_new(insight_tr("Ask AI…")),*ai_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_widget_set_name(ai_fold,"insight-help-ai-prompt-fold");h->ai_prompt=gtk_entry_new();gtk_widget_set_name(h->ai_prompt,"insight-help-ai-prompt");gtk_widget_set_hexpand(h->ai_prompt,TRUE);gtk_widget_set_tooltip_text(h->ai_prompt,insight_tr("Question about this help page; the answer opens here as Markdown"));gtk_box_pack_start(GTK_BOX(ai_row),h->ai_prompt,TRUE,TRUE,0);GtkWidget *ask=gtk_button_new_with_label(insight_tr("Send"));gtk_box_pack_start(GTK_BOX(ai_row),ask,FALSE,FALSE,0);g_signal_connect(ask,"clicked",G_CALLBACK(help_prompt_send),h);g_signal_connect(h->ai_prompt,"activate",G_CALLBACK(help_prompt_send),h);gtk_container_add(GTK_CONTAINER(ai_fold),ai_row);gtk_box_pack_start(GTK_BOX(box),ai_fold,FALSE,FALSE,0);gtk_expander_set_expanded(GTK_EXPANDER(ai_fold),FALSE);
 h->toc=gtk_combo_box_text_new();gtk_widget_set_name(h->toc,"insight-help-toc");g_object_set_data(G_OBJECT(h->toc),"insight-no-i18n",GINT_TO_POINTER(1));insight_i18n_key(G_OBJECT(h->toc),"tooltip-text","Page headings — jump to a section");gtk_widget_set_hexpand(h->toc,TRUE);gtk_box_pack_start(GTK_BOX(bar),h->toc,TRUE,TRUE,0);g_signal_connect(h->toc,"changed",G_CALLBACK(toc_changed),h);
 h->find_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,5);gtk_widget_set_name(h->find_row,"insight-help-find-row");h->find_entry=gtk_search_entry_new();gtk_widget_set_name(h->find_entry,"insight-help-find");gtk_entry_set_placeholder_text(GTK_ENTRY(h->find_entry),"Find on this page (Ctrl+F)…");gtk_entry_set_max_length(GTK_ENTRY(h->find_entry),256);gtk_widget_set_hexpand(h->find_entry,TRUE);gtk_box_pack_start(GTK_BOX(h->find_row),h->find_entry,TRUE,TRUE,0);h->find_status=gtk_label_new("");gtk_widget_set_name(h->find_status,"insight-help-find-status");gtk_box_pack_start(GTK_BOX(h->find_row),h->find_status,FALSE,FALSE,0);
 for(guint i=0;i<3;i++) { GtkWidget *button=gtk_button_new_from_icon_name(i==0 ? "go-previous" : (i==1 ? "go-next" : "window-close"),GTK_ICON_SIZE_BUTTON);gtk_widget_set_tooltip_text(button,i==0 ? "Previous match (Shift+F3)" : (i==1 ? "Next match (F3 / Enter)" : "Close find (Escape)"));gtk_box_pack_start(GTK_BOX(h->find_row),button,FALSE,FALSE,0);g_signal_connect(button,"clicked",i==2 ? G_CALLBACK(find_close) : G_CALLBACK(find_step),i==2 ? (gpointer)h : GINT_TO_POINTER(i==0 ? -1 : 1)); }
 gtk_box_pack_start(GTK_BOX(box),h->find_row,FALSE,FALSE,0);g_signal_connect(h->find_entry,"changed",G_CALLBACK(find_changed),h);g_signal_connect(h->find_entry,"activate",G_CALLBACK(find_step),GINT_TO_POINTER(1));g_signal_connect(h->window,"key-press-event",G_CALLBACK(help_key),h);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);h->scroll=scroll;h->body=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);g_object_set_data(G_OBJECT(h->body),"insight-no-i18n",GINT_TO_POINTER(1));gtk_widget_set_margin_start(h->body,20);gtk_widget_set_margin_end(h->body,20);gtk_widget_set_margin_top(h->body,16);gtk_container_add(GTK_CONTAINER(scroll),h->body);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);h->status=gtk_label_new("");gtk_label_set_ellipsize(GTK_LABEL(h->status),PANGO_ELLIPSIZE_MIDDLE);gtk_box_pack_start(GTK_BOX(box),h->status,FALSE,FALSE,3);g_signal_connect(h->window,"destroy",G_CALLBACK(destroy),h);gtk_widget_show_all(h->window);gtk_widget_set_no_show_all(h->find_row,TRUE);gtk_widget_hide(h->find_row);return h;
}
GEANY_API_SYMBOL
void insight_help_answer(const gchar *question,const gchar *markdown) {
 if(!markdown || !g_utf8_validate(markdown,-1,NULL) || strlen(markdown)>262144) return;
 Help *h=ensure_help();gchar *key=g_strdup_printf("@answer:%u",++h->answer_serial);gchar *title=g_strdup(question ? question:insight_tr("AI answer"));for(gchar *p=title;*p;p++) if(*p=='\n' || *p=='\r') *p=' ';
 g_hash_table_insert(h->answers,g_strdup(key),g_strdup_printf("# %s\n\n%s",title,markdown));g_free(title);load_document(h,key,TRUE);g_free(key);gtk_window_present(GTK_WINDOW(h->window));
}
GEANY_API_SYMBOL
void insight_markup_open_path(const gchar *path) {
 Help *h=ensure_help();load_document(h,path,TRUE);gtk_window_present(GTK_WINDOW(h->window));
}
GEANY_API_SYMBOL
void insight_markup_preview(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;GeanyDocument *doc=document_get_current();if(!doc) return;gchar *key=g_strdup_printf("@editor:%u",doc->id);insight_markup_open_path(key);g_free(key);
}
GtkWidget *insight_panel_help_button(const gchar *topic) {
 GtkWidget *button=gtk_button_new(); gtk_widget_set_name(button,"insight-panel-help"); gtk_button_set_image(GTK_BUTTON(button),insight_command_icon("panel-help")); gtk_button_set_relief(GTK_BUTTON(button),GTK_RELIEF_NONE); gtk_widget_set_tooltip_text(button,"Help for this panel — controls and tutorial"); gtk_widget_set_size_request(button,22,22); g_object_set_data_full(G_OBJECT(button),"insight-help-topic",g_strdup(topic),g_free); g_signal_connect(button,"clicked",G_CALLBACK(insight_help_show),g_object_get_data(G_OBJECT(button),"insight-help-topic")); return button;
}
void insight_panel_help_attach(GtkWidget *panel,const gchar *topic) {
 if(!GTK_IS_BOX(panel) || g_object_get_data(G_OBJECT(panel),"insight-panel-help-attached")) return;
 GtkWidget *header=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0),*button=insight_panel_help_button(topic); gtk_widget_set_name(header,"insight-panel-help-header"); gtk_box_pack_end(GTK_BOX(header),button,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(panel),header,FALSE,FALSE,0); gtk_box_reorder_child(GTK_BOX(panel),header,0); g_object_set_data_full(G_OBJECT(panel),"insight-help-topic",g_strdup(topic),g_free); g_object_set_data(G_OBJECT(panel),"insight-panel-help-attached",GINT_TO_POINTER(1)); gtk_widget_show_all(header);
}
static const gchar *native_panel_topic(GtkNotebook *book,GtkWidget *page) {
 const gchar *explicit_topic=g_object_get_data(G_OBJECT(page),"insight-help-topic"); if(explicit_topic) return explicit_topic;
 const gchar *fallback=g_object_get_data(G_OBJECT(book),"insight-panel-help-default"); if(!g_strcmp0(fallback,"panel-editor.md")) return fallback;
 const gchar *label=gtk_notebook_get_tab_label_text(book,page); label=label ? insight_i18n_english(label) : "";
 if(!strcmp(label,"Symbols")) return "panel-native-symbols.md";
 if(!strcmp(label,"Documents")) return "panel-documents.md";
 if(!strcmp(label,"Status")) return "panel-status.md";
 if(!strcmp(label,"Compiler")) return "panel-compiler.md";
 if(!strcmp(label,"Messages")) return "panel-messages.md";
 if(!strcmp(label,"Scribble")) return "panel-scratchpad.md";
 if(!strcmp(label,"Terminal")) return "panel-terminal.md";
 return g_object_get_data(G_OBJECT(book),"insight-panel-help-default");
}
static void notebook_help_click(GtkWidget *widget,gpointer data) {
 (void)widget; GtkNotebook *book=GTK_NOTEBOOK(data); GtkWidget *page=gtk_notebook_get_nth_page(book,gtk_notebook_get_current_page(book)); if(page) insight_help_show(NULL,(gpointer)native_panel_topic(book,page));
}
static void notebook_help_page(GtkNotebook *book,GtkWidget *page,guint number,gpointer data) { (void)book; (void)number; gtk_widget_set_visible(data,!g_object_get_data(G_OBJECT(page),"insight-panel-help-attached")); }
void insight_panel_help_notebook(GtkNotebook *book,const gchar *fallback) {
 if(g_object_get_data(G_OBJECT(book),"insight-panel-help-default")) return;
 g_object_set_data_full(G_OBJECT(book),"insight-panel-help-default",g_strdup(fallback),g_free); GtkWidget *button=gtk_button_new(); gtk_widget_set_name(button,"insight-notebook-help"); gtk_button_set_image(GTK_BUTTON(button),insight_command_icon("panel-help")); gtk_button_set_relief(GTK_BUTTON(button),GTK_RELIEF_NONE); gtk_widget_set_tooltip_text(button,"Help for this panel — controls and tutorial"); gtk_widget_set_no_show_all(button,TRUE); gtk_widget_show(gtk_button_get_image(GTK_BUTTON(button))); GtkWidget *existing=gtk_notebook_get_action_widget(book,GTK_PACK_END);if(existing) {g_object_ref(existing);gtk_notebook_set_action_widget(book,NULL,GTK_PACK_END);GtkWidget *actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);gtk_box_pack_start(GTK_BOX(actions),existing,FALSE,FALSE,0);g_object_unref(existing);gtk_box_pack_start(GTK_BOX(actions),button,FALSE,FALSE,0);gtk_notebook_set_action_widget(book,actions,GTK_PACK_END);gtk_widget_show(actions);}else gtk_notebook_set_action_widget(book,button,GTK_PACK_END); g_signal_connect_object(button,"clicked",G_CALLBACK(notebook_help_click),book,0); g_signal_connect_object(book,"switch-page",G_CALLBACK(notebook_help_page),button,0); GtkWidget *page=gtk_notebook_get_nth_page(book,gtk_notebook_get_current_page(book)); if(page) notebook_help_page(book,page,0,button);
}
void insight_help_show(GtkWidget *widget,gpointer topic) {
 (void)widget;const gchar *name=topic ? topic : "guide.md";gchar *path=g_build_filename(app->datadir,"insight","docs",insight_i18n_language(),name,NULL);
 if(!g_file_test(path,G_FILE_TEST_IS_REGULAR)) { g_free(path);path=g_build_filename(app->datadir,"insight","docs",name,NULL); }
 insight_markup_open_path(path);g_free(path);
}
void insight_markup_open(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;GtkWidget *dialog=gtk_file_chooser_dialog_new("Open Markdown / reStructuredText",GTK_WINDOW(main_widgets.window),GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);GtkFileFilter *filter=gtk_file_filter_new();gtk_file_filter_set_name(filter,"Markdown / reStructuredText (*.md, *.rst, *.rest)");gtk_file_filter_add_pattern(filter,"*.md");gtk_file_filter_add_pattern(filter,"*.markdown");gtk_file_filter_add_pattern(filter,"*.rst");gtk_file_filter_add_pattern(filter,"*.rest");gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);GtkFileFilter *all=gtk_file_filter_new();gtk_file_filter_set_name(all,"All files");gtk_file_filter_add_pattern(all,"*");gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),all);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));insight_markup_open_path(path);g_free(path); }gtk_widget_destroy(dialog);
}

static void localize_media(GtkWidget *widget,gpointer data) { (void)data;if(GTK_IS_BUTTON(widget)) { const gchar *label=gtk_button_get_label(GTK_BUTTON(widget));if(label && (!strcmp(label,"▶ Video — open multimedia view") || !strcmp(label,"▶ Видео — открыть мультимедийный просмотр"))) { gtk_button_set_label(GTK_BUTTON(widget),insight_tr("▶ Video — open multimedia view"));gtk_widget_set_tooltip_text(widget,insight_tr("Play videos in the browser media view; codec support depends on the browser")); } }if(GTK_IS_CONTAINER(widget)) gtk_container_foreach(GTK_CONTAINER(widget),localize_media,NULL); }
void insight_help_locale_changed(void) { if(!help || !help->path) return;localize_media(help->body,NULL);gchar *base=g_build_filename(app->datadir,"insight","docs",NULL);if(g_str_has_prefix(help->path,base) && help->key && !g_str_has_prefix(help->key,"@editor:") && !g_str_has_prefix(help->key,"@answer:")) { gchar *topic=g_path_get_basename(help->path);insight_help_show(NULL,topic);g_free(topic); }g_free(base); }
