/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "completion_settings.h"
#define GROUP "completion"
void insight_completion_settings_default(InsightCompletionSettings *p) { *p=(InsightCompletionSettings){TRUE,TRUE,TRUE,FALSE,FALSE,FALSE,0,9}; }
gboolean insight_completion_settings_valid(const InsightCompletionSettings *p,GError **error) { if(p->min_chars<0 || p->min_chars>12 || p->max_rows<3 || p->max_rows>20) { g_set_error_literal(error,G_KEY_FILE_ERROR,G_KEY_FILE_ERROR_INVALID_VALUE,"Completion threshold must be 0–12 and visible rows 3–20");return FALSE; }return TRUE; }
gboolean insight_completion_settings_read(InsightCompletionSettings *p,GKeyFile *key,GError **error) {
 InsightCompletionSettings next;insight_completion_settings_default(&next);insight_completion_settings_default(p);GError *failure=NULL;
 const gchar *keys[]={"case-sensitive","detailed","accept-tab","inside-comments","insert-parentheses","space-after-name"};gboolean *values[]={&next.case_sensitive,&next.detailed,&next.accept_tab,&next.inside_comments,&next.insert_parentheses,&next.space_after_name};
 for(guint n=0;n<G_N_ELEMENTS(keys) && !failure;n++) if(g_key_file_has_key(key,GROUP,keys[n],NULL)) *values[n]=g_key_file_get_boolean(key,GROUP,keys[n],&failure);
 if(!failure && g_key_file_has_key(key,GROUP,"min-chars",NULL)) next.min_chars=g_key_file_get_integer(key,GROUP,"min-chars",&failure);
 if(!failure && g_key_file_has_key(key,GROUP,"max-rows",NULL)) next.max_rows=g_key_file_get_integer(key,GROUP,"max-rows",&failure);
 if(!failure) insight_completion_settings_valid(&next,&failure);
 if(failure) { g_propagate_error(error,failure);return FALSE; }*p=next;return TRUE;
}
gboolean insight_completion_settings_write(const InsightCompletionSettings *p,GKeyFile *key,GError **error) {
 if(!insight_completion_settings_valid(p,error)) return FALSE;
 const gchar *keys[]={"case-sensitive","detailed","accept-tab","inside-comments","insert-parentheses","space-after-name"};gboolean values[]={p->case_sensitive,p->detailed,p->accept_tab,p->inside_comments,p->insert_parentheses,p->space_after_name};
 for(guint n=0;n<G_N_ELEMENTS(keys);n++) g_key_file_set_boolean(key,GROUP,keys[n],values[n]);
 g_key_file_set_integer(key,GROUP,"min-chars",p->min_chars);g_key_file_set_integer(key,GROUP,"max-rows",p->max_rows);return TRUE;
}
gchar *insight_completion_settings_suffix(const InsightCompletionSettings *p,gboolean callable,const gchar *right) {
 if(!p->insert_parentheses || !callable) return g_strdup("");
 while(right && (*right==' ' || *right=='\t')) right++;
 if(right && *right=='(') return g_strdup("");
 return g_strdup(p->space_after_name ? " ()" : "()");
}
