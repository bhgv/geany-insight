/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_NOTEBOOK_STORE_H
#define INSIGHT_NOTEBOOK_STORE_H
#include <glib.h>
typedef enum { INSIGHT_BOOKMARK=1,INSIGHT_CLIP=2 } InsightNoteKind;
typedef struct { gchar *name,*path,*text; guint64 line; InsightNoteKind kind; } InsightNote;
typedef struct { GPtrArray *items; } InsightNotes;
InsightNotes *insight_notes_new(void);
void insight_notes_free(InsightNotes *);
gboolean insight_notes_set(InsightNotes *,InsightNoteKind,const char *,const char *,guint64,const char *,GError **);
gboolean insight_notes_remove(InsightNotes *,InsightNoteKind,const char *);
const InsightNote *insight_notes_get(const InsightNotes *,InsightNoteKind,const char *);
gboolean insight_notes_save(const InsightNotes *,const char *,GError **);
InsightNotes *insight_notes_load(const char *,GError **);
/* Return an owning snapshot; never modify either input. Import rejects name
 * collisions instead of silently replacing existing notes. */
InsightNotes *insight_notes_select(const InsightNotes *,InsightNoteKind,GError **);
InsightNotes *insight_notes_merge(const InsightNotes *,const InsightNotes *,InsightNoteKind,GError **);
InsightNotes *insight_notes_rename(const InsightNotes *,InsightNoteKind,const char *,const char *,GError **);
gboolean insight_bookmark_resolve(const InsightNote *,const char *,gsize,guint64 *,gboolean *);
#endif
