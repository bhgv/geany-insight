/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_INDEX_H
#define INSIGHT_INDEX_H
#include <stddef.h>
typedef struct {
 const char *name, *path, *scope, *kind;
 unsigned long line; /* one-based */
 const char *signature,*type,*inheritance; /* optional on input; owned UTF-8-compatible strings in index */
} InsightSymbol;
typedef struct InsightIndex InsightIndex;
InsightIndex *insight_index_new(void);
/* Atomic lifetime management for immutable published snapshots. Updates still
 * require exclusive ownership. free releases one reference. */
InsightIndex *insight_index_ref(InsightIndex *);
/* Valid only with an owned reference; writers must prevent new publication. */
int insight_index_exclusive(const InsightIndex *);
void insight_index_free(InsightIndex *);
InsightIndex *insight_index_clone(const InsightIndex *);
#define INSIGHT_INDEX_SNAPSHOT_LIMIT (64u * 1024u * 1024u)
/* Version 2 (reads version 1), endian-independent snapshot with CRC32 corruption detection.
 * Root is metadata only; decoding never accesses paths from the snapshot.
 * Serialized bytes/root are malloc-owned; caller frees them. On failure,
 * output pointers are NULL and length is zero. Max 250k symbols, 64 KiB/field. */
int insight_index_serialize(const InsightIndex *,const char *root,unsigned char **data,size_t *length);
InsightIndex *insight_index_deserialize(const unsigned char *,size_t,char **root);
size_t insight_index_count(const InsightIndex *);
/* O(1) sorted ordinal access; borrowed until mutation, NULL out of range. */
const InsightSymbol *insight_index_at(const InsightIndex *,size_t);
size_t insight_index_file_count(const InsightIndex *);
/* Sorted unique paths represented by at least one symbol. Prefix byte match.
 * Borrowed paths; total independent of offset/capacity. SIZE_MAX on OOM. */
size_t insight_index_files(const InsightIndex *,const char *,size_t,const char **,size_t);
/* Atomically replace all symbols for path; zero entries deletes that file.
 * Strings are copied. Returns 0 for invalid input/allocation failure. */
int insight_index_update(InsightIndex *,const char *path,const InsightSymbol *,size_t);
/* Case-sensitive UTF-8 byte comparison; exact=0 selects prefix matching.
 * Returns total matches, writes at most capacity borrowed pointers to out.
 * Pointers remain valid until the next successful update/free. */
size_t insight_index_find(const InsightIndex *,const char *,int exact,
 const InsightSymbol **out,size_t capacity);
size_t insight_index_find_filtered(const InsightIndex *,const char *,int,const char *,const char *,const InsightSymbol **,size_t);
/* Exact path filter, optional scope substring and exact kind; total independent
 * of pagination. Results borrowed until index mutation. */
size_t insight_index_query(const InsightIndex *,const char *,int,const char *,const char *,const char *,size_t,const InsightSymbol **,size_t);
#endif
