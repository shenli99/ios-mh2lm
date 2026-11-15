# ios-mh2lm

在 LG G8X ThinQ（`mh2lm`，高通 SM8150 / 骁龙 855）上运行 iPhone X / D22AP 的 iOS 16.7.16 用户态。本项目继承 [ios-guacamole](https://github.com/wochgn/ios-guacamole) 与 [ios-sweet](https://github.com/dipa-studio/ios-sweet) 的 XNU 移植工作；它们的机型参数和启动命令不能直接用于 G8X。

截至 **2026-11-15**，可进入桌面、打开应用、使用触摸和三键、调节亮度并实际充电。最新实机基线为 **0121a39 / 0229，mode7 / PV**：两轮普通息屏/唤醒以及 C08 第3、4项外层 DSI/MDP 引用已验收。完整显示驱动仍在开发，当前是 CPU 软件渲染。

[English](README.md) · [当前状态](docs/STATUS.md) · [文档索引](docs/README.md) · [有效交接](NOTES.md) · [构建指南](docs/BUILD-GUIDE.md)

## 已有能力与边界

| 模块 | 当前状态 |
|---|---|
| 启动、CPU、存储 | LG ABL → Mu-Silicium UEFI → UniXNU / DarwinBoot → 16 KiB 页 XNU；8核、3 GiB 配置窗口，UFS 上的 HFS+ 根卷 |
| 用户态 | iPhone X iOS 16.7.16（20H392）的 launchd、backboardd、SpringBoard、锁屏、桌面及系统应用；根卷包含移植所需配置调整 |
| 显示 | 1125×2436 用户态画布，裁剪到 1080×2340 面板；SweetMDSS 持有自有 A 扫描缓冲并重建固定 VIG0→LM0→PP0→INTF1 管线；硬件 RD_PTR 约60 Hz，CPU 仍约30 Hz 原地拷贝 |
| 面板、背光 | R66456 系列、实机选中 PV；普通供电票/reset/DSI host/PHY/PLL/HS/ULPS/ESC、完整 ON/OFF 表、默认 SDR 色彩及 HS 亮度恢复已有实证；共享状态掉电重建未完成 |
| 输入、USB | Synaptics TouchComm 触摸、三键、DWC3 CDC-ACM 日志已用 IRQ；触摸总线仍为 GPIO 模拟 I²C，USB 仍为板级 gadget |
| 电池 | runtime3 已完成 CC→CV→硬件截止并充满；0169 温度路由挂接已验，实际温度 IRQ 触发仍待自然观察 |
| 日志、关机 | USB 实时日志、UFS 持久日志环，PSCI 关机/重启；完整日志管理和整机低功耗尚未完成 |

硬件扫描60 Hz不代表 UI 达到60 fps。首次交接像素、console/panic 和部分共享配置仍依赖 ABL；F5 surface 直扫、完整 PD4/PD5 与 IOPM 收口尚未完成。最新证据与限制见 [a39记录](docs/PANEL-D1-0121A39.md)。

GPU、Wi-Fi、蓝牙、蜂窝、音频、相机等尚无本项目已验收驱动。Setup、激活和安全策略仍是移植环境配置，不能视为完整可用的手机系统；AppleKeyStore/AMFI 等兼容实现不提供原机的数据保护能力。已知问题统一记录在 [BUGS](docs/BUGS.md)。

## 构建与测试

内核使用 **CircleCI**，配置为 [circleci.yml](.github/workflows/circleci.yml)，目标 `machine_config=MH2LM` / `kernel_config=DEVELOPMENT`。用户 push 并下载 kernel/buildtree；agent 核对补丁源码、标记与 SHA 后使用 `tools/mk-mh2lm-event-matrix.sh` 打包 ABI608 测试镜像并回读。具体命令见 [构建指南](docs/BUILD-GUIDE.md)。

boot image 的 kernel 段承载 BootShim + Mu FD + LG DTB，32 MiB FAT16 ramdisk 承载 XNU、ADT 与菜单。当前不是 guacamole 的裸机 shim 路线，也不是早期 FV 内512 KiB载荷路线。ADT 的 iPhone X `/product` 属性仍是用户态图形资源的必要输入；Apple 设备树不入 git。

测试使用用户执行的 `fastboot boot`；已有 UFS/HFS+ 根卷无需随每版内核重建。历史 persist4 与最新 a39 临时镜像分开记录，不能由测试通过推断已经持久刷入。分区写入、sudo、push 和设备操作分工见构建指南。

## 路线与资料

当前先推进 C08 第5项多使用者汇总后的物理资源转换，再结合第6项提交协调；共享 core/SMMU/RSC/QoS 状态丢失后重建单列 PD4，随后补 PD1/PD5、F5 直扫和 F7。四大目标顺序为 IRQ → 显示 → 日志 → 整机电源管理；电池保留项不自动重新成为显示前置。

| 路径 | 用途 |
|---|---|
| `xnu/patches/`、[XNU说明](xnu/README.md) | CI 使用的顺序补丁，当前至0229 |
| `tools/` | 产物核对、ADT、镜像打包、日志及本地验证工具 |
| `docs/`、`NOTES.md` | 计划、架构、版本证据与有效交接 |
| `shim/`、[GUACAMOLE](GUACAMOLE.md) | 继承的启动实现和一加硬件参考，不是当前 MH2LM 操作入口 |

设备 dump、备份、序列号、Apple 固件/根卷及生成的 kernel/buildtree 不入 git。本仓库不分发 iOS。感谢 Apple 开源项目、BearSSL、blacktop/darwin-xnu-build、UniXNU、Mu-Silicium、ios-guacamole、ios-sweet 及 LG/Qualcomm 开源硬件参考。
