/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * asustor_gpio_it87.h - functions exported by asustor_gpio_it87.ko
 */

#ifndef ASUSTOR_GPIO_IT87_H
#define ASUSTOR_GPIO_IT87_H

struct gpio_desc;

/*
 * gpio_blink_set callback for leds-gpio (struct gpio_led_platform_data),
 * uses the IT87 GP LED blink units for hardware blinking where possible
 */
int it87_gpio_led_blink_set(struct gpio_desc *desc, int state,
			    unsigned long *delay_on, unsigned long *delay_off);

#endif /* ASUSTOR_GPIO_IT87_H */
