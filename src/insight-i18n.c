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
typedef struct { GHashTable *en,*ru,*reverse; } Catalog;
static GMutex catalog_mutex;static Catalog catalog;static GHashTable *active,*watched;
static gchar *mode,*settings;static gint russian;static gboolean busy,system_russian;static gulong show_hook;
static const gchar *properties[]={"label","title","subtitle","tooltip-markup","placeholder-text"};
static void translate_widget(GtkWidget *,gpointer);
static void property(GObject *,const gchar *);
static Catalog catalog_new(void) { Catalog c={g_hash_table_new(g_str_hash,g_str_equal),g_hash_table_new(g_str_hash,g_str_equal),g_hash_table_new(g_str_hash,g_str_equal)};return c; }
static void catalog_free(Catalog *c) { if(c->en) g_hash_table_destroy(c->en);if(c->ru) g_hash_table_destroy(c->ru);if(c->reverse) g_hash_table_destroy(c->reverse);memset(c,0,sizeof(*c)); }
/* Interned catalog values stay valid across reload, including worker lookups. */
static void catalog_add(Catalog *c,const gchar *key,const gchar *en,const gchar *ru) {
 const gchar *k=g_intern_string(key),*a=g_intern_string(en),*b=g_intern_string(ru);g_hash_table_insert(c->en,(gpointer)k,(gpointer)a);g_hash_table_insert(c->ru,(gpointer)k,(gpointer)b);
}
static void format_arg(GPtrArray *args,guint index,const gchar *type) {
 if(!index || index>64) { g_ptr_array_set_size(args,65);return; }if(args->len<index) g_ptr_array_set_size(args,index);if(!g_ptr_array_index(args,index-1)) g_ptr_array_index(args,index-1)=g_strdup(type);else if(strcmp(g_ptr_array_index(args,index-1),type)) { g_free(g_ptr_array_index(args,index-1));g_ptr_array_index(args,index-1)=g_strdup("!invalid"); }
}
static guint format_position(const gchar **text) { const gchar *p=*text,*begin=p;guint n=0;while(g_ascii_isdigit(*p)) { n=MIN(100u,n*10+(*p-'0'));p++; }if(p!=begin && *p=='$') { *text=p+1;return n; }return 0; }
static gchar *format_signature(const gchar *text) {
 GPtrArray *args=g_ptr_array_new_with_free_func(g_free);guint next=1;for(const gchar *p=text;*p;p++) if(*p=='%') { p++;if(!*p) break;if(*p=='%') continue;guint position=format_position(&p);while(*p && strchr("-+ #0'I",*p)) p++;if(*p=='*') { p++;guint width=format_position(&p);format_arg(args,width ? width:next++,"int"); }else while(g_ascii_isdigit(*p)) p++;if(*p=='.') { p++;if(*p=='*') { p++;guint precision=format_position(&p);format_arg(args,precision ? precision:next++,"int"); }else while(g_ascii_isdigit(*p)) p++; }
  const gchar *length=p;while(*p && strchr("hljztL",*p)) p++;gchar *modifier=g_strndup(length,p-length),*type=NULL;
  if(*p && strchr("diuoxX",*p)) type=g_strdup_printf("%s:%s",strchr("di",*p) ? "signed":"unsigned",(!strcmp(modifier,"h") || !strcmp(modifier,"hh")) ? "":modifier);
  else if(*p && strchr("fFeEgGaA",*p)) type=g_strdup(!strcmp(modifier,"L") ? "long double":"double");else if(*p=='s') type=g_strdup(*modifier ? "wide string":"string");else if(*p=='c') type=g_strdup(*modifier ? "wide char":"int");else if(*p=='p') type=g_strdup("pointer");else if(*p=='n') type=g_strdup_printf("!write:%s",modifier);
  if(type) { format_arg(args,position ? position:next++,type);g_free(type); }g_free(modifier);if(!*p) break;
 }GString *out=g_string_new(NULL);for(guint i=0;i<args->len;i++) g_string_append_printf(out,"%u:%s;",i+1,g_ptr_array_index(args,i) ? (gchar*)g_ptr_array_index(args,i):"!missing");g_ptr_array_free(args,TRUE);return g_string_free(out,FALSE);
}
static gboolean read_catalog(Catalog *c,const gchar *path,gboolean optional,GError **error) {
 gchar *text=NULL;if(!g_file_get_contents(path,&text,NULL,error)) { if(optional && error && *error && g_error_matches(*error,G_FILE_ERROR,G_FILE_ERROR_NOENT)) { g_clear_error(error);return TRUE; }return FALSE; }
 if(!g_utf8_validate(text,-1,NULL)) { g_set_error(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Invalid UTF-8 catalog: %s",path);g_free(text);return FALSE; }
 gchar **lines=g_strsplit(text,"\n",-1);gboolean ok=TRUE;
 for(guint n=0;lines[n];n++) { gsize length=strlen(lines[n]);if(length && lines[n][length-1]=='\r') lines[n][length-1]=0;if(!*lines[n] || lines[n][0]=='#') continue;gchar **fields=g_strsplit(lines[n],"\t",4);guint count=g_strv_length(fields);
  if(count<2 || count>3 || !*fields[0] || (count==3 && !*fields[1]) || !*fields[count-1]) { g_set_error(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Invalid catalog row %u: %s",n+1,path);ok=FALSE;g_strfreev(fields);break; }
  gchar *key=g_strcompress(fields[0]),*en=g_strcompress(count==3 ? fields[1]:fields[0]),*ru=g_strcompress(fields[count-1]);if(!*key || !*en || !*ru || !g_utf8_validate(key,-1,NULL) || !g_utf8_validate(en,-1,NULL) || !g_utf8_validate(ru,-1,NULL)) { g_set_error(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Invalid decoded text at row %u: %s",n+1,path);ok=FALSE; }
  if(ok && optional) { gchar *a=format_signature(key),*b=format_signature(en),*d=format_signature(ru);if(strcmp(a,b) || strcmp(a,d)) { g_set_error(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Printf placeholders differ at row %u: %s",n+1,path);ok=FALSE; }g_free(a);g_free(b);g_free(d); }
  if(ok) catalog_add(c,key,en,ru);g_free(key);g_free(en);g_free(ru);g_strfreev(fields);if(!ok) break;
 }g_strfreev(lines);g_free(text);return ok;
}
static gboolean load_catalog(GError **error) {
 Catalog next=catalog_new();const gchar *files[]={"native.tsv","insight.tsv"};gboolean ok=TRUE;
 for(guint i=0;i<G_N_ELEMENTS(files) && ok;i++) { gchar *path=g_build_filename(app->datadir,"insight","locale",files[i],NULL);ok=read_catalog(&next,path,FALSE,error);g_free(path); }
 if(ok) { gchar *path=g_build_filename(app->configdir,"insight","interface-texts.tsv",NULL);ok=read_catalog(&next,path,TRUE,error);g_free(path); }
 if(!ok) { catalog_free(&next);return FALSE; }
 GList *keys=g_hash_table_get_keys(next.en);keys=g_list_sort(keys,(GCompareFunc)g_strcmp0);for(GList *p=keys;p;p=p->next) { gpointer a=g_hash_table_lookup(next.en,p->data),b=g_hash_table_lookup(next.ru,p->data);if(!g_hash_table_contains(next.reverse,a)) g_hash_table_insert(next.reverse,a,p->data);if(!g_hash_table_contains(next.reverse,b)) g_hash_table_insert(next.reverse,b,p->data); }g_list_free(keys);
 g_mutex_lock(&catalog_mutex);Catalog old=catalog;catalog=next;active=russian ? catalog.ru:catalog.en;g_mutex_unlock(&catalog_mutex);catalog_free(&old);return TRUE;
}
static const gchar *canonical_locked(const gchar *text) { if(catalog.en && g_hash_table_contains(catalog.en,text)) return text;const gchar *key=catalog.reverse ? g_hash_table_lookup(catalog.reverse,text):NULL;return key ? key:text; }
GEANY_API_SYMBOL
const gchar *insight_i18n_english(const gchar *text) { if(!text) return NULL;g_mutex_lock(&catalog_mutex);const gchar *key=canonical_locked(text);/* The input can be transient, but a returned catalog key is interned. */const gchar *result=catalog.en && g_hash_table_contains(catalog.en,key) ? g_intern_string(key):text;g_mutex_unlock(&catalog_mutex);return result; }
GEANY_API_SYMBOL
const gchar *insight_tr(const gchar *text) { if(!text) return NULL;g_mutex_lock(&catalog_mutex);const gchar *key=canonical_locked(text),*value=active ? g_hash_table_lookup(active,key):NULL;g_mutex_unlock(&catalog_mutex);return value ? value:text; }
gboolean insight_i18n_is_applying(void) { return busy; }
const gchar *insight_i18n_language(void) { return g_atomic_int_get(&russian) ? "ru":"en"; }
typedef struct { gchar *en,*ru;gsize bytes; } Rendered;
static GQueue rendered_queue=G_QUEUE_INIT;static GHashTable *rendered_en,*rendered_ru;static gsize rendered_bytes;
static void rendered_free(gpointer data) { Rendered *r=data;g_free(r->en);g_free(r->ru);g_free(r); }
static void rendered_clear(void) { while(!g_queue_is_empty(&rendered_queue)) rendered_free(g_queue_pop_head(&rendered_queue));g_clear_pointer(&rendered_en,g_hash_table_destroy);g_clear_pointer(&rendered_ru,g_hash_table_destroy);rendered_bytes=0; }
static Rendered *rendered_copy(const gchar *text) {
 g_mutex_lock(&catalog_mutex);Rendered *r=rendered_en ? g_hash_table_lookup(rendered_en,text):NULL;if(!r && rendered_ru) r=g_hash_table_lookup(rendered_ru,text);Rendered *copy=NULL;if(r) { copy=g_new0(Rendered,1);copy->en=g_strdup(r->en);copy->ru=g_strdup(r->ru); }g_mutex_unlock(&catalog_mutex);return copy;
}
GEANY_API_SYMBOL
gchar *insight_i18n_printf(const gchar *format,...) {
 g_mutex_lock(&catalog_mutex);const gchar *key=canonical_locked(format),*en=catalog.en ? g_hash_table_lookup(catalog.en,key):NULL,*ru=catalog.ru ? g_hash_table_lookup(catalog.ru,key):NULL;g_mutex_unlock(&catalog_mutex);if(!en) en=format;if(!ru) ru=en;
 gchar *signature=format_signature(en),*translated_signature=format_signature(ru);if(strcmp(signature,translated_signature)) ru=en;g_free(signature);g_free(translated_signature);
 va_list args,copy;va_start(args,format);va_copy(copy,args);gchar *a=g_strdup_vprintf(en,args),*b=g_strdup_vprintf(ru,copy);va_end(copy);va_end(args);gchar *value=g_strdup(g_atomic_int_get(&russian) ? b:a);
 /* Remember bounded formatted UI values so switching an already displayed
  * counter/error preserves its arguments. Widgets take their own copy. */
 if(strcmp(a,b) && strlen(a)+strlen(b)<=16384) { g_mutex_lock(&catalog_mutex);if(!rendered_en) { rendered_en=g_hash_table_new(g_str_hash,g_str_equal);rendered_ru=g_hash_table_new(g_str_hash,g_str_equal); }
  if(!g_hash_table_contains(rendered_en,a)) { Rendered *r=g_new0(Rendered,1);r->en=a;r->ru=b;r->bytes=strlen(a)+strlen(b)+sizeof(*r);rendered_bytes+=r->bytes;g_queue_push_tail(&rendered_queue,r);g_hash_table_insert(rendered_en,r->en,r);g_hash_table_insert(rendered_ru,r->ru,r);a=b=NULL; }
  while(g_queue_get_length(&rendered_queue)>512 || rendered_bytes>1024*1024) { Rendered *old=g_queue_pop_head(&rendered_queue);g_hash_table_remove(rendered_en,old->en);if(g_hash_table_lookup(rendered_ru,old->ru)==old) g_hash_table_remove(rendered_ru,old->ru);rendered_bytes-=old->bytes;rendered_free(old); }g_mutex_unlock(&catalog_mutex);
 }g_free(a);g_free(b);return value;
}
static gboolean explicit_property(GObject *object,const gchar *name) { gchar *a=g_strconcat("insight-i18n-key-",name,NULL),*b=g_strconcat("insight-i18n-text-",name,NULL);gboolean result=g_object_get_data(object,a) || g_object_get_data(object,b);g_free(a);g_free(b);return result; }
static gboolean protected_data(GtkWidget *widget) {
 for(GtkWidget *p=widget;p;p=gtk_widget_get_parent(p)) if(g_object_get_data(G_OBJECT(p),"insight-no-i18n")) return TRUE;else if(main_widgets.notebook && gtk_widget_get_parent(p)==main_widgets.notebook && gtk_notebook_page_num(GTK_NOTEBOOK(main_widgets.notebook),p)<0) return TRUE;return FALSE;
}
static gboolean excluded(GtkWidget *widget) {
 GtkWidget *top=gtk_widget_get_toplevel(widget);if(g_object_get_data(G_OBJECT(widget),"insight-i18n-destroyed")) return TRUE;
 if(GTK_IS_WINDOW(top) && (gtk_window_get_type_hint(GTK_WINDOW(top))==GDK_WINDOW_TYPE_HINT_TOOLTIP || !strcmp(G_OBJECT_TYPE_NAME(top),"GtkTooltipWindow"))) return TRUE;
 if(g_object_get_data(G_OBJECT(widget),"insight-i18n-bound")) { if(explicit_property(G_OBJECT(widget),"tooltip-text")) return FALSE;for(guint i=0;i<G_N_ELEMENTS(properties);i++) if(explicit_property(G_OBJECT(widget),properties[i])) return FALSE; }
 return protected_data(widget);
}
typedef struct { GString *out;gboolean canonical; } Markup;
static void markup_start(GMarkupParseContext *ctx,const gchar *name,const gchar **attributes,const gchar **values,gpointer data,GError **error) { (void)ctx;(void)error;Markup *m=data;if(!strcmp(name,"i18n-root")) return;g_string_append_printf(m->out,"<%s",name);for(guint i=0;attributes[i];i++) { gchar *escaped=g_markup_escape_text(values[i],-1);g_string_append_printf(m->out," %s=\"%s\"",attributes[i],escaped);g_free(escaped); }g_string_append_c(m->out,'>'); }
static void markup_end(GMarkupParseContext *ctx,const gchar *name,gpointer data,GError **error) { (void)ctx;(void)error;if(strcmp(name,"i18n-root")) g_string_append_printf(((Markup*)data)->out,"</%s>",name); }
static void markup_text(GMarkupParseContext *ctx,const gchar *text,gsize size,gpointer data,GError **error) { (void)ctx;(void)error;Markup *m=data;gchar *part=g_strndup(text,size),*escaped=g_markup_escape_text(m->canonical ? insight_i18n_english(part):insight_tr(part),-1);g_string_append(m->out,escaped);g_free(escaped);g_free(part); }
static gchar *markup_translate(const gchar *text,gboolean canonical) { Markup m={g_string_new(NULL),canonical};const GMarkupParser parser={markup_start,markup_end,markup_text,NULL,NULL};GMarkupParseContext *ctx=g_markup_parse_context_new(&parser,0,&m,NULL);gchar *wrapped=g_strconcat("<i18n-root>",text,"</i18n-root>",NULL);gboolean ok=g_markup_parse_context_parse(ctx,wrapped,-1,NULL) && g_markup_parse_context_end_parse(ctx,NULL);g_free(wrapped);g_markup_parse_context_free(ctx);if(!ok) { g_string_free(m.out,TRUE);return g_strdup(text); }return g_string_free(m.out,FALSE); }
static void property(GObject *object,const gchar *name) {
 if(!strcmp(name,"tooltip-markup") && explicit_property(object,"tooltip-text")) name="tooltip-text";
 if(GTK_IS_WIDGET(object) && protected_data(GTK_WIDGET(object)) && !explicit_property(object,name)) return;
 GParamSpec *spec=g_object_class_find_property(G_OBJECT_GET_CLASS(object),name);if(!spec || spec->value_type!=G_TYPE_STRING || !(spec->flags&G_PARAM_WRITABLE) || !(spec->flags&G_PARAM_READABLE)) return;
 gchar *text=NULL;g_object_get(object,name,&text,NULL);if(!text) return;
 gboolean markup=!strcmp(name,"tooltip-markup") || (GTK_IS_LABEL(object) && !strcmp(name,"label") && gtk_label_get_use_markup(GTK_LABEL(object)));
 gchar *source_key=g_strconcat("insight-i18n-source-",name,NULL),*last_key=g_strconcat("insight-i18n-last-",name,NULL),*key_name=g_strconcat("insight-i18n-key-",name,NULL),*text_name=g_strconcat("insight-i18n-text-",name,NULL),*rendered_name=g_strconcat("insight-i18n-rendered-",name,NULL);
 const gchar *source=g_object_get_data(object,source_key),*last=g_object_get_data(object,last_key),*explicit_key=g_object_get_data(object,key_name);InsightI18nText *binding=g_object_get_data(object,text_name);gchar *translated=NULL;
 if(binding) { gchar **parts=g_strsplit(insight_tr(binding->key),"{value}",-1);translated=g_strjoinv(binding->value,parts);g_strfreev(parts); }
 else if(explicit_key) translated=g_strdup(insight_tr(explicit_key));
 else { if(!source || !last || strcmp(last,text)) { Rendered *rendered=markup && strchr(text,'<') ? NULL:rendered_copy(text);g_object_set_data_full(object,rendered_name,rendered,rendered_free);g_object_set_data_full(object,source_key,rendered ? g_strdup(rendered->en):markup ? markup_translate(text,TRUE):g_strdup(insight_i18n_english(text)),g_free);source=g_object_get_data(object,source_key); }Rendered *rendered=g_object_get_data(object,rendered_name);translated=rendered ? (markup ? g_markup_escape_text(g_atomic_int_get(&russian) ? rendered->ru:rendered->en,-1):g_strdup(g_atomic_int_get(&russian) ? rendered->ru:rendered->en)):markup ? markup_translate(source,FALSE):g_strdup(insight_tr(source)); }
 g_object_set_data_full(object,last_key,g_strdup(translated),g_free);if(strcmp(text,translated)) g_object_set(object,name,translated,NULL);g_free(translated);g_free(source_key);g_free(last_key);g_free(key_name);g_free(text_name);g_free(rendered_name);g_free(text);
}
static void changed(GObject *object,GParamSpec *spec,gpointer data) { (void)data;if(busy || !active || (GTK_IS_WIDGET(object) && excluded(GTK_WIDGET(object)))) return;gboolean found=FALSE;for(guint i=0;i<G_N_ELEMENTS(properties);i++) if(!strcmp(spec->name,properties[i])) found=TRUE;if(!strcmp(spec->name,"tooltip-text")) found=TRUE;if(!strcmp(spec->name,"text") && g_object_get_data(object,"insight-i18n-cell")) found=TRUE;if(!found) return;busy=TRUE;if(!g_object_get_data(object,"insight-i18n-setting")) { const gchar *property_name=!strcmp(spec->name,"tooltip-text") && !explicit_property(object,"tooltip-text") ? "tooltip-markup":spec->name;gchar *a=g_strconcat("insight-i18n-key-",property_name,NULL),*b=g_strconcat("insight-i18n-text-",property_name,NULL);g_object_set_data(object,a,NULL);g_object_set_data(object,b,NULL);g_free(a);g_free(b); }property(object,!strcmp(spec->name,"tooltip-text") ? "tooltip-markup":spec->name);busy=FALSE; }
static void unwatched(gpointer data,GObject *object) { (void)data;if(watched) g_hash_table_remove(watched,object); }
static void widget_destroyed(GtkWidget *widget,gpointer data) { (void)data;g_object_set_data(G_OBJECT(widget),"insight-i18n-destroyed",GINT_TO_POINTER(1)); }
static void watch(GObject *object) { if(!watched || g_hash_table_contains(watched,object)) return;g_hash_table_add(watched,object);g_object_weak_ref(object,unwatched,NULL);if(!g_object_get_data(object,"insight-i18n-watched")) { g_object_set_data(object,"insight-i18n-watched",GINT_TO_POINTER(1));g_signal_connect(object,"notify",G_CALLBACK(changed),NULL);if(GTK_IS_WIDGET(object)) g_signal_connect(object,"destroy",G_CALLBACK(widget_destroyed),NULL); } }
typedef struct { GtkTreeRowReference *row;gchar *source,*last;Rendered *rendered; } Row;
static void row_free(gpointer data) { Row *r=data;gtk_tree_row_reference_free(r->row);g_free(r->source);g_free(r->last);if(r->rendered) rendered_free(r->rendered);g_free(r); }
typedef struct { GtkTreeModel *model;GPtrArray *rows; } Combo;
static void combo_free(gpointer data) { Combo *c=data;g_ptr_array_free(c->rows,TRUE);g_object_unref(c->model);g_free(c); }
static void translate_combo(GtkWidget *widget);
static void combo_changed(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) { (void)path;(void)iter;GtkWidget *widget=data;if(!busy && !excluded(widget) && gtk_combo_box_get_model(GTK_COMBO_BOX(widget))==model) { busy=TRUE;translate_combo(widget);busy=FALSE; } }
static void translate_combo(GtkWidget *widget) {
 GtkTreeModel *model=gtk_combo_box_get_model(GTK_COMBO_BOX(widget));if(!GTK_IS_LIST_STORE(model)) return;Combo *combo=g_object_get_data(G_OBJECT(widget),"insight-i18n-combo");
 if(!combo || combo->model!=model) { combo=g_new0(Combo,1);combo->model=g_object_ref(model);combo->rows=g_ptr_array_new_with_free_func(row_free);g_object_set_data_full(G_OBJECT(widget),"insight-i18n-combo",combo,combo_free);g_signal_connect_object(model,"row-changed",G_CALLBACK(combo_changed),widget,0); }
 for(guint i=combo->rows->len;i>0;i--) if(!gtk_tree_row_reference_valid(((Row*)g_ptr_array_index(combo->rows,i-1))->row)) g_ptr_array_remove_index(combo->rows,i-1);
 GtkTreeIter iter;for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gchar *text=NULL;gtk_tree_model_get(model,&iter,0,&text,-1);if(!text) continue;GtkTreePath *path=gtk_tree_model_get_path(model,&iter);Row *row=NULL;
  for(guint i=0;i<combo->rows->len;i++) { Row *r=g_ptr_array_index(combo->rows,i);GtkTreePath *p=gtk_tree_row_reference_get_path(r->row);gboolean same=p && !gtk_tree_path_compare(path,p);if(p) gtk_tree_path_free(p);if(same) { row=r;break; } }
  if(!row) { row=g_new0(Row,1);row->row=gtk_tree_row_reference_new(model,path);g_ptr_array_add(combo->rows,row); }if(!row->source || g_strcmp0(row->last,text)) { g_free(row->source);if(row->rendered) rendered_free(row->rendered);row->rendered=rendered_copy(text);row->source=g_strdup(row->rendered ? row->rendered->en:insight_i18n_english(text)); }
  const gchar *translated=row->rendered ? (g_atomic_int_get(&russian) ? row->rendered->ru:row->rendered->en):insight_tr(row->source);g_free(row->last);row->last=g_strdup(translated);if(strcmp(text,translated)) gtk_list_store_set(GTK_LIST_STORE(model),&iter,0,translated,-1);gtk_tree_path_free(path);g_free(text);
 }
}
static void translate_widget(GtkWidget *widget,gpointer data) {
 (void)data;if(excluded(widget)) { /* Header title labels are owned by GTK; explicitly bound button tips are safe. */if(GTK_IS_HEADER_BAR(widget)) gtk_container_foreach(GTK_CONTAINER(widget),translate_widget,NULL);if(GTK_IS_TREE_VIEW(widget)) { GList *columns=gtk_tree_view_get_columns(GTK_TREE_VIEW(widget));for(GList *p=columns;p;p=p->next) { watch(p->data);property(p->data,"title");GtkWidget *header=gtk_tree_view_column_get_widget(p->data);if(header) translate_widget(header,NULL); }g_list_free(columns); }return; }
 watch(G_OBJECT(widget));property(G_OBJECT(widget),"tooltip-markup");
 if(GTK_IS_LABEL(widget) || GTK_IS_BUTTON(widget) || GTK_IS_TOOL_BUTTON(widget) || GTK_IS_MENU_ITEM(widget)) property(G_OBJECT(widget),"label");
 if(GTK_IS_WINDOW(widget)) property(G_OBJECT(widget),"title");
 if(GTK_IS_ENTRY(widget)) property(G_OBJECT(widget),"placeholder-text");
 if(GTK_IS_TREE_VIEW(widget)) { GList *columns=gtk_tree_view_get_columns(GTK_TREE_VIEW(widget));for(GList *p=columns;p;p=p->next) { watch(p->data);property(p->data,"title");GtkWidget *header=gtk_tree_view_column_get_widget(p->data);if(header) translate_widget(header,NULL);GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(p->data));for(GList *c=cells;c;c=c->next) if(g_object_get_data(c->data,"insight-i18n-cell")) { watch(c->data);property(c->data,"text"); }g_list_free(cells); }g_list_free(columns); }
 if(GTK_IS_COMBO_BOX_TEXT(widget)) translate_combo(widget);
 if(GTK_IS_NOTEBOOK(widget)) { for(gint i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(widget));i++) { GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(widget),i),*tab=gtk_notebook_get_tab_label(GTK_NOTEBOOK(widget),page),*menu=gtk_notebook_get_menu_label(GTK_NOTEBOOK(widget),page);if(tab) translate_widget(tab,NULL);if(menu && menu!=tab) translate_widget(menu,NULL); }for(guint i=0;i<2;i++) { GtkWidget *action=gtk_notebook_get_action_widget(GTK_NOTEBOOK(widget),i ? GTK_PACK_END:GTK_PACK_START);if(action) translate_widget(action,NULL); } }
 if(GTK_IS_WINDOW(widget)) { GtkWidget *header=gtk_window_get_titlebar(GTK_WINDOW(widget));if(header) translate_widget(header,NULL); }
 if(GTK_IS_MENU_ITEM(widget)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget));if(menu) translate_widget(menu,NULL); }
 if(GTK_IS_CONTAINER(widget)) gtk_container_foreach(GTK_CONTAINER(widget),translate_widget,NULL);
}
void insight_i18n_apply(GtkWidget *widget) { if(busy || !active || !widget) return;busy=TRUE;translate_widget(widget,NULL);busy=FALSE; }
GEANY_API_SYMBOL
void insight_i18n_refresh(void) {
 if(busy || !active) return;GList *windows=gtk_window_list_toplevels();for(GList *p=windows;p;p=p->next) insight_i18n_apply(p->data);g_list_free(windows);if(main_widgets.editor_menu) insight_i18n_apply(main_widgets.editor_menu);
 /* Refresh weakly registered detached menus, columns and explicitly bound values. */
 GPtrArray *objects=g_ptr_array_new_with_free_func(g_object_unref);GHashTableIter iter;gpointer object;g_hash_table_iter_init(&iter,watched);while(g_hash_table_iter_next(&iter,&object,NULL)) g_ptr_array_add(objects,g_object_ref(object));busy=TRUE;
 for(guint i=0;i<objects->len;i++) { GObject *item=g_ptr_array_index(objects,i);if(GTK_IS_WIDGET(item) && excluded(GTK_WIDGET(item))) continue;for(guint n=0;n<G_N_ELEMENTS(properties);n++) property(item,properties[n]);if(g_object_get_data(item,"insight-i18n-cell")) property(item,"text");if(GTK_IS_WIDGET(item)) gtk_widget_queue_draw(GTK_WIDGET(item)); }
 busy=FALSE;for(guint i=0;i<objects->len;i++) { GObject *item=g_ptr_array_index(objects,i);if(GTK_IS_WIDGET(item) && excluded(GTK_WIDGET(item))) continue;InsightI18nRefresh *refresh=g_object_get_data(item,"insight-i18n-refresh");if(refresh) refresh->callback(refresh->data); }g_ptr_array_free(objects,TRUE);
 insight_help_locale_changed();insight_window_list_locale_changed();insight_symbol_views_locale_changed();insight_workbench_locale_changed();
}
static gboolean shown(GSignalInvocationHint *hint,guint count,const GValue *values,gpointer data) { (void)hint;(void)data;if(count) insight_i18n_apply(g_value_get_object(values));return TRUE; }
void insight_i18n_init(void) {
 const gchar * const *names=g_get_language_names();system_russian=names && names[0] && g_str_has_prefix(names[0],"ru");settings=g_build_filename(app->configdir,"insight","interface.conf",NULL);GKeyFile *key=g_key_file_new();if(g_key_file_load_from_file(key,settings,G_KEY_FILE_NONE,NULL)) mode=g_key_file_get_string(key,"interface","language",NULL);g_key_file_free(key);if(!mode || (strcmp(mode,"auto") && strcmp(mode,"ru") && strcmp(mode,"en"))) { g_free(mode);mode=g_strdup("auto"); }g_atomic_int_set(&russian,!strcmp(mode,"ru") || (!strcmp(mode,"auto") && system_russian));g_setenv("LANGUAGE",russian ? "ru":"en",TRUE);
#ifdef LC_MESSAGES
 setlocale(LC_MESSAGES,"");
#endif
 watched=g_hash_table_new(g_direct_hash,g_direct_equal);GError *error=NULL;if(!load_catalog(&error)) { g_warning("%s",error->message);g_clear_error(&error);catalog=catalog_new();active=russian ? catalog.ru:catalog.en; }
 show_hook=g_signal_add_emission_hook(g_signal_lookup("show",GTK_TYPE_WIDGET),0,shown,NULL,NULL);
}
static void reload_texts(GtkMenuItem *item,gpointer data) { (void)item;(void)data;GError *error=NULL;if(!load_catalog(&error)) { ui_set_statusbar(TRUE,"%s",error->message);g_clear_error(&error);return; }insight_i18n_refresh(); }
static void select_language(GtkCheckMenuItem *item,gpointer data) {
 if(!gtk_check_menu_item_get_active(item)) return;const gchar *choice=data;g_free(mode);mode=g_strdup(choice);g_mutex_lock(&catalog_mutex);g_atomic_int_set(&russian,!strcmp(mode,"ru") || (!strcmp(mode,"auto") && system_russian));active=russian ? catalog.ru:catalog.en;g_mutex_unlock(&catalog_mutex);g_setenv("LANGUAGE",russian ? "ru":"en",TRUE);
#ifdef ENABLE_NLS
 textdomain(GETTEXT_PACKAGE);
#endif
 GKeyFile *key=g_key_file_new();g_key_file_set_string(key,"interface","language",mode);gchar *contents=g_key_file_to_data(key,NULL,NULL),*directory=g_path_get_dirname(settings);GError *error=NULL;if(g_mkdir_with_parents(directory,0700) || !g_file_set_contents(settings,contents,-1,&error)) ui_set_statusbar(TRUE,"Cannot save interface language: %s",error ? error->message:"Cannot create directory");g_clear_error(&error);g_free(directory);g_free(contents);g_key_file_free(key);insight_i18n_refresh();
}
void insight_i18n_menu(GtkWidget *options) {
 GtkWidget *item=gtk_menu_item_new_with_label("Interface language"),*menu=gtk_menu_new();gtk_widget_set_name(item,"insight-interface-language");gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),menu);gtk_menu_shell_append(GTK_MENU_SHELL(options),item);const gchar *ids[]={"auto","ru","en"},*labels[]={"Automatic (system locale)","Русский","English"};GSList *group=NULL;
 for(guint n=0;n<G_N_ELEMENTS(ids);n++) { GtkWidget *radio=gtk_radio_menu_item_new_with_label(group,labels[n]);group=gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(radio));gchar *name=g_strconcat("insight-interface-",ids[n],NULL);gtk_widget_set_name(radio,name);g_free(name);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(radio),!strcmp(mode,ids[n]));g_signal_connect(radio,"toggled",G_CALLBACK(select_language),(gpointer)ids[n]);gtk_menu_shell_append(GTK_MENU_SHELL(menu),radio); }
 gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());GtkWidget *reload=gtk_menu_item_new_with_label("Reload interface texts");gtk_widget_set_name(reload,"insight-interface-reload");gtk_widget_set_tooltip_text(reload,"Reload shared captions and interface-texts.tsv overrides without restarting");gtk_menu_shell_append(GTK_MENU_SHELL(menu),reload);g_signal_connect(reload,"activate",G_CALLBACK(reload_texts),NULL);gtk_widget_show_all(item);
}
void insight_i18n_finalize(void) { if(show_hook) g_signal_remove_emission_hook(g_signal_lookup("show",GTK_TYPE_WIDGET),show_hook);show_hook=0;if(watched) { GHashTableIter iter;gpointer object;g_hash_table_iter_init(&iter,watched);while(g_hash_table_iter_next(&iter,&object,NULL)) g_object_weak_unref(object,unwatched,NULL);g_clear_pointer(&watched,g_hash_table_destroy); }g_mutex_lock(&catalog_mutex);active=NULL;rendered_clear();catalog_free(&catalog);g_clear_pointer(&mode,g_free);g_clear_pointer(&settings,g_free);g_mutex_unlock(&catalog_mutex); }
