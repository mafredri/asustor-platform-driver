// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * asustor_lcm.c - Front-panel LCD module for ASUSTOR NAS hardware
 *
 * Communicates with the front-panel LCD controller via /dev/ttyS2 at 9600 baud
 * (8N1) using a framed packet protocol (0xF0 header + checksum).
 *
 * The LCD is a 16x2 character display. GPIO power must be enabled before use
 * (handled by the power:lcd LED in the main asustor driver).
 *
 * The LCD module also has 4 navigation buttons. When pressed, the LCD controller
 * sends unsolicited F0 packets with CMD 0x80 containing the button ID. A receive
 * thread reads these packets and reports button events via a Linux input device.
 *
 * Provides sysfs attributes:
 *   /sys/devices/platform/asustor_lcm/lcd_line0  — top line text (16 chars)
 *   /sys/devices/platform/asustor_lcm/lcd_line1  — bottom line text (16 chars)
 *   /sys/devices/platform/asustor_lcm/lcd_clear  — write-only, clears display
 *
 * Copyright (C) 2026 Thierry Tremblay
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>

#include "asustor_lcm.h"

/* LCD serial port parameters */
#define LCM_SERIAL_PORT		"/dev/ttyS2"
#define LCM_BAUD_RATE		B9600

/* LCD framed protocol constants */
#define LCM_FRAME_HEADER	0xF0
#define LCM_FRAME_ACK_HEADER	0xF1
#define LCM_FRAME_MAX_SIZE	22

/* LCD commands (sent by host) */
#define LCM_CMD_TEXT		0x27

/* LCD commands (received from LCD controller) */
#define LCM_CMD_BUTTON		0x80	/* button press event */

/* LCD button IDs (data byte in CMD 0x80 packets) */
#define LCM_BTN_UP		1
#define LCM_BTN_DOWN		2
#define LCM_BTN_BACK		3
#define LCM_BTN_ENTER		4

/* LCD display dimensions */
#define LCM_WIDTH		16
#define LCM_NUM_LINES		2

/* Receive thread poll interval */
#define LCM_RX_POLL_MS		2000
#define LCM_RX_READ_TIMEOUT_US	500000	/* 500ms per-byte timeout */
#define LCM_RX_MAX_RETRIES	20	/* max consecutive read failures */

struct asustor_lcm {
	struct mutex lock;		/* serializes LCD communication */
	struct file *tty_filp;
	struct platform_device *pdev;
	char line_cache[LCM_NUM_LINES][LCM_WIDTH + 1]; /* cached display text */
	struct input_dev *input_dev;	/* input device for LCD buttons */
	struct task_struct *rx_thread;	/* serial receive thread */
};

static struct asustor_lcm *lcm_data;

/* ---- Low-level serial I/O ---- */

static struct file *lcm_serial_open(void)
{
	struct file *f;
	struct tty_struct *tty;
	struct ktermios termios;

	f = filp_open(LCM_SERIAL_PORT, O_RDWR | O_NOCTTY | O_NONBLOCK, 0);
	if (IS_ERR(f)) {
		pr_err("failed to open %s: %ld\n", LCM_SERIAL_PORT, PTR_ERR(f));
		return f;
	}

	tty = ((struct tty_file_private *)f->private_data)->tty;
	if (!tty) {
		pr_err("failed to get tty struct\n");
		filp_close(f, NULL);
		return ERR_PTR(-ENODEV);
	}

	/* Configure 9600 8N1 raw */
	termios = tty->termios;
	termios.c_iflag = 0;
	termios.c_oflag = 0;
	termios.c_cflag = LCM_BAUD_RATE | CS8 | CREAD | CLOCAL;
	termios.c_lflag = 0;
	termios.c_cc[VMIN] = 0;
	termios.c_cc[VTIME] = 1;	/* 100ms timeout */
	tty_set_termios(tty, &termios);

	return f;
}

static void lcm_serial_close(struct file *f)
{
	if (!IS_ERR_OR_NULL(f))
		filp_close(f, NULL);
}

/* Compute 8-bit checksum: sum of all bytes in the packet */
static u8 lcm_checksum(const u8 *buf, int len)
{
	u8 sum = 0;
	int i;

	for (i = 0; i < len; i++)
		sum += buf[i];
	return sum;
}

/*
 * Send a framed packet to the LCD controller.
 * Packet format: [0xF0] [N] [CMD] [DATA_0..DATA_{N-1}] [CHECKSUM]
 * where N = number of data bytes after CMD, total size = N + 4.
 *
 * Must be called with lcm->lock held.
 */
static int lcm_send_frame(struct asustor_lcm *lcm, u8 cmd,
			   const u8 *data, int data_len)
{
	u8 frame[LCM_FRAME_MAX_SIZE];
	int frame_len;
	loff_t pos = 0;
	int ret;

	frame_len = data_len + 4;	/* header + length + cmd + data + checksum */
	if (frame_len > LCM_FRAME_MAX_SIZE)
		return -EINVAL;

	frame[0] = LCM_FRAME_HEADER;
	frame[1] = data_len;
	frame[2] = cmd;
	if (data_len > 0)
		memcpy(&frame[3], data, data_len);
	frame[frame_len - 1] = lcm_checksum(frame, frame_len - 1);

	ret = kernel_write(lcm->tty_filp, frame, frame_len, &pos);
	if (ret < 0)
		return ret;
	if (ret != frame_len)
		return -EIO;

	return 0;
}

/* Read a single byte from the serial port. Returns 0 on success, -ETIMEDOUT or error. */
static int lcm_read_byte(struct asustor_lcm *lcm, u8 *byte)
{
	loff_t pos = 0;
	int ret;

	ret = kernel_read(lcm->tty_filp, byte, 1, &pos);
	if (ret == 1)
		return 0;
	if (ret == 0 || ret == -EAGAIN)
		return -ETIMEDOUT;
	return ret;
}

/*
 * Read a framed packet from the LCD controller.
 * Scans for 0xF0 or 0xF1 header, then reads length + cmd + data + checksum.
 * Returns total packet length on success, or negative error.
 */
static int lcm_recv_frame(struct asustor_lcm *lcm, u8 *buf, int buf_size)
{
	u8 byte;
	int ret, data_len, frame_len, i;
	u8 expected_cs;

	/* Scan for frame header */
	ret = lcm_read_byte(lcm, &byte);
	if (ret)
		return ret;

	if (byte != LCM_FRAME_HEADER && byte != LCM_FRAME_ACK_HEADER)
		return -EPROTO;

	buf[0] = byte;

	/* Read length byte */
	ret = lcm_read_byte(lcm, &buf[1]);
	if (ret)
		return ret;

	data_len = buf[1];
	frame_len = data_len + 4; /* header + length + cmd + data + checksum */
	if (frame_len > buf_size || frame_len > LCM_FRAME_MAX_SIZE)
		return -EPROTO;

	/* Read cmd + data + checksum */
	for (i = 2; i < frame_len; i++) {
		ret = lcm_read_byte(lcm, &buf[i]);
		if (ret)
			return ret;
	}

	/* Verify checksum */
	expected_cs = lcm_checksum(buf, frame_len - 1);
	if (buf[frame_len - 1] != expected_cs) {
		pr_debug("checksum mismatch: got 0x%02x, expected 0x%02x\n",
			 buf[frame_len - 1], expected_cs);
		return -EPROTO;
	}

	return frame_len;
}

/* Send an F1 ACK response for a received command */
static void lcm_send_ack(struct asustor_lcm *lcm, u8 cmd)
{
	u8 ack[5];
	loff_t pos = 0;

	ack[0] = LCM_FRAME_ACK_HEADER;
	ack[1] = 0x01;		/* 1 data byte */
	ack[2] = cmd;
	ack[3] = 0x00;		/* status: OK */
	ack[4] = lcm_checksum(ack, 4);

	kernel_write(lcm->tty_filp, ack, sizeof(ack), &pos);
}

/* Map LCD button ID to Linux input key code */
static unsigned int lcm_button_to_keycode(u8 button_id)
{
	switch (button_id) {
	case LCM_BTN_UP:	return KEY_UP;
	case LCM_BTN_DOWN:	return KEY_DOWN;
	case LCM_BTN_BACK:	return KEY_ESC;
	case LCM_BTN_ENTER:	return KEY_ENTER;
	default:		return KEY_UNKNOWN;
	}
}

static int lcm_rx_thread(void *data)
{
	struct asustor_lcm *lcm = data;
	u8 frame[LCM_FRAME_MAX_SIZE];
	int ret;

	while (!kthread_should_stop()) {
		ret = lcm_recv_frame(lcm, frame, sizeof(frame));
		if (ret < 0) {
			if (ret == -ETIMEDOUT) {
				/* No data available — sleep and retry */
				msleep_interruptible(100);
				continue;
			}
			/* Protocol error — skip and retry */
			pr_debug("rx frame error: %d\n", ret);
			continue;
		}

		/* Only process F0 packets (LCD-initiated) */
		if (frame[0] != LCM_FRAME_HEADER)
			continue;

		/* Send ACK */
		mutex_lock(&lcm->lock);
		lcm_send_ack(lcm, frame[2]);
		mutex_unlock(&lcm->lock);

		/* Handle button press (CMD 0x80, 1 data byte = button ID) */
		if (frame[2] == LCM_CMD_BUTTON && frame[1] >= 1) {
			u8 button_id = frame[3];
			unsigned int keycode = lcm_button_to_keycode(button_id);

			if (keycode != KEY_UNKNOWN) {
				input_report_key(lcm->input_dev, keycode, 1);
				input_sync(lcm->input_dev);
				input_report_key(lcm->input_dev, keycode, 0);
				input_sync(lcm->input_dev);
			}
			pr_debug("button press: id=%d keycode=%d\n",
				 button_id, keycode);
		}
	}

	return 0;
}

/* ---- LCD command helpers (must be called with lock held) ---- */

static int lcm_cmd_write_line(struct asustor_lcm *lcm, int line,
			      const char *text)
{
	u8 data[2 + LCM_WIDTH];	/* line + col + 16 chars */
	int len, i;

	if (line < 0 || line >= LCM_NUM_LINES)
		return -EINVAL;

	data[0] = line;
	data[1] = 0x00;		/* column = 0 */

	len = strnlen(text, LCM_WIDTH);
	memcpy(&data[2], text, len);

	/* Pad remaining characters with spaces */
	for (i = len; i < LCM_WIDTH; i++)
		data[2 + i] = ' ';

	return lcm_send_frame(lcm, LCM_CMD_TEXT, data, sizeof(data));
}

/* ---- sysfs attributes ---- */

static ssize_t lcd_line0_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct asustor_lcm *lcm = lcm_data;

	return sysfs_emit(buf, "%s\n", lcm->line_cache[0]);
}

static ssize_t lcd_line0_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct asustor_lcm *lcm = lcm_data;
	char text[LCM_WIDTH + 1];
	int len, ret;

	len = min_t(int, count, LCM_WIDTH);

	/* Strip trailing newline */
	if (len > 0 && buf[len - 1] == '\n')
		len--;

	memcpy(text, buf, len);
	text[len] = '\0';

	mutex_lock(&lcm->lock);
	ret = lcm_cmd_write_line(lcm, 0, text);
	if (ret == 0) {
		memset(lcm->line_cache[0], 0, sizeof(lcm->line_cache[0]));
		memcpy(lcm->line_cache[0], text, len);
	}
	mutex_unlock(&lcm->lock);

	return ret < 0 ? ret : count;
}

static ssize_t lcd_line1_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct asustor_lcm *lcm = lcm_data;

	return sysfs_emit(buf, "%s\n", lcm->line_cache[1]);
}

static ssize_t lcd_line1_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct asustor_lcm *lcm = lcm_data;
	char text[LCM_WIDTH + 1];
	int len, ret;

	len = min_t(int, count, LCM_WIDTH);

	/* Strip trailing newline */
	if (len > 0 && buf[len - 1] == '\n')
		len--;

	memcpy(text, buf, len);
	text[len] = '\0';

	mutex_lock(&lcm->lock);
	ret = lcm_cmd_write_line(lcm, 1, text);
	if (ret == 0) {
		memset(lcm->line_cache[1], 0, sizeof(lcm->line_cache[1]));
		memcpy(lcm->line_cache[1], text, len);
	}
	mutex_unlock(&lcm->lock);

	return ret < 0 ? ret : count;
}

static ssize_t lcd_clear_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct asustor_lcm *lcm = lcm_data;
	int ret;

	mutex_lock(&lcm->lock);
	ret = lcm_cmd_write_line(lcm, 0, "");
	if (ret == 0)
		ret = lcm_cmd_write_line(lcm, 1, "");
	if (ret == 0)
		memset(lcm->line_cache, 0, sizeof(lcm->line_cache));
	mutex_unlock(&lcm->lock);

	return ret < 0 ? ret : count;
}

static DEVICE_ATTR_RW(lcd_line0);
static DEVICE_ATTR_RW(lcd_line1);
static DEVICE_ATTR_WO(lcd_clear);

static struct attribute *asustor_lcm_attrs[] = {
	&dev_attr_lcd_line0.attr,
	&dev_attr_lcd_line1.attr,
	&dev_attr_lcd_clear.attr,
	NULL
};

static const struct attribute_group asustor_lcm_attr_group = {
	.attrs = asustor_lcm_attrs,
};

/* ---- Init / Cleanup ---- */

int __init asustor_lcm_init(const char *model_name)
{
	struct asustor_lcm *lcm;
	struct input_dev *input;
	struct file *f;
	struct platform_device *pdev;
	int ret;

	/* Only AS6806T has the front-panel LCD on ttyS2 */
	if (strcmp(model_name, "AS6806") != 0)
		return 0;

	pr_info("initializing LCM on %s\n", LCM_SERIAL_PORT);

	lcm = kzalloc(sizeof(*lcm), GFP_KERNEL);
	if (!lcm)
		return -ENOMEM;

	mutex_init(&lcm->lock);

	/* Open serial port */
	f = lcm_serial_open();
	if (IS_ERR(f)) {
		ret = PTR_ERR(f);
		goto err_free;
	}
	lcm->tty_filp = f;
	lcm_data = lcm;

	/* Create platform device for sysfs */
	pdev = platform_device_register_simple("asustor_lcm", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		pr_err("failed to register platform device: %d\n", ret);
		goto err_serial;
	}
	lcm->pdev = pdev;

	ret = sysfs_create_group(&pdev->dev.kobj, &asustor_lcm_attr_group);
	if (ret) {
		pr_err("failed to create sysfs group: %d\n", ret);
		goto err_pdev;
	}

	/* Register input device for LCD navigation buttons */
	input = input_allocate_device();
	if (!input) {
		ret = -ENOMEM;
		pr_err("failed to allocate input device\n");
		goto err_sysfs;
	}

	input->name = "ASUSTOR LCD Buttons";
	input->phys = "asustor_lcm/input0";
	input->id.bustype = BUS_RS232;
	input->dev.parent = &pdev->dev;

	input_set_capability(input, EV_KEY, KEY_UP);
	input_set_capability(input, EV_KEY, KEY_DOWN);
	input_set_capability(input, EV_KEY, KEY_ESC);
	input_set_capability(input, EV_KEY, KEY_ENTER);

	ret = input_register_device(input);
	if (ret) {
		pr_err("failed to register input device: %d\n", ret);
		input_free_device(input);
		goto err_sysfs;
	}
	lcm->input_dev = input;

	/* Start receive thread for button events */
	lcm->rx_thread = kthread_run(lcm_rx_thread, lcm, "asustor_lcm_rx");
	if (IS_ERR(lcm->rx_thread)) {
		ret = PTR_ERR(lcm->rx_thread);
		pr_err("failed to start rx thread: %d\n", ret);
		lcm->rx_thread = NULL;
		goto err_input;
	}

	pr_info("LCM initialized (16x2 display + 4 buttons on %s at 9600 baud)\n",
		LCM_SERIAL_PORT);

	return 0;

err_input:
	input_unregister_device(lcm->input_dev);
err_sysfs:
	sysfs_remove_group(&pdev->dev.kobj, &asustor_lcm_attr_group);
err_pdev:
	platform_device_unregister(pdev);
err_serial:
	lcm_serial_close(lcm->tty_filp);
err_free:
	lcm_data = NULL;
	kfree(lcm);
	return ret;
}

void __exit asustor_lcm_cleanup(void)
{
	struct asustor_lcm *lcm = lcm_data;

	if (!lcm)
		return;

	if (lcm->rx_thread)
		kthread_stop(lcm->rx_thread);
	if (lcm->input_dev)
		input_unregister_device(lcm->input_dev);
	sysfs_remove_group(&lcm->pdev->dev.kobj, &asustor_lcm_attr_group);
	platform_device_unregister(lcm->pdev);
	lcm_serial_close(lcm->tty_filp);
	lcm_data = NULL;
	kfree(lcm);

	pr_info("LCM cleaned up\n");
}
