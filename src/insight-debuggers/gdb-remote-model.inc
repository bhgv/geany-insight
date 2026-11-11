/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Construct SSH forwarding arguments and MI source-mapping commands without a local shell.
 * RU: Создаёт аргументы SSH-туннеля и команды сопоставления исходников MI без локальной оболочки. */

/* EN: Accept a single host/alias in host; reject options, shell syntax and whitespace.
 * RU: Принимает один адрес/псевдоним host; отклоняет опции, синтаксис оболочки и пробелы. */
static gboolean native_remote_host_valid(const gchar *host)
{
    return host && *host && (g_ascii_isalnum(*host) || *host == ':') &&
        strspn(host, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._:-") == strlen(host);
}

/* EN: Return owned SSH argv for validated endpoints; optional user, identity/config are separate arguments.
 * RU: Возвращает argv SSH для проверенных адресов; user, identity/config передаются отдельными аргументами. */
static gchar **native_remote_argv(const gchar *ssh, const gchar *host, const gchar *user, guint port,
                                  const gchar *identity, const gchar *config, const gchar *server_host,
                                  guint server_port, guint local_port, gboolean askpass)
{
    if (!native_remote_host_valid(host) || !native_remote_host_valid(server_host) ||
        port > 65535 || !server_port || server_port > 65535 || !local_port || local_port > 65535)
        return NULL;
    GPtrArray *args = g_ptr_array_new_with_free_func(g_free);
    const gchar *options[] = {ssh, "-T", "-oExitOnForwardFailure=yes", "-oConnectTimeout=15",
        "-oServerAliveInterval=10", "-oServerAliveCountMax=3", "-oClearAllForwardings=no",
        "-oControlMaster=no", "-oControlPath=none", "-oRemoteCommand=none"};
    for (guint i = 0; i < G_N_ELEMENTS(options); i++)
        g_ptr_array_add(args, g_strdup(options[i]));
    if (!askpass)
        g_ptr_array_add(args, g_strdup("-oBatchMode=yes"));
    if (*config)
    {
        g_ptr_array_add(args, g_strdup("-F"));
        g_ptr_array_add(args, g_strdup(config));
    }
    if (port)
    {
        g_ptr_array_add(args, g_strdup("-p"));
        g_ptr_array_add(args, g_strdup_printf("%u", port));
    }
    if (*user)
    {
        g_ptr_array_add(args, g_strdup("-l"));
        g_ptr_array_add(args, g_strdup(user));
    }
    if (*identity)
    {
        g_ptr_array_add(args, g_strdup("-i"));
        g_ptr_array_add(args, g_strdup(identity));
    }
    gchar *address = strchr(server_host, ':') ? g_strdup_printf("[%s]", server_host) : g_strdup(server_host);
    g_ptr_array_add(args, g_strdup("-L"));
    g_ptr_array_add(args, g_strdup_printf("127.0.0.1:%u:%s:%u", local_port, address, server_port));
    g_free(address);
    g_ptr_array_add(args, g_strdup("--"));
    g_ptr_array_add(args, g_strdup(host));
    /* EN: Confirm forwarding without probing GDB, then accept only bounded pause:PID requests.
     * RU: Подтверждает туннель без проверки порта GDB, затем принимает только ограниченные запросы pause:PID. */
    g_ptr_array_add(args, g_strdup(
        "printf 'GEANY-GDB-SSH-READY\\n'; "
        "while IFS= read -r request; do "
        "case \"$request\" in pause:*) pid=${request#pause:};; *) continue;; esac; "
        "case \"$pid\" in ''|0|*[!0-9]*) continue;; esac; "
        "[ ${#pid} -le 10 ] && [ \"$pid\" -ge 1 ] && [ \"$pid\" -le 2147483647 ] || continue; "
        "if kill -STOP \"$pid\"; then printf 'GEANY-GDB-SSH-STOPPED\\n'; "
        "else printf 'GEANY-GDB-SSH-STOP-FAILED\\n'; fi; done"));
    g_ptr_array_add(args, NULL);
    return (gchar **)g_ptr_array_free(args, FALSE);
}

/* EN: Return an owned MI console command for CLI text, preserving quoting and path boundaries.
 * RU: Возвращает команду консоли MI для text, сохраняя кавычки и границы путей. */
static gchar *native_remote_console(const gchar *text)
{
    gchar *escaped = g_strescape(text, NULL);
    gchar *command = g_strdup_printf("-interpreter-exec console \"%s\"", escaped);
    g_free(escaped);
    return command;
}

/* EN: Queue substitution of remote with local in commands; a NULL local removes only that mapping.
 * RU: Ставит замену remote на local в commands; local=NULL удаляет только это сопоставление. */
static void native_remote_mapping(GQueue *commands, const gchar *remote, const gchar *local)
{
    gchar *from = g_strescape(remote, NULL);
    gchar *to = local ? g_strescape(local, NULL) : NULL;
    gchar *text = local ? g_strdup_printf("set substitute-path \"%s\" \"%s\"", from, to) :
                          g_strdup_printf("unset substitute-path \"%s\"", from);
    g_queue_push_tail(commands, native_remote_console(text));
    g_free(text);
    g_free(to);
    g_free(from);
}

/* EN: Set a filename/string setting using GDB CLI's complete remainder, with MI escaping only outside it.
 * setting must be sysroot or remote exec-file; value may contain spaces but no line breaks. Caller owns the result.
 * RU: Устанавливает строковый/файловый параметр через остаток команды CLI; экранирование MI применяется снаружи.
 * setting — sysroot либо remote exec-file; value допускает пробелы, но не переводы строк. Результат принадлежит вызывающему. */
static gchar *native_remote_setting(const gchar *setting, const gchar *value)
{
    if (!value || strpbrk(value, "\r\n") ||
        (strcmp(setting, "sysroot") && strcmp(setting, "remote exec-file")))
        return NULL;
    gchar *text = g_strdup_printf("set %s %s", setting, value);
    gchar *command = native_remote_console(text);
    g_free(text);
    return command;
}
