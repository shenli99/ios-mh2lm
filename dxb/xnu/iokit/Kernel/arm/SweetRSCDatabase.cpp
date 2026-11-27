/* SM8150 apps RSC command DB: C3-02, one read-only lookup.
 *
 * The whole module is a walk over firmware data. Every load goes through the
 * caller's reader, every field is validated before the next load, and the only
 * outputs are the class, the address, the auxiliary window and - for a
 * bandwidth resource - the two validated auxiliary words. There is no store
 * here, no scratch buffer and no state that outlives one call.
 */
#include "SweetRSCDatabase.h"

#if defined(ARM64_BOARD_CONFIG_MH2LM)

/* Firmware layout, taken from the vendor's rpmh-rsc command DB and from the
 * display owner in this tree, which already parses the same block. */
enum : uint32_t {
	kSweetRscDbHeader = 144,  /* bytes of header before any entry table */
	kSweetRscDbEntry = 24,    /* name 0..7, address 16, aux size 20, aux delta 22 */
	kSweetRscDbChunkBase = 8, /* first chunk descriptor, inside the header */
	kSweetRscDbChunkStride = 16,
};
enum : unsigned { kSweetRscDbChunks = 8, kSweetRscDbKeyBytes = 8 };

/* One load, counted whether or not it answered: a ring that shows the attempts
 * is more useful than one that shows the successes. */
static bool
sweetRscWord(void *context, SweetRSCDatabase::ReadWord read, uint32_t offset,
 uint32_t *value, unsigned *loads)
{
	if (loads) ++*loads;
	if (offset & 3u) return false;
	return read && read(context, offset, value);
}

/* A window is inside the block when it starts inside it and fits in what is
 * left. Both bounds are unsigned and the subtraction cannot wrap because the
 * first test already failed. */
static bool
sweetRscRanges(unsigned dbBytes, uint32_t offset, uint32_t bytes)
{
	return offset <= dbBytes && bytes <= dbBytes - offset;
}

bool
SweetRSCDatabase::lookup(void *context, ReadWord read, unsigned dbBytes,
 const char *name, unsigned auxMask, SweetRSCResource *resource,
 unsigned *loads, SweetRSCStage *stage)
{
	SweetRSCStage refused = SweetRSCStageNone;
	SweetRSCResource found = {};

	if (loads) *loads = 0;
	if (!read || !name || !resource || !dbBytes) {
		if (stage) *stage = SweetRSCStageName;
		return false;
	}
	if (dbBytes < kSweetRscDbHeader) {
		if (stage) *stage = SweetRSCStageHeader;
		return false;
	}
	/* The key is the name as eight bytes, the way the owner builds it: the
	 * string has to end inside those eight, so a longer name is a refusal
	 * rather than a truncated match. */
	uint32_t key[2] = { 0, 0 };
	unsigned chars = 0;
	while (chars < kSweetRscDbKeyBytes && name[chars])
		key[chars / 4] |= (uint32_t)(uint8_t)name[chars] << (8 * (chars % 4)), ++chars;
	if (!chars || name[chars]) {
		if (stage) *stage = SweetRSCStageName;
		return false;
	}
	unsigned matches = 0;
	for (unsigned i = 0; i < kSweetRscDbChunks && !refused; i++) {
		uint32_t at = kSweetRscDbChunkBase + kSweetRscDbChunkStride * i;
		uint32_t head = 0, tail = 0;
		if (!sweetRscWord(context, read, at, &head, loads) ||
		    !sweetRscWord(context, read, at + 4, &tail, loads)) {
			refused = SweetRSCStageHeader;
			break;
		}
		unsigned slave = head & 0xffffu;
		/* A zero slave ends the descriptor list: the firmware does not leave
		 * holes and the owner stops here too. */
		if (!slave) break;
		uint32_t entries = kSweetRscDbHeader + (head >> 16);
		uint32_t aux = kSweetRscDbHeader + (tail & 0xffffu);
		unsigned count = tail >> 16;
		/* Only the entry table has to be word aligned: the owner's header
		 * gate asks for `range(aux, 0)` and nothing more, and the auxiliary
		 * block is addressed in halfwords by an entry that carries its own
		 * alignment rule. Requiring alignment of the base would refuse a
		 * chunk the owner accepts, and this card's claim is that the two
		 * readers agree - a stricter second reader is still a disagreement. */
		if (slave < SweetRSCSlaveArc || slave > SweetRSCSlaveBcm ||
		    (entries & 3u) ||
		    !sweetRscRanges(dbBytes, entries, count * kSweetRscDbEntry) ||
		    !sweetRscRanges(dbBytes, aux, 0)) {
			refused = SweetRSCStageHeader;
			break;
		}
		for (unsigned j = 0; j < count; j++) {
			uint32_t entry = entries + kSweetRscDbEntry * j;
			uint32_t word0 = 0, word1 = 0, address = 0, auxWords = 0;
			if (!sweetRscWord(context, read, entry, &word0, loads) ||
			    !sweetRscWord(context, read, entry + 4, &word1, loads)) {
				refused = SweetRSCStageEntry;
				break;
			}
			if (word0 != key[0] || word1 != key[1]) continue;
			if (!sweetRscWord(context, read, entry + 16, &address, loads) ||
			    !sweetRscWord(context, read, entry + 20, &auxWords, loads)) {
				refused = SweetRSCStageEntry;
				break;
			}
			unsigned auxBytes = auxWords & 0xffffu;
			uint32_t table = aux + (auxWords >> 16);
			uint32_t unit = 0;
			unsigned width = 0, vcd = 0, reserved = 0;
			unsigned levels[16] = {}, levelCount = 0;
			if (slave == SweetRSCSlaveRail) {
				/* A rail answers with a VRM address: high word 4, sixteen
				 * byte slots, and an auxiliary block inside the DB. */
				if ((address >> 16) != 4 || (address & 15u)) {
					refused = SweetRSCStageAddress;
					break;
				}
				if (!sweetRscRanges(dbBytes, table, auxBytes)) {
					refused = SweetRSCStageAux;
					break;
				}
			} else if (slave == SweetRSCSlaveArc) {
				/* An ARC resource answers with a level table: high word 3,
				 * word-aligned address and table, four to thirty-two bytes of
				 * even length, and the table padded to its own alignment. */
				if ((address >> 16) != 3 || (address & 3u) || (table & 1u)) {
					refused = SweetRSCStageAddress;
					break;
				}
				if (auxBytes < 4 || auxBytes > 32 || (auxBytes & 1u) ||
				    !sweetRscRanges(dbBytes, table, auxBytes) ||
				    !sweetRscRanges(dbBytes, table & ~3u,
				     ((table & 3u) + auxBytes + 3u) & ~3u)) {
					refused = SweetRSCStageAux;
					break;
				}
				/* The table is a sequence of 16-bit levels and it is only
				 * halfword aligned, so the word holding a level is the one
				 * below it: the owner's db16() does the same, and a load at the
				 * halfword address would fault on this mapping. One load covers
				 * both levels of a word, so the loop reloads only when the word
				 * changes. The head level is zero, the rest ascend, and once a
				 * zero appears the tail must be zeros: that is what the consumer
				 * downstream will index into. */
				uint32_t pair = 0, pairAt = ~0u;
				bool padding = false, bad = false;
				for (unsigned k = 0; k < auxBytes / 2u; k++) {
					uint32_t off = table + 2u * k;
					if ((off & ~3u) != pairAt) {
						if (!sweetRscWord(context, read, off & ~3u,
						    &pair, loads)) {
							bad = true;
							break;
						}
						pairAt = off & ~3u;
					}
					unsigned level = (pair >> (8u * (off & 3u))) & 0xffffu;
					if (!k) {
						if (level) bad = true;
					} else if (!level) {
						padding = true;
					} else if (padding || level <= levels[levelCount - 1]) {
						bad = true;
					}
					if (bad) break;
					if (!padding && levelCount < 16) levels[levelCount++] = level;
				}
				if (bad || levelCount < 2) {
					refused = SweetRSCStageValues;
					break;
				}
			} else {
				/* A bandwidth resource answers with two words: the unit and
				 * the packed width/vcd, both of which have to make sense
				 * before anyone votes with them. */
				if ((address >> 16) != 5 || (address & 3u)) {
					refused = SweetRSCStageAddress;
					break;
				}
				uint32_t packed = 0;
				if (auxBytes != 8 || (table & 3u) ||
				    !sweetRscRanges(dbBytes, table, 8) ||
				    !sweetRscWord(context, read, table, &unit, loads) ||
				    !sweetRscWord(context, read, table + 4, &packed, loads)) {
					refused = SweetRSCStageAux;
					break;
				}
				width = packed & 0xffffu;
				vcd = (packed >> 16) & 255u;
				reserved = packed >> 24;
				/* An enable-only resource carries its meaning in the mask, so
				 * a zero width is fine there; a bandwidth resource without a
				 * width is not. */
				if (!unit || (!width && !auxMask) || vcd >= 16 || reserved) {
					refused = SweetRSCStageValues;
					break;
				}
			}
			found.slave = slave;
			found.address = address;
			found.entry = entry;
			found.aux = table;
			found.auxBytes = auxBytes;
			found.unit = unit;
			found.width = width;
			found.vcd = vcd;
			found.reserved = reserved;
			found.levelCount = levelCount;
			for (unsigned k = 0; k < levelCount; k++) found.levels[k] = levels[k];
			++matches;
		}
	}
	if (!refused && matches != 1) refused = SweetRSCStageMatches;
	if (refused) {
		if (stage) *stage = refused;
		return false;
	}
	found.found = true;
	found.matches = matches;
	*resource = found;
	if (stage) *stage = SweetRSCStageNone;
	return true;
}

const char *
SweetRSCDatabase::stageName(SweetRSCStage stage)
{
	switch (stage) {
	case SweetRSCStageName: return "name";
	case SweetRSCStageHeader: return "header";
	case SweetRSCStageEntry: return "entry";
	case SweetRSCStageAddress: return "address";
	case SweetRSCStageAux: return "aux";
	case SweetRSCStageValues: return "values";
	case SweetRSCStageMatches: return "matches";
	default: return "none";
	}
}

#endif /* ARM64_BOARD_CONFIG_MH2LM */
