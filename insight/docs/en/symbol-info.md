# Symbol Info

Open Navigate → Symbol Info… at an identifier, or use Symbol Info… under Project Symbols or File Symbols for the selected row. The reusable nonmodal window offers a name/qualified-name query, a candidate selector, parser metadata and a read-only, syntax-colored Scintilla source view.

Refresh queries a private index snapshot and reparses/imports the current editor file when available (up to 2 MiB). Other project files use the published workspace index; reindex the workspace after external changes. Candidate metadata includes name/signature, kind, recorded type, scope, file:line and inheritance. Missing metadata stays blank. Up to 512 candidates are shown; local/file candidates are preferred for initial display, but no automatic semantic overload resolution is claimed.

Refresh preserves a manually selected candidate by path, line, name, kind, scope and signature when it still exists. Opening Symbol Info at another location sets a new preferred location.

The source view uses unsaved open buffers before reading the disk. Untitled current buffers use stable document identities internally and display as untitled. Jump verifies the displayed source snapshot is still current, opens the selected location, centers it and closes the information window. A changed or unavailable source requires Refresh. References searches the selected name across the project in Relations and hides the popup so the results are visible. Copy details copies the displayed metadata; Close/Escape destroys the window.

Source preview supports the existing decoder and 2 MiB text limit. Nominal recorded types resolve through indexed aliases in declaration scope using the dialog's retained index snapshot. Unsupported or ambiguous types remain unexpanded; expression types and template instantiation are not inferred. Complete brace/Python/Lua functions show their lexical size in lines, including the definition line. Prototypes, unsupported/incomplete bodies and uncertain same-line owners have no size. These are source-range counts, not complexity or semantic metrics. Read-only code scrolling does not edit either the source or the main editor.

Leave file open loads the displayed file into a normal editor buffer without closing the information window. It restores the previously active editor buffer when that buffer still exists and positions a newly loaded buffer at the displayed definition. The button is disabled for already-open sources. The same snapshot checks protect loading/jumping; newly opened files appear in Window List. Ctrl+Enter jumps and Ctrl+R refreshes while the popup has focus.

Validation: native Linux editor build and the real-editor GUI test passed: metadata/source, UTF-8 paths, opening a closed project source in the background, untitled buffer parsing/Jump, stale-snapshot rejection, Window List preview-only selection/activation/filtering, cancellation of native Save/Close and normal shutdown. Windows CI includes this scenario.

Size behavior follows [Symbol Info in the Source Insight guide](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Symbol_Info.htm), with explicit lexical limits.
