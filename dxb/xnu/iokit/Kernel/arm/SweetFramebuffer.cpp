/*
 * Built-in IOMobileFramebuffer for SWEET: the ABL's continuous-splash
 * framebuffer (MH2LM: 1080x2340 BGRA in RAM, the panel scans it out by
 * itself) as the main display, driven by CoreAnimation's CPU renderer.
 *
 * IOMobileFramebuffer.framework (iOS 16.7) counts /arm-io children with
 * device_type "display-subsystem", waits for that many services matching
 * IOServiceMatching("IOMobileFramebuffer"), opens each with type 0 and
 * reads canvas-width/canvas-height from the service's provider. The class
 * here (AppleCLCD, an IOMobileFramebuffer) matches the /arm-io/disp0 nub.
 *
 * User client selectors used by CoreAnimation:
 *   3  GetLayerDefaultSurface [layer, ?] -> [IOSurface port name]
 *   4  SwapBegin -> [swap id]
 *   5  SwapEnd, struct 0x46c: +0x50 swap id, +0x54 IOSurface id per layer
 *      (4 layers), +0x64 rect a / +0xc4 rect b per layer (int x,y,w,h),
 *      +0x104 enabled layer mask
 *   6  SwapWait, 7 GetID, 8 GetDisplaySize -> [w, h],
 *   12 RequestPowerChange [state]: 0 = display off (backboardd, power
 *      button / auto-lock), else on. By default off fades the panel out over
 *      a few vsyncs (halving each pixel) and keeps it black until on again;
 *      sweetfbpreserveoff=1 leaves scanout pixels intact while CoreBrightness
 *      drives the real panel to its lowest safe brightness.
 *   18 IsMainDisplay,
 *   28 GetDotPitch, 72 Enable/DisableNotifications [func, refcon, type, 0]
 * Everything else is accepted and logged once.
 *
 * SwapEnd copies the enabled layers' surfaces into the panel memory (no
 * scaling, CPU copy). Vsync (notification type 5) is a 60 Hz timer that
 * sends (count, mach_absolute_time, 0) as 32-bit halves to clients that
 * enabled it, through the port they registered (IOConnectSetNotificationPort).
 *
 * The panel memory itself is published as an IOSurface and returned as the
 * default framebuffer surface. The kernel text console keeps drawing over
 * the GUI by default: boot-arg sweetfb_noconsole=1 stops drawing it from
 * the first swap on (it still reaches the persistent log; panics draw).
 */
#include "SweetSurface.h"
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "SweetDisplayBackend.h"
#include "SweetScanoutPool.h"
#endif
#include "SweetFBPerf.h"

extern "C" int sweet_console_muted;     /* osfmk/console/serial_console.c */

#if defined(SWEET)
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/system.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#include <sys/proc.h>
#include "../../../bsd/dev/sweet_fb_trace.h"
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"

enum {
	kSweetFBGetLayerDefaultSurface  = 3,
	kSweetFBSwapBegin               = 4,
	kSweetFBSwapEnd                 = 5,
	kSweetFBSwapWait                = 6,
	kSweetFBGetID                   = 7,
	kSweetFBGetDisplaySize          = 8,
	kSweetFBRequestPowerChange      = 12,
	kSweetFBIsMainDisplay           = 18,
	kSweetFBGetDotPitch             = 28,
	kSweetFBNotifications           = 72,
};

#define SWEET_FB_SWAP_REC_SIZE          0x46c
#define SWEET_FB_LAYERS                 4
#define SWEET_FB_NOTIFY_TYPES           8
#define SWEET_FB_NOTIFY_VSYNC           5
#define SWEET_FB_VSYNC_US               16667
#define SWEET_FB_DOT_PITCH              458     /* ppi, as the iPhone X panel */
/*
 * The iOS userland only has status bar / layout data for real iPhone
 * screens (UIKit: "No status bar visual provider for screen", the red
 * rdar://45025538 bar for 360x800 points). The display is therefore
 * presented as the iPhone X panel, 1125x2436 (375x812 points @3x), and the
 * centered 1080x2400 part of it is shown on the 1080x2400 panel.
 * Boot-arg sweetfb_native=1 presents the real panel size instead.
 */
#define SWEET_FB_LOGICAL_W              1125
#define SWEET_FB_LOGICAL_H              2436
#define SWEET_FB_FORMAT_BGRA            0x42475241
#define SWEET_FB_TRACE_SLOTS            4
#define SWEET_FB_TRACE_MAX_SELECTOR     32

typedef struct {
	uint32_t swapID, mask, disabled, raw10c, raw110;
	uint32_t surface[SWEET_FB_LAYERS];
	int32_t rect[SWEET_FB_LAYERS][4];
} SweetFBTraceSwap;

static uint32_t sweet_fb_selector_trace[128];

static uint64_t
sweet_fb_trace_now(void)
{
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns;
}

class IOMobileFramebuffer : public IOService {
	OSDeclareDefaultStructors(IOMobileFramebuffer);
};

OSDefineMetaClassAndStructors(IOMobileFramebuffer, IOService);

class IOMobileFramebufferUserClient;
class SweetFBContract;
class SweetDisplayClient;
class SweetDisplayBackend;
class SweetVsyncChannel;
class SweetScanoutPool;

class AppleCLCD : public IOMobileFramebuffer {
	OSDeclareDefaultStructors(AppleCLCD);

	IOWorkLoop         *fWorkLoop;
	IOTimerEventSource *fVsync;
	uint64_t            fVsyncCount;

	static void vsyncFired(OSObject *owner, IOTimerEventSource *sender);

public:
	/* F1b is opt-in. The gate remains attached until free so retained user
	 * clients can reject calls after stop without touching torn-down state. */
	bool fServiceMode, fServiceStopped, fStateGateAdded, fVsyncAdded;
	IOCommandGate *fStateGate;
	SweetDisplayClient *fDisplayClient;
	bool fBackendBound;
	bool fPanelPowerMode;
 bool fHardwareVsyncRequested, fHardwareVsyncActive, fVsyncFallback, fHardwareEventAdded;
 IOInterruptEventSource *fHardwareEvent;
 SweetVsyncChannel *fVsyncChannel;
 SweetDisplayBackend *fVsyncBackend;
 uint64_t fVsyncSequence, fHardwareSeenFrame, fHardwareTicks, fHardwareAdvance, fHardwareCoalesced;
 SweetFBPerf fPerf;
 void reportPerf(uint64_t);
 bool fOwnScanoutPreflight, fOwnScanoutRequested, fOwnScanoutStarted, fOwnScanoutActive, fOwnScanoutFailed;
 SweetDisplayBackend *fScanoutBackend;
 void scanoutStep(void);
 void shutdownScanout(void);
 SweetScanoutPool *fScanoutPool;
 unsigned fScanoutAuditCount;
 bool fScanoutAuditResume;
 uint64_t fScanoutAuditFrame;
 bool setupScanoutPool(void);
 void auditScanout(void);
 bool setupHardwareVsync(void);
 void bindHardwareVsync(SweetDisplayBackend *);
 void shutdownHardwareVsync(void);
 void fallbackHardwareVsync(const char *);
 static void hardwareVsyncFired(OSObject *,IOInterruptEventSource *,int);
 bool fPanelLifecyclePipeline, fPanelLifecycleRequested, fPanelLifecycleFull, fPanelLifecycleDataParking, fPanelLifecycleLinkCycle, fPanelLifecycleSupplyCycle, fPanelLifecycleFault, fPanelLifecycleDisplayOff, fPanelLifecycleDesiredOff, fPanelLifecycleRequestOff;
 SweetDisplayBackend *fPanelLifecycleBackend;
 uint64_t fPanelLifecycleToken, fPanelLifecycleStart, fPanelPipelineGeneration;
 void servicePanelLifecycle(void);
 void stopPanelLifecycle(void);
 IOReturn requestCoordinatedPower(bool);
	IOReturn requestPanelPower(bool off); /* framebuffer gate -> typed backend */
	enum { StateCall, StateAddClient, StateCloseClient, StatePort, StateStop };
	struct StateRequest {
		unsigned operation;
		IOMobileFramebufferUserClient *client;
		IOExternalMethodArguments *args;
		uint32_t selector;
		mach_port_t port;
	};
	static IOReturn stateAction(OSObject *, void *, void *, void *, void *);
	static void backendChanged(OSObject *);
	IOReturn stateControl(unsigned, IOMobileFramebufferUserClient * = NULL,
	    IOExternalMethodArguments * = NULL, uint32_t = 0, mach_port_t = MACH_PORT_NULL);
	bool setupServiceState(void);
	void free(void) APPLE_KEXT_OVERRIDE;
	/* Legacy/contract paths retain their existing lock. All F1b callers,
	 * including timer/present/trace, already hold the workloop gate. */
	void stateLock(void) { if (!fServiceMode) IOLockLock(fLock); }
	void stateUnlock(void) { if (!fServiceMode) IOLockUnlock(fLock); }
	SweetFBContract *fContract;
	bool fContractMode;
	IOReturn contractCall(IOMobileFramebufferUserClient *, uint32_t, IOExternalMethodArguments *);
	void contractClose(IOMobileFramebufferUserClient *);
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	uint64_t      fPhys;
	uint32_t      fWidth, fHeight, fRowBytes;
	uint32_t      fLogicalW, fLogicalH;     /* size reported to userland */
	int32_t       fOffX, fOffY;             /* logical origin of the panel */
	uintptr_t     fPixels;          /* kernel mapping of the panel memory */
	IOMemoryMap  *fMap;
	SweetSurface *fSurface;         /* the panel memory as an IOSurface */
	IOLock       *fLock;            /* swaps and client list */
	OSArray      *fClients;
	uint32_t      fSwapID;
	bool          fConsolePaused;
	uint8_t       fPending[SWEET_FB_SWAP_REC_SIZE];     /* latest swap, not yet shown */
	bool          fDirty;
	uint32_t      fFlushTicks;    /* vsync ticks between panel copies */
	bool          fPowerOff;      /* RequestPowerChange(0) */
	uint32_t      fFadeStep;      /* fade-out steps done while off */
	bool          fPreserveOff;   /* leave scanout contents intact while off */
	bool          fTrace;
	bool          fTracePowerAfter;
	bool          fTraceOffAfter;
	SweetFBTraceSwap fTraceSwaps[SWEET_FB_TRACE_SLOTS];
	uint32_t      fTraceSwapNext;
	uint32_t      fTraceSourceID; /* observe only, no lifetime extension */
	uint32_t      fTraceCopiedSwap, fTraceAttemptSwap, fTraceAttempts;
	int32_t       fTraceCopy[6]; /* sx, sy, dx, dy, width, height of last actual copy */
	uint64_t      fTraceCopiedNs, fTraceNextNs;
	uint32_t      fTracePeriodic;
	uint64_t      fTraceSelectorMask[2];

	void traceFrame(const char *phase);
	void traceCopy(const uint8_t *rec, SweetSurface *source, int32_t sx, int32_t sy,
	    int32_t dx, int32_t dy, int32_t w, int32_t h);
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void addClient(IOMobileFramebufferUserClient *client);
	void removeClient(IOMobileFramebufferUserClient *client);
	void swap(const uint8_t *rec);
	void present(const uint8_t *rec);
};

OSDefineMetaClassAndStructors(AppleCLCD, IOMobileFramebuffer);

class IOMobileFramebufferUserClient : public IOUserClient {
	OSDeclareDefaultStructors(IOMobileFramebufferUserClient);

public:
	AppleCLCD          *fFB;
	bool fContractClosed; /* only read/written under the contract command gate */
	bool fServiceClosed, fServiceProviderRetained;
	uint32_t fServiceSelectors[32], fServiceSelectorCount;
	IOReturn legacyMethod(uint32_t, IOExternalMethodArguments *);
	IOReturn serviceMethod(uint32_t, IOExternalMethodArguments *);
	static IOReturn serviceDispatch(OSObject *, void *, IOExternalMethodArguments *);
	void serviceClose(void); /* only under the framebuffer state gate */
	void stop(IOService *) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;
	task_t              fTask;
	mach_port_t         fPort;
	io_user_reference_t fFunc[SWEET_FB_NOTIFY_TYPES];
	io_user_reference_t fRefcon[SWEET_FB_NOTIFY_TYPES];

	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn registerNotificationPort(mach_port_t port, UInt32 type,
	    io_user_reference_t refCon) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
	void notify(uint32_t type, const uint64_t *values, uint32_t count);
};

OSDefineMetaClassAndStructors(IOMobileFramebufferUserClient, IOUserClient);

#include "SweetFBContract.inc"
#include "SweetFBService.inc"
#include "SweetFBVsync.inc"
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "SweetScanoutPool.inc"
#include "SweetFBScanout.inc"
#endif

IOReturn
AppleCLCD::contractCall(IOMobileFramebufferUserClient *client,uint32_t selector,IOExternalMethodArguments *args)
{
	stateLock();
	SweetFBContract *contract=fContract;
	if (contract) contract->retain();
	stateUnlock();
	if (!contract) return kIOReturnOffline;
	IOReturn result=contract->invoke(client,selector,args);
	contract->release();
	return result;
}

void
AppleCLCD::contractClose(IOMobileFramebufferUserClient *client)
{
	stateLock();
	SweetFBContract *contract=fContract;
	if (contract) contract->retain();
	stateUnlock();
	if (contract) {contract->close(client);contract->release();}
}

void
AppleCLCD::stop(IOService *provider)
{
	if (fServiceMode) {
		(void)stateControl(StateStop);
#if defined(ARM64_BOARD_CONFIG_MH2LM)
		/* Notification removal synchronizes callbacks; never hold our gate. */
		if (fDisplayClient) fDisplayClient->stop();
#endif
		if (fVsyncAdded) {
			fVsync->cancelTimeout(); fWorkLoop->removeEventSource(fVsync); fVsyncAdded=false;
		}
		IOMobileFramebuffer::stop(provider);
		return;
	}
	if (fContractMode) {
		if (fVsync) {
			fVsync->cancelTimeout();
			fWorkLoop->removeEventSource(fVsync);
			OSSafeReleaseNULL(fVsync);
		}
		stateLock();SweetFBContract *contract=fContract;fContract=NULL;stateUnlock();
		if (contract) {contract->shutdown();contract->release();}
	}
	/* Legacy resource lifetime is unchanged; client nubs still refer to the provider. */
	IOMobileFramebuffer::stop(provider);
}


#if defined(ARM64_BOARD_CONFIG_MH2LM)
/*
 * Panel brightness for CoreBrightness (backboardd). It matches an IOService
 * with backlight-control = true that conforms to AppleARMBacklight (the
 * class name matters: libEDR and the backboardd sandbox check it), reads
 * IODisplayParameters.brightness {min, max, value} and sets
 * {brightness: N, FadePeriod: 16.16 seconds} through setProperties.
 * Same contract as the HakiOS Redmi K20 Pro port (SM8150; kernel/iokit/
 * Kernel/MiniIOMobileFramebuffer.cpp, APSL 2.0). setProperties never blocks
 * (newest request wins, as UniXNU-sheng AppleARMBacklight.cpp: a blocking
 * handoff starved backboardd): the fade runs on a private work loop, one DCS
 * 0x51 write per step through the selected display backend. The optional
 * SweetMDSS client binds through IORegistry notifications, never a singleton.
 * The range is the panel's 12-bit DCS level. Boot-arg sweetbacklight=0
 * leaves the service out.
 */
extern "C" int sweet_display_brightness_ready(void);
extern "C" int sweet_display_brightness_set(uint32_t level);

#define SWEET_BL_MAX            0xfffu
#define SWEET_BL_MIN(a, b)      ((a) < (b) ? (a) : (b))
#define SWEET_BL_STEP_MS        20
#define SWEET_BL_FADE_MAX_MS    5000

class AppleARMBacklight : public IOService {
	OSDeclareDefaultStructors(AppleARMBacklight);

	SweetDisplayClient *fBackendClient;
	bool                fMDSS, fStopping, fTimerAdded, fAwaitingBackend, fWasBound;
	IOWorkLoop         *fWL;
	IOTimerEventSource *fTimer;
	IOSimpleLock       *fReqLock;
	bool                fReq;           /* request not yet taken by step() */
	uint32_t            fReqLevel, fReqMs;
	uint32_t            fLevel;         /* last level written; > max until the first write */
	uint32_t            fFrom, fTo;
	uint64_t            fStart, fPeriod;
	unsigned            fLogged;

	static void step(OSObject *owner, IOTimerEventSource *sender);
	static void backendChanged(OSObject *owner);
	void publish(void);
public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	IOReturn setProperties(OSObject *properties) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(AppleARMBacklight, IOService);

void
AppleARMBacklight::publish(void)
{
	OSDictionary *b = OSDictionary::withCapacity(3);
	OSDictionary *p = OSDictionary::withCapacity(1);
	OSNumber *min = OSNumber::withNumber(0ULL, 32);
	OSNumber *max = OSNumber::withNumber((unsigned long long)SWEET_BL_MAX, 32);
	OSNumber *value = OSNumber::withNumber((unsigned long long)SWEET_BL_MIN(fLevel, SWEET_BL_MAX), 32);
	if (b && p && min && max && value) {
		b->setObject("min", min);
		b->setObject("max", max);
		b->setObject("value", value);
		p->setObject("brightness", b);
		setProperty("IODisplayParameters", p);
	}
	OSSafeReleaseNULL(min);
	OSSafeReleaseNULL(max);
	OSSafeReleaseNULL(value);
	OSSafeReleaseNULL(b);
	OSSafeReleaseNULL(p);
}

bool
AppleARMBacklight::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	fReqLock = IOSimpleLockAlloc();
	fWL = IOWorkLoop::workLoop();
	fTimer = IOTimerEventSource::timerEventSource(this, &AppleARMBacklight::step);
	if (fReqLock == NULL || fWL == NULL || fTimer == NULL || fWL->addEventSource(fTimer) != kIOReturnSuccess) {
		IOLog("SWEET BL: setup failed\n");
		return false;
	}
	fLevel = SWEET_BL_MAX + 1;
	fTimerAdded=true;
	int mdss=0;
	(void)PE_parse_boot_argn("sweetmdss",&mdss,sizeof(mdss));
	fMDSS=mdss!=0;
	if (fMDSS) {
		fBackendClient=SweetDisplayClient::withOwner(this,fWL,backendChanged);
		if (!fBackendClient || !fBackendClient->watch()) {
			SWEET_LOG_ERROR("SWEET BL: display backend notification setup failed\n");
			return false;
		}
		setProperty("SweetDisplayBackend", "SweetMDSS");
	}
	/* The UEFI brightness is unknown until the first request. */
	publish();
	setProperty("backlight-control", true);
	registerService();
	IOLog("SWEET BL: AppleARMBacklight registered, range 0..%u\n", SWEET_BL_MAX);
	return true;
}

void
AppleARMBacklight::backendChanged(OSObject *owner)
{
	AppleARMBacklight *bl=(AppleARMBacklight *)owner;
	SweetDisplayBackend *backend=bl->fBackendClient->copyBackend();
	bool bound=backend!=NULL;
	OSSafeReleaseNULL(backend);
	bl->setProperty("SweetDisplayBackendBound",bound);
	if (bound && !bl->fWasBound) SWEET_LOG_INFO("SWEET BL: registry backend matched; deferred DCS on workloop\n");
	if (!bound && bl->fWasBound) SWEET_LOG_WARN("SWEET BL: registry backend terminated\n");
	bl->fWasBound=bound;
	if (bound && bl->fAwaitingBackend) bl->fTimer->setTimeoutUS(1);
}

void
AppleARMBacklight::stop(IOService *provider)
{
	IOSimpleLockLock(fReqLock); fStopping=true; IOSimpleLockUnlock(fReqLock);
	/* Outside the consumer gate: synchronize notifications before event teardown. */
	if (fBackendClient) fBackendClient->stop();
	if (fTimerAdded) {
		fTimer->cancelTimeout(); fWL->removeEventSource(fTimer); fTimerAdded=false;
	}
	IOService::stop(provider);
}

void
AppleARMBacklight::free(void)
{
	OSSafeReleaseNULL(fBackendClient);
	if (fTimerAdded) { fTimer->cancelTimeout(); fWL->removeEventSource(fTimer); }
	OSSafeReleaseNULL(fTimer); OSSafeReleaseNULL(fWL);
	if (fReqLock) { IOSimpleLockFree(fReqLock); fReqLock=NULL; }
	IOService::free();
}

/* Work loop: take the newest request, then one fade step per SWEET_BL_STEP_MS. */
void
AppleARMBacklight::step(OSObject *owner, IOTimerEventSource *sender)
{
	AppleARMBacklight *bl = (AppleARMBacklight *)owner;
	uint64_t now = mach_absolute_time();

	IOSimpleLockLock(bl->fReqLock);
	if (bl->fStopping) { IOSimpleLockUnlock(bl->fReqLock); return; }
	bool req = bl->fReq;
	uint32_t to = bl->fReqLevel, ms = bl->fReqMs;
	bl->fReq = false;
	IOSimpleLockUnlock(bl->fReqLock);
	if (req) {
		bl->fFrom = SWEET_BL_MIN(bl->fLevel, SWEET_BL_MAX);
		bl->fTo = to;
		bl->fStart = now;
		bl->fPeriod = 0;
		if (bl->fLevel <= SWEET_BL_MAX) {       /* no fade from an unknown level */
			nanoseconds_to_absolutetime((uint64_t)ms * NSEC_PER_MSEC, &bl->fPeriod);
		}
	}
	uint32_t level = bl->fTo;
	bool done = true;
	if (bl->fPeriod != 0 && now - bl->fStart < bl->fPeriod) {
		int64_t d = (int64_t)bl->fTo - (int64_t)bl->fFrom;
		level = (uint32_t)((int64_t)bl->fFrom + d * (int64_t)(now - bl->fStart) / (int64_t)bl->fPeriod);
		done = false;
	}
	if (level != bl->fLevel) {
		int rc;
		if (bl->fMDSS) {
			SweetDisplayBackend *backend=bl->fBackendClient->copyBackend();
			if (!backend) {
				bl->fAwaitingBackend=true;
				return; /* replay newest target once matching event arrives, no polling */
			}
			bl->fAwaitingBackend=false;
			IOReturn result=backend->setBrightness(level);
   /* Lifecycle owns DSI temporarily or is parked OFF. Keep newest target;
    * native property interest queues a retry at completion, no idle polling. */
   if (result==kIOReturnBusy && backend->panelLifecycleCapabilities()) bl->fAwaitingBackend=true;
			backend->release();
			rc=result==kIOReturnSuccess ? 0 : -1;
		} else rc=sweet_display_brightness_set(level);
		if (rc != 0) return; /* backend logs hardware failures; next request retries */
		bl->fLevel = level;
		bl->publish();
	}
	if (!done) {
		sender->setTimeoutMS(SWEET_BL_STEP_MS);
	}
}

IOReturn
AppleARMBacklight::setProperties(OSObject *properties)
{
	OSDictionary *d = OSDynamicCast(OSDictionary, properties);
	if (d == NULL) {
		return kIOReturnBadArgument;
	}
	OSNumber *b = OSDynamicCast(OSNumber, d->getObject("brightness"));
	OSNumber *f = OSDynamicCast(OSNumber, d->getObject("FadePeriod"));
	if (b == NULL) {
		if (fLogged < 40) {
			fLogged++;
			IOLog("SWEET BL: setProperties without a brightness number (%u keys)\n", d->getCount());
		}
		return kIOReturnSuccess;
	}
	uint64_t value = SWEET_BL_MIN(b->unsigned64BitValue(), (uint64_t)SWEET_BL_MAX);
	uint64_t ms = f != NULL ? (f->unsigned64BitValue() * 1000) >> 16 : 0;
	ms = SWEET_BL_MIN(ms, (uint64_t)SWEET_BL_FADE_MAX_MS);
	IOSimpleLockLock(fReqLock);
	if (fStopping) { IOSimpleLockUnlock(fReqLock); return kIOReturnNotReady; }
	fReq = true;
	fReqLevel = (uint32_t)value;
	fReqMs = (uint32_t)ms;
	fTimer->setTimeoutUS(1); /* stop closes admission under this same lock */
	IOSimpleLockUnlock(fReqLock);
	if (fLogged < 40) {
		fLogged++;
		IOLog("SWEET BL: CoreBrightness %llu fade %llu ms\n", value, ms);
	}
	return kIOReturnSuccess;
}
#endif /* ARM64_BOARD_CONFIG_MH2LM */

bool
AppleCLCD::start(IOService *provider)
{
	PE_Video video;

	if (!IOService::start(provider)) {
		return false;
	}
	int service=0, mdssService=0, contractService=0, panelPower=0, hardwareVsync=0, mdssIRQ=0, ownScanout=0;
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	(void)PE_parse_boot_argn("sweetfbservice",&service,sizeof(service));
	(void)PE_parse_boot_argn("sweetfbpower",&panelPower,sizeof(panelPower));
 (void)PE_parse_boot_argn("sweetfbvsync",&hardwareVsync,sizeof(hardwareVsync));
 (void)PE_parse_boot_argn("sweetmdssirq",&mdssIRQ,sizeof(mdssIRQ));
 (void)PE_parse_boot_argn("sweetfbown",&ownScanout,sizeof(ownScanout));
	(void)PE_parse_boot_argn("sweetmdss",&mdssService,sizeof(mdssService));
	(void)PE_parse_boot_argn("sweetfbcontract",&contractService,sizeof(contractService));
#endif
	fServiceMode=service!=0;
	fPanelPowerMode=panelPower!=0;
 fHardwareVsyncRequested=hardwareVsync!=0;
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 if (ownScanout && ((ownScanout!=1 && ownScanout!=2) || !fHardwareVsyncRequested || !fServiceMode ||
     !fPanelPowerMode || !mdssService || mdssIRQ!=2 || contractService)) {
  SWEET_LOG_ERROR("SWEET FB SCANOUT: requires own1-or-2/service1/mdss1/power1/irq2/vsync1/contract0; refusing invalid configuration\n");
  return false;
 }
 fOwnScanoutPreflight=ownScanout!=0;
 fOwnScanoutRequested=ownScanout==2;
#endif
 if (fHardwareVsyncRequested && (!fServiceMode || !fPanelPowerMode || mdssIRQ!=2)) {
  SWEET_LOG_ERROR("SWEET FB VSYNC: requires service1/power1/mdssirq2; refusing invalid configuration\n");
  return false;
 }
	if (fPanelPowerMode && (!fServiceMode || !mdssService || contractService)) {
		SWEET_LOG_ERROR("SWEET FB POWER: requires service1/mdss1/contract0; refusing fallback\n");
		return false;
	}
	if (fServiceMode && (!mdssService || contractService)) {
		SWEET_LOG_ERROR("SWEET FB SERVICE: requires sweetmdss=1 and sweetfbcontract=0; refusing fallback\n");
		return false;
	}
	PE_current_console(&video);
	fPhys = video.v_baseAddr;
	fWidth = (uint32_t)video.v_width;
	fHeight = (uint32_t)video.v_height;
	fRowBytes = (uint32_t)video.v_rowBytes;
	if (fPhys == 0 || fWidth == 0 || fHeight == 0 || fRowBytes < fWidth * 4) {
		IOLog("SWEET FB: no boot framebuffer\n");
		return false;
	}
	int native = 0;
	PE_parse_boot_argn("sweetfb_native", &native, sizeof(native));
	fLogicalW = fWidth;
	fLogicalH = fHeight;
	if (!native) {
		/*
		 * The iOS userland is the iPhone X: present its panel size
		 * (375x812 points @3x) whatever the physical panel is. sweet
		 * (1080x2400) crops it centered; a larger panel (e.g. the
		 * OnePlus 7 Pro's 1440x3120) letterboxes it, which the
		 * present() logical->panel offset does with a negative offX.
		 */
		fLogicalW = SWEET_FB_LOGICAL_W;
		fLogicalH = SWEET_FB_LOGICAL_H;
	}
	fOffX = (int32_t)(fLogicalW - fWidth) / 2;
	fOffY = (int32_t)(fLogicalH - fHeight) / 2;
	fLock = IOLockAlloc();
	fClients = OSArray::withCapacity(4);
	fSurface = sweet_surface_create_physical(fPhys, fWidth, fHeight, fRowBytes, SWEET_FB_FORMAT_BGRA);
	fPixels = fSurface != NULL ? fSurface->kernelAddress() : 0;
	fWorkLoop = IOWorkLoop::workLoop();
	if (fLock == NULL || fClients == NULL || fPixels == 0 || fWorkLoop == NULL) {
		IOLog("SWEET FB: setup failed\n");
		OSSafeReleaseNULL(fSurface);    /* stays published: also held by the table */
		OSSafeReleaseNULL(fWorkLoop);
		return false;
	}
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 int perf=0; (void)PE_parse_boot_argn("sweetfbperf",&perf,sizeof(perf));
 fPerf.enabled=fServiceMode && perf==1;
 setProperty("SweetFramebufferPerfEnabled",fPerf.enabled);
#endif
	fVsync = IOTimerEventSource::timerEventSource(this, &AppleCLCD::vsyncFired);
	if (fVsync == NULL || fWorkLoop->addEventSource(fVsync) != kIOReturnSuccess) {
		IOLog("SWEET FB: no vsync timer\n");
		return false;
	}
	fVsyncAdded=true;
	uint32_t hz = 10;
	PE_parse_boot_argn("sweetfb_hz", &hz, sizeof(hz));
	fFlushTicks = (hz == 0 || hz >= 60) ? 1 : 60 / hz;
	int preserveOff = 0;
	(void)PE_parse_boot_argn("sweetfbpreserveoff", &preserveOff, sizeof(preserveOff));
	fPreserveOff = preserveOff != 0 || fPanelPowerMode;
	int trace = 0;
	(void)PE_parse_boot_argn("sweetfbtrace", &trace, sizeof(trace));
	fTrace = trace != 0;
	if (fTrace) {
		SWEET_LOG_INFO("SWEET FB TRACE: v2 enabled; BGRA samples step=8, interval=1s limit=600; read-only\n");
		fTraceNextNs = sweet_fb_trace_now() + 1000000000ULL;
	}
	int contract=0;
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	(void)PE_parse_boot_argn("sweetfbcontract",&contract,sizeof(contract));
#endif
	if (contract) {
		fContractMode=true;
		fContract=OSTypeAlloc(SweetFBContract);
		if (!fContract || !fContract->init() || !fContract->setup(this,fWorkLoop)) {
			SWEET_LOG_ERROR("SWEET FB CONTRACT: setup failed; refusing legacy fallback\n");
			OSSafeReleaseNULL(fContract);
			fWorkLoop->removeEventSource(fVsync);OSSafeReleaseNULL(fVsync);
			return false;
		}
		if (fTrace) SWEET_LOG_WARN("SWEET FB CONTRACT: disabling live source trace after source release\n");
		fTrace=false;
	}
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 if (fOwnScanoutPreflight && !setupScanoutPool()) {
  SWEET_LOG_ERROR("SWEET FB SCANOUT: pool setup failed; experimental stage disabled, F3 retained\n");
  fOwnScanoutPreflight=false;
  if (fOwnScanoutRequested) return false; /* No silent own2 -> ABL fallback. */
  setProperty("SweetScanoutPreflightFailed",true);
 }
#endif
	if (fServiceMode && !setupServiceState()) {
		SWEET_LOG_ERROR("SWEET FB SERVICE: setup failed; refusing legacy fallback\n");
		return false;
	}
	if (fPanelPowerMode) {
		setProperty("SweetPanelPowerControl",true);
		SWEET_LOG_INFO("SWEET FB POWER: F2 v1 DCS28/29 enabled; scanout pixels preserved\n");
	}
	fVsync->setTimeoutUS(SWEET_FB_VSYNC_US);

	setProperty(kIOUserClientClassKey, "IOMobileFramebufferUserClient");
	setProperty("external", false);
	setProperty("DisplayPipePlaneBaseAlignment", 64ULL, 32);
	registerService();
	IOLog("SWEET FB: AppleCLCD %ux%u stride %u at 0x%llx, default surface %u, presented as %ux%u\n",
	    fWidth, fHeight, fRowBytes, fPhys, fSurface->id, fLogicalW, fLogicalH);
	if (fPreserveOff) {
		IOLog("SWEET FB: preserving framebuffer contents while display is off\n");
	}
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	int backlight = 1;
	PE_parse_boot_argn("sweetbacklight", &backlight, sizeof(backlight));
	int mdss=0;
	(void)PE_parse_boot_argn("sweetmdss",&mdss,sizeof(mdss));
	if (backlight && (mdss || sweet_display_brightness_ready() == 0)) {
		AppleARMBacklight *bl = OSTypeAlloc(AppleARMBacklight);
		if (bl != NULL && bl->init() && bl->attach(this) && !bl->start(this)) {
			bl->detach(this);
		}
		OSSafeReleaseNULL(bl);
	} else {
		IOLog("SWEET BL: no backlight service (sweetbacklight=%d)\n", backlight);
	}
#endif
	return true;
}

void
AppleCLCD::vsyncFired(OSObject *owner, IOTimerEventSource *sender)
{
	AppleCLCD *fb = (AppleCLCD *)owner;
	if (fb->fServiceMode && fb->fServiceStopped) return;
	uint64_t now = mach_absolute_time();
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 if (fb->fPanelLifecycleBackend) fb->servicePanelLifecycle();
 if (fb->fPanelLifecycleBackend || fb->fPanelLifecycleFault) return;
 if (fb->fPerf.enabled && fb->fPowerOff) fb->fPerf.lastPresentNS=0;
 if (fb->fOwnScanoutRequested) fb->scanoutStep();
 if (fb->fHardwareVsyncRequested) {
  if (fb->fPowerOff) return; /* no OFF timer, notifications or presentation */
  if (sender && fb->fHardwareVsyncActive) {
   SweetVsyncSample latest={};
   bool valid=fb->fVsyncChannel && fb->fVsyncChannel->copySample(&latest) && latest.state==SweetVsyncFrame;
   uint64_t age=0;
   if (valid) absolutetime_to_nanoseconds(now-latest.ticks,&age);
   if (valid && age<=250000000ULL) { sender->setTimeoutMS(100); return; }
   fb->fallbackHardwareVsync("consumer watchdog >250ms");
  }
  if (!sender) {
   if (!fb->fHardwareVsyncActive || fb->fVsyncFallback) return;
   now=fb->fHardwareTicks;
  }
 }
#endif
 uint64_t advance=1;
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 if (!sender && fb->fHardwareVsyncRequested) advance=fb->fHardwareAdvance;
#endif
 uint64_t count = (fb->fVsyncCount+=advance);
	uint64_t values[6] = {
		(uint32_t)count, (uint32_t)(count >> 32),
		(uint32_t)now, (uint32_t)(now >> 32),
		0, 0,
	};

	fb->stateLock();
	for (unsigned int i = 0; i < fb->fClients->getCount(); i++) {
		IOMobileFramebufferUserClient *client =
		    (IOMobileFramebufferUserClient *)fb->fClients->getObject(i);
		client->notify(SWEET_FB_NOTIFY_VSYNC, values, 6);
	}
	if (fb->fContractMode) {
		fb->stateUnlock();
		/* All contract pixels are written by external callers outside the command gate. */
		if (sender) sender->setTimeoutUS(SWEET_FB_VSYNC_US);return;
	}
	if (fb->fPowerOff) {
		/* CoreBrightness makes the real panel black. The opt-in preserve mode
		 * avoids destroying the last complete CPU-composited frame, because a
		 * later wake can contain only a partial layer update. */
		bool preserve = fb->fPreserveOff;
		uint32_t step = fb->fFadeStep;
		if (!preserve && step <= 6) {
			fb->fFadeStep = step + 1;
		}
		bool traceOff = fb->fTraceOffAfter && (preserve || step >= 6);
		if (traceOff) {
			fb->fTraceOffAfter = false;
		}
		fb->stateUnlock();
		if (!preserve && step <= 6) {
			for (uint32_t y = 0; y < fb->fHeight; y++) {
				volatile uint32_t *row = (volatile uint32_t *)(fb->fPixels + (uint64_t)y * fb->fRowBytes);
				for (uint32_t x = 0; x < fb->fWidth; x++) {
					row[x] = step < 6 ? (row[x] >> 1) & 0x7f7f7f7f : 0;
				}
			}
		}
		if (traceOff) {
			fb->traceFrame("off-after");
		}
		if (sender) sender->setTimeoutUS(SWEET_FB_VSYNC_US);
		return;
	}
	bool tracePeriodic = false;
	if (fb->fTrace && fb->fTracePeriodic < 600) {
		uint64_t ns = sweet_fb_trace_now();
		if (ns >= fb->fTraceNextNs) {
			fb->fTraceNextNs = ns + 1000000000ULL;
			++fb->fTracePeriodic;
			tracePeriodic = true;
		}
	}
	bool flush = fb->fDirty && (count % fb->fFlushTicks) == 0;
	uint8_t *rec = NULL;
	if (flush) {
		rec = (uint8_t *)IOMallocData(SWEET_FB_SWAP_REC_SIZE);
		if (rec != NULL) {
			memcpy(rec, fb->fPending, SWEET_FB_SWAP_REC_SIZE);
#if defined(ARM64_BOARD_CONFIG_MH2LM)
   if (fb->fPerf.enabled) {
    uint64_t ns=sweet_fb_trace_now();
    if (fb->fPerf.firstPendingNS) fb->fPerf.pending.add(ns-fb->fPerf.firstPendingNS,33333333ULL);
    if (fb->fPerf.latestPendingNS) fb->fPerf.latest.add(ns-fb->fPerf.latestPendingNS,33333333ULL);
    fb->fPerf.firstPendingNS=fb->fPerf.latestPendingNS=0;
   }
#endif
			fb->fDirty = false;
		}
	}
	fb->stateUnlock();
	if (rec != NULL) {
		fb->present(rec);
		IOFreeData(rec, SWEET_FB_SWAP_REC_SIZE);
		bool traceOn = false;
		fb->stateLock();
		traceOn = fb->fTracePowerAfter;
		fb->fTracePowerAfter = false;
		fb->stateUnlock();
		if (traceOn) {
			fb->traceFrame("on-after");
		}
	}
	if (tracePeriodic) {
		fb->traceFrame("periodic");
	}
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 if (fb->fPerf.enabled) fb->reportPerf(sweet_fb_trace_now());
#endif
	if (sender) sender->setTimeoutUS(SWEET_FB_VSYNC_US);
}

void
AppleCLCD::addClient(IOMobileFramebufferUserClient *client)
{
	if (fServiceMode) { (void)stateControl(StateAddClient,client); return; }
	stateLock();
	fClients->setObject(client);
	stateUnlock();
}

void
AppleCLCD::removeClient(IOMobileFramebufferUserClient *client)
{
	if (fServiceMode) { (void)stateControl(StateCloseClient,client); return; }
	stateLock();
	unsigned int i = fClients->getNextIndexOfObject(client, 0);
	if (i != (unsigned int)-1) {
		fClients->removeObject(i);
	}
	stateUnlock();
}

static uint32_t
get32(const uint8_t *p, uint32_t off)
{
	uint32_t v;
	memcpy(&v, p + off, 4);
	return v;
}

static uint32_t sweet_fb_swaps_logged;
static uint64_t sweet_fb_swap_count;

/* Copy each enabled layer's surface into the panel memory (no scaling). */
/*
 * A swap only records the frame: copying 10 MB into the panel on every
 * swap (backboardd presents at 60 Hz) took the whole CPU. The vsync timer
 * shows the latest frame every fFlushTicks ticks (boot-arg sweetfb_hz,
 * default 10 Hz).
 */
void
AppleCLCD::swap(const uint8_t *rec)
{
	stateLock();
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 if (fPerf.enabled) {
  uint64_t ns=sweet_fb_trace_now(); ++fPerf.swaps;
  if (fDirty) ++fPerf.coalesced;
  if (!fPerf.firstPendingNS) fPerf.firstPendingNS=ns;
  fPerf.latestPendingNS=ns;
 }
#endif
	memcpy(fPending, rec, SWEET_FB_SWAP_REC_SIZE);
	fDirty = true;
	if (fTrace) {
		SweetFBTraceSwap *t = &fTraceSwaps[fTraceSwapNext++ % SWEET_FB_TRACE_SLOTS];
		bzero(t, sizeof(*t));
		t->swapID = get32(rec, 0x50);
		t->mask = get32(rec, 0x104);
		t->disabled = get32(rec, 0x108);
		t->raw10c = get32(rec, 0x10c);
		t->raw110 = get32(rec, 0x110);
		for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
			t->surface[layer] = get32(rec, 0x54 + 4 * layer);
			const uint8_t *b = rec + 0xc4 + 16 * layer;
			for (uint32_t n = 0; n < 4; n++) {
				t->rect[layer][n] = (int32_t)get32(b, n * 4);
			}
		}
	}
	stateUnlock();
	if ((++sweet_fb_swap_count % 1000) == 0) {
		IOLog("SWEET FB: %llu swaps\n", sweet_fb_swap_count);
	}
}

/* Preserve metadata for the copy made into the boot scanout buffer. A later null-surface
 * request must not make diagnostics silently switch to an unrelated ring slot. */
void
AppleCLCD::traceCopy(const uint8_t *rec, SweetSurface *source, int32_t sx, int32_t sy,
    int32_t dx, int32_t dy, int32_t w, int32_t h)
{
	if (!fTrace) { return; }
	stateLock();
	fTraceSourceID = source->id;
	fTraceCopiedSwap = get32(rec, 0x50);
	fTraceCopiedNs = sweet_fb_trace_now();
	fTraceCopy[0] = sx; fTraceCopy[1] = sy;
	fTraceCopy[2] = dx; fTraceCopy[3] = dy;
	fTraceCopy[4] = w; fTraceCopy[5] = h;
	stateUnlock();
}

void
AppleCLCD::traceFrame(const char *phase)
{
	if (!fTrace || !sweet_log_level_enabled(2u)) { return; }
	SweetFBTraceSwap recent[SWEET_FB_TRACE_SLOTS];
	uint32_t next, attempted, attempts, copied;
	uint64_t copiedNs, selectors[2];
	int32_t box[6];
	stateLock();
	memcpy(recent, fTraceSwaps, sizeof(recent));
	next = fTraceSwapNext; attempted = fTraceAttemptSwap; attempts = fTraceAttempts;
	copied = fTraceCopiedSwap; copiedNs = fTraceCopiedNs;
	memcpy(box, fTraceCopy, sizeof(box));
	memcpy(selectors, fTraceSelectorMask, sizeof(selectors));
	uint32_t sourceID = fTraceSourceID;
	stateUnlock();
	SweetSurface *source = sourceID ? sweet_surface_copy(sourceID) : NULL;
	uint64_t ns = sweet_fb_trace_now();
	SWEET_LOG_INFO("SWEET FB TRACE: v2 phase=%s ns=%llu attempt=%u attempts=%u copied=%u copy-ns=%llu source=%u alive=%u selectors=%llx/%llx\n",
	    phase, ns, attempted, attempts, copied, copiedNs, sourceID, source != NULL, selectors[0], selectors[1]);
	uint32_t count = next < SWEET_FB_TRACE_SLOTS ? next : SWEET_FB_TRACE_SLOTS;
	for (uint32_t n = 0; n < count; n++) {
		const SweetFBTraceSwap &t = recent[(next - count + n) % SWEET_FB_TRACE_SLOTS];
		SWEET_LOG_INFO("SWEET FB TRACE: v2 %s recent[%u] swap=%u update=%#x disable=%#x raw10c=%#x raw110=%#x ids=%u,%u,%u,%u r0=%d,%d,%d,%d\n",
		    phase, n, t.swapID, t.mask, t.disabled, t.raw10c, t.raw110, t.surface[0], t.surface[1],
		    t.surface[2], t.surface[3], t.rect[0][0], t.rect[0][1], t.rect[0][2], t.rect[0][3]);
	}
	if (source) {
		SWEET_LOG_INFO("SWEET FB TRACE: v2 source=%u %ux%u fmt=%#x bpe=%u bpr=%u alloc=%u length=%llu offset=%u planes=%u copy=%d,%d->%d,%d %dx%d\n",
		    source->id, source->width, source->height, source->pixelFormat, source->bytesPerElement,
		    source->bytesPerRow, source->allocSize, source->memory ? source->memory->getLength() : 0,
		    source->offset, source->planeCount, box[0], box[1], box[2], box[3], box[4], box[5]);
	}
	/* Compare the same destination bands, intersected with the last actual copy.
	 * These observations do not lock a CA renderer or turn a sample into a frame fence. */
	SweetFBTraceView panel = {(const volatile uint8_t *)fPixels,
	    fSurface && fSurface->memory ? fSurface->memory->getLength() : 0, 0, fWidth, fHeight, fRowBytes};
	SweetFBTraceView src = {};
	if (source && source->memory && source->pixelFormat == SWEET_FB_FORMAT_BGRA &&
	    source->bytesPerElement == 4 && source->planeCount == 0) {
		src = {(const volatile uint8_t *)source->kernelAddress(), source->memory->getLength(),
		    source->offset, source->width, source->height, source->bytesPerRow};
	}
	struct Region { const char *name; uint32_t x, y, w, h; };
	const Region regions[] = {
		{"upper", 0, 0, fWidth, fHeight / 3},
		{"middle", 0, fHeight / 3, fWidth, fHeight * 2 / 3 - fHeight / 3},
		{"lower", 0, fHeight * 2 / 3, fWidth, fHeight - fHeight * 2 / 3},
		/* Geometric ROIs, not a claim about UIKit glyph locations. */
		{"clock-center", fWidth / 6, fHeight / 12, fWidth * 2 / 3, fHeight / 4},
		{"home-indicator", fWidth / 4, fHeight * 9 / 10, fWidth / 2, fHeight / 10},
	};
	for (const Region &r : regions) {
		SweetFBTracePixels pixels;
		if (sweet_fb_trace_pixels(&panel, r.x, r.y, NULL, 0, 0, r.w, r.h, 8, &pixels)) {
			SWEET_LOG_INFO("SWEET FB TRACE: v2 %s panel roi=%s xy=%u,%u wh=%u,%u hash=%llx samples=%u rgb=%u alpha=%u\n",
			    phase, r.name, r.x, r.y, r.w, r.h, pixels.hash, pixels.samples, pixels.rgb, pixels.alpha);
		}
		/* present() already validated this rectangle. Recheck before diagnostic reads. */
		if (!src.base || box[0] < 0 || box[1] < 0 || box[2] < 0 || box[3] < 0 ||
		    box[4] <= 0 || box[5] <= 0 || (uint64_t)box[2] + box[4] > fWidth ||
		    (uint64_t)box[3] + box[5] > fHeight) { continue; }
		uint32_t top = r.y > (uint32_t)box[3] ? r.y : (uint32_t)box[3];
		uint32_t bottom = r.y+r.h < (uint32_t)(box[3]+box[5]) ? r.y+r.h : (uint32_t)(box[3]+box[5]);
		uint32_t left = r.x > (uint32_t)box[2] ? r.x : (uint32_t)box[2];
		uint32_t right = r.x+r.w < (uint32_t)(box[2]+box[4]) ? r.x+r.w : (uint32_t)(box[2]+box[4]);
		if (bottom <= top || right <= left) { continue; }
		uint32_t sy = box[1] + top - box[3], sx = box[0] + left - box[2];
		if (sweet_fb_trace_pixels(&src, sx, sy, &panel, left, top,
		    right-left, bottom-top, 8, &pixels)) {
			SWEET_LOG_INFO("SWEET FB TRACE: v2 %s pair roi=%s src=%u src-xy=%u,%u dst-xy=%u,%u wh=%u,%u hash=%llx samples=%u rgb=%u alpha=%u diff-rgb=%u diff-alpha=%u live-sample=1\n",
			    phase, r.name, source->id, sx, sy, left, top, right-left, bottom-top,
			    pixels.hash, pixels.samples, pixels.rgb, pixels.alpha, pixels.differentRGB, pixels.differentAlpha);
		} else {
			SWEET_LOG_WARN("SWEET FB TRACE: v2 source bounds rejected id=%u\n", source->id);
		}
	}
	stateLock();
	bool changed = copiedNs != fTraceCopiedNs;
	stateUnlock();
	SWEET_LOG_INFO("SWEET FB TRACE: v2 %s sample-us=%llu copy-changed=%u live-sample=1\n",
	    phase, (sweet_fb_trace_now()-ns)/1000, changed);

	OSSafeReleaseNULL(source);
}

void
AppleCLCD::present(const uint8_t *rec)
{
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 /* Quiescing/submitted/faulted stage2 never continues writing the ABL path. */
 if (fOwnScanoutStarted && (!fOwnScanoutActive || fOwnScanoutFailed)) return;
 uintptr_t target=fOwnScanoutActive ? (uintptr_t)fScanoutPool->address(0)+48u*4608u+88u : fPixels;
 uint32_t targetStride=fOwnScanoutActive ? 4608u : fRowBytes;
 uint64_t perfStart=fPerf.enabled ? sweet_fb_trace_now() : 0;
 uint64_t perfBytes=0;
#else
 uintptr_t target=fPixels;
 uint32_t targetStride=fRowBytes;
#endif
	uint32_t mask = get32(rec, 0x104);
	if (fTrace) {
		stateLock();
		fTraceAttemptSwap = get32(rec, 0x50);
		++fTraceAttempts;
		stateUnlock();
	}

	if (!fConsolePaused) {
		int quiet = 0;
		fConsolePaused = true;
		if (PE_parse_boot_argn("sweetfb_noconsole", &quiet, sizeof(quiet)) && quiet != 0) {
			/* Not PE_initialize_console(kPEDisableScreen): its console switch
			 * writes the read-only-late cons_ops_index and panics. */
			sweet_console_muted = 1;
		}
	}

	for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
		uint32_t id = get32(rec, 0x54 + 4 * layer);
		const uint8_t *a = rec + 0x64 + 16 * layer;
		const uint8_t *b = rec + 0xc4 + 16 * layer;
		int32_t sx = (int32_t)get32(a, 0), sy = (int32_t)get32(a, 4);
		int32_t dx = (int32_t)get32(b, 0), dy = (int32_t)get32(b, 4);
		int32_t w = (int32_t)get32(b, 8), h = (int32_t)get32(b, 12);

		if (!(mask & (1u << layer)) || id == 0) {
			continue;
		}
		SweetSurface *s = sweet_surface_copy(id);
		if (sweet_fb_swaps_logged < 16) {
			sweet_fb_swaps_logged++;
			IOLog("SWEET FB: swap %u layer %u surface %u %ux%u fmt 0x%x bpr %u a %d,%d %ux%u b %d,%d %dx%d\n",
			    get32(rec, 0x50), layer, id, s ? s->width : 0, s ? s->height : 0,
			    s ? s->pixelFormat : 0, s ? s->bytesPerRow : 0, sx, sy, get32(a, 8),
			    get32(a, 12), dx, dy, w, h);
		}
		if (s == NULL || s == fSurface) {
			OSSafeReleaseNULL(s);
			continue;
		}
		uintptr_t src = s->kernelAddress();
		uint32_t bpe = s->bytesPerElement ? s->bytesPerElement : 4;
		if (w <= 0 || h <= 0) {
			w = (int32_t)s->width;
			h = (int32_t)s->height;
		}
		if (sx < 0 || sy < 0) {
			sx = sy = 0;
		}
		if (dx < 0 || dy < 0) {
			dx = dy = 0;
		}
		/* Logical (presented) display coordinates -> panel coordinates. */
		dx -= fOffX;
		dy -= fOffY;
		if (dx < 0) {
			sx -= dx;
			w += dx;
			dx = 0;
		}
		if (dy < 0) {
			sy -= dy;
			h += dy;
			dy = 0;
		}
		if (w > (int32_t)s->width - sx) {
			w = (int32_t)s->width - sx;
		}
		if (h > (int32_t)s->height - sy) {
			h = (int32_t)s->height - sy;
		}
		if (w > (int32_t)fWidth - dx) {
			w = (int32_t)fWidth - dx;
		}
		if (h > (int32_t)fHeight - dy) {
			h = (int32_t)fHeight - dy;
		}
		uint64_t last = (uint64_t)s->offset + (uint64_t)(sy + h - 1) * s->bytesPerRow +
		    (uint64_t)(sx + w) * 4;
		if (w <= 0 || h <= 0 || s->elementWidth != 1 || s->elementHeight != 1 ||
		    s->memory == NULL || last > s->memory->getLength()) {
			src = 0;
		}
		if (src != 0 && bpe == 4) {
#if defined(ARM64_BOARD_CONFIG_MH2LM)
   uint64_t copyStart=fPerf.enabled ? sweet_fb_trace_now() : 0;
#endif
			for (int32_t y = 0; y < h; y++) {
				memcpy((void *)(target + (uint64_t)(dy + y) * targetStride + (uint64_t)dx * 4),
				    (const void *)(src + s->offset + (uint64_t)(sy + y) * s->bytesPerRow + (uint64_t)sx * 4),
				    (size_t)w * 4);
			}
#if defined(ARM64_BOARD_CONFIG_MH2LM)
   if (fOwnScanoutActive) { __asm__ volatile("dsb oshst" ::: "memory"); }
   if (fPerf.enabled) {
    fPerf.copy.add(sweet_fb_trace_now()-copyStart,16666667ULL);
    perfBytes+=(uint64_t)w*(uint64_t)h*4u;
   }
#endif
			traceCopy(rec, s, sx, sy, dx, dy, w, h);
		}
		s->release();
	}
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 if (fPerf.enabled && perfBytes) {
  uint64_t ns=sweet_fb_trace_now();
  fPerf.present.add(ns-perfStart,33333333ULL);
  if (fPerf.lastPresentNS) fPerf.interval.add(ns-fPerf.lastPresentNS,33333333ULL);
  fPerf.lastPresentNS=ns; fPerf.bytes+=perfBytes; ++fPerf.presents;
 }
#endif
}

#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "SweetFBPerf.inc"
#endif

bool
IOMobileFramebufferUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	fTask = owningTask;
	fContractClosed=false;
	return true;
}

bool
IOMobileFramebufferUserClient::start(IOService *provider)
{
	fFB = OSDynamicCast(AppleCLCD, provider);
	if (fFB == NULL || !IOUserClient::start(provider)) {
		return false;
	}
	if (fFB->fContractMode) {
		/* External wait must not exclusively lock out the independent submitting caller. */
		setProperty(kIOUserClientDefaultLockingKey,kOSBooleanTrue);
		setProperty(kIOUserClientDefaultLockingSetPropertiesKey,kOSBooleanFalse);
		setProperty(kIOUserClientDefaultLockingSingleThreadExternalMethodKey,kOSBooleanFalse);
	}
	if (fFB->fServiceMode) {
		/* Keep the provider and its stopped gate valid until this client frees. */
		fFB->retain(); fServiceProviderRetained=true;
		setProperty(kIOUserClientDefaultLockingKey,kOSBooleanTrue);
		setProperty(kIOUserClientDefaultLockingSingleThreadExternalMethodKey,kOSBooleanFalse);
		return fFB->stateControl(AppleCLCD::StateAddClient,this)==kIOReturnSuccess;
	}
	fFB->addClient(this);
	return true;
}

IOReturn
IOMobileFramebufferUserClient::clientClose(void)
{
	if (fFB && fFB->fServiceMode) {
		(void)fFB->stateControl(AppleCLCD::StateCloseClient,this);
		terminate(); return kIOReturnSuccess;
	}
	if (fFB != NULL) {
		if (fFB->fContractMode) fFB->contractClose(this);
		fFB->removeClient(this);
		fFB->stateLock();
		mach_port_t old = fPort;
		fPort = MACH_PORT_NULL;
		fFB->stateUnlock();
		if (old != MACH_PORT_NULL) {
			releaseNotificationPort(old);
		}
	}
	terminate();
	return kIOReturnSuccess;
}

IOReturn
IOMobileFramebufferUserClient::registerNotificationPort(mach_port_t port, __unused UInt32 type,
    __unused io_user_reference_t refCon)
{
	if (fFB && fFB->fServiceMode) {
		IOReturn rc=fFB->stateControl(AppleCLCD::StatePort,this,NULL,0,port);
		if (rc!=kIOReturnSuccess && port!=MACH_PORT_NULL) releaseNotificationPort(port);
		return rc;
	}
	mach_port_t old;
	fFB->stateLock();
	old = fPort;
	fPort = port;
	fFB->stateUnlock();
	if (old != MACH_PORT_NULL) {
		releaseNotificationPort(old);
	}
	return kIOReturnSuccess;
}

/* Called with the framebuffer lock held. */
void
IOMobileFramebufferUserClient::notify(uint32_t type, const uint64_t *values, uint32_t count)
{
	OSAsyncReference64 ref;

	if (type >= SWEET_FB_NOTIFY_TYPES || fFunc[type] == 0 || fPort == MACH_PORT_NULL) {
		return;
	}
	bzero(ref, sizeof(ref));
	setAsyncReference64(ref, fPort, (mach_vm_address_t)fFunc[type], fRefcon[type], fTask);
	sendAsyncResult64WithOptions(ref, kIOReturnSuccess, (io_user_reference_t *)values, count,
	    kIOUserNotifyOptionCanDrop);
}

static uint64_t sweet_fb_logged;        /* selectors already logged (< 64) */

IOReturn
IOMobileFramebufferUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    __unused IOExternalMethodDispatch *dispatch, __unused OSObject *target,
    __unused void *reference)
{
	if (!fFB) return kIOReturnOffline;
	if (fFB->fServiceMode) return serviceMethod(selector,args);
	return legacyMethod(selector,args);
}

IOReturn
IOMobileFramebufferUserClient::legacyMethod(uint32_t selector, IOExternalMethodArguments *args)
{
	if (fFB->fContractMode) {
		if (!args) return kIOReturnBadArgument;
		switch (selector) {
		case 4:case 5:case 6:case 12:case 52:case 77:case 81:
			return fFB->contractCall(this,selector,args);
		case 20: /* Software syncpoint signalling is not implemented by this experiment. */
			return kIOReturnUnsupported;
		default:break;
		}
	}

	IOReturn ret = kIOReturnSuccess;
	uint64_t out[2] = { 0, 0 };
	uint32_t outCount = 0;

	switch (selector) {
	case kSweetFBGetLayerDefaultSurface:
		out[0] = sweet_surface_port_in_task(fFB->fSurface, fTask);
		outCount = 1;
		break;
	case kSweetFBSwapBegin:
		fFB->stateLock();
		out[0] = ++fFB->fSwapID;
		fFB->stateUnlock();
		outCount = 1;
		break;
	case kSweetFBSwapEnd: {
		uint8_t rec[SWEET_FB_SWAP_REC_SIZE];
		if (args->structureInputDescriptor != NULL ||
		    args->structureInputSize < SWEET_FB_SWAP_REC_SIZE) {
			ret = kIOReturnBadArgument;
			break;
		}
		memcpy(rec, args->structureInput, sizeof(rec));
		fFB->swap(rec);
		break;
	}
	case kSweetFBGetID:
		out[0] = 0;
		outCount = 1;
		break;
	case kSweetFBGetDisplaySize:
		out[0] = fFB->fLogicalW;
		out[1] = fFB->fLogicalH;
		outCount = 2;
		break;
	case kSweetFBIsMainDisplay:
		out[0] = 1;
		outCount = 1;
		break;
	case kSweetFBGetDotPitch:
		out[0] = SWEET_FB_DOT_PITCH;
		outCount = 1;
		break;
	case kSweetFBNotifications:
		if (args->scalarInputCount >= 3 && args->scalarInput[2] < SWEET_FB_NOTIFY_TYPES) {
			uint32_t type = (uint32_t)args->scalarInput[2];
			fFB->stateLock();
			fFunc[type] = args->scalarInput[0];
			fRefcon[type] = args->scalarInput[1];
			fFB->stateUnlock();
			char name[32];
			proc_selfname(name, sizeof(name));
			IOLog("SWEET FB: %s notifications type %u %s\n", name, type,
			    args->scalarInput[0] ? "on" : "off");
		}
		break;
	case kSweetFBRequestPowerChange:
		if (fFB->fPanelPowerMode) {
			ret=fFB->requestPanelPower(args->scalarInput[0]==0);
			break;
		}
		if (args->scalarInputCount >= 1) {
			bool off = args->scalarInput[0] == 0;
			if (off && fFB->fTrace) {
				fFB->traceFrame("off-before");
			}
			fFB->stateLock();
			if (off != fFB->fPowerOff) {
				fFB->fPowerOff = off;
				fFB->fFadeStep = 0;
				fFB->fDirty = true;     /* on: redraw the latest frame */
				if (fFB->fTrace) {
					if (off) fFB->fTraceOffAfter = true;
					else fFB->fTracePowerAfter = true;
				}
			}
			fFB->stateUnlock();
			IOLog("SWEET FB: display %s\n", off ? "off" : "on");
		}
		break;
	case kSweetFBSwapWait:
	default:
		break;
	}

	for (uint32_t i = 0; i < args->scalarOutputCount; i++) {
		args->scalarOutput[i] = i < outCount ? out[i] : 0;
	}
	if (args->structureOutputDescriptor == NULL && args->structureOutput != NULL) {
		bzero(args->structureOutput, args->structureOutputSize);
	}
	if (!fFB->fServiceMode && (selector >= 64 || !(sweet_fb_logged & (1ULL << selector)))) {
		if (selector < 64) {
			sweet_fb_logged |= 1ULL << selector;
		}
		char name[32];
		proc_selfname(name, sizeof(name));
		IOLog("SWEET FB: %s sel %u in %u/%u out %u/%u -> 0x%x\n", name, selector,
		    args->scalarInputCount, args->structureInputSize,
		    args->scalarOutputCount, args->structureOutputSize, ret);
	}
	if (fFB->fTrace && selector < 128) {
		uint32_t n = OSIncrementAtomic((volatile SInt32 *)&sweet_fb_selector_trace[selector]);
		fFB->stateLock();
		fFB->fTraceSelectorMask[selector / 64] |= 1ULL << (selector % 64);
		fFB->stateUnlock();
		if (n < SWEET_FB_TRACE_MAX_SELECTOR) {
			char name[32];
			proc_selfname(name, sizeof(name));
			uint64_t a0 = args->scalarInputCount > 0 ? args->scalarInput[0] : 0;
			uint64_t a1 = args->scalarInputCount > 1 ? args->scalarInput[1] : 0;
			uint64_t a2 = args->scalarInputCount > 2 ? args->scalarInput[2] : 0;
			uint64_t a3 = args->scalarInputCount > 3 ? args->scalarInput[3] : 0;
			uint32_t b0 = args->structureInput != NULL && args->structureInputSize >= 4 ? get32((const uint8_t *)args->structureInput, 0) : 0;
			SWEET_LOG_INFO("SWEET FB TRACE: v2 sel=%u n=%u proc=%s a=%llx,%llx,%llx,%llx struct0=%#x size=%u rc=0x%x\n",
			    selector, n + 1, name, a0, a1, a2, a3, b0, args->structureInputSize, ret);
		}
	}
	return ret;
}
#endif /* SWEET */
