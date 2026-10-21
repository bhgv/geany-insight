# Replace Files

**Search → Replace Files…** opens multi-file replacement. The command can also receive a shortcut in the Editor keybinding category. Preview first; **Apply selected to buffers** changes editor buffers only. Nothing is automatically saved to disk. Use normal Save / Save All afterwards, and Undo in each changed file to revert its batch.

![Native Replace Files window on Linux](../media/replace-files.jpg)

Actual window of the complete Linux editor under Xvfb, using synthetic source/project fixtures. The second replacement in `a.c` is deselected and the comment is skipped; before/after panes show syntax colors and line numbers.

![Scope, preview and explicit application](../media/replace-files.svg)

## Scope and patterns

Enter literal text or an expression in **Old**, and the replacement in **New**. An empty replacement deletes matches. Successful previews retain up to 20 Old/New entries, masks and options privately in `insight-replace-files.conf`. Changing criteria invalidates Apply until another Preview.

Scopes are **Project files** (actual indexed membership), **Open documents** (including unsaved edits), **Current document**, **Chosen files** and **Folder**. Wait for project indexing when using Project files. **Choose files… / Choose folder…** select the corresponding scope. Semicolon-separated basename masks such as `*.c;*.h` restrict inputs; blank or `*` matches all. Windows filename matching is case-insensitive.

**Include subdirectories** applies only to Folder scope. `.git`, `.hg`, `.svn` and symbolic links are not traversed. Folder scope is independent of project exclusions. Open editor buffers take precedence over disk contents in every scope.

## Replacement options

- **Case sensitive**, **Whole words only**, **Preserve old case** control matching and casing. Uniform upper/lowercase and initial capitals are preserved; mixed-case identifiers use the literal replacement.
- **Regular expressions** uses GLib/PCRE Perl-compatible expressions. Replacement supports `\1`–`\99`, `\g<name>`, `\g<number>`, `\n`, `\r`, `\t`, `\\` and capture 0. With Regex off, New is literal. Capture references are validated even with no matches. **Dot matches line endings** affects Regex only.
- **One occurrence per line** retains the first eligible occurrence on each logical line. CRLF is one line ending; zero-width matches inside CRLF are excluded. UTF-8 characters are never split.
- **All text / Code only / Skip comments / Comments only** use lexical classification for C/C++ and C-style files, JS/TS, Python, Lua and Shell based on extension. Code only also excludes strings. Unsupported extensions produce a per-file error rather than an invented dialect. This is not a preprocessor/compiler: inactive conditional code remains included, and complex language extensions may need All text.

## Inspect and select

**Preview replacements** collects the scope, then reads closed files and builds matching snapshots in a cancellable worker without changing buffers or disk. Folder enumeration is bounded but runs on the UI thread before that worker; very slow mounted folders can delay its start. **Stop preview** cancels; cancelled/superseded plans cannot apply.

The file table shows path, selected replacement count and state. Preview initially selects at most 128 eligible files; larger scopes remain available for later batches. Unsupported, unavailable and read-only sources are marked separately. Toggle individual files or use **Select all files / Deselect files**. The match table shows line, column, old and new text; its checkboxes and **Select all matches / Deselect matches** let you confirm individual occurrences before applying.

Full read-only **before/after** panes have syntax colors and line numbers. Selecting a match highlights its location. **Diff / Merge preview** opens an independent snapshot in the existing comparison window; it does not edit the batch or apply anything to source. Adjust patterns or match choices in Replace Files to change the plan.

**Previous match / Next match** and **Shift+F3 / F3** move through all preview occurrences and wrap across files, including unchecked occurrences. **Open source in editor** opens or focuses the selected source without closing Replace Files. Match offsets are used only if the complete live text still equals the expected snapshot; changed sources open without a stale jump. Offsets for skipped occurrences in applied files include the length changes of earlier replacements. After manual edits, use Preview to rebuild. Snapshots do not automatically reload on focus changes.

![Staged review and remaining batches](../media/replace-files-review.svg)

## Apply and confirmation

**Confirm each file before any changes** offers Replace / Skip / Cancel per file. Decisions are collected before buffer changes, so Cancel aborts the unmodified batch. **Confirm each replacement before any changes** additionally opens a modal review with file, line, column, bounded old/new snippets and the candidate highlighted behind it. **Replace occurrence / Skip replacement / Replace remaining / Cancel batch** stage choices without source edits. Replace remaining accepts the remaining enabled occurrences in this batch across files. All file confirmations run first. Cancel discards all staged occurrence choices and leaves the match checkboxes and buffers unchanged. Reviewed results are validated again, including size limits, before application. Confirmation options are local to the window.

All selected sources are preflighted: document identity, filename, complete text and read-only state. Closed files are reread, opened in the editor and their decoded buffers rechecked before mutation. A preflight mismatch aborts the batch; newly opened unchanged tabs may remain open.

Application is **one Undo action per file**, not one project-wide Undo or a filesystem transaction. Each buffer is checked again immediately before mutation. If an on-edit extension synchronously changes another source during application, the remainder stops and the applied count is reported; undo previously applied buffers individually. Successfully applied files stay marked **Applied — use editor Undo** and cannot be selected again in this plan, even after you undo them in the editor. **Select next 128 files** chooses up to 128 remaining eligible files without searching changed buffers again. Each new batch rechecks its sources. A fresh Preview deliberately creates a new plan from current contents; use it after changing criteria or editing a remaining source. Normal Save may trigger previously enabled editor actions such as remote upload; replacement itself does not run those actions.

Read-only sources are not unlocked. One UTF-8 BOM is stripped for comparison with editor text; normal Geany open/save retains encoding/BOM metadata. Closed inputs must be UTF-8; open other encodings in the editor first.

## Limits and validation

Up to 2,000 inputs; 1 MiB per source/result; 16 MiB source total, 32 MiB result total; 100,000 batch matches, 10,000 matches per file; at most 128 files per Apply. Masks: 4 KiB; Old/New: 8 KiB. Folder traversal: depth 32 and 10,000 entries. Limits reject the preview/application rather than silently applying a partial plan; narrow the scope/masks or select fewer files. Regex work/depth is bounded.

Native Linux tests exercise a real project, menu/key routing, masks, lexical regions, colored/numbered read-only previews, individual choices, stale/read-only guards, per-file Skip/Cancel, one Undo per file, unchanged disk, folder recursion, history and cancellation. Core tests cover Unicode/CRLF, captures, zero-width matches, regex bounds and exact comment membership. Windows and Android were not run.

Initial implementation validated on Linux: complete build and installation, **71/71 tests passed**, [run 39609587088](https://github.com/bhgv/geany-insight/actions/runs/39609587088), code `2ce4631551103cbed25147159c35c4df6a9d2b3a`. The capture above comes from this run.

Reference: [Source Insight Replace Files](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Replace_Files.htm). Automatic saving, forced read-only edits, Skip Inactive Code and Source Insight's exact legacy regex dialect are not implemented.
