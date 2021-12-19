TARGET         ?= $(shell uname -r)
KERNEL_MODULES := /lib/modules/$(TARGET)
KERNEL_BUILD   := $(KERNEL_MODULES)/build
SYSTEM_MAP     := /boot/System.map-$(TARGET)
MOD_SUBDIR      = drivers/platform/x86
MOD_DEST_DIR    = $(KERNEL_MODULES)/kernel/$(MOD_SUBDIR)
DRIVER         := asustor

obj-m  := $(patsubst %,%.o,$(DRIVER))
obj-ko := $(patsubst %,%.ko,$(DRIVER))

all: modules

modules:
	@$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) modules

install: modules_modules

modules_modules:
	/usr/bin/install -m 644 -D $(DRIVER).ko $(MOD_DEST_DIR)/$(DRIVER).ko
	depmod -a -F $(SYSTEM_MAP) $(TARGET)

clean:
	$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) clean

.PHONY: all modules install modules_install clean
