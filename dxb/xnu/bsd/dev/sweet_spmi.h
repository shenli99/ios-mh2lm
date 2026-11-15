/* Shared C/C++ interface. Init is safe across late board/battery threads. */
#ifndef SWEET_BATTERY_SPMI_H
#define SWEET_BATTERY_SPMI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int sweet_spmi_init(void);
/* Thread context only; 0 success, -1 failure; *out unchanged on failure.
 * sid is the SPMI USID (PM8150B, which carries both charger and gauge, is 2
 * on guacamole) and addr selects the peripheral: only the DT-declared PM8150B
 * blocks are routed, and every call is a single read. */
int sweet_spmi_read(unsigned sid, unsigned addr, uint8_t *out);
/* Controller-wide observer/RW transaction serialization for keys and battery.
 * Init must have succeeded; acquire/release in thread context, never in IRQ.
 * Cover command/data programming through completion and result extraction.
 * Never call sweet_spmi_read while holding this lock; it locks internally.
 * This does not authorize arbitrary PMIC writes. */
void sweet_spmi_irq_lock(void);
void sweet_spmi_irq_unlock(void);
/* Mux-only, thread-context IRQ write/ACK acquisition. Announces urgency before
 * waiting; normal users yield between transactions. Same unlock and ownership
 * rules as above. Never hold this lock across a callback or an event wait. */
void sweet_spmi_irq_ack_lock(void);
/* RW timeout state is shared with the IRQ mux. Caller MUST hold the above
 * lock. Invalid APIDs fail closed. There is no reset/retry during this boot. */
int sweet_spmi_rw_poisoned_locked(unsigned apid);
void sweet_spmi_rw_poison_locked(unsigned apid);

#if defined(ARM64_BOARD_CONFIG_MH2LM)
/* Restricted charger transport, NOT a charging policy or a control lease.
 * Thread context only, after init; functions acquire the shared lock internally.
 * Fixed USID2/register table, single-byte accesses, unique EE0 write owner and
 * v5 required. IRQ ownership is separate and is NOT required for these APIs.
 * All outputs are unchanged on failure. No automatic retry or rollback.
 * A failed update may already have changed hardware; caller must enter its
 * control fault path, never infer that the old value has been restored.
 * Do not call from mux callbacks while its gate/transport lock is held. */
enum sweet_spmi_result {
	SWEET_SPMI_OK = 0,
	SWEET_SPMI_INVALID = -1,
	SWEET_SPMI_NOT_READY = -2,
	SWEET_SPMI_UNSUPPORTED = -3,
	SWEET_SPMI_NO_OWNER = -4,
	SWEET_SPMI_AMBIGUOUS = -5,
	SWEET_SPMI_MAP_FAILED = -6,
	SWEET_SPMI_DENIED = -7,
	SWEET_SPMI_DROPPED = -8,
	SWEET_SPMI_IO = -9,
	SWEET_SPMI_TIMEOUT = -10,
	SWEET_SPMI_POISONED = -11,
	SWEET_SPMI_VERIFY = -12,
	SWEET_SPMI_BUSY = -13
};
struct sweet_spmi_charger_route {
	uint16_t apid;
	uint8_t write_ee;
	uint8_t irq_owner;
};
/* Metadata only; no PMIC command or RW channel mapping. */
int sweet_spmi_charger_get_route(unsigned sid, unsigned addr,
	struct sweet_spmi_charger_route *out);
/* Exact readable-register table. Does not widen sweet_spmi_read's whitelist. */
int sweet_spmi_charger_read(unsigned sid, unsigned addr, uint8_t *out);
/* Mux-only readback of fixed battery IRQ configuration: USID2 peripherals
 * CHGR/USBIN/TYPEC/MISC/FG-SOC and offsets SET_TYPE/POLARITY_HIGH/POLARITY_LOW/
 * EN_SET. No PMIC writes, ownership changes or arbitrary status access.
 * Internally serialized; output unchanged on error. Not primary IRQ context. */
int sweet_spmi_battery_irq_read(unsigned peripheral, unsigned offset, uint8_t *out);
/* Stable configuration only. mask must be nonzero and a subset of the table;
 * value must contain no bits outside mask. Read/modify/write/readback is atomic
 * against all local controller users. W1C/trigger, IRQ, SRAM and secure writes
 * are rejected. Verified output is the actual byte, including preserved bits.
 * Unchanged values submit no write. This API performs no physical-unit limits;
 * the future battery controller must validate limits/protection before calling. */
int sweet_spmi_charger_update(unsigned sid, unsigned addr, unsigned mask,
	unsigned value, uint8_t *verified);
/* Fixed USID2:0x1643 bit0 pulse, serialized/owner checked/timeout poisoned.
 * No general pulse API or readable-value verification. Success is command
 * completion only. Caller must enforce lifetime and disabled-charge state. */
int sweet_spmi_charger_pet_watchdog(void);
/* Gen4 read-only SRAM data via bounded DMA request/grant/release. This does
 * write MEMIF protocol controls, never profile/learned SRAM contents. Fixed
 * four scalar fields; serial session lock releases the controller lock at waits.
 * Observer reads select the v5 PPID route independently of write ownership.
 * All protocol writers must have a unique EE0 route before any session write;
 * each actual update rechecks it. No foreign RW channel is accessed.
 * Once per boot, the first idle/identified DMA preflight may clear inherited
 * error logs via fixed CLEAR_LOG low/high/low commands and verify the result.
 * This is not a sampling retry: new pre/post DMA status faults disable SRAM
 * sessions until reboot. No FG reset, SRAM content or charging-policy writes.
 * Preserve output on any failure, including cleanup failure. */
#define SWEET_FG_TEMPERATURE       0u
#define SWEET_FG_NOMINAL_CAPACITY  1u
#define SWEET_FG_PROFILE_INTEGRITY 2u
#define SWEET_FG_LEARNED_CAPACITY  3u
int sweet_spmi_fg_read(unsigned field, uint16_t *out);
/* Fixed Gen4 profile evidence, all read in ONE bounded MEMIF session. No
 * address/length input and no profile/learning writes. Output unchanged on
 * any error, including release/restore failure. Call on a consumer workloop. */
#define SWEET_FG_PROFILE_PREFIX_SIZE 24u
struct sweet_fg_profile {
	uint8_t prefix[SWEET_FG_PROFILE_PREFIX_SIZE];
	uint8_t integrity;
	uint16_t nominal_mah;
	uint64_t started_ns, completed_ns;
};
int sweet_spmi_fg_read_profile(struct sweet_fg_profile *out);
#endif /* ARM64_BOARD_CONFIG_MH2LM */
/* Logs PMIC-arb v5 routing state for the PM8150B charger/USB peripherals.
 * This is a read-only bring-up probe: it never enables, clears, or claims
 * an interrupt. Call only after sweet_spmi_init() outside primary IRQ context. */
void sweet_spmi_irq_diag(void);
#define SWEET_SPMI_IRQ_DIAG_ROUTE_META    0x01u
#define SWEET_SPMI_IRQ_DIAG_ROUTE_OWNER   0x02u
#define SWEET_SPMI_IRQ_DIAG_ROUTE_ENABLE  0x04u
#define SWEET_SPMI_IRQ_DIAG_ROUTE_STATUS  0x08u
#define SWEET_SPMI_IRQ_DIAG_ROUTE_CHANNEL 0x10u
#define SWEET_SPMI_IRQ_DIAG_ROUTE_ALL \
	(SWEET_SPMI_IRQ_DIAG_ROUTE_META | SWEET_SPMI_IRQ_DIAG_ROUTE_OWNER | \
	SWEET_SPMI_IRQ_DIAG_ROUTE_ENABLE | SWEET_SPMI_IRQ_DIAG_ROUTE_STATUS | \
	SWEET_SPMI_IRQ_DIAG_ROUTE_CHANNEL)
/* Read-only route inspection for one PMIC peripheral. It never registers a
 * GIC handler, enables/clears a PMIC interrupt or writes a PMIC register. */
void sweet_spmi_irq_diag_source(unsigned sid, unsigned peripheral, const char *name);
void sweet_spmi_irq_diag_source_parts(unsigned sid, unsigned peripheral,
	const char *name, unsigned parts);
#ifdef __cplusplus
}
#endif
#endif
