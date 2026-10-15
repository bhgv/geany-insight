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

Builtins such as filename, date and selection are inserted as ordinary text. Undo/redo, edits outside the active field, multiple selections and unsupported ranges end the session; no source text is rewritten to maintain fields. The session is per editor buffer and is not saved across restart. At most 1024 occurrences are tracked; insertion retains the existing one-step Undo and CRLF/UTF-8 behavior. The option is off by default, preserving ordinary template insertion. Project-specific template stores and name completion are described below.

## Project stores and name completion

The store selector chooses **Global templates** (your existing notes database) or **Project templates**. Project clips are stored next to the project file as `<project-file>.templates.conf`, so separate projects may reuse the same names. Bookmarks remain in the global store. Creation, capture, edit, rename, delete, import/export and insertion apply to the selected store. The store preference is saved; project close temporarily shows the global store. The reload button rereads the project sidecar. Invalid sidecars are preserved and project writes are disabled until a valid store is loaded. A dialog opened before the project/store changes cannot write to the new store. Clip capture/deletion publish the new in-memory snapshot only after a successful save.

In the editor, the Complete snippet command (Tab by default) first tries ordinary Geany snippets. If none matches, it completes names from the selected template store, respecting language restrictions. An exact name opens parameters/inserts the template; a prefix offers a completion list. The typed name is replaced as one undo operation. Canceling parameters leaves the original prefix and caret. Completion checks buffer identity, position, text revision and store generation before applying a choice; stale selections decline. Names match case-sensitively and candidates containing line breaks/tabs are excluded from the popup.
