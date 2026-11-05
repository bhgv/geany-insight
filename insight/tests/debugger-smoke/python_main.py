# EN: Exercise Python module calls and a caller graph with shared references.
# RU: Проверяет вызовы Python-модуля и граф вызывающего кадра с общими ссылками.

# EN: Load the worker from the same directory.
# RU: Загружаем модуль из той же папки.
from python_worker import tick


# EN: Run the example in a function so state is a local variable.
# RU: Выполняем пример в функции, чтобы state был локальной переменной.
def run():
    # EN: Create nested data, an alias and a cycle.
    # RU: Создаём вложенные данные, общую ссылку и цикл.
    state = {"count": 0, "device": {"status": 21}}
    state["alias"] = state["device"]
    state["self"] = state

    # EN: Produce 1, 10 and 12 in the worker.
    # RU: Получаем в модуле значения 1, 10 и 12.
    for increment in (1, 9, 2):
        # EN: Inspect this line by choosing the caller frame.
        # RU: Просмотрите строку, выбрав вызывающий кадр.
        state["count"] = tick(state["count"], increment)  # CALL_SITE

    # EN: Inspect the final graph before printing.
    # RU: Посмотрите итоговый граф перед выводом.
    print(state["count"])  # FINAL_STOP


# EN: Enter the example from the debugger's first frame.
# RU: Входим в пример из первого кадра отладчика.
run()
