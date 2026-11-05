-- EN: Log live Verilog point events and stop on the second matching hit.
-- RU: Записывает события живой отладки Verilog и останавливается на втором совпадении.

-- EN: Keep the first observed tick in the observer closure.
-- RU: Запоминаем первый замеченный такт в замыкании обработчика.
local first_tick

-- EN: Run this callback only for points marked Lua decides.
-- RU: Выполняем обработчик для точек с режимом Lua decides.
return function(event)
    -- EN: Ignore any event other than a live HDL point.
    -- RU: Пропускаем события, отличные от живой точки HDL.
    if event.kind ~= "hdl-point" then
        return false
    end

    -- EN: Read the matching-hit count and exact tick from the snapshot.
    -- RU: Читаем число совпадений и точный такт из снимка.
    local hits = event.locals.hits
    local tick = event.locals.tick
    -- EN: Publish a named value and append a readable console message.
    -- RU: Публикуем именованное значение и добавляем сообщение в консоль.
    watch("HDL hits", hits)
    log("HDL hit " .. hits .. " at tick " .. tick)
    -- EN: Store the first tick, then continue to the next matching event.
    -- RU: Сохраняем первый такт и продолжаем до следующего совпадения.
    if not first_tick then
        first_tick = tick
        return false
    end

    -- EN: Stop when the second match occurs at a different tick.
    -- RU: Останавливаемся, когда второе совпадение происходит на другом такте.
    return hits == 2 and tick ~= first_tick
end
