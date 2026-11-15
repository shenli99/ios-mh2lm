/* ADC measurements consumed through IOKit, never a global service bridge. */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOLib.h>
#include <IOKit/IOMessage.h>
#include <kern/clock.h>
#include "../../../bsd/dev/sweet_adc5_client.h"
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"
OSDefineMetaClassAndStructors(SweetAdc5Client, OSObject);
static uint64_t adcClientNow(void)
{
 uint64_t ticks, ns; clock_get_uptime(&ticks); absolutetime_to_nanoseconds(ticks, &ns); return ns;
}
SweetAdc5Client *
SweetAdc5Client::withOwner(IOWorkLoop *loop, SweetAdcNotify notify, void *context)
{
 if (!loop || !notify || !context) { return NULL; }
 SweetAdc5Client *self = new SweetAdc5Client;
 if (!self) { return NULL; }
 if (!self->init()) { self->release(); return NULL; }
 self->loop = loop; loop->retain(); self->notify = notify; self->context = context;
 self->lock = IOLockAlloc();
 self->event = IOInterruptEventSource::interruptEventSource(self, eventAction, NULL, 0);
 if (!self->lock || !self->event) { self->release(); return NULL; }
 self->event->disable();
 if (loop->addEventSource(self->event) != kIOReturnSuccess) { self->release(); return NULL; }
 self->eventAdded = true;
 OSDictionary *matching = IOService::serviceMatching("SweetPmicAdc5");
 if (matching) {
  self->terminated = IOService::addMatchingNotification(gIOTerminatedNotification, matching, serviceTerminated, self);
  if (self->terminated) { self->matched = IOService::addMatchingNotification(gIOFirstMatchNotification, matching, serviceMatched, self); }
  matching->release();
 }
 if (!self->terminated || !self->matched) { self->release(); return NULL; }
 self->event->enable(); self->event->interruptOccurred(NULL, NULL, 0);
 SWEET_LOG_INFO("SWEET BATT ADC: v1 registry consumer; measured voltages grant no charging permission\n");
 return self;
}
bool
SweetAdc5Client::serviceMatched(void *target, void *, IOService *service, IONotifier *)
{
 SweetAdc5Client *self = (SweetAdc5Client *)target;
 SweetPmicAdc5 *adc = OSDynamicCast(SweetPmicAdc5, service);
 if (!adc) { return true; }
 IOLockLock(self->lock);
 if (!self->stopping && !self->terminal && !self->provider && !adc->isInactive()) {
  adc->retain(); self->provider = adc; self->event->interruptOccurred(NULL, NULL, 0);
 }
 IOLockUnlock(self->lock); return true;
}
bool
SweetAdc5Client::serviceTerminated(void *target, void *, IOService *service, IONotifier *)
{
 SweetAdc5Client *self = (SweetAdc5Client *)target;
 IOLockLock(self->lock);
 if (!self->stopping && self->provider == service) {
  self->gone = true; self->event->interruptOccurred(NULL, NULL, 0);
 }
 IOLockUnlock(self->lock); return true;
}
IOReturn
SweetAdc5Client::serviceMessage(void *target, void *, UInt32, IOService *, void *, vm_size_t)
{
 SweetAdc5Client *self = (SweetAdc5Client *)target;
 /* Provider gate context: no consumer gate, no provider API, no SPMI. */
 IOLockLock(self->lock);
 if (!self->stopping && !self->terminal) { self->event->interruptOccurred(NULL, NULL, 0); }
 IOLockUnlock(self->lock); return kIOReturnSuccess;
}
bool
SweetAdc5Client::live(void)
{
 IOLockLock(lock);
 bool ok = !stopping && !terminal && !gone && provider;
 IOLockUnlock(lock);
 return ok && !provider->isInactive();
}
void
SweetAdc5Client::update(bool inputPresent, uint64_t acquisitionEpoch, bool batteryChanged, uint64_t now)
{
 /* Called only on the owner loop, from source IRQ or its protection monitor.
  * Periodic conversion monitors voltage, never discovers plug state. */
 if (acquisitionEpoch != inputEpoch || inputPresent != present) {
  ++epoch[1]; valid[1] = false;
  needed = inputPresent ? needed | 2 : needed & ~2u;
  SWEET_LOG_INFO("SWEET BATT ADC: invalidate channel=2 epoch=%u present=%u\n", epoch[1], inputPresent);
 }
 inputEpoch = acquisitionEpoch;
 present = inputPresent;
 if (batteryChanged) { ++epoch[0]; valid[0] = false; needed |= 1; }
 if (terminal) { return; }
 if (valid[0] && (now < samples[0].started_ns || now - samples[0].started_ns >= 30000000000ULL)) { needed |= 1; }
 /* The owner ticks at 1s: request with margin before the 3s voltage lease.
  * Delayed scheduling/conversion still expires permission; never extend it. */
 if (present && valid[1] && (now < samples[1].started_ns || now - samples[1].started_ns >= 1000000000ULL)) { needed |= 2; }
 if (needed && !active && !blocked) { event->interruptOccurred(NULL, NULL, 0); }
}
bool
SweetAdc5Client::copyFresh(enum sweet_adc5_channel channel, uint64_t now, struct sweet_adc5_measurement *out)
{
 if (!out || (channel != SWEET_ADC5_BAT_ID && channel != SWEET_ADC5_USBIN) || !live()) { return false; }
 unsigned i = channel - 1;
 uint64_t ageLimit = i ? 3000000000ULL : 60000000000ULL;
 const struct sweet_adc5_measurement &s = samples[i];
 if (!valid[i] || (i && !present) || !s.completed_ns || s.completed_ns < s.started_ns ||
     now < s.completed_ns || now < s.started_ns || now - s.started_ns > ageLimit) { return false; }
 *out = s; return true;
}
bool
SweetAdc5Client::copyFreshInput(uint64_t expectedEpoch, uint64_t now, struct sweet_adc5_measurement *out)
{
 return expectedEpoch && expectedEpoch!=~0ULL && expectedEpoch==inputEpoch &&
  expectedEpoch==sampleInputEpoch && copyFresh(SWEET_ADC5_USBIN,now,out);
}
void
SweetAdc5Client::invalidate(void)
{
 IOLockLock(lock); terminal = true; IOLockUnlock(lock);
 valid[0] = valid[1] = false; needed = active = 0;
}
void
SweetAdc5Client::eventAction(OSObject *owner, IOInterruptEventSource *, int)
{
 SweetAdc5Client *self = OSDynamicCast(SweetAdc5Client, owner); self->process();
}
void
SweetAdc5Client::process(void)
{
 if (!live()) {
  IOLockLock(lock); bool lost = gone; IOLockUnlock(lock);
  if (lost || (provider && provider->isInactive())) {
   invalidate(); notify(context);
   SWEET_LOG_ERROR("SWEET BATT ADC: provider terminated; measurements revoked\n");
  }
  return;
 }
 if (!interest) {
  interest = provider->registerInterest(gIOGeneralInterest, serviceMessage, this);
  if (!interest) {
   invalidate(); notify(context);
   SWEET_LOG_ERROR("SWEET BATT ADC: interest registration failed; measurements unavailable\n");
   return;
  }
 }
 IOReturn status = provider->status();
 if (!live()) { invalidate(); notify(context); return; }
 if (status == kIOReturnNotReady) {
  blocked = true;
  if (providerReady) {
   invalidate(); notify(context);
   SWEET_LOG_ERROR("SWEET BATT ADC: provider lost readiness; measurements revoked\n");
  }
  return;
 }
 if (status != kIOReturnSuccess) {
  invalidate(); notify(context);
  SWEET_LOG_ERROR("SWEET BATT ADC: provider failed; measurements revoked\n"); return;
 }
 providerReady = true; blocked = false; /* READY or conversion completion notification */
 if (active) {
  struct sweet_adc5_measurement result = {};
  IOReturn rc = provider->copySample((enum sweet_adc5_channel)active, &result);
  if (!live()) { invalidate(); notify(context); return; }
  if (rc == kIOReturnNotReady) { return; } /* our EOC still pending */
  unsigned i = active - 1;
  uint64_t now = adcClientNow();
  if (rc == kIOReturnSuccess && result.sequence == token && result.channel == active &&
      activeEpoch == epoch[i] && (!i || (present && activeInputEpoch==inputEpoch)) && result.completed_ns &&
      result.completed_ns >= result.started_ns && now >= result.completed_ns &&
      now >= result.started_ns && now - result.started_ns <= (i ? 3000000000ULL : 60000000000ULL)) {
   samples[i] = result; valid[i] = true;
   if (i) {
    sampleInputEpoch=activeInputEpoch;
    SWEET_LOG_INFO("SWEET BATT ADC INPUT: v1 token=%u epoch=%llu\n",token,(unsigned long long)sampleInputEpoch);
   }
   SWEET_LOG_INFO("SWEET BATT ADC: sample channel=%u token=%u epoch=%u uv=%u valid=1\n", active, token, epoch[i], result.microvolts);
  } else {
   valid[i] = false;
   if (!i || present) { needed |= 1u << i; }
   SWEET_LOG_WARN("SWEET BATT ADC: discarded channel=%u token=%u result=%u epoch=%u/%u error=%#x\n", active, token, result.sequence, activeEpoch, epoch[i], rc);
  }
  active = 0; notify(context);
 }
 if (!needed) { return; }
 unsigned channel = needed & 1 ? 1 : 2;
 unsigned nextToken = 0;
 IOReturn rc = provider->requestSample((enum sweet_adc5_channel)channel, &nextToken);
 if (!live()) { invalidate(); notify(context); return; }
 if (rc == kIOReturnBusy || rc == kIOReturnNotReady) {
  /* Wait for provider READY/completion, never spin or poll-retry. */
  blocked = true; return;
 }
 if (rc != kIOReturnSuccess) {
  invalidate(); notify(context);
  SWEET_LOG_ERROR("SWEET BATT ADC: sample request failed channel=%u error=%#x\n", channel, rc);
  return;
 }
 active = channel; token = nextToken; activeEpoch = epoch[channel - 1]; needed &= ~(1u << (channel - 1));
 activeInputEpoch=inputEpoch;
}
IOReturn
SweetAdc5Client::stopAction(OSObject *owner, void *, void *, void *, void *)
{
 SweetAdc5Client *self = OSDynamicCast(SweetAdc5Client, owner);
 self->invalidate();
 if (self->event) { self->event->disable(); }
 return kIOReturnSuccess;
}
void
SweetAdc5Client::stop(void)
{
 if (lock) { IOLockLock(lock); stopping = true; IOLockUnlock(lock); }
 /* No gate/lock held while remove synchronizes callbacks. First drain gated
  * process (which alone can install interest), then synchronize all notices. */
 if (loop && lock) { (void)loop->runAction(stopAction, this); }
 if (matched) { matched->remove(); matched = NULL; }
 if (terminated) { terminated->remove(); terminated = NULL; }
 if (interest) { interest->remove(); interest = NULL; }
 /* Keep the service retained until its interest callback is synchronized. */
 OSSafeReleaseNULL(provider);
 if (loop && eventAdded) { loop->removeEventSource(event); eventAdded = false; }
}
void
SweetAdc5Client::free(void)
{
 stop(); OSSafeReleaseNULL(event); OSSafeReleaseNULL(loop);
 if (lock) { IOLockFree(lock); lock = NULL; }
 OSObject::free();
}
#endif
