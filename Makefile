TARGET         ?= $(shell uname -r)
KERNEL_MODULES := /lib/modules/$(TARGET)
KERNEL_BUILD   := $(KERNEL_MODULES)/build
SYSTEM_MAP     := /boot/System.map-$(TARGET)
DRIVER         := asustor asustor_it87

asustor_DEST_DIR      = $(KERNEL_MODULES)/kernel/drivers/platform/x86
asustor_it87_DEST_DIR = $(KERNEL_MODULES)/kernel/drivers/hwmon

obj-m  := $(patsubst %,%.o,$(DRIVER))
obj-ko := $(patsubst %,%.ko,$(DRIVER))

all: modules

modules:
	@$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) modules

install: modules_modules

modules_modules:
	$(foreach mod,$(DRIVER),/usr/bin/install -m 644 -D $(mod).ko $($(mod)_DEST_DIR)/$(mod).ko;)
	depmod -a -F $(SYSTEM_MAP) $(TARGET)

clean:
	$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) clean

.PHONY: all modules install modules_install clean
