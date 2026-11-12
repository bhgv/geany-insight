/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Capture real compact-panel allocations before assertions, including clipping and active header controls.
 * RU: Фиксирует настоящие размеры компактных панелей до проверок, включая обрезку и кнопки активной панели. */

/* EN: Report control's position inside host without requiring it to fit; name identifies the diagnostic row.
 * RU: Показывает положение control внутри host без требования вместимости; name обозначает строку диагностики. */
static void panel_control_diagnostic(GtkWidget *host, GtkWidget *control, const gchar *name)
{
    if (!control || !gtk_widget_get_mapped(control))
        return;
    gint x = 0, y = 0;
    gboolean translated = gtk_widget_translate_coordinates(control, host, 0, 0, &x, &y);
    gint width = gtk_widget_get_allocated_width(control);
    g_printerr("PROJECT CONTROL %s translated=%d x=%d width=%d host=%d inside=%d\n",
        name, translated, x, width, gtk_widget_get_allocated_width(host),
        translated && x >= 0 && x + width <= gtk_widget_get_allocated_width(host));
}

/* EN: Capture the real window to optional INSIGHT_PANEL_DIAGNOSTIC_CAPTURE; report capture failures without masking assertions.
 * RU: Сохраняет настоящее окно в необязательный INSIGHT_PANEL_DIAGNOSTIC_CAPTURE; ошибки снимка не скрывают проверки. */
static void panel_diagnostic_capture(void)
{
    const gchar *path = g_getenv("INSIGHT_PANEL_DIAGNOSTIC_CAPTURE");
    if (!path || !*path)
        return;
    GdkPixbuf *picture = gdk_pixbuf_get_from_window(gtk_widget_get_window(window), 0, 0,
        gtk_widget_get_allocated_width(window), gtk_widget_get_allocated_height(window));
    if (!picture)
    {
        g_printerr("Project panel diagnostic capture unavailable\n");
        return;
    }
    GError *error = NULL;
    if (!gdk_pixbuf_save(picture, path, "jpeg", &error, "quality", "90", NULL))
        g_printerr("Project panel diagnostic capture: %s\n", error->message);
    g_clear_error(&error);
    g_object_unref(picture);
}

/* EN: Observe phase two once after allocation; data is unused and diagnostics never alter panel geometry.
 * RU: Однократно наблюдает вторую фазу после размещения; data не используется, диагностика не меняет геометрию. */
static gboolean panel_geometry_diagnostics(gpointer data)
{
    (void)data;
    if (phase != 2 || !window || !book || !gtk_widget_get_mapped(book))
        return G_SOURCE_CONTINUE;
    GtkWidget *host = named(window, "insight-project-width-host");
    if (!host)
        return G_SOURCE_CONTINUE;
    gint minimum = 0, natural = 0;
    gtk_widget_get_preferred_width(book, &minimum, &natural);
    g_printerr("PROJECT GEOMETRY split=%d position=%d host=%d book=%d minimum=%d natural=%d\n",
        gtk_widget_get_allocated_width(split), gtk_paned_get_position(GTK_PANED(split)),
        gtk_widget_get_allocated_width(host), gtk_widget_get_allocated_width(book), minimum, natural);
    width_diagnostics(host, 0);
    GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),
        gtk_notebook_get_current_page(GTK_NOTEBOOK(book)));
    panel_control_diagnostic(host, named(page, "insight-panel-toggle-dock"), "dock");
    panel_control_diagnostic(host, named(page, "insight-panel-help"), "help");
    panel_diagnostic_capture();
    return G_SOURCE_REMOVE;
}

/* EN: Wait for three equal geometry samples before assertions; the global timeout bounds waiting.
 * RU: Ожидает трёх одинаковых снимков геометрии перед проверками; ожидание ограничено общим таймером.
 */
static gboolean panel_allocations_settled(void)
{
    static gint previous[4];
    static guint stable;
    GtkWidget *host = named(window, "insight-project-width-host");
    gint current[] = {gtk_widget_get_allocated_width(split), gtk_paned_get_position(GTK_PANED(split)),
        gtk_widget_get_allocated_width(host), gtk_widget_get_allocated_width(book)};
    if (memcmp(previous, current, sizeof current))
    {
        memcpy(previous, current, sizeof current);
        stable = 0;
    }
    return ++stable >= 3;
}

/* EN: Require a mapped control to fit horizontally within the visible host, including clipping ancestors.
 * RU: Требует, чтобы отображаемый control помещался по горизонтали в видимый host с учётом обрезки.
 */
static void panel_control_inside(GtkWidget *host, GtkWidget *control)
{
    g_assert_nonnull(control);
    g_assert_true(gtk_widget_get_mapped(control));
    gint x, y;
    g_assert_true(gtk_widget_translate_coordinates(control, host, 0, 0, &x, &y));
    g_assert_cmpint(x, >=, 0);
    g_assert_cmpint(x + gtk_widget_get_allocated_width(control), <=, gtk_widget_get_allocated_width(host));
}
