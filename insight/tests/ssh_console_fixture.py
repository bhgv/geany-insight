#!/usr/bin/env python3
"""Run the production SSH panel against an isolated real loopback OpenSSH server."""
import os
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def free_port():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        return listener.getsockname()[1]


def main():
    if (os.name != 'posix' or not os.environ.get('DISPLAY')
            or not os.environ.get('GEANY_RUN_GUI_TESTS')):
        return 77
    import pwd
    sshd = shutil.which('sshd') or '/usr/sbin/sshd'
    ssh = shutil.which('ssh')
    keygen = shutil.which('ssh-keygen')
    if not Path(sshd).is_file() or not ssh or not keygen:
        return 77
    with tempfile.TemporaryDirectory(prefix='insight-ssh-loopback-') as temp:
        root = Path(temp)
        for name in ('client', 'host'):
            subprocess.run([keygen, '-q', '-t', 'ed25519', '-N', '', '-f', str(root / name)], check=True)
        port, refused = free_port(), free_port()
        while refused == port:
            refused = free_port()
        user = pwd.getpwuid(os.getuid()).pw_name
        authorized = root / 'authorized_keys'
        authorized.write_bytes((root / 'client.pub').read_bytes())
        config = root / 'sshd.conf'
        config.write_text(
            f'ListenAddress 127.0.0.1\nPort {port}\nHostKey {root / "host"}\n'
            f'PidFile {root / "sshd.pid"}\nAuthorizedKeysFile {authorized}\n'
            f'AllowUsers {user}\nStrictModes no\nUsePAM no\nPasswordAuthentication no\n'
            'KbdInteractiveAuthentication no\nPubkeyAuthentication yes\n'
            'PermitRootLogin prohibit-password\nPrintMotd no\nLogLevel ERROR\n')
        public = (root / 'host.pub').read_text().split()
        (root / 'known_hosts').write_text(f'[127.0.0.1]:{port} {public[0]} {public[1]}\n')
        client_config = root / 'ssh.conf'
        client_config.write_text(
            f'Host *\n UserKnownHostsFile {root / "known_hosts"}\n'
            ' StrictHostKeyChecking yes\n PreferredAuthentications publickey\n'
            ' PasswordAuthentication no\n LogLevel ERROR\n')
        bin_dir = root / 'bin'
        bin_dir.mkdir()
        wrapper = bin_dir / 'ssh'
        wrapper.write_text(f'#!/bin/sh\nexec {shlex.quote(ssh)} -F {shlex.quote(str(client_config))} "$@"\n')
        wrapper.chmod(0o700)
        with (root / 'server.log').open('w+') as log:
            server = subprocess.Popen([sshd, '-D', '-e', '-f', str(config)], stdout=log, stderr=log)
            try:
                for _ in range(100):
                    if server.poll() is not None:
                        raise RuntimeError('test sshd exited before listening')
                    try:
                        with socket.create_connection(('127.0.0.1', port), timeout=0.1):
                            break
                    except OSError:
                        time.sleep(0.05)
                else:
                    raise RuntimeError('test sshd did not start')
                probe = subprocess.run([ssh, '-F', str(client_config), '-i', str(root / 'client'),
                                        '-p', str(port), '-oBatchMode=yes', user + '@127.0.0.1',
                                        'printf SSH_PREFLIGHT_OK'], capture_output=True, timeout=8)
                if probe.returncode or probe.stdout != b'SSH_PREFLIGHT_OK':
                    raise RuntimeError('Loopback key authentication failed: ' + probe.stderr.decode(errors='replace'))
                env = os.environ.copy()
                # GUI must discover the wrapper; after its removal ssh is absent.
                prefix = Path(env['GEANY_INSIGHT_PREFIX'])
                env.update(PATH=str(bin_dir) + os.pathsep + str(prefix / 'bin'), LC_ALL='C',
                           INSIGHT_SSH_TEST_PORT=str(port), INSIGHT_SSH_REFUSED_PORT=str(refused),
                           INSIGHT_SSH_TEST_USER=user, INSIGHT_SSH_TEST_KEY=str(root / 'client'),
                           INSIGHT_SSH_TEST_WRAPPER=str(wrapper))
                result = subprocess.run([str(Path(sys.argv[1]).resolve())], env=env, timeout=45)
                if result.returncode:
                    raise RuntimeError(f'GUI SSH regression failed: {result.returncode}')
                return 0
            finally:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()
                log.seek(0)
                print('Isolated loopback sshd diagnostics:\n' + log.read(), file=sys.stderr)


if __name__ == '__main__':
    sys.exit(main())
