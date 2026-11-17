/*
 * SM8150 TLMM pin controller: register layout, tile map and the C API that
 * BSD/pexpert code uses to reach a pad.
 *
 * TLMM (Top Level Mode Manager) owns the 175 pads of SM8150 through four
 * 1 MiB windows. A pad occupies one 4 KiB slot inside its tile and the slot
 * index is the *global* pin number, not an index within the tile:
 *
 *     tile_base[tile] + 0x1000 * pin + { 0x0 CFG, 0x4 IO, 0x8 INTR_CFG, 0xc INTR_STATUS }
 *
 * GPIO122 (touch ATTN) is SOUTH + 0x7a000, which is exactly what Linux
 * pinctrl-sm8150.c computes with .ctl_reg = base + REG_SIZE * id.
 *
 * CFG fields are the PINGROUP() values of pinctrl-sm8150.c: pull at bit 0
 * (2 bits), mux function at bit 2 (3 bits), drive strength at bit 6 (3 bits),
 * output enable at bit 9. Bits 11/12 (EGPIO present/enable) belong to the
 * embedded-GPIO block and are deliberately preserved.
 *
 * TLMMGPIOController (iokit/Kernel/arm/TLMMGPIOController.cpp) is the only
 * writer of these registers. A driver either takes an exclusive AGPIO handle
 * from acquirePin() (the handle caches the two register pointers and never
 * searches again) or uses the plain-mode C API below, which refuses pins that
 * an exclusive owner holds.
 */

#ifndef _PEXPERT_ARM64_TLMM_GPIO_H
#define _PEXPERT_ARM64_TLMM_GPIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ------------------------------------------------------------- geometry */

/* SM8150 exposes 175 pads; the driver's table is rounded up so a bad pin is
 * rejected by an explicit bound check instead of a table index. */
#define TLMM_PINS_SM8150            175u
#define TLMM_PIN_MAX                200u

#define TLMM_TILE_COUNT             4u
#define TLMM_TILE_SIZE              0x100000u
#define TLMM_PIN_STRIDE             0x1000u

/* Tile order of the ADT 'reg' list and of the driver's window table. */
#define TLMM_TILE_WEST              0u
#define TLMM_TILE_EAST              1u
#define TLMM_TILE_NORTH             2u
#define TLMM_TILE_SOUTH             3u

/* Tile offsets from the TLMM base (0x03000000 on SM8150). */
#define TLMM_TILE_WEST_OFFSET       0x00100000u
#define TLMM_TILE_EAST_OFFSET       0x00500000u
#define TLMM_TILE_NORTH_OFFSET      0x00900000u
#define TLMM_TILE_SOUTH_OFFSET      0x00d00000u

/* Per-pad registers, relative to the pad's own base. */
#define TLMM_GPIO_CFG               0x0u
#define TLMM_GPIO_IO                0x4u
#define TLMM_GPIO_INTR_CFG          0x8u
#define TLMM_GPIO_INTR_STATUS       0xcu

/* ------------------------------------------------------------ CFG fields */

/* Bits 0..9 are the pin configuration; the driver rewrites exactly these. */
#define TLMM_GPIO_CFG_MASK          0x3ffu

#define TLMM_GPIO_PULL_SHIFT        0u
#define TLMM_GPIO_PULL_MASK         0x3u
#define TLMM_GPIO_PULL_NONE         0u
#define TLMM_GPIO_PULL_DOWN         1u
#define TLMM_GPIO_PULL_KEEPER       2u
#define TLMM_GPIO_PULL_UP           3u

#define TLMM_GPIO_MUX_SHIFT         2u
#define TLMM_GPIO_MUX_MASK          0x7u
#define TLMM_GPIO_MUX_GPIO          0u

#define TLMM_GPIO_DRIVE_SHIFT       6u
#define TLMM_GPIO_DRIVE_MASK        0x7u
#define TLMM_GPIO_DRIVE_2MA         0u
#define TLMM_GPIO_DRIVE_4MA         1u
#define TLMM_GPIO_DRIVE_6MA         2u
#define TLMM_GPIO_DRIVE_8MA         3u
#define TLMM_GPIO_DRIVE_10MA        4u
#define TLMM_GPIO_DRIVE_12MA        5u
#define TLMM_GPIO_DRIVE_14MA        6u
#define TLMM_GPIO_DRIVE_16MA        7u

#define TLMM_GPIO_OE                0x200u

/* ------------------------------------------------------------- IO fields */

#define TLMM_GPIO_IN                0x1u     /* pad level, read-only */
#define TLMM_GPIO_OUT               0x2u     /* output latch */

/* ---------------------------------------------------------- interrupt fields */

/*
 * INTR_CFG/INTR_STATUS are not programmed by the GPIO controller yet: the
 * TLMM summary interrupt (SPI 208, INTID 240) is still owned by the driver
 * that asked for it. The fields are named here so the layout has one home.
 */
#define TLMM_INTR_ENABLE            0x1u
#define TLMM_INTR_POLARITY          0x2u
#define TLMM_INTR_DETECT_SHIFT      2u
#define TLMM_INTR_DETECT_MASK       0x3u
#define TLMM_INTR_RAW_STATUS        0x10u
#define TLMM_INTR_TARGET_SHIFT      5u
#define TLMM_INTR_TARGET_MASK       0x7u
#define TLMM_INTR_TARGET_KPSS       3u
#define TLMM_INTR_STATUS_RAW        0x1u
#define TLMM_SUMMARY_INTID          240u

/* ------------------------------------------------------------ ADT binding */

#define TLMM_ADT_NODE_PATH          "/arm-io/tlmm"
#define TLMM_ADT_COMPATIBLE         "qcom,tlmm-sm8150"
#define TLMM_ADT_GPIO_CELLS         2u
#define TLMM_ADT_PIN_COUNT_PROP     "qcom,pin-count"

/* ------------------------------------------------------- C API (plain mode) */

/* 0 on success, otherwise -errno. */
#define TLMM_GPIO_OK                0
#define TLMM_GPIO_EIO               (-5)
#define TLMM_GPIO_EBUSY             (-16)
#define TLMM_GPIO_ENODEV            (-19)
#define TLMM_GPIO_EINVAL            (-22)
#define TLMM_GPIO_EOPNOTSUPP        (-95)

struct tlmm_pin_config {
	uint32_t func;          /* decoded CFG field set */
	uint32_t pull;
	uint32_t drive;
	int      output;        /* CFG bit 9 (output enable) */
	int      high;          /* IO bit 1 (output latch) */
	/*
	 * Raw registers. An exclusive owner still needs them: preserved bits
	 * (EGPIO present/enable) and full-register readbacks are exactly what a
	 * write-then-verify driver compares, and INTR_CFG is how it checks that
	 * nobody enabled a pin interrupt behind its back.
	 */
	uint32_t cfg;
	uint32_t io;
	uint32_t intr_cfg;
};

/*
 * Plain-mode access for C callers that cannot hold an OSObject. Every call is
 * a pin->tile lookup, a register read/modify/write and a check that no
 * exclusive AGPIO owns the pad (TLMM_GPIO_EBUSY when one does). None of these
 * are safe from interrupt context: an exclusive-check lock may sleep.
 *
 * When no controller has started (no ADT node, or the driver failed to map
 * its windows) every call returns TLMM_GPIO_ENODEV; callers keep their own
 * fallback, as the touch worker does today.
 */
int tlmm_gpio_available(void);
int gpio_set_value(unsigned int pin, int high);
int gpio_get_value(unsigned int pin, int *high);
int gpio_set_direction(unsigned int pin, int output);
int gpio_set_mux(unsigned int pin, unsigned int func);
int gpio_set_pull(unsigned int pin, unsigned int pull);
int gpio_set_drive(unsigned int pin, unsigned int drive);
int gpio_set_config(unsigned int pin, unsigned int func, unsigned int pull,
    unsigned int drive, int output, int high);
int gpio_get_config(unsigned int pin, struct tlmm_pin_config *config);

#ifdef __cplusplus
}
#endif

#endif /* ! _PEXPERT_ARM64_TLMM_GPIO_H */
