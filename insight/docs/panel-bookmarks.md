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
