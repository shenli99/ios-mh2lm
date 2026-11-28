/* Adapted from this project's sweet_rpmh.c (command DB/pre-v3 TCS layout).
 * Only this service writes MH2LM apps-DRV. Other-board legacy code is excluded
 * on MH2LM. ACTIVE completion uses IRQ/event/workloop; timers bound posted
 * readback, GIC drain and watchdog phases without polling completion. */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "SweetRPMh.h"
#include "SweetAppsDoor.h"
#include "SweetAppsLine.h"
#include "SweetPanelPins.h"
#include "SweetTouchPanel.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"

OSDefineMetaClassAndStructors(SweetRPMh,IOService);
OSDefineMetaClassAndStructors(SweetRPMhClient,OSObject);
/* Ownership latch only, never an instance pointer/consumer bridge. A service
 * that wrote this boot cannot be replaced with an empty software vote ledger.
 * 0=unclaimed, 1=live instance, 2=retired after writes/uncertainty. */
static unsigned rpmhOwner;
static const char *const railNames[]={"ldoa14","ldoa17","ldoc3","ldoa5"};
static const SweetRailInfo railLimits[]={
 /* MH2LM LG DTS: L3/L5 also feed UFS and USB PHYs.
  * These clients have no provider votes yet. Their inherited state cannot
  * be represented by an empty XNU ledger or a fabricated baseline lease. */
 {1800,1880,10000,false,false},{2856,3008,1,false,false},
 {1200,1200,1,false,true},{880,880,1,false,true}};
static const char *const displaySupplies[]={"cx.lvl","mx.lvl","mmcx.lvl"};
static const unsigned displayMinimums[]={0,64,128,256,384};
/* Resource policy is independent of raw command DB width. ACV is an enable
 * vote (LG BCM_TCS_CMD_ACV_APPS encoding X=0/Y=8), not a bandwidth ratio.
 * Apply the same explicit mask to display ACV; never fabricate a DB width. */
static const struct { const char *name; unsigned enableMask; } displayBCMs[]={
 {"CN0",0},{"MM1",0},{"MM0",0},{"SH0",0},{"MC0",0},{"ACV",0x8}};
enum { Stride=0x2a0, CommandStride=0x14, IRQEnable=0, IRQStatus=4, IRQClear=8,
 Wait=0x10, Control=0x14, Status=0x18, Enable=0x1c,
 Message=0x30, Address=0x34, Data=0x38, CommandStatus=0x3c,
 AMC=1<<16, Trigger=1<<24, Issued=1<<8, Complete=1<<16,
 WriteACK=8|(1<<8)|(1<<16), DBHeader=144, DBEntry=24 };
struct RPMhRequest {
 unsigned op, rail; uint64_t client, token; SweetRailVote vote;
 uint64_t *idOut; SweetRailInfo *info; SweetRPMhStatus *status;
 uint64_t *previous, *aggregate;
 SweetDisplayPowerInfo *displayInfo; SweetDisplayPowerStatus *displayStatus;
 SweetBCMInfo *bcmInfo; uint64_t bandwidth; SweetRegisterBandwidthStatus *registerStatus;
 const SweetPanelSupplyFence *panelFence;
};
enum { Attach, Detach, Info, Vote, Query, Shutdown, DisplayInfo, DisplayVote, DisplayQuery,
 BCMInfo, RegisterVote, RegisterQuery, PanelVote, PanelCancel, Cancel, Withdraw };

/* C3-06: the request names where a withdrawal reports what it did. */

bool SweetBCMInfo::scale(uint64_t rate,unsigned busWidth,unsigned ports,unsigned *value) const
{
 if (enableMask || !unit || !width || !busWidth || busWidth>32 || !ports || ports>4) return false;
 uint64_t divisor=(uint64_t)busWidth*ports*unit;
 /* Bound before multiplying; all valid products fit 64 bits. Round upward,
  * never silently truncate a positive vote or clamp overflow to 14 bits. */
 if (rate>(0x3fffULL*divisor)/width) return false;
 uint64_t numerator=rate*width;
 *value=(unsigned)(numerator/divisor+(numerator%divisor!=0)); return true;
}

bool SweetBCMInfo::encode(uint64_t average,uint64_t peak,unsigned busWidth,unsigned ports,uint32_t *value) const
{
 if (!value || !busWidth || busWidth>32 || !ports || ports>4) return false;
 unsigned x=0,y=0;
 if (enableMask) {
  if (enableMask>0x3fff) return false;
  /* Any EBI demand enables ACV. Last DATA release sends zero even while a
   * config-only lease holds CN0. Magnitude/width/unit do not scale a mask. */
  if (average || peak) y=enableMask;
 } else if (!scale(average,busWidth,ports,&x) || !scale(peak,busWidth,1,&y)) return false;
 *value=(x||y ? 1u<<29 : 0u)|(x<<14)|y; return true;
}

uint32_t SweetRPMh::read(vm_offset_t base,uint32_t off)
{
 uint32_t value=*(volatile uint32_t *)(base+off);
 __asm__ volatile("dmb oshld" ::: "memory");
 return value;
}
void SweetRPMh::write(vm_offset_t base,uint32_t off,uint32_t value)
{
 __asm__ volatile("dmb oshst" ::: "memory");
 *(volatile uint32_t *)(base+off)=value;
 __asm__ volatile("dsb osh" ::: "memory");
}
/* apps RSC: LG SM8150 DTS SPI5 => INTID37 (level high), GICv3.
 * C3-04 moved the dispatch lock and the "who owns the line" latch into
 * SweetAppsDoor.cpp, so both possible owners share one router: the platform
 * chain calls sweet_rpmh_irq() there and it hands INTID37 to the published
 * table. This service keeps only its own GIC statements. */
/* INTID37's GICD words have one definition for both possible owners
 * (SweetAppsLine.h). The statements below keep their historical spelling. */
enum { GICBit=kSweetAppsGICBit, GICEnable=kSweetAppsGICEnable,
 GICDisable=kSweetAppsGICDisable, GICPending=kSweetAppsGICPending,
 GICClearPending=kSweetAppsGICClearPending, GICActive=kSweetAppsGICActive,
 GICGroup=kSweetAppsGICGroup, GICConfig=kSweetAppsGICConfig,
 GICPriority=kSweetAppsGICPriority, GICRoute=kSweetAppsGICRoute };

bool SweetRPMh::setupIRQ(IOService *provider)
{
 /* Mode1 remains metadata-only with the original three-resource ABI. The
  * explicit extension is mandatory for mode2; never fall back to polling. */
 OSData *abi=OSDynamicCast(OSData,provider->getProperty("sweet,rpmh-irq-abi"));
 if (!abi || abi->getLength()!=4 || memcmp(abi->getBytesNoCopy(),"\1\0\0\0",4)) return false;
 OSData *intid=OSDynamicCast(OSData,provider->getProperty("sweet,rpmh-intid"));
 const uint32_t expected=37;
 if (!intid || intid->getLength()!=4 || memcmp(intid->getBytesNoCopy(),&expected,4)) return false;
 fIRQMap=provider->mapDeviceMemoryWithIndex(3);
 if (!fIRQMap || fIRQMap->getPhysicalAddress()!=0x17a00000 || fIRQMap->getLength()!=0x10000) return false;
 const vm_offset_t gic=fIRQMap->getVirtualAddress();
 if ((read(gic,0xffe8)&0xf0u)!=0x30u || ((read(gic,4)&31u)+1u)*32u<=37u ||
     !(read(gic,0)&2u) || ((read(gic,GICEnable)|read(gic,GICPending)|read(gic,GICActive))&GICBit)) return false;
 fIRQEvent=IOInterruptEventSource::interruptEventSource(this,irqAction);
 if (!fIRQEvent || fLoop->addEventSource(fIRQEvent)!=kIOReturnSuccess) return false;
 fIRQEventAdded=true;
 /* The line is claimed by publishing the table, and this is the only place it
  * happens: one owner per boot, and a refused publish is a refused start. A
  * boot that never takes the line (mode 1) publishes nothing, because there
  * would be no line to name. */
 if (!publishTable()) {
  SWEET_LOG_ERROR("SWEET RPMH: line table refused; another owner published first; no writes\n");
  return false;
 }
 fIRQClaimed=true;
 return true;
}

IOReturn SweetRPMh::prepareIRQ(void)
{
 const vm_offset_t gic=fIRQMap->getVirtualAddress();
 if (fIRQConfigured) {
  /* Already ours: still re-state the invariant before every trigger, because a
   * rewired distributor is exactly what "configured once" would hide. */
  if ((read(gic,GICConfig)&0xc00u) || (read(gic,GICPriority)&0xff00u)!=0xa000u ||
      read(gic,GICRoute) || read(gic,GICRoute+4)) return kIOReturnIOError;
  return admitIRQ(gic);
 }
 if (!fIRQTouched) {
  /* Fail read-only if another owner, pending source or unsupported geometry
   * exists. Do not acknowledge inherited peripheral/GIC pending state. */
  if ((read(gic,0xffe8)&0xf0u)!=0x30u || ((read(gic,4)&31u)+1u)*32u<=37u ||
      !(read(gic,0)&2u) || ((read(gic,GICEnable)|read(gic,GICPending)|read(gic,GICActive))&GICBit) ||
      tcsRead(IRQStatus)) return kIOReturnBusy;
  fIRQTouched=true; fEverWritten=true;
  write(gic,GICDisable,GICBit);
  write(gic,GICGroup,read(gic,GICGroup)|GICBit);
  write(gic,GICConfig,read(gic,GICConfig)&~0xc00u); /* INTID37 only, level */
  write(gic,GICPriority,(read(gic,GICPriority)&~0xff00u)|0xa000u);
  /* Aligned 64-bit IROUTER access, established boot CPU MPIDR0 route. */
  __asm__ volatile("dmb oshst" ::: "memory");
  *(volatile uint64_t *)(gic+GICRoute)=0;
  __asm__ volatile("dsb osh" ::: "memory");
  fIRQConfigureDeadline=nowNS()+1000000ULL;
 }
 /* RWP is an asynchronous timer phase, never a spin or delay under a lock. */
 if (read(gic,0)&(1u<<31))
  return nowNS()<fIRQConfigureDeadline ? kIOReturnNotReady : kIOReturnIOError;
 if ((read(gic,GICConfig)&0xc00u) || (read(gic,GICPriority)&0xff00u)!=0xa000u ||
     read(gic,GICRoute) || read(gic,GICRoute+4)) return kIOReturnIOError;
 if (!fIRQConfigured) {
  fIRQConfigured=true; __atomic_store_n(&fIRQOnline,1u,__ATOMIC_RELEASE);
  setProperty("SweetRPMhCompletionIRQ",37ULL,32);
  SWEET_LOG_INFO("SWEET RPMH IRQ: INTID37 configured; ACTIVE completion on provider workloop, no status polling\n");
 }
 return admitIRQ(gic);
}

/* An IRQ worker may beat the previous sleh EOI on another CPU. Wait via the
 * caller's timer while the owned line drains, never overwrite an active
 * generation. This belongs to the owner of the line, so the delegated mode
 * gets the same steps from the channel's trigger-ready instead. */
IOReturn SweetRPMh::admitIRQ(vm_offset_t gic)
{
 if (read(gic,GICActive)&GICBit) return kIOReturnNotReady;
 if (read(gic,GICPending)&GICBit) {
  if (tcsRead(IRQStatus)) return kIOReturnBusy;
  write(gic,GICClearPending,GICBit);
  if (read(gic,GICPending)&GICBit) return kIOReturnNotReady;
 }
 return kIOReturnSuccess;
}

bool SweetRPMh::handleIRQ(void)
{
 /* Dispatch lock held: event/map stay alive. No peripheral ACK, client gate,
  * payload scan, allocation or wait in the top half. sleh performs GIC EOI.
  * A delegated boot never mapped this window and never publishes this table,
  * so reaching here without a map would be a table/lifecycle bug: claim the
  * line anyway rather than letting it fall into the generic dispatcher. */
 if (!fIRQMap) return true;
 if (!__atomic_load_n(&fIRQOnline,__ATOMIC_ACQUIRE)) return true;
 write(fIRQMap->getVirtualAddress(),GICDisable,GICBit);
 __atomic_add_fetch(&fIRQEntries,1u,__ATOMIC_RELAXED);
 if (!__atomic_exchange_n(&fIRQArmed,0u,__ATOMIC_ACQ_REL)) return true;
 __atomic_store_n(&fIRQPosted,1u,__ATOMIC_RELEASE);
 fIRQEvent->interruptOccurred(NULL,NULL,0);
 return true;
}
void SweetRPMh::irqAction(OSObject *owner,IOInterruptEventSource *,int)
{
 SweetRPMh *self=(SweetRPMh *)owner;
 if (!__atomic_exchange_n(&self->fIRQPosted,0u,__ATOMIC_ACQ_REL) || !self->fAccepting) return;
 self->completionArrived();
}
/* The delegated mode reaches this through the channel's workloop (its top half
 * posts, this runs on a thread), so the body is shared and takes no lock of its
 * own: the caller owns the serialisation that matters. */
void SweetRPMh::completionArrived(void)
{
 SweetRPMh *self=this;
 if (!self->fBusy) { self->lateCompletion(); return; }
 if (!self->fExecuting || self->fOperationCount ||
     !(tcsRead(IRQStatus)&(1u<<self->fTCSIndex))) {
  SWEET_LOG_ERROR("SWEET RPMH IRQ: unexpected ACTIVE completion; source retained, no foreign ACK\n");
  self->quietIRQ(); self->fPoisoned=true;
  if (self->fBusy) self->finish(kIOReturnIOError,true);
  return;
 }
 self->fIRQObserved=true;
 self->advance();
}
void SweetRPMh::quietIRQ(void)
{
 __atomic_store_n(&fIRQArmed,0u,__ATOMIC_RELEASE);
 const SweetAppsDoor *door=executorDoor();
 if (door && door->quiet) { door->quiet(door->context,fIRQSourceMask); return; }
 /* No table at all: this boot owns nothing, so there is nothing to quiet. */
}
void SweetRPMh::stopIRQ(void)
{
 __atomic_store_n(&fIRQOnline,0u,__ATOMIC_RELEASE);
 /* Withdraw first: after this no store or IRQ can reach this service, so the
  * event source and the maps below are no longer reachable from interrupt
  * context. In the delegated mode there is nothing of ours to withdraw. */
 if (fTablePublished) { sweet_apps_door_withdraw(&fTable); fTablePublished=false; }
 fIRQClaimed=false;
 if (fIRQEventAdded) { fIRQEvent->disable(); fLoop->removeEventSource(fIRQEvent); fIRQEventAdded=false; }
}
void SweetRPMh::waitCompletion(void)
{
 uint64_t now=nowNS(),deadline=fDeadline;
 if (fArcRequest && fStageDeadline<deadline) deadline=fStageDeadline;
 fTimer->setTimeoutUS((unsigned)(deadline>now ? (deadline-now+999)/1000 : 1));
}

uint64_t SweetRPMh::nowNS(void)
{
 uint64_t ns=0; absolutetime_to_nanoseconds(mach_absolute_time(),&ns); return ns;
}
uint16_t SweetRPMh::db16(uint32_t off)
{ return (uint16_t)(read(fDB,off & ~3u) >> (8u*(off & 3u))); }
bool SweetRPMh::range(uint32_t off,uint32_t bytes)
{ return off<=fDBSize && bytes<=fDBSize-off; }

bool SweetRPMh::lookup(unsigned rail)
{
 uint32_t key[2]={0,0};
 for (unsigned i=0;i<8 && railNames[rail][i];++i)
  key[i/4]|=(uint32_t)(uint8_t)railNames[rail][i] << (8*(i%4));
 unsigned matches=0;
 for (unsigned i=0;i<8;++i) {
  uint32_t h=8+16*i; unsigned slave=db16(h);
  if (!slave) break;
  uint32_t entries=DBHeader+db16(h+2), aux=DBHeader+db16(h+4);
  unsigned count=db16(h+6);
  if (slave<3 || slave>5 || (entries & 3) || !range(entries,count*DBEntry) || !range(aux,0)) return false;
  for (unsigned j=0;j<count;++j) {
   uint32_t e=entries+DBEntry*j;
   if (read(fDB,e)!=key[0] || read(fDB,e+4)!=key[1]) continue;
   uint32_t address=read(fDB,e+16);
   if (slave!=4 || (address>>16)!=4 || (address & 15) || !range(aux+db16(e+22),db16(e+20))) return false;
   fAddresses[rail]=address; ++matches;
  }
 }
 if (matches!=1) return false;
 for (unsigned r=0;r<rail;++r) if (fAddresses[r]==fAddresses[rail]) return false;
 return true;
}

bool SweetRPMh::lookupArc(unsigned supply)
{
 uint32_t key[2]={0,0};
 for (unsigned i=0;i<8 && displaySupplies[supply][i];++i)
  key[i/4]|=(uint32_t)(uint8_t)displaySupplies[supply][i] << (8*(i%4));
 unsigned matches=0;
 for (unsigned i=0;i<8;++i) {
  uint32_t h=8+16*i; unsigned slave=db16(h);
  if (!slave) break;
  uint32_t entries=DBHeader+db16(h+2), aux=DBHeader+db16(h+4);
  unsigned count=db16(h+6);
  if (slave<3 || slave>5 || (entries&3) || !range(entries,count*DBEntry) || !range(aux,0)) return false;
  for (unsigned j=0;j<count;++j) {
   uint32_t e=entries+DBEntry*j;
   if (read(fDB,e)!=key[0] || read(fDB,e+4)!=key[1]) continue;
   uint32_t address=read(fDB,e+16), table=aux+db16(e+22);
   unsigned bytes=db16(e+20);
   if (slave!=3 || (address>>16)!=3 || (address&3) || (table&1) ||
       bytes<4 || bytes>32 || (bytes&1) || !range(table,bytes) || !range(table&~3u,((table&3)+bytes+3)&~3u)) return false;
   ArcResource resource={}; resource.address=address;
   bool padding=false;
   for (unsigned k=0;k<bytes/2;++k) {
    unsigned level=db16(table+2*k);
    if (!k) { if (level) return false; }
    else if (!level) padding=true;
    else if (padding || level<=resource.levels[k-1]) return false;
    if (!padding) resource.levels[resource.count++]=level;
   }
   if (resource.count<2) return false;
   fArc[supply]=resource; ++matches;
  }
 }
 if (matches!=1) return false;
 for (unsigned r=0;r<supply;++r) if (fArc[r].address==fArc[supply].address) return false;
 return true;
}

bool SweetRPMh::lookupBCM(unsigned bcm)
{
 unsigned bit=1u<<bcm;
 if (fBCMTried&bit) return (fBCMAvailable&bit)!=0;
 fBCMTried|=bit;
 const auto &resource=displayBCMs[bcm];
 const char *name=resource.name;
 uint32_t key[2]={};
 for (unsigned k=0;k<8 && name[k];++k)
  key[k/4]|=(uint32_t)(uint8_t)name[k] << (8*(k%4));
 unsigned matches=0; SweetBCMInfo found;
 /* One diagnostic per first lookup only. Log existing, range-checked reads;
  * never inspect rejected aux data or relax metadata admission to get pixels. */
 for (unsigned i=0;i<8;++i) {
  uint32_t h=8+16*i; unsigned slave=db16(h);
  if (!slave) break;
  uint32_t entries=DBHeader+db16(h+2), aux=DBHeader+db16(h+4);
  unsigned count=db16(h+6);
  if (slave<3 || slave>5 || (entries&3) || !range(entries,count*DBEntry) || !range(aux,0)) {
   SWEET_LOG_ERROR("SWEET RPMH BCM: name=%s gate=header index=%u slave=%u entries=%#x aux=%#x count=%u db-size=%#x; no votes\n",
    name,i,slave,entries,aux,count,fDBSize); return false;
  }
  for (unsigned j=0;j<count;++j) {
   uint32_t e=entries+j*DBEntry;
   if (read(fDB,e)!=key[0] || read(fDB,e+4)!=key[1]) continue;
   uint32_t table=aux+db16(e+22), address=read(fDB,e+16);
   if (slave!=5 || (address>>16)!=5 || (address&3)) {
    SWEET_LOG_ERROR("SWEET RPMH BCM: name=%s gate=address entry=%#x slave=%u address=%#x table=%#x; no votes\n",
     name,e,slave,address,table); return false;
   }
   unsigned bytes=db16(e+20);
   if (bytes!=8 || (table&3) || !range(table,8)) {
    SWEET_LOG_ERROR("SWEET RPMH BCM: name=%s gate=aux entry=%#x address=%#x table=%#x bytes=%u db-size=%#x; no votes\n",
     name,e,address,table,bytes,fDBSize); return false;
   }
   uint32_t unit=read(fDB,table), packed=read(fDB,table+4);
   if (!unit || (!(packed&0xffffu) && !resource.enableMask) || ((packed>>16)&255)>=16 || (packed>>24)) {
    SWEET_LOG_ERROR("SWEET RPMH BCM: name=%s gate=values address=%#x table=%#x unit=%u packed=%#x width=%u vcd=%u reserved=%u; no votes\n",
     name,address,table,unit,packed,packed&0xffffu,(packed>>16)&255,packed>>24); return false;
   }
   found.address=address; found.unit=unit; found.width=packed&0xffff;
   found.vcd=(packed>>16)&255; found.enableMask=resource.enableMask; ++matches;
  }
 }
 if (matches!=1) {
  SWEET_LOG_ERROR("SWEET RPMH BCM: name=%s gate=matches count=%u; no votes\n",name,matches); return false;
 }
 for (unsigned r=0;r<SweetDisplayBCMCount;++r)
  if ((fBCMAvailable&(1u<<r)) && fBCM[r].address==found.address) {
   SWEET_LOG_ERROR("SWEET RPMH BCM: name=%s gate=alias address=%#x other=%s; no votes\n",
    name,found.address,displayBCMs[r].name); return false;
  }
 fBCM[bcm]=found; fBCMAvailable|=bit;
 SWEET_LOG_INFO("SWEET RPMH BCM: name=%s metadata ready address=%#x unit=%u width=%u vcd=%u enable-mask=%#x\n",
  name,found.address,found.unit,found.width,found.vcd,found.enableMask);
 return true;
}

/* Delegated: no flight may reach the array until the owner's identity and the
 * completion binding are both in place. One gate for all three entry points, so
 * a rail vote cannot slip past what a register vote already checks. */
bool SweetRPMh::ensureExecutor(void)
{
 if (!fDelegated) return true;
 /* One owner per boot, and its table is the only write path. Without it there
  * is nothing here to store through, so refuse before the flight - never fall
  * back to a mapping this mode deliberately does not take. */
 if (!executorDoor()) {
  SWEET_LOG_ERROR("SWEET RPMH: no executor door; request refused, no writable path here\n");
  return false;
 }
 if (!fIdentityReady && !adoptDelegatedIdentity()) {
  SWEET_LOG_ERROR("SWEET RPMH: hardware owner published no identity; request refused, no self-mapped fallback\n");
  return false;
 }
 if (!installDelegated()) {
  SWEET_LOG_ERROR("SWEET RPMH: hardware owner accepts no completion; request refused, no polling fallback\n");
  return false;
 }
 return true;
}

static const char *sweetRequestName(unsigned kind)
{
 switch (kind) {
 case SweetAppsRequestRailVote: return "rail-vote";
 case SweetAppsRequestDisplayCorner: return "display-corner";
 case SweetAppsRequestRegisterBandwidth: return "register-bandwidth";
 default: return "unknown";
 }
}

/* One encoded unit of CN0 at this vote's own bus width (4 bytes, one port):
 * divisor = busWidth*ports*unit in scale(), so the rate that moves y by one is
 * that divisor divided by the register width. Zero means this boot cannot say,
 * and no demonstration runs with a raise that would not move the field. */
uint64_t
SweetRPMh::cn0Step(void) const
{
 const SweetBCMInfo &bcm=fBCM[SweetBCMCN0];
 uint64_t divisor=(uint64_t)bcm.unit*4;
 if (!divisor || !bcm.width) return 0;
 return (divisor+bcm.width-1)/bcm.width;
}

/* C3-05 device demonstration, requested explicitly by an ADT property and off
 * for every other boot. Two consumers each raise register bandwidth by one
 * whole encoded step, through the same admission path a client uses, so the
 * second one has to wait in the queue for the first to retire.
 *
 * A rail vote cannot carry this demonstration on this boot: the display client
 * votes every rail as a point (min == max), the aggregate takes the maximum of
 * the floors and the minimum of the ceilings, so any second consumer that moves
 * a floor is illegal and is refused by the admission screen before it costs a
 * token (0148 device evidence: phase=refuse kind=rail-vote detail=invalid
 * result=0xe00002c2 twice, then pair-did-not-serialize). Register bandwidth
 * aggregates by maximum instead, so a consumer that asks for more is legal and
 * produces a real flight rather than the silent no-op an unchanged vote is.
 *
 * The step is one unit of the y field scale() rounds to, never a percentage: a
 * raise smaller than that rounds to the same y and the aggregate would not move.
 * Nothing is submitted unless scale() accepts both targets, and nothing is
 * demonstrated by a pair that could not be written. The votes are retained
 * afterwards like any other vote, and the boot's own display vote for this
 * resource subsumes them; C3-06 withdraws them explicitly, in cancelSelfTest(),
 * once this pair has retired - never as a side effect of this demonstration. */
bool
SweetRPMh::queueSelfTest(void)
{
 /* "One flight window" is a claim about the array when the pair is admitted: the
  * first member must still be holding it when the second arrives. A retirement
  * that starts with a real submission already waiting would park both members
  * behind it, print the same two successes and demonstrate nothing, so this
  * waits for a window that starts empty instead of spending a try on one that
  * cannot show the shape it advertises. (0150: the next demonstration down is
  * reached from the same retirement and was admitted into exactly such a queue.) */
 if (fBusy || !fQueue.empty()) return false;
 if (fQueueTestTries++ >= 6) return true;
 if (fNextClient==~0ULL) return false;
 if (!lookupBCM(SweetBCMCN0)) return false;
 uint64_t step=cn0Step();
 if (!step) return false;
 uint64_t base=fRegisterOwned ? fRegisterAggregate : 0;
 uint64_t proposal[2]={base+step,base+2*step};
 for (unsigned k=0;k<2;++k) {
  unsigned y=0;
  if (!fBCM[SweetBCMCN0].scale(proposal[k],4,1,&y)) return false;
 }
 int clients[2]={-1,-1};
 for (unsigned i=0;i<8 && clients[1]<0;++i) {
  if (fClients[i].id) continue;
  if (clients[0]<0) clients[0]=(int)i; else clients[1]=(int)i;
 }
 if (clients[1]<0) return false;
 unsigned tokens[2]={0,0}; unsigned results[2]={0,0};
 for (unsigned k=0;k<2;++k) {
  fClients[clients[k]]={}; fClients[clients[k]].id=++fNextClient;
  fSelfClients[k]=(unsigned)clients[k];
  IOReturn result=beginRegisterBandwidth((unsigned)clients[k],proposal[k]);
  results[k]=(unsigned)result;
  tokens[k]=(unsigned)fClients[clients[k]].registerStatus.token;
 }
 SWEET_LOG_INFO("SWEET APPS QUEUE: phase=selftest kind=register-bandwidth resource=%u bytes0=%llu bytes1=%llu client0=%u client1=%u token0=%u token1=%u result0=%#x result1=%#x depth=%u high-water=%u; two admissions, one flight window\n",
     (unsigned)SweetBCMCN0,proposal[0],proposal[1],(unsigned)clients[0],(unsigned)clients[1],
     tokens[0],tokens[1],results[0],results[1],fQueue.depth(),fQueue.highWater());
 /* The pair is only evidence if the first one really holds the array when the
  * second arrives; a pair that both ran directly would prove nothing and must
  * not be reported as a demonstration. */
 if (results[0]!=kIOReturnSuccess || results[1]!=kIOReturnSuccess ||
     tokens[0]==tokens[1] || !fQueue.depth()) {
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=selftest result=failed detail=pair-did-not-serialize\n");
  return true;
 }
 return true;
}

/* C3-06 device demonstration, step 1: three requests for one quantity, of
 * which only the first will ever be written.
 *
 * The base is what the array is being told *now*, not a constant: the pair
 * demonstration in front of this one has already raised it, and a proposal that
 * did not exceed it would be admitted without a write - which would make the
 * two behind it pointless. The two behind it must be waiting, at depth 1 and 2;
 * a trio that all flew, or all waited, would prove nothing about either way of
 * taking a request back.
 *
 * The pair's ledger entries are *not* withdrawn here. A withdrawal is legal
 * only while somebody else holds the array up, and until the first of these
 * three lands, nobody does: a vote does not exist before it completes. Step 2
 * runs at that moment, and hands the pair back then.
 */
bool
SweetRPMh::cancelSelfTest(void)
{
 /* The same rule as the pair demonstration above, and for one more reason: the
  * trio is only a demonstration while its first member flies *directly* and the
  * other two wait behind it. Admitted into a busy array - which is what the 0150
  * boot did, because both demonstrations were reached from one retirement and
  * the pair left its second member waiting - all three park, three real raises
  * are left on the array, and nothing was demonstrated (depth=4,
  * trio-did-not-serialize). Step 1 therefore waits for a window that starts
  * empty instead of spending its one shot on a window that cannot show the shape
  * it claims; a wait here prints nothing, which is right, because nothing
  * happened. */
 if (fBusy || !fQueue.empty()) return false;
 uint64_t step=cn0Step();
 if (!step) return true;
 uint64_t base=fRegisterAggregate;
 uint64_t proposal[3]={base+3*step,base+4*step,base+5*step};
 for (unsigned k=0;k<3;++k) {
  unsigned y=0;
  if (!fBCM[SweetBCMCN0].scale(proposal[k],4,1,&y)) {
   SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=cancel-selftest step=1 result=failed detail=scale-refused\n");
   return true;
  }
 }
 int clients[3]={-1,-1,-1};
 for (unsigned i=0;i<8 && clients[2]<0;++i) {
  if (fClients[i].id) continue;
  for (unsigned k=0;k<3;++k) if (clients[k]<0) { clients[k]=(int)i; break; }
 }
 if (clients[2]<0) return true;
 for (unsigned k=0;k<3;++k) {
  unsigned i=(unsigned)clients[k];
  fClients[i]={}; fClients[i].id=++fNextClient;
  fCancelClients[k]=i;
  IOReturn result=beginRegisterBandwidth(i,proposal[k]);
  fCancelTokens[k]=fClients[i].registerStatus.token;
  if (result!=kIOReturnSuccess) {
   SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=cancel-selftest step=1 result=failed detail=admission-%u result=%#x\n",
       k,(unsigned)result);
   return true;
  }
 }
 bool distinct=fCancelTokens[1] && fCancelTokens[2] &&
     fCancelTokens[0]!=fCancelTokens[1] && fCancelTokens[1]!=fCancelTokens[2];
 if (!distinct || fQueue.depth()!=2) {
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=cancel-selftest step=1 result=failed detail=trio-did-not-serialize depth=%u\n",
      fQueue.depth());
  return true;
 }
 SWEET_LOG_INFO("SWEET APPS QUEUE: phase=cancel-selftest kind=register-bandwidth resource=%u bytes0=%llu bytes1=%llu bytes2=%llu client0=%u client1=%u client2=%u token0=%u token1=%u token2=%u depth=%u high-water=%u; first in flight, two waiting\n",
     (unsigned)SweetBCMCN0,proposal[0],proposal[1],proposal[2],
     fCancelClients[0],fCancelClients[1],fCancelClients[2],
     (unsigned)fCancelTokens[0],(unsigned)fCancelTokens[1],(unsigned)fCancelTokens[2],
     fQueue.depth(),fQueue.highWater());
 return true;
}

/* C3-06 device demonstration, step 2: the first half has completed on hardware,
 * so the two behind it are still waiting - the only moment at which they can be
 * taken back without touching anything the provider owns.
 *
 * It is also the only moment the pair demonstration's ledger can be handed
 * back: the completed request holds the array now, so each of those two votes
 * can leave the ledger without moving the array - which is exactly what a
 * withdrawal has to be. Both are ordinary calls, and both clients then leave.
 *
 * Then the two halves of the contract run on the two waiting requests, so one
 * ring shows both: the caller withdraws its own wait by token, and the next
 * client never withdraws anything because it has already exited. Neither is a
 * hardware rollback - the array keeps the value the completed half wrote.
 *
 * Finally the completed client asks for the boot's own rate back. That is the
 * other half of "a withdrawal is not a vote": getting the array back down is a
 * transaction, so it is a vote, it goes out as one, and it is the last thing
 * this demonstration asks for. */
bool
SweetRPMh::cancelSelfTestCancel(void)
{
 const unsigned a=fCancelClients[0], b1=fCancelClients[1], b2=fCancelClients[2];
 bool ok=a<8 && b1<8 && b2<8;
 SweetRegisterBandwidthStatus statusA={};
 if (a<8 && fClients[a].id && fClients[a].registerStatus.token==fCancelTokens[0])
  statusA=fClients[a].registerStatus;
 ok=ok && statusA.state==SweetRPMhComplete;
 unsigned withdrew=0, exited=0;
 if (ok) {
  for (unsigned k=0;k<2 && ok;++k) {
   const unsigned i=fSelfClients[k];
   uint64_t previous=0, aggregate=0;
   if (i>=8 || !fClients[i].id || !fClients[i].registerVote) { ok=false; break; }
   if (withdrawRegisterVote(i,&previous,&aggregate)==kIOReturnSuccess) withdrew++;
   else { ok=false; break; }
   if (detachClient(i,NULL)==kIOReturnSuccess) exited++;
   else ok=false;
  }
  ok=ok && withdrew==2 && exited==2;
 }
 fCancelLedger=ok;
 const IOReturn cancelled=ok ? cancelWaiting(b1,fCancelTokens[1],"caller") : kIOReturnAborted;
 SweetRegisterBandwidthStatus statusB1={};
 if (cancelled==kIOReturnSuccess && b1<8) statusB1=fClients[b1].registerStatus;
 ok=ok && cancelled==kIOReturnSuccess;
 ok=ok && statusB1.state==SweetRPMhFailed && statusB1.result==kIOReturnAborted;
 /* The other waiting request is never named by anyone: its client simply goes,
  * and the exit is what withdraws it. */
 unsigned exitedWith=0;
 const IOReturn detached=ok ? detachClient(b2,&exitedWith) : kIOReturnBusy;
 ok=ok && detached==kIOReturnSuccess && exitedWith==1;
 const uint64_t told=fRegisterAggregate;
 uint64_t lowered=0;
 if (ok) {
  const IOReturn result=beginRegisterBandwidth(a,0);
  lowered=fClients[a].registerStatus.token;
  ok=result==kIOReturnSuccess && lowered && lowered!=fCancelTokens[1] &&
      lowered!=fCancelTokens[2];
 }
 fCancelLower=ok ? lowered : 0;
 if (!ok) {
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=cancel-selftest step=2 result=failed detail=cancel-%#x b1-state=%u b1-result=%#x ledger=%u/%u exit-withdrew=%u lower=%#x depth=%u\n",
      (unsigned)cancelled,statusB1.state,(unsigned)statusB1.result,withdrew,exited,
      exitedWith,(unsigned)(lowered?(IOReturn)0:kIOReturnAborted),fQueue.depth());
  return true;
 }
 /* The console keeps one row in a 255-byte line buffer and silently drops the
  * rest of a longer one, newline included - 0151 lost this row's tail exactly
  * that way. So the fields come first and the sentence last: minus the newline
  * the row measures 203 bytes with this demonstration's own values and 253 with
  * the widest values these fields admit, against the 254 a whole row has.
  * `ledger` is withdrawals/exits. */
 SWEET_LOG_INFO("SWEET APPS QUEUE: phase=cancel-selftest step=2 result=ok a=%u b1=%u b1-state=%u b1-result=%#x b2=%u exit-withdrew=%u lower=%u ledger=%u/%u told=%llu depth=%u; one withdrew, one exited, neither ran\n",
     (unsigned)fCancelTokens[0],(unsigned)fCancelTokens[1],statusB1.state,
     (unsigned)statusB1.result,(unsigned)fCancelTokens[2],exitedWith,
     (unsigned)fCancelLower,withdrew,exited,told,fQueue.depth());
 return true;
}

/* C3-06 device demonstration, step 3: the lowering vote has completed, so the
 * last identity can go. The ring ends with the array being told this boot's own
 * value again - the demonstration leaves no ledger entry and no raised rate
 * behind, and nothing it did lowered the array by withdrawal. */
bool
SweetRPMh::cancelSelfTestFinish(void)
{
 const unsigned a=fCancelClients[0];
 bool ok=fCancelLower && a<8 && fClients[a].id;
 SweetRegisterBandwidthStatus status={};
 if (ok && fClients[a].registerStatus.token==fCancelLower) status=fClients[a].registerStatus;
 ok=ok && status.state==SweetRPMhComplete && !status.bytesPerSecond;
 unsigned exitedWith=0;
 const IOReturn detached=ok ? detachClient(a,&exitedWith) : kIOReturnBusy;
 ok=ok && detached==kIOReturnSuccess && !fClients[a].id && !exitedWith;
 if (!ok) {
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=cancel-selftest step=3 result=failed detail=state-%u bytes=%llu detach-%#x aggregate=%llu\n",
      status.state,status.bytesPerSecond,(unsigned)detached,fRegisterAggregate);
  return true;
 }
 SWEET_LOG_INFO("SWEET APPS QUEUE: phase=cancel-selftest step=3 result=ok exited=%u aggregate=%llu owned=%u depth=%u cancelled=%u; two waits never dispatched, every ledger entry handed back\n",
     a,fRegisterAggregate,fRegisterOwned?1u:0u,fQueue.depth(),fQueue.cancelled());
 return true;
}

/* ---------------------------------------------------------------------------
 * C3-07: naming a transaction, so a failure can be reported against the attempt
 * that earned it and a completion that arrives too late can be told apart from
 * the one now in flight.
 *
 * Three facts are kept, each in one place. The token names a submission for the
 * whole boot and is never reused. The generation names the attempt that
 * actually reached the array, taken when the first command of that request is
 * armed and kept for the rest of its chain, so a request that never needed a
 * TCS keeps generation 0 instead of pretending it did. The retained rows are
 * this service's own record of the last few attempts, and they are what a
 * caller is answered from once its request has retired: a client row may since
 * belong to somebody else, and the queue is empty by then.
 *
 * Nothing in here retries, resets or re-reads the array. A wrong answer would
 * be worse than a refusal, so a mismatched generation and an unknown token are
 * both refusals.
 * ------------------------------------------------------------------------ */

const char *
SweetRPMh::verdictName(unsigned verdict)
{
 switch (verdict) {
 case SweetTransactionPending: return "pending";
 case SweetTransactionComplete: return "complete";
 case SweetTransactionFailed: return "failed";
 case SweetTransactionUncertain: return "uncertain";
 default: return "unknown";
 }
}

/* The attempt that is about to reach the array takes the next generation. Every
 * later arm of the same request keeps it, because a multi-stage display chain
 * is one attempt to its caller and must answer as one. */
void
SweetRPMh::armFlight(uint64_t token)
{
 if (fFlightGeneration && fFlightToken==token) return;
 fFlightToken=token; fFlightGeneration=++fGeneration;
}

/* Keep this attempt's own outcome. The ring holds the last eight, which is more
 * than the queue can hold, so an answer that is still being asked for cannot
 * have been pushed out by the traffic that followed it. */
void
SweetRPMh::retainTransaction(uint64_t token,uint64_t generation,uint64_t client,
    unsigned kind,unsigned state,IOReturn result,bool executed,bool uncertain)
{
 if (!token) return;
 Transaction &row=fRetained[fRetainedNext];
 row.token=token; row.generation=generation; row.client=client; row.kind=kind;
 row.state=state; row.result=result; row.executed=executed; row.uncertain=uncertain;
 fRetainedNext=(fRetainedNext+1)&7;
 if (fRetainedCount<8) fRetainedCount++;
}

const SweetRPMh::Transaction *
SweetRPMh::retainedTransaction(uint64_t token,uint64_t generation) const
{
 for (unsigned i=0;i<fRetainedCount;++i) {
  const Transaction &row=fRetained[(fRetainedNext+8-i-1)&7];
  if (row.token!=token) continue;
  /* The token is the caller's, the generation is the attempt's: an answer for
   * a different generation of the same token would hand this attempt the next
   * one's outcome, so it is not this row. */
  if (row.generation!=generation) return NULL;
  return &row;
 }
 return NULL;
}

/* One place where an attempt stops being in flight: its identity becomes part
 * of the retained record, the array gets the damage marker if the outcome says
 * something may have executed, and the flight identity is named as retired so
 * a completion that arrives afterwards can be recognised instead of applied. */
void
SweetRPMh::settle(uint64_t token,unsigned kind,unsigned client,IOReturn result,bool uncertain)
{
 const bool executed=fFlightGeneration && fFlightToken==token;
 const uint64_t generation=executed ? fFlightGeneration : 0;
 const unsigned state=result==kIOReturnSuccess ? SweetRPMhComplete : SweetRPMhFailed;
 /* The record is keyed by the identity the client was given, not by the slot
  * it holds: a slot is reused by the next client, an identity never is. */
 const uint64_t owner=client<8 ? fClients[client].id : 0;
 retainTransaction(token,generation,owner,kind,state,result,executed,uncertain);
 fRetiredToken=token; fRetiredGeneration=generation; fRetiredUncertain=uncertain;
 if (!uncertain) return;
 /* Something may have executed and nothing here can name its result: stop
  * asking. The TCS is remembered because a slot that reads empty may still be
  * holding a command the array never finished with; it is not reused. */
 const unsigned tcs=fTCSIndex;
 if (!fQuarantine) {
  fQuarantine=true; fQuarantineCause=result; fQuarantineTcs=tcs;
 }
 SWEET_LOG_ERROR("SWEET RPMH: quarantine token=%llu generation=%llu tcs=%u cause=%#x kind=%s executed=%u; array state unknown, new submissions refused, nothing retried\n",
     token,generation,tcs,(unsigned)result,sweetRequestName(kind),executed?1u:0u);
}

/* A completion that arrives when nothing is in flight belongs to an attempt
 * this service already retired, and in every path that reaches here an attempt
 * it stopped waiting for. It is named by the identity that posted it and
 * dropped: the result the caller already has stands, no ledger entry or status
 * moves, and the array is marked because a completion nobody consumed means the
 * slot cannot be trusted to be idle. It is never treated as the current
 * transaction's completion. */
void
SweetRPMh::lateCompletion(void)
{
 const uint64_t token=fRetiredToken, generation=fRetiredGeneration;
 fLateCompletions++; fPoisoned=true;
 if (!fRetiredUncertain) {
  /* The attempt this belongs to already answered: this is a second completion
   * nobody is waiting for. Still a completion this service cannot account for,
   * so new work stops, but no attempt is left in doubt. */
  SWEET_LOG_WARN("SWEET RPMH IRQ: stray completion token=%llu generation=%llu tcs=%u in-flight=0 late=%u; already answered, dropped, no ledger moved\n",
      token,generation,fTCSIndex,fLateCompletions);
  quietIRQ(); dispatchNext(); return;
 }
 /* The attempt was given up on: this is the completion it was still able to
  * produce. Named as that attempt's and dropped, and the array is marked
  * because a slot whose completion nobody consumed is not idle. */
 if (!fQuarantine) {
  fQuarantine=true; fQuarantineCause=kIOReturnTimeout; fQuarantineTcs=fTCSIndex;
 }
 SWEET_LOG_WARN("SWEET RPMH IRQ: late completion token=%llu generation=%llu tcs=%u in-flight=0 late=%u; dropped, no ledger moved, channel quarantined\n",
     token,generation,fTCSIndex,fLateCompletions);
 quietIRQ();
 /* Whatever is waiting cannot run on a channel whose last completion has no
  * owner; it is failed against its own token instead of being left to time out. */
 dispatchNext();
}

IOReturn
SweetRPMh::copyTransactionStatus(uint64_t client,uint64_t token,uint64_t generation,
    SweetTransactionStatus *out)
{
 if (!out || !token) return kIOReturnBadArgument;
 *out=SweetTransactionStatus{};
 out->token=token; out->generation=generation; out->verdict=SweetTransactionUnknown;
 out->quarantined=fQuarantine;
 /* The caller names itself by identity; the queue and the current request name
  * their owner by slot. A client that has already left resolves to no slot,
  * and only its retained record can answer for it. */
 unsigned slot=8;
 for (unsigned i=0;i<8;++i) if (fClients[i].id && fClients[i].id==client) { slot=i; break; }
 /* Waiting: nothing of it has reached the array, so it has no generation of
  * its own and its outcome is exactly Pending. */
 const SweetAppsRequest *waiting=fQueue.findToken(token);
 if (waiting) {
  if (waiting->client!=slot) return kIOReturnNotPermitted;
  out->kind=waiting->kind; out->verdict=SweetTransactionPending; out->result=kIOReturnSuccess;
  out->generation=0; return kIOReturnSuccess;
 }
 if (fCurrentValid && fCurrent.token==token) {
  if (fCurrent.client!=slot) return kIOReturnNotPermitted;
  const bool executed=fFlightGeneration && fFlightToken==token;
  out->kind=fCurrent.kind; out->verdict=SweetTransactionPending;
  out->result=kIOReturnSuccess; out->executed=executed;
  out->generation=executed ? fFlightGeneration : 0;
  return kIOReturnSuccess;
 }
 const Transaction *row=retainedTransaction(token,generation);
 /* A record whose owner was already gone when it was written names nobody, so
  * there is no caller it may be handed to. */
 if (!row || !row->client) return kIOReturnNotFound;
 if (row->client!=client) return kIOReturnNotPermitted;
 out->kind=row->kind; out->generation=row->generation; out->result=row->result;
 out->executed=row->executed; out->uncertain=row->uncertain; out->retained=true;
 out->verdict=row->state==SweetRPMhComplete ? SweetTransactionComplete :
     (row->uncertain ? SweetTransactionUncertain : SweetTransactionFailed);
 return kIOReturnSuccess;
}

void
SweetRPMh::quarantineInfo(unsigned *tcs,IOReturn *cause,unsigned *refused)
{
 if (tcs) *tcs=fQuarantine ? fQuarantineTcs : 0u;
 if (cause) *cause=fQuarantine ? fQuarantineCause : kIOReturnSuccess;
 if (refused) *refused=fQuarantined+fLateCompletions;
}

/* The device half of C3-07: what a caller is told about a transaction it can
 * no longer find in its own row.
 *
 * It submits real votes, because a record of something that did not happen
 * would prove nothing: one raise of the CN0 rate, the restore that puts the
 * boot's own value back, one request for the value the array is already
 * telling, and the vote that hands the raise back so its client can leave.
 * Then it asks about each attempt by (token, generation) - while it flies,
 * after it retires, with the generation it did not take, with a token nobody
 * was given, for a request the array was never told about, and finally after
 * the client that owned them has left.
 *
 * It takes only windows that start empty and it hands back what it raised, so a
 * boot that is already talking to the array is never left driven further than
 * the demonstration itself. A request it submits may retire synchronously, and
 * the steps that need no retirement are taken in one pass; the two that hand
 * the array on wait for their own flight like any other caller. The
 * re-entrancy flag keeps a settlement that lands inside its own submission
 * from starting the next step out of order. */
bool
SweetRPMh::querySelfTest(void)
{
 if (fQueryActive) return false;
 if (fBusy || !fQueue.empty()) return false;
 if (fQueryPhase==0 && fQueryTries++>=6) return true;
 if (fNextClient==~0ULL) return false;
 if (!lookupBCM(SweetBCMCN0)) return false;
 const uint64_t step=cn0Step();
 if (!step) return false;
 fQueryActive=true;
 bool done=false;
 /* Steps that need no flight are taken here, one after another, so the
  * demonstration cannot wait for a retirement that will never come. */
 for (unsigned guard=0;guard<4;++guard) {
  done=(fQueryPhase==0 ? querySelfTestRaise(step) :
      (fQueryPhase==1 ? querySelfTestRetired() :
       (fQueryPhase==2 ? querySelfTestSettle() : querySelfTestLeave())));
  if (!done || fBusy || !fQueue.empty() || fQueryPhase>=4) break;
 }
 fQueryActive=false;
 return done;
}

/* Step 1: a real raise, asked about while the array is still being told. */
bool
SweetRPMh::querySelfTestRaise(uint64_t step)
{
 fQueryBase=fRegisterOwned ? fRegisterAggregate : 0;
 unsigned y=0;
 if (!fBCM[SweetBCMCN0].scale(fQueryBase+step,4,1,&y)) return false;
 int slot=-1;
 for (unsigned i=0;i<8;++i) if (!fClients[i].id) { slot=(int)i; break; }
 if (slot<0) return true;
 fQuerySlot=(unsigned)slot;
 fClients[fQuerySlot]={}; fClients[fQuerySlot].id=++fNextClient;
 fQueryClient=fClients[fQuerySlot].id;
 const IOReturn raised=beginRegisterBandwidth(fQuerySlot,fQueryBase+step);
 fQueryToken=fClients[fQuerySlot].registerStatus.token;
 if (raised!=kIOReturnSuccess || !fQueryToken) {
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=transaction step=1 result=failed detail=raise-refused rc=%#x token=%llu base=%llu\n",
      (unsigned)raised,fQueryToken,fQueryBase);
  fQueryPhase=4;
  return true;
 }
 /* Asked before the request it names has retired: the answer has to be Pending
  * with the generation this attempt took, which is what makes the later answer
  * about the same attempt comparable to it. */
 SweetTransactionStatus status={};
 const IOReturn asked=copyTransactionStatus(fQueryClient,fQueryToken,0,&status);
 const bool inFlight=asked==kIOReturnSuccess && status.verdict==SweetTransactionPending &&
     status.executed && status.generation && !status.retained;
 if (!inFlight) {
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=transaction step=1 result=failed detail=not-in-flight rc=%#x verdict=%s executed=%u generation=%llu token=%llu\n",
      (unsigned)asked,verdictName(status.verdict),status.executed?1u:0u,status.generation,fQueryToken);
  fQueryPhase=4;
  return true;
 }
 fQueryGeneration=status.generation;
 SWEET_LOG_INFO("SWEET APPS QUEUE: phase=transaction step=1 result=ok token=%llu generation=%llu verdict=%s executed=1 retained=0 client=%llu base=%llu target=%llu; asked while the attempt it names was in flight\n",
     fQueryToken,fQueryGeneration,verdictName(status.verdict),fQueryClient,fQueryBase,fQueryBase+step);
 fQueryPhase=1;
 return true;
}

/* Step 2: the attempt has retired, so its outcome exists only in this
 * service's record - the client row already names the next one. It answers for
 * its own generation and refuses the other two questions. */
bool
SweetRPMh::querySelfTestRetired(void)
{
 SweetTransactionStatus own={}, crossed={}, unissued={};
 const IOReturn asked=copyTransactionStatus(fQueryClient,fQueryToken,fQueryGeneration,&own);
 const uint64_t other=fQueryGeneration ? fQueryGeneration-1 : fQueryGeneration+1;
 const IOReturn missed=copyTransactionStatus(fQueryClient,fQueryToken,other,&crossed);
 const IOReturn unknown=copyTransactionStatus(fQueryClient,~fQueryToken,0,&unissued);
 const bool complete=asked==kIOReturnSuccess && own.verdict==SweetTransactionComplete &&
     own.retained && own.executed;
 const bool refused=missed==kIOReturnNotFound && unknown==kIOReturnNotFound &&
     crossed.verdict==SweetTransactionUnknown && unissued.verdict==SweetTransactionUnknown;
 if (!complete || !refused) {
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=transaction step=2 result=failed detail=%s own=%#x verdict=%s retained=%u missed=%#x unissued=%#x token=%llu generation=%llu\n",
      complete?"query-refused":"record-disagrees",(unsigned)asked,verdictName(own.verdict),
      own.retained?1u:0u,(unsigned)missed,(unsigned)unknown,fQueryToken,fQueryGeneration);
  fQueryPhase=4;
  return true;
 }
 /* A second, later submission of the same client: it asks for what the array is
  * already being told, so it settles without a flight of its own and takes the
  * client row's token with it. That is what makes the next step able to say
  * "answered from the record" instead of "answered from whoever holds the row". */
 unsigned y=0;
 if (!fBCM[SweetBCMCN0].scale(fRegisterAggregate,4,1,&y)) return false;
 const IOReturn restored=beginRegisterBandwidth(fQuerySlot,fRegisterAggregate);
 fQueryRestore=fClients[fQuerySlot].registerStatus.token;
 const bool later=restored==kIOReturnSuccess && fQueryRestore && fQueryRestore!=fQueryToken;
 if (!later) {
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=transaction step=2 result=failed detail=second-token-missing rc=%#x restore=%#x token=%llu\n",
      (unsigned)restored,(unsigned)fQueryRestore,fQueryToken);
  fQueryPhase=4;
  return true;
 }
 SWEET_LOG_INFO("SWEET APPS QUEUE: phase=transaction step=2 result=ok token=%llu generation=%llu verdict=%s executed=1 retained=1 other=%#x unissued=%#x next=%llu rc=%#x; own record answered, other generation and unissued token refused\n",
     fQueryToken,fQueryGeneration,verdictName(own.verdict),(unsigned)missed,(unsigned)unknown,
     fQueryRestore,(unsigned)restored);
 fQueryPhase=2;
 return true;
}

/* Step 3 and the hand-back that step 4 asks for: a request the array is already
 * telling settles without a transaction of its own, so both the question and
 * its answer are visible the moment the submission returns - no retirement is
 * needed to observe them. The exit that follows is not like that: the client is
 * still holding the array, so it hands the vote back first, and whether the
 * hand-back was accepted is what the next retirement tells. */
bool
SweetRPMh::querySelfTestSettle(void)
{
 SweetTransactionStatus again={}, restore={};
 const IOReturn kept=copyTransactionStatus(fQueryClient,fQueryToken,fQueryGeneration,&again);
 const IOReturn asked=copyTransactionStatus(fQueryClient,fQueryRestore,0,&restore);
 const bool held=kept==kIOReturnSuccess && again.verdict==SweetTransactionComplete && again.retained;
 /* No generation to name at all: the array was already telling this, so the
  * attempt settled complete without ever being executed. */
 const bool landed=asked==kIOReturnSuccess && restore.verdict==SweetTransactionComplete &&
     restore.retained && !restore.executed && !restore.generation &&
     restore.result==kIOReturnSuccess;
 if (!held || !landed) {
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=transaction step=3 result=failed detail=%s kept=%#x verdict=%s restore=%#x verdict=%s executed=%u generation=%llu token=%llu\n",
      held?"restore-not-answered":"record-lost",(unsigned)kept,verdictName(again.verdict),
      (unsigned)asked,verdictName(restore.verdict),restore.executed?1u:0u,
      restore.generation,fQueryToken);
  fQueryPhase=4;
  return true;
 }
 SWEET_LOG_INFO("SWEET APPS QUEUE: phase=transaction step=3 result=ok token=%llu generation=0 verdict=%s executed=0 retained=1 client=%llu first=%llu; a request the array was already telling settles complete and unexecuted\n",
     fQueryRestore,verdictName(restore.verdict),fQueryClient,fQueryToken);
 /* The client still holds the array with its own vote, and a client that holds
  * anything may not leave: the exit would be refused, and the record this card
  * is about has to outlive an owner that handed everything back. So the vote is
  * given back the only way a vote can be given back - with another vote, this
  * one for zero. It is a real transaction (the boot's own holders keep the
  * value up, this client stops being one of them), so the answer about the
  * record after the exit waits for its retirement. */
 const uint64_t told=fRegisterAggregate;
 const IOReturn given=beginRegisterBandwidth(fQuerySlot,0);
 const uint64_t back=fClients[fQuerySlot].registerStatus.token;
 if (given!=kIOReturnSuccess || !back || back==fQueryToken || back==fQueryRestore) {
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=transaction step=4 result=failed detail=%s rc=%#x token=%llu told=%llu; a client that still holds the array cannot leave it\n",
      given==kIOReturnSuccess?"no-fresh-token":"vote-refused",(unsigned)given,back,told);
  fQueryPhase=4;
  return true;
 }
 SWEET_LOG_INFO("SWEET APPS QUEUE: phase=transaction step=4 result=ok token=%llu base=%llu told=%llu; the exit handed the vote back with a vote, not with the detach\n",
     back,fQueryBase,told);
 fQueryPhase=3;
 return true;
}

/* The last step: the client that asked is gone, and the record is still there.
 *
 * It runs on the retirement of the hand-back above, because a client that is
 * still waiting for its own vote to be acknowledged is still holding it. */
bool
SweetRPMh::querySelfTestLeave(void)
{
 if (fBusy || !fQueue.empty()) return false;
 unsigned exitedWith=0;
 const IOReturn detached=detachClient(fQuerySlot,&exitedWith);
 SweetTransactionStatus after={};
 unsigned tcs=0, refused=0; IOReturn cause=kIOReturnSuccess;
 quarantineInfo(&tcs,&cause,&refused);
 const uint64_t told=fRegisterAggregate;
 const IOReturn last=detached==kIOReturnSuccess ?
     copyTransactionStatus(fQueryClient,fQueryToken,fQueryGeneration,&after) : kIOReturnNotPermitted;
 /* A client that never asked for this attempt may not read it even though the
  * record survived its owner. Its answer goes to its own buffer: a refusal
  * still clears the status it was handed, so asking into the same one would
  * erase the answer this step is about. */
 SweetTransactionStatus alienStatus={};
 const IOReturn alien=detached==kIOReturnSuccess ?
     copyTransactionStatus(fQueryClient+1,fQueryToken,fQueryGeneration,&alienStatus) :
     kIOReturnSuccess;
 const bool stayed=last==kIOReturnSuccess && after.verdict==SweetTransactionComplete &&
     after.retained && alien==kIOReturnNotPermitted;
 if (!stayed) {
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=transaction step=5 result=failed detail=%s detach=%#x query=%#x alien=%#x verdict=%s retained=%u late=%u refused=%u quarantined=%u\n",
      detached==kIOReturnSuccess?"record-lost":"detach-refused",(unsigned)detached,(unsigned)last,
      (unsigned)alien,verdictName(after.verdict),after.retained?1u:0u,fLateCompletions,refused,fQuarantine?1u:0u);
  fQueryPhase=4;
  return true;
 }
 SWEET_LOG_INFO("SWEET APPS QUEUE: phase=transaction step=5 result=ok token=%llu generation=%llu verdict=%s retained=1 client=%llu detach=%#x exited=%u alien=%#x told=%llu late=%u quarantined=%u; answered after the client that owned it had left\n",
     fQueryToken,fQueryGeneration,verdictName(after.verdict),fQueryClient,(unsigned)detached,
     exitedWith,(unsigned)alien,told,fLateCompletions,fQuarantine?1u:0u);
 fQueryPhase=4;
 return true;
}

/* C3-08: the group boundary, asked rather than performed.
 *
 * The same gate the rail path uses is asked three questions on this boot's own
 * identity: does one command fit, do the three the production group carries
 * fit, and does one more than fits get in. The third answer is the
 * demonstration, and it costs no register access at all, because a refused
 * group is refused before a TCS is chosen - which is the honest way to show the
 * boundary: nothing was split, nothing was truncated, and there is no
 * half-programmed batch left behind to explain. */
void
SweetRPMh::groupSelfTest(void)
{
 const unsigned limit=groupCapacity();
 const IOReturn one=checkGroup(1,0,"selftest-one");
 const IOReturn three=checkGroup(3,0,"selftest-three");
 SWEET_LOG_INFO("SWEET RPMH GROUP: phase=selftest ncpt=%u ledger=%u limit=%u one=%#x three=%#x; the room binds, not the array\n",
     fNCPT,(unsigned)SweetRPMhExecutor::kCapacity,limit,(unsigned)one,(unsigned)three);
 const unsigned over=limit+1;
 const IOReturn refused=checkGroup(over,0,"selftest-over");
 SWEET_LOG_INFO("SWEET RPMH GROUP: phase=selftest count=%u limit=%u result=%#x stored=0 tcs=none; refused, not split\n",
     over,limit,(unsigned)refused);
}

/* A queued vote is rebuilt from these two words, never from the caller's
 * reference: the caller may return as soon as its token exists, and the queue
 * must still hold everything the dispatch needs. */
static void
sweetPackRail(SweetAppsRequest &request,const SweetRailVote &vote)
{
 request.param[0]=(uint64_t)vote.minMV|((uint64_t)vote.maxMV<<16)|(vote.enabled ? 1ULL<<32 : 0ULL);
 request.param[1]=vote.loadUA;
}

static SweetRailVote
sweetUnpackRail(const SweetAppsRequest &request)
{
 SweetRailVote vote={};
 vote.enabled=(request.param[0]&(1ULL<<32))!=0;
 vote.minMV=(unsigned)(request.param[0]&0xffffULL);
 vote.maxMV=(unsigned)((request.param[0]>>16)&0xffffULL);
 vote.loadUA=(unsigned)request.param[1];
 return vote;
}

/* The admission screen. It holds the refusals that used to run at the top of
 * each entry point, before that entry point touched a token, a status or the
 * array, so admitting a request that is going to be refused cannot cost the
 * caller anything. */
IOReturn
SweetRPMh::validate(const SweetAppsRequest &request)
{
 switch (request.kind) {
 case SweetAppsRequestRailVote: {
  if (request.rail>=SweetRailCount) return kIOReturnBadArgument;
  return vetoRail(request.client,request.rail,sweetUnpackRail(request),NULL); }
 case SweetAppsRequestDisplayCorner: {
  SweetDisplayCorner corner=(SweetDisplayCorner)request.param[0];
  if ((unsigned)corner>=SweetDisplayCornerCount) return kIOReturnBadArgument;
  if (!loadDisplayPower()) return kIOReturnNotReady;
  SweetDisplayCorner aggregate=corner;
  for (unsigned i=0;i<8;++i)
   if (i!=request.client && fClients[i].id && fClients[i].displayVote>aggregate)
    aggregate=fClients[i].displayVote;
  unsigned target[SweetDisplaySupplyCount]={};
  if (!displayTargets(aggregate,target)) return kIOReturnBadArgument;
  return kIOReturnSuccess; }
 case SweetAppsRequestRegisterBandwidth: {
  uint64_t target=request.param[0];
  for (unsigned i=0;i<8;++i)
   if (i!=request.client && fClients[i].id && fClients[i].registerVote>target)
    target=fClients[i].registerVote;
  if (!lookupBCM(SweetBCMCN0)) return kIOReturnNotReady;
  unsigned y=0;
  return fBCM[SweetBCMCN0].scale(target,4,1,&y) ? kIOReturnSuccess : kIOReturnBadArgument; }
 default: return kIOReturnBadArgument;
 }
}

/* C3-05: one place decides what happens to a submission - run it now, wait
 * behind the transaction in flight, or refuse it. Whatever survives this
 * function is a copy in fQueue or fCurrent, so nothing here points at the
 * caller's stack, and a caller that returns once its token exists can no
 * longer change what will be dispatched on its behalf. */
IOReturn
SweetRPMh::admit(SweetAppsRequest &request)
{
 if (fMode!=2) return kIOReturnNotPermitted;
 if (fPoisoned) {
  /* Something may have executed and nothing here can name its result, so this
   * service refuses rather than guess. The refusal is named and counted: a
   * caller that never receives a token has no other way to learn why. */
  fQuarantined++;
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=refuse kind=%s client=%u resource=%u detail=%s tcs=%u cause=%#x refused=%u result=%#x\n",
      sweetRequestName(request.kind),request.client,request.rail,
      fQuarantine?"quarantined":"stopped",fQuarantineTcs,
      (unsigned)fQuarantineCause,fQuarantined,(unsigned)kIOReturnNotReady);
  return kIOReturnNotReady;
 }
 if (!ensureExecutor()) return kIOReturnNotReady;
 if (!fAccepting || isInactive()) return kIOReturnNotReady;
 if (request.kind!=SweetAppsRequestRailVote && request.kind!=SweetAppsRequestDisplayCorner &&
     request.kind!=SweetAppsRequestRegisterBandwidth) return kIOReturnBadArgument;
 /* A request that cannot run is refused before it costs a token, so the
  * caller's previous completion stays readable and its sequence number does
  * not advance - the contract the single-flight path already had. */
 IOReturn veto=validate(request);
 if (veto!=kIOReturnSuccess) {
  SWEET_LOG_INFO("SWEET APPS QUEUE: phase=refuse kind=%s client=%u resource=%u detail=invalid result=%#x\n",
      sweetRequestName(request.kind),request.client,request.rail,(unsigned)veto);
  return veto;
 }
 /* Two outstanding requests for one resource would drive the same field once
  * dispatched, and merging them would invent an order the caller never asked
  * for. The second one is refused against its own row instead. A direct
  * request keeps seq=0, which is how a ring reader tells it from a queued one. */
 if (fQueue.find(request.kind,request.client,request.rail) ||
     (fCurrentValid && fCurrent.kind==request.kind &&
      fCurrent.client==request.client && fCurrent.rail==request.rail)) {
  fQueueRefused++;
  SWEET_LOG_INFO("SWEET APPS QUEUE: phase=refuse kind=%s client=%u resource=%u detail=already-outstanding depth=%u\n",
      sweetRequestName(request.kind),request.client,request.rail,fQueue.depth());
  return kIOReturnBusy;
 }
 if (fBusy && fQueue.full()) {
  fQueueRefused++;
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=refuse kind=%s client=%u resource=%u detail=queue-full depth=%u\n",
      sweetRequestName(request.kind),request.client,request.rail,fQueue.depth());
  return kIOReturnNoResources;
 }
 if (fNextToken==~0ULL) return kIOReturnNoResources;
 if (!fBusy) {
  /* A direct caller reads the outcome from this return value and from the
  * status the dispatch publishes, exactly as before; nothing is written on
  * its behalf when the dispatch refuses, so its previous completion stands. */
  request.token=++fNextToken; request.seq=0;
  fCurrent=request; fCurrentValid=true;
  SWEET_LOG_INFO("SWEET APPS QUEUE: phase=dispatch kind=%s client=%u resource=%u token=%llu seq=%u depth=%u wait=direct\n",
      sweetRequestName(request.kind),request.client,request.rail,request.token,request.seq,fQueue.depth());
  return launch(fCurrent);
 }
 request.token=++fNextToken;
 /* A waiting caller holds only its token, so the token has to exist and be
  * readable before the request runs; the fields a dispatch has not computed
  * are Pending, never a stale value from its previous request. */
 markPending(request);
 if (!fQueue.push(request,&request)) {
  fQueueRefused++;
  return kIOReturnNoResources;
 }
 SWEET_LOG_INFO("SWEET APPS QUEUE: phase=enqueue kind=%s client=%u resource=%u token=%llu seq=%u depth=%u\n",
     sweetRequestName(request.kind),request.client,request.rail,request.token,request.seq,fQueue.depth());
 return kIOReturnSuccess;
}

/* Dispatch exactly one request. Success means the transaction started, or it
 * completed without needing a write. Anything else is recorded against that
 * request's own token, because a queued caller never sees this return value. */
IOReturn
SweetRPMh::launch(const SweetAppsRequest &request)
{
 fLaunchToken=request.token;
 IOReturn result;
 switch (request.kind) {
 case SweetAppsRequestRailVote: result=launchRail(request); break;
 case SweetAppsRequestDisplayCorner: result=launchDisplayPower(request); break;
 case SweetAppsRequestRegisterBandwidth: result=launchRegisterBandwidth(request); break;
 default: result=kIOReturnUnsupported; break;
 }
 /* A request that completed without a flight owns nothing any more. A
  * refused one is reported by the caller that can report it: the admission
  * path returns this value, and the drain path writes it against the token. */
 if (!fBusy) fCurrentValid=false;
 return result;
}

/* Run whatever can run. The guard matters: a request that needs no write
 * retires inside this loop, and that retirement calls back here. The nested
 * call returns immediately and the outer loop keeps draining, so the drain
 * depth stays bounded by the queue capacity instead of by the call stack. */
void
SweetRPMh::dispatchNext(void)
{
 if (fDispatching) return;
 fDispatching=true;
 while (!fBusy && !fPoisoned && fQueue.depth()) {
  SweetAppsRequest request=*fQueue.head();
  const unsigned remaining=fQueue.depth()-1;
  fQueue.pop();
  SWEET_LOG_INFO("SWEET APPS QUEUE: phase=dispatch kind=%s client=%u resource=%u token=%llu seq=%u depth=%u wait=queued\n",
      sweetRequestName(request.kind),request.client,request.rail,request.token,request.seq,remaining);
  /* The request leaving the queue is the current one from here on: it is what
   * a second submission for the same resource is refused against, and it is
   * why the duplicate screen does not weaken once a waiting request starts.
   * launch() clears this again if the dispatch needed no flight. */
  fCurrent=request; fCurrentValid=true;
  IOReturn result=launch(request);
  /* A refused request does not block the ones behind it; it was refused
   * rather than started, so nothing of it reached the array. The caller is
   * gone, so the refusal has to land on the token it was handed. */
  if (result!=kIOReturnSuccess) {
   markFailure(request,result);
   SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=refuse kind=%s client=%u resource=%u token=%llu seq=%u detail=dispatch-refused result=%#x remaining=%u\n",
       sweetRequestName(request.kind),request.client,request.rail,request.token,request.seq,
       (unsigned)result,fQueue.depth());
  }
 }
 if (fPoisoned) drainQueue(kIOReturnNotReady,"isolation-state-set");
 fDispatching=false;
}

/* Fail every waiting request against its own token. Used when this service can
 * no longer honour the order it accepted: a transaction that left the array in
 * an unknown state, or a stop. Nothing is retried, merged or silently dropped -
 * each one gets a row and a Failed status, and the queue ends empty. */
void
SweetRPMh::drainQueue(IOReturn result,const char *reason)
{
 while (fQueue.depth()) {
  SweetAppsRequest request=*fQueue.head();
  fQueue.pop();
  markFailure(request,result);
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=refuse kind=%s client=%u resource=%u token=%llu seq=%u detail=%s result=%#x\n",
      sweetRequestName(request.kind),request.client,request.rail,request.token,request.seq,
      reason,(unsigned)result);
 }
}

/* C3-06: withdraw one request that has not been triggered yet.
 *
 * The queue is the only place such a record exists, so "there is nothing here
 * to cancel" is answered from this service's own state instead of guessed: the
 * request is either the one the machine is working on - triggered, and from
 * then on the provider's to finalize or isolate - or its token names nothing
 * that still waits. Both refusals are decisive and cost no write; neither sends
 * a compensating vote for a value the array is still being told.
 *
 * A withdrawal is not a hardware rollback: the hardware was never told about a
 * waiting request, so failing it changes only this client's ledger. Another
 * client's ledger and vote are never touched, and the record is out of the
 * queue before the dispatcher could reach it again. */
IOReturn
SweetRPMh::cancelWaiting(unsigned client,uint64_t token,const char *why)
{
 SweetAppsRequest removed={};
 if (!token) return kIOReturnBadArgument;
 const SweetAppsRequest *waiting=fQueue.findToken(token);
 if (waiting) {
  /* One client cannot withdraw another's wait, and a refusal must not disturb
   * the order of what the queue holds - so ownership is settled while the
   * record is still in place. */
  if (waiting->client!=client) {
   SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=refuse kind=%s client=%u resource=%u token=%llu detail=not-owner result=%#x\n",
       sweetRequestName(waiting->kind),client,waiting->rail,token,(unsigned)kIOReturnNotPermitted);
   return kIOReturnNotPermitted;
  }
  if (!fQueue.cancel(token,&removed)) return kIOReturnNotFound;
  markFailure(removed,kIOReturnAborted);
  SWEET_LOG_INFO("SWEET APPS QUEUE: phase=cancel kind=%s client=%u resource=%u token=%llu seq=%u depth=%u result=ok why=%s\n",
      sweetRequestName(removed.kind),removed.client,removed.rail,removed.token,removed.seq,
      fQueue.depth(),why);
  return kIOReturnSuccess;
 }
 const bool triggered=fCurrentValid && fCurrent.token==token;
 SWEET_LOG_INFO("SWEET APPS QUEUE: phase=refuse kind=%s client=%u resource=%u token=%llu detail=%s result=%#x why=%s\n",
     sweetRequestName(triggered?fCurrent.kind:SweetAppsRequestNone),client,
     triggered?fCurrent.rail:0,token,triggered?"already-triggered":"unknown-token",
     (unsigned)(triggered?kIOReturnBusy:kIOReturnNotFound),why);
 return triggered?kIOReturnBusy:kIOReturnNotFound;
}

/* Everything one exiting client still had waiting, newest first. Each record
 * gets its own row and its own token, so a reader can account for every wait
 * that will now never be dispatched. */
unsigned
SweetRPMh::cancelClientQueue(unsigned client,const char *why)
{
 SweetAppsRequest removed[kSweetAppsQueueCapacity];
 const unsigned taken=fQueue.cancelClient(client,removed,kSweetAppsQueueCapacity);
 for (unsigned i=0;i<taken && i<kSweetAppsQueueCapacity;++i) {
  markFailure(removed[i],kIOReturnAborted);
  SWEET_LOG_INFO("SWEET APPS QUEUE: phase=cancel kind=%s client=%u resource=%u token=%llu seq=%u depth=%u result=ok why=%s\n",
      sweetRequestName(removed[i].kind),removed[i].client,removed[i].rail,removed[i].token,
      removed[i].seq,fQueue.depth(),why);
 }
 return taken;
}

/* Detach, with the waiting half of the contract attached to it.
 *
 * The refusal checks come first, before anything is withdrawn: a detach that
 * answers Busy must leave the client exactly as it was, including its waiting
 * requests. Only a detach that is going to succeed takes the waiting records
 * with it - which is the whole point of C3-06, since releasing the identity
 * while a record still names it is how a dispatch would reach a dead slot. */
IOReturn
SweetRPMh::detachClient(unsigned client,unsigned *cancelled)
{
 if (cancelled) *cancelled=0;
 if (client>=8 || !fClients[client].id) return kIOReturnNotFound;
 const char *detail=NULL;
 if (fBusy && fClientIndex==client) detail="in-flight";
 for (unsigned rail=0;!detail && rail<SweetRailCount;++rail)
  if (fClients[client].votes[rail].enabled) detail="rail-vote-retained";
 if (!detail && (fClients[client].displayVote!=SweetDisplayOff ||
     fClients[client].displayStatus.uncertain)) detail="display-vote-retained";
 if (!detail && (fClients[client].registerVote || fClients[client].registerStatus.uncertain))
  detail="register-vote-retained";
 if (detail) {
  SWEET_LOG_INFO("SWEET APPS QUEUE: phase=detach client=%u cancelled=0 depth=%u detail=%s result=%#x\n",
      client,fQueue.depth(),detail,(unsigned)kIOReturnBusy);
  return kIOReturnBusy;
 }
 const unsigned withdrawn=cancelClientQueue(client,"exit");
 fClients[client]={};
 if (cancelled) *cancelled=withdrawn;
 SWEET_LOG_INFO("SWEET APPS QUEUE: phase=detach client=%u cancelled=%u depth=%u result=ok\n",
     client,withdrawn,fQueue.depth());
 return kIOReturnSuccess;
}

/* Drop this client's own CN0 vote from the ledger.
 *
 * The array is being told fRegisterAggregate. Removing a vote is a pure ledger
 * edit only while somebody else still holds that value up; if this client is
 * what holds it, then the hardware would have to be told something lower, and
 * that is a transaction - a new vote - not a side effect of a withdrawal. So
 * the lowering case is refused instead of silently retargeted, and a refusal
 * changes nothing. No reverse vote is ever produced here. */
IOReturn
SweetRPMh::withdrawRegisterVote(unsigned client,uint64_t *previous,uint64_t *aggregate)
{
 if (client>=8 || !fClients[client].id) return kIOReturnNotFound;
 const uint64_t held=fClients[client].registerVote;
 const uint64_t told=fRegisterAggregate;
 uint64_t others=0;
 for (unsigned i=0;i<8;++i)
  if (i!=client && fClients[i].id && fClients[i].registerVote>others)
   others=fClients[i].registerVote;
 if (!held) {
  SWEET_LOG_INFO("SWEET APPS QUEUE: phase=withdraw kind=register-bandwidth client=%u previous=0 aggregate=%llu detail=no-vote\n",
      client,told);
  return kIOReturnNotFound;
 }
 if (others<told) {
  SWEET_LOG_ERROR("SWEET APPS QUEUE: phase=withdraw kind=register-bandwidth client=%u previous=%llu aggregate=%llu others=%llu result=%#x detail=would-change-hardware; lower it with a vote\n",
      client,held,told,others,(unsigned)kIOReturnNotPermitted);
  return kIOReturnNotPermitted;
 }
 /* others == told by the refusal above, so the array keeps being told exactly
  * what it was told before this call: only the ledger moves. */
 fClients[client].registerVote=0;
 if (previous) *previous=held;
 if (aggregate) *aggregate=told;
 SWEET_LOG_INFO("SWEET APPS QUEUE: phase=withdraw kind=register-bandwidth client=%u previous=%llu aggregate=%llu others=%llu hardware=unchanged\n",
     client,held,told,others);
 return kIOReturnSuccess;
}

/* Publish the token before the request runs. Fields that a dispatch has not
 * computed yet stay zero until it does; Pending is the state that says so. */
void
SweetRPMh::markPending(const SweetAppsRequest &request)
{
 if (request.client>=8 || !fClients[request.client].id) return;
 switch (request.kind) {
 case SweetAppsRequestRailVote: {
  SweetRPMhStatus &status=fClients[request.client].status;
  status={}; status.token=request.token; status.state=SweetRPMhPending;
  status.rail=request.rail; status.result=kIOReturnSuccess; break; }
 case SweetAppsRequestDisplayCorner: {
  SweetDisplayPowerStatus &status=fClients[request.client].displayStatus;
  status={}; status.token=request.token; status.state=SweetRPMhPending;
  status.result=kIOReturnSuccess; status.corner=(SweetDisplayCorner)request.param[0];
  status.enabled=status.corner!=SweetDisplayOff; break; }
 case SweetAppsRequestRegisterBandwidth: {
  SweetRegisterBandwidthStatus &status=fClients[request.client].registerStatus;
  status={}; status.token=request.token; status.state=SweetRPMhPending;
  status.result=kIOReturnSuccess; status.bytesPerSecond=request.param[0]; break; }
 default: break;
 }
}

/* A refusal is decisive for the request that earned it: Failed, against its own
 * token, with the result that refused it. Not uncertain - these refusals all
 * happen before the request touches the array. */
void
SweetRPMh::markFailure(const SweetAppsRequest &request,IOReturn result)
{
 /* Kept before the client-row check: a request withdrawn by its client's exit
  * is exactly the one whose record has to outlive the row, and it never
  * reached the array, so its generation is 0. Whose it was is read from the row
  * while the row still names it. */
 const uint64_t owner=request.client<8 ? fClients[request.client].id : 0;
 retainTransaction(request.token,0,owner,request.kind,SweetRPMhFailed,result,false,false);
 if (request.client>=8 || !fClients[request.client].id) return;
 switch (request.kind) {
 case SweetAppsRequestRailVote: {
  SweetRPMhStatus &status=fClients[request.client].status;
  status.token=request.token; status.rail=request.rail;
  status.state=SweetRPMhFailed; status.result=result; status.uncertain=false; break; }
 case SweetAppsRequestDisplayCorner: {
  SweetDisplayPowerStatus &status=fClients[request.client].displayStatus;
  status.token=request.token; status.corner=(SweetDisplayCorner)request.param[0];
  status.state=SweetRPMhFailed; status.result=result; status.uncertain=false; break; }
 case SweetAppsRequestRegisterBandwidth: {
  SweetRegisterBandwidthStatus &status=fClients[request.client].registerStatus;
  status.token=request.token; status.bytesPerSecond=request.param[0];
  status.state=SweetRPMhFailed; status.result=result; status.uncertain=false; break; }
 default: break;
 }
}

IOReturn SweetRPMh::beginRegisterBandwidth(unsigned client,uint64_t proposal)
{
 SweetAppsRequest request={};
 request.kind=SweetAppsRequestRegisterBandwidth; request.client=client;
 request.param[0]=proposal;
 return admit(request);
}

IOReturn SweetRPMh::launchRegisterBandwidth(const SweetAppsRequest &request)
{
 unsigned client=request.client;
 uint64_t proposal=request.param[0];
 if (fMode!=2) return kIOReturnNotPermitted;
 if (fPoisoned) return kIOReturnNotReady;
 /* NotReady keeps the client's retry semantics, never polling. */
 if (!ensureExecutor()) return kIOReturnNotReady;
 if (!lookupBCM(SweetBCMCN0)) return kIOReturnNotReady;
 uint64_t target=proposal;
 for (unsigned i=0;i<8;++i) if (i!=client && fClients[i].id && fClients[i].registerVote>target)
  target=fClients[i].registerVote;
 unsigned y=0;
 if (!fBCM[SweetBCMCN0].scale(target,4,1,&y)) return kIOReturnBadArgument;
 bool noWrite=(!target && !fRegisterOwned) || (fRegisterOwned && target==fRegisterAggregate);
 unsigned chosen=0;
 if (!noWrite) {
  int empty=-1;
  for (unsigned t=0;t<2;++t) {
   uint32_t b=t*Stride, enabled=tcsRead(b+Enable);
   if (enabled&~((1u<<fNCPT)-1u)) return kIOReturnIOError;
   for (unsigned c=0;c<fNCPT;++c)
    if ((enabled&(1u<<c)) && tcsRead(b+Address+c*CommandStride)==fBCM[SweetBCMCN0].address) return kIOReturnBusy;
   if (empty<0 && emptyTCS(t)) empty=(int)t;
  }
  if (empty<0 || (fSavedIRQ=tcsRead(IRQEnable))==~0u) return kIOReturnBusy;
  chosen=(unsigned)empty;
  if (!emptyTCS(chosen)) return kIOReturnBusy;
 }
 SweetRegisterBandwidthStatus status={request.token,target,SweetRPMhPending,kIOReturnSuccess,false};
 fClients[client].registerStatus=status;
 fClientIndex=client; fRegisterProposal=proposal; fRegisterTarget=target;
 fArcRequest=false; fRegisterRequest=true; fBusy=true;
 if (noWrite) { finish(kIOReturnSuccess,false); return kIOReturnSuccess; }
 fTCSIndex=chosen; fExecuting=false; fIRQObserved=false; fOperation=fOperationCount=0; fWriteIssued=false;
 armFlight(request.token);
 fCommandCount=fCommandMask=1;
 uint32_t b=chosen*Stride, bit=1u<<chosen;
 addOperation(IRQEnable,fSavedIRQ&~bit); addOperation(IRQClear,bit,true); addOperation(b+Message,WriteACK);
 addOperation(b+Address,fBCM[SweetBCMCN0].address);
 addOperation(b+Data,(1u<<30)|(y ? 1u<<29 : 0u)|y);
 addOperation(b+Wait,1); addOperation(b+Enable,1); addOperation(b+Control,0); addOperation(b+Control,AMC); addOperation(IRQEnable,0);
 fDeadline=nowNS()+100000000ULL; fTimer->setTimeoutUS(1); return kIOReturnSuccess;
}

bool SweetRPMh::loadDisplayPower(void)
{
 if (!fArcMetadataTried) {
  fArcMetadataTried=true; fArcAvailable=true;
  for (unsigned r=0;r<SweetDisplaySupplyCount;++r)
   if (!lookupArc(r)) { fArcAvailable=false; break; }
  if (!fArcAvailable)
   SWEET_LOG_WARN("SWEET RPMH: display ARC chain unavailable/malformed; requests rejected, no fallback\n");
 }
 return fArcAvailable;
}

bool SweetRPMh::displayTargets(SweetDisplayCorner corner,unsigned *selectors)
{
 if ((unsigned)corner>=SweetDisplayCornerCount || !fArcAvailable) return false;
 for (unsigned r=0;r<SweetDisplaySupplyCount;++r) selectors[r]=0;
 if (corner==SweetDisplayOff) return true;
 /* LG MMCX has min-dropout=-1 in its level ABI (raw DB level+1).
  * Select MMCX first, then MX >= selected raw MMCX-1, minimum retention16.
  * The MX display-supply has no dropout propagation; its CX supply has a
  * fixed MIN_SVS48 floor. Do not invent a CX=MMCX voltage relationship. */
 unsigned required=displayMinimums[corner];
 for (unsigned step=0;step<SweetDisplaySupplyCount;++step) {
  unsigned r=SweetDisplayMMCX-step;
  if (r==SweetDisplayMX) {
   required=fArc[SweetDisplayMMCX].levels[selectors[SweetDisplayMMCX]]-1;
   if (required<16) required=16;
  } else if (r==SweetDisplayCX) required=48;
  unsigned index=1;
  while (index<fArc[r].count && fArc[r].levels[index]<required) ++index;
  if (index==fArc[r].count) return false;
  selectors[r]=index;
 }
 return true;
}

IOReturn SweetRPMh::selectArcTCS(unsigned *selected)
{
 int chosen=-1;
 for (unsigned t=0;t<2;++t) {
  uint32_t b=t*Stride, enabled=tcsRead(b+Enable);
  if (enabled & ~((1u<<fNCPT)-1u)) return kIOReturnIOError;
  for (unsigned c=0;c<fNCPT;++c) if (enabled&(1u<<c)) {
   uint32_t address=tcsRead(b+Address+c*CommandStride);
   /* Reserve the entire dependency chain, including a later stage's rail. */
   for (unsigned r=0;r<SweetDisplaySupplyCount;++r)
    if (address==fArc[r].address) return kIOReturnBusy;
  }
  if (chosen<0 && emptyTCS(t)) chosen=(int)t;
 }
 if (chosen<0) return kIOReturnBusy;
 *selected=(unsigned)chosen; return kIOReturnSuccess;
}

IOReturn SweetRPMh::queueArcStage(void)
{
 unsigned chosen=0;
 IOReturn result=selectArcTCS(&chosen);
 if (result!=kIOReturnSuccess) return result;
 uint32_t irq=tcsRead(IRQEnable);
 if (irq==~0u || !emptyTCS(chosen)) return kIOReturnBusy;
 fTCSIndex=chosen; fSavedIRQ=irq;
 armFlight(fLaunchToken);
 fExecuting=false; fIRQObserved=false; fOperation=fOperationCount=0; fWriteIssued=false;
 fCommandCount=1; fCommandMask=1;
 uint32_t b=chosen*Stride, bit=1u<<chosen;
 const ArcStage &stage=fArcStages[fArcStage];
 addOperation(IRQEnable,irq&~bit); addOperation(IRQClear,bit,true);
 addOperation(b+Message,WriteACK);
 addOperation(b+Address,fArc[stage.supply].address);
 addOperation(b+Data,stage.selector);
 addOperation(b+Wait,1); addOperation(b+Enable,1);
 addOperation(b+Control,0); addOperation(b+Control,AMC); addOperation(IRQEnable,0);
 fStageDeadline=nowNS()+100000000ULL;
 fTimer->setTimeoutUS(1); return kIOReturnSuccess;
}

IOReturn SweetRPMh::beginDisplayPower(unsigned client,SweetDisplayCorner proposal)
{
 if ((unsigned)proposal>=SweetDisplayCornerCount) return kIOReturnBadArgument;
 SweetAppsRequest request={};
 request.kind=SweetAppsRequestDisplayCorner; request.client=client;
 request.param[0]=(uint64_t)proposal;
 return admit(request);
}

IOReturn SweetRPMh::launchDisplayPower(const SweetAppsRequest &request)
{
 unsigned client=request.client;
 SweetDisplayCorner proposal=(SweetDisplayCorner)request.param[0];
 if (fMode!=2) return kIOReturnNotPermitted;
 if (fPoisoned) return kIOReturnNotReady;
 if (!ensureExecutor()) return kIOReturnNotReady;
 if (!loadDisplayPower()) return kIOReturnNotReady;
 SweetDisplayCorner aggregate=proposal;
 for (unsigned i=0;i<8;++i) if (i!=client && fClients[i].id && fClients[i].displayVote>aggregate)
  aggregate=fClients[i].displayVote;
 unsigned target[SweetDisplaySupplyCount]={};
 if (!displayTargets(aggregate,target)) return kIOReturnBadArgument;
 fArcStage=fArcStageCount=0;
 /* An inherited child may still be at a high corner. First guard unknown
  * parents at their highest DB-supported selector before changing the child.
  * Once the child ACKs, lower the guards to its actual requirements. This is
  * at most five stages, not a retry or a guess about inherited vote levels. */
 unsigned planned[SweetDisplaySupplyCount]={};
 for (unsigned r=0;r<SweetDisplaySupplyCount;++r) planned[r]=fArcApplied[r];
 if (aggregate!=SweetDisplayOff) for (unsigned r=SweetDisplayCX;r<SweetDisplayMMCX;++r)
  if (!fArcOwned[r]) {
   planned[r]=fArc[r].count-1;
   fArcStages[fArcStageCount++]={r,planned[r]};
  }
 /* Raises ACK parent->child; lowers ACK child->parent. An empty ledger's OFF
  * never disables inherited resources. Each stage has independent completion. */
 for (unsigned r=0;r<SweetDisplaySupplyCount;++r)
  if (target[r]>planned[r]) {
   fArcStages[fArcStageCount++]={r,target[r]}; planned[r]=target[r];
  }
 for (unsigned step=0;step<SweetDisplaySupplyCount;++step) {
  unsigned r=SweetDisplayMMCX-step;
  if (target[r]<planned[r]) fArcStages[fArcStageCount++]={r,target[r]};
 }
 if (fArcStageCount) {
  IOReturn result=queueArcStage();
  if (result!=kIOReturnSuccess) return result; /* No write/status/token change. */
 }
 SweetDisplayPowerStatus status={};
 status.token=request.token; status.state=SweetRPMhPending; status.corner=aggregate;
 status.result=kIOReturnSuccess; status.enabled=aggregate!=SweetDisplayOff;
 for (unsigned r=0;r<SweetDisplaySupplyCount;++r) {
  status.levels[r]=fArc[r].levels[target[r]];
  if (fArcOwned[r]) { status.known|=1u<<r; status.appliedLevels[r]=fArc[r].levels[fArcApplied[r]]; }
 }
 fClients[client].displayStatus=status;
 fArcProposal=proposal; fArcNext=aggregate; fArcRequest=true; fRegisterRequest=false; fClientIndex=client;
 fBusy=true; fDeadline=nowNS()+500000000ULL;
 if (!fArcStageCount) finish(kIOReturnSuccess,false);
 return kIOReturnSuccess;
}

void SweetRPMh::completeTransaction(void)
{
 if (!fArcRequest) { finish(kIOReturnSuccess,false); return; }
 const ArcStage &stage=fArcStages[fArcStage];
 unsigned supply=stage.supply;
 fArcApplied[supply]=stage.selector; fArcOwned[supply]=true;
 SweetDisplayPowerStatus &status=fClients[fClientIndex].displayStatus;
 status.acknowledged|=1u<<supply; status.known|=1u<<supply;
 status.appliedLevels[supply]=fArc[supply].levels[stage.selector];
 if (++fArcStage==fArcStageCount) { finish(kIOReturnSuccess,false); return; }
 IOReturn result=queueArcStage();
 /* Earlier stages already ACKed: retain their ledger and poison on failure.
  * No automatic rollback may lower a parent after an uncertain child write. */
 if (result!=kIOReturnSuccess) finish(result,true);
}

/* The array identity is the same in both modes; only its source differs. */
bool SweetRPMh::adoptIdentity(uint32_t id,uint32_t solver,uint32_t config)
{
 unsigned major=(id>>16)&255, count=(config>>12)&63, ncpt=(config>>27)&31;
 if ((major!=1 && major!=2) || solver==~0u || config==~0u ||
     count<8 || count>0x3000/Stride || ncpt<3 || ncpt>16) return false;
 fIdentityId=id; fIdentitySolver=solver; fIdentityConfig=config;
 fNCPT=ncpt; fIdentityReady=true; return true;
}

/* Delegated: the array belongs to the channel, so its identity arrives through
 * that owner's table. Absent table means "not started yet", never a reason to
 * map the array ourselves. */
bool SweetRPMh::adoptDelegatedIdentity(void)
{
 if (fIdentityReady) return true;
 const SweetAppsDoor *door=executorDoor();
 if (!door || !door->identity) return false;
 uint32_t id=0,solver=0,config=0;
 if (!door->identity(door->context,&id,&solver,&config)) return false;
 if (!adoptIdentity(id,solver,config)) {
  SWEET_LOG_ERROR("SWEET RPMH: hardware owner reported unsupported array id=%#x config=%#x\n",id,config);
  return false;
 }
 SWEET_LOG_INFO("SWEET RPMH: identity adopted from hardware owner id=%#x config=%#x ncpt=%u\n",id,config,fNCPT);
 return true;
}

/* Locator + command DB. Read-only here in both modes: this service resolves
 * resources from the DB and never writes it. */
bool SweetRPMh::mapDatabase(IOService *provider)
{
 fMaps[2]=provider->mapDeviceMemoryWithIndex(2);
 if (!fMaps[2] || fMaps[2]->getPhysicalAddress()!=0x0c3f000c || fMaps[2]->getLength()!=8) return false;
 vm_offset_t locator=fMaps[2]->getVirtualAddress();
 uint32_t dbPA=read(locator,0); fDBSize=read(locator,4);
 if (dbPA<0x80000000u || (dbPA&3) || (fDBSize&3) || fDBSize<DBHeader || fDBSize>0x100000 || (uint64_t)dbPA+fDBSize>0x100000000ULL) return false;
 IOMemoryDescriptor *descriptor=IOMemoryDescriptor::withPhysicalAddress(dbPA,fDBSize,fDelegated?kIODirectionIn:kIODirectionInOut);
 if (!descriptor) return false;
 fMaps[3]=descriptor->map(); descriptor->release();
 if (!fMaps[3] || fMaps[3]->getPhysicalAddress()!=dbPA || fMaps[3]->getLength()!=fDBSize) return false;
 fDB=fMaps[3]->getVirtualAddress();
 if (read(fDB,4)!=0x0c0330dbu) return false;
 if (read(fDB,0)!=1 || (read(fDB,140)&1))
  SWEET_LOG_WARN("SWEET RPMH: command DB version/flags differ; validated compatible layout only\n");
 for (unsigned r=0;r<SweetRailCount;++r) {
  if (!lookup(r)) { SWEET_LOG_ERROR("SWEET RPMH: missing/malformed display resource %s\n",railNames[r]); return false; }
  SWEET_LOG_INFO("SWEET RPMH: %s address=%#x available; inherited rail state unknown\n",railNames[r],fAddresses[r]);
 }
 return true;
}

bool SweetRPMh::mapResources(IOService *provider)
{
 OSData *abi=OSDynamicCast(OSData,provider->getProperty("sweet,rpmh-abi"));
 if (!abi || abi->getLength()!=4 || memcmp(abi->getBytesNoCopy(),"\1\0\0\0",4)) return false;
 if (provider->getResources()!=kIOReturnSuccess) return false;
 if (fDelegated) {
  /* No DRV map, no TCS map, no GIC map and no handler: the channel owns all of
   * them. This is the whole point of the card -- the old entry point cannot
   * write because it has no writable mapping left to write through. */
  if (!mapDatabase(provider)) return false;
  SWEET_LOG_INFO("SWEET RPMH: executor=delegated maps=locator+db; no tcs/gic window here, no irq handler here\n");
  return true;
 }
 const uint64_t pa[]={0x18220000,0x18220d00};
 const uint64_t length[]={0x100,0x3000};
 for (unsigned i=0;i<2;++i) {
  fMaps[i]=provider->mapDeviceMemoryWithIndex(i);
  if (!fMaps[i] || fMaps[i]->getPhysicalAddress()!=pa[i] || fMaps[i]->getLength()!=length[i]) return false;
 }
 fRSC=fMaps[0]->getVirtualAddress(); fTCS=fMaps[1]->getVirtualAddress();
 uint32_t id=read(fRSC,0), solver=read(fRSC,4), config=read(fRSC,12);
 if (!adoptIdentity(id,solver,config)) return false;
 if (!mapDatabase(provider)) return false;
 SWEET_LOG_INFO("SWEET RPMH: pre-v3 RSC id=%#x config=%#x ncpt=%u ACTIVE=2\n",id,config,fNCPT);
 return true;
}

/* A tree flag is a four-byte property that says exactly 1. Anything else -
 * missing, truncated, zero, another number - is not a request, so a malformed
 * tree can only ever turn a feature off. */
static bool
propertyRequested(IOService *provider,const char *name)
{
 OSData *flag=OSDynamicCast(OSData,provider->getProperty(name));
 if (!flag || flag->getLength()!=4) return false;
 uint32_t value=0; memcpy(&value,flag->getBytesNoCopy(),4);
 return value==1u;
}

/* The tree decides the owner, not a boot arg: the budget is spent and both
 * sides must read the same answer. */
bool SweetRPMh::delegatedRequested(IOService *provider)
{
 return propertyRequested(provider,"sweet,rpmh-executor-delegated");
}

/* Delegated: bind this service's completion to the hardware owner's event
 * source. Refused loudly, because a silently missing completion consumer would
 * turn every ACTIVE ACK into a timeout. */
bool SweetRPMh::installDelegated(void)
{
 if (!fDelegated || fInstalled) return true;
 const SweetAppsDoor *door=executorDoor();
 if (!door || !door->install) {
  SWEET_LOG_WARN("SWEET RPMH: delegated owner has not published yet; retrying on the next request\n");
  return false;
 }
 if (!door->install(door->context,this,completionThunk)) {
  SWEET_LOG_ERROR("SWEET RPMH: hardware owner refused the completion binding; no completion path here\n");
  return false;
 }
 fInstalled=true;
 SWEET_LOG_INFO("SWEET RPMH: phase=delegate result=0 owner=%s stores-here=0 irq-handlers-here=0\n",
  door->name ? door->name : "channel");
 return true;
}

bool SweetRPMh::start(IOService *provider)
{
 int mode=0; (void)PE_parse_boot_argn("sweetdisplayresources",&mode,sizeof(mode));
 if (mode<1 || mode>2 || !IOService::start(provider)) return false;
 fMode=(unsigned)mode;
 fQueue.reset();
 /* C3-07: nothing from a previous life of this object is inherited by this
  * boot - not a generation, not a retained outcome, not a damage marker. */
 fGeneration=fFlightToken=fFlightGeneration=fLaunchToken=0;
 fRetiredToken=fRetiredGeneration=0; fRetiredUncertain=false;
 fRetainedCount=fRetainedNext=fLateCompletions=0;
 for (unsigned i=0;i<8;++i) fRetained[i]=Transaction{};
 fQuarantine=false; fQuarantineCause=kIOReturnSuccess; fQuarantineTcs=0;
 /* An explicit tree request to demonstrate the submission queue on this boot.
  * Absent, zero or anything but 1 means the demonstration does not run. */
 fQueueSelfTest=propertyRequested(provider,"sweet,rpmh-queue-selftest");
 /* C3-06: cancelling and detaching a waiting request is the second explicit
  * tree request. It implies the first, because the thing it withdraws is a
  * request that the pair demonstration queued - a tree that asks only for the
  * withdrawal still runs the pair, and therefore still shows the same queue,
  * refusal and retirement rows before its own. */
 fQueueCancel=propertyRequested(provider,"sweet,rpmh-queue-cancel");
 if (fQueueCancel) fQueueSelfTest=true;
 /* C3-07: naming a transaction is its own explicit tree request. It implies
  * neither of the two above - it asks about a request it submits itself - but a
  * tree may carry all three, and then it runs last, because it asks about
  * attempts that have already retired. */
 fQuerySelfTest=propertyRequested(provider,"sweet,rpmh-transaction-query");
 fQueryActive=false;
 /* C3-08: asking for the group boundary is its own explicit tree request. It
  * implies none of the above: it submits no request, writes no register and
  * consumes no token, so it can run on a boot that asks for nothing else. */
 fGroupSelfTest=propertyRequested(provider,"sweet,rpmh-command-group");
 fGroupTestRun=false;
 /* Both demonstrations start from nothing to say, so a restart cannot inherit a
  * step from the boot before it. */
 fCancelStep=0; fCancelLower=0; fCancelLedger=false;
 fCancelTokens[0]=fCancelTokens[1]=fCancelTokens[2]=0;
 fSelfClients[0]=fSelfClients[1]=0;
 fCancelClients[0]=fCancelClients[1]=fCancelClients[2]=0;
 fQueryPhase=0; fQueryTries=0; fQuerySlot=0;
 fQueryClient=fQueryToken=fQueryGeneration=fQueryBase=0;
 fQueryRestore=fQueryIdle=0;
 /* C3-04: the owner comes from the tree, before the first register access and
  * without a boot arg (the boot-arg budget is spent). A tree that says both
  * sides program the line resolves to SweetAppsOwnerNone and nothing starts. */
 fDelegated=delegatedRequested(provider);
 SweetAppsOwner owner=sweet_apps_owner(fDelegated,!fDelegated);
 if (owner==SweetAppsOwnerNone) {
  SWEET_LOG_ERROR("SWEET RPMH: owner selection contradictory; delegated=%u writes-here=%u; no maps, no writes\n",
   fDelegated?1u:0u,fDelegated?0u:1u);
  return false;
 }
 if (fDelegated && fMode!=2) {
  SWEET_LOG_ERROR("SWEET RPMH: delegated owner needs mode 2 ACTIVE completion; mode=%d rejected\n",mode);
  return false;
 }
 unsigned expected=0;
 if (!__atomic_compare_exchange_n(&rpmhOwner,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
  SWEET_LOG_ERROR("SWEET RPMH: apps-DRV already owned/retired this boot\n"); return false;
 }
 fClaimed=true;
 if (!mapResources(provider)) { SWEET_LOG_ERROR("SWEET RPMH: resource ABI/geometry/DB rejected; no writes\n"); return false; }
 fLoop=IOWorkLoop::workLoop(); fGate=IOCommandGate::commandGate(this);
 fTimer=IOTimerEventSource::timerEventSource(this,timerAction);
 if (!fLoop || !fGate || !fTimer) return false;
 if (fLoop->addEventSource(fGate)!=kIOReturnSuccess) return false;
 fGateAdded=true;
 if (fLoop->addEventSource(fTimer)!=kIOReturnSuccess) return false;
 fTimerAdded=true;
 if (!fDelegated && fMode==2 && !setupIRQ(provider)) {
  SWEET_LOG_ERROR("SWEET RPMH IRQ: ABI/map/event/ownership rejected; mode2 disabled, no polling fallback\n"); return false;
 }
 fAccepting=true; fExecutorReported=false;
 SWEET_LOG_INFO("SWEET RPMH: owner=%s maps=%s irq-handler=%s stores-here=%u\n",
  fDelegated?"IOQcomRSCChannel":"SweetRPMh",fDelegated?"locator+db":"rsc+tcs+locator+db+db",
  fDelegated?"none":"own",fDelegated?0u:__atomic_load_n(&fSelfStores,__ATOMIC_ACQUIRE));
 setProperty("SweetRPMhABI",1ULL,32); setProperty("SweetRPMhWritesAllowed",fMode==2);
 unsigned sharedMask=0;
 for (unsigned r=0;r<SweetRailCount;++r) if (railLimits[r].lastDisableRequiresHandoff) sharedMask|=1u<<r;
 setProperty("SweetRPMhLastDisableBlockedMask",(unsigned long long)sharedMask,32);
 /* A capability marker, set once and never a claim about runtime state: what
  * this service can answer about a transaction is stated here, and what it
  * actually saw is only ever in the rows and in quarantineInfo(). */
 setProperty("SweetRPMhTransactions",1ULL,32);
 registerService(); return true;
}

bool SweetRPMh::emptyTCS(unsigned t)
{
 /* The software veto comes first: after an attempt that may still be executing,
  * this slot can read empty while the array is still finishing a command that
  * nobody consumed, so it is never offered again in this boot. */
 if (fQuarantine && t==fQuarantineTcs) return false;
 uint32_t b=t*Stride, idle=tcsRead(b+Status);
 return idle && idle!=~0u && tcsRead(b+Control)==0 && tcsRead(b+Enable)==0 && tcsRead(b+Wait)==0 && !(tcsRead(IRQStatus)&(1u<<t));
}
void SweetRPMh::addOperation(uint32_t off,uint32_t value,bool w1c)
{ fOperations[fOperationCount++]={off,value,w1c}; }

/* C3-08: how many commands one TCS may carry here, and what a group that asks
 * for more is told.
 *
 * Two numbers bound a group and they are not the same number. The array's NCPT
 * is the hardware's answer, sixteen slots at most. This service's reviewed word
 * buffer is the other one: kCapacity words hold the fixed head and tail plus
 * three words per command, and the room that leaves is the smaller figure. Both
 * are reported wherever the limit is printed, because naming sixteen while four
 * fit is how a refusal stops being believable.
 *
 * A group is the set of commands one trigger carries, so it is admitted or
 * refused whole. It is never split across two TCSes: the order would then span
 * two triggers and the completion would describe two batches, which is a
 * different request that nobody asked for. It is never truncated either: a
 * three-member group is not the four-member one with a member missing. */
unsigned
SweetRPMh::groupCapacity(void) const
{
 if (!fNCPT) return 0; /* identity not adopted: no claim, and no write either */
 const unsigned words=(SweetRPMhExecutor::kCapacity-SweetRPMhGroupOverhead)/SweetRPMhGroupWords;
 return words<fNCPT ? words : fNCPT;
}

IOReturn
SweetRPMh::checkGroup(unsigned count,uint64_t token,const char *kind) const
{
 const unsigned limit=groupCapacity();
 if (!limit) {
  SWEET_LOG_ERROR("SWEET RPMH GROUP: phase=refuse token=%llu kind=%s count=%u limit=0 ncpt=%u; array identity not adopted, group refused, nothing written\n",
      token,kind,count,fNCPT);
  return kIOReturnNotReady;
 }
 if (!count || count>limit) {
  SWEET_LOG_ERROR("SWEET RPMH GROUP: phase=refuse token=%llu kind=%s count=%u limit=%u ncpt=%u ledger=%u; refused whole, nothing written, no split\n",
      token,kind,count,limit,fNCPT,(unsigned)SweetRPMhExecutor::kCapacity);
  return kIOReturnUnsupported;
 }
 return kIOReturnSuccess;
}

/* The layout is declared once, next to the offsets it describes. */
static const SweetRPMhLayout tcsLayout={
 Status,IRQStatus,IRQEnable,IRQClear,Control,Wait,Enable,CommandStatus,CommandStride,
 Issued|Complete
};

/* ---------------------------------------------------------------------------
 * C3-04: one write path per boot.
 *
 * The TCS array is reached through exactly one of two implementations: this
 * service's own map, or the channel's map through the published table. Both
 * are the same function below, so no caller can produce a second store and no
 * client has to know which side owns the hardware.
 * ------------------------------------------------------------------------ */

const SweetAppsDoor *SweetRPMh::executorDoor(void)
{ return sweet_apps_door(); }

uint32_t SweetRPMh::tcsRead(uint32_t offset)
{
 if (fDelegated) {
  const SweetAppsDoor *door=executorDoor();
  if (!door || !door->read) return ~0u;
  return door->read(door->context,offset);
 }
 if (!fTCS) return ~0u;
 return read(fTCS,offset);
}

void SweetRPMh::tcsWrite(uint32_t offset,uint32_t value)
{
 if (fDelegated) {
  const SweetAppsDoor *door=executorDoor();
  if (!door || !door->write) {
   /* The card's refusal: in the new mode the old entry point must be unable to
    * write, and must say so rather than silently invent a second MMIO path. */
   SWEET_LOG_ERROR("SWEET RPMH: no executor door; store refused offset=%#x value=%#x\n",
    offset,value);
   fPoisoned=true; return;
  }
  door->write(door->context,offset,value); return;
 }
 if (!fTCS) {
  SWEET_LOG_ERROR("SWEET RPMH: no TCS window; store refused offset=%#x value=%#x\n",offset,value);
  fPoisoned=true; return;
 }
 write(fTCS,offset,value);
 __atomic_add_fetch(&fSelfStores,1u,__ATOMIC_RELAXED);
}

uint32_t SweetRPMh::portRead(void *context,uint32_t offset)
{ return ((SweetRPMh *)context)->tcsRead(offset); }

void SweetRPMh::portWrite(void *context,uint32_t offset,uint32_t value)
{ ((SweetRPMh *)context)->tcsWrite(offset,value); }

/* The table this service publishes when it owns the line. Every entry is the
 * same code the direct path used, so the delegated mode and the old mode run
 * one implementation and one order. */
bool SweetRPMh::publishTable(void)
{
 if (fTablePublished) return true;
 fTable.name="SweetRPMh"; fTable.context=this;
 fTable.read=tableRead; fTable.write=tableWrite; fTable.identity=tableIdentity;
 fTable.triggerReady=tableTriggerReady; fTable.arm=tableArm; fTable.unmask=tableUnmask;
 fTable.quiet=tableQuiet; fTable.stop=tableStop; fTable.irq=tableIrq;
 fTable.install=tableInstall; fTable.stores=tableStores; fTable.irqs=tableIrqs;
 if (!sweet_apps_door_publish(&fTable)) return false;
 fTablePublished=true; return true;
}

uint32_t SweetRPMh::tableRead(void *context,uint32_t offset)
{ return ((SweetRPMh *)context)->tcsRead(offset); }

void SweetRPMh::tableWrite(void *context,uint32_t offset,uint32_t value)
{ ((SweetRPMh *)context)->tcsWrite(offset,value); }

bool SweetRPMh::tableIdentity(void *context,uint32_t *id,uint32_t *solver,uint32_t *config)
{
 SweetRPMh *self=(SweetRPMh *)context;
 if (!self->fIdentityReady) return false;
 if (id) *id=self->fIdentityId;
 if (solver) *solver=self->fIdentitySolver;
 if (config) *config=self->fIdentityConfig;
 return true;
}

IOReturn SweetRPMh::tableTriggerReady(void *context)
{ return ((SweetRPMh *)context)->prepareIRQ(); }

void SweetRPMh::tableArm(void *context)
{ __atomic_store_n(&((SweetRPMh *)context)->fIRQArmed,1u,__ATOMIC_RELEASE); }

void SweetRPMh::tableUnmask(void *context)
{
 SweetRPMh *self=(SweetRPMh *)context;
 if (self->fIRQMap) write(self->fIRQMap->getVirtualAddress(),GICEnable,GICBit);
}

void SweetRPMh::tableQuiet(void *context,uint32_t bit)
{
 SweetRPMh *self=(SweetRPMh *)context;
 if (self->fIRQTouched && self->fIRQMap) write(self->fIRQMap->getVirtualAddress(),GICDisable,GICBit);
 if (bit) {
  uint32_t enabled=self->tcsRead(IRQEnable);
  if (enabled!=~0u) self->tcsWrite(IRQEnable,enabled&~bit);
 }
}

void SweetRPMh::tableStop(void *context)
{ ((SweetRPMh *)context)->stopIRQ(); }

bool SweetRPMh::tableIrq(void *context)
{ return ((SweetRPMh *)context)->handleIRQ(); }

bool SweetRPMh::tableInstall(void *context,void *target,bool (*callback)(void *))
{
 /* This service is its own single client; a second installer would be a second
  * completion path, which the card forbids. */
 return target==(void *)context && callback!=NULL;
}

unsigned SweetRPMh::tableStores(void *context)
{ return __atomic_load_n(&((SweetRPMh *)context)->fSelfStores,__ATOMIC_ACQUIRE); }

unsigned SweetRPMh::tableIrqs(void *context)
{ return __atomic_load_n(&((SweetRPMh *)context)->fIRQEntries,__ATOMIC_ACQUIRE); }

bool SweetRPMh::completionThunk(void *target)
{
 SweetRPMh *self=(SweetRPMh *)target;
 if (!self || !self->fAccepting) return false;
 self->completionArrived();
 return true;
}

IOReturn SweetRPMh::completionAction(OSObject *owner,void *,void *,void *,void *)
{
 /* Delegated mode: the channel's top half posted its event source, so the
  * completion runs on a workloop thread and never inside the channel's lock. */
 (void)SweetRPMh::completionThunk(owner);
 return kIOReturnSuccess;
}

bool SweetRPMh::panelFenceValid(const SweetPanelSupplyFence &f)
{
 if (!f.pins || !f.touch || !f.pinsConsumer || !f.pinsToken || !f.touchConsumer || !f.touchToken ||
     f.pins->isInactive() || f.touch->isInactive()) return false;
 SweetPanelPinStatus pin={}; SweetTouchPanelStatus touch={};
 return f.pins->copyLowStatus(f.pinsConsumer,f.pinsToken,&pin)==kIOReturnSuccess &&
     pin.token==f.pinsToken && pin.resetKnown && !pin.resetHigh && !pin.uncertain && pin.result==kIOReturnSuccess &&
     f.touch->copyStatus(f.touchConsumer,f.touchToken,&touch)==kIOReturnSuccess &&
     touch.token==f.touchToken && touch.quiesced && !touch.ready && !touch.uncertain && touch.result==kIOReturnSuccess;
}
void SweetRPMh::clearPanelFence(void)
{
 OSSafeReleaseNULL(fPanelFence.pins); OSSafeReleaseNULL(fPanelFence.touch);
 fPanelFence={};
}

/* Every rail refusal that depends only on this request and the committed
 * ledger. Admission runs it before a token exists, dispatch runs it again
 * because the ledger may have moved while the request waited; neither call
 * changes anything, so a refusal here has cost no token and no state. */
IOReturn
SweetRPMh::vetoRail(unsigned client,unsigned rail,const SweetRailVote &proposal,SweetRailVote *result) const
{
 SweetRailVote target={false,railLimits[rail].minMV,railLimits[rail].maxMV,0};
 if (proposal.enabled && (proposal.minMV>proposal.maxMV ||
     proposal.minMV<railLimits[rail].minMV || proposal.maxMV>railLimits[rail].maxMV))
  return kIOReturnBadArgument;
 uint64_t load=0;
 for (unsigned i=0;i<8;++i) if (fClients[i].id) {
  const SweetRailVote &vote=i==client ? proposal : fClients[i].votes[rail];
  if (!vote.enabled) continue;
  target.enabled=true;
  if (target.minMV<vote.minMV) target.minMV=vote.minMV;
  if (target.maxMV>vote.maxMV) target.maxMV=vote.maxMV;
  load+=vote.loadUA;
 }
 if (target.minMV>target.maxMV || load>0xffffffffULL) return kIOReturnBadArgument;
 /* Stopping the display link does not stop UFS/USB. Refuse BEFORE token,
  * request state or MMIO changes. Removing one of several enabled votes is
  * still legal; an untouched inherited rail remains the existing no-op. */
 if (!target.enabled && fOwned[rail] && fAggregate[rail].enabled &&
     railLimits[rail].lastDisableRequiresHandoff) return kIOReturnNotPermitted;
 target.loadUA=(unsigned)load;
 /* RPMh accepts millivolts, PMIC5 LDO hardware rounds by 8mV within these
  * ranges; reject constraints that cannot contain the rounded selection. */
 unsigned rounded=railLimits[rail].minMV+((target.minMV-railLimits[rail].minMV+7)/8)*8;
 if (rounded>target.maxMV) return kIOReturnBadArgument;
 target.minMV=rounded;
 if (result) *result=target;
 return kIOReturnSuccess;
}

IOReturn SweetRPMh::begin(unsigned client,unsigned rail,const SweetRailVote &proposal)
{
 SweetAppsRequest request={};
 request.kind=SweetAppsRequestRailVote; request.client=client; request.rail=rail;
 sweetPackRail(request,proposal);
 return admit(request);
}

IOReturn SweetRPMh::launchRail(const SweetAppsRequest &request)
{
 unsigned client=request.client, rail=request.rail;
 SweetRailVote proposal=sweetUnpackRail(request);
 if (fMode!=2) return kIOReturnNotPermitted;
 if (fPoisoned) return kIOReturnNotReady;
 if (!ensureExecutor()) return kIOReturnNotReady;
 SweetRailVote target={};
 IOReturn veto=vetoRail(client,rail,proposal,&target);
 if (veto!=kIOReturnSuccess) return veto;
 unsigned rounded=target.minMV;
 fArcRequest=false; fRegisterRequest=false; fClientIndex=client; fRail=rail; fProposal=proposal; fTarget=target;
 SweetRPMhStatus status={request.token,SweetRPMhComplete,rail,rounded,
     target.loadUA>=railLimits[rail].modeThresholdUA ? 7u : 4u,kIOReturnSuccess,target.enabled,false};
 /* Never disable an inherited rail simply because software has no votes. */
 if ((!target.enabled && !fOwned[rail]) || (fOwned[rail] && target.enabled==fAggregate[rail].enabled &&
     (!target.enabled || (target.minMV==fAggregate[rail].minMV &&
     (target.loadUA>=railLimits[rail].modeThresholdUA)==(fAggregate[rail].loadUA>=railLimits[rail].modeThresholdUA))))) {
  fClients[client].votes[rail]=proposal; fClients[client].status=status; fAggregate[rail]=target; return kIOReturnSuccess;
 }
 /* C3-08: the voltage group is the existing approved shape - a floor, a mode
  * and an enable vote for one rail, programmed in that order into consecutive
  * slots of one TCS behind one trigger. Its size is decided here, before a TCS
  * is picked, so a group that does not fit changes no state at all: no slot
  * selected, no client status written, nothing stored and no token consumed
  * beyond the caller's own. */
 const unsigned count=target.enabled ? 3u : 1u;
 IOReturn fit=checkGroup(count,request.token,"rail-vote");
 if (fit!=kIOReturnSuccess) return fit;
 int chosen=-1;
 for (unsigned t=0;t<2;++t) {
  uint32_t b=t*Stride, enabled=tcsRead(b+Enable);
  if (enabled & ~((1u<<fNCPT)-1u)) return kIOReturnIOError;
  for (unsigned c=0;c<fNCPT;++c) if ((enabled&(1u<<c)) && (tcsRead(b+Address+c*CommandStride)&~15u)==fAddresses[rail]) return kIOReturnBusy;
  if (chosen<0 && emptyTCS(t)) chosen=(int)t;
 }
 if (chosen<0 || (fSavedIRQ=tcsRead(IRQEnable))==~0u) return kIOReturnBusy;
 fTCSIndex=(unsigned)chosen;
 if (!emptyTCS(fTCSIndex)) return kIOReturnBusy;
 armFlight(request.token);
 fClients[client].status=status; fClients[client].status.state=SweetRPMhPending;
 fBusy=true; fExecuting=false; fIRQObserved=false; fOperation=0; fOperationCount=0; fWriteIssued=false;
 uint32_t b=fTCSIndex*Stride, bit=1u<<fTCSIndex;
 addOperation(IRQEnable,fSavedIRQ&~bit); addOperation(IRQClear,bit,true);
 fCommandCount=count; fCommandMask=(1u<<fCommandCount)-1u;
 const uint32_t offsets[]={0,8,4}, values[]={rounded,status.mode,1};
 for (unsigned c=0;c<fCommandCount;++c) {
  const uint32_t address=fAddresses[rail]+(target.enabled ? offsets[c] : 4);
  addOperation(b+Message+c*CommandStride,WriteACK);
  addOperation(b+Address+c*CommandStride,address);
  addOperation(b+Data+c*CommandStride,target.enabled ? values[c] : 0);
  /* A member row per command, in the order the words are queued: the slot
   * number is the array's own position, so the log reads the same way the
   * hardware will. One member is not a group and is already named by the ACK
   * row that follows, so it stays quiet. */
  if (count>1)
   SWEET_LOG_INFO("SWEET RPMH GROUP: token=%llu kind=rail-vote member=%u/%u slot=%u address=%#x data=%#x wait=1; one TCS, one trigger, slot order is write order\n",
       request.token,c+1,count,c,address,target.enabled ? values[c] : 0);
 }
 addOperation(b+Wait,fCommandMask); addOperation(b+Enable,fCommandMask);
 addOperation(b+Control,0); addOperation(b+Control,AMC); addOperation(IRQEnable,0);
 fDeadline=nowNS()+100000000ULL;
 fTimer->setTimeoutUS(1); return kIOReturnSuccess;
}

void SweetRPMh::finish(IOReturn result,bool uncertain)
{
 if (result!=kIOReturnSuccess) quietIRQ();
 clearPanelFence();
 if (fRegisterRequest) {
  SweetRegisterBandwidthStatus &s=fClients[fClientIndex].registerStatus;
  s.result=result; s.uncertain=uncertain;
  s.state=result==kIOReturnSuccess ? SweetRPMhComplete : SweetRPMhFailed;
  if (result==kIOReturnSuccess) {
   fClients[fClientIndex].registerVote=fRegisterProposal;
   fRegisterAggregate=fRegisterTarget;
   if (fRegisterTarget) fRegisterOwned=true;
   SWEET_LOG_INFO("SWEET RPMH: token=%llu CN0 ACTIVE ACK bytes/s=%llu; apps RSC, not pixel bandwidth\n",s.token,s.bytesPerSecond);
  } else {
   fPoisoned|=uncertain;
   SWEET_LOG_ERROR("SWEET RPMH: token=%llu CN0 rc=%#x uncertain=%u; no retry/reset\n",s.token,(unsigned)result,uncertain);
  }
  settle(s.token,SweetAppsRequestRegisterBandwidth,fClientIndex,result,uncertain);
  retireFlight(); return;
 }
 if (fArcRequest) {
  SweetDisplayPowerStatus &s=fClients[fClientIndex].displayStatus;
  s.result=result; s.uncertain=uncertain;
  s.state=result==kIOReturnSuccess ? SweetRPMhComplete : SweetRPMhFailed;
  if (result==kIOReturnSuccess) {
   fClients[fClientIndex].displayVote=fArcProposal; fArcAggregate=fArcNext;
   SWEET_LOG_INFO("SWEET RPMH: token=%llu display ARC ACTIVE ACK corner=%u cx=%u mx=%u mmcx=%u mask=%#x; physical state unverified\n",
       s.token,(unsigned)s.corner,s.levels[0],s.levels[1],s.levels[2],s.acknowledged);
  } else {
   fPoisoned|=uncertain;
   SWEET_LOG_ERROR("SWEET RPMH: token=%llu display ARC failed rc=%#x acked=%#x uncertain=%u; votes retained, no rollback/reset\n",
       s.token,(unsigned)result,s.acknowledged,uncertain);
  }
  settle(s.token,SweetAppsRequestDisplayCorner,fClientIndex,result,uncertain);
  retireFlight(); return;
 }
 SweetRPMhStatus &s=fClients[fClientIndex].status;
 s.result=result; s.uncertain=uncertain;
 s.state=result==kIOReturnSuccess ? SweetRPMhComplete : SweetRPMhFailed;
 if (result==kIOReturnSuccess) {
  fClients[fClientIndex].votes[fRail]=fProposal; fAggregate[fRail]=fTarget; fOwned[fRail]=true;
  SWEET_LOG_INFO("SWEET RPMH: token=%llu %s ACTIVE ACK enable=%u mv=%u mode=%u; physical state unverified\n",s.token,railNames[fRail],s.enabled,s.millivolts,s.mode);
 } else {
  fPoisoned|=uncertain;
  SWEET_LOG_ERROR("SWEET RPMH: token=%llu %s failed rc=%#x uncertain=%u; no reset/retry\n",s.token,railNames[fRail],(unsigned)result,uncertain);
 }
 settle(s.token,SweetAppsRequestRailVote,fClientIndex,result,uncertain);
 retireFlight();
}

/* One retirement path: the flight is over, the request that owned it is no
 * longer current, and the next waiting request may start. */
void SweetRPMh::retireFlight(void)
{
 fBusy=false; fCurrentValid=false;
 /* Nothing is in flight from here on, so the attempt's identity moves to the
  * retired slot: a completion that still arrives can be named as that
  * attempt's instead of being mistaken for whatever runs next. */
 fFlightToken=0; fFlightGeneration=0;
 if (fTimer) fTimer->cancelTimeout();
 /* The explicit ADT demonstration runs once this boot has actually completed
  * a transaction, because that is the first moment a rail it may re-vote is
  * owned. It submits through admit() like any client, so it cannot bypass the
  * ordering it is meant to demonstrate. */
 if (fQueueSelfTest && !fQueueTestRun && !fPoisoned && fAccepting) {
  if (queueSelfTest()) fQueueTestRun=true;
 }
 /* C3-06: three more moments, each one only after the previous one has actually
  * retired on hardware. Step 1 admits a trio whose last two members will never
  * be dispatched; step 2, reached when the first member completes, hands the
  * pair demonstration's ledger back, takes both waiting requests back (one by
  * its owner, one by its client leaving) and asks for the boot's own rate
  * again; step 3, reached when that lands, lets the last identity exit. All
  * three run through the same gate as any client, so none can bypass the
  * ordering or the refusals it is meant to demonstrate. */
 if (fQueueCancel && fQueueTestRun && !fPoisoned && fAccepting) {
  if (fCancelStep==0) {
   if (cancelSelfTest()) fCancelStep=1;
  } else if (fCancelStep==1) {
   if (cancelSelfTestCancel()) fCancelStep=2;
  } else if (fCancelStep==2) {
   if (cancelSelfTestFinish()) fCancelStep=3;
  }
 }
 /* C3-07: four more moments, each reachable only after the previous one really
  * landed on the array. It runs last, because it asks about attempts that have
  * already retired, and it asks for its own windows: a boot request that
  * intervenes is allowed to go first rather than being queued behind it. */
 if (fQuerySelfTest && !fPoisoned && fAccepting && fQueryPhase<4) querySelfTest();
 /* C3-08: last, because it describes the group the rail traffic of this boot
  * already submitted. It submits nothing itself: it asks this boot's own
  * identity how much room a group has, then asks the same gate for one command
  * too many. fIdentityReady is the gate that makes the answer real rather than
  * a guess - without an adopted array there is no capacity to report. */
 if (fGroupSelfTest && !fGroupTestRun && fIdentityReady && !fPoisoned && fAccepting) {
  groupSelfTest(); fGroupTestRun=true;
 }
 dispatchNext();
}

void SweetRPMh::advance(void)
{
 if (!fAccepting || !fBusy) return;
 if (fPanelFence.pins && (isInactive() || !panelFenceValid(fPanelFence))) {
  finish(kIOReturnNotReady,fExecuting || fWriteIssued || fOperation!=0); return;
 }
 uint64_t now=nowNS(); uint32_t b=fTCSIndex*Stride, bit=1u<<fTCSIndex;
 /* Terminal failure only: bounded, read-only evidence from our selected ACTIVE
  * slot. Sample before finish() retires the transaction; never run at stop,
  * after participant loss, on every timer tick or against other TCS payloads.
  * These sequential reads are evidence, not an atomic hardware snapshot. */
 auto diagnose=[&](const char *reason) {
  const char *phase=!fExecuting ? "program" : (fOperationCount ? "cleanup" : "completion");
  /* The following finish() record carries the consumer token/resource name.
   * Keep this common path independent of client/stage/payload array indexing. */
  SWEET_LOG_ERROR("SWEET RPMH SNAPSHOT: reason=%s phase=%s request=%s rail=%u arc-stage=%u tcs=%u commands=%u mask=%#x executing=%u irq-observed=%u written=%u operation=%u/%u posted=%u deadline-late-us=%llu stage-late-us=%llu write-late-us=%llu\n",
   reason,phase,fArcRequest ? "ARC" : (fRegisterRequest ? "CN0" : "VRM"),
   fRail,fArcRequest ? fArcStage : 0u,fTCSIndex,fCommandCount,fCommandMask,fExecuting,fIRQObserved,fEverWritten,
   fOperation,fOperationCount,fWriteIssued,now>fDeadline ? (now-fDeadline)/1000 : 0ULL,
   fArcRequest && now>fStageDeadline ? (now-fStageDeadline)/1000 : 0ULL,
   fWriteIssued && now>fWriteDeadline ? (now-fWriteDeadline)/1000 : 0ULL);
  SWEET_LOG_ERROR("SWEET RPMH SNAPSHOT: irq-enable=%#x saved-irq-enable=%#x irq-status=%#x tcs-status=%#x control=%#x enable=%#x wait=%#x\n",
   tcsRead(IRQEnable),fSavedIRQ,tcsRead(IRQStatus),tcsRead(b+Status),
   tcsRead(b+Control),tcsRead(b+Enable),tcsRead(b+Wait));
  /* VRM uses three commands, ARC/CN0 one. Refuse any diagnostic expansion
   * beyond those slots even if future request shapes grow. No W1C reads. */
  for (unsigned c=0;c<fCommandCount && c<3;++c) {
   uint32_t command=b+c*CommandStride;
   SWEET_LOG_ERROR("SWEET RPMH SNAPSHOT: command=%u msgid=%#x address=%#x data=%#x status=%#x\n",
    c,tcsRead(command+Message),tcsRead(command+Address),
    tcsRead(command+Data),tcsRead(command+CommandStatus));
  }
 };
 if (now>=fDeadline || (fArcRequest && now>=fStageDeadline)) {
  diagnose("deadline"); finish(kIOReturnTimeout,true); return;
 }
 /* Every store and every line step of this transaction goes through the one
  * published table, whichever side owns the hardware. A withdrawn table is a
  * stopped boot, not a reason to reach for a second MMIO path. */
 const SweetAppsDoor *door=executorDoor();
 if (!door) {
  /* The owner is gone: the flight is refused before its first byte, because the
   * alternative - writing around the table - is exactly the second MMIO path
   * this card removes. A store already in flight hits the refusal in tcsWrite. */
  SWEET_LOG_ERROR("SWEET RPMH: no executor door; flight refused, no second MMIO path\n");
  finish(kIOReturnNotReady,fEverWritten); return;
 }
 SweetRPMhExecutor::Ports ports={this,portRead,portWrite};
 if (!fExecuting && fOperation==0 && !fWriteIssued) {
  IOReturn result=door->triggerReady(door->context);
  if (result==kIOReturnNotReady) { fTimer->setTimeoutUS(100); return; }
  if (result!=kIOReturnSuccess) {
   SWEET_LOG_ERROR("SWEET RPMH IRQ: GIC/source admission failed rc=%#x; no trigger\n",(unsigned)result);
   finish(result,fEverWritten); return;
  }
  __atomic_store_n(&fIRQPosted,0u,__ATOMIC_RELEASE);
 }
 /* Bounded fixed operation count. A posted write not yet visible resumes on
  * the timer, never delay()/sleep or completion busy-wait inside the gate. */
 while (fOperation<fOperationCount) {
  SweetRPMhWord &op=fOperations[fOperation];
  if (!fWriteIssued) {
   /* Recheck ownership after masking and before first payload write. */
   if (fOperation==1 && !fExecuting && !emptyTCS(fTCSIndex)) { finish(kIOReturnBusy,true); return; }
   if (fOperation==1 && !fExecuting && fArcRequest) {
    unsigned selected=0;
    IOReturn result=selectArcTCS(&selected);
    if (result!=kIOReturnSuccess || selected!=fTCSIndex) {
     finish(result==kIOReturnSuccess ? kIOReturnBusy : result,true); return;
    }
   }
   if (fOperation==1 && !fExecuting && fRegisterRequest) {
    for (unsigned t=0;t<2;++t) {
     uint32_t enabled=tcsRead(t*Stride+Enable);
     if (enabled&~((1u<<fNCPT)-1u)) { finish(kIOReturnIOError,true); return; }
     for (unsigned c=0;c<fNCPT;++c)
      if ((enabled&(1u<<c)) && tcsRead(t*Stride+Address+c*CommandStride)==fBCM[SweetBCMCN0].address) {
       finish(kIOReturnBusy,true); return;
      }
    }
   }
   if (op.offset==IRQEnable) {
    uint32_t current=tcsRead(IRQEnable);
    if (current==~0u) { finish(kIOReturnIOError,fEverWritten); return; }
    op.value=fExecuting ? ((current&~bit)|(fSavedIRQ&bit)) :
        (fOperation ? (current|bit) : (current&~bit));
    if (!fExecuting && fOperation) fIRQSourceMask|=bit;
   }
   if (fPanelFence.pins && (isInactive() || !panelFenceValid(fPanelFence))) {
    finish(kIOReturnNotReady,fExecuting || fWriteIssued || fOperation!=0); return;
   }
   fEverWritten=true; fWriteDeadline=now+1000000;
  }
  /* The executor posts one word and reads it back; it never decides when to
   * give up, so the deadline and the diagnosis stay here. */
  uint32_t observed=0;
  SweetRPMhExecutor::Step step=SweetRPMhExecutor::step(ports,tcsLayout,op,&fWriteIssued,
   now,fWriteDeadline,&observed);
  if (step==SweetRPMhExecutor::StepExpired) {
   SWEET_LOG_ERROR("SWEET RPMH SNAPSHOT: posted offset=%#x want=%#x got=%#x w1c=%u\n",
    op.offset,op.value,observed,op.w1c);
   diagnose("posted-write"); finish(kIOReturnIOError,true); return;
  }
  if (step==SweetRPMhExecutor::StepBlocked) { fTimer->setTimeoutUS(100); return; }
  ++fOperation;
 }
 if (!fExecuting) {
  if (fPanelFence.pins && (isInactive() || !panelFenceValid(fPanelFence))) { finish(kIOReturnNotReady,true); return; }
  /* Source is enabled while GIC remains masked. The selected completion was
   * explicitly cleared while idle: require zero after unmask, BEFORE trigger.
   * This excludes masked stale status without accepting old CMD_STATUS. */
  if (SweetRPMhExecutor::arm(ports,tcsLayout,bit,NULL)!=SweetRPMhExecutor::ArmReady) {
   diagnose("stale-before-trigger"); finish(kIOReturnIOError,true); return;
  }
  fIRQObserved=false; fExecuting=true;
  fOperation=fOperationCount=0;
  door->arm(door->context);
  tcsWrite(b+Control,AMC|Trigger);
  /* Fast completion before enable stays pending on the level-high line. Do
   * not clear GIC/peripheral pending after trigger. Actual callback is required. */
  door->unmask(door->context);
  waitCompletion(); return;
 }
 if (fOperationCount) {
  if (fPanelFence.pins && (isInactive() || !panelFenceValid(fPanelFence))) { finish(kIOReturnNotReady,true); return; }
  completeTransaction(); return;
 }
 if (!fIRQObserved) { waitCompletion(); return; } /* watchdog only, no status polling */
 unsigned unacknowledged=0;
 SweetRPMhExecutor::Verdict verdict=SweetRPMhExecutor::verify(ports,tcsLayout,b,bit,
  fCommandCount,&unacknowledged);
 if (verdict==SweetRPMhExecutor::VerdictInvalid) {
  diagnose("invalid-completion"); finish(kIOReturnIOError,true); return;
 }
 if (verdict==SweetRPMhExecutor::VerdictIncomplete) {
  diagnose("incomplete-irq"); finish(kIOReturnIOError,true); return;
 }
 /* The mechanics moved, the hardware owner did not: say so once per boot. */
 if (!fExecutorReported) {
  fExecutorReported=true;
  SWEET_LOG_INFO("SWEET RPMH: executor=%s tcs=%u commands=%u open=%u hardware-owner=%s\n",
   fDelegated?"delegated":"module",fTCSIndex,fCommandCount,unacknowledged,
   fDelegated?"IOQcomRSCChannel":"SweetRPMh");
 }
 SWEET_LOG_INFO("SWEET RPMH IRQ: fresh ACTIVE completion tcs=%u commands=%u entries=%u; cleanup pending\n",
  fTCSIndex,fCommandCount,__atomic_load_n(&fIRQEntries,__ATOMIC_ACQUIRE));
 /* C3-08: the batch verdict above and the members are two statements, not one.
  * verify() reads every member's own status word, so a group that finished with
  * a member unacknowledged never reaches this row - it fails as a whole, with
  * that member named by the snapshot that follows. What this row adds is the
  * headcount: one token, one trigger, and every member accounted for. Only a
  * multi-command flight is a group; the single-command shapes are already fully
  * described by their own ACK row. */
 if (fCommandCount>1 && !fArcRequest && !fRegisterRequest)
  SWEET_LOG_INFO("SWEET RPMH GROUP: phase=done token=%llu kind=rail-vote count=%u acked=%u/%u wait=%#x enable=%#x; batch complete, members separate\n",
      fFlightToken,fCommandCount,fCommandCount-unacknowledged,fCommandCount,fCommandMask,fCommandMask);
 if (!SweetRPMhExecutor::stageCleanup(ports,tcsLayout,b,fOperations,&fOperationCount,AMC,bit,
  fSavedIRQ)) {
  finish(kIOReturnIOError,true); return;
 }
 fTimer->setTimeoutUS(1);
}

void SweetRPMh::timerAction(OSObject *owner,IOTimerEventSource *)
{ ((SweetRPMh *)owner)->advance(); }

IOReturn SweetRPMh::action(OSObject *owner,void *arg,void *,void *,void *)
{
 SweetRPMh *self=(SweetRPMh *)owner; RPMhRequest &r=*(RPMhRequest *)arg;
 if (r.op==Shutdown) {
  self->fAccepting=false; self->quietIRQ();
  /* Nothing queued may start after this point; each one fails against its own
   * token instead of being forgotten. */
  self->drainQueue(kIOReturnAborted,"stop");
  if (self->fBusy) self->finish(kIOReturnAborted,self->fEverWritten);
  if (self->fTimer) self->fTimer->cancelTimeout();
  return kIOReturnSuccess;
 }
 if (!self->fAccepting || self->isInactive()) return kIOReturnNotReady;
 if (r.op==Info) { *r.info=railLimits[r.rail]; r.info->available=true; return kIOReturnSuccess; }
 if (r.op==BCMInfo) {
  if (!self->lookupBCM(r.rail)) return kIOReturnNotFound;
  *r.bcmInfo=self->fBCM[r.rail]; return kIOReturnSuccess;
 }
 if (r.op==DisplayInfo) {
  *r.displayInfo={}; r.displayInfo->available=self->loadDisplayPower();
  for (unsigned c=0;c<SweetDisplayCornerCount;++c) {
   unsigned selectors[SweetDisplaySupplyCount]={};
   r.displayInfo->supported[c]=self->displayTargets((SweetDisplayCorner)c,selectors);
  }
  return kIOReturnSuccess;
 }
 if (r.op==Attach) {
  if (self->fNextClient==~0ULL) return kIOReturnNoResources;
  for (unsigned i=0;i<8;++i) if (!self->fClients[i].id) {
   self->fClients[i]={}; self->fClients[i].id=++self->fNextClient;
   *r.idOut=self->fClients[i].id; return kIOReturnSuccess;
  }
  return kIOReturnNoResources;
 }
 for (unsigned i=0;i<8;++i) if (r.client && self->fClients[i].id==r.client) {
  /* C3-06: an exiting client takes its waiting requests with it. A request the
   * machine has already triggered stays the provider's until it retires, and so
   * does this identity - the same refusal this path always gave, now only for
   * the work that is genuinely in flight. */
  if (r.op==Detach) return self->detachClient(i,NULL);
  if (r.op==Cancel) return self->cancelWaiting(i,r.token,"caller");
  if (r.op==Withdraw) return self->withdrawRegisterVote(i,r.previous,r.aggregate);
  if (r.op==Query) {
   if (!r.token || self->fClients[i].status.token!=r.token) return kIOReturnNotFound;
   *r.status=self->fClients[i].status; return kIOReturnSuccess;
  }
  if (r.op==DisplayQuery) {
   if (!r.token || self->fClients[i].displayStatus.token!=r.token) return kIOReturnNotFound;
   *r.displayStatus=self->fClients[i].displayStatus; return kIOReturnSuccess;
  }
  if (r.op==DisplayVote) {
   IOReturn result=self->beginDisplayPower(i,(SweetDisplayCorner)r.rail);
   if (result==kIOReturnSuccess) *r.idOut=self->fClients[i].displayStatus.token;
   return result;
  }
  if (r.op==RegisterQuery) {
   if (!r.token || self->fClients[i].registerStatus.token!=r.token) return kIOReturnNotFound;
   *r.registerStatus=self->fClients[i].registerStatus; return kIOReturnSuccess;
  }
  if (r.op==RegisterVote) {
   IOReturn result=self->beginRegisterBandwidth(i,r.bandwidth);
   if (result==kIOReturnSuccess) *r.idOut=self->fClients[i].registerStatus.token;
   return result;
  }
  if (r.op==PanelCancel) {
   if (!r.token || self->fClients[i].status.token!=r.token) return kIOReturnNotFound;
   if (self->fBusy && self->fClientIndex==i && self->fPanelFence.pins)
    self->finish(kIOReturnAborted,self->fExecuting || self->fWriteIssued || self->fOperation!=0);
   return kIOReturnSuccess;
  }
  if (r.op==PanelVote) {
   if (self->fMode!=2) return kIOReturnNotPermitted;
   if (self->fPoisoned) return kIOReturnNotReady;
   /* A panel vote is not queued: its fence hands over exclusive leaf-provider
    * references that would have to outlive the wait, which is the contract
    * C3-06 owns. It therefore refuses while anything is in flight or waiting,
    * instead of jumping ahead of requests that are already ordered. */
   if (self->fBusy || !self->fQueue.empty()) {
    SWEET_LOG_INFO("SWEET APPS QUEUE: phase=refuse kind=panel client=%u detail=not-a-queued-kind depth=%u\n",
        i,self->fQueue.depth());
    return kIOReturnBusy;
   }
   if (!self->panelFenceValid(*r.panelFence)) return kIOReturnNotReady;
   self->fPanelFence=*r.panelFence;
   self->fPanelFence.pins->retain(); self->fPanelFence.touch->retain();
  }
  IOReturn result=self->begin(i,r.rail,r.vote);
  if (result==kIOReturnSuccess) *r.idOut=self->fClients[i].status.token;
  if (r.op==PanelVote && (result!=kIOReturnSuccess || !self->fBusy)) self->clearPanelFence();
  return result;
 }
 return kIOReturnNotFound;
}

IOReturn SweetRPMh::attachConsumer(uint64_t *id)
{ if (!id) return kIOReturnBadArgument; *id=0; RPMhRequest r={}; r.op=Attach; r.idOut=id; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetRPMh::detachConsumer(uint64_t id)
{ RPMhRequest r={}; r.op=Detach; r.client=id; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetRPMh::resourceInfo(SweetRail rail,SweetRailInfo *info)
{ if ((unsigned)rail>=SweetRailCount || !info) return kIOReturnBadArgument; RPMhRequest r={}; r.op=Info; r.rail=rail; r.info=info; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetRPMh::requestVote(uint64_t id,SweetRail rail,const SweetRailVote &vote,uint64_t *token)
{ if ((unsigned)rail>=SweetRailCount || !token) return kIOReturnBadArgument; *token=0; RPMhRequest r={}; r.op=Vote; r.client=id; r.rail=rail; r.vote=vote; r.idOut=token; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetRPMh::requestPanelVote(uint64_t id,SweetRail rail,const SweetRailVote &vote,
    const SweetPanelSupplyFence &fence,uint64_t *token)
{
 if (!token) return kIOReturnBadArgument; *token=0;
 if (rail!=SweetRailVDDI && rail!=SweetRailVPNL) return kIOReturnNotPermitted;
 RPMhRequest r={}; r.op=PanelVote; r.client=id; r.rail=rail; r.vote=vote; r.panelFence=&fence; r.idOut=token;
 return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady;
}
IOReturn SweetRPMh::cancelPanelVote(uint64_t id,uint64_t token)
{ RPMhRequest r={}; r.op=PanelCancel; r.client=id; r.token=token; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetRPMh::copyStatus(uint64_t id,uint64_t token,SweetRPMhStatus *status)
{ if (!status) return kIOReturnBadArgument; RPMhRequest r={}; r.op=Query; r.client=id; r.token=token; r.status=status; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetRPMh::displayPowerInfo(SweetDisplayPowerInfo *info)
{ if (!info) return kIOReturnBadArgument; *info={}; RPMhRequest r={}; r.op=DisplayInfo; r.displayInfo=info; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetRPMh::requestDisplayPower(uint64_t id,SweetDisplayCorner corner,uint64_t *token)
{ if (!token) return kIOReturnBadArgument; *token=0; if ((unsigned)corner>=SweetDisplayCornerCount) return kIOReturnBadArgument; RPMhRequest r={}; r.op=DisplayVote; r.client=id; r.rail=(unsigned)corner; r.idOut=token; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetRPMh::copyDisplayPowerStatus(uint64_t id,uint64_t token,SweetDisplayPowerStatus *status)
{ if (!status) return kIOReturnBadArgument; RPMhRequest r={}; r.op=DisplayQuery; r.client=id; r.token=token; r.displayStatus=status; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetRPMh::copyBCMInfo(SweetDisplayBCM bcm,SweetBCMInfo *info)
{ if (!info || (unsigned)bcm>=SweetDisplayBCMCount) return kIOReturnBadArgument; *info=SweetBCMInfo(); RPMhRequest r={}; r.op=BCMInfo; r.rail=(unsigned)bcm; r.bcmInfo=info; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetRPMh::requestRegisterBandwidth(uint64_t id,uint64_t bandwidth,uint64_t *token)
{ if (!token) return kIOReturnBadArgument; *token=0; RPMhRequest r={}; r.op=RegisterVote; r.client=id; r.bandwidth=bandwidth; r.idOut=token; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetRPMh::copyRegisterBandwidthStatus(uint64_t id,uint64_t token,SweetRegisterBandwidthStatus *status)
{ if (!status) return kIOReturnBadArgument; RPMhRequest r={}; r.op=RegisterQuery; r.client=id; r.token=token; r.registerStatus=status; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }

IOReturn SweetRPMh::cancelRequest(uint64_t id,uint64_t token)
{
 if (!token) return kIOReturnBadArgument;
 RPMhRequest r={}; r.op=Cancel; r.client=id; r.token=token;
 return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady;
}
IOReturn SweetRPMh::withdrawRegisterBandwidth(uint64_t id,uint64_t *previous,uint64_t *aggregate)
{
 if (!previous || !aggregate) return kIOReturnBadArgument;
 *previous=*aggregate=0;
 RPMhRequest r={}; r.op=Withdraw; r.client=id; r.previous=previous; r.aggregate=aggregate;
 return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady;
}

void SweetRPMh::shutdown(void)
{
 if (fGateAdded) { RPMhRequest r={}; r.op=Shutdown; (void)fGate->runAction(action,&r); }
 stopIRQ();
 if (fTimerAdded) { fLoop->removeEventSource(fTimer); fTimerAdded=false; }
}
bool SweetRPMh::willTerminate(IOService *provider,IOOptionBits options)
{ shutdown(); return IOService::willTerminate(provider,options); }
void SweetRPMh::stop(IOService *provider)
{ shutdown(); IOService::stop(provider); }
void SweetRPMh::free(void)
{
 shutdown();
 for (unsigned i=0;i<8;++i) if (fClients[i].id)
  SWEET_LOG_WARN("SWEET RPMH: consumer %llu still attached; preserving votes, no automatic powerdown\n",fClients[i].id);
 if (fGateAdded) { fLoop->removeEventSource(fGate); fGateAdded=false; }
 OSSafeReleaseNULL(fIRQEvent); OSSafeReleaseNULL(fIRQMap);
 OSSafeReleaseNULL(fTimer); OSSafeReleaseNULL(fGate); OSSafeReleaseNULL(fLoop);
 for (unsigned i=0;i<4;++i) OSSafeReleaseNULL(fMaps[i]);
 if (fClaimed) { __atomic_store_n(&rpmhOwner,fEverWritten ? 2u : 0u,__ATOMIC_RELEASE); fClaimed=false; }
 IOService::free();
}
SweetRPMhClient *
SweetRPMhClient::withOwner(OSObject *owner, IOWorkLoop *loop, void (*changed)(OSObject *))
{
 if (!owner || !loop || !changed) return NULL;
 SweetRPMhClient *self=OSTypeAlloc(SweetRPMhClient);
 if (!self) return NULL;
 if (!self->init()) { self->release(); return NULL; }
 self->fOwner=owner; self->fChanged=changed; self->fLoop=loop; loop->retain();
 self->fLock=IOLockAlloc();
 self->fEvent=IOInterruptEventSource::interruptEventSource(self, SweetRPMhClient::changed);
 if (!self->fLock || !self->fEvent) { self->release(); return NULL; }
 self->fEvent->disable();
 if (loop->addEventSource(self->fEvent)!=kIOReturnSuccess) { self->release(); return NULL; }
 self->fEventAdded=true;
 return self;
}

bool
SweetRPMhClient::watch(void)
{
 OSDictionary *matching=IOService::serviceMatching("SweetRPMh");
 if (!matching) return false;
 fTerminated=IOService::addMatchingNotification(gIOTerminatedNotification,matching,terminated,this);
 if (fTerminated) fMatched=IOService::addMatchingNotification(gIOFirstMatchNotification,matching,matched,this);
 matching->release(); /* API retains the dictionary */
 if (!fTerminated || !fMatched) { stop(); return false; }
 fEvent->enable(); fEvent->interruptOccurred(NULL,NULL,0);
 return true;
}

bool
SweetRPMhClient::matched(void *target, void *, IOService *service, IONotifier *)
{
 SweetRPMhClient *self=(SweetRPMhClient *)target;
 SweetRPMh *backend=OSDynamicCast(SweetRPMh,service);
 if (!backend) return true;
 IOLockLock(self->fLock);
 if (!self->fStopping && !self->fBackend && !backend->isInactive()) {
  backend->retain(); self->fBackend=backend;
  self->fEvent->interruptOccurred(NULL,NULL,0);
 }
 IOLockUnlock(self->fLock);
 return true;
}

bool
SweetRPMhClient::terminated(void *target, void *, IOService *service, IONotifier *)
{
 SweetRPMhClient *self=(SweetRPMhClient *)target;
 SweetRPMh *old=NULL;
 IOLockLock(self->fLock);
 if (self->fBackend==service) {
  old=self->fBackend; self->fBackend=NULL;
  if (!self->fStopping) self->fEvent->interruptOccurred(NULL,NULL,0);
 }
 IOLockUnlock(self->fLock);
 /* No provider method or register access in notification context. In-flight
  * callers hold their own reference; provider checks inactive under its gate. */
 OSSafeReleaseNULL(old);
 return true;
}

void
SweetRPMhClient::changed(OSObject *owner, IOInterruptEventSource *, int)
{
 SweetRPMhClient *self=(SweetRPMhClient *)owner;
 IOLockLock(self->fLock); bool active=!self->fStopping; IOLockUnlock(self->fLock);
 if (active) self->fChanged(self->fOwner);
}

SweetRPMh *
SweetRPMhClient::copyBackend(void)
{
 IOLockLock(fLock);
 SweetRPMh *backend=!fStopping ? fBackend : NULL;
 if (backend) backend->retain();
 IOLockUnlock(fLock);
 return backend;
}

void
SweetRPMhClient::stop(void)
{
 if (fLock) { IOLockLock(fLock); fStopping=true; IOLockUnlock(fLock); }
 /* remove consumes the notifier and synchronizes callbacks. Never hold a
  * consumer lock/gate here: a replaying notification may be waiting on it. */
 if (fMatched) { fMatched->remove(); fMatched=NULL; }
 if (fTerminated) { fTerminated->remove(); fTerminated=NULL; }
 if (fEventAdded) {
  fEvent->disable(); fLoop->removeEventSource(fEvent); fEventAdded=false;
 }
 if (fLock) {
  IOLockLock(fLock); SweetRPMh *old=fBackend; fBackend=NULL; IOLockUnlock(fLock);
  OSSafeReleaseNULL(old);
 }
}

void
SweetRPMhClient::free(void)
{
 stop(); OSSafeReleaseNULL(fEvent); OSSafeReleaseNULL(fLoop);
 if (fLock) { IOLockFree(fLock); fLock=NULL; }
 OSObject::free();
}

#endif
