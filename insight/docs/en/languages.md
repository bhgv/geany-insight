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
