/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Mask lexical regions without moving source byte coordinates.
 * RU: Маскирует лексические области без смещения байтовых координат. */
#include "source_mask.h"
#include <string.h>

/* EN: Blank out[start:end], retaining line endings. No allocation or errors.
 * RU: Затирает out[start:end], сохраняя переводы строк. Без выделения памяти и ошибок. */
static void erase(char *out, gsize start, gsize end)
{
    for (gsize i = start; i < end; i++)
        if (out[i] != '\r' && out[i] != '\n') out[i] = ' ';
}

/* EN: Return the end of a Lua long literal at start in text of size bytes,
 * size if unterminated, or start if no opener is present; closed reports a complete token.
 * RU: Возвращает конец длинного литерала Lua в start текста размером size,
 * size для незакрытого литерала либо start, если начала нет; closed сообщает завершённость. */
static gsize lua_long(const char *text, gsize size, gsize start, gboolean *closed)
{
    if (closed) *closed = FALSE;
    if (start >= size || text[start] != '[') return start;
    gsize i = start + 1;
    while (i < size && text[i] == '=') i++;
    if (i >= size || text[i] != '[') return start;
    gsize equals = i - start - 1;
    for (i++; i < size; i++)
    {
        if (text[i] != ']') continue;
        gsize j = i + 1;
        while (j < size && text[j] == '=') j++;
        if (j - i - 1 == equals && j < size && text[j] == ']')
        {
            if (closed) *closed = TRUE;
            return j + 1;
        }
    }
    return size;
}

/* EN: Return the end of a C++ raw string at start in text of size bytes,
 * size if unterminated, or start for an invalid opener; closed reports a complete token.
 * RU: Возвращает конец сырой строки C++ в start текста размером size,
 * size для незакрытой строки либо start при неверном начале; closed сообщает завершённость. */
static gsize raw_string(const char *text, gsize size, gsize start, gboolean *closed)
{
    if (closed) *closed = FALSE;
    if (start + 2 >= size || text[start] != 'R' || text[start + 1] != '"') return start;
    gsize body = start + 2;
    while (body < size && body - start - 2 <= 16 && text[body] != '(')
    {
        if (g_ascii_isspace(text[body]) || text[body] == '\\' || text[body] == ')') return start;
        body++;
    }
    if (body >= size || text[body] != '(' || body - start - 2 > 16) return start;
    gsize delimiter = body - start - 2;
    for (gsize i = body + 1; i < size; i++)
        if (text[i] == ')' && i + delimiter + 1 < size &&
            !memcmp(text + i + 1, text + start + 2, delimiter) &&
            text[i + delimiter + 1] == '"')
        {
            if (closed) *closed = TRUE;
            return i + delimiter + 2;
        }
    return size;
}

typedef struct
{
    gsize caret, first, last;
    gboolean found;
} LiteralPick;

/* EN: Record a completed non-comment token [start,end) near pick's caret, with explicit delimiter widths.
 * Prefer a token starting at the caret over an adjacent token ending there; never change source bytes.
 * RU: Сохраняет завершённый литерал [start,end) у курсора pick с явной шириной разделителей.
 * Начало у курсора предпочтительнее конца соседнего литерала; исходные байты не меняются. */
static void pick_literal(LiteralPick *pick, gsize start, gsize end, gsize opening, gsize closing)
{
    if (!pick || opening + closing > end - start)
        return;
    gboolean boundary = (pick->caret >= start && pick->caret <= start + opening) ||
        (pick->caret >= end - closing && pick->caret <= end);
    if (boundary && (!pick->found || pick->caret == start))
    {
        pick->first = start;
        pick->last = end;
        pick->found = TRUE;
    }
}

/* EN: Allocate a region mask for text/size using dialect. membership chooses
 * binary membership rather than display text; optional pick records a literal boundary. Return NULL and set error for
 * invalid input; retain all byte positions and line endings otherwise.
 * RU: Выделяет маску region текста text/size для dialect. membership выбирает
 * бинарную принадлежность вместо текста; необязательный pick сохраняет границы литерала. Возвращает NULL и error при неверных
 * данных; иначе сохраняет все байтовые позиции и переводы строк. */
static gchar *region_mask(const char *text, gsize size, InsightCodeDialect dialect,
                         InsightSourceRegion region, gboolean membership, LiteralPick *pick, GError **error)
{
    if (!text || size > 2u * 1024u * 1024u || memchr(text, 0, size) ||
        !g_utf8_validate(text, (gssize)size, NULL) || dialect < INSIGHT_CODE_C ||
        dialect > INSIGHT_CODE_VERILOG || region < INSIGHT_REGION_ALL ||
        region > INSIGHT_REGION_COMMENTS)
    {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
            "Invalid source or unsupported lexical dialect");
        return NULL;
    }
    gchar *out = g_strndup(text, size);
    if (membership) memset(out, region == INSIGHT_REGION_COMMENTS ? 0 : 1, size);
    if (region == INSIGHT_REGION_ALL) return out;
    if (!membership && region == INSIGHT_REGION_COMMENTS) erase(out, 0, size);
    for (gsize i = 0; i < size;)
    {
        gsize start = i, end = i;
        gboolean comment = FALSE, closed = FALSE, valid_literal = TRUE;
        gsize opening = 1, closing = 1;
        /* HDL escaped identifiers can contain quotes and comment delimiters. */
        if (dialect == INSIGHT_CODE_VERILOG && text[i] == '\\')
        {
            do { i++; } while (i < size && !g_ascii_isspace(text[i]));
            continue;
        }
        /* C++ separators are not character literal delimiters. */
        if (dialect == INSIGHT_CODE_C && text[i] == '\'' && i > 0 && i + 1 < size &&
            g_ascii_isxdigit(text[i - 1]) && g_ascii_isxdigit(text[i + 1]))
        {
            i++;
            continue;
        }
        if (dialect == INSIGHT_CODE_C && text[i] == 'R')
        {
            end = raw_string(text, size, i, &closed);
            if (closed)
            {
                gsize body = i + 2;
                while (text[body] != '(') body++;
                opening = body - i + 1;
                closing = body - i;
            }
        }
        if (end == i && dialect == INSIGHT_CODE_LUA && text[i] == '[')
        {
            end = lua_long(text, size, i, &closed);
            if (closed)
            {
                opening = 2;
                while (text[i + opening - 1] == '=') opening++;
                closing = opening;
            }
        }
        gboolean slash = (dialect == INSIGHT_CODE_C || dialect == INSIGHT_CODE_JS ||
            dialect == INSIGHT_CODE_VERILOG) && i + 1 < size && text[i] == '/';
        gboolean lua = dialect == INSIGHT_CODE_LUA && i + 1 < size &&
            text[i] == '-' && text[i + 1] == '-';
        gboolean hash = (dialect == INSIGHT_CODE_PYTHON || dialect == INSIGHT_CODE_SHELL) &&
            text[i] == '#';
        if (end == i && slash && text[i + 1] == '*')
        {
            comment = TRUE;
            end = i + 2;
            while (end + 1 < size && !(text[end] == '*' && text[end + 1] == '/')) end++;
            end = end + 1 < size ? end + 2 : size;
        }
        else if (end == i && (hash || lua || (slash && text[i + 1] == '/')))
        {
            comment = TRUE;
            if (lua) end = lua_long(text, size, i + 2, NULL);
            if (!lua || end == i + 2)
            {
                end = i;
                while (end < size)
                {
                    if (text[end] == '\n' || text[end] == '\r')
                    {
                        if (dialect == INSIGHT_CODE_C && slash && text[i + 1] == '/' &&
                            end > i && text[end - 1] == '\\')
                        {
                            if (text[end] == '\r' && end + 1 < size && text[end + 1] == '\n') end++;
                            end++;
                            continue;
                        }
                        break;
                    }
                    end++;
                }
            }
        }
        else if (end == i && ((text[i] == '\'' && dialect != INSIGHT_CODE_VERILOG) ||
                 text[i] == '"' || (dialect == INSIGHT_CODE_JS && text[i] == '`')))
        {
            char quote = text[i];
            gboolean triple = dialect == INSIGHT_CODE_PYTHON && i + 2 < size &&
                text[i + 1] == quote && text[i + 2] == quote;
            gsize width = triple ? 3 : 1;
            opening = closing = width;
            end = i + width;
            while (end < size)
            {
                if (text[end] == '\\')
                {
                    gsize escaped = end + 2 < size && text[end + 1] == '\r' && text[end + 2] == '\n' ? 3 : 2;
                    end = MIN(size, end + escaped);
                    continue;
                }
                if (text[end] == quote && (!triple || (end + 2 < size &&
                    text[end + 1] == quote && text[end + 2] == quote)))
                {
                    end += width;
                    closed = TRUE;
                    break;
                }
                if ((text[end] == '\r' || text[end] == '\n') && !triple &&
                    dialect != INSIGHT_CODE_SHELL && quote != '`')
                    valid_literal = FALSE;
                end++;
            }
        }
        if (!comment && closed && valid_literal)
            pick_literal(pick, start, end, opening, closing);
        if (end == start)
        {
            i++;
            continue;
        }
        if (region == INSIGHT_REGION_COMMENTS)
        {
            if (comment)
            {
                if (membership) memset(out + start, 1, end - start);
                else memcpy(out + start, text + start, end - start);
            }
        }
        else if (comment || region == INSIGHT_REGION_CODE)
        {
            if (membership) memset(out + start, 0, end - start);
            else erase(out, start, end);
        }
        i = end;
    }
    return out;
}

/* EN: Allocate a display mask for text/size, dialect and region; return NULL
 * and set error on invalid input. The caller frees the returned buffer.
 * RU: Выделяет текстовую маску для text/size, dialect и region; возвращает NULL
 * и error при неверных данных. Вызывающий освобождает результат. */
gchar *insight_source_region_mask(const char *text, gsize size, InsightCodeDialect dialect,
                                 InsightSourceRegion region, GError **error)
{
    return region_mask(text, size, dialect, region, FALSE, NULL, error);
}

/* EN: Allocate binary membership for text/size, dialect and region; NULL/error
 * denotes invalid input. The caller frees the returned buffer.
 * RU: Выделяет бинарную принадлежность text/size для dialect и region; NULL/error
 * означает неверные данные. Вызывающий освобождает результат. */
guchar *insight_source_region_membership(const char *text, gsize size, InsightCodeDialect dialect,
                                       InsightSourceRegion region, GError **error)
{
    return (guchar *)region_mask(text, size, dialect, region, TRUE, NULL, error);
}

/* EN: Allocate code-only text/size for dialect; NULL/error denotes invalid input.
 * The caller frees the result; comments and strings become spaces.
 * RU: Выделяет код text/size для dialect; NULL/error означает неверные данные.
 * Вызывающий освобождает результат; комментарии и строки заменены пробелами. */
gchar *insight_source_code_mask(const char *text, gsize size, InsightCodeDialect dialect, GError **error)
{
    return insight_source_region_mask(text, size, dialect, INSIGHT_REGION_CODE, error);
}

/* EN: Select a complete literal at a delimiter beside caret in text/size for dialect. Return 1, 0, or -1/error.
 * On success write byte offsets to first/last, including delimiters but excluding encoding/language prefixes.
 * RU: Выделяет завершённый литерал у разделителя возле caret в text/size для dialect. Возвращает 1, 0 либо -1/error.
 * При успехе записывает в first/last байтовые границы с разделителями, без префиксов кодировки/языка. */
gint insight_source_select_literal(const gchar *text, gsize size, InsightCodeDialect dialect,
                                    gsize caret, gsize *first, gsize *last, GError **error)
{
    if (!first || !last || caret > size)
    {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid literal selection position");
        return -1;
    }
    LiteralPick pick = {caret, 0, 0, FALSE};
    gchar *mask = region_mask(text, size, dialect, INSIGHT_REGION_CODE, FALSE, &pick, error);
    if (!mask)
        return -1;
    g_free(mask);
    if (!pick.found)
        return 0;
    *first = pick.first;
    *last = pick.last;
    return 1;
}
