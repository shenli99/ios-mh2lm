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

/* The ADT reg order is the window ABI; these are the only mandatory windows and
 * the only lengths the backend accepts without an opt-in. */
static const struct { uint64_t physical; uint64_t length; } gccWindows[kIOQcomGccWindowCount] = {
	{ 0x0010b000ULL, 0x1000ULL },   /* display block: inherited/pinned branches */
	{ 0x00133000ULL, 0x1000ULL },   /* PDM block: the C2-02 sample */
	{ 0x00143000ULL, 0x1000ULL },   /* XO/4 debug reference branch (C2-08) */
	{ 0x00162000ULL, 0x1000ULL },   /* debug mux block: C2-08 will own it */
};
/* C2-06: the one optional page, at the end of the ADT reg list and only present
 * when sweet,clock-vote-probe arms it. Vendor offset 0x4d000 is the NPU block:
 * the AHB config branch at 0x4d004 and the three branches the vendor declares
 * BRANCH_VOTED at 0x4d008, 0x4d00c and 0x4d010. It is kept apart from the table
 * above because a tree without the property must not see a fifth window. */
enum : uint64_t { kGccProbeWindowPhysical=0x0014d000ULL, kGccProbeWindowLength=0x1000ULL };

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

/* C2-06 probe: the four CBCRs of the NPU page, in vendor offset order. Three of
 * them are the branches gcc-sm8150.c declares BRANCH_VOTED (vote in bit 0, halt
 * status in bit 31); the AHB config branch beside them is a plain halt branch
 * and is read as the page's control. The probe reports what they hold before
 * this candidate ever writes; it does not claim any of them. */
static const struct { uint32_t offset; const char *name; bool voted; }
 gccProbeRegs[kIOQcomGccProbeRegisterCount] = {
	{ 0x004, "gcc_npu_cfg_ahb_clk", false },
	{ 0x008, "gcc_npu_axi_clk", true },
	{ 0x00c, "gcc_npu_trig_clk", true },
	{ 0x010, "gcc_npu_at_clk", true },
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
	 * XO parent, no GDSC, and unused by the iOS device tree.
	 *
	 * "xo4" in the name is not a divider: the vendor branch has no parent and
	 * no divider, and 0130-1's counter read the gated branch four times faster
	 * than the XO/4 parent this entry used to declare. The /4 belongs to the
	 * debug counter's timebase alone (localID 7), which is what makes the
	 * corrected comparison a real check rather than a tautology. */
	{ "gcc_pdm_xo4_clk", kIOQcomGccGateLocalID, kIOQcomClockBranch, kIOQcomHaltCheck,
	  kIOQcomCapQueryConfig|kIOQcomCapQueryRate, 0, 0, 1,
	  {{kIOQcomSourceXO,0,0}},
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
	/* The C2-08 counter reference, declared as the branch it really is. This
	 * is the only XO/4 parent in the tree: gcc_pdm_xo4_clk is a plain XO gate,
	 * so the measurement demand and the C2-03 sample demand are two identities
	 * in both register and parent. */
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

/* The physical address of a window, including the optional C2-06 page. */
static uint64_t gccWindowPhysical(unsigned window)
{
	if (window<kIOQcomGccWindowCount) return gccWindows[window].physical;
	if (window==kIOQcomGccVoteWindow) return kGccProbeWindowPhysical;
	return 0;
}

static void gccName(unsigned window,uint32_t offset,char *out,size_t size)
{
	snprintf(out,size,"gcc:%#llx",(unsigned long long)(gccWindowPhysical(window)+offset));
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
	/* C2-04 is the same kind of opt-in again. It is refused at start unless the
	 * enable it retracts is also switched on: a disable of a branch this driver
	 * never opened would be a store aimed at a state it cannot explain. */
	fDisableEnabled=false;
	OSData *branchDisable=OSDynamicCast(OSData,provider->getProperty("sweet,clock-disable"));
	if (branchDisable) {
		if (branchDisable->getLength()!=4) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=bad-clock-disable length=%u\n",
			 kIOReturnBadArgument,(unsigned)branchDisable->getLength());
			return false;
		}
		uint32_t value=0;
		memcpy(&value,branchDisable->getBytesNoCopy(),4);
		if (value>1) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=bad-clock-disable value=%#x\n",
			 kIOReturnBadArgument,value);
			return false;
		}
		fDisableEnabled=value==1;
		haveGate=true;
	}
	/* C2-05 stands on both cards below it: it shares the enable C2-03 opens and
	 * ends the shared demand through the disable C2-04 verified. Pulling the
	 * switch without them would be a sharing pass with nothing to share, so it
	 * refuses at start rather than degrading into one of them silently. */
	fShareEnabled=false;
	OSData *branchShare=OSDynamicCast(OSData,provider->getProperty("sweet,clock-share"));
	if (branchShare) {
		if (branchShare->getLength()!=4) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=bad-clock-share length=%u\n",
			 kIOReturnBadArgument,(unsigned)branchShare->getLength());
			return false;
		}
		uint32_t value=0;
		memcpy(&value,branchShare->getBytesNoCopy(),4);
		if (value>1) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=bad-clock-share value=%#x\n",
			 kIOReturnBadArgument,value);
			return false;
		}
		fShareEnabled=value==1;
		haveGate=true;
	}
	/* The two implications are checked once, after every switch has been read,
	 * so the row names the newest switch that is armed instead of an older one
	 * that only happens to be missing its prerequisite too. */
	if (fShareEnabled&&(!fGateEnabled||!fDisableEnabled)) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=clock-share-needs-gate-disable clock-gate=%u clock-disable=%u clock-share=%u\n",
		 kIOReturnBadArgument,fGateEnabled?1u:0u,fDisableEnabled?1u:0u,fShareEnabled?1u:0u);
		return false;
	}
	if (fDisableEnabled&&!fGateEnabled) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=clock-disable-needs-gate clock-gate=%u clock-disable=%u\n",
		 kIOReturnBadArgument,fGateEnabled?1u:0u,fDisableEnabled?1u:0u);
		return false;
	}
	/* C2-06: the probe is a read-only opt-in of its own. It does not set
	 * haveGate, because it neither opens nor closes anything; it maps one more
	 * page and reads it. */
	fProbeEnabled=false;
	for (unsigned i=0;i<kIOQcomGccProbeRegisterCount;i++) fProbeValue[i]=0;
	OSData *voteProbe=OSDynamicCast(OSData,provider->getProperty("sweet,clock-vote-probe"));
	if (voteProbe) {
		if (voteProbe->getLength()!=4) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=bad-clock-vote-probe length=%u\n",
			 kIOReturnBadArgument,(unsigned)voteProbe->getLength());
			return false;
		}
		uint32_t value=0;
		memcpy(&value,voteProbe->getBytesNoCopy(),4);
		if (value>1) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=bad-clock-vote-probe value=%#x\n",
			 kIOReturnBadArgument,value);
			return false;
		}
		fProbeEnabled=value==1;
	}
	/* C2-06 write half: the vote needs the read-only page the probe maps, so the
	 * tree has to arm both and a vote without its probe is refused here rather
	 * than discovered as a missing window after the boot is half done. The
	 * fields are cleared first so an instance that was started, stopped and
	 * started again cannot inherit a vote it never placed. */
	fVoteEnabled=false;
	fVoteRunning=false;
	fVoteLeftSet=false;
	fVoteBefore=0;
	fVoteAfterRelease=0;
	OSData *vote=OSDynamicCast(OSData,provider->getProperty("sweet,clock-vote"));
	if (vote) {
		if (vote->getLength()!=4) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=bad-clock-vote length=%u\n",
			 kIOReturnBadArgument,(unsigned)vote->getLength());
			return false;
		}
		uint32_t value=0;
		memcpy(&value,vote->getBytesNoCopy(),4);
		if (value>1) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=bad-clock-vote value=%#x\n",
			 kIOReturnBadArgument,value);
			return false;
		}
		fVoteEnabled=value==1;
	}
	if (fVoteEnabled&&!fProbeEnabled) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=clock-vote-needs-probe clock-vote=%u clock-vote-probe=%u\n",
		 kIOReturnBadArgument,fVoteEnabled?1u:0u,fProbeEnabled?1u:0u);
		return false;
	}
	if (fVoteEnabled) {
		/* The arm marker is its own row and not a field on the start row: a boot
		 * whose vote pass dies before its first store still shows that the image
		 * was armed, and an image that arms nothing keeps every row it had. */
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=gcc:%#llx phase=vote result=0 detail=armed name=gcc_npu_axi_clk offset=%#x voted=1 vote-probe=1\n",
		 (unsigned long long)kGccProbeWindowPhysical,kIOQcomGccVoteOffset);
	}
	/* C2-07: the reset arm. It borrows the probe's page but not the probe's
	 * switch - the page is mapped by either card, the four probe rows are not -
	 * and it is mutually exclusive with the vote, because a boot that both voted
	 * on and reset a block would leave two cards' stores in one ring with no way
	 * to attribute a change to either. One boot walks one write contract (plan
	 * section: a candidate kernel changes the hardware behaviour of exactly one
	 * card under acceptance). */
	fResetEnabled=false;
	fResetDone=false;
	fResetBefore=0;
	fResetAfter=0;
	OSData *branchReset=OSDynamicCast(OSData,provider->getProperty("sweet,clock-reset"));
	if (branchReset) {
		if (branchReset->getLength()!=4) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=bad-clock-reset length=%u\n",
			 kIOReturnBadArgument,(unsigned)branchReset->getLength());
			return false;
		}
		uint32_t value=0;
		memcpy(&value,branchReset->getBytesNoCopy(),4);
		if (value>1) {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=bad-clock-reset value=%#x\n",
			 kIOReturnBadArgument,value);
			return false;
		}
		fResetEnabled=value==1;
	}
	if (fResetEnabled&&fVoteEnabled) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=clock-reset-needs-no-vote clock-reset=%u clock-vote=%u\n",
		 kIOReturnBadArgument,fResetEnabled?1u:0u,fVoteEnabled?1u:0u);
		return false;
	}
	if (fResetEnabled&&fProbeEnabled) {
		/* Both cards want the same page mapped, and there is one map row to say
		 * why it is there: one boot therefore arms one of them. The pair would
		 * also print the probe's read-only rows and the reset's stores into one
		 * ring, which no audit profile can attribute to either card. */
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=gcc phase=start result=%d detail=clock-reset-needs-no-probe clock-reset=%u clock-vote-probe=%u\n",
		 kIOReturnBadArgument,fResetEnabled?1u:0u,fProbeEnabled?1u:0u);
		return false;
	}
	if (fResetEnabled) {
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=gcc:%#llx phase=reset result=0 detail=armed name=gcc_npu reset-id=%u offset=%#x bit=%u pulse-us=%u\n",
		 (unsigned long long)kGccProbeWindowPhysical,kIOQcomGccResetLocalID,
		 kIOQcomGccResetOffset,kIOQcomGccResetBit,kIOQcomGccResetPulseUs);
	}
	if (haveGate) {
		/* The ring written before this candidate keeps its exact row: the share
		 * field appears only on an image that arms the sharing pass, so an
		 * already-pulled C2-04 ring stays byte-identical to what its auditor
		 * expects. */
		if (fShareEnabled) {
			SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=gcc phase=gate result=0 clock-measure=%u clock-gate=%u clock-disable=%u clock-share=%u\n",
			 fMeasureEnabled?1u:0u,fGateEnabled?1u:0u,fDisableEnabled?1u:0u,fShareEnabled?1u:0u);
		} else {
			SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=gcc phase=gate result=0 clock-measure=%u clock-gate=%u clock-disable=%u\n",
			 fMeasureEnabled?1u:0u,fGateEnabled?1u:0u,fDisableEnabled?1u:0u);
		}
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
	return mapProbeWindow(provider);
}

/* C2-06: the optional page gets the same four refusals the mandatory ones get,
 * each with its own detail so a wrong tree names the page it got wrong. */
bool
IOQcomGCC::mapProbeWindow(IOService *provider)
{
	/* C2-07 borrows the same page: the reset card's BCR is word 0 of it, so an
	 * image that arms the reset alone still maps the page - without the probe
	 * rows, which stay the probe's own. */
	if (!fProbeEnabled&&!fResetEnabled) return true;
	Window &w=fWindow[kIOQcomGccVoteWindow];
	w.physical=kGccProbeWindowPhysical;
	w.length=0;
	w.base=0;
	w.map=provider?provider->mapDeviceMemoryWithIndex(kIOQcomGccVoteWindow):NULL;
	char resource[kIOQcomLogBuf];
	gccName(kIOQcomGccVoteWindow,0,resource,sizeof resource);
	if (!w.map) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s phase=map result=%d detail=window-absent\n",
		 resource,kIOReturnNotReady);
		return false;
	}
	uint64_t observed=w.map->getPhysicalAddress();
	uint64_t length=w.map->getLength();
	if (observed!=kGccProbeWindowPhysical) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s phase=map result=%d detail=address-mismatch observed=%#llx\n",
		 resource,kIOReturnBadArgument,(unsigned long long)observed);
		return false;
	}
	if (length<kGccProbeWindowLength) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s phase=map result=%d detail=length-short observed=%#llx required=%#llx\n",
		 resource,kIOReturnBadArgument,(unsigned long long)length,
		 (unsigned long long)kGccProbeWindowLength);
		return false;
	}
	w.length=length;
	w.base=w.map->getVirtualAddress();
	if (!w.base) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s phase=map result=%d detail=no-virtual-base\n",
		 resource,kIOReturnNotReady);
		return false;
	}
	/* The row names whichever card borrowed the page: the probe's opt-in also
	 * covers the vote (which implies it), the reset card maps it on its own. */
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s phase=map result=0 length=%#llx offset=%#llx %s\n",
	 resource,(unsigned long long)length,
	 (unsigned long long)(kGccProbeWindowPhysical-kIOQcomGccBase),
	 fProbeEnabled?"vote-probe=1":"reset=1");
	return true;
}

void
IOQcomGCC::unmapWindows(void)
{
	for (unsigned i=0;i<kIOQcomGccMappedWindowCount;i++) {
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
	if (!value||window>=kIOQcomGccMappedWindowCount||!fLock) return false;
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

/* C2-06 precondition. One page, four reads, no store and no ledger entry: the
 * ring records which vote bits are already set before this candidate speaks,
 * so the vote card is designed against the state the device really boots in
 * rather than the state the vendor source suggests. */
IOReturn
IOQcomGCC::probeVoteSample(void)
{
	if (!fProbeEnabled) return kIOReturnUnsupported;
	unsigned held=0, clear=0, off=0, failed=0, votedRegs=0;
	for (unsigned i=0;i<kIOQcomGccProbeRegisterCount;i++) {
		const uint32_t offset=gccProbeRegs[i].offset;
		uint32_t value=0;
		bool ok=readRegister(kIOQcomGccVoteWindow,offset,&value);
		fProbeValue[i]=ok?value:0;
		char resource[kIOQcomLogBuf];
		gccName(kIOQcomGccVoteWindow,offset,resource,sizeof resource);
		if (!ok) {
			failed++;
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s phase=probe result=%d detail=read-failed\n",
			 resource,kIOReturnNotReady);
			continue;
		}
		const unsigned enable=(value&kGccBranchEnable)?1u:0u;
		const unsigned halt=(value&kGccBranchClockOff)?1u:0u;
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s phase=probe result=0 name=%s voted=%u enable=%u clk-off=%u value=%#x\n",
		 resource,gccProbeRegs[i].name,gccProbeRegs[i].voted?1u:0u,enable,halt,value);
		if (gccProbeRegs[i].voted) {
			votedRegs++;
			if (enable) held++; else clear++;
			if (halt) off++;
		}
	}
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=cc%u phase=probe-sample result=0 voted=%u enable-set=%u enable-clear=%u clk-off=%u failed=%u reads=%u writes=%u\n",
	 kIOQcomCCGCC,votedRegs,held,clear,off,failed,fReads,fWrites);
	/* Observation, not a gate: either baseline is worth a round. An enable bit
	 * already set is a vote this driver did not place, which makes the "the
	 * aggregate stays on" half observable; a clear one is the single-voter case
	 * the card's own release proof needs. A read that failed is the only
	 * failure, because it means the page is not what the card assumes. */
	return failed?kIOReturnNotReady:kIOReturnSuccess;
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
	/* C2-06 write half: the fifth page is writable only while the vote is armed,
	 * and only its one vote register reaches this path at all. An image that
	 * carries the probe but not the vote keeps a store set inside the four
	 * mandatory windows, which is what makes the probe ring a zero-write ring. */
	const bool inWindow=window<kIOQcomGccWindowCount||
	 (fVoteEnabled&&window==kIOQcomGccVoteWindow&&offset==kIOQcomGccVoteOffset)||
	 (fResetEnabled&&window==kIOQcomGccVoteWindow&&offset==kIOQcomGccResetOffset);
	if (inWindow&&w.base&&(offset&3u)==0&&
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
IOQcomGCC::writeVoteRegister(uint32_t offset,uint32_t value,const char *stage)
{
	/* The refusal is a row and not a silent false: a vote store attempted on an
	 * image that did not arm it is a driver bug, and the ring should name it
	 * instead of leaving a write ordinal that has no owner. */
	if (!fVoteEnabled||offset!=kIOQcomGccVoteOffset) {
		if (fLock) { IOLockLock(fLock); fWriteFailures++; IOLockUnlock(fLock); }
		char resource[kIOQcomLogBuf];
		gccName(kIOQcomGccVoteWindow,offset,resource,sizeof resource);
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-vote phase=write-issued result=%d stage=%s detail=vote-unarmed\n",
		 resource,kIOReturnBadArgument,stage);
		return false;
	}
	return writeRegister(kIOQcomGccVoteWindow,offset,value,stage,"sweetclock-vote");
}

bool
IOQcomGCC::writeResetRegister(uint32_t offset,uint32_t value,const char *stage)
{
	/* Same refusal shape as the vote's store path: a reset store on an image
	 * that did not arm it is a driver bug and gets a row, not a silent false. */
	if (!fResetEnabled||offset!=kIOQcomGccResetOffset) {
		if (fLock) { IOLockLock(fLock); fWriteFailures++; IOLockUnlock(fLock); }
		char resource[kIOQcomLogBuf];
		gccName(kIOQcomGccVoteWindow,offset,resource,sizeof resource);
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=write-issued result=%d stage=%s detail=reset-unarmed\n",
		 resource,kIOReturnBadArgument,stage);
		return false;
	}
	return writeRegister(kIOQcomGccVoteWindow,offset,value,stage,"sweetclock-reset");
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
IOQcomGCC::gateNeighbours(bool capture, const char *consumer)
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
			SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=neighbours result=%d detail=changed before=%#x after=%#x mask=%#x\n",
			 resource,consumer,kIOReturnIOError,fGateNeighbour[i].value&mask,value&mask,mask);
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
	(void)gateNeighbours(true,"sweetclock-gate");
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
	/* The demand is this driver's from here on: C2-04 hands it back only after
	 * the hardware reports the branch halted. */
	fGateToken=token;
	fGateTokenHeld=true;
	uint32_t target=saved|kGccBranchEnable;
	if (!writeRegister(window,offset,target,"gate-enable","sweetclock-gate")) {
		bool lastHolder=false;
		(void)provider->ledgerRelease(token,false,&lastHolder);
		fGateToken=0;
		fGateTokenHeld=false;
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
		unsigned changed=gateNeighbours(false,"sweetclock-gate");
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
		fGateToken=0;
		fGateTokenHeld=false;
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

/* C2-04/C2-05 retraction. The card makes the disable a claim the hardware has to
 * back rather than a second store with a nicer comment: the branch must be the
 * one this driver left running, the store must clear the enable bit alone, and
 * CLK_OFF has to rise within the same bound the enable waits for it to fall.
 * Only then is the ledger demand handed back, and handed back with stopped=true
 * so the ledger records "satisfied" instead of the "still running, owed" it
 * would record for a bare release.
 *
 * The token is a parameter because C2-05 may end a demand it did not open: the
 * last holder of a shared branch retracts the enable a different consumer
 * stored, and the hardware contract is the same whichever release got there.
 * The consumer and stage name the rows, so the ring can still attribute the
 * single store to the holder that caused it.
 *
 * A timeout keeps both the demand and the branch: nothing is reset to make a
 * failing row read better, and the clock is still running under a demand the
 * ledger still holds, which is exactly the state C2-03 leaves. A neighbour that
 * moved is treated the same way: the halt happened, but this driver cannot
 * account for what else the store touched, so it keeps the demand, says so, and
 * does not re-enable.
 *
 * The success path ends by running the enable this candidate already verified,
 * so one boot proves enable, disable, halt and recovery - and the branch is
 * running again, which is the state the C2-03 baseline leaves for the rest of
 * the boot. */
IOReturn
IOQcomGCC::retractBranch(uint32_t token, const char *consumer, const char *stage)
{
	if (!fMapped||!fRegistered||!fLock||!token||!consumer||!stage) return kIOReturnNotReady;
	IOQcomClockProvider *provider=IOQcomClockProvider::publishedProvider();
	if (!provider) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=cc%u:0x%x consumer=%s phase=retract result=%d detail=provider-absent hold=0\n",
		 kIOQcomCCGCC,kIOQcomGccGateLocalID,consumer,kIOReturnNotReady);
		return kIOReturnNotReady;
	}
	const GccClock *entry=gccFind(kIOQcomGccGateLocalID);
	if (!entry) return kIOReturnNotFound;
	const unsigned window=entry->window;
	const uint32_t offset=entry->offset;
	char resource[kIOQcomLogBuf];
	gccName(window,offset,resource,sizeof resource);
	uint32_t saved=0;
	if (!readRegister(window,offset,&saved)) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=disable-request result=%d detail=read-failed\n",
		 resource,consumer,kIOReturnNotReady);
		return kIOReturnNotReady;
	}
	bool enabled=(saved&kGccBranchEnable)!=0;
	bool off=(saved&kGccBranchClockOff)!=0;
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s name=%s consumer=%s phase=disable-request result=0 enable=%u off=%u value=%#x\n",
	 resource,entry->name,consumer,enabled?1u:0u,off?1u:0u,saved);
	/* A branch that is already halted, or one that is not the enable this
	 * driver left behind, is not something this store can retract: the row
	 * would claim a transition that never happened. */
	if (!enabled||off) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=disable-request result=%d detail=%s enable=%u off=%u value=%#x\n",
		 resource,consumer,kIOReturnNotReady,off?"already-halted":"not-enabled",
		 enabled?1u:0u,off?1u:0u,saved);
		return kIOReturnNotReady;
	}
	(void)gateNeighbours(true,consumer);
	/* Only the enable bit is cleared: the divider fields and the halt status
	 * belong to the hardware, and a read-modify-write that dropped one of them
	 * would be a write this card cannot justify. */
	uint32_t target=saved&~kGccBranchEnable;
	if (!writeRegister(window,offset,target,stage,consumer)) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=disable-request result=%d stage=%s detail=store-failed writes=%u\n",
		 resource,consumer,kIOReturnNotReady,stage,fWrites);
		return kIOReturnNotReady;
	}
	unsigned polls=0;
	uint32_t value=target;
	bool halted=false,readFailed=false;
	for (polls=0;polls<kIOQcomGccDisablePollTries;polls++) {
		if (!readRegister(window,offset,&value)) { readFailed=true; break; }
		if ((value&kGccBranchClockOff)!=0) { halted=true; break; }
		IODelay(kIOQcomGccDisablePollDelayUs);
	}
	if (!halted) {
		/* The branch did not report halted. The demand stays where it is and the
		 * branch is left alone: undoing the store here would be the unverified
		 * write this card exists to rule out. */
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=halt result=%d enable=%u off=%u value=%#x polls=%u writes=%u hold=1 detail=%s\n",
		 resource,consumer,readFailed?kIOReturnNotReady:kIOReturnTimeout,
		 (value&kGccBranchEnable)?1u:0u,(value&kGccBranchClockOff)?1u:0u,value,polls,fWrites,
		 readFailed?"halt-read-failed":"halt-timeout");
		return readFailed?kIOReturnNotReady:kIOReturnTimeout;
	}
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=halt result=0 enable=%u off=1 value=%#x polls=%u\n",
	 resource,consumer,(value&kGccBranchEnable)?1u:0u,value,polls);
	unsigned changed=gateNeighbours(false,consumer);
	if (changed) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=disable-check result=%d neighbours=%u changed=%u detail=neighbour-changed\n",
		 resource,consumer,kIOReturnIOError,kIOQcomGccGateNeighbourCount,changed);
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=disable-done result=%d writes=%u neighbours=%u changed=%u polls=%u hold=1 detail=neighbour-changed\n",
		 resource,consumer,kIOReturnIOError,fWrites,kIOQcomGccGateNeighbourCount,changed,polls);
		return kIOReturnIOError;
	}
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=disable-check result=0 neighbours=%u changed=0\n",
	 resource,consumer,kIOQcomGccGateNeighbourCount);
	/* The halt verdict is the licence for this release: stopped=true, so the
	 * ledger clears the demand it was still owed and counts the confirmation. */
	bool lastHolder=false;
	IOReturn released=provider->ledgerRelease(token,true,&lastHolder);
	if (released!=kIOReturnSuccess) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=disable-done result=%d writes=%u neighbours=%u changed=0 polls=%u hold=1 detail=ledger-release-failed\n",
		 resource,consumer,released,fWrites,kIOQcomGccGateNeighbourCount,polls);
		return released;
	}
	fGateToken=0;
	fGateTokenHeld=false;
	fGateRunning=false;
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=disable-done result=0 writes=%u neighbours=%u changed=0 polls=%u hold=0 last-holder=%u detail=sample-halted\n",
	 resource,consumer,fWrites,kIOQcomGccGateNeighbourCount,polls,lastHolder?1u:0u);
	/* Recovery: the verified enable again, from the halted state this card just
	 * proved. Its rows are the second gate pass in this ring. */
	IOReturn recovered=gateClocks();
	if (recovered!=kIOReturnSuccess) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=reenable result=%d writes=%u detail=reenable-incomplete\n",
		 resource,consumer,recovered,fWrites);
	}
	return recovered;
}

/* C2-04 entry: hand back the demand C2-03 opened. The guards live here rather
 * than in the retraction, because a shared release (C2-05) ends the demand of a
 * consumer that never owned the gate token, while this one must only ever undo
 * the enable this very driver stored in this very boot. */
IOReturn
IOQcomGCC::disableClocks(void)
{
	if (!fDisableEnabled) {
		SWEET_LOG_DEBUG("SWEET CLOCK: planID=C2 resource=cc%u:0x%x consumer=sweetclock-disable phase=disable result=%d detail=disable-absent\n",
		 kIOQcomCCGCC,kIOQcomGccGateLocalID,kIOReturnUnsupported);
		return kIOReturnUnsupported;
	}
	if (!fMapped||!fRegistered||!fLock) return kIOReturnNotReady;
	IOQcomClockProvider *provider=IOQcomClockProvider::publishedProvider();
	if (!provider||!fGateTokenHeld||!fGateToken) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=cc%u:0x%x consumer=sweetclock-disable phase=disable result=%d detail=%s %s\n",
		 kIOQcomCCGCC,kIOQcomGccGateLocalID,kIOReturnNotReady,
		 provider?"no-token":"provider-absent","hold=0");
		return kIOReturnNotReady;
	}
	return retractBranch(fGateToken,"sweetclock-disable","disable");
}

/* C2-05: the shared demand. Two consumers take the branch the gate opened, then
 * the gate gives its own reference back. Nothing is stored while any of them
 * still holds the clock: the row after each release reads the CBCR and shows
 * the branch still enabled, which is the whole claim of the card - a release
 * that is not the last one must not move the hardware.
 *
 * The last holder is a different consumer than the one that opened the clock,
 * so the retraction below cannot be justified by "the driver that enabled it
 * now disables it". It is justified by the ledger: holders==1, this is the last
 * demand, and the hardware contract of C2-04 does the rest. That is also why the
 * pass refuses to run at all when the ledger does not recognise the gate's
 * demand: an inherited clock with zero references is not this driver's to turn
 * off, and the row says which arm refused. */
IOReturn
IOQcomGCC::shareClocks(void)
{
	if (!fShareEnabled) {
		SWEET_LOG_DEBUG("SWEET CLOCK: planID=C2 resource=cc%u:0x%x consumer=sweetclock-share phase=share result=%d detail=share-absent\n",
		 kIOQcomCCGCC,kIOQcomGccGateLocalID,kIOReturnUnsupported);
		return kIOReturnUnsupported;
	}
	if (!fMapped||!fRegistered||!fLock) return kIOReturnNotReady;
	IOQcomClockProvider *provider=IOQcomClockProvider::publishedProvider();
	if (!provider||!fGateTokenHeld||!fGateToken) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=cc%u:0x%x consumer=sweetclock-share phase=share result=%d detail=%s %s\n",
		 kIOQcomCCGCC,kIOQcomGccGateLocalID,kIOReturnNotReady,
		 provider?"no-token":"provider-absent","hold=0");
		return kIOReturnNotReady;
	}
	const GccClock *entry=gccFind(kIOQcomGccGateLocalID);
	if (!entry) return kIOReturnNotFound;
	const unsigned window=entry->window;
	const uint32_t offset=entry->offset;
	char resource[kIOQcomLogBuf];
	gccName(window,offset,resource,sizeof resource);
	uint32_t value=0;
	if (!readRegister(window,offset,&value)) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-share phase=share-request result=%d detail=read-failed hold=1\n",
		 resource,kIOReturnNotReady);
		return kIOReturnNotReady;
	}
	bool enabled=(value&kGccBranchEnable)!=0;
	bool off=(value&kGccBranchClockOff)!=0;
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s name=%s consumer=sweetclock-share phase=share-request result=0 enable=%u off=%u value=%#x\n",
	 resource,entry->name,enabled?1u:0u,off?1u:0u,value);
	/* Sharing a clock nobody is running would prove nothing: two holders of a
	 * halted branch are two holders of nothing. */
	if (!enabled||off) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-share phase=share-request result=%d detail=%s enable=%u off=%u value=%#x hold=1\n",
		 resource,kIOReturnNotReady,off?"already-halted":"not-enabled",
		 enabled?1u:0u,off?1u:0u,value);
		return kIOReturnNotReady;
	}
	/* The public ledger is the only way this pass takes a reference: a consumer
	 * that bypassed it could not be seen by the release that ends the demand. */
	unsigned taken=0;
	IOReturn acquired=kIOReturnSuccess;
	for (;taken<kIOQcomGccShareConsumers;) {
		uint32_t token=0;
		bool opened=false;
		acquired=provider->ledgerAcquire(kIOQcomCCGCC,kIOQcomGccGateLocalID,
		 gccShareConsumers[taken],&token,&opened);
		if (acquired!=kIOReturnSuccess) break;
		fShareToken[taken]=token;
		/* Counted as taken before anything can refuse it: a reference the ledger
		 * issued must be handed back even when this pass then rejects it. */
		taken++;
		IOQcomClockLedgerState state;
		unsigned holders=0;
		if (provider->ledgerInspect(kIOQcomCCGCC,kIOQcomGccGateLocalID,0,&state)==kIOReturnSuccess) {
			holders=state.holders;
		}
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=share-acquire result=0 token=%u holders=%u opened=%u\n",
		 resource,gccShareConsumers[taken-1],token,holders,opened?1u:0u);
		/* The ledger hands out a transition flag only when it had no holder for
		 * this clock. The gate opened this branch in this boot, so a flag here
		 * means the demand being shared is not the verified one. */
		if (opened) {
			acquired=kIOReturnNotReady;
			SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=share-acquire result=%d token=%u detail=ledger-lost-owner hold=1\n",
			 resource,gccShareConsumers[taken-1],kIOReturnNotReady,token);
			break;
		}
	}
	if (acquired!=kIOReturnSuccess) {
		/* Hand back exactly what this pass took, with stopped=false: the branch
		 * is still running, so the release is not a halt verdict. */
		for (unsigned i=0;i<taken;i++) {
			bool last=false;
			(void)provider->ledgerRelease(fShareToken[i],false,&last);
			fShareToken[i]=0;
		}
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-share phase=share result=%d detail=acquire-failed released=%u hold=1\n",
		 resource,acquired,taken);
		return acquired;
	}
	IOQcomClockLedgerState state;
	fShareHolders=0;
	if (provider->ledgerInspect(kIOQcomCCGCC,kIOQcomGccGateLocalID,0,&state)==kIOReturnSuccess) {
		fShareHolders=state.holders;
	}
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-share phase=share result=0 consumers=%u holders=%u clock-share=1 clock-gate=1 clock-disable=1\n",
	 resource,kIOQcomGccShareConsumers,fShareHolders);
	/* The gate lets go of a clock it opened: the demand was real, but it is not
	 * the one keeping the clock on any more. Every non-last release here reads
	 * the CBCR first and logs what it saw, so "still running, no store" is a row
	 * and not an assumption. The gate's token is the first one handed back; the
	 * first shared consumer is the second. */
	uint32_t releasedToken[kIOQcomGccShareConsumers];
	const char *releasedConsumer[kIOQcomGccShareConsumers];
	releasedToken[0]=fGateToken;releasedConsumer[0]="sweetclock-gate";
	releasedToken[1]=fShareToken[0];releasedConsumer[1]=gccShareConsumers[0];
	for (unsigned i=0;i<kIOQcomGccShareConsumers;i++) {
		uint32_t seen=0;
		if (!readRegister(window,offset,&seen)) {
			SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=share-release result=%d detail=read-failed token=%u hold=1\n",
			 resource,releasedConsumer[i],kIOReturnNotReady,releasedToken[i]);
			return kIOReturnNotReady;
		}
		bool stillEnabled=(seen&kGccBranchEnable)!=0;
		bool stillOff=(seen&kGccBranchClockOff)!=0;
		/* A release that is not the last one must leave the clock running: if it
		 * is not, the ledger cannot be trusted about who holds what, and closing
		 * more demand would hide that. */
		if (!stillEnabled||stillOff) {
			SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=share-release result=%d detail=%s enable=%u off=%u value=%#x token=%u hold=1\n",
			 resource,releasedConsumer[i],kIOReturnNotReady,
			 stillOff?"branch-halted":"branch-not-enabled",
			 stillEnabled?1u:0u,stillOff?1u:0u,seen,releasedToken[i]);
			return kIOReturnNotReady;
		}
		bool last=false;
		IOReturn released=provider->ledgerRelease(releasedToken[i],false,&last);
		if (released!=kIOReturnSuccess) {
			SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=share-release result=%d token=%u detail=ledger-release-failed hold=1\n",
			 resource,releasedConsumer[i],released,releasedToken[i]);
			return released;
		}
		if (i==0) {
			/* The gate's demand is gone; the shared consumers hold the branch. */
			fGateToken=0;
			fGateTokenHeld=false;
		} else {
			fShareToken[0]=0;
		}
		unsigned holders=0;
		if (provider->ledgerInspect(kIOQcomCCGCC,kIOQcomGccGateLocalID,0,&state)==kIOReturnSuccess) {
			holders=state.holders;
		}
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=share-release result=0 token=%u holders=%u last-holder=%u running=1 value=%#x\n",
		 resource,releasedConsumer[i],releasedToken[i],holders,last?1u:0u,seen);
		if (last) {
			/* The ledger says this release ended the demand while the branch is
			 * still enabled: the pass expected one more holder, so it says so
			 * instead of retracting on a count it cannot explain. */
			SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=share result=%d detail=unexpected-last-holder token=%u holders=%u hold=1\n",
			 resource,releasedConsumer[i],kIOReturnIOError,releasedToken[i],holders);
			return kIOReturnIOError;
		}
	}
	/* The last holder is the consumer that must not have been able to turn the
	 * clock off by dropping its own request - only by being the last one. */
	uint32_t lastToken=fShareToken[kIOQcomGccShareConsumers-1];
	unsigned holders=0;
	if (provider->ledgerInspect(kIOQcomCCGCC,kIOQcomGccGateLocalID,0,&state)==kIOReturnSuccess) {
		holders=state.holders;
	}
	fShareHolders=holders;
	if (holders!=1) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=share-last result=%d detail=holders-mismatch holders=%u token=%u hold=1\n",
		 resource,gccShareConsumers[kIOQcomGccShareConsumers-1],kIOReturnNotReady,holders,lastToken);
		return kIOReturnNotReady;
	}
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=%s phase=share-last result=0 token=%u holders=1 action=retract\n",
	 resource,gccShareConsumers[kIOQcomGccShareConsumers-1],lastToken);
	IOReturn verdict=retractBranch(lastToken,gccShareConsumers[kIOQcomGccShareConsumers-1],"share-disable");
	if (verdict!=kIOReturnSuccess) {
		/* The retraction kept the demand and the branch, so this holder is still
		 * the last reference; the row above said which stage refused. */
		return verdict;
	}
	fShareToken[kIOQcomGccShareConsumers-1]=0;
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-share phase=share-done result=0 writes=%u consumers=%u retracted=1 last-holder=1 holders=0 detail=last-release-retracted\n",
	 resource,fWrites,kIOQcomGccShareConsumers);
	return kIOReturnSuccess;
}

/* C2-06 write half: the vote.
 *
 * gcc_npu_axi_clk is a BRANCH_VOTED branch, and "voted" is the whole reason this
 * card reports three facts instead of one verdict. In the vendor tree
 * (clk-branch.h:60-140) BRANCH_VOTED turns clk_branch_wait() into: on enable,
 * poll CLK_OFF clear for at most 500 x 1 us and warn if it does not clear; on
 * *disable*, do one udelay(10) and return success without looking at CLK_OFF at
 * all. The hardware says nothing when a vote is withdrawn, because the vote is a
 * statement about one consumer's need and not about the aggregate.
 *
 * So this pass claims exactly three things:
 *
 *  1. vote-write      the store landed, and the CBCR read back differs from the
 *                     saved value in bit 0 only (config bits compared under
 *                     kIOQcomGccBranchConfigMask, so the FSM-owned halt status
 *                     moving is not counted as a change this driver made);
 *  2. enable-done     CLK_OFF cleared inside the vendor's own 500 us. The 0137
 *                     probe read 0x80000000 on all three voted branches, so the
 *                     aggregate really was off and this is the first observation
 *                     of it coming up;
 *  3. release-settle  bit 0 went back to 0, one 10 us delay, one read. Whatever
 *                     CLK_OFF says afterwards is printed as an observation and
 *                     never as proof: with a single voter it should be 1, but a
 *                     second voter (NPU power collapse, RPMh) would hold the
 *                     aggregate up and this driver cannot see that vote. The
 *                     card does not claim the aggregate is off.
 *
 * Two states refuse before the first store, both of them real: bit 0 already set
 * is a vote this driver did not place (a second holder, and the case the 0137
 * baseline rules out but a later boot may not), and bit 31 already clear while
 * our own vote is clear is an aggregate somebody else is holding up.
 *
 * No ledger entry. A C1 token models "a consumer needs this clock" with exactly
 * two release outcomes - the clock stopped (satisfied) and it did not (owed) -
 * and a vote is neither: the bit is gone while the aggregate belongs to whoever
 * else votes. Booking it would make the last-holder release log
 * detail=hardware-still-on at ERROR for the one outcome this card is designed to
 * produce. The pass therefore keeps its own books (fVoteLeftSet, hold=0/1 on the
 * done row) and leaves the ledger to the demands whose release it can judge. */
IOReturn
IOQcomGCC::voteClocks(void)
{
	if (!fVoteEnabled) {
		SWEET_LOG_DEBUG("SWEET CLOCK: planID=C2 resource=cc%u:0x%x consumer=sweetclock-vote phase=vote result=%d detail=vote-absent\n",
		 kIOQcomCCGCC,kIOQcomGccVoteLocalID,kIOReturnUnsupported);
		return kIOReturnUnsupported;
	}
	if (!fMapped||!fRegistered||!fLock) return kIOReturnNotReady;
	char resource[kIOQcomLogBuf];
	gccName(kIOQcomGccVoteWindow,kIOQcomGccVoteOffset,resource,sizeof resource);
	if (!fWindow[kIOQcomGccVoteWindow].base) {
		/* validateProperties makes the vote imply its probe page, so a missing
		 * mapping here is a driver bug and never a tree mistake. */
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-vote phase=vote result=%d detail=vote-window-absent\n",
		 resource,kIOReturnNotReady);
		return kIOReturnNotReady;
	}
	uint32_t saved=0;
	if (!readRegister(kIOQcomGccVoteWindow,kIOQcomGccVoteOffset,&saved)) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-vote phase=vote-request result=%d detail=vote-read-failed writes=%u\n",
		 resource,kIOReturnNotReady,fWrites);
		return kIOReturnNotReady;
	}
	fVoteBefore=saved;
	const bool held=(saved&kGccBranchEnable)!=0;
	const bool off=(saved&kGccBranchClockOff)!=0;
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s name=gcc_npu_axi_clk consumer=sweetclock-vote phase=vote-request result=0 enable=%u off=%u value=%#x\n",
	 resource,held?1u:0u,off?1u:0u,saved);
	if (held||!off) {
		/* The value is the evidence; a store here would erase a state this card
		 * cannot account for, and it would be a vote on top of somebody's vote. */
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-vote phase=vote-request result=%d detail=%s enable=%u off=%u value=%#x writes=0\n",
		 resource,kIOReturnNotReady,held?"vote-held-by-other":"halt-not-set",
		 held?1u:0u,off?1u:0u,saved);
		return kIOReturnNotReady;
	}
	/* Bit 0 only. The saved config bits come back on release, and the halt
	 * status is never written on either store: the FSM owns it, which is why the
	 * vendor's set/clear helpers are field updates and not writes of a readback. */
	uint32_t target=(saved&kIOQcomGccBranchConfigMask)|kGccBranchEnable;
	if (!writeVoteRegister(kIOQcomGccVoteOffset,target,"vote-enable")) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-vote phase=vote-done result=%d writes=%u vote-store=0 enable-done=0 settle=0 hold=0 detail=store-failed\n",
		 resource,kIOReturnNotReady,fWrites);
		return kIOReturnNotReady;
	}
	uint32_t written=0;
	bool writeRead=readRegister(kIOQcomGccVoteWindow,kIOQcomGccVoteOffset,&written);
	const bool bitOnly=writeRead&&
	 (written&kIOQcomGccBranchConfigMask)==target;
	if (writeRead) {
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-vote phase=vote-write result=0 enable=%u value=%#x mask=%#x changed-only-enable=%u\n",
		 resource,(written&kGccBranchEnable)?1u:0u,written,kIOQcomGccBranchConfigMask,
		 bitOnly?1u:0u);
	} else {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-vote phase=vote-write result=%d detail=readback-failed\n",
		 resource,kIOReturnNotReady);
	}
	/* The vendor's own enable budget: 500 x 1 us, then warn. Nothing longer,
	 * because a voted branch that has not come up in 500 us is the NPU power
	 * collapse's problem and not a wait this pass should extend. */
	unsigned polls=0;
	uint32_t value=target;
	bool running=false,readFailed=false;
	for (polls=0;polls<kIOQcomGccVotePollTries;polls++) {
		if (!readRegister(kIOQcomGccVoteWindow,kIOQcomGccVoteOffset,&value)) {
			readFailed=true;
			break;
		}
		if ((value&kGccBranchClockOff)==0) { running=true; break; }
		IODelay(kIOQcomGccVotePollDelayUs);
	}
	/* The loop reads before it sleeps, so a first-poll success is polls==0: the
	 * number is the count of sleeps the hardware needed, not the count of reads. */
	if (running) {
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-vote phase=vote-running result=0 enable=%u off=0 value=%#x polls=%u\n",
		 resource,(value&kGccBranchEnable)?1u:0u,value,polls);
	} else {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-vote phase=vote-running result=%d enable=%u off=%u value=%#x polls=%u detail=%s\n",
		 resource,readFailed?kIOReturnNotReady:kIOReturnTimeout,
		 (value&kGccBranchEnable)?1u:0u,(value&kGccBranchClockOff)?1u:0u,value,polls,
		 readFailed?"vote-read-failed":"vote-halt-timeout");
	}
	fVoteRunning=running;
	/* The vote comes back out whatever happened above: a vote this driver no
	 * longer needs is a vote that keeps somebody's aggregate alive, and leaving
	 * it behind to make a timeout row read better is the one thing the card
	 * forbids. On a timeout the release is also the safest state to leave. */
	uint32_t release=saved&kIOQcomGccBranchConfigMask;
	bool released=writeVoteRegister(kIOQcomGccVoteOffset,release,"vote-release");
	uint32_t settled=0;
	bool settleRead=false;
	if (released) {
		IODelay(kIOQcomGccVoteSettleUs);
		settleRead=readRegister(kIOQcomGccVoteWindow,kIOQcomGccVoteOffset,&settled);
	}
	fVoteLeftSet=!released||(settleRead&&(settled&kGccBranchEnable)!=0);
	fVoteAfterRelease=settleRead?settled:0;
	if (!released) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-vote phase=vote-release result=%d detail=vote-release-failed\n",
		 resource,kIOReturnNotReady);
	} else if (!settleRead) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-vote phase=vote-release result=%d delay-us=%u detail=settle-read-failed\n",
		 resource,kIOReturnNotReady,kIOQcomGccVoteSettleUs);
	} else {
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-vote phase=vote-release result=0 delay-us=%u enable=%u off=%u value=%#x mask=%#x detail=observed-not-proof\n",
		 resource,kIOQcomGccVoteSettleUs,(settled&kGccBranchEnable)?1u:0u,
		 (settled&kGccBranchClockOff)?1u:0u,settled,kIOQcomGccBranchConfigMask);
	}
	/* Three separate answers, never folded into one: a pass that wrote a vote and
	 * failed to see the aggregate come up still proves the store, and a pass whose
	 * release did not read back still proves the vote. */
	unsigned storeOk=bitOnly?1u:0u, enableDone=running?1u:0u, settleOk=(released&&settleRead&&!fVoteLeftSet)?1u:0u;
	IOReturn verdict=(writeRead&&bitOnly&&running&&settleOk)?kIOReturnSuccess:
	 (readFailed||!released||!writeRead)?kIOReturnNotReady:
	 (running?kIOReturnIOError:kIOReturnTimeout);
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-vote phase=vote-done result=%d writes=%u vote-store=%u enable-done=%u settle=%u polls=%u hold=%u before=%#x after=%#x\n",
	 resource,verdict,fWrites,storeOk,enableDone,settleOk,polls,fVoteLeftSet?1u:0u,
	 saved,fVoteAfterRelease);
	if (fVoteLeftSet) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-vote phase=vote-done result=%d detail=vote-left-set value=%#x\n",
		 resource,kIOReturnNotReady,fVoteAfterRelease);
	}
	return verdict;
}
/* C2-07: the reset pulse.
 *
 * The card asks for one reset on a peripheral that is already stopped. The
 * sample is GCC_NPU_BCR - vendor offset 0x4d000, bit 0, the field the hardware
 * header calls BLK_ARES (1 asserts, 0 releases) - which is word 0 of the page
 * the C2-06 probe already maps, so this card adds no seventh mapping. The block
 * qualifies as stopped by measurement and not by convenience: the 0137 probe
 * read its three voted branches as 0x80000000 and the 0138 vote put a ballot in
 * and took it back out, so nobody claims the NPU and nothing runs it. This pass
 * re-derives that state from the ring before it writes: a claimed vote (bit 0
 * set) or a running branch (halt status clear) both refuse the pulse.
 *
 * The sequence is the vendor's own drivers/clk/qcom/reset.c spelled out one
 * claim per row:
 *
 *  1. request   four CBCRs of the page, then the BCR word. A refusal here is a
 *               row and zero stores, never a silent skip.
 *  2. assert    bit 0 set, config bits preserved, the status bit never written.
 *  3. hold      one readback, then the vendor's udelay(map->udelay ?: 1) - the
 *               1 us default because GCC_NPU_BCR carries no udelay in its map.
 *  4. release   bit 0 cleared with the same config preservation.
 *  5. settle    one readback compared with the arrival word inside the config
 *               mask: a BCR may report status the FSM owns, and this card must
 *               not call that a failed restore.
 *  6. restore   only when the block *arrived* held in reset. Releasing it is
 *               what the pulse asks for, but leaving it released would be the
 *               one durable side effect this card must not have, so the word
 *               goes back to the arrival value and is read back once more.
 *
 * The limit is in what the verdict is made of. The NPU has no driver and its
 * GDSC is off, so "the block reset" can only mean the bit landed, stayed for
 * the pulse and came back - plus the four CBCRs of the same page being
 * unchanged. The done row therefore prints collateral= and not a functional
 * claim; nothing here shows the NPU's internal FSM ran. */
IOReturn
IOQcomGCC::resetBlock(void)
{
	fResetDone=false;
	if (!fResetEnabled) {
		SWEET_LOG_DEBUG("SWEET CLOCK: planID=C2 resource=cc%u:0x%x consumer=sweetclock-reset phase=reset result=%d detail=reset-absent\n",
		 kIOQcomCCGCC,kIOQcomGccResetLocalID,kIOReturnUnsupported);
		return kIOReturnUnsupported;
	}
	if (!fMapped||!fRegistered||!fLock) return kIOReturnNotReady;
	char resource[kIOQcomLogBuf];
	gccName(kIOQcomGccVoteWindow,kIOQcomGccResetOffset,resource,sizeof resource);
	if (!fWindow[kIOQcomGccVoteWindow].base) {
		/* validateProperties makes the reset imply the page, so a missing mapping
		 * here is a driver bug and never a tree mistake. */
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset result=%d detail=reset-window-absent\n",
		 resource,kIOReturnNotReady);
		return kIOReturnNotReady;
	}
	/* (1) the block has to be idle, and the page that says so is the page the
	 * pulse touches. One pass reads the four CBCRs once and uses them twice: as
	 * the stop precondition and as the collateral baseline. */
	uint32_t beside[kIOQcomGccResetCollateralCount];
	unsigned besideFailed=0;
	for (unsigned i=0;i<kIOQcomGccResetCollateralCount;i++) {
		beside[i]=0;
		if (!readRegister(kIOQcomGccVoteWindow,gccProbeRegs[i].offset,&beside[i])) besideFailed++;
	}
	if (besideFailed) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-request result=%d detail=collateral-read-failed failed=%u writes=%u\n",
		 resource,kIOReturnNotReady,besideFailed,fWrites);
		return kIOReturnNotReady;
	}
	unsigned claimed=0, running=0;
	for (unsigned i=0;i<kIOQcomGccResetCollateralCount;i++) {
		if (!gccProbeRegs[i].voted) continue;
		if (beside[i]&kGccBranchEnable) claimed++;
		else if (!(beside[i]&kGccBranchClockOff)) running++;
	}
	uint32_t before=0;
	if (!readRegister(kIOQcomGccVoteWindow,kIOQcomGccResetOffset,&before)) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-request result=%d detail=reset-read-failed writes=%u\n",
		 resource,kIOReturnNotReady,fWrites);
		return kIOReturnNotReady;
	}
	fResetBefore=before;
	const uint32_t bit=1u<<kIOQcomGccResetBit;
	const bool arrivedHeld=(before&bit)!=0;
	if (claimed||running) {
		/* The value is the evidence. Storing here would reset a block somebody is
		 * using, and the card's whole premise is that nobody is. */
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-request result=%d detail=%s claimed=%u running=%u held=%u value=%#x writes=0\n",
		 resource,kIOReturnNotReady,claimed?"reset-block-claimed":"reset-block-running",
		 claimed,running,arrivedHeld?1u:0u,before);
		return kIOReturnNotReady;
	}
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s name=gcc_npu consumer=sweetclock-reset phase=reset-request result=0 detail=idle held=%u value=%#x cfg=%#x axi=%#x trig=%#x at=%#x\n",
	 resource,arrivedHeld?1u:0u,before,beside[0],beside[1],beside[2],beside[3]);
	/* (2) assert. Bit 0 only: the config bits come back on release and the status
	 * bit is never written, exactly like the branch stores one card earlier. */
	const uint32_t configMask=kIOQcomGccBranchConfigMask;
	const uint32_t assertValue=(before&configMask)|bit;
	if (!writeResetRegister(kIOQcomGccResetOffset,assertValue,"reset-assert")) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-done result=%d writes=%u stores=0 pulsed=0 restored=%u collateral=0 detail=assert-store-failed\n",
		 resource,kIOReturnNotReady,fWrites,arrivedHeld?1u:0u);
		return kIOReturnNotReady;
	}
	fResetDone=true;
	uint32_t asserted=0;
	const bool assertRead=readRegister(kIOQcomGccVoteWindow,kIOQcomGccResetOffset,&asserted);
	const bool held=assertRead&&(asserted&bit)!=0;
	if (assertRead) {
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-assert result=0 asserted=%u bit=%u value=%#x mask=%#x\n",
		 resource,held?1u:0u,kIOQcomGccResetBit,asserted,configMask);
	} else {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-assert result=%d detail=assert-read-failed writes=%u\n",
		 resource,kIOReturnNotReady,fWrites);
	}
	if (!held) {
		/* A store that did not land still has to leave the word where it found it:
		 * the arrival value is written back and the pass reports the failure. */
		unsigned restoreStores=0;
		if (writeResetRegister(kIOQcomGccResetOffset,before&configMask,"reset-restore")) restoreStores=1;
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-done result=%d writes=%u stores=%u pulsed=0 restored=0 collateral=0 before=%#x observed=%#x detail=assert-not-landed\n",
		 resource,kIOReturnNotReady,fWrites,1u+restoreStores,before,asserted);
		return kIOReturnNotReady;
	}
	/* (3) hold and (4) release: the vendor's pulse width, then bit 0 clear. */
	IODelay(kIOQcomGccResetPulseUs);
	const uint32_t releaseValue=(before&configMask)&~bit;
	if (!writeResetRegister(kIOQcomGccResetOffset,releaseValue,"reset-release")) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-done result=%d writes=%u stores=1 pulsed=0 restored=0 collateral=0 detail=release-store-failed\n",
		 resource,kIOReturnNotReady,fWrites);
		return kIOReturnNotReady;
	}
	uint32_t after=0;
	const bool releaseRead=readRegister(kIOQcomGccVoteWindow,kIOQcomGccResetOffset,&after);
	const bool released=releaseRead&&(after&bit)==0;
	if (releaseRead) {
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-release result=0 released=%u bit=0 value=%#x delay-us=%u\n",
		 resource,released?1u:0u,after,kIOQcomGccResetPulseUs);
	} else {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-release result=%d detail=release-read-failed writes=%u\n",
		 resource,kIOReturnNotReady,fWrites);
	}
	/* (5) settle, inside the config mask: a BCR is allowed to report status the
	 * FSM owns, and a pass that called that a failed restore would be wrong. */
	bool matched=releaseRead&&((after^before)&configMask)==0;
	unsigned restoreStores=0;
	if (releaseRead) fResetAfter=after;
	if (releaseRead&&!matched) {
		/* (6) the block arrived held in reset and the pulse released it. Put the
		 * arrival word back, then read it once more - the pulse happened, the
		 * state it was told to leave behind is the one it found. */
		if (writeResetRegister(kIOQcomGccResetOffset,before&configMask,"reset-restore")) restoreStores=1;
		uint32_t restoredValue=0;
		if (restoreStores&&readRegister(kIOQcomGccVoteWindow,kIOQcomGccResetOffset,&restoredValue)) {
			fResetAfter=restoredValue;
			matched=((restoredValue^before)&configMask)==0;
			if (matched) {
				SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-restore result=0 restored=1 value=%#x mask=%#x\n",
				 resource,restoredValue,configMask);
			} else {
				SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-restore result=%d detail=restore-mismatch before=%#x after=%#x mask=%#x\n",
				 resource,kIOReturnNotReady,before,restoredValue,configMask);
			}
		} else {
			SWEET_LOG_ERROR("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-restore result=%d detail=restore-%s writes=%u\n",
			 resource,kIOReturnNotReady,restoreStores?"read-failed":"store-failed",fWrites);
		}
	}
	/* The pulse is supposed to stay inside its own block. The same four CBCRs
	 * read again is the only place this card can show that. */
	unsigned moved=0;
	for (unsigned i=0;i<kIOQcomGccResetCollateralCount;i++) {
		uint32_t now=0;
		if (!readRegister(kIOQcomGccVoteWindow,gccProbeRegs[i].offset,&now)) moved++;
		else if (now!=beside[i]) moved++;
	}
	const unsigned stores=2u+restoreStores;
	const IOReturn verdict=(held&&released&&matched&&moved==0)?kIOReturnSuccess:kIOReturnNotReady;
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-done result=%d writes=%u stores=%u pulsed=%u restored=%u collateral=%u held-arrival=%u before=%#x after=%#x delay-us=%u\n",
	 resource,verdict,fWrites,stores,(held&&released)?1u:0u,matched?1u:0u,
	 moved==0?1u:0u,arrivedHeld?1u:0u,before,fResetAfter,kIOQcomGccResetPulseUs);
	if (moved) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-done result=%d detail=collateral-moved moved=%u\n",
		 resource,kIOReturnNotReady,moved);
	}
	if (!released) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=%s consumer=sweetclock-reset phase=reset-done result=%d detail=release-not-landed value=%#x\n",
		 resource,kIOReturnNotReady,after);
	}
	return verdict;
}
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
			 * it: mux 0x99 selects that branch, and the C2-02 decoder states
			 * its rate independently (fixed XO = 19.2 MHz), so this is the
			 * measurement evidence the gate card asks for. A wrong parent
			 * shows up here as BadArgument, which is how 0130's XO/4 claim was
			 * refuted on the device. */
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
	/* fDisableEnabled belongs to validateProperties: it is decided by the ADT,
	 * and clearing it here would disarm the card after it was armed. */
	fGateToken=0;
	fGateTokenHeld=false;
	if (!mapWindows(provider)) {
		unmapWindows();
		IOLockFree(fLock);
		fLock=NULL;
		return false;
	}
	fMapped=true;
	takeSnapshot();
	/* C2-06: read-only and before every write pass, so the baseline this round
	 * reports is the state the boot arrived in. It stores nothing; on an image
	 * without the opt-in it returns kIOReturnUnsupported and says nothing. */
	IOReturn probed=probeVoteSample();
	if (probed!=kIOReturnSuccess&&probed!=kIOReturnUnsupported) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=0 detail=probe-incomplete probe-result=%d writes=%u\n",
		 kIOQcomCCGCC,probed,fWrites);
	}
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
	/* C2-04 retracts the enable it was just given and then runs that same
	 * verified enable again, so by the time anything measures, the branch is
	 * running under a demand the ledger holds - whatever this card found.
	 * C2-05 replaces that pass when it is armed instead of following it: the
	 * shared pass performs the same retraction from a last holder that did not
	 * open the clock, and one boot should walk each contract once. */
	IOReturn disabled=fShareEnabled?kIOReturnUnsupported:disableClocks();
	if (disabled!=kIOReturnSuccess&&disabled!=kIOReturnUnsupported) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=0 detail=disable-incomplete disable-result=%d writes=%u\n",
		 kIOQcomCCGCC,disabled,fWrites);
	}
	IOReturn shared=shareClocks();
	if (shared!=kIOReturnSuccess&&shared!=kIOReturnUnsupported) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=0 detail=share-incomplete share-result=%d writes=%u\n",
		 kIOQcomCCGCC,shared,fWrites);
	}
	/* C2-06 write half runs after the C2-04/C2-05 passes and before the C2-08
	 * measurement: the vote is placed and withdrawn inside this pass, so the
	 * branch is back at its boot state before anything measures, and a vote that
	 * will not release does not sit in the way of a measurement store. */
	IOReturn voted=voteClocks();
	if (voted!=kIOReturnSuccess&&voted!=kIOReturnUnsupported) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=0 detail=vote-incomplete vote-result=%d writes=%u\n",
		 kIOQcomCCGCC,voted,fWrites);
	}
	/* C2-07 runs where the vote ran, and for the same reason: the pulse is over
	 * and the block is back at its arrival state before anything measures, so a
	 * reset that will not come out does not sit in the way of a measurement
	 * store. The two cards cannot both be armed (validateProperties refuses it),
	 * so at most one of these two rows ever appears. */
	IOReturn pulsed=resetBlock();
	if (pulsed!=kIOReturnSuccess&&pulsed!=kIOReturnUnsupported) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=0 detail=reset-incomplete reset-result=%d writes=%u\n",
		 kIOQcomCCGCC,pulsed,fWrites);
	}
	/* C2-08 last, so registration and the C2-02 decode exist before the first
	 * store and a failed measurement cannot leave a half-published backend. */
	IOReturn measured=measureClocks();
	if (measured!=kIOReturnSuccess&&measured!=kIOReturnUnsupported) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=0 detail=measure-incomplete measure-result=%d writes=%u\n",
		 kIOQcomCCGCC,measured,fWrites);
	}
	if (fVoteEnabled) {
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=0 windows=%u snapshot=%u reads=%u writes=%u variant=%#x vote-probe=1 vote=1\n",
		 kIOQcomCCGCC,kIOQcomGccMappedWindowCount,kIOQcomGccSnapshotCount,fReads,fWrites,
		 kIOQcomGccVariantSm8150);
	} else if (fResetEnabled) {
		/* The reset row keeps probe= out: this image maps the fifth page without
		 * arming the probe, and a reader of the ring must be able to see that. */
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=0 windows=%u snapshot=%u reads=%u writes=%u variant=%#x reset=1\n",
		 kIOQcomCCGCC,kIOQcomGccMappedWindowCount,kIOQcomGccSnapshotCount,fReads,fWrites,
		 kIOQcomGccVariantSm8150);
	} else if (fProbeEnabled) {
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=0 windows=%u snapshot=%u reads=%u writes=%u variant=%#x vote-probe=1\n",
		 kIOQcomCCGCC,kIOQcomGccMappedWindowCount,kIOQcomGccSnapshotCount,fReads,fWrites,
		 kIOQcomGccVariantSm8150);
	} else {
		SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=0 windows=%u snapshot=%u reads=%u writes=%u variant=%#x\n",
		 kIOQcomCCGCC,kIOQcomGccWindowCount,kIOQcomGccSnapshotCount,fReads,fWrites,
		 kIOQcomGccVariantSm8150);
	}
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
	/* The ledger entry dies with the registration, so the token this driver held
	 * is not a demand any more. */
	fGateToken=0;
	fGateTokenHeld=false;
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

