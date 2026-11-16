/*
 * GICv3 register layout for the sweet platform, plus the board-facing API of
 * the interrupt controller that owns the distributor.
 *
 * The phone boots with TrustZone alive: Group 0 belongs to the secure world,
 * every interrupt the non-secure side receives is Group 1 and is taken as an
 * IRQ through ICC_IAR1_EL1. This header used to be duplicated in SWEET.h,
 * MH2LM.h and GUACAMOLE.h; it is the single definition now.
 *
 * SweetGIC (iokit/Kernel/arm/SweetGIC.cpp) is the only writer of these
 * registers outside pe_fiq.c: drivers register an INTID and ask for it to be
 * enabled, instead of programming GICD/GICR themselves.
 */

#ifndef _PEXPERT_ARM64_SWEET_GIC_H
#define _PEXPERT_ARM64_SWEET_GIC_H

/* ---------------------------------------------------------------- INTIDs */

#define GIC_INTID_SGI_FIRST        0
#define GIC_INTID_SGI_LAST         15
#define GIC_INTID_PPI_FIRST        16
#define GIC_INTID_PPI_LAST         31
#define GIC_INTID_SPI_FIRST        32
#define GIC_INTID_SPI_LAST         1019
#define GIC_INTID_SPECIAL_FIRST    1020    /* 1020..1023 are not real sources */
#define GIC_SPURIOUS_IRQ           1023    /* IRQ no. for GIC spurious interrupt */

/* ----------------------------------------------------------- Distributor */

#define GICD_CTLR                  0x0000
#define GICD_CTLR_ENABLEGRP1       0x0001    /* Group 1 (non-secure) interrupts */
#define GICD_CTLR_ENABLEGRP1A      0x0002    /* Group 1 to EL1 (no EL2 guest) */
#define GICD_CTLR_ARE_NS           0x0010    /* affinity routing, non-secure */
#define GICD_CTLR_RWP              0x80000000

#define GICD_TYPER                 0x0004
#define GICD_TYPER_LINES_MASK      0x001f    /* (n + 1) * 32 interrupt lines */
#define GICD_TYPER_ES              0x0400    /* security extensions in use */

#define GICD_IIDR                  0x0008

#define GICD_IGROUPR               0x0080    /* 32 bits per INTID word */
#define GICD_ISENABLER             0x0100
#define GICD_ICENABLER             0x0180
#define GICD_ISPENDR               0x0200
#define GICD_ICPENDR               0x0280
#define GICD_ISACTIVER             0x0300
#define GICD_ICACTIVER             0x0380
#define GICD_IPRIORITYR            0x0400    /* one byte per INTID */
#define GICD_ICFGR                 0x0c00    /* two bits per INTID, 16 per word */
#define GICD_IROUTER               0x6000    /* 64 bits per SPI */

/* GICD_ICFGR field values (two bits per interrupt) */
#define GICD_ICFGR_LEVEL           0x0
#define GICD_ICFGR_EDGE            0x2       /* bit 2n+1 of the 2-bit field */

/* SPI target: affinity value in bits [39:0], bit 40 selects "any PE" */
#define GICD_IROUTER_IRM           0x100000000ULL

/* --------------------------------------------------------- Redistributor */

#define GICR_PE_SIZE              0x20000    /* size of each redistributor region */

#define GICR_TYPER                 0x0008
#define GICR_TYPER_AFFINITY_VALUE_SHIFT 32
#define GICR_TYPER_LAST            0x0010
#define GICR_WAKER                 0x0014
#define GICR_WAKER_PROCESSORSLEEP  0x0002
#define GICR_WAKER_CHILDRENASLEEP  0x0004

/* SGI/PPI frame inside the redistributor */
#define GICR_IGROUPR0              0x10080
#define GICR_ISENABLER0            0x10100
#define GICR_ICENABLER0            0x10180
#define GICR_ISPENDR0              0x10200
#define GICR_ICPENDR0              0x10280
#define GICR_ISACTIVER0            0x10300
#define GICR_ICACTIVER0            0x10380
#define GICR_IPRIORITYR0           0x10400
#define GICR_ICFGR0                0x10c00    /* PPIs 16..31 */
#define GICR_ICFGR1                0x10c04    /* SGIs 0..15 */

/* ------------------------------------------------------- CPU interface */

#define ICC_CTLR_EOIMODE           0x2       /* 0: EOI drops priority and deactivates */
#define ICC_SRE_SRE                0x1       /* system register access enabled */
#define ICC_PMR_FULL               0xff

#ifndef ASSEMBLER

#include <stdint.h>

/*
 * Interrupt handler signature, identical to IOInterruptHandler: the target
 * and refCon come from registration, nub is the registerer (NULL here) and
 * source is the INTID.
 */
typedef void (*sweet_gic_handler_t)(void *target, void *refCon, void *nub,
    int source);

#define SWEET_GIC_DEFAULT_PRIORITY 0xa0u

/* Registration options; bits 8..15 carry the priority. */
#define SWEET_GIC_OPT_EDGE         0x00000001u   /* edge-sensitive; level is the default */
#define SWEET_GIC_OPT_PRIORITY(p)  ((((uint32_t)(p)) & 0xffu) << 8)
#define SWEET_GIC_OPT_PRIORITY_OF(o) (((o) >> 8) & 0xffu)
#define SWEET_GIC_OPT_PRIORITY_MASK 0x0000ff00u

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GIC windows mapped by pe_init_fiq(). Zero until the boot CPU has run the
 * platform expert, which is why the controller is installed later (board
 * bring-up), not from the early PE code.
 */
uint64_t pe_gic_distributor_base(void);
uint64_t pe_gic_distributor_size(void);
uint64_t pe_gic_redistributor_base(void);
uint64_t pe_gic_redistributor_size(void);

/*
 * Install the one controller for this SoC and probe the distributor and the
 * redistributors. Returns 0 on success, 1 when already installed, negative
 * when the GIC windows are not available yet.
 */
int sweet_gic_install(void);
int sweet_gic_installed(void);

/*
 * Platform IRQ path: dispatch one INTID that was already acknowledged with
 * ICC_IAR1_EL1 (the caller keeps the raw IAR value) and complete it with
 * ICC_EOIR1_EL1.
 *
 * Returns 1 when the controller completed the interrupt -- the handler ran,
 * or the SPI had no handler and was only counted. It returns 0 for SGIs and
 * PPIs (INTID < 32, platform-owned) and for spurious INTIDs, and also when no
 * controller is installed: in every one of those cases the caller still owns
 * the completion and must write ICC_EOIR1_EL1 itself. Exactly one EOI per
 * interrupt, never two.
 */
int sweet_gic_handle_irq(uint64_t iar);

/*
 * Unregistered SPIs are counted and completed. Masking them is opt-in so a
 * staged migration can leave the legacy chain in front of the controller;
 * enable it once every live SPI is registered.
 */
void sweet_gic_set_stray_policy(int mask);

/*
 * Log the SPIs that currently have their distributor enable bit set. This is
 * the migration inventory while the legacy chain still owns the devices; the
 * call is read-only and returns immediately when no controller is installed.
 */
void sweet_gic_log_enabled(const char *tag);

/*
 * Driver-facing registration, SPIs only (INTID 32..1019; SGIs and PPIs belong
 * to the platform for now and are refused with kIOReturnUnsupported). The
 * controller programs group, priority, trigger and routing, but leaves the
 * source masked until sweet_gic_enable_irq() is called, mirroring
 * IOInterruptController. Returns an IOReturn value (kIOReturnSuccess == 0).
 */
int sweet_gic_register_irq(int intid, void *target,
    sweet_gic_handler_t handler, void *refCon, uint32_t options);
int sweet_gic_unregister_irq(int intid);
int sweet_gic_enable_irq(int intid);
int sweet_gic_disable_irq(int intid);
int sweet_gic_cause_irq(int intid);
uint64_t sweet_gic_hits(int intid);

#ifdef __cplusplus
}
#endif

#endif /* ! ASSEMBLER */

#endif /* ! _PEXPERT_ARM64_SWEET_GIC_H */
