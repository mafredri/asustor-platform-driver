# asustor-platform-driver

Linux kernel platform driver for ASUSTOR NAS hardware (leds, buttons).

On many systems, ASUSTOR uses a mix of IT87 and CPU GPIOs to control leds and buttons. Adding support for more systems should be fairly trivial, but may require some reverse engineering to figure out which GPIOs are responsible for what.

**WARNING:** Changing GPIO input/outputs (as done by this module) without knowledge of their effects can be dangerous and lead to instability, corrupted data or a broken system. **Use at your own risk.**

## Dependencies

**Note:** The following dependencies from the mainline linux kernel are required, if they're not included by your distribution you may need to compile them yourself (note that some modules are only required on specific ASUSTOR models):

- `gpio-ich` (AS6)
- `hwmon-vid` (for the contained `asustor-it87` module)

### Optional

- `it87` (AS6, AS61, AS62, AS66XX, AS67XX, AS54XX, FS67XX)
  - This project includes a patched version of `it87` called `asustor-it87` which skips fan pwm sanity checks
    and supports more variants of IT86XX and the IT87XX chips than the kernels `it87` driver.
  - Also includes a patched version of `gpio-it87` called `asustor-gpio-it87`, which supports
    the IT8625E chip that is used in several newer ASUSTOR devices, including
    [hardware blinking](#hardware-led-blinking) of up to two LEDs.
  - May require adding `acpi_enforce_resources=lax` to kernel boot arguments for full functionality
  - Temperature monitoring (`lm-sensors`)
  - Fan speed regulation via `pwm1`
    - See [`example/fancontrol`](./example/fancontrol) for an example `/etc/fancontrol` config for a AS62 system
    - `pwm1` etc should be in `/sys/devices/platform/asustor_it87.*/hwmon/hwmon*/`
  - Front panel LED brightness adjustment via `pwm3`

## Compatibility

- AS604T
- AS6104T (NOT TESTED!)
- AS6204T
- AS6602T, AS6604T (NOT TESTED!)
- AS6702T, AS6704T, AS6706T
- AS5402T, AS5404T (NOT TESTED!)
- FS6706T, FS6712X
- .. possibly more, if they're similar enough.

The following DMI system-manufacturer / system-product-name combinations are currently supported
(see `sudo dmidecode -s system-manufacturer` and `sudo dmidecode -s system-product-name`):
* "ASUSTOR Inc." / "AS-6xxT"
    - Identified by `asustor` kernel module as **"AS6xx"**
* "Insyde" / "AS61xx"
    - Identified by `asustor` kernel module as **"AS61xx"**
* "Insyde" / "GeminiLake"
    - These are the *Lockerstor* AS66xxT devices, like AS6604T
        - *maybe also others like Nimbustor AS520xT?*
    - Identified by `asustor` kernel module as **"AS66xx"**
* "Intel Corporation" / "Jasper Lake Client Platform"
    - These are the *Lockerstor Gen2* AS67xxT (AS6702T etc), *Nimbustor Gen2* AS54xxT (AS5402T etc)
      and *Flashstor* FS6706T/FS6712X devices.
    - Identified by `asustor` kernel module as:
        * **"AS6702"** for *Lockerstor Gen2* and *Nimbustor Gen2* with *two* SATA drives (AS6702T, AS5402T)
        * **"AS6704"** for *Lockerstor Gen2* and *Nimbustor Gen2* with *four* SATA drives (AS6704T, AS5404T)
        * **"AS6706"** for *Lockerstor Gen2* with *six* SATA drives (AS6706T)
        * **"FS6706"** for *Flashtor* with *six* slots for m.2 NVME SSDs (FS6706T)
        * **"FS6712"** for *Flashtor* with *twelve* slots for m.2 NVME SSDs (FS6712X)

## Features

- LEDs (front panel, disk)
  - Represented as subdirectories in `/sys/class/leds/`
    - In the subdirectories, you can set `brightness` to 0 or 1 to switch the LED off or on
      (for example `echo 1 | sudo tee blue:power/brightness`). Similarly, the `trigger` can
      also be configured, see [below](#set-triggers-for-leds).
    - Sometimes the name of an LED doesn't exactly represent its color, for example, on the
      *Flashstor FS6712X*, the `blue:lan` LED is actually purple when connected with 10GBit
      (but blue when connected with 1GBit). Also, sometimes two LEDs physically appear as one, so
      enabling both will create a third color (e.g. if both `nvme1:green` and `nvme1:red` are enabled,
      it will look orange).
  - See [asustor_main.c](asustor_main.c).
- Buttons
  - USB Copy Button
  - Power Button (AS6)
- Power (`/sys/class/leds/power:*`)
  - LCD
  - Front panel

## Installation

```
git clone https://github.com/mafredri/asustor-platform-driver
cd asustor-platform-driver
make
sudo make install
```

### NixOS
Include the platform drivers in your `flake.nix` as follows:
```
{
  inputs.asustor-platform-driver.url = "github:mafredri/asustor-platform-driver";
  # optional, not necessary for the module
  #inputs.asustor-platform-driver.inputs.nixpkgs.follows = "nixpkgs";

  outputs = { self, nixpkgs, asustor-platform-driver }: {
    # change `yourhostname` to your actual hostname
    nixosConfigurations.yourhostname = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./configuration.nix
        asustor-platform-driver.nixosModules.default
        { hardware.asustor.enable = true; }
      ];
    };
  };
}
```

## Tips

### Hardware LED blinking

The firmware leaves the green status LED blinking (in hardware) after boot. `asustor-gpio-it87`
stops that when the LEDs are set up, so the status LED is simply on once the driver is loaded.

On devices with the IT8625E chip, `asustor-gpio-it87` can also let the chip itself blink LEDs,
using the kernel's `timer` LED trigger (see also [below](#set-triggers-for-leds)):
```
sudo modprobe ledtrig-timer
echo timer | sudo tee /sys/class/leds/green\:status/trigger
```
By default this blinks 500ms on, 500ms off. Other times (in milliseconds) can be set with
`delay_on` and `delay_off`, for example 3s on, 1s off:
```
echo 3000 | sudo tee /sys/class/leds/green\:status/delay_on
echo 1000 | sudo tee /sys/class/leds/green\:status/delay_off
```

The chip supports the following on/off times, which are from ASUSTOR's GPL source:
* **125** / **125**, **250** / **250**, **500** / **500**, **1000** / **1000**, **2000** / **2000**, **4000** / **4000**
* **1000** / **3000** and **3000** / **1000**
* **2000** / **6000** and **6000** / **2000**
* **2000** / **500** for LEDs that are lit when their GPIO is low (like `green:status`),
  or **500** / **2000** for LEDs that are lit when their GPIO is high (like `sata1:green:disk`)

Only LEDs on GPIO pins `it87_gp10` to `it87_gp57` can blink in hardware (see
`sudo cat /sys/kernel/debug/gpio` for which pin an LED uses), and at most two at a time.
Otherwise, or with other times, the kernel blinks the LED in software instead, which works just
as well but needs the CPU.

*Note:* Older versions of this project had `gpled1_blink` and `gpled1_blink_freq` (and `gpled2_*`)
in `/sys/devices/platform/asustor_it87.*/hwmon/hwmon*/` for this, those have been removed.

### Set triggers for LEDs

Linux allows controlling LEDs with "triggers", which means that they will blink on specific events.
By default, the trigger is "none" (which means "always on") for most LEDs, but there are others that
you may enable (likely in a script that's run after boot), for example:
```
# make green USB LED blink on USB traffic
echo usb-host > /sys/class/leds/green\:usb/trigger
# make LAN led light up if the first network link is up:
echo r8169-0-200:00:link > /sys/class/leds/blue\:lan
```

`cat /sys/class/leds/green\:usb/trigger` will list the available triggers, with the currently used
one being marked with square brackes (e.g. `[none]  kbd-scrolllock kbd-numlock kbd-capslock ...`).

Note that currently the disk-related triggers (like `disk-activity`) do **not** work with NVME drives.
That's a general limitation of the Linux kernel that is independent of this project.
If this feature is ever implemented in the kernel, it will automatically work with this driver.

### `it87` and PWM polarity

This project includes a patched version of the `it87` module that is part of mainline kernel (`asustor-it87`). It skips PWM sanity checks for the fan because ASUSTOR firmware correctly initializes fans in active low polarity and can be used straight with `fancontrol` or similar tools.

Note that `it87` conflicts with `asustor-it87`, you may wish to add `it87` to the module blocklist or explicitly load `asustor-it87` instead.

~~You may want to use [`patches/001-ignore-pwm-polarity-it87.patch`](patches/001-ignore-pwm-polarity-it87.patch) for the `it87` kernel module if it complains about PWM polarity. In this case, it's possible to use `fix_pwm_polarity=1`, however, it may reverse the polarity which is unwanted (i.e. high is low, low is high). It works fine when left as configured by the firmware.~~

### Override detection of ASUSTOR device by `asustor` kernel module

If the `asustor` kernel module doesn't detect your device correctly, you can force it to treat your
ASUSTOR device as one of the supported devices by setting the `force_device` module parameter.

This can be done manually with `sudo modprobe asustor force_device=AS66xx`, or by creating
`/etc/modprobe.d/asustor.conf` with the following content:

```
# override device detection of the asustor kernel module
options asustor force_device=FS6712
```

Please replace "FS6712" with the device you want to try.  
See the [Compatibility](#compatibility)-section above for how the `asustor` kernel module identifies devices.
Alternatively, can use the following command to print module parameters, including the currently supported device names for `force_device`:

```console
$ sudo modinfo -p asustor
```

_**NOTE:** If you need to use the `force_device` parameter to make your device work, please open an issue
so the detection logic in the `asustor` kernel module can be fixed to properly support it._

### Misc

- `blue:power` and `red:power` can be turned on simultaneously for a pink-ish tint
- `green:status` and `red:status` can be turned on simultaneously for a orange-ish tint

## Support

If you would like additional hardware to be supported, pull requests are more than welcome. Alternatively, you can install these prerequisites:

```
sudo apt-get install -y gpiod
```

And then open an issue and attach outputs from the following commands:

```
sudo dmesg
sudo dmidecode -s system-manufacturer
sudo dmidecode -s system-product-name
sudo dmidecode -s bios-vendor
sudo dmidecode -s bios-version
sudo dmidecode -s bios-release-date
sudo dmidecode -s bios-revision
sudo gpioinfo
sudo lspci -nn -PP
```

NOTE: If `gpioinfo` does not return anything, you may need to figure out which (if any) gpio drivers to load. Also keep in mind that your distribution may not ship with all `gpio-` drivers, so you may need to compile them yourself.

## TODO

- Support variable amount of disk LEDs
- ~~Create a new led trigger driver so that we can blink disk LEDs individually, the existing `disk-activity` trigger always blinks all LEDs on activity from any disk~~
  - Pray that [[PATCH v13 0/2] Introduce block device LED trigger](https://lore.kernel.org/lkml/20221227225226.546489-1-arequipeno@gmail.com/T/#mc8758efa18e1b7ed51a50c298d881a2e91280b1f)
    by Ian Pilcher lands in the linux kernel
  - Pray that [[PATCH] nvme-pci: trigger disk activity LED](https://lore.kernel.org/lkml/4100a868-c5bd-91dd-0c45-a92fb1344b12@kernel.dk/T/)
    by Enzo Matsumiya (or an alternative implementation that lets NVME disk activity trigger LEDs) lands in the linux kernel

## DKMS

DKMS installation to enable module auto-build with kernel upgrades. 

```
sudo make dkms
```
