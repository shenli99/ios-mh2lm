/* Internal hardware sampling interface used by SweetBattery on its workloop.
 * No address/mask parameter is exposed. This component neither owns a workloop
 * nor grants charging permission; future ADC/I2C providers supply separate
 * typed evidence. The caller sets started/completed time around this call. */
#ifndef SWEET_PMIC_POWER_H
#define SWEET_PMIC_POWER_H
#include "sweet_battery_runtime.h"
#if defined(ARM64_BOARD_CONFIG_MH2LM)
struct sweet_watchdog_fault;
/* Fixed semantic watchdog diagnostics; no caller-supplied addresses. */
int sweet_pmic_power_capture_watchdog_fault(struct sweet_watchdog_fault *);
/* Refresh input/APSD/Type-C only on initial observation or plugin/source/Type-C IRQ.
 * Capacity may remain cached on protection ticks; every fresh read reports its
 * own validity/error. The fixed profile-prefix/metadata tuple shares a grant.
 * Input acquisition has its own monotonic epoch and start/completion times;
 * protection ticks preserve them. Failure is unknown, never a valid zero. */
void sweet_pmic_power_read_snapshot(SweetBattSnapshot &snapshot,
    bool refreshInput, bool refreshCapacity, unsigned lastPercent);
#endif
#endif
