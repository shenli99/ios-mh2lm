/* SM8150 GCC backend, read-only candidate C2-01/C2-02.
 *
 * Contract of this candidate, in one place:
 *  - start() maps the four declared GCC pages through the provider nub and
 *    validates the resolved physical address and the declared length of every
 *    window before the first read. A wrong translation fails start instead of
 *    silently reading somebody else's page.
 *  - Register access is one helper: bounds checked against the declared length,
 *    offset aligned, guarded by the backend lock, with the same post-read dmb
 *    the display backend uses for this SoC. There is no write helper in this
 *    file, so "zero writes" is structural rather than a counter that could
 *    drift.
 *  - Descriptors come from the vendor GCC tables; a branch without an explicit
 *    vdd_class declares domain 0 instead of guessing CX, and the inherited
 *    display branches are declared pinned.
 *  - C2-02 decodes gcc_pdm2_clk_src only: raw CMD/CFG, selector, divider and
 *    the decoded value when every rate in the path is trusted. An untrusted
 *    parent is reported Unknown (configuredHz 0, rateKnown false); the
 *    requested value is never substituted for what the hardware shows.
 *
 * Zero register writes: no device-memory store exists in this file. */
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
	{ 0x00143000ULL, 0x1000ULL },   /* XO/4 branch: debug clock reference */
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
	{ "gcc_pdm_xo4_clk", 4, kIOQcomClockBranch, kIOQcomHaltCheck,
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
	SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=cc%u phase=start result=0 windows=%u snapshot=%u reads=%u writes=0 variant=%#x\n",
	 kIOQcomCCGCC,kIOQcomGccWindowCount,kIOQcomGccSnapshotCount,fReads,kIOQcomGccVariantSm8150);
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
			SWEET_LOG_INFO("SWEET CLOCK: planID=C2 resource=cc%u phase=stop result=%d reads=%u writes=0\n",
			 kIOQcomCCGCC,result,fReads);
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

