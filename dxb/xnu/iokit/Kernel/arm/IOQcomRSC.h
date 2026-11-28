/* SM8150 apps RSC channel: C3-01, one read-only instance.
 *
 * Contract of this candidate, in one place:
 *  - The channel maps the apps RSC the existing display owner already maps:
 *    DRV 0x18220000 (0x100), the TCS array 0x18220d00 (0x3000) and the command
 *    DB locator 0x0c3f000c (8), in that ADT reg order. A fourth window is the
 *    command DB itself, resolved from the locator exactly as the owner does,
 *    and mapped read-only. Every resolved physical address and length is
 *    validated before the first register read, so a tree that describes a
 *    different RSC, a short window or a missing range fails start instead of
 *    reading whatever memory happened to be mapped.
 *  - Read-only by construction. This file has no register store, no GIC
 *    mapping, no interrupt registration, no workloop and no TCS or IRQ-mask
 *    write. The completion IRQ is carried as a described resource only:
 *    the ADT says which line the apps RSC owns, and this card records it
 *    without claiming it, because a second physical handler would be a second
 *    owner of the same line in the same boot.
 *  - Identity is read, not assumed: RSC id/major, solver, TCS count and NCPT
 *    out of the config word, and the command DB version/flags/magic from the
 *    locator. A major outside the vendor range, a decoder word of all ones, a
 *    TCS count outside what a 0x3000 array can hold, NCPT outside 3..16 or a
 *    command DB magic this platform does not use are refusals with their own
 *    rows, not degraded reads.
 *  - submit() is the published entry and this card's answer is NotReady. It
 *    never queues, never stores and never retries; the count of attempts is
 *    reported so a ring shows the caller was told no.
 *  - The display owner keeps writing while this channel observes, so the TCS
 *    interrupt-enable/status words are read once and reported as
 *    observed-not-owned. That is the whole fixed read budget: ten loads, zero
 *    stores, and the ring states both.
 *  - C3-02 adds one optional query, off by default: when the tree carries
 *    sweet,rsc-query the channel asks the shared read-only command DB module
 *    for the one resource the display owner already resolves (CN0) and reports
 *    its class, address and auxiliary metadata next to the owner's own row.
 *    The query is a separate read budget with its own counter, so the ten-load
 *    identity budget stays a fixed number and the ring can show the two apart.
 *    A query that fails is a WARN and never a start refusal: the channel's
 *    evidence does not depend on the DB being readable, and the display owner
 *    remains the only writer either way.
 *  - C3-04 adds the second mode, off unless the tree asks for it. With
 *    sweet,rsc-executor the channel is the boot's apps executor: it gains a
 *    fourth ADT range (the GIC window the display node used to carry), takes
 *    the TCS array writable, becomes the one path that stores an RSC word and
 *    the one place INTID37 is enabled and dispatched, and publishes a
 *    SweetAppsDoor table for the display service to reach it. That mode is
 *    decided from the tree alone and never revised at run time: the same file
 *    boots read-only (C3-01/02 evidence, writes=0 by code path) or as the
 *    owner, and only one side can be the owner in a given boot.
 */
#ifndef IOQCOM_RSC_H
#define IOQCOM_RSC_H

#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include "SweetAppsDoor.h"
#include "SweetAppsLine.h"

#if defined(ARM64_BOARD_CONFIG_MH2LM)

/* The three ADT reg ranges are the window ABI; the command DB is the fourth
 * window and it is resolved, never declared. Keeping the two counts apart is
 * what lets the map rows and the range check disagree loudly. In the executor
 * mode a fourth ADT range (the GIC distributor window) is declared after the
 * three, so the range count and the window count both grow by one and the two
 * modes stay distinguishable in the ring. */
enum : unsigned {
	kIOQcomRscRangeCount = 3,
	kIOQcomRscExecutorRangeCount = 4,
	kIOQcomRscWindowCount = 5,
	/* The windows a tree that does not delegate has: the same ids minus the
	 * GIC, and the number the C3-01..03 rings printed. Keeping it separate is
	 * what lets the start row say how many windows this boot actually holds
	 * instead of how many the driver could hold. */
	kIOQcomRscMappedWindowCount = 4,
};
enum : unsigned {
	kIOQcomRscWindowDrv = 0,
	kIOQcomRscWindowTCS = 1,
	kIOQcomRscWindowLocator = 2,
	kIOQcomRscWindowDB = 3,
	kIOQcomRscWindowGIC = 4,
};

/* C3-04: the executor opt-in. The property is the whole owner switch; the
 * window, the store licence and the IRQ claim all follow from it, and a boot
 * without it behaves exactly as C3-01/02 shipped. */
enum : uint32_t {
	kIOQcomRscExecutorAbi = 1,
	kIOQcomRscExecutorGicPhysical = 0x17a00000U,
	kIOQcomRscExecutorGicLength = 0x10000U,
};

/* Vendor geometry, taken from the apps RSC node the display owner already
 * validates (drivers/soc/qcom/rpmh-rsc.c + sm8150.dtsi): DRV is one 0x100
 * page, the TCS array is 0x3000 long with a 0x2a0 stride, and the locator is
 * two words. An array of 0x3000 bytes holds at most 18 TCS; the vendor's own
 * trees declare 8..18, so anything outside that is somebody else's node. */
enum : uint32_t {
	kIOQcomRscAbi = 1,
	kIOQcomRscTcsStride = 0x2a0,
	kIOQcomRscTcsMinCount = 8,
	kIOQcomRscTcsMaxCount = 0x3000 / kIOQcomRscTcsStride,
};

/* DRV identity words. The config register packs the TCS count in bits 12..17
 * and NCPT in bits 27..31; the major version lives in bits 16..23 of the id
 * word. Pre-v3 RSC (major 1) and v3 (major 2) are the only layouts the vendor
 * driver accepts, and the vendor declares 3..16 channels per TCS. */
enum : uint32_t {
	kIOQcomRscMaxMajor = 2,
	kIOQcomRscMinNcpt = 3,
	kIOQcomRscMaxNcpt = 16,
};

/* Command DB: the locator returns the physical address and byte length; the
 * block starts with a version word, the platform magic and the flag word whose
 * bit 0 means "not compatible". The magic is the only field that can be a hard
 * refusal here - the display owner reads the same three words and merely warns
 * about version/flags, so a second reader that refused them would reject trees
 * the boot's own voter accepts. */
enum : uint32_t {
	kIOQcomRscDbMagic = 0x0c0330dbu,
	kIOQcomRscDbHeaderBytes = 144,
	/* The flag word is the header's last word, not the word after the version
	 * and the magic. The display owner reads offset 140 for the same bit, and a
	 * channel that read 8 would report the first entry's bytes as flags and warn
	 * on a boot the voter accepts. */
	kIOQcomRscDbFlagsOffset = 140,
	kIOQcomRscDbMinPhysical = 0x80000000u,
	kIOQcomRscDbMaxBytes = 0x100000,
};

/* Completion line: LG's SM8150 DTS gives the apps RSC SPI5, i.e. INTID37,
 * level high. Nothing here touches the GIC; the number is the description this
 * card hands to the next one. */
enum : uint32_t { kIOQcomRscIntid = 37, kIOQcomRscIrqAbi = 1 };

/* C3-02: the query opt-in and the one resource it asks about. The name and the
 * zero auxiliary mask are the display owner's own entry for CN0
 * (SweetRPMh.cpp, displayBCMs), so the two rows in a ring are comparable field
 * by field: same name, same class, same address, same unit/width/vcd. */
enum : uint32_t { kIOQcomRscQueryAbi = 1, kIOQcomRscQueryAuxMask = 0 };
static const char *const kIOQcomRscQueryName = "CN0";

class IOQcomRSCChannel : public IOService {
	OSDeclareDefaultStructors(IOQcomRSCChannel);
	struct Window {
		uint64_t physical;      /* expected resolved PA (ADT range, or locator) */
		IOByteCount length;     /* declared length, also the bounds limit */
		IOVirtualAddress base;  /* kernel virtual base, 0 when unmapped */
		IOMemoryMap *map;       /* owned mapping, released on stop/free */
	};
	IOLock *fLock;
	Window fWindow[kIOQcomRscWindowCount];
	/* What the channel read, kept as evidence for the ring and for the next
	 * card. fValid means every refusal below passed; nothing else in this
	 * object is allowed to imply a writable channel. */
	struct Identity {
		uint32_t id, solver, config;
		uint32_t major, tcsCount, ncpt;
		uint32_t dbAddress, dbBytes, dbVersion, dbFlags;
		uint32_t irqEnable, irqStatus;
		/* C3-02 answer for the queried resource, zero when no query ran. */
		uint32_t queryAddress, queryUnit;
		unsigned querySlave, queryWidth, queryVcd, queryAuxBytes;
		bool queryDone;
		bool valid;
	} fIdentity;
	uint32_t fIntid;
	unsigned fReads, fReadFailures, fSubmitAttempts;
	/* Query loads are counted apart from the identity budget: one is a fixed
	 * number this card promises, the other depends on where the entry sits. */
	unsigned fQueryReads;
	bool fQueryEnabled;
	bool fMapped, fRegistered, fAccepting;
	/* C3-04. fExecutor is decided once, from the tree, before the first
	 * register access; everything below it exists only in that mode, so a
	 * read-only boot carries no map, no event source and no door. */
	bool fExecutor;
	vm_offset_t fGIC;
	IOWorkLoop *fLoop;
	IOInterruptEventSource *fIRQEvent;
	bool fIRQEventAdded, fDoorPublished;
	unsigned fStores, fDoorReads;
	unsigned fIRQEntries, fIRQArmed, fIRQPosted, fIRQOnline, fIRQSources;
	bool fIRQTouched, fIRQConfigured;
	uint64_t fIRQDeadline;
	/* The display service's completion entry, installed once through the door.
	 * Second installers are refused: the line has one policy client. */
	void *fCompletionTarget;
	bool (*fCompletion)(void *);
	SweetAppsDoor fDoor;
	bool validateProperties(IOService *provider);
	bool setupExecutor(IOService *provider);
	void buildDoor(void);
	void stopExecutor(void);
	/* Door-side TCS access. The store below is the only one in this file, it is
	 * reachable only when fExecutor is set, and it carries the barriers this
	 * SoC needs on a VM_WIMG_IO mapping. */
	uint32_t tcsRead(uint32_t offset);
	void tcsWrite(uint32_t offset, uint32_t value);
	void lineQuiet(uint32_t bit);
	IOReturn lineTriggerReady(void);
	bool handleIRQ(void);
	static void irqAction(OSObject *owner, IOInterruptEventSource *source, int count);
	static uint32_t doorRead(void *context, uint32_t offset);
	static void doorWrite(void *context, uint32_t offset, uint32_t value);
	static bool doorIdentity(void *context, uint32_t *id, uint32_t *solver,
	 uint32_t *config);
	static IOReturn doorTriggerReady(void *context);
	static void doorArm(void *context);
	static void doorUnmask(void *context);
	static void doorQuiet(void *context, uint32_t bit);
	static void doorStop(void *context);
	static bool doorIrq(void *context);
	/* The status word the line-admission step consults. It is a static member
	 * rather than a file-scope helper because tcsRead() stays protected: the
	 * only caller is this class, and the card's claim is that no second scope
	 * can reach the array. */
	static uint32_t statusThunk(void *context);
	static bool doorInstall(void *context, void *target, bool (*callback)(void *));
	static unsigned doorStores(void *context);
	static unsigned doorIrqs(void *context);
	bool mapWindows(IOService *provider);
	bool readIdentity(void);
	bool mapCommandDB(void);
	void queryCommandDB(void);
	bool observeTCS(void);
	bool describeIRQ(IOService *provider);
	void unmapWindows(void);
	/* The only register accessor in this file. Bounds and alignment are checked
	 * against the declared window before the load, the load carries the read
	 * barrier this SoC needs, and the access runs under the channel lock so a
	 * late caller cannot observe a half-released mapping. Loads only: there is
	 * no store counterpart to audit, which is the card's whole point. The load
	 * counter is a parameter so the query can charge its own budget. */
	bool readWindow(unsigned window, uint32_t offset, uint32_t *value,
	 unsigned *loads);
	bool readRegister(unsigned window, uint32_t offset, uint32_t *value);
	/* The command DB reader handed to the shared query module: same accessor,
	 * same DB window, its own counter. */
	bool readDatabaseWord(uint32_t offset, uint32_t *value);
	static bool readDatabaseThunk(void *context, uint32_t offset, uint32_t *value);
public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;
	/* Published entry. This card answers NotReady with a row that says so; it
	 * has no queue, no deferred write and no retry to hide the refusal behind.
	 * The token is cleared on every call so a caller cannot mistake "no
	 * answer" for "accepted". */
	IOReturn submit(uint64_t client, uint32_t address, uint32_t value,
	 uint64_t *token);
	unsigned channelReads(void) const { return fReads; }
	/* C3-04: the store count and the owner flag are read by the ring rows and by
	 * the host fixture, never by a client. */
	unsigned channelStores(void) const { return fStores; }
	bool channelIsExecutor(void) const { return fExecutor; }
	bool channelSubmitAllowed(void) const { return false; }
};

#endif /* ARM64_BOARD_CONFIG_MH2LM */
#endif /* IOQCOM_RSC_H */
