/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Build bounded SSH setup commands without embedding credentials in
 * arguments. RU: Формирует ограниченные команды настройки SSH без паролей в
 * аргументах. */
#ifndef INSIGHT_SSH_SETUP_H
#define INSIGHT_SSH_SETUP_H
#include "remote_io.h"
gchar **insight_ssh_setup_argv(const InsightRemote *remote, const gchar *client, const gchar *config,
                               const gchar *command, gboolean interactive);
gchar *insight_ssh_public_key(const gchar *text, GError **error);
gchar *insight_ssh_install_key_command(const gchar *directory, GError **error);
gchar *insight_ssh_debug_check_command(const gchar *program, const gchar *directory, gboolean start_server,
                                       GError **error);
gchar *insight_ssh_check_command(const gchar *program, GError **error);
gchar *insight_ssh_server_command(const gchar *directory, guint port, GError **error);
#endif
