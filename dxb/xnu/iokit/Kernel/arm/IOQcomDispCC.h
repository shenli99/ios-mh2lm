/* SM8150 DISPCC backend: the C6-01 read-only registration candidate.
 *
 * Contract of this candidate, in one place:
 *  - It registers the display clock controller's identity and capability, and
 *    nothing else: two declared resources (the MDSS MDP branch and the RCG it
 *    selects from), each with the parent table, halt class, register-access
 *    dependency and supply domain the vendor tables declare. No rate, no
 *    parent selection and no gate state is claimed here, because this
 *    candidate has not read a single register.
 *  - It maps the vendor cc_base window once and validates the resolved
 *    physical address and the declared length against sm8150.dtsi
 *    (clock_dispcc reg = <0xaf00000 0x20000>, reg-names "cc_base"). The
 *    mapping is kept as the registration's resource handle and is released on
 *    stop. No virtual base is ever derived in this file, so there is no load
 *    and no store to audit: "初始化写计数为零" is structural here rather than a
 *    counter that could drift from the code.
 *  - Every access to the block depends on somebody else's branch: sm8150.dtsi
 *    gives dispcc clock-names "cfg_ahb_clk" / clocks <&clock_gcc
 *    GCC_DISP_AHB_CLK>, and the display driver checks DISPCC_AHB_CBCR before
 *    it touches the window. The descriptor declares that dependency on the GCC
 *    controller and the provider's admission enforces it, refusing the
 *    resource with detail=access while that controller is not registered: a
 *    clock whose registers cannot be reached is reported unavailable, never
 *    ready. The declaration is printed by start; it is deliberately not
 *    re-probed there, because a start-time probe would report boot order
 *    rather than the tree.
 *  - The MDP source's parents are the vendor disp_cc_parent_map_5: TCXO is a
 *    declared fixed source, the three PLL outputs are declared Unknown on
 *    purpose. This candidate publishes no PLL resource, and an unknown parent
 *    must make admission refuse instead of turning into a guessed Ready; the
 *    RCG's supply domain is CX because the vendor table gives it vdd_mm, while
 *    the branch declares no domain because its vendor entry has no vdd_class.
 *  - copyClockState answers Unsupported. Rate/parent/gate decode is C6-02's
 *    card, so QueryRate is not advertised either: a capability this candidate
 *    cannot honour is not claimed.
 *  - No write path exists: no enable, no disable, no reset, no PLL start. The
 *    MDSS-owned branches keep their owner, and the backend never reads the
 *    window, so the old writer's first handoff stays exactly as it was. */
#ifndef IOQCOM_DISPCC_H
#define IOQCOM_DISPCC_H

#include "IOQcomClock.h"
#include <IOKit/IOMemoryDescriptor.h>

#if defined(ARM64_BOARD_CONFIG_MH2LM)

/* The ADT ABI of this node: sweet,dispcc-abi 1 and the SoC variant word, the
 * same shape sweet,gcc carries. A tree that states neither is refused instead
 * of being treated as the only variant this driver knows. */
enum : uint32_t { kIOQcomDispAbi=1, kIOQcomDispVariantSm8150=0x8150 };

/* sm8150.dtsi: clock_dispcc reg = <0xaf00000 0x20000>. The 128 KiB window is
 * the vendor's own cc_base range (it covers GDSC 0x3000 and the RSCC block at
 * 0x4000 that the MDSS ledger reads); the display node maps the same pages, so
 * this backend holds a read-only identity copy rather than a second writer. */
enum : uint64_t {
	kIOQcomDispWindowPhysical=0x0af00000ULL,
	kIOQcomDispWindowLength=0x20000ULL,
};

/* Local IDs are this backend's identity space, not vendor clock IDs. The
 * branch is the resource a consumer will ask for; the RCG is published as its
 * parent so that parent is a real registration instead of an Unknown source
 * hiding at the top of the walk. */
enum : uint32_t {
	kIOQcomDispMdp=1,        /* disp_cc_mdss_mdp_clk, CBCR 0x200c */
	kIOQcomDispMdpSrc=2,     /* disp_cc_mdss_mdp_clk_src, RCG 0x20c8 */
	kIOQcomDispClockCount=2,
};

/* The vendor register numbers these two identities name, kept here so the
 * ring, the descriptor and dispcc-sm8150.c can be reconciled without a reader
 * having to trust a comment. Nothing in this candidate dereferences them. */
enum : uint32_t {
	kIOQcomDispMdpEnableOffset=0x200c,
	kIOQcomDispMdpSrcCmdOffset=0x20c8,
};

/* disp_cc_parent_map_5 has five entries and the descriptor table is four wide,
 * so the test-PLL source (P_CORE_BI_PLL_TEST_SE, selector 7) is deliberately
 * not declared: it is not a candidate for admission. The branch declares
 * exactly one parent, its own source, because clk_branch2 has no parent_map at
 * all - the parent of disp_cc_mdss_mdp_clk is spelled out in the vendor table
 * (CLK_SET_RATE_PARENT, clk_branch2_ops). */
enum : unsigned { kIOQcomDispMdpParents=1, kIOQcomDispMdpSrcVendorParents=5,
	kIOQcomDispMdpSrcParents=kIOQcomMaxParents };

/* The single source is the vendor RCG's vdd_class = &vdd_mm and its rate_max
 * table (VDD_MIN/VDD_LOWER/VDD_LOW). These are ceilings the vendor states, not
 * measured rates, and this candidate reports them as declarations only. */
enum : uint64_t {
	kIOQcomDispMdpSrcMinHz=19200000ULL,
	kIOQcomDispMdpSrcLowerHz=171428571ULL,
	kIOQcomDispMdpSrcLowHz=300000000ULL,
};

/* The entry and this backend are independent IOServices and IOKit does not
 * order their start, so the provider is awaited for a bounded time and then
 * named once in a single refusal row. */
enum : unsigned { kIOQcomDispProviderWaitTries=200, kIOQcomDispProviderWaitMs=5 };

class IOQcomDispCC : public IOQcomClockController {
	OSDeclareDefaultStructors(IOQcomDispCC);
	/* One window, one owner: the registration's resource handle. */
	struct Window {
		uint64_t physical;      /* expected resolved PA (ADT ranges applied) */
		IOByteCount length;     /* observed length, reported as evidence */
		IOMemoryMap *map;       /* owned mapping, released on stop/free */
	};
	Window fWindow;
	bool fMapped;
	bool fRegistered;
	bool validateProperties(IOService *provider);
	bool mapWindow(IOService *provider);
	void unmapWindow(void);
	void publishClocks(void);
public:
	uint32_t clockControllerID(void) const APPLE_KEXT_OVERRIDE;
	IOReturn copyClockInfo(uint32_t localID,IOQcomClockInfo *info) APPLE_KEXT_OVERRIDE;
	IOReturn copyClockState(uint32_t localID,IOQcomClockState *state) APPLE_KEXT_OVERRIDE;
	bool copyClockName(uint32_t localID,char *buffer,unsigned size) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;
};

#endif /* ARM64_BOARD_CONFIG_MH2LM */
#endif /* IOQCOM_DISPCC_H */
