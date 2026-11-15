/* ADC5 fixed preflight/conversion protocol. Consumer requests a typed
 * operation, never a SID/register address. No charging control permission. */
#ifndef SWEET_ADC5_H
#define SWEET_ADC5_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum sweet_adc5_field {
 SWEET_ADC5_TYPE, SWEET_ADC5_SUBTYPE, SWEET_ADC5_STATUS, SWEET_ADC5_SEQUENCE,
 SWEET_ADC5_DIGITAL, SWEET_ADC5_AVERAGE, SWEET_ADC5_CHANNEL, SWEET_ADC5_SETTLE,
 SWEET_ADC5_ENABLE, SWEET_ADC5_REQUEST, SWEET_ADC5_DATA_LO, SWEET_ADC5_DATA_HI,
 SWEET_ADC5_CAL_DELAY, SWEET_ADC5_FIELD_COUNT
};
struct sweet_adc5_route {
 uint16_t observer_apid, writer_apid, irq_apid;
 int irq_error; /* unique EE0 APID with IRQ-owner bit, still not armed */
 int writer_error; /* uniqueness/EE0/poison only, not proof a write is accepted */
 uint8_t writer_ee, irq_owner;
};
struct sweet_adc5_preflight {
 uint8_t raw[SWEET_ADC5_FIELD_COUNT];
 int errors[SWEET_ADC5_FIELD_COUNT];
 uint32_t valid;
 struct sweet_adc5_route user, calibration;
};
/* Fixed SID2 user31/cal37, PMIC-arb v5. Read-only, no IRQ/MMIO mapping or
 * new request. Data bytes may be stale or for another channel: NEVER convert
 * them into VBUS/battery ID. No permission is added to the general read API.
 * Returns 0 when the fixed inspection ran; field errors/valid and writer_error
 * must still be inspected individually. On invalid/not-ready/unsupported the
 * output is unchanged. Caller timestamps outside the controller lock. */
int sweet_spmi_adc5_inspect(struct sweet_adc5_preflight *out);

/* Mux-only fixed SID2/0x31 IRQ observer offsets 11/12/13/15. */
int sweet_spmi_adc5_irq_read(unsigned offset, uint8_t *out);

/* Conversion protocol: fixed channels, caller-owned session and output units.
 * All calls are thread-context. begin/advance/abort serialize on the shared
 * controller lock; no waits held across EOC. Session memory lives until abort.
 * Errors preserve measurement output; begin may touch session on a HW failure
 * to report restore_error, but always cleans/releases its lease before return.
 * A BUSY/invalid/unready rejection leaves the session unchanged.
 * Successful begin is followed by SETTLED (USB only), EOC(s), or abort.
 * MORE means another EOC is required; only rc=0 from advance returns a sample.
 * Caller must use its own workloop, never concurrently mutate the session. */
#define SWEET_ADC5_MORE 1
#define SWEET_ADC5_EXPIRED (-101)
enum sweet_adc5_channel { SWEET_ADC5_BAT_ID = 1, SWEET_ADC5_USBIN = 2 };
enum sweet_adc5_phase { SWEET_ADC5_IDLE, SWEET_ADC5_WAIT_SETTLE, SWEET_ADC5_GROUND,
 SWEET_ADC5_RESULT, SWEET_ADC5_DONE, SWEET_ADC5_FAILED };
enum sweet_adc5_cause { SWEET_ADC5_SETTLED = 1, SWEET_ADC5_EOC = 2 };
/* Finite diagnostic observation only, no data bytes or PMIC writes.
 * Per-field errors remain explicit; this can NEVER complete a conversion. */
#define SWEET_ADC5_FAULT_FIELDS 10u
struct sweet_adc5_fault {
 uint8_t raw[SWEET_ADC5_FAULT_FIELDS];
 int errors[SWEET_ADC5_FAULT_FIELDS];
 /* locked_ns is first acquisition; public observers release between fields.
  * completed-locked is elapsed time, NOT continuous lock occupancy. */
 uint64_t entered_ns, locked_ns, completed_ns;
};
struct sweet_adc5_session {
 uint8_t saved[6];
 unsigned channel, phase, touched, active, eoc_count;
 int restore_error;
 uint64_t started_ns, deadline_ns, settle_until_ns;
 /* Diagnostic only; no timestamp below can renew a deadline or validate EOC. */
 uint64_t trigger_enter_ns, trigger_done_ns, advance_enter_ns, advance_locked_ns;
 unsigned diagnostic_phase;
 uint64_t diagnostic_deadline_ns;
 /* Error snapshot saved under the controller lock before protocol restore. */
 unsigned fault_saved; int fault_error;
 struct sweet_adc5_fault fault;
};
struct sweet_adc5_measurement {
 unsigned channel, microvolts, eoc_count;
 unsigned sequence; /* provider request token, assigned after restored completion */
 uint16_t raw;
 uint64_t started_ns, completed_ns;
};
int sweet_spmi_adc5_begin(struct sweet_adc5_session *, enum sweet_adc5_channel);
int sweet_spmi_adc5_advance(struct sweet_adc5_session *, enum sweet_adc5_cause,
 struct sweet_adc5_measurement *);
int sweet_spmi_adc5_abort(struct sweet_adc5_session *);
int sweet_spmi_adc5_capture_fault(struct sweet_adc5_fault *);
#ifdef __cplusplus
}
#endif
#endif
