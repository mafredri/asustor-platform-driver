# asustor-platform-driver

Linux kernel platform driver for ASUSTOR NAS hardware (leds, buttons).

On many systems, ASUSTOR uses a mix of IT87 and CPU GPIOs to control leds and buttons. Adding support for more systems should be fairly trivial, but may require some reverse engineering to figure out which GPIOs are responsible for what.

**WARNING:** Changing GPIO input/outputs (as done by this module) without knowledge of their effects can be dangerous and lead to instability, corrupted data or a broken system. **Use at your own risk.**

## Dependencies

**Note:** All other dependencies used by these modules are part of the mainline linux kernel, if they're not included by your distribution you may need to compile them yourself.

- `gpio-ich` (AS6)
- `hwmon-vid` (for the contained `asustor-it87` module)

### Optional

- `it87` (AS6, AS61, AS62, AS66XX, AS67XX, AS54XX)
  - This project includes a patched version of `it87` called `asustor-it87` which skips fan pwm sanity checks
    and supports more variants of IT86XX and the IT87XX chips than the kernels `it87` driver.
    Furthermore it supports controlling blinking of up to two LEDs.
  - Also includes a patched version of `gpio-it87` called `asustor-gpio-it87`. The only change is supporting
    the IT8625E chip that is used in several newer asustor devices.
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
- AS6702T, AS6704T
- AS5402T, AS5404T
- .. possibly more, if they're similar enough.
  The following DMI system-manufacturer / system-product-name combinations are currently supported
  (see `sudo dmidecode -s system-manufacturer` and `sudo dmidecode -s system-product-name`):
    - "ASUSTOR Inc." / "AS-6xxT"
    - "Insyde" / "AS61xx"
    - "Insyde" / "GeminiLake"
    - "Intel Corporation" / "Jasper Lake Client Platform"

## Features

- LEDs (front panel, disk)
  - See [asustor.c](asustor.c).
- Buttons
  - USB Copy Button
  - Power Button (AS6)

## Installation

```
git clone https://github.com/mafredri/asustor-platform-driver
cd asustor-platform-driver
make
sudo make install
```

## Tips

### Control blinking LEDs with `it87`

*Note:* This is probably not supported on all devices, only ones that use the IT8625E or IT8720F
chips or similar.

Switch off that annoying blinking of the green status LED:
```
echo 0 | sudo tee /sys/devices/platform/asustor_it87.*/hwmon/hwmon*/gpled1_blink
```

You can re-enable it with `echo 47 | sudo tee ...` because the status led is it87_gp**47**.
In theory, you can make other GPIO LEDs blink by using their GP number instead of 47,
but I couldn't get that to work.
Note that this could even be done for two LEDs, as `gpled2_blink` also exists.

If you want the green status LED to be constantly on (without blinking),
the following should work, if `gpled1_blink` is still `47`:
```
echo 11 | sudo tee /sys/devices/platform/asustor_it87.*/hwmon/hwmon*/gpled1_blink_freq
```

You can also configure the blinking frequency to one of 11 supported modes,
for example, set mode 3 with:
```
echo 3 | sudo tee /sys/devices/platform/asustor_it87.*/hwmon/hwmon*/gpled1_blink_freq
```
The following blinking frequency modes exist on the IT8625:
* **0** - 0.125s Off 0.125s On
* **1** - 0.5s Off 0.5s On
* **2** - 2s	Off	2s On
* **3** - 0.25s Off 0.25s On
* **4** - 1s Off 3s On
* **5** - 3s Off 1s On
* **6** - 2s Off	6s On
* **7** - 6s Off	2s On
* **8** - 0.5s Off 2s On
* **9** - 1s Off 1s On
* **10** - 4s Off 4s On
* **11** - Always On

Other chips also support blinking control, but might support fewer modes.
If blink frequency setting is supported at all, mode 11 (always on) *should* always work,
and setting the other modes won't break anything, but might have differing frequencies than
described above (and setting modes 8-10 will automatically set mode 0 instead).

*Mode 11 for "always on" should always work, at least the bit set there was listed in
all datasheets I checked (unfortunately, its function was never described in detail).*

### `it87` and PWM polarity

This project includes a patched version of the `it87` module (`asustor-it87`) that is part of mainline kernel. It skips PWM sanity checks for the fan because ASUSTOR firmware correctly initializes fans in active low polarity and can be used straight with `fancontrol` or similar tools.

Note that `it87` conflicts with `asustor-it87`, you may wish to add `it87` to the module blocklist or explicitly load `asustor-it87` instead.

~~You may want to use [`patches/001-ignore-pwm-polarity-it87.patch`](patches/001-ignore-pwm-polarity-it87.patch) for the `it87` kernel module if it complains about PWM polarity. In this case, it's possible to use `fix_pwm_polarity=1`, however, it may reverse the polarity which is unwanted (i.e. high is low, low is high). It works fine when left as configured by the firmware.~~

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
```

NOTE: If `gpioinfo` does not return anything, you may need to figure out which (if any) gpio drivers to load. Also keep in mind that your distribution may not ship with all `gpio-` drivers, so you may need to compile them yourself.

## TODO

- Add DKMS support
- Support variable amount of disk LEDs
- ~~Create a new led trigger driver so that we can blink disk LEDs individually, the existing `disk-activity` trigger always blinks all LEDs on activity from any disk~~
  - Pray that [[RFC PATCH v3 00/18] Add block device LED trigger](https://lore.kernel.org/linux-leds/20210819025053.222710-1-arequipeno@gmail.com/) by Ian Pilcher lands in the linux kernel
- Release a modified `gpio-it87.c` for figuring out the firmware configuration of IT87 GPIOs (could be useful when adding new devices)
