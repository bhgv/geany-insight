#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Serve isolated SSH password/key authentication and forwarding for real native Windows OpenSSH tests.
# RU: Предоставляет изолированную SSH-авторизацию паролем/ключом и туннель для нативного OpenSSH Windows.
import base64
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time

import paramiko


class Server(paramiko.ServerInterface):
    # EN: Share accepted public keys and counters from fixture state; initialize per-connection requests.
    # RU: Использует ключи и счётчики фикстуры state; создаёт запросы отдельного соединения.
    def __init__(self, state):
        self.state = state
        self.commands = {}
        self.forwards = set()
        self.condition = threading.Condition()

    # EN: Authenticate fixture username/password without consulting actual OS accounts.
    # RU: Проверяет username/password фикстуры без обращения к учётным записям ОС.
    def check_auth_password(self, username, password):
        if username == 'developer' and password == 'isolated-test-password':
            self.state['passwords'] += 1
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED

    # EN: Authenticate username with a public key installed by the wizard into fixture state.
    # RU: Проверяет username по открытому key, установленному мастером в состояние фикстуры.
    def check_auth_publickey(self, username, key):
        if username == 'developer' and key.get_base64() in self.state['keys']:
            self.state['publickeys'] += 1
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED

    # EN: Advertise only the fixture's supported authentication methods for username.
    # RU: Объявляет только поддерживаемые фикстурой способы авторизации для username.
    def get_allowed_auths(self, username):
        return 'publickey,password'

    # EN: Allow only session channels identified by chanid/kind.
    # RU: Разрешает только каналы сеансов, определённые chanid/kind.
    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED if kind == 'session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    # EN: Accept only the debugger's loopback destination; origin identifies the local forwarding client.
    # RU: Принимает только loopback-адрес отладчика; origin задаёт локального клиента туннеля.
    def check_channel_direct_tcpip_request(self, chanid, origin, destination):
        if destination != ('127.0.0.1', 2345):
            return paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
        self.forwards.add(chanid)
        self.state['forwards'] += 1
        return paramiko.OPEN_SUCCEEDED

    # EN: Record command for channel and wake its waiting handler; no user-supplied shell is executed.
    # RU: Сохраняет command канала channel и уведомляет обработчик; пользовательская оболочка не запускается.
    def check_channel_exec_request(self, channel, command):
        with self.condition:
            self.commands[channel.get_id()] = command.decode('utf-8')
            self.condition.notify_all()
        return True


# EN: Serve one channel with explicitly simulated POSIX responses or real SSH-forwarded echo data.
# RU: Обслуживает channel с явно имитируемыми ответами POSIX либо реальными данными SSH-туннеля.
def channel_worker(channel, server):
    try:
        if channel.get_id() in server.forwards:
            while data := channel.recv(4096):
                channel.sendall(data)
            return
        with server.condition:
            assert server.condition.wait_for(lambda: channel.get_id() in server.commands, timeout=10)
            command = server.commands[channel.get_id()]
        if 'authorized_keys' in command:
            public = channel.makefile('rb').readline().decode().strip().split()
            assert len(public) == 2 and public[0] == 'ssh-ed25519'
            base64.b64decode(public[1], validate=True)
            server.state['keys'].add(public[1])
            channel.sendall(b'GEANY-SSH-KEY-INSTALLED\n')
        elif 'GEANY-GDB-SSH-READY' in command:
            channel.sendall(b'GEANY-GDB-SSH-READY\n')
            while channel.recv(4096):
                pass
        elif 'GEANY-SSH-READY' in command:
            channel.sendall(b'GEANY-SSH-READY\n')
        else:
            raise AssertionError('Unexpected fixture command')
        channel.send_exit_status(0)
    except (EOFError, OSError):
        pass
    finally:
        channel.close()


# EN: Serve client socket with host key and shared state until the client disconnects.
# RU: Обслуживает клиентский socket client с ключом host и состоянием state до отключения.
def connection_worker(client, host, state):
    transport = paramiko.Transport(client)
    try:
        transport.add_server_key(host)
        server = Server(state)
        transport.start_server(server=server)
        while transport.is_active():
            channel = transport.accept(0.2)
            if channel:
                threading.Thread(target=channel_worker, args=(channel, server), daemon=True).start()
    except (EOFError, OSError, paramiko.SSHException):
        pass
    finally:
        transport.close()


# EN: Run GUI/tunnel binaries in argv against isolated loopback SSH, never an external service.
# RU: Запускает GUI/туннель из argv против изолированного loopback SSH без внешних служб.
def main(argv):
    if len(argv) != 4:
        raise SystemExit('Usage: ssh_native_client_fixture.py CLIENT_BIN GUI_TEST TUNNEL_TEST')
    state = {'keys': set(), 'passwords': 0, 'publickeys': 0, 'forwards': 0}
    host = paramiko.RSAKey.generate(2048)
    with tempfile.TemporaryDirectory(prefix='insight SSH тест ') as temporary, socket.socket() as listener:
        root = Path(temporary)
        listener.bind(('127.0.0.1', 0))
        listener.listen()
        listener.settimeout(0.2)
        stop = threading.Event()

        # EN: Accept only loopback connections until the fixture's stop event is set.
        # RU: Принимает только loopback-соединения до установки события stop фикстуры.
        def accept():
            while not stop.is_set():
                try:
                    client, _ = listener.accept()
                    threading.Thread(target=connection_worker, args=(client, host, state), daemon=True).start()
                except socket.timeout:
                    pass

        thread = threading.Thread(target=accept)
        thread.start()
        try:
            port = listener.getsockname()[1]
            known = root / 'known hosts'
            known.write_text(f'[127.0.0.1]:{port} {host.get_name()} {host.get_base64()}\n', encoding='utf-8')
            config = root / 'ssh config'
            config.write_text(f'Host test-server\n HostName 127.0.0.1\n Port {port}\n'
                              f' UserKnownHostsFile "{known.as_posix()}"\n StrictHostKeyChecking yes\n'
                              ' PreferredAuthentications publickey,password\n LogLevel ERROR\n', encoding='utf-8')
            # EN/RU: Port override is supplied by the production form. / Порт задаётся производственной формой.
            environment = os.environ.copy()
            environment.update(INSIGHT_SSH_TEST_CLIENT_DIR=str(Path(argv[1]).resolve()),
                               INSIGHT_SSH_TEST_CONFIG=str(config), INSIGHT_SSH_TEST_PORT=str(port),
                               GSETTINGS_BACKEND='memory', NO_AT_BRIDGE='1')
            subprocess.run([str(Path(argv[2]).resolve())], env=environment, check=True, timeout=60)
            subprocess.run([str(Path(argv[3]).resolve())], env=environment, check=True, timeout=30)
            assert state['keys'] and state['passwords'] >= 2 and state['publickeys'] >= 2, state
            assert state['forwards'] == 1, state
            print('Native OpenSSH: Windows GTK wizard, password, installed key and debugger tunnel passed', state)
        finally:
            stop.set()
            thread.join(timeout=2)


if __name__ == '__main__':
    main(sys.argv)
