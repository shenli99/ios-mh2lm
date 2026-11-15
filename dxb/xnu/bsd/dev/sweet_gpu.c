/*
 * GPU bring-up for guacamole, staged and gated by the boot argument so that a
 * bad step can never affect a normal boot:
 *
 *   sweetgpu=0   (default) do nothing
 *   sweetgpu=1   enable the GCC GPU clocks, then dump the GPUCC state
 *   sweetgpu=2   additionally enable the GPUCC AHB clock, power the CX GDSC
 *                and read the GPU's RBBM liveness registers
 *   sweetgpu=3   additionally power the GX head switch (vendor SW reset +
 *                AON reset + clamp sequence), read RBBM, then collapse GX
 *   sweetgpu=4   additionally vote the gfx.lvl ARC rail on before the GX
 *                sequence, then vote it off after the probe
 *   sweetgpu=5   additionally enable the GMU CX/GX clocks and read the GMU
 *                firmware/reset/HFI registers (read-only)
 *   sweetgpu=6   additionally read the GPU SMMU ID/global/SID/context state
 *   sweetgpu=7   additionally set the GMU RCG to 200 MHz, load a640_gmu.bin
 *                from the payload RAMDisk into ITCM/DTCM/SMMU memory and
 *                cold-boot the CM3 (no HFI yet)
 *
 * The ABL leaves gcc_gpu_cfg_ahb_clk gated. Reading the GPUCC register block
 * in that state wedges the interconnect: the shim probe did exactly that and
 * the watchdog dropped the phone into EDL (USB 05c6:900e). GCC itself is
 * always accessible (the UFS and GENI drivers use it), so the clock is
 * enabled and its halt status confirmed *before* the first GPUCC read.
 *
 * Stage 2 follows the mainline/VENDOR GDSC sequence for the CX power domain
 * (clear SW_COLLAPSE, then poll GDS_HW_CTRL BIT(31)); it does not touch the
 * GX domain, whose rail is controlled through RPMh and the GMU. The GPU's
 * own SMMU (0x2ca0000, SID 0 = gfx3d_user, SID 2 = gfx3d_secure), the GMU
 * firmware and the CP ring are still out of scope.
 *
 * Register offsets: drivers/clk/qcom/gcc-sm8150.c, gpucc-sm8150.c and
 * gdsc.c. GPU registers use the downstream/freedreno dword-index convention
 * (RBBM_STATUS 0x210 -> byte offset 0x840).
 */
#if defined(ARM64_BOARD_CONFIG_GUACAMOLE)

#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <pexpert/arm64/sweet_dev.h>
#include <arm/caches_internal.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern void sweet_rpmh_arc_dump(void);
extern int sweet_rpmh_arc_set(const char *res, int corner);
void sweet_gpu_init(void);

#define GPU_GCC_PA		0x00100000u
#define GPUCC_PA		0x02c90000u
#define GPU_PA			0x02c00000u
#define GPU_GMU_PA		0x02c6a000u
#define GPU_GMU_SIZE		0x00030000u

/* GCC branches (enable BIT(0) in the same register). */
#define GCC_GPU_CFG_AHB		0x71004u	/* hwcg BIT(1), critical */
#define GCC_GPU_MEMNOC_GFX	0x7100cu	/* voted branch */
#define GCC_GPU_SNOC_DVM_GFX	0x71018u
#define GCC_DDRSS_GPU_AXI	0x71154u	/* voted; also clocks the GPU SMMU */

/* GPUCC, qcom,gpucc-sm8150. */
#define GPUCC_AHB_CLK		0x1078u		/* BIT(0), CLK_IS_CRITICAL */
#define GPUCC_CXO_CLK		0x109cu		/* rbbmtimer_clk */
#define GPUCC_GX_BCR		0x1008u		/* sw-reset in the GDSC contract */
#define GPUCC_GX_GDSCR		0x100cu		/* +0x4 holds BIT(16) power up */
#define GPUCC_GX_DOMAIN		0x1508u		/* GMEM reset BIT(4), clamp BIT(0) */
#define GPUCC_CX_BCR		0x1068u
#define GPUCC_CX_GDSCR		0x106cu
#define GPUCC_GMU_BCR		0x111cu
#define GPUCC_RCG_CMD		0x1120u
#define GPUCC_CX_GMU_CLK	0x1098u
#define GPUCC_GX_GMU_CLK	0x1064u
#define GPUCC_GX_CFG_GDSCR	0x1010u		/* BIT(16) GX power up */
#define GPUCC_CX_HW_CTRL	0x1540u		/* BIT(31) = CX on */

/* GDSC register bits (drivers/clk/qcom/gdsc.c). */
#define GPUCC_GDSC_SW_COLLAPSE	0x00000001u
#define GPUCC_GDSC_HW_CONTROL	0x00000002u
#define GPUCC_GDSC_PWR_ON	0x80000000u
#define GPUCC_GDSC_PWR_DOWN	0x00008000u
#define GPUCC_GDSC_PWR_UP	0x00010000u
#define GPUCC_GDSC_GMEM_RESET	0x00000010u
#define GPUCC_GDSC_CLAMP_IO	0x00000001u

/* GPU registers, dword offsets from qcom,kgsl-3d0@2C00000. */
#define GPU_RBBM_INT_0_STATUS	0x201u
#define GPU_RBBM_STATUS		0x210u
#define GPU_RBBM_STATUS3	0x213u
#define GPU_CP_ALWAYS_ON_LO	0x980u
#define GPU_CP_ALWAYS_ON_HI	0x981u

/* GMU registers: a6xx_gmu.xml.h offsets are dword indices, not bytes. */
#define GMU_DTCM_LAST		((0x1c00u + 0xff8u) * 4u)
#define GMU_CM3_SYSRESET	(0x5000u * 4u)
#define GMU_CM3_BOOT_CONFIG	(0x5001u * 4u)
#define GMU_CM3_FW_INIT_RESULT	(0x501cu * 4u)
#define GMU_CM3_CFG		(0x502du * 4u)
#define GMU_CX_GMU_WFI_CONFIG	(0x5002u * 4u)
#define GMU_HFI_CTRL_STATUS	(0x5180u * 4u)
#define GMU_GMU2HOST_INTR_INFO	(0x5192u * 4u)
#define GMU_GENERAL_7		(0x51ccu * 4u)
#define GMU_AO_HOST_INTR_STATUS	(0x9305u * 4u)
#define GMU_AHB_FENCE_STATUS	(0x9313u * 4u)
#define GMU_CM3_ITCM_START	(0x0c00u * 4u)
#define GMU_CM3_DTCM_START	(0x1c00u * 4u)
#define GMU_CM3_PWR_COL_CP_RESP	(0x5101u * 4u)
#define GMU_HFI_SFR_ADDR	(0x5182u * 4u)
#define GMU_HFI_QTBL_INFO	(0x5184u * 4u)
#define GMU_HFI_QTBL_ADDR	(0x5185u * 4u)
#define GMU_GMU2HOST_INTR_CLR	(0x5191u * 4u)
#define GMU_GMU2HOST_INTR_MASK	(0x5193u * 4u)
#define GMU_AO_HOST_INTR_CLR	(0x9304u * 4u)
#define GMU_AO_HOST_INTR_MASK	(0x9306u * 4u)
#define GMU_AHB_FENCE_RANGE_0	(0x9311u * 4u)
#define GMU_ICACHE_CONFIG	(0x4c00u * 4u)
#define GMU_DCACHE_CONFIG	(0x4c01u * 4u)
#define GMU_SYS_BUS_CONFIG	(0x4c0fu * 4u)
#define GMU_PWR_COL_INTER_FRAME_CTRL (0x50c0u * 4u)
#define GMU_PWR_COL_CP_MSG	(0x5100u * 4u)
#define GMU_RPMH_CTRL		(0x50e8u * 4u)

/* GPU SMMU (SMMU-500), GR0 global and stream-match registers. */
#define GPU_SMMU_PA		0x02ca0000u
#define GPU_SMMU_SIZE		0x00010000u
#define SMMU_GR0_sCR0		0x0u
#define SMMU_GR0_sACR		0x10u
#define SMMU_GR0_ID0		0x20u
#define SMMU_GR0_ID1		0x24u
#define SMMU_GR0_ID2		0x28u
#define SMMU_GR0_sGFSR		0x48u
#define SMMU_GR0_SMR(n)		(0x800u + (n) * 4u)
#define SMMU_GR0_S2CR(n)	(0xc00u + (n) * 4u)

/* Stage-1 4K page tables: 2MB block descriptors, Normal WB, inner shareable. */
#define SMMU_PTE_BLOCK_ATTR	(1u | (1u << 10) | (3u << 8) | (1u << 2))
#define SMMU_PTE_TABLE		0x3u

static uint32_t
gpu_rd(vm_offset_t base, uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}

static void
gpu_wr(vm_offset_t base, uint32_t off, uint32_t v)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(base + off) = v;
	__asm__ volatile("dsb osh" ::: "memory");
}

/* Branch status: CLK_OFF is BIT(31); NOC FSM bits 30:28 read 0b010 when off. */
static bool
gpu_clk_on(uint32_t v)
{
	return !(v & 0x80000000u) || ((v >> 28) & 7u) == 2u;
}

static void
gpu_enable_branch(vm_offset_t gcc, uint32_t off, const char *name)
{
	uint32_t before = gpu_rd(gcc, off);

	gpu_wr(gcc, off, before | 1u);
	for (unsigned i = 0; i < 100 && !gpu_clk_on(gpu_rd(gcc, off)); ++i) {
		delay(10);
	}
	uint32_t after = gpu_rd(gcc, off);
	printf("SWEET GPU: %s %#x -> %#x%s\n", name, before, after,
	    gpu_clk_on(after) ? "" : " (still halted)");
}

/*
 * Power the GPU CX domain. The parent CX rail is already on (CPU, UFS and
 * GPUCC are alive), so this only flips the local head switch. The GX domain
 * is deliberately left alone: its GFX rail is voted through RPMh and its
 * head switch is controlled by the GMU once the firmware runs.
 */
static bool
gpu_power_cx(vm_offset_t gpucc)
{
	uint32_t gdscr = gpu_rd(gpucc, GPUCC_CX_GDSCR);
	uint32_t hw = gpu_rd(gpucc, GPUCC_CX_HW_CTRL);

	printf("SWEET GPU: cx before gdscr=%#x hw=%#x\n", gdscr, hw);
	if (gdscr & GPUCC_GDSC_HW_CONTROL) {
		printf("SWEET GPU: cx is under hardware control; not touching it\n");
		return (hw & GPUCC_GDSC_PWR_ON) != 0;
	}
	if (gdscr & GPUCC_GDSC_SW_COLLAPSE) {
		gpu_wr(gpucc, GPUCC_CX_GDSCR, gdscr & ~GPUCC_GDSC_SW_COLLAPSE);
		(void)gpu_rd(gpucc, GPUCC_CX_GDSCR);	/* posted-write barrier */
		delay(1);
	}
	for (unsigned i = 0; i < 2000; ++i) {
		hw = gpu_rd(gpucc, GPUCC_CX_HW_CTRL);
		if (hw & GPUCC_GDSC_PWR_ON) {
			break;
		}
		delay(1);
	}
	printf("SWEET GPU: cx after gdscr=%#x hw=%#x%s\n",
	    gpu_rd(gpucc, GPUCC_CX_GDSCR), hw,
	    (hw & GPUCC_GDSC_PWR_ON) ? "" : " (power-up incomplete)");
	return (hw & GPUCC_GDSC_PWR_ON) != 0;
}

/* Read-only liveness dump; a driver never writes these registers. */
static void
gpu_probe_core(void)
{
	vm_offset_t gpu = ml_io_map(GPU_PA, 0x40000);
	if (gpu == 0) {
		printf("SWEET GPU: core mapping failed\n");
		return;
	}
	uint32_t status = gpu_rd(gpu, GPU_RBBM_STATUS * 4);
	uint32_t ints = gpu_rd(gpu, GPU_RBBM_INT_0_STATUS * 4);
	uint32_t status3 = gpu_rd(gpu, GPU_RBBM_STATUS3 * 4);
	uint32_t counter_lo = gpu_rd(gpu, GPU_CP_ALWAYS_ON_LO * 4);
	uint32_t counter_hi = gpu_rd(gpu, GPU_CP_ALWAYS_ON_HI * 4);

	printf("SWEET GPU: rbbm status=%#x int=%#x status3=%#x\n",
	    status, ints, status3);
	printf("SWEET GPU: cp always-on hi=%#x lo=%#x%s\n", counter_hi, counter_lo,
	    (status == 0xffffffffu || status == 0) ? " (no register response)" : "");
}

/*
 * Read-only GMU state probe. The GMU CX clock is the HFI/core clock; the GX
 * clock covers the GX-side logic. Enable them only for the probe, read the
 * reset/firmware/HFI registers, then restore branches that were off before.
 * Nothing here writes the GMU or loads firmware.
 */
static void
gpu_probe_gmu(vm_offset_t gpucc)
{
	uint32_t cx_before = gpu_rd(gpucc, GPUCC_CX_GMU_CLK);
	uint32_t gx_before = gpu_rd(gpucc, GPUCC_GX_GMU_CLK);
	vm_offset_t gmu;

	printf("SWEET GPU: gmu clocks before cx=%#x gx=%#x bcr=%#x rcg=%#x\n",
	    cx_before, gx_before, gpu_rd(gpucc, GPUCC_GMU_BCR),
	    gpu_rd(gpucc, GPUCC_RCG_CMD));
	gpu_enable_branch(gpucc, GPUCC_CX_GMU_CLK, "gpu_cc_cx_gmu_clk");
	gpu_enable_branch(gpucc, GPUCC_GX_GMU_CLK, "gpu_cc_gx_gmu_clk");

	gmu = ml_io_map(GPU_GMU_PA, GPU_GMU_SIZE);
	if (gmu == 0) {
		printf("SWEET GPU: GMU mapping failed\n");
	} else {
		printf("SWEET GPU: gmu dtcm-last=%#x fw-init=%#x boot=%#x reset=%#x\n",
		    gpu_rd(gmu, GMU_DTCM_LAST), gpu_rd(gmu, GMU_CM3_FW_INIT_RESULT),
		    gpu_rd(gmu, GMU_CM3_BOOT_CONFIG), gpu_rd(gmu, GMU_CM3_SYSRESET));
		printf("SWEET GPU: gmu intr=%#x gmu2host=%#x hfi=%#x general7=%#x fence=%#x\n",
		    gpu_rd(gmu, GMU_AO_HOST_INTR_STATUS),
		    gpu_rd(gmu, GMU_GMU2HOST_INTR_INFO),
		    gpu_rd(gmu, GMU_HFI_CTRL_STATUS),
		    gpu_rd(gmu, GMU_GENERAL_7),
		    gpu_rd(gmu, GMU_AHB_FENCE_STATUS));
	}

	if (!(cx_before & 1u)) {
		gpu_wr(gpucc, GPUCC_CX_GMU_CLK,
		    gpu_rd(gpucc, GPUCC_CX_GMU_CLK) & ~1u);
	}
	if (!(gx_before & 1u)) {
		gpu_wr(gpucc, GPUCC_GX_GMU_CLK,
		    gpu_rd(gpucc, GPUCC_GX_GMU_CLK) & ~1u);
	}
	printf("SWEET GPU: gmu clocks after cx=%#x gx=%#x\n",
	    gpu_rd(gpucc, GPUCC_CX_GMU_CLK), gpu_rd(gpucc, GPUCC_GX_GMU_CLK));
}

/*
 * Read-only GPU SMMU state. GMU firmware blocks live at low IOVAs (icache
 * 0x4000, dcache 0x44000, HFI 0x60001000), so whether the SMMU is active and
 * how SIDs 0/2/4/5 are mapped decides the GMU memory plan. No writes here.
 */
static void
gpu_probe_smmu(void)
{
	vm_offset_t s = ml_io_map(GPU_SMMU_PA, GPU_SMMU_SIZE);
	if (s == 0) {
		printf("SWEET GPU: SMMU mapping failed\n");
		return;
	}
	uint32_t id0 = gpu_rd(s, SMMU_GR0_ID0);
	uint32_t id1 = gpu_rd(s, SMMU_GR0_ID1);
	uint32_t id2 = gpu_rd(s, SMMU_GR0_ID2);
	uint32_t scr0 = gpu_rd(s, SMMU_GR0_sCR0);
	uint32_t acr = gpu_rd(s, SMMU_GR0_sACR);
	uint32_t gfsr = gpu_rd(s, SMMU_GR0_sGFSR);
	unsigned pgshift = (id1 & 0x80000000u) ? 16 : 12;
	unsigned npage = 1u << (((id1 >> 28) & 7u) + 1);
	unsigned ncb = id1 & 0xffu;
	unsigned nsmr = id0 & 0xffu;

	printf("SWEET GPU: smmu id0=%#x id1=%#x id2=%#x scr0=%#x acr=%#x gfsr=%#x\n",
	    id0, id1, id2, scr0, acr, gfsr);
	printf("SWEET GPU: smmu pgshift=%u gr-page=%u cb=%u smr=%u clientpd=%u\n",
	    pgshift, npage, ncb, nsmr, scr0 & 1u);
	for (unsigned n = 0; n < 8; ++n) {
		printf("SWEET GPU: smmu sid%u smr=%#x s2cr=%#x\n", n,
		    gpu_rd(s, SMMU_GR0_SMR(n)), gpu_rd(s, SMMU_GR0_S2CR(n)));
	}
	if (!(scr0 & 1u)) {
		uint32_t gr1 = npage << pgshift;
		for (unsigned n = 0; n < 8; ++n) {
			uint32_t s2cr = gpu_rd(s, SMMU_GR0_S2CR(n));
			unsigned type = (s2cr >> 16) & 3u;
			unsigned cb = s2cr & 0xffu;
			uint32_t base = gr1 + (cb << pgshift);
			if (type != 0 || cb >= ncb ||
			    base > GPU_SMMU_SIZE - 0x40u) {
				continue;
			}
			printf("SWEET GPU: smmu sid%u cb%u sctlr=%#x ttbr0=%#x tcr=%#x\n",
			    n, cb, gpu_rd(s, base + 0x0), gpu_rd(s, base + 0x20),
			    gpu_rd(s, base + 0x30));
		}
	}
}

extern ppnum_t pmap_find_phys(pmap_t pmap, addr64_t va);

static vm_offset_t
gpu_contig(vm_size_t size, vm_offset_t align, uint64_t *pa)
{
	vm_offset_t va = 0;
	kern_return_t kr;
	ppnum_t ppn;

	kr = kmem_alloc_contig(kernel_map, &va, size, align, 0, 0,
	    KMA_ZERO | KMA_DATA, VM_KERN_MEMORY_IOKIT);
	if (kr != KERN_SUCCESS || va == 0) {
		return 0;
	}
	ppn = pmap_find_phys(kernel_pmap, va);
	if (ppn == 0) {
		return 0;
	}
	*pa = ptoa_64(ppn);
	return va;
}

static void
gpu_smmu_tlb(vm_offset_t s)
{
	gpu_wr(s, 0x68u, 1u);	/* sTLBIALLNSNH */
	gpu_wr(s, 0x70u, 1u);	/* sTLBGSYNC */
	for (unsigned i = 0; i < 1000 && (gpu_rd(s, 0x74u) & 1u); ++i) {
		delay(1);
	}
}

/*
 * Minimal GPU-SMMU stage-1 setup for the GMU (SIDs 4/5, context bank 0).
 * IOVA 0..2MB maps one 2MB block that holds the firmware icache/dcache
 * blocks; IOVA 0x60000000 maps the dummy/HFI block. This is deliberately
 * small: no L3 tables, no other SIDs, and the client port is re-enabled
 * only after the tables and CB are written.
 */
static int
gpu_smmu_setup(vm_offset_t s, vm_offset_t *fw_va, vm_offset_t *hfi_va)
{
	uint64_t x_pa = 0, y_pa = 0, pt_pa = 0, x_blk, y_blk;
	vm_offset_t x_va, y_va, pt_va, x_map, y_map;
	uint64_t *l1, *l2a, *l2b, ttbr0;
	uint32_t scr0;

	/* 4MB each: any 4MB range contains a 2MB-aligned 2MB subrange. */
	x_va = gpu_contig(0x400000, 0xfff, &x_pa);
	y_va = gpu_contig(0x400000, 0xfff, &y_pa);
	pt_va = gpu_contig(0x3000, 0xfff, &pt_pa);
	if (x_va == 0 || y_va == 0 || pt_va == 0) {
		printf("SWEET GPU: SMMU memory allocation failed\n");
		if (x_va) {
			kmem_free(kernel_map, x_va, 0x400000);
		}
		if (y_va) {
			kmem_free(kernel_map, y_va, 0x400000);
		}
		if (pt_va) {
			kmem_free(kernel_map, pt_va, 0x3000);
		}
		return ENOMEM;
	}
	x_blk = (x_pa + 0x1fffffull) & ~0x1fffffull;
	y_blk = (y_pa + 0x1fffffull) & ~0x1fffffull;
	if (x_blk + 0x200000ull > x_pa + 0x400000ull ||
	    y_blk + 0x200000ull > y_pa + 0x400000ull) {
		printf("SWEET GPU: SMMU no aligned 2MB block %#x/%#x\n",
		    (uint32_t)x_pa, (uint32_t)y_pa);
		return EINVAL;
	}
	x_map = x_va + (vm_offset_t)(x_blk - x_pa);
	y_map = y_va + (vm_offset_t)(y_blk - y_pa);

	l1 = (uint64_t *)pt_va;
	l2a = (uint64_t *)(pt_va + 0x1000);
	l2b = (uint64_t *)(pt_va + 0x2000);
	l1[0] = (pt_pa + 0x1000) | SMMU_PTE_TABLE;
	l1[1] = (pt_pa + 0x2000) | SMMU_PTE_TABLE;
	l2a[0] = x_blk | SMMU_PTE_BLOCK_ATTR;
	l2b[0x100] = y_blk | SMMU_PTE_BLOCK_ATTR;
	flush_dcache(pt_va, 0x3000, FALSE);

	ttbr0 = (1ull << 48) | (pt_pa & 0x0000fffffffff000ull);
	scr0 = gpu_rd(s, SMMU_GR0_sCR0);
	gpu_wr(s, SMMU_GR0_sCR0, scr0 | 1u);	/* CLIENTPD while programming */
	gpu_smmu_tlb(s);

	gpu_wr(s, 0x1000u, 0x1f300u);		/* CBAR0: S1, WB, no-share */
	gpu_wr(s, 0x1800u, 0x1u);		/* CBA2R0: 64-bit TTBR */
	gpu_wr(s, 0x8000u + 0x10u, 0);		/* TCR2 */
	gpu_wr(s, 0x8000u + 0x30u, 0x803520u);	/* TCR: 4K, T0SZ 32, WB */
	gpu_wr(s, 0x8000u + 0x20u, (uint32_t)ttbr0);
	gpu_wr(s, 0x8000u + 0x24u, (uint32_t)(ttbr0 >> 32));
	gpu_wr(s, 0x8000u + 0x28u, 0);
	gpu_wr(s, 0x8000u + 0x2cu, 0);
	gpu_wr(s, 0x8000u + 0x38u, 0x0000ff00u);	/* MAIR0: attr1 WB */
	gpu_wr(s, 0x8000u + 0x3cu, 0);
	gpu_wr(s, 0x8000u + 0x0u, 0x1067u);	/* SCTLR: M, AFE, TRE, CFIE */

	gpu_wr(s, SMMU_GR0_SMR(0), 0x80000004u);	/* SID 4 */
	gpu_wr(s, SMMU_GR0_S2CR(0), 0x0u);	/* translation, CB0 */
	gpu_wr(s, SMMU_GR0_SMR(1), 0x80000005u);	/* SID 5 */
	gpu_wr(s, SMMU_GR0_S2CR(1), 0x0u);
	gpu_wr(s, SMMU_GR0_sGFSR, 0xffffffffu);
	gpu_smmu_tlb(s);
	gpu_wr(s, SMMU_GR0_sCR0, scr0 & ~1u);	/* enable client port */

	printf("SWEET GPU: smmu map x=%#x/%#x y=%#x/%#x blk=%#x/%#x l1=%#x/%#x\n",
	    (uint32_t)x_pa, (uint32_t)(x_pa >> 32), (uint32_t)y_pa,
	    (uint32_t)(y_pa >> 32), (uint32_t)x_blk, (uint32_t)y_blk,
	    (uint32_t)pt_pa, (uint32_t)(pt_pa >> 32));
	printf("SWEET GPU: smmu cb0 cbar=%#x sctlr=%#x ttbr=%#x/%#x tcr=%#x s2cr=%#x/%#x\n",
	    gpu_rd(s, 0x1000u), gpu_rd(s, 0x8000u),
	    gpu_rd(s, 0x8024u), gpu_rd(s, 0x8020u), gpu_rd(s, 0x8030u),
	    gpu_rd(s, SMMU_GR0_S2CR(0)), gpu_rd(s, SMMU_GR0_S2CR(1)));

	*fw_va = x_map;
	*hfi_va = y_map;
	return 0;
}

/*
 * The GMU firmware rides in the payload ramdisk: mkpayload adds a
 * /chosen/memory-map "RAMDisk" range, so no vendor blob enters git. The
 * firmware is a sequence of {addr, size, type, value, data[]} blocks.
 */
static const uint8_t *
gpu_fw_image(uint32_t *length)
{
	DTEntry entry;
	const void *prop = 0;
	unsigned int size = 0;

	if (SecureDTLookupEntry(0, "/chosen/memory-map", &entry) != kSuccess ||
	    SecureDTGetProperty(entry, "RAMDisk", &prop, &size) != kSuccess ||
	    !prop || size < sizeof(DTMemoryMapRange)) {
		return 0;
	}
	const DTMemoryMapRange *range = prop;
	if (range->paddr == 0 || range->length < 32) {
		return 0;
	}
	*length = (uint32_t)range->length;
	return (const uint8_t *)ml_io_map((vm_offset_t)range->paddr, range->length);
}

static uint32_t
gpu_fw_rd32(const uint8_t *fw, uint32_t off)
{
	return *(const volatile uint32_t *)(fw + off);
}

/*
 * Minimal a6xx HFI queue table at IOVA 0x60001000: table header, two queue
 * headers, command queue at +4K and GMU response queue at +8K. The GMU only
 * needs the descriptors to come up; no HFI message is sent yet.
 */
static void
gpu_hfi_init(vm_offset_t hfi)
{
	uint32_t *t = (uint32_t *)(hfi + 0x1000);
	uint32_t *q0 = t + 6;
	uint32_t *q1 = t + 18;

	t[0] = 0;		/* version */
	t[1] = 30;		/* table size in dwords */
	t[2] = 6;		/* first queue header, in dwords */
	t[3] = 12;		/* queue header size, in dwords */
	t[4] = 2;		/* queues */
	t[5] = 2;		/* active queues */

	q0[0] = 1;		/* status */
	q0[1] = 0x60002000u;	/* data IOVA */
	q0[2] = 0xA00u;		/* type 10, id 0 */
	q0[3] = 0x400u;		/* 4K in dwords */
	q0[4] = 0; q0[5] = 0; q0[6] = 1; q0[7] = 1;
	q0[8] = 1; q0[9] = 0; q0[10] = 0; q0[11] = 0;

	q1[0] = 1;
	q1[1] = 0x60003000u;
	q1[2] = 0xA01u;
	q1[3] = 0x400u;
	q1[4] = 0; q1[5] = 0; q1[6] = 1; q1[7] = 1;
	q1[8] = 1; q1[9] = 0; q1[10] = 0; q1[11] = 0;
}

static void
gpu_reg(vm_offset_t base, uint32_t dword, uint32_t value)
{
	gpu_wr(base, dword * 4u, value);
}

/*
 * a6xx_gmu_rpmh_init for a640: RSCC sequencer uCode + PDC sequencer uCode
 * and the PDC TCS commands that release/retain the GMU power state. The live
 * DT gives PDC cfg 0xb290000 and PDC seq 0xb490000; the RSCC is the AP RSC
 * at 0x18220000 (same block the ARC votes use). a640 uses PDC address 0x30090.
 */
static vm_offset_t
gpu_rpmh_init(void)
{
	vm_offset_t rscc = ml_io_map(0x18220000u, 0x10000u);
	vm_offset_t pdc = ml_io_map(0xb290000u, 0x10000u);
	vm_offset_t seq = ml_io_map(0xb490000u, 0x10000u);

	if (rscc == 0 || pdc == 0 || seq == 0) {
		printf("SWEET GPU: RSCC/PDC mapping failed\n");
		return 0;
	}
	/* Disable SDE clock gating and set up the RSC/PDC handshake. */
	gpu_reg(rscc, 0x4, 1u << 24);
	gpu_reg(rscc, 0xb, 1);
	gpu_reg(rscc, 0xe, 0);
	gpu_reg(rscc, 0xd, 0);
	gpu_reg(rscc, 0xe + 2, 0);
	gpu_reg(rscc, 0xd + 2, 0);
	gpu_reg(rscc, 0xe + 4, 0x80000000u);
	gpu_reg(rscc, 0xd + 4, 0);
	gpu_reg(rscc, 0x100, 0);
	gpu_reg(rscc, 0x8, 0x4520);
	gpu_reg(rscc, 0x9, 0x4510);
	gpu_reg(rscc, 0xa, 0x4514);
	/* RSC sequencer uCode (a640 uses the non-a650 sequence). */
	gpu_reg(rscc, 0x180, 0xa7a506a0u);
	gpu_reg(rscc, 0x181, 0xa1e6a6e7u);
	gpu_reg(rscc, 0x182, 0xa2e081e1u);
	gpu_reg(rscc, 0x183, 0xe9a982e2u);
	gpu_reg(rscc, 0x184, 0x0020e8a8u);
	/* PDC sequencer uCode. */
	gpu_reg(seq, 0x0, 0xfebea1e1u);
	gpu_reg(seq, 0x1, 0xa5a4a3a2u);
	gpu_reg(seq, 0x2, 0x8382a6e0u);
	gpu_reg(seq, 0x3, 0xbce3e284u);
	gpu_reg(seq, 0x4, 0x002081fcu);
	/* PDC TCS1: sleep sequence. */
	gpu_reg(pdc, 0x1573, 7);
	gpu_reg(pdc, 0x1574, 0);
	gpu_reg(pdc, 0x1572, 0);
	gpu_reg(pdc, 0x1575, 0x10108);
	gpu_reg(pdc, 0x1576, 0x30010);
	gpu_reg(pdc, 0x1577, 1);
	gpu_reg(pdc, 0x1579, 0x10108);
	gpu_reg(pdc, 0x157a, 0x30000);
	gpu_reg(pdc, 0x157b, 0);
	gpu_reg(pdc, 0x157d, 0x10108);
	gpu_reg(pdc, 0x157e, 0x30090);
	gpu_reg(pdc, 0x157f, 0);
	/* PDC TCS3: wake sequence. */
	gpu_reg(pdc, 0x15d7, 7);
	gpu_reg(pdc, 0x15d8, 0);
	gpu_reg(pdc, 0x15d6, 0);
	gpu_reg(pdc, 0x15d9, 0x10108);
	gpu_reg(pdc, 0x15da, 0x30010);
	gpu_reg(pdc, 0x15db, 2);
	gpu_reg(pdc, 0x15dd, 0x10108);
	gpu_reg(pdc, 0x15de, 0x30000);
	gpu_reg(pdc, 0x15df, 3);
	gpu_reg(pdc, 0x15e1, 0x10108);
	gpu_reg(pdc, 0x15e2, 0x30090);
	gpu_reg(pdc, 0x15e3, 3);
	gpu_reg(pdc, 0x1148, 0);
	gpu_reg(pdc, 0x1140, 0x80000001u);
	__asm__ volatile("dsb sy" ::: "memory");
	printf("SWEET GPU: rscc/pdc uCode loaded rscc=%#x pdc=%#x seq=%#x\n",
	    (uint32_t)rscc, (uint32_t)pdc, (uint32_t)seq);
	return rscc;
}

/* Vendor a640 a6xx_rpmh_power_on_gpu: ACK the RSC and wait for seq idle. */
static bool
gpu_rscc_handshake(vm_offset_t gmu, vm_offset_t rscc)
{
	uint32_t ack = 0, busy = 1;

	if (rscc == 0) {
		return false;
	}
	gpu_wr(gmu, 0x9307u * 4u, 1u << 1);
	for (unsigned i = 0; i < 100; ++i) {
		ack = gpu_rd(gmu, 0x9308u * 4u);
		if (ack & (1u << 1)) {
			break;
		}
		delay(1);
	}
	if (!(ack & (1u << 1))) {
		printf("SWEET GPU: rscc no ack=%#x\n", ack);
		return false;
	}
	for (unsigned i = 0; i < 100; ++i) {
		busy = gpu_rd(rscc, 0x101u * 4u);
		if (!busy) {
			break;
		}
		delay(1);
	}
	if (busy) {
		printf("SWEET GPU: rscc seq busy=%#x\n", busy);
		return false;
	}
	gpu_wr(gmu, 0x9307u * 4u, 0);
	printf("SWEET GPU: rscc ack=%#x busy=%#x\n", ack, busy);
	return true;
}

/*
 * Load the firmware blocks and cold-boot the CM3. The GMU RCG is left at XO
 * (19.2 MHz) here: the earlier 200 MHz GPLL0 setting may have no live parent
 * on this boot path. icache/dcache/HFI use the minimal SMMU map from
 * gpu_smmu_setup(). The CM3 is reset again afterwards.
 */
static void
gpu_boot_gmu(vm_offset_t gpucc, vm_offset_t gcc)
{
	uint32_t fw_len = 0;
	const uint8_t *fw = gpu_fw_image(&fw_len);
	uint32_t cx_before, gx_before, parent, cmd_before, cfg_before;
	vm_offset_t gmu, fw_va = 0, hfi_va = 0, smmu = 0, rscc = 0;
	uint32_t off, itcm = 0, dtcm = 0, bo = 0, skipped = 0, fault = 0;
	bool layout_ok = true, rscc_ok = false;

	if (fw == 0) {
		printf("SWEET GPU: no GMU firmware RAMDisk; skipping CM3 boot\n");
		return;
	}
	cx_before = gpu_rd(gpucc, GPUCC_CX_GMU_CLK);
	gx_before = gpu_rd(gpucc, GPUCC_GX_GMU_CLK);
	gmu = ml_io_map(GPU_GMU_PA, GPU_GMU_SIZE);
	if (gmu == 0) {
		printf("SWEET GPU: GMU mapping failed\n");
		goto restore;
	}

	/* Reset the GMU block before loading anything into ITCM/DTCM. */
	uint32_t bcr = gpu_rd(gpucc, GPUCC_GMU_BCR);
	gpu_wr(gpucc, GPUCC_GMU_BCR, bcr | 1u);
	delay(1);
	gpu_wr(gpucc, GPUCC_GMU_BCR, bcr & ~1u);
	delay(1);
	printf("SWEET GPU: gmu bcr=%#x->%#x\n", bcr,
	    gpu_rd(gpucc, GPUCC_GMU_BCR));

	/* GMU clock tree: GCC GPLL0 divider -> GPUCC GMU RCG at 200 MHz. */
	parent = gpu_rd(gcc, 0x52004);
	gpu_wr(gcc, 0x52004, parent | (1u << 16) | (1u << 15));
	cmd_before = gpu_rd(gpucc, GPUCC_RCG_CMD);
	cfg_before = gpu_rd(gpucc, GPUCC_RCG_CMD + 4);
	gpu_wr(gpucc, GPUCC_RCG_CMD + 4, 0x0);	/* XO 19.2MHz fallback */
	gpu_wr(gpucc, GPUCC_RCG_CMD, 0x3);		/* update + root enable */
	for (unsigned i = 0; i < 1000; ++i) {
		uint32_t cmd = gpu_rd(gpucc, GPUCC_RCG_CMD);
		if (!(cmd & 0x1u) && !(cmd & 0x80000000u)) {
			break;
		}
		delay(1);
	}
	printf("SWEET GPU: gmu clk parent=%#x->%#x rcg cmd=%#x->%#x cfg=%#x->%#x\n",
	    parent, gpu_rd(gcc, 0x52004), cmd_before,
	    gpu_rd(gpucc, GPUCC_RCG_CMD), cfg_before,
	    gpu_rd(gpucc, GPUCC_RCG_CMD + 4));
	gpu_enable_branch(gpucc, GPUCC_CX_GMU_CLK, "gpu_cc_cx_gmu_clk");
	gpu_enable_branch(gpucc, GPUCC_GX_GMU_CLK, "gpu_cc_gx_gmu_clk");
	int cxerr = sweet_rpmh_arc_set("cx.lvl", 2);
	printf("SWEET GPU: cx.lvl corner 2 %s\n", cxerr ? "failed" : "on");

	smmu = ml_io_map(GPU_SMMU_PA, GPU_SMMU_SIZE);
	if (smmu == 0 || gpu_smmu_setup(smmu, &fw_va, &hfi_va) != 0) {
		printf("SWEET GPU: SMMU setup failed; BO blocks cannot land\n");
		fw_va = 0;
		hfi_va = 0;
		layout_ok = false;
	}
	if (smmu) {
		gpu_wr(smmu, 0x48u, ~0u);	/* clear sGFSR */
		gpu_wr(smmu, 0x8058u, ~0u);	/* clear CB0 FSR */
		fault = gpu_rd(smmu, 0x48u) | gpu_rd(smmu, 0x8058u);
		printf("SWEET GPU: smmu faults start=%#x gfsr=%#x cb0=%#x\n",
		    fault, gpu_rd(smmu, 0x48u), gpu_rd(smmu, 0x8058u));
		if (fault) {
			layout_ok = false;
		}
	}

	gpu_wr(gmu, GMU_GENERAL_7, 1);
	rscc = gpu_rpmh_init();
	rscc_ok = gpu_rscc_handshake(gmu, rscc);
	printf("SWEET GPU: gmu fw head=%#x/%#x/%#x/%#x\n",
	    gpu_fw_rd32(fw, 0), gpu_fw_rd32(fw, 4), gpu_fw_rd32(fw, 8),
	    gpu_fw_rd32(fw, 12));
	for (off = 0; off + 16 <= fw_len; ) {
		uint32_t addr = gpu_fw_rd32(fw, off);
		uint32_t size = gpu_fw_rd32(fw, off + 4);
		const uint8_t *data = fw + off + 16;

		if (size == 0) {
			break;	/* page-aligned RAMDisk tail */
		}
		if ((size & 3u) || size > fw_len - off - 16) {
			printf("SWEET GPU: bad GMU fw block off=%#x addr=%#x size=%#x\n",
			    off, addr, size);
			break;
		}
		if (addr < 0x4000u && size <= 0x4000u - addr) {
			printf("SWEET GPU: fw block %#x+%#x -> ITCM\n", addr, size);
			for (uint32_t i = 0; i < size; i += 4) {
				gpu_wr(gmu, GMU_CM3_ITCM_START + addr + i,
				    gpu_fw_rd32(data, i));
			}
			itcm += size;
		} else if (addr >= 0x40000u && addr < 0x44000u &&
		    size <= 0x44000u - addr) {
			printf("SWEET GPU: fw block %#x+%#x -> DTCM\n", addr, size);
			for (uint32_t i = 0; i < size; i += 4) {
				gpu_wr(gmu, GMU_CM3_DTCM_START +
				    (addr - 0x40000u) + i, gpu_fw_rd32(data, i));
			}
			dtcm += size;
		} else if (fw_va && addr < 0x200000u &&
		    size <= 0x200000u - addr) {
			printf("SWEET GPU: fw block %#x+%#x -> DDR\n", addr, size);
			for (uint32_t i = 0; i < size; i += 4) {
				*(uint32_t *)(fw_va + addr + i) =
				    gpu_fw_rd32(data, i);
			}
			bo += size;
		} else if (hfi_va && addr >= 0x60000000u &&
		    addr < 0x60200000u && size <= 0x60200000u - addr) {
			printf("SWEET GPU: fw block %#x+%#x -> HFI\n", addr, size);
			for (uint32_t i = 0; i < size; i += 4) {
				*(uint32_t *)(hfi_va + (addr - 0x60000000u) + i) =
				    gpu_fw_rd32(data, i);
			}
			bo += size;
		} else {
			printf("SWEET GPU: fw block %#x+%#x -> NO FIT\n", addr, size);
			layout_ok = false;
			skipped += size;
		}
		off += 16 + size;
	}
	if (hfi_va) {
		gpu_hfi_init(hfi_va);
	}
	if (fw_va) {
		flush_dcache(fw_va, 0x200000, FALSE);
	}
	if (hfi_va) {
		flush_dcache(hfi_va, 0x200000, FALSE);
	}
	printf("SWEET GPU: gmu fw itcm=%#x dtcm=%#x bo=%#x skipped=%#x size=%#x\n",
	    itcm, dtcm, bo, skipped, fw_len);
	if (smmu) {
		uint32_t gfsr = gpu_rd(smmu, 0x48u);
		uint32_t cbfsr = gpu_rd(smmu, 0x8058u);
		printf("SWEET GPU: smmu faults copy gfsr=%#x cb0=%#x\n",
		    gfsr, cbfsr);
		if (gfsr || cbfsr) {
			layout_ok = false;
		}
	}

	uint32_t last = gpu_rd(gmu, GMU_DTCM_LAST);
	uint32_t mask = 0xffffffffu, reset = 0xbabefaceu;
	if (last > 0x20010004u) {
		mask = 0x1ffu;
		reset = 0x100u;
	}
	if (!layout_ok || !rscc_ok) {
		printf("SWEET GPU: GMU boot gated layout=%d rscc=%d; not resetting CM3\n",
		    layout_ok, rscc_ok);
		gpu_wr(gmu, GMU_CM3_SYSRESET, 1);
		goto restore;
	}
	gpu_wr(gmu, GMU_CM3_FW_INIT_RESULT, 0);
	gpu_wr(gmu, GMU_CM3_BOOT_CONFIG, 0x02);
	gpu_wr(gmu, GMU_HFI_QTBL_ADDR, hfi_va ? 0x60001000u : 0);
	gpu_wr(gmu, GMU_HFI_QTBL_INFO, hfi_va ? 1u : 0);
	gpu_wr(gmu, GMU_AHB_FENCE_RANGE_0, 0x80a008a0u);
	gpu_wr(gmu, GMU_HFI_SFR_ADDR, 0x06040100u);
	/* Clear the CM3 NMI bit so the CM3 runs __main, not the NMI handler. */
	gpu_wr(gmu, GMU_CM3_CFG, 0x4052u);
	printf("SWEET GPU: gmu cm3-cfg=%#x\n", gpu_rd(gmu, GMU_CM3_CFG));
	/* a6xx_gmu_power_config. */
	gpu_wr(gmu, GMU_SYS_BUS_CONFIG, 1);
	gpu_wr(gmu, GMU_ICACHE_CONFIG, 1);
	gpu_wr(gmu, GMU_DCACHE_CONFIG, 1);
	gpu_wr(gmu, GMU_PWR_COL_INTER_FRAME_CTRL, 0x9c40400u);
	gpu_wr(gmu, GMU_PWR_COL_CP_MSG, 0x60004000u);	/* one 4K log page */
	uint32_t rpmh = gpu_rd(gmu, GMU_RPMH_CTRL);
	gpu_wr(gmu, GMU_RPMH_CTRL, rpmh | 0xf11u);
	printf("SWEET GPU: gmu hfi=%d rpmh=%#x->%#x\n", hfi_va != 0, rpmh,
	    gpu_rd(gmu, GMU_RPMH_CTRL));
	printf("SWEET GPU: gmu pre reset=%#x boot=%#x gen7=%#x itcm0=%#x\n",
	    gpu_rd(gmu, GMU_CM3_SYSRESET), gpu_rd(gmu, GMU_CM3_BOOT_CONFIG),
	    gpu_rd(gmu, GMU_GENERAL_7), gpu_rd(gmu, GMU_CM3_ITCM_START));
	gpu_wr(gmu, GMU_CM3_SYSRESET, 1);
	gpu_wr(gmu, GMU_CM3_PWR_COL_CP_RESP, 0);
	/* Vendor a640 a6xx_gmu_start(): leave WFI before __main can run. */
	gpu_wr(gmu, GMU_CX_GMU_WFI_CONFIG, 0);
	gpu_wr(gmu, GMU_CM3_SYSRESET, 0);
	printf("SWEET GPU: gmu reset-after=%#x\n",
	    gpu_rd(gmu, GMU_CM3_SYSRESET));
	uint32_t init = 0;
	bool init_ok = false;
	for (unsigned i = 0; i < 100; ++i) {
		init = gpu_rd(gmu, GMU_CM3_FW_INIT_RESULT);
		if ((init & mask) == reset) {
			init_ok = true;
			break;
		}
		if (smmu && (gpu_rd(smmu, 0x48u) || gpu_rd(smmu, 0x8058u))) {
			fault = 1;
			break;
		}
		delay(1);
	}
	printf("SWEET GPU: gmu boot last=%#x want=%#x/%#x init=%#x %s\n",
	    last, reset, mask, init, init_ok ? "ok" : "timeout");
	printf("SWEET GPU: gmu boot intr=%#x gmu2host=%#x hfi=%#x fault=%d\n",
	    gpu_rd(gmu, GMU_AO_HOST_INTR_STATUS),
	    gpu_rd(gmu, GMU_GMU2HOST_INTR_INFO),
	    gpu_rd(gmu, GMU_HFI_CTRL_STATUS), fault);
	if (smmu) {
		printf("SWEET GPU: smmu faults end gfsr=%#x cb0=%#x\n",
		    gpu_rd(smmu, 0x48u), gpu_rd(smmu, 0x8058u));
	}
	gpu_wr(gmu, GMU_CM3_SYSRESET, 1);	/* stop the CM3; bounded probe */
restore:
	if (!(cx_before & 1u)) {
		gpu_wr(gpucc, GPUCC_CX_GMU_CLK,
		    gpu_rd(gpucc, GPUCC_CX_GMU_CLK) & ~1u);
	}
	if (!(gx_before & 1u)) {
		gpu_wr(gpucc, GPUCC_GX_GMU_CLK,
		    gpu_rd(gpucc, GPUCC_GX_GMU_CLK) & ~1u);
	}
	printf("SWEET GPU: gmu boot clocks after cx=%#x gx=%#x\n",
	    gpu_rd(gpucc, GPUCC_CX_GMU_CLK),
	    gpu_rd(gpucc, GPUCC_GX_GMU_CLK));
}

/*
 * Power the GX head switch for a bounded register probe. This is the vendor
 * gdsc-regulator enable sequence (SW reset, AON reset, deassert clamp, clear
 * collapse) but without the RPMh gfx.lvl vote and the AOP gpu_noc_wa QMP.
 * It is therefore opt-in only (sweetgpu=3) and collapses GX again afterwards.
 */
static bool
gpu_power_gx(vm_offset_t gpucc)
{
	uint32_t gdscr = gpu_rd(gpucc, GPUCC_GX_GDSCR);
	uint32_t cfg = gpu_rd(gpucc, GPUCC_GX_CFG_GDSCR);

	printf("SWEET GPU: gx before gdscr=%#x cfg=%#x\n", gdscr, cfg);
	if (gdscr & GPUCC_GDSC_HW_CONTROL) {
		printf("SWEET GPU: gx is under hardware control; not touching it\n");
		return false;
	}
	if (cfg & GPUCC_GDSC_PWR_UP) {
		printf("SWEET GPU: gx already powered; probing without reset\n");
		return true;
	}
	if (cfg & GPUCC_GDSC_PWR_DOWN) {
		uint32_t bcr = gpu_rd(gpucc, GPUCC_GX_BCR);
		gpu_wr(gpucc, GPUCC_GX_BCR, bcr | 1u);
		(void)gpu_rd(gpucc, GPUCC_GX_BCR);
		delay(1);
		gpu_wr(gpucc, GPUCC_GX_BCR, bcr & ~1u);
		(void)gpu_rd(gpucc, GPUCC_GX_BCR);
	}

	uint32_t domain = gpu_rd(gpucc, GPUCC_GX_DOMAIN);
	gpu_wr(gpucc, GPUCC_GX_DOMAIN, domain | GPUCC_GDSC_GMEM_RESET);
	(void)gpu_rd(gpucc, GPUCC_GX_DOMAIN);
	delay(1);
	gpu_wr(gpucc, GPUCC_GX_DOMAIN, domain & ~GPUCC_GDSC_GMEM_RESET);
	(void)gpu_rd(gpucc, GPUCC_GX_DOMAIN);

	domain = gpu_rd(gpucc, GPUCC_GX_DOMAIN);
	gpu_wr(gpucc, GPUCC_GX_DOMAIN, domain & ~GPUCC_GDSC_CLAMP_IO);
	(void)gpu_rd(gpucc, GPUCC_GX_DOMAIN);

	gdscr = gpu_rd(gpucc, GPUCC_GX_GDSCR);
	gpu_wr(gpucc, GPUCC_GX_GDSCR, gdscr & ~GPUCC_GDSC_SW_COLLAPSE);
	(void)gpu_rd(gpucc, GPUCC_GX_GDSCR);
	delay(1);

	for (unsigned i = 0; i < 2000; ++i) {
		cfg = gpu_rd(gpucc, GPUCC_GX_CFG_GDSCR);
		if (cfg & GPUCC_GDSC_PWR_UP) {
			break;
		}
		delay(1);
	}
	printf("SWEET GPU: gx after gdscr=%#x cfg=%#x%s\n",
	    gpu_rd(gpucc, GPUCC_GX_GDSCR), cfg,
	    (cfg & GPUCC_GDSC_PWR_UP) ? "" : " (power-up incomplete)");
	if (!(cfg & GPUCC_GDSC_PWR_UP)) {
		gpu_wr(gpucc, GPUCC_GX_GDSCR,
		    gpu_rd(gpucc, GPUCC_GX_GDSCR) | GPUCC_GDSC_SW_COLLAPSE);
		gpu_wr(gpucc, GPUCC_GX_DOMAIN,
		    gpu_rd(gpucc, GPUCC_GX_DOMAIN) | GPUCC_GDSC_CLAMP_IO);
		return false;
	}
	return true;
}

static void
gpu_power_down_gx(vm_offset_t gpucc)
{
	gpu_wr(gpucc, GPUCC_GX_GDSCR,
	    gpu_rd(gpucc, GPUCC_GX_GDSCR) | GPUCC_GDSC_SW_COLLAPSE);
	(void)gpu_rd(gpucc, GPUCC_GX_GDSCR);
	delay(1);
	for (unsigned i = 0; i < 2000; ++i) {
		if (gpu_rd(gpucc, GPUCC_GX_CFG_GDSCR) & GPUCC_GDSC_PWR_DOWN) {
			break;
		}
		delay(1);
	}
	gpu_wr(gpucc, GPUCC_GX_DOMAIN,
	    gpu_rd(gpucc, GPUCC_GX_DOMAIN) | GPUCC_GDSC_CLAMP_IO);
	printf("SWEET GPU: gx collapsed gdscr=%#x cfg=%#x\n",
	    gpu_rd(gpucc, GPUCC_GX_GDSCR), gpu_rd(gpucc, GPUCC_GX_CFG_GDSCR));
}

void
sweet_gpu_init(void)
{
	int stage = 0;
	vm_offset_t gcc, gpucc;

	if (!PE_parse_boot_argn("sweetgpu", &stage, sizeof(stage)) || stage <= 0) {
		return;
	}
	gcc = ml_io_map(GPU_GCC_PA, 0x100000);
	if (gcc == 0) {
		printf("SWEET GPU: GCC mapping failed\n");
		return;
	}
	printf("SWEET GPU: stage=%d\n", stage);
	gpu_enable_branch(gcc, GCC_GPU_CFG_AHB, "gcc_gpu_cfg_ahb_clk");
	if (stage >= 2 && !gpu_clk_on(gpu_rd(gcc, GCC_GPU_CFG_AHB))) {
		printf("SWEET GPU: config AHB still gated; not touching GPUCC\n");
		return;
	}
	gpu_enable_branch(gcc, GCC_GPU_MEMNOC_GFX, "gcc_gpu_memnoc_gfx_clk");
	gpu_enable_branch(gcc, GCC_GPU_SNOC_DVM_GFX, "gcc_gpu_snoc_dvm_gfx_clk");
	if (stage >= 2) {
		gpu_enable_branch(gcc, GCC_DDRSS_GPU_AXI, "gcc_ddrss_gpu_axi_clk");
	}

	gpucc = ml_io_map(GPUCC_PA, 0x9000);
	if (gpucc == 0) {
		printf("SWEET GPU: GPUCC mapping failed\n");
		return;
	}
	if (stage >= 2 && gpu_power_cx(gpucc)) {
		gpu_enable_branch(gpucc, GPUCC_AHB_CLK, "gpu_cc_ahb_clk");
		gpu_enable_branch(gpucc, GPUCC_CXO_CLK, "gpu_cc_cxo_clk");
		if (gpu_clk_on(gpu_rd(gpucc, GPUCC_AHB_CLK))) {
			gpu_probe_core();
			if (stage >= 3) {
				sweet_rpmh_arc_dump();
			}
			if (stage >= 4) {
				/* gfx.lvl levels 16/48/64 from the 0087 hardware dump. */
				static const int corners[] = {1, 2, 3};
				bool probed = false;

				for (unsigned i = 0;
				    i < sizeof(corners) / sizeof(corners[0]); ++i) {
					if (sweet_rpmh_arc_set("gfx.lvl", corners[i]) != 0) {
						break;
					}
					printf("SWEET GPU: gfx.lvl corner %d on; trying GX\n",
					    corners[i]);
					if (gpu_power_gx(gpucc)) {
						gpu_probe_core();
						if (stage >= 5) {
							gpu_probe_gmu(gpucc);
						}
						if (stage >= 6) {
							gpu_probe_smmu();
						}
						if (stage >= 7) {
							gpu_boot_gmu(gpucc, gcc);
						}
						gpu_power_down_gx(gpucc);
						probed = true;
						break;
					}
				}
				if (!probed) {
					printf("SWEET GPU: no gfx.lvl corner powered GX\n");
				}
				if (sweet_rpmh_arc_set("gfx.lvl", 0) != 0) {
					printf("SWEET GPU: gfx.lvl vote off failed\n");
				}
			} else if (stage >= 3 && gpu_power_gx(gpucc)) {
				gpu_probe_core();
				gpu_power_down_gx(gpucc);
			}
		} else {
			printf("SWEET GPU: GPUCC AHB is gated; skipping GPU core reads\n");
		}
	}
	printf("SWEET GPU: gpucc gx_cfg=%#x cx_hw=%#x\n",
	    gpu_rd(gpucc, GPUCC_GX_CFG_GDSCR), gpu_rd(gpucc, GPUCC_CX_HW_CTRL));
	printf("SWEET GPU: gpucc bcr gx=%#x cx=%#x gmu=%#x rcg=%#x\n",
	    gpu_rd(gpucc, GPUCC_GX_BCR), gpu_rd(gpucc, GPUCC_CX_BCR),
	    gpu_rd(gpucc, GPUCC_GMU_BCR), gpu_rd(gpucc, GPUCC_RCG_CMD));
	printf("SWEET GPU: gpucc gdscr gx=%#x cx=%#x\n",
	    gpu_rd(gpucc, GPUCC_GX_GDSCR), gpu_rd(gpucc, GPUCC_CX_GDSCR));
}

#endif /* ARM64_BOARD_CONFIG_GUACAMOLE */
