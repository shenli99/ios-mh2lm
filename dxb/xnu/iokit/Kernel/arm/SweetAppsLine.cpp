/* C3-04: the apps RSC line, one reviewed sequence.
 *
 * The reads and writes here are the same ones the display service has used
 * since 0139 (they are the ring-proven path); they moved into functions so the
 * second possible owner cannot restate them differently. Order matters and is
 * not an accident:
 *   - geometry first, read-only, so an unsupported tree never gets a store;
 *   - claimable before program, or an inherited generation would be adopted;
 *   - disable before group/config/priority/route, so nothing fires mid-setup;
 *   - RWP is polled by the caller's timer, never spun here;
 *   - admit re-verifies after RWP retires, because the distributor may have
 *     landed the writes in any order.
 */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "SweetAppsLine.h"

static inline uint32_t
lineRead(vm_offset_t base,uint32_t off)
{
	uint32_t value=*(volatile uint32_t *)(base+off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return value;
}

static inline void
lineWrite(vm_offset_t base,uint32_t off,uint32_t value)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(base+off)=value;
	__asm__ volatile("dsb osh" ::: "memory");
}

bool
SweetAppsLine::geometry(vm_offset_t gic)
{
	if (!gic) return false;
	/* GICv3, the SPI inside the implemented line range, group 1 enabled. */
	if ((lineRead(gic,kSweetAppsGICArch)&0xf0u)!=0x30u) return false;
	if (((lineRead(gic,kSweetAppsGICType)&31u)+1u)*32u<=kSweetAppsDoorIntid) return false;
	if (!(lineRead(gic,kSweetAppsGICCTLR)&2u)) return false;
	return true;
}

bool
SweetAppsLine::claimable(vm_offset_t gic)
{
	/* Three separate loads in a fixed order: an OR of three calls would leave
	 * the bus order to the compiler, and this sequence is a reviewed one. */
	if (lineRead(gic,kSweetAppsGICEnable)&kSweetAppsGICBit) return false;
	if (lineRead(gic,kSweetAppsGICPending)&kSweetAppsGICBit) return false;
	if (lineRead(gic,kSweetAppsGICActive)&kSweetAppsGICBit) return false;
	return true;
}

bool
SweetAppsLine::verify(vm_offset_t gic)
{
	if (!gic) return false;
	if (lineRead(gic,kSweetAppsGICConfig)&0xc00u) return false;      /* 1-of-N only */
	if ((lineRead(gic,kSweetAppsGICPriority)&0xff00u)!=0xa000u) return false;
	if (lineRead(gic,kSweetAppsGICRoute) || lineRead(gic,kSweetAppsGICRoute+4)) return false;
	return true;
}

SweetAppsLine::Verdict
SweetAppsLine::program(vm_offset_t gic)
{
	if (!geometry(gic)) return LineBroken;
	if (!claimable(gic)) return LineForeign;
	lineWrite(gic,kSweetAppsGICDisable,kSweetAppsGICBit);
	lineWrite(gic,kSweetAppsGICGroup,lineRead(gic,kSweetAppsGICGroup)|kSweetAppsGICBit);
	/* Level-triggered, one target: the 1-of-N encoding is the zero value of
	 * the two config bits, so it is written explicitly rather than assumed. */
	lineWrite(gic,kSweetAppsGICConfig,lineRead(gic,kSweetAppsGICConfig)&~0xc00u);
	lineWrite(gic,kSweetAppsGICPriority,(lineRead(gic,kSweetAppsGICPriority)&~0xff00u)|0xa000u);
	/* Aligned 64-bit IROUTER store, boot CPU MPIDR0 route. */
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint64_t *)(gic+kSweetAppsGICRoute)=0;
	__asm__ volatile("dsb osh" ::: "memory");
	return LineReady;
}

SweetAppsLine::Verdict
SweetAppsLine::retired(vm_offset_t gic,uint64_t now,uint64_t deadline)
{
	/* RWP is an asynchronous distributor phase, bounded by the caller: a tree
	 * that never retires it is a broken line, not a slow one. */
	if (lineRead(gic,kSweetAppsGICCTLR)&(1u<<31))
		return now<deadline ? LineWait : LineBroken;
	return verify(gic) ? LineReady : LineBroken;
}

void
SweetAppsLine::mask(vm_offset_t gic)
{
	if (gic) lineWrite(gic,kSweetAppsGICDisable,kSweetAppsGICBit);
}

void
SweetAppsLine::unmask(vm_offset_t gic)
{
	if (gic) lineWrite(gic,kSweetAppsGICEnable,kSweetAppsGICBit);
}

SweetAppsLine::Verdict
SweetAppsLine::admit(vm_offset_t gic,uint64_t now,uint64_t deadline,
    uint32_t (*source)(void *),void *context,bool *cleared)
{
	if (cleared) *cleared=false;
	if (!verify(gic)) return LineBroken;
	if (lineRead(gic,kSweetAppsGICCTLR)&(1u<<31))
		return now<deadline ? LineWait : LineBroken;
	/* An IRQ worker may beat the previous EOI on another CPU. Wait for the
	 * owned line to drain; never overwrite an active generation. */
	if (lineRead(gic,kSweetAppsGICActive)&kSweetAppsGICBit) return LineWait;
	if (lineRead(gic,kSweetAppsGICPending)&kSweetAppsGICBit) {
		/* A pending bit at trigger time cannot be attributed: either our own
		 * earlier flight left a source word (refuse), or the line is simply
		 * still retiring (ACK a clear source and retry). */
		uint32_t status=source ? source(context) : 0;
		if (status) return LineForeign;
		lineWrite(gic,kSweetAppsGICClearPending,kSweetAppsGICBit);
		if (cleared) *cleared=true;
		if (lineRead(gic,kSweetAppsGICPending)&kSweetAppsGICBit) return LineWait;
	}
	return LineReady;
}
#endif /* ARM64_BOARD_CONFIG_MH2LM */