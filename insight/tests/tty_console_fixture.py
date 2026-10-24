#!/usr/bin/env python3
"""Exercise the real serial panel with a local pseudo-terminal, no hardware."""
import os
import subprocess
import sys
import threading


def main():
    if os.name != 'posix' or not os.environ.get('DISPLAY'):
        return 77
    import pty
    import select
    master, slave = pty.openpty()
    received = bytearray()
    stop = threading.Event()

    def device():
        while not stop.is_set():
            if select.select([master], [], [], 0.05)[0]:
                received.extend(os.read(master, 8192))
                if b'PING\r' in received:
                    os.write(master, b'\r\n\x1b[32mGREEN\x1b[0m\r\nTTY_READY\r\n')
                    return

    worker = threading.Thread(target=device, daemon=True)
    worker.start()
    try:
        env = dict(os.environ, INSIGHT_TTY_DEVICE=os.ttyname(slave))
        result = subprocess.run(sys.argv[1:], env=env, timeout=25)
        if result.returncode:
            return result.returncode
        assert b'PING\r' in received, received
        return 0
    finally:
        stop.set()
        worker.join(timeout=1)
        os.close(slave)
        os.close(master)


if __name__ == '__main__':
    raise SystemExit(main())
