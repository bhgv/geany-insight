/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Own the SSH tunnel, connection form and remote GDB request state.
 * RU: Хранит SSH-туннель, форму подключения и состояние удалённых запросов GDB. */

typedef enum
{
    NATIVE_REMOTE_NONE, NATIVE_REMOTE_SYSROOT, NATIVE_REMOTE_CONNECT, NATIVE_REMOTE_CLEAR_STOP, NATIVE_REMOTE_ATTACH, NATIVE_REMOTE_DETACH, NATIVE_REMOTE_DISCONNECT
} NativeRemoteRequest;

typedef struct
{
    GtkWidget *mode, *form, *host, *user, *port, *identity, *config;
    GtkWidget *server_host, *server_port, *executable, *remote_source, *local_source, *sysroot;
    GtkWidget *connect, *disconnect, *status, *profile_controls;
    GtkWidget *profile_save, *profile_restore, *profile_forget, *pid, *attach;
    GSubprocess *ssh;
    GDataInputStream *output, *errors;
    GCancellable *cancel;
    guint timer, local_port, attached_pid;
    gboolean interrupt_pending, pause_requested, clear_stop_signal, release_pending, own_continue_pending;
    gboolean busy, connected, extended, detach_needed, disconnect_needed;
    NativeRemoteRequest pending;
    gchar *source_mapping, *original_sysroot, *profile_path;
} NativeRemote;
