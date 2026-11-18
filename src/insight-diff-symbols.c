/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Collect private C/C++ and Verilog definition ranges from exact comparison text, without changing the project index.
 * RU: Собирает диапазоны C/C++ и Verilog из точного текста сравнения без изменения индекса проекта. */
#include "geany.h"
#include "insight-diff.h"
#include "insight-diff-syntax.h"
#include "filetypes.h"
#include "tagmanager/tm_source_file.h"
#include "tagmanager/tm_tag.h"
#include "../insight/desktop/source_mask.h"
#include "../insight/core/structure_select.h"
#include <string.h>

/* EN: Return whether byte is part of a source identifier; high UTF-8 bytes remain within their token.
 * RU: Возвращает, входит ли byte в идентификатор; старшие байты UTF-8 остаются частью токена. */
static gboolean diff_symbol_word(guchar byte)
{
    return g_ascii_isalnum(byte) || byte == '_' || byte == '$' || byte >= 128;
}

/* EN: Return whether mask[begin,end) equals word exactly.
 * RU: Возвращает точное совпадение mask[begin,end) со словом word. */
static gboolean diff_symbol_token(const gchar *mask, gsize begin, gsize end, const gchar *word)
{
    return end - begin == strlen(word) && !memcmp(mask + begin, word, end - begin);
}

/* EN: Return whether name appears as a complete identifier in mask's bounded declaration; no offsets escape length.
 * RU: Проверяет полное имя name в ограниченном объявлении mask; смещения не выходят за length. */
static gboolean diff_symbol_named(const gchar *mask, gsize begin, gsize end, const gchar *name)
{
    gsize size = strlen(name);
    if (!size || end < begin || size > end - begin)
        return FALSE;
    for (gsize at = begin; at + size <= end; at++)
        if (!memcmp(mask + at, name, size) &&
            (at == begin || !diff_symbol_word(mask[at - 1])) &&
            (at + size == end || !diff_symbol_word(mask[at + size])))
            return TRUE;
    return FALSE;
}

/* EN: Extend end through its whitespace/comment-only line ending, preserving CRLF; never include another code token.
 * RU: Расширяет end до конца строки с одними пробелами/комментарием, сохраняя CRLF; не включает следующий токен кода. */
static gsize diff_symbol_line_end(const gchar *text, const gchar *mask, gsize length, gsize end)
{
    gsize at = end;
    while (at < length && text[at] != '\r' && text[at] != '\n')
    {
        if (!g_ascii_isspace(mask[at]) && mask[at] != ';')
            return end;
        at++;
    }
    if (at < length && text[at] == '\r')
        at++;
    if (at < length && text[at] == '\n')
        at++;
    return at;
}

/* EN: Find a complete Verilog/SV module, task or similar keyword definition at start for name; return FALSE if uncertain.
 * Skip masked strings/comments, escaped identifiers and directive lines; nested equal constructs are balanced.
 * RU: Находит полное Verilog/SV-определение модуля, задачи или родственной конструкции start для name; при сомнении FALSE.
 * Пропускает строки/комментарии, экранированные имена и директивы; балансирует вложенные одноимённые конструкции. */
static gboolean diff_symbol_verilog(const gchar *mask, gsize length, gsize start,
                                    const gchar *name, gsize *finish)
{
    const gchar *starts[] = {"module", "macromodule", "function", "task", "interface", "package", "program", "class"};
    const gchar *ends[] = {"endmodule", "endmodule", "endfunction", "endtask", "endinterface", "endpackage", "endprogram", "endclass"};
    guint chosen = G_N_ELEMENTS(starts);
    gsize declaration_end = start;
    while (declaration_end < length && mask[declaration_end] != '\n' &&
           mask[declaration_end] != '\r' && mask[declaration_end] != ';')
        declaration_end++;
    if (!diff_symbol_named(mask, start, declaration_end, name))
        return FALSE;
    for (gsize at = start; at < declaration_end;)
    {
        if (!diff_symbol_word(mask[at]))
        {
            at++;
            continue;
        }
        gsize begin = at;
        while (at < declaration_end && diff_symbol_word(mask[at]))
            at++;
        for (guint i = 0; i < G_N_ELEMENTS(starts); i++)
            if (diff_symbol_token(mask, begin, at, starts[i]))
            {
                chosen = i;
                break;
            }
        if (chosen < G_N_ELEMENTS(starts))
            break;
    }
    if (chosen == G_N_ELEMENTS(starts))
        return FALSE;
    guint depth = 0;
    for (gsize at = start; at < length;)
    {
        if (mask[at] == '`' || mask[at] == '\\')
        {
            gchar separator = mask[at++] == '`' ? '\n' : ' ';
            while (at < length && (separator == '\n' ? mask[at] != '\n' : !g_ascii_isspace(mask[at])))
                at++;
            continue;
        }
        if (!diff_symbol_word(mask[at]))
        {
            at++;
            continue;
        }
        gsize begin = at;
        while (at < length && diff_symbol_word(mask[at]))
            at++;
        gboolean opening = diff_symbol_token(mask, begin, at, starts[chosen]);
        if (chosen < 2)
            opening |= diff_symbol_token(mask, begin, at, starts[1 - chosen]);
        if (opening)
            depth++;
        else if (diff_symbol_token(mask, begin, at, ends[chosen]) && depth && !--depth)
        {
            *finish = at;
            return TRUE;
        }
    }
    return FALSE;
}

/* EN: Return owned definition ranges for filename/text using a private native parser and lexical extent checks.
 * Accept C/C++ and Verilog/SV up to 1 MiB, at most 4096 ranges; unsupported or incomplete definitions are omitted.
 * Never stat/read filename or consult live-buffer/project symbols; caller owns the returned array.
 * RU: Возвращает собственные диапазоны filename/text через отдельный встроенный парсер и проверку лексических границ.
 * Принимает C/C++ и Verilog/SV до 1 МиБ и 4096 диапазонов; неподдерживаемые/неполные определения пропускает.
 * Не читает filename и символы живого буфера/проекта; возвращаемым массивом владеет вызывающий. */
GPtrArray *insight_diff_native_symbols(const gchar *filename, const gchar *text)
{
    if (!filename || !text || strlen(text) > 1024 * 1024 || !g_utf8_validate(text, -1, NULL))
        return NULL;
    GeanyFiletype *type = filetypes_detect_from_extension(filename);
    if (!type)
        return NULL;
    gboolean verilog = type->lang == TM_PARSER_VERILOG || type->lang == TM_PARSER_SYSVERILOG;
    if (!verilog && type->lang != TM_PARSER_C && type->lang != TM_PARSER_CPP)
        return NULL;
    gsize length = strlen(text);
    gchar *mask = insight_source_code_mask(text, length,
        verilog ? INSIGHT_CODE_VERILOG : INSIGHT_CODE_C, NULL);
    if (!mask)
        return NULL;
    TMSourceFile *file = tm_source_file_new(NULL, NULL);
    if (!file)
    {
        g_free(mask);
        return NULL;
    }
    file->file_name = g_strdup(filename);
    gchar *separator = strrchr(file->file_name, G_DIR_SEPARATOR);
    file->short_name = separator ? separator + 1 : file->file_name;
    file->lang = type->lang;
    file->trust_file_scope = TRUE;
    tm_source_file_parse(file, (guchar *)text, length, TRUE);
    GArray *lines = g_array_new(FALSE, FALSE, sizeof(gsize));
    gsize zero = 0;
    g_array_append_val(lines, zero);
    for (gsize at = 0; at < length; at++)
        if (text[at] == '\r' || text[at] == '\n')
        {
            if (text[at] == '\r' && at + 1 < length && text[at + 1] == '\n')
                at++;
            gsize next = at + 1;
            g_array_append_val(lines, next);
        }
    GPtrArray *ranges = g_ptr_array_new_with_free_func(insight_diff_symbol_free);
    for (guint i = 0; i < MIN(file->tags_array->len, 65536u) && ranges->len < 4096; i++)
    {
        TMTag *tag = file->tags_array->pdata[i];
        if (!tag || !tag->name || !tag->line || tag->line > lines->len)
            continue;
        gsize begin = g_array_index(lines, gsize, tag->line - 1);
        gsize body = 0, end = 0;
        gboolean found = FALSE;
        if (verilog)
        {
            if (tag->type & (tm_tag_prototype_t | tm_tag_function_t | tm_tag_class_t | tm_tag_interface_t | tm_tag_package_t))
                found = diff_symbol_verilog(mask, length, begin, tag->name, &end);
        }
        else if (tag->type & (tm_tag_function_t | tm_tag_method_t))
            found = insight_function_range(text, mask, length, begin, INSIGHT_FUNCTION_BRACES, &body, &end) == 1 &&
                diff_symbol_named(mask, begin, body, tag->name);
        else
        {
            const gchar *keyword = tag->type & tm_tag_class_t ? "class" : tag->type & tm_tag_struct_t ? "struct" :
                tag->type & tm_tag_union_t ? "union" : tag->type & tm_tag_enum_t ? "enum" :
                tag->type & tm_tag_namespace_t ? "namespace" : NULL;
            if (keyword)
                found = insight_nominal_range(text, mask, length, begin, tag->name, keyword,
                    INSIGHT_FUNCTION_BRACES, &body, &end) == 1;
        }
        if (!found || end <= begin)
            continue;
        InsightDiffSymbol *range = g_new0(InsightDiffSymbol, 1);
        gchar *name = tag->scope && *tag->scope ? g_strdup_printf("%s%s%s", tag->scope,
            verilog ? "." : "::", tag->name) : g_strdup(tag->name);
        range->name = g_utf8_make_valid(name, -1);
        range->begin = begin;
        range->end = diff_symbol_line_end(text, mask, length, end);
        g_ptr_array_add(ranges, range);
        g_free(name);
    }
    g_array_unref(lines);
    tm_source_file_free(file);
    g_free(mask);
    return ranges;
}
