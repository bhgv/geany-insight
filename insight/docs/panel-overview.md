# File overview

A miniature code map beside the editor.

## Controls

Click and drag to move the visible range. Ctrl+wheel changes scale; Shift+wheel takes larger steps. The context menu configures overview modes. Markers indicate bookmarks and changes.

## Short tutorial

Scroll a long file, add a bookmark and find its marker in Overview. Click the desired region to navigate there.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [panel-editor.md](panel-editor.md).

Границы функций также поддерживают Python по отступам (async/многострочные определения, вложенные функции) и Lua по вложенным блокам end/until. Текущей считается наименьшая содержащая курсор функция нативного парсера; комментарии и строки маскируются. Тот же расчёт используется в Select Function и lexical Calls.
