/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_AI_CLIENT_H
#define INSIGHT_AI_CLIENT_H
#include <gio/gio.h>
typedef struct { const gchar *endpoint,*model,*key,*question,*context,*code,*history,*location; } InsightAiRequest;
typedef struct { gchar *explanation,*command,*replacement;gboolean has_replacement; } InsightAiAnswer;
void insight_ai_answer_free(InsightAiAnswer *);
InsightAiAnswer *insight_ai_request(const gchar *,const gchar *,const InsightAiRequest *,GCancellable *,GError **);
gboolean insight_ai_command_valid(const gchar *);
#endif
