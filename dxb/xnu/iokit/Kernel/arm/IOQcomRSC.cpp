/* SM8150 apps RSC channel: C3-01, one read-only instance.
 *
 * Contract of this candidate, in one place:
 *  - start() maps the three ADT ranges (apps DRV, TCS array, command DB
 *    locator) through the provider nub and validates every resolved physical
 *    address and declared length before the first register read. The command
 *    DB is a fourth window, resolved from the locator and mapped read-only; it
 *    is not part of the reg ABI and cannot be pointed anywhere the locator does
 *    not name.
 *  - Register access is one load helper: bounds checked against the window,
 *    offset aligned, guarded by the channel lock, with the read barrier this
 *    SoC requires. This object has no store helper at all, so "writes=0" is a
 *    property of the file and not of a code path.
 *  - Nothing here claims the completion line. The GIC is not mapped, no
 *    interrupt source is registered, no workloop is created and no TCS or
 *    IRQ-enable word is written: the display owner keeps both the writes and
 *    the line, and this card only records what it saw.
 *  - Refusals are specific: ABI property, range count, address, length, DRV
 *    major/decoder, TCS count, NCPT, command DB address/size/magic, and the
 *    IRQ description each fail start with their own row. A tree that half
 *    matches is never read "as far as it goes".
 *  - The read budget is fixed and small: three DRV words, two locator words,
 *    three command DB words and the TCS enable/status pair - ten loads. The
 *    start row states the count, so a ring and this source can be reconciled
 *    without guessing.
 *  - C3-02 adds one query behind its own opt-in property and its own counter:
 *    the shared read-only command DB module (SweetRSCDatabase) looks CN0 up and
 *    the row reports class, address, entry/aux offsets and the validated
 *    unit/width/vcd beside the display owner's own row for the same resource.
 *    The module is the migration; this file only supplies the reader, so the
 *    query cannot reach memory the channel could not already read, and a query
 *    that fails is a WARN rather than a start refusal.
 */
#include "IOQcomRSC.h"
#include "SweetRSCDatabase.h"
#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <libkern/c++/OSData.h>
#include <string.h>
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"

#if defined(ARM64_BOARD_CONFIG_MH2LM)

#define super IOService

OSDefineMetaClassAndStructors(IOQcomRSCChannel,IOService);

/* The ADT reg order is the window ABI, exactly as the display owner declares
 * it. These are resolved physical addresses (vendor offsets plus the SM8150
 * RSC base are already applied in the LG DTS) and they are the only addresses
 * this channel accepts: a node pointing at another RSC is somebody else's
 * description and must not be read as this one. */
static const struct { const char *name; uint64_t physical; uint64_t length; }
rscRanges[kIOQcomRscExecutorRangeCount] = {
	{ "apps-drv",         0x18220000ULL, 0x100ULL  },
	{ "apps-tcs",         0x18220d00ULL, 0x3000ULL },
	{ "apps-db-locator",  0x0c3f000cULL, 8ULL      },
	/* C3-04 only: the distributor window the display node carried while it was
	 * the owner. Declared after the three RSC ranges, so the same table serves
	 * both modes and the range count alone says which one a ring booted. */
	{ "apps-gic",         (uint64_t)kIOQcomRscExecutorGicPhysical,
	  (uint64_t)kIOQcomRscExecutorGicLength },
};

/* The two TCS words this card looks at: word 0 is the interrupt-enable mask the
 * owner writes, word 4 is the status the RSC raises. Both are loads. */
enum : uint32_t { kIOQcomRscTcsEnable = 0x000, kIOQcomRscTcsStatus = 0x004 };

static const char *
rscWindowName(unsigned window)
{
	switch (window) {
	case kIOQcomRscWindowDrv: return rscRanges[0].name;
	case kIOQcomRscWindowTCS: return rscRanges[1].name;
	case kIOQcomRscWindowLocator: return rscRanges[2].name;
	case kIOQcomRscWindowDB: return "apps-command-db";
	case kIOQcomRscWindowGIC: return rscRanges[3].name;
	default: return "unknown";
	}
}

bool
IOQcomRSCChannel::validateProperties(IOService *provider)
{
	/* One opt-in property, versioned. An absent property means the tree did not
	 * ask for this channel and start is not even reached; a present but wrong
	 * one is a refusal, never a default. */
	OSData *abi=OSDynamicCast(OSData,provider?provider->getProperty("sweet,rsc-abi"):NULL);
	if (!abi||abi->getLength()!=4) {
		SWEET_LOG_ERROR("SWEET RSC: phase=start result=%d detail=bad-rsc-abi length=%u\n",
		 kIOReturnBadArgument,abi?(unsigned)abi->getLength():0u);
		return false;
	}
	uint32_t value=0;
	memcpy(&value,abi->getBytesNoCopy(),4);
	if (value!=kIOQcomRscAbi) {
		SWEET_LOG_ERROR("SWEET RSC: phase=start result=%d detail=bad-rsc-abi value=%#x expected=%#x\n",
		 kIOReturnBadArgument,value,kIOQcomRscAbi);
		return false;
	}
	/* C3-04, the third opt-in and the only one that changes who writes. It is
	 * validated here - before the line is described, not after - because both of
	 * the description rows have to say which role they describe for: an
	 * `described-not-armed` row on a boot that arms the line four steps later
	 * would be a true sentence about the wrong boot, and a ring is read as one
	 * sentence per row. A malformed request is still a refusal and never a
	 * silent read-only boot: the tree asked for an owner, and "probably
	 * read-only" is not an answer to that. */
	OSData *executor=OSDynamicCast(OSData,provider?provider->getProperty("sweet,rsc-executor"):NULL);
	if (executor) {
		if (executor->getLength()!=4) {
			SWEET_LOG_ERROR("SWEET RSC: phase=start result=%d detail=bad-rsc-executor length=%u\n",
			 kIOReturnBadArgument,(unsigned)executor->getLength());
			return false;
		}
		uint32_t wanted=0;
		memcpy(&wanted,executor->getBytesNoCopy(),4);
		if (wanted!=kIOQcomRscExecutorAbi) {
			SWEET_LOG_ERROR("SWEET RSC: phase=start result=%d detail=bad-rsc-executor value=%#x expected=%#x\n",
			 kIOReturnBadArgument,wanted,kIOQcomRscExecutorAbi);
			return false;
		}
		fExecutor=true;
	}
	/* No other property arms anything here. The irq description is mandatory
	 * for the same reason the ranges are: a channel that cannot say which line
	 * completes it is not the apps RSC, and saying it is how the next card
	 * avoids inventing a second owner. */
	if (!describeIRQ(provider)) return false;
	/* C3-02 is its own opt-in, and present means versioned. A tree that asked
	 * for the channel but not for the query keeps exactly the C3-01 read budget
	 * and no query row; a tree that asked with the wrong shape is refused
	 * rather than read "as far as it goes". */
	OSData *query=OSDynamicCast(OSData,provider?provider->getProperty("sweet,rsc-query"):NULL);
	if (query) {
		if (query->getLength()!=4) {
			SWEET_LOG_ERROR("SWEET RSC: phase=start result=%d detail=bad-rsc-query length=%u\n",
			 kIOReturnBadArgument,(unsigned)query->getLength());
			return false;
		}
		uint32_t wanted=0;
		memcpy(&wanted,query->getBytesNoCopy(),4);
		if (wanted!=kIOQcomRscQueryAbi) {
			SWEET_LOG_ERROR("SWEET RSC: phase=start result=%d detail=bad-rsc-query value=%#x expected=%#x\n",
			 kIOReturnBadArgument,wanted,kIOQcomRscQueryAbi);
			return false;
		}
		fQueryEnabled=true;
	}
	return true;
}

bool
IOQcomRSCChannel::describeIRQ(IOService *provider)
{
	OSData *abi=OSDynamicCast(OSData,provider?provider->getProperty("sweet,rsc-irq-abi"):NULL);
	if (!abi||abi->getLength()!=4) {
		SWEET_LOG_ERROR("SWEET RSC: phase=irq result=%d detail=bad-rsc-irq-abi length=%u\n",
		 kIOReturnBadArgument,abi?(unsigned)abi->getLength():0u);
		return false;
	}
	uint32_t version=0;
	memcpy(&version,abi->getBytesNoCopy(),4);
	if (version!=kIOQcomRscIrqAbi) {
		SWEET_LOG_ERROR("SWEET RSC: phase=irq result=%d detail=bad-rsc-irq-abi value=%#x\n",
		 kIOReturnBadArgument,version);
		return false;
	}
	OSData *intid=OSDynamicCast(OSData,provider?provider->getProperty("sweet,rsc-intid"):NULL);
	if (!intid||intid->getLength()!=4) {
		SWEET_LOG_ERROR("SWEET RSC: phase=irq result=%d detail=bad-rsc-intid length=%u\n",
		 kIOReturnBadArgument,intid?(unsigned)intid->getLength():0u);
		return false;
	}
	uint32_t line=0;
	memcpy(&line,intid->getBytesNoCopy(),4);
	if (line!=kIOQcomRscIntid) {
		SWEET_LOG_ERROR("SWEET RSC: phase=irq result=%d detail=bad-rsc-intid value=%u expected=%u\n",
		 kIOReturnBadArgument,line,kIOQcomRscIntid);
		return false;
	}
	fIntid=line;
	/* Description only. In the read-only mode there is no GIC map, no enable, no
	 * route and no handler, and the row says so in the field a reader would
	 * otherwise have to assume. In the executor mode those four steps happen
	 * later, in setupExecutor, so the row says that instead of claiming an
	 * absence this boot does not have. */
	SWEET_LOG_INFO("SWEET RSC: phase=irq intid=%u detail=%s\n",fIntid,
	 fExecutor?"described-for-executor":"described-not-armed");
	return true;
}

bool
IOQcomRSCChannel::mapWindows(IOService *provider)
{
	if (!provider||provider->getResources()!=kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET RSC: phase=map result=%d detail=resources-absent\n",kIOReturnNotReady);
		return false;
	}
	/* In the executor mode the tree declares one more range, the GIC window. It
	 * lands in its own window index so the command DB keeps index 3 and the
	 * read-only mode's window numbering is untouched. */
	const unsigned ranges=fExecutor?kIOQcomRscExecutorRangeCount:kIOQcomRscRangeCount;
	for (unsigned i=0;i<ranges;i++) {
		const unsigned index=(i<kIOQcomRscRangeCount)?i:kIOQcomRscWindowGIC;
		Window &w=fWindow[index];
		w.physical=rscRanges[i].physical;
		w.length=0;
		w.base=0;
		w.map=provider->mapDeviceMemoryWithIndex(i);
		if (!w.map) {
			SWEET_LOG_ERROR("SWEET RSC: phase=map resource=%s result=%d detail=window-absent\n",
			 rscRanges[i].name,kIOReturnNotReady);
			return false;
		}
		uint64_t observed=w.map->getPhysicalAddress();
		uint64_t length=w.map->getLength();
		if (observed!=rscRanges[i].physical) {
			SWEET_LOG_ERROR("SWEET RSC: phase=map resource=%s result=%d detail=address-mismatch observed=%#llx expected=%#llx\n",
			 rscRanges[i].name,kIOReturnBadArgument,(unsigned long long)observed,
			 (unsigned long long)rscRanges[i].physical);
			return false;
		}
		if (length<rscRanges[i].length) {
			SWEET_LOG_ERROR("SWEET RSC: phase=map resource=%s result=%d detail=length-short observed=%#llx required=%#llx\n",
			 rscRanges[i].name,kIOReturnBadArgument,(unsigned long long)length,
			 (unsigned long long)rscRanges[i].length);
			return false;
		}
		w.length=length;
		w.base=w.map->getVirtualAddress();
		if (!w.base) {
			SWEET_LOG_ERROR("SWEET RSC: phase=map resource=%s result=%d detail=no-virtual-base\n",
			 rscRanges[i].name,kIOReturnNotReady);
			return false;
		}
		SWEET_LOG_INFO("SWEET RSC: phase=map resource=%s result=0 length=%#llx\n",
		 rscRanges[i].name,(unsigned long long)length);
		if (index==kIOQcomRscWindowGIC) fGIC=w.base;
	}
	return true;
}

bool
IOQcomRSCChannel::readIdentity(void)
{
	uint32_t id=0, solver=0, config=0;
	if (!readRegister(kIOQcomRscWindowDrv,0,&id)||
	    !readRegister(kIOQcomRscWindowDrv,4,&solver)||
	    !readRegister(kIOQcomRscWindowDrv,12,&config)) {
		SWEET_LOG_ERROR("SWEET RSC: phase=identity result=%d detail=read-failed\n",kIOReturnNotReady);
		return false;
	}
	fIdentity.id=id;
	fIdentity.solver=solver;
	fIdentity.config=config;
	fIdentity.major=(id>>16)&255u;
	fIdentity.tcsCount=(config>>12)&63u;
	fIdentity.ncpt=(config>>27)&31u;
	SWEET_LOG_INFO("SWEET RSC: phase=identity id=%#x major=%u solver=%#x tcs=%u ncpt=%u\n",
	 id,fIdentity.major,solver,fIdentity.tcsCount,fIdentity.ncpt);
	/* A decoder word of all ones means the window answered nothing at all (the
	 * same refusal the display owner uses); a major other than 1/2 is a layout
	 * this firmware cannot parse. Both are refusals before any second read. */
	if (fIdentity.major<1||fIdentity.major>kIOQcomRscMaxMajor) {
		SWEET_LOG_ERROR("SWEET RSC: phase=identity result=%d detail=identity-version id=%#x major=%u\n",
		 kIOReturnBadArgument,id,fIdentity.major);
		return false;
	}
	if (solver==~0u) {
		SWEET_LOG_ERROR("SWEET RSC: phase=identity result=%d detail=identity-solver solver=%#x\n",
		 kIOReturnNotReady,solver);
		return false;
	}
	if (config==~0u) {
		SWEET_LOG_ERROR("SWEET RSC: phase=identity result=%d detail=identity-config config=%#x\n",
		 kIOReturnNotReady,config);
		return false;
	}
	if (fIdentity.tcsCount<kIOQcomRscTcsMinCount||fIdentity.tcsCount>kIOQcomRscTcsMaxCount) {
		SWEET_LOG_ERROR("SWEET RSC: phase=identity result=%d detail=tcs-count tcs=%u min=%u max=%u\n",
		 kIOReturnBadArgument,fIdentity.tcsCount,kIOQcomRscTcsMinCount,kIOQcomRscTcsMaxCount);
		return false;
	}
	if (fIdentity.ncpt<kIOQcomRscMinNcpt||fIdentity.ncpt>kIOQcomRscMaxNcpt) {
		SWEET_LOG_ERROR("SWEET RSC: phase=identity result=%d detail=ncpt ncpt=%u min=%u max=%u\n",
		 kIOReturnBadArgument,fIdentity.ncpt,kIOQcomRscMinNcpt,kIOQcomRscMaxNcpt);
		return false;
	}
	return true;
}

bool
IOQcomRSCChannel::mapCommandDB(void)
{
	uint32_t address=0, bytes=0;
	if (!readRegister(kIOQcomRscWindowLocator,0,&address)||
	    !readRegister(kIOQcomRscWindowLocator,4,&bytes)) {
		SWEET_LOG_ERROR("SWEET RSC: phase=command-db result=%d detail=locator-read-failed\n",
		 kIOReturnNotReady);
		return false;
	}
	fIdentity.dbAddress=address;
	fIdentity.dbBytes=bytes;
	/* The locator is firmware data, not a mapping hint: every field is checked
	 * before a descriptor is built from it. The physical range is the DRAM band
	 * the display owner already accepts, and the length is bounded by the same
	 * 1 MiB the owner refuses beyond. */
	if (address<kIOQcomRscDbMinPhysical||(address&3u)||(bytes&3u)||
	    bytes<kIOQcomRscDbHeaderBytes||bytes>kIOQcomRscDbMaxBytes||
	    (uint64_t)address+bytes>0x100000000ULL) {
		SWEET_LOG_ERROR("SWEET RSC: phase=command-db result=%d detail=command-db-geometry address=%#x bytes=%u\n",
		 kIOReturnBadArgument,address,bytes);
		return false;
	}
	/* Read-only descriptor: this mapping is a witness, and a direction is part
	 * of that promise - it is not "read-only for now". */
	IOMemoryDescriptor *descriptor=
	 IOMemoryDescriptor::withPhysicalAddress(address,bytes,kIODirectionIn);
	if (!descriptor) {
		SWEET_LOG_ERROR("SWEET RSC: phase=command-db result=%d detail=descriptor-absent\n",
		 kIOReturnNoMemory);
		return false;
	}
	Window &w=fWindow[kIOQcomRscWindowDB];
	w.map=descriptor->map();
	descriptor->release();
	if (!w.map||w.map->getPhysicalAddress()!=address||w.map->getLength()!=bytes) {
		SWEET_LOG_ERROR("SWEET RSC: phase=command-db result=%d detail=command-db-map-mismatch address=%#x bytes=%u\n",
		 kIOReturnNotReady,address,bytes);
		return false;
	}
	w.physical=address;
	w.length=bytes;
	w.base=w.map->getVirtualAddress();
	if (!w.base) {
		SWEET_LOG_ERROR("SWEET RSC: phase=command-db result=%d detail=no-virtual-base\n",
		 kIOReturnNotReady);
		return false;
	}
	uint32_t version=0, magic=0, flags=0;
	if (!readRegister(kIOQcomRscWindowDB,0,&version)||
	    !readRegister(kIOQcomRscWindowDB,4,&magic)||
	    !readRegister(kIOQcomRscWindowDB,kIOQcomRscDbFlagsOffset,&flags)) {
		SWEET_LOG_ERROR("SWEET RSC: phase=command-db result=%d detail=header-read-failed\n",
		 kIOReturnNotReady);
		return false;
	}
	fIdentity.dbVersion=version;
	fIdentity.dbFlags=flags;
	if (magic!=kIOQcomRscDbMagic) {
		SWEET_LOG_ERROR("SWEET RSC: phase=command-db result=%d detail=command-db-magic magic=%#x expected=%#x\n",
		 kIOReturnBadArgument,magic,kIOQcomRscDbMagic);
		return false;
	}
	SWEET_LOG_INFO("SWEET RSC: phase=command-db address=%#x bytes=%u version=%#x flags=%#x detail=read-only-map\n",
	 address,bytes,version,flags);
	if (version!=1||(flags&1u)) {
		/* Same two fields the display owner only warns about: this channel is a
		 * second reader of one firmware block and it must not be stricter than
		 * the voter that already accepts the boot. The layout it validated is
		 * still the compatible one, and the row says which. */
		SWEET_LOG_WARN("SWEET RSC: phase=command-db detail=version-flags-differ version=%#x flags=%#x; validated compatible layout only\n",
		 version,flags);
	}
	return true;
}

bool
IOQcomRSCChannel::observeTCS(void)
{
	uint32_t enable=0, status=0;
	if (!readRegister(kIOQcomRscWindowTCS,kIOQcomRscTcsEnable,&enable)||
	    !readRegister(kIOQcomRscWindowTCS,kIOQcomRscTcsStatus,&status)) {
		SWEET_LOG_ERROR("SWEET RSC: phase=tcs result=%d detail=read-failed\n",kIOReturnNotReady);
		return false;
	}
	fIdentity.irqEnable=enable;
	fIdentity.irqStatus=status;
	/* Observed, not owned *yet*: in the read-only mode the display owner keeps
	 * both of these words for the whole boot, and this card must not mask, clear
	 * or acknowledge anything. In the executor mode the channel reads them
	 * before it publishes the door and owns them from then on, so the row says
	 * it looked at a live block without claiming the words. Either way the row
	 * exists so a ring shows the channel read a live owner rather than a quiet
	 * block. */
	SWEET_LOG_INFO("SWEET RSC: phase=tcs irq-enable=%#x irq-status=%#x detail=%s\n",
	 enable,status,fExecutor?"observed-before-owner":"observed-not-owned");
	return true;
}

/* ---------------------------------------------------------------------------
 * C3-04: the executor mode.
 *
 * Everything below runs only when the tree carried sweet,rsc-executor, and it
 * is the whole of this boot's RSC write capability: one store helper, one GIC
 * line, one event source, one published door. The read-only mode above never
 * reaches any of it, so "writes=0 by code path" stays a structural fact rather
 * than a promise.
 * ------------------------------------------------------------------------ */

static uint64_t
rscNowNS(void)
{
	uint64_t now=0;
	absolutetime_to_nanoseconds(mach_absolute_time(),&now);
	return now;
}

/* The status word the line-admission step consults: any set bit means a
 * previous flight is still unacknowledged, which is a refusal, not a stall. */
static uint32_t
rscStatusThunk(void *context)
{
	IOQcomRSCChannel *channel=(IOQcomRSCChannel *)context;
	return channel?channel->tcsRead(kIOQcomRscTcsStatus):0u;
}

uint32_t
IOQcomRSCChannel::tcsRead(uint32_t offset)
{
	uint32_t value=~0u;
	if (!fExecutor||!fLock||!fWindow[kIOQcomRscWindowTCS].base) return ~0u;
	if (!readWindow(kIOQcomRscWindowTCS,offset,&value,&fDoorReads)) return ~0u;
	return value;
}

void
IOQcomRSCChannel::tcsWrite(uint32_t offset,uint32_t value)
{
	/* The only store in this file, and it exists only in the executor mode. A
	 * store without the mode is a programming error, so it is refused loudly
	 * rather than quietly ignored. */
	if (!fExecutor||!fLock) {
		SWEET_LOG_ERROR("SWEET RSC: phase=store result=%d detail=not-executor offset=%#x\n",
		 kIOReturnNotPermitted,offset);
		return;
	}
	bool ok=false;
	IOLockLock(fLock);
	Window &w=fWindow[kIOQcomRscWindowTCS];
	uint64_t end=(uint64_t)offset+sizeof(uint32_t);
	if (w.base&&(offset&3u)==0&&w.length>=sizeof(uint32_t)&&end<=w.length) {
		/* VM_WIMG_IO is not strongly ordered: the store must be published
		 * before the caller's read-back or the next trigger. */
		__asm__ volatile("dmb oshst" ::: "memory");
		*(volatile uint32_t *)(w.base+offset)=value;
		__asm__ volatile("dsb osh" ::: "memory");
		__atomic_add_fetch(&fStores,1u,__ATOMIC_RELAXED);
		ok=true;
	}
	IOLockUnlock(fLock);
	if (!ok) {
		SWEET_LOG_ERROR("SWEET RSC: phase=store result=%d detail=out-of-window offset=%#x value=%#x writes=%u\n",
		 kIOReturnBadArgument,offset,value,fStores);
	}
}

void
IOQcomRSCChannel::buildDoor(void)
{
	fDoor.name="IOQcomRSCChannel";
	fDoor.context=this;
	fDoor.read=doorRead;
	fDoor.write=doorWrite;
	fDoor.identity=doorIdentity;
	fDoor.triggerReady=doorTriggerReady;
	fDoor.arm=doorArm;
	fDoor.unmask=doorUnmask;
	fDoor.quiet=doorQuiet;
	fDoor.stop=doorStop;
	fDoor.irq=doorIrq;
	fDoor.install=doorInstall;
	fDoor.stores=doorStores;
	fDoor.irqs=doorIrqs;
}

bool
IOQcomRSCChannel::setupExecutor(IOService *provider)
{
	/* The line is taken before the door exists, and the door exists before the
	 * first store: a client that finds the door can therefore never find a line
	 * that is half-programmed. Geometry and claim are read-only checks, so a
	 * tree that describes an unsupported or already-live line fails here with
	 * no store, no event source and no door. */
	if (!fGIC) {
		SWEET_LOG_ERROR("SWEET RSC: phase=executor result=%d detail=gic-window-absent\n",
		 kIOReturnNotReady);
		return false;
	}
	if (!SweetAppsLine::geometry(fGIC)) {
		SWEET_LOG_ERROR("SWEET RSC: phase=executor result=%d detail=unsupported-gic\n",
		 kIOReturnUnsupported);
		return false;
	}
	if (!SweetAppsLine::claimable(fGIC)) {
		SWEET_LOG_ERROR("SWEET RSC: phase=executor result=%d detail=line-already-live\n",
		 kIOReturnBusy);
		return false;
	}
	fLoop=IOWorkLoop::workLoop();
	if (!fLoop) {
		SWEET_LOG_ERROR("SWEET RSC: phase=executor result=%d detail=no-workloop\n",
		 kIOReturnNoResources);
		return false;
	}
	fIRQEvent=IOInterruptEventSource::interruptEventSource(this,irqAction);
	if (!fIRQEvent||fLoop->addEventSource(fIRQEvent)!=kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET RSC: phase=executor result=%d detail=no-event-source\n",
		 kIOReturnNoResources);
		OSSafeReleaseNULL(fIRQEvent);
		return false;
	}
	fIRQEventAdded=true;
	buildDoor();
	if (!sweet_apps_door_publish(&fDoor)) {
		SWEET_LOG_ERROR("SWEET RSC: phase=executor result=%d detail=door-refused\n",
		 kIOReturnBusy);
		return false;
	}
	fDoorPublished=true;
	SWEET_LOG_INFO("SWEET RSC: phase=executor result=0 owner=%s intid=%u windows=%u ranges=%u stores=0 writes-at-start=0 workloop=own\n",
	 fDoor.name,fIntid,kIOQcomRscWindowCount,kIOQcomRscExecutorRangeCount);
	return true;
}

void
IOQcomRSCChannel::stopExecutor(void)
{
	if (!fExecutor) return;
	/* Withdraw first: after this returns no new store or IRQ can enter, and the
	 * display service sees an absent door instead of a line being dismantled. */
	__atomic_store_n(&fIRQOnline,0u,__ATOMIC_RELEASE);
	if (fDoorPublished) {
		sweet_apps_door_withdraw(&fDoor);
		fDoorPublished=false;
	}
	if (fIRQTouched&&fGIC) SweetAppsLine::mask(fGIC);
	if (fIRQEventAdded&&fLoop) {
		fIRQEvent->disable();
		fLoop->removeEventSource(fIRQEvent);
		fIRQEventAdded=false;
	}
	OSSafeReleaseNULL(fIRQEvent);
	/* The loop is this driver's own: it was created for the one event source
	 * above, so it is released here and not left for the boot to reclaim. */
	if (fLoop) {
		fLoop->release();
		fLoop=NULL;
	}
	fIRQConfigured=false;
	fIRQTouched=false;
	__atomic_store_n(&fCompletion,(bool (*)(void *))NULL,__ATOMIC_RELEASE);
	fCompletionTarget=NULL;
	SWEET_LOG_INFO("SWEET RSC: phase=executor-stop result=0 stores=%u irqs=%u door=withdrawn\n",
	 fStores,fIRQEntries);
}

IOReturn
IOQcomRSCChannel::lineTriggerReady(void)
{
	if (!fExecutor||!fGIC) return kIOReturnNotPermitted;
	if (fIRQConfigured) {
		if (!SweetAppsLine::verify(fGIC)) return kIOReturnIOError;
	} else {
		if (!fIRQTouched) {
			SweetAppsLine::Verdict program=SweetAppsLine::program(fGIC);
			if (program!=SweetAppsLine::LineReady) {
				return program==SweetAppsLine::LineForeign?kIOReturnBusy:kIOReturnIOError;
			}
			fIRQTouched=true;
			fIRQDeadline=rscNowNS()+1000000ULL;
		}
		/* RWP is an asynchronous distributor phase: the caller asks again with
		 * its own timer, this path never spins. */
		SweetAppsLine::Verdict retired=SweetAppsLine::retired(fGIC,rscNowNS(),fIRQDeadline);
		if (retired!=SweetAppsLine::LineReady) {
			return retired==SweetAppsLine::LineWait?kIOReturnNotReady:kIOReturnIOError;
		}
		fIRQConfigured=true;
		__atomic_store_n(&fIRQOnline,1u,__ATOMIC_RELEASE);
		setProperty("SweetRSCExecutorIRQ",(unsigned long long)fIntid,32);
		SWEET_LOG_INFO("SWEET RSC: phase=line result=0 intid=%u owner=channel; ACTIVE completion on own workloop, no status polling\n",
		 fIntid);
	}
	/* One trigger: no active generation of ours, and any pending bit either
	 * acknowledges a clear source or refuses the flight. An inherited source is
	 * never cleared on somebody else's behalf. */
	SweetAppsLine::Verdict admit=SweetAppsLine::admit(fGIC,rscNowNS(),
	 rscNowNS()+1000000ULL,rscStatusThunk,this,NULL);
	if (admit==SweetAppsLine::LineWait) return kIOReturnNotReady;
	if (admit==SweetAppsLine::LineForeign) return kIOReturnBusy;
	if (admit!=SweetAppsLine::LineReady) return kIOReturnIOError;
	return kIOReturnSuccess;
}

void
IOQcomRSCChannel::lineQuiet(uint32_t bit)
{
	/* Stop this completion from arriving again, and drop only our own source
	 * bit from the array mask. No peripheral ACK, no status write: the source
	 * word belongs to the client that armed it. */
	__atomic_store_n(&fIRQArmed,0u,__ATOMIC_RELEASE);
	if (fIRQTouched&&fGIC) SweetAppsLine::mask(fGIC);
	if (!bit) return;
	uint32_t enabled=tcsRead(kIOQcomRscTcsEnable);
	if (enabled==~0u) return;
	if (enabled&bit) tcsWrite(kIOQcomRscTcsEnable,enabled&~bit);
}

bool
IOQcomRSCChannel::handleIRQ(void)
{
	/* Top half, called from the one router with its dispatch lock held: mask,
	 * count, post. No lock allocation, no client code, no payload scan and no
	 * wait; the workloop thread decides what the completion means. */
	if (!__atomic_load_n(&fIRQOnline,__ATOMIC_ACQUIRE)) return true;
	if (fGIC) SweetAppsLine::mask(fGIC);
	__atomic_add_fetch(&fIRQEntries,1u,__ATOMIC_RELAXED);
	if (!__atomic_exchange_n(&fIRQArmed,0u,__ATOMIC_ACQ_REL)) return true;
	__atomic_store_n(&fIRQPosted,1u,__ATOMIC_RELEASE);
	if (fIRQEvent) fIRQEvent->interruptOccurred(NULL,NULL,0);
	return true;
}

void
IOQcomRSCChannel::irqAction(OSObject *owner,IOInterruptEventSource *,int)
{
	IOQcomRSCChannel *self=(IOQcomRSCChannel *)owner;
	if (!self||!__atomic_exchange_n(&self->fIRQPosted,0u,__ATOMIC_ACQ_REL)) return;
	/* The client's entry runs with no channel lock held: it takes its own gate,
	 * and may come straight back through the door to program the next flight. */
	bool (*completion)(void *)=__atomic_load_n(&self->fCompletion,__ATOMIC_ACQUIRE);
	void *target=self->fCompletionTarget;
	if (!completion||!target) {
		SWEET_LOG_WARN("SWEET RSC: phase=irq result=%d detail=no-completion-client entries=%u\n",
		 kIOReturnNotReady,self->fIRQEntries);
		return;
	}
	if (!completion(target)) {
		SWEET_LOG_WARN("SWEET RSC: phase=irq result=%d detail=client-refused entries=%u\n",
		 kIOReturnIOError,self->fIRQEntries);
	}
}

uint32_t
IOQcomRSCChannel::doorRead(void *context,uint32_t offset)
{
	IOQcomRSCChannel *channel=(IOQcomRSCChannel *)context;
	return channel?channel->tcsRead(offset):~0u;
}

void
IOQcomRSCChannel::doorWrite(void *context,uint32_t offset,uint32_t value)
{
	IOQcomRSCChannel *channel=(IOQcomRSCChannel *)context;
	if (channel) channel->tcsWrite(offset,value);
}

bool
IOQcomRSCChannel::doorIdentity(void *context,uint32_t *id,uint32_t *solver,
 uint32_t *config)
{
	IOQcomRSCChannel *channel=(IOQcomRSCChannel *)context;
	if (!channel||!channel->fIdentity.valid) return false;
	if (id) *id=channel->fIdentity.id;
	if (solver) *solver=channel->fIdentity.solver;
	if (config) *config=channel->fIdentity.config;
	return true;
}

IOReturn
IOQcomRSCChannel::doorTriggerReady(void *context)
{
	IOQcomRSCChannel *channel=(IOQcomRSCChannel *)context;
	return channel?channel->lineTriggerReady():kIOReturnNotPermitted;
}

void
IOQcomRSCChannel::doorArm(void *context)
{
	IOQcomRSCChannel *channel=(IOQcomRSCChannel *)context;
	if (channel) __atomic_store_n(&channel->fIRQArmed,1u,__ATOMIC_RELEASE);
}

void
IOQcomRSCChannel::doorUnmask(void *context)
{
	IOQcomRSCChannel *channel=(IOQcomRSCChannel *)context;
	if (channel&&channel->fGIC) SweetAppsLine::unmask(channel->fGIC);
}

void
IOQcomRSCChannel::doorQuiet(void *context,uint32_t bit)
{
	IOQcomRSCChannel *channel=(IOQcomRSCChannel *)context;
	if (channel) channel->lineQuiet(bit);
}

void
IOQcomRSCChannel::doorStop(void *context)
{
	IOQcomRSCChannel *channel=(IOQcomRSCChannel *)context;
	if (channel) channel->stopExecutor();
}

bool
IOQcomRSCChannel::doorIrq(void *context)
{
	IOQcomRSCChannel *channel=(IOQcomRSCChannel *)context;
	return channel?channel->handleIRQ():true;
}

bool
IOQcomRSCChannel::doorInstall(void *context,void *target,bool (*callback)(void *))
{
	IOQcomRSCChannel *channel=(IOQcomRSCChannel *)context;
	if (!channel||!channel->fExecutor||!callback||!target) return false;
	bool (*current)(void *)=__atomic_load_n(&channel->fCompletion,__ATOMIC_ACQUIRE);
	if (current) {
		if (current==callback&&channel->fCompletionTarget==target) return true;
		SWEET_LOG_WARN("SWEET RSC: phase=install result=%d detail=client-already-installed\n",
		 kIOReturnBusy);
		return false;
	}
	channel->fCompletionTarget=target;
	__atomic_store_n(&channel->fCompletion,callback,__ATOMIC_RELEASE);
	SWEET_LOG_INFO("SWEET RSC: phase=install result=0 detail=completion-client\n");
	return true;
}

unsigned
IOQcomRSCChannel::doorStores(void *context)
{
	IOQcomRSCChannel *channel=(IOQcomRSCChannel *)context;
	return channel?channel->fStores:0u;
}

unsigned
IOQcomRSCChannel::doorIrqs(void *context)
{
	IOQcomRSCChannel *channel=(IOQcomRSCChannel *)context;
	return channel?channel->fIRQEntries:0u;
}

bool
IOQcomRSCChannel::readWindow(unsigned window,uint32_t offset,uint32_t *value,
 unsigned *loads)
{
	if (!value||!loads||window>=kIOQcomRscWindowCount||!fLock) return false;
	bool ok=false;
	uint32_t observed=0;
	IOLockLock(fLock);
	const Window &w=fWindow[window];
	if (w.base&&(offset&3u)==0&&(uint64_t)offset+sizeof(uint32_t)<=w.length) {
		observed=*(volatile uint32_t *)(w.base+offset);
		__asm__ volatile("dmb oshld" ::: "memory");
		ok=true;
		(*loads)++;
	} else {
		fReadFailures++;
	}
	IOLockUnlock(fLock);
	if (!ok) return false;
	*value=observed;
	return true;
}

bool
IOQcomRSCChannel::readRegister(unsigned window,uint32_t offset,uint32_t *value)
{
	/* The identity budget is the card's promise: ten loads, whatever the DB
	 * holds. The query is charged to fQueryReads instead. */
	return readWindow(window,offset,value,&fReads);
}

bool
IOQcomRSCChannel::readDatabaseWord(uint32_t offset,uint32_t *value)
{
	return readWindow(kIOQcomRscWindowDB,offset,value,&fQueryReads);
}

bool
IOQcomRSCChannel::readDatabaseThunk(void *context,uint32_t offset,uint32_t *value)
{
	IOQcomRSCChannel *channel=(IOQcomRSCChannel *)context;
	return channel?channel->readDatabaseWord(offset,value):false;
}

void
IOQcomRSCChannel::queryCommandDB(void)
{
	fIdentity.queryDone=false;
	if (!fQueryEnabled) return;
	/* One resource, the one the display owner already resolves for the panel:
	 * CN0 is a class 5 bandwidth resource and its auxiliary block is the two
	 * words the owner's own INFO row prints. The answer here has to agree with
	 * that row field by field, which is what makes this a migration rather than
	 * a second interpretation of the DB. */
	SweetRSCResource resource={};
	unsigned loads=0;
	SweetRSCStage stage=SweetRSCStageNone;
	if (!SweetRSCDatabase::lookup(this,&IOQcomRSCChannel::readDatabaseThunk,
	    fIdentity.dbBytes,kIOQcomRscQueryName,kIOQcomRscQueryAuxMask,&resource,
	    &loads,&stage)) {
		/* Not a start refusal. The channel's evidence does not depend on the DB
		 * being readable, and the display owner is the voter that already
		 * accepts this boot. The row keeps "no such resource" apart from "could
		 * not read it" by naming the stage that refused. */
		SWEET_LOG_WARN("SWEET RSC: phase=db-query name=%s result=%d stage=%s reads=%u detail=query-refused\n",
		 kIOQcomRscQueryName,
		 (stage==SweetRSCStageMatches)?kIOReturnNotFound:kIOReturnNotReady,
		 SweetRSCDatabase::stageName(stage),loads);
		return;
	}
	fIdentity.queryDone=true;
	fIdentity.queryAddress=resource.address;
	fIdentity.querySlave=resource.slave;
	fIdentity.queryAuxBytes=resource.auxBytes;
	fIdentity.queryUnit=resource.unit;
	fIdentity.queryWidth=resource.width;
	fIdentity.queryVcd=resource.vcd;
	SWEET_LOG_INFO("SWEET RSC: phase=db-query name=%s result=0 slave=%u address=%#x entry=%#x aux=%#x/%u unit=%u width=%u vcd=%u matches=%u reads=%u\n",
	 kIOQcomRscQueryName,resource.slave,resource.address,resource.entry,
	 resource.aux,resource.auxBytes,resource.unit,resource.width,resource.vcd,
	 resource.matches,loads);
}

void
IOQcomRSCChannel::unmapWindows(void)
{
	for (unsigned i=0;i<kIOQcomRscWindowCount;i++) {
		OSSafeReleaseNULL(fWindow[i].map);
		fWindow[i].base=0;
		fWindow[i].length=0;
	}
	fMapped=false;
	fIdentity.valid=false;
}

bool
IOQcomRSCChannel::start(IOService *provider)
{
	if (!IOService::start(provider)) return false;
	fIdentity.valid=false;
	fIntid=0;
	fReads=0;
	fReadFailures=0;
	fSubmitAttempts=0;
	fQueryReads=0;
	fQueryEnabled=false;
	/* C3-04 state is initialised before any property is read: an early refusal
	 * must not leave a half-built door or event source behind for stop(). */
	fExecutor=false;
	fGIC=0;
	fLoop=NULL;
	fIRQEvent=NULL;
	fIRQEventAdded=false;
	fDoorPublished=false;
	fStores=0;
	fDoorReads=0;
	fIRQEntries=0;
	fIRQArmed=0;
	fIRQPosted=0;
	fIRQOnline=0;
	fIRQSources=0;
	fIRQTouched=false;
	fIRQConfigured=false;
	fIRQDeadline=0;
	fCompletionTarget=NULL;
	fCompletion=NULL;
	memset(&fDoor,0,sizeof(fDoor));
	fIdentity.queryDone=false;
	fIdentity.queryAddress=0;
	fIdentity.queryUnit=0;
	fIdentity.querySlave=0;
	fIdentity.queryWidth=0;
	fIdentity.queryVcd=0;
	fIdentity.queryAuxBytes=0;
	fLock=IOLockAlloc();
	if (!fLock) {
		SWEET_LOG_ERROR("SWEET RSC: phase=start result=%d detail=lock-absent\n",kIOReturnNoMemory);
		return false;
	}
	if (!validateProperties(provider)) goto fail;
	if (!mapWindows(provider)) goto fail;
	fMapped=true;
	if (!readIdentity()) goto fail;
	if (!mapCommandDB()) goto fail;
	queryCommandDB();
	if (!observeTCS()) goto fail;
	/* The owner decision is already made from the tree; this is the only place
	 * it turns into a line, a workloop and a door. */
	if (fExecutor&&!setupExecutor(provider)) goto fail;
	/* Nothing below can write, so this is the whole published surface: an
	 * identity, a read count, and a channel that refuses to submit. */
	fIdentity.valid=true;
	fAccepting=true;
	setProperty("SweetRSCChannelABI",(unsigned long long)kIOQcomRscAbi,32);
	setProperty("SweetRSCChannelWrites",(unsigned long long)fStores,32);
	setProperty("SweetRSCChannelExecutor",fExecutor);
	setProperty("SweetRSCChannelSubmitAllowed",false);
	setProperty("SweetRSCChannelIRQ",(unsigned long long)fIntid,32);
	registerService();
	fRegistered=true;
	SWEET_LOG_INFO("SWEET RSC: phase=start result=0 windows=%u ranges=%u reads=%u query-reads=%u writes=%u id=%#x tcs=%u ncpt=%u db=%#x db-size=%u irq=%u query=%s submit=not-ready executor=%s\n",
	 fExecutor?kIOQcomRscWindowCount:kIOQcomRscMappedWindowCount,
	 fExecutor?kIOQcomRscExecutorRangeCount:kIOQcomRscRangeCount,
	 fReads,fQueryReads,fStores,fIdentity.id,
	 fIdentity.tcsCount,fIdentity.ncpt,fIdentity.dbAddress,fIdentity.dbBytes,fIntid,
	 !fQueryEnabled?"absent":(fIdentity.queryDone?"ok":"missing"),
	 fExecutor?"channel":"absent");
	return true;
fail:
	fAccepting=false;
	stopExecutor();
	unmapWindows();
	if (fLock) {
		IOLockFree(fLock);
		fLock=NULL;
	}
	SWEET_LOG_ERROR("SWEET RSC: phase=start result=%d detail=refused reads=%u writes=0\n",
	 kIOReturnBadArgument,fReads);
	return false;
}

IOReturn
IOQcomRSCChannel::submit(uint64_t client,uint32_t address,uint32_t value,uint64_t *token)
{
	/* The token is cleared before anything can fail, so a caller that ignores
	 * the return value still cannot mistake "no transaction" for "accepted". */
	if (token) *token=0;
	__atomic_add_fetch(&fSubmitAttempts,1u,__ATOMIC_RELAXED);
	if (!fAccepting) {
		SWEET_LOG_WARN("SWEET RSC: phase=submit result=%d detail=channel-not-accepting client=%#llx\n",
		 kIOReturnNotReady,(unsigned long long)client);
		return kIOReturnNotReady;
	}
	/* NotReady is the answer this card ships. The address and value are logged
	 * so a consumer that ignores the refusal is visible in the ring, and the
	 * writes field restates that nothing followed. */
	SWEET_LOG_INFO("SWEET RSC: phase=submit result=%d detail=read-only-channel client=%#llx address=%#x value=%#x writes=0\n",
	 kIOReturnNotReady,(unsigned long long)client,address,value);
	return kIOReturnNotReady;
}

void
IOQcomRSCChannel::stop(IOService *provider)
{
	fAccepting=false;
	stopExecutor();
	SWEET_LOG_INFO("SWEET RSC: phase=stop result=0 reads=%u writes=%u submit-attempts=%u executor=%s\n",
	 fReads,fStores,(unsigned)__atomic_load_n(&fSubmitAttempts,__ATOMIC_RELAXED),
	 fExecutor?"channel":"absent");
	unmapWindows();
	if (fLock) {
		IOLockFree(fLock);
		fLock=NULL;
	}
	fRegistered=false;
	super::stop(provider);
}

void
IOQcomRSCChannel::free(void)
{
	stopExecutor();
	unmapWindows();
	if (fLock) {
		IOLockFree(fLock);
		fLock=NULL;
	}
	super::free();
}
#endif /* ARM64_BOARD_CONFIG_MH2LM */
