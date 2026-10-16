Open the internal comparison window from **Tools → Diff / Merge…**. The Context panel also has a Diff / Merge button.

# Internal Diff / Merge

Open **Diff / Merge…** in the Context header. Choose Left and Right files and press
**Compare**. The nonmodal window displays both read-only sources, highlights changed
lines and lists changed blocks. Selecting a block scrolls all three panes to it.
The editable result initially contains Left. **Take Left** / **Take Right** replaces
only the selected block; either version can be chosen repeatedly. Empty blocks
represent insertions or deletions. Manual edits are allowed in the result.

**Undo** / **Redo**, Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y cover block replacements and
manual edits. A block replacement is a single history operation. Editing after
undo discards redo. Result marks move with edits and are restored with history.
History uses full snapshots, bounded to roughly 32 MiB (the newest snapshot is
always retained). **Save result…** writes to a chosen destination with overwrite
confirmation. Closing or starting another comparison asks before discarding a
changed result. Sources are never saved automatically.

**External KDiff3 (optional)** explicitly launches `kdiff3` from PATH with the two
currently selected files, even when internal comparison exceeds its limits. It
never launches automatically. Missing KDiff3 does not affect the internal tool.
External editing has its own history, outside this window.

Initial scope: two-way, line-based comparison and manual merge; UTF-8 text without
embedded NULs, up to 1 MiB per source. CRLF and final-newline differences are
preserved. The dependency-free LCS engine trims common prefixes/suffixes and caps
its matrix at 4 million cells. This is not yet three-way base-aware merge,
automatic conflict resolution, intraline highlighting, or padding that visually aligns both source panes line for line.
If a manual edit removes several block boundaries, compare again to rebuild the
block list; the list describes the original sources, not a new diff of the result.

Core validation: `python3 insight/tests/diff_test.py` (C99 compiler required).
Native GUI builds include the module in both Meson and Autotools. GTK development
headers are needed to compile and exercise the window.

The line engine recognizes CR, LF and CRLF while retaining their exact bytes. Nonadjacent changes in CR-only sources remain separate blocks. A pure insertion/deletion after trimming shared edges needs one block and no LCS matrix; two-sided comparisons retain the four-million-cell work limit. The UI source-size limit is unchanged.
## File Compare navigation and display

**Previous difference** / **Next difference** and **Ctrl+Up / Ctrl+Down** select
and reveal a block in all three panes. Navigation stops at either end; buttons
are disabled at the boundaries. With no selection, Next selects the first block
and Previous selects the last. Clicking a changed source block also selects it.
The status shows the selected difference number and total count.

**Difference list** hides or shows the block list; navigation keeps working while
it is hidden. **Wrap lines** applies word/character wrapping to all three panes.
These display options last for the lifetime of the comparison window.

**Scroll sources together** is enabled initially. It maps the top visible source
line through the diff, so insertions/deletions do not accumulate a line offset.
Inside a changed block it aligns to the start of the opposite block. Disable it
for independent scrolling. The editable Result scrolls independently; selecting
a difference still reveals its current marked range. This is position linking,
not blank-line padding or pixel-perfect alignment of wrapped lines.

Reference: [Source Insight File Compare](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/File_Compare.htm).
The Source Insight commands copy directly between files; this implementation
keeps Take Left/Right directed into the separate result buffer.

Validation for this increment: core suite (9 tests), 3000 randomized diff
reconstructions, bidirectional monotonic offset mapping, UTF-8 boundary checks,
insertions/deletions and the LCS work limit. The production GTK window also passes an isolated Linux/Xvfb fixture with
strict compiler warnings and fatal GLib criticals; this is not a full editor run. Manual check: compare the supplied example, navigate
both ends using buttons and shortcuts, hide the list, click each changed source,
scroll past insertions with linking on/off, toggle wrapping, then take/undo/redo.

## Comparison options

**Ignore spaces and tabs** ignores ASCII horizontal whitespace (space, tab,
vertical tab, form feed) within each logical line. It does not merge lines or
ignore Unicode spaces. **Ignore line ending style** treats CR, LF and CRLF as
the same terminator; a missing final terminator is still a difference. Press
**Compare** to apply changed options. They do not change the loaded comparison
until that command succeeds. The initial mode is exact.

Hunks retain original byte ranges. The result starts from Left, so ignored
changes outside a selected block keep Left's spelling/spacing/endings. Taking
Right copies the original Right bytes of that block. When mixing CR-only and
LF sources could fuse two lines into one CRLF, a neighbouring equal line is
included in the block. Overlapping blocks are combined. Linked source scrolling
uses logical lines, so differing whitespace byte widths do not shift navigation.

Validation adds 2000 random normalized reconstructions in all four option
combinations, monotonic line mapping and a GTK comparison of Cyrillic CRLF/LF
sources. No semantic or language-aware whitespace comparison is claimed.

## Cancellable comparison

File reads, UTF-8 validation and LCS comparison run in a GTask worker. Reads are
bounded to 1 MiB plus one detection byte per regular source file. **Cancel
comparison** keeps the previous sources, result and undo history. A failed read,
invalid UTF-8 or exceeded work limit also leaves them intact. While comparison is
pending, source choices, comparison options and result editing are disabled.
Closing the window cancels the worker; a late completion never accesses destroyed
widgets. Cancellation is checked between phases and LCS rows, not within a
single line comparison. Publishing source text, GTK tags and list rows still
runs on the UI thread; this is not a guarantee of zero UI latency.

The isolated production-widget fixture covers cancellation, rejected directory
input and destroying a window with a pending comparison. Core tests cancel at
several LCS phases and verify that no partial diff escapes.

## Find text and line numbers

The search row selects **Left**, **Right** or **Result**. **Ctrl+F** targets the
focused source/result pane and focuses the query. Enter or **F3** finds the next
literal Unicode match; **Shift+F3** or **Find previous** searches backward.
**Match case** controls case sensitivity. Search wraps at a boundary and reports
that fact, or reports no match. Escape in the query returns focus to the selected
pane. Search changes selection, not source text, result contents or merge history.
The difference list selection continues to identify the block used by Take.

**Line numbers** toggles gutters in all three panes. Numbers follow logical lines,
including CR, LF, CRLF and wrapped lines, and update after result edits. They are
not inserted into text or copied selections. Display settings are independent per window and remembered as defaults.

![Production Diff/Merge window in an isolated GTK fixture](../media/file-compare.jpg)

The JPEG shows the real production window on Linux/Xvfb with synthetic test files;
it is an isolated fixture, not a screenshot of the complete editor. The fixture
also covers literal search, case sensitivity, wraparound, focus and gutter
visibility. A near-four-million-cell comparison lets the main-loop timer run;
this checks that calculation moved off the UI thread, not every possible latency.

![Sources, selected block and independent result](../media/file-compare-flow.svg)

GTK fixture: Meson test **Insight isolated Diff Merge GUI** (requires a display).
For standalone checking, compile `insight/tests/diff_gui_test.c` together with
`insight/core/diff.c` using GTK3 flags, then run under Xvfb. It uses production
widgets with stubbed host help/translation functions. Windows/Android were not
executed in this increment.

The Linux fixture additionally checks linked scrolling past a 20-line insertion,
independent scrolling after disabling the link, invalid UTF-8 and a source one
byte above the size limit. The same fixture passes UBSan with fatal GLib
criticals. ASan reached the checks but its leak-scanning runtime could not finish
because `/proc/task` is unavailable in this environment; a clean ASan result is
not claimed. Linux CI now includes both the core and production-widget fixture.

Gutters include the final empty editor line after a terminator. Saving is disabled
while a new comparison is pending, so a save dialog cannot overlap replacement
of the result. The GTK fixture verifies save-control sensitivity.

[Directory Compare — compare directory trees and open a selected pair](directory-compare.md).

## Take all blocks

**All Left / All Right** replaces every difference block in the separate result
as one Undo action. Text manually edited outside the blocks is preserved. Taking
an already matching side does not add history. Undo/Redo restores block marks
with the text, and editing after Undo replaces the redo branch.

**Alt+Left / Alt+Right** takes the selected Left/Right block. Add Shift to take
all blocks. These shortcuts act inside the comparison window and write only
the result buffer. If manual edits made block boundaries touch or overlap, batch
replacement refuses the operation and asks for a fresh comparison. No partial
batch is applied. The production GTK fixture covers single-step undo, no-op,
manual outside text, collapsed boundaries, shortcuts and 150 randomized exact
batch reconstructions, including empty, UTF-8 and mixed-EOL sources.

Repeated-comparison regression: list-selection signals are ignored while source
text, marks and list rows are being replaced. This prevents old selection IDs
from reading the new mark array before it is populated. The randomized batch
fixture exercises 150 successive comparisons in the same production window.

## Display options and scaling

**Options… / Ctrl+Q** opens a single File Compare display dialog. Apply commits the controls together; Cancel preserves the current view. **Mono Font View**, **Vertical scrollbars** and **Scale (%)** affect all three panes. Scale is bounded to 50–250% of a 10-point base; Ctrl+mouse wheel changes it by ten points. Font changes also update the line-number gutters. Display changes preserve source bytes, the merge result and Undo history.

**Overview** adds a narrow map beside each read-only source. Orange/green markers show its difference blocks and the selected block is blue. The outline indicates the viewport; with wrapped lines its physical proportions are approximate. Clicking a marker selects the block in the shared difference list; clicking elsewhere scrolls that source to the corresponding logical line. Linked scrolling remains controlled by Scroll sources together. Marker caches are built in one forward pass over each source, including CR/LF/CRLF and zero-length insertions.

## Source context commands

Right-click either source for **Edit File — Ctrl+E** and **Copy selected difference**, plus the display toggles and Options. Edit File opens the completed comparison’s source in the editor at its caret line, reusing an existing document; editing the path fields does not reroute it. The live file can have changed since comparison. Clipboard copy uses the selected block’s original source bytes, including line endings, and leaves the result/history untouched. The editable result retains the normal GTK text menu. Source filenames are converted between UTF-8 entry text and native filenames and captured as absolute paths when Compare starts.

Display defaults (mono font, numbers, list, wrap, linked scrolling, scrollbars, Overview and scale), text-comparison options and window size are remembered in the workbench configuration. New windows use the last closed defaults; at editor exit the most recently opened live window supplies them. Existing windows stay independent. Invalid values retain the standard defaults. Restoring these settings does not load sources, run a comparison or restore an unsaved result; filenames and merge history are not saved. A directory pair’s explicit comparison flags override saved text-comparison defaults.

![File Compare display options](../media/file-compare-options.jpg)

This dialog is captured from production GTK widgets in the isolated Linux fixture, with editor-host help and translation stubs. The native editor test separately verifies Ctrl+Q, applying scale/Overview, Ctrl+E source-line navigation and workbench persistence. Syntax formatting and code nesting guides remain outside this GTK comparison-view implementation.

### Search in comparison panes

Ctrl+F targets the focused pane and seeds the query from a single-line selection of up to 4096 characters. **Whole words only** uses identifier boundaries: Unicode letters, digits, combining marks and underscores belong to words. **Wrap around** allows one retry from the opposite end; disable it to stop at the file boundary. F3 and Shift+F3 search forward and backward. Search options apply to the current window and never modify result text or merge history. Regular expressions are not supported.

![Production comparison widget search, isolated GTK fixture](../media/file-compare.jpg)

### Go to Line and pane switching

Ctrl+G targets the focused pane and seeds its current caret line. Enter a logical line number and press Enter or **Go**. Numbers beyond EOF land on the last line; an empty buffer has line 1. **Select line text** selects content without its line ending. **First line** and **Last line** use the selected pane. Escape returns focus from the number field to that pane. Go to Line is also available in the text context menu.

F6 cycles Left → Right → Result; Shift+F6 reverses the order. Search keeps its independent pane choice. Navigation preserves text, selected difference and merge history; Go to Line is blocked while comparison runs. Logical lines do not change when long lines wrap visually. The production-widget capture above includes the navigation bar.

### Replace in the merge result

Ctrl+H targets search at Result and focuses the replacement field. Enter the old literal text in the search bar above and the new text in **Replace in Result**; an empty replacement deletes matches. Match case and whole-word options apply. **Result selection only** restricts edits to the current result selection; **One occurrence per line** uses the first match on each logical line. Otherwise the entire result is processed, independent of caret position or search wrap.

**Preview count** recomputes actual changes without editing. **Replace all in Result** recomputes against current text and settings, then applies the batch as one Undo/Redo action. Identical text is skipped. A preflight limit of 100000 changes and 1 MiB result rejects the whole batch before edits if exceeded during calculation. Replacement is blocked during comparison. This is literal replacement; regex and source-file replacement are not implemented here. Search and replacement strings are not saved in preferences. The production GTK-widget capture above shows the replacement bar.

### Preserving and converting case

**Preserve old case** computes replacement text separately for every match. Unicode cased letters determine the style; digits, punctuation and underscores do not. Mixed-case matches use the literal replacement. Initial-capital style lowercases the replacement and uppercases its first cased letter. Preview and size checks use actual converted strings, including expansions such as `ß → SS`. This option belongs to the current window and is not persisted.

| Old match | Typed replacement | Result |
|---|---|---|
| `foo` | `bAr` | `bar` |
| `FOO` | `bAr` | `BAR` |
| `Foo` | `bAr` | `Bar` |
| `fooBar` | `bAr` | `bAr` |
| `ЁЖ` | `straße` | `STRASSE` |

The Result context menu provides **Uppercase selection**, **Lowercase selection** and **Toggle selection case**. They preserve line endings and select the converted text. Unicode conversion can change character count. Each operation is one Undo action; empty selections, unchanged text and comparison work produce no edits. Conversion is rejected in full if the result would exceed 1 MiB. Source-pane menus omit these editing commands.

### Result line editing

**Result menu** exposes the result commands, also available in its text context menu. Line commands affect selected logical lines, or the caret line without a selection. A selection ending at the start of the next line excludes that line. Indent skips empty lines and lines beginning with `#`.

| Command | Behavior |
|---|---|
| Indent result lines | Inserts one tab-width of spaces |
| Outdent result lines | Removes a leading tab or up to one tab-width of spaces |
| Tabs to spaces in result lines | Expands tabs to the next tab-stop boundary |
| Trim trailing spaces in result lines | Removes trailing ASCII spaces and tabs |

**Edit tab width** controls these operations: 1–16, default 4. Tab expansion counts each Unicode character as one logical column; this is not glyph-width measurement or a display tab setting. CR, LF and CRLF are preserved. Each operation is one Undo/Redo action; unchanged text creates no history. Preflight limits of 100000 individual changes and 1 MiB result reject the whole batch before editing if exceeded. Commands are blocked during comparison. Width belongs only to the current window.

### Result history

**Result History…** lists retained result snapshots. Selecting a row shows its full text in a read-only preview. The table includes UTF-8 byte count, a short excerpt and Current/Saved markers. Saved means the text equals the last saved result, not that every snapshot has a separate file. Before the first Save result, it marks the initial left-side result baseline. Cancel leaves the result unchanged. **Restore selected state** restores text and merge-block positions while keeping later states available through Redo. A new edit after going back discards the redo branch. If history changes while the dialog is open, its stale selection is not applied.

Result menu offers **Undo All retained changes** and **Redo All retained changes**, jumping to the earliest and latest available states. History is capped at 512 states and approximately 32 MiB of snapshots, retaining at least one state even if a single snapshot is larger. After eviction, the earliest available state can differ from the original result. A successful new comparison resets history; closing the window removes it. History is not saved in preferences. Saving result text retains available history. History browsing and undo are blocked during comparison.

![Production GTK history dialog, isolated test fixture](../media/file-compare-history.jpg)

### Saving a copy or the result selection

Result menu provides **Save a copy of Result…** and **Save Result selection…**. The former writes the entire current result; the latter writes its exact selected text without adding a newline or BOM. Output is UTF-8 and preserves CR, LF and CRLF. A successfully exported selection opens in the main editor when source navigation is available. A copy does not open automatically.

| Command | Updates result saved-text marker | Changes history |
|---|---|---|
| Save result… | Yes | No |
| Save a copy of Result… | No | No |
| Save Result selection… | No | No |

Text is captured before the file chooser. If the result changes, a comparison starts or the parent window closes while choosing a destination, nothing is written. Cancel creates no file. GTK confirms overwrite of an existing destination. This exports to a file; appending to an open buffer is not implemented. Selection export requires selected result text. All three commands are blocked during comparison.

![Production GTK copy-save dialog, isolated test fixture](../media/file-compare-export.jpg)

### Visible whitespace and line endings

**Visible tabs and spaces** draws ASCII spaces as `·` and tabs as `→`. **Visible line endings** draws `¶` for CR, LF or a CRLF pair, with one marker per pair. These are overlays in all three panes. Marker glyphs do not enter searches, selections, clipboard text, exports or saved files and create no merge history.

Both toggles are available in the toolbar, Options and context menus. Apply commits changes; Cancel keeps the previous settings. Values persist with comparison-window preferences. Drawing follows zoom and wrapping. Work is capped at 20000 characters from the visible starting position per draw; pathological long lines can omit some markers. This does not affect text or comparison results. The main-window capture above shows enabled markers.

### Identifier search and match lists

Every pane's context menu provides **Load Search String**, forward/backward identifier search and **Find all matches in pane**. Loading takes the identifier under the caret or the first identifier in the selection, targets that search pane and preserves the caret. Identifiers use Unicode letters/digits, combining marks and underscore, without language parsing. Words requiring more than 4096 characters of traversal are not loaded.

Ctrl+F3 searches for the next identifier occurrence; Ctrl+Shift+F3 searches backward. Both enable **Whole words only** and honor current case/wrap options. **Find all…** searches the entire pane chosen in the search bar. Ctrl+Shift+F uses the focused pane or the search-bar pane when focus is in controls. This is local comparison-pane search.

The list contains up to 5000 matches with one-based logical lines/columns and text excerpts, indicating when capped. **Go to match** or double-click selects the exact range; Close changes nothing. Text edits or a new comparison invalidate old positions, so stale navigation is rejected. Queries, lists and positions are not persisted. Commands do not edit text or merge history and are unavailable while comparison runs.

![Production GTK match list, isolated test fixture](../media/file-compare-search.jpg)
