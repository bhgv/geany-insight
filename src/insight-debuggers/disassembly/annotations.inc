/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Annotate address operands, constants and architecture-specific frame references without rewriting machine code.
 * RU: Подписывает адреса, константы и ссылки на кадр разных архитектур без изменения машинного кода. */

/* EN: Return the most specific named range covering address in function, or NULL; global_only excludes stack locals.
 * RU: Возвращает наиболее точный именованный диапазон address в function либо NULL; global_only исключает локальные данные. */
static const NativeNamedRange *native_name_lookup(NativeAssembly *assembly, guint64 address,
    const gchar *function, gboolean global_only)
{
    const NativeNamedRange *best = NULL;
    for (guint i = 0; i < assembly->ranges->len; i++)
    {
        NativeNamedRange *range = assembly->ranges->pdata[i];
        gboolean scoped = range->scope && *range->scope;
        if (range->frame || (scoped && (global_only || g_strcmp0(function, range->scope))))
            continue;
        if (address < range->address || address - range->address >= range->size)
            continue;
        if (!best || (range->manual && !best->manual) ||
            (range->manual == best->manual && range->size <= best->size))
            best = range;
    }
    return best;
}

/* EN: Return an owned symbolic name for address/range, preserving a nonzero byte offset.
 * RU: Возвращает имя address/range в собственной памяти с сохранением ненулевого байтового смещения. */
static gchar *native_name_label(const NativeNamedRange *range, guint64 address)
{
    return address == range->address ? g_strdup(range->name) :
        g_strdup_printf("%s+0x%" G_GINT64_MODIFIER "x", range->name, address - range->address);
}

/* EN: Append a unique readable label to notes, limiting one instruction to 16 annotations.
 * RU: Добавляет уникальную читаемую label в notes, не более 16 подписей на инструкцию. */
static void native_name_note(GPtrArray *notes, const gchar *label)
{
    if (notes->len >= 16)
        return;
    for (guint i = 0; i < notes->len; i++)
        if (!strcmp(notes->pdata[i], label))
            return;
    g_ptr_array_add(notes, g_strdup(label));
}

/* EN: Safely apply signed displacement offset to base, returning FALSE on address overflow.
 * RU: Безопасно применяет знаковое смещение offset к base; FALSE означает переполнение адреса. */
static gboolean native_name_displace(guint64 base, gint64 offset, guint64 *address)
{
    guint64 magnitude = offset < 0 ? (guint64)(-(offset + 1)) + 1 : (guint64)offset;
    if (offset < 0 ? base < magnitude : base > G_MAXUINT64 - magnitude)
        return FALSE;
    *address = offset < 0 ? base - magnitude : base + magnitude;
    return TRUE;
}

/* EN: Annotate one frame memory reference reg+offset with actual symbol/field names and function-scoped manual rules.
 * RU: Подписывает ссылку reg+offset настоящими именами/полями и ручными правилами текущей функции. */
static void native_name_frame(NativeAssembly *assembly, const gchar *function,
    const gchar *reg, gint64 offset, GPtrArray *notes)
{
    guint64 *base = g_hash_table_lookup(assembly->registers, reg), address = 0;
    if (base && native_name_displace(*base, offset, &address))
    {
        const NativeNamedRange *range = native_name_lookup(assembly, address, function, FALSE);
        if (range)
        {
            gchar *label = native_name_label(range, address);
            native_name_note(notes, label);
            g_free(label);
        }
    }
    for (guint i = 0; i < assembly->ranges->len; i++)
    {
        NativeNamedRange *range = assembly->ranges->pdata[i];
        if (!range->frame || g_strcmp0(range->scope, function) || range->offset != offset)
            continue;
        const gchar *wanted = range->base && *range->base ? range->base : "fp";
        guint64 *other = g_hash_table_lookup(assembly->registers, wanted);
        if (!strcmp(wanted, reg) || (base && other && *base == *other))
            native_name_note(notes, range->name);
    }
}

/* EN: Find x86/ARM bracket and RISC-V displacement(base) forms in instruction for function; omit indexed expressions.
 * RU: Ищет формы x86/ARM со скобками и RISC-V displacement(base) в instruction для function; пропускает индексацию. */
static void native_name_frame_operands(NativeAssembly *assembly, const gchar *instruction,
    const gchar *function, GPtrArray *notes)
{
    const gchar *patterns[] = {
        "\\[\\s*(rbp|ebp|rsp|esp|fp|sp|x29|r11|r7|s0)\\s*(?:([+-])\\s*(0x[0-9a-fA-F]+|[0-9]+)|,\\s*#?(-?(?:0x[0-9a-fA-F]+|[0-9]+)))?\\s*\\]",
        "(-?(?:0x[0-9a-fA-F]+|[0-9]+))\\((s0|fp|sp)\\)"
    };
    for (guint kind = 0; kind < G_N_ELEMENTS(patterns); kind++)
    {
        GRegex *regex = g_regex_new(patterns[kind], G_REGEX_OPTIMIZE, 0, NULL);
        GMatchInfo *match = NULL;
        g_regex_match(regex, instruction, 0, &match);
        while (g_match_info_matches(match))
        {
            gchar *reg = g_match_info_fetch(match, kind ? 2 : 1);
            gchar *first = g_match_info_fetch(match, kind ? 1 : 3);
            gchar *sign = kind ? g_strdup("") : g_match_info_fetch(match, 2);
            gchar *arm = kind ? g_strdup("") : g_match_info_fetch(match, 4);
            gchar *text = *arm ? g_strdup(arm) : *first ? g_strconcat(sign, first, NULL) : g_strdup("0");
            gint64 offset = 0;
            if (native_name_offset(text, &offset))
                native_name_frame(assembly, function, reg, offset, notes);
            g_free(text);
            g_free(arm);
            g_free(sign);
            g_free(first);
            g_free(reg);
            g_match_info_next(match, NULL);
        }
        g_match_info_free(match);
        g_regex_unref(regex);
    }
}

/* EN: Build owned notes for instruction in function, separating true addresses, immediates and frame offsets.
 * RU: Создаёт подписи instruction в function, различая настоящие адреса, непосредственные значения и смещения кадра. */
static gchar *native_name_instruction(NativeAssembly *assembly, const gchar *instruction, const gchar *function)
{
    GPtrArray *notes = g_ptr_array_new_with_free_func(g_free);
    native_name_frame_operands(assembly, instruction, function, notes);
    guint bracket = 0, symbol = 0;
    gboolean branch = g_str_has_prefix(instruction, "call") || *instruction == 'j' ||
        g_str_has_prefix(instruction, "b ") || g_str_has_prefix(instruction, "bl ") ||
        g_str_has_prefix(instruction, "b.") || g_str_has_prefix(instruction, "beq") ||
        g_str_has_prefix(instruction, "bne");
    for (const gchar *p = instruction; *p; p++)
    {
        if (*p == '[' || *p == '(')
            bracket++;
        if ((*p == ']' || *p == ')') && bracket)
            bracket--;
        if (*p == '<')
            symbol++;
        if (*p == '>' && symbol)
            symbol--;
        if (symbol || !g_ascii_isdigit(*p) || (p > instruction && (g_ascii_isalnum(p[-1]) || p[-1] == '_')))
            continue;
        gboolean hex = p[0] == '0' && p[1] == 'x';
        const gchar *end = p + (hex ? 2 : 0);
        while (hex ? g_ascii_isxdigit(*end) : g_ascii_isdigit(*end))
            end++;
        if (end == p || (hex && end == p + 2))
            continue;
        gchar *token = g_strndup(p, end - p);
        guint64 address = 0;
        const NativeNamedRange *range = native_assembly_number(token, &address) ?
            native_name_lookup(assembly, address, function, FALSE) : NULL;
        if (range)
        {
            gchar *label = native_name_label(range, address);
            native_name_note(notes, label);
            g_free(label);
        }
        else if (!bracket && *end != '(' && !branch && (p == instruction || p[-1] != '<'))
        {
            const gchar *suffix = end;
            while (g_ascii_isspace(*suffix))
                suffix++;
            gchar *label = g_strdup_printf(*suffix == '<' ? "address %s" : "const %s", token);
            native_name_note(notes, label);
            g_free(label);
        }
        g_free(token);
        p = end - 1;
    }
    GString *text = g_string_new("");
    for (guint i = 0; i < notes->len; i++)
        g_string_append_printf(text, "%s%s", i ? "; " : "", (gchar *)notes->pdata[i]);
    g_ptr_array_unref(notes);
    return g_string_free(text, FALSE);
}

/* EN: Append global names to hexadecimal addresses in session's display text; return an owned copy even without names.
 * RU: Добавляет глобальные имена к hex-адресам в отображаемом text сеанса session; всегда возвращает собственную копию. */
static gchar *native_name_addresses(NativeDebug *session, const gchar *text)
{
    NativeAssembly *assembly = session->assembly;
    if (!assembly || !text || !assembly->ranges->len)
        return g_strdup(text ? text : "");
    GString *output = g_string_new("");
    for (const gchar *p = text; *p; p++)
    {
        if (p[0] == '0' && p[1] == 'x' && (p == text || !g_ascii_isalnum(p[-1])))
        {
            const gchar *end = p + 2;
            while (g_ascii_isxdigit(*end))
                end++;
            gchar *token = g_strndup(p, end - p);
            guint64 address = 0;
            const NativeNamedRange *range = native_assembly_number(token, &address) ?
                native_name_lookup(assembly, address, NULL, TRUE) : NULL;
            g_string_append(output, token);
            if (range)
            {
                gchar *label = native_name_label(range, address);
                g_string_append_printf(output, " <%s>", label);
                g_free(label);
            }
            g_free(token);
            p = end - 1;
        }
        else
            g_string_append_c(output, *p);
        if (output->len > 262144)
            break;
    }
    return g_string_free(output, FALSE);
}

/* EN: Decorate validated MI record with assembly's resolved names; preserve all original instructions and numeric addresses.
 * RU: Дополняет проверенный record разрешёнными именами assembly, сохраняя исходные инструкции и числовые адреса. */
static gchar *native_assembly_render(NativeDebug *session, const gchar *record)
{
    gchar *raw = native_assembly_format(record);
    if (!raw)
        return NULL;
    gchar **lines = g_strsplit(raw, "\n", 258);
    const gchar *cursor = record + strlen("^done,asm_insns=["), *start, *end;
    GString *output = g_string_new("");
    guint row = 0;
    while (row < 256 && lines[row] && (end = native_next_tuple(&cursor, &start)))
    {
        gchar *tuple = g_strndup(start, end - start);
        gchar *instruction = native_debug_field(tuple, "inst");
        gchar *function = native_debug_field(tuple, "func-name");
        gchar *notes = native_name_instruction(session->assembly, instruction,
            function ? function : session->assembly->function);
        gchar *named = native_name_addresses(session, lines[row++]);
        g_string_append_printf(output, "%s%s%s\n", named, *notes ? "  ; " : "", notes);
        g_free(named);
        g_free(notes);
        g_free(function);
        g_free(instruction);
        g_free(tuple);
    }
    g_strfreev(lines);
    g_free(raw);
    return g_string_free(output, FALSE);
}
