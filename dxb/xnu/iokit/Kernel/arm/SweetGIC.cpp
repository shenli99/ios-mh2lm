/*
 * SweetGIC: the GICv3 distributor owner for the sweet platform.
 *
 * The problem this replaces: sleh_irq() acknowledges the interrupt and then
 * offers the INTID to every device driver in turn, and each driver maps GICD
 * itself to mask, configure and route its own SPI. Here one INTID maps to one
 * registered source, the controller owns the distributor, and drivers only
 * register/enable/disable their source.
 *
 * Scope is SPIs only (32..1019). The first 32 INTIDs are per-CPU: the vtimer
 * PPI and the IPI SGI are managed by the platform expert and sleh_irq, and a
 * controller that owned them would need a per-CPU table for something nothing
 * asks for yet. Registration of INTID < 32 is refused, and handleInterrupt()
 * leaves SGIs/PPIs alone (returning false) so the caller keeps ownership.
 *
 * EOI is written exactly once per interrupt, and this controller writes it
 * only for the INTIDs it completed. sleh_irq() therefore must not write
 * ICC_EOIR1_EL1 for an interrupt that sweet_gic_handle_irq() reported as
 * completed; it still owns the legacy paths (timer, IPIs, drivers that have
 * not migrated yet).
 *
 * Rollout is staged: the legacy predicate chain runs first in sleh_irq() and
 * only unclaimed INTIDs reach this controller. While that is the case the
 * stray policy is "count and complete", not "mask", so an SPI nobody has
 * registered yet behaves exactly as it did before (level storm included);
 * mask the strays once every live SPI is registered.
 */

#include "SweetGIC.h"

#include <IOKit/IOLib.h>
#include <stdint.h>

#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"

#if defined(SWEET)

/* One table entry per SPI; INTID 0..31 have no entry at all. */
#define SWEET_GIC_VECTORS \
	((uint32_t)(GIC_INTID_SPI_LAST - GIC_INTID_SPI_FIRST) + 1u)

OSDefineMetaClassAndStructors(SweetGIC, OSObject);

static SweetGIC *sweetGIC;

/* ---------------------------------------------------------------- MMIO */

uint32_t
SweetGIC::gicdRead32(uint32_t offset) const
{
	return *(volatile uint32_t *)(fGICD + offset);
}

void
SweetGIC::gicdWrite32(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(fGICD + offset) = value;
}

void
SweetGIC::gicdWrite64(uint32_t offset, uint64_t value)
{
	*(volatile uint64_t *)(fGICD + offset) = value;
}

/*
 * Make the programmed configuration visible before the enable bit is set,
 * same reason as the DSB ISH in IOInterruptController::enableInterrupt: a DMB
 * is not enough, the distributor is a separate observer.
 */
void
SweetGIC::synchronizeIO(void)
{
	__asm__ volatile ("dsb ish" ::: "memory");
}

/*
 * GICD_CTLR.RWP is polled exactly once: right after the CTLR write in
 * start(), where the spec wants the change acknowledged before anything else
 * is configured. Nothing else waits on it. It is a distributor-wide stall,
 * and the enable/disable writes that drivers issue from interrupt context
 * must not spin on it -- that would block the calling core, and XNU's own GIC
 * code does not poll RWP for those writes either.
 */
bool
SweetGIC::waitForRWP(void)
{
	for (unsigned int i = 0; i < 1000000u; i++) {
		if (!(gicdRead32(GICD_CTLR) & GICD_CTLR_RWP)) {
			return true;
		}
		__asm__ volatile ("yield" ::: "memory");
	}

	return false;
}

/* ------------------------------------------------------- table lookup */

IOInterruptVector *
SweetGIC::vectorAt(int intid) const
{
	if (intid < GIC_INTID_SPI_FIRST || intid > fMaxIntID) {
		return NULL;
	}

	return &fVectors[(uint32_t)intid - GIC_INTID_SPI_FIRST];
}

SweetGICSourceExtra *
SweetGIC::extraAt(int intid) const
{
	if (intid < GIC_INTID_SPI_FIRST || intid > fMaxIntID) {
		return NULL;
	}

	return &fExtras[(uint32_t)intid - GIC_INTID_SPI_FIRST];
}

/* ------------------------------------------------------- configuration */

void
SweetGIC::writeGroup(int intid, bool group1)
{
	uint32_t offset = GICD_IGROUPR + 4u * ((uint32_t)intid / 32);
	uint32_t bit = 1u << ((uint32_t)intid % 32);
	uint32_t value = gicdRead32(offset);

	if (group1) {
		value |= bit;
	} else {
		value &= ~bit;
	}

	gicdWrite32(offset, value);
}

void
SweetGIC::writePriority(int intid, uint8_t priority)
{
	uint32_t offset = GICD_IPRIORITYR + 4u * ((uint32_t)intid / 4);
	uint32_t shift = 8u * ((uint32_t)intid % 4);
	uint32_t value = gicdRead32(offset);

	value &= ~(0xffu << shift);
	value |= (uint32_t)priority << shift;

	gicdWrite32(offset, value);
}

void
SweetGIC::writeTrigger(int intid, bool level)
{
	uint32_t offset = GICD_ICFGR + 4u * ((uint32_t)intid / 16);
	uint32_t shift = 2u * ((uint32_t)intid % 16);
	uint32_t field = level ? GICD_ICFGR_LEVEL : GICD_ICFGR_EDGE;
	uint32_t value = gicdRead32(offset);

	value &= ~(3u << shift);
	value |= field << shift;

	gicdWrite32(offset, value);
}

bool
SweetGIC::triggerIsLevel(int intid) const
{
	uint32_t offset = GICD_ICFGR + 4u * ((uint32_t)intid / 16);
	uint32_t shift = 2u * ((uint32_t)intid % 16);

	return ((gicdRead32(offset) >> shift) & 2u) == 0;
}

/*
 * SPIs only, and 0 is the default: every driver on this platform programs
 * IROUTER = 0 today, so an unconfigured route keeps the current behaviour.
 * Routing to another PE comes with the per-CPU work, if it is ever needed.
 */
void
SweetGIC::writeRoute(int intid, uint64_t affinity)
{
	gicdWrite64(GICD_IROUTER + 8u * (uint32_t)intid,
	    affinity & 0xffffffffffULL);
}

void
SweetGIC::maskSPI(int intid)
{
	gicdWrite32(GICD_ICENABLER + 4u * ((uint32_t)intid / 32),
	    1u << ((uint32_t)intid % 32));
}

void
SweetGIC::unmaskSPI(int intid)
{
	gicdWrite32(GICD_ISENABLER + 4u * ((uint32_t)intid / 32),
	    1u << ((uint32_t)intid % 32));
}

/*
 * An SPI nobody registered. Counted, completed by the caller of dispatch(),
 * and masked only when the stray policy says so: during the staged rollout
 * the legacy chain still owns most INTIDs, so masking here would silently
 * kill a device that simply has not migrated yet.
 */
void
SweetGIC::straySPI(int intid)
{
	SweetGICSourceExtra *extra = extraAt(intid);
	uint32_t seen;

	if (!extra) {
		return;
	}

	seen = (uint32_t)__atomic_add_fetch(&extra->stray, 1u, __ATOMIC_RELAXED);

	/*
	 * One line per stray INTID, then every 1024th: this runs in interrupt
	 * context and a console write costs milliseconds.
	 */
	if (seen == 1u || (seen % 1024u) == 0u) {
		SWEET_LOG_ERROR("SWEET GIC: intid %d has no handler (%u)%s\n",
		    intid, seen, fMaskStraySPIs ? "; masked" : "");
	}

	if (fMaskStraySPIs) {
		maskSPI(intid);
	}
}

/* --------------------------------------------------------- registration */

IOReturn
SweetGIC::registerInterrupt(IOService *nub, int source, void *target,
    IOInterruptHandler handler, void *refCon, IOOptionBits options)
{
	uint32_t opts = (uint32_t)options;
	uint8_t priority = (uint8_t)SWEET_GIC_OPT_PRIORITY_OF(opts);
	bool level = (opts & SWEET_GIC_OPT_LEVEL) != 0;
	IOInterruptState interruptState;
	SweetGICSourceExtra *extra;
	IOInterruptVector *vector;

	if (!fReady) {
		return kIOReturnNotReady;
	}
	if (!handler) {
		return kIOReturnBadArgument;
	}
	if (source < GIC_INTID_SPI_FIRST || source > GIC_INTID_SPI_LAST) {
		/* SGIs and PPIs are per-CPU platform state, not ours. */
		return kIOReturnUnsupported;
	}
	if (source > fMaxIntID ||
	    GICD_IROUTER + 8u * (uint32_t)source + 8u > fGICDSize) {
		return kIOReturnNoSpace;
	}

	if (priority == 0) {
		priority = (uint8_t)SWEET_GIC_DEFAULT_PRIORITY;
	}

	interruptState = IOSimpleLockLockDisableInterrupt(fLock);
	vector = vectorAt(source);
	extra = extraAt(source);

	if (vector->interruptRegistered) {
		IOSimpleLockUnlockEnableInterrupt(fLock, interruptState);
		return kIOReturnNoResources;
	}

	/*
	 * Mask first: the source must not fire between configure and the
	 * driver's explicit enable. No RWP polling here or in enable/disable --
	 * those paths run in interrupt context and the distributor stall is a
	 * whole-SoC wait.
	 */
	maskSPI(source);
	writeGroup(source, true);
	writePriority(source, priority);
	writeTrigger(source, level);
	writeRoute(source, 0);

	vector->nub = nub;
	vector->source = source;
	vector->target = target;
	vector->handler = handler;
	vector->refCon = refCon;
	vector->interruptActive = 0;
	vector->interruptDisabledHard = 1;
	vector->interruptDisabledSoft = 1;
	vector->interruptRegistered = 1;

	extra->options = (opts & ~SWEET_GIC_OPT_PRIORITY_MASK) |
	    SWEET_GIC_OPT_PRIORITY(priority);
	extra->stray = 0;
	extra->hits = 0;

	IOSimpleLockUnlockEnableInterrupt(fLock, interruptState);

	return kIOReturnSuccess;
}

IOReturn
SweetGIC::unregisterInterrupt(IOService *nub, int source)
{
	IOInterruptState interruptState;
	IOInterruptVector *vector;
	SweetGICSourceExtra *extra;

	(void)nub;

	if (!fReady || source < GIC_INTID_SPI_FIRST || source > fMaxIntID) {
		return kIOReturnBadArgument;
	}

	interruptState = IOSimpleLockLockDisableInterrupt(fLock);
	vector = vectorAt(source);
	extra = extraAt(source);

	if (!vector->interruptRegistered) {
		IOSimpleLockUnlockEnableInterrupt(fLock, interruptState);
		return kIOReturnNotOpen;
	}

	maskSPI(source);
	vector->interruptActive = 0;
	vector->interruptDisabledHard = 0;
	vector->interruptDisabledSoft = 0;
	vector->interruptRegistered = 0;
	vector->nub = NULL;
	vector->source = 0;
	vector->target = NULL;
	vector->handler = NULL;
	vector->refCon = NULL;

	extra->options = 0;
	extra->stray = 0;
	extra->hits = 0;

	IOSimpleLockUnlockEnableInterrupt(fLock, interruptState);

	return kIOReturnSuccess;
}

IOReturn
SweetGIC::getInterruptType(IOService *nub, int source, int *interruptType)
{
	(void)nub;

	if (!interruptType || !fReady) {
		return kIOReturnBadArgument;
	}
	if (source < GIC_INTID_SPI_FIRST || source > fMaxIntID) {
		return kIOReturnBadArgument;
	}

	*interruptType = triggerIsLevel(source) ? kIOInterruptTypeLevel :
	    kIOInterruptTypeEdge;

	return kIOReturnSuccess;
}

IOReturn
SweetGIC::enableInterrupt(IOService *nub, int source)
{
	IOInterruptState interruptState;
	IOInterruptVector *vector;

	(void)nub;

	if (!fReady || source < GIC_INTID_SPI_FIRST || source > fMaxIntID) {
		return kIOReturnBadArgument;
	}

	interruptState = IOSimpleLockLockDisableInterrupt(fLock);
	vector = vectorAt(source);

	if (!vector->interruptRegistered) {
		IOSimpleLockUnlockEnableInterrupt(fLock, interruptState);
		return kIOReturnNotOpen;
	}

	vector->interruptDisabledSoft = 0;
	vector->interruptDisabledHard = 0;
	synchronizeIO();
	unmaskSPI(source);

	IOSimpleLockUnlockEnableInterrupt(fLock, interruptState);

	return kIOReturnSuccess;
}

IOReturn
SweetGIC::disableInterrupt(IOService *nub, int source)
{
	IOInterruptState interruptState;
	IOInterruptVector *vector;

	(void)nub;

	if (!fReady || source < GIC_INTID_SPI_FIRST || source > fMaxIntID) {
		return kIOReturnBadArgument;
	}

	interruptState = IOSimpleLockLockDisableInterrupt(fLock);
	vector = vectorAt(source);

	if (!vector->interruptRegistered) {
		IOSimpleLockUnlockEnableInterrupt(fLock, interruptState);
		return kIOReturnNotOpen;
	}

	vector->interruptDisabledSoft = 1;
	vector->interruptDisabledHard = 1;
	maskSPI(source);

	IOSimpleLockUnlockEnableInterrupt(fLock, interruptState);

	return kIOReturnSuccess;
}

IOReturn
SweetGIC::causeInterrupt(IOService *nub, int source)
{
	(void)nub;

	if (!fReady || source < GIC_INTID_SPI_FIRST || source > fMaxIntID) {
		return kIOReturnBadArgument;
	}

	gicdWrite32(GICD_ISPENDR + 4u * ((uint32_t)source / 32),
	    1u << ((uint32_t)source % 32));

	return kIOReturnSuccess;
}

IOReturn
SweetGIC::setPriority(int source, uint8_t priority)
{
	IOInterruptState interruptState;
	IOInterruptVector *vector;
	SweetGICSourceExtra *extra;

	if (!fReady || source < GIC_INTID_SPI_FIRST || source > fMaxIntID) {
		return kIOReturnBadArgument;
	}

	interruptState = IOSimpleLockLockDisableInterrupt(fLock);
	vector = vectorAt(source);
	extra = extraAt(source);

	if (!vector->interruptRegistered) {
		IOSimpleLockUnlockEnableInterrupt(fLock, interruptState);
		return kIOReturnNotOpen;
	}

	writePriority(source, priority);
	extra->options = (extra->options & ~SWEET_GIC_OPT_PRIORITY_MASK) |
	    SWEET_GIC_OPT_PRIORITY(priority);

	IOSimpleLockUnlockEnableInterrupt(fLock, interruptState);

	return kIOReturnSuccess;
}

IOReturn
SweetGIC::setTriggerType(int source, bool level)
{
	IOInterruptState interruptState;
	IOInterruptVector *vector;
	SweetGICSourceExtra *extra;
	bool wasEnabled;

	if (!fReady || source < GIC_INTID_SPI_FIRST || source > fMaxIntID) {
		return kIOReturnBadArgument;
	}

	interruptState = IOSimpleLockLockDisableInterrupt(fLock);
	vector = vectorAt(source);
	extra = extraAt(source);

	if (!vector->interruptRegistered) {
		IOSimpleLockUnlockEnableInterrupt(fLock, interruptState);
		return kIOReturnNotOpen;
	}

	/* Changing the configuration of an enabled source: quiet it first. */
	wasEnabled = !vector->interruptDisabledSoft;
	if (wasEnabled) {
		maskSPI(source);
		synchronizeIO();
	}

	writeTrigger(source, level);
	extra->options = level ? (extra->options | SWEET_GIC_OPT_LEVEL) :
	    (extra->options & ~SWEET_GIC_OPT_LEVEL);

	if (wasEnabled) {
		synchronizeIO();
		unmaskSPI(source);
	}

	IOSimpleLockUnlockEnableInterrupt(fLock, interruptState);

	return kIOReturnSuccess;
}

void
SweetGIC::setMaskStraySPIs(bool mask)
{
	fMaskStraySPIs = mask;
}

/* ------------------------------------------------------------ dispatch */

uint32_t
SweetGIC::acknowledge(void)
{
	return (uint32_t)__builtin_arm_rsr64("ICC_IAR1_EL1");
}

bool
SweetGIC::dispatch(uint32_t intid)
{
	IOInterruptVector *vector = vectorAt((int)intid);
	SweetGICSourceExtra *extra;
	IOInterruptHandler handler;
	void *target;
	void *refCon;
	void *nub;

	if (!vector) {
		if (intid >= (uint32_t)GIC_INTID_SPI_FIRST) {
			__atomic_add_fetch(&fStats.stray, 1u, __ATOMIC_RELAXED);
		}
		return false;
	}

	extra = extraAt((int)intid);
	handler = vector->handler;

	if (!vector->interruptRegistered || !handler) {
		__atomic_add_fetch(&fStats.stray, 1u, __ATOMIC_RELAXED);
		straySPI((int)intid);
		return false;
	}

	target = vector->target;
	refCon = vector->refCon;
	nub = vector->nub;

	__atomic_add_fetch(&extra->hits, 1u, __ATOMIC_RELAXED);
	__atomic_add_fetch(&fStats.handled, 1u, __ATOMIC_RELAXED);

	handler(target, refCon, nub, (int)intid);

	return true;
}

void
SweetGIC::endOfInterrupt(uint64_t iar)
{
	/* EOIMODE = 0 on this platform: EOI drops priority and deactivates. */
	__builtin_arm_wsr64("ICC_EOIR1_EL1", iar);
	__atomic_add_fetch(&fStats.endOfInterrupts, 1u, __ATOMIC_RELAXED);
}

/*
 * Dispatch and complete one acknowledged interrupt. Returns true exactly when
 * this controller wrote ICC_EOIR1_EL1, so the caller knows it must not write
 * it again: either a registered handler ran, or the SPI had no handler and
 * was only counted (and possibly masked).
 *
 * SGIs and PPIs (INTID < 32) return false without completing anything: they
 * belong to the platform, and the caller still owns the EOI. The same is true
 * for spurious INTIDs.
 */
bool
SweetGIC::handleInterrupt(uint64_t iar)
{
	uint32_t intid = (uint32_t)(iar & 0xffffffULL);

	if (intid >= (uint32_t)GIC_INTID_SPECIAL_FIRST) {
		__atomic_add_fetch(&fStats.spurious, 1u, __ATOMIC_RELAXED);
		return false;
	}
	if (intid < (uint32_t)GIC_INTID_SPI_FIRST) {
		return false;
	}

	__atomic_add_fetch(&fStats.interrupts, 1u, __ATOMIC_RELAXED);
	(void)dispatch(intid);
	endOfInterrupt(iar);

	return true;
}

/* ------------------------------------------------------------- queries */

bool
SweetGIC::isRegistered(int source) const
{
	IOInterruptVector *vector = vectorAt(source);

	return vector && vector->interruptRegistered;
}

bool
SweetGIC::isEnabled(int source) const
{
	IOInterruptVector *vector = vectorAt(source);

	return vector && vector->interruptRegistered &&
	    !vector->interruptDisabledSoft;
}

uint64_t
SweetGIC::getHits(int source) const
{
	SweetGICSourceExtra *extra = extraAt(source);

	return extra ? __atomic_load_n(&extra->hits, __ATOMIC_RELAXED) : 0;
}

void
SweetGIC::getStatistics(Statistics *stats) const
{
	if (stats) {
		*stats = fStats;
	}
}

/* ----------------------------------------------------------- lifecycle */

SweetGIC *
SweetGIC::withBases(vm_offset_t gicd, vm_size_t gicdSize, vm_offset_t gicr,
    vm_size_t gicrSize)
{
	SweetGIC *gic = new SweetGIC;

	if (gic && !gic->initWithBases(gicd, gicdSize, gicr, gicrSize)) {
		gic->release();
		gic = NULL;
	}

	return gic;
}

bool
SweetGIC::initWithBases(vm_offset_t gicd, vm_size_t gicdSize, vm_offset_t gicr,
    vm_size_t gicrSize)
{
	/* A failed init is released, so every field must be safe to free. */
	fGICD = 0;
	fGICDSize = 0;
	fGICR = 0;
	fGICRSize = 0;
	fMaxIntID = GIC_INTID_SPI_LAST;
	fReady = false;
	fMaskStraySPIs = false;
	fVectors = NULL;
	fExtras = NULL;
	fLock = NULL;
	fStats.interrupts = 0;
	fStats.handled = 0;
	fStats.spurious = 0;
	fStats.stray = 0;
	fStats.endOfInterrupts = 0;

	if (!gicd || !gicdSize || !gicr || !gicrSize) {
		return false;
	}

	fGICD = gicd;
	fGICDSize = gicdSize;
	fGICR = gicr;
	fGICRSize = gicrSize;
	fMaxIntID = GIC_INTID_SPI_LAST;
	fMaskStraySPIs = false;

	fVectors = IONewZero(IOInterruptVector, SWEET_GIC_VECTORS);
	if (!fVectors) {
		return false;
	}

	fExtras = IONewZero(SweetGICSourceExtra, SWEET_GIC_VECTORS);
	if (!fExtras) {
		IODelete(fVectors, IOInterruptVector, SWEET_GIC_VECTORS);
		fVectors = NULL;
		return false;
	}

	fLock = IOSimpleLockAlloc();
	if (!fLock) {
		IODelete(fExtras, SweetGICSourceExtra, SWEET_GIC_VECTORS);
		fExtras = NULL;
		IODelete(fVectors, IOInterruptVector, SWEET_GIC_VECTORS);
		fVectors = NULL;
		return false;
	}

	return true;
}

void
SweetGIC::free(void)
{
	if (fLock) {
		IOSimpleLockFree(fLock);
		fLock = NULL;
	}
	if (fExtras) {
		IODelete(fExtras, SweetGICSourceExtra, SWEET_GIC_VECTORS);
		fExtras = NULL;
	}
	if (fVectors) {
		IODelete(fVectors, IOInterruptVector, SWEET_GIC_VECTORS);
		fVectors = NULL;
	}

	OSObject::free();
}

IOReturn
SweetGIC::start(void)
{
	uint32_t ctlr;
	uint32_t wanted;
	uint32_t typer;
	uint32_t lines;
	uint32_t maxIntID;

	if (fReady) {
		return kIOReturnSuccess;
	}

	/*
	 * The platform expert already turned Group 1 on; this only makes the
	 * controller self-sufficient if it is installed on a different path.
	 * This is the one place RWP is polled, and it is not under a lock.
	 */
	ctlr = gicdRead32(GICD_CTLR);
	wanted = ctlr | GICD_CTLR_ARE_NS | GICD_CTLR_ENABLEGRP1A |
	    GICD_CTLR_ENABLEGRP1;
	if (wanted != ctlr) {
		gicdWrite32(GICD_CTLR, wanted);
		(void)waitForRWP();
	}

	typer = gicdRead32(GICD_TYPER);
	lines = (typer & GICD_TYPER_LINES_MASK) + 1u;
	maxIntID = 32u * lines - 1u;
	if (maxIntID > (uint32_t)GIC_INTID_SPI_LAST) {
		maxIntID = (uint32_t)GIC_INTID_SPI_LAST;
	}

	fMaxIntID = (int)maxIntID;
	fReady = true;

	SWEET_LOG_INFO(
	    "SWEET GIC: ctlr=%#x typer=%#x max-intid=%d vectors=%u gicr=%#lx+%#lx\n",
	    ctlr, typer, fMaxIntID, SWEET_GIC_VECTORS, (unsigned long)fGICR,
	    (unsigned long)fGICRSize);

	return kIOReturnSuccess;
}

SweetGIC *
SweetGIC::controller(void)
{
	return sweetGIC;
}

void
SweetGIC::setController(SweetGIC *gic)
{
	sweetGIC = gic;
}

/* ------------------------------------------------------------- C entry */

extern "C" int
sweet_gic_install(void)
{
	uint64_t gicd;
	uint64_t gicdSize;
	uint64_t gicr;
	uint64_t gicrSize;
	SweetGIC *gic;

	if (sweetGIC) {
		return 1;
	}

	gicd = pe_gic_distributor_base();
	gicdSize = pe_gic_distributor_size();
	gicr = pe_gic_redistributor_base();
	gicrSize = pe_gic_redistributor_size();

	if (!gicd || !gicdSize || !gicr || !gicrSize) {
		return -1;
	}

	gic = SweetGIC::withBases((vm_offset_t)gicd, (vm_size_t)gicdSize,
	    (vm_offset_t)gicr, (vm_size_t)gicrSize);
	if (!gic) {
		return -2;
	}

	if (gic->start() != kIOReturnSuccess) {
		gic->release();
		return -3;
	}

	SweetGIC::setController(gic);

	return 0;
}

extern "C" int
sweet_gic_installed(void)
{
	return sweetGIC ? 1 : 0;
}

extern "C" int
sweet_gic_handle_irq(uint64_t iar)
{
	if (!sweetGIC) {
		return 0;
	}

	return sweetGIC->handleInterrupt(iar) ? 1 : 0;
}

extern "C" void
sweet_gic_set_stray_policy(int mask)
{
	if (sweetGIC) {
		sweetGIC->setMaskStraySPIs(mask != 0);
	}
}

extern "C" int
sweet_gic_register_irq(int intid, void *target, sweet_gic_handler_t handler,
    void *refCon, uint32_t options)
{
	IOInterruptHandler interruptHandler = handler;

	if (!sweetGIC) {
		return kIOReturnNotReady;
	}

	return sweetGIC->registerInterrupt(NULL, intid, target, interruptHandler,
	    refCon, (IOOptionBits)options);
}

extern "C" int
sweet_gic_unregister_irq(int intid)
{
	if (!sweetGIC) {
		return kIOReturnNotReady;
	}

	return sweetGIC->unregisterInterrupt(NULL, intid);
}

extern "C" int
sweet_gic_enable_irq(int intid)
{
	if (!sweetGIC) {
		return kIOReturnNotReady;
	}

	return sweetGIC->enableInterrupt(NULL, intid);
}

extern "C" int
sweet_gic_disable_irq(int intid)
{
	if (!sweetGIC) {
		return kIOReturnNotReady;
	}

	return sweetGIC->disableInterrupt(NULL, intid);
}

extern "C" int
sweet_gic_cause_irq(int intid)
{
	if (!sweetGIC) {
		return kIOReturnNotReady;
	}

	return sweetGIC->causeInterrupt(NULL, intid);
}

extern "C" uint64_t
sweet_gic_hits(int intid)
{
	if (!sweetGIC) {
		return 0;
	}

	return sweetGIC->getHits(intid);
}

#endif /* SWEET */
