# Lua debugger

Open a Lua buffer and choose **Scripts → Debug Current Lua Buffer…**. The
debugger takes a snapshot including unsaved edits and stops at the first
executable line. Use the **?** in the upper-right corner to reopen this guide.

## Execution and breakpoints

| Control | Key | What it does |
| --- | --- | --- |
| Continue | F5 | Run to a breakpoint or to the end of the script |
| Step Over | F10 | Advance without entering a called function |
| Step Into | F11 | Stop at the next Lua line, including inside a call |
| Step Out | Shift+F11 | Continue until control returns to the caller |
| Pause | Pause | Request a stop at the next yieldable Lua line |
| Stop | Shift+F5 | End execution and retain the breakpoint list |
| Restart | Ctrl+F5 | Run the original snapshot from the beginning |
| Go to Source | | Jump to the paused line if the editor buffer still matches the snapshot |

Click a line number to toggle a breakpoint. You can also enter numbers
separated by spaces or commas. The left pane highlights the paused line.
**Stack and locals** shows the call stack and named locals. Execution runs in
short slices between GTK events. A native call must return before the debugger
can pause. The Lua memory budget still applies.

While paused, hover over a variable in the debugger's source pane. The tooltip shows the local, captured value, or a simple field such as `config.device.name`. Values come from the current frame snapshot. Field lookup reads the table directly without invoking metamethods.

## Debug a Lua project

The current Lua file is the default entry point. Use **Scripts → Debug Lua File…** to choose another. In an open project, `require("net.codec")` looks for `net/codec.lua` or `net/codec/init.lua` under the project base directory. On first load the debugger snapshots the module, including unsaved edits in an open tab. Steps and breakpoints work in the entry and loaded modules. Use **Breakpoints in File…** to set line breakpoints before a module is reached. Breakpoints are stored per file and restored the next time you debug. Each language keeps separate settings for the Geany project or, outside a project, for the entry file. Breakpoints in unnamed buffers last for the current debug session. Saved line numbers are restored as entered. **All Breakpoints…** lists the breakpoints across files and shows the total. Double-click or choose **Open Source** to jump to the line in the editor. Choose **Remove** to delete the selected breakpoint.

The debug VM loads text modules from the project. System package search and native Lua modules are unavailable in this VM. Restart keeps loaded snapshots. Start a new session to pick up edited module sources.

## Watch graph

Open **Watch graph** while paused. **Current frame** is the root. Follow the
arrows through local values, table entries, actual metatables and upvalues
captured by closures. For example, a table entry can point to a table also
reachable through a metatable and captured by a lambda. Those links lead to the
same card, and a self-reference draws an arrow back to its own card.

Double-click a member card or select it and choose **Pin member**. Turn on
**Pinned only** to display those members as independent roots. Pins resolve
their paths again at each stop. **Unavailable** counts paths outside the
current scope or snapshot. **Clear pins** removes all watches. Select a card
and hover over the canvas to see the full incoming and outgoing link labels.

Orange cards indicate a new value or a change to its value or direct links
since the previous source stop. Restart clears that comparison history.
The graph uses raw inspection. It does not execute closures or metamethods,
and it skips the implicit `_ENV` upvalue. A metatable link shows stored
references rather than the result of evaluating `__index`. The snapshot is
limited to 96 cards, 224 links, six expansion levels and 16 entries per table.

## Editor and project access

Read-only `editor.*` calls expose the captured text, selection, caret,
filename and lines. `project.*` reads the index while the original project
is active. Opening, saving and editing documents require the normal Lua script
runner. Restart reuses the original snapshot. Start a new debugger window to
pick up edits made after launch.

## Stop conditions and Lua observers

Open **Conditions and Script…** while paused. Enter `event.locals.counter == 10`
to stop before the next source line when the local reaches 10. For a global,
use `event.globals.counter == 10`. Clear the field to disable the condition. For an ordering comparison, guard against a missing value, for example `event.globals.counter ~= nil and event.globals.counter >= 10`.

The lower field accepts a Lua chunk returning an event handler. The handler receives
`line`, `call` and `return` events with `kind`, `file`, `line`, `name`,
`locals`, `args` and `globals`. The name may be empty. Value tables contain bounded
copies of scalar values. Returning `true` requests a stop at the next yieldable line.
**Observer log** refreshes at stops and at exit. A callback error disables the observer
and appears in the log. The condition and script belong to the current debugger window.
Restart resets the callback state and log.

### Run the examples

Save the first block as `observer-demo.lua` and choose **Scripts → Debug Current Lua Buffer…**.
At the first stop, open **Conditions and Script…**. To try a conditional stop, enter
`event.globals.counter == 10` in the top field, choose **Apply** and press F5.
The debugger stops before `local final_counter = counter`.

To try an observer, clear the top field, restart with Ctrl+F5 and paste **one**
observer from the examples into the lower field. Choose **Apply** and press F5.
Check **Observer log** at the next stop or at exit. Applying a new observer clears
the previous log and callback state.

#### Program to debug

```lua
-- EN: Sample program to debug. It calls tick twice and changes a global counter after each call.
-- RU: Пример отлаживаемой программы. Дважды вызывает tick и после каждого вызова меняет глобальный счётчик.

-- EN: Name the two parameters so the observer can read them from event.args.
-- RU: Именуем два параметра, чтобы обработчик мог прочитать их из event.args.
local function tick(value, increment)
    -- EN: Compute the next counter value using the supplied increment.
    -- RU: Вычисляем следующее значение счётчика с заданным приращением.
    return value + increment
-- EN: End the function definition.
-- RU: Завершаем определение функции.
end

-- EN: Initialize the global counter for the condition and the change logger.
-- RU: Задаём глобальный счётчик для условия и журнала изменений.
counter = 0
-- EN: First call. The arguments are 0 and 1, and the result is 1.
-- RU: Первый вызов. Аргументы равны 0 и 1, результат равен 1.
counter = tick(counter, 1)
-- EN: Second call. The arguments are 1 and 9, and the result is 10.
-- RU: Второй вызов. Аргументы равны 1 и 9, результат равен 10.
counter = tick(counter, 9)
-- EN: Provide an executable line where the debugger can see the final value.
-- RU: Оставляем исполняемую строку, на которой отладчик увидит итоговое значение.
local final_counter = counter
```

`log(text)` appends a line to **Observer log**. `watch(name, value)` publishes an
observer card in **Watch graph**. Calling it again with the same name updates the
card. `watch(name, nil)` removes it. Values can be numbers, strings or Booleans.
An observer can publish up to 16 cards within the graph's overall size limit.
Names can contain up to 63 bytes.

### Conditional breakpoints on a specific line

While paused, **Shift-click** a line number in the debugger's source pane.
Enter a Lua expression such as `event.locals.counter == 10` and choose **Apply**.
An empty expression matches every visit. The threshold counts **matching** visits.
The point acts on the threshold match and every later match. **Log hits and continue**
writes the file, line and hit count to **Observer log** and keeps running.
Other breakpoints and step commands can still stop execution.

An ochre line number marks a conditional point. It takes precedence over an
ordinary breakpoint on the same line. **Shift-click** it again to edit the rule,
or choose **Remove Rule** to restore normal behavior for the line. A debugger window
can hold up to 32 rules. Rules last until the window closes. Restarting or applying
changes to a rule or observer resets the counters.

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

### Debugger Git history

With a Geany project open, **Conditions and Script…** has two extra buttons.
**Commit Draft…** commits the condition and Lua script together with your message.
**Git History…** lists up to 100 recent versions for the current language.
Select a revision and choose **Load Draft** to populate both fields. Edit the
restored draft and choose **Commit Draft…** to add a new commit to the current
history. Choose **Apply** separately to activate the draft in the debugger.

Git must be on `PATH`. Each project uses a separate local repository under
`insight/observer-history` in the Geany profile. This workflow leaves the project's
source repository, index and branches untouched.

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

Expect a pause before `local final_counter = counter` and a matching log entry.
