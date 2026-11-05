-- EN: Provide a separate Lua source file with a repeatable breakpoint.
-- RU: Даёт отдельный файл Lua со строкой для повторяющейся точки останова.

-- EN: Name both parameters so the event observer can inspect them.
-- RU: Именуем параметры, чтобы обработчик событий мог их прочитать.
local function tick(value, increment)
    -- EN: Compute a local value that is visible at the next line.
    -- RU: Вычисляем локальное значение, видимое на следующей строке.
    local next_value = value + increment
    -- EN: Use this line for conditional stops and hit counters.
    -- RU: Используйте эту строку для условий и счётчиков попаданий.
    return next_value -- WORKER_STOP
end

-- EN: Export the worker function to the entry file.
-- RU: Возвращаем функцию для использования из точки входа.
return tick
