/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Validate/persist user names independently of runtime symbol resolution.
 * RU: Проверяет и сохраняет имена пользователя независимо от разрешения символов текущего процесса. */

/* EN: Free owned strings in the rule passed as data.
 * RU: Освобождает строки правила, переданного как data. */
static void native_name_rule_free(gpointer data)
{
    NativeNameRule *rule = data;
    g_free(rule->anchor);
    g_free(rule->name);
    g_free(rule->type);
    g_free(rule->scope);
    g_free(rule);
}

/* EN: Free owned range data and its display name/function scope.
 * RU: Освобождает диапазон data, его отображаемое имя и область функции. */
static void native_name_range_free(gpointer data)
{
    NativeNamedRange *range = data;
    g_free(range->name);
    g_free(range->scope);
    g_free(range->base);
    g_free(range);
}

/* EN: Test single-line UTF-8 value against limit; empty strings are permitted only with empty TRUE.
 * RU: Проверяет однострочный UTF-8 value по limit; пустая строка допускается только при empty TRUE. */
static gboolean native_name_text(const gchar *value, glong limit, gboolean empty)
{
    return value && (empty || *value) && strlen(value) <= (gsize)limit * 4 &&
        g_utf8_validate(value, -1, NULL) && g_utf8_strlen(value, -1) <= limit && !strpbrk(value, "\r\n");
}

/* EN: Parse a signed decimal/hex frame displacement text into offset, rejecting overflow/trailing data.
 * RU: Читает знаковое десятичное/hex смещение text в offset, отклоняя переполнение и лишние символы. */
static gboolean native_name_offset(const gchar *text, gint64 *offset)
{
    if (!text || !*text || g_ascii_isspace(*text))
        return FALSE;
    gboolean negative = *text == '-';
    const gchar *number = (*text == '-' || *text == '+') ? text + 1 : text;
    guint64 value = 0;
    guint base = g_str_has_prefix(number, "0x") ? 16 : 10;
    if (base == 16)
        number += 2;
    if (!g_ascii_string_to_unsigned(number, base, 0, G_MAXINT64, &value, NULL))
        return FALSE;
    *offset = negative ? -(gint64)value : (gint64)value;
    return TRUE;
}

/* EN: Return whether name is an explicit program-counter register; these values must not be used as frame bases.
 * RU: Проверяет, является ли name явным регистром счётчика команд; такие значения нельзя использовать как базы кадра. */
static gboolean native_name_pc_register(const gchar *name)
{
    return !g_strcmp0(name, "pc") || !g_strcmp0(name, "rip") || !g_strcmp0(name, "eip");
}

/* EN: Accept integer/address registers, including PC, for x86/x64, ARM/ARM64 and RISC-V; exclude vector/status registers.
 * RU: Принимает целочисленные/адресные регистры, включая PC, x86/x64, ARM/ARM64 и RISC-V; исключает векторы и флаги. */
static gboolean native_name_register(const gchar *name)
{
    if (!name)
        return FALSE;
    if (native_name_pc_register(name))
        return TRUE;
    const gchar *fixed[] = {"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
        "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp", "fp", "sp", "lr", "ra", "gp", "tp", "zero"};
    for (guint i = 0; i < G_N_ELEMENTS(fixed); i++)
        if (!strcmp(name, fixed[i]))
            return TRUE;
    if (strchr("rxast", name[0]) && name[0] && name[1])
    {
        guint64 number = 0;
        return g_ascii_string_to_unsigned(name + 1, 10, 0,
            name[0] == 'a' ? 7 : name[0] == 's' ? 11 : name[0] == 't' ? 6 : 31, &number, NULL);
    }
    return FALSE;
}

/* EN: Validate rule fields without executing expressions; names may be human-readable, types must be nonempty for kind 2.
 * RU: Проверяет поля rule без выполнения выражений; имена допускают обычный текст, тип обязателен для kind 2. */
static gboolean native_name_rule_valid(const NativeNameRule *rule)
{
    gint64 offset;
    return rule->kind <= 2 && native_name_text(rule->anchor, 512, FALSE) &&
        native_name_text(rule->name, 128, FALSE) && native_name_text(rule->type, 256, rule->kind != 2) &&
        native_name_text(rule->scope, 512, rule->kind != 1) &&
        (rule->kind != 1 || (native_name_offset(rule->anchor, &offset) &&
         (!*rule->type || (native_name_register(rule->type) && !native_name_pc_register(rule->type)))));
}

/* EN: Decode rule index from file into owned storage; return NULL when any required field is invalid.
 * RU: Читает правило index из file в собственную память; возвращает NULL при неверном обязательном поле. */
static NativeNameRule *native_name_rule_read(GKeyFile *file, guint index)
{
    gchar *group = g_strdup_printf("assembly-name-%u", index);
    NativeNameRule *rule = g_new0(NativeNameRule, 1);
    GError *error = NULL;
    gint kind = g_key_file_get_integer(file, group, "kind", &error);
    rule->kind = kind < 0 ? 3 : (guint)kind;
    rule->anchor = g_key_file_get_string(file, group, "anchor", NULL);
    rule->name = g_key_file_get_string(file, group, "name", NULL);
    rule->type = g_key_file_get_string(file, group, "type", NULL);
    rule->scope = g_key_file_get_string(file, group, "scope", NULL);
    gboolean valid = !error && native_name_rule_valid(rule);
    g_clear_error(&error);
    g_free(group);
    if (!valid)
    {
        native_name_rule_free(rule);
        rule = NULL;
    }
    return rule;
}

/* EN: Validate optional names in profile before applying any settings; error reports invalid data/limits.
 * RU: Проверяет необязательные имена profile до применения настроек; error сообщает неверные данные/лимиты. */
static gboolean native_name_profile_validate(GKeyFile *profile, GError **error)
{
    if (!g_key_file_has_group(profile, "assembly-names"))
        return TRUE;
    GError *failure = NULL;
    gint count = g_key_file_get_integer(profile, "assembly-names", "count", &failure);
    gboolean valid = !failure && count >= 0 && count <= 256;
    if (valid && g_key_file_has_key(profile, "assembly-names", "follow", NULL))
    {
        g_key_file_get_boolean(profile, "assembly-names", "follow", &failure);
        valid = failure == NULL;
    }
    if (valid && g_key_file_has_key(profile, "assembly-names", "bytes", NULL))
    {
        g_key_file_get_boolean(profile, "assembly-names", "bytes", &failure);
        valid = failure == NULL;
    }
    for (gint i = 0; valid && i < count; i++)
    {
        NativeNameRule *rule = native_name_rule_read(profile, (guint)i);
        valid = rule != NULL;
        if (rule)
            native_name_rule_free(rule);
    }
    g_clear_error(&failure);
    if (!valid)
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
            insight_tr("Invalid assembly names (maximum 256 rules)."));
    return valid;
}

/* EN: Replace the same rule identity in assembly with owned rule; a later explicit rename has priority.
 * RU: Заменяет правило с тем же ключом в assembly принадлежащим rule; новое явное имя имеет приоритет. */
static void native_name_rule_replace(NativeAssembly *assembly, NativeNameRule *rule)
{
    for (guint i = 0; i < assembly->rules->len;)
    {
        NativeNameRule *old = assembly->rules->pdata[i];
        gboolean same = old->kind == rule->kind && !g_strcmp0(old->anchor, rule->anchor) &&
            !g_strcmp0(old->scope, rule->scope) && (rule->kind != 1 || !g_strcmp0(old->type, rule->type));
        if (same)
            g_ptr_array_remove_index(assembly->rules, i);
        else
            i++;
    }
    g_ptr_array_add(assembly->rules, rule);
}
