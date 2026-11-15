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
