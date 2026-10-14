# Context and bookmarks

Single click on a symbol in Context follows its definition inside Context only. Double click on a code line opens the currently previewed file at that line in the editor and centers the caret vertically. Single-click navigation waits for the system double-click interval so it cannot redirect the double click.

Double click on an editor line number toggles its bookmark. A blue circle appears to the left of the number. The book icon in the toolbar opens a nonmodal searchable list: `filename:line> text`. Selecting a row opens that line in the editor. Scintilla markers follow inserted and deleted lines; named bookmarks are persisted with source anchors.

Open internal comparison from Tools → Diff / Merge…. External KDiff3 is optional and only runs when its button is clicked.

## Обзор файла и переходы по правкам

Миниатюра **Вид → Обзор** (Overview) показывает файл справа от редактора.
Синие кружки слева обозначают закладки. Оранжевая полоса справа обозначает
несохранённые правки, зелёная — правки, уже сохранённые в текущем сеансе файла.
После закрытия и повторного открытия файла история изменений начинается заново.
Удаление текста отмечается на оставшейся строке рядом с местом удаления.
Метки обновляются при Undo/Redo; закладки перемещаются вместе с текстом.

**Правка → Следующее несохранённое изменение** (`Alt+Num +`) и
**Предыдущее несохранённое изменение** (`Alt+Num -`) переходят между отдельными
блоками изменённых строк. Клавиши +/− относятся к цифровому блоку клавиатуры.
Команды также доступны по правому клику на миниатюре. Переход пропускает текущий
блок, не делает круг по файлу и не меняет его содержимое; сохранённые изменения
не участвуют. Переходы поддерживают файлы до 2 МиБ.

Правый клик → **Параметры обзора…** включает и выключает закладки, изменения,
сохранённые изменения и подсказки при наведении. Подсказка содержит номер и текст
строки, состояние правки и наличие закладки. Эти параметры сохраняются между
запусками. Обзор рисует только отметки строк, видимых в миниатюре.
Настройка отметок в левом поле основного редактора остаётся отдельной.
