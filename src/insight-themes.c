/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "insight-themes.h"
#include "insight-appearance.h"
#include "insight-formatting.h"
#include "insight-i18n.h"
#include "highlighting.h"
#include "ui_utils.h"
#include <gmodule.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <pango/pangocairo.h>
#include <string.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
typedef struct { gchar *name,*folder; } Theme;
static GPtrArray *themes,*font_files;
static GHashTable *rules,*families;
static GtkCssProvider *provider;
static gchar *selected,*config;
static gboolean initialized;
static GModule *font_module;
static void free_theme(gpointer data) { Theme *t=data;
 g_free(t->name);
 g_free(t->folder);
 g_free(t); }
static void free_style(gpointer data) { InsightThemeStyle *s=data;
 g_free(s->font);
 g_free(s); }
static gboolean safe_name(const gchar *name) { return name && *name && strlen(name)<=160 && g_utf8_validate(name,-1,NULL) && strcmp(name,".") && strcmp(name,"..") && !strpbrk(name,"/\\\r\n"); }
static gchar *read_css(const gchar *path,GError **error) {
 GStatBuf info;
 if(g_stat(path,&info) || info.st_size>512*1024 || info.st_size<0) { g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Theme file is missing or too large");
 return NULL; }
 gchar *text=NULL;gsize size=0;
 if(!g_file_get_contents(path,&text,&size,error)) return NULL;
 if(size>512*1024 || memchr(text,0,size) || !g_utf8_validate(text,size,NULL) || strchr(text,'@') || strstr(text,"url(")) { g_free(text);g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Theme CSS must be UTF-8 without imports or URLs");
 return NULL; }return text;
}
static void free_font_file(gpointer data) { GFile *file=data;g_file_delete(file,NULL,NULL);g_object_unref(file); }
/* Fontconfig requires a file. Keep private, complete decompressions only for
 * this process. Bundled originals are uncompressed and used directly. */
static gchar *font_path(const gchar *path) {
 GStatBuf info;if(g_stat(path,&info) || info.st_size>8*1024*1024) return NULL;
 GError *error=NULL;GFile *source=g_file_new_for_path(path);GFileInputStream *input=g_file_read(source,NULL,&error);g_object_unref(source);
 if(!input) { g_clear_error(&error);return NULL; }
 guchar signature[2];gsize size=0;
 if(!g_input_stream_read_all(G_INPUT_STREAM(input),signature,sizeof signature,&size,NULL,&error) || size!=2) { g_object_unref(input);g_clear_error(&error);return NULL; }
 if(signature[0]!=0x1f || signature[1]!=0x8b) { g_object_unref(input);return g_strdup(path); }
 if(!g_seekable_seek(G_SEEKABLE(input),0,G_SEEK_SET,NULL,&error)) { g_object_unref(input);g_clear_error(&error);return NULL; }
 GZlibDecompressor *decoder=g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP);GInputStream *unpacked=g_converter_input_stream_new(G_INPUT_STREAM(input),G_CONVERTER(decoder));g_object_unref(input);g_object_unref(decoder);
 GFileIOStream *io=NULL;GFile *file=g_file_new_tmp("insight-font-XXXXXX.ttf",&io,&error);gboolean ok=file!=NULL;gsize total=0;guchar buffer[16384];
 while(ok) { gssize count=g_input_stream_read(unpacked,buffer,sizeof buffer,NULL,&error);if(count<0) { ok=FALSE;break; }if(!count) break;total+=(gsize)count;if(total>16*1024*1024) { ok=FALSE;break; }ok=g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(io)),buffer,(gsize)count,NULL,NULL,&error); }
 if(io) { if(!g_io_stream_close(G_IO_STREAM(io),NULL,ok ? &error : NULL)) ok=FALSE;g_object_unref(io); }
 g_object_unref(unpacked);g_clear_error(&error);
 if(!ok || !total) { if(file) free_font_file(file);return NULL; }
 gchar *result=g_file_get_path(file);g_ptr_array_add(font_files,file);return result;
}
/* Fonts are process-private: no system installation or user font config edits. */
static void add_fonts(const gchar *root) {
 GDir *dir=g_dir_open(root,0,NULL);
 if(!dir) return;
 const gchar *name;guint count=0;
 while((name=g_dir_read_name(dir)) && count++<64) {
  gchar *folder=g_build_filename(root,name,NULL);GDir *fonts=g_dir_open(folder,0,NULL);const gchar *file;guint n=0;
  while(fonts && (file=g_dir_read_name(fonts)) && n++<32) if(g_str_has_suffix(file,".ttf") || g_str_has_suffix(file,".otf") || g_str_has_suffix(file,".ttf.gz") || g_str_has_suffix(file,".otf.gz")) {
   gchar *stored=g_build_filename(folder,file,NULL),*path=font_path(stored);g_free(stored);if(!path) continue;
#ifdef G_OS_WIN32
   typedef int (WINAPI *AddFont)(LPCWSTR,DWORD,PVOID);AddFont add=NULL;
   if(font_module && g_module_symbol(font_module,"AddFontResourceExW",(gpointer *)&add)) { gunichar2 *wide=g_utf8_to_utf16(path,-1,NULL,NULL,NULL);
 if(wide) add((LPCWSTR)wide,FR_PRIVATE,NULL);
 g_free(wide); }
#else
   typedef gpointer (*GetConfig)(void);typedef int (*AddFont)(gpointer,const guchar *);GetConfig current=NULL;AddFont add=NULL;
   if(font_module && g_module_symbol(font_module,"FcConfigGetCurrent",(gpointer *)&current) && g_module_symbol(font_module,"FcConfigAppFontAddFile",(gpointer *)&add)) add(current(),(const guchar *)path);
#endif
   g_free(path);
  }
  if(fonts) g_dir_close(fonts);
 g_free(folder);
 }
 g_dir_close(dir);
}
static void load_fonts(void) {
 font_files=g_ptr_array_new_with_free_func(free_font_file);
#ifdef G_OS_WIN32
 font_module=g_module_open("gdi32.dll",G_MODULE_BIND_LAZY);
#else
 font_module=g_module_open("libfontconfig.so.1",G_MODULE_BIND_LAZY);
#endif
 gchar *root=g_build_filename(app->datadir,"fonts",NULL);add_fonts(root);
 g_free(root);root=g_build_filename(app->configdir,"fonts",NULL);add_fonts(root);
 g_free(root);
 PangoFontMap *map=pango_cairo_font_map_get_default();
#ifndef G_OS_WIN32
 /* PangoFc keeps its own family cache in addition to the base font-map cache. */
 GModule *pango_fc=g_module_open("libpangoft2-1.0.so.0",G_MODULE_BIND_LAZY);
 if(pango_fc) { typedef void (*Changed)(gpointer);Changed changed=NULL;if(g_module_symbol(pango_fc,"pango_fc_font_map_config_changed",(gpointer *)&changed)) changed(map);g_module_close(pango_fc); }
#endif
 pango_font_map_changed(map);
 families=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);PangoFontFamily **list=NULL;gint count=0;pango_font_map_list_families(map,&list,&count);
 for(gint i=0;i<count;i++) g_hash_table_add(families,g_utf8_casefold(pango_font_family_get_name(list[i]),-1));
 g_free(list);
}
static gchar *choose_font(const gchar *value) {
 gchar **parts=g_strsplit(value,",",-1),*out=NULL;
 for(guint i=0;parts[i] && !out;i++) { gchar *name=g_strstrip(parts[i]);gsize len=strlen(name);
 if(len>=2 && name[0]=='"' && name[len-1]=='"') { name[len-1]=0;name++; }gchar *fold=g_utf8_casefold(name,-1);
  if(g_hash_table_contains(families,fold)) out=g_strdup(name);
  else if(!strcmp(name,"monospace")) out=g_strdup("Monospace");else if(!strcmp(name,"sans-serif")) out=g_strdup("Sans");
  g_free(fold);
 }
 g_strfreev(parts);
 return out;
}
static gboolean number(const gchar *text,const gchar *unit,double minimum,double maximum,double *value) {
 gchar *end=NULL;*value=g_ascii_strtod(text,&end);
 return end!=text && !strcmp(g_strstrip(end),unit) && *value>=minimum && *value<=maximum;
}
static gboolean property(InsightThemeStyle *style,const gchar *name,const gchar *value) {
 if(!strcmp(name,"font-family")) { gchar *font=choose_font(value);
 if(!font) return FALSE;
 g_free(style->font);style->font=font;style->mask|=IT_FONT; }
 else if(!strcmp(name,"color") || !strcmp(name,"background-color")) { GdkRGBA c;
 if(!gdk_rgba_parse(&c,value) || c.alpha!=1) return FALSE;
 gint color=(gint)(c.red*255+0.5)|((gint)(c.green*255+0.5)<<8)|((gint)(c.blue*255+0.5)<<16);
 if(!strcmp(name,"color")) { style->fore=color;style->mask|=IT_FORE; }else { style->back=color;style->mask|=IT_BACK; } }
 else if(!strcmp(name,"font-size")) { double size;
 if(number(value,"pt",4,96,&size)) { style->size=(gint)(size*100+0.5);style->mask=(style->mask & ~IT_SCALE)|IT_SIZE; }else if(number(value,"%",25,400,&size)) { style->scale=(gint)size;style->mask=(style->mask & ~IT_SIZE)|IT_SCALE; }else return FALSE; }
 else if(!strcmp(name,"font-weight")) { if(strcmp(value,"normal") && strcmp(value,"bold")) return FALSE;style->weight=!strcmp(value,"bold") ? 700 : 400;style->mask|=IT_WEIGHT; }
 else if(!strcmp(name,"font-style")) { if(strcmp(value,"normal") && strcmp(value,"italic")) return FALSE;style->italic=!strcmp(value,"italic");style->mask|=IT_ITALIC; }
 else if(!strcmp(name,"text-decoration-line")) { if(strcmp(value,"none") && strcmp(value,"underline")) return FALSE;style->underline=!strcmp(value,"underline");style->mask|=IT_UNDERLINE; }
 else return FALSE;
 return TRUE;
}
static gboolean selector_valid(const gchar *selector) {
 const gchar *names[]={"editor",".keyword",".comment",".string",".number",".operator",".type",".preprocessor",".line-number"};
 for(guint i=0;i<G_N_ELEMENTS(names);i++) if(!strcmp(selector,names[i])) return TRUE;
 if(!g_str_has_prefix(selector,".si-") || !selector[4]) return FALSE;
 for(const gchar *p=selector+4;*p;p++) if(!g_ascii_islower(*p) && !g_ascii_isdigit(*p) && *p!='-') return FALSE;
 return TRUE;
}
static GHashTable *parse_editor(const gchar *text,GError **error) {
 GRegex *comments=g_regex_new("/\\*.*?\\*/",G_REGEX_DOTALL,0,NULL);gchar *clean=g_regex_replace_literal(comments,text,-1,0,"",0,NULL);g_regex_unref(comments);gchar *cursor=clean;
 GHashTable *out=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,free_style);gboolean ok=TRUE;
 while(*g_strstrip(cursor)) {
  gchar *open=strchr(cursor,'{'),*close=open ? strchr(open+1,'}') : NULL;
 if(!close) { ok=FALSE;break; }*open=0;*close=0;gchar *selector=g_strstrip(cursor);
 if(!selector_valid(selector)) { ok=FALSE;break; }
  InsightThemeStyle *style=g_hash_table_lookup(out,selector);
 if(!style) { style=g_new0(InsightThemeStyle,1);g_hash_table_insert(out,g_strdup(selector),style); }
  gchar **props=g_strsplit(open+1,";",-1);
  for(guint i=0;props[i];i++) { gchar *entry=g_strstrip(props[i]);
 if(!*entry) continue;
 gchar *colon=strchr(entry,':');
 if(!colon) { ok=FALSE;break; }*colon=0;
 if(!property(style,g_strstrip(entry),g_strstrip(colon+1))) { ok=FALSE;break; } }
  g_strfreev(props);
 if(!ok) break;
 cursor=close+1;
 }
 g_free(clean);
 if(ok) return out;
 g_hash_table_unref(out);g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Invalid editor CSS selector or font/color property");
 return NULL;
}
static void merge(InsightThemeStyle *a,const InsightThemeStyle *b) {
 if(!b) return;
 if(b->mask&IT_FONT) a->font=b->font;
 if(b->mask&IT_SIZE) { a->size=b->size;a->mask &= ~IT_SCALE; }if(b->mask&IT_SCALE) { a->scale=b->scale;a->mask &= ~IT_SIZE; }
 if(b->mask&IT_WEIGHT) a->weight=b->weight;
 if(b->mask&IT_ITALIC) a->italic=b->italic;
 if(b->mask&IT_UNDERLINE) a->underline=b->underline;
 if(b->mask&IT_FORE) a->fore=b->fore;
 if(b->mask&IT_BACK) a->back=b->back;
 a->mask|=b->mask;
}
InsightThemeStyle insight_themes_style(ScintillaObject *sci,gint style) {
 InsightThemeStyle out={0};
 if(!rules) return out;
 merge(&out,g_hash_table_lookup(rules,"editor"));
 if(style==STYLE_LINENUMBER) { merge(&out,g_hash_table_lookup(rules,".line-number"));
 return out; }
 GeanyFiletype *ft=g_object_get_data(G_OBJECT(sci),"insight-theme-filetype");gint lexer=SEND(sci,SCI_GETLEXER,0,0);
 const gchar *categories[]={".keyword",".number",".operator",".type",".preprocessor",".comment",".string"};
 for(guint i=0;i<G_N_ELEMENTS(categories);i++) { const gchar *name=categories[i];gboolean match=(ft && highlighting_style_category(ft,style,name)) || (!strcmp(name,".comment") && highlighting_is_comment_style(lexer,style)) || (!strcmp(name,".string") && highlighting_is_string_style(lexer,style));
  if(match) merge(&out,g_hash_table_lookup(rules,name));
 }return out;
}
static void scan(const gchar *root) {
 GDir *dir=g_dir_open(root,0,NULL);
 if(!dir) return;
 const gchar *name;
 while(themes->len<128 && (name=g_dir_read_name(dir))) if(safe_name(name)) {
  gchar *folder=g_build_filename(root,name,NULL),*file=g_build_filename(folder,"editor.css",NULL);gboolean exists=g_file_test(file,G_FILE_TEST_IS_REGULAR);
 g_free(file);
  if(exists) { for(guint i=0;i<themes->len;i++) { Theme *old=g_ptr_array_index(themes,i);
 if(!strcmp(old->name,name)) { g_ptr_array_remove_index(themes,i);break; } }Theme *t=g_new0(Theme,1);t->name=g_strdup(name);t->folder=folder;g_ptr_array_add(themes,t); }else g_free(folder);
 }g_dir_close(dir);
}
static gint compare(gconstpointer a,gconstpointer b) { return g_utf8_collate((*(Theme *const *)a)->name,(*(Theme *const *)b)->name); }
static gboolean save(const gchar *name,GError **error) {
 GKeyFile *key=g_key_file_new();g_key_file_set_integer(key,"theme","version",1);g_key_file_set_string(key,"theme","name",name);gsize size=0;gchar *text=g_key_file_to_data(key,&size,NULL),*dir=g_path_get_dirname(config);gboolean ok=FALSE;
 if(g_mkdir_with_parents(dir,0700)) g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_FAILED,"Cannot create theme settings directory");else { GFile *file=g_file_new_for_path(config);ok=g_file_replace_contents(file,text,size,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,error);g_object_unref(file); }
 g_free(dir);
 g_free(text);g_key_file_unref(key);
 return ok;
}
static gboolean choose(const gchar *name,gboolean persist,GError **error) {
 GHashTable *next=NULL;GtkCssProvider *css=NULL;
 if(*name) {
  Theme *theme=NULL;for(guint i=0;i<themes->len;i++) { Theme *t=g_ptr_array_index(themes,i);
 if(!strcmp(t->name,name)) theme=t; }
  if(!theme) { g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_NOENT,"Theme not found");
 return FALSE; }
  gchar *path=g_build_filename(theme->folder,"editor.css",NULL),*editor=read_css(path,error);
 g_free(path);
 if(!editor) return FALSE;
  next=parse_editor(editor,error);
 g_free(editor);
 if(!next) return FALSE;
  path=g_build_filename(theme->folder,"panels.css",NULL);gchar *panels=read_css(path,error);
 g_free(path);
 if(!panels) { g_hash_table_unref(next);
 return FALSE; }
  css=gtk_css_provider_new();gboolean valid=gtk_css_provider_load_from_data(css,panels,-1,error);
 g_free(panels);
 if(!valid) { g_hash_table_unref(next);g_object_unref(css);
 return FALSE; }
 }
 if(persist && !save(name,error)) { if(next) g_hash_table_unref(next);
 g_clear_object(&css);
 return FALSE; }
 if(provider) gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(provider));
 g_clear_object(&provider);provider=css;
 g_clear_pointer(&rules,g_hash_table_unref);rules=next;
 g_free(selected);selected=g_strdup(name);
 if(provider) gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(provider),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION-1);
 insight_formatting_refresh();
 return TRUE;
}
void insight_themes_init(void) {
 if(initialized) return;
 initialized=TRUE;insight_appearance_init();load_fonts();themes=g_ptr_array_new_with_free_func(free_theme);config=g_build_filename(app->configdir,"insight","visual-theme.conf",NULL);selected=g_strdup("");
 gchar *root=g_build_filename(app->datadir,"styles",NULL);scan(root);
 g_free(root);root=g_build_filename(app->configdir,"styles",NULL);scan(root);
 g_free(root);g_ptr_array_sort(themes,compare);
 GKeyFile *key=g_key_file_new();
 if(g_key_file_load_from_file(key,config,G_KEY_FILE_NONE,NULL) && g_key_file_get_integer(key,"theme","version",NULL)==1) { gchar *name=g_key_file_get_string(key,"theme","name",NULL);
 if(name && (!*name || safe_name(name))) { GError *error=NULL;
 if(!choose(name,FALSE,&error)) g_warning("Visual theme ignored: %s",error ? error->message : "invalid theme");
 g_clear_error(&error); }g_free(name); }g_key_file_unref(key);
}
static void options(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;GtkWidget *dialog=gtk_dialog_new_with_buttons("Visual theme",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Apply",GTK_RESPONSE_APPLY,NULL),*box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)),*choice=gtk_combo_box_text_new(),*status=gtk_label_new("Themes use editable CSS in styles/<name>. Local panel appearance overrides the theme.");
 gtk_widget_set_name(dialog,"insight-theme-dialog");gtk_widget_set_name(choice,"insight-theme-choice");gtk_widget_set_name(status,"insight-theme-status");gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(choice),"","Current Geany theme");
 for(guint i=0;i<themes->len;i++) { Theme *t=g_ptr_array_index(themes,i);gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(choice),t->name,t->name); }
 gtk_combo_box_set_active_id(GTK_COMBO_BOX(choice),selected);gtk_label_set_line_wrap(GTK_LABEL(status),TRUE);gtk_box_pack_start(GTK_BOX(box),choice,FALSE,FALSE,8);gtk_box_pack_start(GTK_BOX(box),status,FALSE,FALSE,8);insight_i18n_apply(dialog);gtk_widget_show_all(dialog);
 while(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_APPLY) { const gchar *name=gtk_combo_box_get_active_id(GTK_COMBO_BOX(choice));GError *error=NULL;
 if(name && choose(name,TRUE,&error)) break;
 gtk_label_set_text(GTK_LABEL(status),error ? error->message : "Select a theme");
 g_clear_error(&error); }
 gtk_widget_destroy(dialog);
}
void insight_themes_menu(GtkWidget *menu) {
 insight_appearance_menu(menu);insight_appearance_menu(ui_lookup_widget(main_widgets.window,"menu_view1_menu"));
 gtk_style_context_add_class(gtk_widget_get_style_context(main_widgets.window),"insight-visual-theme");GtkWidget *item=gtk_menu_item_new_with_label("Visual theme…");gtk_widget_set_name(item,"insight-visual-theme");g_signal_connect(item,"activate",G_CALLBACK(options),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item);
}
void insight_themes_finalize(void) {
 if(!initialized) return;
 insight_appearance_finalize();
 if(provider) gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(provider));
 g_clear_object(&provider);
 g_clear_pointer(&rules,g_hash_table_unref);
 g_clear_pointer(&themes,g_ptr_array_unref);
 g_clear_pointer(&families,g_hash_table_unref);
 g_clear_pointer(&font_files,g_ptr_array_unref);
 g_clear_pointer(&selected,g_free);
 g_clear_pointer(&config,g_free);
 if(font_module) { g_module_close(font_module);font_module=NULL; }initialized=FALSE;
}
