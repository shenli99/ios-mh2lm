# MH2LM 编译、打包与刷写指南

适用：LG G8X（mh2lm）iOS 16 移植，2026-11-08 起的流程。目标是让接手的 agent 不必再从
历史脚本里反推。路径默认在 WSL `/home/shenli/work1`（下称 `W`）。

## 0. 总览

```
ios-mh2lm 仓库(xnu/patches/*.patch)
   └─用户 push → CircleCI(macOS, Xcode 15.4) 编 XNU
        └─用户下载两个产物到 W/logs/
             xnu-kernel-<LABEL>.gz     （kernel.development.mh2lm + SHA256SUMS）
             xnu-buildtree-<LABEL>.gz  （补丁后的完整源码树 + 编译参数，供本地核对/交叉编译）
                └─tools/mk-mh2lm-event-matrix.sh  → W/out-bs/boot-16k-<LABEL>-<profile>.img（测试，fastboot boot）
                └─out-bs/mk-boot-persist*.sh       → W/out-bs/boot-persistN.img（刷 boot_a/boot_b）

boot.img（Android header v1, page 2048）
   kernel 段 = gzip(BootShim 112B + Mu-Silicium FD 3 MiB) + LG DTB     → out-bs/kernel-32m*.blob
   ramdisk 段 = 32 MiB FAT16 卷（卷标 MH2LMSP）：
       EFI/UniXNU/unixnu.toml              启动菜单/参数
       EFI/UniXNU/Payloads/kernel.macho    XNU 内核
       EFI/UniXNU/Payloads/kernel.macho.adt  ADT（out-bs/adt-16k-3g.adt，3 GiB 窗口）
       EFI/UniXNU/Payloads/MassStorage.efi （持久镜像才有）
```

分工：**用户**负责 push、下载 CI 产物、所有 fastboot 操作和 sudo；**agent** 负责补丁、本地验证、
核对产物、打包、写记录。调试期只 `fastboot boot`；刷分区前说明风险并得到确认。

已实现功能在后续使用中暴露的独立问题统一记入 [Bug 台账](BUGS.md)，
不自动作为当前主计划的新步骤或前置测试。

a29已实机完成显示接管，当前按用户要求先[对照Lineage唤醒流程](PANEL-LINEAGE-PATH-COMPARISON.md)，
暂缓0219云编译/诊断实测。之后若构建含0219的诊断产物，须包含 `SWEET PANEL WAKE:`、
`reset-proof-ready`、`dma-allocate-enter`（并保留0218 CONFIG）；打包门会拒绝旧a29。
沿原 ABI608/ADT/retained 参数打包，详见 [实测与下一轮取证](PANEL-D1-0121A29.md)。

## 1. 写补丁（agent）

1. 基线：从最新 `logs/xnu-buildtree-<最新LABEL>.gz` 解出相关文件当 `base/`，复制一份为 `edited/`，
   工作目录放 `W/scratch/<主题>/`（不入 git）。
   ```sh
   tar -xzf W/logs/xnu-buildtree-0120a18.gz -C base --wildcards 'xnu/iokit/Kernel/arm/SweetBattery.cpp' ...
   ```
   只改 `edited/`，用脚本做修改（`edit-*.py`，每处 `assert count==1`），方便重放。
2. 生成补丁并校验：参照 `scratch/battery-0169/generate-verify.py`——`git diff` 生成
   `ios-mh2lm/xnu/patches/NNNN-mh2lm-<主题>.patch`，再对 `base/` 做 `git apply --check` + apply，
   结果与 `edited/` 逐字节相同，写入 `verify/`。编号顺延（当前最大 0169）。
3. 本地交叉编译（不需要 macOS）：参照 `scratch/battery-0169/compile.py`。它用 0114a13 CI 树里记录的
   编译参数（`scratch/irq-final-0114a13/ci`、`scratch/mux-service-0115/native-0116-command.txt`）和
   Debian `clang-19` 编译 7 个电池/mux/SPMI 文件。`--verify` 编 `verify/`。比较告警：新增类别要解释。
   其他文件按同样方式把路径加进列表。
4. host 测试：`for t in ios-mh2lm/tools/tests/test-*.py; do python3 $t <源码树>; done`，
   源码树是 `edited/` 或 `verify/`（含 `bsd/`、`iokit/`）。全部 PASS 才提交。改了结构体/路由表要同步改测试里的
   mock（`tools/tests/sweet_mux_test_support.py` 等）。
   **0121a1 教训：** 验证不得额外加 `-I <整个XNU或edited根目录>` 来让私有头文件可见。
   这会掩盖 CI 中不存在的搜索路径。保证源码同目录头文件齐全，沿用真实的 component 搜索路径；
   对未导出的 BSD 私有头，BSD 用同目录双引号，IOKit 用项目已有的源码相对路径。
   若为移植本地工具链添加兼容参数，须说明哪些是兼容调整，不将其误称等价 CI。
5. CI 门：在 `.github/workflows/circleci.yml` 的 Build 步骤加 `require_marker <文件> '<补丁独有字符串>'`，
   防止补丁被静默拒绝。**字符串里不能有 `<<`**（CircleCI 模板会解析，曾导致配置损坏）。本地可用
   `python3 -c "import yaml;yaml.safe_load(open(...))"` 和对 `verify/` 逐条检查 marker。
6. 打包脚本：新功能需要新启动参数时，在 `tools/mk-mh2lm-event-matrix.sh` 加 profile，并在其中
   `grep -Fqa` 检查内核里有新日志字符串（旧内核拒绝打包）。`bash -n` 检查语法。
7. 提交到 ios-mh2lm（不 push），更新 `NOTES.md` 交接段。告诉用户需要 push。

## 2. 云端编译（用户）

- CI 是 CircleCI，配置文件放在 `ios-mh2lm/.github/workflows/circleci.yml`（项目设置指向此路径）。
  push 后触发；也可手动带参数 `machine_config=MH2LM kernel_config=DEVELOPMENT`。
- 构建流程：取 blacktop/darwin-xnu-build@6639065，把 `xnu/patches/*` 拷进去，`build.sh` 打补丁并编译，
  然后检查 require_marker。**dxb 把打不上的补丁当警告**，所以 marker 是唯一的“补丁生效”保证。
- 产物：`xnu-kernel`（kernel-out.tgz）、`xnu-buildtree`（buildtree.tgz）、`xnu-build-log`。用户另存为
  `W/logs/xnu-kernel-<LABEL>.gz`、`W/logs/xnu-buildtree-<LABEL>.gz`。LABEL 惯例 `0120aNN` 递增。
- agent 不能直接看 CI；先去 `W/logs/` 找产物，没有再问用户。

## 3. 核对 CI 产物（agent）

参照 `scratch/artifact-0120a18/verify-artifacts.py`（复制后改 LABEL 和期望源码目录）：
1. 从 buildtree 取出补丁涉及的源码，与本地 `verify/` 逐字节比较。
2. 对 buildtree 源码逐条执行 circleci.yml 里的 `require_marker`。
3. 解 kernel 包，`kernel.development.mh2lm` 的 SHA256 必须出现在 `SHA256SUMS`；`grep` 新补丁的日志字符串。
4. 结果写 `scratch/artifact-<LABEL>/verification.json`。没有构建日志时注明“未查全量警告”。
**防旧产物**：新内核 SHA 必须与上一 LABEL 不同，且含新补丁字符串（曾因下载到旧产物白查一轮）。

## 4. 打测试镜像（agent）

```sh
cd W/ref/ios-mh2lm
tools/mk-mh2lm-event-matrix.sh W/logs/xnu-kernel-<LABEL>.gz <LABEL> --battery-basic-charge
```
- 输入：内核包、`out-bs/kernel-32m.blob`（DEBUG UEFI）、`out-bs/adt-16k-3g.adt`、`out-bs/ramdisk-clean`。
- 输出：`out-bs/boot-16k-<LABEL>-<profile>.img`（+ 同名 kernel/ramdisk/toml），一次生成同一内核的多档。
- 回读：参照 `scratch/artifact-0120a18/verify-boots.py`，检查 header、kernel 段、ramdisk 内
  kernel/ADT/TOML 逐字节一致、参数无重复且符合预期。
- 常用 profile：`--battery-basic-charge`（runtime3 充电，0169 内核额外出 `-tempirq`）、`--irq-only`
  （三键/触摸/USB 基线）。`--framebuffer-contract` 需要0173，输出 baseline/owned 两档，
  只差 `sweetfbcontract=0/1`；测试与限制见[自有快照实验](FRAMEBUFFER-OWNED-CONTRACT.md)。
  `--framebuffer-proof` 需要0174，仅输出 `fb-contract-proof`（contract=1、proof=1），
  用于关联同一 swap 的自有像素、组帧和扫描内存回读，见[同ID取证](FRAMEBUFFER-SAME-SWAP-PROOF.md)。
  完整列表看脚本 usage。

用户测试：`fastboot boot W/out-bs/boot-16k-<LABEL>-<profile>.img`，测完导出日志（第 6 节）。

### 4.1 0201完整固件与面板生命周期四档

**0121a17当前执行点：** 已核对0204 ACV metadata诊断：78源码字节一致、114 source/154
binary markers及ABI608镜像回读通过。只启动`boot-16k-0121a17-abi608-panel-retained.img`，
停留约一分钟导日志，目标取ACV具体gate/字段；这不是ACV修复版，仍可能显示静帧。
无需息屏/插拔/长测；identity不重测，combined/supply继续暂停。
见[0121a17镜像与步骤](PANEL-D1-0121A17.md)。以下旧执行点按历史保留，以本段及NOTES为准。

0121a14已下载并核对0201补丁、SHA及实际产物，本轮ABI修正只涉及loader和本地打包，
可复用该内核，无需push/云编译。未来内核仍需核对相应marker，不能拿不含0201的旧kernel只改参数。
完整Mu FD已在本地构建，包含ABL→Mu HOB→DarwinBoot→ADT身份传递adapter。
原`out-bs/kernel-32m.blob`未改；完整FD构建成功尚不代表手机启动成功。

在仓库根目录执行（将`LABEL`替换为用户实际下载编号）：

```sh
SWEET_PANEL_UEFI_MANIFEST=/home/shenli/work1/out-bs/panel-uefi-0121a14-bootargs608/panel-uefi.json \
  tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-LABEL.gz LABEL --panel-lifecycle
```

脚本核验manifest ABI/full-FD与blob SHA，检查新内核marker；四档共用内核/新固件。
manifest必须声明`xnu8796-arm64-608`，旧1024-byte loader布局不兼容。所有参数需ASCII且
不超过607字节；面板矩阵省略经源码核对的默认值，保留非默认硬件模式，不静默截断。
当前identity477字节、后三档605字节。原0201-final manifest及原失败镜像仅保留取证，
勿继续上机；[ABI错误与修正](BOOTARGS-ABI-0121A14.md)记录了原因和测试边界。
identity独立ADT仅增加resource/RPMh与96-byte全零身份slot，不声明NoC/Pins/Touch，使用
resource1/clock0/pins0/touch0。后三档共用包含NoC/Pins/Touch的完整ADT，使用resource2/clock1。
NoC声明与resource1/clock0不兼容，会导致SweetMDSS启动失败；不要混用两种ADT。
不接受静态指定panel。保留USB ACM、按键与电池：

| 镜像（位于`W/out-bs/`） | 测什么 |
|---|---|
| `boot-16k-LABEL-panel-identity.img` | lifecycle0、metadata-only；先确认新固件可启动、live source/profile有效且唯一。panel写入/接管开关为0 |
| `boot-16k-LABEL-panel-retained.img` | lifecycle2，供电票保留；完整表、reset、亮度与实际触摸恢复（E7） |
| `boot-16k-LABEL-panel-combined.img` | lifecycle5，DATA/clock停驻及host/PHY/PLL关闭重建 |
| `boot-16k-LABEL-panel-supply.img` | lifecycle6，reset低保持、VPNL/VDDI撤本票及恢复、FB联合完成证据 |

由用户按上述顺序`fastboot boot`，每档先导出日志并核对再进入后档。任一失败停止后档。
后三档手动息屏/唤醒并确认画面、亮度、按键、触摸；不要求自动息屏或半小时等待，
不重测旧USB/BUG002/F3矩阵。供电档保留共享L3/L5、ARC/config floor；RPMh ACK和本票撤销
不证明实际rail-off。完整模式参数由脚本生成，勿手动拼接缺依赖的档位。

需要重建固件时，用`tools/build-mh2lm-panel-uefi.py --output <新隔离目录>`；manifest记录
实际Mu/DarwinBoot HEAD、命令和SHA。不要将设备DTB、FD/blob或Apple产物加入git。
详见[供电生命周期、架构及剩余边界](PANEL-SUPPLY-LIFECYCLE-D1.md)。

## 5. 持久镜像（刷 boot_a/boot_b）

规则：默认菜单项必须是 **Mass Storage**——Android 一旦启动会清空 userdata 上的 iOS 根卷，
任何复位都要停在 UEFI 里。

### 5.1 Mu-Silicium UEFI

```sh
bash W/scripts/build-mh2-uefi.sh DEBUG     # 或 RELEASE
# 产物：W/ref/Mu-Silicium/Build/mh2Pkg/<TYPE>_CLANGPDB/FV/SILICIUM_UEFI.fd 与 AARCH64/MassStorage.efi
```
- 源码：`ref/Mu-Silicium`（cca95b7，含 UniXNU 启动链补丁）+ `ref/UniXNU-src`（DarwinBoot 4ad0c42，32 MiB 放置）。
- **FD_BASE 必须是 0xA0000000**（`Resources/Configs/mh2.toml`）。脚本默认值 2026-11-08 已改正，旧的
  0x9FC00000 会做出起不来的 FD。改完可用 `grep PcdFdBaseAddress Build/.../AutoGen.h` 确认。
- 当前 `out-bs/kernel-32m.blob` 里是 10-31 编的 DEBUG FD，旧测试档使用它；4.1的面板生命周期档使用独立的新固件manifest。**不要覆盖 DEBUG 构建目录**
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
fastboot boot  W/out-bs/boot-persistN.img      # 先试：菜单、默认进 U 盘、能进 iOS
fastboot flash boot_a W/out-bs/boot-persistN.img
fastboot flash boot_b W/out-bs/boot-persistN.img
fastboot reboot
```
boot 分区 96 MiB（0x6000000）。失败时 fastboot 仍可用，刷回上一版 persist 镜像。
历史：persist2（0107，DEBUG）、persist3（0111，DEBUG）、persist4（0120a18 充电，RELEASE UEFI，2026-11-08）。

## 6. 取日志

内核把控制台写进 userdata 最后 4 MiB 环形区（每 10 s 刷一次，`ufslogsecs=` 可调）。
- 首选：手机进 UEFI Mass Storage，Windows 管理员 PowerShell 运行
  `\\wsl.localhost\Debian\home\shenli\work1\scratch\p4-flash\pull-log.ps1 -Name <名字>`，得到
  `W/logs/sweetlog-<名字>.bin`（只读访问磁盘）。usbipd 转 WSL 会在第一条 SCSI 命令超时，不要用。
- 解码：`python3 ios-mh2lm/tools/sweet-log.py W/logs/sweetlog-<名字>.bin out.txt`，或照
  `scratch/artifact-0120a18` 里的方式按头部（magic/version/size/total/seq）自行展开环形区。
- 实时：带 `sweetusb=2` 的镜像有 USB CDC-ACM 日志（`tools/sweet-usbacm-log.sh`）。
- 命名惯例：`sweetlog-<LABEL>-<profile>.bin`。关机/重启前最后 10 s 的日志可能丢失。

## 7. 常见坑

| 现象 | 原因 / 处理 |
|---|---|
| CI 绿了但功能没变 | 补丁被 dxb 拒绝；加 require_marker，核对 buildtree 源码 |
| CircleCI 配置报错 | require_marker 字符串含 `<<` |
| Installing KDK 后 `hdiutil: attach failed - image not recognized` | KDK下载文件无法识别，尚未进入XNU编译；100%不代表文件有效。先重跑现有job重新下载，重复失败时保留文件，核对实际URL、HTTP状态/重定向、大小、file类型及hdiutil校验。可能是错误页面或损坏/截断，未核实前不定因、不改内核或CI配置 |
| 新镜像行为和上次一样 | 下载了旧产物；比 SHA 和新字符串 |
| UEFI 不启动 | FD_BASE 错（要 0xA0000000）或 FD 尺寸不是 0x300000 |
| iOS 内核静默挂起 | ADT 缺 `/chosen/darwinboot-memory-size`（要 3 GiB），用 `adt-16k-3g.adt` |
| 手机进了 Android | userdata 上的 iOS 根卷会被清；持久镜像默认项必须是 Mass Storage |
| 新根卷首启 SpringBoard 不出图 | lsd 重建数据库 10+ 分钟，等 30 分钟再判断 |
| 本地交叉编译找不到头文件 | 用 buildtree 里的 `sdk-kernel-platform`，路径替换见 compile.py |

2026-11-09用户贴出的失败片段为KDK 13.5 / 22G74下载100%后挂载不可识别，尚无完整失败
日志文件或CI run ID。旧`W/logs/xnu-build-log-0121a1.txt`第5–34行同版KDK下载、挂载校验、
安装均成功，故不是该版本从未可用。近期显示/日志诊断提交未改circleci.yml；失败下载文件
未取得，不能区分下载错误内容与传输损坏。本地无法联网复查源端，不能声称已验证下载URL。
