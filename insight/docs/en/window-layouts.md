# Window layouts and Tab Tray

Ten individual panels are managed: Project Files, Project Symbols, Folders,
Relations, Symbol Categories, File Types, Bookmarks, Clips, File Symbols and Context.

## Header controls

| Action | Effect |
| --- | --- |
| Float | Opens the panel in a separate window. |
| Dock (house) | Returns the panel to its home and selects its tab. |
| Maximize / Restore | Changes floating-window size while retaining normal bounds. |
| Minimize (minus) | Places the floating panel in the bottom Tab Tray. |
| Native window close | Docks the panel. |

Tooltips describe each action. Maximize and Minimize are enabled for floating
panels. Repeated floating/docking preserves panel contents and controls.

![Panel states](../media/panel-controls.svg)

## Bottom tray

Click a panel label to restore its floating window. An empty tray hides itself.
View → Panels → Tab Tray hides/shows the strip without restoring its windows.
Restore minimized panels works even when the strip is hidden. Activating a panel
from its menu restores its window too. Native iconification also enters the tray.

## Slots A–D

View → Layouts → Save current layout → Save as A–D captures the arrangement;
Load layout A–D restores it. The record includes splitters, visibility, current
project tab, Follow/Wrap, detached panels, their normal bounds and maximized/minimized
state, tray visibility and Window List settings. Floating panel bounds are limited
to the available monitor work area when restored.

## Additional files

View → Layouts → Save layout to file… writes `*.geany-layout`;
Load layout from file… applies it. These operations preserve slots A–D. Save a
loaded arrangement to a slot separately if desired. Cancel leaves the arrangement
unchanged. The native chooser confirms overwriting an existing file.

This is Geany Insight's INI format, without source text or project paths; Source
Insight XML is not imported. Files over 64 KiB, NUL bytes, missing fields and invalid
values are rejected before any window changes. Errors appear in the status bar.
Internal layout versions 1–7 remain readable with defaults for newer features.

## Try it

1. Float Project Files, resize it and minimize it to Tab Tray.
2. Save to A or to `review.geany-layout`.
3. Restore the panel from the tray and dock it.
4. Load the arrangement: the floating panel is minimized in the tray again.

Native Linux application captured during the automated build/restart scenario
(top part of the Project Files window):

![Panel controls](../media/panel-windows.jpg)

Arbitrary edge docking is not implemented. The eight project panels can be grouped as described below. The separate
Window List and the whole floating Project Window group are outside these ten
Tab Tray hosts. Linux was checked; Windows and Android were not run for this series.

## Main editor window

Layouts also capture the main window's normal size/position and maximized state.
A maximized layout retains the previously observed normal bounds: Restore returns
to those bounds instead of keeping the full-screen dimensions. Loading limits
bounds to the available monitor work area. Older layout formats leave the main
window unchanged. Window-manager minimum sizes still apply.

![Main window state](../media/main-window-layout.svg)

## Floating panel transparency

View → Panels → Enable transparent panels enables the opacity control in each
floating panel header. Click the brightness icon and adjust the slider from 40 to
100 percent; its tooltip shows the current value. 100 percent is opaque. Docked
panels hide this control. Disabling the option makes floating panels opaque while
retaining their individual values. The option is disabled initially.

The global switch and per-panel values are retained in sessions, slots A–D and
layout files. Older layouts use opaque panels with the option disabled. The
visible effect requires desktop compositing. Mouse input remains with the panel;
Source Insight's background click-through behavior is not implemented.

Linux checks include the GTK value and a changed screen pixel under Xvfb,
Openbox and xcompmgr, as well as layout/session round trips.

## Floating project panel groups

Float a project panel and click the tab/group icon in its header. Choose another
project panel to put both in one floating window with tabs; a docked target floats
automatically. Add more panels in the same way. Move tabs within a group to change
their order. Float this panel separately extracts only the selected panel to its
own window. Dock returns only that panel to its home. Closing the group docks all
remaining members without closing editor documents.

Minimize, Maximize and opacity apply to the whole window. A minimized group has
one Tab Tray button labeled with a panel name and the number of additional members.
Layouts and session settings retain membership, tab order and active tab. Invalid
group links, duplicate tab positions and inconsistent group visibility/opacity are
rejected by layout loading; invalid session groups fall back to separate windows.

Grouping covers Project Files, Project Symbols, Folders, Relations, Symbol
Categories, File Types, Bookmarks and Clips. File Symbols, Context, Window List
and the whole Project Window group remain separate. This increment uses the group
menu; dragging panels between windows is not yet provided.

Native Linux application captured during the group/restart scenario:

![Floating project panel group](../media/panel-groups.jpg)

![Group and separate](../media/panel-groups.svg)
