/* C3-05: fixed-capacity FIFO, no allocation and no policy.
 *
 * pop() only moves the head forward; the slot is overwritten by the next
 * push(), so a retired request's bytes are not retained past the owner's
 * dispatch. Nothing here calls into IOKit, locks, or the log: the owner
 * serializes every call from its gate, and a refusal is the owner's row to
 * print, not this unit's.
 *
 * C3-06: a withdrawn record is compacted out of the logical window, so the
 * surviving order is the sequence the records were stamped with and no slot
 * between head and tail is ever reachable twice. Cancelling is not dispatching:
 * fDispatched only counts records the dispatcher actually took, which is what
 * lets a ring reader tell "the owner retired it" from "the caller took it back".
 */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "SweetAppsQueue.h"

SweetAppsQueue::SweetAppsQueue()
{
	reset();
}

void
SweetAppsQueue::reset(void)
{
	for (unsigned i = 0; i < kSweetAppsQueueCapacity; ++i) fSlots[i] = SweetAppsRequest();
	fHead = fCount = fNextSeq = 0;
	fAccepted = fDispatched = fHighWater = fCancelled = 0;
}

bool
SweetAppsQueue::push(const SweetAppsRequest &request, SweetAppsRequest *stored)
{
	if (full()) return false;
	unsigned tail = (fHead + fCount) % kSweetAppsQueueCapacity;
	fSlots[tail] = request;
	fSlots[tail].seq = ++fNextSeq;
	fCount++;
	fAccepted++;
	if (fCount > fHighWater) fHighWater = fCount;
	if (stored) *stored = fSlots[tail];
	return true;
}

const SweetAppsRequest *
SweetAppsQueue::head(void) const
{
	if (!fCount) return NULL;
	return &fSlots[fHead];
}

void
SweetAppsQueue::pop(void)
{
	if (!fCount) return;
	fHead = (fHead + 1) % kSweetAppsQueueCapacity;
	fCount--;
	fDispatched++;
}

const SweetAppsRequest *
SweetAppsQueue::find(unsigned kind, unsigned client, unsigned rail) const
{
	for (unsigned i = 0; i < fCount; ++i) {
		const SweetAppsRequest &slot = fSlots[(fHead + i) % kSweetAppsQueueCapacity];
		if (slot.kind == kind && slot.client == client && slot.rail == rail) return &slot;
	}
	return NULL;
}

bool
SweetAppsQueue::waiting(unsigned client) const
{
	for (unsigned i = 0; i < fCount; ++i) {
		const SweetAppsRequest &slot = fSlots[(fHead + i) % kSweetAppsQueueCapacity];
		if (slot.client == client) return true;
	}
	return false;
}

/* Pull one record out of the logical window and close the gap behind it. The
 * slot that leaves keeps its bytes only in *removed; it is not re-readable
 * through the queue, which is the point - nothing the owner cancels can be
 * dispatched by a later drain. */
void
SweetAppsQueue::removeAt(unsigned index, SweetAppsRequest *removed)
{
	if (index >= fCount) return;
	if (removed) *removed = fSlots[(fHead + index) % kSweetAppsQueueCapacity];
	for (unsigned i = index; i + 1 < fCount; ++i)
		fSlots[(fHead + i) % kSweetAppsQueueCapacity] =
			fSlots[(fHead + i + 1) % kSweetAppsQueueCapacity];
	fSlots[(fHead + fCount - 1) % kSweetAppsQueueCapacity] = SweetAppsRequest();
	fCount--;
	fCancelled++;
}

const SweetAppsRequest *
SweetAppsQueue::findToken(uint64_t token) const
{
	if (!token) return NULL;
	for (unsigned i = 0; i < fCount; ++i) {
		const SweetAppsRequest &slot = fSlots[(fHead + i) % kSweetAppsQueueCapacity];
		if (slot.token == token) return &slot;
	}
	return NULL;
}

bool
SweetAppsQueue::cancel(uint64_t token, SweetAppsRequest *removed)
{
	if (!token) return false;
	for (unsigned i = 0; i < fCount; ++i) {
		if (fSlots[(fHead + i) % kSweetAppsQueueCapacity].token != token) continue;
		removeAt(i, removed);
		return true;
	}
	return false;
}

unsigned
SweetAppsQueue::cancelClient(unsigned client, SweetAppsRequest *removed, unsigned cap)
{
	unsigned taken = 0;
	/* Backwards, so every index still to be examined keeps its meaning while
	 * the window shrinks under it. */
	for (unsigned i = fCount; i-- > 0; ) {
		const SweetAppsRequest &slot = fSlots[(fHead + i) % kSweetAppsQueueCapacity];
		if (slot.client != client) continue;
		if (removed && taken < cap) removeAt(i, &removed[taken]);
		else removeAt(i, NULL);
		taken++;
	}
	return taken;
}
#endif