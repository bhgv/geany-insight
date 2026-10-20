# Structural selection and symbol editing

Edit → Structural selection offers enclosing-block expansion (Ctrl+Alt+B), matching brackets (Ctrl+Alt+M), line selection (Ctrl+Alt+L) and Select Function (Ctrl+Alt+F). Function Up/Down use Ctrl+Alt+PageUp/PageDown. Lexical masks exclude comments and strings; brace, Python indentation and Lua end blocks are supported. This is lexical structure, not semantic analysis.

## Copy Symbol / Cut Symbol / Duplicate Symbol

The Edit menu operates on the smallest complete function enclosing the caret or entire selection. The File Symbols context menu operates on its selected function, independently of the caret. Project Symbols does not offer these commands yet.

| Command | Result |
| --- | --- |
| Copy Symbol | Copies the full function without moving the caret or changing Undo. Read-only is allowed. |
| Cut Symbol | Copies and removes the function. One Undo restores it. |
| Duplicate Symbol | Inserts and selects a copy immediately after the function. One Undo removes it; clipboard and symbol name are unchanged. |

The range includes indentation on the parser definition line, the body, trailing comments and its final line ending. At EOF without EOL, duplication inserts a separator using the document's LF/CRLF/CR mode. Previous decorator, annotation and return-type lines are excluded. Functions and methods in brace languages, Python and Lua are supported. The smallest nested function wins.

Classes, variables, prototypes, unfinished bodies and neighboring definitions on one line are rejected. Fresh tags validate the name and parser line before an outline command edits text. Limits: 2 MiB document, 1 MiB function, valid UTF-8 without NUL, one normal selection without virtual space. Read-only buffers refuse mutations.

![Copy, cut and duplicate](../media/symbol-edit.svg)

Actual Linux editor after Duplicate Symbol from File Symbols: the new function is selected and the following function remains intact.

![Native function duplication](../media/symbol-edit.jpg)

## Exact selection duplication

Edit → Duplicate Line or Selection / Ctrl+D duplicates exactly the selected letters, words, spaces or partial multiline text. Boundary lines are no longer expanded. With no selection the current line is duplicated. Reverse selections and Undo/Redo are supported; read-only buffers refuse changes.
