#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Exercise production scheduler-policy confirmation, rollback and busy guards with real GLib and fake controls.
# RU: Проверяет подтверждение режима потоков, откат и запрет занятых действий настоящим GLib с фиктивными виджетами.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import DECLARATIONS, function

STATE = r'''
extern int g_utf8_validate(const char *, gssize, const char **);
typedef void GtkWidget;
typedef void GtkComboBox;
typedef void *gpointer;
typedef struct {gboolean active;} NativeAssembly;
typedef struct {
    GtkWidget *combo, *status;
    gchar *mode, *requested;
    guint pending;
    gboolean filling, needed;
} NativeThreadPolicy;
typedef struct {
    NativeThreadPolicy *thread_policy;
    NativeAssembly *assembly;
    GtkWidget *window;
    gboolean closed, ready, stopped, running, pending, writing;
} NativeDebug;
#define TRUE 1
#define GTK_COMBO_BOX(value) (value)
#define GTK_LABEL(value) (value)
#define g_clear_pointer(p,f) do {f(*(p)); *(p)=NULL;} while(0)
#define g_steal_pointer(p) __extension__ ({void *v=*(p); *(p)=NULL; v;})
static NativeDebug *active;
static gchar *picked, *label, *logged, *sent;
static guint send_count;
static gboolean sensitive;
/* EN: Return the borrowed selection of combo; no actual GTK display is constructed.
 * RU: Возвращает заимствованный выбор combo; настоящий дисплей GTK не создаётся. */
static const gchar *gtk_combo_box_get_active_id(GtkComboBox *combo)
{
    (void)combo;
    return picked;
}
/* EN: Restore combo to id only under callback suppression; NULL clears its unknown value.
 * RU: Восстанавливает combo в id только при подавленных обработчиках; NULL очищает неизвестное значение. */
static gboolean gtk_combo_box_set_active_id(GtkComboBox *combo, const gchar *id)
{
    (void)combo;
    assert(active->thread_policy->filling);
    g_free(picked);
    picked = g_strdup(id);
    return TRUE;
}
/* EN: Record sensitivity for widget without creating a GTK widget.
 * RU: Запоминает enabled для widget без создания виджета GTK. */
static void gtk_widget_set_sensitive(GtkWidget *widget, gboolean enabled)
{
    (void)widget;
    sensitive = enabled;
}
/* EN: Record valid UTF-8 text for widget as the current policy explanation.
 * RU: Запоминает корректный UTF-8 text для widget как текущее пояснение режима. */
static void gtk_label_set_text(GtkWidget *widget, const gchar *text)
{
    (void)widget;
    assert(g_utf8_validate(text, -1, NULL));
    g_free(label);
    label = g_strdup(text);
}
/* EN: Record a diagnostic text for session without opening an editor.
 * RU: Запоминает диагностический text для session без открытия редактора. */
static void native_debug_log(NativeDebug *session, const gchar *text)
{
    (void)session;
    g_free(logged);
    logged = g_strdup(text);
}
/* EN: Record dispatched command for window; this fixture never executes a target.
 * RU: Запоминает отправленную command для window; фикстура не исполняет программу. */
static void native_debug_send(GtkWidget *window, const gchar *command)
{
    (void)window;
    g_free(sent);
    sent = g_strdup(command);
    send_count++;
}
'''
MAIN = r'''
/* EN: Run the complete read/set/reject/guard state machine; no arguments are used.
 * RU: Выполняет автомат чтения/изменения/отказа/запрета действий; аргументы не нужны. */
int main(void)
{
    NativeThreadPolicy policy = {.needed = TRUE};
    NativeAssembly assembly = {0};
    NativeDebug session = {.thread_policy = &policy, .assembly = &assembly, .ready = TRUE, .stopped = TRUE};
    active = &session;
    native_thread_policy_sync(&session);
    assert(!sensitive);
    session.stopped = FALSE;
    assert(!native_thread_policy_next(&session) && policy.needed && !policy.pending);
    session.stopped = TRUE;
    gchar *command = native_thread_policy_next(&session);
    assert(command && !strcmp(command, "-gdb-show scheduler-locking") && policy.pending == 1 && !policy.needed);
    g_free(command);
    assert(!native_thread_policy_next(&session));
    assert(native_thread_policy_reply(&session, "^done,value=\"replay\""));
    assert(!strcmp(policy.mode, "replay") && !strcmp(picked, "replay"));
    native_thread_policy_sync(&session);
    assert(sensitive);
    native_thread_policy_changed(NULL, &session);
    assert(!send_count);
    policy.pending = 1;
    g_free(picked);
    picked = g_strdup("step");
    native_thread_policy_changed(NULL, &session);
    assert(!send_count && !strcmp(picked, "replay"));
    policy.pending = 0;
    g_free(picked);
    picked = g_strdup("step");
    native_thread_policy_changed(NULL, &session);
    assert(send_count == 1 && policy.pending == 2 && !strcmp(sent, "-gdb-set scheduler-locking step"));
    assert(!strcmp(policy.mode, "replay"));
    native_thread_policy_reply(&session, "^done");
    assert(!strcmp(policy.mode, "step") && !strcmp(picked, "step"));
    g_free(picked);
    picked = g_strdup("on");
    native_thread_policy_changed(NULL, &session);
    native_thread_policy_reply(&session, "^error,msg=\"Target does not support scheduler locking\"");
    assert(send_count == 2 && !strcmp(policy.mode, "step") && !strcmp(picked, "step"));
    assert(strstr(label, "does not support") && strstr(logged, "does not support"));
    gboolean *busy[] = {&session.closed, &session.running, &session.pending, &session.writing, &assembly.active};
    for (guint i = 0; i < G_N_ELEMENTS(busy); i++)
    {
        *busy[i] = TRUE;
        g_free(picked);
        picked = g_strdup("on");
        native_thread_policy_changed(NULL, &session);
        native_thread_policy_sync(&session);
        assert(send_count == 2 && !sensitive && !strcmp(picked, "step"));
        *busy[i] = FALSE;
    }
    gboolean *inactive[] = {&session.ready, &session.stopped};
    for (guint i = 0; i < G_N_ELEMENTS(inactive); i++)
    {
        *inactive[i] = FALSE;
        g_free(picked);
        picked = g_strdup("off");
        native_thread_policy_changed(NULL, &session);
        assert(send_count == 2 && !strcmp(picked, "step"));
        *inactive[i] = TRUE;
    }
    g_free(picked);
    picked = g_strdup("on; quit");
    native_thread_policy_changed(NULL, &session);
    assert(send_count == 2 && !strcmp(picked, "step"));
    policy.pending = 1;
    native_thread_policy_reply(&session, "^done,value=\"unknown\"");
    native_thread_policy_sync(&session);
    assert(!policy.mode && !picked && !sensitive);
    picked = g_strdup("on");
    native_thread_policy_changed(NULL, &session);
    assert(send_count == 2 && !picked);
    policy.pending = 1;
    native_thread_policy_reply(&session, "^error,msg=\"bad\\377\"");
    assert(g_utf8_validate(label, -1, NULL));
    assert(!native_thread_policy_reply(&session, "^done"));
    g_free(policy.mode);
    g_free(policy.requested);
    g_free(picked);
    g_free(label);
    g_free(logged);
    g_free(sent);
    return 0;
}
'''


class NativeThreadPolicy(unittest.TestCase):
    # EN: Compile the production policy parser/state machine against real GLib and reject incorrect transitions.
    # RU: Компилирует настоящий разборщик/автомат режима с GLib и отклоняет неверные переходы.
    def test_confirmation_and_rollback(self):
        library = ctypes.util.find_library('glib-2.0')
        if not library or not shutil.which('cc'):
            self.skipTest('Needs cc and GLib runtime.')
        prefix = DECLARATIONS[:DECLARATIONS.index('typedef struct {\n    GtkTreeStore *all_stack_rows;')]
        source = prefix + STATE + function('src/insight-debuggers/gdb-context.inc', 'native_debug_field')
        for name in ('native_thread_mode_valid', 'native_thread_policy_sync', 'native_thread_policy_next',
                     'native_thread_policy_restore', 'native_thread_policy_reply', 'native_thread_policy_changed'):
            source += function('src/insight-debuggers/gdb-thread-policy.inc', name)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'policy.c'
            path.write_text(source + MAIN)
            executable = path.with_suffix('')
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', str(path),
                            '-o', str(executable), '-l:' + library], check=True)
            subprocess.run([str(executable)], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
