/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../desktop/html_export.h"
#include "../desktop/text_transfer.h"
#include <glib/gstdio.h>
#include <string.h>
static InsightHtmlSnapshot *snapshot(const gchar *text) {
 InsightHtmlSnapshot *s=g_new0(InsightHtmlSnapshot,1);s->text=g_strdup(text);s->length=strlen(text);s->title=g_strdup("x<&\".c");s->styles=g_malloc0(s->length+1);s->first_line=12;s->tab_width=4;
 for(guint i=0;i<256;i++) { s->palette[i].fore=0x332211;s->palette[i].back=0xffffff;s->palette[i].size=12;g_strlcpy(s->palette[i].font,"Mono\"</style><script>",257); }return s;
}
int main(void) {
 GError *e=NULL;InsightHtmlSnapshot *s=snapshot("α<&\"\t\r\nb\rc\n");s->styles[0]=5;s->styles[1]=77;s->palette[5].bold=TRUE;s->palette[5].italic=TRUE;s->palette[5].underline=TRUE;
 gchar *html=insight_html_export_build(s,TRUE,NULL,&e);g_assert_no_error(e);g_assert_nonnull(strstr(html,"x&lt;&amp;&quot;.c"));g_assert_nonnull(strstr(html,"#112233"));g_assert_nonnull(strstr(html,"font-weight:bold;font-style:italic;text-decoration:underline"));g_assert_nonnull(strstr(html,"id=\"L12\""));g_assert_nonnull(strstr(html,"id=\"L15\""));g_assert_nonnull(strstr(html,"<span class=\"s5\">α"));g_assert_null(strstr(html,"<script>"));g_assert_null(strstr(html,"\r"));g_assert_nonnull(strstr(html,"&lt;&amp;&quot;"));g_assert_nonnull(strstr(html,"default-src 'none'"));g_free(html);
 html=insight_html_export_build(s,FALSE,NULL,&e);g_assert_no_error(e);g_assert_null(strstr(html,"aria-hidden=\"true\""));g_assert_nonnull(strstr(html,"id=\"L12\""));g_free(html);
 GCancellable *c=g_cancellable_new();g_cancellable_cancel(c);g_assert_null(insight_html_export_build(s,TRUE,c,&e));g_assert_error(e,G_IO_ERROR,G_IO_ERROR_CANCELLED);g_clear_error(&e);g_object_unref(c);insight_html_snapshot_free(s);
 s=snapshot("");html=insight_html_export_build(s,TRUE,NULL,&e);g_assert_no_error(e);g_assert_nonnull(strstr(html,"id=\"L12\""));g_free(html);insight_html_snapshot_free(s);
 s=snapshot("a\1b");g_assert_null(insight_html_export_build(s,TRUE,NULL,&e));g_assert_error(e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA);g_clear_error(&e);insight_html_snapshot_free(s);
 s=snapshot("\xff");g_assert_null(insight_html_export_build(s,TRUE,NULL,&e));g_clear_error(&e);insight_html_snapshot_free(s);
 s=snapshot("abc");s->text[1]=0;g_assert_null(insight_html_export_build(s,TRUE,NULL,&e));g_clear_error(&e);insight_html_snapshot_free(s);
 s=snapshot("x");s->length=INSIGHT_HTML_SOURCE_LIMIT+1;g_assert_null(insight_html_export_build(s,TRUE,NULL,&e));g_clear_error(&e);s->length=1;memset(s->palette[1].font,'x',257);g_assert_null(insight_html_export_build(s,TRUE,NULL,&e));g_clear_error(&e);insight_html_snapshot_free(s);
 gchar *many=g_strnfill(INSIGHT_HTML_SOURCE_LIMIT,'x');s=snapshot(many);g_free(many);for(gsize i=0;i<s->length;i++) s->styles[i]=i%2;g_assert_null(insight_html_export_build(s,TRUE,NULL,&e));g_assert_error(e,G_IO_ERROR,G_IO_ERROR_NO_SPACE);g_clear_error(&e);insight_html_snapshot_free(s);
 gchar *dir=g_dir_make_tmp("html-export-XXXXXX",NULL),*source=g_build_filename(dir,"source.c",NULL),*dest=g_build_filename(dir,"source.html",NULL);g_assert_true(g_file_set_contents(source,"source",-1,NULL));GPtrArray *protected=g_ptr_array_new();g_ptr_array_add(protected,source);g_assert_false(insight_text_file_write_protected(source,"wrong",5,protected,&e));g_assert_error(e,G_IO_ERROR,G_IO_ERROR_PERMISSION_DENIED);g_clear_error(&e);g_assert_true(insight_text_file_write_protected(dest,"<pre>safe</pre>",15,protected,&e));g_assert_no_error(e);g_assert_true(insight_text_file_write_protected(dest,"new",3,protected,&e));g_assert_no_error(e);
#ifndef G_OS_WIN32
 gchar *linked=g_build_filename(dir,"linked",NULL),*through=g_build_filename(linked,"out.html",NULL);GFile *file=g_file_new_for_path(linked);g_assert_true(g_file_make_symbolic_link(file,dir,NULL,&e));g_object_unref(file);g_assert_false(insight_text_file_write_protected(through,"x",1,protected,&e));g_assert_error(e,G_IO_ERROR,G_IO_ERROR_NOT_DIRECTORY);g_clear_error(&e);g_remove(linked);g_free(linked);g_free(through);
#endif
 g_ptr_array_unref(protected);g_remove(source);g_remove(dest);g_rmdir(dir);g_free(dir);g_free(source);g_free(dest);g_print("HTML export: escaping, CSS font isolation, UTF-8 byte styles, exact line anchors, optional numbers, EOL normalization, cancellation, bounds and protected destinations passed\n");return 0;
}
