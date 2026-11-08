TARGET         ?= $(shell uname -r)
KERNEL_MODULES := /lib/modules/$(TARGET)
KERNEL_BUILD   := $(KERNEL_MODULES)/build
SYSTEM_MAP     := /boot/System.map-$(TARGET)
DRIVER         := asustor asustor_it87 asustor_gpio_it87

# DKMS package; dkms.conf holds the version.
PACKAGE_NAME    := asustor-platform-driver
PACKAGE_VERSION := $(shell sed -n 's/^PACKAGE_VERSION="\(.*\)"$$/\1/p' dkms.conf)
DKMS_SOURCE     := /usr/src/$(PACKAGE_NAME)-$(PACKAGE_VERSION)
DKMS_FILES      := Kbuild dkms.conf $(wildcard *.c)
# Before 0.3.0, `make dkms` installed one DKMS package per module.
DKMS_PACKAGES   := $(PACKAGE_NAME) asustor asustor-it87 asustor-gpio-it87

asustor_DEST_DIR      = $(KERNEL_MODULES)/kernel/drivers/platform/x86
asustor_it87_DEST_DIR = $(KERNEL_MODULES)/kernel/drivers/hwmon
asustor_gpio_it87_DEST_DIR = $(KERNEL_MODULES)/kernel/drivers/gpio

all: modules

modules:
	@$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) modules

install:
	$(foreach mod,$(DRIVER),install -m 644 -D $(mod).ko $($(mod)_DEST_DIR)/$(mod).ko;)
	depmod -a -F $(SYSTEM_MAP) $(TARGET)

uninstall:
	$(foreach mod,$(DRIVER),rm -f $($(mod)_DEST_DIR)/$(mod).ko;)
	depmod -a -F $(SYSTEM_MAP) $(TARGET)

clean:
	$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) clean

# Installs the modules through DKMS for $(TARGET); AUTOINSTALL rebuilds them
# for kernels installed later. Replaces any version installed before, after
# checking the version so a bad dkms.conf cannot remove a working install.
dkms_install:
	@[ -n "$(PACKAGE_VERSION)" ] || { \
		echo "No PACKAGE_VERSION=\"x.y.z\" line in dkms.conf; run make in the repository root." >&2; \
		exit 1; \
	}
	@$(MAKE) --no-print-directory dkms_remove
	mkdir -p $(DKMS_SOURCE)
	cp $(DKMS_FILES) $(DKMS_SOURCE)
	dkms add -m $(PACKAGE_NAME) -v $(PACKAGE_VERSION)
	dkms build -m $(PACKAGE_NAME) -v $(PACKAGE_VERSION) -k $(TARGET)
	dkms install -m $(PACKAGE_NAME) -v $(PACKAGE_VERSION) -k $(TARGET)

# Unloads modules from an earlier install first, so the new version is the
# one that runs.
dkms: dkms_install
	@$(MAKE) --no-print-directory dkms_unload
	modprobe asustor_gpio_it87
	modprobe asustor_it87
	modprobe asustor

# Removes every installed version of the DKMS package, including the
# per-module packages from before 0.3.0, and their sources in /usr/src.
dkms_remove:
	@for pkg in $(DKMS_PACKAGES); do \
		for src in /var/lib/dkms/$$pkg/*/source; do \
			[ -e "$$src" ] || [ -L "$$src" ] || continue; \
			ver=$$(basename "$$(dirname "$$src")"); \
			echo "Removing DKMS package $$pkg/$$ver"; \
			dkms remove -m "$$pkg" -v "$$ver" --all || { \
				echo "dkms could not remove $$pkg/$$ver; fix the error above" \
					"(dkms needs /usr/src/$$pkg-$$ver), then run make again." >&2; \
				exit 1; \
			}; \
			rm -rf "/usr/src/$$pkg-$$ver"; \
		done; \
	done

# asustor uses the GPIO chip of asustor_gpio_it87, so it is unloaded first.
dkms_unload:
	@for mod in asustor asustor_it87 asustor_gpio_it87; do \
		[ -d "/sys/module/$$mod" ] || continue; \
		rmmod "$$mod" || { \
			echo "Cannot unload $$mod; stop what uses it, or reboot to load the new version." >&2; \
			exit 1; \
		}; \
	done

dkms_clean:
	@$(MAKE) --no-print-directory dkms_unload || true
	@$(MAKE) --no-print-directory dkms_remove

.PHONY: all modules install uninstall clean dkms dkms_install dkms_remove dkms_unload dkms_clean
