/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_INDEX_H
#define INSIGHT_INDEX_H
#include <stddef.h>
typedef struct {
 const char *name, *path, *scope, *kind;
 unsigned long line; /* one-based */
} InsightSymbol;
typedef struct InsightIndex InsightIndex;
InsightIndex *insight_index_new(void);
void insight_index_free(InsightIndex *);
InsightIndex *insight_index_clone(const InsightIndex *);
#define INSIGHT_INDEX_SNAPSHOT_LIMIT (64u * 1024u * 1024u)
/* Versioned, endian-independent snapshot with CRC32 corruption detection.
 * Root is metadata only; decoding never accesses paths from the snapshot.
 * Serialized bytes/root are malloc-owned; caller frees them. On failure,
 * output pointers are NULL and length is zero. Max 250k symbols, 64 KiB/field. */
int insight_index_serialize(const InsightIndex *,const char *root,unsigned char **data,size_t *length);
InsightIndex *insight_index_deserialize(const unsigned char *,size_t,char **root);
size_t insight_index_count(const InsightIndex *);
/* Atomically replace all symbols for path; zero entries deletes that file.
 * Strings are copied. Returns 0 for invalid input/allocation failure. */
int insight_index_update(InsightIndex *,const char *path,const InsightSymbol *,size_t);
/* Case-sensitive UTF-8 byte comparison; exact=0 selects prefix matching.
 * Returns total matches, writes at most capacity borrowed pointers to out.
 * Pointers remain valid until the next successful update/free. */
size_t insight_index_find(const InsightIndex *,const char *,int exact,
 const InsightSymbol **out,size_t capacity);
size_t insight_index_find_filtered(const InsightIndex *,const char *,int,const char *,const char *,const InsightSymbol **,size_t);
#endif
