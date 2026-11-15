#ifndef _PEXPERT_ARM64_SWEET_DEV_H
#define _PEXPERT_ARM64_SWEET_DEV_H

/*
 * Per-device hardware constants for the sweet XNU port.
 *
 * One block per board, selected by the machine config:
 *   MH2LM      LG G8X ThinQ (SM8150):   -DARM64_BOARD_CONFIG_MH2LM
 *   GUACAMOLE  OnePlus 7 Pro (SM8150):  -DARM64_BOARD_CONFIG_GUACAMOLE
 *   SWEET      Redmi Note 10 Pro (SM7150): default
 *
 * tools/devices.py carries the same numbers for the ADT and the shim
 * (shim/boards/*.h); keep the three in sync. Values marked VERIFY are from
 * public kernel sources and have not been confirmed on the device's own
 * live FDT yet - see GUACAMOLE.md for the audit list.
 *
 * ARM64_BOARD_CONFIG_SWEET is also the switch for the port's platform code
 * (its drivers, HFS, pthread and libkern pieces); the GUACAMOLE machine
 * config therefore defines it as well (makedefs/MakeInc.def). Read it as
 * "sweet port", not "sweet phone": per-board numbers live here.
 */

#if defined(ARM64_BOARD_CONFIG_MH2LM)

/* ------------- LG G8X ThinQ (mh2lm, SM8150) ------------- */

#define SWEET_DEV_NAME          "mh2lm"

/* The framebuffer ABL scans out is Mu-Silicium's "Display Reserved" region
 * (lg-mh2lm PlatformMemoryMapLib: 0x9D200000 + 0x01200000), not the DTB's
 * splash_region at 0x9C000000. It is not 32 MiB aligned: the early mapping
 * maps the block at 0x9C000000 and the console starts 0x1200000 into it. */
#define SWEET_DEV_FB_PA         0x9D200000UL
#define SWEET_DEV_FB_W          1080
#define SWEET_DEV_FB_H          2340
#define SWEET_DEV_FB_STRIDE     (SWEET_DEV_FB_W * 4)

/* Same SoC as guacamole: SPMI arbiter and RPMh/command DB at the same
 * addresses. Only the keys driver uses SPMI here (read only).
 * Keys from the LG Windows DSDT (aloha-mh2lm lg-mh2lm/ACPI/DSDT.aml, BTNS on
 * \_SB.PM01): pin 0 = power (PON KPDPWR), pin 1 = volume down (PON RESIN),
 * pin 0x85 = volume up (PM8150 GPIO6). The Xiaomi Mi 9 (cepheus, SM8150)
 * DSDT has the same three pins: this is the SM8150 reference wiring, unlike
 * guacamole (GPIO6/GPIO7). The driver checks that GPIO6 is an enabled
 * input before using it and never writes PMIC registers. */
#define SWEET_DEV_SPMI_CORE_PA  0x0c440000u
#define SWEET_DEV_SPMI_CHNLS_PA 0x0c600000u
#define SWEET_DEV_SPMI_OBS_PA   0x0e600000u
#define SWEET_DEV_SPMI_INTR_PA  0x0e700000u
#define SWEET_DEV_SPMI_CFG_PA   0x0c40a000u
#define SWEET_DEV_PON_RT        0x0810u          /* PON RT_STS: bit 0 KPDPWR, bit 1 RESIN */
#define SWEET_DEV_VOL_GPIO_RT   0xc510u          /* volume up: PM8150 GPIO6 */
#define SWEET_DEV_VOL_GPIO2_RT  0xc610u          /* unused: volume down is PON RESIN */
#define SWEET_DEV_VOL_GPIO_SID  0                /* PM8150 */
#define SWEET_DEV_VOL_DOWN_PON  1
#define SWEET_DEV_KEYS_VOLUME   1

#define SWEET_DEV_RPMH_RSC_PA   0x18220000u
#define SWEET_DEV_RPMH_TCS_PA   0x18220d00u
#define SWEET_DEV_RPMH_TCS_SIZE 0x3000u
#define SWEET_DEV_RPMH_DICT_PA  0x0c3f000cu

/* Touch remains off: the touch controller, its I2C SE, GPIOs and supply rail
 * differ from guacamole. USB uses the verified DWC3/QUSB2 register map below;
 * keep its bring-up behind the sweetusb boot arg until host enumeration is
 * proven on this board. */
#define SWEET_DEV_TOUCH         0
#define SWEET_DEV_TOUCH_SEC     0
#define SWEET_DEV_TOUCH_TCM     1
#define SWEET_DEV_TOUCH_VREG_NAME "none"
#define SWEET_DEV_TOUCH_VREG_MV 0
#define SWEET_DEV_TOUCH_SE_PA   0x00c80000u
#define SWEET_DEV_TOUCH_GCC_PA  0x00100000u
#define SWEET_DEV_TOUCH_NORTH_PA 0x03000000u
#define SWEET_DEV_TOUCH_GCC_VOTE 0x52014u
#define SWEET_DEV_TOUCH_RCG     0x1e148u

#define SWEET_DEV_USB           1
#define SWEET_DEV_MARK_PA       0x0UL   /* USB progress marker; no IMEM marker on mh2lm */
#define SWEET_DEV_USB_ARM_ON_EVENT 1
#define SWEET_DEV_USB_VBUS_DEFAULT 0
#define SWEET_DEV_USB_GCC_PA    0x00100000u
#define SWEET_DEV_USB_DWC3_PA   0x0a600000u
#define SWEET_DEV_USB_QUSB2_PA  0x088e2000u
#define SWEET_DEV_USB_QSCRATCH_PA 0x0a6f8800u
#define SWEET_DEV_USB_GDSCR     0x0f004u
#define SWEET_DEV_USB_CBCR_MASTER 0x0f010u
#define SWEET_DEV_USB_CBCR_SLEEP  0x0f014u
#define SWEET_DEV_USB_CBCR_UTMI   0x0f018u
#define SWEET_DEV_USB_CBCR_CFGNOC 0x0f078u
#define SWEET_DEV_USB_CBCR_AGGRE  0x0f07cu
#define SWEET_DEV_USB_CBCR_AUX    0x0f050u
#define SWEET_DEV_USB_CBCR_AUX_NAME "phy-aux"
#define SWEET_DEV_USB_CBCR_CLKREF 0x8c008u

/* Panel physical size in mm (6.4", 19.5:9). */
#define SWEET_DEV_PANEL_MM_W    68
#define SWEET_DEV_PANEL_MM_H    148

#elif defined(ARM64_BOARD_CONFIG_GUACAMOLE)

/* ------------- OnePlus 7 Pro (guacamole, SM8150) ------------- */

#define SWEET_DEV_NAME          "guacamole"

/* ABL continuous-splash framebuffer (reserved-memory). 1440x3120 a8r8g8b8;
 * on sweet the node is cont_splash_region at 0x9c000000 - the guacamole
 * base/size must be read from its live DTB before the first boot. */
#define SWEET_DEV_FB_PA         0x9c000000UL     /* VERIFY */
#define SWEET_DEV_FB_W          1440
#define SWEET_DEV_FB_H          3120
#define SWEET_DEV_FB_STRIDE     (SWEET_DEV_FB_W * 4)

/* SPMI arbiter v5 at 0xc440000 (regs 0xc440000/0xc600000/0xe600000/
 * 0xe700000/0xc40a000 in the guacamole DTS - same block layout as SM7150).
 * PMIC SIDs: PM8150 = 0 (PON, keys), PM8150B = 2 (charger), PM8150L = 4,
 * PM8009 = 0xa. The drivers find APIDs by peripheral ID (PON 0x008,
 * GPIO 0x4c1), so they pick the right SID themselves. */
#define SWEET_DEV_SPMI_CORE_PA  0x0c440000u
#define SWEET_DEV_SPMI_CHNLS_PA 0x0c600000u
#define SWEET_DEV_SPMI_OBS_PA   0x0e600000u
#define SWEET_DEV_SPMI_INTR_PA  0x0e700000u
#define SWEET_DEV_SPMI_CFG_PA   0x0c40a000u
#define SWEET_DEV_PON_RT        0x0810u          /* PON RT_STS (PON base 0x800 + 0x10) */
#define SWEET_DEV_VOL_GPIO_RT   0xc110u          /* sweet: PM6150L GPIO2 */

/* Keys: guacamole uses PM8150 (SID 0) GPIO6 = volume up and GPIO7 = volume
 * down, both active low, instead of sweet's PM6150L GPIO2 + PON RESIN.
 * A PMIC GPIO pin has its own 0x100-byte SPMI page: GPIO6 RT_STS = 0xc510
 * (arbiter PPID (SID << 8) | (addr >> 8) = 0x00c5), GPIO7 = 0xc610 (0x00c6);
 * both are read at pin offset 0x10, bit 0. The ABL configures them as inputs
 * with pull-up for its own key menu and that survives fastboot boot.
 * Power is PON bit 0 as on sweet. */
#define SWEET_DEV_KEYS_VOLUME   1
#define SWEET_DEV_VOL_GPIO_RT   0xc510u         /* volume up: PM8150 GPIO6 */
#define SWEET_DEV_VOL_GPIO2_RT  0xc610u         /* volume down: PM8150 GPIO7 */
#define SWEET_DEV_VOL_GPIO_SID  0               /* PM8150 */
#define SWEET_DEV_VOL_DOWN_PON  0               /* volume-down is a GPIO here */

/* RPMh: apps RSC / DRV-2 at 0x18220000, TCS command registers at
 * 0x18220d00 (offset 0xd00; ACTIVE 2 / SLEEP 3 / WAKE 3), command DB at
 * 0xc3f0000c (live FDT "qcom,cmd-db@c3f000c" - same as sweet). */
#define SWEET_DEV_RPMH_RSC_PA   0x18220000u
#define SWEET_DEV_RPMH_TCS_PA   0x18220d00u
#define SWEET_DEV_RPMH_TCS_SIZE 0x3000u
/* Live tree qcom,cmd-db@c3f000c reg=<0xc3f000c 0x8>: the 8-byte locator
 * holds {command-db physical address, size}. */
#define SWEET_DEV_RPMH_DICT_PA  0x0c3f000cu

/* Touch: Samsung S6SY761 @0x48 on qupv3 SE17 I2C (i2c@0xc80000, SPI 373),
 * IRQ tlmm 122, reset tlmm 54, 1.8 V I/O switch tlmm 119, vdd pm8150_l17
 * (RPMh resource "ldoa17", 3.008 V). Driver: bsd/dev/sweet_touch_sec.c. */
#define SWEET_DEV_TOUCH         0
#define SWEET_DEV_TOUCH_SEC     1
#define SWEET_DEV_TOUCH_VREG_NAME "ldoa17"
#define SWEET_DEV_TOUCH_VREG_MV 3008
/* Reset-surviving scratch for the USB gadget progress marker (DRAM, above
 * the kernel image/ADT/boot_args; the shim reads the same word). */
#define SWEET_DEV_MARK_PA       0x198000000UL
/* SM8150's DWC3 wedges (bus hang -> watchdog -> EDL) if STARTTRANSFER is
 * issued before the host has reset the bus, so arm ep0 on USB reset /
 * XferNotReady instead of at bring-up. */
#define SWEET_DEV_USB_ARM_ON_EVENT 1
/* QSCRATCH VBUS override: 0 none, 1 HS wrapper write, 2 HS+SS wrapper writes,
 * 3 PHY-side override (femto v2 COMMON1/CTRL1). The wrapper at 0xa6f8800 is
 * readable but any write to it wedges the bus in this state, and the SS PHY
 * is the USB3/DP combo ABL drives for the display, so guacamole uses the
 * PHY-side override. */
#define SWEET_DEV_USB_VBUS_DEFAULT 3
#define SWEET_DEV_TOUCH_SE_PA   0x00c80000u
#define SWEET_DEV_TOUCH_GCC_PA  0x00100000u
#define SWEET_DEV_TOUCH_NORTH_PA 0x03000000u     /* live tree pinctrl@03000000 */
#define SWEET_DEV_TOUCH_GCC_VOTE 0x52014u        /* qupv3 wrap2 CBCR (branch vote) */
#define SWEET_DEV_TOUCH_RCG     0x1e148u         /* qupv3 wrap2 s0 clk src (cmd_rcgr) */

/* USB: usb0 = ssusb@a600000 (dwc3@a600000, 0xcd00) + hsphy@88e2000 (HS,
 * 0x110) + ssphy@88e8000 (QMP USB3/DP, 0x3000, vdd pm8150_l18) +
 * eud@88e0000; QSCRATCH = core + 0xf8800 = 0xa6f8800 (confirmed from
 * dwc3-msm.c); extcon is the PM8150B PD PHY (SID 2). The GCC clock
 * registers come from mainline gcc-sm8150.c (GDSCR 0xf004; CBCRs
 * 0xf010/0xf014/0xf018, cfg-noc 0xf078, aggre 0xf07c, phy aux 0xf050,
 * clkref 0x8c008). No ahb2phy clock on this SoC. */
#define SWEET_DEV_USB           1
#define SWEET_DEV_USB_GCC_PA    0x00100000u
#define SWEET_DEV_USB_DWC3_PA   0x0a600000u
#define SWEET_DEV_USB_QUSB2_PA  0x088e2000u
#define SWEET_DEV_USB_QSCRATCH_PA 0x0a6f8800u
#define SWEET_DEV_USB_GDSCR     0x0f004u
#define SWEET_DEV_USB_CBCR_MASTER 0x0f010u
#define SWEET_DEV_USB_CBCR_SLEEP  0x0f014u
#define SWEET_DEV_USB_CBCR_UTMI   0x0f018u
#define SWEET_DEV_USB_CBCR_CFGNOC 0x0f078u
#define SWEET_DEV_USB_CBCR_AGGRE  0x0f07cu
#define SWEET_DEV_USB_CBCR_AUX    0x0f050u	/* USB3 PHY aux */
#define SWEET_DEV_USB_CBCR_AUX_NAME "phy-aux"
#define SWEET_DEV_USB_CBCR_CLKREF 0x8c008u

/* Panel physical size in mm (digitizer reporting). */
#define SWEET_DEV_PANEL_MM_W    76
#define SWEET_DEV_PANEL_MM_H    163

#else /* !ARM64_BOARD_CONFIG_GUACAMOLE -> sweet */

/* ------------- Redmi Note 10 Pro (sweet, SM7150) ------------- */

#define SWEET_DEV_NAME          "sweet"

/* reserved-memory cont_splash_region that ABL leaves lit. */
#define SWEET_DEV_FB_PA         0x9c000000UL
#define SWEET_DEV_FB_W          1080
#define SWEET_DEV_FB_H          2400
#define SWEET_DEV_FB_STRIDE     (SWEET_DEV_FB_W * 4)

#define SWEET_DEV_SPMI_CORE_PA  0x0c440000u
#define SWEET_DEV_SPMI_CHNLS_PA 0x0c600000u
#define SWEET_DEV_SPMI_OBS_PA   0x0e600000u
#define SWEET_DEV_SPMI_INTR_PA  0x0e700000u
#define SWEET_DEV_SPMI_CFG_PA   0x0c40a000u
#define SWEET_DEV_PON_RT        0x0810u
#define SWEET_DEV_VOL_GPIO_RT   0xc110u

#define SWEET_DEV_RPMH_RSC_PA   0x18220000u
#define SWEET_DEV_RPMH_TCS_PA   0x18220d00u
#define SWEET_DEV_RPMH_TCS_SIZE 0x3000u
#define SWEET_DEV_RPMH_DICT_PA  0x0c3f000cu

#define SWEET_DEV_TOUCH_SE_PA   0x00a84000u
#define SWEET_DEV_TOUCH_GCC_PA  0x00100000u
#define SWEET_DEV_TOUCH_NORTH_PA 0x03900000u
#define SWEET_DEV_TOUCH_GCC_VOTE 0x5200cu
#define SWEET_DEV_TOUCH_RCG     0x18148u

#define SWEET_DEV_USB           1
#define SWEET_DEV_USB_GCC_PA    0x00100000u
#define SWEET_DEV_USB_DWC3_PA   0x0a600000u
#define SWEET_DEV_USB_QUSB2_PA  0x088e2000u
#define SWEET_DEV_USB_QSCRATCH_PA 0x0a6f8800u
#define SWEET_DEV_USB_GDSCR     0x0f004u
#define SWEET_DEV_USB_CBCR_MASTER 0x0f010u
#define SWEET_DEV_USB_CBCR_SLEEP  0x0f014u
#define SWEET_DEV_USB_CBCR_UTMI   0x0f018u
#define SWEET_DEV_USB_CBCR_CFGNOC 0x0502cu
#define SWEET_DEV_USB_CBCR_AGGRE  0x8201cu
#define SWEET_DEV_USB_CBCR_AUX    0x6a004u	/* ahb2phy */
#define SWEET_DEV_USB_CBCR_AUX_NAME "ahb2phy"
#define SWEET_DEV_USB_CBCR_CLKREF 0x8c010u

#define SWEET_DEV_TOUCH         1
#define SWEET_DEV_TOUCH_SEC     0
#define SWEET_DEV_TOUCH_VREG_NAME "ldoc11"
#define SWEET_DEV_TOUCH_VREG_MV 3200
#define SWEET_DEV_MARK_PA       0x118000000UL
#define SWEET_DEV_USB_ARM_ON_EVENT 0
#define SWEET_DEV_USB_VBUS_DEFAULT 2
#define SWEET_DEV_KEYS_VOLUME   1   /* GPIO2 volume-up + PON RESIN volume-down */
#define SWEET_DEV_VOL_GPIO_SID  4   /* PM6150L */
#define SWEET_DEV_VOL_DOWN_PON  1   /* volume-down is PON RESIN */

/* Panel physical size in mm (digitizer reporting). */
#define SWEET_DEV_PANEL_MM_W    68
#define SWEET_DEV_PANEL_MM_H    151

#endif /* ARM64_BOARD_CONFIG_MH2LM / GUACAMOLE */

/*
 * The early framebuffer mapping (start.s V=P, arm_vm_init.c kernel alias) is
 * one 32 MiB L2 block (16 KiB granule); block descriptors need an aligned
 * output address, so map the block that holds SWEET_DEV_FB_PA.
 */
#define SWEET_DEV_FB_BLOCK      0x2000000UL
#define SWEET_DEV_FB_MAP_PA     (SWEET_DEV_FB_PA & ~(SWEET_DEV_FB_BLOCK - 1))

#endif /* !_PEXPERT_ARM64_SWEET_DEV_H */
