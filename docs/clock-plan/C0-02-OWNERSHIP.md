# C0-02 结果：现有时钟相关寄存器所有权表

[返回 C0](00-baseline.md)。日期：2026-11-22。基线 `b3f735b`（[C0-01](C0-01-BASELINE.md)）。等级：**调查完成（源码）**；实机日志交叉确认项见末节，未新增观测。

路径简写：`A/` = `dxb/xnu/iokit/Kernel/arm/`，`B/` = `dxb/xnu/bsd/dev/`。

## 窗口来源

ADT 节点由 `tools/mk-mh2lm-mdss-adt.py` 派生，没有独立的时钟控制器节点（见 [C0-05](C0-05-SCOPE.md)）。

- `arm-io/mdss`（`sweet,mdss`）：reg 0 = GCC 页 0x10b000/0x1000（低于 SOC_BASE，用 LOW_ALIAS 别名），reg 1 = DISPCC 0xaf00000（resource 模式 0x20000，含 GDSC／clamp），reg 12 = DSI PHY+PLL0，reg 13–15 = display RSC 0xaf20000／TCS 0xaf21c00／wrapper 0xaf30000。驱动启动时硬校验 PA／长度：A/SweetMDSS.cpp:643-657、738-742；A/SweetMDSSNoC.inc:314-321。
- `arm-io/display-rpmh`（`sweet,rpmh`）：apps RSC DRV2 0x18220000/0x100、TCS 0x18220d00/0x3000、cmd-db 0xc3f000c/8，可选 GIC；校验 A/SweetRPMh.cpp:557-566。
- 写能力开关链：`sweetmdss` → `sweetdisplayresources`（只有 2 允许写，SweetRPMh.cpp:370/488/645）→ `sweetdisplayclocks=1`（Clock.inc:150）→ `sweetdisplaycore=2` 且 `sweetdisplaymanaged=1`（:164-168）。

## 所有权表

| 资源 | 寄存器 | 现有 writer | 触发 | 操作 | 访问前置／继承假设 | writer 数 |
|---|---|---|---|---|---|---|
| GCC DISP_AHB 0x10b00c、DISP_HF_AXI 0x10b038 | CBCR | **无**；只读：SweetMDSS.cpp:1317/1498，Clock.inc:67/77，Core.inc:13 | 每次访问 DISPCC 前的检查 | 只读 | 依赖 ABL 留下的 CLK_ENABLE（LG 标 CLK_IS_CRITICAL） | 0 |
| GCC 其他（QUP、USB GDSC／CBCR） | — | B/sweet_touch.c 的写路径在 MH2LM 不编译（`SWEET_DEV_TOUCH 0`）；B/sweet_usb.c:169-177 只读 | — | — | USB 依赖 ABL 留下的 GDSC／CBCR | 0 |
| GCC 本地 vote 寄存器（0x52xxx） | — | 无 | — | — | — | 0 |
| GCC debug mux 0x62000／0x62004／0x62008／0x62038／0x6203c、XO/4 CBCR 0x43008 | — | **未引用**（全树 grep 为空） | — | — | — | 0 |
| DISPCC MDSS GDSC 0x3000 | SW_COLLAPSE bit0 | Core.inc:161 | Resources.inc:125 `beginCoreResources` ← Scanout.inc:193/251 | 上电投票，从不写断电 | 先 ARC Turbo 与 CN0 完成；warm 路径要求 GDSC 已 ON（Core.inc:96） | 1 |
| DISPCC PLL0／PLL1（Trion） | 配置／run／out | Core.inc:189-208 | 同上 CorePLL 阶段 | 配置／使能 | 已锁定判为 inherited 并跳过（:173-175） | 1 |
| DISPCC RCG AHB 0x22bc、MDP 0x20c8 | CMD／CFG | Core.inc:222/228 | 同上 | 切源／更新 | 只选受控 PLL 源 | 1 |
| DISPCC 10 个 branch（Clock.inc:8-19） | CBCR bit0 | Clock.inc:445 `advanceLocalClocks`；AHB／MDP 另有 Core.inc:257（cold） | boot 租约、DSI 传输、显示客户端、NoC(RSC)、managed | 引用计数 enable／disable | `clockPrerequisites`（Clock.inc:49-88）；latch `mdssClockWriter` | 1 实例／2 路径 |
| DISPCC link：ESC／BYTE／PCLK 门控、root 0x2110／0x2098、byte_intf div 0x2128 | | Link.inc:181/207/390-407/546 | `beginLink`／`resumeLink`（Resources.inc:171；Lifecycle.inc:296/561/610） | 门控、RCG、分频 | **与 Clock.inc:445 写同一批 CBCR**，同一实例内靠阶段互斥 | 1 实例／2 路径 |
| DISPCC 0x8000（DSI0 misc／reset mask） | | Link.inc:115/175/285/340/461 | link 状态机、ULPS | reset mask | — | 1 |
| DISPCC RSCC_BCR 0x4000 | | 只读（Clock.inc:324/338） | — | — | — | 0 |
| display RSC 0xaf20000 + wrapper 0xaf30000 | seq／solver／CLK state／TCS_USE_OK | NoC.inc:232 `initNoCRSC`，:264-289、:699 | `advanceNoC` ← `beginBandwidth` ← Resources.inc:136/303 | 初始化、握手 | 需 Clock(RSC) 租约（0x4004／0x400c／0x4008）；假设 ABL 未运行 RSC；latch `mdssNoCWriter` | 1 |
| display RSC TCS 0xaf21c00（DRV0） | slot 2..3 | NoC.inc:747 写／:772 触发 | 同上 | BCM MM1／MM0／SH0／MC0／ACV | 只用空闲且地址不冲突的 slot | 1 |
| apps RSC TCS 0x18220d00（DRV2） | slot 0..1 + IRQ_EN | SweetRPMh.cpp:843 写／:867 触发／:217 清 IRQ | 见下 | VRM／ARC／CN0 | latch `rpmhOwner`；旧 B/sweet_rpmh.c 在 MH2LM 排除 | 1 |
| GPUCC／VIDEOCC／CAMCC／NPUCC／SCC | — | 无（B/sweet_gpu.c 仅 Guacamole 板） | — | — | — | 0 |

### RPMh 命令来源

- VRM ldoa14／ldoa17／ldoc3／ldoa5：SweetRPMh `begin()`（:643-706），调用方 Resources.inc:97（managed）、PanelSupply.inc:98/168（面板供电）。ldoc3／ldoa5 同时供 UFS／USB PHY，这些使用者在 XNU 中无票，最后撤票被拒（:24-28）。
- ARC cx／mx／mmcx：`beginDisplayPower`（:486），调用方 Resources.inc:113。
- BCM CN0：`beginRegisterBandwidth`（:368），调用方 Core.inc:126、NoC.inc:643。
- BCM MM1／MM0／SH0／MC0／ACV：**不走 apps RSC**，由 SweetMDSS 经 display RSC 发出；SweetRPMh 只提供 cmd-db 元数据。
- XNU 中没有 GCC 本地 vote 写；所有“投票”都是 RPMh TCS 命令。

## 迁移清单与切换点

| 资源组 | 现 owner | 拟接收方 | 旧方禁止写入的切换点 |
|---|---|---|---|
| apps DRV2 TCS／IRQ | SweetRPMh | IOQcomRSCChannel(apps) | C3-04 启动时 owner 选择 |
| cmd-db 查询 | SweetRPMh | 公共 Command DB 模块 | C3-02 只读，无写切换 |
| DISPCC branch（10 个） | SweetMDSS Clock.inc + Link.inc | IOQcomDispCC | 每个 branch 的 C6-03 实例；**Link.inc 的同寄存器写必须同卡迁移** |
| DISPCC GDSC／PLL／RCG | SweetMDSS Core.inc | IOQcomDispCC | C6-07／08／09 |
| display RSC／TCS | SweetMDSS NoC.inc | IOQcomRSCChannel(display) | C6-13 |
| GCC 全部（含 debug mux） | 无 | IOQcomGCC | 无旧方；C2 首次写即新 owner |

## 未知项（各自阻塞相关交接，不阻塞 C0-03）

1. **GCC DISP_AHB／HF_AXI 没有 owner**，完全靠 ABL 状态；IOQcomGCC 建立后应以 inherited/pinned 登记，不得关闭。查证：C2-01 只读快照这两个 CBCR，并在普通 OFF/ON 前后各取一次。
2. **display RSC 的初始状态**：`initNoCRSC` 假设 ABL 未留下正在运行的 RSC。代码已在 `takeNoCRSC` 前读 0x404／0xc14／0xc20 及 wrapper +0x4（NoC.inc:256-258）；需从 b32 日志确认对应值（阻塞 C6-11 起）。
3. **apps DRV2 的 SLEEP／WAKE TCS 内容**：代码只用 ACTIVE slot 0..1，用“空闲且无同地址”规则避让，未处理 ABL 遗留的 SLEEP／WAKE TCS。查证：C3-01 只读 dump 全部 TCS 的 enable／addr（阻塞 C8-03）。
4. **DISPCC branch 双路径**（Clock.inc:445 与 Core.inc:257／Link.inc）只靠阶段互斥，没有统一仲裁；C6-03 迁移 ESC／BYTE／PCLK 时必须先在本地测试确认 link 关门控时 Clock 租约计数一致。
5. B/sweet_display_probe.c 在 `sweetmdss=0` 时会只读映射 GCC／DISPCC，不是 writer，但要在 C2-01 中避免重复映射冲突。
