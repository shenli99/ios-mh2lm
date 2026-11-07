#!/usr/bin/env python3
"""Run the actual runtime3 basic charge controller on a host.
Usage: test-mh2lm-charge-basic.py EXTRACTED_XNU
Models PMIC register effects of semantic commands; does not prove charging.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
root = Path(sys.argv[1])
dev = root/'bsd/dev'
for name in ['sweet_charge_basic.h', 'sweet_battery_runtime.h', 'sweet_charger.h', 'sweet_spmi.h']:
    assert (dev/name).is_file(), name
test = r'''
#define ARM64_BOARD_CONFIG_MH2LM 1
#include <cassert>
#include <cstdio>
#include <vector>
#include "sweet_spmi.h"
#include "sweet_charge_basic.h"
struct Op { sweet_charger_operation op; unsigned value; };
static std::vector<Op> ops;
static SweetBattSnapshot hw;           /* what the next sample would read */
static int failAt=-1, calls=0;
static bool clearsEnable=false;
static int cmd(void *, sweet_charger_operation op, unsigned value, unsigned *out)
{
    ops.push_back({op,value});
    if (calls++==failAt) return SWEET_SPMI_TIMEOUT;
    switch (op) {
    case SWEET_CHARGER_ENABLE: hw.raw[SB_ENABLE]=clearsEnable?0:value; *out=hw.raw[SB_ENABLE]; break;
    case SWEET_CHARGER_BOARD_FAST_CURRENT_MA: hw.raw[SB_FCC]=value/50; *out=value; break;
    case SWEET_CHARGER_BOARD_FLOAT_VOLTAGE_MV: hw.raw[SB_FV]=(value-3600)/10; *out=value; break;
    default: *out=value; break;
    }
    return 0;
}
static const uint64_t S=1000000000ULL;
static SweetBattSnapshot good(uint64_t sec, int temp=2850, unsigned mv=3900)
{
    SweetBattSnapshot s=hw;
    s.valid=(1ULL<<SB_REG_COUNT)-1;s.startedNs=s.completedNs=sec*S;
    s.voltageValid=s.tempValid=s.presentValid=s.present=true;
    s.mv=mv;s.tempCenti=temp;s.raw[SB_USB]=0x10;s.raw[SB_ERROR]=0;s.raw[SB_THERMAL]=0;
    return s;
}
static unsigned count(sweet_charger_operation op)
{ unsigned n=0; for (auto &o:ops) n+=o.op==op; return n; }
static SweetChargeBasicStep step(SweetChargeBasic &c, const SweetBattSnapshot &s, bool bark=false, bool stop=false)
{ return scb_step(c,s,s.startedNs,stop,bark,cmd,nullptr); }
static void reset() { hw={};ops.clear();calls=0;failAt=-1;clearsEnable=false; }
int main()
{
    /* No input, inherited enable0: no command at all. */
    { reset();SweetChargeBasic c={};SweetBattSnapshot s=good(1);s.raw[SB_USB]=0;
      assert(step(c,s)==SCB_IDLE && ops.empty() && c.reasons==SCB_R_NO_INPUT && c.health==SCB_H_GOOD); }
    /* Inherited enable1 without input is cleared once. */
    { reset();hw.raw[SB_ENABLE]=1;SweetChargeBasic c={};SweetBattSnapshot s=good(1);s.raw[SB_USB]=0;
      assert(step(c,s)==SCB_DISABLED && ops.size()==1 && hw.raw[SB_ENABLE]==0); }
    /* Plug-in IRQ: LG order, watchdog configured and petted before limits, enable last. */
    reset();SweetChargeBasic c={};
    assert(step(c,good(1))==SCB_ENABLED && c.enabled && c.enables==1);
    {const sweet_charger_operation want[]={SWEET_CHARGER_ENABLE,SWEET_CHARGER_PARALLEL_INHIBIT,
      SWEET_CHARGER_WATCHDOG_PREPARE,SWEET_CHARGER_WATCHDOG_PET,SWEET_CHARGER_BOARD_FAST_CURRENT_MA,
      SWEET_CHARGER_BOARD_FLOAT_VOLTAGE_MV,SWEET_CHARGER_PAUSE,SWEET_CHARGER_INPUT_SUSPEND,SWEET_CHARGER_ENABLE};
     const unsigned value[]={0,1,1,1,SCB_FCC_MA,SCB_FV_MV,0,0,1};
     assert(ops.size()==9);
     for (unsigned i=0;i<9;++i) assert(ops[i].op==want[i] && ops[i].value==value[i]);}
    assert(hw.raw[SB_FCC]*50u==SCB_FCC_MA && 3600u+hw.raw[SB_FV]*10u==SCB_FV_MV);
    assert(scb_period_ms(c)==SCB_PERIOD_GOOD_MS);
    /* Steady state: no writes and no software-timed pet. */
    ops.clear();
    for (uint64_t t=2;t<=200;++t) assert(step(c,good(t))==SCB_IDLE);
    assert(ops.empty() && count(SWEET_CHARGER_CHARGING_PET)==0 && c.pets==0);
    /* Bark IRQ: exactly one pet, nothing else. */
    assert(step(c,good(201),true)==SCB_IDLE && ops.size()==1 && ops[0].op==SWEET_CHARGER_CHARGING_PET && c.pets==1);
    /* A stale snapshot (started before our last write) is not an external change. */
    {SweetBattSnapshot s=good(1);s.raw[SB_ENABLE]=0;ops.clear();
     assert(step(c,s)==SCB_IDLE && ops.empty() && c.enabled && c.failures==0);}
    /* Unplug disables; replug re-enables without ADC/identity evidence. */
    ops.clear();{SweetBattSnapshot s=good(202);s.raw[SB_USB]=0;
    assert(step(c,s)==SCB_DISABLED && !c.enabled && hw.raw[SB_ENABLE]==0);}
    assert(step(c,good(203))==SCB_ENABLED && c.enables==2);
    /* LG protection-battemp bands and hysteresis, re-voted while charging. */
    {uint64_t t=210;
     auto at=[&](int temp,unsigned mv,SweetChargeBasicStep want,unsigned fcc,unsigned fv,SweetChargeHealth h){
      ops.clear();SweetChargeBasicStep got=step(c,good(t++,temp,mv));
      if (got!=want || c.health!=h || (c.enabled && (c.fccMa!=fcc || c.fvMv!=fv))) {
       printf("temp=%d mv=%u got=%u want=%u health=%u fcc=%u fv=%u\n",temp,mv,got,want,c.health,c.fccMa,c.fvMv);
       assert(false);
      }
      if (c.enabled) assert(hw.raw[SB_FCC]*50u==fcc && 3600u+hw.raw[SB_FV]*10u==fv);};
     at(4600,3900,SCB_LIMITS,SCB_WARM_MA,SCB_WARM_FV_MV,SCB_H_WARM);
     assert(ops.size()==2 && c.enabled && scb_period_ms(c)==SCB_PERIOD_WARNING_MS);
     at(4600,3900,SCB_IDLE,SCB_WARM_MA,SCB_WARM_FV_MV,SCB_H_WARM);
     at(4400,3900,SCB_IDLE,SCB_WARM_MA,SCB_WARM_FV_MV,SCB_H_WARM);   /* down to good at 43C */
     at(4200,3900,SCB_LIMITS,SCB_FCC_MA,SCB_FV_MV,SCB_H_GOOD);
     at(5600,3900,SCB_DISABLED,0,0,SCB_H_HOT);
     assert(!c.enabled && (c.reasons&SCB_R_TEMPERATURE) && scb_period_ms(c)==SCB_PERIOD_EMERGENCY_MS);
     at(5300,3900,SCB_IDLE,0,0,SCB_H_HOT);                           /* down to warm at 52C */
     assert(ops.empty());
     at(5100,3900,SCB_ENABLED,SCB_WARM_MA,SCB_WARM_FV_MV,SCB_H_WARM);
     at(2850,3900,SCB_LIMITS,SCB_FCC_MA,SCB_FV_MV,SCB_H_GOOD);
     at(1100,3900,SCB_LIMITS,SCB_CHILLY_MA,SCB_FV_MV,SCB_H_GOOD);     /* chilly 0..15C */
     assert(c.chilly && scb_period_ms(c)==SCB_PERIOD_WARNING_MS);
     at(1100,4050,SCB_LIMITS,SCB_CHILLY_ALERT_MA,SCB_FV_MV,SCB_H_GOOD);
     at(900,3900,SCB_LIMITS,SCB_COOL_MA,SCB_FV_MV,SCB_H_COOL);
     at(1150,3900,SCB_IDLE,SCB_COOL_MA,SCB_FV_MV,SCB_H_COOL);         /* up to good at 12C */
     at(1150,4050,SCB_LIMITS,SCB_COOL_ALERT_MA,SCB_FV_MV,SCB_H_COOL);
     at(1250,3900,SCB_LIMITS,SCB_CHILLY_MA,SCB_FV_MV,SCB_H_GOOD);
     at(-50,3900,SCB_DISABLED,0,0,SCB_H_COLD);
     assert(!c.chilly && scb_period_ms(c)==SCB_PERIOD_EMERGENCY_MS);
     at(20,3900,SCB_IDLE,0,0,SCB_H_COLD);                             /* up to cool at 3C */
     at(2850,3900,SCB_ENABLED,SCB_FCC_MA,SCB_FV_MV,SCB_H_GOOD);
     assert(scb_period_ms(c)==SCB_PERIOD_GOOD_MS);}
    /* Unknown temperature from boot never enables. */
    {reset();SweetChargeBasic d={};SweetBattSnapshot s=good(1);s.tempValid=false;
     assert(step(d,s)==SCB_IDLE && ops.empty() && (d.reasons&SCB_R_TEMPERATURE) && (d.reasons&SCB_R_TELEMETRY));
     assert(scb_period_ms(d)==SCB_PERIOD_EMERGENCY_MS);}
    /* Over-voltage battery, PMIC BAT_OV, hard temp limit, USBIN OV, stopping each disable. */
    {reset();SweetChargeBasic d={};assert(step(d,good(1))==SCB_ENABLED);
     for (int k=0;k<5;++k) {
        uint64_t t=10+2*k;SweetBattSnapshot s=good(t);bool stop=false;
        if (k==0) s.mv=4460;
        if (k==1) s.raw[SB_ERROR]=2;
        if (k==2) s.raw[SB_THERMAL]=8;
        if (k==3) s.raw[SB_USB]=0x18;
        if (k==4) stop=true;
        assert(step(d,s,false,stop)==SCB_DISABLED && hw.raw[SB_ENABLE]==0 && d.failures==0);
        assert(step(d,good(t+1))==SCB_ENABLED);
     }
     /* Missing telemetry while enabled fails safe and re-checks at 10s. */
     SweetBattSnapshot s=good(30);s.valid&=~(1ULL<<SB_USB);
     assert(step(d,s)==SCB_DISABLED && (d.reasons&SCB_R_TELEMETRY));
     assert(scb_period_ms(d)==SCB_PERIOD_EMERGENCY_MS);
     /* The bark is still serviced while stopping (LG pets unconditionally). */
     ops.clear();assert(step(d,good(31),true,true)==SCB_IDLE && ops.size()==1 && d.pets==1);}
    /* A command error disables and backs off 5s, then retries. */
    {reset();SweetChargeBasic d={};failAt=3;
     assert(step(d,good(100))==SCB_ERROR && !d.enabled && d.failures==1);
     assert(ops.back().op==SWEET_CHARGER_ENABLE && !ops.back().value && hw.raw[SB_ENABLE]==0);
     failAt=-1;assert(step(d,good(101))==SCB_IDLE && (d.reasons&SCB_R_BACKOFF));
     assert(scb_period_ms(d)==SCB_PERIOD_EMERGENCY_MS);
     assert(step(d,good(105))==SCB_ENABLED && d.retryNs==0);
     /* Stable 60s clears the failure count. */
     assert(step(d,good(164))==SCB_IDLE && d.failures==1);
     assert(step(d,good(165))==SCB_IDLE && d.enabled && d.failures==0);}
    /* Bark pet error while charging is a fault; while idle it is only counted. */
    {reset();SweetChargeBasic d={};
     assert(step(d,good(200))==SCB_ENABLED);
     failAt=calls;assert(step(d,good(211),true)==SCB_ERROR && !d.enabled && d.petErrors==1);
     assert(ops.back().op==SWEET_CHARGER_ENABLE && !ops.back().value);
     reset();SweetChargeBasic e={};SweetBattSnapshot s=good(1);s.raw[SB_USB]=0;failAt=0;
     assert(step(e,s,true)==SCB_IDLE && e.petErrors==1 && e.failures==0);}
    /* Another writer clearing enable or changing FCC: bounded fight, then permanent stop. */
    {reset();SweetChargeBasic d={};
     assert(step(d,good(300))==SCB_ENABLED);
     hw.raw[SB_FCC]=56;assert(step(d,good(301))==SCB_ENABLED && d.failures==1);
     uint64_t t=302;
     for (int i=0;i<40 && !d.failed;++i,++t) { hw.raw[SB_ENABLE]=0; (void)step(d,good(t)); }
     assert(d.failed && !d.enabled && hw.raw[SB_ENABLE]==0 && t==306);
     ops.clear();assert(step(d,good(t+10))==SCB_IDLE && ops.empty() && (d.reasons&SCB_R_FAILED));
     assert(step(d,good(t+11),true)==SCB_IDLE && ops.size()==1 && ops[0].op==SWEET_CHARGER_CHARGING_PET);}
    /* Enable that does not read back as set is a failure, not a success. */
    {reset();SweetChargeBasic d={};clearsEnable=true;
     assert(step(d,good(400))==SCB_ERROR && !d.enabled);}
    puts("PASS: runtime3 IRQ basic charge: LG order, bark-only pet, LG battemp bands/hysteresis/cadence, OV/BAT_OV/hard-temp/USBIN-OV/stop, telemetry loss, backoff/stable reset, bounded external-writer fight");
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory)/'test.cpp'; binary = Path(directory)/'test'
    cpp.write_text(test)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(dev), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
