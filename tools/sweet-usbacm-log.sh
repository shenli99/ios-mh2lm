#!/bin/sh
# Host side of the MH2LM USB CDC-ACM console (sweetusb=2, sweetvbus=1).
# Usage: tools/sweet-usbacm-log.sh [LOGFILE]
# Set SWEET_ACM_DEVICE=/dev/ttyACM<N> when more than one ACM device exists.
set -eu

out=${1:-}
dev=${SWEET_ACM_DEVICE:-}

# Stay alive across the USB disconnect/reconnect caused by fastboot boot.
# With SWEET_ACM_DEVICE set, wait for that exact node rather than requiring it
# to exist before the listener starts.
exec python3 - "$dev" "$out" <<'PY'
import fcntl
import glob
import os
import select
import stat
import struct
import sys
import termios
import time
import tty

requested, output = sys.argv[1:]
bits = struct.pack('I', termios.TIOCM_DTR | termios.TIOCM_RTS)
log = open(output, 'ab', buffering=0) if output else None
last_wait_state = None

def character_devices(paths):
    devices = []
    for path in paths:
        try:
            if stat.S_ISCHR(os.stat(path).st_mode):
                devices.append(path)
        except FileNotFoundError:
            pass
    return devices

try:
    while True:
        if requested:
            candidates = character_devices([requested])
        else:
            candidates = character_devices(glob.glob('/dev/ttyACM*'))

        if len(candidates) != 1:
            if requested:
                wait_state = f'Waiting for {requested} to appear...'
            elif not candidates:
                wait_state = 'Waiting for one /dev/ttyACM* device...'
            else:
                wait_state = ('More than one /dev/ttyACM* device is present; '
                              'set SWEET_ACM_DEVICE=/dev/ttyACM<N>.')
            if wait_state != last_wait_state:
                print(wait_state, file=sys.stderr)
                last_wait_state = wait_state
            time.sleep(1)
            continue

        device = candidates[0]
        fd = None
        try:
            fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            tty.setraw(fd, when=termios.TCSANOW)
            fcntl.ioctl(fd, termios.TIOCMBIS, bits)
            print(f'Reading live MH2LM console from {device} (DTR/RTS asserted)', file=sys.stderr)
            last_wait_state = None

            while True:
                ready, _, _ = select.select([fd], [], [], 1)
                if fd not in ready:
                    continue
                try:
                    chunk = os.read(fd, 4096)
                except BlockingIOError:
                    continue
                if not chunk:
                    raise OSError('CDC-ACM device disconnected')
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
                if log is not None:
                    log.write(chunk)
        except OSError as exc:
            print(f'CDC-ACM read session ended: {exc}', file=sys.stderr)
            time.sleep(1)
        finally:
            if fd is not None:
                try:
                    fcntl.ioctl(fd, termios.TIOCMBIC, bits)
                except OSError:
                    pass
                os.close(fd)
except KeyboardInterrupt:
    pass
finally:
    if log is not None:
        log.close()
PY
