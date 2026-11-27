/* C3-04: single-owner publication and the platform claim router.
 *
 * Two things live here and nothing else: the table a boot publishes and the
 * claim symbol osfmk/arm64/sleh.c calls from its handwritten dispatch chain.
 * The lock is allocated once, on the thread that publishes, and is never freed:
 * a late interrupt that reaches the router after the owner retired must find a
 * usable lock and an empty slot, not a freed lock.
 */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "SweetAppsDoor.h"
#include <IOKit/IOLib.h>
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"

/* The published table, the fence around it, and the name a ring row can print.
 * publication is decisive: whoever gets here first owns the stores and the line
 * for the whole boot, and the loser says so instead of sharing. */
static IOSimpleLock *doorDispatch;
static const SweetAppsDoor *doorOwner;

static IOSimpleLock *
door_lock(void)
{
	IOSimpleLock *lock=__atomic_load_n(&doorDispatch,__ATOMIC_ACQUIRE);
	if (!lock) {
		IOSimpleLock *candidate=IOSimpleLockAlloc();
		if (!candidate) return NULL;
		if (__atomic_compare_exchange_n(&doorDispatch,&lock,candidate,false,
		    __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
			SWEET_LOG_INFO("SWEET APPS DOOR: lock %p\n",candidate);
			lock=candidate;
		}
		else IOSimpleLockFree(candidate);
	}
	return lock;
}

SweetAppsOwner
sweet_apps_owner(bool delegated,bool irqServices)
{
	/* A delegated node and a service IRQ description contradict each other: the
	 * tree cannot mean both owners, so it means neither and both drivers refuse
	 * their start. Resolving it to "whoever asked first" is exactly the race
	 * this card exists to remove. */
	if (delegated) return irqServices ? SweetAppsOwnerNone : SweetAppsOwnerChannel;
	return irqServices ? SweetAppsOwnerService : SweetAppsOwnerNone;
}

const SweetAppsDoor *
sweet_apps_door(void)
{
	return __atomic_load_n(&doorOwner,__ATOMIC_ACQUIRE);
}

bool
sweet_apps_door_publish(const SweetAppsDoor *door)
{
	if (!door || !door->name || !door->context || !door->read || !door->write ||
	    !door->irq) {
		SWEET_LOG_ERROR("SWEET APPS DOOR: phase=publish result=%d detail=incomplete-table\n",
		 kIOReturnBadArgument);
		return false;
	}
	IOSimpleLock *lock=door_lock();
	if (!lock) {
		SWEET_LOG_ERROR("SWEET APPS DOOR: phase=publish result=%d detail=lock-absent\n",
		 kIOReturnNoMemory);
		return false;
	}
	const SweetAppsDoor *held=NULL;
	bool ok=false;
	IOInterruptState saved=IOSimpleLockLockDisableInterrupt(lock);
	held=__atomic_load_n(&doorOwner,__ATOMIC_ACQUIRE);
	if (!held) { __atomic_store_n(&doorOwner,door,__ATOMIC_RELEASE); ok=true; }
	IOSimpleLockUnlockEnableInterrupt(lock,saved);
	if (!ok) {
		/* Never steal, and never fall back to a private MMIO path: the loser
		 * keeps working through the published table or refuses its start. */
		SWEET_LOG_WARN("SWEET APPS DOOR: phase=publish result=%d detail=line-taken owner=%s\n",
		 kIOReturnExclusiveAccess,held->name?held->name:"unknown");
		return false;
	}
	SWEET_LOG_INFO("SWEET APPS DOOR: phase=publish result=0 owner=%s intid=%u stores=%u irqs=%u\n",
	 door->name,kSweetAppsDoorIntid,
	 door->stores?door->stores(door->context):0u,
	 door->irqs?door->irqs(door->context):0u);
	return true;
}

void
sweet_apps_door_withdraw(const SweetAppsDoor *door)
{
	IOSimpleLock *lock=__atomic_load_n(&doorDispatch,__ATOMIC_ACQUIRE);
	if (!lock) return;
	/* Takes the fence the router holds, so a store or an interrupt in flight
	 * finishes before the owner releases its maps. */
	IOInterruptState saved=IOSimpleLockLockDisableInterrupt(lock);
	const SweetAppsDoor *held=__atomic_load_n(&doorOwner,__ATOMIC_ACQUIRE);
	if (held && (door==NULL || held==door)) {
		__atomic_store_n(&doorOwner,NULL,__ATOMIC_RELEASE);
		SWEET_LOG_INFO("SWEET APPS DOOR: phase=withdraw owner=%s stores=%u irqs=%u\n",
		 held->name?held->name:"unknown",
		 held->stores?held->stores(held->context):0u,
		 held->irqs?held->irqs(held->context):0u);
	}
	IOSimpleLockUnlockEnableInterrupt(lock,saved);
}

/* The handwritten GIC chain in sleh.c calls this for every interrupt it takes,
 * with the line number. Unclaimed lines fall through to gic_dispatch(). */
extern "C" bool
sweet_rpmh_irq(unsigned int intid)
{
	if (intid!=kSweetAppsDoorIntid) return false;
	/* No allocation, no logging, no wait: interrupt context. A boot where the
	 * owner has not published yet simply has nothing to hand the line to. */
	IOSimpleLock *lock=__atomic_load_n(&doorDispatch,__ATOMIC_ACQUIRE);
	if (lock) {
		IOInterruptState saved=IOSimpleLockLockDisableInterrupt(lock);
		const SweetAppsDoor *door=__atomic_load_n(&doorOwner,__ATOMIC_ACQUIRE);
		if (door && door->irq) (void)door->irq(door->context);
		IOSimpleLockUnlockEnableInterrupt(lock,saved);
	}
	/* Reserved apps RSC SPI: claiming it keeps a stopped or not-yet-started
	 * owner's late delivery out of the generic dispatcher. */
	return true;
}
#endif /* ARM64_BOARD_CONFIG_MH2LM */