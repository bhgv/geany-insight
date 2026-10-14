# File symbols

Functions, types and variables in the current buffer.

## Controls

Filter, sorting and Symbol Info apply to the current file. Colors and icons match Project Symbols. The grouping checkbox creates type groups and preserves selection when rebuilding.

## Short tutorial

Open a file containing functions and structures. Enable grouping. Select a function for Context, then double-click to open it.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [panel-project-symbols.md](panel-project-symbols.md).


## Symbol type filter

The funnel button opens checked categories: functions/methods, types/structures, enumerations, enum constants, macros, members/fields, variables, local variables, namespaces/includes and other symbols. Checked categories remain visible. Show all resets the restriction; Hide all lets you select only the categories you need.

Type filtering combines with the name query and grouping, and does not remove index entries. The counter shows visible rows / rows loaded for the name query. File and project panels save their settings separately. Available kinds depend on the language parser; the initial source list contains up to 2000 file symbols or 500 project symbols matching the name query. Load more appends another portion.

Tutorial: hide all types, enable only functions/methods, search part of a name and enable grouping. Restore the complete loaded list with Show all.


## Context menu and keyboard

Right-click selects a row without changing Context or the editor. The menu opens a definition, Symbol Info or lexical references; copies the name or path:line location of a saved file. Symbols and group headers support Show only this category and Hide this category; categories match the funnel menu. You can also show all types, toggle grouping, expand/collapse all groups and open this help page. Source-opening commands are disabled on group headers.

From the list, Ctrl+F focuses its name filter; Ctrl+C copies the name and Ctrl+Shift+C copies a saved source location. Ctrl+Enter opens the selected symbol and centers its line (on a group it toggles expansion); Alt+Enter opens Symbol Info. Escape returns focus to the editor. Shift+F10 or Menu opens the context menu.

Tutorial: collapse groups through the menu, focus the name filter and press Down. The first group expands and its first symbol is selected while the editor stays in place. Press Ctrl+Enter to navigate. Next, right-click a group and show only its category; restore others with Show all symbol types.

While the list is updating, navigation from its filter is temporarily disabled so an old result cannot open.


## Loading a large outline

A + after the total counter means additional name-matching symbols remain. The + button beside the counter and Load more symbols in the context menu append the next 2000 file rows or 500 project rows. The button is icon-only; its tooltip states the portion size. Once the list is exhausted, the button and partial marker disappear.

The type filter applies to loaded rows. If a category is still empty, load another portion or refine the name: name search examines the entire available index. A new query resets the portion size; changing the file resets its outline, and changing the project or language profile resets the project outline. Loading runs in the background and leaves the editor in place. The index ceiling remains 250000 symbols.

Tutorial: open a large file, retain only functions, find the counter ending in + and load another portion. A function beyond the initial portion can then appear. To find a known name quickly, enter it in the filter instead of repeatedly loading portions.


## Sorting the outline

The sort selector offers name, source line, symbol type, scope and file path. The adjacent arrow button toggles descending order; its label appears in a tooltip. Clicking a column header also changes the order and updates these controls, including grouped and filtered views. File and project outlines save their sort field and direction independently in workbench.conf.

Tutorial: choose source line for a file outline, then toggle the arrow to inspect declarations from the end of the file. Choose scope or file path in Project Symbols to gather related entries. Sorting leaves the editor and Context in place.


## Locate the caret symbol

The target icon beside the type controls and Locate symbol at caret in the outline context menu reveal the current declaration. For supported brace languages in buffers up to 2 MiB, the command first looks for the containing indexed function; otherwise it selects the nearest indexed declaration starting at or before the caret line. It does not infer indentation-based function boundaries.

The command expands a collapsed group and centers the matching row without opening a source location or updating Context. If there is no earlier declaration, or the matching row is hidden by the name/type filter or outside the loaded portion, a status message explains the result. Filters and loaded portions stay as selected; refine the query or load more and retry. The command waits for the current file outline to finish updating.

Tutorial: place the caret inside a C function, collapse its outline group and press the target button. The function row is revealed while the editor remains at the same position. Filter for a different name and repeat: the filter stays intact and the status bar explains why the current function cannot be shown.


## Column layout

Drag the boundary between column headers to resize a column. Drag a header horizontally to move its column; click it to sort. Long values are abbreviated with an ellipsis, and the list can scroll horizontally. The file and project outlines independently save column widths and order when the workbench settings are saved. Grouping, filtering and loading more preserve this layout.

Reset symbol columns in the outline context menu restores the original column order and widths for this panel. It keeps the current query, type filters, grouping and sort order. Older settings without a layout use the defaults; invalid saved layouts are ignored.

Tutorial: widen Symbol and move Line before it in File Symbols. In Project Symbols move File beside Symbol and widen it. Restart to restore each layout, or reset just one panel through its context menu.
