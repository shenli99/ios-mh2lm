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

# Opening the TTY asserts DTR through cdc_acm. The device sends raw console
# bytes, so disable terminal transformations before reading it.
stty -F "$dev" 115200 raw -echo -icrnl -onlcr
printf 'Reading live MH2LM console from %s\n' "$dev" >&2

if [ -n "$out" ]; then
	cat "$dev" | tee -a "$out"
else
	cat "$dev"
fi
