-- EN: Publish a computed card from the selected frame's nested graph.
-- RU: Публикует вычисляемую карточку из вложенных данных выбранного кадра.

-- EN: Register a calculation before returning the event handler.
-- RU: Регистрируем вычисление до возврата обработчика событий.
watch_eval("Double device status", function(root)
    -- EN: Find the local state and traverse its string-key links.
    -- RU: Находим локальную state и переходим по связям строковых ключей.
    local state = root.members["local state"]
    local device = state and state.members['"device"']
    local status = device and device.members['"status"']
    -- EN: The graph stores display text, so convert it before multiplication.
    -- RU: Граф хранит текст отображения. Преобразуем его перед умножением.
    local number = status and tonumber(status.value)
    -- EN: Return 42 for the fixture or nil for a frame without state.
    -- RU: Возвращаем 42 для примера или nil для кадра без state.
    return number and number * 2
end)

-- EN: Use the normal debugger commands for execution control.
-- RU: Оставляем управление выполнением обычным командам отладчика.
return function()
end
