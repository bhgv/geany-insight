#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Verify the production askpass reuses passwords and migrates old key passphrases without a GUI.
# RU: Проверяет повторное использование паролей и перенос прежних паролей ключа настоящим askpass без GUI.
import configparser
import hashlib
import os
import ntpath
from pathlib import Path
import subprocess
import sys
import tempfile


# EN: Run helper with prompt against private vault/session, asserting a reused answer without display access.
# RU: Запускает helper с prompt и закрытыми vault/session, проверяя ответ без доступа к дисплею.
def answer(helper, prompt, vault, session, account, identity, expected):
    session.mkdir(mode=0o700)
    environment = os.environ.copy()
    environment.pop('DISPLAY', None)
    environment.update(GEANY_INSIGHT_SSH_CONFIG=str(vault), GEANY_INSIGHT_SSH_TARGET=account,
                       GEANY_INSIGHT_SSH_AUTH_SESSION=str(session), GEANY_INSIGHT_SSH_SAVE_PASSWORD='1',
                       GEANY_INSIGHT_SSH_KEY=str(identity))
    result = subprocess.run([str(helper), prompt], env=environment, text=True, encoding='utf-8',
                            capture_output=True, timeout=5, check=True)
    assert result.stdout.rstrip('\n') == expected


# EN: Validate helper from argv in disposable private storage; no actual user accounts or SSH endpoints are used.
# RU: Проверяет helper из argv во временном закрытом каталоге без настоящих учётных записей и SSH-серверов.
def main(argv):
    if len(argv) != 2:
        raise SystemExit('Usage: ssh_auth_helper_test.py ASKPASS_EXECUTABLE')
    helper = Path(argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='insight askpass тест-') as temporary:
        root = Path(temporary)
        identity = root / 'example-key'
        account = hashlib.sha256(b'test-host:22:tester').hexdigest()
        prompt = f"Enter passphrase for key '{identity}':"
        legacy = hashlib.sha256(prompt.encode()).hexdigest()
        canonical = str(identity)
        if os.name == 'nt' or sys.platform.startswith(('win', 'mingw')):
            canonical = ntpath.normpath(canonical).replace("/", "\\")
        stable = hashlib.sha256(('private-key:' + canonical).encode()).hexdigest()
        vault = root / 'credentials.conf'
        vault.write_text(f'[insight-ssh-password-{account}]\npassword=fixture-пароль\n\n'
                         f'[insight-ssh-password-{legacy}]\npassword=fixture-passphrase\n', encoding='utf-8')
        vault.chmod(0o600)
        for number in range(2):
            answer(helper, "tester@test-host's password:", vault, root / f'account-{number}',
                   account, identity, 'fixture-пароль')
        for number in range(2):
            answer(helper, prompt, vault, root / f'key-{number}', account, identity, 'fixture-passphrase')
        saved = configparser.ConfigParser()
        saved.read(vault, encoding="utf-8")
        assert saved.has_section(f'insight-ssh-password-{stable}'), (sys.platform, str(identity), canonical, saved.sections())
        assert saved[f'insight-ssh-password-{stable}']['password'] == 'fixture-passphrase'
        assert not saved.has_section(f'insight-ssh-password-{legacy}')
    print('Production askpass: repeat account/key reuse and legacy passphrase migration passed')


if __name__ == '__main__':
    main(sys.argv)
