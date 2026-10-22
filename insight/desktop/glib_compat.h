/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_GLIB_COMPAT_H
#define INSIGHT_GLIB_COMPAT_H
#include <gio/gio.h>

/* Keep the declared GLib 2.56 baseline, including standalone core modules. */
static inline gboolean insight_pattern_match_string(GPatternSpec *pattern, const gchar *text) {
#if GLIB_CHECK_VERSION(2, 70, 0) && !defined(INSIGHT_GLIB_COMPAT_FORCE_FALLBACK)
 return g_pattern_spec_match_string(pattern, text);
#else
 G_GNUC_BEGIN_IGNORE_DEPRECATIONS
 gboolean matches = g_pattern_match_string(pattern, text);
 G_GNUC_END_IGNORE_DEPRECATIONS
 return matches;
#endif
}

static inline gchar *insight_canonicalize_filename(const gchar *filename, const gchar *relative_to) {
#if GLIB_CHECK_VERSION(2, 58, 0) && !defined(INSIGHT_GLIB_COMPAT_FORCE_FALLBACK)
 return g_canonicalize_filename(filename, relative_to);
#else
 g_return_val_if_fail(filename != NULL, NULL);
 g_return_val_if_fail(relative_to == NULL || g_path_is_absolute(relative_to), NULL);
 GFile *file;
 if (relative_to && !g_path_is_absolute(filename)) {
  GFile *base = g_file_new_for_path(relative_to);
  file = g_file_resolve_relative_path(base, filename);
  g_object_unref(base);
 } else {
  file = g_file_new_for_path(filename);
 }
 gchar *path = g_file_get_path(file);
 g_object_unref(file);
 return path;
#endif
}
#endif
