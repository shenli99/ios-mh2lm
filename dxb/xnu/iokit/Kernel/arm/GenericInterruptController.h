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
 * getVectorType); the source side is resolved by resolveSpec() from the
 * controller's own specifier dictionary. Registration is the stock path: a
 * device node's specifier comes from its public IOInterruptSpecifiers
 * property, and the base class owns the vector number, the vector table and
 * the per-vector lock.
 *
 * EOI is written exactly once per interrupt, by the layer that acknowledged
 * it: sleh_irq writes ICC_IAR1_EL1 and completes every INTID it dispatches
 * through the controller's standard action, so neither the controller nor the
 * handler writes ICC_EOIR1_EL1.
 */

#ifndef _IOKIT_GENERICINTERRUPTCONTROLLER_H
#define _IOKIT_GENERICINTERRUPTCONTROLLER_H

#include <IOKit/IOInterruptController.h>   /* IOInterruptVector, base class */
#include <IOKit/IOLocks.h>
#include <IOKit/IOPlatformExpert.h>        /* getPlatform(), controller registry */
#include <IOKit/IODeviceTreeSupport.h>     /* gIODTPlane, ADT node lookup */
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
	void free(void) APPLE_KEXT_OVERRIDE;

	/*
	 * IOService lifecycle: the controller is attached to the platform
	 * expert and started like any other service. start() probes TYPER,
	 * programs GICD_CTLR and publishes the controller under the name XNU
	 * derives from its ADT phandle (IOInterruptController%08X), which is
	 * what a device's interrupt-parent resolves through.
	 */
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;

	/* Identity read from the ADT node, kept for the device-specifier
	 * phase: the phandle is the name clients resolve, and the cell count
	 * is what their "interrupts" arrays will use. */
	void setAdtInfo(uint32_t phandle, uint32_t interruptCells);
	uint32_t adtPhandle(void) const { return fAdtPhandle; }
	uint32_t interruptCells(void) const { return fInterruptCells; }

	/* IOInterruptController overrides. `source` is the INTID in hardcoded
	 * mode (nub == NULL); with a nub the base class ADT path is used. */
	IOReturn registerInterrupt(IOService *nub, int source, void *target,
	    IOInterruptHandler handler, void *refCon) APPLE_KEXT_OVERRIDE;
	/* The base class only sets interruptDisabledSoft here. Drivers on this
	 * platform mask-and-wait (touch quiesce reads ISENABLER back), so this
	 * override also writes ICENABLER synchronously. */
	IOReturn disableInterrupt(IOService *nub, int source) APPLE_KEXT_OVERRIDE;





	IOInterruptAction getInterruptHandlerAddress(void) APPLE_KEXT_OVERRIDE;
	IOReturn handleInterrupt(void *refCon, IOService *nub, int source)
	    APPLE_KEXT_OVERRIDE;

	/*
	 * Nub side of the standard lookup: it fills the source table with the
	 * specifier whose first cell is the vector number, so the base class
	 * (registerInterrupt, the vector table and the per-vector lock) does
	 * the bookkeeping. Hardcoded mode is the controller acting as its own
	 * nub; a real ADT-backed nub resolves through IOService::lookupInterrupt
	 * before it reaches registerInterrupt().
	 *
	 * Not marked override on purpose: in kernel-private builds
	 * IOService::resolveInterrupt() is not virtual (only kexts get the
	 * compatibility virtual), so this is the same signature, called by the
	 * controller itself.
	 */
	IOReturn resolveInterrupt(IOService *nub, int source);

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

	/* Resolved hardware properties of one source. */
	struct SourceSpec {
		uint32_t intid;
		uint32_t type;
		uint32_t flags;
	};

	/* Look a vector number up in the controller's own specifier
	 * dictionary. The dictionary holds the standard 3-cell
	 * <INTID TYPE FLAGS> specifier; there is no private form. */
	IOReturn resolveSpec(int vectorNumber, SourceSpec *spec);

	/* Unregistered SPIs are counted and completed; masking them is opt-in
	 * so a staged migration can keep the legacy chain in front of the
	 * controller until every live SPI has registered. */
	void setMaskStraySPIs(bool mask);

	bool dispatch(uint32_t intid);

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
	};
	void getStatistics(Statistics *stats) const;

	static GenericInterruptController *controller(void);
	static void setController(GenericInterruptController *controller);

private:
	IOReturn ensureSpec(uint32_t intid, uint32_t type, uint32_t flags);
	IOReturn specFromNubProperties(IOService *nub, int source,
	    SourceSpec *spec) const;
	int intidFromNubProperties(IOService *nub, int source) const;

	IOInterruptVector *vectorAt(int intid) const;

	uint32_t gicdRead32(uint32_t offset) const;
	void gicdWrite32(uint32_t offset, uint32_t value);
	void gicdWrite64(uint32_t offset, uint64_t value);

	bool waitForRWP(void);
	void synchronizeIO(void);

	/* IOKit instantiates a service through init()/start(), never through a
	 * custom initializer, so every allocation happens in start(). */
	bool allocateTables(void);
	bool mapFromProvider(IOService *provider);
	IOReturn bringUp(void);
	void programSpec(int intid, const SourceSpec *spec);
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
	uint32_t              fAdtPhandle;
	uint32_t              fInterruptCells;
	/* One INTID-indexed table each, heap allocated: an OSObject is limited
	 * to 16 KiB and four such tables inline would blow past it. */
	OSData              **fSpecData;
	IOService           **fSpecNub;
	uint64_t             *fHits;
	uint32_t             *fStray;
	Statistics            fStats;
};

#endif /* SWEET */

#endif /* ! _IOKIT_GENERICINTERRUPTCONTROLLER_H */
