/* Registry discovery and termination synchronization shared by PMIC consumers.
 * There is one of these objects per consumer, with no global service pointer. */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOLib.h>
#include "../../../bsd/dev/sweet_spmi_mux_client.h"
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"
OSDefineMetaClassAndStructors(SweetSpmiMuxClient, OSObject);

SweetSpmiMuxClient *
SweetSpmiMuxClient::withOwner(IOService *owner, IOWorkLoop *loop,
    unsigned group, unsigned events, SweetMuxNotify notify, void *context,
    SweetMuxPrepare prepare)
{
	if (!owner || !loop || !notify || !context) {
		return NULL;
	}
	SweetSpmiMuxClient *self = new SweetSpmiMuxClient;
	if (!self) {
		return NULL;
	}
	if (!self->init()) {
		self->release();
		return NULL;
	}
	self->owner = owner;
	self->loop = loop;
	loop->retain();
	self->group = group;
	self->events = events;
	self->notify = notify;
	self->prepare = prepare;
	self->context = context;
	self->lock = IOLockAlloc();
	self->event = IOInterruptEventSource::interruptEventSource(self, eventAction, NULL, 0);
	if (!self->lock || !self->event) {
		self->release();
		return NULL;
	}
	/* addMatchingNotification can synchronously replay existing services.
	 * Install every field and both subscriptions before enabling processing. */
	self->event->disable();
	if (loop->addEventSource(self->event) != kIOReturnSuccess) {
		self->release();
		return NULL;
	}
	self->eventAdded = true;
	OSDictionary *matching = IOService::serviceMatching("SweetSpmiInterruptMux");
	if (matching) {
		self->terminated = IOService::addMatchingNotification(gIOTerminatedNotification,
		    matching, serviceTerminated, self);
		if (self->terminated) {
			self->matched = IOService::addMatchingNotification(gIOFirstMatchNotification,
			    matching, serviceMatched, self);
		}
		matching->release(); /* API retains, does NOT consume this dictionary */
	}
	if (!self->terminated || !self->matched) {
		self->release();
		return NULL;
	}
	SWEET_LOG_INFO("SWEET MUX CLIENT: watching registry group=%u; no global lookup\n", group);
	self->event->enable();
	self->event->interruptOccurred(NULL, NULL, 0);
	return self;
}

bool
SweetSpmiMuxClient::serviceMatched(void *target, void *, IOService *service, IONotifier *)
{
	SweetSpmiMuxClient *self = (SweetSpmiMuxClient *)target;
	SweetSpmiInterruptMux *mux = OSDynamicCast(SweetSpmiInterruptMux, service);
	if (!mux) {
		return true;
	}
	IOLockLock(self->lock);
	if (!self->stopping && !self->terminal && !self->provider && !mux->isInactive()) {
		mux->retain();
		self->provider = mux;
		self->event->interruptOccurred(NULL, NULL, 0);
	}
	IOLockUnlock(self->lock);
	return true;
}

bool
SweetSpmiMuxClient::serviceTerminated(void *target, void *, IOService *service, IONotifier *)
{
	/* This is phase-1 inactive notification, not proof that mux stop ran.
	 * Never call the mux or release our provider reference here. Queue the
	 * consumer loop, which synchronizes detach before releasing that reference. */
	SweetSpmiMuxClient *self = (SweetSpmiMuxClient *)target;
	IOLockLock(self->lock);
	if (!self->stopping && self->provider == service) {
		self->providerGone = true;
		self->event->interruptOccurred(NULL, NULL, 0);
	}
	IOLockUnlock(self->lock);
	return true;
}

void
SweetSpmiMuxClient::routeNotify(void *context, unsigned events)
{
	SweetSpmiMuxClient *self = (SweetSpmiMuxClient *)context;
	/* Mux gate context: only atomics and a software event, no consumer gate. */
	__atomic_fetch_or(&self->pending, events, __ATOMIC_RELEASE);
	self->event->interruptOccurred(NULL, NULL, 0);
}

void
SweetSpmiMuxClient::eventAction(OSObject *owner, IOInterruptEventSource *, int)
{
	SweetSpmiMuxClient *self = OSDynamicCast(SweetSpmiMuxClient, owner);
	if (self) {
		self->process();
	}
}

bool
SweetSpmiMuxClient::hasPendingEvents(unsigned mask)
{
	IOLockLock(lock);
	bool unavailable=stopping || terminal || providerGone || !provider ||
	    !attached || provider->isInactive();
	IOLockUnlock(lock);
	return unavailable || (__atomic_load_n(&pending,__ATOMIC_ACQUIRE) & (mask | SWEET_MUX_FAILED));
}

bool
SweetSpmiMuxClient::copyInterruptTrace(unsigned token, SweetMuxIrqTrace *out)
{
 IOLockLock(lock);
 bool ok = !stopping && provider && provider->copyInterruptTrace(group, token, out);
 IOLockUnlock(lock);
 return ok;
}

void
SweetSpmiMuxClient::process(void)
{
	unsigned pending = __atomic_exchange_n(&this->pending, 0u, __ATOMIC_ACQ_REL);
	IOLockLock(lock);
	bool done = stopping || terminal;
	SweetSpmiInterruptMux *mux = provider;
	bool gone = providerGone;
	IOLockUnlock(lock);
	if (done || !mux) {
		return;
	}
	if (gone || mux->isInactive() || (pending & SWEET_MUX_FAILED)) {
		SWEET_LOG_ERROR("SWEET MUX CLIENT: provider terminated/failed group=%u; detaching\n", group);
		closeBinding();
		notify(context, SWEET_MUX_FAILED);
		return;
	}
	if (!attached) {
		/* Owner-specific reads/maps/calibration belong here, outside registry
		 * notification locks and before enabling this consumer's routes.
		 * SPMI work uses the transport's controller-wide transaction lock. */
		if (prepare && !prepare(context)) {
			SWEET_LOG_ERROR("SWEET MUX CLIENT: consumer preparation failed group=%u\n", group);
			closeBinding();
			notify(context, SWEET_MUX_FAILED);
			return;
		}
		/* Provider or owner termination may have happened during preparation.
		 * Keep callbacks/resources alive but do not attach a disappearing owner. */
		IOLockLock(lock);
		gone = stopping || providerGone;
		IOLockUnlock(lock);
		if (gone || mux->isInactive() || owner->isInactive()) {
			closeBinding();
			notify(context, SWEET_MUX_FAILED);
			return;
		}
		IOReturn result = mux->attachConsumer(owner, group, events, routeNotify, this);
		if (result != kIOReturnSuccess) {
			SWEET_LOG_ERROR("SWEET MUX CLIENT: attachConsumer failed group=%u result=%#x\n", group, result);
			closeBinding();
			notify(context, SWEET_MUX_FAILED);
			return;
		}
		attached = true;
		IOLockLock(lock);
		gone = providerGone;
		IOLockUnlock(lock);
		if (gone || mux->isInactive()) {
			closeBinding();
			notify(context, SWEET_MUX_FAILED);
			return;
		}
		SWEET_LOG_INFO("SWEET MUX CLIENT: matched and attached group=%u\n", group);
		notify(context, SWEET_MUX_READY);
	}
	if (pending & events) {
		notify(context, pending & events);
	}
}

void
SweetSpmiMuxClient::closeBinding(void)
{
	/* Only the consumer workloop mutates attached or releases provider. */
	IOLockLock(lock);
	terminal = true;
	SweetSpmiInterruptMux *mux = provider;
	provider = NULL;
	IOLockUnlock(lock);
	if (mux) {
		if (attached) {
			/* Inactive does not invalidate this retained object: the mux keeps
			 * its gate/MMIO through free and accepts detach after stop too. */
			mux->detachConsumer(owner, group, this); /* synchronizes routeNotify */
		}
		mux->release();
	}
	attached = false;
	__atomic_store_n(&pending, 0u, __ATOMIC_RELEASE);
}

IOReturn
SweetSpmiMuxClient::detachAction(OSObject *owner, void *, void *, void *, void *)
{
	SweetSpmiMuxClient *self = OSDynamicCast(SweetSpmiMuxClient, owner);
	self->closeBinding();
	return kIOReturnSuccess;
}

void
SweetSpmiMuxClient::detach(void)
{
	if (lock && loop) {
		(void)loop->runAction(detachAction, this);
	}
}

void
SweetSpmiMuxClient::stop(void)
{
	if (lock) {
		IOLockLock(lock);
		stopping = true;
		IOLockUnlock(lock);
	}
	/* remove synchronizes callbacks; never hold lock or a workloop gate.
	 * Notifier::remove consumes the notifier, so no extra release follows. */
	if (matched) {
		matched->remove();
		matched = NULL;
	}
	if (terminated) {
		terminated->remove();
		terminated = NULL;
	}
	detach();
	if (event && loop && eventAdded) {
		event->disable();
		loop->removeEventSource(event);
		eventAdded = false;
	}
}

void
SweetSpmiMuxClient::free(void)
{
	stop();
	OSSafeReleaseNULL(event);
	OSSafeReleaseNULL(loop);
	if (lock) {
		IOLockFree(lock);
		lock = NULL;
	}
	OSObject::free();
}
#endif
