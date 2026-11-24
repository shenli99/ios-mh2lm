# C0-03 结果：首批安全试验样本

[返回 C0](00-baseline.md)。日期：2026-11-22。等级：**调查完成（资料）**；样本的上电原状态尚无实机读数，由 C2-01 的只读快照补齐后才允许进入写卡。

来源：LG `drivers/clk/qcom/gcc-sm8150.c`、`debugcc-sm8150.c`、`clk-debug.c`；b32 附带 LG DTB（[C0-05](C0-05-SCOPE.md)）；所有权表 [C0-02](C0-02-OWNERSHIP.md)。

## S1：debugcc 测量链（C2-08）

| 项 | 内容 |
|---|---|
| 资源 | GCC debug mux：`DEBUG_CTL`(sel) 0x62000 [9:0]、post-div 0x62004 [3:0]、`GCC_DEBUG_CBCR` 0x62008 bit0；计数 ctl 0x62038／status 0x6203c；XO/4 CBCR 0x43008 bit0 |
| 当前 owner | 无（C0-02：全树未引用）；DTB 中 `qcom,cc-debug` 只供 Linux debugfs |
| 父源／电源 | CXO（RPMh CXO 由 ABL／固件长期持有）；GCC 本身常供电 |
| 允许动作 | 保存原值 → 写 sel/post-div → 开 DEBUG_CBCR 与 XO/4 → 短测(0x1000 tick)＋长测(0x10000 tick) → 恢复全部原值 |
| 完成观测 | status bit25 DONE，计数 [24:0]；短测＝长测 ⇒ NotRunning；频率 = (cnt·10+15)·4.8 MHz ／ (0x10000·10+35) |
| 停止／撤回 | DONE 等待有界（参考约 15 ms，给 50 ms 上限），超时则停止计数、恢复原值并报告 Uncertain |
| 结论 | **可实施**，前提：GCC 窗口需扩展到覆盖 0x33000、0x43000、0x62000 三页。0125 起 ADT 已映射四页（物理地址 `0x0010b000/0x00133000/0x00143000/0x00162000`，见 [02-gcc](02-gcc.md) 的地址模型）；C2-08 只需在同一节点上新增写路径 |

测量参考样本（只测量，不控制）：

- **运行样本**：`gcc_disp_xo_clk`（CBCR 0xb048，mux sel 0x53）。LG 标 `CLK_IS_CRITICAL`，显示正在使用，应为 19.2 MHz。
- **关闭样本**：S2 的 `gcc_pdm_xo4_clk`（CBCR 0x33008，mux sel 0x99），前提是 C2-01 读到其原状态为 OFF。
- **身份修正（0237）**：`0x43008` 不是 PDM XO/4，而是 `gcc_xo_div4_clk`（localID 7）；0127／0128 的测量参考源实际就是它（写 `0x43008` 的 enable 后读回 `0x1`）。本表按寄存器地址仍然正确，只是名字换了。

## S2：普通 branch gate（C2-03／04／05）

| 项 | 内容 |
|---|---|
| 资源 | `gcc_pdm_xo4_clk`，CBCR 0x33008，enable bit0，`BRANCH_HALT`（CLK_OFF bit31），无 RCG 父源（XO/4 固定 4.8 MHz） |
| 当前 owner／使用者 | XNU 无；b32 DTB 中无任何节点引用 GCC_PDM_*（id 68–71 引用数 0）。PDM 是 PWM 外设，本机无已知使用者 |
| 排除理由 | 不选 UFS／USB／QUP／DISP 等任何有 DTB 消费者或 C0-02 中有继承依赖的 branch |
| 父源／电源凭据 | CXO 同 S1；GCC 常供电；不需要 GDSC |
| 原状态 | **0126 实测 OFF**（`0x33008`＝`0x80000000`：off=1、enable=0），0130 门控档的重读必须仍是这一值（`phase=request result=0 enable=0 off=1 value=0x80000000`）。若变 ON 则说明有未知使用者，S2 作废，改选下一候选 `gcc_pdm2_clk`（0x3300c，但有 RCG 父源，需额外确认 0x33xxx RCG 已配置） |
| 允许动作 | 原为 OFF 时：enable → 等 CLK_OFF 清零 → debugcc 测得 ≈4.8 MHz → disable → 等 CLK_OFF 置位 → debugcc 得 NotRunning。C2-03 只做前半段（enable → 等 halt 清零 → 留样本到重启），disable 归 C2-04 |
| 停止条件 | CLK_OFF 等待有界；超时保留所有权并报告 Uncertain |
| 结论 | **可实施**（0126 已观测 OFF）；0128 已作为关闭样本测得 `hz=0 running=0`；0130 执行 C2-03 enable 与门控 |

“DTB 无引用”只能排除 Linux 消费者，不能排除固件／TZ 使用。保护措施：只在原状态 OFF 时动作，且只操作 bit0。

## S3：apps RSC 既有合法请求（C3-01／03／04）

| 项 | 内容 |
|---|---|
| 资源 | ARC `mmcx.lvl` 显示 Turbo 票（SweetRPMh `beginDisplayPower`，调用方 Resources.inc:113），备选 BCM CN0 300 MB/s（Core.inc:126） |
| 当前 owner | SweetRPMh（apps DRV2 slot 0..1） |
| 理由 | 每次 managed 显示启动都会自然发出，已有 fresh IRQ 完成路径及回归（rpmh-resources、rpmh-completion-irq） |
| 允许动作 | 只观察自然请求；不新增资源或票值 |
| 结论 | **可实施** |

## 后续 voted／reset／PLL／GDSC 样本

C0-04 已将 C2-06 voted、C2-07 reset 暂缓；PLL／GDSC 只在 C6 迁移已有显示操作时使用显示自身资源，进入对应卡前补样本卡。

## 第一个候选镜像需要的只读快照

合并 C1-01～04、C2-01／02（全部写计数为零）时，C2-01 启动快照记录 11 个寄存器（厂商偏移，物理地址 = `0x00100000 + 偏移`）：0x10b00c、0x10b038、0x10b048、0x33008、0x3300c、0x33010、0x33014、0x43008、0x62000、0x62004、0x62008。**0126 实测值**（`sweetlog-0126-0.bin`，`phase=snapshot result=0`）依次为 `0x80000003`、`0x4221`、`0x1`、`0x80000000`、`0x80000000`、`0x80000000`、`0x100000`、`0x80000000`、`0x14b`、`0`、`0x80000000`；其中 `0x33014`＝`0x100000` 给 `SRC_SEL=0`／`hid_div=0`／`mode=0`，即 `gcc_pdm2_clk_src` 选 XO、未编码分频——C2-02 由它解出 `hz=19200000 known=1`。
