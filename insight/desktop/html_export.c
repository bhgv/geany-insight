/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "html_export.h"
#include <string.h>
void insight_html_snapshot_free(InsightHtmlSnapshot *s) { if(s) { g_free(s->text);g_free(s->title);g_free(s->styles);g_free(s); } }
static gboolean fail(GError **e,GIOErrorEnum code,const gchar *message) { g_set_error_literal(e,G_IO_ERROR,code,message);return FALSE; }
static void color(GString *out,guint rgb) { g_string_append_printf(out,"#%02x%02x%02x",rgb&255,(rgb>>8)&255,(rgb>>16)&255); }
static void css_font(GString *out,const gchar *font) {
 g_string_append_c(out,'"');for(const gchar *p=font;*p;p=g_utf8_next_char(p)) g_string_append_printf(out,"\\%x ",g_utf8_get_char(p));g_string_append_c(out,'"');
}
static gboolean valid(const InsightHtmlSnapshot *s,GError **error) {
 if(!s || !s->text || !s->styles || !s->title || s->length>INSIGHT_HTML_SOURCE_LIMIT || memchr(s->text,0,s->length) || !g_utf8_validate(s->text,s->length,NULL) || strlen(s->title)>16384 || !g_utf8_validate(s->title,-1,NULL) || !s->first_line || s->first_line>1000000000u) return fail(error,G_IO_ERROR_INVALID_DATA,"HTML export requires bounded UTF-8 text and a valid snapshot.");
 for(gsize i=0;i<s->length;i++) if((guchar)s->text[i]<32 && !strchr("\t\r\n",s->text[i])) return fail(error,G_IO_ERROR_INVALID_DATA,"HTML export cannot represent source control characters other than tabs and line endings.");
 for(guint i=0;i<256;i++) if(!memchr(s->palette[i].font,0,sizeof s->palette[i].font) || !g_utf8_validate(s->palette[i].font,-1,NULL)) return fail(error,G_IO_ERROR_INVALID_DATA,"HTML export font names must be bounded UTF-8.");return TRUE;
}
static void start_line(GString *out,guint line,gboolean numbers) { g_string_append_printf(out,"<span id=\"L%u\">",line);if(numbers) g_string_append_printf(out,"<span class=\"ln\" aria-hidden=\"true\">%u</span>",line); }
gchar *insight_html_export_build(const InsightHtmlSnapshot *s,gboolean numbers,GCancellable *cancel,GError **error) {
 if(!valid(s,error)) return NULL;if(cancel && g_cancellable_set_error_if_cancelled(cancel,error)) return NULL;
 gboolean used[256]={FALSE};for(gsize i=0;i<s->length;i++) used[s->styles[i]]=TRUE;
 gchar *title=g_markup_escape_text(s->title,-1);GString *out=g_string_new("<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\"><meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'\"><title>");g_string_append(out,title);g_free(title);
 g_string_append(out,"</title><style>\nbody{margin:1em;color:");color(out,s->palette[32].fore);g_string_append(out,";background:");color(out,s->palette[32].back);g_string_append(out,"}pre{white-space:pre;line-height:1.4;font-family:");css_font(out,*s->palette[32].font ? s->palette[32].font:"monospace");g_string_append_printf(out,",monospace;font-size:%upt;tab-size:%u}.ln{display:inline-block;min-width:4em;padding-right:1em;text-align:right;opacity:.6;user-select:none}\n",CLAMP(s->palette[32].size,1,100),CLAMP(s->tab_width,1,32));
 for(guint i=0;i<256;i++) if(used[i]) { const InsightHtmlStyle *p=&s->palette[i];g_string_append_printf(out,".s%u{color:",i);color(out,p->fore);g_string_append(out,";background-color:");color(out,p->back);g_string_append_printf(out,";font-weight:%s;font-style:%s;text-decoration:%s;font-size:%upt;font-family:",p->bold ? "bold":"normal",p->italic ? "italic":"normal",p->underline ? "underline":"none",CLAMP(p->size,1,100));css_font(out,*p->font ? p->font:"monospace");g_string_append(out,",monospace}\n"); }
 g_string_append(out,"</style></head><body><pre>");guint line=s->first_line;gint style=-1;start_line(out,line,numbers);
 for(gsize i=0;i<s->length;) {
  if(cancel && g_cancellable_set_error_if_cancelled(cancel,error)) { g_string_free(out,TRUE);return NULL; }
  if(s->text[i]=='\r' || s->text[i]=='\n') { if(style>=0) { g_string_append(out,"</span>");style=-1; }if(s->text[i]=='\r' && i+1<s->length && s->text[i+1]=='\n') i++;i++;g_string_append(out,"</span>\n");start_line(out,++line,numbers); }
  else { guint next=s->styles[i];if(style!=(gint)next) { if(style>=0) g_string_append(out,"</span>");g_string_append_printf(out,"<span class=\"s%u\">",next);style=next; }const gchar *p=s->text+i,*end=g_utf8_next_char(p);gchar *safe=g_markup_escape_text(p,end-p);g_string_append(out,safe);g_free(safe);i=end-s->text; }
  if(out->len>INSIGHT_HTML_OUTPUT_LIMIT-64) { g_string_free(out,TRUE);fail(error,G_IO_ERROR_NO_SPACE,"HTML export exceeds the 8 MiB output limit.");return NULL; }
 }
 if(style>=0) g_string_append(out,"</span>");g_string_append(out,"</span></pre></body></html>\n");return g_string_free(out,FALSE);
}
