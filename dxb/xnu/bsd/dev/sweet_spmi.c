/*
 * Battery observer EXT_READL and shared PMIC-arb transaction serialization.
 * Observer API remains read only. Keys and mux IRQ writes share its lock.
 * A separate MH2LM charger table permits bounded, verified configuration RMW.
 * No charging policy invokes these writes until its protections are ready.
 * Initialization is serialized across battery and late board services.
 * Disjoint APIDs do not exempt commands from controller serialization.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <machine/machine_routines.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOLib.h>
#include <mach/vm_param.h>
#include <vm/memory_types.h>
#include <pexpert/arm64/sweet_dev.h>
#include "sweet_spmi.h"
#include "sweet_adc5.h"
#include "sweet_charger.h"
#include <kern/clock.h>
#include "sweet_log_level.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, unsigned int);
extern void ml_io_unmap(vm_offset_t, vm_size_t);

#define SP_CORE_PA SWEET_DEV_SPMI_CORE_PA
#define SP_CHNLS_PA SWEET_DEV_SPMI_CHNLS_PA
#define SP_OBS_PA  SWEET_DEV_SPMI_OBS_PA
#define SP_INTR_PA SWEET_DEV_SPMI_INTR_PA
#define SP_CFG_PA  SWEET_DEV_SPMI_CFG_PA
#define SP_NO_APID 0xffffu
#define SP_APID_MAP_OFF 0x900u
#define SP_OWNERSHIP_OFF 0x700u
#define SP_CHNLS_APID_STRIDE 0x10000u
#define SP_ACC_ENABLE_OFF 0x100u
#define SP_IRQ_STATUS_OFF 0x104u
#define SP_IRQ_OWNER_BIT 0x01000000u

static vm_offset_t sp_core, sp_obs, sp_cfg;
static IOLock *sp_lock;
/* Registered before a mux ACK waits for sp_lock. Normal transactions sleep
 * rather than reacquiring ahead of an already waiting ACK. No IRQ-context
 * callers, no preemption of an in-flight command and no new bus concurrency. */
static unsigned sp_ack_waiters;
static unsigned sp_count;
static bool sp_attempted, sp_ready;
/* A timed-out command may complete late: never reuse that lane this boot. */
static bool sp_poisoned[512];
/* RW and observer are distinct lanes. All RW users (including mux) share this
 * state under sp_lock so a late command cannot corrupt another component. */
static bool sp_rw_poisoned[512];

static uint32_t
sp_rd(vm_offset_t base, unsigned off)
{
	uint32_t value = *(volatile uint32_t *)(base + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return value;
}

static uint16_t
sp_find_apid(unsigned ppid)
{
	uint16_t found = SP_NO_APID;
	for (unsigned i = 0; i < sp_count; ++i) {
		uint32_t map = sp_rd(sp_core, SP_APID_MAP_OFF + 4u * i);
		if (!map || ((map >> 8) & 0xfffu) != ppid) {
			continue;
		}
		unsigned owner = sp_rd(sp_cfg, SP_OWNERSHIP_OFF + 4u * i) & 7u;
		if (found == SP_NO_APID || owner == 0) {
			found = (uint16_t)i;
		}
	}
	return found;
}

/* Linux's v5 PMIC-arb driver routes PMIC peripheral IRQs through GIC SPI
 * 481. Before this port registers that SPI or changes a PMIC enable/clear
 * bit, dump the routing state for the two relevant peripherals. The v5
 * access-enable and status words are in the read/write channel, while the
 * 32-APID owner bitmap is in the separate intr region. All reads here are
 * bounded and have no side effects on the PMIC. */
static void
sp_irq_diag_peripheral(unsigned sid, unsigned peripheral, const char *name,
	unsigned parts)
{
	unsigned ppid = (sid << 8) | peripheral;
	bool found = false;
	vm_offset_t intr = 0;
	if ((parts & SWEET_SPMI_IRQ_DIAG_ROUTE_OWNER) != 0) {
		intr = ml_io_map(SP_INTR_PA, 0x1000);
		if (!intr) {
			SWEET_LOG_WARN("SWEET SPMI IRQ: %s ppid=%#x intr mapping failed\n", name,
			    ppid);
			return;
		}
	}
	for (unsigned apid = 0; apid < sp_count; ++apid) {
		uint32_t map = sp_rd(sp_core, SP_APID_MAP_OFF + 4u * apid);
		if (!map || ((map >> 8) & 0xfffu) != ppid) {
			continue;
		}
		found = true;
		unsigned owner = sp_rd(sp_cfg, SP_OWNERSHIP_OFF + 4u * apid) & 7u;
		unsigned ownerWord = apid >> 5;
		unsigned ownerBit = apid & 31u;
		uint32_t ownerMask = 0, enable = 0, status = 0;
		if ((parts & SWEET_SPMI_IRQ_DIAG_ROUTE_OWNER) != 0) {
			ownerMask = sp_rd(intr, 4u * ownerWord);
		}
		/* v5 can have duplicate PPIDs for different execution environments.
		 * The channel's IRQ registers are only probed through our own EE0,
		 * IRQ-owned APID; do not touch a foreign EE's channel registers. */
		if ((parts & (SWEET_SPMI_IRQ_DIAG_ROUTE_CHANNEL |
		    SWEET_SPMI_IRQ_DIAG_ROUTE_ENABLE |
		    SWEET_SPMI_IRQ_DIAG_ROUTE_STATUS)) != 0 &&
		    (owner != 0u || (map & SP_IRQ_OWNER_BIT) == 0)) {
			SWEET_LOG_INFO("SWEET SPMI IRQ: %s ppid=%#x apid=%u channel skipped owner=%u irq-owner=%u (EE0 IRQ owner required)\n",
			    name, ppid, apid, owner, (map & SP_IRQ_OWNER_BIT) ? 1 : 0);
			continue;
		}
		if ((parts & (SWEET_SPMI_IRQ_DIAG_ROUTE_CHANNEL |
		    SWEET_SPMI_IRQ_DIAG_ROUTE_ENABLE |
		    SWEET_SPMI_IRQ_DIAG_ROUTE_STATUS)) != 0) {
			vm_offset_t chnls = ml_io_map(
			    SP_CHNLS_PA + SP_CHNLS_APID_STRIDE * apid, 0x1000);
			if (!chnls) {
				SWEET_LOG_WARN("SWEET SPMI IRQ: %s ppid=%#x apid=%u chnls mapping failed\n",
				    name, ppid, apid);
				continue;
			}
			if ((parts & SWEET_SPMI_IRQ_DIAG_ROUTE_ENABLE) != 0) {
				enable = sp_rd(chnls, SP_ACC_ENABLE_OFF);
			}
			if ((parts & SWEET_SPMI_IRQ_DIAG_ROUTE_STATUS) != 0) {
				status = sp_rd(chnls, SP_IRQ_STATUS_OFF);
			}
		}
		SWEET_LOG_INFO("SWEET SPMI IRQ: %s route-parts=%#x ppid=%#x apid=%u map=%#x owner=%u "
		    "irq-owner=%u owner-word=%u owner-mask=%#x owner-bit=%u active=%u "
		    "acc-enable=%#x irq-status=%#x\n", name, parts, ppid, apid, map, owner,
		    (map & SP_IRQ_OWNER_BIT) ? 1 : 0, ownerWord, ownerMask, ownerBit,
		    (ownerMask >> ownerBit) & 1u, enable, status);
	}
	if (!found) {
		SWEET_LOG_WARN("SWEET SPMI IRQ: %s ppid=%#x has no APID mapping\n", name, ppid);
	}
}

void
sweet_spmi_irq_diag(void)
{
	if (!__atomic_load_n(&sp_ready, __ATOMIC_ACQUIRE)) {
		SWEET_LOG_WARN("SWEET SPMI IRQ: skipped; arbiter is not ready\n");
		return;
	}
	sweet_spmi_irq_lock();
	SWEET_LOG_INFO("SWEET SPMI IRQ: PMIC-arb v5 read-only preflight; no GIC handler, no enable, no clear\n");
	sp_irq_diag_peripheral(2, 0x13, "usbin", SWEET_SPMI_IRQ_DIAG_ROUTE_ALL);
	sp_irq_diag_peripheral(2, 0x10, "chgr", SWEET_SPMI_IRQ_DIAG_ROUTE_ALL);
	sweet_spmi_irq_unlock();
}

void
sweet_spmi_irq_diag_source(unsigned sid, unsigned peripheral, const char *name)
{
	sweet_spmi_irq_diag_source_parts(sid, peripheral, name,
	    SWEET_SPMI_IRQ_DIAG_ROUTE_ALL);
}

void
sweet_spmi_irq_diag_source_parts(unsigned sid, unsigned peripheral,
	const char *name, unsigned parts)
{
	parts &= SWEET_SPMI_IRQ_DIAG_ROUTE_ALL;
	if (!__atomic_load_n(&sp_ready, __ATOMIC_ACQUIRE)) {
		SWEET_LOG_WARN("SWEET SPMI IRQ: %s skipped; arbiter is not ready\n",
		    name ? name : "source");
		return;
	}
	if (sid > 15u || peripheral > 0xffu) {
		SWEET_LOG_WARN("SWEET SPMI IRQ: %s invalid sid=%u peripheral=%#x\n",
		    name ? name : "source", sid, peripheral);
		return;
	}
	if (!parts) {
		SWEET_LOG_INFO("SWEET SPMI IRQ: %s requested no route parts\n",
		    name ? name : "source");
		return;
	}
	sweet_spmi_irq_lock();
	SWEET_LOG_INFO("SWEET SPMI IRQ: %s read-only route parts=%#x; no GIC handler, no enable, no clear\n",
	    name ? name : "source", parts);
	sp_irq_diag_peripheral(sid, peripheral, name ? name : "source", parts);
	sweet_spmi_irq_unlock();
}

/* Allocate once without depending on which IOService starts first. Allocation
 * happens outside the controller lock; losing candidates are freed. */
static IOLock *
sp_get_lock(void)
{
	IOLock *lock = __atomic_load_n(&sp_lock, __ATOMIC_ACQUIRE);
	if (!lock) {
		IOLock *candidate = IOLockAlloc();
		if (!candidate) {
			return NULL;
		}
		IOLock *expected = NULL;
		if (__atomic_compare_exchange_n(&sp_lock, &expected, candidate, false,
		    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			lock = candidate;
		} else {
			IOLockFree(candidate);
			lock = expected;
		}
	}
	return lock;
}

/* Called with the shared lock held, including the attempted/ready checks. */
static int
sp_initialize(void)
{
	if (sp_attempted) {
		return sp_ready ? 0 : -1;
	}
	sp_attempted = true;
	sp_core = ml_io_map(SP_CORE_PA, 0x2000);
	sp_cfg = ml_io_map(SP_CFG_PA, 0x1000);
	sp_obs = ml_io_map(SP_OBS_PA, 0x10000); /* EE 0, 512 * 0x80 */
	if (!sp_core || !sp_cfg || !sp_obs) {
		SWEET_LOG_ERROR("SWEET BATTERY: SPMI mapping failed\n");
		return -1;
	}
	uint32_t arb_version = sp_rd(sp_core, 0);
	sp_count = sp_rd(sp_core, 4) & 0x7ffu;
	if (arb_version < 0x50000000u || arb_version >= 0x70000000u ||
	    !sp_count || sp_count > 512) {
		SWEET_LOG_ERROR("SWEET BATTERY: unsupported SPMI arb=%#x APIDs=%u\n",
		    arb_version, sp_count);
		return -1;
	}
	__atomic_store_n(&sp_ready, true, __ATOMIC_RELEASE);
	SWEET_LOG_INFO("SWEET BATTERY: SPMI arb=%#x APIDs=%u, observer reads only\n",
	    arb_version, sp_count);
	return 0;
}

int
sweet_spmi_init(void)
{
	IOLock *lock = sp_get_lock();
	if (!lock) {
		return -1;
	}
	IOLockLock(lock);
	int result = sp_initialize();
	IOLockUnlock(lock);
	return result;
}

void
sweet_spmi_irq_lock(void)
{
	IOLockLock(sp_lock);
	while (__atomic_load_n(&sp_ack_waiters, __ATOMIC_ACQUIRE)) {
		/* Atomically enqueue and drop sp_lock; recheck after every wakeup. */
		IOLockSleep(sp_lock, &sp_ack_waiters, THREAD_UNINT);
	}
}

void
sweet_spmi_irq_ack_lock(void)
{
	__atomic_add_fetch(&sp_ack_waiters, 1u, __ATOMIC_ACQ_REL);
	IOLockLock(sp_lock);
	__atomic_sub_fetch(&sp_ack_waiters, 1u, __ATOMIC_ACQ_REL);
}

void
sweet_spmi_irq_unlock(void)
{
	/* Wake under the same lock used by the sleep predicate: no lost wakeup.
	 * The finite mux drain/storm bounds remain responsible for IRQ fairness. */
	if (!__atomic_load_n(&sp_ack_waiters, __ATOMIC_ACQUIRE)) {
		IOLockWakeup(sp_lock, &sp_ack_waiters, false);
	}
	IOLockUnlock(sp_lock);
}

int
sweet_spmi_rw_poisoned_locked(unsigned apid)
{
	return apid >= sp_count || apid >= 512 || sp_rw_poisoned[apid];
}

void
sweet_spmi_rw_poison_locked(unsigned apid)
{
	if (apid < sp_count && apid < 512) {
		sp_rw_poisoned[apid] = true;
	}
}

#if defined(ARM64_BOARD_CONFIG_MH2LM)
/* LG smb5-reg.h / smb5-lib.c. These CMD bits are persistent controls (Linux
 * uses masked writes); pulse/W1C commands intentionally have no writer here.
 * Security unlock, SRAM and IRQ offsets are absent. Watchdog pet has its own
 * fixed pulse API; it must never use this stable-value update table.
 * This table describes access semantics, not approval to enable charging. */
struct sp_charger_register {
	uint16_t addr;
	uint8_t write_mask;
};
static const struct sp_charger_register sp_charger_registers[] = {
	{0x1006, 0}, {0x1007, 0}, {0x100b, 0}, {0x100d, 0},
	{0x1042, 0x01}, {0x1043, 0x01}, {0x1051, 0},
	{0x1061, 0xff}, {0x1067, 0}, {0x1068, 0}, {0x1069, 0}, {0x106a, 0},
	{0x106b, 0}, {0x106c, 0}, {0x1070, 0xff}, {0x1072, 0},
	{0x107d, 0}, {0x107e, 0}, {0x107f, 0}, {0x1090, 0},
	{0x1107, 0}, {0x1108, 0}, {0x110b, 0},
	{0x1307, 0}, {0x1308, 0}, {0x1310, 0},
	{0x1340, 0x01}, {0x1342, 0x01}, {0x1362, 0}, {0x1365, 0x10},
	{0x1366, 0x07}, {0x1370, 0xff}, {0x1380, 0x9c},
	/* Only these Type-C status bytes; no peripheral-wide permission. */
	{0x1506, 0}, {0x1507, 0}, {0x1509, 0}, {0x150b, 0}, {0x150d, 0},
	{0x1610, 0}, {0x1648, 0x18}, {0x1651, 0xc2}, {0x1653, 0xff}, {0x1661, 0}, {0x1690, 0}
};

static const struct sp_charger_register *
sp_charger_register(unsigned sid, unsigned addr)
{
	if (sid != 2u || addr > 0xffffu) {
		return NULL;
	}
	for (unsigned i = 0; i < sizeof(sp_charger_registers) /
	    sizeof(sp_charger_registers[0]); ++i) {
		if (sp_charger_registers[i].addr == addr) {
			return &sp_charger_registers[i];
		}
	}
	return NULL;
}

/* Called with sp_lock held. The IRQ-owner bit is intentionally independent:
 * v5 may map IRQ and write ownership to different APIDs for one peripheral. */
static int
sp_charger_route(unsigned sid, unsigned addr, struct sweet_spmi_charger_route *out)
{
	uint32_t version = sp_rd(sp_core, 0);
	if (version < 0x50000000u || version >= 0x60000000u) {
		return SWEET_SPMI_UNSUPPORTED;
	}
	uint16_t found = SP_NO_APID;
	uint8_t irq_owner = 0;
	unsigned ppid = (sid << 8) | (addr >> 8);
	for (unsigned apid = 0; apid < sp_count; ++apid) {
		uint32_t map = sp_rd(sp_core, SP_APID_MAP_OFF + 4u * apid);
		if (!map || ((map >> 8) & 0xfffu) != ppid ||
		    (sp_rd(sp_cfg, SP_OWNERSHIP_OFF + 4u * apid) & 7u) != 0) {
			continue;
		}
		if (found != SP_NO_APID) {
			return SWEET_SPMI_AMBIGUOUS;
		}
		found = (uint16_t)apid;
		irq_owner = (map & SP_IRQ_OWNER_BIT) != 0;
	}
	if (found == SP_NO_APID) {
		return SWEET_SPMI_NO_OWNER;
	}
	*out = (struct sweet_spmi_charger_route){found, 0, irq_owner};
	return SWEET_SPMI_OK;
}

static void
sp_wr(vm_offset_t base, unsigned off, uint32_t value)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(base + off) = value;
	__asm__ volatile("dsb osh" ::: "memory");
}

/* A DONE error has completed and can be reported without retry. A timeout is
 * different: the lane must be poisoned before releasing the controller lock. */
static int
sp_charger_wait(vm_offset_t lane)
{
	for (unsigned i = 0; i < 1000; ++i) {
		uint32_t status = sp_rd(lane, 0x08u);
		if (status & 1u) {
			if (status & 4u) { return SWEET_SPMI_DENIED; }
			if (status & 8u) { return SWEET_SPMI_DROPPED; }
			if (status & 2u) { return SWEET_SPMI_IO; }
			return SWEET_SPMI_OK;
		}
		delay(1);
	}
	return SWEET_SPMI_TIMEOUT;
}

/* Internal observer primitive; caller holds sp_lock and validates its fixed
 * protocol/table address and observer APID. Does not impose a public whitelist. */
static int
sp_charger_read_locked(unsigned apid, unsigned addr, uint8_t *out)
{
	if (sp_poisoned[apid]) {
		return SWEET_SPMI_POISONED;
	}
	vm_offset_t lane = sp_obs + 0x80u * apid;
	sp_wr(lane, 0, (1u << 27) | ((addr & 0xffu) << 4));
	int result = sp_charger_wait(lane);
	if (result == SWEET_SPMI_TIMEOUT) {
		sp_poisoned[apid] = true;
		SWEET_LOG_ERROR("SWEET CHARGER IO: observer timeout addr=%#x apid=%u; lane poisoned\n",
		    addr, apid);
	}
	if (result == SWEET_SPMI_OK) {
		*out = (uint8_t)sp_rd(lane, 0x18u);
	}
	return result;
}

int
sweet_spmi_charger_get_route(unsigned sid, unsigned addr,
    struct sweet_spmi_charger_route *out)
{
	if (!out || !sp_charger_register(sid, addr)) {
		return SWEET_SPMI_INVALID;
	}
	if (!__atomic_load_n(&sp_ready, __ATOMIC_ACQUIRE)) {
		return SWEET_SPMI_NOT_READY;
	}
	sweet_spmi_irq_lock();
	int result = sp_charger_route(sid, addr, out);
	sweet_spmi_irq_unlock();
	return result;
}

int
sweet_spmi_charger_read(unsigned sid, unsigned addr, uint8_t *out)
{
	if (!out || !sp_charger_register(sid, addr)) {
		return SWEET_SPMI_INVALID;
	}
	if (!__atomic_load_n(&sp_ready, __ATOMIC_ACQUIRE)) {
		return SWEET_SPMI_NOT_READY;
	}
	struct sweet_spmi_charger_route route;
	sweet_spmi_irq_lock();
	int result = sp_charger_route(sid, addr, &route);
	if (result == SWEET_SPMI_OK) {
		result = sp_charger_read_locked(route.apid, addr, out);
	}
	sweet_spmi_irq_unlock();
	return result;
}

/* Read-only ADC5 capability preflight. No converter ownership is claimed.
 * Observer selection is independent of the writer; never touch foreign RW. */
static void
sp_adc5_route(unsigned peripheral, struct sweet_adc5_route *out)
{
 out->observer_apid = sp_find_apid(0x200u | peripheral);
 out->writer_apid = out->irq_apid = SP_NO_APID;
 out->irq_error = SWEET_SPMI_NO_OWNER;
 for (unsigned apid = 0; apid < sp_count; ++apid) {
  uint32_t map = sp_rd(sp_core, SP_APID_MAP_OFF + 4u * apid);
  if (!map || ((map >> 8) & 0xfffu) != (0x200u | peripheral) ||
      !(map & SP_IRQ_OWNER_BIT) ||
      (sp_rd(sp_cfg, SP_OWNERSHIP_OFF + 4u * apid) & 7u) != 0) { continue; }
  if (out->irq_apid != SP_NO_APID) {
   out->irq_error = SWEET_SPMI_AMBIGUOUS; break;
  }
  out->irq_apid = (uint16_t)apid; out->irq_error = SWEET_SPMI_OK;
 }
 struct sweet_spmi_charger_route route;
 out->writer_error = sp_charger_route(2, peripheral << 8, &route);
 if (!out->writer_error) {
  out->writer_apid = route.apid;
  out->writer_ee = route.write_ee;
  out->irq_owner = route.irq_owner;
  if (sweet_spmi_rw_poisoned_locked(route.apid)) { out->writer_error = SWEET_SPMI_POISONED; }
 }
}

int
sweet_spmi_adc5_inspect(struct sweet_adc5_preflight *out)
{
 if (!out) { return SWEET_SPMI_INVALID; }
 if (!__atomic_load_n(&sp_ready, __ATOMIC_ACQUIRE)) { return SWEET_SPMI_NOT_READY; }
 sweet_spmi_irq_lock();
 uint32_t version = sp_rd(sp_core, 0);
 if (version < 0x50000000u || version >= 0x60000000u) {
  sweet_spmi_irq_unlock(); return SWEET_SPMI_UNSUPPORTED;
 }
 struct sweet_adc5_preflight next = {0};
 sp_adc5_route(0x31, &next.user);
 sp_adc5_route(0x37, &next.calibration);
 static const unsigned addresses[SWEET_ADC5_FIELD_COUNT] = {
  0x3104, 0x3105, 0x3108, 0x3109, 0x3142, 0x3143, 0x3144,
  0x3145, 0x3146, 0x3147, 0x3150, 0x3151, 0x3744
 };
 for (unsigned i = 0; i < SWEET_ADC5_FIELD_COUNT; ++i) {
  unsigned apid = i == SWEET_ADC5_CAL_DELAY ? next.calibration.observer_apid : next.user.observer_apid;
  int rc = apid == SP_NO_APID ? SWEET_SPMI_NO_OWNER :
      sp_charger_read_locked(apid, addresses[i], &next.raw[i]);
  next.errors[i] = rc;
  if (!rc) { next.valid |= 1u << i; }
 }
 *out = next;
 sweet_spmi_irq_unlock();
 return SWEET_SPMI_OK;
}

/* Internal primitive for a separately validated protocol register. */
static int
sp_update_owned_locked(unsigned sid, unsigned addr, unsigned mask,
    unsigned value, uint8_t *verified)
{
	struct sweet_spmi_charger_route route;
	uint8_t old = 0, actual = 0;
	vm_offset_t channel = 0;
	int result = sp_charger_route(sid, addr, &route);
	if (result == SWEET_SPMI_OK && sweet_spmi_rw_poisoned_locked(route.apid)) {
		result = SWEET_SPMI_POISONED;
	}
	if (result == SWEET_SPMI_OK) {
		result = sp_charger_read_locked(route.apid, addr, &old);
	}
	uint8_t next = (old & ~mask) | value;
	if (result == SWEET_SPMI_OK && old != next) {
		/* Temporary kernel_map allocation; never unmap permanent ml_io_map. */
		channel = ml_io_map_unmappable(
		    SP_CHNLS_PA + SP_CHNLS_APID_STRIDE * route.apid, 0x1000, VM_WIMG_IO);
		if (!channel) {
			result = SWEET_SPMI_MAP_FAILED;
		} else {
			sp_wr(channel, 0x10u, next);
			sp_wr(channel, 0, (addr & 0xffu) << 4); /* EXT_WRITEL, BC=0 */
			result = sp_charger_wait(channel);
			if (result == SWEET_SPMI_TIMEOUT) {
				sweet_spmi_rw_poison_locked(route.apid);
			}
		}
		if (result == SWEET_SPMI_OK) {
			result = sp_charger_read_locked(route.apid, addr, &actual);
			if (result == SWEET_SPMI_OK && ((actual ^ next) & mask)) {
				result = SWEET_SPMI_VERIFY;
			}
		}
	} else {
		actual = old; /* no-change success needs no RW mapping or command */
	}
	if (channel) {
		ml_io_unmap(trunc_page(channel), round_page(0x1000 + (channel & PAGE_MASK)));
	}
	if (result == SWEET_SPMI_OK) {
		*verified = actual;
	}
	if (result != SWEET_SPMI_OK) {
		SWEET_LOG_ERROR("SWEET CHARGER IO: update addr=%#x mask=%#x value=%#x error=%d; hardware state unconfirmed\n",
		    addr, mask, value, result);
	}
	return result;
}
static int
sp_update_owned(unsigned sid, unsigned addr, unsigned mask,
    unsigned value, uint8_t *verified)
{
 if (!__atomic_load_n(&sp_ready, __ATOMIC_ACQUIRE)) { return SWEET_SPMI_NOT_READY; }
 sweet_spmi_irq_lock();
 int rc = sp_update_owned_locked(sid, addr, mask, value, verified);
 sweet_spmi_irq_unlock();
 return rc;
}

int
sweet_spmi_charger_update(unsigned sid, unsigned addr, unsigned mask,
    unsigned value, uint8_t *verified)
{
	const struct sp_charger_register *reg = sp_charger_register(sid, addr);
	if (!verified || !reg || !mask || (mask & ~reg->write_mask) ||
	    (value & ~mask)) {
		return SWEET_SPMI_INVALID;
	}
	return sp_update_owned(sid, addr, mask, value, verified);
}


/* Fixed BARK_BITE_WDOG_PET pulse (LG smb5-lib.c:wdog_bark_irq_handler).
 * Completion proves bus acceptance, not that the hardware timer restarted.
 * No read/modify/write/readback, no skipped identical value, no retry. */
int
sweet_spmi_charger_pet_watchdog(void)
{
 if (!__atomic_load_n(&sp_ready, __ATOMIC_ACQUIRE)) return SWEET_SPMI_NOT_READY;
 sweet_spmi_irq_lock();
 struct sweet_spmi_charger_route route;
 int rc=sp_charger_route(2,0x1643,&route);
 if (!rc && sweet_spmi_rw_poisoned_locked(route.apid)) rc=SWEET_SPMI_POISONED;
 vm_offset_t channel=0;
 if (!rc) {
  channel=ml_io_map_unmappable(SP_CHNLS_PA+SP_CHNLS_APID_STRIDE*route.apid,0x1000,VM_WIMG_IO);
  if (!channel) rc=SWEET_SPMI_MAP_FAILED;
 }
 if (!rc) {
  sp_wr(channel,0x10,1);
  sp_wr(channel,0,0x43u<<4);
  rc=sp_charger_wait(channel);
  if (rc==SWEET_SPMI_TIMEOUT) sweet_spmi_rw_poison_locked(route.apid);
 }
 if (channel) ml_io_unmap(trunc_page(channel),round_page(0x1000+(channel&PAGE_MASK)));
 sweet_spmi_irq_unlock();
 return rc;
}

/* ADC5 fixed protocol. The lease belongs to the controller, not a discoverable
 * service singleton. No arbitrary register API or conversion-time sleeps.
 * Every phase and cleanup is under sp_lock; EOC/settle waits release it. */
static struct sweet_adc5_session *sp_adc5_owner;
static bool sp_adc5_failed;
static const unsigned sp_adc5_saved_addr[] = {0x3142,0x3143,0x3144,0x3145,0x3146,0x3744};
static const unsigned sp_adc5_saved_mask[] = {0x7c,0x07,0xff,0x0f,0x80,0xff};

static uint64_t
sp_adc5_now(void)
{
 uint64_t absolute, ns;
 clock_get_uptime(&absolute); absolutetime_to_nanoseconds(absolute, &ns);
 return ns;
}

/* Internal observer transaction for the fixed ADC5 protocol and PMIC
 * identity tuple. This is deliberately not exported as an arbitrary raw
 * register API and never reuses the charger write whitelist. */
static int
sp_observer_read_locked(unsigned sid, unsigned addr, uint8_t *out)
{
 if (!out || sid > 15u || addr > 0xffffu) { return SWEET_SPMI_INVALID; }
 unsigned apid = sp_find_apid((sid << 8) | (addr >> 8));
 return apid == SP_NO_APID ? SWEET_SPMI_NO_OWNER : sp_charger_read_locked(apid, addr, out);
}

static int
sp_adc5_read_locked(unsigned addr, uint8_t *out)
{
 return sp_observer_read_locked(2, addr, out);
}
int
sweet_spmi_battery_irq_read(unsigned peripheral, unsigned offset, uint8_t *out)
{
 if (!out || (peripheral != 0x10 && peripheral != 0x13 &&
     peripheral != 0x15 && peripheral != 0x16 && peripheral != 0x40 && peripheral != 0x42) ||
     (offset != 0x11 && offset != 0x12 && offset != 0x13 && offset != 0x15)) {
  return SWEET_SPMI_INVALID;
 }
 if (!__atomic_load_n(&sp_ready, __ATOMIC_ACQUIRE)) { return SWEET_SPMI_NOT_READY; }
 sweet_spmi_irq_lock();
 int rc = sp_observer_read_locked(2, (peripheral << 8) | offset, out);
 sweet_spmi_irq_unlock();
 return rc;
}
int
sweet_spmi_adc5_irq_read(unsigned offset, uint8_t *out)
{
 if (!out || (offset != 0x11 && offset != 0x12 && offset != 0x13 && offset != 0x15)) { return SWEET_SPMI_INVALID; }
 if (!__atomic_load_n(&sp_ready, __ATOMIC_ACQUIRE)) { return SWEET_SPMI_NOT_READY; }
 sweet_spmi_irq_lock();
 int rc = sp_adc5_read_locked(0x3100 | offset, out);
 sweet_spmi_irq_unlock();
 return rc;
}

static int
sp_adc5_capture_fault_locked(struct sweet_adc5_fault *out, uint64_t entered)
{
 uint32_t version=sp_rd(sp_core,0);
 if (version<0x50000000u || version>=0x60000000u) return SWEET_SPMI_UNSUPPORTED;
 struct sweet_adc5_fault next = {0}; next.entered_ns=entered;
 next.locked_ns=sp_adc5_now();
 /* status, sequence, channel, enable, request, RT, type, EN_SET, latched, cal */
 static const unsigned regs[SWEET_ADC5_FAULT_FIELDS]={0x3108,0x3109,0x3144,0x3146,0x3147,0x3110,0x3111,0x3115,0x3118,0x3744};
 for (unsigned i=0;i<SWEET_ADC5_FAULT_FIELDS;++i)
  next.errors[i]=sp_observer_read_locked(2,regs[i],&next.raw[i]);
 next.completed_ns=sp_adc5_now(); *out=next; return 0;
}

int
sweet_spmi_adc5_capture_fault(struct sweet_adc5_fault *out)
{
 if (!out) return SWEET_SPMI_INVALID;
 if (!__atomic_load_n(&sp_ready,__ATOMIC_ACQUIRE)) return SWEET_SPMI_NOT_READY;
 struct sweet_adc5_fault next = {0}; next.entered_ns=sp_adc5_now();
 static const unsigned regs[SWEET_ADC5_FAULT_FIELDS]={0x3108,0x3109,0x3144,0x3146,0x3147,0x3110,0x3111,0x3115,0x3118,0x3744};
 /* Independent diagnostic fields, not an atomic image or a conversion result.
  * Give a waiting ACK the bus between fields; never split protocol restore. */
 for (unsigned i=0;i<SWEET_ADC5_FAULT_FIELDS;++i) {
  sweet_spmi_irq_lock();
  if (!i) next.locked_ns=sp_adc5_now();
  uint32_t version=sp_rd(sp_core,0);
  if (version<0x50000000u || version>=0x60000000u) {
   sweet_spmi_irq_unlock(); return SWEET_SPMI_UNSUPPORTED;
  }
  next.errors[i]=sp_observer_read_locked(2,regs[i],&next.raw[i]);
  sweet_spmi_irq_unlock();
 }
 next.completed_ns=sp_adc5_now(); *out=next; return 0;
}

/* Called only for a session owned by this protocol, before its error restore.
 * Preserve the original error/output contract; a snapshot grants no result. */
static void
sp_adc5_save_fault_locked(struct sweet_adc5_session *session)
{
 session->fault_error=sp_adc5_capture_fault_locked(&session->fault,sp_adc5_now());
 session->fault_saved=1;
}

int
sweet_spmi_watchdog_capture_fault(struct sweet_watchdog_fault *out)
{
 if (!out) return SWEET_SPMI_INVALID;
 if (!__atomic_load_n(&sp_ready,__ATOMIC_ACQUIRE)) return SWEET_SPMI_NOT_READY;
 struct sweet_watchdog_fault next={0}; next.entered_ns=sp_adc5_now();
 /* MISC RT, type, EN_SET, latched, WD_CFG, timeout, SMB output.
  * Like the independent ADC diagnostic, yield between fixed read-only fields. */
 static const unsigned regs[SWEET_WDOG_FAULT_FIELDS]={0x1610,0x1611,0x1615,0x1618,0x1651,0x1653,0x1648};
 for (unsigned i=0;i<SWEET_WDOG_FAULT_FIELDS;++i) {
  sweet_spmi_irq_lock();
  if (!i) next.locked_ns=sp_adc5_now();
  uint32_t version=sp_rd(sp_core,0);
  if (version<0x50000000u || version>=0x60000000u) {
   sweet_spmi_irq_unlock(); return SWEET_SPMI_UNSUPPORTED;
  }
  next.errors[i]=sp_observer_read_locked(2,regs[i],&next.raw[i]);
  sweet_spmi_irq_unlock();
 }
 next.completed_ns=sp_adc5_now(); *out=next; return 0;
}

static int
sp_adc5_set_locked(struct sweet_adc5_session *session, unsigned index, unsigned value)
{
 if (sp_adc5_now() >= session->deadline_ns) { return SWEET_ADC5_EXPIRED; }
 uint8_t actual;
 session->touched |= 1u << index; /* a failed command may still have changed HW */
 return sp_update_owned_locked(2, sp_adc5_saved_addr[index],
     sp_adc5_saved_mask[index], value, &actual);
}

/* CONV_REQ is a trigger, never a stable masked update or retry/readback.
 * Revalidate the unique EE0 writer and the shared RW poison for each request. */
static int
sp_adc5_trigger_locked(struct sweet_adc5_session *session)
{
 if (sp_adc5_now() >= session->deadline_ns) { return SWEET_ADC5_EXPIRED; }
 session->trigger_enter_ns = sp_adc5_now();
 session->trigger_done_ns = 0;
 struct sweet_spmi_charger_route route;
 int rc = sp_charger_route(2, 0x3147, &route);
 if (!rc && sweet_spmi_rw_poisoned_locked(route.apid)) { rc = SWEET_SPMI_POISONED; }
 if (rc) { return rc; }
 vm_offset_t channel = ml_io_map_unmappable(
     SP_CHNLS_PA + SP_CHNLS_APID_STRIDE * route.apid, 0x1000, VM_WIMG_IO);
 if (!channel) { return SWEET_SPMI_MAP_FAILED; }
 sp_wr(channel, 0x10u, 0x80);
 sp_wr(channel, 0, 0x47u << 4);
 rc = sp_charger_wait(channel);
 session->trigger_done_ns = sp_adc5_now();
 if (rc == SWEET_SPMI_TIMEOUT) { sweet_spmi_rw_poison_locked(route.apid); }
 ml_io_unmap(trunc_page(channel), round_page(0x1000 + (channel & PAGE_MASK)));
 return rc;
}

/* Best effort, bounded restoration. Cal is on another APID and must still be
 * restored if the user lane was poisoned. Never restart a saved conversion.
 * Entry requires idle/disabled, so saved ADC enable is always zero. */
static int
sp_adc5_restore_locked(struct sweet_adc5_session *session)
{
 int error = 0, rc;
 uint8_t actual = 0, status = 0;
 if (session->touched) {
  rc = sp_update_owned_locked(2, 0x3146, 0x80, 0, &actual);
  if (rc) { error = rc; }
  rc = sp_adc5_read_locked(0x3108, &status);
  if (!rc && (status & 2)) { rc = SWEET_SPMI_BUSY; }
  if (rc && !error) { error = rc; }
  /* Do not change channel/configuration of a still-running converter. */
  if (!rc) {
   for (unsigned i = 0; i < 4; ++i) {
    if (!(session->touched & (1u << i))) { continue; }
    rc = sp_update_owned_locked(2, sp_adc5_saved_addr[i], sp_adc5_saved_mask[i],
        session->saved[i] & sp_adc5_saved_mask[i], &actual);
    if (rc && !error) { error = rc; }
   }
  }
  if (session->touched & (1u << 5)) {
   rc = sp_update_owned_locked(2, 0x3744, 0xff, session->saved[5], &actual);
   if (rc && !error) { error = rc; }
  }
 }
 session->restore_error = error;
 session->active = 0;
 sp_adc5_owner = NULL;
 if (error) {
  sp_adc5_failed = true;
  SWEET_LOG_ERROR("SWEET ADC5 IO: restore error=%d status=%#x; ADC unavailable until reboot\n", error, status);
 }
 return error;
}

static int
sp_adc5_launch_locked(struct sweet_adc5_session *session, bool ground)
{
 int rc;
 session->deadline_ns = sp_adc5_now() + 100000000ULL;
 if (ground) {
  rc = sp_adc5_set_locked(session, 2, 0); /* REF_GND */
  if (!rc) { rc = sp_adc5_set_locked(session, 4, 0x80); }
  if (!rc) { rc = sp_adc5_trigger_locked(session); }
  /* LG USBIN workaround: select ABS_CAL and USBIN before ground EOC. */
  if (!rc) { rc = sp_adc5_set_locked(session, 0, 0x28); }
  if (!rc) { rc = sp_adc5_set_locked(session, 2, 0x08); }
  session->phase = SWEET_ADC5_GROUND;
 } else {
  rc = sp_adc5_set_locked(session, 0, 0x28); /* absolute/timer, decimation 840 */
  if (!rc) { rc = sp_adc5_set_locked(session, 1, 0); } /* one averaged sample */
  if (!rc) { rc = sp_adc5_set_locked(session, 2, 0x4b); }
  if (!rc) { rc = sp_adc5_set_locked(session, 3, 0); } /* DT settle index 0 */
  if (!rc) { rc = sp_adc5_set_locked(session, 4, 0x80); }
  if (!rc) { rc = sp_adc5_trigger_locked(session); }
  session->phase = SWEET_ADC5_RESULT;
 }
 return rc;
}

int
sweet_spmi_adc5_begin(struct sweet_adc5_session *session, enum sweet_adc5_channel channel)
{
 if (!session || (channel != SWEET_ADC5_BAT_ID && channel != SWEET_ADC5_USBIN)) { return SWEET_SPMI_INVALID; }
 if (!__atomic_load_n(&sp_ready, __ATOMIC_ACQUIRE)) { return SWEET_SPMI_NOT_READY; }
 sweet_spmi_irq_lock();
 int rc = SWEET_SPMI_OK;
 if (sp_adc5_failed) { rc = SWEET_SPMI_POISONED; }
 else if (sp_adc5_owner) { rc = SWEET_SPMI_BUSY; }
 struct sweet_adc5_session next = {0};
 uint8_t type = 0, subtype = 0, pmic = 0, revision = 0, request = 0, status = 0;
 struct sweet_spmi_charger_route route;
 if (!rc) { rc = sp_charger_route(2, 0x3142, &route); }
 if (!rc && sweet_spmi_rw_poisoned_locked(route.apid)) { rc = SWEET_SPMI_POISONED; }
 if (!rc) { rc = sp_charger_route(2, 0x3744, &route); }
 if (!rc && sweet_spmi_rw_poisoned_locked(route.apid)) { rc = SWEET_SPMI_POISONED; }
 if (!rc) { rc = sp_adc5_read_locked(0x3104, &type); }
 if (!rc) { rc = sp_adc5_read_locked(0x3105, &subtype); }
 if (!rc) { rc = sp_adc5_read_locked(0x0105, &pmic); }
 if (!rc) { rc = sp_adc5_read_locked(0x0103, &revision); }
 /* Only the target tuple verified in 0120a3; no guessed layout fallback. */
 if (!rc && (type != 8 || subtype != 0x28 || pmic != 0x20 || revision != 2)) { rc = SWEET_SPMI_UNSUPPORTED; }
 if (!rc) { rc = sp_adc5_read_locked(0x3108, &status); }
 if (!rc) { rc = sp_adc5_read_locked(0x3147, &request); }
 for (unsigned i = 0; !rc && i < 6; ++i) { rc = sp_adc5_read_locked(sp_adc5_saved_addr[i], &next.saved[i]); }
 if (!rc && ((status & 2) || (request & 0x80) || (next.saved[4] & 0x80))) { rc = SWEET_SPMI_BUSY; }
 if (!rc) {
  next.channel = channel; next.active = 1; next.started_ns = sp_adc5_now();
  next.deadline_ns = next.started_ns + 100000000ULL;
  *session = next; sp_adc5_owner = session;
  if (channel == SWEET_ADC5_USBIN) {
   rc = sp_adc5_set_locked(session, 5, 0x73);
   session->settle_until_ns = sp_adc5_now() + 20000000ULL;
   session->phase = SWEET_ADC5_WAIT_SETTLE;
  } else { rc = sp_adc5_launch_locked(session, false); }
  if (rc) { sp_adc5_save_fault_locked(session); (void)sp_adc5_restore_locked(session); }
 }
 sweet_spmi_irq_unlock();
 return rc;
}

int
sweet_spmi_adc5_advance(struct sweet_adc5_session *session, enum sweet_adc5_cause cause,
    struct sweet_adc5_measurement *out)
{
 if (!session || !out) { return SWEET_SPMI_INVALID; }
 if (!__atomic_load_n(&sp_ready, __ATOMIC_ACQUIRE)) { return SWEET_SPMI_NOT_READY; }
 uint64_t entered = sp_adc5_now();
 sweet_spmi_irq_lock();
 int rc = SWEET_SPMI_OK;
 if (sp_adc5_owner != session || !session->active) { sweet_spmi_irq_unlock(); return SWEET_SPMI_INVALID; }
 session->advance_enter_ns = entered; session->advance_locked_ns = sp_adc5_now();
 session->diagnostic_phase = session->phase; session->diagnostic_deadline_ns = session->deadline_ns;
 uint64_t now = sp_adc5_now();
 if (now >= session->deadline_ns) { rc = SWEET_ADC5_EXPIRED; }
 if (!rc && session->phase == SWEET_ADC5_WAIT_SETTLE) {
  if (cause != SWEET_ADC5_SETTLED || now < session->settle_until_ns) { sweet_spmi_irq_unlock(); return SWEET_SPMI_BUSY; }
  rc = sp_adc5_launch_locked(session, true);
  if (!rc) { sweet_spmi_irq_unlock(); return SWEET_ADC5_MORE; }
 } else if (!rc) {
  if (cause != SWEET_ADC5_EOC) { sweet_spmi_irq_unlock(); return SWEET_SPMI_INVALID; }
  uint8_t status = 0, channel = 0;
  rc = sp_adc5_read_locked(0x3108, &status);
  if (!rc && (status & 3) != 1) { rc = SWEET_SPMI_VERIFY; }
  if (!rc) { rc = sp_adc5_read_locked(0x3144, &channel); }
  if (!rc && channel != (session->channel == SWEET_ADC5_BAT_ID ? 0x4b : 0x08)) { rc = SWEET_SPMI_VERIFY; }
  if (!rc && session->phase == SWEET_ADC5_GROUND) {
   ++session->eoc_count;
   session->deadline_ns = sp_adc5_now() + 100000000ULL;
   rc = sp_adc5_trigger_locked(session);
   session->phase = SWEET_ADC5_RESULT;
   if (!rc) { sweet_spmi_irq_unlock(); return SWEET_ADC5_MORE; }
  } else if (!rc && session->phase == SWEET_ADC5_RESULT) {
   uint8_t lo = 0, hi = 0;
   rc = sp_adc5_read_locked(0x3150, &lo);
   if (!rc) { rc = sp_adc5_read_locked(0x3151, &hi); }
   unsigned raw = lo | ((unsigned)hi << 8);
   if (!rc && raw >= 0x8000) { rc = SWEET_SPMI_VERIFY; }
   struct sweet_adc5_measurement result = {0};
   if (!rc) {
    ++session->eoc_count;
    result.raw = (uint16_t)raw; result.channel = session->channel;
    result.microvolts = (unsigned)(((uint64_t)raw * 1875000u) / 0x70e4u);
    if (session->channel == SWEET_ADC5_USBIN) { result.microvolts *= 16u; }
    result.started_ns = session->started_ns; result.completed_ns = sp_adc5_now();
    result.eoc_count = session->eoc_count;
   }
   if (rc) { sp_adc5_save_fault_locked(session); }
   int restore = sp_adc5_restore_locked(session);
   if (!rc) { rc = restore; }
   if (!rc) { session->phase = SWEET_ADC5_DONE; *out = result; }
   else { session->phase = SWEET_ADC5_FAILED; }
   sweet_spmi_irq_unlock(); return rc;
  } else if (!rc) { rc = SWEET_SPMI_INVALID; }
 }
 if (rc) { sp_adc5_save_fault_locked(session); (void)sp_adc5_restore_locked(session); session->phase = SWEET_ADC5_FAILED; }
 sweet_spmi_irq_unlock();
 return rc;
}

int
sweet_spmi_adc5_abort(struct sweet_adc5_session *session)
{
 if (!session || !__atomic_load_n(&sp_ready, __ATOMIC_ACQUIRE)) { return SWEET_SPMI_INVALID; }
 sweet_spmi_irq_lock();
 int rc = SWEET_SPMI_OK;
 if (sp_adc5_owner == session && session->active) {
  rc = sp_adc5_restore_locked(session); session->phase = SWEET_ADC5_FAILED;
 }
 sweet_spmi_irq_unlock();
 return rc;
}

/* A separate protocol session, not part of the charger configuration table.
 * There are no public raw MEMIF writes or arbitrary SRAM word accesses.
 * The session lock never nests inside sp_lock; grant/ALG waits drop sp_lock.
 * One session may take up to ~0.9s. Call only from a consumer workloop/thread.
 * Unknown silicon/layout and an already claimed MEMIF fail closed. */
static IOLock *sp_fg_lock;
static bool sp_fg_failed;
static bool sp_fg_dma_prepared;
static bool sp_fg_dma_recovery_used;

/* Session-local failure provenance survives cleanup. Only diagnostic dedup
 * state has boot lifetime, protected by sp_fg_lock along with the session. */
struct sp_fg_session {
	unsigned addr;
	const char *operation;
	int error;
};
/* Four scalar fields plus the private fixed-profile operation. */
static int sp_fg_last_error[5];
static unsigned sp_fg_last_addr[5];
static const char *sp_fg_last_operation[5];

static int
sp_fg_record(struct sp_fg_session *session, unsigned addr,
    const char *operation, int result)
{
	if (result && !session->error) {
		session->addr = addr;
		session->operation = operation;
		session->error = result;
	}
	return result;
}

static void
sp_fg_route_diagnostics(void)
{
	static bool reported;
	if (reported) { return; }
	reported = true;
	/* Metadata only: no interrupt-bank mapping or foreign RW access. The
	 * observer window belongs to EE0 even if an APID's writer is another EE.
	 * Match Linux v5: first mapping, preferring the last local EE mapping. */
	static const unsigned peripherals[] = {0x01, 0x41, 0x42, 0x43, 0x45, 0x46, 0x47, 0x48};
	sweet_spmi_irq_lock();
	for (unsigned n = 0; n < sizeof(peripherals) / sizeof(peripherals[0]); ++n) {
		unsigned ppid = 0x200u | peripherals[n];
		unsigned matches = 0;
		uint16_t selected = sp_find_apid(ppid);
		for (unsigned apid = 0; apid < sp_count; ++apid) {
			uint32_t map = sp_rd(sp_core, SP_APID_MAP_OFF + 4u * apid);
			if (!map || ((map >> 8) & 0xfffu) != ppid) { continue; }
			++matches;
			unsigned owner = sp_rd(sp_cfg, SP_OWNERSHIP_OFF + 4u * apid) & 7u;
			SWEET_LOG_INFO("SWEET FG ROUTE: v2 ppid=%#x apid=%u write-ee=%u irq-owner=%u observer-selected=%u\n",
			    ppid, apid, owner, (map & SP_IRQ_OWNER_BIT) != 0, apid == selected);
		}
		if (!matches) {
			SWEET_LOG_WARN("SWEET FG ROUTE: v2 ppid=%#x has no mapping\n", ppid);
		}
	}
	sweet_spmi_irq_unlock();
}

static int
sp_fg_byte(struct sp_fg_session *session, unsigned addr, uint8_t *out)
{
	sweet_spmi_irq_lock();
	uint32_t version = sp_rd(sp_core, 0);
	int result = SWEET_SPMI_UNSUPPORTED;
	if (version >= 0x50000000u && version < 0x60000000u) {
		uint16_t apid = sp_find_apid(0x200u | (addr >> 8));
		result = apid == SP_NO_APID ? SWEET_SPMI_NO_OWNER :
		    sp_charger_read_locked(apid, addr, out);
	}
	sweet_spmi_irq_unlock();
	return sp_fg_record(session, addr, "observer-read", result);
}

static int
sp_fg_update(struct sp_fg_session *session, unsigned addr,
    unsigned mask, unsigned value)
{
	uint8_t verified;
	return sp_fg_record(session, addr, "protocol-update",
	    sp_update_owned(2, addr, mask, value, &verified));
}

static int
sp_fg_require_writer(struct sp_fg_session *session, unsigned addr)
{
	struct sweet_spmi_charger_route route;
	sweet_spmi_irq_lock();
	int result = sp_charger_route(2, addr, &route);
	if (!result && sweet_spmi_rw_poisoned_locked(route.apid)) {
		result = SWEET_SPMI_POISONED;
	}
	sweet_spmi_irq_unlock();
	return sp_fg_record(session, addr, "writer-preflight", result);
}

/* Fixed command, not a stable-configuration RMW: CLEAR_LOG can self-clear.
 * Caller holds both session and controller locks, checked idle/identity and
 * a unique EE0 writer. Do not export this as an arbitrary register writer. */
static int
sp_fg_dma_command_locked(unsigned apid, vm_offset_t channel, uint8_t value)
{
	if (sweet_spmi_rw_poisoned_locked(apid)) { return SWEET_SPMI_POISONED; }
	sp_wr(channel, 0x10u, value);
	sp_wr(channel, 0, 0x71u << 4);
	int result = sp_charger_wait(channel);
	if (result == SWEET_SPMI_TIMEOUT) { sweet_spmi_rw_poison_locked(apid); }
	return result;
}

static int
sp_fg_prepare_dma(struct sp_fg_session *session)
{
	struct sweet_spmi_charger_route route;
	uint8_t arb = 0, cfg = 0, before = 0, after = 0, ctl = 0, actual = 0;
	bool afterValid = false, actualValid = false;
	vm_offset_t channel = 0;
	sweet_spmi_irq_lock();
	int result = sp_charger_route(2, 0x4371, &route);
	if (!result && sweet_spmi_rw_poisoned_locked(route.apid)) { result = SWEET_SPMI_POISONED; }
	sp_fg_record(session, 0x4371, "dma-writer", result);
	/* Recheck idle atomically with the clear command against local users.
	 * This does not claim to serialize external execution environments. */
	if (!result) { result = sp_fg_record(session, 0x4340, "dma-pre-read",
	    sp_charger_read_locked(route.apid, 0x4340, &arb)); }
	if (!result) { result = sp_fg_record(session, 0x4350, "dma-pre-read",
	    sp_charger_read_locked(route.apid, 0x4350, &cfg)); }
	if (!result && ((arb & 1) || (cfg & 0xa0))) {
		result = sp_fg_record(session, (arb & 1) ? 0x4340 : 0x4350,
		    "dma-pre-busy", SWEET_SPMI_BUSY);
	}
	if (!result) { result = sp_fg_record(session, 0x4370, "dma-pre-read",
	    sp_charger_read_locked(route.apid, 0x4370, &before)); }
	if (!result) { result = sp_fg_record(session, 0x4371, "dma-pre-read",
	    sp_charger_read_locked(route.apid, 0x4371, &ctl)); }
	if (!result && (!sp_fg_dma_prepared || (before & 6) || (ctl & 1))) {
		SWEET_LOG_INFO("SWEET FG DMA: v3 phase=pre sts=%#x ctl=%#x arb=%#x cfg=%#x initialized=%u recovery-used=%u\n",
		    before, ctl, arb, cfg, sp_fg_dma_prepared, sp_fg_dma_recovery_used);
	}
	if (!result && sp_fg_dma_prepared && ((before & 6) || (ctl & 1))) {
		/* New errors after initialization are evidence, not a reason to clear
		 * and silently retry a failed sample on the next timer tick. */
		sp_fg_failed = true;
		result = sp_fg_record(session, 0x4370, "dma-pre-status", SWEET_SPMI_IO);
	}
	if (!result && !sp_fg_dma_prepared && ((before & 6) || (ctl & 1))) {
		if (sp_fg_dma_recovery_used) {
			result = sp_fg_record(session, 0x4370, "dma-recovery-used", SWEET_SPMI_IO);
		} else {
			sp_fg_dma_recovery_used = true; /* one bounded attempt this boot */
			channel = ml_io_map_unmappable(
			    SP_CHNLS_PA + SP_CHNLS_APID_STRIDE * route.apid, 0x1000, VM_WIMG_IO);
			if (!channel) { result = sp_fg_record(session, 0x4371,
			    "dma-clear-map", SWEET_SPMI_MAP_FAILED); }
			uint8_t base = ctl & ~1u;
			bool highAttempted = false, lowConfirmed = false;
			/* Linux sets CLEAR_LOG for errors and clears it on the next check.
			 * Explicit low/high/low also handles an inherited asserted bit.
			 * Preserve ADDR_KIND and every unrelated control bit throughout. */
			for (unsigned step = 0; !result && step < 3; ++step) {
				uint8_t value = base | (step == 1 ? 1 : 0);
				actualValid = false;
				if (step == 1) { highAttempted = true; }
				result = sp_fg_record(session, 0x4371, "dma-clear-command",
				    sp_fg_dma_command_locked(route.apid, channel, value));
				if (!result) { result = sp_fg_record(session, 0x4371, "dma-clear-readback",
				    sp_charger_read_locked(route.apid, 0x4371, &actual)); }
				actualValid = !result;
				/* The asserted bit may self-clear. Never demand a high readback. */
				if (!result && ((actual & ~1u) != base || (step != 1 && (actual & 1)))) {
					result = sp_fg_record(session, 0x4371, "dma-clear-verify", SWEET_SPMI_VERIFY);
				}
				if (!result && step == 2) { lowConfirmed = true; }
			}
			if (highAttempted && !lowConfirmed && !sweet_spmi_rw_poisoned_locked(route.apid)) {
				/* An error may follow an effective write: try deassert once, but
				 * never reuse a timed-out RW channel or erase the primary error. */
				int cleanup = sp_fg_dma_command_locked(route.apid, channel, base);
				if (!cleanup) { cleanup = sp_charger_read_locked(route.apid, 0x4371, &actual); }
				actualValid = !cleanup;
				lowConfirmed = !cleanup && actual == base;
				if (!lowConfirmed) {
					SWEET_LOG_ERROR("SWEET FG DMA: v3 clear deassert unconfirmed error=%d ctl=%#x; session disabled\n",
					    cleanup, actual);
				}
			}
			if (highAttempted && !lowConfirmed && sweet_spmi_rw_poisoned_locked(route.apid)) {
				SWEET_LOG_ERROR("SWEET FG DMA: v3 clear deassert unconfirmed; RW lane poisoned, no further writes\n");
			}
			if (channel) {
				ml_io_unmap(trunc_page(channel), round_page(0x1000 + (channel & PAGE_MASK)));
			}
			/* Status may settle after the command. Poll only five times, with
			 * no additional clear commands and at most 40us explicit delay. */
			for (unsigned n = 0; !result && n < 5; ++n) {
				result = sp_fg_record(session, 0x4370, "dma-clear-status-read",
				    sp_charger_read_locked(route.apid, 0x4370, &after));
				afterValid = !result;
				if (result || !(after & 6)) { break; }
				if (n != 4) { delay(10); }
			}
			if (!result && (after & 6)) { result = sp_fg_record(session, 0x4370,
			    "dma-clear-status", SWEET_SPMI_IO); }
			if (result) { sp_fg_failed = true; }
			SWEET_LOG_INFO("SWEET FG DMA: v3 phase=recovery before=%#x after=%#x after-ok=%u ctl=%#x ctl-ok=%u low-confirmed=%u error=%d\n",
			    before, after, afterValid, actual, actualValid, lowConfirmed, result);
			if (result) { SWEET_LOG_ERROR("SWEET FG DMA: v3 startup recovery failed error=%d; session disabled until reboot\n", result); }
		}
	}
	if (!result) { sp_fg_dma_prepared = true; }
	sweet_spmi_irq_unlock();
	return result;
}

static int
sp_fg_release(struct sp_fg_session *session)
{
	/* Always attempt both, even when the first command fails. */
	int first = sp_fg_update(session, 0x4350, 0xa0, 0);
	int second = sp_fg_update(session, 0x4340, 0x01, 0);
	if (first || second) {
		sp_fg_failed = true;
		SWEET_LOG_ERROR("SWEET FG SRAM: release failed intf=%d arb=%d; session disabled until reboot\n",
		    first, second);
	}
	return first ? first : second;
}

static uint64_t
sp_fg_now(void)
{
	uint64_t ticks, ns;
	clock_get_uptime(&ticks);
	absolutetime_to_nanoseconds(ticks, &ns);
	return ns;
}

static int
sp_fg_read_fixed(unsigned field, uint16_t *out, struct sweet_fg_profile *profile)
{
	if (!__atomic_load_n(&sp_ready, __ATOMIC_ACQUIRE)) { return SWEET_SPMI_NOT_READY; }
	IOLock *lock = __atomic_load_n(&sp_fg_lock, __ATOMIC_ACQUIRE);
	if (!lock) {
		IOLock *candidate = IOLockAlloc(), *expected = NULL;
		if (!candidate) { return SWEET_SPMI_NOT_READY; }
		if (__atomic_compare_exchange_n(&sp_fg_lock, &expected, candidate, false,
		    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) { lock = candidate; }
		else { IOLockFree(candidate); lock = expected; }
	}
	IOLockLock(lock);
	struct sweet_fg_profile next = {0};
	if (profile) { next.started_ns = sp_fg_now(); }
	struct sp_fg_session session = {0, "session-disabled", 0};
	sp_fg_route_diagnostics();
	int result = sp_fg_failed ? SWEET_SPMI_POISONED : SWEET_SPMI_OK;
	uint8_t revision = 0, subtype = 0, memtype = 0, cfg = 0, arb = 0, dma = 0;
	uint8_t lo = 0, hi = 0, peek = 0, active = 0;
	bool requested = false, peekChanged = false;
	unsigned word = 0, addr = 0;
	if (!result) { result = sp_fg_byte(&session, 0x0105, &subtype); }
	if (!result) { result = sp_fg_byte(&session, 0x0103, &revision); }
	if (!result) { result = sp_fg_byte(&session, 0x4305, &memtype); }
	if (!result && (subtype != 0x20 || (revision != 1 && revision != 2) || memtype != 0x0d)) {
		result = sp_fg_record(&session, subtype != 0x20 ? 0x0105 :
		    (revision != 1 && revision != 2) ? 0x0103 : 0x4305,
		    "identity-layout", SWEET_SPMI_UNSUPPORTED);
	}
	if (!result) { result = sp_fg_byte(&session, 0x4340, &arb); }
	if (!result) { result = sp_fg_byte(&session, 0x4350, &cfg); }
	if (!result && ((arb & 1) || (cfg & 0xa0))) {
		result = sp_fg_record(&session, (arb & 1) ? 0x4340 : 0x4350,
		    "session-busy", SWEET_SPMI_BUSY);
	}
	/* Validate all protocol writers before changing any session state. This
	 * never grants a foreign writer permission; every update checks again. */
	if (!result) { result = sp_fg_require_writer(&session, 0x4340); }
	if (!result) { result = sp_fg_require_writer(&session, 0x4350); }
	if (!result) { result = sp_fg_require_writer(&session, 0x4371); }
	if (!result && revision == 1) { result = sp_fg_require_writer(&session, 0x41ee); }
	if (!result) { result = sp_fg_prepare_dma(&session); }
	/* v1: select ALG_ACTIVE peek, wait for it to go low before requesting DMA.
	 * Restore our mux selection even on failure; never change FG algorithms. */
	if (!result && revision == 1) {
		result = sp_fg_byte(&session, 0x41ee, &peek);
		if (!result) {
			peekChanged = true;
			result = sp_fg_update(&session, 0x41ee, 0xff, 0xac);
		}
		for (unsigned n = 0; !result && n < 35; ++n) {
			result = sp_fg_byte(&session, 0x41ef, &active);
			if (result || !(active & 8)) { break; }
			IOSleep(10);
		}
		if (!result && (active & 8)) {
			result = sp_fg_record(&session, 0x41ef, "alg-wait", SWEET_SPMI_TIMEOUT);
		}
		if (!result) { IOSleep(1); }
	}
	if (!result) {
		/* We checked that no prior session owns MEMIF. From the first request
		 * attempt onward unwind both request bits, even after a failed write. */
		requested = true;
		result = sp_fg_update(&session, 0x4340, 0x01, 0x01);
	}
	if (!result) { result = sp_fg_update(&session, 0x4350, 0xa0, 0x80); }
	if (!result) { delay(40); }
	bool granted = false;
	for (unsigned n = 0; !result && n < 50; ++n) {
		result = sp_fg_byte(&session, 0x4310, &active);
		if (result) { break; }
		if (active & 8) { granted = true; break; }
		IOSleep(10);
	}
	if (!result && !granted) {
		result = sp_fg_record(&session, 0x4310, "grant-wait", SWEET_SPMI_TIMEOUT);
	}
	if (!result && revision == 1) { IOSleep(1); }
	/* Partition addressing bit must be set; it does not change SRAM contents.
	 * Preserve and restore the addressing mode around this session. */
	uint8_t dmaCfg = 0;
	bool dmaChanged = false;
	if (!result) { result = sp_fg_byte(&session, 0x4371, &dmaCfg); }
	if (!result && !(dmaCfg & 2)) {
		dmaChanged = true;
		result = sp_fg_update(&session, 0x4371, 2, 2);
	}
	if (profile) {
		/* Gen4 battery profile partition starts at word64 / DMA1 0x4520.
		 * PROFILE_LOAD_WORD65 maps to 0x4522, NOT the >=170 aperture.
		 * LG compares this fixed 24-byte prefix; also read its integrity and
		 * nominal capacity before releasing the same grant. */
		for (unsigned i = 0; !result && i < SWEET_FG_PROFILE_PREFIX_SIZE; ++i) {
			result = sp_fg_byte(&session, 0x4522 + i, &next.prefix[i]);
		}
		if (!result) { result = sp_fg_byte(&session, 0x4750, &next.integrity); }
		if (!result) { result = sp_fg_byte(&session, 0x46ea, &lo); }
		if (!result) { result = sp_fg_byte(&session, 0x46eb, &hi); }
		next.nominal_mah = (uint16_t)(lo | ((unsigned)hi << 8));
	} else {
		if (field == SWEET_FG_TEMPERATURE) { word = 328; }
		else if (field == SWEET_FG_NOMINAL_CAPACITY) { word = 271; }
		else if (field == SWEET_FG_PROFILE_INTEGRITY) { word = 299; }
		else { word = revision == 1 ? 285 : 287; }
		if (word >= 300) { addr = 0x4820 + (word - 300) * 2; }
		else if (word >= 275) { addr = 0x4720 + (word - 275) * 2; }
		else { addr = 0x4620 + (word - 170) * 2; }
		if (!result) { result = sp_fg_byte(&session, addr, &lo); }
		if (!result && field != SWEET_FG_PROFILE_INTEGRITY) { result = sp_fg_byte(&session, addr + 1, &hi); }
	}
	if (!result) { result = sp_fg_byte(&session, 0x4370, &dma); }
	if (!result && (dma & 6)) {
		SWEET_LOG_WARN("SWEET FG DMA: v3 phase=post sts=%#x field=%u; no clear or retry\n", dma, field);
		sp_fg_failed = true;
		result = sp_fg_record(&session, 0x4370, "dma-post-status", SWEET_SPMI_IO);
	}
	if (dmaChanged) {
		int cleanup = sp_fg_update(&session, 0x4371, 2, dmaCfg & 2);
		if (cleanup) { sp_fg_failed = true; result = cleanup; }
	}
	if (requested) {
		int cleanup = sp_fg_release(&session);
		if (cleanup) { result = cleanup; }
	}
	if (peekChanged) {
		int cleanup = sp_fg_update(&session, 0x41ee, 0xff, peek);
		if (cleanup) { sp_fg_failed = true; result = cleanup; }
	}
	if (!result) {
		if (profile) {
			next.completed_ns = sp_fg_now();
			*profile = next; /* Only publish after every cleanup succeeded. */
		} else { *out = (uint16_t)(lo | ((unsigned)hi << 8)); }
	}
	/* Report first failure and transitions only: a persistent fault must not
	 * produce one warning per protection tick. Primary provenance survives
	 * cleanup; final-error also exposes a failed unwind. */
	if (!sp_fg_last_operation[field] || sp_fg_last_error[field] != result ||
	    (result && (sp_fg_last_addr[field] != session.addr ||
	    strcmp(sp_fg_last_operation[field], session.operation) != 0))) {
		if (result) {
			SWEET_LOG_WARN("SWEET FG ACCESS: v2 field=%u op=%s sid=2 addr=%#x ppid=%#x error=%d final-error=%d; value=unknown\n",
			    field, session.operation, session.addr, 0x200u | (session.addr >> 8),
			    session.error ? session.error : result, result);
		} else if (profile) {
			SWEET_LOG_INFO("SWEET FG PROFILE: v1 fixed prefix24; same-session integrity=%#x nominal=%u recovered=%u\n",
			    next.integrity, next.nominal_mah, sp_fg_last_error[field] != 0);
		} else {
			SWEET_LOG_INFO("SWEET FG ACCESS: v2 field=%u rev=%u memtype=%#x raw=%#x recovered=%u\n",
			    field, revision, memtype, *out, sp_fg_last_error[field] != 0);
		}
	}
	sp_fg_last_error[field] = result;
	sp_fg_last_addr[field] = session.addr;
	sp_fg_last_operation[field] = session.operation;
	IOLockUnlock(lock);
	return result;
}

int
sweet_spmi_fg_read(unsigned field, uint16_t *out)
{
	if (!out || field > SWEET_FG_LEARNED_CAPACITY) { return SWEET_SPMI_INVALID; }
	return sp_fg_read_fixed(field, out, NULL);
}

int
sweet_spmi_fg_read_profile(struct sweet_fg_profile *out)
{
	if (!out) { return SWEET_SPMI_INVALID; }
	return sp_fg_read_fixed(4, NULL, out);
}

#endif /* ARM64_BOARD_CONFIG_MH2LM */

int
sweet_spmi_read(unsigned sid, unsigned addr, uint8_t *out)
{
	/* Restrict access to the battery blocks; keys retain their own lanes.
	 * The charger and the gauge live on PM8150B, which is SPMI USID 2 on
	 * guacamole (DT: qcom,pm8150b@2, reg = <0x2 0x0>), so the old sid != 0
	 * rule made every battery register unreachable. The allowed set is the
	 * DT's own block list for that PMIC: qcom,qpnp-smb5 chgr/dcdc/batif/usb/
	 * dc/typec/misc at 0x1000..0x1600, and qpnp,fg batt-soc/batt-info/rradc/
	 * memif at 0x4000..0x4300 -- the VBATT/IBATT words 0x41A0..0x41A3 are
	 * inside peripheral 0x41. 0x48 stays for the older gen3 layout.
	 * Read-only: this widens a predicate, it adds no write path, and the
	 * timeout rule below (one lane poisoned for the boot) is untouched. */
	unsigned peripheral = addr >> 8;
	if (!__atomic_load_n(&sp_ready, __ATOMIC_ACQUIRE) || !out || sid > 15u || addr > 0xffffu ||
	    (!(sid == 2u && peripheral == 0x01u) &&
	    peripheral != 0x10 && peripheral != 0x11 && peripheral != 0x12 &&
	    peripheral != 0x13 && peripheral != 0x16 && peripheral != 0x40 &&
	    peripheral != 0x41 && peripheral != 0x42 && peripheral != 0x43 &&
	    peripheral != 0x48)) {
		return -1;
	}
	sweet_spmi_irq_lock();
	uint16_t apid = sp_find_apid((sid << 8) | peripheral);
	if (apid == SP_NO_APID || sp_poisoned[apid]) {
		sweet_spmi_irq_unlock();
		return -1;
	}
	unsigned off = 0x80u * apid;
	uint32_t cmd = (1u << 27) | ((addr & 0xffu) << 4);
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(sp_obs + off) = cmd; /* EXT_READL, BC=0 */
	__asm__ volatile("dsb osh" ::: "memory");
	for (unsigned i = 0; i < 1000; ++i) {
		uint32_t status = sp_rd(sp_obs, off + 0x08u);
		if (status & 1u) {
			if (status & 0x0eu) {
				sweet_spmi_irq_unlock();
				return -1;
			}
			*out = (uint8_t)sp_rd(sp_obs, off + 0x18u);
			sweet_spmi_irq_unlock();
			return 0;
		}
		delay(1);
	}
	sp_poisoned[apid] = true;
	SWEET_LOG_ERROR("SWEET BATTERY: SPMI timeout sid=%u addr=%#x APID=%u disabled\n",
	    sid, addr, apid);
	sweet_spmi_irq_unlock();
	return -1;
}
#endif /* ARM64_BOARD_CONFIG_SWEET */
