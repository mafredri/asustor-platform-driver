// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * asustor_mcu.c - MCU serial communication for ASUSTOR AS68xx (Gen3) NAS hardware
 *
 * Communicates with the onboard AS72XXR MCU via /dev/ttyS1 at 115200 baud (8N1).
 * Provides:
 *   - hwmon interface for fan PWM control
 *   - LED class devices for power and status LEDs
 *
 * Copyright (C) 2026 Thierry Tremblay
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>

#include "asustor_mcu.h"

/* MCU command bytes */
#define MCU_CMD_SET_FAN_PWM	0x30
#define MCU_CMD_GET_FAN		0x31
#define MCU_CMD_GET_FAN_RPM_LO	0x10	/* sub-cmd: RPM low byte */
#define MCU_CMD_GET_FAN_RPM_HI	0x11	/* sub-cmd: RPM high byte */
#define MCU_CMD_SET_LED		0x10
#define MCU_CMD_LED_SUBCMD_LIGHT 0x02
#define MCU_CMD_LED_SUBCMD_PWM	0x03
#define MCU_CMD_GET_VERSION	0x41

/* MCU LED base values (OFF byte) — add mode offset for other states */
#define MCU_LED_POWER_BLUE	0x98
#define MCU_LED_POWER_ORANGE	0x68
#define MCU_LED_STATUS_GREEN	0x70
#define MCU_LED_STATUS_RED	0x78

/* MCU LED mode offsets from base value */
#define MCU_LED_MODE_OFF		0x00
#define MCU_LED_MODE_ON			0x03
#define MCU_LED_MODE_BLINK_SLOW		0x04
#define MCU_LED_MODE_BLINK_MED		0x05
#define MCU_LED_MODE_BLINK_FAST		0x06
#define MCU_LED_MODE_BLINK_FASTEST	0x07

/* MCU communication parameters */
#define MCU_SERIAL_PORT		"/dev/ttyS1"
#define MCU_BAUD_RATE		115200
#define MCU_TX_LEN		3
#define MCU_RX_MAX_LEN		8
#define MCU_RESP_TIMEOUT_MS	500
#define MCU_CMD_DELAY_US	100000	/* 100ms between commands */
#define MCU_FAN_DEFAULT_PWM	0x55

struct asustor_mcu {
	struct mutex lock;	/* serializes MCU communication */
	struct file *tty_filp;
	struct platform_device *pdev;

	/* hwmon */
	struct device *hwmon_dev;
	u8 fan_pwm;		/* cached PWM value */

};

static struct asustor_mcu *mcu_data;

/* ---- Low-level serial I/O ---- */

static struct file *mcu_serial_open(void)
{
	struct file *f;
	struct tty_struct *tty;
	struct ktermios termios;

	f = filp_open(MCU_SERIAL_PORT, O_RDWR | O_NOCTTY | O_NONBLOCK, 0);
	if (IS_ERR(f)) {
		pr_err("failed to open %s: %ld\n", MCU_SERIAL_PORT, PTR_ERR(f));
		return f;
	}

	tty = ((struct tty_file_private *)f->private_data)->tty;
	if (!tty) {
		pr_err("failed to get tty struct\n");
		filp_close(f, NULL);
		return ERR_PTR(-ENODEV);
	}

	/* Configure 115200 8N1 raw */
	termios = tty->termios;
	termios.c_iflag = 0;
	termios.c_oflag = 0;
	termios.c_cflag = B115200 | CS8 | CREAD | CLOCAL;
	termios.c_lflag = 0;
	termios.c_cc[VMIN] = 0;
	termios.c_cc[VTIME] = 5;	/* 500ms timeout */
	tty_set_termios(tty, &termios);

	return f;
}

static void mcu_serial_close(struct file *f)
{
	if (!IS_ERR_OR_NULL(f))
		filp_close(f, NULL);
}

static int mcu_send_cmd(struct asustor_mcu *mcu, const u8 *cmd, int cmd_len,
			u8 *resp, int resp_len)
{
	struct file *f = mcu->tty_filp;
	loff_t pos = 0;
	int ret;
	int i;

	if (IS_ERR_OR_NULL(f))
		return -ENODEV;

	/* Write command */
	ret = kernel_write(f, cmd, cmd_len, &pos);
	if (ret < 0)
		return ret;
	if (ret != cmd_len)
		return -EIO;

	if (!resp || resp_len == 0)
		return 0;

	/* Wait for response */
	usleep_range(MCU_RESP_TIMEOUT_MS * 500, MCU_RESP_TIMEOUT_MS * 1000);

	/* Read response */
	pos = 0;
	memset(resp, 0, resp_len);
	for (i = 0; i < resp_len; i++) {
		ret = kernel_read(f, &resp[i], 1, &pos);
		if (ret <= 0)
			break;
	}

	return i;	/* return number of bytes read */
}

static int mcu_cmd_locked(struct asustor_mcu *mcu, const u8 *cmd, int cmd_len,
			  u8 *resp, int resp_len)
{
	int ret;

	mutex_lock(&mcu->lock);
	ret = mcu_send_cmd(mcu, cmd, cmd_len, resp, resp_len);
	mutex_unlock(&mcu->lock);

	return ret;
}

/* ---- hwmon (fan control) ---- */

static int mcu_fan_get_pwm(struct asustor_mcu *mcu)
{
	u8 cmd[] = { MCU_CMD_GET_FAN, 0x00, 0x00 };
	u8 resp[6];
	int ret;

	ret = mcu_cmd_locked(mcu, cmd, sizeof(cmd), resp, sizeof(resp));
	if (ret >= 6) {
		mcu->fan_pwm = resp[5];
		return resp[5];
	}
	return ret < 0 ? ret : -EIO;
}

static int mcu_fan_get_rpm(struct asustor_mcu *mcu)
{
	u8 cmd_lo[] = { MCU_CMD_GET_FAN, MCU_CMD_GET_FAN_RPM_LO, 0x00 };
	u8 cmd_hi[] = { MCU_CMD_GET_FAN, MCU_CMD_GET_FAN_RPM_HI, 0x00 };
	u8 resp[6];
	int ret;
	u8 lo, hi;

	mutex_lock(&mcu->lock);

	ret = mcu_send_cmd(mcu, cmd_lo, sizeof(cmd_lo), resp, sizeof(resp));
	if (ret < 6) {
		mutex_unlock(&mcu->lock);
		return ret < 0 ? ret : -EIO;
	}
	lo = resp[5];

	ret = mcu_send_cmd(mcu, cmd_hi, sizeof(cmd_hi), resp, sizeof(resp));
	if (ret < 6) {
		mutex_unlock(&mcu->lock);
		return ret < 0 ? ret : -EIO;
	}
	hi = resp[5];

	mutex_unlock(&mcu->lock);

	return (hi << 8) | lo;
}

static int mcu_fan_set_pwm(struct asustor_mcu *mcu, u8 duty)
{
	u8 cmd[] = { MCU_CMD_SET_FAN_PWM, 0x00, duty };
	int ret;

	ret = mcu_cmd_locked(mcu, cmd, sizeof(cmd), NULL, 0);
	if (ret >= 0)
		mcu->fan_pwm = duty;
	return ret;
}

static umode_t asustor_mcu_hwmon_is_visible(const void *data,
					    enum hwmon_sensor_types type,
					    u32 attr, int channel)
{
	if (type == hwmon_pwm) {
		switch (attr) {
		case hwmon_pwm_input:
			return 0644;
		case hwmon_pwm_enable:
			return 0644;
		default:
			return 0;
		}
	}
	if (type == hwmon_fan) {
		switch (attr) {
		case hwmon_fan_input:
			return 0444;
		default:
			return 0;
		}
	}
	return 0;
}

static int asustor_mcu_hwmon_read(struct device *dev,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel, long *val)
{
	struct asustor_mcu *mcu = dev_get_drvdata(dev);

	if (type == hwmon_fan) {
		switch (attr) {
		case hwmon_fan_input:
			*val = mcu_fan_get_rpm(mcu);
			if (*val < 0)
				return *val;
			return 0;
		default:
			return -EOPNOTSUPP;
		}
	}

	if (type != hwmon_pwm)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_pwm_input:
		*val = mcu_fan_get_pwm(mcu);
		if (*val < 0)
			return *val;
		return 0;
	case hwmon_pwm_enable:
		*val = 1;	/* always manual mode */
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int asustor_mcu_hwmon_write(struct device *dev,
				   enum hwmon_sensor_types type,
				   u32 attr, int channel, long val)
{
	struct asustor_mcu *mcu = dev_get_drvdata(dev);

	if (type != hwmon_pwm)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_pwm_input:
		if (val < 0 || val > 255)
			return -EINVAL;
		return mcu_fan_set_pwm(mcu, val);
	case hwmon_pwm_enable:
		/* Only manual mode supported */
		if (val != 1)
			return -EINVAL;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_channel_info *asustor_mcu_hwmon_info[] = {
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT),
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
	NULL
};

static const struct hwmon_ops asustor_mcu_hwmon_ops = {
	.is_visible = asustor_mcu_hwmon_is_visible,
	.read       = asustor_mcu_hwmon_read,
	.write      = asustor_mcu_hwmon_write,
};

static const struct hwmon_chip_info asustor_mcu_hwmon_chip_info = {
	.ops  = &asustor_mcu_hwmon_ops,
	.info = asustor_mcu_hwmon_info,
};

/* ---- LED class devices ---- */

struct mcu_led {
	struct led_classdev cdev;
	u8 base_val;	/* MCU OFF byte — add MCU_LED_MODE_* for other states */
};

static int mcu_led_brightness_set(struct led_classdev *cdev,
				  enum led_brightness brightness)
{
	struct mcu_led *led = container_of(cdev, struct mcu_led, cdev);
	u8 mode = brightness ? MCU_LED_MODE_ON : MCU_LED_MODE_OFF;
	u8 cmd[] = { MCU_CMD_SET_LED, MCU_CMD_LED_SUBCMD_LIGHT,
		     led->base_val + mode };

	return mcu_cmd_locked(mcu_data, cmd, sizeof(cmd), NULL, 0);
}

/* MCU hardware blink speeds — symmetric 50% duty cycle, measured on AS6806T */
static const struct {
	unsigned long delay;	/* half-period in ms */
	u8 mode;
} mcu_blink_speeds[] = {
	{ 500, MCU_LED_MODE_BLINK_SLOW },
	{ 250, MCU_LED_MODE_BLINK_MED },
	{ 125, MCU_LED_MODE_BLINK_FAST },
	{  62, MCU_LED_MODE_BLINK_FASTEST },
};

static int mcu_led_blink_set(struct led_classdev *cdev,
			     unsigned long *delay_on,
			     unsigned long *delay_off)
{
	struct mcu_led *led = container_of(cdev, struct mcu_led, cdev);
	unsigned long period = *delay_on + *delay_off;
	unsigned long best_diff = ~0UL;
	int best = 0;
	int i;
	u8 cmd[3];

	/* Default to slow blink if both delays are zero */
	if (period == 0)
		period = 1000;

	for (i = 0; i < ARRAY_SIZE(mcu_blink_speeds); i++) {
		unsigned long hw_period = 2 * mcu_blink_speeds[i].delay;
		unsigned long diff = (period > hw_period) ?
			period - hw_period : hw_period - period;

		if (diff < best_diff) {
			best = i;
			best_diff = diff;
		}
	}

	*delay_on = mcu_blink_speeds[best].delay;
	*delay_off = mcu_blink_speeds[best].delay;

	cmd[0] = MCU_CMD_SET_LED;
	cmd[1] = MCU_CMD_LED_SUBCMD_LIGHT;
	cmd[2] = led->base_val + mcu_blink_speeds[best].mode;

	return mcu_cmd_locked(mcu_data, cmd, sizeof(cmd), NULL, 0);
}

static struct mcu_led mcu_leds[] = {
	{
		.cdev = {
			.name			 = "blue:power",
			.brightness_set_blocking = mcu_led_brightness_set,
			.blink_set		 = mcu_led_blink_set,
			.max_brightness		 = 1,
			.default_trigger	 = "default-on",
		},
		.base_val = MCU_LED_POWER_BLUE,
	},
	{
		.cdev = {
			.name			 = "orange:power",
			.brightness_set_blocking = mcu_led_brightness_set,
			.blink_set		 = mcu_led_blink_set,
			.max_brightness		 = 1,
		},
		.base_val = MCU_LED_POWER_ORANGE,
	},
	{
		.cdev = {
			.name			 = "green:status",
			.brightness_set_blocking = mcu_led_brightness_set,
			.blink_set		 = mcu_led_blink_set,
			.max_brightness		 = 1,
			.default_trigger	 = "timer",
		},
		.base_val = MCU_LED_STATUS_GREEN,
	},
	{
		.cdev = {
			.name			 = "red:status",
			.brightness_set_blocking = mcu_led_brightness_set,
			.blink_set		 = mcu_led_blink_set,
			.max_brightness		 = 1,
			.default_trigger	 = "panic",
		},
		.base_val = MCU_LED_STATUS_RED,
	},
};

/* ---- Init / Cleanup ---- */

int __init asustor_mcu_init(const char *model_name)
{
	struct asustor_mcu *mcu;
	struct file *f;
	struct platform_device *pdev;
	int ret, i;

	/* Only AS6806T uses the MCU */
	if (strcmp(model_name, "AS6806") != 0)
		return 0;

	pr_info("initializing MCU on %s\n", MCU_SERIAL_PORT);

	mcu = kzalloc(sizeof(*mcu), GFP_KERNEL);
	if (!mcu)
		return -ENOMEM;

	mutex_init(&mcu->lock);

	/* Open serial port */
	f = mcu_serial_open();
	if (IS_ERR(f)) {
		ret = PTR_ERR(f);
		goto err_free;
	}
	mcu->tty_filp = f;
	mcu_data = mcu;

	/* Create a platform device as parent for hwmon */
	pdev = platform_device_register_simple("asustor_mcu", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		pr_err("failed to register platform device: %d\n", ret);
		goto err_serial;
	}
	mcu->pdev = pdev;

	/* Register hwmon for fan control */
	mcu->hwmon_dev = hwmon_device_register_with_info(&pdev->dev, "asustor_mcu",
			mcu, &asustor_mcu_hwmon_chip_info, NULL);
	if (IS_ERR(mcu->hwmon_dev)) {
		ret = PTR_ERR(mcu->hwmon_dev);
		pr_err("failed to register hwmon: %d\n", ret);
		goto err_pdev;
	}

	/* Register MCU LEDs */
	for (i = 0; i < ARRAY_SIZE(mcu_leds); i++) {
		ret = led_classdev_register(NULL, &mcu_leds[i].cdev);
		if (ret) {
			pr_err("failed to register LED %s: %d\n",
			       mcu_leds[i].cdev.name, ret);
			goto err_leds;
		}
	}

	/* Apply a sane startup speed, then read back the current PWM for logging. */
	ret = mcu_fan_set_pwm(mcu, MCU_FAN_DEFAULT_PWM);
	if (ret < 0)
		pr_warn("failed to set default fan PWM: %d\n", ret);
	ret = mcu_fan_get_pwm(mcu);
	if (ret < 0)
		pr_warn("failed to read initial fan PWM: %d\n", ret);
	pr_info("MCU initialized, fan PWM: %d\n", mcu->fan_pwm);

	return 0;

err_leds:
	while (--i >= 0)
		led_classdev_unregister(&mcu_leds[i].cdev);
	hwmon_device_unregister(mcu->hwmon_dev);
err_pdev:
	platform_device_unregister(mcu->pdev);
err_serial:
	mcu_serial_close(mcu->tty_filp);
err_free:
	mcu_data = NULL;
	kfree(mcu);
	return ret;
}

void __exit asustor_mcu_cleanup(void)
{
	struct asustor_mcu *mcu = mcu_data;
	int i;

	if (!mcu)
		return;

	for (i = 0; i < ARRAY_SIZE(mcu_leds); i++)
		led_classdev_unregister(&mcu_leds[i].cdev);

	hwmon_device_unregister(mcu->hwmon_dev);
	platform_device_unregister(mcu->pdev);
	mcu_serial_close(mcu->tty_filp);
	mcu_data = NULL;
	kfree(mcu);

	pr_info("MCU cleaned up\n");
}
