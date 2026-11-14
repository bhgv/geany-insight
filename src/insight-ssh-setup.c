/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Implement a cancellable SSH setup wizard with key enrollment and
 * connection verification. RU: Реализует отменяемый мастер SSH с настройкой
 * ключа и проверкой соединения. */
#include "insight-ssh-setup.h"
#include "../insight/desktop/glib_compat.h"
#include "../insight/desktop/remote_credentials.h"
#include "../insight/desktop/ssh_auth.h"
#include "../insight/desktop/ssh_setup.h"
#include "insight-i18n.h"
#include <glib/gstdio.h>
#include <string.h>

typedef enum
{
    SETUP_GENERATE,
    SETUP_PUBLIC,
    SETUP_INSTALL,
    SETUP_TEST
} SetupJob;
typedef struct
{
    GtkWidget *window, *stack, *heading, *status, *next, *back;
    GtkWidget *host, *user, *port, *identity, *password, *remember, *config;
    GtkWidget *public_text, *program, *directory, *local_source, *remote_source, *server_port, *start_server;
    GtkWidget *summary;
    gchar *configdir, *client_bin, *public_key, *auth, *key_stage, *key_destination;
    GSubprocess *process;
    GCancellable *cancel;
    InsightSshSetupDone done;
    gpointer data;
    guint page, timer;
    gboolean debugging, verified, closed, timed_out;
} SshSetupWizard;
typedef struct
{
    GSubprocess *process;
    gchar *input;
    SetupJob kind;
} SetupTask;
static void setup_update(SshSetupWizard *wizard);
static void setup_job_start(SshSetupWizard *wizard, SetupJob kind);
static gboolean setup_collect(SshSetupWizard *wizard, InsightSshSetup *settings, GError **error);
#include "insight-ssh-setup/jobs.inc"
#include "insight-ssh-setup/keys.inc"
#include "insight-ssh-setup/pages.inc"
#include "insight-ssh-setup/lifecycle.inc"
