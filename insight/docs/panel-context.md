# Context

Definition previews without moving the main editor.

## Controls

Single-click a resolved link to change Context only. Double-click to open the shown file and center its line in the editor. History, definition selector, follow-caret, wrapping and search control the preview. Diff/Merge is on the main toolbar and in Tools.

## Short tutorial

Select a symbol in a list. Click another resolved definition in Context: the editor stays in place. Double-click to open the previewed line in the editor.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [context-bookmarks.md](context-bookmarks.md).

Member definition links and editor Follow resolve nominal C/C++ typedef/using chains in declaration scopes, including `this` and qualified namespace receivers. Ambiguous/cyclic types decline. Template argument substitution, overload selection and arbitrary chained expressions are not inferred.

Explicit qualified nested types can be inherited: `Derived::Nested`, an alias-qualified inherited alias, and functions returning those types resolve to the recorded declaring owner. Direct nested declarations hide bases. The same declaration reached through a diamond is one type target; different declarations, a non-type declaration hiding the name, missing bases, cycles or limits decline. Base aliases resolve in the owner's declaration scope and eligibility is checked before choosing owners. Inherited nested lookup is bounded to 4096 visited owners, 256 bases per owner, the shared 64-level recursion and 32 alias expansions. Unqualified nominal type names search the nearest class and its inherited types before falling back to enclosing scopes. A direct local type declaration wins, while a hidden or uncertain inherited name declines. Qualified chains select the nearest first component, then stay within that owner: a missing/hidden member does not reopen outer lookup. Reopened namespace declarations share one nominal namespace; namespace members keep use-file preference while class members use declaration context. All attempted alias expansions count toward the work limit, while completed unsuccessful probes do not inflate the displayed alias count. Template substitution, access, include/ODR semantics and exact positional scope for several functions on one source line remain open.

C++ variables whose declaration starts with global `::` qualification are indexed with that prefix retained in the recorded type. Namespace-scope, member and local declaration parsing use the same rule; qualified calls or assignments are not converted into variable declarations.

Для результатов функций nominal-поиск учитывает собственные параметры шаблона: зависимый `T` не выбирает одноимённый глобальный класс. Явный `::T` и обычные результаты разрешаются в контексте объявления; параметры-значения и локальные aliases тела не меняют тип результата. Выведение аргументов шаблонов и выбор перегрузки пока не выполняются.


Полная справка: [Режимы поиска/замены и языки запросов](search-replace.md).

## Компактная правая панель Context

Поиск и список кандидатов определений при запуске скрыты. Значки справа от
исходника расположены вертикально; полное название команды появляется в
подсказке при наведении. При небольшой высоте панель значков прокручивается.

| Значок | Назначение |
| --- | --- |
| Стрелка влево / вправо | Предыдущая / следующая позиция истории Context |
| Открыть документ | Открыть показанную строку в основном редакторе; также Enter |
| Лупа | Показать/скрыть поиск по предпросмотру; Ctrl+F в Context открывает его и фокусирует поле |
| Список | Показать/скрыть кандидатов определений; выбор меняет только Context |
| Прицел | Включить/выключить Follow: слежение за кареткой основного редактора |
| Выравнивание строк | Включить/выключить перенос длинных строк предпросмотра |
| Копировать | Скопировать адрес каретки предпросмотра `файл:строка` |
| Знак вопроса | Открыть эту справку |

Скрытие поиска или списка не очищает запрос/кандидатов и не меняет положение
редактора. Повторное нажатие раскрывает тот же элемент. F3/Shift+F3 повторяют
поиск; Enter в исходнике открывает строку, F12 следует определению только в
Context. Двойной щелчок открывает файл в редакторе.

При первом запуске боковые области открыты примерно на 100 логических пикселей,
Context занимает около четверти рабочей высоты. Разделители можно двигать до
узкой полосы; последние положения восстанавливаются после перезапуска.

## Проверенная навигация Python

Границы функций/классов определяются по отступам из снимка текста, включая
вложенные функции и возврат во внешнюю область после их окончания. Локальные
имена и параметры не выбираются из соседней функции; видимые имена замыканий
сохраняются. `self`/`cls` как первый параметр метода разрешаются в его
индексированном классе; одноимённые поля другого класса не подставляются.
Поддержаны непосредственно названные классы и индексированные аннотированные
получатели. Предпросмотр, F12 и автоматический Follow используют эти правила.

Это ограниченное лексическое разрешение, а не исполнение Python: динамические
атрибуты, произвольные цепочки/результаты вызовов, import/alias-граф, декораторы
и полное выведение типов не вычисляются. Непривязанное имя другого Python-файла
не считается импортом. Неопределённый получатель не переключается на случайное
одноимённое поле. Строки и комментарии не являются ссылками.
