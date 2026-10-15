/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-i18n.h"
#include "insight-help.h"
#include "insight-window-list.h"
#include "insight-symbol-view.h"
#include "insight-workbench.h"
#include "main.h"
#include "ui_utils.h"
#include <locale.h>
#ifdef ENABLE_NLS
#include <libintl.h>
#endif
#include <stdarg.h>
#include <glib/gstdio.h>
#include <string.h>
static GMutex catalog_mutex;static GHashTable *ru,*english;static gchar *mode,*settings;static gboolean russian,busy,system_russian;static gulong show_hook;
static void read_catalog(const gchar *path) {
 gchar *text=NULL;if(!g_file_get_contents(path,&text,NULL,NULL)) return;gchar **lines=g_strsplit(text,"\n",-1);
 for(guint n=0;lines[n];n++) { gsize length=strlen(lines[n]);if(length && lines[n][length-1]=='\r') lines[n][length-1]=0;if(lines[n][0]=='#') continue;gchar *tab=strchr(lines[n],'\t');if(!tab) continue;*tab=0;gchar *en=g_strcompress(lines[n]),*translated=g_strcompress(tab+1);if(*en && *translated) { g_hash_table_replace(ru,g_strdup(en),g_strdup(translated));if(!g_hash_table_contains(english,translated)) g_hash_table_insert(english,g_strdup(translated),g_strdup(en)); }g_free(en);g_free(translated); }g_strfreev(lines);g_free(text);
}
const gchar *insight_i18n_english(const gchar *text) { if(!text) return NULL;const gchar *original=english ? g_hash_table_lookup(english,text) : NULL;return original ? original : text; }
const gchar *insight_tr(const gchar *text) { if(!text) return NULL;const gchar *original=insight_i18n_english(text);const gchar *translated=g_atomic_int_get(&russian) && ru ? g_hash_table_lookup(ru,original) : NULL;return translated ? translated : original; }
gboolean insight_i18n_is_applying(void) { return busy; }
const gchar *insight_i18n_language(void) { return russian ? "ru" : "en"; }
gchar *insight_i18n_printf(const gchar *format,...) { g_mutex_lock(&catalog_mutex);gchar *localized=g_strdup(insight_tr(format));g_mutex_unlock(&catalog_mutex);va_list args;va_start(args,format);gchar *value=g_strdup_vprintf(localized,args);va_end(args);g_free(localized);return value; }
static gboolean excluded(GtkWidget *widget) { for(GtkWidget *p=widget;p;p=gtk_widget_get_parent(p)) if(g_object_get_data(G_OBJECT(p),"insight-no-i18n")) return TRUE;else if(main_widgets.notebook && gtk_widget_get_parent(p)==main_widgets.notebook && gtk_notebook_page_num(GTK_NOTEBOOK(main_widgets.notebook),p)<0) return TRUE;return FALSE; }
static void property(GObject *object,const gchar *name) {
 GParamSpec *spec=g_object_class_find_property(G_OBJECT_GET_CLASS(object),name);if(!spec || spec->value_type!=G_TYPE_STRING || !(spec->flags&G_PARAM_WRITABLE)) return;
 gchar *text=NULL;g_object_get(object,name,&text,NULL);if(text) {
  gchar *source_key=g_strconcat("insight-i18n-source-",name,NULL),*last_key=g_strconcat("insight-i18n-last-",name,NULL);const gchar *source=g_object_get_data(object,source_key),*last=g_object_get_data(object,last_key);
  if(!source || !last || strcmp(last,text)) { g_object_set_data_full(object,source_key,g_strdup(insight_i18n_english(text)),g_free);source=g_object_get_data(object,source_key); }
  const gchar *translated=insight_tr(source);g_object_set_data_full(object,last_key,g_strdup(translated),g_free);if(strcmp(text,translated)) g_object_set(object,name,translated,NULL);g_free(source_key);g_free(last_key);
 }g_free(text);
}
static void changed(GObject *object,GParamSpec *spec,gpointer data) { (void)data;if(busy || !ru || (GTK_IS_WIDGET(object) && excluded(GTK_WIDGET(object)))) return;if(strcmp(spec->name,"label") && strcmp(spec->name,"title") && strcmp(spec->name,"tooltip-text") && strcmp(spec->name,"placeholder-text")) return;busy=TRUE;property(object,spec->name);busy=FALSE; }
static void watch(GObject *object) { if(g_object_get_data(object,"insight-i18n-watched")) return;g_object_set_data(object,"insight-i18n-watched",GINT_TO_POINTER(1));g_signal_connect(object,"notify",G_CALLBACK(changed),NULL); }
static void translate_widget(GtkWidget *widget,gpointer data) {
 (void)data;if(excluded(widget)) return;watch(G_OBJECT(widget));property(G_OBJECT(widget),"tooltip-text");
 if(GTK_IS_LABEL(widget) || GTK_IS_BUTTON(widget) || GTK_IS_TOOL_BUTTON(widget) || GTK_IS_IMAGE_MENU_ITEM(widget)) property(G_OBJECT(widget),"label");
 if(GTK_IS_WINDOW(widget)) property(G_OBJECT(widget),"title");
 if(GTK_IS_ENTRY(widget)) property(G_OBJECT(widget),"placeholder-text");
 if(GTK_IS_TREE_VIEW(widget)) { GList *columns=gtk_tree_view_get_columns(GTK_TREE_VIEW(widget));for(GList *p=columns;p;p=p->next) { watch(p->data);property(p->data,"title"); }g_list_free(columns); }
 if(GTK_IS_COMBO_BOX_TEXT(widget)) { GtkTreeModel *model=gtk_combo_box_get_model(GTK_COMBO_BOX(widget));if(GTK_IS_LIST_STORE(model)) { GtkTreeIter iter;for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gchar *text=NULL;gtk_tree_model_get(model,&iter,0,&text,-1);if(text && strcmp(text,insight_tr(text))) gtk_list_store_set(GTK_LIST_STORE(model),&iter,0,insight_tr(text),-1);g_free(text); } } }
 if(GTK_IS_MENU_ITEM(widget)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget));if(menu) translate_widget(menu,NULL); }
 if(GTK_IS_CONTAINER(widget)) gtk_container_foreach(GTK_CONTAINER(widget),translate_widget,NULL);
}
void insight_i18n_apply(GtkWidget *widget) { if(busy || !ru || !widget) return;busy=TRUE;translate_widget(widget,NULL);busy=FALSE; }
static gboolean shown(GSignalInvocationHint *hint,guint count,const GValue *values,gpointer data) { (void)hint;(void)data;if(count) insight_i18n_apply(g_value_get_object(values));return TRUE; }
void insight_i18n_init(void) {
 const gchar * const *names=g_get_language_names();system_russian=names && names[0] && g_str_has_prefix(names[0],"ru");settings=g_build_filename(app->configdir,"insight","interface.conf",NULL);GKeyFile *key=g_key_file_new();if(g_key_file_load_from_file(key,settings,G_KEY_FILE_NONE,NULL)) mode=g_key_file_get_string(key,"interface","language",NULL);g_key_file_free(key);if(!mode || (strcmp(mode,"auto") && strcmp(mode,"ru") && strcmp(mode,"en"))) { g_free(mode);mode=g_strdup("auto"); }g_atomic_int_set(&russian,!strcmp(mode,"ru") || (!strcmp(mode,"auto") && system_russian));
 /* Native GTK/Geany gettext handles future dialogs and formatted messages;
  * our bilingual catalog also refreshes existing widgets without changing LC_CTYPE. */
 g_setenv("LANGUAGE",russian ? "ru" : "en",TRUE);
#ifdef LC_MESSAGES
 setlocale(LC_MESSAGES,"");
#endif
 ru=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);english=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
 const gchar *catalogs[]={"native.tsv","insight.tsv"};for(guint n=0;n<G_N_ELEMENTS(catalogs);n++) { gchar *path=g_build_filename(app->datadir,"insight","locale",catalogs[n],NULL);read_catalog(path);g_free(path); }
 show_hook=g_signal_add_emission_hook(g_signal_lookup("show",GTK_TYPE_WIDGET),0,shown,NULL,NULL);
}
static void select_language(GtkCheckMenuItem *item,gpointer data) {
 if(!gtk_check_menu_item_get_active(item)) return;
 const gchar *choice=data;g_free(mode);mode=g_strdup(choice);g_atomic_int_set(&russian,!strcmp(mode,"ru") || (!strcmp(mode,"auto") && system_russian));g_setenv("LANGUAGE",russian ? "ru" : "en",TRUE);
#ifdef ENABLE_NLS
 textdomain(GETTEXT_PACKAGE);
#endif
 GKeyFile *key=g_key_file_new();g_key_file_set_string(key,"interface","language",mode);gchar *contents=g_key_file_to_data(key,NULL,NULL),*directory=g_path_get_dirname(settings);GError *error=NULL;if(g_mkdir_with_parents(directory,0700) || !g_file_set_contents(settings,contents,-1,&error)) ui_set_statusbar(TRUE,"Cannot save interface language: %s",error ? error->message : "Cannot create directory");g_clear_error(&error);g_free(directory);g_free(contents);g_key_file_free(key);
 GList *windows=gtk_window_list_toplevels();for(GList *p=windows;p;p=p->next) insight_i18n_apply(p->data);g_list_free(windows);if(main_widgets.editor_menu) insight_i18n_apply(main_widgets.editor_menu);insight_help_locale_changed();insight_window_list_locale_changed();insight_symbol_views_locale_changed();insight_workbench_locale_changed();
}
void insight_i18n_menu(GtkWidget *options) {
 GtkWidget *item=gtk_menu_item_new_with_label("Interface language"),*menu=gtk_menu_new();gtk_widget_set_name(item,"insight-interface-language");gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),menu);gtk_menu_shell_append(GTK_MENU_SHELL(options),item);const gchar *ids[]={"auto","ru","en"},*labels[]={"Automatic (system locale)","Русский","English"};GSList *group=NULL;
 for(guint n=0;n<G_N_ELEMENTS(ids);n++) { GtkWidget *radio=gtk_radio_menu_item_new_with_label(group,labels[n]);group=gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(radio));gchar *name=g_strconcat("insight-interface-",ids[n],NULL);gtk_widget_set_name(radio,name);g_free(name);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(radio),!strcmp(mode,ids[n]));g_signal_connect(radio,"toggled",G_CALLBACK(select_language),(gpointer)ids[n]);gtk_menu_shell_append(GTK_MENU_SHELL(menu),radio); }gtk_widget_show_all(item);
}
void insight_i18n_finalize(void) { if(show_hook) g_signal_remove_emission_hook(g_signal_lookup("show",GTK_TYPE_WIDGET),show_hook);show_hook=0;g_mutex_lock(&catalog_mutex);g_clear_pointer(&ru,g_hash_table_destroy);g_clear_pointer(&english,g_hash_table_destroy);g_clear_pointer(&mode,g_free);g_clear_pointer(&settings,g_free);g_mutex_unlock(&catalog_mutex); }
