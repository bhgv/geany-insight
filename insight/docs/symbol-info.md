# Symbol Info

Open Navigate → Symbol Info… at an identifier, or use Symbol Info… under Project Symbols or File Symbols for the selected row. The reusable nonmodal window offers a name/qualified-name query, a candidate selector, parser metadata and a read-only, syntax-colored Scintilla source view.

Refresh queries a private index snapshot and reparses/imports the current editor file when available (up to 2 MiB). Other project files use the published workspace index; reindex the workspace after external changes. Candidate metadata includes name/signature, kind, recorded type, scope, file:line and inheritance. Missing metadata stays blank. Up to 512 candidates are shown; local/file candidates are preferred for initial display, but no automatic semantic overload resolution is claimed.

The source view uses unsaved open buffers before reading the disk. Untitled current buffers use stable document identities internally and display as untitled. Jump verifies the displayed source snapshot is still current, opens the selected location, centers it and closes the information window. A changed or unavailable source requires Refresh. References searches the selected name across the project in Relations and hides the popup so the results are visible. Copy details copies the displayed metadata; Close/Escape destroys the window.

Source preview supports the existing decoder and 2 MiB text limit. Inferred expression types and full semantic resolution remain open; lexical definition sizes are described below. Read-only code scrolling does not edit either the source or the main editor.

Leave file open loads the displayed file into a normal editor buffer without closing the information window. It restores the previously active editor buffer when that buffer still exists and positions a newly loaded buffer at the displayed definition. The button is disabled for already-open sources. The same snapshot checks protect loading/jumping; newly opened files appear in Window List. Ctrl+Enter jumps and Ctrl+R refreshes while the popup has focus.

Validation: native Linux editor build and the real-editor GUI test passed: metadata/source, UTF-8 paths, opening a closed project source in the background, untitled buffer parsing/Jump, stale-snapshot rejection, Window List preview-only selection/activation/filtering, cancellation of native Save/Close and normal shutdown. Windows GUI validation remains separate.

Для результатов функций C/C++ nominal-тип использует тот же resolver, что и Context: зависимый параметр шаблона не выбирает глобальный класс; явный `::T` и обычные результаты сохраняются. Выведение аргументов шаблонов остаётся открытым.

## Named definition size

Symbol Info also shows Definition size (lines) for complete named classes,
structs, unions, enums, interfaces and namespaces in C/C++/C#/Java lexical
buffers, and for Python class suites. The range starts at the parser's
definition line and ends at the closing body delimiter or suite. Open unsaved
and untitled buffers use their current text. Existing function-size display
is unchanged. These are lexical source spans, not complexity measurements.

The header must match both the symbol name and declaration kind. Forward
declarations, namespace aliases, incomplete bodies, unknown header syntax and
a later declaration on the same parser line keep size unknown. Supported
headers include common modifiers, scoped enums, C++ template prefixes and
base clauses; macros/attributes and specialized syntax may still decline.
JavaScript/TypeScript nominal sizes are deferred because their lexical mask
does not yet cover all regular-expression literals.
