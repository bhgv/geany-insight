# Search and replacement: modes and query languages

Choose the surface first: the same punctuation can have different meanings in a
text search, a symbol filter and a filename mask. Context is read-only; replacement
is performed in the editor. Save or review changed buffers before closing them.

## Where to search

| Surface | Scope and commands | Query language |
| --- | --- | --- |
| Find (`Ctrl+F`) | Current editor; Next/Previous; Find All in document/session; Mark | Literal text or GLib regular expression |
| Replace (`Ctrl+H`) | Replace one; Replace All in document, selection or open session | Literal text or regex with capture references in replacement |
| Quick find in file | Current editor, Previous/Next | Literal; `~subsequence`; Regex |
| Context magnifier (`Ctrl+F` while Context has focus) | Preview only; Previous/Next, `F3` / `Shift+F3` | Literal; `~subsequence`; Regex; no replacement |
| Project/folder/selected-file search | Project files, open documents, current document, chosen files/folder; masks restrict paths | Literal; fuzzy; Regex; or Keywords/Boolean |
| Find Usage / Relations | Indexed symbols or lexical occurrences/calls; Code only masks comments/strings in supported languages | Symbol name, with language/owner restrictions; not a rename operation |
| Symbol/file list filters | Filter rows without editing source | Lookup syntax described below |
| Help viewer (`Ctrl+F`) | Current help page, `F3` / `Shift+F3` | Text search; not editor regex/replacement |
| Legacy Geany Find in Files, if invoked | Disk files through configured grep | Literal `grep -F` or extended regex `grep -E`, not GLib/PCRE |

Case means case-sensitive matching. Word restricts matching to complete words;
Word start in native Find/Replace restricts the beginning of a word. Direction,
wrap-around and Close dialog options control navigation. Native regex searches
run forward; backwards regex search is unsupported. Compact Context search
supports both directions and reports wrap-around. A zero-width regex match
advances on repetition instead of selecting the same position forever.

Find Next/Previous Selection uses the selected text, or the configured current
word/last-search fallback. Find All lists matching lines; Mark adds temporary
highlights. Native replacement affects buffers and supports Undo; Replace All in
Session can change several open buffers. Review every changed file before Save.
Project search and Context do not replace files on disk.

## Literal, escape and fuzzy modes

With Regex off, `a.b` finds a literal dot. Native Find/Replace's separate **Use
escape sequences** option decodes `\t`, `\n`, `\r`, `\\` and `\uXXXX`; with it
off these are ordinary text. Regex implies escape handling in those dialogs.
The compact searches have no separate escape-decoding option: turn on Regex to
search `\t` or `\n` as characters.

Only the compact file/Context/project text searches interpret a leading `~` with
Regex off. `~gctx` finds an ordered Unicode subsequence such as `get_context`, on
one line; intervening characters are allowed. This is not edit-distance/spelling
correction. Enable Regex and use `~gctx` to look for a literal leading tilde.

## Regular expressions (Regex)

Native Find/Replace and compact Insight text searches use GLib's Perl-compatible
regular expressions. Enter patterns directly in the field: do not surround them
with `/.../`, and do not double every backslash as if writing a C string.

| Pattern | Meaning / example |
| --- | --- |
| `.` | One character except newline by default; `a.b` matches `a_b` |
| `\.` / `\\` | Literal dot / backslash |
| `[abc]`, `[a-z]`, `[^0-9]` | Character set, range, negated set |
| `\d`, `\w`, `\s` | Digit, word character, whitespace; capitals `\D`, `\W`, `\S` negate |
| `\p{L}`, `\p{N}` | Unicode letters / numbers |
| `^`, `$` | Line start / end; anchors do not consume text |
| `\bname\b` | Word boundaries around `name` |
| `x*`, `x+`, `x?` | Zero or more, one or more, optional |
| `x{2,4}` | Between two and four repetitions |
| `.*?` | Lazy repetition; prefer the shortest match |
| `cat|dog` | Either alternative |
| `(name)`, `(?:name)` | Capturing / noncapturing group |
| `(\w+)\s+\1` | Repeated identical captured word |
| `(?=text)`, `(?!text)` | Positive / negative lookahead |
| `(?<=x)`, `(?<!x)` | Lookbehind subject to the regex engine's length rules |
| `(?i)`, `(?s)`, `(?m)` | Inline case-insensitive, dot-matches-newline, line-anchor modes |

Native Find/Replace is line-by-line until **Use multi-line matching** is enabled.
Multi-line mode searches the buffer as a whole; use `\n`, `\r?\n` or `(?s)` when
needed. Compact Insight searches already search the whole source with multiline
anchors: `^`/`$` address each line, while dot still excludes newline unless `(?s)`
is used. A negated set such as `[^x]` can consume a newline; use `[^x\r\n]` to
keep it on one line. Case/Word switches still apply to compact regex queries.
Invalid patterns report an error. Regex is a text language, not a Python/C++
parser; a broad expression can also match comments, strings and unrelated names.

## Replacement text

With native Regex enabled, `\0` inserts the entire match and `\1` through `\9`
insert captured groups. For example, search `\bget_(\w+)\b` and replace with
`fetch_\1`: `get_name` becomes `fetch_name`. Use `\\` for a literal backslash.
`$1` and `${name}` are not this dialog's capture-reference syntax. With Regex off,
replacement is literal except when Use escape sequences is enabled. The same
option permits newline/tab characters in replacement text. An empty replacement
deletes the matched text. Preview with Find Next and Replace one before Replace
All; a regex can match zero characters or much more than intended.

## Keywords / Boolean project search

Enabling Keywords/Boolean selects a separate query parser and disables the plain
Regex switch. Operators are case-insensitive; precedence is NOT, AND, OR. Use
parentheses to make grouping explicit.

| Query | Meaning |
| --- | --- |
| `open file`, `open AND file`, `open + file` | Both terms within the configured line distance |
| `open OR close` | Either term |
| `open NOT error`, `open !error`, `open -error` | Positive term without excluded term in the window |
| `(open OR close) AND file` | Grouped expression |
| `"open file"` | Literal phrase |
| `=Open` | Case-sensitive term regardless of global Case |
| `~opfl` | Fuzzy subsequence term |
| `?"get_[a-z]+"` | Regex term; quote regex containing spaces/parentheses |

Within quoted terms, `\"` represents a quote and `\\` a backslash; other regex
backslashes are preserved. Word applies to ordinary terms; a regex term chooses
its own boundaries. A query needs a positive witness: purely negative expressions
produce no highlighted matches. Maximum keyword distance is 0–50 lines; 0 requires
the positive terms on one line. Word variations adds regular English forms such
as `open/opens/opened/opening` or `save/saving` to eligible base keywords. It is
not a dictionary, Russian morphology or general stemming; regex, fuzzy and phrase
terms retain their own semantics. Limits include 32 terms, 128 parser nodes and
16 nested parser levels; invalid/over-limit queries report an error.

## Symbol/file lookup and filename masks

Lookup filters are distinct from text search. Space-separated terms intersect;
`~term` is an ordered subsequence; `?pattern` is a case-insensitive regex on the
symbol name (or file path in file lists). `"name"` matches an exact symbol/name;
`file:"full path"` and `scope:"qualified scope"` select exact fields. `@pathpart`
restricts the path and `scope=part` the scope. A trailing `(` in symbol filters
restricts results to functions/prototypes/methods. Matching is case-insensitive.

Filename masks use glob syntax, not regex: `*` means any sequence and `?` one
character. Separate project search masks with semicolons, e.g. `*.c;*.h;*.py`.
The lookup file filters can also use glob terms on their stored paths. Regex
`.*\.py$` belongs in a regex filter, not a glob mask. Master File List masks have
separate directory/scan limits: see [Project Files](panel-project-files.md).

## Results and limits

Search selects/marks text without editing it. Selecting a project result previews
it; Enter/double-click opens it. Alt+Up/Down previews adjacent results; Shift+F8/F9
moves between results. Stop retains partial output. Changed source snapshots must
be searched again before a stale hit can be opened. Insight compact source search
is limited to 2 MiB per source and 1024 UTF-8 bytes per query; results and project
scans have additional bounds reported in the UI. Context only searches the loaded
preview, not the entire project. [Context controls](panel-context.md).
