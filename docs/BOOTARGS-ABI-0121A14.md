# 0121a14：DarwinBoot 与 XNU boot_args ABI 错位

2026-11-12。power0成功后核对实际云端头文件，发现确定的ABI错误，并离线复现HFS默认
缓冲大小溢出。修正版已本地构建、打包，**尚未实机；不能提前宣布原panic消失**。

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
未捕获，修正后启动是否通过仍须实机；不把这次离线结果写成完整panic重现。
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

先只测`/home/shenli/work1/out-bs/boot-16k-0121a14-abi608-panel-identity.img`，
SHA256 `959b734560beaf05eec555a0bc22a489d0ba89fa1ac3dd756d02801629e11bff`。
该档仍仅身份/资源metadata，不启用reset、vendor ON/OFF或供电周期。
用户确认能否进系统，成功导`sweetlog-0121a14-abi608-panel-identity.bin`，核对参数与身份。
若仍panic，记录PC/LR/FAR/ESR。**不再复测ABI错误的原失败镜像。**

本档通过后，再依次恢复同前缀的panel-retained、panel-combined、panel-supply，
每档检查后才进入下一档。后三档已打包但暂停。无需push或新云编译。
旧镜像全部保留用于取证，不因修正成功就认定其它路径不存在内存问题。
