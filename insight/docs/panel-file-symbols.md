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
