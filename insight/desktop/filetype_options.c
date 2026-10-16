/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "filetype_options.h"
#define GROUP "editing"
void insight_filetype_options_default(InsightFiletypeOptions *p)
{ *p=(InsightFiletypeOptions){FALSE,FALSE,FALSE,TRUE,FALSE,FALSE,TRUE,FALSE,TRUE,TRUE,TRUE,TRUE,4,4,0,3,80}; }
gboolean insight_filetype_options_valid(const InsightFiletypeOptions *p,GError **error)
{
 if(p->tab_width<1 || p->tab_width>32 || p->indent_width<1 || p->indent_width>32 || p->indent_type<0 || p->indent_type>2 || p->auto_indent<0 || p->auto_indent>3 || p->margin<20 || p->margin>500) { g_set_error_literal(error,G_KEY_FILE_ERROR,G_KEY_FILE_ERROR_INVALID_VALUE,"Editing options require tab/indent widths 1–32 and a margin 20–500");return FALSE; }return TRUE;
}
gboolean insight_filetype_options_read(InsightFiletypeOptions *p,GKeyFile *key,GError **error)
{
 InsightFiletypeOptions next;insight_filetype_options_default(&next);insight_filetype_options_default(p);
 if(!g_key_file_has_group(key,GROUP)) return TRUE;
 GError *failure=NULL;gint version=g_key_file_get_integer(key,GROUP,"version",&failure);
 if(!failure && version!=1) g_set_error_literal(&failure,G_KEY_FILE_ERROR,G_KEY_FILE_ERROR_INVALID_VALUE,"Unsupported editing options version");
 const gchar *bools[]={"enabled","soft-wrap","hard-wrap","line-numbers","white-space","line-endings","indent-guides","right-margin","allow-completion","allow-snippets","typing-shortcuts","enter-newline"};
 gboolean *values[]={&next.enabled,&next.soft_wrap,&next.hard_wrap,&next.line_numbers,&next.white_space,&next.line_endings,&next.indent_guides,&next.right_margin,&next.allow_completion,&next.allow_snippets,&next.typing_shortcuts,&next.enter_newline};
 for(guint i=0;i<G_N_ELEMENTS(bools) && !failure;i++) if(g_key_file_has_key(key,GROUP,bools[i],NULL)) *values[i]=g_key_file_get_boolean(key,GROUP,bools[i],&failure);
 const gchar *ints[]={"tab-width","indent-width","indent-type","auto-indent","margin"};gint *numbers[]={&next.tab_width,&next.indent_width,&next.indent_type,&next.auto_indent,&next.margin};
 for(guint i=0;i<G_N_ELEMENTS(ints) && !failure;i++) if(g_key_file_has_key(key,GROUP,ints[i],NULL)) *numbers[i]=g_key_file_get_integer(key,GROUP,ints[i],&failure);
 if(!failure) insight_filetype_options_valid(&next,&failure);
 if(failure) { g_propagate_error(error,failure);return FALSE; }*p=next;return TRUE;
}
gboolean insight_filetype_options_write(const InsightFiletypeOptions *p,GKeyFile *key,GError **error)
{
 if(!insight_filetype_options_valid(p,error)) return FALSE;
 g_key_file_set_integer(key,GROUP,"version",1);
 const gchar *bools[]={"enabled","soft-wrap","hard-wrap","line-numbers","white-space","line-endings","indent-guides","right-margin","allow-completion","allow-snippets","typing-shortcuts","enter-newline"};
 gboolean values[]={p->enabled,p->soft_wrap,p->hard_wrap,p->line_numbers,p->white_space,p->line_endings,p->indent_guides,p->right_margin,p->allow_completion,p->allow_snippets,p->typing_shortcuts,p->enter_newline};for(guint i=0;i<G_N_ELEMENTS(bools);i++) g_key_file_set_boolean(key,GROUP,bools[i],values[i]);
 const gchar *ints[]={"tab-width","indent-width","indent-type","auto-indent","margin"};gint numbers[]={p->tab_width,p->indent_width,p->indent_type,p->auto_indent,p->margin};for(guint i=0;i<G_N_ELEMENTS(ints);i++) g_key_file_set_integer(key,GROUP,ints[i],numbers[i]);return TRUE;
}
