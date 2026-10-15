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

## Project store and keyboard

Each project uses `<project file>.bookmarks.conf` beside its project file, with
paths relative to the source root. With no project open, bookmarks use the global
`insight/notes.conf`; global clips in that file remain independent. This is Geany
Insight's own INI format, not Source Insight XML.

The label identifies the active store. Reload bookmarks reads external changes
without first overwriting the file. A damaged store blocks writes until repaired
and reloaded. If moving bookmarks cannot be saved while changing projects, the
old store stays in memory, is identified as unsaved and blocks editing. Export
can preserve it. Reload refuses to discard unsaved data.

Columns show name, file, containing symbol and line. Filtering also matches the
symbol name. Symbol anchors retain function/class-relative offsets across source
edits; ambiguous or missing source text does not trigger a guessed jump.

- Enter in an empty field creates A, B, …, Z, A1, B1, … using an unused name.
- Enter in the list or Go to activates the selected bookmark.
- Ctrl+N adds a bookmark or updates the entered name.
- Ctrl+X deletes the selected bookmark.
- Ctrl+O / Ctrl+S open import / export.

A project change during a modal operation prevents writing into the new store.
Failed add/delete/rename operations retain the old list and editor markers.
Source Insight XML import remains unimplemented; import/export uses the native format.

## Выбор, сохранение и удаление нескольких закладок

Ctrl+щелчок выбирает отдельные строки, Shift+щелчок — диапазон. Ctrl+A выбирает
все видимые строки. Delete или Ctrl+X удаляет выбранные закладки одной записью:
при ошибке записи сохраняются список, выделение и маркеры редактора. Фильтрация
и обновление сохраняют выбранные имена, которые остаются видимыми.

**Экспортировать выбранные…** и Ctrl+S сохраняют только выбранные строки.
Без выделения экспорт не начинается. Правый щелчок или Menu/Shift+F10 открывает
меню со значками: переход, переименование, пакетное удаление, импорт, экспорт
выбранных, **Экспортировать все закладки…**, выбор всех и снятие выделения.
Экспорт всех включает закладки, скрытые фильтром; Ctrl+A выбирает только видимые.
Переименование требует одной строки. При множественном выборе Enter открывает
выбранную строку с курсором. Правый щелчок по уже выбранной строке сохраняет
группу и не переводит редактор в другое место.

Экспорт фиксирует выбранные записи до открытия диалога, включая символьные
привязки и переносимые пути. Смена проекта/хранилища в открытом диалоге отменяет
экспорт. Clips по-прежнему экспортирует всю панель. Используется собственный
формат Geany Insight (`.conf`), а не XML Source Insight.

## Команды поля и подтверждение удаления

Enter в поле открывает закладку с точным именем. Уникальное частичное имя тоже
открывает закладку; при нескольких совпадениях нужно выбрать строку. Десятичное
число переводит редактор на строку с этим номером, считая от 1. Существующая
закладка с точно таким числовым именем имеет приоритет. Ноль и номер вне файла
вызывают сообщение без создания закладки. Несовпавшее нечисловое имя создаёт
закладку в текущем месте редактора. Пустое поле сохраняет автоматические имена
A/B/…; Enter в списке по-прежнему открывает выбранную строку.

Меню списка содержит **Удалить все закладки…**, включая скрытые фильтром.
Команда всегда запрашивает подтверждение, по умолчанию выбрана отмена. Удаляются
только закладки активного хранилища; шаблоны и исходные файлы сохраняются.
**Подтверждать удаление закладок** в том же меню включает подтверждение также
для Delete/Ctrl+X выбранных строк. Настройка сохраняется между запусками; для
прежних профилей она выключена. Отмена, перечитывание хранилища/смена проекта
в открытом подтверждении и ошибка записи не применяют запрошенное удаление.
Это отдельная настройка, а не весь диалог Bookmark Options Source Insight:
политики сохранения между сеансами и удаления при закрытии файла ещё не перенесены.

## Шрифт и цвет панели

В меню списка закладок (правый щелчок или Menu / Shift+F10) выберите
«Шрифт и цвет…». Включите «Использовать свои настройки оформления», задайте
шрифт размером 4–96 пунктов, цвет текста и фона, затем нажмите «Применить».
«Отмена» оставляет прежнее оформление. Отключение своих настроек возвращает
оформление текущей темы; выбранные шрифт и цвета сохраняются для повторного
включения. Настройка применяется ко всем столбцам панели и сохраняется между
запусками независимо от Project Folders и Browse disk. Цвета выделения задаёт
тема. Настройка относится к панели Bookmarks, а не к отдельному списку закладок
редактора, открываемому кнопкой с книгой.

## Удаление при закрытии файла

В меню панели включите «Удалять закладки при закрытии файла». По умолчанию
настройка выключена и сохраняется между запусками. При включении закрытие
файла удаляет все его закладки из активного хранилища, включая скрытые фильтром.
Закладки других файлов и Clips сохраняются. Подтверждение ручного удаления
не применяется к этой автоматической операции. Отмена закрытия изменённого
файла не удаляет закладки. При ошибке записи прежний список остаётся в памяти;
закладки можно восстановить при повторном открытии файла. Настройка не очищает
закладки при обычном выходе с сохранением сеанса.
