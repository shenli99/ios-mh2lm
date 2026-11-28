/* C3-04: the one door to the apps RSC hardware.
 *
 * Until this card the display service was both the policy owner and the
 * hardware owner: it mapped the TCS array writable, programmed INTID37 itself
 * and was the only code that ever stored an RSC word. C3-04 lets the tree pick
 * which side plays that role and makes the choice a property of the boot, not
 * of a code path: whoever owns the line publishes one table here, before its
 * first store, and everything else reaches the hardware through that table.
 *
 * The table is small and does one thing per entry, so a ring row or a review
 * can point at the caller that performed an operation:
 *   read/write   - the TCS array, array-global offsets (the slot base is added
 *                  by the caller, exactly as the executor already expects)
 *   identity     - the three DRV words, read once by the owner that mapped it
 *   triggerReady - admit the completion line before a trigger (RWP, drain,
 *                  foreign pending); NotReady means "ask again shortly"
 *   arm          - from here on a completion on the line is this flight's
 *   unmask       - enable the line, after the trigger has been posted
 *   quiet        - mask and drop this slot's source bit after a failed flight
 *   stop         - retire the line for good; the maps stay with the owner
 *   irq          - interrupt context: claim INTID37 and wake the owner
 *   install      - let the policy side register its completion callback
 *   stores/irqs  - counters, so "the old side wrote nothing" is a number and
 *                  not an assertion
 *
 * publish() is single-owner by compare-and-swap: a second owner is refused and
 * logged instead of silently taking the line. withdraw() only accepts the
 * table it published, so a stopped owner cannot retire its successor's claim.
 *
 * sweet_rpmh_irq() is the platform's claim symbol (osfmk/arm64/sleh.c calls it
 * from the native dispatch chain). It no longer lives in the display service:
 * it is a router that hands INTID37 to whoever published a door. With nobody
 * published it still claims the line - the SPI belongs to the apps RSC on this
 * board and must never fall through to the generic GIC path - but it handles
 * nothing, which is what a stopped or not-yet-started owner must see.
 */
#ifndef SWEET_APPS_DOOR_H
#define SWEET_APPS_DOOR_H
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOTypes.h>

/* INTID37 is the apps RSC SPI on every SM8150 tree this port boots (LG DTS
 * SPI5). It is a constant here because it is the one number the router and
 * both owners must agree on before either has read the ADT. */
enum : uint32_t { kSweetAppsDoorIntid = 37 };

/* Who the boot chose. Decided from the tree alone, before either driver's
 * first register access, and never revised at run time: there is no handover
 * in this card, only a different image or a different tree. */
enum SweetAppsOwner {
	SweetAppsOwnerNone = 0, /* neither node described an owner: nothing writes */
	SweetAppsOwnerService,  /* the display service owns the stores and the line */
	SweetAppsOwnerChannel,  /* the RSC channel does */
};

/* One owner's table. Every entry may be NULL only where the caller checks for
 * it; both shipped owners fill every field. */
struct SweetAppsDoor {
	const char *name; /* the ring row's owner field, never resolved at run time */
	void *context;
	uint32_t (*read)(void *, uint32_t offset);
	void (*write)(void *, uint32_t offset, uint32_t value);
	bool (*identity)(void *, uint32_t *id, uint32_t *solver, uint32_t *config);
	IOReturn (*triggerReady)(void *);
	void (*arm)(void *);
	void (*unmask)(void *);
	void (*quiet)(void *, uint32_t bit);
	void (*stop)(void *);
	bool (*irq)(void *);
	bool (*install)(void *, void *target, bool (*callback)(void *));
	unsigned (*stores)(void *);
	unsigned (*irqs)(void *);
};

/* The tree rule, in one place: a delegated node makes the channel the owner and
 * contradicts the service's IRQ description; an IRQ description with no
 * delegation keeps the service; a tree that says both is not resolved to
 * whichever driver asked first, it is refused (None) so both sides fail their
 * start instead of racing for the line. */
SweetAppsOwner sweet_apps_owner(bool delegated, bool irqServices);

/* Publish, retire and look up the owner. publish() returns false when another
 * table is already published; withdraw() only removes the caller's own. */
bool sweet_apps_door_publish(const SweetAppsDoor *door);
void sweet_apps_door_withdraw(const SweetAppsDoor *door);
const SweetAppsDoor *sweet_apps_door(void);

/* The platform claim symbol. Defined here, not in either owner. */
extern "C" bool sweet_rpmh_irq(unsigned int intid);

#endif /* ARM64_BOARD_CONFIG_MH2LM */
#endif /* SWEET_APPS_DOOR_H */
