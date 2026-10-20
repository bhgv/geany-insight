# Insert and export file text

**Edit → Insert and clipboard** provides **Insert File…**, **Save Selection…** and **Save a Copy…**.

| Command | Action |
| --- | --- |
| Insert File | Replaces the selection or inserts at the caret using a local text file, in one Undo step. |
| Save Selection | Exports the exact selected text and opens the result in a separate tab. |
| Save a Copy | Exports the entire current buffer and opens the copy in a separate tab. |

## Insert File

Accepts regular UTF-8 files without NUL, up to 8 MiB. One leading UTF-8 BOM is removed. All remaining bytes, indentation, LF/CRLF/CR and unterminated final lines are preserved without intelligent indentation. An empty file replaces a selection with empty text; with no selection it adds nothing. Normal Undo reverses the edit.

Requires write permission and one normal selection without virtual space. Any text edit while the chooser is open invalidates the request. On confirmation the original document and selection must still be current. Closed/read-only targets refuse insertion. Binary data, other encodings, directories and symbolic links are rejected.

## Save Selection / Save a Copy

Exports buffer text as UTF-8 without adding a BOM, preserving line endings without adding a final EOL. The original filename, Undo and saved/modified state are preserved. Read-only sources are allowed. Save Selection requires a nonempty normal selection, including reverse selections. Exported text is limited to 8 MiB.

Choose a path in the standard Save dialog. Existing closed files require overwrite confirmation. Paths belonging to any open document, including the original file and detected file aliases, are protected: close the destination first. Appending to an open buffer is not implemented yet.

![Native Linux export chooser](../media/file-text-export.jpg)

Changes to the source text while the chooser is open cancel the write. Save Selection also rechecks the original selection. Cancel leaves both source and destination unchanged. Successful exports open as ordinary editor documents; write errors appear in the status bar.

New destinations use a sibling temporary file and a no-overwrite move. Existing regular files use conditional etag replacement. GIO checks do not lock the filesystem. Metadata and original encoding are not duplicated. Source Insight's project-wide and wildcard browser options are not implemented; this version uses the standard local file chooser.

![Exact text insertion and export](../media/file-text.svg)

Reference: [Insert File](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Insert_File.htm), [Save Selection](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Save_Selection.htm), [Save A Copy](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Save_A_Copy.htm).
