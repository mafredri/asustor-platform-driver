// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * asustor.c - Platform driver for ASUSTOR NAS hardware
 *
 * Copyright (C) 2021 Mathias Fredriksson <mafredri@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dmi.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/version.h>

#define GPIO_IT87 "asustor_gpio_it87"
#define GPIO_ICH "gpio_ich"
#define GPIO_AS6100 "INT33FF:01"

#define DISK_ACT_LED(_name)                                                    \
	{                                                                      \
		.name            = _name ":green:disk",                        \
		.default_state   = LEDS_GPIO_DEFSTATE_ON,                      \
		.default_trigger = "disk-activity"                             \
	}
#define DISK_ERR_LED(_name)                                                    \
	{                                                                      \
		.name          = _name ":red:disk",                            \
		.default_state = LEDS_GPIO_DEFSTATE_OFF                        \
	}

// ASUSTOR Leds.
// If ledtrig-blkdev ever lands, use that instead of disk-activity:
// https://lore.kernel.org/linux-leds/20210819025053.222710-1-arequipeno@gmail.com/
static struct gpio_led asustor_leds[] = {
	{ .name          = "power:front_panel",
	  .default_state = LEDS_GPIO_DEFSTATE_ON },                         // 0
	{ .name = "power:lcd", .default_state = LEDS_GPIO_DEFSTATE_ON },    // 1
	{ .name = "blue:power", .default_state = LEDS_GPIO_DEFSTATE_ON },   // 2
	{ .name = "red:power", .default_state = LEDS_GPIO_DEFSTATE_OFF },   // 3
	{ .name = "green:status", .default_state = LEDS_GPIO_DEFSTATE_ON }, // 4
	{
		.name            = "red:status",
		.default_state   = LEDS_GPIO_DEFSTATE_OFF,
		.panic_indicator = 1,
		.default_trigger = "panic",
	},                                                                // 5
	{ .name = "blue:usb", .default_state = LEDS_GPIO_DEFSTATE_OFF },  // 6
	{ .name = "green:usb", .default_state = LEDS_GPIO_DEFSTATE_OFF }, // 7
	{ .name = "blue:lan", .default_state = LEDS_GPIO_DEFSTATE_ON },   // 8
	DISK_ACT_LED("sata1"),                                            // 9
	DISK_ERR_LED("sata1"),                                            // 10
	DISK_ACT_LED("sata2"),                                            // 11
	DISK_ERR_LED("sata2"),                                            // 12
	DISK_ACT_LED("sata3"),                                            // 13
	DISK_ERR_LED("sata3"),                                            // 14
	DISK_ACT_LED("sata4"),                                            // 15
	DISK_ERR_LED("sata4"),                                            // 16
	DISK_ACT_LED("sata5"),                                            // 17
	DISK_ERR_LED("sata5"),                                            // 18
	DISK_ACT_LED("sata6"),                                            // 19
	DISK_ERR_LED("sata6"),                                            // 20
};

static const struct gpio_led_platform_data asustor_leds_pdata = {
	.leds     = asustor_leds,
	.num_leds = ARRAY_SIZE(asustor_leds),
};

// clang-format off
static struct gpiod_lookup_table asustor_fs6700_gpio_leds_lookup = {
	.dev_id = "leds-gpio",
	.table = {
		//GPIO_LOOKUP_IDX(GPIO_IT87, 29, NULL,  0, GPIO_ACTIVE_HIGH),	// power:front_panel
		//GPIO_LOOKUP_IDX(GPIO_IT87, 59, NULL,  1, GPIO_ACTIVE_HIGH),	// power:lcd
		//GPIO_LOOKUP_IDX(GPIO_IT87, 56, NULL,  2, GPIO_ACTIVE_LOW),	// blue:power
		GPIO_LOOKUP_IDX(GPIO_IT87,  8, NULL,  3, GPIO_ACTIVE_LOW),	// red:power
		GPIO_LOOKUP_IDX(GPIO_IT87, 31, NULL,  4, GPIO_ACTIVE_LOW),	// green:status
		//GPIO_LOOKUP_IDX(GPIO_IT87, 49, NULL,  5, GPIO_ACTIVE_LOW),	// red:status
		// 6
		// 7
		//GPIO_LOOKUP_IDX(GPIO_IT87, 55, NULL,  8, GPIO_ACTIVE_HIGH),	// blue:lan
		GPIO_LOOKUP_IDX(GPIO_IT87, 12, NULL,  9, GPIO_ACTIVE_HIGH),	// sata1:green:disk
		GPIO_LOOKUP_IDX(GPIO_IT87, 13, NULL, 10, GPIO_ACTIVE_LOW),	// sata1:red:disk
		{}
	},
};

static struct gpiod_lookup_table asustor_6100_gpio_leds_lookup = {
	.dev_id = "leds-gpio",
	.table = {
		GPIO_LOOKUP_IDX(GPIO_IT87,   29, NULL,  0, GPIO_ACTIVE_HIGH), // power:front_panel
		GPIO_LOOKUP_IDX(GPIO_IT87,   59, NULL,  1, GPIO_ACTIVE_HIGH), // power:lcd
		GPIO_LOOKUP_IDX(GPIO_IT87,   56, NULL,  2, GPIO_ACTIVE_LOW),  // blue:power
		GPIO_LOOKUP_IDX(GPIO_IT87,    8, NULL,  3, GPIO_ACTIVE_LOW),  // red:power
		GPIO_LOOKUP_IDX(GPIO_IT87,   31, NULL,  4, GPIO_ACTIVE_LOW),  // green:status
		GPIO_LOOKUP_IDX(GPIO_AS6100, 21, NULL,  5, GPIO_ACTIVE_HIGH), // red:status
		// 6
		GPIO_LOOKUP_IDX(GPIO_IT87,   21, NULL,  7, GPIO_ACTIVE_LOW),  // green:usb
		GPIO_LOOKUP_IDX(GPIO_IT87,   52, NULL,  8, GPIO_ACTIVE_HIGH), // blue:lan
		GPIO_LOOKUP_IDX(GPIO_AS6100, 24, NULL,  9, GPIO_ACTIVE_LOW),  // sata1:green:disk
		GPIO_LOOKUP_IDX(GPIO_AS6100, 15, NULL, 10, GPIO_ACTIVE_HIGH), // sata1:red:disk
		GPIO_LOOKUP_IDX(GPIO_AS6100, 22, NULL, 11, GPIO_ACTIVE_LOW),  // sata2:green:disk
		GPIO_LOOKUP_IDX(GPIO_AS6100, 19, NULL, 12, GPIO_ACTIVE_HIGH), // sata2:red:disk
		GPIO_LOOKUP_IDX(GPIO_AS6100, 25, NULL, 13, GPIO_ACTIVE_LOW),  // sata3:green:disk
		GPIO_LOOKUP_IDX(GPIO_AS6100, 16, NULL, 14, GPIO_ACTIVE_HIGH), // sata3:red:disk
		GPIO_LOOKUP_IDX(GPIO_AS6100, 18, NULL, 15, GPIO_ACTIVE_LOW),  // sata4:green:disk
		GPIO_LOOKUP_IDX(GPIO_AS6100, 17, NULL, 16, GPIO_ACTIVE_HIGH), // sata4:red:disk
		{}
	},
};

static struct gpiod_lookup_table asustor_600_gpio_leds_lookup = {
	.dev_id = "leds-gpio",
	.table = {
		GPIO_LOOKUP_IDX(GPIO_IT87, 29, NULL, 0, GPIO_ACTIVE_HIGH), // power:front_panel
		GPIO_LOOKUP_IDX(GPIO_IT87, 59, NULL, 1, GPIO_ACTIVE_HIGH), // power:lcd
		GPIO_LOOKUP_IDX(GPIO_IT87, 56, NULL, 2, GPIO_ACTIVE_LOW),  // blue:power
		GPIO_LOOKUP_IDX(GPIO_IT87,  8, NULL, 3, GPIO_ACTIVE_LOW),  // red:power
		GPIO_LOOKUP_IDX(GPIO_IT87, 31, NULL, 4, GPIO_ACTIVE_LOW),  // green:status
		GPIO_LOOKUP_IDX(GPIO_ICH,  27, NULL, 5, GPIO_ACTIVE_HIGH), // red:status
		GPIO_LOOKUP_IDX(GPIO_IT87, 21, NULL, 6, GPIO_ACTIVE_LOW),  // blue:usb
		// 7
		GPIO_LOOKUP_IDX(GPIO_IT87, 52, NULL, 8, GPIO_ACTIVE_HIGH), // blue:lan
		{}
	},
};

static struct gpiod_lookup_table asustor_6700_gpio_leds_lookup = {
	.dev_id = "leds-gpio",
	.table = {
		GPIO_LOOKUP_IDX(GPIO_IT87, 29, NULL,  0, GPIO_ACTIVE_HIGH),	// power:front_panel
		GPIO_LOOKUP_IDX(GPIO_IT87, 59, NULL,  1, GPIO_ACTIVE_HIGH),	// power:lcd
		GPIO_LOOKUP_IDX(GPIO_IT87, 56, NULL,  2, GPIO_ACTIVE_LOW),	// blue:power
		GPIO_LOOKUP_IDX(GPIO_IT87,  8, NULL,  3, GPIO_ACTIVE_LOW),	// red:power
		GPIO_LOOKUP_IDX(GPIO_IT87, 31, NULL,  4, GPIO_ACTIVE_LOW),	// green:status
		GPIO_LOOKUP_IDX(GPIO_IT87, 49, NULL,  5, GPIO_ACTIVE_LOW),	// red:status
		// 6
		GPIO_LOOKUP_IDX(GPIO_IT87, 21, NULL,  7, GPIO_ACTIVE_LOW),	// green:usb
		GPIO_LOOKUP_IDX(GPIO_IT87, 55, NULL,  8, GPIO_ACTIVE_HIGH),	// blue:lan
		GPIO_LOOKUP_IDX(GPIO_IT87, 12, NULL,  9, GPIO_ACTIVE_HIGH),	// sata1:green:disk
		GPIO_LOOKUP_IDX(GPIO_IT87, 13, NULL, 10, GPIO_ACTIVE_LOW),	// sata1:red:disk
		GPIO_LOOKUP_IDX(GPIO_IT87, 46, NULL, 11, GPIO_ACTIVE_HIGH),	// sata2:green:disk
		GPIO_LOOKUP_IDX(GPIO_IT87, 47, NULL, 12, GPIO_ACTIVE_LOW),	// sata2:red:disk
		GPIO_LOOKUP_IDX(GPIO_IT87, 51, NULL, 13, GPIO_ACTIVE_HIGH),	// sata3:green:disk
		GPIO_LOOKUP_IDX(GPIO_IT87, 52, NULL, 14, GPIO_ACTIVE_LOW),	// sata3:red:disk
		GPIO_LOOKUP_IDX(GPIO_IT87, 63, NULL, 15, GPIO_ACTIVE_HIGH),	// sata4:green:disk
		GPIO_LOOKUP_IDX(GPIO_IT87, 48, NULL, 16, GPIO_ACTIVE_LOW),	// sata4:red:disk
		GPIO_LOOKUP_IDX(GPIO_IT87, 61, NULL, 17, GPIO_ACTIVE_HIGH),	// sata5:green:disk
		GPIO_LOOKUP_IDX(GPIO_IT87, 62, NULL, 18, GPIO_ACTIVE_LOW),	// sata5:red:disk
		GPIO_LOOKUP_IDX(GPIO_IT87, 58, NULL, 19, GPIO_ACTIVE_HIGH),	// sata6:green:disk
		GPIO_LOOKUP_IDX(GPIO_IT87, 60, NULL, 20, GPIO_ACTIVE_LOW),	// sata6:red:disk
		{}
	},
};
// clang-format on

// ASUSTOR Buttons.
// Unfortunately, gpio-keys-polled does not use gpio lookup tables.
static struct gpio_keys_button asustor_gpio_keys_table[] = {
	{
		.desc       = "USB Copy Button",
		.code       = KEY_COPY,
		.type       = EV_KEY,
		.active_low = 1,
		.gpio       = -1, // Invalid, set in init.
	},
	{
		.desc       = "Power Button",
		.code       = KEY_POWER,
		.type       = EV_KEY,
		.active_low = 1,
		.gpio       = -1, // Invalid, set in init.
	},
};

static struct gpio_keys_platform_data asustor_keys_pdata = {
	.buttons       = asustor_gpio_keys_table,
	.nbuttons      = ARRAY_SIZE(asustor_gpio_keys_table),
	.poll_interval = 50,
	.name          = "asustor-keys",
};

// clang-format off
static struct gpiod_lookup_table asustor_fs6700_gpio_keys_lookup = {
	.dev_id = "gpio-keys-polled",
	.table = {
		// 0 (There is no USB Copy Button).
		// 1 (Power Button is already handled properly via ACPI).
		{}
	},
};

static struct gpiod_lookup_table asustor_6100_gpio_keys_lookup = { // same for 6700
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
	struct gpiod_lookup_table *leds;
	struct gpiod_lookup_table *keys;
};

static struct asustor_driver_data asustor_fs6700_driver_data = {
	.leds = &asustor_fs6700_gpio_leds_lookup,
	.keys = &asustor_fs6700_gpio_keys_lookup,
};

static struct asustor_driver_data asustor_6700_driver_data = {
	.leds = &asustor_6700_gpio_leds_lookup,
	.keys = &asustor_6100_gpio_keys_lookup,
};

static struct asustor_driver_data asustor_6100_driver_data = {
	.leds = &asustor_6100_gpio_leds_lookup,
	.keys = &asustor_6100_gpio_keys_lookup,
};

static struct asustor_driver_data asustor_600_driver_data = {
	.leds = &asustor_600_gpio_leds_lookup,
	.keys = &asustor_600_gpio_keys_lookup,
};

static const struct dmi_system_id asustor_systems[] = {
	{
		// Note: This uses the BIOS release date to help match the FS67xx,
		//       because otherwise it matches the AS670xT, AS540xT and others
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Jasper Lake Client Platform"),
			DMI_EXACT_MATCH(DMI_BIOS_DATE, "09/15/2023"),
		},
		.driver_data = &asustor_fs6700_driver_data,
	},
	{
		// Note: This not only matches (and works with) AS670xT (Lockerstore Gen2),
		//       but also AS540xT (Nimbustor Gen2)
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Jasper Lake Client Platform"),
		},
		.driver_data = &asustor_6700_driver_data,
	},
	{
		// Note: The same also seemed to work with AS6602T,
		//	 though I can't test that anymore
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "GeminiLake"),
		},
		.driver_data = &asustor_6700_driver_data,
	},
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 7, 0)
static int gpiochip_match_name(struct gpio_chip *chip, void *data)
{
	const char *name = data;

	return !strcmp(chip->label, name);
}

static struct gpio_chip *find_chip_by_name(const char *name)
{
	return gpiochip_find((void *)name, gpiochip_match_name);
}
#else
// DG: kernel 6.7 removed gpiochip_find() and introduced gpio_device_find()
//     and friends instead
static struct gpio_chip *find_chip_by_name(const char *name)
{
	struct gpio_device *dev = gpio_device_find_by_label(name);
	return (dev != NULL) ? gpio_device_get_chip(dev) : NULL;
}
#endif

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
		// This is here simply because gpio-keys-polled does
		// not support gpio lookups.
		keys_table = driver_data->keys->table;
		for (; keys_table->key != NULL; keys_table++) {
			if (i == keys_table->idx) {
				// add the GPIO chip's base, so we get the absolute (global) gpio number
				struct gpio_chip *chip =
					find_chip_by_name(keys_table->key);
				if (chip == NULL)
					continue;
				asustor_gpio_keys_table[i].gpio =
					chip->base + keys_table->chip_hwnum;
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
MODULE_SOFTDEP("pre: asustor-it87 asustor-gpio-it87 gpio-ich"
               " platform:leds-gpio"
               " platform:gpio-keys-polled");
