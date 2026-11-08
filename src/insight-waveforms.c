/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Assembles the Verilog debugger and waveform viewer from private modules.
 * Include order follows the static dependencies within this translation unit.
 * RU: Собирает отладчик Verilog и просмотр диаграмм из внутренних модулей.
 * Порядок подключения соответствует зависимостям внутри единицы трансляции.
 */
#include "insight-waveforms.h"
#include "msgwindow.h"
#include "document.h"
#include "ui_utils.h"
#include "utils.h"
#include "insight-ai.h"
#include "insight-help.h"
#include "insight-i18n.h"
#include "../insight/desktop/glib_compat.h"
#include "../insight/core/runtime.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

#include "insight-waveforms/setup-dialogs.inc"
#include "insight-waveforms/vcd-parser.inc"
#include "insight-waveforms/wave-tracks.inc"
#include "insight-waveforms/wave-instruments.inc"
#include "insight-waveforms/wave-drawing.inc"
#include "insight-waveforms/wave-controls.inc"
#include "insight-waveforms/wave-signal-map.inc"
#include "insight-waveforms/wave-session.inc"
#include "insight-waveforms/wave-loading.inc"
#include "insight-waveforms/simulation-state.inc"
#include "insight-waveforms/harness-memory.inc"
#include "insight-waveforms/harness-buses.inc"
#include "insight-waveforms/harness-models.inc"
#include "insight-waveforms/harness-instruments.inc"
#include "insight-waveforms/harness-generator.inc"
#include "insight-waveforms/port-source.inc"
#include "insight-waveforms/port-bindings.inc"
#include "insight-waveforms/port-browser.inc"
#include "insight-waveforms/device-catalog.inc"
#include "insight-waveforms/device-port-fields.inc"
#include "insight-waveforms/device-wizard.inc"
#include "insight-waveforms/testbench-preview.inc"
#include "insight-waveforms/port-setup.inc"
#include "insight-waveforms/source-breakpoints.inc"
#include "insight-waveforms/simulator-process.inc"
#include "insight-waveforms/live-output-records.inc"
#include "insight-waveforms/signal-snapshots.inc"
#include "insight-waveforms/signal-stop-rules.inc"
#include "insight-waveforms/console-events.inc"
#include "insight-waveforms/live-inputs.inc"
#include "insight-waveforms/breakpoint-actions.inc"
#include "insight-waveforms/frame-actions.inc"
#include "insight-waveforms/signal-history.inc"
#include "insight-waveforms/signal-stop-panel.inc"
#include "insight-waveforms/signal-tables.inc"
#include "insight-waveforms/simulation-preview.inc"
#include "insight-waveforms/observer-lifecycle.inc"
#include "insight-waveforms/simulation-window.inc"
