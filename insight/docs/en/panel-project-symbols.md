# Project symbols

Names from the project Ctags index, restricted to compatible languages.

## Controls

Filter names; sort by name, line or type. Group by symbol type creates expandable groups. Colors and icons distinguish functions (blue), types (purple), structures (teal), variables (green) and macros (amber). Selection uses the system theme for readability. Symbol Info opens details.

## Short tutorial

Reindex the project. Enable grouping and expand Functions. Single-click to preview a definition in Context; double-click to open its line in the editor. Drag the panel’s left boundary right to narrow it.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [symbol-info.md](symbol-info.md).


## Symbol type filter

The funnel button opens checked categories: functions/methods, types/structures, enumerations, enum constants, macros, members/fields, variables, local variables, namespaces/includes and other symbols. Checked categories remain visible. Show all resets the restriction; Hide all lets you select only the categories you need.

Type filtering combines with the name query and grouping, and does not remove index entries. The counter shows visible rows / rows loaded for the name query. File and project panels save their settings separately. Available kinds depend on the language parser; the source list is limited to the first 2000 file symbols or 500 project symbols matching the name query.

Tutorial: hide all types, enable only functions/methods, search part of a name and enable grouping. Restore the complete loaded list with Show all.


## Context menu and keyboard

Right-click selects a row without changing Context or the editor. The menu opens a definition, Symbol Info or lexical references; copies the name or path:line location of a saved file. Symbols and group headers support Show only this category and Hide this category; categories match the funnel menu. You can also show all types, toggle grouping, expand/collapse all groups and open this help page. Source-opening commands are disabled on group headers.

From the list, Ctrl+F focuses its name filter; Ctrl+C copies the name and Ctrl+Shift+C copies a saved source location. Ctrl+Enter opens the selected symbol and centers its line (on a group it toggles expansion); Alt+Enter opens Symbol Info. Escape returns focus to the editor. Shift+F10 or Menu opens the context menu.

Tutorial: collapse groups through the menu, focus the name filter and press Down. The first group expands and its first symbol is selected while the editor stays in place. Press Ctrl+Enter to navigate. Next, right-click a group and show only its category; restore others with Show all symbol types.

While the list is updating, navigation from its filter is temporarily disabled so an old result cannot open.
