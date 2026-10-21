/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ai_edit_session.h"
#include <gio/gio.h>
#include <string.h>
int main(void) {
 InsightAiEdits *s=insight_ai_edits_new("int a;\n");GError *e=NULL;
 gchar *first=insight_ai_requirements_append("","  Fix syntax  ",&e);g_assert_no_error(e);g_assert_cmpstr(first,==,"Fix syntax");
 g_assert_true(insight_ai_edits_push(s,"int alpha;\n",first,"Renamed",&e));gchar *second=insight_ai_requirements_append(first,"Add documentation",&e);g_assert_cmpstr(second,==,"Fix syntax\n\nAdd documentation");g_assert_true(insight_ai_edits_push(s,"/** alpha */\nint alpha;\n",second,"Documented",&e));
 g_assert_true(insight_ai_edits_move(s,-1));g_assert_cmpstr(insight_ai_edits_current(s)->requirements,==,first);g_assert_true(insight_ai_edits_move(s,1));g_assert_cmpstr(insight_ai_edits_current(s)->code,==,"/** alpha */\nint alpha;\n");
 g_assert_true(insight_ai_edits_move(s,-1));g_assert_true(insight_ai_edits_push(s,"int beta;\n",first,"Alternative",&e));g_assert_false(insight_ai_edits_move(s,1));
 for(guint i=0;i<40;i++) { gchar *code=g_strdup_printf("int value%u;\n",i);g_assert_true(insight_ai_edits_push(s,code,second,"Variant",&e));g_free(code); }g_assert_cmpuint(s->variants->len,==,32);g_assert_cmpuint(s->position,==,31);
 gchar *too_long=g_strnfill(131073,'a');g_assert_false(insight_ai_edits_push(s,too_long,"","",&e));g_assert_nonnull(e);g_clear_error(&e);g_free(too_long);g_assert_null(insight_ai_requirements_append(""," \n ",&e));g_clear_error(&e);
 gchar *dir=g_dir_make_tmp("ai-prompts-XXXXXX",NULL),*path=g_build_filename(dir,"prompts.conf",NULL);GPtrArray *p=insight_ai_prompts_load(path);g_assert_cmpuint(p->len,==,0);g_assert_true(insight_ai_prompts_record(p,path,"Improve names α",&e));g_assert_true(insight_ai_prompts_record(p,path,"Improve names α",&e));g_assert_cmpuint(p->len,==,1);g_ptr_array_unref(p);p=insight_ai_prompts_load(path);g_assert_cmpuint(p->len,==,1);g_assert_cmpuint(((InsightAiPrompt *)g_ptr_array_index(p,0))->count,==,2);
 GPtrArray *other=insight_ai_prompts_load(path);g_assert_true(insight_ai_prompts_record(p,path,"First window",&e));g_assert_true(insight_ai_prompts_record(other,path,"Second window",&e));g_assert_cmpuint(other->len,==,3);g_ptr_array_unref(other);
 for(guint i=0;i<70;i++) { gchar *q=g_strdup_printf("Request %u",i);g_assert_true(insight_ai_prompts_record(p,path,q,&e));g_free(q); }g_assert_cmpuint(p->len,==,64);g_assert_no_error(e);g_assert_cmpuint(insight_ai_task_count(),==,10);for(guint i=0;i<10;i++) { g_assert_nonnull(insight_ai_task_name(i));g_assert_true(g_utf8_validate(insight_ai_task_prompt(i),-1,NULL)); }
 g_ptr_array_unref(p);insight_ai_edits_free(s);g_free(first);g_free(second);g_free(path);g_free(dir);g_print("AI edit requirements, variant Undo/Redo, branching, bounds and durable prompt usage passed\n");return 0;
}
