/*
 * MH2LM's single EE0 PMIC peripheral SPI. The GIC top half masks INTID 513
 * before EOI and queues a provider-less IOInterruptEventSource; all PMIC-arb
 * and SPMI work runs on its IOWorkLoop. The table is independent of the
 * physical GIC handoff so future PMIC consumers can be registered here.
 *
 * Board startup owns infrastructure; keys and battery attach through the
 * route table after their consumer is ready. A fatal shared mux error notifies both groups.
 */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <device/device_types.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm64/sweet_dev.h>
#include <stdint.h>
#include <mach/vm_param.h>
#include <vm/memory_types.h>
#include <kern/clock.h>
#include "../../../bsd/dev/sweet_spmi.h"
#include "../../../bsd/dev/sweet_adc5.h"
#include "../../../bsd/dev/sweet_spmi_mux.h"
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"

extern "C" vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, unsigned int);
extern "C" void ml_io_unmap(vm_offset_t, vm_size_t);

#define MUX_GICD_PA       0x17a00000u
#define MUX_PDC_PA        0x0b220000u
#define MUX_INTID         513u
#define MUX_SPI_WORD      (MUX_INTID / 32u)
#define MUX_SPI_BIT       (1u << (MUX_INTID % 32u))
#define MUX_NO_APID       0xffffu
#define MUX_IRQ_OWNER_BIT 0x01000000u
#define MUX_IRQ_LIMIT     256u

static_assert(SWEET_MUX_ROUTE_COUNT <= 32u, "bounded batch route mask capacity");
static const MuxRoute routeTemplates[] = {
	{0, 0x08, 0, 1u << 0, MUX_KEYS, 3, "power"},
	{0, 0x08, 1, 1u << 2, MUX_KEYS, 3, "volume-down"},
	{0, 0xc5, 0, 1u << 1, MUX_KEYS, 3, "volume-up"},
	{2, 0x13, 4, SWEET_BATT_IRQ_USB, MUX_BATTERY, 3, "battery-usbin"},
	{2, 0x10, 1, SWEET_BATT_IRQ_CHG, MUX_BATTERY, 1, "battery-chgr"},
	{2, 0x40, 3, SWEET_BATT_IRQ_MSOC, MUX_BATTERY, 1, "battery-msoc"},
	{2, 0x40, 1, SWEET_BATT_IRQ_READY, MUX_BATTERY, 3, "battery-ready"},
	/* PM8150B DTS: source-change USBIN bit6 and CHGR error bit0 rising. */
	{2, 0x13, 6, SWEET_BATT_IRQ_SOURCE, MUX_BATTERY, 1, "battery-source"},
	{2, 0x10, 0, SWEET_BATT_IRQ_FAULT, MUX_BATTERY, 1, "battery-fault"},
	/* PM8150B Type-C DTS: each status event queues one typed input refresh.
	 * No CC/SPMI work runs in the GIC callback or consumer notification. */
	{2, 0x15, 0, SWEET_BATT_IRQ_TYPEC, MUX_BATTERY, 1, "battery-typec-rid"},
	{2, 0x15, 2, SWEET_BATT_IRQ_TYPEC, MUX_BATTERY, 1, "battery-typec-cc"},
	{2, 0x15, 3, SWEET_BATT_IRQ_TYPEC, MUX_BATTERY, 3, "battery-typec-vconn"},
	{2, 0x15, 4, SWEET_BATT_IRQ_TYPEC, MUX_BATTERY, 1, "battery-typec-vbus"},
	{2, 0x15, 5, SWEET_BATT_IRQ_TYPEC, MUX_BATTERY, 1, "battery-typec-attach"},
	{2, 0x15, 6, SWEET_BATT_IRQ_TYPEC, MUX_BATTERY, 3, "battery-typec-legacy"},
	{2, 0x31, 0, SWEET_ADC5_IRQ_EOC, MUX_ADC5, 1, "adc5-eoc"},
	/* LG PM8150B DTS: USBIN UV bit2 / OV bit3 both edges. Their recovery
	 * also refreshes input; the battery never treats the IRQ as a grant. */
	{2, 0x13, 2, SWEET_BATT_IRQ_INPUT_FAULT, MUX_BATTERY, 3, "battery-usbin-uv"},
	{2, 0x13, 3, SWEET_BATT_IRQ_INPUT_FAULT, MUX_BATTERY, 3, "battery-usbin-ov"},
	/* LG pm8150b.dtsi: MISC bark bit1, rising; opt-in diagnostic only. */
	{2, 0x16, 1, SWEET_BATT_IRQ_WDOG, MUX_BATTERY, 1, "battery-wdog-bark"},
	/* LG pm8150b.dtsi fg-rradc@4200 irq2 "batt-temp-delta", both edges; LG
	 * qpnp-fg-gen4 re-reads temperature and notifies the battery. The FG keeps
	 * its own delta threshold; this route only ACKs, never writes FG SRAM. */
	{2, 0x42, 2, SWEET_BATT_IRQ_TEMP, MUX_BATTERY, 3, "battery-temp-delta"},
};
static_assert(sizeof(routeTemplates) / sizeof(routeTemplates[0]) == SWEET_MUX_ROUTE_COUNT,
    "route layout must match the instance storage");

/* The board publishes a late nub. IOKit matches and owns this service. */
class SweetSpmiMuxDevice : public IOService {
	OSDeclareDefaultStructors(SweetSpmiMuxDevice);
};
OSDefineMetaClassAndStructors(SweetSpmiMuxDevice, IOService);

OSDefineMetaClassAndStructors(SweetSpmiInterruptMux, IOService);

/* The only service pointer global is the physical GIC dispatch slot. It is
 * not a consumer discovery API. Registry notifications own consumer references.
 * The spin lock synchronizes the top half with stop; never SPMI/workloop waits. */
static SweetSpmiInterruptMux *irqService;
static IOSimpleLock *irqLock;
static bool bootClaimed; /* protected by irqLock; terminal until reboot */

/* ml_io_map uses a permanent io_submap allocation in this XNU version.
 * Service-owned mappings must instead come from kernel_map's unmappable path.
 * Unmap the entire 16K allocation, including a physical base's page offset
 * (notably CFG at ...a000), rather than a sub-page register span. */
static vm_offset_t
mapMMIO(vm_offset_t physical, vm_size_t size)
{
	return ml_io_map_unmappable(physical, size, VM_WIMG_IO);
}

static void
unmapMMIO(vm_offset_t address, vm_size_t size)
{
	ml_io_unmap(trunc_page(address), round_page(size + (address & PAGE_MASK)));
}

static uint32_t
read32(vm_offset_t base, unsigned off)
{
	uint32_t value = *(volatile uint32_t *)(base + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return value;
}

static void
write32(vm_offset_t base, unsigned off, uint32_t value)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(base + off) = value;
	__asm__ volatile("dsb oshst" ::: "memory");
}

static uint64_t
read64(vm_offset_t base, unsigned off)
{
	uint64_t value = *(volatile uint64_t *)(base + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return value;
}

static void
write64(vm_offset_t base, unsigned off, uint64_t value)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint64_t *)(base + off) = value;
	__asm__ volatile("dsb oshst" ::: "memory");
}

bool
SweetSpmiInterruptMux::waitGic(void)
{
	for (unsigned i = 0; i < 1000; ++i) {
		if ((read32(gicd, 0x000u) & (1u << 31)) == 0) {
			return true;
		}
		delay(1);
	}
	return false;
}

uint16_t
SweetSpmiInterruptMux::findOwnedApid(unsigned sid, unsigned per, unsigned count)
{
	unsigned ppid = (sid << 8) | per;
	uint16_t result = MUX_NO_APID;
	for (unsigned i = 0; i < count; ++i) {
		uint32_t map = read32(core, 0x900u + 4u * i);
		if (((map >> 8) & 0xfffu) != ppid ||
		    (map & MUX_IRQ_OWNER_BIT) == 0) {
			continue;
		}
		if ((read32(cfg, 0x700u + 4u * i) & 7u) != 0) {
			continue; /* never access a foreign EE channel */
		}
		if (result != MUX_NO_APID) {
			return MUX_NO_APID; /* ambiguous EE0 IRQ owner */
		}
		result = (uint16_t)i;
	}
	return result;
}

/* Every controller transaction shares the transport lock, including keys.
 * Battery's observer API locks internally; key reads retain their owned APID. */
bool
SweetSpmiInterruptMux::readPeripheral(const MuxRoute &route, unsigned reg, uint8_t *out)
{
	if (!obs || !out || route.apid == MUX_NO_APID) {
		return false;
	}
	if (route.group == MUX_ADC5) {
		return sweet_spmi_adc5_irq_read(reg, out) == 0;
	}
	if (route.group == MUX_BATTERY) {
		/* Generic sweet_spmi_read deliberately excludes Type-C. IRQ readback
		 * has its own fixed, read-only capability, separate from charger data. */
		int rc = route.sid == 2 ? sweet_spmi_battery_irq_read(route.per, reg, out) : SWEET_SPMI_INVALID;
		if (rc) {
			SWEET_LOG_ERROR("SWEET MUX: IRQ read failed route=%s sid=%u per=%#x reg=%#x error=%d\n",
			    route.name, route.sid, route.per, reg, rc);
		}
		return rc == 0;
	}
	sweet_spmi_irq_lock();
	vm_offset_t lane = obs + 0x80u * route.apid;
	write32(lane, 0, (1u << 27) | ((reg & 0xffu) << 4));
	for (unsigned i = 0; i < 1000; ++i) {
		uint32_t status = read32(lane, 0x08u);
		if (status & 1u) {
			if (status & 0x0eu) {
				sweet_spmi_irq_unlock();
				SWEET_LOG_WARN("SWEET MUX: observer error route=%s reg=%#x status=%#x\n",
				    route.name, reg, status);
				return false;
			}
			*out = (uint8_t)read32(lane, 0x18u);
			sweet_spmi_irq_unlock();
			return true;
		}
		delay(1);
	}
	sweet_spmi_irq_unlock();
	SWEET_LOG_ERROR("SWEET MUX: observer timeout route=%s reg=%#x\n", route.name, reg);
	return false;
}

/* v5 EXT_WRITEL through the EE0 APID's RW channel, 1 or 2 bytes. */
bool
SweetSpmiInterruptMux::writePeripheral(MuxRoute &route, unsigned reg, uint16_t data,
    unsigned bytes, uint64_t *waitNs, uint64_t *holdNs)
{
	if (!route.channel || route.writePoisoned || bytes == 0 || bytes > 2) {
		return false;
	}
	/* Only IRQ configuration/ack is permitted for newly attached consumers. */
	bool shared = route.group != MUX_KEYS;
	if (shared && (reg < 0x11u || reg > 0x16u ||
	    (bytes == 2 && reg != 0x14u))) {
		return false;
	}
	uint64_t ticks, entered, locked;
	clock_get_uptime(&ticks); absolutetime_to_nanoseconds(ticks, &entered);
	sweet_spmi_irq_ack_lock();
	clock_get_uptime(&ticks); absolutetime_to_nanoseconds(ticks, &locked);
	if (waitNs) { *waitNs += locked - entered; }
	auto unlock = [&] {
		uint64_t done;
		clock_get_uptime(&ticks); absolutetime_to_nanoseconds(ticks, &done);
		if (holdNs) { *holdNs += done - locked; }
		sweet_spmi_irq_unlock();
	};
	if (sweet_spmi_rw_poisoned_locked(route.apid)) {
		route.writePoisoned = true;
		unlock();
		SWEET_LOG_ERROR("SWEET MUX: RW lane poisoned route=%s apid=%u; write refused\n",
		    route.name, route.apid);
		return false;
	}
	write32(route.channel, 0x10u, data);
	write32(route.channel, 0x00u, ((reg & 0xffu) << 4) | (bytes - 1u));
	for (unsigned i = 0; i < 1000; ++i) {
		uint32_t status = read32(route.channel, 0x08u);
		if (status & 1u) {
			unlock();
			if (status & 0x0eu) {
				SWEET_LOG_WARN("SWEET MUX: write error route=%s reg=%#x data=%#x status=%#x\n",
				    route.name, reg, data, status);
			}
			return (status & 0x0eu) == 0;
		}
		delay(1);
	}
	/* A timed-out RW command can complete late. Never reuse that APID,
	 * including the power/volume-down pair which shares a key lane. */
	sweet_spmi_rw_poison_locked(route.apid);
	for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
		if (routes[i].apid == route.apid) {
			routes[i].writePoisoned = true;
		}
	}
	unlock();
	SWEET_LOG_ERROR("SWEET MUX: write timeout route=%s reg=%#x; APID poisoned\n", route.name, reg);
	return false;
}

bool
SweetSpmiInterruptMux::ackRoute(MuxRoute &route, uint64_t *waitNs, uint64_t *holdNs)
{
	uint32_t mask = 1u << route.bit;
	/* A key observer read reported PMIC-arb STATUS 0x9 (DROPPED)
	 * beside a failed ack write. Both clears are idempotent. Keep
	 * the GIC masked until the worker confirms the ack; retain the
	 * IRQ shutdown after three failed attempts. */
	for (unsigned attempt = 1; attempt <= 3; ++attempt) {
		write32(route.channel, 0x108u, mask); /* v5 channel IRQ_CLEAR */
		if (writePeripheral(route, 0x14u, (uint16_t)mask, 1, waitNs, holdNs)) {
			if (attempt > 1) {
				SWEET_LOG_WARN("SWEET MUX: ack recovered route=%s attempts=%u\n",
				    route.name, attempt);
			}
			return true;
		}
		if (attempt < 3) {
			delay(20);
		}
	}
	return false;
}

uint32_t
SweetSpmiInterruptMux::allowedMaskForApid(uint16_t apid)
{
	uint32_t mask = 0;
	for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
		const MuxRoute &route = routes[i];
		if (route.active && route.apid == apid) {
			mask |= 1u << route.bit;
		}
	}
	return mask;
}

void
SweetSpmiInterruptMux::disableHardware(void)
{
	__atomic_store_n(&state, 3u, __ATOMIC_RELEASE);
	setProperty("SweetMuxLifecycle", "failed");
	if (gicd) {
		write32(gicd, 0x180u + 4u * MUX_SPI_WORD, MUX_SPI_BIT);
	}
	if (pdc) {
		write32(pdc, 0x10u, read32(pdc, 0x10u) & ~2u);
	}
	for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
		MuxRoute &route = routes[i];
		if (!route.active || !route.channel) {
			continue;
		}
		(void)writePeripheral(route, 0x16u, (uint16_t)(1u << route.bit), 1);
		write32(route.channel, 0x100u, 0);
	}
	for (unsigned i = 0; i < MUX_GROUPS; ++i) {
		if (bindings[i].notify) {
			bindings[i].notify(bindings[i].context, SWEET_MUX_FAILED);
		}
	}
}

bool
SweetSpmiInterruptMux::configureGic(void)
{
	uint32_t typer = read32(gicd, 0x004u);
	if (((typer & 0x1fu) + 1u) * 32u <= MUX_INTID) {
		return false;
	}
	write32(gicd, 0x180u + 4u * MUX_SPI_WORD, MUX_SPI_BIT);
	if (!waitGic()) {
		return false;
	}
	unsigned word = MUX_INTID / 32u;
	uint32_t group = read32(gicd, 0x80u + 4u * word) | MUX_SPI_BIT;
	write32(gicd, 0x80u + 4u * word, group);
	unsigned cfgOff = 0xc00u + 4u * (MUX_INTID / 16u);
	uint32_t cfgValue = read32(gicd, cfgOff) &
	    ~(3u << (2u * (MUX_INTID % 16u)));
	write32(gicd, cfgOff, cfgValue); /* level-high after PDC conversion */
	unsigned priOff = 0x400u + (MUX_INTID & ~3u);
	unsigned priShift = 8u * (MUX_INTID & 3u);
	uint32_t priority = read32(gicd, priOff);
	write32(gicd, priOff, (priority & ~(0xffu << priShift)) |
	    (0xa0u << priShift));
	write64(gicd, 0x6000u + 8u * MUX_INTID, 0); /* boot CPU MPIDR 0 */
	if (!waitGic()) {
		return false;
	}
	uint32_t gotGroup = read32(gicd, 0x80u + 4u * word);
	uint32_t gotCfg = read32(gicd, cfgOff);
	uint32_t gotPri = read32(gicd, priOff);
	uint64_t gotRoute = read64(gicd, 0x6000u + 8u * MUX_INTID);
	uint32_t ctlr = read32(gicd, 0x000u);
	SWEET_LOG_DEBUG("SWEET MUX: GIC intid=%u ctlr=%#x group-read=%u cfg=%u pri=%#x route=%#x:%#x\n",
	    MUX_INTID, ctlr, (gotGroup & MUX_SPI_BIT) ? 1 : 0,
	    (gotCfg >> (2u * (MUX_INTID % 16u))) & 3u,
	    (gotPri >> priShift) & 0xffu,
	    (uint32_t)(gotRoute >> 32), (uint32_t)gotRoute);
	/* Earlier non-secure IGROUPR reads were zero despite Group 1 PPI/SGI
	 * delivery. That does not establish SPI 513 ownership. The bounded
	 * software-pending probe below tests this SPI before PMIC/PDC gates. */
	return (ctlr & 2u) != 0 &&
	    ((gotCfg >> (2u * (MUX_INTID % 16u))) & 3u) == 0 &&
	    ((gotPri >> priShift) & 0xffu) == 0xa0u && gotRoute == 0;
}

/* Prove SPI 513 reaches sleh_irq without enabling the PMIC or PDC source.
 * Never infer delivery from the non-secure IGROUPR readback alone. */
bool
SweetSpmiInterruptMux::probeGicDelivery(void)
{
	unsigned off = 4u * MUX_SPI_WORD;
	uint32_t pending = read32(gicd, 0x200u + off) & MUX_SPI_BIT;
	uint32_t active = read32(gicd, 0x300u + off) & MUX_SPI_BIT;
	uint32_t pdcEnable = read32(pdc, 0x10u);
	if (pending || active || (pdcEnable & 2u)) {
		SWEET_LOG_WARN("SWEET MUX: GIC probe refused pending=%u active=%u pdc=%#x\n",
		    pending ? 1 : 0, active ? 1 : 0, pdcEnable);
		return false;
	}
	__atomic_store_n(&probeHits, 0u, __ATOMIC_RELEASE);
	__atomic_store_n(&state, 4u, __ATOMIC_RELEASE);
	write32(gicd, 0x100u + off, MUX_SPI_BIT);
	if (!waitGic() || (read32(gicd, 0x100u + off) & MUX_SPI_BIT) == 0) {
		__atomic_store_n(&state, 3u, __ATOMIC_RELEASE);
		write32(gicd, 0x180u + off, MUX_SPI_BIT);
		SWEET_LOG_ERROR("SWEET MUX: GIC probe enable/wait failed\n");
		return false;
	}
	write32(gicd, 0x200u + off, MUX_SPI_BIT);
	for (unsigned i = 0; i < 1000; ++i) {
		if (__atomic_load_n(&probeHits, __ATOMIC_ACQUIRE)) {
			break;
		}
		delay(100);
	}
	__atomic_store_n(&state, 3u, __ATOMIC_RELEASE);
	write32(gicd, 0x180u + off, MUX_SPI_BIT);
	write32(gicd, 0x280u + off, MUX_SPI_BIT);
	bool clear = waitGic();
	for (unsigned i = 0; clear && i < 1000; ++i) {
		pending = read32(gicd, 0x200u + off) & MUX_SPI_BIT;
		active = read32(gicd, 0x300u + off) & MUX_SPI_BIT;
		if (!pending && !active) {
			break;
		}
		if (i == 999) {
			clear = false;
		} else {
			delay(1);
		}
	}
	unsigned hits = __atomic_load_n(&probeHits, __ATOMIC_ACQUIRE);
	SWEET_LOG_INFO("SWEET MUX: GIC software-pending probe hits=%u clear=%u pending=%u active=%u\n",
	    hits, clear ? 1 : 0, pending ? 1 : 0, active ? 1 : 0);
	return hits == 1u && clear;
}

bool
SweetSpmiInterruptMux::configureRoute(MuxRoute &route)
{
	uint8_t type = 0, high = 0, low = 0, enabled = 0;
	if (!readPeripheral(route, 0x11u, &type) ||
	    !readPeripheral(route, 0x12u, &high) ||
	    !readPeripheral(route, 0x13u, &low) ||
	    !readPeripheral(route, 0x15u, &enabled)) {
		return false;
	}
	uint8_t mask = (uint8_t)(1u << route.bit);
	if (route.group != MUX_KEYS && (enabled & mask)) {
		SWEET_LOG_ERROR("SWEET MUX: route %s already enabled; refusing takeover\n", route.name);
		return false;
	}
	route.oldType = type;
	route.oldHigh = high;
	route.oldLow = low;
	route.touched = true;
	uint8_t wantHigh = (high & ~mask) | ((route.edges & 1u) ? mask : 0);
	uint8_t wantLow = (low & ~mask) | ((route.edges & 2u) ? mask : 0);
	if (!writePeripheral(route, 0x11u, type | mask, 1) ||
	    !writePeripheral(route, 0x12u, wantHigh, 1) ||
	    !writePeripheral(route, 0x13u, wantLow, 1) ||
	    !ackRoute(route)) {
		return false;
	}
	write32(route.channel, 0x100u, 1u); /* ACC_ENABLE */
	if (!writePeripheral(route, 0x14u,
	    (uint16_t)(mask | (uint16_t)(mask << 8)), 2)) {
		return false; /* LATCHED_CLR + EN_SET in one v5 transaction */
	}
	uint8_t gotType = 0, gotHigh = 0, gotLow = 0, gotEnable = 0;
	bool typeOk = readPeripheral(route, 0x11u, &gotType);
	bool highOk = readPeripheral(route, 0x12u, &gotHigh);
	bool lowOk = readPeripheral(route, 0x13u, &gotLow);
	bool enableOk = readPeripheral(route, 0x15u, &gotEnable);
	uint32_t access = read32(route.channel, 0x100u);
	if (!typeOk || !highOk || !lowOk || !enableOk ||
	    (gotType & mask) == 0 || (gotLow & mask) != (wantLow & mask) ||
	    (gotHigh & mask) != (wantHigh & mask) || (gotEnable & mask) == 0 ||
	    (access & 1u) == 0) {
		SWEET_LOG_ERROR("SWEET MUX: %s peripheral/channel readback failed type=%#x high=%#x low=%#x enable=%#x expected=%#x/%#x/%#x mask=%#x access=%#x reads=%u/%u/%u/%u\n",
		    route.name, gotType, gotHigh, gotLow, gotEnable, type | mask,
		    wantHigh, wantLow, mask, access, typeOk, highOk, lowOk, enableOk);
		return false;
	}
	SWEET_LOG_DEBUG("SWEET MUX: route %s sid=%u per=%#x apid=%u bit=%u cfg=%#x/%#x/%#x old-enable=%#x\n",
	    route.name, route.sid, route.per, route.apid, route.bit,
	    type, high, low, enabled);
	return true;
}

void
SweetSpmiInterruptMux::publishBatch(SweetMuxIrqTrace *batch, unsigned acknowledged)
{
	/* One bounded table pass before consumer telemetry may start. Coalesce
	 * equal Type-C tokens, retain every group and the original route order. */
	for (unsigned group = 0; group < MUX_GROUPS; ++group) {
		unsigned events = 0;
		uint64_t ticks, notifyNs;
		clock_get_uptime(&ticks); absolutetime_to_nanoseconds(ticks, &notifyNs);
		for (unsigned i = 0; i < SWEET_MUX_ROUTE_COUNT; ++i) {
			if (!(acknowledged & (1u << i)) || routes[i].group != group) { continue; }
			events |= routes[i].token;
			SweetMuxIrqTrace &t = traces[i];
			SweetMuxIrqTrace &v = batch[i];
			v.notifyNs = notifyNs;
			unsigned seq = __atomic_fetch_add(&t.sequence, 1u, __ATOMIC_ACQ_REL);
#define TRACE_STORE(field) __atomic_store_n(&t.field, v.field, __ATOMIC_RELAXED)
			TRACE_STORE(hits); TRACE_STORE(irq); TRACE_STORE(work); TRACE_STORE(status); TRACE_STORE(apid);
			TRACE_STORE(gicNs); TRACE_STORE(scanNs); TRACE_STORE(ackNs); TRACE_STORE(notifyNs);
			TRACE_STORE(ackWaitNs); TRACE_STORE(ackHoldNs);
#undef TRACE_STORE
			__atomic_store_n(&t.sequence, seq + 2u, __ATOMIC_RELEASE);
		}
		MuxBinding &binding = bindings[group];
		if (events && binding.notify) { binding.notify(binding.context, events); }
	}
}

void
SweetSpmiInterruptMux::logBatch(const SweetMuxIrqTrace *batch, unsigned acknowledged)
{
	/* Logging follows ALL bounded passes and the GIC rearm/failure decision. */
	for (unsigned i = 0; i < SWEET_MUX_ROUTE_COUNT; ++i) {
		if (!(acknowledged & (1u << i))) { continue; }
		const MuxRoute &route = routes[i];
		const SweetMuxIrqTrace &t = batch[i];
		if (route.group != MUX_KEYS || routeCount <= 32 || (routeCount & 63u) == 0) {
			if (route.group == MUX_ADC5 || (route.group == MUX_BATTERY && route.token == SWEET_BATT_IRQ_WDOG)) {
				SWEET_LOG_INFO("SWEET MUX: hit route=%s status=%#x irq=%u work=%u route-hits=%u gic=%llu scan=%llu ack=%llu notify=%llu\n",
				    route.name,t.status,t.irq,t.work,t.hits,(unsigned long long)t.gicNs,
				    (unsigned long long)t.scanNs,(unsigned long long)t.ackNs,(unsigned long long)t.notifyNs);
				SWEET_LOG_INFO("SWEET MUX ACK: v1 route=%s irq=%u work=%u wait=%llu hold=%llu\n",
				    route.name,t.irq,t.work,(unsigned long long)t.ackWaitNs,(unsigned long long)t.ackHoldNs);
			} else {
				SWEET_LOG_INFO("SWEET MUX: hit route=%s status=%#x irq=%u work=%u hits=%u route-hits=%u\n",
				    route.name,t.status,t.irq,t.work,routeCount,t.hits);
			}
		}
	}
}

void
SweetSpmiInterruptMux::drainInterrupt(void)
{
    for (unsigned pass = 0; pass < 4; ++pass) { drainMasks[pass] = 0; }
    drainPending();
    for (unsigned pass = 0; pass < 4; ++pass) { logBatch(drainTraces[pass], drainMasks[pass]); }
}

void
SweetSpmiInterruptMux::drainPending(void)
{
	++workCount;
 const uint64_t parentGicNs=__atomic_load_n(&lastGicNs,__ATOMIC_ACQUIRE);
	if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) != 2u) {
		return;
	}
	uint64_t now, ns;
	clock_get_uptime(&now);
	absolutetime_to_nanoseconds(now, &ns);
	if (!stormStartNs || ns - stormStartNs >= 1000000000ULL) {
		stormStartNs = ns;
		stormBase = irqCount;
	}
	if (irqCount - stormBase > MUX_IRQ_LIMIT) {
		SWEET_LOG_ERROR("SWEET MUX: IRQ storm count=%u/window=1s; IRQ disabled\n", irqCount - stormBase);
		disableHardware();
		return;
	}
	bool handled = false;
	bool maintenance = allowEmptyDrain;
	allowEmptyDrain = false;
	for (unsigned pass = 0; pass < 4; ++pass) {
		bool pending = false;
		unsigned acknowledged = 0;
		SweetMuxIrqTrace *batch = drainTraces[pass];
		for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
			MuxRoute &route = routes[i];
			if (!route.active) {
				continue;
			}
			uint32_t status = read32(route.channel, 0x104u);
			if (status & ~allowedMaskForApid(route.apid)) {
				publishBatch(batch, acknowledged);
				SWEET_LOG_ERROR("SWEET MUX: unregistered PMIC pending apid=%u status=%#x; IRQ disabled\n",
				    route.apid, status);
				disableHardware();
				return;
			}
			if ((status & (1u << route.bit)) == 0) {
				continue;
			}
			pending = true;
   uint64_t stamp, scanNs, ackNs, waitNs = 0, holdNs = 0;
   clock_get_uptime(&stamp); absolutetime_to_nanoseconds(stamp, &scanNs);
			if (!ackRoute(route, &waitNs, &holdNs)) {
				publishBatch(batch, acknowledged);
				SWEET_LOG_ERROR("SWEET MUX: ack failed route=%s status=%#x\n",
				    route.name, status);
				disableHardware();
				return;
			}
   clock_get_uptime(&stamp); absolutetime_to_nanoseconds(stamp, &ackNs);
			++routeCount;
			++route.hits;
   SweetMuxIrqTrace &t = batch[i]; t = {};
   t.hits = route.hits; t.irq = irqCount; t.work = workCount;
   t.status = status; t.apid = route.apid; t.gicNs = parentGicNs;
   t.scanNs = scanNs; t.ackNs = ackNs; t.ackWaitNs = waitNs; t.ackHoldNs = holdNs;
   acknowledged |= 1u << i; drainMasks[pass] = acknowledged;
			handled = true;
		}
		publishBatch(batch, acknowledged);
		if (!pending) {
			break;
		}
	}
	unsigned everAcked = 0;
	for (unsigned pass = 0; pass < 4; ++pass) {
		everAcked |= drainMasks[pass];
	}
	unsigned pendingRoutes = 0;
	for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
		const MuxRoute &route = routes[i];
		if (route.active &&
		    (read32(route.channel, 0x104u) & (1u << route.bit))) {
			pendingRoutes |= 1u << i;
		}
	}
	/* An ACKed route may have reasserted; an unACKed route may have arrived
	 * after its last scan. Neither proves a stuck source. Rearm the level
	 * IRQ below and let the next drain handle either case. ACK failures
	 * already return through disableHardware() above. */
	const bool reassertedPending = (pendingRoutes & everAcked) != 0;
	const bool newlyPending = (pendingRoutes & ~everAcked) != 0;
	const bool stillPending = reassertedPending || newlyPending;
	if (!handled && !maintenance && !stillPending) {
		++faultCount;
		unsigned pendingGroups = 0;
		for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
			const MuxRoute &route = routes[i];
			if (!route.active) {
				continue;
			}
			uint32_t status = read32(route.channel, 0x104u);
			if (status & (1u << route.bit)) {
				pendingGroups |= 1u << route.group;
				SWEET_LOG_ERROR("SWEET MUX: pending route=%s group=%u apid=%u bit=%u status=%#x\n",
				    route.name, route.group, route.apid, route.bit, status);
			}
		}
		SWEET_LOG_ERROR("SWEET MUX: unhandled/stuck irq handled=%u pending=%u groups=%#x faults=%u\n",
		    handled ? 1 : 0, stillPending ? 1 : 0, pendingGroups, faultCount);
		if (pendingGroups && quarantinePendingGroups(pendingGroups)) {
			/* Keep healthy groups alive; only the pending group is revoked. */
			__atomic_store_n(&state, 1u, __ATOMIC_RELEASE);
			for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
				if (routes[i].active) {
					write32(gicd, 0x100u + 4u * MUX_SPI_WORD, MUX_SPI_BIT);
					break;
				}
			}
			SWEET_LOG_WARN("SWEET MUX: isolated pending groups=%#x; other groups remain available\n",
			    pendingGroups);
			return;
		}
		SWEET_LOG_ERROR("SWEET MUX: pending group isolation failed; IRQ disabled\n");
		disableHardware();
		return;
	}
	__atomic_store_n(&state, 1u, __ATOMIC_RELEASE);
	for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
		if (routes[i].active) {
			write32(gicd, 0x100u + 4u * MUX_SPI_WORD, MUX_SPI_BIT);
			break;
		}
	}
}

void
SweetSpmiInterruptMux::workAction(OSObject *owner, IOInterruptEventSource *source, int count)
{
	(void)source;
	(void)count;
	SweetSpmiInterruptMux *self = OSDynamicCast(SweetSpmiInterruptMux, owner);
	if (self && !self->stopping) {
		self->drainInterrupt();
	}
}

bool
SweetSpmiInterruptMux::copyInterruptTrace(unsigned group, unsigned token, SweetMuxIrqTrace *out) const
{
 if (!out || !token || (token & (token - 1u))) return false;
 SweetMuxIrqTrace best = {};
 /* Route metadata is immutable after init; trace fields are all atomic. There
  * is no gate acquisition, SPMI, MMIO or unbounded seqlock spin here. */
 for (unsigned i=0; i<SWEET_MUX_ROUTE_COUNT; ++i) {
  if (routes[i].group!=group || routes[i].token!=token) continue;
  const SweetMuxIrqTrace &t=traces[i];
  for (unsigned attempt=0; attempt<3; ++attempt) {
   unsigned seq=__atomic_load_n(&t.sequence,__ATOMIC_ACQUIRE);
   if (!seq || (seq & 1u)) continue;
   SweetMuxIrqTrace next={}; next.sequence=seq;
#define TRACE_LOAD(field) next.field=__atomic_load_n(&t.field,__ATOMIC_RELAXED)
   TRACE_LOAD(hits); TRACE_LOAD(irq); TRACE_LOAD(work); TRACE_LOAD(status); TRACE_LOAD(apid);
   TRACE_LOAD(gicNs); TRACE_LOAD(scanNs); TRACE_LOAD(ackNs); TRACE_LOAD(notifyNs);
   TRACE_LOAD(ackWaitNs); TRACE_LOAD(ackHoldNs);
#undef TRACE_LOAD
   __atomic_thread_fence(__ATOMIC_ACQUIRE);
   if (seq!=__atomic_load_n(&t.sequence,__ATOMIC_ACQUIRE)) continue;
   if (!best.sequence || next.notifyNs>=best.notifyNs) best=next;
   break;
  }
 }
 if (!best.sequence) return false;
 *out=best; return true;
}

bool
SweetSpmiInterruptMux::irq(unsigned int intid)
{
	if (intid != MUX_INTID) {
		return false;
	}
	if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) == 4u) {
		write32(gicd, 0x180u + 4u * MUX_SPI_WORD, MUX_SPI_BIT);
		__atomic_add_fetch(&probeHits, 1u, __ATOMIC_RELEASE);
		return true;
	}
	unsigned expected = 1u;
	if (!__atomic_compare_exchange_n(&state, &expected, 2u, false,
	    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		return expected == 2u || expected == 3u || expected == 5u;
	}
	write32(gicd, 0x180u + 4u * MUX_SPI_WORD, MUX_SPI_BIT);
 uint64_t ticks, ns;
 clock_get_uptime(&ticks); absolutetime_to_nanoseconds(ticks, &ns);
 __atomic_store_n(&lastGicNs, ns, __ATOMIC_RELEASE);
	++irqCount;
	eventSource->interruptOccurred(NULL, NULL, 0);
	return true;
}

int
SweetSpmiInterruptMux::initializeHardware(void)
{
	SWEET_LOG_INFO("SWEET MUX ARBITRATION: v1 ACK priority, bounded notify batches, split diagnostics\n");
	if (sweet_spmi_init() != 0) {
		SWEET_LOG_ERROR("SWEET MUX: shared SPMI transport unavailable\n");
		return -1;
	}
	SWEET_LOG_INFO("SWEET MUX: controller transactions serialized across keys and battery\n");
	workLoop = IOWorkLoop::workLoop();
	if (!workLoop) {
		return -1;
	}
	eventSource = IOInterruptEventSource::interruptEventSource(this,
	    workAction, NULL, 0);
	if (!eventSource || workLoop->addEventSource(eventSource) != kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET MUX: software event source unavailable\n");
		return -1;
	}
	eventAdded = true;
	gicd = mapMMIO(MUX_GICD_PA, 0x10000);
	pdc = mapMMIO(MUX_PDC_PA, 0x400);
	core = mapMMIO(SWEET_DEV_SPMI_CORE_PA, 0x2000);
	cfg = mapMMIO(SWEET_DEV_SPMI_CFG_PA, 0x1000);
	obs = mapMMIO(SWEET_DEV_SPMI_OBS_PA, 0x10000);
	if (!gicd || !pdc || !core || !cfg || !obs) {
		SWEET_LOG_ERROR("SWEET MUX: upstream or PMIC map failed; IRQ unavailable\n");
		return -1;
	}
	uint32_t version = read32(core, 0);
	unsigned count = read32(core, 4) & 0x7ffu;
	if (version < 0x50000000u || version >= 0x60000000u ||
	    count == 0 || count > 512) {
		SWEET_LOG_ERROR("SWEET MUX: unsupported arb=%#x count=%u\n", version, count);
		return -1;
	}
	apidCount = count;
	if ((read32(pdc, 0x114u) & 7u) != 4u) {
		SWEET_LOG_ERROR("SWEET MUX: PDC polarity invalid; IRQ unavailable\n");
		return -1;
	}
	hardwareTouched = true;
	if (!configureGic() || !probeGicDelivery()) {
		SWEET_LOG_ERROR("SWEET MUX: PDC polarity, GIC config or delivery probe failed; IRQ unavailable\n");
		return -1;
	}
	/* Idle but ready: GIC remains masked and PDC stays closed until attach.
	 * Thus disabled/failed keys never leave unconsumed key routes enabled. */
	__atomic_store_n(&state, 1u, __ATOMIC_RELEASE);
	SWEET_LOG_INFO("SWEET MUX: infrastructure ready intid=%u; waiting for consumers\n", MUX_INTID);
	return 0;
}
bool
SweetSpmiInterruptMux::beginRouteUpdate(void)
{
	write32(gicd, 0x180u + 4u * MUX_SPI_WORD, MUX_SPI_BIT);
	unsigned expected = 1u;
	if (__atomic_compare_exchange_n(&state, &expected, 5u, false,
	    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		return true;
	}
	if (expected == 2u) {
		allowEmptyDrain = true;
		return true; /* queued IRQ worker still owns the rearm */
	}
	return false;
}

bool
SweetSpmiInterruptMux::endRouteUpdate(void)
{
	unsigned expected = 5u;
	if (__atomic_compare_exchange_n(&state, &expected, 1u, false,
	    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		bool active = false;
		for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
			active |= routes[i].active;
		}
		if (!active) {
			/* Last consumer detached. Keep the upstream SPI masked. */
			return true;
		}
		write32(pdc, 0x10u, read32(pdc, 0x10u) | 2u);
		if ((read32(pdc, 0x10u) & 2u) == 0) {
			SWEET_LOG_ERROR("SWEET MUX: PDC pin 1 enable failed; IRQ disabled\n");
			disableHardware();
			return false;
		}
		write32(gicd, 0x100u + 4u * MUX_SPI_WORD, MUX_SPI_BIT);
		uint32_t enabled = read32(gicd, 0x100u + 4u * MUX_SPI_WORD) & MUX_SPI_BIT;
		unsigned current = __atomic_load_n(&state, __ATOMIC_ACQUIRE);
		/* Immediate IRQ masking is a successful enable, as in the old path. */
		if ((!enabled && current == 1u) || current == 3u) {
			SWEET_LOG_ERROR("SWEET MUX: GIC SPI 513 enable failed; IRQ disabled\n");
			disableHardware();
			return false;
		}
	}
	return __atomic_load_n(&state, __ATOMIC_ACQUIRE) != 3u;
}

bool
SweetSpmiInterruptMux::removeGroup(unsigned group)
{
	bool ok = true;
	/* Clear active first so shared-APID access restoration sees the complete
	 * group removed. Only our route bits are disabled/restored. */
	for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
		MuxRoute &route = routes[i];
		if (route.group == group) {
			route.active = false;
		}
	}
	for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
		MuxRoute &route = routes[i];
		if (route.group != group || !route.touched) {
			continue;
		}
		uint8_t mask = (uint8_t)(1u << route.bit);
		bool cleared = writePeripheral(route, 0x16u, mask, 1) && ackRoute(route);
		uint8_t type = 0, high = 0, low = 0;
		bool restored = cleared && readPeripheral(route, 0x11u, &type) &&
		    readPeripheral(route, 0x12u, &high) && readPeripheral(route, 0x13u, &low) &&
		    writePeripheral(route, 0x11u, (type & ~mask) | (route.oldType & mask), 1) &&
		    writePeripheral(route, 0x12u, (high & ~mask) | (route.oldHigh & mask), 1) &&
		    writePeripheral(route, 0x13u, (low & ~mask) | (route.oldLow & mask), 1);
		if (!allowedMaskForApid(route.apid)) {
			write32(route.channel, 0x100u, route.oldAccess);
		}
		ok = restored && ok;
		route.touched = false;
	}
	return ok;
}

/* A failed PMIC restore need not take unrelated APIDs offline. Keep GIC
 * masked (beginRouteUpdate) while closing each affected local arbiter gate.
 * No SPMI command is issued here: a poisoned RW lane must never be reused.
 * Cross-group APIDs cannot be gated independently, so fail closed in that case.
 * Successful isolation is terminal for this group for the rest of this boot. */
bool
SweetSpmiInterruptMux::isolateGroup(unsigned group)
{
	bool isolated = true;
	for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
		MuxRoute &route = routes[i];
		if (route.group != group || !route.channel) {
			continue;
		}
		bool seen = false;
		for (unsigned j = 0; j < i; ++j) {
			seen |= routes[j].group == group && routes[j].channel &&
			    routes[j].apid == route.apid;
		}
		if (seen) {
			continue;
		}
		if (allowedMaskForApid(route.apid)) {
			SWEET_LOG_ERROR("SWEET MUX: cannot isolate group=%u shared active APID=%u\n",
			    group, route.apid);
			isolated = false;
			continue;
		}
		uint32_t mask = 0;
		for (unsigned j = 0; j < sizeof(routes) / sizeof(routes[0]); ++j) {
			if (routes[j].group == group && routes[j].apid == route.apid) {
				mask |= 1u << routes[j].bit;
			}
		}
		write32(route.channel, 0x100u, 0); /* ACC_ENABLE: close this APID */
		write32(route.channel, 0x108u, mask); /* clear only this group's bits */
		uint32_t access = read32(route.channel, 0x100u);
		uint32_t pending = read32(route.channel, 0x104u);
		if (access != 0 || pending != 0) {
			SWEET_LOG_ERROR("SWEET MUX: isolation readback failed group=%u apid=%u access=%#x pending=%#x\n",
			    group, route.apid, access, pending);
			isolated = false;
		}
	}
	if (isolated) {
		isolatedGroups |= 1u << group;
		setProperty("SweetMuxIsolatedGroups", (uint64_t)isolatedGroups, 32);
		setProperty("SweetMuxLifecycle", "degraded");
		SWEET_LOG_WARN("SWEET MUX: isolated group=%u; other groups remain available\n", group);
	}
	return isolated;
}

bool
SweetSpmiInterruptMux::quarantinePendingGroups(unsigned groups)
{
	/* drainInterrupt runs on the mux workloop with the GIC masked. Mark the
	 * affected groups inactive before isolateGroup(), so a private APID can be
	 * closed without taking unrelated consumers offline. If an APID is shared
	 * with another still-active group, isolation is unsafe and the caller must
	 * use the fail-closed whole-mux path. */
	bool ok = true;
	for (unsigned group = 0; group < MUX_GROUPS; ++group) {
		if (!(groups & (1u << group))) {
			continue;
		}
		for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
			if (routes[i].group == group) {
				routes[i].active = false;
				/* isolateGroup already closed this route; later consumer detach
				 * must not issue a second PMIC restore transaction. */
				routes[i].touched = false;
			}
		}
		if (!isolateGroup(group)) {
			ok = false;
		}
	}
	if (!ok) {
		return false;
	}
	for (unsigned group = 0; group < MUX_GROUPS; ++group) {
		if ((groups & (1u << group)) && bindings[group].notify) {
			bindings[group].notify(bindings[group].context, SWEET_MUX_FAILED);
		}
	}
	return true;
}

IOReturn
SweetSpmiInterruptMux::updateBinding(BindingRequest *request, bool detach)
{
	unsigned group = request->group;
	MuxBinding &binding = bindings[group];
	if (detach) {
		if (binding.consumer != request->consumer || binding.context != request->context || !binding.notify) {
			return kIOReturnSuccess;
		}
		bool running = beginRouteUpdate();
		if (!removeGroup(group)) {
			SWEET_LOG_ERROR("SWEET MUX: detach cleanup failed group=%u; attempting group isolation\n", group);
			if (!isolateGroup(group)) {
				SWEET_LOG_ERROR("SWEET MUX: detach isolation failed; shared IRQ stays off\n");
				disableHardware();
			}
		}
		binding.consumer = NULL;
		binding.notify = NULL;
		binding.context = NULL;
		binding.events = 0;
		if (running) {
			endRouteUpdate();
		}
		return kIOReturnSuccess;
	}
	unsigned current = __atomic_load_n(&state, __ATOMIC_ACQUIRE);
	if (stopping || isInactive() || request->consumer->isInactive() || binding.notify ||
	    (isolatedGroups & (1u << group)) || (current != 1u && current != 2u)) {
		return kIOReturnNotReady;
	}
	/* Resolve only unique EE0 IRQ-owned channels before reading channel MMIO. */
	unsigned found = 0;
	for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
		MuxRoute &route = routes[i];
		if (route.group != group || !(request->events & route.token)) {
			continue;
		}
		found |= route.token;
		uint16_t resolved = findOwnedApid(route.sid, route.per, apidCount);
		if (resolved == MUX_NO_APID || (route.channel && route.apid != resolved)) {
			SWEET_LOG_ERROR("SWEET MUX: %s has no unique EE0 IRQ-owned APID\n", route.name);
			return kIOReturnNotPermitted;
		}
		/* An isolated APID must not be reopened through another group's route,
		 * even if that group was inactive when isolation completed. */
		for (unsigned j = 0; j < sizeof(routes) / sizeof(routes[0]); ++j) {
			if ((isolatedGroups & (1u << routes[j].group)) && routes[j].apid == resolved) {
				SWEET_LOG_ERROR("SWEET MUX: attach refused group=%u quarantined APID=%u\n",
				    group, resolved);
				return kIOReturnNotReady;
			}
		}
		route.apid = resolved;
		if (!route.channel) {
			route.channel = mapMMIO(SWEET_DEV_SPMI_CHNLS_PA + 0x10000u * route.apid, 0x1000);
		}
		if (!route.channel || route.writePoisoned) {
			return kIOReturnNotReady;
		}
		route.oldAccess = read32(route.channel, 0x100u);
		SWEET_LOG_INFO("SWEET MUX: attach route=%s apid=%u acc=%#x status=%#x\n",
		    route.name, route.apid, route.oldAccess, read32(route.channel, 0x104u));
	}
	if (found != request->events || !beginRouteUpdate()) {
		return kIOReturnNotReady;
	}
	binding.consumer = request->consumer;
	binding.notify = request->notify;
	binding.context = request->context;
	binding.events = request->events;
	for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
		MuxRoute &route = routes[i];
		if (route.group != group || !(found & route.token)) {
			continue;
		}
		route.active = true;
		if (!configureRoute(route)) {
			SWEET_LOG_ERROR("SWEET MUX: attach setup failed route=%s; consumer IRQ unavailable\n", route.name);
			if (!removeGroup(group)) {
				SWEET_LOG_ERROR("SWEET MUX: attach rollback failed group=%u; attempting group isolation\n", group);
				if (!isolateGroup(group)) {
					SWEET_LOG_ERROR("SWEET MUX: attach isolation failed; shared IRQ stays off\n");
					disableHardware();
				}
			}
			binding.consumer = NULL;
			binding.notify = NULL;
			binding.context = NULL;
			binding.events = 0;
			endRouteUpdate();
			return kIOReturnIOError;
		}
	}
	SWEET_LOG_INFO("SWEET MUX: consumer attached group=%u events=%#x intid=%u\n",
	    group, found, MUX_INTID);
	if (!endRouteUpdate()) {
		/* Attach failed after installing the callback. A failed caller does
		 * not own a binding: erase it before returning, even after fatal IRQ
		 * gating errors, so stop/free cannot leave a dangling context. */
		(void)removeGroup(group);
		binding.consumer = NULL;
		binding.notify = NULL;
		binding.context = NULL;
		binding.events = 0;
		return kIOReturnIOError;
	}
	return kIOReturnSuccess;
}

IOReturn
SweetSpmiInterruptMux::attachConsumer(IOService *consumer, unsigned group, unsigned events, SweetMuxNotify notify,
    void *context)
{
	static const unsigned groupMasks[] = {SWEET_KEYS_IRQ_ALL, SWEET_BATT_IRQ_ALL, SWEET_ADC5_IRQ_EOC};
	static_assert(sizeof(groupMasks) / sizeof(groupMasks[0]) == MUX_GROUPS, "group masks");
	if (!consumer || group >= MUX_GROUPS || !events || !notify || !context ||
	    (events & ~groupMasks[group]) || (group == MUX_KEYS && events != SWEET_KEYS_IRQ_ALL)) {
		return kIOReturnBadArgument;
	}
	if (!workLoop) {
		return kIOReturnNotReady;
	}
	BindingRequest request = {group, events, consumer, notify, context};
	return workLoop->runAction(bindingAction, this, &request);
}

void
SweetSpmiInterruptMux::detachConsumer(IOService *consumer, unsigned group, void *context)
{
	/* A runtime fault reports FAILED, but an attached context still must
	 * be removed under the existing gate before its owner is released. */
	if (group >= MUX_GROUPS || !workLoop) {
		return;
	}
	BindingRequest request = {group, 0, consumer, NULL, context};
	(void)workLoop->runAction(bindingAction, this, &request, context);
}

static IOSimpleLock *
irqDispatchLock(void)
{
	IOSimpleLock *lock = __atomic_load_n(&irqLock, __ATOMIC_ACQUIRE);
	if (!lock) {
		IOSimpleLock *candidate = IOSimpleLockAlloc();
		if (!candidate) {
			return NULL;
		}
		if (__atomic_compare_exchange_n(&irqLock, &lock, candidate, false,
		    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			SWEET_LOG_INFO("SWEET SPMI: irq lock %p\n", candidate);
			lock = candidate;
		} else {
			IOSimpleLockFree(candidate);
		}
	}
	return lock;
}

bool
SweetSpmiInterruptMux::init(OSDictionary *dictionary)
{
	if (!IOService::init(dictionary)) {
		return false;
	}
	memcpy(routes, routeTemplates, sizeof(routes));
	return true;
}

bool
SweetSpmiInterruptMux::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	superStarted = true;
	IOSimpleLock *dispatch = irqDispatchLock();
	if (!dispatch) {
		IOService::stop(provider);
		superStarted = false;
		return false;
	}
	setProperty("SweetMuxLifecycle", "starting");
	setProperty("SweetMuxIntID", MUX_INTID, 32);
	setProperty("SweetMuxHardwareProvider", false);
	IOInterruptState saved = IOSimpleLockLockDisableInterrupt(dispatch);
	if (bootClaimed) {
		IOSimpleLockUnlockEnableInterrupt(dispatch, saved);
		IOService::stop(provider);
		superStarted = false;
		return false;
	}
	irqService = this; /* private to physical interrupt dispatch */
	claimed = true;
	bootClaimed = true;
	IOSimpleLockUnlockEnableInterrupt(dispatch, saved);
	if (initializeHardware() != 0) {
		shutdown();
		IOService::stop(provider);
		superStarted = false;
		return false;
	}
	setProperty("SweetMuxLifecycle", "ready");
	provider->setProperty("SweetMuxReady", true);
	registerService();
	SWEET_LOG_INFO("SWEET MUX: IOService started; owns workloop, routes and MMIO\n");
	return true;
}

IOReturn
SweetSpmiInterruptMux::bindingAction(OSObject *owner, void *arg0, void *arg1,
    void *, void *)
{
	SweetSpmiInterruptMux *self = OSDynamicCast(SweetSpmiInterruptMux, owner);
	return self ? self->updateBinding((BindingRequest *)arg0, arg1 != NULL) : kIOReturnBadArgument;
}

IOReturn
SweetSpmiInterruptMux::stopAction(OSObject *owner, void *, void *, void *, void *)
{
	SweetSpmiInterruptMux *self = OSDynamicCast(SweetSpmiInterruptMux, owner);
	if (!self) {
		return kIOReturnBadArgument;
	}
	self->stopping = true;
	self->disconnectIRQ();
	/* Registry watchers are not IOService children. Their queued detach may
	 * follow willTerminate/stop, so diagnose and revoke instead of asserting
	 * an empty table here. Workloop/MMIO live until the last retain is gone. */
	for (unsigned i = 0; i < MUX_GROUPS; ++i) {
		if (self->bindings[i].consumer || self->bindings[i].notify || self->bindings[i].context) {
			SWEET_LOG_WARN("SWEET MUX: shutdown revoking attached consumer group=%u events=%#x\n",
			    i, self->bindings[i].events);
		}
	}
	if (self->hardwareTouched) {
		self->disableHardware();
	}
	for (unsigned i = 0; i < MUX_GROUPS; ++i) {
		if (!self->removeGroup(i)) {
			SWEET_LOG_ERROR("SWEET MUX: IOService stop route cleanup failed group=%u; IRQ stays off\n", i);
		}
		self->bindings[i].consumer = NULL;
		self->bindings[i].notify = NULL;
		self->bindings[i].context = NULL;
		self->bindings[i].events = 0;
	}
	if (self->eventSource) {
		self->eventSource->disable();
		if (self->eventAdded) {
			self->workLoop->removeEventSource(self->eventSource);
			self->eventAdded = false;
		}
	}
	self->setProperty("SweetMuxLifecycle", "stopped");
	return kIOReturnSuccess;
}

/* Called under the mux gate (or before any workloop exists). Taking the
 * gate first prevents an in-flight attach/drain from re-enabling GIC after
 * the IRQ pointer has been disconnected. The top half never takes the gate. */
void
SweetSpmiInterruptMux::disconnectIRQ(void)
{
	IOSimpleLock *dispatch = __atomic_load_n(&irqLock, __ATOMIC_ACQUIRE);
	IOInterruptState saved = IOSimpleLockLockDisableInterrupt(dispatch);
	if (gicd && hardwareTouched) {
		write32(gicd, 0x180u + 4u * MUX_SPI_WORD, MUX_SPI_BIT);
	}
	if (irqService == this) {
		irqService = NULL;
	}
	IOSimpleLockUnlockEnableInterrupt(dispatch, saved);
}

void
SweetSpmiInterruptMux::shutdown(void)
{
	if (quiesced) {
		return;
	}
	quiesced = true;
	if (claimed) {
		if (workLoop) {
			(void)workLoop->runAction(stopAction, this);
		} else {
			disconnectIRQ();
		}
		if (getProvider()) {
			getProvider()->setProperty("SweetMuxReady", false);
		}
		claimed = false;
	}
}

bool
SweetSpmiInterruptMux::willTerminate(IOService *provider, IOOptionBits options)
{
	shutdown();
	return IOService::willTerminate(provider, options);
}

void
SweetSpmiInterruptMux::stop(IOService *provider)
{
	shutdown();
	if (superStarted) {
		IOService::stop(provider);
		superStarted = false;
	}
	SWEET_LOG_INFO("SWEET MUX: IOService stopped; IRQ and callbacks disconnected\n");
}

void
SweetSpmiInterruptMux::releaseResources(void)
{
	if (eventSource) {
		eventSource->release();
		eventSource = NULL;
	}
	if (workLoop) {
		workLoop->release();
		workLoop = NULL;
	}
	for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
		if (routes[i].channel) {
			unmapMMIO(routes[i].channel, 0x1000);
			routes[i].channel = 0;
		}
	}
	if (obs) { unmapMMIO(obs, 0x10000); obs = 0; }
	if (cfg) { unmapMMIO(cfg, 0x1000); cfg = 0; }
	if (core) { unmapMMIO(core, 0x2000); core = 0; }
	if (pdc) { unmapMMIO(pdc, 0x400); pdc = 0; }
	if (gicd) { unmapMMIO(gicd, 0x10000); gicd = 0; }
}

void
SweetSpmiInterruptMux::free(void)
{
	shutdown();
	releaseResources();
	IOService::free();
}

extern "C" bool
sweet_spmi_mux_irq(unsigned int intid)
{
	if (intid != MUX_INTID) {
		return false;
	}
	IOSimpleLock *lock = __atomic_load_n(&irqLock, __ATOMIC_ACQUIRE);
	if (lock) {
		IOInterruptState saved = IOSimpleLockLockDisableInterrupt(lock);
		if (irqService) {
			(void)irqService->irq(intid);
		}
		IOSimpleLockUnlockEnableInterrupt(lock, saved);
	}
	return true; /* reserved SPI: stopped/unpublished delivery only needs EOI */
}

extern "C" int
sweet_spmi_mux_start(void)
{
	/* Board publication only. Consumers discover the service via IOKit;
	 * this one-shot scalar is not a provider lookup or readiness rendezvous. */
	static bool requested;
	if (requested) {
		return 0;
	}
	requested = true;
	IOPlatformExpert *platform = IOService::getPlatform();
	if (!platform) {
		return -1;
	}
	SweetSpmiMuxDevice *nub = new SweetSpmiMuxDevice;
	if (!nub) {
		return -1;
	}
	if (!nub->init() || !nub->attach(platform)) {
		nub->release();
		return -1;
	}
	nub->setName("mh2lm-spmi-mux");
	nub->registerService(kIOServiceSynchronous);
	bool ready = nub->getProperty("SweetMuxReady") == kOSBooleanTrue;
	if (!ready) {
		SWEET_LOG_ERROR("SWEET MUX: IOService matching/start failed; no polling fallback\n");
		(void)nub->terminate(kIOServiceSynchronous);
	}
	nub->release();
	return ready ? 0 : -1;
}

#else
extern "C" int sweet_spmi_mux_start(void) { return -1; }
#endif /* ARM64_BOARD_CONFIG_MH2LM */
