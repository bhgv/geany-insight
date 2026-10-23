# Function targets and Symbol Info categories

Navigate and the editor context menu provide **Jump To Prototype…** and **Jump To Implementation…**. Both open the existing Symbol Info chooser with the appropriate category selected. Inspect the candidates and source preview, then choose **Jump** (Ctrl+Enter). There is no automatic overload guess, including when only one row is shown.

![Function target workflow](media/function-targets.svg)

## Categories and counts

Symbol Info now offers All indexed symbols, Function prototypes, Function implementations, Types and namespaces, and Variables and members. A compact counter shows **displayed / matching** candidates after category and source-language filtering. Filtering happens before the 512-row presentation limit, so an implementation after hundreds of same-name prototypes remains discoverable.

At most 16384 raw name matches are considered. Broader queries are refused explicitly; use a qualified name such as `api::work`. The chooser preserves the native parser's scopes and signatures. Exact overload resolution, argument/type inference, template instantiation and include visibility are not inferred. Parser support determines which declarations are recorded as prototypes. Category selection changes only this window.

## Related targets

Three compact buttons beside the category selector provide:

| Button tooltip | Action |
| --- | --- |
| Browse function prototypes | Query prototypes for the selected candidate's qualified name |
| Browse function implementations | Query implementations in the same recorded scope, retaining overloads |
| Browse underlying nominal type | Browse the resolved type behind a variable/alias, or a function's declared result |

The first two buttons are disabled for noncallable symbols. The nominal-type button appears enabled only when the existing bounded type resolver finds an indexed destination. Cyclic, ambiguous and unknown types do not produce an active link. This follows aliases to a nominal type; it does not walk to the oldest inheritance ancestor or deduce runtime/template types.

## Source guards

Opening either command requires a single normal selection in a buffer up to 2 MiB and a code identifier rather than a comment/string. Previewing and switching categories do not open files in the editor, move its caret, edit text or alter Undo. Jump reuses Symbol Info's before/after source-digest checks and native navigation history; Back returns to the original position. If the source changes after preview, Refresh before jumping.

Refresh uses an index snapshot and updates tags for the current buffer. It does not initiate a full project rescan. Opening ordinary Symbol Info starts with All indexed symbols; counterpart buttons select the relevant category explicitly. Category choices are not stored in project configuration.

Source Insight references: [Jump To Prototype](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Jump_To_Prototype.htm) and [Jump To Caller](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Jump_To_Caller.htm). Caller selection remains in the separate lexical Call sites Relations mode; this series adds declaration/implementation browsing, not semantic caller resolution.

## Type command, shortcuts and query history

**Jump To Base Type…** in Navigate and the editor menu captures the code identifier. When exactly one indexed declaration has a resolved nominal type, the chooser previews that type directly. Otherwise it keeps the declaration candidates: select one and use **Browse underlying nominal type**. Unknown, cyclic and ambiguous types are not guessed. This is tag-based browsing rather than local-expression or overload inference; **Jump** remains explicit and source-checked.

Four configurable **Editor** keybindings are initially unassigned: Symbol Info, Jump To Prototype, Jump To Implementation and Jump To Base Type. Existing shortcut IDs are preserved.

The window has independent **Back / Forward** query history (`Alt+Left / Alt+Right`) and **Previous / Next candidate** (`Alt+Up / Alt+Down`). Up to 64 queries retain category, preferred location and overload identity. History re-queries the current index and rereads the preview; it is not an old source snapshot and never navigates the source editor. New queries after Back discard the forward branch; Refresh and candidate changes preserve it. Candidate traversal stops at either end without wrapping. Empty/oversized queries are not recorded. Closing the window discards its history; nothing is saved in project settings. This query history is a usability extension rather than a claim of a matching Source Insight dialog.

Reference: [Jump To Base Type](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Jump_To_Base_Type.htm).

## Lookup options and reusing parsed symbols

**Navigate → Symbol Info Lookup Options…** opens the modeless window and expands its compact options section. The same section is available inside Symbol Info. Both choices are stored in the workbench configuration and apply to Symbol Info plus prototype, implementation and nominal-type choosers:

- **Parse locally before lookup** (initially enabled): update stale tags for the current saved buffer before querying. If its native tag generation is already current, reuse it. Repeated Refresh, categories and query history do not reparse an unchanged saved buffer or invalidate the shared index. Untitled buffers use the existing private in-memory parse when enabled.
- **Find symbols across different language types** (initially disabled): include any indexed source language before the category and 512-row filters. With it disabled, the existing language compatibility rules remain active. Type metadata still resolves within the selected declaration's compatible languages.

Disabling local parsing uses the available cached tags, which may be stale; it does not disable the editor's ordinary background tag updates. Re-enable the option to update stale current-buffer tags explicitly. The source preview and Jump digest guards remain active under either setting. History uses the current lookup options, rather than restoring old options from an earlier visit.

These controls do not change automatic Context, F12, project indexing, project membership or the search paths of other panels. They do not import another project's database or add an external symbol path. Opening the options alone does not start a project rescan. Existing configuration files retain their prior behavior; a malformed local-parse value retains the enabled default.

Reference: [Symbol Lookup Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Symbol_Lookup_Options.htm).

## Source scopes and open non-project files

**Lookup sources** offers three persistent choices:

| Scope | Candidates |
| --- | --- |
| Project and open files (default) | Completed project membership plus named open buffers, including files outside the project; the current untitled buffer is supported |
| Project files only | Files in the published project membership, even if another non-project buffer is active |
| Current buffer only | The active named or untitled buffer, excluding closed headers and other open tabs |

Without a workspace root, Project files only falls back to the current buffer. Source membership is checked before the 16384 raw name-match bound and 512 displayed-candidate limit; category and language filters still apply. History uses the current scope. A nominal-type link is enabled only when its resolved destination is also eligible in this scope.

Other named open buffers contribute **already available native tags** to the private lookup snapshot. They are not reparsed by these queries; ordinary editor background updates still apply. The existing local-parse option affects only the active eligible buffer. Unsaved source changes appear after those tags update; source previews and Jump retain their digest checks. Closing an external buffer removes its eligibility immediately, even while the shared index is refreshing. Opening external files does not add them to project membership or its database.

Additional open-file capture is bounded to 128 named buffers, 2 MiB per buffer and a conservative 250000-tag import-work budget including the cloned index. A failed/oversized snapshot clears candidates and disables Jump rather than publishing a partial list; narrow the scope and retry. This is not cross-project database import, an external symbol path or an expansion of automatic Context/F12 search.

Reference: [Symbol Lookup Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Symbol_Lookup_Options.htm), “Find symbols in open files not part of the current project”.
