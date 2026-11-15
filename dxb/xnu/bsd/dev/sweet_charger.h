/* Semantic charger operations. Values and verified outputs are mA, mV or
 * boolean, never register addresses/masks/codes. Thread context only.
 * The battery workloop owns policy/control lifetime; this backend owns encoding.
 * No operation grants a charging lease or supplies missing board evidence. */
#ifndef SWEET_CHARGER_H
#define SWEET_CHARGER_H
#include <stdint.h>
#define SWEET_CHARGER_EXPIRED (-100)
#ifdef __cplusplus
extern "C" {
#endif
enum sweet_charger_operation {
 SWEET_CHARGER_ENABLE, SWEET_CHARGER_PAUSE, SWEET_CHARGER_INPUT_SUSPEND,
 SWEET_CHARGER_INPUT_LIMIT_MA, SWEET_CHARGER_SOFTWARE_INPUT_LIMIT,
 SWEET_CHARGER_FAST_CURRENT_MA, SWEET_CHARGER_FLOAT_VOLTAGE_MV,
 /* Diagnostic preparation only: require charge disabled, never authorize
  * external isolation or managed-watchdog policy from a command result. */
 SWEET_CHARGER_PARALLEL_INHIBIT, SWEET_CHARGER_WATCHDOG_PREPARE, SWEET_CHARGER_WATCHDOG_PET,
 /* Runtime3 board-bounded controller (sweet_charge_basic.h): FCC <=2800mA,
  * FV <=4430mV (LG supplement/BLT42 profile); pet allowed while charging. */
 SWEET_CHARGER_BOARD_FAST_CURRENT_MA, SWEET_CHARGER_BOARD_FLOAT_VOLTAGE_MV,
 SWEET_CHARGER_CHARGING_PET
};
/* Fixed MISC watchdog fault observation, no caller-supplied register address.
 * The transport owns the layout; field errors must be inspected individually. */
#define SWEET_WDOG_FAULT_FIELDS 7u
struct sweet_watchdog_fault {
 uint8_t raw[SWEET_WDOG_FAULT_FIELDS];
 int errors[SWEET_WDOG_FAULT_FIELDS];
 /* locked_ns is first acquisition; public observers release between fields.
  * completed-locked is elapsed time, NOT continuous lock occupancy. */
 uint64_t entered_ns, locked_ns, completed_ns;
};
int sweet_spmi_watchdog_capture_fault(struct sweet_watchdog_fault *);
/* Fixed PM8150B backend; uses the restricted shared SPMI transport. Exact
 * quantization required, no rounding up. First-stage caps: 500mA / 4200mV.
 * SOFTWARE_INPUT_LIMIT accepts only 1 and configures its three verified modes.
 * Failure may have changed hardware; caller must stop/fault, not restore an
 * inherited enable. *verified is unchanged on any failure. No hidden retries.
 * deadline_ns uses XNU uptime in ns and is checked before EVERY physical update.
 * ENABLE=0 alone is always allowed without a deadline, including power stop. */
int sweet_charger_execute(enum sweet_charger_operation operation,
 unsigned value, uint64_t deadline_ns, unsigned *verified);
#ifdef __cplusplus
}
#endif
#endif
