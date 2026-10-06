# Linux 5.10.9 Kernel for EMB03

[![Build EMB03 Linux Kernel](https://github.com/Factory48/linux-imx-5.10.9-emb03-factory48/actions/workflows/build-kernel.yml/badge.svg)](https://github.com/Factory48/linux-imx-5.10.9-emb03-factory48/actions/workflows/build-kernel.yml)
[![License: GPL-2.0](https://img.shields.io/badge/License-GPL%202.0-blue.svg)](COPYING)

Production Linux kernel source tree for the **EMB03** hardware platform (NXP i.MX8M Plus), maintained by **Factory48 Labs** (Singapore).

Based on NXP official Android BSP release `android-11.0.0_2.0.0` (commit `24e30e721438600a496bcf1ae44cb4d7c93eafd8`, Linux kernel version `5.10.9-emb03-factory48`).

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
- Cross Toolchain: Android Clang `r383902b` (`clang-6573524` / LLVM 11.0.2)

### Building Locally

```bash
# 1. Export toolchain to PATH
export PATH="/path/to/clang-r383902b/bin:$PATH"
export CLANG_TRIPLE=aarch64-linux-gnu-

# 2. Configure kernel
make O=build/out ARCH=arm64 LLVM=1 LLVM_IAS=1 \
     CROSS_COMPILE=aarch64-linux-gnu- emb03_factory48_defconfig

# 3. Build kernel Image and modules
make O=build/out ARCH=arm64 LLVM=1 LLVM_IAS=1 \
     CROSS_COMPILE=aarch64-linux-gnu- \
     KCFLAGS="-Werror=incompatible-pointer-types" \
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
