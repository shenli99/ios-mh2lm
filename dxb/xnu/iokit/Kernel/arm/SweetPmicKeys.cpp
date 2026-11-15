/* Late board nub -> keys IOService -> registry-discovered PMIC mux.
 * The C backend retains only observer/HID/worker state, never a mux pointer. */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <pexpert/pexpert.h>
#include "../../../bsd/dev/sweet_spmi_mux_client.h"
#include "../../../bsd/dev/sweet_keys.h"
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"

class SweetPmicKeysDevice : public IOService {
	OSDeclareDefaultStructors(SweetPmicKeysDevice);
};
OSDefineMetaClassAndStructors(SweetPmicKeysDevice, IOService);
class SweetPmicKeys : public IOService {
	OSDeclareDefaultStructors(SweetPmicKeys);
public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	bool willTerminate(IOService *provider, IOOptionBits options) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;
	IOWorkLoop *getWorkLoop(void) const APPLE_KEXT_OVERRIDE { return loop; }
private:
	IOWorkLoop *loop = NULL;
	SweetSpmiMuxClient *connection = NULL;
	bool backendPrepared = false;
	static bool prepareBackend(void *);
	static void muxEvent(void *, unsigned);
	static IOReturn subscribeAction(OSObject *, void *, void *, void *, void *);
	void shutdown(void);
};
OSDefineMetaClassAndStructors(SweetPmicKeys, IOService);

void
SweetPmicKeys::muxEvent(void *context, unsigned events)
{
	SweetPmicKeys *self = (SweetPmicKeys *)context;
	if (events & SWEET_MUX_FAILED) {
		self->setProperty("SweetKeysIRQHealthy", false);
		sweet_keys_stop(); /* releases pressed HID keys and joins the worker */
		return;
	}
	if (events & SWEET_MUX_READY) {
		if (!sweet_keys_start()) {
			self->connection->detach();
			sweet_keys_stop();
			self->setProperty("SweetKeysIRQHealthy", false);
			return;
		}
		self->setProperty("SweetKeysIRQHealthy", true);
		SWEET_LOG_INFO("SWEET KEYS IRQ: three routes active; IRQ-only\n");
	}
	if (events & SWEET_KEYS_IRQ_ALL) {
		sweet_keys_event(events);
	}
}

bool
SweetPmicKeys::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	setProperty("SweetKeysIRQHealthy", false);
	loop = IOWorkLoop::workLoop();
	/* Hold the loop gate across installation and pointer publication: an
	 * immediate existing-service replay must not observe connection=NULL.
	 * Preparation runs later via the software event on this owner's loop. */
	if (loop) {
		(void)loop->runAction(subscribeAction, this);
	}
	if (!connection) {
		shutdown();
		IOService::stop(provider);
		SWEET_LOG_ERROR("SWEET KEYS IRQ: consumer matching setup failed; keys disabled\n");
		return false;
	}
	provider->setProperty("SweetKeysClientStarted", true);
	registerService();
	return true;
}

bool
SweetPmicKeys::prepareBackend(void *context)
{
	SweetPmicKeys *self = (SweetPmicKeys *)context;
	SWEET_LOG_INFO("SWEET KEYS IRQ: preparing backend on consumer workloop before attach\n");
	self->backendPrepared = sweet_keys_prepare();
	return self->backendPrepared;
}

IOReturn
SweetPmicKeys::subscribeAction(OSObject *owner, void *, void *, void *, void *)
{
	SweetPmicKeys *self = OSDynamicCast(SweetPmicKeys, owner);
	self->connection = SweetSpmiMuxClient::withOwner(self, self->loop,
	    SWEET_MUX_KEYS, SWEET_KEYS_IRQ_ALL, muxEvent, self, prepareBackend);
	return self->connection ? kIOReturnSuccess : kIOReturnNoMemory;
}

void
SweetPmicKeys::shutdown(void)
{
	/* Detach first: stop blocks owner dispatch, removes/synchronizes registry
	 * notifications, and drains mux callbacks before releasing the provider.
	 * Only then stop/join the worker (which emits final HID key-up), and unmap
	 * this backend's MMIO. These mappings are separate from the mux's own. */
	if (connection) {
		connection->stop(); /* synchronize matching, detach, and drain loop */
	}
	if (backendPrepared) {
		sweet_keys_stop();
		backendPrepared = false;
	}
}

bool
SweetPmicKeys::willTerminate(IOService *provider, IOOptionBits options)
{
	shutdown();
	return IOService::willTerminate(provider, options);
}

void
SweetPmicKeys::stop(IOService *provider)
{
	shutdown();
	IOService::stop(provider);
}

void
SweetPmicKeys::free(void)
{
	shutdown();
	OSSafeReleaseNULL(connection);
	OSSafeReleaseNULL(loop);
	IOService::free();
}

extern "C" void
sweet_keys_init(void)
{
	int enabled = 1;
	(void)PE_parse_boot_argn("sweetkeys", &enabled, sizeof(enabled));
	if (!enabled) {
		SWEET_LOG_INFO("SWEET KEYS: disabled (sweetkeys=0)\n");
		return;
	}
	IOPlatformExpert *platform = IOService::getPlatform();
	SweetPmicKeysDevice *nub = new SweetPmicKeysDevice;
	if (!platform || !nub) {
		OSSafeReleaseNULL(nub);
		return;
	}
	if (!nub->init() || !nub->attach(platform)) {
		nub->release();
		return;
	}
	nub->setName("mh2lm-pmic-keys");
	nub->registerService(kIOServiceSynchronous);
	if (nub->getProperty("SweetKeysClientStarted") != kOSBooleanTrue) {
		SWEET_LOG_ERROR("SWEET KEYS IRQ: consumer IOService failed to match/start\n");
		(void)nub->terminate(kIOServiceSynchronous);
	}
	nub->release();
}
#else
extern "C" void sweet_keys_init(void) { }
#endif
