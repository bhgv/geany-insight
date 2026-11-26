# Structural selection and symbol editing

Edit → Structural selection offers enclosing-block expansion (Ctrl+Alt+B), matching brackets (Ctrl+Alt+M), line selection (Ctrl+Alt+L) and Select Function (Ctrl+Alt+F). Function Up/Down use Ctrl+Alt+PageUp/PageDown. Lexical masks exclude comments and strings; brace, Python indentation and Lua end blocks are supported. This is lexical structure, not semantic analysis.

## Copy Symbol / Cut Symbol / Duplicate Symbol / Paste Symbol

The Edit menu operates on the smallest complete supported definition enclosing the caret or entire selection. The File Symbols context menu operates on its selected definition, independently of the caret. Project Symbols does not offer these commands yet.

| Command | Result |
| --- | --- |
| Copy Symbol | Copies the full function without moving the caret or changing Undo. Read-only is allowed. |
| Paste Symbol… | Previews clipboard text inserted before the captured definition, then applies in one Undo action. |
| Cut Symbol | Copies and removes the function. One Undo restores it. |
| Duplicate Symbol | Inserts and selects a copy immediately after the function. One Undo removes it; clipboard and symbol name are unchanged. |

The range includes indentation on the parser definition line, the body, trailing comments and its final line ending. At EOF without EOL, duplication inserts a separator using the document's LF/CRLF/CR mode. Previous decorator, annotation and return-type lines are excluded. Functions and methods in brace languages, Python and Lua are supported. The smallest nested function wins.

Named class, struct, union, enum, namespace and interface definitions are supported where the lexical range recognizer and parser agree; Python classes are supported. Variables, forward declarations, aliases with trailing declarators, unfinished bodies and neighboring definitions on one line are rejected. Fresh tags validate the name and parser line before an outline command edits text. Limits: 2 MiB document, 1 MiB definition, valid UTF-8 without NUL, one normal selection without virtual space. Read-only buffers refuse mutations.

![Copy, cut and duplicate](../media/symbol-edit.svg)

Actual Linux editor after Duplicate Symbol from File Symbols: the new function is selected and the following function remains intact.

![Native function duplication](../media/symbol-edit.jpg)

## Exact selection duplication

Edit → Duplicate Line or Selection / Ctrl+D duplicates exactly the selected letters, words, spaces or partial multiline text. Boundary lines are no longer expanded. With no selection the current line is duplicated. Reverse selections and Undo/Redo are supported; read-only buffers refuse changes.

[Delimiter navigation and enclosing-pair browser / Переходы по скобкам](delimiter-navigation.md).

### Paste Symbol review and guarded editing

Paste Symbol is available in Edit → Structural selection and the File Symbols context menu. It captures the original document and definition before asynchronously reading the clipboard. The colored, numbered read-only panes show full source before and after. Apply inserts before the definition, without replacing its body or the current selection; Cancel inserts nothing. A missing final clipboard EOL adds the document EOL; existing clipboard bytes and line endings returned by GTK stay exact. No indentation adjustment or symbol renaming occurs, and disk files are not saved. Clipboard payloads are plain UTF-8 text, not parsed ASTs.

All four commands have configurable Editor keybindings, with no default shortcuts. Cut/Duplicate/Paste share an exact splice planner and one Undo/Redo. Source identity, filename, file type, full text, text revision, ordinary selection, active tab and write permission are rechecked. An edit followed by Undo still invalidates Paste. Closed, renamed or switched sources refuse Apply. Legal extension substitutions are detected against the actual resulting text; inspect the source and use Undo if interrupted. Copy remains available for read-only sources. Column, multiple and virtual-space selections are refused. Limits: 2 MiB source, 1 MiB definition or clipboard payload.

![Symbol editing flow](../media/paste-symbol.svg)

References: [Paste Symbol](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Paste_Symbol.htm), [Copy Symbol](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Copy_Symbol.htm), [Cut Symbol](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Cut_Symbol.htm), [Duplicate Symbol](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Duplicate_Symbol.htm).

![Actual native Paste Symbol review](../media/paste-symbol.jpg)

Complete Linux editor under Xvfb using synthetic C fixtures. The right pane selects the insertion before the original function.

## Select matching quotes and literals

**Edit → Structural selection → Select matching brackets or quotes**, **Ctrl+Alt+M**, now selects completed lexical literals as well as code brackets. Place the caret on, or just after, an opening/closing delimiter. Selection includes the delimiters and uses source byte positions, including UTF-8 and CRLF. It does not edit text, clipboard or Undo and works in read-only documents.

Supported forms follow the existing lexical dialects: C/C++ quoted strings and characters, C++ raw strings, Verilog double-quoted strings, Python ordinary/triple quotes, Lua ordinary/long-bracket literals, JavaScript quotes/templates and shell quotes. Comments, unfinished tokens, C++ digit separators and Verilog numeric apostrophes/escaped identifiers do not become literals. Interpolation is treated as opaque text; this is lexical selection, not an AST. Encoding/language prefixes before ordinary quotes are excluded, while raw/long delimiters belong to their token.

An actual code bracket at the caret takes precedence over the preceding literal. Otherwise a literal boundary takes precedence over an adjacent preceding bracket; this lets the opening quote in `f("value")` select the string. At a shared boundary between adjacent literals, the next literal wins. Unsupported languages, multiple/rectangular selections and virtual-space selections retain their position. Source size is limited to 2 MiB.

![Matching literal selection](../media/literal-selection.svg)

Reference: [Source Insight Select Match](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Select_Match.htm). Existing bracket and enclosing-block shortcuts are unchanged.

## Expand and shrink as one selection workflow

| Action | Shortcut |
| --- | --- |
| Expand to enclosing block | Ctrl+Alt+B |
| Select matching bracket or literal | Ctrl+Alt+M |
| Select current line | Ctrl+Alt+L |
| Select enclosing function | Ctrl+Alt+F |
| Restore previous structural selection | Ctrl+Alt+Shift+B |

**Shrink structural selection** restores the previous exact anchor and caret, including reversed selections. For example, select a string, expand to its argument parentheses, expand to the enclosing block, then shrink through the same ranges back to the original caret. Line and Function selections participate in the same history. A repeated command that leaves the range unchanged adds no duplicate entry. This reversible selection history is an editor extension; it is not a claim of a corresponding Source Insight command.

Each editor owns up to 128 prior ranges. Switching documents preserves each independent stack; closing a document discards it. Text modifications, including Undo/Redo, clear the stack even if the original bytes are restored. Mouse clicks, observed manual selection moves and source file-type changes invalidate it as well. A complex/multiple/virtual-space selection is refused; no source mutation, clipboard operation or text Undo entry is introduced. The stack is transient and is not saved in project settings. Restoring an empty/invalidated stack leaves the current selection untouched.
