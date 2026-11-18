/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Isolated production comparison fixture and mode regressions without libgeany.
 * RU: Изолированная проверка настоящего сравнения и режимов без libgeany. */
#include <stdarg.h>
#include <glib/gstdio.h>
#include "../../src/insight-diff.c"
const gchar *insight_tr(const gchar *text) { return text; }
void insight_hex_show(GtkWidget *parent,const gchar *left,const gchar *right) { (void)parent;(void)left;(void)right; }
void insight_directory_show(GtkWidget *parent) { (void)parent; }
gchar *insight_i18n_printf(const gchar *format,...) {
 va_list args;va_start(args,format);gchar *s=g_strdup_vprintf(format,args);va_end(args);return s;
}
GtkWidget *insight_panel_help_button(const gchar *topic) { (void)topic;return gtk_button_new_with_label("?"); }
void insight_panel_help_attach(GtkWidget *widget,const gchar *topic) { (void)widget;(void)topic; }
static gchar *editor_path;static guint editor_line;

#include "diff-gui/legacy.inc"
#include "diff-gui/modes.inc"
#include "diff-gui/symbol-navigation.inc"
#include "diff-gui/symbol-browser.inc"
#include "diff-gui/symbol-changes.inc"

/* EN: Run isolated comparison regressions with argc/argv for GTK; failed assertions terminate the fixture.
 * RU: Запускает изолированные проверки с argc/argv для GTK; неудачная проверка завершает фикстуру. */
int main(int argc,char **argv)
{
#include "diff-gui/hunks-and-navigation.inc"
#include "diff-gui/overview-and-lifetime.inc"
#include "diff-gui/preferences-and-review.inc"
}
