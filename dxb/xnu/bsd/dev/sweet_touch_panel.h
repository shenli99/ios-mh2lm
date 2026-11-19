/* Typed, pointer-free mailbox to the existing sole TouchComm worker.
 * IOService owns one session; it never performs TLMM/GIC/I2C/HID operations.
 * No callback/IOService instance is stored by C. All public calls are bounded,
 * nonblocking and perform no MMIO. The caller serializes its session methods.
 */
#ifndef SWEET_TOUCH_PANEL_H
#define SWEET_TOUCH_PANEL_H
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
enum sweet_touch_panel_state {
 SWEET_TOUCH_OFF, SWEET_TOUCH_RUNNING, SWEET_TOUCH_PAUSE_REQUESTED,
 SWEET_TOUCH_PAUSING, SWEET_TOUCH_QUIESCED, SWEET_TOUCH_RESUME_REQUESTED,
 SWEET_TOUCH_RESUMING, SWEET_TOUCH_FAILED, SWEET_TOUCH_STOPPED
};
struct sweet_touch_panel_status { uint64_t token; unsigned state, available; };
/*
 * GIC top half registered by the IOService that matched the touch ADT node.
 * The service also installs ts_irq_mask_fn/ts_irq_unmask_fn (defined by the
 * worker) so enable/mask go through the interrupt controller; with them NULL
 * the worker keeps the legacy raw GICD path.
 */
void sweet_touch_irq_action(void *target, void *refCon, void *nub, int source);
/* Pad path: the facade installs mask/unmask/ack so the worker never writes a
 * pad register it does not own. NULL keeps the legacy raw distributor path. */
extern void (*ts_irq_mask_fn)(void);
extern void (*ts_irq_unmask_fn)(void);
extern void (*ts_irq_ack_fn)(void);
/* ATTN pad: the touch interrupt is a TLMM pad, not a direct-connect GIC line.
 * The pad number lives here so the worker and the IOService cannot drift. */
#define SWEET_TOUCH_IRQ_PIN 122u
/* TLMM pad-interrupt entry with the controller's handler shape. */
void sweet_touch_tlmm_irq(unsigned int pin, void *refcon);

int sweet_touch_panel_open(uint64_t *);
int sweet_touch_panel_close(uint64_t);
int sweet_touch_panel_request(uint64_t, unsigned resume, uint64_t *);
int sweet_touch_panel_snapshot(uint64_t, uint64_t, struct sweet_touch_panel_status *);
/* A successful QUIESCED snapshot proves worker/IRQ/HID quiescence, not rails or
 * physical touch low power. Resume requires caller's panel/rail-ready proof and
 * re-identifies/reconfigures TouchComm before rearming. Stop cancels; a running
 * worker finishes bus STOP/masking asynchronously and never calls a freed owner.
 * Any session that issued a request retires on close until reboot. */
#ifdef __cplusplus
}
#endif
#endif
