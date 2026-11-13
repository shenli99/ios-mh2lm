# 0218：按 Lineage 区分配置写入与硬件完成

2026-11-13。用户要求继续核对 qtimer 后面的路径，把我们额外增加的配置回读门槛改成打印。
参考仓为 `lge-lineage-kernel@2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`。
补丁为 `xnu/patches/0218-mh2lm-lineage-config-observation.patch`，在 0217 之后应用。

a28 的实际首错仍是 wrapper `+0x2020` 写 `ffffffff`、读 `224cac6`；原因未知。
0217 只处理六个 CNTP 寄存器；0218 将同一规则扩展到下面已审计的后续配置路径。
这些修改尚无新内核实机证据，不能宣布屏幕已经恢复。

## 源码依据与修改

Lineage `drivers/gpu/drm/msm/sde_io_util.c` 的 `dss_reg_w()` 写寄存器，调试时可以读打印，
不把相等作为成功条件。具体同步操作须另看调用点，不能据此删除所有等待。

| 阶段 | Lineage 对应源码/行为 | 0218 行为 |
|---|---|---|
| RSC 初始化 | `sde_rsc_hw.c` 的 qtimer/wrapper/seq-v2/solver/PDC 五个 helper 使用普通写，`rsc_hw_init()` 最后 `wmb()` | 全部 65 项按原值、原顺序写入，逐项打印；取消其余 59 项额外回显等待 |
| CLK/TCS 交接 | `rsc_hw_tcs_wait()` 关注 SEQ_BUSY，忙时交还 USE_OK、延时、重新请求；不要求 USE_OK bit0 回读 0 | override/USE_OK 配置回显只打印；真实 SEQ_BUSY、期限继续约束交接 |
| ACTIVE 槽准备/提交 | `qcom-rpmh-mailbox.c` 的 `write_tcs_reg()` 普通写；`write_tcs_reg_sync()` 用于清 TRIGGER、清 AMC、置 AMC | payload、WAIT、ENABLE、IRQ 配置和清理只打印；三个 CONTROL 写继续等待相等。CONTROL 按读值 RMW，保留无关位 |
| ACTIVE 完成 | mailbox IRQ handler 要求本槽 IRQ、各命令 ISSUED 和必要的 COMPLETE；不另要求 STATUS idle | STATUS idle 改成打印；仍须本次真实 INTID161 回调及本槽/命令完成。其他同时发生的 IRQ 位不单独否决本槽 |
| ACTIVE 复用 | `tcs_is_free()` 看真实槽状态和软件 in-use；不比较旧 CONTROL/WAIT 固定模板 | 取消旧 CONTROL/WAIT 模板门槛；自己的已完成槽不要求清理 ENABLE 写回显，仍检查实际可用状态及外来冲突 |
| 核心 PLL/RCG/branch | `clk-alpha-pll.c`、`clk-rcg2.c`、`clk-branch.c` 配置写与 LOCK、UPDATE、CLK_OFF 等等待分开 | PLL 参数/RCG CFG/自有 branch enable 回显（包括普通和 voted 分支的写入记录）、父时钟原始快照差异打印；保留 LOCK/UPDATE/HALT 和真实上游票据。voted release 保留 10us settle，不等 enable 回显 |
| PHY/DSI PLL | `dsi_phy_hw_v4_0.c` 配置普通写，等待 REFGEN/实际 lane idle；`mdss-dsi-pll-7nm.c` 启动等 lock，`dsi_pll_disable()` 普通写关闭、不等 unlock | PHY/PLL 参数、reset、divider、misc、配置表读回打印；取消额外 unlock 和停止时 ROOT_OFF 快照门槛。实际 branch halt、UPDATE 排他、REFGEN 和启动 lock 仍保留 |
| DSI host | `dsi_ctrl_hw_cmn.c` 等配置/软复位/force-clock 普通写，依赖实际引擎状态及传输完成 | host 配置表、reset、IRQ mask、控制值、force-clock 回读打印；版本、真实引擎 busy、错误和资源状态仍检查 |
| 自有 scanout 与生命周期 | SDE 配置写不以逐项回显作为像素传输完成 | 自有 source/stride/crop、恢复 TE/autorefresh 及休眠配置快照差异打印；原硬件 lifetime pin、flush 消退、新 PP_DONE 和帧推进继续必需 |

本轮改变配置观察规则，不代表完整 XNU 驱动已经与 Lineage 等价。continuous splash、完整 RSC
CMD/VID/IDLE/VSYNC/sleep-wake 状态机、独立 MDP/SMMU 初始化仍有差异。

## 保留的边界

ABI、映射范围、硬件版本、provider 有效性、软件所有权/引用、资源票据、供电及可访问条件继续检查。
继承路径中，未知时钟源/不支持的频率或 CPHY/别名消费者，以及固定 geometry/SMMU 合同仍拒绝：
XNU 尚不能独立重建这些条件，不能凭一条配置日志建立支持。
已持有资源后的父时钟原始快照全值差异则仅诊断。运行路径仍要求父时钟可用和实际 branch 运行。
无资源权限时不为打印补读 MMIO；stop、失权和迟到事件的保护继续保留。
配置日志不补造 ACK，不证明目标频率/带宽/电压或 source 地址已实际生效。

## 日志与下一轮

统一格式示例：

```text
SWEET MDSS CONFIG: stage=rsc-wrapper-init offset=0x2020 want=0xffffffff got=0x224cac6 mask=0xffffffff mismatch=1; diagnostic only
```

初始化和主要写入打印值；复核时主要打印差异。父时钟日志给出 branch/field/want/got。
每个实例最多 1024 条配置观察；匹配记录最多 768 条，给后续差异留空间。
已有样本的路径直接使用缓存值，打印不增加第二次寄存器采样。

用户 push 最新 main，云编译后下载新的 kernel/buildtree（建议 `0121a29`）。
agent 核对包含 0218 的源码和 `SWEET MDSS CONFIG:` 内核标记，再沿原 ABI608 固件、ADT 和
605 字节 retained 参数打包。打包脚本会拒绝缺少新标记的旧内核。
先测试 retained；亮度/OFFON/长测/combined/supply 的实机验证继续等完整接管之后。

日志按 `CLOCK Held → enter CLK → 65 项配置观察 → TCS ownership → INTID161 → fresh ACTIVE →
NoC ACK → PHY/host → 新 DMA/transfer/HANDOFF` 检查。配置差异先记录；若真实完成等待失败，
同时查看该阶段之前的打印值，不再被额外配置回显条件提前截断。

## 本地验证

- 16 套既有时钟/资源/NoC/managed/面板生命周期回归，加 DSI link、host、完成证据、qtimer
  和新增配置专项，共 21 套 ASan/UBSan 测试；完成专项 36 场景、qtimer 16 场景。
- 新专项独立注入全部 65 项初始化非回显、5 类 payload/WAIT/ENABLE、2 类清理非回显，
  并验证三次 CONTROL 同步写失败及上游失权；每次配置通过后仍须真实 IRQ，加上 IRQ_ENABLE 非回显/同时外来源场景，合计 78 场景。
- 动态执行 Lineage 实际五个初始化函数，65 项值和顺序一致。
- ARM64 `SweetMDSS.cpp` 对象编译通过；告警 1573→1585，净增 12 条均属已有的
  `-Wunsafe-buffer-usage` 类别，来自有固定边界的诊断数组/表索引，没有新增告警类别。
- 0185–0218 共 34 个补丁严格无 offset/fuzz 重放；78 文件字节一致，原 114 个 CI 源码标记保留。
- matrix 脚本语法检查通过；实际旧 a28 内核因缺 `SWEET MDSS CONFIG:` 被拒绝，无新 boot 输出。

本地底稿：`/home/shenli/work1/scratch/framebuffer-0218/`。未 push、触发 CI 或操作设备。
