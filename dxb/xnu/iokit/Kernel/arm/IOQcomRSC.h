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
 *    observed-not-owned. That is the whole read budget: ten loads, zero
 *    stores, and the ring states both.
 */
#ifndef IOQCOM_RSC_H
#define IOQCOM_RSC_H

#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>

#if defined(ARM64_BOARD_CONFIG_MH2LM)

/* The three ADT reg ranges are the window ABI; the command DB is the fourth
 * window and it is resolved, never declared. Keeping the two counts apart is
 * what lets the map rows and the range check disagree loudly. */
enum : unsigned { kIOQcomRscRangeCount = 3, kIOQcomRscWindowCount = 4 };
enum : unsigned {
	kIOQcomRscWindowDrv = 0,
	kIOQcomRscWindowTCS = 1,
	kIOQcomRscWindowLocator = 2,
	kIOQcomRscWindowDB = 3,
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
		bool valid;
	} fIdentity;
	uint32_t fIntid;
	unsigned fReads, fReadFailures, fSubmitAttempts;
	bool fMapped, fRegistered, fAccepting;
	bool validateProperties(IOService *provider);
	bool mapWindows(IOService *provider);
	bool readIdentity(void);
	bool mapCommandDB(void);
	bool observeTCS(void);
	bool describeIRQ(IOService *provider);
	void unmapWindows(void);
	/* The only register accessor in this file. Bounds and alignment are checked
	 * against the declared window before the load, the load carries the read
	 * barrier this SoC needs, and the access runs under the channel lock so a
	 * late caller cannot observe a half-released mapping. Loads only: there is
	 * no store counterpart to audit, which is the card's whole point. */
	bool readRegister(unsigned window, uint32_t offset, uint32_t *value);
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
	bool channelSubmitAllowed(void) const { return false; }
};

#endif /* ARM64_BOARD_CONFIG_MH2LM */
#endif /* IOQCOM_RSC_H */