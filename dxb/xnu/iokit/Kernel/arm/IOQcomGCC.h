/* SM8150 GCC backend, read-only candidate C2-01/C2-02.
 *
 * Contract of this candidate, in one place:
 *  - The backend maps exactly the four GCC pages this candidate reads, in the
 *    ADT reg order, and validates every resolved physical address and length
 *    before the first register read. A wrong window fails start; it never
 *    degrades into a read of whatever memory happened to be mapped.
 *  - Register access is a narrow, bounds-checked, read-only helper under one
 *    IOLock with the platform-required read barrier. There is no write path in
 *    this file at all, so the zero-write claim is structural.
 *  - Descriptors declare only what the vendor source declares: parents, halt
 *    class and the supply domain that has an explicit vdd_class. Branches with
 *    no declared domain report domain 0, not a guess.
 *  - C2-02 decodes the one fixed-parent sample configuration. A decoded rate
 *    is reported only when every parent rate in the path is trusted; anything
 *    else is Unknown (configuredHz 0, rateKnown false), never the requested
 *    value. No parent switching and no rate writes exist here.
 *  - Nothing reconfigures a PLL, a reset or a GDSC, and the inherited display
 *    branches stay somebody else's property: they are declared pinned. */
#ifndef IOQCOM_GCC_H
#define IOQCOM_GCC_H

#include "IOQcomClock.h"
#include <IOKit/IOMemoryDescriptor.h>

#if defined(ARM64_BOARD_CONFIG_MH2LM)

/* C2-01: the ADT reg order is the window ABI. One 4 KiB page per block this
 * candidate observes: 0x10b000 (display), 0x133000 (PDM), 0x143000 (XO/4),
 * 0x162000 (debug mux). Those are resolved physical addresses, i.e. the
 * vendor driver offset plus the GCC base 0x00100000 from sm8150.dtsi
 * (reg = <0x100000 0x1f0000>); a vendor offset such as 0x33008 is never an
 * address and must not be mapped as one. */
enum : unsigned { kIOQcomGccWindowCount = 4 };

/* C2-01: register evidence frozen at start, before any later card can change
 * the state. The order is the documented C0-03 snapshot, not a discovery. */
enum : unsigned { kIOQcomGccSnapshotCount = 11 };

/* C2-02: only the fixed-parent sample (gcc_pdm2_clk_src) is decoded. */
enum : unsigned { kIOQcomGccSampleLocalID = 5, kIOQcomGccSampleHidWidth = 5 };

/* Variant identity from the ADT property; a mismatched SoC must refuse. */
enum : uint32_t { kIOQcomGccVariantSm8150 = 0x8150 };

class IOQcomGCC : public IOQcomClockController {
	OSDeclareDefaultStructors(IOQcomGCC);
	struct Window {
		uint64_t physical;      /* expected resolved PA (ADT ranges applied) */
		IOByteCount length;     /* declared length, also the bounds limit */
		IOVirtualAddress base;  /* kernel virtual base, 0 when unmapped */
		IOMemoryMap *map;       /* owned mapping, released on stop/free */
	};
	IOLock *fLock;
	Window fWindow[kIOQcomGccWindowCount];
	struct { uint64_t value; bool valid; } fSnapshot[kIOQcomGccSnapshotCount];
	unsigned fReads, fReadFailures;
	bool fMapped, fRegistered;
	bool validateProperties(IOService *provider);
	bool mapWindows(IOService *provider);
	void unmapWindows(void);
	bool readRegister(unsigned window, uint32_t offset, uint32_t *value);
	void takeSnapshot(void);
	void decodeSamples(void);
public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;
	uint32_t clockControllerID(void) const APPLE_KEXT_OVERRIDE { return kIOQcomCCGCC; }
	IOReturn copyClockInfo(uint32_t localID, IOQcomClockInfo *info) APPLE_KEXT_OVERRIDE;
	IOReturn copyClockState(uint32_t localID, IOQcomClockState *state) APPLE_KEXT_OVERRIDE;
	bool copyClockName(uint32_t localID, char *buffer, unsigned size) APPLE_KEXT_OVERRIDE;
	/* No write path exists in this candidate, so the count is a constant and
	 * cannot drift away from the code the way a counter would. */
	uint64_t registerWrites(void) const APPLE_KEXT_OVERRIDE { return 0; }
	unsigned registerReads(void) const { return fReads; }
};

#endif /* ARM64_BOARD_CONFIG_MH2LM */
#endif /* IOQCOM_GCC_H */