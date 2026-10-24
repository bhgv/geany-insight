# Language profiles and file bindings

Open **Options → Language profiles…** to edit profiles. Editor right click → **File language / profile…** opens the same dialog with forced binding selected for the current file.

Choose a profile and edit its language name, base Geany parser/lexer, own extensions and compatible extensions. To create a custom profile, select a suitable starting profile and enter a new name. The base parser controls syntax and definition extraction: renaming does not create a compiler or parser.

Separate extensions with semicolons: `c;c++;C;cpp;cxx;h;hpp;hxx;h++`. Matching is case-sensitive on all platforms; specify `s` and `S` separately. Compatible extensions add search inputs without changing their parsers. Default C/C++ compatibility includes C/C++ and ASM/S. Configured profiles restrict definitions, links and text searches to their own/compatible extensions and files forced to the same profile. Other files remain project members but are excluded from these searches.

**Force this profile…** overrides extension and shebang for one file. Named-file bindings persist across launches. Unnamed-buffer bindings last for the session and are recorded for their path after saving. **Reset file binding** restores automatic detection. Bindings belong to paths; reassign after externally renaming a file.

## Colors, fonts and words

**Predefined words** overrides the primary keyword set, separated by spaces. Empty retains the base parser's words. This influences highlighting and excludes keywords from Context links; it does not add index definitions.

**Fonts and CSS** selects current Geany styling or a light/dark starting preset. Choosing a preset replaces the dialog's CSS; editor changes happen only on **Apply**. Choose a category, font, size, Bold/Italic and color, then **Add CSS rule**. Appended rules override earlier declarations. `editor` sets the main font; `.keyword` addresses keywords and `.comment` comments. Semantic categories map to named base-lexer styles; `.style-N` addresses a Scintilla style number for any lexer.

```css
editor { font-family: "Monospace"; font-size: 12pt; }
.keyword { font-weight: bold; font-size: 14pt; color: #2456a8; }
.comment { font-style: italic; color: #68756b; }
.string { color: #8e421c; }
```

Supported selectors: `editor`, `.keyword`, `.comment`, `.string`, `.number`, `.operator`, `.type`, `.preprocessor`, `.style-0` … `.style-255`. Supported properties: `font-family`, `font-size`, `font-weight`, `font-style`, `color`, `background-color`. Specify size in `pt`, from 4 to 96. This is a Scintilla CSS subset without nested selectors or web animation. Apply rejects unsupported selectors/properties.

## Settings files

Profiles occupy `languages/<language name>/` under Geany's configuration directory (`config/languages/` in a portable configuration). `language.conf` stores the name, parser, extensions, words, preset and bound paths; `style.css` stores appearance. Renaming the profile renames its folder; unsafe path characters are rejected.

At first launch, absent standard profiles are populated from current Geany extensions, keywords, colors and emphasis, plus the editor's main font. C/C++/ASM receive initial compatible extensions. Existing language.conf/CSS files are retained. User profiles take precedence over seeded defaults; conflicting own extensions between two user profiles require correction.

Edit these files manually if desired. **Reload profiles** rereads them. **Apply** saves the dialog, reloads profiles, updates open documents and requests workspace reindexing. Invalid parsers/CSS exclude a profile from loading. Profile changes do not modify or save source text.

Display overrides are separate from profile CSS: [Syntax Formatting](syntax-formatting.md) describes Mono Font View, color-only, grayscale and plain text modes.

## Filename filters

File filters supplements legacy extensions with literal basenames (`Buildfile`,
`cmd.inc`) and masks (`*.inc`, `Т?ст.h`). Separate entries with spaces, commas or
semicolons. Paths, drive prefixes and quotes are rejected. Limits: 4096 UTF-8
bytes, 256 filters of 255 bytes each. `?` matches one Unicode character. Windows
ignores case; other hosts preserve it. Empty filters retain existing extensions.

Explicit path bindings win, then exact profile/native filenames, then profile
globs/extensions; bare `*` is a last resort after native types. Within a tier,
profiles retain deterministic custom-first/name order. For an exception to a
broad mask, assign the literal filename to the desired profile. Reloading clears
the recognition cache. Filters also enter language eligibility snapshots and
known MFL types. Invalid input is rejected before writing; legacy profiles
without `file-filters` keep extension-based matching. Include when adding to
project and remaining File Type Options are separate work.
Reference: https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/File_Type_Options.htm


## File Type Options: editing

Open **Options → File Type Options…** or the editor context menu. The current file's resolved profile is selected. The **Editing options** tab has an explicit override switch; existing profiles inherit their document settings until it is enabled. File binding remains a separate choice.

![Production editing options page](../media/filetype-editing.jpg)

Actual production GTK page captured in its isolated GUI fixture, using English sample settings.

![Independent indentation and two wrap modes](../media/filetype-editing.svg)

Set spaces, tabs or mixed indentation, tab width and indent step (1–32 columns), one of four auto-indent modes, screen wrapping, typing wrapping, line numbers, whitespace, line endings, indentation guides and the right margin (20–500 columns). In tabs-only mode the indent step follows the tab width; mixed mode keeps them independent.

Screen wrapping only changes display. Typing wrapping uses the margin column and inserts real newlines into subsequently typed text. Apply does not reformat existing text, move the caret or clear Undo. Overrides affect matching open and newly opened documents. Disabling restores the per-document indentation, wrapping and display settings captured before the override; this baseline is session-local. New global settings can be applied after disabling the override.

The options are stored in the profile's `language.conf`, in versioned `[editing]` settings. Missing settings keep inheritance; invalid settings disable the entire override while retaining the parser/CSS profile. Close without Apply discards dialog edits. Existing Apply, profile reload and file binding commands still work. Printing and Include when adding to project remain separate work; typing permissions are described below.

### Typing permissions per file type

The Editing page also controls automatic symbol completion, code snippet expansion, automatic closing brackets/quotes (including XML tags), and Enter behavior. These settings share the explicit editing override switch. Old profiles default to allowing all four behaviors.

Allowing completion or automatic closing does not turn on a globally disabled feature. Disallowing automatic completion leaves the explicit completion command available. The snippet permission controls Geany snippet expansion, snippet accelerator keys, and project template name completion; explicit insertion from the Clips panel stays available. Applying a restrictive profile closes an already visible completion popup.

When **Enter inserts a new line** is off, plain Return/keypad Enter moves to the start of the next line without changing text or Undo. On the last line it moves to that line's start. Modified Enter, multiple selections, and acceptance of an active completion popup retain their normal behavior. This setting does not change Paste or explicit newline commands.

![Typing permissions and inheritance](../media/filetype-typing.svg)

Intelligent paste, printing, and Include when adding are separate remaining work. Existing bracket editing is reused; this does not claim every Source Insight paired-character shortcut.

### Editing generated bracket and quote pairs

With file type editing overrides and **Allow automatic closing brackets and quotes** enabled, typing the closing character at a generated `)`, `]`, `}`, single quote or double quote advances over it instead of inserting a duplicate. Backspace in an empty generated pair removes both characters in one Undo action. Typing `(`, `[` or `{` immediately before an existing matched block with the same opening character surrounds that block with a new pair, respecting the global automatic-closing permission.

![Tracked pair editing](../media/typing-pairs.svg)

Nested pairs and UTF-8 edits before/inside them are tracked by byte position. Tracking is transient, limited to 128 pairs per editor, and cleared by Undo/Redo or disabling the editing override/typing permission. Manual pairs are ordinary text. Read-only documents, selections, multiple carets, modified editing keys and active completion popups retain their normal behavior. Generated quotes are tracked, but surrounding pre-existing quoted strings and every Source Insight shortcut are not claimed. No XML-tag pair deletion is added.

### Detailed completion window

**Options → Detailed completion window** enables the detailed symbol popup (on by default). It uses the existing symbol-completion threshold and candidate search: set the threshold to 3 or 4 characters in Geany's completion preferences. Names, return/variable types, function parameter lists and scope are shown in colored text, with symbol icons and a kind label. The selected row uses theme selection colors; its declaration location is shown below and in a tooltip. Missing parser metadata is left blank.

![Production completion view captured in an isolated GTK fixture](../media/completion-list.jpg)

Keep typing to narrow the list; Backspace refreshes candidates and can broaden it again. Up/Down and PageUp/PageDown select rows, Enter/Tab insert the selected **name only**, and Escape closes without editing. Hover selects a row; a left click accepts it. The editor keeps keyboard focus. The popup appears beside the caret, moves above it near the bottom of the monitor, and closes on navigation, scrolling, focus loss, Undo/Redo or stale selection. Acceptance is one Undo action and observes the existing drop-rest-of-word setting.

The switch is saved in `insight-completion.conf`; turning it off restores the classic list. File type permission and the global automatic-completion setting continue to gate automatic display; the explicit completion command remains available. Document-word, plugin-provided and snippet lists keep their existing interfaces. Candidate accuracy still depends on the existing parser; automatic insertion of function parameters and full Source Insight completion parity are separate work.

### Completion options dialog

Open **Options → Completion options…**. Cancel discards unapplied edits, Apply saves while leaving the dialog open, and OK saves and closes. The settings survive restart and the detailed-list menu switch updates the same file without erasing the other options.

![Production completion preferences page in an isolated GTK fixture](../media/completion-options.jpg)

| Setting | Default | Effect |
| --- | --- | --- |
| Detailed window | On | Rich symbol rows; off restores the classic list |
| Tab accepts | On | Off returns Tab to its normal editor behavior; Enter still accepts |
| Inside comments | Off | Allows automatic suggestions in comments, while global/per-file permissions still apply; strings remain excluded |
| Insert parentheses | Off | Adds `()` for function/method/prototype candidates only |
| Space after name | Off | Uses `name ()` when parentheses are added |
| Show after characters | 0 | 0 inherits Geany; 1–12 overrides ordinary-prefix threshold |
| Visible rows | 9 | 3–20 rows before scrolling |

![Function insertion and existing arguments](../media/completion-insertion.svg)

Parentheses are inserted only if no opening parenthesis follows the accepted name after spaces/tabs. Existing arguments and spacing are preserved. The caret is placed inside a new pair; acceptance is one Undo action. No arguments or types are synthesized from a declaration. Insertion into the middle of a word with its suffix retained suppresses new parentheses; uncertain long whitespace tails also suppress them. Tab acceptance and parentheses settings apply to the detailed symbol list; the threshold and comment permission also affect the classic symbol path. Member completion retains its existing context-specific triggers.

Older `insight-completion.conf` files containing only `detailed` retain their behavior. Missing keys use defaults; malformed values reject the settings group and fall back to defaults. Unknown valid keys/groups are preserved when saving. A pending refresh is resolved before keyboard acceptance, so fast Backspace followed by Enter uses the updated prefix.

### Case-sensitive symbol matching

**Completion options → Case-sensitive symbol matching** is on by default, including when loading an older settings file. Turn it off to find symbols whose names start with the typed prefix regardless of case. This applies to the ordinary symbol query, member completion, the detailed list and the classic symbol list. Exact-case prefix matches are retained and preferred for initial selection; acceptance inserts the original identifier spelling. Further typing preserves the selected candidate when it remains appropriate to the typed case.

![Case matching and preferred selection](../media/completion-case.svg)

The matcher supports Unicode casefold (for example, `STRASS` can match `Straße`); it does not normalize different Unicode representations. Prefixes are limited to 512 UTF-8 bytes and candidate identifiers to 4096 bytes. Typed-word extraction and supported language metadata retain their existing language-specific behavior.

Case-insensitive expansion examines at most 100000 tag entries, including repeated visibility passes. Existing exact-case candidates are kept first, and the normal maximum-entry setting still limits the result count. A partial expansion is marked **Case-insensitive search limit reached** in the detailed footer. Current-file locals, scope/line visibility, included files, workspace/global sources, language compatibility and name deduplication retain the existing lookup rules. The scan budget can omit additional matches in very large indexes. File name filters keep their own case rules. The choice is saved with the other completion settings and survives restart.

### Intelligent whole-line paste

Enable **Options → Intelligent paste**. It defaults to off and persists in `insight-paste.conf`. An active File Type Options → Editing profile can disable it with **Allow intelligent paste**; older profiles allow it, and a disabled editing override inherits the global behavior.

Ordinary Paste (menu or Ctrl+V) and Clips treat text ending in a newline as whole lines. A caret inside a line inserts the block above it; a caret at the end of a nonempty line inserts below it. A final line without an EOL receives a separating EOL. Complete line selections are replaced; partial selections keep native replacement behavior.

The minimum shared indentation of nonblank lines is removed and the destination line indentation is added. Relative nesting remains, tabs are measured with the document tab width, and output uses destination tabs/spaces and LF/CRLF/CR. Blank lines have no added whitespace. This is text indentation handling, not syntax-aware reformatting. The complete insert is one Undo/Redo action.

![Whole-line placement and relative indentation](../media/intelligent-paste.svg)

Rectangular or multiple selections, virtual positions, partial text, invalid UTF-8 and bounded-work rejection keep ordinary paste. Transformation limits: 1 MiB input, 4 MiB output, 10000 lines, 4096 indentation columns, tab width 1–32. Pending clipboard reads validate the document, text generation, selection and caret; a changed destination cancels that pending paste. Read-only remains protected. Drag/drop, Drag Line Up/Down, a separate Paste Line command and post-paste selection options are not ported in this series. Dialog text fields retain their normal behavior.

### Paste Line and line movement

**Edit → Line operations** adds **Paste Line**, **Drag Line Up/Down**, and **Drag Line Up/Down More**. Paste Line clears a normal selection, places the caret at the beginning of its current line, and runs Paste. Partial text also inserts at line start; whole lines adopt the destination indentation when intelligent paste is enabled. The same pending-clipboard destination guards apply.

Drag Line Up/Down move the current line or all lines touched by a normal selection. A selection ending at the next line start excludes that next line. **More** moves up to five lines and stops at the document boundary. With Options → Intelligent paste enabled and allowed by the file type, the block adopts the indentation of the last crossed line while retaining relative nesting. Without that permission, native movement keeps indentation. The existing bounded indentation rules apply; rejected transformations fall back to native movement.

![Line commands and destination indentation](../media/line-operations.svg)

Existing **Move line(s) up/down** and their configured keys (Alt+Page Up/Down by default) also use this mode. This series assigns no separate default keys to More or Paste Line. Each move, including five steps and reindentation, is one Undo/Redo action. In intelligent mode, selected blocks stay selected with their original direction; an unselected caret follows the line text, snapping to text start if originally inside changed indentation. Read-only is protected; new menu commands require a single normal selection without virtual space. Existing movement commands retain native handling for other selection modes.

These are line movement commands. Mouse drag/drop remains a separate open item; intelligent post-paste selection is described below. Indentation handling is not syntax-aware reformatting.

### Clipboard: whole lines and line fragments

**Edit → Line operations** adds these Source Insight commands:

| Command | Behavior |
| --- | --- |
| Copy Line | Expands the selection to all touched whole lines and copies it. Repeating with an already whole-line selection adds the next line. |
| Cut Line | Cuts the active caret's current line, including its EOL when present. Even a multiline selection cuts only that current line. |
| Copy Line Right | Copies from the caret to the end of the line text, retaining the current selection. |
| Cut Line Left / Right | Cuts from line start to the caret / from the caret to line end. The line ending stays. |
| Cut Selection or Paste | Cuts an extended selection; with only a caret, runs Paste. Whole lines use intelligent paste when enabled. |

![Copy and cut ranges](../media/line-clipboard.svg)

Existing **Copy/Cut Current Line(s)** and their configured keys (normally Ctrl+Shift+C / Ctrl+Shift+X) use these Copy Line/Cut Line behaviors in source editors. Copy Line expansion is based on the current full-line selection, so a manually selected whole-line block also grows on invocation. When first expanding a partial selection, an end at the next line start excludes that next line. Growth stops at EOF. A final line without EOL is copied without adding one.

Each cut is one Undo/Redo action; Copy Line and Copy Line Right do not edit text or create Undo entries. Copy works in read-only; Cut/Paste do not. Empty fragments and a final empty line preserve the previous clipboard. Ranges are limited to 1 MiB and require complete UTF-8 without NUL bytes; rejection preserves text and clipboard. New menu commands require one normal selection without virtual space. Existing Geany commands retain native handling for other selection modes and non-document Scintilla widgets. Line fragment commands use the active caret even with an extended selection. No separate keys are assigned to the new fragment commands or Cut Selection or Paste in this series.

Clipboard line endings may be normalized by GTK/platform text transfer; this does not convert line endings in the source document.

![Line operations menu captured in the running Linux editor](../media/line-operations.jpg)

### Selection and navigation, Extend Mode

**Edit → Selection and navigation** adds **Toggle Extend Mode**, **Beginning/End of Selection**, **Blank Line Up/Down**, **Select Word**, **Select Line**, selections to line end / file start / file end, and Top/Bottom of File.

With Extend Mode on, native Scintilla movement (arrows, words, Home/End, Page Up/Down) extends from the existing anchor. Geany navigation through `sci_goto_line`, `sci_goto_pos` and scrolling `sci_set_current_position` also retains the anchor. New blank-line and file-edge jumps honor the mode. Without it, jumps place a normal caret. **Escape** exits mode while retaining selected text; a visible completion popup gets first handling. Beginning/End of Selection collapse an extended selection to its edge and exit the mode; with only a caret they do nothing.

![Anchor and extended selection](../media/selection-navigation.svg)

Select Word uses the editor's current word rules. Select Line selects the current line with its EOL when present. Explicit Select To… commands retain the current anchor. Blank means empty or containing only spaces/tabs; search starts on the previous/next line and does not jump to a file edge when there is no match. A final empty line after EOL is included. Searches stop at 100000 lines or 4 MiB displacement without moving the caret.

Mode belongs to each document's Scintilla widget and is not persisted. The menu check follows document switches. Text edits and mouse clicks inside the editor end the mode; state is restored when a document is activated. External plugins sending raw `SCI_GOTOPOS/SCI_GOTOLINE`, and non-scrolling `sci_set_current_position`, retain their previous semantics; universal API interception is not claimed. No additional default keys are assigned to these commands; existing movement keys remain configured normally.

Commands work in read-only, never edit source text or create Undo entries, and require a single normal selection without virtual space. Full Source Insight Select Block/Match, symbolic ranges, Extend Mode across every external/plugin jump, and mouse selection shortcuts remain separate porting items.

![Selection and navigation menu in the running Linux editor](../media/selection-navigation.jpg)

### Selection after intelligent paste

**Options → After intelligent paste** offers three persistent choices: **Cursor at end of pasted text** (default), **Cursor at start of pasted text**, and **Select pasted text**. The choice applies to successful intelligent whole-line insertion through ordinary Paste, Paste Line and Clips. A selected block can immediately be copied, moved or replaced.

![Three post-paste positions](../media/paste-position.svg)

Positions use the transformed block, including its destination indentation and EOLs. At an unterminated EOF, the separator added to the previous line is excluded from the selected block. Each insertion remains one Undo action. The setting is saved in `insight-paste.conf`; missing or out-of-range values use the default. Failed saves restore the previous menu choice and report an error.

This series covers intelligent whole-line insertion only. Partial/native fallback, multiple/rectangular selections, file types forbidding intelligent paste and mouse drag/drop keep their existing behavior. A separate always-select override and general selection policy for native partial-text paste remain open Source Insight porting items.

## Live interface captions

Options → Interface language switches the active English/Russian table and refreshes
open windows, notebook tabs, detached menus, combo options, formatted counters,
Pango labels/tooltips and enum/status cells. Stable keys retain the original message;
paths, source text, editable input, model values and combo IDs remain data.

`insight/locale/insight.tsv` is the single editable Insight caption table. Native
Geany captions are generated from `po/ru.po` into `native.tsv`; both feed the same
runtime registry. The old separate `insight.pairs` source is retired without losing
its captions. `python3 insight/tools/build_i18n.py --check` checks catalog consistency;
without `--check` it updates only the native catalog.

For local changes create `insight/interface-texts.tsv` in the Geany configuration
directory. Each UTF-8 row contains a stable English key, a literal TAB, and its
Russian caption; three fields allow separate English and Russian captions:

```text
Before<TAB>Earlier<TAB>Ранее
After<TAB>Later<TAB>Позже
```

Replace `<TAB>` with actual tab characters. `\n`, `\t` and `\\` represent a newline,
tab and backslash inside a field. Preserve printf argument types and `{value}`
placeholders. Choose **Options → Interface language → Reload interface texts** to
reload captions and refresh existing UI without restarting. Invalid UTF-8, malformed
rows or incompatible printf slots leave the previous catalog active. Remove the
file and reload to restore distributed captions. Existing gettext language handling
is retained for future native dialogs. Editor text and AI/terminal contents are not
translated.

![Shared caption registry / Общий реестр надписей](../media/interface-texts.svg)
