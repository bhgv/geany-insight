/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "symbol_edit.h"
#include "text_transfer.h"
#include <string.h>
void insight_symbol_plan_clear(InsightSymbolPlan *p) { if(p) { g_free(p->text);g_free(p->payload);memset(p,0,sizeof(*p)); } }
gboolean insight_symbol_plan(const gchar *source,gsize size,gsize first,gsize last,InsightSymbolEdit action,const gchar *clipboard,const gchar *eol,InsightSymbolPlan *plan,GError **error) {
 memset(plan,0,sizeof(*plan));
 if(!source||size>INSIGHT_SYMBOL_SOURCE_LIMIT||first>=last||last>size||last-first>INSIGHT_SYMBOL_TEXT_LIMIT||action<INSIGHT_SYMBOL_CUT||action>INSIGHT_SYMBOL_PASTE||!eol||(strcmp(eol,"\n")&&strcmp(eol,"\r")&&strcmp(eol,"\r\n"))) goto invalid;
 if((first&&source[first-1]!='\n'&&source[first-1]!='\r')||(last<size&&source[last-1]!='\n'&&source[last-1]!='\r')) goto invalid;
 plan->begin=first;plan->end=action==INSIGHT_SYMBOL_CUT?last:first;plan->selection_begin=first;
 if(action==INSIGHT_SYMBOL_CUT) plan->payload=g_strdup("");
 else if(action==INSIGHT_SYMBOL_DUPLICATE) {
  gboolean separator=source[last-1]!='\n'&&source[last-1]!='\r';gchar *text=g_strndup(source+first,last-first);plan->payload=g_strconcat(separator?eol:"",text,NULL);g_free(text);plan->begin=plan->end=last;plan->selection_begin=last+(separator?strlen(eol):0);
 } else {
  if(!clipboard||!*clipboard||strlen(clipboard)>INSIGHT_SYMBOL_TEXT_LIMIT) goto invalid;
  gsize amount=strlen(clipboard);gboolean separator=clipboard[amount-1]!='\n'&&clipboard[amount-1]!='\r';plan->payload=g_strconcat(clipboard,separator?eol:"",NULL);
 }
 plan->text=insight_text_splice(source,size,plan->payload,strlen(plan->payload),plan->begin,plan->end,error);
 if(!plan->text) { insight_symbol_plan_clear(plan);return FALSE; }
 plan->selection_end=plan->selection_begin+(action==INSIGHT_SYMBOL_CUT?0:action==INSIGHT_SYMBOL_DUPLICATE?last-first:strlen(clipboard));return TRUE;
invalid:
 insight_symbol_plan_clear(plan);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Symbol editing requires a complete standalone definition up to 1 MiB, a source up to 2 MiB and nonempty UTF-8 clipboard text for Paste.");return FALSE;
}
