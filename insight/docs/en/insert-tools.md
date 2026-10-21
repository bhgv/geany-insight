# Insert tools and clipboard viewer

Open **Edit → Insert and clipboard**.

| Command | Result |
| --- | --- |
| Insert GUID | Generates a UUID v4 in `xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx` form and replaces the selection or inserts it at the caret. |
| Insert ASCII / Unicode… | Opens a character-code field, radix selector and preview. |
| Show Clipboard | Opens a separate window showing the system clipboard's current text. |

## Character code

Auto interprets plain numbers as decimal, `0x` and `U+` as hexadecimal, `0o` as octal and `0b` as binary. A leading zero without a prefix remains decimal. You can also choose the radix explicitly. `65`, `0x41`, `0o101` and `0b1000001` insert `A`; `U+03B1` inserts `α`.

The preview shows the character and Unicode code; controls are shown only as a code. Invalid input disables Insert. Codes U+0001–U+10FFFF are accepted except surrogates U+D800–U+DFFF. NUL is unsupported. Signs, trailing junk, overflow and invalid digits are rejected.

![Native Linux character dialog](../media/insert-character.jpg)

GUID and character insertion replace the exact normal selection, including reverse selections, in one Undo step. The clipboard is unchanged. A writable document and one normal selection without virtual space are required. Any text edit after opening the dialog invalidates the request. On confirmation, the original document must still be open and current with the original caret/selection and write permission. Cancel inserts nothing.

Characters are inserted as UTF-8 without intelligent indentation. Codes 9, 10 and 13 insert literal TAB, LF and CR; line endings are not converted to the document mode.

## Show Clipboard

The window displays a text snapshot in monospace, preserving spaces, tabs and line endings. Text cannot be edited or selected. **Refresh** reads it again; repeating Show Clipboard refreshes the existing window. Closing the viewer leaves the document and clipboard unchanged.

![Native Linux clipboard window](../media/clipboard-view.jpg)

Reads are asynchronous: responses for closed windows are discarded, and old reads cannot overwrite a newer Refresh. UTF-8 text up to 256 KiB is displayed; empty, non-text or oversized data produces a status message. Images, RTF and HTML are not rendered. There is no automatic owner-change tracking or content persistence between runs.

![Code input, validation and one Undo step](../media/insert-tools.svg)

Reference: [Insert GUID](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Insert_GUID.htm), [Insert ASCII](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Insert_ASCII.htm), [Show Clipboard](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Show_Clipboard.htm). Unicode support extends the ASCII command.

Insert File and text export: [guide](file-text.md).

[Renumber: columns and multiple selections](renumber.md) — one source-order sequence, colored before/after preview, per-fragment selection and one Undo/Redo.
