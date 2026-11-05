/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-python-debug.h"
#include <glib/gstdio.h>
#include <string.h>

#define MAX_PACKET_BYTES (256u * 1024u)

struct InsightPythonDebug {
    GSubprocess *process;
    GDataInputStream *output;
    gchar *snapshot;
};

/* Start the isolated interpreter using a private snapshot of the editor buffer. */
/* Запускает отдельный интерпретатор с приватным снимком буфера редактора. */
InsightPythonDebug *insight_python_debug_new(const gchar *interpreter, const gchar *helper,
    const gchar *filename, const gchar *project_root, const gchar *source, gsize length, GError **error)
{
    InsightPythonDebug *debugger = g_new0(InsightPythonDebug, 1);
    gint descriptor = g_file_open_tmp("geany-python-debug-XXXXXX", &debugger->snapshot, error);
    if (descriptor < 0)
    {
        insight_python_debug_free(debugger);
        return NULL;
    }
    if (!g_close(descriptor, error) ||
        !g_file_set_contents(debugger->snapshot, source, (gssize)length, error))
    {
        insight_python_debug_free(debugger);
        return NULL;
    }

    debugger->process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE |
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, error,
        interpreter, "-I", "-u", helper, debugger->snapshot, filename, project_root ? project_root : "", NULL);
    if (!debugger->process)
    {
        insight_python_debug_free(debugger);
        return NULL;
    }
    debugger->output = g_data_input_stream_new(g_subprocess_get_stdout_pipe(debugger->process));
    return debugger;
}

/* Terminate the child and remove the private source snapshot. */
/* Завершает дочерний процесс и удаляет приватный снимок исходника. */
void insight_python_debug_free(InsightPythonDebug *debugger)
{
    if (!debugger)
        return;
    if (debugger->process)
        g_subprocess_force_exit(debugger->process);
    g_clear_object(&debugger->output);
    g_clear_object(&debugger->process);
    if (debugger->snapshot)
        g_unlink(debugger->snapshot);
    g_free(debugger->snapshot);
    g_free(debugger);
}

/* Send a short, newline-terminated debugger command. */
/* Отправляет короткую команду отладчика с переводом строки. */
gboolean insight_python_debug_send(InsightPythonDebug *debugger, const gchar *command, GError **error)
{
    GOutputStream *input = g_subprocess_get_stdin_pipe(debugger->process);
    return g_output_stream_write_all(input, command, strlen(command), NULL, NULL, error);
}

/* Return the stream carrying stop and completion packets. */
/* Возвращает поток пакетов остановки и завершения. */
GDataInputStream *insight_python_debug_output(InsightPythonDebug *debugger)
{
    return debugger->output;
}

/* Decode one protocol field into a fixed-size, NUL-terminated buffer. */
/* Декодирует поле протокола в буфер заданного размера с нулевым окончанием. */
static gboolean decode_field(const gchar *hex, gchar *output, gsize capacity)
{
    gsize length = strlen(hex);
    if (length % 2)
        return FALSE;
    gsize bytes = length / 2;
    for (gsize i = 0; i < bytes; i++)
    {
        gint first = g_ascii_xdigit_value(hex[2 * i]);
        gint second = g_ascii_xdigit_value(hex[2 * i + 1]);
        if (first < 0 || second < 0)
            return FALSE;
        if (i + 1 < capacity)
        {
            gchar value = (gchar)(first * 16 + second);
            output[i] = value ? value : ' ';
        }
    }
    output[MIN(bytes, capacity - 1)] = '\0';
    return TRUE;
}

/* Parse an unsigned integer within the caller's protocol limit. */
/* Разбирает беззнаковое целое в пределах ограничения протокола. */
static gboolean parse_number(const gchar *field, guint maximum, guint *number)
{
    guint64 value = 0;
    if (!g_ascii_string_to_unsigned(field, 10, 0, maximum, &value, NULL))
        return FALSE;
    *number = (guint)value;
    return TRUE;
}

/* Validate a packet before exposing a source line, stack, or watch graph. */
/* Проверяет пакет перед передачей строки, стека или графа наблюдения. */
gint insight_python_debug_parse(const gchar *packet, gsize length, unsigned long *line,
    gchar **filename, gchar **stack, InsightLuaWatchGraph **graph, gchar **message)
{
    *line = 0;
    *filename = NULL;
    *stack = NULL;
    *graph = NULL;
    *message = NULL;
    if (!packet)
    {
        *message = g_strdup("Python debugger process exited without a response");
        return -1;
    }
    if (length > MAX_PACKET_BYTES || memchr(packet, 0, length))
    {
        *message = g_strdup("Invalid Python debugger response size");
        return -1;
    }
    if (!strcmp(packet, "D"))
        return 0;

    gchar **fields = g_strsplit(packet, "\t", -1);
    guint count = g_strv_length(fields);
    gint result = -1;
    if (count == 2 && !strcmp(fields[0], "E"))
    {
        gchar error[2048];
        if (decode_field(fields[1], error, sizeof error))
            *message = g_strdup(error);
    }
    else if (count >= 7 && !strcmp(fields[0], "P"))
    {
        guint position, nodes, edges, limited;
        if (parse_number(fields[1], G_MAXINT, &position) && position &&
            parse_number(fields[4], INSIGHT_LUA_WATCH_NODES, &nodes) && nodes &&
            parse_number(fields[5], INSIGHT_LUA_WATCH_EDGES, &edges) &&
            parse_number(fields[6], 1, &limited) && count == 7 + 3 * (nodes + edges))
        {
            InsightLuaWatchGraph *snapshot = g_new0(InsightLuaWatchGraph, 1);
            snapshot->node_count = nodes;
            snapshot->edge_count = edges;
            snapshot->limited = (gint)limited;
            gchar stack_text[32768];
            gchar *source_path = g_malloc(strlen(fields[2]) / 2 + 1);
            gboolean valid = decode_field(fields[2], source_path, strlen(fields[2]) / 2 + 1) &&
                *source_path && g_utf8_validate(source_path, -1, NULL) &&
                decode_field(fields[3], stack_text, sizeof stack_text);
            guint at = 7;
            for (guint i = 0; valid && i < nodes; i++, at += 3)
            {
                InsightLuaWatchNode *node = &snapshot->nodes[i];
                valid = decode_field(fields[at], node->label, sizeof node->label) &&
                    decode_field(fields[at + 1], node->value, sizeof node->value) &&
                    decode_field(fields[at + 2], node->path, sizeof node->path);
            }
            for (guint i = 0; valid && i < edges; i++, at += 3)
            {
                InsightLuaWatchEdge *edge = &snapshot->edges[i];
                valid = parse_number(fields[at], nodes - 1, &edge->from) &&
                    parse_number(fields[at + 1], nodes - 1, &edge->to) &&
                    decode_field(fields[at + 2], edge->label, sizeof edge->label);
            }
            if (valid)
            {
                *line = position;
                *filename = g_steal_pointer(&source_path);
                *stack = g_strdup(stack_text);
                *graph = snapshot;
                result = 1;
            }
            else
                g_free(snapshot);
            g_free(source_path);
        }
    }
    g_strfreev(fields);
    if (result < 0 && !*message)
        *message = g_strdup("Invalid Python debugger response");
    return result;
}
