# Clips and templates

Saved text fragments and insertion templates.

## Controls

Capture saves the selection. Paste inserts a clip in the editor. Templates support variables and caret placement. Filter and Show all languages control visibility; a clip can belong to multiple language profiles.

## Short tutorial

Select code and capture it. Name the clip. Move to another location and paste it; undo the insertion with one Undo.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [tutorials.md](tutorials.md).

## Editable fields after insertion

In Template parameters, enable **Keep template fields editable (Tab / Shift+Tab)**. The first custom variable occurrence is selected after insertion. Replace it, use Tab to select the next occurrence (including empty ones), and Shift+Tab to return. Repeated variables start with the same supplied value and remain independently editable. After the last field, Tab clears the field marks and moves to `$cursor$` or the expansion end. Escape leaves the inserted text and ends field navigation.

Builtins such as filename, date and selection are inserted as ordinary text. Undo/redo, edits outside the active field, multiple selections and unsupported ranges end the session; no source text is rewritten to maintain fields. The session is per editor buffer and is not saved across restart. At most 1024 occurrences are tracked; insertion retains the existing one-step Undo and CRLF/UTF-8 behavior. The option is off by default, preserving ordinary template insertion. Project-specific template stores and completion remain outstanding.
