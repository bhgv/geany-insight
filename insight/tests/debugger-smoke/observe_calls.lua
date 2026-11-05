-- EN: Log the three worker calls and publish their running count.
-- RU: Записывает три вызова модуля и выводит их текущий счётчик.

-- EN: Preserve the count across events.
-- RU: Сохраняем счётчик между событиями.
local calls = 0

-- EN: Return the handler used by either debugger.
-- RU: Возвращаем обработчик для любого из отладчиков.
return function(event)
    -- EN: Process only entry events for tick.
    -- RU: Обрабатываем только входы в tick.
    if event.kind == "call" and event.name == "tick" then
        -- EN: Count and display the named arguments.
        -- RU: Считаем вызовы и выводим именованные аргументы.
        calls = calls + 1
        log(string.format("tick(%s, %s)", tostring(event.args.value), tostring(event.args.increment)))
        -- EN: Update the same graph card after each call.
        -- RU: Обновляем ту же карточку графа после каждого вызова.
        watch("Worker calls", calls)
    end
-- EN: Leave execution running after logging.
-- RU: Продолжаем выполнение после записи в журнал.
end
