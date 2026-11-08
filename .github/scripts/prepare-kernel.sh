#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Prepares an unpacked kernel.org source tree for building out-of-tree
# modules: x86_64 defconfig plus the subsystems the asustor modules use, then
# modules_prepare. Without a vmlinux build there is no Module.symvers, so
# build modules against the result with KBUILD_MODPOST_WARN=1. Such builds
# check that the modules compile, not that their symbols resolve.
#
# Usage: prepare-kernel.sh <kernel source dir>
set -eu

cd "$1"

make -s x86_64_defconfig
scripts/config \
	--enable GPIOLIB \
	--enable NEW_LEDS \
	--enable LEDS_CLASS \
	--module LEDS_GPIO \
	--enable LEDS_TRIGGERS \
	--enable INPUT_KEYBOARD \
	--module KEYBOARD_GPIO_POLLED \
	--enable HWMON \
	--module SENSORS_IT87
make -s olddefconfig

# scripts/config does not check dependencies, so confirm olddefconfig kept
# every option.
for opt in GPIOLIB=y LEDS_CLASS=y LEDS_GPIO=m KEYBOARD_GPIO_POLLED=m HWMON=y HWMON_VID=m; do
	grep -qx "CONFIG_${opt}" .config || {
		echo "prepare-kernel.sh: CONFIG_${opt} is not set after olddefconfig;" \
			"update the option list." >&2
		exit 1
	}
done

make -s -j"$(nproc)" modules_prepare
