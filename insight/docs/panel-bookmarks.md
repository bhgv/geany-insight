# Bookmarks

Quick access to marked source lines.

## Controls

Double-click a line number to toggle its blue bookmark circle. The book button opens the list. The list supports navigation, deletion, renaming, filtering and import/export.

## Short tutorial

Mark two lines. Open the list with the book button. Click a file:line > text entry to navigate in the editor.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [context-bookmarks.md](context-bookmarks.md).

## Перенос проекта

При выбранном корне проекта/workspace экспорт закладок записывает файлы внутри корня относительными путями (формат notes v2). Перед импортом в перемещённую копию выберите её новый корень. Внешние файлы сохраняют абсолютные пути. Для относительных закладок импорт требует корень и отклоняет выход через `..`; совпадающие имена отклоняются, существующие заметки сохраняются. Старые снимки v1 читаются; обычное сохранение заметок и экспорт Clips остаются v1. Clips содержат текст/языковые привязки, а не пути источников. Список файлов проекта уже сохраняет внутренние пути относительно корня.

## Symbol-relative restoration

Bookmarks remember the nearest preceding indexed function/class (exact name,
scope and kind) plus a line offset. Reopening after external edits relocates the
mark with that declaration, even when another line has the same text at the old
number. Duplicate declarations decline relocation. If the symbol is missing,
only a unique text match is used; an old line-number coincidence cannot win.
Legacy notes remain readable. Anchor metadata survives rename, selected export,
merge and project-relative export. Capture uses current parser generations;
explicit navigation/close can refresh a bookmarked buffer up to 2 MiB. Live
markers still track ordinary editing. Arbitrary edits inside a function may
change the meaning of its saved offset; this is not semantic change tracking.
