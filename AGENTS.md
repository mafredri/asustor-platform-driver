# Agent guidelines

Out-of-tree Linux kernel modules for ASUSTOR NAS hardware, developed on GitHub. Write code as a kernel maintainer expects it: follow [coding-style](https://docs.kernel.org/process/coding-style.html) and [coding-assistants](https://docs.kernel.org/process/coding-assistants.html) unless this file says otherwise. Changes go through pull requests, not emailed patches; see [Pull requests](#pull-requests) for how to describe and merge them.

## Modules

| Module                 | Sources                             | Upstream it tracks                                                                                                                         |
| ---------------------- | ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `asustor.ko`           | `asustor_main.c`, `asustor_gpl2.c`  | None; ours. Model detection (DMI, PCI counts) and GPIO lookup tables for LEDs and buttons.                                                  |
| `asustor_it87.ko`      | `asustor_it87.c`                    | [frankcrawford/it87](https://github.com/frankcrawford/it87), see below.                                                                     |
| `asustor_gpio_it87.ko` | `asustor_gpio_it87.c`               | Mainline `drivers/gpio/gpio-it87.c`, plus IT8625E.                                                                                         |

`asustor_it87.c` started as mainline 5.17 `it87.c`. It then took most of frankcrawford/it87 (Feb 2022) through h0tw1r3's port to 5.17, parts of ASUSTOR's GPL kernel source, and LED blink control written in this repo. Of the external sources, frankcrawford/it87 is the most authoritative: compare against it first when porting fixes or chip support. When porting, keep the local changes unless changing them is the task: IT8625E support, the LED blink sysfs attributes, and the PWM polarity check that always passes (`it87_check_pwm()`), because ASUSTOR firmware sets active-low PWM, which upstream treats as broken.

`asustor_gpl2.c` holds code copied from GPL-2.0-only kernel files, so `asustor_main.c` can stay GPL-2.0-or-later. Do not move code between the two without checking the license of its origin. Every C source starts with an SPDX license identifier.

## Coding style

- Kernel style: tabs, lines up to 80 columns unless breaking them hurts readability (checkpatch only warns past 100), `/* */` or `//` comments, `pr_fmt`/`dev_*` logging. `.clang-format` is the kernel's; use it on new code only.
- Do not restyle existing code in a change that does something else. In `asustor_it87.c` and `asustor_gpio_it87.c`, keep the diff against upstream small so upstream fixes still apply; match upstream style there, not ours.
- When porting an upstream fix, name the upstream commit in the pull request: `commit <sha, at least 12 characters> ("<subject>")`.
- Use descriptor-based GPIO APIs (`gpiod_*`, lookup tables, software nodes) for new code, not global GPIO numbers.
- Every error path releases what it acquired, and module exit unregisters everything init registered. Check `pci_get_*`/`gpio_device_*` reference rules in the kernel source before using them.

## checkpatch

CI runs `.github/scripts/checkpatch-range.sh` on the combined diff of a pull request, with `checkpatch.pl` from the kernel release pinned in `.github/workflows/build.yaml`. It checks code only, not commit messages, and fails on errors and warnings. To run it locally, pass the PR's base branch (not always `origin/main` for stacked PRs):

```sh
.github/scripts/checkpatch-range.sh <linux>/scripts/checkpatch.pl <base> HEAD
```

The script ignores `LINUX_VERSION_CODE` (version guards, which also trip `CONSTANT_COMPARISON`) and `FILE_PATH_CHANGES` (asks about MAINTAINERS updates). Still put the constant on the right in ordinary comparisons. Fix everything else in lines you add. Run `checkpatch.pl --strict` as well and fix what is reasonable; CI does not enforce it.

## Kernel compatibility

- Supported: 6.1 (Debian 12) and 6.8 (Ubuntu 24.04) through current mainline. Code must build on all of them.
- Handle API changes with `#if LINUX_VERSION_CODE >= KERNEL_VERSION(x, y, 0)` around the smallest possible region, with a comment naming the upstream change. Find the version from the upstream commit (`git describe --contains <sha>` in a kernel tree), not from memory.
- Drop guards for versions below the supported floor when touching that code.

## Hardware support

Wrong GPIO configuration can damage hardware or corrupt data, and nobody in CI has the hardware.

- Add a model or GPIO mapping only from evidence posted by someone with the device: `dmidecode` strings, `lspci -nn`, `gpioinfo`, and which LED or button each pin drives. Link the issue in the pull request.
- New detection must identify the hardware positively: when DMI strings are shared, match PCI devices the model has. Do not add matches that only mean "none of the others", which catch unrelated boards. The existing DMI-only matches and the FS6706 match on absent PCI devices are not precedents.
- Say in the pull request which models the change was tested on, and which were not tested.

## Building and verifying

```sh
make                                      # against the running kernel
make KERNEL_BUILD=/path/to/kernel/build   # against another headers tree
make clean                                # pass the same KERNEL_BUILD= as the build
```

Without the hardware, verification means building. CI (`.github/workflows/build.yaml`) builds every pull request that changes more than documentation with `-Werror` against distribution kernel headers and against kernel.org trees (mainline, stable, and longterm releases from 6.1). To reproduce a kernel.org build locally:

```sh
.github/scripts/prepare-kernel.sh /path/to/linux-x.y
make KERNEL_BUILD=/path/to/linux-x.y KBUILD_MODPOST_WARN=1 KCFLAGS=-Werror
```

In the PR, say which parts you wrote, what you built, and whether any of it ran on the hardware. A successful build says nothing about whether the GPIOs are right.

## Fixing bugs

- Confirm the bug is real before fixing it: reproduce it, or trace the failing code path when reproducing needs hardware. Drop it if it turns out not to be real.
- Fix it in the same change, built against the supported kernels and clean under checkpatch.
- In the pull request, describe the problem and the fix, and name the commit that introduced the bug: `<sha, at least 12 characters> ("<subject>")`.
- Say what you could not build, reproduce, or test.
- If the bug could be a security vulnerability, do not describe it in an issue, PR, commit, or any other public place. Tell the person you work for; the maintainer takes reports through the repository's private vulnerability reporting.

## Releases

Versions follow [semver](https://semver.org/), and the release tag `vX.Y.Z` is the only place the version is set. `scripts/version.sh` derives the version from it for `make dkms`, in a git clone and in source archives. To release, push the tag on a commit on `main`. Only tags of exactly the form `vX.Y.Z` count, one per commit. `.github/workflows/release.yaml` checks that the commit is on `main`, builds it, then publishes a GitHub release with a source tarball and notes listing the merged pull requests. Push tags only when asked to.

## Pull requests

- One logical change per pull request.
- Title: `<prefix>: <imperative summary>`, at most 70 characters. Prefixes: `asustor`, `asustor-it87`, `asustor-gpio-it87`, `build` (Makefile, Kbuild), `clang-format`, `dkms`, `scripts`, `ci`, `docs`, `nix`.
- Description: what was wrong or missing, why this change is the fix, what it affects, and what was built and tested. Explain the reasons; do not narrate the diff. Reference issues with GitHub's keywords (`Fixes #N`) so merging closes them.
- The maintainer decides when to merge; do not merge unless asked. Pull requests are squash-merged. The squash commit title is GitHub's `<PR title> (#N)`, and its body is one cohesive explanation of the whole change, based on the description and wrapped at 72 columns. It is not the list of the pull request's commits.
- The commits inside a pull request are working history and are not kept; they need no particular shape or message.
- Do not add `Signed-off-by:` anywhere. Only a human can certify the [Developer's Certificate of Origin](https://docs.kernel.org/process/submitting-patches.html#sign-your-work-the-developer-s-certificate-of-origin).
