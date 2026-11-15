# ios-mh2lm

Apple iOS 16.7.16 userland on the **LG G8X ThinQ** (`mh2lm`, Qualcomm SM8150 / Snapdragon 855), using an XNU-8796 kernel and the iPhone X / D22AP identity. This port builds on [ios-guacamole](https://github.com/wochgn/ios-guacamole) and [ios-sweet](https://github.com/dipa-studio/ios-sweet); their board parameters and boot commands are not MH2LM instructions.

As of **2026-11-15**, the phone reaches SpringBoard, opens apps, accepts touch and hardware keys, controls brightness, and charges the battery. The latest hardware baseline is **0121a39 / 0229, mode7 / PV**. Two normal display OFF/ON cycles and the outer DSI/MDP client references (C08 items 3 and 4) passed. The complete display driver is still in progress.

[中文说明](README.zh-CN.md) · [Current status](docs/STATUS.md) · [Documentation](docs/README.md) · [Active handoff](NOTES.md) · [Build guide](docs/BUILD-GUIDE.md)

## What works

| Area | Current state |
|---|---|
| Boot, CPU, storage | LG ABL → Mu-Silicium UEFI → UniXNU / DarwinBoot → XNU; 16 KiB pages, 8 cores, 3 GiB configured memory window, UFS and a writable HFS+ root in `userdata` |
| Userland | iPhone X iOS 16.7.16 (20H392): launchd, backboardd, SpringBoard, lock/home screens and stock apps; root configuration includes port-specific adjustments |
| Display | 1125×2436 canvas cropped to the 1080×2340 panel; SweetMDSS owns scanout buffer A and rebuilds the fixed VIG0→LM0→PP0→INTF1 pipeline. Hardware RD_PTR runs at about 60 Hz; CPU copying remains about 30 Hz |
| Panel and backlight | R66456 profiles, PV selected on hardware; normal power-vote/reset/host/PHY/PLL/HS/ULPS/ESC cycles, full ON/OFF tables, default SDR colour and HS brightness restoration tested |
| Input and USB | IRQ-driven Synaptics TouchComm touch, three hardware keys and DWC3 CDC-ACM logging. Touch still uses GPIO bit-bang I²C; USB is a board-specific gadget |
| Battery | runtime3 completed CC→CV→hardware termination to 100%. The 0169 temperature route attaches successfully; an actual temperature IRQ remains unobserved |
| Logs and shutdown | USB live console, persistent UFS log ring, PSCI shutdown/restart |

## Remaining work

Rendering is entirely on the CPU. A 60 Hz hardware scan does not establish a 60 fps UI. Initial pixels, console/panic and some shared hardware state still depend on ABL. Surface direct scanout, reconstruction after shared core/SMMU/RSC/QoS state loss, and full display lifecycle/IOPM handling remain unfinished. See the [a39 hardware evidence](docs/PANEL-D1-0121A39.md) and [display plan](docs/FRAMEBUFFER-PANEL-PLAN.md).

There are no validated GPU, Wi-Fi, Bluetooth, cellular, audio or camera drivers in this port. Setup/activation and security policies use bring-up configuration; permissive AMFI and compatibility stubs such as AppleKeyStore do not provide the original device's data protection. This is an experimental port with incomplete phone functionality. Known issues are tracked in [BUGS](docs/BUGS.md).

## Build and test

The active kernel CI is **CircleCI**, configured in [.github/workflows/circleci.yml](.github/workflows/circleci.yml), with `machine_config=MH2LM` and `kernel_config=DEVELOPMENT`. The user pushes and downloads the kernel/buildtree artifacts; the agent verifies patched sources, markers and hashes, packages ABI608 images, and reads them back. Follow the [build guide](docs/BUILD-GUIDE.md) for exact commands and responsibilities.

The Android boot image contains BootShim + Mu FD + LG DTB in its kernel segment, and XNU/ADT/menu files in a 32 MiB FAT16 ramdisk. MH2LM uses this UEFI path. The inherited guacamole bare-metal shim and the early 512 KiB FV payload are historical references. The iPhone X `/product` ADT properties remain required for userland artwork; Apple device trees are local inputs, not repository assets.

Temporary tests use `fastboot boot` against the existing UFS/HFS+ root. The historical persist4 image and the latest a39 test image are separate artifacts; test acceptance does not imply installation into `boot_a`/`boot_b`. Partition writes and persistent boot changes follow the explicit procedure in the build guide.

## Roadmap and repository

Next: C08 item 5, aggregate physical resource transitions across clients, followed by item 6 command/scan coordination. Shared-state reconstruction remains PD4, followed by the remaining PD1/PD5 work, F5 direct scanout and F7 cleanup. The broader order is IRQ → display → logging → whole-device power management.

| Path | Purpose |
|---|---|
| `xnu/patches/`, [XNU notes](xnu/README.md) | Ordered kernel patches, currently through 0229 |
| `tools/` | ADT generation, artifact/image checks, logging and local validation |
| `docs/`, `NOTES.md` | Plans, architecture, version evidence and active handoff |
| `shim/`, [GUACAMOLE.md](GUACAMOLE.md) | Inherited implementation and OnePlus hardware reference |

Device dumps, backups, serial numbers, Apple firmware/root images and generated kernels/buildtrees are excluded from git. iOS itself is not distributed here. Thanks to Apple's open-source projects, BearSSL, blacktop/darwin-xnu-build, UniXNU, Mu-Silicium, ios-guacamole, ios-sweet, and LG/Qualcomm's published hardware sources.
