#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Tests `make dkms_install` and `make dkms_clean` as root against every
# kernel headers tree in /lib/modules. The first install runs over the
# per-module packages that `make dkms` registered before 0.3.0 and an older
# version of the new package, to test that it removes them; the second runs
# over the first.
set -eu

pkg=asustor-platform-driver
ver=$(sed -n 's/^PACKAGE_VERSION="\(.*\)"$/\1/p' dkms.conf)

fail() {
	echo "test-dkms.sh: $*" >&2
	dkms status >&2 || true
	for k in $kvers; do
		echo "== /lib/modules/$k" >&2
		ls -la "/lib/modules/$k" "/lib/modules/$k/updates/dkms" >&2 || true
	done
	exit 1
}

kvers=
for kdir in /lib/modules/*/build; do
	[ -d "$kdir" ] && kvers="$kvers $(basename "$(dirname "$kdir")")"
done
[ -n "$kvers" ] || fail "no kernel headers under /lib/modules."

# The containers have kernel headers but no kernel image, so dkms finds no
# modules.dep and skips depmod. Look for the installed files instead of
# asking modinfo.
installed() {
	find "/lib/modules/$1/" -name "$2.ko*" | grep -q .
}
first=${kvers# }
first=${first%% *}

# Register the old packages the way the old Makefile did, with -k added and
# dkms's default make command, which matches the old MAKE line.
for old in asustor asustor-it87 asustor-gpio-it87; do
	mod=$(echo "$old" | tr - _)
	dir=/usr/src/$old-v0.2
	mkdir -p "$dir"
	cp ./*.c "$dir"
	printf 'obj-m := %s.o\nasustor-y := asustor_main.o asustor_gpl2.o\n' "$mod" >"$dir/Makefile"
	cat >"$dir/dkms.conf" <<CONF
PACKAGE_NAME="$old"
PACKAGE_VERSION="v0.2"
BUILT_MODULE_NAME="$mod"
DEST_MODULE_LOCATION="/kernel/drivers/misc"
AUTOINSTALL="yes"
CONF
	dkms add -m "$old" -v v0.2
	dkms install -m "$old" -v v0.2 -k "$first"
done

# An older version of the new package, registered but not built.
mkdir -p "/usr/src/$pkg-0.0.1"
cp Kbuild ./*.c "/usr/src/$pkg-0.0.1"
sed 's/^PACKAGE_VERSION=.*/PACKAGE_VERSION="0.0.1"/' dkms.conf >"/usr/src/$pkg-0.0.1/dkms.conf"
dkms add -m "$pkg" -v 0.0.1

registered() {
	ls "/var/lib/dkms/$1"/*/source >/dev/null 2>&1
}

# Leave build output in the tree, as `make` before `make dkms` would.
make KERNEL_BUILD="/lib/modules/$first/build"

for run in 1 2; do
	make dkms_install TARGET="$first"
	if ls "/usr/src/$pkg-$ver"/*.mod.c >/dev/null 2>&1; then
		fail "run $run: dkms_install copied generated .mod.c files."
	fi
	for old in asustor asustor-it87 asustor-gpio-it87; do
		registered "$old" && fail "run $run: $old is still registered."
		[ ! -e "/usr/src/$old-v0.2" ] || fail "run $run: /usr/src/$old-v0.2 is left."
	done
	[ ! -e "/var/lib/dkms/$pkg/0.0.1" ] || fail "run $run: $pkg/0.0.1 is still registered."
	[ ! -e "/usr/src/$pkg-0.0.1" ] || fail "run $run: /usr/src/$pkg-0.0.1 is left."
done

for k in $kvers; do
	[ "$k" = "$first" ] || dkms install -m "$pkg" -v "$ver" -k "$k"
	dkms status -m "$pkg" -v "$ver" -k "$k" | grep -q ': installed' ||
		fail "dkms status does not list $pkg/$ver as installed for $k."
	for mod in asustor asustor_it87 asustor_gpio_it87; do
		installed "$k" "$mod" || fail "$mod is not installed for $k."
	done
done
dkms status

make dkms_clean
make KERNEL_BUILD="/lib/modules/$first/build" clean
[ ! -e "/usr/src/$pkg-$ver" ] || fail "dkms_clean left /usr/src/$pkg-$ver."
if registered "$pkg"; then
	fail "dkms_clean left $pkg registered."
fi
for k in $kvers; do
	for mod in asustor asustor_it87 asustor_gpio_it87; do
		if installed "$k" "$mod"; then
			fail "dkms_clean left $mod installed for $k."
		fi
	done
done
echo "test-dkms.sh: OK"
