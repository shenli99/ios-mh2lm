/*
 * SWEET process sampler: where is a daemon stuck?
 *
 * The phone has no debugger and daemons log nowhere readable. Every
 * interval a kernel thread prints, for each process named in boot-arg
 * sweetsample (comma separated, default "SpringBoard,backboardd"; "-" = off)
 * and for each of its threads, the user pc/lr/sp and a frame-pointer
 * backtrace as "SMP name[pid] tN pc ... lr ... bt ..." (header line with the
 * shared region slide and the main binary's mach header, to symbolicate
 * with tools/sweet-crash.py). A thread blocked in a syscall shows the
 * syscall stub in pc and its caller in lr/bt.
 *
 * First sample 90 s after start, then every 120 s, 40 samples at most.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/proc_internal.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm64/generic_interrupt_controller.h>
#include <pexpert/arm64/tlmm_gpio.h>

extern task_t proc_task(proc_t);
extern int hz;
extern void sweet_sample_start(void);

static char sweet_sample_names[128];

static bool
sweet_sample_wanted(const char *name)
{
	size_t n = strlen(name);
	for (const char *t = sweet_sample_names; *t;) {
		const char *end = strchr(t, ',');
		size_t tl = end ? (size_t)(end - t) : strlen(t);
		if (tl == n && n > 0 && strncmp(t, name, n) == 0) {
			return true;
		}
		if (!end) {
			break;
		}
		t = end + 1;
	}
	return false;
}

extern void sweet_sample_task(const char *name, int pid, task_t task);  /* osfmk/kern/exception.c */

static int
sweet_sample_proc(proc_t p, __unused void *arg)
{
	char name[32];
	proc_name(proc_pid(p), name, sizeof(name));
	if (sweet_sample_wanted(name)) {
		task_t task = proc_task(p);
		if (task != TASK_NULL) {
			sweet_sample_task(name, proc_pid(p), task);
		}
	}
	return PROC_RETURNED;
}

static void
sweet_sample_thread(__unused void *arg, __unused wait_result_t wr)
{
	static int chan;
	if (!PE_parse_boot_argn("sweetsample", sweet_sample_names, sizeof(sweet_sample_names))) {
		strlcpy(sweet_sample_names, "SpringBoard,backboardd", sizeof(sweet_sample_names));
	}
	(void)tsleep(&chan, PZERO, "sweetsmp", 90 * hz);
	/*
	 * One-shot GIC inventory for the staged interrupt migration: by now the
	 * deferred drivers (USB waits 15 s, TouchComm 20 s, the PMIC mux arms
	 * its SPI when consumers attach after the root mount) have enabled
	 * their sources. Read-only, and independent of whether sampling runs.
	 */
	gic_log_enabled("sample");
	/*
	 * Read-only cross-check of the pad controller. GPIO6/8 belong to
	 * SweetPanelPins through an exclusive AGPIO handle, so these reads prove
	 * the write-exclusive/read-shared split: the values come back even though
	 * a gpio_set_* on the same pads would be refused with EBUSY. GPIO54/122
	 * come from the touch worker's own register path, so their values prove
	 * the tile and slot arithmetic against an independent writer: GPIO122
	 * must read func 0, pull 3, drive 3, output 0
	 * (ts_gpio(122,0,3,3,false,false)).
	 */
	if (tlmm_gpio_available()) {
		static const unsigned int pads[] = { 6, 8, 54, 122 };

		for (unsigned int i = 0; i < sizeof pads / sizeof pads[0]; i++) {
			struct tlmm_pin_config cfg = { 0 };
			int rc = gpio_get_config(pads[i], &cfg);

			printf("SWEET TLMM: gpio%u rc=%d func=%u pull=%u drive=%u output=%d high=%d readonly=%d\n",
			    pads[i], rc, cfg.func, cfg.pull, cfg.drive, cfg.output,
			    cfg.high, gpio_is_read_only(pads[i]));
		}
	} else {
		printf("SWEET TLMM: no controller started; pads untouched\n");
	}
	if (sweet_sample_names[0] == '-') {
		return;
	}
	for (int i = 0; i < 40; i++) {
		printf("SMP --- sample %d\n", i);
		proc_iterate(PROC_ALLPROCLIST, sweet_sample_proc, NULL, NULL, NULL);
		(void)tsleep(&chan, PZERO, "sweetsmp", 120 * hz);
	}
}

void
sweet_sample_start(void)
{
	thread_t thread;
	if (kernel_thread_start(sweet_sample_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}
