#!/usr/bin/env python3
"""C3-05/C3-06: the bounded apps-submission queue as a unit.

The queue is the only new storage the apps RSC got in C3-05, so it is tested on
its own, without IOKit and without the driver: capacity, arrival order across the
array wrap, the refusal when full, the exact-resource lookup the owner uses to
reject a duplicate, and the payload round trip that lets a request survive after
the caller's stack and objects are gone.

C3-06 adds the one way a record leaves without being dispatched. A withdrawal is
tested here at the storage level: by token anywhere in the window (head, middle,
tail), by client (newest first, and the client's own records only), the refusal
for a token that is not waiting, and the counters - a cancelled record is not a
dispatched one, and the slot it leaves is cleared rather than left readable.

Run: python3 tools/tests/test-mh2lm-apps-queue.py [xnu-tree]
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else "dxb/xnu").resolve()
arm = root / "iokit/Kernel/arm"
queue_h = (arm / "SweetAppsQueue.h").read_text()
queue_cpp = (arm / "SweetAppsQueue.cpp").read_text()

# The unit must not depend on IOKit beyond the integer types, and must compile to
# nothing on a board that does not build this driver.
assert '#include <IOKit/IOTypes.h>' in queue_h, "queue header lost its type include"
assert queue_h.count('#if defined(ARM64_BOARD_CONFIG_MH2LM)') == 1
assert queue_cpp.count('#if defined(ARM64_BOARD_CONFIG_MH2LM)') == 1
assert 'kSweetAppsQueueCapacity = 8' in queue_h

TEST = r'''
static unsigned failures;
#define check(x) do{ if(!(x)){ fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #x); failures++; } }while(0)

static SweetAppsRequest make(unsigned kind, unsigned client, unsigned rail,
                             uint64_t p0, uint64_t p1, uint64_t token)
{
	SweetAppsRequest r = SweetAppsRequest();
	r.kind = kind; r.client = client; r.rail = rail;
	r.param[0] = p0; r.param[1] = p1; r.token = token;
	return r;
}

static void fresh(void)
{
	SweetAppsQueue q;
	check(q.empty() && !q.full() && q.depth() == 0);
	check(q.accepted() == 0 && q.dispatched() == 0 && q.highWater() == 0);
	check(q.head() == NULL);
	check(!q.waiting(0));
	check(q.find(1, 0, 0) == NULL);
	q.pop();			/* popping nothing is a no-op, not an underflow */
	check(q.empty() && q.dispatched() == 0);
}

/* Capacity is exactly the published bound, the ninth request is refused and the
 * eight already accepted are untouched by the refusal. */
static void capacity(void)
{
	SweetAppsQueue q;
	for (unsigned i = 0; i < kSweetAppsQueueCapacity; ++i) {
		SweetAppsRequest stored;
		check(q.push(make(1, i, 0, i, ~uint64_t(i), 0x100 + i), &stored));
		check(stored.seq == i + 1 && stored.client == i && stored.param[1] == ~uint64_t(i));
		check(q.depth() == i + 1 && q.highWater() == i + 1);
	}
	check(q.full() && q.depth() == kSweetAppsQueueCapacity && q.accepted() == kSweetAppsQueueCapacity);
	SweetAppsRequest more = make(1, 99, 0, 99, 99, 0x999);
	check(!q.push(more) && q.depth() == kSweetAppsQueueCapacity && q.accepted() == kSweetAppsQueueCapacity);
	check(more.seq == 0);		/* a refused request is not stamped */
	check(q.head() && q.head()->client == 0 && q.head()->seq == 1);
	for (unsigned i = 0; i < kSweetAppsQueueCapacity; ++i) {
		check(q.head() && q.head()->client == i && q.head()->seq == i + 1);
		q.pop();
	}
	check(q.empty() && q.dispatched() == kSweetAppsQueueCapacity && q.accepted() == kSweetAppsQueueCapacity);
	check(q.highWater() == kSweetAppsQueueCapacity);
}

/* Arrival order survives the array wrap: the sequence stamped at push() is the
 * order the owner dispatches in, never the slot order. */
static void wrap(void)
{
	SweetAppsQueue q;
	for (unsigned i = 0; i < 5; ++i) check(q.push(make(2, i, 0, i, 0, 0)));
	for (unsigned i = 0; i < 3; ++i) q.pop();
	check(q.depth() == 2 && q.dispatched() == 3);
	for (unsigned i = 5; i < 11; ++i) check(q.push(make(2, i, 0, i, 0, 0)));
	check(q.depth() == 8 && q.full() && q.highWater() == 8);
	for (unsigned i = 3; i < 11; ++i) {
		check(q.head() && q.head()->client == i);
		check(q.head()->seq == i + 1);	/* seq keeps counting across the wrap */
		check(q.head()->param[0] == i);
		check(q.find(2, i, 0) == q.head());
		q.pop();
	}
	check(q.empty() && q.accepted() == 11 && q.dispatched() == 11);
	check(q.find(2, 3, 0) == NULL && !q.waiting(3));
}

/* The duplicate rule rests on this lookup being exact: same kind, same client
 * and same resource, and nothing else. */
static void lookup(void)
{
	SweetAppsQueue q;
	check(q.push(make(3, 1, 4, 0, 0, 0xaa)));
	check(q.push(make(3, 2, 4, 0, 0, 0xbb)));
	check(q.push(make(4, 1, 4, 0, 0, 0xcc)));
	check(q.find(3, 1, 4) && q.find(3, 1, 4)->token == 0xaa);
	check(q.find(3, 2, 4) && q.find(3, 2, 4)->token == 0xbb);
	check(q.find(4, 1, 4) && q.find(4, 1, 4)->token == 0xcc);
	check(q.find(3, 1, 5) == NULL);			/* different resource */
	check(q.find(3, 3, 4) == NULL);			/* different client */
	check(q.find(5, 1, 4) == NULL);			/* different kind */
	check(q.waiting(1) && q.waiting(2) && !q.waiting(3) && !q.waiting(99));
	q.pop();
	check(q.find(3, 1, 4) == NULL && q.waiting(1) && !q.waiting(3));
	q.pop();
	check(q.find(4, 1, 4) && q.waiting(1));
	q.pop();
	check(q.find(4, 1, 4) == NULL && !q.waiting(1) && q.empty());
}

/* Everything the dispatcher needs travels inside the record: the token was
 * issued before the wait, and no pointer to the caller's stack is kept. */
static void payload(void)
{
	SweetAppsQueue q;
	SweetAppsRequest in = make(0xdead, 7, 0xbeef, 0x1122334455667788ULL, ~0ULL, 0xfe);
	SweetAppsRequest stored;
	check(q.push(in, &stored));
	const SweetAppsRequest *head = q.head();
	check(head != NULL);
	check(head->kind == in.kind && head->client == in.client && head->rail == in.rail);
	check(head->param[0] == in.param[0] && head->param[1] == in.param[1]);
	check(head->token == in.token);
	check(head->seq == stored.seq && stored.seq == 1);
	in.kind = 0;			/* the caller may reuse its struct after admission */
	check(q.head()->kind == 0xdead);
}

/* C3-06: a token identifies one waiting record and nothing else. Withdrawing it
 * closes the gap in arrival order without disturbing the requests behind it, and
 * the record that left is handed back so the owner can fail exactly that token. */
static void cancelByToken(void)
{
	SweetAppsQueue q;
	SweetAppsRequest stored;
	check(q.push(make(1, 4, 2, 0x11, 0, 0xa1), &stored));
	check(q.push(make(1, 5, 2, 0x22, 0, 0xa2), &stored));
	check(q.push(make(1, 6, 2, 0x33, 0, 0xa3), &stored));
	check(q.findToken(0xa1) && q.findToken(0xa1)->client == 4);
	check(q.findToken(0xa3) && q.findToken(0xa3)->client == 6);
	check(q.findToken(0) == NULL && q.findToken(0xff) == NULL);

	/* the head */
	SweetAppsRequest gone = make(0, 0, 0, 0, 0, 0);
	check(q.cancel(0xa1, &gone) && gone.token == 0xa1 && gone.client == 4);
	check(gone.seq == 1);			/* the withdrawn record keeps its stamp */
	check(q.depth() == 2 && q.cancelled() == 1 && q.dispatched() == 0);
	check(q.head() && q.head()->token == 0xa2 && q.head()->seq == 2);
	check(q.findToken(0xa1) == NULL && q.find(1, 4, 2) == NULL);

	/* the tail: the two ahead of it keep their order */
	check(q.cancel(0xa3) && q.depth() == 1 && q.cancelled() == 2);
	check(q.cancel(0xa3) == false && q.depth() == 1 && q.cancelled() == 2);
	check(q.head() && q.head()->token == 0xa2);
	q.pop();
	check(q.empty() && q.dispatched() == 1 && q.cancelled() == 2);

	/* nothing to withdraw must change nothing: an unknown token, a zero token
	 * (never issued), and a token that has already been dispatched. */
	check(q.push(make(1, 7, 2, 0x44, 0, 0xa4), &stored));
	check(!q.cancel(0xff) && !q.cancel(0) && !q.cancel(0xa2) && q.depth() == 1);
	check(q.cancelled() == 2 && q.accepted() == 4);
	q.pop();

	/* A cancelled record is not a dispatched one, and its slot is cleared: a
	 * stale copy in the window must not be readable as a live request. */
	check(q.dispatched() == 2 && q.cancelled() == 2);
	check(q.findToken(0xa1) == NULL && !q.waiting(4) && !q.waiting(6));
}

/* A withdrawing client takes its own records and no one else's, newest first, so
 * the copy the owner publishes is the order a reader would expect. */
static void cancelByClient(void)
{
	SweetAppsQueue q;
	SweetAppsRequest stored;
	SweetAppsRequest out[3];
	const unsigned tokens[6] = { 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6 };
	const unsigned owners[6] = { 3, 8, 3, 9, 3, 8 };
	for (unsigned i = 0; i < 6; ++i)
		check(q.push(make(1, owners[i], i, i, 0, tokens[i]), &stored));
	check(q.depth() == 6 && q.cancelled() == 0);

	const unsigned taken = q.cancelClient(3, out, 3);
	check(taken == 3 && q.depth() == 3 && q.cancelled() == 3 && q.dispatched() == 0);
	check(out[0].token == 0xb5 && out[1].token == 0xb3 && out[2].token == 0xb1);
	check(!q.waiting(3) && q.waiting(8) && q.waiting(9));
	check(q.head() && q.head()->token == 0xb2);	/* others keep their order */
	check(q.head()->seq == 2);
	check(q.findToken(0xb3) == NULL && q.findToken(0xb4) != NULL);

	/* a client with nothing waiting is not an error, and costs nothing */
	check(q.cancelClient(3, out, 3) == 0 && q.depth() == 3 && q.cancelled() == 3);
	check(q.cancelClient(99) == 0 && q.depth() == 3);

	/* a caller with a smaller buffer still gets every record out: the ones past
	 * the cap are withdrawn without being copied anywhere. */
	check(q.cancelClient(8, out, 1) == 2 && q.depth() == 1 && q.cancelled() == 5);
	check(out[0].token == 0xb6);
	q.pop();
	check(q.empty() && q.dispatched() == 1 && q.cancelled() == 5);
	check(q.cancelled() + q.dispatched() == q.accepted());
}

/* reset() is the boot-time clear the owner runs from start(): storage and
 * counters go back to zero, so a stale request can never be dispatched. */
static void reset(void)
{
	SweetAppsQueue q;
	check(q.push(make(9, 0, 0, 0, 0, 1)) && q.push(make(9, 1, 0, 0, 0, 2)));
	q.pop();
	check(q.depth() == 1 && q.accepted() == 2 && q.dispatched() == 1);
	q.reset();
	check(q.empty() && !q.full() && q.depth() == 0 && q.head() == NULL);
	check(q.accepted() == 0 && q.dispatched() == 0 && q.highWater() == 0);
	check(q.cancelled() == 0);
	check(q.find(9, 1, 0) == NULL && !q.waiting(1) && q.findToken(2) == NULL);
	SweetAppsRequest stored;
	check(q.push(make(9, 2, 0, 0, 0, 3), &stored) && stored.seq == 1);
}

int main(void)
{
	fresh(); capacity(); wrap(); lookup(); payload(); reset();
	cancelByToken(); cancelByClient();
	if (failures) { fprintf(stderr, "%u checks failed\n", failures); return 1; }
	puts("PASS: C3-05/C3-06 apps submission queue; fixed capacity 8, refused when full without stamping, arrival order across the array wrap, exact kind/client/resource lookup, payload and pre-issued token preserved after the caller reuses its struct, withdrawal by token (head/tail/unknown/zero/already dispatched) and by client (own records only, newest first, every record out even past the caller's buffer) counted apart from dispatches, boot-time reset");
	return 0;
}
'''

PRELUDE = '''
#include <stdint.h>
#include <stddef.h>
typedef int IOReturn;
'''

with tempfile.TemporaryDirectory(prefix='mh2lm-apps-queue-') as temp:
    temp = Path(temp)
    inc = temp / 'fake'
    (inc / 'IOKit').mkdir(parents=True)
    (inc / 'IOKit/IOTypes.h').write_text(PRELUDE)
    # The shipped source is compiled as it is, through its own include, so a unit
    # that stopped being self-contained could not hide behind a rewritten copy.
    tu = temp / 'test.cpp'
    tu.write_text('#include "SweetAppsQueue.cpp"\n'
                  '#include <stdio.h>\n'
                  '#include <stdint.h>\n' + TEST)
    binary = temp / 'test'
    flags = ['-std=c++17', '-g', '-pthread', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer', '-I', str(temp), '-I', str(inc), '-I', str(arm),
             '-DARM64_BOARD_CONFIG_MH2LM=1']
    subprocess.run([os.environ.get('CXX', 'clang++-19'), *flags, str(tu), '-o', str(binary)],
                   check=True, timeout=120)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    subprocess.run([str(binary)], check=True, env=env, timeout=120)

    # Without the board macro the unit must compile to an empty object: no other
    # board inherits this storage.
    empty = temp / 'empty.cpp'
    empty.write_text('#include "SweetAppsQueue.h"\nvoid nothing(void) {}\n')
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-c',
                    str(empty), '-o', str(temp / 'empty.o'), '-I', str(temp), '-I', str(inc), '-I', str(arm)],
                   check=True, timeout=120)
    nm = subprocess.run(['llvm-nm-19', str(temp / 'empty.o')], capture_output=True, text=True)
    if nm.returncode != 0:
        nm = subprocess.run(['llvm-nm', str(temp / 'empty.o')], capture_output=True, text=True)
    assert nm.returncode == 0, nm.stderr
    assert 'SweetAppsQueue' not in nm.stdout, nm.stdout