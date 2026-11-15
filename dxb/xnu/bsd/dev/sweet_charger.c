/* PM8150B charger backend. Keep protocol encoding below battery policy.
 * No MMIO here: all transactions use the existing restricted controller API. */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "sweet_spmi.h"
#include "sweet_charger.h"
#include <kern/clock.h>

static int
sc_update(unsigned addr, unsigned mask, unsigned value, uint64_t deadline,
    uint8_t *actual)
{
 if (!(addr == 0x1042 && mask == 1 && value == 0)) {
  uint64_t now, ns;
  clock_get_uptime(&now); absolutetime_to_nanoseconds(now, &ns);
  if (!deadline || ns >= deadline) { return SWEET_CHARGER_EXPIRED; }
 }
 return sweet_spmi_charger_update(2, addr, mask, value, actual);
}

int
sweet_charger_execute(enum sweet_charger_operation operation,
    unsigned value, uint64_t deadline_ns, unsigned *verified)
{
 unsigned addr, mask, code, scale = 1, offset = 0;
 if (!verified) { return SWEET_SPMI_INVALID; }
 if (operation>=SWEET_CHARGER_PARALLEL_INHIBIT && operation<=SWEET_CHARGER_WATCHDOG_PET) {
  if (value!=1) return SWEET_SPMI_INVALID;
  uint8_t enabled;
  int rc=sweet_spmi_charger_read(2,0x1042,&enabled);
  if (rc) return rc;
  if (enabled&1) return SWEET_SPMI_NOT_READY;
 }
 switch (operation) {
 case SWEET_CHARGER_ENABLE:
 case SWEET_CHARGER_PAUSE:
 case SWEET_CHARGER_INPUT_SUSPEND:
  if (value > 1) { return SWEET_SPMI_INVALID; }
  addr = operation == SWEET_CHARGER_ENABLE ? 0x1042 :
      operation == SWEET_CHARGER_PAUSE ? 0x1043 : 0x1340;
  mask = 1; code = value; break;
 case SWEET_CHARGER_INPUT_LIMIT_MA:
 case SWEET_CHARGER_FAST_CURRENT_MA:
  if (!value || value > 500 || value % 50) { return SWEET_SPMI_INVALID; }
  addr = operation == SWEET_CHARGER_INPUT_LIMIT_MA ? 0x1370 : 0x1061;
  mask = 255; scale = 50; code = value / scale; break;
 case SWEET_CHARGER_FLOAT_VOLTAGE_MV:
  if (value < 3600 || value > 4200 || value % 10) { return SWEET_SPMI_INVALID; }
  addr = 0x1070; mask = 255; scale = 10; offset = 3600;
  code = (value - offset) / scale; break;
 case SWEET_CHARGER_BOARD_FAST_CURRENT_MA:
  if (!value || value > 2800 || value % 50) { return SWEET_SPMI_INVALID; }
  addr = 0x1061; mask = 255; scale = 50; code = value / scale; break;
 case SWEET_CHARGER_BOARD_FLOAT_VOLTAGE_MV:
  if (value < 3600 || value > 4430 || value % 10) { return SWEET_SPMI_INVALID; }
  addr = 0x1070; mask = 255; scale = 10; offset = 3600;
  code = (value - offset) / scale; break;
 case SWEET_CHARGER_CHARGING_PET: {
  if (value!=1) return SWEET_SPMI_INVALID;
  uint64_t ticks,ns;clock_get_uptime(&ticks);absolutetime_to_nanoseconds(ticks,&ns);
  if (!deadline_ns || ns>=deadline_ns) return SWEET_CHARGER_EXPIRED;
  int rc=sweet_spmi_charger_pet_watchdog();
  if (!rc) *verified=1; /* Bus completion only, never a timer readback. */
  return rc;
 }

 case SWEET_CHARGER_PARALLEL_INHIBIT:
  if (value!=1) return SWEET_SPMI_INVALID;
  /* LG SEC_NONE override: force SMB_EN low; preserve all CP/STAT bits.
   * Does NOT prove the I2C charger's CHG_EN_SRC/polarity or actual state. */
  addr=0x1648;mask=0x18;code=8;break;
 case SWEET_CHARGER_WATCHDOG_PREPARE: {
  if (value!=1) return SWEET_SPMI_INVALID;
  /* LG encodings: snarl8s/bark16s/bite8s, bite disables charging.
   * Keep watchdog plugin enable and bark IRQ, AFP off per SMB5 init.
   * Do not modify inherited thermal/JEITA or turn off the watchdog. */
  uint8_t actual;
  int rc=sc_update(0x1653,255,0xf3,deadline_ns,&actual);
  if (rc) return rc;
  if (actual!=0xf3) return SWEET_SPMI_VERIFY;
  rc=sc_update(0x1651,0xc2,0x42,deadline_ns,&actual);
  if (rc) return rc;
  if ((actual&0xc2)!=0x42) return SWEET_SPMI_VERIFY;
  *verified=1;return SWEET_SPMI_OK;
 }
 case SWEET_CHARGER_WATCHDOG_PET: {
  if (value!=1) return SWEET_SPMI_INVALID;
  uint64_t ticks,ns;clock_get_uptime(&ticks);absolutetime_to_nanoseconds(ticks,&ns);
  if (!deadline_ns || ns>=deadline_ns) return SWEET_CHARGER_EXPIRED;
  int rc=sweet_spmi_charger_pet_watchdog();
  if (!rc) *verified=1; /* Bus completion only, never a timer readback. */
  return rc;
 }
 case SWEET_CHARGER_SOFTWARE_INPUT_LIMIT: {
  if (value != 1) { return SWEET_SPMI_INVALID; }
  /* LG smb5-lib.c: ICL override mode, then override-after-APSD. Keep this
   * fixed sequence inside the hardware backend, with per-write verification. */
  const unsigned registers[] = {0x1366, 0x1342, 0x1365};
  const unsigned masks[] = {7, 1, 16}, codes[] = {1, 0, 16};
  for (unsigned i = 0; i < 3; ++i) {
   uint8_t actual;
   int rc = sc_update(registers[i], masks[i], codes[i], deadline_ns, &actual);
   if (rc) { return rc; }
   if ((actual & masks[i]) != codes[i]) { return SWEET_SPMI_VERIFY; }
  }
  *verified = 1; return SWEET_SPMI_OK;
 }
 default: return SWEET_SPMI_INVALID;
 }
 uint8_t actual;
 int rc = sc_update(addr, mask, code, deadline_ns, &actual);
 if (rc) { return rc; }
 if ((actual & mask) != code) { return SWEET_SPMI_VERIFY; }
 *verified = operation==SWEET_CHARGER_PARALLEL_INHIBIT ? 1 : (actual & mask) * scale + offset;
 return SWEET_SPMI_OK;
}
#endif /* ARM64_BOARD_CONFIG_MH2LM */
