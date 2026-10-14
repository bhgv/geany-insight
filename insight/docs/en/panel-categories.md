# Symbol Categories

A language-aware index browser for classes, functions/methods, structures, members, variables and other symbol types. Labels, colors and icons share the native parser mapping; a Ctags letter can mean different types in C++ and Python. Nested scopes form separate branches.

Class/structure/union/interface declaration rows also expand to their direct indexed scope members. These supplemental rows remain available in normal categories too; counters count unique loaded symbols rather than repeated display rows. Ambiguous same-name declarations receive no supplemental member link. Type aliases, overloads, inferred types and inherited members are not semantically resolved here.

## Controls

The shared symbol filter accepts fragments, `~fuzzy`, `?regex`, quoted exact names, `scope=` and `@path`. The active document's language scope applies. Initially 5000 matches are loaded; Load more adds 5000 up to the 250000-symbol index limit. The counter reports loaded / matching unique symbols. New queries reset the portion. Supplemental member rows obey the same query and loaded portion.

Selection previews in Context. Double-click or Ctrl+Enter opens a definition; double-click toggles a group. Right-click opens the menu without navigating Context/editor. Commands include open, Symbol Info, References, copy name/path/location, exact-name filtering, clearing, branches, columns, paging and help. Source actions are disabled on group headers.

Name, scope, file, line and kind columns support ordering, widths and visibility; name is mandatory. Columns and sorting persist independently of File Types. Refresh preserves selection and expanded branches using stable row identities. Switching RU/EN refreshes category labels and counts.

Keys: Ctrl+F filter, Ctrl+C name, Ctrl+Shift+C path:line, Ctrl+Enter open, Alt+Enter Info, Menu/Shift+F10 menu, Escape editor. In the entry, Down selects the first record, Enter opens it and Escape clears the query before returning focus. Use Primary on macOS. Invalid regex retains the previous result and blocks Enter opening.

## Short tutorial

1. Expand Classes, a declaration and its direct indexed fields/methods.
2. Find a Function/Method and invoke Alt+Enter.
3. Enter `~rn` or `scope=Widget` and inspect the count.
4. For a large project, press Load more.
5. Adjust columns and switch RU/EN; the selected symbol should not navigate the editor.

See [panel-project-symbols.md](panel-project-symbols.md). The question icon opens this page.
