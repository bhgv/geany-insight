# Classic and macOS-inspired interface

Open **Options → Interface style** or **View → Interface style** and choose **Classic** or **macOS style**. Switching immediately affects existing application windows and future windows, including Context, Window List, AI and Diff/Merge. Both menus stay synchronized. No restart is required; the choice persists between launches.

![Appearance layers](../media/interface-style.svg)

The macOS-inspired style provides light gradient surfaces, translucent panel layers, rounded fields/buttons/tabs, thin separators, quiet scrollbars and a blue accent. Active editor tabs remain bold and compact Diff/Merge controls remain compact. UI fonts use the first installed family: SF Pro Text, Helvetica Neue, Segoe UI, Noto Sans, system sans-serif. Apple fonts are not distributed.

**Reduce transparency** makes the main surfaces opaque. Translucency blends RGBA layers within the window. This portable GTK style does not implement behind-window desktop blur, system Liquid Glass, reposition native window buttons or replace native window decorations. The OS controls the frame and system dialogs. Panel layout and functions are preserved.

Interface style is independent of the source **Visual theme**. It preserves Scintilla font/syntax colors, text, selection, savepoint and Undo. A light shell can be combined with dark code. Explicit panel Font & Color overrides keep priority. **Classic** removes the appearance layer and restores the current GTK look without resetting the source palette or other preferences.

Settings are stored in `insight/interface-style.conf` under the configuration directory. Atomic save failures leave the previous style active. Classic is the initial default. Installed CSS is `styles/appearance-macos.css`, scoped to the `insight-macos` class; the system GTK theme is not changed. Windows are styled on mapping, without background redraw timers.
