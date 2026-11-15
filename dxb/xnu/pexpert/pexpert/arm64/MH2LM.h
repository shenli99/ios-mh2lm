/*
 * Board config for the LG G8X ThinQ ("mh2lm", Qualcomm SM8150 /
 * Snapdragon 855): Kryo 485 = 4x Cortex-A55 + 4x Cortex-A76, ARMv8.2,
 * no PAC. Same platform class as sweet (SM7150): generic ARM, 16K pages,
 * GICv3 with two security states (every interrupt, including the timer,
 * is a Group 1 IRQ taken through sleh_irq()).
 *
 * Per-device addresses (framebuffer, SPMI, RPMh, USB) live in
 * pexpert/pexpert/arm64/sweet_dev.h, not here.
 */

#ifndef _PEXPERT_ARM64_MH2LM_H
#define _PEXPERT_ARM64_MH2LM_H

#define NO_MONITOR                1
#define NO_ECORE                  1

#define SWEET                     1

/*
 * All eight cores (four A55 + four A76, Kryo 485) as one homogeneous SMP
 * cluster: cpus=N is clamped here. PSCI CPU_ON, 64-byte lines; the
 * scheduler does not know the A76s are faster (no AMP/P-E clusters yet).
 */
#define SWEET_SMP_MAX_CPUS         8

#define ARM_ARCH_TIMER
#define __ARM_ARCH__              8
#define __ARM_VMSA__              8
#define __ARM_VFP__               4
#define __ARM_COHERENT_CACHE__    1
#define __ARM_DEBUG__             7
#define __ARM_ENABLE_SWAP__       1
#define __ARM_V8_CRYPTO_EXTENSIONS__ 1
#define __ARM_PAN_AVAILABLE__     1
#define __ARM_16K_PG__            1

#define HAS_GIC_V3                1

#ifndef ASSEMBLER
#define PLATFORM_PANIC_LOG_DISABLED
#endif /* ! ASSEMBLER */

/*
 * Qualcomm PPI numbering (secure phys, phys, virt, hyp = 1/2/3/0) instead
 * of the usual 13/14/11/10: the virtual timer XNU uses (CNTV) is 16+3.
 * Must match the virtual-timer-intid the ADT publishes for /gic.
 */
#define SWEET_VTIMER_INTID        19
#define SWEET_IPI_SGI             1       // SGI used for cross-CPU signals

/* GICv3 register layout and the board-facing controller API. */
#include <pexpert/arm64/sweet_gic.h>

#endif /* ! _PEXPERT_ARM64_MH2LM_H */
