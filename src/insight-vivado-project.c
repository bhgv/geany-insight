/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-vivado-project.h"
#include <glib/gstdio.h>
#include <string.h>
#define GROUP "insight-vivado"
#define MAX_PROJECT_BYTES (8u * 1024u * 1024u)
#define MAX_HDL_FILES 8192u

typedef struct {
 gchar *root, *stem, *top;
 GPtrArray *files, *includes, *defines;
 GHashTable *seen;
 guint depth, design;
 gboolean project, found_design;
} Import;

static const gchar *attribute(const gchar **names, const gchar **values, const gchar *key) {
 for (guint i = 0; names[i]; i++) if (!strcmp(names[i], key)) return values[i];
 return NULL;
}

static gchar *resolve_path(Import *state, const gchar *value) {
 if (!value || !*value || strpbrk(value, "\r\n\t")) return NULL;
 const gchar *relative = value;
 gchar *base = NULL;
 if (*value == '$') {
  const gchar *slash = strpbrk(value, "/\\");
  if (!slash) return NULL;
  gchar *variable = g_strndup(value, slash - value);
  const gchar *suffix = !strcmp(variable, "$PSRCDIR") ? ".srcs" :
                         !strcmp(variable, "$PGENDIR") ? ".gen" :
                         !strcmp(variable, "$PIPUSERFILESDIR") ? ".ip_user_files" : NULL;
  if (suffix) {
   gchar *folder = g_strconcat(state->stem, suffix, NULL);
   base = g_build_filename(state->root, folder, NULL);
   g_free(folder);
  } else if (!strcmp(variable, "$PPRDIR")) base = g_strdup(state->root);
  g_free(variable);
  if (!base) return NULL;
  relative = slash + 1;
 }
 gchar *path = g_canonicalize_filename(relative, base ? base : state->root);
 g_free(base);
 return path;
}

static gboolean hdl_file(const gchar *path) {
 const gchar *ext = strrchr(path, '.');
 if (!ext) return FALSE;
 const gchar *extensions[] = {".v", ".sv", ".vh", ".svh", ".vhd", ".vhdl"};
 for (guint i = 0; i < G_N_ELEMENTS(extensions); i++)
  if (!g_ascii_strcasecmp(ext, extensions[i])) return TRUE;
 return FALSE;
}

/* XPR represents these options as lists. Braced and quoted values can
 * contain spaces; plain backslashes are retained for Windows paths. */
static void option_values(Import *state, GPtrArray *out, const gchar *text, gboolean paths) {
 while (*text) {
  while (g_ascii_isspace(*text) || *text == ';') text++;
  if (!*text) break;
  gchar delimiter = *text == '{' ? '}' : *text == '"' ? '"' : 0;
  if (delimiter) text++;
  const gchar *start = text;
  while (*text && (delimiter ? *text != delimiter :
                    !g_ascii_isspace(*text) && *text != ';')) text++;
  gchar *value = g_strndup(start, text - start);
  if (delimiter && *text) text++;
  if (paths) {
   gchar *path = resolve_path(state, value);
   g_free(value);
   value = path;
  }
  if (value && *value && !strpbrk(value, "\r\n\t")) g_ptr_array_add(out, value);
  else g_free(value);
 }
}

static void start_element(GMarkupParseContext *ctx, const gchar *name,
                          const gchar **names, const gchar **values, gpointer data, GError **error) {
 (void)ctx;
 Import *state = data;
 state->depth++;
 if (state->depth == 1) state->project = !strcmp(name, "Project");
 if (!state->project) return;
 if (!state->design && !state->found_design && !strcmp(name, "FileSet") &&
     !g_strcmp0(attribute(names, values, "Type"), "DesignSrcs")) {
  state->design = state->depth;
  state->found_design = TRUE;
 } else if (state->design && !strcmp(name, "File")) {
  const gchar *raw = attribute(names, values, "Path");
  if (raw && hdl_file(raw)) {
   gchar *path = resolve_path(state, raw);
   if (!path) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Unsupported Vivado source path: %s", raw);
    return;
   }
   if (!g_hash_table_contains(state->seen, path)) {
    if (state->files->len >= MAX_HDL_FILES) {
     g_free(path);
     g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Vivado design fileset exceeds 8192 HDL files");
     return;
    }
    g_hash_table_add(state->seen, path);
    g_ptr_array_add(state->files, path);
   } else g_free(path);
  }
 } else if (state->design && !strcmp(name, "Option")) {
  const gchar *key = attribute(names, values, "Name"), *value = attribute(names, values, "Val");
  if (!key || !value) return;
  if (!g_ascii_strcasecmp(key, "TopModule") && *value && !strpbrk(value, "\r\n\t")) {
   g_free(state->top);
   state->top = g_strdup(value);
  } else if (!g_ascii_strcasecmp(key, "IncludeDirs"))
   option_values(state, state->includes, value, TRUE);
  else if (!g_ascii_strcasecmp(key, "VerilogDefine"))
   option_values(state, state->defines, value, FALSE);
 }
}

static void end_element(GMarkupParseContext *ctx, const gchar *name, gpointer data, GError **error) {
 (void)ctx; (void)name; (void)error;
 Import *state = data;
 if (state->depth == state->design) state->design = 0;
 state->depth--;
}

static gboolean read_xpr(const gchar *path, Import *state, GError **error) {
 state->root = g_path_get_dirname(path);
 state->stem = g_path_get_basename(path);
 state->stem[strlen(state->stem) - 4] = '\0';
 state->files = g_ptr_array_new_with_free_func(g_free);
 state->includes = g_ptr_array_new_with_free_func(g_free);
 state->defines = g_ptr_array_new_with_free_func(g_free);
 state->seen = g_hash_table_new(g_str_hash, g_str_equal);
 GFile *file = g_file_new_for_path(path);
 GFileInputStream *stream = g_file_read(file, NULL, error);
 g_object_unref(file);
 if (!stream) return FALSE;
 GMarkupParser parser = {start_element, end_element, NULL, NULL, NULL};
 GMarkupParseContext *ctx = g_markup_parse_context_new(&parser, G_MARKUP_TREAT_CDATA_AS_TEXT, state, NULL);
 gchar buffer[16384];
 gssize size;
 gsize total = 0;
 gboolean ok = TRUE;
 while ((size = g_input_stream_read(G_INPUT_STREAM(stream), buffer, sizeof buffer, NULL, error)) > 0) {
  total += (gsize)size;
  if (total > MAX_PROJECT_BYTES) {
   g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Vivado project exceeds 8 MiB");
   ok = FALSE;
   break;
  }
  if (!g_markup_parse_context_parse(ctx, buffer, size, error)) {
   ok = FALSE;
   break;
  }
 }
 if (ok) ok = size == 0 && g_markup_parse_context_end_parse(ctx, error);
 g_markup_parse_context_free(ctx);
 g_object_unref(stream);
 if (ok && (!state->project || !state->found_design)) {
  g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Vivado project has no design fileset");
  ok = FALSE;
 }
 return ok;
}

static void import_clear(Import *state) {
 g_hash_table_unref(state->seen);
 g_ptr_array_unref(state->files);
 g_ptr_array_unref(state->includes);
 g_ptr_array_unref(state->defines);
 g_free(state->root);
 g_free(state->stem);
 g_free(state->top);
}

gboolean insight_vivado_is_xpr(const gchar *path) {
 gsize length = path ? strlen(path) : 0;
 return length >= 4 && !g_ascii_strcasecmp(path + length - 4, ".xpr");
}

static gchar *companion(const gchar *file, const gchar *suffix) {
 const gchar *extension = strrchr(file, '.');
 gchar *stem = g_strndup(file, extension - file);
 gchar *path = g_strconcat(stem, suffix, NULL);
 g_free(stem);
 return path;
}

static gboolean replace_file(const gchar *path, const gchar *contents, gsize size, GError **error) {
 GFile *file = g_file_new_for_path(path);
 gboolean ok = g_file_replace_contents(file, contents, size, NULL, FALSE,
                                      G_FILE_CREATE_PRIVATE, NULL, NULL, error);
 g_object_unref(file);
 return ok;
}

static gboolean create_file(const gchar *path, const gchar *contents, gsize size, GError **error) {
 GFile *file = g_file_new_for_path(path);
 GFileOutputStream *stream = g_file_create(file, G_FILE_CREATE_PRIVATE, NULL, error);
 gboolean ok = stream != NULL;
 if (stream) {
  ok = g_output_stream_write_all(G_OUTPUT_STREAM(stream), contents, size, NULL, NULL, error);
  if (ok) ok = g_output_stream_close(G_OUTPUT_STREAM(stream), NULL, error);
  g_object_unref(stream);
  if (!ok) g_unlink(path);
 }
 g_object_unref(file);
 return ok;
}

static gboolean write_import(const gchar *project, const gchar *xpr, GKeyFile *config,
                             InsightProjectFiles *policy, Import *state, gboolean creating, GError **error) {
 gchar *base = g_key_file_get_string(config, "project", "base_path", NULL);
 gchar *project_dir = g_path_get_dirname(project);
 gchar *root = g_canonicalize_filename(base && *base ? base : ".", project_dir);
 gboolean ok = insight_project_files_write(policy, config, root, error);
 g_free(root);
 g_free(project_dir);
 g_free(base);
 if (!ok) return FALSE;
 g_key_file_set_string(config, GROUP, "source", xpr);
 g_key_file_set_string_list(config, GROUP, "files", (const gchar *const *)state->files->pdata, state->files->len);
 gchar *settings = companion(project, ".hdl.tsv");
 GString *tsv = g_string_new("geany-insight-vivado-v1\n");
 for (guint i = 0; i < state->includes->len; i++)
  g_string_append_printf(tsv, "include\t%s\n", (gchar *)state->includes->pdata[i]);
 for (guint i = 0; i < state->defines->len; i++)
  g_string_append_printf(tsv, "define\t%s\n", (gchar *)state->defines->pdata[i]);
 if (state->top) g_string_append_printf(tsv, "top\t%s\n", state->top);
 gchar *previous = NULL;
 gsize previous_size = 0, size = 0;
 gboolean settings_exist = g_file_test(settings, G_FILE_TEST_EXISTS);
 if (creating && settings_exist) {
  g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "A companion .hdl.tsv already exists");
  ok = FALSE;
 } else if (settings_exist) ok = g_file_get_contents(settings, &previous, &previous_size, error);
 gchar *contents = g_key_file_to_data(config, &size, NULL);
 if (ok && (tsv->len > 65536 || size > MAX_PROJECT_BYTES)) {
  g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Imported project settings exceed size limits");
  ok = FALSE;
 }
 if (ok && (creating ? create_file(settings, tsv->str, tsv->len, error) :
                        replace_file(settings, tsv->str, tsv->len, error))) {
  ok = creating ? create_file(project, contents, size, error) : replace_file(project, contents, size, error);
  if (!ok) {
   if (settings_exist) replace_file(settings, previous, previous_size, NULL);
   else g_unlink(settings);
  }
 } else ok = FALSE;
 g_free(contents);
 g_free(previous);
 g_string_free(tsv, TRUE);
 g_free(settings);
 return ok;
}

gchar *insight_vivado_open(const gchar *filename, GError **error) {
 gchar *xpr = g_canonicalize_filename(filename, NULL);
 gchar *project = companion(xpr, ".geany");
 if (g_file_test(project, G_FILE_TEST_EXISTS)) {
  /* Opening never resyncs a linked companion or overwrites an unrelated one. */
  GKeyFile *existing = g_key_file_new();
  gchar *source = NULL;
  if (g_key_file_load_from_file(existing, project, G_KEY_FILE_NONE, error))
   source = g_key_file_get_string(existing, GROUP, "source", NULL);
  if (!source || strcmp(source, xpr)) {
   if (!error || !*error)
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                        "A .geany with the same name already exists and is not linked to this Vivado project");
   g_clear_pointer(&project, g_free);
  }
  g_free(source);
  g_key_file_unref(existing);
  g_free(xpr);
  return project;
 }
 Import state = {0};
 gboolean ok = read_xpr(xpr, &state, error);
 if (ok) {
  GKeyFile *config = g_key_file_new();
  g_key_file_set_string(config, "project", "name", state.stem);
  g_key_file_set_string(config, "project", "base_path", state.root);
  InsightProjectFiles *policy = insight_project_files_new();
  policy->automatic = FALSE;
  for (guint i = 0; ok && i < state.files->len; i++)
   ok = insight_project_files_set(policy, state.files->pdata[i], FALSE, TRUE, error);
  if (ok) ok = write_import(project, xpr, config, policy, &state, TRUE, error);
  insight_project_files_free(policy);
  g_key_file_unref(config);
 }
 import_clear(&state);
 g_free(xpr);
 if (!ok) g_clear_pointer(&project, g_free);
 return project;
}

InsightProjectFiles *insight_vivado_sync(const gchar *filename, GError **error) {
 GKeyFile *config = g_key_file_new();
 gchar *xpr = NULL;
 InsightProjectFiles *policy = NULL;
 if (!g_key_file_load_from_file(config, filename, G_KEY_FILE_KEEP_COMMENTS, error)) goto done;
 xpr = g_key_file_get_string(config, GROUP, "source", NULL);
 if (!xpr || !insight_vivado_is_xpr(xpr) || !g_path_is_absolute(xpr)) {
  g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "This Geany project is not linked to a Vivado .xpr");
  goto done;
 }
 Import state = {0};
 gboolean ok = read_xpr(xpr, &state, error);
 if (ok) {
  gchar *base = g_key_file_get_string(config, "project", "base_path", NULL);
  gchar *dir = g_path_get_dirname(filename);
  gchar *root = g_canonicalize_filename(base && *base ? base : ".", dir);
  policy = insight_project_files_read_project(config, root, filename, error);
  g_free(root);
  g_free(dir);
  g_free(base);
  ok = policy != NULL;
 }
 if (ok) {
  gchar **previous = g_key_file_get_string_list(config, GROUP, "files", NULL, NULL);
  GHashTable *old = g_hash_table_new(g_str_hash, g_str_equal);
  for (guint i = 0; previous && previous[i]; i++) g_hash_table_add(old, previous[i]);
  /* Apply only Vivado's delta. If a file was locally removed and is still
   * present in Vivado, leave it removed. Keep unrelated local additions. */
  for (guint i = policy->files->len; i > 0; i--) {
   const gchar *file = policy->files->pdata[i - 1];
   if (g_hash_table_contains(old, file) && !g_hash_table_contains(state.seen, file))
    g_ptr_array_remove_index(policy->files, i - 1);
  }
  for (guint i = 0; ok && i < state.files->len; i++)
   if (!g_hash_table_contains(old, state.files->pdata[i]) &&
       !g_ptr_array_find_with_equal_func(policy->excluded_files, state.files->pdata[i], g_str_equal, NULL))
    ok = insight_project_files_set(policy, state.files->pdata[i], FALSE, TRUE, error);
  g_hash_table_unref(old);
  g_strfreev(previous);
 }
 if (ok) ok = write_import(filename, xpr, config, policy, &state, FALSE, error);
 if (!ok && policy) {
  insight_project_files_free(policy);
  policy = NULL;
 }
 import_clear(&state);
done:
 g_free(xpr);
 g_key_file_unref(config);
 return policy;
}
