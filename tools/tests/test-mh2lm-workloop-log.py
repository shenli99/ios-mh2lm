#!/usr/bin/env python3
"""Drive the shipped IOWorkLoop console line and prove it prints once per owner.

The 0134 ring held 9042 rows carrying "SWEET IOWORKLOOP" out of 12863 - 9030 of
them this very line, the other 12 the allocator line below it - so 70% of the
ring: threadMain() is re-entered through its own continuation on every wakeup,
so an unguarded printf there costs a row per event-source round and a 4 MiB ring
lasts minutes. Two of those rows were even recorded spliced onto the end of
unrelated messages, so the cost was paid in other people's lines too.

The loop's own bits (loopRestart, a bool, already carries kLoopRestart and
kLoopTerminate through the byte-wide ISSETP/SETP/CLRP helpers) gain a third
diagnostic bit, so this test compiles the real guard - the three helpers, the
enum, the threadMain() prologue and the exitThread cleanup, all lifted verbatim
from the shipped file - and drives it the way the device does: twelve loops, a
round-robin of wakeups, and the one hot pair that dominated the ring.

Each shaping case is paired with a mutation of the shipped text that must be
rejected, so a harness that silently stopped looking cannot pass.  The global
word mutation is the first attempt at this fix: one remembered thread instead of
one bit per loop, which reprints on every alternation between two hot loops and
is exactly the noise this card removes.
Usage: test-mh2lm-workloop-log.py EXTRACTED_XNU
"""
from pathlib import Path
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
tree = Path(sys.argv[1]).resolve()
shipped = (tree/'iokit/Kernel/IOWorkLoop.cpp').read_text()

# ---- structural checks on the shipped text --------------------------------
assert shipped.count('SWEET IOWORKLOOP: thread') == 1, 'one console line, not two'
assert shipped.count('SWEET IOWORKLOOP: workToDo lock') == 1, \
    'the allocator line is already once per loop (guarded by workToDoLock == NULL)'
assert 'kLoopThreadNamed = 0x4' in shipped, 'the diagnostic bit keeps its own value'
assert 'kLoopRestart = 0x1' in shipped and 'kLoopTerminate = 0x2' in shipped, \
    'the two state bits keep theirs; this bit must not take one'
# The bit lives in loopRestart's byte: ISSETP/SETP/CLRP are byte-wide and the
# macro is what threadMain() uses for the two state bits.
assert '#define fFlags loopRestart' in shipped
assert 'bool loopRestart;' in (tree/'iokit/IOKit/IOWorkLoop.h').read_text(), \
    'kLoopThreadNamed rides the byte that holds the two state bits'

print_block = """#ifdef SWEET
	/* The recursive-lock panic names a thread; printing the loop's own
	 * thread here and the touch worker's in ts_thread() lets the panic line
	 * be matched against a known owner instead of an anonymous pointer.
	 * Once per owning thread, not once per wakeup - see kLoopThreadNamed. */
	if (!ISSETP(&fFlags, kLoopThreadNamed)) {
		SETP(&fFlags, kLoopThreadNamed);
		printf("SWEET IOWORKLOOP: thread %p running loop %p\\n",
		    (void *)IOThreadSelf(), this);
	}
#endif"""
assert print_block in shipped, 'the line is guarded by the bit and sets it first'

prologue = shipped[shipped.index('IOWorkLoop::threadMain()'):]
head, rest = prologue.split('restartThread:', 1)
assert 'SWEET IOWORKLOOP' in head and 'do {' not in head, \
    'the line prints on entry, not inside the event-source loop'
assert 'SWEET IOWORKLOOP' not in rest, 'nothing reprints it in the loop body'

exit_frag = rest[rest.index('exitThread:'):]
exit_frag = exit_frag[:exit_frag.index('closeGate();')]
assert 'CLRP(&fFlags, kLoopThreadNamed);' in exit_frag, \
    'a loop that loses its thread lets the replacement announce itself'

helpers = shipped[shipped.index('static inline void\nSETP'):shipped.index('#define fFlags')]
enum = shipped[shipped.index('enum IOWorkLoopState {'):]
enum = enum[:enum.index('};') + 2]

# ---- the harness -----------------------------------------------------------
# Only libc-shaped primitives are stubbed: IOThreadSelf() names the running
# thread, printf records (thread, loop) once per call, and fFlags/loopRestart
# are the object's own fields.  Everything else is the shipped text.
harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <utility>

%(helpers)s
#define fFlags loopRestart
%(enum)s

typedef void *thread_t;
static thread_t gSelf;
static thread_t IOThreadSelf(void) { return gSelf; }

static int gPrints;
static thread_t gPrintedThread[32];
static void *gPrintedLoop[32];

static void sweet_test_printf(const char *fmt, ...)
{
	va_list ap; va_start(ap, fmt);
	thread_t t = va_arg(ap, thread_t);
	void *l = va_arg(ap, void *);
	va_end(ap);
	(void)fmt;
	if (gPrints < 32) { gPrintedThread[gPrints] = t; gPrintedLoop[gPrints] = l; }
	gPrints++;
}
#define printf sweet_test_printf

struct Loop {
	bool loopRestart = false;
	void threadMain();
	void leave();
};

void Loop::threadMain()
{
%(body)s}

void Loop::leave()
{
%(leave)s}

%(main)s
'''

harness_tail = r'''
int main(void)
{
	int fails = 0;
	static Loop loops[12];
	static thread_t threads[12];
	for (int i = 0; i < 12; i++) threads[i] = (thread_t)(uintptr_t)(0x1000 + i * 0x40);

	/* Case 1: the device's shape - twelve loops, round-robin wakeups. */
	gPrints = 0;
	for (int round = 0; round < 500; round++) {
		for (int i = 0; i < 12; i++) {
			gSelf = threads[i];
			loops[i].threadMain();
		}
	}
	if (gPrints != 12) {
		fprintf(stderr, "case1: %d prints for 12 loops x 500 wakeups\n", gPrints);
		fails++;
	}
	for (int i = 0; i < 12 && i < gPrints; i++) {
		if (gPrintedLoop[i] != &loops[i] || gPrintedThread[i] != threads[i]) {
			fprintf(stderr, "case1: print %d names the wrong owner\n", i);
			fails++;
		}
	}

	/* Case 2: the pair that dominated the 0134 ring - two hot loops
	 * alternating 4000 times each must stay at two rows, not 8000. */
	gPrints = 0;
	for (int round = 0; round < 4000; round++) {
		gSelf = threads[0]; loops[0].threadMain();
		gSelf = threads[1]; loops[1].threadMain();
	}
	if (gPrints != 0) {
		fprintf(stderr, "case2: %d prints on two already-named loops\n", gPrints);
		fails++;
	}

	/* Case 3: the loop thread exits and a replacement takes over. */
	gSelf = threads[2];
	loops[2].leave();
	thread_t next = (thread_t)(uintptr_t)0x9000;
	gSelf = next;
	loops[2].threadMain();
	if (gPrints != 1) {
		fprintf(stderr, "case3: replacement thread printed %d times\n", gPrints);
		fails++;
	}
	gSelf = next;
	loops[2].threadMain();
	if (gPrints != 1) {
		fprintf(stderr, "case3: replacement reprinted on its own wakeup\n");
		fails++;
	}

	/* Case 4: the bit is per loop object, not one remembered thread: a
	 * loop that never printed still prints after another loop did. */
	struct Loop fresh;
	gPrints = 0;
	gSelf = threads[3];
	fresh.threadMain();
	if (gPrints != 1) {
		fprintf(stderr, "case4: an unnamed loop stayed silent\n");
		fails++;
	}
	return fails ? 1 : 0;
}
'''

def build(_unused=None, mutate=None):
    """Compile the shipped fragments, optionally through a mutation."""
    helpers_text = helpers
    enum_text = enum
    body = head[head.index('{') + 1:]
    leave = exit_frag[len('exitThread:'):]
    if mutate is not None:
        enum_text, body, leave = mutate(enum_text, body, leave)
    source = harness % {'helpers': helpers_text, 'enum': enum_text,
                        'body': body, 'leave': leave, 'main': harness_tail}
    return source


def compile_and_run(name, text):
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp)/'case.cpp'
        exe = Path(tmp)/'case'
        src.write_text(text)
        # -DSWEET: the console line and its bit are inside the tree's SWEET
        # guard, so a harness without it would drive an empty prologue.
        proc = subprocess.run(['g++', '-std=c++17', '-O1', '-w', '-DSWEET',
                               '-o', str(exe), str(src)],
                              capture_output=True, text=True)
        if proc.returncode != 0:
            raise AssertionError(f'{name}: fragment does not compile\n{proc.stderr}')
        return subprocess.run([str(exe)], capture_output=True, text=True)


def expect_pass(name, mutate=None):
    run = compile_and_run(name, build(None, mutate))
    assert run.returncode == 0, f'{name}: {run.stderr.strip()}'


def expect_fail(name, mutate, marker, why):
    run = compile_and_run(name, build(None, mutate))
    assert run.returncode == 1, f'{name}: mutation was not caught ({why})'
    assert marker in run.stderr, f'{name}: caught for the wrong reason ({why}): {run.stderr.strip()}'


expect_pass('shipped')

# ---- mutations that must be rejected --------------------------------------

def unguarded(enum_text, body, leave):
    """The 0134 code: no bit, a row per wakeup."""
    old = """	if (!ISSETP(&fFlags, kLoopThreadNamed)) {
		SETP(&fFlags, kLoopThreadNamed);
		printf("SWEET IOWORKLOOP: thread %p running loop %p\\n",
		    (void *)IOThreadSelf(), this);
	}"""
    new = """	printf("SWEET IOWORKLOOP: thread %p running loop %p\\n",
	    (void *)IOThreadSelf(), this);"""
    assert old in body
    return enum_text, body.replace(old, new), leave


def wrong_bit(enum_text, body, leave):
    """Guard on a state bit instead of the diagnostic bit."""
    return enum_text, body.replace('kLoopThreadNamed)) {', 'kLoopRestart)) {'), leave


def no_cleanup(enum_text, body, leave):
    """Keep the bit when the thread goes away."""
    return enum_text, body, leave.replace('	CLRP(&fFlags, kLoopThreadNamed);\n', '')


expect_fail('unguarded', unguarded, 'case1', 'prints once per wakeup')
expect_fail('wrong-bit', wrong_bit, 'case1', 'guards on a state bit that is cleared')
expect_fail('no-cleanup', no_cleanup, 'case3', 'a replacement thread stays silent')

# The global-word mutation needs its own declaration to compile, so it is
# checked against the harness header rather than through build().
global_source = (build(None).replace('struct Loop {', 'static uintptr_t gRemembered;\n\nstruct Loop {', 1))
global_source = global_source.replace(
    """	if (!ISSETP(&fFlags, kLoopThreadNamed)) {
		SETP(&fFlags, kLoopThreadNamed);
		printf("SWEET IOWORKLOOP: thread %p running loop %p\\n",
		    (void *)IOThreadSelf(), this);
	}""",
    """	{
		thread_t self = IOThreadSelf();
		if ((uintptr_t)self != gRemembered) {
			gRemembered = (uintptr_t)self;
			printf("SWEET IOWORKLOOP: thread %p running loop %p\\n",
			    (void *)self, this);
		}
	}""")
run = compile_and_run('global-word', global_source)
assert run.returncode != 0, 'global-word: one remembered thread was not caught'
assert 'case2' in run.stderr, f'global-word: caught for the wrong reason: {run.stderr}'

print('workloop log: shipped fragment prints once per owner, 4 mutations rejected')