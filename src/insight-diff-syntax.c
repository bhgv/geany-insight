/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-diff-syntax.h"
#include "document.h"
#include "filetypes.h"
#include "highlighting.h"
#include "sciwrappers.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkTextTag *style_tag(GtkTextBuffer *buffer,ScintillaObject *sci,guint style) {
 gchar *name=g_strdup_printf("insight-syntax-%u",style);GtkTextTag *tag=gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer),name);if(!tag) tag=gtk_text_buffer_create_tag(buffer,name,NULL);g_free(name);guint color=SEND(sci,SCI_STYLEGETFORE,style,0);gchar rgb[8];g_snprintf(rgb,sizeof rgb,"#%02x%02x%02x",color&255,(color>>8)&255,(color>>16)&255);g_object_set(tag,"foreground",rgb,"weight",SEND(sci,SCI_STYLEGETBOLD,style,0) ? PANGO_WEIGHT_BOLD:PANGO_WEIGHT_NORMAL,"style",SEND(sci,SCI_STYLEGETITALIC,style,0) ? PANGO_STYLE_ITALIC:PANGO_STYLE_NORMAL,NULL);return tag;
}
void insight_diff_native_colorize(GtkTextBuffer *buffer,const gchar *filename) {
 GtkTextIter a,z;gtk_text_buffer_get_bounds(buffer,&a,&z);gchar *text=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);gsize size=strlen(text);if(size>1024*1024 || !g_utf8_validate(text,size,NULL)) { g_free(text);return; }
 GtkTextTagTable *table=gtk_text_buffer_get_tag_table(buffer);for(guint i=0;i<256;i++) { gchar *name=g_strdup_printf("insight-syntax-%u",i);GtkTextTag *tag=gtk_text_tag_table_lookup(table,name);if(tag) gtk_text_buffer_remove_tag(buffer,tag,&a,&z);g_free(name); }
 ScintillaObject *sci=SCINTILLA(scintilla_new());g_object_ref_sink(sci);SEND(sci,SCI_SETCODEPAGE,SC_CP_UTF8,0);GeanyFiletype *type=filetypes_detect_from_extension(filename ? filename:"");if((!filename || !strchr(filename,'.')) && (!type || type->id==GEANY_FILETYPES_NONE)) { GeanyDocument *doc=document_get_current();if(DOC_VALID(doc)) type=doc->file_type; }highlighting_set_styles(sci,type ? type:filetypes[GEANY_FILETYPES_NONE]);sci_set_text(sci,text);SEND(sci,SCI_COLOURISE,0,-1);GtkTextTag *tags[256]={0};tags[STYLE_DEFAULT]=style_tag(buffer,sci,STYLE_DEFAULT);gtk_text_buffer_apply_tag(buffer,tags[STYLE_DEFAULT],&a,&z);
 gint begin=0,offset=0;guint previous=size ? SEND(sci,SCI_GETSTYLEAT,0,0):0,runs=0;for(const gchar *p=text;*p;p=g_utf8_next_char(p),offset++) { guint current=SEND(sci,SCI_GETSTYLEAT,p-text,0)&255;if(current!=previous) { GtkTextIter first,last;gtk_text_buffer_get_iter_at_offset(buffer,&first,begin);gtk_text_buffer_get_iter_at_offset(buffer,&last,offset);if(!tags[previous]) tags[previous]=style_tag(buffer,sci,previous);gtk_text_buffer_apply_tag(buffer,tags[previous],&first,&last);if(++runs>=20000) break;begin=offset;previous=current; } }
 if(runs<20000 && offset>begin) { gtk_text_buffer_get_iter_at_offset(buffer,&a,begin);gtk_text_buffer_get_iter_at_offset(buffer,&z,offset);if(!tags[previous]) tags[previous]=style_tag(buffer,sci,previous);gtk_text_buffer_apply_tag(buffer,tags[previous],&a,&z); }gtk_widget_destroy(GTK_WIDGET(sci));g_object_unref(sci);g_free(text);
}
