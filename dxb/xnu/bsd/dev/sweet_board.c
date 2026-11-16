/* Board-owned late initialization. Called once from bsd_init, after
 * bsd_autoconf/IOBSD and inittodr, before root lookup/mount. Do not call
 * this from a leaf driver or move GIC probing into early IOKit matching. */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <pexpert/arm64/sweet_gic.h>
#include "sweet_spmi_mux.h"
#include "sweet_log_level.h"

void sweet_board_init(void);
extern void sweet_keys_init(void);
extern void sweet_touch_init(void);
extern void sweet_usb_probe(void);
extern void sweet_sample_start(void);
#if defined(ARM64_BOARD_CONFIG_MH2LM)
extern void sweet_display_probe_init(void);
extern int sweet_adc5_init(void);
#endif
#if defined(ARM64_BOARD_CONFIG_GUACAMOLE)
extern void sweet_gpu_init(void);
#endif

void
sweet_board_init(void)
{
	static bool started;
	/* Single caller on the BSD startup thread, outside the root retry loop. */
	if (started) {
		return;
	}
	started = true;
	SWEET_LOG_INFO("SWEET BOARD: late init before root lookup; independent of UFS\n");
	/*
	 * Take the distributor before any driver publishes an SPI. Devices that
	 * have not migrated yet keep dispatching through the legacy chain in
	 * sleh_irq: the controller sits behind it, so installing it here does
	 * not disturb them.
	 */
	if (sweet_gic_install() < 0) {
		SWEET_LOG_ERROR("SWEET BOARD: GIC controller unavailable; legacy dispatch retained\n");
	}
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	/* Publish the late nub and synchronously match its mux IOService.
	 * Consumers own only their routes; sweetkeys=0 does not suppress it. */
	if (sweet_spmi_mux_start() != 0) {
		SWEET_LOG_ERROR("SWEET BOARD: shared PMIC mux unavailable; continuing independent devices\n");
	}
	if (sweet_adc5_init() != 0) {
		SWEET_LOG_ERROR("SWEET BOARD: ADC5 provider unavailable; continuing independent devices\n");
	}
#endif
	sweet_keys_init();
	sweet_touch_init();
	sweet_usb_probe();
	sweet_sample_start();
#if defined(ARM64_BOARD_CONFIG_GUACAMOLE)
	sweet_gpu_init();
#endif
#if defined(ARM64_BOARD_CONFIG_MH2LM)
	sweet_display_probe_init();
#endif
	SWEET_LOG_INFO("SWEET BOARD: device init dispatched; storage remains root-owned\n");
	/* Live SPI inventory for the staged interrupt migration. */
	sweet_gic_log_enabled("board-init");
}
#endif /* ARM64_BOARD_CONFIG_SWEET */
