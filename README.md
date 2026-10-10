# Linux 5.10.72 Kernel for EMB03

[![Build EMB03 Linux Kernel](https://github.com/Factory48/linux-imx-5.10.9-emb03-factory48/actions/workflows/build-kernel.yml/badge.svg)](https://github.com/Factory48/linux-imx-5.10.9-emb03-factory48/actions/workflows/build-kernel.yml)
[![License: GPL-2.0](https://img.shields.io/badge/License-GPL%202.0-blue.svg)](COPYING)

Production Linux kernel source tree for the **EMB03** hardware platform (NXP i.MX8M Plus), maintained by **Factory48 Labs** (Singapore).

Based on NXP official Android BSP release `android-11.0.0_2.6.0` (commit `f0b39243c99211907acb9cb3d332ab466e76d089`, Linux kernel version `5.10.72-emb03`). The EMB03 changes were re-applied on top of the unmodified 2.6.0 tree; all NXP SoC drivers (GPU `galcore`, Hantro VPU, SAI, ISI, DWC3, …) are the 2.6.0 versions.

The GPU kernel driver (`galcore` 6.4.3 build 336687) must be paired with the Vivante user-space libraries from the same 2.6.0 release (`vendor/nxp/fsl-proprietary/gpu-viv`); the 2.0.0 user-space libraries do not match this kernel.

## 2.6.0 repair candidate (not hardware-validated)

The retained 5.10.9 board DT is not compatible with the new audio bindings.
Before packaging this kernel, run `scripts/emb03-migrate-dtb.py SOURCE.dtb OUTPUT.dtb`
and embed the same output in **both** `vendor_boot` and `dtbo`, rebuilding their
AVB descriptors. The tool removes CCM clock 111's assignment, wires SDMA2 to
`IMX8MP_CLK_AUDIO_AHB_ROOT` for both clocks, and selects the i.MX8MP SAI3/SAI5
bindings. All other properties, including the input's NPU policy, are preserved.
The workspace `build-emb03-k5.10.72-ota.py` performs this migration automatically.
It requires `--source-dir`, `--source-manifest` (eight full-partition size/hash
records) and a fresh `--name`; it never guesses a sleep-p4 or v12 baseline.
The input DT's NPU policy is preserved. Kernel-repair deltas reject a change in
NPU policy, obsolete target DTs, or a source image whose full hash does not match.
For a DT-only source such as r3, supply inherited boot/vendor images explicitly
with the delta builder's `--source-images`; every inherited image is hash-checked.

SDMA rejects missing clocks rather than silently programming the wrong ratio.
Runtime-PM SDMA controllers handle interrupts in an ONESHOT IRQ thread, check
resume errors before MMIO, and use IRQ-safe channel locking. GPU DMA-BUF import
errors release every acquired reference; deferred-free initialization failure
frees backing pages synchronously. PMIC optional GPIO errors propagate, and the
NUMA-only speculative-fault interleave guard is corrected without enabling NUMA.

These fixes address reviewed defects, not a proven explanation of the historical
reboot loop. DT migration has offline behavioral checks; audio capture, IRQ/PM
behavior and reboot stability still require separately authorized hardware tests.
Never install the kernel alone with an unmigrated DT or unmatched modules.

## Automatic reset diagnostics (test build)

`CONFIG_IMX2_WDT_RESET_DIAGNOSTICS` logs `RESETDIAG` events from early probe,
watchdog feed entry and completion, hardware pretimeout, software restart,
emergency restart, power-off and panic. Successful feed completion means both
regmap writes returned, not an independently measured hardware counter reload.
The existing watchdogd's SETTIMEOUT requests a five-second pretimeout when an
IRQ is available and the timeout exceeds five seconds. Shorter timeouts request
disable. WICR is read back, the observed value is logged, and a mismatch is
reported rather than claiming the pretimeout was armed (firmware may have
locked its configuration). Feed messages use deferred printk to avoid draining
the serial console on the feed path.
The pretimeout logs the interrupted CPU's stack, does not feed or panic, and
leaves the original reset deadline intact. It cannot diagnose a CPU that masks
the IRQ or a power loss. No cross-CPU backtrace guarantee is made.

The diagnostic defconfig uses a 4 MiB printk ring, watchdog sysfs, the softlockup
detector and a ten-second initial hung-task threshold; detector-triggered panic
is disabled. Android init may write hung_task_timeout_secs=0, disabling that
detector after boot; the collector records the effective value, and no Android
hung-task coverage is claimed without a nonzero readback. Watchdog pretimeout
and software-reset attribution do not depend on this sysctl. This build does
not reserve RAM for ramoops or promise DDR survives a PMIC cold reset.
Capture starts when ADB first enumerates, not at boot completion:
the workspace `scripts/capture-early-boot-adb.py` streams logcat, `dmesg -w` and
one-second lightweight state samples concurrently, writes host arrival times,
and reconnects into separate cycle directories. It never clears logs, pushes
files, reboots, changes watchdog ownership, or modifies the device.

---

## Key Hardware Features & Integrated Drivers

- **PMIC & Power Rails**: NXP PCA9450 power management IC support with `factory48,emb03-ldo4-init` configuration (with legacy fallback).
- **USB & Cold-Boot Hub Recovery**: DWC3 USB controller support with power cycling and hardware reset sequence for Terminus USB 2.0 Hub (VID:PID `1a40:0101`).
- **Cellular 4G Modem**: Fibocom L716CN modem serial endpoints integrated into the `option` driver, preserving ECM network and ADB interfaces.
- **Wi-Fi & WoWLAN**: Cypress CYW43455 SDIO Wi-Fi (`brcmfmac`) firmware basename alignment and legacy PNO scheduled scan fixes for reliable wake-on-wireless.
- **MIPI DSI Display**: Raydium RM67191 AMOLED panel driver with factory DCS sequences, 720x1280 @ 80.29MHz timings, and dynamic panel lifecycle listener interface (`include/linux/emb03-panel.h`).
- **Touchscreen**: AiXieSheng AXS15205 multi-touch I2C driver (Type-B multi-touch) with panel readiness synchronization and workqueue retry.
- **Bluetooth**: Integrated `mx8_bt_rfkill` power and wake line management.
- **Power Management & Batteryless Support**: Silergy SY6915 charging controller and CW2015 fuel gauge with `factory48,batteryless` mode support.
- **Audio Subsystem**: Everest Semiconductor ES8316 codec and i.MX8MP SAI3 machine driver aligned with Android 48kHz HAL contract.

---

## v10 Dedicated Test-Terminal Kernel

The `dev` cutover is for an owner-controlled EMB03 acceptance terminal, not a
production trust claim. `/proc/cmdline` presents a software boot-state view;
the kernel's saved command line, U-Boot, physical unlock state and eFuses remain
unchanged. Empty-path `truncate` failures receive a 5 microsecond compensation
budget that still requires timing calibration on the target board.

Privilege entry requires init-namespace UID/euid 2000 in the SELinux `shell`
domain. Credentials are replaced with the standard COW API, seccomp is retained,
and application allowlists cannot grant privilege. Runtime SELinux policy
mutation and PTY relabeling are removed. Maintenance permissions must be supplied
by the matching, compiled firmware policy: **do not deploy this kernel alone on
the old firmware**. Application maintenance-path AVC decisions remain denied;
only their audit records are filtered before creation. System-domain diagnostics
are retained. OTA installation and reboot remain explicit human actions; CI only
builds offline artifacts.


## Build Instructions

### Prerequisites
- Host OS: Ubuntu 20.04 / 22.04 LTS x86_64
- Host tools: `build-essential`, `bc`, `bison`, `flex`, `libssl-dev`, `libelf-dev`, `python3`, `ccache`
- Cross Toolchain: Android Clang `r416183b` (`clang-7284624` / LLVM 12.0.5), the toolchain NXP uses for 2.6.0

### Building Locally

```bash
# 1. Export toolchain to PATH
export PATH="/path/to/clang-r416183b/bin:$PATH"
export CLANG_TRIPLE=aarch64-linux-gnu-

# 2. Configure kernel
make O=build/out ARCH=arm64 LLVM=1 LLVM_IAS=1 \
     CROSS_COMPILE=aarch64-linux-gnu- emb03_factory48_defconfig

# 3. Build kernel Image and modules
make O=build/out ARCH=arm64 LLVM=1 LLVM_IAS=1 \
     CROSS_COMPILE=aarch64-linux-gnu- \
     KCFLAGS="-Wno-incompatible-pointer-types" \
     -j$(nproc) Image modules
```

---

## CI / Automated Builds

Automated CI pipelines are configured via GitHub Actions (`.github/workflows/build-kernel.yml`):
- **Ccache & Toolchain Cache**: Optimized compiler caching for fast incremental builds.
- **Triggers**: Manual dispatch (`workflow_dispatch`) and version tag pushes (`v*`).
- **Artifacts**: Generates `Image`, `modules.tar.gz`, `System.map`, `Module.symvers`, and checksums.

---

## License & Copyright

- The Linux kernel is licensed under the terms of the **GNU General Public License version 2 (GPL-2.0)**, as specified in `COPYING` and `LICENSES/preferred/GPL-2.0`.
- Platform enhancements and drivers: Copyright &copy; 2026 **Factory48 Labs**. All rights reserved.
