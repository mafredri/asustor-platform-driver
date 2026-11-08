// SPDX-License-Identifier: GPL-2.0-only
/*
 *  GPIO interface for IT87xx Super I/O chips
 *
 *  Author: Diego Elio Pettenò <flameeyes@flameeyes.eu>
 *  Copyright (c) 2017 Google, Inc.
 *
 *  Based on it87_wdt.c     by Oliver Schuster
 *           gpio-it8761e.c by Denis Turischev
 *           gpio-stmpe.c   by Rabin Vincent
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/leds.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/version.h> // for LINUX_VERSION_CODE and KERNEL_VERSION()

#include "asustor_gpio_it87.h"

/* Chip Id numbers */
#define NO_DEV_ID	0xffff
#define IT8613_ID	0x8613
#define IT8625_ID	0x8625 /* DG: added for IT8625E support */
#define IT8620_ID	0x8620
#define IT8628_ID	0x8628
#define IT8718_ID       0x8718
#define IT8728_ID	0x8728
#define IT8732_ID	0x8732
#define IT8761_ID	0x8761
#define IT8772_ID	0x8772
#define IT8786_ID	0x8786

/* IO Ports */
#define REG		0x2e
#define VAL		0x2f

/* Logical device Numbers LDN */
#define GPIO		0x07

/* Configuration Registers and Functions */
#define LDNREG		0x07
#define CHIPID		0x20
#define CHIPREV		0x22

/*
 * BW: GP LED blink units (LDN 7), see research/LED-Blinking.txt.
 * Pin mapping register: bits 5:0 GP LED location, bits 7:6 must be preserved
 * (bit 7 of 0xf8 is "SMBus isolation" on IT8625E).
 * Control register: bit 0 output low enable, bits 3:1 and 7:6 frequency,
 * bit 4 pin mapping register clear, bit 5 short low pulse enable.
 */
#define IT87_GPIO_BLINK_UNITS		2
#define IT87_GPIO_BLINK_LOCATION	0x3f
#define IT87_GPIO_BLINK_CTRL_KEEP	0x30

static const u8 it87_gpio_blink_pin_map_reg[IT87_GPIO_BLINK_UNITS] = { 0xf8, 0xfa };
static const u8 it87_gpio_blink_ctrl_reg[IT87_GPIO_BLINK_UNITS] = { 0xf9, 0xfb };

/**
 * struct it87_gpio_blink_mode - hardware blink mode of a GP LED blink unit
 * @low_ms: time the pin is driven low per period
 * @high_ms: time the pin is driven high per period
 * @ctrl: frequency bits for the blink control register
 */
struct it87_gpio_blink_mode {
	u16 low_ms;
	u16 high_ms;
	u8 ctrl;
};

/*
 * BW: IT8625E blink modes. The frequency bits and times are from the table in
 * ASUSTOR's GPL kernel source (ADM 4.1.0), which lists "ON" times for LEDs
 * that light up when the pin is low.
 */
static const struct it87_gpio_blink_mode it8625_blink_modes[] = {
	{  125,  125, 0x00 },
	{  500,  500, 0x02 },
	{ 2000, 2000, 0x04 },
	{  250,  250, 0x06 },
	{ 3000, 1000, 0x08 },
	{ 1000, 3000, 0x0a },
	{ 6000, 2000, 0x0c },
	{ 2000, 6000, 0x0e },
	{ 2000,  500, 0x40 },
	{ 1000, 1000, 0x80 },
	{ 4000, 4000, 0xc0 },
};

/**
 * struct it87_gpio - it87-specific GPIO chip
 * @chip: the underlying gpio_chip structure
 * @lock: a lock to avoid races between operations
 * @io_base: base address for gpio ports
 * @io_size: size of the port rage starting from io_base.
 * @output_base: Super I/O register address for Output Enable register
 * @simple_base: Super I/O 'Simple I/O' Enable register
 * @simple_size: Super IO 'Simple I/O' Enable register size; this is
 *	required because IT87xx chips might only provide Simple I/O
 *	switches on a subset of lines, whereas the others keep the
 *	same status all time.
 * @blink_modes: hardware blink modes, NULL if blinking is not supported
 * @num_blink_modes: number of entries in @blink_modes
 */
struct it87_gpio {
	struct gpio_chip chip;
	struct mutex lock;
	u16 io_base;
	u16 io_size;
	u8 output_base;
	u8 simple_base;
	u8 simple_size;
	const struct it87_gpio_blink_mode *blink_modes;
	unsigned int num_blink_modes;
};

/*
 * BW: a mutex instead of a spinlock, because superio_enter() may sleep in
 * request_muxed_region() while the it87 hwmon driver holds the region.
 */
static struct it87_gpio it87_gpio_chip = {
	.lock = __MUTEX_INITIALIZER(it87_gpio_chip.lock),
};

/* Superio chip access functions; copied from wdt_it87 */

static inline int superio_enter(void)
{
	/*
	 * Try to reserve REG and REG + 1 for exclusive access.
	 */
	if (!request_muxed_region(REG, 2, KBUILD_MODNAME))
		return -EBUSY;

	outb(0x87, REG);
	outb(0x01, REG);
	outb(0x55, REG);
	outb(0x55, REG);
	return 0;
}

static inline void superio_exit(void)
{
	outb(0x02, REG);
	outb(0x02, VAL);
	release_region(REG, 2);
}

static inline void superio_select(int ldn)
{
	outb(LDNREG, REG);
	outb(ldn, VAL);
}

static inline int superio_inb(int reg)
{
	outb(reg, REG);
	return inb(VAL);
}

static inline void superio_outb(int val, int reg)
{
	outb(reg, REG);
	outb(val, VAL);
}

static inline int superio_inw(int reg)
{
	int val;

	outb(reg++, REG);
	val = inb(VAL) << 8;
	outb(reg, REG);
	val |= inb(VAL);
	return val;
}

static inline void superio_set_mask(int mask, int reg)
{
	u8 curr_val = superio_inb(reg);
	u8 new_val = curr_val | mask;

	if (curr_val != new_val)
		superio_outb(new_val, reg);
}

static inline void superio_clear_mask(int mask, int reg)
{
	u8 curr_val = superio_inb(reg);
	u8 new_val = curr_val & ~mask;

	if (curr_val != new_val)
		superio_outb(new_val, reg);
}

/* BW: GP LED location of a line, as used by the blink pin mapping registers */
static inline u8 it87_gpio_blink_location(unsigned gpio_num)
{
	return ((gpio_num / 8 + 1) << 3) | (gpio_num % 8);
}

/*
 * BW: stop the blink unit(s) mapped to gpio_num, e.g. by the firmware.
 * Must be called in a superio_enter() session with the GPIO LDN selected.
 */
static void it87_gpio_blink_unmap(struct it87_gpio *it87_gpio,
				  unsigned gpio_num)
{
	u8 loc = it87_gpio_blink_location(gpio_num);
	int i, val;

	if (!it87_gpio->blink_modes)
		return;

	for (i = 0; i < IT87_GPIO_BLINK_UNITS; i++) {
		val = superio_inb(it87_gpio_blink_pin_map_reg[i]);
		if ((val & IT87_GPIO_BLINK_LOCATION) == loc)
			superio_outb(val & ~IT87_GPIO_BLINK_LOCATION,
				     it87_gpio_blink_pin_map_reg[i]);
	}
}

static int it87_gpio_request(struct gpio_chip *chip, unsigned gpio_num)
{
	u8 mask, group;
	int rc = 0;
	struct it87_gpio *it87_gpio = gpiochip_get_data(chip);

	mask = 1 << (gpio_num % 8);
	group = (gpio_num / 8);

	mutex_lock(&it87_gpio->lock);

	rc = superio_enter();
	if (rc)
		goto exit;

	/* BW: the it87 hwmon driver may have selected another LDN */
	superio_select(GPIO);

	/* not all the IT87xx chips support Simple I/O and not all of
	 * them allow all the lines to be set/unset to Simple I/O.
	 */
	if (group < it87_gpio->simple_size)
		superio_set_mask(mask, group + it87_gpio->simple_base);

	it87_gpio_blink_unmap(it87_gpio, gpio_num);

	/* clear output enable, setting the pin to input, as all the
	 * newly-exported GPIO interfaces are set to input.
	 */
	superio_clear_mask(mask, group + it87_gpio->output_base);

	superio_exit();

exit:
	mutex_unlock(&it87_gpio->lock);
	return rc;
}

static int it87_gpio_get(struct gpio_chip *chip, unsigned gpio_num)
{
	u16 reg;
	u8 mask;
	struct it87_gpio *it87_gpio = gpiochip_get_data(chip);

	mask = 1 << (gpio_num % 8);
	reg = (gpio_num / 8) + it87_gpio->io_base;

	return !!(inb(reg) & mask);
}

static int it87_gpio_direction_in(struct gpio_chip *chip, unsigned gpio_num)
{
	u8 mask, group;
	int rc = 0;
	struct it87_gpio *it87_gpio = gpiochip_get_data(chip);

	mask = 1 << (gpio_num % 8);
	group = (gpio_num / 8);

	mutex_lock(&it87_gpio->lock);

	rc = superio_enter();
	if (rc)
		goto exit;

	superio_select(GPIO);

	/* clear the output enable bit */
	superio_clear_mask(mask, group + it87_gpio->output_base);

	superio_exit();

exit:
	mutex_unlock(&it87_gpio->lock);
	return rc;
}

/*
 * BW: the lines of a group share one data register, so the read-modify-write
 * must be done with it87_gpio->lock held.
 */
static void it87_gpio_set_locked(struct it87_gpio *it87_gpio,
				 unsigned gpio_num, int val)
{
	u8 mask, curr_vals;
	u16 reg;

	mask = 1 << (gpio_num % 8);
	reg = (gpio_num / 8) + it87_gpio->io_base;

	curr_vals = inb(reg);
	if (val)
		outb(curr_vals | mask, reg);
	else
		outb(curr_vals & ~mask, reg);
}

// DG: from Kernel 6.17 on, the set callback must return int (0 for success)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
static int it87_gpio_set(struct gpio_chip *chip,
			  unsigned gpio_num, int val)
#else
static void it87_gpio_set(struct gpio_chip *chip,
			  unsigned gpio_num, int val)
#endif
{
	struct it87_gpio *it87_gpio = gpiochip_get_data(chip);

	mutex_lock(&it87_gpio->lock);
	it87_gpio_set_locked(it87_gpio, gpio_num, val);
	mutex_unlock(&it87_gpio->lock);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
	return 0;
#endif
}

static int it87_gpio_direction_out(struct gpio_chip *chip,
				   unsigned gpio_num, int val)
{
	u8 mask, group;
	int rc = 0;
	struct it87_gpio *it87_gpio = gpiochip_get_data(chip);

	mask = 1 << (gpio_num % 8);
	group = (gpio_num / 8);

	mutex_lock(&it87_gpio->lock);

	rc = superio_enter();
	if (rc)
		goto exit;

	superio_select(GPIO);

	/* set the output enable bit */
	superio_set_mask(mask, group + it87_gpio->output_base);

	it87_gpio_set_locked(it87_gpio, gpio_num, val);

	superio_exit();

exit:
	mutex_unlock(&it87_gpio->lock);
	return rc;
}

/* BW: find the hardware blink mode for the requested LED on/off times */
static const struct it87_gpio_blink_mode *
it87_gpio_blink_find_mode(struct it87_gpio *it87_gpio, bool active_low,
			  unsigned long on_ms, unsigned long off_ms)
{
	unsigned long low_ms = active_low ? on_ms : off_ms;
	unsigned long high_ms = active_low ? off_ms : on_ms;
	unsigned int i;

	for (i = 0; i < it87_gpio->num_blink_modes; i++) {
		if (it87_gpio->blink_modes[i].low_ms == low_ms &&
		    it87_gpio->blink_modes[i].high_ms == high_ms)
			return &it87_gpio->blink_modes[i];
	}
	return NULL;
}

/* BW: start blinking gpio_num with one of the (at most two) blink units */
static int it87_gpio_blink_start(struct it87_gpio *it87_gpio,
				 unsigned gpio_num,
				 const struct it87_gpio_blink_mode *mode)
{
	u8 mask = 1 << (gpio_num % 8);
	u8 group = gpio_num / 8;
	u8 loc = it87_gpio_blink_location(gpio_num);
	int rc, i, unit = -1, val;

	mutex_lock(&it87_gpio->lock);

	rc = superio_enter();
	if (rc)
		goto exit;

	superio_select(GPIO);

	/* reuse the unit already blinking this line, else take a free one */
	for (i = 0; i < IT87_GPIO_BLINK_UNITS; i++) {
		val = superio_inb(it87_gpio_blink_pin_map_reg[i]) &
		      IT87_GPIO_BLINK_LOCATION;
		if (val == loc) {
			unit = i;
			break;
		}
		if (val == 0 && unit < 0)
			unit = i;
	}
	if (unit < 0) {
		rc = -EBUSY;
		goto exit_superio;
	}

	val = superio_inb(it87_gpio_blink_ctrl_reg[unit]);
	superio_outb((val & IT87_GPIO_BLINK_CTRL_KEEP) | mode->ctrl,
		     it87_gpio_blink_ctrl_reg[unit]);

	val = superio_inb(it87_gpio_blink_pin_map_reg[unit]);
	superio_outb((val & ~IT87_GPIO_BLINK_LOCATION) | loc,
		     it87_gpio_blink_pin_map_reg[unit]);

	/* the blink unit only drives pins in "alternate function" mode */
	superio_clear_mask(mask, group + it87_gpio->simple_base);

exit_superio:
	superio_exit();
exit:
	mutex_unlock(&it87_gpio->lock);
	return rc;
}

/* BW: stop hardware blinking of gpio_num, if it is blinking */
static int it87_gpio_blink_stop(struct it87_gpio *it87_gpio,
				unsigned gpio_num)
{
	u8 mask = 1 << (gpio_num % 8);
	u8 group = gpio_num / 8;
	int rc;

	mutex_lock(&it87_gpio->lock);

	rc = superio_enter();
	if (rc)
		goto exit;

	superio_select(GPIO);
	superio_set_mask(mask, group + it87_gpio->simple_base);
	it87_gpio_blink_unmap(it87_gpio, gpio_num);
	superio_exit();

exit:
	mutex_unlock(&it87_gpio->lock);
	return rc;
}

/**
 * it87_gpio_led_blink_set() - gpio_blink_set callback for leds-gpio
 * @desc: GPIO of the LED
 * @state: GPIO_LED_BLINK, GPIO_LED_NO_BLINK_LOW or GPIO_LED_NO_BLINK_HIGH
 * @delay_on: requested LED on time in ms, updated if 0
 * @delay_off: requested LED off time in ms, updated if 0
 *
 * BW: Uses the chip's GP LED blink units when the requested times match a
 * hardware mode. Otherwise returns an error, so the LED core falls back to
 * software blinking. Handles GPIOs of other chips too, so it can be set for
 * all LEDs of a leds-gpio device.
 */
int it87_gpio_led_blink_set(struct gpio_desc *desc, int state,
			    unsigned long *delay_on, unsigned long *delay_off)
{
	struct it87_gpio *it87_gpio = &it87_gpio_chip;
	const struct it87_gpio_blink_mode *mode;
	bool ours = gpiod_to_chip(desc) == &it87_gpio->chip;
	unsigned gpio_num = 0;
	int rc;

	if (ours)
		gpio_num = desc_to_gpio(desc) - it87_gpio->chip.base;

	if (state != GPIO_LED_BLINK) {
		if (ours && it87_gpio->blink_modes &&
		    gpio_num / 8 < it87_gpio->simple_size) {
			rc = it87_gpio_blink_stop(it87_gpio, gpio_num);
			if (rc)
				return rc;
		}
		if (gpiod_cansleep(desc))
			gpiod_set_value_cansleep(desc, state);
		else
			gpiod_set_value(desc, state);
		return 0;
	}

	/* only lines with a Simple I/O switch can be given to a blink unit */
	if (!ours || !it87_gpio->blink_modes ||
	    gpio_num / 8 >= it87_gpio->simple_size)
		return -EOPNOTSUPP;

	if (*delay_on == 0 && *delay_off == 0) {
		*delay_on = 500;
		*delay_off = 500;
	}

	mode = it87_gpio_blink_find_mode(it87_gpio, gpiod_is_active_low(desc),
					 *delay_on, *delay_off);
	if (!mode)
		return -EINVAL;

	return it87_gpio_blink_start(it87_gpio, gpio_num, mode);
}
EXPORT_SYMBOL_GPL(it87_gpio_led_blink_set);

static const struct gpio_chip it87_template_chip = {
	.label			= KBUILD_MODNAME,
	.owner			= THIS_MODULE,
	.request		= it87_gpio_request,
	.get			= it87_gpio_get,
	.direction_input	= it87_gpio_direction_in,
	.set			= it87_gpio_set,
	.direction_output	= it87_gpio_direction_out,
	.base			= -1,
	/* BW: all callbacks take it87_gpio->lock, which is a mutex */
	.can_sleep		= true,
};

static int __init it87_gpio_init(void)
{
	int rc = 0, i;
	u16 chip_type;
	u8 chip_rev, gpio_ba_reg;
	char *labels, **labels_table;

	struct it87_gpio *it87_gpio = &it87_gpio_chip;

	rc = superio_enter();
	if (rc)
		return rc;

	chip_type = superio_inw(CHIPID);
	chip_rev  = superio_inb(CHIPREV) & 0x0f;
	superio_exit();

	it87_gpio->chip = it87_template_chip;

	switch (chip_type) {
	case IT8613_ID:
		gpio_ba_reg = 0x62;
		it87_gpio->io_size = 8;  /* it8613 only needs 6, use 8 for alignment */
		it87_gpio->output_base = 0xc8;
		it87_gpio->simple_base = 0xc0;
		it87_gpio->simple_size = 6;
		it87_gpio->chip.ngpio = 64;  /* has 48, use 64 for convenient calc */
		break;
	case IT8625_ID: /* DG: added for IT8625E support */
		gpio_ba_reg = 0x62;
		it87_gpio->io_size = 11;
		it87_gpio->output_base = 0xc8;
		/*
		 * BW: groups 1-5 have Simple I/O switches, like IT8728F.
		 * Verified on AS6704T: 0xc0-0xc4 hold the firmware setup
		 * and 0xc5-0xc7 read 0 while groups 6-8 work as GPIOs.
		 */
		it87_gpio->simple_base = 0xc0;
		it87_gpio->simple_size = 5;
		it87_gpio->chip.ngpio = 64;
		it87_gpio->blink_modes = it8625_blink_modes;
		it87_gpio->num_blink_modes = ARRAY_SIZE(it8625_blink_modes);
		break;
	case IT8620_ID:
	case IT8628_ID:
		gpio_ba_reg = 0x62;
		it87_gpio->io_size = 11;
		it87_gpio->output_base = 0xc8;
		it87_gpio->simple_size = 0;
		it87_gpio->chip.ngpio = 64;
		break;
	case IT8718_ID:
	case IT8728_ID:
	case IT8732_ID:
	case IT8772_ID:
	case IT8786_ID:
		gpio_ba_reg = 0x62;
		it87_gpio->io_size = 8;
		it87_gpio->output_base = 0xc8;
		it87_gpio->simple_base = 0xc0;
		it87_gpio->simple_size = 5;
		it87_gpio->chip.ngpio = 64;
		break;
	case IT8761_ID:
		gpio_ba_reg = 0x60;
		it87_gpio->io_size = 4;
		it87_gpio->output_base = 0xf0;
		it87_gpio->simple_size = 0;
		it87_gpio->chip.ngpio = 16;
		break;
	case NO_DEV_ID:
		pr_err("no device\n");
		return -ENODEV;
	default:
		pr_err("Unknown Chip found, Chip %04x Revision %x\n",
		       chip_type, chip_rev);
		return -ENODEV;
	}

	rc = superio_enter();
	if (rc)
		return rc;

	superio_select(GPIO);

	/* fetch GPIO base address */
	it87_gpio->io_base = superio_inw(gpio_ba_reg);

	superio_exit();

	pr_info("Found Chip IT%04x rev %x. %u GPIO lines starting at %04xh\n",
		chip_type, chip_rev, it87_gpio->chip.ngpio,
		it87_gpio->io_base);

	if (!request_region(it87_gpio->io_base, it87_gpio->io_size,
							KBUILD_MODNAME))
		return -EBUSY;

	/* Set up aliases for the GPIO connection.
	 *
	 * ITE documentation for recent chips such as the IT8728F
	 * refers to the GPIO lines as GPxy, with a coordinates system
	 * where x is the GPIO group (starting from 1) and y is the
	 * bit within the group.
	 *
	 * By creating these aliases, we make it easier to understand
	 * to which GPIO pin we're referring to.
	 */
	labels = kcalloc(it87_gpio->chip.ngpio, sizeof("it87_gpXY"),
								GFP_KERNEL);
	labels_table = kcalloc(it87_gpio->chip.ngpio, sizeof(const char *),
								GFP_KERNEL);

	if (!labels || !labels_table) {
		rc = -ENOMEM;
		goto labels_free;
	}

	for (i = 0; i < it87_gpio->chip.ngpio; i++) {
		char *label = &labels[i * sizeof("it87_gpXY")];

		sprintf(label, "it87_gp%u%u", 1+(i/8), i%8);
		labels_table[i] = label;
	}

	it87_gpio->chip.names = (const char *const*)labels_table;

	rc = gpiochip_add_data(&it87_gpio->chip, it87_gpio);
	if (rc)
		goto labels_free;

	return 0;

labels_free:
	kfree(labels_table);
	kfree(labels);
	release_region(it87_gpio->io_base, it87_gpio->io_size);
	return rc;
}

static void __exit it87_gpio_exit(void)
{
	struct it87_gpio *it87_gpio = &it87_gpio_chip;

	gpiochip_remove(&it87_gpio->chip);
	release_region(it87_gpio->io_base, it87_gpio->io_size);
	kfree(it87_gpio->chip.names[0]);
	kfree(it87_gpio->chip.names);
}

module_init(it87_gpio_init);
module_exit(it87_gpio_exit);

MODULE_AUTHOR("Diego Elio Pettenò <flameeyes@flameeyes.eu>");
MODULE_DESCRIPTION("GPIO interface for IT87xx Super I/O chips");
MODULE_LICENSE("GPL");
