/* Battery policy/controller: no IOKit, MMIO, timer or global service state.
 * The battery's workloop owns all instances. Units: mV, mA, centidegrees C, ns.
 * Board evidence comes from real providers; no boot-arg can assert validity. */
#ifndef SWEET_BATTERY_RUNTIME_H
#define SWEET_BATTERY_RUNTIME_H
#include <stdint.h>
#include <stdbool.h>
#include "sweet_charger.h"

enum SweetBattReg {
 SB_STAGE, SB_ERROR, SB_THERMAL, SB_ENABLE, SB_PAUSE, SB_CFG2, SB_FCC, SB_FV,
 SB_PATH, SB_SETTLED, SB_APSD, SB_RESULT, SB_USB, SB_SUSPEND, SB_OVERRIDE,
 SB_LOAD, SB_MODE, SB_ICL, SB_AICL, SB_TYPEC, SB_ATTACHED, SB_MISC,
 SB_SMB, SB_WDOG, SB_WDOG_TIME, SB_SMB_CFG, SB_JEITA, SB_ITERM_HI,
 SB_ITERM_LO, SB_RECHARGE, SB_DEBUG_ACCESS, SB_LEGACY, SB_APSD_OPTIONS, SB_WDOG_RT, SB_REG_COUNT
};
struct SweetBattSnapshot {
 uint8_t raw[SB_REG_COUNT];
 int errors[SB_REG_COUNT];
 uint64_t valid, startedNs, completedNs;
 bool voltageValid, currentValid, tempValid, presentValid, present;
 bool socValid, nominalValid, learnedValid, profileValid;
 unsigned mv, percent, nominalMah, learnedMah;
 int ma, tempCenti; /* IOPM sign: positive current = charging */
 uint16_t rawVoltage, rawCurrent, rawTemp, integrity;
 int tempError, nominalError, learnedError, profileError;
 /* Input timestamps/epoch change ONLY on initial/plugin/source/Type-C reads.
  * Protection telemetry must not renew a cached source's acquisition time. */
 uint64_t inputEpoch, inputStartedNs, inputCompletedNs;
 /* Observed USB session, separate from snapshot acquisition epoch. Source IRQ
  * qualifies a completion; a fresh register read alone never does. Missed or
  * coalesced physical detach/attach cannot be ruled out by this evidence. */
 uint64_t usbSession, apsdSession, apsdStartedNs, apsdCompletedNs;
 unsigned inputEvents;
 bool apsdCoherent, apsdFromSource;
 /* CC evidence is independent of APSD DONE/enable/result. Its event time
  * cannot be renewed by source-change or protection telemetry. */
 bool ccCoherent, ccFromEvent;
 uint64_t ccSession, ccStartedNs, ccCompletedNs;
 uint8_t profilePrefix[24];
 bool profilePrefixValid;
 int profilePrefixError;
 uint64_t profileStartedNs, profileCompletedNs;
};
static inline bool sb_valid(const SweetBattSnapshot &s, unsigned field)
{ return field < SB_REG_COUNT && (s.valid & (1ULL << field)); }

static inline uint64_t sb_cc_mask(void)
{
 return (1ULL<<SB_USB)|(1ULL<<SB_TYPEC)|(1ULL<<SB_ATTACHED)|
  (1ULL<<SB_MISC)|(1ULL<<SB_DEBUG_ACCESS)|(1ULL<<SB_LEGACY);
}
static inline uint64_t sb_input_mask(void)
{
 return (1ULL<<SB_USB)|(1ULL<<SB_APSD)|(1ULL<<SB_RESULT)|(1ULL<<SB_TYPEC)|
  (1ULL<<SB_ATTACHED)|(1ULL<<SB_MISC)|(1ULL<<SB_DEBUG_ACCESS)|(1ULL<<SB_LEGACY)|
  (1ULL<<SB_APSD_OPTIONS);
}
static inline bool sb_input_fresh(const SweetBattSnapshot &s, uint64_t now)
{
 return (s.valid&sb_cc_mask())==sb_cc_mask() && s.inputEpoch && s.inputEpoch!=~0ULL &&
  s.inputStartedNs && s.inputCompletedNs>=s.inputStartedNs &&
  now>=s.inputCompletedNs && now-s.inputStartedNs<=3000000000ULL;
}
static inline uint64_t sb_evidence_deadline(uint64_t start)
{ return start>~0ULL-3000000000ULL?~0ULL:start+3000000000ULL; }
enum SweetBattSourceType {
 SB_SOURCE_UNKNOWN, SB_SOURCE_SDP, SB_SOURCE_CDP, SB_SOURCE_DCP,
 SB_SOURCE_OCP, SB_SOURCE_FLOAT, SB_SOURCE_QC2, SB_SOURCE_QC3
};
/* LG smblib_get_apsd_result: exact masked values, status QC override. This
 * describes detection, never establishes a PD/USB current permission. */
static inline SweetBattSourceType sb_source_type(const SweetBattSnapshot &s)
{
 if (!sb_valid(s,SB_APSD) || !sb_valid(s,SB_RESULT) || !(s.raw[SB_APSD]&1))
  return SB_SOURCE_UNKNOWN;
 unsigned result=s.raw[SB_RESULT]&0x7f;
 if (s.raw[SB_APSD]&2) return result==0x48?SB_SOURCE_QC3:SB_SOURCE_QC2;
 switch (result) {
 case 1: return SB_SOURCE_SDP; case 4: return SB_SOURCE_CDP;
 case 8: return SB_SOURCE_DCP; case 2: return SB_SOURCE_OCP;
 case 0x10: return SB_SOURCE_FLOAT; case 0x28: return SB_SOURCE_QC2;
 case 0x48: return SB_SOURCE_QC3; default: return SB_SOURCE_UNKNOWN;
 }
}
/* These values mirror the mux consumer events; compile-time checked by the
 * IOKit adapter. No dependency on mux/service internals in policy code. */
enum { SB_INPUT_PLUGIN = 0x01u, SB_INPUT_SOURCE = 0x10u, SB_INPUT_TYPEC = 0x40u,
 SB_INPUT_PROTECTION = 0x80u };
enum SweetBattCcReason : uint32_t {
 SB_CC_READ=1u<<0, SB_CC_RACE=1u<<1, SB_CC_NO_VBUS=1u<<2,
 SB_CC_DETACHED=1u<<3, SB_CC_DEBOUNCE=1u<<4, SB_CC_SOURCE=1u<<5,
 SB_CC_WATER=1u<<6,
 /* Bit7 is reserved: 0160/0161 incorrectly vetoed sink Rp on misc bit4. */
 SB_CC_VCONN_OC=1u<<8, SB_CC_DEBUG=1u<<9, SB_CC_LEGACY=1u<<10,
 SB_CC_RP=1u<<11, SB_CC_USB_OV=1u<<12, SB_CC_USB_UV=1u<<13
};
enum SweetBattCcRole { SB_CC_ROLE_UNKNOWN, SB_CC_ROLE_SINK, SB_CC_ROLE_SOURCE };
enum SweetBattCcObserved : uint32_t { SB_CC_OBS_VBUS_ERROR=1u<<0 };
enum SweetBattRp { SB_RP_UNKNOWN, SB_RP_DEFAULT, SB_RP_1P5, SB_RP_3P0 };
struct SweetBattCc {
 uint32_t reasons, observed;
 SweetBattCcRole role; /* observed misc direction, not a managed PD role */
 SweetBattRp rp;
 unsigned advertisedMa, orientation; /* default Rp does not grant 500mA */
};
static inline SweetBattCc sb_cc_status(const SweetBattSnapshot &s)
{
 SweetBattCc cc={};
 if ((s.valid&sb_cc_mask())!=sb_cc_mask()) { cc.reasons=SB_CC_READ;return cc; }
 if (!s.ccCoherent) cc.reasons|=SB_CC_RACE;
 unsigned misc=s.raw[SB_MISC],rp=s.raw[SB_TYPEC]&0x7f;
 /* LG SMB5 smblib_get_prop_typec_mode selects UFP/DFP from misc bit6,
  * then smblib_get_prop_ufp_mode interprets Rp. It does not use misc bit4
  * as a sink Rp veto. Preserve that observation without calling it a USBIN
  * voltage fault or clearing hardware. Actual USBIN OV/UV are separate
  * RT_STS bits (smb5-reg.h), rejected below even when Rp looks valid. */
 cc.role=(misc&0x40)?SB_CC_ROLE_SOURCE:SB_CC_ROLE_SINK;
 if (misc&0x10) cc.observed|=SB_CC_OBS_VBUS_ERROR;
 if (s.raw[SB_USB]&8) cc.reasons|=SB_CC_USB_OV;
 if (s.raw[SB_USB]&4) cc.reasons|=SB_CC_USB_UV;
 if (!(s.raw[SB_USB]&16) || !(misc&0x20)) cc.reasons|=SB_CC_NO_VBUS;
 if (!(s.raw[SB_ATTACHED]&0x20) || !(misc&1)) cc.reasons|=SB_CC_DETACHED;
 if (!(misc&8)) cc.reasons|=SB_CC_DEBOUNCE;
 if (cc.role==SB_CC_ROLE_SOURCE) cc.reasons|=SB_CC_SOURCE;
 if (misc&0x80) cc.reasons|=SB_CC_WATER;
 if (misc&4) cc.reasons|=SB_CC_VCONN_OC;
 if ((s.raw[SB_DEBUG_ACCESS]&0x3f) || (rp&0x70)) cc.reasons|=SB_CC_DEBUG;
 if (s.raw[SB_LEGACY]&3) cc.reasons|=SB_CC_LEGACY;
 /* Never interpret stale sink-status Rp as an input source while sourcing. */
 if (cc.role==SB_CC_ROLE_SINK) switch(rp) {
 case 8: cc.rp=SB_RP_DEFAULT;break;
 case 4: cc.rp=SB_RP_1P5;cc.advertisedMa=1500;break;
 case 2: cc.rp=SB_RP_3P0;cc.advertisedMa=3000;break;
 default: cc.reasons|=SB_CC_RP;break;
 }
 cc.orientation=(misc&1)?((misc&2)?2u:1u):0u;
 return cc;
}
static inline bool sb_same_cc(const SweetBattSnapshot &a,const SweetBattSnapshot &b)
{
 return (a.valid&sb_cc_mask())==sb_cc_mask() &&
  (b.valid&sb_cc_mask())==sb_cc_mask() && a.raw[SB_USB]==b.raw[SB_USB] &&
  a.raw[SB_TYPEC]==b.raw[SB_TYPEC] && a.raw[SB_ATTACHED]==b.raw[SB_ATTACHED] &&
  a.raw[SB_MISC]==b.raw[SB_MISC] && a.raw[SB_DEBUG_ACCESS]==b.raw[SB_DEBUG_ACCESS] &&
  a.raw[SB_LEGACY]==b.raw[SB_LEGACY];
}
static inline void sb_note_cc(SweetBattSnapshot &s,const SweetBattSnapshot &previous,unsigned events)
{
 s.ccFromEvent=false;s.ccSession=s.ccStartedNs=s.ccCompletedNs=0;
 if (!s.usbSession || s.usbSession==~0ULL || sb_cc_status(s).reasons) return;
 bool same=previous.ccFromEvent && previous.ccSession==s.usbSession && sb_same_cc(s,previous);
 /* Duplicate notifications never extend an unchanged observation's lifetime.
  * Startup/source alone cannot establish fresh CC provenance. */
 if (same) {
  s.ccFromEvent=true;s.ccSession=previous.ccSession;
  s.ccStartedNs=previous.ccStartedNs;s.ccCompletedNs=previous.ccCompletedNs;
 } else if (events&(SB_INPUT_PLUGIN|SB_INPUT_TYPEC)) {
  s.ccFromEvent=true;s.ccSession=s.usbSession;
  s.ccStartedNs=s.inputStartedNs;s.ccCompletedNs=s.inputCompletedNs;
 }
}
static inline bool sb_cc_event_fresh(const SweetBattSnapshot &s,uint64_t now)
{
 return s.ccFromEvent && !sb_cc_status(s).reasons && s.usbSession &&
  s.usbSession!=~0ULL && s.ccSession==s.usbSession && sb_input_fresh(s,now) &&
  s.ccStartedNs && s.ccCompletedNs>=s.ccStartedNs && now>=s.ccCompletedNs &&
  now-s.ccStartedNs<=3000000000ULL;
}
static inline void sb_note_input(SweetBattSnapshot &s,
 const SweetBattSnapshot &previous, unsigned events)
{
 s.inputEvents=events;
 s.apsdFromSource=false;
 s.apsdSession=s.apsdStartedNs=s.apsdCompletedNs=0;
 bool known=sb_valid(s,SB_USB), oldKnown=sb_valid(previous,SB_USB);
 bool present=known && (s.raw[SB_USB]&16);
 bool oldPresent=oldKnown && (previous.raw[SB_USB]&16);
 if (!known || !oldKnown || present!=oldPresent) {
  /* Saturation is terminal, never wrap to reuse an old session. */
  if (s.usbSession!=~0ULL) ++s.usbSession;
 }
 sb_note_cc(s,previous,events);
 if (!known || !present || !s.usbSession || s.usbSession==~0ULL ||
     !s.apsdCoherent || !sb_valid(s,SB_APSD_OPTIONS) ||
     !(s.raw[SB_APSD_OPTIONS]&8) || sb_source_type(s)==SB_SOURCE_UNKNOWN) return;
 if (events&SB_INPUT_SOURCE) {
  s.apsdFromSource=true;s.apsdSession=s.usbSession;
  s.apsdStartedNs=s.inputStartedNs;s.apsdCompletedNs=s.inputCompletedNs;
 } else if (oldKnown && oldPresent && previous.apsdFromSource &&
     previous.apsdSession==s.usbSession &&
     previous.raw[SB_APSD]==s.raw[SB_APSD] &&
     previous.raw[SB_RESULT]==s.raw[SB_RESULT] &&
     previous.raw[SB_TYPEC]==s.raw[SB_TYPEC] &&
     previous.raw[SB_ATTACHED]==s.raw[SB_ATTACHED] &&
     previous.raw[SB_MISC]==s.raw[SB_MISC] &&
     previous.raw[SB_DEBUG_ACCESS]==s.raw[SB_DEBUG_ACCESS] &&
     previous.raw[SB_LEGACY]==s.raw[SB_LEGACY]) {
  /* A plugin notification can follow source-change. Preserve compatible
   * evidence, but NEVER renew its source-event acquisition time. */
  s.apsdFromSource=true;s.apsdSession=previous.apsdSession;
  s.apsdStartedNs=previous.apsdStartedNs;s.apsdCompletedNs=previous.apsdCompletedNs;
 }
}
static inline bool sb_apsd_event_fresh(const SweetBattSnapshot &s,uint64_t now)
{
 return s.apsdFromSource && s.apsdCoherent && s.usbSession &&
  s.usbSession!=~0ULL && s.apsdSession==s.usbSession &&
  sb_input_fresh(s,now) && sb_valid(s,SB_APSD_OPTIONS) &&
  (s.raw[SB_APSD_OPTIONS]&8) && (s.raw[SB_USB]&16) &&
  s.apsdStartedNs && s.apsdCompletedNs>=s.apsdStartedNs &&
  now>=s.apsdCompletedNs && now-s.apsdStartedNs<=3000000000ULL &&
  sb_source_type(s)!=SB_SOURCE_UNKNOWN;
}
static inline bool sb_normal_sink(const SweetBattSnapshot &s)
{
 return sb_cc_status(s).reasons==0;
}
enum SweetBattInputKind { SB_INPUT_NONE, SB_INPUT_BC12, SB_INPUT_CC };
struct SweetBattInputCapability {
 SweetBattInputKind kind;
 unsigned maxMa;
 uint64_t startedNs;
};
/* This is observed input evidence, NOT a PD contract or an inputVerified
 * provider. Never infer PD from Rp, nor authorize elevated voltage/QC. */
static inline SweetBattInputCapability sb_input_capability(const SweetBattSnapshot &s,uint64_t now)
{
 SweetBattInputCapability cap={};
 SweetBattSourceType type=sb_source_type(s);
 if (type==SB_SOURCE_QC2 || type==SB_SOURCE_QC3) return cap;
 SweetBattCc cc=sb_cc_status(s);
 if (sb_cc_event_fresh(s,now) && cc.advertisedMa) {
  cap.kind=SB_INPUT_CC;cap.maxMa=cc.advertisedMa;cap.startedNs=s.ccStartedNs;
 } else if (sb_normal_sink(s) && sb_apsd_event_fresh(s,now) &&
     (type==SB_SOURCE_DCP || type==SB_SOURCE_CDP)) {
  /* Initial control trial remains capped at 500mA below either BC class. */
  cap.kind=SB_INPUT_BC12;cap.maxMa=500;cap.startedNs=s.apsdStartedNs;
 }
 return cap;
}
/* Durable source classification, owned by the battery workloop. Only an input
 * IRQ acquisition may establish/revalidate it. Voltage refreshes never create
 * source provenance or renew its event timestamp. Not a standalone IOService.
 * The observed session cannot distinguish coalesced detach/attach with identical
 * status; it is not a PD contract and never grants QC/elevated voltage. */
struct SweetBattInputSession {
 SweetBattInputKind kind;
 unsigned maxMa;
 uint64_t session, epoch, epochCompletedNs, sourceStartedNs, sourceCompletedNs;
};
struct SweetBattInputVoltage {
 bool valid;
 unsigned microvolts, sequence;
 uint64_t epoch, startedNs, completedNs;
};
struct SweetBattInputPermit {
 bool valid;
 SweetBattInputKind kind;
 unsigned maxMa, vbusMv, voltageSequence;
 uint64_t session, epoch, voltageStartedNs, validUntilNs;
};
static inline bool sb_input_source_matches(const SweetBattInputSession &m,
 const SweetBattSnapshot &s)
{
 if (m.kind==SB_INPUT_NONE || !m.session || m.session!=s.usbSession ||
     s.usbSession==~0ULL || sb_cc_status(s).reasons || !s.apsdCoherent ||
     (s.valid&sb_input_mask())!=sb_input_mask()) return false;
 SweetBattSourceType type=sb_source_type(s);
 if ((s.raw[SB_APSD]&2) || type==SB_SOURCE_QC2 || type==SB_SOURCE_QC3 ||
     ((s.raw[SB_APSD]&1) && type==SB_SOURCE_UNKNOWN)) return false;
 if (m.kind==SB_INPUT_CC) return s.ccFromEvent && s.ccSession==m.session &&
  s.ccStartedNs==m.sourceStartedNs && s.ccCompletedNs==m.sourceCompletedNs &&
  sb_cc_status(s).advertisedMa==m.maxMa;
 return m.kind==SB_INPUT_BC12 && s.apsdFromSource && s.apsdSession==m.session &&
  (s.raw[SB_APSD_OPTIONS]&8) && (type==SB_SOURCE_DCP || type==SB_SOURCE_CDP) &&
  s.apsdStartedNs==m.sourceStartedNs && s.apsdCompletedNs==m.sourceCompletedNs;
}
static inline void sb_input_observe(SweetBattInputSession &m,
 const SweetBattSnapshot &s, uint64_t now)
{
 SweetBattInputSession next={};
 if (!sb_input_fresh(s,now)) { m=next;return; }
 SweetBattInputCapability cap=sb_input_capability(s,now);
 if (m.kind==SB_INPUT_CC && sb_input_source_matches(m,s)) {
  next=m; /* LG: a still valid medium/high Rp takes priority over BC1.2. */
 } else if (cap.kind!=SB_INPUT_NONE) {
  next.kind=cap.kind;next.maxMa=cap.maxMa;next.session=s.usbSession;
  next.sourceStartedNs=cap.startedNs;
  next.sourceCompletedNs=cap.kind==SB_INPUT_CC?s.ccCompletedNs:s.apsdCompletedNs;
 } else if (sb_input_source_matches(m,s)) {
  /* Same previously qualified source, revalidated by an actual input event.
   * Retain its original event time even after the diagnostic 3s window. */
  next=m;
 }
 if (!sb_input_source_matches(next,s)) { m={};return; }
 next.epoch=s.inputEpoch;next.epochCompletedNs=s.inputCompletedNs;m=next;
}
static inline SweetBattInputPermit sb_input_permit(const SweetBattInputSession &m,
 const SweetBattSnapshot &s,const SweetBattInputVoltage &v,uint64_t now)
{
 SweetBattInputPermit p={};
 if (!sb_input_source_matches(m,s) || !m.epoch || m.epoch==~0ULL ||
     m.epoch!=s.inputEpoch || !v.valid || v.epoch!=m.epoch || !v.sequence ||
     !v.startedNs || v.startedNs<m.epochCompletedNs ||
     v.completedNs<v.startedNs || now<v.completedNs ||
     now-v.startedNs>=3000000000ULL || v.microvolts<4500000 ||
     v.microvolts>5500000) return p;
 p.valid=true;p.kind=m.kind;p.maxMa=m.maxMa;p.vbusMv=v.microvolts/1000;
 p.session=m.session;p.epoch=m.epoch;p.voltageSequence=v.sequence;
 p.voltageStartedNs=v.startedNs;p.validUntilNs=sb_evidence_deadline(v.startedNs);
 return p;
}
static inline bool sb_input_permit_valid(const SweetBattInputPermit &p,
 const SweetBattSnapshot &s,uint64_t now)
{
 SweetBattSourceType type=sb_source_type(s);
 return p.valid && (p.kind==SB_INPUT_CC || p.kind==SB_INPUT_BC12) && p.maxMa &&
  p.session && p.session==s.usbSession && p.session!=~0ULL &&
  p.epoch && p.epoch==s.inputEpoch && p.epoch!=~0ULL &&
  !sb_cc_status(s).reasons && s.apsdCoherent &&
  (s.valid&sb_input_mask())==sb_input_mask() &&
  !(s.raw[SB_APSD]&2) && type!=SB_SOURCE_QC2 && type!=SB_SOURCE_QC3 &&
  (!(s.raw[SB_APSD]&1) || type!=SB_SOURCE_UNKNOWN) &&
  (p.kind==SB_INPUT_CC ? (s.ccFromEvent && s.ccSession==p.session &&
    sb_cc_status(s).advertisedMa==p.maxMa) :
   (s.apsdFromSource && s.apsdSession==p.session && (s.raw[SB_APSD_OPTIONS]&8) &&
    (type==SB_SOURCE_DCP || type==SB_SOURCE_CDP) && p.maxMa==500)) &&
  p.voltageSequence && p.voltageStartedNs && p.vbusMv>=4500 && p.vbusMv<=5500 &&
  p.voltageStartedNs>=s.inputCompletedNs && now>=p.voltageStartedNs &&
  p.validUntilNs==sb_evidence_deadline(p.voltageStartedNs) && now<p.validUntilNs;
}
/* Diagnostic match only. Both data and metadata came from the same grant;
 * this is LG's prefix convention, not a full 416-byte profile verification. */
static inline unsigned sb_profile_match(const SweetBattSnapshot &s)
{
 if (!s.profilePrefixValid || !s.profileValid || !s.nominalValid || !(s.integrity&1)) return 0;
 unsigned flags=s.integrity&~1u;
 if (flags!=8 && flags!=2 && flags!=6 && flags!=12 && flags!=18 && flags!=22 && flags!=28) return 0;
 static const uint8_t tocad[24]={0x09,0x00,0xff,0xe3,0xf1,0x07,0x96,0xda,
  0xb3,0xd5,0x00,0x00,0xa1,0xbd,0xa3,0x8a,0xf8,0x87,0x84,0x92,0x9e,0x94,0x23,0x80};
 static const uint8_t lgc[24]={0x09,0x00,0xdf,0xe3,0x8c,0xdc,0x11,0xe3,
  0xa8,0xdc,0x00,0x00,0xca,0xbc,0x3e,0x8b,0xf9,0x87,0x84,0x80,0xb5,0x8d,0x1f,0x80};
 bool t=s.nominalMah==4027, l=s.nominalMah==4073;
 for (unsigned i=0;i<24;++i) { t=t && s.profilePrefix[i]==tocad[i];l=l && s.profilePrefix[i]==lgc[i]; }
 return t?1:l?2:0;
}


/* Board identity is a bounded join of fresh ADC BAT_ID and the fixed FG
 * profile tuple. LG's integer resistor formula and nearest 15% range match;
 * never use the Android default-profile fallback as an identity proof. */
struct SweetBattIdentity {
 bool valid;
 unsigned profile, ohms, adcToken, maxFccMa, maxFvMv;
 uint64_t validUntilNs;
};
static inline SweetBattIdentity sb_battery_identity(const SweetBattSnapshot &s,
 bool measured, unsigned microvolts, unsigned token, uint64_t idStarted,
 uint64_t idCompleted, uint64_t now)
{
 SweetBattIdentity id={};
 if (!measured || !token || !s.presentValid || !s.present || !s.profilePrefixValid ||
     !s.profileStartedNs || s.profileCompletedNs<s.profileStartedNs ||
     now<s.profileCompletedNs || now-s.profileStartedNs>=60000000000ULL ||
     !idStarted || idCompleted<idStarted || now<idCompleted || now-idStarted>=60000000000ULL ||
     microvolts<1000 || microvolts>=1825000) return id;
 unsigned mv=microvolts/1000;
 unsigned divisor=1825000/mv;
 if (divisor<=1000) return id;
 id.ohms=100000000/(divisor-1000);
 unsigned kohm=id.ohms/1000;
 const unsigned nominal[]={91,12};unsigned best=~0u,profile=0;
 for(unsigned i=0;i<2;++i) {
  unsigned delta=kohm>nominal[i]?kohm-nominal[i]:nominal[i]-kohm;
  if(delta<=nominal[i]*15/100 && delta<best) {best=delta;profile=i+1;}
 }
 if (!profile || sb_profile_match(s)!=profile) return {};
 id.valid=true;id.profile=profile;id.adcToken=token;
 id.maxFccMa=2800;id.maxFvMv=4430; /* MH2LM board supplement, not 3300mA profile. */
 uint64_t start=idStarted<s.profileStartedNs?idStarted:s.profileStartedNs;
 id.validUntilNs=start>~0ULL-60000000000ULL?~0ULL:start+60000000000ULL;
 return id;
}

/* Identity can now be joined when diagnostics request it; external parallel
 * isolation and managed-watchdog authorization still block hardware enable.
 * Input is an explicit source/ADC permit; raw register values alone are not
 * authorization. Synthetic evidence exists only in host tests. */
struct SweetBattLimits {
 bool batteryVerified, parallelVerified;
 unsigned maxFccMa, maxFvMv;
 uint64_t verifiedNs;
 SweetBattInputPermit input;
 uint64_t batteryUntilNs; /* earliest BAT_ID/profile expiry, not policy-refresh time */
};
enum SweetBattReason : uint32_t {
 SB_R_TELEMETRY=1u<<0, SB_R_STALE=1u<<1, SB_R_NO_BATTERY=1u<<2,
 SB_R_NO_INPUT=1u<<3, SB_R_TEMPERATURE=1u<<4, SB_R_HARDWARE=1u<<5,
 SB_R_BATTERY_ID=1u<<6, SB_R_PARALLEL=1u<<7, SB_R_INPUT=1u<<8,
 SB_R_WATCHDOG=1u<<9, SB_R_LIMITS=1u<<10, SB_R_EXPIRED=1u<<11,
 SB_R_TRANSPORT=1u<<12, SB_R_STOPPED=1u<<13
};
enum SweetBattState { SB_WAITING, SB_NO_INPUT, SB_PREPARE, SB_CHARGING,
 SB_THERMAL_HOLD, SB_FULL, SB_FAULT, SB_STOPPED };
struct SweetBattDecision {
 uint32_t reasons;
 unsigned fccMa, fvMv, iclMa;
 bool allow;
 SweetBattState state;
 uint64_t validUntilNs;
};
struct SweetBattController {
 bool owned, enabled, fault, stopped, thermalHold, stopConfirmed;
 uint64_t deadlineNs;
 int lastError;
 uint32_t faultReasons;
 SweetBattState state;
};
static inline SweetBattDecision
sb_decide(SweetBattController &c, const SweetBattSnapshot &s,
 const SweetBattLimits &limits, uint64_t now)
{
 SweetBattDecision d = {};
 d.state=SB_WAITING;
 const uint64_t needed=(1ULL<<SB_STAGE)|(1ULL<<SB_ERROR)|(1ULL<<SB_THERMAL)|
  (1ULL<<SB_PATH)|(1ULL<<SB_USB)|(1ULL<<SB_ENABLE)|(1ULL<<SB_PAUSE)|
  (1ULL<<SB_WDOG)|(1ULL<<SB_WDOG_TIME)|
  (1ULL<<SB_ICL)|(1ULL<<SB_FCC)|(1ULL<<SB_FV)|(1ULL<<SB_SUSPEND)|
  (1ULL<<SB_MODE)|(1ULL<<SB_LOAD)|(1ULL<<SB_OVERRIDE);
 if ((s.valid&needed)!=needed || !s.voltageValid || !s.currentValid ||
     !s.tempValid || !s.presentValid || !s.socValid) d.reasons|=SB_R_TELEMETRY;
 /* Validate start-to-now, not just time since the final register. */
 if (!s.completedNs || now<s.startedNs || now<s.completedNs ||
     now-s.startedNs>3000000000ULL) d.reasons|=SB_R_STALE;
 if (!s.presentValid || !s.present) d.reasons|=SB_R_NO_BATTERY;
 if (!sb_valid(s,SB_USB) || !(s.raw[SB_USB]&16)) d.reasons|=SB_R_NO_INPUT;
 /* Initial narrow 10..40 C band, resume only within 12..38 C. Missing temp
  * never clears a hold. This is a conservative policy, not LG tempcomp. */
 if (!s.tempValid || s.tempCenti<1000 || s.tempCenti>4000) c.thermalHold=true;
 else if (s.tempCenti>=1200 && s.tempCenti<=3800) c.thermalHold=false;
 if (c.thermalHold) d.reasons|=SB_R_TEMPERATURE;
 if ((sb_valid(s,SB_ERROR) && (s.raw[SB_ERROR]&2)) ||
     (sb_valid(s,SB_THERMAL) && (s.raw[SB_THERMAL]&15)) ||
     (c.enabled && (!(s.raw[SB_ENABLE]&1) || (s.raw[SB_PAUSE]&1))) ||
     s.mv>4450 || (s.currentValid && s.ma>600)) d.reasons|=SB_R_HARDWARE;
 if (!limits.batteryVerified || !limits.batteryUntilNs || now>=limits.batteryUntilNs ||
     !s.profileValid || !(s.integrity&1)) d.reasons|=SB_R_BATTERY_ID;
 if (!limits.parallelVerified) d.reasons|=SB_R_PARALLEL;
 const SweetBattInputPermit &input=limits.input;
 if (!sb_input_permit_valid(input,s,now) ||
     (sb_valid(s,SB_USB) && (s.raw[SB_USB]&16) &&
      (!(s.raw[SB_PATH]&1) || !(s.raw[SB_PATH]&16)))) d.reasons|=SB_R_INPUT;
 /* First implementation supports only a proven watchdog-off session. Refuse
  * enabled/plugin watchdog rather than pet an unverified timing contract. */
 if (!sb_valid(s,SB_WDOG) || (s.raw[SB_WDOG]&0xc2)) d.reasons|=SB_R_WATCHDOG;
 d.fccMa=limits.maxFccMa<500?limits.maxFccMa:500;
 d.iclMa=input.maxMa<500?input.maxMa:500;
 d.fvMv=limits.maxFvMv<4200?limits.maxFvMv:4200;
 /* Quantize downward, never silently round above the approved bound. */
 d.fccMa=d.fccMa/50*50;d.iclMa=d.iclMa/50*50;
 d.fvMv=d.fvMv>=3600?3600+(d.fvMv-3600)/10*10:0;
 if (!limits.verifiedNs || now<limits.verifiedNs || now-limits.verifiedNs>3000000000ULL ||
     !limits.batteryUntilNs || now>=limits.batteryUntilNs ||
     !d.fccMa || !d.iclMa || !d.fvMv || limits.maxFvMv>4430 ||
     limits.maxFccMa>2800) d.reasons|=SB_R_LIMITS; /* LG board supplement */
 /* A revoked permit has no approved ICL. Disable for the input hold without
  * misdiagnosing the previous programmed limit as a permanent HW fault. */
 if (c.enabled && !(d.reasons&(SB_R_INPUT|SB_R_LIMITS)) && (s.raw[SB_FCC]*50u>d.fccMa ||
     3600u+s.raw[SB_FV]*10u>d.fvMv || s.raw[SB_ICL]*50u>d.iclMa)) d.reasons|=SB_R_HARDWARE;
 if (c.owned && now>=c.deadlineNs) d.reasons|=SB_R_EXPIRED;
 if (c.fault) d.reasons|=c.faultReasons?c.faultReasons:SB_R_TRANSPORT;
 if (c.stopped) d.reasons|=SB_R_STOPPED;
 d.validUntilNs=sb_evidence_deadline(s.startedNs);
 if (sb_evidence_deadline(limits.verifiedNs)<d.validUntilNs) d.validUntilNs=sb_evidence_deadline(limits.verifiedNs);
 if (input.validUntilNs<d.validUntilNs) d.validUntilNs=input.validUntilNs;
 if (limits.batteryUntilNs<d.validUntilNs) d.validUntilNs=limits.batteryUntilNs;
 d.allow=d.reasons==0;
 if (d.reasons&(SB_R_TRANSPORT|SB_R_HARDWARE)) d.state=SB_FAULT;
 else if (d.reasons&(SB_R_STOPPED|SB_R_EXPIRED)) d.state=SB_STOPPED;
 else if (d.reasons&SB_R_TEMPERATURE) d.state=SB_THERMAL_HOLD;
 else if (d.reasons&SB_R_NO_INPUT) d.state=SB_NO_INPUT;
 else if (d.allow && s.percent>=99 && (s.raw[SB_STAGE]&7)==5) {
  d.allow=false;d.state=SB_FULL;
 } else if (d.allow) d.state=c.enabled?SB_CHARGING:SB_PREPARE;
 return d;
}

typedef uint64_t (*SweetBattClock)(void *);
typedef int (*SweetBattUpdate)(void *,enum sweet_charger_operation,unsigned,uint64_t,unsigned *);
static inline bool sb_stop(SweetBattController &c, SweetBattUpdate update, void *ctx)
{
 if (!c.owned) { return true; }
 c.stopConfirmed=false;
 unsigned actual=0;
 int rc=update(ctx,SWEET_CHARGER_ENABLE,0,0,&actual);
 if (rc || (actual!=0) || !c.fault) c.lastError=rc?rc:((actual!=0)?-12:0);
 if (rc || (actual!=0)) { c.fault=true;c.state=SB_FAULT;return false; }
 c.enabled=false;c.stopConfirmed=true;
 return true;
}

/* Called only on the battery loop. dry-run never acquires control or writes.
 * No automatic restart after a fault, expiration or explicit service stop.
 * A transient thermal/input hold may resume after a newly valid decision. */
static inline bool sb_apply(SweetBattController &c, const SweetBattDecision &d,
 uint64_t now, bool dryRun, SweetBattUpdate update, void *ctx, SweetBattClock clock)
{
 if (dryRun) { return true; }
 if (!d.allow) {
  if (d.reasons&SB_R_HARDWARE) { c.fault=true;c.faultReasons|=SB_R_HARDWARE; }
  if (d.reasons&SB_R_EXPIRED) c.stopped=true;
  bool ok=sb_stop(c,update,ctx);c.state=ok?d.state:SB_FAULT;return ok;
 }
 /* Defense in depth against callers passing a stale/contradictory decision. */
 if (!clock || !d.validUntilNs || now>=d.validUntilNs || d.reasons || c.stopped || c.fault ||
     (c.owned && now>=c.deadlineNs) || !d.fccMa || d.fccMa>500 ||
     !d.iclMa || d.iclMa>500 || d.fvMv<3600 || d.fvMv>4200) {
  c.stopped=true;(void)sb_stop(c,update,ctx);return false;
 }
 if (c.enabled) { c.state=SB_CHARGING;return true; }
 /* Ownership starts at the first attempted disable, including ambiguous error.
  * Disable inherited charging first, then configure; enable is always last. */
 if (!c.owned) { c.owned=true;c.deadlineNs=now+60000000000ULL; }
 if (!sb_stop(c,update,ctx)) return false;
 const enum sweet_charger_operation operations[]={SWEET_CHARGER_INPUT_SUSPEND,
  SWEET_CHARGER_INPUT_LIMIT_MA,SWEET_CHARGER_SOFTWARE_INPUT_LIMIT,
  SWEET_CHARGER_FAST_CURRENT_MA,SWEET_CHARGER_FLOAT_VOLTAGE_MV,
  SWEET_CHARGER_PAUSE,SWEET_CHARGER_INPUT_SUSPEND,SWEET_CHARGER_ENABLE};
 const unsigned value[]={1,d.iclMa,1,d.fccMa,d.fvMv,0,0,1};
 for (unsigned i=0;i<sizeof(operations)/sizeof(operations[0]);++i) {
  uint64_t current=clock(ctx);
  if (current>=c.deadlineNs || current>=d.validUntilNs || current<now) {
   c.stopped=true;(void)sb_stop(c,update,ctx);c.state=SB_STOPPED;return false;
  }
  if (operations[i]==SWEET_CHARGER_ENABLE && value[i]) c.stopConfirmed=false;
  uint64_t deadline=c.deadlineNs<d.validUntilNs?c.deadlineNs:d.validUntilNs;
  unsigned actual=0;int rc=update(ctx,operations[i],value[i],deadline,&actual);
  if (rc==SWEET_CHARGER_EXPIRED) {
   c.stopped=true;(void)sb_stop(c,update,ctx);c.state=c.fault?SB_FAULT:SB_STOPPED;return false;
  }
  if (rc || actual!=value[i]) {
   c.fault=true;c.lastError=rc?rc:-12;
   (void)sb_stop(c,update,ctx);c.state=SB_FAULT;return false;
  }
 }
 c.enabled=true;
 if (clock(ctx)>=c.deadlineNs || clock(ctx)>=d.validUntilNs) {
  c.stopped=true;(void)sb_stop(c,update,ctx);c.state=c.fault?SB_FAULT:SB_STOPPED;return false;
 }
 c.state=SB_CHARGING;return true;
}
#endif
