/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Present one SSH/key setup wizard for terminals and remote debugging.
 * RU: Предоставляет единый мастер SSH/ключей для терминалов и удалённой
 * отладки. */
#ifndef INSIGHT_SSH_SETUP_UI_H
#define INSIGHT_SSH_SETUP_UI_H
#include <gtk/gtk.h>
typedef struct
{
    const gchar *host, *user, *identity, *config;
    const gchar *program, *directory, *local_source, *remote_source;
    guint port, server_port;
    gboolean remember, start_server;
} InsightSshSetup;
typedef void (*InsightSshSetupDone) (const InsightSshSetup *settings, gpointer data);
GtkWidget *insight_ssh_setup_show (GtkWindow *parent, const gchar *configdir, const gchar *client_bin,
                                   const InsightSshSetup *initial, gboolean debugging,
                                   InsightSshSetupDone done, gpointer data);
#endif
