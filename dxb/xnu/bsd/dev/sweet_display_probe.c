/*
 * mh2lm display-controller probe: reads and prints the state UEFI left the
 * SM8150 MDSS/DSI0 in (scanout source, tear check, autorefresh, DSI command
 * engine) so DCS brightness and TE-synced flips can be written against facts.
 * The probe writes nothing: clocks are checked first (a read of a gated block
 * would hang the bus) and only plain status/config registers are read, never
 * an interrupt-status or FIFO register with side effects.
 *
 * Block addresses from the mh2lm Android dtb (mdss_mdp@ae00000: sspp VIG0
 * +0x5000, ctl0 +0x2000, intf "dp","dsi","dsi","dp" at +0x6b000/+0x6b800/
 * +0x6c000/+0x6c800; dsi_ctrl0@ae94000; dispcc@af00000; gcc@100000).
 * Register meanings follow the HakiOS Redmi K20 Pro port (SM8150, same
 * 1080x2340 command-mode OLED arrangement; kernel/bsd/dev/hakios.adreno.c
 * raphael_display_*, hakios.raphael-dsi.h, APSL 2.0), itself from
 * LineageOS android_kernel_xiaomi_sm8150 sde_hw_intf.c / dsi_ctrl_hw_cmn.c.
 *
 * Boot-arg sweetdispprobe=0 disables the probe.
 *
 * sweet_display_brightness_set() is the only writer: one DCS 0x51 (12-bit
 * level) through DSI0's embedded command FIFO, for AppleARMBacklight
 * (SweetFramebuffer.cpp). It touches the INTF1 tear-check/autorefresh and
 * DSI0 command registers and restores them; 0110 (sweetdcs test) showed it
 * works on mh2lm. After three failed writes in a row it stops for good.
 */
#include <pexpert/arm64/sweet_dev.h>

#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/locks.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include "sweet_log_level.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern kern_return_t thread_terminate(thread_t);
void sweet_display_probe_init(void);
int sweet_display_brightness_ready(void);
int sweet_display_brightness_set(uint32_t level);

#define GCC_PA          0x00100000u
#define GCC_DISP_PAGE   0xb000u
#define GCC_DISP_AHB    0x00cu          /* gcc_disp_ahb_clk CBCR at 0xb00c */
#define DISPCC_PA       0x0af00000u
#define DISPCC_AHB_CBCR 0x2080u         /* the K20 Pro code's MDSS access check */
#define CBCR_ENABLE     (1u << 0)
#define CBCR_OFF        (1u << 31)
#define MDP_PA          0x0ae00000u
#define DSI0_PA         0x0ae94000u
#define SMMU_PA         0x15000000u
#define SMMU_SID        0x0800u

static int dp_chan;
static uint32_t dp_last_frame[2];
static uint64_t dp_last_ns[2];
static vm_offset_t dp_smmu_gr1, dp_smmu_cb2;

/* Register windows, mapped once; dp_lock serialises every DSI write. */
static LCK_GRP_DECLARE(dp_grp, "sweet_disp");
static LCK_MTX_DECLARE(dp_lock, &dp_grp);
static int dp_mapped;           /* 0 not yet, 1 mapped, -1 failed */
static int dp_failed;           /* consecutive failed DCS writes */
static unsigned dp_logged;
static vm_offset_t dp_gcc, dp_dispcc, dp_sspp, dp_ctl, dp_intf1, dp_intf2, dp_dsi, dp_smmu;

/* Ownership is exclusive for the whole boot, including failure paths. */
static int
dp_service_enabled(void)
{
 int enabled=0;
 (void)PE_parse_boot_argn("sweetmdss",&enabled,sizeof(enabled));
 return enabled!=0;
}

/* Caller holds dp_lock. */
static int
dp_map(void)
{
	if (dp_service_enabled()) { return -1; }
	if (dp_mapped == 0) {
		dp_gcc = ml_io_map(GCC_PA + GCC_DISP_PAGE, 0x1000);
		dp_dispcc = ml_io_map(DISPCC_PA, 0x3000);
		dp_sspp = ml_io_map(MDP_PA + 0x5000u, 0x1000);
		dp_ctl = ml_io_map(MDP_PA + 0x2000u, 0x1000);
		dp_intf1 = ml_io_map(MDP_PA + 0x6b800u, 0x800);
		dp_intf2 = ml_io_map(MDP_PA + 0x6c000u, 0x800);
		dp_dsi = ml_io_map(DSI0_PA, 0x400);
		dp_smmu = ml_io_map(SMMU_PA, 0x2000);
		/* SMMU is optional evidence: a mapping failure must not disable display DCS. */
		dp_mapped = (dp_gcc && dp_dispcc && dp_sspp && dp_ctl && dp_intf1 && dp_intf2 &&
		    dp_dsi) ? 1 : -1;
		if (dp_mapped < 0) {
			printf("SWEET DISP: mapping failed\n");
		}
	}
	return dp_mapped > 0 ? 0 : -1;
}

static uint32_t
dp_rd(vm_offset_t base, uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}

static void
dp_wait(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&dp_chan, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

static void
dp_dump(const char *what, vm_offset_t base, uint32_t from, uint32_t to)
{
	for (uint32_t o = from; o < to; o += 0x20) {
		printf("SWEET DISP: %s +%03x: %08x %08x %08x %08x %08x %08x %08x %08x\n", what, o,
		    dp_rd(base, o), dp_rd(base, o + 4), dp_rd(base, o + 8), dp_rd(base, o + 12),
		    dp_rd(base, o + 16), dp_rd(base, o + 20), dp_rd(base, o + 24), dp_rd(base, o + 28));
	}
}

/* LG arm-smmu.c: ID1=0x60000038 => 4K pages, GR half 0x80000,
 * whole resource 0x100000 (also apps_smmu DT resource). Only known CB2 config;
 * never dereference TTBR or read TLB/fault/command registers. Unknown IDs stop. */
static void
dp_smmu_context(uint32_t id0, uint32_t id1, uint32_t s2cr)
{
	int trace = 0;
	(void)PE_parse_boot_argn("sweetfbtrace", &trace, sizeof(trace));
	if (trace < 2 || !sweet_log_level_enabled(2u)) { return; }
	if (id0 != 0x4c017e5eu || id1 != 0x60000038u ||
	    ((s2cr >> 16) & 3u) != 0 || (s2cr & 0xffu) != 2) {
		SWEET_LOG_WARN("SWEET DISP: F0 v2 unknown SMMU layout/route, skip CB config\n");
		return;
	}
	/* dp_smmu already maps both GR0 and GR1 pages. */
	dp_smmu_gr1 = dp_smmu + 0x1000u;
	if (!dp_smmu_cb2) { dp_smmu_cb2 = ml_io_map(SMMU_PA + 0x82000u, 0x1000); }
	if (!dp_smmu_gr1 || !dp_smmu_cb2) {
		SWEET_LOG_WARN("SWEET DISP: F0 v2 optional CB2 mapping unavailable\n");
		return;
	}
	vm_offset_t cb = dp_smmu_cb2;
	SWEET_LOG_INFO("SWEET DISP: F0 v2 cb2 pa=0x15082000 cbar=%#x cba2r=%#x sctlr=%#x actlr=%#x ttbcr2=%#x ttbcr=%#x ttbr0=%08x:%08x ttbr1=%08x:%08x mair=%08x:%08x\n",
	    dp_rd(dp_smmu_gr1, 2u * 4u), dp_rd(dp_smmu_gr1, 0x800u + 2u * 4u),
	    dp_rd(cb, 0x00), dp_rd(cb, 0x04), dp_rd(cb, 0x10), dp_rd(cb, 0x30),
	    dp_rd(cb, 0x24), dp_rd(cb, 0x20), dp_rd(cb, 0x2c), dp_rd(cb, 0x28),
	    dp_rd(cb, 0x3c), dp_rd(cb, 0x38));
}

static void
dp_probe(unsigned pass, vm_offset_t gcc, vm_offset_t dispcc, vm_offset_t sspp, vm_offset_t ctl,
    vm_offset_t intf1, vm_offset_t intf2, vm_offset_t dsi, vm_offset_t smmu)
{
	/*
	 * Same gate as the K20 Pro code (raphael_display_init), without its enable
	 * writes: gcc_disp_ahb only needs CLK_ENABLE (it is hardware-gated, so
	 * CLK_OFF reads 1 while the bus is idle: 0x80000003 on mh2lm, 0108 run 1),
	 * and dispcc +0x2080 must report CLK_OFF clear.
	 */
	uint32_t ahb = dp_rd(gcc, GCC_DISP_AHB);
	printf("SWEET DISP: pass %u gcc disp_ahb %#x\n", pass, ahb);
	if (!(ahb & CBCR_ENABLE)) {
		printf("SWEET DISP: gcc disp_ahb not enabled: no DISPCC/MDSS read\n");
		return;
	}
	uint32_t mahb = dp_rd(dispcc, DISPCC_AHB_CBCR);
	printf("SWEET DISP: dispcc +0x2080 %#x\n", mahb);
	if (mahb & CBCR_OFF) {
		printf("SWEET DISP: dispcc +0x2080 clock off: no MDSS read\n");
		return;
	}
	/* VIG0 source: read-only geometry, format and address latch. */
	printf("SWEET DISP: vig0 size %#x xy %#x out %#x addr %#x stride %#x fmt %#x unpack %#x op %#x addr-status %#x\n",
	    dp_rd(sspp, 0x00), dp_rd(sspp, 0x08), dp_rd(sspp, 0x0c), dp_rd(sspp, 0x14),
	    dp_rd(sspp, 0x24), dp_rd(sspp, 0x30), dp_rd(sspp, 0x34), dp_rd(sspp, 0x38),
	    dp_rd(sspp, 0x70));
	printf("SWEET DISP: vig0 expect size %#x addr %#x stride %#x\n",
	    (uint32_t)(SWEET_DEV_FB_H << 16 | SWEET_DEV_FB_W), (uint32_t)SWEET_DEV_FB_PA,
	    (uint32_t)SWEET_DEV_FB_STRIDE);
	printf("SWEET DISP: ctl0 flush %#x start %#x\n", dp_rd(ctl, 0x18), dp_rd(ctl, 0x1c));
	vm_offset_t intf[2] = {intf1, intf2};
	for (unsigned i = 0; i < 2; i++) {
		vm_offset_t p = intf[i];
		printf("SWEET DISP: intf%u te-en %#x cfg %#x height %#x init %#x count %#x line %#x auto %#x frame %#x\n",
		    i + 1, dp_rd(p, 0x284), dp_rd(p, 0x288), dp_rd(p, 0x28c), dp_rd(p, 0x294),
		    dp_rd(p, 0x298), dp_rd(p, 0x2b0), dp_rd(p, 0x2b4), dp_rd(p, 0xac));
		uint32_t frame = dp_rd(p, 0xac), line = dp_rd(p, 0x2b0);
		uint64_t ns;
		absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
		if (dp_last_ns[i] != 0) {
			SWEET_LOG_INFO("SWEET DISP: F0 v2 intf%u elapsed-ns=%llu frame-delta=%u tear-line-phase=%#x frame-en=%#x raster-line=%#x command-mode-not-TE-rate\n",
			    i + 1, ns - dp_last_ns[i], frame - dp_last_frame[i], line, dp_rd(p, 0xa8), dp_rd(p, 0xb0));
		}
		dp_last_frame[i] = frame; dp_last_ns[i] = ns;
	}
	/* Read-only ARM SMMU v2 stream matching. Never write or probe a gated status FIFO. */
	if (smmu != 0) {
		uint32_t id0 = dp_rd(smmu, 0x20), id1 = dp_rd(smmu, 0x24);
		uint32_t nsmr = id0 & 0xffu;
		printf("SWEET DISP: smmu id0 %#x id1 %#x num-smr=%u sid=%#x\n", id0, id1, nsmr, SMMU_SID);
		for (uint32_t i = 0; i < nsmr && i < 256; i++) {
			uint32_t smr = dp_rd(smmu, 0x800u + i * 4u);
			uint32_t smask = (smr >> 16) & 0x7fffu;
			if ((smr & 0x80000000u) && ((smr & 0x7fffu & ~smask) == (SMMU_SID & ~smask))) {
				uint32_t s2cr = dp_rd(smmu, 0xc00u + i * 4u);
				printf("SWEET DISP: smmu match idx=%u smr=%#x s2cr=%#x type=%u cb=%u\n",
				    i, smr, s2cr, (s2cr >> 16) & 3u, s2cr & 0xffu);
				if (pass == 0) { dp_smmu_context(id0, id1, s2cr); }
			}
		}
	} else {
		printf("SWEET DISP: smmu mapping unavailable\n");
	}
	/* DSI0: +0x004 ctrl (mode bits), +0x008 status, +0x03c cmd DMA ctrl, +0x04c length,
	 * +0x084 trigger, +0x15c embedded cmd FIFO ctrl (the K20 Pro code reads these). */
	printf("SWEET DISP: dsi0 hw %#x ctrl %#x status %#x dma %#x len %#x trig %#x fifo %#x\n",
	    dp_rd(dsi, 0x000), dp_rd(dsi, 0x004), dp_rd(dsi, 0x008), dp_rd(dsi, 0x03c),
	    dp_rd(dsi, 0x04c), dp_rd(dsi, 0x084), dp_rd(dsi, 0x15c));
	if (pass == 0) {
		dp_dump("dsi0", dsi, 0x000, 0x0a0);
		dp_dump("intf1", intf1, 0x280, 0x2b8);
		dp_dump("intf2", intf2, 0x280, 0x2b8);
	}
}

/* ---- DCS 0x51 brightness (AppleARMBacklight is the only caller) ---- */

static void
dp_wr(vm_offset_t base, uint32_t off, uint32_t v)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(base + off) = v;
	__asm__ volatile("dsb sy" ::: "memory");
}

/* Poll base+off until (value & mask) == want, at most ms milliseconds. */
static int
dp_poll(vm_offset_t base, uint32_t off, uint32_t mask, uint32_t want, unsigned ms)
{
	for (unsigned us = 0; us < ms * 1000u; us += 50) {
		if ((dp_rd(base, off) & mask) == want) {
			return 0;
		}
		delay(50);
	}
	return -1;
}

/*
 * One DCS long write through DSI0's embedded command FIFO, in the order of the
 * K20 Pro dsi_packet()/raphael_display_command_begin() (from Linux
 * sde_encoder_phys_cmd_prepare_commit and dsi_ctrl_hw_cmn.c): stop TE kickoffs
 * and autorefresh, wait for the frame in flight and DSI idle, send, wait for
 * CMD_DMA_DONE, clean up, restore every register touched.
 */
static int
dp_dcs_write(vm_offset_t intf, vm_offset_t dsi, const uint32_t *words, unsigned count)
{
	if ((dp_rd(dsi, 0x004) & 7u) != 5u || dp_rd(dsi, 0x15c) != 0 || (dp_rd(dsi, 0x110) & (1u << 24))) {
		printf("SWEET DISP: dcs: DSI not in the expected state (ctrl %#x fifo %#x irq %#x)\n",
		    dp_rd(dsi, 0x004), dp_rd(dsi, 0x15c), dp_rd(dsi, 0x110));
		return -1;
	}
	uint32_t sync = dp_rd(intf, 0x288), refresh = dp_rd(intf, 0x2b4);
	dp_wr(intf, 0x288, sync & ~(1u << 20));         /* no new TE kickoff */
	dp_wr(intf, 0x2b4, 0);                          /* autorefresh off */
	int rc = 0;
	/* Let the frame in flight finish: line counter back at 0 or past the last line. */
	unsigned us;
	for (us = 0; us < 100000u; us += 50) {
		uint32_t line = dp_rd(intf, 0x2b0) & 0xffffu;
		if (!line || line >= SWEET_DEV_FB_H) {
			break;
		}
		delay(50);
	}
	if (us >= 100000u || dp_poll(dsi, 0x008, (1u << 2) | (1u << 1), 0, 200)) {
		printf("SWEET DISP: dcs: DSI did not go idle (status %#x line %#x)\n",
		    dp_rd(dsi, 0x008), dp_rd(intf, 0x2b0));
		rc = -2;
		goto restore;
	}
	uint32_t dma = dp_rd(dsi, 0x03c), length = dp_rd(dsi, 0x04c), trigger = dp_rd(dsi, 0x084);
	const uint32_t statuses = 0x51115501u;          /* W1C only the DMA/BTA status bits */
	dp_wr(dsi, 0x110, (dp_rd(dsi, 0x110) & ~statuses) | 1u | (1u << 20));
	dp_wr(dsi, 0x084, (trigger & ~7u) | 4u);        /* software DMA trigger */
	dp_wr(dsi, 0x15c, (1u << 1) | (1u << 2) | (3u << 16));
	for (unsigned i = 0; i < count; i++) {
		dp_wr(dsi, 0x17c, words[i]);
	}
	if (count & 1u) {
		dp_wr(dsi, 0x17c, 0);
	}
	dp_wr(dsi, 0x03c, (dma & ~((1u << 31) | (1u << 30))) | (1u << 28) | (1u << 26));
	dp_wr(dsi, 0x04c, count * 4u);
	dp_wr(dsi, 0x090, 1);                           /* CMD_DMA trigger */
	if (dp_poll(dsi, 0x110, 1u, 1u, 200)) {
		rc = -3;
	} else if (dp_rd(dsi, 0x110) & (1u << 24)) {
		rc = -4;
	}
	uint32_t irq = dp_rd(dsi, 0x110), status = dp_rd(dsi, 0x008), ack = dp_rd(dsi, 0x068);
	dp_wr(dsi, 0x110, (dp_rd(dsi, 0x110) & ~statuses) | 1u | (1u << 20));
	dp_wr(dsi, 0x15c, 0);
	dp_wr(dsi, 0x1ec, 1);                           /* FIFO reset, as the native timeout path */
	delay(1);
	dp_wr(dsi, 0x1ec, 0);
	dp_wr(dsi, 0x04c, length);
	dp_wr(dsi, 0x03c, dma);
	dp_wr(dsi, 0x084, trigger);
	if (rc != 0 || dp_logged < 8) {
		printf("SWEET DISP: dcs: rc %d irq %#x status %#x ack %#x\n", rc, irq, status, ack);
	}
restore:
	dp_wr(intf, 0x288, sync);
	dp_wr(intf, 0x2b4, refresh);
	return rc;
}

/* Caller holds dp_lock and has mapped the registers. */
static int
dp_clocks_on(void)
{
	return (dp_rd(dp_gcc, GCC_DISP_AHB) & CBCR_ENABLE) &&
	       !(dp_rd(dp_dispcc, DISPCC_AHB_CBCR) & CBCR_OFF);
}

/* 0 when brightness can be written: registers mapped, MDSS clocked, no failures yet. */
int
sweet_display_brightness_ready(void)
{
	lck_mtx_lock(&dp_lock);
	int rc = (dp_map() == 0 && dp_failed < 3 && dp_clocks_on()) ? 0 : -1;
	lck_mtx_unlock(&dp_lock);
	return rc;
}

/*
 * DCS 0x51, 12-bit big-endian (LG DSDT: BacklightBitWidth 12, BacklightDCSEndian
 * TRUE). level 1..0xfff; 0 is raised to 1 (never display-off brightness).
 * Blocks for up to ~0.3 s: call it from a thread, never from an interrupt.
 */
int
sweet_display_brightness_set(uint32_t level)
{
	if (level == 0) {
		level = 1;
	} else if (level > 0xfffu) {
		level = 0xfffu;
	}
	/* Embedded header {wc lo, wc hi, type 0x39 | VC 0, flags 0xc0}, then 51 hi lo. */
	const uint32_t words[2] = {0xc0390003u, 0xff000051u | ((level >> 8) << 8) | ((level & 0xffu) << 16)};
	int rc;

	lck_mtx_lock(&dp_lock);
	if (dp_map() != 0 || dp_failed >= 3) {
		rc = -1;
	} else if (!dp_clocks_on()) {
		printf("SWEET DISP: dcs skipped: MDSS clock gate not met\n");
		rc = -1;
	} else {
		if (dp_logged < 8) {
			printf("SWEET DISP: dcs 51 %02x %02x\n", level >> 8, level & 0xffu);
		}
		rc = dp_dcs_write(dp_intf1, dp_dsi, words, 2);
		dp_logged++;
		if (rc != 0 && ++dp_failed >= 3) {
			printf("SWEET DISP: dcs failed %d times: brightness writes disabled\n", dp_failed);
		} else if (rc == 0) {
			dp_failed = 0;
		}
	}
	lck_mtx_unlock(&dp_lock);
	return rc;
}

static void
dp_thread(void *arg __unused, wait_result_t wr __unused)
{
	dp_wait(30000);
	lck_mtx_lock(&dp_lock);
	if (dp_map() == 0) {
		for (unsigned pass = 0; pass < 3; pass++) {
			dp_probe(pass, dp_gcc, dp_dispcc, dp_sspp, dp_ctl, dp_intf1, dp_intf2,
			    dp_dsi, dp_smmu);
			if (pass < 2) {
				lck_mtx_unlock(&dp_lock);
				dp_wait(1000);
				lck_mtx_lock(&dp_lock);
			}
		}
	}
	lck_mtx_unlock(&dp_lock);
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

void
sweet_display_probe_init(void)
{
	static bool started;
	int enabled = 1;
	thread_t thread;

	if (dp_service_enabled()) {
		SWEET_LOG_INFO("SWEET DISP: legacy probe disabled; SweetMDSS owns hardware\n");
		return;
	}
	if (started) {
		return;
	}
	started = true;
	(void)PE_parse_boot_argn("sweetdispprobe", &enabled, sizeof(enabled));
	if (!enabled) {
		return;
	}
	if (kernel_thread_start(dp_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}
#endif /* ARM64_BOARD_CONFIG_MH2LM */
