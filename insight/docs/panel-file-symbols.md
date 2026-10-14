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

Type filtering combines with the name query and grouping, and does not remove index entries. The counter shows visible rows / rows loaded for the name query. File and project panels save their settings separately. Available kinds depend on the language parser; the source list is limited to the first 2000 file symbols or 500 project symbols matching the name query.

Tutorial: hide all types, enable only functions/methods, search part of a name and enable grouping. Restore the complete loaded list with Show all.
