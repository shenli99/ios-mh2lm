#ifndef MH2LM_PANEL_HANDOFF_H
#define MH2LM_PANEL_HANDOFF_H
#include "panel_identity.h"
/* Input extents are mandatory. No allocation, libc, unaligned word accesses,
 * pointers into FDT in the result, or propagation of arbitrary bootargs. */
void mh2lm_panel_from_fdt(const void *,size_t,uint8_t [MH2LM_PANEL_IDENTITY_SIZE]);
/* 1=single empty slot, 0=no slot, -1=invalid tree/slot. No changes. */
int mh2lm_panel_adt_has_slot(const void *,size_t);
/* Replace exactly one zero-filled /arm-io/mdss slot, only after validating the
 * WHOLE ADT. 1=filled, 0=no slot, -1=invalid (caller MUST refuse boot).
 * No partial changes, new nodes, resizing, or unique-device data. */
int mh2lm_panel_fill_adt(void *,size_t,const uint8_t [MH2LM_PANEL_IDENTITY_SIZE]);
#endif
