-- EN: Stop when tick(0, 1) is followed by tick(1, 9).
-- RU: Останавливает выполнение, когда за tick(0, 1) следует tick(1, 9).

-- EN: Remember whether the last call matched the first pair.
-- RU: Запоминаем, совпал ли предыдущий вызов с первой парой.
local first_seen = false

-- EN: Inspect call events without evaluating code in the target.
-- RU: Проверяем события вызовов без выполнения кода в отлаживаемой программе.
return function(event)
    -- EN: Ignore unrelated events.
    -- RU: Пропускаем остальные события.
    if event.kind ~= "call" or event.name ~= "tick" then
        return
    end

    -- EN: Compare the current arguments with both expected pairs.
    -- RU: Сравниваем текущие аргументы с двумя ожидаемыми парами.
    local value, increment = event.args.value, event.args.increment
    local matched = first_seen and value == 1 and increment == 9
    -- EN: Save the state for the next event.
    -- RU: Сохраняем состояние для следующего события.
    first_seen = value == 0 and increment == 1
    -- EN: Report the matching sequence and request a stop.
    -- RU: Записываем совпадение и запрашиваем остановку.
    if matched then
        log("Matched tick(0, 1) -> tick(1, 9)")
        return true
    end
-- EN: Finish the callback.
-- RU: Завершаем обработчик.
end
