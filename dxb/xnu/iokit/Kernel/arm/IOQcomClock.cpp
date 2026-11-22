/* SM8150 unified clock entry, read-only candidate C1-01..C1-04.
 *
 * Contract of this candidate, in one place:
 *  - The entry is a single IOService matched from an opt-in ADT node
 *    (sweet,clock-provider). start() builds locks/event resources only; it
 *    never waits for a controller and never maps a controller window.
 *  - Controllers register their identity and lifetime with the entry. Their
 *    resource tables, windows, rates and register access stay inside each CC
 *    backend; the entry stores no resource and performs no MMIO.
 *  - A consumer resolves a stable name through its own sweet,clock-names /
 *    sweet,clocks properties. Binding is read-only: it records a
 *    (controller, local ID, generation) triple and retains it for the life of
 *    the handle; withdrawal invalidates every earlier generation.
 *  - Descriptors declare possible/selected parents, register-access
 *    dependencies, required supply domains and rate-to-corner ceilings. The
 *    admission walk proves those declarations against what is registered right
 *    now; it does not power, vote, set a rate or reset anything.
 *  - Every hardware operation is Unsupported in this candidate, from the base
 *    controller default and from the capability gate, whichever applies.
 *
 * Zero register writes: this file contains no device-memory access at all. */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "IOQcomClock.h"
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include <string.h>
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"

/* XNU does not provide a super alias; every in-kernel IOService subclass
 * declares it next to its metaclass definitions. */
#define super IOService

OSDefineMetaClassAndAbstractStructors(IOQcomClockController,IOService);
OSDefineMetaClassAndStructors(IOQcomClockHandle,OSObject);
OSDefineMetaClassAndStructors(IOQcomClockProvider,IOService);

enum : unsigned { kIOQcomLogBuf=40, kIOQcomNameMax=32, kIOQcomNameBlobMax=8*(kIOQcomNameMax+1),
 kIOQcomPairBytes=8 };

static const char *clockDepName(uint32_t reason)
{
	switch (reason) {
	case kIOQcomDepReady: return "ready";
	case kIOQcomDepControllerAbsent: return "controller-absent";
	case kIOQcomDepParentController: return "parent-controller-absent";
	case kIOQcomDepParentClock: return "parent-clock-absent";
	case kIOQcomDepCycle: return "cycle";
	case kIOQcomDepDomain: return "domain-absent";
	case kIOQcomDepAccess: return "access-absent";
	case kIOQcomDepUnknownParent: return "parent-unknown";
	case kIOQcomDepDepth: return "depth";
	default: return "malformed";
	}
}

static void clockResource(char *buffer,unsigned size,uint32_t controller,uint32_t localID)
{
	snprintf(buffer,size,"cc%u:0x%x",controller,localID);
}

static const char *clockConsumerName(IOService *service)
{
	const char *name=service?service->getName():NULL;
	return name?name:"consumer?";
}

static uint32_t clockLE32(const unsigned char *bytes)
{
	return (uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
}

/* Structural validation of one descriptor. Identity, kind, halt semantics,
 * parent shape and the capability vocabulary are the whole contract a
 * controller may publish in this candidate. */
static bool clockDescriptorValid(const IOQcomClockInfo *info)
{
	const uint32_t known=kIOQcomCapQueryConfig|kIOQcomCapQueryRate|kIOQcomCapEnable|
	 kIOQcomCapDisable|kIOQcomCapSetRate|kIOQcomCapReset;
	if (info->kind!=kIOQcomClockBranch&&info->kind!=kIOQcomClockRCG) return false;
	if (info->halt!=kIOQcomHaltCheck&&info->halt!=kIOQcomHaltDelay) return false;
	if (info->possibleCount>kIOQcomMaxParents) return false;
	if (!(info->capabilities&kIOQcomCapQueryConfig)||(info->capabilities&~known)) return false;
	for (unsigned i=0;i<info->possibleCount;i++) {
		const IOQcomClockParent &parent=info->possible[i];
		if (parent.source>kIOQcomSourceUnknown) return false;
		if (parent.source==kIOQcomSourceNone) return false;
		if (parent.source==kIOQcomSourceLocal&&
		 (parent.controller==0||parent.controller>=kIOQcomCCMax||parent.localID==0)) return false;
	}
	return true;
}

/* sweet,clock-names is a concatenation of NUL-terminated UTF-8 strings and
 * sweet,clocks is the parallel little-endian (controller, local ID) u32 pair
 * table. Both live on the consumer's nub; the entry never guesses. */
struct IOQcomClockBinding { uint32_t controller, localID; };

static IOReturn clockParseBindings(IOService *consumer,const char *name,
 IOQcomClockBinding *items,unsigned *countOut,unsigned *matchOut)
{
	unsigned starts[kIOQcomBindingMax], lengths[kIOQcomBindingMax];
	if (!consumer||!name||!*name) return kIOReturnBadArgument;
	size_t length=0;
	while (name[length]) {
		if (++length>=kIOQcomNameMax) return kIOReturnBadArgument;
	}
	OSObject *namesObject=consumer->getProperty("sweet,clock-names");
	OSObject *clocksObject=consumer->getProperty("sweet,clocks");
	if (!namesObject||!clocksObject) return kIOReturnNotFound;
	OSData *names=OSDynamicCast(OSData,namesObject);
	OSData *clocks=OSDynamicCast(OSData,clocksObject);
	if (!names||!clocks) return kIOReturnBadArgument;
	const unsigned char *nameBytes=(const unsigned char *)names->getBytesNoCopy();
	const unsigned char *pairBytes=(const unsigned char *)clocks->getBytesNoCopy();
	unsigned nameLength=names->getLength();
	if (!nameBytes||nameLength==0||nameLength>kIOQcomNameBlobMax||!pairBytes) return kIOReturnBadArgument;
	unsigned count=0,start=0;
	for (unsigned i=0;i<nameLength;i++) {
		if (nameBytes[i]) continue;
		unsigned entryLength=i-start;
		if (entryLength==0||entryLength>=kIOQcomNameMax) return kIOReturnBadArgument;
		if (count>=kIOQcomBindingMax) return kIOReturnBadArgument;
		starts[count]=start;
		lengths[count]=entryLength;
		count++;
		start=i+1;
	}
	if (start!=nameLength) return kIOReturnBadArgument;
	if (count==0||clocks->getLength()!=count*kIOQcomPairBytes) return kIOReturnBadArgument;
	for (unsigned i=0;i<count;i++) {
		items[i].controller=clockLE32(pairBytes+i*kIOQcomPairBytes);
		items[i].localID=clockLE32(pairBytes+i*kIOQcomPairBytes+4);
		if (items[i].controller==0||items[i].controller>=kIOQcomCCMax||
		 items[i].localID==0) return kIOReturnBadArgument;
	}
	/* Duplicate names would make one request select two clocks: reject the
	 * whole table rather than pick one silently. */
	for (unsigned i=0;i<count;i++) {
		for (unsigned j=i+1;j<count;j++) {
			if (lengths[i]==lengths[j]&&!memcmp(nameBytes+starts[i],nameBytes+starts[j],lengths[i]))
				return kIOReturnBadArgument;
		}
	}
	unsigned match=count;
	for (unsigned i=0;i<count;i++) {
		if (!strncmp((const char *)nameBytes+starts[i],name,kIOQcomNameMax)) { match=i; break; }
	}
	if (match==count) return kIOReturnNotFound;
	*countOut=count;
	*matchOut=match;
	return kIOReturnSuccess;
}

IOReturn IOQcomClockController::enableClock(uint32_t,uint64_t)
{
	return kIOReturnUnsupported;
}

IOReturn IOQcomClockController::disableClock(uint32_t,uint64_t)
{
	return kIOReturnUnsupported;
}

/* ------------------------------------------------------------------ handle */

void IOQcomClockHandle::free(void)
{
	IOQcomClockProvider *provider=fProvider;
	IOService *consumer=fConsumer;
	fProvider=NULL;
	fConsumer=NULL;
	if (provider) {
		if (provider->fLock) {
			IOLockLock(provider->fLock);
			if (provider->fHandles) provider->fHandles--;
			IOLockUnlock(provider->fLock);
		}
		provider->release();
	}
	OSSafeReleaseNULL(consumer);
	OSObject::free();
}

IOReturn IOQcomClockHandle::copyInfo(IOQcomClockInfo *info)
{
	if (!info||!fProvider) return kIOReturnBadArgument;
	IOQcomClockController *controller=NULL;
	IOReturn result=fProvider->retainController(fController,fGeneration,&controller,NULL);
	if (result!=kIOReturnSuccess) return result;
	memset(info,0,sizeof *info);
	result=controller->copyClockInfo(fLocalID,info);
	controller->release();
	return result;
}

IOReturn IOQcomClockHandle::copyState(IOQcomClockState *state)
{
	if (!state||!fProvider) return kIOReturnBadArgument;
	IOQcomClockController *controller=NULL;
	IOReturn result=fProvider->retainController(fController,fGeneration,&controller,NULL);
	if (result!=kIOReturnSuccess) return result;
	memset(state,0,sizeof *state);
	result=controller->copyClockState(fLocalID,state);
	controller->release();
	return result;
}

/* Admission precedes the capability gate: a clock whose declared dependency is
 * missing reports NotReady even when its backend claims Enable. The backend is
 * only reached after admission passes and it owns the final refusal. */
IOReturn IOQcomClockHandle::enable(void)
{
	if (!fProvider) return kIOReturnBadArgument;
	IOQcomClockController *controller=NULL;
	IOReturn result=fProvider->retainController(fController,fGeneration,&controller,NULL);
	if (result!=kIOReturnSuccess) return result;
	IOQcomClockInfo info;
	char resource[kIOQcomLogBuf];
	clockResource(resource,sizeof resource,fController,fLocalID);
	result=fProvider->admit(controller,fLocalID,clockConsumerName(fConsumer),&info,NULL,true);
	if (result==kIOReturnSuccess) {
		if (!(info.capabilities&kIOQcomCapEnable)) {
			result=kIOReturnUnsupported;
			SWEET_LOG_INFO("SWEET CLOCK: planID=C1 resource=%s consumer=%s token=%llu generation=%llu phase=enable result=%d detail=capability-absent\n",
			 resource,clockConsumerName(fConsumer),(unsigned long long)fToken,
			 (unsigned long long)fGeneration,result);
		} else {
			result=controller->enableClock(fLocalID,fToken);
			SWEET_LOG_INFO("SWEET CLOCK: planID=C1 resource=%s consumer=%s token=%llu generation=%llu phase=enable result=%d\n",
			 resource,clockConsumerName(fConsumer),(unsigned long long)fToken,
			 (unsigned long long)fGeneration,result);
		}
	}
	controller->release();
	return result;
}

/* Disabling removes a requirement, so it needs register access only: a missing
 * parent or supply never blocks turning a clock off. */
IOReturn IOQcomClockHandle::disable(void)
{
	if (!fProvider) return kIOReturnBadArgument;
	IOQcomClockController *controller=NULL;
	IOReturn result=fProvider->retainController(fController,fGeneration,&controller,NULL);
	if (result!=kIOReturnSuccess) return result;
	IOQcomClockInfo info;
	uint32_t reason=kIOQcomDepReady;
	char resource[kIOQcomLogBuf];
	clockResource(resource,sizeof resource,fController,fLocalID);
	result=fProvider->admit(controller,fLocalID,clockConsumerName(fConsumer),&info,&reason,false);
	if (result==kIOReturnSuccess) {
		if (info.pinned) {
			result=kIOReturnNotPermitted;
			SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=%s token=%llu generation=%llu phase=disable result=%d detail=pinned-clock\n",
			 resource,clockConsumerName(fConsumer),(unsigned long long)fToken,
			 (unsigned long long)fGeneration,result);
		} else if (!(info.capabilities&kIOQcomCapDisable)) {
			result=kIOReturnUnsupported;
			SWEET_LOG_INFO("SWEET CLOCK: planID=C1 resource=%s consumer=%s token=%llu generation=%llu phase=disable result=%d detail=capability-absent\n",
			 resource,clockConsumerName(fConsumer),(unsigned long long)fToken,
			 (unsigned long long)fGeneration,result);
		} else {
			result=controller->disableClock(fLocalID,fToken);
			SWEET_LOG_INFO("SWEET CLOCK: planID=C1 resource=%s consumer=%s token=%llu generation=%llu phase=disable result=%d\n",
			 resource,clockConsumerName(fConsumer),(unsigned long long)fToken,
			 (unsigned long long)fGeneration,result);
		}
	}
	controller->release();
	return result;
}

/* ---------------------------------------------------------------- provider */

/* The entry never reads its own nub's name table: bindings are declared by
 * consumers. Report what this node declares so a boot that pins something is
 * not confused with one that pins nothing; the count is the only honest
 * answer here, a capacity constant would look like a successful bind. */
static unsigned clockDeclaredBindings(IOService *service)
{
	OSData *names=service?OSDynamicCast(OSData,service->getProperty("sweet,clock-names")):NULL;
	if (!names) return 0;
	const unsigned char *bytes=(const unsigned char *)names->getBytesNoCopy();
	unsigned length=names->getLength(),count=0;
	if (!bytes||!length) return 0;
	for (unsigned i=0;i<length;i++) if (bytes[i]==0) count++;
	return count;
}

/* sweet,clock-diag=1 on the matched node turns the diagnostic backend on. The
 * boot argument path stays for images that have room for it, but this device's
 * firmware rejects an argument list past 607 bytes and the shipped list
 * already takes 605, so the device tree is the only usable request here. A
 * property that is present but malformed is a configuration error, not a
 * silent no. */
static bool clockDiagRequested(IOService *service)
{
	int mode=0;
	if (PE_parse_boot_argn("sweetclock",&mode,sizeof mode)&&mode) return true;
	OSData *property=service?OSDynamicCast(OSData,service->getProperty("sweet,clock-diag")):NULL;
	if (!property) return false;
	const void *bytes=property->getBytesNoCopy();
	if (property->getLength()==4&&bytes&&!memcmp(bytes,"\1\0\0\0",4)) return true;
	SWEET_LOG_WARN("SWEET CLOCK: planID=C1 phase=start result=%d detail=bad-diag-property\n",
	 kIOReturnBadArgument);
	return false;
}

bool IOQcomClockProvider::start(IOService *provider)
{
	if (fLock||fReady) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 phase=start result=%d detail=already-started\n",
		 kIOReturnBusy);
		return false;
	}
	if (!provider||!super::start(provider)) return false;
	OSData *abi=OSDynamicCast(OSData,provider->getProperty("sweet,clock-abi"));
	if (!abi||abi->getLength()!=4||memcmp(abi->getBytesNoCopy(),"\1\0\0\0",4)) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C1 phase=start result=%d detail=missing-clock-abi\n",
		 kIOReturnBadArgument);
		return false;
	}
	fLock=IOLockAlloc();
	if (!fLock) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C1 phase=start result=%d detail=no-lock\n",
		 kIOReturnNoMemory);
		return false;
	}
	for (unsigned id=0;id<kIOQcomCCMax;id++) {
		fSlots[id].controller=NULL;
		fSlots[id].generation=0;
		fSlots[id].active=false;
	}
	fNextGeneration=1;
	fNextToken=0;
	fHandles=0;
	fStopping=false;
	fDiagCall=thread_call_allocate(diagnose,this);
	if (!fDiagCall) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C1 phase=start result=%d detail=no-diagnostic-call\n",
		 kIOReturnNoMemory);
		IOLockFree(fLock);
		fLock=NULL;
		return false;
	}
	fReady=true;
	SWEET_LOG_INFO("SWEET CLOCK: planID=C1 phase=start result=0 detail=entry-ready bindings=%u\n",
	 clockDeclaredBindings(provider));
	/* Diagnostics are opt-in: the default boot performs no clock request and
	 * leaves every backend absent. */
	if (clockDiagRequested(provider)) thread_call_enter(fDiagCall);
	return true;
}

void IOQcomClockProvider::stop(IOService *provider)
{
	bool first=!fStopping;
	IOQcomClockController *dead[kIOQcomCCMax];
	unsigned deadCount=0;
	thread_call_t call=NULL;
	if (fLock) {
		IOLockLock(fLock);
		fReady=false;
		fStopping=true;
		call=fDiagCall;
		fDiagCall=NULL;
		for (unsigned id=0;id<kIOQcomCCMax;id++) {
			if (!fSlots[id].active) continue;
			dead[deadCount++]=fSlots[id].controller;
			fSlots[id].controller=NULL;
			fSlots[id].generation=0;
			fSlots[id].active=false;
		}
		IOLockUnlock(fLock);
	}
	if (call) {
		thread_call_cancel_wait(call);
		thread_call_free(call);
	}
	for (unsigned i=0;i<deadCount;i++) OSSafeReleaseNULL(dead[i]);
	SWEET_LOG_INFO("SWEET CLOCK: planID=C1 phase=stop result=0 withdrawn=%u\n",deadCount);
	if (first) super::stop(provider);
}

void IOQcomClockProvider::free(void)
{
	IOQcomClockController *dead[kIOQcomCCMax];
	unsigned deadCount=0;
	if (fDiagCall) {
		thread_call_cancel_wait(fDiagCall);
		thread_call_free(fDiagCall);
		fDiagCall=NULL;
	}
	if (fLock) {
		IOLockLock(fLock);
		for (unsigned id=0;id<kIOQcomCCMax;id++) {
			if (!fSlots[id].active) continue;
			dead[deadCount++]=fSlots[id].controller;
			fSlots[id].controller=NULL;
			fSlots[id].generation=0;
			fSlots[id].active=false;
		}
		IOLockUnlock(fLock);
	}
	for (unsigned i=0;i<deadCount;i++) OSSafeReleaseNULL(dead[i]);
	if (fHandles) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C1 phase=free result=%d detail=%llu-live-handles\n",
		 kIOReturnBusy,(unsigned long long)fHandles);
	}
	if (fLock) {
		IOLockFree(fLock);
		fLock=NULL;
	}
	fReady=false;
	fStopping=true;
	super::free();
}

IOReturn IOQcomClockProvider::registerController(IOQcomClockController *controller)
{
	if (!controller) return kIOReturnBadArgument;
	uint32_t id=controller->clockControllerID();
	if (id==0||id>=kIOQcomCCMax) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=cc%u consumer=provider phase=register result=%d detail=identity-out-of-range\n",
		 id,kIOReturnBadArgument);
		return kIOReturnBadArgument;
	}
	char resource[kIOQcomLogBuf];
	clockResource(resource,sizeof resource,id,0);
	if (!fLock) return kIOReturnNotReady;
	uint64_t generation=0;
	IOLockLock(fLock);
	bool live=fReady&&!fStopping;
	bool taken=live&&fSlots[id].active;
	if (live&&!taken) {
		controller->retain();
		fSlots[id].controller=controller;
		fSlots[id].generation=fNextGeneration++;
		if (fNextGeneration==0) fNextGeneration=1;
		generation=fSlots[id].generation;
		fSlots[id].active=true;
	}
	IOLockUnlock(fLock);
	if (!live) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=provider phase=register result=%d detail=provider-not-ready\n",
		 resource,kIOReturnNotReady);
		return kIOReturnNotReady;
	}
	if (taken) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=provider phase=register result=%d detail=duplicate-controller-id\n",
		 resource,kIOReturnBusy);
		return kIOReturnBusy;
	}
	SWEET_LOG_INFO("SWEET CLOCK: planID=C1 resource=%s consumer=provider phase=register result=0 generation=%llu\n",
	 resource,(unsigned long long)generation);
	return kIOReturnSuccess;
}

IOReturn IOQcomClockProvider::withdrawController(IOQcomClockController *controller)
{
	if (!controller) return kIOReturnBadArgument;
	if (!fLock) return kIOReturnNotReady;
	IOQcomClockController *victim=NULL;
	IOLockLock(fLock);
	for (unsigned id=0;id<kIOQcomCCMax;id++) {
		if (!fSlots[id].active||fSlots[id].controller!=controller) continue;
		victim=fSlots[id].controller;
		fSlots[id].controller=NULL;
		fSlots[id].generation=0;
		fSlots[id].active=false;
		break;
	}
	IOLockUnlock(fLock);
	if (!victim) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 consumer=provider phase=withdraw result=%d detail=not-registered\n",
		 kIOReturnNotFound);
		return kIOReturnNotFound;
	}
	SWEET_LOG_INFO("SWEET CLOCK: planID=C1 resource=cc%u consumer=provider phase=withdraw result=0\n",
	 victim->clockControllerID());
	victim->release();
	return kIOReturnSuccess;
}

IOReturn IOQcomClockProvider::retainController(uint32_t id,uint64_t generation,
 IOQcomClockController **out,uint64_t *generationOut)
{
	if (out) *out=NULL;
	if (generationOut) *generationOut=0;
	if (!fLock) return kIOReturnNotReady;
	if (id==0||id>=kIOQcomCCMax) return kIOReturnBadArgument;
	IOQcomClockController *controller=NULL;
	uint64_t observed=0;
	IOLockLock(fLock);
	if (fReady&&!fStopping) {
		Slot &slot=fSlots[id];
		if (slot.active&&slot.controller&&
		 (generation==0||slot.generation==generation)) {
			controller=slot.controller;
			controller->retain();
			observed=slot.generation;
		}
	}
	IOLockUnlock(fLock);
	if (!controller) return kIOReturnNotReady;
	if (out) *out=controller;
	if (generationOut) *generationOut=observed;
	return kIOReturnSuccess;
}

IOReturn IOQcomClockProvider::acquireClock(IOService *consumer,const char *name,
 IOQcomClockHandle **out)
{
	if (out) *out=NULL;
	if (!consumer||!name||!*name||!out) return kIOReturnBadArgument;
	IOQcomClockBinding items[kIOQcomBindingMax];
	unsigned count=0,match=0;
	IOReturn result=clockParseBindings(consumer,name,items,&count,&match);
	if (result!=kIOReturnSuccess) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=name:%s consumer=%s phase=bind result=%d detail=declared-bindings-unusable\n",
		 name,clockConsumerName(consumer),result);
		return result;
	}
	uint32_t controllerID=items[match].controller,localID=items[match].localID;
	char resource[kIOQcomLogBuf];
	clockResource(resource,sizeof resource,controllerID,localID);
	IOQcomClockController *controller=NULL;
	uint64_t generation=0;
	result=retainController(controllerID,0,&controller,&generation);
	if (result!=kIOReturnSuccess) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=%s phase=bind result=%d detail=controller-not-registered\n",
		 resource,clockConsumerName(consumer),kIOReturnNotReady);
		return kIOReturnNotReady;
	}
	IOQcomClockHandle *handle=OSTypeAlloc(IOQcomClockHandle);
	if (!handle||!handle->init()) {
		OSSafeReleaseNULL(handle);
		controller->release();
		return kIOReturnNoMemory;
	}
	handle->fProvider=this;
	retain();
	handle->fConsumer=consumer;
	consumer->retain();
	handle->fController=controllerID;
	handle->fLocalID=localID;
	handle->fGeneration=generation;
	handle->fToken=0;
	IOLockLock(fLock);
	Slot &slot=fSlots[controllerID];
	bool live=fReady&&!fStopping&&slot.active&&slot.generation==generation&&
	 slot.controller==controller;
	if (live) {
		fNextToken++;
		if (fNextToken==0) fNextToken=1;
		handle->fToken=fNextToken;
		fHandles++;
	}
	IOLockUnlock(fLock);
	controller->release();
	if (!live) {
		handle->release();
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=%s phase=bind result=%d detail=registration-withdrawn\n",
		 resource,clockConsumerName(consumer),kIOReturnNotReady);
		return kIOReturnNotReady;
	}
	SWEET_LOG_INFO("SWEET CLOCK: planID=C1 resource=%s consumer=%s token=%llu generation=%llu phase=bind result=0 bindings=%u\n",
	 resource,clockConsumerName(consumer),(unsigned long long)handle->fToken,
	 (unsigned long long)generation,count);
	*out=handle;
	return kIOReturnSuccess;
}

IOReturn IOQcomClockProvider::admitWalk(uint32_t controller,uint32_t localID,
 unsigned depth,Walk *walk,const char *consumer,uint32_t *reason)
{
	char resource[kIOQcomLogBuf];
	clockResource(resource,sizeof resource,controller,localID);
	if (depth>kIOQcomMaxDependencyDepth) {
		if (reason) *reason=kIOQcomDepDepth;
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=%s phase=admit result=%d detail=%s\n",
		 resource,consumer,kIOReturnBadArgument,clockDepName(kIOQcomDepDepth));
		return kIOReturnBadArgument;
	}
	for (unsigned i=0;i<walk->count;i++) {
		if (walk->controller[i]==controller&&walk->localID[i]==localID) {
			if (reason) *reason=kIOQcomDepCycle;
			SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=%s phase=admit result=%d detail=%s\n",
			 resource,consumer,kIOReturnBadArgument,clockDepName(kIOQcomDepCycle));
			return kIOReturnBadArgument;
		}
	}
	if (walk->count>=kIOQcomWalkMax) {
		if (reason) *reason=kIOQcomDepDepth;
		return kIOReturnBadArgument;
	}
	IOQcomClockController *owner=NULL;
	IOReturn result=retainController(controller,0,&owner,NULL);
	if (result!=kIOReturnSuccess) {
		if (reason) *reason=kIOQcomDepParentController;
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=%s phase=admit result=%d detail=%s\n",
		 resource,consumer,kIOReturnNotReady,clockDepName(kIOQcomDepParentController));
		return kIOReturnNotReady;
	}
	IOQcomClockInfo info;
	memset(&info,0,sizeof info);
	result=owner->copyClockInfo(localID,&info);
	if (result!=kIOReturnSuccess||!clockDescriptorValid(&info)||
	 info.controller!=controller||info.localID!=localID) {
		owner->release();
		if (reason) *reason=kIOQcomDepParentClock;
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=%s phase=admit result=%d detail=%s\n",
		 resource,consumer,kIOReturnNotReady,clockDepName(kIOQcomDepParentClock));
		return kIOReturnNotReady;
	}
	walk->controller[walk->count]=controller;
	walk->localID[walk->count]=localID;
	walk->count++;
	for (unsigned i=0;i<info.possibleCount;i++) {
		const IOQcomClockParent &parent=info.possible[i];
		if (parent.source!=kIOQcomSourceLocal) continue;
		result=admitWalk(parent.controller,parent.localID,depth+1,walk,consumer,reason);
		if (result!=kIOReturnSuccess) {
			owner->release();
			return result;
		}
	}
	walk->count--;
	owner->release();
	return kIOReturnSuccess;
}

IOReturn IOQcomClockProvider::admit(IOQcomClockController *owner,uint32_t localID,
 const char *consumer,IOQcomClockInfo *info,uint32_t *reason,bool requirements)
{
	if (reason) *reason=kIOQcomDepReady;
	if (!owner||!info) return kIOReturnBadArgument;
	char resource[kIOQcomLogBuf];
	clockResource(resource,sizeof resource,owner->clockControllerID(),localID);
	memset(info,0,sizeof *info);
	IOReturn result=owner->copyClockInfo(localID,info);
	if (result!=kIOReturnSuccess) {
		if (reason) *reason=kIOQcomDepMalformed;
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=%s phase=admit result=%d detail=%s\n",
		 resource,consumer,kIOReturnNotReady,clockDepName(kIOQcomDepMalformed));
		return kIOReturnNotReady;
	}
	if (!clockDescriptorValid(info)||info->controller!=owner->clockControllerID()||
	 info->localID!=localID) {
		if (reason) *reason=kIOQcomDepMalformed;
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=%s phase=admit result=%d detail=%s\n",
		 resource,consumer,kIOReturnBadArgument,clockDepName(kIOQcomDepMalformed));
		return kIOReturnBadArgument;
	}
	for (uint32_t id=1;id<kIOQcomCCMax;id++) {
		if (!(info->accessDeps&(1u<<id))) continue;
		IOQcomClockController *dependency=NULL;
		if (retainController(id,0,&dependency,NULL)!=kIOReturnSuccess) {
			if (reason) *reason=kIOQcomDepAccess;
			SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=%s phase=admit result=%d detail=%s dependency=cc%u\n",
			 resource,consumer,kIOReturnNotReady,clockDepName(kIOQcomDepAccess),id);
			return kIOReturnNotReady;
		}
		dependency->release();
	}
	if (!requirements) return kIOReturnSuccess;
	if (info->domain!=0) {
		if (reason) *reason=kIOQcomDepDomain;
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=%s phase=admit result=%d detail=%s domain=%#x\n",
		 resource,consumer,kIOReturnNotReady,clockDepName(kIOQcomDepDomain),info->domain);
		return kIOReturnNotReady;
	}
	Walk walk;
	walk.count=0;
	walk.controller[0]=info->controller;
	walk.localID[0]=localID;
	walk.count=1;
	for (unsigned i=0;i<info->possibleCount;i++) {
		const IOQcomClockParent &parent=info->possible[i];
		if (parent.source==kIOQcomSourceUnknown) {
			if (reason) *reason=kIOQcomDepUnknownParent;
			SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=%s phase=admit result=%d detail=%s\n",
			 resource,consumer,kIOReturnNotReady,clockDepName(kIOQcomDepUnknownParent));
			return kIOReturnNotReady;
		}
		if (parent.source!=kIOQcomSourceLocal) continue;
		result=admitWalk(parent.controller,parent.localID,1,&walk,consumer,reason);
		if (result!=kIOReturnSuccess) return result;
	}
	return kIOReturnSuccess;
}

IOReturn IOQcomClockProvider::checkDependencies(IOQcomClockHandle *handle,uint32_t *reason)
{
	if (reason) *reason=kIOQcomDepReady;
	if (!handle) return kIOReturnBadArgument;
	IOQcomClockController *controller=NULL;
	IOReturn result=retainController(handle->fController,handle->fGeneration,&controller,NULL);
	if (result!=kIOReturnSuccess) return result;
	IOQcomClockInfo info;
	result=admit(controller,handle->fLocalID,clockConsumerName(handle->fConsumer),
	 &info,reason,true);
	controller->release();
	return result;
}

void IOQcomClockProvider::diagnose(thread_call_param_t param0,thread_call_param_t)
{
	IOQcomClockProvider *provider=(IOQcomClockProvider *)param0;
	if (provider) provider->runDiagnostics();
}

/* ------------------------------------------------- diagnostic backend, C1 */

struct IOQcomClockDiagEntry { uint32_t localID,kind,halt,capabilities,domain;
 unsigned parents; IOQcomClockParent parent[1]; };

/* Four clocks are enough to show every C1 fact on a device: a root sample, a
 * local child, a missing parent controller and an unsatisfied supply domain.
 * None of them has a window, a register or a write path. */
static const IOQcomClockDiagEntry diagClocks[]={
 {1,kIOQcomClockRCG,kIOQcomHaltDelay,kIOQcomCapQueryConfig|kIOQcomCapQueryRate,0,
  1,{{kIOQcomSourceXO,0,0}}},
 {2,kIOQcomClockBranch,kIOQcomHaltCheck,
  kIOQcomCapQueryConfig|kIOQcomCapQueryRate|kIOQcomCapEnable,0,
  1,{{kIOQcomSourceLocal,kIOQcomCCDiagnostic,1}}},
 {3,kIOQcomClockRCG,kIOQcomHaltDelay,
  kIOQcomCapQueryConfig|kIOQcomCapEnable,0,
  1,{{kIOQcomSourceLocal,kIOQcomCCGCC,0x30}}},
 {4,kIOQcomClockBranch,kIOQcomHaltCheck,
  kIOQcomCapQueryConfig|kIOQcomCapQueryRate|kIOQcomCapEnable|kIOQcomCapDisable,
  kIOQcomDomainCX,1,{{kIOQcomSourceXO,0,0}}},
};

class IOQcomClockDiagController : public IOQcomClockController {
 OSDeclareDefaultStructors(IOQcomClockDiagController);
public:
 uint32_t clockControllerID(void) const APPLE_KEXT_OVERRIDE { return kIOQcomCCDiagnostic; }
 IOReturn copyClockInfo(uint32_t localID,IOQcomClockInfo *info) APPLE_KEXT_OVERRIDE;
 IOReturn copyClockState(uint32_t localID,IOQcomClockState *state) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(IOQcomClockDiagController,IOQcomClockController);

static const IOQcomClockDiagEntry *diagFind(uint32_t localID)
{
	for (unsigned i=0;i<sizeof diagClocks/sizeof diagClocks[0];i++) {
		if (diagClocks[i].localID==localID) return &diagClocks[i];
	}
	return NULL;
}

IOReturn IOQcomClockDiagController::copyClockInfo(uint32_t localID,IOQcomClockInfo *info)
{
	const IOQcomClockDiagEntry *entry=diagFind(localID);
	if (!entry||!info) return kIOReturnNotFound;
	memset(info,0,sizeof *info);
	info->controller=kIOQcomCCDiagnostic;
	info->localID=entry->localID;
	info->kind=entry->kind;
	info->halt=entry->halt;
	info->capabilities=entry->capabilities;
	info->domain=entry->domain;
	info->possibleCount=entry->parents;
	for (unsigned i=0;i<entry->parents&&i<kIOQcomMaxParents;i++) info->possible[i]=entry->parent[i];
	return kIOReturnSuccess;
}

IOReturn IOQcomClockDiagController::copyClockState(uint32_t localID,IOQcomClockState *state)
{
	const IOQcomClockDiagEntry *entry=diagFind(localID);
	if (!entry||!state) return kIOReturnNotFound;
	memset(state,0,sizeof *state);
	state->enableBit=false;
	state->clockOffBit=true;
	state->haltTrusted=false;
	state->rootOff=false;
	state->updatePending=false;
	state->selected.source=kIOQcomSourceNone;
	if (entry->parents) state->selected=entry->parent[0];
	state->configuredHz=0;
	state->rateKnown=false;
	state->divX2=0;
	return kIOReturnSuccess;
}

void IOQcomClockProvider::runDiagnostics(void)
{
	bool live=false;
	IOLockLock(fLock);
	live=fReady&&!fStopping;
	IOLockUnlock(fLock);
	if (!live) {
		SWEET_LOG_WARN("SWEET CLOCK: planID=C1 consumer=sweetclock-diag phase=diag result=%d detail=entry-not-ready\n",
		 kIOReturnNotReady);
		return;
	}
	IOQcomClockDiagController *diag=OSTypeAlloc(IOQcomClockDiagController);
	if (!diag||!diag->init()) {
		OSSafeReleaseNULL(diag);
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C1 consumer=sweetclock-diag phase=diag result=%d detail=no-backend\n",
		 kIOReturnNoMemory);
		return;
	}
	IOReturn result=registerController(diag);
	if (result!=kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C1 consumer=sweetclock-diag phase=diag result=%d detail=backend-register-failed\n",
		 result);
		diag->release();
		return;
	}
	diag->release();
	SWEET_LOG_INFO("SWEET CLOCK: planID=C1 resource=cc%u consumer=sweetclock-diag phase=diag result=0 detail=backend-registered\n",
	 (unsigned)kIOQcomCCDiagnostic);

	static const char *const names[]={"diag-core","diag-child","diag-orphan","diag-domain"};
	unsigned char nameBytes[64],pairBytes[sizeof names/sizeof names[0]*kIOQcomPairBytes];
	unsigned nameLength=0;
	for (unsigned i=0;i<sizeof names/sizeof names[0];i++) {
		unsigned length=(unsigned)strlen(names[i])+1;
		if (nameLength+length>sizeof nameBytes) return;
		memcpy(nameBytes+nameLength,names[i],length);
		nameLength+=length;
		uint32_t pair[2]={kIOQcomCCDiagnostic,(uint32_t)(i+1)};
		memcpy(pairBytes+i*kIOQcomPairBytes,pair,kIOQcomPairBytes);
	}
	IOService *consumer=OSTypeAlloc(IOService);
	if (!consumer||!consumer->init()) {
		OSSafeReleaseNULL(consumer);
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C1 consumer=sweetclock-diag phase=diag result=%d detail=no-consumer\n",
		 kIOReturnNoMemory);
		return;
	}
	consumer->setName("sweetclock-diag");
	OSData *nameData=OSData::withBytes(nameBytes,nameLength);
	OSData *pairData=OSData::withBytes(pairBytes,sizeof pairBytes);
	if (!nameData||!pairData) {
		OSSafeReleaseNULL(nameData);
		OSSafeReleaseNULL(pairData);
		consumer->release();
		SWEET_LOG_ERROR("SWEET CLOCK: planID=C1 consumer=sweetclock-diag phase=diag result=%d detail=no-properties\n",
		 kIOReturnNoMemory);
		return;
	}
	consumer->setProperty("sweet,clock-names",nameData);
	consumer->setProperty("sweet,clocks",pairData);
	nameData->release();
	pairData->release();
	for (unsigned i=0;i<sizeof names/sizeof names[0];i++) {
		IOQcomClockHandle *handle=NULL;
		char resource[kIOQcomLogBuf];
		clockResource(resource,sizeof resource,kIOQcomCCDiagnostic,(uint32_t)(i+1));
		result=acquireClock(consumer,names[i],&handle);
		if (result!=kIOReturnSuccess) {
			SWEET_LOG_WARN("SWEET CLOCK: planID=C1 resource=%s consumer=%s phase=diag-bind result=%d\n",
			 resource,names[i],result);
			continue;
		}
		IOQcomClockInfo info;
		IOQcomClockState state;
		uint32_t reason=kIOQcomDepReady;
		IOReturn infoResult=handle->copyInfo(&info);
		IOReturn stateResult=handle->copyState(&state);
		IOReturn depResult=checkDependencies(handle,&reason);
		IOReturn enableResult=handle->enable();
		SWEET_LOG_INFO("SWEET CLOCK: planID=C1 resource=%s consumer=%s token=%llu generation=%llu phase=diag result=0 info=%d state=%d deps=%d depReason=%s enable=%d caps=%#x parents=%u domain=%#x\n",
		 resource,names[i],(unsigned long long)handle->token(),
		 (unsigned long long)handle->generation(),infoResult,stateResult,
		 depResult,clockDepName(reason),enableResult,info.capabilities,
		 info.possibleCount,info.domain);
		handle->release();
	}
	consumer->release();
	SWEET_LOG_INFO("SWEET CLOCK: planID=C1 consumer=sweetclock-diag phase=diag-done result=0 detail=zero-writes\n");
}
#endif /* ARM64_BOARD_CONFIG_MH2LM */
