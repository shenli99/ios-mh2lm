/* C3-05: the bounded submission queue in front of the apps RSC.
 *
 * The array runs one transaction at a time, and a display-corner request is
 * itself up to five flights (guard, guard, then three supplies). Callers that
 * arrive while one transaction is in flight used to be refused outright; they
 * now wait here, in arrival order, until the owner retires the previous one.
 *
 * This unit owns no policy and touches no hardware. It stores a record the
 * owner filled in, hands back the same bytes, and runs out of room explicitly.
 * There is no dynamic storage, no timer, no reference counting, no coalescing
 * and no drop-oldest: what it accepts it still holds until the owner retires
 * it. The caller's stack and its live objects are gone by the time a queued
 * request is dispatched, so everything needed to rebuild it must already be
 * inside the record.
 *
 * C3-06 adds the one way a record leaves without being dispatched: the owner
 * withdraws it by the token it already handed to the caller, or by client when
 * that client exits. Both only ever see what is still waiting - a dispatched
 * request is out of this storage, so the queue cannot cancel a flight, and a
 * cancelled record is gone before the dispatcher could reach it. Order among
 * the survivors is the arrival order they were stamped with, never slot order.
 */
#ifndef SWEET_APPS_QUEUE_H
#define SWEET_APPS_QUEUE_H
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOTypes.h>

/* Eight is one waiting request per client slot. A request larger than that is
 * refused, never merged into an older one: two waiting votes for the same
 * resource have no defined order once they collapse. */
enum : unsigned { kSweetAppsQueueCapacity = 8 };

struct SweetAppsRequest {
	unsigned kind;		/* owner-defined tag */
	unsigned client;	/* owner's client slot, not a caller pointer */
	unsigned rail;		/* owner's resource tag; kinds without a
				 * sub-target use 0 */
	uint64_t param[2];	/* owner's payload, decoded by the owner */
	uint64_t token;		/* issued before enqueueing, so the caller can
				 * already query the request it submitted */
	unsigned seq;		/* stamped by push(); never reused in a boot */
};

class SweetAppsQueue {
	SweetAppsRequest fSlots[kSweetAppsQueueCapacity];
	unsigned fHead, fCount, fNextSeq;
	unsigned fAccepted, fDispatched, fHighWater, fCancelled;
	void removeAt(unsigned index, SweetAppsRequest *removed);
public:
	SweetAppsQueue();
	void reset(void);
	bool empty(void) const { return fCount == 0; }
	bool full(void) const { return fCount == kSweetAppsQueueCapacity; }
	unsigned depth(void) const { return fCount; }
	unsigned accepted(void) const { return fAccepted; }
	unsigned dispatched(void) const { return fDispatched; }
	unsigned highWater(void) const { return fHighWater; }
	/* Copy in, never overwrite: false means the queue is full and the caller
	 * must refuse the request - dropping an older one would silently reorder
	 * two accepted votes. full() answers the same question before a token is
	 * spent. The stamped sequence is what an ordering claim about a ring row
	 * is checked against. */
	bool push(const SweetAppsRequest &request, SweetAppsRequest *stored = NULL);
	/* Oldest request, still owned by the queue. NULL when empty. */
	const SweetAppsRequest *head(void) const;
	/* Retire the head after it has been dispatched or failed. */
	void pop(void);
	/* One waiting request for exactly this resource, or NULL. Same client and
	 * same resource means two requests would drive the same field once
	 * dispatched, so the owner refuses the second one instead of queueing it. */
	const SweetAppsRequest *find(unsigned kind, unsigned client, unsigned rail) const;
	/* Whether this client still has a waiting request, without naming one. */
	bool waiting(unsigned client) const;
	/* C3-06: withdraw one waiting record by the token the owner issued for it.
	 * True means it was still waiting and no longer is, and *removed carries the
	 * same bytes the dispatcher would have seen, so the owner can fail exactly
	 * that request against exactly that token. False means this queue never
	 * held that token - it was already dispatched, already withdrawn, or never
	 * admitted - which is the owner's to distinguish, not the queue's. */
	bool cancel(uint64_t token, SweetAppsRequest *removed = NULL);
	/* The waiting record for a token, or NULL. Read-only, so the owner can
	 * settle who it belongs to before withdrawing anything. */
	const SweetAppsRequest *findToken(uint64_t token) const;
	/* Every waiting record of one client, because an exiting client may hold one
	 * per resource. Newest first in *removed so the owner's own re-insertion (if
	 * it ever needed one) cannot invert arrival order; at most cap are written
	 * and the return value is how many left the queue. */
	unsigned cancelClient(unsigned client, SweetAppsRequest *removed = NULL,
	    unsigned cap = kSweetAppsQueueCapacity);
	/* How many records have left this queue without being dispatched. */
	unsigned cancelled(void) const { return fCancelled; }
};
#endif
#endif