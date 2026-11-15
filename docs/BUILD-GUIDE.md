# MH2LM 编译、打包与取日志指南

更新：2026-11-15。适用LG G8X（mh2lm）iOS 16移植；工作区为 `/home/shenli/work1`。当前状态见[STATUS](STATUS.md)，最新实机基线为[a39/0229](PANEL-D1-0121A39.md)，已完成本轮验收，无需重编或重测同版。以下命令用于实际有新实现时的交付。

## 0. 总览与分工

用户负责push、下载CI产物、sudo及所有fastboot操作；agent负责补丁、本地验证、源码/产物核对、打包回读、记录和本地提交。调试使用 `fastboot boot`；持久刷写按第5节单独处理。独立问题记入[BUGS](BUGS.md)，不自动成为当前计划的前置测试。

```text
ios-mh2lm/xnu/patches → 用户push → CircleCI构建MH2LM
  → 用户下载kernel/buildtree/构建日志 → agent核对与打包回读
  → 用户临时启动并取日志 → 实机审计 → 更新状态/计划/交接

boot.img: Android header v1 / page2048
  kernel段: gzip(BootShim 112B + Mu FD 3 MiB) + LG DTB
  ramdisk段: 32 MiB FAT16
    EFI/UniXNU/unixnu.toml
    EFI/UniXNU/Payloads/kernel.macho
    EFI/UniXNU/Payloads/kernel.macho.adt
    EFI/UniXNU/Payloads/MassStorage.efi（持久镜像）
```

## 1. 写补丁与本地验证（agent）

1. 从最新已核对的buildtree提取相关源码为 `scratch/<主题>/base`，修改副本 `edited`。当前基线是0121a39/0229；补丁编号从仓库实际最大编号顺延，不预留空版本。
2. 生成 `xnu/patches/NNNN-mh2lm-<主题>.patch`，对基线严格apply，比较结果与edited逐字节一致；涉及累积依赖时重放整段。保留生成/验证脚本与摘要，避免用模糊上下文掩盖未应用的补丁。
3. 用真实CI编译参数做相关文件ARM64单编译，比较告警并解释新增类别。示例见工作区 `scratch/framebuffer-0229/`；本地兼容参数须记录，不能额外加入整个XNU根include来掩盖CI缺失头文件。
4. 根据改动运行相关 `tools/tests/` 状态机与故障验证；各脚本入口参数不同，按对应测试说明调用。变更共享接口时覆盖依赖consumer；通过后不无故重复整套测试。
5. 在 `.github/workflows/circleci.yml` 增加必要的 `require_marker`；字符串不能含未处理的 `<<`，CircleCI会将其解释为模板。检查YAML、模板及生成shell，核对实际源码标记。marker不能替代源码比对。
6. 新打包能力在 `tools/mk-mh2lm-event-matrix.sh` 增加必要的内核标记/profile，验证旧内核会被拒绝、参数符合ABI限制，再做shell语法检查。
7. 更新STATUS/计划/NOTES，提交到ios-mh2lm。只有确实需要新内核时才交由用户push；文档整理不要求云编译。

## 2. 云端编译（用户）

活跃CI是CircleCI，项目设置指向 `.github/workflows/circleci.yml`；参数 `machine_config=MH2LM kernel_config=DEVELOPMENT`，macOS/Xcode构建环境与固定dxb版本以该配置为准。GitHub Actions旧 `xnu.yml` 和guacamole/sweet指令不是当前流程。

```sh
git -C /home/shenli/work1/ref/ios-mh2lm push origin main
```

下载 `xnu-kernel`、`xnu-buildtree`，保存为 `logs/xnu-kernel-<LABEL>.gz`、`logs/xnu-buildtree-<LABEL>.gz`，并尽可能保存构建日志和CI run ID。LABEL按已有实验编号递增，当前最新0121a39。agent先检查本地已有产物；缺少CI元数据时明确记录，不能把文件名视为构建来源证明。

### 2.1 完整源码分支 `full-source`（可选路线）

该分支把 xnu-8796.141.3 与 dxb 构建修正、三个 staging 脚本及 0001..0229 全部补丁
提交为 `dxb/xnu/` 完整源码树，CI 读取分支内的 `.github/workflows/circleci.yml`，
**不再执行补丁步骤**；`require_marker` 与产物形式不变。版本横幅由 workflow 的
`KERNEL_BUILD_DATE` / `KERNEL_BUILD_OBJROOT` 固定为 `Mon Aug 14 05:06:07 UTC 2023` /
`xnu-8796.141.3`。

```sh
git -C /home/shenli/work1/ref/ios-mh2lm switch full-source
# 直接修改 dxb/xnu/**，不生成 patch
git -C /home/shenli/work1/ref/ios-mh2lm push origin full-source
```

边界：只有该分支是完整树，`main` 仍是补丁仓库；`xnu/patches/` 在分支上仅作历史参考；
依赖项目与 KDK 仍在云端获取。来源与树组成见 [`dxb/README-MH2LM.md`](../dxb/README-MH2LM.md)。

## 3. 核对CI产物（agent）

1. 读取gzip/tar至结尾，检查CRC/完整性，记录输入大小和SHA256。
2. 将buildtree源码与预期补丁结果比较，执行当前源码标记检查；记录受检范围。
3. 核对 `kernel.development.mh2lm` 的SHA与包内清单，检查新增内核标记，防止重下旧版本。a39首次包实际等同a38，已由重新下载解决，详见[a39记录](PANEL-D1-0121A39.md)。
4. 保存 `scratch/artifact-<LABEL>/verification.json` 等证据。没有完整build log/CI ID就标明未查全量告警/云端提交元数据；不以标记数量推断全部构建正确。

## 4. 当前ABI608面板镜像（agent）

以下为新产物命令模板；将两处 `<LABEL>` 替换为同一实际编号。命令只打包，不执行设备操作。

```sh
cd /home/shenli/work1/ref/ios-mh2lm
SWEET_PANEL_UEFI_MANIFEST=/home/shenli/work1/out-bs/panel-uefi-0121a14-bootargs608/panel-uefi.json \
  tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-<LABEL>.gz <LABEL>-abi608 --panel-pipeline
```

输出 `out-bs/boot-16k-<LABEL>-abi608-panel-pipeline.img`（mode7）及 `panel-identity.img`。identity为配套metadata档，并非每轮都要求重测；本批主线mode7。脚本核对manifest/full-FD/blob SHA和内核标记，并生成对应ADT与参数。

manifest必须声明 `xnu8796-arm64-608`，命令行ASCII且≤607字节；a39 pipeline为605字节，identity为477字节。旧1024-byte loader不兼容，见[ABI说明](BOOTARGS-ABI-0121A14.md)。不能把identity的resource1/clock0与完整NoC/pins/touch ADT混用，也不能静态伪造实机panel身份。

打包后回读boot header、kernel段与FAT中的kernel/ADT/TOML，逐字节比对、检查参数重复和长度，并记录SHA。保留已验证镜像；新固件需要时才用 `tools/build-mh2lm-panel-uefi.py --output <新隔离目录>` 构建并核对。

历史/专项入口：`--panel-lifecycle` 生成identity/retained/combined/supply，mode2 retained的当前实现已包含供电票与链路周期，不能按旧文件名解释为永远保留供电。`--irq-only`、`--battery-basic-charge`、`--framebuffer-*` 等用途和依赖见脚本usage及对应版本记录。不按旧a14/a17说明重复逐档测试。

## 5. 持久镜像（刷 boot_a/boot_b）

以下保留persist4构建方式；它不是a39已持久更新的证明，也不安排本次刷写。源码提交和旧blob日期描述该历史基线，未来更新须重新核对实际源码/固件/ABI及回读结果。

规则：默认菜单项必须是 **Mass Storage**——Android 一旦启动会清空 userdata 上的 iOS 根卷，
任何复位都要停在 UEFI 里。

### 5.1 Mu-Silicium UEFI

```sh
bash /home/shenli/work1/scripts/build-mh2-uefi.sh DEBUG     # 或 RELEASE
# 产物：/home/shenli/work1/ref/Mu-Silicium/Build/mh2Pkg/<TYPE>_CLANGPDB/FV/SILICIUM_UEFI.fd 与 AARCH64/MassStorage.efi
```
- 源码：`ref/Mu-Silicium`（cca95b7，含 UniXNU 启动链补丁）+ `ref/UniXNU-src`（DarwinBoot 4ad0c42，32 MiB 放置）。
- **FD_BASE 必须是 0xA0000000**（`Resources/Configs/mh2.toml`）。脚本默认值 2026-11-08 已改正，旧的
  0x9FC00000 会做出起不来的 FD。改完可用 `grep PcdFdBaseAddress Build/.../AutoGen.h` 确认。
- 当前 `out-bs/kernel-32m.blob` 里是 10-31 编的 DEBUG FD，旧测试档使用它；第4节的ABI608面板档使用独立的新固件manifest。**不要覆盖 DEBUG 构建目录**
  （重编前先备份 `Build/mh2Pkg/DEBUG_CLANGPDB`）。同源重编的 FD 只因编译时间戳不同而不逐字节相同。
- `make-bootimg.sh`/`build-mh2lm-uefi.sh` 是 Aloha 旧线（SurfaceDuo1Pkg），已不用。

### 5.2 kernel 段 blob

`gzip(BootShim.bin + SILICIUM_UEFI.fd)`（gzip 内文件名 `SILICIUM_UEFI.fd-bootshim`）后直接拼接 LG DTB。
换 FD 时，从现有 `kernel-32m.blob` 解出 BootShim（前 112 字节）和尾部 DTB 复用，见
`out-bs/mk-boot-persist4.sh` 第 1 步（它还会断言 BootShim 与已验证的一致）。

### 5.3 打包

照 `out-bs/mk-boot-persist4.sh` 复制一份改：内核文件、iOS 项的参数来源（直接取某个已实测
profile 的 `unixnu-16k-<LABEL>-<profile>.toml` 的 `args`）、标题、UEFI 类型。用 mtools 改 FAT，不需要 sudo。
脚本最后用 `Mu-Silicium/Resources/Scripts/mkbootimg.py --header_version 1` 出镜像并回读比对。

### 5.4 刷写（用户执行，刷前确认）

```sh
fastboot boot  /home/shenli/work1/out-bs/boot-persistN.img      # 先试：菜单、默认进 U 盘、能进 iOS
fastboot flash boot_a /home/shenli/work1/out-bs/boot-persistN.img
fastboot flash boot_b /home/shenli/work1/out-bs/boot-persistN.img
fastboot reboot
```
boot 分区 96 MiB（0x6000000）。失败时 fastboot 仍可用，刷回上一版 persist 镜像。
历史：persist2（0107，DEBUG）、persist3（0111，DEBUG）、persist4（0120a18 充电，RELEASE UEFI，2026-11-08）。

## 6. 取日志与实机验收

下列命令将 `<LABEL>` 替换为实际测试编号；复测使用新后缀保存，避免把多次启动混在同一日志。先在Bash开启采集，由用户执行sudo：

```bash
sudo /home/shenli/work1/ref/ios-mh2lm/tools/sweet-usbacm-log.sh /home/shenli/work1/logs/live-<LABEL>-abi608-panel-pipeline.txt > /dev/null 2> >(tee /home/shenli/work1/logs/live-<LABEL>-abi608-panel-pipeline-host.txt >&2)
```

另一个终端由用户临时启动新镜像：

```sh
fastboot boot /home/shenli/work1/out-bs/boot-16k-<LABEL>-abi608-panel-pipeline.img
```

采集器会跨断开/重连等待唯一的ACM节点。看到 `Reading live MH2LM console` 且live文件增长才算开始采集；Waiting不算。多ACM时按实际节点设置 `SWEET_ACM_DEVICE`。观察输出可用 `tail -n 40 -F` 对应live文件。

持久日志从UEFI Mass Storage读取：Windows管理员PowerShell运行工作区 `scratch/p4-flash/pull-log.ps1 -Name <名字>`，得到 `logs/sweetlog-<名字>.bin`。此流程只读磁盘；USB转WSL曾在SCSI读取时超时，沿用已验证的PowerShell方式。解码：

```sh
python3 /home/shenli/work1/ref/ios-mh2lm/tools/sweet-log.py /home/shenli/work1/logs/sweetlog-<名字>.bin /home/shenli/work1/logs/sweetlog-<名字>.txt
```

持久区包含4 KiB头部及4 MiB环，默认约10秒刷盘；尾部缺关机日志不能直接判定回调没执行。USB与持久日志分别记录覆盖范围、SHA、NUL/回卷/截断情况，不能由一路成功推断另一路或BUG-006已修复。

新显示批次按实际改动制定验收；普通回归检查首次桌面/触摸/按键/调光，两轮息屏约5秒再唤醒，每轮正常后亮屏至少60秒，并审计资源、命令、新帧、亮度、触摸完成顺序。a39这一轮已通过，不因文档更新安排重测。60Hz硬件IRQ统计不代表60fps UI、长期耐久或完整掉电重建。

## 7. 常见问题

| 现象 | 核对 |
|---|---|
| CI成功但功能没变化 | dxb可能只警告补丁失败；核对完整源码、新标记及SHA，不只看包名 |
| CircleCI模板解析失败 | 检查marker中的 `<<` 及模板展开后的shell |
| KDK下载完成但hdiutil无法识别 | 尚未进入内核编译；保存失败日志，检查实际URL/内容/完整性，不能由下载100%断定包有效 |
| UEFI不启动 | FD_BASE须与已验证布局一致（0xA0000000），FD精确0x300000；核对ABI608及manifest |
| 内核早期挂起 | 核对ADT 3 GiB窗口、loader ABI、实际kernel/ADT和参数来源 |
| USB文件为空 | 查host是否Reading、手机是否配置ACM/打开端口；不能凭Waiting判定内核没日志 |
| 本地找不到头文件 | 沿真实CI component include路径及buildtree SDK排查，不能加整个源码根来绕过 |

2026-11-09的KDK 13.5/22G74失败片段未附完整文件或CI ID；旧0121a1日志曾成功安装同版，根因未定。该历史问题不表示a39构建失败。
