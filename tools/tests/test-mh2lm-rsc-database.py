#!/usr/bin/env python3
"""Actual C3-02 command DB query module against a fixture-built database.
The module is compiled as written and reads through a fixture reader (the same
word-granular accessor the channel hands it), so the fixture is also the only
place the DB layout is written down twice: the fixture builds the block from a
high-level description and the module parses it back. Every gate the module
declares has a two-sided case - the same block with the one field fixed must
pass - and the accepted cases pin the load count and the entry/auxiliary
offsets, so an off-by-one in a stride or a header size fails here instead of in
a ring.
Usage: test-mh2lm-rsc-database.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
arm = Path(sys.argv[1]) / 'iokit' / 'Kernel' / 'arm'
header = (arm / 'SweetRSCDatabase.h').read_text()
source = (arm / 'SweetRSCDatabase.cpp').read_text()
# The module is header-only in its dependencies, so the fixture strips the
# includes and supplies the board define itself.
for name, text in (('header', header), ('source', source)):
    if '#include' not in text:
        sys.exit('%s: expected the shipped include lines' % name)
strip = lambda text: re.sub(r'^#include.*$', '', text, flags=re.M)
header, source = strip(header), strip(source)
if 'ARM64_BOARD_CONFIG_MH2LM' not in source:
    sys.exit('source: expected the board guard')

prelude = r'''
#include <cstdio>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#define ARM64_BOARD_CONFIG_MH2LM 1
'''

fixture = r'''
/* ---- the database the fixture builds, from a description ---- */
/* Literals, not the module's constants: a wrong constant in the module has to
   fail here, and a fixture that shares it would agree with the mistake. */
static constexpr uint32_t HDR = 144, ENT = 24, CHUNK = 8, STRIDE = 16;

struct Db {
 std::map<uint32_t, uint32_t> words;
 unsigned loads = 0, misaligned = 0;
};
static bool reader(void *context, uint32_t off, uint32_t *value)
{
 Db *db = (Db *)context;
 if (off & 3u) { ++db->misaligned; return false; }
 ++db->loads;
 auto it = db->words.find(off);
 if (it == db->words.end()) return false;
 *value = it->second;
 return true;
}
static void put(Db &db, uint32_t off, uint32_t word) { db.words[off] = word; }

struct Entry {
 std::string name;
 uint32_t address = 0;
 std::vector<uint32_t> aux; /* written 4-aligned, little endian */
 unsigned declared = 0;     /* declared auxiliary bytes; 0 means 4*aux.size() */
};
struct Chunk {
 unsigned slave;
 std::vector<Entry> entries;
 /* Bytes of padding before this chunk's auxiliary base. The owner's header gate
  * range-checks that base and nothing else, so a real block may place it on a
  * halfword; a reader that also demands word alignment reads those chunks as
  * absent. */
 unsigned skew = 0;
};
struct Layout {
 std::vector<uint32_t> entries, aux;
 uint32_t end = 0; /* one past the last byte written */
 unsigned entryOf(size_t chunk, size_t index) const
 { return entries[chunk] + (unsigned)index * ENT; }
};

static Layout build(Db &db, const std::vector<Chunk> &chunks)
{
 Layout layout;
 uint32_t total = 0;
 for (auto &c : chunks) total += (uint32_t)c.entries.size();
 uint32_t e = HDR, a = HDR + total * ENT;
 for (uint32_t i = 0; i < chunks.size(); ++i) {
  layout.entries.push_back(e);
  a += chunks[i].skew;
  layout.aux.push_back(a);
  for (uint32_t j = 0; j < chunks[i].entries.size(); ++j) {
   const Entry &entry = chunks[i].entries[j];
   assert(entry.name.size() <= 8);
   uint32_t key[2] = {0, 0};
   for (size_t k = 0; k < entry.name.size(); ++k)
    key[k / 4] |= (uint32_t)(uint8_t)entry.name[k] << (8 * (k % 4));
   uint32_t off = e + j * ENT;
   put(db, off, key[0]);
   put(db, off + 4, key[1]);
   put(db, off + 16, entry.address);
   unsigned declared = entry.declared ? entry.declared
    : (unsigned)entry.aux.size() * 4;
   /* The entry's auxiliary delta is relative to its chunk's auxiliary base,
      which is what the display owner adds it to: a delta measured from the
      header instead lands every table 144 bytes late. */
   put(db, off + 20, declared | ((a - layout.aux[i]) << 16));
   for (size_t w = 0; w < entry.aux.size(); ++w) put(db, a + 4 * (uint32_t)w, entry.aux[w]);
   a += (uint32_t)entry.aux.size() * 4;
  }
  e += (uint32_t)chunks[i].entries.size() * ENT;
  put(db, CHUNK + STRIDE * i, chunks[i].slave | ((layout.entries[i] - HDR) << 16));
  put(db, CHUNK + STRIDE * i + 4,
   (layout.aux[i] - HDR) | ((uint32_t)chunks[i].entries.size() << 16));
 }
 /* Every descriptor slot inside the header exists even when no chunk uses it:
   the slot is inside the 144-byte header, so an unreadable one would mean the
   mapping is gone, not that the list is short. Unused slots are the zero slave
   that ends the walk. */
 for (uint32_t i = (uint32_t)chunks.size(); i < 8; ++i) {
  put(db, CHUNK + STRIDE * i, 0);
  put(db, CHUNK + STRIDE * i + 4, 0);
 }
 layout.end = a;
 return layout;
}

static SweetRSCResource run(Db &db, unsigned dbBytes, const char *name,
 unsigned mask, unsigned *loads = nullptr, SweetRSCStage *stage = nullptr)
{
 SweetRSCResource resource = {};
 unsigned count = 0;
 SweetRSCStage refused = SweetRSCStageNone;
 db.loads = 0;
 db.misaligned = 0;
 bool ok = SweetRSCDatabase::lookup(&db, reader, dbBytes, name, mask, &resource,
  &count, &refused);
 /* The counter the caller sees has to be the number of loads that happened,
   and the module must never hand the reader a misaligned offset: the channel's
   DB mapping is word-addressed. */
 assert(db.misaligned == 0);
 assert(db.loads == count);
 assert(ok == resource.found);
 if (loads) *loads = count;
 if (stage) *stage = refused;
 return resource;
}
static void refuses(Db &db, unsigned dbBytes, const char *name,
 SweetRSCStage want, unsigned mask = 0)
{
 SweetRSCStage stage = SweetRSCStageNone;
 SweetRSCResource resource = run(db, dbBytes, name, mask, nullptr, &stage);
 assert(!resource.found);
 assert(stage == want);
}

/* The display owner's own resource set: four rails, one ARC supply and the
   panel's three bandwidth resources. CN0's unit/width/vcd are the numbers the
   owner prints for it on this platform. */
static std::vector<Chunk> base()
{
 return {
  {4, {{"ldoa14", 0x42100}, {"ldoa17", 0x42200}, {"ldoc3", 0x42300}, {"ldoa5", 0x42400}}},
  {3, {{"mx", 0x30000, {0x00010000u, 0x00030002u, 0u}}}},
  {5, {{"CN0", 0x500a8, {20000u, (5u << 16) | 4u}},
       {"ACV", 0x50ac0, {1u, 0u}}}},
 };
}

static void accepted()
{
 Db db; Layout layout = build(db, base());
 unsigned loads = 0;
 SweetRSCResource r = run(db, layout.end, "CN0", 0, &loads);
 assert(r.found && r.slave == SweetRSCSlaveBcm && r.matches == 1);
 assert(r.address == 0x500a8u && r.unit == 20000u);
 assert(r.width == 4u && r.vcd == 5u && r.reserved == 0u && r.auxBytes == 8u);
 /* The offsets are evidence a ring can be reconciled with: the entry sits where
   the descriptor says and the auxiliary block where the entry says. */
 assert(r.entry == (uint32_t)layout.entryOf(2, 0));
 assert(r.aux == layout.aux[2]);
 /* What the query costs: four descriptor slots (the three chunks and the zero
   slave that ends the walk), one key for each of the seven entries - a name is
   only known by reading it, so the whole name column is scanned - and CN0's two
   field words plus its two auxiliary words. Twenty-six loads is the number a
   ring has to show, and the size of the name column is why it is not a
   constant. */
 assert(loads == 4 * 2 + 7 * 2 + 4);

 r = run(db, layout.end, "ldoa14", 0);
 assert(r.found && r.slave == SweetRSCSlaveRail && r.address == 0x42100u);
 assert(r.auxBytes == 0u && r.unit == 0u && r.width == 0u && r.levelCount == 0u);
 assert(r.entry == (uint32_t)layout.entryOf(0, 0));

 r = run(db, layout.end, "ldoa5", 0);
 assert(r.found && r.address == 0x42400u && r.entry == (uint32_t)layout.entryOf(0, 3));

 r = run(db, layout.end, "mx", 0);
 assert(r.found && r.slave == SweetRSCSlaveArc && r.address == 0x30000u);
 assert(r.auxBytes == 12u && r.levelCount == 4u);
 assert(r.levels[0] == 0u && r.levels[1] == 1u && r.levels[2] == 2u && r.levels[3] == 3u);

 /* ACV is enable-only: its width is zero and the caller's mask is the only
   thing that makes that legal, so the same bytes answer differently to the two
   callers. */
 r = run(db, layout.end, "ACV", 8u);
 assert(r.found && r.address == 0x50ac0u && r.width == 0u && r.vcd == 0u);
 refuses(db, layout.end, "ACV", SweetRSCStageValues, 0u);
}

static void nameStage()
{
 Db db; Layout layout = build(db, base());
 refuses(db, layout.end, "", SweetRSCStageName);
 refuses(db, layout.end, "CN0-extra", SweetRSCStageName);
 refuses(db, layout.end, "ABCDEFGHI", SweetRSCStageName);
 /* Eight characters is the key, so it is a legal name even when nothing has
   it: the refusal is then about the entry, not the name. */
 refuses(db, layout.end, "ZZZZZZZZ", SweetRSCStageMatches);
 /* A shorter name is a different key, never a prefix match. */
 refuses(db, layout.end, "CN", SweetRSCStageMatches);
}

static void headerStage()
{
 Db db; Layout layout = build(db, base());
 /* Short of the header: the descriptors cannot all be there. */
 refuses(db, 0, "CN0", SweetRSCStageName);
 refuses(db, HDR - 4, "CN0", SweetRSCStageHeader);
 refuses(db, HDR, "CN0", SweetRSCStageHeader);

 /* A slave outside the three classes this platform numbers. */
 std::vector<Chunk> chunks = base();
 chunks[2].slave = 6;
 Db wrong; Layout wrongLayout = build(wrong, chunks);
 refuses(wrong, wrongLayout.end, "CN0", SweetRSCStageHeader);

 /* Entries that start off a word boundary, and an entry table that does not
   fit in the block. */
 chunks = base();
 Db shifted; Layout shiftedLayout = build(shifted, chunks);
 put(shifted, CHUNK + STRIDE * 1, chunks[1].slave | ((uint32_t)(2u) << 16));
 refuses(shifted, shiftedLayout.end, "CN0", SweetRSCStageHeader);
 /* A block that ends inside the last chunk's entry table: the entries cannot
   all be there, so the header is what disagrees, not an entry read. */
 refuses(db, layout.entries[2] + ENT, "CN0", SweetRSCStageHeader);

 /* The auxiliary base past the end of the block. */
 Db auxOut = db;
 put(auxOut, CHUNK + STRIDE * 2 + 4,
  (0xff00u) | ((uint32_t)chunks[2].entries.size() << 16));
 refuses(auxOut, layout.end, "CN0", SweetRSCStageHeader);

 /* A chunk whose auxiliary base is not word aligned is still a readable chunk:
   the owner's header gate only range-checks that base, and the entry carries
   its own alignment rule. Demanding alignment of the base here would make this
   reader refuse a block the display owner resolves, which is the one kind of
   disagreement this card cannot have. The rail entry is the sharpest case: it
   reads no auxiliary word at all, so the base is the only thing the gate sees. */
 std::vector<Chunk> skewed(3);
 skewed[0].slave = 4;
 skewed[1].slave = 4;
 skewed[2].slave = 5;
 skewed[1].skew = 2;
 skewed[0].entries.push_back({"vddcx", 0x40010, {}, 4});
 skewed[1].entries.push_back({"ldoa14", 0x40020, {}, 4});
 skewed[2].entries.push_back({"CN0", 0x500a8, {20000, (5u << 16) | 4u}, 8});
 Db odd; Layout oddLayout = build(odd, skewed);
 assert(oddLayout.aux[1] % 4u != 0);
 SweetRSCResource rail = run(odd, oddLayout.end, "ldoa14", 0);
 assert(rail.found && rail.slave == 4 && rail.address == 0x40020);
}

static void entryStage()
{
 Db db; Layout layout = build(db, base());
 /* An unreadable entry word is its own stage: the block is fine, the load was
   not. Both halves of the key matter, so either one missing refuses. */
 Db missing = db;
 missing.words.erase(layout.entryOf(2, 0));
 refuses(missing, layout.end, "CN0", SweetRSCStageEntry);
 missing = db;
 missing.words.erase(layout.entryOf(2, 0) + 4);
 refuses(missing, layout.end, "CN0", SweetRSCStageEntry);
 missing = db;
 missing.words.erase(layout.entryOf(2, 0) + 16);
 refuses(missing, layout.end, "CN0", SweetRSCStageEntry);
 missing = db;
 missing.words.erase(layout.entryOf(2, 0) + 20);
 refuses(missing, layout.end, "CN0", SweetRSCStageEntry);
 /* A descriptor that cannot be read is the header stage, not the entry one. */
 missing = db;
 missing.words.erase(CHUNK + STRIDE * 2);
 refuses(missing, layout.end, "CN0", SweetRSCStageHeader);
}

static void addressStage()
{
 /* Rail: high word must be 4 and the address must sit on a sixteen byte slot. */
 std::vector<Chunk> chunks = base();
 chunks[0].entries[0].address = 0x52100;
 Db db; Layout layout = build(db, chunks);
 refuses(db, layout.end, "ldoa14", SweetRSCStageAddress);
 chunks[0].entries[0].address = 0x42104;
 Db misaligned; Layout misalignedLayout = build(misaligned, chunks);
 refuses(misaligned, misalignedLayout.end, "ldoa14", SweetRSCStageAddress);

 /* BCM: high word 5, word aligned address. */
 chunks = base();
 chunks[2].entries[0].address = 0x400a8;
 Db bcm; Layout bcmLayout = build(bcm, chunks);
 refuses(bcm, bcmLayout.end, "CN0", SweetRSCStageAddress);
 chunks[2].entries[0].address = 0x500aa;
 Db odd; Layout oddLayout = build(odd, chunks);
 refuses(odd, oddLayout.end, "CN0", SweetRSCStageAddress);

 /* ARC: high word 3, word aligned address, halfword aligned table. The table
   moves by two bytes, which shifts every level under the reader. */
 chunks = base();
 chunks[1].entries[0].address = 0x20000;
 Db arc; Layout arcLayout = build(arc, chunks);
 refuses(arc, arcLayout.end, "mx", SweetRSCStageAddress);

 /* The class comes from the chunk, so a bandwidth address in the rail chunk is
   refused even though the entry itself is well formed. */
 chunks = base();
 chunks[0].entries[1].address = 0x500a8;
 Db other; Layout otherLayout = build(other, chunks);
 refuses(other, otherLayout.end, "ldoa17", SweetRSCStageAddress);
 /* The same block still answers for its other entries: one bad entry does not
   poison a lookup that does not name it. */
 assert(run(other, otherLayout.end, "ldoa14", 0).found);
}

static void auxStage()
{
 /* BCM: exactly two auxiliary words, word aligned. */
 std::vector<Chunk> chunks = base();
 chunks[2].entries[0].aux = {20000u};
 Db shortAux; Layout shortLayout = build(shortAux, chunks);
 refuses(shortAux, shortLayout.end, "CN0", SweetRSCStageAux);
 chunks[2].entries[0].aux = {20000u, (5u << 16) | 4u, 7u};
 Db longAux; Layout longLayout = build(longAux, chunks);
 refuses(longAux, longLayout.end, "CN0", SweetRSCStageAux);
 /* Declared eight bytes but only one word behind them, with nothing after it
   that could be mistaken for the second one. */
 chunks = base();
 chunks[2].entries.resize(1);
 chunks[2].entries[0].aux = {20000u};
 chunks[2].entries[0].declared = 8;
 Db truncated; Layout truncatedLayout = build(truncated, chunks);
 refuses(truncated, truncatedLayout.end + 4, "CN0", SweetRSCStageAux);
 refuses(truncated, truncatedLayout.end, "CN0", SweetRSCStageAux);

 /* ARC: four to thirty-two bytes, even. */
 unsigned bad[] = {2u, 34u, 5u};
 for (unsigned bytes : bad) {
  chunks = base();
  chunks[1].entries[0].declared = bytes;
  Db arc; Layout layout = build(arc, chunks);
  refuses(arc, layout.end, "mx", SweetRSCStageAux);
 }
 /* An auxiliary block that runs off the end of the DB. */
 chunks = base();
 chunks[1].entries[0].declared = 32u;
 Db off; Layout offLayout = build(off, chunks);
 refuses(off, offLayout.end, "mx", SweetRSCStageAux);
 /* Rail: its auxiliary window must be inside the block too. */
 chunks = base();
 chunks[0].entries[0].declared = 64u;
 Db rail; Layout railLayout = build(rail, chunks);
 refuses(rail, railLayout.end, "ldoa14", SweetRSCStageAux);
}

static void valuesStage()
{
 /* A bandwidth resource without a unit cannot be scaled, and an enable-only
   resource needs the caller's mask to explain a zero width. */
 struct Case { uint32_t unit, packed; unsigned mask; bool accepted; } cases[] = {
  {0u, (5u << 16) | 4u, 0u, false},        /* no unit */
  {20000u, 5u << 16, 0u, false},           /* no width */
  {20000u, 5u << 16, 8u, true},            /* width is the mask's business */
  {20000u, (16u << 16) | 4u, 0u, false},   /* vcd out of the range the encoder knows */
  {20000u, (5u << 16) | 4u | (1u << 24), 0u, false}, /* reserved bits set */
 };
 for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i) {
  std::vector<Chunk> chunks = base();
  chunks[2].entries[0].aux = {cases[i].unit, cases[i].packed};
  Db db; Layout layout = build(db, chunks);
  if (cases[i].accepted) {
   SweetRSCResource r = run(db, layout.end, "CN0", cases[i].mask);
   assert(r.found && r.width == 0u);
  } else {
   refuses(db, layout.end, "CN0", SweetRSCStageValues, cases[i].mask);
  }
 }

 /* ARC levels: zero head, strictly ascending, zero padding only at the end. */
 struct Levels { uint32_t word0, word1; } bad[] = {
  {0x00000001u, 0u},               /* head is not zero */
  {0x00020000u, 0x00000001u},      /* third level descends */
  {0x00010000u, 0x00000001u},      /* repeated level */
  {0x00000000u, 0u},               /* only the head level */
 };
 for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i) {
  std::vector<Chunk> chunks = base();
  chunks[1].entries[0].aux = {bad[i].word0, bad[i].word1};
  Db db; Layout layout = build(db, chunks);
  refuses(db, layout.end, "mx", SweetRSCStageValues);
 }
 /* Padding then a real level again is not a table. */
 {
  std::vector<Chunk> chunks = base();
  chunks[1].entries[0].aux = {0x00010000u, 0u, 0x00020000u};
  Db db; Layout layout = build(db, chunks);
  refuses(db, layout.end, "mx", SweetRSCStageValues);
 }
 /* A two level table is a table: the head zero is one of the two. */
 {
  std::vector<Chunk> chunks = base();
  chunks[1].entries[0].aux = {0x00010000u, 0, 0};
  Db db; Layout layout = build(db, chunks);
  SweetRSCResource r = run(db, layout.end, "mx", 0);
  assert(r.found && r.levelCount == 2u && r.levels[1] == 1u);
 }
}

static void matchStage()
{
 /* Two entries by the same name: the caller cannot pick one. */
 std::vector<Chunk> chunks = base();
 chunks[2].entries.push_back({"CN0", 0x500b0u, {20001u, (6u << 16) | 4u}});
 Db db; Layout layout = build(db, chunks);
 refuses(db, layout.end, "CN0", SweetRSCStageMatches);
 /* Two chunks that both answer, which the per-chunk gates cannot catch. */
 chunks = base();
 chunks[0].entries.push_back({"CN0", 0x42110u});
 Db split; Layout splitLayout = build(split, chunks);
 refuses(split, splitLayout.end, "CN0", SweetRSCStageMatches);
 /* No entry at all. */
 refuses(db, layout.end, "MM1", SweetRSCStageMatches);

 /* A zero slave ends the descriptor list: whatever follows is not read, so a
   broken descriptor after the terminator cannot refuse the block. */
 chunks = base();
 chunks.insert(chunks.begin() + 1, {0u, {}});
 chunks.push_back({6u, {{"CN0", 0x500a8u}}});
 Db terminated; Layout terminatedLayout = build(terminated, chunks);
 refuses(terminated, terminatedLayout.end, "CN0", SweetRSCStageMatches);
 refuses(terminated, terminatedLayout.end, "MM1", SweetRSCStageMatches);
 assert(terminated.words.count(CHUNK + STRIDE * 3));

 /* All eight descriptor slots used, none of them terminating: the walk stops
   at eight without refusing. */
 std::vector<Chunk> eight;
 for (unsigned i = 0; i < 8; ++i) eight.push_back({4u, {}});
 Db full; Layout fullLayout = build(full, eight);
 refuses(full, fullLayout.end, "CN0", SweetRSCStageMatches);
 unsigned loads = 0;
 SweetRSCResource none = {};
 assert(!SweetRSCDatabase::lookup(&full, reader, fullLayout.end, "CN0", 0u, &none, &loads, nullptr));
 assert(loads == 16u && !none.found);
}

static void contract()
{
 /* The optional arguments are optional, and a caller that cannot supply a
   reader or a name is refused instead of dereferenced. */
 Db db; Layout layout = build(db, base());
 SweetRSCResource r = {};
 SweetRSCStage stage = SweetRSCStageNone;
 assert(!SweetRSCDatabase::lookup(&db, nullptr, layout.end, "CN0", 0, &r, nullptr, &stage));
 assert(stage == SweetRSCStageName);
 assert(!SweetRSCDatabase::lookup(&db, reader, layout.end, nullptr, 0, &r, nullptr, &stage));
 assert(stage == SweetRSCStageName);
 assert(!SweetRSCDatabase::lookup(&db, reader, layout.end, "CN0", 0, nullptr, nullptr, &stage));
 assert(stage == SweetRSCStageName);
 /* A block with no bytes at all is refused as a name: there is nothing to
   resolve the name against. */
 assert(!SweetRSCDatabase::lookup(&db, reader, 0, "CN0", 0, &r, nullptr, &stage));
 assert(stage == SweetRSCStageName);
 /* A call that is refused before it reaches the block loads nothing. */
 unsigned loads = 99;
 assert(!SweetRSCDatabase::lookup(&db, nullptr, layout.end, "CN0", 0, &r, &loads, &stage));
 assert(loads == 0 && stage == SweetRSCStageName);
 assert(!strcmp(SweetRSCDatabase::stageName(SweetRSCStageNone), "none"));
 assert(!strcmp(SweetRSCDatabase::stageName((SweetRSCStage)99), "none"));
}

int main()
{
 accepted();
 nameStage();
 headerStage();
 entryStage();
 addressStage();
 auxStage();
 valuesStage();
 matchStage();
 contract();
 printf("test-mh2lm-rsc-database: PASS\n");
 return 0;
}
'''


with tempfile.TemporaryDirectory(prefix='mh2lm-db-') as temp:
    cpp = Path(temp) / 'test.cpp'
    cpp.write_text(prelude + header + source + fixture)
    if os.environ.get('MH2LM_KEEP_CPP'):
        Path('/tmp/mh2lm-db-kept.cpp').write_text(cpp.read_text())
    binary = Path(temp) / 'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '') + ':detect_leaks=0'
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '') + ':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env, timeout=60)
