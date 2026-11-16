/*
 * GenericInterruptController: the GICv3 distributor owner for the sweet
 * platform, as an IOInterruptController subclass.
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
 * asks for yet.
 *
 * Two parameter sources coexist:
 *
 *   hardcoded (today)   gic_register_irq(intid, ...) passes the INTID with a
 *                       NULL nub; resolveSpec() reads the per-INTID table at
 *                       the top of this file, whose values are exactly what
 *                       the ADT specifier will carry later.
 *   ADT (prepared)      a nub registers through IOService; the base class
 *                       resolves vector number and vectorData, and
 *                       resolveSpec() parses the GIC_SPEC_CELLS-cell
 *                       specifier (see generic_interrupt_controller.h).
 *
 * Nothing in the hardware path differs between the two: initVector() programs
 * group, priority, trigger and route from the resolved spec, and the hooks
 * enable/disable/cause the INTID. Switching over means emitting the ADT
 * specifiers and passing a nub; the controller code does not change.
 *
 * EOI is written exactly once per interrupt and only by this controller, for
 * the INTIDs it completed. sleh_irq() keeps the legacy paths (timer, IPIs,
 * drivers that have not migrated) and must not EOI what gic_handle_irq()
 * reported as completed.
 *
 * Rollout is staged: the legacy predicate chain runs first in sleh_irq() and
 * only unclaimed INTIDs reach this controller. While that is the case the
 * stray policy is "count and complete", not "mask", so an SPI nobody has
 * registered yet behaves exactly as it did before; mask the strays once every
 * live SPI is registered.
 */

#include "GenericInterruptController.h"

#include <IOKit/IOLib.h>
#include <stdint.h>

#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"

#if defined(SWEET)

#define super IOInterruptController

OSDefineMetaClassAndStructors(GenericInterruptController, IOInterruptController);

/* Vector table is indexed by INTID; 1020 entries cover SPIs and the specials
 * that are never registered, so vectorNumber == INTID and an ADT specifier can
 * carry the INTID in its first cell. */
#define GIC_VECTOR_COUNT GIC_INTID_SPECIAL_FIRST

static GenericInterruptController *gIC;

/*
 * Hardcoded wiring. This is the table the ADT will replace: one row per live
 * SPI, with the values the specifier will carry (type, priority, target CPU).
 * Every current driver programs exactly this (level, priority 0xa0, route to
 * the boot CPU), so the hardware path is already the final one.
 */
static const gic_interrupt_spec gHardcodedSpecs[] = {
	{  37, GIC_SPEC_TYPE_LEVEL, GIC_SPEC_DEFAULT_PRIORITY, 0 }, /* apps RSC */
	{ 115, GIC_SPEC_TYPE_LEVEL, GIC_SPEC_DEFAULT_PRIORITY, 0 }, /* MDSS */
	{ 161, GIC_SPEC_TYPE_LEVEL, GIC_SPEC_DEFAULT_PRIORITY, 0 }, /* MDSS NoC */
	{ 165, GIC_SPEC_TYPE_LEVEL, GIC_SPEC_DEFAULT_PRIORITY, 0 }, /* DWC3 */
	{ 240, GIC_SPEC_TYPE_LEVEL, GIC_SPEC_DEFAULT_PRIORITY, 0 }, /* TLMM summary */
	{ 513, GIC_SPEC_TYPE_LEVEL, GIC_SPEC_DEFAULT_PRIORITY, 0 }, /* PMIC-arb */
};

/* ---------------------------------------------------------------- MMIO */

uint32_t
GenericInterruptController::gicdRead32(uint32_t offset) const
{
	return *(volatile uint32_t *)(fGICD + offset);
}

void
GenericInterruptController::gicdWrite32(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(fGICD + offset) = value;
}

void
GenericInterruptController::gicdWrite64(uint32_t offset, uint64_t value)
{
	*(volatile uint64_t *)(fGICD + offset) = value;
}

/*
 * Make the programmed configuration visible before the enable bit is set,
 * same reason as the DSB ISH in IOInterruptController::enableInterrupt: a DMB
 * is not enough, the distributor is a separate observer.
 */
void
GenericInterruptController::synchronizeIO(void)
{
	__asm__ volatile ("dsb ish" ::: "memory");
}

/*
 * GICD_CTLR.RWP is polled exactly once: right after the CTLR write in
 * bringUp(), where the spec wants the change acknowledged before anything
 * else is configured. Nothing else waits on it. It is a distributor-wide
 * stall, and the enable/disable writes that drivers issue from interrupt
 * context must not spin on it.
 */
bool
GenericInterruptController::waitForRWP(void)
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
GenericInterruptController::vectorAt(int intid) const
{
	if (intid < GIC_INTID_SPI_FIRST || intid > fMaxIntID) {
		return NULL;
	}

	return &vectors[intid];
}

const gic_interrupt_spec *
GenericInterruptController::hardcodedSpec(int intid) const
{
	for (unsigned int i = 0;
	    i < sizeof(gHardcodedSpecs) / sizeof(gHardcodedSpecs[0]); i++) {
		if (gHardcodedSpecs[i].intid == (uint32_t)intid) {
			return &gHardcodedSpecs[i];
		}
	}

	return NULL;
}

/* ------------------------------------------------------- spec resolution */

/*
 * The ADT side. Once the generator emits, for a device node,
 *
 *     interrupt-parent = <phandle of the gicv3 node>
 *     interrupts       = <INTID TYPE PRIORITY TARGET>
 *
 * IODeviceTreeSupport maps that into IOInterruptSpecifiers, resolveInterrupt()
 * records our controller from "gicv3", and the base-class registration path
 * hands us `source` = the device-local index with vectorData pointing at the
 * four cells. Nothing else needs to change.
 */
IOReturn
GenericInterruptController::resolveSpecADT(IOService *nub, int source,
    gic_interrupt_spec *spec) const
{
	IOInterruptSource *sources;
	OSData *data;
	const uint32_t *cells;

	if (!nub) {
		return kIOReturnBadArgument;
	}
	sources = nub->_interruptSources;
	if (!sources) {
		return kIOReturnNoResources;
	}
	data = sources[source].vectorData;
	if (!data || data->getLength() < GIC_SPEC_CELLS * sizeof(uint32_t)) {
		return kIOReturnBadArgument;
	}
	cells = (const uint32_t *)data->getBytesNoCopy();

	spec->intid = cells[0];
	spec->type = cells[1];
	spec->priority = cells[2];
	spec->target = cells[3];

	return kIOReturnSuccess;
}

IOReturn
GenericInterruptController::resolveSpecHardcoded(int source,
    gic_interrupt_spec *spec) const
{
	const gic_interrupt_spec *hard;

	if (source < GIC_INTID_SPI_FIRST || source > GIC_INTID_SPI_LAST) {
		/* SGIs and PPIs are per-CPU platform state, not ours. */
		return kIOReturnUnsupported;
	}

	hard = hardcodedSpec(source);
	if (hard) {
		*spec = *hard;
	} else {
		spec->intid = (uint32_t)source;
		spec->type = GIC_SPEC_TYPE_LEVEL;
		spec->priority = GIC_SPEC_DEFAULT_PRIORITY;
		spec->target = 0;
	}

	return kIOReturnSuccess;
}

IOReturn
GenericInterruptController::resolveSpec(IOService *nub, int source,
    gic_interrupt_spec *spec)
{
	if (!spec) {
		return kIOReturnBadArgument;
	}

	if (nub != NULL) {
		return resolveSpecADT(nub, source, spec);
	}

	return resolveSpecHardcoded(source, spec);
}

/* ------------------------------------------------------- configuration */

void
GenericInterruptController::writeGroup(int intid, bool group1)
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
GenericInterruptController::writePriority(int intid, uint8_t priority)
{
	uint32_t offset = GICD_IPRIORITYR + 4u * ((uint32_t)intid / 4);
	uint32_t shift = 8u * ((uint32_t)intid % 4);
	uint32_t value = gicdRead32(offset);

	value &= ~(0xffu << shift);
	value |= (uint32_t)priority << shift;

	gicdWrite32(offset, value);
}

void
GenericInterruptController::writeTrigger(int intid, bool level)
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
GenericInterruptController::triggerIsLevel(int intid) const
{
	uint32_t offset = GICD_ICFGR + 4u * ((uint32_t)intid / 16);
	uint32_t shift = 2u * ((uint32_t)intid % 16);

	return ((gicdRead32(offset) >> shift) & 2u) == 0;
}

/*
 * SPIs only, and 0 is the default: every driver on this platform programs
 * IROUTER = 0 today, so an unconfigured route keeps the current behaviour.
 * The ADT specifier's TARGET cell will be translated through the topology
 * into an affinity value when routing to another PE is needed.
 */
uint64_t
GenericInterruptController::routeForTarget(uint32_t target) const
{
	if (target != 0) {
		SWEET_LOG_ERROR("SWEET GIC: target CPU %u is not translated yet; "
		    "routing to the boot CPU\n", target);
	}

	return 0;
}

void
GenericInterruptController::writeRoute(int intid, uint64_t affinity)
{
	gicdWrite64(GICD_IROUTER + 8u * (uint32_t)intid,
	    affinity & 0xffffffffffULL);
}

void
GenericInterruptController::maskSPI(int intid)
{
	gicdWrite32(GICD_ICENABLER + 4u * ((uint32_t)intid / 32),
	    1u << ((uint32_t)intid % 32));
}

void
GenericInterruptController::unmaskSPI(int intid)
{
	gicdWrite32(GICD_ISENABLER + 4u * ((uint32_t)intid / 32),
	    1u << ((uint32_t)intid % 32));
}

void
GenericInterruptController::programSpec(int intid, const gic_interrupt_spec *spec)
{
	uint8_t priority = (uint8_t)spec->priority;

	if (priority == 0) {
		priority = (uint8_t)GIC_SPEC_DEFAULT_PRIORITY;
	}

	writeGroup(intid, true);   /* Group 1: every NS interrupt arrives there */
	writePriority(intid, priority);
	writeTrigger(intid, spec->type != GIC_SPEC_TYPE_EDGE);
	writeRoute(intid, routeForTarget(spec->target));
}

/*
 * An SPI nobody registered. Counted, completed by the caller of dispatch(),
 * and masked only when the stray policy says so: during the staged rollout
 * the legacy chain still owns most INTIDs, so masking here would silently
 * kill a device that simply has not migrated yet.
 */
void
GenericInterruptController::straySPI(int intid)
{
	uint32_t seen;

	if (intid < GIC_INTID_SPI_FIRST || intid >= (int)GIC_VECTOR_COUNT) {
		return;
	}

	seen = (uint32_t)__atomic_add_fetch(&fStray[intid], 1u, __ATOMIC_RELAXED);

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

/* ------------------------------------------- simplified subclass hooks */

bool
GenericInterruptController::vectorCanBeShared(IOInterruptVectorNumber,
    IOInterruptVector *)
{
	/* GIC SPIs are not shareable: one INTID, one consumer. */
	return false;
}

/*
 * Called with the source still masked; the caller (registerInterrupt) decides
 * when it becomes visible.
 */
void
GenericInterruptController::initVector(IOInterruptVectorNumber vectorNumber,
    IOInterruptVector *vector)
{
	gic_interrupt_spec spec;

	if (resolveSpec(vector->nub, vector->source, &spec) != kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET GIC: cannot resolve intid %d; left masked\n",
		    (int)vectorNumber);
		return;
	}

	maskSPI((int)vectorNumber);
	programSpec((int)vectorNumber, &spec);
}

int
GenericInterruptController::getVectorType(IOInterruptVectorNumber vectorNumber,
    IOInterruptVector *)
{
	return triggerIsLevel((int)vectorNumber) ? kIOInterruptTypeLevel :
	    kIOInterruptTypeEdge;
}

void
GenericInterruptController::disableVectorHard(
    IOInterruptVectorNumber vectorNumber, IOInterruptVector *)
{
	maskSPI((int)vectorNumber);
}

void
GenericInterruptController::enableVector(IOInterruptVectorNumber vectorNumber,
    IOInterruptVector *)
{
	unmaskSPI((int)vectorNumber);
}

void
GenericInterruptController::causeVector(IOInterruptVectorNumber vectorNumber,
    IOInterruptVector *)
{
	gicdWrite32(GICD_ISPENDR + 4u * ((uint32_t)vectorNumber / 32),
	    1u << ((uint32_t)vectorNumber % 32));
}

/* --------------------------------------------------------- registration */

IOReturn
GenericInterruptController::registerInterrupt(IOService *nub, int source,
    void *target, IOInterruptHandler handler, void *refCon)
{
	gic_interrupt_spec spec;
	IOInterruptState interruptState;
	IOInterruptVector *vector;
	int intid;
	IOReturn rc;

	if (!fReady) {
		return kIOReturnNotReady;
	}
	if (!handler) {
		return kIOReturnBadArgument;
	}

	rc = resolveSpec(nub, source, &spec);
	if (rc != kIOReturnSuccess) {
		return rc;
	}

	intid = (int)spec.intid;
	if (intid < GIC_INTID_SPI_FIRST || intid > GIC_INTID_SPI_LAST) {
		return kIOReturnUnsupported;
	}
	if (intid > fMaxIntID ||
	    GICD_IROUTER + 8u * (uint32_t)intid + 8u > fGICDSize) {
		return kIOReturnNoSpace;
	}

	interruptState = IOSimpleLockLockDisableInterrupt(controllerLock);
	vector = vectorAt(intid);
	if (!vector) {
		IOSimpleLockUnlockEnableInterrupt(controllerLock, interruptState);
		return kIOReturnNoSpace;
	}
	if (vector->interruptRegistered) {
		IOSimpleLockUnlockEnableInterrupt(controllerLock, interruptState);
		return kIOReturnNoResources;
	}

	/* The base class locks a vector through this field; our overrides use
	 * the controller lock, but a real IOInterruptVector carries its lock. */
	if (!vector->interruptLock) {
		vector->interruptLock = IOLockAlloc();
		if (!vector->interruptLock) {
			IOSimpleLockUnlockEnableInterrupt(controllerLock, interruptState);
			return kIOReturnNoMemory;
		}
	}

	vector->nub = nub;
	vector->source = source;
	vector->target = target;
	vector->handler = handler;
	vector->refCon = refCon;
	vector->interruptActive = 0;

	initVector(vectorNumberForIntid(intid), vector);

	vector->interruptDisabledHard = 1;
	vector->interruptDisabledSoft = 1;
	vector->interruptRegistered = 1;

	__atomic_store_n(&fStray[intid], 0u, __ATOMIC_RELAXED);
	__atomic_store_n(&fHits[intid], 0ull, __ATOMIC_RELAXED);

	IOSimpleLockUnlockEnableInterrupt(controllerLock, interruptState);

	return kIOReturnSuccess;
}

IOReturn
GenericInterruptController::unregisterInterrupt(IOService *nub, int source)
{
	gic_interrupt_spec spec;
	IOInterruptState interruptState;
	IOInterruptVector *vector;
	IOReturn rc;

	if (!fReady) {
		return kIOReturnNotReady;
	}
	rc = resolveSpec(nub, source, &spec);
	if (rc != kIOReturnSuccess) {
		return rc;
	}

	interruptState = IOSimpleLockLockDisableInterrupt(controllerLock);
	vector = vectorAt((int)spec.intid);
	if (!vector || !vector->interruptRegistered) {
		IOSimpleLockUnlockEnableInterrupt(controllerLock, interruptState);
		return kIOReturnNotOpen;
	}

	maskSPI((int)spec.intid);
	vector->interruptActive = 0;
	vector->interruptDisabledHard = 0;
	vector->interruptDisabledSoft = 0;
	vector->interruptRegistered = 0;
	vector->nub = NULL;
	vector->source = 0;
	vector->target = NULL;
	vector->handler = NULL;
	vector->refCon = NULL;

	IOSimpleLockUnlockEnableInterrupt(controllerLock, interruptState);

	return kIOReturnSuccess;
}

IOReturn
GenericInterruptController::getInterruptType(IOService *nub, int source,
    int *interruptType)
{
	gic_interrupt_spec spec;
	IOInterruptVector *vector;

	if (!interruptType || !fReady) {
		return kIOReturnBadArgument;
	}
	if (resolveSpec(nub, source, &spec) != kIOReturnSuccess) {
		return kIOReturnBadArgument;
	}

	vector = vectorAt((int)spec.intid);
	if (!vector) {
		return kIOReturnBadArgument;
	}

	*interruptType = getVectorType(vectorNumberForIntid((int)spec.intid),
	    vector);

	return kIOReturnSuccess;
}

IOReturn
GenericInterruptController::enableInterrupt(IOService *nub, int source)
{
	gic_interrupt_spec spec;
	IOInterruptState interruptState;
	IOInterruptVector *vector;
	bool enableHardware = false;

	if (!fReady) {
		return kIOReturnNotReady;
	}
	if (resolveSpec(nub, source, &spec) != kIOReturnSuccess) {
		return kIOReturnBadArgument;
	}

	interruptState = IOSimpleLockLockDisableInterrupt(controllerLock);
	vector = vectorAt((int)spec.intid);
	if (!vector || !vector->interruptRegistered) {
		IOSimpleLockUnlockEnableInterrupt(controllerLock, interruptState);
		return kIOReturnNotOpen;
	}

	if (vector->interruptDisabledSoft) {
		vector->interruptDisabledSoft = 0;
		OSMemoryBarrier();
		if (vector->interruptDisabledHard) {
			vector->interruptDisabledHard = 0;
			enableHardware = true;
		}
	}

	IOSimpleLockUnlockEnableInterrupt(controllerLock, interruptState);

	if (enableHardware) {
		/* Do not unmask under the lock, and let an in-flight handler on
		 * another CPU finish first (base-class behaviour). */
		if (!getPlatform()->atInterruptLevel()) {
			while (vector->interruptActive) {
				__asm__ volatile ("yield" ::: "memory");
			}
		}
		OSSynchronizeIO();
		enableVector(vectorNumberForIntid((int)spec.intid), vector);
	}

	return kIOReturnSuccess;
}

/*
 * Mask immediately. The base class defers the hard disable to the next
 * dispatch (it sets interruptDisabledSoft and lets handleInterrupt call
 * disableVectorHard); that does not work here, because the drivers mask their
 * level source in the top half so it must be quiet before the handler returns.
 * dispatch() still honours the soft flag for anything that arrives late.
 */
IOReturn
GenericInterruptController::disableInterrupt(IOService *nub, int source)
{
	gic_interrupt_spec spec;
	IOInterruptState interruptState;
	IOInterruptVector *vector;
	bool maskHardware = false;

	if (!fReady) {
		return kIOReturnNotReady;
	}
	if (resolveSpec(nub, source, &spec) != kIOReturnSuccess) {
		return kIOReturnBadArgument;
	}

	interruptState = IOSimpleLockLockDisableInterrupt(controllerLock);
	vector = vectorAt((int)spec.intid);
	if (!vector || !vector->interruptRegistered) {
		IOSimpleLockUnlockEnableInterrupt(controllerLock, interruptState);
		return kIOReturnNotOpen;
	}

	vector->interruptDisabledSoft = 1;
	OSMemoryBarrier();
	if (!vector->interruptDisabledHard) {
		vector->interruptDisabledHard = 1;
		maskHardware = true;
	}

	IOSimpleLockUnlockEnableInterrupt(controllerLock, interruptState);

	if (maskHardware) {
		disableVectorHard(vectorNumberForIntid((int)spec.intid), vector);
	}

	return kIOReturnSuccess;
}

IOReturn
GenericInterruptController::causeInterrupt(IOService *nub, int source)
{
	gic_interrupt_spec spec;
	IOInterruptVector *vector;

	if (!fReady) {
		return kIOReturnNotReady;
	}
	if (resolveSpec(nub, source, &spec) != kIOReturnSuccess) {
		return kIOReturnBadArgument;
	}

	vector = vectorAt((int)spec.intid);
	if (!vector || !vector->interruptRegistered) {
		return kIOReturnNotOpen;
	}

	causeVector(vectorNumberForIntid((int)spec.intid), vector);

	return kIOReturnSuccess;
}

/* ------------------------------------------------------------ dispatch */

uint32_t
GenericInterruptController::acknowledge(void)
{
	return (uint32_t)__builtin_arm_rsr64("ICC_IAR1_EL1");
}

bool
GenericInterruptController::dispatch(uint32_t intid)
{
	IOInterruptVector *vector = vectorAt((int)intid);
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

	handler = vector->handler;
	if (!vector->interruptRegistered || !handler) {
		__atomic_add_fetch(&fStats.stray, 1u, __ATOMIC_RELAXED);
		straySPI((int)intid);
		return false;
	}

	if (vector->interruptDisabledSoft) {
		/* The driver asked for quiet after registration or from its own
		 * top half; make sure the distributor agrees and do not run the
		 * handler for a source it considers stopped. */
		if (!vector->interruptDisabledHard) {
			vector->interruptDisabledHard = 1;
			maskSPI((int)intid);
		}
		return false;
	}

	target = vector->target;
	refCon = vector->refCon;
	nub = vector->nub;

	__atomic_add_fetch(&fHits[intid], 1ull, __ATOMIC_RELAXED);
	__atomic_add_fetch(&fStats.handled, 1u, __ATOMIC_RELAXED);

	vector->interruptActive = 1;
	handler(target, refCon, nub, (int)intid);
	vector->interruptActive = 0;

	return true;
}

void
GenericInterruptController::endOfInterrupt(uint64_t iar)
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
GenericInterruptController::handleInterruptIAR(uint64_t iar)
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

IOReturn
GenericInterruptController::handleInterrupt(void *refCon, IOService *nub,
    int source)
{
	/* Hardcoded-mode entry: `source` is the INTID. In ADT mode the platform
	 * passes the device-local source and the base class context; this
	 * method is then replaced by the platform's own action lookup. */
	(void)refCon;
	(void)nub;

	return dispatch((uint32_t)source) ? kIOReturnSuccess : kIOReturnInvalid;
}

IOInterruptAction
GenericInterruptController::getInterruptHandlerAddress(void)
{
	return OSMemberFunctionCast(IOInterruptAction, this,
	    &GenericInterruptController::handleInterrupt);
}

/* ------------------------------------------------------------- queries */

bool
GenericInterruptController::isRegistered(int source) const
{
	IOInterruptVector *vector;

	if (!fReady || source < GIC_INTID_SPI_FIRST || source > fMaxIntID) {
		return false;
	}
	vector = vectorAt(source);

	return vector && vector->interruptRegistered;
}

bool
GenericInterruptController::isEnabled(int source) const
{
	IOInterruptVector *vector;

	if (!fReady || source < GIC_INTID_SPI_FIRST || source > fMaxIntID) {
		return false;
	}
	vector = vectorAt(source);

	return vector && vector->interruptRegistered &&
	    !vector->interruptDisabledSoft;
}

uint64_t
GenericInterruptController::getHits(int source) const
{
	if (source < GIC_INTID_SPI_FIRST || source >= (int)GIC_VECTOR_COUNT) {
		return 0;
	}

	return __atomic_load_n(&fHits[source], __ATOMIC_RELAXED);
}

void
GenericInterruptController::getStatistics(Statistics *stats) const
{
	if (stats) {
		*stats = fStats;
	}
}

void
GenericInterruptController::setMaskStraySPIs(bool mask)
{
	fMaskStraySPIs = mask;
}

/*
 * Read-only inventory of the live SPIs. The dispatch path cannot supply this
 * while the legacy chain is in front of the controller: those INTIDs are
 * claimed before dispatch ever sees them. GICD_ISENABLER is the ground truth.
 */
void
GenericInterruptController::logEnabledSPIs(const char *tag)
{
	uint32_t intids[64];
	uint32_t count = 0;
	uint32_t total = 0;
	uint32_t firstWord = (uint32_t)GIC_INTID_SPI_FIRST / 32;
	uint32_t lastWord = (uint32_t)fMaxIntID / 32;

	if (!fReady) {
		return;
	}

	for (uint32_t word = firstWord; word <= lastWord; word++) {
		uint32_t enabled = gicdRead32(GICD_ISENABLER + 4u * word);

		if (!enabled) {
			continue;
		}
		for (uint32_t bit = 0; bit < 32; bit++) {
			uint32_t intid = word * 32 + bit;

			if ((enabled & (1u << bit)) == 0) {
				continue;
			}
			if (intid < (uint32_t)GIC_INTID_SPI_FIRST ||
			    intid > (uint32_t)fMaxIntID) {
				continue;
			}
			total++;
			if (count < sizeof(intids) / sizeof(intids[0])) {
				intids[count++] = intid;
			}
		}
	}

	SWEET_LOG_INFO("SWEET GIC: %u enabled SPIs at %s (max-intid %d)\n",
	    total, tag ? tag : "?", fMaxIntID);
	for (uint32_t i = 0; i < count; i++) {
		SWEET_LOG_INFO("SWEET GIC:   intid %u%s\n", intids[i],
		    isRegistered((int)intids[i]) ? " registered" : "");
	}
	if (total > count) {
		SWEET_LOG_INFO("SWEET GIC:   ... %u more\n", total - count);
	}
}

/* ----------------------------------------------------------- lifecycle */

GenericInterruptController *
GenericInterruptController::withBases(vm_offset_t gicd, vm_size_t gicdSize,
    vm_offset_t gicr, vm_size_t gicrSize)
{
	GenericInterruptController *gic = new GenericInterruptController;

	if (gic && !gic->initWithBases(gicd, gicdSize, gicr, gicrSize)) {
		gic->release();
		gic = NULL;
	}

	return gic;
}

bool
GenericInterruptController::initWithBases(vm_offset_t gicd, vm_size_t gicdSize,
    vm_offset_t gicr, vm_size_t gicrSize)
{
	/* A failed init is released, so every field must be safe to free. */
	fGICD = 0;
	fGICDSize = 0;
	fGICR = 0;
	fGICRSize = 0;
	fMaxIntID = GIC_INTID_SPI_LAST;
	fReady = false;
	fMaskStraySPIs = false;
	fStats.interrupts = 0;
	fStats.handled = 0;
	fStats.spurious = 0;
	fStats.stray = 0;
	fStats.endOfInterrupts = 0;
	for (unsigned int i = 0; i < GIC_VECTOR_COUNT; i++) {
		fHits[i] = 0;
		fStray[i] = 0;
	}

	if (!super::init()) {
		return false;
	}
	if (!gicd || !gicdSize || !gicr || !gicrSize) {
		return false;
	}

	fGICD = gicd;
	fGICDSize = gicdSize;
	fGICR = gicr;
	fGICRSize = gicrSize;

	vectors = IONewZero(IOInterruptVector, GIC_VECTOR_COUNT);
	if (!vectors) {
		return false;
	}

	controllerLock = IOSimpleLockAlloc();
	if (!controllerLock) {
		IODelete(vectors, IOInterruptVector, GIC_VECTOR_COUNT);
		vectors = NULL;
		return false;
	}

	return true;
}

void
GenericInterruptController::free(void)
{
	if (vectors) {
		for (unsigned int i = 0; i < GIC_VECTOR_COUNT; i++) {
			if (vectors[i].interruptLock) {
				IOLockFree(vectors[i].interruptLock);
				vectors[i].interruptLock = NULL;
			}
		}
		IODelete(vectors, IOInterruptVector, GIC_VECTOR_COUNT);
		vectors = NULL;
	}
	if (controllerLock) {
		IOSimpleLockFree(controllerLock);
		controllerLock = NULL;
	}

	super::free();
}

IOReturn
GenericInterruptController::bringUp(void)
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
	 * With DS==0 the non-secure view has EnableGrp1 (bit 0) as RES0, so the
	 * readback is 0x12 (ARE_NS | ENABLEGRP1A) and not 0x13.
	 */
	ctlr = gicdRead32(GICD_CTLR);
	wanted = ctlr | GICD_CTLR_ARE_NS | GICD_CTLR_ENABLEGRP1A |
	    GICD_CTLR_ENABLEGRP1;
	if (wanted != ctlr) {
		gicdWrite32(GICD_CTLR, wanted);
		(void)waitForRWP();
		ctlr = gicdRead32(GICD_CTLR);
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
	    ctlr, typer, fMaxIntID, (uint32_t)GIC_VECTOR_COUNT,
	    (unsigned long)fGICR, (unsigned long)fGICRSize);

	return kIOReturnSuccess;
}

GenericInterruptController *
GenericInterruptController::controller(void)
{
	return gIC;
}

void
GenericInterruptController::setController(GenericInterruptController *controller)
{
	gIC = controller;
}

/* ------------------------------------------------------------- C entry */

extern "C" int
gic_install(void)
{
	uint64_t gicd;
	uint64_t gicdSize;
	uint64_t gicr;
	uint64_t gicrSize;
	GenericInterruptController *gic;
	OSSymbol *name;

	if (gIC) {
		return 1;
	}

	gicd = pe_gic_distributor_base();
	gicdSize = pe_gic_distributor_size();
	gicr = pe_gic_redistributor_base();
	gicrSize = pe_gic_redistributor_size();

	if (!gicd || !gicdSize || !gicr || !gicrSize) {
		return -1;
	}

	gic = GenericInterruptController::withBases((vm_offset_t)gicd,
	    (vm_size_t)gicdSize, (vm_offset_t)gicr, (vm_size_t)gicrSize);
	if (!gic) {
		return -2;
	}

	if (gic->bringUp() != kIOReturnSuccess) {
		gic->release();
		return -3;
	}

	/*
	 * ADT preparation: publish the controller under the name the ADT GIC
	 * node will carry, so a device's interrupt-parent can resolve to it
	 * through IOPlatformExpert::lookUpInterruptController(). Harmless while
	 * nothing in the tree names it.
	 */
	name = (OSSymbol *)OSSymbol::withCStringNoCopy(GIC_ADT_CONTROLLER_NAME);
	if (name && IOService::getPlatform() &&
	    IOService::getPlatform()->registerInterruptController(name, gic) ==
	    kIOReturnSuccess) {
		SWEET_LOG_INFO("SWEET GIC: registered as \"%s\" for ADT lookup\n",
		    GIC_ADT_CONTROLLER_NAME);
	} else {
		SWEET_LOG_ERROR("SWEET GIC: platform registration failed; "
		    "ADT lookup will not resolve\n");
	}
	if (name) {
		name->release();
	}

	GenericInterruptController::setController(gic);

	return 0;
}

extern "C" int
gic_installed(void)
{
	return gIC ? 1 : 0;
}

extern "C" int
gic_handle_irq(uint64_t iar)
{
	if (!gIC) {
		return 0;
	}

	return gIC->handleInterruptIAR(iar) ? 1 : 0;
}

extern "C" void
gic_set_stray_policy(int mask)
{
	if (gIC) {
		gIC->setMaskStraySPIs(mask != 0);
	}
}

extern "C" void
gic_log_enabled(const char *tag)
{
	if (gIC) {
		gIC->logEnabledSPIs(tag);
	}
}

extern "C" int
gic_register_irq(int intid, void *target, gic_handler_t handler, void *refCon)
{
	IOInterruptHandler interruptHandler = handler;

	if (!gIC) {
		return kIOReturnNotReady;
	}

	return gIC->registerInterrupt(NULL, intid, target, interruptHandler,
	    refCon);
}

extern "C" int
gic_unregister_irq(int intid)
{
	if (!gIC) {
		return kIOReturnNotReady;
	}

	return gIC->unregisterInterrupt(NULL, intid);
}

extern "C" int
gic_enable_irq(int intid)
{
	if (!gIC) {
		return kIOReturnNotReady;
	}

	return gIC->enableInterrupt(NULL, intid);
}

extern "C" int
gic_disable_irq(int intid)
{
	if (!gIC) {
		return kIOReturnNotReady;
	}

	return gIC->disableInterrupt(NULL, intid);
}

extern "C" int
gic_cause_irq(int intid)
{
	if (!gIC) {
		return kIOReturnNotReady;
	}

	return gIC->causeInterrupt(NULL, intid);
}

extern "C" uint64_t
gic_hits(int intid)
{
	if (!gIC) {
		return 0;
	}

	return gIC->getHits(intid);
}

#endif /* SWEET */
