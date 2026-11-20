/*
 * LG G8X ThinQ (mh2lm, SM8150) touch: Synaptics TouchComm (TCM) at I2C
 * address 0x20 on the SE17 lines (gpio55/gpio56).
 *
 * Board facts are from the LG Windows ACPI tables: TSC1 (_HID "SNPTSTCM" in
 * Mu-Silicium's mh2 DSDT, MSHW1003 in aloha-mh2lm's) is I2C 0x20 at 400 kHz
 * on \_SB.IC18 (= 0x00C80000 = SE17), interrupt TLMM gpio122 (level, active
 * low, pull-up), reset TLMM gpio54. Its PEP D0 sequence drives TLMM gpio59
 * and gpio152 high, waits, then raises gpio54; there is no PMIC or RPMh rail
 * in it, so this driver never votes a regulator.
 *
 * Transport: SE17's FIFO interface is fused off on SM8150 (guacamole,
 * sweet_touch_sec.c), so I2C is bit-banged on the two GPIOs exactly as there
 * (open-drain emulation, SDA/SCL auto-swapped when nothing acknowledges).
 * TouchComm over I2C has no register address: plain reads and writes.
 *
 * Protocol: TouchComm v1/v2 from UniXNU-Pagani-Drivers PaganiInput/S3910.c
 * (Copyright (c) 2026 UniXNU contributors, BSD-2-Clause-Patent), reduced to
 * the generic sequence: identify, application info, set and read back the
 * touch report format, enable the touch report. The S3910-only HBP steps
 * (report 0x23, dynamic config 0x11 = LBP) are left out. Controller
 * firmware is never written, erased or flashed; a controller in bootloader
 * mode is reported and left alone.
 */
#include <pexpert/arm64/sweet_dev.h>

#if defined(ARM64_BOARD_CONFIG_SWEET) && defined(SWEET_DEV_TOUCH_TCM) && SWEET_DEV_TOUCH_TCM
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include "sweet_log_level.h"
#include "sweet_touch_panel.h"
#include <mach/mach_time.h>

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
/* Mach MIG declaration is not included by BSD's exported thread.h. */
extern kern_return_t thread_terminate(thread_t);
extern void sweet_hid_touch(int finger, int down, int x, int y);
void sweet_touch_init(void);

#define TS_ADDR         0x20u
#define TS_PIN_RESET    54u
#define TS_PIN_PWR_A    59u     /* PEP D0 first step */
#define TS_PIN_PWR_B    152u    /* PEP D0 second step */
#define TS_PIN_IRQ      122u    /* ATTN, active low */
#define TS_PIN_I2C_A    55u
#define TS_PIN_I2C_B    56u

/* GPIO122 is not a SM8150 direct-connect pin. TLMM summary GIC SPI 208. */
#define TS_IRQ_INTID    240u
#define TS_IRQ_BIT      (1u << (TS_IRQ_INTID % 32u))
#define TS_IRQ_WORD     (TS_IRQ_INTID / 32u)
#define TS_GICD_PA      0x17a00000u
#define TS_INTR_CFG     (TS_GPIO_OFF(TS_PIN_IRQ) + 8u)
#define TS_INTR_STATUS  (TS_GPIO_OFF(TS_PIN_IRQ) + 12u)

#define TCM_CONTACTS    10u
#define TCM_PAYLOAD_MAX 512u

/* ---- TLMM (SM8150: four tile arrays, as sweet_touch_sec.c) ---- */

enum { TS_TILE_WEST, TS_TILE_EAST, TS_TILE_NORTH, TS_TILE_SOUTH, TS_TILE_COUNT };

static vm_offset_t ts_tiles[TS_TILE_COUNT];

static unsigned
ts_tile(unsigned pin)
{
	if (pin <= 7 || pin == 38 || (pin >= 51 && pin <= 82) ||
	    (pin >= 98 && pin <= 101) || (pin >= 114 && pin <= 124) ||
	    (pin >= 126 && pin <= 154)) {
		return TS_TILE_SOUTH;
	}
	if ((pin >= 8 && pin <= 20) || (pin >= 31 && pin <= 37) ||
	    (pin >= 39 && pin <= 42) || (pin >= 83 && pin <= 86) ||
	    (pin >= 88 && pin <= 97) || (pin >= 102 && pin <= 104) ||
	    pin >= 169) {
		return TS_TILE_NORTH;
	}
	if ((pin >= 21 && pin <= 30) || (pin >= 43 && pin <= 50) || pin == 87) {
		return TS_TILE_EAST;
	}
	return TS_TILE_WEST;
}

#define TS_GPIO_OFF(n)    ((uint32_t)(n) * 0x1000u)
#define TS_GPIO_IO_OFF(n) (TS_GPIO_OFF(n) + 4u)

static uint32_t
ts_rd(vm_offset_t base, uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}

static void
ts_wr(vm_offset_t base, uint32_t off, uint32_t v)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(base + off) = v;
	__asm__ volatile("dsb osh" ::: "memory");
}

static uint32_t ts_gpio_rd(unsigned pin) { return ts_rd(ts_tiles[ts_tile(pin)], TS_GPIO_OFF(pin)); }
static uint32_t ts_gpio_io_rd(unsigned pin) { return ts_rd(ts_tiles[ts_tile(pin)], TS_GPIO_IO_OFF(pin)); }
static void ts_gpio_wr(unsigned pin, uint32_t v) { ts_wr(ts_tiles[ts_tile(pin)], TS_GPIO_OFF(pin), v); }
static void ts_gpio_io_wr(unsigned pin, uint32_t v) { ts_wr(ts_tiles[ts_tile(pin)], TS_GPIO_IO_OFF(pin), v); }

static int ts_chan;
/* 0: inactive; 1: armed; 2: worker pending; 3: failed;
 * 4: bounded GIC software-pending probe; 5: panel-coordination parked. */
static volatile unsigned ts_irq_state, ts_irq_hits, ts_irq_probe_hits;
/* Wakes where the controller had already dropped ATTENTION but its pad latch
 * still held a packet: the exact case that used to be acked and thrown away. */
static unsigned ts_irq_late;
static vm_offset_t ts_gicd;
static int ts_irq_chan;
static bool ts_irq_ready;
static unsigned ts_irq_empty, ts_irq_no_progress;

/* Panel coordination has no provider pointer/callback. The packed atomic
 * frame publishes a coherent token+state; only the sole worker touches hardware.
 * Open/submit/query/close are metadata-only. State5 in the IRQ engine is parked. */
static uint64_t ts_panel_owner, ts_panel_serial, ts_panel_frame;
static unsigned ts_panel_live, ts_panel_cancel;
static bool ts_panel_servicing;
static uint64_t ts_panel_deadline;
static uint32_t ts_panel_pin_cfg;
#define TS_PANEL_LIMIT (~0ULL >> 4)
static uint64_t ts_panel_now(void)
{
 uint64_t n=0; absolutetime_to_nanoseconds(mach_absolute_time(), &n); return n;
}
static bool ts_panel_interrupted(void)
{
 return ts_panel_servicing && (__atomic_load_n(&ts_panel_cancel,__ATOMIC_ACQUIRE) ||
     ts_panel_now()>=ts_panel_deadline);
}
int sweet_touch_panel_open(uint64_t *session)
{
 if (!session) return EINVAL;
 *session=0;
 uint64_t serial=__atomic_load_n(&ts_panel_serial,__ATOMIC_RELAXED);
 if (serial>=TS_PANEL_LIMIT) return EOVERFLOW;
 /* Native service start is single-owner, but competing starts remain safe. */
 if (!__atomic_compare_exchange_n(&ts_panel_serial,&serial,serial+1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return EBUSY;
 uint64_t empty=0;
 if (!__atomic_compare_exchange_n(&ts_panel_owner,&empty,serial+1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return EBUSY;
 *session=serial+1; return 0;
}
int sweet_touch_panel_close(uint64_t session)
{
 if (!session || session!=__atomic_load_n(&ts_panel_owner,__ATOMIC_ACQUIRE)) return ENOENT;
 uint64_t frame=__atomic_load_n(&ts_panel_frame,__ATOMIC_ACQUIRE);
 if (frame>>4) {
  __atomic_store_n(&ts_panel_cancel,1u,__ATOMIC_RELEASE);
  __atomic_store_n(&ts_panel_owner,~0ULL,__ATOMIC_RELEASE);
  thread_wakeup(&ts_irq_chan); thread_wakeup(&ts_chan);
 } else __atomic_store_n(&ts_panel_owner,0ULL,__ATOMIC_RELEASE);
 return 0;
}
int sweet_touch_panel_request(uint64_t session,unsigned resume,uint64_t *token)
{
 if (!token) return EINVAL;
 *token=0;
 if (!session || session!=__atomic_load_n(&ts_panel_owner,__ATOMIC_ACQUIRE)) return ENOENT;
 if (resume>1) return EINVAL;
 if (!__atomic_load_n(&ts_panel_live,__ATOMIC_ACQUIRE) || __atomic_load_n(&ts_panel_cancel,__ATOMIC_ACQUIRE)) return EAGAIN;
 uint64_t frame=__atomic_load_n(&ts_panel_frame,__ATOMIC_ACQUIRE);
 unsigned state=(unsigned)(frame&15u);
 if (state!=(resume ? SWEET_TOUCH_QUIESCED : SWEET_TOUCH_RUNNING)) return EBUSY;
 uint64_t ticket=(frame>>4)+1;
 if (ticket>TS_PANEL_LIMIT) return EOVERFLOW;
 uint64_t next=(ticket<<4)|(resume ? SWEET_TOUCH_RESUME_REQUESTED : SWEET_TOUCH_PAUSE_REQUESTED);
 if (!__atomic_compare_exchange_n(&ts_panel_frame,&frame,next,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return EBUSY;
 *token=ticket; thread_wakeup(&ts_irq_chan); return 0;
}
int sweet_touch_panel_snapshot(uint64_t session,uint64_t token,struct sweet_touch_panel_status *out)
{
 if (!out) return EINVAL;
 *out=(struct sweet_touch_panel_status){0};
 if (!session || session!=__atomic_load_n(&ts_panel_owner,__ATOMIC_ACQUIRE)) return ENOENT;
 uint64_t frame=__atomic_load_n(&ts_panel_frame,__ATOMIC_ACQUIRE);
 if (token && token!=frame>>4) return ENOENT;
 out->token=frame>>4; out->state=(unsigned)(frame&15u);
 out->available=__atomic_load_n(&ts_panel_live,__ATOMIC_ACQUIRE);
 return 0;
}

static void
ts_wait(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&ts_chan, THREAD_UNINT, deadline);
 if (ts_panel_interrupted()) clear_wait(current_thread(), THREAD_AWAKENED);
	thread_block(THREAD_CONTINUE_NULL);
}

static uint32_t
ts_gic_rd(unsigned off)
{
	return ts_rd(ts_gicd, off);
}

static void
ts_gic_wr(unsigned off, uint32_t value)
{
	ts_wr(ts_gicd, off, value);
}

static uint64_t
ts_gic_rd64(unsigned off)
{
	uint64_t value = *(volatile uint64_t *)(ts_gicd + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return value;
}

/*
 * Interrupt ownership: the IOService that matched this node's ADT entry owns
 * the GIC line (registration, enable and mask). Until it claims the source --
 * or on a tree whose node carries no interrupt specifier -- the worker keeps
 * the legacy raw GICD path, so a controller-less boot still behaves as before.
 */
void (*ts_irq_mask_fn)(void);
void (*ts_irq_unmask_fn)(void);
void (*ts_irq_ack_fn)(void);
bool (*ts_irq_prepare_fn)(void);
uint32_t (*ts_irq_cfg_fn)(void);

/*
 * Interrupt-level ownership of the distributor bit. The facade's mask is
 * IOService::disableInterrupt(), which walks the shared interrupt controller:
 * IOSharedInterruptController::disableInterrupt takes controllerLock, and its
 * dispatch path already holds that lock when a vector is soft-disabled, so the
 * acquisition is a recursive hwlock and panics (caller ...disableInterrupt+0x4c).
 * A line that belongs to this driver alone does not need that lock: the
 * distributor bit is the same mask, written directly, and the worker re-arms it
 * the same way. The facade still enables the source once, in ts_irq_arm().
 */
static bool ts_irq_line_owned;
/* Mode 3: the TLMM controller owns the summary SPI and demuxes the ATTENTION
 * pad. This worker asks its handle for pad operations; the controller retains
 * the b22 whole-summary gate until the later leaf-masking stage. */
static bool ts_irq_line_delegated;

void
sweet_touch_irq_own_line(void)
{
	ts_irq_line_owned = true;
}

void
sweet_touch_irq_release_line(void)
{
	ts_irq_line_owned = false;
}

void
sweet_touch_irq_delegate_line(unsigned delegated)
{
	ts_irq_line_delegated = delegated != 0;
}

static void
ts_irq_mask_raw(void)
{
	ts_gic_wr(0x180u + 4u * TS_IRQ_WORD, TS_IRQ_BIT);
}

static void
ts_irq_unmask_raw(void)
{
	ts_gic_wr(0x100u + 4u * TS_IRQ_WORD, TS_IRQ_BIT);
}

static void ts_irq_mask(void);
static void ts_irq_unmask(void);

/*
 * The distributor bit and the controller's software disable are two flags.
 * registerInterrupt() leaves the vector soft disabled; only the controller's
 * enableInterrupt() clears that flag, and dispatch() refuses a vector while it
 * is set. A raw ISENABLER write is therefore not an enable: the probe is the
 * first enable, so it must pass through the owner exactly like ts_irq_arm().
 * Repeated calls are no-ops in IOSharedInterruptController::enableInterrupt.
 */
static void
ts_irq_enable_through_owner(void)
{
	if (ts_irq_line_owned && ts_irq_unmask_fn) {
		ts_irq_unmask_fn();
	}
	ts_irq_unmask();
}

static void
ts_irq_mask(void)
{
	if (ts_irq_line_delegated) {
		if (ts_irq_mask_fn) ts_irq_mask_fn();
		return;
	}
	if (ts_irq_line_owned && ts_gicd) {
		ts_irq_mask_raw();
	} else if (ts_irq_mask_fn) {
		ts_irq_mask_fn();
	} else {
		ts_irq_mask_raw();
	}
}

static void
ts_irq_unmask(void)
{
	if (ts_irq_line_delegated) {
		if (ts_irq_unmask_fn) ts_irq_unmask_fn();
		return;
	}
	if (ts_irq_line_owned && ts_gicd) {
		ts_irq_unmask_raw();
	} else if (ts_irq_unmask_fn) {
		ts_irq_unmask_fn();
	} else {
		ts_irq_unmask_raw();
	}
}

/*
 * Clear the ATTENTION pad's status latch. In the pad path the handle that owns
 * the pad acks it; the worker must not write INTR_STATUS directly then, because
 * the controller leaves that latch for the owner on purpose.
 */
static uint32_t
ts_irq_pad_cfg(void)
{
	if (ts_irq_line_delegated) {
		return ts_irq_cfg_fn ? ts_irq_cfg_fn() : ~0u;
	}
	return ts_rd(ts_tiles[ts_tile(TS_PIN_IRQ)], TS_INTR_CFG);
}

static void
ts_irq_ack_pad(void)
{
	if (ts_irq_line_delegated) {
		if (ts_irq_ack_fn) ts_irq_ack_fn();
		return;
	}
	{
		vm_offset_t tile = ts_tiles[ts_tile(TS_PIN_IRQ)];
		uint32_t status = ts_rd(tile, TS_INTR_STATUS);
		if (status & 1u) {
			ts_wr(tile, TS_INTR_STATUS, status & ~1u);
		}
	}
}

static bool
ts_gic_wait(void)
{
	for (unsigned i = 0; i < 1000; ++i) {
		if ((ts_gic_rd(0) & (1u << 31)) == 0) {
			return true;
		}
		delay(1);
	}
	return false;
}

static bool
ts_gic_configure(void)
{
	unsigned off = 4u * TS_IRQ_WORD;
	uint32_t typer = ts_gic_rd(0x004u);
	if (((typer & 0x1fu) + 1u) * 32u <= TS_IRQ_INTID) {
		return false;
	}
	/* Priority, trigger, route and Group 1 are programmed by the interrupt
	 * controller when the service registers this source; verify them. */
	unsigned cfgOff = 0xc00u + 4u * (TS_IRQ_INTID / 16u);
	unsigned cfgShift = 2u * (TS_IRQ_INTID % 16u);
	unsigned priOff = 0x400u + (TS_IRQ_INTID & ~3u);
	unsigned priShift = 8u * (TS_IRQ_INTID & 3u);
	uint32_t ctlr = ts_gic_rd(0);
	uint32_t group = ts_gic_rd(0x080u + off);
	uint32_t cfg = (ts_gic_rd(cfgOff) >> cfgShift) & 3u;
	uint32_t pri = (ts_gic_rd(priOff) >> priShift) & 0xffu;
	uint64_t route = ts_gic_rd64(0x6000u + 8u * TS_IRQ_INTID);
	SWEET_LOG_DEBUG("SWEET TOUCH IRQ: GIC intid=%u ctlr=%#x group-read=%u cfg=%u pri=%#x route=%#llx\n",
	    TS_IRQ_INTID, ctlr, (group & TS_IRQ_BIT) ? 1 : 0, cfg, pri, route);
	/* IGROUPR readback was zero for proven key and USB Group 1 IRQs. */
	return (ctlr & 2u) != 0 && cfg == 0 && pri == 0xa0u && route == 0;
}

static bool
ts_gic_probe(void)
{
	unsigned off = 4u * TS_IRQ_WORD;
	uint32_t pinCfg = ts_irq_pad_cfg();
	uint32_t pending = ts_gic_rd(0x200u + off) & TS_IRQ_BIT;
	uint32_t active = ts_gic_rd(0x300u + off) & TS_IRQ_BIT;
	if ((pinCfg & 1u) || pending || active) {
		SWEET_LOG_WARN("SWEET TOUCH IRQ: probe refused pin-enable=%u pending=%u active=%u\n",
		    pinCfg & 1u, pending ? 1 : 0, active ? 1 : 0);
		return false;
	}
	__atomic_store_n(&ts_irq_probe_hits, 0u, __ATOMIC_RELEASE);
	__atomic_store_n(&ts_irq_state, 4u, __ATOMIC_RELEASE);
	ts_irq_enable_through_owner();
	if (!ts_gic_wait() || (ts_gic_rd(0x100u + off) & TS_IRQ_BIT) == 0) {
		__atomic_store_n(&ts_irq_state, 3u, __ATOMIC_RELEASE);
		ts_irq_mask();
		SWEET_LOG_ERROR("SWEET TOUCH IRQ: GIC probe enable failed\n");
		return false;
	}
	ts_gic_wr(0x200u + off, TS_IRQ_BIT);
	for (unsigned i = 0; i < 1000; ++i) {
		if (__atomic_load_n(&ts_irq_probe_hits, __ATOMIC_ACQUIRE)) {
			break;
		}
		delay(100);
	}
	/* Read before clearing: a bit still pending means the distributor
	 * accepted the request and no handler ever ran (dispatch refused), not
	 * that the line was never signalled. */
	uint32_t pendingAfter = ts_gic_rd(0x200u + off) & TS_IRQ_BIT;
	__atomic_store_n(&ts_irq_state, 3u, __ATOMIC_RELEASE);
	ts_irq_mask();
	ts_gic_wr(0x280u + off, TS_IRQ_BIT);
	bool clear = ts_gic_wait();
	for (unsigned i = 0; clear && i < 1000; ++i) {
		pending = ts_gic_rd(0x200u + off) & TS_IRQ_BIT;
		active = ts_gic_rd(0x300u + off) & TS_IRQ_BIT;
		if (!pending && !active) {
			break;
		}
		if (i == 999) {
			clear = false;
		} else {
			delay(1);
		}
	}
	unsigned hits = __atomic_load_n(&ts_irq_probe_hits, __ATOMIC_ACQUIRE);
	SWEET_LOG_INFO("SWEET TOUCH IRQ: software-pending probe hits=%u clear=%u pending=%u active=%u pending-after=%u\n",
	    hits, clear ? 1 : 0, pending ? 1 : 0, active ? 1 : 0,
	    pendingAfter ? 1 : 0);
	return hits == 1u && clear;
}

/* GIC top half: mask the shared TLMM summary SPI before EOI, then wake
 * the touch worker. I2C, TLMM status ack, and HID run only on that worker. */
bool
sweet_touch_irq(unsigned int intid)
{
	if (intid != TS_IRQ_INTID) {
		return false;
	}
	unsigned state = __atomic_load_n(&ts_irq_state, __ATOMIC_ACQUIRE);
	if (state == 4u) {
		ts_irq_mask();
		__atomic_add_fetch(&ts_irq_probe_hits, 1u, __ATOMIC_RELEASE);
		return true;
	}
	unsigned expected = 1u;
	if (__atomic_compare_exchange_n(&ts_irq_state, &expected, 2u, false,
	    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		ts_irq_mask();
		__atomic_add_fetch(&ts_irq_hits, 1u, __ATOMIC_RELEASE);
		thread_wakeup(&ts_irq_chan);
		return true;
	}
	/* Parked callbacks never touch registers. Mask/active confirmation happened
  * before QUIESCED was published; a late callback cannot unpark the worker. */
 if (expected == 5u) return true;
 if (expected == 2u || expected == 3u) {
		/* A delegated run has no distributor bit of its own: the pad mask
		 * above already quiets this pad, and ICENABLER here would silently
		 * disable the shared summary line behind its owner's back. */
		if (!ts_irq_line_delegated) {
			ts_gic_wr(0x180u + 4u * TS_IRQ_WORD, TS_IRQ_BIT);
		}
		return true;
	}
	return false;
}

/*
 * GIC top half entry for the registered action: the controller dispatches the
 * INTID, the handler runs exactly like the legacy predicate did.
 */
void
sweet_touch_irq_action(void *target, void *refCon, void *nub, int source)
{
	(void)target;
	(void)refCon;
	(void)nub;
	(void)source;  /* IOInterruptController passes the device source index */
	(void)sweet_touch_irq(TS_IRQ_INTID);
}

static void
ts_irq_wait(unsigned observed)
{
	(void)assert_wait(&ts_irq_chan, THREAD_UNINT);
	if (__atomic_load_n(&ts_irq_hits, __ATOMIC_ACQUIRE) != observed ||
	    __atomic_load_n(&ts_irq_state, __ATOMIC_ACQUIRE) == 3u ||
     (__atomic_load_n(&ts_panel_frame,__ATOMIC_ACQUIRE)&15u)!=SWEET_TOUCH_RUNNING ||
     __atomic_load_n(&ts_panel_cancel,__ATOMIC_ACQUIRE)) {
		clear_wait(current_thread(), THREAD_AWAKENED);
	}
	thread_block(THREAD_CONTINUE_NULL);
}

static void
ts_irq_fail(const char *reason)
{
	__atomic_store_n(&ts_irq_state, 3u, __ATOMIC_RELEASE);
	if (!ts_irq_line_delegated) {
		vm_offset_t tile = ts_tiles[ts_tile(TS_PIN_IRQ)];
		ts_wr(tile, TS_INTR_CFG, ts_rd(tile, TS_INTR_CFG) & ~1u);
	}
	if (ts_gicd || ts_irq_line_delegated) {
		ts_irq_mask();
	}
	SWEET_LOG_ERROR("SWEET TOUCH IRQ: FAILED %s hits=%u empty=%u; touch stopped\n",
	    reason, __atomic_load_n(&ts_irq_hits, __ATOMIC_ACQUIRE), ts_irq_empty);
}

static bool
ts_irq_arm(void)
{
	if (!ts_irq_ready) {
		return false;
	}
	vm_offset_t tile = ts_tiles[ts_tile(TS_PIN_IRQ)];
	uint32_t cfg = 0;
	if (ts_irq_line_delegated) {
		/* Prepare at the same arm point as the legacy programming below,
		 * not at claim during display bring-up. The handle owns all writes. */
		if (!ts_irq_prepare_fn || !ts_irq_cfg_fn || !ts_irq_prepare_fn()) {
			ts_irq_fail("handle preparation");
			return false;
		}
	} else {
		cfg = ts_rd(tile, TS_INTR_CFG);
		/* Linux SM8150 TLMM: target=KPSS 3, raw status on, level low,
		 * pin interrupt disabled until the GIC callback and worker are ready. */
		cfg = (cfg & ~0xffu) | (3u << 5) | (1u << 4);
		ts_wr(tile, TS_INTR_CFG, cfg);
		uint32_t status = ts_rd(tile, TS_INTR_STATUS);
		ts_wr(tile, TS_INTR_STATUS, status & ~1u);
		uint32_t readback = ts_rd(tile, TS_INTR_CFG);
		if ((readback & 0xffu) != (cfg & 0xffu)) {
			ts_irq_fail("TLMM config readback");
			return false;
		}
	}
	if (ts_panel_interrupted()) return false;
 __atomic_store_n(&ts_irq_state, 1u, __ATOMIC_RELEASE);
	/* Same owner-first enable as the probe; the distributor bit stays ours. */
	ts_irq_enable_through_owner();
	bool waited = ts_gic_wait();
	bool enabled = (ts_gic_rd(0x100u + 4u * TS_IRQ_WORD) & TS_IRQ_BIT) != 0;
	unsigned state = __atomic_load_n(&ts_irq_state, __ATOMIC_ACQUIRE);
	if (!waited || (!enabled && state == 1u) || state == 3u) {
		ts_irq_fail("GIC enable readback");
		return false;
	}
	if (ts_panel_interrupted()) return false;
	if (!ts_irq_line_delegated) {
		ts_wr(tile, TS_INTR_CFG, cfg | 1u);
		if ((ts_rd(tile, TS_INTR_CFG) & 1u) == 0) {
			ts_irq_fail("TLMM enable readback");
			return false;
		}
	} else {
		uint32_t padCfg = ts_irq_pad_cfg();
		if (padCfg == ~0u || !(padCfg & 1u)) {
			/* First-packet readback races are a separate stage. */
			ts_irq_fail("handle enable readback");
			return false;
		}
	}
	SWEET_LOG_INFO("SWEET TOUCH IRQ: armed gpio=%u intid=%u cfg=%#x status=%#x attn=%u\n",
	    TS_PIN_IRQ, TS_IRQ_INTID, ts_irq_pad_cfg(),
	    ts_rd(tile, TS_INTR_STATUS), ts_gpio_io_rd(TS_PIN_IRQ) & 1u);
	return true;
}

/* mux 0 = GPIO, pull 0 none / 1 down / 3 up, drive 3 = 8 mA. */
static void
ts_gpio(unsigned pin, unsigned mux, unsigned pull, unsigned drive, bool out, bool high)
{
	uint32_t cfg = ts_gpio_rd(pin);
	if (out) {
		/* Latch the level before switching the pin to output. */
		ts_gpio_io_wr(pin, (ts_gpio_io_rd(pin) & ~2u) | (high ? 2u : 0u));
	}
	cfg &= ~0x3ffu;
	cfg |= (mux << 2) | pull | (drive << 6) | (out ? 0x200u : 0u);
	ts_gpio_wr(pin, cfg);
}

/* ---- bit-banged I2C (sweet_touch_sec.c, about 100 kHz) ---- */

static unsigned bb_sda_pin = TS_PIN_I2C_A, bb_scl_pin = TS_PIN_I2C_B;
static unsigned ts_i2c_errors;

/* "low" drives 0, "release" is an input (the external pull-up sets the level). */
static void
bb_level(unsigned pin, bool low)
{
	uint32_t cfg = ts_gpio_rd(pin) & ~0x3ffu;
	if (low) {
		ts_gpio_io_wr(pin, ts_gpio_io_rd(pin) & ~2u);
		cfg |= 0x200u;
	}
	ts_gpio_wr(pin, cfg);
}

static void bb_sda(unsigned v) { bb_level(bb_sda_pin, !v); }
static void bb_scl(unsigned v) { bb_level(bb_scl_pin, !v); }
static int bb_sda_read(void) { return (int)(ts_gpio_io_rd(bb_sda_pin) & 1u); }
static void bb_delay(void) { delay(4); }

static void
bb_start(void)
{
	bb_sda(1);
	bb_scl(1);
	bb_delay();
	bb_sda(0);
	bb_delay();
	bb_scl(0);
	bb_delay();
}

static void
bb_stop(void)
{
	bb_sda(0);
	bb_delay();
	bb_scl(1);
	bb_delay();
	bb_sda(1);
	bb_delay();
}

static void
bb_tx_byte(uint8_t b)
{
	for (int i = 7; i >= 0; --i) {
		bb_sda((b >> i) & 1u);
		bb_delay();
		bb_scl(1);
		bb_delay();
		bb_scl(0);
		bb_delay();
	}
}

static uint8_t
bb_rx_byte(bool ack)
{
	uint8_t v = 0;

	bb_sda(1);
	for (int i = 7; i >= 0; --i) {
		bb_delay();
		bb_scl(1);
		bb_delay();
		v = (uint8_t)((v << 1) | (uint8_t)bb_sda_read());
		bb_scl(0);
	}
	bb_sda(ack ? 0 : 1);
	bb_delay();
	bb_scl(1);
	bb_delay();
	bb_scl(0);
	bb_sda(1);
	bb_delay();
	return v;
}

/* Returns 0 when the target acknowledged the byte just sent. */
static int
bb_ack(void)
{
	int ack;
	bb_sda(1);
	bb_delay();
	bb_scl(1);
	bb_delay();
	ack = bb_sda_read();
	bb_scl(0);
	bb_delay();
	return ack ? ENXIO : 0;
}

/* Clock nine pulses so a target holding SDA low can let go. */
static void
bb_recover(void)
{
	bb_sda(1);
	for (unsigned i = 0; i < 9; ++i) {
		bb_scl(0);
		bb_delay();
		bb_scl(1);
		bb_delay();
	}
	bb_stop();
}

/* TouchComm transfers: no register address on I2C. */
static int
ts_i2c_write(const uint8_t *tx, size_t n)
{
 if (ts_panel_interrupted()) return ECANCELED;
	int err;
	bb_start();
	bb_tx_byte((uint8_t)(TS_ADDR << 1));
	err = bb_ack();
	for (size_t i = 0; i < n && err == 0; ++i) {
		if (ts_panel_interrupted()) { err=ECANCELED; break; }
		bb_tx_byte(tx[i]);
		err = bb_ack();
	}
	bb_stop();
	if (err) {
		ts_i2c_errors++;
	}
	return err;
}

static int
ts_i2c_read(uint8_t *rx, size_t n)
{
 if (ts_panel_interrupted()) return ECANCELED;
	int err;
	bb_start();
	bb_tx_byte((uint8_t)((TS_ADDR << 1) | 1u));
	err = bb_ack();
	for (size_t i = 0; i < n && err == 0; ++i) {
		if (ts_panel_interrupted()) { err=ECANCELED; break; }
		rx[i] = bb_rx_byte(i + 1u < n);
	}
	bb_stop();
	if (err) {
		ts_i2c_errors++;
	}
	return err;
}

/* ---- TouchComm (from PaganiInput/S3910.c, see the header comment) ---- */

static struct {
	uint16_t max_x, max_y, config_size, max_write, max_read;
	uint8_t mode, code, ready, crc, version, sequence;
	uint16_t length;
	uint8_t command;
	const char *stage;
	uint32_t detail;
	uint8_t payload[TCM_PAYLOAD_MAX];
} tcm;

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }

/* Report format: object count (0x18, 8 bits), then per object index (6),
 * classification (7), x (8, 16 bits), y (9, 16 bits); end (3). */
static const uint8_t tcm_format[] = {0x18, 8, 1, 6, 8, 7, 8, 8, 16, 9, 16, 3, 0};

static uint8_t
crc6(const uint8_t *p, unsigned bits)
{
	uint8_t crc = 0x3f;
	for (unsigned i = 0; i < bits; ++i) {
		unsigned bit = ((crc >> 5) ^ (p[i / 8] >> (7 - i % 8))) & 1;
		crc = (uint8_t)(((crc << 1) & 0x3f) ^ (bit ? 3 : 0));
	}
	return crc;
}

static uint16_t
crc16(const uint8_t *p, unsigned size)
{
	uint16_t crc = 0xffff;
	for (unsigned i = 0; i < size; ++i) {
		crc ^= (uint16_t)p[i] << 8;
		for (unsigned b = 0; b < 8; ++b) {
			crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
		}
	}
	return crc;
}

static int
write_v2(uint8_t cmd, const uint8_t *data, unsigned length)
{
	static uint8_t packet[TCM_PAYLOAD_MAX + 6];
	unsigned size = 4 + length + (length ? 2 : 0);
	tcm.stage = "write";
	tcm.detail = length;
	if (length > TCM_PAYLOAD_MAX || (length && !data) || (tcm.max_write && size > tcm.max_write)) {
		return -90;
	}
	packet[0] = cmd;
	packet[1] = (uint8_t)length;
	packet[2] = (uint8_t)(length >> 8);
	packet[3] = (uint8_t)((tcm.sequence++ & 1) << 6);
	packet[3] |= crc6(packet, 26);
	if (length) {
		memcpy(packet + 4, data, length);
		uint16_t crc = crc16(packet, length + 4);
		packet[length + 4] = (uint8_t)(crc >> 8);
		packet[length + 5] = (uint8_t)crc;
	}
	return ts_i2c_write(packet, size);
}

static int
response_v2(void)
{
	/* One driver thread: static keeps the buffer off the 16 KiB kernel stack. */
	static uint8_t body[TCM_PAYLOAD_MAX + 6];
	uint8_t head[4];
	tcm.stage = "header";
	tcm.detail = 0;
	int rc = ts_i2c_read(head, sizeof head);
	if (rc) {
		return rc;
	}
	tcm.detail = le32(head);
	if (crc6(head, 32)) {
		return -74;
	}
	tcm.code = head[0];
	tcm.length = le16(head + 1);
	int discard = tcm.length > TCM_PAYLOAD_MAX;
	if (discard && (tcm.ready || tcm.code != 0x23)) {
		return -90;
	}
	unsigned cap = tcm.max_read ? tcm.max_read : 256;
	if (cap > sizeof body) {
		cap = sizeof body;
	}
	if (cap <= 6) {
		tcm.stage = "read limit";
		tcm.detail = cap;
		return -90;
	}
	for (unsigned offset = 0; offset < tcm.length;) {
		unsigned size = tcm.length - offset;
		if (size > cap - 6) {
			size = cap - 6;
		}
		for (unsigned retry = 0; retry < 5; ++retry) {
			rc = write_v2(retry ? 8 : 7, NULL, 0);
			if (rc) {
				return rc;
			}
			tcm.stage = "continued";
			tcm.detail = 0;
			rc = ts_i2c_read(body, size + 6);
			if (rc) {
				return rc;
			}
			tcm.detail = le32(body);
			if (!crc6(body, 32) && !crc16(body, size + 6)) {
				break;
			}
			tcm.stage = "body CRC";
			if (retry == 4) {
				return -74;
			}
		}
		if (!discard) {
			memcpy(tcm.payload + offset, body + 4, size);
		}
		offset += size;
	}
	return 0;
}

static int
exchange_v2(uint8_t cmd, const uint8_t *data, unsigned length)
{
	uint8_t sequence = tcm.sequence;
	for (unsigned retry = 0; retry < 5; ++retry) {
		/* A requested retransmission keeps the original sequence bit. */
		tcm.sequence = sequence;
		int rc = write_v2(cmd, data, length);
		if (rc) {
			return rc;
		}
		ts_wait(1);
		rc = response_v2();
		if (rc) {
			return rc;
		}
		if (tcm.code != 8 && tcm.code != 0xfe) {
			return 0;
		}
	}
	tcm.stage = "retry";
	tcm.detail = tcm.code;
	return -5;
}

static int
request_v2(uint8_t cmd, const uint8_t *data, unsigned length)
{
	unsigned cap = tcm.max_write ? tcm.max_write : TCM_PAYLOAD_MAX + 6, offset = 0;
	if (cap > TCM_PAYLOAD_MAX + 6) {
		cap = TCM_PAYLOAD_MAX + 6;
	}
	tcm.stage = "write limit";
	tcm.detail = cap;
	if (length > TCM_PAYLOAD_MAX || (length && (!data || cap <= 6)) || cap < 4) {
		return -90;
	}
	do {
		unsigned size = length - offset;
		if (size && size > cap - 6) {
			size = cap - 6;
		}
		int rc = exchange_v2(offset ? 1 : cmd, data ? data + offset : NULL, size);
		if (rc) {
			return rc;
		}
		if (tcm.code == 0x10 && cmd != 2) {
			tcm.stage = "restart";
			return -104;
		}
		if (tcm.code < 0x10 && tcm.code != 1 && tcm.code != 4 && tcm.code != 7) {
			tcm.stage = "reply";
			tcm.detail = tcm.code;
			return -5;
		}
		offset += size;
	} while (offset < length);
	return 0;
}

static int
message(void)
{
	static uint8_t body[TCM_PAYLOAD_MAX + 7];
	uint8_t head[4];

	if (tcm.version == 2) {
		return exchange_v2(0x0a, NULL, 0);
	}
	tcm.stage = "header";
	tcm.detail = 0;
	int rc = ts_i2c_read(head, sizeof head);
	if (rc) {
		return rc;
	}
	tcm.detail = le32(head);
	if (head[0] != 0xa5) {
		if (!tcm.version && tcm.detail && tcm.detail != 0xffffffff && !crc6(head, 32)) {
			tcm.version = 2;    /* identify through a fresh v2 command */
			return 0;
		}
		return -71;
	}
	tcm.version = 1;
	tcm.code = head[1];
	tcm.length = le16(head + 2);
	if (tcm.length > TCM_PAYLOAD_MAX) {
		/* A queued oversized report can precede a command response after reset. */
		if (tcm.ready || tcm.code != 0x23) {
			return -90;
		}
		unsigned left = tcm.length;
		while (left) {
			unsigned size = left > TCM_PAYLOAD_MAX ? TCM_PAYLOAD_MAX : left;
			tcm.stage = "continued";
			tcm.detail = 0;
			rc = ts_i2c_read(body, size + 2 + (size == left ? 5 : 0));
			if (rc) {
				return rc;
			}
			tcm.detail = le32(body);
			if (body[0] != 0xa5 || body[1] != 3) {
				return -71;
			}
			if (size == left && body[size + 2] != 0x5a) {
				tcm.stage = "padding";
				tcm.detail = body[size + 2];
				return -71;
			}
			left -= size;
		}
		return 0;
	}
	if (!tcm.length) {
		return 0;
	}
	/* The continued read repeats a5 03, then payload, 0x5a, optional CRC/RC, 0x5a. */
	tcm.stage = "continued";
	tcm.detail = 0;
	rc = ts_i2c_read(body, tcm.length + 7u);
	if (rc) {
		return rc;
	}
	tcm.detail = le32(body);
	if (body[0] != 0xa5 || body[1] != 3) {
		return -71;
	}
	if (body[tcm.length + 2] != 0x5a) {
		tcm.stage = "padding";
		tcm.detail = body[tcm.length + 2];
		return -71;
	}
	if (tcm.code == 0x10) {
		tcm.crc = le16(body + tcm.length + 3) != 0x5a5a;
	}
	memcpy(tcm.payload, body + 2, tcm.length);
	return 0;
}

static int
command(uint8_t cmd, const uint8_t *data, unsigned length)
{
	static uint8_t packet[TCM_PAYLOAD_MAX + 5];
	int rc;

	tcm.command = cmd;
	tcm.stage = "write";
	tcm.detail = length;
	if (tcm.version == 2) {
		rc = request_v2(cmd, data, length);
	} else {
		if (length > TCM_PAYLOAD_MAX || (length && !data) ||
		    (tcm.max_write && length + 3 + 2 * tcm.crc > tcm.max_write)) {
			return -90;
		}
		packet[0] = cmd;
		packet[1] = (uint8_t)length;
		packet[2] = (uint8_t)(length >> 8);
		if (length) {
			memcpy(packet + 3, data, length);
		}
		unsigned size = length + 3;
		if (tcm.crc) {
			uint16_t crc = crc16(packet, size);
			packet[size++] = (uint8_t)crc;
			packet[size++] = (uint8_t)(crc >> 8);
		}
		rc = ts_i2c_write(packet, size);
	}
	if (rc) {
		return rc;
	}
	for (unsigned n = 0; n < 300; ++n) {
 if (ts_panel_interrupted()) return ECANCELED;
		if (tcm.version != 2 || n) {
			ts_wait(10);
			rc = message();
			if (rc) {
				return rc;
			}
		}
		if (tcm.code == 1 || (cmd == 2 && tcm.code == 0x10)) {
			return 0;
		}
		if (tcm.code == 0x10) {
			tcm.stage = "restart";
			return -104;
		}
		if (tcm.code >= 0xfe || (tcm.code && tcm.code < 0x10 && tcm.code != 4 && tcm.code != 7)) {
			tcm.stage = "reply";
			tcm.detail = tcm.code;
			return -5;
		}
	}
	tcm.stage = "timeout";
	return -110;
}

static void
tcm_fail(int rc)
{
	SWEET_LOG_WARN("SWEET TOUCH: TouchComm v%u cmd %#x failed at \"%s\" rc=%d detail=%#x code=%#x len=%u i2c-errors=%u\n",
	    tcm.version, tcm.command, tcm.stage ? tcm.stage : "?", rc, tcm.detail, tcm.code,
	    tcm.length, ts_i2c_errors);
}

/*
 * Identify, application info, then the touch report format. Returns 0 when
 * touch reports (0x11) are enabled in the format tcm_decode() expects.
 */
static int
tcm_start(void)
{
	int rc;

	memset(&tcm, 0, sizeof tcm);
	rc = message();
	if (rc) {
		return rc;
	}
	if (tcm.code != 0x10) {
		rc = command(2, NULL, 0);           /* IDENTIFY */
		if (rc) {
			return rc;
		}
	}
	tcm.stage = "identify";
	tcm.detail = tcm.length;
	if (tcm.length < 24) {
		return -71;
	}
	char part[17];
	memcpy(part, tcm.payload + 2, 16);
	part[16] = 0;
	for (unsigned i = 0; i < 16; ++i) {
		if (part[i] && (part[i] < 0x20 || part[i] > 0x7e)) {
			part[i] = '?';
		}
	}
	tcm.mode = tcm.payload[1];
	tcm.max_write = le16(tcm.payload + 22);
	if (tcm.version == 2 && tcm.length >= 26) {
		tcm.max_read = le16(tcm.payload + 24);
	}
	SWEET_LOG_INFO("SWEET TOUCH: TouchComm v%u identify: proto %u mode %u part \"%s\" build %u max-write %u max-read %u crc %u\n",
	    tcm.version, tcm.payload[0], tcm.mode, part,
	    tcm.length >= 22 ? (unsigned)le32(tcm.payload + 18) : 0u, tcm.max_write, tcm.max_read, tcm.crc);
	tcm.detail = tcm.mode;
	if (tcm.mode != 1) {
		/* Bootloader or another mode: the firmware is left untouched. */
		SWEET_LOG_ERROR("SWEET TOUCH: controller not in application mode (%u); touch withheld\n", tcm.mode);
		return -19;
	}
	rc = command(0x20, NULL, 0);                /* GET_APPLICATION_INFO */
	if (rc) {
		return rc;
	}
	tcm.stage = "app info";
	tcm.detail = tcm.length;
	if (tcm.length < 38) {
		return -71;
	}
	unsigned status = le16(tcm.payload + 2);
	tcm.config_size = le16(tcm.payload + 12);
	tcm.max_x = le16(tcm.payload + 32);
	tcm.max_y = le16(tcm.payload + 34);
	unsigned objects = le16(tcm.payload + 36);
	SWEET_LOG_INFO("SWEET TOUCH: app info status %u config %u max %ux%u objects %u\n",
	    status, tcm.config_size, tcm.max_x, tcm.max_y, objects);
	tcm.detail = status;
	if (status) {
		return -71;
	}
	tcm.stage = "limits";
	tcm.detail = (uint32_t)tcm.config_size << 16 | objects;
	if (!tcm.max_x || !tcm.max_y || !objects || tcm.config_size < sizeof tcm_format ||
	    tcm.config_size > TCM_PAYLOAD_MAX) {
		return -71;
	}
	uint8_t report = 0x11;
	rc = command(6, &report, 1);                /* DISABLE_REPORT touch */
	if (rc) {
		return rc;
	}
	/* The format lives in controller RAM only; a reset restores the default. */
	static uint8_t config[TCM_PAYLOAD_MAX];
	memset(config, 0, sizeof config);
	memcpy(config, tcm_format, sizeof tcm_format);
	rc = command(0x26, config, tcm.config_size);        /* SET_TOUCH_REPORT_CONFIG */
	if (rc) {
		return rc;
	}
	rc = command(0x25, NULL, 0);                /* GET_TOUCH_REPORT_CONFIG */
	if (rc) {
		return rc;
	}
	tcm.stage = "format";
	tcm.detail = tcm.length >= 4 ? le32(tcm.payload) : tcm.length;
	if (tcm.length < sizeof tcm_format || memcmp(tcm.payload, tcm_format, sizeof tcm_format)) {
		return -71;
	}
	report = 0x11;
	rc = command(5, &report, 1);                /* ENABLE_REPORT touch */
	if (rc) {
		return rc;
	}
	tcm.ready = 1;
	return 0;
}

struct tcm_contact { uint16_t x, y; uint8_t active; };

/* Object count, then per object: index, classification, x, y (16-bit LE). */
static int
tcm_decode(const uint8_t *p, size_t length, struct tcm_contact *out)
{
	if (!length || p[0] > TCM_CONTACTS || length != 1u + 6u * p[0]) {
		return -71;
	}
	uint16_t seen = 0;
	memset(out, 0, sizeof(*out) * TCM_CONTACTS);
	for (unsigned n = 0; n < p[0]; ++n) {
		const uint8_t *c = p + 1 + n * 6;
		unsigned id = c[0];
		if (id >= TCM_CONTACTS || (seen & (1u << id))) {
			return -71;
		}
		seen |= (uint16_t)(1u << id);
		uint16_t x = le16(c + 2), y = le16(c + 4);
		if (x > tcm.max_x || y > tcm.max_y) {
			return -71;
		}
		/* Finger and glove are contacts; palms never become fingers. */
		if (c[1] != 1 && c[1] != 2) {
			continue;
		}
		out[id].active = 1;
		out[id].x = x;
		out[id].y = y;
	}
	return 0;
}

/* ---- driver thread ---- */

static struct tcm_contact ts_now[TCM_CONTACTS], ts_prev[TCM_CONTACTS];
static int ts_px[TCM_CONTACTS], ts_py[TCM_CONTACTS];
static unsigned ts_flip;        /* boot-arg sweettouchflip: 1 = mirror x, 2 = mirror y, 4 = swap x/y */
static unsigned ts_events;

static void
ts_map(const struct tcm_contact *c, int *px, int *py)
{
	unsigned x = c->x, y = c->y, mx = tcm.max_x, my = tcm.max_y;
	if (ts_flip & 4u) {
		unsigned t = x; x = y; y = t;
		t = mx; mx = my; my = t;
	}
	if (ts_flip & 1u) {
		x = mx - x;
	}
	if (ts_flip & 2u) {
		y = my - y;
	}
	*px = (int)((uint64_t)x * (SWEET_DEV_FB_W - 1) / mx);
	*py = (int)((uint64_t)y * (SWEET_DEV_FB_H - 1) / my);
}

static void
ts_report(void)
{
	static unsigned moves;
	for (unsigned id = 0; id < TCM_CONTACTS; ++id) {
		if (ts_now[id].active) {
			int x, y;
			ts_map(&ts_now[id], &x, &y);
			ts_px[id] = x;
			ts_py[id] = y;
			ts_events++;
			if (!ts_prev[id].active || (++moves % 25u) == 0) {
				SWEET_LOG_DEBUG("SWEET TOUCH: f%u %s raw %u,%u -> %d,%d\n", id,
				    ts_prev[id].active ? "move" : "down", ts_now[id].x, ts_now[id].y, x, y);
			}
			sweet_hid_touch((int)id, 1, x, y);
		} else if (ts_prev[id].active) {
			ts_events++;
			SWEET_LOG_DEBUG("SWEET TOUCH: f%u up %d,%d\n", id, ts_px[id], ts_py[id]);
			sweet_hid_touch((int)id, 0, ts_px[id], ts_py[id]);
		}
		ts_prev[id] = ts_now[id];
	}
}

static void
ts_release_all(void)
{
	memset(ts_now, 0, sizeof ts_now);
	ts_report();
}

/* Disable only our GPIO122 source and the already-owned summary line. No
 * transport/rail/reset writes. Wait outside a spinlock for RWP and EOI/active;
 * peripheral mask must be read back before declaring a drained worker. */
static bool ts_panel_mask(void)
{
 __atomic_store_n(&ts_irq_state,5u,__ATOMIC_RELEASE);
 vm_offset_t tile=ts_tiles[ts_tile(TS_PIN_IRQ)];
 uint32_t config=ts_irq_pad_cfg();
 if (config==~0u) {
  ts_irq_mask();
  SWEET_LOG_ERROR("SWEET TOUCH PANEL: GPIO122 IRQ configuration unreadable\n");return false;
 }
 ts_panel_pin_cfg=config&~1u;
 if (!ts_irq_line_delegated) ts_wr(tile,TS_INTR_CFG,ts_panel_pin_cfg);
 ts_irq_mask();
 for (unsigned n=0;n<21;++n) {
  uint32_t readback=ts_irq_pad_cfg();
  if (readback!=ts_panel_pin_cfg) {
   SWEET_LOG_ERROR("SWEET TOUCH PANEL: GPIO122 disable mismatch want=%#x read=%#x\n",ts_panel_pin_cfg,readback);return false;
  }
  /* The pad readback above proves this worker's line is disabled. A delegated
   * run leaves the shared summary SPI enabled for the other pads, so only an
   * owned line has to see its own distributor bit clear. */
  bool lineQuiet=ts_irq_line_delegated ||
      !(ts_gic_rd(0x100u+4u*TS_IRQ_WORD)&TS_IRQ_BIT);
  if (!(ts_gic_rd(0)&(1u<<31)) && lineQuiet &&
      !(ts_gic_rd(0x300u+4u*TS_IRQ_WORD)&TS_IRQ_BIT)) return true;
  if (n!=20) ts_wait(1);
 }
 SWEET_LOG_ERROR("SWEET TOUCH PANEL: IRQ drain deadline ctlr=%#x enabled=%#x active=%#x\n",
     ts_gic_rd(0),ts_gic_rd(0x100u+4u*TS_IRQ_WORD),ts_gic_rd(0x300u+4u*TS_IRQ_WORD));
 return false;
}
static void ts_panel_publish(uint64_t frame,unsigned state)
{ __atomic_store_n(&ts_panel_frame,(frame&~15ULL)|state,__ATOMIC_RELEASE); }
/* 0 run normal IRQ loop, 1 sleep with no GPIO/I2C/HID accesses, 2 exit. */
static unsigned ts_panel_step(void)
{
 uint64_t frame=__atomic_load_n(&ts_panel_frame,__ATOMIC_ACQUIRE);
 unsigned state=(unsigned)(frame&15u);
 if (__atomic_load_n(&ts_panel_cancel,__ATOMIC_ACQUIRE)) {
  (void)ts_panel_mask(); ts_release_all();
  ts_panel_publish(frame,SWEET_TOUCH_STOPPED); return 2;
 }
 if (state==SWEET_TOUCH_QUIESCED) return 1;
 bool resume=state==SWEET_TOUCH_RESUME_REQUESTED;
 if (state!=SWEET_TOUCH_PAUSE_REQUESTED && !resume) return 0;
 uint64_t working=(frame&~15ULL)|(resume ? SWEET_TOUCH_RESUMING : SWEET_TOUCH_PAUSING);
 if (!__atomic_compare_exchange_n(&ts_panel_frame,&frame,working,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return 0;
 frame=working; ts_panel_servicing=true; ts_panel_deadline=ts_panel_now()+4000000000ULL;
 bool ok;
 if (!resume) {
  ok=ts_panel_mask();
  ts_release_all(); /* release every held HID contact before publication */
 } else {
  /* The caller has restored panel/rails. Do not reset touch or cycle its GPIO
   * supplies: re-identify app mode and revalidate report configuration in RAM.
   * IRQ remains masked during all protocol I/O; no contacts are dispatched. */
  vm_offset_t tile=ts_tiles[ts_tile(TS_PIN_IRQ)];
  unsigned old_x=tcm.max_x,old_y=tcm.max_y;
  ok=__atomic_load_n(&ts_irq_state,__ATOMIC_ACQUIRE)==5u &&
      ts_irq_pad_cfg()==ts_panel_pin_cfg &&
      (ts_irq_line_delegated || !(ts_gic_rd(0x100u+4u*TS_IRQ_WORD)&TS_IRQ_BIT));
  if (ok) {
   int rc=tcm_start();
   ok=rc==0 && tcm.ready && tcm.max_x==old_x && tcm.max_y==old_y;
   if (rc) tcm_fail(rc);
   else if (!ok) SWEET_LOG_ERROR("SWEET TOUCH PANEL: resume geometry/ready mismatch old=%ux%u new=%ux%u ready=%u\n",
       old_x,old_y,tcm.max_x,tcm.max_y,tcm.ready);
  }
  if (ok && !ts_panel_interrupted()) {
   /* Source still disabled: discard stale summary pending, then enable the
    * level source only after fresh application/format readback has succeeded.
    * A delegated run never clears the shared pending bit: it also carries the
    * other pads, and the pad status ack in ts_irq_arm() is its own latch. */
   if (!ts_irq_line_delegated) {
    ts_gic_wr(0x280u+4u*TS_IRQ_WORD,TS_IRQ_BIT);
    ok=ts_gic_wait() && !(ts_gic_rd(0x200u+4u*TS_IRQ_WORD)&TS_IRQ_BIT);
   }
   ts_irq_empty=ts_irq_no_progress=0;
   if (ok && !ts_panel_interrupted()) ok=ts_irq_arm();
  }
 }
 bool cancelled=__atomic_load_n(&ts_panel_cancel,__ATOMIC_ACQUIRE)!=0;
 ok=ok && !ts_panel_interrupted(); ts_panel_servicing=false;
 if (!ok) {
  bool masked=ts_panel_mask(); ts_release_all();
  ts_panel_publish(frame,cancelled ? SWEET_TOUCH_STOPPED : SWEET_TOUCH_FAILED);
  if (cancelled) SWEET_LOG_INFO("SWEET TOUCH PANEL: cancelled; irq-masked=%u, touch stopped\n",masked);
  else SWEET_LOG_ERROR("SWEET TOUCH PANEL: %s failed irq-masked=%u; touch withheld, no reset/retry/rail change\n",resume ? "resume" : "quiesce",masked);
  return 2;
 }
 ts_panel_publish(frame,resume ? SWEET_TOUCH_RUNNING : SWEET_TOUCH_QUIESCED);
 SWEET_LOG_INFO("SWEET TOUCH PANEL: token=%llu %s; no low-power/rail proof\n",frame>>4,resume ? "app/format verified, IRQ resumed" : "worker/IRQ/HID quiesced");
 return resume ? 0u : 1u;
}
static void ts_panel_wait(void)
{
 (void)assert_wait(&ts_irq_chan,THREAD_UNINT);
 if ((__atomic_load_n(&ts_panel_frame,__ATOMIC_ACQUIRE)&15u)!=SWEET_TOUCH_QUIESCED ||
     __atomic_load_n(&ts_panel_cancel,__ATOMIC_ACQUIRE)) clear_wait(current_thread(),THREAD_AWAKENED);
 thread_block(THREAD_CONTINUE_NULL);
}

static void
ts_pins(const char *when)
{
	SWEET_LOG_DEBUG("SWEET TOUCH: pins %s rst=%#x/%#x pwr59=%#x/%#x pwr152=%#x/%#x irq=%#x/%#x bus=%#x/%#x\n", when,
	    ts_gpio_rd(TS_PIN_RESET), ts_gpio_io_rd(TS_PIN_RESET),
	    ts_gpio_rd(TS_PIN_PWR_A), ts_gpio_io_rd(TS_PIN_PWR_A),
	    ts_gpio_rd(TS_PIN_PWR_B), ts_gpio_io_rd(TS_PIN_PWR_B),
	    ts_gpio_rd(TS_PIN_IRQ), ts_gpio_io_rd(TS_PIN_IRQ),
	    ts_gpio_io_rd(TS_PIN_I2C_A), ts_gpio_io_rd(TS_PIN_I2C_B));
}

static void
ts_thread(void *arg __unused, wait_result_t wr __unused)
{
	unsigned delay_s = 20;
	(void)PE_parse_boot_argn("sweettouchdelay", &delay_s, sizeof(delay_s));
	(void)PE_parse_boot_argn("sweettouchflip", &ts_flip, sizeof(ts_flip));
	SWEET_LOG_INFO("SWEET TOUCH: worker thread %p; TouchComm driver starting in %u s (flip %#x)\n",
	    current_thread(), delay_s, ts_flip);
	ts_wait(delay_s * 1000u);
	/* The display lifecycle waits for this worker's token, so a boot that
	 * never reaches this line is stuck before the panel comes up at all. */
	SWEET_LOG_INFO("SWEET TOUCH: delay over after %u s; bringing the controller up\n", delay_s);

	ts_tiles[TS_TILE_WEST] = ml_io_map(SWEET_DEV_TOUCH_NORTH_PA + 0x00100000u, 0x100000);
	ts_tiles[TS_TILE_EAST] = ml_io_map(SWEET_DEV_TOUCH_NORTH_PA + 0x00500000u, 0x100000);
	ts_tiles[TS_TILE_NORTH] = ml_io_map(SWEET_DEV_TOUCH_NORTH_PA + 0x00900000u, 0x100000);
	ts_tiles[TS_TILE_SOUTH] = ml_io_map(SWEET_DEV_TOUCH_NORTH_PA + 0x00d00000u, 0x100000);
	if (!ts_tiles[0] || !ts_tiles[1] || !ts_tiles[2] || !ts_tiles[3]) {
		SWEET_LOG_ERROR("SWEET TOUCH: TLMM mapping failed\n");
		goto out;
	}
	ts_pins("before");

	/* LG PEP D0: gpio59, gpio152 high; then reset. Bus released, ATTN input pull-up. */
	bb_level(TS_PIN_I2C_A, false);
	bb_level(TS_PIN_I2C_B, false);
	ts_gpio(TS_PIN_IRQ, 0, 3, 3, false, false);
	ts_gpio(TS_PIN_RESET, 0, 0, 3, true, false);    /* reset asserted */
	ts_gpio(TS_PIN_PWR_A, 0, 3, 3, true, true);
	ts_wait(1);
	ts_gpio(TS_PIN_PWR_B, 0, 3, 3, true, true);
	ts_wait(20);
	ts_gpio(TS_PIN_RESET, 0, 0, 3, true, true);     /* reset released */
	ts_wait(200);
	bb_recover();
	ts_pins("after");

	int rc = -1;
	for (unsigned attempt = 0; attempt < 2 && rc; ++attempt) {
		rc = tcm_start();
		if (rc == ENXIO && tcm.version == 0 && attempt == 0) {
			/* Nothing acknowledged 0x20: the device tree does not say which pin is SDA. */
			SWEET_LOG_WARN("SWEET TOUCH: no ack with sda=%u scl=%u; swapping\n", bb_sda_pin, bb_scl_pin);
			unsigned t = bb_sda_pin;
			bb_sda_pin = bb_scl_pin;
			bb_scl_pin = t;
			bb_level(TS_PIN_I2C_A, false);
			bb_level(TS_PIN_I2C_B, false);
			bb_recover();
			continue;
		}
		break;
	}
	if (rc) {
		tcm_fail(rc);
		ts_pins("failed");
		goto out;
	}
	SWEET_LOG_INFO("SWEET TOUCH: TouchComm ready (sda=%u scl=%u), max %ux%u, IRQ-only\n",
	    bb_sda_pin, bb_scl_pin, tcm.max_x, tcm.max_y);
	ts_gicd = ml_io_map(TS_GICD_PA, 0x10000);
	ts_irq_ready = ts_gicd && ts_gic_configure() && ts_gic_probe();
	if (!ts_irq_ready || !ts_irq_arm()) {
		SWEET_LOG_ERROR("SWEET TOUCH IRQ: setup failed; touch disabled\n");
		goto out;
	}

	ts_panel_publish(0,SWEET_TOUCH_RUNNING);
 __atomic_store_n(&ts_panel_live,1u,__ATOMIC_RELEASE);
 unsigned errors = 0, ticks = 0, attn_low = 0, reports = 0;
	for (;;) {
  unsigned control=ts_panel_step();
  if (control==2u) goto irq_stop;
  if (control==1u) { ts_panel_wait(); continue; }
		unsigned observed = __atomic_load_n(&ts_irq_hits, __ATOMIC_ACQUIRE);
		unsigned irqState = __atomic_load_n(&ts_irq_state, __ATOMIC_ACQUIRE);
		if (irqState == 3u) {
			goto irq_stop;
		}
		bool attn = (ts_gpio_io_rd(TS_PIN_IRQ) & 1u) == 0;
		uint32_t pinStatus = ts_rd(ts_tiles[ts_tile(TS_PIN_IRQ)],
		    TS_INTR_STATUS) & 1u;
		attn_low += attn;
		bool did_message = false, did_report = false;
		/* A woken worker can find ATTN already high: the controller deasserts
		 * between the interrupt and this sample, and a fast controller does it
		 * within the wakeup latency. The pad's latched status keeps that
		 * evidence. Acking it without a read drops the packet, and a lost "up"
		 * holds the contact down for good, which is what makes the UI go deaf
		 * after one or two touches. Drain while either says data is queued. */
		bool pending = attn || pinStatus != 0;
		if (!attn && pinStatus) ts_irq_late++;
		if (pending) {
			if (tcm.version == 2) {
				tcm.command = 0x0a;
			}
			rc = message();
			did_message = rc == 0;
			if (rc == 0 && tcm.code == 0x10) {
				ts_irq_fail("controller restart");
				goto irq_stop;
			}
			if (rc == 0 && tcm.code == 0x11) {
				rc = tcm_decode(tcm.payload, tcm.length, ts_now);
				if (rc == 0) {
					reports++;
					did_report = true;
					ts_report();
				} else {
					tcm.stage = "contacts";
					tcm.detail = tcm.length;
				}
			}
			if (rc) {
				if ((errors++ % 100u) == 0) {
					tcm_fail(rc);
				}
				if (errors == 10u) {
					bb_recover();
					ts_irq_fail("10 I2C errors");
					goto irq_stop;
				}
			} else {
				errors = 0;
			}
		}
		if ((ts_gpio_io_rd(TS_PIN_IRQ) & 1u) == 0 &&
		    did_message && !did_report && ++ts_irq_no_progress >= 16u) {
			ts_irq_fail("ATTN low without touch-report progress");
			goto irq_stop;
		} else if (did_report || (ts_gpio_io_rd(TS_PIN_IRQ) & 1u) != 0) {
			ts_irq_no_progress = 0;
		}
		if ((++ticks % 50u) == 0u) {
			SWEET_LOG_DEBUG("SWEET TOUCH: state reports=%u events=%u attn-low=%u i2c-errors=%u\n",
			    reports, ts_events, attn_low, ts_i2c_errors);
			SWEET_LOG_INFO("SWEET TOUCH IRQ: counters hits=%u empty=%u pin-status=%u attn=%u late=%u\n",
			    __atomic_load_n(&ts_irq_hits, __ATOMIC_ACQUIRE),
			    ts_irq_empty, pinStatus, attn ? 1 : 0, ts_irq_late);
		}
		vm_offset_t tile = ts_tiles[ts_tile(TS_PIN_IRQ)];
		if (irqState == 2u) {
			if (!did_message && ++ts_irq_empty >= 16u) {
				ts_irq_fail("16 empty IRQ wakeups");
				goto irq_stop;
			}
			if (did_message) {
				ts_irq_empty = 0;
			}
			uint32_t status = ts_rd(tile, TS_INTR_STATUS);
			if (observed <= 16u || (observed & 255u) == 0) {
				SWEET_LOG_INFO("SWEET TOUCH IRQ: hit irq=%u status=%#x attn=%u reports=%u\n",
				    observed, status, attn ? 1 : 0, reports);
			}
			__atomic_store_n(&ts_irq_state, 1u, __ATOMIC_RELEASE);
			ts_irq_unmask();
		}
		/* One ack per pass, whatever the top-half state: a latched status that
		 * nobody clears would spin this loop. Clearing it before the re-check
		 * makes a packet that landed in the meantime show up as a fresh latch. */
		{
			ts_irq_ack_pad();
			bool queued = (ts_gpio_io_rd(TS_PIN_IRQ) & 1u) == 0 ||
			    (ts_rd(tile, TS_INTR_STATUS) & 1u) != 0;
			if (queued) continue;
		}
		ts_irq_wait(observed);
	}
irq_stop:
	ts_release_all();

out:
 if (ts_irq_line_delegated) ts_irq_mask();
 __atomic_store_n(&ts_panel_live,0u,__ATOMIC_RELEASE);
 uint64_t frame=__atomic_load_n(&ts_panel_frame,__ATOMIC_ACQUIRE);
 if ((frame&15u)!=SWEET_TOUCH_STOPPED) ts_panel_publish(frame,SWEET_TOUCH_FAILED);
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

void
sweet_touch_init(void)
{
	static bool started;
	int enabled = 1;
	thread_t thread;

	if (started) {
		return;
	}
	started = true;
	(void)PE_parse_boot_argn("sweettouch", &enabled, sizeof(enabled));
	if (!enabled) {
		SWEET_LOG_INFO("SWEET TOUCH: disabled (sweettouch=0)\n");
		return;
	}
	kern_return_t kr = kernel_thread_start(ts_thread, NULL, &thread);
	if (kr == KERN_SUCCESS) {
		thread_deallocate(thread);
	} else {
		SWEET_LOG_ERROR("SWEET TOUCH: kernel_thread_start failed %d\n", kr);
	}
}
#endif /* ARM64_BOARD_CONFIG_SWEET && SWEET_DEV_TOUCH_TCM */
