/* C3-04: the apps RSC completion line, as a small set of reviewed steps.
 *
 * INTID37 is a level-high SPI whose GICD words are the same on every owner, so
 * the sequence that takes the line, admits a trigger and masks it again is
 * written once here and called by whichever side the boot made the owner. The
 * display service keeps calling its own long-standing statements for the tree
 * it owns today (that path is proven by 0139-0143 and stays untouched); this
 * header exists so a second owner cannot invent a second interpretation of the
 * same registers, and so the geometry/claim checks have exactly one definition.
 *
 * Nothing here maps memory, allocates, logs or waits: a caller passes a GICD
 * base it already owns, the current time and its own deadline. Every step is a
 * handful of loads and stores, and every store carries the barriers this SoC
 * needs on a non-strongly-ordered device mapping.
 */
#ifndef SWEET_APPS_LINE_H
#define SWEET_APPS_LINE_H
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOTypes.h>
#include "SweetAppsDoor.h"

/* INTID37 sits in bit 5 of the second 32-bit GICD bitmap word. */
enum : uint32_t {
	kSweetAppsGICBit = 1u << 5,
	kSweetAppsGICCTLR = 0x000, kSweetAppsGICEnable = 0x104,
	kSweetAppsGICDisable = 0x184, kSweetAppsGICPending = 0x204,
	kSweetAppsGICClearPending = 0x284, kSweetAppsGICActive = 0x304,
	kSweetAppsGICGroup = 0x084, kSweetAppsGICConfig = 0xc08,
	kSweetAppsGICPriority = 0x424, kSweetAppsGICRoute = 0x6128,
	kSweetAppsGICArch = 0xffe8, kSweetAppsGICType = 0x004,
};

class SweetAppsLine {
public:
	/* Same vocabulary the display service uses for its own admission path, so
	 * a caller maps one enum onto IOReturn and nothing else. */
	enum Verdict {
		LineReady,  /* nothing pending, distributor write retired */
		LineWait,   /* a write is still retiring: ask again shortly */
		LineForeign,/* a pending bit with a live source nobody may ACK */
		LineBroken, /* geometry, priority or route is no longer ours */
		LineAbsent  /* the line was never programmed by this owner */
	};
	/* GICv3, the line inside the implemented range, group 1 enabled. Read
	 * before the first store so an unsupported tree fails read-only. */
	static bool geometry(vm_offset_t gic);
	/* Nothing enabled, pending or active: the line is free to take. An
	 * inherited generation is not acknowledged here. */
	static bool claimable(vm_offset_t gic);
	/* Disable, group, level-triggered 1-of-N, priority, route. Written once per
	 * owner; the RWP phase it starts is waited for by retired(), never here. */
	static Verdict program(vm_offset_t gic);
	/* RWP retired (bounded by the caller's deadline) and config/priority/route
	 * still describe what program() left behind. */
	static Verdict retired(vm_offset_t gic, uint64_t now, uint64_t deadline);
	/* Config/priority/route still describe what program() left behind. */
	static bool verify(vm_offset_t gic);
	static void mask(vm_offset_t gic);
	static void unmask(vm_offset_t gic);
	/* Admit one trigger: RWP retired, no active generation of ours, and any
	 * pending bit either acknowledges a clear source or refuses the flight. */
	static Verdict admit(vm_offset_t gic, uint64_t now, uint64_t deadline,
	    uint32_t (*source)(void *), void *context, bool *cleared);
};
#endif /* ARM64_BOARD_CONFIG_MH2LM */
#endif /* SWEET_APPS_LINE_H */
