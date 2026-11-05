#!/usr/bin/env python3
"""Smoke-test the interactive PTY route without launching a model turn."""
import errno
import os
from pathlib import Path
import pty
import select
import subprocess
import time

root = Path(__file__).resolve().parents[1]
master, slave = pty.openpty()
p = subprocess.Popen([str(root / 'build-linux/aicage'), 'run', 'bash', '-lc',
                      'test -t 0 && test -t 1 && printf PTY_OK'],
                     stdin=slave, stdout=slave, stderr=slave, close_fds=True)
os.close(slave)
output = bytearray()
deadline = time.monotonic() + 45
try:
    while time.monotonic() < deadline:
        if select.select([master], [], [], .2)[0]:
            try:
                chunk = os.read(master, 16384)
            except OSError as error:
                if error.errno == errno.EIO:
                    break
                raise
            if not chunk:
                break
            output.extend(chunk)
        elif p.poll() is not None:
            break
    else:
        p.kill()
        raise TimeoutError('PTY smoke test timed out')
    assert p.wait(timeout=5) == 0 and b'PTY_OK' in output, output.decode(errors='replace')
    print('PASS interactive PTY: child stdin/stdout are terminals and output returns intact')
finally:
    os.close(master)
