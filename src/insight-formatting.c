#define INSIGHT_LOCALIZE_FORMATS
#include "insight-i18n.h"
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-formatting.h"
#include "insight-themes.h"
#include "insight-help.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
typedef struct { gchar *font;gint size,weight,italic,underline,letter_case,fore,back; } Style;
typedef struct { ScintillaObject *sci;Style styles[STYLE_MAX+1];gboolean valid; } Record;
typedef struct { gboolean enabled,color_only,monochrome;gchar *font; } Settings;
static Settings settings;static gboolean initialized,mono;static gchar *config;static GHashTable *records;
static gboolean syntax_style(gint style) { return style<=STYLE_DEFAULT || style>STYLE_LASTPREDEFINED; }
static void raw_clear(Record *r) { for(gint i=0;i<=STYLE_MAX;i++) g_clear_pointer(&r->styles[i].font,g_free);r->valid=FALSE; }
static void freed(gpointer data) { Record *r=data;raw_clear(r);g_free(r); }
static void destroyed(gpointer data,GObject *object) { (void)data;if(records) g_hash_table_remove(records,object); }
static void widget_destroyed(GtkWidget *widget,gpointer data) {
 (void)data;if(records && g_hash_table_contains(records,widget)) { g_object_weak_unref(G_OBJECT(widget),destroyed,NULL);g_hash_table_remove(records,widget); }
}
static void set_style(Record *r,gint i,const Style *s) {
 SEND(r->sci,SCI_STYLESETFONT,i,(sptr_t)s->font);SEND(r->sci,SCI_STYLESETSIZEFRACTIONAL,i,s->size);SEND(r->sci,SCI_STYLESETWEIGHT,i,s->weight);SEND(r->sci,SCI_STYLESETITALIC,i,s->italic);SEND(r->sci,SCI_STYLESETUNDERLINE,i,s->underline);SEND(r->sci,SCI_STYLESETCASE,i,s->letter_case);SEND(r->sci,SCI_STYLESETFORE,i,s->fore);SEND(r->sci,SCI_STYLESETBACK,i,s->back);
}
static void restore(Record *r) { if(r->valid) for(gint i=0;i<=STYLE_MAX;i++) if(syntax_style(i) || i==STYLE_LINENUMBER) set_style(r,i,&r->styles[i]); }
static gint grayscale(gint color) { gint y=(77*(color&255)+150*((color>>8)&255)+29*((color>>16)&255)+128)>>8;return y|(y<<8)|(y<<16); }
static Style themed(Record *r,gint i) {
 Style out=r->styles[i];InsightThemeStyle theme=insight_themes_style(r->sci,i);
 if(theme.mask&IT_FONT) out.font=theme.font;
 if(theme.mask&IT_SIZE) out.size=theme.size;
 if(theme.mask&IT_SCALE) out.size=CLAMP((theme.size ? theme.size : r->styles[STYLE_DEFAULT].size)*theme.scale/100,400,9600);
 if(theme.mask&IT_WEIGHT) out.weight=theme.weight;
 if(theme.mask&IT_ITALIC) out.italic=theme.italic;
 if(theme.mask&IT_UNDERLINE) out.underline=theme.underline;
 if(theme.mask&IT_FORE) out.fore=theme.fore;
 if(theme.mask&IT_BACK) out.back=theme.back;
 return out;
}
static void render(Record *r) {
 if(!r->valid) return;
 PangoFontDescription *font=mono ? pango_font_description_from_string(settings.font) : NULL;
 Style themed_base=themed(r,STYLE_DEFAULT);
 for(gint i=0;i<=STYLE_MAX;i++) if(syntax_style(i) || i==STYLE_LINENUMBER) {
  Style style=themed(r,i);const Style *base=&themed_base;
  if(!syntax_style(i)) { set_style(r,i,&style);continue; }
  if(mono || settings.color_only || !settings.enabled) { style.font=mono ? (gchar *)pango_font_description_get_family(font) : base->font;style.size=mono ? (gint)(100.0*pango_font_description_get_size(font)/PANGO_SCALE+0.5) : base->size;style.weight=mono ? (gint)pango_font_description_get_weight(font) : base->weight;style.italic=mono ? pango_font_description_get_style(font)!=PANGO_STYLE_NORMAL : base->italic;style.underline=mono ? FALSE : base->underline;style.letter_case=SC_CASE_MIXED; }
  /* Mono Font View overrides the other formatting options, retaining colors. */
  if(!mono && (!settings.enabled || settings.monochrome)) { style.fore=settings.enabled && settings.monochrome ? grayscale(base->fore) : base->fore;style.back=settings.enabled && settings.monochrome ? grayscale(base->back) : base->back; }
  set_style(r,i,&style);
 }
 if(font) pango_font_description_free(font);
}
static Record *record(ScintillaObject *sci) {
 Record *r=g_hash_table_lookup(records,sci);if(!r) { r=g_new0(Record,1);r->sci=sci;g_hash_table_insert(records,sci,r);g_object_weak_ref(G_OBJECT(sci),destroyed,NULL);g_signal_connect(sci,"destroy",G_CALLBACK(widget_destroyed),NULL); }return r;
}
void insight_formatting_begin(ScintillaObject *sci) {
 if(!initialized || !sci) return;
 Record *r=g_hash_table_lookup(records,sci);if(r) { restore(r);raw_clear(r); }
}
void insight_formatting_apply(ScintillaObject *sci) {
 if(!initialized || !sci) return;
 Record *r=record(sci);
 if(!r->valid) {
  for(gint i=0;i<=STYLE_MAX;i++) { Style *s=&r->styles[i];gint length=SEND(sci,SCI_STYLEGETFONT,i,0);s->font=g_malloc0(MAX(0,length)+1);SEND(sci,SCI_STYLEGETFONT,i,(sptr_t)s->font);s->size=SEND(sci,SCI_STYLEGETSIZEFRACTIONAL,i,0);s->weight=SEND(sci,SCI_STYLEGETWEIGHT,i,0);s->italic=SEND(sci,SCI_STYLEGETITALIC,i,0);s->underline=SEND(sci,SCI_STYLEGETUNDERLINE,i,0);s->letter_case=SEND(sci,SCI_STYLEGETCASE,i,0);s->fore=SEND(sci,SCI_STYLEGETFORE,i,0);s->back=SEND(sci,SCI_STYLEGETBACK,i,0); }
  r->valid=TRUE;
 }
 render(r);
}
static gboolean font_valid(const gchar *font) {
 if(!font || !*font || strlen(font)>1024 || !g_utf8_validate(font,-1,NULL) || strpbrk(font,"\r\n")) return FALSE;
 PangoFontDescription *p=pango_font_description_from_string(font);gint size=pango_font_description_get_size(p);gboolean ok=pango_font_description_get_family(p) && size>=4*PANGO_SCALE && size<=96*PANGO_SCALE && !pango_font_description_get_size_is_absolute(p);pango_font_description_free(p);return ok;
}
static void refresh(void) { GHashTableIter it;gpointer value;g_hash_table_iter_init(&it,records);while(g_hash_table_iter_next(&it,NULL,&value)) render(value); }
void insight_formatting_refresh(void) { if(initialized) refresh(); }
static gboolean save(const Settings *s,GError **error) {
 GKeyFile *key=g_key_file_new();g_key_file_set_integer(key,"formatting","version",1);g_key_file_set_boolean(key,"formatting","enabled",s->enabled);g_key_file_set_boolean(key,"formatting","color-only",s->color_only);g_key_file_set_boolean(key,"formatting","monochrome",s->monochrome);g_key_file_set_string(key,"formatting","mono-font",s->font);gsize length=0;gchar *text=g_key_file_to_data(key,&length,NULL),*directory=g_path_get_dirname(config);gboolean ok=g_mkdir_with_parents(directory,0700)==0 && g_file_set_contents(config,text,length,error);if(!ok && (!error || !*error)) g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_FAILED,"Cannot save syntax formatting options");g_free(directory);g_free(text);g_key_file_unref(key);return ok;
}
void insight_formatting_init(void) {
 if(initialized) return;
 records=g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,freed);settings=(Settings){TRUE,FALSE,FALSE,g_strdup("Monospace 12")};config=g_build_filename(app->configdir,"insight","syntax-formatting.conf",NULL);GKeyFile *key=g_key_file_new();GError *error=NULL;
 if(g_key_file_load_from_file(key,config,G_KEY_FILE_NONE,NULL)) {
  Settings candidate={0};gint version=g_key_file_get_integer(key,"formatting","version",&error);if(!error) candidate.enabled=g_key_file_get_boolean(key,"formatting","enabled",&error);if(!error) candidate.color_only=g_key_file_get_boolean(key,"formatting","color-only",&error);if(!error) candidate.monochrome=g_key_file_get_boolean(key,"formatting","monochrome",&error);if(!error) candidate.font=g_key_file_get_string(key,"formatting","mono-font",&error);
  if(!error && version==1 && font_valid(candidate.font)) { g_free(settings.font);settings=candidate;candidate.font=NULL; }g_free(candidate.font);
 }
 g_clear_error(&error);g_key_file_unref(key);mono=FALSE;initialized=TRUE;
}
static void mono_toggled(GtkCheckMenuItem *item,gpointer data) { (void)data;mono=gtk_check_menu_item_get_active(item);refresh(); }
static void show_options(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Syntax Formatting",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Apply",GTK_RESPONSE_APPLY,NULL);gtk_widget_set_name(dialog,"insight-formatting-dialog");
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)),*enabled=gtk_check_button_new_with_label("Use syntax formatting"),*colors=gtk_check_button_new_with_label("Use only color formatting"),*monochrome=gtk_check_button_new_with_label("Use no color formatting (monochrome)"),*font=gtk_font_button_new_with_font(settings.font);
 gtk_widget_set_name(enabled,"insight-formatting-enabled");gtk_widget_set_name(colors,"insight-formatting-color-only");gtk_widget_set_name(monochrome,"insight-formatting-monochrome");gtk_widget_set_name(font,"insight-formatting-mono-font");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled),settings.enabled);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(colors),settings.color_only);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(monochrome),settings.monochrome);
 GtkWidget *label=gtk_label_new("Mono Font View font"),*status=gtk_label_new("Mono Font View temporarily overrides these options. Text and undo history are unchanged.");gtk_label_set_line_wrap(GTK_LABEL(status),TRUE);GtkWidget *items[]={enabled,colors,monochrome,label,font,status};for(guint i=0;i<G_N_ELEMENTS(items);i++) gtk_box_pack_start(GTK_BOX(box),items[i],FALSE,FALSE,5);insight_panel_help_attach(box,"syntax-formatting.md");gtk_widget_show_all(dialog);
 while(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_APPLY) {
  Settings candidate={gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enabled)),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(colors)),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(monochrome)),gtk_font_chooser_get_font(GTK_FONT_CHOOSER(font))};GError *error=NULL;
  if(font_valid(candidate.font) && save(&candidate,&error)) { g_free(settings.font);settings=candidate;refresh();break; }
  gtk_label_set_text(GTK_LABEL(status),error ? error->message : "Choose a font between 4 and 96 points");g_clear_error(&error);g_free(candidate.font);
 }
 gtk_widget_destroy(dialog);
}
void insight_formatting_menu(GtkWidget *view,GtkWidget *options) {
 GtkWidget *item=gtk_check_menu_item_new_with_label("Mono Font View");gtk_widget_set_name(item,"insight-mono-font-view");g_signal_connect(item,"toggled",G_CALLBACK(mono_toggled),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(view),item);gtk_widget_show(item);
 item=gtk_menu_item_new_with_label("Syntax Formatting…");gtk_widget_set_name(item,"insight-syntax-formatting");g_signal_connect(item,"activate",G_CALLBACK(show_options),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(options),item);gtk_widget_show(item);
}
void insight_formatting_finalize(void) {
 if(!initialized) return;
 GHashTableIter it;gpointer value;g_hash_table_iter_init(&it,records);while(g_hash_table_iter_next(&it,NULL,&value)) { Record *r=value;restore(r);g_signal_handlers_disconnect_by_func(r->sci,widget_destroyed,NULL);g_object_weak_unref(G_OBJECT(r->sci),destroyed,NULL); }
 g_clear_pointer(&records,g_hash_table_unref);g_clear_pointer(&settings.font,g_free);g_clear_pointer(&config,g_free);initialized=FALSE;mono=FALSE;
}
