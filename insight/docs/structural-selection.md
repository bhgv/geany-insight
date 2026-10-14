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
это пары скобок, а не блоки отступов или `end`. Ограничения маски остаются:
нет AST, разрешения препроцессора, JS regex literals или интерполяции строк.
Несогласованные скобки не соединяются искусственно; при отсутствии внешнего
блока выделение сохраняется. Прямоугольное и множественное выделение отклоняются.
Выделение полного символа/функции — отдельный следующий этап.

В `insight/examples/structural-selection.c` поставьте курсор внутри
`value + 1`. Три нажатия Ctrl+Alt+B выделят выражение со скобками, тело `if`,
затем тело функции. Строка и комментарий с фиктивными скобками не помешают.

Проверка ядра: `python3 insight/tests/run.py`. Тест также включён в CMake,
Meson и ручную Windows-сборку. Автоматический запуск Actions остаётся выключенным.

Select Function (Ctrl+Alt+F) selects from the parser definition line through the closing body brace in supported brace languages. It does not include earlier return-type or annotation lines. Function Up/Down (Ctrl+Alt+PageUp/PageDown) navigate parsed definitions.
