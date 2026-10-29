/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "call_paths.h"
#include "source_mask.h"
#include "../core/structure_select.h"
#include <string.h>
#define SOURCE_LIMIT (2u*1024u*1024u)
#define TOTAL_LIMIT (64u*1024u*1024u)
typedef struct { InsightCallPaths *r;GHashTable *names,*missing;InsightCallLoad load;gpointer data;GCancellable *cancel;gsize bytes;guint calls;gboolean indirect,halt; } Builder;
gchar *insight_call_normalize(const gchar *text) {
 if(g_str_has_prefix(text,"\357\273\277")) text+=3;GString *out=g_string_sized_new(strlen(text));for(const gchar *s=text;*s;s++) if(*s!='\r' || s[1]!='\n') g_string_append_c(out,*s);return g_string_free(out,FALSE);
}
gchar *insight_call_digest(const gchar *text) {gchar *normal=insight_call_normalize(text),*digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,normal,-1);g_free(normal);return digest;}
static void source_free(gpointer data) {InsightCallSource *s=data;g_free(s->text);g_free(s->mask);g_free(s->digest);g_array_unref(s->lines);g_free(s);}
static void function_free(gpointer data) {InsightCallFunction *f=data;g_free(f->name);g_free(f->scope);g_free(f->path);g_free(f);}
void insight_call_paths_free(InsightCallPaths *r) {if(!r) return;g_ptr_array_unref(r->functions);g_ptr_array_unref(r->paths);g_array_unref(r->edges);g_hash_table_unref(r->sources);g_free(r);}
static gboolean stopped(Builder *b) {return b->cancel && g_cancellable_is_cancelled(b->cancel);}
static gboolean word_at(const gchar *text,gsize length,gsize at,const gchar *word) {gsize n=strlen(word);return at+n<=length && !memcmp(text+at,word,n) && (!at || !(g_ascii_isalnum(text[at-1]) || text[at-1]=='_')) && (at+n==length || !(g_ascii_isalnum(text[at+n]) || text[at+n]=='_'));}
static InsightCallSource *source(Builder *b,const gchar *path) {
 InsightCallSource *s=g_hash_table_lookup(b->r->sources,path);if(s || stopped(b) || g_hash_table_contains(b->missing,path)) return s;
 if(b->r->sources && g_hash_table_size(b->r->sources)>=512) {b->r->limited=TRUE;return NULL;}
 gchar *text=b->load(path,b->cancel,b->data);gsize length=text ? strlen(text):0;
 if(!text || length>SOURCE_LIMIT || !g_utf8_validate(text,(gssize)length,NULL)) {g_free(text);g_hash_table_add(b->missing,g_strdup(path));b->r->skipped++;return NULL;}
 if(b->bytes+length>TOTAL_LIMIT) {g_free(text);b->r->limited=TRUE;return NULL;}b->bytes+=length;
 s=g_new0(InsightCallSource,1);s->text=insight_call_normalize(text);g_free(text);length=strlen(s->text);const gchar *extension=strrchr(path,'.');InsightCodeDialect dialect=extension && (!g_ascii_strcasecmp(extension,".js") || !g_ascii_strcasecmp(extension,".ts")) ? INSIGHT_CODE_JS:INSIGHT_CODE_C;
 s->mask=insight_source_code_mask(s->text,length,dialect,NULL);s->digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,s->text,-1);s->lines=g_array_new(FALSE,FALSE,sizeof(gsize));gsize zero=0;g_array_append_val(s->lines,zero);for(gsize i=0;i<length;i++) if(s->text[i]=='\n') {gsize next=i+1;g_array_append_val(s->lines,next);}
 if(!s->mask) {source_free(s);g_hash_table_add(b->missing,g_strdup(path));b->r->skipped++;return NULL;}g_hash_table_insert(b->r->sources,g_strdup(path),s);return s;
}
static gboolean prepare(Builder *b,guint id) {
 InsightCallFunction *f=g_ptr_array_index(b->r->functions,id);if(f->checked) return f->valid;f->checked=TRUE;InsightCallSource *s=source(b,f->path);if(!s || !f->line || f->line>s->lines->len) return FALSE;
 f->begin=g_array_index(s->lines,gsize,f->line-1);gsize length=strlen(s->text);
 f->valid=insight_function_range(s->text,s->mask,length,f->begin,INSIGHT_FUNCTION_BRACES,&f->body,&f->end)==1;
 if(!f->valid) {b->r->skipped++;return FALSE;}
 gsize first=f->begin;while(first && f->begin-first<2048 && s->mask[first-1]!=';' && s->mask[first-1]!='}') first--;
 for(gsize p=first;p<f->body;p++) if(word_at(s->mask,f->body,p,"static")) {f->is_static=TRUE;break;}return TRUE;
}
static guint source_line(InsightCallSource *s,gsize offset) {guint lo=0,hi=s->lines->len;while(lo+1<hi) {guint mid=lo+(hi-lo)/2;if(g_array_index(s->lines,gsize,mid)<=offset) lo=mid;else hi=mid;}return lo+1;}
static gboolean endpoint(Builder *b,const InsightCallEndpoint *query,guint *id,GError **error) {
 guint count=0;gsize smallest=G_MAXSIZE;gchar *qualified=NULL;
 for(guint i=0;i<b->r->functions->len && !stopped(b);i++) {
  InsightCallFunction *f=g_ptr_array_index(b->r->functions,i);if(query->path && *query->path) {if(strcmp(query->path,f->path) || f->line>query->line) continue;}else {qualified=*f->scope ? g_strconcat(f->scope,"::",f->name,NULL):g_strdup(f->name);gboolean same=query->name && (!strcmp(query->name,f->name) || !strcmp(query->name,qualified));g_free(qualified);if(!same) continue;}
  if(!prepare(b,i)) continue;
  if(query->path && *query->path) {InsightCallSource *s=source(b,f->path);if(query->line>source_line(s,f->end-1)) continue;gsize size=f->end-f->begin;if(size>smallest) continue;if(size<smallest) count=0;smallest=size;}
  *id=i;count++;
 }
 if(stopped(b)) return FALSE;
 if(count!=1) {g_set_error_literal(error,G_IO_ERROR,count ? G_IO_ERROR_EXISTS:G_IO_ERROR_NOT_FOUND,count ? "Several matching functions; set the endpoint from a selected source node":"Function definition not found in the indexed project");return FALSE;}return TRUE;
}
static gboolean c_like(const gchar *path) {const gchar *dot=strrchr(path,'.');if(!dot) return FALSE;const gchar *ext[]={".c",".h",".cc",".cpp",".cxx",".hpp",".hh",".hxx",".cs",".java",".js",".ts"};for(guint i=0;i<G_N_ELEMENTS(ext);i++) if(!g_ascii_strcasecmp(dot,ext[i])) return TRUE;return FALSE;}
static void candidates(Builder *b,guint owner,const gchar *name,gboolean member,GArray *out) {
 GArray *ids=g_hash_table_lookup(b->names,name);if(!ids) {b->r->unresolved++;return;}InsightCallFunction *from=g_ptr_array_index(b->r->functions,owner);gboolean local=FALSE;
 for(guint i=0;i<ids->len;i++) {guint id=g_array_index(ids,guint,i);InsightCallFunction *f=g_ptr_array_index(b->r->functions,id);if(prepare(b,id) && !strcmp(f->path,from->path) && !strcmp(f->scope,from->scope)) local=TRUE;}
 for(guint i=0;i<ids->len;i++) {guint id=g_array_index(ids,guint,i);InsightCallFunction *f=g_ptr_array_index(b->r->functions,id);if(!prepare(b,id)) continue;gboolean same=!strcmp(f->path,from->path),scope=!strcmp(f->scope,from->scope) || !*f->scope;
  if(!member && ((local && (!same || strcmp(f->scope,from->scope))) || (!local && f->is_static && !same) || (!scope && !b->indirect))) continue;
  if(member && !b->indirect) continue;g_array_append_val(out,id);
 }
 if(out->len>1) {b->r->ambiguous++;if(!b->indirect) g_array_set_size(out,0);}if(!out->len) b->r->unresolved++;
}
static gint existing(InsightCallPaths *r,guint from,guint to) {for(guint i=r->edges->len;i>0;i--) {InsightCallEdge e=g_array_index(r->edges,InsightCallEdge,i-1);if(e.from!=from) break;if(e.to==to) return (gint)i-1;}return -1;}
static void expand(Builder *b,guint id,GArray *found) {
 if(!prepare(b,id)) return;InsightCallFunction *f=g_ptr_array_index(b->r->functions,id);InsightCallSource *s=source(b,f->path);gsize cursor=f->body,a=0,z=0;GArray *ids=g_array_new(FALSE,FALSE,sizeof(guint));
 while(insight_next_call(s->mask,f->end-1,&cursor,&a,&z)==1 && !stopped(b)) {
  if(++b->calls>200000) {b->r->limited=TRUE;b->halt=TRUE;break;}gsize previous=a;while(previous && g_ascii_isspace(s->mask[previous-1])) previous--;gboolean member=previous && (s->mask[previous-1]=='.' || s->mask[previous-1]=='>' || s->mask[previous-1]==':');
  gchar *name=g_strndup(s->mask+a,z-a);g_array_set_size(ids,0);candidates(b,id,name,member,ids);g_free(name);
  for(guint i=0;i<ids->len;i++) {guint to=g_array_index(ids,guint,i);gint old=existing(b->r,id,to);if(old>=0) continue;if(b->r->edges->len==INSIGHT_CALL_EDGE_LIMIT) {b->r->limited=TRUE;b->halt=TRUE;break;}
   guint line=source_line(s,a);InsightCallEdge edge={id,to,line,(guint)(a-g_array_index(s->lines,gsize,line-1)),member || ids->len>1 || (*((InsightCallFunction *)g_ptr_array_index(b->r->functions,to))->scope && strcmp(((InsightCallFunction *)g_ptr_array_index(b->r->functions,to))->scope,f->scope))};guint ei=b->r->edges->len;g_array_append_val(b->r->edges,edge);g_array_append_val(found,ei);
  }
  if(b->halt) break;
 }
 g_array_unref(ids);
}
static void collect_paths(InsightCallPaths *r,GPtrArray *predecessors,guint current,GArray *reverse) {
 if(r->paths->len>=INSIGHT_CALL_PATH_LIMIT) {r->limited=TRUE;return;}
 if(current==r->start) {GArray *path=g_array_new(FALSE,FALSE,sizeof(guint));for(guint i=reverse->len;i>0;i--) {guint e=g_array_index(reverse,guint,i-1);g_array_append_val(path,e);}g_ptr_array_add(r->paths,path);return;}
 GArray *pred=g_ptr_array_index(predecessors,current);for(guint i=0;pred && i<pred->len;i++) {guint e=g_array_index(pred,guint,i);InsightCallEdge edge=g_array_index(r->edges,InsightCallEdge,e);g_array_append_val(reverse,e);collect_paths(r,predecessors,edge.from,reverse);g_array_set_size(reverse,reverse->len-1);if(r->paths->len==INSIGHT_CALL_PATH_LIMIT) break;}
}
InsightCallPaths *insight_call_paths_find(const InsightIndex *index,GPtrArray *paths,const InsightCallEndpoint *start,const InsightCallEndpoint *target,gboolean indirect,InsightCallLoad load,gpointer data,GCancellable *cancel,GError **error) {
 g_return_val_if_fail(index && paths && start && target && load,NULL);InsightCallPaths *r=g_new0(InsightCallPaths,1);r->functions=g_ptr_array_new_with_free_func(function_free);r->paths=g_ptr_array_new_with_free_func((GDestroyNotify)g_array_unref);r->edges=g_array_new(FALSE,FALSE,sizeof(InsightCallEdge));r->sources=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,source_free);
 Builder b={r,g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)g_array_unref),g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL),load,data,cancel,0,0,indirect,FALSE};GHashTable *members=g_hash_table_new(g_str_hash,g_str_equal);for(guint i=0;i<paths->len;i++) g_hash_table_add(members,g_ptr_array_index(paths,i));
 gsize metadata=0;for(gsize i=0;i<insight_index_count(index) && !stopped(&b);i++) {const InsightSymbol *symbol=insight_index_at(index,i);if(!symbol->path || !symbol->name || !symbol->kind || (strcmp(symbol->kind,"function") && strcmp(symbol->kind,"method")) || !symbol->line || !g_hash_table_contains(members,symbol->path) || !c_like(symbol->path)) continue;
  if(symbol->line>G_MAXUINT || !g_utf8_validate(symbol->name,-1,NULL) || !g_utf8_validate(symbol->path,-1,NULL) || (symbol->scope && !g_utf8_validate(symbol->scope,-1,NULL))) {r->skipped++;continue;}
  gsize cost=strlen(symbol->name)+strlen(symbol->path)+(symbol->scope ? strlen(symbol->scope):0);if(cost>16384 || metadata+cost>16u*1024u*1024u) {r->limited=TRUE;break;}metadata+=cost;
  if(r->functions->len==INSIGHT_CALL_FUNCTION_LIMIT) {r->limited=TRUE;break;}InsightCallFunction *f=g_new0(InsightCallFunction,1);f->name=g_strdup(symbol->name);f->scope=g_strdup(symbol->scope ? symbol->scope:"");f->path=g_strdup(symbol->path);f->line=(guint)symbol->line;guint id=r->functions->len;g_ptr_array_add(r->functions,f);GArray *ids=g_hash_table_lookup(b.names,f->name);if(!ids) {ids=g_array_new(FALSE,FALSE,sizeof(guint));g_hash_table_insert(b.names,g_strdup(f->name),ids);}g_array_append_val(ids,id);
 }
 g_hash_table_unref(members);gboolean ok=endpoint(&b,start,&r->start,error) && endpoint(&b,target,&r->target,error);
 if(ok) {
  gint *distance=g_new(gint,r->functions->len);GPtrArray *pred=g_ptr_array_new_with_free_func((GDestroyNotify)g_array_unref);for(guint i=0;i<r->functions->len;i++) {distance[i]=-1;g_ptr_array_add(pred,g_array_new(FALSE,FALSE,sizeof(guint)));}GArray *queue=g_array_new(FALSE,FALSE,sizeof(guint)),*edges=g_array_new(FALSE,FALSE,sizeof(guint));distance[r->start]=0;g_array_append_val(queue,r->start);
  for(guint position=0;position<queue->len && !stopped(&b);position++) {guint id=g_array_index(queue,guint,position);if(distance[r->target]>=0 && distance[id]>=distance[r->target]) break;if(distance[id]>=INSIGHT_CALL_DEPTH_LIMIT) {r->limited=TRUE;continue;}g_array_set_size(edges,0);expand(&b,id,edges);
   for(guint i=0;i<edges->len;i++) {guint e=g_array_index(edges,guint,i);InsightCallEdge edge=g_array_index(r->edges,InsightCallEdge,e);if(distance[edge.to]<0) {distance[edge.to]=distance[id]+1;g_array_append_val(queue,edge.to);}if(distance[edge.to]==distance[id]+1) {GArray *incoming=g_ptr_array_index(pred,edge.to);g_array_append_val(incoming,e);}}
   if(b.halt) break;
  }
  if(distance[r->target]>=0) {GArray *reverse=g_array_new(FALSE,FALSE,sizeof(guint));collect_paths(r,pred,r->target,reverse);g_array_unref(reverse);}g_array_unref(queue);g_array_unref(edges);g_ptr_array_unref(pred);g_free(distance);
 }
 if(stopped(&b)) {if(!error || !*error) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_CANCELLED,"Call path search cancelled");ok=FALSE;}
 g_hash_table_unref(b.names);g_hash_table_unref(b.missing);if(!ok) {insight_call_paths_free(r);return NULL;}return r;
}
