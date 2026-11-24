/* SM8150 GCC backend: C2-01/C2-02 read-only candidate plus C2-08 measurement.
 *
 * Contract of this candidate, in one place:
 *  - start() maps the four declared GCC pages through the provider nub and
 *    validates the resolved physical address and the declared length of every
 *    window before the first read. A wrong translation fails start instead of
 *    silently reading somebody else's page.
 *  - Register access is one read helper and, since C2-08, one store helper:
 *    both bounds checked against the declared length, offset aligned, guarded
 *    by the backend lock, with the platform's barriers. Every store is logged
 *    with its ordinal, so the ring's write rows and registerWrites() are the
 *    same set and a store cannot be added without its row.
 *  - Descriptors come from the vendor GCC tables; a branch without an explicit
 *    vdd_class declares domain 0 instead of guessing CX, and the inherited
 *    display branches are declared pinned.
 *  - C2-02 decodes gcc_pdm2_clk_src only: raw CMD/CFG, selector, divider and
 *    the decoded value when every rate in the path is trusted. An untrusted
 *    parent is reported Unknown (configuredHz 0, rateKnown false); the
 *    requested value is never substituted for what the hardware shows.
 *  - C2-08 measures through the vendor debug mux. Every register it stores is
 *    saved first and restored afterwards; the restore is verified by reading
 *    the register back. Equal short and full counts mean the measured clock is
 *    not running and are reported as such, never as 0 Hz. Without the ADT
 *    measurement gate this file stores nothing. */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "IOQcomGCC.h"
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <string.h>
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"

#define super IOQcomClockController

OSDefineMetaClassAndStructors(IOQcomGCC,IOQcomClockController);

enum : unsigned { kIOQcomLogBuf=40, kIOQcomGccProviderWaitTries=200, kIOQcomGccProviderWaitMs=5 };

/* sm8150.dtsi declares the GCC block as reg = <0x100000 0x1f0000>. The vendor
 * driver's register numbers are offsets inside that block, so a window is the
 * base plus the offset the LG/QCOM tables quote; logging both keeps a device
 * ring reconcilable with those tables without redoing the subtraction. */
enum : uint64_t { kIOQcomGccBase=0x00100000ULL };

/* The ADT reg order is the window ABI; these are the only windows this
 * candidate reads and the only lengths it accepts. */
static const struct { uint64_t physical; uint64_t length; } gccWindows[kIOQcomGccWindowCount] = {
	{ 0x0010b000ULL, 0x1000ULL },   /* display block: inherited/pinned branches */
	{ 0x00133000ULL, 0x1000ULL },   /* PDM block: the C2-02 sample */
	{ 0x00143000ULL, 0x1000ULL },   /* XO/4 debug reference branch (C2-08) */
	{ 0x00162000ULL, 0x1000ULL },   /* debug mux block: C2-08 will own it */
};

/* C0-03 snapshot order. Evidence only: none of these is a clock this
 * candidate claims, and no owner is created by reading them. */
enum : unsigned { kSnapshotDispAhb=0, kSnapshotDispHfAxi, kSnapshotDispXo, kSnapshotPdmXo4,
	kSnapshotPdm2, kSnapshotPdm2SrcCmd, kSnapshotPdm2SrcCfg, kSnapshotXoDiv4,
	kSnapshotDebugCtl, kSnapshotDebugPost, kSnapshotDebugCbcr };
static const struct { unsigned window; uint32_t offset; } gccSnapshot[kIOQcomGccSnapshotCount] = {
	{ 0, 0x00c }, { 0, 0x038 }, { 0, 0x048 },
	{ 1, 0x008 }, { 1, 0x00c }, { 1, 0x010 }, { 1, 0x014 },
	{ 2, 0x008 },
	{ 3, 0x000 }, { 3, 0x004 }, { 3, 0x008 },
};

/* Branch CBCR and RCG CMD/CFG field layout (drivers/clk/qcom/clk-rcg2.c and
 * clk-branch.h of the vendor tree). */
enum : uint32_t { kGccBranchEnable=1u<<0, kGccBranchClockOff=1u<<31,
	kGccCmdUpdate=1u<<0, kGccCmdRootEn=1u<<1, kGccCmdRootOff=1u<<31,
	kGccCfgSrcDivMask=(1u<<kIOQcomGccSampleHidWidth)-1u,
	kGccCfgSrcSelMask=0x7u, kGccCfgSrcSelShift=8,
	kGccCfgModeMask=0x3u, kGccCfgModeShift=12,
	kGccCfgRegOffset=0x4 };

enum : uint64_t { kGccXoHz=19200000ULL, kGccXoDiv4Hz=4800000ULL };

/* C2-08 register map, transcribed from the vendor debugcc-sm8150.c
 * measurement block. Offsets are inside the mapped pages, so 0x62000 in the
 * vendor table is kGccWindowDebug + kGccDebugMuxOffset = 0x00162000. */
enum : unsigned { kGccWindowDisplay=0, kGccWindowPdm=1, kGccWindowXoDiv4=2, kGccWindowDebug=3 };
enum : uint32_t {
	kGccDebugMuxOffset=0x000,          /* 0x62000 source select, mask 0x3ff */
	kGccDebugPostDivOffset=0x004,      /* 0x62004 post divider, mask 0xf */
	kGccDebugGateOffset=0x008,         /* 0x62008 GCC debug branch enable */
	kGccMeasureCtlOffset=0x038,        /* 0x62038 counter control */
	kGccMeasureStatusOffset=0x03c,     /* 0x6203c counter status */
	kGccXoDiv4Offset=0x008,            /* 0x43008: the debug XO/4 reference
	                                    * branch, NOT gcc_pdm_xo4_clk. The PDM
	                                    * XO/4 branch is window 1 offset 0x008
	                                    * (0x133008); device evidence: enabling
	                                    * 0x43008 moved the counter, enabling
	                                    * 0x33008 is the C2-03 gate sample. */
	kGccDebugSrcSelMask=0x3ffu,
	kGccDebugPostDivMask=0xfu,
	kGccMeasureCountEnable=1u<<20,     /* CNT_EN */
	kGccMeasureCountDone=1u<<25,       /* XO_DIV4_CNT_DONE */
	kGccMeasureCountMask=(1u<<25)-1u,  /* MEASURE_CNT */
	kGccMeasureTicksShort=0x1000u,     /* ~1 ms of XO/4 */
	kGccMeasureTicksFull=0x10000u,     /* ~14 ms of XO/4 */
	kGccMeasurePostDiv=1u,             /* prim_mux_div_val of both samples */
	kGccMeasureMultiplier=1u,
};

/* Bounded polling (kIOQcomGccMeasurePoll*): the vendor driver spins
 * unbounded waiting for CNT_DONE, a hang this card cannot afford. A dead
 * reference costs at most that per wait, never the boot. */

/* A declared clock. parent[].localID carries, for fixed sources, the RCG
 * parent-map selector the decoder matches against CFG_SRC_SEL; it is never a
 * clock identity and the admission walk ignores it for those sources. */
struct GccClock {
	const char *name;
	uint32_t localID, kind, halt, capabilities, domain, accessDeps;
	unsigned parents;
	IOQcomClockParent parent[kIOQcomMaxParents];
	unsigned window, snapshot;
	uint32_t offset;
	bool pinned;
};

static const GccClock gccClocks[] = {
	{ "gcc_disp_ahb_clk", 1, kIOQcomClockBranch, kIOQcomHaltDelay,
	  kIOQcomCapQueryConfig|kIOQcomCapQueryRate, 0, 0, 0, {{0,0,0}},
	  0, kSnapshotDispAhb, 0x00c, true },
	{ "gcc_disp_hf_axi_clk", 2, kIOQcomClockBranch, kIOQcomHaltCheck,
	  kIOQcomCapQueryConfig|kIOQcomCapQueryRate, 0, 0, 0, {{0,0,0}},
	  0, kSnapshotDispHfAxi, 0x038, false },
	{ "gcc_disp_xo_clk", 3, kIOQcomClockBranch, kIOQcomHaltDelay,
	  kIOQcomCapQueryConfig|kIOQcomCapQueryRate, 0, 0, 1,
	  {{kIOQcomSourceXO,0,0}},
	  0, kSnapshotDispXo, 0x048, true },
	/* The C2-03 sample: vendor offset 0x33008, one enable bit, halt check,
	 * fixed XO/4 parent, no GDSC, and unused by the iOS device tree. */
	{ "gcc_pdm_xo4_clk", kIOQcomGccGateLocalID, kIOQcomClockBranch, kIOQcomHaltCheck,
	  kIOQcomCapQueryConfig|kIOQcomCapQueryRate, 0, 0, 1,
	  {{kIOQcomSourceXODiv4,0,0}},
	  1, kSnapshotPdmXo4, 0x008, false },
	/* The one C2-02 sample: mnd_width 0, hid_width 5, gcc_parent_map_0. */
	{ "gcc_pdm2_clk_src", kIOQcomGccSampleLocalID, kIOQcomClockRCG, kIOQcomHaltDelay,
	  kIOQcomCapQueryConfig|kIOQcomCapQueryRate, kIOQcomDomainCX, 0, 3,
	  {{kIOQcomSourceXO,0,0},{kIOQcomSourceGPLL0Main,0,1},{kIOQcomSourceGPLL0Even,0,6}},
	  1, kSnapshotPdm2SrcCmd, 0x010, false },
	{ "gcc_pdm2_clk", 6, kIOQcomClockBranch, kIOQcomHaltCheck,
	  kIOQcomCapQueryConfig|kIOQcomCapQueryRate, 0, 0, 1,
	  {{kIOQcomSourceLocal,kIOQcomCCGCC,kIOQcomGccSampleLocalID}},
	  1, kSnapshotPdm2, 0x00c, false },
	/* The C2-08 counter reference, declared as the branch it really is. It
	 * shares nothing with gcc_pdm_xo4_clk but the XO/4 fixed parent, so the
	 * measurement demand and the C2-03 sample demand are two identities. */
	{ "gcc_xo_div4_clk", kIOQcomGccMeasureReferenceLocalID, kIOQcomClockBranch,
	  kIOQcomHaltCheck, kIOQcomCapQueryConfig|kIOQcomCapQueryRate, 0, 0, 1,
	  {{kIOQcomSourceXODiv4,0,0}},
	  kGccWindowXoDiv4, kSnapshotXoDiv4, kGccXoDiv4Offset, false },
};
enum : unsigned { kGccClockCount=sizeof gccClocks/sizeof gccClocks[0] };

static const GccClock *gccFind(uint32_t localID)
{
	for (unsigned i=0;i<kGccClockCount;i++) {
		if (gccClocks[i].localID==localID) return &gccClocks[i];
	}
	return NULL;
}

/* Fixed sources are the only rates this tree trusts. The vendor frequency
 * tables are not evidence: their divisor rows disagree with clk_rcg2_calc_rate
 * for the same field values, so a table row is never used as a rate. */
static bool gccFixedRate(const IOQcomClockParent &parent,uint64_t *hz)
{
	switch (parent.source) {
	case kIOQcomSourceXO: *hz=kGccXoHz; return true;
	case kIOQcomSourceXODiv4: *hz=kGccXoDiv4Hz; return true;
	default: *hz=0; return false;
	}
}

static void gccName(unsigned window,uint32_t offset,char *out,size_t size)
{
	snprintf(out,size,"gcc:%#llx",(unsigned long long)(gccWindows[window].physical+offset));
}

bool
IOQcomGCC::validateProperties(IOService *provider)
{
	OSData *abi=OSDynamicCast(OSData,provider->getProperty("sweet,gcc-abi"));
	if (!abi||abi->getLength()!=4||memcmp(abi->getBytesNoCopy(),"\1\0\0\0",4)) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=missing-gcc-abi\n",
		 kIOReturnBadArgument);
		return false;
	}
	OSData *variant=OSDynamicCast(OSData,provider->getProperty("sweet,gcc-variant"));
	if (!variant||variant->getLength()!=4) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=missing-variant\n",
		 kIOReturnBadArgument);
		return false;
	}
	uint32_t observed=0;
	memcpy(&observed,variant->getBytesNoCopy(),4);
	if (observed!=kIOQcomGccVariantSm8150) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=variant-mismatch observed=%#x required=%#x\n",
		 kIOReturnUnsupported,observed,kIOQcomGccVariantSm8150);
		return false;
	}
	/* C2-08 is a write candidate: it runs only when the ADT says so, so the
	 * decoded-only image keeps its zero-store property. An unreadable or
	 * unknown gate value refuses start instead of silently measuring. */
	fMeasureEnabled=false;
	bool haveGate=false;
	OSData *measure=OSDynamicCast(OSData,provider->getProperty("sweet,clock-measure"));
	if (measure) {
		if (measure->getLength()!=4) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=bad-clock-measure-gate length=%u\n",
			 kIOReturnBadArgument,(unsigned)measure->getLength());
			return false;
		}
		uint32_t value=0;
		memcpy(&value,measure->getBytesNoCopy(),4);
		if (value>1) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=bad-clock-measure-gate value=%#x\n",
			 kIOReturnBadArgument,value);
			return false;
		}
		fMeasureEnabled=value==1;
		haveGate=true;
	}
	/* C2-03 is the same kind of opt-in, with its own property so a measurement
	 * image and an enable image stay independently selectable, and a value the
	 * ADT never writes refuses start instead of enabling a clock. */
	fGateEnabled=false;
	OSData *branchGate=OSDynamicCast(OSData,provider->getProperty("sweet,clock-gate"));
	if (branchGate) {
		if (branchGate->getLength()!=4) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=bad-clock-gate length=%u\n",
			 kIOReturnBadArgument,(unsigned)branchGate->getLength());
			return false;
		}
		uint32_t value=0;
		memcpy(&value,branchGate->getBytesNoCopy(),4);
		if (value>1) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=bad-clock-gate value=%#x\n",
			 kIOReturnBadArgument,value);
			return false;
		}
		fGateEnabled=value==1;
		haveGate=true;
	}
	if (haveGate) {
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=gcc phase=gate result=0 clock-measure=%u clock-gate=%u\n",
		 fMeasureEnabled?1u:0u,fGateEnabled?1u:0u);
	}
	return true;
}

bool
IOQcomGCC::mapWindows(IOService *provider)
{
	for (unsigned i=0;i<kIOQcomGccWindowCount;i++) {
		fWindow[i].physical=gccWindows[i].physical;
		fWindow[i].length=0;
		fWindow[i].base=0;
		fWindow[i].map=provider?provider->mapDeviceMemoryWithIndex(i):NULL;
		char resource[kIOQcomLogBuf];
		gccName(i,0,resource,sizeof resource);
		if (!fWindow[i].map) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s phase=map result=%d detail=window-absent\n",
			 resource,kIOReturnNotReady);
			return false;
		}
		uint64_t observed=fWindow[i].map->getPhysicalAddress();
		uint64_t length=fWindow[i].map->getLength();
		if (observed!=gccWindows[i].physical) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s phase=map result=%d detail=address-mismatch observed=%#llx\n",
			 resource,kIOReturnBadArgument,(unsigned long long)observed);
			return false;
		}
		if (length<gccWindows[i].length) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s phase=map result=%d detail=length-short observed=%#llx required=%#llx\n",
			 resource,kIOReturnBadArgument,(unsigned long long)length,
			 (unsigned long long)gccWindows[i].length);
			return false;
		}
		fWindow[i].length=length;
		fWindow[i].base=fWindow[i].map->getVirtualAddress();
		if (!fWindow[i].base) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s phase=map result=%d detail=no-virtual-base\n",
			 resource,kIOReturnNotReady);
			return false;
		}
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s phase=map result=0 length=%#llx offset=%#llx\n",
		 resource,(unsigned long long)length,
		 (unsigned long long)(gccWindows[i].physical-kIOQcomGccBase));
	}
	return true;
}

void
IOQcomGCC::unmapWindows(void)
{
	for (unsigned i=0;i<kIOQcomGccWindowCount;i++) {
		OSSafeReleaseNULL(fWindow[i].map);
		fWindow[i].base=0;
		fWindow[i].length=0;
	}
	fMapped=false;
}

/* The only register accessor in this file. Bounds and alignment are checked
 * against the declared window before the load, the load is followed by the
 * read barrier the display backend uses for this SoC, and the whole access
 * runs under the backend lock so a query never interleaves with a mapping
 * change. Reads only: there is no store counterpart to audit. */
bool
IOQcomGCC::readRegister(unsigned window,uint32_t offset,uint32_t *value)
{
	if (!value||window>=kIOQcomGccWindowCount||!fLock) return false;
	bool ok=false;
	uint32_t observed=0;
	IOLockLock(fLock);
	const Window &w=fWindow[window];
	if (w.base&&(offset&3u)==0&&(uint64_t)offset+sizeof(uint32_t)<=w.length) {
		observed=*(volatile uint32_t *)(w.base+offset);
		__asm__ volatile("dmb oshld" ::: "memory");
		ok=true;
		fReads++;
	} else {
		fReadFailures++;
	}
	IOLockUnlock(fLock);
	if (!ok) return false;
	*value=observed;
	return true;
}

void
IOQcomGCC::takeSnapshot(void)
{
	for (unsigned i=0;i<kIOQcomGccSnapshotCount;i++) {
		uint32_t value=0;
		bool ok=readRegister(gccSnapshot[i].window,gccSnapshot[i].offset,&value);
		fSnapshot[i].value=ok?value:0;
		fSnapshot[i].valid=ok;
		char resource[kIOQcomLogBuf];
		gccName(gccSnapshot[i].window,gccSnapshot[i].offset,resource,sizeof resource);
		if (!ok) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s phase=snapshot result=%d detail=read-failed\n",
			 resource,kIOReturnNotReady);
			continue;
		}
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s phase=snapshot result=0 value=%#x\n",
		 resource,value);
	}
}

/* C2-08: the ordered store set. The index is also the restore order, so this
 * table is the entire write footprint of the card and measureRestore() walks it
 * backwards. */
enum : unsigned { kMeasureXo4=0, kMeasureDebugGate, kMeasureMux, kMeasurePost, kMeasureCtl };
static const struct {
	unsigned window;
	uint32_t offset;
	uint32_t verifyMask;    /* bits measureRestore() compares after the write */
} gccMeasureRegs[kIOQcomGccMeasureRegCount] = {
	{ kGccWindowXoDiv4, kGccXoDiv4Offset, kIOQcomGccBranchConfigMask },
	{ kGccWindowDebug, kGccDebugGateOffset, kIOQcomGccBranchConfigMask },
	{ kGccWindowDebug, kGccDebugMuxOffset, kIOQcomGccVerifyAll },
	{ kGccWindowDebug, kGccDebugPostDivOffset, kIOQcomGccVerifyAll },
	{ kGccWindowDebug, kGccMeasureCtlOffset, kIOQcomGccVerifyAll },
};

/* The vendor rate formula, unchanged: the full count is scaled by the XO/4
 * reference and divided by the loaded tick count with its rounding term. */
static uint64_t
gccMeasureRate(uint32_t shortCount,uint32_t fullCount)
{
	if (shortCount==fullCount) return 0;   /* counter did not advance: not running */
	uint64_t scaled=((uint64_t)fullCount*10u+15u)*kGccXoDiv4Hz;
	return scaled/((uint64_t)kGccMeasureTicksFull*10u+35u);
}

/* The only store in this file. Same bounds and alignment checks as the read,
 * store barrier before the store and dsb after it, ordinal logged so the ring
 * and fWrites are the same set. */
bool
IOQcomGCC::writeRegister(unsigned window,uint32_t offset,uint32_t value,
 const char *stage,const char *consumer)
{
	if (!fLock) return false;
	if (!consumer) consumer="sweetclock-measure";
	bool ok=false;
	unsigned ordinal=0;
	IOLockLock(fLock);
	const Window &w=fWindow[window];
	if (window<kIOQcomGccWindowCount&&w.base&&(offset&3u)==0&&
	 (uint64_t)offset+sizeof(uint32_t)<=w.length) {
		__asm__ volatile("dmb oshst" ::: "memory");
		*(volatile uint32_t *)(w.base+offset)=value;
		__asm__ volatile("dsb osh" ::: "memory");
		ok=true;
		fWrites++;
		ordinal=fWrites;
	} else {
		fWriteFailures++;
	}
	IOLockUnlock(fLock);
	char resource[kIOQcomLogBuf];
	gccName(window,offset,resource,sizeof resource);
	if (!ok) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=write-issued result=%d stage=%s detail=bounds\n",
		 resource,consumer,kIOReturnBadArgument,stage);
		return false;
	}
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=write-issued result=0 stage=%s write=%u value=%#x\n",
	 resource,consumer,stage,ordinal,value);
	return true;
}

bool
IOQcomGCC::storeMeasure(MeasureReg *regs,unsigned index,uint32_t value,const char *stage)
{
	if (!regs||index>=kIOQcomGccMeasureRegCount) return false;
	if (!writeRegister(gccMeasureRegs[index].window,gccMeasureRegs[index].offset,value,stage,
	   "sweetclock-measure")) return false;
	regs[index].stored=true;
	return true;
}

bool
IOQcomGCC::updateMeasure(MeasureReg *regs,unsigned index,uint32_t clearMask,uint32_t setMask,
 const char *stage)
{
	if (!regs||index>=kIOQcomGccMeasureRegCount||!regs[index].valid) return false;
	uint32_t value=(regs[index].saved&~clearMask)|setMask;
	return storeMeasure(regs,index,value,stage);
}

IOReturn
IOQcomGCC::measureSave(MeasureReg *regs,unsigned count)
{
	if (!regs||count>kIOQcomGccMeasureRegCount) return kIOReturnBadArgument;
	for (unsigned i=0;i<count;i++) {
		regs[i].window=gccMeasureRegs[i].window;
		regs[i].offset=gccMeasureRegs[i].offset;
		regs[i].saved=0;
		regs[i].valid=false;
		regs[i].stored=false;
		char resource[kIOQcomLogBuf];
		gccName(regs[i].window,regs[i].offset,resource,sizeof resource);
		if (!readRegister(regs[i].window,regs[i].offset,&regs[i].saved)) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-measure phase=measure-save result=%d detail=read-failed\n",
			 resource,kIOReturnNotReady);
			return kIOReturnNotReady;
		}
		regs[i].valid=true;
		SWEET_LOG_DEBUG("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-measure phase=measure-save result=0 value=%#x\n",
		 resource,regs[i].saved);
	}
	return kIOReturnSuccess;
}

/* Reverse order, only the registers this card actually stored, each verified by
 * reading it back after the dsb inside writeRegister(). */
IOReturn
IOQcomGCC::measureRestore(MeasureReg *regs,unsigned count)
{
	if (!regs||count>kIOQcomGccMeasureRegCount) return kIOReturnBadArgument;
	IOReturn result=kIOReturnSuccess;
	for (unsigned back=count;back>0;back--) {
		MeasureReg &r=regs[back-1];
		if (!r.stored||!r.valid) continue;
		char resource[kIOQcomLogBuf];
		gccName(r.window,r.offset,resource,sizeof resource);
		if (!writeRegister(r.window,r.offset,r.saved,"restore","sweetclock-measure")) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-measure phase=restore result=%d detail=write-failed\n",
			 resource,kIOReturnNotReady);
			result=kIOReturnNotReady;
			continue;
		}
		uint32_t observed=0;
		if (!readRegister(r.window,r.offset,&observed)) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-measure phase=restore result=%d detail=read-failed\n",
			 resource,kIOReturnNotReady);
			result=kIOReturnNotReady;
			continue;
		}
		/* Only the bits this card may own are compared: a branch CBCR's
		 * halt status belongs to the hardware and reappears on its own
		 * schedule (the vendor polls it, clk_branch_wait()). */
		uint32_t mask=gccMeasureRegs[back-1].verifyMask;
		if ((observed&mask)!=(r.saved&mask)) {
			fRestoreMismatches++;
			SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-measure phase=restore result=%d expected=%#x value=%#x mask=%#x detail=mismatch\n",
			 resource,kIOReturnIOError,r.saved,observed,mask);
			result=kIOReturnIOError;
			continue;
		}
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-measure phase=restore result=0 expected=%#x value=%#x mask=%#x\n",
		 resource,r.saved,observed,mask);
	}
	return result;
}

/* Vendor run_measurement(): load the XO/4 start value, wait for the counter to
 * be idle, enable it, wait for completion, take the count, stop it. Every wait
 * is bounded; a timeout is reported, not spun on. */
IOReturn
IOQcomGCC::runMeasurement(MeasureReg *regs,uint32_t ticks,uint32_t *count,unsigned *polls)
{
	if (!regs||!count||!polls) return kIOReturnBadArgument;
	*polls=0;
	*count=0;
	uint32_t status=0,observed=0;
	char row[kIOQcomLogBuf];
	gccName(kGccWindowDebug,kGccMeasureStatusOffset,row,sizeof row);
	if (!storeMeasure(regs,kMeasureCtl,ticks,"count-ctl")) return kIOReturnNotReady;
	if (!readRegister(kGccWindowDebug,kGccMeasureStatusOffset,&status)) return kIOReturnNotReady;
	while ((status&kGccMeasureCountDone)!=0) {
		if (++*polls>kIOQcomGccMeasurePollTries) {
			SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-measure phase=count-ctl result=%d detail=idle-timeout polls=%u status=%#x\n",
			 row,kIOReturnTimeout,*polls,status);
			return kIOReturnTimeout;
		}
		IODelay(kIOQcomGccMeasurePollDelayUs);
		if (!readRegister(kGccWindowDebug,kGccMeasureStatusOffset,&status)) return kIOReturnNotReady;
	}
	if (!storeMeasure(regs,kMeasureCtl,kGccMeasureCountEnable|ticks,"count-start")) return kIOReturnNotReady;
	*polls=0;
	if (!readRegister(kGccWindowDebug,kGccMeasureStatusOffset,&status)) return kIOReturnNotReady;
	while ((status&kGccMeasureCountDone)==0) {
		if (++*polls>kIOQcomGccMeasurePollTries) {
			SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-measure phase=count-start result=%d detail=done-timeout polls=%u status=%#x\n",
			 row,kIOReturnTimeout,*polls,status);
			return kIOReturnTimeout;
		}
		IODelay(kIOQcomGccMeasurePollDelayUs);
		if (!readRegister(kGccWindowDebug,kGccMeasureStatusOffset,&status)) return kIOReturnNotReady;
	}
	if (!readRegister(kGccWindowDebug,kGccMeasureStatusOffset,&observed)) return kIOReturnNotReady;
	*count=observed&kGccMeasureCountMask;
	if (!storeMeasure(regs,kMeasureCtl,ticks,"count-stop")) return kIOReturnNotReady;
	return kIOReturnSuccess;
}

/* C2-02 says what the registers mean; this says whether the clock is really
 * running and how fast. A mismatch between the two is a finding, not a pass. */
IOReturn
IOQcomGCC::measureSample(MeasureReg *regs,uint32_t localID,uint32_t muxIndex,bool runningExpected)
{
	char name[kIOQcomLogBuf];
	if (!copyClockName(localID,name,sizeof name)) snprintf(name,sizeof name,"cc%u:0x%x",kIOQcomCCGCC,localID);
	if (!updateMeasure(regs,kMeasureMux,kGccDebugSrcSelMask,muxIndex&kGccDebugSrcSelMask,"mux-select")) return kIOReturnNotReady;
	uint32_t shortCount=0,fullCount=0;
	unsigned shortPolls=0,fullPolls=0;
	IOReturn result=runMeasurement(regs,kGccMeasureTicksShort,&shortCount,&shortPolls);
	if (result==kIOReturnSuccess) result=runMeasurement(regs,kGccMeasureTicksFull,&fullCount,&fullPolls);
	if (result!=kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=cc%u:0x%x name=%s consumer=sweetclock-measure phase=measure result=%d detail=count-failed\n",
		 kIOQcomCCGCC,localID,name,result);
		return result;
	}
	uint64_t hz=gccMeasureRate(shortCount,fullCount);
	bool running=hz!=0;
	if (running!=runningExpected) {
		result=kIOReturnNotReady;
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=cc%u:0x%x name=%s consumer=sweetclock-measure phase=measure result=%d mux=%#x ticks=%#x/%#x short=%u full=%u polls=%u/%u hz=%llu running=%u detail=%s\n",
		 kIOQcomCCGCC,localID,name,result,muxIndex,kGccMeasureTicksShort,kGccMeasureTicksFull,
		 shortCount,fullCount,shortPolls,fullPolls,(unsigned long long)hz,running?1u:0u,
		 running?"running-unexpected":"not-running");
	} else {
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=cc%u:0x%x name=%s consumer=sweetclock-measure phase=measure result=0 mux=%#x ticks=%#x/%#x short=%u full=%u polls=%u/%u hz=%llu running=%u post-div=%u multiplier=%u\n",
		 kIOQcomCCGCC,localID,name,muxIndex,kGccMeasureTicksShort,kGccMeasureTicksFull,
		 shortCount,fullCount,shortPolls,fullPolls,(unsigned long long)hz,running?1u:0u,
		 kGccMeasurePostDiv,kGccMeasureMultiplier);
	}
	/* The C2-02 comparison the card asks for, on the same clock identity. */
	IOQcomClockState state;
	if (running&&copyClockState(localID,&state)==kIOReturnSuccess&&state.rateKnown&&state.configuredHz) {
		int64_t delta=(int64_t)hz-(int64_t)state.configuredHz;
		/* Both operands of the division must be signed: configuredHz is
		 * unsigned, and a mixed division turns a small negative delta into a
		 * huge unsigned value that would fail the tolerance check. */
		int64_t ppm=delta*1000000/(int64_t)state.configuredHz;
		if (ppm<0) ppm=-ppm;
		if (ppm>kIOQcomGccMeasureTolerancePpm) {
			SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=cc%u:0x%x name=%s consumer=sweetclock-measure phase=measure-compare result=%d decoded-hz=%llu measured-hz=%llu delta-ppm=%lld tolerance-ppm=%d\n",
			 kIOQcomCCGCC,localID,name,kIOReturnIOError,(unsigned long long)state.configuredHz,
			 (unsigned long long)hz,(long long)((delta*1000000)/(int64_t)state.configuredHz),
			 kIOQcomGccMeasureTolerancePpm);
			if (result==kIOReturnSuccess) result=kIOReturnIOError;
		} else {
			SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=cc%u:0x%x name=%s consumer=sweetclock-measure phase=measure-compare result=0 decoded-hz=%llu measured-hz=%llu delta-ppm=%lld tolerance-ppm=%d\n",
			 kIOQcomCCGCC,localID,name,(unsigned long long)state.configuredHz,
			 (unsigned long long)hz,(long long)((delta*1000000)/(int64_t)state.configuredHz),
			 kIOQcomGccMeasureTolerancePpm);
		}
	}
	return result;
}

/* C2-03: the registers that must not move while the gate is enabled. The PDM
 * branch shares its page with the sample and so does its source RCG; the
 * display XO switch is the inherited branch the snapshot already read, and a
 * change there would mean the store landed on somebody else's clock. Bits
 * 28..31 are excluded for the same reason the restore excludes them: the
 * hardware owns the halt status and moves it on its own schedule. */
static const struct { unsigned window; uint32_t offset; uint32_t mask; }
 gccGateNeighbours[kIOQcomGccGateNeighbourCount] = {
	{ kGccWindowPdm, 0x00c, kIOQcomGccBranchConfigMask },     /* gcc_pdm2_clk */
	{ kGccWindowPdm, 0x010, kIOQcomGccBranchConfigMask },     /* pdm2_clk_src CMD */
	{ kGccWindowPdm, 0x014, kIOQcomGccVerifyAll },            /* pdm2_clk_src CFG */
	{ kGccWindowDisplay, 0x048, kIOQcomGccBranchConfigMask }, /* gcc_disp_xo_clk */
};

/* Capture (capture=true) or compare (false). A failed read during the compare
 * counts as a change: this proof exists to rule out an unexplained move, so an
 * unreadable neighbour must not pass it. */
unsigned
IOQcomGCC::gateNeighbours(bool capture)
{
	unsigned changed=0;
	for (unsigned i=0;i<kIOQcomGccGateNeighbourCount;i++) {
		uint32_t value=0;
		bool ok=readRegister(gccGateNeighbours[i].window,gccGateNeighbours[i].offset,&value);
		if (capture) {
			fGateNeighbour[i].window=gccGateNeighbours[i].window;
			fGateNeighbour[i].offset=gccGateNeighbours[i].offset;
			fGateNeighbour[i].value=value;
			fGateNeighbour[i].valid=ok;
			continue;
		}
		uint32_t mask=gccGateNeighbours[i].mask;
		if (!ok||!fGateNeighbour[i].valid||
		 (value&mask)!=(fGateNeighbour[i].value&mask)) {
			changed++;
			char resource[kIOQcomLogBuf];
			gccName(gccGateNeighbours[i].window,gccGateNeighbours[i].offset,resource,sizeof resource);
			SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-gate phase=neighbours result=%d detail=changed before=%#x after=%#x mask=%#x\n",
			 resource,kIOReturnIOError,fGateNeighbour[i].value&mask,value&mask,mask);
		}
	}
	fGateNeighbourChanges=changed;
	return changed;
}

/* C2-03 entry: the first card in this file that enables a clock. The C0-03 S2
 * sample is a plain branch, so the enable bit is the only field this card may
 * write, the CLK_OFF halt status is the verifier (never a read-back of the bit
 * the store just wrote: that path is exactly what a lagging halt status fools),
 * and the neighbour set is the proof the store landed where it was aimed.
 *
 * The ledger token is kept, not released. This candidate has no verified
 * disable yet (C2-04 owns that) and handing the demand back while the branch
 * still runs would be a release row the hardware does not back. The cost is
 * explicit: hold=1 and one held holder until reboot. */
IOReturn
IOQcomGCC::gateClocks(void)
{
	if (!fGateEnabled) {
		SWEET_LOG_DEBUG("SWEET CLOCK: planID=C2 resource=cc%u:0x%x consumer=sweetclock-gate phase=gate result=%d detail=gate-absent\n",
		 kIOQcomCCGCC,kIOQcomGccGateLocalID,kIOReturnUnsupported);
		return kIOReturnUnsupported;
	}
	if (!fMapped||!fRegistered||!fLock) return kIOReturnNotReady;
	const GccClock *entry=gccFind(kIOQcomGccGateLocalID);
	if (!entry) return kIOReturnNotFound;
	const unsigned window=entry->window;
	const uint32_t offset=entry->offset;
	char resource[kIOQcomLogBuf];
	gccName(window,offset,resource,sizeof resource);
	uint32_t saved=0;
	if (!readRegister(window,offset,&saved)) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-gate phase=request result=%d detail=read-failed\n",
		 resource,kIOReturnNotReady);
		return kIOReturnNotReady;
	}
	bool enabled=(saved&kGccBranchEnable)!=0;
	bool off=(saved&kGccBranchClockOff)!=0;
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s name=%s consumer=sweetclock-gate phase=request result=0 enable=%u off=%u value=%#x\n",
	 resource,entry->name,enabled?1u:0u,off?1u:0u,saved);
	/* An enable bit somebody else already set is not this card's to touch: the
	 * shared-demand case is C2-05, and a write now would hide a state this card
	 * cannot explain. A branch that is not halted is equally unexplained. */
	if (enabled||!off) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-gate phase=request result=%d detail=%s enable=%u off=%u value=%#x\n",
		 resource,kIOReturnNotReady,enabled?"already-enabled":"halt-not-set",
		 enabled?1u:0u,off?1u:0u,saved);
		return kIOReturnNotReady;
	}
	(void)gateNeighbours(true);
	IOQcomClockProvider *provider=IOQcomClockProvider::publishedProvider();
	uint32_t token=0;
	bool opened=false;
	IOReturn acquired=provider?provider->ledgerAcquire(kIOQcomCCGCC,kIOQcomGccGateLocalID,
	 "sweetclock-gate",&token,&opened):kIOReturnNotReady;
	if (acquired!=kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-gate phase=request result=%d detail=ledger-refused writes=%u\n",
		 resource,acquired,fWrites);
		return acquired;
	}
	if (!opened) {
		bool lastHolder=false;
		(void)provider->ledgerRelease(token,false,&lastHolder);
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=cc%u:0x%x consumer=sweetclock-gate phase=transition result=%d token=%u owner=0 detail=not-owner\n",
		 kIOQcomCCGCC,kIOQcomGccGateLocalID,kIOReturnNotReady,token);
		return kIOReturnNotReady;
	}
	SWEET_LOG_INFO("SWEET CLOCK: planID=C1 resource=cc%u:0x%x consumer=sweetclock-gate phase=transition result=0 token=%u owner=1\n",
	 kIOQcomCCGCC,kIOQcomGccGateLocalID,token);
	uint32_t target=saved|kGccBranchEnable;
	if (!writeRegister(window,offset,target,"gate-enable","sweetclock-gate")) {
		bool lastHolder=false;
		(void)provider->ledgerRelease(token,false,&lastHolder);
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-gate phase=request result=%d stage=gate-enable detail=store-failed writes=%u\n",
		 resource,kIOReturnNotReady,fWrites);
		return kIOReturnNotReady;
	}
	unsigned polls=0;
	uint32_t value=target;
	bool running=false,readFailed=false;
	for (polls=0;polls<kIOQcomGccGatePollTries;polls++) {
		if (!readRegister(window,offset,&value)) { readFailed=true; break; }
		if ((value&kGccBranchClockOff)==0) { running=true; break; }
		IODelay(kIOQcomGccGatePollDelayUs);
	}
	IOReturn parked=running?kIOReturnSuccess:
	 (readFailed?kIOReturnNotReady:kIOReturnTimeout);
	if (running) {
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-gate phase=running result=0 enable=%u off=0 value=%#x polls=%u\n",
		 resource,(value&kGccBranchEnable)?1u:0u,value,polls);
	} else {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-gate phase=running result=%d enable=%u off=%u value=%#x polls=%u detail=%s\n",
		 resource,parked,(value&kGccBranchEnable)?1u:0u,
		 (value&kGccBranchClockOff)?1u:0u,value,polls,
		 readFailed?"halt-read-failed":"halt-timeout");
	}
	fGateRunning=running;
	if (provider) (void)provider->ledgerComplete(token,running);
	if (running) {
		unsigned changed=gateNeighbours(false);
		if (changed) {
			SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-gate phase=gate-check result=%d neighbours=%u changed=%u detail=neighbour-changed\n",
			 resource,kIOReturnIOError,kIOQcomGccGateNeighbourCount,changed);
		} else {
			SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-gate phase=gate-check result=0 neighbours=%u changed=0\n",
			 resource,kIOQcomGccGateNeighbourCount);
		}
	}
	if (!running) {
		/* The store may have landed on a branch that will not come up. Leave the
		 * state as it is (the card asks for that), give the demand back and
		 * report the timeout; a disable here would be the unverified write
		 * C2-04 exists to design. */
		bool lastHolder=false;
		if (provider) (void)provider->ledgerRelease(token,false,&lastHolder);
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-gate phase=gate-done result=%d writes=%u neighbours=%u changed=%u polls=%u hold=0 detail=%s\n",
		 resource,parked,fWrites,kIOQcomGccGateNeighbourCount,fGateNeighbourChanges,
		 polls,readFailed?"halt-read-failed":"halt-timeout");
		return parked;
	}
	/* A neighbour that moved means the store's effect is not the one this card
	 * can account for. The branch is running and stays that way (there is no
	 * verified disable in this candidate), but the card's verdict is a failure
	 * and the caller reports it; the ring carries the before/after pair. */
	IOReturn verdict=fGateNeighbourChanges?kIOReturnIOError:kIOReturnSuccess;
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-gate phase=completed result=%d token=%u enable=1 off=0 detail=sample-kept-until-reboot\n",
	 resource,verdict,token);
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-gate phase=gate-done result=%d writes=%u neighbours=%u changed=%u polls=%u hold=1\n",
	 resource,verdict,fWrites,kIOQcomGccGateNeighbourCount,fGateNeighbourChanges,polls);
	return verdict;
}

/* C2-08 entry: save, acquire the XO/4 reference through the C1-05 ledger, store
 * the mux and the reference gate, measure the two declared samples, restore
 * every stored register, release the reference. Without the ADT gate nothing
 * here runs and no store exists. */
IOReturn
IOQcomGCC::measureClocks(void)
{
	if (!fMeasureEnabled) {
		SWEET_LOG_DEBUG("SWEET CLOCK: planID=C2 resource=cc%u consumer=sweetclock-measure phase=measure result=%d detail=gate-absent\n",
		 kIOQcomCCGCC,kIOReturnUnsupported);
		return kIOReturnUnsupported;
	}
	if (!fMapped||!fRegistered||!fLock) return kIOReturnNotReady;
	MeasureReg regs[kIOQcomGccMeasureRegCount];
	IOReturn worst=kIOReturnSuccess;
	IOReturn result=measureSave(regs,kIOQcomGccMeasureRegCount);
	if (result!=kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=cc%u consumer=sweetclock-measure phase=measure result=%d detail=save-failed writes=%u\n",
		 kIOQcomCCGCC,result,fWrites);
		return result;
	}
	IOQcomClockProvider *entry=IOQcomClockProvider::publishedProvider();
	uint32_t token=0;
	bool opened=false;
	IOReturn acquired=entry?entry->ledgerAcquire(kIOQcomCCGCC,kIOQcomGccMeasureReferenceLocalID,
	 "sweetclock-measure",&token,&opened):kIOReturnNotReady;
	if (acquired!=kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=cc%u:0x%x consumer=sweetclock-measure phase=measure result=%d detail=ledger-refused writes=%u\n",
		 kIOQcomCCGCC,kIOQcomGccMeasureReferenceLocalID,acquired,fWrites);
		return acquired;
	}
	SWEET_LOG_INFO("SWEET CLOCK: planID=C1 resource=cc%u:0x%x consumer=sweetclock-measure phase=transition result=0 token=%u owner=%u\n",
	 kIOQcomCCGCC,kIOQcomGccMeasureReferenceLocalID,token,opened?1u:0u);
	uint32_t observed=0;
	bool referenceRunning=false;
	/* Only the holder that opened the demand stores the enable; a shared holder
	 * inherits an already enabled branch and must not undo somebody else's. */
	if (opened&&!updateMeasure(regs,kMeasureXo4,0u,kGccBranchEnable,"ref-gate")) {
		worst=kIOReturnNotReady;
	} else if (!readRegister(kGccWindowXoDiv4,kGccXoDiv4Offset,&observed)) {
		worst=kIOReturnNotReady;
	} else {
		bool enabled=(observed&kGccBranchEnable)!=0;
		bool off=(observed&kGccBranchClockOff)!=0;
		referenceRunning=enabled&&!off;
		if (referenceRunning) {
			SWEET_LOG_INFO("SWEET CLOCK: planID=C1 resource=cc%u:0x%x consumer=sweetclock-measure phase=running result=0 enable=1 off=0 value=%#x\n",
			 kIOQcomCCGCC,kIOQcomGccMeasureReferenceLocalID,observed);
		} else {
			SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=cc%u:0x%x consumer=sweetclock-measure phase=running result=%d enable=%u off=%u value=%#x detail=reference-not-running\n",
			 kIOQcomCCGCC,kIOQcomGccMeasureReferenceLocalID,kIOReturnNotReady,enabled?1u:0u,off?1u:0u,observed);
			if (worst==kIOReturnSuccess) worst=kIOReturnNotReady;
		}
		if (entry) entry->ledgerComplete(token,referenceRunning);
	}
	if (referenceRunning) {
		/* Vendor order: mux select and post divider, then the debug branch. */
		if (!updateMeasure(regs,kMeasureMux,kGccDebugSrcSelMask,
		   kIOQcomGccMeasureRunningMux&kGccDebugSrcSelMask,"mux-select")
		 || !updateMeasure(regs,kMeasurePost,kGccDebugPostDivMask,
		   (kGccMeasurePostDiv-1u)&kGccDebugPostDivMask,"post-div")
		 || !updateMeasure(regs,kMeasureDebugGate,0u,kGccBranchEnable,"debug-gate")) {
			if (worst==kIOReturnSuccess) worst=kIOReturnNotReady;
		} else {
			IOReturn running=measureSample(regs,kIOQcomGccMeasureRunningLocalID,
			 kIOQcomGccMeasureRunningMux,true);
			IOReturn gated=measureSample(regs,kIOQcomGccMeasureOffLocalID,
			 kIOQcomGccMeasureOffMux,false);
			if (running!=kIOReturnSuccess&&worst==kIOReturnSuccess) worst=running;
			if (gated!=kIOReturnSuccess&&worst==kIOReturnSuccess) worst=gated;
			/* C2-03's branch is a third sample only when C2-03 really enabled
			 * it: mux 0x99 is the counter's own reference, and the C2-02 decoder
			 * states its rate independently (fixed XO/4 = 4.8 MHz), so this is
			 * the measurement evidence the gate card asks for. */
			if (fGateRunning) {
				IOReturn gate=measureSample(regs,kIOQcomGccGateLocalID,
				 kIOQcomGccGateMux,true);
				if (gate!=kIOReturnSuccess&&worst==kIOReturnSuccess) worst=gate;
			}
		}
	}
	IOReturn restored=measureRestore(regs,kIOQcomGccMeasureRegCount);
	uint32_t finalValue=0;
	bool stopped=false;
	if (readRegister(kGccWindowXoDiv4,kGccXoDiv4Offset,&finalValue)) {
		stopped=(finalValue&kGccBranchEnable)==0;
	}
	bool lastHolder=false;
	if (entry) (void)entry->ledgerRelease(token,stopped,&lastHolder);
	if (!stopped) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C1 resource=cc%u:0x%x consumer=sweetclock-measure phase=release result=%d value=%#x detail=reference-still-enabled\n",
		 kIOQcomCCGCC,kIOQcomGccMeasureReferenceLocalID,kIOReturnIOError,finalValue);
		if (worst==kIOReturnSuccess) worst=kIOReturnIOError;
	}
	if (restored!=kIOReturnSuccess&&worst==kIOReturnSuccess) worst=restored;
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=cc%u consumer=sweetclock-measure phase=measure-done result=%d writes=%u restores=%u mismatches=%u hold=%u\n",
	 kIOQcomCCGCC,worst,fWrites,kIOQcomGccMeasureRegCount,fRestoreMismatches,lastHolder?0u:1u);
	return worst;
}

bool
IOQcomGCC::start(IOService *provider)
{
	if (fLock||fMapped) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=already-started\n",
		 kIOReturnBusy);
		return false;
	}
	if (!provider||!super::start(provider)) return false;
	if (!validateProperties(provider)) return false;
	fLock=IOLockAlloc();
	if (!fLock) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=no-lock\n",
		 kIOReturnNoMemory);
		return false;
	}
	fReads=0;
	fReadFailures=0;
	fWrites=0;
	fWriteFailures=0;
	fRestoreMismatches=0;
	fGateRunning=false;
	fGateNeighbourChanges=0;
	if (!mapWindows(provider)) {
		unmapWindows();
		IOLockFree(fLock);
		fLock=NULL;
		return false;
	}
	fMapped=true;
	takeSnapshot();
	/* The entry and this backend are independent IOServices; IOKit does not
	 * order their start. Wait a bounded time for the entry to publish itself,
	 * then refuse with a single line instead of half-registering. */
	IOQcomClockProvider *entry=NULL;
	for (unsigned i=0;i<kIOQcomGccProviderWaitTries;i++) {
		entry=IOQcomClockProvider::publishedProvider();
		if (entry) break;
		IOSleep(kIOQcomGccProviderWaitMs);
	}
	if (!entry) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=provider-absent\n",
		 kIOReturnNotReady);
		unmapWindows();
		IOLockFree(fLock);
		fLock=NULL;
		return false;
	}
	IOReturn result=entry->registerController(this);
	if (result!=kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=%d detail=registration-refused\n",
		 kIOQcomCCGCC,result);
		unmapWindows();
		IOLockFree(fLock);
		fLock=NULL;
		return false;
	}
	fRegistered=true;
	decodeSamples();
	/* C2-03 before C2-08: the gate is the sample the measurement adds, and an
	 * enable that could not be verified must not be inherited by it silently. */
	IOReturn gated=gateClocks();
	if (gated!=kIOReturnSuccess&&gated!=kIOReturnUnsupported) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=0 detail=gate-incomplete gate-result=%d writes=%u\n",
		 kIOQcomCCGCC,gated,fWrites);
	}
	/* C2-08 last, so registration and the C2-02 decode exist before the first
	 * store and a failed measurement cannot leave a half-published backend. */
	IOReturn measured=measureClocks();
	if (measured!=kIOReturnSuccess&&measured!=kIOReturnUnsupported) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=0 detail=measure-incomplete measure-result=%d writes=%u\n",
		 kIOQcomCCGCC,measured,fWrites);
	}
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=0 windows=%u snapshot=%u reads=%u writes=%u variant=%#x\n",
	 kIOQcomCCGCC,kIOQcomGccWindowCount,kIOQcomGccSnapshotCount,fReads,fWrites,
	 kIOQcomGccVariantSm8150);
	return true;
}

void
IOQcomGCC::stop(IOService *provider)
{
	(void)provider;
	if (fRegistered) {
		IOQcomClockProvider *entry=IOQcomClockProvider::publishedProvider();
		if (entry) {
			IOReturn result=entry->withdrawController(this);
			SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=cc%u phase=stop result=%d reads=%u writes=%u\n",
			 kIOQcomCCGCC,result,fReads,fWrites);
		}
		fRegistered=false;
	}
	/* Stopping releases everything start acquired, so a later start is a
	 * real first start rather than a refused "already running". */
	unmapWindows();
	fMapped=false;
	if (fLock) {
		IOLockFree(fLock);
		fLock=NULL;
	}
	super::stop(provider);
}

void
IOQcomGCC::free(void)
{
	unmapWindows();
	if (fLock) {
		IOLockFree(fLock);
		fLock=NULL;
	}
	super::free();
}

IOReturn
IOQcomGCC::copyClockInfo(uint32_t localID,IOQcomClockInfo *info)
{
	if (!info) return kIOReturnBadArgument;
	const GccClock *entry=gccFind(localID);
	if (!entry) return kIOReturnNotFound;
	memset(info,0,sizeof *info);
	info->controller=kIOQcomCCGCC;
	info->localID=entry->localID;
	info->kind=entry->kind;
	info->halt=entry->halt;
	info->capabilities=entry->capabilities;
	info->domain=entry->domain;
	info->accessDeps=entry->accessDeps;
	info->possibleCount=entry->parents;
	for (unsigned i=0;i<entry->parents&&i<kIOQcomMaxParents;i++) info->possible[i]=entry->parent[i];
	info->pinned=entry->pinned;
	/* The enable bit observed before this candidate touched anything. */
	if (entry->snapshot<kIOQcomGccSnapshotCount&&fSnapshot[entry->snapshot].valid) {
		uint32_t value=(uint32_t)fSnapshot[entry->snapshot].value;
		info->inheritedOn=(value&(entry->kind==kIOQcomClockRCG?kGccCmdRootEn:kGccBranchEnable))!=0;
	}
	return kIOReturnSuccess;
}

/* The vendor table name, so a device log and the tree it came from use the
 * same identifier. */
bool
IOQcomGCC::copyClockName(uint32_t localID,char *buffer,unsigned size)
{
	const GccClock *entry=gccFind(localID);
	if (!entry||!buffer||!size) return false;
	snprintf(buffer,size,"%s",entry->name);
	return true;
}

/* Branch state: enable bit and CLK_OFF are read, halt trust follows the vendor
 * halt_check class (HALT_DELAY CLK_OFF is not evidence of a stopped clock).
 * A declared parent rate is used only when that source is fixed. */
IOReturn
IOQcomGCC::copyClockState(uint32_t localID,IOQcomClockState *state)
{
	if (!state) return kIOReturnBadArgument;
	const GccClock *entry=gccFind(localID);
	if (!entry) return kIOReturnNotFound;
	if (!fLock||!fMapped) return kIOReturnNotReady;
	memset(state,0,sizeof *state);
	uint32_t cmd=0,cfg=0;
	char resource[kIOQcomLogBuf];
	snprintf(resource,sizeof resource,"cc%u:0x%x",kIOQcomCCGCC,localID);
	if (entry->kind==kIOQcomClockRCG) {
		if (!readRegister(entry->window,entry->offset,&cmd)||
		 !readRegister(entry->window,entry->offset+kGccCfgRegOffset,&cfg)) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s phase=query result=%d detail=register-read-failed\n",
			 resource,kIOReturnNotReady);
			return kIOReturnNotReady;
		}
		state->raw[0]=cmd;
		state->raw[1]=cfg;
		state->updatePending=(cmd&kGccCmdUpdate)!=0;
		state->rootOff=(cmd&kGccCmdRootOff)!=0;
		state->enableBit=(cmd&kGccCmdRootEn)!=0;
		state->clockOffBit=state->rootOff;
		/* clk_rcg2 has no branch halt status: ROOT_OFF alone is not a halt
		 * poll and this candidate never claims a stopped clock from it. */
		state->haltTrusted=false;
		unsigned div=cfg&kGccCfgSrcDivMask;
		/* clk_rcg2_calc_rate divides only when the field is non-zero, so 0
		 * marks "no division encoded" rather than a divider of one. */
		state->divX2=div?(div+1):0;
		unsigned sel=(cfg>>kGccCfgSrcSelShift)&kGccCfgSrcSelMask;
		unsigned mode=(cfg>>kGccCfgModeShift)&kGccCfgModeMask;
		const IOQcomClockParent *match=NULL;
		for (unsigned i=0;i<entry->parents;i++) {
			if (entry->parent[i].localID==sel) { match=&entry->parent[i]; break; }
		}
		if (!match) {
			/* 7 is the test selector and 2..5 are unused in gcc_parent_map_0. */
			state->selected.source=kIOQcomSourceUnknown;
			state->selected.controller=0;
			state->selected.localID=sel;
			SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s phase=decode result=%d detail=src-invalid sel=%u raw=%#x,%#x\n",
			 resource,kIOReturnBadArgument,sel,cmd,cfg);
			return kIOReturnBadArgument;
		}
		state->selected=*match;
		if (mode!=0) {
			/* mnd_width is 0 for this sample: any mode field means the
			 * configuration is not the one this card declares. */
			SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s phase=decode result=%d detail=mode-unsupported mode=%u raw=%#x,%#x\n",
			 resource,kIOReturnBadArgument,mode,cmd,cfg);
			return kIOReturnBadArgument;
		}
		uint64_t parentHz=0;
		if (state->updatePending) {
			SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s phase=decode result=0 detail=update-pending sel=%u div=%u raw=%#x,%#x known=0\n",
			 resource,sel,div,cmd,cfg);
			return kIOReturnSuccess;
		}
		if (!gccFixedRate(*match,&parentHz)) {
			SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s phase=decode result=0 detail=parent-rate-unknown sel=%u div=%u raw=%#x,%#x known=0\n",
			 resource,sel,div,cmd,cfg);
			return kIOReturnSuccess;
		}
		state->configuredHz=state->divX2?(parentHz*2)/state->divX2:parentHz;
		state->rateKnown=true;
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s phase=decode result=0 detail=configured sel=%u div=%u div2=%u hz=%llu raw=%#x,%#x known=1\n",
		 resource,sel,div,state->divX2,(unsigned long long)state->configuredHz,cmd,cfg);
		return kIOReturnSuccess;
	}
	if (!readRegister(entry->window,entry->offset,&cmd)) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s phase=query result=%d detail=register-read-failed\n",
		 resource,kIOReturnNotReady);
		return kIOReturnNotReady;
	}
	state->raw[0]=cmd;
	state->enableBit=(cmd&kGccBranchEnable)!=0;
	state->clockOffBit=(cmd&kGccBranchClockOff)!=0;
	state->haltTrusted=(entry->halt==kIOQcomHaltCheck);
	if (entry->parents) {
		state->selected=entry->parent[0];
		uint64_t parentHz=0;
		if (gccFixedRate(entry->parent[0],&parentHz)) {
			state->configuredHz=parentHz;
			state->rateKnown=true;
		}
	}
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s phase=decode result=0 detail=%s enable=%u off=%u raw=%#x known=%u\n",
	 resource,state->rateKnown?"configured":"parent-rate-unknown",state->enableBit?1u:0u,
	 state->clockOffBit?1u:0u,cmd,state->rateKnown?1u:0u);
	return kIOReturnSuccess;
}

/* Start-time decode of the sample plus the inherited branches, so the device
 * evidence carries raw fields and the derivation basis instead of only a
 * value. debugcc comparison stays with C2-08. */
void
IOQcomGCC::decodeSamples(void)
{
	for (unsigned i=0;i<kGccClockCount;i++) {
		IOQcomClockState state;
		IOReturn result=copyClockState(gccClocks[i].localID,&state);
		if (result!=kIOReturnSuccess) continue;
		if (gccClocks[i].kind!=kIOQcomClockRCG) continue;
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=cc%u:0x%x name=%s phase=decode-sample result=0 hz=%llu known=%u\n",
		 kIOQcomCCGCC,gccClocks[i].localID,gccClocks[i].name,
		 (unsigned long long)state.configuredHz,state.rateKnown?1u:0u);
	}
}
#endif /* ARM64_BOARD_CONFIG_MH2LM */

