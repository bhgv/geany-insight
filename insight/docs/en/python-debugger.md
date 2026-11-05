# Python debugger

Open a Python buffer and choose **Scripts → Debug Current Python Buffer…**.
The debugger runs a snapshot of the current buffer, including unsaved changes,
in a separate Python 3 process. Set `GEANY_INSIGHT_PYTHON` to select an
interpreter. The original file is used as `__file__`, and its directory is
available for imports. The program runs normally, so it can modify files and
have other side effects. The **Console** tab shows Python's text output from stdout and stderr. When the program calls `input()` or `sys.stdin.readline()`, **Console** opens and accepts a line with **Enter**, including an empty line. **Send EOF** ends text input for the current run.

## Source controls

Click a line number to toggle a breakpoint. **F5** continues, **F10** steps over,
**F11** steps into, and **Shift+F11** steps out. **Pause** requests a stop on the
next traced line. **Shift+F5** stops the child process, and **Ctrl+F5** restarts
the original snapshot. **Go to Source** opens the line when the editor buffer
still matches the snapshot. The current file is the default entry point. Use **Scripts → Debug Python File…** to choose another. With a project open, the debugger follows runtime calls into files under its base directory. **Breakpoints in File…** lets you choose a file and set breakpoints before execution reaches it. Breakpoints are stored per file and restored the next time you debug. Each language keeps separate settings for the Geany project or, outside a project, for the entry file. Breakpoints in unnamed buffers last for the current debug session. Saved line numbers are restored as entered. The entry and edited open project modules run from snapshots. An imported module must already exist on disk to be found by the normal import search. Python loads other modules normally. Restart keeps the captured snapshots and clears the console.

## Values and watch graph

While paused, hover over the base name in `items[3]` or `config["name"]` to inspect the item when it is present in the graph snapshot. This supports simple unescaped string keys and nonnegative decimal indices without calling `__getitem__`.

While paused, hover over a variable name in the source pane to see a local or
global value in the current frame. **Stack and locals** shows the call stack.
**Watch graph** expands built-in dictionaries, lists, tuples and function
closure cells. Shared objects and cycles reuse one card. Double-click a member
to pin it, or select it and choose **Pin member**. **Pinned only** displays
those paths as roots. Orange marks a change since the previous stop.

The debugger reads built-in values directly. It does not evaluate properties,
arbitrary attributes or expressions for a hover tooltip. Other objects are
shown as opaque values. Use the graph to inspect built-in collection members.

## Conditional and scripted debugging

1. Save the program below as `observer_demo.py`, open it and choose **Debug Current Python Buffer…**.
2. At the first pause, open **Conditions and Script…** and enter this Lua condition
   in the top field: `event.globals.counter ~= nil and event.globals.counter >= 10`.
3. Choose **Apply** and press F5. The debugger stops before `print(counter)`.

Use `event.locals` for a local variable and `event.globals` for a global.
Lua comparison and Boolean operators include `==`, `~=`, `<`, `<=`, `>`, `>=`, `and` and `or`.
Check for `nil` before an ordering comparison when the variable may be undefined.

To run an observer, clear the condition and restart with Ctrl+F5. At the first pause,
paste one of the Lua observers below into the lower field of the same dialog. Choose
**Apply** and press F5. Both debuggers use the same observer API. Events `line`,
`call` and `return` carry `kind`, `file`, `line`, `name` and bounded copies of
scalar values in `locals`, `args` and `globals`. Python supplies named positional
and keyword-only parameters. `None`, containers and application objects are omitted.
Returning `true` requests a stop at the next tracked line. The log and watch cards
refresh at stops and at exit. When a condition or observer is enabled, the debugger
exchanges a message with Python at each tracked event.

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

`log(text)` appends a line to **Observer log**. `watch(name, value)` publishes an
observer card in **Watch graph**. Calling it again with the same name updates the
card. `watch(name, nil)` removes it. Values can be numbers, strings or Booleans.
An observer can publish up to 16 cards within the graph's overall size limit.
Names can contain up to 63 bytes.

### Observer files

In **Conditions and Script…**, choose **Load Lua…** to load a `.lua` file into
the lower field. **Save Lua As…** writes the lower field to a file. The condition
in the upper field stays in the debugger dialog.

If you edit the file in a Geany tab, save it and choose **Reload File**. Loading
and reloading update the draft. Choose **Apply** to use the new version.
**Cancel** keeps the active observer in place. Files must be UTF-8 and at most
64 KiB.

The last applied condition, script text and file path are saved for the Geany
project or, outside a project, for the entry file. Lua and Python keep separate
drafts. In a new session, open **Conditions and Script…** and choose **Apply**
to activate the restored draft. **Reload File** reads the current version of
the file if it has changed since the draft was saved. To clear the saved
condition and script, empty both fields and choose **Apply**.

#### Log calls and arguments

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

Expect two entries with arguments `(0, 1)` and `(1, 9)`.

#### Log variable changes

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

Expect the transitions `nil → 0 → 1 → 10`.

#### Stop after a sequence of events

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

Expect a pause before `print(counter)` and a matching log entry.
