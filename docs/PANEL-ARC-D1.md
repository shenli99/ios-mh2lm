# D1：MMCX 与 MX/CX 父供电事务（0186）

2026-11-11。0186 在原 `SweetRPMh` IOService 中实现显示 ARC 投票链，继续使用同一个
apps-DRV writer、workloop、command gate 和 timer。没有新增重叠映射、第二个 writer 或全局
C 实例桥。**本地验证通过，未实机投票；D1 整体未完成。**

这段提供可执行的 typed 获取/降档/撤票接口，但当前 MDSS 生命周期尚未调用它。
没有新开启参数、默认投票或新 boot 档；`sweetdisplayresources=2` 仍只允许显式内核请求。
不为此单独要求云编译或上机。下一段合并 NoC 及实际链路资源调用。

## 结构和接口

```text
SweetMDSS : IOService（已有匹配引用；本段尚不自动申请 ARC）
 └─ SweetRPMhClient / retained SweetRPMh
     ├─ LDO：resourceInfo / requestVote / copyStatus
     ├─ ARC：displayPowerInfo / requestDisplayPower / copyDisplayPowerStatus
     ├─ 8 consumer：各自 LDO 票、显示 corner 票及 token 状态
     ├─ 显示聚合票 / 各供电已确认阶段账本
     └─ 同一 apps-DRV ACTIVE TCS 引擎
         ├─ gate：跨 LDO/ARC 一次一个在途请求
         ├─ timer：写读回、独立完成、清理、下一供电阶段
         └─ command DB：VRM 与 ARC 分类型解析
```

`SweetDisplayCorner` 为 Off、LowSVS、SVS、Nominal、Turbo。调用者不传寄存器地址、
毫伏或硬件档位索引。`displayPowerInfo()` 按完整父依赖报告各 corner 是否可表示；
查询仅解析映射中的 DB，不写硬件。ARC 表缺失/内容错误显式拒绝新请求，不回退到旧 helper。
只缺可选 ARC entry 不影响原四路 LDO 绑定；DB 总体几何损坏仍按原规则拒绝服务启动。

状态包含合并请求 corner、目标 levels、Pending/Complete/Failed、result、uncertain、token，
以及本请求已经 ACK 并清理完成的供电位图 acknowledged；位置为 CX0、MX1、MMCX2。
appliedLevels/known 则单独记录最近已确认的软件投票及其有效位，首次接管前为未知。
目标不等于已经到达：父供电 guard ACK 后，其 appliedLevels 仍可高于最终目标。
失败时结合已确认值及 uncertain 解读；这些值都不是物理电压测量。
token 不回绕，旧 token/错误 consumer 查询拒绝；新增 client/token 耗尽检查也覆盖旧 LDO API。

## LG 依据与档位推导

参考 `android_kernel_lge_sm8150` 提交 `d4f335e13d73c4aa2af85581a6ec11b670ee5b94`：

| 依据 | 已核对的内容 |
|---|---|
| `arch/arm64/boot/dts/qcom/sm8150-regulator.dtsi:527` | MX 为 PM8150L S4；MMCX 的 MX supply 下限 retention；其父为独立 CX display-supply |
| 同文件 `:579`、`:670` | MMCX 为 PM8150L S5，资源 `mmcx.lvl`，最低 LowSVS；CX display-supply 最低 MinSVS |
| `drivers/regulator/rpmh-regulator.c:1244` | ARC aux 是最多16项的 LE u16 表；索引是发给 ARC 的值；尾部可有零填充 |
| 同文件 `:1555`、`drivers/regulator/core.c:3039` | MMCX 的 min-dropout=-1 参与父 supply 的 level 选择，先升父再改子，下降反序 |
| `include/dt-bindings/regulator/qcom,rpmh-regulator.h` | 原始语义 level：retention16、MinSVS48、LowSVS64、SVS128、Nominal256、Turbo384；Linux regulator ABI 另加1 |

推导顺序与执行顺序不同：先选 MMCX，再推父依赖，最后按供电顺序发送。

1. MMCX 表向上选择满足64/128/256/384下限的 level，得到硬件 selector。
2. MX 表向上选择 `max(16, selectedMMCXLevel - 1)`。这里用实际选中的 MMCX level，
   不能用调用者的未舍入下限；减1来自 LG 的 level dropout，并非降低一个硬件 selector。
3. CX display-supply 的下限为48。MX display-supply 未设置 dropout，且 ARC 有 get-voltage-sel，
   LG core 不会把 MX level 继续按等值传播给这个 CX supply。不能凭三路有父子关系就全投同档。

三路表均要求 ARC slave3、地址 accelerator 类型3、word 对齐、资源名和地址唯一、aux 偶数
且4..32 bytes、完整映射范围内。索引0必须是真 OFF，后续有效项严格上升，零填充后不再允许
非零项。没有使用硬编码资源地址或硬件 selector。aux 按 LG 的 u16 通用表解析；LG ARC 路径
未按 aux version 分支，本段也不把未经实机确认的 version 常数作为假定。

## 事务、聚合和失败边界

每个 consumer 有独立 corner 票，聚合取最高请求，再从三路 DB 表推导依赖。
正常已拥有状态上调按 CX→MX→MMCX，下调按 MMCX→MX→CX；未变化的阶段不发命令。
**首次接管不能假定固件的子供电已经处于低档：** 未知 CX/MX 先分别投各自 DB 的最高有效
selector，以免 child 仍在高档时先降低 parent。MMCX 目标 ACK 后，再将 MX、CX 反序降到
推导的需求。最多五阶段；不是硬编码最高电压、重复重试或在后台永久维持高档。
完成后后续请求使用已有账本，不重复 guard；全部 owned 但 Off 的再次获取也不需要 guard。
每阶段独立单命令 ACTIVE ACK，完成清理后才启动下一阶段，不能依赖一个批次中父子命令
已排好顺序就假设物理完成有序。固定五槽 stage 数组，不引入无界队列。

选槽时检查两个 ACTIVE 槽中整个三路依赖的继承 payload；包括 idle 但仍 armed 的请求。
每阶段准入，以及 timer 恢复、屏蔽自己的 IRQ 后首个 payload 写之前再次检查。
不覆盖其他资源 armed 的槽；可使用另一个真正空闲槽，其他槽 IRQ/status 位保留。

沿用原独立完成证据：issued+complete、idle，且 fresh IRQ 或已观察到执行状态变化。
旧命令状态单独不能完成下一阶段；先清 trigger/mode/enable/wait、W1C 本槽、恢复本槽 IRQ 位。
写读回最长1ms，单阶段100ms，整个链500ms，timer 推进，无 gate 内长忙等。
LDO/ARC 在整链期间互相返回 Busy，不能在父子阶段间插入另一消费者请求。

每阶段完成后更新该供电的已确认账本；只有整链成功才提交 consumer 票与聚合票。
任何可能有硬件影响的失败都保留已确认阶段、记录 uncertain 并关闭后续写入，不自动撤父、
回滚、reset 或重试。尤其 child 状态未知时，撤 parent 可能导致正在运行的显示掉电。
consumer 有有效显示票、在途请求或不确定显示请求时 detach 返回 Busy；stop/free 不隐式关电。
有写实例退出后，沿用原 boot 内 retired 所有权保护；晚 timer 不再碰 MMIO。

空账本的 Off 不下发关继承供电命令。获得过的软件票最后撤回时，调用者必须已经停 DMA/link、
关闭依赖域；任何降档前先降低依赖时钟频率。ACK 仅证明本 XNU ACTIVE 请求完成，既不证明
实测电压/物理掉电，也不覆盖 AOP、其他执行环境或 sleep/wake 投票。

## NoC 边界与后续

已核对 LG `sm8150.dtsi`、`sm8150-bus.dtsi`、`sm8150-sde.dtsi` 和
`drivers/soc/qcom/msm_bus/msm_bus_{fabric,arb}_rpmh.c`：

- apps RSC 与 display RSC 是不同的 DRV；后者窗口 `0xaf20000`、TCS `0xaf21c00`。
  `_display` BCM 节点使用 `rsc_disp`，不能沿用本段 apps-DRV 地址直接发它们的票。
- BCM aux 为 unit-size/width/VCD；命令有14-bit X/Y、VALID、COMMIT，按 VCD 的完成边界组织。
  ACV 还有特殊 RSC 位语义，不能套普通带宽公式。
- SDE 有 reg bus 及 MNOC/LLCC/EBI 路径；完整带宽依赖不能只投一个 MM0。
- LG `sde_rscc` 自身还声明 `mdss_core_gdsc` 和 RSCC/non-GDSC AHB/VSYNC 时钟依赖；
  后续须核对 TCS 与 wrapper 各自访问准入及冷启动先后，不能未经确认就访问未供电的 RSC。

本段未映射/改写 display RSC，也没有 BCM 编码、NoC lease、冷 GDSC 控制或 PHY/PLL/host
初始化。0185 时钟模块仍 `inheritedUpstream=true`，MDSS 原有绑定仍不发票；不能把新 API
或温态运行位当作上游已获取的证据。下一段实现 display RSC 的所有权/完成及 typed 带宽路由，
再与频率、GDSC、链路初始化一起接入 MDSS 生命周期，合并有意义的测试镜像。

## 本地验证

补丁 `xnu/patches/0186-mh2lm-display-arc-votes.patch` 只改变 RPMh 的 `.h/.cpp`；
严格应用到0185验证树，无 offset/fuzz，两个源码字节比对一致，114条原 CI marker 保留。
真实 XNU/KDK 头的 RPMh 基线/最终及 MDSS 最终 arm64 对象编译通过；RPMh 告警1228→1298，
新增70条均为固定数组访问提示，逐项核对了枚举、索引和阶段边界，不声称零告警。

扩展实际源码 ASan/UBSan RPMh 测试，覆盖：可选元数据/只读模式、语义 level 与 selector 区分、
父供电舍入、首次最高档 guard 及回落、逐阶段独立完成、共享票/升降/反序撤票、LDO/ARC 并发、
继承冲突与 timer 前新冲突、五阶段各处失败及其已确认值/清理失败/posted timeout/stop 晚事件/
retired、11类坏 ARC 数据和 ID/token 耗尽。corner 使用固定 unsigned underlying type，
非法 enum 输入可定义地拒绝；UBSan 设置 halt-on-error，不能把输出 PASS 当成无 sanitizer 错误。
原显示、时钟、framebuffer、DSI、perf 和 ADT 回归通过。六套 C++ 测试使用 ASan/UBSan，
ADT 是 Python；LSan 受环境 ptrace 限制关闭，另断言对象/映射/分配/通知归零。

底稿 `scratch/framebuffer-0186/`：验证树、严格重放、对象编译、告警与测试记录。
无新实机或云构建证据，未改变 CircleCI、USB、按键、触摸、电池或 mux。
