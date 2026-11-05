# Отладчик Python

Откройте буфер Python и выберите **Скрипты → Debug Current Python Buffer…**.
Отладчик выполняет снимок текущего буфера, включая несохранённые правки,
в отдельном процессе Python 3. Переменная `GEANY_INSIGHT_PYTHON` задаёт
интерпретатор. В `__file__` передаётся имя исходного файла, а его каталог
доступен для импорта. Программа действительно выполняется и может менять
файлы или иметь другие побочные эффекты. Вкладка **Console** показывает текстовый вывод Python в stdout и stderr. Когда программа вызывает `input()` или `sys.stdin.readline()`, открывается вкладка **Console**. Введите строку и нажмите **Enter**. Пустая строка тоже передаётся программе. Кнопка **Send EOF** закрывает текстовый ввод до перезапуска.

## Управление выполнением

Щелчок по номеру строки ставит или снимает точку останова. **F5** продолжает
выполнение, **F10** делает шаг через вызов, **F11** входит в вызов,
**Shift+F11** выходит из функции. **Pause** запрашивает остановку на ближайшей
отслеживаемой строке. **Shift+F5** завершает процесс, а **Ctrl+F5** перезапускает
исходный снимок. **Go to Source** открывает строку, если буфер совпадает со
снимком. Точка входа по умолчанию — текущий файл. Другой можно выбрать командой **Скрипты → Debug Python File…**. При открытом проекте отладчик следует за реальными вызовами Python в его каталоге. Чтобы поставить точку до первого перехода в файл, нажмите **Breakpoints in File…**, выберите файл и укажите строки. Точки хранятся отдельно для каждого файла и восстанавливаются при следующем запуске отладки. Для каждого языка настройки привязаны к проекту Geany или, вне проекта, к файлу точки входа. Для буферов без имени точки действуют только в текущем сеансе отладки. Номера строк сохраняются как есть. Точка входа и изменённые открытые модули проекта выполняются со снимков. Для импорта файл модуля должен уже существовать на диске. Остальные модули Python читает как обычно. Перезапуск повторно использует сохранённые снимки и очищает консоль.


## Просмотр кадров стека

На паузе выберите кадр в списке над вкладкой **Stack and locals**. Кадр **#0**
соответствует текущей остановке. Кадры выше по стеку позволяют посмотреть место
вызова, локальные переменные и **Watch graph** вызывающей функции. Подсказки
при наведении тоже используют значения выбранного кадра. **Go to Source**
открывает строку выбранного кадра, если текст редактора совпадает со снимком.

Выбор кадра меняет только просмотр. Шаги, включая **Step Out**, выполняются
от фактической точки остановки. На следующей остановке снова выбран кадр **#0**.
Список ограничен первыми 16 кадрами стека и показывает кадры доступных исходников.

## Значения и граф наблюдения

На паузе наведите указатель на имя переменной в исходнике, чтобы увидеть
локальное или глобальное значение текущего кадра. Стек находится на вкладке
**Stack and locals**. **Watch graph** раскрывает встроенные словари, списки,
кортежи и захваты замыканий. Общие ссылки и циклы ведут к одной карточке.
Дважды щёлкните по члену или нажмите **Pin member**, чтобы закрепить его.
**Pinned only** показывает закреплённые пути как корни. Оранжевые карточки
отмечают изменения с предыдущей остановки.

Наведите указатель на имя в `items[3]` или `config["name"]`, чтобы увидеть элемент, если он есть в снимке графа. Поддерживаются простые строковые ключи без экранирования и неотрицательные десятичные индексы. При наведении отладчик читает снимок и не вызывает `__getitem__`. Свойства,
произвольные атрибуты и выражения не вычисляются. Для просмотра элементов
встроенных коллекций используйте граф. Остальные объекты показаны без
раскрытия.

## Условная и сценарная отладка

1. Сохраните пример ниже как `observer_demo.py`. Откройте его и выберите **Debug Current Python Buffer…**.
2. На первой паузе откройте **Conditions and Script…**. В верхнем поле введите условие Lua
   `event.globals.counter ~= nil and event.globals.counter >= 10`.
3. Нажмите **Apply**, затем F5. Отладчик остановится перед `print(counter)`.

Используйте `event.locals` для локальной переменной и `event.globals` для глобальной.
Доступны операторы Lua, в том числе `==`, `~=`, `<`, `<=`, `>`, `>=`, `and` и `or`.
Проверка на `nil` нужна для сравнения по порядку, если переменная ещё не создана.

Для сценарной отладки очистите условие и перезапустите сеанс по Ctrl+F5.
На первой паузе вставьте один из Lua-обработчиков в нижнее поле того же диалога.
Нажмите **Apply** и F5. API одинаков в обоих отладчиках.
События `line`, `call` и `return` содержат `kind`, `file`, `line`, `name`
и ограниченные копии простых значений в `locals`, `args` и `globals`.
Python передаёт именованные позиционные и keyword-only аргументы.
`None`, коллекции и пользовательские объекты в снимок сценария не входят.
Возврат `true` запрашивает паузу на ближайшей отслеживаемой строке.
Журнал и карточки обновляются при паузе или завершении. Обмен с Python идёт
на каждом отслеживаемом событии, пока действует условие или сценарий.

```python
# EN: Sample program for a Lua observer. Two calls increase the global counter to 10.
# RU: Пример для обработчика Lua. Два вызова увеличивают глобальный счётчик до 10.

# EN: Expose named parameters that the observer can read from event.args.
# RU: Задаём именованные параметры, которые обработчик прочитает из event.args.
def tick(value, increment):
    # EN: Compute the next counter value.
    # RU: Вычисляем следующее значение счётчика.
    return value + increment


# EN: Start with a value below the stop threshold.
# RU: Начинаем со значения ниже порога остановки.
counter = 0
# EN: First matching call. It leaves the counter at 1.
# RU: Первый подходящий вызов оставляет счётчик равным 1.
counter = tick(counter, 1)
# EN: Second matching call. It leaves the counter at 10.
# RU: Второй подходящий вызов оставляет счётчик равным 10.
counter = tick(counter, 9)
# EN: The condition or sequence observer can stop before this output.
# RU: Условие или обработчик последовательности может остановить выполнение до вывода.
print(counter)
```

`log(text)` добавляет строку во вкладку **Observer log**. `watch(name, value)`
публикует карточку сценария во вкладке **Watch graph**. Повторный вызов с тем же
именем обновляет карточку. `watch(name, nil)` удаляет её. Поддерживаются числа,
строки и логические значения. У сценария может быть до 16 карточек, в пределах
общего размера графа. Имена ограничены 63 байтами.

### Условная точка на конкретной строке

На паузе щёлкните с **Shift** по номеру строки в исходнике окна отладки.
Введите условие Lua, например `event.locals.counter == 10`, и нажмите **Apply**.
Пустое условие совпадает при каждом проходе. Поле порога задаёт число **совпавших**
проходов до первого срабатывания. После порога точка срабатывает при каждом
следующем совпадении. **Log hits and continue** пишет файл, строку и счётчик в
**Observer log** и продолжает выполнение. Другие точки и команды шага по-прежнему
могут остановить программу.

Номер строки условной точки выделен охрой. Она имеет приоритет над обычной
точкой на той же строке. Повторный **Shift**-щелчок позволяет изменить правило,
а **Remove Rule** возвращает обычное поведение строки. В одном окне можно задать
до 32 правил. **Conditional Points…** показывает правила во всех файлах и позволяет
изменить или удалить их, даже если файл сейчас не открыт. Правила для именованных
файлов сохраняются для проекта или, вне проекта, для точки входа. Lua и Python
хранят их отдельно. Правила безымянного буфера действуют до закрытия окна.
Перезапуск и применение правок к сценарию или правилу сбрасывают счётчики.

### Файлы сценариев

В диалоге **Conditions and Script…** нажмите **Load Lua…**, чтобы загрузить файл
`.lua` в нижнее поле. **Save Lua As…** записывает текст нижнего поля в файл.
Верхнее поле условия в этот файл не входит.

Если вы изменили файл во внешней вкладке Geany, сохраните его и нажмите
**Reload File**. Загрузка и перечитывание обновляют черновик. Нажмите **Apply**,
чтобы передать новую версию отладчику. **Cancel** оставляет действующий сценарий
в силе. Поддерживаются файлы UTF-8 размером до 64 КиБ.

Последние применённые условие, текст сценария и путь к файлу сохраняются
для проекта Geany или, вне проекта, для файла точки входа. Lua и Python
хранят черновики отдельно. В новом сеансе откройте **Conditions and Script…**
и нажмите **Apply**, чтобы включить восстановленный черновик. **Reload File**
читает текущую версию файла, если он изменился после сохранения черновика.
Чтобы очистить сохранённое условие и сценарий, очистите оба поля и нажмите **Apply**.

### История отладки в Git

При открытом проекте Geany в **Conditions and Script…** есть две дополнительные кнопки.
**Commit Draft…** сохраняет условие и Lua-сценарий вместе, с вашим сообщением коммита.
**Git History…** показывает до 100 последних версий для текущего языка.
Выберите версию и нажмите **Load Draft**, чтобы заполнить оба поля. Черновик можно
изменить и снова сохранить через **Commit Draft…**. Новый коммит добавляется поверх
текущей истории. Чтобы включить черновик для отладки, отдельно нажмите **Apply**.

Git должен быть доступен в `PATH`. Для каждого проекта используется отдельный
локальный репозиторий в каталоге профиля Geany, под `insight/observer-history`.
История исходников проекта, его индекс и ветки в этом процессе не используются.

#### Журнал вызовов и аргументов

```lua
-- EN: Log every call to tick with its named arguments and the source location.
-- RU: Записывает каждый вызов tick с именованными аргументами и позицией в исходнике.
-- EN: Paste this observer into the lower field in Conditions and Script.
-- RU: Вставьте обработчик в нижнее поле окна Conditions and Script.

-- EN: Return the function that the debugger invokes for each event.
-- RU: Возвращаем функцию, которую отладчик вызывает для каждого события.
return function(event)
    -- EN: Ignore line events, returns and calls to other functions.
    -- RU: Пропускаем события строк, возвраты и вызовы других функций.
    if event.kind ~= "call" or event.name ~= "tick" then
        -- EN: Returning without a value lets the program run.
        -- RU: Возврат без значения позволяет программе продолжить работу.
        return
    -- EN: Finish filtering the event.
    -- RU: Завершаем проверку события.
    end

    -- EN: Read the two scalar arguments copied at function entry.
    -- RU: Читаем два простых аргумента, скопированных на входе в функцию.
    local value, increment = event.args.value, event.args.increment
    -- EN: Format the arguments and source location for Observer log.
    -- RU: Форматируем аргументы и позицию в исходнике для Observer log.
    local entry = string.format("%s:%d tick(value=%s, increment=%s)",
        event.file, event.line, tostring(value), tostring(increment))
    -- EN: Append a line to the log and leave execution running.
    -- RU: Добавляем строку в журнал и продолжаем выполнение.
    log(entry)
-- EN: End the observer callback.
-- RU: Завершаем обработчик событий.
end
```

Ожидаются две строки с аргументами `(0, 1)` и `(1, 9)`.

#### Журнал изменений переменной

```lua
-- EN: Log changes to the global counter, including its first observed numeric value.
-- RU: Записывает изменения глобального счётчика, включая первое замеченное число.
-- EN: The callback keeps the previous sample in a closure between line events.
-- RU: Обработчик хранит предыдущий отсчёт в замыкании между событиями строк.

-- EN: Start with no previous sample.
-- RU: Начинаем без предыдущего отсчёта.
local previous_value

-- EN: Inspect the counter whenever a source line is about to run.
-- RU: Проверяем счётчик перед выполнением строки исходника.
return function(event)
    -- EN: Ignore function entry and return events.
    -- RU: Пропускаем входы в функции и возвраты.
    if event.kind ~= "line" then
        -- EN: Keep the debugger running.
        -- RU: Продолжаем выполнение.
        return
    -- EN: Finish filtering the event.
    -- RU: Завершаем проверку события.
    end

    -- EN: Read the current global value from the snapshot.
    -- RU: Читаем текущее глобальное значение из снимка.
    local current_value = event.globals.counter
    -- EN: Ignore missing values and repeated numeric samples.
    -- RU: Пропускаем отсутствующие значения и повторные числовые отсчёты.
    if type(current_value) ~= "number" or current_value == previous_value then
        -- EN: There is no change to record.
        -- RU: Изменения для журнала нет.
        return
    -- EN: Finish filtering the event.
    -- RU: Завершаем проверку события.
    end

    -- EN: Record the transition, with nil marking the first sample.
    -- RU: Записываем переход. Значение nil отмечает первый отсчёт.
    log("counter: " .. tostring(previous_value) .. " -> " .. tostring(current_value))
    -- EN: Keep this sample for the next comparison.
    -- RU: Сохраняем отсчёт для следующего сравнения.
    previous_value = current_value
    -- EN: Publish the latest value as a named card in Watch graph.
    -- RU: Показываем последнее значение в именованной карточке Watch graph.
    watch("Observed counter", current_value)
-- EN: End the observer callback.
-- RU: Завершаем обработчик событий.
end
```

Ожидаются переходы `nil → 0 → 1 → 10`.

#### Остановка по последовательности событий

```lua
-- EN: Stop after tick(0, 1) is followed by tick(1, 9) and a line sees counter == 10.
-- RU: Останавливает после tick(0, 1), затем tick(1, 9) и строки, на которой counter равен 10.
-- EN: Each matching event advances a state stored in the callback closure.
-- RU: Каждое подходящее событие меняет состояние, сохранённое в замыкании.

-- EN: Begin by waiting for the first tick call.
-- RU: Сначала ждём первый вызов tick.
local phase = "first_call"

-- EN: Handle the call and line events that form the sequence.
-- RU: Обрабатываем события вызовов и строк, образующие последовательность.
return function(event)
    -- EN: Inspect arguments only on calls to tick.
    -- RU: Проверяем аргументы только на вызовах tick.
    if event.kind == "call" and event.name == "tick" then
        -- EN: Copy both argument values into readable local variables.
        -- RU: Сохраняем оба аргумента в локальных переменных с понятными именами.
        local value, increment = event.args.value, event.args.increment
        -- EN: A matching first call starts or restarts the sequence.
        -- RU: Подходящий первый вызов начинает последовательность заново.
        if value == 0 and increment == 1 then
            -- EN: The next tick call must match the second pair of arguments.
            -- RU: Следующий вызов tick должен совпасть со второй парой аргументов.
            phase = "second_call"
        -- EN: Advance only if the second pair follows the first one.
        -- RU: Продвигаемся дальше, только если вторая пара следует за первой.
        elseif phase == "second_call" and value == 1 and increment == 9 then
            -- EN: Wait for the assignment to become visible on a line event.
            -- RU: Ждём, пока результат присваивания станет виден на событии строки.
            phase = "final_value"
        -- EN: Handle any other argument pair.
        -- RU: Обрабатываем любую другую пару аргументов.
        else
            -- EN: An unexpected tick call cancels the partial match.
            -- RU: Неожиданный вызов tick отменяет частичное совпадение.
            phase = "first_call"
        -- EN: Finish matching the argument pair.
        -- RU: Завершаем проверку пары аргументов.
        end
    -- EN: Finish processing the call event.
    -- RU: Завершаем обработку события вызова.
    end

    -- EN: Stop only after both calls and the final observed value.
    -- RU: Останавливаемся только после обоих вызовов и появления итогового значения.
    if phase == "final_value" and event.kind == "line" and event.globals.counter == 10 then
        -- EN: Reset so that continuing waits for a new sequence.
        -- RU: Сбрасываем состояние, чтобы при продолжении ждать новую последовательность.
        phase = "first_call"
        -- EN: Explain the stop in Observer log.
        -- RU: Записываем причину остановки в Observer log.
        log("tick(0, 1), tick(1, 9), counter == 10")
        -- EN: Ask the debugger to pause on this source line.
        -- RU: Просим отладчик остановиться на этой строке исходника.
        return true
    -- EN: Finish checking whether the sequence is complete.
    -- RU: Завершаем проверку всей последовательности.
    end
-- EN: End the observer callback.
-- RU: Завершаем обработчик событий.
end
```

Ожидается пауза перед `print(counter)` и запись о совпадении в журнале.

### Вычисляемые карточки графа

В начале Lua-сценария вызовите `watch_eval(name, function(root) ... end)`.
Функция получает копию графа выбранного кадра при остановке и при смене кадра.
`root.members` содержит связи из корня, а каждый узел имеет `label`, `value`,
`path` и `members`. Имена связей совпадают с подписями графа, например
`local state`, `"devices"` и `[3]`. Общие ссылки и циклы сохраняются.
`root.limited` показывает, что снимок ограничен по размеру.
`value` содержит строку отображения. Для арифметики используйте `tonumber`.
Верните число, строку или логическое значение для карточки. `nil` убирает её.
Ошибка вычисления отображается в карточке. Вычисление работает только на паузе
и не меняет состояние отлаживаемой программы.

Пример работает с локальной таблицей Lua или локальным словарём Python
`state`, содержащим числовое поле `count`. Вставьте сценарий в нижнее поле
**Conditions and Script…**.

```lua
-- EN: Show twice the count from a nested value in the paused watch graph.
-- RU: Показывает удвоенный счётчик из вложенного значения в графе на паузе.

-- EN: Register a calculation under the name shown on the card.
-- RU: Регистрируем вычисление под именем, которое появится на карточке.
watch_eval("Double count", function(root)
    -- EN: Find the local variable and follow the link for the string key.
    -- RU: Находим локальную переменную и переходим по связи строкового ключа.
    local state = root.members["local state"]
    -- EN: A different frame or limited snapshot may omit the value.
    -- RU: В другом кадре или ограниченном снимке значения может не быть.
    local count = state and state.members['"count"']
    -- EN: Convert display text to a number before doing arithmetic.
    -- RU: Преобразуем текст отображения в число перед вычислением.
    local number = count and tonumber(count.value)
    -- EN: Publish the result, or hide the card when the value is unavailable.
    -- RU: Публикуем результат или скрываем карточку, если значения нет.
    return number and number * 2
-- EN: Finish the calculation registration.
-- RU: Завершаем регистрацию вычисления.
end)

-- EN: Supply the observer callback. This example only uses paused snapshots.
-- RU: Возвращаем обработчик событий. Пример использует только снимки на паузе.
return function()
-- EN: Leave execution control to the normal debugger commands.
-- RU: Оставляем управление выполнением обычным командам отладчика.
end
```

### Перенос точек после правок

При сохранении обычной, условной или журнальной точки отладчик запоминает
отпечаток строки и двух соседних строк. При следующем запуске он ищет эту
строку в снимке исходника. Если строка встречается несколько раз, соседние
строки должны однозначно указать нужное место. Точки на удалённых,
неоднозначных или недоступных строках остаются неактивными. Их число
показывается в строке состояния отладчика. Укажите нужную строку заново,
если точку требуется восстановить вручную. Старые записи без отпечатка
по-прежнему используют сохранённый номер до редактирования точки.
