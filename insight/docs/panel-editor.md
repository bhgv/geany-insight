# Editor

The main text buffer and open-file tabs.

## Controls

Ctrl+F opens Find; Ctrl+H opens Replace. Double-click a line number to toggle a bookmark. The context menu can assign the file language. Overview shows code position and markers.

## Copy a selection by dragging

Select any text: a word, a letter, spaces, part of a line or several lines. Press the left mouse button inside the selection, drag to the destination and release. **Ctrl held when the mouse button is released copies the selection; otherwise it moves.** You can press Ctrl only at the end of the gesture or release it before dropping. Text is inserted at the drop position without adding a newline. Include line endings in the selection when copying complete lines.

Plain dragging moves the selection. Press or release Ctrl during a drag to switch actions; Esc cancels. A copy takes one Undo; moving uses separate undo steps for insertion and deletion. A read-only buffer refuses insertion.

Start inside the text rather than the line-number margin. Ctrl+click outside the selection still navigates to a definition. Enable text drag and drop in editor preferences if it is disabled.

![Ctrl drag keeps the source and inserts a copy](media/editor-drag.svg)

Actual Linux editor after copying two lines; the inserted copy is selected:

![Two-line copy in the native editor](media/editor-drag.jpg)

## Short tutorial

Open a file, find a name with Ctrl+F, set a bookmark and select a symbol in File Symbols.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [tutorials.md](tutorials.md).

Copy/Cut/Duplicate Symbol and exact selection duplication: [structural editing](structural-selection.md).

История переходов и Undo All / Redo All: [руководство](editor-history.md).

Вставка GUID и символов, просмотр буфера обмена: [руководство](insert-tools.md).

Вставка файла и экспорт текста: [руководство](file-text.md).

[Renumber / Перенумерация](renumber.md) — integer sequences, offset and radix conversion.

[Paragraph and lines / Абзацы и строки](paragraph-tools.md) — Reform Paragraph, Join Lines, Preview.

[Line insertion / Вставка строк](line-insertion.md) — above, below, split and stay, marked preview.

[Text boundaries / Границы текста](text-boundaries.md) — smart line/file edges, literal sentence selection and native word fragments.

[Calculation and file status / Вычисления и состояние файла](calculate.md) — bounded expression evaluation, calculator window, current-buffer statistics.

[Window scrolling / Прокрутка окна](window-scrolling.md) — viewport commands preserve caret and selections.

[Word and character operations / Операции со словами и символами](word-editing.md) — bounded native Cut Word/Left and clipboard-preserving Delete Line/Character.

[Command and key references / Списки команд и клавиш](command-reference.md) — editable snapshots of current menus and registered keyboard bindings.
