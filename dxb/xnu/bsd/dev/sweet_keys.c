/*
 * MH2LM PMIC keys driven by the PMIC IRQ mux.
 * See hw-report.md: volume-up is PM6150L SID 4, NOT PM6150 SID 0.
 * Key state is read through PMIC observer channels. The MH2LM mux owns
 * interrupt configuration and acknowledgment.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <mach/vm_param.h>
#include <vm/memory_types.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm64/sweet_dev.h>
#include "sweet_spmi.h"
#include "sweet_keys.h"
#include "sweet_log_level.h"

extern vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, unsigned int);
extern void ml_io_unmap(vm_offset_t, vm_size_t);
extern kern_return_t thread_terminate(thread_t);
extern void sweet_hid_key(int key, int down);


#define KEYS_CORE_PA     SWEET_DEV_SPMI_CORE_PA
#define KEYS_OBS_PA      SWEET_DEV_SPMI_OBS_PA
#define KEYS_CNFG_PA     SWEET_DEV_SPMI_CFG_PA
#define KEYS_PON_RT      SWEET_DEV_PON_RT
#define KEYS_GPIO_RT     SWEET_DEV_VOL_GPIO_RT
#define KEYS_GPIO2_RT    SWEET_DEV_VOL_GPIO2_RT
#define KEYS_NO_APID     0xffffu
#define KEYS_PDC_PA      0x0b220000u
#define KEYS_GICD_PA     0x17a00000u
#define KEYS_PMIC_INTID  513u

/* The SPMI arbiter selects a peripheral by PPID = (SID << 8) | (reg >> 8),
 * and an observer command only carries the register's low byte. Every PMIC
 * GPIO pin lives in its own 0x100-byte page, so each pin needs its own APID
 * lookup (0xc5 = GPIO6, 0xc6 = GPIO7, 0xc1 = GPIO2, ...). */
#ifndef SWEET_DEV_VOL_GPIO_SID
#define SWEET_DEV_VOL_GPIO_SID 4
#endif
#ifndef SWEET_DEV_VOL_DOWN_PON
#define SWEET_DEV_VOL_DOWN_PON 1
#endif

static vm_offset_t keys_core, keys_obs, keys_cnfg;
static uint16_t keys_pon_apid = KEYS_NO_APID, keys_gpio_apid = KEYS_NO_APID;
#if SWEET_DEV_KEYS_VOLUME && !SWEET_DEV_VOL_DOWN_PON
static uint16_t keys_gpio2_apid = KEYS_NO_APID;
#endif
static uint32_t keys_version, keys_last_status;
static bool keys_started;
static int keys_sleep_chan;
static volatile unsigned keys_irq_events;
static volatile unsigned keys_irq_failed;
static volatile unsigned keys_worker_running;

/* Owned observer mappings: never pair permanent ml_io_map with unmap. */
static vm_offset_t
keys_map(vm_offset_t address, vm_size_t size)
{
	return ml_io_map_unmappable(address, size, VM_WIMG_IO);
}

static void
keys_unmap(vm_offset_t address, vm_size_t size)
{
	if (address) {
		ml_io_unmap(trunc_page(address), round_page(size + (address & PAGE_MASK)));
	}
}

/* Called only from the mux's IOWorkLoop, never from the GIC top half. */
static void
sweet_keys_irq_notify(unsigned key)
{
	(void)key;
	__atomic_add_fetch(&keys_irq_events, 1u, __ATOMIC_RELAXED);
	thread_wakeup(&keys_sleep_chan);
}

static void
sweet_keys_irq_failed(void)
{
	__atomic_store_n(&keys_irq_failed, 1u, __ATOMIC_RELEASE);
	thread_wakeup(&keys_sleep_chan);
}

/* Called by the owning IOService on its workloop, never a mux singleton. */
void
sweet_keys_event(unsigned events)
{
	if (events & 0x80000000u) {
		sweet_keys_irq_failed();
	} else {
		sweet_keys_irq_notify(events);
	}
}

static uint32_t
keys_rd(vm_offset_t base, uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}

/* Read-only snapshots of the two upstream gates. The SM8150 PDC pin map
 * identifies pin 1 as the EE0 PMIC peripheral IRQ feeding GIC INTID 513.
 * Keep these probes independent so a fault in either mapping is isolated. */
static void
keys_irq_diag_pdc(void)
{
	vm_offset_t pdc = keys_map(KEYS_PDC_PA, 0x400);
	if (!pdc) {
		SWEET_LOG_WARN("SWEET KEYS IRQ PDC: map failed pa=%#x\n", KEYS_PDC_PA);
		return;
	}
	uint32_t enable = keys_rd(pdc, 0x10u);
	uint32_t config = keys_rd(pdc, 0x114u);
	SWEET_LOG_DEBUG("SWEET KEYS IRQ PDC: pin=1 intid=%u enable-bank=%#x enabled=%u config=%#x (read-only)\n",
	    KEYS_PMIC_INTID, enable, (enable >> 1) & 1u, config);
	keys_unmap(pdc, 0x400);
}

static void
keys_irq_diag_gic(void)
{
	vm_offset_t gicd = keys_map(KEYS_GICD_PA, 0x10000);
	if (!gicd) {
		SWEET_LOG_WARN("SWEET KEYS IRQ GIC: map failed pa=%#x\n", KEYS_GICD_PA);
		return;
	}
	const uint32_t word = KEYS_PMIC_INTID / 32u;
	const uint32_t bit = KEYS_PMIC_INTID % 32u;
	const uint32_t group = keys_rd(gicd, 0x80u + 4u * word);
	const uint32_t enabled = keys_rd(gicd, 0x100u + 4u * word);
	const uint32_t pending = keys_rd(gicd, 0x200u + 4u * word);
	const uint32_t active = keys_rd(gicd, 0x300u + 4u * word);
	const uint32_t cfg = keys_rd(gicd, 0xc00u +
	    4u * (KEYS_PMIC_INTID / 16u));
	const uint32_t priorityWord = keys_rd(gicd,
	    0x400u + (KEYS_PMIC_INTID & ~3u));
	const uint32_t routerLo = keys_rd(gicd, 0x6000u + 8u * KEYS_PMIC_INTID);
	const uint32_t routerHi = keys_rd(gicd, 0x6004u + 8u * KEYS_PMIC_INTID);
	SWEET_LOG_DEBUG("SWEET KEYS IRQ GIC: intid=%u typer=%#x ctlr=%#x group=%u enabled=%u pending=%u active=%u cfg=%u priority=%#x router=%#x:%#x (read-only)\n",
	    KEYS_PMIC_INTID, keys_rd(gicd, 0x004u), keys_rd(gicd, 0x000u),
	    (group >> bit) & 1u, (enabled >> bit) & 1u,
	    (pending >> bit) & 1u, (active >> bit) & 1u,
	    (cfg >> (2u * (KEYS_PMIC_INTID % 16u))) & 3u,
	    (priorityWord >> (8u * (KEYS_PMIC_INTID & 3u))) & 0xffu,
	    routerHi, routerLo);
	keys_unmap(gicd, 0x10000);
}

/* v5 can map one PPID more than once; prefer the apps-owned entry. */
static uint16_t
keys_find_apid(uint16_t ppid, uint32_t count)
{
	uint16_t found = KEYS_NO_APID;
	for (uint32_t i = 0; i < count; ++i) {
		uint32_t map = keys_rd(keys_core, 0x900u + 4u * i);
		if (map == 0 || ((map >> 8) & 0xfffu) != ppid) {
			continue;
		}
		uint32_t owner = keys_rd(keys_cnfg, 0x700u + 4u * i) & 7u;
		if (found == KEYS_NO_APID || owner == 0) {
			found = (uint16_t)i;
		}
	}
	return found;
}

static bool
keys_read(uint16_t apid, uint16_t addr, uint8_t *out)
{
	if (apid == KEYS_NO_APID) {
		return false;
	}
	sweet_spmi_irq_lock();
	uint32_t off = 0x80u * apid; /* obs + EE(0)*0x10000 */
	uint32_t cmd = (1u << 27) | ((uint32_t)(addr & 0xffu) << 4);
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(keys_obs + off) = cmd; /* EXT_READL, one byte */
	__asm__ volatile("dsb osh" ::: "memory");
	for (unsigned i = 0; i < 1000; ++i) {
		keys_last_status = keys_rd(keys_obs, off + 0x08u);
		if (keys_last_status & 1u) {
			if (keys_last_status & 0x0eu) {
				sweet_spmi_irq_unlock();
				return false;
			}
			*out = (uint8_t)keys_rd(keys_obs, off + 0x18u);
			sweet_spmi_irq_unlock();
			return true;
		}
		delay(1);
	}
	sweet_spmi_irq_unlock();
	return false;
}

/* This is deliberately a read-only preflight. The raw bytes are recorded as
 * whole registers because the PON/GPIO interrupt bit assignment and firmware
 * gates must be demonstrated on MH2LM before SweetSpmiInterruptMux arms them. */
static void
keys_irq_diag_line(const char *name, uint16_t apid, uint16_t rtAddr)
{
	uint8_t rt = 0, type = 0, high = 0, low = 0, enabled = 0, latched = 0;
	if (apid == KEYS_NO_APID) {
		SWEET_LOG_WARN("SWEET KEYS IRQ CFG: %s skipped; no APID\n", name);
		return;
	}
	bool ok = keys_read(apid, rtAddr, &rt) &&
	    keys_read(apid, (uint16_t)(rtAddr + 1u), &type) &&
	    keys_read(apid, (uint16_t)(rtAddr + 2u), &high) &&
	    keys_read(apid, (uint16_t)(rtAddr + 3u), &low) &&
	    keys_read(apid, (uint16_t)(rtAddr + 5u), &enabled) &&
	    keys_read(apid, (uint16_t)(rtAddr + 8u), &latched);
	SWEET_LOG_DEBUG("SWEET KEYS IRQ CFG: %s apid=%u rt=%#x edge=%#x high=%#x low=%#x "
	    "enabled=%#x latched=%#x read-ok=%d\n", name, apid, rt, type, high,
	    low, enabled, latched, ok ? 1 : 0);
}

#if SWEET_DEV_KEYS_VOLUME
static uint16_t
keys_gpio_ppid(uint32_t rt_addr)
{
	return (uint16_t)(((SWEET_DEV_VOL_GPIO_SID & 0xffu) << 8) |
	    ((rt_addr >> 8) & 0xffu));
}

/* A PMIC GPIO pin keeps RT_STS at rt, MODE_CTL at +0x30, EN_CTL at +0x36 and
 * the TYPE/SUBTYPE bytes at -0x0c/-0x0b. Only an enabled digital input makes
 * RT_STS meaningful; the ABL leaves the volume pins that way. */
static bool
keys_gpio_input_ok(uint16_t apid, uint32_t rt, uint8_t *mode_out,
    uint8_t *en_out, uint8_t *subtype_out)
{
	uint8_t mode = 0, en = 0, subtype = 0;

	if (!keys_read(apid, (uint16_t)(rt + 0x30u), &mode) ||
	    !keys_read(apid, (uint16_t)(rt + 0x36u), &en) ||
	    !keys_read(apid, (uint16_t)(rt - 0x0bu), &subtype)) {
		return false;
	}
	*mode_out = mode;
	*en_out = en;
	*subtype_out = subtype;
	unsigned dir = (subtype >= 0x10u && subtype <= 0x13u) ?
	    (mode & 3u) : ((mode >> 4) & 7u);
	return (en & 0x80u) != 0 && (dir == 0 || dir == 2);
}
#endif /* SWEET_DEV_KEYS_VOLUME */

static void
keys_wait(unsigned observedEvents)
{
	(void)assert_wait(&keys_sleep_chan, THREAD_UNINT);
	/* An IRQ delivered after the last sample but before assert_wait must
	 * still cause an immediate new sample. */
	if (__atomic_load_n(&keys_irq_events, __ATOMIC_ACQUIRE) !=
	    observedEvents ||
	    __atomic_load_n(&keys_irq_failed, __ATOMIC_ACQUIRE)) {
		thread_wakeup(&keys_sleep_chan);
	}
	thread_block(THREAD_CONTINUE_NULL);
}

static void
keys_thread(void *arg, wait_result_t result)
{
	int state[3] = {0, 0, 0};
	unsigned errors = 0;
	(void)arg;
	(void)result;
	for (;;) {
		if (__atomic_load_n(&keys_irq_failed, __ATOMIC_ACQUIRE)) {
			for (int k = 0; k < 3; ++k) {
				if (state[k]) {
					sweet_hid_key(k, 0);
				}
			}
			SWEET_LOG_INFO("SWEET KEYS IRQ: worker stopped; pressed HID keys released\n");
			__atomic_store_n(&keys_worker_running, 0u, __ATOMIC_RELEASE);
			thread_wakeup(&keys_worker_running);
			(void)thread_terminate(current_thread());
			thread_block(THREAD_CONTINUE_NULL);
		}
		unsigned observedEvents = __atomic_load_n(&keys_irq_events,
		    __ATOMIC_ACQUIRE);
		uint8_t pon = 0;
		bool pok = keys_read(keys_pon_apid, KEYS_PON_RT, &pon);
#if SWEET_DEV_KEYS_VOLUME
		uint8_t gpio = 0;
		bool gok = keys_read(keys_gpio_apid, KEYS_GPIO_RT, &gpio);
#if !SWEET_DEV_VOL_DOWN_PON
		uint8_t gpio2 = 0;
		bool g2ok = keys_read(keys_gpio2_apid, KEYS_GPIO2_RT, &gpio2);
		int now[3] = {!!(pon & 1u), !(gpio & 1u), !(gpio2 & 1u)};
#else
		bool g2ok = true;
		int now[3] = {!!(pon & 1u), !(gpio & 1u), !!(pon & 2u)};
#endif
		if (!pok || !gok || !g2ok) {
			if ((errors++ % 250u) == 0) {
				SWEET_LOG_WARN("SWEET KEYS: read error pon-ok=%d gpio-ok=%d g2-ok=%d arb-status=%#x\n",
				    pok, gok, g2ok, keys_last_status);
			}
		} else {
			errors = 0;
		}
		for (int k = 0; k < 3; ++k) {
			bool ok = (k == 0) ? pok : (k == 1 ? gok :
			    (SWEET_DEV_VOL_DOWN_PON ? pok : g2ok));
			if (ok && now[k] != state[k]) {
				state[k] = now[k];
				SWEET_LOG_DEBUG("SWEET KEYS: key=%d down=%d pon=%#x gpio=%#x\n",
				    k, state[k], pon, gpio);
				sweet_hid_key(k, state[k]);
			}
		}
#else /* !SWEET_DEV_KEYS_VOLUME: power key only */
		int now[3] = {!!(pon & 1u), 0, 0};
		if (!pok) {
			if ((errors++ % 250u) == 0) {
				SWEET_LOG_WARN("SWEET KEYS: read error pon-ok=%d arb-status=%#x\n",
				    pok, keys_last_status);
			}
		} else {
			errors = 0;
		}
		if (pok && now[0] != state[0]) {
			state[0] = now[0];
			SWEET_LOG_DEBUG("SWEET KEYS: key=0 down=%d pon=%#x\n", state[0], pon);
			sweet_hid_key(0, state[0]);
		}
#endif /* SWEET_DEV_KEYS_VOLUME */
		keys_wait(observedEvents);
	}
}

bool
sweet_keys_prepare(void)
{
	int enabled = 1;
	int irqDiag = 0;
	if (keys_started) {
		return false;
	}
	keys_started = true;
	(void)PE_parse_boot_argn("sweetkeys", &enabled, sizeof(enabled));
	(void)PE_parse_boot_argn("sweetkeysirqdiag", &irqDiag, sizeof(irqDiag));
	if (!enabled) {
		SWEET_LOG_INFO("SWEET KEYS: disabled (sweetkeys=0)\n");
		sweet_keys_stop();
		return false;
	}
	if (sweet_spmi_init() != 0) {
		SWEET_LOG_ERROR("SWEET KEYS IRQ: shared SPMI transport unavailable; keys disabled\n");
		return false;
	}
	keys_core = keys_map(KEYS_CORE_PA, 0x2000);
	keys_cnfg = keys_map(KEYS_CNFG_PA, 0x1000);
	keys_obs = keys_map(KEYS_OBS_PA, 0x10000);
	if (!keys_core || !keys_cnfg || !keys_obs) {
		SWEET_LOG_ERROR("SWEET KEYS: MMIO mapping failed\n");
		sweet_keys_stop();
		return false;
	}
	keys_version = keys_rd(keys_core, 0);
	uint32_t count = keys_rd(keys_core, 4) & 0x7ffu;
	if (keys_version < 0x50000000u || keys_version >= 0x70000000u ||
	    count == 0 || count > 512) {
		SWEET_LOG_ERROR("SWEET KEYS: unsupported arbiter version=%#x apids=%u\n",
		    keys_version, count);
		sweet_keys_stop();
		return false;
	}
	keys_pon_apid = keys_find_apid(0x008, count);
	uint8_t pon = 0;
	bool pok = keys_read(keys_pon_apid, KEYS_PON_RT, &pon);
#if SWEET_DEV_KEYS_VOLUME
	uint16_t up_ppid = keys_gpio_ppid(KEYS_GPIO_RT);
	keys_gpio_apid = keys_find_apid(up_ppid, count);
	uint8_t gpio = 0, mode = 0, en = 0, subtype = 0;
	bool gok = keys_read(keys_gpio_apid, KEYS_GPIO_RT, &gpio);
	bool up_ok = keys_gpio_input_ok(keys_gpio_apid, KEYS_GPIO_RT,
	    &mode, &en, &subtype);
	SWEET_LOG_DEBUG("SWEET KEYS: arb=%#x apids=%u pon-apid=%u volup-ppid=%#x apid=%u pon=%#x/%d gpio=%#x/%d mode=%#x en=%#x subtype=%#x input=%d\n",
	    keys_version, count, keys_pon_apid, up_ppid, keys_gpio_apid,
	    pon, pok, gpio, gok, mode, en, subtype, up_ok);
	/* GPIO configuration belongs to firmware. Diagnose, never repair it here. */
	if (!up_ok) {
		SWEET_LOG_WARN("SWEET KEYS: volume-up GPIO not confirmed enabled as input; volume-up withheld, PMIC unchanged\n");
		keys_gpio_apid = KEYS_NO_APID;
	}
#if !SWEET_DEV_VOL_DOWN_PON
	uint16_t down_ppid = keys_gpio_ppid(KEYS_GPIO2_RT);
	keys_gpio2_apid = keys_find_apid(down_ppid, count);
	uint8_t gpio2 = 0, mode2 = 0, en2 = 0, subtype2 = 0;
	bool g2ok = keys_read(keys_gpio2_apid, KEYS_GPIO2_RT, &gpio2);
	bool down_ok = keys_gpio_input_ok(keys_gpio2_apid, KEYS_GPIO2_RT,
	    &mode2, &en2, &subtype2);
	SWEET_LOG_DEBUG("SWEET KEYS: voldown-ppid=%#x apid=%u gpio=%#x/%d mode=%#x en=%#x subtype=%#x input=%d\n",
	    down_ppid, keys_gpio2_apid, gpio2, g2ok, mode2, en2, subtype2, down_ok);
	if (!down_ok) {
		SWEET_LOG_WARN("SWEET KEYS: volume-down GPIO not confirmed enabled as input; volume-down withheld, PMIC unchanged\n");
		keys_gpio2_apid = KEYS_NO_APID;
	}
	if (!pok && !gok && !g2ok) {
#else
	if (!pok && !gok) {
#endif
		SWEET_LOG_WARN("SWEET KEYS: no readable keys\n");
	}
	if (!pok || !gok || !up_ok) {
		SWEET_LOG_ERROR("SWEET KEYS IRQ: initial PMIC state unavailable; keys disabled\n");
		sweet_keys_stop();
		return false;
	}
#else /* !SWEET_DEV_KEYS_VOLUME */
	/* Volume keys unmapped on this board yet: the PMIC GPIO (PM8150 GPIO6,
	 * GPIO7 on guacamole) still has to be identified in the APID table.
	 * Power (PON_REASON1 bit 0) is polled as usual. See GUACAMOLE.md. */
	SWEET_LOG_DEBUG("SWEET KEYS: arb=%#x apids=%u pon-apid=%u pon=%#x/%d (volume keys unmapped)\n",
	    keys_version, count, keys_pon_apid, pon, pok);
	if (!pok) {
		SWEET_LOG_WARN("SWEET KEYS: no readable power key\n");
	}
#endif /* SWEET_DEV_KEYS_VOLUME */
	if (irqDiag != 0) {
		unsigned mask = (unsigned)irqDiag;
		SWEET_LOG_DEBUG("SWEET KEYS IRQ: read-only diagnostic mask=%#x\n", mask);
		/* bit 0: helper init; bits 1/2 or 5..9: PMIC-arb route probes. */
		if ((mask & 0x3e7u) != 0) {
			if (sweet_spmi_init() != 0) {
				SWEET_LOG_WARN("SWEET KEYS IRQ: shared SPMI preflight unavailable\n");
			} else {
				if ((mask & 0x02u) != 0) {
					sweet_spmi_irq_diag_source(0u, KEYS_PON_RT >> 8,
					    "keys-pon");
				}
				if ((mask & 0x20u) != 0) {
					sweet_spmi_irq_diag_source_parts(0u, KEYS_PON_RT >> 8,
					    "keys-pon", SWEET_SPMI_IRQ_DIAG_ROUTE_META);
				}
				if ((mask & 0x40u) != 0) {
					sweet_spmi_irq_diag_source_parts(0u, KEYS_PON_RT >> 8,
					    "keys-pon", SWEET_SPMI_IRQ_DIAG_ROUTE_OWNER);
				}
				if ((mask & 0x80u) != 0) {
					sweet_spmi_irq_diag_source_parts(0u, KEYS_PON_RT >> 8,
					    "keys-pon", SWEET_SPMI_IRQ_DIAG_ROUTE_CHANNEL);
				}
				if ((mask & 0x100u) != 0) {
					sweet_spmi_irq_diag_source_parts(0u, KEYS_PON_RT >> 8,
					    "keys-pon", SWEET_SPMI_IRQ_DIAG_ROUTE_ENABLE);
				}
				if ((mask & 0x200u) != 0) {
					sweet_spmi_irq_diag_source_parts(0u, KEYS_PON_RT >> 8,
					    "keys-pon", SWEET_SPMI_IRQ_DIAG_ROUTE_STATUS);
				}
#if SWEET_DEV_KEYS_VOLUME
				if ((mask & 0x04u) != 0) {
					sweet_spmi_irq_diag_source(SWEET_DEV_VOL_GPIO_SID,
					    KEYS_GPIO_RT >> 8, "keys-volume-up");
				}
#endif
			}
		}
		/* bits 3/4 are observer reads of the PMIC interrupt blocks. */
		if ((mask & 0x08u) != 0) {
			keys_irq_diag_line("pon", keys_pon_apid, KEYS_PON_RT);
		}
#if SWEET_DEV_KEYS_VOLUME
		if ((mask & 0x10u) != 0) {
			keys_irq_diag_line("volume-up", keys_gpio_apid, KEYS_GPIO_RT);
		}
#endif
		/* bits 10/11: independent read-only PDC and GIC snapshots. */
		if ((mask & 0x400u) != 0) {
			keys_irq_diag_pdc();
		}
		if ((mask & 0x800u) != 0) {
			keys_irq_diag_gic();
		}
	}
	return true;
}

bool
sweet_keys_start(void)
{
	thread_t thread;
	__atomic_store_n(&keys_worker_running, 1u, __ATOMIC_RELEASE);
	kern_return_t kr = kernel_thread_start(keys_thread, NULL, &thread);
	if (kr == KERN_SUCCESS) {
		thread_deallocate(thread);
		return true;
	}
	__atomic_store_n(&keys_worker_running, 0u, __ATOMIC_RELEASE);
	sweet_keys_irq_failed();
	SWEET_LOG_ERROR("SWEET KEYS: kernel_thread_start failed %d\n", kr);
	return false;
}

void
sweet_keys_stop(void)
{
	sweet_keys_irq_failed();
	/* Join before the IOService/connection can disappear. Worker releases
	 * every pressed HID key before publishing completion. No polling timer. */
	while (__atomic_load_n(&keys_worker_running, __ATOMIC_ACQUIRE)) {
		(void)assert_wait(&keys_worker_running, THREAD_UNINT);
		if (!__atomic_load_n(&keys_worker_running, __ATOMIC_ACQUIRE)) {
			thread_wakeup(&keys_worker_running);
		}
		thread_block(THREAD_CONTINUE_NULL);
	}
	keys_unmap(keys_obs, 0x10000);
	keys_unmap(keys_cnfg, 0x1000);
	keys_unmap(keys_core, 0x2000);
	keys_obs = keys_cnfg = keys_core = 0;
}
#endif /* ARM64_BOARD_CONFIG_SWEET */
