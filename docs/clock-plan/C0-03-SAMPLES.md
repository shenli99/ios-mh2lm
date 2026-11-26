# C0-03 结果：首批安全试验样本

[返回 C0](00-baseline.md)。日期：2026-11-22（S4 追加于 2026-11-26）。等级：**调查完成（资料）**；样本的上电原状态尚无实机读数，由 C2-01 的只读快照补齐后才允许进入写卡；S4 的原状态由 0137 的只读探针补齐。

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
- **关闭样本**：S2 的 `gcc_pdm_xo4_clk`（CBCR 0x33008，mux sel 0x99），前提是 C2-01 读到其原状态为 OFF。C2-03 把它打开之后，同一路 mux 在 0130-1 变成可测的第三路源（`cc1:0x4`，实测 19200036），关闭态与运行态的对照也因此留在同一条环里：`0x9a`（`gcc_pdm2_clk`，关）读到短长计数相等即 `hz=0`，`0x99`（已被门控打开）读到 16396/262157。
- **身份修正（0237）**：`0x43008` 不是 PDM XO/4，而是 `gcc_xo_div4_clk`（localID 7）；0127／0128 的测量参考源实际就是它（写 `0x43008` 的 enable 后读回 `0x1`）。本表按寄存器地址仍然正确，只是名字换了。
- **频率修正（0238）**：`0x33008` 的名字里带 `xo4`，但它**不是** XO/4 的子节点。厂商 `MUX_SRC_LIST` 把 debug index `0x99` 映射到 `gcc_pdm_xo4_clk`，而该 branch 在 0x33008 只有 `enable_mask=BIT(0)` 与 `clk_branch2_ops`，**没有 `.parent_names`、没有分频字段**——没有父名的 branch 无法对父源分频。0130（声明 XO/4＝4.8 MHz）被 0130-1 的计数直接反驳：门控自身测得 `19200036`，等于声明值的 ×4.0000075。全系统唯一的 /4 是计数器时基 `0x43008`。**0131 验证**：改成 XO（19.2 MHz）后同一路源测得 `19199963`（`delta-ppm=-1`，`result=0`），两个对照（`0x53` 19199963、`0x9a` 0）不变，门控行逐字未动——名字里的 `xo4` 到此只作历史。

## S2：普通 branch gate（C2-03／04／05）

| 项 | 内容 |
|---|---|
| 资源 | `gcc_pdm_xo4_clk`，CBCR 0x33008，enable bit0，`BRANCH_HALT`（CLK_OFF bit31），无 RCG 父源、无分频（XO 直通，固定 19.2 MHz；名字里的 `xo4` 不是分频，见下方频率修正与 0130-1 实测） |
| 当前 owner／使用者 | XNU 无；b32 DTB 中无任何节点引用 GCC_PDM_*（id 68–71 引用数 0）。PDM 是 PWM 外设，本机无已知使用者 |
| 排除理由 | 不选 UFS／USB／QUP／DISP 等任何有 DTB 消费者或 C0-02 中有继承依赖的 branch |
| 父源／电源凭据 | CXO 同 S1；GCC 常供电；不需要 GDSC |
| 原状态 | **0126 实测 OFF**（`0x33008`＝`0x80000000`：off=1、enable=0），0130 门控档的重读必须仍是这一值（`phase=request result=0 enable=0 off=1 value=0x80000000`）。若变 ON 则说明有未知使用者，S2 作废，改选下一候选 `gcc_pdm2_clk`（0x3300c，但有 RCG 父源，需额外确认 0x33xxx RCG 已配置） |
| 允许动作 | 原为 OFF 时：enable → 等 CLK_OFF 清零 → debugcc 测得 ≈19.2 MHz → disable → 等 CLK_OFF 置位 → debugcc 得 NotRunning。C2-03 只做前半段（enable → 等 halt 清零 → 留样本到重启），disable 归 C2-04（0132 已做：撤回一位 → 首读即见 `CLK_OFF` 置位 → 账本以 halt 作确认归还 → 同一开机内用已验 enable 恢复到原值）；实测 19.2 MHz 已被 0130-1／0131 两轮计数确认（19199963／19200036） |
| 停止条件 | CLK_OFF 等待有界；超时保留所有权并报告 Uncertain。C2-04 的 halt 超时或四邻位漂移只保留已写状态并报错，绝不写回去 |
| 结论 | **可实施**（0126 已观测 OFF）；0128 已作为关闭样本测得 `hz=0 running=0`；0130 执行 C2-03 enable 与门控并通过；0130-1 在此基础上把该 branch 当作第三路测量源，测得 `19200036`（对照声明值 4800000 即 ×4），据此把父源改正为 XO（0238）；0132 完成 C2-04，同一次开机内 enable→halt→归还→再 enable，收尾 `writes=3`、恢复到 `enable=1 off=0` |

“DTB 无引用”只能排除 Linux 消费者，不能排除固件／TZ 使用。保护措施：只在原状态 OFF 时动作，且只操作 bit0。

## S4：voted branch 本方票（C2-06）

| 项 | 内容 |
|---|---|
| 资源 | `gcc_npu_axi_clk`，CBCR 0x4d008，`enable_mask=BIT(0)`，`halt_reg=0x4d008`（与 enable 同址）、`halt_check=BRANCH_VOTED`、`ops=clk_branch2_ops`；同页另有 0x4d00c `gcc_npu_trig_clk`、0x4d010 `gcc_npu_at_clk`（同样 voted），以及 0x4d004 `gcc_npu_cfg_ahb_clk`（`BRANCH_HALT`＋`hwcg_reg=0x4d004 hwcg_bit=1`＋`CLK_IS_CRITICAL`） |
| 当前 owner／使用者 | 无（C0-02：全树未引用）。b32 DTB 无 NPU 子树，**没有消费者证据可引用**——本样本能证明的只有寄存器自身，不能被用来论证“NPU 没在用” |
| voted 语义的来源 | `clk-branch.h:43` 把 `BRANCH_VOTED` 注释成 “Delay on disable”，`clk-branch.c:82-111` 的 `clk_branch_wait()` 因此分成两半：**开**是 500×1 µs 轮询 halt 谓词，**关**只有一次 `udelay(10)` 然后返回 0，既不轮询也不宣称 aggregate 已停（该位是“至少一个 master 还在投票”的或，别的 master 的票它看不见） |
| 计数含义 | 0x4d008／0x4d00c／0x4d010 的 bit0 是**本驱动的票**，不是钟的开关；bit31 `CLK_OFF` 是 aggregate 状态。两者不等价，所以“自己写了 0”永远不能被写成“钟关了” |
| 排除理由 | 不选四个强制窗口：`0x10b000`（显示）、`0x133000`（PDM）、`0x143000`（XO/4）、`0x162000`（debug mux）里没有 voted branch；不选 `gcc_ddrss_gpu_axi_clk`(0x71154)／`gcc_gpu_memnoc_gfx_clk`(0x7100c)——GPU 侧有既存 writer；不选 0x4d004，它带 `CLK_IS_CRITICAL`＋HWCG，只能当本页的对照，不能当票 |
| 父源／电源凭据 | NPU 侧由 `npu_gdsc` 供电，GCC 只提供 CXO 与常供电寄存器；**本卡不对 NPU 做任何供电或 GDSC 动作**，只投票／观察 |
| 原状态 | **0137 只读探针实测**：`0x14d004/8/c/10` 四值见 [02-gcc](02-gcc.md) 的 0137 节。bit0 已置位＝开机就存在一张本驱动没投过的票（真实第二持有者）；清零＝单投票者场景，正好是撤票证明需要的那一半 |
| 允许动作 | 先只读一轮；写阶段只置本方位 bit0、有界等待 `CLK_OFF` 清零；撤回只用已核实的 settle，**不宣称 aggregate OFF**，不向 RSC 发此票 |
| 停止／撤回 | 等待超时保留已写票并报 Uncertain，不写回去；读到第二投票者时如实记录（这正是共享硬件场景），不擅自替别人撤票 |
| 结论 | **可实施，但只在本方票这一层**；0137 是必做的只读前置，0138 才写票 |

voted 与普通 branch 的差别只在**等待**，不在位语义：两者都是 bit0 置位投票、bit31 报 aggregate。C2-03／04／05 的经验（写一位、等 halt、以 halt 作归还确认）在 voted 上只能原样搬到“开”的方向；“关”的方向厂商自己都只等 10 µs，本卡因此把三件事分开报：写了本方位、开方向的完成、撤回后的 settle。

## S3：apps RSC 既有合法请求（C3-01／03／04）

| 项 | 内容 |
|---|---|
| 资源 | ARC `mmcx.lvl` 显示 Turbo 票（SweetRPMh `beginDisplayPower`，调用方 Resources.inc:113），备选 BCM CN0 300 MB/s（Core.inc:126） |
| 当前 owner | SweetRPMh（apps DRV2 slot 0..1） |
| 理由 | 每次 managed 显示启动都会自然发出，已有 fresh IRQ 完成路径及回归（rpmh-resources、rpmh-completion-irq） |
| 允许动作 | 只观察自然请求；不新增资源或票值 |
| 结论 | **可实施** |

## 后续 voted／reset／PLL／GDSC 样本

C2-06 voted 已有样本（S4，0137 只读探针补齐原状态后进入写卡）；C2-07 reset 仍由 C0-04 暂缓。PLL／GDSC 只在 C6 迁移已有显示操作时使用显示自身资源，进入对应卡前补样本卡。

## 第一个候选镜像需要的只读快照

合并 C1-01～04、C2-01／02（全部写计数为零）时，C2-01 启动快照记录 11 个寄存器（厂商偏移，物理地址 = `0x00100000 + 偏移`）：0x10b00c、0x10b038、0x10b048、0x33008、0x3300c、0x33010、0x33014、0x43008、0x62000、0x62004、0x62008。**0126 实测值**（`sweetlog-0126-0.bin`，`phase=snapshot result=0`）依次为 `0x80000003`、`0x4221`、`0x1`、`0x80000000`、`0x80000000`、`0x80000000`、`0x100000`、`0x80000000`、`0x14b`、`0`、`0x80000000`；其中 `0x33014`＝`0x100000` 给 `SRC_SEL=0`／`hid_div=0`／`mode=0`，即 `gcc_pdm2_clk_src` 选 XO、未编码分频——C2-02 由它解出 `hz=19200000 known=1`。
