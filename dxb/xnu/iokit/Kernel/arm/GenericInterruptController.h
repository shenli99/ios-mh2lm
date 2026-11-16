/*
 * GenericInterruptController: the GICv3 distributor owner for the sweet
 * platform, implemented as the IOInterruptController subclass it should be.
 *
 * Scope is deliberately SPIs only (INTID 32..1019). SGIs and PPIs are per-CPU
 * state, the platform already drives them (vtimer PPI and the IPI SGI in
 * sleh_irq), and a controller that owned one of them would have to grow a
 * per-CPU table for a feature nothing needs yet. The vector table is therefore
 * indexed by INTID and only INTIDs >= 32 are ever registered.
 *
 * The hardware side lives in the simplified-subclass hooks the base class
 * calls (initVector, enableVector, disableVectorHard, causeVector,
 * getVectorType); the source side is resolved by resolveSpec(), which today
 * reads a hardcoded per-INTID table and later will parse the ADT interrupt
 * specifier (see generic_interrupt_controller.h for the cell layout). The
 * registration entry points accept both worlds:
 *
 *   - hardcoded mode: the caller passes the INTID as `source` with a NULL nub
 *     (gic_register_irq()), so no ADT exists yet;
 *   - ADT mode: a nub is passed and the base class resolves the vector number
 *     through IOInterruptSpecifiers, with initVector() parsing the specifier.
 *
 * EOI is written exactly once per interrupt and only by this controller, for
 * the INTIDs it completed: sleh_irq keeps the legacy paths (timer, IPIs,
 * drivers that have not migrated) and must not EOI what gic_handle_irq()
 * reported as completed.
 */

#ifndef _IOKIT_GENERICINTERRUPTCONTROLLER_H
#define _IOKIT_GENERICINTERRUPTCONTROLLER_H

#include <IOKit/IOInterruptController.h>   /* IOInterruptVector, base class */
#include <IOKit/IOLocks.h>
#include <IOKit/IOPlatformExpert.h>        /* getPlatform(), controller registry */
#include <libkern/c++/OSObject.h>
#include <mach/vm_types.h>
#include <pexpert/arm64/board_config.h>
#include <pexpert/arm64/generic_interrupt_controller.h>
#include <stdint.h>

#if defined(SWEET)

class GenericInterruptController : public IOInterruptController
{
	OSDeclareDefaultStructors(GenericInterruptController);

public:
	static GenericInterruptController *withBases(vm_offset_t gicd,
	    vm_size_t gicdSize, vm_offset_t gicr, vm_size_t gicrSize);

	/*
	 * A controller has no provider on this port: pe_fiq() maps the GIC
	 * before bsd_init and publishes the windows through pe_gic_*(), so the
	 * board bring-up constructs the object directly instead of matching a
	 * device-tree node.
	 */
	bool initWithBases(vm_offset_t gicd, vm_size_t gicdSize,
	    vm_offset_t gicr, vm_size_t gicrSize);
	void free(void) APPLE_KEXT_OVERRIDE;

	/* Probe TYPER, program GICD_CTLR and make the object usable. */
	IOReturn bringUp(void);

	/* IOInterruptController overrides. `source` is the INTID in hardcoded
	 * mode (nub == NULL); with a nub the base class ADT path is used. */
	IOReturn registerInterrupt(IOService *nub, int source, void *target,
	    IOInterruptHandler handler, void *refCon) APPLE_KEXT_OVERRIDE;
	IOReturn unregisterInterrupt(IOService *nub, int source)
	    APPLE_KEXT_OVERRIDE;
	IOReturn getInterruptType(IOService *nub, int source, int *interruptType)
	    APPLE_KEXT_OVERRIDE;
	IOReturn enableInterrupt(IOService *nub, int source) APPLE_KEXT_OVERRIDE;
	IOReturn disableInterrupt(IOService *nub, int source) APPLE_KEXT_OVERRIDE;
	IOReturn causeInterrupt(IOService *nub, int source) APPLE_KEXT_OVERRIDE;
	IOInterruptAction getInterruptHandlerAddress(void) APPLE_KEXT_OVERRIDE;
	IOReturn handleInterrupt(void *refCon, IOService *nub, int source)
	    APPLE_KEXT_OVERRIDE;

	/* Simplified-subclass hooks: the hardware side of one vector. */
	bool vectorCanBeShared(IOInterruptVectorNumber vectorNumber,
	    IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	void initVector(IOInterruptVectorNumber vectorNumber,
	    IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	int getVectorType(IOInterruptVectorNumber vectorNumber,
	    IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	void disableVectorHard(IOInterruptVectorNumber vectorNumber,
	    IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	void enableVector(IOInterruptVectorNumber vectorNumber,
	    IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	void causeVector(IOInterruptVectorNumber vectorNumber,
	    IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;

	/* Platform IRQ entry: ICC_IAR1_EL1 was already read by the caller.
	 * Dispatches the SPI and completes it; returns true when it wrote
	 * ICC_EOIR1_EL1 (registered source, or unclaimed SPI that was only
	 * counted). SGIs/PPIs and spurious INTIDs return false: the caller
	 * still owns the completion. */
	bool handleInterruptIAR(uint64_t iar);

	/* Translate a registration into hardware properties. Hardcoded table
	 * today; the ADT specifier will be parsed here once it exists. */
	IOReturn resolveSpec(IOService *nub, int source,
	    gic_interrupt_spec *spec);

	/* Unregistered SPIs are counted and completed; masking them is opt-in
	 * so a staged migration can keep the legacy chain in front of the
	 * controller until every live SPI has registered. */
	void setMaskStraySPIs(bool mask);

	uint32_t acknowledge(void);
	bool dispatch(uint32_t intid);
	void endOfInterrupt(uint64_t iar);

	/* Read-only inventory of the enabled SPIs, used as the migration list
	 * while the legacy chain still claims the sources. */
	void logEnabledSPIs(const char *tag);

	int getMaxInterrupt(void) const { return fMaxIntID; }
	bool isRegistered(int source) const;
	bool isEnabled(int source) const;
	uint64_t getHits(int source) const;

	struct Statistics {
		uint64_t interrupts;
		uint64_t handled;
		uint64_t spurious;
		uint64_t stray;
		uint64_t endOfInterrupts;
	};
	void getStatistics(Statistics *stats) const;

	static GenericInterruptController *controller(void);
	static void setController(GenericInterruptController *controller);

private:
	IOReturn resolveSpecADT(IOService *nub, int source,
	    gic_interrupt_spec *spec) const;
	IOReturn resolveSpecHardcoded(int source, gic_interrupt_spec *spec) const;

	IOInterruptVector *vectorAt(int intid) const;
	int vectorNumberForIntid(int intid) const { return intid; }
	int intidForSpec(const gic_interrupt_spec *spec) const;
	const gic_interrupt_spec *hardcodedSpec(int intid) const;

	uint32_t gicdRead32(uint32_t offset) const;
	void gicdWrite32(uint32_t offset, uint32_t value);
	void gicdWrite64(uint32_t offset, uint64_t value);

	bool waitForRWP(void);
	void synchronizeIO(void);

	void programSpec(int intid, const gic_interrupt_spec *spec);
	void writeGroup(int intid, bool group1);
	void writePriority(int intid, uint8_t priority);
	void writeTrigger(int intid, bool level);
	bool triggerIsLevel(int intid) const;
	void writeRoute(int intid, uint64_t affinity);
	uint64_t routeForTarget(uint32_t target) const;
	void maskSPI(int intid);
	void unmaskSPI(int intid);
	void straySPI(int intid);

	vm_offset_t           fGICD;
	vm_size_t             fGICDSize;
	vm_offset_t           fGICR;
	vm_size_t             fGICRSize;
	int                   fMaxIntID;
	bool                  fReady;
	bool                  fMaskStraySPIs;
	uint64_t              fHits[GIC_INTID_SPECIAL_FIRST];
	uint32_t              fStray[GIC_INTID_SPECIAL_FIRST];
	Statistics            fStats;
};

#endif /* SWEET */

#endif /* ! _IOKIT_GENERICINTERRUPTCONTROLLER_H */
