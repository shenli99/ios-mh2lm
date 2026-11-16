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
 * The class follows the standard IOInterruptController layout:
 *
 *   start(provider)        IOService lifecycle: probe TYPER, program DL_CTLR,
 *                          publish under the ADT phandle name
 *   resolveInterrupt()     nub side: fills IOInterruptSources so the base
 *                          class owns the vector number (cell 0)
 *   registerInterrupt()    validates, then calls super::registerInterrupt;
 *                          the base class owns the vector table, the
 *                          per-vector lock and the registered/disabled state
 *   initVector() etc.      hardware only: group/priority/trigger/route,
 *                          enable/disable/cause for one vector
 *   getInterruptHandlerAddress() / handleInterrupt()
 *                          the standard action sleh_irq() dispatches through;
 *                          it never writes ICC_EOIR1_EL1
 *
 * Creation is ADT-driven: gic_install() looks up the controller's own node
 * (/arm-io/gic), reads reg, AAPL,phandle and #interrupt-cells, maps the
 * windows and hands them to start(). The platform expert's pe_gic_*() windows,
 * mapped from the same node before BSD existed, are only a fallback.
 *
 * Registration is the stock path: the device node carries the standard
 * 3-cell specifier in its IOInterruptSpecifiers property, resolveInterrupt()
 * records the parsed spec in the controller's dictionary, and
 * IOInterruptController keeps the vector number, the vector table and the
 * per-vector lock.
 *
 * Rollout is staged: the legacy predicate chain runs first in sleh_irq() and
 * only unclaimed INTIDs reach this controller. While that is the case the
 * stray policy is "count and complete", not "mask", so an SPI nobody has
 * registered yet behaves exactly as it did before; mask the strays once every
 * live SPI is registered.
 */

#include "GenericInterruptController.h"

#include <IOKit/IOLib.h>
#include <mach/vm_param.h>
#include <vm/memory_types.h>
#include <stdint.h>

#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"

extern "C" vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, unsigned int);
extern "C" void ml_io_unmap(vm_offset_t, vm_size_t);

#if defined(SWEET)

#define super IOInterruptController

OSDefineMetaClassAndStructors(GenericInterruptController, IOInterruptController);

/* Vector table is indexed by INTID; 1020 entries cover SPIs and the specials
 * that are never registered, so vectorNumber == INTID and the internal
 * specifier carries the INTID in its first cell. */
#define GIC_VECTOR_COUNT GIC_INTID_SPECIAL_FIRST

static GenericInterruptController *gIC;

/* ------------------------------------------------- ADT node description */

struct GicAdtNode {
	uint64_t gicdPhys;
	uint64_t gicdSize;
	uint64_t gicrPhys;
	uint64_t gicrSize;
	uint32_t phandle;
	uint32_t interruptCells;
};

/*
 * Read the controller's own description out of the ADT. pe_fiq() parses the
 * same node for the very early vtimer setup (it has to map the GIC before BSD
 * exists); here the controller owns the values.
 *
 * reg is four u64 cells relative to the /arm-io ranges parent (SOC base):
 * gicd offset, gicd size, gicr offset, gicr size. The phandle is the name
 * clients will resolve through (IODTInterruptControllerName), and
 * #interrupt-cells is what their "interrupts" arrays will carry.
 */
static bool
gicAdtLookup(GicAdtNode *node)
{
	IORegistryEntry *armIo;
	IORegistryEntry *gic;
	OSData *ranges;
	OSData *reg;
	OSData *value;
	const uint64_t *cells;
	uint64_t socBase;

	bzero(node, sizeof(*node));
	node->interruptCells = GIC_ADT_CELLS;

	gic = IORegistryEntry::fromPath(GIC_ADT_NODE_PATH, gIODTPlane);
	if (!gic) {
		return false;
	}
	armIo = IORegistryEntry::fromPath("/arm-io", gIODTPlane);
	reg = OSDynamicCast(OSData, gic->getProperty("reg"));
	ranges = armIo ? OSDynamicCast(OSData, armIo->getProperty("ranges")) : NULL;
	if (!reg || reg->getLength() < 4 * sizeof(uint64_t) ||
	    !ranges || ranges->getLength() < 3 * sizeof(uint64_t)) {
		if (armIo) {
			armIo->release();
		}
		gic->release();
		return false;
	}

	/* /arm-io ranges: child address, parent address, size. */
	socBase = ((const uint64_t *)ranges->getBytesNoCopy())[1];
	cells = (const uint64_t *)reg->getBytesNoCopy();
	node->gicdPhys = socBase + cells[0];
	node->gicdSize = cells[1];
	node->gicrPhys = socBase + cells[2];
	node->gicrSize = cells[3];

	/* A dedicated property keeps the redistributor description independent
	 * of the reg cell order; reg cells 2/3 remain the fallback. */
	value = OSDynamicCast(OSData, gic->getProperty("redistributor"));
	if (value && value->getLength() >= 2 * sizeof(uint64_t)) {
		const uint64_t *rd = (const uint64_t *)value->getBytesNoCopy();

		node->gicrPhys = socBase + rd[0];
		node->gicrSize = rd[1];
	}

	value = OSDynamicCast(OSData, gic->getProperty("AAPL,phandle"));
	if (value && value->getLength() == sizeof(uint32_t)) {
		node->phandle = *(const uint32_t *)value->getBytesNoCopy();
	}
	value = OSDynamicCast(OSData, gic->getProperty("#interrupt-cells"));
	if (value && value->getLength() == sizeof(uint32_t)) {
		node->interruptCells = *(const uint32_t *)value->getBytesNoCopy();
	}

	if (armIo) {
		armIo->release();
	}
	gic->release();

	return node->gicdSize != 0 && node->gicrSize != 0;
}

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
 * start(), where the spec wants the change acknowledged before anything else
 * is configured. Nothing else waits on it. It is a distributor-wide stall,
 * and the enable/disable writes that drivers issue from interrupt context
 * must not spin on it.
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


/*
 * Record the 3-cell specifier <INTID TYPE FLAGS> for one INTID.
 * The dictionary is ours (no nub->_interruptSources access); controllerLock
 * protects its lazy creation together with the single-cell vector-number
 * table the base class reads.
 */
/*
 * Record the 3-cell specifier <INTID TYPE FLAGS> for one INTID in
 * the controller's own dictionary. The base class' IOInterruptSources array is
 * not touched: the vector number it needs comes from the nub's own source
 * table, which IOService::resolveInterrupt() already filled.
 */
IOReturn
GenericInterruptController::ensureSpec(uint32_t intid, uint32_t type,
    uint32_t flags)
{
	uint32_t cells[GIC_ADT_CELLS];
	OSData *specData;

	if (intid < GIC_INTID_SPI_FIRST || intid > (uint32_t)fMaxIntID) {
		return kIOReturnNoInterrupt;
	}

	IOSimpleLockLock(controllerLock);
	if (!fSpecData[intid]) {
		cells[0] = intid;
		cells[1] = type;
		cells[2] = flags;
		specData = OSData::withBytes(cells, sizeof(cells));
		if (!specData) {
			IOSimpleLockUnlock(controllerLock);
			return kIOReturnNoMemory;
		}
		fSpecData[intid] = specData;
	}
	IOSimpleLockUnlock(controllerLock);

	return kIOReturnSuccess;
}

/*
 * Read a device node's standard 3-cell specifier from its public
 * IOInterruptSpecifiers property. No private IOService state is touched.
 */
IOReturn
GenericInterruptController::specFromNubProperties(IOService *nub, int source,
    SourceSpec *spec) const
{
	OSArray *specifiers;
	OSData *data;
	const uint32_t *cells;

	if (!nub || !spec) {
		return kIOReturnBadArgument;
	}
	if (source < 0) {
		return kIOReturnBadArgument;
	}

	specifiers = OSDynamicCast(OSArray,
	    nub->getProperty(gIOInterruptSpecifiersKey));
	if (!specifiers) {
		return kIOReturnNoResources;
	}
	data = OSDynamicCast(OSData, specifiers->getObject(source));
	if (!data || data->getLength() != GIC_ADT_CELLS * sizeof(uint32_t)) {
		return kIOReturnBadArgument;
	}

	cells = (const uint32_t *)data->getBytesNoCopy();
	spec->intid = cells[0];
	spec->type = cells[1];
	spec->flags = cells[2];
	if (spec->type != GIC_ADT_TYPE_SPI && spec->type != GIC_ADT_TYPE_PPI) {
		return kIOReturnBadArgument;
	}
	if (spec->intid > (uint32_t)fMaxIntID) {
		return kIOReturnNoInterrupt;
	}

	return kIOReturnSuccess;
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
 * Routing to another PE comes with the topology work, if it is ever needed.
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

/*
 * The internal specifier handed to IOInterruptController is three cells
 * <INTID FLAGS TYPE>: the first cell is the vector number the base class
 * resolves, the other two are what the hooks need. The ADT-facing specifier a
 * device node carries is <INTID TYPE FLAGS>; resolveSpec()
 * translates between the two.
 */

/* ------------------------------------------------------- nub side */

/*
 * Fill IOInterruptSources for the controller acting as its own nub: one
 * source per INTID, specifier in the internal three-cell form. The base class
 * then owns the vector number (cell 0), the vector table entry and its lock,
 * and the registered/enabled state.
 *
 * A real ADT-backed nub is not handled here: its base-class
 * resolveInterrupt() resolves the controller from the device tree, and
 * registerInterrupt() translates its standard three-cell specifier before
 * handing it to the base class.
 */
/*
 * Called from registerInterrupt() before the base class runs. A device node
 * contributes only its public IOInterruptSpecifiers property (the standard
 * 3-cell specifier); the parsed spec lands in the controller's dictionary.
 * The base class keeps its own IOInterruptSources table for the vector number.
 *
 * In kernel-private builds IOService::resolveInterrupt() is not virtual
 * (APPLE_KEXT_COMPATIBILITY_VIRTUAL is empty), so this is a same-signature
 * method the controller calls itself.
 */
IOReturn
GenericInterruptController::resolveInterrupt(IOService *nub, int source)
{
	SourceSpec spec;
	IOReturn rc;

	if (!fReady) {
		return kIOReturnNotReady;
	}
	if (!nub || nub == this) {
		return kIOReturnBadArgument;
	}

	rc = specFromNubProperties(nub, source, &spec);
	if (rc != kIOReturnSuccess) {
		return rc;
	}
	rc = ensureSpec(spec.intid, spec.type, spec.flags);
	if (rc != kIOReturnSuccess) {
		return rc;
	}
	fSpecNub[spec.intid] = nub;

	return kIOReturnSuccess;
}

/* ------------------------------------------------------- spec resolution */

/*
 * Translate a source into the hardware properties this controller needs.
 *
 * nub == this   the internal three-cell specifier created by
 *               resolveInterrupt(), cell 0 is already the INTID.
 * real nub      the device's ADT specifier, standard three-cell
 *               <INTID TYPE FLAGS>; cell 0 is the vector number.
 */
IOReturn
GenericInterruptController::resolveSpec(int vectorNumber, SourceSpec *spec)
{
	OSData *data;
	const uint32_t *cells;

	if (!spec) {
		return kIOReturnBadArgument;
	}
	if (vectorNumber < GIC_INTID_SPI_FIRST ||
	    vectorNumber >= (int)GIC_VECTOR_COUNT) {
		return kIOReturnNoInterrupt;
	}

	data = fSpecData[vectorNumber];
	if (!data || data->getLength() < GIC_ADT_CELLS * sizeof(uint32_t)) {
		return kIOReturnNoInterrupt;
	}

	cells = (const uint32_t *)data->getBytesNoCopy();
	spec->intid = cells[0];
	spec->type = cells[1];
	spec->flags = cells[2];
	if (spec->type != GIC_ADT_TYPE_SPI && spec->type != GIC_ADT_TYPE_PPI) {
		return kIOReturnBadArgument;
	}

	return kIOReturnSuccess;
}


void
GenericInterruptController::programSpec(int intid, const SourceSpec *spec)
{
	bool level = !(spec->flags & GIC_ADT_FLAG_EDGE_RISING) &&
	    !(spec->flags & GIC_ADT_FLAG_EDGE_FALLING) &&
	    (spec->flags != GIC_ADT_FLAG_EDGE_BOTH);

	writeGroup(intid, true);   /* Group 1: every NS interrupt arrives there */
	writePriority(intid, (uint8_t)GIC_SPEC_DEFAULT_PRIORITY);
	writeTrigger(intid, level);
	writeRoute(intid, routeForTarget(0));
}

/*
 * An SPI nobody registered. Counted and completed by the platform, and masked
 * only when the stray policy says so: during the staged rollout the legacy
 * chain still owns most INTIDs, so masking here would silently kill a device
 * that simply has not migrated yet.
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
	/*
	 * SPIs may be shared: IOInterruptController then routes the extra
	 * consumers through an IOSharedInterruptController that sits on our
	 * vector. Nothing shares an INTID today, but the vector must say so
	 * before the first device that does.
	 */
	return true;
}

/*
 * Called by the base class with the source still masked; the driver decides
 * when it becomes visible through enableInterrupt().
 */
void
GenericInterruptController::initVector(IOInterruptVectorNumber vectorNumber,
    IOInterruptVector *vector)
{
	SourceSpec spec;

	(void)vector;

	if (resolveSpec((int)vectorNumber, &spec) != kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET GIC: cannot resolve intid %d; left masked\n",
		    (int)vectorNumber);
		return;
	}

	maskSPI((int)vectorNumber);
	programSpec((int)vectorNumber, &spec);
	/* Group/priority/trigger/route must be visible before any later
	 * enableVector() for this source. */
	synchronizeIO();
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

/*
 * The base class owns the vector table, the per-vector lock and the
 * registered/enabled flags; this override only validates the source, makes
 * sure the specifier the base class will read starts with the vector number,
 * and allocates the per-vector lock it needs.
 */
/*
 * Everything the base class needs is in the controller's own dictionary: a
 * single-cell vector-number entry for the vector table and the standard
 * 3-cell specifier for the hardware. A real device node contributes only its
 * public IOInterruptSpecifiers property.
 */
/*
 * Soft-disable (base class state) plus an immediate distributor mask: the
 * drivers on this platform mask and then read ISENABLER back (touch quiesce
 * proves the source is quiet before calling a panel transition done), which
 * the deferred base-class model cannot guarantee when the level source is no
 * longer asserted and no further interrupt would arrive to trigger the mask.
 */
IOReturn
GenericInterruptController::disableInterrupt(IOService *nub, int source)
{
	SourceSpec spec;
	IOReturn rc;

	if (!fReady) {
		return kIOReturnNotReady;
	}
	if (!nub) {
		return kIOReturnBadArgument;
	}

	rc = super::disableInterrupt(nub, source);
	if (rc != kIOReturnSuccess) {
		return rc;
	}

	/* Only public specifier data: cell 0 is the vector number. */
	if (specFromNubProperties(nub, source, &spec) != kIOReturnSuccess) {
		return kIOReturnSuccess;
	}
	if (spec.intid < GIC_INTID_SPI_FIRST || (int)spec.intid > fMaxIntID) {
		return kIOReturnSuccess;
	}

	vectors[spec.intid].interruptDisabledHard = 1;
	disableVectorHard(spec.intid, &vectors[spec.intid]);

	return kIOReturnSuccess;
}

/*
 * Standard path: the nub resolved its controller and specifier through
 * IOService::lookupInterrupt(), our resolveInterrupt() recorded the hardware
 * spec, and IOInterruptController owns the vector table, the per-vector lock
 * and the registered/disabled state.
 */
IOReturn
GenericInterruptController::registerInterrupt(IOService *nub, int source,
    void *target, IOInterruptHandler handler, void *refCon)
{
	IOReturn rc;

	if (!fReady) {
		return kIOReturnNotReady;
	}
	if (!nub || !handler) {
		return kIOReturnBadArgument;
	}

	rc = resolveInterrupt(nub, source);
	if (rc != kIOReturnSuccess) {
		return rc;
	}

	return super::registerInterrupt(nub, source, target, handler, refCon);
}




/*
 * The base class only clears the soft flag here and lets the next dispatch
 * hard-disable the vector. That is the standard XNU model and it works for a
 * level source: the interrupt that is still asserted is re-delivered after
 * EOI, dispatch sees interruptDisabledSoft, masks the distributor and does not
 * run the handler again.
 */


/* ------------------------------------------------------------ dispatch */

bool
GenericInterruptController::dispatch(uint32_t intid)
{
	IOInterruptVector *vector = vectorAt((int)intid);
	IOInterruptHandler handler;
	void *target;
	void *refCon;
	void *nub;

	if (intid >= (uint32_t)GIC_INTID_SPI_FIRST) {
		__atomic_add_fetch(&fStats.interrupts, 1u, __ATOMIC_RELAXED);
	}

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
		/* The driver asked for quiet from its top half; mask here, the
		 * way the base class' dispatch path does. */
		if (!vector->interruptDisabledHard) {
			vector->interruptDisabledHard = 1;
			maskSPI((int)intid);
		}
		return false;
	}

	target = vector->target;
	refCon = vector->refCon;
	nub = fSpecNub[intid] ? fSpecNub[intid] : vector->nub;

	__atomic_add_fetch(&fHits[intid], 1ull, __ATOMIC_RELAXED);
	__atomic_add_fetch(&fStats.handled, 1u, __ATOMIC_RELAXED);

	vector->interruptActive = 1;
	handler(target, refCon, nub, vector->source);
	vector->interruptActive = 0;

	return true;
}

/*
 * The standard IOInterruptAction. It dispatches one source (the vector
 * number; for SPIs that is the INTID) and never completes the interrupt:
 * sleh_irq() wrote ICC_IAR1_EL1 before calling, so sleh_irq() owns the EOI.
 */
IOReturn
GenericInterruptController::handleInterrupt(void *refCon, IOService *nub,
    int source)
{
	(void)refCon;
	(void)nub;

	if (source < GIC_INTID_SPI_FIRST ||
	    source >= (int)GIC_INTID_SPECIAL_FIRST) {
		return kIOReturnInvalid;
	}

	return dispatch((uint32_t)source) ? kIOReturnSuccess :
	       kIOReturnInvalid;
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
	fAdtPhandle = 0;
	fInterruptCells = GIC_ADT_CELLS;
	fStats.interrupts = 0;
	fStats.handled = 0;
	fStats.spurious = 0;
	fStats.stray = 0;
	fSpecData = NULL;
	fSpecNub = NULL;
	fHits = NULL;
	fStray = NULL;

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
	fSpecData = IONewZero(OSData *, GIC_VECTOR_COUNT);
	fSpecNub = IONewZero(IOService *, GIC_VECTOR_COUNT);
	fHits = IONewZero(uint64_t, GIC_VECTOR_COUNT);
	fStray = IONewZero(uint32_t, GIC_VECTOR_COUNT);
	controllerLock = IOSimpleLockAlloc();
	if (!vectors || !fSpecData || !fSpecNub || !fHits || !fStray ||
	    !controllerLock) {
		if (vectors) {
			IODelete(vectors, IOInterruptVector, GIC_VECTOR_COUNT);
			vectors = NULL;
		}
		if (fSpecData) {
			IODelete(fSpecData, OSData *, GIC_VECTOR_COUNT);
			fSpecData = NULL;
		}
		if (fSpecNub) {
			IODelete(fSpecNub, IOService *, GIC_VECTOR_COUNT);
			fSpecNub = NULL;
		}
		if (fHits) {
			IODelete(fHits, uint64_t, GIC_VECTOR_COUNT);
			fHits = NULL;
		}
		if (fStray) {
			IODelete(fStray, uint32_t, GIC_VECTOR_COUNT);
			fStray = NULL;
		}
		if (controllerLock) {
			IOSimpleLockFree(controllerLock);
			controllerLock = NULL;
		}
		return false;
	}

	return true;
}

void
GenericInterruptController::free(void)
{
	for (unsigned int i = 0; i < GIC_VECTOR_COUNT; i++) {
		if (fSpecData && fSpecData[i]) {
			fSpecData[i]->release();
			fSpecData[i] = NULL;
		}
		if (fSpecNub) {
			fSpecNub[i] = NULL;
		}
	}
	if (fSpecData) {
		IODelete(fSpecData, OSData *, GIC_VECTOR_COUNT);
		fSpecData = NULL;
	}
	if (fSpecNub) {
		IODelete(fSpecNub, IOService *, GIC_VECTOR_COUNT);
		fSpecNub = NULL;
	}
	if (fHits) {
		IODelete(fHits, uint64_t, GIC_VECTOR_COUNT);
		fHits = NULL;
	}
	if (fStray) {
		IODelete(fStray, uint32_t, GIC_VECTOR_COUNT);
		fStray = NULL;
	}
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

/*
 * Map the two windows described by the controller's ADT node. Called from
 * start() when the controller was matched as an IOPlatformDevice (the nub
 * carries the same properties as the node it was built from).
 */
bool
GenericInterruptController::mapFromProvider(IOService *provider)
{
	GicAdtNode node;
	OSData *value;

	if (!gicAdtLookup(&node)) {
		return false;
	}

	if (provider) {
		/* The nub is authoritative for its own identity properties. */
		value = OSDynamicCast(OSData, provider->getProperty("AAPL,phandle"));
		if (value && value->getLength() == sizeof(uint32_t)) {
			node.phandle = *(const uint32_t *)value->getBytesNoCopy();
		}
		value = OSDynamicCast(OSData, provider->getProperty("#interrupt-cells"));
		if (value && value->getLength() == sizeof(uint32_t)) {
			node.interruptCells = *(const uint32_t *)value->getBytesNoCopy();
		}
	}

	fGICDSize = (vm_size_t)node.gicdSize;
	fGICRSize = (vm_size_t)node.gicrSize;
	fGICD = ml_io_map_unmappable((vm_offset_t)node.gicdPhys, fGICDSize,
	    VM_WIMG_IO);
	fGICR = fGICD ? ml_io_map_unmappable((vm_offset_t)node.gicrPhys,
	    fGICRSize, VM_WIMG_IO) : 0;
	if (!fGICD || !fGICR) {
		if (fGICD) {
			ml_io_unmap(fGICD, fGICDSize);
		}
		fGICD = 0;
		fGICR = 0;
		fGICDSize = 0;
		fGICRSize = 0;
		return false;
	}

	fAdtPhandle = node.phandle;
	fInterruptCells = node.interruptCells ? node.interruptCells :
	    GIC_ADT_CELLS;

	return true;
}

/*
 * Probe TYPER, program GICD_CTLR and make the object usable. Called from
 * start(), i.e. inside the IOService lifecycle.
 */
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
	 * controller self-sufficient if it is started on a different path.
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

bool
GenericInterruptController::start(IOService *provider)
{
	char nameBuf[40];
	OSSymbol *name;

	if (!super::start(provider)) {
		return false;
	}

	if (!fGICD) {
		if (!mapFromProvider(provider)) {
			SWEET_LOG_ERROR("SWEET GIC: start without GIC windows "
			    "(provider %s)\n",
			    provider ? provider->getName() : "none");
			return false;
		}
	}

	if (bringUp() != kIOReturnSuccess) {
		return false;
	}
	setController(this);

	/*
	 * Register under the name XNU derives from the node's phandle
	 * (IODTInterruptControllerName): that is exactly what a device's
	 * interrupt-parent will resolve through once the ADT carries GIC
	 * specifiers.
	 */
	if (fAdtPhandle != 0) {
		snprintf(nameBuf, sizeof(nameBuf), "%s%08X",
		    GIC_ADT_CONTROLLER_PREFIX, fAdtPhandle);
		name = (OSSymbol *)OSSymbol::withCStringNoCopy(nameBuf);
		if (name) {
			if (IOService::getPlatform() &&
			    IOService::getPlatform()->registerInterruptController(
			    name, this) == kIOReturnSuccess) {
				SWEET_LOG_INFO("SWEET GIC: registered as \"%s\" "
				    "for ADT lookup\n", nameBuf);
			} else {
				SWEET_LOG_ERROR("SWEET GIC: platform registration "
				    "failed for \"%s\"\n", nameBuf);
			}
			name->release();
		}
	} else {
		SWEET_LOG_ERROR("SWEET GIC: no ADT phandle; not registered for "
		    "interrupt-parent lookup\n");
	}

	registerService();

	return true;
}

void
GenericInterruptController::setAdtInfo(uint32_t phandle,
    uint32_t interruptCells)
{
	fAdtPhandle = phandle;
	fInterruptCells = interruptCells ? interruptCells : GIC_ADT_CELLS;
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
	/*
	 * The controller is created and started by IOService matching on its ADT
	 * node (personality 'arm,gic-v3'). This entry only reports whether that
	 * happened; there is no manual construction path any more.
	 */
	if (gIC) {
		return 1;
	}

	SWEET_LOG_ERROR("SWEET GIC: no controller instance; the ADT 'gic' node "
	    "was not matched\n");

	return -1;
}

extern "C" int
gic_installed(void)
{
	return gIC ? 1 : 0;
}

extern "C" void
gic_dispatch(uint32_t intid)
{
	if (gIC) {
		(void)gIC->dispatch(intid);
	}
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


#endif /* SWEET */
