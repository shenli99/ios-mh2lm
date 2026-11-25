/* SM8150 GCC backend: C2-01/C2-02 read-only candidate plus the C2-08
 * debugcc measurement (the first candidate in this file that stores) and the
 * C2-03 branch gate (the first candidate that enables a clock for real).
 *
 * Contract of this candidate, in one place:
 *  - The backend maps exactly the four GCC pages it touches, in the ADT reg
 *    order, and validates every resolved physical address and length before
 *    the first register read. A wrong window fails start; it never degrades
 *    into a read of whatever memory happened to be mapped.
 *  - Register access is one narrow, bounds-checked, aligned helper under one
 *    IOLock with the platform-required barriers. Reads have no store
 *    counterpart anywhere else in the file, so the store set is exactly the
 *    measurement sequence below and a write counter that cannot drift from it.
 *  - Descriptors declare only what the vendor source declares: parents, halt
 *    class and the supply domain that has an explicit vdd_class. Branches with
 *    no declared domain report domain 0, not a guess.
 *  - C2-02 decodes the one fixed-parent sample configuration. A decoded rate
 *    is reported only when every parent rate in the path is trusted; anything
 *    else is Unknown (configuredHz 0, rateKnown false), never the requested
 *    value.
 *  - C2-08 measures with the vendor debug mux (clk-debug.c run_measurement):
 *    XO/4 branch as the counter reference, GCC debug mux select, post divider
 *    and counter control. Every register it stores is saved first and restored
 *    afterwards, and the restore is verified by reading the register back. A
 *    short and a full measurement returning the same count means "the measured
 *    clock is not running", which is reported as such and never as 0 Hz.
 *  - The measurement is an ADT opt-in. Without the property the backend is
 *    byte-for-byte the read-only C2-01/C2-02 backend: no store, writes=0.
 *  - C2-03 enables one normal branch gate (gcc_pdm_xo4_clk, C0-03 S2): save the
 *    CBCR, store only the enable bit, poll CLK_OFF clear within a bound, prove
 *    the neighbours did not move, and leave the branch running for the rest of
 *    the boot because this candidate has no verified disable yet (C2-04).
 *  - Nothing reconfigures a PLL, a reset or a GDSC, and the inherited display
 *    branches stay somebody else's property: they are declared pinned. */
#ifndef IOQCOM_GCC_H
#define IOQCOM_GCC_H

#include "IOQcomClock.h"
#include <IOKit/IOMemoryDescriptor.h>

#if defined(ARM64_BOARD_CONFIG_MH2LM)

/* C2-01: the ADT reg order is the window ABI. One 4 KiB page per block this
 * candidate observes: 0x10b000 (display), 0x133000 (PDM), 0x143000 (debug
 * XO/4 reference), 0x162000 (debug mux). Those are resolved physical
 * addresses, i.e. the vendor driver offset plus the GCC base 0x00100000 from
 * sm8150.dtsi (reg = <0x100000 0x1f0000>); a vendor offset such as 0x33008 is
 * never an address and must not be mapped as one. */
enum : unsigned { kIOQcomGccWindowCount = 4 };

/* C2-01: register evidence frozen at start, before any later card can change
 * the state. The order is the documented C0-03 snapshot, not a discovery. */
enum : unsigned { kIOQcomGccSnapshotCount = 11 };

/* C2-02: only the fixed-parent sample (gcc_pdm2_clk_src) is decoded. */
enum : unsigned { kIOQcomGccSampleLocalID = 5, kIOQcomGccSampleHidWidth = 5 };

/* C2-08: the two declared measurement samples. One is a running clock whose
 * rate the C2-02 decoder already states (gcc_disp_xo_clk = XO = 19.2 MHz), the
 * other is a clock the same snapshot shows gated (gcc_pdm2_clk), so both halves
 * of the card's device check are one bounded sequence on one mux. The debug
 * mux indices are the vendor debugcc-sm8150.c sel values, not device IDs. */
enum : uint32_t {
	kIOQcomGccMeasureRunningLocalID = 3,   /* gcc_disp_xo_clk */
	kIOQcomGccMeasureRunningMux = 0x53,
	kIOQcomGccMeasureRunningHz = 19200000,
	kIOQcomGccMeasureOffLocalID = 6,       /* gcc_pdm2_clk, enable=0 off=1 */
	kIOQcomGccMeasureOffMux = 0x9A,
	/* The counter reference is the vendor debugcc XO/4 gate at 0x43008, a
	 * branch of its own. It is NOT gcc_pdm_xo4_clk (0x33008, localID 4): the
	 * two sit in different windows, they do not share a parent (0x43008 is the
	 * only /4 in this system, 0x33008 is a plain XO gate) and the C2-03 sample
	 * owns the other one, so the ledger identity has to follow the register. */
	kIOQcomGccMeasureReferenceLocalID = 7, /* gcc_xo_div4_clk, 0x43008 */
};

/* C2-03: one normal branch gate. gcc_pdm_xo4_clk is the C0-03 S2 sample:
 * vendor offset 0x33008, one enable bit, CLK_OFF halt class, no parent divider
 * to program and no GDSC, and the iOS device tree never consumes it. Its debug
 * mux index (0x99) is also the C2-08 counter reference, so the measurement
 * chain already on the device can see whether enabling it really clocks.
 *
 * The parent is XO, not XO/4, and the device said so. 0130 shipped with the
 * XO/4 parent the name suggests; its counter then measured the gated branch at
 * 19200036 Hz against a declared 4800000 (delta 3000007 ppm, BadArgument).
 * The vendor tree agrees with the counter: debugcc-sm8150.c's MUX_SRC_LIST
 * maps 0x99 to gcc_pdm_xo4_clk, gcc-sm8150.c's branch at 0x33008 carries no
 * parent and no divider (a clk_branch2 gate cannot divide), and the only /4 in
 * this system is the counter's own timebase at 0x43008. The 4.8 MHz figure was
 * the calibration constant of the rate formula, not a rate of this branch. */
enum : uint32_t {
	kIOQcomGccGateLocalID = 4,             /* gcc_pdm_xo4_clk */
	kIOQcomGccGateMux = 0x99,
	kIOQcomGccGateHz = 19200000,           /* XO: 0130-1 counted 4x the claim */
};

/* The halt poll is the vendor clk_branch_wait(): bounded, 2500 x 10 us = 25 ms,
 * which is the same budget the counter wait uses and far below a watchdog. The
 * neighbours are the registers that share the sample's page plus the display
 * XO switch the snapshot already read; they must not move. */
enum : unsigned {
	kIOQcomGccGatePollTries = 2500,
	kIOQcomGccGatePollDelayUs = 10,
	kIOQcomGccGateNeighbourCount = 4,
};

/* C2-04: the verified disable. It polls for the halt status the hardware owns
 * with the same budget the enable polls for that status to clear: a branch
 * that does not report halted inside 25 ms is a timeout, and a timeout keeps
 * the demand in the ledger instead of resetting anything to look successful. */
enum : unsigned {
	kIOQcomGccDisablePollTries = 2500,
	kIOQcomGccDisablePollDelayUs = 10,
};

/* C2-08: tolerances. The vendor formula rounds to +-0.5 %; a rate outside one
 * percent means the mux or the counter is wrong, not a rounding difference. */
enum : int { kIOQcomGccMeasureTolerancePpm = 10000 };
/* Bounded counter poll: 2500 x 10 us = 25 ms per run, far above the 14 ms a
 * full measurement takes and far below a watchdog. The saved-register list is
 * the whole debug block plus the XO/4 branch gate. */
enum : unsigned {
	kIOQcomGccMeasurePollTries = 2500,
	kIOQcomGccMeasurePollDelayUs = 10,
	kIOQcomGccMeasureRegCount = 5,
};

/* The branch CBCRs are read back only over their configuration bits. Bits
 * 28..31 are the FSM's halt/NOC status: the hardware owns them and raises the
 * halt bit asynchronously, which is exactly why the vendor polls them in
 * clk_branch_wait() instead of storing them. The 0127 device read back 0x0
 * from GCC_DEBUG_CBCR right after disabling it, while the saved value was
 * 0x80000000 (halt status set at reset), so comparing that field would report
 * a restore failure that never happened. */
enum : uint32_t {
	kIOQcomGccBranchConfigMask = 0x0FFFFFFFu,
	kIOQcomGccVerifyAll = 0xFFFFFFFFu,
};

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
	/* One register the measurement stores and must put back. */
	struct MeasureReg {
		unsigned window;
		uint32_t offset;
		uint32_t saved;
		bool valid;
		bool stored;
	};
	IOLock *fLock;
	Window fWindow[kIOQcomGccWindowCount];
	struct { uint64_t value; bool valid; } fSnapshot[kIOQcomGccSnapshotCount];
	unsigned fReads, fReadFailures;
	unsigned fWrites, fWriteFailures, fRestoreMismatches;
	bool fMapped, fRegistered;
	bool fMeasureEnabled;
	bool fGateEnabled;
	bool fDisableEnabled;
	/* The C2-03 token while this driver still holds it. C2-04 releases that
	 * demand and clears the flag, so a disable can never hand back a token it
	 * does not own, and a second disable in the same boot has nothing to undo. */
	uint32_t fGateToken;
	bool fGateTokenHeld;
	/* Set once the C2-03 sample is proven running, so the C2-08 pass can add
	 * the one sample that needs somebody else's enable: the gate branch. */
	bool fGateRunning;
	unsigned fGateNeighbourChanges;
	struct { unsigned window; uint32_t offset; uint32_t value; bool valid; }
	 fGateNeighbour[kIOQcomGccGateNeighbourCount];
	bool validateProperties(IOService *provider);
	bool mapWindows(IOService *provider);
	void unmapWindows(void);
	bool readRegister(unsigned window, uint32_t offset, uint32_t *value);
	/* C2-08: the only store path. It re-checks bounds and alignment exactly as
	 * the read does, logs one row per store so the ring can be reconciled with
	 * the counter, and never retries: a failed store is a failed store. The
	 * consumer is passed in so a C2-03 gate store is not attributed to the
	 * measurement consumer that does not hold it. */
	bool writeRegister(unsigned window, uint32_t offset, uint32_t value,
	 const char *stage, const char *consumer);
	/* Every measurement store goes through one of these two, so the stored set
	 * is exactly the registers they mark for restore. */
	bool storeMeasure(MeasureReg *regs, unsigned index, uint32_t value, const char *stage);
	bool updateMeasure(MeasureReg *regs, unsigned index, uint32_t clearMask,
	 uint32_t setMask, const char *stage);
	void takeSnapshot(void);
	void decodeSamples(void);
	/* C2-08 measurement. Returns kIOReturnSuccess when every sample produced a
	 * verdict, kIOReturnUnsupported when the gate is absent (no store at all),
	 * and the first failing reason otherwise. */
	IOReturn measureClocks(void);
	IOReturn measureSave(MeasureReg *regs, unsigned count);
	IOReturn measureRestore(MeasureReg *regs, unsigned count);
	IOReturn runMeasurement(MeasureReg *regs, uint32_t ticks, uint32_t *count,
	 unsigned *polls);
	IOReturn measureSample(MeasureReg *regs, uint32_t localID, uint32_t muxIndex,
	 bool runningExpected);
	/* C2-03: the enable. Saves the CBCR, stores only the enable bit through the
	 * one store path, polls CLK_OFF clear within a bound, proves the neighbours
	 * did not move, and hands the ledger a running confirmation it keeps until
	 * the C2-04 disable hands that same demand back. */
	IOReturn gateClocks(void);
	/* C2-04: the disable. Refuses without the C2-03 demand in hand and without a
	 * running branch, stores the cleared enable bit alone, polls CLK_OFF set,
	 * proves the neighbours stayed put, and releases the ledger with the halted
	 * verdict the hardware actually reported. On success it re-runs the verified
	 * enable, so one boot proves both directions and the recovery. */
	IOReturn disableClocks(void);
	/* Reads the neighbour set into fGateNeighbour (capture=true) or compares it
	 * with that capture (capture=false), returning how many moved. The consumer
	 * is passed in so a disable row is not attributed to the gate pass. */
	unsigned gateNeighbours(bool capture, const char *consumer);
public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;
	uint32_t clockControllerID(void) const APPLE_KEXT_OVERRIDE { return kIOQcomCCGCC; }
	IOReturn copyClockInfo(uint32_t localID, IOQcomClockInfo *info) APPLE_KEXT_OVERRIDE;
	IOReturn copyClockState(uint32_t localID, IOQcomClockState *state) APPLE_KEXT_OVERRIDE;
	bool copyClockName(uint32_t localID, char *buffer, unsigned size) APPLE_KEXT_OVERRIDE;
	/* C2-08: the measured stores are counted, so the ring's write rows and this
	 * number are the same set. Zero without the ADT gate. */
	uint64_t registerWrites(void) const APPLE_KEXT_OVERRIDE { return fWrites; }
	unsigned registerReads(void) const { return fReads; }
};

#endif /* ARM64_BOARD_CONFIG_MH2LM */
#endif /* IOQCOM_GCC_H */