/* MDSS-internal clock leases. No MMIO addresses in the caller API.
 * Branch leases retain inherited parents unless the separate typed core
 * controller has explicitly established their rates/domain. Supply/NoC votes
 * are separate typed transactions, not implied by a local clock lease.
 * This interface grants no right to reset/collapse/reprogram a display domain.
 */
#ifndef SWEET_DISPLAY_CLOCK_H
#define SWEET_DISPLAY_CLOCK_H
#include <stdint.h>
enum SweetCoreStart { SweetCoreWarm, SweetCoreCold };
enum SweetCorePhase { CoreIdle, CoreConfigVote, CoreDomain, CoreDomainWait,
 CoreStable, CorePLL, CorePLLConfig, CorePLLRun, CorePLLLock, CorePLLOut,
 CorePLLGlobal, CoreRoot, CoreRootUpdate, CoreRootWait, CoreBranches, CoreReady, CoreFault, CoreStopped };
enum SweetClockUse { SweetClockCore, SweetClockDSI0, SweetClockRSC, SweetClockUseCount };
enum SweetClockState { SweetClockUnused, SweetClockPending, SweetClockHeld,
 SweetClockReleasing, SweetClockReleased, SweetClockFailed, SweetClockStopped };
enum { SweetClockCount=10 };
struct SweetClockLease {
 uint64_t token;
 unsigned mask, state;
 int result;
 bool inheritedUpstream, uncertain;
 bool hardwareSuspended; /* lease retained; HS, and Lineage OFF ESC, gated */
};
#endif
