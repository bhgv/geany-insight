/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_HISTORY_UI_H
#define INSIGHT_HISTORY_UI_H
#include <gtk/gtk.h>
#include "insight-i18n.h"
typedef struct {
 gpointer data;guint (*count)(gpointer);guint (*position)(gpointer);void (*jump)(gpointer,guint);void (*preview)(gpointer,guint,gchar **,gchar **);
 gchar *(*label)(gpointer,guint);GtkWidget *undo,*redo,*menu,*list;
} InsightHistoryUI;
static inline gchar *insight_history_excerpt(const gchar *text,gsize offset) {
 gsize size=strlen(text);offset=MIN(offset,size);while(offset && !g_utf8_validate(text,offset,NULL)) offset--;gsize begin=offset;for(guint i=0;begin && i<80;i++) begin--;while(begin<offset && ((guchar)text[begin]&0xc0)==0x80) begin++;
 const gchar *end=text+begin;guint lines=0,chars=0;while(*end && chars++<500 && lines<8) { if(*end=='\n') lines++;end=g_utf8_next_char(end); }return g_strndup(text+begin,end-(text+begin));
}
static inline void insight_history_text_preview(const gchar *before,const gchar *after,gchar **a,gchar **b) {
 gsize i=0;while(before[i] && after[i] && before[i]==after[i]) i++;*a=insight_history_excerpt(before,i);*b=insight_history_excerpt(after,i);
}
static inline gchar *insight_history_text_label(const gchar *before,const gchar *after) {
 gsize a=strlen(before),b=strlen(after),start=0;while(start<MIN(a,b) && before[start]==after[start]) start++;gsize end=0;while(end<a-start && end<b-start && before[a-1-end]==after[b-1-end]) end++;
 return start==a && a==b ? g_strdup(insight_tr("Merge decisions changed")):g_strdup_printf("@%" G_GSIZE_FORMAT " −%" G_GSIZE_FORMAT " +%" G_GSIZE_FORMAT " bytes",start,a-start-end,b-start-end);
}
static inline gboolean insight_history_tip(GtkWidget *w,gint x,gint y,gboolean keyboard,GtkTooltip *tip,gpointer data) {
 (void)x;(void)y;(void)keyboard;InsightHistoryUI *ui=data;guint index=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w),"history-index"));gchar *a=NULL,*b=NULL;ui->preview(ui->data,index,&a,&b);if(!a || !b) { g_free(a);g_free(b);return FALSE; }
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);const gchar *texts[]={a,b},*titles[]={"Before","After"},*colors[]={"#808080","#d73535"};for(guint i=0;i<2;i++) { gchar *safe=g_markup_escape_text(texts[i],-1),*markup=g_strdup_printf("<b>%s</b>\n<span font_family=\"monospace\" foreground=\"%s\">%s</span>",insight_tr(titles[i]),colors[i],safe);GtkWidget *label=gtk_label_new(NULL);gtk_label_set_markup(GTK_LABEL(label),markup);gtk_label_set_xalign(GTK_LABEL(label),0);gtk_label_set_yalign(GTK_LABEL(label),0);gtk_label_set_max_width_chars(GTK_LABEL(label),60);gtk_label_set_line_wrap(GTK_LABEL(label),TRUE);gtk_box_pack_start(GTK_BOX(box),label,TRUE,TRUE,0);g_free(safe);g_free(markup); }gtk_widget_show_all(box);gtk_tooltip_set_custom(tip,box);g_free(a);g_free(b);return TRUE;
}
static inline void insight_history_activate(GtkWidget *w,gpointer data) { InsightHistoryUI *ui=data;ui->jump(ui->data,GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w),"history-index"))); }
static inline void insight_history_show(GtkWidget *menu,gpointer data) {
 InsightHistoryUI *ui=data;GList *children=gtk_container_get_children(GTK_CONTAINER(menu));for(GList *p=children;p;p=p->next) gtk_widget_destroy(p->data);g_list_free(children);
 guint count=ui->count(ui->data),current=ui->position(ui->data);for(guint i=0;i<count;i++) { gchar *event=ui->label ? ui->label(ui->data,i):g_strdup(i ? insight_tr("Result change"):insight_tr("Initial Result"));gchar *text=g_strdup_printf("%s%u — %s",i==current ? "✓ ":"",i,event);g_free(event);GtkWidget *item=gtk_menu_item_new_with_label(text);g_free(text);g_object_set_data(G_OBJECT(item),"history-index",GUINT_TO_POINTER(i));gtk_widget_set_has_tooltip(item,TRUE);g_signal_connect(item,"query-tooltip",G_CALLBACK(insight_history_tip),ui);g_signal_connect(item,"activate",G_CALLBACK(insight_history_activate),ui);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item); }
}
static inline void insight_history_step(GtkWidget *w,gpointer data) { InsightHistoryUI *ui=data;guint p=ui->position(ui->data),n=ui->count(ui->data);if(w==ui->undo && p) ui->jump(ui->data,p-1);else if(w==ui->redo && p+1<n) ui->jump(ui->data,p+1); }
static inline void insight_history_update(InsightHistoryUI *ui,gboolean enabled) { if(!ui) return;guint p=ui->position(ui->data),n=ui->count(ui->data);gtk_widget_set_sensitive(ui->undo,enabled && p>0);gtk_widget_set_sensitive(ui->redo,enabled && p+1<n);gtk_widget_set_sensitive(ui->list,enabled && n>0); }
static inline InsightHistoryUI *insight_history_ui(GtkWidget *box,gpointer data,guint (*count)(gpointer),guint (*position)(gpointer),void (*jump)(gpointer,guint),void (*preview)(gpointer,guint,gchar **,gchar **)) {
 InsightHistoryUI *ui=g_new0(InsightHistoryUI,1);ui->data=data;ui->count=count;ui->position=position;ui->jump=jump;ui->preview=preview;GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);g_object_set_data_full(G_OBJECT(row),"history-ui",ui,g_free);
 ui->undo=gtk_button_new_from_icon_name("edit-undo-symbolic",GTK_ICON_SIZE_SMALL_TOOLBAR);ui->redo=gtk_button_new_from_icon_name("edit-redo-symbolic",GTK_ICON_SIZE_SMALL_TOOLBAR);GtkWidget *buttons[]={ui->undo,ui->redo};for(guint i=0;i<2;i++) { gtk_widget_set_tooltip_text(buttons[i],insight_tr(i ? "Redo":"Undo"));gtk_box_pack_start(GTK_BOX(row),buttons[i],FALSE,FALSE,0);g_signal_connect(buttons[i],"clicked",G_CALLBACK(insight_history_step),ui); }
 ui->list=gtk_menu_button_new();gtk_button_set_label(GTK_BUTTON(ui->list),insight_tr("History"));gtk_box_pack_start(GTK_BOX(row),ui->list,FALSE,FALSE,0);ui->menu=gtk_menu_new();gtk_menu_shell_append(GTK_MENU_SHELL(ui->menu),gtk_menu_item_new_with_label(insight_tr("History")));gtk_menu_button_set_popup(GTK_MENU_BUTTON(ui->list),ui->menu);g_signal_connect(ui->menu,"show",G_CALLBACK(insight_history_show),ui);return ui;
}
#endif
