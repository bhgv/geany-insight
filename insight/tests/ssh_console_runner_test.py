#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Verify real SSH exit codes and console retention without connecting to any server.
# RU: Проверяет настоящие коды SSH и удержание консоли без подключения к серверу.
import os
from pathlib import Path
import subprocess
import sys


# EN: Run helper/client from argv; check success, failure and explicit close versus waiting for Enter.
# RU: Запускает helper/client из argv; проверяет успех, ошибку, автозакрытие и ожидание Enter.
def main(argv):
    if len(argv) != 3:
        raise SystemExit('Usage: ssh_console_runner_test.py HELPER SSH')
    helper, client = (str(Path(value).resolve()) for value in argv[1:])
    environment = os.environ.copy()
    environment.update(SSH_ASKPASS='nonexistent-test-helper', SSH_ASKPASS_REQUIRE='force')
    for arguments, expected in [('-V', 0), ('-Z', 255)]:
        result = subprocess.run([helper, client, arguments, 'close'], env=environment,
                                capture_output=True, timeout=10)
        assert result.returncode == expected, (result.returncode, result.stderr)
        assert f'code / Код завершения SSH: {expected}'.encode() in result.stdout, result.stdout
        assert b'Press Enter' not in result.stdout
    with subprocess.Popen([helper, client, '-Z', 'keep'], env=environment,
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            pass
        else:
            raise AssertionError('Failure console closed without Enter')
        process.stdin.write(b'\n')
        process.stdin.flush()
        output, errors = process.communicate(timeout=10)
        assert process.returncode == 255, (process.returncode, errors)
        assert b'Press Enter' in output, output
        assert 'Код завершения SSH: 255'.encode() in output, output
    print('Real SSH console: success/failure exit codes, automatic close and retained failure passed')


if __name__ == '__main__':
    main(sys.argv)
