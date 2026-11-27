/* SM8150 apps RSC command DB: C3-02, one read-only lookup.
 *
 * Contract of this module, in one place:
 *  - The command DB is firmware data behind a 144-byte header. At offset 8 sit
 *    up to eight 16-byte chunk descriptors - slave id, entry and auxiliary
 *    deltas from the header, and an entry count - and the entries themselves
 *    follow at 24 bytes each: an eight-byte name key, the resource address at
 *    +16, the auxiliary length at +20 and the auxiliary delta at +22. Both
 *    numbers at +20 share one word, so an entry costs one load, not two.
 *  - This module reads that block and nothing else. It has no store, no cache,
 *    no allocation and no state of its own: the caller passes the same word
 *    reader its driver already uses for the mapped DB, so memory this card
 *    cannot read is memory it cannot query either.
 *  - Every field is validated before the next load: header geometry, the
 *    slave/size class of each entry, the address high word and alignment, the
 *    auxiliary window, and for class 5 the two auxiliary words. A block that
 *    disagrees fails at the stage that disagreed and the stage comes back, so
 *    a ring can say whether the DB was missing, malformed or simply had no
 *    entry by that name.
 *  - The answer is metadata, never a raw-address API: class, address, the
 *    auxiliary offset/size and, for the classes that carry one, the validated
 *    auxiliary content - class 5's unit and packed width/vcd, class 3's level
 *    table. Decoding it here is what keeps the consumer from writing a second
 *    parser against the same firmware bytes. Nothing here can be turned into a
 *    value to write.
 *  - The rules are the display owner's own (SweetRPMh::lookup/lookupArc/
 *    lookupBCM): same classes, same alignments, same auxiliary sizes, exactly
 *    one match. That is what makes a second reader comparable field by field
 *    with the voter that already accepts the boot.
 */
#ifndef SWEET_RSC_DATABASE_H
#define SWEET_RSC_DATABASE_H

#if defined(ARM64_BOARD_CONFIG_MH2LM)

#include <stdint.h>

/* Slave id classes, as the RSC numbers them. 3 answers with a level table
 * (display ARC), 4 with a rail address, 5 with a bandwidth block. */
enum SweetRSCSlave : unsigned {
	SweetRSCSlaveArc = 3,
	SweetRSCSlaveRail = 4,
	SweetRSCSlaveBcm = 5,
};

/* Which check refused, so a caller can log the stage instead of a bare false. */
enum SweetRSCStage : unsigned {
	SweetRSCStageNone = 0,
	SweetRSCStageName,      /* absent, empty or longer than the 8-byte key */
	SweetRSCStageHeader,    /* chunk descriptor geometry or DB size */
	SweetRSCStageEntry,     /* an entry could not be read */
	SweetRSCStageAddress,   /* address class or alignment, or table alignment */
	SweetRSCStageAux,       /* auxiliary size or window */
	SweetRSCStageValues,    /* class 5 unit/width/vcd/reserved */
	SweetRSCStageMatches,   /* zero or several entries answered */
};

struct SweetRSCResource {
	bool found;
	unsigned slave;         /* SweetRSCSlave of the matching chunk */
	unsigned matches;       /* entries that answered; success means exactly one */
	uint32_t address;       /* resource address a TCS command names */
	uint32_t entry;         /* DB offset of the matched entry, evidence only */
	uint32_t aux;           /* DB offset of the auxiliary block */
	unsigned auxBytes;      /* its declared length */
	/* Class 5 only, already validated; zero for the other classes. */
	uint32_t unit;
	unsigned width, vcd, reserved;
	/* Class 3 only: the level table, decoded and validated here so the ARC
	 * consumer does not need a second parser (first entry zero, strictly
	 * ascending, zero padding only at the end, at least two real levels). */
	unsigned levels[16], levelCount;
};

class SweetRSCDatabase {
public:
	/* Reads one word of the mapped DB. Returns false when the load failed, so a
	 * caller whose mapping is gone cannot get a partial answer. */
	typedef bool (*ReadWord)(void *context, uint32_t offset, uint32_t *value);
	/* Looks one resource up by its 8-byte name. auxMask is the caller's policy
	 * bit for an enable-only resource (zero for a bandwidth resource); it is the
	 * one input that is not in the DB, and it is the caller's to know.
	 * loads, when not null, receives the number of word loads attempted. */
	static bool lookup(void *context, ReadWord read, unsigned dbBytes,
	 const char *name, unsigned auxMask, SweetRSCResource *resource,
	 unsigned *loads, SweetRSCStage *stage);
	static const char *stageName(SweetRSCStage stage);
};

#endif /* ARM64_BOARD_CONFIG_MH2LM */
#endif /* SWEET_RSC_DATABASE_H */
