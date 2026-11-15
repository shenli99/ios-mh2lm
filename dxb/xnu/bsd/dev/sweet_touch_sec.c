/*
 * OnePlus 7 Pro (guacamole, SM8150) touch: Samsung S6SY761 at I2C address 0x48
 * on the SE17 lines (gpio55/gpio56, live tree qupv3_se17_i2c_active).
 *
 * Why bit-banging: the serial engine behind those pins has its FIFO (CPU)
 * interface fused off on this platform - GENI_IF_DISABLE_RO (0x64) reads 1,
 * FIFO_IF_DISABLE - so mainline/Android drive it with GPI DMA only. The GENI
 * FIFO path can never start a command (every transfer times out, no NACK),
 * and implementing GPI DMA is out of scope here, so this driver emulates the
 * I2C bus directly on the two GPIOs (push-pull low, external pull-up for the
 * release level; the live tree configures both pins bias-disable, so the bus
 * has external pull-ups). The SE registers are only printed once for the
 * record.
 *
 * Everything else follows the vendor sequence for this board: gpio119 1.8 V
 * enable high, 1 ms, PM8150 L17 through RPMh (cmd-db "ldoa17", 3008 mV),
 * reset gpio54 low/high, wait for the boot-complete event, SENSE_ON, then
 * poll the event stack. The TLMM register map matters: SM8150 splits the
 * 0x03000000 region into four tile arrays (west +0x100000, east +0x500000,
 * north +0x900000, south +0xD00000) with pins at tile + 0x1000 * gpio; the
 * touch pins are SOUTH. Addressing without the tile offset reads zero and
 * drops writes silently - that is what broke every earlier attempt.
 */
#include <pexpert/arm64/sweet_dev.h>

#if defined(ARM64_BOARD_CONFIG_SWEET) && SWEET_DEV_TOUCH_SEC
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
/* Mach MIG declaration is not included by BSD's exported thread.h. */
extern kern_return_t thread_terminate(thread_t);
extern int sweet_rpmh_vreg_enable(const char *res, unsigned mv);
extern void sweet_hid_touch(int finger, int down, int x, int y);
void sweet_touch_init(void);

/* SE17 registers used by the one-shot diagnostic print only. */
#define SE_GENI_FW_REVISION 0x068u
#define SE_GENI_IF_DISABLE  0x064u
#define SE_GENI_IOS         0x908u

/* sec_ts protocol (mainline s6sy761.c + vendor sec_drivers_s6sy761.h). */
#define TS_ADDR             0x48u
#define TS_EVENT_LEN        8u
#define TS_CMD_SENSE_ON     0x10u
#define TS_CMD_SENSE_OFF    0x11u
#define TS_CMD_FW_INTEGRITY 0x21u
#define TS_CMD_PANEL_INFO   0x23u
#define TS_CMD_TOUCH_FUNC   0x30u
#define TS_CMD_DEVICE_ID    0x52u
#define TS_CMD_BOOT_STATUS  0x55u
#define TS_CMD_READ_EVENT   0x60u      /* one event */
#define TS_CMD_READ_ALL     0x61u      /* stacked events */
#define TS_FW_OK            0x80u
#define TS_BS_APPLICATION   0x20u
#define TS_EVENT_INFO       0x02u
#define TS_EVENT_VENDOR     0x07u
#define TS_ACK_BOOT_COMPLETE 0x00u
#define TS_MASK_TOUCH       0x01u
#define TS_MAX_FINGERS      10u

/* Touch pins on this board (live device tree: tp_1v8/tp_rst/tp_irq). */
#define TS_PIN_RESET    54u
#define TS_PIN_1V8      119u
#define TS_PIN_IRQ      122u
/* SE17 I2C lines; the roles are auto-detected at run time. */
#define TS_PIN_I2C_A    55u
#define TS_PIN_I2C_B    56u

/*
 * SM8150 splits the TLMM into four tile arrays inside the 0x03000000 region
 * (vendor and mainline group tables agree), pins at tile + 0x1000 * gpio,
 * ctl at +0 and io (latch/level) at +4. Each tile is mapped separately: one
 * 14 MiB mapping does not fit this kernel's VA space.
 */
enum { TS_TILE_WEST, TS_TILE_EAST, TS_TILE_NORTH, TS_TILE_SOUTH, TS_TILE_COUNT };

static vm_offset_t ts_tiles[TS_TILE_COUNT];

static unsigned
ts_tile(unsigned pin)
{
	if (pin <= 7 || pin == 38 || (pin >= 51 && pin <= 82) ||
	    (pin >= 98 && pin <= 101) || (pin >= 114 && pin <= 124) ||
	    (pin >= 126 && pin <= 154)) {
		return TS_TILE_SOUTH;	/* touch, panel reset, keys */
	}
	if ((pin >= 8 && pin <= 20) || (pin >= 31 && pin <= 37) ||
	    (pin >= 39 && pin <= 42) || (pin >= 83 && pin <= 86) ||
	    (pin >= 88 && pin <= 97) || (pin >= 102 && pin <= 104) ||
	    pin >= 169) {
		return TS_TILE_NORTH;
	}
	if ((pin >= 21 && pin <= 30) || (pin >= 43 && pin <= 50) || pin == 87) {
		return TS_TILE_EAST;
	}
	return TS_TILE_WEST;		/* 105-113, 125, 155-168 */
}

#define TS_GPIO_OFF(n)    ((uint32_t)(n) * 0x1000u)
#define TS_GPIO_IO_OFF(n) (TS_GPIO_OFF(n) + 4u)

static struct {
	vm_offset_t se;
	bool started, dead;
	unsigned events, i2c_errors;
	unsigned sda_pin, scl_pin;
	int vreg_err, ack_err, id_err, sense_err;
	uint8_t id[3];
	int x[TS_MAX_FINGERS], y[TS_MAX_FINGERS];
	bool down[TS_MAX_FINGERS];
} ts;

static uint32_t
ts_rd(vm_offset_t base, uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}

static void
ts_wr(vm_offset_t base, uint32_t off, uint32_t v)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(base + off) = v;
	__asm__ volatile("dsb osh" ::: "memory");
}

static uint32_t
ts_gpio_rd(unsigned pin)
{
	return ts_rd(ts_tiles[ts_tile(pin)], TS_GPIO_OFF(pin));
}

static uint32_t
ts_gpio_io_rd(unsigned pin)
{
	return ts_rd(ts_tiles[ts_tile(pin)], TS_GPIO_IO_OFF(pin));
}

static void
ts_gpio_wr(unsigned pin, uint32_t v)
{
	ts_wr(ts_tiles[ts_tile(pin)], TS_GPIO_OFF(pin), v);
}

static void
ts_gpio_io_wr(unsigned pin, uint32_t v)
{
	ts_wr(ts_tiles[ts_tile(pin)], TS_GPIO_IO_OFF(pin), v);
}

static void
ts_wait(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&ts, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

/* mux 0 = GPIO, pull 0b11 = pull-up, drive 3 = 8 mA. */
static void
ts_gpio(unsigned pin, unsigned mux, unsigned pull, unsigned drive, bool out, bool high)
{
	uint32_t cfg = ts_gpio_rd(pin);
	if (out) {
		/* Latch the level before switching the pin to output. */
		ts_gpio_io_wr(pin, (ts_gpio_io_rd(pin) & ~2u) | (high ? 2u : 0u));
	}
	cfg &= ~0x3ffu;
	cfg |= (mux << 2) | pull | (drive << 6) | (out ? 0x200u : 0u);
	ts_gpio_wr(pin, cfg);
}

/*
 * Open-drain emulation: "low" is an output driving 0, "release" is an input
 * (the external pull-up sets the level). The output latch stays 0 so that
 * enabling the driver always pulls the line down.
 */
static void
bb_level(unsigned pin, bool low)
{
	uint32_t cfg = ts_gpio_rd(pin) & ~0x3ffu;
	if (low) {
		ts_gpio_io_wr(pin, ts_gpio_io_rd(pin) & ~2u);
		cfg |= 0x200u;
	}
	ts_gpio_wr(pin, cfg);
}

static void
bb_sda(unsigned v) { bb_level(ts.sda_pin, !v); }
static void
bb_scl(unsigned v) { bb_level(ts.scl_pin, !v); }
static int
bb_sda_read(void) { return (int)(ts_gpio_io_rd(ts.sda_pin) & 1u); }

/* About 100 kHz: every edge separated by 4 us. */
static void
bb_delay(void) { delay(4); }

static void
bb_start(void)
{
	bb_sda(1);
	bb_scl(1);
	bb_delay();
	bb_sda(0);
	bb_delay();
	bb_scl(0);
	bb_delay();
}

static void
bb_stop(void)
{
	bb_sda(0);
	bb_delay();
	bb_scl(1);
	bb_delay();
	bb_sda(1);
	bb_delay();
}

static void
bb_tx_byte(uint8_t b)
{
	for (int i = 7; i >= 0; --i) {
		bb_sda((b >> i) & 1u);
		bb_delay();
		bb_scl(1);
		bb_delay();
		bb_scl(0);
		bb_delay();
	}
}

static uint8_t
bb_rx_byte(bool ack)
{
	uint8_t v = 0;

	bb_sda(1);
	for (int i = 7; i >= 0; --i) {
		bb_delay();
		bb_scl(1);
		bb_delay();
		v = (uint8_t)((v << 1) | (uint8_t)bb_sda_read());
		bb_scl(0);
	}
	bb_sda(ack ? 0 : 1);
	bb_delay();
	bb_scl(1);
	bb_delay();
	bb_scl(0);
	bb_sda(1);
	bb_delay();
	return v;
}

/* Returns 0 when the target acknowledged the byte just sent. */
static int
bb_ack(void)
{
	int ack;
	bb_sda(1);
	bb_delay();
	bb_scl(1);
	bb_delay();
	ack = bb_sda_read();
	bb_scl(0);
	bb_delay();
	return ack ? ENXIO : 0;
}

static int
bb_write(uint8_t addr, const uint8_t *tx, unsigned n)
{
	int err;
	bb_start();
	bb_tx_byte((uint8_t)(addr << 1));
	err = bb_ack();
	for (unsigned i = 0; i < n && err == 0; ++i) {
		bb_tx_byte(tx[i]);
		err = bb_ack();
	}
	bb_stop();
	return err;
}

static int
bb_read(uint8_t addr, uint8_t reg, uint8_t *buf, unsigned n)
{
	int err;
	bb_start();
	bb_tx_byte((uint8_t)(addr << 1));
	err = bb_ack();
	if (err == 0) {
		bb_tx_byte(reg);
		err = bb_ack();
	}
	if (err == 0) {
		bb_start();	/* repeated start */
		bb_tx_byte((uint8_t)((addr << 1) | 1u));
		err = bb_ack();
	}
	for (unsigned i = 0; i < n && err == 0; ++i) {
		buf[i] = bb_rx_byte(i + 1u < n);
	}
	bb_stop();
	return err;
}

/* Clock nine pulses so a target holding SDA low can let go. */
static void
bb_recover(void)
{
	bb_sda(1);
	for (unsigned i = 0; i < 9; ++i) {
		bb_scl(0);
		bb_delay();
		bb_scl(1);
		bb_delay();
	}
	bb_stop();
}

static int
ts_read(uint8_t reg, uint8_t *buf, unsigned len)
{
	int err = bb_read(TS_ADDR, reg, buf, len);
	if (err) {
		ts.i2c_errors++;
	}
	return err;
}

static int
ts_cmd(uint8_t reg)
{
	int err = bb_write(TS_ADDR, &reg, 1);
	if (err) {
		ts.i2c_errors++;
	}
	return err;
}

static int
ts_write_reg(uint8_t reg, uint8_t val)
{
	uint8_t t[2] = {reg, val};
	int err = bb_write(TS_ADDR, t, 2);
	if (err) {
		ts.i2c_errors++;
	}
	return err;
}

/* 16-bit little-endian payload (i2c_smbus_write_word_data style). */
static int
ts_write_word(uint8_t reg, uint16_t val)
{
	uint8_t t[3] = {reg, (uint8_t)val, (uint8_t)(val >> 8)};
	int err = bb_write(TS_ADDR, t, 3);
	if (err) {
		ts.i2c_errors++;
	}
	return err;
}

/* Read events until the requested information ACK shows up (vendor logic). */
static int
ts_wait_ready(unsigned ack, unsigned retry_cnt)
{
	uint8_t ev[TS_EVENT_LEN] = {0,};
	int retry = 0;

	while (ts_read(TS_CMD_READ_EVENT, ev, sizeof(ev)) == 0) {
		unsigned status = (ev[0] >> 2) & 0xfu;
		if (((status == TS_EVENT_INFO) || (status == TS_EVENT_VENDOR)) &&
		    ev[1] == (uint8_t)ack) {
			return 0;
		}
		if (++retry > (int)retry_cnt) {
			printf("SWEET TOUCH: wait ack %#x timeout, event %02x %02x %02x %02x %02x %02x %02x %02x\n",
			    ack, ev[0], ev[1], ev[2], ev[3], ev[4], ev[5], ev[6], ev[7]);
			break;
		}
		ts_wait(20);
	}
	return -1;
}

/* One event buffer; returns 1 when a frame was forwarded. */
static int
ts_handle_event(const uint8_t *ev)
{
	static unsigned moves;
	if (!ev[0]) {
		return 0;
	}
	unsigned eid = ev[0] & 0x03u;
	if (eid != 0u) {
		return 0; /* status / information / vendor event */
	}
	unsigned tid_raw = (ev[0] & 0x3cu) >> 2;
	if (!tid_raw || tid_raw > TS_MAX_FINGERS) {
		return 0;
	}
	unsigned id = tid_raw - 1u;
	unsigned state = (ev[0] & 0xc0u) >> 6;
	int x = (int)(((unsigned)ev[1] << 4) | (((unsigned)ev[3] & 0xf0u) >> 4));
	int y = (int)(((unsigned)ev[2] << 4) | ((unsigned)ev[3] & 0x0fu));
	switch (state) {
	case 1: /* press */
	case 2: /* move */
		ts.x[id] = x;
		ts.y[id] = y;
		ts.down[id] = true;
		ts.events++;
		if (state == 1 || (++moves % 25u) == 0) {
			printf("SWEET TOUCH: f%u %s %d,%d\n", id,
			    state == 1 ? "down" : "move", x, y);
		}
		sweet_hid_touch((int)id, 1, x, y);
		return 1;
	case 3: /* release */
		if (ts.down[id]) {
			ts.down[id] = false;
			ts.events++;
			printf("SWEET TOUCH: f%u up %d,%d\n", id, ts.x[id], ts.y[id]);
			sweet_hid_touch((int)id, 0, ts.x[id], ts.y[id]);
		}
		return 1;
	default: /* S6SY761_TS_NONE */
		return 0;
	}
}

/* Poll the event stack: one event, then as many as the IC reports waiting. */
static int
ts_poll(void)
{
	uint8_t ev[TS_EVENT_LEN];
	int err = ts_read(TS_CMD_READ_EVENT, ev, sizeof(ev));
	if (err) {
		return err;
	}
	int handled = ts_handle_event(ev);
	unsigned left = ev[7] & 0x3fu;
	if (left) {
		uint8_t stack[TS_MAX_FINGERS * TS_EVENT_LEN];
		if (left > TS_MAX_FINGERS) {
			left = TS_MAX_FINGERS;
		}
		if (ts_read(TS_CMD_READ_ALL, stack, left * TS_EVENT_LEN) == 0) {
			for (unsigned i = 0; i < left; ++i) {
				handled += ts_handle_event(&stack[i * TS_EVENT_LEN]);
			}
		}
	}
	return handled;
}

static void
ts_release_all(void)
{
	for (unsigned i = 0; i < TS_MAX_FINGERS; ++i) {
		if (ts.down[i]) {
			ts.down[i] = false;
			sweet_hid_touch((int)i, 0, ts.x[i], ts.y[i]);
		}
	}
}

static void
ts_thread(void *arg __unused, wait_result_t wr __unused)
{
	unsigned delay_s = 20;
	(void)PE_parse_boot_argn("sweettouchdelay", &delay_s, sizeof(delay_s));
	printf("SWEET TOUCH: starting in %u s\n", delay_s);
	ts_wait(delay_s * 1000u);

	printf("SWEET TOUCH: mapping GCC/TLMM/SE17\n");
	ts_tiles[TS_TILE_WEST] = ml_io_map(SWEET_DEV_TOUCH_NORTH_PA + 0x00100000u, 0x100000);
	ts_tiles[TS_TILE_EAST] = ml_io_map(SWEET_DEV_TOUCH_NORTH_PA + 0x00500000u, 0x100000);
	ts_tiles[TS_TILE_NORTH] = ml_io_map(SWEET_DEV_TOUCH_NORTH_PA + 0x00900000u, 0x100000);
	ts_tiles[TS_TILE_SOUTH] = ml_io_map(SWEET_DEV_TOUCH_NORTH_PA + 0x00d00000u, 0x100000);
	ts.se = ml_io_map(SWEET_DEV_TOUCH_SE_PA, 0x1000);
	if (!ts.se || !ts_tiles[0] || !ts_tiles[1] || !ts_tiles[2] || !ts_tiles[3]) {
		printf("SWEET TOUCH: MMIO mapping failed\n");
		goto out;
	}
	printf("SWEET TOUCH: se17 fw=%#x proto=%u fifo_disabled=%u ios=%#x\n",
	    ts_rd(ts.se, SE_GENI_FW_REVISION),
	    (ts_rd(ts.se, SE_GENI_FW_REVISION) >> 8) & 0xffu,
	    ts_rd(ts.se, SE_GENI_IF_DISABLE) & 1u, ts_rd(ts.se, SE_GENI_IOS));

	ts.sda_pin = TS_PIN_I2C_A;
	ts.scl_pin = TS_PIN_I2C_B;

	printf("SWEET TOUCH: pins reset=%#x/%#x 1v8=%#x/%#x irq=%#x/%#x\n",
	    ts_gpio_rd(TS_PIN_RESET), ts_gpio_io_rd(TS_PIN_RESET),
	    ts_gpio_rd(TS_PIN_1V8), ts_gpio_io_rd(TS_PIN_1V8),
	    ts_gpio_rd(TS_PIN_IRQ), ts_gpio_io_rd(TS_PIN_IRQ));

	/* TLMM write probe on gpio104 (gpio-only in the group table). */
	ts_gpio_wr(104, 0x000000c3u);
	ts_wait(1);
	printf("SWEET TOUCH: tlmm w32 c3 -> %#x\n", ts_gpio_rd(104));

	/* Bus pins released (inputs); external pull-ups hold them high. */
	bb_level(TS_PIN_I2C_A, false);
	bb_level(TS_PIN_I2C_B, false);
	ts_gpio(TS_PIN_IRQ, 0, 0, 3, false, false);   /* input, bias-disable */
	ts_gpio(TS_PIN_1V8, 0, 3, 3, true, true);     /* output high: 1v8 enable */
	ts_wait(1);
	ts.vreg_err = sweet_rpmh_vreg_enable(SWEET_DEV_TOUCH_VREG_NAME, SWEET_DEV_TOUCH_VREG_MV);
	printf("SWEET TOUCH: vreg %s %u mV -> %d\n", SWEET_DEV_TOUCH_VREG_NAME,
	    SWEET_DEV_TOUCH_VREG_MV, ts.vreg_err);
	ts_gpio(TS_PIN_RESET, 0, 0, 3, true, false);  /* reset asserted */
	ts_wait(20);
	ts_gpio(TS_PIN_RESET, 0, 0, 3, true, true);   /* reset released */
	ts_wait(140);                                 /* mainline power-on wait */
	/* A previous session may have left the target holding SDA low. */
	bb_recover();
	printf("SWEET TOUCH: pins now rst=%#x/%#x 1v8=%#x/%#x irq=%#x/%#x bus=%#x/%#x\n",
	    ts_gpio_rd(TS_PIN_RESET), ts_gpio_io_rd(TS_PIN_RESET),
	    ts_gpio_rd(TS_PIN_1V8), ts_gpio_io_rd(TS_PIN_1V8),
	    ts_gpio_rd(TS_PIN_IRQ), ts_gpio_io_rd(TS_PIN_IRQ),
	    ts_gpio_io_rd(TS_PIN_I2C_A), ts_gpio_io_rd(TS_PIN_I2C_B));

	ts.ack_err = ts_wait_ready(TS_ACK_BOOT_COMPLETE, 50);
	if (ts.ack_err) {
		/* The device tree does not say which pin is SDA; try the swap. */
		printf("SWEET TOUCH: no ack with sda=%u scl=%u; swapping\n",
		    ts.sda_pin, ts.scl_pin);
		unsigned t = ts.sda_pin;
		ts.sda_pin = ts.scl_pin;
		ts.scl_pin = t;
		bb_level(TS_PIN_I2C_A, false);
		bb_level(TS_PIN_I2C_B, false);
		ts.ack_err = ts_wait_ready(TS_ACK_BOOT_COMPLETE, 50);
	}
	printf("SWEET TOUCH: boot-complete ack -> %d (sda=%u scl=%u)\n",
	    ts.ack_err, ts.sda_pin, ts.scl_pin);

	uint8_t buf[16];
	ts.id_err = ts_read(TS_CMD_DEVICE_ID, ts.id, 3);
	printf("SWEET TOUCH: device id err=%d %02x %02x %02x (devid %u)\n",
	    ts.id_err, ts.id[0], ts.id[1], ts.id[2],
	    (unsigned)((ts.id[1] << 8) | ts.id[2]));

	uint8_t bs = 0, fw = 0;
	int err = ts_read(TS_CMD_BOOT_STATUS, &bs, 1);
	printf("SWEET TOUCH: boot status err=%d %02x (want %02x)\n", err, bs, TS_BS_APPLICATION);
	err = ts_read(TS_CMD_FW_INTEGRITY, &fw, 1);
	printf("SWEET TOUCH: fw integrity err=%d %02x (want %02x)\n", err, fw, TS_FW_OK);
	err = ts_read(TS_CMD_PANEL_INFO, buf, 11);
	if (!err) {
		printf("SWEET TOUCH: panel max_x=%u max_y=%u tx=%u\n",
		    (unsigned)((buf[0] << 8) | buf[1]), (unsigned)((buf[2] << 8) | buf[3]),
		    buf[8] ? buf[8] : 1u);
	} else {
		printf("SWEET TOUCH: panel info err=%d\n", err);
	}

	err = ts_write_word(TS_CMD_TOUCH_FUNC, TS_MASK_TOUCH);
	printf("SWEET TOUCH: touch func -> %d\n", err);
	ts.sense_err = ts_cmd(TS_CMD_SENSE_ON);
	if (ts.sense_err) {
		printf("SWEET TOUCH: SENSE_ON failed %d\n", ts.sense_err);
	} else {
		printf("SWEET TOUCH: SENSE_ON ok, polling at 10 ms\n");
	}
	printf("SWEET TOUCH: state vreg=%d ack=%d id_err=%d id=%02x%02x%02x sense=%d events=%u\n",
	    ts.vreg_err, ts.ack_err, ts.id_err, ts.id[0], ts.id[1], ts.id[2],
	    ts.sense_err, ts.events);

	unsigned errors = 0, ticks = 0;
	for (;;) {
		int r = ts_poll();
		if (r < 0) {
			if ((errors++ % 100u) == 0) {
				printf("SWEET TOUCH: poll err=%d i2c-errors=%u\n",
				    r, ts.i2c_errors);
			}
			if (errors == 10u) {
				ts_release_all();
				bb_recover();
			}
		} else {
			errors = 0;
		}
		if ((++ticks % 1000u) == 0u) { /* every 10 s: always-visible state */
			printf("SWEET TOUCH: state vreg=%d ack=%d id_err=%d id=%02x%02x%02x sense=%d events=%u errs=%u\n",
			    ts.vreg_err, ts.ack_err, ts.id_err, ts.id[0], ts.id[1], ts.id[2],
			    ts.sense_err, ts.events, ts.i2c_errors);
		}
		ts_wait(10);
	}
out:
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

void
sweet_touch_init(void)
{
	int enabled = 1;
	thread_t thread;

	if (ts.started) {
		return;
	}
	ts.started = true;
	(void)PE_parse_boot_argn("sweettouch", &enabled, sizeof(enabled));
	if (!enabled) {
		printf("SWEET TOUCH: disabled (sweettouch=0)\n");
		return;
	}
	kern_return_t kr = kernel_thread_start(ts_thread, NULL, &thread);
	if (kr == KERN_SUCCESS) {
		thread_deallocate(thread);
	} else {
		printf("SWEET TOUCH: kernel_thread_start failed %d\n", kr);
	}
}
#endif /* ARM64_BOARD_CONFIG_SWEET && SWEET_DEV_TOUCH_SEC */
