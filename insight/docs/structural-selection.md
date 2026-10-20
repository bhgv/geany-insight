# Структурное выделение

Эталон поведения:
https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Selection_Shortcuts.htm

**Edit → Structural selection**:

| Команда | Клавиши | Поведение |
| --- | --- | --- |
| Select enclosing block / expand | Ctrl+Alt+B | Выделяет наименьшую пару `()`, `[]` или `{}`, охватывающую курсор или всё выделение. Повторение выбирает следующий внешний блок. |
| Select matching brackets | Ctrl+Alt+M | Выделяет пару скобок, на которой или непосредственно после которой стоит курсор. |
| Select line | Ctrl+Alt+L | Вызывает штатное выделение текущей строки/строк Geany. |

Клавиши отличаются от Source Insight: штатные сочетания Geany, включая
Ctrl+- для масштаба, сохраняются. Команды не меняют текст файла.

Для блоков и пар используется существующая лексическая маска: скобки в
комментариях и строках не участвуют в поиске. Поддержаны C/C++, C#, Java,
JavaScript/TypeScript, Python, Lua и Sh; файлы UTF-8 до 2 MiB. Для Python/Lua
Ctrl+Alt+B также выбирает вложенные блоки отступов или `end`/`until`; повтор расширяет выделение к родительскому блоку. Ctrl+Alt+M остаётся командой пар скобок. Ограничения маски остаются:
нет AST, разрешения препроцессора, JS regex literals или интерполяции строк.
Несогласованные скобки не соединяются искусственно; при отсутствии внешнего
блока выделение сохраняется. Прямоугольное и множественное выделение отклоняются.
Выделение полного символа/функции — отдельный следующий этап.

В `insight/examples/structural-selection.c` поставьте курсор внутри
`value + 1`. Три нажатия Ctrl+Alt+B выделят выражение со скобками, тело `if`,
затем тело функции. Строка и комментарий с фиктивными скобками не помешают.

Проверка ядра: `python3 insight/tests/run.py`. Тест также включён в CMake,
Meson и ручную Windows-сборку. Автоматический запуск Actions остаётся выключенным.

Select Function (Ctrl+Alt+F) selects from the parser definition line through the closing body brace, Python suite end, or Lua function `end`. It does not include earlier return-type or annotation lines. Function Up/Down (Ctrl+Alt+PageUp/PageDown) navigate parsed definitions.

Python supports async/multiline definitions, nested suites, continuation brackets, tabs (8-column indentation) and CRLF. Lua balances nested function/if/for/while/do/repeat blocks; masked comments and strings do not contribute keywords. Function selection chooses the smallest containing parsed definition. Function tracking, lexical Calls and Overview use the same ranges. Unsupported languages and unfinished delimiter bodies retain a no-range result; this is lexical structure, not compiler syntax/type validation. Decorators preceding the parser definition line are not included.

## Copy Symbol / Cut Symbol / Duplicate Symbol

В **Edit → Structural selection** добавлены команды копирования, вырезания и дублирования целой функции. Они выбирают наименьшую завершённую функцию, содержащую курсор или всё выделение. Те же команды доступны в контекстном меню **File Symbols**: там обрабатывается выбранная строка панели, даже если курсор находится в другой функции. Project Symbols пока не предоставляет эти операции.

| Команда | Результат |
| --- | --- |
| Copy Symbol | Текст полной функции попадает в обычный буфер обмена. Положение курсора и Undo не меняются; read-only разрешён. |
| Cut Symbol | Функция удаляется и копируется в буфер обмена; одно Undo возвращает её. |
| Duplicate Symbol | Копия вставляется сразу после функции и выделяется; буфер обмена не меняется. Одно Undo удаляет копию. Имя остаётся прежним. |

Диапазон включает отступ на строке определения, тело, оставшийся комментарий на завершающей строке и её перевод строки. В конце файла без EOL перед копией добавляется один разделитель в формате документа (LF/CRLF/CR). Python выбирает вложенную функцию; Lua учитывает вложенные `if`/`end`. Предыдущие строки с декораторами, аннотациями или типом возврата не включаются.

Поддержаны отдельно расположенные определения функций и методов в языках со скобками, Python и Lua. Это лексические границы с актуальными parser tags, а не AST: классы, переменные, прототипы, неполные тела и несколько определений на одной строке отклоняются. Документ ограничен 2 МиБ, функция — 1 МиБ; нужны корректный UTF-8 без NUL и одно обычное выделение без виртуальных позиций. Изменённая строка выбранного символа проверяется заново, устаревший пункт панели не вырезает соседний код. Изменения в read-only запрещены.

![Три операции над функцией](media/symbol-edit.svg)

Реальный редактор Linux после Duplicate Symbol из File Symbols: вставленная функция выделена, следующая функция сохранена.

![Копия функции в редакторе](media/symbol-edit.jpg)

## Точное дублирование выделения

**Edit → Duplicate Line or Selection / Ctrl+D** дублирует именно выделенный фрагмент, включая части первой и последней строк, буквы, пробелы и переводы строк. Предварительное расширение многострочного выделения до полных строк больше не выполняется. Без выделения сохраняется дублирование текущей строки. Операция поддерживает обратное выделение и Undo/Redo; read-only запрещает изменение.
