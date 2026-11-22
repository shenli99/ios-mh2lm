/* SM8150 unified clock entry (clock plan C1) and its controller contract.
 * Read-only first candidate: registration, bindings, descriptors and
 * configuration queries. Every hardware operation returns Unsupported; no
 * controller in this candidate has a register write path at all. */
#ifndef IOQCOM_CLOCK_H
#define IOQCOM_CLOCK_H
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <kern/thread_call.h>

enum IOQcomClockControllerID : uint32_t { kIOQcomCCNone=0, kIOQcomCCGCC=1, kIOQcomCCDispCC=2,
 kIOQcomCCRPMh=3, kIOQcomCCDiagnostic=7, kIOQcomCCMax=8 };
enum IOQcomClockKind : uint32_t { kIOQcomClockBranch=1, kIOQcomClockRCG=2 };
/* Halt semantics differ by branch type; HALT_DELAY CLK_OFF is not evidence. */
enum IOQcomClockHalt : uint32_t { kIOQcomHaltCheck=1, kIOQcomHaltDelay=2 };
enum IOQcomClockCap : uint32_t { kIOQcomCapQueryConfig=1u<<0, kIOQcomCapQueryRate=1u<<1,
 kIOQcomCapEnable=1u<<8, kIOQcomCapDisable=1u<<9, kIOQcomCapSetRate=1u<<10, kIOQcomCapReset=1u<<11 };
/* Parent identity: a local clock, a fixed source or unknown. Never a request. */
enum IOQcomClockSource : uint32_t { kIOQcomSourceNone=0, kIOQcomSourceLocal=1, kIOQcomSourceXO=2,
 kIOQcomSourceXODiv4=3, kIOQcomSourceGPLL0Main=4, kIOQcomSourceGPLL0Even=5, kIOQcomSourceUnknown=6 };
enum IOQcomClockDomain : uint32_t { kIOQcomDomainCX=1 };
/* C1-04 admission verdict. Reasons are diagnostic facts, not error strings. */
enum IOQcomClockDep : uint32_t { kIOQcomDepReady=0, kIOQcomDepControllerAbsent=1,
 kIOQcomDepParentController=2, kIOQcomDepParentClock=3, kIOQcomDepCycle=4,
 kIOQcomDepDomain=5, kIOQcomDepAccess=6, kIOQcomDepUnknownParent=7,
 kIOQcomDepMalformed=8, kIOQcomDepDepth=9 };
enum : unsigned { kIOQcomMaxParents=4, kIOQcomCornerCount=3, kIOQcomBindingMax=8,
 kIOQcomMaxDependencyDepth=8, kIOQcomWalkMax=16 };

struct IOQcomClockParent { uint32_t source, controller, localID; };
/* C1-04 descriptor: declared facts only. Nothing here powers or votes. */
struct IOQcomClockInfo {
 uint32_t controller, localID, kind, halt, capabilities;
 IOQcomClockParent possible[kIOQcomMaxParents]; unsigned possibleCount;
 uint32_t accessDeps;        /* register-access prerequisites; 0 = controller always accessible */
 uint32_t domain;            /* required supply domain for the output */
 uint64_t cornerMaxHz[kIOQcomCornerCount]; /* MIN/LOWER/LOW rate ceilings, 0 = none declared */
 bool pinned;                /* inherited/critical: never a disable candidate */
 bool inheritedOn;           /* enable bit observed at controller start */
};
/* C2-02 query: configured (decoded) state, never a measured rate. */
struct IOQcomClockState {
 uint32_t raw[2];            /* branch: CBCR; RCG: CMD, CFG */
 bool enableBit, clockOffBit, haltTrusted, rootOff, updatePending;
 IOQcomClockParent selected;
 uint64_t configuredHz;      /* 0 with rateKnown=false when the parent rate is untrusted */
 bool rateKnown;
 unsigned divX2;             /* RCG half-integer divider times two */
};

class IOQcomClockProvider;

/* A controller owns its own identity, resource table and capabilities. The
 * provider stores the registration (identity + lifetime) only and never maps,
 * reads or validates controller windows; that stays with each CC backend. */
class IOQcomClockController : public IOService {
 OSDeclareAbstractStructors(IOQcomClockController);
public:
 virtual uint32_t clockControllerID(void) const=0;
 virtual IOReturn copyClockInfo(uint32_t localID,IOQcomClockInfo *info)=0;
 virtual IOReturn copyClockState(uint32_t localID,IOQcomClockState *state)=0;
 /* Default for every operation not explicitly implemented and verified. */
 virtual IOReturn enableClock(uint32_t,uint64_t);
 virtual IOReturn disableClock(uint32_t,uint64_t);
 virtual uint64_t registerWrites(void) const { return 0; }
};

class IOQcomClockHandle : public OSObject {
 OSDeclareDefaultStructors(IOQcomClockHandle);
 friend class IOQcomClockProvider;
 IOQcomClockProvider *fProvider;
 IOService *fConsumer;
 uint32_t fController, fLocalID;
 uint64_t fGeneration, fToken;
public:
 void free(void) APPLE_KEXT_OVERRIDE;
 uint32_t controller(void) const { return fController; }
 uint32_t localID(void) const { return fLocalID; }
 uint64_t token(void) const { return fToken; }
 uint64_t generation(void) const { return fGeneration; }
 /* Every query keeps the controller object alive across the backend call.
 * After withdrawal or provider stop they return NotReady, never stale data. */
 IOReturn copyInfo(IOQcomClockInfo *info);
 IOReturn copyState(IOQcomClockState *state);
 IOReturn enable(void);
 IOReturn disable(void);
};

class IOQcomClockProvider : public IOService {
 OSDeclareDefaultStructors(IOQcomClockProvider);
 friend class IOQcomClockHandle;
 struct Slot { IOQcomClockController *controller; uint64_t generation; bool active; };
 struct Walk { uint32_t controller[kIOQcomWalkMax]; uint32_t localID[kIOQcomWalkMax]; unsigned count; };
 IOLock *fLock;
 Slot fSlots[kIOQcomCCMax];
 uint64_t fNextGeneration, fNextToken, fHandles;
 bool fReady, fStopping;
 thread_call_t fDiagCall;
 /* generation 0 = any active registration (dependency walk); otherwise the
 * exact registration observed at bind time. Returns a retained controller. */
 IOReturn retainController(uint32_t id,uint64_t generation,IOQcomClockController **out,
  uint64_t *generationOut);
 IOReturn admitWalk(uint32_t controller,uint32_t localID,unsigned depth,Walk *walk,
  const char *consumer,uint32_t *reason,bool log);
 /* Same admission walk, with the refusal line suppressed when the caller will
 * report the failure under its own phase (a handle enable, a diagnostics probe). */
 IOReturn admitInternal(IOQcomClockController *owner,uint32_t localID,const char *consumer,
  IOQcomClockInfo *info,uint32_t *reason,bool requirements,bool log);
 IOReturn checkDependenciesInternal(IOQcomClockHandle *handle,uint32_t *reason,bool log);
 static void diagnose(thread_call_param_t,thread_call_param_t);
 void runDiagnostics(void);
public:
 bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
 void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
 void free(void) APPLE_KEXT_OVERRIDE;
 /* Registration is accepted only with a live provider and a unique, in-range
 * identity. The controller is retained until withdraw or stop. */
 IOReturn registerController(IOQcomClockController *controller);
 IOReturn withdrawController(IOQcomClockController *controller);
 /* Resolve a consumer resource name through its sweet,clock-names /
 * sweet,clocks properties. NotFound: no such name/ID; NotReady: the named
 * controller is not registered (yet). No hardware access. */
 IOReturn acquireClock(IOService *consumer,const char *name,IOQcomClockHandle **out);
 /* Read-only admission of a bound handle: walks declared clock parents,
 * register-access prerequisites and required supply domains. Success means
 * the execution interfaces may be attempted by this candidate; it does not
 * claim that the clock can be enabled without later cards. */
 IOReturn checkDependencies(IOQcomClockHandle *handle,uint32_t *reason);
 IOReturn admit(IOQcomClockController *owner,uint32_t localID,const char *consumer,
  IOQcomClockInfo *info,uint32_t *reason,bool requirements);
};

#endif /* ARM64_BOARD_CONFIG_MH2LM */
#endif /* IOQCOM_CLOCK_H */
