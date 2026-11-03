#!/bin/sh
# Host side of the MH2LM USB CDC-ACM console (sweetusb=2, sweetvbus=1).
# Usage: tools/sweet-usbacm-log.sh [LOGFILE]
# Set SWEET_ACM_DEVICE=/dev/ttyACM<N> when more than one ACM device exists.
set -eu

out=${1:-}
dev=${SWEET_ACM_DEVICE:-}

if [ -z "$dev" ]; then
	printf '%s\n' 'Waiting for exactly one /dev/ttyACM* device...' >&2
	while :; do
		set -- /dev/ttyACM*
		if [ "$#" -eq 1 ] && [ -c "$1" ]; then
			dev=$1
			break
		fi
		sleep 1
	done
fi

if [ ! -c "$dev" ]; then
	printf 'CDC-ACM device is not a character device: %s\n' "$dev" >&2
	exit 1
fi

# Open read/write and assert modem-control lines explicitly. Relying on the
# implicit DTR policy of stty/cat is not reliable across Linux cdc_acm builds.
exec python3 - "$dev" "$out" <<'PY'
import fcntl
import os
import select
import struct
import sys
import termios
import tty

device, output = sys.argv[1:]
fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
log = None
bits = struct.pack('I', termios.TIOCM_DTR | termios.TIOCM_RTS)

try:
    tty.setraw(fd, when=termios.TCSANOW)
    fcntl.ioctl(fd, termios.TIOCMBIS, bits)
    if output:
        log = open(output, 'ab', buffering=0)
    print(f'Reading live MH2LM console from {device} (DTR/RTS asserted)', file=sys.stderr)

    while True:
        ready, _, _ = select.select([fd], [], [])
        if fd not in ready:
            continue
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            continue
        if not chunk:
            continue
        sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()
        if log is not None:
            log.write(chunk)
except KeyboardInterrupt:
    pass
finally:
    try:
        fcntl.ioctl(fd, termios.TIOCMBIC, bits)
    except OSError:
        pass
    if log is not None:
        log.close()
    os.close(fd)
PY
