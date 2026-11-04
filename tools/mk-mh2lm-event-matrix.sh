#!/bin/bash
# Build several temporary MH2LM fastboot-boot images from one CI kernel artifact.
# The profiles differ only in UniXNU boot arguments; every image shares the
# extracted kernel, the known 16K ADT, and the clean 32 MiB ramdisk baseline.
set -euo pipefail

if [ "$#" -ne 2 ] && [ "$#" -ne 3 ]; then
	printf 'usage: %s XNU_KERNEL_ARTIFACT LABEL [--key-irq-preflight|--key-irq-diag-matrix|--key-irq-pon-route-micro|--key-irq-upstream|--key-irq-mux|--usb-irq|--touch-irq|--irq-only]\n' "$0" >&2
	exit 2
fi

ARTIFACT=$1
LABEL=$2
KEY_IRQ_PROFILE=${3:-}
case "$KEY_IRQ_PROFILE" in
	'')
		;;
	--key-irq-preflight)
		;;
	--key-irq-diag-matrix)
		;;
	--key-irq-pon-route-micro)
		;;
	--key-irq-upstream)
		;;
	--key-irq-mux)
		;;
	--usb-irq)
		;;
	--touch-irq)
		;;
	--irq-only)
		;;
	*)
		printf 'unknown optional profile: %s\n' "$KEY_IRQ_PROFILE" >&2
		exit 2
		;;
esac
case "$LABEL" in
	*[!A-Za-z0-9._-]*|'')
		printf 'label may contain only A-Z, a-z, 0-9, dot, underscore, and dash\n' >&2
		exit 2
		;;
esac
if [ ! -f "$ARTIFACT" ]; then
	printf 'kernel artifact not found: %s\n' "$ARTIFACT" >&2
	exit 2
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORKSPACE=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
OUT="$WORKSPACE/out-bs"
MU="$WORKSPACE/ref/Mu-Silicium"
RAMDISK_BASE="$OUT/ramdisk-clean"
ADT="$OUT/adt-16k-3g.adt"
TOML_BASE="$OUT/unixnu.toml.orig"
KERNEL_BLOB="$OUT/kernel-32m.blob"

for required in "$OUT" "$MU" "$RAMDISK_BASE" "$ADT" "$TOML_BASE" "$KERNEL_BLOB"; do
	if [ ! -e "$required" ]; then
		printf 'required packaging input not found: %s\n' "$required" >&2
		exit 2
	fi
done

TMPDIR=$(mktemp -d "/tmp/mh2lm-event-matrix-${LABEL}.XXXXXX")
trap 'rm -rf -- "$TMPDIR"' EXIT

# The CircleCI artifact is expected to carry both the kernel and its manifest.
tar -xzf "$ARTIFACT" -C "$TMPDIR" ./kernel.development.mh2lm ./SHA256SUMS
KERNEL_SHA256=$(sha256sum "$TMPDIR/kernel.development.mh2lm" | awk '{print $1}')
grep -Fqx "$KERNEL_SHA256  kernel.development.mh2lm" "$TMPDIR/SHA256SUMS"

# Avoid making a matrix from an artifact that did not actually include the
# requested battery transition trace and the previously validated ACM driver.
grep -Fqa 'SWEET BATT UI:' "$TMPDIR/kernel.development.mh2lm"
grep -Fqa 'ACM endpoints configured' "$TMPDIR/kernel.development.mh2lm"
if [ -n "$KEY_IRQ_PROFILE" ]; then
	grep -Fqa 'keys-volume-up' "$TMPDIR/kernel.development.mh2lm"
fi
if [ "$KEY_IRQ_PROFILE" = '--key-irq-mux' ]; then
	grep -Fqa 'SWEET MUX: armed mode=' "$TMPDIR/kernel.development.mh2lm"
fi
if [ "$KEY_IRQ_PROFILE" = '--usb-irq' ]; then
	if ! grep -Fqa 'SWEET USB IRQ: software-pending probe hits=' "$TMPDIR/kernel.development.mh2lm"; then
		printf 'kernel artifact does not contain the DWC3 IRQ probe; check the CI buildtree\n' >&2
		exit 1
	fi
	grep -Fqa 'SWEET MUX: armed mode=' "$TMPDIR/kernel.development.mh2lm"
fi
if [ "$KEY_IRQ_PROFILE" = '--touch-irq' ]; then
	if ! grep -Fqa 'SWEET TOUCH IRQ: software-pending probe hits=' "$TMPDIR/kernel.development.mh2lm"; then
		printf 'kernel artifact does not contain the TouchComm TLMM IRQ probe; check the CI buildtree\n' >&2
		exit 1
	fi
	grep -Fqa 'SWEET USB IRQ: software-pending probe hits=' "$TMPDIR/kernel.development.mh2lm"
	grep -Fqa 'SWEET MUX: armed mode=' "$TMPDIR/kernel.development.mh2lm"
fi
if [ "$KEY_IRQ_PROFILE" = '--irq-only' ]; then
	for marker in 'SWEET KEYS IRQ: three routes active; IRQ-only' \
	    'SWEET TOUCH: TouchComm ready (sda=%u scl=%u), max %ux%u, IRQ-only' \
	    'SWEET USB IRQ: FAILED %s hits=%u empty=%u; gadget stopped'; do
		if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
			printf 'kernel artifact lacks fixed IRQ marker: %s\n' "$marker" >&2
			exit 1
		fi
	done
elif grep -Fqa 'SWEET KEYS IRQ: three routes active; IRQ-only' \
    "$TMPDIR/kernel.development.mh2lm"; then
	printf 'fixed IRQ kernel requires --irq-only packaging profile\n' >&2
	exit 1
fi

make_profile() {
	local name=$1
	local title=$2
	local extra=$3
	local kernel="$OUT/kernel.development.mh2lm-${LABEL}-${name}"
	local ramdisk="$OUT/ramdisk-16k-${LABEL}-${name}"
	local toml="$OUT/unixnu-16k-${LABEL}-${name}.toml"
	local image="$OUT/boot-16k-${LABEL}-${name}.img"
	local verify="$TMPDIR/${name}"
	local base='cpus=8 sweetfb_noconsole=1 sweetfb_hz=30'

	install -m 0644 "$TMPDIR/kernel.development.mh2lm" "$kernel"
	cp "$RAMDISK_BASE" "$ramdisk"
	sed "s| cpus=1 | |; s|^args = \"\(.*\)\"\$|args = \"\\1 $base $extra\"|; s|^title = \"LG G8X - XNU (Debug)\"|title = \"$title\"|" \
		"$TOML_BASE" > "$toml"
	grep -Fq "$extra" "$toml"

	mcopy -o -i "$ramdisk" "$toml" ::/EFI/UniXNU/unixnu.toml
	mcopy -o -i "$ramdisk" "$kernel" ::/EFI/UniXNU/Payloads/kernel.macho
	mcopy -o -i "$ramdisk" "$ADT" ::/EFI/UniXNU/Payloads/kernel.macho.adt

	mkdir -p "$verify"
	mcopy -o -i "$ramdisk" ::/EFI/UniXNU/unixnu.toml "$verify/unixnu.toml"
	mcopy -o -i "$ramdisk" ::/EFI/UniXNU/Payloads/kernel.macho "$verify/kernel.macho"
	mcopy -o -i "$ramdisk" ::/EFI/UniXNU/Payloads/kernel.macho.adt "$verify/kernel.macho.adt"
	cmp "$verify/unixnu.toml" "$toml"
	cmp "$verify/kernel.macho" "$kernel"
	cmp "$verify/kernel.macho.adt" "$ADT"

	(
		cd "$MU"
		python3 Resources/Scripts/mkbootimg.py --kernel "$KERNEL_BLOB" \
			--ramdisk "$ramdisk" --header_version 1 -o "$image"
	)
	file "$image"
	sha256sum "$image"
}

if [ "$KEY_IRQ_PROFILE" = '--irq-only' ]; then
	make_profile irq-only \
		"LG G8X - XNU fixed key, USB and touch IRQ (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetkeys=1"
	if grep -Fqa 'sweetloglevel' "$TMPDIR/kernel.development.mh2lm" && \
	    grep -Fqa '[DEBUG] SWEET USB' "$TMPDIR/kernel.development.mh2lm"; then
		make_profile irq-only-debug \
			"LG G8X - XNU fixed IRQ debug logs (${LABEL})" \
			"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetkeys=1 sweetloglevel=3"
		printf 'kernel SHA256: %s\ncreated normal and debug fixed IRQ profiles for %s\n' "$KERNEL_SHA256" "$LABEL"
	else
		printf 'kernel SHA256: %s\ncreated fixed IRQ profile for %s; artifact has no log-level support\n' "$KERNEL_SHA256" "$LABEL"
	fi
	exit 0
fi

# Keys remain in their normal enabled mode in every profile. USB is explicitly
# off in the non-USB profiles, because sweetusb=1 otherwise performs a probe.
make_profile baseline \
	"LG G8X - XNU stable baseline (${LABEL})" \
	"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0"
make_profile battery-ui \
	"LG G8X - XNU battery UI trace (${LABEL})" \
	"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetbattery=1 sweetbatt=1 sweetbattdiag=1 sweetbattirqdiag=1 sweetbattuidiag=1"
make_profile battery-ui-preserve-off \
	"LG G8X - XNU battery UI preserve-off (${LABEL})" \
	"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetbattery=1 sweetbatt=1 sweetbattdiag=1 sweetbattirqdiag=1 sweetbattuidiag=1 sweetfbpreserveoff=1"
make_profile usb \
	"LG G8X - XNU USB ACM (${LABEL})" \
	"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1"
make_profile usb-battery-ui \
	"LG G8X - XNU USB plus battery UI trace (${LABEL})" \
	"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetbattery=1 sweetbatt=1 sweetbattdiag=1 sweetbattirqdiag=1 sweetbattuidiag=1"
if [ "$KEY_IRQ_PROFILE" = '--key-irq-preflight' ]; then
	make_profile key-irq-preflight \
		"LG G8X - XNU key IRQ preflight (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=31"
fi
if [ "$KEY_IRQ_PROFILE" = '--key-irq-diag-matrix' ]; then
	make_profile key-irq-init \
		"LG G8X - XNU key IRQ helper init (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=1"
	make_profile key-irq-route-pon \
		"LG G8X - XNU key IRQ PON route (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=2"
	make_profile key-irq-route-volume-up \
		"LG G8X - XNU key IRQ GPIO6 route (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=4"
	make_profile key-irq-cfg-pon \
		"LG G8X - XNU key IRQ PON config (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=8"
	make_profile key-irq-cfg-volume-up \
		"LG G8X - XNU key IRQ GPIO6 config (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=16"
	make_profile key-irq-full \
		"LG G8X - XNU key IRQ full preflight (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=31"
fi
if [ "$KEY_IRQ_PROFILE" = '--key-irq-pon-route-micro' ]; then
	make_profile key-irq-pon-route-meta \
		"LG G8X - XNU PON route metadata (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=32"
	make_profile key-irq-pon-route-owner \
		"LG G8X - XNU PON route owner bitmap (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=64"
	make_profile key-irq-pon-route-map \
		"LG G8X - XNU PON route channel map (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=128"
	make_profile key-irq-pon-route-enable \
		"LG G8X - XNU PON route ACC_ENABLE (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=256"
	make_profile key-irq-pon-route-status \
		"LG G8X - XNU PON route IRQ_STATUS (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=512"
fi
if [ "$KEY_IRQ_PROFILE" = '--key-irq-upstream' ]; then
	make_profile key-irq-pdc \
		"LG G8X - XNU key IRQ PDC snapshot (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=1024"
	make_profile key-irq-gic \
		"LG G8X - XNU key IRQ GIC snapshot (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=2048"
	make_profile key-irq-upstream \
		"LG G8X - XNU key IRQ upstream snapshot (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=0 sweetkeys=1 sweetkeysirqdiag=3072"
fi
if [ "$KEY_IRQ_PROFILE" = '--key-irq-mux' ]; then
	# CDC-ACM is the established live console for key IRQ bring-up.
	make_profile key-irq-mux-soft \
		"LG G8X - XNU key IRQ mux software handoff (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetkeys=1 sweetkeysirq=1"
	make_profile key-irq-power \
		"LG G8X - XNU Power key IRQ (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetkeys=1 sweetkeysirq=2"
	make_profile key-irq-all \
		"LG G8X - XNU all key IRQs (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetkeys=1 sweetkeysirq=3"
fi
if [ "$KEY_IRQ_PROFILE" = '--usb-irq' ]; then
	# One kernel, staged GIC-only and real DWC3 IRQ tests. CDC-ACM remains
	# enabled in all profiles and persistent SWEETLOG covers USB failures.
	make_profile usb-irq-gic \
		"LG G8X - XNU USB GIC probe (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetusbirq=1"
	make_profile usb-irq \
		"LG G8X - XNU DWC3 IRQ (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetusbirq=2"
	make_profile usb-irq-keys \
		"LG G8X - XNU DWC3 and key IRQ (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetusbirq=2 sweetkeys=1 sweetkeysirq=3"
fi
if [ "$KEY_IRQ_PROFILE" = '--touch-irq' ]; then
	# The proven USB IRQ console stays available. Touch probe leaves GPIO122
	# masked; the real-touch stages separate key polling from key IRQs.
	make_profile touch-irq-gic \
		"LG G8X - XNU touch GIC probe (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetusbirq=2 sweetkeys=1 sweetkeysirq=3 sweettouchirq=1"
	make_profile touch-irq \
		"LG G8X - XNU touch IRQ (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetusbirq=2 sweetkeys=1 sweetkeysirq=0 sweettouchirq=2"
	make_profile touch-irq-all \
		"LG G8X - XNU touch, USB and key IRQ (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetusbirq=2 sweetkeys=1 sweetkeysirq=3 sweettouchirq=2"
fi

printf 'kernel SHA256: %s\n' "$KERNEL_SHA256"
printf 'created profiles for %s: baseline, battery-ui, battery-ui-preserve-off, usb, usb-battery-ui' "$LABEL"
if [ "$KEY_IRQ_PROFILE" = '--key-irq-preflight' ]; then
	printf ', key-irq-preflight'
fi
if [ "$KEY_IRQ_PROFILE" = '--key-irq-diag-matrix' ]; then
	printf ', key-irq-init, key-irq-route-pon, key-irq-route-volume-up, key-irq-cfg-pon, key-irq-cfg-volume-up, key-irq-full'
fi
if [ "$KEY_IRQ_PROFILE" = '--key-irq-pon-route-micro' ]; then
	printf ', key-irq-pon-route-meta, key-irq-pon-route-owner, key-irq-pon-route-map, key-irq-pon-route-enable, key-irq-pon-route-status'
fi
if [ "$KEY_IRQ_PROFILE" = '--key-irq-upstream' ]; then
	printf ', key-irq-pdc, key-irq-gic, key-irq-upstream'
fi
if [ "$KEY_IRQ_PROFILE" = '--key-irq-mux' ]; then
	printf ', key-irq-mux-soft, key-irq-power, key-irq-all'
fi
if [ "$KEY_IRQ_PROFILE" = '--usb-irq' ]; then
	printf ', usb-irq-gic, usb-irq, usb-irq-keys'
fi
if [ "$KEY_IRQ_PROFILE" = '--touch-irq' ]; then
	printf ', touch-irq-gic, touch-irq, touch-irq-all'
fi
printf '\n'
