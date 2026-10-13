/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_REMOTE_CREDENTIALS_H
#define INSIGHT_REMOTE_CREDENTIALS_H
#include <gio/gio.h>
gchar *insight_remote_password_load(const char *,const char *,GError **);
gboolean insight_remote_password_save(const char *,const char *,const char *,GError **);
gboolean insight_remote_password_forget(const char *,const char *,GError **);
#endif
