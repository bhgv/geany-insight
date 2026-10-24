/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_I18N_H
#define INSIGHT_I18N_H
#include <gtk/gtk.h>
void insight_i18n_init(void);
void insight_i18n_finalize(void);
void insight_i18n_menu(GtkWidget *);
gboolean insight_i18n_is_applying(void);
void insight_i18n_apply(GtkWidget *);
const gchar *insight_tr(const gchar *);
const gchar *insight_i18n_english(const gchar *);
const gchar *insight_i18n_language(void);
gchar *insight_i18n_printf(const gchar *,...) G_GNUC_PRINTF(1,2);
/* Stable message keys and structured text: values are data, never translations. */
typedef struct { gchar *key,*value; } InsightI18nText;
static inline void insight_i18n_text_free(gpointer data) { InsightI18nText *b=data;g_free(b->key);g_free(b->value);g_free(b); }
static inline void insight_i18n_key(GObject *object,const gchar *property,const gchar *key) {
 g_object_set_data(object,"insight-i18n-bound",GINT_TO_POINTER(1));gchar *other=g_strconcat("insight-i18n-text-",property,NULL);g_object_set_data(object,other,NULL);g_free(other);gchar *name=g_strconcat("insight-i18n-key-",property,NULL);g_object_set_data_full(object,name,g_strdup(key),g_free);g_object_set_data(object,"insight-i18n-setting",GINT_TO_POINTER(1));g_object_set(object,property,insight_tr(key),NULL);g_object_set_data(object,"insight-i18n-setting",NULL);g_free(name);
}
static inline void insight_i18n_text(GObject *object,const gchar *property,const gchar *key,const gchar *value) {
 g_object_set_data(object,"insight-i18n-bound",GINT_TO_POINTER(1));gchar *other=g_strconcat("insight-i18n-key-",property,NULL);g_object_set_data(object,other,NULL);g_free(other);InsightI18nText *b=g_new0(InsightI18nText,1);b->key=g_strdup(key);b->value=g_strdup(value ? value:"");gchar *name=g_strconcat("insight-i18n-text-",property,NULL);g_object_set_data_full(object,name,b,insight_i18n_text_free);g_free(name);
 gchar **parts=g_strsplit(insight_tr(key),"{value}",-1),*text=g_strjoinv(b->value,parts);g_object_set_data(object,"insight-i18n-setting",GINT_TO_POINTER(1));g_object_set(object,property,text,NULL);g_object_set_data(object,"insight-i18n-setting",NULL);g_free(text);g_strfreev(parts);
}
typedef struct { void (*callback)(gpointer);gpointer data; } InsightI18nRefresh;
static inline void insight_i18n_on_refresh(GObject *object,void (*callback)(gpointer),gpointer data) {
 InsightI18nRefresh *refresh=g_new(InsightI18nRefresh,1);refresh->callback=callback;refresh->data=data;g_object_set_data_full(object,"insight-i18n-refresh",refresh,g_free);
}
void insight_i18n_refresh(void);
/* Opt in enum/status renderers without modifying their model's raw values. */
static inline void insight_i18n_cell(GtkCellRenderer *cell) { g_object_set_data(G_OBJECT(cell),"insight-i18n-cell",GINT_TO_POINTER(1)); }
#ifdef INSIGHT_LOCALIZE_FORMATS
#define g_strdup_printf(...) insight_i18n_printf(__VA_ARGS__)
#endif
#endif
