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
Internal layout versions 1–4 remain readable with defaults for newer features.

## Try it

1. Float Project Files, resize it and minimize it to Tab Tray.
2. Save to A or to `review.geany-layout`.
3. Restore the panel from the tray and dock it.
4. Load the arrangement: the floating panel is minimized in the tray again.

Native Linux application captured during the automated build/restart scenario
(top part of the Project Files window):

![Panel controls](../media/panel-windows.jpg)

Arbitrary edge dragging and free panel grouping are not implemented. The separate
Window List and the whole floating Project Window group are outside these ten
Tab Tray hosts. Linux was checked; Windows and Android were not run for this series.
