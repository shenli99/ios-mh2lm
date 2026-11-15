/* ADC5 IOService: own gate/timer/lifecycle, registry mux consumer, IRQ-only
 * conversion completion. Hardware protocol stays behind sweet_adc5.h. */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOPlatformExpert.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#include "../../../bsd/dev/sweet_adc5_service.h"
#include "../../../bsd/dev/sweet_spmi.h"
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"
class SweetPmicAdc5Device : public IOService {
 OSDeclareDefaultStructors(SweetPmicAdc5Device);
};
OSDefineMetaClassAndStructors(SweetPmicAdc5Device, IOService);
OSDefineMetaClassAndStructors(SweetPmicAdc5, IOService);

static uint64_t
adcNow(void)
{
 uint64_t absolute, ns;
 clock_get_uptime(&absolute); absolutetime_to_nanoseconds(absolute, &ns);
 return ns;
}

bool
SweetPmicAdc5::start(IOService *provider)
{
 unsigned mode = 0;
 PE_parse_boot_argn("sweetbattadcconv", &mode, sizeof(mode));
 /* Always publish the typed provider. Existing preflight remains read-only;
  * only explicit conversion diagnostics or a typed client can start work. */
 if (mode > 3 || !IOService::start(provider)) { return false; }
 diagnostics = mode; /* bit0 BAT_ID, bit1 USBIN, finite initial sequence */
 /* Set initial properties before registry replay can queue READY/failure. */
 setProperty("SweetADC5MeasurementProvider", true);
 setProperty("SweetADC5ChargingPermission", false);
 setProperty("SweetADC5IRQHealthy", false);
 loop = IOWorkLoop::workLoop();
 timer = IOTimerEventSource::timerEventSource(this, timerFired);
 if (loop && timer && loop->addEventSource(timer) == kIOReturnSuccess) {
  timerAdded = true;
  (void)loop->runAction(subscribeAction, this);
 }
 if (!connection) {
  shutdown(); IOService::stop(provider); return false;
 }
 provider->setProperty("SweetADC5ServiceStarted", true);
 registerService();
 SWEET_LOG_INFO("SWEET ADC5 TIMING: v1 phase deadline guard; terminal observer snapshot, no fallback\n");
 SWEET_LOG_INFO("SWEET ADC5 SERVICE: v1 mode=%u registry consumer; IRQ-only completion, no charging permission\n", mode);
 return true;
}

IOReturn
SweetPmicAdc5::subscribeAction(OSObject *owner, void *, void *, void *, void *)
{
 SweetPmicAdc5 *self = OSDynamicCast(SweetPmicAdc5, owner);
 self->connection = SweetSpmiMuxClient::withOwner(self, self->loop,
     SWEET_MUX_ADC5, SWEET_ADC5_IRQ_EOC, muxEvent, self);
 return self->connection ? kIOReturnSuccess : kIOReturnNoMemory;
}

IOReturn
SweetPmicAdc5::status(void)
{
 return loop ? loop->runAction(statusAction, this) : kIOReturnNotReady;
}
IOReturn
SweetPmicAdc5::statusAction(OSObject *owner, void *, void *, void *, void *)
{
 SweetPmicAdc5 *self = OSDynamicCast(SweetPmicAdc5, owner);
 if (self->failed) { return kIOReturnIOError; }
 return !__atomic_load_n(&self->stopping, __ATOMIC_ACQUIRE) && self->ready ? kIOReturnSuccess : kIOReturnNotReady;
}

IOReturn
SweetPmicAdc5::requestSample(enum sweet_adc5_channel channel, unsigned *token)
{
 if ((channel != SWEET_ADC5_BAT_ID && channel != SWEET_ADC5_USBIN) || !loop) { return kIOReturnBadArgument; }
 return loop->runAction(sampleAction, this, &channel, token);
}

IOReturn
SweetPmicAdc5::sampleAction(OSObject *owner, void *argument, void *token, void *, void *)
{
 SweetPmicAdc5 *self = OSDynamicCast(SweetPmicAdc5, owner);
 enum sweet_adc5_channel channel = *(enum sweet_adc5_channel *)argument;
 if (__atomic_load_n(&self->stopping, __ATOMIC_ACQUIRE) || !self->ready || self->failed) { return kIOReturnNotReady; }
 if (self->session.active) { return kIOReturnBusy; }
 self->valid[channel - 1] = false;
 self->setProperty(channel == SWEET_ADC5_BAT_ID ? "SweetADC5BATIDValid" : "SweetADC5USBINValid", false);
 self->session = {}; /* Rejected begin must not inherit a previous request. */
 ++self->sequence;
 SweetMuxIrqTrace baseline = {};
 self->traceBaselineHits = self->connection && self->connection->copyInterruptTrace(SWEET_ADC5_IRQ_EOC,&baseline) ? baseline.hits : 0;
 self->lastCallbackNs = self->lastTimerNs = 0;
 int rc = sweet_spmi_adc5_begin(&self->session, channel);
 SWEET_LOG_INFO("SWEET ADC5 REQUEST: v1 id=%u channel=%u error=%d phase=%u\n",
     self->sequence, channel, rc, self->session.phase);
 if (rc) { self->complete(rc, NULL); return kIOReturnIOError; }
 self->armDeadline();
 if (token) { *(unsigned *)token = self->sequence; }
 return kIOReturnSuccess;
}

IOReturn
SweetPmicAdc5::copySample(enum sweet_adc5_channel channel, struct sweet_adc5_measurement *out)
{
 if (!out || !loop || (channel != SWEET_ADC5_BAT_ID && channel != SWEET_ADC5_USBIN)) { return kIOReturnBadArgument; }
 return loop->runAction(copyAction, this, &channel, out);
}

IOReturn
SweetPmicAdc5::copyAction(OSObject *owner, void *argument, void *output, void *, void *)
{
 SweetPmicAdc5 *self = OSDynamicCast(SweetPmicAdc5, owner);
 unsigned index = *(enum sweet_adc5_channel *)argument - 1;
 if (__atomic_load_n(&self->stopping, __ATOMIC_ACQUIRE) || !self->ready || self->failed || !self->valid[index]) { return kIOReturnNotReady; }
 *(struct sweet_adc5_measurement *)output = self->samples[index];
 return kIOReturnSuccess;
}

void
SweetPmicAdc5::armDeadline(void)
{
 uint64_t now = adcNow();
 uint64_t deadline = session.phase == SWEET_ADC5_WAIT_SETTLE ? session.settle_until_ns : session.deadline_ns;
 unsigned ms = deadline > now ? (unsigned)((deadline - now + 999999ULL) / 1000000ULL) : 0;
 timer->setTimeoutMS(ms);
}

void
SweetPmicAdc5::muxEvent(void *context, unsigned events)
{
 SweetPmicAdc5 *self = (SweetPmicAdc5 *)context;
 /* SweetSpmiMuxClient has already crossed onto this consumer's own loop. */
 if (__atomic_load_n(&self->stopping, __ATOMIC_ACQUIRE)) { return; }
 if (events & SWEET_MUX_FAILED) { self->complete(SWEET_SPMI_NOT_READY, NULL); return; }
 if (events & SWEET_MUX_READY) {
  self->ready = true;
  self->setProperty("SweetADC5IRQHealthy", true);
  /* A matched client may have arrived before mux READY. Wake it explicitly. */
  self->messageClients(kIOMessageServicePropertyChange);
  if (self->diagnostics) {
   unsigned first = self->diagnostics & 1 ? 1 : 2;
   self->diagnostics &= ~first;
   (void)self->requestSample((enum sweet_adc5_channel)first);
  }
 }
 if (events & SWEET_ADC5_IRQ_EOC) {
  self->lastCallbackNs=adcNow();
  if (!self->session.active || self->session.phase == SWEET_ADC5_WAIT_SETTLE) {
   ++self->strayEoc;
   SWEET_LOG_WARN("SWEET ADC5 EOC: stale/unsolicited phase=%u count=%u; no sample accepted\n", self->session.phase, self->strayEoc);
   return;
  }
  self->timer->cancelTimeout();
  struct sweet_adc5_measurement result = {};
  int rc = sweet_spmi_adc5_advance(&self->session, SWEET_ADC5_EOC, &result);
  SWEET_LOG_INFO("SWEET ADC5 EOC: v1 id=%u error=%d phase=%u count=%u\n",
      self->sequence, rc, self->session.phase, self->session.eoc_count);
  if (rc == SWEET_ADC5_MORE) { self->armDeadline(); }
  else { self->complete(rc, rc ? NULL : &result); }
 }
}

void
SweetPmicAdc5::timerFired(OSObject *owner, IOTimerEventSource *)
{
 SweetPmicAdc5 *self = OSDynamicCast(SweetPmicAdc5, owner);
 if (__atomic_load_n(&self->stopping, __ATOMIC_ACQUIRE) || !self->session.active) { return; }
 uint64_t now=adcNow(); self->lastTimerNs=now;
 uint64_t due=self->session.phase==SWEET_ADC5_WAIT_SETTLE ? self->session.settle_until_ns : self->session.deadline_ns;
 /* A timer notification is not proof the CURRENT phase has expired. Never
  * accept data here, and never extend its existing conversion deadline. */
 if (now<due) { self->armDeadline(); return; }
 if (self->session.phase == SWEET_ADC5_WAIT_SETTLE) {
  struct sweet_adc5_measurement unused = {};
  int rc = sweet_spmi_adc5_advance(&self->session, SWEET_ADC5_SETTLED, &unused);
  if (rc == SWEET_ADC5_MORE || rc == SWEET_SPMI_BUSY) { self->armDeadline(); }
  else { self->complete(rc, NULL); }
 } else {
  /* A deadline diagnoses failure, never substitutes a status-poll sample. */
  SWEET_LOG_ERROR("SWEET ADC5 TIMEOUT: id=%u phase=%u; EOC missing, no polling fallback\n", self->sequence, self->session.phase);
  self->complete(SWEET_ADC5_EXPIRED, NULL);
 }
}

void
SweetPmicAdc5::captureFailure(int error)
{
 /* Failed/valid state is already withdrawn. Protocol errors retain their
  * pre-restore snapshot; timer errors observe before abort. Other failures
  * explicitly report a post-restore/inactive snapshot. No result is read. */
 SweetMuxIrqTrace t={};
 bool trace=connection && connection->copyInterruptTrace(SWEET_ADC5_IRQ_EOC,&t);
 SWEET_LOG_ERROR("SWEET ADC5 FAULT: v1 id=%u error=%d phase=%u count=%u now=%llu deadline=%llu callback=%llu timer=%llu\n",
  sequence,error,session.phase,session.eoc_count,(unsigned long long)adcNow(),(unsigned long long)session.deadline_ns,
  (unsigned long long)lastCallbackNs,(unsigned long long)lastTimerNs);
 SWEET_LOG_ERROR("SWEET ADC5 STAGE: v1 id=%u trigger=%llu done=%llu enter=%llu locked=%llu phase=%u due=%llu\n",
  sequence,(unsigned long long)session.trigger_enter_ns,(unsigned long long)session.trigger_done_ns,
  (unsigned long long)session.advance_enter_ns,(unsigned long long)session.advance_locked_ns,
  session.diagnostic_phase,(unsigned long long)session.diagnostic_deadline_ns);
 SWEET_LOG_ERROR("SWEET ADC5 MUX: v1 id=%u valid=%u baseline=%u hits=%u irq=%u work=%u apid=%u status=%#x\n",
  sequence,trace,traceBaselineHits,t.hits,t.irq,t.work,t.apid,t.status);
 SWEET_LOG_ERROR("SWEET ADC5 PATH: v1 id=%u gic=%llu scan=%llu ack=%llu notify=%llu\n",sequence,
  (unsigned long long)t.gicNs,(unsigned long long)t.scanNs,(unsigned long long)t.ackNs,(unsigned long long)t.notifyNs);
 struct sweet_adc5_fault f=session.fault;
 bool beforeRestore=session.fault_saved || session.active;
 int rc=session.fault_saved ? session.fault_error : sweet_spmi_adc5_capture_fault(&f);
 SWEET_LOG_ERROR("SWEET ADC5 SNAPSHOT: v2 id=%u error=%d before-restore=%u saved=%u enter=%llu first-lock=%llu done=%llu\n",sequence,rc,beforeRestore,session.fault_saved,
  (unsigned long long)f.entered_ns,(unsigned long long)f.locked_ns,(unsigned long long)f.completed_ns);
 if (!rc) for (unsigned i=0;i<SWEET_ADC5_FAULT_FIELDS;++i)
  SWEET_LOG_ERROR("SWEET ADC5 FIELD: v1 id=%u field=%u raw=%#x error=%d\n",sequence,i,f.raw[i],f.errors[i]);
}

void
SweetPmicAdc5::complete(int error, const struct sweet_adc5_measurement *result)
{
 timer->cancelTimeout();
 if (error) {
  failed = true; ready = false; valid[0] = valid[1] = false;
  captureFailure(error);
  int cleanup = sweet_spmi_adc5_abort(&session);
  diagnostics = 0;
  setProperty("SweetADC5IRQHealthy", false);
  setProperty("SweetADC5BATIDValid", false); setProperty("SweetADC5USBINValid", false);
  if (connection) { connection->detach(); }
  SWEET_LOG_ERROR("SWEET ADC5 FAILURE: v1 id=%u error=%d restore-error=%d cleanup=%d; provider stopped\n", sequence, error, session.restore_error, cleanup);
 } else if (result) {
  unsigned index = result->channel - 1;
  samples[index] = *result; samples[index].sequence = sequence; valid[index] = true;
  setProperty(index ? "SweetADC5USBINMicrovolts" : "SweetADC5BATIDMicrovolts", (uint64_t)result->microvolts, 32);
  setProperty(index ? "SweetADC5USBINValid" : "SweetADC5BATIDValid", true);
  SWEET_LOG_INFO("SWEET ADC5 SAMPLE: v1 id=%u channel=%u raw=%#x uv=%u eoc=%u valid=1 restored=1\n",
      sequence, result->channel, result->raw, result->microvolts, result->eoc_count);
  SWEET_LOG_INFO("SWEET ADC5 TIME: v1 id=%u started-ns=%llu completed-ns=%llu\n",
      sequence, (unsigned long long)result->started_ns, (unsigned long long)result->completed_ns);
 }
 /* General-interest clients must only queue their own work; never call back
  * synchronously into this provider from the notification. */
 messageClients(kIOMessageServicePropertyChange);
 if (!error && diagnostics) {
  unsigned next = diagnostics & 1 ? 1 : 2;
  diagnostics &= ~next;
  (void)requestSample((enum sweet_adc5_channel)next);
 }
}

IOReturn
SweetPmicAdc5::stopAction(OSObject *owner, void *, void *, void *, void *)
{
 SweetPmicAdc5 *self = OSDynamicCast(SweetPmicAdc5, owner);
 if (self->timer) { self->timer->cancelTimeout(); self->timer->disable(); }
 self->ready = false; self->valid[0] = self->valid[1] = false;
 self->setProperty("SweetADC5IRQHealthy", false);
 self->setProperty("SweetADC5BATIDValid", false); self->setProperty("SweetADC5USBINValid", false);
 int rc = sweet_spmi_adc5_abort(&self->session);
 if (rc) { SWEET_LOG_ERROR("SWEET ADC5 STOP: restore error=%d\n", rc); }
 self->messageClients(kIOMessageServicePropertyChange);
 return kIOReturnSuccess;
}

void
SweetPmicAdc5::shutdown(void)
{
 __atomic_store_n(&stopping, 1u, __ATOMIC_RELEASE);
 /* Outside the gate: remove/synchronize notifiers and mux callbacks first. */
 if (connection) { connection->stop(); }
 if (loop) { (void)loop->runAction(stopAction, this); }
}

bool
SweetPmicAdc5::willTerminate(IOService *provider, IOOptionBits options)
{
 shutdown(); return IOService::willTerminate(provider, options);
}
void
SweetPmicAdc5::stop(IOService *provider)
{
 shutdown(); IOService::stop(provider);
}
void
SweetPmicAdc5::free(void)
{
 shutdown();
 if (timer && loop && timerAdded) { loop->removeEventSource(timer); timerAdded = false; }
 OSSafeReleaseNULL(connection); OSSafeReleaseNULL(timer); OSSafeReleaseNULL(loop);
 IOService::free();
}
/* Late board publication, never a global service discovery bridge. */
extern "C" int sweet_adc5_init(void);
extern "C" int
sweet_adc5_init(void)
{
 static bool requested;
 if (requested) { return 0; }
 requested = true;
 IOPlatformExpert *platform = IOService::getPlatform();
 if (!platform) { return -1; }
 SweetPmicAdc5Device *nub = new SweetPmicAdc5Device;
 if (!nub) { return -1; }
 if (!nub->init() || !nub->attach(platform)) { nub->release(); return -1; }
 nub->setName("mh2lm-pmic-adc5");
 nub->registerService(kIOServiceSynchronous);
 bool started = nub->getProperty("SweetADC5ServiceStarted") == kOSBooleanTrue;
 if (!started) {
  SWEET_LOG_ERROR("SWEET ADC5 SERVICE: matching/start failed; provider unavailable\n");
  (void)nub->terminate(kIOServiceSynchronous);
 }
 nub->release();
 return started ? 0 : -1;
}
#endif
