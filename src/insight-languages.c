#define INSIGHT_LOCALIZE_FORMATS
#include "insight-i18n.h"
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-languages.h"
#include "insight-index.h"
#include "insight-workbench.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "geanyobject.h"
#include "ui_utils.h"
#include "utils.h"
#include "sciwrappers.h"
#include "highlighting.h"
#include "SciLexer.h"
#include <glib/gstdio.h>
#include <string.h>
#include <stdlib.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
typedef struct { gchar *name,*folder,*extensions,*compatible,*words,*css,*theme;GeanyFiletype *parser;gchar **files;gboolean seeded; } Language;
static GPtrArray *languages;static GHashTable *path_cache;static gchar *root;static gboolean applying;static gulong save_handler;
static void binding_saved(GObject *,GeanyDocument *,gpointer);
static void seed_languages(void);
static void free_language(gpointer data) { Language *p=data;g_free(p->name);g_free(p->folder);g_free(p->extensions);g_free(p->compatible);g_free(p->words);g_free(p->css);g_free(p->theme);g_strfreev(p->files);g_free(p); }
static gchar *get(GKeyFile *key,const gchar *field) { gchar *s=g_key_file_get_string(key,"language",field,NULL);return s ? s : g_strdup(""); }
static gboolean extensions_match(const gchar *list,const gchar *path) {
 if(!path) return FALSE;
 const gchar *base=strrchr(path,G_DIR_SEPARATOR);base=base ? base+1 : path;const gchar *ext=strrchr(base,'.');if(!ext) return FALSE;
 gchar **tokens=g_strsplit_set(list,";, \t\r\n",-1);gboolean match=FALSE;
 for(guint n=0;tokens[n];n++) { const gchar *t=tokens[n];while(*t=='*' || *t=='.') t++;if(*t && !strcmp(t,ext+1)) { match=TRUE;break; } }g_strfreev(tokens);return match;
}
static Language *by_name(const gchar *name) { for(guint n=0;languages && n<languages->len;n++) { Language *p=g_ptr_array_index(languages,n);if(!g_strcmp0(p->name,name)) return p; }return NULL; }
static gboolean binding_path_equal(const gchar *a,const gchar *b) {
#ifdef G_OS_WIN32
 gchar *ca=g_utf8_casefold(a,-1),*cb=g_utf8_casefold(b,-1);gboolean equal=!strcmp(ca,cb);g_free(ca);g_free(cb);return equal;
#else
 return !strcmp(a,b);
#endif
}
static Language *for_path(const gchar *path) {
 if(!path) return NULL;
 gchar *canonical=g_canonicalize_filename(path,NULL);Language *found=NULL;
 if(!path_cache) path_cache=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 gpointer cached=NULL;if(g_hash_table_lookup_extended(path_cache,canonical,NULL,&cached)) { g_free(canonical);return cached; }
 for(guint n=0;languages && n<languages->len && !found;n++) { Language *p=g_ptr_array_index(languages,n);for(guint k=0;p->files && p->files[k];k++) if(binding_path_equal(p->files[k],canonical)) { found=p;break; } }
 if(found) { g_hash_table_insert(path_cache,canonical,found);return found; }
 for(guint n=0;languages && n<languages->len;n++) { Language *p=g_ptr_array_index(languages,n);if(extensions_match(p->extensions,path)) { g_hash_table_insert(path_cache,canonical,p);return p; } }g_hash_table_insert(path_cache,canonical,NULL);return NULL;
}
static Language *for_editor(ScintillaObject *sci,const gchar *path,GeanyFiletype *ft) {
 const gchar *forced=g_object_get_data(G_OBJECT(sci),"insight-language-binding");Language *p=by_name(forced);if(p) return p;
 p=for_path(path);if(p) return p;
 if(!path) return by_name(ft ? ft->name : NULL);
 return NULL;
}
struct InsightLanguageFilter { gchar *extensions,*compatible;GPtrArray *own,*excluded;gboolean unrestricted; };
InsightLanguageFilter *insight_language_filter_new(const gchar *from,GeanyFiletype *ft) {
 InsightLanguageFilter *filter=g_new0(InsightLanguageFilter,1);Language *p=for_path(from);if(!p && !from) { GeanyDocument *doc=document_get_current();p=doc ? for_editor(doc->editor->sci,from,ft) : by_name(ft ? ft->name : NULL); }filter->unrestricted=!p;filter->own=g_ptr_array_new_with_free_func(g_free);filter->excluded=g_ptr_array_new_with_free_func(g_free);filter->extensions=g_strdup(p ? p->extensions : "");filter->compatible=g_strdup(p ? p->compatible : "");
 if(p) for(guint n=0;n<languages->len;n++) { Language *other=g_ptr_array_index(languages,n);GPtrArray *paths=other==p ? filter->own : filter->excluded;for(guint k=0;other->files && other->files[k];k++) g_ptr_array_add(paths,g_strdup(other->files[k])); }
 return filter;
}
gboolean insight_language_filter_match(const InsightLanguageFilter *filter,const gchar *path) {
 if(!filter || filter->unrestricted) return TRUE;
 if(!path) return FALSE;
 gchar *canonical=g_canonicalize_filename(path,NULL);gboolean own=FALSE,excluded=FALSE;
 for(guint n=0;n<filter->own->len;n++) if(binding_path_equal(g_ptr_array_index(filter->own,n),canonical)) own=TRUE;
 for(guint n=0;n<filter->excluded->len;n++) if(binding_path_equal(g_ptr_array_index(filter->excluded,n),canonical)) excluded=TRUE;
 g_free(canonical);
 return own || (!excluded && (extensions_match(filter->extensions,path) || extensions_match(filter->compatible,path)));
}
void insight_language_filter_free(InsightLanguageFilter *filter) { if(!filter) return;g_free(filter->extensions);g_free(filter->compatible);g_ptr_array_free(filter->own,TRUE);g_ptr_array_free(filter->excluded,TRUE);g_free(filter); }
GeanyFiletype *insight_languages_detect(const gchar *path) { Language *p=for_path(path);return p ? p->parser : NULL; }
gboolean insight_languages_compatible(const gchar *from,GeanyFiletype *fallback,const gchar *to) {
 Language *p=for_path(from);if(!p && !from) { GeanyDocument *current=document_get_current();p=current ? for_editor(current->editor->sci,NULL,fallback) : by_name(fallback ? fallback->name : NULL); }
 if(p) { Language *target=for_path(to);if(target && target!=p) { gchar *canonical=to ? g_canonicalize_filename(to,NULL) : NULL;for(guint n=0;canonical && target->files && target->files[n];n++) if(binding_path_equal(target->files[n],canonical)) { g_free(canonical);return FALSE; }g_free(canonical); }return target==p || extensions_match(p->extensions,to) || extensions_match(p->compatible,to); }
 GeanyDocument *d=to ? document_find_by_filename(to) : NULL;GeanyFiletype *ft=d ? d->file_type : (to ? filetypes_detect_from_extension(to) : NULL);
 return fallback && ft && insight_native_language_compatible(fallback->lang,ft->lang);
}
gboolean insight_languages_search_path(const gchar *from,GeanyFiletype *fallback,const gchar *to) {
 if(!for_path(from) && (from || !by_name(fallback ? fallback->name : NULL))) return TRUE;
 return insight_languages_compatible(from,fallback,to);
}
gboolean insight_languages_keyword(ScintillaObject *sci,const gchar *word) {
 const gchar *words=sci ? g_object_get_data(G_OBJECT(sci),"insight-language-keywords") : NULL;if(!words || !word) return FALSE;
 gsize size=strlen(word);while(*words) { while(g_ascii_isspace(*words)) words++;const gchar *begin=words;while(*words && !g_ascii_isspace(*words)) words++;if((gsize)(words-begin)==size && !memcmp(begin,word,size)) return TRUE; }return FALSE;
}
/* CSS is a deliberately limited, validated subset mapped to Scintilla styles. */
static gchar *strip_css_comments(const gchar *css) { GRegex *comments=g_regex_new("/\\*.*?\\*/",G_REGEX_DOTALL,0,NULL);gchar *clean=g_regex_replace_literal(comments,css,-1,0,"",0,NULL);g_regex_unref(comments);return clean; }
static gboolean css_valid(const gchar *css,GError **error) {
 GtkCssProvider *provider=gtk_css_provider_new();gboolean valid=gtk_css_provider_load_from_data(provider,css,-1,error);g_object_unref(provider);if(!valid) return FALSE;
 gchar *clean=strip_css_comments(css),*cursor=clean;
 while(*g_strstrip(cursor)) { gchar *open=strchr(cursor,'{');if(!open) { valid=FALSE;break; }*open=0;gchar *selector=g_strstrip(cursor),*close=strchr(open+1,'}');if(!close) { valid=FALSE;break; }*close=0;
  const gchar *selectors[]={"editor",".keyword",".comment",".string",".number",".operator",".type",".preprocessor"};gboolean supported=FALSE;for(guint n=0;n<G_N_ELEMENTS(selectors);n++) if(!strcmp(selector,selectors[n])) supported=TRUE;
  if(g_str_has_prefix(selector,".style-")) { gchar *end=NULL;long style=strtol(selector+7,&end,10);supported=end && !*end && end>selector+7 && style>=0 && style<=STYLE_MAX; }
  if(!supported) { valid=FALSE;break; }gchar **properties=g_strsplit(open+1,";",-1);
  for(guint n=0;properties[n];n++) { gchar *prop=g_strstrip(properties[n]);if(!*prop) continue;gchar *colon=strchr(prop,':');if(!colon) { valid=FALSE;break; }*colon=0;const gchar *names[]={"font-family","font-size","font-weight","font-style","color","background-color"};supported=FALSE;for(guint k=0;k<G_N_ELEMENTS(names);k++) if(!strcmp(g_strstrip(prop),names[k])) supported=TRUE;if(!supported) { valid=FALSE;break; }
   if(!strcmp(g_strstrip(prop),"font-size")) { gchar *end=NULL;double size=g_ascii_strtod(g_strstrip(colon+1),&end);if(size<4 || size>96 || strcmp(g_strstrip(end),"pt")) { valid=FALSE;break; } }
  }
  g_strfreev(properties);if(!valid) break;cursor=close+1;
 }g_free(clean);if(!valid) g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Unsupported CSS selector/property. Use editor, .keyword/.comment/.string/.number/.operator/.type/.preprocessor or .style-N; fonts and colors only; font-size must be 4–96pt.");return valid;
}
static gboolean category_style(const gchar *category,gint lexer,gint style) {
 if(!strcmp(category,"editor")) return TRUE;
 if(g_str_has_prefix(category,".style-")) { gchar *end=NULL;long id=strtol(category+7,&end,10);return end && !*end && id>=0 && id<=STYLE_MAX && style==id; }
 if(!strcmp(category,".comment")) return highlighting_is_comment_style(lexer,style);
 if(!strcmp(category,".string")) return highlighting_is_string_style(lexer,style);
 if(lexer==SCLEX_CPP) { if(!strcmp(category,".keyword")) return style==SCE_C_WORD;if(!strcmp(category,".type")) return style==SCE_C_WORD2;if(!strcmp(category,".number")) return style==SCE_C_NUMBER;if(!strcmp(category,".operator")) return style==SCE_C_OPERATOR;if(!strcmp(category,".preprocessor")) return style==SCE_C_PREPROCESSOR; }
 if(lexer==SCLEX_PYTHON) { if(!strcmp(category,".keyword")) return style==SCE_P_WORD;if(!strcmp(category,".number")) return style==SCE_P_NUMBER;if(!strcmp(category,".operator")) return style==SCE_P_OPERATOR; }
 if(lexer==SCLEX_BASH) { if(!strcmp(category,".keyword")) return style==SCE_SH_WORD;if(!strcmp(category,".number")) return style==SCE_SH_NUMBER;if(!strcmp(category,".operator")) return style==SCE_SH_OPERATOR; }
 if(lexer==SCLEX_VHDL) { if(!strcmp(category,".keyword")) return style==SCE_VHDL_KEYWORD;if(!strcmp(category,".number")) return style==SCE_VHDL_NUMBER;if(!strcmp(category,".operator")) return style==SCE_VHDL_OPERATOR; }
 if(lexer==SCLEX_ASM) { if(!strcmp(category,".keyword")) return style==SCE_ASM_CPUINSTRUCTION || style==SCE_ASM_MATHINSTRUCTION;if(!strcmp(category,".number")) return style==SCE_ASM_NUMBER;if(!strcmp(category,".operator")) return style==SCE_ASM_OPERATOR; }return FALSE;
}
static void css_property(ScintillaObject *sci,gint style,const gchar *name,gchar *value) {
 g_strstrip(value);gsize length=strlen(value);if(length>1 && ((value[0]=='"' && value[length-1]=='"') || (value[0]=='\'' && value[length-1]=='\''))) { value[length-1]=0;value++; }
 if(!strcmp(name,"font-family")) SEND(sci,SCI_STYLESETFONT,style,(sptr_t)value);
 else if(!strcmp(name,"font-size")) { double size=g_ascii_strtod(value,NULL);if(size>=4 && size<=96) SEND(sci,SCI_STYLESETSIZEFRACTIONAL,style,(sptr_t)(size*100)); }
 else if(!strcmp(name,"font-weight")) SEND(sci,SCI_STYLESETBOLD,style,!strcmp(value,"bold") || atoi(value)>=600);
 else if(!strcmp(name,"font-style")) SEND(sci,SCI_STYLESETITALIC,style,!strcmp(value,"italic") || !strcmp(value,"oblique"));
 else if(!strcmp(name,"color") || !strcmp(name,"background-color")) { GdkRGBA color;if(gdk_rgba_parse(&color,value)) SEND(sci,!strcmp(name,"color") ? SCI_STYLESETFORE : SCI_STYLESETBACK,style,(gint)(color.red*255) | ((gint)(color.green*255)<<8) | ((gint)(color.blue*255)<<16)); }
}
void insight_languages_apply(ScintillaObject *sci,const gchar *path,GeanyFiletype *ft) {
 if(!languages || !sci || applying) return;
 Language *p=for_editor(sci,path,ft);g_object_set_data_full(G_OBJECT(sci),"insight-language-keywords",p && *p->words ? g_strdup(p->words) : NULL,g_free);if(!p) return;
 if(*p->words) SEND(sci,SCI_SETKEYWORDS,0,(sptr_t)p->words);
 gchar *css=strip_css_comments(p->css),*cursor=css;gint lexer=SEND(sci,SCI_GETLEXER,0,0);
 while(*cursor) { gchar *open=strchr(cursor,'{');if(!open) break;*open=0;gchar *close=strchr(open+1,'}');if(!close) break;*close=0;gchar *selector=g_strstrip(cursor);gchar **properties=g_strsplit(open+1,";",-1);
  for(gint style=0;style<=STYLE_MAX;style++) if(category_style(selector,lexer,style) || highlighting_style_category(ft,style,selector)) for(guint n=0;properties[n];n++) { gchar *colon=strchr(properties[n],':');if(!colon) continue;gchar *name=g_strndup(properties[n],colon-properties[n]),*value=g_strdup(colon+1);css_property(sci,style,g_strstrip(name),value);g_free(name);g_free(value); }
  g_strfreev(properties);cursor=close+1;
 }g_free(css);
}
static gint compare_languages(gconstpointer a,gconstpointer b) { const Language *pa=*(Language * const *)a,*pb=*(Language * const *)b;if(pa->seeded!=pb->seeded) return pa->seeded ? 1 : -1;return strcmp(pa->name,pb->name); }
static void load_languages(void) {
 if(!root) return;
 g_clear_pointer(&path_cache,g_hash_table_destroy);
 if(languages) g_ptr_array_free(languages,TRUE);
 languages=g_ptr_array_new_with_free_func(free_language);GDir *dir=g_dir_open(root,0,NULL);if(!dir) return;const gchar *name;
 while((name=g_dir_read_name(dir))) { gchar *folder=g_build_filename(root,name,NULL),*config=g_build_filename(folder,"language.conf",NULL),*style=g_build_filename(folder,"style.css",NULL);GKeyFile *key=g_key_file_new();
  if(g_key_file_load_from_file(key,config,G_KEY_FILE_NONE,NULL)) { Language *p=g_new0(Language,1);p->folder=g_strdup(folder);p->name=get(key,"name");gchar *parser=get(key,"parser");p->parser=filetypes_lookup_by_name(parser);g_free(parser);p->extensions=get(key,"extensions");p->compatible=get(key,"compatible-extensions");p->words=get(key,"keywords");p->theme=get(key,"theme");p->seeded=g_key_file_get_boolean(key,"language","seeded",NULL);p->files=g_key_file_get_string_list(key,"language","files",NULL,NULL);if(!g_file_get_contents(style,&p->css,NULL,NULL)) p->css=g_strdup("");GError *error=NULL;
   if(p->parser && *p->name && css_valid(p->css,&error)) g_ptr_array_add(languages,p);else { g_warning("Language profile %s ignored: invalid parser, name or CSS",name);free_language(p); }g_clear_error(&error);
  }g_key_file_free(key);g_free(folder);g_free(config);g_free(style);
 }g_dir_close(dir);g_ptr_array_sort(languages,compare_languages);
}
void insight_languages_init(void) { if(!root) root=g_build_filename(app->configdir,"languages",NULL);load_languages();seed_languages();load_languages();if(!save_handler) save_handler=g_signal_connect(geany_object,"document-save",G_CALLBACK(binding_saved),NULL); }
void insight_languages_finalize(void) { if(save_handler) { g_signal_handler_disconnect(geany_object,save_handler);save_handler=0; }g_clear_pointer(&path_cache,g_hash_table_destroy);g_clear_pointer(&languages,g_ptr_array_unref);g_clear_pointer(&root,g_free); }
typedef struct { GtkWidget *dialog,*choice,*name,*parser,*extensions,*compatible,*words,*css,*theme,*bind,*category,*font,*bold,*italic,*color,*status;guint document;gchar *original;gboolean filling; } LanguageDialog;
static GtkWidget *field(GtkGrid *grid,gint row,const gchar *label,const gchar *name) { GtkWidget *entry=gtk_entry_new(),*title=gtk_label_new(label);gtk_widget_set_halign(title,GTK_ALIGN_START);gtk_widget_set_name(entry,name);gtk_grid_attach(grid,title,0,row,1,1);gtk_grid_attach(grid,entry,1,row,1,1);gtk_widget_set_hexpand(entry,TRUE);return entry; }
static gchar *text_value(GtkWidget *view) { GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));GtkTextIter a,b;gtk_text_buffer_get_bounds(buffer,&a,&b);return gtk_text_buffer_get_text(buffer,&a,&b,FALSE); }
static GtkWidget *text_editor(GtkWidget *box,const gchar *name) { GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL),*view=gtk_text_view_new();gtk_widget_set_name(view,name);gtk_text_view_set_monospace(GTK_TEXT_VIEW(view),TRUE);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_widget_set_size_request(scroll,-1,180);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);return view; }
static gchar *default_extensions(GeanyFiletype *ft) { GString *out=g_string_new(NULL);for(guint n=0;ft && ft->pattern && ft->pattern[n];n++) { const gchar *pattern=ft->pattern[n];if(g_str_has_prefix(pattern,"*.") && !strpbrk(pattern+2,"*?[]")) { if(out->len) g_string_append_c(out,';');g_string_append(out,pattern+2); } }return g_string_free(out,FALSE); }
static void choice_changed(GtkComboBox *choice,gpointer data) {
 LanguageDialog *d=data;if(d->filling || insight_i18n_is_applying()) return;const gchar *name=gtk_combo_box_get_active_id(choice);if(!name) return;Language *p=by_name(name);GeanyFiletype *ft=p ? p->parser : filetypes_lookup_by_name(name);if(!ft) return;d->filling=TRUE;g_free(d->original);d->original=p ? g_strdup(p->name) : NULL;
 gtk_entry_set_text(GTK_ENTRY(d->name),p ? p->name : ft->name);gtk_combo_box_set_active_id(GTK_COMBO_BOX(d->parser),ft->name);gchar *extensions=p ? g_strdup(p->extensions) : default_extensions(ft);gtk_entry_set_text(GTK_ENTRY(d->extensions),extensions);g_free(extensions);
 gtk_entry_set_text(GTK_ENTRY(d->compatible),p ? p->compatible : ((ft->lang==TM_PARSER_C || ft->lang==TM_PARSER_CPP || ft->lang==TM_PARSER_CUDA) ? "c;c++;C;cpp;cxx;cc;h;hpp;hxx;h++;hh;asm;ASM;s;S;sx" : ""));
 gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->words)),p ? p->words : "",-1);gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->css)),p ? p->css : "",-1);gtk_combo_box_set_active_id(GTK_COMBO_BOX(d->theme),p && *p->theme ? p->theme : "current");d->filling=FALSE;
}
static void refresh_choices(LanguageDialog *d,const gchar *selected) {
 gchar *name=g_strdup(selected);d->filling=TRUE;gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(d->choice));for(guint n=0;n<filetypes_array->len;n++) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->choice),filetypes[n]->name,filetypes[n]->name);
 for(guint n=0;languages && n<languages->len;n++) { Language *p=g_ptr_array_index(languages,n);if(!filetypes_lookup_by_name(p->name)) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->choice),p->name,p->name); }gtk_combo_box_set_active_id(GTK_COMBO_BOX(d->choice),name);d->filling=FALSE;choice_changed(GTK_COMBO_BOX(d->choice),d);g_free(name);
}
static void theme_changed(GtkComboBox *combo,gpointer data) {
 LanguageDialog *d=data;if(d->filling || insight_i18n_is_applying()) return;const gchar *id=gtk_combo_box_get_active_id(combo);const gchar *css=!g_strcmp0(id,"dark") ? "editor { color: #d8dee9; background-color: #242933; }\n.keyword { color: #81a1c1; font-weight: bold; }\n.comment { color: #8f9bad; font-style: italic; }\n.string { color: #a3be8c; }\n.number { color: #b48ead; }\n" : (!g_strcmp0(id,"light") ? "editor { color: #202020; background-color: #ffffff; }\n.keyword { color: #004da8; font-weight: bold; }\n.comment { color: #68756b; font-style: italic; }\n.string { color: #8e421c; }\n" : "");gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->css)),css,-1);
}
static gchar *font_family_escape(const gchar *family) { GString *out=g_string_new(NULL);for(const gchar *p=family;*p;p++) { if(*p=='\\' || *p=='"') g_string_append_c(out,'\\');g_string_append_c(out,*p); }return g_string_free(out,FALSE); }
static void append_font(GtkButton *button,gpointer data) {
 (void)button;LanguageDialog *d=data;const gchar *category=gtk_combo_box_get_active_id(GTK_COMBO_BOX(d->category));gchar *font=gtk_font_chooser_get_font(GTK_FONT_CHOOSER(d->font));PangoFontDescription *desc=pango_font_description_from_string(font);GdkRGBA color;gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(d->color),&color);const gchar *family=pango_font_description_get_family(desc);gchar *escaped=font_family_escape(family ? family : "Monospace");
 gchar size[G_ASCII_DTOSTR_BUF_SIZE];g_ascii_dtostr(size,sizeof size,pango_font_description_get_size(desc)/(double)PANGO_SCALE);
 gchar *rule=g_strdup_printf("\n%s { font-family: \"%s\"; font-size: %spt; font-weight: %s; font-style: %s; color: #%02x%02x%02x; }\n",category,escaped,size,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->bold)) ? "bold" : "normal",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->italic)) ? "italic" : "normal",(guint)(color.red*255),(guint)(color.green*255),(guint)(color.blue*255));GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->css));GtkTextIter end;gtk_text_buffer_get_end_iter(buffer,&end);gtk_text_buffer_insert(buffer,&end,rule,-1);g_free(rule);g_free(escaped);g_free(font);pango_font_description_free(desc);
}
static gboolean write_language(Language *p,GError **error) {
 if(g_mkdir_with_parents(p->folder,0700)) { g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_FAILED,"Cannot create language folder");return FALSE; }GKeyFile *key=g_key_file_new();g_key_file_set_string(key,"language","name",p->name);g_key_file_set_string(key,"language","parser",p->parser->name);g_key_file_set_string(key,"language","extensions",p->extensions);g_key_file_set_string(key,"language","compatible-extensions",p->compatible);g_key_file_set_string(key,"language","keywords",p->words);g_key_file_set_string(key,"language","theme",p->theme);g_key_file_set_boolean(key,"language","seeded",p->seeded);if(p->files) g_key_file_set_string_list(key,"language","files",(const gchar * const *)p->files,g_strv_length(p->files));gsize size;gchar *contents=g_key_file_to_data(key,&size,NULL),*config=g_build_filename(p->folder,"language.conf",NULL),*style=g_build_filename(p->folder,"style.css",NULL);gboolean ok=g_file_set_contents(style,p->css,-1,error) && g_file_set_contents(config,contents,size,error);g_free(contents);g_free(config);g_free(style);g_key_file_free(key);return ok;
}
static void seed_languages(void) {
 /* Seed absent folders once. Existing profiles and hand-written CSS win. */
 ScintillaObject *sci=SCINTILLA(scintilla_new());g_object_ref_sink(sci);
 for(guint n=0;n<filetypes_array->len;n++) { GeanyFiletype *ft=filetypes[n];if(ft->lang<0) continue;gchar *folder=g_build_filename(root,ft->name,NULL),*config=g_build_filename(folder,"language.conf",NULL);if(g_file_test(config,G_FILE_TEST_EXISTS)) { g_free(folder);g_free(config);continue; }
  Language p={0};p.name=ft->name;p.folder=folder;p.parser=ft;p.extensions=default_extensions(ft);p.compatible=insight_native_language_compatible(TM_PARSER_C,ft->lang) ? "c;c++;C;cpp;cxx;cc;h;hpp;hxx;h++;hh;asm;ASM;s;S;sx" : "";p.theme="current";p.seeded=TRUE;
  highlighting_set_styles(sci,ft);p.words=(gchar *)highlighting_primary_keywords(ft);PangoFontDescription *font=pango_font_description_from_string(interface_prefs.editor_font ? interface_prefs.editor_font : "Monospace 12");const gchar *family=pango_font_description_get_family(font);gchar *escaped=font_family_escape(family ? family : "Monospace");gchar size[G_ASCII_DTOSTR_BUF_SIZE];g_ascii_dtostr(size,sizeof size,pango_font_description_get_size(font)/(double)PANGO_SCALE);GString *css=g_string_new(NULL);g_string_append_printf(css,"editor { font-family: \"%s\"; font-size: %spt; }\n",escaped,size);g_free(escaped);pango_font_description_free(font);
  for(gint style=0;style<=STYLE_MAX;style++) { gint fore=SEND(sci,SCI_STYLEGETFORE,style,0),back=SEND(sci,SCI_STYLEGETBACK,style,0);g_string_append_printf(css,".style-%d { color: #%02x%02x%02x; background-color: #%02x%02x%02x; font-weight: %s; font-style: %s; }\n",style,fore&255,(fore>>8)&255,(fore>>16)&255,back&255,(back>>8)&255,(back>>16)&255,SEND(sci,SCI_STYLEGETBOLD,style,0) ? "bold" : "normal",SEND(sci,SCI_STYLEGETITALIC,style,0) ? "italic" : "normal"); }
  p.css=css->str;GError *error=NULL;if(!write_language(&p,&error)) g_warning("Cannot seed language %s: %s",ft->name,error ? error->message : "write failed");g_clear_error(&error);g_string_free(css,TRUE);g_free(p.extensions);g_free(folder);g_free(config);
 }gtk_widget_destroy(GTK_WIDGET(sci));g_object_unref(sci);
}
static void binding_saved(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object;(void)data;if(!doc || !doc->is_valid || !doc->file_name) return;Language *p=by_name(g_object_get_data(G_OBJECT(doc->editor->sci),"insight-language-binding"));if(!p) return;
 g_clear_pointer(&path_cache,g_hash_table_destroy);gchar *path=g_canonicalize_filename(doc->file_name,NULL);for(guint n=0;p->files && p->files[n];n++) if(binding_path_equal(p->files[n],path)) { g_free(path);return; }
 GPtrArray *files=g_ptr_array_new_with_free_func(g_free);for(guint n=0;p->files && p->files[n];n++) g_ptr_array_add(files,g_strdup(p->files[n]));g_ptr_array_add(files,path);g_ptr_array_add(files,NULL);g_strfreev(p->files);p->files=(gchar **)g_ptr_array_free(files,FALSE);GError *error=NULL;if(!write_language(p,&error)) ui_set_statusbar(TRUE,"Cannot save language binding: %s",error ? error->message : "write failed");g_clear_error(&error);
}
static gboolean reset_binding(GeanyDocument *doc,GError **error) {
 if(!doc) return TRUE;
 g_clear_pointer(&path_cache,g_hash_table_destroy);
 gchar *path=doc->file_name ? g_canonicalize_filename(doc->file_name,NULL) : NULL;
 for(guint n=0;path && n<languages->len;n++) { Language *p=g_ptr_array_index(languages,n);GPtrArray *files=g_ptr_array_new_with_free_func(g_free);gboolean changed=FALSE;for(guint k=0;p->files && p->files[k];k++) { if(binding_path_equal(p->files[k],path)) changed=TRUE;else g_ptr_array_add(files,g_strdup(p->files[k])); }g_ptr_array_add(files,NULL);if(changed) { g_strfreev(p->files);p->files=(gchar **)g_ptr_array_free(files,FALSE);if(!write_language(p,error)) { g_free(path);return FALSE; } }else g_ptr_array_free(files,TRUE); }
 g_free(path);g_object_set_data(G_OBJECT(doc->editor->sci),"insight-language-binding",NULL);document_set_filetype(doc,doc->file_name ? filetypes_detect_from_file(doc->file_name) : filetypes[GEANY_FILETYPES_NONE]);document_reload_config(doc);insight_workbench_languages_changed();return TRUE;
}
static gboolean valid_name(const gchar *name) { return name && *name && strcmp(name,".") && strcmp(name,"..") && !strpbrk(name,"/\\:\n\r\t*?\"<>|") && name[strlen(name)-1]!='.' && name[strlen(name)-1]!=' '; }
static gboolean save_dialog(LanguageDialog *d,GError **error) {
 Language p={0};p.name=g_strdup(gtk_entry_get_text(GTK_ENTRY(d->name)));g_strstrip(p.name);p.parser=filetypes_lookup_by_name(gtk_combo_box_get_active_id(GTK_COMBO_BOX(d->parser)));p.extensions=g_strdup(gtk_entry_get_text(GTK_ENTRY(d->extensions)));p.compatible=g_strdup(gtk_entry_get_text(GTK_ENTRY(d->compatible)));p.words=text_value(d->words);p.css=text_value(d->css);p.theme=g_strdup(gtk_combo_box_get_active_id(GTK_COMBO_BOX(d->theme)));
 if(!valid_name(p.name) || !p.parser) { g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Enter a safe language name and select a parser");goto fail; }if(!css_valid(p.css,error)) goto fail;
 Language *old=by_name(d->original),*collision=by_name(p.name);if(collision && collision!=old) { g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_EXIST,"Another profile already has this name");goto fail; }
 gchar **extensions=g_strsplit_set(p.extensions,";, \t\r\n",-1);
 for(guint n=0;languages && n<languages->len;n++) { Language *other=g_ptr_array_index(languages,n);if(other==old || other->seeded) continue;for(guint k=0;extensions[k];k++) { const gchar *ext=extensions[k];while(*ext=='*' || *ext=='.') ext++;if(!*ext) continue;gchar *sample=g_strconcat("file.",ext,NULL);gboolean overlap=extensions_match(other->extensions,sample);g_free(sample);if(overlap) { g_set_error(error,G_FILE_ERROR,G_FILE_ERROR_EXIST,"Extension %s already belongs to profile %s",ext,other->name);g_strfreev(extensions);goto fail; } } }g_strfreev(extensions);
 p.folder=g_build_filename(root,p.name,NULL);if(old) p.files=g_strdupv(old->files);GeanyDocument *doc=document_find_by_id(d->document);gboolean bind=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->bind));gchar *path=bind && doc && doc->file_name ? g_canonicalize_filename(doc->file_name,NULL) : NULL;
 if(path) { GPtrArray *files=g_ptr_array_new_with_free_func(g_free);for(guint n=0;p.files && p.files[n];n++) if(!binding_path_equal(p.files[n],path)) g_ptr_array_add(files,g_strdup(p.files[n]));g_ptr_array_add(files,g_strdup(path));g_ptr_array_add(files,NULL);g_strfreev(p.files);p.files=(gchar **)g_ptr_array_free(files,FALSE); }
 if(old && strcmp(old->folder,p.folder) && g_rename(old->folder,p.folder)) { g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_FAILED,"Cannot rename profile folder (destination may exist)");g_free(path);goto fail; }
 if(!write_language(&p,error)) { if(old && strcmp(old->folder,p.folder)) g_rename(p.folder,old->folder);g_free(path);goto fail; }
 if(path) for(guint n=0;n<languages->len;n++) { Language *other=g_ptr_array_index(languages,n);if(other==old || other==collision) continue;gboolean owns=FALSE;for(guint k=0;other->files && other->files[k];k++) if(binding_path_equal(other->files[k],path)) owns=TRUE;if(!owns) continue;GPtrArray *files=g_ptr_array_new_with_free_func(g_free);for(guint k=0;other->files && other->files[k];k++) if(!binding_path_equal(other->files[k],path)) g_ptr_array_add(files,g_strdup(other->files[k]));g_ptr_array_add(files,NULL);g_strfreev(other->files);other->files=(gchar **)g_ptr_array_free(files,FALSE);if(!write_language(other,error)) { g_free(path);goto fail; } }
 g_free(path);
 if(doc && bind) g_object_set_data_full(G_OBJECT(doc->editor->sci),"insight-language-binding",g_strdup(p.name),g_free);
 if(old && strcmp(old->name,p.name)) for(guint n=0;n<documents_array->len;n++) { GeanyDocument *buffer=g_ptr_array_index(documents_array,n);if(buffer->is_valid && !g_strcmp0(g_object_get_data(G_OBJECT(buffer->editor->sci),"insight-language-binding"),old->name)) g_object_set_data_full(G_OBJECT(buffer->editor->sci),"insight-language-binding",g_strdup(p.name),g_free); }
 load_languages();applying=TRUE;for(guint n=0;n<documents_array->len;n++) { GeanyDocument *buffer=g_ptr_array_index(documents_array,n);if(!buffer->is_valid) continue;Language *profile=for_editor(buffer->editor->sci,buffer->file_name,buffer->file_type);if(profile) document_set_filetype(buffer,profile->parser);document_reload_config(buffer); }applying=FALSE;
 for(guint n=0;n<documents_array->len;n++) { GeanyDocument *buffer=g_ptr_array_index(documents_array,n);if(buffer->is_valid) insight_languages_apply(buffer->editor->sci,buffer->file_name,buffer->file_type); }insight_workbench_languages_changed();
 g_free(d->original);d->original=g_strdup(p.name);refresh_choices(d,p.name);g_free(p.name);g_free(p.folder);g_free(p.extensions);g_free(p.compatible);g_free(p.words);g_free(p.css);g_free(p.theme);g_strfreev(p.files);return TRUE;
fail: g_free(p.name);g_free(p.folder);g_free(p.extensions);g_free(p.compatible);g_free(p.words);g_free(p.css);g_free(p.theme);g_strfreev(p.files);return FALSE;
}
void insight_languages_show(GtkWidget *widget,gpointer data) {
 (void)widget;LanguageDialog d={0};GeanyDocument *doc=document_get_current();d.document=doc ? doc->id : 0;d.dialog=gtk_dialog_new_with_buttons("Language profiles and file binding",GTK_WINDOW(main_widgets.window),GTK_DIALOG_DESTROY_WITH_PARENT,"Reset file binding",1001,"Reload profiles",1002,"Close",GTK_RESPONSE_CLOSE,"Apply",GTK_RESPONSE_APPLY,NULL);gtk_widget_set_name(d.dialog,"insight-language-dialog");gtk_window_set_default_size(GTK_WINDOW(d.dialog),760,650);GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d.dialog)),*grid=gtk_grid_new();gtk_grid_set_row_spacing(GTK_GRID(grid),7);gtk_grid_set_column_spacing(GTK_GRID(grid),10);gtk_container_set_border_width(GTK_CONTAINER(grid),12);gtk_box_pack_start(GTK_BOX(box),grid,FALSE,FALSE,0);
 d.choice=gtk_combo_box_text_new();gtk_widget_set_name(d.choice,"insight-language-profile");d.parser=gtk_combo_box_text_new();gtk_widget_set_name(d.parser,"insight-language-parser");for(guint n=0;n<filetypes_array->len;n++) { GeanyFiletype *ft=filetypes[n];gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d.choice),ft->name,ft->name);gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d.parser),ft->name,ft->name); }for(guint n=0;languages && n<languages->len;n++) { Language *p=g_ptr_array_index(languages,n);if(!filetypes_lookup_by_name(p->name)) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d.choice),p->name,p->name); }
 gtk_grid_attach(GTK_GRID(grid),gtk_label_new("Profile"),0,0,1,1);gtk_grid_attach(GTK_GRID(grid),d.choice,1,0,1,1);d.name=field(GTK_GRID(grid),1,"Language name","insight-language-name");gtk_grid_attach(GTK_GRID(grid),gtk_label_new("Base parser / lexer"),0,2,1,1);gtk_grid_attach(GTK_GRID(grid),d.parser,1,2,1,1);d.extensions=field(GTK_GRID(grid),3,"Own extensions (case sensitive)","insight-language-extensions");d.compatible=field(GTK_GRID(grid),4,"Compatible extensions","insight-language-compatible");gtk_widget_set_tooltip_text(d.extensions,"Separate extensions with semicolons: c;C;cpp;cxx;h;hpp;hxx;h++");d.bind=gtk_check_button_new_with_label("Force this profile for the current file (persist for named files)");gtk_widget_set_name(d.bind,"insight-language-force");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d.bind),data!=NULL);gtk_widget_set_sensitive(d.bind,doc!=NULL);gtk_grid_attach(GTK_GRID(grid),d.bind,0,5,2,1);
 GtkWidget *notebook=gtk_notebook_new(),*words_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6),*appearance=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_box_pack_start(GTK_BOX(box),notebook,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(words_box),gtk_label_new("Primary keyword override (empty keeps Geany defaults)"),FALSE,FALSE,0);d.words=text_editor(words_box,"insight-language-keywords");gtk_notebook_append_page(GTK_NOTEBOOK(notebook),words_box,gtk_label_new("Predefined words"));
 d.theme=gtk_combo_box_text_new();gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d.theme),"current","Current Geany theme + CSS overrides");gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d.theme),"light","Light preset");gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d.theme),"dark","Dark preset");gtk_box_pack_start(GTK_BOX(appearance),d.theme,FALSE,FALSE,0);
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);d.category=gtk_combo_box_text_new();const gchar *categories[]={"editor",".keyword",".comment",".string",".number",".operator",".type",".preprocessor"};for(guint n=0;n<G_N_ELEMENTS(categories);n++) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d.category),categories[n],categories[n]);gtk_combo_box_set_active(GTK_COMBO_BOX(d.category),0);d.font=gtk_font_button_new_with_font("Monospace 12");d.bold=gtk_check_button_new_with_label("Bold");d.italic=gtk_check_button_new_with_label("Italic");d.color=gtk_color_button_new();GtkWidget *add=gtk_button_new_with_label("Add CSS rule");GtkWidget *controls[]={d.category,d.font,d.bold,d.italic,d.color,add};for(guint n=0;n<G_N_ELEMENTS(controls);n++) gtk_box_pack_start(GTK_BOX(row),controls[n],FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(appearance),row,FALSE,FALSE,0);d.css=text_editor(appearance,"insight-language-css");gtk_notebook_append_page(GTK_NOTEBOOK(notebook),appearance,gtk_label_new("Fonts and CSS"));d.status=gtk_label_new("Changes apply to open documents and the workspace index.");gtk_label_set_line_wrap(GTK_LABEL(d.status),TRUE);gtk_box_pack_start(GTK_BOX(box),d.status,FALSE,FALSE,6);
 g_signal_connect(d.choice,"changed",G_CALLBACK(choice_changed),&d);g_signal_connect(d.theme,"changed",G_CALLBACK(theme_changed),&d);g_signal_connect(add,"clicked",G_CALLBACK(append_font),&d);Language *current=doc ? for_editor(doc->editor->sci,doc->file_name,doc->file_type) : NULL;gtk_combo_box_set_active_id(GTK_COMBO_BOX(d.choice),current ? current->name : (doc ? doc->file_type->name : "C++"));gtk_widget_show_all(d.dialog);
 gint response;while((response=gtk_dialog_run(GTK_DIALOG(d.dialog)))==GTK_RESPONSE_APPLY || response==1001 || response==1002) {
  GError *error=NULL;gboolean ok=FALSE;if(response==GTK_RESPONSE_APPLY) ok=save_dialog(&d,&error);else if(response==1001) ok=reset_binding(document_find_by_id(d.document),&error);else { load_languages();for(guint n=0;n<documents_array->len;n++) { GeanyDocument *buffer=g_ptr_array_index(documents_array,n);if(buffer->is_valid) { Language *p=for_editor(buffer->editor->sci,buffer->file_name,buffer->file_type);if(p) document_set_filetype(buffer,p->parser);document_reload_config(buffer); } }GeanyDocument *active=document_find_by_id(d.document);Language *profile=active ? for_editor(active->editor->sci,active->file_name,active->file_type) : NULL;refresh_choices(&d,profile ? profile->name : (active ? active->file_type->name : "C++"));insight_workbench_languages_changed();ok=TRUE; }
  gtk_label_set_text(GTK_LABEL(d.status),ok ? "Applied. Profiles reloaded; workspace reindex requested." : (error ? error->message : "Cannot apply settings"));g_clear_error(&error);
 }gtk_widget_destroy(d.dialog);g_free(d.original);
}
