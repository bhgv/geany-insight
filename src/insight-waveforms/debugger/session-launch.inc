/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Public simulation entry point and restart using the current testbench configuration.
 * RU: Вход в симуляцию и перезапуск с текущей конфигурацией тестового стенда. */

/* EN: Relaunch previous for parent with fresh runtime state and the same harness and sources.
 * RU: Запускает previous для parent с новым состоянием, прежним стендом и исходниками. */
static void simulation_restart_with_settings(WaveSimulation *previous, GtkWindow *parent)
{
    gchar *bin = g_path_get_dirname(previous->compiler);
    simulation_start(parent, bin, previous->include_dir, (const gchar *const *)previous->sources,
                      previous->interactive, previous->navigate, previous->show_context,
                      previous->open_location, previous->settings_directory, previous->project,
                      FALSE, previous);
    g_free(bin);
}

/* EN: Start sources with tools from bin and include_dir. parent owns dialogs; interactive
 * enables debugging; navigate, show_context and open_location provide editor integration;
 * settings_directory and project select saved settings; configure_ports opens setup.
 * RU: Запускает sources инструментами bin с include_dir. parent владеет окнами, interactive
 * включает отладку; navigate, show_context и open_location связывают её с редактором;
 * settings_directory и project задают настройки; configure_ports открывает настройку портов. */
void insight_waveforms_simulate(GtkWindow *parent, const gchar *bin, const gchar *include_dir,
                                const gchar *const *sources, gboolean interactive,
                                InsightWaveSource navigate, InsightWaveContext show_context,
                                InsightWaveLocation open_location, const gchar *settings_directory,
                                const gchar *project, gboolean configure_ports)
{
    simulation_start(parent, bin, include_dir, sources, interactive, navigate, show_context,
                      open_location, settings_directory, project, configure_ports, NULL);
}
