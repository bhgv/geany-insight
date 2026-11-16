/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Present confirmed GDB scheduler-locking policy and keep rejected changes out of the thread UI.
 * RU: Показывает подтверждённый режим scheduler-locking GDB и не применяет отвергнутые изменения в интерфейсе. */

/* EN: Validate mode against GDB's four legacy policies; NULL and unknown values return FALSE.
 * RU: Проверяет mode по четырём стандартным режимам GDB; NULL и неизвестные значения дают FALSE. */
static gboolean native_thread_mode_valid(const gchar *mode)
{
    return mode && (!strcmp(mode, "off") || !strcmp(mode, "step") ||
                    !strcmp(mode, "on") || !strcmp(mode, "replay"));
}

/* EN: Enable session's policy picker only with a confirmed value and an idle paused inferior.
 * RU: Включает выбор режима session только с подтверждённым значением и свободной целью на паузе. */
static void native_thread_policy_sync(NativeDebug *session)
{
    NativeThreadPolicy *policy = session->thread_policy;
    if (!policy)
        return;
    gtk_widget_set_sensitive(policy->combo, !session->closed && session->ready && session->stopped &&
        !session->running && !session->pending && !session->writing && !policy->pending &&
        native_thread_mode_valid(policy->mode) && (!session->assembly || !session->assembly->active));
}

/* EN: Return an owned show command when session needs its paused policy reread; otherwise return NULL.
 * RU: Возвращает команду show для перечитывания режима session на паузе; иначе возвращает NULL. */
static gchar *native_thread_policy_next(NativeDebug *session)
{
    NativeThreadPolicy *policy = session->thread_policy;
    if (!policy || session->closed || !session->ready || !session->stopped || session->running ||
        session->pending || session->writing || !policy->needed || policy->pending)
        return NULL;
    policy->needed = FALSE;
    policy->pending = 1;
    return g_strdup("-gdb-show scheduler-locking");
}

/* EN: Restore policy's confirmed picker value while suppressing changed callbacks.
 * RU: Восстанавливает подтверждённый выбор policy с подавлением обработчика changed. */
static void native_thread_policy_restore(NativeThreadPolicy *policy)
{
    policy->filling = TRUE;
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(policy->combo), policy->mode);
    policy->filling = FALSE;
}

/* EN: Apply record to session's outstanding show/set request; return TRUE only when consumed.
 * Failed sets keep the previous confirmed mode; failed shows make an unknown policy unselectable.
 * RU: Применяет record к ожидающему show/set session; TRUE означает обработанный ответ.
 * Отказ set сохраняет прежний режим; отказ show делает неизвестный режим недоступным для выбора. */
static gboolean native_thread_policy_reply(NativeDebug *session, const gchar *record)
{
    NativeThreadPolicy *policy = session->thread_policy;
    if (!policy || !policy->pending)
        return FALSE;
    guint request = policy->pending;
    policy->pending = 0;
    gboolean success = g_str_has_prefix(record, "^done");
    gchar *mode = request == 1 ? native_debug_field(record, "value") : g_strdup(policy->requested);
    success = success && native_thread_mode_valid(mode);
    if (success)
    {
        g_free(policy->mode);
        policy->mode = g_steal_pointer(&mode);
        gtk_label_set_text(GTK_LABEL(policy->status), insight_tr("Thread execution policy confirmed by GDB."));
    }
    else
    {
        if (request == 1)
            g_clear_pointer(&policy->mode, g_free);
        gchar *message = native_debug_field(record, "msg");
        gchar *valid = g_utf8_make_valid(message ? message :
            insight_tr("GDB did not confirm a supported thread execution policy."), -1);
        gtk_label_set_text(GTK_LABEL(policy->status), valid);
        native_debug_log(session, valid);
        g_free(valid);
        g_free(message);
    }
    native_thread_policy_restore(policy);
    g_clear_pointer(&policy->requested, g_free);
    g_free(mode);
    return TRUE;
}

/* EN: Request combo's new policy for session data; busy changes roll back without sending commands.
 * RU: Запрашивает новый режим combo для сеанса data; занятые изменения откатываются без команд. */
static void native_thread_policy_changed(GtkComboBox *combo, gpointer data)
{
    NativeDebug *session = data;
    NativeThreadPolicy *policy = session->thread_policy;
    if (policy->filling)
        return;
    const gchar *mode = gtk_combo_box_get_active_id(combo);
    if (session->closed || !session->ready || !session->stopped || session->running ||
        session->pending || session->writing || policy->pending ||
        (session->assembly && session->assembly->active) || !native_thread_mode_valid(policy->mode) ||
        !native_thread_mode_valid(mode))
    {
        native_thread_policy_restore(policy);
        return;
    }
    if (!g_strcmp0(mode, policy->mode))
        return;
    g_free(policy->requested);
    policy->requested = g_strdup(mode);
    policy->pending = 2;
    gchar *command = g_strdup_printf("-gdb-set scheduler-locking %s", mode);
    gtk_label_set_text(GTK_LABEL(policy->status), insight_tr("Waiting for GDB to confirm the thread policy…"));
    native_debug_send(session->window, command);
    g_free(command);
}

/* EN: Build session's current-session policy controls; read the actual setting on each stop and console change.
 * RU: Создаёт выбор режима текущего сеанса session; читает фактическое значение при остановках и командах консоли. */
static GtkWidget *native_thread_policy_widget(NativeDebug *session)
{
    NativeThreadPolicy *policy = session->thread_policy = g_new0(NativeThreadPolicy, 1);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new(insight_tr("Thread execution")), FALSE, FALSE, 0);
    policy->combo = gtk_combo_box_text_new();
    gtk_widget_set_name(policy->combo, "insight-gdb-thread-policy");
    const gchar *ids[] = {"off", "step", "on", "replay"};
    const gchar *labels[] = {"Allow other threads (off)", "Selected thread for Step Into / Over (step)",
                             "Selected thread only (on)", "GDB replay policy (replay)"};
    for (guint i = 0; i < G_N_ELEMENTS(ids); i++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(policy->combo), ids[i], insight_tr(labels[i]));
    gtk_widget_set_tooltip_text(policy->combo, insight_tr(
        "step locks other threads for Step Into/Over; Continue and Step Out allow them. on keeps other threads stopped, including on Continue."));
    gtk_widget_set_sensitive(policy->combo, FALSE);
    g_signal_connect(policy->combo, "changed", G_CALLBACK(native_thread_policy_changed), session);
    gtk_box_pack_start(GTK_BOX(row), policy->combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    policy->status = gtk_label_new(insight_tr("Pause to read the target's thread execution policy."));
    gtk_widget_set_name(policy->status, "insight-gdb-thread-policy-status");
    gtk_label_set_line_wrap(GTK_LABEL(policy->status), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(policy->status), 90);
    gtk_box_pack_start(GTK_BOX(box), policy->status, FALSE, FALSE, 0);
    return box;
}

/* EN: Free owned policy state; NULL is accepted and widgets are owned by their GTK parents.
 * RU: Освобождает состояние policy; допускает NULL, виджеты принадлежат родителям GTK. */
static void native_thread_policy_free(NativeThreadPolicy *policy)
{
    if (!policy)
        return;
    g_free(policy->mode);
    g_free(policy->requested);
    g_free(policy);
}
