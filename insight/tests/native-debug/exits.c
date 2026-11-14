/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Supply predictable normal, nonzero and signal exits for the production debugger GUI test.
 * RU: Даёт предсказуемые нормальные, ненулевые и сигнальные выходы для проверки интерфейса отладчика. */
#include <signal.h>
#include <stdlib.h>

/* EN: Select exit using argc/argv: "signal" raises SIGTERM, otherwise return the numeric argument or zero.
 * RU: Выбирает выход по argc/argv: "signal" вызывает SIGTERM, иначе возвращает числовой аргумент или ноль. */
int main(int argc, char **argv)
{
    if (argc > 1 && argv[1][0] == 's')
        raise(SIGTERM);
    return argc > 1 ? atoi(argv[1]) : 0;
}
