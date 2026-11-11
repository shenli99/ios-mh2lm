# DSI0 host 重建（0190，PD2 / D1 后续）

**0191后续更新：** 已将 PHY v4.0 / PLL 7nm-v2 与 BYTE/PCLK roots 的真实重建接到
managed 链的 host 之前，沿原 owner/gate/DSI timer；独立确认停稳、失锁/新锁及更新/运行。
本地12套检查和arm64对象编译通过，未实机；面板生命周期/整屏冷启动仍未完成。
默认关闭，详见[link实现与边界](PANEL-LINK-D1.md)，下文原阶段记录保留。

2026-11-11，接0189。`SweetMDSSHost.inc` 已接实际 managed 扫描接管：软复位 host，
从明确的 command-mode 配置重建寄存器，再继续原自有 A 提交及新帧完成检查。
**本地编译和测试通过，未上机；PHY/PLL 仍继承运行状态，完整面板驱动未完成。**
这段不单独要求云编译或刷机，继续合并可运行的完整驱动批次；完整驱动优先，之后零拷贝。

## 所有权与接入位置

```text
AppleCLCD / SweetFBScanout
 └─ typed beginScanout / scanoutStatus
     └─ SweetMDSS : IOService（唯一 MMIO owner / 原 command gate）
         ├─ managed 协调器 / 原 scanout timer
         │   ├─ SweetRPMh typed LDO / ARC
         │   ├─ core 模块 / 原 clock timer：CN0、GDSC、核心PLL、AHB/MDP
         │   ├─ NoC 模块 / 原 NoC timer：带宽票
         │   ├─ clock 模块：独立 DSI0 lease
         │   └─ host 模块 / 原 DSI timer：host reset / common / command / stream
         ├─ DSI command executor（与 host 重建互斥）
         └─ 原 IRQ / 扫描提交 / DMA lifetime pin / fresh completion
```

无新增 IOService 空壳、全局实例桥、映射、线程或第二 writer。内部方法
`beginHost()` / `advanceHost()` 由原 gate 调用；上层不传寄存器地址。
`fClockTouched` 沿用已有 writer 退休账本，首次 host 写之前置位；重建失败或 stop 后，
不能让新实例丢失已发生的硬件影响。timer 退出后不再访问 MMIO。

显式 `sweetdsihost=1` 要求 `sweetdisplaymanaged=1`、`sweetdisplaycore=2` 及它们原有的
own2/power1/IRQ2/resource2/clock1/NoC ABI 前提。默认0保留0189路径；不支持的参数或依赖
缺失明确拒绝启动。startup/matched 只建立资源/配置选择，不复位 host。
`SweetDSIHostProfile=1` 表示所选公共链路配置，**不是已识别本机 DDIC 版本**。

实际执行点是 NoC 完成、独立 DSI0 lease Held 之后，扫描 IRQ 重开、A pin 和源地址写入之前。
原 managed 协调器等待 `ManagedHost`；host 成功后再核实保留的 pipeline、资源和 idle，
随后走原 fresh PP DONE、flush=0、RD_PTR 新帧检查。HostReady 只代表配置读回及 idle，
不能代替像素完成，更不能授权释放扫描内存。

## 配置来源与写入范围

LG 主源 `android_kernel_lge_sm8150` HEAD：
`d4f335e13d73c4aa2af85581a6ec11b670ee5b94`。

- `drivers/gpu/drm/msm/dsi-staging/dsi_ctrl_hw_cmn.c`：host setup、soft reset、
  command engine、stream、controller enable 和 mixed interrupt 寄存器语义。
- `dsi_ctrl_hw_2_0.c`、`dsi_display.c`：v2 lane map 为 one-hot，identity 是 `0x8421`，
  不是数字排列 `0x3210`；`dsi_catalog.c` 为 v2.3 选择该实现。
- `dsi_ctrl_reg.h`：寄存器 offset。全部访问沿现有 DSI0 `0x400` 窗口，最大 `+0x330`。
- `settings_panel/dsi-panel-r66456-{pp1,dv1,dv2,pv}-tianmamd-fhd-cmd.dtsi`：
  四版本共用 RGB888、1080×2340、VC0、4lane、无 DSC、T_CLK_POST=0x18/PRE=0x1b、
  TX EOT、MDP DCS start/continue=2c/3c。未选择任何版本的厂商 ON/OFF 表。

已有实机 `sweetlog-0121a12/0121a13` 记录 HW_VERSION `+0=0x20030000`。
写之前再次核对实际版本、controller/cmd 模式、无 TPG、ERROR/ACK_ERR 清零；这次没有
新增版本读数或上机结果。继承 pipeline 的扫描 idle 和 IRQ 暂停必须另行确认。

目标值包括 CTRL=1f5、DMA=14000000、MDP_CTRL=8、MDP_CTRL2=10000、DCS=13c2c、
stream CTRL=0ca90039 / TOTAL=09240438、HS_TIMER=3fd08、CLKOUT=181b、EOT=1。
禁用 DSC/split/CPHY/TPG/强制 lane 状态，DMA 地址及长度初始为0。主机配置是明确的目标表，
不保存/回放 ABL host 寄存器快照。

一个需要实机核对的差异：LG `te-pin-select=1` 对应 TRIG `0x80000004`，已有 ABL 快照为4。
新增 bit31 选择 external TE；MDP trigger 仍为 NONE，DMA trigger 仍为 SW。
MDP_CTRL 也按 LG 默认 interleave=0 写8，而已有 ABL 为100008（interleave=1）。
这不是“完全复制 ABL 配置”，也不是已验证这些差异无影响。

## 状态与失败边界

1. 完成资源票并确认 IRQ Idle、TE disconnected、autorefresh=0、DSI STATUS[2:1]=0。
   不额外要求 FIFO_STATUS 的所有 lane-empty 位全1：实机已有 `+c=0x1310`，
   LG 的 lane idle 实现也不是该全等条件。
2. INT_CTRL 写0只关闭使能，不 ACK W1C 状态。核对 enable mask `0xa222aa02` 清零；
   不对整个 mixed 寄存器做普通配置等值读回，不顺带 ACK ERROR。
3. 关 CTRL 低3位，核对 engine disable；CLK_CTRL=200b3f 强制必要动态时钟并读回；
   写 host SOFT_RESET `+0x118=1`。以 timer 等待至少1µs，无忙等。
4. 写 SOFT_RESET=0，核实版本、reset 清除、idle、ERROR/ACK_ERR，再逐项写公共配置。
   控制器在 forced clocks 下开启，恢复 CLK_CTRL=23f，再开启 command engine。
5. 下一次 timer 独立核对全部目标、使能关闭、ERROR/ACK_ERR 和 idle；整段预算20ms。
   成功后由 managed 完整复核并继续新扫描，不能提前 pin 或提交 A。

只复位 host；不写 PHY_SW_RESET `+0x12c`、PHY/PLL 或面板 reset，不发送 vendor 命令。
因此本段既不是 PHY/PLL 初始化，也不是冷链路重建；没有证明 ABL 已退出全部依赖。

重建期间 DCS、亮度、面板电源请求不准入；DSI executor 与 host 共用 timer，不能同时执行。
clock/NoC 修改请求返回 Busy，查询现有 token 仍可用；扫描快照和 DSI probe 避开过渡/故障态。
每步重新核实 typed ARC/NoC/DSI0 lease、core/local clocks、IRQ idle 及 TE/auto 条件。

读回失败、超时、资源/owner 丢失均明确失败：保留票和部分 host 状态，停止 timer，关闭
继续写入的入口；不自动重试/reset、不恢复 ABL host/TE，不推断可以撤电。若已写 host，
置 `fDsiPoisoned`；stop 在 reset 中也不会偷偷 release reset 或撤资源。
普通 DCS OFF 不重建 host、不撤票；完整 unprepare 和资源反向释放仍待后续实现。

## 本地验证与后续

- 严格应用0189树，无 offset/fuzz，7份变化源码字节一致；原114个 CI source marker 保留。
- 真实 XNU/KDK arm64 MDSS 基线和最终对象均通过；warning 1413→1417，新增4处仅为
  固定表的有界数组访问提示：clock use 常量索引、26项 host 配置表遍历，逐处已审，无抑制。
- 11套通过：新增实际源码 `test-mh2lm-dsi-host.py` 加原10套；10套 ASan/UBSan，
  另1套 Python ADT。UBSan halt_on_error，无 sanitizer 报告。
  LSan 因环境 ptrace 限制关闭，fixtures 另断言对象/映射/通知/分配归零。
- host 专项执行真实资源/扫描链；模拟 reset 销毁旧配置，证明必须重新构造。
  覆盖 >=1µs、stale W1C 不误清、配置逐项丢写、错误版本/TPG/error/模式拒绝、busy 准入、
  reset 退出失败、资源丢失、超时/provider 退出/各阶段 stop、晚 timer 无访问；
  有独立 fresh 像素完成、无 PHY/PLL/vendor 写入及最终对象归零检查。
- 底稿 `scratch/framebuffer-0190/{base,edited,verify,prepare-patch.py,compile.py,verification.json,
  native-results.json,warning-counts.json,array-bound-review.json,reference-audit.json,tests.json}`。

下一步：PHY v4.0 / 7nm PLL 的版本准入及真实配置、DSI 输出时钟，再接已确认面板
身份/reset/完整 ON/OFF、反向释放、状态丢失后的独立重建及 ABL 退出。NoC 冷 solver/QoS/
端口/sleepwake 也未完成。A 单缓冲、B 未提交、30Hz CPU 拷贝不变；未宣称修复旧卡顿。
本轮未 push、未触发 CI、无新 boot/硬件验收；CircleCI 内容保持原 blob，USB/按键/触摸/
电池/mux 未改。无需重测旧 BUG-002 或其他已知充电/USB 问题。
