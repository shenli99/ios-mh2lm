#!/bin/bash
# Build several temporary MH2LM fastboot-boot images from one CI kernel artifact.
# Profiles share the extracted kernel and clean 32 MiB ramdisk baseline.
# Panel identity uses a metadata-only ADT; hardware lifecycle profiles also
# declare NoC/pins/touch resources and enable their required runtime modes.
set -euo pipefail

if [ "$#" -ne 2 ] && [ "$#" -ne 3 ]; then
	printf 'usage: %s XNU_KERNEL_ARTIFACT LABEL [--key-irq-preflight|--key-irq-diag-matrix|--key-irq-pon-route-micro|--key-irq-upstream|--key-irq-mux|--usb-irq|--touch-irq|--irq-only|--battery-irq|--battery-charge-check|--battery-transport|--battery-runtime|--battery-adc5-conversion|--battery-adc5-consumer|--battery-evidence|--battery-dependencies|--battery-basic-charge|--board-startup|--framebuffer-f0|--framebuffer-contract|--framebuffer-proof|--framebuffer-service|--framebuffer-state|--framebuffer-power|--framebuffer-irq-preflight|--framebuffer-vsync|--framebuffer-scanout|--framebuffer-handoff|--panel-resources|--panel-lifecycle]\n' "$0" >&2
	exit 2
fi

ARTIFACT=$1
LABEL=$2
KEY_IRQ_PROFILE=${3:-}
DEPENDENCY_MATRIX=0
FRAMEBUFFER_STATE=0
FRAMEBUFFER_POWER=0
FRAMEBUFFER_IRQ_PREFLIGHT=0
FRAMEBUFFER_VSYNC=0
FRAMEBUFFER_SCANOUT=0
FRAMEBUFFER_HANDOFF=0
PANEL_RESOURCES=0
PANEL_LIFECYCLE=0
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
	--battery-dependencies)
		DEPENDENCY_MATRIX=1
		KEY_IRQ_PROFILE='--battery-evidence'
		;;
	--battery-irq|--battery-charge-check|--battery-transport|--battery-runtime|--battery-adc5-conversion|--battery-adc5-consumer|--battery-evidence|--battery-basic-charge|--board-startup)
		;;
	--framebuffer-state)
		FRAMEBUFFER_STATE=1
		KEY_IRQ_PROFILE='--framebuffer-service'
		;;
	--framebuffer-power)
		FRAMEBUFFER_STATE=1
		FRAMEBUFFER_POWER=1
		KEY_IRQ_PROFILE='--framebuffer-service'
		;;
	--framebuffer-vsync)
		FRAMEBUFFER_STATE=1
		FRAMEBUFFER_POWER=1
		FRAMEBUFFER_VSYNC=1
		KEY_IRQ_PROFILE='--framebuffer-service'
		;;
	--framebuffer-scanout|--framebuffer-handoff|--panel-resources|--panel-lifecycle)
		if [ "$KEY_IRQ_PROFILE" = '--panel-lifecycle' ]; then
			PANEL_LIFECYCLE=1
			FRAMEBUFFER_HANDOFF=1
		fi
		if [ "$KEY_IRQ_PROFILE" = '--framebuffer-handoff' ]; then FRAMEBUFFER_HANDOFF=1; fi
		if [ "$KEY_IRQ_PROFILE" = '--panel-resources' ]; then
			PANEL_RESOURCES=1
			FRAMEBUFFER_HANDOFF=1
		fi
		FRAMEBUFFER_STATE=1
		FRAMEBUFFER_POWER=1
		FRAMEBUFFER_VSYNC=1
		FRAMEBUFFER_SCANOUT=1
		KEY_IRQ_PROFILE='--framebuffer-service'
		;;
	--framebuffer-irq-preflight)
		FRAMEBUFFER_STATE=1
		FRAMEBUFFER_POWER=1
		FRAMEBUFFER_IRQ_PREFLIGHT=1
		KEY_IRQ_PROFILE='--framebuffer-service'
		;;
	--framebuffer-f0|--framebuffer-contract|--framebuffer-proof|--framebuffer-service)
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

if [ "$PANEL_LIFECYCLE" = '1' ]; then
 if [ -z "${SWEET_PANEL_UEFI_MANIFEST:-}" ]; then
  printf 'panel lifecycle needs SWEET_PANEL_UEFI_MANIFEST from tools/build-mh2lm-panel-uefi.py; old kernel-32m.blob cannot supply live panel identity\n' >&2
  exit 2
 fi
 KERNEL_BLOB=$(python3 - "$SWEET_PANEL_UEFI_MANIFEST" <<'PYMANIFEST'
from pathlib import Path
import hashlib,json,sys
p=Path(sys.argv[1]);m=json.loads(p.read_text())
assert m.get('abi')==1 and m.get('panel_handoff') is True and m.get('full_fd_built') is True, 'expected completed panel firmware bundle'
b=Path(m['blob']);assert b.is_absolute() and hashlib.sha256(b.read_bytes()).hexdigest()==m['blob_sha256'], 'panel firmware blob hash mismatch'
print(b)
PYMANIFEST
 )
fi

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
if [ "$KEY_IRQ_PROFILE" = '--irq-only' ] || [ "$KEY_IRQ_PROFILE" = '--battery-irq' ] || [ "$KEY_IRQ_PROFILE" = '--battery-charge-check' ] || [ "$KEY_IRQ_PROFILE" = '--battery-transport' ] || [ "$KEY_IRQ_PROFILE" = '--battery-runtime' ] || [ "$KEY_IRQ_PROFILE" = '--battery-adc5-conversion' ] || [ "$KEY_IRQ_PROFILE" = '--battery-adc5-consumer' ] || [ "$KEY_IRQ_PROFILE" = '--battery-evidence' ] || [ "$KEY_IRQ_PROFILE" = '--battery-basic-charge' ] || [ "$KEY_IRQ_PROFILE" = '--board-startup' ] || [ "$KEY_IRQ_PROFILE" = '--framebuffer-contract' ] || [ "$KEY_IRQ_PROFILE" = '--framebuffer-proof' ] || [ "$KEY_IRQ_PROFILE" = '--framebuffer-service' ]; then
	for marker in 'SWEET KEYS IRQ: three routes active; IRQ-only' \
	    'SWEET TOUCH: TouchComm ready (sda=%u scl=%u), max %ux%u, IRQ-only' \
	    'SWEET USB IRQ: FAILED %s hits=%u empty=%u; gadget stopped' \
	    'SWEET BATTERY SID2: msoc='; do
		if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
			printf 'kernel artifact lacks fixed IRQ marker: %s\n' "$marker" >&2
			exit 1
		fi
	done
elif [ "$KEY_IRQ_PROFILE" != '--framebuffer-f0' ] && grep -Fqa 'SWEET KEYS IRQ: three routes active; IRQ-only' \
    "$TMPDIR/kernel.development.mh2lm"; then
	printf 'fixed IRQ kernel requires --irq-only, --battery-irq, --battery-charge-check, --battery-transport, --battery-runtime, --battery-adc5-conversion, --battery-adc5-consumer, --battery-evidence, --battery-basic-charge or --board-startup packaging profile\n' >&2
	exit 1
fi
if [ "$KEY_IRQ_PROFILE" = '--battery-irq' ] || [ "$KEY_IRQ_PROFILE" = '--battery-charge-check' ] || [ "$KEY_IRQ_PROFILE" = '--battery-transport' ] || [ "$KEY_IRQ_PROFILE" = '--battery-runtime' ] || [ "$KEY_IRQ_PROFILE" = '--battery-adc5-conversion' ] || [ "$KEY_IRQ_PROFILE" = '--battery-adc5-consumer' ] || [ "$KEY_IRQ_PROFILE" = '--battery-evidence' ] || [ "$KEY_IRQ_PROFILE" = '--battery-basic-charge' ] || [ "$KEY_IRQ_PROFILE" = '--board-startup' ] || [ "$KEY_IRQ_PROFILE" = '--framebuffer-contract' ] || [ "$KEY_IRQ_PROFILE" = '--framebuffer-proof' ] || [ "$KEY_IRQ_PROFILE" = '--framebuffer-service' ]; then
	for marker in 'SWEET BATT IRQ: armed mode=' \
	    'SWEET MUX: controller transactions serialized across keys and battery' \
	    'SWEET MUX: isolated group=%u; other groups remain available' \
	    'SWEET KEYS IRQ: preparing backend on consumer workloop before attach' \
	    'SWEET BATT IRQ: REVID sid=2' 'battery-msoc' \
	    'SWEET MUX: consumer attached group=' \
	    'SWEET BATT IRQ: waiting for mux registry match; no timer/status polling' \
	    'SWEET BATT IRQ: registry provider matched; routes already attached'; do
		if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
			printf 'kernel artifact lacks battery IRQ marker: %s\n' "$marker" >&2
			exit 1
		fi
	done
fi

if [ "$KEY_IRQ_PROFILE" = '--battery-charge-check' ]; then
	for marker in 'charger/current diagnostics enabled (read-only)' \
	    'SWEET BATT DIAG:' 'SWEET BATT INPUT:' 'SWEET BATT TYPEC:' 'SWEET BATT SMB:'; do
		if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
			printf 'kernel artifact lacks charge diagnostic marker: %s\n' "$marker" >&2
			exit 1
		fi
	done
fi

if [ "$KEY_IRQ_PROFILE" = '--battery-transport' ]; then
	if ! grep -Fqa 'SWEET CHARGER PREFLIGHT: restricted transport v1' "$TMPDIR/kernel.development.mh2lm"; then
		printf 'kernel artifact lacks 0150 restricted transport preflight; check CI buildtree\n' >&2
		exit 1
	fi
fi

if [ "$KEY_IRQ_PROFILE" = '--battery-runtime' ] || [ "$KEY_IRQ_PROFILE" = '--battery-adc5-consumer' ] || [ "$KEY_IRQ_PROFILE" = '--battery-evidence' ]; then
	for marker in 'SWEET BATT RUNTIME: v1 mode=' 'SWEET BATT SAMPLE: v2 id=' \
	    'SWEET BATT POLICY: v1 mode=' 'battery-source' 'battery-fault' \
	    'SWEET FG ACCESS: v2 field=' 'SWEET BATT CONFIG: v3 id=' \
	    'SWEET FG DMA: v3 phase=recovery' 'SWEET BATT TIME: v3 id=' \
	    'SWEET BATT HARDWARE: v1 semantic charger operations' 'SWEET ADC5 PREFLIGHT: v1'; do
		if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
			printf 'kernel lacks battery runtime marker: %s\n' "$marker" >&2
			exit 1
		fi
	done
fi

if [ "$KEY_IRQ_PROFILE" = '--battery-adc5-conversion' ] || [ "$KEY_IRQ_PROFILE" = '--battery-adc5-consumer' ] || [ "$KEY_IRQ_PROFILE" = '--battery-evidence' ]; then
	for marker in 'SWEET ADC5 SERVICE: v1 mode=' 'SWEET ADC5 EOC:' 'SWEET ADC5 SAMPLE:'; do
		if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
			printf 'kernel lacks ADC5 provider marker: %s\n' "$marker" >&2
			exit 1
		fi
	done
fi

if [ "$KEY_IRQ_PROFILE" = '--battery-adc5-consumer' ] || [ "$KEY_IRQ_PROFILE" = '--battery-evidence' ]; then
 for marker in 'SWEET BATT ADC: v1 registry consumer' 'SWEET BATT ADC: sample channel='; do
  if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
   printf 'kernel lacks 0156 ADC consumer marker: %s\n' "$marker" >&2
   exit 1
  fi
 done
fi

if [ "$KEY_IRQ_PROFILE" = '--battery-evidence' ]; then
 for marker in 'SWEET FG PROFILE: v1 fixed prefix24' 'SWEET BATT PROFILE: v1' \
  'SWEET BATT INPUT: v4' 'SWEET BATT INPUT TIME: v1' 'SWEET BATT ICL MODE: v1' \
  'SWEET BATT APSD EVENT: v1' 'SWEET BATT APSD TIME: v1' \
  'SWEET BATT CC: v1' 'SWEET BATT CC TIME: v1' 'SWEET BATT INPUT CAP: v1' \
  'SWEET BATT CC ROLE: v1' \
  'SWEET BATT INPUT SESSION: v1' 'SWEET BATT INPUT PERMIT: v1' 'SWEET BATT INPUT LEASE: v1' 'SWEET BATT ADC INPUT: v1' \
  'SWEET BATT INPUT QUEUE: v1' \
  'battery-usbin-uv' 'battery-usbin-ov' \
  'battery-typec-cc' 'battery-typec-attach' \
  'SWEET MUX: IRQ read failed route=' \
  'SWEET BATT IRQ: published input invalid; external/charging state withdrawn'; do
  if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
   printf 'kernel lacks 0164 input queue/provenance marker: %s\n' "$marker" >&2
   exit 1
  fi
 done
fi

if [ "$KEY_IRQ_PROFILE" = '--battery-basic-charge' ]; then
 for marker in 'SWEET BATT BASIC: v2 runtime3 IRQ control' 'SWEET BATT BASIC: v2 step=' \
  'SWEET BATT BASIC STATE: v2 temp-centi=' 'SWEET BATT BASIC CMD: v2 op=' \
  'SweetBatteryBasicChargeWatchdogPets' 'battery-wdog-bark' \
  'SWEET MUX ARBITRATION: v1' 'SWEET MUX ACK: v1'; do
  if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
   printf 'kernel lacks 0168 runtime3 basic charge marker: %s\n' "$marker" >&2
   exit 1
  fi
 done
fi

if [ "$DEPENDENCY_MATRIX" = 1 ]; then
 for marker in 'SWEET BATT DEPENDENCIES: v1' 'SWEET BATT IDENTITY: v1' \
  'SWEET BATT INHIBIT: v1' 'SWEET BATT DEP CHECK: v1' \
  'SWEET BATT WATCHDOG: v1' 'battery-wdog-bark'; do
  if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
   printf 'kernel lacks 0165 dependency diagnostic marker: %s\n' "$marker" >&2
   exit 1
  fi
 done
 for marker in 'SWEET ADC5 TIMING: v1' 'SWEET ADC5 FAULT: v1' \
  'SWEET BATT WD START: v1' 'SWEET BATT WD COMMAND: v1' 'SWEET BATT WD FAULT: v1'; do
  if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
   printf 'kernel lacks 0166 IRQ timing marker: %s\n' "$marker" >&2
   exit 1
  fi
 done
 for marker in 'SWEET MUX ARBITRATION: v1' 'SWEET MUX ACK: v1'; do
  if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
   printf 'kernel lacks 0167 ACK arbitration marker: %s\n' "$marker" >&2
   exit 1
  fi
 done
fi

if [ "$KEY_IRQ_PROFILE" = '--board-startup' ]; then
	for marker in 'SWEET BOARD: late init before root lookup; independent of UFS' \
	    'SWEET MUX: infrastructure ready intid=' \
	    'SWEET MUX: IOService started; owns workloop, routes and MMIO' \
	    'SWEET MUX CLIENT: matched and attached group=' \
	    'SWEET KEYS IRQ: consumer matching setup failed; keys disabled'; do
		if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
			printf 'kernel artifact lacks board startup marker: %s\n' "$marker" >&2
			exit 1
		fi
	done
fi

if [ "$KEY_IRQ_PROFILE" = '--framebuffer-f0' ]; then
	for marker in 'SWEET FB TRACE: v2 enabled' 'SWEET FB TRACE: v2 %s pair roi=' \
	    'SWEET DISP: F0 v2 cb2' 'SWEET SURF TRACE: v2 id='; do
		if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
			printf 'kernel artifact lacks framebuffer F0 marker: %s\n' "$marker" >&2
			exit 1
		fi
	done
fi

if [ "$KEY_IRQ_PROFILE" = '--framebuffer-contract' ] || [ "$KEY_IRQ_PROFILE" = '--framebuffer-proof' ]; then
	for marker in 'SWEET FB CONTRACT: v1 enabled' 'SWEET FB CONTRACT: setup failed; refusing legacy fallback' \
	    'SWEET BATT BASIC: v2 runtime3 IRQ control'; do
		if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
			printf 'kernel artifact lacks framebuffer contract marker: %s\n' "$marker" >&2
			exit 1
		fi
	done
fi

if [ "$KEY_IRQ_PROFILE" = '--framebuffer-proof' ]; then
	for marker in 'SWEET FB PROOF: v1 enabled' 'SWEET FB PROOF: copy mismatch sample='; do
		if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
			printf 'kernel artifact lacks framebuffer owned-pixel proof marker: %s\n' "$marker" >&2
			exit 1
		fi
	done
fi

if [ "$KEY_IRQ_PROFILE" = '--framebuffer-service' ]; then
 for marker in 'SWEET MDSS: F1a v1 IOService backend' 'SWEET BL: registry backend matched' \
     'SWEET DISP: legacy probe disabled; SweetMDSS owns hardware'; do
  if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
   printf 'kernel lacks F1a display service marker: %s\n' "$marker" >&2
   exit 1
  fi
 done
 if [ "$FRAMEBUFFER_STATE" = '1' ]; then
  for marker in 'SWEET FB SERVICE: F1b v1 dispatch and state gate enabled' \
      'SWEET FB SERVICE: registry backend matched on framebuffer workloop' \
      'SWEET FB SERVICE: state admission closed; clients and ports withdrawn'; do
   if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
    printf 'kernel lacks F1b framebuffer state marker: %s\n' "$marker" >&2
    exit 1
   fi
  done
 fi
 if [ "$FRAMEBUFFER_POWER" = '1' ]; then
  for marker in 'SWEET FB POWER: F2 v1 DCS28/29 enabled; scanout pixels preserved' \
      'SWEET MDSS POWER: %s rc=0 state=' \
      'SWEET FB POWER: requires service1/mdss1/contract0; refusing fallback'; do
   if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
    printf 'kernel lacks F2 panel power marker: %s\n' "$marker" >&2
    exit 1
   fi
  done
 fi
 if [ "$FRAMEBUFFER_VSYNC" = '1' ]; then
  for marker in 'SWEET MDSS IRQ: F3 v2 staged mode=' \
      'SWEET MDSS IRQ: GIC probe passed hits=' 'SWEET MDSS IRQ: RD_PTR armed' \
      'SWEET MDSS IRQ: stats partial=' 'SWEET MDSS IRQ: FAILED' \
      'SWEET FB VSYNC: F3 v2 mailbox consumer' 'SWEET FB VSYNC: hardware active' \
      'SWEET FB VSYNC: FAILED'; do
   if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
    printf 'kernel lacks F3 hardware vsync marker: %s\n' "$marker" >&2
    exit 1
   fi
  done
 fi
 if [ "${FRAMEBUFFER_HANDOFF:-0}" = '1' ]; then
  for marker in 'SWEET MDSS SCANOUT: F4 v2 quiescing' \
      'SWEET MDSS SCANOUT: transfer observed' 'SWEET FB HANDOFF: active' \
      'SweetScanoutHardwarePinned'; do
   if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
    printf 'kernel lacks F4 controlled handoff marker: %s\n' "$marker" >&2
    exit 1
   fi
  done
 fi
 if [ "$FRAMEBUFFER_SCANOUT" = '1' ]; then
  for marker in 'SWEET FB SCANOUT: F4 v1 pool ready' \
      'SWEET FB SCANOUT: seeded A from ABL' 'SWEET FB SCANOUT: inspect pass=' \
      'SWEET FB SCANOUT: pool setup failed' 'SweetScanoutPreflightSamples'; do
   if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
    printf 'kernel lacks F4 scanout prerequisite marker: %s\n' "$marker" >&2
    exit 1
   fi
  done
 fi
 if [ "$PANEL_LIFECYCLE" = '1' ]; then
  for marker in 'SweetPanelSupplyCycleEnabled' 'SweetPanelLifecyclePanelVotesParked' \
      'SweetPanelSelectedProfile' 'SWEET PANEL SUPPLY:' 'SweetPanelLifecycleResetHeldLow'; do
   if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
    printf 'kernel lacks panel lifecycle marker: %s\n' "$marker" >&2
    exit 1
   fi
  done
  # NoC ABI declaration requires resource2/clock1 even before lifecycle setup.
  # Keep metadata admission independent of hardware lifecycle providers.
  python3 "$SCRIPT_DIR/mk-mh2lm-mdss-adt.py" --panel-identity "$ADT" "$TMPDIR/identity.adt"
  python3 "$SCRIPT_DIR/mk-mh2lm-mdss-adt.py" --display-noc --panel-pins --panel-touch --panel-identity "$ADT" "$TMPDIR/mdss.adt"
 elif [ "$PANEL_RESOURCES" = '1' ]; then
  for marker in 'SweetRPMhABI' 'SweetRPMhWritesAllowed' 'SweetDisplayRPMhBound' \
      'SWEET MDSS: matched RPMh consumer=' 'SWEET RPMH: pre-v3 RSC id=' \
      'SWEET FB PERF:' 'sweetfbperf' 'sweetdisplayresources'; do
   if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
    printf 'kernel lacks D1 resource/performance marker: %s\n' "$marker" >&2
    exit 1
   fi
  done
  python3 "$SCRIPT_DIR/mk-mh2lm-mdss-adt.py" --display-resources "$ADT" "$TMPDIR/mdss.adt"
 elif [ "$FRAMEBUFFER_IRQ_PREFLIGHT" = '1' ]; then
  for marker in 'SWEET MDSS IRQ PREFLIGHT: F3 v1 read-only' \
      'SWEET MDSS IRQ PREFLIGHT: v1 pass=%u GIC intid=115' \
      'SWEET MDSS IRQ PREFLIGHT: v1 pass=%u MDSS rev=' \
      'SweetDisplayIRQPreflightSamples'; do
   if ! grep -Fqa "$marker" "$TMPDIR/kernel.development.mh2lm"; then
    printf 'kernel lacks F3 read-only preflight marker: %s\n' "$marker" >&2
    exit 1
   fi
  done
  python3 "$SCRIPT_DIR/mk-mh2lm-mdss-adt.py" --irq-preflight "$ADT" "$TMPDIR/mdss.adt"
 elif [ "$FRAMEBUFFER_VSYNC" = '1' ]; then
  python3 "$SCRIPT_DIR/mk-mh2lm-mdss-adt.py" --irq-preflight "$ADT" "$TMPDIR/mdss.adt"
 else
  python3 "$SCRIPT_DIR/mk-mh2lm-mdss-adt.py" "$ADT" "$TMPDIR/mdss.adt"
 fi
 ADT="$TMPDIR/mdss.adt"
fi

make_profile() {
	local name=$1
	local title=$2
	local extra=$3
	local profile_adt=${4:-$ADT}
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
	mcopy -o -i "$ramdisk" "$profile_adt" ::/EFI/UniXNU/Payloads/kernel.macho.adt

	mkdir -p "$verify"
	mcopy -o -i "$ramdisk" ::/EFI/UniXNU/unixnu.toml "$verify/unixnu.toml"
	mcopy -o -i "$ramdisk" ::/EFI/UniXNU/Payloads/kernel.macho "$verify/kernel.macho"
	mcopy -o -i "$ramdisk" ::/EFI/UniXNU/Payloads/kernel.macho.adt "$verify/kernel.macho.adt"
	cmp "$verify/unixnu.toml" "$toml"
	cmp "$verify/kernel.macho" "$kernel"
	cmp "$verify/kernel.macho.adt" "$profile_adt"

	(
		cd "$MU"
		python3 Resources/Scripts/mkbootimg.py --kernel "$KERNEL_BLOB" \
			--ramdisk "$ramdisk" --header_version 1 -o "$image"
	)
	if command -v file >/dev/null 2>&1; then file "$image"; fi
	sha256sum "$image"
}

if [ "$KEY_IRQ_PROFILE" = '--framebuffer-service' ]; then
 FB_ARGS='ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetkeys=1 sweetbattery=1 sweetbatt=1 sweetbattirq=4 sweetbattruntime=3 sweetfbpreserveoff=1 sweetfbtrace=0 sweetdispprobe=1 sweetloglevel=2 sweetfbcontract=0 sweetfbproof=0'
 if [ "$PANEL_LIFECYCLE" = '1' ]; then
  PANEL_ARGS="$FB_ARGS sweetmdss=1 sweetfbservice=1 sweetmdssirqprobe=0 sweetfbperf=1"
  make_profile panel-identity "LG G8X - XNU live ABL panel selection, metadata only (${LABEL})" \
   "$PANEL_ARGS sweetfbpower=0 sweetmdssirq=0 sweetfbvsync=0 sweetfbown=0 sweetdsi=0 sweetdisplayresources=1 sweetdisplayclocks=0 sweetdisplaycore=0 sweetdisplaymanaged=0 sweetdsihost=0 sweetdsilink=0 sweetpanelpins=0 sweettouchpanel=0 sweetpanellifecycle=0" "$TMPDIR/identity.adt"
  PANEL_ARGS="$PANEL_ARGS sweetfbpower=1 sweetmdssirq=2 sweetfbvsync=1 sweetfbown=2 sweetdsi=1 sweetdisplayresources=2 sweetdisplayclocks=1 sweetdisplaycore=2 sweetdisplaymanaged=1 sweetdsihost=1 sweetdsilink=2 sweetpanelpins=2 sweettouchpanel=2"
  make_profile panel-retained "LG G8X - XNU full panel reset, all supply votes held (${LABEL})" "$PANEL_ARGS sweetpanellifecycle=2"
  make_profile panel-combined "LG G8X - XNU DATA and link parking, supply votes held (${LABEL})" "$PANEL_ARGS sweetpanellifecycle=5"
  make_profile panel-supply "LG G8X - XNU panel-only vote withdrawal and restore (${LABEL})" "$PANEL_ARGS sweetpanellifecycle=6"
  printf 'kernel SHA256: %s\ncreated identity/retained/combined/supply profiles for %s; inspect live identity before vendor reset/supply tests; no shared L3/L5 withdrawal\n' "$KERNEL_SHA256" "$LABEL"
  exit 0
 fi
 if [ "$PANEL_RESOURCES" = '1' ]; then
  # Reuse the already tested single-buffer path. Both profiles have the same
  # v1 ADT, kernel and optional performance accounting; only resource admission
  # differs. Mode1 performs metadata/binding only and cannot send RPMh votes.
  FB_ARGS="$FB_ARGS sweetmdss=1 sweetfbservice=1 sweetfbpower=1 sweetmdssirqprobe=0 sweetmdssirq=2 sweetfbvsync=1 sweetfbown=2 sweetfbperf=1 sweetdsi=0"
  make_profile panel-resources "LG G8X - XNU D1 resources and FB performance (${LABEL})" "$FB_ARGS sweetdisplayresources=1"
  make_profile panel-resources-control "LG G8X - XNU D1 resources disabled control (${LABEL})" "$FB_ARGS sweetdisplayresources=0"
  printf 'kernel SHA256: %s\ncreated D1 metadata binding and optional control for %s; no RPMh votes or vendor DSI sequences\n' "$KERNEL_SHA256" "$LABEL"
  exit 0
 fi
 if [ "${FRAMEBUFFER_SCANOUT:-0}" = '1' ]; then
  FB_ARGS="$FB_ARGS sweetmdss=1 sweetfbservice=1 sweetfbpower=1 sweetmdssirqprobe=0 sweetmdssirq=2 sweetfbvsync=1"
  make_profile fb-scanout-baseline "LG G8X - XNU F4 prerequisite disabled (${LABEL})" "$FB_ARGS sweetfbown=0"
  make_profile fb-scanout-preflight "LG G8X - XNU F4 pool and read-only inspection (${LABEL})" "$FB_ARGS sweetfbown=1"
  if [ "${FRAMEBUFFER_HANDOFF:-0}" = '1' ]; then
   make_profile fb-scanout-single "LG G8X - XNU F4 controlled single-buffer handoff (${LABEL})" "$FB_ARGS sweetfbown=2"
   printf 'kernel SHA256: %s\ncreated F4 baseline/preflight/single-buffer profiles for %s; own2 may submit DMA, lifetime pin survives until reboot\n' "$KERNEL_SHA256" "$LABEL"
  else
   printf 'kernel SHA256: %s\ncreated F4 scanout prerequisite baseline/preflight profiles for %s; no scan-address writes\n' "$KERNEL_SHA256" "$LABEL"
  fi
  exit 0
 fi
 if [ "$FRAMEBUFFER_VSYNC" = '1' ]; then
  FB_ARGS="$FB_ARGS sweetmdss=1 sweetfbservice=1 sweetfbpower=1 sweetmdssirqprobe=0"
  make_profile fb-vsync-baseline "LG G8X - XNU F3 IRQ disabled (${LABEL})" "$FB_ARGS sweetmdssirq=0 sweetfbvsync=0"
  make_profile fb-vsync-gic "LG G8X - XNU F3 GIC delivery (${LABEL})" "$FB_ARGS sweetmdssirq=1 sweetfbvsync=0"
  make_profile fb-vsync-rdptr "LG G8X - XNU F3 RD_PTR counting (${LABEL})" "$FB_ARGS sweetmdssirq=2 sweetfbvsync=0"
  make_profile fb-vsync-hardware "LG G8X - XNU F3 hardware vsync (${LABEL})" "$FB_ARGS sweetmdssirq=2 sweetfbvsync=1"
  printf 'kernel SHA256: %s\ncreated F3 IRQ baseline/GIC/RD_PTR/hardware-vsync profiles for %s\n' "$KERNEL_SHA256" "$LABEL"
  exit 0
 fi
 if [ "$FRAMEBUFFER_IRQ_PREFLIGHT" = '1' ]; then
  FB_ARGS="$FB_ARGS sweetmdss=1 sweetfbservice=1 sweetfbpower=1"
  make_profile fb-irq-baseline "LG G8X - XNU F3 preflight disabled (${LABEL})" "$FB_ARGS sweetmdssirqprobe=0"
  make_profile fb-irq-gic "LG G8X - XNU F3 GIC read-only (${LABEL})" "$FB_ARGS sweetmdssirqprobe=1"
  make_profile fb-irq-preflight "LG G8X - XNU F3 MDSS IRQ read-only (${LABEL})" "$FB_ARGS sweetmdssirqprobe=2"
  printf 'kernel SHA256: %s\ncreated F3 read-only IRQ baseline/GIC/MDSS profiles for %s\n' "$KERNEL_SHA256" "$LABEL"
  exit 0
 fi
 if [ "$FRAMEBUFFER_POWER" = '1' ]; then
  make_profile fb-power-baseline "LG G8X - XNU F1b panel power legacy (${LABEL})" "$FB_ARGS sweetmdss=1 sweetfbservice=1 sweetfbpower=0"
  make_profile fb-power-dcs "LG G8X - XNU F2 DCS display power (${LABEL})" "$FB_ARGS sweetmdss=1 sweetfbservice=1 sweetfbpower=1"
  printf 'kernel SHA256: %s\ncreated F2 panel power baseline and DCS profiles for %s\n' "$KERNEL_SHA256" "$LABEL"
  exit 0
 fi
 if [ "$FRAMEBUFFER_STATE" = '1' ]; then
  make_profile fb-state-baseline "LG G8X - XNU F1a state legacy (${LABEL})" "$FB_ARGS sweetmdss=1 sweetfbservice=0"
  make_profile fb-state-gated "LG G8X - XNU F1b state gate (${LABEL})" "$FB_ARGS sweetmdss=1 sweetfbservice=1"
  printf 'kernel SHA256: %s\ncreated F1b state baseline and gated profiles for %s\n' "$KERNEL_SHA256" "$LABEL"
  exit 0
 fi
 make_profile fb-service-baseline "LG G8X - XNU display legacy (${LABEL})" "$FB_ARGS sweetmdss=0"
 make_profile fb-service-mdss "LG G8X - XNU SweetMDSS F1a (${LABEL})" "$FB_ARGS sweetmdss=1"
 printf 'kernel SHA256: %s\ncreated F1a legacy and SweetMDSS profiles for %s\n' "$KERNEL_SHA256" "$LABEL"
 exit 0
fi

if [ "$KEY_IRQ_PROFILE" = '--framebuffer-contract' ] || [ "$KEY_IRQ_PROFILE" = '--framebuffer-proof' ]; then
	# Keep source-sampling probes off. Isolate source ownership from off erasure.
	FB_ARGS='ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetkeys=1 sweetbattery=1 sweetbatt=1 sweetbattirq=4 sweetbattruntime=3 sweetfbpreserveoff=1 sweetfbtrace=0 sweetdispprobe=0 sweetloglevel=2'
	if [ "$KEY_IRQ_PROFILE" = '--framebuffer-proof' ]; then
		make_profile fb-contract-proof \
			"LG G8X - XNU framebuffer same-swap proof (${LABEL})" \
			"$FB_ARGS sweetfbcontract=1 sweetfbproof=1"
		printf 'kernel SHA256: %s\ncreated framebuffer same-swap proof profile for %s\n' "$KERNEL_SHA256" "$LABEL"
		exit 0
	fi
	make_profile fb-contract-baseline \
		"LG G8X - XNU framebuffer contract baseline (${LABEL})" \
		"$FB_ARGS sweetfbcontract=0"
	make_profile fb-contract-owned \
		"LG G8X - XNU framebuffer owned snapshots (${LABEL})" \
		"$FB_ARGS sweetfbcontract=1"
	printf 'kernel SHA256: %s\ncreated framebuffer contract baseline and owned profiles for %s\n' "$KERNEL_SHA256" "$LABEL"
	exit 0
fi

if [ "$KEY_IRQ_PROFILE" = '--framebuffer-f0' ]; then
	# F0 is read-only display evidence. The two images differ only in the
	# existing display-off behavior so the black-block cause can be separated
	# from the preserve-off experiment without another kernel build.
	make_profile fb-trace \
		"LG G8X - XNU framebuffer F0 trace (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetkeys=1 sweetbattery=1 sweetbatt=1 sweetbattirq=4 sweetbattruntime=3 sweetfbtrace=2 sweetdispprobe=1 sweetloglevel=2"
	make_profile fb-trace-preserveoff \
		"LG G8X - XNU framebuffer F0 preserve-off (${LABEL})" \
		"ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetkeys=1 sweetbattery=1 sweetbatt=1 sweetbattirq=4 sweetbattruntime=3 sweetfbtrace=2 sweetdispprobe=1 sweetloglevel=2 sweetfbpreserveoff=1"
	printf 'kernel SHA256: %s\ncreated framebuffer F0 trace and preserve-off profiles for %s\n' "$KERNEL_SHA256" "$LABEL"
	exit 0
fi

if [ "$KEY_IRQ_PROFILE" = '--irq-only' ] || [ "$KEY_IRQ_PROFILE" = '--battery-irq' ] || [ "$KEY_IRQ_PROFILE" = '--battery-charge-check' ] || [ "$KEY_IRQ_PROFILE" = '--battery-transport' ] || [ "$KEY_IRQ_PROFILE" = '--battery-runtime' ] || [ "$KEY_IRQ_PROFILE" = '--battery-adc5-conversion' ] || [ "$KEY_IRQ_PROFILE" = '--battery-adc5-consumer' ] || [ "$KEY_IRQ_PROFILE" = '--battery-evidence' ] || [ "$KEY_IRQ_PROFILE" = '--battery-basic-charge' ] || [ "$KEY_IRQ_PROFILE" = '--board-startup' ]; then
	# Fixed IRQ images retain the validated PM8150B gauge/UI path. Without
	# both battery arguments SweetBattery publishes the static 100% source.
	IRQ_ARGS='ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetusb=2 sweetusbstep=6 sweetvbus=1 sweetkeys=1 sweetbattery=1 sweetbatt=1'

	if [ "$KEY_IRQ_PROFILE" = '--battery-basic-charge' ]; then
  # Runtime3 actively enables PMIC charging (0168); runtime-only, no deps/ADC client.
  make_profile battery-basic-charge \
   "LG G8X - XNU runtime3 basic charge (${LABEL})" \
   "$IRQ_ARGS sweetbattirq=4 sweetbattuidiag=1 sweetbattruntime=3 sweetbattadcconv=0 sweetbattdeps=0"
  # 0169 kernels add the opt-in FG batt-temp-delta route; same kernel, one extra argument.
  if grep -Fqa 'SWEET BATT TEMP IRQ: v1' "$TMPDIR/kernel.development.mh2lm" &&
     grep -Fqa 'battery-temp-delta' "$TMPDIR/kernel.development.mh2lm" &&
     grep -Fqa 'SWEET BATT BASIC POWER: v1' "$TMPDIR/kernel.development.mh2lm"; then
   make_profile battery-basic-charge-tempirq \
    "LG G8X - XNU runtime3 charge + temp IRQ (${LABEL})" \
    "$IRQ_ARGS sweetbattirq=4 sweetbattuidiag=1 sweetbattruntime=3 sweetbattadcconv=0 sweetbattdeps=0 sweetbatttempirq=1"
   printf 'created runtime3 + FG temp-delta IRQ profile for %s (0169)\n' "$LABEL"
  fi
  printf 'kernel SHA256: %s\ncreated runtime3 basic charge profile for %s; charging is ENABLED by software\n' "$KERNEL_SHA256" "$LABEL"
  exit 0
 fi

	if [ "$KEY_IRQ_PROFILE" = '--battery-evidence' ]; then
  make_profile battery-evidence \
   "LG G8X - XNU battery profile/input evidence (${LABEL})" \
   "$IRQ_ARGS sweetbattirq=4 sweetbattuidiag=1 sweetbattruntime=1 sweetbattadcconv=0 sweetbattdeps=0"
  if [ "$DEPENDENCY_MATRIX" = 1 ]; then
   for entry in 'identity:1' 'inhibit:2' 'watchdog:4' 'deps-all:7'; do
    name=${entry%:*}
    mask=${entry#*:}
    make_profile "battery-${name}" \
     "LG G8X - XNU battery ${name} observation (${LABEL})" \
     "$IRQ_ARGS sweetbattirq=4 sweetbattuidiag=1 sweetbattruntime=1 sweetbattadcconv=0 sweetbattdeps=$mask"
   done
   printf 'dependency matrix ready: evidence / identity / inhibit / watchdog / deps-all; all runtime1, no charging enable\n'
  fi
  printf 'kernel SHA256: %s\ncreated battery profile/input evidence profile for %s; charging remains interlocked\n' "$KERNEL_SHA256" "$LABEL"
  exit 0
 fi

	if [ "$KEY_IRQ_PROFILE" = '--battery-adc5-consumer' ]; then
  make_profile battery-adc5-consumer \
   "LG G8X - XNU battery ADC5 consumer (${LABEL})" \
   "$IRQ_ARGS sweetbattirq=4 sweetbattuidiag=1 sweetbattruntime=1 sweetbattadcconv=0"
  printf 'kernel SHA256: %s\ncreated battery ADC5 consumer profile for %s; charging remains interlocked\n' "$KERNEL_SHA256" "$LABEL"
  exit 0
 fi

	if [ "$KEY_IRQ_PROFILE" = '--battery-adc5-conversion' ]; then
		make_profile battery-adc5-conversion \
			"LG G8X - XNU ADC5 conversion provider (${LABEL})" \
			"$IRQ_ARGS sweetbattirq=4 sweetbattuidiag=1 sweetbattruntime=1 sweetbattadcdiag=1 sweetbattadcconv=3"
		printf 'kernel SHA256: %s\ncreated ADC5 conversion provider profile for %s; charging remains interlocked\n' "$KERNEL_SHA256" "$LABEL"
		exit 0
	fi

	if [ "$KEY_IRQ_PROFILE" = '--battery-runtime' ]; then
		# One kernel, three independent profiles. No controlled-enable image:
		# battery identity, measured VBUS and parallel evidence are outstanding.
		make_profile battery-runtime-observe \
			"LG G8X - XNU battery observe (${LABEL})" \
			"$IRQ_ARGS sweetbattirq=4 sweetbattuidiag=1 sweetbattdiag=1"
		make_profile battery-telemetry \
			"LG G8X - XNU real battery telemetry (${LABEL})" \
			"$IRQ_ARGS sweetbattirq=4 sweetbattuidiag=1 sweetbattruntime=1 sweetbattadcdiag=1"
		make_profile battery-policy-dry \
			"LG G8X - XNU battery policy dry run (${LABEL})" \
			"$IRQ_ARGS sweetbattirq=4 sweetbattuidiag=1 sweetbattruntime=2"
		printf 'kernel SHA256: %s\ncreated observe/telemetry/policy-dry profiles for %s; control remains interlocked\n' "$KERNEL_SHA256" "$LABEL"
		exit 0
	fi

	if [ "$KEY_IRQ_PROFILE" = '--battery-transport' ]; then
		# Same kernel and existing IRQ drivers; no charging-policy writes.
		make_profile battery-observe \
			"LG G8X - XNU battery observe (${LABEL})" \
			"$IRQ_ARGS sweetbattirq=4 sweetbattuidiag=1 sweetbattdiag=1"
		make_profile battery-transport \
			"LG G8X - XNU charger transport read-only (${LABEL})" \
			"$IRQ_ARGS sweetbattirq=4 sweetbattuidiag=1 sweetbattdiag=1 sweetbatttransport=1"
		printf 'kernel SHA256: %s\ncreated observe + read-only transport profiles for %s\n' "$KERNEL_SHA256" "$LABEL"
		exit 0
	fi

	if [ "$KEY_IRQ_PROFILE" = '--battery-charge-check' ]; then
		# Keep mode4 and all input IRQs; enable existing read-only diagnostics.
		make_profile battery-charge-diag \
			"LG G8X - XNU battery charge diagnostics (${LABEL})" \
			"$IRQ_ARGS sweetbattirq=4 sweetbattuidiag=1 sweetbattdiag=1"
		# Optional control only: compare gadget/VBUS override with the earlier
		# charge-positive configuration. No ACM in this profile; use SWEETLOG.
		CHARGE_NO_USB_ARGS=${IRQ_ARGS/sweetusb=2 sweetusbstep=6 sweetvbus=1/sweetusb=0}
		make_profile battery-charge-diag-no-usb \
			"LG G8X - XNU battery charge diagnostics without gadget (${LABEL})" \
			"$CHARGE_NO_USB_ARGS sweetbattirq=4 sweetbattuidiag=1 sweetbattdiag=1"
		printf 'kernel SHA256: %s\ncreated charge diagnostic profiles for %s; no-usb is an optional control\n' "$KERNEL_SHA256" "$LABEL"
		exit 0
	fi

	if [ "$KEY_IRQ_PROFILE" = '--board-startup' ]; then
		# Separate startup ownership regression from battery IRQ delivery.
		make_profile board-startup-baseline \
			"LG G8X - XNU board startup baseline (${LABEL})" \
			"$IRQ_ARGS sweetbattirq=0"
		BOARD_NO_KEYS_ARGS=${IRQ_ARGS/sweetkeys=1/sweetkeys=0}
		make_profile board-startup-no-keys \
			"LG G8X - XNU board mux without keys (${LABEL})" \
			"$BOARD_NO_KEYS_ARGS sweetbattirq=2 sweetbattuidiag=1"
		make_profile board-startup-usbin \
			"LG G8X - XNU board keys and battery IRQ (${LABEL})" \
			"$IRQ_ARGS sweetbattirq=2 sweetbattuidiag=1"
		printf 'kernel SHA256: %s\ncreated board startup profiles for %s\n' "$KERNEL_SHA256" "$LABEL"
		exit 0
	fi

	if [ "$KEY_IRQ_PROFILE" = '--battery-irq' ]; then
		# One compiled kernel; staged sources without disabling key/USB/touch IRQ.
		make_profile battery-irq-baseline \
			"LG G8X - XNU battery polling baseline (${LABEL})" \
			"$IRQ_ARGS sweetbattirq=0"
		make_profile battery-irq-preflight \
			"LG G8X - XNU battery read-only preflight (${LABEL})" \
			"$IRQ_ARGS sweetbattirq=1 sweetbattirqdiag=1 sweetbattuidiag=1"
		make_profile battery-irq-usbin \
			"LG G8X - XNU battery USB insertion IRQ (${LABEL})" \
			"$IRQ_ARGS sweetbattirq=2 sweetbattuidiag=1"
		make_profile battery-irq-charger \
			"LG G8X - XNU battery USB and charge-state IRQ (${LABEL})" \
			"$IRQ_ARGS sweetbattirq=3 sweetbattuidiag=1"
		make_profile battery-irq-all \
			"LG G8X - XNU battery USB charge and gauge IRQ (${LABEL})" \
			"$IRQ_ARGS sweetbattirq=4 sweetbattuidiag=1"
		if grep -Fqa 'sweetloglevel' "$TMPDIR/kernel.development.mh2lm"; then
			make_profile battery-irq-all-debug \
				"LG G8X - XNU all battery IRQ debug (${LABEL})" \
				"$IRQ_ARGS sweetbattirq=4 sweetbattuidiag=1 sweetloglevel=3"
		fi
		printf 'kernel SHA256: %s\ncreated staged battery IRQ profiles for %s\n' "$KERNEL_SHA256" "$LABEL"
		exit 0
	fi
	make_profile irq-only \
		"LG G8X - XNU fixed key, USB and touch IRQ (${LABEL})" \
		"$IRQ_ARGS"
	if grep -Fqa 'sweetloglevel' "$TMPDIR/kernel.development.mh2lm" && \
	    grep -Fqa '[DEBUG] SWEET USB' "$TMPDIR/kernel.development.mh2lm"; then
		make_profile irq-only-debug \
			"LG G8X - XNU fixed IRQ debug logs (${LABEL})" \
			"$IRQ_ARGS sweetloglevel=3"
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
