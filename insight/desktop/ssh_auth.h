/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Share private SSH credentials and askpass sessions between terminals,
 * setup and debuggers. RU: Объединяет закрытые данные SSH и сеансы askpass
 * терминалов, мастера и отладчиков. */
#ifndef INSIGHT_SSH_AUTH_H
#define INSIGHT_SSH_AUTH_H
#include <gio/gio.h>
gchar *insight_ssh_auth_target (const gchar *host, const gchar *user, guint port);
gchar *insight_ssh_auth_key_target (const gchar *identity);
gchar *insight_ssh_auth_path (const gchar *configdir);
gchar *insight_ssh_auth_prepare (GSubprocessLauncher *launcher, const gchar *configdir, const gchar *host,
                                 const gchar *user, guint port, const gchar *identity, const gchar *helper,
                                 gboolean remember, GError **error);
gboolean insight_ssh_auth_seed (GSubprocessLauncher *launcher, const gchar *password, GError **error);
void insight_ssh_auth_cleanup (const gchar *directory);
void insight_ssh_auth_migrate (const gchar *legacy, const gchar *configdir, const gchar *host,
                               const gchar *user, guint port);
gboolean insight_ssh_auth_forget (const gchar *configdir, const gchar *host, const gchar *user, guint port,
                                  const gchar *identity, GError **error);
gboolean insight_ssh_auth_migrate_key (GKeyFile *key, const gchar *configdir, GError **error);
#endif
