# Classic and macOS-inspired interface

Open **Options → Interface style** or **View → Interface style** and choose **Classic** or **macOS style**. Switching immediately affects existing application windows and future windows, including Context, Window List, AI and Diff/Merge. Both menus stay synchronized. No restart is required; the choice persists between launches.

![Appearance layers](../media/interface-style.svg)

The macOS-inspired style provides light gradient surfaces, translucent panel layers, rounded fields/buttons/tabs, thin separators, quiet scrollbars and a blue accent. Active editor tabs remain bold and compact Diff/Merge controls remain compact. UI fonts use the first installed family: SF Pro Text, Helvetica Neue, Segoe UI, Noto Sans, system sans-serif. Apple fonts are not distributed.

**Reduce transparency** makes the main surfaces opaque. Translucency blends RGBA layers within the window. This portable GTK style does not implement behind-window desktop blur, system Liquid Glass, reposition native window buttons or replace native window decorations. The OS controls the frame and system dialogs. Panel layout and functions are preserved.

Interface style is independent of the source **Visual theme**. It preserves Scintilla font/syntax colors, text, selection, savepoint and Undo. A light shell can be combined with dark code. Explicit panel Font & Color overrides keep priority. **Classic** removes the appearance layer and restores the current GTK look without resetting the source palette or other preferences.

Settings are stored in `insight/interface-style.conf` under the configuration directory. Atomic save failures leave the previous style active. macOS is the initial default for a new profile; an explicitly saved style remains in effect. Installed CSS is `styles/appearance-macos.css`, scoped to the `insight-macos` class; the system GTK theme is not changed. Windows are styled on mapping, without background redraw timers.

## Actual application appearance

[Real Classic and macOS-inspired captures are available in the successful Linux GUI run artifact](https://github.com/bhgv/geany-insight/actions/runs/39900274485/artifacts/12479155454), as `interface-classic.jpg` and `interface-macos.jpg`. They show the production GTK editor in an isolated Linux/Xvfb fixture with the same demo source and panel widths, not mockups or a native macOS run.

Verified on Linux: [108 full-editor checks](https://github.com/bhgv/geany-insight/actions/runs/39900274485), including switching, saved startup state, failed-save rollback, future windows and independent code palette/font behavior. Autotools and GTK 3.22 / GLib 2.56 builds passed. Windows/macOS GUI was not run in this stage.

## Compact menus and independent windows

**View / Options → Interface style → macOS — Independent Windows** moves menu and toolbar into a separate top-level palette. File Symbols, Project and Context occupy three other GTK windows. Native source tabs remain in the original editor window, preserving document identity and keyboard handling; these are not MDI children. Existing panel Float/Dock and project-tab grouping controls remain available.

Choosing normal macOS or Classic restores the pre-entry layout. Closing the tools palette also joins windows and selects normal macOS. Panel Close uses existing docking behavior. The style choice survives restart; entry arranges windows in the monitor work area. Positions are not a separately persisted workspace yet: use named Layouts for custom arrangements. This is an application palette, not an OS global menubar. Rebuilt native toolbars return to the palette.

Menus explicitly reset GTK minimum row heights, margins and icon/check spacing, with 10pt UI type and compact padding. Font preference is SF Pro Text, Helvetica Neue, Inter, Segoe UI, Noto Sans, sans-serif, depending on installed families. Apple fonts and SF Symbols are not redistributed. Available themed symbolic icons replace corresponding named/stock icons at 16px; Classic restores originals. Bespoke application icons retain their artwork.

New floating windows use GTK client-side headerbars with round close/minimize/maximize controls on the left, rounded decoration edges and soft CSS shadows. Outer frames and shadow visibility depend on the platform/window manager/compositor. Already realized editor windows retain their OS frame; no desktop blur is added. Source fonts, syntax palettes, selection and Undo are unchanged.

![Independent application windows](../media/independent-windows.svg)

macOS tabs use 9pt labels, 16px minimum tab content height, small rounded corners, reduced margins, unobtrusive close buttons and a light selected-tab surface. The active source filename remains bold; inactive filenames remain normal. Classic retains its previous tab styling.

## Compact menus / Компактные меню

Menu rows use a 16 px icon slot instead of the former 40 px reservation. Unknown
commands have no placeholder icon; check/radio rows retain their native indicator.
The native accelerator label and shortcuts stay intact. In macOS styles all ordinary
rows use compact spacing, including Find Next, Find in Files and Replace. Menu width
is determined by the longest label and shortcut, with a small icon/arrow allowance.
Menus have a soft shadow and a layered blue/lilac gradient. Reduce transparency
removes the material gradients and makes menus and chrome opaque; unticking restores
the translucent layers immediately. Source text remains opaque. RGBA visuals are
selected before new windows realize; desktop blending and outer shadows depend on
the desktop compositor. No desktop blur or global fading of source text is used.

Строки меню используют место под значок 16 px вместо прежних 40 px. Для неизвестных
команд нет значка-заглушки; чекбоксы и радиокнопки используют собственный индикатор.
Пункты «Найти следующее», «Найти в файлах» и «Заменить» имеют одинаковые компактные
отступы. Ширина определяется самым длинным названием и сочетанием клавиш.
Мягкая тень и голубовато-сиреневый градиент оформляют меню. «Уменьшить прозрачность»
сразу убирает градиент материала и делает поверхности непрозрачными, снятие отметки
возвращает слои. Исходник остаётся непрозрачным. Смешивание с рабочим столом и внешние
тени зависят от композитора ОС; размытие рабочего стола не применяется.
