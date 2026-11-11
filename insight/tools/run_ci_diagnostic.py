#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Stream a CI command and expose bounded failure details before the surrounding job finishes.
# RU: Показывает вывод команды CI и ограниченную диагностику ошибки до завершения всей задачи.
import collections
import os
from pathlib import Path
import subprocess
import sys


# EN: Run argv and preserve its status; optionally save the bounded tail to INSIGHT_CI_DIAGNOSTIC_FILE and annotate failures.
# RU: Запускает argv с сохранением статуса; по запросу пишет ограниченный хвост в INSIGHT_CI_DIAGNOSTIC_FILE и аннотирует ошибки.
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
    details = ''.join(tail)[-24000:]
    destination = os.environ.get('INSIGHT_CI_DIAGNOSTIC_FILE')
    if destination:
        Path(destination).write_text(details, encoding='utf-8')
    if status and os.environ.get('GITHUB_ACTIONS') == 'true':
        escaped = details.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
        print(f'::error title=Native debugger diagnostics::{escaped}', flush=True)
    return status if status >= 0 else 128 - status


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
