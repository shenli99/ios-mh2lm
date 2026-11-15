/*
 * SWEET: USB bring-up, step 1 - read-only state probe.
 *
 * The phone's USB is a Synopsys DWC3 (USB 2.0 only, maximum-speed
 * high-speed) at 0x0a600000 behind Qualcomm's dwc-usb3-msm wrapper, with a
 * QUSB2 PHY at 0x088e2000; its power domain is the USB30_PRIM GDSC and its
 * clocks come from GCC (offsets from Linux gcc-sdmmagpie.c). ABL uses this
 * controller for fastboot; this probe only reports what state it is left
 * in after `fastboot boot`. It never writes a register, and it touches the
 * DWC3 / PHY register blocks only when the GCC status registers say their
 * power domain and clocks are on (an access to a gated block would hang).
 */
/* The guard below uses SWEET_DEV_USB: the header must come first. */
#include <pexpert/arm64/sweet_dev.h>

#if defined(ARM64_BOARD_CONFIG_SWEET) && SWEET_DEV_USB
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <arm/caches_internal.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm64/sweet_dev.h>
#include "sweet_log_level.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern kern_return_t thread_terminate(thread_t);
void sweet_usb_probe(void);
static void sweet_usb_gadget_start(vm_offset_t dwc);
static void ud_bringup_thread(void *arg, wait_result_t wr);
extern ppnum_t pmap_find_phys(pmap_t pmap, addr64_t va);

/*
 * Qualcomm wrapper registers ("QSCRATCH", core base + 0xf8800). The
 * controller only connects (pull-up on D+) while it sees a valid VBUS
 * session; Linux dwc3-qcom forces that in software when the charger
 * reports a cable (dwc3_qcom_vbus_override_enable).
 */
#define QSCRATCH_PA             SWEET_DEV_USB_QSCRATCH_PA
#define QSCRATCH_HS_PHY_CTRL    0x10
#define   UTMI_OTG_VBUS_VALID   (1u << 20)
#define   SW_SESSVLD_SEL        (1u << 28)
#define QSCRATCH_SS_PHY_CTRL    0x30
#define   LANE0_PWR_PRESENT     (1u << 24)

#define GCC_PA                  SWEET_DEV_USB_GCC_PA
#define GCC_SIZE                0x00090000
#define GCC_USB30_PRIM_GDSCR    SWEET_DEV_USB_GDSCR	/* bit 31: power on */
#define CBCR_CLK_OFF            (1u << 31)

static const struct {
	const char *name;
	uint32_t    off;
} usb_cbcr[] = {
	{ "master", SWEET_DEV_USB_CBCR_MASTER },
	{ "sleep", SWEET_DEV_USB_CBCR_SLEEP },
	{ "utmi", SWEET_DEV_USB_CBCR_UTMI },
	{ "cfg-noc-axi", SWEET_DEV_USB_CBCR_CFGNOC },
	{ "aggre-axi", SWEET_DEV_USB_CBCR_AGGRE },
	{ SWEET_DEV_USB_CBCR_AUX_NAME, SWEET_DEV_USB_CBCR_AUX },
	{ "clkref", SWEET_DEV_USB_CBCR_CLKREF },
};

#define DWC3_PA                 SWEET_DEV_USB_DWC3_PA
#define DWC3_SIZE               0x0000d000
#define QUSB2_PA                SWEET_DEV_USB_QUSB2_PA
#define QUSB2_SIZE              0x00000400

static uint32_t
usb_rd(vm_offset_t base, uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);
	__asm__ volatile ("dmb oshld" ::: "memory");
	return v;
}

/*
 * Progress markers for the gadget bring-up survive a reset in DRAM: warm
 * resets keep DRAM contents and the bootloader does not touch this address
 * (0x18000000 into the kernel bank, far above the kernel image, ramdisk,
 * ADT and boot_args). The shim prints the same word before jumping.
 */
#define SWEET_USB_IMEM_PA    SWEET_DEV_MARK_PA
#define SWEET_USB_IMEM_MARK  0
#define SWEET_USB_MARK_GADGET   0x100u
#define SWEET_USB_MARK_DALE     0x110u
#define SWEET_USB_MARK_DALE_OK  0x111u
#define SWEET_USB_MARK_TRB_OK   0x112u
#define SWEET_USB_MARK_STEP4_OK 0x113u
#define SWEET_USB_MARK_TRB_WRITE 0x114u
#define SWEET_USB_MARK_TRB_SYNC  0x115u
#define SWEET_USB_MARK_TRB_CMD   0x116u
#define SWEET_USB_MARK_TRB_WAIT  0x117u
#define SWEET_USB_MARK_STEP5    0x120u
#define SWEET_USB_MARK_STEP5_OK 0x121u
#define SWEET_USB_MARK_RUN      0x130u
#define SWEET_USB_MARK_RUN_OK   0x131u

static vm_offset_t usb_imem;

static uint32_t
usb_mark_read(void)
{
	/* MH2LM has no verified reset-surviving scratch address yet. */
	if (SWEET_USB_IMEM_PA == 0) {
		return 0;
	}
	if (usb_imem == 0) {
		usb_imem = ml_io_map(SWEET_USB_IMEM_PA, 0x1000);
	}
	return usb_imem ? *(volatile uint32_t *)(usb_imem + SWEET_USB_IMEM_MARK) : 0;
}

static void
usb_mark(uint32_t v)
{
	if (SWEET_USB_IMEM_PA == 0) {
		return;
	}
	(void)usb_mark_read();
	if (usb_imem) {
		*(volatile uint32_t *)(usb_imem + SWEET_USB_IMEM_MARK) = v;
		__asm__ volatile ("dsb sy" ::: "memory");
	}
}

static const char *
usb_mark_name(uint32_t v)
{
	switch (v) {
	case 0: return "none";
	case SWEET_USB_MARK_GADGET: return "gadget entered";
	case SWEET_USB_MARK_DALE: return "before DALEPENA";
	case SWEET_USB_MARK_DALE_OK: return "DALEPENA written";
	case SWEET_USB_MARK_TRB_OK: return "ep0 TRB written (DEPCMD next)";
	case SWEET_USB_MARK_STEP4_OK: return "step 4 done";
	case SWEET_USB_MARK_TRB_WRITE: return "TRB stores";
	case SWEET_USB_MARK_TRB_SYNC: return "TRB cache sync";
	case SWEET_USB_MARK_TRB_CMD: return "TRB DEPCMD STARTTRANSFER";
	case SWEET_USB_MARK_TRB_WAIT: return "TRB command wait";
	case SWEET_USB_MARK_STEP5: return "before QSCRATCH";
	case SWEET_USB_MARK_STEP5_OK: return "QSCRATCH done";
	case SWEET_USB_MARK_RUN: return "before RUN_STOP";
	case SWEET_USB_MARK_RUN_OK: return "RUN_STOP written";
	default: return "unknown";
	}
}
void
sweet_usb_probe(void)
{
	int enabled = 1;
	uint32_t gdscr, cbcr[sizeof(usb_cbcr) / sizeof(usb_cbcr[0])];
	bool clocks_on = true;

	/* sweetusb=0 off, 1 (default) report state only, 2 also start the USB device */
	PE_parse_boot_argn("sweetusb", &enabled, sizeof(enabled));

	/* What the previous (possibly EDL'd) boot left in IMEM. */
	uint32_t prev_mark = usb_mark_read();
	SWEET_LOG_DEBUG("SWEET USB: previous run marker 0x%08x (%s)\n", prev_mark, usb_mark_name(prev_mark));
	usb_mark(0x1);
	if (!enabled) {
		return;
	}
	vm_offset_t gcc = ml_io_map(GCC_PA, GCC_SIZE);
	if (gcc == 0) {
		SWEET_LOG_ERROR("SWEET USB: GCC map failed\n");
		return;
	}
	gdscr = usb_rd(gcc, GCC_USB30_PRIM_GDSCR);
	SWEET_LOG_DEBUG("SWEET USB: GDSC usb30_prim 0x%08x (%s)\n", gdscr, (gdscr & (1u << 31)) ? "on" : "off");
	for (unsigned i = 0; i < sizeof(usb_cbcr) / sizeof(usb_cbcr[0]); i++) {
		cbcr[i] = usb_rd(gcc, usb_cbcr[i].off);
		SWEET_LOG_DEBUG("SWEET USB: clk %-12s CBCR 0x%08x (%s)\n", usb_cbcr[i].name, cbcr[i],
		    (cbcr[i] & CBCR_CLK_OFF) ? "off" : "on");
	}
	/* DWC3 registers need the GDSC, the master clock and the config-NoC path. */
	clocks_on = (gdscr & (1u << 31)) && !(cbcr[0] & CBCR_CLK_OFF) && !(cbcr[3] & CBCR_CLK_OFF);
	if (!clocks_on) {
		SWEET_LOG_WARN("SWEET USB: controller unpowered or unclocked; DWC3 not read\n");
		return;
	}
	vm_offset_t dwc = ml_io_map(DWC3_PA, DWC3_SIZE);
	if (dwc == 0) {
		SWEET_LOG_ERROR("SWEET USB: DWC3 map failed\n");
		return;
	}
	SWEET_LOG_DEBUG("SWEET USB: DWC3 GSNPSID 0x%08x GCTL 0x%08x GSTS 0x%08x GUSB2PHYCFG 0x%08x\n",
	    usb_rd(dwc, 0xc120), usb_rd(dwc, 0xc110), usb_rd(dwc, 0xc118), usb_rd(dwc, 0xc200));
	SWEET_LOG_DEBUG("SWEET USB: DWC3 DCFG 0x%08x DCTL 0x%08x DEVTEN 0x%08x DSTS 0x%08x DALEPENA 0x%08x\n",
	    usb_rd(dwc, 0xc700), usb_rd(dwc, 0xc704), usb_rd(dwc, 0xc708), usb_rd(dwc, 0xc70c),
	    usb_rd(dwc, 0xc720));
	SWEET_LOG_DEBUG("SWEET USB: DWC3 GEVNTADR 0x%08x%08x GEVNTSIZ 0x%08x GEVNTCOUNT 0x%08x GHWPARAMS0 0x%08x\n",
	    usb_rd(dwc, 0xc404), usb_rd(dwc, 0xc400), usb_rd(dwc, 0xc408), usb_rd(dwc, 0xc40c),
	    usb_rd(dwc, 0xc140));
	/* GHWPARAMS0 encodes the bus widths: mode[1:0], MDWIDTH[15:8],
	 * SDWIDTH[23:16], AWIDTH[31:24]. */
	SWEET_LOG_DEBUG("SWEET USB: GHWPARAMS0 mode=%u mwidth=%u swidth=%u awidth=%u\n",
	    usb_rd(dwc, 0xc140) & 0x3, (usb_rd(dwc, 0xc140) >> 8) & 0xff,
	    (usb_rd(dwc, 0xc140) >> 16) & 0xff, (usb_rd(dwc, 0xc140) >> 24) & 0xff);
	if (enabled >= 2) {
		/*
		 * The bring-up runs on its own thread: a controller that stalls
		 * must never delay or block the boot. The requirement for this
		 * board is that iOS comes up whatever USB does.
		 */
		SWEET_LOG_INFO("SWEET USB: gadget on its own thread, deferred\n");
		thread_t t;
		if (kernel_thread_start(ud_bringup_thread, (void *)dwc, &t) == KERN_SUCCESS) {
			thread_deallocate(t);
		}
		return;
	}
	if (cbcr[5] & CBCR_CLK_OFF) {
		SWEET_LOG_WARN("SWEET USB: ahb2phy clock off; QUSB2 PHY not read\n");
		return;
	}
	vm_offset_t phy = ml_io_map(QUSB2_PA, QUSB2_SIZE);
	if (phy == 0) {
		return;
	}
	/* SM8150's HS PHY is the 7nm femto v2 (mainline phy-qcom-snps-femto-v2.c);
	 * its registers live below 0xa4, unlike SM6150's qusb2-v2 layout. */
	SWEET_LOG_DEBUG("SWEET USB: PHY UTMI_CTRL0 0x%08x UTMI_CTRL1 0x%08x UTMI_CTRL5(POR) 0x%08x\n",
	    usb_rd(phy, 0x3c), usb_rd(phy, 0x40), usb_rd(phy, 0x50));
	SWEET_LOG_DEBUG("SWEET USB: PHY COMMON0 0x%08x COMMON1 0x%08x COMMON2 0x%08x CTRL1 0x%08x CTRL2 0x%08x\n",
	    usb_rd(phy, 0x54), usb_rd(phy, 0x58), usb_rd(phy, 0x5c), usb_rd(phy, 0x60), usb_rd(phy, 0x64));
	SWEET_LOG_DEBUG("SWEET USB: PHY CFG0 0x%08x REFCLK_CTRL 0x%08x OVR 0x%08x/0x%08x/0x%08x/0x%08x\n",
	    usb_rd(phy, 0x94), usb_rd(phy, 0xa0), usb_rd(phy, 0x6c), usb_rd(phy, 0x70),
	    usb_rd(phy, 0x74), usb_rd(phy, 0x78));

	/* USB3/QUSB2 wrapper (dwc3-msm QSCRATCH = core + 0xf8800): read-only. */
	vm_offset_t qs = ml_io_map(QSCRATCH_PA, 0x100);
	if (qs != 0) {
		SWEET_LOG_DEBUG("SWEET USB: QSCRATCH HS_PHY_CTRL 0x%08x SS_PHY_CTRL 0x%08x GENERAL_CFG 0x%08x\n",
		    usb_rd(qs, QSCRATCH_HS_PHY_CTRL), usb_rd(qs, QSCRATCH_SS_PHY_CTRL), usb_rd(qs, 0x08));
	}
}

/*
 * Step 2: USB device mode, CDC-ACM serial port, used as the console log
 * channel.  The host sees a plain "USB serial device": Windows has the
 * inbox usbser.sys, macOS has AppleUSBCDCACMData, Linux has cdc_acm, so no
 * driver install is needed anywhere.  Every console byte written after the
 * port is opened is streamed to the host over the bulk IN endpoint, which
 * makes a real log channel (no more photographs of the screen).
 *
 * ABL leaves the DWC3 powered, clocked, in device mode and halted, with the
 * PHY set up, so only the device controller is restarted: soft reset, an
 * event buffer in our memory, endpoint 0, run. A DWC3 IRQ wakes the
 * event worker. DMA is not cache
 * coherent (DT qcom,gsi-disable-io-coherency): buffers are cleaned and
 * invalidated around every transfer.
 *
 * Register layout and the control-transfer state machine follow the
 * Synopsys DWC3 databook as used by Linux drivers/usb/dwc3 (ep0.c).
 *
 * DMA placement matters: all of XNU's RAM lives above 4 GiB
 * (phys_base=0x180000000), and a DWC3 that only implements a 32-bit AXI
 * address silently truncates every TRB and event pointer it is given. If
 * the host reports "device descriptor request failed" while the gadget says
 * it is running, that is the symptom. `sweetusbdma=<MiB>` puts the
 * whole gadget buffer at that physical address instead (e.g.
 * sweetusbdma=3584 = 0xE0000000), which is the way to test a low buffer.
 */
#define GCTL            0xc110
#define GEVNTADRLO      0xc400
#define GEVNTADRHI      0xc404
#define GEVNTSIZ        0xc408
#define GEVNTCOUNT      0xc40c
#define DCFG            0xc700
#define DCTL            0xc704
#define DEVTEN          0xc708
#define DSTS            0xc70c
#define DALEPENA        0xc720
#define DEPCMDPAR2(n)   (0xc800 + 0x10 * (n))
#define DEPCMDPAR1(n)   (0xc804 + 0x10 * (n))
#define DEPCMDPAR0(n)   (0xc808 + 0x10 * (n))
#define DEPCMD(n)       (0xc80c + 0x10 * (n))

#define DCTL_RUN_STOP   (1u << 31)
#define DCTL_CSFTRST    (1u << 30)
#define DSTS_DEVCTRLHLT (1u << 22)

#define DEPCMD_SETEPCONFIG      1
#define DEPCMD_SETTRANSFRESOURCE 2
#define DEPCMD_SETSTALL         4
#define DEPCMD_STARTTRANSFER    6
#define DEPCMD_DEPSTARTCFG      9
#define DEPCMD_CMDACT           (1u << 10)

#define TRB_HWO         (1u << 0)
#define TRB_LST         (1u << 1)
#define TRB_ISP_IMI     (1u << 10)
#define TRB_IOC         (1u << 11)
/* TRB control types for control transfers (DWC3 databook): 1 normal,
 * 2 setup, 3 status of a 2-stage (no data) transfer, 5 data,
 * 4 status of a 3-stage (with data) transfer. */
#define TRBCTL_SETUP    (2u << 4)
#define TRBCTL_STATUS2  (3u << 4)
#define TRBCTL_STATUS3  (4u << 4)
#define TRBCTL_DATA     (5u << 4)
#define TRBCTL_NORMAL   (1u << 4)

/* DMA area layout (SIZE must stay 16 KiB-aligned, it is one allocation) */
#define UD_EVT          0x0000          /* event buffer, 4 KiB */
#define UD_EVT_SIZE     0x1000
#define UD_TRB_OUT      0x1000          /* ep0 OUT TRB */
#define UD_TRB_IN       0x1010          /* ep0 IN TRB */
#define UD_TRB_RX       0x1020          /* ep1 OUT (bulk, host -> phone) */
#define UD_TRB_TX       0x1030          /* ep1 IN (bulk, phone -> host) */
#define UD_SETUP        0x1040          /* 8-byte setup packet */
#define UD_TRB_INT      0x1050          /* ep2 IN (interrupt, notifications) */
#define UD_DATA         0x1100          /* ep0 data stage, 512 bytes */
#define UD_NOTIFY       0x1300          /* 10-byte notification */
#define UD_RX           0x1400          /* bulk OUT data (dropped), 512 bytes */
#define UD_TX           0x2000          /* console bytes waiting for the host */
/* m8 confirmed the MH2LM enumerates at high speed.  Keep every console TRB
 * short of a whole number of HS bulk packets while isolating TX completion. */
#define UD_BULK_MPS     512
#define UD_RX_MAX       UD_BULK_MPS
#define UD_TX_MAX       0x1fff
#define UD_SIZE         0x4000

/* Physical endpoints: 2n = OUT, 2n+1 = IN */
#define EP_RX           2               /* 0x01 bulk OUT */
#define EP_TX           3               /* 0x81 bulk IN */
#define EP_INT          5               /* 0x82 interrupt IN */

/* sm8150-usb.dtsi: dwc3@a600000 GIC_SPI 133 -> architectural INTID 165. */
#define UD_IRQ_INTID    165u
#define UD_IRQ_WORD     (UD_IRQ_INTID / 32u)
#define UD_IRQ_BIT      (1u << (UD_IRQ_INTID % 32u))
#define UD_GICD_PA      0x17a00000u
#define UD_INTMASK      (1u << 31)

static struct {
	vm_offset_t dwc;
	vm_offset_t dma;
	uint64_t    dma_pa;
	uint32_t    evt_pos;
	int         sleep_chan;
	enum { EP0_SETUP, EP0_DATA, EP0_WAIT_STATUS2, EP0_WAIT_STATUS3, EP0_STATUS } state;
	bool        data_in;        /* data stage direction */
	uint32_t    events;
	uint32_t    posted;         /* console bytes handed to the host */
	uint32_t    tx_submitted;
	uint32_t    tx_completed;
	uint32_t    tx_errors;
	bool        configured;
	bool        dtr;            /* the host opened the port */
	bool        tx_busy;
	uint64_t    log_pos;        /* committed console position */
	uint64_t    log_pos_next;   /* position once the IN transfer completes */
} ud;
static inline uint32_t dwc_rd(uint32_t off);

/* 0: inactive, 1: armed, 2: worker pending, 3: failed,
 * 4: bounded software-pending GIC probe. */
static volatile unsigned ud_irq_state;
static volatile unsigned ud_irq_hits;
static volatile unsigned ud_irq_probe_hits;
static vm_offset_t ud_gicd;
static int ud_irq_sleep_chan;
static bool ud_irq_ready;
static unsigned ud_irq_empty;
static int ud_log_chan;
static volatile unsigned ud_log_started, ud_log_pending;

/* Called by the console ring after new bytes arrive. Coalesce wakeups. */
void
sweet_usb_log_notify(void)
{
	if (__atomic_load_n(&ud_log_started, __ATOMIC_ACQUIRE) &&
	    __atomic_exchange_n(&ud_log_pending, 1u, __ATOMIC_ACQ_REL) == 0u) {
		thread_wakeup(&ud_log_chan);
	}
}


static uint32_t
ud_gic_rd(unsigned off)
{
	return usb_rd(ud_gicd, off);
}

static void
ud_gic_wr(unsigned off, uint32_t value)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(ud_gicd + off) = value;
	__asm__ volatile("dsb oshst" ::: "memory");
}

static uint64_t
ud_gic_rd64(unsigned off)
{
	uint64_t value = *(volatile uint64_t *)(ud_gicd + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return value;
}

static void
ud_gic_wr64(unsigned off, uint64_t value)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint64_t *)(ud_gicd + off) = value;
	__asm__ volatile("dsb oshst" ::: "memory");
}

static bool
ud_gic_wait(void)
{
	for (unsigned i = 0; i < 1000; ++i) {
		if ((ud_gic_rd(0) & (1u << 31)) == 0) {
			return true;
		}
		delay(1);
	}
	return false;
}

static bool
ud_gic_configure(void)
{
	unsigned off = 4u * UD_IRQ_WORD;
	uint32_t typer = ud_gic_rd(0x004u);
	if (((typer & 0x1fu) + 1u) * 32u <= UD_IRQ_INTID) {
		return false;
	}
	ud_gic_wr(0x180u + off, UD_IRQ_BIT);
	if (!ud_gic_wait()) {
		return false;
	}
	ud_gic_wr(0x080u + off, ud_gic_rd(0x080u + off) | UD_IRQ_BIT);
	unsigned cfgOff = 0xc00u + 4u * (UD_IRQ_INTID / 16u);
	unsigned cfgShift = 2u * (UD_IRQ_INTID % 16u);
	ud_gic_wr(cfgOff, ud_gic_rd(cfgOff) & ~(3u << cfgShift));
	unsigned priOff = 0x400u + (UD_IRQ_INTID & ~3u);
	unsigned priShift = 8u * (UD_IRQ_INTID & 3u);
	uint32_t priority = ud_gic_rd(priOff);
	ud_gic_wr(priOff, (priority & ~(0xffu << priShift)) | (0xa0u << priShift));
	ud_gic_wr64(0x6000u + 8u * UD_IRQ_INTID, 0); /* boot CPU MPIDR */
	if (!ud_gic_wait()) {
		return false;
	}
	uint32_t ctlr = ud_gic_rd(0);
	uint32_t group = ud_gic_rd(0x080u + off);
	uint32_t cfg = (ud_gic_rd(cfgOff) >> cfgShift) & 3u;
	uint32_t pri = (ud_gic_rd(priOff) >> priShift) & 0xffu;
	uint64_t route = ud_gic_rd64(0x6000u + 8u * UD_IRQ_INTID);
	SWEET_LOG_DEBUG("SWEET USB IRQ: GIC intid=%u ctlr=%#x group-read=%u cfg=%u pri=%#x route=%#llx\n",
	    UD_IRQ_INTID, ctlr, (group & UD_IRQ_BIT) ? 1 : 0, cfg, pri, route);
	/* Non-secure IGROUPR readback was zero for the proven key SPI 513.
	 * Real delivery is checked by the probe, never by this bit alone. */
	return (ctlr & 2u) != 0 && cfg == 0 && pri == 0xa0u && route == 0;
}

/* Leave DWC3 INTMASK set: this checks only the GIC path, not the device. */
static bool
ud_gic_probe(void)
{
	unsigned off = 4u * UD_IRQ_WORD;
	uint32_t pending = ud_gic_rd(0x200u + off) & UD_IRQ_BIT;
	uint32_t active = ud_gic_rd(0x300u + off) & UD_IRQ_BIT;
	if ((dwc_rd(GEVNTSIZ) & UD_INTMASK) == 0 || pending || active) {
		SWEET_LOG_WARN("SWEET USB IRQ: GIC probe refused mask=%u pending=%u active=%u\n",
		    (dwc_rd(GEVNTSIZ) & UD_INTMASK) ? 1 : 0,
		    pending ? 1 : 0, active ? 1 : 0);
		return false;
	}
	__atomic_store_n(&ud_irq_probe_hits, 0u, __ATOMIC_RELEASE);
	__atomic_store_n(&ud_irq_state, 4u, __ATOMIC_RELEASE);
	ud_gic_wr(0x100u + off, UD_IRQ_BIT);
	if (!ud_gic_wait() || (ud_gic_rd(0x100u + off) & UD_IRQ_BIT) == 0) {
		__atomic_store_n(&ud_irq_state, 3u, __ATOMIC_RELEASE);
		ud_gic_wr(0x180u + off, UD_IRQ_BIT);
		SWEET_LOG_ERROR("SWEET USB IRQ: GIC probe enable/wait failed\n");
		return false;
	}
	ud_gic_wr(0x200u + off, UD_IRQ_BIT);
	for (unsigned i = 0; i < 1000; ++i) {
		if (__atomic_load_n(&ud_irq_probe_hits, __ATOMIC_ACQUIRE)) {
			break;
		}
		delay(100);
	}
	__atomic_store_n(&ud_irq_state, 3u, __ATOMIC_RELEASE);
	ud_gic_wr(0x180u + off, UD_IRQ_BIT);
	ud_gic_wr(0x280u + off, UD_IRQ_BIT);
	bool clear = ud_gic_wait();
	for (unsigned i = 0; clear && i < 1000; ++i) {
		pending = ud_gic_rd(0x200u + off) & UD_IRQ_BIT;
		active = ud_gic_rd(0x300u + off) & UD_IRQ_BIT;
		if (!pending && !active) {
			break;
		}
		if (i == 999) {
			clear = false;
		} else {
			delay(1);
		}
	}
	unsigned hits = __atomic_load_n(&ud_irq_probe_hits, __ATOMIC_ACQUIRE);
	SWEET_LOG_INFO("SWEET USB IRQ: software-pending probe hits=%u clear=%u pending=%u active=%u\n",
	    hits, clear ? 1 : 0, pending ? 1 : 0, active ? 1 : 0);
	return hits == 1u && clear;
}

/* Called by sleh_irq before GIC EOI. No event-ring, DMA, or gadget work here. */
bool
sweet_usb_irq(unsigned int intid)
{
	if (intid != UD_IRQ_INTID) {
		return false;
	}
	unsigned state = __atomic_load_n(&ud_irq_state, __ATOMIC_ACQUIRE);
	if (state == 4u) {
		ud_gic_wr(0x180u + 4u * UD_IRQ_WORD, UD_IRQ_BIT);
		__atomic_add_fetch(&ud_irq_probe_hits, 1u, __ATOMIC_RELEASE);
		return true;
	}
	unsigned expected = 1u;
	if (__atomic_compare_exchange_n(&ud_irq_state, &expected, 2u, false,
	    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		ud_gic_wr(0x180u + 4u * UD_IRQ_WORD, UD_IRQ_BIT);
		__atomic_add_fetch(&ud_irq_hits, 1u, __ATOMIC_RELEASE);
		thread_wakeup(&ud_irq_sleep_chan);
		return true;
	}
	if (expected == 2u || expected == 3u) {
		ud_gic_wr(0x180u + 4u * UD_IRQ_WORD, UD_IRQ_BIT);
		return true;
	}
	return false;
}

static inline void
dwc_wr(uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(ud.dwc + off) = v;
	__asm__ volatile ("dsb sy" ::: "memory");
}

static inline uint32_t
dwc_rd(uint32_t off)
{
	return usb_rd(ud.dwc, off);
}

static void
ud_sync(uint32_t off, uint32_t len)
{
	flush_dcache64((addr64_t)(ud.dma_pa + off), len, TRUE);
}

static void
ud_sleep_ms(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&ud.sleep_chan, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

static void
ud_irq_wait(unsigned observed)
{
	(void)assert_wait(&ud_irq_sleep_chan, THREAD_UNINT);
	if (__atomic_load_n(&ud_irq_hits, __ATOMIC_ACQUIRE) != observed ||
	    __atomic_load_n(&ud_irq_state, __ATOMIC_ACQUIRE) == 3u) {
		clear_wait(current_thread(), THREAD_AWAKENED);
	}
	thread_block(THREAD_CONTINUE_NULL);
}

static void
ud_irq_fail(const char *reason)
{
	__atomic_store_n(&ud_irq_state, 3u, __ATOMIC_RELEASE);
	if (ud_gicd) {
		ud_gic_wr(0x180u + 4u * UD_IRQ_WORD, UD_IRQ_BIT);
	}
	dwc_wr(GEVNTSIZ, dwc_rd(GEVNTSIZ) | UD_INTMASK);
	__atomic_store_n(&ud_log_started, 0u, __ATOMIC_RELEASE);
	thread_wakeup(&ud_irq_sleep_chan);
	thread_wakeup(&ud_log_chan);
	SWEET_LOG_ERROR("SWEET USB IRQ: FAILED %s hits=%u empty=%u; gadget stopped\n",
	    reason, __atomic_load_n(&ud_irq_hits, __ATOMIC_ACQUIRE), ud_irq_empty);
}

static bool
ud_irq_arm(void)
{
	if (!ud_irq_ready) {
		return false;
	}
	__atomic_store_n(&ud_irq_state, 1u, __ATOMIC_RELEASE);
	ud_gic_wr(0x100u + 4u * UD_IRQ_WORD, UD_IRQ_BIT);
	bool waited = ud_gic_wait();
	bool enabled = (ud_gic_rd(0x100u + 4u * UD_IRQ_WORD) & UD_IRQ_BIT) != 0;
	unsigned state = __atomic_load_n(&ud_irq_state, __ATOMIC_ACQUIRE);
	if (!waited || (!enabled && state == 1u) || state == 3u) {
		ud_irq_fail("GIC enable readback");
		return false;
	}
	dwc_wr(GEVNTSIZ, dwc_rd(GEVNTSIZ) & ~UD_INTMASK);
	if (dwc_rd(GEVNTSIZ) & UD_INTMASK) {
		ud_irq_fail("DWC3 event unmask readback");
		return false;
	}
	SWEET_LOG_INFO("SWEET USB IRQ: armed intid=%u size=%#x pending=%u\n",
	    UD_IRQ_INTID, dwc_rd(GEVNTSIZ), dwc_rd(GEVNTCOUNT) & 0xffffu);
	return true;
}


static int
ud_epcmd(unsigned ep, uint32_t cmd, uint32_t p0, uint32_t p1, uint32_t p2)
{
	dwc_wr(DEPCMDPAR0(ep), p0);
	dwc_wr(DEPCMDPAR1(ep), p1);
	dwc_wr(DEPCMDPAR2(ep), p2);
	dwc_wr(DEPCMD(ep), cmd | DEPCMD_CMDACT);
	for (int i = 0; i < 1000; i++) {
		uint32_t v = dwc_rd(DEPCMD(ep));
		if (!(v & DEPCMD_CMDACT)) {
			uint32_t status = (v >> 12) & 0xf;
			if (status) {
				SWEET_LOG_WARN("SWEET USB: ep%u cmd 0x%x status %u\n", ep, cmd & 0xf, status);
				return -1;
			}
			return 0;
		}
		delay(1);
	}
	SWEET_LOG_ERROR("SWEET USB: ep%u cmd 0x%x timeout\n", ep, cmd & 0xf);
	return -1;
}

/* One TRB (at DMA offset trb) on physical endpoint ep. */
static int
ud_start_trb(unsigned ep, uint32_t trb, uint32_t buf, uint32_t len, uint32_t ctl)
{
	volatile uint32_t *t = (volatile uint32_t *)(ud.dma + trb);
	uint64_t bpa = ud.dma_pa + buf;

	t[0] = (uint32_t)bpa;
	t[1] = (uint32_t)(bpa >> 32);
	t[2] = len;
	t[3] = ctl | TRB_HWO | TRB_LST | TRB_ISP_IMI | TRB_IOC;
	ud_sync(trb, 16);
	if (len) {
		ud_sync(buf, len);
	}
	usb_mark(SWEET_USB_MARK_TRB_CMD);
	uint64_t tpa = ud.dma_pa + trb;
	int err = ud_epcmd(ep, DEPCMD_STARTTRANSFER, (uint32_t)(tpa >> 32), (uint32_t)tpa, 0);
	usb_mark(SWEET_USB_MARK_TRB_WAIT);
	return err;
}

static void
ud_start(unsigned ep, uint32_t buf, uint32_t len, uint32_t ctl)
{
	ud_start_trb(ep, ep ? UD_TRB_IN : UD_TRB_OUT, buf, len, ctl);
}

static void
ud_start_setup(void)
{
	ud.state = EP0_SETUP;
	ud_start(0, UD_SETUP, 8, TRBCTL_SETUP);
}

static void
ud_stall(void)
{
	ud_epcmd(0, DEPCMD_SETSTALL, 0, 0, 0);
	ud_start_setup();
}

/*
 * USB descriptors: one CDC-ACM function (a serial port) behind an Interface
 * Association Descriptor.  VID 0x1209 / PID 0x0002 = pid.codes test ID, not
 * a product.  Interface 0 is the communication class (what Windows matches
 * on: USB\Class_02&SubClass_02&Prot_01), interface 1 carries the bulk pair.
 * MH2LM enumerates at high speed (DSTS connect speed 0), so the bulk pair
 * uses the HS maximum of 512 bytes.  The endpoint configuration must agree
 * with this descriptor.
 */
static const uint8_t ud_dev_desc[18] = {
	18, 1, 0x00, 0x02, 0xef, 0x02, 0x01, 64,
	0x09, 0x12, 0x02, 0x00, 0x00, 0x01, 1, 2, 3, 1,
};
static const uint8_t ud_cfg_desc[75] = {
	9, 2, 75, 0, 2, 1, 0, 0x80, 250,                /* config: 2 interfaces, 500 mA */
	8, 0x0b, 0, 2, 0x02, 0x02, 0x01, 0,             /* IAD: interfaces 0..1, CDC ACM */
	9, 4, 0, 0, 1, 0x02, 0x02, 0x01, 0,             /* comm interface 0, ACM + AT, 1 ep */
	5, 0x24, 0x00, 0x10, 0x01,                      /* header, CDC 1.10 */
	5, 0x24, 0x01, 0x00, 1,                         /* call management: none, data if 1 */
	4, 0x24, 0x02, 0x02,                            /* ACM: line coding + serial state */
	5, 0x24, 0x06, 0, 1,                            /* union: control 0, data 1 */
	7, 5, 0x82, 0x03, 16, 0, 8,                     /* ep 0x82 interrupt IN, 16 bytes */
	9, 4, 1, 0, 2, 0x0a, 0x00, 0x00, 0,             /* data interface 1: two bulk endpoints */
	7, 5, 0x81, 0x02, 0, 2, 0,                      /* ep 0x81 bulk IN, 512 */
	7, 5, 0x01, 0x02, 0, 2, 0,                      /* ep 0x01 bulk OUT, 512 */
};
static const char *const ud_strings[] = {
	NULL, "ios-sweet", "Sweet iOS serial console", "0001",
};

static uint32_t
ud_string(unsigned idx, uint8_t *out)
{
	if (idx == 0) {
		out[0] = 4; out[1] = 3; out[2] = 0x09; out[3] = 0x04;   /* en-US */
		return 4;
	}
	if (idx >= sizeof(ud_strings) / sizeof(ud_strings[0])) {
		return 0;
	}
	const char *str = ud_strings[idx];
	uint32_t n = 2;
	for (; *str && n < 126; str++) {
		out[n++] = (uint8_t)*str;
		out[n++] = 0;
	}
	out[0] = (uint8_t)n;
	out[1] = 3;
	return n;
}

/* SERIAL_STATE notification: DCD and DSR follow DTR, so the host's port
 * shows "connected" as soon as the application opens it. */
static void
ud_notify_serial_state(void)
{
	uint8_t *n = (uint8_t *)(ud.dma + UD_NOTIFY);
	n[0] = 0xa1; n[1] = 0x20;       /* SERIAL_STATE */
	n[2] = 0; n[3] = 0;             /* wValue */
	n[4] = 0; n[5] = 0;             /* wIndex: interface 0 */
	n[6] = 2; n[7] = 0;             /* wLength: 2 */
	n[8] = ud.dtr ? 0x03 : 0x00;    /* DCD | DSR */
	n[9] = 0;
	ud_start_trb(EP_INT, UD_TRB_INT, UD_NOTIFY, 10, TRBCTL_NORMAL);
}

static void ud_rx_arm(void);

/* SET_CONFIGURATION(1): bulk IN/OUT and the interrupt endpoint. */
static void
ud_configure(void)
{
	if (ud.configured) {
		return;
	}
	if (ud_epcmd(0, DEPCMD_DEPSTARTCFG | (2u << 16), 0, 0, 0) ||
	    ud_epcmd(EP_RX, DEPCMD_SETEPCONFIG, (2u << 1) | (UD_BULK_MPS << 3), (1u << 8) | ((uint32_t)EP_RX << 25), 0) ||
	    ud_epcmd(EP_RX, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0) ||
	    ud_epcmd(EP_TX, DEPCMD_SETEPCONFIG, (2u << 1) | (UD_BULK_MPS << 3) | (1u << 17), (1u << 8) | ((uint32_t)EP_TX << 25), 0) ||
	    ud_epcmd(EP_TX, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0) ||
	    ud_epcmd(EP_INT, DEPCMD_SETEPCONFIG, (3u << 1) | (16u << 3) | (2u << 17),
	    (1u << 8) | (7u << 16) | ((uint32_t)EP_INT << 25), 0) ||
	    ud_epcmd(EP_INT, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0)) {
		SWEET_LOG_ERROR("SWEET USB: data endpoints setup failed\n");
		return;
	}
	dwc_wr(DALEPENA, dwc_rd(DALEPENA) | (1u << EP_RX) | (1u << EP_TX) | (1u << EP_INT));
	ud.configured = true;
	ud_rx_arm();
	ud_notify_serial_state();
	SWEET_LOG_INFO("SWEET USB: ACM endpoints configured\n");
}

static void
ud_setup(void)
{
	const uint8_t *st = (const uint8_t *)(ud.dma + UD_SETUP);
	uint8_t *data = (uint8_t *)(ud.dma + UD_DATA);
	uint8_t type = st[0], req = st[1];
	uint16_t value = (uint16_t)(st[2] | st[3] << 8);
	uint16_t index = (uint16_t)(st[4] | st[5] << 8);
	uint16_t length = (uint16_t)(st[6] | st[7] << 8);
	uint32_t n = 0;
	bool ok = true;

	if (ud.events <= 200) {
		SWEET_LOG_DEBUG("SWEET USB: setup %02x %02x %04x %04x len %u\n", type, req, value, index, length);
	}
	ud.data_in = (type & 0x80) != 0;
	if ((type & 0x60) == 0x20) {    /* CDC class request */
		switch (req) {
		case 0x20:              /* SET_LINE_CODING: 7 bytes, contents do not matter */
			ok = (length == 7);
			break;
		case 0x21:              /* GET_LINE_CODING: 115200 8N1 */
			data[0] = 0x00; data[1] = 0xc2; data[2] = 0x01; data[3] = 0x00;
			data[4] = 0;            /* one stop bit */
			data[5] = 0;            /* no parity */
			data[6] = 8;            /* eight data bits */
			n = 7;
			ok = (length >= 7);
			break;
		case 0x22:              /* SET_CONTROL_LINE_STATE: bit0 DTR, bit1 RTS */
			ud.dtr = (value & 1) != 0;
			ok = (length == 0);
			if (ok) {
				SWEET_LOG_INFO("SWEET USB: port %s by the host\n", ud.dtr ? "opened" : "closed");
				ud_notify_serial_state();
			}
			break;
		case 0x23:              /* SEND_BREAK */
			ok = (length == 0);
			break;
		default:
			ok = (length == 0);
			break;
		}
	} else if ((type & 0x60) != 0) {
		ok = false;
	} else if (req == 6) {          /* GET_DESCRIPTOR */
		switch (value >> 8) {
		case 1: memcpy(data, ud_dev_desc, sizeof(ud_dev_desc)); n = sizeof(ud_dev_desc); break;
		case 2: memcpy(data, ud_cfg_desc, sizeof(ud_cfg_desc)); n = sizeof(ud_cfg_desc); break;
		case 3: n = ud_string(value & 0xff, data); ok = n != 0; break;
		default: ok = false; break;
		}
	} else if (req == 5) {          /* SET_ADDRESS: takes effect now (DWC3 handles the timing) */
		uint32_t dcfg = dwc_rd(DCFG) & ~(0x7fu << 3);
		dwc_wr(DCFG, dcfg | ((uint32_t)(value & 0x7f) << 3));
		SWEET_LOG_DEBUG("SWEET USB: address %u\n", value & 0x7f);
	} else if (req == 9) {          /* SET_CONFIGURATION */
		if (value == 1) {
			ud_configure();
		}
		sweet_usb_log_notify();
		SWEET_LOG_INFO("SWEET USB: configured (%u)\n", value);
	} else if (req == 8) {          /* GET_CONFIGURATION */
		data[0] = ud.configured ? 1 : 0; n = 1;
	} else if (req == 0) {          /* GET_STATUS */
		data[0] = 0; data[1] = 0; n = 2;
	} else if (req == 11) {         /* SET_INTERFACE */
	} else if (req == 10) {         /* GET_INTERFACE */
		data[0] = 0; n = 1;
	} else if (req == 1 || req == 3) { /* CLEAR/SET_FEATURE (halt, remote wakeup) */
	} else {
		ok = false;
	}
	if (!ok) {
		SWEET_LOG_WARN("SWEET USB: stall %02x %02x\n", type, req);
		ud_stall();
		return;
	}
	if (length == 0) {
		ud.state = EP0_WAIT_STATUS2;    /* status stage on XferNotReady */
		return;
	}
	ud.state = EP0_DATA;
	if (ud.data_in) {
		if (n > length) {
			n = length;
		}
		ud_start(1, UD_DATA, n, TRBCTL_DATA);
	} else {
		/* The host sends at most the request's wLength (1 KiB limit is plenty). */
		uint32_t dlen = length > 512 ? 512 : length;
		ud_start(0, UD_DATA, dlen, TRBCTL_DATA);
	}
}

/* ---- console stream ---- */

extern uint32_t sweet_log_copy(uint64_t *pos, char *out, uint32_t max);
extern uint64_t sweet_log_position(void);

static void
ud_rx_arm(void)
{
	(void)ud_start_trb(EP_RX, UD_TRB_RX, UD_RX, UD_RX_MAX, TRBCTL_NORMAL);
}

/* The console ring, streamed to the host: nothing here may print, it would
 * feed itself through the ring. */
static void
ud_log_thread(void *arg __unused, wait_result_t wr __unused)
{
	ud.log_pos = sweet_log_position();
	ud.log_pos = ud.log_pos > 65536 ? ud.log_pos - 65536 : 0;
	__atomic_store_n(&ud_log_started, 1u, __ATOMIC_RELEASE);
	for (;;) {
		if (__atomic_load_n(&ud_irq_state, __ATOMIC_ACQUIRE) == 3u) {
			break;
		}
		__atomic_store_n(&ud_log_pending, 0u, __ATOMIC_RELEASE);
		if (ud.configured && !ud.tx_busy) {
			uint64_t next = ud.log_pos;
			uint32_t n = sweet_log_copy(&next, (char *)(ud.dma + UD_TX), UD_TX_MAX);
			if (n > 1 && (n % UD_BULK_MPS) == 0) {
				/* Do not make a CDC read depend on an implicit ZLP. */
				next = ud.log_pos;
				n = sweet_log_copy(&next, (char *)(ud.dma + UD_TX), n - 1);
			}
			if (n) {
				ud.log_pos_next = next;
				ud.tx_busy = true;
				ud.posted += n;
				ud.tx_submitted++;
				if (ud.tx_submitted <= 3) {
					SWEET_LOG_DEBUG("SWEET USB: console TX submit %u bytes\n", n);
				}
				if (ud_start_trb(EP_TX, UD_TRB_TX, UD_TX, n,
				    TRBCTL_NORMAL) != 0) {
					ud.tx_busy = false;
					if (++ud.tx_errors <= 3) {
						SWEET_LOG_WARN("SWEET USB: console TX submit failed\n");
					}
				}
			}
		}
		(void)assert_wait(&ud_log_chan, THREAD_UNINT);
		if (__atomic_exchange_n(&ud_log_pending, 0u,
		    __ATOMIC_ACQ_REL)) {
			clear_wait(current_thread(), THREAD_AWAKENED);
		}
		thread_block(THREAD_CONTINUE_NULL);
	}
	__atomic_store_n(&ud_log_started, 0u, __ATOMIC_RELEASE);
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}


static void
ud_event(uint32_t ev)
{
	if (ev & 1) {                   /* device event */
		uint32_t type = (ev >> 8) & 0xf;
		if (type == 1) {        /* USB reset */
			dwc_wr(DCFG, dwc_rd(DCFG) & ~(0x7fu << 3));
			ud.configured = false;
			ud.dtr = false;
			ud.tx_busy = false;
			sweet_usb_log_notify();
			SWEET_LOG_INFO("SWEET USB: bus reset\n");
			ud_start_setup();
		} else if (type == 2) { /* connect done */
			uint32_t speed = dwc_rd(DSTS) & 7;
			/* ep0 max packet: 64 at high/full speed (modify action) */
			ud_epcmd(0, DEPCMD_SETEPCONFIG, (0u << 1) | (64u << 3) | (2u << 30),
			    (1u << 8) | (1u << 10) | (0u << 25), 0);
			ud_epcmd(1, DEPCMD_SETEPCONFIG, (0u << 1) | (64u << 3) | (2u << 30),
			    (1u << 8) | (1u << 10) | (1u << 25), 0);
			SWEET_LOG_INFO("SWEET USB: connected, speed %u\n", speed);
		} else if (type == 0) {
			SWEET_LOG_INFO("SWEET USB: disconnect\n");
		}
		return;
	}
	unsigned ep = (ev >> 1) & 0x1f;
	unsigned type = (ev >> 6) & 0xf;
	unsigned status = (ev >> 12) & 0xf;

	if (ep == EP_RX && type == 1) {         /* host -> phone bulk data: dropped */
		ud_rx_arm();
		return;
	}
	if (ep == EP_TX && type == 1) {         /* a console chunk has been read */
		ud.log_pos = ud.log_pos_next;
		ud.tx_busy = false;
		sweet_usb_log_notify();
		if (++ud.tx_completed <= 3) {
			SWEET_LOG_DEBUG("SWEET USB: console TX complete %u\n", ud.tx_completed);
		}
		return;
	}
	if (ep == EP_INT && type == 1) {        /* notification taken */
		return;
	}
	if (ep > 1) {
		return;
	}
	/*
	 * Control transfer state machine (Linux dwc3 ep0.c): SETUP completes ->
	 * request handled; the data stage (if any) is started right away; the
	 * status stage is started when the controller reports XferNotReady
	 * with status "control status"; its completion re-arms SETUP.
	 */
	if (type == 1) {                /* XferComplete */
		switch (ud.state) {
		case EP0_SETUP:
			ud_sync(UD_SETUP, 8);
			ud_setup();
			break;
		case EP0_DATA:
			ud.state = EP0_WAIT_STATUS3;
			break;
		case EP0_STATUS:
			ud_start_setup();
			break;
		default:
			break;
		}
		return;
	}
	if (type == 3 && status == 2) { /* XferNotReady, control status phase */
		if (ud.state == EP0_WAIT_STATUS2 || ud.state == EP0_WAIT_STATUS3) {
			uint32_t ctl = ud.state == EP0_WAIT_STATUS3 ? TRBCTL_STATUS3 : TRBCTL_STATUS2;
			ud.state = EP0_STATUS;
			ud_start(ep, UD_DATA, 0, ctl);
		}
	}
}

static void
ud_thread(void *arg __unused, wait_result_t wr __unused)
{
	for (;;) {
		unsigned observed = __atomic_load_n(&ud_irq_hits, __ATOMIC_ACQUIRE);
		unsigned irqState = __atomic_load_n(&ud_irq_state, __ATOMIC_ACQUIRE);
		if (irqState == 3u) {
			break;
		}
		uint32_t count = dwc_rd(GEVNTCOUNT) & 0xffffu;
		if (count) {
			ud_sync(UD_EVT, UD_EVT_SIZE);
			for (uint32_t done = 0; done < count; done += 4) {
				uint32_t ev = *(volatile uint32_t *)(ud.dma + UD_EVT + ud.evt_pos);
				ud.evt_pos = (ud.evt_pos + 4) % UD_EVT_SIZE;
				if (ud.events++ < 200) {
					SWEET_LOG_DEBUG("SWEET USB: event 0x%08x\n", ev);
				}
				ud_event(ev);
			}
			dwc_wr(GEVNTCOUNT, count);
		}
		if (__atomic_load_n(&ud_irq_state, __ATOMIC_ACQUIRE) == 3u) {
			break;
		}
		if (irqState == 2u) {
			if (count == 0 && ++ud_irq_empty >= 16u) {
				ud_irq_fail("16 empty IRQ wakeups");
				break;
			}
			if (count) {
				ud_irq_empty = 0;
			}
			if (observed <= 16u || (observed & 255u) == 0) {
				SWEET_LOG_INFO("SWEET USB IRQ: hit irq=%u bytes=%u events=%u empty=%u\n",
				    observed, count, ud.events, ud_irq_empty);
			}
			if ((observed & 127u) == 0) {
				SWEET_LOG_INFO("SWEET USB IRQ: counters hits=%u empty=%u count=%u TX=%u/%u err=%u\n",
				    observed, ud_irq_empty, dwc_rd(GEVNTCOUNT) & 0xffffu,
				    ud.tx_completed, ud.tx_submitted, ud.tx_errors);
			}
			unsigned expected = 2u;
			if (!__atomic_compare_exchange_n(&ud_irq_state, &expected,
			    1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
				break;
			}
			ud_gic_wr(0x100u + 4u * UD_IRQ_WORD, UD_IRQ_BIT);
			if (__atomic_load_n(&ud_irq_state, __ATOMIC_ACQUIRE) == 3u) {
				ud_gic_wr(0x180u + 4u * UD_IRQ_WORD, UD_IRQ_BIT);
				break;
			}
		}
		if ((dwc_rd(GEVNTCOUNT) & 0xffffu) != 0) {
			continue; /* Drain events queued while the worker ran. */
		}
		ud_irq_wait(observed);
	}
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}


/*
 * The controller is brought up long after the rest of the boot, so a USB
 * problem can never take the system down with it: by the time this runs iOS
 * is already up. `sweetusbdelay=<seconds>` changes the wait.
 */
static void
ud_bringup_thread(void *arg, wait_result_t wr __unused)
{
	int delay_s = 15;
	(void)PE_parse_boot_argn("sweetusbdelay", &delay_s, sizeof(delay_s));
	if (delay_s < 0) {
		delay_s = 0;
	}
	if (delay_s > 0) {
		SWEET_LOG_INFO("SWEET USB: waiting %d s before touching the controller\n", delay_s);
		ud_sleep_ms((unsigned)delay_s * 1000u);
	}
	sweet_usb_gadget_start((vm_offset_t)arg);
}

static void
sweet_usb_gadget_start(vm_offset_t dwc)
{
	thread_t thread;

	ud.dwc = dwc;
	SWEET_LOG_DEBUG("SWEET USB: gadget step 0: DMA\n");
	/*
	 * All of XNU's DRAM is above 4 GiB. If the controller turns out to
	 * implement fewer address bits, every DMA pointer it is handed is
	 * truncated and the host only ever sees "device descriptor request
	 * failed": `sweetusbdma=<MiB>` moves the buffer to that physical
	 * address (device memory, uncached) so the limitation can be tested
	 * without another kernel build.
	 */
	int dma_mb = 0;
	(void)PE_parse_boot_argn("sweetusbdma", &dma_mb, sizeof(dma_mb));
	if (dma_mb > 0) {
		ud.dma_pa = (uint64_t)dma_mb << 20;
		ud.dma = ml_io_map((vm_offset_t)ud.dma_pa, UD_SIZE);
		if (ud.dma == 0) {
			SWEET_LOG_ERROR("SWEET USB: cannot map DMA at 0x%llx\n", ud.dma_pa);
			return;
		}
		bzero((void *)ud.dma, UD_SIZE);
		SWEET_LOG_DEBUG("SWEET USB: DMA forced to 0x%llx (sweetusbdma=%d)\n", ud.dma_pa, dma_mb);
	} else {
		if (kmem_alloc_contig(kernel_map, &ud.dma, UD_SIZE, PAGE_MASK, 0, 0,
		    KMA_DATA | KMA_ZERO | KMA_PERMANENT, VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
			SWEET_LOG_ERROR("SWEET USB: no DMA memory\n");
			return;
		}
		ppnum_t pn = pmap_find_phys(kernel_pmap, (addr64_t)ud.dma);
		if (!pn) {
			SWEET_LOG_ERROR("SWEET USB: DMA memory has no physical page\n");
			return;
		}
		ud.dma_pa = ptoa_64(pn);
		SWEET_LOG_DEBUG("SWEET USB: DMA at 0x%llx (%s)\n", ud.dma_pa,
		    (ud.dma_pa + UD_SIZE - 1) >> 32 ? "above 4 GiB" : "below 4 GiB");
	}

	/* Device controller soft reset (PHY and global setup stay as ABL left them). */
	SWEET_LOG_DEBUG("SWEET USB: gadget step 1: DCTL soft reset (GCTL 0x%08x)\n", dwc_rd(GCTL));
	dwc_wr(DCTL, DCTL_CSFTRST);
	int i;
	for (i = 0; i < 500 && (dwc_rd(DCTL) & DCTL_CSFTRST); i++) {
		delay(1000);
	}
	if (dwc_rd(DCTL) & DCTL_CSFTRST) {
		SWEET_LOG_ERROR("SWEET USB: soft reset stuck after %d ms, GCTL 0x%08x DCFG 0x%08x\n",
		    i, dwc_rd(GCTL), dwc_rd(DCFG));
		return;
	}
	SWEET_LOG_INFO("SWEET USB: soft reset done after %d ms, GCTL 0x%08x DCFG 0x%08x\n",
	    i, dwc_rd(GCTL), dwc_rd(DCFG));

	SWEET_LOG_DEBUG("SWEET USB: GTXFIFOSIZ %08x %08x %08x %08x GRXFIFOSIZ %08x GHWPARAMS3 %08x\n",
	    dwc_rd(0xc300), dwc_rd(0xc304), dwc_rd(0xc308), dwc_rd(0xc30c),
	    dwc_rd(0xc380), dwc_rd(0xc14c));
	SWEET_LOG_DEBUG("SWEET USB: gadget step 2: event buffer + DCFG/DEVTEN\n");
	/* How many address bits does the event-buffer register really keep?
	 * 0xffffffff coming back means the core can be handed addresses above
	 * 4 GiB; anything less means it cannot and the buffer has to be low. */
	dwc_wr(GEVNTADRHI, 0xffffffffu);
	uint32_t hi_probe = dwc_rd(GEVNTADRHI);
	dwc_wr(GEVNTADRHI, 0);
	SWEET_LOG_DEBUG("SWEET USB: GEVNTADRHI kept 0x%08x of 0xffffffff (%s)\n", hi_probe,
	    (hi_probe >> 8) ? "64-bit addresses" : "32-bit addresses only");

	dwc_wr(GEVNTADRLO, (uint32_t)(ud.dma_pa + UD_EVT));
	dwc_wr(GEVNTADRHI, (uint32_t)((ud.dma_pa + UD_EVT) >> 32));
	dwc_wr(GEVNTSIZ, (1u << 31) | UD_EVT_SIZE);     /* masked until IRQ arm */
	dwc_wr(GEVNTCOUNT, dwc_rd(GEVNTCOUNT) & 0xffff);
	ud.evt_pos = 0;
	SWEET_LOG_DEBUG("SWEET USB: GEVNTADR readback 0x%08x%08x size 0x%08x\n",
	    dwc_rd(GEVNTADRHI), dwc_rd(GEVNTADRLO), dwc_rd(GEVNTSIZ));
	ud_gicd = ml_io_map(UD_GICD_PA, 0x10000);
	ud_irq_ready = ud_gicd && ud_gic_configure() && ud_gic_probe();
	if (!ud_irq_ready) {
		SWEET_LOG_ERROR("SWEET USB IRQ: GIC setup failed; gadget disabled\n");
		return;
	}

	/* High speed, address 0 */
	uint32_t dcfg = dwc_rd(DCFG) & ~(7u | (0x7fu << 3));
	dwc_wr(DCFG, dcfg | 0);
	/* Events: disconnect, USB reset, connect done */
	dwc_wr(DEVTEN, 0x7);

	SWEET_LOG_DEBUG("SWEET USB: gadget step 3: endpoint 0 setup\n");
	/* Endpoint 0 OUT/IN: start config, control type, max packet 512 until connect done. */
	if (ud_epcmd(0, DEPCMD_DEPSTARTCFG, 0, 0, 0) ||
	    ud_epcmd(0, DEPCMD_SETEPCONFIG, (0u << 1) | (64u << 3), (1u << 8) | (1u << 10) | (0u << 25), 0) ||
	    ud_epcmd(0, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0) ||
	    ud_epcmd(1, DEPCMD_SETEPCONFIG, (0u << 1) | (64u << 3), (1u << 8) | (1u << 10) | (1u << 25), 0) ||
	    ud_epcmd(1, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0)) {
		SWEET_LOG_ERROR("SWEET USB: endpoint 0 setup failed\n");
		return;
	}
	SWEET_LOG_DEBUG("SWEET USB: gadget step 4: DALEPENA + setup engine\n");
	int maxstep = 99;
	(void)PE_parse_boot_argn("sweetusbstep", &maxstep, sizeof(maxstep));
	if (maxstep < 4) {
		SWEET_LOG_INFO("SWEET USB: stopping before step 4 (sweetusbstep=%d)\n", maxstep);
		return;
	}
	SWEET_LOG_DEBUG("SWEET USB:   - DALEPENA write\n");
	usb_mark(SWEET_USB_MARK_DALE);
	dwc_wr(DALEPENA, 0x3);
	usb_mark(SWEET_USB_MARK_DALE_OK);
	/*
	 * The ep0 SETUP TRB is armed only after RUN_STOP, when the controller is
	 * actually running: issuing STARTTRANSFER while the device controller is
	 * halted is what took the SoC down (a DEPCMD with the core stopped ends
	 * in a bus error, not in a status code).
	 */
	usb_mark(SWEET_USB_MARK_STEP4_OK);
	SWEET_LOG_DEBUG("SWEET USB:   - step 4 done\n");

	int vbus = SWEET_DEV_USB_VBUS_DEFAULT;
	(void)PE_parse_boot_argn("sweetvbus", &vbus, sizeof(vbus));
	if (maxstep < 5) {
		SWEET_LOG_INFO("SWEET USB: stopping before step 5 (sweetusbstep=%d)\n", maxstep);
		return;
	}
	SWEET_LOG_DEBUG("SWEET USB: gadget step 5: QSCRATCH mode %d (0 none, 1 HS, 2 HS+SS)\n", vbus);
	usb_mark(SWEET_USB_MARK_STEP5);
	vm_offset_t qs = ml_io_map(QSCRATCH_PA, 0x100);
	if (qs != 0) {
		uint32_t hs = usb_rd(qs, QSCRATCH_HS_PHY_CTRL), ss = usb_rd(qs, QSCRATCH_SS_PHY_CTRL);
		SWEET_LOG_DEBUG("SWEET USB: QSCRATCH HS_PHY_CTRL 0x%08x SS_PHY_CTRL 0x%08x GENERAL_CFG 0x%08x\n",
		    hs, ss, usb_rd(qs, 0x08));
		if (vbus >= 1) {
			SWEET_LOG_DEBUG("SWEET USB:   - HS VBUS override write\n");
			*(volatile uint32_t *)(qs + QSCRATCH_HS_PHY_CTRL) = hs | UTMI_OTG_VBUS_VALID | SW_SESSVLD_SEL;
			__asm__ volatile ("dsb sy" ::: "memory");
			hs = usb_rd(qs, QSCRATCH_HS_PHY_CTRL);
			SWEET_LOG_DEBUG("SWEET USB:   - HS_PHY_CTRL now 0x%08x\n", hs);
		}
		if (vbus >= 2) {
			SWEET_LOG_DEBUG("SWEET USB:   - SS PHY write\n");
			*(volatile uint32_t *)(qs + QSCRATCH_SS_PHY_CTRL) = ss | LANE0_PWR_PRESENT;
			__asm__ volatile ("dsb sy" ::: "memory");
			SWEET_LOG_DEBUG("SWEET USB:   - SS_PHY_CTRL now 0x%08x\n", usb_rd(qs, QSCRATCH_SS_PHY_CTRL));
		}
	}
	if (vbus == 3) {
		/* PHY-side VBUS override instead of the wrapper write (femto v2:
		 * COMMON1 bit4 VBUSVLDEXTSEL0, CTRL1 bit0 VBUSVLDEXT0). Byte
		 * accesses: the PHY window mirrors one byte across the word. */
		vm_offset_t phy2 = ml_io_map(QUSB2_PA, QUSB2_SIZE);
		if (phy2 != 0) {
			uint8_t c1 = *(volatile uint8_t *)(phy2 + 0x58);
			uint8_t c2 = *(volatile uint8_t *)(phy2 + 0x60);
			SWEET_LOG_DEBUG("SWEET USB:   - PHY VBUS override (COMMON1 0x%02x CTRL1 0x%02x)\n", c1, c2);
			*(volatile uint8_t *)(phy2 + 0x58) = c1 | (1u << 4);
			*(volatile uint8_t *)(phy2 + 0x60) = c2 | 1u;
			__asm__ volatile ("dsb sy" ::: "memory");
			SWEET_LOG_DEBUG("SWEET USB:   - PHY now COMMON1 0x%02x CTRL1 0x%02x\n",
			    *(volatile uint8_t *)(phy2 + 0x58), *(volatile uint8_t *)(phy2 + 0x60));
		}
	}

	if (maxstep < 6) {
		SWEET_LOG_INFO("SWEET USB: stopping before step 6 (sweetusbstep=%d)\n", maxstep);
		return;
	}
	/* PRTCAPDIR: 2 = device only. Print what it was, then force it. */
	SWEET_LOG_DEBUG("SWEET USB: GCTL PRTCAPDIR was %u\n", (dwc_rd(GCTL) >> 12) & 3);
	dwc_wr(GCTL, (dwc_rd(GCTL) & ~(3u << 12)) | (2u << 12));
	SWEET_LOG_DEBUG("SWEET USB: gadget step 6: RUN_STOP\n");
	usb_mark(SWEET_USB_MARK_RUN);
	dwc_wr(DCTL, dwc_rd(DCTL) | DCTL_RUN_STOP);
	usb_mark(SWEET_USB_MARK_RUN_OK);
	for (i = 0; i < 500 && (dwc_rd(DSTS) & DSTS_DEVCTRLHLT); i++) {
		delay(1000);
	}
	SWEET_LOG_INFO("SWEET USB: running (DSTS 0x%08x after %d ms), DMA at 0x%llx\n", dwc_rd(DSTS), i, ud.dma_pa);
	SWEET_LOG_DEBUG("SWEET USB:   - arming ep0 setup now that the controller runs\n");
	ud_start_setup();
	if (!ud_irq_arm()) {
		SWEET_LOG_ERROR("SWEET USB IRQ: arm failed; gadget disabled\n");
		return;
	}
	if (kernel_thread_start(ud_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	} else {
		ud_irq_fail("event worker creation");
		return;
	}
	if (kernel_thread_start(ud_log_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	} else {
		ud_irq_fail("log worker creation");
	}
}
#elif defined(ARM64_BOARD_CONFIG_SWEET)
/*
 * No USB bring-up for this board yet: the SM8150 GCC CBCR/GDSC offsets and
 * the QSCRATCH wrapper layout still need to be mapped (GUACAMOLE.md).
 */
#include <sys/systm.h>
void
sweet_usb_probe(void)
{
	SWEET_LOG_INFO("SWEET USB: no driver for this board yet\n");
}
#endif /* ARM64_BOARD_CONFIG_SWEET */
