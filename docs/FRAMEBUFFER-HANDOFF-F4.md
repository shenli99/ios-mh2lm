# F4 第二段：受控单缓冲接管（0182，0121a12接管日志通过，待画面确认）

2026-11-10。基于已实测的0181/0121a11，新增
`xnu/patches/0182-mh2lm-controlled-scanout-handoff.patch`。
本段第一次允许MDP读取驱动缓冲；本地验证不等于接管实机成功。
默认own0、own1只读预检语义保持。只有新`sweetfbown=2`进行一次接管尝试。

## 1. 分层与所有权

```text
AppleCLCD : IOMobileFramebuffer
 ├─ 原有workloop / state gate / 硬件vsync mailbox
 ├─ SweetScanoutPool : OSObject
 │   ├─ A：连续低32位/WC，1125×2436、stride4608
 │   └─ B：本段不提交，后续双缓冲使用
 └─ retained SweetDisplayBackend*
      beginScanout(pool, channel) / scanoutStatus(pool, stack output)
      abandonScanout(pool)
          │ consumer gate → provider gate；反向没有同步回调
     SweetMDSS : IOService
      ├─ 已有ADT映射 / gate / GIC115和RD_PTR路径
      ├─ 持有pool引用，一次接管的状态和有界IOTimerEventSource
      └─ 首次源寄存器写入前，pool取得独立硬件生命周期引用
```

硬件寄存器仍只有SweetMDSS访问；framebuffer不映射或写MMIO。
匹配通知仍只绑定服务；新初始化不在matched回调里执行。
typed新增方法默认返回Unsupported，SweetMDSS明确override；不会由其它provider隐式执行。
不改USB、按键、电池、触摸、mux或GIC callback代码，不改ADT资源ABI和CircleCI。

## 2. 一次接管的状态与依据

1. own2仍先运行0181池分配和前三份只读快照。第三份后，在framebuffer自己的workloop上
   禁止继续旧路径呈现，重新复制**当时最新**ABL面板矩形到A的(22,48)，执行WC写入屏障。
   这次refresh替换启动seed，不把早期启动样本当最终交接帧。无新分配，也不反复全池清零；
   一次复制仍在原有呈现workloop中，会产生一次启动延迟，不当性能优化。
2. `beginScanout()`核对own2、同一已订阅channel、IRQ真实帧、PanelOn及未失败的IRQ状态；
   每个provider只准一次尝试。重新核对已知SMMU、唯一SMR3/SID0x800/TRANS/CB2、
   CR0=0x2b0406、CBAR=0x1f000（stage2 bypass）、SCTLR=0xe0/M0、空页表字段、global fault0。
   FSR允许0或此次记录的原始0x400；任何已知fault位或其他未知位都拒绝。
   不清FSR/RESUME、不改SMMU和页表，不把CPU物理地址自动当DMA已验证。
3. 原SRC尺寸/地址/stride/格式/unpack/op/secure、CTL_TOP、INTF1 TE/auto必须匹配基线。
   gate内断开外部TE（INTF1+0x288 bit20）并关闭autorefresh（+0x2b4），随后返回。
   独立timer每1ms推进，最多50ms；不在gate或spin lock内忙等传输完成。
4. 按LG `_sde_encoder_phys_cmd_is_ongoing_pptx()`，读取INTF1+0x2b0低16位write-line；
   `(0,2340)`表示在途。两次分隔timer采样均为0或不小于2340，并确认TE/auto仍关闭，才继续。
   不使用+0x290的SYNC_WRCOUNT配置值或+0x2ac的OUT_LINE代替这个判据。
5. 仅W1C MDP+0x1018的PP0 DONE bit8并读回确认清零，排除旧锁存值。随后在**任何源写入前**
   给pool加硬件pin；写SSPP SRC_XY=(22,48)、YSTRIDE0=4608、SRC0_ADDR=A，CTL_FLUSH只写VIG0 bit0。
   1080×2340 source/output及已验证BGRA格式、unpack、op保持。autorefresh下不写CTL_START。
6. source读回与SMMU复核通过，恢复原auto/TE；timer最多100ms检查：新的PP0 DONE、flush清零、
   提交后的RD_PTR推进，三者齐全才标记`transfer observed`。完成位本段不增加IRQ使能，
   只在一次提交的有界timer窗口观察。读回或下一RD_PTR单独都不能标记成功。
7. framebuffer查询状态后才将正常UI CPU复制目标切到A的(22,48)、stride4608。
   **单缓冲、原30Hz、原CPU裁剪仍在**；硬件SRC_XY取对应面板矩形。
   写入目标矩形后执行屏障；此时可能仍有原类原地更新撕裂，不计双缓冲/60Hz呈现验收。
8. 首次OFF→ON仍保留第四份快照预算；实际源地址、stride、crop或SMMU不符合预期，或者
   读快照失败，都明确报错、冻结新像素更新并保留pool。正常后输出`owned resume verified`。

LG依据为`drivers/gpu/drm/msm/sde/`的`sde_encoder_phys_cmd.c`（prepare_commit、
ongoing_pptx、trigger_start和pp_tx_done）、`sde_hw_intf.c`、`sde_hw_ctl.c`、
`sde_hw_sspp.c`、`sde_hw_interrupts.c`。FSR位来自`drivers/iommu/arm-smmu-regs.h`。
PP0映射及首次提交的完整性最终仍需真实画面与本次新事件实证。

## 3. 故障、停止与本段未完成项

- 接管期间DCS亮度写返回Busy，避免穿插DCS传输/TE暂存恢复破坏新提交的完成证据；
  不消耗DCS硬件故障预算。电源请求会取消接管，再走原DCS电源路径，不忽略请求。
- 提交前取消/超时且时钟可用时恢复继承的auto/TE，尚无新DMA地址，pool可正常释放。
  该次实验明确失败、停止旧路径呈现，不悄悄当作own0成功。
- 一旦源寄存器可能生效，失败不按readback/下一RD_PTR释放内存，也不自动翻回ABL。
  状态、ERROR和失败属性明确保留；IRQ失败、provider停止或consumer撤回也不能解除硬件pin。
- **0182有意将提交后的整个pool保留至重启**，包括尚未使用的B，逻辑约21.41MiB。
  pool独立引用先于源写入，不依赖已终止的provider/consumer继续存活；常规路径只允许一次
  接管，源配置不再匹配ABL后不能接受第二个pool。退出会输出硬件pin仍在的ERROR。
  这是本段的保守隔离策略，**不是最终可卸载/回收实现**；不能称资源平衡已验收。
- 下一阶段必须补真正的提交代号/完成关联、硬件停止及撤销协议，再做A/B复用与释放、
  SwapWait/性能；PD1另补不读ABL的首帧和自有console/panic。现在console/panic和默认
  surface仍有ABL依赖，不能说PD1/完整面板驱动已完成。

不把`SSPP+0x70`安全属性、raw完成状态或RD_PTR单独当release fence；本段transfer observed
不授予任何旧buffer复用/回收权。现有ABL区域仍保留，不释放，也不靠隐藏回退完成验收。

## 4. 本地验证与云端交接

- 在已核对的0181树上strict apply，零offset/fuzz，应用后源码逐字节一致；114条现有CI源码
  marker保持，工作流blob仍`da0b6c0947a66f03491b3a9023590b49665292df`。
- SweetMDSS/SweetFramebuffer基线和0182树均通过arm64-apple-darwin对象编译。
  MDSS新增5条有界数组诊断（固定资源9及pool两项），新include引出17条现有SDK命名诊断；
  FB新增2条数组/5条指针诊断（两项访问及有界矩形复制/目标偏移），已核对范围与实际guard测试。
  不是整内核云构建，无本次CI run ID或新kernel。
- 实际provider的ASan/UBSan测试覆盖20类独立输入：busy-line、旧done清除失败、fresh done、
  flush未清、IRQ不推进/丢失、真实fault/未知位、stage1/stage2改变、地址读回失败、超时、
  停止/撤回/电源打断，以及未调用stop时在Quiescing/Submitted直接free的timer撤销/移除；
  在途时不写源，写过源后pool引用跨服务free保留。free先关闭admission/取消timer、
  从workloop移除timer，再释放事件和provider持有的pool引用。
- 实际FB测试确认begin/transfer/resume失败冻结、正常复制目标/stride/crop、ABL像素不变、
  OFF/ON读自有源快照，以及停止detach。实际pool测试确认refresh边界、pin幂等、引用
  撤回后内存仍在，模拟硬件reset后再由fixture回收。LSan因环境ptrace仍关闭，引用/prepare
  平衡及guard另外断言；不声称真硬件已停止DMA。
- 打包器保留`--framebuffer-scanout`原两档；新增`--framebuffer-handoff`同时提供own0/1/2，
  除own值外参数相同。真实旧F3内核被F4 marker门拒绝，0181内核被own2新marker门拒绝，
  均在make_profile前退出，无旧核新实验boot。

底稿`work1/scratch/framebuffer-0182/`：base/edited/verify、verification.json、
tests.json、strict-apply.txt、test-{display,framebuffer,pool,profiles}.txt、对象编译输出和warning-counts.json。
仓库只收编号补丁、打包/验证工具和文档，不收二进制或日志。用户push后下载新产物，
核对0182源码/manifest/marker，再打包；目前没有可测的新boot。

```sh
tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-编号.gz 编号 --framebuffer-handoff
```

先测`boot-16k-编号-fb-scanout-single.img`；own0/1仅故障对照，不要求重复已通过的预检。
仅由用户`fastboot boot`：进入UI后亮屏约60秒，简测三键/触摸/亮度及正常画面位置；
手动OFF20–30秒/ON后亮屏20–30秒，导出`sweetlog-编号-fb-scanout-single.bin`。
重点看quiescing→submitted→transfer observed→FB active、源地址A/crop22,48/stride4608、
owned resume verified及画面更新。不追加电池插拔、BUG-002、半小时或60Hz呈现专项。
若未进入active或画面冻结，保留持久日志；不把“能进桌面”单独当作接管通过。

## 5. 0121a12 云产物与测试镜像（已核对，待实机）

对应实现`f78abc8`。buildtree中52份源码与0182验证树逐字节一致，含本补丁8份变化；
114条原CI源码marker和继承/新增内核marker通过。MH2LM内核17526608bytes，SHA256为
`95ee2dec7b39c73d471d4430798b0568af047bce26bb2a15c86a63807c521c33`，归档manifest通过，
与0181不同。没有本次build-log或CI run ID，未审全量云告警，不提前给出实机结论。

已生成`out-bs/boot-16k-0121a12-fb-scanout-{baseline,preflight,single}.img`，各35667968bytes。
独立回读header v1/page2048、UEFI blob、FAT内kernel/ADT/TOML全部一致；三档仅own0/1/2
不同，原ADT及IRQ/ACM/runtime3/F2/F3参数保持。先测single，另两档只作故障对照。

| 档位 | boot SHA256 |
|---|---|
| `fb-scanout-baseline` | `4d407e175e74b56098e44c137715a5ad781606655166f4c3e14aef038ea4f45e` |
| `fb-scanout-preflight` | `3b3e65db5b9f90d409065797923b134ec3a99135ec89bde6320c3258a54e7cad` |
| `fb-scanout-single` | `9683dd4adb3dc90ced254128dc216b6c1c3eebf4ff445dbfbf5d45d289f2129d` |

```sh
fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a12-fb-scanout-single.img
```

按第4节亮屏约60秒及手动OFF/ON短测，导出`sweetlog-0121a12-fb-scanout-single.bin`；
若冻结或不亮，同样保留持久日志。不重复F3/半小时/电池专项。
产物归档SHA、52份源码比较和完整boot参数/回读证据保存在
`work1/scratch/artifact-0121a12/{verification.json,source-compare.json,boot-verification.json}`。
本轮未改驱动或CircleCI，无push或设备操作。

## 6. 0121a12 single 实测日志（接管链通过，画面反馈待补）

日志`work1/logs/sweetlog-0121a12-fb-scanout-single.bin`，SHA256：
`915f877f08464d5cfe069498e589adfaac949835f0ecc5dabc657b42edeed70c`。
有效364415bytes，flush69，未回卷，共4990行。371行boot-args确认own2/irq2/vsync1。
用户仅告知已导出日志，尚未反馈画面位置/持续更新/亮度等结果；已另行询问，不替用户填写。

### 6.1 本轮已取得的接管证据

| 解码行号 | 观察 | 含义 |
|---|---|---|
| 171–172 | A=`0xc3b30000`、B=`0xc4668000`，每个11225088bytes，seed167014us | 与0181相同几何/范围；启动seed耗时不作逐帧性能 |
| 881/987/1074 | 前三快照frame1/61/121，地址`0x9d200000`、stride4320、XY0 | 切换前仍扫描ABL |
| 1088–1089 | quiescing/accepted | 暂停TE/autorefresh后进入有界状态机，FB已刷新交接图像 |
| 1090 | submitted A、stride4608/crop22,48、idle-line2340、frame130 | 在空闲判据通过后提交自有池 |
| 1092–1093 | frame131、flush0、fresh PP0 DONE0x100，FB active A | 首次新传输观察通过，CPU呈现目标已切A；非仅寄存器读回 |
| 3821–3825 | 首次唤醒frame27980，地址A、stride0x1200、XY0x300016，owned resume verified | 手动OFF/ON后仍保留自有扫描配置，不翻回ABL |

四份SMMU/CB2记录一致：唯一SMR3/TRANS CB2、CBAR0x1f000、SCTLR0xe0/M0、
TTBR/TTBCR/MAIR0、global fault0。FSR仍为原始0x400，不命中LG `FSR_FAULT=0xc00001fe`，
未出现新已知fault位，也未清原值。FAR本轮为`0001fdfe:ba6e8cff`；在未确立故障有效性时
不将它解释成有效fault地址。前三份WARN来自预检对原始非零FSR的提示，不是接管失败；
own2的显式验证按本段既定允许条件执行。

这比0181增加了真实自有源提交/新传输及唤醒配置证据；画面内容/位置仍需用户确认。
没有逐帧CPU复制计数，不报告60Hz UI呈现或无撕裂；不授予A/B复用和释放权。

### 6.2 中断、开关屏及输入

- 8个完整IRQ窗口共480.283秒/28775帧，59.879–59.931Hz，加权59.913Hz；
  首次OFF前7窗，首次ON后1窗。所有统计窗dropped0；这是硬件IRQ频率。
- 6组实际DCS OFF/ON全rc0：OFF耗时1/14/2/9/4/11ms，ON10/4/8/10/6/6ms；
  每次OFF暂停帧/entry与ON重启值一致，首个ON帧均恰好加1。OFF区间没有新增MDSS
  统计/帧日志；首次OFF内4份电池完成样本跨度49.139秒，说明其它线程仍活动。
- 8份FB hardware-active快照：首次coalesced0，幂等ON时为8，后续6次ON均为8；
  是mailbox消费合并计数，不是丢中断/失败日志，不能补造最后一帧累计值。
  无显式软件回退、watchdog失败、HANDOFF FAILED或像素冻结错误。
- 32键事件配对：power13对、volume-up1对、volume-down2对；42触摸down/up事件。
  亮度记录包含2775→0→2775，多次恢复。输入/亮度日志健康不替代画面与手感确认。

### 6.3 其它观察与范围

- 无驱动`[ERROR]`、mux隔离/poison/SPMI timeout或kernel panic。6条WARN由3条原始
  FSR提示和3条既有USB端点命令警告组成；两次USB reset后均成功配置ACM，不关闭BUG-001。
- driverkitd在791–795行仍sig9/ns1/code7，与BUG-005一致；不描述为全日志无错误。
- 本轮出现10条`hfs_clonefile: cluster_write failed - 34`；XNU errno34为ERANGE。
  0121a11预检未出现，但更早0121a10预检有5条、0121a6 MDSS档有3条同签名，且本轮
  966/998行已发生在接管前。保留为既有文件克隆异常观察；未核实调用文件、影响和原因，
  不据时间邻近归因扫描地址切换，不扩入本次显示修复。
- 40电池样本/39refresh，healthy全1；全部gauge charging0，电流-132..0mA。启动有一次
  ENABLE，后续没有VERIFY(-12)或反复启停日志；采样健康不代表真实充电，BUG-004不关闭。

**结论与下一步：** 本轮日志确认受控单缓冲接管及正常OFF/ON链路通过；画面反馈待补，
暂不将F4第二段整体或PD1标为验收完成。若视觉无新增异常，无需补刷own0/1或重复F3矩阵，
下一工作为安全DMA停止/撤销与真实提交完成关联，再推进双缓冲和SwapWait；
保留无ABL首帧/console/panic、独立初始化等既定后续目标。提交池仍有意保留至重启。

底稿`work1/scratch/artifact-0121a12/{analyze-single.py,single-log-analysis.json,
single-analysis-summary.json,single-selected.txt,fb-scanout-single.txt}`。本轮仅分析和文档，
无驱动/参数/CI修改，无编译、push或设备操作。
