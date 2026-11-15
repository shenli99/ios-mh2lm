/*
 * Built-in iOS battery bring-up service. Default: 100%, AC, not charging.
 * sweetbattery=1: read-only PM6150 polling every two seconds; voltage-derived
 * SOC is an UNCALIBRATED estimate, not a replacement for the Linux QG algo.
 * sweetbattery=1 sweetbatt=1: also read PM8150B at SPMI USID 2 -- raw MSOC
 * (0x4009 plus its 0x400a shadow, gated on 0x4010 bit1) with the voltage
 * curve only as fallback, real
 * VBATT (0x41A0/0x41A1) and SMB5 charging state (0x1006/0x1310). Unlike the
 * old all-or-nothing chain these blocks are independently valid. Without
 * sweetbatt the file behaves exactly as before. What the gauge reports is what
 * gets published: a poll whose percentage jumps by more than SWEET_BATT_SOC_JUMP
 * points keeps the previously published value until a second poll agrees, so the
 * number on screen cannot flicker between the gauge and the voltage curve.
 * MH2LM sweetbattirq=0/1 keeps explicit polling/baseline preflight; 2/3/4
 * stages USBIN, charger and gauge IRQ updates without periodic status polling.
 * See report.md for binary evidence, register provenance and integration.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <device/device_types.h> /* io_buf_ptr_t for private OSKext headers */
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOInterruptEventSource.h>
#include <kern/clock.h>
#include "../../../bsd/dev/sweet_spmi.h"
#include "../../../bsd/dev/sweet_adc5.h"
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "../../../bsd/dev/sweet_adc5_client.h"
#endif
#include "../../../bsd/dev/sweet_battery_runtime.h"
#include "../../../bsd/dev/sweet_charge_basic.h"
#include "../../../bsd/dev/sweet_pmic_power.h"
#include "../../../bsd/dev/sweet_spmi_mux.h"
#include "../../../bsd/dev/sweet_spmi_mux_client.h"
class SweetSpmiMuxClient;
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"
#include <IOKit/pwr_mgt/IOPMPowerSource.h>
#include <libkern/c++/OSCollectionIterator.h>
#include <pexpert/pexpert.h>
#include <sys/proc.h>
/* bsd/dev/sweet_spmi.c */


/* PM8150B carries both the SMB5 charger and the fuel gauge on guacamole, and
 * the DT places it at USID 2 (qcom,pm8150b@2, reg = <0x2 0x0>). Offsets are the
 * vendor fg-reg.h and smb5 definitions for the fg-gen4 blocks that same DT
 * declares: batt-soc@4000 and batt-info@4100. Everything below is read only. */
#define SWEET_BATT_SID		2u
#define SWEET_BATT_FG_SOC	0x4009u	/* BATT_SOC_FG_MONOTONIC_SOC raw */
#define SWEET_BATT_FG_SOC_CP	0x400au	/* BATT_SOC_FG_MONOTONIC_SOC_CP */
#define SWEET_BATT_FG_RT_STS	0x4010u	/* BATT_SOC_INT_RT_STS */
#define SWEET_BATT_SOC_READY	0x02u	/* bit1 SOC_READY: 0x4009 is live */
#define SWEET_BATT_VBATT	0x41a0u	/* BATT_INFO_VBATT_LSB, MSB at +1 */
#define SWEET_BATT_IBATT	0x41a2u	/* BATT_INFO_IBATT_LSB, MSB at +1 */
#define SWEET_BATT_IBATT_CP	0x41a8u	/* stable-copy IBATT LSB/MSB */
#define SWEET_BATT_SYS_BATT	0x4107u	/* bit2 BATT_MISSING_HW */
#define SWEET_BATT_BATT_MISSING	0x04u
#define SWEET_BATT_CHGR_STATUS	0x1006u	/* smb5 chgr status, bits 2:0 */
#define SWEET_BATT_CHGR_STATUS2	0x1007u	/* bit1 battery over-voltage */
#define SWEET_BATT_CHGR_STATUS5	0x100bu	/* trickle/pre/full-on enables */
#define SWEET_BATT_CHGR_STATUS7	0x100du	/* battery thermal limits */
#define SWEET_BATT_CHGR_ENABLE	0x1042u	/* bit0 charging enable command */
#define SWEET_BATT_CHGR_PAUSE	0x1043u	/* bit0 charging pause command */
#define SWEET_BATT_CHGR_CFG2	0x1051u	/* bit0 charger inhibit */
#define SWEET_BATT_SMB_EN	0x1648u	/* SMB enable override/status command */
#define SWEET_BATT_DCDC_PATH	0x110bu	/* USB/DC power-path status */
#define SWEET_BATT_USBIN_APSD	0x1307u	/* USB charger-detection status */
#define SWEET_BATT_USBIN_RT_STS	0x1310u	/* smb5 USB IN rt sts, bit4 = VBUS */
#define SWEET_BATT_USBIN_CMD_IL	0x1340u	/* bit0 explicitly suspends USB input */
#define SWEET_BATT_USBIN_ICL	0x1370u	/* PM8150B input-current limit, 50mA */
#define SWEET_BATT_USBIN_AICL	0x1380u	/* AICL/suspend-on-collapse policy */
#define SWEET_BATT_TYPEC_SNK	0x1506u	/* advertised Type-C sink current */
#define SWEET_BATT_TYPEC_STATE	0x1509u	/* Type-C attach/detach state */
#define SWEET_BATT_TYPEC_MISC	0x150bu	/* Type-C attached/VBUS status */
#define SWEET_BATT_SMB_CFG	0x1690u	/* bit4 selects the SMB_EN pin */
#define SWEET_BATT_POLL_MS	2000u	/* bounded observer reads; prompt UI state */
/* The VBATT LSB scale is not in the DT: gen4 documents 122070 uV, the older
 * gen3 part used 194637 uV. Take the raw word and keep whichever lands inside a
 * single lithium cell's range, so a wrong guess shows as "no voltage" instead
 * of a confident wrong number. */
#define SWEET_BATT_VBATT_UV_GEN4	122070u
#define SWEET_BATT_VBATT_UV_GEN3	194637u
#define SWEET_BATT_VBATT_MIN_MV	2800u
#define SWEET_BATT_VBATT_MAX_MV	4600u
/* A 30 s poll that moves the published percentage by more than this is held
 * back until the next poll confirms it: one torn or stale gauge byte must not
 * move what IOKit (and therefore the screen) already shows. */
#define SWEET_BATT_SOC_JUMP	5u

/* Opt-in tracing for the lock-screen plug/unplug investigation. It is read
 * from SweetBattery::start() before the service is published; the user client
 * later uses it only to increase logging, never to alter its ABI. */
static bool gSweetBattUiDiag;

class SweetBattery : public IOPMPowerSource {
	OSDeclareDefaultStructors(SweetBattery);
public:
	bool init(OSDictionary *dictionary = NULL) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	bool willTerminate(IOService *provider, IOOptionBits options) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;
private:

 unsigned fDependencyMode;
 bool fDependencyStarted, fDependencyFailed, fDependencyFinished, fDependencyConfirmed, fWatchdogArmed, fWatchdogPending, fWatchdogInput;
 unsigned fWatchdogPets;
 uint64_t fDependencyDeadlineNs, fDependencyReadAfterNs, fWatchdogLastNs, fWatchdogInputEpoch;
 SweetBattIdentity fIdentity;
 void refreshDependencies(uint64_t);
 int dependencyCommand(enum sweet_charger_operation, uint64_t, unsigned *);
 void dependencyFailure(const char *, int);
 void captureWatchdogFailure(void);
 unsigned fRuntimeMode, fRuntimeRequested;
	IOTimerEventSource *fMonitor;
	IOLock *fControlLock;
	IONotifier *fPowerNotice;
	bool fPowerStopping, fControlClaimed;
	bool fTempIrq;
	unsigned fTempIrqs;
	SweetChargeBasic fBasic;
	static int basicCommand(void *, enum sweet_charger_operation, unsigned, unsigned *);
	void runBasicCharge(uint64_t now);
	void removeRuntimeNotifier(void);
	static IOReturn runtimePowerNotice(void *, void *, UInt32, IOService *, void *, vm_size_t);
	bool fMonitorAdded;
	bool fAdcPreflightRequested;
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 SweetAdc5Client *fAdcClient;
 static void adcChanged(void *);
 void publishAdc(void);
#endif
	SweetBattSnapshot fSnapshot;
	SweetBattLimits fLimits;
	SweetBattInputSession fInputSession;
	bool inputChangePending(void);
	void refreshInputPermit(uint64_t now);
	SweetBattController fController;
	unsigned fMonitorSamples;
	unsigned fRuntimeInputEvents; /* owned by the battery workloop */
	uint32_t fLastReasons;
	void sampleRuntime(bool refreshInput);
	void runRuntimePolicy(void);
	void publishRuntime(void);
	static uint64_t runtimeClock(void *);
	static void monitorFired(OSObject *, IOTimerEventSource *);
	static IOReturn startRuntime(OSObject *, void *, void *, void *, void *);
	static int runtimeUpdate(void *, enum sweet_charger_operation, unsigned, uint64_t, unsigned *);
	void stopRuntime(void);
	void removePowerValue(const char *);
	IOWorkLoop *fLoop;
	IOTimerEventSource *fTimer;
	IOInterruptEventSource *fIrqEvent;
	bool fIrqEventAdded, fIrqAttached, fIrqFailed, fPreflightOk;
	SweetSpmiMuxClient *fMuxClient;
	bool fSettlePending;
	unsigned fIrqMode, fIrqSamples, fSettleBudget;
	volatile unsigned fIrqPending, fIrqNotifications;
	volatile uint64_t fIrqNotifyTime;
	bool fTimerAdded, fRunning, fReal;
	bool fSid2, fDiag, fUiDiag, fPercentValid;
	bool fUiPublished, fUiExternal, fUiCapable, fUiCharging, fUiFull;
	uint32_t fUiSequence;
	unsigned fLastMv, fLastPercent, fPendingPercent;
	int fLastMode;
	void putNumber(const char *key, UInt32 value);
	void putBool(const char *key, bool value);
	void publish(unsigned percent, unsigned mv, int ma, bool external,
	    bool capable, bool charging, bool full, bool measured, bool currentValid);
	void poll(void);
	void pollSid2(void);
	static void timerFired(OSObject *owner, IOTimerEventSource *sender);
	static void irqNotify(void *context, unsigned events);
	static void irqAction(OSObject *owner, IOInterruptEventSource *, int count);
	static IOReturn startIrqAction(OSObject *owner, void *, void *, void *, void *);
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	static IOReturn transportPreflightAction(OSObject *owner, void *, void *, void *, void *);
#endif
	void tryAttachIrq(void);
	void failIrq(const char *reason);
	bool preflightIrq(void);
	static IOReturn stopPolling(OSObject *owner, void *, void *, void *, void *);
	static bool readWord(unsigned sid, unsigned address, uint16_t *value);
	static bool readWordPair(unsigned sid, unsigned address, unsigned copyAddress,
	    uint16_t *value);
	static bool readBytePair(unsigned sid, unsigned address, unsigned copyAddress,
	    uint8_t *value);
	static unsigned scaleMsoc(uint8_t raw);
	static void logIrqLines(void);
	static void logChargePolicy(void);
	static void logChargeDiagnostics(unsigned state);
	static unsigned estimatePercent(unsigned mv);
};
OSDefineMetaClassAndStructors(SweetBattery, IOPMPowerSource);

/*
 * powerd accepts the generic source but also uses this exact iPhone-class
 * match to select its battery UI path.  Inherit every SweetBattery method and
 * state unchanged: the class name is the compatibility surface, not a second
 * controller or a second power source.
 */
class AppleSmartBatteryManager : public SweetBattery {
	OSDeclareDefaultStructors(AppleSmartBatteryManager);
public:
	bool init(OSDictionary *dictionary = NULL) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(AppleSmartBatteryManager, SweetBattery);

class AppleSmartBatteryManagerUserClient : public IOUserClient {
	OSDeclareDefaultStructors(AppleSmartBatteryManagerUserClient);
public:
	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
	IOReturn exportObjectToClient(task_t task, OSObject *object,
	    io_object_t *clientObject) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(AppleSmartBatteryManagerUserClient, IOUserClient);

bool
AppleSmartBatteryManager::init(OSDictionary *dictionary)
{
	if (!SweetBattery::init(dictionary)) {
		return false;
	}
	/* SweetBattery::start() registers the service, so do this in init(). */
	setProperty(kIOUserClientClassKey, "AppleSmartBatteryManagerUserClient");
	return true;
}

bool
AppleSmartBatteryManagerUserClient::initWithTask(task_t owningTask,
    void *securityID, UInt32 type)
{
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	char name[32];
	proc_selfname(name, sizeof(name));
	SWEET_LOG_INFO("SWEET BATT UC: %s open type %u\n", name, type);
	return true;
}

IOReturn
AppleSmartBatteryManagerUserClient::clientClose(void)
{
	terminate();
	return kIOReturnSuccess;
}

IOReturn
AppleSmartBatteryManagerUserClient::exportObjectToClient(__unused task_t task,
    OSObject *object, __unused io_object_t *clientObject)
{
	OSSafeReleaseNULL(object);
	return kIOReturnUnsupported;
}

IOReturn
AppleSmartBatteryManagerUserClient::externalMethod(uint32_t selector,
    IOExternalMethodArguments *args, __unused IOExternalMethodDispatch *dispatch,
    __unused OSObject *target, __unused void *reference)
{
	static uint64_t logged;
	bool first = selector >= 64 || !(logged & (1ULL << selector));
	if (selector < 64) {
		logged |= 1ULL << selector;
	}
	if (gSweetBattUiDiag || first) {
		char name[32];
		proc_selfname(name, sizeof(name));
		uint32_t structIn = args->structureInputDescriptor != NULL ?
		    (uint32_t)args->structureInputDescriptor->getLength() : args->structureInputSize;
		uint32_t structOut = args->structureOutputDescriptor != NULL ?
		    (uint32_t)args->structureOutputDescriptor->getLength() : args->structureOutputSize;
		uint64_t scalar0 = args->scalarInputCount ? args->scalarInput[0] : 0;
		SWEET_LOG_INFO("SWEET BATT UC: %s sel %u in %u/%u arg0=%#llx out %u/%u -> unsupported\n",
		    name, selector, args->scalarInputCount, structIn,
		    (unsigned long long)scalar0, args->scalarOutputCount, structOut);
	}
	return kIOReturnUnsupported;
}

bool
SweetBattery::init(OSDictionary *dictionary)
{
	/* IOCatalogue calls init(dictionary). This XNU's IOPMPowerSource only
	 * overrides init(void): explicitly run it to allocate its PS dictionary
	 * and symbols, then copy the matching personality into the registry. */
	if (!IOPMPowerSource::init()) {
		return false;
	}
	fRuntimeMode = fRuntimeRequested = fMonitorSamples = fRuntimeInputEvents = 0;
	fDependencyMode = fWatchdogPets = 0;
	fDependencyStarted = fDependencyFailed = fDependencyFinished = fDependencyConfirmed = fWatchdogArmed = fWatchdogPending = fWatchdogInput = false;
	fDependencyDeadlineNs = fDependencyReadAfterNs = fWatchdogLastNs = fWatchdogInputEpoch = 0; fIdentity = {};
	fLastReasons = ~0u;
	fMonitor = NULL;
	fControlLock = NULL;
	fPowerNotice = NULL;
	fPowerStopping = fControlClaimed = false;
	fTempIrq = false;
	fTempIrqs = 0;
	fBasic = {};
	fMonitorAdded = false;
	fSnapshot = {};
	fLimits = {};
	fInputSession = {};
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 fAdcClient = NULL;
#endif
	fController = {};
	fLoop = NULL;
	fTimer = NULL;
	fTimerAdded = fRunning = fReal = false;
	fIrqEvent = NULL;
	fIrqEventAdded = fIrqAttached = fIrqFailed = fPreflightOk = false;
	fMuxClient = NULL;
	fSettlePending = false;
	fIrqMode = fIrqSamples = fSettleBudget = 0;
	fIrqPending = fIrqNotifications = 0;
	fIrqNotifyTime = 0;
	fSid2 = fDiag = fUiDiag = false;
	fUiPublished = fUiExternal = fUiCapable = fUiCharging = fUiFull = false;
	fUiSequence = 0;
	fLastMv = 4200;	/* last real VBATT; static until a gauge word works */
	/* Nothing has been published yet, so the first real sample is adopted as
	 * is instead of being held back as an implausible jump. */
	fPercentValid = false;
	fLastPercent = fPendingPercent = 100;
	if (dictionary) {
		OSCollectionIterator *it = OSCollectionIterator::withCollection(dictionary);
		if (!it) {
			return false;
		}
		OSObject *object;
		bool ok = true;
		while ((object = it->getNextObject())) {
			const OSSymbol *key = OSDynamicCast(OSSymbol, object);
			if (!key || !setProperty(key, dictionary->getObject(key))) {
				ok = false;
				break;
			}
		}
		it->release();
		if (!ok) {
			return false;
		}
	}
	fLastMode = -1;
	return true;
}

void
SweetBattery::putNumber(const char *key, UInt32 value)
{
	const OSSymbol *symbol = OSSymbol::withCString(key);
	OSNumber *number = OSNumber::withNumber(value, 32);
	if (symbol && number) {
		setPSProperty(symbol, number);
	}
	OSSafeReleaseNULL(number);
	OSSafeReleaseNULL(symbol);
}

void
SweetBattery::putBool(const char *key, bool value)
{
	const OSSymbol *symbol = OSSymbol::withCString(key);
	if (symbol) {
		setPSProperty(symbol, value ? kOSBooleanTrue : kOSBooleanFalse);
		symbol->release();
	}
}

void
SweetBattery::publish(unsigned percent, unsigned mv, int ma, bool external,
    bool capable, bool charging, bool full, bool measured, bool currentValid)
{
	/* Use consistent mAh units (DT nominal=5020). powerd computes C*100/M. */
	unsigned maximum = 5020;
	if (fRuntimeMode) {
		maximum = fSnapshot.learnedValid ? fSnapshot.learnedMah :
		    (fSnapshot.nominalValid ? fSnapshot.nominalMah : 100u);
		ma = fSnapshot.currentValid ? fSnapshot.ma : 0;
		currentValid = fSnapshot.currentValid;
	}
	const unsigned capacity = (maximum * percent + 50u) / 100u;
	setBatteryInstalled(true);
	setCurrentCapacity(capacity);
	setMaxCapacity(maximum);
	setExternalConnected(external);
	setExternalChargeCapable(capable);
	setIsCharging(charging);
	setAtWarnLevel(false);
	/* An uncalibrated voltage curve must not assert a critical shutdown. */
	setAtCriticalLevel(false);
	setVoltage(mv);
	if (!fRuntimeMode || currentValid) { setAmperage(ma); }
	else { removePowerValue("Amperage"); }
	setTimeRemaining(-1); /* Unknown; never publish zero minutes on error. */
	if (!fRuntimeMode) { setCycleCount(0); }
	else { removePowerValue("CycleCount"); }
	putBool("FullyCharged", full);
	putBool("AppleRawExternalConnected", external);
	putNumber("AppleRawCurrentCapacity", capacity);
	putNumber("AppleRawMaxCapacity", maximum);
	if (!fRuntimeMode) { putNumber("DesignCapacity", 5020); }
	else if (fSnapshot.nominalValid) { putNumber("DesignCapacity", fSnapshot.nominalMah); }
	else { removePowerValue("DesignCapacity"); }
	if (!fRuntimeMode || currentValid) { putNumber("InstantAmperage", (UInt32)ma); }
	else { removePowerValue("InstantAmperage"); }
	/* iOS battery convention: centidegrees C; fixed 25 C, NOT measured. */
	if (!fRuntimeMode) { putNumber("Temperature", 2500); }
	else if (fSnapshot.tempValid) { putNumber("Temperature", (UInt32)fSnapshot.tempCenti); }
	else { removePowerValue("Temperature"); }
	if (fRuntimeMode) {
		putBool("SweetBatteryTemperatureValid", fSnapshot.tempValid);
		putBool("SweetBatteryCapacityValid", fSnapshot.nominalValid);
		putBool("SweetBatteryCapacityUsesPercent", !fSnapshot.nominalValid && !fSnapshot.learnedValid);
		putBool("SweetBatteryCycleCountValid", false);
		putBool("SweetBatteryVoltageValid", fSnapshot.voltageValid);
		putBool("SweetBatteryInstalledValid", fSnapshot.presentValid);
		if (fSnapshot.presentValid) { setBatteryInstalled(fSnapshot.present); }
		putNumber("SweetBatteryPolicyReasons", fLastReasons);
		putNumber("SweetBatteryControlState", (unsigned)fController.state);
		putBool("SweetBatteryControlOwned", fController.owned);
		putBool("SweetBatteryStopConfirmed", fController.owned && fController.stopConfirmed);
	}
	putBool("SweetBatteryHardwareSampleValid", measured);
	putBool("SweetBatteryCurrentValid", currentValid);
	putBool("SweetBatterySOCEstimated", fRuntimeMode ? !fSnapshot.socValid : measured);
	putBool("SweetBatteryTemperatureEstimated", !fRuntimeMode || !fSnapshot.tempValid);
	putBool("SweetBatteryFallback", !measured);
	putNumber("SweetBatteryPercent", percent);

	/* IOPMCopyBatteryInfo's per-source compatibility dictionary. */
	OSDictionary *legacy = OSDictionary::withCapacity(7);
	if (legacy) {
		const char *keys[] = {kIOBatteryCurrentChargeKey, kIOBatteryCapacityKey,
		    kIOBatteryVoltageKey, kIOBatteryAmperageKey, kIOBatteryCycleCountKey,
		    kIOBatteryFlagsKey};
		UInt32 values[] = {capacity, maximum, mv, (UInt32)ma, 0,
		    (UInt32)(kIOBatteryInstalled | (external ? kIOBatteryChargerConnect : 0) |
		    (charging ? kIOBatteryCharge : 0))};
		for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
			if (fRuntimeMode && (i == 4 || (i == 3 && !currentValid))) { continue; }
			OSNumber *number = OSNumber::withNumber(values[i], 32);
			if (number) {
				legacy->setObject(keys[i], number);
				number->release();
			}
		}
		setLegacyIOBatteryInfo(legacy);
		legacy->release();
	}
	/* Log only an externally visible source transition. This makes a plug or
	 * charge-state transition unambiguous in the circular persistent log while
	 * retaining updateStatus() as the only IOPM notification mechanism. */
	bool uiChanged = !fUiPublished || external != fUiExternal ||
	    capable != fUiCapable || charging != fUiCharging ||
	    full != fUiFull;
	if (fUiDiag && uiChanged) {
		fUiSequence++;
		SWEET_LOG_INFO("SWEET BATT UI: event=%u publish external=%d capable=%d charging=%d "
		    "full=%d percent=%u mv=%u sample=%d current-valid=%d\n",
		    fUiSequence, external ? 1 : 0, capable ? 1 : 0,
		    charging ? 1 : 0, full ? 1 : 0, percent, mv,
		    measured ? 1 : 0, currentValid ? 1 : 0);
	}
	updateStatus(); /* Flush PS properties and send 0xe0024100 on change. */
	if (fUiDiag && uiChanged) {
		SWEET_LOG_INFO("SWEET BATT UI: event=%u updateStatus complete\n", fUiSequence);
	}
	fUiPublished = true;
	fUiExternal = external;
	fUiCapable = capable;
	fUiCharging = charging;
	fUiFull = full;
	int mode = measured ? 1 : 0;
	if (mode != fLastMode) {
		SWEET_LOG_INFO("SWEET BATTERY: %s, %u%% %umV external=%d charging=%d\n",
		    !measured ? (fIrqFailed ? "IRQ fault / sample invalid" : "static fallback") :
		    (fSid2 ? "PM6150B / gauge SOC" : "PM6150 / estimated SOC"),
		    percent, mv, external, charging);
		fLastMode = mode;
	}
}

/* Keep the proven single-byte observer command. Read two matching whole
 * words to reject most torn ADC samples without writing the QG HOLD bit.
 * This is a bounded consistency check, not a hardware snapshot guarantee. */
bool
SweetBattery::readWord(unsigned sid, unsigned address, uint16_t *value)
{
	uint16_t previous = 0;
	for (unsigned attempt = 0; attempt < 4; ++attempt) {
		uint8_t lo, hi;
		if (sweet_spmi_read(sid, address, &lo) ||
		    sweet_spmi_read(sid, address + 1, &hi)) {
			return false;
		}
		uint16_t next = (uint16_t)((unsigned)lo | ((unsigned)hi << 8));
		if (attempt && next == previous) {
			*value = next;
			return true;
		}
		previous = next;
	}
	return false;
}

/* FG exposes an IBATT copy register specifically for a consistent raw sample.
 * Do not write its hold bits: a matching pair is enough for diagnostics. */
bool
SweetBattery::readWordPair(unsigned sid, unsigned address, unsigned copyAddress,
    uint16_t *value)
{
	for (unsigned attempt = 0; attempt < 4; ++attempt) {
		uint8_t lo, hi, copyLo, copyHi;
		if (sweet_spmi_read(sid, address, &lo) ||
		    sweet_spmi_read(sid, address + 1, &hi) ||
		    sweet_spmi_read(sid, copyAddress, &copyLo) ||
		    sweet_spmi_read(sid, copyAddress + 1, &copyHi)) {
			return false;
		}
		uint16_t sample = (uint16_t)((unsigned)lo | ((unsigned)hi << 8));
		uint16_t copy = (uint16_t)((unsigned)copyLo | ((unsigned)copyHi << 8));
		if (sample == copy) {
			*value = sample;
			return true;
		}
	}
	return false;
}

/* PM8150B's MONOTONIC_SOC is a byte with an adjacent shadow copy, not a
 * literal percentage. Match the Linux FG driver's bounded consistency check
 * before scaling it. A failed pair is unknown, never 0%. */
bool
SweetBattery::readBytePair(unsigned sid, unsigned address, unsigned copyAddress,
    uint8_t *value)
{
	for (unsigned attempt = 0; attempt < 4; ++attempt) {
		uint8_t sample, copy;
		if (sweet_spmi_read(sid, address, &sample) ||
		    sweet_spmi_read(sid, copyAddress, &copy)) {
			return false;
		}
		if (sample == copy) {
			*value = sample;
			return true;
		}
	}
	return false;
}

unsigned
SweetBattery::scaleMsoc(uint8_t raw)
{
	/* Linux fg_get_msoc(): endpoints stay 0/100; raw 1..254 maps to 1..99
	 * with nearest-integer rounding. In particular 0xa8 is 66%, not invalid. */
	if (raw == 0xffu) {
		return 100u;
	}
	if (raw == 0u) {
		return 0u;
	}
	return (((unsigned)(raw - 1u) * 98u + 126u) / 253u) + 1u;
}

void
SweetBattery::logIrqLines(void)
{
	/* PMIC peripheral IRQ configuration is separate from the PMIC-arb APID
	 * route. These reads prove whether firmware has enabled the two lines we
	 * would later claim; they neither clear latches nor alter charger policy. */
	struct irqLine {
		const char *name;
		unsigned peripheral;
		unsigned bit;
	};
	static const irqLine lines[] = {
		{ "usbin-plugin", 0x13u, 4u },
		{ "chg-state-change", 0x10u, 1u },
	};
	for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
		const irqLine *line = &lines[i];
		unsigned base = line->peripheral << 8;
		uint8_t rt = 0, type = 0, high = 0, low = 0, enabled = 0, latched = 0;
		bool ok = !sweet_spmi_read(SWEET_BATT_SID, base + 0x10u, &rt) &&
		    !sweet_spmi_read(SWEET_BATT_SID, base + 0x11u, &type) &&
		    !sweet_spmi_read(SWEET_BATT_SID, base + 0x12u, &high) &&
		    !sweet_spmi_read(SWEET_BATT_SID, base + 0x13u, &low) &&
		    !sweet_spmi_read(SWEET_BATT_SID, base + 0x15u, &enabled) &&
		    !sweet_spmi_read(SWEET_BATT_SID, base + 0x18u, &latched);
		unsigned mask = 1u << line->bit;
		SWEET_LOG_INFO("SWEET BATT IRQ CFG: %s bit=%u rt=%u edge=%u high=%u low=%u "
		    "enabled=%u latched=%u read-ok=%d\n", line->name, line->bit,
		    ((unsigned)rt & mask) ? 1 : 0, ((unsigned)type & mask) ? 1 : 0,
		    ((unsigned)high & mask) ? 1 : 0, ((unsigned)low & mask) ? 1 : 0,
		    ((unsigned)enabled & mask) ? 1 : 0, ((unsigned)latched & mask) ? 1 : 0,
		    ok ? 1 : 0);
	}
}

/* A failed read is unknown, never a decoded zero. Each diagnostic register is
 * attempted independently so inaccessible Type-C fields cannot hide SMB_CFG.
 * The observer API owns the controller transaction lock; diagnostics add no
 * PMIC writes, polling, retries, or changes to published power-source state. */
struct SweetBattDiagByte {
	uint8_t value;
	int error;
};

static SweetBattDiagByte
sweet_batt_diag_read(unsigned address)
{
	SweetBattDiagByte sample = {0, 0};
	sample.error = sweet_spmi_read(SWEET_BATT_SID, address, &sample.value);
	if (sample.error) {
		SWEET_LOG_WARN("SWEET BATT DIAG READ: sid=%u addr=%#x error=%d; value=unknown\n",
		    SWEET_BATT_SID, address, sample.error);
	}
	return sample;
}

static const char *
sweet_batt_diag_raw(const SweetBattDiagByte &sample, char *buffer)
{
	if (sample.error) {
		return "unknown";
	}
	snprintf(buffer, 16, "%#x", (unsigned)sample.value);
	return buffer;
}

static int
sweet_batt_diag_bit(const SweetBattDiagByte &sample, unsigned mask)
{
	return sample.error ? -1 : ((sample.value & mask) ? 1 : 0);
}

static unsigned
sweet_batt_diag_mask(const SweetBattDiagByte *samples, unsigned count)
{
	unsigned mask = 0;
	for (unsigned i = 0; i < count; ++i) {
		if (!samples[i].error) {
			mask |= 1u << i;
		}
	}
	return mask;
}

void
SweetBattery::logChargePolicy(void)
{
	/* Bit positions in each read-ok mask follow the fields' raw read order:
	 * INPUT path, APSD, CMD_IL, ICL; TYPEC AICL, sink, state, misc;
	 * SMB enable, configuration. A decoded -1 means unknown.
	 * The current observer whitelist rejects Type-C peripheral 0x15 in
	 * software. Preserve that access boundary; do not mistake its failures
	 * for measured zero or skip the separately readable SMB fields. */
	const SweetBattDiagByte input[] = {
		sweet_batt_diag_read(SWEET_BATT_DCDC_PATH),
		sweet_batt_diag_read(SWEET_BATT_USBIN_APSD),
		sweet_batt_diag_read(SWEET_BATT_USBIN_CMD_IL),
		sweet_batt_diag_read(SWEET_BATT_USBIN_ICL),
	};
	const SweetBattDiagByte typec[] = {
		sweet_batt_diag_read(SWEET_BATT_USBIN_AICL),
		sweet_batt_diag_read(SWEET_BATT_TYPEC_SNK),
		sweet_batt_diag_read(SWEET_BATT_TYPEC_STATE),
		sweet_batt_diag_read(SWEET_BATT_TYPEC_MISC),
	};
	const SweetBattDiagByte smb[] = {
		sweet_batt_diag_read(SWEET_BATT_SMB_EN),
		sweet_batt_diag_read(SWEET_BATT_SMB_CFG),
	};
	char raw[4][16];
	SWEET_LOG_INFO("SWEET BATT INPUT: path=%s use-usb=%d valid-input=%d path-suspend=%d "
	    "cmd-il=%s cmd-suspend=%d icl=%dmA apsd=%s dtc-done=%d enum=%d slow-plugin=%d read-ok=%#x/0xf\n",
	    sweet_batt_diag_raw(input[0], raw[0]), sweet_batt_diag_bit(input[0], 0x10u),
	    sweet_batt_diag_bit(input[0], 0x01u), sweet_batt_diag_bit(input[0], 0x40u),
	    sweet_batt_diag_raw(input[2], raw[1]), sweet_batt_diag_bit(input[2], 0x01u),
	    input[3].error ? -1 : (int)input[3].value * 50,
	    sweet_batt_diag_raw(input[1], raw[2]), sweet_batt_diag_bit(input[1], 0x01u),
	    sweet_batt_diag_bit(input[1], 0x10u), sweet_batt_diag_bit(input[1], 0x20u),
	    sweet_batt_diag_mask(input, 4));
	SWEET_LOG_INFO("SWEET BATT TYPEC: aicl=%s aicl-en=%d aicl-adc=%d aicl-rerun=%d "
	    "collapse-suspend=%d sink=%s typec=%s state-attached=%d misc=%s "
	    "cc-attached=%d vbus=%d read-ok=%#x/0xf\n",
	    sweet_batt_diag_raw(typec[0], raw[0]), sweet_batt_diag_bit(typec[0], 0x04u),
	    sweet_batt_diag_bit(typec[0], 0x08u), sweet_batt_diag_bit(typec[0], 0x10u),
	    sweet_batt_diag_bit(typec[0], 0x80u), sweet_batt_diag_raw(typec[1], raw[1]),
	    sweet_batt_diag_raw(typec[2], raw[2]), sweet_batt_diag_bit(typec[2], 0x20u),
	    sweet_batt_diag_raw(typec[3], raw[3]), sweet_batt_diag_bit(typec[3], 0x01u),
	    sweet_batt_diag_bit(typec[3], 0x20u), sweet_batt_diag_mask(typec, 4));
	SWEET_LOG_INFO("SWEET BATT SMB: en=%s override=%d override-val=%d cfg=%s "
	    "smb-sel=%d read-ok=%#x/0x3\n", sweet_batt_diag_raw(smb[0], raw[0]),
	    sweet_batt_diag_bit(smb[0], 0x08u), sweet_batt_diag_bit(smb[0], 0x10u),
	    sweet_batt_diag_raw(smb[1], raw[1]), sweet_batt_diag_bit(smb[1], 0x10u),
	    sweet_batt_diag_mask(smb, 2));
}

void
SweetBattery::logChargeDiagnostics(unsigned state)
{
	/* config-valid bits: STATUS2, STATUS5, STATUS7, ENABLE, PAUSE, CFG2, SMB_EN.
	 * Keep the aggregate config-ok for old capture readers, but never let one
	 * failed field suppress the remaining configuration or IBATT reads. */
	const SweetBattDiagByte config[] = {
		sweet_batt_diag_read(SWEET_BATT_CHGR_STATUS2),
		sweet_batt_diag_read(SWEET_BATT_CHGR_STATUS5),
		sweet_batt_diag_read(SWEET_BATT_CHGR_STATUS7),
		sweet_batt_diag_read(SWEET_BATT_CHGR_ENABLE),
		sweet_batt_diag_read(SWEET_BATT_CHGR_PAUSE),
		sweet_batt_diag_read(SWEET_BATT_CHGR_CFG2),
		sweet_batt_diag_read(SWEET_BATT_SMB_EN),
	};
	uint16_t rawCurrent = 0;
	bool currentOk = readWordPair(SWEET_BATT_SID, SWEET_BATT_IBATT,
	    SWEET_BATT_IBATT_CP, &rawCurrent);
	char raw[7][16], ibatt[16], signedValue[16], ua[16];
	if (currentOk) {
		int signedCurrent = (rawCurrent & 0x8000u) ? (int)rawCurrent - 65536 :
		    (int)rawCurrent;
		/* Qualcomm FG convention: 488.281 uA/step, negative charges the pack. */
		int currentUa = (int)(((int64_t)signedCurrent * 488281) / 1000);
		snprintf(ibatt, sizeof(ibatt), "%#x", (unsigned)rawCurrent);
		snprintf(signedValue, sizeof(signedValue), "%d", signedCurrent);
		snprintf(ua, sizeof(ua), "%d", currentUa);
	}
	unsigned valid = sweet_batt_diag_mask(config, 7);
	SWEET_LOG_INFO("SWEET BATT DIAG: state=%u st2=%s st5=%s st7=%s en=%s "
	    "pause=%s cfg2=%s smben=%s ibatt=%s signed=%s ua=%s "
	    "config-ok=%d current-ok=%d config-valid=%#x/0x7f\n", state,
	    sweet_batt_diag_raw(config[0], raw[0]), sweet_batt_diag_raw(config[1], raw[1]),
	    sweet_batt_diag_raw(config[2], raw[2]), sweet_batt_diag_raw(config[3], raw[3]),
	    sweet_batt_diag_raw(config[4], raw[4]), sweet_batt_diag_raw(config[5], raw[5]),
	    sweet_batt_diag_raw(config[6], raw[6]), currentOk ? ibatt : "unknown",
	    currentOk ? signedValue : "unknown", currentOk ? ua : "unknown",
	    valid == 0x7fu ? 1 : 0, currentOk ? 1 : 0, valid);
	logChargePolicy();
}

unsigned
SweetBattery::estimatePercent(unsigned mv)
{
	/* Generic single-cell curve, intentionally NOT advertised as QG SOC.
	 * Load sag, temperature, aging and charging introduce large errors. */
	static const unsigned volts[] = {3400, 3500, 3600, 3700, 3800, 3900,
	    4000, 4100, 4200, 4350};
	static const unsigned soc[] = {1, 5, 10, 20, 40, 60, 75, 85, 93, 100};
	if (mv <= volts[0]) {
		return soc[0];
	}
	for (unsigned i = 1; i < sizeof(volts) / sizeof(volts[0]); ++i) {
		if (mv < volts[i]) {
			return soc[i - 1] + (mv - volts[i - 1]) * (soc[i] - soc[i - 1]) /
			       (volts[i] - volts[i - 1]);
		}
	}
	return 100;
}

void
SweetBattery::poll(void)
{
	/* sweetbatt unset: take the old path unchanged, reads included. */
	if (fSid2) {
		pollSid2();
		return;
	}
	uint8_t type, present, usb, path, charger, error, temperature;
	uint16_t rawVoltage;
	bool valid = fReal &&
	    !sweet_spmi_read(0, 0x4804, &type) && type == 0x0d &&
	    !sweet_spmi_read(0, 0x4808, &present) && (present & 1u) &&
	    readWord(0, 0x48c0, &rawVoltage) && rawVoltage != 0x8000 &&
	    !sweet_spmi_read(0, 0x1310, &usb) &&
	    !sweet_spmi_read(0, 0x110b, &path) &&
	    !sweet_spmi_read(0, 0x1006, &charger) &&
	    !sweet_spmi_read(0, 0x1007, &error) &&
	    !sweet_spmi_read(0, 0x100d, &temperature);
	unsigned mv = valid ? (unsigned)(((uint64_t)rawVoltage * 194637u) / 1000000u) : 0;
	/* Zero, reset, floating, or implausible data cannot become 0% battery. */
	valid = valid && mv >= 2800 && mv <= 4600;
	if (!valid) {
		publish(100, 4200, 0, true, true, false, true, false, false);
		return;
	}
	bool external = (usb & 0x10u) != 0;
	bool capable = external && (path & 0x10u) && (path & 1u) && !(path & 0x40u);
	unsigned state = charger & 7u;
	bool charging = capable && state >= 1 && state <= 4 &&
	    !(error & 2u) && !(temperature & 0x0cu);
	/* Inhibit (0) does not by itself establish full capacity. */
	bool full = external && state == 5 && !(error & 2u);
	int ma = 0;
	uint8_t subtype;
	uint16_t rawCurrent;
	bool currentValid = !sweet_spmi_read(0, 0x4805, &subtype) &&
	    (subtype == 3 || subtype == 4) && readWord(0, 0x48c2, &rawCurrent) &&
	    rawCurrent != 0x8000;
	if (currentValid) {
		int signedRaw = (rawCurrent & 0x8000u) ? (int)rawCurrent - 65536 : (int)rawCurrent;
		/* Linux: negative=charging; IOPMPowerSource: positive=charging, mA. */
		ma = (int)(-((int64_t)signedRaw * (subtype == 3 ? 152588 : 305176)) / 1000000);
	}
	publish(full ? 100 : estimatePercent(mv), mv, ma, external, capable,
	    charging, full, true, currentValid);
}

/*
 * Real PM8150B read path, enabled by boot-arg sweetbatt=1. Same discipline as
 * the old path: one observer EXT_READL per byte, no PMIC write anywhere, and a
 * failed read is "unknown" rather than zero. SOC, voltage and charger state are
 * independently valid, so one dead block can no longer collapse the battery to
 * the static 100%.
 */
void
SweetBattery::pollSid2(void)
{
	uint8_t soc = 0, socSts = 0, sysBatt = 0, chgr = 0, usb = 0;
	uint16_t rawVoltage = 0;
	unsigned uv = SWEET_BATT_VBATT_UV_GEN4;
	unsigned mv;

	if (!fReal) {
		publish(100, 4200, 0, true, true, false, true, false, false);
		return;
	}
	/* 0x4010 bit1 gates 0x4009: without SOC_READY the MSOC shadow pair is
	 * stale. Linux compares 0x4009 and 0x400a before it scales raw 0..255. */
	bool socOk = !sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_FG_RT_STS,
	    &socSts) && (socSts & SWEET_BATT_SOC_READY) &&
	    readBytePair(SWEET_BATT_SID, SWEET_BATT_FG_SOC,
	    SWEET_BATT_FG_SOC_CP, &soc);
	bool mvOk = readWord(SWEET_BATT_SID, SWEET_BATT_VBATT, &rawVoltage) &&
	    rawVoltage != 0 && rawVoltage != 0xffffu;
	mv = mvOk ? (unsigned)(((uint64_t)rawVoltage * uv) / 1000000u) : 0;
	if (mvOk && (mv < SWEET_BATT_VBATT_MIN_MV || mv > SWEET_BATT_VBATT_MAX_MV)) {
		uv = SWEET_BATT_VBATT_UV_GEN3;
		mv = (unsigned)(((uint64_t)rawVoltage * uv) / 1000000u);
	}
	mvOk = mvOk && mv >= SWEET_BATT_VBATT_MIN_MV &&
	    mv <= SWEET_BATT_VBATT_MAX_MV;
	/* Only a successful read that says "hardware missing" counts as absent;
	 * an unreadable byte is unknown and never invalidates the rest. */
	bool missing = !sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_SYS_BATT,
	    &sysBatt) && (sysBatt & SWEET_BATT_BATT_MISSING);
	bool chgOk = !sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_CHGR_STATUS,
	    &chgr) && !sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_USBIN_RT_STS,
	    &usb);
	if (fIrqMode >= 2 && (!socOk || !chgOk || missing)) {
		/* Preserve the last real source; never turn an IRQ/transport fault
		 * into a static full battery or a silent polling fallback. */
		failIrq("gauge/charger sample invalid or battery missing");
		return;
	}
	if (!socOk && !mvOk && !chgOk) {
		/* Nothing on the PMIC answered: the one case that keeps the old
		 * static source, so an absent charger is never reported as absent
		 * battery. */
		publish(100, 4200, 0, true, true, false, true, false, false);
		SWEET_LOG_ERROR("SWEET BATTERY SID2: every read failed, static fallback\n");
		return;
	}
	/* A gauge-less or pack-less sample must never become a 0% shutdown. */
	bool measured = (socOk || mvOk) && !missing;
	bool external = chgOk && (usb & 0x10u) != 0;
	unsigned state = chgr & 7u;
	/* The unproven 0x1007/0x100d gates are dropped: the working rmx1931 port
	 * decodes these two bytes and nothing else. 0x110b (charge path) is
	 * equally unproven, so "capable" follows VBUS presence instead. */
	bool charging = external && state >= 1u && state <= 4u;
	/* The percentage that goes to IOKit: the stable MSOC shadow pair whenever
	 * it is ready, the single-cell curve only when the gauge read fails. */
	unsigned curve = mvOk ? estimatePercent(mv) : 0u;
	unsigned raw = socOk ? scaleMsoc(soc) :
	    (mvOk ? curve : 100u);
	/* Which of the two said it, for the log line below. */
	const char *src = socOk ? "gauge" : (mvOk ? "curve" : "none");
	if (!measured) {
		/* No usable SOC and no usable pack: keep the old static source
		 * rather than publish a number derived from nothing. */
		raw = 100u;
		src = "none";
	}
	/* SMB5 state 5 is "charging inhibited", which is not a capacity claim.
	 * Calling that full at the gauge's 59 % is exactly the inconsistency this
	 * patch removes, so only the gauge itself may reach 100 %. */
	bool full = external && state == 5u && raw >= 99u;
	/* Flicker guard. A poll that moves more than SWEET_BATT_SOC_JUMP points
	 * holds the previously published value, unless the poll before it already
	 * reported that same value (two in a row agreeing means the pack really
	 * moved). A photo of the log shows raw= vs the published number. */
	unsigned published = raw;
	if (!fPercentValid) {
		/* First usable sample after boot: adopt it, nothing to hold. */
		fPercentValid = true;
	} else {
		unsigned gap = raw > fLastPercent ? raw - fLastPercent :
		    fLastPercent - raw;
		if (gap > SWEET_BATT_SOC_JUMP && raw != fPendingPercent) {
			published = fLastPercent;	/* keep what the UI shows */
			if (fIrqMode >= 2 && fSettleBudget && fTimer) {
				fSettlePending = true;
				fTimer->setTimeoutMS(100); /* at most one confirmation per event */
			}
		}
	}
	fPendingPercent = raw;
	fLastPercent = published;
	if (mvOk) {
		fLastMv = mv;	/* hold the last real voltage over a failed word */
	}
	/* Exactly one line per poll: the raw bytes, the percentage the PMIC gave
	 * (src=gauge is 0x4009, src=curve is the fallback table) and the one
	 * actually published, so a photo of the log proves both what was read and
	 * what iOS was told. held=1 means the guard kept the previous value. */
	SWEET_LOG_INFO("SWEET BATTERY SID2: msoc=%#x scaled=%u%% sts=%#x ready=%d "
	    "vbatt=%#x mv=%u uv=%u curve=%u%% chgr=%#x usb=%#x sysbatt=%#x "
	    "raw=%u%% src=%s held=%d "
	    "-> %u%% %umV external=%d charging=%d full=%d\n",
	    (unsigned)soc, socOk ? scaleMsoc(soc) : 0u, (unsigned)socSts,
	    (socSts & SWEET_BATT_SOC_READY) ? 1 : 0, (unsigned)rawVoltage, mv, uv,
	    curve, (unsigned)chgr, (unsigned)usb, (unsigned)sysBatt, raw, src,
	    published != raw ? 1 : 0, published, fLastMv,
	    external ? 1 : 0, charging ? 1 : 0, full ? 1 : 0);
	/* The optional diagnostic never changes published state or writes a PMIC
	 * register. It exists solely to distinguish VBUS presence from a real charge
	 * current and to expose the hardware gates behind DISABLE_CHARGE. */
	if (fDiag) {
		logChargeDiagnostics(state);
	}

	if (fRuntimeMode) {
		sampleRuntime(true);
		runRuntimePolicy();
		publishRuntime();
		return;
	}
	publish(published, fLastMv, 0, external, external, charging, full,
	    measured, false);
}

bool
SweetBattery::preflightIrq(void)
{
	uint8_t rev[6] = {0};
	bool ok = true;
	for (unsigned i = 0; i < 6; ++i) {
		if (sweet_spmi_read(2, 0x100u + i, &rev[i]) != 0) {
			ok = false;
		}
	}
	uint8_t socSubtype = 0;
	bool socOk = sweet_spmi_read(2, 0x4005u, &socSubtype) == 0;
	bool match = ok && rev[4] == 0x51u && rev[5] == 0x20u &&
	    socOk && socSubtype == 0x10u;
	SWEET_LOG_INFO("SWEET BATT IRQ: REVID sid=2 rev=%#x/%#x/%#x/%#x type=%#x subtype=%#x soc-subtype=%#x read-ok=%u/%u pm8150b=%u\n",
	    rev[0], rev[1], rev[2], rev[3], rev[4], rev[5], socSubtype,
	    ok ? 1 : 0, socOk ? 1 : 0, match ? 1 : 0);
	/* Global metadata/owner bitmap only; no new FG channel MMIO reads here. */
	sweet_spmi_irq_diag_source_parts(2, 0x40, "battery-fg-soc",
	    SWEET_SPMI_IRQ_DIAG_ROUTE_META | SWEET_SPMI_IRQ_DIAG_ROUTE_OWNER);
	uint8_t regs[6] = {0};
	const unsigned offsets[] = {0x10, 0x11, 0x12, 0x13, 0x15, 0x18};
	bool cfgOk = true;
	for (unsigned i = 0; i < 6; ++i) {
		if (sweet_spmi_read(2, 0x4000u + offsets[i], &regs[i]) != 0) {
			cfgOk = false;
		}
	}
	SWEET_LOG_INFO("SWEET BATT IRQ: FG preflight rt=%#x edge=%#x high=%#x low=%#x enable=%#x latch=%#x read-ok=%u; SRAM threshold unchanged/unmeasured\n",
	    regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], cfgOk ? 1 : 0);
	return match && cfgOk;
}

void
SweetBattery::failIrq(const char *reason)
{
	if (fIrqFailed) {
		return;
	}
	fIrqFailed = true;
	stopRuntime();
	fRunning = false;
	if (fTimer) {
		fTimer->cancelTimeout();
	}
	SWEET_LOG_ERROR("SWEET BATT IRQ: FAILED %s mode=%u notifications=%u samples=%u; no polling fallback\n",
	    reason, fIrqMode, __atomic_load_n(&fIrqNotifications, __ATOMIC_ACQUIRE), fIrqSamples);
	putBool("SweetBatteryIRQHealthy", false);
	putBool("SweetBatteryIRQWaitingForMux", false);
	putBool("SweetBatteryHardwareSampleValid", false);
	/* Stop discovery, but also withdraw the last published cable/charge state.
	 * Unknown input is not a fresh disconnected measurement: validity stays
	 * false while the IOPM booleans cease advertising an unusable source. */
	fSnapshot.valid = 0;
	fSnapshot.apsdFromSource = fSnapshot.ccFromEvent = false;
	fSnapshot.socValid = fSnapshot.currentValid = fSnapshot.voltageValid = false;
	fSnapshot.tempValid = fSnapshot.presentValid = false;
	putBool("SweetBatteryExternalValid", false);
	putBool("SweetBatteryChargeStateValid", false);
	if (fRuntimeMode) {
		fController.fault = true;
		fController.faultReasons |= SB_R_TRANSPORT;
		fController.state = SB_FAULT;
		fLastReasons |= SB_R_TRANSPORT | SB_R_STOPPED;
#if defined(ARM64_BOARD_CONFIG_MH2LM)
		if (fAdcClient) { fAdcClient->invalidate(); publishAdc(); }
#endif
		publishRuntime();
	} else {
		publish(fLastPercent, fLastMv, 0, false, false, false, false, false, false);
	}
	SWEET_LOG_ERROR("SWEET BATT IRQ: published input invalid; external/charging state withdrawn\n");
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	if (fMuxClient) {
		fMuxClient->detach();
	}
	fIrqAttached = false;
#endif
}

void
SweetBattery::irqNotify(void *context, unsigned events)
{
	SweetBattery *self = (SweetBattery *)context;
	if (events & SWEET_BATT_IRQ_ALL) {
		uint64_t now;
		clock_get_uptime(&now);
		__atomic_store_n(&self->fIrqNotifyTime, now, __ATOMIC_RELEASE);
		__atomic_add_fetch(&self->fIrqNotifications, 1u, __ATOMIC_RELAXED);
	}
	__atomic_fetch_or(&self->fIrqPending, events, __ATOMIC_RELEASE);
	/* Per-consumer connection delivers on our loop. Only queue this action. */
	self->fIrqEvent->interruptOccurred(NULL, NULL, 0);
}

void
SweetBattery::irqAction(OSObject *owner, IOInterruptEventSource *, int count)
{
	SweetBattery *self = OSDynamicCast(SweetBattery, owner);
	if (!self || !self->fRunning) {
		return;
	}
	unsigned pending = __atomic_exchange_n(&self->fIrqPending, 0u, __ATOMIC_ACQ_REL);
	if (pending & SWEET_MUX_FAILED) {
		self->failIrq("mux connection unavailable; consumer attach or provider failure");
		return;
	}
	if (pending & SWEET_MUX_READY) {
		SWEET_LOG_INFO("SWEET BATT IRQ: registry provider matched; routes already attached\n");
		self->tryAttachIrq();
	}
	pending &= SWEET_BATT_IRQ_ALL;
	if (!pending || !self->fRunning || !self->fIrqAttached) {
		return;
	}
	uint64_t notified = __atomic_load_n(&self->fIrqNotifyTime, __ATOMIC_ACQUIRE);
	if ((pending & SWEET_BATT_IRQ_FAULT) && self->fRuntimeMode) {
		self->fController.fault = true;
		self->fController.faultReasons = SB_R_HARDWARE;
		SWEET_LOG_ERROR("SWEET BATT CONTROL: CHGR fault IRQ latched; recovery requires explicit restart\n");
	}
	if (pending & SWEET_BATT_IRQ_WDOG) { self->fWatchdogPending = true; }
	/* LG fg_delta_batt_temp_irq_handler: re-read temperature, then notify.
	 * poll() below takes a fresh FG snapshot and re-runs the battemp vote. */
	if (pending & SWEET_BATT_IRQ_TEMP) { ++self->fTempIrqs; }
	self->fRuntimeInputEvents = pending & (SWEET_BATT_IRQ_USB | SWEET_BATT_IRQ_SOURCE | SWEET_BATT_IRQ_TYPEC | SWEET_BATT_IRQ_INPUT_FAULT);
	self->fSettleBudget = 1;
	self->poll();
	++self->fIrqSamples;
	uint64_t now, elapsed;
	clock_get_uptime(&now);
	absolutetime_to_nanoseconds(now >= notified ? now - notified : 0, &elapsed);
	SWEET_LOG_INFO("SWEET BATT IRQ: refresh events=%#x source=irq notifications=%u samples=%u ies-count=%d route-to-publish-us=%llu healthy=%u\n",
	    pending, __atomic_load_n(&self->fIrqNotifications, __ATOMIC_ACQUIRE),
	    self->fIrqSamples, count, elapsed / 1000ULL, self->fIrqFailed ? 0 : 1);
}

void
SweetBattery::tryAttachIrq(void)
{
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	if (!fRunning || fIrqAttached || fIrqFailed) {
		return;
	}
	unsigned events = SWEET_BATT_IRQ_USB;
	if (fIrqMode >= 3) {
		events |= SWEET_BATT_IRQ_CHG;
	}
	if (fIrqMode >= 4) {
		events |= SWEET_BATT_IRQ_MSOC | SWEET_BATT_IRQ_READY;
	}
	if (fRuntimeRequested) { events |= SWEET_BATT_IRQ_SOURCE | SWEET_BATT_IRQ_FAULT | SWEET_BATT_IRQ_TYPEC | SWEET_BATT_IRQ_INPUT_FAULT; }
	if ((fDependencyMode & 4) || fRuntimeRequested == 3) { events |= SWEET_BATT_IRQ_WDOG; }
	if (fTempIrq) { events |= SWEET_BATT_IRQ_TEMP; }
	fIrqAttached = true;
	putBool("SweetBatteryIRQWaitingForMux", false);
	putBool("SweetBatteryIRQHealthy", true);
	putNumber("SweetBatteryIRQEvents", events);
	SWEET_LOG_INFO("SWEET BATT IRQ: armed mode=%u events=%#x; 2s polling disabled\n",
	    fIrqMode, events);
	/* Resample once after arm to close the initial-sample/enable race. Any
	 * simultaneous IRQ is already queued and runs after this action. */
	fSettleBudget = 1;
	poll();
	SWEET_LOG_INFO("SWEET BATT IRQ: refresh source=arm-sync healthy=%u\n", fIrqFailed ? 0 : 1);
#endif
}

IOReturn
SweetBattery::startIrqAction(OSObject *owner, void *, void *, void *, void *)
{
	SweetBattery *self = OSDynamicCast(SweetBattery, owner);
	if (!self) {
		return kIOReturnBadArgument;
	}
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	self->putBool("SweetBatteryIRQWaitingForMux", true);
	self->putBool("SweetBatteryIRQHealthy", false);
	self->updateStatus();
	unsigned events = SWEET_BATT_IRQ_USB;
	if (self->fIrqMode >= 3) {
		events |= SWEET_BATT_IRQ_CHG;
	}
	if (self->fIrqMode >= 4) {
		events |= SWEET_BATT_IRQ_MSOC | SWEET_BATT_IRQ_READY;
	}
	if (self->fRuntimeRequested) { events |= SWEET_BATT_IRQ_SOURCE | SWEET_BATT_IRQ_FAULT | SWEET_BATT_IRQ_TYPEC | SWEET_BATT_IRQ_INPUT_FAULT; }
	if ((self->fDependencyMode & 4) || self->fRuntimeRequested == 3) { events |= SWEET_BATT_IRQ_WDOG; }
	if (self->fTempIrq) { events |= SWEET_BATT_IRQ_TEMP; }
	SWEET_LOG_INFO("SWEET BATT IRQ: waiting for mux registry match; no timer/status polling\n");
	self->fMuxClient = SweetSpmiMuxClient::withOwner(self, self->fLoop,
	    SWEET_MUX_BATTERY, events, irqNotify, self);
	if (!self->fMuxClient) {
		self->failIrq("mux registry subscriptions unavailable");
	}
#endif
	return kIOReturnSuccess;
}

void
SweetBattery::timerFired(OSObject *owner, IOTimerEventSource *sender)
{
	SweetBattery *self = OSDynamicCast(SweetBattery, owner);
	if (!self || !self->fRunning) {
		return;
	}
	if (self->fIrqMode >= 2) {
		if (self->fIrqAttached && self->fSettlePending && self->fSettleBudget) {
			self->fSettlePending = false;
			--self->fSettleBudget;
			self->poll();
			SWEET_LOG_INFO("SWEET BATT IRQ: refresh source=bounded-confirm healthy=%u\n",
			    self->fIrqFailed ? 0 : 1);
		}
		return;
	}
	self->poll();
	sender->setTimeoutMS(SWEET_BATT_POLL_MS);
}


void
SweetBattery::removePowerValue(const char *key)
{
	if (properties && properties->getObject(key)) {
		properties->removeObject(key);
		removeProperty(key);
		settingsChangedSinceUpdate = true;
	}
}

static uint64_t
sweet_batt_now_ns(void)
{
	uint64_t ticks, ns;
	clock_get_uptime(&ticks);
	absolutetime_to_nanoseconds(ticks, &ns);
	return ns;
}

void
SweetBattery::sampleRuntime(bool refreshInput)
{
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	static_assert(SB_INPUT_PLUGIN == SWEET_BATT_IRQ_USB && SB_INPUT_SOURCE == SWEET_BATT_IRQ_SOURCE &&
	    SB_INPUT_TYPEC == SWEET_BATT_IRQ_TYPEC && SB_INPUT_PROTECTION == SWEET_BATT_IRQ_INPUT_FAULT,
	    "input causes must match mux events");
	/* CHGR/MSOC/READY and bounded SOC confirmation must not renew APSD.
	 * All state and PMIC reads remain on this consumer's workloop. */
	const char *sampleSource = refreshInput ? "irq" : "protection-timer";
	unsigned inputEvents = fRuntimeInputEvents;
	fRuntimeInputEvents = 0;
	bool refreshCapacity = refreshInput || !fMonitorSamples;
	/* Identity/profile refresh is protection telemetry, never input discovery. */
	if ((fDependencyMode & 1) && fSnapshot.profileStartedNs &&
	    sweet_batt_now_ns() >= fSnapshot.profileStartedNs &&
	    sweet_batt_now_ns() - fSnapshot.profileStartedNs >= 30000000000ULL) refreshCapacity = true;
	refreshInput = refreshInput && (!fMonitorSamples || inputEvents);
	SweetBattSnapshot next = fSnapshot;
	next.startedNs = sweet_batt_now_ns();
	sweet_pmic_power_read_snapshot(next, refreshInput, refreshCapacity, fLastPercent);
	next.completedNs = sweet_batt_now_ns();
	if (refreshInput) { sb_note_input(next, fSnapshot, inputEvents); }
	/* A queued event blocks permission, not the provenance of a coherent
	 * observation. Erasing history here strands a valid CC source when the
	 * next action is only a duplicate APSD source notification. sb_note_input
	 * already withdraws changed/invalid status; the next input IRQ must still
	 * acquire a new tuple and ADC epoch before use. Both queues are fenced by
	 * refreshInputPermit/runtimeUpdate, including events arriving after here. */
	bool inputPending = inputChangePending();
	bool batteryChanged = next.presentValid != fSnapshot.presentValid || next.present != fSnapshot.present;
 if (refreshInput) { sb_input_observe(fInputSession,next,next.completedNs); }
 fSnapshot = next;
 if (fAdcClient) {
  IOLockLock(fControlLock); bool stopping = fPowerStopping; IOLockUnlock(fControlLock);
  if (stopping) { fAdcClient->invalidate(); }
  fAdcClient->update(sb_valid(next, SB_USB) && (next.raw[SB_USB] & 16),
      next.inputEpoch, batteryChanged, next.completedNs);
  publishAdc();
 }
 ++fMonitorSamples;
	/* Keep each console record below 255 bytes, including severity/newline.
	 * A shared sample id joins the independently complete records. */
	SWEET_LOG_INFO("SWEET BATT SAMPLE: v2 id=%u source=%s valid=%#llx mv=%u v-ok=%u ma=%d i-ok=%u\n",
	    fMonitorSamples, sampleSource,
	    (unsigned long long)next.valid, next.mv, next.voltageValid, next.ma, next.currentValid);
	SWEET_LOG_INFO("SWEET BATT FG: v3 id=%u temp-centi=%d temp-ok=%u temp-error=%d temp-raw=%#x\n",
	    fMonitorSamples, next.tempCenti, next.tempValid, next.tempError, next.rawTemp);
	SWEET_LOG_INFO("SWEET BATT CAP: v3 id=%u nominal=%u n-ok=%u n-error=%d learned=%u l-ok=%u l-error=%d profile=%#x p-ok=%u p-error=%d\n",
	    fMonitorSamples, next.nominalMah, next.nominalValid, next.nominalError,
	    next.learnedMah, next.learnedValid, next.learnedError,
	    next.integrity, next.profileValid, next.profileError);
	SWEET_LOG_INFO("SWEET BATT TIME: v3 id=%u started-ns=%llu completed-ns=%llu\n",
	    fMonitorSamples, (unsigned long long)next.startedNs, (unsigned long long)next.completedNs);
	if (refreshCapacity) {
		char prefix[49];
		static const char hex[] = "0123456789abcdef";
		for (unsigned i = 0; i < 24; ++i) {
			prefix[2 * i] = hex[next.profilePrefix[i] >> 4];
			prefix[2 * i + 1] = hex[next.profilePrefix[i] & 15];
		}
		prefix[48] = 0;
		SWEET_LOG_INFO("SWEET BATT PROFILE: v1 id=%u valid=%u error=%d match=%u integrity=%#x nominal=%u prefix=%s; identity also requires ADC\n",
		    fMonitorSamples, next.profilePrefixValid, next.profilePrefixError,
		    sb_profile_match(next), next.integrity, next.nominalMah, prefix);
		SWEET_LOG_INFO("SWEET BATT PROFILE TIME: v1 id=%u started-ns=%llu completed-ns=%llu\n",
		    fMonitorSamples, (unsigned long long)next.profileStartedNs, (unsigned long long)next.profileCompletedNs);
	}
	if (refreshInput) {
		SweetBattCc cc = sb_cc_status(next);
		SweetBattInputCapability capability = sb_input_capability(next, next.completedNs);
		SWEET_LOG_INFO("SWEET BATT INPUT QUEUE: v1 id=%u pending=%u; history retained, permission fenced\n",
		    fMonitorSamples, inputPending);
		SWEET_LOG_INFO("SWEET BATT CC: v1 id=%u events=%#x coherent=%u reasons=%#x rp=%u advertised-ma=%u orientation=%u qualified=%u\n",
		    fMonitorSamples, next.inputEvents, next.ccCoherent, cc.reasons, cc.rp,
		    cc.advertisedMa, cc.orientation, sb_cc_event_fresh(next, next.completedNs));
		SWEET_LOG_INFO("SWEET BATT CC ROLE: v1 id=%u role=%u observed=%#x; LG-SMB5 sink classification\n",
		    fMonitorSamples, cc.role, cc.observed);
		SWEET_LOG_INFO("SWEET BATT CC TIME: v1 id=%u session=%llu started-ns=%llu completed-ns=%llu\n",
		    fMonitorSamples, (unsigned long long)next.ccSession,
		    (unsigned long long)next.ccStartedNs, (unsigned long long)next.ccCompletedNs);
		SWEET_LOG_INFO("SWEET BATT INPUT CAP: v1 id=%u kind=%u max-ma=%u; observed-only not-PD\n",
		    fMonitorSamples, capability.kind, capability.maxMa);
		SWEET_LOG_INFO("SWEET BATT APSD EVENT: v1 id=%u events=%#x session=%llu source=%u coherent=%u qualified=%u options=%#x/%d enabled=%u\n",
		    fMonitorSamples, next.inputEvents, (unsigned long long)next.usbSession,
		    next.apsdFromSource, next.apsdCoherent, sb_apsd_event_fresh(next, next.completedNs),
		    next.raw[SB_APSD_OPTIONS], next.errors[SB_APSD_OPTIONS],
		    sb_valid(next, SB_APSD_OPTIONS) && (next.raw[SB_APSD_OPTIONS] & 8) ? 1u : 0u);
		SWEET_LOG_INFO("SWEET BATT APSD TIME: v1 id=%u session=%llu started-ns=%llu completed-ns=%llu\n",
		    fMonitorSamples, (unsigned long long)next.apsdSession,
		    (unsigned long long)next.apsdStartedNs, (unsigned long long)next.apsdCompletedNs);
		SWEET_LOG_INFO("SWEET BATT INPUT: v4 id=%u epoch=%llu type=%u sink-ok=%u fresh=%u apsd=%#x/%d result=%#x/%d usb=%#x/%d\n",
		    fMonitorSamples, (unsigned long long)next.inputEpoch, sb_source_type(next),
		    sb_normal_sink(next), sb_input_fresh(next, next.completedNs),
		    next.raw[SB_APSD], next.errors[SB_APSD], next.raw[SB_RESULT], next.errors[SB_RESULT],
		    next.raw[SB_USB], next.errors[SB_USB]);
		SWEET_LOG_INFO("SWEET BATT INPUT TIME: v1 id=%u epoch=%llu started-ns=%llu completed-ns=%llu\n",
		    fMonitorSamples, (unsigned long long)next.inputEpoch,
		    (unsigned long long)next.inputStartedNs, (unsigned long long)next.inputCompletedNs);
		SWEET_LOG_INFO("SWEET BATT TYPEC: v4 id=%u sink=%#x/%d attached=%#x/%d misc=%#x/%d debug=%#x/%d legacy=%#x/%d\n",
		    fMonitorSamples, next.raw[SB_TYPEC], next.errors[SB_TYPEC],
		    next.raw[SB_ATTACHED], next.errors[SB_ATTACHED], next.raw[SB_MISC], next.errors[SB_MISC],
		    next.raw[SB_DEBUG_ACCESS], next.errors[SB_DEBUG_ACCESS], next.raw[SB_LEGACY], next.errors[SB_LEGACY]);
		SWEET_LOG_INFO("SWEET BATT ICL: v1 id=%u path=%#x/%d suspend=%#x/%d icl=%#x/%d settled=%#x/%d\n",
		    fMonitorSamples, next.raw[SB_PATH], next.errors[SB_PATH], next.raw[SB_SUSPEND], next.errors[SB_SUSPEND],
		    next.raw[SB_ICL], next.errors[SB_ICL], next.raw[SB_SETTLED], next.errors[SB_SETTLED]);
		SWEET_LOG_INFO("SWEET BATT ICL MODE: v1 id=%u override=%#x/%d load=%#x/%d mode=%#x/%d aicl=%#x/%d\n",
		    fMonitorSamples, next.raw[SB_OVERRIDE], next.errors[SB_OVERRIDE], next.raw[SB_LOAD], next.errors[SB_LOAD],
		    next.raw[SB_MODE], next.errors[SB_MODE], next.raw[SB_AICL], next.errors[SB_AICL]);
		SWEET_LOG_INFO("SWEET BATT LIMIT CONFIG: v1 id=%u fcc=%#x/%d fv=%#x/%d cfg2=%#x/%d jeita=%#x/%d\n",
		    fMonitorSamples, next.raw[SB_FCC], next.errors[SB_FCC], next.raw[SB_FV], next.errors[SB_FV],
		    next.raw[SB_CFG2], next.errors[SB_CFG2], next.raw[SB_JEITA], next.errors[SB_JEITA]);
		/* Raw configuration is evidence, not a charging permission. Errors
		 * accompany every byte so a retained value cannot masquerade as fresh. */
		SWEET_LOG_INFO("SWEET BATT CONFIG: v3 id=%u valid=%#llx enable=%#x enable-error=%d pause=%#x pause-error=%d wdog=%#x wdog-error=%d wdog-time=%#x wdog-time-error=%d\n",
		    fMonitorSamples, (unsigned long long)next.valid,
		    next.raw[SB_ENABLE], next.errors[SB_ENABLE], next.raw[SB_PAUSE], next.errors[SB_PAUSE],
		    next.raw[SB_WDOG], next.errors[SB_WDOG], next.raw[SB_WDOG_TIME], next.errors[SB_WDOG_TIME]);
		SWEET_LOG_INFO("SWEET BATT PARALLEL: v3 id=%u smb=%#x smb-error=%d smb-cfg=%#x smb-cfg-error=%d\n",
		    fMonitorSamples, next.raw[SB_SMB], next.errors[SB_SMB], next.raw[SB_SMB_CFG], next.errors[SB_SMB_CFG]);
	}
#endif
}

#if defined(ARM64_BOARD_CONFIG_MH2LM)
void
SweetBattery::adcChanged(void *context)
{
 SweetBattery *self = (SweetBattery *)context;
 /* Delivered only on the battery loop, never synchronously by ADC provider. */
 if (!self->fRunning || !self->fRuntimeMode || self->fController.stopped) { return; }
 IOLockLock(self->fControlLock);
 bool stopping = self->fPowerStopping;
 IOLockUnlock(self->fControlLock);
 if (stopping) { self->fAdcClient->invalidate(); }
 self->publishAdc();
 self->runRuntimePolicy();
 self->updateStatus();
}
void
SweetBattery::publishAdc(void)
{
 struct sweet_adc5_measurement id = {}, vbus = {};
 uint64_t now = sweet_batt_now_ns();
 bool idOk = fAdcClient && fAdcClient->copyFresh(SWEET_ADC5_BAT_ID, now, &id);
 bool vbusOk = fAdcClient && fAdcClient->copyFresh(SWEET_ADC5_USBIN, now, &vbus);
 putBool("SweetBatteryBATIDMeasured", idOk);
 putBool("SweetBatteryVBUSMeasured", vbusOk);
 if (idOk) {
  putNumber("SweetBatteryBATIDMicrovolts", id.microvolts);
  putNumber("SweetBatteryBATIDAgeMS", (UInt32)((now - id.started_ns) / 1000000ULL));
 } else {
  removePowerValue("SweetBatteryBATIDMicrovolts"); removePowerValue("SweetBatteryBATIDAgeMS");
 }
 if (vbusOk) {
  putNumber("SweetBatteryVBUSMicrovolts", vbus.microvolts);
  putNumber("SweetBatteryVBUSAgeMS", (UInt32)((now - vbus.started_ns) / 1000000ULL));
 } else {
  removePowerValue("SweetBatteryVBUSMicrovolts"); removePowerValue("SweetBatteryVBUSAgeMS");
 }
 /* Measurement publication is separate from source/voltage permission.
  * Never infer battery bounds or physical parallel isolation from ADC. */
}
#endif

/* The diagnostic mask requests work, never asserts evidence. Inhibit/watchdog
 * experiments run with main charging disabled and cannot call runtime enable. */
int
SweetBattery::dependencyCommand(enum sweet_charger_operation operation, uint64_t deadline, unsigned *out)
{
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 if (!fControlLock || !out) return SWEET_SPMI_NOT_READY;
 bool disable=operation==SWEET_CHARGER_ENABLE;
 if (!disable && operation!=SWEET_CHARGER_PARALLEL_INHIBIT &&
     operation!=SWEET_CHARGER_WATCHDOG_PREPARE && operation!=SWEET_CHARGER_WATCHDOG_PET)
  return SWEET_SPMI_INVALID;
 if (!disable && !(fDependencyMode & (operation==SWEET_CHARGER_PARALLEL_INHIBIT?2:4)))
  return SWEET_SPMI_INVALID;
 uint64_t entered=sweet_batt_now_ns();
 IOLockLock(fControlLock);
 uint64_t locked=sweet_batt_now_ns();
 int rc=SWEET_SPMI_NOT_READY;
 if (disable || (!fPowerStopping && fRuntimeMode==1 && fDependencyStarted && !fDependencyFailed && !fDependencyFinished)) {
  fControlClaimed=true;
  rc=sweet_charger_execute(operation,disable?0:1,deadline,out);
 }
 IOLockUnlock(fControlLock);
 if (operation==SWEET_CHARGER_WATCHDOG_PREPARE || operation==SWEET_CHARGER_WATCHDOG_PET) {
  SWEET_LOG_INFO("SWEET BATT WD COMMAND: v1 op=%u enter=%llu locked=%llu done=%llu error=%d actual=%u\n",
   (unsigned)operation,(unsigned long long)entered,(unsigned long long)locked,
   (unsigned long long)sweet_batt_now_ns(),rc,rc?0:*out);
 }
 return rc;
#else
 return -1;
#endif
}

void
SweetBattery::captureWatchdogFailure(void)
{
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 SweetMuxIrqTrace t={};bool trace=fMuxClient && fMuxClient->copyInterruptTrace(SWEET_BATT_IRQ_WDOG,&t);
 SWEET_LOG_ERROR("SWEET BATT WD FAULT: v1 now=%llu last=%llu epoch=%llu pets=%u input=%u pending=%u\n",
  (unsigned long long)sweet_batt_now_ns(),(unsigned long long)fWatchdogLastNs,
  (unsigned long long)fWatchdogInputEpoch,fWatchdogPets,fWatchdogInput,fWatchdogPending);
 SWEET_LOG_ERROR("SWEET BATT WD MUX: v1 valid=%u hits=%u irq=%u work=%u apid=%u status=%#x\n",
  trace,t.hits,t.irq,t.work,t.apid,t.status);
 SWEET_LOG_ERROR("SWEET BATT WD PATH: v1 gic=%llu scan=%llu ack=%llu notify=%llu\n",
  (unsigned long long)t.gicNs,(unsigned long long)t.scanNs,(unsigned long long)t.ackNs,(unsigned long long)t.notifyNs);
 struct sweet_watchdog_fault f={};int rc=sweet_pmic_power_capture_watchdog_fault(&f);
 SWEET_LOG_ERROR("SWEET BATT WD SNAPSHOT: v2 error=%d enter=%llu first-lock=%llu done=%llu\n",rc,
  (unsigned long long)f.entered_ns,(unsigned long long)f.locked_ns,(unsigned long long)f.completed_ns);
 if (!rc) for (unsigned i=0;i<SWEET_WDOG_FAULT_FIELDS;++i)
  SWEET_LOG_ERROR("SWEET BATT WD FIELD: v1 field=%u raw=%#x error=%d\n",i,f.raw[i],f.errors[i]);
#endif
}

void
SweetBattery::dependencyFailure(const char *reason, int error)
{
 if (fDependencyFailed) return;
 fDependencyFailed=true;fWatchdogArmed=false;
 putBool("SweetBatterySecondaryOutputInhibited",false);
 putBool("SweetBatteryWatchdogDiagnosticActive",false);
 SWEET_LOG_ERROR("SWEET BATT DEPENDENCIES: FAILED %s error=%d; no restart or enable\n",reason,error);
 unsigned disabled=0;
 int rc=dependencyCommand(SWEET_CHARGER_ENABLE,0,&disabled);
 if (rc) SWEET_LOG_ERROR("SWEET BATT DEPENDENCIES: fault disable unconfirmed error=%d\n",rc);
 if ((fDependencyMode & 4) && fDependencyStarted) captureWatchdogFailure();
 fWatchdogPending=false;
}

void
SweetBattery::refreshDependencies(uint64_t now)
{
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 if (!fDependencyMode) return;
 if (fDependencyMode & 1) {
  struct sweet_adc5_measurement id={};
  bool measured=!fController.stopped && fRunning && !fIrqFailed && fAdcClient &&
   fAdcClient->copyFresh(SWEET_ADC5_BAT_ID,now,&id);
  SweetBattIdentity next=sb_battery_identity(fSnapshot,measured,id.microvolts,id.sequence,
   id.started_ns,id.completed_ns,now);
  if (next.valid!=fIdentity.valid || next.adcToken!=fIdentity.adcToken || next.profile!=fIdentity.profile) {
   SWEET_LOG_INFO("SWEET BATT IDENTITY: v1 valid=%u profile=%u ohms=%u token=%u fcc-ma=%u fv-mv=%u\n",
    next.valid,next.profile,next.ohms,next.adcToken,next.maxFccMa,next.maxFvMv);
  }
  fIdentity=next;fLimits.batteryVerified=next.valid;
  fLimits.maxFccMa=next.maxFccMa;fLimits.maxFvMv=next.maxFvMv;
  fLimits.verifiedNs=next.valid?fSnapshot.startedNs:0;
  fLimits.batteryUntilNs=next.validUntilNs;
  putBool("SweetBatteryIdentityVerified",next.valid);
  putNumber("SweetBatteryIdentityProfile",next.profile);
  putNumber("SweetBatteryApprovedFCCMA",next.maxFccMa);
  putNumber("SweetBatteryApprovedFVMV",next.maxFvMv);
 }
 /* A primary SMB_EN override cannot verify an I2C chip configured for command
  * control. Keep final isolation authorization false until that provider exists. */
 fLimits.parallelVerified=false;
 putBool("SweetBatteryParallelVerified",false);
 if (fController.stopped) {
  fWatchdogArmed=false;fWatchdogPending=false;
  putBool("SweetBatteryWatchdogDiagnosticActive",false);
  return;
 }
 if (!(fDependencyMode & 6) || fDependencyFailed || fDependencyFinished) return;
 if (!fIrqAttached || fIrqFailed || !fMuxClient || fMuxClient->hasPendingEvents(0)) {
  if (fDependencyStarted) dependencyFailure("mux unavailable",SWEET_SPMI_NOT_READY);
  return;
 }
 if (!fDependencyStarted) {
  fDependencyStarted=true;
  fDependencyDeadlineNs=now+180000000000ULL; /* bounded one-shot observation */
  unsigned actual=0;int rc=dependencyCommand(SWEET_CHARGER_ENABLE,0,&actual);
  if (rc || actual) { dependencyFailure("initial disable",rc?rc:SWEET_SPMI_VERIFY);return; }
  if (fDependencyMode & 2) {
   rc=dependencyCommand(SWEET_CHARGER_PARALLEL_INHIBIT,fDependencyDeadlineNs,&actual);
   if (rc || actual!=1) {dependencyFailure("parallel inhibit",rc?rc:SWEET_SPMI_VERIFY);return;}
   SWEET_LOG_INFO("SWEET BATT INHIBIT: v1 command accepted; awaiting telemetry; external isolation unverified\n");
  }
  if (fDependencyMode & 4) {
   rc=dependencyCommand(SWEET_CHARGER_WATCHDOG_PREPARE,fDependencyDeadlineNs,&actual);
   if (rc || actual!=1) {dependencyFailure("watchdog prepare",rc?rc:SWEET_SPMI_VERIFY);return;}
   rc=dependencyCommand(SWEET_CHARGER_WATCHDOG_PET,fDependencyDeadlineNs,&actual);
   if (rc || actual!=1) {dependencyFailure("watchdog initial pet",rc?rc:SWEET_SPMI_VERIFY);return;}
   fWatchdogArmed=true;fWatchdogLastNs=now;fWatchdogPets=1;fWatchdogPending=false;
   fWatchdogInputEpoch=fSnapshot.inputEpoch;fWatchdogInput=false;
   putBool("SweetBatteryWatchdogDiagnosticActive",true);
   SWEET_LOG_INFO("SWEET BATT WATCHDOG: v1 prepared bark16s bite8s snarl8s bite-disable1; pulse completed, replug then await IRQ\n");
  }
  fDependencyReadAfterNs=sweet_batt_now_ns();
  return; /* delayed snapshot must independently confirm the writes */
 }
 if (now>=fDependencyDeadlineNs) {
  fWatchdogArmed=false;fWatchdogPending=false;
  /* Successful finite completion is terminal too; no implicit rearm. */
  unsigned actual=0;int rc=dependencyCommand(SWEET_CHARGER_ENABLE,0,&actual);
  fDependencyFinished=true;
  putBool("SweetBatteryWatchdogDiagnosticActive",false);
  if (rc) {dependencyFailure("window disable",rc);return;}
  SWEET_LOG_INFO("SWEET BATT DEPENDENCIES: v1 window complete pets=%u disable-error=%d; restart required\n",fWatchdogPets,rc);
  return;
 }
 /* ADC notifications can run before the first post-write telemetry sample.
  * They must neither verify the old tuple nor misdiagnose it as lost config. */
 if (fSnapshot.startedNs<fDependencyReadAfterNs) {
  if (now-fDependencyReadAfterNs>=3000000000ULL)
   dependencyFailure("post-write telemetry missing",SWEET_CHARGER_EXPIRED);
  return;
 }
 if (!fSnapshot.startedNs || fSnapshot.completedNs<fSnapshot.startedNs ||
     now<fSnapshot.completedNs || now-fSnapshot.startedNs>=3000000000ULL) {
  dependencyFailure("protection telemetry stale",SWEET_CHARGER_EXPIRED);return;
 }
 if (!sb_valid(fSnapshot,SB_ENABLE) || (fSnapshot.raw[SB_ENABLE]&1)) {
  dependencyFailure("main disable lost",SWEET_SPMI_VERIFY);return;
 }
 if (fDependencyMode & 2) {
  bool inhibited=sb_valid(fSnapshot,SB_SMB) && sb_valid(fSnapshot,SB_SMB_CFG) &&
   (fSnapshot.raw[SB_SMB]&0x18)==8;
  putBool("SweetBatterySecondaryOutputInhibited",inhibited);
  if (!inhibited) {dependencyFailure("parallel inhibit lost",SWEET_SPMI_VERIFY);return;}
 }
 if (fWatchdogArmed) {
  if (!sb_valid(fSnapshot,SB_WDOG) || !sb_valid(fSnapshot,SB_WDOG_TIME) ||
      (fSnapshot.raw[SB_WDOG]&0xc2)!=0x42 || fSnapshot.raw[SB_WDOG_TIME]!=0xf3) {
   dependencyFailure("watchdog config lost",SWEET_SPMI_VERIFY);return;
  }
  bool input=sb_valid(fSnapshot,SB_USB) && (fSnapshot.raw[SB_USB]&16);
  /* Plugin-enable does not prove a timer started for an inherited cable.
   * Expect bark only after a post-config plugin acquisition or actual bark.
   * Source/protection/ADC callbacks must not keep renewing this deadline. */
  bool plugin=input && (fSnapshot.inputEvents&SB_INPUT_PLUGIN) &&
   fSnapshot.inputEpoch!=fWatchdogInputEpoch;
  fWatchdogInputEpoch=fSnapshot.inputEpoch;
  if (!input) fWatchdogInput=false;
  if (plugin && !fWatchdogInput) {
  fWatchdogInput=true;fWatchdogLastNs=now;
  SWEET_LOG_INFO("SWEET BATT WD START: v1 epoch=%llu input-start=%llu input-end=%llu observed=%llu pets=%u; hardware-start unproven\n",
   (unsigned long long)fSnapshot.inputEpoch,(unsigned long long)fSnapshot.inputStartedNs,
   (unsigned long long)fSnapshot.inputCompletedNs,(unsigned long long)now,fWatchdogPets);
 }
  if (input && fWatchdogInput && now-fWatchdogLastNs>=23000000000ULL) {
   dependencyFailure("watchdog bark missing/late",SWEET_CHARGER_EXPIRED);return;
  }
  if (fWatchdogPending) {
   fWatchdogPending=false;
   unsigned actual=0;int rc=dependencyCommand(SWEET_CHARGER_WATCHDOG_PET,fDependencyDeadlineNs,&actual);
   if (rc || actual!=1) {dependencyFailure("watchdog IRQ pet",rc?rc:SWEET_SPMI_VERIFY);return;}
   fWatchdogLastNs=now;fWatchdogInput=input;++fWatchdogPets;
   SWEET_LOG_INFO("SWEET BATT WATCHDOG: v1 bark IRQ pet=%u pulse-completed=1 main-enable=0 rt=%#x rt-error=%d\n",
    fWatchdogPets,fSnapshot.raw[SB_WDOG_RT],fSnapshot.errors[SB_WDOG_RT]);
   putNumber("SweetBatteryWatchdogPets",fWatchdogPets);
  }
 }
 if (!fDependencyConfirmed) {
  fDependencyConfirmed=true;
  SWEET_LOG_INFO("SWEET BATT DEP CHECK: v1 mode=%u main-enable=%#x smb=%#x select=%#x wdog=%#x time=%#x; external isolation unverified\n",
   fDependencyMode,fSnapshot.raw[SB_ENABLE],fSnapshot.raw[SB_SMB],fSnapshot.raw[SB_SMB_CFG],
   fSnapshot.raw[SB_WDOG],fSnapshot.raw[SB_WDOG_TIME]);
 }
#endif
}

bool
SweetBattery::inputChangePending(void)
{
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 const unsigned mask=SWEET_BATT_IRQ_USB | SWEET_BATT_IRQ_SOURCE |
  SWEET_BATT_IRQ_TYPEC | SWEET_BATT_IRQ_INPUT_FAULT;
 return !fIrqAttached || fIrqFailed || !fMuxClient ||
  (__atomic_load_n(&fIrqPending,__ATOMIC_ACQUIRE) & (mask | SWEET_MUX_FAILED)) ||
  fMuxClient->hasPendingEvents(mask);
#else
 return true;
#endif
}

void
SweetBattery::refreshInputPermit(uint64_t now)
{
 SweetBattInputPermit next={};
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 struct sweet_adc5_measurement vbus={};
 if (!fController.stopped && !inputChangePending() && fAdcClient &&
     fAdcClient->copyFreshInput(fSnapshot.inputEpoch,now,&vbus)) {
  SweetBattInputVoltage voltage={true,vbus.microvolts,vbus.sequence,
   fSnapshot.inputEpoch,vbus.started_ns,vbus.completed_ns};
  next=sb_input_permit(fInputSession,fSnapshot,voltage,now);
 }
#endif
 const SweetBattInputPermit &old=fLimits.input;
 if (next.valid!=old.valid || next.epoch!=old.epoch ||
     next.voltageSequence!=old.voltageSequence || next.maxMa!=old.maxMa) {
  SWEET_LOG_INFO("SWEET BATT INPUT PERMIT: v1 valid=%u kind=%u max-ma=%u vbus-mv=%u session=%llu epoch=%llu token=%u\n",
   next.valid,next.kind,next.maxMa,next.vbusMv,(unsigned long long)next.session,
   (unsigned long long)next.epoch,next.voltageSequence);
  SWEET_LOG_INFO("SWEET BATT INPUT LEASE: v1 source-ns=%llu voltage-ns=%llu until-ns=%llu\n",
   (unsigned long long)fInputSession.sourceStartedNs,
   (unsigned long long)next.voltageStartedNs,(unsigned long long)next.validUntilNs);
 }
 fLimits.input=next;
 putBool("SweetBatteryInputVerified",next.valid);
 putNumber("SweetBatteryInputKind",next.kind);
 putNumber("SweetBatteryInputMaxMA",next.maxMa);
}

int
SweetBattery::runtimeUpdate(void *context, enum sweet_charger_operation operation, unsigned value, uint64_t deadline, unsigned *out)
{
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	SweetBattery *self = (SweetBattery *)context;
	if (!self || !self->fControlLock) { return SWEET_SPMI_NOT_READY; }
 if (operation>=SWEET_CHARGER_PARALLEL_INHIBIT) return SWEET_SPMI_INVALID;
	/* Serialize the stop notification with command submission. A flag check
	 * outside this lock would race a final enable after a shutdown disable. */
	IOLockLock(self->fControlLock);
	bool disable = operation == SWEET_CHARGER_ENABLE && value == 0;
	int rc;
 struct sweet_adc5_measurement voltage={};
 uint64_t now=sweet_batt_now_ns();
 bool inputOk=disable || (!self->inputChangePending() && self->fAdcClient &&
  sb_input_permit_valid(self->fLimits.input,self->fSnapshot,now) &&
  self->fAdcClient->copyFreshInput(self->fSnapshot.inputEpoch,now,&voltage) &&
  voltage.sequence==self->fLimits.input.voltageSequence);
 if (!disable && (self->fPowerStopping || !inputOk)) {
  rc = SWEET_SPMI_NOT_READY;
 }
	else {
		self->fControlClaimed = true;
		rc = sweet_charger_execute(operation, value, deadline, out);
	}
	IOLockUnlock(self->fControlLock);
	if (!rc) {
		SWEET_LOG_INFO("SWEET BATT CONTROL: operation=%u requested=%u verified=%u\n",
		    operation, value, *out);
	}
	return rc;
#else
	return -1;
#endif
}

void
SweetBattery::runRuntimePolicy(void)
{
 /* Pending input notifications suspend the permit without rewriting cached
  * event timestamps. The next IRQ acquisition revalidates classification and
  * invalidates USBIN conversions from the previous input epoch. */
	if (fControlLock) {
		IOLockLock(fControlLock);
		if (fPowerStopping) { fController.stopped = true; }
		IOLockUnlock(fControlLock);
	}
	uint64_t now = sweet_batt_now_ns();
	if (fRuntimeMode == 3) { runBasicCharge(now); return; }
	refreshDependencies(now);
	refreshInputPermit(now);
	SweetBattDecision decision = sb_decide(fController, fSnapshot, fLimits, now);
	if (fLastReasons != decision.reasons || fController.state != decision.state) {
		SWEET_LOG_INFO("SWEET BATT POLICY: v1 mode=%u state=%u reasons=%#x allow=%u fcc=%u fv=%u icl=%u owned=%u\n",
		    fRuntimeMode, decision.state, decision.reasons, decision.allow,
		    decision.fccMa, decision.fvMv, decision.iclMa, fController.owned);
	}
	fLastReasons = decision.reasons;
	if (fRuntimeMode >= 2) {
		if (!sb_apply(fController, decision, now, fRuntimeMode != 3, runtimeUpdate, this, runtimeClock)) {
			SWEET_LOG_ERROR("SWEET BATT CONTROL: failure error=%d; stop state unconfirmed=%u\n",
			    fController.lastError, !fController.stopConfirmed);
		}
	}
	if (fController.fault) { fController.state = SB_FAULT; }
	else if (fController.stopped) { fController.state = SB_STOPPED; }
	else { fController.state = decision.state; }
}

int
SweetBattery::basicCommand(void *context, enum sweet_charger_operation operation, unsigned value, unsigned *out)
{
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	SweetBattery *self = (SweetBattery *)context;
	bool disable = operation == SWEET_CHARGER_ENABLE && value == 0;
	int rc;
	/* Serialized with the power notifier: nothing but disable after restart/off. */
	IOLockLock(self->fControlLock);
	if (!disable && self->fPowerStopping) { rc = SWEET_SPMI_NOT_READY; }
	else {
		self->fControlClaimed = true;
		rc = sweet_charger_execute(operation, value, sweet_batt_now_ns() + 1000000000ULL, out);
	}
	IOLockUnlock(self->fControlLock);
	if (rc || operation != SWEET_CHARGER_CHARGING_PET) {
		SWEET_LOG_INFO("SWEET BATT BASIC CMD: v2 op=%u value=%u error=%d actual=%u\n",
		    operation, value, rc, rc ? 0 : *out);
	}
	return rc;
#else
	return -1;
#endif
}

/* Runs only from charger/input/watchdog IRQ refreshes and the LG battemp
 * cadence timer, always after a fresh snapshot on this workloop. */
void
SweetBattery::runBasicCharge(uint64_t now)
{
	bool stopping = true;
	if (fControlLock) {
		IOLockLock(fControlLock); stopping = fPowerStopping; IOLockUnlock(fControlLock);
	}
	bool bark = fWatchdogPending;
	fWatchdogPending = false;
	uint32_t before = fBasic.reasons;
	bool wasEnabled = fBasic.enabled;
	SweetChargeHealth health = fBasic.health;
	SweetChargeBasicStep step = scb_step(fBasic, fSnapshot, now, stopping || fController.stopped,
	    bark, basicCommand, this);
	if (step != SCB_IDLE || before != fBasic.reasons || wasEnabled != fBasic.enabled ||
	    health != fBasic.health || fLastReasons == ~0u) {
		SWEET_LOG_INFO("SWEET BATT BASIC: v2 step=%u reasons=%#x enabled=%u health=%u chilly=%u fcc=%u fv=%u failures=%u failed=%u enables=%u error=%d\n",
		    step, fBasic.reasons, fBasic.enabled, fBasic.health, fBasic.chilly, fBasic.fccMa,
		    fBasic.fvMv, fBasic.failures, fBasic.failed, fBasic.enables, fBasic.lastError);
		SWEET_LOG_INFO("SWEET BATT BASIC STATE: v2 temp-centi=%d mv=%u ma=%d usb=%#x stage=%u pets=%u pet-errors=%u next-ms=%u temp-irqs=%u\n",
		    fSnapshot.tempCenti, fSnapshot.mv, fSnapshot.ma, fSnapshot.raw[SB_USB],
		    fSnapshot.raw[SB_STAGE] & 7u, fBasic.pets, fBasic.petErrors, scb_period_ms(fBasic), fTempIrqs);
	}
	fLastReasons = fBasic.reasons;
	putBool("SweetBatteryBasicChargeEnabled", fBasic.enabled);
	putNumber("SweetBatteryBasicChargeReasons", fBasic.reasons);
	putNumber("SweetBatteryBasicChargeHealth", fBasic.health);
	putNumber("SweetBatteryBasicChargeFailures", fBasic.failures);
	putNumber("SweetBatteryBasicChargeWatchdogPets", fBasic.pets);
	putNumber("SweetBatteryTemperatureIRQs", fTempIrqs);
	/* LG polling_status_work cadence; each IRQ evaluation restarts it. */
	if (fMonitor && fRunning && !fController.stopped) { fMonitor->setTimeoutMS(scb_period_ms(fBasic)); }
}

IOReturn
SweetBattery::startRuntime(OSObject *owner, void *, void *, void *, void *)
{
	SweetBattery *self = OSDynamicCast(SweetBattery, owner);
	if (!self || !self->fRunning) { return kIOReturnNotReady; }
	self->fRuntimeInputEvents = 0; /* initial snapshot is not an APSD completion IRQ */
	self->fRuntimeMode = self->fRuntimeRequested;
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 if (self->fRuntimeMode == 3) {
  /* No ADC5 consumer: charging must not depend on shared-mux conversions. */
  SWEET_LOG_INFO("SWEET BATT BASIC: v2 runtime3 IRQ control fcc=%u fv=%u; LG battemp cadence %u/%u/%ums, bark-IRQ pet; no ADC/identity/permit gates\n",
      SCB_FCC_MA, SCB_FV_MV, SCB_PERIOD_GOOD_MS, SCB_PERIOD_WARNING_MS, SCB_PERIOD_EMERGENCY_MS);
  SWEET_LOG_INFO("SWEET BATT TEMP IRQ: v1 fg batt-temp-delta route=%u; battemp timer kept as fallback\n",
      self->fTempIrq ? 1u : 0u);
  self->sampleRuntime(true);
  self->runRuntimePolicy();
  self->publishRuntime();
  return kIOReturnSuccess;
 }
 self->fAdcClient = SweetAdc5Client::withOwner(self->fLoop, adcChanged, self);
 if (!self->fAdcClient) {
  SWEET_LOG_ERROR("SWEET BATT ADC: registry consumer allocation failed; runtime unavailable\n");
  return kIOReturnNoMemory;
 }
#endif
	SWEET_LOG_INFO("SWEET BATT RUNTIME: v1 mode=%u; identity optional, external isolation/watchdog authorization missing; control interlocked\n",
	    self->fRuntimeMode);
	SWEET_LOG_INFO("SWEET BATT DEPENDENCIES: v1 mode=%u; identity1 inhibit2 watchdog4; observation only, no enable\n", self->fDependencyMode);
	SWEET_LOG_INFO("SWEET BATT INPUT SESSION: v1 IRQ source + same-epoch ADC; UV/OV revoke; trial cap 500mA\n");
	SWEET_LOG_INFO("SWEET BATT HARDWARE: v1 semantic charger operations; sampling via PMIC backend\n");
	self->sampleRuntime(true);
	self->runRuntimePolicy();
	self->publishRuntime();
	self->fMonitor->setTimeoutMS(1000);
	return kIOReturnSuccess;
}

void
SweetBattery::monitorFired(OSObject *owner, IOTimerEventSource *)
{
	SweetBattery *self = OSDynamicCast(SweetBattery, owner);
	if (!self || !self->fRunning || !self->fRuntimeMode || self->fController.stopped) { return; }
	self->sampleRuntime(false);
	self->runRuntimePolicy();
	self->publishRuntime();
	/* Runtime3 re-arms from runBasicCharge at the LG battemp cadence. */
	if (self->fRuntimeMode != 3 && self->fRunning && !self->fController.stopped) { self->fMonitor->setTimeoutMS(1000); }
}

uint64_t
SweetBattery::runtimeClock(void *)
{
	return sweet_batt_now_ns();
}

void
SweetBattery::publishRuntime(void)
{
	uint64_t now = sweet_batt_now_ns();
	uint64_t freshNs = fRuntimeMode == 3 ? (SCB_PERIOD_GOOD_MS + 10000ULL) * 1000000ULL : 3000000000ULL;
	bool fresh = !fController.stopped && fSnapshot.completedNs && now >= fSnapshot.startedNs &&
	    now >= fSnapshot.completedNs && now - fSnapshot.startedNs <= freshNs;
	putBool("SweetBatteryTelemetryFresh", fresh);
	SweetBattCc cc = sb_cc_status(fSnapshot);
	SweetBattInputCapability input = sb_input_capability(fSnapshot, now);
	putNumber("SweetBatteryCCRejectReasons", cc.reasons);
	putNumber("SweetBatteryCCRole", cc.role);
	putNumber("SweetBatteryCCObservedFlags", cc.observed);
	putNumber("SweetBatteryCCRp", cc.rp);
	putNumber("SweetBatteryCCAdvertisedMA", cc.advertisedMa);
	bool inputUsable=!fController.stopped && !inputChangePending();
	putBool("SweetBatteryCCEvidenceFresh", inputUsable && sb_cc_event_fresh(fSnapshot, now));
	putNumber("SweetBatteryObservedInputKind", !inputUsable ? SB_INPUT_NONE : input.kind);
	putNumber("SweetBatteryObservedInputMaxMA", !inputUsable ? 0 : input.maxMa);
	if (!fresh) {
		fSnapshot.tempValid = fSnapshot.currentValid = fSnapshot.voltageValid = false;
	}
	putNumber("SweetBatterySampleAgeMS", now >= fSnapshot.startedNs ?
	    (UInt32)((now - fSnapshot.startedNs) / 1000000ULL) : 0xffffffffu);
	unsigned percent = fPercentValid ? fLastPercent : fSnapshot.percent;
	bool external = sb_valid(fSnapshot, SB_USB) && (fSnapshot.raw[SB_USB] & 16);
	bool capable = external && sb_valid(fSnapshot, SB_PATH) && (fSnapshot.raw[SB_PATH] & 1) &&
	    (fSnapshot.raw[SB_PATH] & 16) && !(fSnapshot.raw[SB_PATH] & 64);
	bool statusValid = sb_valid(fSnapshot, SB_STAGE) && sb_valid(fSnapshot, SB_ERROR) &&
	    sb_valid(fSnapshot, SB_THERMAL) && sb_valid(fSnapshot, SB_ENABLE) && sb_valid(fSnapshot, SB_PAUSE);
	unsigned state = fSnapshot.raw[SB_STAGE] & 7;
	bool charging = capable && statusValid && (fSnapshot.raw[SB_ENABLE] & 1) &&
	    !(fSnapshot.raw[SB_PAUSE] & 1) && !(fSnapshot.raw[SB_ERROR] & 2) &&
	    !(fSnapshot.raw[SB_THERMAL] & 15) && state >= 1 && state <= 4;
	putBool("SweetBatteryExternalValid", sb_valid(fSnapshot, SB_USB));
	putBool("SweetBatteryChargeStateValid", statusValid);
	putBool("SweetBatteryTemperatureCompensated", false);
	publish(percent, fSnapshot.voltageValid ? fSnapshot.mv : fLastMv,
	    fSnapshot.ma, external, capable, charging,
	    external && fSnapshot.socValid && fSnapshot.percent >= 99 && statusValid && state == 5,
	    fSnapshot.socValid, fSnapshot.currentValid);
}

IOReturn
SweetBattery::runtimePowerNotice(void *target, void *, UInt32 message,
    IOService *, void *argument, vm_size_t)
{
	SweetBattery *self = (SweetBattery *)target;
	if (message != kIOMessageSystemWillSleep && message != kIOMessageSystemWillRestart &&
	    message != kIOMessageSystemWillPowerOff) { return kIOReturnUnsupported; }
	if (message == kIOMessageSystemWillSleep && self->fRuntimeMode == 3) {
		/* LG keeps charging in suspend: PMIC limits stay active, and if the
		 * bark IRQ is not serviced the watchdog bite stops charging. */
		SWEET_LOG_INFO("SWEET BATT BASIC POWER: v1 will-sleep acknowledged; charging kept\n");
		if (argument) {
			sleepWakeNote *note = (sleepWakeNote *)argument;
			note->returnValue = 0;
			acknowledgeSleepWakeNotification(note->powerRef);
		}
		return kIOReturnSuccess;
	}
	/* No wait for the consumer loop: it can be publishing to power clients.
	 * Only a bounded disable command is allowed here, serialized with submit.
	 * All controller state remains owned by the consumer loop. */
	IOLockLock(self->fControlLock);
	self->fPowerStopping = true;
	if (self->fControlClaimed) {
#if defined(ARM64_BOARD_CONFIG_MH2LM)
		unsigned verified = 0;
		int rc = sweet_charger_execute(SWEET_CHARGER_ENABLE, 0, 0, &verified);
		if (rc || verified != 0) {
			SWEET_LOG_ERROR("SWEET BATT CONTROL: power transition=%#x disable unconfirmed error=%d\n", message, rc);
		} else if (self->fRuntimeMode == 3) {
			SWEET_LOG_INFO("SWEET BATT BASIC POWER: v1 transition=%#x disabled readback=0\n", message);
		}
#endif
	}
	IOLockUnlock(self->fControlLock);
	if (message == kIOMessageSystemWillSleep && argument) {
		sleepWakeNote *note = (sleepWakeNote *)argument;
		note->returnValue = 0;
		acknowledgeSleepWakeNotification(note->powerRef);
	}
	return kIOReturnSuccess;
}

void
SweetBattery::removeRuntimeNotifier(void)
{
	/* Outside our workloop gate; remove synchronizes an in-flight callback. */
	if (fPowerNotice) { fPowerNotice->remove();fPowerNotice = NULL; }
}

void
SweetBattery::stopRuntime(void)
{
	if (fMonitor) { fMonitor->cancelTimeout();fMonitor->disable(); }
	if (!fRuntimeMode) { return; }
	fController.stopped = true;
	fInputSession = {}; fIdentity = {};
	fLimits.batteryVerified = false; fWatchdogArmed = false; fWatchdogPending = false;
 putBool("SweetBatteryIdentityVerified",false);
 putBool("SweetBatteryWatchdogDiagnosticActive",false);
	if (fRuntimeMode == 3) {
		unsigned disabled = 1;
		int rc = basicCommand(this, SWEET_CHARGER_ENABLE, 0, &disabled);
		fBasic.enabled = false;
		if (rc || disabled) SWEET_LOG_ERROR("SWEET BATT BASIC: stop disable unconfirmed error=%d\n", rc);
		return;
	}
	if (fDependencyStarted) {
		unsigned disabled = 0; int rc = dependencyCommand(SWEET_CHARGER_ENABLE, 0, &disabled);
		if (rc) SWEET_LOG_ERROR("SWEET BATT DEPENDENCIES: stop disable unconfirmed error=%d\n", rc);
	}
	refreshInputPermit(sweet_batt_now_ns());
	if (!sb_stop(fController, runtimeUpdate, this)) {
		SWEET_LOG_ERROR("SWEET BATT CONTROL: service stopping; hardware disable unconfirmed error=%d\n",
		    fController.lastError);
	}
}

#if defined(ARM64_BOARD_CONFIG_MH2LM)
/* One-shot, explicit read-only access preflight on the consumer's own loop.
 * Never call this from registry matching or interpret it as control approval.
 * Missing/invalid values retain their errors rather than becoming zero. */
IOReturn
SweetBattery::transportPreflightAction(OSObject *owner, void *, void *, void *, void *)
{
	SweetBattery *self = OSDynamicCast(SweetBattery, owner);
	if (!self || !self->fRunning || !self->fSid2) {
		return kIOReturnNotReady;
	}
	SWEET_LOG_INFO("SWEET CHARGER PREFLIGHT: restricted transport v1; read-only, no control acquired\n");
	const unsigned registers[] = {
		0x1042, 0x1043, 0x1061, 0x1070,
		0x1107, 0x1108, 0x110b,
		0x1307, 0x1308, 0x1340, 0x1342, 0x1365, 0x1366, 0x1370, 0x1380,
		0x1506, 0x1509, 0x150b,
		0x1648, 0x1651, 0x1653, 0x1690
	};
	for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i) {
		unsigned addr = registers[i];
		struct sweet_spmi_charger_route route;
		int result = sweet_spmi_charger_get_route(SWEET_BATT_SID, addr, &route);
		if (result != SWEET_SPMI_OK) {
			SWEET_LOG_WARN("SWEET CHARGER PREFLIGHT: addr=%#x route-error=%d value=unknown\n",
			    addr, result);
			continue;
		}
		uint8_t value = 0;
		uint64_t sampled, ns;
		result = sweet_spmi_charger_read(SWEET_BATT_SID, addr, &value);
		clock_get_uptime(&sampled);
		absolutetime_to_nanoseconds(sampled, &ns);
		if (result == SWEET_SPMI_OK) {
			SWEET_LOG_INFO("SWEET CHARGER PREFLIGHT: sid=2 addr=%#x apid=%u write-ee=%u irq-owner=%u raw=%#x at-ns=%llu\n",
			    addr, route.apid, route.write_ee, route.irq_owner, value,
			    (unsigned long long)ns);
		} else {
			SWEET_LOG_WARN("SWEET CHARGER PREFLIGHT: sid=2 addr=%#x apid=%u read-error=%d value=unknown at-ns=%llu\n",
			    addr, route.apid, result, (unsigned long long)ns);
		}
	}
	/* Preflight only. Keep ADC addresses/protocol inside the controller backend;
	 * this service neither starts conversions nor treats cached data as limits. */
	struct sweet_adc5_preflight adc = {};
	int adcError = sweet_spmi_adc5_inspect(&adc);
	uint64_t adcTime, adcNs;
	clock_get_uptime(&adcTime); absolutetime_to_nanoseconds(adcTime, &adcNs);
	SWEET_LOG_INFO("SWEET ADC5 PREFLIGHT: v1 error=%d valid=%#x at-ns=%llu measurement-valid=0; no conversion requested\n",
	    adcError, adc.valid, (unsigned long long)adcNs);
	if (adcError || adc.valid != ((1u << SWEET_ADC5_FIELD_COUNT) - 1u)) {
		SWEET_LOG_WARN("SWEET ADC5 PREFLIGHT: incomplete error=%d valid=%#x; no measurement or control permission\n", adcError, adc.valid);
	}
	if (!adcError) {
		const struct sweet_adc5_route *routes[] = {&adc.user, &adc.calibration};
		for (unsigned i = 0; i < 2; ++i) {
			SWEET_LOG_INFO("SWEET ADC5 ROUTE: v1 block=%s observer=%u writer=%u writer-error=%d write-ee=%u irq-apid=%u irq-error=%d\n",
			    i ? "calibration" : "user", routes[i]->observer_apid, routes[i]->writer_apid,
			    routes[i]->writer_error, routes[i]->writer_ee, routes[i]->irq_apid, routes[i]->irq_error);
		}
		for (unsigned i = 0; i < SWEET_ADC5_FIELD_COUNT; ++i) {
			SWEET_LOG_INFO("SWEET ADC5 FIELD: v1 field=%u raw=%#x valid=%u error=%d; diagnostic-only\n",
			    i, adc.raw[i], (adc.valid >> i) & 1u, adc.errors[i]);
		}
	}
	SWEET_LOG_INFO("SWEET CHARGER PREFLIGHT: complete; write permission not exercised, charging policy absent\n");
	return kIOReturnSuccess;
}
#endif /* ARM64_BOARD_CONFIG_MH2LM */

bool
SweetBattery::start(IOService *provider)
{
	if (!IOPMPowerSource::start(provider)) {
		return false;
	}
	int real = 0;
	(void)PE_parse_boot_argn("sweetbattery", &real, sizeof(real));
	fReal = real == 1 && sweet_spmi_init() == 0;
	/* sweetbatt=1 opts in to the PM8150B (USID 2) reads, on top of the real
	 * polling above. Without it fSid2 stays false and poll() is unchanged. */
	int sid2 = 0;
	(void)PE_parse_boot_argn("sweetbatt", &sid2, sizeof(sid2));
	fSid2 = fReal && sid2 == 1;
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	int irqMode = 0;
	(void)PE_parse_boot_argn("sweetbattirq", &irqMode, sizeof(irqMode));
	if (irqMode < 0 || irqMode > 4 || (irqMode && !fSid2)) {
		SWEET_LOG_ERROR("SWEET BATT IRQ: invalid mode=%d or real SID2 service disabled\n", irqMode);
		return false;
	}
	fIrqMode = (unsigned)irqMode;
	int runtime = 0;
	(void)PE_parse_boot_argn("sweetbattruntime", &runtime, sizeof(runtime));
	if (runtime < 0 || runtime > 3 || (runtime && (!fSid2 || fIrqMode < 4))) {
		SWEET_LOG_ERROR("SWEET BATT RUNTIME: invalid mode=%d; real SID2 and IRQ mode4 required\n", runtime);
		return false;
	}
	fRuntimeRequested = (unsigned)runtime;
	int tempIrq = 0;
	(void)PE_parse_boot_argn("sweetbatttempirq", &tempIrq, sizeof(tempIrq));
	if (tempIrq < 0 || tempIrq > 1 || (tempIrq && runtime != 3)) {
		SWEET_LOG_ERROR("SWEET BATT TEMP IRQ: invalid mode=%d; runtime3 required\n", tempIrq);
		return false;
	}
	fTempIrq = tempIrq == 1;
	int dependencies = 0;
	(void)PE_parse_boot_argn("sweetbattdeps", &dependencies, sizeof(dependencies));
	if (dependencies < 0 || dependencies > 7 || (dependencies && runtime != 1)) {
		SWEET_LOG_ERROR("SWEET BATT DEPENDENCIES: invalid mode=%d; runtime1 observation required\n", dependencies);
		return false;
	}
	fDependencyMode = (unsigned)dependencies;
	int adcPreflight = 0;
	(void)PE_parse_boot_argn("sweetbattadcdiag", &adcPreflight, sizeof(adcPreflight));
	fAdcPreflightRequested = adcPreflight == 1;
#endif
	int diag = 0;
	(void)PE_parse_boot_argn("sweetbattdiag", &diag, sizeof(diag));
	fDiag = fSid2 && diag == 1;
	int uiDiag = 0;
	(void)PE_parse_boot_argn("sweetbattuidiag", &uiDiag, sizeof(uiDiag));
	fUiDiag = fSid2 && uiDiag == 1;
	gSweetBattUiDiag = fUiDiag;
	int irqDiag = 0;
	(void)PE_parse_boot_argn("sweetbattirqdiag", &irqDiag, sizeof(irqDiag));
	if (fSid2) {
		SWEET_LOG_INFO("SWEET BATTERY: PM8150B SID %u read path enabled (sweetbatt=1)\n",
		    SWEET_BATT_SID);
	}
	if (fDiag) {
		SWEET_LOG_INFO("SWEET BATTERY: charger/current diagnostics enabled (read-only)\n");
	}
	if (fUiDiag) {
		SWEET_LOG_INFO("SWEET BATT UI: transition/user-client tracing enabled (read-only)\n");
	}
	if (fSid2 && irqDiag == 1) {
		SWEET_LOG_INFO("SWEET BATTERY: PMIC-arb IRQ preflight enabled (read-only)\n");
		sweet_spmi_irq_diag();
		logIrqLines();
	}
	if (fIrqMode) {
		fPreflightOk = preflightIrq();
		SWEET_LOG_INFO("SWEET BATT IRQ: selected mode=%u preflight=%u\n", fIrqMode, fPreflightOk ? 1 : 0);
		if (fIrqMode >= 2 && !fPreflightOk) {
			failIrq("PM8150B identity/FG preflight failed before arm");
			return false;
		}
	}
	putNumber("SweetBatteryIRQMode", fIrqMode);
	/* Publish a complete source BEFORE registerService wakes powerd. */
	poll();
	if (fIrqFailed) {
		return false;
	}
	if (fReal) {
		fLoop = IOWorkLoop::workLoop();
		fTimer = IOTimerEventSource::timerEventSource(this, &SweetBattery::timerFired);
		if (fLoop && fTimer && fLoop->addEventSource(fTimer) == kIOReturnSuccess) {
			fTimerAdded = true;
#if defined(ARM64_BOARD_CONFIG_MH2LM)
			if (fRuntimeRequested) {
				fMonitor = IOTimerEventSource::timerEventSource(this, &SweetBattery::monitorFired);
				if (!fMonitor || fLoop->addEventSource(fMonitor) != kIOReturnSuccess) {
					failIrq("protection monitor unavailable");
					return false;
				}
				fMonitorAdded = true;
				fControlLock = IOLockAlloc();
				if (!fControlLock || !IOService::getPMRootDomain()) {
					SWEET_LOG_ERROR("SWEET BATT RUNTIME: control lock or PM root domain unavailable\n");
					return false;
				}
				fPowerNotice = registerPrioritySleepWakeInterest(runtimePowerNotice, this);
				if (!fPowerNotice) {
					SWEET_LOG_ERROR("SWEET BATT RUNTIME: power transition notifier unavailable\n");
					return false;
				}
			}
#endif
			fRunning = true;
#if defined(ARM64_BOARD_CONFIG_MH2LM)
			int transport = 0;
			(void)PE_parse_boot_argn("sweetbatttransport", &transport, sizeof(transport));
			if (fSid2 && (transport == 1 || fAdcPreflightRequested)) {
				(void)fLoop->runAction(&SweetBattery::transportPreflightAction, this);
			}
#endif
			if (fIrqMode >= 2) {
				fIrqEvent = IOInterruptEventSource::interruptEventSource(this, &SweetBattery::irqAction, NULL, 0);
				if (fIrqEvent && fLoop->addEventSource(fIrqEvent) == kIOReturnSuccess) {
					fIrqEventAdded = true;
					(void)fLoop->runAction(&SweetBattery::startIrqAction, this);
				} else {
					failIrq("battery software event source unavailable");
				}
			} else {
				fTimer->setTimeoutMS(SWEET_BATT_POLL_MS);
			}
#if defined(ARM64_BOARD_CONFIG_MH2LM)
			if (fRuntimeRequested && fLoop->runAction(&SweetBattery::startRuntime, this) != kIOReturnSuccess) {
				return false;
			}
#endif
		} else if (fIrqMode >= 2) {
			failIrq("battery workloop/timer unavailable");
		} else {
			fReal = false;
			poll();
			SWEET_LOG_ERROR("SWEET BATTERY: timer unavailable; static fallback\n");
		}
	}

	registerService();
	return true;
}

bool
SweetBattery::willTerminate(IOService *provider, IOOptionBits options)
{
	removeRuntimeNotifier();
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 if (fAdcClient) { fAdcClient->stop(); }
#endif
	if (fLoop && fTimerAdded) {
		(void)fLoop->runAction(&SweetBattery::stopPolling, this);
	}
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	if (fMuxClient) {
		fMuxClient->stop();
	}
#endif
	return IOPMPowerSource::willTerminate(provider, options);
}

void
SweetBattery::stop(IOService *provider)
{
	removeRuntimeNotifier();
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 if (fAdcClient) { fAdcClient->stop(); }
#endif
	if (fLoop && fTimerAdded) {
		/* Serialize against a running timer callback, including rearming. */
		(void)fLoop->runAction(&SweetBattery::stopPolling, this);
#if defined(ARM64_BOARD_CONFIG_MH2LM)
		if (fMuxClient) {
			fMuxClient->stop();
		}
#endif
		fLoop->removeEventSource(fTimer);
		fTimerAdded = false;
	}
	if (fLoop && fIrqEventAdded) {
		fLoop->removeEventSource(fIrqEvent);
		fIrqEventAdded = false;
	}
	if (fLoop && fMonitorAdded) {
		fLoop->removeEventSource(fMonitor);
		fMonitorAdded = false;
	}
	IOPMPowerSource::stop(provider);
}

IOReturn
SweetBattery::stopPolling(OSObject *owner, void *, void *, void *, void *)
{
	SweetBattery *self = OSDynamicCast(SweetBattery, owner);
	if (!self) {
		return kIOReturnBadArgument;
	}
	self->stopRuntime();
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 if (self->fAdcClient) { self->fAdcClient->invalidate(); self->publishAdc(); }
#endif
	self->fRunning = false;
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	if (self->fMuxClient) {
		self->fMuxClient->detach();
	}
	self->fIrqAttached = false;
#endif
	if (self->fMonitor) {
		self->fMonitor->cancelTimeout();
		self->fMonitor->disable();
	}
	if (self->fIrqEvent) {
		self->fIrqEvent->disable();
	}
	self->fTimer->cancelTimeout();
	self->fTimer->disable();
	return kIOReturnSuccess;
}

void
SweetBattery::free(void)
{
	removeRuntimeNotifier();
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 if (fAdcClient) { fAdcClient->stop(); }
#endif
	/* Defensive unwind also covers a start() failure before stop(). */
	if (fLoop && fTimerAdded) {
		(void)fLoop->runAction(&SweetBattery::stopPolling, this);
		fLoop->removeEventSource(fTimer);
	}
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	if (fMuxClient) {
		fMuxClient->stop();
		OSSafeReleaseNULL(fMuxClient);
	}
#endif
	if (fLoop && fIrqEventAdded) {
		fLoop->removeEventSource(fIrqEvent);
	}
	OSSafeReleaseNULL(fIrqEvent);
	OSSafeReleaseNULL(fTimer);
	if (fLoop && fMonitorAdded) {
		fLoop->removeEventSource(fMonitor);
		fMonitorAdded = false;
	}
	OSSafeReleaseNULL(fMonitor);
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 OSSafeReleaseNULL(fAdcClient);
#endif
	OSSafeReleaseNULL(fLoop);
	if (fControlLock) { IOLockFree(fControlLock);fControlLock = NULL; }
	IOPMPowerSource::free();
}
#endif /* ARM64_BOARD_CONFIG_SWEET */
