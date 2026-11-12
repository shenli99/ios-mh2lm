# 0121a14：DarwinBoot 与 XNU boot_args ABI 错位

2026-11-12。power0成功后核对实际云端头文件，发现确定的ABI错误，并离线复现HFS默认
缓冲大小溢出。**修正版身份档已实机进入系统，原panic本轮未复现；下一步retained。**

## 证据与影响

实际`xnu-buildtree-0121a14.gz`的源文件与EXPORT_HDRS均定义ARM64
`BOOT_LINE_LENGTH=608`。此前DarwinBoot的`BootArgs.h`和局部命令行数组却使用1024。

| 字段 | 实际XNU 8796 | 原DarwinBoot |
|---|---|---|
| CommandLine | 偏移0x6c，608字节 | 偏移0x6c，1024字节 |
| bootFlags | 0x2d0 | 0x470 |
| memSizeActual | 0x2d8 | 0x478 |
| 结构体大小 | 0x2e0 | 0x480 |

失败的`fix-panel-identity`参数长739字节。按原loader布局构造交接内存后，XNU读取的
`bootFlags`字节为`sweetdis`，`memSizeActual`字节为`playcore`，后者按小端整数是
`0x65726f6379616c70`。power0成功档只有590字节，这两个位置恰好全0，
`arm_vm_init()`因此使用`memSize`回退值。它不是显示开关的独立故障证据。

实际产物`.CFLAGS`有`-DXNU_TARGET_OS_OSX`：

1. `osfmk/arm64/arm_vm_init.c:1891`将非零`memSizeActual`传给`max_mem_actual`。
2. `bsd/kern/kern_mib.c:915`在OSX构建中将`hw.memsize`绑定到`max_mem_actual`。
3. `bsd/sweet_hfs/core/hfs_journal.c:1552`据`hw.memsize`计算`int def_tbuffer_size`。
   将实际源码片段独立编译，正常3GiB得到1572864，错误值得到**-328335360**。
4. 后续事务缓冲分配会收到错误大小；此前已定位的`_end_transaction`在memcpy中向NULL写。

ABI错位和默认大小溢出已离线证实，与原panic高度吻合。原崩溃现场的分配返回值、事务内容
未捕获，修正后身份档已启动通过；不把这次结果写成完整panic重现或所有HFS错误修复。
短参数时原loader仍把真实内存值写在错误偏移，只是0回退掩盖了错误。

底稿均在`/home/shenli/work1/scratch/artifact-0121a14/`：
`actual-arm64-boot.h`、`actual-CFLAGS`、`boot-abi-sources/`、
`bootargs-hfs-default-repro.c`、`bootargs-abi-root-cause.json`。

## 修正范围与验证

- `boot-support/0002-mh2lm-darwinboot-xnu8796-bootargs.patch`将结构体及局部数组统一为608，
  静态断言核对flags、实际内存与整个结构体偏移。既有长度检查随数组限制拒绝超过607字节
  的输入，不静默截断。补丁只应用到隔离的loader构建副本，不修改参考仓库。
- `tools/mh2lm-boot-args.py`在打包前核对ASCII、长度及重复参数；面板矩阵省略已核对的
  默认值。保留USB/电池及所有非默认硬件模式；省略keys1仍默认启用，loglevel仍默认INFO，
  显示probe仍默认1。power1隐含preserveOff，无需重复参数。性能日志仍启用。
- identity参数477字节，其余三档605字节，均包含在607字节上限内。未扩展XNU ABI，
  未修改HFS、内核驱动、CircleCI或Github Actions。
- ASan/UBSan测试编译**实际loader头文件与实际内核头文件**，逐字段核对布局；执行实际
  loader参数解析片段，覆盖长度0–1024、非ASCII和NULL输入；检查四个实际shell参数分支。
  超长/重复输入失败时TOML不变。旧manifest被实际打包入口拒绝。
- 完整Mu RELEASE FD已构建，PE→FFS→FVMAIN→压缩FV→FD验证通过；BootShim、两个追加
  DTB及原baseline保持。四个boot的header/FAT/kernel/ADT/TOML均回读通过。

新manifest：`/home/shenli/work1/out-bs/panel-uefi-0121a14-bootargs608/panel-uefi.json`。
blob SHA256：`27e7a1d1a3f9421af18d02f216e4e7d651137d42f2955be775f9142f118ea480`。
内核仍为0121a14，SHA256：`7943a67e340c00715f1d32be891204d9e9a0c209c93d9f59d9fed70aaf68322f`。
新boot校验底稿：`boot-abi608-verification.json`。

## 当前测试顺序

`abi608-panel-identity`已通过：用户确认进入系统，持久日志
`sweetlog-0121a14-abi608-panel-identity.bin` SHA256
`fa471912e0284870a59ea85b419d8cec6c6868a1be61e66f48f80cf14479c938`，total242952/flush9。
386行477字节参数与TOML完全一致；88行source2/profile4、89行MDSS、100行RPMh，
666行SpringBoard，5条touch。无panic/驱动ERROR，也无显示IRQ/scanout/生命周期操作，
符合metadata-only。日志没有直接输出实际内存值或HFS分配；不替代reset/供电验收。
仍有3USB warning、13HFS clonefile -34、3BindFS ENOENT及2signal-reason分配失败。
无unmount记录，不据此判断完整关机或OOM。分析底稿`abi608-panel-identity-log-analysis.json`。

**当前只测**`/home/shenli/work1/out-bs/boot-16k-0121a14-abi608-panel-retained.img`，
SHA256 `a0bf30f21b2fab53bfc5fe076be45ec24d648cc74dea3eb78e961bb0dd94e828`。
参数605字节、lifecycle2；保留供电票。进入系统后调一次亮度，手动电源息屏5–10秒再唤醒，
做两次，确认画面/亮度/触摸/三个按键；无需USB插拔、自动息屏或长测。
日志命名`sweetlog-0121a14-abi608-panel-retained.bin`。
首次温态接管不自动reset；真正的完整OFF表/ON复位及触摸恢复由手动OFF/ON触发。
本档通过后依次panel-combined、panel-supply，每档先检查日志。失败停止后档，
若启动panic则记录PC/LR/FAR/ESR。后两档继续暂停。无需push或新云编译。
**不再复测ABI错误的原失败镜像。**
旧镜像全部保留用于取证，不因修正成功就认定其它路径不存在内存问题。
