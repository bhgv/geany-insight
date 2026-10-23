/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-text-print.h"
#include "insight-i18n.h"
#include <math.h>
#include <string.h>
#ifdef GEANY_PRIVATE
#include "geany.h"
#include "ui_utils.h"
#endif
#define MAX_TEXT (1024u*1024u)
#define MAX_PAGES 512
typedef struct { PangoLayoutLine *line;gdouble height,baseline; } PrintLine;
typedef struct { guint first,end; } PrintPage;
typedef struct {
    gchar *title,*text;
    PangoLayout *layout,*label;
    GArray *lines,*pages;
    gdouble width,height;
} TextPrint;
static void clear_layout(TextPrint *p) {
    g_clear_object(&p->layout);g_clear_object(&p->label);
    g_array_set_size(p->lines,0);g_array_set_size(p->pages,0);
}
static void free_print(gpointer data) {
    TextPrint *p=data;clear_layout(p);g_array_unref(p->lines);g_array_unref(p->pages);g_free(p->title);g_free(p->text);g_free(p);
}
static void invalid(GtkPrintOperation *op,const gchar *reason) {
    g_object_set_data(G_OBJECT(op),"insight-text-print-error",(gpointer)reason);gtk_print_operation_cancel(op);
}
static void font(PangoLayout *layout,const gchar *description) {
    PangoFontDescription *f=pango_font_description_from_string(description);pango_layout_set_font_description(layout,f);pango_font_description_free(f);
}
static void begin(GtkPrintOperation *op,GtkPrintContext *context,gpointer data) {
    TextPrint *p=data;clear_layout(p);g_object_set_data(G_OBJECT(op),"insight-text-print-error",NULL);
    p->width=gtk_print_context_get_width(context);p->height=gtk_print_context_get_height(context);
    if(!isfinite(p->width) || !isfinite(p->height) || p->width<72 || p->width>14400 || p->height<90 || p->height>14400) { invalid(op,"Printable area is too small or invalid");return; }
    p->layout=gtk_print_context_create_pango_layout(context);font(p->layout,"Monospace 9");pango_layout_set_width(p->layout,(gint)(p->width*PANGO_SCALE));pango_layout_set_wrap(p->layout,PANGO_WRAP_WORD_CHAR);pango_layout_set_text(p->layout,p->text,-1);
    p->label=gtk_print_context_create_pango_layout(context);font(p->label,"Sans 9");pango_layout_set_width(p->label,(gint)(p->width*PANGO_SCALE));pango_layout_set_ellipsize(p->label,PANGO_ELLIPSIZE_END);pango_layout_set_single_paragraph_mode(p->label,TRUE);
    gdouble available=p->height-44,used=0;PrintPage page={0,0};PangoLayoutIter *iter=pango_layout_get_iter(p->layout);
    do {
        PangoRectangle logical;pango_layout_iter_get_line_extents(iter,NULL,&logical);
        PrintLine line={pango_layout_iter_get_line_readonly(iter),MAX(1.0,(gdouble)logical.height/PANGO_SCALE),(gdouble)(pango_layout_iter_get_baseline(iter)-logical.y)/PANGO_SCALE};
        if(line.height>available || p->lines->len>=100000) { pango_layout_iter_free(iter);invalid(op,"Text exceeds the printable page or layout limits");return; }
        if(used+line.height>available && page.end>page.first) {
            g_array_append_val(p->pages,page);page.first=page.end;used=0;
            if(p->pages->len>=MAX_PAGES) { pango_layout_iter_free(iter);invalid(op,"Text printing exceeds 512 pages; narrow the list");return; }
        }
        g_array_append_val(p->lines,line);page.end++;used+=line.height;
    } while(pango_layout_iter_next_line(iter));
    pango_layout_iter_free(iter);g_array_append_val(p->pages,page);gtk_print_operation_set_n_pages(op,(gint)p->pages->len);
}
static void draw(GtkPrintOperation *op,GtkPrintContext *context,gint number,gpointer data) {
    (void)op;TextPrint *p=data;if(number<0 || (guint)number>=p->pages->len) return;
    cairo_t *cr=gtk_print_context_get_cairo_context(context);cairo_save(cr);cairo_set_source_rgb(cr,0,0,0);
    pango_layout_set_alignment(p->label,PANGO_ALIGN_LEFT);pango_layout_set_text(p->label,p->title,-1);cairo_move_to(cr,0,0);pango_cairo_show_layout(cr,p->label);
    cairo_set_line_width(cr,0.4);cairo_move_to(cr,0,17);cairo_line_to(cr,p->width,17);cairo_stroke(cr);
    PrintPage page=g_array_index(p->pages,PrintPage,number);gdouble top=24;
    cairo_save(cr);cairo_rectangle(cr,0,24,p->width,p->height-44);cairo_clip(cr);
    for(guint i=page.first;i<page.end;i++) { PrintLine *line=&g_array_index(p->lines,PrintLine,i);cairo_move_to(cr,0,top+line->baseline);pango_cairo_show_layout_line(cr,line->line);top+=line->height; }
    cairo_restore(cr);
    gchar *footer=g_strdup_printf("%s %d / %u",insight_tr("Page"),number+1,p->pages->len);pango_layout_set_alignment(p->label,PANGO_ALIGN_RIGHT);pango_layout_set_text(p->label,footer,-1);g_free(footer);cairo_move_to(cr,0,p->height-14);pango_cairo_show_layout(cr,p->label);cairo_restore(cr);
}
GtkPrintOperation *insight_text_print_operation(const gchar *title,const gchar *text,gsize length) {
    if(!text || length>MAX_TEXT || memchr(text,0,length) || !g_utf8_validate(text,length,NULL) || (title && (strlen(title)>8192 || !g_utf8_validate(title,-1,NULL)))) return NULL;
    gsize column=0;guint lines=1;GString *normalized=g_string_sized_new(length);
    for(gsize i=0;i<length;i++) {
        guchar c=(guchar)text[i];
        if(c=='\r' || c=='\n') { if(c=='\r' && i+1<length && text[i+1]=='\n') i++;g_string_append_c(normalized,'\n');column=0;if(++lines>20000) { g_string_free(normalized,TRUE);return NULL; } }
        else { if((c<32 && c!='\t') || c==127 || ++column>65536) { g_string_free(normalized,TRUE);return NULL; }g_string_append_c(normalized,(gchar)c); }
    }
    TextPrint *p=g_new0(TextPrint,1);p->text=g_string_free(normalized,FALSE);p->title=g_strdup(title && *title ? title:insight_tr("List"));for(gchar *c=p->title;*c;c++) if((guchar)*c<32 || *c==127) *c=' ';
    p->lines=g_array_new(FALSE,FALSE,sizeof(PrintLine));p->pages=g_array_new(FALSE,FALSE,sizeof(PrintPage));
    GtkPrintOperation *op=gtk_print_operation_new();gtk_print_operation_set_unit(op,GTK_UNIT_POINTS);gtk_print_operation_set_allow_async(op,FALSE);gtk_print_operation_set_embed_page_setup(op,TRUE);gtk_print_operation_set_job_name(op,p->title);
    g_object_set_data_full(G_OBJECT(op),"insight-text-print",p,free_print);g_signal_connect(op,"begin-print",G_CALLBACK(begin),p);g_signal_connect(op,"draw-page",G_CALLBACK(draw),p);return op;
}
void insight_text_print_run(GtkPrintOperation *op,GtkWindow *parent) {
    if(!op) {
#ifdef GEANY_PRIVATE
        ui_set_statusbar(TRUE,"%s",insight_tr("Print snapshot is invalid or exceeds 1 MiB, 20000 lines or 64 KiB per line"));
#endif
        return;
    }
    GError *error=NULL;gtk_print_operation_run(op,GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,parent,&error);
#ifdef GEANY_PRIVATE
    const gchar *reason=g_object_get_data(G_OBJECT(op),"insight-text-print-error");if(reason || error) ui_set_statusbar(TRUE,"%s",reason ? insight_tr(reason):error->message);
#endif
    g_clear_error(&error);g_object_unref(op);
}
