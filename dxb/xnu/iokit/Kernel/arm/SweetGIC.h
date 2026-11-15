/*
 * SweetGIC: the GICv3 distributor owner for the sweet platform.
 *
 * Scope is deliberately SPIs only (INTID 32..1019). SGIs and PPIs are per-CPU
 * state, the platform already drives them (vtimer PPI and the IPI SGI in
 * sleh_irq), and a controller that owned one of them would have to grow a
 * per-CPU table for a feature nothing needs yet. So no state is kept for
 * INTID < 32 at all.
 *
 * Each SPI has one IOInterruptVector, the record IOInterruptController itself
 * uses (nub/source/target/handler/refCon and the registered/disabled flags).
 * The vectors stay a flat array so this class can become an IOInterruptController
 * subclass later without moving the storage; controller-private bookkeeping
 * (priority, counters) lives in a parallel SweetGICSourceExtra array, which is
 * what we would keep out of the base class anyway.
 *
 * Dispatch model, XNU style:
 *
 *   ack       uint64_t iar = ICC_IAR1_EL1            (sleh_irq, not here)
 *   dispatch  dispatch(intid)                        one registered source
 *   eoi       endOfInterrupt(iar)                    once, and only once
 *
 * handleInterrupt(iar) performs dispatch + EOI and reports whether it
 * completed the interrupt. There is exactly one EOI for every interrupt:
 * either sleh_irq (legacy paths) or this controller, never both.
 */

#ifndef _IOKIT_SWEETGIC_H
#define _IOKIT_SWEETGIC_H

#include <IOKit/IOInterruptController.h>   /* IOInterruptVector */
#include <IOKit/IOLocks.h>
#include <IOKit/IOInterrupts.h>
#include <libkern/c++/OSObject.h>
#include <mach/vm_types.h>
#include <pexpert/arm64/board_config.h>
#include <stdint.h>

#if defined(SWEET)

/* Controller-private state per SPI; the standard vector stays untouched. */
struct SweetGICSourceExtra {
	uint32_t options;
	uint32_t stray;
	uint64_t hits;
};

class SweetGIC : public OSObject
{
	OSDeclareDefaultStructors(SweetGIC);

public:
	static SweetGIC *withBases(vm_offset_t gicd, vm_size_t gicdSize,
	    vm_offset_t gicr, vm_size_t gicrSize);

	bool initWithBases(vm_offset_t gicd, vm_size_t gicdSize,
	    vm_offset_t gicr, vm_size_t gicrSize);
	void free(void) APPLE_KEXT_OVERRIDE;

	/* Probe TYPER/redistributor affinities and make sure Group 1 is
	 * forwarded. Called once, after the platform expert mapped the GIC. */
	IOReturn start(void);

	/* IOInterruptController-shaped API, SPIs only. */
	IOReturn registerInterrupt(IOService *nub, int source, void *target,
	    IOInterruptHandler handler, void *refCon, IOOptionBits options);
	IOReturn unregisterInterrupt(IOService *nub, int source);
	IOReturn getInterruptType(IOService *nub, int source, int *interruptType);
	IOReturn enableInterrupt(IOService *nub, int source);
	IOReturn disableInterrupt(IOService *nub, int source);
	IOReturn causeInterrupt(IOService *nub, int source);

	IOReturn setPriority(int source, uint8_t priority);
	IOReturn setTriggerType(int source, bool level);

	/* Unregistered SPIs are counted and completed; masking them is opt-in so
	 * a staged migration can keep the legacy chain in front of the
	 * controller until every live SPI has registered. */
	void setMaskStraySPIs(bool mask);

	/* GIC steps. dispatch() and endOfInterrupt() leave completion to the
	 * caller; handleInterrupt() does both and reports completion. */
	uint32_t acknowledge(void);
	bool dispatch(uint32_t intid);
	void endOfInterrupt(uint64_t iar);
	bool handleInterrupt(uint64_t iar);

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

	static SweetGIC *controller(void);
	static void setController(SweetGIC *gic);

private:
	IOInterruptVector *vectorAt(int intid) const;
	SweetGICSourceExtra *extraAt(int intid) const;

	uint32_t gicdRead32(uint32_t offset) const;
	void gicdWrite32(uint32_t offset, uint32_t value);
	void gicdWrite64(uint32_t offset, uint64_t value);

	bool waitForRWP(void);
	void synchronizeIO(void);

	void writeGroup(int intid, bool group1);
	void writePriority(int intid, uint8_t priority);
	void writeTrigger(int intid, bool level);
	bool triggerIsLevel(int intid) const;
	void writeRoute(int intid, uint64_t affinity);
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
	IOInterruptVector    *fVectors;
	SweetGICSourceExtra  *fExtras;
	IOSimpleLock         *fLock;
	Statistics            fStats;
};

#endif /* SWEET */

#endif /* ! _IOKIT_SWEETGIC_H */
