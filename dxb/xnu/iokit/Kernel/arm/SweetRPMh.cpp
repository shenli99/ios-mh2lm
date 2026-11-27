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
 SweetDisplayPowerInfo *displayInfo; SweetDisplayPowerStatus *displayStatus;
 SweetBCMInfo *bcmInfo; uint64_t bandwidth; SweetRegisterBandwidthStatus *registerStatus;
 const SweetPanelSupplyFence *panelFence;
};
enum { Attach, Detach, Info, Vote, Query, Shutdown, DisplayInfo, DisplayVote, DisplayQuery,
 BCMInfo, RegisterVote, RegisterQuery, PanelVote, PanelCancel };

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
 if (!self->fBusy || !self->fExecuting || self->fOperationCount ||
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

IOReturn SweetRPMh::beginRegisterBandwidth(unsigned client,uint64_t proposal)
{
 if (fMode!=2) return kIOReturnNotPermitted;
 if (fPoisoned) return kIOReturnNotReady;
 if (fBusy) return kIOReturnBusy;
 /* NotReady keeps the client's retry semantics, never polling. */
 if (!ensureExecutor()) return kIOReturnNotReady;
 if (!lookupBCM(SweetBCMCN0)) return kIOReturnNotReady;
 uint64_t target=proposal;
 for (unsigned i=0;i<8;++i) if (i!=client && fClients[i].id && fClients[i].registerVote>target)
  target=fClients[i].registerVote;
 unsigned y=0;
 if (!fBCM[SweetBCMCN0].scale(target,4,1,&y)) return kIOReturnBadArgument;
 if (fNextToken==~0ULL) return kIOReturnNoResources;
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
 SweetRegisterBandwidthStatus status={++fNextToken,target,SweetRPMhPending,kIOReturnSuccess,false};
 fClients[client].registerStatus=status;
 fClientIndex=client; fRegisterProposal=proposal; fRegisterTarget=target;
 fArcRequest=false; fRegisterRequest=true; fBusy=true;
 if (noWrite) { finish(kIOReturnSuccess,false); return kIOReturnSuccess; }
 fTCSIndex=chosen; fExecuting=false; fIRQObserved=false; fOperation=fOperationCount=0; fWriteIssued=false;
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
 if (fMode!=2) return kIOReturnNotPermitted;
 if (fPoisoned) return kIOReturnNotReady;
 if (!ensureExecutor()) return kIOReturnNotReady;
 if (fBusy) return kIOReturnBusy;
 if ((unsigned)proposal>=SweetDisplayCornerCount) return kIOReturnBadArgument;
 if (!loadDisplayPower()) return kIOReturnNotReady;
 SweetDisplayCorner aggregate=proposal;
 for (unsigned i=0;i<8;++i) if (i!=client && fClients[i].id && fClients[i].displayVote>aggregate)
  aggregate=fClients[i].displayVote;
 unsigned target[SweetDisplaySupplyCount]={};
 if (!displayTargets(aggregate,target)) return kIOReturnBadArgument;
 if (fNextToken==~0ULL) return kIOReturnNoResources;
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
 status.token=++fNextToken; status.state=SweetRPMhPending; status.corner=aggregate;
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

/* The tree decides the owner, not a boot arg: the budget is spent and both
 * sides must read the same answer. */
bool SweetRPMh::delegatedRequested(IOService *provider)
{
 OSData *flag=OSDynamicCast(OSData,provider->getProperty("sweet,rpmh-executor-delegated"));
 if (!flag || flag->getLength()!=4) return false;
 uint32_t value=0; memcpy(&value,flag->getBytesNoCopy(),4);
 return value==1u;
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
 registerService(); return true;
}

bool SweetRPMh::emptyTCS(unsigned t)
{
 uint32_t b=t*Stride, idle=tcsRead(b+Status);
 return idle && idle!=~0u && tcsRead(b+Control)==0 && tcsRead(b+Enable)==0 && tcsRead(b+Wait)==0 && !(tcsRead(IRQStatus)&(1u<<t));
}
void SweetRPMh::addOperation(uint32_t off,uint32_t value,bool w1c)
{ fOperations[fOperationCount++]={off,value,w1c}; }

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

IOReturn SweetRPMh::begin(unsigned client,unsigned rail,const SweetRailVote &proposal)
{
 if (fMode!=2) return kIOReturnNotPermitted;
 if (fPoisoned) return kIOReturnNotReady;
 if (!ensureExecutor()) return kIOReturnNotReady;
 if (fBusy) return kIOReturnBusy;
 SweetRailVote target={false,railLimits[rail].minMV,railLimits[rail].maxMV,0};
 if (proposal.enabled && (proposal.minMV>proposal.maxMV || proposal.minMV<railLimits[rail].minMV || proposal.maxMV>railLimits[rail].maxMV)) return kIOReturnBadArgument;
 uint64_t load=0;
 for (unsigned i=0;i<8;++i) if (fClients[i].id) {
  const SweetRailVote &v=i==client ? proposal : fClients[i].votes[rail];
  if (!v.enabled) continue;
  target.enabled=true;
  if (target.minMV<v.minMV) target.minMV=v.minMV;
  if (target.maxMV>v.maxMV) target.maxMV=v.maxMV;
  load+=v.loadUA;
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
 if (fNextToken==~0ULL) return kIOReturnNoResources;
 target.minMV=rounded;
 fArcRequest=false; fRegisterRequest=false; fClientIndex=client; fRail=rail; fProposal=proposal; fTarget=target;
 SweetRPMhStatus status={++fNextToken,SweetRPMhComplete,rail,rounded,
     load>=railLimits[rail].modeThresholdUA ? 7u : 4u,kIOReturnSuccess,target.enabled,false};
 /* Never disable an inherited rail simply because software has no votes. */
 if ((!target.enabled && !fOwned[rail]) || (fOwned[rail] && target.enabled==fAggregate[rail].enabled &&
     (!target.enabled || (target.minMV==fAggregate[rail].minMV &&
     (target.loadUA>=railLimits[rail].modeThresholdUA)==(fAggregate[rail].loadUA>=railLimits[rail].modeThresholdUA))))) {
  fClients[client].votes[rail]=proposal; fClients[client].status=status; fAggregate[rail]=target; return kIOReturnSuccess;
 }
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
 fClients[client].status=status; fClients[client].status.state=SweetRPMhPending;
 fBusy=true; fExecuting=false; fIRQObserved=false; fOperation=0; fOperationCount=0; fWriteIssued=false;
 uint32_t b=fTCSIndex*Stride, bit=1u<<fTCSIndex;
 addOperation(IRQEnable,fSavedIRQ&~bit); addOperation(IRQClear,bit,true);
 fCommandCount=target.enabled ? 3u : 1u; fCommandMask=(1u<<fCommandCount)-1u;
 const uint32_t offsets[]={0,8,4}, values[]={rounded,status.mode,1};
 for (unsigned c=0;c<fCommandCount;++c) {
  addOperation(b+Message+c*CommandStride,WriteACK);
  addOperation(b+Address+c*CommandStride,fAddresses[rail]+(target.enabled ? offsets[c] : 4));
  addOperation(b+Data+c*CommandStride,target.enabled ? values[c] : 0);
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
  fBusy=false; fTimer->cancelTimeout(); return;
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
  fBusy=false; fTimer->cancelTimeout(); return;
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
 fBusy=false; fTimer->cancelTimeout();
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
  if (r.op==Detach) {
   if (self->fBusy && self->fClientIndex==i) return kIOReturnBusy;
   for (unsigned rail=0;rail<SweetRailCount;++rail) if (self->fClients[i].votes[rail].enabled) return kIOReturnBusy;
   if (self->fClients[i].displayVote!=SweetDisplayOff || self->fClients[i].displayStatus.uncertain) return kIOReturnBusy;
   if (self->fClients[i].registerVote || self->fClients[i].registerStatus.uncertain) return kIOReturnBusy;
   self->fClients[i]={}; return kIOReturnSuccess;
  }
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
   if (self->fBusy) return kIOReturnBusy;
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
