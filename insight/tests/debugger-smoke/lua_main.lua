-- EN: Exercise project calls, caller frames, nested values and shared references.
-- RU: Проверяет вызовы в проекте, кадры, вложенные значения и общие ссылки.

-- EN: Load the worker from the Geany project root.
-- RU: Загружаем модуль из корня проекта Geany.
local tick = require("lua_worker")

-- EN: Keep the state local so it is visible in the caller frame.
-- RU: Храним состояние локально, чтобы оно было видно в кадре вызывающей функции.
local state = { count = 0, device = { status = 21 } }
-- EN: Add a shared reference and a cycle for the watch graph.
-- RU: Добавляем общую ссылку и цикл для графа наблюдения.
state.alias = state.device
state.self = state

-- EN: Make three calls with distinct arguments and results.
-- RU: Делаем три вызова с различными аргументами и результатами.
for _, increment in ipairs({ 1, 9, 2 }) do
    -- EN: Inspect this caller line from the worker frame.
    -- RU: Просмотрите эту строку вызывающего кадра из модуля.
    state.count = tick(state.count, increment) -- CALL_SITE
end

-- EN: Stop here to inspect the completed state before the script returns.
-- RU: Остановитесь здесь, чтобы посмотреть итоговое состояние до возврата.
local final_count = state.count -- FINAL_STOP
-- EN: Return the result to the test runner.
-- RU: Возвращаем результат тестовому запуску.
return final_count
