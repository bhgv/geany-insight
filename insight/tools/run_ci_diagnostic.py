#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Stream a CI command and expose bounded failure details before the surrounding job finishes.
# RU: Показывает вывод команды CI и ограниченную диагностику ошибки до завершения всей задачи.
import collections
import os
import subprocess
import sys


# EN: Run command from argv and preserve its status; publish a bounded GitHub annotation only on failure.
# RU: Запускает команду из argv с сохранением статуса; при ошибке публикует ограниченную аннотацию GitHub.
def main(argv):
    if not argv:
        print('Usage: run_ci_diagnostic.py COMMAND [ARGUMENTS...]', file=sys.stderr)
        return 2
    tail = collections.deque(maxlen=300)
    with subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, errors='replace') as process:
        for line in process.stdout:
            print(line, end='', flush=True)
            tail.append(line[-24000:])
        status = process.wait()
    if status and os.environ.get('GITHUB_ACTIONS') == 'true':
        details = ''.join(tail)[-24000:]
        escaped = details.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
        print(f'::error title=Native debugger diagnostics::{escaped}', flush=True)
    return status if status >= 0 else 128 - status


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
