#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Run the production debugger GUI with isolated OpenSSH, strict host keys and a real gdbserver.
# RU: Запускает настоящий интерфейс отладчика с изолированным OpenSSH, проверкой ключа и gdbserver.
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time


# EN: Select a currently free loopback port; listeners report any later bind race as a test failure.
# RU: Выбирает свободный loopback-порт; последующая гонка привязки приводит к ошибке теста.
def free_port():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        return listener.getsockname()[1]


# EN: Stop child and reap it, escalating only after a bounded graceful shutdown.
# RU: Останавливает child и забирает его статус; принудительное завершение — только после ожидания.
def stop(child):
    child.terminate()
    try:
        child.wait(timeout=5)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait()


# EN: Configure real loopback services and pass their settings to the editor test binary from argv.
# RU: Настраивает настоящие loopback-службы и передаёт параметры тестовому редактору из argv.
def main():
    if not sys.platform.startswith('linux') or not os.environ.get('DISPLAY') or not os.environ.get('GEANY_RUN_GUI_TESTS'):
        return 77
    import pwd
    sshd = shutil.which('sshd') or '/usr/sbin/sshd'
    programs = {name: shutil.which(name) for name in ('ssh', 'ssh-keygen', 'gdbserver', 'c++')}
    if not Path(sshd).is_file() or not all(programs.values()):
        return 77
    with tempfile.TemporaryDirectory(prefix='insight-remote-gdb-') as temporary:
        root = Path(temporary)
        children = []
        try:
            for name in ('client', 'host'):
                subprocess.run([programs['ssh-keygen'], '-q', '-t', 'ed25519', '-N', '',
                                '-f', str(root / name)], check=True)
            ssh_port, server_port, refused_port = [free_port() for _ in range(3)]
            assert len({ssh_port, server_port, refused_port}) == 3
            user = pwd.getpwuid(os.getuid()).pw_name
            (root / 'authorized_keys').write_bytes((root / 'client.pub').read_bytes())
            config = root / 'sshd.conf'
            config.write_text(
                f'ListenAddress 127.0.0.1\nPort {ssh_port}\nHostKey {root / "host"}\n'
                f'PidFile {root / "sshd.pid"}\nAuthorizedKeysFile {root / "authorized_keys"}\n'
                f'AllowUsers {user}\nStrictModes no\nUsePAM no\nPasswordAuthentication no\n'
                'KbdInteractiveAuthentication no\nPubkeyAuthentication yes\n'
                'PermitRootLogin prohibit-password\nAllowTcpForwarding yes\nPrintMotd no\nLogLevel ERROR\n')
            public = (root / 'host.pub').read_text().split()
            (root / 'known_hosts').write_text(f'[127.0.0.1]:{ssh_port} {public[0]} {public[1]}\n')
            client_config = root / 'client.conf'
            client_config.write_text(
                f'Host *\n UserKnownHostsFile {root / "known_hosts"}\n'
                ' StrictHostKeyChecking yes\n PreferredAuthentications publickey\n'
                ' PasswordAuthentication no\n BatchMode yes\n LogLevel ERROR\n'
                f' ControlMaster auto\n ControlPath {root / "mux"}\n RemoteCommand false\n')
            source_root = root / 'local source'
            source_root.mkdir()
            source = source_root / 'remote-main.cpp'
            # EN: A bounded live loop permits Pause; append PIDs to reap detached inferiors too.
            # RU: Ограниченный цикл позволяет проверить Pause; PID нужны для очистки отсоединённых программ.
            pid_file = root / 'inferior-pids'
            source.write_text(
                '#include <unistd.h>\n#include <stdio.h>\n#include <sys/prctl.h>\n'
                'template<typename T> __attribute__((noinline)) int profile_twice(T x) { return (int)x * 2; }\n'
                'int main(void) {\n volatile int marker = 41;\n alarm(30);\n'
                ' prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);\n'
                f' FILE *pids = fopen("{pid_file}", "a");\n'
                ' if (pids) { fprintf(pids, "%d\\n", (int)getpid()); fclose(pids); }\n'
                ' while (marker == 41) usleep(10000);\n return profile_twice(marker) + profile_twice(1.0);\n}\n')
            executable = root / 'remote app'
            subprocess.run([programs['c++'], '-g', '-O0', f'-fdebug-prefix-map={source_root}=/insight-remote-build',
                            str(source), '-o', str(executable)], check=True)
            with (root / 'ssh.log').open('w+') as ssh_log, (root / 'gdbserver.log').open('w+') as server_log:
                children.append(subprocess.Popen([sshd, '-D', '-e', '-f', str(config)], stdout=ssh_log, stderr=ssh_log))
                for mode in (2, 1):
                    attach_process = None
                    if mode == 2:
                        # EN: An independently started inferior permits a real PID attach under Linux Yama.
                        # RU: Независимо запущенный процесс позволяет проверить подключение по PID при Linux Yama.
                        attach_process = subprocess.Popen([str(executable)])
                        children.append(attach_process)
                        for _ in range(100):
                            if pid_file.exists() and str(attach_process.pid) in pid_file.read_text().splitlines():
                                break
                            time.sleep(0.02)
                        else:
                            raise RuntimeError('Attach inferior did not initialize')
                    arguments = [programs['gdbserver']]
                    if mode == 2:
                        arguments.append('--multi')
                    arguments.append(f'127.0.0.1:{server_port}')
                    if mode == 1:
                        arguments.append(str(executable))
                    server_log.seek(0)
                    server_log.truncate()
                    children.append(subprocess.Popen(arguments, stdout=server_log, stderr=server_log))
                    for _ in range(200):
                        if any(child.poll() is not None for child in children):
                            raise RuntimeError('Loopback service exited before readiness')
                        server_log.seek(0)
                        if 'Listening on port' in server_log.read():
                            try:
                                with socket.create_connection(('127.0.0.1', ssh_port), timeout=0.1):
                                    break
                            except OSError:
                                pass
                        time.sleep(0.05)
                    else:
                        raise RuntimeError('Loopback services did not become ready')
                    env = os.environ.copy()
                    env.update(LC_ALL='C.UTF-8', INSIGHT_GDB_REMOTE_GUI='1', INSIGHT_GDB_REMOTE_MODE=str(mode),
                               INSIGHT_GDB_ATTACH_PID=str(attach_process.pid if attach_process else 0), INSIGHT_GDB_SSH_HOST='127.0.0.1',
                               INSIGHT_GDB_SSH_PORT=str(ssh_port), INSIGHT_GDB_SERVER_PORT=str(server_port),
                               INSIGHT_GDB_REFUSED_PORT=str(refused_port), INSIGHT_GDB_SSH_USER=user,
                               INSIGHT_GDB_SSH_KEY=str(root / 'client'), INSIGHT_GDB_SSH_CONFIG=str(client_config),
                               INSIGHT_GDB_REMOTE_EXECUTABLE=str(executable), INSIGHT_GDB_REMOTE_SOURCE='/insight-remote-build',
                               INSIGHT_GDB_LOCAL_SOURCE=str(source_root))
                    try:
                        subprocess.run([str(Path(sys.argv[1]).resolve()), str(executable),
                                        str(source), str(root / f'profile-{mode}')], env=env, check=True, timeout=150)
                    finally:
                        for label, log in [('SSH', ssh_log), ('gdbserver', server_log)]:
                            log.flush()
                            log.seek(0)
                            print(f'{label} fixture diagnostics:\n{log.read()}', file=sys.stderr)
                    stop(children.pop())
                    if attach_process:
                        assert attach_process.poll() is None, 'Detach must leave the attached program alive'
                        stop(children.pop())
            assert not (root / 'mux').exists(), 'Debugger must not reuse or leave an SSH control master'
            return 0
        finally:
            for child in reversed(children):
                stop(child)
            pid_file = root / 'inferior-pids'
            if pid_file.exists():
                for pid in pid_file.read_text().splitlines():
                    try:
                        os.kill(int(pid), signal.SIGTERM)
                    except ProcessLookupError:
                        pass


if __name__ == '__main__':
    sys.exit(main())
