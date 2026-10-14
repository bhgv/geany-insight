# Context and bookmarks

Single click on a symbol in Context follows its definition inside Context only. Double click on a code line opens the currently previewed file at that line in the editor and centers the caret vertically. Single-click navigation waits for the system double-click interval so it cannot redirect the double click.

Double click on an editor line number toggles its bookmark. A blue circle appears to the left of the number. The book icon in the toolbar opens a nonmodal searchable list: `filename:line> text`. Selecting a row opens that line in the editor. Scintilla markers follow inserted and deleted lines; named bookmarks are persisted with source anchors.

Open internal comparison from Tools → Diff / Merge…. External KDiff3 is optional and only runs when its button is clicked.

## File overview and change navigation

The miniature in **View → Overview** shows the file beside the editor.
Blue dots on its left identify bookmarks. Orange bars on its right identify
unsaved changes; green bars identify changes saved during the current file session.
Closing and reopening the file starts a fresh change history. Deleted text is
marked on the surviving line beside the deletion. Undo/Redo updates the marks,
and bookmarks move with the text.

**Edit → Next unsaved change** (`Alt+Num +`) and **Previous unsaved change**
(`Alt+Num -`) move between separate blocks of modified lines. These shortcuts use
the numeric keypad. The commands are also available by right-clicking the miniature.
Navigation skips the current block, does not wrap around, leaves the text unchanged,
and excludes saved changes. It supports files up to 2 MiB.

Right-click → **Overview Options…** controls bookmarks, change marks, saved changes
and hover messages. Hover messages show the line number, source text, change state
and bookmark state. These options persist across restarts. Only marks within the
visible miniature are drawn. Marks in the main editor's left margin have their own
setting.

### Function boundaries in the overview

**Overview Options…** provides **Show current function boundary** and
**Show all function boundaries**. The function containing the entire caret or
selection is outlined in purple; other functions use grey outlines. The current
outline disappears outside a function body. Hover messages include the function
name and its scope when supplied by the parser.

Outlines combine parser definitions with lexical body matching. C, C++, C#, Java,
JavaScript and TypeScript brace-bodied definitions are supported. Prototypes,
incomplete bodies and expression-only definitions receive no outline. Braces in
comments and strings are excluded. These are lexical boundaries, without semantic
handling of templates or preprocessing.

Computation runs in the background for files up to 2 MiB. Editing removes old
outlines until the symbols are refreshed. Further edits or closing the file reject
obsolete results. Both display options persist across restarts.

## Code templates

**Tools → Code templates / Clips…**, or the page with blue angle brackets on the
main toolbar, opens Clips. Buttons use icons with action names in tooltips.

1. Click **New code template**, enter a name and body, and save.
2. Select the clip and optionally select text in the editor.
3. Click **Insert as code template — expand variables** (page with brackets).
4. Enter custom parameter values and click **Insert**.

Example:

```c
// $date$ — $filename$
void $name$(void)
{
    $selection$$cursor$
}
```

`$date$` is the local `YYYY-MM-DD` date; `$time$` is `HH:MM:SS`; `$file$` is the full
path; `$filename$` is the basename; `$project$` is the project name; `$selection$`
is the selected text; `$cursor$` marks the final caret position. File variables are
empty for unnamed buffers; the project variable is empty without a project.
Other `$identifier$` variables are prompted once, with repeated occurrences sharing
a value. Names are case-sensitive ASCII identifiers starting with a letter or `_`.
`$$` produces a literal dollar; `$$date$$` inserts the literal text `$date$`.

Values are not expanded recursively, so selected source containing `$name$` retains
it literally. The first `$cursor$` sets the caret; subsequent markers are removed.
Without a marker the caret moves to the end. Insertion replaces one normal selection,
preserves the file's line endings and forms one Undo action; Redo restores the inserted
text. Cancel leaves the file unchanged. If the source, selection, line-ending mode or
active file changes during the dialog, insertion is rejected and must be repeated.

Ordinary **Paste** and double-clicking Clips insert literal text. Templates share
`config/insight/notes.conf` with Clips and use its existing edit, rename, import and
export buttons. Creating a duplicate name is rejected. Limits are 64 KiB per template
or value, 64 distinct variables and 256 KiB expanded output; the notes database holds
up to 64 entries. Completion integration is not yet implemented.

### Language-specific templates

**New code template** and **Edit clip** include an **Allowed language profiles**
list. Check one or more profiles, such as C and C++; leave all unchecked to allow
any language. Template insertion checks the file's active profile, including a
forced assignment from the editor menu. Profiles are matched by name, independently
of file extensions and the compatibility settings used for definition searches.
A template can name up to 32 profiles.

Clips hides templates for other languages when switching files. **Show all languages**
reveals them for editing and management but does not allow template expansion in an
incompatible profile. Ordinary literal Clips insertion remains available in this mode.
Bindings survive text edits, rename, import and export. Existing Clips without bindings
continue to work in every language. After renaming or deleting a profile, show all
languages and update the checks in Edit clip. Saved unknown profiles remain listed.

Bindings use the optional `languages` list in each clip's section of
`config/insight/notes.conf`. It follows standard GKeyFile list syntax, for example
`languages=C;C++;`. An empty or absent list means all languages.
