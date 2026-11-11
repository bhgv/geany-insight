/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Provide live locals and a structured global for the native debugger GUI scenario.
 * RU: Предоставляет живые локальные переменные и составной объект для сценария интерфейса отладчика. */
struct NativeChild
{
    int code;
};

struct NativeProbe
{
    int count;
    struct NativeChild nested;
    int values[260];
};

struct NativeProbe debug_sample = {7, {9}, {2, 3, 5}};
char debug_memory[64] = "GDB memory";

/* EN: Keep marker alive through two writes for data-watchpoint and assignment checks.
 * RU: Сохраняет marker на время двух записей для проверки точек по данным и присваивания. */
int main(void)
{
    /* EN: Keep addressable aggregate storage for automatic field-name discovery without reading its contents.
     * RU: Сохраняет адресуемый агрегат для автоматических имён полей без чтения его содержимого программой. */
    volatile struct NativeChild local_child;
    volatile int marker = 41;
    marker++;
    marker += 2;
    return marker == 44 ? 0 : 1;
}
