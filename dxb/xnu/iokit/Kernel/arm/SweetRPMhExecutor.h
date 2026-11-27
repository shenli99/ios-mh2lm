/* MH2LM ACTIVE transaction mechanics.
 *
 * The service stays the only hardware owner: it picks the TCS, admits the
 * request, admits again before every queued write, keeps the GIC route, owns
 * the deadlines and decides when to give up. What lives here is the mechanical
 * half of one ACTIVE transaction: the posted-write readback that decides
 * whether the next queued word may be issued, the verdict that says a
 * completion is real, and the tail that quiesces a completed TCS. No function
 * here chooses a resource, an address or a value, maps a base address, or
 * remembers state between calls: the caller owns the word storage and passes
 * its own ports, so a reviewed call site reads exactly like the register
 * sequence it performs. */
#ifndef SWEET_RPMH_EXECUTOR_H
#define SWEET_RPMH_EXECUTOR_H
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOTypes.h>

/* One queued TCS word. w1c words are acknowledged by writing, then observed
 * through the source status word instead of their own address. */
struct SweetRPMhWord { uint32_t offset, value; bool w1c; };

/* TCS register geometry. Passed in rather than duplicated here, so the layout
 * stays declared once, next to the offsets it describes. */
struct SweetRPMhLayout {
 uint32_t status;         /* per-TCS busy/status word */
 uint32_t irqStatus;      /* source status, also the w1c readback */
 uint32_t irqEnable;
 uint32_t clear;          /* write-one-to-clear acknowledge */
 /* Slot-relative: the caller passes the selected slot base to the calls
  * that touch it, because one TCS block never owns another. */
 uint32_t control, wait, enable, commandStatus, commandStride;
 uint32_t ackMask;        /* bits a command must report when it finished */
};

class SweetRPMhExecutor {
public:
 /* Room for the largest staged shape: the inherited enable/clear pair, three
  * commands with wait/enable/control, the trigger and the cleanup tail. */
 static constexpr unsigned kCapacity = 20;
 /* Both directions belong to the MMIO owner; a port never caches a base. */
 struct Ports {
  void *context;
  uint32_t (*read)(void *, uint32_t offset);
  void (*write)(void *, uint32_t offset, uint32_t value);
 };
 enum Step {
  StepAdvanced, /* the queued word was accepted: move on to the next one */
  StepBlocked,  /* the posted write is not visible yet: resume on the timer */
  StepExpired   /* still not visible past the write deadline */
 };
 enum Arm { ArmReady, ArmStale, ArmInvalid };
 enum Verdict {
  VerdictComplete,   /* idle, source bit set, every command acknowledged */
  VerdictIncomplete, /* the bus is idle-looking but a claim is missing */
  VerdictInvalid     /* a required status word folded to ~0 */
 };
 /* A transaction may only trigger on a quiet source: no status, no inherited
  * bit. A source that answers ~0 is not a quiet one. */
 static Arm arm(Ports &, const SweetRPMhLayout &, uint32_t bit, uint32_t *observed);
 /* Issue one queued word and read it back. The queue moves on only when the
  * readback matches; otherwise the same word stays at the head. */
 static Step step(Ports &, const SweetRPMhLayout &, const SweetRPMhWord &,
     bool *issued, uint64_t now, uint64_t deadline, uint32_t *observed);
 /* A completion claim is real only when the source bit survived, the TCS is
  * idle and every command reported both issued and complete. */
 static Verdict verify(Ports &, const SweetRPMhLayout &, uint32_t slot, uint32_t bit,
     unsigned commands, unsigned *unacknowledged);
 /* Words that quiesce a completed TCS: stop the engine, drop this slot's
  * enable/wait words, acknowledge the source, then restore the saved enable
  * bits for this slot only. False when the enable word cannot be read. */
 static bool stageCleanup(Ports &, const SweetRPMhLayout &, uint32_t slot,
     SweetRPMhWord *words,
     unsigned *count, uint32_t amc, uint32_t bit, uint32_t savedEnable);
};
#endif
#endif