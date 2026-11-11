/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Resolve real debug symbols, frame registers and typed fields through bounded GDB/MI requests.
 * RU: Разрешает настоящие символы, регистры кадра и поля типов ограниченными запросами GDB/MI. */

/* EN: Release the queued MI work passed as data and all its owned strings.
 * RU: Освобождает задание MI data и все принадлежащие ему строки. */
static void native_assembly_work_free(gpointer data)
{
    NativeAssemblyWork *work = data;
    g_free(work->command);
    g_free(work->expression);
    g_free(work->label);
    g_free(work->scope);
    g_free(work->object);
    g_free(work->reply);
    g_free(work);
}

/* EN: Queue owned command for assembly with action/context strings copied from expression, label, scope and object.
 * RU: Ставит принадлежащую command в assembly с action и копиями контекста expression, label, scope, object. */
static void native_assembly_queue(NativeAssembly *assembly, gchar *command, guint action,
    const gchar *expression, const gchar *label, const gchar *scope, const gchar *object, guint depth, gboolean manual)
{
    NativeAssemblyWork *work = g_new0(NativeAssemblyWork, 1);
    work->command = command;
    work->action = action;
    work->expression = g_strdup(expression);
    work->label = g_strdup(label);
    work->scope = g_strdup(scope);
    work->object = g_strdup(object);
    work->depth = depth;
    work->manual = manual;
    g_queue_push_tail(&assembly->work, work);
}

/* EN: Return an owned quoted MI command for verb and expression; escaping prevents MI command injection.
 * RU: Возвращает экранированную команду MI для verb и expression, исключая внедрение команд MI. */
static gchar *native_assembly_expression(const gchar *verb, const gchar *expression)
{
    gchar *escaped = g_strescape(expression, NULL);
    gchar *command = g_strdup_printf("%s \"%s\"", verb, escaped);
    g_free(escaped);
    return command;
}

/* EN: Parse a numeric GDB value, permitting a symbol suffix after a hexadecimal pointer; FALSE rejects nonnumeric/overflow.
 * RU: Читает числовое значение GDB, разрешая имя символа после hex-указателя; FALSE отклоняет нечисла/переполнение. */
static gboolean native_assembly_number(const gchar *value, guint64 *number)
{
    if (!value)
        return FALSE;
    gboolean hex = g_str_has_prefix(value, "0x");
    const gchar *start = hex ? value + 2 : value, *end = start;
    while (hex ? g_ascii_isxdigit(*end) : g_ascii_isdigit(*end))
        end++;
    if (end == start || (*end && !(hex && *end == ' ')))
        return FALSE;
    gchar *digits = g_strndup(start, end - start);
    gboolean valid = g_ascii_string_to_unsigned(digits, hex ? 16 : 10, 0, G_MAXUINT64, number, NULL);
    g_free(digits);
    return valid;
}

/* EN: Queue paired address/size evaluations for expression and label/scope in assembly; return FALSE at the 128-location limit.
 * RU: Ставит парные запросы адреса/размера expression с label/scope в assembly; возвращает FALSE при лимите 128 адресов. */
static gboolean native_assembly_range_request(NativeAssembly *assembly, const gchar *expression,
    const gchar *label, const gchar *scope, gboolean manual)
{
    if (assembly->collected >= 128 || !native_name_text(expression, 2048, FALSE))
        return FALSE;
    assembly->collected++;
    gchar *address = g_strdup_printf("(void*)&(%s)", expression);
    gchar *size = g_strdup_printf("sizeof(%s)", expression);
    native_assembly_queue(assembly, native_assembly_expression("-data-evaluate-expression", address),
        4, expression, label, scope, NULL, 0, manual);
    native_assembly_queue(assembly, native_assembly_expression("-data-evaluate-expression", size),
        5, expression, label, scope, NULL, 0, manual);
    g_free(address);
    g_free(size);
    return TRUE;
}

/* EN: Request bounded fields for object with label/scope/depth/manual priority, retaining GDB's actual type layout.
 * RU: Запрашивает ограниченный список полей object с label/scope/depth/приоритетом manual по настоящему типу GDB. */
static void native_assembly_children(NativeAssembly *assembly, const gchar *object,
    const gchar *label, const gchar *scope, guint depth, gboolean manual)
{
    if (!native_name_text(object, 512, FALSE) || assembly->work.length > 512)
        return;
    gchar *quoted = g_strescape(object, NULL);
    gchar *command = g_strdup_printf("-var-list-children --no-values \"%s\" 0 64", quoted);
    native_assembly_queue(assembly, command, 7, NULL, label, scope, object, depth, manual);
    g_free(quoted);
}

/* EN: Disable a dynamic pretty-printer on object before reading its physical fields for label/scope/depth/manual.
 * RU: Отключает динамический pretty-printer object перед чтением физических полей label/scope/depth/manual. */
static void native_assembly_physical_fields(NativeAssembly *assembly, const gchar *object,
    const gchar *label, const gchar *scope, guint depth, gboolean manual)
{
    gchar *quoted = g_strescape(object, NULL);
    gchar *command = g_strdup_printf("-var-set-visualizer \"%s\" None", quoted);
    native_assembly_queue(assembly, command, 15, NULL, label, scope, object, depth, manual);
    g_free(quoted);
}

/* EN: Resolve child tuples in record for parent work; recurse into embedded aggregates but never follow pointers.
 * RU: Разрешает поля record задания parent; раскрывает вложенные агрегаты, но не следует по указателям. */
static void native_assembly_fields(NativeAssembly *assembly, const NativeAssemblyWork *parent, const gchar *record)
{
    const gchar *cursor = strstr(record, "children=["), *start, *end;
    if (!cursor)
        return;
    cursor += strlen("children=[");
    guint count = 0;
    while (count++ < 64 && assembly->collected < 128 && (end = native_next_tuple(&cursor, &start)))
    {
        gchar *tuple = g_strndup(start, end - start);
        gchar *object = native_debug_field(tuple, "name");
        gchar *field = native_debug_field(tuple, "exp");
        gchar *type = native_debug_field(tuple, "type");
        gchar *children = native_debug_field(tuple, "numchild");
        gchar *dynamic = native_debug_field(tuple, "dynamic");
        gboolean group = !g_strcmp0(field, "public") || !g_strcmp0(field, "private") || !g_strcmp0(field, "protected");
        if (native_name_text(object, 512, FALSE) && native_name_text(field, 128, FALSE))
        {
            gchar *label = group ? g_strdup(parent->label) : g_ascii_isdigit(*field) ?
                g_strdup_printf("%s[%s]", parent->label, field) : g_strdup_printf("%s.%s", parent->label, field);
            if (!group)
                native_assembly_queue(assembly, native_assembly_expression("-var-info-path-expression", object),
                    8, NULL, label, parent->scope, object, parent->depth, parent->manual);
            if (parent->depth < 3 && (!type || !strchr(type, '*')))
            {
                guint depth = parent->depth + (group ? 0 : 1);
                if (!g_strcmp0(dynamic, "1"))
                    native_assembly_physical_fields(assembly, object, label, parent->scope, depth, parent->manual);
                else if (children && strcmp(children, "0"))
                    native_assembly_children(assembly, object, label, parent->scope, depth, parent->manual);
            }
            g_free(label);
        }
        g_free(dynamic);
        g_free(children);
        g_free(type);
        g_free(field);
        g_free(object);
        g_free(tuple);
    }
}

/* EN: Decode GDB's actual register names and request at most 128 general-purpose values for assembly.
 * RU: Читает настоящие имена регистров GDB и запрашивает до 128 значений общего назначения для assembly. */
static void native_assembly_register_names(NativeAssembly *assembly, const gchar *record)
{
    const gchar *cursor = strstr(record, "register-names=[");
    if (!cursor || !assembly->register_names)
        return;
    cursor += strlen("register-names=[");
    GString *command = g_string_new("-data-list-register-values x");
    guint selected = 0;
    while (*cursor && *cursor != ']' && assembly->register_names->len < 1024)
    {
        if (*cursor != '"')
        {
            cursor++;
            continue;
        }
        const gchar *start = ++cursor;
        while (*cursor && *cursor != '"' && cursor - start < 128)
            cursor++;
        if (*cursor != '"')
            break;
        gchar *name = g_strndup(start, cursor++ - start);
        guint index = assembly->register_names->len;
        g_ptr_array_add(assembly->register_names, name);
        if (selected < 128 && native_name_register(name))
        {
            g_string_append_printf(command, " %u", index);
            selected++;
        }
    }
    if (selected)
        native_assembly_queue(assembly, g_string_free(command, FALSE), 14, NULL, NULL, NULL, NULL, 0, FALSE);
    else
        g_string_free(command, TRUE);
}

/* EN: Store confirmed scalar register values from record using assembly's matching name indices.
 * RU: Сохраняет подтверждённые скалярные значения record по соответствующим индексам имён assembly. */
static void native_assembly_register_values(NativeAssembly *assembly, const gchar *record)
{
    const gchar *cursor = strstr(record, "register-values=["), *start, *end;
    if (!cursor || !assembly->register_names || !assembly->register_names->len)
        return;
    cursor += strlen("register-values=[");
    guint count = 0;
    while (count++ < 128 && (end = native_next_tuple(&cursor, &start)))
    {
        gchar *tuple = g_strndup(start, end - start);
        gchar *index = native_debug_field(tuple, "number");
        gchar *value = native_debug_field(tuple, "value");
        guint64 number = 0, address = 0;
        if (index && g_ascii_string_to_unsigned(index, 10, 0, assembly->register_names->len - 1, &number, NULL) &&
            native_assembly_number(value, &address))
        {
            const gchar *name = assembly->register_names->pdata[number];
            if (native_name_register(name))
            {
                guint64 *stored = g_new(guint64, 1);
                *stored = address;
                g_hash_table_replace(assembly->registers, g_strdup(name), stored);
            }
        }
        g_free(value);
        g_free(index);
        g_free(tuple);
    }
}

/* EN: Queue only in-scope manual rules for assembly after GDB confirms the selected function.
 * RU: Ставит только действующие ручные правила assembly после подтверждения выбранной функции GDB. */
static void native_assembly_user_rules(NativeAssembly *assembly)
{
    for (guint i = 0; i < assembly->rules->len; i++)
    {
        NativeNameRule *rule = assembly->rules->pdata[i];
        if (*rule->scope && g_strcmp0(rule->scope, assembly->function))
            continue;
        if (rule->kind == 1)
        {
            NativeNamedRange *range = g_new0(NativeNamedRange, 1);
            range->frame = range->manual = TRUE;
            range->name = g_strdup(rule->name);
            range->scope = g_strdup(rule->scope);
            range->base = g_strdup(rule->type);
            native_name_offset(rule->anchor, &range->offset);
            g_ptr_array_add(assembly->ranges, range);
        }
        else
        {
            gchar *expression = g_strdup_printf("*((%s*)(%s))", rule->kind == 2 ? rule->type : "char", rule->anchor);
            native_assembly_range_request(assembly, expression, rule->name, rule->scope, TRUE);
            if (rule->kind == 2)
                native_assembly_queue(assembly, native_assembly_expression("-var-create - *", expression),
                    6, NULL, rule->name, rule->scope, NULL, 0, TRUE);
            g_free(expression);
        }
    }
}

/* EN: Apply one confirmed MI record to assembly's work; errors skip only unavailable optional metadata.
 * RU: Применяет подтверждённый ответ MI record к work в assembly; ошибки пропускают только недоступные метаданные. */
static void native_assembly_metadata(NativeAssembly *assembly, NativeAssemblyWork *work, const gchar *record)
{
    if (work->manual && g_str_has_prefix(record, "^error"))
    {
        assembly->unresolved++;
        g_free(assembly->last_error);
        assembly->last_error = native_debug_field(record, "msg");
    }
    gchar *value = native_debug_field(record, "value");
    guint64 number = 0;
    gboolean numeric = native_assembly_number(value, &number);
    if (work->action == 1)
    {
        g_free(assembly->function);
        assembly->function = native_debug_field(record, "func");
        native_assembly_user_rules(assembly);
    }
    else if (work->action == 2 && numeric)
    {
        guint64 *stored = g_new(guint64, 1);
        *stored = number;
        g_hash_table_replace(assembly->registers, g_strdup(work->expression), stored);
    }
    else if (work->action == 3 && native_name_text(assembly->function, 512, FALSE))
    {
        const gchar *cursor = strstr(record, "variables=["), *start, *end;
        guint count = 0;
        if (cursor)
            cursor += strlen("variables=[");
        while (cursor && count++ < 64 && (end = native_next_tuple(&cursor, &start)))
        {
            gchar *tuple = g_strndup(start, end - start);
            gchar *name = native_debug_field(tuple, "name");
            gchar *arg = native_debug_field(tuple, "arg");
            if (native_name_text(name, 512, FALSE))
            {
                gchar *label = g_strdup_printf("%s: %s", arg && !strcmp(arg, "1") ? "arg" : "local", name);
                if (native_assembly_range_request(assembly, name, label, assembly->function, FALSE))
                    native_assembly_queue(assembly, native_assembly_expression("-var-create - *", name),
                        6, NULL, label, assembly->function, NULL, 0, FALSE);
                g_free(label);
            }
            g_free(arg);
            g_free(name);
            g_free(tuple);
        }
    }
    else if (work->action == 4)
    {
        assembly->temporary_address = number;
        assembly->temporary_valid = numeric;
    }
    else if (work->action == 5 && numeric && number && assembly->temporary_valid)
    {
        NativeNamedRange *range = g_new0(NativeNamedRange, 1);
        range->address = assembly->temporary_address;
        range->size = number;
        range->name = g_strdup(work->label);
        range->scope = g_strdup(work->scope);
        range->manual = work->manual;
        g_ptr_array_add(assembly->ranges, range);
        assembly->temporary_valid = FALSE;
    }
    else if (work->action == 6)
    {
        gchar *object = native_debug_field(record, "name");
        if (native_name_text(object, 512, FALSE))
        {
            g_ptr_array_add(assembly->objects, g_strdup(object));
            gchar *type = native_debug_field(record, "type");
            gchar *children = native_debug_field(record, "numchild");
            gchar *dynamic = native_debug_field(record, "dynamic");
            if (work->manual || !type || !strchr(type, '*'))
            {
                if (!g_strcmp0(dynamic, "1"))
                    native_assembly_physical_fields(assembly, object, work->label, work->scope, 0, work->manual);
                else if (children && strcmp(children, "0"))
                    native_assembly_children(assembly, object, work->label, work->scope, 0, work->manual);
            }
            g_free(dynamic);
            g_free(children);
            g_free(type);
        }
        g_free(object);
    }
    else if (work->action == 7)
        native_assembly_fields(assembly, work, record);
    else if (work->action == 15 && g_str_has_prefix(record, "^done"))
        native_assembly_children(assembly, work->object, work->label, work->scope, work->depth, work->manual);
    else if (work->action == 13)
        native_assembly_register_names(assembly, record);
    else if (work->action == 14)
        native_assembly_register_values(assembly, record);
    else if (work->action == 8)
    {
        gchar *expression = native_debug_field(record, "path_expr");
        if (expression)
            native_assembly_range_request(assembly, expression, work->label, work->scope, work->manual);
        g_free(expression);
    }
    g_free(value);
}
