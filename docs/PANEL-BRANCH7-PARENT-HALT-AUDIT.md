# branch7：父时钟链和 halt 语义核对

**2026-11-13更新：a25的OFF已经为0；当前超时由XNU额外的enable读回条件触发，
不能称为已证实的Linux halt握手超时。** [0213局部取证](PANEL-RSC-LOCAL-RESET-EVIDENCE.md)
已经实现并验证，待用户云编译；本次不再追加一个内核诊断版本。

## a25之后：核对外部分析与完整leaf调用链

Lineage `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd` 的实际调用链为
`clk_branch2_enable → clk_branch_toggle → clk_enable_regmap → mb → clk_branch_wait`。
`regmap_update_bits`提交bit0后，wait调用`clk_branch2_check_halt`；没有再次要求bit0为1。
它检查当前电平，不要求先观察OFF=1再观察OFF=0。**CBCR0会直接通过这个开启检查。**
`clk_is_enabled_regmap`是单独的操作，仍读取bit0；因此enable读回有诊断价值，
但不能把我们附加的读回门槛称为Linux的完成判据，也不能因Linux返回成功就断言写入一定被硬件接受。

`BRANCH_VOTED=BIT(7)`是软件策略标志，不是CBCR中的状态位。这个leaf依旧本地RMW，
开启时检查halt，关闭时只延时10us，不等其他投票者的共享时钟关闭。
它不表示“等待voted位置位”，也不表示“用RPMh替换本地enable写入”。
ARC/CN0上游投票是另一层依赖。

外部分析中的地址需改为：

| 寄存器 | 本机物理地址 | 定义依据 |
|---|---|---|
| RSCC_BCR | `0x0af04000`，bit0 | dispcc-sm8150.c reset表 |
| NON_GDSC_AHB CBCR | `0x0af04004`，enable bit0 / OFF bit31 | 同文件branch定义 |
| AHB CMD_RCGR / CFG | `0x0af022bc` / `0x0af022c0` | 同文件ahb_clk_src；CFG为CMD+4 |
| MDSS GDSC | `0x0af03000` | sm8150-gdsc.dtsi |

`0x0af04150/4154`不是本机AHB root；`0x0af03000+0x3000`得到SPDM BCR所在的
`0x0af06000`，不能作为MDSS GDSC读取。a25的source4是PLL1 main，不是GPLL0；
CFG=0也不能普遍判为未配置，XO旁路存在合法零配置。

a25没有BCR样本，`sde_rsc_clk_enable()`及其所在sde_rsc源码中未发现RSCC reset操作。
因此“reset最可能未释放”以及“reset必然使enable写入被忽略”尚无本机证据。
父源已经经过coreReady的PLL输出、频率、root与域检查；这不覆盖未知电气依赖，
但也不能说父源根本没有被开启或核验。OFF0是状态位报告，不是独立的物理时钟测量。

本次新增可复现核对：

```sh
python3 tools/tests/audit-mh2lm-lineage-branch.py \
  ../lge-lineage-kernel ../android_kernel_lge_sm8150
```

脚本动态提取上述实际函数及branch2 disable/is_enabled，核对LG/Lineage内容字节一致，
在mock regmap与时间接口上执行24场景，ASan/UBSan通过：HALT/VOTED的六种CBCR组合、
延后OFF清除、update返回错误、四种关闭状态；检查RMW→屏障→状态读取顺序。
CBCR0时enable返回0而is_enabled返回0；OFF1持续500次读后WARN/-EBUSY；
VOTED disable不读halt，只延时10us。这是leaf函数执行验证，不含CCF父递归、真实总线或实机时钟。

## enable门槛的影响与下一步选择

当前软件门槛和硬件根因需分开处理。检查实际0213树后，影响范围为：

| 代码 | enable检查覆盖范围 |
|---|---|
| Clock.inc | retain准入/是否写票、worker等待、最终校验；ownedMask在写前置位，仅表示保守账本 |
| NoC.inc | noCClocksReady在访问RSC/TCS前复查branch7 enable/OFF和父权限 |
| Link.inc / Lifecycle.inc | 遍历knownMask中非PHY分支，可能包含branch7 |
| Clock.inc localClocksReady / Resources.inc managedRetainedResources | 当前只验证BOOT的DSI0 mask0x7f，不含branch7；不应全局替换这些位检查 |

仅删除worker的enable条件会在最终校验或NoC准入被再次拒绝。
若后续改为Linux式完成语义，需要同时定义已发出请求与已完成租约的区别、继承票、
共享引用和最后一次撤回，并覆盖Held后enable消失的处理；不能用写前ownedMask直接宣称已完成。
额外enable检查是我们的策略选择，**不是Linux硬件规范要求**；它本身属于待评审的实现差异。

本轮选择先执行已就绪的0213，取得warm-entry / pre-write / timeout的BCR和局部状态，
帮助判断是否存在复位或core转换相关变化，再选择局部控制修正或统一修改完成语义。
不凭当前读零直接复位正在继承的RSC，也不混入未经验证的地址、顺序或RPMh替代操作。
0213保持现有判据，所以仍可能在同一处失败；其目的只是补缺失证据，并非握手修复。
用户push一次包含0213的代码即可，a25日志无需重提，文档/离线核对不增加另一次云编译。

以下保留2026-11-12的a23时点审计。

2026-11-12。用户要求先核对父依赖和状态语义，不为NoC/HANDOFF连锁报错另开修复。
本次对照LG、Lineage及实际0211源码，未改变生产内核；0211仍用于取得a23缺失的现场读回。

## 寄存器及极性

LG与工作区`lge-lineage-kernel`的定义一致：

| 项目 | 核对结果 |
|---|---|
| branch7 | `disp_cc_mdss_non_gdsc_ahb_clk` |
| enable / halt寄存器 | 均为DISPCC+0x4004 |
| 自身enable | bit0，写1开启 |
| ops | `clk_branch2_ops`，实际调用`clk_branch2_check_halt()` |
| 停止状态 | CBCR bit31：1停止、0开启；不是bit0，也不反转 |
| BRANCH_VOTED | 是软件策略枚举的bit7，不是要求写CBCR bit7；开启仍检查halt，关闭仅延时10us |
| HWCG | 该分支没有声明hwcg_reg，不能引用“HWCG跳过halt”作为放行理由 |

容易混淆的是`struct clk_branch`未显式设置`halt_bit`，默认值为0；但这里的branch2 ops
不读取该字段，而是直接使用bit31。不能拿普通`clk_branch_check_halt()`的bit0逻辑代替它。

branch2参考函数先将CBCR掩为`0xf0000000`，开启时判定`!CLK_OFF || masked==0x20000000`。
第二个分支也必然要求bit31=0；**FSM=2不能放行bit31仍为1的0xa0000000**。
LG和Lineage该函数字节一致，提取实际函数离线执行64组OFF/FSM/enable/无关位组合已核对。
我们目前开启完成条件为`enable==1 && OFF==0`，是在参考halt条件上加自身写后读回，未发现极性错误。
voted释放则只确认自身enable清除并异步settle，不要求共享aggregate停止；0210已区分。

参考：`drivers/clk/qcom/dispcc-sm8150.c:1408`，`clk-branch.c:55/:79/:114/:271`，`clk-branch.h:43`。

## 实际父链及软件对接

当前选择的链路是：19.2MHz bi_tcxo → DISPCC PLL1 main 600MHz → AHB root /8 75MHz → branch7。

| 层级 | 参考定义 / 当前检查 |
|---|---|
| PLL1 | DISPCC+0x1000；L=0x1f、alpha=0x4000，对应600MHz；source4指PLL1 main，source5才是even输出 |
| PLL输出 | corePLLReady(1)读MODE的LOCK、OUTCTRL、reset/run控制，OPMODE(+0x1038)=RUN，USER_CTL(+0x100c)main output bit0；不是仅看LOCK或软件ready |
| AHB RCG | CMD +0x22bc、CFG +0x22c0；source字段[10:8]=4，HID[4:0]=15，公式600×2/(15+1)=75MHz，受控CFG为0x40f |
| RCG状态 | coreReady()要求ROOT_OFF清除、UPDATE/dirty清除、受控CFG/模式匹配；clockParent()再保存CFG并检查漂移 |
| 域与访问 | GCC_DISP_AHB使能、DISPCC AHB开启、MDSS GDSC PWR_ON且不处于强制override/HW控制；另外保留原HF总线检查 |
| 投票权限 | clockEnableParentReady()要求core profile2、实际coreReady、typed ARC Turbo成功/ACK7/非uncertain/启用及当前CN0成功/非uncertain/至少300MB/s；仅给表声明的两路RSC AHB权限 |
| 分支写后 | 实际RMW bit0，再由异步worker读回自身enable及OFF；写前保留owned账本不能当作硬件已完成 |

core2不仅设置频率，也执行PLL1输出开启和AHB root UPDATE；a23的inherited-mask=1说明
PLL0继承、PLL1由本次core路径建立。每次clock worker先运行clockPrerequisites()，
CoreReady状态下会重新调用coreReady()读物理父链，再进入overall期限/分支处理。
等待branch7时还复核父配置和自身票对应的typed权限。

因此，a23的Timeout不能直接解释成“代码完全没有开启或检查PLL1父源”。若这些已检查的
父链位持续不满足，当前实现应在更早阶段返回NotReady（部分写前权限缺失为Busy），而不是进入Timeout。
这也不证明所有电气依赖已齐全：参考资料未表达的依赖、读数变化、写后状态及时间仍需现场证据。

参考：`dispcc-sm8150.c:145/:300/:323/:330`，`clk-alpha-pll.c:919`；实际源码
`SweetMDSSCore.inc`的corePLLReady/coreReady/CorePLLOut/CoreRoot与
`SweetMDSSClock.inc`的clockPrerequisites/clockEnableParentReady/advanceLocalClocks。
实际dp_wr写后有`dsb sy`；不是完全缺少参考`clk_branch_toggle()`所要求的写入屏障。

## 尚未闭合的参考差异

1. LG AHB RCG设置`enable_safe_config=true`。CCF递归启用父源后，RCG enable会临时force root，
   配置已保存的频率再清force。我们的core路径直接做受控CFG/UPDATE并核验root ON，
   未照搬该完整CCF操作序列。这是明确的实现差异；a23已通过root ON检查，不能直接断言它导致branch7 Timeout，
   也不能据此永久置ROOT_EN或重启正在使用的父源。
2. LG sde_rscc的DTS顺序为VSYNC → NON_GDSC_AHB → RSCC_AHB；clock-names解析和
   msm_dss_enable_clk()保留这个顺序，关闭逆序。我们目前按branch编号取得7 → 8 → 9。
   三者在时钟树上不是父子，AHB源与VSYNC源也不同；但是否存在RSC握手次序依赖尚未实证。
   记录为候选差异，不把它说成已发现的电气父依赖或已确定根因，不在无新读回时混入顺序改动。

参考：`clk-rcg2.c:656`；`sm8150-sde.dtsi:300`；`sde_power_handle.c:310`；
`sde_rsc.c:324`；`sde_io_util.c:444`。

## 本次验证和下一步

- 在真实0211 provider中注入17项父链/域/总线/typed权限故障，分别发生在branch7写前和写后，
  共34场景通过ASan/UBSan：均在对应检查返回NotReady/Busy，没有伪装成Timeout、继续写分支或提交display TCS。
- 提取LG实际branch2 halt函数，核对Lineage字节一致及64组状态组合；通过ASan/UBSan。
- 底稿在`scratch/branch7-parent-audit/`：check.py、parent/halt.cpp、结果日志及report.json。
  这些是离线源码审计，不代表新的实机通过或硬件采样。

下一份0211日志先看branch7的write/sample/timeout：自身enable是否读回、bit31是否清除、
命中分支还是整体期限，以及样本是否来自当次callback。先据此收敛以上差异；NoC/HANDOFF连锁结果不单独修。
本次未改变内核、固件、参数、CircleCI或已有驱动，不增加一次云编译要求；旧a23不重测。
