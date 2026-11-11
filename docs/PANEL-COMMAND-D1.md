# D1 首段：完整面板命令传输基础（0183）

**后续0184已完成资源ABI、真实RPMh provider/typed投票及MDSS绑定，见[资源层记录](PANEL-POWER-D1.md)。**
0185已补MDSS内部本地时钟lease及温态GDSC准入，见[时钟记录](PANEL-CLOCK-D1.md)；
开启后DSI/旧DCS也检查该lease。冷态clock/GDSC、MMCX/NoC及链路依赖仍缺。

2026-11-10。按用户“优先完整驱动，随后零拷贝”的要求，停止把 CPU 拷贝双缓冲当作
PD2/PD3 前置。0183 先实现可复用的命令基础，并合并 0121a12 滑动性能回归的计时。
**这是 D1 的一部分，本地验证通过；尚未运行于设备，不是完整面板初始化或卡顿修复。**
资源 ABI、统一 RPMh writer/lease、clock/GDSC、PHY/PLL 和实际 panel profile 仍未完成。
这一段不单独请求云编译/刷机；继续补资源和链路，按可运行的批次一起交付。

## 架构与请求语义

```text
已确认 profile 的面板生命周期模块（后续接入，不在 matched 回调初始化）
 └─ SweetDisplayBackend 的 beginDSISequence / dsiSequenceStatus / cancelDSISequence
     └─ SweetMDSS（现有 IOService / workloop / command gate）
         ├─ SweetDSICommand.h：有界消息校验、7-byte 表解析、embedded packet 编码
         ├─ SweetMDSSDSI.inc：timer 推进 idle / transfer / delay / terminal
         └─ 私有命令 IOBufferMemoryDescriptor（与像素扫描池无关）
```

请求在入口复制为 OSData，整表校验通过才进入硬件 admission。返回 token 只代表接收，
完成、失败、取消、完成包数及原始状态由 typed status 返回；查询不读 MMIO。旧 token
不能取消后续请求。没有新增用户态 selector、全局单例、consumer MMIO 或反向同步回调。

目前支持本面板需要的 VC0、LP、无 ACK 的 DCS 05/15/39 写；拒绝 read/BTA、其他 VC、
非 final 的分组、非法长度/类型。每条 payload 最大252 bytes，表最多64包/16KiB；
候选 PV ON 的17包、694 bytes、最大149-byte D3，以及 OFF 的4包均可完整解析/编码。
**候选厂商表仅用于离线测试 fixture；生产代码没有选定或自动发送这个未知版本的表。**

`sweetdsi=1`允许内部请求，默认关闭；开启本身不发命令。发完整表前，生命周期模块必须
显式暂停帧/IRQ消费；执行器在 RD_PTR IRQ 仍活跃时返回 Busy。OFF 的90+150ms延时加传输
可能超过现有250ms帧看门狗，不应屏蔽看门狗或伪造帧完成来掩盖暂停。恢复 IRQ 也由
后续生命周期模块在 pipeline 可用后进行，不从不透明 DCS 字节推断最终面板电源状态。
已有亮度与 F2 的原始短命令路径保留，命令序列运行时返回 Busy，状态不确定时拒绝写入。

## 长包与硬件边界

LG `dsi_ctrl.h`分别定义 FIFO store 64 bytes、memory embedded DMA 256 bytes。
编码后不超过64的命令用原 FIFO 机制；较长命令通过内存 embedded DMA 整包发送，
149-byte payload 编码后156 bytes，不拆包，也不需要 non-embedded→embedded 的 soft reset。
超过当前 embedded 上限直接拒绝，不能照用别处“256-byte FIFO”的注释扩大 store 上限。

本轮复核既有0121a12日志，DSI0 HW_VERSION 已实际读到 `0x20030000`（191/402/603行）；
对应的 PHY/PLL live 版本仍未确认。执行前检查当前版本、时钟、命令模式、FIFO/错误状态。
LG DSI命令内存使用 `MSM_SMMU_DOMAIN_UNSECURE`，与 SDE 共享 unsecure address space；
DTS的 `0x801` 是 **secure domain**，不能猜成 DSI 的独立 SID。
新路径检查当前已知 unsecure route/CB2 的完整无转换条件及故障状态；不修改页表/路由。
这些条件和扫描缓冲实测不代替新命令 DMA 的实际传输证明，首次实机仍须验证 fresh DONE。

命令缓冲为连续低32位、WC、已 prepare 的4KiB descriptor，校验实际 segment、对齐、
长度、32位范围及不覆盖 ABL 保留区。首次可能生效的地址写前取得独立引用；写屏障后触发。
先清并确认旧 DMA_DONE 已消失；之后必须同时看到新的 DMA_DONE 和 DMA/MDP idle，
才释放硬件引用并允许复用。保存/恢复借用的 command DMA/address/length/FIFO/trigger配置。
等待 idle、完成及表内90/150ms延时均用 timer，gate 内仅保留原 FIFO reset 的1µs脉冲。

超时/取消中的 DMA、错误状态、时钟丢失或部分已执行的表都明确进入不确定状态，关闭
后续 DCS admission、保留 TE/auto 停止，不以复位或恢复 ABL 冒充成功。若 DMA 可能仍读
命令内存，其独立 prepared 引用跨服务 stop/free 保留到重启；已确认 idle 的缓冲正常回收。
这不是最终 DMA abort/recovery 实现；后续完整重建需补恢复协议，不能先移除此保护。
stop/free 撤销并移除 timer，晚到 action 不访问已拆除映射，部分 start/分配失败可回收。

## 合并的滑动性能证据

`sweetfbperf=1`在 serialized framebuffer service 模式启用；默认关闭，汇总沿用INFO等级。固定直方图、
每15秒低频汇总，无逐帧日志、排序或额外 timer，不改变30Hz策略、屏障、复制和提交规则。

| 指标 | 实际测量含义 |
|---|---|
| copy+barrier / copy-bytes | 每层实际复制与必要写屏障的耗时、总字节 |
| cpu-present / cpu-presents | 实际有像素复制的 CPU present 耗时/次数 |
| provider-query | scanoutStatus 跨 provider gate 查询的耗时 |
| pending-first / pending-latest | flush 取走记录时，最早未处理请求/最新合并请求的年龄；含期间等待 |
| coalesced | 有 dirty 记录时又收到新 swap 的次数 |
| cpu-interval | 两次有复制的 CPU present 完成间隔，含无新 swap 的空闲间隔；OFF 间隔排除 |

直方图 p50/p99 是微秒桶上界，另有最大值/总耗时/超预算数；超预算不等于丢帧数。
这些不是硬件 latch、释放 fence 或 UI 的实际呈现帧率；约59.91Hz IRQ仍不能当作体验验收。
当前未取得这些新增计时的实机数据，卡顿根因未定，也没有把未接入的双缓冲归为原因。

## 本地验证与交接

- 0183 对 verified0182 严格 apply，无 offset/fuzz；应用后9份变化源码逐字节相同，原CI的
  114条源码 marker 均保留；CircleCI blob仍为 `da0b6c0947a66f03491b3a9023590b49665292df`。
- 基线和最终重放树的 MDSS/FB 均以真实 XNU/KDK 头文件编译 arm64-apple-darwin 对象成功。
  相比基线，MDSS新增16条、FB新增19条有界数组/指针诊断已核对；无新编译错误。
- 实际编码器/执行器的 ASan/UBSan：完整候选ON/OFF、149-byte整包、padding、等待和冻结TE、
  不可变请求、独立DONE/idle、staleDONE、20类admission/资源失败、token隔离、取消/超时/
  error/clock-loss/stop/直接free及DMA引用保留通过；部分表取消不伪装已恢复。
- 实际 FB 计时的 ASan/UBSan：字节/耗时、合并与两种 pending 年龄、CPU间隔、OFF排除、
  桶上界/清窗及关闭计时时不增加计数/时钟读取通过。原显示服务/F2/F3/F4回归通过。
  LSan仍受本环境ptrace限制关闭，另有对象计数和prepare/complete/pin断言，不宣称硬件验证。

补丁：`xnu/patches/0183-mh2lm-dsi-command-sequencer.patch`。
底稿：`scratch/framebuffer-0183/{verification.json,warning-counts.json,test-*.txt,*compile.txt}`；
未push、触发CI或操作设备。下一实现直接补 D1 剩余资源ABI/统一RPMh lease/clock-GDSC，
随后接 PD2 link 与 PD3 生命周期；不要回到拷贝双缓冲前置或单项探针云编译。

源码依据为LG基线 `d4f335e13d73c4aa2af85581a6ec11b670ee5b94` 的
`drivers/gpu/drm/msm/dsi-staging/{dsi_ctrl.c,dsi_ctrl.h,dsi_ctrl_hw_cmn.c,dsi_catalog.c}`、
`sm8150-sde.dtsi`与候选PV DTS。参见[完整计划](PANEL-FULL-DRIVER-PLAN.md#7-加速后的交付批次与性能回归)。
