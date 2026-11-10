# F4 第二段：受控单缓冲接管（0182，待云构建及实机）

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
