/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_REMOTE_IO_H
#define INSIGHT_REMOTE_IO_H
#include <gio/gio.h>
typedef struct { gchar *host,*user,*identity,*remote,*local; guint port; } InsightRemote;
InsightRemote *insight_remote_new(const char *,const char *,guint,const char *,const char *,const char *,GError **);
void insight_remote_free(InsightRemote *);
/* Resolve bundled clients before PATH; pass the absolute result to spawnv. */
gchar *insight_remote_program(const gchar *program,const gchar *directory);
gchar **insight_remote_argv(const InsightRemote *,gboolean);
gchar *insight_remote_download(const char *,const char *,gboolean,GError **);
gchar *insight_remote_upload(const char *,const char *,const char *,GError **);
gchar *insight_remote_path(const InsightRemote *,const char *);
gchar *insight_remote_normalize(const char *,const char *);
#endif
