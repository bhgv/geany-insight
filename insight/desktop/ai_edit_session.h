/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_AI_EDIT_SESSION_H
#define INSIGHT_AI_EDIT_SESSION_H
#include <glib.h>
#define INSIGHT_AI_SOURCE_LIMIT 131072u
#define INSIGHT_AI_REQUIREMENTS_LIMIT 16384u
typedef struct { gchar *code,*requirements,*explanation; } InsightAiVariant;
typedef struct { GPtrArray *variants;guint position; } InsightAiEdits;
InsightAiEdits *insight_ai_edits_new(const gchar *);
void insight_ai_edits_free(InsightAiEdits *);
const InsightAiVariant *insight_ai_edits_current(const InsightAiEdits *);
gboolean insight_ai_edits_push(InsightAiEdits *,const gchar *,const gchar *,const gchar *,GError **);
gboolean insight_ai_edits_move(InsightAiEdits *,gint);
gchar *insight_ai_requirements_append(const gchar *,const gchar *,GError **);
typedef struct { gchar *text;guint count;gint64 last; } InsightAiPrompt;
gchar *insight_ai_prompts_path(const gchar *,const gchar *,const gchar *);
gboolean insight_ai_prompts_remove(GPtrArray *,const gchar *,const gchar *,GError **);
GPtrArray *insight_ai_prompts_load(const gchar *);
gboolean insight_ai_prompts_record(GPtrArray *,const gchar *,const gchar *,GError **);
const gchar *insight_ai_task_name(guint);
const gchar *insight_ai_task_prompt(guint);
guint insight_ai_task_count(void);
#endif
