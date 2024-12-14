// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * asustor_main.c - main part of asustor.ko, a platform driver for ASUSTOR NAS hardware
 *                  (the other part is in asustor_gpl2.c which is GPL-2.0-only)
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
#include <linux/pci.h>
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
#define NVME_ACT_LED(_name)                                                    \
	{                                                                      \
		.name          = _name ":green:disk",                          \
		.default_state = LEDS_GPIO_DEFSTATE_OFF                        \
	}
#define NVME_ERR_LED(_name)                                                    \
	{                                                                      \
		.name          = _name ":red:disk",                            \
		.default_state = LEDS_GPIO_DEFSTATE_OFF                        \
	}

// clang-format off

// ASUSTOR Leds.
// If ledtrig-blkdev ever lands, use that instead of disk-activity:
// https://lore.kernel.org/linux-leds/20210819025053.222710-1-arequipeno@gmail.com/
// Also, the "disk-activity" trigger does (currently?) *not* trigger for NVME devices.
static struct gpio_led asustor_leds[] = {
	{ .name          = "power:front_panel",                             // 0
	  .default_state = LEDS_GPIO_DEFSTATE_ON },
	{ .name = "power:lcd", .default_state = LEDS_GPIO_DEFSTATE_ON },    // 1
	{ .name = "blue:power", .default_state = LEDS_GPIO_DEFSTATE_ON },   // 2
	{ .name = "red:power", .default_state = LEDS_GPIO_DEFSTATE_OFF },   // 3
	{ .name = "green:status", .default_state = LEDS_GPIO_DEFSTATE_ON }, // 4
	{
		.name            = "red:status",                            // 5
		.default_state   = LEDS_GPIO_DEFSTATE_OFF,
		.panic_indicator = 1,
		.default_trigger = "panic",
	},
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
	NVME_ACT_LED("nvme1"),                                            // 21
	NVME_ERR_LED("nvme1"),                                            // 22
	{ .name = "red:side_inner", .default_state = LEDS_GPIO_DEFSTATE_ON }, // 23
	{ .name = "red:side_mid",   .default_state = LEDS_GPIO_DEFSTATE_ON }, // 24
	{ .name = "red:side_outer", .default_state = LEDS_GPIO_DEFSTATE_ON }, // 25
};

static const struct gpio_led_platform_data asustor_leds_pdata = {
	.leds     = asustor_leds,
	.num_leds = ARRAY_SIZE(asustor_leds),
};

static struct gpiod_lookup_table asustor_fs6700_gpio_leds_lookup = {
	.dev_id = "leds-gpio",
	.table = {
		// 0 - no front panel on this device
		// 1 - no LCD either
										// blue:power also controls the red LED
										// inside the power button on the side
		GPIO_LOOKUP_IDX(GPIO_IT87, 56, NULL,  2, GPIO_ACTIVE_LOW),	// blue:power
		GPIO_LOOKUP_IDX(GPIO_IT87,  8, NULL,  3, GPIO_ACTIVE_LOW),	// red:power
		GPIO_LOOKUP_IDX(GPIO_IT87, 31, NULL,  4, GPIO_ACTIVE_LOW),	// green:status
		GPIO_LOOKUP_IDX(GPIO_IT87, 49, NULL,  5, GPIO_ACTIVE_LOW),	// red:status
		// 6
		// 7
		GPIO_LOOKUP_IDX(GPIO_IT87, 55, NULL,  8, GPIO_ACTIVE_HIGH),	// blue:lan
		// LEDs 9 - 20 don't exist in this system
		GPIO_LOOKUP_IDX(GPIO_IT87, 12, NULL, 21, GPIO_ACTIVE_LOW),	// nvme1:green:disk
		GPIO_LOOKUP_IDX(GPIO_IT87, 13, NULL, 22, GPIO_ACTIVE_LOW),	// nvme1:red:disk
		// red LED strip next to the power button on the side
		GPIO_LOOKUP_IDX(GPIO_IT87, 46, NULL, 23, GPIO_ACTIVE_LOW),	// red:side_inner
		GPIO_LOOKUP_IDX(GPIO_IT87, 47, NULL, 24, GPIO_ACTIVE_LOW),	// red:side_mid
		GPIO_LOOKUP_IDX(GPIO_IT87, 52, NULL, 25, GPIO_ACTIVE_LOW),	// red:side_outer
		{}
	},
};

static struct gpiod_lookup_table asustor_as6702_gpio_leds_lookup = {
	.dev_id = "leds-gpio",
	.table = {
		// 0: AS6702T and AS5402T don't have a front panel to illuminate
		// 1: they don't have a LCD either
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
		{}
	},
};

static struct gpiod_lookup_table asustor_as6704_gpio_leds_lookup = {
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
		{}
	},
};

static struct gpiod_lookup_table asustor_as6706_gpio_leds_lookup = {
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

struct pci_device_match {
	// match PCI devices with the given vendorID and productID (to help identify ASUSTOR systems)
	// you can get them from `lspci -nn`, for example in
	// "00:08.0 System peripheral [0880]: Intel Corporation Device [8086:4e11]"
	// 0x8086 is the vendorID and 0x4e11 is the deviceID
	uint16_t vendorID;
	uint16_t deviceID;

	int16_t min_count; // how often that device should exist at least
	int16_t max_count; // how often that device should exist at most
};

enum {
	DEVICE_COUNT_MAX = 0x7fff // INT16_MAX - used for "no upper limit"
};

// ASUSTOR Platform.
struct asustor_driver_data {
	const char *name; // used for force_device and for some log messages

	struct pci_device_match pci_matches[4];

	struct gpiod_lookup_table *leds;
	struct gpiod_lookup_table *keys;
};

#define VALID_OVERRIDE_NAMES                                                   \
	"AS6xx, AS61xx, AS66xx, AS6702, AS6704, AS6706, FS6706, FS6712"

// NOTE: if you add another device here, update VALID_OVERRIDE_NAMES accordingly!

/*
 * Unfortunately, AS67xx and FS67xx can't be told apart by DMI, they all identify as
 * "Intel Corporation" - "Jasper Lake Client Platform", so we need to match PCI devices.
 *
 * How to tell AS67xx and FS6xx apart:
 *
 * only AS6702T/AS5402T has [8086:4dd3] Intel Corporation Jasper Lake SATA AHCI Controller
 * (only [AF]S67xx: [8086:4dc8] Intel Corporation Jasper Lake HD Audio
 *  - but for now I think AS670xT vs AS540xT doesn't matter. Not sure if AS5404T has this; AS5402T doesn't)
 *
 * only AS6704T has [1b21:1164] ASMedia Technology Inc. ASM1164 Serial ATA AHCI Controller
 * - TODO: does AS5404T also use this? until disproven, I assume it does
 * only AS6706T has [1b21:1166] ASMedia Technology Inc. ASM1166 Serial ATA Controller
 *
 * only FS6712X has [1b21:2806] ASMedia Technology Inc. ASM2806 4-Port PCIe x2 Gen3 Packet Switch
 *              (it doesn't have any SATA controller)
 * FS6706T does not have any SATA controller and no ASMedia PCIe packet switch either
 */

static struct asustor_driver_data asustor_as6702_driver_data = {
	.name = "AS6702",
	.pci_matches = {
		// SATA controller [0106]: Intel Corporation Jasper Lake SATA AHCI Controller [8086:4dd3] (rev 01)
		// Both AS6702T and AS5402T use this SATA controller (the other devices don't)
		{ 0x8086, 0x4dd3, 1, 1 }
	},
	.leds = &asustor_as6702_gpio_leds_lookup,
	.keys = &asustor_6100_gpio_keys_lookup,
};

static struct asustor_driver_data asustor_as6704_driver_data = {
	.name = "AS6704",
	.pci_matches = {
		// SATA controller: ASMedia Technology Inc. ASM1164 Serial ATA AHCI Controller [1b21:1164] (rev 02)
		// This SATA controller is used by AS6704T, and hopefully by AS5404T as well, but
		// not by any of the other AS67xx or FS67xx devices
		{ 0x1b21, 0x1164, 1, 1 }
	},
	.leds = &asustor_as6704_gpio_leds_lookup,
	.keys = &asustor_6100_gpio_keys_lookup,
};

static struct asustor_driver_data asustor_as6706_driver_data = {
	.name = "AS6706",
	.pci_matches = {
		// SATA controller [0106]: ASMedia Technology Inc. ASM1166 Serial ATA Controller [1b21:1166] (rev 02)
		// only used by AS6706T; there (currently?) is no AS5406T
		{ 0x1b21, 0x1166, 1, 1 }
		// (BTW, AS6706T also has 5x "ASM2812 6-Port PCIe x4 Gen3 Packet Switch" [1b21:2812],
		//  which thankfully is NOT the same one that FS6712 uses. Also it allows replacing the
		//  m.2 NVME slots with a 10Gbit NIC, could be that then the packet switch goes away, IDK)
	},
	.leds = &asustor_as6706_gpio_leds_lookup,
	.keys = &asustor_6100_gpio_keys_lookup,
};

static struct asustor_driver_data asustor_fs6712_driver_data = {
	.name = "FS6712",
	.pci_matches = {
		// PCI bridge: ASMedia Technology Inc. ASM2806 4-Port PCIe x2 Gen3 Packet Switch (rev 01)
		// apparently only FS6712X uses this - 15 of those turn up in lspci, at least if
		// all m.2 NVME slots have a SSD installed. I guess it's safest to match that at least
		// one of these exist; upper limit doesn't matter, so just use DEVICE_COUNT_MAX
		{ 0x1b21, 0x2806, 1, DEVICE_COUNT_MAX },
	},
	.leds = &asustor_fs6700_gpio_leds_lookup,
	.keys = &asustor_fs6700_gpio_keys_lookup,
};

static struct asustor_driver_data asustor_fs6706_driver_data = {
	.name = "FS6706",
	.pci_matches = {
		// FS6706T doesn't have that ASMedia PCI bridge / PCIe Packet switch
		{ 0x1b21, 0x2806, 0, 0 },
		// .. it doesn't have any of the SATA controllers either
		{ 0x8086, 0x4dd3, 0, 0 }, // .. not the Intel one used by AS6702T/AS5402T
		{ 0x1b21, 0x1164, 0, 0 }, // .. neither the ASMedia one used by AS6704T
		{ 0x1b21, 0x1166, 0, 0 }, // .. nor the ASMedia one used by AS6706T
	},
	.leds = &asustor_fs6700_gpio_leds_lookup,
	.keys = &asustor_fs6700_gpio_keys_lookup,
};

/*
 * It currently looks like the older systems are easier to tell apart, at least if one doesn't insist
 * on detecting the 2 vs 4 vs 6 drives versions (I only did this for AS67xx because I had to do the
 * advanced detection anyway)
 */

static struct asustor_driver_data asustor_6600_driver_data = {
	// NOTE: This is (currently?) the same as for AS6700
	//       because it seems to use the same GPIO numbers,
	//       but listed extra for the different name
	.name = "AS66xx",
	// This (and the remaining systems) don't need to match PCI devices to be detected,
	// so they're not set here (and thus initialized to all-zero)

	// the LED GPIOs are the same as in AS67xx, so use the one from AS6704 which should work for
	// both AS6602T and AS6604T (an AS66xx with more than 4 drives doesn't seem to exist)
	.leds = &asustor_as6704_gpio_leds_lookup,
	.keys = &asustor_6100_gpio_keys_lookup,
};

static struct asustor_driver_data asustor_6100_driver_data = {
	.name = "AS61xx",
	.leds = &asustor_6100_gpio_leds_lookup,
	.keys = &asustor_6100_gpio_keys_lookup,
};

static struct asustor_driver_data asustor_600_driver_data = {
	.name = "AS6xx",
	.leds = &asustor_600_gpio_leds_lookup,
	.keys = &asustor_600_gpio_keys_lookup,
};

// NOTE: Don't use this table with dmi_first_match(), because it has several entries that
//       match the same (for AS67xx and FS67xx). find_matching_asustor_system() handles
//       that by also matching PCI devices from asustor_driver_data::pci_matches.
//       This table only exists in this form (instead of just using an array of
//       asustor_driver_data) for MODULE_DEVICE_TABLE().
static const struct dmi_system_id asustor_systems[] = {
	// NOTE: each entry in this table must have its own unique asustor_driver_data
	//       (having a unique .name) set as .driver_data

	// The following devices all use the same DMI matches and are actually told apart by
	// our custom matching logic in find_matching_asustor_system() also takes
	// driver_data->pci_matches[] into account.
	// See also the bigger comment above about AS67xx vs FS67xx
	{
		// NOTE: This not only matches (and works with) AS6702T (Lockerstor Gen2),
		//       but also AS5402T (Nimbustor Gen2)
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Jasper Lake Client Platform"),
		},
		.driver_data = &asustor_as6702_driver_data,
	},
	{
		// NOTE: This not only matches (and works with) AS6704T (Lockerstor Gen2),
		//       but (hopefully!) also AS5404T (Nimbustor Gen2)
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Jasper Lake Client Platform"),
		},
		.driver_data = &asustor_as6704_driver_data,
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Jasper Lake Client Platform"),
		},
		.driver_data = &asustor_as6706_driver_data,
	},
	// *F*S67xx:
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Jasper Lake Client Platform"),
		},
		.driver_data = &asustor_fs6706_driver_data,
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Jasper Lake Client Platform"),
		},
		.driver_data = &asustor_fs6712_driver_data,
	},

	// older devices can be matched only by DMI
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "GeminiLake"),
		},
		.driver_data = &asustor_6600_driver_data,
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

// -1 means "not found"
static int get_gpio_base_for_chipname(const char *name)
{
	struct gpio_chip *gc = gpiochip_find((void *)name, gpiochip_match_name);
	return (gc != NULL) ? gc->base : -1;
}
#else
// DG: kernel 6.7 removed gpiochip_find() and introduced gpio_device_find()
//     and friends instead

// -1 means "not found"
static int get_gpio_base_for_chipname(const char *name)
{
	int ret                 = -1;
	struct gpio_device *dev = gpio_device_find_by_label(name);
	if (dev != NULL) {
		ret = gpio_device_get_base(dev);
		// make sure to decrement the reference count of dev
		gpio_device_put(dev);
	}
	return ret;
}
#endif

// How many PCI(e) devices with given vendor/device IDs exist in this system?
static int count_pci_device_instances(unsigned int vendor, unsigned int device)
{
	int ret = 0;
	struct pci_dev *pd, *last_pd = NULL;

	while ((pd = pci_get_device(vendor, device, last_pd)) != NULL) {
		if (last_pd) {
			pci_dev_put(last_pd);
		}
		last_pd = pd;
		++ret;
	}
	if (last_pd) {
		pci_dev_put(last_pd);
	}
	return ret;
}

// check if sys->pci_matches[] match with the PCI devices in the system
// NOTE: this only checks if the devices in sys->pci_matches[] exist in the system
//       with the expected count (or the device does *not* exist if the counts are 0),
//       PCI devices existing that aren't listed in sys->pci_matches[] is expected
//       and does not make this function fail.
static bool pci_devices_match(const struct asustor_driver_data *sys)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(sys->pci_matches); ++i) {
		int dev_cnt;
		const struct pci_device_match *pdm;
		pdm = &sys->pci_matches[i];
		if (pdm->vendorID == 0 && pdm->deviceID == 0) {
			// no more entries, the previous ones matched
			// or we would've returned false already
			return true;
		}
		dev_cnt = count_pci_device_instances(pdm->vendorID,
		                                     pdm->deviceID);
		if (dev_cnt < pdm->min_count || dev_cnt > pdm->max_count) {
			return false;
		}
	}
	return true;
}

// returns true if dmi->matches match on the current system
// implemented in asustor_gpl2.c
extern bool asustor_dmi_matches(const struct dmi_system_id *dmi);

// find out which ASUSTOR system this is, based on asustor_systems[], including
// their linked asustor_driver_data's pci_matches
// returns NULL if this isn't a known system
static const struct dmi_system_id *find_matching_asustor_system(void)
{
	int as_idx;

	for (as_idx = 0; as_idx < ARRAY_SIZE(asustor_systems); as_idx++) {
		struct asustor_driver_data *dd;
		const struct dmi_system_id *sys;
		sys = &asustor_systems[as_idx];
		dd  = sys->driver_data;
		if (dd == NULL) {
			// no driverdata? must be at end of table
			break;
		}

		if (!asustor_dmi_matches(sys)) {
			continue;
		}

		if (!pci_devices_match(dd)) {
			continue;
		}

		// DMI and PCI devices matched => this is it
		return sys;
	}

	return NULL;
}

static char *force_device = "";
module_param(force_device, charp, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(
	force_device,
	"Don't try to detect ASUSTOR device, use the given one instead. "
	"Valid values: " VALID_OVERRIDE_NAMES);

// TODO(mafredri): Allow force model for testing.
static int __init asustor_init(void)
{
	const struct dmi_system_id *system;
	const struct gpiod_lookup *keys_table;
	int ret, i;

	driver_data = NULL;
	// allow overriding detection with force_device kernel parameter
	if (force_device && *force_device) {
		for (i = 0; i < ARRAY_SIZE(asustor_systems); i++) {
			struct asustor_driver_data *dd =
				asustor_systems[i].driver_data;
			if (dd && dd->name &&
			    strcmp(force_device, dd->name) == 0) {
				driver_data = dd;
				break;
			}
		}
		if (driver_data == NULL) {
			pr_err("force_device parameter set to invalid value \"%s\"!\n",
			       force_device);
			pr_info("  valid force_device values are: %s\n",
			        VALID_OVERRIDE_NAMES);
			return -EINVAL;
		}
		pr_info("force_device parameter is set to \"%s\", treating your machine as "
		        "that device instead of trying to detect it!\n",
		        force_device);
	} else { // try to detect the ASUSTOR system
		system = find_matching_asustor_system();
		if (!system) {
			pr_info("No supported ASUSTOR mainboard found");
			return -ENODEV;
		}

		driver_data = system->driver_data;

		pr_info("Found %s or similar (%s/%s)\n", driver_data->name,
		        system->matches[0].substr, system->matches[1].substr);
	}

	gpiod_add_lookup_table(driver_data->leds);
	gpiod_add_lookup_table(driver_data->keys);

	for (i = 0; i < ARRAY_SIZE(asustor_gpio_keys_table); i++) {
		// This is here simply because gpio-keys-polled does
		// not support gpio lookups.
		keys_table = driver_data->keys->table;
		for (; keys_table->key != NULL; keys_table++) {
			if (i == keys_table->idx) {
				// add the GPIO chip's base, so we get the absolute (global) gpio number
				const char *cn = keys_table->key;
				int gpio_base  = get_gpio_base_for_chipname(cn);
				if (gpio_base == -1)
					continue;
				asustor_gpio_keys_table[i].gpio =
					gpio_base + keys_table->chip_hwnum;
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
