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
 */
#include "IOQcomRSC.h"
#include <IOKit/IOLib.h>
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
rscRanges[kIOQcomRscRangeCount] = {
	{ "apps-drv",         0x18220000ULL, 0x100ULL  },
	{ "apps-tcs",         0x18220d00ULL, 0x3000ULL },
	{ "apps-db-locator",  0x0c3f000cULL, 8ULL      },
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
	/* No other property arms anything here. The irq description is mandatory
	 * for the same reason the ranges are: a channel that cannot say which line
	 * completes it is not the apps RSC, and saying it is how the next card
	 * avoids inventing a second owner. */
	return describeIRQ(provider);
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
	/* Description only. No GIC map, no enable, no route, no handler - the row
	 * says so in the field a reader would otherwise have to assume. */
	SWEET_LOG_INFO("SWEET RSC: phase=irq intid=%u detail=described-not-armed\n",fIntid);
	return true;
}

bool
IOQcomRSCChannel::mapWindows(IOService *provider)
{
	if (!provider||provider->getResources()!=kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET RSC: phase=map result=%d detail=resources-absent\n",kIOReturnNotReady);
		return false;
	}
	for (unsigned i=0;i<kIOQcomRscRangeCount;i++) {
		Window &w=fWindow[i];
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
	    !readRegister(kIOQcomRscWindowDB,kIOQcomRscDbHeaderBytes,&flags)) {
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
	/* Observed, not owned: the display owner owns both of these words for this
	 * boot, and this card must not mask, clear or acknowledge anything. The row
	 * exists so a ring shows the channel looked at a live owner rather than a
	 * quiet block. */
	SWEET_LOG_INFO("SWEET RSC: phase=tcs irq-enable=%#x irq-status=%#x detail=observed-not-owned\n",
	 enable,status);
	return true;
}

bool
IOQcomRSCChannel::readRegister(unsigned window,uint32_t offset,uint32_t *value)
{
	if (!value||window>=kIOQcomRscWindowCount||!fLock) return false;
	bool ok=false;
	uint32_t observed=0;
	IOLockLock(fLock);
	const Window &w=fWindow[window];
	if (w.base&&(offset&3u)==0&&(uint64_t)offset+sizeof(uint32_t)<=w.length) {
		observed=*(volatile uint32_t *)(w.base+offset);
		__asm__ volatile("dmb oshld" ::: "memory");
		ok=true;
		fReads++;
	} else {
		fReadFailures++;
	}
	IOLockUnlock(fLock);
	if (!ok) return false;
	*value=observed;
	return true;
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
	if (!observeTCS()) goto fail;
	/* Nothing below can write, so this is the whole published surface: an
	 * identity, a read count, and a channel that refuses to submit. */
	fIdentity.valid=true;
	fAccepting=true;
	setProperty("SweetRSCChannelABI",(unsigned long long)kIOQcomRscAbi,32);
	setProperty("SweetRSCChannelWrites",0ULL,32);
	setProperty("SweetRSCChannelSubmitAllowed",false);
	setProperty("SweetRSCChannelIRQ",(unsigned long long)fIntid,32);
	registerService();
	fRegistered=true;
	SWEET_LOG_INFO("SWEET RSC: phase=start result=0 windows=%u ranges=%u reads=%u writes=0 id=%#x tcs=%u ncpt=%u db=%#x db-size=%u irq=%u submit=not-ready\n",
	 kIOQcomRscWindowCount,kIOQcomRscRangeCount,fReads,fIdentity.id,
	 fIdentity.tcsCount,fIdentity.ncpt,fIdentity.dbAddress,fIdentity.dbBytes,fIntid);
	return true;
fail:
	fAccepting=false;
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
	SWEET_LOG_INFO("SWEET RSC: phase=stop result=0 reads=%u writes=0 submit-attempts=%u\n",
	 fReads,(unsigned)__atomic_load_n(&fSubmitAttempts,__ATOMIC_RELAXED));
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
	unmapWindows();
	if (fLock) {
		IOLockFree(fLock);
		fLock=NULL;
	}
	super::free();
}
#endif /* ARM64_BOARD_CONFIG_MH2LM */
