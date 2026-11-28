/* C3-05: fixed-capacity FIFO, no allocation and no policy.
 *
 * pop() only moves the head forward; the slot is overwritten by the next
 * push(), so a retired request's bytes are not retained past the owner's
 * dispatch. Nothing here calls into IOKit, locks, or the log: the owner
 * serializes every call from its gate, and a refusal is the owner's row to
 * print, not this unit's.
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
	fAccepted = fDispatched = fHighWater = 0;
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
#endif