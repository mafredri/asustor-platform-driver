TARGET         ?= $(shell uname -r)
KERNEL_MODULES := /lib/modules/$(TARGET)
KERNEL_BUILD   := $(KERNEL_MODULES)/build
SYSTEM_MAP     := /boot/System.map-$(TARGET)
DRIVER         := asustor asustor_it87 asustor_gpio_it87
DRIVER_VERSION := v0.1
#DRIVER_VERSION ?= $(shell git describe --long)

# DKMS
DKMS_ROOT_PATH_ASUSTOR=/usr/src/asustor-$(DRIVER_VERSION)
DKMS_ROOT_PATH_ASUSTOR_IT87=/usr/src/asustor-it87-$(DRIVER_VERSION)
DKMS_ROOT_PATH_ASUSTOR_GPIO_IT87=/usr/src/asustor-gpio-it87-$(DRIVER_VERSION)

asustor_DEST_DIR      = $(KERNEL_MODULES)/kernel/drivers/platform/x86
asustor_it87_DEST_DIR = $(KERNEL_MODULES)/kernel/drivers/hwmon
asustor_gpio_it87_DEST_DIR = $(KERNEL_MODULES)/kernel/drivers/gpio

obj-m  := $(patsubst %,%.o,$(DRIVER))
obj-ko := $(patsubst %,%.ko,$(DRIVER))

all: modules

modules:
	@$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) modules

install: modules_modules

modules_modules:
	$(foreach mod,$(DRIVER),install -m 644 -D $(mod).ko $($(mod)_DEST_DIR)/$(mod).ko;)
	depmod -a -F $(SYSTEM_MAP) $(TARGET)

clean:
	$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) clean

.PHONY: all modules install modules_install clean

dkms:
	@mkdir -p $(DKMS_ROOT_PATH_ASUSTOR)
	@echo "obj-m := asustor.o" >>$(DKMS_ROOT_PATH_ASUSTOR)/Makefile
	@echo "obj-ko := asustor.ko" >>$(DKMS_ROOT_PATH_ASUSTOR)/Makefile
	@cp dkms.conf $(DKMS_ROOT_PATH_ASUSTOR)
	@cp asustor.c $(DKMS_ROOT_PATH_ASUSTOR)
	@sed -i -e '/^PACKAGE_VERSION=/ s/=.*/=\"$(DRIVER_VERSION)\"/' $(DKMS_ROOT_PATH_ASUSTOR)/dkms.conf

	@mkdir -p $(DKMS_ROOT_PATH_ASUSTOR_IT87)
	@echo "obj-m := asustor_it87.o" >>$(DKMS_ROOT_PATH_ASUSTOR_IT87)/Makefile
	@echo "obj-ko := asustor_it87.ko" >>$(DKMS_ROOT_PATH_ASUSTOR_IT87)/Makefile
	@cp dkms_it87.conf $(DKMS_ROOT_PATH_ASUSTOR_IT87)/dkms.conf
	@cp asustor_it87.c $(DKMS_ROOT_PATH_ASUSTOR_IT87)
	@sed -i -e '/^PACKAGE_VERSION=/ s/=.*/=\"$(DRIVER_VERSION)\"/' $(DKMS_ROOT_PATH_ASUSTOR_IT87)/dkms.conf

	@mkdir -p $(DKMS_ROOT_PATH_ASUSTOR_GPIO_IT87)
	@echo "obj-m := asustor_gpio_it87.o" >>$(DKMS_ROOT_PATH_ASUSTOR_GPIO_IT87)/Makefile
	@echo "obj-ko := asustor_gpio_it87.ko" >>$(DKMS_ROOT_PATH_ASUSTOR_GPIO_IT87)/Makefile
	@cp dkms_gpio_it87.conf $(DKMS_ROOT_PATH_ASUSTOR_GPIO_IT87)/dkms.conf
	@cp asustor_gpio_it87.c $(DKMS_ROOT_PATH_ASUSTOR_GPIO_IT87)
	@sed -i -e '/^PACKAGE_VERSION=/ s/=.*/=\"$(DRIVER_VERSION)\"/' $(DKMS_ROOT_PATH_ASUSTOR_GPIO_IT87)/dkms.conf

	@dkms add -m asustor -v $(DRIVER_VERSION)
	@dkms add -m asustor-it87 -v $(DRIVER_VERSION)
	@dkms add -m asustor-gpio-it87 -v $(DRIVER_VERSION)
	@dkms build -m asustor -v $(DRIVER_VERSION)
	@dkms build -m asustor-it87 -v $(DRIVER_VERSION)
	@dkms build -m asustor-gpio-it87 -v $(DRIVER_VERSION)
	@dkms install --force -m asustor -v $(DRIVER_VERSION)
	@dkms install --force -m asustor-it87 -v $(DRIVER_VERSION)
	@dkms install --force -m asustor-gpio-it87 -v $(DRIVER_VERSION)
	@modprobe asustor_gpio_it87
	@modprobe asustor_it87
	@modprobe asustor

dkms_clean:
	@rmmod asustor 2> /dev/null || true
	@rmmod asustor_it87 2> /dev/null || true
	@rmmod asustor_gpio_it87 2> /dev/null || true
	@dkms remove -m asustor -v $(DRIVER_VERSION) --all
	@dkms remove -m asustor-it87 -v $(DRIVER_VERSION) --all
	@dkms remove -m asustor-gpio-it87 -v $(DRIVER_VERSION) --all
	@rm -rf $(DKMS_ROOT_PATH_ASUSTOR)
	@rm -rf $(DKMS_ROOT_PATH_ASUSTOR_IT87)
	@rm -rf $(DKMS_ROOT_PATH_ASUSTOR_GPIO_IT87)

