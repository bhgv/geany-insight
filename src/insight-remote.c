/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Own SSH terminal sessions, remote-file transfers and their connection forms.
 * RU: Управляет терминалами SSH, передачей удалённых файлов и формами подключения. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "../insight/desktop/glib_compat.h"
#include "../insight/desktop/ssh_auth.h"
#include "document.h"
#include "geany.h"
#include "geanyobject.h"
#include "insight-ai.h"
#include "insight-help.h"
#include "insight-remote.h"
#include "insight-serial.h"
#include "insight-ssh-setup.h"
#include "insight-terminal-ui.h"
#include "insight-terminal.h"
#include "msgwindow.h"
#include "navqueue.h"
#include "project.h"
#include "ui_utils.h"
#include "utils.h"
#ifdef G_OS_WIN32
#include "win32.h"
#endif
#include "../insight/desktop/remote_credentials.h"
#include "../insight/desktop/remote_io.h"
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#ifndef G_OS_WIN32
#include <errno.h>
#include <fcntl.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif
typedef struct
{
    InsightRemote *remote;
    gchar *directory, *config, *ssh_config;
    gboolean auto_upload;
} RemoteRoot;
typedef enum
{
    OPEN_FILE,
    OPEN_FOLDER,
    PROJECT_READ,
    PROJECT_FOLDER,
    UPLOAD
} TransferKind;
typedef struct
{
    RemoteRoot *root;
    TransferKind kind;
    gchar *target, *snapshot, *auth, *origin;
    GKeyFile *project;
    GSubprocess *process;
    GCancellable *cancel;
} Transfer;
typedef struct _ScpJob ScpJob;
typedef struct
{
    gint refs;
    guint number;
    GtkWidget *page, *output, *input, *overlay, *stack, *chat;
    GSubprocess *process;
    GCancellable *cancel;
    gchar *auth, *endpoint;
    InsightRemote *remote;
    GQueue pending;
    gsize pending_bytes;
    gboolean writing;
} ShellSession;
/* EN: Retain session s and return it.
 * RU: Удерживает сеанс s и возвращает его. */
static ShellSession *
session_ref (ShellSession *s)
{
    g_atomic_int_inc (&s->refs);
    return s;
}
/* EN: Release session data and its owned terminal resources when its last reference ends.
 * RU: Освобождает сеанс data и ресурсы терминала после последней ссылки. */
static void
session_unref (gpointer data)
{
    ShellSession *s = data;
    if (!g_atomic_int_dec_and_test (&s->refs))
        return;
    if (s->chat)
        g_object_remove_weak_pointer (G_OBJECT (s->chat), (gpointer *)&s->chat);
    g_clear_object (&s->page);
    g_free (s->endpoint);
    insight_remote_free (s->remote);
    g_free (s);
}
static struct
{
    GtkWidget *page, *host, *user, *port, *identity, *ssh_config, *path, *status, *output, *input,
        *save_password, *automatic;
    GPtrArray *roots;
    GHashTable *upload_versions;
    GCancellable *snapshot_cancel;
    GQueue pending;
    Transfer *active;
    ScpJob *scp;
    GPtrArray *sessions;
    ShellSession *selected;
    GtkWidget *tabs, *connection, *files, *transfer_local, *transfer_remote, *transfer_mode, *transfer_folder,
        *transfer_status;
    gchar *config, *directory;
    gboolean closing, loading;
    gulong signals[4];
    gboolean (*workspace) (const char *);
} remote_ui;
static guint64 remote_epoch, upload_sequence;
static void profile_save (void);
static void transfer_run (Transfer *);
static void scp_cancel (void);
#include "insight-remote/connection.inc"
#include "insight-remote/roots.inc"
#include "insight-remote/scp.inc"
#include "insight-remote/uploads.inc"
#include "insight-remote/transport.inc"
#include "insight-remote/actions.inc"
