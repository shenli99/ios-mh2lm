/* PM8150B measurement backend. The IOPMPowerSource service owns scheduling,
 * snapshots and publication; all register/protocol detail stays in this layer.
 * This is an internal stateless component, not a second competing power source.
 * No direct MMIO: use existing controller/FG APIs and their shared locks. */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "../../../bsd/dev/sweet_spmi.h"
#include "../../../bsd/dev/sweet_pmic_power.h"
#include "../../../bsd/dev/sweet_charger.h"
#include <kern/clock.h>

int
sweet_pmic_power_capture_watchdog_fault(struct sweet_watchdog_fault *out)
{
 return sweet_spmi_watchdog_capture_fault(out);
}

static uint64_t powerNow(void)
{
	uint64_t ticks, ns;
	clock_get_uptime(&ticks);
	absolutetime_to_nanoseconds(ticks, &ns);
	return ns;
}

/* FG exposes an IBATT copy register specifically for a consistent raw sample.
 * Do not write its hold bits: a matching pair is enough for diagnostics. */
static bool
powerReadWordPair(unsigned sid, unsigned address, unsigned copyAddress,
    uint16_t *value)
{
	for (unsigned attempt = 0; attempt < 4; ++attempt) {
		uint8_t lo, hi, copyLo, copyHi;
		if (sweet_spmi_read(sid, address, &lo) ||
		    sweet_spmi_read(sid, address + 1, &hi) ||
		    sweet_spmi_read(sid, copyAddress, &copyLo) ||
		    sweet_spmi_read(sid, copyAddress + 1, &copyHi)) {
			return false;
		}
		uint16_t sample = (uint16_t)((unsigned)lo | ((unsigned)hi << 8));
		uint16_t copy = (uint16_t)((unsigned)copyLo | ((unsigned)copyHi << 8));
		if (sample == copy) {
			*value = sample;
			return true;
		}
	}
	return false;
}

/* PM8150B's MONOTONIC_SOC is a byte with an adjacent shadow copy, not a
 * literal percentage. Match the Linux FG driver's bounded consistency check
 * before scaling it. A failed pair is unknown, never 0%. */
static bool
powerReadBytePair(unsigned sid, unsigned address, unsigned copyAddress,
    uint8_t *value)
{
	for (unsigned attempt = 0; attempt < 4; ++attempt) {
		uint8_t sample, copy;
		if (sweet_spmi_read(sid, address, &sample) ||
		    sweet_spmi_read(sid, copyAddress, &copy)) {
			return false;
		}
		if (sample == copy) {
			*value = sample;
			return true;
		}
	}
	return false;
}

static unsigned
powerScaleMsoc(uint8_t raw)
{
	/* Linux fg_get_msoc(): endpoints stay 0/100; raw 1..254 maps to 1..99
	 * with nearest-integer rounding. In particular 0xa8 is 66%, not invalid. */
	if (raw == 0xffu) {
		return 100u;
	}
	if (raw == 0u) {
		return 0u;
	}
	return (((unsigned)(raw - 1u) * 98u + 126u) / 253u) + 1u;
}

void
sweet_pmic_power_read_snapshot(SweetBattSnapshot &next, bool refreshInput,
    bool refreshCapacity, unsigned lastPercent)
{
	static const unsigned addresses[SB_REG_COUNT] = {
		0x1006,0x1007,0x100d,0x1042,0x1043,0x1051,0x1061,0x1070,
		0x110b,0x1108,0x1307,0x1308,0x1310,0x1340,0x1342,0x1365,
		0x1366,0x1370,0x1380,0x1506,0x1509,0x150b,0x1648,0x1651,
		0x1653,0x1690,0x1090,0x1067,0x1068,0x107d,0x1507,0x150d,0x1362,0x1610
	};
	if (refreshInput) {
		/* Invalidate before acquiring a replacement. Saturation fails closed;
		 * no wrap can make an earlier source epoch appear current. */
		next.valid &= ~sb_input_mask();
		next.apsdCoherent = next.ccCoherent = false;
		next.inputStartedNs = next.inputCompletedNs = 0;
		if (next.inputEpoch != ~0ULL) {
			++next.inputEpoch;
			next.inputStartedNs = powerNow();
		}
	}
	for (unsigned i = 0; i < SB_REG_COUNT; ++i) {
		/* Input tuple is acquired below, bracketed by status/VBUS reads. */
		if (sb_input_mask() & (1ULL << i)) { continue; }
		uint8_t value = 0;
		int rc = sweet_spmi_charger_read(2, addresses[i], &value);
		next.errors[i] = rc;
		next.valid &= ~(1ULL << i);
		next.raw[i] = 0; /* Failure is unknown, not a retained prior raw value. */
		if (!rc) { next.raw[i] = value; next.valid |= 1ULL << i; }
	}
	if (refreshInput) {
		/* No sleeping, rerun, or retry. Reject a result changing during reads.
		 * Matching bytes are a bounded consistency check, not a PMIC snapshot
		 * or proof of a new BC1.2 session. Source-event provenance is separate. */
		const unsigned fields[] = {SB_USB, SB_TYPEC, SB_ATTACHED, SB_MISC,
		    SB_DEBUG_ACCESS, SB_LEGACY, SB_APSD_OPTIONS, SB_APSD, SB_RESULT};
		for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
			unsigned field = fields[i];
			uint8_t value = 0;
			int rc = sweet_spmi_charger_read(2, addresses[field], &value);
			next.errors[field] = rc;
			next.raw[field] = rc ? 0 : value;
			if (!rc) { next.valid |= 1ULL << field; }
		}
		const unsigned afterFields[] = {SB_APSD, SB_TYPEC, SB_ATTACHED, SB_MISC,
		    SB_DEBUG_ACCESS, SB_LEGACY, SB_APSD_OPTIONS, SB_USB};
		for (unsigned i = 0; i < sizeof(afterFields) / sizeof(afterFields[0]); ++i) {
			unsigned field = afterFields[i];
			uint8_t after = 0;
			int rc = sweet_spmi_charger_read(2, addresses[field], &after);
			if (rc || !sb_valid(next, field) || after != next.raw[field]) {
				next.valid &= ~(1ULL << field);
				if (!next.errors[field]) { next.errors[field] = rc ? rc : SWEET_SPMI_VERIFY; }
			}
		}
		next.apsdCoherent = sb_valid(next, SB_USB) && sb_valid(next, SB_APSD) && sb_valid(next, SB_RESULT);
		/* CC validity does not require APSD enable, DONE, or RESULT. Compare
		 * every CC status byte and VBUS before/after; no retry or MMIO. */
		next.ccCoherent = (next.valid & sb_cc_mask()) == sb_cc_mask();
		if (next.inputStartedNs) { next.inputCompletedNs = powerNow(); }
	}
	uint8_t present = 0;
	next.presentValid = !sweet_spmi_read(2, 0x4107, &present);
	next.present = next.presentValid && !(present & 4);
	/* Gen4 has a defined voltage scale. Do not guess a different generation
	 * merely because a voltage falls outside the plausibility bounds. */
	next.voltageValid = powerReadWordPair(2, 0x41a0, 0x41a6, &next.rawVoltage);
	next.mv = (unsigned)(((uint64_t)next.rawVoltage * 122070u) / 1000000u);
	next.voltageValid = next.voltageValid && next.mv >= 2800 && next.mv <= 4600;
	next.currentValid = powerReadWordPair(2, 0x41a2, 0x41a8, &next.rawCurrent);
	int signedRaw = (next.rawCurrent & 0x8000) ? (int)next.rawCurrent - 65536 : next.rawCurrent;
	next.ma = (int)(-((int64_t)signedRaw * 488281) / 1000000);
	next.currentValid = next.currentValid && next.ma >= -8000 && next.ma <= 8000;
	uint8_t soc = 0, ready = 0;
	next.socValid = !sweet_spmi_read(2, 0x4010, &ready) && (ready & 2) &&
	    powerReadBytePair(2, 0x4009, 0x400a, &soc);
	next.percent = next.socValid ? powerScaleMsoc(soc) : lastPercent;
	next.tempError = sweet_spmi_fg_read(SWEET_FG_TEMPERATURE, &next.rawTemp);
	int rawTemp = next.rawTemp & 0x3ff;
	if (rawTemp & 0x200) { rawTemp -= 0x400; }
	next.tempCenti = rawTemp * 25;
	next.tempValid = !next.tempError && next.rawTemp != 0xffffu && next.tempCenti >= -4000 && next.tempCenti <= 8000;
	/* Identity/capacity is sampled at initial observation and each real event,
	 * not on every protection tick. Never write/reload SRAM profile data. */
	if (refreshCapacity) {
		struct sweet_fg_profile profile = {};
		uint16_t learned = 0;
		next.profilePrefixError = sweet_spmi_fg_read_profile(&profile);
		next.profilePrefixValid = !next.profilePrefixError;
		next.profileStartedNs = profile.started_ns;
		next.profileCompletedNs = profile.completed_ns;
		for (unsigned i = 0; i < 24; ++i) { next.profilePrefix[i] = profile.prefix[i]; }
		next.profileError = next.nominalError = next.profilePrefixError;
		next.profileValid = !next.profileError;
		next.integrity = profile.integrity;
		next.learnedError = sweet_spmi_fg_read(SWEET_FG_LEARNED_CAPACITY, &learned);
		next.nominalValid = !next.nominalError && next.profileValid && (profile.integrity & 1) &&
		    profile.nominal_mah >= 1000 && profile.nominal_mah <= 6000;
		next.learnedValid = next.nominalValid && !next.learnedError &&
		    learned >= profile.nominal_mah / 2 && learned <= profile.nominal_mah * 12 / 10;
		next.nominalMah = profile.nominal_mah;
		next.learnedMah = learned;
	}
}
#endif /* ARM64_BOARD_CONFIG_MH2LM */
