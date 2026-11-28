/* SM8150 DISPCC backend: the C6-01 read-only registration candidate.
 *
 * What this file does, in order: map the one declared window, verify the
 * resolved physical address and the declared length against sm8150.dtsi,
 * register the controller, and publish two descriptors plus the access
 * prerequisite they both carry. It never derives a virtual base from the
 * mapping, so there is no load and no store anywhere in this translation unit
 * - the card's "初始化写计数为零" is a property of the code rather than a
 * counter that could drift from it.
 *
 * Refusal attribution follows the card's own vocabulary: identity/property
 * failures are phase=register, window failures are phase=map, and the
 * cross-controller access prerequisite is phase=access.
 *
 * Nothing here is an opt-in: an image that carries the sweet,dispcc node gets
 * these rows, and an image that does not gets none of them. There is no write
 * gate to arm because there is no write path to gate. */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "IOQcomDispCC.h"
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <string.h>
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"

#define super IOQcomClockController

OSDefineMetaClassAndStructors(IOQcomDispCC,IOQcomClockController);

enum : unsigned { kIOQcomLogBuf=40 };

/* A declared clock. parent[].localID carries, for fixed and unknown sources,
 * the RCG parent-map selector the C6-02 decoder will match against
 * CFG_SRC_SEL; it is never a clock identity and the admission walk ignores it
 * for those sources. */
struct DispClock {
	const char *name;
	uint32_t localID, kind, halt, capabilities, domain, accessDeps;
	unsigned parents;
	IOQcomClockParent parent[kIOQcomMaxParents];
	uint32_t offset;
	uint64_t corner[kIOQcomCornerCount];
	bool pinned;
};

/* Both entries are the vendor dispcc-sm8150.c declarations, and neither is a
 * guess:
 *  - disp_cc_mdss_mdp_clk is a clk_branch2 at CBCR 0x200c with one enable bit,
 *    halt_check BRANCH_HALT, no vdd_class (so no domain) and - unlike most
 *    branch clocks - an explicit parent, disp_cc_mdss_mdp_clk_src.
 *  - disp_cc_mdss_mdp_clk_src is the RCG at cmd_rcgr 0x20c8, hid_width 5,
 *    parent_map disp_cc_parent_map_5 and vdd_class vdd_mm (CX).
 *
 * The register-access prerequisite is declared on both because it belongs to
 * the block, not to one clock: sm8150.dtsi gives dispcc clock-names
 * "cfg_ahb_clk" from GCC_DISP_AHB_CLK, so no register in this window can be
 * read until GCC's branch is on. Declaring it makes the provider refuse the
 * resource with detail=access on a tree whose GCC controller is missing,
 * instead of handing out a handle into an unreachable window.
 *
 * capabilities carries QueryConfig only. QueryRate exists in the contract, and
 * this candidate deliberately does not claim it: copyClockState answers
 * Unsupported until C6-02 implements the decode, and advertising a rate this
 * backend cannot report would be exactly the "能力虚报" the card forbids. */
static const DispClock dispClocks[] = {
	{ "disp_cc_mdss_mdp_clk", kIOQcomDispMdp, kIOQcomClockBranch, kIOQcomHaltCheck,
	  kIOQcomCapQueryConfig, 0, 1u<<kIOQcomCCGCC, kIOQcomDispMdpParents,
	  {{kIOQcomSourceLocal,kIOQcomCCDispCC,kIOQcomDispMdpSrc}},
	  kIOQcomDispMdpEnableOffset, {0,0,0}, true },
	{ "disp_cc_mdss_mdp_clk_src", kIOQcomDispMdpSrc, kIOQcomClockRCG, kIOQcomHaltDelay,
	  kIOQcomCapQueryConfig, kIOQcomDomainCX, 1u<<kIOQcomCCGCC, kIOQcomDispMdpSrcParents,
	  {{kIOQcomSourceXO,0,0},          /* P_BI_TCXO,            sel 0 */
	   {kIOQcomSourceUnknown,0,1},     /* P_DISP_CC_PLL0_OUT_MAIN, sel 1 */
	   {kIOQcomSourceUnknown,0,4},     /* P_DISP_CC_PLL1_OUT_MAIN, sel 4 */
	   {kIOQcomSourceUnknown,0,5}},    /* P_DISP_CC_PLL1_OUT_EVEN, sel 5 */
	  kIOQcomDispMdpSrcCmdOffset,
	  {kIOQcomDispMdpSrcMinHz,kIOQcomDispMdpSrcLowerHz,kIOQcomDispMdpSrcLowHz}, false },
};

static const DispClock *
dispEntry(uint32_t localID)
{
	for (unsigned i=0;i<sizeof dispClocks/sizeof dispClocks[0];i++) {
		if (dispClocks[i].localID==localID) return &dispClocks[i];
	}
	return NULL;
}

/* planID=C6 rows name the controller as "dispcc" and a resource exactly as the
 * provider spells one, "cc2:0x<localID>": a reader has to be able to join the
 * C1 row for a clock to the C6 row that introduced it, and two spellings for
 * one clock would make that a guess. */
static void
dispResource(char *buffer,unsigned size,uint32_t localID)
{
	if (localID) snprintf(buffer,size,"cc%u:0x%x",(unsigned)kIOQcomCCDispCC,localID);
	else snprintf(buffer,size,"dispcc");
}

bool
IOQcomDispCC::validateProperties(IOService *provider)
{
	if (!provider) return false;
	/* Identity first: an ABI this driver does not know means the node was
	 * written for a different candidate, and mapping its window to find out
	 * how different would be the wrong order. */
	OSData *abi=OSDynamicCast(OSData,provider->getProperty("sweet,dispcc-abi"));
	if (!abi||abi->getLength()!=4||memcmp(abi->getBytesNoCopy(),"\1\0\0\0",4)) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C6 resource=dispcc phase=register result=%d detail=missing-dispcc-abi\n",
		 kIOReturnBadArgument);
		return false;
	}
	OSData *variant=OSDynamicCast(OSData,provider->getProperty("sweet,dispcc-variant"));
	if (!variant||variant->getLength()!=4) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C6 resource=dispcc phase=register result=%d detail=missing-variant\n",
		 kIOReturnBadArgument);
		return false;
	}
	uint32_t observed=0;
	memcpy(&observed,variant->getBytesNoCopy(),4);
	if (observed!=kIOQcomDispVariantSm8150) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C6 resource=dispcc phase=register result=%d detail=variant-mismatch observed=%#x required=%#x\n",
		 kIOReturnUnsupported,observed,(unsigned)kIOQcomDispVariantSm8150);
		return false;
	}
	return true;
}

/* The mapping is the whole resource claim of this candidate: it proves the
 * firmware translated this node's reg to the controller the vendor tree names,
 * and it is the handle registration hands back to stop. No virtual base is
 * taken here, so nothing that follows can reach the window by accident. */
bool
IOQcomDispCC::mapWindow(IOService *provider)
{
	fWindow.physical=kIOQcomDispWindowPhysical;
	fWindow.length=0;
	fWindow.map=provider?provider->mapDeviceMemoryWithIndex(0):NULL;
	if (!fWindow.map) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C6 resource=dispcc phase=map result=%d detail=window-absent\n",
		 kIOReturnNotReady);
		return false;
	}
	uint64_t observed=fWindow.map->getPhysicalAddress();
	uint64_t length=fWindow.map->getLength();
	if (observed!=kIOQcomDispWindowPhysical) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C6 resource=dispcc phase=map result=%d detail=address-mismatch observed=%#llx required=%#llx\n",
		 kIOReturnBadArgument,(unsigned long long)observed,
		 (unsigned long long)kIOQcomDispWindowPhysical);
		return false;
	}
	if (length<kIOQcomDispWindowLength) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C6 resource=dispcc phase=map result=%d detail=length-short observed=%#llx required=%#llx\n",
		 kIOReturnBadArgument,(unsigned long long)length,
		 (unsigned long long)kIOQcomDispWindowLength);
		return false;
	}
	fWindow.length=length;
	SWEET_LOG_INFO("SWEET CLOCK: planID=C6 resource=dispcc phase=map result=0 physical=%#llx length=%#llx vbase=0\n",
	 (unsigned long long)kIOQcomDispWindowPhysical,(unsigned long long)length);
	return true;
}

void
IOQcomDispCC::unmapWindow(void)
{
	OSSafeReleaseNULL(fWindow.map);
	fWindow.length=0;
	fMapped=false;
}

/* The access prerequisite is declared, not probed, and that is deliberate:
 * the entry publishes itself and the GCC controller starts when IOKit gets to
 * it, so a probe in this backend's start would report whatever the boot order
 * happened to be - a false "dependency absent" on the exact boot where the
 * dependency was a few milliseconds late. The provider owns the check: every
 * admission of either resource walks accessDeps and refuses with
 * detail=access, and the C1 listing prints the verdict in deps/depReason once
 * both controllers are up. This candidate therefore prints the declaration it
 * ships and leaves enforcement where it already lives. */
void
IOQcomDispCC::publishClocks(void)
{
	SWEET_LOG_INFO("SWEET CLOCK: planID=C6 resource=dispcc phase=access result=0 declared=cc%u:0x0 bit=%#x gate=admission\n",
	 (unsigned)kIOQcomCCGCC,(unsigned)(1u<<kIOQcomCCGCC));
	for (unsigned i=0;i<sizeof dispClocks/sizeof dispClocks[0];i++) {
		char resource[kIOQcomLogBuf];
		dispResource(resource,sizeof resource,dispClocks[i].localID);
		SWEET_LOG_INFO("SWEET CLOCK: planID=C6 resource=%s consumer=dispcc phase=clock kind=%u halt=%u caps=%#x parents=%u domain=%#x access=%#x offset=%#x pinned=%u\n",
		 resource,dispClocks[i].kind,dispClocks[i].halt,dispClocks[i].capabilities,
		 dispClocks[i].parents,dispClocks[i].domain,dispClocks[i].accessDeps,
		 dispClocks[i].offset,dispClocks[i].pinned?1:0);
	}
}

bool
IOQcomDispCC::start(IOService *provider)
{
	if (fRegistered||fMapped) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C6 resource=dispcc phase=register result=%d detail=already-started\n",
		 kIOReturnBusy);
		return false;
	}
	if (!provider||!super::start(provider)) return false;
	if (!validateProperties(provider)) return false;
	if (!mapWindow(provider)) {
		unmapWindow();
		return false;
	}
	fMapped=true;
	/* The entry and this backend are independent IOServices; IOKit does not
	 * order their start. Wait a bounded time for the entry to publish itself,
	 * then refuse with a single line instead of registering into nothing. */
	IOQcomClockProvider *entry=NULL;
	for (unsigned i=0;i<kIOQcomDispProviderWaitTries;i++) {
		entry=IOQcomClockProvider::publishedProvider();
		if (entry) break;
		IOSleep(kIOQcomDispProviderWaitMs);
	}
	if (!entry) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C6 resource=dispcc phase=register result=%d detail=provider-absent\n",
		 kIOReturnNotReady);
		unmapWindow();
		return false;
	}
	IOReturn result=entry->registerController(this);
	if (result!=kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C6 resource=dispcc phase=register result=%d detail=registration-refused\n",
		 result);
		unmapWindow();
		return false;
	}
	fRegistered=true;
	publishClocks();
	/* One aggregate row, and the number it prints is the provider's own sum over
	 * every registered backend, not this driver's opinion of itself. */
	SWEET_LOG_INFO("SWEET CLOCK: planID=C6 resource=dispcc phase=register result=0 controller=cc%u:0x0 clocks=%u writes=%llu\n",
	 (unsigned)kIOQcomCCDispCC,(unsigned)kIOQcomDispClockCount,
	 (unsigned long long)entry->registeredWrites());
	return true;
}

void
IOQcomDispCC::stop(IOService *provider)
{
	unsigned released=fMapped?1:0;
	unsigned withdrawn=0;
	if (fRegistered) {
		IOQcomClockProvider *entry=IOQcomClockProvider::publishedProvider();
		if (entry&&entry->withdrawController(this)==kIOReturnSuccess) withdrawn=1;
		fRegistered=false;
	}
	unmapWindow();
	SWEET_LOG_INFO("SWEET CLOCK: planID=C6 resource=dispcc phase=stop result=0 released=%u withdrawn=%u\n",
	 released,withdrawn);
	super::stop(provider);
}

void
IOQcomDispCC::free(void)
{
	unmapWindow();
	super::free();
}

uint32_t
IOQcomDispCC::clockControllerID(void) const
{
	return kIOQcomCCDispCC;
}

IOReturn
IOQcomDispCC::copyClockInfo(uint32_t localID,IOQcomClockInfo *info)
{
	if (!info) return kIOReturnBadArgument;
	const DispClock *entry=dispEntry(localID);
	if (!entry) return kIOReturnNotFound;
	memset(info,0,sizeof *info);
	info->controller=kIOQcomCCDispCC;
	info->localID=entry->localID;
	info->kind=entry->kind;
	info->halt=entry->halt;
	info->capabilities=entry->capabilities;
	info->accessDeps=entry->accessDeps;
	info->domain=entry->domain;
	info->possibleCount=entry->parents;
	for (unsigned i=0;i<entry->parents;i++) info->possible[i]=entry->parent[i];
	for (unsigned i=0;i<kIOQcomCornerCount;i++) info->cornerMaxHz[i]=entry->corner[i];
	info->pinned=entry->pinned;
	/* Nothing in this candidate reads the window, so no enable bit was
	 * observed. Setting this from a guess would make the first real decode
	 * look like a confirmation of it. */
	info->inheritedOn=false;
	return kIOReturnSuccess;
}

/* C6-02 owns the decode. Until then the honest answer is Unsupported: the
 * provider's QueryRate capability is not advertised either, so a consumer
 * cannot be surprised by a rate this backend cannot state. */
IOReturn
IOQcomDispCC::copyClockState(uint32_t localID,IOQcomClockState *state)
{
	if (!state) return kIOReturnBadArgument;
	if (!dispEntry(localID)) return kIOReturnNotFound;
	return kIOReturnUnsupported;
}

bool
IOQcomDispCC::copyClockName(uint32_t localID,char *buffer,unsigned size)
{
	if (!buffer||!size) return false;
	const DispClock *entry=dispEntry(localID);
	if (!entry) return false;
	snprintf(buffer,size,"%s",entry->name);
	return true;
}
#endif /* ARM64_BOARD_CONFIG_MH2LM */