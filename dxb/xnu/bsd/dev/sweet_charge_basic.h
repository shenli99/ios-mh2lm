/* Runtime3 charge controller, modeled on LG SMB5 init_hw + CHG_DISABLE vote.
 * The PMIC owns APSD/AICL/ICL, CC/CV, termination, recharge and its hard
 * temperature/BAT_OV limits. Software, as on LG, bounds FCC/FV, keeps the
 * secondary charger's SMB_EN low, configures the charger watchdog (bite stops
 * charging), pets it only from the bark IRQ, applies the LG protection-battemp
 * table and votes disable on any local fault. Decisions run on charger/input
 * IRQs and on LG's temperature cadence; no ADC, identity or permit gates.
 * No register addresses here; see sweet_charger.c.
 * Battery workloop only. Units: mV, mA, centidegrees C, ns. */
#ifndef SWEET_CHARGE_BASIC_H
#define SWEET_CHARGE_BASIC_H
#include "sweet_battery_runtime.h"
#include "sweet_spmi.h"

enum SweetChargeBasicReason : uint32_t {
 SCB_R_TELEMETRY=1u<<0, SCB_R_NO_INPUT=1u<<1, SCB_R_NO_BATTERY=1u<<2,
 SCB_R_TEMPERATURE=1u<<3, SCB_R_VOLTAGE=1u<<4, SCB_R_CHARGER=1u<<5,
 SCB_R_INPUT_OV=1u<<6, SCB_R_STOPPING=1u<<7, SCB_R_BACKOFF=1u<<8,
 SCB_R_FAILED=1u<<9
};
/* BLT42 profile FV is 4430mV and LG board FCC 2800mA; stay below both. */
#define SCB_FCC_MA 2000u
#define SCB_FV_MV 4400u
#define SCB_VBAT_MAX_MV 4450u
/* LG sm8150_common-pm.dtsi protection-battemp, capacity-mah-min 3880,
 * percentages rounded down to 50mA as in battemp_create_parsedt. */
#define SCB_ALERT_MV 4000
#define SCB_COOL_ALERT_MA 500u
#define SCB_COOL_MA 1150u
#define SCB_WARM_MA 1900u
#define SCB_WARM_FV_MV 4000u
#define SCB_CHILLY_ALERT_MA 1000u
#define SCB_CHILLY_MA 1900u
/* LG period-ms-normal/warning/emergency; unknown waits for the next event. */
#define SCB_PERIOD_GOOD_MS 60000u
#define SCB_PERIOD_WARNING_MS 30000u
#define SCB_PERIOD_EMERGENCY_MS 10000u
#define SCB_BACKOFF_NS 5000000000ULL
#define SCB_STABLE_NS 60000000000ULL
#define SCB_MAX_FAILURES 5u

enum SweetChargeHealth { SCB_H_UNKNOWN, SCB_H_COLD, SCB_H_COOL, SCB_H_GOOD, SCB_H_WARM, SCB_H_HOT };
struct SweetChargeBasic {
 bool enabled, failed, chilly;
 enum SweetChargeHealth health;
 unsigned failures, enables, pets, petErrors, fccMa, fvMv;
 uint64_t enabledNs, programmedNs, retryNs;
 uint32_t reasons;
 int lastError;
};
enum SweetChargeBasicStep { SCB_IDLE, SCB_ENABLED, SCB_DISABLED, SCB_ERROR, SCB_LIMITS };
typedef int (*SweetChargeCommand)(void *, enum sweet_charger_operation, unsigned, unsigned *);
/* LG protection-battemp.c:polling_status_jeita, thresholds in 0.1 C:
 * upto cool30/good120/warm450/hot550, downto warm520/good430/cool100/cold0. */
static inline enum SweetChargeHealth
scb_health(enum SweetChargeHealth now, int deci)
{
 int cold=now==SCB_H_COLD?30:0, cool=now==SCB_H_COOL?120:100;
 int good=now==SCB_H_WARM?430:450, warm=now==SCB_H_HOT?520:550;
 if (deci<cold) return SCB_H_COLD;
 if (deci<cool) return SCB_H_COOL;
 if (deci<good) return SCB_H_GOOD;
 if (deci<warm) return SCB_H_WARM;
 return SCB_H_HOT;
}

/* LG reschedules by health; telemetry loss re-checks at emergency cadence. */
static inline unsigned
scb_period_ms(const SweetChargeBasic &c)
{
 if (c.reasons&SCB_R_TELEMETRY) return SCB_PERIOD_EMERGENCY_MS;
 if (c.retryNs && !c.failed) return SCB_PERIOD_EMERGENCY_MS;
 switch (c.health) {
 case SCB_H_GOOD: return c.chilly?SCB_PERIOD_WARNING_MS:SCB_PERIOD_GOOD_MS;
 case SCB_H_COOL: case SCB_H_WARM: return SCB_PERIOD_WARNING_MS;
 default: return SCB_PERIOD_EMERGENCY_MS;
 }
}

static inline uint32_t
scb_decide(SweetChargeBasic &c, const SweetBattSnapshot &s, uint64_t now, bool stopping,
 unsigned *fccMa, unsigned *fvMv)
{
 uint32_t r=0;
 const uint64_t needed=(1ULL<<SB_ERROR)|(1ULL<<SB_THERMAL)|(1ULL<<SB_ENABLE)|
  (1ULL<<SB_USB)|(1ULL<<SB_FCC)|(1ULL<<SB_FV);
 if ((s.valid&needed)!=needed || !s.voltageValid || !s.tempValid || !s.presentValid)
  r|=SCB_R_TELEMETRY;
 if (sb_valid(s,SB_USB) && !(s.raw[SB_USB]&16)) r|=SCB_R_NO_INPUT;
 if (sb_valid(s,SB_USB) && (s.raw[SB_USB]&8)) r|=SCB_R_INPUT_OV;
 if (s.presentValid && !s.present) r|=SCB_R_NO_BATTERY;
 if (s.tempValid) {
  int deci=s.tempCenti/10;
  c.health=scb_health(c.health,deci);
  c.chilly=deci>=0 && deci<=150;
 }
 unsigned fcc=SCB_FCC_MA, fv=SCB_FV_MV;
 bool alert=s.voltageValid && (int)s.mv>=SCB_ALERT_MV;
 switch (c.health) {
 case SCB_H_COOL: fcc=alert?SCB_COOL_ALERT_MA:SCB_COOL_MA; break;
 case SCB_H_WARM: fcc=SCB_WARM_MA; fv=SCB_WARM_FV_MV; break;
 case SCB_H_GOOD: break;
 default: r|=SCB_R_TEMPERATURE; break;
 }
 if (c.chilly) {
  unsigned limit=alert?SCB_CHILLY_ALERT_MA:SCB_CHILLY_MA;
  if (limit<fcc) fcc=limit;
 }
 if (s.voltageValid && s.mv>SCB_VBAT_MAX_MV) r|=SCB_R_VOLTAGE;
 /* PMIC BAT_OV, or its hard/AFP battery temperature limits. */
 if ((sb_valid(s,SB_ERROR) && (s.raw[SB_ERROR]&2)) ||
     (sb_valid(s,SB_THERMAL) && (s.raw[SB_THERMAL]&15))) r|=SCB_R_CHARGER;
 if (stopping) r|=SCB_R_STOPPING;
 if (c.failed) r|=SCB_R_FAILED;
 else if (c.retryNs && now<c.retryNs) r|=SCB_R_BACKOFF;
 c.reasons=r;*fccMa=fcc;*fvMv=fv;
 return r;
}
/* Any command error: best-effort disable, then back off. Repeated failures
 * (including another writer clearing enable) stop until the next boot. */
static inline SweetChargeBasicStep
scb_fail(SweetChargeBasic &c, int rc, uint64_t now, SweetChargeCommand cmd, void *ctx)
{
 unsigned actual=0;
 (void)cmd(ctx,SWEET_CHARGER_ENABLE,0,&actual);
 c.enabled=false;c.lastError=rc;
 if (++c.failures>=SCB_MAX_FAILURES) c.failed=true;
 c.retryNs=now+SCB_BACKOFF_NS;
 return SCB_ERROR;
}

static inline bool
scb_limits_match(const SweetChargeBasic &c, const SweetBattSnapshot &s)
{
 return s.raw[SB_FCC]*50u==c.fccMa && 3600u+s.raw[SB_FV]*10u==c.fvMv;
}

/* bark: the charger watchdog bark IRQ was delivered since the last step.
 * LG wdog_bark_irq_handler pets unconditionally; a missed pet lets the
 * hardware bite stop charging, so a pet error while enabled is a fault. */
static inline SweetChargeBasicStep
scb_step(SweetChargeBasic &c, const SweetBattSnapshot &s, uint64_t now, bool stopping,
 bool bark, SweetChargeCommand cmd, void *ctx)
{
 unsigned fcc=0, fv=0;
 uint32_t r=scb_decide(c,s,now,stopping,&fcc,&fv);
 if (bark) {
  unsigned actual=0;
  int rc=cmd(ctx,SWEET_CHARGER_CHARGING_PET,1,&actual);
  if (!rc && actual==1) ++c.pets;
  else {
   ++c.petErrors;
   if (c.enabled) return scb_fail(c,rc?rc:SWEET_SPMI_VERIFY,now,cmd,ctx);
  }
 }
 bool hwEnabled=sb_valid(s,SB_ENABLE) && (s.raw[SB_ENABLE]&1);
 if (r) {
  /* Also clears an enable inherited from boot firmware. */
  if (!c.enabled && !hwEnabled) return SCB_IDLE;
  unsigned actual=1;
  int rc=cmd(ctx,SWEET_CHARGER_ENABLE,0,&actual);
  c.enabled=false;
  if (rc || actual) return scb_fail(c,rc?rc:SWEET_SPMI_VERIFY,now,cmd,ctx);
  return SCB_DISABLED;
 }
 if (c.enabled) {
  /* Only a snapshot started after our last write can show external changes. */
  bool observed=s.startedNs>c.programmedNs;
  if (!observed || (hwEnabled && scb_limits_match(c,s))) {
   if (c.failures && now-c.enabledNs>=SCB_STABLE_NS) c.failures=0;
   if (fcc==c.fccMa && fv==c.fvMv) return SCB_IDLE;
   /* Temperature band changed: LG re-votes FCC/FV while charging. */
   const enum sweet_charger_operation op[]={SWEET_CHARGER_BOARD_FAST_CURRENT_MA,
    SWEET_CHARGER_BOARD_FLOAT_VOLTAGE_MV};
   const unsigned value[]={fcc,fv};
   for (unsigned i=0;i<2;++i) {
    unsigned actual=~0u;
    int rc=cmd(ctx,op[i],value[i],&actual);
    if (rc || actual!=value[i]) return scb_fail(c,rc?rc:SWEET_SPMI_VERIFY,now,cmd,ctx);
   }
   c.fccMa=fcc;c.fvMv=fv;c.programmedNs=now;
   return SCB_LIMITS;
  }
  /* Enable or limits changed under us: count it, then reprogram. */
  c.enabled=false;
  if (c.failures+1>=SCB_MAX_FAILURES) return scb_fail(c,SWEET_SPMI_VERIFY,now,cmd,ctx);
  ++c.failures;
 }
 /* LG order: disable, isolate secondary, watchdog config + pet, limits,
  * unpause/resume input, enable last. Every write is read back. */
 const enum sweet_charger_operation op[]={SWEET_CHARGER_ENABLE,
  SWEET_CHARGER_PARALLEL_INHIBIT,SWEET_CHARGER_WATCHDOG_PREPARE,
  SWEET_CHARGER_WATCHDOG_PET,SWEET_CHARGER_BOARD_FAST_CURRENT_MA,
  SWEET_CHARGER_BOARD_FLOAT_VOLTAGE_MV,SWEET_CHARGER_PAUSE,
  SWEET_CHARGER_INPUT_SUSPEND,SWEET_CHARGER_ENABLE};
 const unsigned value[]={0,1,1,1,fcc,fv,0,0,1};
 for (unsigned i=0;i<sizeof(op)/sizeof(op[0]);++i) {
  unsigned actual=~0u;
  int rc=cmd(ctx,op[i],value[i],&actual);
  if (rc || actual!=value[i]) return scb_fail(c,rc?rc:SWEET_SPMI_VERIFY,now,cmd,ctx);
 }
 c.enabled=true;c.fccMa=fcc;c.fvMv=fv;c.enabledNs=c.programmedNs=now;
 c.retryNs=0;++c.enables;
 return SCB_ENABLED;
}
#endif
