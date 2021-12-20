// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * asustor.c - Platform driver for ASUSTOR NAS hardware
 *
 * Copyright (C) 2021 Mathias Fredriksson <mafredri@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dmi.h>
#include <linux/errno.h>
#include <linux/gpio/machine.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define GPIO_IT87 "gpio_it87"
#define GPIO_ICH "gpio_ich"
#define GPIO_AS6100 "INT33FF:01"

#define AS6100_GPIO_IT87_BASE 161
#define AS600_GPIO_IT87_BASE 448

// ASUSTOR Leds.
// If ledtrig-blkdev ever lands, use that instead of disk-activity:
// https://lore.kernel.org/linux-leds/20210819025053.222710-1-arequipeno@gmail.com/
static struct gpio_led asustor_leds[] = {
	{ .name = "blue:power", .default_state = LEDS_GPIO_DEFSTATE_ON },
	{ .name = "red:power", .default_state = LEDS_GPIO_DEFSTATE_OFF },
	{ .name = "green:status", .default_state = LEDS_GPIO_DEFSTATE_ON },
	{
		.name		 = "red:status",
		.default_state	 = LEDS_GPIO_DEFSTATE_OFF,
		.panic_indicator = 1,
		.default_trigger = "panic",
	},
	{ .name = "blue:usb", .default_state = LEDS_GPIO_DEFSTATE_OFF },
	{ .name = "green:usb", .default_state = LEDS_GPIO_DEFSTATE_OFF },
	{ .name = "blue:lan", .default_state = LEDS_GPIO_DEFSTATE_ON },
	{
		.name		 = "sata1:green:disk",
		.default_state	 = LEDS_GPIO_DEFSTATE_ON,
		.default_trigger = "disk-activity",
	},
	{ .name = "sata1:red:disk", .default_state = LEDS_GPIO_DEFSTATE_OFF },
	{
		.name		 = "sata2:green:disk",
		.default_state	 = LEDS_GPIO_DEFSTATE_ON,
		.default_trigger = "disk-activity",
	},
	{ .name = "sata2:red:disk", .default_state = LEDS_GPIO_DEFSTATE_OFF },
	{
		.name		 = "sata3:green:disk",
		.default_state	 = LEDS_GPIO_DEFSTATE_ON,
		.default_trigger = "disk-activity",
	},
	{ .name = "sata3:red:disk", .default_state = LEDS_GPIO_DEFSTATE_OFF },
	{
		.name		 = "sata4:green:disk",
		.default_state	 = LEDS_GPIO_DEFSTATE_ON,
		.default_trigger = "disk-activity",
	},
	{ .name = "sata4:red:disk", .default_state = LEDS_GPIO_DEFSTATE_OFF },
};

static const struct gpio_led_platform_data asustor_leds_pdata = {
	.leds	  = asustor_leds,
	.num_leds = ARRAY_SIZE(asustor_leds),
};

// clang-format off
static struct gpiod_lookup_table asustor_6100_gpio_leds_lookup = {
	.dev_id = "leds-gpio",
	.table = {
		GPIO_LOOKUP_IDX(GPIO_IT87, 56, NULL, 0, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX(GPIO_IT87, 8, NULL, 1, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX(GPIO_IT87, 31, NULL, 2, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX(GPIO_AS6100, 21, NULL, 3, GPIO_ACTIVE_HIGH),
		// 4
		GPIO_LOOKUP_IDX(GPIO_IT87, 21, NULL, 5, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX(GPIO_IT87, 52, NULL, 6, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(GPIO_AS6100, 24, NULL, 7, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(GPIO_AS6100, 15, NULL, 8, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(GPIO_AS6100, 22, NULL, 9, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(GPIO_AS6100, 19, NULL, 10, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(GPIO_AS6100, 25, NULL, 11, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(GPIO_AS6100, 16, NULL, 12, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(GPIO_AS6100, 18, NULL, 13, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(GPIO_AS6100, 17, NULL, 14, GPIO_ACTIVE_HIGH),
		{}
	},
};

static struct gpiod_lookup_table asustor_600_gpio_leds_lookup = {
	.dev_id = "leds-gpio",
	.table = {
		GPIO_LOOKUP_IDX(GPIO_IT87, 56, NULL, 0, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX(GPIO_IT87, 8, NULL, 1, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX(GPIO_IT87, 31, NULL, 2, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX(GPIO_ICH, 27, NULL, 3, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(GPIO_IT87, 21, NULL, 4, GPIO_ACTIVE_LOW),
		// 5
		GPIO_LOOKUP_IDX(GPIO_IT87, 52, NULL, 6, GPIO_ACTIVE_HIGH),
		{}
	},
};
// clang-format on

// ASUSTOR Buttons.
// Unfortunately, gpio-keys-polled does not use gpio lookup tables.
static struct gpio_keys_button asustor_gpio_keys_table[] = {
	{
		.desc	    = "USB Copy Button",
		.code	    = KEY_COPY,
		.type	    = EV_KEY,
		.active_low = 1,
		.gpio	    = -1, // Invalid, set in init.
	},
	{
		.desc	    = "Power Button",
		.code	    = KEY_POWER,
		.type	    = EV_KEY,
		.active_low = 1,
		.gpio	    = -1, // Invalid, set in init.
	},
};

static struct gpio_keys_platform_data asustor_keys_pdata = {
	.buttons       = asustor_gpio_keys_table,
	.nbuttons      = ARRAY_SIZE(asustor_gpio_keys_table),
	.poll_interval = 50,
	.name	       = "asustor-keys",
};

// clang-format off
static struct gpiod_lookup_table asustor_6100_gpio_keys_lookup = {
	.dev_id = "gpio-keys-polled",
	.table = {
		GPIO_LOOKUP_IDX(GPIO_IT87, 20, NULL, 0, GPIO_ACTIVE_LOW),
		// 1 (Power Button is already handled properly via ACPI).
		{}
	},
};

static struct gpiod_lookup_table asustor_600_gpio_keys_lookup = {
	.dev_id = "gpio-keys-polled",
	.table = {
		GPIO_LOOKUP_IDX(GPIO_IT87, 20, NULL, 0, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX(GPIO_IT87, 27, NULL, 1, GPIO_ACTIVE_LOW),
		{}
	},
};
// clang-format on

// ASUSTOR Platform.
struct asustor_driver_data {
	int gpio_base;
	struct gpiod_lookup_table *leds;
	struct gpiod_lookup_table *keys;
};

static struct asustor_driver_data asustor_6100_driver_data = {
	.gpio_base = AS6100_GPIO_IT87_BASE,
	.leds	   = &asustor_6100_gpio_leds_lookup,
	.keys	   = &asustor_6100_gpio_keys_lookup,
};

static struct asustor_driver_data asustor_600_driver_data = {
	.gpio_base = AS600_GPIO_IT87_BASE,
	.leds	   = &asustor_600_gpio_leds_lookup,
	.keys	   = &asustor_600_gpio_keys_lookup,
};

static const struct dmi_system_id asustor_systems[] = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "AS61xx"),
		},
		.driver_data = &asustor_6100_driver_data,
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ASUSTOR Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "AS-6xxT"),
		},
		.driver_data = &asustor_600_driver_data,
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, asustor_systems);

static struct asustor_driver_data *driver_data;
static struct platform_device *asustor_leds_pdev;
static struct platform_device *asustor_keys_pdev;

static struct platform_device *__init asustor_create_pdev(const char *name,
							  const void *pdata,
							  size_t sz)
{
	struct platform_device *pdev;

	pdev = platform_device_register_data(NULL, name, PLATFORM_DEVID_NONE,
					     pdata, sz);
	if (IS_ERR(pdev))
		pr_err("failed registering %s: %ld\n", name, PTR_ERR(pdev));

	return pdev;
}

// TODO(mafredri): Allow force model for testing.
static int __init asustor_init(void)
{
	const struct dmi_system_id *system;
	const struct gpiod_lookup *keys_table;
	int ret, i;

	system = dmi_first_match(asustor_systems);
	if (!system) {
		pr_info("No supported ASUSTOR mainboard found");
		return -ENODEV;
	}

	pr_info("Found %s/%s\n", system->matches[0].substr,
		system->matches[1].substr);

	driver_data = system->driver_data;
	gpiod_add_lookup_table(driver_data->leds);
	gpiod_add_lookup_table(driver_data->keys);

	for (i = 0; i < ARRAY_SIZE(asustor_gpio_keys_table); i++) {
		// TODO(mafredri): Use gpiod or something less hacky.
		// This is here simply because gpio-keys-polled does
		// not support gpio lookups.
		keys_table = driver_data->keys->table;
		for (; keys_table->key != NULL; keys_table++) {
			if (i == keys_table->idx) {
				asustor_gpio_keys_table[i].gpio =
					driver_data->gpio_base +
					keys_table->chip_hwnum;
			}
		}
	}

	// TODO(mafredri): Handle number of disk slots -> enabled LEDs.
	asustor_leds_pdev = asustor_create_pdev(
		"leds-gpio", &asustor_leds_pdata, sizeof(asustor_leds_pdata));
	if (IS_ERR(asustor_leds_pdev)) {
		ret = PTR_ERR(asustor_leds_pdev);
		goto err;
	}

	asustor_keys_pdev =
		asustor_create_pdev("gpio-keys-polled", &asustor_keys_pdata,
				    sizeof(asustor_keys_pdata));
	if (IS_ERR(asustor_keys_pdev)) {
		ret = PTR_ERR(asustor_keys_pdev);
		platform_device_unregister(asustor_leds_pdev);
		goto err;
	}

	return 0;

err:
	gpiod_remove_lookup_table(driver_data->leds);
	gpiod_remove_lookup_table(driver_data->keys);
	return ret;
}

static void __exit asustor_cleanup(void)
{
	platform_device_unregister(asustor_leds_pdev);
	platform_device_unregister(asustor_keys_pdev);

	gpiod_remove_lookup_table(driver_data->leds);
	gpiod_remove_lookup_table(driver_data->keys);
}

module_init(asustor_init);
module_exit(asustor_cleanup);

MODULE_AUTHOR("Mathias Fredriksson <mafredri@gmail.com>");
MODULE_DESCRIPTION("Platform driver for ASUSTOR NAS hardware");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:asustor");
MODULE_SOFTDEP("pre: it87 gpio-it87 gpio-ich"
	       " platform:leds-gpio"
	       " platform:gpio-keys-polled");
