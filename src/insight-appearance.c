/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-appearance.h"
#include "insight-ai.h"
#include "insight-i18n.h"
#include "insight-workbench.h"
#include "ui_utils.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <math.h>
typedef struct { GWeakRef widget; } Surface;
static gboolean alive,macos=TRUE,reduced,syncing,independent;
static GtkCssProvider *provider;
static GPtrArray *surfaces;
static GtkWidget *choices[2][4],*opacity_choices[2][4];
static guint opacity=100;
static gboolean fading;static gdouble fade_from;static gint64 fade_started;
static guint menu_count,map_signal;
static gulong map_hook;
static gchar *config;
/* GTK measures and positions tooltips before mapping their private window.
 * Replacing its font/visual/icons from a global map hook invalidates that
 * measurement and causes a second resize/repaint under the pointer. */
static gboolean tooltip_surface(GtkWidget *widget) {
 GtkWidget *top=gtk_widget_get_toplevel(widget);
 return GTK_IS_WINDOW(top) && (gtk_window_get_type_hint(GTK_WINDOW(top))==GDK_WINDOW_TYPE_HINT_TOOLTIP || !strcmp(G_OBJECT_TYPE_NAME(top),"GtkTooltipWindow"));
}
static void surface_free(gpointer data) { Surface *s=data;g_weak_ref_clear(&s->widget);g_free(s); }
typedef struct { gchar *name,*replacement;GtkIconSize size;gint pixels;gboolean stock; } OriginalIcon;
static void icon_free(gpointer data) { OriginalIcon *icon=data;g_free(icon->name);g_free(icon->replacement);g_free(icon); }
static void style_icons(GtkWidget *widget) {
 if(GTK_IS_IMAGE(widget)) {
  GtkImage *image=GTK_IMAGE(widget);OriginalIcon *old=g_object_get_data(G_OBJECT(image),"insight-original-icon");
  GtkImageType type=gtk_image_get_storage_type(image);const gchar *name=NULL;GtkIconSize size=GTK_ICON_SIZE_MENU;
  if(type==GTK_IMAGE_ICON_NAME) gtk_image_get_icon_name(image,&name,&size);
  if(old && (!macos || g_strcmp0(name,old->replacement))) {
   if(!g_strcmp0(name,old->replacement)) { if(old->stock) gtk_image_set_from_stock(image,old->name,old->size);else gtk_image_set_from_icon_name(image,old->name,old->size);gtk_image_set_pixel_size(image,old->pixels); }
   g_object_set_data(G_OBJECT(image),"insight-original-icon",NULL);old=NULL;
  }
  if(macos && !old) {
   gboolean stock=type==GTK_IMAGE_STOCK;
   if(stock) gtk_image_get_stock(image,(gchar **)&name,&size);
   if(name && !g_str_has_suffix(name,"-symbolic")) {
    const gchar *base=name;
    const gchar *stocks[][2]={{"gtk-new","document-new"},{"gtk-open","document-open"},{"gtk-save","document-save"},{"gtk-save-as","document-save-as"},{"gtk-close","window-close"},{"gtk-quit","application-exit"},{"gtk-undo","edit-undo"},{"gtk-redo","edit-redo"},{"gtk-cut","edit-cut"},{"gtk-copy","edit-copy"},{"gtk-paste","edit-paste"},{"gtk-find","edit-find"},{"gtk-print","document-print"},{"gtk-refresh","view-refresh"},{"gtk-delete","edit-delete"}};
    for(guint i=0;i<G_N_ELEMENTS(stocks);i++) if(!strcmp(name,stocks[i][0])) { base=stocks[i][1];break; }
    gchar *symbolic=g_strconcat(base,"-symbolic",NULL);
    if(gtk_icon_theme_has_icon(gtk_icon_theme_get_default(),symbolic)) {
     old=g_new0(OriginalIcon,1);old->name=g_strdup(name);old->replacement=symbolic;old->size=size;old->stock=stock;old->pixels=gtk_image_get_pixel_size(image);
     g_object_set_data_full(G_OBJECT(image),"insight-original-icon",old,icon_free);gtk_image_set_from_icon_name(image,symbolic,GTK_ICON_SIZE_MENU);gtk_image_set_pixel_size(image,16);
    } else g_free(symbolic);
   }
  }
 }
 if(GTK_IS_MENU_ITEM(widget)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget));if(menu) style_icons(menu); }
 if(GTK_IS_CONTAINER(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget));for(GList *p=children;p;p=p->next) style_icons(p->data);g_list_free(children); }
}
static gdouble window_opacity(GtkWidget *widget) {
 if(fading) {gdouble t=CLAMP((g_get_monotonic_time()-fade_started)/400000.0,0.0,1.0);return fade_from+(1.0-fade_from)*t;}
 if(independent || g_object_get_data(G_OBJECT(widget),"insight-independent-opacity") || opacity!=100) return opacity/100.0;
 guint local=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget),"insight-local-window-opacity"));return local ? local/100.0:1.0;
}
static void apply_native_opacity(GtkWidget *widget) {
 if(GTK_IS_WINDOW(widget) && !tooltip_surface(widget)) {gdouble value=window_opacity(widget);if(fabs(gtk_widget_get_opacity(widget)-value)>.0001) gtk_widget_set_opacity(widget,value);}
}
gboolean insight_appearance_panel_opacity_available(void) { return opacity==100 && !independent && !fading; }
void insight_appearance_panel_opacity(GtkWidget *window,gint percent) {
 if(!GTK_IS_WINDOW(window)) return;g_object_set_data(G_OBJECT(window),"insight-local-window-opacity",GUINT_TO_POINTER(CLAMP(percent,1,100)));apply_native_opacity(window);
}
gboolean insight_appearance_opacity_tick(gboolean finish) {
 if(!fading) return FALSE;if(finish || g_get_monotonic_time()-fade_started>=400000) fading=FALSE;
 for(guint n=0;surfaces && n<surfaces->len;n++) {Surface *surface=g_ptr_array_index(surfaces,n);GtkWidget *widget=g_weak_ref_get(&surface->widget);if(widget) {apply_native_opacity(widget);g_object_unref(widget);}}return fading;
}
static void apply_surface(GtkWidget *widget) {
 if(tooltip_surface(widget)) return;
 apply_native_opacity(widget);
 /* Opaque editor windows retain their native visual and focus behavior.
  * Only composited popups need alpha for their rounded material and shadow. */
 if(GTK_IS_WINDOW(widget) && gtk_window_get_window_type(GTK_WINDOW(widget))==GTK_WINDOW_POPUP &&
    gdk_screen_is_composited(gtk_widget_get_screen(widget)) && !gtk_widget_get_realized(widget)) {
  GdkVisual *visual=gdk_screen_get_rgba_visual(gtk_widget_get_screen(widget));if(visual) gtk_widget_set_visual(widget,visual);
 }
 GtkStyleContext *style=gtk_widget_get_style_context(widget);
 if(macos) gtk_style_context_add_class(style,"insight-macos");else gtk_style_context_remove_class(style,"insight-macos");
 if(macos && reduced) gtk_style_context_add_class(style,"insight-solid");else gtk_style_context_remove_class(style,"insight-solid");
}
static void remember(GtkWidget *widget) {
 if(tooltip_surface(widget)) return;
 if(GTK_IS_IMAGE(widget)) { style_icons(widget);return; }
 if(!GTK_IS_WINDOW(widget) && !GTK_IS_MENU(widget)) return;
 gboolean found=FALSE;
 for(guint i=0;i<surfaces->len;) { Surface *s=g_ptr_array_index(surfaces,i);GtkWidget *old=g_weak_ref_get(&s->widget);if(!old) { g_ptr_array_remove_index(surfaces,i);continue; }if(old==widget) found=TRUE;g_object_unref(old);i++; }
 if(!found) { Surface *s=g_new0(Surface,1);g_weak_ref_init(&s->widget,widget);g_ptr_array_add(surfaces,s); }
 apply_surface(widget);style_icons(widget);
 if(GTK_IS_MENU(widget)) { GtkWidget *popup=gtk_widget_get_toplevel(widget);if(GTK_IS_WINDOW(popup)) apply_surface(popup); }
}
static gboolean mapped(GSignalInvocationHint *hint,guint count,const GValue *values,gpointer data) {
 (void)hint;(void)data;if(alive && count) remember(g_value_get_object(&values[0]));return TRUE;
}
static void sync_menus(void) {
 syncing=TRUE;for(guint i=0;i<menu_count;i++) {gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(choices[i][independent ? 3 : macos ? 1:0]),TRUE);for(guint n=0;n<4;n++) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(opacity_choices[i][n]),opacity==100-10*n);}syncing=FALSE;
}
static void apply(void) {
 GList *windows=gtk_window_list_toplevels();for(GList *p=windows;p;p=p->next) remember(p->data);g_list_free(windows);
 for(guint i=0;i<surfaces->len;i++) { Surface *s=g_ptr_array_index(surfaces,i);GtkWidget *w=g_weak_ref_get(&s->widget);if(w) { apply_surface(w);style_icons(w);g_object_unref(w); } }
 sync_menus();
 insight_workbench_set_independent(independent);
 insight_ai_window_modes();
}
static gboolean load_css(GError **error) {
 if(provider) return TRUE;
 gchar *path=g_build_filename(app->datadir,"styles","appearance-macos.css",NULL),*text=NULL;gsize size=0;GStatBuf info;gboolean ok=!g_stat(path,&info) && info.st_size>=0 && info.st_size<=64*1024;
 if(!ok) g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"macOS-style CSS is missing or too large");else ok=g_file_get_contents(path,&text,&size,error);g_free(path);
 if(ok && (memchr(text,0,size) || !g_utf8_validate(text,size,NULL))) { g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Invalid macOS-style CSS");ok=FALSE; }
 GtkCssProvider *next=NULL;if(ok) { next=gtk_css_provider_new();ok=gtk_css_provider_load_from_data(next,text,size,error); }g_free(text);
 if(!ok) { g_clear_object(&next);return FALSE; }provider=next;gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(provider),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);return TRUE;
}
static gboolean save(gboolean next_mac,guint next_opacity,gboolean next_independent,GError **error) {
 GKeyFile *key=g_key_file_new();g_key_file_set_integer(key,"interface","version",1);g_key_file_set_string(key,"interface","style",next_independent ? "macos-windows":next_mac ? "macos":"classic");g_key_file_set_boolean(key,"interface","reduce-transparency",next_opacity==100);g_key_file_set_integer(key,"interface","opacity",next_opacity);gsize size;gchar *text=g_key_file_to_data(key,&size,NULL),*dir=g_path_get_dirname(config);gboolean ok=FALSE;
 if(g_mkdir_with_parents(dir,0700)) g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_FAILED,"Cannot create interface style settings directory");else { GFile *file=g_file_new_for_path(config);ok=g_file_replace_contents(file,text,size,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,error);g_object_unref(file); }g_free(dir);g_free(text);g_key_file_unref(key);return ok;
}
static void selected(GtkCheckMenuItem *item,gpointer data) {
 if(syncing || !alive || !gtk_check_menu_item_get_active(item)) return;guint action=GPOINTER_TO_UINT(data);gboolean next_independent=action==3,next_mac=action==1 || action==3;guint next_opacity=next_independent && !independent ? 100:opacity;GError *error=NULL;
 if((next_mac && !load_css(&error)) || !save(next_mac,next_opacity,next_independent,&error)) {ui_set_statusbar(TRUE,"%s",error ? error->message:insight_tr("Cannot save interface style"));g_clear_error(&error);sync_menus();return;}
 if(next_independent && !independent && opacity<100) {fading=TRUE;fade_from=opacity/100.0;fade_started=g_get_monotonic_time();}
 macos=next_mac;opacity=next_opacity;reduced=opacity==100;independent=next_independent;apply();
}
static void opacity_selected(GtkCheckMenuItem *item,gpointer data) {
 if(syncing || !alive || !gtk_check_menu_item_get_active(item)) return;guint next=GPOINTER_TO_UINT(data);GError *error=NULL;
 if(!save(macos,next,independent,&error)) {ui_set_statusbar(TRUE,"%s",error ? error->message:insight_tr("Cannot save interface style"));g_clear_error(&error);sync_menus();return;}opacity=next;reduced=opacity==100;fading=FALSE;apply();
}
void insight_appearance_init(void) {
 if(alive) return;alive=TRUE;macos=TRUE;opacity=100;reduced=TRUE;fading=FALSE;surfaces=g_ptr_array_new_with_free_func(surface_free);config=g_build_filename(app->configdir,"insight","interface-style.conf",NULL);
 GKeyFile *key=g_key_file_new();GStatBuf info;
 if(!g_stat(config,&info) && info.st_size<=8192 && g_key_file_load_from_file(key,config,G_KEY_FILE_NONE,NULL) && g_key_file_get_integer(key,"interface","version",NULL)==1) { gchar *name=g_key_file_get_string(key,"interface","style",NULL);independent=!g_strcmp0(name,"macos-windows");macos=independent || !g_strcmp0(name,"macos");gint saved=g_key_file_get_integer(key,"interface","opacity",NULL);if(saved==70 || saved==80 || saved==90 || saved==100) opacity=(guint)saved;else opacity=100;if(independent) opacity=100;reduced=opacity==100;g_free(name); }
 g_key_file_unref(key);if(macos) { GError *error=NULL;if(!load_css(&error)) { macos=independent=FALSE;g_warning("Interface style ignored: %s",error ? error->message:"CSS unavailable");g_clear_error(&error); } }
 map_signal=g_signal_lookup("map",GTK_TYPE_WIDGET);map_hook=g_signal_add_emission_hook(map_signal,0,mapped,NULL,NULL);apply();
}
void insight_appearance_menu(GtkWidget *menu) {
 if(!alive) insight_appearance_init();if(menu_count>=2) return;guint index=menu_count++;
 GtkWidget *root=gtk_menu_item_new_with_label(insight_tr("Interface style")),*sub=gtk_menu_new();gchar *root_name=g_strdup_printf("insight-appearance-menu-%u",index);gtk_widget_set_name(root,root_name);g_free(root_name);gtk_menu_item_set_submenu(GTK_MENU_ITEM(root),sub);gtk_menu_shell_append(GTK_MENU_SHELL(menu),root);GSList *group=NULL;const gchar *labels[]={"Classic","macOS style"};
 for(guint i=0;i<2;i++) { choices[index][i]=gtk_radio_menu_item_new_with_label(group,insight_tr(labels[i]));group=gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(choices[index][i]));gtk_menu_shell_append(GTK_MENU_SHELL(sub),choices[index][i]); }
 choices[index][3]=gtk_radio_menu_item_new_with_label(group,insight_tr("macOS — Independent Windows"));gtk_menu_shell_append(GTK_MENU_SHELL(sub),choices[index][3]);
 gtk_menu_shell_append(GTK_MENU_SHELL(sub),gtk_separator_menu_item_new());GSList *levels=NULL;
 for(guint n=0;n<4;n++) {guint percent=100-10*n;gchar *label=insight_i18n_printf("Opacity %u%%",percent);GtkWidget *item=gtk_radio_menu_item_new_with_label(levels,label);g_free(label);levels=gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));opacity_choices[index][n]=item;gchar *name=g_strdup_printf("insight-opacity-%u-%u",index,percent);gtk_widget_set_name(item,name);g_free(name);gtk_menu_shell_append(GTK_MENU_SHELL(sub),item);g_signal_connect(item,"toggled",G_CALLBACK(opacity_selected),GUINT_TO_POINTER(percent));}
 for(guint i=0;i<4;i++) if(i!=2) {gchar *name=g_strdup_printf("insight-appearance-%u-%u",index,i);gtk_widget_set_name(choices[index][i],name);g_free(name);g_signal_connect(choices[index][i],"toggled",G_CALLBACK(selected),GUINT_TO_POINTER(i));}sync_menus();gtk_widget_show_all(root);
}
void insight_appearance_finalize(void) {
 if(!alive) return;alive=FALSE;if(map_hook) g_signal_remove_emission_hook(map_signal,map_hook);map_hook=0;
 macos=FALSE;opacity=100;fading=FALSE;for(guint i=0;i<surfaces->len;i++) { Surface *s=g_ptr_array_index(surfaces,i);GtkWidget *w=g_weak_ref_get(&s->widget);if(w) { apply_surface(w);style_icons(w);g_object_unref(w); } }
 if(provider) gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(provider));g_clear_object(&provider);g_clear_pointer(&surfaces,g_ptr_array_unref);g_clear_pointer(&config,g_free);menu_count=0;memset(choices,0,sizeof choices);memset(opacity_choices,0,sizeof opacity_choices);reduced=independent=FALSE;
}

gboolean insight_appearance_independent(void) { return independent; }
void insight_appearance_join_windows(void) {
 if(!independent) return;
 GError *error=NULL;
 if(!save(macos,opacity,FALSE,&error)) { ui_set_statusbar(TRUE,"%s",error->message);g_clear_error(&error);return; }
 independent=FALSE;apply();
}
static void window_title_changed(GObject *window,GParamSpec *spec,gpointer data) {
 (void)spec;GtkHeaderBar *header=GTK_HEADER_BAR(data);const gchar *title=gtk_window_get_title(GTK_WINDOW(window));
 if(g_strcmp0(title,gtk_header_bar_get_title(header))) gtk_header_bar_set_title(header,title);
}
void insight_appearance_decorate(GtkWidget *window) {
 if(!macos || !GTK_IS_WINDOW(window) || gtk_widget_get_realized(window) || gtk_window_get_titlebar(GTK_WINDOW(window))) return;
 GtkWidget *header=gtk_header_bar_new();gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header),TRUE);
 gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(header),"close,minimize,maximize:");
 /* GTK also synchronizes its custom titlebar. A second GBinding can keep
  * queuing equal title notifications during set_titlebar(), starving the UI.
  * Translate the owning window once, not GTK's internal header labels. */
 g_object_set_data(G_OBJECT(header),"insight-no-i18n",GINT_TO_POINTER(1));
 gtk_header_bar_set_title(GTK_HEADER_BAR(header),gtk_window_get_title(GTK_WINDOW(window)));
 gtk_window_set_titlebar(GTK_WINDOW(window),header);
 g_signal_connect_object(window,"notify::title",G_CALLBACK(window_title_changed),header,0);
 gtk_widget_show(header);
}
