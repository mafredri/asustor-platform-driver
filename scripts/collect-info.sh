#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Collects the hardware information needed to add or fix support for an
# ASUSTOR model, as Markdown on stdout. Run it as root, with this project's
# modules loaded if they load at all; see "Support" in README.md.
#
# Read-only: it reads /sys, /proc and debugfs and runs listing tools. It never
# requests or sets a GPIO (gpioget/gpioset would reconfigure the line). It
# leaves out serial numbers, UUIDs, MAC addresses and the kernel command
# line, but sensor labels or device names could still identify someone, so
# the output should be reviewed before it is shared.
#
# Backticks in the printf formats are Markdown, not command substitution.
# shellcheck disable=SC2016
set -u

section() {
	printf '\n## %s\n\n' "$1"
}

# Prints a command's output in a code block, or why it was skipped.
run() {
	if ! command -v "$1" >/dev/null 2>&1; then
		printf '_`%s` not installed._\n' "$1"
		return
	fi
	printf '```\n'
	"$@" 2>&1
	printf '```\n'
}

# Prints a file in a code block, if it can be read.
show() {
	if [ -r "$1" ]; then
		printf '`%s`:\n\n```\n' "$1"
		cat "$1" 2>&1
		printf '```\n'
	else
		printf '_`%s` not readable._\n' "$1"
	fi
}

if [ "$(id -u)" != 0 ]; then
	echo "collect-info.sh: not running as root; the GPIO, pinctrl and" \
		"kernel log sections will be missing. Run it with sudo." >&2
fi

printf '# ASUSTOR hardware information\n'

section "System"
printf '```\n'
uname -srm
sed -n 's/^PRETTY_NAME=//p' /etc/os-release 2>/dev/null
sed -n 's/^model name[[:space:]]*: //p' /proc/cpuinfo | sort -u
printf '```\n'

section "DMI"
printf '```\n'
for f in sys_vendor product_name product_version product_family \
	board_vendor board_name board_version bios_vendor bios_version \
	bios_date chassis_type; do
	[ -r "/sys/class/dmi/id/$f" ] && printf '%s: %s\n' "$f" "$(cat "/sys/class/dmi/id/$f")"
done
printf '```\n'

# The LPC bridge, storage controllers, PCIe switches and NICs are what tells
# models apart that share DMI strings.
section "PCI devices"
run lspci -nn -PP

section "GPIO and pinctrl controllers"
printf '```\n'
for d in /sys/bus/platform/devices/INT* /sys/bus/platform/devices/AMDI*; do
	[ -e "$d" ] && printf '%s\n' "${d##*/}"
done
printf '```\n'
run gpiodetect
# Line names such as it87_gp77, for every line, without requesting any.
run gpioinfo

section "Loaded modules"
printf '```\n'
grep -E '^(asustor|it87|gpio|pinctrl|lpc_ich|leds_gpio|gpio_keys)' /proc/modules |
	cut -d' ' -f1
printf '```\n'
for p in /sys/module/asustor/parameters/*; do
	[ -r "$p" ] && printf '%s = %s\n' "${p##*/}" "$(cat "$p")"
done

# Lines in use, with direction and value, without touching any line.
section "GPIO lines in use"
show /sys/kernel/debug/gpio

# Every pad's current mode and configuration, including pads no driver uses.
# Unmapped LEDs and buttons are usually GPIO-mode pads here. Run before this
# project's modules are loaded, it shows what the firmware set up.
section "Pin configuration"
found=
for d in /sys/kernel/debug/pinctrl/*; do
	[ -r "$d/pins" ] || continue
	found=1
	show "$d/pins"
done
[ -n "$found" ] || printf '_No readable pinctrl debugfs entries; is debugfs mounted?_\n'

section "Hardware monitoring"
for h in /sys/class/hwmon/hwmon*; do
	[ -r "$h/name" ] && printf '%s: %s\n' "${h##*/}" "$(cat "$h/name")"
done
printf '\n'
run sensors

section "LEDs"
printf '```\n'
for l in /sys/class/leds/*; do
	[ -e "$l" ] || continue
	trigger=$(sed -n 's/.*\[\([^]]*\)\].*/\1/p' "$l/trigger" 2>/dev/null)
	printf '%s brightness=%s trigger=%s\n' "${l##*/}" \
		"$(cat "$l/brightness" 2>/dev/null)" "${trigger:-?}"
done
printf '```\n'

# Which controller and port each disk hangs off, to map disks to bays.
section "Disks"
printf '```\n'
for b in /sys/class/block/sd* /sys/class/block/nvme*; do
	[ -e "$b" ] && [ ! -e "$b/partition" ] &&
		printf '%s -> %s\n' "${b##*/}" "$(readlink "$b")"
done
printf '```\n'

section "Input devices"
printf '```\n'
sed -n 's/^N: Name=//p' /proc/bus/input/devices 2>/dev/null
printf '```\n'

section "Kernel log"
if log=$(dmesg 2>&1); then
	# Without the command line, which can hold UUIDs, addresses and
	# host names.
	log=$(printf '%s\n' "$log" | grep -v -i 'command line')
	printf 'Detection:\n\n```\n'
	printf '%s\n' "$log" | grep -i -E 'asustor|it87'
	printf '```\n\nGPIO, pinctrl and LED messages (last 200):\n\n```\n'
	printf '%s\n' "$log" |
		grep -i -E 'gpio|pinctrl|leds|lpc_ich' | grep -v -i -E 'asustor|it87' |
		tail -n 200
	printf '```\n'
else
	printf '_`dmesg` failed: %s_\n' "$log"
fi
