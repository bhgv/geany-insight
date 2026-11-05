# EN: Provide a separate Python source file with a repeatable breakpoint.
# RU: Даёт отдельный файл Python со строкой для повторяющейся точки останова.


# EN: Expose arguments to the shared Lua observer.
# RU: Делаем аргументы доступными общему Lua-обработчику.
def tick(value, increment):
    # EN: Compute a local value that is visible at the next line.
    # RU: Вычисляем локальное значение, видимое на следующей строке.
    next_value = value + increment
    # EN: Use this line for conditional stops and hit counters.
    # RU: Используйте эту строку для условий и счётчиков попаданий.
    return next_value  # WORKER_STOP
