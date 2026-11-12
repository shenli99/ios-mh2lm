# branch7：父时钟链和 halt 语义核对

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
