/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_CONTROL_FLOW_H
#define INSIGHT_CONTROL_FLOW_H
#include <stddef.h>
#define INSIGHT_FLOW_LIMIT 512
#define INSIGHT_FLOW_EDGE_LIMIT (INSIGHT_FLOW_LIMIT * 2)
typedef enum { INSIGHT_FLOW_ENTRY, INSIGHT_FLOW_EXIT, INSIGHT_FLOW_STEP, INSIGHT_FLOW_IF, INSIGHT_FLOW_LOOP, INSIGHT_FLOW_RETURN, INSIGHT_FLOW_BREAK, INSIGHT_FLOW_CONTINUE } InsightFlowKind;
typedef enum { INSIGHT_FLOW_NEXT, INSIGHT_FLOW_YES, INSIGHT_FLOW_NO, INSIGHT_FLOW_BACK } InsightFlowEdgeKind;
typedef struct { size_t begin,end; InsightFlowKind kind; unsigned lane; } InsightFlowNode;
typedef struct { unsigned from,to; InsightFlowEdgeKind kind; } InsightFlowEdge;
typedef struct { unsigned count,edge_count; InsightFlowNode nodes[INSIGHT_FLOW_LIMIT]; InsightFlowEdge edges[INSIGHT_FLOW_EDGE_LIMIT]; char error[160]; } InsightFlow;
/* mask has identical byte offsets, with comments/strings replaced by spaces.
 * body points to an opening brace; end is exclusive. Failure clears all nodes.
 * Structured C-like syntax only, no preprocessor evaluation/type inference. */
int insight_control_flow(const char *mask,size_t length,size_t body,size_t end,InsightFlow *out);
/* Return controlling Yes/No edges along a shortest structural entry-to-offset
 * route. Reconverged choices are omitted. -1 means offset is unreachable or
 * outside a statement. This is lexical reachability, not condition evaluation. */
int insight_control_guards(const InsightFlow *,size_t offset,InsightFlowEdge *out,unsigned capacity);
#endif
