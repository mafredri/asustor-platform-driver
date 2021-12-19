# asustor-platform-driver

Linux kernel platform driver for ASUSTOR NAS hardware (leds, buttons).

On many systems, ASUSTOR use a mix of IT87 and CPU GPIOs to control leds and buttons. Adding support for more systems should be fairly trivial, but may require some reverse engineering to figure out which GPIOs are responsible for what.

**WARNING:** Changing GPIO input/outputs (as done by this module) without knowledge of their effects can be dangerous and lead to instability, corrupted data or a broken system. **Use at your own risk.**

## Dependencies

**Note:** All dependencies used by this module are part of the mainline linux kernel, if they're not included by your distribution you may need to compile them yourself.

- `gpio-it87` (AS6, AS61, AS62)
- `gpio-ich` (AS6)

### Optional

- `it87` (AS6, AS61, AS62)
  - Temperature monitoring (`lm-sensors`)
  - Fan speed regulation via `pwm1`
    - See [`example/fancontrol`](./example/fancontrol) for an example `/etc/fancontrol` config for a AS62 system
  - Front panel LED brightness adjustment via `pwm3`

## Compatibility

- AS6204T
- AS6104T (NOT TESTED!)
- AS604T

## Features

- LEDs
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

### `it87` and PWM polarity

You may want to use [`patches/001-ignore-pwm-polarity-it87.patch`](patches/001-ignore-pwm-polarity-it87.patch) for the `it87` kernel module if it complains about PWM polarity. In this case, it's possible to use `fix_pwm_polarity=1`, however, it may reverse the polarity which is unwanted (i.e. high is low, low is high). It works fine when left as configured by the firmware.

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
