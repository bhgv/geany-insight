/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "insight-list-style.h"
#include "insight-i18n.h"
#include <string.h>
typedef struct { GtkWidget *view;GtkCssProvider *provider;gchar *font,*foreground,*background;gboolean enabled,alive; } ListStyle;
static const gchar *key="insight-list-style";
static gboolean valid(const gchar *font,const gchar *foreground,const gchar *background) {
 if(!font || !*font || strlen(font)>1024 || !g_utf8_validate(font,-1,NULL) || strpbrk(font,"\r\n") || !foreground || strlen(foreground)>128 || !background || strlen(background)>128) return FALSE;
 GdkRGBA color;PangoFontDescription *description=pango_font_description_from_string(font);gint size=pango_font_description_get_size(description);
 gboolean ok=pango_font_description_get_family(description) && size>=4*PANGO_SCALE && size<=96*PANGO_SCALE && gdk_rgba_parse(&color,foreground) && gdk_rgba_parse(&color,background);pango_font_description_free(description);return ok;
}
static gboolean apply(ListStyle *style,gboolean enabled,const gchar *font,const gchar *foreground,const gchar *background) {
 if(!valid(font,foreground,background)) return FALSE;
 GtkCssProvider *provider=NULL;
 if(enabled) {
  GdkRGBA fg,bg;gdk_rgba_parse(&fg,foreground);gdk_rgba_parse(&bg,background);gchar *fg_text=gdk_rgba_to_string(&fg),*bg_text=gdk_rgba_to_string(&bg);
  gchar *css=g_strdup_printf("treeview.view:not(:selected) { color: %s; background-color: %s; }",fg_text,bg_text);
  provider=gtk_css_provider_new();GError *error=NULL;gboolean loaded=gtk_css_provider_load_from_data(provider,css,-1,&error);g_clear_error(&error);g_free(css);g_free(fg_text);g_free(bg_text);if(!loaded) { g_object_unref(provider);return FALSE; }
 }
 GtkStyleContext *context=gtk_widget_get_style_context(style->view);if(style->provider) gtk_style_context_remove_provider(context,GTK_STYLE_PROVIDER(style->provider));g_clear_object(&style->provider);style->provider=provider;if(provider) gtk_style_context_add_provider(context,GTK_STYLE_PROVIDER(provider),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
 /* Cell renderers accept the complete Pango description, including weights
  * such as 350 which GTK3 CSS and its widget override round or reject. */
 PangoFontDescription *description=enabled ? pango_font_description_from_string(font) : NULL;GList *columns=gtk_tree_view_get_columns(GTK_TREE_VIEW(style->view));
 for(GList *c=columns;c;c=c->next) { GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(c->data));for(GList *p=cells;p;p=p->next) if(GTK_IS_CELL_RENDERER_TEXT(p->data)) g_object_set(p->data,"font-desc",description,NULL);g_list_free(cells); }g_list_free(columns);if(description) pango_font_description_free(description);gtk_widget_queue_resize(style->view);
 gchar *new_font=g_strdup(font),*new_fg=g_strdup(foreground),*new_bg=g_strdup(background);g_free(style->font);g_free(style->foreground);g_free(style->background);style->font=new_font;style->foreground=new_fg;style->background=new_bg;style->enabled=enabled;return TRUE;
}
static void freed(gpointer data) { ListStyle *style=data;g_clear_object(&style->provider);g_free(style->font);g_free(style->foreground);g_free(style->background);g_free(style); }
static void destroyed(GtkWidget *view,gpointer data) { (void)view;((ListStyle *)data)->alive=FALSE; }
static ListStyle *get(GtkWidget *view) {
 ListStyle *style=g_object_get_data(G_OBJECT(view),key);if(style) return style;
 style=g_new0(ListStyle,1);style->view=view;style->alive=TRUE;style->font=g_strdup("Sans 10");style->foreground=g_strdup("#000000");style->background=g_strdup("#ffffff");g_object_set_data_full(G_OBJECT(view),key,style,freed);g_signal_connect(view,"destroy",G_CALLBACK(destroyed),style);return style;
}
static void sensitivity(GtkToggleButton *button,gpointer data) { gtk_widget_set_sensitive(GTK_WIDGET(data),gtk_toggle_button_get_active(button)); }
static void dialog(GtkWidget *view) {
 ListStyle *style=get(view);if(!style->alive) return;g_object_ref(view);GtkWidget *parent=gtk_widget_get_toplevel(view);
 GtkWidget *window=gtk_dialog_new_with_buttons("Font & Color…",GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Apply",GTK_RESPONSE_APPLY,NULL);gtk_widget_set_name(window,"insight-list-style-dialog");
 GtkWidget *content=gtk_dialog_get_content_area(GTK_DIALOG(window)),*enabled=gtk_check_button_new_with_label("Use custom appearance"),*grid=gtk_grid_new();gtk_widget_set_name(enabled,"insight-list-style-enabled");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled),style->enabled);gtk_box_pack_start(GTK_BOX(content),enabled,FALSE,FALSE,4);gtk_box_pack_start(GTK_BOX(content),grid,TRUE,TRUE,4);gtk_grid_set_row_spacing(GTK_GRID(grid),6);gtk_grid_set_column_spacing(GTK_GRID(grid),8);
 GtkWidget *font=gtk_font_button_new_with_font(style->font),*foreground=gtk_color_button_new(),*background=gtk_color_button_new();gtk_widget_set_name(font,"insight-list-style-font");gtk_widget_set_name(foreground,"insight-list-style-foreground");gtk_widget_set_name(background,"insight-list-style-background");GdkRGBA fg,bg;gdk_rgba_parse(&fg,style->foreground);gdk_rgba_parse(&bg,style->background);gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(foreground),&fg);gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(background),&bg);
 GtkWidget *controls[]={font,foreground,background};const gchar *labels[]={"Font","Text color","Background color"};for(guint i=0;i<3;i++) { GtkWidget *label=gtk_label_new(labels[i]);gtk_widget_set_halign(label,GTK_ALIGN_START);gtk_grid_attach(GTK_GRID(grid),label,0,i,1,1);gtk_grid_attach(GTK_GRID(grid),controls[i],1,i,1,1); }g_signal_connect(enabled,"toggled",G_CALLBACK(sensitivity),grid);sensitivity(GTK_TOGGLE_BUTTON(enabled),grid);insight_i18n_apply(window);gtk_widget_show_all(window);
 GtkWidget *failure=gtk_label_new(insight_tr("Choose a font size from 4 to 96 points"));gtk_widget_set_name(failure,"insight-list-style-error");gtk_box_pack_start(GTK_BOX(content),failure,FALSE,FALSE,3);
 while(gtk_dialog_run(GTK_DIALOG(window))==GTK_RESPONSE_APPLY && style->alive) {
  gchar *selected=gtk_font_chooser_get_font(GTK_FONT_CHOOSER(font));gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(foreground),&fg);gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(background),&bg);gchar *fg_text=gdk_rgba_to_string(&fg),*bg_text=gdk_rgba_to_string(&bg);gboolean custom=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enabled));gboolean applied=apply(style,custom,custom ? selected : style->font,custom ? fg_text : style->foreground,custom ? bg_text : style->background);g_free(selected);g_free(fg_text);g_free(bg_text);if(applied) break;gtk_widget_show(failure);
 }
 gtk_widget_destroy(window);g_object_unref(view);
}
GtkWidget *insight_list_style_button(GtkWidget *view) { get(view);GtkWidget *button=gtk_button_new_with_label("Font & Color…");g_signal_connect_object(button,"clicked",G_CALLBACK(dialog),view,G_CONNECT_SWAPPED);return button; }
GtkWidget *insight_list_style_menu_item(GtkWidget *view) { get(view);GtkWidget *item=gtk_menu_item_new_with_label("Font & Color…");g_signal_connect_object(item,"activate",G_CALLBACK(dialog),view,G_CONNECT_SWAPPED);return item; }
void insight_list_style_write(GtkWidget *view,GKeyFile *settings,const gchar *group) {
 ListStyle *style=get(view);g_key_file_set_integer(settings,group,"version",1);g_key_file_set_boolean(settings,group,"enabled",style->enabled);g_key_file_set_string(settings,group,"font",style->font);g_key_file_set_string(settings,group,"foreground",style->foreground);g_key_file_set_string(settings,group,"background",style->background);
}
void insight_list_style_read(GtkWidget *view,GKeyFile *settings,const gchar *group) {
 if(g_key_file_get_integer(settings,group,"version",NULL)!=1) return;
 GError *error=NULL;gboolean enabled=g_key_file_get_boolean(settings,group,"enabled",&error);gchar *font=g_key_file_get_string(settings,group,"font",NULL),*foreground=g_key_file_get_string(settings,group,"foreground",NULL),*background=g_key_file_get_string(settings,group,"background",NULL);
 if(!error && valid(font,foreground,background)) apply(get(view),enabled,font,foreground,background);
 g_clear_error(&error);g_free(font);g_free(foreground);g_free(background);
}
