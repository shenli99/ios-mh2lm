# iOS 16 on sweet (Redmi Note 10 Pro, SM7150) - working notes


## Agent handoff / active work (2026-11-14, 0121a30 artifacts and ABI608 wake test ready)

**CURRENT：用户已下载0121a30；核对含0219+0220并完成ABI608四档打包/回读，待retained一次OFF/ON实测。**
本段替代下方“待push/云编译/无新产物”的NEXT。0220实现提交9244ec1；本轮无驱动修改或实机结果。
78源码与scratch/framebuffer-0220/verify逐字节一致；相比a29仅0219+0220预期8份源码变化。
原114项CI source markers、222内核markers通过；WAKE/LINEAGE/ULPS/PINS CONFIG/owned recommit齐备。
内核17775352bytes，SHA256=ccf487de8262681d1e0b9d8ace58274d553ed76af8bf6e935eda90c45fa88315；包内SHA256SUMS匹配，ARM64 subtype0。
没有独立CI run ID/完整build log，未核对云端提交元数据和全量告警。
四档boot header/FAT kernel+ADT+TOML回读一致；ABI608固件/BootShim/两个追加DTB保持，全部ADT和args与a29相同。
retained605/607bytes，display161/apps37保持，identity最小ADT不变；panel-retained是历史文件名，mode2采用0220 panel票周期/ULPS恢复。
菜单标题更正为panel sleep and wake，去掉错误的all supply votes held；仅标题修改，不需重编内核。
**NEXT：用户先开USB接收，然后fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a30-abi608-panel-retained.img。**
镜像35545088bytes，SHA256=4bda150aa066c3cd4bcdfe2c49f292f98d56e08f95b5a90a787b54713bf0714a。
日志logs/live-0121a30-abi608-panel-retained.txt；进入桌面确认更新，息屏等约5秒再唤醒，无论结果继续接收至少30秒。
按OFF表done→reset低/ULPS entry/link停/撤票→ON供电ACK/ULPS reentry/PLL/exit/host soft reset→DDIC/ON→owned flush/fresh pixels/touch核对。
配置mismatch只观察；GPIOcommanded不是电压证明，票撤销不是物理断电。若USB缺关键记录再补持久日志。
不需重测a29/identity，不切combined/supply；本地产物核对不等于已修复黑屏，a29仍是最后实测。
底稿scratch/artifact-0121a30/，两份verify脚本和输出、source/changes/verification/boot-verification JSON、packaging.txt；详见docs/PANEL-D1-0121A30.md。
未push/触发CI/操作设备；未改驱动/固件/ADT/args/CI，未重跑既有0220驱动回归；workspace PLAN/编译指南/显示计划已同步。以下交接为历史。


## Agent handoff / active work (2026-11-13, 0220 Lineage suspend/resume implemented)

**CURRENT：用户已授权对齐息屏后状态、host/PHY、恢复画面、复位检查；0220已实现，本地验证通过，待云编译实测。**
实现提交 **9244ec1**（0220及回归/文档/打包门），用户push最新main即可包含本轮修改。
本段替代下方“仅审计/暂缓0219构建”的执行点。a29最后实测仍是桌面正常、OFF完成、ON reset complete之后无记录；根因未确认。
补丁xnu/patches/0220-mh2lm-lineage-panel-suspend-resume.patch，SHA256=2d6c31f88189915ee0e0a068ad3f95d32f92fa8503ff99278de25014f55d5e71。
参考Lineage@2e897dd；四种R66456 DTS均启用lp11/suspendULPS/allowPHYpoweroff。mode2不再保留所有资源常开，镜像名/启动参数仍沿用retained历史名称。
OFF：原表完成→前5ms→suspend pinctrl→reset低5ms→host disable→PHY ULPS100us/状态→gating/link/PLL停→PHY idle-off→reset mask→VPNL/VDDI撤票→5ms。
ON：active-low→前5ms→VDDI ACK+20ms→VPNL ACK+5ms→DATA/host setup/REFGEN/PHY config→ULPS reentry100us/状态→unmask→PLL/roots/branches真实完成→resync→ULPS exit1000us/stop pulse100us/状态→host soft reset≥1us→DDIC10/2/10+5ms→原ON/亮度。
v2.3 DISPCC+8000.bit0是false置1/true清0；v4/controller硬件clamp ops是NULL，不写猜测clamp寄存器，仍配对状态/ULPS。
唤醒跳过DSI+12c冷PHY reset，但PHY idle_on仍重新配置PHY；不能说PHY完全保留不动。初次温接管路径保持。
恢复像素重新提交已pin自有source/stride/crop并flush；按低16位frame_count决定autorefresh或CTL_START，等fresh PP_DONE/flush0/RD_PTR再恢复touch。
GPIO普通CTL/IRQ快照/mux/pull/drive/latch/input差异变SWEET PANEL PINS CONFIG打印，每token最多64条；取消1ms输入跟随门槛。
resetKnown/resetHigh和copyLowStatus明确为已命令状态/有效owner token，不是物理电压证明；eGPIO/IRQ竞争、ABI/provider/排他owner、总调度期限仍检查。
命令表/短FIFO/长prepared物理DMA/200ms fresh done+idle/SMMU边界未修改；PV仍ON694bytes17条、OFF35bytes4条、29先于11；不加PPS/强制首帧亮度延迟。
共享L3/L5/ARC Turbo/boot/config与MDP/SMMU主体仍保留；本轮不是完整CORE collapse、DRM或RSC sleep/wake移植。panel撤票不保证物理断电。
12套ASan/UBSan通过：新增Lineage wake、profile、pins、supply fences，及host/link/lifecycle/FB/resource/link-cycle/combined/supply。
新增四profile各三轮、寄存器与ULPS最小延时/bit极性/无冷reset、framecount0分支、flush完成门槛、真实状态失败、每新增等待cancel/stop/provider-loss。
普通GPIO回显/输入不符继续，实际eGPIO/IRQ owner失去停止；旧故障用例对应更新。Lineage soft reset测试按其保留setup、恢复clock/control合同，低层host另测破坏性上下文重建。
实际抽取执行Lineage六个底层函数，寄存器/等待轨迹与回归oracle一致。ARM64 MDSS/Pins对象通过；MDSS告警1585→1587，净增两项为固定边界host/gates数组索引；Pins1171→1171。
0185–0220共36patch严格无offset/fuzz重放、78文件一致、114原CI markers通过；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
打包门新要求LINEAGE/LineageSuspendULPS/PINS CONFIG/owned recommit标识，真实旧a29被拒绝，未产生boot输出；0219 WAKE打印保留。
底稿scratch/framebuffer-0220/，prepare-patch会重建verify，不可在编译/测试读它时运行；细节docs/PANEL-LINEAGE-SUSPEND-RESUME.md，工作区PLAN已同步。
**NEXT：用户push最新main并云编译，下载kernel/buildtree（建议0121a30）；agent核对含0219+0220后，沿ABI608/原ADT/retained605字节参数打包。**
进入桌面后先一次OFF/ON，USB保持收取；按ULPS entry/reentry/exit、供电ACK、host soft reset、DDIC/ON、owned flush/fresh pixels/touch顺序看。
当前没有新云端内核/boot/本轮实机结果；未push/触发CI/操作设备，未改固件/ADT/bootargs。以下交接为历史。


## Agent handoff / active work (2026-11-13, audit wake against Lineage before more diagnostics)

**CURRENT：用户要求先比较唤醒流程，暂缓上一段0219 push/云编译/诊断实测安排；本轮只审计和更新文档，没有新增驱动修改。**
比较Lineage@2e897dd与actual0121a29/0218的PV普通OFF→ON；0219只打印，硬件路径相同。
直接抽取DTS与SweetPanelProfile数组逐字节比对：ON694bytes/17条、OFF35bytes/4条完全相等；
ON末尾29→11→150ms，OFF51=0后90ms、10后150ms；reset高10/低2/高10+settle5ms相同。
关键差异：retained2 OFF后reset高/四rails+host/PHY/PLL/clock/NoC全保留；ON仅DDIC reset+ON表，没对应Lineage正常OFF/ON的reset低/供电请求周期、host关闭/配置/soft reset和ULPS/clamp/clock恢复状态机。
PV启用LP11/suspendULPS/allowPHYpoweroff，Lineage不能简化为每次完整PHYreset；v4 PHY有ULPS ops，正常host disable硬件分支可走。
Lineage dsi_display_wake_up是空函数；普通传输统一FETCH_MEMORY/GEM/IOVA、200ms CMD_DMA_DONE；XNU短包FIFO/长包物理DMA+固定CB2，DMA_DONE+idle轮询。
ON第3/6/7/10条>64-byte编码，最大payload149；a29 OFF全FIFO及MDP像素DMA不证明DSI命令DMA已经工作。
Lineage通过DRM管线/flush/kickoff恢复；autorefresh本来就不发CTL_START，不能据XNU未写START简单判漏。
PV无DSC/PPS、无PRE/POST表、无first-frame亮度flag；不能照搬其他面板规则，亮度先于首帧不是已证实错序。
额外强制检查仍有SweetPanelPins input/latch/pinctrl、1ms跟随/100ms总期限；0218没有覆盖该provider，a29已reset complete说明本次已通过。
XNU还额外依赖触摸token/fresh pixel完成和固定SMMU/ownership合同；这些与配置回显不同，不能一起删掉。
**NEXT：以docs/PANEL-LINEAGE-PATH-COMPARISON.md顶部a29专项为依据，先对齐OFF/ON状态配对和host/LP11/ULPS/供电reset顺序，再决定后续实机验证。**
不能只切已有4/5/6档就称为Lineage，也不能把源码差异直接当作a29复位后记录中断的根因。
0219提交d4ffb3d保留，不撤销、不增加诊断。本轮无新内核/镜像/设备操作，不重跑驱动回归（无生产变更）。
对照字节/命令/延时证据：workspace scratch/wake-lineage-a29/command-comparison.json；PLAN、BUILD-GUIDE、a29及显示计划已同步。
以下“立即push/云编译a30”NEXT是本次用户纠正前的历史，不要自动执行。


## Agent handoff / active work (2026-11-13, a29 desktop works; 0219 wake evidence ready)

**CURRENT：用户实测0121a29 retained进入桌面且画面正常；首次完整接管成功。OFF完成，ON在pin复位完成后无后续日志；根因未定。**
本段替代下方“待a29实机/完整接管前暂停”的旧交接；本轮0219仅补诊断，不得宣称已修复黑屏。
实现与实测记录提交 **d4ffb3d**，用户push最新main即可包含0219。
USB logs/live-0121a29-abi608-panel-retained.txt：300909bytes/3728行，SHA256=9d285907d505db553ee2ea30c85d9c12b3f72178ec1331420debcda8e96068a9。
1981新DMA A=c3be4000/fresh PP_DONE100/frame925/flush0，1987touch verified，1988takeover complete，1989HANDOFF active。
3642 RD_PTR约59.9Hz/dropped0；3706OFF begin，3724四条FIFO命令fresh DMA_DONE+idle，3725OFF complete/票保留。
第二次power down在OFF执行期间已到达；3726ON begin，3727pins active，3728reset complete/input1为USB末行。
未见ON D1 admitted、错误码或panic；最后打印不能证明具体函数挂死/整机死机，亦不能排除USB记录中断。
持久日志sweetlog-0121a29-abi608-panel-retained.bin：4198400bytes，SHA256=4a897038731c4cf5e2b29c57f071216bc3db72aaf7f055d243e93ce598839547。
version1/ring4194304/total309220/flush16/未绕回，解码3826行；sample12时间戳与USB同次启动，但落盘停在按键前，无OFF/ON。
底稿scratch/artifact-0121a29/persistent.txt、wake-log-review.json；不必再次导同份日志。
0219补丁xnu/patches/0219-mh2lm-panel-wake-admission-evidence.patch，SHA256=e3b29c7e5dce11c27f1b79bd63bc7fc9f7f186bb4fd18f601a76f2811704f8f6。
修改MDSS.cpp/DSI.inc/Lifecycle.inc：WAKE token/phase/step/value/command/dsi-token/completed；pin阶段每轮依赖和状态查询进出；
ON准入、SMMU snapshot、首次DMA allocate/prepare/physical、逐包encode/registers/trigger和真实done；超时附phase/进度。
PV ON含长包需要DMA，OFF全FIFO；这是新经过的路径，但未证明DMA分配/SMMU是根因。
仅已有gate内软件打印，无新外围MMIO/写入/延时；每次ON1024条+上限提示，新lifecycle重置，终态不继续。
entry/return不是完成证明，command0时completed可能属于旧OFF，必须按DSI token区分；真实IRQ/idle/pixel/touch/权限/SMMU/pin不变。
0218 Lineage配置观察策略保持；未新增自动重试/复位/票变化/放宽完成条件。日志可能影响短阶段调度，实机须核对期限。
九套既有ASan/UBSan通过：四profile retained各三轮与故障退出、DSI host/link、lifecycle/FB、resource parking/link/combined/supply。
ARM64 MDSS对象通过，当前同条件基线/修改均1585告警，类别计数一致；0185–0219共35patch严格无offset/fuzz、78文件一致、114原CI markers通过。
CI配置不变；matrix新门WAKE/reset-proof-ready/dma-allocate-enter；真实旧a29被拒绝且无boot输出，bash语法通过。
底稿scratch/framebuffer-0219/（base/edited/verify、compile/replay/prepare脚本和final-review.json）。prepare-patch会重建verify，不得在测试/编译读取时运行。
**NEXT：用户push最新main并云编译，下载kernel/buildtree（建议0121a30）；agent核对含0219后沿原ABI608/ADT/retained605字节打包。**
用户保持USB接收，进入桌面后做一次OFF/ON；黑屏后继续收约30秒，按最后WAKE step/return/真实首错定位；USB中断再补持久日志。
不重测a29/identity，不用combined/supply绕过当前问题；亮度遍历/长测暂不展开。
详见docs/PANEL-D1-0121A29.md实测和0219章节；docs/FRAMEBUFFER-PANEL-PLAN.md、BUILD-GUIDE.md及workspace PLAN已同步。
当前没有0219新云端内核/boot或实机结果；未push/触发CI/操作设备，未改固件/ADT/启动参数。以下交接为历史。


## Agent handoff / active work (2026-11-13, 0121a29 artifacts and retained ready)

**CURRENT：用户已下载0121a29；产物核对和ABI608四档打包/回读通过，待retained实机USB日志。**
本段替代下方“待push/云编译/没有0218新内核”的交接；本轮无驱动修改。
78源码与scratch/framebuffer-0218/verify字节一致，114原CI source markers、211内核markers通过。
相比a28仅10个预期MDSS文件改变，包含0217/0218、CONFIG和普通/voted时钟请求记录。
内核17758808bytes，SHA256=7c3b50dc16f813aed42c1c04634fd3ee3c07628893323729e1e47ba1e9106ffd，包内自带哈希匹配，ARM64 subtype0。
未取得独立CI run ID或完整build log，未核对云端提交元数据/全量告警。
四档boot header/FAT kernel+ADT+TOML回读通过；ABI608固件、BootShim、两追加DTB保持。
全部ADT和参数与a28一致，retained605/607字节，display161/apps37保持；identity独立最小ADT不变。
**NEXT：用户 fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a29-abi608-panel-retained.img，先开USB接收并记录约90秒。**
镜像35545088bytes，SHA256=f8e725063204dbeb5b0ea31f61ccc1eccdb05ac838b048a4e7d3ae90360d6c0a。
USB路径 logs/live-0121a29-abi608-panel-retained.txt；双终端命令见docs/PANEL-D1-0121A29.md。
按CLOCK Held→enter CLK→65项CONFIG→initialized/ownership→INTID161→fresh ACTIVE/ACK→PHY/host→新DMA/transfer/HANDOFF核对。
配置mismatch只记录；若失败按首个真实等待/完成错误和此前打印值排查。尚无a29实机结果，不能认定已修好。
a28仍是最后实机：qtimer/+2020 wantffffffff/got224cac6，未到display TCS/新DMA；原因未知。
完整接管前亮度/OFFON/长测/combined/supply实机验证继续暂停；无需重测identity/a28。
底稿scratch/artifact-0121a29/，两份verify脚本/输出和packaging.txt齐备；未重跑既有0218驱动回归。
未push/触发CI/操作设备；未改CI/驱动/固件/ADT/启动参数。以下CURRENT/NEXT为历史，以本段为准。


## Agent handoff / active work (2026-11-13, 0218 Lineage config observation ready)

**CURRENT：按用户要求扩展审计 qtimer 后续步骤；0218 已实现配置回读仅打印，相关本地验证通过，待用户 push 云编译。**
本段替代下方“0217 仅六项/其余59项仍严格回读”的交接；新产物必须包含 0217+0218。
补丁 xnu/patches/0218-mh2lm-lineage-config-observation.patch；实现提交 **b349072**（b3490728dd5fad4a597cb83b6b2c6dbd46daa3ae），push 最新 main 即可包含本轮修正。
SHA256=9fc779ee9781c6546f34ef45a175819cc85729af5cdedf6cc29af4c206178658。
参考 Lineage@2e897dd：RSC65项初始化普通写、TCS_USE_OK/override回显、NoC payload/WAIT/ENABLE/IRQ配置/清理、
父时钟原始快照和自有PLL/RCG/branch配置、PHY/PLL/host reset/config、休眠恢复快照及自有scanout source/stride/crop改为打印。
取消 Lineage 未额外要求的 ACTIVE 完成 STATUS idle、PLL关闭unlock、branch关闭ROOT_OFF快照门槛。
CONTROL按读值RMW保留无关位；三个 pre-trigger 同步 CONTROL 回读仍保留，和 Lineage write_tcs_reg_sync 一致。
自己的已完成槽不再要求旧CONTROL/WAIT模板或cleanup ENABLE回显；外来冲突、实际槽可用/本次IRQ关联仍检查。
IRQ_ENABLE回显只打印，本槽真实IRQ+ISSUED/必要COMPLETE仍必需；同时外来源不否决本槽。
真实SEQ_BUSY/UPDATE/CLK_OFF/REFGEN/PLL锁定、命令/像素完成、票据/权限/ABI/maps/版本/生命周期/physical pin保留。
继承的未知源/不支持频率、CPHY/别名消费者、固定geometry/SMMU合同仍拒绝，不能说全部驱动等价Lineage。
统一 SWEET MDSS CONFIG: stage/offset/want/got/mask/mismatch；父时钟branch/field；最多1024条，匹配记录最多768。
配置观察不是ACK，不证明目标频率/带宽/地址生效；stop/失权不为打印补读外围MMIO。
16既有回归+DSI link/host+36完成专项+16qtimer+78新配置场景，共21套ASan/UBSan通过。
ARM64 SweetMDSS对象编译通过；warnings1573→1585，净增12条均为既有unsafe-buffer类别的固定边界诊断数组/表索引。
Lineage动态五helper输出65项值/顺序一致；0185–0218共34patch严格无offset/fuzz重放/78文件一致/114原CI markers通过。
matrix新增CONFIG marker；真实旧a28被拒绝，无boot输出；bash语法通过。底稿 scratch/framebuffer-0218/。
a28仍为最后实机：RSC qtimer index4/+2020 wantffffffff/got224cac6，未到INTID161或display TCS/新DMA。
读值不同原因仍未知；当前没有0218新内核/boot或实机结果，不能认定静帧已修复。
**NEXT：用户 push 最新 main 云编译，下载 kernel/buildtree（建议0121a29）；agent核对包含0218后沿原ABI608/ADT/605字节retained打包。**
先看65项配置→TCS ownership/INTID161→fresh ACTIVE/ACK→PHY/host→新DMA/transfer/HANDOFF；失败按新首错及之前打印值排查。
完整接管前亮度/OFFON/长测/combined/supply实机验证继续暂停。
详见 docs/PANEL-LINEAGE-CONFIG-OBSERVATION.md；已更新流程对照/编译指南/a28和0217文档及workspace PLAN。
未push/触发CI/操作设备；未改CI/ADT/固件/bootargs/其他provider；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
以下CURRENT/NEXT和旧验证要求均为历史，以本段为准。

## Agent handoff / active work (2026-11-13, a28 init failure; 0217 ready)

**CURRENT：a28仍静帧，首错为RSC qtimer初始化index4读回不相等；0217已实现并本地验证通过，待用户push云编译。**
实现提交 **75f5846**（75f5846719086befc0d0d98ca18287bb9f26bb49）；push最新main即可包含本轮修正。
日志 logs/live-0121a28-abi608-panel-retained.txt：208507bytes/2957行，SHA256=30b45bb8a9b0844e439f4391da637002bebd6a3766a86395ebc94253260864f4。
10次apps fresh IRQ/LDO/ARC/CN0和core2成功；1781 CLOCK Held/state2/mask381；1782 RSC enter CLK。
1783 init index4/wrapper1/+2020 wantffffffff/got224cac6，随后NoC IOError e00002ca；group-valid0/trigger-issued0。
尚未走到INTID161配置、display ACTIVE或新DMA；1788/1791为SCANOUT/HANDOFF连锁失败，用户态继续。
不能据此判定完成中断失败，也不能认定CVAL只读/硬件改写/拒写；本次USB足够，无需补持久日志或重测a28。
0217参照Lineage qtimer初始化不要求回显：仅六项CNTP CVAL/CTL取消全值相等等待，保留原写/屏障及一次诊断读。
每项新增qtimer write issued/observed，明确非硬件ACK；其余59项读回和真实IRQ+本槽+ISSUED/COMPLETE判据保持。
65项写数值/顺序、CNTACR bit0、资源准入、失败保票/stop保持；不添加RWPT权限、reset或其他硬件改动。
16既有回归+37完成专项+16新qtimer场景ASan/UBSan通过，实际旧a28源码复现7组失败，含实测224cac6。
真实ARM64 MDSS对象通过，warnings1573→1573；0185–0217共33patch严格重放/78文件一致/114原CI source markers通过。
动态执行Lineage五个初始化函数，65项逐项逐序一致；matrix新增qtimer marker，实际拒旧a28且无boot输出。
补丁 xnu/patches/0217-mh2lm-rsc-qtimer-init.patch，SHA256=ae19f9864a6896b3fa9b4c3c9e6a92456cccb86b866067081463dbe626d5ceff。
**NEXT：用户push最新main并云编译，下载新kernel/buildtree（建议0121a29），agent核对后沿原ABI608/ADT/605字节retained打包。**
先查六条qtimer实际读值→init完成→TCS ownership/INTID161→fresh ACTIVE/NoC ACK→新DMA/transfer/HANDOFF；按新首错继续。
当前没有0217新boot或实机结果；亮度/OFFON/长测/combined/supply在完整接管前继续暂停。
详见docs/PANEL-RSC-QTIMER-INIT.md和PANEL-D1-0121A28.md；底稿scratch/framebuffer-0217/。
未push/触发CI/操作设备；未改CI/ADT/固件/启动参数/其他provider；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
以下NEXT/待实机/未打包等为历史，以本段为准。


## Agent handoff / active work (2026-11-13, a28 retained boot reverified)

**CURRENT：已接手继续屏幕修复；0121a28 retained 镜像已就绪并重新核对通过，待实机 USB 日志。**
纠正上一版“尚未打包”：目录已有四档 ABI608 镜像及回读底稿，本轮复跑两份核对脚本均通过。
78 份源码与 scratch/framebuffer-0216/verify 字节一致，114 项原 CI source markers、195 项内核 markers 通过；
相比 a27 只变更 SweetMDSS.cpp、SweetMDSSNoC.inc、sleh.c，含 0215/0216。
内核 17758560 bytes，SHA256=368c0511a5dee824d33667104478a744a44f583e10de375f71cd678e6eff37f4，自带哈希匹配。
四档 boot header/FAT 中 kernel+ADT+TOML 回读通过；hardware ADT 仅比 a27 增加 NoC IRQ ABI1/INTID161，apps37 保留。
identity ADT、ABI608 固件、BootShim、两个追加 DTB 和原参数保持；retained 605/607 字节。
**NEXT：用户 fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a28-abi608-panel-retained.img，先开 USB 接收并记录约90秒。**
镜像 35545088 bytes，SHA256=b350530af92e7a9aec646219a73af6481f5112a284a4867fe4a535f6a3dc4f36。
日志路径 logs/live-0121a28-abi608-panel-retained.txt；双终端命令和验收见 docs/PANEL-D1-0121A28.md。
按 CLOCK Held→RSC CLK/初始化/使用权→INTID161→各组 fresh ACTIVE→NoC ACK→新DMA/transfer/HANDOFF 检查；失败按新首错定位。
a27 仍为最后一次实机：NoC ACTIVE2 超时、未提交新DMA、HANDOFF失败；a28本地通过不等于硬件修好。
无需重新云编译或重测a27；亮度/OFFON/长测/combined/supply在完整接管前继续暂停。
底稿 scratch/artifact-0121a28/，本轮复跑 verify-artifacts.py 和 verify-boots-abi608.py；未重跑原0216驱动回归。
未核对独立CI run ID/完整build log；未改驱动/固件/参数/CI，未push/触发CI/操作设备。
以下待push/待云编译/未打包等 NEXT 为历史，以本段为准。


## Agent handoff / active work (2026-11-13, 0216 RSC/ACTIVE flow implemented)

**CURRENT：用户要求补齐 Lineage 流程；0216 已落实真实驱动修改，本地验证通过，待用户 push 云编译。**
实现提交 **eb72381**（完整 eb7238167d28a7e34647c5b0ffddc98108d186d3），工作树已提交；用户 push 最新 main 即可。
本段替代下方 0215 仅诊断的编译交接。a27 首错仍为 display NoC ACTIVE2 等待超时，尚无 0216 实机结果。
按 Lineage@2e897dd 的 RSC v2：CLK 清 wrapper override.bit0；TCS_USE_OK 写0/忙则写1让出200us/再写0等待；
持有使用权后首次 qtimer→wrapper→seq→solver→PDC 65 项初始化，后续实例请求不重写表；握手超时未改表/投票则交还use_ok。
真实 solver override/modes/status0/1/2 采样，+d0 日志纠正为 error-irq-status；模式历史值不等于“必须全零”。
独立 INTID161 顶半部/软件事件/sleh 分发，先handler→source enable→trigger→GIC enable；极快完成保留pending。
只有真实 IRQ 事件+本槽源+ISSUED/必要COMPLETE 可确认；timer仅期限/读回/EOI等，不再用busy或裸CMD_STATUS伪造ACK。
ACTIVE成功仅清ENABLE/WAIT/本槽IRQ，保留AMC/control/source；自身槽账本允许trigger保持/自清，复用按clear-trigger→clear-AMC→AMC→trigger。
初次空槽前两次control写0，自身已完成槽首写保留AMC；新专项核对两种精确顺序。
完成后use_ok1再按既有引用账本保留/释放RSC时钟/CN0；失败保票，stop/失权只屏蔽GIC，不访问无权限外围；dispatch lock保护free。
16既有provider/clock/core/NoC/managed/lifecycle回归通过；完成专项37场景ASan/UBSan覆盖ABI/maps、握手/初始化失败、快/旧/外来/部分IRQ、EOI/RWP、邻位、stop并发/迟到。
新增 audit-mh2lm-lineage-rsc.py 动态执行Lineage实际计时和五个init函数，65项逐项逐序一致；默认60fps/vtotal505/prefill25限制成35。
staticWake298299/threshold17938/slot0=35771/slot1=320512/backoff17886/PDC186020，保留源码单位和整数运算顺序。
真实ARM64 SweetMDSS.cpp/sleh.c编译通过，C未解析回调与C++导出符号匹配；MDSS warnings1561→1573（12固定表/数组unsafe-buffer），sleh1条既有告警。
0185–0216共32patch严格重放/78源码字节一致/114原CI markers通过；ADT全组合/CLI通过；实际旧a27被matrix拒绝且无boot输出。
改动源文件为MDSS.cpp/NoC.inc/sleh.c；0216补丁SHA=403fbaea29ca77d277052a075650387e7947f0e3fcc3eec1b1f2517222f10eb2。
**NEXT：用户git push origin main并云编译，下载kernel/buildtree（可继续编号0121a28），agent核对后重新打retained。**
硬件档ADT增加sweet,display-noc-irq-abi=1和sweet,display-noc-intid=161，复用map11；必须重生成ADT，不能照搬a27。
ABI608固件和605字节retained参数保持，identity档无NoC声明不变；matrix新增四个RSC/IRQ marker。当前没有新boot镜像。
下一轮USB顺序：CLOCK Held→enter CLK→v2 initialized→TCS ownership ready→INTID161 configured→每组fresh ACTIVE→NoC ACK→新DMA/transfer/HANDOFF。
POLL日志名保留兼容，但现在只记录IRQ后的完成检查；没有IRQ时samples0/status-valid0是未采样，不是硬件零。
本轮只对齐已上电/持钟的当前CLK/ACTIVE资源接管；mode2 collapsed/失电仍拒绝，无完整CMD/VID/IDLE与VSYNC/sleep-wake管理。
后续retained PHY/host重建仍与Lineage continuous splash不同；MDP/SMMU独立配置也未补。本地成功不等于真实完整接管或整套驱动等价。
完整接管前亮度/OFFON/长测/combined/supply实机测试继续暂停；不必补a27持久日志。
详见docs/PANEL-RSC-TAKEOVER.md与更新的流程对照；scratch/framebuffer-0216保存编译/重放/测试底稿。
未push/触发CI/操作设备；未改固件/bootargs/其他provider/CI。CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
以下CURRENT/NEXT及“本轮仅文档”均为历史，以本段为准。

## Resource takeover missing-step review (2026-11-13)

用户追问当前接管流程缺项，再次核对Lineage RSC v2寄存器定义/状态机/带宽触发/IRQ与实际0215源码。
新发现：NoC的fNoCRSC+0xd0是ERROR_IRQ_STATUS，代码变量/日志solver-status命名错误；Lineage和LG原厂头文件及debug_show一致。
真正SOLVER_STATUS0/1/2在+c24/+c28/+c2c，override在+c14，当前NoC未读取；不能再说+d0=0证明solver已空闲。
仍有wrapper override bit0/power bit12/SEQ_BUSY保护，不能说没有任何模式相关检查。已纠正对照文档和旧准入诊断说明。
明确缺项：display INTID161与本次可靠完成通道；TCS_USE_OK/SEQ_BUSY硬件交接；完整RSC工作状态确认/转换。
完整RSC计时/序列/solver/PDC初始化和sleep/wake低功耗表亦未实现，但未证明这些全部为当前warm ACTIVE所必需。
payload/WAIT/ENABLE/AMC/TRIGGER基本提交步骤存在；成功后撤AMC是差异，尚未执行，非a27首组超时直接原因。
本轮仅复核/文档，无生产改动；旧solver-status标签及真实solver采样尚未修正，0215也尚未补这些流程能力。
详见PANEL-LINEAGE-PATH-COMPARISON.md新增缺项表；原0215代码2ffc1a4/a28云编译交接保持，无额外编译/设备操作。

## Lineage flow audit (2026-11-13, no new driver changes)

用户要求对比LG G8X逻辑流程/硬件控制，已对Lineage@2e897dd的MH2LM公共PV配置、DRM/DSI/LG override、RSC v2、RPMh/mailbox/bus调用链审计。
详见docs/PANEL-LINEAGE-PATH-COMPARISON.md。两边同DSI0/PHY0/R66456A PV与RSC硬件；Lineage也有continuous splash，不能说只有XNU依赖ABL。
主要缺口：Lineage probe初始化RSC计时/wrapper/seq/solver/PDC，显式CLK/CMD/VID/IDLE切换与VSYNC协调；XNU仅读继承状态，无对应初始化/模式交接。
Lineage带宽更新有TCS_USE_OK/SEQ_BUSY握手；XNU只查当时idle。不能把几项准入读数当完整硬件交接，也不能认定所有solver/PDC初始化都为ACTIVE必需。
Lineage非solver走ACTIVE；solver状态实际check_ctrlr_state拒ACTIVE/awake，bus对display EBUSY特判、继续维护sleep/wake并配合flush。
rpmh_mode_solver_set的上方注释与实际拒绝条件相矛盾，按代码分支判断；不能笼统说Lineage任意状态都向TCS2发同样命令。
display INTID161与apps37不同；Lineage先handler再ACTIVE source enable，XNU本槽屏蔽+timer/fresh判据仍是当前直接差异，真实IRQ_STATUS门控未知。
Lineage ACTIVE成功清理保留AMC，XNU撤AMC；a27首组尚未成功，此后续差异不能解释当前首错。
RSC clock表顺序Lineage VSYNC→NON_GDSC→AHB，XNU common AHB→NON_GDSC→AHB→VSYNC；a26后两路已继承ON，a27 mask381 Held，不能仅凭顺序定因。
retained参数link2/host1：XNU温接管后计划重建PHY/host而保留panel；Lineage continuous splash跳过PHY reset/hw enable/面板ON。
这段XNU尚未走到，非a27首错；MDP/SMMU仍固定继承管线/CB2 bypass和物理源，与完整DRM地址空间/管线配置不同。
LG LABIBB文本所调power_info在本profile只有VDDI/VPNL，不能据名字推断漏独立偏压外设；共享L3/L5最后撤票及板级偏压仍有边界。
排查优先级是明确非solver提交状态/使用权交接/完成通知，再到PHY/host/新DMA；不是只补一个IRQ就宣称与Lineage等价。
本轮仅审计文档，无生产改动/新测试/额外编译要求；0215代码2ffc1a4与下面a28云编译交接保持，未push/操作设备。

## Agent handoff / active work (2026-11-13, a27 clock Held; 0215 NoC evidence ready)

**CURRENT：a27已实测越过branch7，首错推进到NoC ACTIVE2执行等待超时；0215已提交2ffc1a4，本地通过，待用户push云编译。**
日志live-0121a27-abi608-panel-retained.txt为220191bytes/3044行，SHA=f4c689092c5e8ab227069017b0768675341d37b2fe09155b6e3f9548553219c7。
10次apps fresh IRQ、四LDO/ARC/CN0和core2成功；1746voted halt complete branch7/cbcr0/request-issued1。
1747CLOCK token2/state2/mask381/owned80，RSC lease已Held；0214额外enable门槛修正已实机越过，读零硬件原因仍未解释。
1763NOC phase3/cursor0of5/group1/tcs2/executing1/op0of0/posted0/register-token7/acked1；1764Timeout e00002d6/ACK0/uncertain1。
1767明确no new DMA，1769/1772生命周期/HANDOFF连锁失败；后续用户态/电池/USB继续，无kernel panic签名。
executing只表示软件已写trigger；无TCS样本/时间，不能区分100ms组与1.5s整体期限，不能认定硬件拒票或丢IRQ。
Lineage@2e897dd的drivers/mailbox/qcom-rpmh-mailbox.c：ACTIVE IRQ在trigger前启用，handler核对ISSUED/COMPLETE。
DTS disp_rsc SPI129=INTID161，ACTIVE2/3；apps INTID37修复不能代替display中断接入。
XNU先屏蔽本槽IRQ，等待irq或曾见busy/不完整；如果IRQ_STATUS受mask影响且快速完成，可能缺fresh证据。
模拟已复现该条件，但实际IRQ_STATUS门控/本轮根因尚未证明；不直接启用未接handler的IRQ或放宽完成判据。
0215仅改MDSS.cpp/NoC.inc，新增请求/组/trigger时间、原轮询首末状态、命令计划与原写读回缓存。
trigger后及正常准入通过的group-timeout各额外四读IRQ_ENABLE/CONTROL/ENABLE/WAIT；overall/权限丢失/stop不新增MMIO。
日志只读缓存，valid0不是硬件0，组cursor标明缓存归属；最多10条新ERROR，成功/Aborted不输出，原期限/完成/写入/保票保持。
22真实provider场景ASan/UBSan通过，与实际a27基线仅差38组四读，其余MMIO/结果/账本/timer/模拟时间一致。
16套既有回归通过，共17套；真实ARM64对象通过，warnings1553→1561（8个有界数组unsafe-buffer-usage，未屏蔽）。
0185–0215共31patch严格重放/78源码字节一致/114原CI markers通过；新matrix五marker门槛实际拒旧a27，无boot输出。
补丁SHA=454174e71caf1631373c6deac1ce412e955e771cbb80ff2f5219479cddd40351。
**NEXT：用户git push origin main后云编译，下载新kernel/buildtree（建议0121a28），agent核对后打原ABI608 retained。**
预计比a27仅MDSS.cpp/NoC.inc两文件变化、191 binary markers；原固件/ADT/605字节参数保持。现在无0215新boot。
沿用fastboot boot，启动约90秒完整USB，先CLOCK Held再NoC EVIDENCE/REGISTERS/POLL/READBACK/COMMAND与CONTEXT。
按group/overall/readback出口及idle/required/fresh证据分流；若成功再追新DMA/transfer/HANDOFF，不用branch成功替代接管验收。
a27 USB足够，无需补持久日志/重测；完整接管前亮度/OFFON/长测/其他显示档继续暂停。
详见PANEL-D1-0121A27.md实测与PANEL-NOC-COMPLETION-EVIDENCE.md；scratch/framebuffer-0215验证底稿、a27分析JSON。
原始日志/内核/Apple二进制未入git。未push/触发CI/操作设备，未改固件/ADT/参数/其他provider/CI。
CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。以下NEXT为历史，以本段为准。


## Agent handoff / active work (2026-11-13, 0121a27 voted halt completion boot ready)

**CURRENT：用户下载的0121a27已核对，0214在新kernel内，retained启动镜像打包/回读通过，待实机日志。**
78源码字节匹配scratch/framebuffer-0214/verify，内容对应55cab80；比a26仅MDSS.cpp/Clock.inc/NoC.inc/Link.inc/Lifecycle.inc变化。
114原CI source/186 binary markers通过，包含新voted halt complete及原所有CLOCK取证。
内核17757712bytes/ARM64 subtype0，自带SHA匹配，SHA=fccfc817949806d84dc986c19f2f59a0ed545b668781597be478d49e74573f5d。
无独立CI run ID/完整build log；仅验证下载内容，不声称云端提交元数据或全量告警核对。
ABI608固件/BootShim/两个追加DTB/各档ADT与参数保持a26，RPMh INTID37扩展保留；retained605/607字节。
四档boot header/FAT内kernel+ADT+TOML回读通过，只交付retained测试，无需再次编译。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a27-abi608-panel-retained.img。**
镜像35545088bytes，SHA=db97076fb455f2a6f7d62e82b20918bff4efe7bf26575241692c6a0ddc2f73be。
先开USB接收脚本，启动约90秒保存logs/live-0121a27-abi608-panel-retained.txt；双终端命令见PANEL-D1-0121A27.md。
先voted halt complete branch7→CLOCK Held/state2/mask381→NoC本轮完成或新首错→新DMA/HANDOFF。
分支cbcr0/request-issued1可以成功；issued只证明软件已发出请求，不是硬件ACK或DMA验收。成功时无超时LOCAL ERROR属预期。
本版修正自身voted开启判据，同步消费者；继承票/普通HALT/父权限/撤票检查保持。尚未证明实机完整接管。
本轮没有驱动改动，未重复0214已有16套回归/4基线复现/34父链故障/ARM64对象/累计重放，原验证保持有效。
完整接管前亮度/OFFON/长测和其他显示档继续暂停，a26不用重测或补日志；本次USB不足时才补本次持久日志。
底稿scratch/artifact-0121a27/两验证脚本/JSON/输出和packaging.txt；原始日志与二进制未入git。
未改固件/CI/参数，未push/触发CI/操作设备；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
以下NEXT为历史，以本段为准。


## Agent handoff / active work (2026-11-13, a26 analyzed; 0214 voted halt completion ready)

**CURRENT：a26 USB证据完整；0214修正额外enable完成门槛，已提交55cab80并本地通过，待用户push云编译。**
日志live-0121a26-abi608-panel-retained.txt为224475bytes/3115行，SHA=f03f15d930e0d2264d47535c5ee27be86960aba6af66d2e3512c504280143c32。
10次apps fresh IRQ/四LDO/ARC/CN0与core2成功；1711首错branch-wait/+4004；1713CBCR0/enable0/OFF0/current1。
立即读回PAaf04004/CBCR0；42次等待未见enable/OFF置位。整体期限余188.321354ms，write记时至立即样本677ns、至最终样本10.044584ms。
1717warm-entry：BCR0/NON_GDSC0/RSCC_AHB1/RSCC_VSYNC1/AHB CMD0 CFG1；1718pre-write除CFG40f外相同；1719timeout BCR0。
未支持reset一直未释放；branch7读零在core转换前已存在，未观察到core转换关掉该分支；父源按计划切换。
三个采样时刻不排除中间瞬变/未知依赖；OFF0符合Linux halt，直接拒绝为XNU额外enable条件，硬件读零原因仍未解释。
1720–1730clock→NoC/managed/lifecycle/HANDOFF连锁失败，phase2/executing0，明确未提交新DMA；后续用户态活动，无kernel panic签名。
0214基于实际a26：新issuedMask只在enable写+屏障返回后记账，区别于写前owned账本；自身voted按issued+OFF0完成，bit0仅诊断。
统一Clock准入/worker/final、NoC、Link/Lifecycle；继承票与普通HALT仍要enable，BOOT mask7f检查保持。
共享retain不重写，最后引用释放仍clear读回+异步10us settle，完成清issued、重新获取需再写；故障保票/stop无MMIO保持。
父源/core/ARC/CN0/idle/域/期限/Held准入不变，无复位或顺序修改。新日志voted halt complete仅表示分支等待完成，不是DMA成功。
18场景新专项+15套既有回归共16套通过；4个a26基线读零场景复现Timeout，修改版推进到模拟managed scanout。
新增持续读零的完整mode4 PHY关/开生命周期回归通过；34项写前/写后父链/域/权限故障仍拒绝，无多余leaf/TCS写。
真实ARM64对象通过，warnings1552→1553（新增1固定表索引unsafe-buffer-usage，未屏蔽）；0185–0214共30patch严格重放/78源码/114原CI markers通过。
matrix新增voted halt complete门槛，实际旧a26被拒绝且无boot输出；补丁SHA=7ec98ca1d2015cb12719b436582d4cd0f53256b0b2897a0c3fc9a76cdd3ef25f。
**NEXT：用户git push origin main并云编译，下载新kernel/buildtree（建议0121a27），agent核对后打原ABI608 retained。**
预计比a26仅MDSS.cpp/Clock.inc/NoC.inc/Link.inc/Lifecycle.inc五文件变化，186 binary markers；原固件/ADT/605字节参数保持。
启动约90秒保存完整USB：先voted halt complete→CLOCK Held/state2/mask381→NoC本轮完成→新DMA/HANDOFF；若失败按新首错定位。
尚未证明实机修复，当前无新boot；a26无需重测或补持久日志。完整接管前亮度/OFFON/长测和其他显示档继续暂停。
详见PANEL-D1-0121A26.md实测及PANEL-VOTED-HALT-COMPLETION.md；底稿scratch/framebuffer-0214/验证脚本/JSON/输出，a26日志分析JSON。
未push/触发CI/操作设备，未改固件/CI/参数/USB/输入/电池/apps IRQ；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
以下NEXT为历史，以本段为准。


## Agent handoff / active work (2026-11-13, 0121a26 local/reset evidence boot ready)

**CURRENT：用户下载的0121a26已确认包含0213，retained启动镜像打包/回读通过，待实机USB日志。**
78份源码匹配scratch/framebuffer-0213/verify，内容对应7f4f55a；比a25仅MDSS.cpp/Clock.inc/Core.inc变化。
114原CI source/185 binary markers通过，包含新增CLOCK LOCAL/RESET及原六条诊断。
内核17741200bytes/ARM64 subtype0，自带SHA匹配，SHA=f0af1e7c2bc22e5a852b448bdf986a4e2fbfe84e278bdcbf647cfad86ff0a0ee。
无独立CI run ID/完整build log；仅验证下载内容，不声称云端提交元数据或全量告警已核对。
ABI608固件/BootShim/两个追加DTB/各档ADT和参数保持a25，RPMh INTID37扩展保留；retained605/607字节。
四档boot header/FAT内kernel+ADT+TOML回读通过，只交付retained测试，无需再次编译。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a26-abi608-panel-retained.img。**
镜像35545088bytes，SHA=ab69d8473d75dfebd60d8e675422e03dc59a37d2d5c8c53f551751c8615e01d1。
先开USB接收脚本，启动约90秒，保存logs/live-0121a26-abi608-panel-retained.txt；双终端命令见PANEL-D1-0121A26.md。
若仍超时，取原六条及LOCAL warm-entry/pre-write、RESET；比较BCR/三路CBCR/AHB CMD+CFG及时间。
LOCAL在阶段入口采集、超时时输出，按序非原子；valid0不是硬件零，RESET仅BCR，overall SAMPLE可能历史。
若CLOCK Held/state2/mask381，则追后续NoC/新DMA/HANDOFF；成功时没有超时LOCAL ERROR属预期。
本版保留enable判据，仍可能同处失败。a25 OFF0已满足Linux halt，不能称已证实的硬件握手失败。
尚未证明修复/完整接管；完整接管前亮度/OFFON/长测及其他显示档暂停，a25不用重测或补持久日志。
本轮没有驱动改动，未重复0213已有27场景/15套回归/ARM64编译/累计重放，原验证保持有效。
底稿scratch/artifact-0121a26/两验证脚本/JSON/输出和packaging.txt；原始日志与二进制未入git。
未改固件/CI/参数，未push/触发CI/操作设备。CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
以下NEXT为历史，以本段为准。


## Agent handoff / active work (2026-11-13, a25 analyzed; 0213 local reset evidence ready)

**CURRENT：a25 USB证据完整，立即及39次等待均未见enable；0213已提交7f4f55a并本地验证，待用户push云编译。**
最新外部分析核对：a25 OFF=0已满足Lineage开启halt判据；直接阻塞为XNU额外enable1条件，不能称硬件握手未返回。
BRANCH_VOTED是软件bit7策略，enable仍本地RMW并验halt，disable延时10us，不是等voted位或改走RPMh。
动态提取LG/Lineage实际leaf enable/toggle/wait/disable/is_enabled，字节一致，24场景ASan/UBSan通过。
新增tools/tests/audit-mh2lm-lineage-branch.py；不含CCF父递归/真实总线，不是新实机结果。
外部分析地址需纠正：AHB CMD/CFG=af022bc/af022c0，MDSS GDSC=af03000；af06000是SPDM BCR。
BCR=af04000/bit0，但a25无样本，reset最可能/必然使写失效均未证实。不能照抄外部地址或复位建议。
enable门槛覆盖Clock worker/final、NoC和Link/Lifecycle knownMask；BOOT localClocksReady仅mask7f不含branch7。
已审查单点删门槛不足：需连同请求/完成租约、继承票、共享引用及撤回语义评审；额外门槛是本地策略，非Linux要求。
本轮保留0213取证选择，无新生产补丁；需要的是已经交付的同一次云编译。详细核对见PANEL-BRANCH7-PARENT-HALT-AUDIT.md。
用户询问面板型号/手册；已复核docs/panel.md与Lineage@2e897dd（7份相关文件与LG字节一致）。
实机既有ABL source2/profile4对应TIANMAMD PV、1080x2340@60；DTS明确DDIC r66456a，软件名R66456 MD cmd mode dsi panel。
完整模组料号和物理DDIC读回尚无；旧文档Renesas品牌归属本次未独立确认。当前+0xaf04004属于SM8150 DISPCC，不是面板DSI寄存器。
手册优先找SM8150/msmnile NON_GDSC_AHB_CBCR与RSCC_BCR语义，其次R66456A/R66456及天马模组规格。
详见docs/PANEL-HARDWARE-MANUALS.md；型号核对未改生产代码，本轮仅新增leaf离线审计，0213交接保持。
logs/live-0121a25-abi608-panel-retained.txt为301264bytes/3942行，SHA=de38a819d1df09802a7bb292b16f356a4dd3949f889164ffd3c9679d41289fcb。
1752–1769十次fresh apps IRQ/四LDO/ARC/CN0完成；1770core2成功，1771CN0 token7，1772branch7请求。
1773首错branch-wait/+4004；1774WRITE before0/want1；1775SAMPLE valid1/current1/CBCR0/enable0/OFF0/CFG40f/source4。
1776READBACK valid1/PA0x0af04004/CBCR0；1777OBSERVED wait-samples39/first0/seen-enable0/seen-off0；
1778RSC PREFLIGHT valid1/non-gdsc0/rscc-ahb1/rscc-vsync1（历史准入，非失败现场的新读值）。
写后记时至立即样本729ns，至最终样本10.043490ms；整体期限余188.273334ms，wait-late65364ns。
没有观察到enable置位再丢失；不能排除采样间短暂置位。PA匹配不证明总线写权限或实际物理时钟状态。
1779–1788clock→NoC→managed/lifecycle/HANDOFF失败；仍phase2/executing0，明确未提交新DMA；后续用户态继续，无kernel panic签名。
实际a25内联store/DSB/同地址load已核对；ClockDxe PE表及14个callback指令校验通过。
三路静态表alternate vote地址均0，enable/is-enabled callback用CBCR bit0；未发现另一套应写的vote地址。
LG RSCC_BCR与ClockDxe reset地址均+4000/bit0，a25没有BCR样本，不能直接归因或操作复位。
重要边界：LG开启等待只验halt，XNU额外要求enable1。CBCR0满足LG halt，当前拒绝不等于物理时钟已证实停止；
但LG/固件is-enabled仍读bit0，未找到只写/自清依据，也不能据此盲删自身票验证。
0213仅改MDSS.cpp/Clock.inc/Core.inc：warm入口/branch7写前各六项读取（BCR/三CBCR/AHB CMD+CFG），超时仅再读BCR。
新增LOCAL warm-entry/pre-write及RESET三条，valid/时间明确，按序采样非原子；原SAMPLE overall仍为历史值。
原写入/完成条件/期限/失败保票不变，cold无warm采样，release/其他分支/无写入/访问丢失/stop不触发新超时读取。
27真实provider场景ASan/UBSan通过；与0212基线仅差44组六读及16次BCR（全部场景合计），其余MMIO/结果/所有权/timer/时间相同。
15套回归通过；真实ARM64对象编译告警1552→1552；0185–0213严格重放/78源码字节比较/114原CI markers通过。
matrix增加LOCAL/RESET二门槛，旧a25实际被拒绝且无boot输出。补丁SHA=06bcc6dce23f4b376ea40422655c7894ee56cf301f1d328d054414bf28786083。
**NEXT：用户git push origin main并云编译，下载新kernel/buildtree（建议0121a26），agent核对后打原ABI608 retained。**
新产物应比a25仅上述三文件变化，预计185 binary markers；原固件/ADT/605字节参数保持。当前没有新boot镜像。
启动约90秒取完整USB，保留原六条和新LOCAL/RESET。a25无需重测或补持久日志；仍未证明硬件修复。
完整接管前亮度/OFFON/长测和其他显示档继续暂停。详见docs/PANEL-RSC-LOCAL-RESET-EVIDENCE.md及PANEL-D1-0121A25.md实测。
底稿scratch/artifact-0121a25/live-retained-log-analysis.json、rsc-write-path-audit.json及实际内核/固件反汇编；原始日志/二进制未入git。
代码底稿scratch/framebuffer-0213/各验证JSON、编译/回归/基线轨迹比较、拒旧日志及base/edited/verify/replay源码。
未改固件/ADT/参数/CI/USB/输入/电池/apps IRQ，未push/触发CI/操作设备。CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
以下NEXT为历史，以本段为准。


## Agent handoff / active work (2026-11-13, 0121a25 write-readback boot ready)

**CURRENT：用户下载的0121a25已确认包含0212，retained镜像打包/回读通过，待实机USB日志。**
78源码字节匹配scratch/framebuffer-0212/verify，代码内容对应25d8fbc；比a24仅MDSS.cpp/Clock.inc变化。
114原CI source/183 binary markers通过，新增CLOCK READBACK/OBSERVED/RSC PREFLIGHT，原三条0211记录保留。
内核17740976bytes/ARM64 subtype0，自带SHA匹配，SHA=7a487e640d37213cbf5fb92eda4f1c3d43101150ffec9cfe808def81a24fa612。
无CI run ID/完整build log，不声称核对云端提交元数据或全量告警。原0212本地验证记录保持，本轮未重复驱动测试。
ABI608固件/BootShim/两个append DTB/各档ADT和启动参数保持a24，RPMh INTID37扩展保留；retained605/607字节。
四档boot header/FAT中kernel+ADT+TOML回读通过，只交付retained；无需再次云编译。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a25-abi608-panel-retained.img。**
镜像35545088bytes，SHA=a2d95471e8aac5a90aa87eadf5a4388613d3856d31286fc49329f5d0014e5b71。
先启动USB接收脚本，启动约90秒，保存logs/live-0121a25-abi608-panel-retained.txt；具体双终端命令见交付文档。
若仍Timeout取TIMEOUT/WRITE/SAMPLE及READBACK/OBSERVED/RSC PREFLIGHT；区分写后enable1再丢失、等待中才出现、均未观察到。
READBACK PA预期0x0af04004，正确翻译不证明总线权限；OBSERVED不含立即读回，valid0/样本0不能当硬件值。
RSC PREFLIGHT为历史准入样本；若CLOCK Held/state2/mask381，则追NoC首错或新DMA。
仍为定位版，未证明硬件修复/完整接管；亮度/OFFON/长测/其他显示档暂停，旧a24无需补持久日志或重测。
详见docs/PANEL-D1-0121A25.md、PANEL-CLOCK-WRITE-READBACK.md；底稿scratch/artifact-0121a25/两验证脚本、JSON及输出。
本轮仅产物核对/打包/文档，未改驱动/固件/CI，未push/触发CI/操作设备。无需为本轮文档提交重编译。
CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。以下NEXT为历史，以本段为准。


## Agent handoff / active work (2026-11-13, a24 enable absent; 0212 ready)

**CURRENT：a24 USB日志足够，首错为branch-wait且自身enable未读回；0212已提交`25d8fbc`，待用户push云编译。**
日志222873bytes/3099行，SHA=df160b8bed570227a7c3b8dfc3356362c86439a549fd764ffea594fbaf954abb。
1727–1744十次fresh IRQ/四LDO/ARC/CN0确认，1745core2成功，1746CN0 token7成功。
1748 TIMEOUT reason=branch-wait/+4004/wait-late103906ns；1749 WRITE before0/want1；
1750 SAMPLE valid1/current1/cbcr0/enable0/OFF0/parent-valid1/CFG40f/source4。
写后记时到采样10.081510ms，整体期限余187.727396ms；不是overall超时，也非等待OFF清除。
后续NoC/managed/lifecycle失败及pixels冻结为连锁结果，尚未display TCS/新DMA，无kernel panic签名。
静态复核：v1 DISPCC PAaf00000/长度20000，LG/Lineage/v2定义及本机ClockDxe三路CBCR地址表一致；
实际内核dp_wr具备dmb oshst/str w2/dsb sy。未证明实时翻译/写入接受/是否被其他主体清除。
0212仅在branch7实际写后读同一CBCR一次、记录ml_vtophys，并汇总原等待及RSC准入样本。
Timeout增加READBACK/OBSERVED/RSC PREFLIGHT，字段不参与控制；原期限/ready/失败保票/写入序列保持。
新专项19场景ASan/UBSan，与0211仅差允许的写后读取，其余MMIO/结果/所有权/timer/模拟时间一致。
14套回归通过；真实ARM64 MDSS前后对象通过，warnings1549→1552（三数组提示未屏蔽）；
0185–0212严格累计重放/78源码字节比较/114原CI source markers通过，旧a24被READBACK门槛拒绝且无boot输出。
补丁SHA=58c3076bb016b038ba7f4001f42f5735332b6898eadcba912233334e7a9ed326。
**NEXT：用户`git push origin main`并云编译，下载新kernel/buildtree（建议0121a25）后agent核对并打原ABI608 retained。**
启动约90秒取完整USB日志，区分立即读回1后丢失、延后出现后丢失、各次样本均未见enable；
READBACK PA预期af04004，PA0/异常需查映射；正确PA仍不证明总线写入权限。RSC PREFLIGHT只是原准入值。
这仍是定位补丁，尚未修复/证明接管；a24无需补持久日志或重测，其他显示档/亮度/OFFON/长测继续暂停。
详见docs/PANEL-CLOCK-WRITE-READBACK.md及PANEL-D1-0121A24.md实测节。
底稿scratch/framebuffer-0212/与scratch/artifact-0121a24/live-retained-log-analysis.json。
未改CircleCI/固件/ADT/参数/USB/输入/电池/apps RPMh IRQ；未push/触发CI/操作设备，无新镜像。
CircleCI blob保持da0b6c0947a66f03491b3a9023590b49665292df。以下NEXT为历史。


## Agent handoff / active work (2026-11-13, 0121a24 timeout diagnostics boot ready)

**CURRENT：用户下载的0121a24已核对，0211在新kernel内；retained镜像回读通过，待实机。**
78源码字节匹配0211验证树，代码内容对应7d34942；比a23仅SweetMDSS.cpp/Clock.inc变化。
114原CI source/180 binary markers通过，包含CLOCK TIMEOUT/WRITE/SAMPLE三条。
内核17740976bytes/ARM64 subtype0，自带SHA匹配，SHA=ce771b8a2b4751971f592f1b9a39d16b7f248c970ebd4c5f9bb49fb2f50029a5。
无CI run ID/完整build log，不声称核对云端提交元数据或全量警告。
ABI608固件/BootShim/两个append DTB/各档ADT及参数保持a23，INTID37扩展保留；retained605/607字节。
四档boot/FAT内kernel+ADT+TOML回读通过，只交付retained；本轮无需再次云编译。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a24-abi608-panel-retained.img**
镜像35545088bytes，SHA=0c6fb213855559b4da7fe7ea95e2359b9b38b9a4b06f43a2b4b237c1f9073126。
先开USB等待接收脚本，启动后约90秒存live-0121a24-abi608-panel-retained.txt或同名sweetlog bin。
重点首条CLOCK TIMEOUT/WRITE/SAMPLE：overall或branch-wait、valid/current/enable/OFF及时间。
current0不是当前CBCR读值，before/want不是写后读回；Held/state2/mask381后再追NoC首错/新DMA。
诊断版未证明修复；完整接管前亮度/OFFON/长测/反复USB/combined/supply暂停，旧a23/identity不重测。
详见docs/PANEL-D1-0121A24.md；底稿scratch/artifact-0121a24/两verify脚本及JSON/packaging日志。
本轮仅产物核对/打包/文档，未改驱动/固件/CI、未push/触发CI/操作设备。
CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df；以下NEXT为历史。


## Agent handoff / active work (2026-11-13, 0211 cloud build requested)

**CURRENT：用户确认 agent 改代码/本地验证，用户负责 push 云编译和下载产物；现在需要编译已提交的0211。**
本轮复核实际Clock worker、两个Timeout出口、日志字段、打包门槛和父链/halt审计。
生产补丁仍为`7d34942`的0211，父链审计为`fb9ad82`；没有追加猜测性硬件修正。
核对补丁SHA仍为be780786c6bb91b6003fb06978297c81d76376fa651f9c01a1b954bc6f479b47，
两份修改源码edited/verify字节一致，打包入口包含TIMEOUT/WRITE/SAMPLE三项新binary marker，bash语法通过。
既有13套测试、14场景与0210基线MMIO/结果/timer一致及ARM64编译记录已核对；本轮未重复跑这些测试。
a23仍是本地最新实机样本，branch7使能后Timeout原因未定，未Held、未进入display TCS/新DMA。
**NEXT：用户在本仓库执行`git push origin main`，由CircleCI编译；不能直接`git push`到跟踪的upstream。**
下载`xnu-kernel`与`xnu-buildtree`到工作区logs，建议新标签0121a24：
`logs/xnu-kernel-0121a24.gz`、`logs/xnu-buildtree-0121a24.gz`；若有build log一并保留。
产物到齐后agent核对实际0211源码/新markers/SHA，再沿原ABI608固件/ADT/605字节参数打retained。
用户启动约90秒取完整日志，先区分enable读回、CLK_OFF、overall/branch-wait与采样时间，再决定修正。
这次编译用于取得缺失的硬件现场，不代表超时已修；旧a23与其他显示档继续不重测。
本轮仅更新交接和总计划；无新boot，未push/触发CI/操作设备。CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
以下NEXT为历史，以本段为准。


## Agent handoff / active work (2026-11-12, branch7 parent/halt audit)

**CURRENT：按用户要求核对branch7父链和halt；本次只补审计文档，没有新内核补丁。0211仍待用户push云编译/新日志。**
LG与lge-lineage-kernel定义一致：branch7 NON_GDSC_AHB、enable/halt+4004、bit0使能、branch2_ops固定bit31 OFF。
BRANCH_VOTED为软件枚举bit7，开启仍检查halt、关闭仅10us delay；默认halt_bit0不适用branch2_ops，不得错用bit0判断停止。
提取LG真实halt函数与Lineage字节比较，64组OFF/FSM/enable/无关位ASan/UBSan通过；FSM2不能放行OFF1。
父链XO19.2→PLL1 main600→AHB root22bc/CFG22c0/source4/HID15(/8)→branch7；source5才是even。
实际corePLLReady/coreReady每次worker前读PLL1 LOCK/OPMODE/main output/L/alpha、root OFF/UPDATE/CFG、域和bus；
写branch7前/等待又复核父快照与typed ARC/CN0。不是只查频率日志或软件ready，a23 Timeout不能直接归为父PLL完全未开。
17项父/域/bus/typed故障分别写前/写后共34场景真实provider ASan/UBSan通过：NotReady/Busy，不是Timeout，无新分支写/TCS。
发现但未定因的参考差异：LG AHB enable_safe_config经force-root/配置/clear；我们core受控CFG/UPDATE后要求rootON；
LG DTS+解析+enable循环顺序VSYNC→NON_GDSC_AHB→RSCC_AHB，我们branch编号顺序7→8→9。
它们不是已证明的电气依赖，不据此永久force或盲改时钟顺序；下一份0211样本先判enable/OFF/期限再收敛。
**NEXT：沿原0211交付等待新kernel/buildtree并核对打retained；NoC/HANDOFF连锁不单独处理。**
本轮不增加编译/镜像/旧a23测试，完整接管前其他显示测试继续暂停。
详见docs/PANEL-BRANCH7-PARENT-HALT-AUDIT.md；底稿scratch/branch7-parent-audit/check.py/源文件/结果/report.json。
未改CircleCI/固件/ADT/参数/生产内核，未push/触发CI/操作设备。CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
以下NEXT为历史；0211代码7d34942及交接b4f4169保持。


## Agent handoff / active work (2026-11-12, 0211 timeout evidence and root cause review ready)

**CURRENT：0211代码/专项/实现和归因文档已提交`7d34942`，本地验证完成，待用户push云编译。**
a23首错仍是branch7 clock token2 Timeout；未Held、未display TCS/新DMA，具体原因仍未知。
0211只改实际MDSS.cpp/Clock.inc：每次retain/release清诊断缓存；记录原CBCR/parent和实际RMW before/want/时间。
两个原超时出口各输出TIMEOUT/WRITE/SAMPLE三条ERROR，区分overall/branch-wait、now/deadline/late、
valid/current/样本分支/enable/OFF及缓存年龄；overall无本次CBCR时current0，不把历史值当当前。
logger只读软件，不加MMIO/provider/timer；字段不参与控制。原10ms/200ms/100us、ready/timeout顺序和失败保票不变。
新专项14场景ASan/UBSan通过，与0210完整MMIO轨迹、结果、所有权、timer状态/模拟时间完全一致。
另12套显示/NoC/RSC/生命周期回归通过，共13套；实际ARM64 MDSS前后对象编译通过，warnings1547→1549，
新增2条unsafe-buffer-usage未屏蔽、无新增格式告警；0185–0211严格累计apply无offset/fuzz，78源码字节比较，114原CI markers通过。
matrix加三新binary marker，bash语法/实际旧a23拒绝通过，未生成boot。patch SHA=be780786c6bb91b6003fb06978297c81d76376fa651f9c01a1b954bc6f479b47。
**NEXT：用户自行push/云编译，下载新kernel/buildtree后核对0211，沿原ABI608/ADT/605字节参数打retained。**
只启动约90秒取完整USB或持久日志，区分enable未读回/OFF未清/overall期限与晚回调。不是硬件修复版，不能宣称已通过。
Held/mask381后再沿NoC wrapper/solver/TCS追首错或新DMA；不延长期限/假ACK/回退ABL，不盲改硬件。
完整接管前亮度/OFFON/长测/反复USB/combined/supply暂停，旧a23/identity不重测。无新镜像可刷。
用户要求归因已写docs/PANEL-BRINGUP-ROOT-CAUSES.md：ABI错位/HFS溢出、XO旁路、ACV开关模型、触摸worker时序、
apps完成IRQ、RSC voted/父权限与当前未知Timeout；区分已确认/实机越过/连锁结果/旧bug，承认诊断粒度不足增加轮次。
详见docs/PANEL-CLOCK-TIMEOUT-DIAGNOSTICS.md；底稿scratch/framebuffer-0211/各JSON/编译测试和拒旧日志。
未改CircleCI/固件/ADT/boot args/apps RPMh IRQ/USB/按键/触摸/电池/mux，未push/触发CI/操作设备。
CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。以下NEXT为历史。


## Agent handoff / active work (2026-11-12, 0121a23 branch7 completion timeout)

**CURRENT：a23屏幕静帧，已分析日志；a22准入拒绝已越过，branch7开启等待超时，尚未Held。**
日志218900bytes/3118行，SHA=6ac16167b9ccdcf67606d53a139c1fbe402f1178ad3d599b1b3591bdd42c0c5c。
1757–1775 10次fresh IRQ/四LDO/ARC/CN0，1776core2/AHB75/MDP460成功，1777CN0 token7成功。
1778 voted branch7 enable1说明RMW发出，不是enable读回或ON证明；1779首错clock token2 Timeout0xe00002d6/owned80/uncertain1。
1780 clock-status token2/state5/resultTimeout；1781 phase2/clock-token2/register-token7/register-acked1/cursor0/5/
executing0/op0/0/clock-cursor7/waiting1/clock-fault1。仍在时钟阶段，未进入display TCS或新DMA。
后续NoC NotReady是失败转换，managed/HANDOFF冻结及2923按键后lifecycle拒绝是连锁结果。
clock整体200ms超时出口在当次CBCR读取前；另有10ms等待超时。日志缺原因/期限/CBCR，不能分enable未读回、
OFF未清或整体期限/调度晚到；owned80为写前保守账本，不能当成enable成功。不可宣称0210实机获取已成功。
**NEXT：补clock timeout有界取证：区分overall/branch-wait、now/deadline/cursor/waiting，写before/want/time及已有等待样本。**
整体期限无当次CBCR时明确unavailable或用带时间的缓存样本，不把旧值当当前；不改期限/重试/读写顺序或绕过前置。
先组件验证enable/OFF与两类timeout，再交付取证代码供用户push。本次仅分析文档，无新patch/boot，不重测a23。
LG+4004/bit0/BRANCH_VOTED及enable流程再核对，未发现凭据支持改地址/跳过完成。无kernel panic签名，USB/进程/按键后续活动。
4USB WARN/4FG mismatch/charging0及既存HFS用户态报错未归因本次首错；不转移任务。
详见docs/PANEL-D1-0121A23.md实测；底稿scratch/artifact-0121a23/live-retained-log-analysis.json。
未改驱动/固件/CI，未push/触发CI/操作设备；CircleCI blob保持da0b6c0947a66f03491b3a9023590b49665292df。以下NEXT历史。


## Agent handoff / active work (2026-11-12, 0121a23 RSC vote retained boot ready)

**CURRENT：0121a23源码/内核/镜像核对通过，0210修复在新kernel内，待实机。**
78源码字节匹配0210验证树，内容对应7d35162；比a22仅MDSS.cpp/Clock.inc/NoC.inc变化。
114原CI source/177 binary markers通过，含voted branch/clock-owned-parent/worker-enable/register-token。
内核17724560bytes/ARM64 subtype0，自带SHA匹配，SHA=91d9fd81a3f6a9e3c05a0035923e7d98cea633253e88db66ea2db364e0382342。
无CI run ID/完整build log，不声称核对云端提交元数据。固件ABI608/BootShim/append DTB/各档ADT/参数保持a22。
retained605/607字节，RPMh INTID37扩展保留；四档boot/FAT payload回读通过，只交付retained。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a23-abi608-panel-retained.img**
镜像35545088bytes，SHA=2b93b4fe42d345675794e12c079e86753a4f05a8f693963741de70de41ce7e5f。
等约90秒取live-0121a23-abi608-panel-retained.txt或同名sweetlog bin，观察branch7请求→CLOCK Held/mask381→NoC首错或新DMA。
请求INFO不是完成证明；wrapper/solver/其他branch/TCS仍未实测，不宣称完整接管完成；静帧USB可用就取日志。
panic记PC/LR/FAR/ESR。完整接管前亮度/OFFON/长测/反复USB/combined/supply暂停，旧a22/identity不重测。
详见docs/PANEL-D1-0121A23.md，底稿scratch/artifact-0121a23/。本轮仅核对打包文档，无需重新编译。
未改驱动/固件/CI，未push/触发CI/操作设备；CircleCI blob保持da0b6c0947a66f03491b3a9023590b49665292df。以下NEXT历史。


## Agent handoff / active work (2026-11-12, 0210 RSC clock votes ready)

**CURRENT：0210代码/专项/实现文档已提交`7d35162`，待用户push云编译，未实机。**
修复a22首错branch7：表显式voted/managedAHB；只有NON_GDSC_AHB(+4004)是voted，RSCC_AHB/VSYNC仍halt。
branch7允许enable0/aggregate ON或OFF后实际取自身票；Held仍要enable读回+OFF清除，保留CBCR其他位。
只给表声明的两RSC AHB分支新PLL1/source4/CFG40f权限：core2实际coreReady+typed ARC Turbo ACK7/成功/非uncertain/
启用，当前typed CN0 Complete/成功/非uncertain/至少300M。XO策略及其他PLL/PHY开启限制保持。
NoC fNoCRegisterToken保存最新已确认CN0 token，pending清旧凭据，不再借用被替换的core CN0 token。
worker写前、等待、获取完成/NoC使用前复核父配置/票；失败关写/保不确定资源，未加fallback/timeout。
voted撤回自身enable清除后异步至少10us settle，不要求aggregate OFF；普通halt仍需OFF。
fClockReleaseMask最终整体复核并在撤回失败保所有权；stop只标Stopped/取消事件，不自动撤票。
只改MDSS.cpp/Clock.inc/NoC.inc，无新映射或未知MMIO扫描；CONTEXT加register-token软件字段。
新专项43组ASan/UBSan通过（含exact cbcr0、实际新DMA、DATA/CN0 Off及重新取得、共享/故障/stop）；
另10套显示/生命周期+0209诊断专项通过，共12套；ARM64 MDSS前后编译通过，warnings1543→1547（4数组提示未屏蔽）。
0185–0210累计严格apply无offset/fuzz、78源码字节比较、114原CI source marker通过。
补丁SHA=0de3721f4b89d9c21bfc4556a29eee25205862d04d7d8e1c8b64627dc4788a61。
matrix新增0209/0210 binary门槛，旧a22 kernel被拒且未产出镜像；bash语法通过。
**NEXT：用户自行push并云编译，下载新kernel/buildtree后核对0210，沿原ABI608/ADT/605字节参数打retained。**
只启动等约90秒取完整日志：voted branch7请求→CLOCK Held/mask381→NoC准入/后续首错或新DMA。
尚未观测的wrapper/solver/TCS仍可能阻塞；本地组件通过不代表实机完整接管，无新boot不可刷旧kernel测新修复。
OFFON/亮度/长测/反复USB/combined/supply继续暂停，旧a22/identity不重测。
详见docs/PANEL-RSC-CLOCK-VOTES.md；架构及各计划已同步。底稿scratch/framebuffer-0210/各JSON/编译测试日志。
未改CircleCI/固件/ADT/boot args/apps RPMh IRQ/USB/按键/触摸/电池/mux；未push/触发CI/操作设备。
CircleCI blob保持da0b6c0947a66f03491b3a9023590b49665292df。以下NEXT为历史。


## Agent handoff / active work (2026-11-12, 0121a22 RSC branch admission diagnosed)

**CURRENT：a22实测定位到NON_GDSC_AHB branch7准入，尚未修复。**
日志220866bytes/3061行，SHA=78c0176f0bb518c06191834ba1642946440ff9bcd68480bda2612a11b2ecdd54。
1722–1752 apps IRQ共10次fresh/entries1–10，四LDO/ARC/CN0通过；1753core2/AHB75/MDP460成功。
1757首错CLOCK branch-unowned branch7/cbcr0/source4/link0；1758clock-retain NotReady；
1759phase2/clock-token0/register-acked1/cursor0/5/executing0/op0/0，尚未进入display TCS，并非BCM未ACK。
LG branch7=+4004 NON_GDSC_AHB/BRANCH_VOTED，source4=PLL1 main，共用AHB root22bc；
旧retain和worker仍限关闭+XO策略，未对接core profile2实际PLL1 AHB父时钟；cbcr0也不满足原halt准入。
**NEXT：实现显式RSC voted/halt分支策略，在coreReady/父源及票据成立下取得自己的enable，覆盖worker/final/release/fault。**
不要普遍放行PLL/删除保护；RSCC_AHB/VSYNC为BRANCH_HALT，只有NON_GDSC_AHB voted，不强求撤票后全局OFF。
先有界组件测试与真实ARM64编译/补丁重放，再交付新代码供用户push；本次没有新patch/boot，不重刷a22。
未见kernel panic或RPMh SNAPSHOT；4USB WARN/8FG profile mismatch/charging0/HFS -34伴随，a21亦有，未归因本次首错。
详见docs/PANEL-D1-0121A22.md实测；底稿scratch/artifact-0121a22/live-retained-log-analysis.json。
未改驱动/固件/CI，未push/触发CI/操作设备；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。以下NEXT历史。


## Agent handoff / active work (2026-11-12, 0121a22 NoC diagnostic retained boot ready)

**CURRENT：0121a22产物/源码/镜像回读通过，0209已在新kernel内，待实机。**
78源码与0209验证树字节相同，对应a325219；相比a21仅MDSS.cpp/Clock.inc/NoC.inc变化。
114原CI source/173 binary markers通过，新增NOC ADMIT/CONTEXT在内；无CI run ID/完整build log。
内核17740872bytes/ARM64 subtype0，自带SHA匹配，SHA=6b085d4a741ec0f2530bcb815df426333138f1b4fef6e547f820cf03a299fab9。
ABI608固件/BootShim/append DTB/各档ADT及参数保持a21；retained605字节，INTID37扩展保留。
四档boot/FAT payload回读通过，只交付retained。诊断用途，未修复NotReady/冻结，未实机。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a22-abi608-panel-retained.img**
镜像35545088bytes，SHA=5d3ccc77ef6dd4b1ae87e45eda7354ac0e0c0a73c52b385607522b6df074ca3d。
等约90秒导live-0121a22-abi608-panel-retained.txt或同名sweetlog bin，取首条CLOCK/NOC gate→CONTEXT→NoC错误。
即使静帧也可取USB；panic记PC/LR/FAR/ESR。接管前亮度/OFFON/长测/反复USB/combined/supply暂停，a21/identity不重测。
详见docs/PANEL-D1-0121A22.md；底稿scratch/artifact-0121a22/。本轮仅核对打包文档，无需再次编译。
未改驱动/固件/CI，未push/触发CI/操作设备；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。以下NEXT为历史。


## Agent handoff / active work (2026-11-12, 0209 NoC admission diagnostics ready)

**CURRENT：0209代码/测试/实现文档已本地提交`a325219`，待用户push云编译，未实机。**
针对0121a21新首错display NoC NotReady，补RSC clock请求/状态、clock/domain/lease/parent/branch、
RSC layout、wrapper override/power/control、seq busy/solver的原始检查分支日志。
终止新增一次ERROR `SWEET MDSS NOC CONTEXT:`，记录phase/clock-token/apps-token/register-acked/
cursor/group/TCS/executing/operation/posted及clock worker；全为软件字段，Aborted无该增量。
clock私有诊断enum区分silent/first-boot verbose/NoC failure-only，成功timer不刷屏。
仅改SweetMDSS.cpp私有声明、Clock.inc、NoC.inc；无新增成员、MMIO读写/触发/ACK/timeout，不放宽任何保护。
先clock可达后RSC/layout再wrapper，原short-circuit读取顺序保持，失败后不再取样，typed owner/gate不变。
新专项29准入场景+3完整失败路径ASan/UBSan通过，0208基线与诊断版完整MMIO轨迹和返回值相同。
另10套clock/core/noc/managed/lifecycle/link/combined/supply/parking/profile回归通过。
MDSS真实XNU/KDK ARM64对象前后通过，warnings1538→1543（新增5数组访问提示，未屏蔽）；无新格式错误。
0185–0209严格累计应用/78源码字节比较/114原CI marker通过。补丁SHA=e61e94601c842190b7d003d710d8c39a5302622439b38c48aa4790dc3aedd27e。
**NEXT：需要用户push本地提交并云编译，下载新kernel/buildtree后核对0209，再打新ABI608 retained。**
固件/ADT/605字节参数沿用0121a21，不能用旧kernel刷出新诊断。目标首条CLOCK/NOC ADMIT gate→CONTEXT→原NoC rc。
只是取证，尚未修复NotReady/冻结；完整接管前亮度/OFFON/长测/反复USB/combined/supply暂停，旧a21/identity不重测。
详见docs/PANEL-NOC-ADMISSION-DIAGNOSTICS.md；架构/NoC/两显示计划/总计划已同步。
底稿scratch/framebuffer-0209/各JSON/编译日志及test-noc-admission-diagnostics.txt（基线对照）。
无新boot；未改CircleCI/固件/USB/按键/触摸/电池/mux/apps RPMh IRQ，未push/触发CI/操作设备。
CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。以下NEXT均为历史。


## Agent handoff / active work (2026-11-12, 0121a21 RPMh IRQ PASS; next display NoC admission)

**CURRENT：0121a21日志已核对；0208 apps RPMh实测通过本轮事务，首错移到display NoC NotReady。**
live-0121a21-abi608-panel-retained.txt，243716bytes/3288行，SHA95fb47d73a88926dc852ba515cf8e208eb93331659376ab357ece392928ac8f3。
1731触摸quiesce/1736managed；1737INTID37 configured；1738–1758共10 fresh IRQ，entries连续1–10。
四LDO token1–4 ACK、ARC五stage/token5/Turbo/cx64mx384mmcx384/mask7、CN0 token6/300M完成。
1759core2/GDSC/PLL0/1=1380/600/AHB75/MDP460成功；1761NoC的CN0 token7同值no-op完成（非第11硬件IRQ）。
1762首错display NoC token1/NotReady0xe00002d8/ACKmask0/uncertain1；1764后续managed因fNoCFault连锁失败，
rails0xf/NoC1/clock0，1765无新DMA，1768pixels冻结。无RPMh SNAPSHOT/panic签名，未验收物理电压/完整接管。
NoC advance的NClock retain/status与NData noCAdmission均可给同码；当前日志无phase/clock lease/原始准入值，
不能认定具体时钟或wrapper/solver条件。dataACK0不证明未写payload；uncertain1也可由apps工作设置。
**NEXT：为SweetMDSSNoC的准入失败补分支/phase/clock lease/现有读值取证，先保可访问性，再RSC/wrapper。**
保留typed provider/MMIO所有权，不放宽保护或盲目触发，不改已通过apps IRQ，不扫描未知地址。
本次仅分析/文档无新patch/boot，不需push云编译或重刷0121a21；接管前OFFON/亮度/combined/supply继续暂停。
详见docs/PANEL-D1-0121A21.md实测节及NoC页/架构/计划；底稿scratch/artifact-0121a21/live-retained-log-analysis.json。
4USB WARN及16FG profile match0/integrity0伴随，未归因NoC，不切任务。FSR400WARN仍非已定位停止分支，不清除。
未改CI/驱动/固件、未push/触发CI/操作设备；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
以下NEXT均为历史。


## Agent handoff / active work (2026-11-12, 0121a21 completion IRQ retained boot ready)

**CURRENT：用户下载0121a21，0208产物/源码/镜像回读通过，待实机。**
78源码与0208验证树字节一致，内容对应190c558；较a20仅RPMh.h/.cpp/sleh变化，114原CI source/171 binary markers通过。
无CI run ID/完整build log，不声称核对云端提交元数据。内核17724392bytes/ARM64 subtype0、自带SHA匹配，
SHA=5ab3343759ba165096b43a0fd3d4697206cfc9d4a112fe9328ff0a68500b25d8。
四档boot header/FAT kernel+ADT+TOML/firmware通过；ABI608固件27e7a1d1...18ea480及retained605字节参数同a20。
hardware ADT显式新增RPMh map3 GIC/irq-abi1/intid37，移除扩展即字节恢复a20；identity ADT不变。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a21-abi608-panel-retained.img**
镜像35545088bytes，SHA=298c30a8ac6a92e9804d398085711e9241cf415b44fea052258ed887b388006d。
等约90秒导live-0121a21-abi608-panel-retained.txt或sweetlog同名bin，一份完整启动日志即可。
目标touch-ready/quiesce/managed→INTID37 configured→fresh ACTIVE completion→ldoa14 ACTIVE ACK及后续首错/新DMA。
接管前不测亮度/OFFON/长测/反复USB；identity/a20不重测，combined/supply暂停。panic记PC/LR/FAR/ESR。
CMD_STATUS delta/doorbell示例仅文档，未入生产；本轮不是接IOInterruptController，仍平台GIC callback→软件event/workloop。
详见docs/PANEL-D1-0121A21.md，RPMh页/架构/两显示计划/总计划已同步。
证据scratch/artifact-0121a21/两个verify脚本/JSON/packaging.txt。未改驱动/固件/CI，未push/触发CI/操作设备。
CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。文档本地提交无需重编；以下NEXT均为历史。


## Agent handoff / active work (2026-11-12, 0208 RPMh ACTIVE completion IRQ ready)

**补充复核：** 用户要求CMD_STATUS delta/IRQ_ENABLE/doorbell诊断，已核对0208实际路径并提供
docs/PANEL-RPMH-SUBMIT-DIAGNOSTICS.md完整示例和判断标准。IRQ source开启/读回和命令后trigger已实现；
尚无前后status delta与最终trigger显式读回。delta0不能证明未执行，非零也不是全命令fresh ACK。
示例仅文档，隔离树ARM64编译通过，新增6条unsafe-buffer-usage提示未屏蔽；未接入生产/实机。
底稿scratch/rpmh-submit-review。无新编译测试轮次要求，0208待用户push/云编译的NEXT保持。

**CURRENT：0208已实现并本地提交`190c558`，待用户push/云编译，未实机验收。**
针对0121a20 ldoa14 completion缺fresh证据，新增apps RSC SPI5/INTID37 level-high路径：
GIC callback只mask/原子排队→software IOInterruptEventSource→SweetRPMh workloop核验selected IRQ、
idle和全部issued/complete→cleanup读回→成功票据。timer仅期限/posted/GIC RWP或EOI drain，无completion polling。
触发前在已认领空闲TCS清旧完成、启source并核验selected status0；先Armed再trigger再启GIC，trigger后不清pending。
启动与首请求检查SPI owner；保留邻GIC字段及其他source位，未知/partial/丢失IRQ明确失败，不ACKforeign或伪造票据。
stop关准入/mask→Online0→dispatch lock撤物理入口→同步移除event→释放map；原100ms/ARC期限不增加。
原typed consumer和唯一writer不变；physical dispatch指针不是consumer全局单例桥接。
16套真实源码ASan/UBSan通过，另ADT/CLI检查；RPMh/sleh真实ARM64对象通过，warnings1326→1326/1→1，符号相符。
0185–0208严格累计应用、78源码字节比较、114原CI source marker通过。补丁SHA=afd9f1480ce665b238065aa5127e27795b3cf7f3cc02aa2592048db323b09b05。
工具增加--rpmh-irq：RPMh nub原3资源不动，尾加GIC17a00000/10000及irq-abi1/intid37；mode2必须新扩展，mode1兼容。
hardware ADT现在不同于0121a20，须重生成；identity旧ADT保持。matrix要求新IRQ binary markers，拒绝旧kernel冒充。
**NEXT：用户自行push并云编译，下载新kernel/buildtree后核对0208源码/IRQ markers，同ABI608及原参数重打retained。**
先只取完整启动日志，目标INTID37 configured→fresh ACTIVE completion→ldoa14 ACTIVE ACK及后续首次错误/新DMA。
旧0121a20/identity不重测；完整接管前不测亮度/OFFON/长测/反复USB，combined/supply继续暂停。
ACK不是物理电压证明；不宣称实机已修好或完整面板驱动完成。未生成新boot，不能刷旧kernel验证新IRQ。
详见docs/PANEL-RPMH-COMPLETION-IRQ.md，超时页/架构/两显示计划及总计划已同步。
本地证据scratch/framebuffer-0208/{verification,tests,cumulative-replay,warning-comparison}.json与编译/测试日志。
未改CI/固件/boot args/USB/按键/触摸/电池/mux，未push/触发CI/操作设备；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
以下NEXT均为历史，当前执行点以本节为准。


## Agent handoff / active work (2026-11-12, 0121a20 completion evidence missing; audit WARN explained)

**CURRENT：0121a20日志已到，用户追问SCANOUT WARN；已定位WARN分支及RPMh完成条件阻塞。**
日志live-0121a20-abi608-panel-retained.txt，225225bytes/3119行，SHA=84560e58b3d0f726573be90e2620cb80e6537b95ca06875726f7d9e8476b0109。
607/695/840审计WARN来自SweetFBScanout.inc的AppleCLCD::auditScanout原始cbFault0x400非零；layout/route符合既有条件，
此WARN分支不置Failed/不拦接管。849 F4 accepted、1651触摸quiesce、1657 managed证明已继续。
1679 SNAPSHOT deadline/completion VRMrail0/TCS0/cmd3/mask7/executing1/progress0/operation0/0/posted0/总期限迟68us。
1680 IRQ_ENABLE0/saved1/IRQ_STATUS0/TCS_STATUS1/CONTROL1010000/ENABLE7/WAIT7。
1681–1683 addr42100/data708、42108/data7、42104/data1，三个CMD_STATUS均10101（含issued8/complete16）。
1684 token1/ldoa14 Timeout；1686账本rails0/NoC0/clock0，1687未提交新DMA，1696pixels冻结。
具体阻塞条件是irq&bit || fProgress为假。已排除仍program或cleanup；未证明CMD_STATUS属于本轮新ACK，
不能称已成功供电或确定纯假超时。优先查mask+timer fresh证据路径；是否IRQ_STATUS被mask影响/错过busy/旧status尚未闭合。
FSR0x400不是本轮停止分支，但未证明其无害，不清/删WARN；CMD_STATUS额外bit0含义不猜。
**NEXT：继续时优先设计/修正SweetRPMh获取fresh完成证据方式，参考LG ACTIVE IRQ。**
若接apps RSC IRQ，先审DTS SPI5→INTID37的既有所有者/GIC回调/event/workloop/source启停ACK/stop，
完整安装接收路径后才启source。不能仅删fresh/加超时/忙等。仍由typed provider唯一writer完成，不穿透consumer。
本轮仅日志/源码分析和文档，无新patch/boot，不需push编译/旧镜像复测；接管前OFFON/combined/supply继续暂停。
代码ae78f99/文档7fd04c0对应0121a20产物；本次分析文档另行本地提交。
详见PANEL-RPMH-FIRST-VOTE-TIMEOUT.md实测节、PANEL-D1-0121A20.md、架构/两计划。
证据scratch/artifact-0121a20/live-retained-log-analysis.json。11FG profile/4USB WARN伴随记录，未转移任务。
未改CI/固件/USB/输入/电池/mux，未push/触发CI/操作设备；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
以下NEXT均为历史。


## Agent handoff / active work (2026-11-12, 0121a20 RPMh snapshot retained boot ready)

**CURRENT：用户下载0121a20，0207产物/源码/boot回读通过，待实机。**
78份实际源码与0207树字节一致，内容对应ae78f99，增量仅SweetRPMh.cpp；114 CI source/164 binary markers通过。
含0206首次启动等待和0207四类SNAPSHOT格式。无CI run ID/build log，不声称CI提交元数据已核对。
内核ARM64 subtype0/17723872bytes、自带SHA一致，SHA=b6f0eb607110c6b8dfcfb8c7af33ff3df837b9434e7e333053c84b49c6ef6e15。
四档镜像header/firmware/FAT kernel+ADT+TOML通过；同ABI608固件27e7a1d1...18ea480，
retained605字节参数及完整ADT同0121a19，旧镜像/基线保留，只交付retained。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a20-abi608-panel-retained.img**
镜像35545088bytes，SHA=75ec4ba32e6b27675795e43331fe1254d3eac0528001ba8e09b3edc04cdcd9bf。
启动等约90秒，导live-0121a20-abi608-panel-retained.txt或sweetlog同名bin，一种完整日志即可。
目标waiting→worker-ready→quiesce→managed→首次RPMh SNAPSHOT+紧接token/ldoa14结果。
phase=program/completion/cleanup，TCS/IRQ/idle/control/enable/wait、最多3cmd、operation/posted/超期量。
这是诊断版未修复超时，仍可静帧，不承诺正常画面。触摸已ready时可无waiting。
接管前不测亮度/OFFON/长测/反复USB；identity/旧0121a19不重测，combined/supply暂停。
panic记PC/LR/FAR/ESR；完整接管成功先核对日志再同镜像显示测试。
详见docs/PANEL-D1-0121A20.md，架构/两计划/RPMh页已更新；底稿scratch/artifact-0121a20/两verify脚本/JSON/packaging.txt。
本轮仅产物核对/打包/文档，未改驱动/固件/CI，未push/触发CI/操作设备。
CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。文档本地提交无需重编，直接测新镜像；以下NEXT均为历史。


## Agent handoff / active work (2026-11-12, 0121a19 touch wait PASS; 0207 RPMh evidence ready)

**CURRENT：0121a19实测通过触摸等待/quiesce，第一笔ldoa14 RPMh事务Timeout；0207故障快照已准备。**
日志live-0121a19-abi608-panel-retained.txt，278410bytes/3705行，SHA=c7797054e95eb4d22d42643bdd250f9196f7b582cbe60b29be709ec7b635f5d0。
862–867六BCM通过含ACVwidth0/mask8；868waiting；1761–1766ready/IRQ/HID暂停/恢复期限；
1767–1770显示TE/auto/IRQ暂停并进入managed。1773token1/ldoa14/0xe00002d6(Timeout)/uncertain1；
1775rails0/NoC0/clock0，1776未提交新DMA，1780pixels冻结。确认账本0不证明没有实际写入。
0206启动竞态实机修复通过；尚无完整managed/新扫描地址成功，不是ACV票又失败，也非整核panic。
现日志不含TCS/编程/完成/清理细节，不能直接认定PMIC拒绝、电压错或mask是根因。
已核对LG sm8150 DTS/mailbox/regulator：apps ACTIVE0/1、DRV2、步长/MSGID/触发及VRM偏移匹配；
LG走已使能IRQ，当前timer/mask/fresh差异须进一步实机证据，未擅自启用未接GIC的IRQ。
0207只改SweetRPMh::advance局部lambda：期限/posted读回失败/无效完成读时一次ERROR快照最多6条，
program/completion/cleanup、request/index/TCS/operation、超期量、IRQ/idle/control/enable/wait、最多三cmd。
finish原日志关联token/资源名；顺序取样非原子。无新写/未知地址/扫描其它TCS；stop/取消/退出/晚timer无快照。
原timeout/fresh完成/IRQmask/故障保留/唯一writer与typed接口不变。不是解除冻结的修复版。
6套实际源码ASan/UBSan通过；含首timer迟到/cleanup迟到、诊断阶段/条数、无写/W1C不读/stop/晚timer。
RPMh真实XNU/KDK ARM64前后通过，warning1326→1326；0185–0207严格累计应用、78源码字节一致、114CI marker。
代码/测试提交`ae78f99`（0207）；本次交接与实测文档随下一提交记录。
**NEXT：需要用户push本轮本地提交、云编译并下载新kernel/buildtree。**
核对0207后同ABI608/605参数打新retained；等约90秒导一份启动USB或持久日志，看SNAPSHOT和首次失败。
旧0121a19/identity不重测；接管前不做OFFON/亮度/反复USB/长测，combined/supply继续暂停。
本版仍可能静帧，勿宣称实机已修好。无新boot文件，等新kernel再打包。
伴随29FG profile match0/integrity0/nominal3510、4USB WARN，未定因，不切换当前任务或称真实充电正常。
文档PANEL-D1-0121A19/PANEL-RPMH-FIRST-VOTE-TIMEOUT/PANEL-TOUCH-STARTUP-WAIT及架构/两计划已更新。
证据artifact-0121a19/live-retained-log-analysis.json、scratch/framebuffer-0207/各验证JSON与编译日志。
未改CI/固件/boot参数/USB/输入/电池/mux，未push/触发CI/操作设备；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
本轮代码/测试/文档本地提交；以下NEXT均为历史。


## Agent handoff / active work (2026-11-12, 0121a19 startup-wait retained boot ready)

**CURRENT：用户下载0121a19，产物/源码/boot回读通过，待实机。**
78源码与0206树字节一致，内容对应5966378；114 CI source/160 binary markers通过，
含waiting/worker-ready/participant/worker拒绝和启动期限。无CI run ID/build log，不声称CI提交元数据核对。
内核ARM64 subtype0/17723824bytes、自带SHA一致，SHA=2c008e7dc33c74f3216c39223744df7dfe6601743d7c4ee225192f0496114c27。
四档镜像header/firmware/FAT payload通过；ABI608固件仍27e7a1d1...18ea480，retained605字节参数
及ADT与0121a18完全一致，旧镜像/基线保留，只交付retained。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a19-abi608-panel-retained.img**
镜像35545088bytes，SHA=464c7f6b1bc47ed2a528043cbbc0b33d9ef5a1106b9aee53a2ffe637e0ebdcb0。
启动后等约90秒，看早期静帧是否继续Logo/系统，导live-0121a19-abi608-panel-retained.txt或sweetlog同名bin；
一种完整日志即可。目标waiting→worker-ready→quiesce→managed/SCANOUT/HANDOFF及首次错误；
若触摸原已ready可无waiting。先不息屏/亮度/反复USB/长测，确认接管再同镜像OFFON。
identity不重测，combined/supply暂停；旧0121a18不重测，panic记PC/LR/FAR/ESR。
用户问触摸/面板为何竞态：已核对是主动软件生命周期依赖+provider先匹配/worker后ready，
不是已证实共享I²C/DSI或reset寄存器冲突。显示DSI/reset6/TE8/INTID115；触摸I²C55/56/reset54/
ATTN122/INTID240。TLMM映射包含但引脚写所有权不重叠；共享供电影响未知，不声称电气全独立。
LG panel BLANK通知会进S3618 LCD_MODE；RESET通知在本型号分支为空。首次统一暂停为保守软件策略，
不是面板硬件上电已证明必须等待触摸。已写入PANEL-TOUCH-STARTUP-WAIT.md末节。
详见docs/PANEL-D1-0121A19.md；证据scratch/artifact-0121a19/（两verify脚本/JSON/packaging.txt）。
本轮仅核对/打包/文档，未改驱动/固件/CI、未push/触发CI/操作设备；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
本轮文档本地提交不需重编，直接测已打好的0121a19；以下NEXT均为历史。


## Agent handoff / active work (2026-11-12, 0121a18 ACV PASS; 0206 late-touch startup fix ready)

**CURRENT：用户0121a18仍静帧；USB日志证明ACV metadata已通过，已修下一处启动竞态。**
live-0121a18-abi608-panel-retained.txt，237460bytes/3311行，SHA=c7077d1e0d78526591716db9dfe344f4b19e0b99027e3d5ee003fef7c9ee3446。
813–818六BCM全通过，ACVunit1/width0/VCD11/mask8；819 begin NotReady，无managed预检ERROR/
lifecycle begin或warm paused，尚未开始managed资源写。1601–1638触摸才identify/app/ready/IRQarm。
两个RD_PTR完整窗59877/59867mHz，SpringBoard/USB/按键/触摸活着，无panic签名；
3265息屏生命周期拒绝为接管未完成的后续表现。CB2 FSR400仍泛化审计项，不清/绕保护。
原TouchPanel服务可先匹配，worker后online；beginLifecycle要求worker available，FB只begin一次，
NotReady永久Failed。旧日志没分支诊断，不能排除同时的participant绑定问题；已直接联合复现
“provider匹配但worker晚8秒online”原代码拒绝/无重试，修复后一次token继续完成managed接管。
0206仅改SweetMDSSLifecycle.inc：首次Takeover接受后TouchPause/token0等待已有worker软件快照，
50ms IOKit timer/30秒上限，不轮询硬件/忙等/sleep；等待期不发quiesce、不改显示/投票/pinpool。
ready后一次typed quiesce，重启原16秒总/6.5秒pause预算，真实quiesced后才原scanout/managed链。
普通OFF/ON准入/运行期故障策略不放宽；timeout/cancel/stop/provider退出不能被迟到ready复活。
补participant/worker拒绝ERROR及waiting/ready INFO，不高频刷日志。ACV0205保持。
15套ASan/UBSan通过；新增等待8秒/无MMIO/一次请求完成、超时/取消/stop/touch和RPMh退出；
retained mode2四profile晚worker接管及三轮原OFF/ON通过。旧baseline新增case按预期失败。
MDSS前后真实XNU/KDK ARM64单文件通过，无新增warning1538→1538；0185–0206严格累计应用、
78源码字节一致、114现有CI marker通过。不是整核/实机修复验收。
**NEXT：用户push本轮0206本地提交并云编译，下载新kernel/buildtree后同ABI608/605参数打新retained。**
先取启动waiting→ready→quiesce→managed/SCANOUT/HANDOFF及首次错误；不重测0121a18、identity/
combined/supply，不做额外USB插拔/长测。实测确认接管后再同镜像亮度/OFFON。
详见docs/PANEL-TOUCH-STARTUP-WAIT.md；PANEL-D1-0121A18/架构/两计划/ACV页已更新。
证据scratch/framebuffer-0206/与artifact-0121a18/live-retained-log-analysis.json。
伴随观察：10份FG profile match0/integrity0/nominal3510，capacity有效位0；4USB WARN，未定因，
不把这轮称真实充电正常或因显示导致，也不额外安排电池/USB测试。
未改CI/固件/boot参数/USB/输入/电池，未push/触发CI/操作设备；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
本轮代码/测试/文档本地提交；以下NEXT均为历史。


## Agent handoff / active work (2026-11-12, 0121a18 ACV-fixed retained boot ready)

**CURRENT：用户下载0121a18，两包校验/实际源码/打包回读均通过，待实机。**
78源码字节一致含0205三源，内容对应433aceb；114现有CI source/155 binary markers通过。
未提供CI run ID/build log，不声称核对CI提交元数据。内核ARM64 subtype0、自带SHA一致，
17723824bytes，SHA=a99b3a820947319c034592452566334577ab586c484b9316be436a9464b62f0e。
ABI608固件仍27e7a1d1...18ea480，retained605字节参数及ADT与0121a17一致；
四档header/firmware/FAT payload回读通过，旧镜像/基线保留，只交付retained。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a18-abi608-panel-retained.img**
镜像35545088bytes，SHA=c81fbfa2a2336ed0009bfa014fe89854fa6e7de323e8a845fa451dbce968956b。
启动停留约一分钟，观察早期静帧是否继续到Logo/系统，导live-0121a18-abi608-panel-retained.txt
或sweetlog同名bin，一种完整日志即可。ACV预期width0/VCD11/enable-mask8，然后看NoC/managed/
SCANOUT/HANDOFF；不把ACV准入当全部成功。若静帧而USB活着直接取日志，若panic记PC/LR/FAR/ESR。
先不息屏/亮度/反复USB或长测，日志通过后同镜像测亮度/OFFON；identity不重测，combined/supply暂停。
详见docs/PANEL-D1-0121A18.md，底稿scratch/artifact-0121a18/（两verify脚本、JSON及packaging.txt）。
本轮仅核对/打包/文档；未改驱动/固件/CI、未push/触发CI/操作设备。CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
本轮文档本地提交后无需为文档重新云编译，直接测试已打好的0121a18。以下NEXT均为历史。


## Agent handoff / active work (2026-11-12, 0205 ACV enable-mask fix ready for build)

**CURRENT：按用户明确要求把display ACV建模为mask8开关资源，已实现，不再只做诊断。**
本地SM8150 DTS无enable-mask/bmask属性；LG apps定义BCM_TCS_CMD_ACV_APPS=8、X0/Y8。
用户提供Linux补丁摘录支持ACV width0/enable_mask思路，链接因环境网络限制未取得正文；
不声称逐位复刻未读补丁或已证实所有RPMh SoC通用。原LG display普通分支是历史源码事实，
当前按用户决定采用明确开关策略，取代下面各历史handoff的“不能套mask/仍待推导”约束。
0205只改RPMh h/cpp及MDSSNoC：资源表ACV mask8、typed encode、保留真实width0/unit1/VCD11；
仅mask资源允许width0，其它metadata/alias/范围校验保持。普通缩放显式拒绝mask资源；
EBI avg/peak任一非零时X0/Y8，均零时零票；不填width、不继承MC0累计结果、不删ACV。
MDSS唯一display RSC writer及VCD/fresh ACK/部分完成账本保持；apps无新增ACV写。
VCD11单命令ON含COMMIT/VALID为60000008、OFF仅COMMIT为40000000。
双DATA/保留config/最后DATA撤回/重新申请/非零width仍mask及无重复写均由实际writer测试覆盖。
26 metadata场景、9套ASan/UBSan回归通过（含managed/core/lifecycle/parking/combined/supply）；
RPMh/MDSS修改前后真实CI/XNU/KDK arm64单文件编译成功，无新增warning（1326→1326/1539→1538）。
0185–0205严格累计应用无offset/fuzz，78源码字节一致，114现有CI marker通过。
**NEXT：需要用户push本地本轮提交、云编译并下载kernel/buildtree；本代理未push/触发CI。**
拿到新产物核对0205，再同ABI608固件/605字节参数打新retained镜像；先看ACV准入、managed
投票/接管及画面，成功后同镜像亮度/OFFON。不要称已实机修好或完整面板已完成。
旧0121a17无需重测，identity/combined/supply保持暂停；不额外插拔USB或半小时长测。
详见docs/PANEL-ACV-ENABLE-MASK.md，架构/计划/width来源已加最新策略覆盖历史判断。
底稿scratch/framebuffer-0205/{verification,tests,cumulative-replay,warning-comparison}.json及编译日志。
未改固件/USB/按键/触摸/电池/boot参数；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
本轮未操作设备、未启动Android/扫描MMIO；源码修复本地提交，以下NEXT均为历史。


## Agent handoff / active work (2026-11-12, full Lineage ACV zero-width vote reproduced offline)

**CURRENT：用户要求更仔细查width0，本轮已补查表公式/版本调用/实际构票原函数复现。**
静态DB header/entry/aux公式与Lineage一致，db16对齐半字无错位；Lineage BCM init未调用
cmd_db_get_version，无证据存在“另一版本width取高位”分支。仍无设备DB原始独立dump/aux版本，
不能把重建日志或fixture当成物理地址/格式已独立确认。
新证据：原文提取Lineage div64/acv_update/bcm_update/tcs_cmd_gen四函数，不改函数体；
本机MC0(width16/unit2050000)、ACV(width0/unit1)，DTS width4/ports4/MC0→ACV顺序。
合成AB64MB/s/IB128MB/s下，MC0得到31/249；ACV自身width缩放0/0，但因跨BCM累计值，
display ACV最终31/249（含COMMIT命令6007c0f9），apps ACV0/8（60000008）；OFF均0。
两context ON/OFF/ON共12场景ASan/UBSan通过，LSan因环境关闭；并非实机发送/量测。
**NEXT：明确ACV不能套普通非零width准入/独立缩放，需资源特定模型；不盲填width/删ACV。**
已证实Lineage软件能在width0生成非零票；跨BCM行为硬件意图及实际XNU最终策略仍须保守推导。
未修改驱动或打新补丁，无push/云编译需求。资料PANEL-BCM-WIDTH-SOURCES.md新复查段；
证据artifact-0121a17/lineage-acv-oracle.{c,txt,json}。
本地没有commandDB/AOP dump；尝试下载固定Linux v6.12相关源被网络限制拒绝，未引用未取得外部源码。
无CI/固件/设备操作，未启动Android/扫描MMIO。以下NEXT为历史。


## Agent handoff / active work (2026-11-12, BCM width bit-position re-audited)

**CURRENT：按用户质疑重新核对lookupBCM来源/有效位，尚无驱动修改。**
lookupBCM是本项目0187新增自有代码；Lineage为msm_bus_bcm_init调用cmd_db_get_aux_data后
直接取aux_data.width（uint16_t）。width在aux+4；是第二个u32的低16位，整个aux64的32..47位。
从Lineage原结构提取做Linux ARM64/Darwin ARM64编译，各12静态断言通过：
aux size8/width+4/VCD+6/reserved+7；DB header144/slave16/entry24及entry addr16/len20/offset22。
IR确认Lineage读取u16+4、当前读取u32+4&ffff，两者小端字段值一致，未把VCD当width。
日志重建字节得到width0/VCD11，非零对照得到1234；不冒称这是独立设备内存dump。
**NEXT不变：依据LG/Lineage构票链修ACV模型，不能把问题改说为已证实有效位读错。**
完整地址/aux版本实机正确性与硬件投票语义不由本次layout证明单独闭合；不猜width、不套apps掩码。
证据artifact-0121a17/lineage-bcm-layout-*及docs/PANEL-BCM-WIDTH-SOURCES.md新复核段。
本轮仅源码审阅/离线布局验证/文档，无CI/固件/驱动改动，无push/上机需求。以下NEXT为历史。


## Agent handoff / active work (2026-11-12, ACV width provenance confirmed against Lineage/DTS)

**CURRENT：已按用户要求核对旧资料、设备树及独立lge-lineage-kernel。**
Lineage HEAD2e897dddf49a4c3e85b2ff77fdf7c70f860398dd与LG d4f335e相关五文件字节一致。
ACV BCM width直接来自remote固件加载的commandDB aux（u32unit/u16width/u8VCD/u8reserved），
msm_bus_fabric_rpmh.c1206直接赋值，无width0拒绝/无DTS补值。XNU字段布局一致。
sm8150-bus.dtsi2150节点的qcom,buswidth4是分母，BCM width是分子，不能互填；
ACV width0不是面板/DDR物理宽度0。固件为何生成0的内部实现不在公开驱动源码。
**NEXT：继续按真实元数据设计ACV投票模型；当前无代码/编译/额外实机需求。**
不要只删lookup gate（scale也拒绝width0），不要用DTS4/MC0的16覆盖0，不套apps mask8。
display27006与apps7038分支不同，LG MC0→ACV循环累计值未清零仍须纳入构票分析。
当前已闭合“值从哪里来、怎样解码、为何拒绝”，未闭合最终硬件投票意图。
资料docs/PANEL-BCM-WIDTH-SOURCES.md；对照scratch/artifact-0121a17/bcm-width-source-review.json。
仅文档，本轮无驱动/固件/CI改动，未push/触发编译/操作设备。以下NEXT为历史。


## Agent handoff / active work (2026-11-12, 0121a17 live ACV diagnosis complete)

**CURRENT：USB日志已取得所需字段，不需补持久日志或重测。**
logs/live-0121a17-abi608-panel-retained.txt SHA=cb9f4f944dc35e881514ae3ea901834cff8a9130256239fff739ddc3fb0decfc。
862–866五项metadata成功；867 ACV gate=values addr5000c/table9c0/unit1/packedb0000/
width0/VCD11/reserved0。直接拒绝条件是width非零假设；不是key缺失/aux长度/unit0。
已过该条目前置检查，未完成后续唯一性/alias验证。868–869 managed preflight/scanout失败。
SpringBoard430；14个完整RD_PTR窗口约59.9Hz，无panic；4USB warning/15HFS clonefile-34。
**NEXT：按LG依据修正ACV资源模型前，先追清display ACV构票语义。**
LG接受width0；display27006不进入apps7038的mask8特殊分支。重要新观察：
msm_bus_arb_rpmh.c bcm_update_bus_req的max_ab/max_ib在BCM循环外初始化，内部不清零，
MC0后ACV可能继承上一轮归一化累计值；不能看到width0就声称display最终票恒0。
这是源码事实，尚未证明硬件期望；继续追构票/提交，不猜width、不跳过ACV或套apps mask。
本轮仅日志分析/文档，无新代码、无需push/云编译；combined/supply暂停。
详见docs/PANEL-D1-0121A17.md新增实测段；底稿artifact-0121a17/live-retained-log-analysis.json。
未改CI/固件/驱动，未操作设备。以下NEXT为历史。


## Agent handoff / active work (2026-11-12, 0121a17 verified; ACV diagnostic boot ready)

**CURRENT：0121a17实际源码/内核已核对0204并打包，待实机ACV具体gate与字段。**
78源码字节一致、114 CI source/154 binary markers通过，内容匹配61402a7；
未提供build log/run ID，不冒称已核对CI提交元数据。
内核a45aaac92b616d3b1a15c4d78819e782e951429d6f8b0748f49d8ef34f6ef76b，17723768bytes，ARM64 subtype0，包内SHA一致。
ABI608固件仍blob27e7a1d1...18ea480；retained605参数/ADT与0121a16完全一致。
四档boot header/firmware/FAT payload回读通过，旧镜像/基线保留；只交付retained。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a17-abi608-panel-retained.img**
SHA=ef48f926cba46d7f6e6ec93b5668dc73f21e9f46a8a3f9cf1bd65c34802ae656，35545088bytes。
启动停留约一分钟导sweetlog-0121a17-abi608-panel-retained.bin；USB流可另存live同名txt。
只取SWEET RPMH BCM name=ACV gate/原始元数据，诊断版可能仍静帧，不是ACV修复；
不重复息屏/亮度/USB插拔或长测；identity不重测，combined/supply暂停。若panic取寄存器。
拿到字段再修，不能猜key/零unit/放宽aux或绕过managed准入。ESC0及七路boot时钟上轮已通过。
本轮无驱动/固件/CI改动；未push/触发CI/操作设备。详见docs/PANEL-D1-0121A17.md；
证据scratch/artifact-0121a17/；以下NEXT为历史。


## Agent handoff / active work (2026-11-12, 0121a16 clocks PASS; ACV metadata blocked, 0204 diagnostics ready)

**CURRENT：已定位本轮静帧到managedPreflight的BCM5=ACV metadata；不是整核死锁。**
持久日志7d6bb833...34e912d，total282705/flush31；USB日志de1f2317...ac76a2。
85–103行ESC0 CFG0及后续BYTE/PCLK全部通过，boot clock mask7f/owned0；109–127行
PV/source2选择、MDSS发布及touch/RPMh/pins匹配；694 SpringBoard；887后GIC/RD_PTR，
五个窗口约59.9Hz。1132 metadata unavailable、1133 begin NotReady；3372 lifecycle拒绝。
软件因own handoff失败停止呈现，亮度DCS仍可成功；用户黑/亮不当作完整OFF/ON通过。
泛化scanout warning伴CB2 FSR400，不是该次最终失败点，不清fault或绕过managed保护。
5USB warning/2HFS clonefile-34/3BindFS ENOENT，无panic/未见group隔离；非本次静帧因果。
LG display label ACV_DISPLAY但DB key ACV正确；LG aux读取允许>=8，XNU严格=8，
具体本机字段未知，不能猜是长度/零unit/width/reserved或缺失，不能盲改投票语义。
0204仅lookupBCM首次诊断：ERROR带header/address/aux/values/matches/alias及已读字段，
成功INFO记录metadata；缓存后不重复。无新增寄存器读写/准入放宽/boot-arg/ABL fallback。
21场景ASan/UBSan及RPMh/NoC/managed/lifecycle回归通过；前后RPMh arm64编译及warning
集合一致；21场景前后返回值/MMIO轨迹610行一致，无额外读取；0185–0204 strict replay/
78文件一致/114 CI marker通过。并非整核或实机修复验收。
**NEXT：用户push本地0204提交、云编译并下载kernel/buildtree；核对后同ABI608/605参数
打新retained档，只取启动ACV gate/字段。旧0121a16不重测；combined/supply继续暂停。**
无需按息屏/反复USB/长测；得到元数据后按LG依据修解析或资源模型，不跳过ACV继续投票。
未改CircleCI（blob仍da0b6c0947a66f03491b3a9023590b49665292df）/固件/USB/输入/电池；
未push/触发CI/操作设备。详情docs/PANEL-D1-0121A16.md及PANEL-NOC-D1.md；
底稿scratch/framebuffer-0204/，设备日志不入git。以下NEXT为历史。


## Agent handoff / active work (2026-11-12, 0121a16 verified; ESC0-fixed retained boot ready)

**CURRENT：0121a16已核对0203修复并打包，待实机启动/接管日志。**
源码包最初0字节，用户补下后重新核对通过；78源码字节一致，clockParent含xoConfig>1u，
114 CI source marker及147二进制marker通过。未提供build log/run ID，不冒称CI提交元数据。
内核199340fa843564a076f1ccd5e207868d9fe980f8ac35bc67466f048264d2bd67、17723760bytes，ARM64 subtype0，自带SHA一致。
ABI608固件仍blob27e7a1d1...18ea480；retained605字节参数/ADT与0121a15完全一致。
四档Android header/firmware/FAT payload回读通过；旧镜像/基线保留，仅retained交付。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a16-abi608-panel-retained.img**
SHA=00c757f8cebfbc74cb7ca28e47a11fe6ee084d726b62d7534cdf2a0a1d249403，35545088bytes。
停留约一分钟导sweetlog-0121a16-abi608-panel-retained.bin，先不按息屏/不做USB插拔或长测。
确认ESC0后BYTE/PCLK、MDSS发布及managed自有扫描接管；日志通过后同一镜像再安排亮度/OFFON。
若画面静帧先导持久日志；若panic记录寄存器。identity不重测，combined/supply继续暂停。
新修复尚未实机验收，勿将打包通过当完整panel成功。详见docs/PANEL-D1-0121A16.md。
底稿scratch/artifact-0121a16/；本轮仅核对/打包/文档，未改驱动/固件/CI、未push/触发CI/操作设备。
以下NEXT均为历史。

## Agent handoff / active work (2026-11-12, ESC0 XO bypass bug fixed locally; needs build)

**CURRENT：0121a15诊断已定位并本地修正ESC0合法旁路被拒绝，不称完整驱动已通过。**
日志SHA=42c6f5f060029d9e9577b13b589f31674d35da80c6226d3332445591c1a18ab1，total265904/flush18；383行605字节参数匹配。
86行GCC AHB80000003/DISPCC AHB1/HF4221/GDSCf822f000通过；AHB/MDP/VSYNC通过；
94行xo-config branch3/root2148/CMD0/CFG0，95行NotReady，无MDSS发布/managed接管。
LG clk-rcg2.c241–249：HID0旁路、HID1=parent*2/2，都为XO原频；旧掩码只接受1是软件错误。
0203只改SweetMDSSClock.inc一处predicate允许0/1，原始parent快照/漂移、update/dirty/root/
branch/source/域/空闲检查不变，不写寄存器强修状态，无fallback/新参数。
实测前缀回归旧代码拒绝、新代码成功且无写；16组合/1280独立rate判据、原始漂移、
旁路ESC0真实异步ON/OFF及19错误拒绝通过。5套ASan/UBSan、前后MDSS arm64编译通过、
无新增warning；0185–0203累计strict replay/78文件一致/114marker通过。
本轮无panic，3USB warning/15HFS clonefile -34/3BindFS ENOENT另记，未测试息屏。
**NEXT：告知用户push本地0203提交并云编译；新产物后同ABI608/605参数打新retained。**
先查BYTE/PCLK后续准入/MDSS发布/接管，若startup仍失败只取日志；后续再安排亮度/OFFON。
不重测旧0121a15，combined/supply继续暂停。尚未证明全部时钟或完整生命周期通过。
文档docs/PANEL-D1-0121A15.md；底稿scratch/framebuffer-0203/和artifact-0121a15/。
未push/触发CI/操作设备；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df，
未改固件/USB/按键/触摸/电池；以下NEXT均为历史。

## Agent handoff / active work (2026-11-12, 0121a15 verified; retained diagnostic boot ready)

**CURRENT：0121a15源码/内核核对及ABI608镜像打包通过，待用户启动取证。**
78份源码与0202验证树完全一致、114项CI marker及新warm admission/gdsc/root/branch字符串通过。
内核c941024722649a5c019614b78b4cc3c95276c236a9c3acbd124ba8810876f8ed，17723760bytes、ARM64 subtype0，自带SHA匹配。
完整build log/CI run ID未提供，不冒称核对了具体CI提交。与842d964验证源码相符。
同一已启动的ABI608固件blob27e7a1d1...18ea480，retained参数605字节和ADT与失败档完全一致。
四档Android header/firmware/FAT payload回读通过，旧镜像/基线保留；只交付retained测试。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a15-abi608-panel-retained.img**
SHA=e380f1ae57cb1fe9f6ce90e6326b765f7f2885e8769dbb933f07953542491cd6，35545088bytes。
进系统停留约一分钟导sweetlog-0121a15-abi608-panel-retained.bin；启动自动打印准入门槛，
不用再调亮度或按息屏，不做插拔/长测，combined/supply继续暂停。启动panic记录寄存器。
本轮仅诊断，硬件准入未修复；先读admit gate及同次读值，若通过再查后续managed/服务发布。
详见docs/PANEL-D1-0121A15.md；证据scratch/artifact-0121a15/。
本轮未改驱动/固件/CI、未push/触发CI/操作设备；以下NEXT均为历史。

## Agent handoff / active work (2026-11-12, retained FAIL; 0202 diagnostics ready)

**CURRENT：用户报告亮度不变、息屏静帧。已定位MDSS启动时钟准入失败，未修复具体硬件条件。**
logs/sweetlog-0121a14-abi608-panel-retained.bin SHA=ca64443926f456214c14a4e41837744c7fa744f2b0183ad3c80ba4bedd0cc18d，
total275230/flush14。第370行605字节参数完整；85行warm lease admission NotReady，
start在setupLocalClocks早退，无MDSS发布/绑定/managed takeover，尚未执行panel OFF/ON。
3304–3339行CoreBrightness目标变化，3624行FB admission失败主动冻结；之后仍有按键/触摸/
进程活动，不证实整核死锁。后档combined/supply暂停，不重测旧retained。
0202仅补首次准入software/GCC/GDSC/root/branch分阶段ERROR/INFO；取现有读值，不加读取/
写入、不放宽检查或fallback，runtime检查静默，未增boot-arg。具体失败寄存器值仍待实机。
基底是0121a14实际云端源码；补丁严格apply/字节一致及现有114项CI marker通过；
ASan/UBSan含19项时钟拒绝/准确失败门槛/短路访问/静默，core/managed/link/lifecycle回归，
修改前后MDSS真实arm64单文件编译通过；0185–0202累计严格重放/78文件一致，
旧/新准入682行MMIO轨迹逐字一致。编译新增8条unsafe-buffer诊断，均为已检查索引的日志取值；
无新增类型/格式警告。底稿scratch/framebuffer-0202/。
**NEXT：告知用户push本地提交并编译，下载新产物后同ABI608固件/retained参数打新名字boot。**
新镜像先取启动准入日志；若仍报startup admission失败，无需再反复按息屏。取到具体门槛才修正。
不把诊断叫修复；E7/E8仍未验收。不操作设备、不push或触发CI，CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
已核对并在CURRENT-DRIVER-ARCHITECTURE.md补当前面板组成、DSI协议/硬件、typed封装与
尚无通用DSI总线/独立panel服务的边界；仅文档说明，下一步0202编译/retained取证不变。
详见docs/PANEL-D1-0121A14.md retained段及PANEL-CLOCK-D1.md；以下NEXT均为历史。

## Agent handoff / active work (2026-11-12, abi608 identity PASS; retained next)

**CURRENT：用户确认ABI修正版进入系统，持久日志吻合；恢复面板生命周期分档测试。**
logs/sweetlog-0121a14-abi608-panel-identity.bin SHA=fa471912e0284870a59ea85b419d8cec6c6868a1be61e66f48f80cf14479c938；
total242952/flush9。386行477字节参数完全吻合；88行source2/profile4（ABL选择，非DDIC读回），
89行MDSS、100行RPMh、666行SpringBoard，5条touch。无panic/驱动ERROR，无显示IRQ、
scanout或生命周期事务，符合metadata-only。原NULL写本轮未复现，不扩大为完整面板验收。
仍有3USB warning、13HFS clonefile -34、3BindFS ENOENT及2关机signal-reason分配失败。
日志未见unmount；无live memSizeActual/HFS分配读回，不称原panic全过程已重现或所有HFS错误修复。
loader ABI修正提交23f9153，内核仍0121a14；无需push/云编译，本轮未改驱动/固件/CI。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a14-abi608-panel-retained.img**
SHA=a0bf30f21b2fab53bfc5fe076be45ec24d648cc74dea3eb78e961bb0dd94e828，已再次核对。
参数605字节，lifecycle2，完整ADT；保留供电票，首次温态takeover不自动reset，
实际OFF执行完整表/触摸静止，ON执行GPIO reset/完整ON/亮度/fresh像素/TCM恢复。
进系统后调亮度、手动电源息屏约5–10秒后唤醒，做两次，确认画面/亮度/触摸/三个按键。
若启动或首轮失败停止，不继续combined/supply；成功导sweetlog-0121a14-abi608-panel-retained.bin。
无需额外USB插拔、自动息屏或半小时测试。combined/supply仍暂停，尚未验收E7/E8。
底稿scratch/artifact-0121a14/{abi608-panel-identity-persistent.txt,abi608-panel-identity-log-analysis.json}。
BOOTARGS-ABI及面板计划/架构同步；以下NEXT为历史，以本段为准。

## Agent handoff / active work (2026-11-12, power0 PASS; boot_args ABI defect fixed locally)

**CURRENT：power0成功；找到loader1024/XNU608 ABI错位，已本地修正并打包，待实机。**
logs/sweetlog-0121a14-identity-power0-control.bin SHA=8687f94b8a68a3639be33019a6aded99398ae07c4dcf51dce2cb197a46fa6d43；
total254168/flush10。386行power0/IRQ0/vsync0/own0吻合，88行source2/profile4，
666行SpringBoard，7条touch，无panic/驱动ERROR。仍有3USB warning、10HFS clonefile -34、
3BindFS ENOENT和关机2signal-reason分配失败；日志到unmount开始，不证明完整停机。
此前逐项对照由5116321记录，本轮不继续猜显示开关或逐个测8个默认参数。
实际云端ARM64 boot.h=608（含EXPORT_HDRS），loader=1024，flags/memActual偏移分别
0x2d0/0x2d8与0x470/0x478。失败参数739字节，内核读flags='sweetdis'、memActual='playcore'
=0x65726f6379616c70。成功590字节时两处0，内核回退memSize，掩盖错误。
实际.CFLAGS证实OSX构建，hw.memsize用max_mem_actual；实际HFS默认大小片段离线得到
-328335360（正常3GiB为1572864）。错误ABI与HFS溢出已实证，原现场分配/NULL写因果待实机收口。
新增boot-support/0002 loader补丁（不入xnu/patches）；独立副本构建，不改参考仓库。
新增mh2lm-boot-args.py长度/重复检查，面板仅省略默认值，USB/电池/非默认硬件模式均保留。
identity477字节，后三档605；keys仍默认1，INFO默认2、probe默认1，power1隐含preserveOff。
ASan/UBSan真实头文件布局+实际参数parser、0..1024边界及四shell分支通过；旧manifest拒绝。
完整FD构建/封装链验证及四boot header/FAT/payload回读通过；旧镜像/基线不变。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a14-abi608-panel-identity.img**
SHA=959b734560beaf05eec555a0bc22a489d0ba89fa1ac3dd756d02801629e11bff。
只确认进系统，成功导sweetlog-0121a14-abi608-panel-identity.bin；失败记录寄存器。
不启reset/链路/供电周期；先检查该档再依次retained/combined/supply，后三档暂停。
新manifest=out-bs/panel-uefi-0121a14-bootargs608/panel-uefi.json；blob27e7a1d1...18ea480，
内核仍7943a67e...68322f，无需云编译/推送。原失败镜像原样复测安排已撤回。
未改HFS/内核驱动/CircleCI/Actions、未操作设备/推送/触发CI。工作区只含loader/打包/测试/文档。
详情docs/BOOTARGS-ABI-0121A14.md；底稿scratch/artifact-0121a14/{identity-power0-control-log-analysis.json,
bootargs-abi-root-cause.json,bootargs-hfs-default-repro.c,verify-boots-abi608.py,boot-abi608-verification.json}。
以下NEXT均为历史，以本段为准。

## Agent handoff / active work (2026-11-12, IRQ0 PASS; power0 next)

**CURRENT：IRQ0日志确认进SpringBoard，下一步仅改power0。原NULL写未修复。**
logs/sweetlog-0121a14-identity-irq0-control.bin SHA=7d8b889176125d2be695bb42c2bc270d469cf4ac6d1cef985599f572ed4e325b；
total245020/flush9。387行power1/IRQ0/vsync0/own0吻合；89行source2/profile4，90行MDSS、
102行RPMh、668行SpringBoard、3条touch。无F3 IRQ/FB hardware mailbox/F4 pool或handoff。
无panic/ERROR；3USB warning、10HFS clonefile -34；关机两条signal-reason分配失败后推进unmount，
不证明完整停机或OOM根因。本档无MDSS POWER调用，不称电源切换通过。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a14-identity-power0-control.img**
SHA=c751f4007487950d3a36f1abe1b886dca92209a4bca95094da72977ef8e732aa。
仅成功IRQ0档sweetfbpower1→0；其余参数/内核/固件/ADT均不变；IRQ0/vsync0/own0保留，合法准入。
保留preserveoff1，FB软件电源状态路径；managed/lifecycle关闭，无新reset/供电周期。
全容器/FAT回读仅TOML单参数变，原镜像保留；菜单旧0121a13刻意保留、实际内核0121a14。
只确认进系统，成功导sweetlog-0121a14-identity-power0-control.bin；失败记录寄存器，无需其它测试。
与原失败fix档公共参数值相同，但原档另有显式8个默认0选项及不同顺序/标题/ramdisk字节，
不能声称原镜像原样重测。power0-versus-failed-config.json存比较。若失败聚焦power路径，
若成功重查精确打包/参数/状态/时序，不能强行归因。后三档继续暂停，无需push/云编译。
无内核代码修复，未改CI/操作设备/推送/触发CI。
底稿scratch/artifact-0121a14/{identity-irq0-control-persistent.txt,identity-irq0-control-log-analysis.json,
make-power0-control.py,identity-power0-control-verification.json,power0-versus-failed-config.json}。
文档/计划已同步；以下NEXT均为历史，以本段为准。

## Agent handoff / active work (2026-11-12, vsync0 PASS; IRQ0 next)

**CURRENT：vsync0持久日志支持正常启动，下一步只测MDSS IRQ0。原NULL写未修复。**
logs/sweetlog-0121a14-identity-vsync0-control.bin SHA=1fb245cd7657f7bfefebd6281a12c343ef0178e149d1b6f7aae3c8ce97c291b5；
total244487/flush9。388行参数power1/IRQ2/vsync0/own0吻合；90行source2/profile4，
91行MDSS、102行RPMh、668行SpringBoard、11条touch/7组FB PERF。
无F4 pool/handoff/FB hardware mailbox；MDSS IRQ仍有GIC/RD_PTR armed、60066ms/3598帧≈59.9Hz，
不等于FB用硬件IRQ呈现。无panic/ERROR；3USB warning，本采集无HFS clonefile/signal-reason错误。
无MDSS POWER请求，不称息屏恢复通过。组合/时序/分配原因仍不能排除。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a14-identity-irq0-control.img**
SHA=a9ac3de3ad17d7e3a052ecec53c2fcf2c22c30fd9c4d8823484771857798254c。
仅成功vsync0档sweetmdssirq2→0，power1/vsync0/own0保留；其余参数/内核/固件/ADT不变。
实际setupIRQ(mode0)早退，无interval/event/INTID115 claim；FB本已软件timer，依赖合法。
原镜像保留，容器/全部FAT回读只TOML单参数不同。managed/lifecycle关闭，无reset/供电周期。
只确认进系统，成功导sweetlog-0121a14-identity-irq0-control.bin，无需其它测试。
若panic记录寄存器，聚焦IRQ启动/分配；若通过最后只改power0。IRQ2依赖power1，不能反过来。
无需push/编译；后三档暂停；无驱动/内核修复，未改CI/操作设备/推送/触发CI。
证据scratch/artifact-0121a14/{identity-vsync0-control-persistent.txt,identity-vsync0-control-log-analysis.json,
make-irq0-control.py,identity-irq0-control-verification.json,identity-irq0-control.unixnu.toml}。
文档和计划已更新；以下NEXT均为历史，以本段为准。

## Agent handoff / active work (2026-11-12, own0 PASS; vsync0 next)

**CURRENT：own0持久日志确认进SpringBoard，单独关闭F4未复现panic。下一步只测vsync0。**
logs/sweetlog-0121a14-identity-own0-control.bin SHA=1ec8d2efb16f09b4dd4a7e210467b7abefd7a5542dad8ea99de90d86c554a087；
total242064/flush12。390行参数吻合power1/IRQ2/vsync1/own0；91行source2/profile4，
92行MDSS、105行RPMh、499/2385行hardware vsync、673行SpringBoard，3条touch。
无pool/handoff记录，无panic/ERROR；3USB cmd warning、2HFS clonefile -34，未见signal reason失败。
无MDSS power调用，不能称息屏恢复验收。仍不能排除组合/时序因素，原NULL写未修复。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a14-identity-vsync0-control.img**
SHA=ea6888879198f986b10ca8e2ffffad31c566b180548cd8f6ee30eb7b0b4bb130。
仅own0成功档TOML的sweetfbvsync1→0，power1/IRQ2/own0保留；内核/固件/ADT/其它参数不变。
合法准入，FB不建hardware mailbox而用原timer呈现，MDSS IRQ2仍在；不混淆两个层次。
managed/lifecycle仍关闭，不新增reset/供电周期。原镜像保留，容器/全部FAT仅TOML单参数变化。
只确认是否进系统，成功导sweetlog-0121a14-identity-vsync0-control.bin，无需额外操作。
若失败记录panic寄存器，查硬件消费/软件timer；若成功再拆MDSS IRQ，power0放最后，IRQ2要求power1。
无需push/云编译；retained/combined/supply仍暂停；无内核代码修复/CI更改/设备操作/推送。
证据scratch/artifact-0121a14/{identity-own0-control-persistent.txt,identity-own0-control-log-analysis.json,
make-vsync0-control.py,identity-vsync0-control-verification.json,identity-vsync0-control.unixnu.toml}。
文档/计划已同步；以下NEXT均为历史记录，以本段为准。

## Agent handoff / active work (2026-11-12, live ABL identity PASS profile4)

**CURRENT：identity-slot-control进系统，持久日志确认source2/profile4，下一步只测own0。**
logs/sweetlog-0121a14-identity-slot-control.bin SHA=97217777a6fccefc2718323b1010acd73ae2ea044280544a5f74f86b5c1b3077；
total253260/flush12。第85行真实ABL source2/profile4→PV allowlist label
 dsi_r66456_tianmamd_pv_1080_2340_cmd_display。不是DDIC读回，E1选择交接已实机，E7/E8仍缺。
86行MDSS、96行RPMh、392行原参数吻合、685行SpringBoard、802–803行own A接管，
1组off/on rc0+owned resume，5条touch，无panic/[ERROR]。3scanout临时warning、3USB warning、
2旧HFS clonefile -34；关机请求后有两条signal reason分配失败，不能据此推断OOM/HFS根因。
新固件+身份在power1/IRQ2/vsync1/own2下成功；与失败fix身份档ADT完全相同。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a14-identity-own0-control.img**
SHA=5263be89f8c7fc263aeb328f8f7af9b714fa965f64757679df401b8e9ae9abc3。
仅TOML sweetfbown2→0，其他全部参数/内核/固件/ADT不变；power1/IRQ2/vsync1保留。
own0跳过F4池及handoff，临时用ABL buffer定位；不是放弃完整接管目标。实际start准入合法，
managed/lifecycle仍关闭。原镜像保留；header/ramdisk/固件/FAT全量回读仅TOML单参数不同。
只确认能否进系统，成功导sweetlog-0121a14-identity-own0-control.bin；不需附加操作。
若panic记录寄存器，查F4分配/seed/handoff/时序；若成功再拆其余显示开关组合。
不能将关联性当源码根因，仍未修复原NULL写；retained/combined/supply继续暂停。
无需push/编译；无新驱动/内核修复、未操作设备/推送/触发CI，CircleCI未改。
证据scratch/artifact-0121a14/{identity-slot-control-persistent.txt,identity-slot-control-log-analysis.json,
make-own0-control.py,identity-own0-control-verification.json,identity-own0-control.unixnu.toml}。
文档/PD0 E1/完整计划/profile/boot-support入口已同步；以下NEXT均为历史。

## Agent handoff / active work (2026-11-12, firmware-control PASS; slot-only next)

**CURRENT：用户确认firmware-control进系统，持久日志支持；下一步只测identity-slot-control。**
logs/sweetlog-0121a14-firmware-control.bin SHA=b3d39299d4e677bd350457f842a1568ed00a407b1851c7578b417e908406d1c4，
total256574/flush10，区别于上轮；85–86行source0/profile0+MDSS发布、96行RPMh绑定，
392行参数与成功TOML一致、672行SpringBoard、1099–1100行own A接管；2组off/on rc0，
owned resume成功，17条touch，无panic/[ERROR]。3条scanout临时warning后成功接管、
3条USB cmd warning、3条旧HFS clonefile -34；external1/charging0，未验收充电。
新固件在成功配置能启动；无slot所以身份adapter早退，不能排除HOB/FDT路径或称修好NULL写。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a14-identity-slot-control.img**
SHA256=5fb1501e53ae1af3483659eb8d99e35d324d96f0444623e7ba3ae4b1ce7500ba。
以成功firmware-control为底，仅ADT加96-byte零身份slot（含属性头总增132bytes），
固件/内核/全部TOML及参数原样；power1/IRQ2/vsync1/own2保留，菜单旧0121a13属刻意。
新ADT与失败fix身份档完全一致，去掉唯一slot重建原ADT；3个FAT文件全量比较仅ADT变，
boot容器/各payload回读通过，原control未改。无NoC/Pins/Touch，不启新reset/供电周期。
实际新固件parser与仓库相同，真实新ADT+20种合成记录ASan/UBSan验证只改96byte/canary完好；
只是本地合成身份测试，不是本机ABL证据，不证明HOB/FDT运行正确。
用户确认能否进系统，导sweetlog-0121a14-identity-slot-control.bin查早期source2/profile1..4。
USB可用但可能漏早期行；不重测插拔/旧USB/BUG002/F3/半小时，后三档继续暂停。
成功后再定位原显示开关组合；失败集中身份路径，仍不定罪具体函数。
无需push/云编译；未改内核/驱动/CircleCI、未操作设备/推送/触发CI。
证据scratch/artifact-0121a14/{firmware-control-persistent.txt,firmware-control-log-analysis.json,
make-identity-slot-control.py,identity-slot-control-verification.json,identity-slot-native-check.*}。
文档docs/PANEL-D1-0121A14.md及架构/计划已同步；以下NEXT都是历史阶段。

## Agent handoff / active work (2026-11-12, fixed identity still PANIC)

**CURRENT：用户报告fix-panel-identity仍panic。NoC配置修正未能解决NULL写；根因未定。**
新PC=0xfffffe0007b95478/FAR0/ESR0x96000045与旧一致；实际memmove向0写指令。
LR原文0xfffffe0077c8c68共15hex位，不补位声称与旧HFS调用点完全一致；本次无独立panic日志。
identity-fixed-panic-report.json保留原文。成功kernel-control的持久日志也已解码：
logs/sweetlog-0121a14-kernel-control.bin SHA=b87b2d077b8a0a453fbf835eacb1b51aab84e043ef7b03e5a1dcc93f4cb7f044；
85–86行source0/profile0及MDSS发布、96行RPMh绑定、178行pool、1098行own A接管，
392行boot-args与成功TOML完全相同。USB缺早期的限制由这份persist补足，仅针对成功档。
原成功→fix身份档仍同时变固件/身份slot及power-IRQ-vsync-own(1/2/1/2→0/0/0/0)。
已确认fix ADT删去唯一96-byte零slot即与成功ADT逐字节相同；不能忽略显示参数/分配路径。
**NEXT：只测 /home/shenli/work1/out-bs/boot-16k-0121a14-firmware-control.img**
SHA256=fbff2365215791f064787262536a224421b07b07bc5d2f4e6a1fe4c328db72d3。
该档以成功kernel-control为底，仅boot kernel段换新Mu固件；整FAT ramdisk逐字节相同，
内核/ADT/所有TOML和boot-args保留（菜单旧标题0121a13是刻意保留，不是旧内核）。
无身份slot，adapter按代码不读HOB/FDT。boot header/整ramdisk/各SHA回读通过，原control未改。
尚未实机，只测是否进系统；成功导live-0121a14-firmware-control.txt，失败记录panic寄存器。
成功才在此基础仅加slot；失败查新固件/内存交接，不声称已定位到根因，不修HFS/磁盘。
无需push/云编译；身份/retained/combined/supply均暂停，不重测旧USB/BUG002/F3/半小时。
证据scratch/artifact-0121a14/{make-firmware-control.py,firmware-control-verification.json,
kernel-control-persistent.txt,kernel-control-persistent-analysis.json,identity-fixed-panic-report.json}。
只更新文档与本地镜像，未修改内核/驱动/CircleCI或操作设备，未push/触发CI。
详情docs/PANEL-D1-0121A14.md。以下NEXT均为历史阶段，当前以本段为准。

## Agent handoff / active work (2026-11-12, kernel-control PASS; identity packaging fix)

**CURRENT：用户确认kernel-control成功进系统；本地已修正identity配置，尚未实机。**
USB日志logs/live-0121a14-kernel-control.txt SHA256=1f37328114f5a3f74a76556cefc7a982858217cac202bbfeed8fcac786561f95，
223576 bytes；425行SpringBoard、850–851行fresh PP_DONE/own A接管、10条touch、2条vsync；
无panic/data abort/[ERROR]。从用户态开始，早期参数/服务日志缺失；不称全量启动日志验证。
6条USB cmd warning、3条临时scanout warning、10条旧式HFS clonefile -34，末段charging0；
不是NULL panic重现，也不称HFS完全正常或充电通过。kernel-control-log-analysis.json存底。
实际云端setupNoC要求resource2/clock1；原identity full ADT声明NoC却resource1/clock0，
必然start失败且到不了setupLifecycle。这是确定的打包错误，HFS NULL因果仍未证实。
不能因HFS源码没改或kernel-control成功就排除新驱动间接影响。
修正matrix：identity单独--panel-identity ADT（resource/RPMh/零slot，无NoC/Pins/Touch），
pins0/touch0，其他identity参数保留；后三档完整ADT及参数不变。make_profile支持单档ADT。
实际源码ASan/UBSan NoC回归含两种配置start/free准入和无写入；ADT suite、bash语法通过。
隔离NONBOOTABLE包装fixture及四个真boot完整FAT/header/kernel/ADT/TOML/固件回读通过。
**NEXT：仅由用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a14-fix-panel-identity.img**
SHA256=860586416eb546e8e714a81c9fa4247ffc999ed52c2f786d7b8e29fd76fb67e5。
正常进入后基本画面/触摸/按键即可，导出sweetlog-0121a14-fix-panel-identity.bin持久日志，
核对早期SWEET PANEL source2/profile1..4；USB仍开着，但live可能漏早期行。
后三档仍暂停，不能宣称本修正已消除panic；不重测旧USB/BUG002/F3/半小时。
内核仍7943a67e...68322f，新固件仍8e1e65d8...9561e；无需push/云编译。
原四档、olduefi与成功kernel-control保留，原四档/control SHA已再核对。
底稿scratch/artifact-0121a14/{kernel-control-log-analysis.json,check-packaging-fixed.py,
packaging-fixed-results.json,packaging-fixed.txt,verify-boots-fixed.py,boot-fixed-verification.json}。
不改HFS/充电/USB、未操作设备/推送/CI；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
详情docs/PANEL-D1-0121A14.md；以下是历史记录，当前NEXT以本段为准。

## Agent handoff / active work (2026-11-12, 0121a14 identity panic)

**当前执行点更新：用户报告olduefi对照仍panic。新固件不是必要条件，本轮内核间接影响不能排除。**
完整源码树已比对：0121a13 5274份、0121a14 5292份，31份不同（18新增13修改），全部在
显示/资源/触摸协调与注册，不仅0201的7份。full-source-comparison/comparison.json及脚本已存。
HFS/通用分配器不变不等于显示改动无关；初查新服务启动/匹配/分配路径未找到确证，不能称排除。
NEXT：用户测/home/shenli/work1/out-bs/boot-16k-0121a14-kernel-control.img，
SHA256=63c2137e7993d97e5c1bd13cccc95bb6f61d8b7a88706e6519b6c39957df432a。
它只把0121a13原panel-resources镜像FAT内kernel替换为本轮kernel，其余UEFI/ADT/TOML逐字节
保留，旧标题0121a13也是刻意保留；新Pins/Touch节点缺席，不启其consumer。
回读kernel SHA7943a67e...68322f通过，原旧镜像未改。尚未实机，无需云编译。
成功再拆新ADT/服务/参数；失败需核对原0121a13在当前设备状态下结果，不直接认定文件系统损坏。
artifact-0121a14/make-kernel-control.py与kernel-control-verification.json为证据。
旧UEFI档暂无独立PC/LR/FAR，按用户“同样panic”记录，不虚构逐寄存器一致。
以下为该结果之前的逐步记录，当前NEXT以上段为准。

**最新：** 用户修正ESR=0x96000045，确认FAR=0、x0=0、x1=0xfffffe205cd42040、x2=0xfe0、
LR=0xfffffe00077c8c68。LR准确匹配实际memmove call返回，PC为stp x12,x13,[x0]，
确认HFS end_transaction的memcpy目的地址为NULL；same-EL write level1 translation fault。
目的来自blhdrim->buffers+tbuffer_offset，分配失败vs后续破坏未定，不要求继续找backtrace。
全sweet_hfs及IOLib.cpp/kalloc.c共83份源码与0121a13完全相同，证据hfs-and-allocator-comparison.json。
本轮未改HFS/分配器，但新显示服务/ADT和新Mu RELEASE FD可有间接影响，不能仅凭故障点定根因。
NEXT：已生成/home/shenli/work1/out-bs/boot-16k-0121a14-panel-identity-olduefi.img，
仅换回旧DEBUG firmware，实际ramdisk（kernel/ADT/TOML）与identity逐字节相同。
SHA256=22a39d30891c125174169209bf491a886547e0be3fc72d8cfd64dcc0313be7de；
olduefi-control-verification.json记录完整容器对比；无需新编译，不验证live身份，只测能否启动。
尚未实机，不测后三档、不修改HFS/文件系统或做修复。以下首轮定位过程保留历史抄录与不确定性。

**CURRENT：用户报告panel-identity未进入系统即panic，暂停后三档；需要屏幕panic内容。**
已取得首行：PC=0xfffffe0007b95478命中本轮memcpy/memmove+0x48，指令stp x12,x13,[x0]。
LR=0xfffffe00077c8c60在HFS end_transaction附近，但call返回点为...8c68，需核对末位。
原始caller=0xffffe0007b36a28少一个f；若补齐为fffffe0007b36a28则命中sleh_synchronous_sp1，未确认。
当前已请求FAR/ESR、x0/x1/x2、backtrace、slide（若有）及核对PC/LR。
无slide定位为初步结果，未确认错误地址/访问原因，不归因面板寄存器/文件系统损坏。
证据新增scratch/artifact-0121a14/{panic-address-analysis.json,panic-memmove.txt,panic-end-transaction.txt}。
已解码logs/sweetlog-0121a14-panel-identity.bin；它与旧0121a13-panel-resources.bin整文件
逐字节相同，SHA256=461a537553b33b12f8740035985eaa45d7442ed62820c397085e70c48fd6e236，
total440091/flush77。仍为旧resources参数及SpringBoard正常运行，无新identity参数/本次panic栈。
不能据旧日志认定本轮进系统或解释新panic；本次尚未更新该持久区，不能确定UEFI/XNU故障阶段。
NEXT：用户再次启动同一identity镜像，从屏幕记录panic首行、PC/LR/backtrace；不测其它档。
现有镜像/参数未改，未要求重新编译；不推测根因为面板供电/reset（首档未启用这些新动作）。
证据scratch/artifact-0121a14/{panel-identity.txt,panel-identity-log-analysis.json}；
docs/PANEL-D1-0121A14.md已记录首档失败，以下“待实机”段落是打包时状态。

## Agent handoff / active work (2026-11-12, 0121a14 artifacts)

**CURRENT：0121a14实际内核核对通过，四档boot已打包并回读。当前只测panel-identity，等待用户日志。**
用户已完成77c7631/0201云编译并下载kernel/buildtree；无需为本轮再push/编译。
78份源码逐字节匹配scratch/framebuffer-0201/verify（含7份0201变更），114原CI marker保留。
最终Mach-O有7个供电协调器/RPMh/Pins关键方法的定义，新增运行marker及SHA256SUMS通过。
kernel SHA256=7943a67e340c00715f1d32be891204d9e9a0c209c93d9f59d9fed70aaf68322f，
17,740,144 bytes，区别于0121a13。未提供全量build-log/CI run ID，不宣称全量告警已查。
四档均在/home/shenli/work1/out-bs/，共用实际kernel、NoC/Pins/Touch/identity ADT及新Mu RELEASE固件：
 boot-16k-0121a14-panel-identity.img   lifecycle0 metadata-only
 boot-16k-0121a14-panel-retained.img   lifecycle2 全部票保留
 boot-16k-0121a14-panel-combined.img   lifecycle5 DATA/link组合
 boot-16k-0121a14-panel-supply.img     lifecycle6 panel两票撤销恢复
完整header v1/page2048、firmware/ramdisk及FAT内kernel/ADT/TOML回读一致，无重复参数；
后三档仅lifecycle不同。identity的面板写入/接管开关0，96-byte身份slot全零，不猜PV。
新Mu manifest=/home/shenli/work1/out-bs/panel-uefi-0201-final/panel-uefi.json，blob SHA256=
8e1e65d891a831b6f9b9005701f339d80330378c21520ec6f6ef4159d8e9561e。
原kernel-32m.blob/baseline ADT未改，BootShim与两个append DTB保留。USB ACM/按键/battery runtime3保留。
**NEXT：用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a14-panel-identity.img。**
正常启动后简单确认画面/触摸/按键，导出live-0121a14-panel-identity.txt或
sweetlog-0121a14-panel-identity.bin。只需本档，不要求息屏/充电插拔/长测。
查SWEET PANEL: ABL selection source=2 profile=1..4；来源是ABL选择，不是物理DDIC读回。
缺记录/无效/未知/歧义或新固件启动失败，停后档；通过后再retained→combined→supply。
尚无设备结果，不宣称reset/供电恢复已通过；E7/E8、物理共享偏压仍缺。
共享L3/L5保留，本panel撤票/ACK不证明物理rail-off；PD4冷NoC/MDP、ABL首帧/console/panic退出、
故障后受控释放及完整IOPM仍缺。A单缓冲/30Hz保留，未称修复卡顿/BUG003。
证据scratch/artifact-0121a14/{verification.json,source-compare.json,actual/xnu,kernel-symbols.txt,
kernel-symbol-verification.json,packaging.txt,boot-verification.json,boot-verification.txt,mdss.adt,
verify-artifacts.py,verify-boots.py,*.unixnu.toml}。二进制/设备树/Apple产物不入git。
详见docs/PANEL-D1-0121A14.md；计划/架构/0201文档已同步。
agent未push/触发CI/操作设备，CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
不重测旧USB/BUG002/F3/半小时，不启动Android或扫未知PMIC。

**以下0201实现与旧阶段记录按当时范围保留，当前执行点以上段为准。**

## Agent handoff / active work (2026-11-12, latest)

**CURRENT：0201供电周期接入统一生命周期协调器、本地验证和完整Mu FD构建已完成；现在需要用户push云编译。**
父提交8722913；0201-mh2lm-panel-supply-lifecycle.patch SHA256=
1a79089011539675d801d09059091d9e2294ddd7268b0274dd1435618af1596e，7份源码。
新增SweetMDSSPanelSupply.inc，复用原MDSS owner/gate/timer及typed Pins/RPMh/Touch，不建新provider/
线程/映射/全局桥。显式sweetpanellifecycle=6，原0–5保留、默认关闭。
OFF完整表与等待→warm引脚准备/pre5ms→reset park实际低保持>=5ms→mode5 config/clock/link/DATA
停驻→VPNL ACK→VDDI ACK→post5ms。ON先同provider和低保持/停驻账本→active-low无reset脉冲/
pre5ms→VDDI ACK+20ms→VPNL ACK+5ms→DATA/link/clock/host重建→reset/完整ON/最新亮度/
fresh像素/实际TCM恢复。L3/L5共享UFS/USB保留，ARC/config floor/boot/RSC/core通路保留。
只撤本panel consumer两票；aggregate可仍enabled，不称物理rail-off。rails15→13→12及逆序恢复，
新增released/aggregate mask、每rail ACK token、resetHeldLow/parked/restored。FB核对联合证据。
低保持期间MDSS子timer亦实时查Pins。父失败先关闭generation，再同步cancelPanelVote，最后撤
Pins/Touch token；已成功ACK但父未读取的真实账本补记，未确认token/旧票/uncertain保留。
无补电/回滚/重试或一次性takeover重跑，DMA pin故障保留至reboot。
23套检查全过（22实际源码ASan/UBSan+PythonADT），新联测含四profile多轮/独立MMIO时间戳、
引脚各阶段取消/stop/withdraw、两方向两rail各阶段取消、ACK先于父cancel、子先于父丢fence、
缺ACK/posted-write/超时/重入/保电consumer/停驻漂移。LSan ptrace关闭，另查对象/映射/通知/
分配/模拟reboot DMA pin归零。base连续0200verify、严格重放无offset/fuzz、114 CI marker保留。
RPMh/Pins/MDSS/FB前后8个真实XNU/KDK arm64对象和符号通过；非整核链接。
warnings RPMh1326/Pins1171不变，MDSS1527→1531、FB1397→1398，5处有界数组提示已审，无抑制。
完整Mu mh2 RELEASE FD成功构建（隔离输出，参考仓库既有dirty未清理）；0195 loader adapter
已纳入真实DarwinBoot PE→FFS→FVMAIN→guided压缩→最终FD，全链核验。新builder为
 tools/build-mh2lm-panel-uefi.py。保留原BootShim112字节和两个append DTB，不改旧kernel-32m.blob。
manifest=/home/shenli/work1/out-bs/panel-uefi-0201-final/panel-uefi.json
blob SHA256=8e1e65d891a831b6f9b9005701f339d80330378c21520ec6f6ef4159d8e9561e
FD SHA256=fadb5b59f3ec22f67ebac3e33babc642d372e42cdb004c5730fdaf376d43e48f
FD构建成功不代表手机启动；不把二进制/设备DTB/Apple产物入git。
新增matrix --panel-lifecycle，要求SWEET_PANEL_UEFI_MANIFEST并核验blobhash/ABI/fullFD；四档
同kernel/ADT/新firmware，identity(mode0 metadata-only，所有写入/接管开关0)、retained(mode2)、
combined(mode5)、supply(mode6)。identity只读live ABL选择，ADT预留96-byte零slot，不猜PV。
隔离TemporaryDirectory用明确NONBOOTABLE fixture核查实际FAT/mkbootimg容器、四档参数、同源
内核/ADT/固件、缺manifest/错误hash/旧kernel拒绝，fixture镜像已删除，无新可刷boot。
**NEXT：用户push本地提交并在CircleCI编译；下载新kernel+buildtree后agent核对并执行：**
 SWEET_PANEL_UEFI_MANIFEST=/home/shenli/work1/out-bs/panel-uefi-0201-final/panel-uefi.json \
 tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-<LABEL>.gz <LABEL> --panel-lifecycle
先identity日志确认live source/profile，再retained验E7，再combined，最后supply验E8相关证据。
任一失败停后档，用户负责fastboot；不重测半小时/F3/旧USB或BUG002，不要求自动息屏。
E7/E8、物理共享偏压仍未实机；PD4冷NoC/MDP、ABL首帧/default surface/console/panic退出、
故障后受控释放与完整IOPM仍缺，不能称完整驱动完成。A单缓冲/30Hz保留，未称修复卡顿/BUG003。
未push/触发CI/设备操作；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
USB/充电/按键/mux/TCM硬件IRQ策略未改；不启动Android、不扫未知PMIC。
新文档docs/PANEL-SUPPLY-LIFECYCLE-D1.md，路线图/架构/计划/PD0/lifecycle/power/供电归属及
boot-support README、BUILD-GUIDE已同步。底稿scratch/framebuffer-0201/{base,edited,verify,
prepare-patch.py,compile.py,native-checks.py,run-tests.py,finalize-checks.py,check-packaging.py,
verification.json,base-continuity.json,tests.json,native-results.json,warning-review.json,delivery.json,
packaging-results.json,*compile.txt,*symbols.txt}。

**以下0200及更早记录按当时范围保留；当前以0201及其保留的旧路径为准。**

## Agent handoff / active work (2026-11-12, latest)

**CURRENT：0200供电事务的实时Pins/Touch依赖校验已本地通过，尚未接入协调器撤票/恢复，未实机。**
父提交2f06e93；0200-mh2lm-panel-supply-fences.patch SHA256=
4fcde091139853f08e405c1c0dcf3eec1ebbf6021fa8d04d85b8f1c6051fa6f1，四份Pins/RPMh源码。
base逐字节连续于0199verify；严格应用无offset/fuzz，114原CI source marker保留。
Pins新增typed copyLowStatus，在自己的gate读取CTL/IRQ/latch及独立input，仅接受成功Parked或
自己Parked后Active-low的同client/token，不能用缓存成功冒充当前reset低保持。旧token/pending/
high/inactive/stop/poison拒绝；已成功低保持后发现漂移则Failed/uncertain/poison，无引脚写入/回滚。
RPMh新增SweetPanelSupplyFence/requestPanelVote/cancelPanelVote，只接受VDDI/VPNL，沿原唯一
TCS owner、gate/timer、聚合及fresh ACK。复制本请求的leaf refs和ID/token，不保存caller栈；
admission、每timer入口、逐TCS写、trigger、最终cleanup完成前重查Pins实时低和真实TCM quiesced。
请求成功/失败/no-op/拒绝/取消/stop释放两份依赖引用；取消仅本consumer/token panel请求，
不取消普通LDO或其它请求。新请求编程前丢fence不标硬件uncertain，开始写/在途/trigger后保留
部分TCS、旧ACK票并报uncertain，原RPMh stop保守语义保留。已trigger不能撤回，不补电/复位/重试。
锁序consumer→RPMh→Pins/Touch，leaf无反向调用。不新增provider/workloop/线程/MMIO/全局实例桥。
这是逐写实时校验，不是跨gate硬件原子锁，不能阻止最后一次读后硬件立刻漂移；后续协调器
失败收口需先同步cancelPanelVote，再撤销Pins/Touch token。父timer取消不等于子事务已停止。
普通requestVote/ARC/CN0未改，协调器尚未调用新接口；mode0–5/MDSS/FB/TouchComm源码逐字节未变，
没有新增档位或正常VDDI/VPNL撤票时序。0199共享L3/L5最后disable保护保留，不把接口作为供电许可。
22套检查通过：21实际源码ASan/UBSan+PythonADT，无最终报告。新检查覆盖实际Pins/RPMh/TCM，
成功Parked/Active-low、缓存与实际输入分离、四在途阶段×12子先于父故障、单timer写间漂移、
trigger和最后cleanup边界、取消/stop/ACK丢失/期限、begin内部拒绝/no-op/其它consumer保电/
并发owner拒绝/refs释放，旧0–5资源/link/FB回归通过。LSan ptrace环境禁用，另查对象/映射/
通知/分配和模拟reboot DMA pin归零。真实XNU/KDK arm64 RPMh/Pins/MDSS/FB base+final八对象及
新方法符号通过，非整核链接或硬件验证。RPMh warnings1325→1326仅新增1处fClients[i]数组提示，
位于i<8匹配循环，已审无抑制；Pins1171/MDSS1527/FB1397不变。
**NEXT：将panel两票接回原统一协调器：完整OFF→reset低保持→原mode5 link/DATA停驻→
VPNL/VDDI逆序撤本票；ON保持低→VDDI ACK+20ms→VPNL ACK→原DATA/link/clock/host/reset/
ON/brightness/pixel/touch。补LG前后等待、部分释放账本、子取消顺序、FB联合证据，保L3/L5。**
上面序列仍待实现，不能宣称0200已完成rail-off。E7触摸/E8物理共享偏压/本机live profile未实机，
真实rail-off/故障释放、PD4冷NoC/MDP、ABL buffer/console/panic退出与完整IOPM仍缺。
0195实际Mu→DarwinBoot身份/ADT slot仍待合入完整FD，仅新XNU配旧UEFI不能提供live身份。
无新FD/boot、push/CI/设备操作，暂不要求单开云编译/刷机；CircleCI blob保持
 da0b6c0947a66f03491b3a9023590b49665292df。USB/按键/电池/mux/触摸IRQ策略未改。
不启动Android、不扫未知PMIC、不撤其它consumer/ABL票；完整驱动先于零拷贝，A单缓冲/30Hz保留。
新文档docs/PANEL-SUPPLY-FENCE-D1.md，计划/架构/PD0/协调器/资源/归属文档已同步。
底稿scratch/framebuffer-0200/{base,edited,verify,prepare-patch.py,compile.py,native-checks.py,run-tests.py,
finalize-checks.py,verification.json,base-continuity.json,tests.json,native-results.json,warning-review.json,
delivery.json,*compile.txt,*symbols.txt}。新联测tools/tests/test-mh2lm-panel-supply-fences.py。

**以下0199按当时范围保留；当前以0200及其明确保留的0199/0198运行路径为准。**

## Agent handoff / active work (2026-11-12, latest)

**CURRENT：0199供电归属核对及共享L3/L5最后disable保护已本地通过，未实机；没有新增rail-off档。**
父提交dedfe49。0199-mh2lm-shared-rail-disable-guard.patch SHA256=
91852c738d0f5e244f3615dce2f6b46cb85bcc7b07102a375a353ee35d9da5a5，2份源码，
base逐字节连续于0198verify；严格重放无offset/fuzz，114原CI source marker通过。
LG基线d4f335e13d73c4aa2af85581a6ec11b670ee5b94：MH2LM common-storage UFS的
vdda-phy=pm8150_l5且always-on，vdda-pll=pm8150l_l3；USB HS/QMP也共用L5/L3，DP亦有同源。
因此fManagedRails四票不能一起撤；UFS/USB尚未通过SweetRPMh登记依赖，不伪造baseline票。
**L14的vddio映射是主/次DSI节点，不是DP；本轮曾在commentary误读，已明确更正。**
四R66456 profile供电表仅VDDI/VPNL；use-labibb走LG原power_info，不能据日志字符串推断独立
LAB/IBB。common-pm禁用LCDB，panel尾部PMI8998片段在注释内；未知外部偏压仍未确认。
MH2LM touch用GPIO152/59/54/122，LG S3618独立power切GPIO；不能从不同GPIO推断物理电源隔离。
现有touch暂停不是deep sleep/rail-off证明。旧android-mh2lm.dts UFS disabled/无最终overlay，
来源未重新认证，不能充当当前最终DT或据此否定LG板级连接。参考源码SHA和入口见底稿reference-evidence.json。
SweetRailInfo追加lastDisableRequiresHandoff：Host/PHY=true，VDDI/VPNL=false；false不是关闭许可。
provider属性SweetRPMhLastDisableBlockedMask=0xc。原gate/唯一TCS owner聚合后，若已owned且
aggregate enabled的共享rail将变disabled，在分配token/修改请求或账本/旧status/MMIO前
返回NotPermitted、输出token0；旧票/状态不变，detach仍Busy。空历史撤票仍no-op；仍有另一
consumer enabled则可原路径撤自己的票；panel两路typed disable/fresh ACK保留。无bypass参数，
无自动基线consumer/降压/复位/重试；startup/metadata不投票。ADT/用户ABI未变，内部同核编译字段扩展。
mode0–5流程未改；这不是旧USB/UFS/充电bug的修复或根因结论，也未测新硬件。
21套检查全过：20实际源码ASan/UBSan+PythonADT，无最终报告；新断言覆盖四rail/all8consumer、
共享最后撤票重复拒绝无副作用、非最后撤票、旧token/status保持、无历史no-op、panel disable真ACK；
原组合mode5及全部资源/link/FB回归通过。LSan ptrace限制禁用，另查对象/映射/通知/分配归零。
真实XNU/KDK arm64 RPMh/MDSS/FB base+final六个对象与关键符号通过，非整核链接/硬件验证。
RPMh warnings1321→1325净增4有界数组提示，已审枚举0..3/固定数组4项；MDSS1527/FB1397不变，无抑制。
**NEXT：保留共享L3/L5，补panel reset-low证明及VDDI/VPNL受控撤票/恢复，再衔接0198组合事务。**
LG lp11-init完整off顺序：panel unprepare内reset_off先拉低reset并等待→停host/link/PHY/core→
post_unprepare/power_off逆序关VPNL/VDDI，不能复制通用weak或颠倒成先断电再reset低。
下一档需原pins服务真实低保持/同代touch fence、精确部分释放账本/失败停止、typed LDO ACK、
恢复供电后再走既有DATA/Link/Clock/Host/reset/ON/pixel/touch；不能盲撤共享模拟电源。
正常rail-off、E7触摸/E8物理共享偏压、PD4冷NoC/MDP、ABL buffer/console/panic退出及完整IOPM仍缺。
0195实际Mu→DarwinBoot身份补丁和ADT slot仍须纳入后续完整FD；只新XNU配旧UEFI无live profile。
无新FD/boot、push/CI/设备操作，暂不要求push/刷机；CircleCI blob仍
 da0b6c0947a66f03491b3a9023590b49665292df。USB/电池/按键/mux/触摸IRQ代码未改。
不启动Android、不扫未知PMIC、不为孤立接口开CI；A单缓冲/30Hz复制保留，完整驱动先于零拷贝。
详见docs/PANEL-SUPPLY-OWNERSHIP-D1.md；计划/架构/PD0/power/组合文档已同步。
底稿scratch/framebuffer-0199/{base,edited,verify,prepare-patch.py,compile.py,native-checks.py,run-tests.py,
verification.json,base-continuity.json,reference-evidence.json,warning-review.json,native-results.json,
tests.json,delivery.json,*compile.txt,*symbols.txt}。后续新测试若需真实断电，不能仅凭本轮软件保护证明安全。

**以下0198按当时范围保留，当前以0199及其明确保留的0198运行路径为准。**

## Agent handoff / active work (2026-11-12, latest)

**CURRENT：0198数据票停驻与host/PHY/PLL硬件下电已组合，本地通过，未实机；完整驱动未完成。**
父提交0265f05；0198-mh2lm-panel-combined-suspend.patch SHA256=
83f012309c8f6dd4cc06cd6f5ee3722dfca3ec4bed6a502efc19ec84e2f37ba3，6份源码，
base字节连续于0197verify；严格重放无offset/fuzz、114原CI source marker通过。
新增显式sweetpanellifecycle=5，原0–4语义保留，要求mode4全部live身份/pins2/touch2/
own2/power1/IRQ2/dsi1/managed1/core2/host1/link2及原resource/clock/NoC依赖，无猜PV/降档。
OFF：touch静止/IRQDMA drain/完整OFF等待→config-only NoC floor Held→managed clock Released
确认清旧token→ManagedParkLink→原host/PHY/PLL真实关闭并验证Dormant→managed DATA NoC
Released确认清旧token→resourcesParked+linkPoweredDown联合完成。
ON：同代Dormant与配置fence→新DATA NoC Held（之前不reset）→ManagedRestoreLink→原PHY/
PLL/root/branches真实重建与snapshot提交、清暂停mask→新managed clock Held→原host reset/
配置读回→面板activate/reset/完整ON/最新亮度/fresh像素/实际TCM恢复。没重跑一次性takeover。
仍沿原唯一MDSS owner/gate/timer和typed执行层，新增两个内部衔接状态，无新provider/线程/映射/
实例桥或第二套PLL/host/NoC算法。旧takeover状态机显式跳过新bridge。
普通Clock/NoC暂停API仍拒绝改票；私有lifecycleDataBandwidth构造栈上typed标志，无全局临时
bypass；NoC检查mode5准确phase/state/指定DATA token或输出、零managed clock/中间token、
Dormant/config floor/完整fence。不能释放floor/boot/RSC或在PHY关闭时取得DSI0 clock。
boot/RSC/config floor/LDO/ARC/core通路保留，config token跨周期复用，DATA/clock使用新token。
释放本票不等于其它consumer DATA降零或PMIC rail-off；其它link票/alias仍在DDI副作用前拒绝。
FB mode5要求两能力与两组联合证据，不能mode3/4单项或普通Complete冒充；暂停票与物理Ready
继续区分。每子timer重查同代participants/touch/真实非link clock/core、GIC/TEauto/DSI/CTL/line/
自有A/SMMU/期限。原总16s和资源4s/link1s及原内部期限保留。
失败/取消/stop/provider撤回关闭本代，保留在途账本/部分硬件/DMA pin，无自动回退、复位收尾或
清poison。已提交RPMh仍可后来ACK，不宣称取消撤回已提交事务。
21套本地检查通过：20实际源码ASan/UBSan+PythonADT，无最终报告。四profile各4轮，验证两个
handoff顺序、DATA ACK前不reset/PHY就绪前不取clock/clock就绪前不reset host、floor/boot/RSC/
ARC稳定与新票、暂停公共接口拒绝、每资源/link阶段取消/stop/撤回、ACK丢失/硬件响应失败、
子timer先运行漂移/期限、其它票保护、token耗尽、停驻漂移/取消、FB联合证据拒绝。LSan环境
ptrace限制禁用，另检查对象/映射/通知/分配和模拟reboot DMA pin归零。
真实XNU/KDK arm64 MDSS/FB base/final对象及关键符号通过，非整核链接/硬件结果。
MDSS warnings1524→1527净增3数组提示（新5处/替换旧2处）已审边界，FB1397不变，无抑制。
**NEXT：继续PD3正常rail-off/故障受控释放，先核对共享供电/偏压E8所有权，再PD4冷NoC/MDP。**
NoC solver/sequencer与core配置仍温态保留，不宣称冷NoC；未知labibb/触摸/ABL票不盲撤，
不启动Android、不扫描未知PMIC。live身份/E7复位触摸/E8仍未实机，完整驱动未验收。
0195实际Mu→DarwinBoot身份补丁/ADT slot仍须合入后续FD，只有新XNU配旧UEFI没有live身份。
无新FD/boot、push/CI/设备操作，暂不要求push或刷机；CircleCI blob仍
 da0b6c0947a66f03491b3a9023590b49665292df，USB/按键/电池/mux/触摸IRQ策略未改。
ABL buffer/console/panic退出与完整IOPM仍缺；A单缓冲/30Hz复制保留，旧滑动/BUG003未修复，
不重测旧USB/BUG002/F3/半小时；完整驱动先于零拷贝。
详见docs/PANEL-COMBINED-SUSPEND-D1.md，计划/架构/PD0/协调器/资源/Link/Clock/NoC已同步。
底稿scratch/framebuffer-0198/{base,edited,verify,prepare-patch.py,compile.py,run-tests.py,native-checks.py,
verification.json,base-continuity.json,tests.json,native-results.json,warning-review.json,delivery.json,
*compile.txt,*symbols.txt}；新联测tools/tests/test-mh2lm-panel-combined-suspend.py。

**以下0197按当时范围保留，当前以0198为准。**

## Agent handoff / active work (2026-11-12, latest)

**CURRENT：0197 host/PHY/PLL实际关闭与重建已本地通过，未实机；保留全部票，未完成rail-off/完整驱动。**
父提交9791218；0197-mh2lm-panel-link-power-cycle.patch SHA256=
efdca7364953775c661ed8dda2e91e3638aad3b570b132e589fd8e934bc40bb7，12份源码，base字节连续于0196verify；严格应用无offset/fuzz/114原CI marker通过。
新增sweetpanellifecycle=4，默认0及原1/2/3语义保留；4要求live profile/pins2/touch2/own2/
power1/IRQ2/dsi1/managed1/core2/host1/link2及原resource/clock/NoC依赖，无猜PV/降档。
实际OFF：原touch fence/显示IRQDMA静止/完整OFF及等待→typed host disable（command先停，
0x200b3f dynamic force→controller停→恢复0x23f，每步读回）→lanes idle→pixel/intf/byte关→
roots确实off/无update→PLL global/控制off→新LOCK清零→bias off>=250ns→LPRX/lane/PHY blocks关。
成功终态LinkDormant/HostDormant，读回gates/roots/PLL/bias/PHY/host/reset未assert才发完成。
OFF无PHY/面板reset、无撤票；stopLink/stopHost仍只取消，无清理硬件副作用。
实际ON只接受自己同provider成功Dormant且再次读回一致→原PHY reset/refgen/完整PHY+PLLrate/
SSC/新LOCK/root更新/实际running/受控snapshot提交→原host reset+显式配置读回→原面板
activate/reset/完整ON/最新亮度/fresh像素/真实TCM恢复。自有A布局，未重跑一次性takeover。
全部LDO/ARC/NoC/clock票保留，mode4不叠加mode3释放。首次关link gate前置suspended mask0x70，
localClockStatus除逻辑Held另给hardwareSuspended，localClocksReady=false；暂停时普通clock/NoC
增撤票及DSI/scanout均拒绝。只有实际PHY/PLL/root/branch/parent恢复成功后才清mask；失败保留mask。
DDI副作用前拒绝额外active软件link票/refs异常/硬件DSI1 alias，事务期间重复检查，不越过其它consumer。
新增lifecycle LinkPowerDown/LinkRebuild、SweetPanelLinkPowerCycle能力、linkPoweredDown/linkRebuilt
证据与属性；FB mode4要求供电和data/clock票保留、OFF真实关闭/ON链路及原恢复证据，不能普通Complete冒充。
原唯一MDSS gate/MMIO owner/DSI timer和Link前后半段、Host执行层，无新provider/线程/映射/实例桥。
每协调器/DSI/clock/NoC子timer重查同代participant/touch、保留票、非link clock/core、alias、
IRQ/TEauto/DSIbusy/CTL/line/自有A/SMMU及期限；允许受控link非Ready期间只读MDP/AHB snapshot，
不跳过core/clock准入。原总16s，link段1s、原link250ms/host20ms和各短期限保留。
取消/stop/provider撤回/失败关闭本代、保票/部分硬件/DMA pin，无恢复或复位收尾、回放TE、降档。
加root/divider更新前漂移验证和Dormant保留字段检查，避免旧快照覆盖未知变化。
20套检查通过：19实际源码ASan/UBSan+PythonADT，无最终报告。四版各4轮真实状态驱动关闭/重建，
独立wire/物理响应、OFF无reset/ON必经破坏配置的PHY+host reset、保留token、暂停API拒绝、
TCM/pixel、额外users、每link阶段cancel/stop/withdrawal、halt/unlock/refgen/lock/root/postedwrite/
host失败、停驻取消/撤回/root漂移、子timer先运行依赖变化及FB档4证据拒绝。LSan环境ptrace
限制关闭，另断言对象/映射/通知/分配及模拟reboot DMA pin归零。真实XNU/KDK arm64 MDSS/FB
base+final对象与关键符号核实通过，不是整核链接；warnings MDSS1510→1524净增14数组提示
逐项边界已审，FB1397不变，无抑制。最近仅修改mode4错误状态/提前准入后重跑新联测和MDSS对象，原19路径不变。
**NEXT：组合票释放与硬件下电，继续PD3共享电/偏压E8与PD4冷NoC/MDP独立重建。**
LG完整unprepare仍有core/domain/面板reset-low/regulator撤销，本轮未完成，不用软件stop冒充物理off。
共享labibb/触摸/ABL票归属未确认，不盲撤未知偏压或其它票。live profile/E7/E8仍未实测。
0195实际DarwinBoot身份补丁和ADT slot仍须纳入后续完整FD/boot；只换XNU配旧UEFI无live身份。
无新FD/boot、push/CI/设备操作，暂不要求push/刷机；CircleCI blob仍
 da0b6c0947a66f03491b3a9023590b49665292df，USB/按键/电池/mux和触摸IRQ策略未改。
正常rail-off、故障后受控释放、冷NoC/MDP、ABL buffer/console/panic退出、完整IOPM仍缺；
A单缓冲/B未提交/30Hz复制保留，不宣称旧滑动退化/BUG003修复，不重测旧USB/BUG002/F3/半小时。
详见docs/PANEL-LINK-POWER-D1.md；计划/架构/PD0/协调器/Link/Host/Clock等已同步。
底稿scratch/framebuffer-0197/{base,edited,verify,prepare-patch.py,compile.py,native-checks.py,run-tests.py,
verification.json,base-continuity.json,native-results.json,warning-review.json,tests.json,*compile.txt,*symbols.txt}。

**以下0196按当时范围保留，当前以0197为准。**

## Agent handoff / active work (2026-11-12, latest)

**CURRENT：0196协调器数据资源票反向释放/重新取得已本地通过，未实机；不等于物理断电或完整驱动。**
父提交c675d46；XNU补丁0196-mh2lm-panel-resource-parking.patch SHA256=
6f5e5026aa3ea655ac927ae2010452b22f4db287d615b5258b604bd382f8d32c，8份源码，base逐字节等0195verify，严格应用无offset/fuzz，114原CI marker保留。
新增显式sweetpanellifecycle=3，默认0和原1/2语义保留；要求原full模式全部身份/pins/touch/
managed等依赖及core2，无猜PV/静默mode2回退。start/match无硬件请求，温态takeover不变。
实际OFF：原touch QUIESCED/TEauto停流/IRQDMA静止/完整OFF后，先config-only NoC Held，
再managed DSI0 clock Released，再managed DATA NoC Released。旧token仅在明确Released后清。
实际ON：同代fence与保留资源→DATA NoC新token Held→managed clock新token Held→
原pins activate/reset/完整ON/typed最新亮度/fresh像素/实际TCM恢复。每步失败停止，无回退/自动恢复。
新增独立CN0 300MB/s config floor复用同token，保留RSC/boot clock及LDO/ARC/core/host/PHY；
避免最后DATA票释放时同consumer CN0被降零。NoC typed接口允许所有DATA值为零的config-only票，
拒绝不完整路径/平均值超峰值；聚合尊重其它active票，释放本票不保证DATA BCM为零。
boot lease仍持DSI0 branches，所以managed clock票释放通常不产生物理关钟，未宣称节电。
managed状态新增ParkConfig/ParkClock/ParkNoC/Parked/RestoreNoC/RestoreClock；沿原MDSS
唯一gate/MMIO owner和timer，复用typed资源接口，无新线程/映射/全局实例桥/空闲轮询。
资源段4s，原总16s及NoC1.5s/clock200ms期限保留。协调器及clock/NoC子timer写前都复核
同代participant/touch fence、boot分支/root/core/PHY、GIC/peripheral、TEauto、DSI/CTL/line/
自有A和期限；子timer早于父timer也不能继续漂移后的写入。非资源阶段保持原participants检查。
FB mode3检查SweetPanelDataParking能力及resourcesParked/resourcesRestored/retainedPower/
configHeld/configToken/dataToken/clockToken，与原profile/reset/亮度/pixel/touch证据一起核验。
失败保留剩余/在途账本和DMA pin。已提交RPMh CN0工作可能随后ACK，不假称取消撤销已提交事务。
19套本地检查通过：原18+实际资源票联测，18实际源码ASan/UBSan+PythonADT无最终报告。
四版各4轮完整释放重取/reset唤醒、wire延时/实际C TCM、DATA BCM ACK/新token/floor复用、
其它active票保留、每资源阶段取消/stop/provider撤回、子timer先运行下的物理漂移/期限、
OFF及ON丢ACK、token耗尽、FB能力/证据缺失；LSan ptrace限制关闭，另断言对象/映射/通知/
分配及模拟reboot后DMA pin归零。真实XNU/KDK arm64 MDSS/FB base+final对象编译及符号核实通过；
MDSS warnings1503→1510，净增7为数组提示（NoC原1处替换2处+资源6处）边界已审；FB1397不变。
不是整核链接/实机。USB/按键/电池/mux/触摸IRQ策略未改，CircleCI blob仍
 da0b6c0947a66f03491b3a9023590b49665292df，无push/CI/设备操作/新FD/boot，暂不要求push或刷机。
**NEXT：继续PD3真实host/PHY shutdown与共享电/偏压E8、PD4冷NoC/MDP/独立重建。**
LG完整unprepare含host/link/core/PHY关闭及panel post_unprepare，本轮只释放数据票，没有照搬或
宣称完成该链。不得拿stopLink/stopHost软件取消当硬件off，不盲撤ABL/其它consumer或未知偏压。
0195实际live profile与E7/E8仍未实测；后续镜像必须纳入boot-support独立DarwinBoot补丁和ADT slot，
只新XNU配旧UEFI不能得到身份。同核先确认source/profile，再mode2/mode3分档，不为孤立接口单开CI。
PD3正常rail-off/故障释放、PD4冷MDP/NoC及ABL buffer/console/panic退出、PD5完整IOPM仍未完成。
A单缓冲/B未提交/30Hz CPU复制保留，旧滑动退化/BUG003未宣称修复；不重测旧USB/BUG002/F3/半小时。
详见docs/PANEL-RESOURCE-PARKING-D1.md；计划/架构/PD0/协调器/资源/NoC/clock文档已同步。
底稿scratch/framebuffer-0196/{base,edited,verify,prepare-patch.py,compile.py,native-checks.py,run-tests.py,
verification.json,base-continuity.json,native-results.json,warning-review.json,tests.json,*compile.txt,*symbols.txt}。

**以下0195按当时范围保留，当前以0196为准。**

## Agent handoff / active work (2026-11-11, latest)

**CURRENT：0195完整profile命令/保留供电的sleep-reset恢复与实际DarwinBoot身份传递本地通过，未实机；完整驱动未完成。**
父提交b4ccbbb；XNU补丁0195-mh2lm-panel-profile-lifecycle.patch SHA256=
80d05508fde251a47850c421b44bd187ca5509b46ef4f0313bd6333038d379de，9份源码变化，严格接0194verify/114原CI marker保留。
另交付boot-support/0001-mh2lm-darwinboot-panel-selection.patch，SHA256=
fac5b74dfb30fc947424e648a11074aaaeda1588415e0bbdf84dac046c98c026；7份启动侧文件，适配实际Mu UEFI→DarwinBoot链。
UniXNU parent f86227c39bc4742dccb3c7d5f2eca6c4854740d5 / DarwinBoot component
4ad0c423361fa7added7dd8a5d6589c1c51af3b1；真实原component内git apply --check -p3通过，未改该checkout。
旧shim不是当前入口，本轮交付没有改shim。前期一度按旧shim研究，已纠正，勿再从那里取身份。
ADT --panel-identity只预留96字节全零slot，与pins/touch/NoC组合/旧默认字节不变；没有手填revision参数。
DarwinBoot在ADT验证后/kernel-window复制前，经Mu SEC x0 HOB及完整UEFI RAM范围检查读FDT；
bounded v17结构/字符串/深度/完整tree，bootargs≤8192，FDT≤2MiB，只认唯一msm_drm.dsi_display0。
PP1/DV1/DV2/PV精确label及默认config0/timing0，缺失/未知/重复/畸形分别记录，不复制整条bootargs。
畸形/预填slot拒绝启动，未带slot不查HOB/FDT。XNU验证完整magic/version/source/ID-label/零保留字节。
这是ABL选择证据，不是物理DDIC读回/认证；本机仍没有实际live label，不猜PV，E1没有实机关闭。
四版八表逐字节LG源表：ON PP1 834/20包、DV1 538/16、DV2 538/16、PV694/17，OFF均35/4。
LG ON末尾29→11→150ms不交换；OFF B003→510000后90ms→28→10后150ms，不误写成28后等150ms。
工作区docs/panel.md该旧等待描述已修正（工作区资料，不在本仓库提交范围）。保留CAF版权/GPLv2来源。
默认sweetpanellifecycle=0；1仍原温态28/29。新显式2要求live记录/pins2和原touch2/own2/power1/
IRQ2/dsi1/managed1及resource/clock/NoC依赖，未知则拒绝，无静默PV或mode1回退。
实际OFF：touchfence→显示/IRQ/DMA退出→完整OFF+延时，sleeping=true；触摸停驻，全部供电票保留。
实际ON：同代fence/保票/静止→首轮typed activate→typed GPIO6 reset(10/2/10+LG5ms)→完整ON/
长包prepared DMA→DCS51恢复typed目标→原TE/IRQ/fresh像素完成→原TCM实际识别/格式验证恢复。
温态takeover保持0194，不自动reset/发vendor表。Standalone Prepare/Reset/Unprepare仍Unsupported。
SweetMDSS typed setBrightness override同gate保存已知最新目标；未知亮度时完整OFFON副作用前NotReady，
Busy目标继续保留，晚于restore发出的请求沿背光interest重试。FB mode2查额外capability及profile/
sleeping/resetComplete/brightnessRestored/pixel/touch证据，不拿温态Complete冒充完整恢复。
新增PinActivate/PinReset/Brightness，busy改显式阶段判断；pins100ms/完整表5s/亮度500ms/总16s。
pins新增cancelRequest(id,token)，同步gate停timer/poison保留部分状态，不park/reset做cleanup。
故障/撤回/超时/stop保票/DMA pin，关闭本代；取消不等于恢复/硬件已静止；pin-active不能假detach。
无新线程/全局实例桥/consumer MMIO/空闲poll；USB/按键/充电/mux/触摸IRQ策略未改。
18套通过：原16+实际parser/UEFIadapter与完整profile联测，17实际源码ASan/UBSan+PythonADT无最终报告。
覆盖四版各3轮sleep/reset/ON、独立完整wire包/延时/输入与像素/实际C触摸worker、每pinphase取消、
provider loss/stop、input/readback/期限/DMA pin/亮度/触摸失败、身份/配置拒绝、FB缺证据、
HOB/map/分配故障、FDT/ADT每截断/bit mutation。LSan ptrace限制关闭，另断言资源和重启pin归零。
真实当前out-bs/adt-16k-3g.adt和组合空identity slot解析通过（未复制设备/Apple产物进库）。
真实XNU/KDK arm64 MDSS/FB/Pins base+final对象成功/关键符号核实；非整核链接。
MDSS warnings1481→1503，22个固定ABI/表索引边界提示已审；发现的format类型告警已修，没有抑制。
FB1397→1397、Pins1171→1171。Mu mh2实际消费DarwinBoot模块-Werror编译链接成功，模块
41472 bytes/SHA256=56083c42fc786c4bcdf7cea6aa1feaee4ed2e6d46fdba64dfbe04bd4ec930487；1个既有平台HwResetSystemLib配置warning。
**没有重建完整UEFI FD，没有替换out-bs/kernel-32m.blob，没有生成新boot、push/CI/设备操作或硬件结果。**
**NEXT：继续PD3共享供电/反向释放设计及PD4冷NoC/MDP/独立重建；后续合并镜像要同时纳入新DarwinBoot。**
同核先mode1取source/profile确认，再mode2验证保票的sleep/reset；无需为单个只读接口新开云编译。
E7实际reset后触摸、E8共享电/偏压未验收，不能盲撤ABL或其它consumer票；rail release不等于物理断电。
ABL初始buffer/console/panic、冷MDP、完整IOPM/正常关电及失败资源释放仍缺，不宣称PD3/完整驱动验收。
A单缓冲/B未提交/30Hz复制不变，旧滑动退化/BUG003未宣称修复；不重测旧USB/BUG002/F3/半小时。
CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df，暂不要求push/刷机。
底稿scratch/framebuffer-0195/{base,edited,verify,prepare-patch.py,compile.py,run-tests.py,verification.json,
base-continuity.json,tests.json,native-results.json,warning-counts.json,warning-review.json,*compile.txt,
*-symbols.txt,real-adt-validation.json,uefi-base,uefi-edited,uefi-verify,prepare-uefi.py,compile-uefi.py,
uefi-verification.json,uefi-native-result.json,uefi-native-build.txt,DarwinBoot.efi,DarwinBoot-headers.txt}。
详见docs/PANEL-PROFILE-D1.md、boot-support/README.md；计划/架构/PD0/协调器/pins/touch文档同步。

**以下0194按当时范围保留，当前以0195为准。**

## Agent handoff / active work (2026-11-11, latest)

**CURRENT：0194统一温态生命周期协调器已接实际调用并本地通过，未实机；完整面板驱动仍未完成。**
父提交c3b530a；0194-mh2lm-panel-lifecycle-coordinator.patch SHA256=
327e2ca4cf4582e52030337463c5f699c007dc5e90bae8c866c390d740627af7，12份源码变化。
SweetMDSSLifecycle.inc是原SweetMDSS内部模块，同一gate/唯一MMIO owner、独立推进timer；
无新线程/全局实例桥/映射。默认sweetpanellifecycle=0；1要求own2/power1/IRQ2/dsi1/
touchpanel2/managed1及原resource/clock/NoC依赖。start/match不自动发life请求。
实际beginScanout：原只读准入→touch QUIESCED→原TE/auto停流→GIC115 RWP/enable/active及
peripheral禁用/DSI DMAidle确认→原LDO/ARC/core/NoC/clock/可选PHY-host重建→自有A提交→
freshPPDONE+flush0+RD_PTR→原TCM应用/格式验证恢复→对外Active；不复制另一套重建算法。
DisplayOff/On使用原异步DSI 28/29，保票；Off继续parked触摸/停流，On须新像素完成及touchReady。
仍是温态DisplayOff，不是SleepIn/断电/reset；prepare/reset/unprepare在副作用前Unsupported。
没有调用pins activate/reset/park或发送候选vendor表。status显式token/phase/result/证据账本，
原setPanelPower在默认档不变，协调档必须新typed异步接口；FB在pending冻结呈现、保最新swap，
快速相反请求合并、核对token/op及完成标记，consumer17s watchdog，无F2/ABL静默回退。
每段touch6.5s、display/IRQdrain50ms、takeover5s、DSI500ms、freshpixel100ms，总16s；
原子状态/计时仅协调期间工作，无新增空闲poll。fReady等worker状态仍沿0193原子mailbox。
每个生命周期/scanout/DSI/clock/NoC推进检查冻结provider身份、inactive及已取得touchfence；
withdrawal即使changed事件未运行，也不能继续下一写入，旧token不能接续新provider。
consumer cancel新增SweetTouchPanel::cancelRequest(id,token)，关独占C session/poison而非stop服务；
worker异步收尾/退休至reboot，取消不冒充hardware fence。协调器failure/stop终止子timer，
DSI显式noRestore、scanout不恢复继承TE，保票/部分状态/DMA pin，无自动reset/resume/撤电。
背光Busy保最新目标；SweetDisplayClient真实registerInterest/messageClients，仅回调排队IES，
完成后背光自己的loop重试，不反向同步进gate/不idlepoll；匹配/interest/保留引用stop同步清理。
16套通过：原14+真实生命周期与FB消费者；15套实际源码ASan/UBSan+PythonADT，无最终报告。
新增覆盖TCM实际Cworker/协议、GICactive/RWP、真实资源/PHY/host/DSI/像素完成、假DONE/缺frame、
touch格式/availability失效、pins/其他provider撤回早于changed、取消/期限/stop/晚timer、
保票/pin、token/ID错误/耗尽、快速OFFON、错误完成/consumerwatchdog、实际背光interest重试。
LSan环境ptrace限制关闭，另断言对象/映射/通知/分配及模拟重启后的pin归零。
真实XNU/KDK arm64 MDSS/FB基线最终与TouchPanel对象成功，核实实际生命周期/取消/consumer符号。
MDSS warnings1475→1481：6个固定fMaps[9/10/11]/clock mask数组访问已审；FB1397→1397；
TouchPanel1151为原头/宏诊断，无warning抑制。不是整核链接或硬件结果。
0194base与0193verify字节连续，严格应用无offset/fuzz、12源码一致、114原CI markers保留。
**NEXT：继续完整驱动，确认实际panel profile及共享供电证据，接pins/reset/完整vendor ONOFF与反向释放；**
之后冷NoC/MDP、独立重建与ABL退出，再零拷贝。E1/E7/E8未关闭，不猜厂商表、不启动Android。
A单缓冲/B未提交/30Hz复制不变，不宣称旧卡顿或BUG003修复；协调档仅在自有接管后受理OFFON。
本轮无push/CI/设备操作/新boot/实机结果，暂不要求push或新刷机；CircleCI blob仍
 da0b6c0947a66f03491b3a9023590b49665292df。USB/按键/充电/mux和触摸硬件策略未改。
底稿scratch/framebuffer-0194/{base,edited,verify,prepare-patch.py,compile.py,run-tests.py,
verification.json,base-continuity.json,native-results.json,warning-counts.json,warning-review.json,
tests.json,test-*.txt,*compile.txt,*symbols.txt}；docs/PANEL-LIFECYCLE-D1.md及计划/架构/PD0已同步。

**以下0193按当时范围保留，当前以0194为准。**

**CURRENT：0193触摸quiesce/resume执行层与MDSS匹配绑定本地通过，未实机；PD3完整协调器未接。**
父提交354cc16；0193-mh2lm-panel-touch-coordination.patch SHA256=
cce91ee520c9e868ad0082af0315aa0a9c5a836d6e3c0c161a053e4777fc2693，7份源码变化。
新增SweetTouchPanel IOService/client及无MMIO/IRQ的ADT nub，typed consumer请求，MDSS真实registry
通知仅retain/release+排队，自己的workloop attach。服务内部用受限C mailbox接既有唯一TCM worker，
不是整个touch IOService化；C只存原子session/packed token+state/cancel，无provider实例或回调。
原GPIO/I2C/GIC/HID所有权仍在worker；无新线程/映射，不改原IRQ-only策略/触摸供电初始化。
sweettouchpanel默认0；1metadata/2允许显式请求；--panel-touch隐含resources可组合pins/NoC。
start/matched/attach无硬件I/O/自动请求，worker未就绪不发token。MDSS本批仍只绑定，实际boot
不quiesce/reset/ONOFF。暂停在worker事务边界maskGPIO122/GIC240、readback/RWP/active确认、
HID all-up后才发QUIESCED，最长20次1ms睡眠；不是deep sleep/rail-off证明。
parked worker/晚IRQ无MMIO/I2C/HID。resume要求caller已恢复panel/rails，复用实际tcm_start
identify/app/config读回/原geometry匹配，再清stale pending和arm，无touch reset/rail/firmware写入。
worker执行4s预算/transport字节与命令等待处取消；provider pending才1ms查mailbox，6s watchdog。
无provider gate内等待/I2C、无空闲轮询；失败终止上报不自动重跑resume/退轮询（原协议内有界重传保留）。
stop同步自己的timer后取消session，worker异步完成bus STOP/mask并退出；不阻塞join，不宣称
取消=硬件静止。arm中取消可能已有部分enable写，复查后remask，不交付Ready。曾发请求session
close退休至reboot；清ready/quiesced，服务free后worker无对象指针可解引用。
未来协调器provider loss/fault必须停后续reset/撤电，保票，不拿旧fence继续；当前仍仅绑定/解绑。
旧live-e-0114a15-irq-only-debug1.txt:1724已有TCM v1 part s3618-14.0.1证据（非panelrevision）；
LG s3618 NOTIFY_TOUCH_RESET为空，LCD_MODE另有suspend/resume，不套S3706 reset，不推断无共享。
14套通过：新增实际C worker/IRQ/TCMv1协议+C++provider/MDSS联测，及原13套；13实际源码
ASan/UBSan+PythonADT，无报告。LSan ptrace限制关闭，另断言对象/映射/通知/分配归零。
覆盖真实active drain/独立协议回复/HID release/无I/O parked/状态和总期限失败/等待竞态/并发/
排队到arm中取消/服务free后收尾/退休/ID-token耗尽/consumer旧状态隔离/真实registry与ADT CLI。
真实XNU/KDK arm64触摸C/MDSS基线最终与新provider对象成功；C warning1->1、MDSS1475->1475
无新增签名，新provider1151为既有头/宏诊断，无压制。核实C对象有mailbox/IRQ/init实际符号。
0193 base等0192verify，严格应用无offset/fuzz、7源码字节一致、114原CI marker保留。
**NEXT：统一PD3生命周期事务，接touch fence/DMA/IRQ/资源/pins/已确认profile，完整ONOFF与反向释放。**
E1身份未关闭，E7软件接口有而实机共享供电/reset恢复仍缺；不猜vendor表，不启动Android。
NoC/MDP冷初始化、独立重建、ABL退出/零拷贝仍后续；A单缓冲/B未提交/30Hz复制不变。
未push/触发CI/操作设备，无新boot/云产物/实机结果，暂不要求push/新刷机；CircleCI blob仍
 da0b6c0947a66f03491b3a9023590b49665292df。USB/按键/电池/mux未改，不重测旧bug/半小时。
底稿scratch/framebuffer-0193/{base,edited,verify,prepare-patch.py,compile.py,verification.json,
base-continuity.json,native-results.json,native-worker-symbols.txt,warning-counts.json,warning-review.json,
reference-audit.json,run-tests.py,tests.json,test-*.txt,*compile.txt}；详见docs/PANEL-TOUCH-D1.md，计划/架构/PD0已同步。

**以下0192按当时阶段保留，当前以0193为准。**

**CURRENT：0192 reset/TE引脚服务与MDSS匹配绑定已本地通过，尚未实机；PD3完整生命周期未接。**
父提交52e4c8d；0192-mh2lm-panel-pin-provider.patch SHA256=
ead1f8ef6cf6d19e409f847895bf2e8f4395eff06f78fde73706b1ee0f277a57，5份源码变化。
新增SweetPanelPins IOService与client、独立ADT nub/编译入口；GPIO6 SOUTH0x03d06000/4K，
GPIO8 NORTH0x03908000/4K。不写GPIO7/IRQ/触摸pin，旧触摸整tile映射与两页有包含关系。
sweetpanelpins默认0，1 metadata/attach/query，2允许显式typed请求；start/matched/attach/query
无引脚MMIO。MDSS要求resource mode并真实匹配/自己的loop attach，通知仅retain/release+排队。
本批MDSS不调用activate/reset/park，实际boot不reset，不发送vendor表。
provider独占consumer/单调token，用原生loop/gate/timer；warm高电平准入、CTL高位/完整IRQ快照，
reset latch和实际input分别确认，每edge最多1ms，hold>=10/2/10ms+settle5ms，整体100ms。
park确认低>=5ms再弱drive/pull/OE保留；重activate仍低。漂移/丢写/电平不跟随poison，保留部分
效果，不回滚/重试/自动park，active/uncertain不可detach。stop同步timer/无pin变化，晚timer无MMIO。
写过后owner retired latch拒绝同boot无账本重建，无全局实例桥。INFO停止/ERROR失败/WARN残consumer。
13套通过：pins新增+原12；12实际源码ASan/UBSan+PythonADT，无报告。LSan ptrace限制关闭，
另断言对象/映射/通知/分配归零；覆盖独立input、最短hold、park/reactivate、owner/IRQ/eGPIO漂移、
读回/输入超时/总期限、每phase stop/晚timer、并发gate stop、ID/token耗尽/退休、真实MDSS绑定。
真实XNU/KDK arm64 MDSS基线/最终及pins对象通过；MDSS warning1475->1475，无新增签名，
pins1171含原XNU/KDK宏/头warning；数组/页内MMIO边界已审，无抑制。不是完整链接或硬件结果。
0192 base等0191verify，严格应用无offset/fuzz、5源码字节一致、114原CI marker保留。
**NEXT：继续PD3触摸quiesce/resume与生命周期协调；实际panel/revision证据未有，不发候选vendor表。**
E1/E7仍开；reset/完整ONOFF/资源反向释放/NoC与MDP冷初始化/独立重建/ABL退出仍缺。
caller必须自己证明identity/rails/link/DMA/IRQ/touch依赖，pins provider不伪造这些证明。
provider丢失当前只解绑（尚无实际pin使用）；接lifecycle时必须加fault/cancel。
A单缓冲/B未提交/30Hz复制不变，不宣称完整驱动或旧卡顿修好；暂不要求push/新刷机。
未push/触发CI/操作设备，CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
USB/按键/触摸/电池/mux未改，不重测旧BUG-002/USB充电/F3/半小时，无新boot/云产物/实机结果。
底稿scratch/framebuffer-0192/{base,edited,verify,prepare-patch.py,compile.py,verification.json,
base-continuity.json,native-results.json,warning-counts.json,array-bound-review.json,reference-audit.json,
run-tests.py,tests.json,test-*.txt,*compile.txt}；详见docs/PANEL-PINS-D1.md，计划/架构/PD0已同步。

**以下0191按当时阶段保留，当前以0192为准。**

**CURRENT：0191 PHY v4.0/PLL7nm-v2/输出时钟重建已接实际managed扫描，本地通过；未实机。**
父提交90c51d6；0191-mh2lm-dsi-link-rebuild.patch SHA256=
ba6ea09922027fc2058421b3c9f25c7d13263ac266b9f4cbc6a815de9bfaa3d6，10份变化源码。
新增SweetDSILink.h纯计算、SweetMDSSLink.inc与数值表；沿原MDSS owner/gate/DSI timer和
clock writer退休账本，无新provider/映射/线程/C实例桥。显式sweetdsilink=2要求host1/core2/
managed1及原依赖，默认0保留0190行为；startup/matched不reset。
实际NoC/DSI0 Held后：停止host/IRQ/TE-auto->lane idle->分支CLK_OFF与ROOT_OFF/无UPDATE各自
确认->PLL stop等旧lock清->bias/PHY关->PHY reset>=1ms/退出100us->REFGEN->明确PHY/timing/
v2 PLL表/DEC-FRAC/DOWN-SSC/分频->新lock->两RCG各自UPDATE完成->分支与root运行->resync->
仅更新受控父源快照->host重建->原A pin/提交/freshPPDONE+flush0+RD_PTR。锁定不等于像素完成。
目标bit1056619200/VCO2113238400，DEC37/FRAC2106，实际VCO2113238378（计算非测量）；
byte132077398/intf66038699/pixel176103198，SSCperiod302/step714。LG四R66456源表rate/timing一致。
profile为LG板级选择，不臆造revision编码；记录原始四byte，拒绝未知坏状态/alias/ULPS/CPHY/
PNswap。没有新硬件identity证据，不写PHY1、panel GPIO或vendor表。
过渡期间clock lease保留但readiness关闭，亮度/DCS/power不准入，clock/NoC修改Busy，token
查询可用；每步核对typed资源/非受控clock/alias/idle，250ms整链及分阶段期限，timer无长忙等。
失败保票/部分状态并poison，不回放/重试/恢复TE/盲撤电；stop/晚timer无MMIO或偷偷解除reset。
0191基线等0190验证树；严格应用无offset/fuzz，10源码字节一致，114原CI marker保留。
真实XNU/KDK arm64 MDSS基线/最终对象通过，warning1417->1475（58有界数组提示逐项审，无抑制）。
12套通过：link新增+原11套；11实际源码ASan/UBSan+PythonADT，UBSanhalt_on_error无报告。
LSan因ptrace限制关，另断言对象/映射/通知/分配归零。专项涵盖真实资源/host/扫描链、reset
销毁配置、独立branch/root停稳及运行、旧lock/新lock/REFGEN/UPDATE超时、写丢/漂移/alias/
provider/stop/晚timer、host时PLL丢锁、未ready不提交及真实fresh帧。
**NEXT：确认panel身份，接reset/pinctrl/完整ONOFF与资源反向释放，再做独立重建/ABL退出。**
NoC冷solver/QoS/端口/sleepwake和MDP冷初始化也仍缺。真实start仍温态，DCS OFF不撤票，
不宣称整屏冷启动/完整驱动完成。A单缓冲/B未提交/30Hz复制不变，不声称旧卡顿已修好。
继续合并可运行完整驱动批次，暂不要求push/新刷机轮次；未push/触发CI/操作设备。
CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df；USB/按键/触摸/电池/mux未改，
不重测旧BUG-002/USB充电/F3/半小时。无新boot/云产物/硬件结果。
底稿scratch/framebuffer-0191/{base,edited,verify,prepare-patch.py,compile.py,verification.json,
native-results.json,warning-counts.json,array-bound-review.json,reference-audit.json,pll-table-audit.json,
tests.json,test-*.txt,*compile.txt}；详见docs/PANEL-LINK-D1.md，主计划/架构/PD0/依赖文档已同步。

**以下0190按当时阶段保留，当前以0191为准。**

**CURRENT：0190 DSI host v2.3重建已接实际managed扫描，本地通过，未实机；完整面板驱动仍未完成。**
父提交fbabaee；0190-mh2lm-dsi-host-rebuild.patch SHA256=
48aac4737c1e755d507996efc3fc1b4bdc240e10c040f9dc2c8f530aaf547f47，7份变化源码。新增SweetMDSSHost.inc，沿原MDSS唯一owner/gate、DSI timer与clock writer退休账本。
显式sweetdsihost=1要求managed1/core2及原依赖，默认0保留0189行为；不增加空壳provider/映射/线程。
setup/matched不reset。真正beginScanout暂停TE/auto/IRQ后LDO/ARC/core/NoC/独立DSI0 lease
完成，再ManagedHost：核实资源/版本/idle->关IRQ enables/CTRL低3位->force clocks->host reset
>=1us timer->解除reset->明确公共command配置->controller/cmd enable->独立读回/idle->
managed复核->原IRQ重开/A pin+源提交/freshPPDONE+flush0+RD_PTR。hostReady不代替像素完成。
LG四R66456版本公共RGB8881080x2340/VC0/4lane/无DSC；one-hot lane map=8421，不能用3210。
TRIG=80000004按LG外部TE，比ABL4多bit31；MDP_CTRL=8按LGinterleave0，ABL100008为1，
这些差异尚未实测。已有0121a12/a13 HW_VERSION20030000，写前再核对；没有新增身份读数。
INT_CTRL混合W1C/enable，只写0关使能，不整字等值读回/随意ACK ERROR。host配置表非ABL快照。
只写host+118 reset，不写+12c/PHYPLL/resetGPIO，不发vendor；PHY/PLL/面板状态仍继承。
DCS/亮度/面板电源、clock/NoC修改在过渡中关闭/Busy，查询token可用；快照/probe避开host过渡。
每步核对ARC/NoC/DSI0 Held/core/clock/IRQidle/TEauto，20ms预算；失败保票/部分硬件状态并
poison DSI，无恢复旧配置/TE、自动重试/reset/撤电。stop/晚timer不MMIO，不偷偷释放reset。
严格应用0189树无offset/fuzz，7源码字节一致，114原CI marker保留。真实XNU/KDK arm64
MDSS基线/最终对象通过，warning1413->1417（4处固定有界数组提示已审，无抑制）。
11套通过：host新增+原10套，10实际源码ASan/UBSan+PythonADT；UBSanhalt_on_error无报告。
LSan因ptrace限制关，另断言对象/映射/通知/分配归零。host专项含reset销毁配置、>=1us、
W1C/enable、逐项丢写/版本错误/忙准入、真实fresh扫描完成、资源/error/timeout/provider/
stop晚timer、无PHY/vendor写入。没有新boot/云产物/硬件结果。
**NEXT：PHY v4.0/7nm PLL版本准入及真实配置、DSI输出时钟，再接已确认panel身份/reset/**
**完整ONOFF、反向释放、独立重建/ABL退出；NoC冷solver/QoS/sleepwake/端口也仍缺。**
实际start仍需温态splash，DCS OFF仍不撤票；不宣称冷启动/完整驱动/旧卡顿修好。
A单缓冲/B未提交/30Hz复制未改。继续合并可运行完整驱动批次，暂不要求push或新刷机轮次。
未push/触发CI/操作设备；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df；
USB/按键/触摸/电池/mux未改，不重测旧BUG-002/USB充电/F3/半小时。
底稿scratch/framebuffer-0190/{base,edited,verify,prepare-patch.py,compile.py,verification.json,
native-results.json,warning-counts.json,array-bound-review.json,reference-audit.json,tests.json,
test-*.txt,*compile.txt}。详见docs/PANEL-HOST-D1.md；主计划/架构/PD0/managed/core/总路线已同步。

**以下0189按当时阶段保留，当前以0190为准。**

**CURRENT：0189 GDSC/核心PLL/AHB/MDP控制已接实际managed扫描链，本地通过；未实机，D1整体未完成。**
父提交9b71a3b；0189-mh2lm-display-core-control.patch SHA256=
ae2980e54f3bc97fb6421c8eda1e151c4348eff4b4c4de310fe2df6a3bd27043，7份变化源码。
新增SweetMDSSCore.inc，仍为MDSS唯一DISPCC/GDSC owner，复用gate/clock timer/writer与retired账本。
显式sweetdisplaycore=2选择SM8150-v2，要求clock1/managed1及原依赖，默认0保留0188行为；
SelectedProfile不是实机版本检测。原解码DT称v2，但本轮无新硬件identity证据；不写v1 TEST字段。
真实beginScanout暂停/idle/IRQ后LDO->ARC Turbo->CN0 300MB/s实际完成->GDSC软件票->
两核心TRION PLL->AHB75/MDP460 RCG->核心分支实际运行->原NoC/DSI0 lease->自有A/fresh完成。
GDSC只清SW_COLLAPSE、拒绝HW_CONTROL/OVERRIDE；>=1us后PWR_ON，成功再稳定2us。
PLL0=1380MHz(L47/alphae000)，PLL1=600MHz(L1f/alpha4000)；工作PLL校验后保留/标继承，
不重配live/FSM/更新中/错误频率PLL。关闭PLL逐写v2 CONFIG/CAL/USER与STANDBY/RESET/RUN，
等独立LOCK再开输出；检查四可选root无孤立使用者。AHB75=PLL1/8，MDP460=PLL0/3+HWCTRL；
只更新这两受控root的lease CFG/source，其他父源漂移仍拒绝。CFG写后独立UPDATE/dirty清，
冷分支另等真实CLK_OFF清及root运行；Ready整体复核。仍持Turbo，未做DVFS/低功耗策略。
内部typed Warm/Cold入口；实际start仍需温态splash，仅调用Warm。Cold组件已执行真实核心
代码的域/PLL/分支开启，要求从未有IRQ/clock/DMA账本，ARC和CN0 ACK前不读GDSC/PLL，
过程中不读MDSS/DSI/PHY/display RSC；**这不是现有start支持整屏冷启动的证据。**
预算：整链500ms、GDSC100us、PLL锁1ms、RCG500us、分支10ms，timer推进无忙等。
过渡期协调器/诊断/扫描快照不读临时pipeline/旧父源；暂停IRQ后晚watchdog在Idle不误报。
域丢失时pauseIRQ避免MDSS访问，GIC照常安全mask；失败显式保票/部分效果、不reset/重试/
盲撤/恢复旧TE。stop不撤电、晚timer无MMIO。core期间CN0 token完整校验；Ready后原NoC
以同consumer/同300MB/s票接新token，无第二apps writer。ARC token保留并逐步校验。
严格0188树重放无offset/fuzz，7源码字节一致，114原CI marker保留。真实XNU/KDK arm64
MDSS基线/最终对象通过，warning1398->1413（新增15固定有界数组提示已审，无抑制）。
10套最终检查通过：9实际源码ASan/UBSan（core/managed/NoC/clock/display/FB/DSI/RPMh/perf）
+Python ADT；UBSan halt_on_error，无sanitizer报告。LSan ptrace限制仍关，另assert对象/
映射/通知/分配归零。测试覆盖真实扫描链、冷核心独立完成/无冷域读取、保留live PLL、单/
双PLL初始化、参数/频率/FSM/更新/孤立root拒绝、PWR/LOCK/UPDATE/分支故障、stop/provider
丢失、controlled快照/fresh帧、DCS保票/晚watchdog及零对象。无新boot/云编译/硬件结果。
**仍缺NoC冷solver/sleepwake/QoS/SBM/端口、DSI host/PHY/PLL、panel身份/reset/完整ONOFF、**
**完整unprepare与GDSC/PLL关闭/资源反向释放、独立重建/ABL退出。DCS OFF不撤资源。**
NEXT继续DSI host/PHY/PLL真实配置，再接panel完整生命周期/反向释放/独立重建，之后零拷贝。
A单缓冲/B未提交/30Hz复制未改，不声称旧卡顿已解决；不回CPU双缓冲/孤立只读云轮次/旧bug专项。
暂不需要push，继续合并可运行完整驱动批次。未push/触发CI/操作设备；CircleCI blob仍
 da0b6c0947a66f03491b3a9023590b49665292df；USB/按键/触摸/电池/mux未修改。
底稿scratch/framebuffer-0189/{base,edited,verify,prepare-patch.py,compile.py,verification.json,
native-results.json,warning-counts.json,array-bound-review.json,reference-audit.json,tests.json,
test-*.txt,*compile.txt}；详见docs/PANEL-CORE-D1.md，主计划/架构/PD0/clock/managed已同步。

**以下0188按当时阶段保留，当前以0189为准。**

**CURRENT：0188实际扫描接管已接LDO/ARC/NoC/local clock资源获取，本地通过；未实机，D1整体未完成。**
父提交e912bd6。0188-mh2lm-managed-display-resources.patch SHA256=
9c8552e22ed68a0a6944fee7c6d7ea5a02f219c96587b195d6407df3800b7ea8，7份变化源码。
新增SweetMDSSResources.inc，沿原MDSS gate/scanout timer，无新映射/writer/全局桥/直写供电。
显式sweetdisplaymanaged=1要求own2/power1/IRQ2/resource2/clock1/NoC ABI；默认0不改变旧行为，
startup/matched仍不投票。实际beginScanout先完整metadata/约束/可表示性preflight，暂停TE/auto，
两次非有效扫描行后pauseIRQ，新增SweetScanoutResources状态，通过原typed status等待。
实际VDDI->VPNL->Host->PHY逐路ACK，VDDI后20ms、VPNL后5ms timer等待；然后ARC Turbo
整链完成->NoC lease->独立DSI0 clock lease。全确认、复核pipeline/idle/clock，rearm IRQ，
再走原A pin/源提交/恢复TE-auto/freshPPDONE+flush0+RD_PTR新帧；资源ACK不替代新传输完成。
LG sm8150-sde enabled RSC usecase：每MNOC端口/LLCC/EBI IB6.4GB/s、AB0，CN0最高300MB/s；
Turbo是温态不改频下的typed保守bootstrap，不是电压猜测或正常最低功耗策略。真DB若14bit
不能表示则首票前拒绝并打印具体preflight ERROR，不clamp/减票冒充满足。各阶段Busy显式失败。
获取期间亮度/DCS/DSI序列/电源入口NotReady；Idle未开始仍保留旧splash DCS，Ready后恢复。
资源链4s预算，原quiesce/新传输50/100ms独立。rails位图/各token/状态账本；Fault保留已得/
在途票，停止本MDSS NoC/clock推进，不恢复旧TE-auto、不提交新源、不盲撤父；已送RPMh事务
可能继续完成，provider自身管理，保留其票。失去provider也故障并关扫描入口；无静默回退。
正常stop为ManagedStopped/Aborted，不当本协调器硬件ERROR；晚timer无MMIO，DMA pin规则未变。
**普通DCS OFF不撤电/带宽。完整unprepare/反向释放仍缺，GDSC/非XO父源仍继承。**
补clock日志/ownership属性，避免将已获得LDO/ARC/NoC一概标继承；lease inheritedUpstream仍真，
表示剩余domain/parents没有完整接管。仍无冷GDSC/NoC QoS/solver/sleepwake/PHY/PLL/host控制，
无reset/vendor完整初始化或ABL完全退出，不能验收完整D1/面板驱动。未新开boot矩阵。
严格0187树应用无offset/fuzz，7源码字节一致，114原CI marker保留。真实XNU/KDK arm64
MDSS/FB基线和最终对象通过，MDSS1391->1398告警（7有界数组提示，已审rail0..3/BCM0..5），
FB1397不增，不声称零warning。9套最终检查全通过：8实际源码ASan/UBSan（managed/NoC/
clock/display/FB/DSI/RPMh/perf）+Python ADT；UBSan halt_on_error，无sanitizer报告。
managed从真实beginScanout进入，含真实IRQ暂停/恢复及provider引擎，测未ACK不提交/20ms/
顺序/实际A pin-fresh完成/DCS保票/5坏startup/metadata/8阶段故障期限stop/运行后provider丢失；
默认旧回归保留。LSan ptrace限制关，另断言对象/映射/分配/通知归零。没有实机投票证据。
底稿scratch/framebuffer-0188/{base,edited,verify,prepare-patch.py,compile.py,verification.json,
native-results.json,warning-counts.json,array-bound-review.json,reference-audit.json,tests.json,
test-*.txt,*compile.txt}；docs/PANEL-MANAGED-D1.md及架构/ARC/clock/NoC/PD0/主计划/总路线已同步。
**NEXT补冷GDSC及clock父源/频率，再接PHY/PLL/host、确认profile/reset/完整ONOFF与资源撤回，**
再验证独立重建/ABL退出，之后零拷贝。managed1是实际获取首段，不要重回纯metadata接口迭代。
完整驱动优先；A单缓冲B未提交/30Hz复制未改，不重测旧bug/F3矩阵/半小时，不开孤立只读云轮次。
暂不需要push，继续合并可运行驱动批次。未push/触发CI/操作设备，无新云产物/boot/run ID。
CircleCI blob保持da0b6c0947a66f03491b3a9023590b49665292df；USB/按键/触摸/电池/mux未修改。

**以下0187按当时阶段保留，当前以0188为准。**

**CURRENT：0187 CN0/display NoC温态ACTIVE事务已实现并本地验证；未接入面板启停，D1整体未完成。**
父提交1a0ac96。0187-mh2lm-display-noc-votes.patch SHA256=
14d2845e6bf429833888de20d3bd99dd14f5b3c4d3eb30485c836c4f3b827937，6份变化源码。
所有权：SweetRPMh保留唯一apps-DRV/gate/timer，新增CN0配置带宽；SweetMDSS在原gate/clock
所有权内拥有display RSC/TCS/wrapper及数据BCM，不引入RPMh->MDSS反向同步调用或全局实例桥。
新增typed BCM metadata（地址仅friend硬件owner可见），CN0多consumer max票，沿原VRM/ARC引擎。
MDSS retainBandwidth/releaseBandwidth/bandwidthStatus：4lease、AB求和/IB max、完整MNOC两端口/
LLCC/EBI路径，按DB unit/width/VCD有界上取整14bit；LG ACV_DISPLAY ID27006走普通XY，
只有apps ACV ID7038特殊bitmask8，不把后者搬到display。资源/名字/地址/aux唯一有界校验。
display DRV0按LG DTS顺序SLEEP0/WAKE1/ACTIVE2,3，按VCD整组提交、末COMMIT/响应/Wait，
非末issued、末issued+complete，另需freshIRQ或执行变化+idle。仅本槽清理，保留无关IRQ位。
CN0->真实RSC clock lease->数据；撤票数据->clock OFF完成->CN0。新增NON_GDSC_AHB4004/
RSCC_AHB400c/RSCC_VSYNC4008，RSC clock mask381；数据清完前不允许撤其token。
实际温态GDSC/clock条件与真实lease确认后才首次读RSC；拒绝solver启用/seqbusy/errorIRQ/
collapse/未知版本。首次/全部Off后再取/任意撤票核实硬件idle，不允许历史owned位绕过。
写回1ms、每VCD100ms、整链1.5s，timer推进；故障保留确认票和时钟、显式fault，无回滚/
reset/静默回退；stop晚事件不MMIO，有写free后本boot writer retired。
ADT --display-noc隐含resources，在MDSS追加13..15：af20000/1c00、af21c00/3000、af30000/3fd4，
无重叠，require resource2/clock1；坏ABI类型/长度/值及缺映射启动显式失败。default/IRQ/v1字节不变。
**无新boot档/自动请求；LDO/ARC/NoC尚未接入实际面板启停；本地clock上游仍标inherited。**
不是冷GDSC/父源、solver/sleepwake/QoS/SBM/端口、PHY/PLL/host或完整面板驱动的验收。
严格应用0186树，无offset/fuzz，6源码字节一致，114原CI marker保留。真实XNU/KDK arm64
RPMh/MDSS基线/最终对象均通过；warning1298->1321/1324->1391，新增23/67仅有界数组提示，
已审6BCM/4lease/5数据route/24操作槽/16map/10clock及DB范围，不声称零warning。
8套最终检查全通过：7实际源码ASan/UBSan（NoC/clock/display/FB/DSI/RPMh/perf）+Python ADT，
UBSan halt_on_error，无sanitizer报告；LSan ptrace限制关，另断言对象/映射/分配/通知归零。
新测试含真实匹配、首次不读RSC、编码/VCD/fresh完成、共享/撤回/Off重取idle、继承新冲突、
独立clock ONOFF+CN0保留、坏元数据/溢出/NCPT不足/准入拒绝/超时/清理/stop与ABI启动清理。
旧clock回归原7次反扫上限改SweetClockCount，新10分支后仍验证同一独立物理OFF条件。
底稿scratch/framebuffer-0187/{base,edited,verify,prepare-patch.py,compile.py,verification.json,
native-results.json,warning-counts.json,array-bound-review.json,reference-audit.json,tests.json,
test-*.txt,*compile.txt}；详见docs/PANEL-NOC-D1.md，架构/ARC/clock/PD0/主计划/总路线已同步。
**NEXT把已有LDO/ARC/NoC/local clock接入实际MDSS资源生命周期，再补冷GDSC/父源/PHY/PLL/host，**
再接确认profile/reset/完整ONOFF及独立重建，不止于metadata API。不回CPU双缓冲优先/单项只读
云轮次/旧bug专项；A单缓冲B未提交、30Hz复制未变。暂不需要push，合并可运行驱动批次再通知。
未push/触发CI/操作设备，无新云产物/boot/run ID。CircleCI blob保持
 da0b6c0947a66f03491b3a9023590b49665292df；USB/按键/触摸/电池/mux未修改。

**以下0186按当时阶段保留，当前以0187为准。**

**CURRENT：0186 typed MMCX/MX/CX供电链已实现并本地验证；未实机，D1整体未完成。**
父提交35f61f1。0186-mh2lm-display-arc-votes.patch SHA256=
41ced0dbec1edc12065c801804906d97b17f715b0e33bf956af26929ad1bb104，仅改SweetRPMh.h/.cpp。
原IOService/apps-DRV唯一writer/gate/timer内新增displayPowerInfo/requestDisplayPower/
copyDisplayPowerStatus；8consumer各有corner票，聚合最高请求。typed Off/LowSVS/SVS/Nominal/
Turbo，不接受raw地址/mV/selector；ARC资源是cx.lvl/mx.lvl/mmcx.lvl，与四个面板LDO分开。
按LG regulator/core实际图推导：MMCX ceil所需rawlevel，MX ceil(max(16,selectedMMCX-1))，
CX display-supply floor48；MX无dropout不向CX传播等值level。选中值是DB raw semantic，
硬件命令发selector。ARC aux LEu16/<=16项，Off0/严格上升/尾零/类型/地址唯一/范围保护；
可选entry缺失不影响原LDO绑定，显式ARC请求拒绝，DB整体几何损坏仍拒绝startup。
**首次接管保护：未知parent先CX/MX各自DB最高selector，MMCX目标ACK后再MX/CX降到需求。**
不能在未知固件child高档时先降parent。最多五个stage，各自单命令独立fresh完成及清理；
已拥有正常升档CX->MX->MMCX，降档反向，全部owned-Off再获取不重做guard。
stage后记录appliedLevels/known/acknowledged，整链后才提交consumer/aggregate；失败不回滚
parent、不reset/retry，保留确认值并poison，可能保留高guard，不能伪装目标已到达。
1ms posted读回/100ms stage/500ms总预算，timer推进，不在gate长忙等；整链跨LDO/ARC串行。
初始/每stage/屏蔽本槽IRQ后首payload前都查整链继承冲突；只动自己的IRQ位和真正空TCS。
有效票/在途/uncertain显示请求detach=Busy；stop晚事件不MMIO，有写free后原boot retired保护。
补ID/token耗尽不回绕；corner固定unsigned underlying，非法enum输入定义明确地拒绝。
**MDSS生命周期尚未调用ARC，默认/boot参数无变化，没有自动投票；0185 upstream仍inherited。**
不是实机电压/掉电证据，不具NoC/cold GDSC/PHY/PLL/host初始化，不能验收完整D1/面板驱动。
0186严格应用到0185树，无offset/fuzz，两源码字节一致，114原CI marker保留。真实XNU/KDK
arm64 RPMh基线/最终及MDSS最终对象通过；RPMh warning1228->1298，新增70全是固定数组
usage提示，已审索引/边界，不声称零warning。六套实际源码ASan/UBSan+Python ADT通过，
UBSan halt_on_error，日志无sanitizer报告；RPMh含首次guard/舍入/五阶段各处失败与确认值、
共享票/反撤/并发LDO、继承冲突/准入后新冲突、11坏ARC entry、stop/retire/exhaustion。
原显示/clock/FB/DSI/perf/ADT回归通过；LSan ptrace环境仍关，另断言对象/映射/分配/通知归零。
底稿scratch/framebuffer-0186/{base,edited,verify,prepare-patch.py,compile.py,verification.json,
warning-counts.json,reference-audit.json,tests.json,test-*.txt,*compile.txt}。实现依据与边界
见docs/PANEL-ARC-D1.md，资源/clock/PD0/架构/主计划和两份总路线已同步。
**NEXT实现display RSC/NoC typed带宽路由及其访问准入，再接实际MDSS资源/冷clock/GDSC/link生命周期。**
LG apps与disp RSC不同：display头af20000/TCSaf21c00/DRV0，sleep1+wake1+active2布局不能
直接复用apps ACTIVE0/1。_display BCM走rsc_disp，不盲发apps；BCM aux unit/width/VCD、
14bitXY/VALID/COMMIT及VCD完成，ACV特殊语义另核对。SDE reg/MNOC/LLCC/EBI路径需完整，
不把单MM0当全部带宽。sde_rscc自身有mdss_core_gdsc及RSCC/AHB/VSYNC依赖：先核对TCS/
wrapper各自访问条件及bootstrap顺序，不能先盲读未供电RSC。现在没新映射/写display RSC。
继续真实完整驱动，不回CPU双缓冲前置/单项只读云轮次/旧bug专项；A单缓冲B未提交、30Hz
复制未改，旧卡顿未定因，0121a13用户改善不能归因本段。暂不需要push，合并可运行批次再说。
未push/触发CI/操作设备，无新云产物/boot/run ID。CircleCI blob仍
 da0b6c0947a66f03491b3a9023590b49665292df；USB/按键/触摸/电池/mux未修改。

**以下0185按当时阶段保留，当前以0186为准。**


**CURRENT：0185本地温态时钟lease及FB息屏统计修复已实现、本地验证通过；未实机，完整D1未完成。**
父提交c02e345。新增0185-mh2lm-local-display-clock-leases.patch，SHA256=
84d019a5201587645f8974254788cedd05a38ffcf2f688300863ed1d4276b059，6份变化源码。
SweetMDSSClock.inc为现有MDSS内部typed模块，无重叠MMIO/provider空壳/C实例桥；沿同一
workloop/gate，retain(Core/DSI0)/status/release、四槽单调token、每分支引用、timer推进。
原子writer所有权字无实例指针；写过的实例free后retired，防止同boot丢账本重建；无写可重建。
分支AHB2080/root22bc、MDP200c/20c8、VSYNC2024/20f8、ESC2038/2148、BYTE2028/2110、
INTF202c/2110+div2128、PCLK2004/2098，保存CFG/MND/divider。只恢复明确XO/div1关闭分支；
非XO/PHY父源只保留已运行配置。继承enable绝不关闭；最后引用关自己新增分支前核实TE/auto/
IRQIdle/DSI DMA-MDP空闲及无scanout/DSI过渡，消费者不能给布尔idle绕过。写读回不算完成，
独立CLK_OFF状态确认；100us timer、单段10ms/请求200ms，无gate内长等待。
GCC AHB按LG critical/HWCG保留，HF AXI须运行；config MMIO准入通过后才读GDSCR3000，
PWR_ON1且SW_COLLAPSE/HW_CONTROL/SW_OVERRIDE全0才接收；不写GDSCR，不reset/cold enable。
故障/timeout/源漂移关闭后续写，保留已改分支；stop取消源/标Stopped而不假定DMA退出关钟。
sweetdisplayclocks默认0；1要求sweetdisplayresources2/v1 ADT，MDSS给DSI0获取实际boot lease。
原亮度/DCS28/29/DSI/scanout在新模式检查lease，不就绪显式拒绝；没有静默转旧写路径。
**上游MMCX/NoC/GDSC/非XO父源仍继承，不是完整供电/冷启动能力。** 新模式未加入boot矩阵，
没有要求单段云编译/刷机；旧0121a13 resource1参数不受新clock模块影响。
FB修复requestPanelPower实际转换清lastPresentNS，OFF没有vsync回调也不会混入CPUinterval；
first/latestPending不清，保留待提交语义。没有改变30Hz/复制/屏障或修复已定因的卡顿。
0185严格重放到0184验证树、无offset/fuzz、6源码字节一致、114原CI源码marker保留。
真实XNU/KDK arm64 MDSS/FB基线和最终对象都编译通过；新增42条固定数组usage warning
已核对索引和状态范围，FB告警不增，不声称零warning。六套ASan/UBSan测试及ADT检查全部通过：clock/
display-service/framebuffer-service/DSI/RPMh/FBperf及ADT（ADT为Python检查）。clock含16准入
错误/重复writer/分配失败/四token/引用/独立完成/关钟busy/源漂移/posted超时/stop晚事件/retire，
实际DCS/电源/DSI拒绝不就绪；FBperf走真实OFF/ON，期间无vsync，interval排除OFF且pending保留。
LSan受ptrace环境限制关闭，另断言对象/映射/通知/分配归零。没有实机时钟或投票新证据。
底稿scratch/framebuffer-0185/{base,edited,verify,prepare-patch.py,compile.py,verification.json,
warning-counts.json,reference-audit.json,tests.json,test-*.txt,*compile.txt}；实现/边界见
 docs/PANEL-CLOCK-D1.md，架构/PD/面板主计划/总路线/0121a13统计状态已同步。
**NEXT核对并实现真正MMCX/NoC上游依赖，再完成冷态clock/GDSC及PHY/PLL/host控制。**
LG mmcx为sm8150-regulator.dtsi的mmcx.lvl，不是四个LDO；sm8150-bus.dtsi还分apps/display
BCM及rsc_disp路由，不能把所有BCM盲发apps-DRV。下一段先核对DB资源类型/aux与RSC目标，
再接实际typed投票/引用；不要以物理运行位替代拥有的上游lease。确认profile/reset/完整ONOFF
接后续D2，不再回到CPU双缓冲前置/独立只读探针/旧bug重测。本段暂不要求push，合并可运行
驱动批次再通知用户；未push/启动CI/设备操作，无新云产物/CI run ID/boot。
CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df；USB/充电/按键/触摸/mux未修改。

**以下为0121a13日志阶段交接，最新代码进度以上段0185为准。**



**CURRENT：0121a13主档资源绑定、既有扫描接管/OFFON实测通过；用户反馈滑动有所改善，全部常用操作无新增异常。**
主档打包提交37c78a3，驱动d644866/0184。日志sweetlog-0121a13-panel-resources.bin，SHA256=
461a537553b33b12f8740035985eaa45d7442ed62820c397085e70c48fd6e236；440091有效bytes/flush77/
未回卷/5840行，resources1/perf1/own2/dsi0。86–95行四路DB地址ldoa14=42100/17=41300/
c3=41e00/a5=40b00，pre-v3 RSC id10700/config800c8104/ncpt16/ACTIVE2，consumer1真实绑定。
无ACTIVE vote，符合mode1，不证明写权限/电压/撤票掉电；D1整体仍未完成。
798–803行A=c3bb8000提交frame130→131/freshPPDONE100/flush0/active，B=c4670000未提交。
10完整IRQ窗600436ms/35963帧，加权59.895Hz/dropped0；不是UI帧率。OFF3ms/ON11ms，
frame37780→37781，owned resume verified；OFF内样本覆盖>=65.504s，无帧统计。
用户确认画面/亮度/触摸/三键/息屏唤醒全部正常；无需control/旧矩阵/半小时/充电专项。
47个perf窗1237次CPU呈现，copy+barrier均2400.732us/max4786us，无16.667ms超预算；
CPU present均2407.146us/max4792us。provider-query45973次均2.379us/max11966us，
4次>1ms集中唤醒相邻窗；通常最大4–52us。不支持复制普遍超预算，但尚未定因/修复旧滑动退化，
不把用户改善归因metadata或双缓冲。pending最大90.749s含OFF保留，不是亮屏卡死。
发现统计缺口：lastPresentNS只在vsyncFired看到OFF时清零，requestPanelPower实际取消timer/
暂停帧，可能绕过清零；本日志cpu-interval106.409s跨OFF。尚未改代码，下个有意义驱动批次
在实际电源转换处修，测试OFF无vsync回调；保留first/latest pending，不单独占编译轮次。
无ERROR/panic/mux隔离/显示失败；6WARN=3既有rawFSR/3USB端点，BUG-005 driverkitd仍在。
48电池样本全charging0，不验收充电；未关闭USB旧bug。详细见docs/PANEL-D1-0121A13.md。
底稿scratch/artifact-0121a13/{analyze-resources.py,panel-resources.txt,resources-log-analysis.json,
resources-analysis-summary.json,resources-selected.txt}。无新内核补丁，本轮仅分析/文档。
**NEXT继续D1 clock/GDSC获取、MMCX/NoC与PHY/PLL/host，再接确认profile/reset/完整ON/OFF；**
不重回CPU双缓冲前置，后续独立重建/ABL退出/零拷贝。无待答问题，不需要补刷本轮control。
未push/触发CI/操作设备；CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。

**以下为0121a13打包阶段记录，待测状态已由上段实测更新。**


**CURRENT：用户提供0121a13；已核对含0183/0184并打包两档boot，待主档短测。**
源码实现提交d644866；60相关源码逐字节匹配0184 verify（含其6份变化），114原CI源码marker通过。
归档manifest/arm64 Mach-O正确，kernel17567368bytes，SHA256=
20b2526037e9470f00cdf763d6a277a77a56d94d1bfca5e905606ab109c5be47；内核marker含命令/perf/RPMh。
归档buildtree SHA256=67ac2504974bb3f5381aebd358a4657c519007652ebaec4cd76929dfa1604375，
kernel归档=1a6dc8d51b14202dd5eb58645ed91b12adeff1b3f35d4e44ecb292fe8df38098。
没有build-log/CI run ID，不声称全量编译告警审阅。本轮新增打包档--panel-resources，无新内核补丁。
用户主动编好本批，因此复用其做资源绑定+合并性能采样，不新请求元数据云编译，不变更驱动优先级。
已生成out-bs/boot-16k-0121a13-panel-resources.img及同目录panel-resources-control.img，
均35667968bytes；主档SHA256=66aea9eaac31f76fb3f92cddacaa6de9954c643e92d12e38e2feafbc75d114f6；
control=f85c5dd3565005746c1c384f83315c20473a843e4857feab95ddd6ac529cbf56。
独立回读Android v1/2048 header、原UEFI blob、FAT内kernel/ADT/TOML均一致，原baseline ADT未改。
两档共用v1完整资源ADT/own2/irq2/vsync1/service1/power1/30Hz/runtime3/ACM，sweetfbperf=1、
sweetdsi=0；只差sweetdisplayresources=1/0。主档只映射/DB/metadata绑定，拒绝电源写，无自动票、
clock/GDSC/PHY/reset/vendor表操作。control仅故障时用，不重刷own0/1/F3矩阵/半小时/充电专项。
环境file(1)仍缺，仅scratch shell函数替换信息展示，正式打包步骤不变，最终另用Python/mcopy回读。
底稿scratch/artifact-0121a13/{verify-artifacts.py,verification.json,source-compare.json,actual/,
package.sh,packaging.txt,verify-boots.py,boot-verification.json}；步骤见docs/PANEL-D1-0121A13.md。
**NEXT用户fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a13-panel-resources.img；**
UI亮屏30s→连续滑动20–30s→静止15s→简测亮度/三键/触摸→手动OFF20s/ON15s，导出
sweetlog-0121a13-panel-resources.bin；USB live可选，持久日志够。看RPMh四路available/pre-v3/
matched consumer元数据，原hand-off active/resume及FB PERF copy/query/pending数据，不能提前验收。
0121a12滑动卡顿仍未修复，A单缓冲B未提交；D1整体未完成，技术下一段仍clock/GDSC、MMCX/NoC、
PHY/PLL/host与确认profile生命周期。不要求为本次打包脚本重编内核；未push/触发CI/设备操作。
CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df；旧USB/充电bug观察边界不变。

**以下为0184实现阶段交接，云产物状态已由上段更新。**


**CURRENT：0184资源ABI/RPMh provider与显示绑定已实现并完成本地验证；D1整体仍未完成。**
父提交87ba5bb（0183命令/计时）。新增0184-mh2lm-display-resource-leases.patch，SHA256=
202251a9eeebca4c43539ec84307f0d20dc7f9d7cfe1a8286858a73d7cbcd5b1，6份变化源码。
真实SweetRPMh IOService由ADT sweet,rpmh nub匹配；拥有RSC/TCS/commandDB、workloop/gate/timer。
迁移原sweet_rpmh.c的pre-v3/DRV2/ACTIVE2布局与继承保护，旧C仅在MH2LM排除，未另建竞争writer。
核对0121a12实际所有C/C++ caller：MH2LM TouchComm没有RPMh调用，旧touch/sec在MH2LM关闭，
GPU调用在GUACAMOLE guard；没有改当前触摸供电。没有通过充电SPMI API写面板电源。
attach/detach独立ID、4个typed rail（ldoa14/17/c3/a5）、最多8consumer、约束/load聚合、token/status；
最后撤票由调用者先停DMA/link，未拥有的继承电源不自动disable；有活动票detach=Busy。
timer推进写回<=1ms/总请求<=100ms，无gate内delay/长忙等；freshIRQ/执行转换+issued/complete+idle
后清trigger/mode/enable/wait，只W1C自己槽并保留他槽IRQ位；Busy不隐式重试，故障不reset。
仅全局原子所有权状态字，无实例指针/单例桥；任何写后实例free即本boot retired，拒绝丢票账本重建。
纯元数据实例可重匹配；不宣称动态故障恢复/睡眠投票或ACK即物理掉电证明。
MDSS通过SweetRPMhClient实际匹配通知+自己的IES/workloop attach/查询，terminated只撤回/release，
stop在owner gate外remove同步通知/事件再detach；无反向同步consumer回调，绑定不发任何电源票。
mk-mh2lm-mdss-adt.py新增--display-resources：v1、完整DISPCC0x20000、IRQ9..11、PHY0/PLL0
单窗口资源12(0x0ae94400/0x760)，另加RPMh三段窗口nub；默认/IRQ原ADT逐字节不变。
sweetdisplayresources默认0，1只绑定/metadata且拒写，2只允许typed请求，开启参数本身仍不发票。
新mode资源不全显式拒绝；不读取尚未获clock的PHY/GDSC。尚无独立测试boot，不要求本段上机。
PD0独立clock/GDSC provider设计明确修订为MDSS内部typed资源模块，沿现有唯一MMIO所有者，
不另映射第二writer；当前只有窗口/所有权，acquire、父源/频率/GDSC控制并未写好。
严格apply至verified0183无offset/fuzz、6源码字节一致、114原CI marker保留；真实XNU/KDK
arm64-apple-darwin RPMh/MDSS/FB/旧C/KernelConfigTables五对象编译通过，旧C nm无RPMh符号。
ASan/UBSan实际代码：元数据不写、8ID/旧ID-token、并发票gate、聚合撤票/电压冲突/负载溢出、
独立fresh完成、继承冲突、6类fault/stop/晚事件、真实MMIO action与stop同步、retire重建拒绝、
11坏资源、actual matched→MDSS attach→terminated通过。原显示/F2/F3/F4、FB、DSI、perf、ADT
6套通过；LSan因原ptrace限制关闭，显式对象/映射/通知计数归零。编译有既有宏/有界数组警告，
两个新IOReturn格式警告已修；不宣称零warning或硬件通过。
底稿scratch/framebuffer-0184/{base,edited,verify,verification.json,tests.json,warning-counts.json,
test-*.txt,*compile.txt}。实施与边界见docs/PANEL-POWER-D1.md；主计划/PD0/架构/总路线已同步。
**NEXT补clock/GDSC获取、MMCX/NoC依赖与PHY/PLL/host，再接确认profile/reset/完整ON/OFF；**
0183命令表/性能统计已可复用，不回拷贝双缓冲/单项版本探针轮次，不改已有USB/按键/触摸/电池。
0121a12滑动卡顿仍未定因/修复，A单缓冲B未提交；不要把本地资源层通过写成完整PD2/PD3完成。
暂不要求push，合并实际可运行驱动批次再通知；未push/启动CI/操作设备，无新云产物/run ID/boot。
CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。旧F3矩阵/半小时/BUG-002不专项重测。

**以下0183记录按当时状态保留；其资源层缺口现由上段0184部分更新。**


**CURRENT：已按用户要求优先完整面板驱动；0183完成D1命令基础及合并性能计时，本地验证通过。**
优先级修订提交91d85bf；本提交提供0183-mh2lm-dsi-command-sequencer.patch，SHA256=
3bf924da1b0101024922343e21438eafcfb201f399f06eac7988146e0be6815f，9份变化源码。
用户确认0121a12 single滑动卡顿明显，仍是A单缓冲/B未提交，不能写双缓冲已启用或体验通过。
0183新增SweetDSICommand.h/SweetMDSSDSI.inc，现有IOService/gate下typed begin/status/cancel，
复制并整表校验OSData、token隔离、LP/VC0/05/15/39；FIFO64、内存embedded DMA256分开，
最长252payload，PV149整包不拆；生产没有自动选择/发送未确认的PV表（仅离线fixture）。
默认sweetdsi=0；=1只开放内部请求，本身不发包。timer推进idle/DONE/90或150ms延时，
无gate内长等；先清并确认旧DONE，新的DONE+DMA/MDP idle才复用/解pin。内存命令独立
4KiB低32位连续WC/prepare descriptor，写地址前retain；超时/在途取消保留独立引用至重启。
错误/时钟丢失/部分已执行表取消明确unknown/阻止DCS，不翻回ABL；最终abort/recovery未补。
完整表可能超过RD_PTR250ms watchdog，要求生命周期模块先暂停frame/IRQ，当前非IrqIdle
则Busy；执行器不偷偷改看门狗/重启帧，也不推断不透明表执行后的面板电源状态。
复核0121a12既有日志DSI0 HW_VERSION=20030000（191/402/603行），0183写前复核；PHY/PLL
尚不知。LG DSI使用unsecure aspace，DTS801是secure domain，不能当DSI独立SID。
新DMA检查共享unsecure800/SMR3/CB2已知无转换/故障条件，不新映射路由；仍待实机新传输证明。
sweetfbperf=1在serialized FB可选统计，默认关闭；每15秒汇总copy+barrier字节/耗时、CPU
present/间隔、provider query、pending first/latest年龄、coalesced，p50/p99桶上界/max/预算。
不改30Hz、memcpy或屏障；尚无实机计时，未定因/未修复卡顿，不把IRQ频率当UI呈现。
验证：0183对verified0182严格apply，无offset/fuzz，字节比较一致，114原CI marker保留；
基线/最终重放树MDSS和FB真实XNU/KDK arm64-apple-darwin对象均编译通过，新增16/19条
有界数组/指针警告已审。实际代码ASan/UBSan：PV17/4整表/149长包、等待/冻结TE、不可变
请求、独立freshDONE+idle、staleDONE、20类admission/资源失败、token、取消/timeout/error/
clock/stop/直接free与pin、部分表取消通过；FB计时/合并/两种pending/OFF/清窗/默认不读clock
通过，原显示服务/F2/F3/F4测试通过。LSan受ptrace环境限制关闭，另有对象/prepare/complete断言。
底稿scratch/framebuffer-0183/{verification.json,tests.json,warning-counts.json,test-*.txt,*compile.txt}。
实现与范围见docs/PANEL-COMMAND-D1.md；主计划/PD0/架构同步。PD2–PD5完整硬件初始化
尚未完成；D1资源ABI、统一RPMh原TCS writer/lease、clock-GDSC尚缺，不能把设计图当已存在服务。
**NEXT继续D1剩余资源/时钟及PD2 link，再接PD3完整profile；不回到拷贝双缓冲前置。**
这一段不单独占云编译/刷机轮次，暂不要求push；待可运行批次再通知用户。不补F3矩阵/半小时/
BUG-002/旧充电专项。未push/触发CI/操作设备，没有0183云产物/boot或CI run ID；未改USB/
按键/电池/触摸/mux/GIC callback策略，CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。

**以下为优先级调整时的记录，当前实现进度以上段0183为准。**

**CURRENT：用户要求完整面板驱动提速；撤销“先把拷贝双缓冲/F4性能全部做完”的前置，下一代码交付转PD2/PD3基础。**
用户明确反馈0121a12 single“比较明显，滑动比较明显”，是本轮体验性能回归，不能再写成
未反馈或正常验收。已实测的是A单缓冲接管/OFFON，B未提交，30Hz CPU复制仍在；不能归因
尚未接入的双缓冲，也不能用约59.91Hz IRQ反驳主观卡顿。位置/花屏等其它视觉项仍未单独确认。
本轮核对真实0182代码：refresh只启动一次；Active后scanoutStatus每次vsync跨provider gate，
逐层WC目标memcpy后dsb。无逐帧copy耗时，暂不定因WC/屏障/gate，也不删屏障或强开60Hz。
新顺序：已有PD0/0182→PD2/PD3安全停止/资源/链路/面板实现→PD4独立重建→PD1余ABL依赖/
PD5必要生命周期→F5直扫/共用完成SwapWait→F7；拷贝双缓冲改兼容路径按需，不再独立占
云编译轮次。DMA停止/真实完成/token与持有规则仍为硬件重配/直扫必要基础，不维护两套。
D1下一代码交付：资源ABI/版本保护、统一RPMh原TCS writer/显示lease/clock-GDSC所有权，
typed DSI消息与支持完整vendor长包的传输/可撤销序列；D2整合PD2 link与PD3 panel动作，
D3独立重建/无ABL首帧console/生命周期，D4零拷贝，详细门槛见PANEL-FULL-DRIVER-PLAN第7节。
未确认panel profile/E1或link/E4时只拒绝对应写入，不把整个资料表变成前置测试清单；
LG FIFO_STORE在dsi_ctrl.h仍限64bytes，dsi_ctrl_hw.h另处提256，不能据后者直接扩大当前FIFO。
下一驱动批次合并低频呈现/copy bytes+耗时/pending年龄/合并/provider查询计时，无独立计时
云编译/只读探针轮次。现有0121a12 own0/1可作对照，但本轮不要求用户重刷，不补旧长测。
主面板/总路线/F4/PD/PD0/架构/接管记录已同步优先级和性能反馈。当前回归属于显示任务，
不误记为旧已完工功能的独立BUG。本轮只有路线/证据修订，没有新内核补丁或驱动实现，
不用push重编；未push/启动CI/设备操作。CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
NEXT直接按D1实现，不再从拷贝双缓冲或单项预检开始；不需用户重复授权改顺序。

**以下为0121a12日志交接；“画面反馈待补”已由滑动性能回归反馈部分更新。**

**历史日志交接：0121a12接管/OFFON日志通过；最新滑动回归与新执行顺序以上段为准。**
日志sweetlog-0121a12-fb-scanout-single.bin，SHA256=
915f877f08464d5cfe069498e589adfaac949835f0ecc5dabc657b42edeed70c；364415有效bytes/
flush69/未回卷/4990行，371行确认own2/irq2/vsync1。171–172池A/B同0181，seed167014us。
1088–1093 quiescing→accepted→submitted(frame130/A=c3b30000/stride4608/crop22,48/
idle-line2340)→transfer observed(frame131/flush0/newPP0DONE100)→FB active。首次ON后
3821–3825快照frame27980，实际A/stride1200/XY300016，owned resume verified。不是仅分配
或寄存器读回证据，但画面内容/位置/持续更新仍待用户确认；已async询问，不伪造反馈。
四份SMMU/CB2相同globalfault0、CB2.M0，FSR仍raw400不命中LG faultmask，无新已知fault位；
FAR本轮0001fdfe:ba6e8cff不解释为有效故障地址。3条预检WARN仍来自raw400，接管按显式条件通过。
8个完整IRQ窗480.283s/28775帧，59.879–59.931Hz，加权59.913；OFF前7/首次ON后1，
dropped全0。6组DCS off/on全rc0：off1/14/2/9/4/11ms，on10/4/8/10/6/6ms；每次暂停帧/
entry与重启一致，ON首帧+1。首次OFF内4份电池完成样本跨度49.139s，OFF无MDSS帧统计。
8份FB active快照coalesced首次0，幂等ON8，6次后续ON均8，无FAILED/显式软件回退。
32键事件配对power13/up1/down2对，42touch；亮度2775→0→2775。无驱动ERROR/mux隔离/
poison/SPMI timeout/panic。6WARN=3rawFSR+3USB端点；2reset均ACM成功，BUG-001不关闭。
driverkitd791–795仍sig9/ns1/code7（BUG-005）。10条hfs_clonefile cluster_write failed34
(ERANGE)，更早0121a10预检5条/0121a6 MDSS3条同签名，且本轮966/998行在切源前已出现；
仅保留既有文件克隆异常观察，原因/影响未查，不据时间邻近归因接管。40电池样本/39refresh
healthy1，全gauge charging0/ma-132..0；启动ENABLE一次，无VERIFY-12，不当充电成功。
底稿scratch/artifact-0121a12/{analyze-single.py,single-log-analysis.json,single-analysis-summary.json,
single-selected.txt,fb-scanout-single.txt}；接管文档第6节/主计划/PD/PD0/架构同步。
历史NEXT已被上段加速后的D1–D4顺序覆盖，不重刷own0/1/F3/半小时/旧bug专项。
当前仍单缓冲30Hz/提交池pin至重启，不能验收安全回收/资源平衡/完整PD1。
本轮只日志分析/文档，无新驱动/参数/CI改动或编译/push/设备操作；CircleCI blob仍
 da0b6c0947a66f03491b3a9023590b49665292df。旧BUG边界及整机PM大目标四不变。

**以下为0121a12打包交接；待实机状态由上段日志结果更新，画面反馈仍待补。**

**CURRENT：0121a12云产物已核对包含0182，三档boot已打包并回读验证；NEXT先实测single。**
对应实现f78abc8；52份源码与0182验证树逐字节一致（含8份变化），114条原CI源码marker及
继承/新增内核marker通过，kernel归档manifest正确，Mach-O为17526608bytes。
kernel SHA256=95ee2dec7b39c73d471d4430798b0568af047bce26bb2a15c86a63807c521c33，
与上一0181内核不同；归档kernel SHA256=
104554f72f0e1f34e60a76dfb86b51df13e3d7ab328709c6912aa9d9079b5dc0，buildtree SHA256=
fd5626f1f2c968977a9a6636c704f5278259700fbb7a1cdf3aaa9d946b68869d。
没有本次build-log/CI run ID，不声称全量云告警已审。底稿scratch/artifact-0121a12/
{verify-artifacts.py,verification.json,source-compare.json,actual/,package.sh,packaging.txt,
verify-boots.py,boot-verification.json}。
已生成out-bs/boot-16k-0121a12-fb-scanout-{baseline,preflight,single}.img，各35667968bytes；
独立回读Android v1/2048header、原UEFI blob、FAT内kernel/ADT/TOML全部一致，三档只差
sweetfbown=0/1/2，原ADT未改，IRQ/ACM/runtime3/F2/F3参数保留。single SHA256=
9683dd4adb3dc90ced254128dc216b6c1c3eebf4ff445dbfbf5d45d289f2129d；baseline/preflight SHA
及完整参数见boot-verification.json。环境没有file(1)，仅用shell函数替换其信息展示，真正
打包脚本未改，最终另用Python和mcopy回读核验，未掩盖打包步骤失败。
NEXT用户只需fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a12-fb-scanout-single.img。
进UI亮屏约60s，简测画面持续更新/位置/亮度/三键/触摸，手动OFF20–30s/ON再亮屏20–30s，
导出sweetlog-0121a12-fb-scanout-single.bin。分析quiescing→submitted→transfer observed→
FB active，源地址A/stride4608/crop22,48及owned resume verified；不能单凭能进桌面验收。
若冻结/不亮，保留持久日志；baseline/preflight只需故障时对照，不重测F3/半小时/旧bug专项。
仍是单缓冲30Hz，提交池有意pin至重启，非最终回收/双缓冲/PD1首帧console验收。
本轮只核对/打包/文档，无新驱动或参数设计、CI变更/push/设备操作；CircleCI blob仍
 da0b6c0947a66f03491b3a9023590b49665292df。F4/PD1待实机，BUG与整机PM边界保持。

**以下为0182实现交接；待云产物状态已由上段0121a12核对结果更新。**

**CURRENT：0182受控单缓冲接管已本地实现/验证，待用户push、新云产物及实机；F4/PD1未验收。**
基于d0c075a和已实测的0181/0121a11；新补丁0182-mh2lm-controlled-scanout-handoff.patch，
SHA256=97295c1e6973e006d98300070005a652c8c7ae1245c37939dd245d898ee6fbfa，8份源码变化。
新增sweetfbown=2；own0默认/own1只读预检保持。AppleCLCD仍持有池，经typed
beginScanout/scanoutStatus/abandonScanout调用保留的SweetDisplayBackend；SweetMDSS拥有
MMIO及自身workloop/gate，没有consumer MMIO或provider反向同步回调。第三份快照后在FB
workloop重新复制当前ABL面板矩形至A(22,48)，WC屏障；一次约10MiB复制可造成启动延迟。
provider只允许一次尝试：核对已知SMMU/TRANS CB2、S1/S2 bypass、继承布局和IRQ真帧；
允许原raw FSR0或400，真实fault/新未知位拒绝，不清FSR/RESUME/页表。断TE/关auto后用
1ms timer、最多50ms等待两次write-line空闲；清且确认旧PP0 DONE，先加硬件pin再写XY/
stride4608/地址A/VIG0 FLUSH；不发CTL_START。恢复auto/TE后100ms内要求新PP0 DONE+
flush0+RD_PTR推进；不是通用flip/reuse fence。FB查询Active后CPU目标才切A；仍单缓冲、
30Hz、原CPU裁剪/原地更新，可能仍撕裂。首次OFF/ON第4快照复核自有地址/裁剪/route，
失败明确ERROR/属性并冻结呈现。DCS在接管中返回Busy不记硬件故障预算；电源请求取消
接管后仍执行原路径。stop/free关admission、cancel并移除timer后释放引用，含未调用stop清理。
重要实验边界：首个可能生效的源写入前pool取独立retain；一旦提交，整个A+B约21.41MiB
有意保留至重启，跨provider/consumer终止也不释放。不推断DMA退出，不自动翻回ABL；
提交前取消可恢复TE/auto并正常释放CPU-only池。最终可卸载/回收协议尚缺；不能将此当作
资源平衡完成。安全停止/撤销、提交代号和完成关联、A/B复用、SwapWait/60Hz后续补齐；
PD1还缺无ABL首帧/default surface/console/panic，PD2–PD5完整初始化亦未开始。
严格apply零offset/fuzz、应用后逐字节一致、114条原CI marker通过；MDSS/FB基线和验证树
arm64-apple-darwin对象编译均rc0。MDSS新增5有界数组+17 SDK命名诊断，FB新增2数组/5指针
诊断已审。实际provider ASan/UBSan 20类接管/故障/stop/free场景通过，含busy/staleDONE/
freshDONE/flush/IRQ分离、SMMU/fault/readback/超时/电源打断和无stop直接free两态；FB真实
代码验证目标/crop/stride、ABL不写/失败冻结/ON检查/stop detach；pool验证refresh边界/
pin幂等及跨服务free保留，模拟reset才回收。旧F1/F2/F3测试仍通过。LSan仍因ptrace环境关，
另有引用/prepare和guard断言，不声称硬件DMA已停止。底稿scratch/framebuffer-0182/
{base,edited,verify,verification.json,tests.json,test-*.txt,*compile.txt,warning-counts.json}。
打包器原--framebuffer-scanout仍两档own0/1，新--framebuffer-handoff生成同核baseline/
preflight/single三档，只差own0/1/2。真旧F3核及0181核被对应marker门拒绝，在make_profile
前退出，未用旧核造新实验boot。当前无0182云kernel/buildtree/build-log或CI run ID。
NEXT：用户push本轮提交并下载新kernel+buildtree（有build-log一并核对），确认0182源码/
manifest/marker后打包；先fastboot boot ...-fb-scanout-single.img。进UI亮屏约60s简测
位置/亮度/三键/触摸，OFF20–30s/ON亮屏20–30s，导出sweetlog-编号-fb-scanout-single.bin。
看quiescing→submitted→transfer observed→FB active、地址A/stride4608/crop22,48、owned
resume verified与画面持续更新。own0/1只故障对照，不重测F3/半小时/旧电池/BUG-002专项。
详见docs/FRAMEBUFFER-HANDOFF-F4.md；主计划/PD/PD0/架构同步，路线不变。
未改USB/按键/电池/触摸/mux/GIC callback或ADT；BUG-001/003/004/005状态保持、BUG-002
仅常态观察，整机PM仍大目标四。未push/启动CI/操作设备；CircleCI blob保持
 da0b6c0947a66f03491b3a9023590b49665292df。

**以下为0181预检实测交接；当前执行点以上段0182待云构建/实机为准。**

**CURRENT：0181/0121a11扫描池预检实测已分析；双池/seed/四快照及基本回归通过，NEXT受控单缓冲接管设计。**
基于8f6c5b8，日志sweetlog-0121a11-fb-scanout-preflight.bin，SHA256=
8cc34d8a15178179031ef8f155e348ce84d3d06649a08d63cb5bb5675ddd20cc；有效343538bytes/
flush76/未回卷/4561行。用户确认画面、亮度、三键、触摸和手动OFF/ON“都正常，未见新增问题”。
171–172行pool ready、seed成功：A0xc3b30000..0xc45e4800/B0xc4668000..0xc511c800
（末端不含，逻辑字节范围）；各11225088bytes/stride4608/crop22,48，16KiB对齐、低32位、
不相交/不覆盖ABL18MiB。seed167265us为启动清零/复制耗时，不当逐帧性能或新池显示验证。
866/972/1079/3853四快照来自frame1/61/121/17929；地址全9d200000/secure0，扫描仍ABL。
四份SMMU记录一致：SID800唯一SMR3/TRANS/CB2，SCTLR e0/M0、TTBCR/TTBR/MAIR0、globalfault0。
CB2原始FSR400/FAR0001fdfe:ba6e8cf7引发四WARN；LG arm-smmu-regs.h的FSR_FAULT=c00001fe，
bit10未定义，400&mask=0，context handler亦按mask判fault。不能说已证明DMA故障，也不能
称raw全零/清掉旧故障/新DMA可达；不将FAR当有效故障地址。保留原值/复核，未改代码或清状态。
GIC hits1，FB首次/幂等ON/唤醒均hardware active；九整窗540.191s/32358帧，59.871–59.916Hz
加权59.901，dropped0，OFF前4/ON后5。OFF暂停17928/17929，ON armed同数/首帧17929；
OFF内7个电池样本跨度至少98.294s。DCS OFF/ON rc0，backend9/7ms、FB10/7ms。无FAILED/
显式回退/驱动ERROR/mux隔离/poison/SPMI timeout/panic。16键事件power3/up1/down4对、12touch，
亮度2775→0→2775。47样本/46refresh全healthy1，末796.659s；全gauge charging0/电流-132..0mA，
仅启动ENABLE一次，供电来源未另确认，不当真实充电成功。USB两reset/两ACM/三WARN，VERIFY0；
driverkitd780–783仍sig9/ns1/code7，BUG-001/004/005不关闭，BUG-002只常态观察、BUG-003暂缓。
底稿scratch/artifact-0121a11-f4/{analyze-preflight.py,preflight-log-analysis.json,
preflight-analysis-summary.json,preflight-selected.txt,fb-scanout-preflight.txt}。
F4第9节/主面板/总路线/PD/PD0/BUGS已同步：首段运行通过，E2取得池/live部分；新DMA/接管/
完成/双缓冲/SwapWait及PD1无ABL首帧/控制台尚未完成。无需刷own0或重测F3/半小时/旧BUG。
NEXT据本轮证据设计受控单缓冲接管，先核对缓存/格式/裁剪/autorefresh提交与完成关系，明确
硬件持有buffer的生命周期与失败保留/停机策略；区分已知fault位与未解释raw值，另设接管档，
不改变own1只读预检语义。路线PD0→F4/PD1→PD2–PD5→F5不变，不宣称完整面板接管。
本轮仅分析/文档，无驱动/boot参数/CI变更，无编译/push/设备操作；CircleCI blob仍
da0b6c0947a66f03491b3a9023590b49665292df，整机PM仍目标四占位。

**以下为本次产物及打包交接；待实测状态已由上段更新。**

**历史：用户更正为0121a11；替换后的产物已确认包含0181，扫描池预检boot已打包并独立校验。**
本次logs/xnu-{kernel,buildtree}-0121a11.gz为本日18:53替换包，须区别历史同编号F3产物。
kernel归档SHA256=afd78ed2eddebd2f12e64742ba2641557146c211d42062d014117625095f365d；
buildtree归档SHA256=65cb7ab5d97e8bd6f3d7087b22cbf77ba157c5573fa1c8bdd20e85a42b9de6a7。
解包MH2LM kernel为17525432bytes，SHA256=b605b12db6e6f8a5edaae1182560b50589f099bb9d4f10fce1fcf5a5e5469234；
归档manifest通过，52份源码逐字节匹配0181验证树（含9份变化文件），114原CI源码marker及F3/F4
内核marker通过，对应实现42047d3。无本次build log/CI run ID，未宣称审完全部云端告警。
已生成out-bs/boot-16k-0121a11-fb-scanout-{baseline,preflight}.img，各35667968bytes。
baseline SHA256=9ed8681e1fe29bfaa9048ee9bb89ceeda976546eb3b0ee652ef70786d86bfba4；
preflight SHA256=5823bdbc9917292f578239b032ec25558b17c0e0c9a07aa1c1b62551414864d2。
从最终boot重新解析Android v1头及FAT内容，kernel/ramdisk/UEFI/扩展ADT/TOML匹配；两档参数
只差sweetfbown=0/1，保留irq2/vsync1/F2/ACM/按键/触摸/电池runtime3，无重复参数。
底稿scratch/artifact-0121a11-f4/{verification.json,source-compare.json,boot-verification.json,
verify-artifacts.py,verify-boots.py,packaging.txt}；历史scratch/artifact-0121a11/及旧F3 boot不覆盖。
NEXT：只先fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a11-fb-scanout-preflight.img；
亮屏45–60秒，简测亮度/三键/触摸，手动OFF20–30秒/ON后10–20秒，导出
sweetlog-0121a11-fb-scanout-preflight.bin。baseline仅故障对照，不重测F3矩阵/半小时/旧BUG专项。
0181仍是池分配/seed/只读live快照，尚未切扫描地址，画面改善不属本轮预期；F4/PD1尚未验收。
路线PD0 → F4/PD1 → PD2–PD5 → F5不变。本轮无驱动/CI改动，无push/设备操作；CircleCI
blob仍da0b6c0947a66f03491b3a9023590b49665292df。详情FRAMEBUFFER-SCANOUT-F4.md第8节。

**以下为此前误报0121a10的核对；正确产物已由上段更正。**

**历史：用户报新产物0121a10，核对发现仍是历史0179内容，不能用于0181/F4。**
logs/xnu-{kernel,buildtree}-0121a10.gz修改时间为本日18:39，压缩包SHA与历史不同，
但解包MH2LM kernel SHA仍b6a0732a419f28276b6b6f8fba7c15f711d66394e66ee200043066a0aeda397e
（17503376bytes），与此前0121a10完全相同。历史核对的44份源码SHA全部相同，无缺失/变化。
新包缺0180的SweetMDSSIRQ.inc/SweetFBVsync.inc和0181四个扫描池/预检文件；内核无F3v2/
SweetScanoutPool/SweetScanoutPreflight/sweetfbown。不能只按文件名或mtime认定新代码存在。
未打包新boot，未覆盖旧artifact-0121a10证据；本轮核对底稿为
scratch/framebuffer-0181/incoming-0121a10-verification.json（含本次两归档SHA/缺失文件）。
logs没有另一个更新kernel/buildtree，亦无本次build log；下载旧任务、构建旧提交或补丁未入
等原因尚未区分，不擅判云端故障。NEXT用户核对正确任务产物；若确是最新任务需本次构建
日志进一步定位。PD0完成/F4待测状态不变，无驱动/参数/CI变更，无push/设备操作。

**以下为PD0设计交接；0181预检须先取得正确新产物。**

**CURRENT：PD0静态资源核对/接口设计已交付；下一步0181新云产物扫描池预检，之后F4/PD1。**
基于1872425，新增docs/PANEL-RESOURCES-PD0.md，同步PD总计划/主面板/总路线/F4/架构。
直接核对LG d4f335e13d73c4aa2af85581a6ec11b670ee5b94实际DTS/DRM/dsi-staging/LG override。
确认参考路径为DSI ctrl v2.3、PHY v4.0、PLL7nm系列（v2 overlay改7nm_v2），不能按旧PHY
v3.0/10nm头文件名选实现；本机版本仍待核验。面板vddi/vpnl=ldoa14/ldoa17，host/PHY另需
ldoc3/ldoa5，属RPMh投票，不经充电SPMI白名单。use-labibb分支实际启用panel power_info
里的vddi/vpnl，LCDB在LG common-pm禁用、基础AMOLED节点disabled、PV PMI8998块为注释；
不据名字猜额外偏压寄存器。现有RPMh有DB/TCS/完成及poison，但仅触摸vreg helper/ARC直投，
无面板lease/撤销/多consumer聚合；新provider必须统一原TCS writer，不能两套锁争同一DRV。
0121a11真实buildtree提取sweet_rpmh.c SHA256=be347e987f70ec41831f61e4012a83980d254a45390a3e1c4280c9cab352c6e8。
PV严格解析ON17包/694bytes，最大D3 payload149bytes；CE67/E2135/DF92也超64bytes；OFF4包/
35bytes，等待/29→11次序有核对。现有SweetMDSS dcsWrite最多16words=64编码bytes，不可
只扩大上限/切碎vendor长命令；需typed message与受支持DMA/完成生命周期。当前ADT缺完整
PHY/PLL/GDSC/clamp/mixer/PP/VBIF资源，不能越界访问；两个clock位检查不是初始化/使能。
设计确定：面板序列先为SweetMDSS同gate内部模块，link模块统一host/PHY/PLL；资源经独立
provider/lease，不做互相等待的Panel↔MDSS服务。长等待timer推进，不持gate/TCS/SPMI锁
跨150ms延时；完成带generation，停止先关admission，再同步在途操作和DMA后释放。
文档含完整资源表、LG上/下电依赖、partial-failure回滚及E1–E8验证门槛。PD0静态设计完成，
实际panel/revision、live资源/写权限未实证，不宣称PD1/完整驱动完成。触摸引脚与GPIO6/8
不重合，但共享供电/reset后协调仍需核对。限定logs/evidence95文本未找到panel选择参数，
不重启Android取证。旧android-mh2lm.dts含v2 PLL但本轮未重验来源，不当当前硬件读数。
底稿work1/scratch/panel-pd0/{reference-audit.json,sweet_rpmh.c}，含19参考源码SHA/命令逐包
元数据与表SHA。文档链接/差异和源表边界校验通过；本轮无内核补丁/参数/CI/boot/设备变化。
NEXT：0181仍42047d3、own1仅预检，待用户push/新云kernel+buildtree（F4尚未实测）。核对
后打同核baseline/preflight两档，只先测preflight；E1/E4–E8不阻塞扫描池预检，E2/E3仍是
扫描接管/释放旧buffer门槛。不重测F3矩阵/半小时/旧BUG专项；不为本轮文档单独编译。
未push，工作流blob仍da0b6c0947a66f03491b3a9023590b49665292df，BUG及整机PM边界不变。

**以下为PD计划制定交接；当前执行点以上段PD0交付为准。**

**CURRENT：用户要求完整接管面板，已新增PD0–PD5分步计划；下一执行点为PD0资源/接口核对。**
新增docs/PANEL-FULL-DRIVER-PLAN.md，已同步主面板计划、总路线、F4记录和架构边界。
最低先解除ABL缓冲依赖（含首帧/控制台/panic）；最终要具备控制器/DSI/PHY/PLL、面板
供电/reset/厂商命令初始化，以及显示链路状态失效/断电后的独立重建，不能仅继承ABL配置。
执行顺序PD0 → F4/PD1 → PD2–PD5 → F5 → F6按需 → F7。PD1复用F4工作，不另做一套
扫描池；新增无ABL像素首帧与自有故障缓冲验收。PD2显式重建显示链路，PD3完整面板
生命周期，PD4实测断电重建，PD5 IOPM/故障/终止收口；F5零拷贝延后，F7不再强制翻回ABL。
面板型号版本、LAB/IBB电压/顺序、共享资源等仍有待核对项，不将PV候选或文档值当实机确认。
最终默认驱动不能隐式回退ABL；过渡对照路径可保留但不计验收。panic不能简单用29恢复
已断电面板，也不能等普通workloop；自有扫描内存须在确认DMA退出后才能释放。
本轮基于8adbc56，只有文档修订；无驱动/补丁/boot参数/CI改动，无新编译/实机结论。
F3验收仍89e57b9；F4实现仍42047d3/0181（SHA及验证见下段），own1仍只读预检，待用户
push及云产物后实测，不能宣称已脱离ABL。无push/设备操作；后续先PD0并利用0181证据。
资料work1/docs/{INDEX.md,panel.md}和LG实际DRM/SDE/dsi-staging/LG override；不猜厂商配置。
整机低功耗仍大目标四，显示自身电源/恢复已纳入本PD计划，不再延期到整机PM。
BUG-001/003/004/005状态不变，BUG-002正常观察。CircleCI blob保持
da0b6c0947a66f03491b3a9023590b49665292df。

**以下为此前0181实现交接；待测事实保留，执行顺序以新增PD计划为准。**

**CURRENT：用户授权开始F4；首段0181已完成本地验证，提交42047d3，待用户push/云编译。**
前置基线89e57b9，F3实测0121a11通过。新补丁0181-mh2lm-scanout-pool-preflight.patch，
SHA256=6218d61838cc92a87ab97c680d8ce7a14d690405d1ea27ce3d17b7597d29ef41；9个源码文件变化。
重要纠正：LG实际DRM/SDE的SSPP +0x70 ADDR_SW_STATUS由setup_secure修改，是安全属性，
不是active address或完成状态；SRC0_ADDR读回/下一RD_PTR也不能直接充当release fence。
面板计划已纠正，F4分为池与live预检→单缓冲接管→真实完成关联/双缓冲→SwapWait/性能收口。
本首段默认own0不分配/不预检；own1要求service1/mdss1/power1/irq2/vsync1/contract0。
AppleCLCD持有SweetScanoutPool，经标准IOBufferMemoryDescriptor申请两个1125x2436/stride4608
连续/WC/低32位缓冲，每个11225088字节，prepare+物理segment/边界/重叠检查；在任何显式
清零/复制前排除与整块18MiB ABL保留区重叠，再只读1080x2340/stride4320面板矩形，复制到
A的22,48，B/边缘/padding清零。启动样本不用于未来最终接管，内核页取整不算作逻辑字节。
typed inspectScanout(stack)经SweetMDSS自己的gate和已有ADT映射只读SSPP/CTL/INTF/MDP/TE/
SMMU/CB2。未知ID、多重match、非CB2不猜布局；fault不清除；快照非原子，不拿锁存估完成Hz。
从F3真实帧触发前三份约隔60帧，加首次后续ON一份；整启动最多4次含失败，OFF不读，无新timer。
本首段不写新扫描地址、ABL仍扫描、CPU仍30Hz、SwapWait未变；没有DMA可达/翻页成功结论。
分配/prepare/物理范围/alias失败明确ERROR、失败属性并关闭首段保留F3；预检错误消耗预算，
不停止F3。stop关admission/订阅后释放尚未给硬件的池，free覆盖部分启动失败。
实际pool/provider/FB的ASan/UBSan通过：12类pool失败及真实几何/guard/边界/配对释放；
FB另测覆盖可见ABL和其后保留区的alias，确认不先写像素；typed MMIO全只读、未知/多重route/
关时钟/stop拒绝，原DCS/F2/F3/dispatch/close仍通过。LSan仍因ptrace关，另有对象/资源断言。
0181严格应用零offset/fuzz、逐字节一致、114原CI正向源码marker通过。两个cpp基线/验证树
均arm64-apple-darwin对象编译通过；MDSS新增6/FB新增23条有界数组/指针诊断已审，FB另4条
OS元类宏既有类别告警，其他计数不变。底稿work1/scratch/framebuffer-0181/{base,edited,verify,
verification.json,tests.json,test-*.txt,warning-counts.json,*compile.txt,strict-apply.txt}。
打包新增--framebuffer-scanout：fb-scanout-{baseline,preflight}两档只差own0/1，其余F3硬件/
IRQ/ACM/runtime3/F2不变；实测旧0121a11 kernel被F4 marker门拒绝，未造旧核新实验boot。
NEXT：用户push后下载新kernel/buildtree/build-log，先核对0181完整源码/marker/manifest再打包。
先fastboot boot ...-fb-scanout-preflight.img；进系统亮屏45–60秒，简测亮度/三键/触摸，手动
OFF20–30秒/ON后10–20秒，导出sweetlog-编号-fb-scanout-preflight.bin。看pool ready/seed/
4份live快照、地址仍9d200000、输入和唤醒健康。baseline仅故障对照；不重测F3矩阵/半小时/
旧充电和BUG-002专项。首段通过再做接管与完成事件，own1语义不悄悄改成切地址。
详见FRAMEBUFFER-SCANOUT-F4.md。F4未实机验收，无新云kernel/boot/CI run ID，无设备操作/
push。CircleCI未改，blob仍da0b6c0947a66f03491b3a9023590b49665292df；原USB/按键/触摸/
电池/mux源码未变，BUG-001/003/004/005状态保持、BUG-002只常态观察，整机PM仍目标四占位。

**以下为历史交接；当前实现与下一步以上段0181首段为准。**

**CURRENT：0121a11完整硬件vsync实测通过，F3正常使用路径验收通过；NEXT F4。**
sweetlog-0121a11-fb-vsync-hardware.bin SHA256=
9839ade3d47d58ee5ca2396ad73f77729c4d66302a5013b127e77f0dcae06145；有效365908/flush84/
未回卷/4900行。用户“我未发现异常”。369参数irq2/vsync1；853probe hits1，854armed，
855 FB hardware active frame1；2361幂等ON后active2413，非fallback。十个partial0窗共
600.386秒/35961帧，59.874–59.915Hz（加权59.896），p50 16694–16708us/p99 16698–16718us，
dropped0；无IRQ/FB FAILED、风暴/watchdog/显式回退。三次active快照coalesced0，不补造全程计数。
3905paused frames28538/entries28539，4146ON armed同数；3907/4147 DCS28/29 rc0，14/2ms。
OFF内10个电池样本完成时刻552.710–700.083s，至少147.372s其它线程活跃但MDSS不计数。
4150首个ON后帧active28539；4538/4607/4741三整窗180.010s/10782帧，59.893–59.898Hz，
补齐RD_PTR档ON后整窗缺口。末整窗total39320/entries39322；两窗差2因top half可在统计
快照时已入下一IRQ，其余差1软件probe，不误判丢帧或声称计数原子。无client逐帧通知计数。
33亮度请求/10预算DCS rc0，24键事件power3/up3/down6对、touch20；52电池样本/51refresh
healthy1，末样本864.498s；无驱动ERROR/mux隔离/poison/SPMI timeout/panic，BUG-002只常态观察。
USB2reset/2ACM/3WARN/VERIFY0，不关闭BUG-001/004；driverkitd785–788仍sig9/ns1/code7，
BUG-005未修。52gauge全charging0、电流-132..0mA，仅启动ENABLE一次；本完整档供电来源
未另确认，不套用前档电脑USB、不据healthy1证明充电、不擅改充电。BUG-003仍暂缓。
底稿work1/scratch/artifact-0121a11/{analyze-hardware.py,fb-vsync-hardware.txt,
hardware-log-analysis.json,hardware-analysis-summary.json,hardware-selected.txt}。
NEXT：按FRAMEBUFFER-PANEL-PLAN的F4准备自有扫描池/硬件裁剪/双缓冲翻页；首次写新地址前
重查live SMMU/缓存/DMA可达性，CB2.M0不强加完整IOMMU驱动。ABL约18MiB不够双缓冲，
交接只读已知1080×2340现图，不能越界按1125×2436读。无需重测F3/新boot/push。
F3范围为正常使用+手动OFF/ON；真实终止/重匹配未全部上机，默认irq0/vsync0及CPU拷贝30Hz
保留，F7再收口默认/旧路径，不把硬件通知等同翻页fence。整机PM仍大目标四占位。
本轮仅分析/文档，无驱动/boot/CI改动、无设备操作/推送；CircleCI blob仍
da0b6c0947a66f03491b3a9023590b49665292df。当前实现7329197，实测详细记录F3第12节。

**以下为历史交接；当前结果与下一步以上段完整硬件档实测为准。**

**CURRENT：0121a11 RD_PTR计帧档实测通过；下一步同核完整硬件vsync档。**
日志sweetlog-0121a11-fb-vsync-rdptr.bin SHA256=6b1727bbe355831f7355658ef41a9da2191f2854985da98992aee6a87baf5a5f；
有效381403/flush98/未回卷/5121行。用户确认画面/亮度/触摸/按键/手动唤醒正常，无新增问题。
367参数irq2/vsync0；431GIC probe hits1，432armed。十个partial0窗共600.819秒/35988帧，
59.863–59.913Hz，加权59.898；p50 16689–16709us，p99 16698–16715us，dropped全0；
entries始终total+1（GIC probe额外一次）。无IRQ FAILED/风暴/watchdog回退，证明连续硬件
投递/ACK/rearm。FB消费仍软件，不提前称完整硬件vsync成功。
4010 paused frames38147/entries38148，4471 ON armed仍同数；OFF内部电池样本43–60完成时刻
713.924–992.364s，至少278.440s其它线程活动但MDSS计数不增长。4012/4472 DCS28/29 rc0，
13/2ms。ON后rearmed无watchdog错误；无ON后的整分钟统计，下一完整档留70s补，不补造最终计数。
35亮度请求/9预算DCS rc0；44键事件power3/up8/down11对、36touch。无驱动ERROR/mux隔离/
SPMI timeout/kernel panic；61电池样本/60refresh healthy1，包括OFF18份。BUG-002仅常态观察。
USB2reset/2ACM/3WARN（ep0二ep5一），本轮VERIFY0不关闭旧bug。driverkitd787–790仍sig9/
ns1/code7，BUG-005未修。另61份gauge全charging0、电流-132..0mA，启动ENABLE成功不能当真实
充电证据；用户已确认接电脑USB。原因未知，不判MDSS因果、不擅自改充电，BUGS/F3第11节已记。
底稿scratch/artifact-0121a11/{analyze-rdptr.py,fb-vsync-rdptr.txt,rdptr-log-analysis.json,
rdptr-analysis-summary.json}。
NEXT：fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a11-fb-vsync-hardware.img。
进系统亮屏110s，手动OFF20–30s/ON，唤醒后亮屏70s（补ON后完整统计），简测画面/亮度/
三键/触摸；导出sweetlog-0121a11-fb-vsync-hardware.bin。检查FB首次/ON后hardware active、
无回退及MDSS频率/计数。无需新编译/push/打包，不额外插拔/电池/BUG-002专项测试。
本轮仅分析/文档，无驱动/CI/boot修改或设备操作，F3整体尚未验收。

**以下为历史交接；当前结果与下一步以上段RD_PTR实测为准。**

**CURRENT：0121a11 fb-vsync-gic实际投递通过；下一步同核RD_PTR计帧档。**
日志sweetlog-0121a11-fb-vsync-gic.bin SHA256=2ecff7f99fdcdba61676b16be307c97307327daf8c50d77ad22746d9a3b6adcc；
有效337146/flush36/未回卷/4500行。用户明确未察觉异常，只要求复核隐藏问题。
367参数irq1/vsync0；89staged1，851 GIC probe passed hits1/peripheral untouched，证明实际
GIC115 callback投递及50ms后检查通过；尚未开TE/RD_PTR，不证明Hz/ACK/硬件vsync成功。
3546/3567实际off/on rc0，14/2ms；亮度2838→0→2838，9预算DCS rc0。18键事件各3对down/up，
触摸14；37电池样本/36refresh全healthy1，末样本415.454s，事件继续；无驱动ERROR/mux隔离/
SPMI timeout/kernel panic，BUG-002只正常观察无专项验收。
隐藏旧问题：driverkitd在769–772 sig9/ns1/code7，核对实际reason.h88/kern_memorystatus.h290
明确Jetsam单进程内存上限；0121a10 792–795已有同签名，登记BUG-005，具体limit/footprint/
原因及影响未知，不直接放宽内存策略、不阻塞F3。不是内核panic或已证实GIC回归。
USB7WARN（ep0六/ep5一）/7reset/2ACM；最后2次配置在1549/2471，后5reset未见ACM配置，
可疑但无主机反馈不确证枚举失败，补BUG-001。USB VERIFY id10/18两份，之前已reasons2禁充，
之后step0/reasons1保持禁用，再3252/3790启充；不称两次遥测主动从启充态停充/图标闪烁复现。
旧BindFS缺路径/NVRAM NotReady/CarKit等与0121a10同类，用户态兼容不扩进F3。
底稿scratch/artifact-0121a11/{analyze-gic.py,fb-vsync-gic.txt,gic-log-analysis.json,
gic-analysis-summary.json,gic-selected.txt}；FRAMEBUFFER-VSYNC-F3第10节/BUGS已同步。
NEXT：fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a11-fb-vsync-rdptr.img。
进系统亮屏至少110秒（35s延迟接管后留足60s完整统计），手动OFF20–30s/ON，再简测亮度/
三键/触摸，导出sweetlog-0121a11-fb-vsync-rdptr.bin；此档irq2/vsync0前端仍软件。
不需新push/编译/重新打包；完整hardware档待rdptr结果后，baseline仅故障定位备用。
本轮只分析/文档，无驱动/CI/boot变更或设备操作，未push；F3整体未验收。

**以下为历史交接；当前结果与下一步以上段GIC实测为准。**

**CURRENT：0121a11 / 0180云产物与同核四档boot已核对，待真实GIC投递档实测。**
实现7329197/交接0907868；48源码与0180验证树逐字节一致，含7个改动文件/0178 mux修复；
114原CI源码marker及F3v2内核marker、kernel归档manifest通过。无完整build log/CI run ID，
未审全量云编译告警。kernel SHA256=5c7a21c1b077ccfae427547d9cb350b070749c1f0a5a8208485503bbf1d9dd5a，
17,523,360字节，与0121a10不同。四档out-bs/boot-16k-0121a11-fb-vsync-
{baseline,gic,rdptr,hardware}.img各35,667,968字节，IRQ/vsync依次0/0、1/0、2/0、2/1。
boot SHA256按以上顺序：
d32c5cb335beaa278b2ad6f8e149f28a680a440b53678a0f3fc689306f98f947
61037c52b5198a475129ecf73d73a0538bb84242a3d7bd24614f2ad275f097ac
166364ecb7f1d284ac3b6d5ecdd2b046686829bfe631b65feb2a4a1050e852d0
fb9795a630b82b4b388fc32f83de747c8592bdc986770f4b06900f58102c6286
最终boot header/UEFI blob/FAT ramdisk内核/ADT/TOML完整回读通过；参数无重复，仅irq/vsync不同；
原IRQ/ACM/runtime3/F2 power1保留，同一扩展ADT，基础ADT与上一轮一致。环境仍缺file(1)，仅本地
package.sh用Python header检测代信息输出，未改项目打包脚本；随后独立完整回读通过。
底稿scratch/artifact-0121a11/{verify-artifacts.py,verification.json,source-compare.json,package.sh,
packaging.txt,verify-boots.py,boot-verification.json,mdss.adt}。文档FRAMEBUFFER-VSYNC-F3第9节。
NEXT：先仅fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a11-fb-vsync-gic.img。
进入系统保持亮屏至少40秒（35s配置/软件pending注入+50ms后判断一次命中），简测亮度/按键/
触摸，导出sweetlog-0121a11-fb-vsync-gic.bin。此档不是0179只读GIC：实际验证父线投递，
但不使能外围TE、不切硬件vsync。通过后RD_PTR档亮屏110秒取得完整统计再OFF20–30秒/ON，
最后完整硬件档同样验证。baseline仅故障定位备用；持久日志即可，不专项BUG-002、不重开
BUG-003或整机PM。F3整体尚未实机验收。用户负责fastboot，本轮无设备操作/驱动修改/push；
CircleCI blob仍da0b6c0947a66f03491b3a9023590b49665292df。

**以下为历史交接；当前产物与测试步骤以上段0121a11为准。**

**CURRENT：0180 F3真实MDSS IRQ/硬件vsync完成本地验证，待用户push/云编译；尚无新boot/实测。**
基线2e5383b及0121a10实际源码；新补丁xnu/patches/0180-mh2lm-mdss-hardware-vsync.patch。
SweetMDSS拥有验证后的ADT资源9–11、GIC115物理callback、软件IES/workloop、35秒延迟GIC
软件pending投递probe（50ms后判一次命中），mode2通过后才开INTF1 TE RD_PTR bit2。
顶半部只mask/原子状态计数/记时/排队；worker精确W1C bit2/计帧/开父线/发布mailbox，
不把ACK后新pending判故障。fFrameTicks单独保存已处理帧时间戳，订阅不误拿下一顶半部ticks。
默认sweetmdssirq0/sweetfbvsync0保持旧路径；mode1只GIC投递不使能外围；mode2实际RD_PTR。
FB硬件档要求service1/mdss1/power1/contract0/mdssirq2；typed attachVsync/detachVsync单channel，
provider发布只排队consumer IES，不反调FB gate。首真实帧后60Hz软件通知timer降100ms看门狗；
250ms无帧显式ERROR/fallback，consumer非阻塞排队请求provider停IRQ。OFF停通知/提交/本地IRQ，
ON成功后重开；终止关闭admission/停源/撤channel/锁下摘物理入口，maps/events到free才释放。
每60秒统计frames/milli-Hz/p50/p99，运行期16KiB缓冲4096间隔，有界heapsort、dropped可见；
OFF摘要partial1，>200帧/一秒统计窗停用。无自有扫描缓冲/翻页，CPU拷贝30Hz留给F4。
本地：严格git apply零offset/fuzz、逐字节一致、114原CI源码marker；SweetMDSS/FB/sleh基线及
验证树arm64-apple-darwin对象编译通过。FB/sleh告警不变；MDSS新增4条元类宏既有类别告警及
29条有界数组unsafe-buffer-usage已审阅。实际provider及FB/channel ASan/UBSan通过，覆盖默认
DCS/F2/0179、probe/ACK、新pending、时间戳竞态、60s样本、OFF/ON、watchdog/storm、RWP失败、
withdraw/rebind、stop/迟到IRQ、close/publish并发、部分启动失败、资源归零。LSan受ptrace限制关闭。
打包新增--framebuffer-vsync，同核fb-vsync-{baseline,gic,rdptr,hardware}四档，只有IRQ/vsync参数
不同，保留原IRQ/ACM/runtime3/F2。真实ADT及参数检查通过，旧0121a10缺F3v2 marker会拒绝；
未用旧内核生成新测试boot。底稿scratch/framebuffer-0180/{base,edited,verify,verification.json,
strict-apply.txt,warning-counts.json,test-display.txt,test-framebuffer.txt,test-adt.txt,packaging-checks.json}。
NEXT：用户push后下载新kernel/buildtree/buildlog，先核对0180完整源码及新marker再打包。
先测真实GIC投递档（不是0179只读GIC档），进系统亮屏约40秒取日志；通过后RD_PTR档进入系统
亮屏至少110秒（或armed后70秒）取得partial0统计，再手动OFF20–30秒/ON；最后完整硬件vsync档
同样测试。只fastboot boot，持久日志即可；目标60±1Hz/p50/p99/OFF计数停止及输入/USB/电池健康。
不默认重测baseline、不专项BUG-002、不重开BUG-003、不扩大整机PM。F3尚未实机验收。
文档FRAMEBUFFER-VSYNC-F3第8节含接口架构/生命周期/测试步骤，路线及架构文档已同步。
本轮无USB/电池/按键/mux源码修改、无push/设备操作；CircleCI blob仍
da0b6c0947a66f03491b3a9023590b49665292df。实现提交7329197（基线2e5383b），未push。

**以下为历史交接；当前待编译代码与下一步以上段0180为准。**

**CURRENT：0121a10 / 0179完整只读预检实测通过；下一步接真实MDSS IRQ/vsync。**
日志sweetlog-0121a10-fb-irq-preflight.bin SHA256=
504d8167c635b24fbc8bea4385ba1fda9eb0c0692bf2805b58d12325bcba46b3；有效318413/flush22/
未回卷/4240行。用户确认画面/亮度/触摸/按键/手动息屏唤醒都正常、未见新增问题。
89mode2，370boot args正确；197–199/402–404/606–608三组GIC/MDSS/INTF1均完整。
GIC PIDR2=3b→v3，TYPER=7b041d覆盖960INTIDs；115 en/pending/active/cfg均0（level），
priority80/route0，group/groupmod0保留NS RAZ边界，不证secure或投递。MDSSrev50000001，
sources0，MDP en0/status100 en2=0/status2=200；TE1 en0/statusf，TE2 en0/status0。
INTF1 TE1/cfg180088/rdptr925/auto80000001，line924→99→23a。两端未使能，RD_PTR锁存
不等于已投递；三次相同锁存不是ACK失败，不能算Hz。追加映射已通过provider校验。
95/96后端匹配，CoreBrightness2838/DCS rc0；三键12条（power2/up6/down4）、触摸48条。
本日志无实际MDSS off/on，只有初始FB on，不补造28/29或F2五轮覆盖；用户反馈单独记。
29电池样本/28IRQ refresh healthy1；无驱动ERROR/mux隔离/SPMI timeout/panic。新内核含0178，
本次无隔离仅正常使用记录，不专项验收BUG-002。USB10WARN（ep0九/ep5一）、10reset/
2ACM配置；不定主机枚举失败。USBIN id10/17 VERIFY-12发生在此前reason2停充之后，
后step0/reason1，再3238/3521启充；不算两次遥测从启充态主动停充，不称UI闪烁复现。
独立BUGS已补，不扩当前目标。底稿scratch/artifact-0121a10/analyze-preflight.py及
preflight-log-analysis.json/analysis-summary.json/preflight-selected.txt/fb-irq-preflight.txt。
NEXT：无需测GIC/关闭档，实施实际MDSS IRQ（GIC callback+software IES/workloop）及
provider typed vsync订阅/撤回、OFF禁用/恢复、风暴停用/显式watchdog回退。父线使能前
检查非目标enabled source，ACK精确bit2；provider→FB仅排队，避免双gate反调死锁。
当前尚无该后续代码/新boot，软件vsync仍在，F3整体未验收。BUG-003暂缓、整机PM末尾占位。
本轮仅分析/文档，无驱动/CI/boot修改或push；CI blob仍da0b6c0947a66f03491b3a9023590b49665292df。

**以下为历史交接；当前结果与下一步以上段0121a10实测为准。**

**CURRENT：0121a10云源码/内核核对通过，同核三档boot已打包并回读，待只读预检实测。**
实现d73cbc8（最新交接dc7442a）；44源码与0179验证树逐字节一致，含0178/0179，114原CI
源码marker/F3内核marker及归档manifest通过。无完整build log/CI run ID，未审全量告警。
kernel SHA256=b6a0732a419f28276b6b6f8fba7c15f711d66394e66ee200043066a0aeda397e，与a9不同。
out-bs/boot-16k-0121a10-fb-irq-{preflight,gic,baseline}.img各35,667,968字节；mode2/1/0，
SHA256依次641c1b0aefc1be781b71e146c176e060976e425bd339638b69fd5fd74511c9dd、
0e9dd9ef014e7fad818f28e9777f112d00615cf8aba3bd38fc28d10740a12060、
9b79a1ad447470b30b374aef79eb7587cd78ab4e0c2609cbeb05dc15aed72614。
最终boot回读header/UEFI blob/FAT ramdisk/XNU/ADT/TOML通过；参数无重复，仅
sweetmdssirqprobe不同；同一扩展ADT，基础ADT未改，原IRQ/ACM/runtime3/F2 power1保留。
本地缺file命令，用Python header检测替代该信息展示步骤，未改打包脚本；独立完整回读
通过。底稿scratch/artifact-0121a10/{verification.json,source-compare.json,boot-verification.json,
verify-artifacts.py,verify-boots.py,packaging.txt,mdss.adt}；详见FRAMEBUFFER-VSYNC-F3末节。
NEXT：用户fastboot boot完整preflight档，进入系统保持亮屏至少35s取三次快照，随后简测
亮度/触摸/按键/手动息屏唤醒；导出sweetlog-0121a10-fb-irq-preflight.bin，持久日志即可。
有新增启动异常才GIC/关闭档定位，不默认重跑三档。不专项测BUG-002、不重开BUG-003。
当前仅只读预检，未注册MDSS handler/开IRQ/替换软件vsync，无实机成功结论。
本轮无驱动/CI修改、未push/操作设备；CI blob仍da0b6c0947a66f03491b3a9023590b49665292df。

**以下为历史交接；当前产物和下一步以上段0121a10为准。**

**CURRENT：F3第一步0179只读预检已实现并完成本地验证，待用户push/云编译。**
实现提交d73cbc8，基线3458ccf；0121a9实际0177云源码+0178严格重放后，仅改SweetMDSS.cpp。核对LG
sm8150-sde.dtsi SPI83→INTID115；SDE5.x command mode选INTF1 TE RD_PTR，TE IRQ块
0ae6e800 EN0/STATUS4/CLEAR8 bit2，与配置窗口0ae6b800分开；INTF2 IRQ0ae6e900。
新增sweetmdssirqprobe默认0，mode1只读GIC，2读GIC+MDSS/MDP/TE/INTF1；不注册handler、
不ACK/使能/注入pending、不替换FB软件timer。ADT reg追加9(top0ae00000/2000)、
10(tear0ae6e800/200)、11(GICD17a00000/10000)，provider校验PA/长度；原九窗口不变。
独立MDSS timer约30/31/32s各一次后停止，同gate串行；MDSS先过原AHB时钟条件，GIC
独立采样。缺失/错位/过短资源或timer失败ERROR并仅停预检；stop关闭admission/取消
移除timer，maps到free再释放。Ready表示资源，Samples含时钟跳过的尝试，不是IRQ计数。
只读STATUS锁存位、group可能NS RAZ，不能定stuck/频率/secure投递；真实IRQ接入仍待做。
patch strict apply/零fuzz与offset/字节匹配、114既有CI marker通过；SweetMDSS base/new
arm64对象编译rc0。clang19 unsafe-buffer-usage新增15条（新常量下标/有界循环已审），
其他告警类别/数量不变，不宣称无新增告警。实际provider/client/backlight ASan/UBSan、
FB service、ADT三套通过；覆盖只读/默认零新增访问/GIC解码/三次停止/资源与timer故障/
clock门禁/inactive/stop并发/映射对象归零。LSan ptrace不支持，关闭。打包bash-n/三档参数/
真实ADT CLI及旧a9缺marker拒绝通过。底稿scratch/framebuffer-0179。
新增打包--framebuffer-irq-preflight，出fb-irq-{preflight,gic,baseline} mode2/1/0，均F2
power1/service1/mdss1，ACM/所有原IRQ/runtime3保留，proof/contract0；共用扩展ADT。
旧--framebuffer-power不变。NEXT：用户push后下载kernel/buildtree/log，核对0178/0179
源码/marker→打包回读→先只测完整preflight，亮屏等35s后简测输入/亮度/手动息屏唤醒，
持久日志sweetlog-编号-fb-irq-preflight.bin即可；有新增启动问题才gic/关闭档定位，不默认
全矩阵。暂无新boot，无硬件成功证据。BUG-002不专项复测；BUG-003暂缓；整机PM仍末尾
占位，当前不扩入system sleep。详见docs/FRAMEBUFFER-VSYNC-F3.md。
未push/操作设备，CI未改，blob仍da0b6c0947a66f03491b3a9023590b49665292df。

**以下为历史交接；当前实现与下一步以上段0179为准。**

**CURRENT：用户确定完整电源管理排到大计划最后；当前继续显示，下一步F3硬件vsync。**
基线7d65026。已在docs/IRQ-FRAMEBUFFER-PLAN.md总体顺序及末尾追加大目标四“完整电源管理
与低功耗适配”，位于大目标三日志优化之后，仅登记目标，无子目标/实施顺序/验收方案；
未来再定。低功耗审计中的建议标为候选思路，不是已定计划，不提前扩入当前显示任务。
用户明确继续面板路线：F2基础及长时电源键正常使用已通过，插拔次数不足保留覆盖边界；
下一步F3先核对真实MDSS/TE/RD_PTR来源、候选INTID115映射和只读预检，再走既定GIC callback
+IOWorkLoop的硬件vsync。显示OFF中断/通知管理仍属原显示目标，不实现整机sleep。
本轮只改路线/说明文档，没有F3代码或新测试镜像；不称硬件vsync已实现，不自行要求重测
F2全矩阵/BUG-002，BUG-003继续暂缓。NEXT：按F3计划做源码/参考实现核对与预检设计。
无内核/CI/boot修改，未push；CI blob仍da0b6c0947a66f03491b3a9023590b49665292df。

**以下为历史交接；当前目标顺序与下一步以上段为准。**

**CURRENT：按用户要求核对0121a9息屏掉电/各驱动低功耗；仅分析与文档，未实施PM。**
基线8b7cd5d。长测原日志SHA仍0ae824787b04471ec67b721c78e3ea5434ed924319ad3aff8bd333a9b6228335。
整份93→78%，息屏前最后91%；息屏id14–100共87样本，时间跨度4805.156s约80min，
电流-393～-289mA，中位-294，加权-311；离散梯形积分约415mAh/1.70Wh、平均1.28W，
并非精确累计库仑/功率计。电压4159→4048mV，温度35～36.25℃，数据有效，有真实放电依据。
不将未验证3510mAh profile容量反推百分比。待机耗电偏高，但符合低功耗未适配的开发状态，
不是已证实0177新回归或电池故障；显示唤醒通过不等于功耗验收。
新增docs/POWER-MANAGEMENT-AUDIT-0121A9.md表格+源码行号：CPU通用WFE/WFI框架存在但
quiesceCPU/haltCPU仍panic，无本板suspend恢复；不要称8核全程满载/完全没WFI。
显示仅28/29、无10/11/rails/clock关断；FB OFF仍16667us软件timer，先通知clients再判OFF；
sweetfb_hz30只降拷贝频率。USB IRQ线程等待但RUN_STOP/override保持、未接USBIN runtime
suspend；触摸IRQ线程等待但无TCM深睡/gesture wake/resume；UFS无自有runtime PM且日志
每10s按新增数据写盘。按键/mux保持IRQ、尚无整机深睡wake配置恢复。电池priorityWillSleep
仅ack保留充电，本轮无will-sleep日志，60s温控采样继续；不把该采样未经测量定主耗电源。
息屏段2290exec，其中fairplayd.H2=481/bluetoothd=480/xpcproxy=1145，未知退出原因和CPU占比，
不能仅凭exec认定崩溃原因或全部耗电。底稿scratch/artifact-0121a9-long/power-usage.py/json，
另从同云归档提取CPU源码到该目录pm-source/xnu，SweetCPU SHA见审计文档。
NEXT：回到显示路线，后续明确OFF通知/定时器及MDSS电源，再分层推进设备PM/系统睡眠；
本轮不改大计划顺序，不自动启动新云编译/长测，不强行开启未实现的system sleep。
BUG-002继续正常使用观察、不专项测试；F2基础/长时正常路径保持通过，F3尚未实现。
已同步当前架构/F2/IRQ路线摘要。无驱动/CI/boot改动，未push；CI blob仍
da0b6c0947a66f03491b3a9023590b49665292df。

**以下为历史交接；当前耗电核对以上段为准，既有修复和显示结果仍保留。**

**CURRENT：0121a9长测通过；BUG-002已由41a0efa / 0178代码修复，用户要求后续正常使用观察。**
本轮基线41a0efa。BUG-002仅改drainPending收尾：everAcked/pendingRoutes为局部，ACK过和
未ACK过的末尾pending均rearm待下一次drain；不增faultCount/不隔离。四遍/ack语义/ACK
失败disable/隔离调用点保留，无成员/全局变量。源码strict apply/收尾外字节不变、consumer
suite含timeout及80生命周期竞态通过；ASan/UBSan晚到CC、空遍晚到CC、第五事件在下次
drain清除且fault0/无隔离，按键健康。底稿scratch/bug002-fix，尚无新内核实机证据。
用户明确：不再专项测试BUG-002，不安排重复插拔/A-B；后续正常使用，复发反馈后再查。
已补BUGS与BUG-002-MUX-PENDING修复日志，历史专项关闭清单不再作为显示前置。
长测sweetlog-0121a9-fb-power-dcs-long.bin SHA256=
0ae824787b04471ec67b721c78e3ea5434ed924319ad3aff8bd333a9b6228335；有效547519/flush550/
未回卷/8573行。3472 off rc0/state2/12ms→7958 on rc0/state0/4ms，用户确认画面/亮度/
触摸都正常。87个息屏段采样首末内核时间跨度4805秒约80min（非精确off-on测量）。
亮度3460的0→7972的2838恢复初始值；10HID（power8/up2），唤醒后18touch；100电池样本/
17IRQ refresh，无分级ERROR/WARN/显示fallback/UNKNOWN/mux隔离/panic/USB VERIFY。
无ACM/USB reset，不补造USB覆盖；长测是原0177镜像不含0178，无隔离不能证明BUG-002
修复已上机；不算充电能力或系统suspend。底稿scratch/artifact-0121a9-long/analyze.py等。
F2基础及长时电源键正常使用路径通过，完整5轮×三来源矩阵未补齐，旧隔离后充电唤醒仍
无效，不宣称全矩阵通过。NEXT：回到显示计划后续推进；F3尚未开始实现，不重开BUG-003，
不主动插入BUG-002专项测试。本轮仅文档/分析，未push/操作设备/改镜像/内核/CI；CI blob
仍da0b6c0947a66f03491b3a9023590b49665292df。

**以下为历史交接；当前修复、长测结果及后续观察政策以上段为准。**

**CURRENT：F2长测期间，按用户要求独立定位BUG-002；仅诊断/复现，尚未改驱动。**
基线e706e4b / 0121a9。已确认mux末尾stillPending即永久隔离的错误判定；该位无需曾被ACK。
a9未回卷日志4689 work87总hits123，4694/4695 work88总hits125，只有USBIN/CHGR两次成功ACK，
无CC hit；电池日志不受按键budget限制。4690末尾CC APID271 bit2/status4即隔离group1，
不是CC清四次仍失败。LG ACK同为arb CLEAR→PMIC LATCHED_CLR，CC极性也匹配DTS。
新增docs/BUG-002-MUX-PENDING.md及tools/tests/repro-mh2lm-mux-pending.py：实际mux host
ASan/UBSan三场景完成；5CC分批正常；空遍扫描完新到1CC，0次CC ACK即隔离；四遍成功ACK
后第5CC到达也被隔离。缺陷场景各1次GIC dispatch，无timeout/poison模拟，按键继续工作。
这是已知缺陷特征复现（REPRODUCED不等于健康验收），未来修复后应改健康/恢复断言。
底稿scratch/bug002/committed-reproducer-result.txt；更新BUGS。硬件晚到究竟新边沿/传播延迟
仍未知，不自动把历史所有实例定为同一触发，不宣称已排除全部硬件问题。软件永久失效
原因已定位。建议后续有界续批/公平调度/逐组过载预算，与真正ACK错误及未知位分开处理，
不可仅增大4遍、不可加轮询回退。用户本轮只要求排查，尚未实现或要求新云测。
NEXT：等待原0121a9 F2手动息屏30min长测日志，继续显示验收；不重开BUG-003，不进F3。
本轮无内核/CI/boot改动、未push；CI blob仍da0b6c0947a66f03491b3a9023590b49665292df。

**以下为历史交接；当前独立诊断以上段为准，F2长测任务仍保留。**

**CURRENT：0121a9日志已分析；用户确认息屏/唤醒正常无新增显示问题，基础DCS28/29通过。**
日志sweetlog-0121a9-fb-power-dcs.bin SHA256=
f02beab6ad0b0d9fbf2882ba0d73ae3a8205fbd49c22c1910ba120a7df4810ca；有效372499字节/
flush51/未回卷/4993行。76启用/357参数power1；94FB后端match，95背光match。
MDSS实际off/on三组4738→4774(9/11ms)、4896→4908(4/3ms)、4923→4937(9/0ms)，
均rc0及OFF2/ON0，无电源ERROR/fallback/UNKNOWN。2270初始幂等ON仅前端日志，不算实际29。
35亮度请求768～2374、8条预算DCS rc0、24键（power18/down6）、21触摸、1ACM配置；
无volume-up记录，不补造三键/5轮覆盖。耗时是毫秒取整、gate内测量，不是端到端延迟。
用户报告旧充电bug：主要BUG-002，4690 Type-C CC APID271 bit2/status4持续pending→
4692隔离group1；在途id43后4722–4727撤回电池连接/disable0/输入失效，停在93%，之后无
refresh。group0未隔离，随后电源键/面板仍工作；无SPMI timeout/panic，不称全内核死锁。
该故障在第一笔实际28之前，不能归因该事务，也不能完全排除负载间接影响。隔离后充电
事件来源失效，插拔无反应不能定面板失败，相关测试段不纳入有效充电唤醒验收。
另有4USB VERIFY(-12)，3次遥测停充后恢复，补BUG-004；6启充/3次reason2停充，源类型未知。
已更新BUGS及docs/FRAMEBUFFER-POWER-F2.md，含四类测试目的和证据边界。旧bug不在本轮修。
底稿scratch/artifact-0121a9/{fb-power-dcs.txt,power-log-analysis.json,analyze-power.py,
power-and-battery.timeline.txt}。基础路径成功不等于F2全部验收，30分钟/有效插拔覆盖仍缺。
NEXT：同一个DCS镜像保持线缆状态不变，手动息屏30min后按电源键唤醒，检查显示/亮度/
触摸，导出sweetlog-0121a9-fb-power-dcs-long.bin；不重做全套短测/不需新编译。不能将该
长测当作充电30min成功；旧BUG独立，不为其自动增加显示前置，不重开BUG-003，不进F3。
本轮只分析/文档，无驱动/CI修改、未push；CI blob仍da0b6c0947a66f03491b3a9023590b49665292df。

**以下为历史交接；当前实测结论及下一步以上段为准。**

**CURRENT：0121a9 / 0177云产物核对及F2两档boot打包回读完成，待用户短测。**
预期实现decebe3；44个已跟踪源码逐字节匹配（0177四文件），114个既有CI源码marker/
F2内核marker及状态属性名通过，归档manifest通过。无完整build log/CI run ID，未查全量告警。
kernel SHA256=50d9fc40ebb98ca23d424ba743513a8cb00f873b8a330032f95583769acd7e55，与a7不同。
out-bs/boot-16k-0121a9-fb-power-{baseline,dcs}.img均35,667,968字节，SHA256分别
5277895a1708cec2a2943bdfcd442f7db2be6144db3bd0b4bcc73f8212720826、
afc416c3e3b5c8a60b0290ff843cde9fff6c09f3d310266cd11221997e70c44c。
最终boot回读header/FAT/kernel blob/ramdisk/XNU/ADT/TOML通过，参数无重复，仅power0/1
不同，均mdss1/service1；原ADT未改。IRQ/ACM/runtime3/preserveoff1保留，trace/proof/
contract0、probe1/loglevel2。底稿scratch/artifact-0121a9/{verification.json,source-compare.json,
boot-verification.json,verify-artifacts.py,verify-boots.py,packaging.txt,mdss.adt}。
NEXT：用户fastboot boot DCS档，等35秒；亮度低高低/三键/连续触摸，手动off-on（间隔3秒）
5轮、息屏插/拔电各5轮、关机界面/取消；记录新不亮/局部黑屏/冻结/明显迟缓，导出
sweetlog-0121a9-fb-power-dcs.bin。有新增问题才用同核baseline，不默认重复两档。
先核对短测实际DCS28/29成功、无fallback/UNKNOWN，再同镜像30分钟手动息屏，无需重编译。
没有自动息屏；BUG-003缺时间/半透明继续基线对比不重开专项，充电BUG-004独立。
F2尚未实机验收，不进入F3；本轮只核对/打包/文档，无驱动/CI改动、未push或操作设备。
CI blob仍da0b6c0947a66f03491b3a9023590b49665292df。详见docs/FRAMEBUFFER-POWER-F2.md末节。

**以下为历史交接；当前产物及下一步以上段0121a9为准。**

**CURRENT：用户授权F2；0177实现提交decebe3，本地验证完成，待push云编译。**
基线e17918f / 0121a7正常使用路径验收；新增sweetfbpower默认0，要求service1/mdss1/
contract0。AppleCLCD selector12经dispatch/state gate，copyBackend调用typed setPanelPower，
SweetMDSS gate检查admission/inactive/clock，发LP短包80050028/29；无consumer穿透MMIO。
ON/OFF_PENDING/OFF/ON_PENDING/UNKNOWN；off失败补偿一次29，失败不伪记ON。故障后
停止新的OFF，保留ON恢复；与亮度失败预算独立。consumer保留旧逻辑成功返回，但硬件rc
单独ERROR/属性记录，不拿selector成功证明DCS成功。F2强制保留扫描像素/息屏pending。
F2命令及OFF/UNKNOWN亮度事务用DSI idle条件，不依赖停住的TE；默认旧亮度MMIO轨迹不变。
同步双gate调用每事务最多约400ms，off补偿最坏约800ms，实机耗时/响应待测，不声称异步。
仅改kernel四文件SweetDisplayBackend.h/SweetMDSS.cpp/SweetFramebuffer.cpp/SweetFBService.inc；
patch0177。LG依据及全部边界见docs/FRAMEBUFFER-POWER-F2.md；无10/11/rails/reset/vsync/扫描地址改动。
strict apply+四文件字节核对、114既有CI源码marker；base/verify两单元arm64对象编译rc0，
告警类别/数量完全一致（含FB两条旧format）；17项host通过，ASan/UBSan，LSan关闭。
新增测试执行实际provider/FB，覆盖短包/LP/恢复/UNKNOWN/TE冻结/保留像素/重复ON/stop。
底稿scratch/framebuffer-0177/{base,edited,verify,implement.py,compile.py,verification.json,
warning-counts.json,tests.json,test-*.txt,old-kernel-rejection.txt}。bash-n通过，新打包入口
--framebuffer-power出fb-power-{baseline,dcs}，均mdss1/service1，仅power0/1差异；
保留IRQ/ACM/runtime3，trace/proof/contract0、probe1/loglevel2；旧a7被缺F2 marker正确拒绝。
NEXT：用户push最新分支并下载新kernel/buildtree/log→核对四源码/manifest/marker→打包回读。
先仅测dcs短测：启动等35秒，亮度/触摸/三键、手动off/on及息屏插/拔电各5轮、关机取消；
有新增异常再baseline。短测通过再同镜像30分钟息屏，不需第二次云编译；持久日志fb-power-dcs。
没有新云产物/boot，F2未验收，不重开缺时间/充电A/B；BUG-001/002/003/004未关闭。
本轮未push/操作设备，未改USB/按键/电池/SPMI或CI，CI blob仍da0b6c0947a66f03491b3a9023590b49665292df。

**以下为历史交接；当前实现及下一步以上段F2为准。**

**CURRENT：0121a7 gated日志已分析，用户“没注意到新问题”；F1b正常运行路径验收通过。**
结合0121a6 F1a，F1分层重构正常使用范围收口；真实provider终止/重匹配/stop-free仍未
上机触发，仅host证据，默认sweetfbservice0/sweetmdss0不变，不声称生命周期全部实测。
日志sweetlog-0121a7-fb-state-gated.bin SHA256=
6cb1c9f504fb4c67543e565dbcaec0d079e7add1b168941873b388ff83b3a449；有效713446字节/
flush14/未回卷/8607行。75新dispatch/gate、93后端match，365参数service1/mdss1/contract0。
9资源/3探针；观察到sel3/4/5/6/8/12/28成功（有界去重，非全调用记录），SwapEnd1132字节。
30亮度请求973～2632、8条预算内DCS rc0；4组off/on：3754→3781、4817→4955、
6527→6555、6733→6762；32三键HID配对、73触摸、2ACM配置、6USB WARN/6reset。
无分级ERROR/显示失败/mux隔离/SPMI timeout/kernel panic；用户态既有错误不记“全日志无错”。
BUG-004复现：146电池样本、30USB VERIFY(-12)、30遥测停充/32启充、1次reason2停充，
352UV route；4000行id10无效→4009停充→4051启充。已补BUGS，不视作电池完全无异常，
不把F1b定为必要原因，也不排除负载影响。独立BUG-001/002/003/004未关闭。
底稿scratch/artifact-0121a7/{fb-state-gated.txt,gated-log-analysis.json,analyze-gated.py,
gated-selected.txt}；详见docs/FRAMEBUFFER-SERVICE-F1B.md末节。无需重复baseline。
NEXT：按FRAMEBUFFER-PANEL-PLAN进入F2真显示电源；先查LG面板28/29时序/状态与typed
backend电源接口，consumer不穿透寄存器；F3/vsync、F4扫描缓冲不混入，不重开缺时间专项。
本轮仅分析/文档，未改驱动/CI、未push，无需新云编译。CI blob仍da0b6c0947a66f03491b3a9023590b49665292df。

**以下为历史交接；当前验收结论及下一步以上段为准。**

**CURRENT：0121a7 / 0176云产物核对及F1b两档boot打包回读完成，待用户实测。**
预期实现b1c7969；44个已跟踪源码逐字节匹配（0176两文件），114个既有CI源码marker及
新增F1b内核marker通过，归档manifest通过。未收到全量build log/CI run ID，未核对全量告警。
kernel SHA256=92cb3d7df535f692ba5d5fc220068da8f50d4e3a1ca9ce974fc084dadac55c8b，与a6不同。
out-bs/boot-16k-0121a7-fb-state-{baseline,gated}.img均35,667,968字节；SHA256分别
ac15b4a0dedfb247167c6df1bf4bb19c391c7ece4eacf6c9a882be53170ee080、
3cf98432131b221478c8031e28b80ae8d2678df0f90e3887ae8c2bea96e1fbc2。
最终boot回读header/FAT/kernel blob/ramdisk/XNU/ADT/TOML通过，参数无重复，仅service0/1
不同，均mdss1；原ADT未改。IRQ/ACM/runtime3/preserveoff1保留，trace/proof/contract0，
probe1/loglevel2。底稿scratch/artifact-0121a7/{verification.json,source-compare.json,
boot-verification.json,verify-artifacts.py,verify-boots.py,packaging.txt,mdss.adt}。
NEXT：优先fastboot boot gated，启动等35秒；桌面连续触摸/滑动、三键、关机界面/取消、
亮度低高低、手动息屏/唤醒3轮、USB ACM；记录新增卡死/迟缓/亮度或唤醒异常，导出
sweetlog-0121a7-fb-state-gated.bin。有新增问题再测同核baseline，不默认重测两档。
BUG-001/002/003/004继续独立跟踪，不重开充电A/B或缺时间专项；F1b/F1未上机验收。
本轮只核对/打包/文档，未改内核或CI、未push/操作设备，无需重新编译。
CI blob仍da0b6c0947a66f03491b3a9023590b49665292df。详见docs/FRAMEBUFFER-SERVICE-F1B.md。

**以下为历史交接，当前产物及下一步以上段0121a7为准。**

**CURRENT：F1b / 0176实现提交b1c7969，本地验证完成，待用户push云编译；尚无新云产物/boot。**
接续F1a正常运行路径验收；新增sweetfbservice默认0，开启要求sweetmdss1/contract0。
AppleCLCD独立SweetDisplayClient registry consumer，匹配回调只更新属性，无MMIO/硬件调用；
用户接口经真正IOExternalMethodDispatch验证数量/大小再进入state gate，共享legacy handler
以保持像素/电源/软件vsync行为。未知selector继续成功/清零inline输出，每client32种日志预算。
前端校验scalar上限/空指针/已知inline ABI；7/18未观察的输入保持可变，72接受3/4项且type<8。
client数组、swap ID/pending/power、端口和close都走AppleCLCD workloop/IOCommandGate；
gate同步执行不等于调用一律切到后台线程。新路径不重复拿legacy fLock。
stop先gate内关入口/取消timer/撤回client端口，再gate外stop通知helper/移除timer；gate/
资源留到free，user client显式retain FB到free，晚到调用返回Offline。真实终止仍未上机测。
修改文件仅SweetFramebuffer.cpp与新增SweetFBService.inc，补丁0176；USB/电池/按键/
SweetMDSS引擎及circleci.yml未改，CI blob仍da0b6c0947a66f03491b3a9023590b49665292df。
strict apply+两文件逐字节匹配、114既有源码marker通过；base/edited/verify FB arm64编译rc0，
无新增告警类别，format仍2条旧告警，unsafe-buffer87→92（有界数组/表/索引）。17项host
最终通过，初次3项LSan/ptrace失败用detect_leaks0重跑，ASan/UBSan保留。新fixture运行真实
FB/UserClient，覆盖800并发ID分配、timer/端口/close/stop并行、拒绝无副作用及引用清理。
本地底稿scratch/framebuffer-0176/{base,edited,verify,verification.json,warning-counts.json,
tests.json,compile.py,implement.py,make-patch.py,test-*.txt,old-kernel-rejection.txt}。
tools/mk-mh2lm-event-matrix.sh新增--framebuffer-state，生成fb-state-{baseline,gated}，
两档mdss1，只差sweetfbservice0/1；保留IRQ/ACM/runtime3及preserveoff1，trace/proof/contract0。
旧0121a6被新marker正确拒绝，bash -n通过。详见docs/FRAMEBUFFER-SERVICE-F1B.md。
NEXT：用户push当前分支→下载新kernel/buildtree/log；逐文件核对0176两文件、kernel marker/
manifest，再打两档并回读boot。优先只测gated：桌面/连续触摸/三键/关机取消/亮度/手动
息屏唤醒/USB ACM，出现新增异常再用baseline定位。不重测充电A/B、不重开BUG-003，
独立BUG状态不变。F1b/F1整体未上机验收；F2/F3/F4不提前混入。本轮未push/未操作设备。

**以下为历史交接，当前实现及下一步以上段F1b为准。**

**CURRENT：0121a6日志已分析，用户确认亮度/息屏唤醒正常；F1a正常运行路径功能验收通过。**
日志sweetlog-0121a6-fb-service-mdss.bin，SHA256=
2b67f0b4facc4067c5ad1756dd74414571612aa662a1f70f543f9dc4a9f41095；有效321588字节/
flush22/未回卷/4641行。363行参数sweetmdss1；78–86九资源，87服务启动，92背光match，
226旧探针禁用，174/383/447三次探针正常，VIG0仍0x9d200000/stride4320。
29条亮度请求406～2770，8条DCS rc0（成功日志预算8，非总写次数），无显示WARN/ERROR。
off3576→on3609、off3724→on3758、off3960→on3991；用户确认实际亮度跟随/息屏亮屏正常。
30条三键HID、61条触摸，2次ACM配置、2reset、3条USB endpoint WARN；不据此定BUG-001。
13份电池快照、1次启充，无USB VERIFY(-12)/遥测停充/隔离/SPMI timeout/panic，未复现
BUG-004不等于已修复。详见docs/FRAMEBUFFER-SERVICE-F1A.md末节；底稿scratch/artifact-0121a6/
{fb-service-mdss.txt,mdss-log-analysis.json,analyze-mdss.py}。
验收仅F1a资源/背光迁移正常路径；真实provider terminate/重匹配/stop-free未上机测，
精确亮度0/4095端点无日志证明，保留边界，不无限重复同轮测试。F1整体/F2/F3/F4未完成。
NEXT：进入F1b，AppleCLCD以匹配通知持有typed backend；用户客户端分发表和本版ABI参数
校验、状态统一workloop/IOCommandGate。保持硬件行为，新增开关默认关闭；不混入真关屏/
vsync/模式修复，不重开BUG-003，不重复0121a5充电A/B；独立BUG状态不变。
本轮仅分析/文档，无新驱动补丁、未改CI、未push；用户不需要为本次记录重新编译。

**以下为历史交接，当前验收结论及下一步以上段为准。**

**CURRENT：0121a6产物核对、boot打包与回读完成；同0121a5源码，待补充F1a亮度/唤醒验收。**
用户提供kernel/buildtree，无完整build-log或CI run ID，未查全量编译告警。43源码逐字节
匹配（含0175六文件），114个CI marker及内核marker/manifest通过。两个buildtree的全部
xnu/普通文件5261个内容完全一致；没有新BUG-004修复/F1b实现，二进制SHA不同不等于新行为。
0121a6 kernel SHA256=91ef77c4b7ce7b1c36f754ea4b26f6b52fc28a5940709f6c877229eed2dbc918。
out-bs/boot-16k-0121a6-fb-service-{baseline,mdss}.img均35667968字节，SHA分别为
db5658bad52c40dae92c5d52a712f86c9da036f5ac29570f30cc9d4e69f52cc7、
f14370a039e7a371a4bfef33b69650df47f4558cfa2578507255ea7b5a080d66。
最终boot回读header/FAT/kernel blob/ramdisk/XNU/ADT/TOML通过，两档只差sweetmdss，参数
无重复；IRQ/USB ACM/电池runtime3均保留。底稿scratch/artifact-0121a6/
{verification.json,source-compare.json,full-xnu-source-comparison.json,boot-verification.json,
verify-artifacts.py,verify-boots.py,packaging.txt,mdss.adt}。详见docs/FRAMEBUFFER-SERVICE-F1A.md末节。
NEXT：只用mdss档，启动等35秒，调亮度低→中→高→低，手动电源键息屏/等3秒/唤醒3次，
检查实际亮度及触摸/按键，导出sweetlog-0121a6-fb-service-mdss.bin并说明肉眼结果。
不重测baseline/充电A/B，不重开BUG-003；BUG-004独立，尚无修复。F1a完整验收仍未完成。
本轮仅核对/打包/文档，未改内核或CI、未push、未操作设备，无需再编译即可测试。

**以下为历史交接，当前镜像及下一步以上段0121a6为准。**

**CI补充：用户贴出的新失败发生在KDK安装前置阶段，不是XNU编译报错。**
KDK13.5/22G74下载100%后hdiutil attach报image not recognized；尚无完整失败日志/CI run ID
或下载文件，错误页面/文件损坏等待区分。旧logs/xnu-build-log-0121a1.txt同版安装成功。
circleci.yml当前blob=da0b6c0947a66f03491b3a9023590b49665292df，与显示改造前相同；
本轮仅补充文档，未改CI/驱动、未push。本地联网复查下载源未成功，不声称已检查远端URL。
NEXT：用户可先重跑现有job重新下载，无需push；若仍失败，保留实际下载文件/URL/响应及
类型/大小/hdiutil检查结果再定位。主线仍是下段F1a收口/F1b，BUG-004独立跟踪。

**CURRENT：0121a5 baseline也复现充电启停，A/B结束；BUG-004独立跟踪。**
用户确认成功复现，日志sweetlog-0121a5-fb-service-baseline.bin，SHA256=
05b5ae79eaa5e41b32e18e3a8b9b23bc65158d89dbe84d7666a0589191296e09；有效1079001字节/
flush15/未回卷/11885行。第332行sweetmdss=0，旧探针三遍、旧DCS rc0，无新backend启动。
301快照/179.87秒，61次USB VERIFY(-12)、59次遥测失效停充、64次启充、4次无输入停充；
698次UV、772次CHGR route；639条BASIC命令error0，无mux隔离/SPMI timeout/panic。
样本22第3585–3594行USB0x15/-12→ENABLE0；样本23第3614–3631行USB0x10/0→九步启充，
两样本开始差约103ms；故障链与mdss一致。新后端不是必要触发条件，不证明旧0121a4已复现，
不排除共用配置/负载；UV最初原因仍待查，不直接归因电脑供电或USB endpoint WARN。
详见docs/FRAMEBUFFER-SERVICE-0121A5.md、BUGS.md#bug-004；底稿scratch/artifact-0121a5/
{fb-service-baseline.txt,fb-service-baseline.timeline.txt,baseline-log-analysis.json,
baseline-summary.txt,analyze-baseline.py}。本轮仅分析/文档，无驱动或CI改动、无push。
NEXT：不重测该A/B、不为其重编译；BUG-004独立排期，核对ICL/AICL和USB动态状态/协议
一致性校验边界，修复必须走charger provider并保留保护。显示继续原F1a收口/F1b路线，
完整亮度/生命周期仍未验收，不凭此次对照验收F1，不重开BUG-003。

**以下为历史交接；当前结论和下一步以上段为准。**

**CURRENT：0121a5 mdss日志已分析，电脑USB充电闪烁确认是runtime3反复启停；待同核baseline。**
用户直接测mdss，确认电脑USB口，尚无baseline。见docs/FRAMEBUFFER-SERVICE-0121A5.md。
日志sweetlog-0121a5-fb-service-mdss.bin，SHA256=770a8164c2d66ad75053919c4191da80344aff7df8735c23d4fd0bdf18e80b16；
未回卷，有效1259276字节/flush32/14095行，349样本；771次USBIN UV、843次CHGR route命中。
62份USB VERIFY(-12)，61次遥测失效停充、整份日志67次启充、命令日志670条error0。
id28第4238–4247行USB0x15/-12→ENABLE0；id29第4267–4284行USB0x10/0→9步重新启充。
原因链：完整USB字节双读变化→SB_USB无效→SCB_R_TELEMETRY停充，下一次有效立即启充；
遥测原因不走scb_fail退避。原始CHGR也1/0变化，不是仅显示闪烁；输入UV最初原因未定。
电池/charger/SPMI/mux七核心文件对0121a4逐字节不变，不足以排除显示负载/时序影响。
已登记BUG-004，与BUG-002隔离/BUG-003缺时间分开；无ERROR、mux隔离、SPMI timeout或panic。
MDSS9资源/registry绑定/旧探针禁用/3次探针成功，8次DCS日志rc0（预算8，非总写次数）；
16个键、65个触摸、4次off/5次on。USB10条WARN/10reset/2次ACM，不能单独认定枚举失败。
LG WEAK_ADAPTER_WA只在PMI632分支，不能未经核对搬给PM8150B。未来修复须走charger API。
NEXT：无需push或新编译，用现有out-bs/boot-16k-0121a5-fb-service-baseline.img，保持同电脑口/
线缆、亮度固定，启动等35秒→拔5秒→插保持60秒，观察闪烁并导出
sweetlog-0121a5-fb-service-baseline.bin；不重做全套显示/输入专项，不重开缺时间取证。
若baseline同现则独立排期输入稳定性bug；仅mdss有则收窄迁移回归。F1a/F1暂不验收。
底稿scratch/artifact-0121a5/{mdss-log-analysis.json,fb-service-mdss.txt,fb-service-mdss.timeline.txt,
analyze-mdss.py,battery-source-comparison.json}；本轮只分析/文档，未改驱动/CI、未push。

**以下为历史交接；下一步以上段短对照为准。**

**CURRENT：0121a5 / 0175 云产物核对及F1a两档boot打包回读完成，待用户实测。**
实现提交a22e1b3（交接610b69b）；43个源码文件逐字节匹配，包含0175六文件；114个原CI源码
marker及新SweetMDSS/consumer/旧路径关闭内核marker通过，manifest核验通过。
kernel SHA256=9cc2e35f4eb0f7b8c742557a9c2f4c69e5e4c0d7adb4e35787420e955cfbc9c3，与0121a4不同。
未收到本轮构建日志/CI run ID，未检查全量编译告警，不据产物反推零告警。
已生成out-bs/boot-16k-0121a5-fb-service-{baseline,mdss}.img，均35,667,968字节。
baseline SHA256=0b62ea58d19d86172ac3846e277ce31825bc0c59def06ab1b1af921ef1e6baf5；
mdss SHA256=216c11874f17febeb1763f447f1c8bcd4dc1d384b9e14924821022985f4856c0。
最终boot回读header/FAT，kernel blob/ramdisk/XNU/ADT/TOML逐字节匹配，参数无重复，
两档仅sweetmdss=0/1不同；共用派生ADT，原adt-16k-3g.adt字节未改，9个硬件资源由派生工具
追加。preserveoff=1、trace/proof/contract=0、probe=1/loglevel2，既有IRQ及充电runtime3保留。
底稿scratch/artifact-0121a5/{verification.json,source-compare.json,boot-verification.json,
verify-artifacts.py,verify-boots.py,packaging.txt,mdss.adt}。
NEXT：先baseline后mdss，各启动等待至少35秒覆盖探针；亮度低→高→低、三键/触摸/USB ACM、
手动息屏/按键唤醒、息屏插拔各2–3次，导出sweetlog-0121a5-fb-service-{baseline,mdss}.bin。
目的验证F1a硬件资源/背光迁移不退化；BUG-003缺时间和半透明颜色问题保留，不再专项取证。
BUG-001/002独立；发生mux隔离后的电池/按键段须标无效。无自动息屏测试；F0/F1未验收。
本轮仅核对/打包/文档，未改内核或CI、未push，未执行设备boot/刷写。无需为这次打包重新编译。
详见docs/FRAMEBUFFER-SERVICE-F1A.md末节。

**以下为历史交接；上机步骤以上段0121a5为准。**

**CURRENT：用户暂缓BUG-003，转入F1；0175 / F1a硬件资源与背光服务迁移实现、本地验证完成，待用户push云编译。**
缺时间异常已写docs/BUGS.md#bug-003，0121a4原始证据保留，F0未闭环、不记全部验收。
先做F1a：SweetMDSS持有ADT资源和workloop/gate，迁入原探针/DCS；背光通过每consumer的
匹配通知取得typed backend。sweetmdss默认0，新路径禁止BSD旧映射/写入；失败不静默回退。
F1b随后做用户客户端分发表/参数校验和状态gate，不与本轮硬件迁移混测；F1整体尚未验收。
旧的“继续离线查缺时间模式元数据”NEXT已被用户本次决定取代，不重新启动专项ROI测试。
实现提交a22e1b3；详见docs/FRAMEBUFFER-SERVICE-F1A.md。0175六文件严格应用/逐字节比对通过，4个源码真实CI
参数arm64单文件编译rc0；既有文件无新增告警类别（不是零告警）。16项host测试通过，
ASan/UBSan；LSan仍detect_leaks=0，fixture对象/映射计数归零。底稿scratch/framebuffer-0175。
新--framebuffer-service出baseline/mdss两档，仅sweetmdss差异，共用派生ADT：原ranges保留，
追加低PA别名映射GCC0x10b000，9个资源PA/长度验证；INTID115仅metadata、不提前接IRQ。
两档均preserveoff=1、trace/proof/contract=0、probe=1/loglevel2，原IRQ/充电runtime3保留。
旧0121a4内核被新marker正确拒绝；bash -n通过。未改CircleCI/未push，暂无新云产物/boot。
NEXT：用户push本轮提交（git log顶部）→下载kernel/buildtree/log→核对0175六文件/manifest/
markers→打两档、回读boot；上机等待35秒探针、调亮度、手动息屏/唤醒与输入/充电回归。
不再安排缺时间专项证明；没有实机证据前不验收F1a/F1。不要按下段历史NEXT要求重测。

**以下为历史交接。**

编译/打包/刷写/取日志的完整流程见 docs/BUILD-GUIDE.md（2026-11-08 新增）。scripts/build-mh2-uefi.sh 默认 FD_BASE 已改为 0xA0000000。

**CURRENT：0121a4 / 0174 两轮持久日志已分析；同ID新源ROI全黑有证据，F0未验收。**
详见docs/FRAMEBUFFER-PROOF-0121A4.md。proof1/2均未回卷，279765/420254有效字节，SHA与
行号见报告；底稿scratch/artifact-0121a4/{fb-contract-proof*.txt,*.timeline.txt,log-analysis.json,
analyze-logs.py}。用户反馈第一轮插电/UI无反应但电源能唤醒；第二轮插电大logo后缺时间，
原本亮屏插电也同样缺时间，息屏拔电正常，取消关机后的锁屏正常（更新旧描述，不据此宣称修复）。
proof1第3165–3210行Type-C RID/APID271/bit0 status1持续pending→隔离电池group1→consumer
退出/禁充/撤回输入，最后保留96%。之后按键group0/显示仍活动，已补BUG-002，非全内核死锁。
proof2无ERROR/WARN/隔离或已记录helper失败；6次off/7次on含启动，61样本(55swap/6replay)。
18份新layer/同请求ID及64位seq/完整ROI/rc0的全黑样本，2份启动、16份后续插电窗口（含过渡，
不把每份都称为独立故障）；关键samples20–27、ID151→370、164.031–171.363s连续约7.33秒
源layer/scene/panel同黑，421200像素完整检查、逐像素diff0。之后拔电样本31–38等ROI恢复非黑。
这补上旧live-source会复用的限制：只能证明复制进驱动的该几何ROI全黑，非硬件fence/整屏/时间
字形识别，也不能把CA本身定为根因。启动窗口有非黑候选sample3/4，但无截图识别时间。
全部61份panel比较diff0；6份空layer的replay按合法blank分开，不拿来证明新源内容。
第一条长日志有255字符截断(1轮22份/2轮12份rc缺失)，按sample拆分，缺rc不补0；18份关键
全黑样本均完整rc0。可用observed时间对28/56份，采样中位38.644/45.568ms，有明显开销；
部分cost尾部被截断不能解释成4ms。以后保留日志时拆短/结果前置，不单为此重编译重测。
第二轮组合键在4702行后，最后sample61在4691，关机/取消未捕获对应像素，不能解释半透明。
NEXT：不重复本轮或旧A/B，继续离线核对模式/颜色/时序注册属性、sel79/block0x40及本版无匹配
mode fallback，明确实际分支后设计独立修复；不任意填ID1、不强行重放旧时间、不直接写MDP定因。
F0仍未验收、不提前验收F1/F3/F4；BUG-001/002独立。本轮只分析/文档，未改驱动/CI、未push。

**以下为历史交接；下一步以上段实测结果为准。**

**CURRENT：0121a4 / 0174 产物核对与单档boot打包完成，等待用户实测。**
预期实现提交d0f9555，用户已下载xnu-buildtree-0121a4.gz及xnu-kernel-0121a4.gz。
41个源码文件（含0174两文件）逐字节匹配，114个CI源码marker和新proof内核marker通过；
manifest匹配，kernel SHA256=692153135ed2286b9e39cddcbce7f2079cb323afadb069e767a255f0f20b71ca，
与0121a3不同。未收到本轮构建日志/CI run ID，未检查全量编译警告。
已生成out-bs/boot-16k-0121a4-fb-contract-proof.img，35,667,968字节，SHA256=
854a82bfc8f942f20f8145fc3e4f21b6a2adb38e94ec5644539c45b8cb48bc06。
最终boot回读header/FAT，kernel blob/payload/ADT/TOML逐字节匹配，参数无重复；contract=1/proof=1/
preserveoff=1、trace/probe=0、loglevel=2及既有IRQ/电池runtime3。底稿scratch/artifact-0121a4/
{verification.json,source-compare.json,boot-verification.json,verify-*.py}。
NEXT：用户fastboot boot单档；启动正常锁屏先停5–10秒并记时间是否显示，再手动息屏插/拔电各
2–3次，大logo消失后停5–10秒；最后一次唤醒30秒内关机界面/取消，观察时间及滑条区域半透明颜色。
导出sweetlog-0121a4-fb-contract-proof.bin，注明视觉表现/冻结/明显延迟；不重复旧对照、不测auto-lock。
旧0121a2首次off前也有ROI非黑样本（普通118.052s/949，保留145.307s/1007，均6660点/diff0），
拔电249.168s有1044点，但无截图/逐秒动作对应，不能确认这些像素就是时间数字或完整覆盖时间。
本轮仍只做几何ROI统计，同ID不能替代画面身份识别；硬件完成和视觉修复尚未验收，F0未验收。
本轮仅核对打包/文档，未改驱动或CI、未push；BUG-001/002仍独立。

**以下为历史交接；当前上机步骤以上段0121a4为准。**

**CURRENT：0174 同一swap像素取证实现及本地验证完成，待用户push后云编译；尚无新镜像。**
用户追问是否能证明原始提交缺时间，随后授权继续；因此先补同ID取证，顺序优先于下段旧NEXT的
模式/颜色时序元数据核对。见docs/FRAMEBUFFER-SAME-SWAP-PROOF.md。新sweetfbproof=1默认关闭，
依赖0173 contract；仅修改SweetFBContract.inc并新增SweetFBProof.h。源复制→自有layer→组帧→
写扫描内存后的ROI回读，不重读CA source；记录请求/scene/layer的ID和64位seq、surface、
update/disable、rect、background、源复制时间与观察时间/耗时/最终rc，区分新layer、缓存、blank。
1080x2340时完整检查时钟ROI(180,195,720,585)的421200像素，RGB/alpha统计及逐像素diff。
回读借用空闲scratch，无新增分配；gate外采样但active作业仍背压，Finish后只输出冻结job数据。
每次off→on/首次on请求开30秒窗口，通常1Hz，重放/首swap可立即采；最多32/窗口、256总计、12窗口。
无新timer/轮询；无提交就无新样本。CPU回读不是硬件显示fence；非黑不识别文字，全黑不定位到CA/SB。
真实CI参数基线/修改后arm64单文件编译rc0，无新增告警类别；0174严格应用及两文件逐字节比对通过。
14项host回归全过，ASan/UBSan通过；LSan仍受ptrace限制，detect_leaks=0且fixture对象/分配计数归零。
底稿scratch/framebuffer-0174/{verification.json,tests.json,*.compile.txt,test-*.txt}。
打包新增--framebuffer-proof，仅出fb-contract-proof，contract=1/proof=1/preserveoff=1/trace和probe=0/
loglevel=2及原IRQ/电池runtime3；bash -n通过，旧0121a3内核缺proof marker被正确拒绝。
NEXT：用户push本轮提交（git log顶部），下载新kernel/buildtree→核对源码/marker/manifest→打单档并回读。
测试手动息屏插/拔电各2–3次，大logo后停5–10秒；最后唤醒30秒内关机界面/取消，观察时间和
滑条区域半透明颜色，留sweetlog-编号-fb-contract-proof.bin。无需重复旧对照、不测auto-lock。
这轮是补证而非视觉修复；F0未验收，不宣称F1/F3/F4完成，BUG-001/002独立；未改CI、未push。

**以下为历史交接；当前下一步以上段0174为准。**

**CURRENT：0121a3 / 0173 两档日志已分析，用户确认两档一样、未见新异常。F0未验收。**
见docs/FRAMEBUFFER-CONTRACT-0121A3.md。对照/实验ring均未回卷，451944/460493有效字节；
SHA与行号见报告，底稿scratch/artifact-0121a3/{fb-contract-*.txt,*.timeline.txt,log-analysis.json}。
实验启用标记在75行，30326400字节；49个wait中26成功、23 Busy，Busy均为0x80000000且
id==display；ID1在display1时Busy，切display2后成功，options1在仍显示时成功，确实走新分支。
无helper ERROR/reject/timeout，初期96条常规预算耗尽不能误读成ID24后停止；15条blank，
7次off/8次on（含启动），最后blankID695后仍正常on。没有像素重采样，不能声称时钟ROI已正常。
用户肉眼无改善，因此源快照/等待/blank补全不是当前视觉异常的充分解释，保持0173独立开关，
不默认启用、不重复同类A/B；F1/F3/F4仍未完成。本轮取消/停止未触发的并发分支不算实机验收。
对照末段5281起Type-C APID271 bit0/2/4/5 status0x35导致电池group1隔离、consumer失败禁充，
保留97%；按键group0未隔离，后续仍有亮屏/触摸活动。已补BUG-002，不扩显示线；实验未复现
不能当修复（两档时长/操作次数不同）。实验三条USB命令WARN不能单独认定枚举失败。
NEXT：先离线核对显示模式/颜色时序注册属性、sel79/block0x40及本版无匹配mode的fallback；
完整恢复分支后才做独立修复，不直接填ID1。半透明合成能力另查，不按无GPU或transform-only
警告定因。不需用户重复本次测试。本轮仅分析/文档，未改驱动/CI，未push，无新镜像待编译。

**CURRENT：0121a3 / 0173 产物核对与两档boot打包完成，等待用户实测。**
用户已下载xnu-buildtree-0121a3.gz、xnu-kernel-0121a3.gz，对应预期实现提交6c2e77d。
40个源码文件（含0173三文件）逐字节匹配，114个CI源码marker及新内核marker通过；
manifest核验通过，kernel SHA256=513d16fa31f746cac23573ba1ac87343a0ffe03c361cf7eb0835b4c44f04a8c8，
与0121a2不同。无本轮构建日志/CI run ID，未检查全量编译告警，不据产物反推零告警。
已生成out-bs/boot-16k-0121a3-fb-contract-{baseline,owned}.img，两档均35,667,968字节。
baseline SHA256=33ee5f10df8230b6071cc4c9c5ab689334b239eaf56cf97685cc2398d88ef30c；
owned SHA256=67e8a96ea29c57e8e0c59d63d5d999e508d010e15096fd8ad0cbe3f48a70c085。
从最终boot回读header/FAT，kernel blob、kernel payload、ADT、TOML逐字节核验通过，参数无重复；
两档只差sweetfbcontract=0/1，均preserveoff=1、trace/probe=0、loglevel=2、既有IRQ/电池runtime3。
底稿scratch/artifact-0121a3/{verification.json,source-compare.json,boot-verification.json,verify-*.py}。
NEXT：用户依次测试baseline/owned：手动息屏插/拔电后时间、关机滑条区域半透明颜色、取消后的时间，
各留持久日志sweetlog-0121a3-fb-contract-{baseline,owned}.bin；关注冻结/延迟并回归输入与充电。
尚未刷机/上机或验收F0，不测自动锁屏；未改驱动/CI，未push，本次仅核对打包与文档记录。

**CURRENT：0173 CPU自有快照实验实现/本地验证完成，尚未云编译或上机。**
详见`docs/FRAMEBUFFER-OWNED-CONTRACT.md`。新增0173补丁（三文件：SweetFramebuffer.cpp、
SweetFBContract.inc、SweetFBContractState.h）；sweetfbcontract默认0，仅MH2LM可开启。
提交线程在gate外复制源到自有快照/组帧/写panel，gate内维护归属/reading/cache/display/终态，
源释放后重放只读自有内存；timer仅保留原软件vsync通知。支持无缩放单layer0 BGRA、持久背景、
disable/合法blank；其它启用层明确Unsupported。options0/1区别、2整队列、3/4配置ready、noBlock
及有界commandSleep均接入。52/81取消和77时间ABI核对；81使用同mach_absolute_time的Begin
cutoff是实验策略，原生provider虚表/内部完整语义未恢复，非零timeout毫秒是显式兼容边界。
source/自有像素/display分开，CPU写完不等于硬件整帧显示完成；约28.9MiB、同步多次copy会影响性能。
取消不提前归还读者；commit后取消不回滚已输出像素。helper每调用retain，stop撤入口、唤醒/等调用
退出再释放；初始化失败不fallback。实验用户客户端显式共享外部调用锁，避免wait挡住提交。
三文件strict apply及逐字节比对通过；基线/修改后CI参数arm64单文件编译rc0，无新增告警类别
（非零告警）。14项tools/tests通过，ASan/UBSan通过；LSan报ptrace fatal未通过，detect_leaks=0
重跑且fixture对象/缓冲计数归零。底稿scratch/framebuffer-0173/{verification.json,tests.json,...}。
打包新增--framebuffer-contract，baseline/owned两档仅sweetfbcontract=0/1不同，均preserveoff=1、
trace/probe=0、loglevel=2，共用已验收IRQ/电池runtime3参数。旧0121a2内核被marker检查正确拒绝。
F0仍未验收，不代表F1/F3/F4已做完，不认定缺时间/滑条区域半透明异常已修好。
NEXT：用户push本次提交（git log顶部），下载新kernel/buildtree→核对0173三文件/marker/构建
日志→一次打包两档→手动息屏插/拔电、关机半透明/取消观察，导出两档持久日志。不测自动息屏。
未改CircleCI，未push；没有本轮CI编号/新可刷镜像。BUG-001/002仍独立，不扩本轮。

**以下为前轮交接，0173实现状态以上段为准。**

**CURRENT：原版内核SwapWait语义核对与帧状态设计已完成文档；未实现新行为。**
见`docs/FRAMEBUFFER-SWAP-CONTRACT.md`，接续1d8c71d。原版16.7.16 kernelcache解压SHA
2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221，UUID28E24CE2-BA1C-38B1-AC56-C0BE08A077BC。
28项原始指令/dispatch记录通过，底稿scratch/framebuffer-abi-0121a2/kernel-wait-verification.json。
legacy gated wait正常分支：options0=ID既非display也不在pending/queue，1=不在pending/queue，
2=整队列空；3/4=配置字段0x1478非0/为0，失败直接BadArgument。高位noBlock对等待分支返回Busy。
ID0含义取决于options；不能把0x80000003当排空，也不能统一lastCompleted或copy-done。
selector6原生dispatch/用户wrapper已核对；provider虚表至gated实现的完整链尚未恢复，不冒称已直连。
方案明确workloop/gate唯一改状态、非阻塞查询、有界commandSleep、独立copy执行者、queued/reading/
源释放/displayCurrent/终态分离、ID回卷和客户端归属、stop唤醒与同步清理。源释放后重放必须来自
自有像素；retain不保像素。显示计划§2.4已撤掉“覆盖pending统一算完成”的粗略表述。
NEXT：继续核对取消入口/虚表/配置ready来源和当前copy后端display节点；将状态设计落实为实际驱动
复用的逻辑与host测试，再接独立行为实验档。F0仍未验收，不偷跑F1/F3/F4，不混入只读取证开关。
本轮仅文档、未改CI/内核/设备，未push，无新镜像，不要求重复0121a2。具体UI根因仍未定。

**CURRENT：F0 本版用户态 ABI 离线核对已推进；症状校正为关机滑条区域半透明颜色未正常渲染。**
见 `docs/FRAMEBUFFER-USERLAND-ABI.md`。不是控件几何重叠，相关计划/报告已校正。
本地scratch/dsc由ipsw识别iOS16.7/arm64，cache UUID 8f970452c9e33e88b114feb1864bc9e0；
定位原IOMFB/QuartzCore并核对17项实际指令，底稿scratch/framebuffer-abi-0121a2（含验证JSON）。
确认：record+0x10c是背景色，bit31背景更新；CA set_all_layers_to_null明确生成黑背景+三层null，
日志0x80000007全disable是合法blank。当前跳过surface ID0、忽略disable/background是实现缺口。
确认：CA can_update_在SwapWait返回0后清除页swap ID并推进资源回收；实机具体ID等待均rc0，
而内核仅排队尚未copy。这是假完成/复用契约缺口，优先于单补模式ID；尚未证明所有症状同因。
GetDigitalOutMode确实用block0x40/40字节，读+4/+8两ID；当前清0，但0是否匹配fallback未知，
不能仅填1。SWContext有blur_surface，不能仅按缺GPU认定半透明无解，也不能直接给memcpy加混色。
NEXT：厘清SwapWait flags阶段/非阻塞Busy/id0 ready/取消覆盖，设计workloop+gate内状态与可释放
gate的有界等待；同时处理持久layer/background和blank。模式契约成组核对另档验证。半透明效果
如需取证做局部alpha分布，避免每秒79ms取证。F0未验收，不进入F1，不要求重复刷0121a2。
本轮只改文档；没有新kernel补丁、待push版本；未改CI、用户态二进制或设备，未push。

**CURRENT：0121a2 F0 v2 两档持久日志已分析；用户确认两档显示异常相同。F0 未验收。**
详见 `docs/FRAMEBUFFER-F0-0121A2.md`。普通/保留档日志完整未回卷，SHA/行号/统计见报告。
插电后多次稳定观察：时钟 ROI 的当前 source 与扫描内存同为 RGB0、差异0，且期间继续 copy；
不能仅用息屏擦除或漏拷解释，更不能据此认定 CA 自身 bug。普通首次 off 擦黑且 on 重放空记录，
preserveoff 确实保留旧像素，但两档后续均同黑；空记录可能是合法 blank，不能直接过滤/硬恢复。
关机滑条区域半透明效果异常未独立定因，本版正常充电过渡 UI 尚未独立核验。
接口候选：每次 on 后 selector79/block0x40 输出40字节，当前全清0；参考 HakiOS 为当前模式ID，
并与 TimingElements/ColorElements/IOMFBDisplayRefresh 一致。当前无模式属性，SwapWait 也为空。
需先核对本项目用户态 ABI，不能凭跨版本参考直接定因。source 复用证据已捕获：留在panel的画面
与上次copy来源surface的当前内容不同；retain 只保对象存活，不防 CA 改写，必须核对完成/复用协议。
硬件：VIG0仍0x9d200000/stride4320/1080x2340；INTF frame-en=0，不能测TE，F3用IRQ时间戳。
SMMU虽route TRANS/CB2，但CB2 SCTLR=0xe0/M0、TTBR/TTBCR/MAIR0，本次stage1转换未使能；
不再据TRANS单独要求F4a。F4前仍重查live配置/新扫描内存归属和可达性，不假定任意物理页可用。
两档本轮未见ERROR/WARN、mux隔离或panic，BUG-002仅未复现，未修复；独立bug不扩入显示线。
trace每秒样本中位约79ms（含读/比较/日志），明显增加workloop负担，不能代表默认性能。
NEXT：离线核对GetBlock模式/属性、SwapEnd/SwapWait/完成通知和update/disable/background语义，
再设计针对性修复；目标framework后续已定位，离线结果以上段及ABI文档为准。不立即重复同类上机读数，
不进入F1，不直接改panel/SMMU。本轮只改分析/计划文档，无新驱动待push，未改CI，未push。
底稿 `scratch/artifact-0121a2/{fb-trace*.txt,*.timeline.txt,log-analysis.json,analyze-logs.py}`。
产物核对历史：38源码、114marker、kernel/两档boot回读通过，记录提交e89754d；
kernel SHA256 `5bc262e74534dbc2952a09342a0d97348561226d58a16a5a7c937ebad10966b1`。
镜像仍在out-bs，SHA/参数见F0-V2；无本次CI run ID/构建日志，未查全量编译告警。

**0121a1 编译失败与0172修复记录（0121a2产物核对已完成，以上段为准）。**
日志 `logs/xnu-build-log-0121a1.txt`，SHA256
`902da91ad88582b5c4118dd7e600d7b4689729d358a2be07b5e045580c7cb090`。
0171已应用；失败是新增3源码的私有头文件引用用了`<bsd/dev/...>`，CI没有XNU根目录搜索路径。
fatal在16504（sweet_display_probe）、22940（SweetSurface）、23034（SweetFramebuffer）。
上轮本地验证额外加了`-I edited根目录`，掩盖错误，不能将其视作等价CI编译。
0172仅改4条include：BSD同目录双引号，IOKit使用../../../bsd/dev/，与现有PMIC驱动一致。
去掉额外源码根目录和arm目录搜索路径后，原0171三文件均复现同一fatal，修复后三文件rc0。
0171→0172严格应用、4文件逐字节比对通过；scratch/framebuffer-0172保留编译日志和verification.json。
未改circleci.yml，未push。无需产物诊断此编译故障；需要用户push后下载新kernel/buildtree，
再按下段F0 v2两档流程核对打包。0171诊断策略不变，F0仍未验收。

**0171 / F0 v2 实现摘要（0121a1 include 编译问题由顶部 0172 修复）。**
见 `docs/FRAMEBUFFER-F0-V2.md`。新增 BGRA 四通道/时钟中心/横条 ROI 取证，
同坐标 source-panel 比较（含裁剪/stride/offset bounds）、最新提交与最近实际拷贝帧分开记录；
亮屏每秒采样上限600次；selector追到127；surface前32次create最多各64键。
仅记录source id，采样时临时retain，不长期延长surface生命周期；明确live-sample限制。
frame改读+0xac，记录真实间隔、计数使能、行相位，不声称已测TE频率。
**SMMU地址校正：** 原机DT resource=0x100000；ID1 NUMPAGENDXB6 ⇒ GR半区0x80000，
与LG cb_base=resource_size/2一致，CB2 PA为**0x15082000**，不是0x15042000。
trace≥2且已知ID/TRANS/CB2才读配置，不遍历页表、不写硬件。
四文件strict apply逐字节一致；三源码arm64交叉编译rc0、无新增告警类别（并非零告警）；
C/C++ ASan/UBSan边界测试通过；13个host测试最终全过（充电测试因LSan/ptrace限制，
仅加detect_leaks=0重跑通过）；全套结果在scratch/framebuffer-0171/tests.json。
打包脚本旧0121内核拒绝通过；两档现用sweetfbtrace=2/sweetloglevel=2，仅差preserveoff。
没有新内核/可刷镜像；下载新kernel/buildtree后核对并打包回读。测试手动息屏插拔、滑条/取消，
不安排自动锁屏。保持原渲染/擦屏/DCS/IRQ/充电策略，未改circleci.yml，未push；F0未验收。

**独立 Bug 台账：`docs/BUGS.md`。** 按用户要求，将原功能基本完成后暴露的遗留/回归问题独立
登记：BUG-001 USB多次插拔枚举失败；BUG-002 PMIC持续pending后电池/按键group被隔离。
不自动加入当前显示计划、不自动作为显示阶段前置；需修复时单独安排。该两项只建档、整理证据，
本轮未改其驱动；显示F0补丁待编译（见顶部）。显示缺时间/滑条区域半透明效果异常及F0取证不足仍在显示计划中。

**CURRENT 显示线：0121（d58e7fc / 0170）两档持久日志已分析；F0 未验收，不重复要求本轮测试。**
详见 `docs/FRAMEBUFFER-F0-0121.md`。普通档 USBIN(group1/APID269/bit4) 在第6436行触发 pending
隔离，电池 consumer 失败后禁充并保留99%；第6618行 power(group0/APID248/bit0)同样隔离，
按键 worker 停止，最后 display off 后再无电池/按键唤醒，触摸和用户态仍输出，未记录 panic。
保留像素档无ERROR；不能据一次对照断言 preserveoff 修复 mux。下一启动 gauge 原始MSOC
0xfa=97%，上次最后0xfe=99%；故障间无遥测，实际消耗/电量计重估贡献未知。
用户确认当前无自动息屏，已撤掉auto-lock项；插电大logo后缺时间、拔电有时间、关机滑条区域半透明效果异常、
取消后缺时间均已记录，黑背景本身不算故障。Apple同版正常UI对照尚未核验（网络/DNS失败）。
普通档首次off hash改变，preserveoff首次off/on不变，证实保留参数有效；仍未解决全部显示现象。
0170取证缺口：hash只读稀疏B通道且不覆盖时钟；source选择未按ring时间序、常被id0覆盖；
delta frame误用0x2b4 autorefresh配置，无法得TE频率；未记disable/bg/实际present像素。
SMMU SMR3=0x84200800/S2CR=0x2=TRANS/CB2，CB2使能/页表范围待查，F4不可假定新物理页可用。
NEXT：继续补F0取证；mux四遍drain后永久隔离问题另记BUG-002，修复单独安排，保留无轮询回退原则；
不进入F1，不直接改panel序列/切自有扫描缓冲。0121分析轮仅改文档；后续0171补充实现见顶部。
底稿 `scratch/artifact-0121/{fb-trace.txt,fb-trace-preserveoff.txt,log-analysis.json,analyze-logs.py}`。
日志SHA/有效字节/flush及详细源码证据见报告。以下保留产物核对记录：
buildtree 中 27 个核对文件与 0120a18 + 0170 预期逐字节一致（含 0170 改动的全部 3 文件），
现有 114 个源码 require_marker 通过；kernel SHA256SUMS 通过且包含 F0/原 IRQ/充电 marker。
kernel SHA256 `6e3be4432ea04c331ac9aef8e5783e0980ed2174dc463c44f76eda24bfbb48a0`，不同于 0120a18。
用户未提供本次构建日志/CI run ID，未查全量编译警告。核对底稿 `scratch/artifact-0121/verification.json`。
镜像均在 `/home/shenli/work1/out-bs/`，35,667,968 字节，header v1/page 2048；
kernel blob/ramdisk 内 kernel、ADT、TOML 回读一致，启动参数唯一，两档只差 `sweetfbpreserveoff=1`：
- `boot-16k-0121-fb-trace.img`，SHA256 `e5aaead64c1736690ed18f645c91f3cab49b3a5b9f42d920be8d11706e998ed5`。
- `boot-16k-0121-fb-trace-preserveoff.img`，SHA256 `39d5837bf3992f7d5a91b29a7ff39d3b7e8e9a108678b1311bff0a018cbdccd4`。
两档保留三键/触摸/USB ACM/电池 IRQ 与 runtime3 充电，温度 IRQ 扩展未启用。
原两档测试已完成，当前结论以上段为准。完整回读记录 `scratch/artifact-0121/boot-verification.json`。

**persist4 持久镜像已生成，待用户先 fastboot boot 验证再刷 boot_a/boot_b。**
`/home/shenli/work1/out-bs/boot-persist4.img`，35,543,040 字节，SHA256
729a858db55cbddf465bbd730a69d5567a4f224a10e74b3ed93982e26987bc84（脚本 out-bs/mk-boot-persist4.sh，无 sudo）。
- Mu-Silicium **RELEASE** FD：同一源码（Mu-Silicium cca95b7、DarwinBoot 4ad0c42），FD_BASE=0xA0000000
  （scripts/build-mh2-uefi.sh 默认 0x9FC00000 是错的，与 Resources/Configs/mh2.toml 不符；本次用 0xA0000000）。
  FD SHA256 cfb79257…6e05。BootShim 与已验证 blob 逐字节相同，追加 DTB 相同。验证方法：同法重编 DEBUG
  与旧 DEBUG FD 仅差编译时间戳及其导致的地址常量，已恢复旧 DEBUG 构建目录。
- RELEASE FD **从未在设备上启动过**，这是本镜像唯一新增风险；ABL/fastboot 不受影响，失败可 fastboot 刷回 boot-persist2/3。
- 菜单：默认 `msd`（RELEASE MassStorage.efi，超时 5s）；第二项 iOS：0120a18 内核（61d3cbda…）+ adt-16k-3g.adt +
  0120a18 tempirq 档原样参数（runtime3 充电、温度中断、USB ACM、三键）。之后 Restart/Power off/Tools。
- 镜像回读：header v1/page 2048、kernel 段解压后 = BootShim + RELEASE FD、DTB 相同、ramdisk 内 4 个文件逐字节一致。

**显示/面板重构计划（第二版）：F0 两档实测已分析，取证尚不充分，待补齐。** 见 docs/FRAMEBUFFER-PANEL-PLAN.md：
F0 取证→F1 SweetMDSS IOService 分层→F2 DCS真关屏→F3 MDSS RD_PTR vsync→F4 自有扫描缓冲+SSPP硬件裁剪+
双缓冲翻页→F5 CA surface 直接扫描（cache clean，零拷贝）→F6 可选kickoff→F7 IOPM/panic/清理。
接手 agent：0170 只做只读取证；通过验收后再更新该文档§6/§7和本段，不能提前进入 F1。
打包脚本新增 `--framebuffer-f0`，同一内核产出 `fb-trace`、`fb-trace-preserveoff` 两档；
两档均保留已验证的按键/USB/触摸/电池 IRQ 与 runtime3 参数，后者额外设置
`sweetfbpreserveoff=1`。
已复核工作区根目录 `docs/panel.md`：量产节点为 Tianma/R66456A 1080×2340@60、RGB888、DSI0
四 lane command mode，TE=GPIO8，ON/OFF DCS 顺序必须原样保留；vendor payload 仍是 opaque。
另外，Android DTS 的 `splash_region`（0x9c000000/36 MiB）与 0120a17 运行时
`SRC0_ADDR=0x9d200000` 的范围关系尚未证明，已列为 F0 的硬性核对项。
电池线 0120a18 用户正在实测（见下段），两条线互不阻塞，但同一内核产物可同时打两边的 profile。

**CURRENT: 0120a18 tempirq档已实测：温度中断路由挂接成功（APID285）但温度只变1.25°C未触发；截止后25分钟未回充（固件为VBAT回充模式，LG用SOC98%）；关机日志未落盘。** 详见docs/BATTERY-BASIC-CHARGE.md 0120a18节。
（下为产物核对记录）
logs/xnu-buildtree-0120a18.gz SHA256 2dcb6e19…97ec23、xnu-kernel-0120a18.gz 4c0ff197…c97e7e；34源码与
scratch/battery-0169/verify逐字节一致，114个require_marker通过；kernel 61d3cbda702d7218d1e32de513cd27728f121bab63812e4336501e8a3c645e59
含TEMP IRQ v1/battery-temp-delta/BASIC POWER v1。无CI run ID，未查全量警告。out-bs/boot-16k-0120a18-
battery-basic-charge.img（1dc8ce6b…6c1f）与-tempirq.img（93340afc…ed7a），各35,667,968字节，回读一致，
参数仅差sweetbatttempirq=1。底稿scratch/artifact-0120a18。
（以下为0169实现记录）
xnu/patches/0169-mh2lm-battery-temp-irq.patch（4文件）；测试/CI/打包脚本已更新。12个host测试全过，
7文件交叉编译rc0，strict apply逐字节一致（scratch/battery-0169）。NEXT：用户push；下载0120a18产物后
`tools/mk-mh2lm-event-matrix.sh <kernel> 0120a18 --battery-basic-charge`，得到普通档和-tempirq档。
测tempirq档：充满后继续插≥60分钟看回充；中途从iOS关机或重启一次留POWER日志；看状态栏闪电/满电。
详细见docs/BATTERY-BASIC-CHARGE.md 0169节。

### Previous 0120a17 handoff

**CURRENT: 0120a17实机充满：CC约1.97A→404s起CV 4400mV TAPER→4878s硬件TERMINATE，71%→100%，喂狗305次无错。前一次15分钟实测也成功。**

证据见docs/BATTERY-BASIC-CHARGE.md末节及scratch/artifact-0120a17/basic-charge-persistent.txt。
喂狗54次无错，failures=0。开机电脑口SDP不充（用户确认正常）。
NEXT（见docs/BATTERY-IMPLEMENTATION-PLAN.md顶部段落表）：第5～6段补强一批——0169 batt-temp-delta IRQ、回充、停止留证、UI核对，之后电池IRQ结案转framebuffer。0169待用户确认后实现。

### Previous 0120a17 artifact handoff (superseded)

**CURRENT: 0120a17（e30cf81/0168）云产物核对通过，runtime3基础充电boot已打包回读，待用户实机测。**

用户给logs/xnu-buildtree-0120a17.gz（SHA256 8ff69927…ba575）、xnu-kernel-0120a17.gz
（ab0e4f4b…363b6）。buildtree中34个源码与scratch/battery-basic-charge/verify逐字节一致，
CI 110个require_marker通过；kernel SHA256SUMS校验OK，
kernel 5ef5143973ce163510b5e78c7de8123f096f2ddc734eee199072437d4bd24e63，含BASIC v2各marker。
没有CI run ID/构建日志，未查全量警告。打包：/home/shenli/work1/out-bs/
boot-16k-0120a17-battery-basic-charge.img，35,667,968字节，SHA256 acd5da77…924b4e，
header v1；回读kernel/ADT/TOML/ramdisk逐字节一致，参数唯一，runtime3/irq4/adcconv0/deps0。
底稿scratch/artifact-0120a17（verify-artifacts.py、verify-boots.py、*.json），不入git。
硬件JEITA结论（docs/BATTERY-BASIC-CHARGE.md新节）：LG init关硬件补偿、从不开HARDLIMIT，
mh2lm电池档无jeita阈值；实测0x1090=0。硬限是否生效【待核实】，不能当保护层。
NEXT：用户`fastboot boot`上面的img → 接充电器10分钟以上 → 导出sweetlog-0120a17-*.bin。
期望：`SWEET BATT BASIC: v2 step=1 ... enabled=1`、pets递增、FG电流为负、stage1～3。
可选0169（未做，待用户决定）：FG RRADC batt-temp-delta IRQ接入电池workloop。

### Previous 0168 implementation handoff (superseded)

**0168 runtime3基础充电已实现并本地验证。**

改动（ios-mh2lm提交见git log）：xnu/patches/0168-mh2lm-basic-charge-control.patch
（新sweet_charge_basic.h；sweet_charger.c/h新增BOARD_FAST_CURRENT_MA≤2800、
BOARD_FLOAT_VOLTAGE_MV≤4430、CHARGING_PET；SweetBattery.cpp runtime3分支）、
CI 4个require_marker、打包脚本--battery-basic-charge、两个host测试。
根因：旧放行门实机永远不满足（详见docs/BATTERY-BASIC-CHARGE.md）。
观察（本地）：12个host测试全过；7文件CI buildtree交叉编译rc0；0168对0167结果树
strict apply与patch -p1 dry-run通过、逐字节一致（scratch/battery-basic-charge/
validation-results.json）。未验证：实机是否真充电、SMB1355是否听SMB_EN、bite恢复、
WillSleep时机——都标【待核实】。
NEXT：用户push → CircleCI → 下载kernel → `tools/mk-mh2lm-event-matrix.sh <kernel> 0120a17
--battery-basic-charge` → fastboot boot → 接充电器10分钟以上 → 导出sweetlog。
期望：`SWEET BATT BASIC: v2 step=1 ... enabled=1`、pets递增、FG电流为负、stage1～3。

### Previous 0120a16 identity handoff (superseded)

**CURRENT: 0120a16身份档未通过：第三请求USBIN RESULT VERIFY-12；ACK及时，无本档超时。下一实现先细分校验原因/核对LG数据语义，不追加本批硬件测试。**

实现357b26f/0167，上一打包文档8ea64b0。本轮只读日志/源码/参考及写文档，未改驱动/
CI，未push/操作设备/重打包。日志sweetlog-0120a16-battery-identity.bin，SHA256
cf1d0c93ea6ca932c73a499f558e63dcb27f1355572ea108f9a53a657aa7f3ba。
v1/4MiB，有效672132/flush67/8466行，无回卷/NUL；692电池采样/699.666秒。
ADC只有3请求/2成功/5EOC，约启动53.744秒已停，仅覆盖首ADC起1.2555秒，不能用
长日志宣布持续ADC或混合IRQ负载通过。首BATID888775µV/94786Ω/profile1/2800/4430，
USBIN2 raw1/1024µV（近零，不是有效VBUS）；请求3 GND MORE后RESULT-12/count1，
restore-error/cleanup0，provider停止、身份/token/限值撤销，无后续采样或正permit。

失败时uptime：trigger53.729172604/done53.729846458；GIC53.730575364，scan
53.730618072，ACK53.730633854，notify53.730637343，callback53.730650833，
advance lock53.730652031；deadline53.829172395。GIC→notify61.979µs，ACKwait
0.312µs/hold12.188µs；callback早98.522ms，advance锁等待0.104µs。本档没有-101/
TIMEOUT；全部5ACK wait0.260～731.094µs/hold12.188～31.615µs，不能扩大成修复验收。
恢复前snapshot saved1，53.733314427～53.739926770锁内6.612ms，error0/raw
01 00 08 80 00 00 01 01 00 73。status/channel是后续重读，data lo/hi未记录；-12
复用status/channel/raw校验，具体失败项未知，不能擅自断言raw>=0x8000。

LG参考具体差异：qcom-spmi-adc5.c的adc_read_voltage_data只拒绝==0x8000；
qcom-vadc-common.c的hw_calib_volt将>VADC5_MAX_CODE归零，我们>=0x8000停provider。
差异已找到，但没有本次被拒raw，不能当已证实根因。NEXT：typed ADC后端保存
已读status/channel/lo/hi及明确错误阶段，核对无效码/零输入语义，再做针对性实现/
验证。不能直接放宽数据有效性或给充电放行。保留IRQ/deadline/owner/poison/撤销。
不追加旧identity/watchdog/组合/baseline；新修复完整验证后再通知push。

19IRQ healthy89.934～103.635ms，FG tuple41match1/13/4027；三键power3/down2/up1
对，touch438/empty0，无panic/mux失败/warning；ACM configured0，不推断USB故障。
19CONFIG enable0，无CONTROL/WD命令；主插拔事件在ADC停止后。长期许可/外部SMB1355
隔离/最终managed授权仍未完成。完整文档BATTERY-DEPENDENCY-TESTS末节，路线入口
已同步；底稿scratch/artifact-0120a16的analyze-log/audit-identity及JSON，断言通过。

### Previous 0120a16 artifact handoff (superseded next action)

**CURRENT: 0120a16云产物核对通过，同核五档boot已打包回读，待用户测identity1。**

实现357b26f/0167。真实buildtree的33个相关源码文件与scratch/spmi-ack-arbitration/
verify逐字节一致，原106源码gate通过；kernel manifest及ARBITRATION v1/ACK v1、
ADC/WD snapshot v2和既有输入/IRQ/ACM marker通过。用户仅提供buildtree/kernel，
没有构建日志/CI run ID，不宣称全量警告已查。无0120a16实机结果，不能宣布超时修好。

本轮未改驱动/CI、未push/操作设备。已生成out-bs下五份
boot-16k-0120a16-battery-{evidence,identity,inhibit,watchdog,deps-all}.img，均
35,667,968字节、boot header v1；最终boot回读外层blob/ramdisk与内层FAT
kernel/ADT/TOML逐字节一致。同一新内核、16K ADT，参数唯一，共同runtime1/IRQ4/
ADCconv0，deps0/1/2/4/7；三键/USB/触摸IRQ及USB日志保留，全部不主动启充。
实际kernel SHA256：b2a1affab8549e97ff702320b4e314407e89e27e72fc11c61eb5e049bf19d368。
完整归档/boot SHA与验证边界在docs/BATTERY-DEPENDENCY-TESTS末节，其余路线入口
已同步。底稿scratch/artifact-0120a16，下载/源码/产物/验证底稿不入git。

NEXT：用户fastboot boot
/home/shenli/work1/out-bs/boot-16k-0120a16-battery-identity.img。
桌面后尽快拔5秒→同一稳定电源保持5～10分钟→拔5秒，简测三键/触摸，导出
sweetlog-0120a16-battery-identity.bin，持久日志即可。关注持续USBIN至少覆盖旧59
请求、100ms deadline及MUX ACK wait/hold/scan/notify；hold含调度，v2独立snapshot
耗时不等于连续占锁。出现异常可提前导出，之后按新日志决定watchdog4/组合7。
无需新push/编译，不重刷旧0120a15/baseline/evidence0/inhibit。全部runtime1不主动
启充；首bark起算、长期许可/外部SMB1355隔离/最终授权仍未关闭。

### Previous 0167 implementation handoff (superseded next action)

**CURRENT: 0167 ACK/遥测竞争修复本地完成，待用户push/新云产物；未宣称实机修好。**

用户在ACK/并发阻塞审计后授权实现。基于真实0120a15的0166源码，新增
0167-mh2lm-spmi-ack-arbitration.patch。控制器仍单sp_lock；mux受限IRQ写先原子
登记ACK等待者，再取同锁；普通SPMI访问统一经过IOLockSleep谓词复查，释放时
同锁wakeup，不打断已开始事务。mux每遍最多19路先ACK再按group合并通知，最多
4遍表；所有遍次及GIC重开/失败后再打日志。原按键/USB/ADC/WD均保留，无表顺序
硬换；trace在真实notify前一次发布，固定实例历史无热路径分配/大栈。
public ADC/WD故障观察逐字段让锁，ADC协议错误保存现场+恢复仍原子。快照v2的
first-lock不是连续占锁；新增MUX ACK v1 wait/hold纳秒分别记录等锁/持锁经过时间，
包含调度，不能等同纯总线耗时。启动ARBITRATION v1，dependency打包拒绝旧内核。

本地：严格apply真实0120a15/33文件回读，原106源码gate/CI字节不变、原五档参数
通过；新仲裁100受控调度/1100线程、transport134（含独立快照期间ACK插入）、
mux67+80竞争（含同批全组/Type-C合并/通知中再触发/trace）、runtime全部、ADC
client19+100竞争、初始化/按键并发、日志等级均通过ASAN/UBSAN；7文件ARM64单
编译通过，非云端链接或硬件时延验证。详见docs/SPMI-ACK-ARBITRATION.md及其余
路线入口；底稿scratch/spmi-ack-arbitration，不入git。提交号见git log -1。

未动CI/未push/未操作设备/未生成新boot；不延ADC100ms或WD23/180秒，不增加
转换重试/轮询回退/pet/启充；原owner/poison/ACK幂等尝试/group隔离保持。当前
事务映射/原子恢复仍可阻塞ACK，native调度与真实故障是否修复待下一轮数据。
NEXT：用户push本轮，云产物到齐核对0167后打包同核deps0/1/2/4/7；优先identity1，
桌面后尽快拔5秒→稳定电源5～10分钟→拔5秒，三键/触摸及持久日志，关注持续USBIN
至少覆盖旧59请求。之后按新结果决定watchdog4/组合7，不重刷旧0120a15或baseline。
全部runtime1不主动启充；首bark起算、长期许可/外部SMB1355隔离/最终授权仍未关闭。

### Previous 0120a15 watchdog handoff (superseded next action)

**CURRENT: 0120a15 watchdog4维护/有限停止通过；ADC59确认deadline前pending、ACK/notify晚。下一实现修关键分发/重遥测竞争，不追加硬件测试。**

实现9f49c1d/0166，上轮文档470a376。本轮只读日志/源码和写文档，未改驱动/CI、
未push/操作设备/重打包，无新修复实现。日志sweetlog-0120a15-battery-watchdog.bin，
SHA256 b330efaca945025918974eed1fad5dc9eeb948edea4cc7bfe2907e6f37fbd31b。
v1/4MiB/有效592827/flush44/7341行，无回卷/NUL；497采样、469.531秒。

WD prepare55.957199427秒、初始pet55.958830833秒，error0/actual1；新遥测62/f3、
16CONFIG均enable0。29自然bark，前9各pet2..10，加初始化10pet；GIC→notify
85.885～122.448µs，notify→pet完成90.332～93.156ms（完整电池采样开销）。首
GIC71.856766406，比初始pet15.897936秒；稳定间隔16.378～16.381秒，跨拔插
33.415秒。软件START epoch5/121.236512、epoch12/122.246604，pets4；后者至
138.029354首后续bark15.782751秒，但已有3次自然维护，不能算0120a13无首bark
新plugin失败的同条件通过。236.168015秒样本后正常window complete/pets10/
disable0，后20bark不再pet。无WD FAILED/故障快照，不验收带电bite/深睡/最终授权。

ADC59请求/58成功（3ID/55USB），114consumer EOC/115mux hit，无丢弃。
59首EOC MORE后RESULT-101。关键：第二scan121.333559739早于deadline
121.337562291约4.003ms，channel status1已pending；ACK121.346589479、
notify121.346589843晚9.028ms。scan→ACK13.030ms；timer121.339324010真到期。
首notify→callback22.187µs、advance锁等待0.312µs。snapshot区间121.339799322～
121.346517083、十字段error0/01 00 08 80 00 00 01 01 01 73，锁等待0.340ms/
读6.718ms。两次hit同irq121/work121。已证实硬件事件deadline前可见却未及时
ACK/notify；精确ACK锁等待/抢占/总线分量无证据，诊断读也参与竞争，不能全归锁。
restore/cleanup0，provider撤销，无结果重试/有效许可。ADC失败后WD仍维护。
44IRQ healthy89.537～116.566ms；power/down各4对，上键无本档记录；touch124/
empty0，ACM2，3条EP warning、无panic/mux整体失败。无CONTROL/正permit/启充。

完整文档BATTERY-DEPENDENCY-TESTS末节；NEXT/实施计划/ADC/IRQ/TIMING已同步。
底稿scratch/artifact-0120a15：battery-watchdog-persistent.txt、analyze-watchdog.py/
watchdog-log-analysis.json、audit-watchdog.py/watchdog-verification.json，不入git。
NEXT：不再要求identity/watchdog/baseline/inhibit/组合7；下一实现用本次预截止
pending/晚ACK案例做可复现回归，修关键ACK/事件分发和重遥测SPMI仲裁/调度，
考虑诊断读对紧急路径的影响。保留原owner/ACK/按键公平/group隔离/清理及deadline，
不盲目只换表顺序、不去锁穿透/轮询/重试/扩大期限。修复完整验证后再通知push。
首bark初始起算、长期有效输入许可、外部SMB1355 I²C隔离/最终授权仍未关闭。

### Previous 0120a15 identity handoff (superseded next action)

**CURRENT: 0120a15 identity1已分析：首笔USBIN RESULT超时，抓到EOC锁存后的分发延迟；下一项同核watchdog4。**

实现9f49c1d/0166，上一产物文档f97811f。本轮只读日志/真实云源码、更新文档，
未改驱动/CI、未push/操作设备/重打包，未开始新修复实现。日志
logs/sweetlog-0120a15-battery-identity.bin，SHA256
38000bb01b639453f18bea952490912bae5524d5c3a4dcfa68bbe25577651df9。
v1/4MiB/有效579259/flush54/7268行，无回卷/NUL；565采样、569.517秒。
5个有效BAT_ID（888516～889035uV，94786/95057Ω/profile1/2800mA/4430mV），
35FG tuple均match1/13/4027。6是首USBIN，首EOC MORE/phase3/count1后-101，
restore/cleanup0，provider停止并撤销身份/token/限值；无有效VBUS/正permit/
CONTROL，18CONFIG全enable0，无WD命令。长期许可/外部隔离/最终授权仍未验收。

关键uptime：RESULT trigger done482.004609791，deadline482.103924635；
timer482.105213854（真到期，+1.289ms）；恢复前snapshot锁定482.105971250、
完成482.112758802，十字段error0：01/00/08/80/00/00/01/01/01/73（status/EOC
latched证明已完成，不接受为测量）。第二scan482.182058281、notify482.213030156，
较deadline晚78.134/109.106ms，仍irq10/work10。首notify→callback23.178µs、
advance锁等待0.312µs；snapshot锁等待0.629ms；第二scan→ACK30.972ms。
父GIC早于ground trigger，不能当ADC完成时刻；快照发生deadline后，也不证明
硬件deadline前已完成。已证明锁存后未及时处理，精确阻塞/历史案例同因仍未定。

同drain普通USB/Type-C路由穿插，电池自己的loop样本157/158/159耗98.536/
94.587/100.053ms，其中profile各约30.6ms；与mux共享SPMI事务重叠。
notify仅排事件，无同步重采样。调查方向是急/普通事件公平分发、逐路notify/
重复重采样和SPMI仲裁/调度；优先扫描/有界batch是候选，未实现、不能先承诺
可根治，不能把ACK30ms全部当锁等待。保留owner/ACK/按键公平/group隔离和期限，
先代码模型验证，禁轮询/自动重试/直接穿透/扩大期限掩盖。
18IRQ healthy（90.115～113.613ms），三键配对5/3/4次；touch207/empty1，ACM
配置1次，无panic/mux失败。2条EP0/EP5 warning保留已知USB问题。完整时间线见
BATTERY-DEPENDENCY-TESTS末节；NEXT/计划/ADC/IRQ/TIMING入口已同步。
底稿scratch/artifact-0120a15：persistent、analyze-log.py/log-analysis.json、
audit-identity.py/identity-verification.json；原日志/底稿不入git。

NEXT：用户fastboot boot
/home/shenli/work1/out-bs/boot-16k-0120a15-battery-watchdog.img。
桌面后尽快拔5秒→同一稳定电源≥60秒→拔5秒，简单确认三键/触摸，导出
sweetlog-0120a15-battery-watchdog.bin（持久日志即可）。180秒从驱动准备起算，
不要桌面先空等。核对WD COMMAND/START/首bark GIC/scan/ACK/notify与pet/终止
快照，再决定后续实现合批；当前不重测identity/baseline/inhibit、不要求组合7。
无需新push/云编译，仍不主动启充。若用户要求先修ADC，再按上述证据实现。

### Previous 0120a15 artifact handoff (superseded next action)

**CURRENT: 0120a15实际云源码/内核核对通过，同核五档boot已打包回读；待用户测identity1。**

实现提交9f49c1d（0166），上一文档4dd3dbb。真实buildtree的33个相关源码文件与
scratch/battery-irq-timing/verify逐字节一致，原106源码gate通过；kernel manifest
及ADC TIMING/FAULT/STAGE/SNAPSHOT、WD COMMAND/START/FAULT/SNAPSHOT和既有
驱动/输入marker均通过。用户仅提供buildtree/kernel，未提供构建日志或CI run
ID，不宣称全量警告已检查。无0120a15实机日志，ADC/首bark根因仍未定。

本轮未改驱动/CI、未push/操作设备。实际生成out-bs下五份
boot-16k-0120a15-battery-{evidence,identity,inhibit,watchdog,deps-all}.img。
均35,667,968字节、boot header v1；从最终boot回读外层blob/ramdisk和内层FAT
kernel/ADT/TOML逐字节一致。参数唯一、同一内核/16K ADT，deps0/1/2/4/7，
runtime1/IRQ4/ADCconv0、三键/USB/触摸IRQ及USB日志保留，全部不主动启充。
实际kernel SHA256：83294f81fd0df1d3303eebc82daa0733ae747c4f059e4d1bcd818f9e76e6d552。

完整归档/boot SHA、核对范围及步骤在docs/BATTERY-DEPENDENCY-TESTS.md末节；
BATTERY-IRQ-TIMING/NEXT/实施计划/ADC consumer/IRQ路线图已有最新入口。底稿
scratch/artifact-0120a15：verify-artifacts.py、source-compare.json、verification.json、
packaging.txt、verify-boots.py、boot-verification.json；scratch/产物不入git。

NEXT：用户fastboot boot
/home/shenli/work1/out-bs/boot-16k-0120a15-battery-identity.img。
桌面后尽快拔5秒→同一稳定电源保持约5分钟→拔5秒，简单确认三键/触摸，导出
sweetlog-0120a15-battery-identity.bin（持久日志即可）。5分钟覆盖旧身份档约296秒
故障窗口，若已明显异常可提前导出；优先分析新时序/故障快照，再决定同核
watchdog4/组合7。后两档180秒从驱动准备起算，不先空等。当前无需evidence0/
baseline/旧0120a13四档/独立inhibit，无需新push/编译。长期许可/外部SMB1355
I²C隔离及最终managed-watchdog授权仍缺，不把boot打包通过当实机修复结论。

### Previous 0166 implementation handoff (superseded next action)

**CURRENT: 0166 ADC/watchdog时序取证本地实现完成，验证通过；待用户push并下载新云产物。尚无新实机修复结论。**

承接4dd3dbb四档结果；新patch0166-mh2lm-battery-irq-timing.patch，基于真实
0120a13云源码33文件（scratch/artifact-0120a13/xnu）。本轮未改CI/未push/未操作
设备/未打新boot。详情docs/BATTERY-IRQ-TIMING.md；NEXT/实施计划/依赖测试/ADC
consumer/IRQ路线图已同步。以下旧动作均为历史，勿再让用户重刷0120a13。

实现：mux每route原子奇偶sequence有界拷贝、GIC/drain/ACK/notify时间；现有
ADC/WD hit单行附时间，保留晚到hit。client通过保留provider提供typed trace；
无gate/SPMI/MMIO，不分发事件。ADC trigger/advance锁前后/回调/timer时间、
每request新baseline；timer核对当前phase原deadline，未到仅重挂。底层错误
恢复前锁内保存固定10字段只读现场，timer错误先失效再snapshot/abort；日志
标明before-restore/saved，未建session或restore本身失败现场不冒充恢复前。
WD经SweetPmicPower语义接口固定7字段取证，terminal后先尝试禁主路再读；
prepare/pet命令和软件plugin起点有时间，hardware-start明确unproven。

保留100ms/23秒/180秒、原IRQ-only和运行互锁；无重试/延长期限/新增pet/启充，
不认为防御timer修正已证明修好实机故障。LG参考仍是plugin使能和bark IRQ pet，
没有软件plugin epoch等于硬件计时零点证据。长期许可/外部SMB1355 I²C隔离/
最终managed-watchdog授权仍缺。

验证底稿scratch/battery-irq-timing：strict apply+byte compare33；原106源码
gate、CI字节未改、同核profiles0/1/2/4/7与runtime1、旧0120a13 marker拒绝通过；
新增日志最大222字节。ASAN/UBSAN transport131、mux66+80竞争（含trace并发200
IRQ）、runtime全部、ADCclient19+100竞争、日志等级通过；7文件ARM64单编译
通过（非完整云链接）。新patch/hash与结果见integration.json，scratch不入git。

NEXT：用户push本轮0166提交（不代用户push）。用户下载新云产物后
先验证真实源码/内核再打包--battery-dependencies五档；优先identity1，桌面尽快
拔5秒→稳定电源≥60秒→拔5秒，若未复现可延长至旧故障时长，导出持久日志；
再按新时序证据安排watchdog4/组合7。无需重测0120a13/baseline/独立inhibit。
本轮提交题目为 Add bounded battery IRQ timing diagnostics and guard ADC deadlines；
提交号见git log -1。无新boot路径，不能重贴旧kernel当新版本。

### Previous 0120a13 combined log handoff (superseded next action)

**CURRENT: 0120a13 deps-all7未通过：ADC19第二阶段超时；首bark晚于23秒期待。四档日志分析已完成，本批不再重刷，下一步定位两项时序。**

上一文档c194b5c，实现8d6435b、打包ba5143e。本轮只读日志/云源码与更新文档，
未改驱动/CI、未push/操作设备/重打包，无新驱动提交需要云编译。日志
logs/sweetlog-0120a13-battery-deps-all.bin，SHA256
68213bfedc94ac51b53e8c83a825c54bb79bbda0361d6f4d9e4009c78ff77c95。
SWEETLOG v1/4MiB/有效460149/flush26/5879行，无回卷/NUL；308连续采样、289.737秒
（30irq含初始化/arm-sync、278保护）。详细证据/边界在
 docs/BATTERY-DEPENDENCY-TESTS.md末节；NEXT/实施计划/ADC5-CONSUMER/INPUT-SESSION同步。

deps7/runtime1，battery1ff，新遥测main-enable0/SMB08/select02/WD62(maskc2=42)/
timef3。身份token1/17/18 valid1/profile1，95057/94786/94786Ω、限值2800/4430；
31FG tuple均match1/13/4027。16后续PARALLEL均08/02/error0，17CONFIG均enable0、
写后62/f3保持，无CONTROL。只证明启动配置相容和主PMIC输出禁止，不代表外部隔离。

ADC19请求/18成功（3BATID/15USBIN），34consumer EOC/35mux hit，18全部valid/
restored/接受、无epoch丢弃，总耗时7.549～48.252ms。19首EOC MORE/phase3/count1后
RESULT timer超时，稍后mux记录第二hit（两次irq/work39）；restore/cleanup0，
provider停机，身份/token/限值归零。与watchdog4的ADC51时序一致，仍无精确
trigger/GIC/mux scan+notify/consumer/锁前后时间戳，不能据行序认定锁或硬件根因。

watchdog只有初始化pet，**没有自然bark pet**。稳定plugin id106完成124.219秒，
23秒等待到id131/147.490秒失败终止，error-100 watchdog bark missing/late。
首bark id136完成152.072秒（比plugin27.853秒、比失败4.582秒）。后续12bark全
healthy交付、不再pet/无隐式重启，主路保持enable0；没有window-complete。
故障后间隔约8.19/16.05交替，不能算健康维护或bite保护验收。第一bark晚到已
证实，原因未定：软件plugin epoch不等于已验证的PMIC计时启动点；不能认定
三项mux冲突，不能直接放宽23秒。单独watchdog4正常维护/有限停止通过仍有效，
但不得推广成组合或正式managed授权通过。

CC id106/108/109保留原来源，但ADC已失效，无新VBUS/正permit，长期许可/有效
许可内撤销未验收。28 IRQ refresh全healthy，89.560～117.417ms；三键配对、
touch151/empty0，无panic或mux失败/隔离。UV5/OV未命中；ACM configured0、无USB
warning，USB枚举本档无验证证据，不据此判正常或失效。底稿scratch/artifact-0120a13：
battery-deps-all-persistent.txt、analyze-deps-all.py、deps-all-log-analysis.json、
audit-deps-all.py、deps-all-verification.json。

NEXT：不再让用户重刷本批四档或追加baseline/evidence0。下一实现先定位ADC各
阶段/事件投递/锁等待并补有界时间/故障快照；并核对LG/PMIC watchdog的配置、pet、
首次真实plugin/掉线抖动与首bark起算关系。取证在typed ADC/PMIC后端及mux各自
边界，保持GIC→mux IOService→registry client→consumer workloop，不让高层穿透
读寄存器，不用轮询/自动重试/单纯延长deadline掩盖。新版本具体实现/验证完成
再通知用户push；本轮无需新编译。后续仍需ADC可靠性与长期许可、外部SMB1355
I²C隔离、最终managed-watchdog/保护验收。所有0120a13档均未主动启充。

### Previous 0120a13 watchdog log handoff (superseded next action)

**CURRENT: 0120a13 watchdog4自然bark维护/新配置/有限停止通过；ADC51第二阶段超时。下一项deps-all7仅测并存/撤销，之后优先修ADC时序。**

上一文档727c15d，实现8d6435b，打包ba5143e；本轮只读日志/实际云源码与更新
文档，未改驱动/CI、未push/操作设备或重新打包。日志
logs/sweetlog-0120a13-battery-watchdog.bin，SHA256
47eca1cdc7c6ab4dd8b1730afe585b08e2734bb56609cb42fc3e0081ce2987f3。
SWEETLOG v1/4MiB/有效441261/flush20/5750行，无回卷/NUL；246笔采样、229.202秒
（26irq含初始化/arm-sync、220保护）。完整证据/边界见
 docs/BATTERY-DEPENDENCY-TESTS.md末节；NEXT/实施计划/ADC5-CONSUMER/INPUT-SESSION同步。

deps4/runtime1、battery mask1ff，bark APID272/acc0/status0；新遥测main-enable0/
WD62(maskc2=42)/timef3，16笔CONFIG均enable0、写后均62/f3/error0，无CONTROL。
9自然bark hit/9 events100 healthy刷新，前8次一一对应pet2..9/pulse-completed1/
main-enable0/rt0/error0；初始化算入共9pet。rt0为mux ACK后采样，非未发生bark。
采样完成uptime72.442/88.818/141.232/160.229/176.606/192.984/209.362/225.739秒；
稳定段约16.377～16.378秒，另含跨拔插52.414秒及18.997秒间隔，不能说每笔16秒。
180秒窗口约236.531秒正常终止pets9/disable-error0；其后第9bark id231约242.116秒
仍IRQ采样、无第10pet。无DEPENDENCIES FAILED/配置丢失，诊断正常/有限停止通过，
非带电bite禁充/深睡恢复/最终managed-watchdog授权全通过。

ADC51请求/50成功（3BATID/47USBIN），98consumer EOC/99mux EOC hit；50全部
valid/restored/接受，无epoch丢弃，总耗时7.478～48.287ms。51先有首EOC MORE/
phase3/count1，后RESULT phase3 timer超时，之后mux才记录第二hit（两次irq/work104），
随后FAILURE error-101、restore/cleanup0，停provider并撤销测量，无第二成功EOC。
这里hit打印在routeNotify之后，行序不是精确硬件/投递时间；缺trigger/GIC/mux
scan+notify/consumer入口/锁前后时间戳，不能认定硬件慢或锁根因。三档故障汇总：
identity238未见首EOC、inhibit68收到首EOC但advance过期、watchdog51第二阶段超时/
稍后hit。后续先补typed ADC/mux边界有界取证，保留IRQ-only/失败撤销，不用轮询/
自动重试/简单延长deadline掩盖。ADC失败后watchdog仍自然维护，符合独立诊断。

CC id110/112/113保留原来源时间戳，但在ADC失败后，无新VBUS/正permit，长期
许可/有效许可内撤销未验收。24 IRQ refresh全healthy，89.839～115.977ms；三键
配对down/up、touch177/empty0、ACM configured2，无panic或mux失败/隔离；UV5/
OV未命中，3条USB EP0/EP5 warning保留旧枚举问题。底稿scratch/artifact-0120a13：
battery-watchdog-persistent.txt、analyze-watchdog.py、watchdog-log-analysis.json、
audit-watchdog.py、watchdog-verification.json。

NEXT：用户fastboot boot
/home/shenli/work1/out-bs/boot-16k-0120a13-battery-deps-all.img。
桌面后尽快拔5秒→接同一稳定电源至少60秒→拔5秒；确认三键/触摸，导出
sweetlog-0120a13-battery-deps-all.bin（持久日志即可）。诊断从驱动准备起180秒，
不要在桌面等几分钟。最后组合只增加身份/SMB08/bark pet并存与ADC故障后撤销
覆盖，不能叫完整验收。无需新编译/baseline、不启充。之后优先修ADC时序及补
长期输入许可；外部SMB1355 I²C隔离、最终managed-watchdog/保护验收仍待完成。

### Previous 0120a13 inhibit log handoff (superseded next action)

**CURRENT: 0120a13 inhibit2主PMIC输出禁止通过；ADC68收到EOC但处理过期，根因仍待定位。下一项独立watchdog4。**

上一文档3f08ae2，实现8d6435b，打包ba5143e。本轮只读日志/实际云源码和更新
文档，未改驱动/CI、未push/操作设备/重打包。日志
logs/sweetlog-0120a13-battery-inhibit.bin，SHA256
07f0c0a427ae5f038a640c493183de3b97b1d222a3f17e544cfc77ef25c2f60e。
SWEETLOG v1/4MiB/有效476631/flush25/6154行，无回卷/NUL；291连续采样、
279.991秒（22irq含初始化/arm-sync、269保护）。完整证据在
 docs/BATTERY-DEPENDENCY-TESTS.md末节；NEXT/实施计划/ADC5-CONSUMER/INPUT-SESSION同步。

deps2/runtime1，初始SMB0→INHIBIT命令接受→新遥测id31 main-enable0/smb08/
select02。20笔后续PARALLEL均08/02/error0，覆盖插拔/ADC故障/180秒窗口结束，
最后id285 uptime297.923秒仍保持。21笔CONFIG均enable0，无CONTROL。
180秒窗口正常结束pets0/disable-error0，无DEPENDENCIES FAILED或禁止位丢失。
仅主PMIC输出禁止通过，外部SMB1355 I²C状态仍缺，parallelVerified仍false。

ADC68请求、67成功（3BATID/64USBIN）、132EOC；67全部valid/restored/接受，
无epoch丢弃，成功总耗时7.486～51.881ms。68/USBIN从WAIT_SETTLE开始，
mux irq137明确投递adc5-eoc route-hits132，随后consumer EOC68返回-101/
phase5/count0，restore-error0/cleanup0，provider停机/测量撤销。
没有timer TIMEOUT行。源码advance先获取共享SPMI锁再查deadline，count0说明
首EOC尚未接受已过期；附近电池id121采样99.935ms。调度/锁等待是调查方向，
没有各层时间戳，不能认定根因；不能声称这次没有硬件EOC，也不能倒推身份档同因。
后续补typed ADC及mux边界故障快照/trigger→IRQ→queue→callback→lock时序，
保留IRQ-only、失败撤销，不用轮询/重试/单纯延长截止掩盖。

CC id127/128/130/131在重复source后保留原时间戳，但在ADC故障后，无新VBUS/
正permit，长期许可与有效许可内撤销仍未验收。20 IRQ refresh均healthy，
99.871～108.922ms；三键配对down/up、touch83/empty0、ACM configured2，
无panic或mux失败/隔离；UV6/OV未命中，5条USB EP0/EP5 warning保留旧枚举问题。
底稿scratch/artifact-0120a13：battery-inhibit-persistent.txt、analyze-inhibit.py、
inhibit-log-analysis.json、audit-inhibit.py、inhibit-verification.json。

NEXT：用户fastboot boot
/home/shenli/work1/out-bs/boot-16k-0120a13-battery-watchdog.img。
桌面后尽快拔线5秒→接同一稳定电源至少60秒→拔线5秒；确认三键/触摸，导出
sweetlog-0120a13-battery-watchdog.bin（持久日志即可）。诊断从驱动准备起180秒，
不要在桌面先等几分钟。查bark路由attach/healthy、f3与maskc2=42新遥测、至少
两次自然bark+pet；初次pet/窗口结束不能算维护通过。无需push/编译/baseline，
仍不启充。组合7、ADC可靠性/长期许可、外部SMB1355 I²C隔离、正式managed-
watchdog授权/保护验收仍待完成。

### Previous 0120a13 identity log handoff (superseded next action)

**CURRENT: 0120a13身份档日志已分析。身份建立/刷新与失败撤销通过；ADC238缺首个EOC，长期输入许可未覆盖。下一项独立inhibit2。**

上一打包/文档提交ba5143e，实现8d6435b；本轮只读日志/实际云源码及更新文档，
未改驱动/CI、未push/操作设备或重打包。日志
logs/sweetlog-0120a13-battery-identity.bin，SHA256
9ce9fabcad010e21bb0bf16be8a0253d1e0316c8f33c6a66613a62ab9d3587a4。
SWEETLOG v1/4MiB/有效901517/flush67/11510行，无回卷/NUL；约700.009秒、
711连续采样（39irq含初始化与arm-sync/672保护）。完整证据及边界在
 docs/BATTERY-DEPENDENCY-TESTS.md末节；INPUT-SESSION、ADC5-CONSUMER、NEXT、
实施计划已同步本轮结论。

8次BAT_ID身份有效刷新token1/31/61/91/121/151/181/211，889359～889814uV，
LG公式均95057Ω/profile1/FCC2800/FV4430；58次FG tuple全部match1/13/4027。
238请求中237成功（8BATID/229USBIN），466EOC，全部valid/restored/接受，无丢弃，
成功总耗时7.488～49.756ms。238/channel2在WAIT_SETTLE后GROUND/phase2超时，
无该请求EOC，error-101、restore-error0/cleanup0，provider停止，身份/限值立即
归零，此后无请求/结果。故障uptime295.883～296.885秒，首拔线305.366秒；
全部Rp3A分类在故障后，无正permit/lease，所以不能验收长期许可/有效许可内撤销。

0164来源保留已有两组部分实证：id523/524/526/527和545/546/548/549均CC
qualified1/coherent1/reasons0，重复source保持原CC时间戳；没有复现1.3秒误删。
但无新VBUS许可，不能把此部分直接升级为长期许可通过。38CONFIG均enable0、
无CONTROL；37次IRQ refresh全healthy，route→publish100.973～104.479ms；UV12，
OV未命中。三键配对down/up，touch310/empty0，ACM configured2；无panic或共享
mux失败/隔离，5条USB EP0/EP5 warning保留旧重复枚举缺陷。

本轮缺的是USBIN第一阶段REF_GND EOC，不等同历史第二EOC超时。当前没有故障
当时ADC状态/PMIC IRQ latch+enable/arbiter pending及mux排队快照，根因未定；
不能断言是插拔/身份刷新/某个mux ACK竞态。后续在typed后端及mux各自边界补
一次性故障快照/阶段时间取证，再定位修复，保留IRQ-only与失败撤销，不能用轮询/
自动重试或延长deadline掩盖。底稿scratch/artifact-0120a13：
battery-identity-persistent.txt、analyze-log.py、log-analysis.json、audit-identity.py、
identity-verification.json；分析显式计入失败请求238，不套旧3正permit断言。

NEXT：用户fastboot boot
/home/shenli/work1/out-bs/boot-16k-0120a13-battery-inhibit.img，桌面后尽快拔5秒、
接同一稳定电源20～30秒再拔，确认三键/触摸，导出
sweetlog-0120a13-battery-inhibit.bin（持久日志即可）。这项不依赖身份/输入许可，
检查main-enable0、INHIBIT接受、新遥测SMB mask18为08并保持；external parallel
仍false、不启充。无需push/新编译/baseline；下一步再按日志推进watchdog4。
组合7与完整验收仍待ADC可靠性/长期许可、外部SMB1355 I²C隔离和managed-watchdog
正式授权/保护；不要把本轮失败当身份正常路径失败，也不要当全项通过。

### Previous 0120a13 packaging handoff (superseded next action)

**CURRENT: 0120a13云产物已核对，五档boot已生成并回读验证，待第一项identity实机日志。**

实现8d6435b、上一交接fe21cca。实际0120a13的33个选定源码文件与
scratch/battery-dependencies/verify/xnu逐字节一致，原106项源码gate和内核
manifest/0164+0165 marker通过。未提供本轮构建日志或CI run ID，不能据此称所有
云编译warning已排查。本轮未改驱动/CI，未push或操作设备；实机尚未验收。

输入产物在工作区logs：xnu-buildtree-0120a13.gz SHA256
 e383562860e87913d89cf1dfae10ce96f34128be4c972660f662501fb72fa12b；
xnu-kernel-0120a13.gz SHA256
 5bfa27a403b5dda60887f2a35cb47efd5abdd9d5bc1b2c05f7614ce0f01ab0e2；
实际kernel SHA256 1e66616567862cf52c3eb67ba0aa1feb27b3d4018b51b47d00578e5fe3b3dc24。

已运行tools/mk-mh2lm-event-matrix.sh的--battery-dependencies，五个boot均在
/home/shenli/work1/out-bs/，每份35,667,968字节，文件前缀boot-16k-0120a13-：
battery-evidence.img(deps0)、battery-identity.img(1)、battery-inhibit.img(2)、
battery-watchdog.img(4)、battery-deps-all.img(7)。全部runtime1/IRQ4/ADCconv0，
保留USB日志和三键/DWC3/触摸IRQ，不启充。boot header v1、外层firmware/ramdisk
切片与FAT内kernel/ADT/TOML回读通过，同kernel/16K ADT，参数无重复且除deps及
菜单标题外一致。全部SHA与路径见docs/BATTERY-DEPENDENCY-TESTS.md末节。
第一项identity SHA256 db6368d1490bd31e8ecb1bc08703d174a0eb6ed6d7150e444fce9a0daec58f4a。

底稿scratch/artifact-0120a13/：verify-artifacts.py、source-compare.json、
verification.json、packaging.txt、verify-boots.py、boot-verification.json及五份
回读TOML。完整本地实现验证仍见下方0165历史交接，不重复算本轮重跑。

NEXT：用户fastboot boot
/home/shenli/work1/out-bs/boot-16k-0120a13-battery-identity.img。
桌面约15秒→拔线5秒→同一充电器/线材稳定45～60秒→拔5秒→接1～2秒再拔→
稳定接15秒再拔；确认三键/触摸。导出sweetlog-0120a13-battery-identity.bin，
持久日志即可。检查身份约95kΩ/profile1/板级上界及0164重复source后长期输入
许可恢复、来源不续期、同epoch新VBUS和有效许可内拔线撤销。先分析此日志，
再依次inhibit2/watchdog4，分项核对后组合7；默认无需evidence0/baseline重测。
watchdog诊断从驱动准备起180秒，届时桌面后须尽快拔5秒再接稳定至少50秒。
仍待外部SMB1355 I²C隔离、正式managed-watchdog授权/保护验收，所有镜像不启充。

### Previous 0165 implementation handoff (superseded next action)

**CURRENT: 按用户新要求，0165后三项依赖已同轮推进，和0164一起待用户push/云编译。**

实现/回归/文档提交8d6435b；当前main已含0164和0165，尚未push。

用户明确：不必等长期输入许可验收才开发身份/parallel/watchdog，下一轮同内核分步
测试。新0165-mh2lm-battery-dependency-probes.patch基于0164验证树，架构仍GIC→mux
IOService→registry client→SweetBattery自己的workloop；没有新增全局桥接、匹配回调
重SPMI、高层寄存器写、轮询IRQ fallback或真正启充。所有新档强制runtime1，deps非0
与runtime2/3组合会显式拒绝，参数只能请求工作，不能强行把硬件依据设true。

sweetbattdeps位：1身份、2主PMIC副路禁止、4watchdog诊断；默认0。身份将现有ADC
BAT_ID（live/token/60秒）和固定FG profile tuple（前缀24/integrity/nominal/60秒）
交叉核对，复现LG1825mV整数公式及91/12kΩ最近15%匹配，无默认profile fallback。
通过后batteryVerified及板级maxFCC2800mA/maxFV4430mV可用；原试验仍500mA/4200mV。
batteryUntilNs保存BAT_ID/profile最早到期点，并收紧策略/命令期限，不能用新策略
核对时间延长原身份依据的寿命；本地测试包含两次保护采样之间跨过该期限的拒绝。
身份档30秒保护遥测刷新profile，不续输入来源时间，不改ADC provider/client接口。

并联档先禁主路，再语义PARALLEL_INHIBIT使SID2:1648 mask18为08，仅强制SMB_EN
低，保留CP/STAT位，记录1690选择但不写。命令即时回读与写后新遥测分别确认。
外部SMB1355仍缺SE4 I²C typed provider及CHG_EN_SRC/polarity/actual状态依据，
parallelVerified始终false。不能把主PMIC输出禁止当外部充电芯片已隔离。

watchdog档同样先禁主路。mux表尾新增index18 SID2/PID16/bit1 rising bark路由，
19路/3组，WDOG consumer bit100、合法ALL1ff；默认实际订阅仍ff，只有deps含4才1ff。
保留唯一owner发现、已启用位拒绝接管、按组回滚/隔离。语义WATCHDOG_PREPARE固定
1653=f3（snarl8s/bark16s/bite8s/bite禁充），1651 maskc2为42（plugin+bark，AFP按LG
主路初始化关闭，保留其他位）。WATCHDOG_PET走专用固定1643 bit0脉冲，transport锁/
唯一EE0/v5/timeout poison，无RMW/readback/相同值跳过/重试；成功只算总线命令完成。
初次pet后仅真实bark事件pet，不是定时读状态或定时喂狗。现有策略watchdog授权
仍只支持off，尚未授予managed许可，必须实机验证后再接正式控制，不开enable1。

写后必须等snapshot.start>=配置完成的新遥测；中间ADC通知等待，不能拿旧快照误报。
新遥测缺失/过3秒、main-enable变化、禁止位/WD配置读错或丢失、mux丢失均ERROR
终止并尽力禁主路，无自动重试/重启。继承插线不证明plugin计时器已启动，只有
配置后新plugin采集或真实bark才建立23秒无中断期限；source/ADC/保护tick不续期。
诊断从准备最多180秒，结束再次禁主路并停止pet；不恢复未知enable/副路/WD配置，
保持bite禁充。priority sleep/shutdown与诊断提交用同一control锁，停止后不再pet。
初次pet1或window complete但无自然bark不能算硬件验收。息屏非系统深睡，恢复待办。

工具--battery-dependencies复用实际同一云kernel，生成battery-evidence(deps0)、
battery-identity(1)、battery-inhibit(2)、battery-watchdog(4)、battery-deps-all(7)。
均runtime1/IRQ4/ADCconv0，保留USB日志和三键/DWC3/触摸IRQ。旧evidence档仍保留，
不需要默认再测一次baseline。打包器要求0164/0165 marker，旧0120a12被拒绝。
CircleCI/.github/workflows/xnu.yml逐字节未改，未push/操作设备，无新boot/云run ID。

本地底稿scratch/battery-dependencies：实际0120a12的33文件依次严格apply0164+0165
并与edited/verify逐字节一致，原106源码gate通过；ASAN/UBSAN runtime11、transport123、
mux70 PASS（包括新bark交付/外部owner/已有enable拒绝/读错回滚、按键并行与原80次
退出竞态），日志等级1 PASS。runtime新增各准备操作故障、真实配置/mux丢失、新快照/
旧回调时序、继承插线/source不伪造中断期限、180秒终止与20次pet/priority停止竞态。
五个ARM64单文件编译通过：SweetBattery/SweetPmicPower/SweetSpmiInterruptMux/
sweet_spmi/sweet_charger，Mach-O arm64对象，有warnings，非完整macOS内核链接。
实际packager分档分支参数唯一性/五组mask、bash语法、diff检查及旧内核拒绝通过；
未重跑未改动的ADC client独立测试，不将历史通过算本轮重跑。integration.json、
final-validation.json、*-tests.txt及*.compile.txt保存本地证据。0165 patch SHA256
fc4896cc0727b4896dc83604087ccf5cc3bb4e830f2d99c0b8006e74277dc21c。
完整契约/LG符号/验收步骤在docs/BATTERY-DEPENDENCY-TESTS.md；NEXT、实施计划、
输入会话/ADC/硬件接口/当前架构已更新。下面0164结果与历史硬件缺口仍有效。

NEXT：用户push当前main并下载新产物。核对0164+0165源码/marker，使用新的
--battery-dependencies打包五档并回读payload/参数。先identity1：沿用45～60秒稳定
电源+拔线/短接，同时验0164重复source后恢复、>3秒原来源不续、同epoch新VBUS与
有效许可内拔线撤销，并看身份约95kΩ/profile1/板级上界。再inhibit2：桌面后尽快
插拔并稳定20～30秒，查enable0和新遥测SMB输出禁止；再watchdog4：桌面后尽快
拔5秒再接稳定电源至少50秒，取得至少两次自然bark/pet及配置持续相符。180秒从
驱动准备开始，过时需明确日志，不能插拔自动重启。各档日志检查后再deps-all7验证
并存；持久日志即可，不重测0120a12/baseline/ADC bring-up。所有档不启充，外部
SMB1355 I²C隔离、正式managed-watchdog/保护验收和0164输入长期实测仍未闭合。

### Previous 0164 implementation handoff (superseded next action)

**CURRENT: 0120a12日志已分析；0164修复重复source排队误删CC来源，已本地验证，待用户push。**

实现/回归/文档提交e73cf94，当前main包含该修复；未push。下一动作是用户push后云编译。

日志logs/sweetlog-0120a12-battery-evidence.bin，SHA256
d0be98dae2a0f39f7733053f95ecb4259d638f6f5d8df9224bcc691401a9c5c4。SWEETLOG v1/
4MiB/有效959950/flush49/12437行，无回卷/NUL。底稿scratch/artifact-0120a12/
battery-evidence-persistent.txt、analyze-log.py、log-analysis.json、audit-permits.py、
permit-verification.json。约519.191秒、546连续采样（47irq含初始/arm-sync，499保护），
45运行期IRQ刷新全healthy，route→publish89.106～119.658ms。UV/OV都attach APID269、
battery mask ff；UV14、USBIN14、source7，Type-C rid27/cc15/vbus14/attach12/legacy6，
MSOC1。OV、CHGR/VCONN/READY无自然命中，不能冒充完整保护验收。id435/520 UV-only
事件可发布断线，但不是稳定带电欠压验收。

ADC434请求/434有效且恢复结果/851 EOC，17BATID/417USBIN，424接受、10因epoch变更
正确丢弃(error0)；BATID888581～889749uV、USBIN4548784～5107264uV。总耗时7.503438～
205.157344ms，不代替阶段deadline；旧第二EOC超时未复现但未修。47profile全match1/
integrity13/4027mAh；18次UI发布updateStatus完成，59%/charging0，无屏幕视觉描述。

关键失败：仅id217/429/450取得kind2/max3000的正permit，session6/11/16、epoch11/23/37、
token170/342/349、VBUS5082/5083/5081mV。随后id219/431/452首个source采样tuple保持一致，
CC年龄仅1.350070/1.356721/1.339414秒、coherent1/reasons0，但采样中又排进source，
0163清除ccFromEvent/apsdFromSource，sb_input_observe再清会话。下笔220/432/453只有
source/FLOAT，不能重建CC；原CC时间清零。最终epoch仍有163/2/66笔新电压也不恢复。
长期许可未通过；后续拔线前许可早已撤销，不能算有效许可内拔线撤销通过。

0164仅修改SweetBattery::sampleRuntime：保留一致来源历史，以原client与battery两段
pending检查暂停许可。下次真实输入采集递增epoch、得到同epoch新VBUS后才恢复；
真实断线/读错/角色或Rp变化/QC/UV/OV仍由sb_note_input及来源匹配撤销。新增INFO
INPUT QUEUE v1诊断。没有放宽时效/来源、轮询fallback、自动rerun或新控制写，架构
仍GIC→mux IOService→registry client→consumer loop；按键/USB/触摸/ADC实现未改。

本地scratch/battery-input-queue：实际0120a12的33文件base，0164严格apply并与edited/
verify逐字节一致，原106 gate通过。新回归在旧代码稳定失败，新代码ASAN/UBSAN
runtime9及mux66 PASS，SweetBattery ARM64单文件编译通过（有warnings，非全核链接）。
涵盖采样/保护tick撞重复source、两段pending阻断命令、新epoch新电压、>3秒来源不续、
七类真实故障撤销及缺依赖无控制写。未重跑未改动的transport/ADC client测试。底稿
0163-reproduction.txt、battery-runtime-tests.txt、battery-irq-tests.txt、SweetBattery.compile.txt、
integration.json、final-validation.json。patch SHA256
1e4345596ab1f458cb9bc28d2aa16cdb7c2ed894b7e7128078ef7105d1faa697。

45 CONFIG全enable0，无CONTROL/allow/owned；身份限值、parallel隔离/watchdog仍阻断，
watchdog raw62/time8c。无ERROR/panic/mux隔离；power/音量加有down/up，音量减无本轮
证据，触摸39/empty1。两次ACM configured，后段EP0 warning/reset保留重复枚举旧bug。
本地打包器新增0164 marker，实际旧0120a12内核已拒绝。CircleCI和GitHub XNU配置未改；
未push/操作设备，无新boot/CI run ID/0164实机结果。文档INPUT-SESSION/NEXT/实施计划/
CC/ADC/硬件接口/当前架构已更新，完整证据以BATTERY-INPUT-SESSION.md末节为准。

NEXT：用户push当前main，下载新云产物后核对0164源码/marker，原--battery-evidence
打包单镜像runtime1/IRQ4/ADCconv0，保留USB日志和三键/USB/触摸IRQ。不重测0120a12/
baseline/dry或ADC正常bring-up：电脑桌面15秒→拔5秒→原电源稳定45～60秒→拔5秒→
原电源接1～2秒再拔→稳定接15秒再拔；确认三键/触摸，导出持久日志。验证重复source
后来源保留、队列清空并取得新epoch电压后恢复、>3秒来源不续、有效许可期间拔线撤销。
短接无许可不能冒充撤销覆盖。仍不启充，输入通过后再闭合身份/parallel/watchdog。

### Previous 0120a12 packaging handoff (superseded)

**CURRENT: 0120a12云产物核对完成，0163输入会话观察boot已打包，待实机日志。**

驱动提交7b7da93、实现交接0ab66ae。实际0120a12 buildtree的33个选定源码文件
与scratch/battery-input-session/verify/xnu逐字节一致，原106个CI源码gate通过。
内核manifest有效，INPUT SESSION/PERMIT/LEASE、ADC INPUT v1、两条USBIN UV/OV
及既有CC/APSD/mux/三键/DWC3/触摸/ACM标记完整。未提供CI run ID或新构建日志，
没有因此宣称所有构建warning已排查；实际kernel和源码核对通过。

产物SHA256：buildtree 09e47b9329f0d01f0785be6fa2a3ba4c58bdcb03726f3580de30805549db83df；
kernel包 ad17d450a4773b175f38e66674cc9998cd50cb82978d2b319680783f08824919；
实际kernel 4402489776e4dfb0eb42ff9247972465269c516fd60cc13c65c2822e36f72ac6。
镜像out-bs/boot-16k-0120a12-battery-evidence.img，35,667,968字节，SHA256
038b794b93a2747030e43e21346ac0c94c1349abe07a9ce2964a3cf2bfabecdb。

原--battery-evidence参数：runtime1/IRQ4/ADCconv0，保留USB日志和三键/DWC3/
触摸IRQ、framebuffer30Hz；不启充。boot header v1，firmware/ramdisk切片、FAT
回读kernel/ADT/TOML全部一致，参数无重复。底稿scratch/artifact-0120a12：
verify-artifacts.py、source-compare.json、verification.json、packaging.txt、
verify-boot.py、boot-verification.json及最终TOML。本轮只核对/打包/文档，无新
驱动或CI修改，未push/操作设备，无0120a12实机结果。

NEXT：用户fastboot boot上述镜像。不重测baseline/dry：电脑桌面15秒→拔5秒→
0120a11同一电源/线材稳定45～60秒→拔5秒→接该电源1～2秒再拔→再次稳定接
15秒再拔。确认三键/触摸及UI拔插变化，导出sweetlog-0120a12-battery-evidence.bin，
持久日志即可。关注battery mask ff、UV/OV route attach/healthy，CC/APSD原始
时间不续、>3秒后新ADC token/同inputEpoch的INPUT PERMIT仍有效及拔线撤销。
短接没建立permit时不算有效许可内撤销覆盖；OV/UV无自然命中也不当保护验收。
电脑SDP/default/debug附件仍可无许可。原enable0时charging预期0：身份限值、
parallel隔离/watchdog仍未闭合，不能把输入许可成功当真实充电成功。旧ADC第二
EOC超时取证、USB重复枚举问题及完整协议/恢复缺口保持待办。详细接口、LG依据
和本地结果见docs/BATTERY-INPUT-SESSION.md；下一动作以上方为准。

### Previous 0163 implementation handoff (superseded)

**CURRENT: 0163输入会话与同epoch VBUS许可已本地完成，提交7b7da93；待用户push。**

基于实际0120a11的33文件源码，新增0163-mh2lm-input-session-permits.patch。
SweetBattery自己的workloop持有纯SweetBattInputSession；只在输入采集时建立/
重验CC/APSD来源。SweetBattInputPermit组合该来源与精确输入inputEpoch的ADC
VBUS：4.5～5.5V、采样开始严格小于3秒、不能早于输入采集完成。原3秒CC/APSD
诊断保留，长期会话不再靠反复读取续来源时间。中/高Rp优先，APSD未完成/FLOAT
可与CC并存；正式许可需APSD元数据完整一致以排除QC，default/SDP/FLOAT单独
不授予许可。仍无PD/QC/DPDM完整协议，不将Rp3A当PD合同或实际取电3A。

ADC client update改为外部uint64 inputEpoch，copyFreshInput核对请求/结果/当前
输入epoch以及原token/通道/内部generation/时效/服务存活；旧转换不能重新贴
新epoch标签。USBIN从采样开始满1秒安排下一笔，给3秒许可留余量，迟调度/故障
仍到期撤销；BATID周期未改。新增ADC INPUT v1 token/epoch日志，原sample格式
保留。这是电压保护转换，不是定时读取输入状态或轮询EOC。

mux表追加USBIN UV bit2/OV bit3双边沿（LG pm8150b DTS），18路、battery mask ff，
runtime才订阅。仍从元数据发现owner/APID，不硬编码269，不接管已使能位；
沿用按组回滚/隔离。新增client.hasPendingEvents只读fence，覆盖mux已排入client
但尚未送达battery回调的队列及provider消失；再结合battery fIrqPending。策略/
每个非禁充语义命令都阻断pending/失效ADC/过期permit，安全禁充仍可执行。
策略撤销时不篡改snapshot来源时间，采样期间撞到新事件则拒绝本笔来源。
修正撤销后批准ICL=0误触永久HW fault的问题：撤销先禁充，不用无效上界与旧
设置比较；独立硬件故障检查保留。恢复边沿不能伪造CC/APSD来源；丢来源后等
下一次合格plugin/Type-C/source，不自动rerun/retry。

SweetBattLimits改为typed input许可；batteryVerified/parallelVerified和电池限值
仍缺，verifiedNs仅为限值时效，其失败归SB_R_LIMITS。watchdog仍按原只接受已
证明关闭的状态检查，未改配置；本轮无真实启充，不把input.valid等同全依赖完成。
GIC→mux IOService→registry client→consumer loop边界保留；无新全局桥接、重
matched回调、高层寄存器写、轮询fallback，按键/DWC3/触摸/显示代码未改。

本地验证：33文件严格apply并逐字节一致，原106个源码gate通过；四个ARM64
SweetBattery/Adc5Client/SpmiMuxClient/SpmiInterruptMux编译通过（头文件/格式/
严格诊断和路由聚合初始化warnings，非macOS全核链接）。ASAN/UBSAN runtime8、
mux66、transport122、日志1条PASS，ADC consumer19场景+100通知/终止竞态。
含>3秒稳定会话、新VBUS/旧来源、外部epoch/陈旧转换、两段pending、命令前ADC
失效、源角色/QC/UV/OV/拔线、撤销非虚假HW故障、缺身份/parallel无控制写等。
底稿scratch/battery-input-session/{base,edited,verify}/xnu及final-validation.json、
integration.json、compile/test输出。patch SHA256
0a7cc0526b28fc27bb6bb66e977430b95d02715a08c45fe7793eec1a5b00b3ec。

CircleCI和GitHub XNU配置逐字节未改；新增marker只放本地打包器，旧0120a11/
0162内核已被battery-evidence拒绝。未push/操作设备、无新boot/实机结果或CI
run ID。文档BATTERY-INPUT-SESSION、NEXT、实施计划、CC/ADC/接口/当前架构已
更新，详细LG来源、保护边界和测试流程见docs/BATTERY-INPUT-SESSION.md。

NEXT：用户push当前main并云编译。新产物下载后核对0163源码/33文件/内核marker，
原--battery-evidence打包runtime1/IRQ4/ADCconv0，保留三键/USB/触摸IRQ和USB日志。
不重测baseline/dry档：电脑桌面约15秒→拔5秒→0120a11同一电源稳定45～60秒→
拔5秒→电源接1～2秒再拔→稳定接15秒再拔；检查输入许可在3秒诊断过期后随新
VBUS继续、原source时间不续、旧token/epoch不复用、拔线撤销；短接没有建立
许可则不能算有效许可内撤销通过。导出持久日志即可，USB重复枚举旧bug保留。
两条新保护路由owner/enable/自然交付尚未实机验证，UV/OV无命中不能当验收，
不要求制造过压。原enable0时charging仍预期0；输入通过后继续身份/parallel/
watchdog闭环，ADC旧第二EOC超时取证仍未解决。尚无完整OV debounce/UV storm/
AICL/collapse-VASHDN恢复或系统深睡恢复，不能将本轮当完整充电验收。

### Previous 0120a11 log-analysis handoff (superseded)

**CURRENT: 0120a11日志分析完成；0162 sink CC分类与正常拔线撤销已实机通过。**

日志sweetlog-0120a11-battery-evidence.bin，SHA256
4ec5fbc4c666bbe8fb5cb179e1e0ef7b4fde04c58c97b6d6fb7f00351d2db231；SWEETLOG
v1/4MiB/有效779516/flush58/10086行，无回卷或NUL。分析底稿scratch/artifact-0120a11/
analyze-log.py、log-analysis.json、cc-verification.json及解码文本，原始日志不入git。

关键成功证据：id359 events41先取得Rp3A，sink02/attached23/misc3b/debug0/
legacy4/USB10，CC role1/observed1/reasons0/qualified1，INPUT CAP kind2/max3000；
APSD当时未完成。id361/362 source事件在plugin后1.259/1.371秒返回FLOAT，
CC仍有效。3笔CC session7/start652523091875/end652550295000完全相同，source
没有续CC时间。相比0120a10方向位改变到orientation2，bit4不再否决普通sink。
id535/536拔线USB26，CC session/time全0、qualified0/kind0，UI external/capable0；
回电脑source得到SDP，普通sink许可仍拒绝。8次UI发布均updateStatus完成，
仅内核证据，无用户屏幕描述。拔线晚于3秒观测期，不宣称有效期内拔线竞态已测。

约609.832秒，606连续遥测（20irq含初始/arm-sync、586保护），20profile全
match1/integrity13/4027mAh。19输入全APSD options18/enabled1，3次source完成
为2FLOAT/1SDP；18次运行期IRQ刷新healthy1，rid11/cc8/vbus6/attach5/legacy3/
usbin6/source3自然命中。无VCONN/MSOC/CHGR自然覆盖，不将缺命中当失败。
id351/358 USB字节切换VERIFY/-12后续事件恢复；拔线reasons281c/280c含USBIN
UV是USB26的断线组合，不是已证明稳定输入欠压，VBUS存在时OV/UV保护未覆盖。

ADC226请求/226有效且restored结果/432EOC，20 BATID/206 USBIN，223接受/3因
epoch变化丢弃(error0)。BATID888646..889684uV，USBIN4490656..5104144uV；
请求总耗时7.463..160.536ms，不能单凭总时长判断每阶段deadline。旧第二EOC
超时未复现但未修，仍需故障快照。无[ERROR]/panic/mux隔离；三键都有down/up，
touch至少192。两次ACM configured，后段电脑重连reset无新configured，保留
已知gadget枚举bug。19配置enable0、无CONTROL，charging0，策略allow0/owned0/
reasons7c0，真实充电依赖仍未闭合，CC广播不是PD合同/实际取电电流。

本轮只分析和更新文档，无驱动/CI修改、新boot、push或设备操作，未提供新CI
run ID。BATTERY-TYPEC-CC、BATTERY-DRIVER-NEXT、BATTERY-IMPLEMENTATION-PLAN
已更新；本轮目标完成，无需重跑baseline/当前镜像。

NEXT：正式输入能力管理：组合已验证CC/APSD和同会话有效ADC VBUS，设计typed
授权接口、长期会话、变化/失效撤销和保护事件；不靠定时读续3秒观测、不直接
把kind2转为inputVerified或enable。身份授权/parallel隔离/watchdog仍要补齐，
ADC错误快照取证独立推进。后续保留XNU服务/workloop边界及既有CI不动。

### Previous 0120a11 packaging handoff (superseded)

**CURRENT: 0120a11云产物已核对，0162输入分类测试boot已打包，待实机日志。**

驱动实现40e2e91，CI配置修复2ecfcef，GitHub XNU手动触发c239241。
实际0120a11 buildtree的33文件与battery-cc-lg-sink/verify逐字节一致，106个
现有CI源码gate通过；kernel manifest有效，新增CC ROLE/LG-SMB5分类及既有
CC/APSD/mux/三键/USB/touch/ACM标记完整。buildtree gz SHA256
f13aa3b0b6da1e687f8e4e8ff79b9855dde62121e07a3ee3b5310396310c5da2；kernel gz
3e2c7d106788b5322b9d5042bb560c5364eb448cc28c78b3f66664d83ec024fe；实际kernel
0f9f352da3ffed32066605451ec1f063d1c2add6cc0f7d94fb6f3e58237e2ecc。

镜像out-bs/boot-16k-0120a11-battery-evidence.img，35,667,968字节，SHA256
41b0ceab10ab97f6767e1fd87556b0238387652a15e46bd5a4504aed15b055c5。
原--battery-evidence，runtime1/IRQ4/ADCconv0，保留三种IRQ与USB日志，不启充。
boot header v1 firmware/ramdisk切片与输入一致，回读FAT中kernel/ADT/TOML一致，
参数无重复。底稿scratch/artifact-0120a11，verification.json/source-compare.json/
boot-verification.json及验证脚本、打包记录。本轮只核对/打包/记录，未改驱动或
任何CI配置，未push/操作设备。没有新实机结果，未提供CI run ID或构建日志。

NEXT：用户fastboot boot上述镜像。电脑启动桌面约30秒→拔10秒→当前电源稳定
约30秒→拔10秒→电脑约10秒；确认图标插拔变化及按键/触摸，记录线材和电源。
导出sweetlog-0120a11-battery-evidence.bin，持久日志即可。关注CC role/observed/
reasons/qualified、INPUT CAP及拔线撤销；沿用原电源若仍Rp3A/misc39，应看到
role1/observed1/reasons0和事件有效期内kind2/max3000。misc bit4清除亦正常，
不强求observed1或APSD DCP。3秒观测期限仍在，charging预期0，不是真实启充。
无需重跑baseline或0120a10；长期input provider、ADC故障取证及身份/parallel/
watchdog互锁仍待后续，不以本轮分类成功代替这些依赖。

### Previous CI/manual-trigger handoff (superseded)

用户要求将GitHub Actions的XNU构建改为手动：已删除`.github/workflows/xnu.yml`
的push触发，仅保留workflow_dispatch及原有手动参数；构建job和CircleCI配置
未改。YAML解析与结构比较通过。修改尚未push，推送后生效；未在线禁用整个
workflow、触发构建或取消既有运行。当前电池工作与下一步见下方交接。

**CURRENT: 0162驱动修复保留；撤回40e2e91新增CI检查，恢复原CircleCI配置。**

用户反馈CircleCI配置损坏。40e2e91只改了circleci.yml两条require_marker，
其中字符串含未转义的`1u<<12`/`1u<<13`；CircleCI配置模板阶段会处理`<<`，
shell单引号不能保护它。前一轮只核对源码marker/本地shell与编译，漏掉了
CircleCI模板层校验，这是本次引入的配置错误，不归因于云端环境。

本轮删除这两条新增检查，circleci.yml逐字节恢复为40e2e91父提交a5ee894版本；
驱动0162、host测试和打包器CC ROLE marker保留。检查恢复后所有`<<`仅为原有
合法pipeline.parameters表达式，提取的run命令bash语法通过；没有在线触发/
验证CircleCI，不宣称云端重跑已成功。旧108 gate记录为当时本地源码校验结果，
恢复后CI保持原106 gate。未push，无新boot/设备结果或CI run ID。

后续驱动marker校验放在本地验证/打包器中，不再为此修改既有CircleCI配置。
NEXT：用户push本地CI回退提交后重跑云端构建；得到新产物继续0162镜像核对/
打包与设备测试，步骤见下一段交接。本次没有撤销电池驱动修复。

### Previous 0162 implementation handoff (superseded for CI status)

**CURRENT: 0162按LG SMB5修正sink Rp误拒绝，本地验证完成，待用户push。**

用户要求读LG参考实现并修复0120a10暴露的CC误拒绝。核对LG树d4f335e，
smb5-lib:get_prop_typec_mode/ufp_mode、update_sw_icl_max、attach/state handler、
smb5-reg及LG extension的usbin_ov_check：参考路径先角色后Rp，没有misc bit4
直接否决sink Rp；USBIN OV/UV另为1310 bit3/2。不能把旧SMB2定义搬过来，也不
宣称misc bit4仅source有效或已确定物理成因。

新增补丁0162接0161：SweetBattCc增加role/observed，source不解码残留sink Rp；
misc bit4移到SB_CC_OBS_VBUS_ERROR，原reasons bit7保留不用；独立USBIN OV/UV
采集拒绝位1000/2000。新增INFO CC ROLE及registry role/observed属性。保持
water/VCONN/debug/legacy/attach/debounce/一致性/session/3s期限/pending撤销。
没有新增寄存器访问、IRQ路由或写权限，不清位/改角色/APSD rerun/PD协商，
不修改mux/keys/USB/touch/ADC。OV/UV检查依赖已有输入事件采样，未实现完整
独立OV/UV事件保护或LG迟滞/storm恢复。真实启充和长期input provider仍未完成。

本地：严格apply到实际0120a10源码，33文件与verify一致，108源码gate；
SweetBattery ARM64单文件编译通过（已有工具链/格式/unsafe-buffer warnings，
不等于macOS整核构建）。ASAN/UBSAN runtime6/mux66/transport122/ADCconsumer1/
log1条PASS记录，含实际采样→策略→registry的Rp3A/misc39、OV/UV、source残留、
撤销/时效和缺依赖无控制写。34笔实机快照新旧实际策略离线回放，7笔稳定sink
（221/222/225/226/357/359/360）新规则kind2/max3000，其余拒绝；无法重建未
记录的pending IRQ时序，不是新设备结果。底稿scratch/battery-cc-lg-sink，
integration.json/log-replay.json及各测试/编译输出。测试原始日志不入git。

CI只增2个源码gate，编译命令不变；打包器要求CC ROLE marker，旧0120a10已被
拒绝。文档BATTERY-TYPEC-CC、BATTERY-DRIVER-NEXT更新。未push/操作设备，无新
boot/实机结果或CI run ID；本次无需用户再重测旧镜像/baseline。

NEXT：用户push本地0162提交；下载新kernel/buildtree后核对并用原
--battery-evidence打包runtime1/IRQ4/ADCconv0，保留三种IRQ及USB日志。电脑启动
→拔线→当前电源稳定约30秒→拔线约10秒→电脑，导出持久日志，记录线材/电源。
重点验role1/observed1/reasons0/qualified1、INPUT CAP kind2/max3000及拔线撤销；
APSD仍可FLOAT，charging预期0，3秒后观测到期不续期。之后补长期input/Type-C/
DPDM及保护事件provider，ADC故障取证/identity授权/parallel/watchdog仍需闭合。

### Previous 0120a10 log handoff (superseded)

**CURRENT: 0120a10实机分析完成：0161初始化修复通过，CC输入仍被策略拒绝。**

日志sweetlog-0120a10-battery-evidence.bin，SHA256
8c26b574212b60f1a3dd46011e84449f509c79d1a4b72a2ee22bf890c66783ab，v1/4MiB/
有效896826/flush70/11356行，无回卷/NUL。分析底稿scratch/artifact-0120a10/
analyze-log.py、log-analysis.json。约729.588秒、737连续遥测（irq36/保护701），
36次profile全match1/integrity13/4027mAh。

Type-C六路实际APID271，battery group1/events7f/INTID513 attach成功，IRQ4
armed，34次运行期刷新全healthy1。rid19/cc12/vbus10/attach9/legacy5命中，
usbin10/source5/msoc1；无VCONN自然事件。13次UI发布含external多次撤销/恢复，
已非0120a9冻结；未收到用户屏幕反馈，不扩张为视觉验收。正常运行没有触发
0161 failIrq撤销分支，不宣称该分支已有实机覆盖。

34输入均APSD options18/enabled1，5次source完成为4 FLOAT/1 SDP，无DCP/CDP。
7笔一致Rp3A/misc39（id221/222/225/226/357/359/360）仅CC reasons80，
全qualified0/capability0。当前sb_cc_status以150b bit4直接拒绝。LG smb5-reg.h
确实定义VBUS_ERROR，但smb5-lib的get_prop_typec_mode/ufp_mode没有同样的sink
Rp拒绝；这是当前软件规则与参考实现的差异，不能认定硬件故障或立即删检查。
6笔切换采集因字节变化VERIFY/-12撤销，后续IRQ得到一致结果，没有总线超时。
电脑misc28/debug1/legacy6，亦不当普通sink许可；线材/电源型号尚未提供。

ADC273请求/273有效且restored结果/522EOC，24 BATID/249 USBIN，266接受、
7次因epoch变化丢弃(error0)。BATID888581..889684uV，USBIN4430448..5106224uV。
旧第二EOC超时未复现（本轮请求196也成功），不宣称已修。无[ERROR]/panic/mux
隔离；power/volume-up完整down/up，volume-down无本轮证据；touch至少327。
两次ACM configured，后段电脑reset未再次configured，保留已知gadget枚举bug。
34config全enable0，无CONTROL，UI charging0；runtime1观察没有真实启充。

本轮只分析与更新文档，未改驱动/CI、未push/操作设备，无新boot或CI run ID。
结果docs/BATTERY-TYPEC-CC.md，推进入口docs/BATTERY-DRIVER-NEXT.md。

NEXT：无需重复baseline或本镜像。先离线核对SMB5 bit4在sink/source角色下的
适用范围及保护语义，明确Type-C/DPDM input provider与FLOAT策略接口；静态证据
不足再加入明确typed只读诊断，不盲清错误位/改角色/重跑APSD或放宽许可。ADC
失败快照、identity授权/parallel/watchdog互锁仍待闭合，不跳到真实启充/PD。

### Previous 0120a10 packaging handoff (superseded)

**CURRENT: 0120a10云产物已核对，0161测试boot已打包，待设备日志。**

实现提交1f953e2。0120a10 buildtree的33文件与battery-cc-irq-fix/verify逐字节
一致，106个源码gate通过；kernel manifest有效，含固定IRQ读取错误标记、故障
撤销发布标记，以及CC/Type-C/三键/触摸/USB IRQ/ACM/APSD标记。
buildtree gz SHA256 a1babf9ff1e16b477983904ed43a9111db49d3080badee4169b1ae3f87e5fe72；
kernel gz SHA256 617b81ff9792c1d1820d43adfa045432b6ece104c0be178dbffb949588fc2763；
实际kernel SHA256 a1db9878a0d2f1e10574975bd6c7c62645670c900b1f78d861658cad1de13fa4。

镜像工作区out-bs/boot-16k-0120a10-battery-evidence.img，35,667,968字节，
SHA256 55e35f48f094ad85e9c33355dc2283166eadb52fae737c0db1daeb7caf99eb41。
原--battery-evidence，runtime1/IRQ4/ADCconv0；保留按键/触摸/USB IRQ和USB日志，
不启充。最终boot header v1的firmware/ramdisk切片与输入逐字节一致；从boot内
FAT回读kernel/ADT/TOML匹配，参数无重复。底稿scratch/artifact-0120a10。
本轮只核对/打包/记录，未改驱动/CI、未push或操作设备；没有新实机结果，未提供
本轮CI run ID/构建日志。

NEXT：用户fastboot boot上述镜像。电脑启动桌面约30秒→拔10秒→当前充电器
约30秒→拔10秒→电脑约10秒；确认插拔图标变化及三键/触摸，记录线材A-to-C/
C-to-C与电源型号。导出sweetlog-0120a10-battery-evidence.bin，持久日志即可。
先验battery consumer attach与Type-C路由，再看plugin/CC/APSD/UI记录。若再次
失败直接分析日志，不要求重测旧0120a9/baseline或反复插到枚举成功。真实充电/
PD、完整input provider和ADC第二EOC超时仍未完成。

### Previous 0161 implementation handoff (superseded)

**CURRENT: 0120a9电池consumer初始化失败根因已定；0161修复本地完成，待用户push。**

日志sweetlog-0120a9-battery-evidence.bin，SHA256
4e788faad4f31f902caada05a38085c7586986090176806994b052b640370214，v1/4MiB/
有效273261/flush60/3870行，无回卷或NUL。29组遥测（初始1+保护28，29.219s），
只有初始输入/UI external1 capable1 charging0，config enable0，无CONTROL，
无后续battery IRQ refresh或Type-C hit。用户松线/插稳/拔线图标不消失；
初次视觉变化不能由本日志定位为新IRQ，外接电源缓存确实未撤销。

Type-C元数据解析成功：实际EE0 IRQ-owned APID271；首条battery-typec-rid
configureRoute首读0x1511经旧sweet_spmi_read被软件白名单拒绝（不含per15），
未发起该硬件事务，battery attach失败0xe00002ca并回滚，服务stop。不是
CC极性/APSD未完成/适配器协议导致的route失败。状态采样原先用另一个typed
精确表，能读Type-C，不要混淆两个入口。0160测试的宽松read mock漏掉此限制。
不是mux全停：失败后ADC2请求/2成功/3EOC，三键都有down/up，触摸到74。
2次ACM configured，最后重插reset无新configured按已知gadget bug保留；
ADC请求196问题本轮未覆盖，不能宣称修复。底稿scratch/artifact-0120a9。

新增0161接0160：sweet_spmi_battery_irq_read固定SID2，per10/13/15/40和
offset11/12/13/15的配置只读，走原observer共享锁/timeout poison，不改通用
白名单、写表或owner。mux用此接口，读错ERROR含route/SID/per/offset/rc。
failIrq停止后撤销snapshot/CC/APSD/测量及ADC consumer有效性，走原publish/
updateStatus撤销modern/legacy external/capable/charging/full，input/status
valid=false，保留上次百分比，不假装新断线测量；仍先走原控制disable，不新增
重试/轮询/自动fallback。错误措辞不再把consumer失败笼统称为整个mux/keys停机。

本地：严格apply到实际0120a9云源码，verify33文件一致，106个源码gate通过；
SweetBattery/sweet_spmi/SweetSpmiInterruptMux ARM64单文件编译通过。ASAN/UBSAN
实际mux readPeripheral→完整transport联测成功/拒绝/缺route/timeout poison，
固定权限表拒绝范围和输出保留；mux mock恢复旧Type-C拒绝，新增单consumer
失败但keys继续案例；实际failIrq/publish验证modern/legacy源标志与有效性
撤销。runtime6/mux66/transport122/SPMI4/ADCclient1/log1/board6个PASS记录。
CI只追加源码gate，编译命令不变；打包器拒绝旧0120a9。文档BATTERY-TYPEC-CC、
IRQ-BRINGUP-LESSONS、BATTERY-DRIVER-NEXT。底稿scratch/battery-cc-irq-fix。
未push，无新boot或修复版设备结果。

NEXT：用户push本地0161提交；新产物下载后核对并用原--battery-evidence打包
runtime1/IRQ4/ADCconv0，保留三键/触摸/USB IRQ和USB日志。不重测0120a9，
下一轮先看battery group成功attach，再看当前充电器松线/插稳/拔线的plugin/
Type-C事件和UI发布。真实充电/PD、完整input provider和ADC第二EOC仍未完成。

### Previous 0120a9 packaging handoff (superseded)

**CURRENT: 0120a9云产物已核对，0160 CC镜像已打包，待实机日志。**

实现提交0b1cf56。0120a9 buildtree的33文件与battery-cc/verify逐字节一致，
102个源码gate通过；实际kernel manifest有效，含CC/CC TIME/INPUT CAP与
battery-typec-cc/attach及既有三键/触摸/USB IRQ/ACM/APSD标记。
产物SHA256：buildtree gz 68edbf3a493e4abf801c1107bca9835f42cbaaf15949bf469aad564b0eff11fd；
kernel gz 5e4ea03e937c9de70321219c67faa175d5f41af5c8935ced10df56af68b7a24e。
实际kernel SHA256 7d99770458eab7dfb03a4441078eb5efd6d510f479642e756f3e384799ef5f01。

已生成工作区out-bs/boot-16k-0120a9-battery-evidence.img，35,667,968字节，
SHA256 8dc5f64f39de24683732b67451265cfaf70787e64048254837832d17447e9c72。
使用原--battery-evidence：runtime1/IRQ4/ADCconv0，保留按键/触摸/USB IRQ及
USB日志，仍未启充或实现PD。最终boot header v1载荷与输入firmware/ramdisk
逐字节一致；从boot内FAT回读kernel/ADT/TOML匹配，参数没有重复。
底稿scratch/artifact-0120a9，含源码/manifest/marker校验与boot回读报告。
本轮未改驱动/CI，未push或操作设备；没有新实机结果，未提供本轮CI run ID/日志。

NEXT：用户fastboot boot上述镜像。电脑USB启动桌面约30秒→拔10秒→当前充电器
约30秒→拔10秒→电脑约10秒；期间确认三键/触摸，注明A-to-C/C-to-C及电源型号/
输出标识，导出sweetlog-0120a9-battery-evidence.bin（持久日志即可）。关注SID2/
PID15路由attach/hit、CC reasons/Rp/qualified及独立APSD来源。若启动/路由失败
保留持久日志分析，不要求先重测baseline。已知电脑重复枚举bug不要反复重插到
成功。ADC第二EOC超时仍未修改，观察镜像不能作为真实充电验收。

### Previous 0160 implementation handoff (superseded)

**CURRENT: 0160 CC/Rp输入判断已本地完成，待用户push及新云产物。**

用户要求“把cc判断也加进来”。新增补丁0160接0159：typed纯策略解码Rp default/
1.5A/3A、方向和12类拒绝原因；CC来源/session/3秒期限独立于APSD enable/DONE/
result。初始读只诊断，plugin/Type-C可建立依据；source仅保留同状态已有依据，
重复通知不延长旧时间。中高Rp正常sink可产生CC输入依据，不再强求DCP/CDP；
default仍需BC/USB，已知QC暂拒绝。APSD读错不阻断完整CC。policy选择实际依据
期限，ICL仍不超过provider/能力/500mA试验上限；inputVerified仍未设置，不启充。

新增SID2/PID15 bit0/2/3/4/5/6路由（rid/cc/vconn/vbus/attach/legacy），边沿取
本机DTS1/1/3/1/1/3，共用事件0x40。沿用GIC513→mux IOService/workloop→registry
client→battery workloop，按键/USB/触摸实现不改。新增APID不硬编码，必须通过
唯一EE0 IRQ ownership及原enable检查；实机权限/交付待验证，不能以host映射271
当事实。attach失败仍显式报错/回滚/按group隔离，不退回轮询。重SPMI只在consumer
loop用原typed采样后端，未增写权限。VBUS与全CC字段前后核对，变化/读错撤销。
输入IRQ采样中或采样后排队撤销旧依据，控制提交前也拒绝pending的非disable操作。

新增INFO日志CC/CC TIME/INPUT CAP及IORegistry诊断属性；保留原APSD/TYPEC原始
日志与等级。0120a8 Rp3A/misc39会明确reasons80 VBUS_ERROR，仍不认可输入；
不把Rp当PD合同，不声称Type-C角色管理/DPDM/PD完整provider已实现。

本地：严格apply到实际0120a8云源码，与verify33文件一致；102个源码gate通过，
旧0120a8被新kernel marker打包gate拒绝。SweetBattery/SweetPmicPower/
SweetSpmiInterruptMux ARM64单文件交叉编译通过；runtime ASAN/UBSAN、mux/IRQ/
registry生命周期/6条Type-C路由边沿及邻位保持/按键并行、ADC consumer、charger
transport、日志分级、board startup回归通过。CI只追加源码gate，编译命令没改。
底稿scratch/battery-cc，说明docs/BATTERY-TYPEC-CC.md。无新boot/设备结果、未push。

NEXT：告诉用户push本地0160提交。下载新kernel/buildtree后核对并用原
--battery-evidence打包runtime1/IRQ4/ADCconv0，保留三键/触摸/USB IRQ和USB日志。
一次boot电脑→拔线→当前充电器→拔线→电脑；记A-to-C/C-to-C及电源标签，持久
日志足够，不要求baseline。观察Type-C attach/hit、CC reasons、Rp与APSD独立结果。
若mux路由权限/配置失败按日志定位，不盲改owner或恢复轮询。ADC第二EOC超时、
完整input/identity/parallel/watchdog授权与真实启充仍是后续，0160未修改ADC。

### Previous 0120a8 log handoff (superseded)

**CURRENT: 0120a8实机日志已分析：APSD使能/source事件正常；ADC5请求196超时。**

日志sweetlog-0120a8-battery-evidence.bin，SHA256
f242bb5eb22f8440d40c35e90e8bde52e020a6ee2c94b7210a3d10a23b84b3cd，
v1/ring4194304/total810725/flush76，未回卷，有效正文无NUL。771连续遥测，
跨度789.563s；12profile全match1/integrity13/4027mAh。10输入（初始1/plugin6/
source3）全options18/error0/enabled1/coherent1，确认BC1.2实际已开；XNU没写
enable不是硬件关闭的证据。启动DONE不认可；id394plugin未完成，1.350s后
id397source21/result10=FLOAT qualified1，id398再次source同结果；id718重新
接电脑未完成，0.751s后id720source01/result01=SDP qualified1。无DCP/CDP，
10次sink-ok0。MSOC id390不采输入，759保护tick不刷新输入，时序区分已实测。
中段TypeC misc39含VBUS_ERROR bit4，后段debug/legacy，不能认作普通sink许可。
LG有FLOAT500mA投票路径，XNU完整输入provider尚缺，不伪装DCP也不以换充电器
替代实现。10config均enable0，无CONTROL，runtime1未启充。

新独立故障：ADC请求1..195成功（BATID16/USBIN179），374EOC；接受193、两次
插拔/source epoch变化正确丢弃。USBIN请求196首个ground EOC处理后phase3/
MORE1，缺第二EOC处理，报TIMEOUT/-101；restore-error0/cleanup0，停止provider
并撤销测量。全375EOC，失败后无新ADC。没有超时status/request/IRQ/deadline
快照，转换未完成/IRQ未交付/时序竞态尚不能区分。IOTimer有generation保护，
不将假想旧timer直接当根因。此次不是mux全停：没有mux错误或隔离，故障后3次
电池IRQ和触摸229仍有记录；三键完整down/up仅在故障前，不声称故障后已测。
0120a6 mux pending本轮没复现，0158隔离错误分支仍缺设备覆盖。

最后回电脑重复reset/connected未再次ACM configured，保留已知gadget问题；
全程两次ACM configured属于启动段。有效BATID888516..889684uV，VBUS
4550864..5103104uV。无kernel panic记录，无真实充电证据。
底稿工作区scratch/artifact-0120a8/{analyze-log.py,log-analysis.json,
battery-evidence-persistent.txt}，结果docs/BATTERY-APSD-EVENTS.md。
本轮只分析/记文档，无驱动/CI修改、新boot、push或设备操作。

NEXT：不要求重测本轮/baseline或ADC正常bring-up。先离线复核ADC第二阶段
trigger/ack/交付/100ms deadline，下一批错误路径加typed有界只读故障快照与
真实deadline核对，不在timeout里补读发布样本/静默重试。输入并行推进方向是
Type-C/DPDM/角色撤销provider及FLOAT受限输入策略，之后补APSD生命周期与合同，
不是简单补enable。真实充电仍缺identity授权/parallel/watchdog等依赖。

### Previous 0120a8 artifact handoff (superseded)

**CURRENT: 0120a8云产物已核对，0158/0159生效，boot已打包，等待实机日志。**

实现提交775d009。实际0120a8 buildtree的33文件与最终verify逐字节一致，95个
源码gate markers全部匹配；kernel manifest有效，包含APSD EVENT/TIME、mux
pending/isolated日志、profile/input以及三键/USB/触摸IRQ/ACM标记。
产物SHA256：kernel gz b56fe9ee1f0fb286c501d1563f5f2bea079cac1fa22a651c7f12e092e04d42e7；
buildtree gz 5cf510d30a9332f3f039a94820bb5bd381811aea77d13f96481e67f5909f355c。
实际内核SHA256 b5e1546a6d8207003b8a4812a9e17e9fbb01d4ee50130d1cff30f4c6d420de7e。

镜像工作区out-bs/boot-16k-0120a8-battery-evidence.img，35,667,968字节，SHA256
1b44219c37d3d4247582bcc19781109108b331b5f0deaa87082c42ef078b9481。
原 --battery-evidence 单镜像；runtime1/IRQ4/ADCconv0，保留按键/触摸/USB IRQ与
USB日志，仍未主动enable/rerun APSD、不启充。ramdisk内kernel/ADT/TOML回读
一致，boot header v1的kernel blob/ramdisk实际切片与输入一致；无重复runtime参数。
底稿工作区scratch/artifact-0120a8/{verification.json,source-compare.json,
boot-verification.json,unixnu.toml,packaging.txt}。本轮只核对/打包/写文档，未push
或fastboot，无新设备运行结果；没有用户提供的本轮云编译日志/CI run ID。

NEXT：用户fastboot boot上述镜像。电脑USB启动桌面30秒→拔10秒→已知普通5V
充电器20～30秒→拔10秒→电脑10秒，确认三键/触摸正常；导出
sweetlog-0120a8-battery-evidence.bin并注明电源类型，无live也可。APSD重点观察
1362 bit3 enabled、source事件0x10、DONE/result、一致性与独立source时间；不要
把plugin读取到DONE解释为本次检测完成。继续观察0158 pending route及group。
第二次电脑枚举失败仍按已知gadget问题保留证据，不要求反复插拔到成功。

### Previous 0159 implementation handoff (superseded)

**CURRENT: 0159 APSD事件来源/时效修复本地完成；用户不单独push0158，一起提交。**

更正前次对话：0151已实现battery-source（SID2/PID13/bit6）、SOURCE事件位及
runtime订阅，0155保留，0120a5已有6次source命中。“当前没有source路由”的
判断误看了0141早期版本。本轮不重复注册route，也不能从“XNU未写enable”
推断硬件使能关闭；原先1362 bit3没有读过。

补丁0159接0158：irqAction传递plugin/source原因，初始采一次；CHGR/MSOC/READY/
有限SOC确认/保护monitor不读取或续期APSD。新增精确只读1362/BC1P2 bit3，
USBIN→APSD_STATUS→RESULT→STATUS→USBIN一致性检查，失败/变化拒绝。观测会话、
source来源及独立时间拒绝启动残留DONE，处理两种事件先后但不续期；已观测
拔插/读错/新输入IRQ排队撤销。策略与执行deadline增加source期限。缺少真实
input/identity/parallel/watchdog授权仍不启充，未主动enable/rerun APSD。
后续需DPDM PHY/角色/PD状态provider及typed enable/trigger生命周期，不能把
事件证据当已证实的电流合同或假定捕获了每次物理插拔。

本地：0158+0159严格apply到实际0120a6云源码并与verify逐文件一致；新源码gate
检查0159 marker，旧0120a6被拒绝。ARM64三个TU交叉编译、runtime/IRQ/mux/ADC
consumer/transport/SPMI/日志/board回归通过，含来源先后、读取竞态、过期与撤销。
CI只追加源码gate，编译命令不变；packager原battery-evidence新增0159 markers。
文档docs/BATTERY-APSD-EVENTS.md；底稿工作区scratch/battery-apsd-events。
没有push/新云run/boot/设备结果。用户自己push。

NEXT：用户一次push含0158和0159的当前main；下载新编号kernel/buildtree后核对
patch与markers，再用原 --battery-evidence 打包（runtime1/IRQ4/ADCconv0），
保留三键/触摸/USB IRQ及USB日志。一次boot采电脑/普通已知5V电源的插拔持久日志，
看events0/1/10/11、enabled/coherent/source/qualified和source时间，再看0158
pending route。仍非真实充电验收，不重测baseline或ADC bring-up。

### Previous 0120a6 packaging handoff (superseded)

**CURRENT: 0120a6 云产物已核对并打包，等待实机 profile/input evidence 日志。**

0120a6 SHA256：`xnu-kernel-0120a6.gz` `c11a13767ac100fbc3cb345cec4fd0e4092085e6bc384af633ae9080fdd4d4f8`；
`xnu-buildtree-0120a6.gz` `2f3945678c0aff6543a9bda6d9c0f33ad7509d78350a79208ba0b6443e5fb1ed`。
云 buildtree 中9个0157关键文件与本地verify逐字节匹配，kernel strings含新
`SWEET FG PROFILE`、`SWEET BATT PROFILE`、`SWEET BATT INPUT v4`、`SWEET BATT ICL MODE`
markers；源码gate/packager通过，没有旧内核冒充。

已生成镜像：`out-bs/boot-16k-0120a6-battery-evidence.img`，35,667,968字节，
SHA256 `6a7c939d7c2b1356251ac68e895655d4619393d52466ba5b5e0c4cb898e0b90a`。
内核SHA256 `3b597fc3924bd346c7ebad678b04a1b63673878fd690ccbc65b9ba04f3f80066`。
TOML参数确认：`sweetbattirq=4 sweetbattuidiag=1 sweetbattruntime=1
sweetbattadcconv=0`，保留按键/USB/触摸IRQ和USB日志；无启充参数。

本轮仍需用户 `fastboot boot`，再导出 `sweetlog-0120a6-battery-evidence.bin`。
测试顺序：电脑USB启动桌面约30秒→拔10秒→已知普通5V充电器20～30秒→拔10秒→
电脑10秒；注明每段电源。关注 `SWEET BATT PROFILE` 是否 match=1、integrity=0x13、
nominal=4027、Tocad prefix，及每段 INPUT/TYPEC/ICL raw/error/epoch/time。检查
无FG cleanup/poison、无mux隔离、无错误CONTROL。即使sink-ok=0也保留原始状态，
不判成单纯驱动故障；本批充电仍互锁。

底稿 `scratch/artifact-0120a6/{source-compare.json,unixnu.toml}`。

**CURRENT: 0157 profile/输入证据实现已完成，等待用户push与新云产物。**

实现提交 `39f9cc7`，补丁 `xnu/patches/0157-mh2lm-battery-profile-input-evidence.patch`。
新增 `sweet_spmi_fg_read_profile(sweet_fg_profile *)`：固定24字节前缀4522..4539、
integrity4750、nominal46ea/b，同MEMIF grant，开始时间在FG锁内、完成时间在所有
清理之后。复用原有界会话/版本/writer/初始DMA恢复/poison，四字段scalar签名及
范围不变。部分读取或清理失败保持调用者输出；后端撤销本次tuple，不用旧前缀。
事件采样用tuple代替两次独立integrity/nominal会话，learned仍单读。前缀失败
也使本次nominal/profile未知，现有IOPM按百分比语义发布，不静默退旧元数据。

输入snapshot新增独立inputEpoch/start/end，初始/IRQ/既有有界确认才更新；保护
tick保留APSD/TypeC/plugin/debug/legacy的原获取时间和序号，不扫描续期。仅新增
1507/150d两项固定只读白名单，禁止写；失败清本次raw/valid。实时保护配置仍读。
epoch饱和时间清零，避免回卷。该epoch和ADC client epoch是独立获取序号，不能
只因数字相等就当共享token。未来输入consumer需明确关联VBUS来源撤销与时效。

新增PROFILE/PROFILE TIME、INPUT v4/INPUT TIME、TYPEC v4、ICL/ICL MODE/LIMIT
CONFIG短行，raw/error/id/时间可拼接，INFO分级；底层WARN/ERROR沿用。profile
match=1 Tocad/2 LGC/0未知，校验24字节/白名单/nominal，仅诊断，不是完整416字节
或物理BATID验证。输入精确APSD/QC分类+普通sink拒绝条件，初始策略只接受DCP/CDP、
拒绝legacy/debug/SDP/QC等；3秒独立快照期限并入决策期限，饱和加法防溢出。
板级FCC校验2800，首次受控执行器仍500mA/4200mV。所有limits授权仍false，未启充。
长期稳定插线时缓存输入不再新鲜，不通过保护timer扫描续期；事件许可provider
尚未完成，不能声称可持续充电。并联隔离、watchdog维护与身份授权仍缺。

本地底稿 `scratch/battery-evidence-0120a5/`，含base/edited/verify、cloud-apply.json、
validation-results.json、implementation-summary.json、各suite日志和编译结果。
0120a5实际33文件基线逐字节一致、0157严格apply后与verify相同。3个ARM64 TU
SweetBattery/SweetPmicPower/sweet_spmi编译通过（旧CI头/Linux clang兼容参数，非
完整native link）。transport ASAN/UBSAN118场景含37个固定profile场景，runtime
策略/实际采样/日志长度/时效/退出、mux及80竞态、ADC client17+100竞态、SPMI与
board回归通过。源码gate接受0157拒绝0156，打包档拒绝真实0120a5旧kernel。
CircleCI只新增源码gate，实际build命令/参数不变。无新云build ID、boot或设备
结果，本轮未push/fastboot。详见 `docs/BATTERY-PROFILE-INPUT-EVIDENCE.md`。

NEXT：用户push main并下载新编号kernel/buildtree；核对0157的6个变动文件/33基线
及 `_sweet_spmi_fg_read_profile` 符号，再运行
`tools/mk-mh2lm-event-matrix.sh <xnu-kernel-编号.gz> <编号> --battery-evidence`。
输出 `out-bs/boot-16k-编号-battery-evidence.img`（runtime1/IRQ4/ADCconv0），保留
三键/触摸/USB IRQ与USB日志。无需baseline/dry或重新测ADC bring-up。
同次boot：电脑USB桌面30s→拔10s→普通已知5V充电器20～30s→拔10s→电脑10s，
导出 `sweetlog-编号-battery-evidence.bin` 并注明电源。无live也可仅持久日志。
重点tuple完整、预期Tocad match1/4027/13及prefix，与BATID结合解释；输入各阶段
raw/error/type/sink/epoch可解释，无清理失败/隔离/错误CONTROL。sink-ok=0不必然
是驱动读错，可能legacy/accessory等尚不支持路径；保留原始状态，不伪造许可。
本批仍不充电符合范围。之后接身份/输入证据消费者、并联隔离与watchdog。

### Previous 0120a5 offline identity/input audit

**CURRENT: 0120a5后身份/输入离线核对完成；下一实现范围已落文档，尚无新内核。**

新增 `docs/BATTERY-IDENTITY-INPUT-AUDIT.md` 与离线复算工具
`tools/audit-mh2lm-battery-evidence.py`。LG源码HEAD仍为d4f335e。
严格复现extension_get_batt_id：BIAS1825mV，先uV/1000截断再分压，48个accepted
BAT_ID得到94786..95057Ω（整数94/95kΩ），全部落Tocad91k的78..104区间，
不落LGC12k的11..13。Tocad416字节profile offset412 nominal4027与实测一致，
LGC4073不一致。两项支持Tocad候选，未声称完整身份/profile已验证。
LG supplement将profile3300mA覆盖为2800mA，未来不能漏此板级约束。

FG integrity13在LG白名单，但仍需固定word65/offset0/len24前缀比对。Gen4
word64..169映射4520+(word-64)*2，前缀SID2 4522..4539；当前FG API只处理>=170，
不能直接塞65进旧分支。应复用有界FG会话和锁/恢复，只读profile不写学习数据。
Tocad prefix0900ffe3f10796dab3d50000a1bda38af88784929e942380。

0120a3旧preflight：APSD1/result1=SDP，FCC34=2600mA/FV53=4430mV，ICL1e=1500/
settled3c=3000mA。TypeC1506=40为accessory路径，150b2a无CC-attached，不能当
普通Rp；旧启动片段不能充当前0120a5许可。0120a5只打印valid bitmap而没打印
这些raw，不能反推。APSD解析还须检查status QC bit1/精确result枚举/角色；现
sb_decide仅排result60，inputVerified一直false所以未放行。snapshot保护tick
会刷新总时间却缓存输入，未来必须给输入独立时间/epoch，不能偷偷续期。

USB配置描述符bMaxPower250=USB2 500mA，configured仅内部endpoint bool，尚无
电源许可provider；SET_CONFIGURATION(0)分支未清configured，reset才清。
先记录未修改USB，不能直接将bool接入授权；需撤销/reset/disconnect/suspend/
generation语义。备份boot_a/b静态header无lge.battid参数，不排除ABL动态追加。

验证：离线工具按有效SWEETLOG长度复算，结果
scratch/artifact-0120a5/battery-evidence-audit.json；源码SHA/profile SHA/全部数值
可复现。具体检查结果与提交号见git提交。未改驱动/CI/boot、未push/设备操作。

NEXT：实现固定profile-prefix typed读取+既有输入配置raw/error/epoch/独立时间
记录，按需在后端加固定1507/150d；接精确APSD/角色/时效消费者后才形成候选限值。
与并联隔离/watchdog依赖按六段计划推进，仍不打包启充档。新产物同一观察镜像
可分电脑USB/已知普通5V充电器采集，持久日志足够；无需重复0120a5 ADC转换。
本次离线核对无需用户上机/push。用户自己push/fastboot。

### Previous 0120a5 consumer device result

**CURRENT: 0120a5 ADC5 consumer持续转换/插拔正常路径已通过，下一步补身份/输入证据。**

日志logs/sweetlog-0120a5-battery-adc5-consumer.bin，SHA256
`f009090b73162c63ecb16122789013d4ab08b90e0b4bfb1dda12ca242c431ddb`。
v1/ring4194304/total1511002/flush144、未回卷/有效正文无NUL，按header有效长度
解码。实参runtime1/IRQ4/ADCconv0，遥测跨度1470.037s，约24分半。

657请求/完成连续id1..657、全error0/valid1/restored1；BAT_ID48次全接受，
888581..889814uV，中位889489；USBIN609次完成/607接受/2丢弃，接受范围
4610032..4994112uV。1266真实ADC EOC=48+609*2。BAT_ID间隔30.141..30.346s；
同epoch接受USBIN596对启动间隔2.054..2.861s、中位2.079。BAT_ID测量区间
7.444..14.399ms，USBIN29.857..92.015ms，不含最后恢复耗时。

两个真实竞争：token496拔线时raw4b/77840uV完成，但epoch12→13/present0，
consumer正确丢弃；token498识别事件epoch15→16，4981648uV旧结果同样丢弃，
之后token499新epoch接受。655接受+2丢弃恰好覆盖657结果；日志顺序重放确认
present0撤销后无新USBIN请求、没有错误epoch接受。这两WARN是预期保护，无ADC
FAILURE/TIMEOUT、ERROR、panic、mux故障/隔离。source epoch共19（含startup/arm），
present0六条，不能将每条epoch算一次物理插拔；短暂presence反复的物理原因未证明。

1433组SAMPLE/FG/CAP/TIME完整；19 IRQ/arm+1414保护tick，V/I均有效，
3854..3953mV、IOPM -736..+40mA；temp29.25..31.50C（未LG补偿），容量4027/
profile13，SOC64→63%。19CONFIG全enable0/pause0/WD62/time8c；11policy全allow0，
reasons7c0(6)/7c8(5)，无CONTROL，未恢复真实充电。17真实电池IRQ刷新USBIN10/
source6/MSOC1，notification/sample连续且healthy1，发布延迟89.319..111.930ms。
三键完整down/up、touch记录140/empty0、USB128/err0；既有endpoint WARN10条。
有SpringBoard shutdown请求，无完整ADC stop/系统PM/故障恢复实机证据；未取独立
IORegistry dump，不宣称所有时效/生命周期边界已外部验证或USB问题全修。

底稿scratch/artifact-0120a5/{analyze-consumer.py,consumer-analysis.json,
analysis-summary.txt,battery-adc5-consumer-persistent.txt}，完整结果见
[docs/BATTERY-ADC5-CONSUMER.md](docs/BATTERY-ADC5-CONSUMER.md)。本轮仅分析记录，
未改驱动/CI、无新boot/push/设备操作；无需补live/baseline或重复本轮测试。

NEXT：按六段计划继续实际电池身份/额定限值与输入能力证据，再补SMB1355隔离
和watchdog维护。通过IOKit/typed接口接入，不从889mV直接宣称电池型号，也不把
4.8V当电流承诺；缺项保持充电互锁。0156正常路径已通过，不再把接ADC consumer
列为下一轮任务。用户自己push/fastboot。

### Previous 0120a5 artifact packaging handoff

**CURRENT: 0120a5云产物已核对并打包，等待实机ADC5 consumer日志。**

kernel gzip SHA256 `f4d0709e6100cd5fb0e84b89b53e419f16142887c758e698bc1af35ed5a7f89e`；
buildtree gzip `90e421b00c5409c54bd9a7db10ae7a8755ae2b768cb9c00051c8f78609477bbc`。
33个0156源码逐字节匹配本地verify，source gates、plain arm64/subtype0、16K segment、
manifest及新ADC consumer/provider方法符号通过。内核SHA256
`8f21f6d1b0cb653c440de3318a85fce9b91dfd9edb620e9c4d6ebd18d797ef92`。

镜像 `out-bs/boot-16k-0120a5-battery-adc5-consumer.img`，35,667,968字节，SHA256
`ff0f5284331f402d091f003b2cb4fb8276f48dc161db17e62665a974b59ad4a3`。
最终boot回读：header/原firmware、FAT kernel/ADT/TOML一致，无重复args；
runtime1/IRQ4/ADCconv0，无adcdiag，保留USB/按键/触摸IRQ及USB日志。充电仍互锁。
底稿 `scratch/artifact-0120a5/{verify-artifact.py,verify-images.py,verification.json}`。
本轮仅核对/打包/记录，未改驱动或CI，未push/操作设备。

NEXT：用户fastboot boot上述镜像，USB插入启动、桌面等30秒，拔线15～20秒/
插入15～20秒两轮，操作三键/触摸，可含屏幕息屏一轮；导出
`sweetlog-0120a5-battery-adc5-consumer.bin`。重点确认client接收BAT_ID/USBIN、
插入时持续EOC与恢复、拔线撤销旧值/停止安排USBIN、重插新epoch有效，且无ADC
失败/共享mux隔离/错误启充。不必先baseline/dry或重跑启动有限转换档。
日志按header total解码，避免旧ring尾部混入。完成后再核对身份/输入能力等依赖。

### Previous 0156 implementation handoff

**CURRENT: 0156 ADC5 → SweetBattery 测量接入已实现，等待用户push与新云产物。**

实现提交 `9b6d817`，补丁 `xnu/patches/0156-mh2lm-battery-adc5-consumer.patch`。
新增每consumer的SweetAdc5Client OSObject，IOKit matched/terminated/general-interest
发现并retain ADC5 IOService，通知只短锁/排队；所有typed status/request/copy在
battery自己的workloop上，不直读寄存器、不走全局单例。provider新增READY/stop
通知、status()、请求token与measurement.sequence；Busy/初始NotReady只等通知。

runtime1..3自动接入。启动BAT_ID，已知输入存在才测USBIN；IRQ/arm-sync/有界确认
撤销旧USBIN并递增epoch，转换中插拔的旧结果丢弃。现有1s保护monitor维护测量：
USBIN满2s安排转换、3s过期；BAT_ID满30s刷新、60s过期；使用startedNs保守计龄。
不定时扫描USB/APSD/Type-C发现插拔，也不轮询ADC status替代缺失EOC。
终止/失败/READY丢失撤销缓存、连接终结不自动重绑；stop在battery gate外同步
通知，保留provider到interest remove后再release。失效移除旧测量数值并updateStatus。
系统真正sleep后的完整ADC suspend/resume尚未实现，不把屏幕息屏当系统睡眠验收。

充电授权字段没有设置，仍互锁；本轮不证明电池身份/输入电流能力/SMB1355隔离/
watchdog维护，不应出现启充CONTROL。USB/按键/触摸路径未改。CircleCI只增加0156
source gates；实际build.sh命令和参数未变。用户自己push/fastboot，本轮未push/设备操作。

本地底稿 `scratch/adc-consumer-0120a4/{base,edited,verify}/xnu`；
`validation-results.json`、`cloud-apply.json`、`*-tests.txt`、`gate-results.json`。
31基线文件与0120a4云树逐字节匹配，0156严格apply后33文件匹配；四个ARM64 TU
编译通过（旧CI头+Linux clang兼容参数，非完整native link）。实际ADC client
17场景+100通知/终止stop竞态，实际mux/provider/keys/battery 57场景+80竞态，
transport81，runtime（含ADC发布/撤回与不授权）、SPMI、board回归通过。
源码gate接受0156拒绝0155；packager拒绝0120a4旧kernel。host模型不证明native
锁序或真实重复转换稳定。接口、锁序和测试步骤见
[docs/BATTERY-ADC5-CONSUMER.md](docs/BATTERY-ADC5-CONSUMER.md)。

NEXT：用户push main，下载新编号kernel/buildtree；核对0156后运行
`tools/mk-mh2lm-event-matrix.sh <xnu-kernel-编号.gz> <编号> --battery-adc5-consumer`。
生成 `out-bs/boot-16k-编号-battery-adc5-consumer.img`，runtime1/IRQ4/ADCconv0，
保留USB/按键/触摸及USB日志，不混入启动有限诊断。USB插入启动，桌面约30秒，
拔线15～20秒/插入15～20秒两轮并操作三键/触摸，可含屏幕息屏一轮；取持久日志
`sweetlog-编号-battery-adc5-consumer.bin`。重点看accepted token/epoch/uv、
invalidate present0/1、插入时重复EOC/restore，拔线后不继续安排USBIN，无ADC失败/
共享mux隔离/panic/错误启充。无需补baseline/dry或重复上一轮单次转换。

### Previous 0120a4 real-device conversion result

**LATEST RESULT: 0120a4 实机 ADC5 转换链路通过；下一步接入测量provider与充电证据。**

日志logs/sweetlog-0120a4-battery-adc5-conversion.bin，SHA256
61b405c6b10b1f66b341abc0687f57b065850060874627c88afc35091b637814。
v1/ring4194304/total357043/flush26、未回卷/有效正文无NUL。ring尾部残留旧轮
日志，必须按header total解码，不能统计整个bin的strings。参数conv3/runtime1/IRQ4。

服务/group2 attach成功，三次真实ADC EOC：BAT_ID raw3591/889684uV/eoc1，
USBIN raw122e/4831136uV/eoc2，均valid1/restored1。USBIN中间error1是MORE正返回，
并非失败。TIME区间70.336459/32.360156ms，含等待/调度、不含最终恢复；无ADC
FAILURE/TIMEOUT、ERROR、panic、mux隔离。真实转换与受限恢复正常，真实故障/
服务终止路径尚未设备验证。

280完整快照/id1..280，跨度289.003s；2次真实容量读取+278缓存。V/I有效3951..3952mV、
IOPM -132..0mA；temp30.25..30.75C、capacity4027/profile13。CONFIG两次en0，
WD62/time8c，policy allow0/reasons7c0，无CONTROL。当前转换结果未接入limits，
不能称已恢复充电。power/up完整down/up，down仅down；触摸44/USB128err0，
3条既有endpoint WARN。本轮没有USBIN插拔事件，勿计入旧轮19次刷新。

NEXT无需补ADC/baseline/dry；实现SweetBattery通过IOKit发现/retain/终止清理接入
ADC5测量provider，核对实际电池ID/限值、VBUS/输入能力、SMB1355隔离和watchdog pet，
保持充电互锁。底稿scratch/artifact-0120a4/{adc5-analysis.json,analysis-summary.txt,
battery-adc5-conversion-persistent.txt,analyze-adc5.py}。详细结果见
[docs/BATTERY-ADC5-PROVIDER.md](docs/BATTERY-ADC5-PROVIDER.md)。本轮仅分析/记录，
未改驱动/CI/生成新boot/push/操作设备。

### Previous 0155 implementation and packaging

**0155实现提交db2728f，0120a4已云构建并完成上述实机转换；下方保留实现/打包过程。**

实现提交 `db2728f`；0155 patch: `xnu/patches/0155-mh2lm-adc5-conversion-provider.patch`。新增专用
`SweetPmicAdc5Device` nub 和 `SweetPmicAdc5 : IOService`，默认发布并订阅 mux
ADC5 group2 EOC、保持转换空闲；自己的 IOWorkLoop/timer 负责 BAT_ID/USBIN 转换，
没有在 matched 回调做重 SPMI，也没有接入充电放行。专用 consumer 不再匹配全部
IOResources。`sweet_board_init()` 在 mux 后发布 ADC nub，按键/电池事件路径保持独立。

新增 `sweetbattadcconv=1..3`：1=BAT_ID，2=USBIN（含GND workaround），3=两者顺序。
每阶段100ms有界、校验EOC/status/channel/raw、恢复配置；错误/恢复失败停止provider，
不静默切回轮询。默认0只启动服务不发起转换。打包入口：
`tools/mk-mh2lm-event-matrix.sh <xnu-kernel-编号.gz> <编号> --battery-adc5-conversion`，
生成 `out-bs/boot-16k-编号-battery-adc5-conversion.img`。

本地证据：`scratch/adc-provider-0120a3/validation-results.json`、
`arm64-tests.txt`、`transport-tests.txt`、`mux-tests.txt`、`board-tests.txt`。
严格apply/31文件逐字节比较、6个ARM64单文件编译、transport 81项、mux/provider
57场景（含13项ADC）及80次竞态、runtime/SPMI/board回归均通过。旧0120a3 kernel被新profile gate拒绝。
尚无0155云端完整链接或真实ADC EOC/电压恢复日志；充电仍互锁，不能据此称真实充电恢复。
真实测试步骤见 [`docs/BATTERY-ADC5-PROVIDER.md`](docs/BATTERY-ADC5-PROVIDER.md)。

0120a4 已下载并核对：kernel gzip SHA256 `1ae1aa5e6749d0ac609ab3dac515238f4cae5f85030ae8442e50070143301a06`，buildtree gzip SHA256 `ffc54f6cbb916395cc2f9f2f5a31499470a48153e2726cf1c6dce2734e9998b4`；内核 SHA256 `f53f989649556aeb125912d802156cc307e7f8b93c0518f1536b0a3f8c4118d2`。31个0155源码逐字节一致，plain arm64/subtype0、16K segment、ADC5符号/marker通过。

已生成并回读镜像：`out-bs/boot-16k-0120a4-battery-adc5-conversion.img`，SHA256 `bce9b70785a24f48352335960f4833e30e1764604bbdcd5781deae5a03f971a5`。参数包含 `sweetbattirq=4 sweetbattadcdiag=1 sweetbattadcconv=3 sweetbattruntime=1`，保持 USB/按键/触摸 IRQ，充电仍互锁。验证脚本见 `scratch/artifact-0120a4/verification.json`。

NEXT：用户push main并下载新编号kernel/buildtree；核对0155后打包上述组合镜像。
首次保持USB线插入，取得启动早期BAT_ID一次及USBIN两次EOC和恢复日志；
约60秒内操作三键/触摸并插拔一轮，导出持久日志。ADC只在启动时各采一次，
插拔用于回归事件，不会自动重采。无需先baseline/dry；本轮无push/设备操作。

**0154实机结果仍为0120a3；0155 ADC provider已在上方完成本地实现，当前等待云构建和实机证据。**

用户导出logs/sweetlog-0120a3-battery-telemetry.bin，SHA256
f414a6331edb147c6ab48e4203ee9ac6db2f9056b1e7bae03a7e5725f74f60fe。
v1/ring4194304/total886017/flush125，未回卷/无NUL；实参runtime1/IRQ4/adcdiag1，
HARDWAREv1出现。ADC检查1次/error0/valid1fff，13字段全valid/error0。
user observer=writer=irqAPID280/writeEE0；cal observer=writer282/writeEE0，
irqAPID65535/noEE0owner(-4)。DTS只声明user0x31bit0rising EOC，不要求独立cal
IRQ，该项不构成阻塞。元数据不证明真实写权或EOC投递，尚未发起转换。

字段顺序0..12的raw：08,28,00,00,18,80,4b,01,00,00,00,80,03，对应
3104/3105/3108/3109/3142/3143/3144/3145/3146/3147/3150/3151/3744。
channel4b对应BAT_ID_PU2只是继承配置；data8000也是LG ADC_USR_DATA_CHECK
无效值。不能将可读valid1或配置当成有效ID/VBUS/电池身份。

1252完整SAMPLE/FG/CAP/TIME，id1..1252匹配，跨度1279.409s；21初始/事件
真实容量重读，其余1231缓存。V/I全有效3859..3970mV、IOPM -737..+15mA；
温度全有效28.50..30.50C（raw72..7a，仍未LG补偿），capacity4027/profile13。
TIME min37.046/median37.596/max92.607ms。首次DMA WRITE_ERROR2清到0成功，
无后续FG WARN/ERROR。ADC记录最长INFO+newline116/124/79bytes。

19mux电池刷新USBIN12/source5/MSOC2，notification/sample1..19连续healthy1，
routepublish88.953..90.822ms。power/down各2hit且完整HID down/up，up无本次
操作证据；touch计数229、USB128，无ERROR/panic/mux隔离。10USB endpoint
cmd6/status1 WARN属既有问题，powerdsel4unsupported也仍在。不能称所有驱动
无bug/全部生命周期已验收。充电21CONFIG均en0/pause0/WD62/time8c/SMB0/CFG2，
policy13条allow0/owned0 reasons7c0(7)/7c8(6)，无CONTROL，CHGR47，不能称真充电。

底稿scratch/artifact-0120a3/{battery-telemetry-persistent.txt,
analyze-telemetry.py,battery-telemetry-analysis.json,analysis-summary.txt}，详细
结果见docs/BATTERY-HARDWARE-INTERFACES.md及runtime测试文档。本轮只分析/
文档，未改驱动/CI、无新boot/push/设备操作，无需补live/先dry/重复本轮。
NEXT实现实际ADC provider：保留IOService/自己的workloop，经现有共享SPMI
受限协议和mux注册EOC；准备、IRQ完成、有界超时、错误恢复/退出都在明确
生命周期内，不在matched做重活，不静默切换轮询。验证实际写/转换/EOC和
BAT_ID_PU2、USB_IN_V_16校准/单位、USBIN workaround，再接有效证据接口。
本次owner可读通过不等于ADC转换成功或已具备安全limits；SMB1355 provider、
实际电池身份/额定限制、输入能力和WD pet仍缺。保持充电互锁，勿关WD绕过。

### Previous 0120a3 packaging handoff

**CURRENT AT PACKAGING: 0120a3已核对为0154并打包。**

用户提供logs/xnu-kernel-0120a3.gz与xnu-buildtree-0120a3.gz。两者SHA256分别为
`6e80b1e7fddefdd4d763d4cb60b5dde8484a8a26154abf490e9da8d775d3b9c9`、
`a1dec3ad24c00fe39ba39e6d50a44dd1bdbdcca71c1c4039546540d6af806d76`。
buildtree解出29个选定文件，逐字节匹配0154 verify；内核manifest、plain
arm64/subtype0、16K segment和新增hardware/ADC5 marker、semantic charger/
ADC inspect symbols通过。内核SHA256 `22460d3e4056179e3ace39fc367c25c438dd9829a53327cf93a5a57a7afe91d3`。

已用`tools/mk-mh2lm-event-matrix.sh ... 0120a3 --battery-runtime`生成并回读
三个同核镜像：
`out-bs/boot-16k-0120a3-battery-runtime-observe.img` SHA256
`259dc12f3ea402f9ba731d3941635ce5207c4c2f994aef446c0b2fdf9d0bb09b`；
`out-bs/boot-16k-0120a3-battery-telemetry.img` SHA256
`de0e8c5474c323df44abaaa2ad1fa260d1c31190f40a7012250169dab672ec78`；
`out-bs/boot-16k-0120a3-battery-policy-dry.img` SHA256
`cb91931bdeb4946c66ed08afcec4093d7f2b5712990a7d1ef0001e6dae82c088`。
回读确认kernel/ADT/TOML/firmware一致、参数无重复，telemetry唯一带
`sweetbattadcdiag=1`；控制仍有互锁。未执行设备操作。

NEXT用户先fastboot boot telemetry镜像，启动稳定约60秒，插拔两轮且每状态
15～30秒，导出` sweetlog-0120a3-battery-telemetry.bin`。重点查
`SWEET BATT HARDWARE: v1`、`SWEET ADC5 PREFLIGHT/ROUTE/FIELD`，分别核对
observer/writer/IRQ owner与字段valid/error；同时检查原FG温度/容量/V/I、
共享IRQ和是否有panic/error。ADC日志明确diagnostic-only，不把data字节当
VBUS或电池ID，也不要先跑dry/重复USB实验。控制档未生成。

0154源码与本地测试记录仍见提交ca6927f、文档提交6cb79e2和
scratch/battery-interfaces-0120a2/validation-results.json。

### Previous 0154 implementation handoff

用户要求按XNU常见模式、复用接口，避免上层穿透读写寄存器。新增0154，
10个内核文件：runtime回调改为sweet_charger_execute固定操作/物理mA,mV/
verified/uptime期限；地址编码在内部后端，每次物理更新检查deadline（含
softwareICL模式内部三次），仅ENABLE0禁充免期限。复用现有受限SPMI原子
更新/回读，无新写权限；原PM停止互锁保留。新runtime采样迁至内部
SweetPmicPower组件，仍调用原SPMI/FG API，采样/事件/缓存语义保留。
这些是现有服务内部无状态组件，不是新IOService/provider或单例桥接。
SweetBattery仍拥有IOPMPowerSource/workloop/发布/生命周期；旧baseline/
IRQ采样/历史诊断仍保留transport调用，不宣称整个服务已无寄存器知识。

新增sweet_spmi_adc5_inspect固定SID2/user31/cal37，只读13字段；返回raw/
逐字段error/valid以及独立observer、唯一EE0writer、唯一EE0IRQowner归属。
可读≠可写≠可接IRQ；无转换/ADC写/IRQarming/新MMIO映射/白名单扩大。
旧ADC数据明确measurement-valid0，不用于VBUS/电池ID/限制放行。
--battery-runtime的telemetry新增sweetbattadcdiag1，在自己loop一次预检、
早于consumer注册；同时沿用charger只读preflight。dry/observe参数不变。
匹配/IRQ上半部无重SPMI。按键/USB/触摸IRQ-only和muxconsumer未改。

本地严格apply及29文件byteequal；58transport/FG/语义/ADC ASAN+UBSAN场景，
runtime策略/真实采样/发布/stop、11次物理写各步failure/expiry及100PMrace，
mux全套+80race、SPMI+10并发均通过、无sanitizer诊断。四ARM64单文件编译
通过（旧CIheaders/LinuxClang兼容，含头文件/严格诊断警告，非native全构建）。
完整CIgate接受0154拒绝0153，actual build.sh前缀byteequal；YAML/bash-n/
telemetry唯一ADC参数/ADC记录255byte预算通过，打包器拒绝旧0120a2内核。
底稿scratch/battery-interfaces-0120a2/{base,edited,verify,strict-apply,
*tests.txt,validation-results.json}。详细文档docs/BATTERY-HARDWARE-INTERFACES.md，
runtime/NEXT/计划/当前架构/路线图同步。无push/新云构建/新boot/设备操作。

NEXT用户push后下载下一编号产物，先核对0154源/符号/marker再用
--battery-runtime打包。先telemetry约60秒+插拔两轮/每状态15～30秒，导出
持久日志，查HARDWAREv1与ADC5PREFLIGHT/ROUTE/FIELD、原FG/共享IRQ健康；
无需先dry或重复旧USB实验。没有0154实机证据。下一步实际ADC provider
应有IOService/自己的loop并经现mux接EOC，初始化不在matched。SMB1355
SE4I2C0c仍缺provider，watchdog触发pet未实现。电池身份/限值、实测VBUS/
输入能力/并联隔离与WD保护未闭环，fLimits仍unknown、不开放充电或控制档。
0120a2遥测通过≠0154通过≠真实充电恢复。下方保留0120a2确切实机证据。

### Previous 0120a2 telemetry result

**LATEST RESULT: 0120a2初始DMA清错/温度容量/短日志实机通过；转向provider和watchdog缺口。**

用户已导出logs/sweetlog-0120a2-battery-telemetry.bin，SHA256
fedff4bec0d304aee5c720ff15fbc45fb04778781f5752eeb8bc1fa7d9458bc6。
header v1/ring4194304/total655416/flush75，未回卷/无NUL，实参runtime1/IRQ4。
承接实现47fa07e、打包记录e3235ea。首次DMA pre sts2/ctl0/arb6/cfg0，说明本次
WRITE_ERRORbit1高（不是READ_ERROR）；恢复after0/after-ok1/ctl0/ctl-ok1/
low-confirmed1/error0，后续四字段成功rev2/memtype0d。不能倒推0120a1具体bit，
也不能证明初态错误来源。ACCESS recovered0只是字段此前无失败报告，不是清错失败。

779快照=29初始/事件+750protectiontimer，started跨度779.694秒；V/I全部有效，
3875..3987mV、IOPM -647..+40mA。779温度全部有效/error0/raw73..7a/28.75..30.5C，
仍未LG补偿/独立校准。nominal/learned4027mAh/profile13，29次初始/事件真实重读
全部成功，余750缓存；不能称779次独立容量读取，不能据容量/profile证明电池身份。
四类短记录各779、id1..779匹配，INFO+newline最长SAMPLE107/FG85/CAP123/TIME86，
CONFIG155/PARALLEL84，完整TIME耗时min37.060/median37.640/max92.749ms(首次)。
只有首次DMApre/recovery，无后续FGwarn/error；本次正常路径通过，故障路径仍host。

29CONFIG均valid、en0/pause0/WD62/time8c/SMB0/CFG2。21policy allow0/owned0，
reason7c0(11)/7c8(10，多NO_INPUT)，原TELEMETRY/TEMP阻止位消失；电池ID/限值、
并联/输入能力、watchdog仍缺。29原发布charging0/CHGR47、SOC67->66；无CONTROL，
不能声称真实充电。USBIN20/source7对应27refresh，counter1..27连续healthy1、
routepublish88.776..90.595ms；keys power/down各2hit、up无本次操作证据，touch到80、
USB到128。无ERROR/panic/mux隔离；8既有USBcmdWARN另记。powerdsel4unsupported
仍为既有ABI缺口，不据此否定FG。UI/完整IORegistry未另抓。

NEXT无需补live/重复本镜像或先dry；进入第2～3段剩余证据provider：实际电池
身份/上限、实测VBUS/输入能力、并联状态/隔离，以及WDpet责任/保护。不要再把
DMA错误列为当前阻塞，不用bootarg伪造证据，不自动关WD绕过保护。第4～6段/
CHGR真实充电转换和framebuffer顺序未变。详细结果已写入
 docs/BATTERY-FG-DMA-RECOVERY.md末节，runtime/NEXT/计划/路线图/测量清单同步。
底稿scratch/artifact-0120a2/{battery-telemetry-persistent.txt,
battery-telemetry-analysis.json,analyze-telemetry.py}。本轮仅分析/文档，无驱动
修改/新CI/新boot/push/设备操作。

### Previous 0120a2 packaging handoff

**CURRENT: 0120a2云产物已核对，0153三档boot已生成，待用户先测telemetry持久日志。**

本轮只核对/打包/记录，不改驱动或CI、未push/设备操作。logs两份0120a2归档
原生24选定文件与scratch/fg-dma-0120a1/verify/xnu逐字节相同，0153生效；完整
sourcegate、manifest、plainarm64/subtype0/16Ksegment、16marker/6定义符号通过。
kernelSHA191c424d1683ca4d3137ee72589377f86ad66c8f5abb35c0bbf713ed4b4daf56。
无对应buildlog/CIrunID，不编造。归档SHA和三bootSHA详见
 docs/BATTERY-FG-DMA-RECOVERY.md 的0120a2节。

使用--battery-runtime生成out-bs/boot-16k-0120a2-battery-telemetry.img、
boot-16k-0120a2-battery-policy-dry.img、boot-16k-0120a2-battery-runtime-observe.img，
均35667968字节。从最终boot再次读回FAT核对kernel/ADT/TOML，参数无重复、
realSID2/mode4/keys/ACM保留、runtime分别1/2/省略；固件blob未变。
原生源码、脚本、TOML、verification.json在scratch/artifact-0120a2/。

NEXT用户先telemetry，约60秒+插拔两轮/每状态15～30秒，导出
sweetlog-0120a2-battery-telemetry.bin即可，无需先observe/dry或补live/旧noUSB。
分析首次DMApre/recovery原值/有效性、清错结果、四FG字段error、完整TIME，
再核对共享IRQ健康。没有0120a2实机证据；不能提前称DMA/温度/容量恢复。
本轮仍不启充，电池身份/限值、实际VBUS/输入能力/并联隔离provider和WDpet缺口
未消失。正常显示或外接电源不等于本驱动已恢复真实充电。

### Previous 0153 implementation handoff

**CURRENT: 0153已本地完成，待用户push/云编译；先验证FG遥测，不启充。**

用户“那继续吧”后完成首次FG DMA清错与短日志，实现提交47fa07e。
新增xnu/patches/0153-mh2lm-fg-dma-recovery.patch，3源文件：身份/revision、
MEMIFidle和全部writer预检后，在共享SPMI锁内重查idle/唯一EE0writer/poison，
读4370/4371。首次发现mask6错误或clearbit高才尝试一次固定4371low/high/low，
保留其他bit，high允许自清，每步回读；状态最多5读/显式delay总40us。不是
完整LinuxFGinit，实机clear时序/错误来源未确认，不声称一定是固件遗留。
清错失败锁存FGfailed；high后只在RW未poison时额外一次deassert清理，超时
共享poison后绝不复用。准备完成后的新pre/post错禁用本启动SRAM，不重清重试。
新DMA v3记录pre/recovery/post原值与有效性；ACCESS v2新增阶段操作名保留。

SAMPLE v2/FG,CAP,TIME v3以相同sampleid拆短记录，CONFIG v3和PARALLEL v3仅
初始/事件打印，独立错误与完整时间保留，不改全局console或timer采样范围。
按键/USB/触摸IRQ-only、registryconsumer、共享锁/timeoutpoison均保留。
CI仅新增/更新sourcegate（接受0153拒绝0152），actual build.sh前缀byteequal；
packager --battery-runtime marker同步更新/bash-n通过，无新参数或控制boot档。

本地50个transport/FG真实函数ASAN/UBSAN场景通过，含三命令失败、自清/延迟/
状态不清、map/readback/status失败、超时poison/不复用、权限/忙/未知拒绝、
后续/post不重清；runtime策略/发布/stop和100PMrace通过，日志含INFO/newline
的255byte检查与最大id/64bit时间通过；mux套件+80race、旧SPMI+10race通过。
原生0120a1上严格apply/24选定文件bytecompare通过，2ARM64单文件编译通过
（已有CIheaders/Linuxclang兼容，不是完整nativebuild）。底稿
scratch/fg-dma-0120a1/{base,edited,verify,*tests.txt,validation-results.json}。

文档docs/BATTERY-FG-DMA-RECOVERY.md记录语义/边界/下一轮，runtime/计划/NEXT/
IRQ路线图/observer互链已更新。无push/新CI/新boot/设备操作。用户push后下载
新产物，再核对并用--battery-runtime打包；先telemetry约60秒+插拔两轮、
各状态15～30秒，导出持久日志保留早期DMA。无需先跑observe/dry或旧noUSB。
检查清错前后raw/有效性、四字段和完整TIME，再检查共享IRQ健康。
控制仍缺实际电池身份/限值、VBUS/输入能力和并联隔离provider，fLimits空；
watchdog/pet仍缺，不自动关WDOG。测量恢复≠充电已恢复。

### Previous 0120a1 analysis handoff

**LATEST RESULT: 0120a1证实observer修正，当前FG阻塞为4370 DMA错误位；尚未补恢复。**

用户已提供logs/sweetlog-0120a1-battery-telemetry.bin，SHA256
182dee0a02438f85393c06468538c09c1714c3750e363f15d3288e5a588a4862。
v1/ring4194304/total515723/flush77，未回卷/无解码NUL。bootargsruntime1。
793快照（16irq/777protectiontimer），startedNs跨度799.873s，V/I均有效：
3895..3997mV、IOPM -588..+15mA。温度和n/l/p三个error全部-9，均无效。
ROUTEv2证实REVIDppid201/APID103/write-ee4，旧要求EE0writer确实会在身份
读取前失败；现已通过identity/REVID，进入4370的DMA状态位检查。
其余映射241->284/243->286/246->289/247->290/248->291，write-ee0。
ACCESS四字段均op=dma-status/addr4370/error-9。由代码能确定DMA_STS mask6
非零（READ_ERROR bit2或WRITE_ERROR bit1），不是SPMI命令超时/NO_OWNER。
旧日志没打印raw且pre/post检查同op，不能断言具体哪bit或首笔pre/post阶段，
也不能证明错误由本轮写入、历史启动残留或外部EE造成。

16次CONFIG全部有效且enable0/pause0/WD1651=62/time1653=8c/SMB1648=0/
SMBCFG1690=2；pluginWD启用、BARK IRQ使能、BITE配置会禁充，不能直接声称
已发生bark/bite或enable0由其造成。此状态与现策略要求WDoff不符，pet尚缺。
11policy均allow0/owned0，无CONTROL；16原发布charging0/CHGR47，仍未启充。
14次IRQ（USBIN10/source4）notification/sample1..14、healthy1；无ERROR/
panic/mux隔离，6USB命令WARN另记。不能把配置SMB0当作并联隔离证据。

本轮发现0152SAMPLE太长：每条自身输出约255字节时截断，丢newline/结束时间，
后继文本粘连。计数按severity标记拆记录，不能用物理行数数样本；开始时间和
错误字段仍可取，completedNs/延迟无法还原。不要称persistent ring损坏/回卷。
实际IOLib_IOLogv无显式256buffer，尚未定位底层截断点，不乱改全局console。
LG fg-memif.c:fg_dma_init调用fg_clear_dma_errors_if_any，以4371bit0处理DMA
错误日志，再设置partition并release；现接口遇错误直接拒绝，缺对应恢复。
NEXT设计有界DMAerror清理：先记录原始4370/4371及pre/post，确认idle/身份/
独立writer、不清理本轮事务失败以静默重试、不把trigger塞稳定RMW；清理后
验证状态、保留partition/其他bit、失败退出。与拆短SAMPLE日志一起实施再编译；
另落实watchdog/pet及三provider，不能为了启充关WD/放宽权限。本轮仅分析/记录，
无新内核补丁/CI/boot/push/设备操作，无需补live或重复0120a1/跑dry。
原解码/JSON/native参考底稿scratch/artifact-0120a1/；完整结果见
docs/BATTERY-FG-OBSERVER-ROUTES.md的0120a1实机结果。

### Previous packaging handoff

**CURRENT: 0120a1云产物已核对，0152三档boot已生成，待用户测telemetry持久日志。**

用户报“01120a1”，实际文件logs/xnu-{kernel,buildtree}-0120a1.gz，统一按0120a1
打包，未另造01120a1编号。原生选定24文件与scratch/fg-observer-0120/verify/xnu
逐字节相同，包含0152；manifest、plainarm64、16Ksegment、FGROUTE/ACCESSv2、
CONFIGv2与独立容量error等字符串、FG/charger/PM通知定义符号均通过。
kernelSHA08d44a9394b0921d555aa00534b68a81ff55037b9f6b1916c84adb3c107dee10。
没有本轮构建日志，不编造CI运行ID。无驱动/CI修改、push或设备操作。
已用--battery-runtime生成out-bs/boot-16k-0120a1-battery-telemetry.img、
boot-16k-0120a1-battery-policy-dry.img、boot-16k-0120a1-battery-runtime-observe.img。
每档35667968字节；再次从最终boot解出FAT校验kernel/ADT/TOML/firmware，参数
无重复，realSID2/mode4/keys/USBACM保留；新telemetryruntime1、dry2、observe无runtime。
完整hash与说明见docs/BATTERY-FG-OBSERVER-ROUTES.md的0120a1节；原生源码、
TOML、verification.json与required-symbols.txt在scratch/artifact-0120a1/。

NEXT用户优先telemetry，启动约1分钟、插拔2轮每状态15～30秒，导出
sweetlog-0120a1-battery-telemetry.bin即可，无需先observe/dry或补USB日志。
先分析FG温度/容量/四fielderror、ACCESS失败地址/阶段和ROUTEowner，再核对
CONFIGwatchdog/enable/SMB。该镜像仍不提交充电策略，三个provider缺口未消失，
不以“插线仍不充”直接判本轮遥测修正失败，不声称已恢复充电。

### Previous implementation handoff

**CURRENT: 0152 FG observer路由分层修正已本地完成，待用户push/云编译；不启充。**

用户“那继续吧”后对照LG spmi-pmic-arb.c的read_apid_map_v5/offset_v5：observer
不要求APIDwrite-ee==本地，RW才要求。0151sp_fg_byte误把所有固定读都经唯一
EE0writer查找；0152改用现有sp_find_apid选observer（first/localpreferred），
原EE0observer窗口/共享锁/timeoutpoison保留。不改旧read白名单或chargerAPI。
会话写之前预检4340/4350/4371唯一EE0writer，v1加41ee；写仍逐笔重新校验。
没有外部RW操作、SRAM内容写、充电策略写或bootarg放行，IRQ/consumer不改。
新FG ROUTE v2首次core/cfg元数据（不映射intr）；ACCESS v2带首个失败字段/
操作/地址/PPID、primary/final错误，WARN按状态变化，清理ERROR仍无条件。
SAMPLE加tempraw/nerr/lerr/perr；CONFIG v2初始/事件打印enable/pause/watchdog/
time/SMB/SMB_CFG原字节+独立error，不额外轮询USB。旧0120缺失败地址，不能
断言硬件确切失败在REVID/MEMIF/DMA哪一步；修正明确代码问题，待现场定位。

新增xnu/patches/0152-mh2lm-fg-observer-routes.patch，3文件；实际完整transport/
FG26场景ASAN/UBSAN通过（foreignREAD/localpreferred/noMEMIFwriter/ambiguous/
poison/denied/timeout/noextraWARN/recovery，及原有grant/ALG/cleanup），实际
batteryruntime采样/发布/保护策略+100PMrace通过；mux/consumer全套+80race、
旧SPMI+10race通过。原生0120上严格apply/24文件bytecompare通过，2ARM64
编译单元通过（已有CIheaders/Linuxclang兼容，不是nativefullbuild）。完整结果
scratch/fg-observer-0120/validation-results.json、verify/xnu、compile.py。
CI仅4个新增sourcegate+版本提示，实际build.sh前缀byteequal；YAML/gate接受
0152拒绝0120。打包脚本runtime加新marker，bash-n通过。无新boot/实机/push。

文档docs/BATTERY-FG-OBSERVER-ROUTES.md记录语义/证据/边界/下一轮。NEXT让用户
push后等新产物，核对并用--battery-runtime打包；先telemetry持久日志即可，
同时取得真实FG结果和watchdog/enable/SMB原值，再决定dry与providers实现。
勿重复旧0120/0119noUSB，不去掉MEMIF写权限检查；控制三provider仍缺未启充。

### Previous 0120 result

**LATEST RESULT: 0120 telemetry持久日志已分析；不充电符合mode1边界，但FG遥测失败，先不测dry。**

用户仅提供logs/sweetlog-0120-battery-telemetry.bin，足够完成本轮分析，无需补
live。SHA7e06beb2344ddef2262d37dfcfe4742888e636392f410a0eb32203a1fe58fc86，
v1/ring4MiB/total472585/flush58，未回卷、解码无NUL。scratch/artifact-0120/
battery-telemetry-persistent.txt及battery-telemetry-analysis.json保存逐行证据。
实际args/runtime=1，617快照（23irq/594protectiontimer），跨度609.888s；
全部V/I valid，3873..3987mV、IOPM -684..+16mA，没有持续正充电电流证据。
23原电池发布charging0、CHGR47/state7；65/66%不能独自证明充电。
15policy均owned0/allow0，无CONTROL提交；mode1本来就不写充电策略。

新发现：617/617 temp-error=-4即SWEET_SPMI_NO_OWNER（sp_charger_route未找到
目标PPID的EE0owner），不是温度0C或总线timeout。temp/nominal/learned/profile
valid均0，30配置寄存器valid=0x3fffffff。SRAM读先经0105/0103/4305和MEMIF/
DMA各地址，日志没打印失败地址，不能断言必然在MEMIF或硬件权限不支持。
capacity/profile独立error未打印，不能硬说它们也都是-4。应查身份只读步骤
不必要依赖writeowner的问题、实际PPID/APID/ownership，再判断会话写权限。
不要去掉MEMIF控制写owner校验或强行启充。
policy0x7d1（8次）/0x7d9（7次，多NO_INPUT）明确包含telemetry/temp/identity/
parallel/input/watchdog/limits；state4是缺温度触发的软件hold，不证明过热。
全部配置valid且有WATCHDOG位，说明1651不满足当前off条件，尚无raw字节和pet。

IRQ21次刷新均healthy1、notifications/samples1..21；14USBIN+7source，未见
CHGR/MSOC/READY自然hit，不能因缺hit撤销历史MSOC验证。无驱动ERROR/panic/
mux隔离；4USB ep0/ep5命令WARN仍在，不凭此归因充电。NEXT先补FG路由诊断/
正确划分observer读取和owned协议写、解析watchdog及三个证据provider；暂缓
policy-dry/observe额外启动，本轮未修改驱动/CI、未push/设备操作。

### Previous packaging handoff

**CURRENT USER TASK: 0120云产物已核对、三档boot已打包，待用户先测telemetry。**

承接bde5519；本轮只核对/打包/记录，没有改驱动、CI、push或操作设备。
logs/xnu-kernel-0120.gz和xnu-buildtree-0120.gz可正常解包；没有0120构建日志。
原生源码24/24与scratch/battery-runtime-0119/verify/xnu逐字节相同，包含
0149～0151；kernel manifest通过、plain arm64、segment 16K对齐，新增runtime/
sample/policy/source/fault/preflight字符串及SRAM/transport/PM通知符号均存在。
kernel SHA256 628c3d8e2fe0baa8ba47a1ccd2cd792d2870cefbcb2a814fca8e2fa0c62801ac。
已运行tools/mk-mh2lm-event-matrix.sh logs/xnu-kernel-0120.gz 0120 --battery-runtime
（实际传绝对artifact路径），out-bs生成boot-16k-0120-battery-telemetry.img、
boot-16k-0120-battery-policy-dry.img、boot-16k-0120-battery-runtime-observe.img。
再次从最终Android boot镜像提取FAT校验kernel/ADT/TOML、无重复参数、firmware
不变；每档35667968字节。详细hash/步骤见docs/BATTERY-RUNTIME-0120-TESTING.md，
机器核对结果和选定源码在scratch/artifact-0120/verification.json及xnu/。

NEXT：用户先boot telemetry，USB live日志+持久日志（保留启动早期SRAM错误），
稳定约60秒，插拔两轮，每状态15～30秒，记录UI和异常；不必先boot observe。
先分析真实温度/容量/MEMIF、事件路由/共享SPMI健康；之后再测policy-dry。
控制档未生成，三个board provider仍缺、fLimits空，不能启充；1/2档有MEMIF
协议写但不写充电策略/FG内容。不得称真实充电恢复或第4～6段已实机验收。

### Previous implementation handoff (0120 packaging supersedes its artifact status)

**CURRENT USER TASK: 补后三段；0151已补可独立实现代码，待一次云编译/实机。**

本轮承接本地13c443f(0150)。按六段计划第4～6段理解，同时补第2～3段前置；
已向用户发异步范围核对，无答复，按该范围实施。没有push/设备操作。
新增 xnu/patches/0151-mh2lm-battery-runtime.patch（基于0150，7文件）：
- bsd/dev/sweet_battery_runtime.h：实际snapshot/limits/controller与纯策略，
  reason位、3s时效、10..40C限制/12..38C恢复、FCC/ICL<=500mA FV<=4.2V，
  单会话60s，动作先disable/配置回读/最后enable，编程中超时也禁用，故障锁存。
- C transport新增sweet_spmi_fg_read固定4字段，Gen4 DMA request/grant/release，
  v1 peek/ALG等待，v2布局，busy/未知rev拒绝，cleanup失败boot sessionpoison。
  SRAM数据只读，MEMIF/peek协议控制确实有写；没有profile写或任意SRAM访问。
- SweetBattery可选sweetbattruntime=1真实遥测/2模拟/3受互锁的控制入口，0默认旧行为。
  新模式要求realSID2+IRQ4，ownworkloop+独立1smonitor；timer不读USB/APSD/TypeC
  状态，不回退插拔轮询。标准IOPM发布实测I/温度、FG容量和有效性；失效移除旧
  温度/电流，容量未知用100比例单位并标记，未知cycle不伪造0；LG温度补偿未实现。
- mux表新增USBIN bit6 source-change、CHGR bit0 error（已有两个APID、rising），
  event0x10/0x20，ALL0x3f；client转发mask同步扩展，mode0原事件订阅不变。
- 新power notifier在ownloop外remove并同步回调；控制提交独立IOLock将shutdown/
  sleep禁充与迟到enable互斥。通知不等batteryloop、不改loop-ownedcontroller，
  bootPMroot缺失拒绝新模式；service停止/IRQfail取消timer禁充，失败显式未确认。

重要限制：fLimits目前初始化为空，没有实际电池ID/上限、实测VBUS/输入能力、
并联隔离证据provider，设备SB_R_BATTERY_ID/PARALLEL/INPUT/LIMITS必然禁止enable。
不能把合成host证据搬到bootarg。控制执行器有代码≠手机已恢复充电。
watchdog目前只接受确认off的条件，不实现pet、不自动关WDOG以跳过保护。
SRAM温度原始未LG补偿、nominal/learned属FG数据不证明实际电池身份。
真正睡眠不自动恢复控制，普通displayoff仍保护监测；完整sleep/PM时序待实机。
第4～6段尚未验收、CHGR正常充电转换未证明；不得声称电池/IRQ整体结案。

本地实际函数ASAN/UBSAN PASS：SRAMv1/v2/busy/unknown/grant/ALG/读/清理失败，
无SRAM内容写、等待释放共享锁；策略所有关键字段失效、迟滞、所有11写步骤
失败、编程中到期、service stop和PM禁充锁；额外100次power通知/enable竞争通过；实际sample/publish移除失效值/
容量比率/monitor no-rearm；新mux source/fault投递与detach；旧mux套件+80race、
旧SPMI+10race、0149diag和loglevel通过。4编译单元ARM64单文件通过，Linux
clang19+既有CI headers/builtin兼容，并非完整原生build/link。24选定源文件
严格apply/bytecompare，YAML/sourcegate接受0151拒绝0150、build.sh命令未变。
底稿 scratch/battery-runtime-0119/{base,edited,verify,compile.py,*tests.txt}。
新调用IOSleep/PMnotifier/ack符号已核对原生0119存在。

文档docs/BATTERY-RUNTIME.md给精确能力/参数/缺口；计划/NEXT/IRQ路线图已更新。
NEXT：可让用户push一次合并0149～0151；新产物下载后验源码/标记并用
 tools/mk-mh2lm-event-matrix.sh ARTIFACT LABEL --battery-runtime
生成同核battery-runtime-observe / battery-telemetry / battery-policy-dry，保留
keys/touch/ACM/mode4。没有受控镜像，尚无新boot路径；不重复0119 noUSB测试。
先取得温度/容量/MEMIF现场数据，再落实ADC5身份+VBUS和并联隔离provider，
之后才开放控制档做实际充电与第4～6段验收。user push/fastboot，不由agent代做。

**LATEST historical charging audit: inherited-enabled hypothesis supported, NO new boot/build needed.**
User corrected framing: XNU never enablescharging, so en0 persistence is expected,
not itself a newlyproven fault. Now requests oldsuccess explanation BEFORE work.
Read-only audit in docs/BATTERY-CHARGING-HISTORY.md; scratch/charge-history-0119/
holds rawdecodes,summary.json,m19-timeline.json,source-comparison.json and
firmware-module-match.json. m19 SHA8071ed68c6b3be36f5efc72f4f3dabec02275d479d7ef8e10d53a1404915da0e,
ring4M/total438739 NOTwrapped/noNUL. Oldwrap disclaimer was wrong for currentfile.
First112/114external0/state7/en1(valid);all248DIAGen1 (247config-ok1,1config0
laterCFG2failure3865;allcurrentok1). Pairedruns121unplugged/state7/en1 ->
65plugged/state3/en1(3730..4316,negativecurrent,63->66%) ->16plugged/state7/en1
(4325..4430,path95/no suspend/ICLconfigured1500) ->46unplugged/state7/en1.
Thus enablewasalready1 atfirstobservation;no0->1event; enable1 NOTsufficient.

Actual0113-battery-ui(noUSB)alreadyfirst113/115en0,124samples,PREmux/batteryIRQ.
Available nativebuildtree0112k5 actualsourceincludes0125;applyONLY0126 then
battery/SPMI/USBbyteequalnative0113. Not exactoldkernelprovenance just fromname.
Oldm19sweetusbunsetdefaults1readonlyprobe,NOTactiveACM;don'tcallallUSBhardwareoff.
Finalbootimages0112k6primary/0119charge/0119noUSB have IDENTICALkernelfield
BootShim+compressedMu+DTB,2111336bytes/SHA80b40a0fd2dbcd79d87f4a7e45d9b93e8ef6ba9d661599f4ee6e4e09df68b1bd.
DecompressedactualFV FULLmatcheslocalmh2QcomChargerDxeLA,ChargerExDxe,QcomChargerCfg.
Binaryenable/disable/exitstrings+APRIORI provecontrolcapability/inclusion, NOT
whichbranchran. EnableChargerWdog1disableonexit meansWDOG,NOTchargerdisable.
StrongestexplanationpreXNUenablefrombootenvironment; exactABL/UEFI/priorOS/
coldwarmreset/source/thermalconditions unknown. Don'timputeAndroidvotesinXNU,
firmwarelock or soleIRQ/gadgetcause. No needoldkernelbootsorAndroidboot.

IMPORTANT currentlogs/sweetlog-0112m20.bin SHA6fffa51f4d791c9eb233fa82ad96a33024d4ace23ca641cb99a1ec0c47ea0c47
byteEQUALSsweetlog-0113-battery-ui.bin(124samples/en0), NOT historicalm20SHA
ef270d05c9d4f770299d679bd8c4e332edcacf21dba271d015dd2b4e3342414a/83charge.
DoNOTclaimcurrentfileproves83chargeorrefutesoldrecord;identitymismatchreason
unknown,doNOTrename/overwrite. Oldm19independentlyconfirmsphysicalcharging.
0149 remainsdiagnostics-only/readypushasbelow;useraskedhistoricalreview,
notauthorizingblind1042enable. Needrealtemp/currentlimits/FV/parallelerrors
beforecontrolimplementation. Userdoespush/fastboot;noneperformedhere.

**CURRENT: 0119 no-usb still DISABLE_CHARGE; 0149 diagnostic validity ready for USER PUSH.**
User follow-up audit: root cause NOT closed; don't call en0 a complete cause.
Workspace docs/INDEX + chapters1/3/5/6 rechecked with actual LGsource:
qpnp-smb5.c:smb5_init_hw enables via DEFAULT_VOTER false;
smb5-lib.c:smblib_chg_disable_vote_callback writes inverse effective disable
to1042bit0. XNU lacks equivalent chargingpolicysetter; mux batterywrites
limitedIRQoffset11..16. This explains no active recovery, NOT who cleared
enable initially or why oldm19/m20charged. Only partial documentchecklist
implemented: ICLmode1366/1342/1365,settled1108,temp,FCC/FV,parallelstate and
controlwritepermission/readback remain unresolved. SMB_EN1648=0 means no
softwareoverride, NOT proofSMB1355disabled (reference disablefalse also
clearsbit3). BATTERY-DRIVER-NEXT has explicit audit/status/sourceanchors.
Don't claim whole chargingplan followed or 0149fixescharge; don't reduce
all missingproof to SMB_CFG or waitforCI for source/historyanalysis.

User supplied logs/sweetlog-0119-battery-charge-diag-no-usb.bin, SHA256
`b045d42e38a41b27e36a99c469e55f9b25794186007eccae2acbdedbbec11b96`.
Header v1/ring4194304/total255185/flush47/notwrapped/no decodedNUL. Exact
decode/analysis in scratch/artifact-0119/charge-diag-no-usb-persistent.txt,
analyze-charge-diag-no-usb.py,charge-diag-no-usb-analysis.json. Actualargs
sweetusb0/keys1/realgauge/mode4/diag1, no vbus/usbstep, no USB/ACM init.
11samples(initial+armsync+9USBIN) allCHGR47/state7/en0/pause0/cfg2=2c/smben0,
allconfig/current-ok1. IBATTua65917,488,0,463378,463378,465820,465820,580566,
0,421386,0: no negative charge direction. Connectedpath95/validinput1/no
suspend/ICLconfigured1500mA/AICLd4. Gauge b3x9->b2x2, both70%;3910..3990mV.
No active charging sampled with gadget OFF: not solely gadget under captured
conditions. Same source/cable not confirmed, cannot eliminate inherited
firmware state or claim unique cause. Initial112/114 stillprecedesboard138.

Bothgroupsattached;9USBINrefreshes2852/2942/2956/2975/2988/3001/3480/3556/3576,
notification/sample1..9/allhealthy1/externalalternates->0. NoCHGR/MSOC/READYhit
here; previous3independentMSOCpass STILL STANDS. No2spoll. Keyspower3pairs/
down1pair/noup. Touch10completepairs+lastdown3583atEOF(11down/10up), nosequence
error: neither call stuck nor fullybalanced. Displayoff3225/on3369. No scoped
ERROR/WARN/muxfailure/isolation/panic; incomplete tail cannot exclude user GUI
or hang anomaly. Async question asks exact observed anomaly; no answer yet.

SOURCE FACT: sweet_spmi_read whitelist EXCLUDES peripheral0x15, so TypeC
1506/1509/150b return-1 beforelock/MMIO, NOT evidence of hardware permissions.
Oldpolicy&& chain skips subsequentSMB_CFG1690 and emitsfakezeros. Separate
DIAGconfig-ok1 DOES validateSMB_EN1648=0. Don't widenwhitelist blindly, write
1042enable, readSRAM/profile or addpollfallback.

0149 xnu/patches/0149-mh2lm-battery-diagnostic-validity.patch fixes ONLY
SweetBattery read-onlydiag: independentexistingfields, WARNaddress/error,
rawunknown/decoded-1, pergroupmasks, invalidIBATTunknown. Sameacquisition/
publication/IRQs/properties/transportboundary; formerlyskippedlaterreads now
attempted. CI addsfourrequiredsource markers0139..0149; actualbuildcommand
unchanged. Newtools/tests/test-mh2lm-battery-diagnostics.py actualfunctions
ASAN/UBSAN failureinjection PASS; transporttest addsactualTypeCwhitelistreject
beforelock/MMIO, suite+10concurrencyrunsPASS; actualmux/consumer suite+80race
runsPASS. YAMLparse/sourcegatePASS0149/FAILold0119 as intended. Strictapply,
23filebytecomparison, ARM64singlefilecompilePASS; localLinuxcompatflags,
NOTnativefullbuild/link. snprintf alreadyexportednative0119. Scratch exact
base/edited/verify/object/compile/tests: scratch/battery-diag-validity-0119/.
Docs BATTERY-IRQ-TESTING/BATTERY-DRIVER-NEXT/IRQ-FRAMEBUFFER-PLAN updated;
new BATTERY-DIAGNOSTIC-VALIDITY.md recordsmasks/limits.

NEXT: userpush thencloudbuild; awaitnewnumberedartifacts, verify0149 and
packoncemultiplechargeprofiles. Prioritize independentlyvalidSMB_CFG and
inheriteddisableconfiguration;0149doesNOTclaimchargefix. Don'trepeatold0119
primary/noUSBboots or re-debugUSBlogging. Userfastboots/pushes; agentdoesnot.
BatteryIRQgoal remainsincompletependingCHGR; framebufferafterIRQacceptance.

### Previous handoff (superseded by no-usb result above)

**0119 USB lifecycle clarification (source review, no driver change).** User
asks whether gadget exists/alwaysenabled vs cable-triggered. DirectDWC3 driver
implements device-side CDC-ACM itself, no Linuxgadget/configfs framework; host
CDCACM creates ttyACM. sweetusb2 init once after default15s, sets persistent
QSCRATCH VBUSVALID+SWSESSVLD for sweetvbus1, startsRUN_STOP; no PMICUSBIN->USB
session start/stop link. IRQworker sleeps when idle, not polling. disconnect
only logs, doesn't reset configured/dtr/tx_busy or stopcore; reset DOES reset
those flags. RuntimePM/full reconnect lifecycle missing, causal role in old
enumeration failures/charging not established. Architecture doc updated;
noUSBcontrol below remains next test. Don't conflate forced controller session
valid with physical PMICVBUS or supply/chargeenable.

**CURRENT0119 charge-diag: NO ACTIVE CHARGE SAMPLED; next existing NO-USB CONTROL.**
User supplied logs/sweetlog-0119-battery-charge-diag.bin, SHA256
`36f445679818886a45e53eb1b44baa11750f3ec65efc6575d373ea1c44057875`.
Header v1/ring4194304/total259899/flush49, NOTwrapped/no decodedNUL.
scratch/artifact-0119/charge-diag-persistent.txt,analyze-charge-diag.py,
charge-diag-analysis.json preserve exact evidence/counts/assertions.
Actual bootargs mode4,keys1,realgauge,ACM2/vbus1,diag1; group1/eventsf and
group0 attached, no2s poll.9samples allCHGR47/state7/en0/pause0/cfg2=2c,
allconfig-ok1/current-ok1; matchedIBATT uA488,488,0,460448,460448,539550,
488,0,466308: no negative charge-direction values. Gauge71->70%.
ConnectedINPUT path95/USBselected+valid, unsuspended, ICLconfigured1500mA,
AICLenabled; NOT actual measured inputcurrent. EarlyAPSD0 afterplug doesn't
prove permanently incomplete; laterMSOC sample3428APSD21/done1/slowplugin1.
Immediate observable blocker: charging enable remains off. Can't claim all
walltime sampled, positive edge currents aren't steady supply-share proof.

IMPORTANT initial112/114 state7/en0 occurs BEFORE board138/mux142 and
USBcontroller1394. This doesn't support IRQ arm or gadget init causing initial
disable; gadget may still affect later recovery, and boot/firmware/source
conditions remain unresolved. No enable writes were added. SWEET BATT SMB
read-ok0 all9times; don't infer TYPEC/SMBselect zeros as valid. SeparateDIAG
config-ok1 DOES validate its own smben0 at0x1648;0x1690 select remains unknown.
UIcurrent-valid0 is lack of standard current publication, NOT IBATTreadfailure.
PM6150B old summary string is stale; REVID still proves PM8150B.

6USBIN hits/refreshes3003,3067,3080,3093,3465,3600 external0/1/0/1/0/1;
MSOC3425/refresh3431 events4 external1 drops71->70.7notifications/samples1..7,
allhealthy1, notify-publish18011..18351us (diagnostic extra reads, NOT UIlatency).
NoCHGR/READYhit. Keys4balancedpairs(power3/up1), no volume-downHID recorded;
touch5balancedpairs/single slot,lastIRQ136/empty0. Onepoweroff/on, input
recorded up to finalreplug (no laterHID in capture). No scopedERROR/muxfailure/
isolation/panic;6existingUSBWARN; onlyearlyUSBcounter128/2ACMconfigs. Normal
SpringBoard shutdownrequest3624, not completed service termination proof.

NEXT USER: out-bs/boot-16k-0119-battery-charge-diag-no-usb.img, existing
35667968bytes; SHA rechecked
`90655d8c533a0e90d09705938b797dd41617a1a35dc55941ae84cde2a761ce5c`.
Same0119 nativekernel/mode4/realgauge/diag/keys/touch; sweetusb0/no vbus or
usbstep. NO ACM expected: this is charge comparison, not USBlog re-debug.
Use SAME known-good independent charger/cable as primary; desktop->unplugPC
20..30s->charger3..5min->unplug20..30s/optionalreplug. Export
sweetlog-0119-battery-charge-diag-no-usb.bin via established persistent path;
record source/cable/UI. Exact power source still not confirmed by user, don't
infer PC vs wallcharger fromexternal. No need re-run all/preflight/baseline,
push/cloudbuild or enablewrite. Ifchargepositive then separately isolate
gadget/override vs inheritedbootstate; ifstillstate7, not solely gadget under
this condition, proceed per-field read validity/chargeinit investigation.
Updated testing/nextplan/IRQroadmap and handoff only, no driver/CI/boot edits.
BatteryIRQgoal remains incomplete pendingCHGR; previousMSOC pass stands.

**CURRENT0119 mode4 MSOC IRQ PASS; resume battery/physical-charge work; diagnostic boots READY.**
User supplied logs/sweetlog-0119-battery-irq-all.bin and asks to continue battery
driver if acceptable. SHA256
`3ede5ea87e447b1b45e54627128b1361906112af0f4dc54b3468b11c567351aa`.
Header v1/ring4194304/total288962/flush86; not wrapped/no decoded NUL.
scratch/artifact-0119/{all-persistent.txt,analyze-all.py,all-analysis.json}.
Actual args keys1/realgauge/ACM2/vbus1/mode4; NO sweetbattdiag yet.
139..154 USBIN269/CHGR266/MSOC+READY283, group1/eventsf attached/armed/no2s
poll; group0 keys attached160.4USBIN refreshes3413/3431/3474/4123,external0/1/0/1.
INDEPENDENT FG MSOC hits3604/3824/4020, IRQ20/21/22, refresh3606/3826/4022,
events4, raw0xbc/0xb9/0xb7 ->73/72/71%, all unplugged with NO USBIN event
between.7IRQ notifications/samples1..7 allhealthy1; notify-publish7269..7399us.
9total samples(initial+arm-sync+7IRQ), allCHGR47/state7/charging0, voltage3910..
4037mV. No CHGR/READY hit. FG end-to-end normal path accepted, unknown firmware
threshold NOT blocker to demonstrated delivery; don't add SRAM reads for it.
UI tracing logs only flag transitions, not each percent; actual publish calls
updateStatus unconditionally, so absent BATT UI line on MSOC isn't missed publish.

Keys11balanced pairs(power5/up2/down4),touch9balanced pairs/single slot, last
touchIRQ158/empty0; input after finalreplug present.1power off/on covered,
NO cable wake in THIS log(mode3 covered it). No scopedERROR/muxfailure/
isolation/panic;14existingUSBWARN ep0/5cmd6status1.USB onlyearlyhits128 and2ACM
configs, can't claim full later enumeration. No completedshutdown/stopfree
evidence. BatteryIRQ goal remains incomplete pendingCHGR; READY stable/uncovered.

NEXT: same native0119 kernel + existing read-only sweetbattdiag1, preserve
mode4/input/ACM. Extended packer with --battery-charge-check; actual generated
and independently parsed final Androidv1/FAT/kernel/16KADT/TOML/unique selectors
verified, Bashsyntax/diffcheck passed. No driver/CI change or cloudbuild needed.
out-bs/boot-16k-0119-battery-charge-diag.img (PRIMARY):35667968bytes
`c33d8d49eaffdfeb8859f9a211afc2599e791c7599833790bf3f72518e711f41`.
Optional ONLY IF NEEDED out-bs/boot-16k-0119-battery-charge-diag-no-usb.img:
`90655d8c533a0e90d09705938b797dd41617a1a35dc55941ae84cde2a761ce5c`;
same mode4/gauge/keys/touch/diag, sweetusb0 with vbus/usbstep absent, noACM.
Do not require both tests up front. charge-boot-manifest.json,
verify-charge-boots.py, charge-boot-verification.txt in artifact-0119.
PRIMARY test: desktop/input, unplugPC20..30s, knowncharge-good wallcharger
3..5min, unplug20..30s/optionallyreplug; export
sweetlog-0119-battery-charge-diag.bin, live optional. Capture CHGR state/IBATT,
CHGR IRQ/events2 and physical charge vs VBUS only. IRQdiag isn't periodic;
no new sample on quiet cable doesn't alone prove failure. Optional async
question asked whether current0119 source is PC, prior m19/m20 charger or
different charger; answer not yet provided. Don't assume source from logs.

New docs/BATTERY-DRIVER-NEXT.md records resumed plan: recover charge evidence
first, then independent per-field read validity (existing DIAG/logChargePolicy
short-circuit chains can print unread zero fields; SMB/read-ok0 is invalid),
real IBATT/temp publishing with validity/sign/age, pack profile/capacity, then
separate charge arbitration/control. Oldm19/m20 actualcharge state3/en1/-2.2..
-2.3A already proved; do NOT claim charge neverworked or blindlywrite0x1042.
No new PMIC writes/SRAM/profile/pollfallback. Updated testing/hardware/roadmap/
architecture/consumer/isolation docs. User pushes/fastboots; no device actions.

**0119 ARCHITECTURE SNAPSHOT refreshed (documentation only).** User asks current
driver architecture. CURRENT-DRIVER-ARCHITECTURE.md now leads with0119 service
tree, IRQ/data paths, shared transport locking and failure/verification limits;
older0114a15..0118 descriptions explicitly marked historical. Checked actual
0119 board/mux/client/keys/battery and USB/touch constants. Board independent
ofUFS; mux+keys IOService matched nubs, battery IOResources/IOPMPowerSource,
per-consumer registry connection and typed attach/detach;7static routes/2groups.
GIC remains board callbacks (no generic IOInterruptController/filter), USB/touch
still event-driven BSD workers. Display remains software-vsync/CPU-copy and
charger policy inherited. No kernel/CI/image changes; git diff --check passed.
Next phone action remains existing mode4 below; no push/build/device action.

**CURRENT0119 mode3 routes/coexistence PASS; CHGR EVENT UNCOVERED; next existing mode4.**
User supplied only logs/sweetlog-0119-battery-irq-charger.bin; no live USB log.
Persistent sufficient; don't request redundant live/baseline/no-keys retest.
SHA256 `26f7a12e66338e12efde80ed0c834a444bb9256b208ca1bae1f55ce950f40629`.
Header v1/ring4194304/total317377/flush47, not wrapped, decoded no NUL.
scratch/artifact-0119/charger-persistent.txt, analyze-charger.py and
charger-analysis.json preserve counts, hashes, line evidence and assertions.

89/98 identity preflight1/mode3;139 USBIN APID269,148 CHGR APID266;
149 group1/events3 attached,152 armed/no2s poll. Keys prepared on own loop,
153..160 three routes/group0 attached. Actual bootargs keys1,realgauge,
ACM2/vbus1,batteryIRQ3.20 distinct USBIN hits3444..4395 and20 IRQ refreshes
3448..4399, notifications/samples1..20, all events1/healthy1; external0/1
alternates ten cycles, each updateStatus complete. Notify-to-publish7103..7780us
NOT cable/UI latency. No runtime battery-chgr hit or mask containing2.
22samples (initial+arm-sync+20IRQ), all CHGR47/state7,charging0,gauge74%,
3953..4040mV. RawMSOC0xbe21times then0xbd once; actual raw SOC changed but
integer percentage unchanged. Mode3 doesn't subscribe FG, no FG conclusion.
CHGR configuration/coexistence only accepted; no sampled natural CHGR
transition, cannot distinguish absent transition vs failed CHGR delivery.
Do NOT claim real charging, complete battery IRQ or change IRQ polarity/
charger control merely because CHGR had no hit.

Keys32 balanced transitions power9pairs/up3/down4; touch54 balanced pairs
across5slots, lastIRQ629/empty0. All recorded key/touch sequences valid,
no0clients, lastkey4474/lasttouch4480 before SpringBoard shutdown4489.
Displayoff6/on7; e.g off3968/unplug4035/on4045 and off4064/plug4078/on4090,
input continues. No scoped ERROR/mux failure/isolation/panic signatures.
13WARN all existing USBep0/ep5cmd6status1. Only early USB counter hits128/
empty0,TX0/1err0 and2ACM configurations: NOT proof every later cable cycle
enumerated/logged. No live supplement needed to assess captured battery IRQ.
Native service stop/free and0148 fault isolation still unexercised; existing
FB lockscreen rendering/deep-sleep remain outside this test.

NEXT USER: same-kernel out-bs/boot-16k-0119-battery-irq-all.img (already
packaged/independently verified; SHA rechecked this turn)
`320ff213e595a3aea0cc80e1eec5f5135f04921e9f4e7f3242d389e54e85c1dd`.
mode4/events0xf adds FG APID283 MSOC_DELTA/SOC_READY, keeps keys/gauge/ACM
and fixed USB/touch IRQ. Check desktop/keys/touch, one cable cycle/off-plug
wake; then unplugged normal use with a quiet cable interval, initially
10..20min observation (NOT guaranteed threshold; firmware MSOC delta unknown).
Export sweetlog-0119-battery-irq-all.bin; live optional. Require FG attach,
mode4 arm and independent MSOC hit/mask4; SOC_READY8/CHGR2 only if actually
observed. No SRAM/profile/charger writes to manufacture IRQ, no polling
fallback. CHGR stays pending while testing FG independently. No rebuild,
push or device actions this turn; driver/CI unchanged. Updated battery test,
IRQ roadmap, consumer and isolation docs; battery goal remains incomplete.

**CURRENT0119 combined keys+USBIN PASS; next SAME KERNEL batteryIRQ3 boot ready.**
User reports test felt normal. Logs/live-0119-board-startup-usbin.txt SHA256
`e9c7f5c3badc8fde7d0ade276db19f88dd77939573bd8c4a068f7a01f7df1b86`;
logs/sweetlog-0119-board-startup-usbin.bin SHA256
`ecda21c3e3c4f7658164f96dd6c89235b5666cac5d095ffeacd1124e3bc64743`.
Persistent header v1/ring4194304/total329685/flush37, NOT wrapped, no NUL in
either capture. Decoded scratch/artifact-0119/usbin-persistent.txt; actual
bootargs keys1/batteryIRQ2, realgauge,ACM2/vbus1. Live has no BATT IRQ records
and covers early interaction only; persistent supplies startup and later
cable/sleep/wake. Do not require live recapture or re-debug USB logging.

Persistent139..160 group1 attaches first, keys prepare on owner workloop,
then group0 attaches/three IRQ routes healthy. Original0118 SET_TYPE readback
failure NOT reproduced. No scoped driver ERROR, readback failure, isolate,
timeout/storm or panic signature. This does not prove unique hardware cause
of0118 or native failure-containment path. Four DISTINCT USBIN hits at
3825/3996/4200/4406, IRQ numbers34/42/43/55, notifications/samples1..4,
external0/1/0/1 and updateStatus healthy1. Two complete cycles established.
Notify-to-publish7504/7217/7219/7444us, NOT physical/UI latency. Startup+
arm-sync+4IRQ gives6 samples, all gauge75%, CHGR0x47/charging0; stable SOC
and USBIN sensing proven, NOT real charging or CHGR/MSOC event validation.

Keys through last touch:56 balanced HID transitions, power10pairs/up10/down8,
combinations and standalone sleep/wake present. Tail4644 extra volume-down
press before SpringBoard initiated shutdown4655;4772up/4773down with0clients,
final tail incomplete; DON'T claim entire file fully balanced or infer key
stuck solely from end-of-shutdown. Touch40balanced pairs across5slots, last
IRQ563/empty0. Displayoff3176/3957/4341 and on2218/3196/4011/4361;
replug3996 beforedisplayon4011 while no intervening powerHID. Input after
cable events/wakes continues. USB last1920/empty0 TX1276/1281err0,
ACM configured6times.12WARN all existing ep0/ep5cmd6status1. Do not claim
err0 guarantees lossless live logging or native service stop/free exercised.
Analysis script+hashes/counts/line refs in usbin-analysis.json and analyze-usbin.py.

Generated SIX additional temporary profiles from same0119.gz using existing
--battery-irq packer; no source/kernel rebuild/push. All independently parsed
Androidv1; embedded FAT/kernel/16KADT/TOML byte-match, selectors unique, real
gauge/keys/ACM retained, defaultINFO except explicit all-debug3. Records and
scripts in artifact-0119/battery-{packaging.txt,boot-manifest.json,
boot-verification.txt},verify-battery-boots.py. Native kernel remains
SHA81074d86...6c3c3ae2 (full SHA below).
NEXT USER: out-bs/boot-16k-0119-battery-irq-charger.img, SHA256
`f6a80c553288cb37523d4d4d7fa972aab85c5e62795005c9fafc3ff9e4006ad3`.
keys1/batteryIRQ3 events3 adds CHGR to USBIN, USB/touch IRQ fixed, ACM enabled.
Test desktop/three keys/touch,2unplug5s/replug5s cycles, leave connected for
natural charge-state change, one display-off plug/wake. Save
live-0119-battery-irq-charger.txt AND sweetlog-0119-battery-irq-charger.bin.
Require group1 includes battery-chgr APID266 and arm mode3, distinguish real
CHGR hit/events2/charger-state transition from USBIN-only. If CHGR staysstate7
without natural transition, mark event uncovered, don't write charger control
to manufacture event. Inspect before already-prepared mode4 FG test. No
baseline/no-keys repeat or cloud build needed. IRQ big goal remains incomplete
until CHGR/MSOC validation; framebuffer still later. No device actions/push.
Updated BATTERY-IRQ-TESTING, IRQ-FRAMEBUFFER-PLAN, MUX-CONSUMERS and
MUX-GROUP-ISOLATION docs with current phone acceptance/limits/next profile.

**CURRENT0119 NATIVE VERIFIED; direct combined keys+USBIN BOOT READY, phone pending.**
User downloaded logs/xnu-kernel-0119.gz and xnu-buildtree-0119.gz; no CI run ID
or build log supplied. Extracted kernel SHA256
`81074d86d567d0f4b7804fdf983f108aa7e50a53f6b6b91292a581466c3c3ae2`
(17340640 bytes) matches artifact SHA256SUMS. All23 relevant final sources
byte-match group-isolation-0118/verify/xnu, including0146..0148 and unchanged
USB/touch/UFS/board sources. Current CI gate passes. Native ARM64 Mach-O
executable MH_NOUNDEFS, no undefined symbols. Confirmed isolateGroup,
prepareBackend, registry callbacks, typed attach/detach, transport lock/log
predicate symbols; old global consumer C bridge absent. Native compile/link
and patch inclusion verified, NOT phone root cause or isolation timing proof.

Generated via tools/mk-mh2lm-event-matrix.sh ...0119.gz 0119 --board-startup.
All under out-bs/ (prefix boot-16k-0119-):
- board-startup-usbin.img keys1/batteryIRQ2: DIRECT NEXT TEST
  `c79a86a52d7cbd04fdd5d198ac3244df8040aa87dfe4cc5ed574e324786b2d05`
- board-startup-baseline.img keys1/batteryIRQ0: optional anomaly control
  `a99b8ac54919a71ee15ee2d7a3c8f7c3090cd82b6b3fb083839e516b41058033`
- board-startup-no-keys.img keys0/batteryIRQ2: generated by standard packer,
  NOT required repeat per user steering
  `5c1d0ef9b4ba7d44abab83547a8850413f1edc97acda28286871b8be062ec900`
All35667968 bytes. Independently parsed Android v1/page2048 final images;
loader blob and embedded FAT match inputs, FAT-extracted native kernel,
16K ADT and TOML byte-match. Arguments unique/correct:8CPUs, real gauge,
ACM2/vbus1, keys/batteryIRQ selectors as above, defaultINFO. Fixed USB/touch
IRQ preserved. Source/kernel/symbol/header/boot manifests and verification
scripts/results under scratch/artifact-0119/. Outputs untracked.

NEXT USER: start existing ACM reconnect listener before fastboot boot direct
out-bs/boot-16k-0119-board-startup-usbin.img. Check desktop, all3 keys and
touch; two unplug5s/replug5s cycles with input in between; standalone power
screen off/wake then touch. Export live-0119-board-startup-usbin.txt AND
sweetlog-0119-board-startup-usbin.bin (early attach/readback diagnosis).
Need group0/1 registry attach, keys healthy and balanced HID, USBIN real
transitions/updateStatus, USB/touch and sleep/wake. Successful containment
of a failed group is NOT full functional acceptance. Don't force service
termination/faults on phone or require no-keys/baseline first. Existing FB
redraw glitches are separate. No push/cloud/device actions this turn;
only artifact checks/boot packaging/doc updates. Docs/MUX-GROUP-ISOLATION.md
and IRQ-FRAMEBUFFER-PLAN.md updated to native verified, phone pending.

**CURRENT0146..0148 READY FOR USER PUSH; per-group rollback isolation + log levels.**
Implementation/tests/docs committed `1c9598b` (0148), after0147 `232b8b3` and
0146 `93ccaeb`. User requests removeGroup failure prefer disabling only faulty
group and ERROR always / WARN below / INFO lowest normal running severity.
New0148-mh2lm-mux-group-isolation.patch changes5 XNU files. On attach rollback
or detach cleanup failure, original removeGroup tries restoration; on failure
isolateGroup under mux gate (GIC masked) closes each group's unique local
APID ACC_ENABLE, clears only group bits, requires access0/pending0 readback.
No SPMI command in isolation, poisoned RW lane not reused. Preserve other
active groups if successful; sticky isolatedGroups mask (IORegistry
SweetMuxIsolatedGroups, lifecycle degraded), no reattach/retry/fallback this
boot. Any route from another group resolving an isolated APID also rejected.
Cannot independently gate a cross-group active APID, gate readback failure or
stuck pending -> original global disableHardware. Runtime ack exhaustion,
unknown/stuck IRQ, storm, shared GIC/PDC failures remain global: this change
is specifically failed rollback/detach containment, not blanket runtime policy.
Normal successful cleanup unchanged; not complete PMIC state restoration.

Existing ERROR/WARN/INFO/DEBUG macros already0140. ERROR now unconditional
sink, no severity predicate. Battery and SPMI raw IOLog/printf converted to
macros: init/map/timeout/static-fallback failures ERROR, diagnostic inability
WARN, startup/samples/normal diagnostics INFO. Keys/mux/client already graded.
0 ERROR;1+WARN;2+INFO default;3 explicit DEBUG. Whole XNU/userland and per-target
logging management NOT claimed completed; future logging goal remains.

Verification scratch/group-isolation-0118/: fresh0148 strict apply/byte compare,
complete0142..0148 final replay matches23 files;8 ARM64 objects compile, using
existing subsystem flags/Apple builtin local adaptations, NOT native CI/link.
Registry suite actual mux/client/keys class + selected battery methods passes:
SET_TYPE stale+restore-write failure isolates keys/battery remains sampled;
detach EN_CLR failure or RW poison timeout isolates battery/keys still live;
queued IRQ detach rearms surviving keys; gate refuses off or pending sticks ->
global shutdown; synthetic shared active APID refused; another group cannot
reopen isolated APID. Original lifecycle/hardware suite and80 extra races,
ASAN/UBSAN + explicit object/notifier/MMIO counts pass. transport-tests covers
actual init/key+battery lock/error paths and10 extra concurrency runs.
New test-mh2lm-log-levels.py compiles actual macros+threshold in C/C++, checks
all valid/invalid/default levels, forced all-filter-off still emits ERROR,
filtered args not evaluated, six PMIC modules have no raw logging calls.
CI source gate accepts0148/rejects0147; CircleCI build COMMAND UNCHANGED,
only markers added. Packer board/battery requires new isolation string.
Shell/Python syntax and diff whitespace pass. No native build/phone proof of
ACC_ENABLE/pending isolation yet; no new native artifacts or boot generated.

Docs/MUX-GROUP-ISOLATION.md current policy/severity/tests. MUX-CONSUMERS,
SPMI-TRANSACTIONS and IRQ-FRAMEBUFFER-PLAN updated; SPMI-CALLSITE-AUDIT remains
explicit0147 snapshot/hashes/line numbers, top links0148 changed behavior.
NEXT: user push current HEAD -> one native kernel/buildtree -> direct keys1 /
batteryIRQ2 combined real-gauge + ACM test, fixed USB/touch IRQ, all3 keys,
USBIN unplug/replug, sleep/wake. No required skipped no-keys repeat; baseline
optional anomaly control. Existing0118 lacks0146..0148. Do not claim original
SET_TYPE42 hardware cause proven or native isolation validated. No push/cloud/
device actions performed. Preserve user-owned push/fastboot procedure.

**0147 source audit requested by user: no new kernel behavior change.**
Verified exact consumer-prepare-0118/verify/xnu at implementation232b8b3.
Added docs/SPMI-CALLSITE-AUDIT.md, linked from MUX-CONSUMERS.md: all95 static
call sites (battery51 incl5 helper calls, keys19 incl4 wrappers, mux25 incl
ack wrappers) with function/line/expression, source hashes and scope.
Correct architectural wording: NO mux spmiRead/spmiWrite public API exists;
battery sweet_spmi_read, keys keys_read, mux private readPeripheral/
writePeripheral share one transport IOLock. Hard IRQ sends no SPMI command;
local controller IRQ/ACC MMIO isn't a PMIC transaction and isn't all locked.
No fReady field in these23 final sources: sp_ready is bool with atomic
release/acquire publication, init plain read under lock; mux state atomic
unsigned, READY event delivered on consumer loop; client lifecycle bools
under IOLock/gate, keys backendPrepared synchronized by stop/drain.
Prepare NEVER configures GPIO: validation fail stops/unmaps before attach.
Attach config rollback exists but is BEST EFFORT: restore type/high/low bits
and unused APID oldAccess, disable IRQ, clear latch; doesn't restore original
PMIC enable/latches or read-verify restoration. Failed/poisoned write cleanup
stops entire mux. Do not call this complete hardware rollback.
Only documentation/NOTES modified; no new test run needed for read-only audit.
Implementation0146+0147 and next direct native keys+USBIN test remain as below.

**CURRENT0146+0147 READY FOR USER PUSH; direct combined keys+USBIN next.**
User proposes lightweight consumer matched callbacks/own-loop preparation or a
mux whole-initialization queue. Chose own-loop preparation; raw notification
must not even call attachConsumer (it writes hardware). Prior0145 already
retains/queues matched/terminated and attaches on consumer loop, while mux
gate serializes route setup. New0147 committed `232b8b3` moves synchronous
sweet_keys_prepare out of SweetPmicKeys::start into optional per-client
SweetMuxPrepare on owner loop AFTER live match and BEFORE attach/READY.
start only installs loop/connection; initial healthy=false. Preparation reads
and validates firmware GPIO configuration, does NOT reconfigure GPIO/add
calibration/charging policy. Battery early real sample remains for startup/UI;
postmatch arm-sync/event sampling stays battery loop. All SPMI transactions
use0146 shared controller lock; do NOT add a mux whole-init queue/global
consumer bridge. Independent initialization work may overlap; hardware
transactions cannot. Multi-command atomic init only if later hardware needs it.

Prepare runs without registry/mux/connection locks; client stop marks stopping,
removes notifiers, then drains consumer gate. After prepare, recheck stopping,
providerGone, mux/owner inactive before attach. Failure terminates binding;
no config retry/poll fallback. Owner resources/context remain valid until loop
completion and backend stop. Existing prepared-backend/worker/HID teardown
remains. Hardware cause of0118 SET_TYPE42 readback still UNPROVEN; unified lock
fixes a concrete code gap, not yet a confirmed native phone resolution.

Local verification in scratch/consumer-prepare-0118/: fresh0147 apply and
byte compare; prior strict0142..0146 final chain +0147 matches all23 sources.
8 ARM64 Mach-O TUs compile (real subsystem flags/local Apple builtin/path
adaptations, NOT native CI/link proof); compile-results.txt. Actual registry/
client/keys C++ harness + selected battery methods passes ASAN/UBSAN and
explicit object/notifier/MMIO counts. Added delayed preparation/no hardware
in immediate matched callback; late match; pre-match cancel; prepare failure
with battery still live; provider terminate/owner stop DURING preparation,
no attach/READY/worker; existing worker-fail/type-mismatch/parallel regressions.
80 extra notification/IRQ/preparation/teardown races pass, registry-tests.txt.
C key/backend/transport unchanged since0146, prior tests still recorded above.
CI source gate accepts0147 verify tree and rejects0146 base at new key marker;
ci-gate-results.txt. CircleCI build COMMAND UNCHANGED, only source gates.
Packer battery/board profiles require new key preparation marker; bash syntax,
Python syntax/diff whitespace pass. Docs/MUX-CONSUMERS.md records boundaries,
SPMI-TRANSACTIONS.md and IRQ-FRAMEBUFFER-PLAN.md updated.

NEXT: user pushes current HEAD including0146 `93ccaeb` +0147 `232b8b3` ->
fresh native kernel/buildtree -> direct combined keys1/batteryIRQ2 profile,
real gauge+ACM, fixed USB/touch IRQ. Validate both groups attach, all3 keys,
touch/USB unplug/replug, sleep/wake. Baseline optional anomaly control;
do not require skipped no-keys repeat. Existing0118 artifacts lack0146/0147;
no corrected boot image yet. No push/cloud/device action performed this turn.

**CURRENT0118 combined FAILED keys attach;0146 transport fix/diagnostics ready for USER PUSH.**
Implementation/tests/docs committed as `93ccaeb`; ready for user push, not pushed.
User reports keys dead and asks if boot argument omitted. Persistent evidence
logs/sweetlog-0118-board-startup-usbin.bin SHA256
`c0cd6907da5bba7a12bfa656682384f7915794c03a2ace84ffec6dfb7e665578`;
header v1/ring4MiB/total277831/flush37,not wrapped. Decoded artifact-0118/
usbin-persistent.txt. Bootargs line261 have keys1/batteryIRQ2; existing image
SHA6702a2d8...e273dd rechecked. NOT missing args. Battery subscribes group1 at
116-117, mux service starts133-136, battery APID269/group1 attach+arm147-150.
Keys registers watcher138; power/down APID248 and up261 configure151-153.
Line154 volume-up readback type42/high1/low1/enable1, required edge bit0 absent;
group0 rollback then attachConsumer result0xe00002ca at156. NO keys armed/HID
or worker started. group1 survives: IRQ1/2 at3456/3469 refresh external0/1,
healthy1. This is group0 initialization failure, not global mux stop. Original
log lacks expected initial trigger values, channel/read success detail and
per-command status, so cannot prove a dropped write or stale read mechanism.
Initial battery samples report charging1 at66->67%,4164mV; after unplug/replug
charging0,67%,4046/3990mV. No current diagnostic/physical charging proof.

Found concrete synchronization gap: battery observer/RW take sp_lock; key
observer (mux/C worker) and key RW bypass it. Separate workloops permit key
setup concurrent with battery arm-sync sampling. LG reference spmi-pmic-arb.c
uses ONE controller lock around read/write transactions even across APIDs.
New additive0146-mh2lm-spmi-transaction-serialization.patch (4 files) makes key
observer/RW use same thread IOLock, serializes init via CAS-published lock and
locked attempted/ready/mappings, initializes transport from late mux/keys too.
No battery-first dependency, no new earlier hardware phase. RW timeout poisons
all routes sharing APID including key lanes. Log write/observer errors plus
expected/mask/access/read-ok on config failure. Preserve strict readback,
group rollback, registry consumer lifecycle, IRQ-only behavior, existing ack
retry, USB/touch and charging/SOC policy. No new configuration retry/fallback.
This fixes a real code gap; whether it resolves this exact phone failure is
UNVERIFIED until new native build+phone. Do not claim hardware root cause proven.

Validation: exact0118+0146 strict apply/byte compare and full0142..0146 chain
matches23 final source files;8 ARM64 objects compile with subsystem flags.
Registry suite enforces locks on key observer/RW; injected SET_TYPE still42
after DONE must reject/rollback group0 while USBIN remains live. Existing
identity/lifecycle/40race/APID/rollback/storm/timeout suite passes. New actual
SPMI init/lock/key+battery read host test covers16-way init, single map set,
both disjoint read lanes blocked by held lock, concurrent reads, error/timeout
unlock, allocation/map failure, battery timeout poison;10 extra race runs.
Actual key worker/HID/join/maps suite+20 read/stop races and board tests pass.
ASAN/UBSAN and counts, not native MMIO timing/scheduling proof; LSan unavailable.
CI YAML only source gates updated (accept0146/reject0118), build command
UNCHANGED. Packer rejects original0118 before creating fake corrected profiles.
Scratch spmi-serialization-0118/{base,edited,verify,full-chain}/xnu plus replay/
compile.py and results, registry/key/transport tests, gate and rejection log.
docs/SPMI-TRANSACTIONS.md records cause/evidence/limitations.
NEXT: USER PUSH -> fresh native kernel/buildtree -> direct keys+USBIN combined
test; baseline available as anomaly control, no required no-keys repeat per
user steering. No new kernel/boot yet, no push/cloud/device action performed.

**USER STEERING: skip no-keys persistent supplement; NEXT combined0118 USBIN.**
User explicitly says stop spending time on this round and proceed with all
consumers connected. Do NOT require no-keys persistent/repeat before next test.
Existing live proves one real independent USBIN unplug/replug; early matching
and second-cycle evidence remain incomplete, not silently marked fully passed.
Proceed same0118 native kernel, existing out-bs/
boot-16k-0118-board-startup-usbin.img. SHA256 rechecked
`6702a2d82e7a8797ac07cdb7a93de9cda9fd288f57dc2b9261488a2789e273dd`.
keys1 + batteryIRQ2 (USBIN), real gauge, ACM2/vbus1; fixed USB/touch IRQ stay
enabled. Both group0 keys and group1 battery attach mux concurrently. This
combines current consumers; CHGR/MSOC additional sources aren't enabled in
this profile. No rebuild/repack needed. Start listener, test all3 keys/touch,
two unplug5s/replug5s cycles with input in between, then power-key screen off/
wake + touch. Save live-0118-board-startup-usbin.txt. Judge shared mux route
counts/healthy, external transitions, balanced key HID, touch and ACM; known
framebuffer redraw glitches and live replay should be separated from IRQ
failure. No automatic request to repeat skipped no-keys stage. No code change,
push, cloud or device action; only existing image checked and docs updated.

**0118 no-keys LIVE: real independent USBIN IRQ observed; get persistent before acceptance.**
logs/live-0118-board-startup-no-keys.txt SHA256
`e93062b23cbfd0b8e0d61cce866e207da3d5b903f87583030a2b56c923f12e69`.
220798 bytes,2 NUL in an AKS prefix. Bootargs at13/148 confirm keys0,
real gauge, batteryIRQ2, ACM2/vbus1. Capture starts in launchd: no BOARD/mux
registry/group1 attach/armed/arm-sync or keys-disabled startup lines. No runtime
key HID/group0. At2913-2918 battery-usbin status0x10 irq1 -> observer usb0x26,
external0/capable0,69%,3996mV -> updateStatus -> notifications1/samples1,
healthy1. At2967-2971 irq2 -> observer usb0x10 external1/capable1,69%,3921mV
-> updateStatus notifications2/samples2 healthy1. Recorded consumer notify-
to-publish7.393/7.434ms, NOT physical cable-to-UI latency. charging0 in both;
this establishes cable sensing/event-driven publication, not actual charging.
First successful real USBIN delivery evidence with keys disabled; no claim
yet of full battery IRQ/charging/MSOC acceptance.

Important live replay: lines2914-2981 equal2982-3049 (EXACT68-line block,
same battery samples, IRQ1/2 and timing, same USB counter768). Also adjacent
duplicate irq1 at2913/2914. Count only TWO unique IRQ observations, not5 route
lines or4 refresh lines. Re-enumeration/ACM reconfiguration present; cannot
infer kernel restart or four cable edges from replay. No unique irq3/4 in
capture, so requested two full unplug/replug cycles not established. No scoped
driver ERROR/FAILED/stop/storm/timeout or panic signature. USB has existing
ep0/ep5 cmd6/status1 WARNs, last counter1024/empty0/TX546/550 err0, not proof
of lossless logging. Raw analysis scratch/artifact-0118/no-keys-live-analysis.json.
NEXT USER: extract sweetlog-0118-board-startup-no-keys.bin from THIS SAME boot
using existing procedure; no need repeat test/reflash yet. Need startup
registry/group1/USBIN arm, no group0, and unique subsequent route/refresh
records to distinguish live capture loss/replay from absent second IRQ cycle.
Inspect before combined usbin. No code change/rebuild/push/device action.

**0118 supplemental power sleep/wake PASS; NEXT no-keys USBIN profile.**
User reports screen can turn off; logs/live-0118-board-startup-wak.txt (exact
filename without final e) SHA256
`21c6d41ea55f206a3546f413350c500278a02240efc62cd202134854e5b706f5`.
272526 bytes, no NUL. Capture starts mid userspace boot, no early board/boot
args; previous baseline pair supplies matching/identity/startup evidence.
Lines3014-3023 power IRQ1/2 + balanced HID down/up ->3044 FB display off;
3267-3277 power IRQ3/4 + balanced HID ->3272 FB display on. Backlight fades
to0/restores1088. After wake42 touch down/up pairs across5 slots, counter195
empty0; USB continues during off and after wake, last1408 empty0 TX1161/1162
err0. Four existing USB ep0/ep5 cmd6/status1 WARNs; no scoped driver ERROR/
FAILED/stop/timeout/storm or panic signature. No need persistent supplement:
live fully covers targeted transitions; this isn't native mux teardown proof.
Unrelated userland vmd[226] CPU-budget warning (>50%/180s) at3666 recorded,
cause not investigated, no evidence tying it to IRQ consumers. Analysis saved
scratch/artifact-0118/wake-analysis.json. Baseline normal path now accepted
across original persistent/live and supplemental wake live; batteryIRQ0 only.

NEXT PHONE: out-bs/boot-16k-0118-board-startup-no-keys.img, SHA256
efca21c10e99d09dd74d4d7279f3accdedd7a46bcc9725210da43073d744fbed rechecked.
keys0/real gauge/batteryIRQ2/ACM2/vbus1. Start listener before boot. Keep
screen ON; two unplug5s/replug5s cycles. Keys, software power wake and key
combination deliberately disabled; use existing recovery/export procedure,
then return key-enabled profile. Export live-0118-board-startup-no-keys.txt;
if capture lacks early mux/battery matching or cable transitions, get
sweetlog-0118-board-startup-no-keys.bin. Need group1 registry match/attach,
USBIN armed/hits -> battery refresh; NO group0/keys routes. Inspect before
combined usbin. No compile/push/repack/device write; battery IRQ unverified.

**0118 baseline logs: registry/key/USB/touch smoke PASS; sleep/wake NOT covered.**
Evidence logs/live-0118-board-startup-baseline.txt SHA256
`1163c24fd2105dd39eaf59019d7e4502e9adb480dfbdcfc9102ce53417502207`
and logs/sweetlog-0118-board-startup-baseline.bin SHA256
`9dbcfabc992e1c91898fed6b186342e29852f11d6c58a3d455db8300cc7fe3af`.
Persistent header v1/ring4194304/total378593/flush86, not wrapped. Decoded
scratch/artifact-0118/baseline-persistent.txt; counts/hashes baseline-analysis.json.
Boot args keys1/real gauge/IRQ0/ACM2/vbus1 agree baseline. Persistent lines137-154:
BOARD once -> GIC probe hits1/clear1 -> mux infrastructure+IOService -> group0
watching registry -> consumer attached events7 -> matched and attached -> keys
three routes IRQ-only. UFS root log is interleaved at147/149; async key arm
after it is expected. No remaining dependency on keys to start mux.

16 real mux hits (power6/up6/down4) match16 HID transitions, balanced3/3/2
press-release pairs. Combination overlaps present; screenshot service also
starts. No claim that a shutdown slider was visually correct. Touch48 down/up
pairs across5 slots, last IRQ counter468/empty0. USB last4480/empty0,
TX4243/4244 err0, ACM configured twice. Three existing ep0/ep5 cmd6 status1
WARNs; no scoped driver ERROR/FAILED/stop/timeout/storm or panic signature.
Live has duplicated early kernel prefix and THREE NUL bytes replacing 'SWE'
in one volume-up key-up prefix at offset292856; persistent has intact matching
HID release and no NUL. Treat as live-log corruption, not a stuck key; err0
does not prove lossless ACM. Keep for future log/USB review, cause not proven.

444 persistent battery samples69%,3995-3997mV external1/charging0; no battery
group1/IRQ records (mode0 intended). Does NOT prove battery IRQ or charging.
Both logs contain only display on, NO display off. All power press groups in
this capture overlap volume keys; standalone power sleep/wake not evidenced.
No native terminated/stop/free exercise; WARN branch not exercised.
NEXT: same0118 baseline, ONLY short-press power (no volume) -> observe screen
off, wait5s -> short-press power wake -> check touch; export new
live-0118-board-startup-wake.txt + sweetlog-0118-board-startup-wake.bin.
If screen fails to sleep/wake report actual observation; do not infer from
missing logs alone. Inspect before existing no-keys then usbin profiles.
No new compile/push/repack or device action this turn. Battery IRQ unverified.

**0118 native consumer artifact verified; THREE boot profiles ready, phone pending.**
User downloaded logs/xnu-kernel-0118.gz and xnu-buildtree-0118.gz. No build log
or CI run ID supplied. Kernel SHA256
`04dd80a939c584c834030d8fa5eecc36dbae5ffc682909311c1712eae898fb45`
matches archive SHA256SUMS. All23 relevant final files byte-match current0145
verify tree plus unchanged0117 USB/touch/storage/startup sources; includes
ee92128 shutdown warning/comments. ARM64 Mach-O executable MH_NOUNDEFS, no
undefined symbols. Native symbols include per-consumer matching/terminated,
typed attachConsumer/detachConsumer, keys service/C backend; old consumer C
bridge/global readiness symbols absent. This proves native compile/link and
source inclusion, NOT runtime registry matching/IRQ or stop/free correctness.

Packaged same kernel via tools/mk-mh2lm-event-matrix.sh ...0118.gz 0118
--board-startup into workspace out-bs/ (prefix boot-16k-0118-):
- board-startup-baseline.img (keys1, batteryIRQ0):
  `823a6a8e6a86f008cbe63466796b90e4ebbaf8fe734200a29370b9d8c89e7991`
- board-startup-no-keys.img (keys0, batteryIRQ2):
  `efca21c10e99d09dd74d4d7279f3accdedd7a46bcc9725210da43073d744fbed`
- board-startup-usbin.img (keys1, batteryIRQ2):
  `6702a2d82e7a8797ac07cdb7a93de9cda9fd288f57dc2b9261488a2789e273dd`
Independent final Android v1 image parsing verifies loader blob/ramdisk;
FAT-extracted native kernel/16K ADT/TOML byte-match inputs, every selector is
unique/correct (real battery, ACM2/vbus1,8CPUs). Source/boot manifests, native
kernel, symbols and packaging log under scratch/artifact-0118; outputs untracked.

NEXT PHONE: FIRST out-bs/boot-16k-0118-board-startup-baseline.img, desktop,
three keys/combination/touch, ACM, display sleep and power-key wake. Export
live-0118-board-startup-baseline.txt and sweetlog-0118-board-startup-baseline.bin.
Require board/mux IOService startup, group0 registry matched/attached, key IRQ
armed and balanced HID; actual key arm is async and needn't precede UFS.
Baseline batteryIRQ0 deliberately retains real periodic samples; not battery
IRQ proof. Inspect logs before no-keys (display ON, two5s unplug/replug cycles,
keys/wake disabled; existing recovery/export then restore key-enabled profile),
then combined usbin. Battery IRQ remains UNVERIFIED; no CHGR/MSOC yet. No push,
cloud run, source changes or device write this turn. No phone terminate test.

**0145 teardown audit follow-up: `ee92128`; ready for USER PUSH, no push.**
Reviewed outside advice against XNU IOService::terminatePhase1 and notifier
remove: terminated is phase-1 inactive notification, can precede stop; removing
a notifier waits/removes/releases it and does NOT generate service termination.
Current notification callback only marks/queues, never dereferences/calls mux
or releases the retained provider before synchronized detach. Consumer loop
may detach an inactive/stopped mux by explicit teardown contract; gate/MMIO
survive to free. Keys normal shutdown stops connection delivery, removes
notifiers, drains/detaches/releases mux, then requests worker exit; worker
releases pressed HID before publishing completion, owner waits then unmaps.
No cleanup-order change was necessary. Added per-bound-group WARN at gated
mux shutdown before revocation/clearing; no empty assertion at entry because
registry consumers aren't mux IOService children and cleanup is asynchronous.
Three extra host scenarios pass: notification/consumer cleanup before provider
stop, provider stop before consumer cleanup (two WARNs), key owner stop before
provider (no WARN). Registry suite/40 races, seven ARM64 objects, strict full
0142->0145 replay, syntax/diff checks pass. Logs in mux-consumers-0117/
termination-audit-{tests,compile}.txt. Docs/MUX-CONSUMERS.md records exact order.
Only diagnostics/comments/tests/docs changed this follow-up; CI unchanged.
Native build/phone still pending; same new baseline/no-keys/usbin sequence.

**CURRENT:0145 registry consumers + terminated cleanup implemented locally.**
Implementation/tests/docs committed as `6a2a552`; final strict full-chain replay,
seven ARM64 objects, registry/key lifecycle suites and source/syntax gates pass.
Ready for USER PUSH; no push performed. New native build and phone baseline pending.
User reprioritized consumer migration after0117 baseline. Additive0145-mh2lm-
mux-consumer-matching.patch changes10 XNU files. Public mux class moved to
sweet_spmi_mux.h; virtual IOReturn attachConsumer(IOService*,group,events,
notify,context) / synchronized detachConsumer(IOService*,group,context) check
both consumer and context identity. Removed muxService/retainMux/readyLock/
publishReadiness and C attach/detach/watch_ready/unwatch_ready. Remaining C
entries publish board nub / dispatch hardware IRQ only. irqService is private
spin-locked GIC dispatch, not discovery. Boot claim prevents restart until
reboot; seven fixed routes/two groups retained, not dynamic route registration.

Each consumer owns SweetSpmiMuxClient with typed retained provider, first-match
and terminated notifiers, lock and software IES on its own loop. Install
terminated first; callbacks only retain/mark/queue. Consumer loop calls typed
methods, terminated/FAILED -> detach/release/fail, no rebind or polling.
stop marks stopping, synchronizes notifier removal outside gates, then gated
detach/drain and event removal. Missing/unpublished mux remains explicitly
waiting/unhealthy, no timer; no invented FAILED notification for a service
never published. Board/mux startup errors distinguish that case. READY means
this connection's routes are attached, not a global infrastructure rendezvous.

SweetPmicKeys IOService matches a late keys nub, owns C observer preparation,
worker and registry client. Backend no longer includes/calls mux. Stop joins
the worker, releases pressed HID keys, then unmaps observer/core/cfg with full
16K ranges. Partial prepare and temporary PDC/GIC maps also unwind. Backend
still one physical key block; no automatic restart. Battery retains early
IOResources personality/initial sample, connects via own registry client;
willTerminate/stop/free stop timers/detach/remove notifications before freeing
events/loop. Mux willTerminate uses same gated shutdown as stop; workloop and
MMIO survive until last retained user releases. Charging/SOC policy and actual
USB/touch/UFS/framebuffer processing unchanged.

Validation: strict exact0117+0145 apply/byte comparison and0115->0142->0143->
fixed0144->0145 sequence; seven final TUs produce ARM64 Mach-O with subsystem-
correct IOKit/BSD flags, old consumer C symbols absent. Actual mux/connection/
keys service and selected battery methods with host registry/MMIO/scheduler:
early/late, cancel, terminated before/after attach, notifier/IES/mux startup
failures, identity, independent/parallel and old owner/rollback/600IRQ/storm/
timeout pass plus40 teardown races. Actual C key worker/start/stop with host
thread/MMIO adapters passes IRQ wake, HID release, paused-read join, thread/
three mapping failures,16K accounting and20 read/stop races. ASAN/UBSAN and
explicit object/notifier/map counts; LSan unsupported in sandbox. Historical
0117 suite and board fan-out pass. Full native CI/link and new phone results
NONE; models do not prove native scheduling/lock order or hardware teardown.

Scratch mux-consumers-0117/{base,edited,verify,full-sequence,full-final}/xnu, compile.py,
make-patch.py, seven objects/logs, registry-tests.txt/key-lifecycle-tests.txt/
historical-tests.txt, ci-source-gate.sh. Initial edit scripts are NOT final
replay scripts. docs/MUX-CONSUMERS.md is current consumer architecture;0144
MUX-IOSERVICE.md retained as historical bridge baseline. CI YAML changes ONLY
final source gates; build command unchanged. Gate accepts0145/rejects0117;
packer rejects old0117 without generating profiles. NEXT user push -> fresh
native kernel/buildtree -> same THREE --board-startup profiles, FIRST new
baseline then no-keys then usbin. Key arm now async, need not precede UFS;
board publication still precedes root. Battery IRQ still UNVERIFIED. No push,
cloud run/new boot/device write. No proactive phone terminate test requested.

**0117 board-startup-baseline phone logs PASS; next no-keys USBIN profile.**
Evidence: logs/sweetlog-0117-board-startup-baseline.bin SHA256
`0a9bb4c205e83b0e62ac9a1b9a948f5e3dc3a97f971b126ef892d8ee6bd73ece`
and logs/live-0117-board-startup-baseline.txt. Persistent header v1/ring4MiB/
total296753/flush35, NOT wrapped; decoded scratch/artifact-0117/baseline-persistent.txt.
Boot args confirm keys1, real battery selectors, sweetbattirq0, USB ACM2.
Persistent lines137-145: BOARD -> GIC probe hits1/clear1 -> infrastructure
ready INTID513 -> IOService started -> power/down APID248, up APID261 ->
group0 events7 -> three routes IRQ-only. UFS userdata/root/log start follows
at150-152. One BOARD invocation in persistent; live repeats an early prefix,
so do not double-count initialization from live output.

Runtime persistent:18 real mux hits(power6/down4/up8), matching18 HID events
with balanced down/up(3/2/4 pairs). FB off3128 -> power IRQ/HID3146-47 ->
on3150. Final Power+Up overlapping down/up recorded; later SpringBoard initiates
shutdown, not a panic. Touch115 IRQs/empty0,8 balanced touch down/up pairs.
USB INTID165 armed and ACM configured/reconfigured; persistent1536 hits,
TX1301/1303 err0; live continues to1664 hits TX1429/1430 err0/empty0.
Four existing ep0/ep5 cmd6 status1 WARNs; no scoped driver ERROR/FAILED/timeout/
stop or kernel panic signature. This accepts normal matching/startup and input/
USB smoke regression, not native terminate/stop/free or all USB reconnect cases.

Battery mode0 intentionally has NO group1 or BATT IRQ records.190 persistent
samples show69%,3996-3997mV,external1/charging0; no cable-state transition
recorded. This is neither battery IRQ nor physical charging proof.0142 readiness
delivery to battery and independent no-key attach remain UNVERIFIED.
NEXT: existing out-bs/boot-16k-0117-board-startup-no-keys.img. Keep display on,
start host ACM listener before boot, unplug5s/replug and wait5s, twice. Keys
are deliberately disabled(including software wake/combination); use existing
recovery/log-export procedure and restore enabled profile afterwards. Save
live-0117-board-startup-no-keys.txt + sweetlog-0117-board-startup-no-keys.bin.
Require mux IOService ready with NO group0, battery readiness -> group1 USBIN
armed, real USBIN route hits -> refresh/IOPM. Inspect before combined usbin.
No new compile/push needed; no code or device writes performed this turn.

**0117 native artifact verified; three board-startup boot images ready.**
User downloaded logs/xnu-kernel-0117.gz and xnu-buildtree-0117.gz. No0117
build log or CI run ID supplied; warning details not reviewed. Kernel SHA256
`09940209c8cf7f343824910694b73edaa72de51b8c0c9228a483f1fbe04a82e3`
matches archive SHA256SUMS. All18 relevant buildtree sources byte-match
scratch/mux-service-0115/verify/xnu (0142/0143/fixed0144). Final ARM64 executable
has no undefined symbols per Mach-O header, contains mux/nub metaclasses,
start/stop/free/getWorkLoop, board init, C bridge and unmappable/unmap symbols;
all packaging markers passed. Native artifact establishes compilation/linking,
NOT successful runtime service matching or hardware IRQ delivery.

Packaged via tools/mk-mh2lm-event-matrix.sh ...0117.gz 0117 --board-startup.
Images in workspace out-bs/ (prefix boot-16k-0117-):
- board-startup-baseline.img (keys1, batteryIRQ0):
  `fe488db32a147cb5a28bda4238a9a8844e17001f677c9e916b11df3bdff7aaf7`
- board-startup-no-keys.img (keys0, batteryIRQ2):
  `0a4e977bef64032984dbe8367abbd9e33114ab76c82882b4ac0b5fd0877b30da`
- board-startup-usbin.img (keys1, batteryIRQ2):
  `d2b0b38ad166abc9be7934cf3132c9a62e69eace44cc6cf68085bae9067e31a6`
FAT-extracted TOML/kernel/16K ADT match inputs; final Android v1 boot image
kernel blob/ramdisk bytes match packaged inputs. Unique selectors and native
kernel SHA checked in each profile. All retain real gauge selectors, USB ACM
(sweetusb2/sweetvbus1), touch IRQ and8 CPUs. Scratch artifact-0117 contains
selected final sources/native kernel/boot-manifest.json; generated files untracked.

NEXT PHONE: FIRST baseline, check desktop/three keys/combination/touch/ACM and
display sleep/wake, export fresh live+persistent logs. Require BOARD before
root/UFS, mux infrastructure ready + IOService started, group0 attach/key armed.
Inspect before no-keys then usbin. No-keys intentionally disables all key
actions/wake/combination; use USB log/recovery and return to enabled profile.
Those stages must show group1/USBIN armed then real route hits + battery refresh;
no-keys must not attach group0. Battery IRQ remains UNVERIFIED; CHGR/MSOC not
next yet. No code change, push, cloud trigger or device write this turn.

**0116 build failure fixed locally: 0144 memory attribute header dependency.**
Fix and validation/docs committed as `40f106d`; user push is next.
Log `logs/xnu-build-log-0116.txt` confirms 0142/0143/0144 applied. Actual
compiler failure is SweetSpmiInterruptMux.cpp:239: VM_WIMG_IO expands to
undeclared VM_MEM_COHERENT / VM_MEM_NOT_CACHEABLE / VM_MEM_GUARDED; its object
fails, then make errors cascade. Historical optional/upstream patch skip
warnings are separate. No evidence of a cloud infrastructure failure.
0144 now includes `<vm/memory_types.h>` instead of `<arm/memory_types.h>`:
the generic header supplies VM_MEM_* and includes the architecture header.
Only this source include changed; unmappable MMIO attributes/lifetime,
startup, C consumer bridge and runtime IRQ behavior retained. CI YAML unchanged.

Validation: extracted the actual 0116 IOKit clang++ command; translated local
paths/Darwin target/resource headers and supplied scratch-only Apple type
builtin stand-ins. Original source reproduces exactly the three errors; fixed
source generates ARM64 Mach-O with the same flags. Previous compile.py mixed
BSD flags/includes into IOKit and masked this dependency: DO NOT use that
mixed command as evidence for native IOKit header correctness. New scratch
`mux-service-0115/compile-native-0116.py`, native-0116-command.txt and
native0116-{before,after}.compile.txt record the check. Cached headers are
from 0114a13; full native CI/link is still required. Exact0115 ->0142 ->0143 ->
fixed0144 applies with whitespace=error-all and byte-matches compiled verify.
Board and mux/battery ASAN/UBSAN regression suites pass, including20 extra
lifetime races; mocks strip headers and cannot catch this compile dependency.

NEXT: user push and fresh native build, then original three --board-startup
profiles. No 0116 artifact needed to fix this compile error. No push, cloud
run, boot packaging or phone write performed. Battery IRQ remains unverified.

**CURRENT:0144 mux IOService conversion implemented locally; ready for user push.**
Implementation/tests/docs committed as `f291397`; staged whitespace checks
passed for all twelve files. No push or new native artifact/phone result yet.
User explicitly requested IOService conversion and evaluation of filter/controller
before testing0143. New additive0144-mh2lm-spmi-mux-ioservice.patch follows0142/0143.
Board still runs in late BSD, independent of UFS/keys, but sweet_spmi_mux_start
now only publishes SweetSpmiMuxDevice under IOService::getPlatform(). Its
registerService(kIOServiceSynchronous) uses a MH2LM-only KernelConfigTables
personality to instantiate/start SweetSpmiInterruptMux : IOService. It is NOT
a manual call to driver start or an early MMIO init. Init/start/stop/free and
getWorkLoop are real overrides; mutable routes/bindings/state/counts/MMIO/
software IES/workloop belong to instance. Existing C APIs and key/battery group
interfaces retained; keys/USB/touch/battery processing code unchanged.

DECISION: keep GIC callback + ordinary provider-less software IES. IOFilter
is technically callable manually with provider=NULL (base interruptOccurred
calls virtual normalInterruptOccurred), but this adds no hardware provider or
mask/rearm management. Pending source detection can use arb MMIO; the SPMI
ack/config/state transactions remain bottom-half work. No normal IRQ class
can presently be discarded safely. Upstream still lacks a GIC IOKit interrupt
controller/specifiers; building that controller is separate work, NOT claimed
done. IOService, IOFilterIES and IOInterruptController serve different layers.
No fake provider=this or new PMIC domain. Keys are BSD clients, battery still
IOResources-matched, NOT newly matched mux child IOService drivers.

Lifecycle: C lookup retains service under rendezvous IOLock then drops lock
before gate wait. Workloop lives through stop until final free. stop takes
workloop gate FIRST, sets stopping, takes IRQ spin lock briefly to mask and
clear/synchronize top-half pointer, then stops hardware, notifies/clears groups,
disables/removes IES. Only afterwards hide C lookup. This prevents attach/drain
re-enabling SPI after disconnect and detach missing a live callback. Free
releases IES/loop/mappings after outstanding API refs drain. Partial starts
clean up too. Stop/failed remains terminal for this boot; no automatic restart
or polling fallback. Global locks/boot rendezvous remain pinned for C/IRQ calls.

IMPORTANT mapping fact checked in XNU arm/io_map.c/machine_routines.c: late
ml_io_map uses io_submap + KMA_PERMANENT and cannot pair with ml_io_unmap.
0144 uses ml_io_map_unmappable(...,VM_WIMG_IO) for owned mappings, same physical
spans/device attributes. Release trunc_page(VA), round_page(size+page_offset),
including CFG0x0c40a000's offset0x2000 on16K pages. Channel mappings cached per
route across attach, owner/APID revalidated before reuse. Old0115 linked kernel
contains both map_unmappable/unmap symbols. Do not revert to freeing permanent
mappings or free only the raw sub-page register size.

Validation PASS: fresh exact0115 +0142 +0143 +0144 apply whitespace=error-all;
all edited/source files byte-match verify. Mux/KernelConfigTables/board generate
ARM64 Mach-O objects; class metaclasses/start/stop/free and C symbols checked.
Linux C++ still uses scratch-only Apple type-builtin stand-ins; native full
CI/link required. ASAN/UBSAN compiles actual service and old mux/battery paths,
with mocked registry/matching/scheduler/MMIO/time/sampling: old rollback/foreignEE/
storm/600IRQ/timeout/readiness cases pass. New missing match, init/loop/event/map
failure cleanup, queued stop, battery stop race, IRQ/API/stop/free race pass,
plus20 extra lifetime races. Models really delete service/IES and verify no
late callback; track balanced full-page mappings including CFG offset/cache.
Mock MMIO access is locked for stress; cannot establish native matching or real
hardware teardown timing. Board test enforces phase/nub matching and old edges.
Actual CircleCI source gate accepts0144 and rejects0143; Bash/Python syntax,
YAML and diff whitespace pass. Packer --board-startup now additionally requires
compiled IOService-start marker. Native kernel/boot image/phone results NONE.
Scratch: workspace scratch/mux-service-0115/{base,edited,verify}/xnu, make_patch.py,
compile.py, individual compile logs/objects, board-tests.txt, mux-tests.txt.
Initial edit.py predates review fixes: do not rerun it as final implementation.

Docs:docs/MUX-IOSERVICE.md (decision, exact hierarchy, lifecycle, limitations);
startup/architecture/IRQ plan/battery guide/lessons updated. CI build command
unchanged; only source marker gates added. NEXT user push -> fresh CI kernel+
buildtree label -> validate final source/classes/markers -> package SAME THREE
--board-startup profiles (baseline, no-keys, usbin). First baseline must show
BOARD before root/UFS, infrastructure ready, IOService started, keys attach.
Then check no-keys battery independently and combined USBIN, requiring real
route/refresh evidence. No proactive runtime terminate test on phone. Battery
checkpoint remains0115 preflight passed, mode2 never attached; actual battery
IRQ still UNVERIFIED. No charging policy/profile/capacity/SRAM/framebuffer work.
No push, cloud run, device write or generated image performed this turn.


**CURRENT: board startup ownership cleanup0143 implemented; ready for user push.**
Implementation and validation/docs committed as `88fe6b2`; staged whitespace
checks passed for all eleven files. No push, CI run or new phone test yet.
User reprioritized startup architecture before battery acceptance. Additive
xnu/patches/0143-mh2lm-board-driver-startup.patch builds on0142 and exact0115.
New bsd/dev/sweet_board.c is called once by bsd_init AFTER bsd_autoconf/IOBSD
and inittodr, BEFORE root lookup/mount retry loop. It starts shared mux, keys,
touch, USB/ACM, sampler and display diagnostics independently of root/UFS.
UFS retains storage and partition-backed persistent SWEETLOG (real dependency).
Mux start(void) only initializes shared workloop/MMIO/GIC probe, publishes
READY with no key routes/gates armed. Keys now attach/detach group0 through
the same table API as battery group1; mux has NO references to key functions.
Key tokens are bitmasks1/2/4 (Power/Up/Down), handler still coalesces/reads levels.
No consumers -> SPI masked; first attach configures routes then opens PDC/GIC;
last detach keeps SPI masked even with pending worker. Disabled/failed keys
cannot prevent battery attach. Partial failure rolls back own group; shared
fatal errors still stop both. Failed upstream arm clears new binding/context
before returning error, avoiding an unowned callback after consumer free.
Old startup mode/armMode scaffolding removed; all runtime IRQ handlers and
HID/IOPM interfaces preserved. No silent polling fallback introduced.

Battery CHECKPOINT:0115 preflight PASSED, PM8150B confirmed, USBIN269/CHGR266/
FG283 metadata ownership measured. FG channel/gates, SRAM threshold and actual
battery IRQ delivery remain untested. USBIN mode2 FAILED before attach because
its old10s wait expired before UFS->keys->mux. Only one initial70%/external1/
charging0 sample; this is not physical IRQ evidence.0142 readiness(a312626)
remains base, not phone-tested. Do NOT resume old0115 charger/all tests or
claim charging/SOC policy fixed. Current work does not alter policy/profile/
SRAM/capacity/scaling. Framebuffer remains later; display probe is diagnostics,
not actual framebuffer/backlight service ownership.

Validation PASS: exact0115 ->0142 ->0143 strict whitespace apply; fresh verify
byte-matches edited inputs. Six final fresh-applied TUs produce ARM64 Mach-O
objects (bsd_init/board/UFS/keys/mux/battery); nm confirms C API links and no
keys startup reference from mux. C++ Linux compilation uses scratch-only
Apple type-builtin stand-ins; full native link still required. Actual board
fanout ASAN/UBSAN test covers one-shot, child key disable/fail and mux failure;
source checks enforce phase/removed edges. Actual mux/battery simulation
passes no-key, missing key APID, partial key rollback, pending detach/idle,
upstream arm failure/context cleanup, old rollback/foreignEE/600IRQ/storm/
timeout and readiness lifecycle/races. Mocks do NOT prove hardware timing.
Python/Bash syntax/YAML parse pass. Actual CircleCI post-build gate accepts
fresh source; old0115 is rejected by --board-startup before creating images.
Packaging dispatch smoke checks unique selectors and retained real battery/ACM.
Scratch: workspace scratch/driver-startup-0115/{base,post0142,edited,verify}/xnu,
compile.py, six compile logs/objects, profile-dispatch.json. No new native
kernel, boot image, CI run or phone result. No push/device write performed.

CI .github/workflows/circleci.yml changes only post-build marker/old-edge gates;
build command unchanged. New --board-startup packages THREE profiles from one
fresh kernel: board-startup-baseline(keys1,battirq0); board-startup-no-keys
(keys0,battirq2); board-startup-usbin(keys1,battirq2). All retain real battery,
USB ACM/touch IRQ. No-keys deliberately disables key wake/combination; use
ACM/recovery export then restore enabled profile. Existing --battery-irq six
profiles retained. NEXT: user push, native CI, fresh kernel+buildtree label;
verify final source/symbols, package --board-startup. FIRST test baseline
startup/devices/persistent log; inspect before no-keys and parallel USBIN.
Require BOARD before UFS root, mux infrastructure ready, correct groups,
key/battery armed. Only then judge actual USBIN events/IOPM transition.
Detailed old/new graph, battery pause point and limits:docs/DRIVER-STARTUP.md;
architecture/IRQ plan/battery guide/lessons updated. Full board startup and
battery IRQ acceptance remain OPEN until real logs.

**0115 USBIN failed BEFORE route attach; startup readiness fix 0142 ready for user push:**
Fix committed as `a312626`; all ten staged files passed whitespace checks.
No push was performed; native CI and new phone logs remain the next gate.
User saw the UI stay at70% with an external-power/charging symbol regardless
of unplug/replug and no display-off cable wake. Persistent log
logs/sweetlog-0115-battery-irq-usbin.bin SHA256
`150d0bbbc938ce875a4ee1456f81a8fac81784f75f19761f9f23c4c3bb7d3705`,
v1 ring4194304 total269111 flush36, not wrapped; decoded3922 lines at
scratch/battery-irq-0115/usbin-persistent.txt. Mode2/preflight1 at line98,
ONE initial gauge sample at111 (70%,4009mV,external1,charging0,state7), then
ERROR116: FAILED mux not ready within10s notifications0 samples0; no polling
fallback. Only AFTER that does mux arm3 key routes at INTID513 (lines134/135).
ZERO battery attach/arm/route/refresh records. Diagnosis: early IOKit battery
exhausted its arbitrary10s retry budget before BSD key initialization called
sweet_spmi_mux_start. Thus frozen initial external state/no cable wake follows
an unarmed stopped battery service; NOT evidence of USBIN polarity/ack/GIC
failure, and not a framebuffer regression. Mode2 lacks MSOC IRQ by design,
so percentage staying70 alone is not the failed criterion. Other paths stay
alive:31 key route records(power19/down6/up6), USB1024 hits TX758/760 err0,
touch408 hits, multiple display off/on. Seven known USB command WARNs.

New additive patch xnu/patches/0142-mh2lm-battery-mux-readiness.patch applies
AFTER0141 to exact0115 source. Only header, mux and SweetBattery change.
Replaces100ms retry/deadline with a one-shot watch_ready/unwatch_ready API,
usable before mux workloop exists. Battery registers after its own event
source is ready and returns immediately; mux publishes READY only after
hardware/workloop initialization, or FAILED for actual initialization/fatal
failure. Callback only queues; battery's own loop attaches then arm-sync reads.
Already-ready/already-failed mux delivers the corresponding event immediately.
Readiness is NOT counted as a hardware battery IRQ notification. IOLock guards
one-shot callback/context lifetime; publication/registration race cannot lose
the event, and cancellation synchronizes with callbacks before context free.
Failure/stop/free unsubscribe before detach/event release. Runtime fatal
FAILED publication still permits gated detach to erase the old consumer
context; tests cover removal after storm and RW timeout. Waiting publishes
SweetBatteryIRQWaitingForMux=true, IRQHealthy=false; there is no startup timer,
status polling or silent fallback. If mux is never invoked, waiting remains
explicit, not healthy; the fixed profiles keep sweetkeys=1. Do not move mux
GIC/MMIO initialization into early IOKit to work around lifecycle ordering.

Checks PASS:0142 whitespace=error-all apply to0115, fresh verify source byte-
matches all3 edited/compiled files; both C++ units generate ARM64 objects,
watch/unwatch C linkage matches. Local clang19 still uses compile-only Apple
type-builtin stand-ins as before; native full CI/link and phone remain pending.
ASAN/UBSAN test now compiles7 REAL battery readiness/IRQ/lifecycle methods
with mocked sample/property/scheduler adapters, plus the actual mux. Covers
late mux(+30s,no timer/status reads), mux-first, startup failure before/after
subscription, cancellation before mux start and after queued READY, IRQ-driven
read/stop with pending IRQ, and200 registration/publication plus cancellation
races. Existing rollback,foreignEE,600 IRQ,storm,timeout tests still pass.
Mocks do NOT validate real PMIC/GIC timing or complete IOKit scheduling.
Bash/Python syntax and CircleCI YAML parse passed. Packer now rejects old0115
missing readiness markers before creating images; no new boot was generated.

Scratch source/base/verify, compile.py/logs/objects:
workspace scratch/battery-irq-startup-0115/. CI .github/workflows/circleci.yml
adds0142 marker checks only; build command unchanged. One-build six-profile
matrix preserved; --battery-irq now requires0142 (old0115 images retained as
failure evidence). Tests/docs/IRQ lessons updated. NEXT: user push, native
CircleCI build, download fresh kernel+buildtree under a NEW label, verify and
package, then repeat USBIN mode2 first; require waiting -> mux armed -> READY
received -> consumer attached -> battery armed -> arm-sync -> real plug IRQ.
Do not test0115 charger/all/debug: same startup bug. Baseline/preflight need
not be repeated without a new concern. No push/device write performed here.

**0115 persistent preflight PASSED; PM8150B identity/FG mapping now measured:**
Reviewed logs/sweetlog-0115-battery-irq-preflight.bin, SHA256
`ff7c67cba070256e558e5272f5e6eb0e1ceced9756c4c58a53e8a18265c5fc92`.
Decoder tools/sweet-log.py reports v1, ring4194304, total301479, flush28,
not wrapped; decoded4087 lines stored outside git at
scratch/battery-irq-0115/preflight-persistent.txt. Boot args at line261 match
the earlier mode1 live capture; missing early records are now recovered.
Line106: REVID SID2 rev1..4=00/00/00/02, type51/subtype20, FG SOC subtype10,
read-ok1/1, pm8150b1. This is direct hardware identification, not just DTS;
do not assign a marketing silicon revision name without source mapping.
Line117 explicitly selected mode1 preflight1. No new kernel build needed.

USBIN line78: PPID213/APID269, EE0 owner0 IRQ-owner1, ACC0/status0/active0;
line92 bit4 rt1 edge1 high1 low1 enabled0 latched0 read-ok1.
CHGR line79: PPID210/APID266, EE0 IRQ-owner1, ACC0/status0/active0;
line99 bit1 rt0 edge1 high1 low0 enabled0 latched0 read-ok1.
FG line114: PPID240/APID283 map01024000 EE0 owner0 IRQ-owner1, owner-word8,
owner-bit27, active0. IMPORTANT: FG route-parts03 inspects META+OWNER ONLY;
acc-enable0/irq-status0 in this line are initialized placeholders, NOT channel
readbacks. FG channel access/gate/status remains untested until mode4.
Line116 observer reads: RT02 (SOC_READY), edge00/high00/low00/enable00/latch00,
read-ok1. These are actual peripheral register bytes: FG IRQs are currently
unconfigured/disabled, not proof of broken delivery. Mode4 configures the
MSOC rising and READY both-edge route bits. SRAM threshold remains unknown.

154 real samples, all MSOCb4/70%/held0; 4008..4009mV, external1/charging0/full0,
USB10/CHGR47 state7. Key mux22 hits (power10/down4/up8), balanced key HID22;
USB1152 hits TX904/905 err0, touch218 hits, display off/on. No scoped driver
ERROR, stop or timeout; six known EP command WARNs. Early mux probe passed
and arms three key routes at INTID513. No battery route is armed in mode1;
passing preflight does NOT validate battery IRQ delivery or real charging.

NEXT: use EXISTING out-bs/boot-16k-0115-battery-irq-usbin.img (mode2), SHA256
1f9569ad0e3d52428d8e5209338c845875af2d74f9d4f698aaaf53432dd680ff.
Test cable insertion/removal ~3 cycles with5..10s dwell, three keys/combination,
touch and one display-off insertion/removal. Mode2 disables two-second battery
polling; only USBIN plus initial/arm-sync/bounded-confirm samples. Require
battery-usbin route -> refresh events01/sourceirq -> real external change
and IOPM UI transition records, plus shared keys intact. Export persistent
sweetlog-0115-battery-irq-usbin.bin and retain live ACM if available. Disconnect
interrupts ACM normally; persistent log is required for early arm/unplug.
No baseline repeat, push or cloud compile needed. CHGR/MSOC modes still wait.
Hardware baseline, test guide and plan updated; only docs changed this turn.

**0115 preflight live capture: runtime healthy, early preflight evidence MISSING:**
User skipped mode0 baseline and tested mode1 directly; do not require baseline
unless a concrete regression needs comparison. Reviewed
logs/live-0115-battery-irq-preflight.txt, 3401 lines, SHA256
`c72a47a28d9174853d00471d1fef829279960b42a5550f3999f4af041198acd1`.
Launchd args at lines10/151 confirm sweetbattirq=1, real battery selectors,
irqdiag/uidiag and the intended key/USB/ACM configuration. Capture begins near
launchd and contains ZERO SWEET BATT IRQ, REVID, battery-fg-soc, SPMI or BATT UI
lines; startup preflight results are not present. Source runs preflight before
initial sample/registerService, and mode1 deliberately continues polling even
if preflight returns false. Thus successful boot/real samples CANNOT prove
identity matched or preflight passed. Do not infer REVID/APID values or move
this evidence to the verified hardware column.

Runtime: 54 samples, all gauge-ready MSOC b4/70%, 4009mV, held0, USB10,
CHGR47/state7, external1/charging0/full0, pack not reported missing. Confirms
real gauge polling, not battery IRQ or real charging. State7 matches prior
0114a15 real-battery capture; no plug transition in this capture. Mux recorded
12 routes: power6, vol-down4, vol-up2, all matching balanced HID down/up;
power+vol-down concurrent events at lines3262/3269/3292/3294. Display off3121,
on3150 with later touch. USB reached1152 hits, TX904/905 err0; touch reached
141 hits, one earlier empty1 returned to0. No scoped driver ERROR, FAILED,
stop, fallback, ack failure or timeout appears. Six USB EP cmd status1 WARN
lines match existing warning class. Eighteen HFS clonefile cluster_write -34
records are also a class present in older 0114a15 captures (11/3 records in
normal/debug), not evidence of a new PMIC IRQ regression; not fixed here.

NEXT: export persistent SWEETLOG for this SAME preflight boot, e.g.
sweetlog-0115-battery-irq-preflight.bin, before replacing the current boot/log.
No need to re-run baseline, recompile, push, or re-debug ACM. Read early REVID,
FG ownership/config and selected mode/preflight result before recommending
mode2 USBIN. Runtime smoke coverage is good; preflight and battery IRQ
acceptance remain OPEN. Only docs changed in this review.

**0115 battery IRQ artifact verified; six temporary images ready, NOT phone-tested:**
User downloaded logs/xnu-kernel-0115.gz and logs/xnu-buildtree-0115.gz.
No 0115 build log or CI run ID was provided; do not claim a warning-free build.
Kernel manifest SHA256 verified:
`a601cb597d38697832e4324fbb4341c75810034722ed95b139472bdb65dae6f2`.
All five 0141 files in the final buildtree byte-match the implementation in
scratch/battery-irq-0114a15/xnu; keys/USB/touch/log-level files byte-match the
previous verified source. Kernel contains battery mode/REVID/arm/route markers,
and linked SweetBattery IRQ methods plus mux attach/detach and transport-lock
symbols. Thus the native artifact includes 0141, not merely a successful
build of skipped patches. No code change or new CI run was needed here.

Packaged with tools/mk-mh2lm-event-matrix.sh logs/xnu-kernel-0115.gz 0115
--battery-irq. Images in workspace out-bs/ (prefix boot-16k-0115-):
- battery-irq-baseline.img (mode0)
  SHA256 0124ec14946051d951f75ea40aadffa917ccfa673e4f1e4580995b3e2f180883
- battery-irq-preflight.img (mode1)
  SHA256 5d3bdb71e1fea360f735d609289fb815218988f0e6a2eb23a6a029961929ab2a
- battery-irq-usbin.img (mode2)
  SHA256 1f9569ad0e3d52428d8e5209338c845875af2d74f9d4f698aaaf53432dd680ff
- battery-irq-charger.img (mode3)
  SHA256 6d819d324c7b9cdec1b40192a5a8257291ccbf39e1e4ca50bfd7eebcfecf7db5
- battery-irq-all.img (mode4)
  SHA256 ae33c014aa8ee78378ec7282e4cf571bf7ff6c0f053e8c504502df36a671dab9
- battery-irq-all-debug.img (mode4, sweetloglevel=3)
  SHA256 157c5bceb5772c6dd9886f9fd16312bffcbf80ac0da4e5b38147e0f9fb8053d8

Read back ALL SIX FINAL Android boot images (not only source ramdisks): boot
header/page, firmware kernel blob, embedded ramdisk, FAT TOML/kernel/ADT all
verified; exact shared XNU kernel hash and intended unique sweetbattirq mode
confirmed. Each retains sweetbattery=1 sweetbatt=1, key/USB/touch IRQ and ACM;
only DEBUG profile has sweetloglevel=3. Preflight has irqdiag/uidiag. Evidence:
scratch/battery-irq-0115/xnu (extracted source), kernel (kernel/manifest),
artifact-verification.txt, boot-verification.json (all hashes and boot args).

NEXT PHONE STEP: fastboot boot out-bs/boot-16k-0115-battery-irq-baseline.img;
check desktop, real battery, three keys/combination, touch and USB ACM. If
normal, boot battery-irq-preflight.img and export fresh persistent SWEETLOG
(plus ACM if useful) for REVID/FG/APID configuration review. Stop there for
analysis before mode2 USBIN. Baseline/preflight explicitly retain two-second
battery polling; modes2..4 disable it. Remaining stages use the SAME compiled
kernel; do not push/recompile for these parameter changes. No new phone log,
REVID readback or real battery IRQ proof exists yet. Subgoal4 stays open.
User controls fastboot and push; no push or device/partition write was done.

**Battery IRQ first implementation ready for user push / native CI; NOT phone-tested:**
Implementation committed as `c7c02d4`; staged whitespace checks passed for all
ten files. User retains push control; no remote action was taken.
User authorized implementation after the workspace power research review.
New `xnu/patches/0141-mh2lm-battery-event-irqs.patch` is based on exact final
0114a15 source, changes sweet_spmi.c/.h, SweetSpmiInterruptMux.cpp and
SweetBattery.cpp, and adds bsd/dev/sweet_spmi_mux.h. Reuses the sole INTID513
mux and route table; battery attach/detach is serialized on its workloop,
notifications only queue the battery's own IOInterruptEventSource/IOWorkLoop,
and IOPMPowerSource/updateStatus remains the publication path. Key/USB/touch
IRQ and ACM remain enabled. New mode sweetbattirq=0 polling baseline,
1 read-only identity/FG preflight + polling, 2 USBIN IRQ, 3 USBIN+CHGR,
4 above +MSOC_DELTA/SOC_READY. Modes >=2 have no periodic sampling/fallback;
initial and arm-sync samples plus at most one 100ms SOC-jump confirmation per
event are explicit. Invalid gauge/charger or missing pack stops sampling,
holds the last source and publishes an explicit unhealthy/invalid property.

New routes resolve unique EE0 IRQ-owned APIDs before channel access, refuse
already-enabled route takeover, preserve/restore unrelated trigger bits, and
restrict writes to IRQ offsets11..16. Battery IRQ writes share the observer
transport lock. RW timeout poisons that APID until reboot. Partial attach
rollback preserves keys if cleanup succeeds; failed cleanup or shared fatal
mux fault explicitly stops/notifies both groups, so key concurrency needs real
regression. Fixed inherited lifetime256 IRQ cutoff to >256 IRQ/1-second window.
No charger-policy/SRAM/profile/capacity/SOC-scaling changes. Identity still
expects type51/subtype20/SOC subtype10; actual REVID, new FG APID/ownership/
gates and inherited MSOC threshold remain UNMEASURED on phone.

`tools/mk-mh2lm-event-matrix.sh ... LABEL --battery-irq` now packages one
new compiled kernel into baseline/preflight/usbin/charger/all/all-debug images,
retaining sweetbattery=1 sweetbatt=1 and ACM. Existing --irq-only remains mode0.
`.github/workflows/circleci.yml` changed ONLY to add 0141 source marker gates;
the native build command is unchanged. No push, cloud run or new boot image
has been produced. Do not package the older 0114a15 as this test.

Local checks: patch applies with whitespace=error-all to exact baseline;
fresh applied five files byte-match edited/compiled sources. Three ARM64
Mach-O translation units compile and C/C++ API symbol names match. Linux
clang19 C++ required compile-only stand-ins for Apple's XNU type builtins;
these are NOT in production and do not replace native CI/full link. Actual
patch-applied mux source passes ASAN/UBSAN simulation: trigger masks, foreign
EE rejection, unrelated-bit preservation, partial setup rollback, queued
detach without stopping keys, 600 lifetime IRQs, dense storm stops both,
and timeout poison/no reuse. Test script is
`tools/tests/test-mh2lm-battery-irq.py EXTRACTED_XNU_DIRECTORY`; simulator
cannot establish real delivery/timing or complete battery service lifecycle.
Bash/Python syntax and CircleCI YAML parse pass; --battery-irq rejects old
0114a15 missing markers before any image creation.

Exact local sources/checks: workspace scratch/battery-irq-0114a15/ contains
base/xnu (0114a15), xnu (edited), verify (fresh patch-applied byte-match),
compile.py, compile-summary.txt, individual compile logs and objects. Do NOT
rerun initial edit.py/edit_battery.py; later review fixes are not in them.
Next action: user push then download fresh xnu-kernel and xnu-buildtree with
the new label; verify native build/markers, package via --battery-irq, start
baseline -> preflight -> USBIN -> CHGR -> all. USB ACM is the existing log
channel, persistent SWEETLOG supplements unplug/startup faults. Full test
guide and limits: docs/BATTERY-IRQ-TESTING.md. Plan, hardware baseline,
architecture pending-change note and logging scope updated. Battery subgoal4
is implemented locally but NOT accepted; framebuffer still waits.

**Workspace power research reviewed and integrated into battery IRQ baseline:**
User pointed to workspace docs/INDEX.md and its six static-analysis chapters.
Checked the IRQ/FG/profile claims against new reference
ref/android_kernel_lge_sm8150 HEAD d4f335e13d73c4aa2af85581a6ec11b670ee5b94;
its pm8150b.dtsi byte-matches the previously used lge-lineage-kernel file.
Ack clears arb APID pending then peripheral +0x14 BIT; unmask opens ACC and
clears/enables a previously disabled source. LG delta config 3 = 0.3%, default
5 = 0.5%; MSOC SRAM word32:byte1, BSOC word30:byte1. These are source facts,
not current phone readbacks. SRAM reads require grant/release and silicon
workarounds; first try inherited FG configuration, do not reload/restart the
profile just to obtain IRQs. Remaining hardware evidence: REVID, FG mapping/
ownership/gates/current threshold and real delivery. PMIC manuals are optional
cross-checks, not blockers. Documented independent data-quality issue: target
DTS uses BLT42 Tocad/LGC 4000mAh-named profiles and LG UI full raw247, whereas
0114a15 XNU fixes capacity at 5020mAh and uses generic MSOC scaling. No current
pack ID/profile readback exists; do not change capacity/SOC or infer jump cause
solely from that mismatch. SMB1355/P9222 are DTS topology, not XNU enumeration.
Only repository docs changed; original workspace reports and kernel unchanged.

**Plan amended at user's request: battery IRQ before framebuffer:**
IRQ-FRAMEBUFFER-PLAN.md now adds subgoal 4 after keys/USB/touch; the overall
IRQ goal requires all four plus combined regression. Reuse the sole INTID513
SweetSpmiInterruptMux route table; stage USBIN_PLUGIN and CHG_STATE_CHANGE,
then FG MSOC_DELTA (SOC_READY if needed), with handoff to the battery's own
workloop and standard IOPMPowerSource notification. No silent timer fallback;
any low-frequency telemetry must be explicit and cannot substitute for IRQ
acceptance. Charging policy/control remains out of scope. No battery IRQ code
has been implemented in this documentation milestone. New
docs/BATTERY-IRQ-HARDWARE-BASELINE.md distinguishes target DTS + matching SID2
hardware evidence for PM8150B from the still-unread REVID/subtype/revision.
USBIN APID269/bit4 and CHGR APID266/bit1 were checked in 0112m17; FG PPID0x240
APID/ownership/gates/threshold remain to verify. PM8150B subtype0x20 is a
Linux-defined expected value, not a phone readback. Existing local Linux
sources are enough to start; user can optionally help find PM8150B register
reference/errata or existing device revision logs. No CI or push needed for
these docs; user retains control of pushes.

**Real battery restored; architecture boundary reviewed:**
`logs/live-0114a15-batt-irq-only.txt` SHA256
`1a1ed7062cec374202bcfbc6546073ef4da06f1be94b5f49e33693ed75eea62e`
contains 3017 lines and 63 real SID2 samples. Boot args include both battery
selectors. Every sample is gauge-ready MSOC 0xb4 / 70%, held=0, 4012–4013 mV;
USB present (0x10), charger 0x47 / state 7 DISABLE_CHARGE, charging=0/full=0.
This confirms the packaging fix restored hardware sampling. No current
diagnostic or plug transition was captured, so this does not establish why
charging is disabled. USB reaches 768 IRQs, TX err=0; touch down/up exists,
but no key HID/mux route activity, so full real-battery + three-key concurrent
regression remains uncovered. User primarily requested an architecture review,
not another charging experiment. Expanded CURRENT-DRIVER-ARCHITECTURE.md:
IOPMPowerSource/updateStatus notification is standard XNU; two-second IOKit
polling is still active, battery PMIC IRQ routes are not installed; observer
reads do not implement charging control. Fixed temperature/current/capacity
metadata, unsupported manager user-client selectors and missing low-battery/
thermal/lifecycle policy are documented. No kernel change, CI run or new boot
is needed for this documentation review. Do not push; user controls pushes.

**0114a15 IRQ image battery omission found and repackaged; initial diagnosis
and packaging record (later phone result above):** The user reported that battery
behavior looked abnormal again. Both original 0114a15 IRQ profile boot args
and the two INFO/DEBUG live logs omit `sweetbattery=1 sweetbatt=1`.
The exact 0114a15 `SweetBattery.cpp:739–746` defaults both selectors to zero;
`poll()` then publishes static 100%, 4200 mV, external/capable/full true,
charging false, and `start()` creates no sampling timer. The prior MSOC pair
validation/scaling and IOPM notification fixes remain in the compiled source.
This is a packaging regression, not evidence those fixes were removed.

Commit `d84ac7f` makes `--irq-only` normal and DEBUG profiles share the two
real-battery arguments and requires the kernel's MSOC marker when packaging.
Using the SAME 0114a15 kernel, label `0114a15-batt` created:
`out-bs/boot-16k-0114a15-batt-irq-only.img` SHA256
`33f8d1287e78328efbb0add119f8d14b2d241aa5363425e357802eb5804b6006`,
and `out-bs/boot-16k-0114a15-batt-irq-only-debug.img` SHA256
`2940b199209135610d469533e9b741ad8bfd37b76aa3a02c6bf0a6611ad88f2c`.
Both retain kernel SHA256
`bc596ce6d81bd24b3cdbb24cf78260c70df4133eff2b7174e4ca2e385d1e00cf`.
Readback from both final Android boot images verified the two added battery
arguments, the unchanged remaining arguments and kernel hash. `bash -n` and
patch whitespace checks passed. No cloud compile or push is required to test.
Boot the normal `0114a15-batt` image, check `SWEET BATTERY SID2` with
`src=gauge`, percentage and cable transitions plus key/touch/ACM concurrency.
The older fixed-IRQ captures prove three-driver concurrency with a STATIC
battery service only; they do not validate concurrent real battery polling.
Current charger current/physical charging cannot be inferred from the old
0114a15 static battery icon; the older 0112m19/m20 charging evidence remains
valid for those tests.

**0114a15 driver architecture reviewed; documentation only:**
`docs/CURRENT-DRIVER-ARCHITECTURE.md` (commit `98199bf`) now maps the exact
0114a15 buildtree source and phone evidence for USB, keys, touch input,
framebuffer/IOSurface presentation and backlight. PMIC keys follow the planned GIC callback plus
software IOInterruptEventSource/IOWorkLoop design. USB and touch use short GIC
callbacks plus event-woken BSD workers. All three INTIDs still use board-specific
branches in `sleh_irq` and direct GIC Distributor MMIO; no generic interrupt
controller provider/registration exists yet. HID and IOMobileFramebuffer/
IOSurface are project-built IOKit-compatible userland interfaces. Display
still uses ABL scanout memory, timer-generated vsync and CPU copy of the latest
swap, so the known partial lock-screen redraw is a framebuffer architecture
gap. This review changed no kernel code and requires no CI build or phone boot.
The workspace `AI-GUIDE.md` now points to the current handoff and architecture
document; its older body remains an explicitly dated boot-bring-up snapshot.

**0114a15 DEBUG profile confirmed; scoped INFO/DEBUG filtering works:**
`logs/live-e-0114a15-irq-only-debug1.txt` SHA-256
`c3dc0443f8fb7daf37376e89fd8283b32e535824573a17842f02a7a0e77d317d`
is a 3240-line ACM capture from the DEBUG image. Launchd boot arguments at
lines 16/146 include `sweetloglevel=3`; the capture contains 279 `[DEBUG]`
lines (274 USB, 5 touch), 108 `[INFO]`, 8 `[WARN]` and no driver `[ERROR]`.
The prior `live-e-0114a15-irq-only-debug.txt` had no `sweetloglevel=3` and
zero `[DEBUG]`, so the default INFO versus DEBUG profile distinction is
confirmed. USB INTID 165 armed, reached 896 IRQ hits and TX 461/463 with
`err=0`; it reconfigured and reopened after host reconnects. Early USB IRQ
hits had transient `empty=1`, but later counters show `empty=0` and USB
remained active. Touch GPIO122/INTID 240 armed, reached at least 16 IRQ hits,
and produced HID down/up with DEBUG coordinate records. Eight USB endpoint
command status 1 WARN records accompanied enumeration/reconnect; no USB or
touch IRQ failure, fallback or driver stop appears. No key press or PMIC mux
route event is present in this short DEBUG capture, so key concurrency is
supported by the preceding normal-profile capture, not this one. ERROR
persistence on a deliberately induced driver fault remains untested and is
not needed to accept the normal-path logging-level behavior.

**0114a15 fixed IRQ live capture passes the concurrent functional path;
default INFO profile identified by a later DEBUG capture:**
`logs/live-e-0114a15-irq-only-debug.txt`
SHA-256 `19f64f6ade97ab8c3808f49208a34ff52abda1fdd6cddf2c98e301a969530484`
is a 3719-line ACM capture. Its launchd boot arguments at lines 14/146 omit
`sweetloglevel=3`, and it has zero `[DEBUG]` lines, despite the filename;
the later `debug1` capture confirms that it represents the default INFO
profile. The capture shows USB GIC
INTID 165 armed and 1536 IRQ hits, ACM stream through TX 1296/1298 with
`err=0` and `empty=0`; TouchComm GPIO122/INTID 240 armed, 689 IRQ hits,
HID touch events including finger 1, and a transient `empty=1` at 396
which returned to zero at 441. PMIC mux had 32 logged hardware route
hits: Power 11, Volume Up 8, Volume Down 13, with matching key HID events.
Display off/on appears at lines 3005/3035 and 3234/3294; touch HID events
follow the latter wake at line 3304. Concurrent USB, key and touch activity
continued. No driver `[ERROR]`, ack failure/recovery, fallback or shutdown
appears. Four `[WARN]` USB endpoint command status 1 records also occurred
in the earlier 0114a13 combined capture; they did not stop enumeration or
streaming here. Thus normal fixed-IRQ operation passed in this capture, while
the 0138 exceptional ack retry path was not exercised. DEBUG-level output
was subsequently verified by the `debug1` capture. The log begins in launchd,
so early boot records and the mux's `armed` line are not in this capture;
the real PMIC route hits prove it was active later.

**0114a15 artifact verified and two temporary fixed-IRQ images packaged;
phone results above:** User downloaded `logs/xnu-buildtree-0114a15.gz` and
`logs/xnu-kernel-0114a15.gz`; the 0114a15 build log was not downloaded, so
its full warnings and CI run ID are not recorded. The build tree's key, USB,
TouchComm, UFS, log-level header and PMIC mux files all byte-match
`scratch/log-level-0114a13/xnu/`. The kernel manifest passed SHA-256 and the
kernel contains the 0139 fixed key/touch/USB IRQ markers plus 0140
`sweetloglevel` and DEBUG text. `tools/mk-mh2lm-event-matrix.sh ... 0114a15
--irq-only` created and verified both FAT ramdisk copies:
`out-bs/boot-16k-0114a15-irq-only.img` SHA-256
`c32009aa4bc01acfdc638f469171f6d7675ef48fd36a6e596aca4638662230d8`,
and `out-bs/boot-16k-0114a15-irq-only-debug.img` SHA-256
`0dc9b2fcff0dd1fdec7cc116e539edea1bb89da9f6e35600044617724a0dd657`.
Both carry kernel SHA-256
`bc596ce6d81bd24b3cdbb24cf78260c70df4133eff2b7174e4ca2e385d1e00cf`;
DEBUG differs only by `sweetloglevel=3` in boot arguments. The normal and
DEBUG phone captures are recorded above; use the normal image for later
functional testing and DEBUG only when detailed event records are needed.

**0114a14 CircleCI built a kernel without 0139/0140; do not phone-test that
artifact:** `logs/xnu-build-log-0114a14.txt` lines 229–241 show 0138 applied,
but dxb skipped 0139 after `SweetSpmiInterruptMux.cpp:180` failed and then
skipped dependent 0140. The final `XNU Build Done!` only means the older
source compiled. The 0139 patch was generated against the pre-0138 mux and
repeated 0138's ack retry hunk. It has now been rebased onto the source after
0138. The correction and CI gate are committed as `f31ff83`. Local
`git apply --check` and `git apply` in CI order (0138 → 0139 →
0140) succeeded against the extracted 0114a13 build-tree files; all six
resulting source files byte-match `scratch/log-level-0114a13/xnu/`. CircleCI
now checks the staged XNU source for 0139/0140 markers after `build.sh` and
fails clearly if dxb silently skips either patch. The later 0114a15 artifact
contains both corrections; no 0114a14 kernel artifact is needed.

**0140 scoped driver log levels, commit `6a9d8c0`; present in 0114a15,
phone result pending:** The user
observed that the current console, persistent SWEETLOG and ACM output have no
unified severity. `0140-mh2lm-driver-log-levels.patch` adds ERROR/WARN/INFO/
DEBUG prefixes and `sweetloglevel=0..3` (default INFO) to key, DWC3 USB,
TouchComm and PMIC mux call sites. Failure and shutdown records are ERROR;
transient reads/retries are WARN; bounded IRQ hits/counters remain INFO so
default logs still prove hardware delivery; register dumps, control/event
details and touch coordinates are DEBUG. The ring and ACM byte-stream formats
are unchanged. Other XNU/IOKit/userspace and Sweet driver logs remain raw;
no unprefixed line should be assumed to be INFO. See
`docs/LOGGING-LEVELS.md` for semantics and scope.

`tools/mk-mh2lm-event-matrix.sh --irq-only` now makes a normal image and,
only if the kernel contains 0140, an `irq-only-debug` image with
`sweetloglevel=3` from the same kernel. Four changed C files compiled as
ARM64 Mach-O with CI-derived flags; the mux C++ header/macros compiled in an
isolated C++ test and the full mux source preprocessed. Full local C++
compilation with system clang 19 hit existing XNU/Apple builtin errors in
exported IOKit headers, so CircleCI is still the compile gate for that TU.
After correcting 0139, `git apply` applied 0138, 0139 and 0140 sequentially
to the extracted 0114a13 source; all six resulting files byte-match the local
edited sources. The 0114a15 artifact contains 0140 and two temporary images
are ready; no 0140 phone result exists. Boot the normal image first and use
the DEBUG image only when detailed event traces are needed. Confirm the normal
image retains ERROR/WARN/INFO and the
low-rate IRQ proof while suppressing `[DEBUG]` from these drivers. A USB
failure still requires persistent SWEETLOG extraction.

The user chose to use 0140 temporarily and defer a complete logging
architecture to **major goal 3** in `docs/IRQ-FRAMEBUFFER-PLAN.md`:
standardize log structure, add per-target enablement and level thresholds,
and manage output modes/channels. This plan is documented, not implemented.
The reported 0114a14 build skipped 0139 and 0140; it is not a valid test
artifact. The corrected 0114a15 artifacts are present, but no build log or
CI run ID was downloaded.

**0139 fixed IRQ candidate, commits `f90c4a3` and cleanup `c0c5602`,
corrected in `f31ff83` and present in 0114a15; phone test pending:** Following the
0114a13 combined run, the user chose to remove silent runtime polling fallback
from the MH2LM key, DWC3 USB and TouchComm drivers. New
`xnu/patches/0139-mh2lm-fixed-event-irqs.patch` builds on committed 0138
(`a41193a`). Keys always arm the three PMIC mux routes; touch always arms
GPIO122 / INTID 240; USB always arms DWC3 / INTID 165. A failed route,
GIC probe, arm or worker stops its function and logs the reason. The CDC log
sender wakes on new ring bytes, USB configuration and TX completion rather
than its old 20 ms timer. `tools/mk-mh2lm-event-matrix.sh --irq-only` packages
one fixed IRQ image and, with 0140, a second DEBUG log image; legacy test
profiles reject an 0139 kernel. The changes are implemented in shared key
and USB sources. The user confirmed that
only MH2LM needs support here, so the temporary legacy polling branches were
removed instead of maintained for GUACAMOLE and SWEET.
The patch applied to a copy of the exact 0114a13 source plus 0138, and all
five resulting files byte-match the locally edited tree. All four changed C
sources compiled as ARM64 Mach-O with CI-derived flags; the mux C++ source
compiled before the final USB-only fixes, and its key symbols match the C
objects. The final simplified key and USB sources compiled for MH2LM, and
their machine-code disassembly is identical to the prior MH2LM fixed-IRQ
draft.
GUACAMOLE and SWEET are outside the validation scope. `bash -n` passed for
the packer, and the tracked script/docs diff
passed `git diff --check` before commit.
**0114a15 includes 0139; phone validation remains open.** Temporarily boot
the packaged `--irq-only` image. Keep the 0114a13 combined image as the prior boot
baseline. A USB IRQ failure also stops live ACM output, so export a fresh
persistent SWEETLOG after shutdown; compare boot args and IRQ failure lines.
Verify Power, Volume Up/Down including repeated combinations, touch after
display sleep/wake, USB enumeration and streaming, and concurrent activity.
The 0138 ack retry has not yet been phone tested on its own.

**0114a13 three-driver test exposed one key-mux ack failure; 0138 prepared:**
`logs/live-e-0114a13-touch-irq-all.txt` SHA-256
`8c2152e91d9426f95f5492de7a0336ff6de476de01b8244313b543e4b1a2f02e`
confirms boot args `sweettouchirq=2 sweetusbirq=2 sweetkeysirq=3`.
Touch GPIO122 / INTID 240 stayed IRQ-driven through 1334 hits and 1334
reports (`events=5753`, `i2c-errors=0`, `empty=0`); fingers 0–9 produced
HID down/up. Screen off/on appears at lines 3535/3571, followed by a
touch down at line 3603. Touch `missed` intermittently reached 1, below
its 8-report fallback threshold. DWC3 reached 836 IRQ hits with
`empty=0 missed=0` and continued streaming the console. No touch or USB
fallback appeared.

The key mux armed three routes. Volume Down got four hardware route hits
with correct HID down/up; Volume Up got two route hits, then the second
ack failed at line 3485. Immediately before it, a key observer read
reported `gpio-ok=0 arb-status=0x9` (PMIC-arb DONE|DROPPED per the target
Linux driver). `ackRoute()` calls `disableHardware()` on failure, so the
mux went to polling fallback; subsequent HID events for all three keys,
including Power and display off/on, do **not** prove concurrent key IRQ.
No hardware Power route hit appears in this capture. The three-driver
acceptance criterion is therefore still open, despite functional keys.

New `0138-mh2lm-spmi-mux-ack-retry.patch` retries only the idempotent
channel/PMIC key IRQ clear sequence up to three times with 20 us between
attempts, logging recovery; three failures still trigger the existing
polling fallback. It applied cleanly with `patch -p1 --dry-run` and real
application on the exact 0114a13 build-tree source. **0138 has not been
compiled or phone-tested.** After commit, user push and a new CircleCI
build are needed; then package the new kernel with `--touch-irq` and
repeat the combined test, explicitly checking Volume Up retry/route,
Power route and no mux fallback.

**0114a13 real touch GPIO122 IRQ passed; three-driver test next:**
`logs/live-e-0114a13-touch-irq.img` is ASCII/CRLF text despite its suffix;
SHA-256 `9b19fd6030f2678cf09e46f31d8396b6fab3b5cae66c3f7d400d2d915ab3b2de`.
Boot args confirm `sweettouchirq=2 sweetusbirq=2 sweetkeysirq=0`.
TouchComm armed GPIO122 / INTID 240 with `cfg=0x71`, then real IRQs
occurred with pin status `0x1` and ATTN low. The final counters in the
capture reached 990 hits, 990 reports, 990 events and zero I2C errors.
Touch move records and HID down/up records appear repeatedly. `empty=0` throughout;
one transient `missed=1` at 288 hits returned to zero by 328 hits, with
no fallback. USB IRQ also reached 526 hits, `empty=0 missed=0`, and ACM
console output remained active with TX errors zero. The capture has no
multi-finger (`f1+`) records or tagged sleep/wake sequence, so those
cases remain unverified. Next boot the existing
`out-bs/boot-16k-0114a13-touch-irq-all.img` and check simultaneous real
touch, USB and three-key IRQs; include multi-finger and first touch after
screen wake. No new kernel build or push is needed for this phone test.

**0114a13 touch GIC-only phone result; advance to real GPIO122 IRQ:**
`logs/live-0114a13-touch-irq-keys.txt` SHA-256
`076631f8e9f2f7e2117e7aeeea7f85231c09c3e2867e6efc42d17923ea85f0af`
contains boot args `sweettouchirq=1 sweetusbirq=2 sweetkeysirq=3`.
TouchComm initialized, INTID 240 software pending returned exactly
`hits=1 clear=1 pending=0 active=0`, and the log explicitly says the
TLMM pin interrupt stayed disabled with 10 ms polling retained. Touch
reports/events reached 37/37, `attn-low=37`, `i2c-errors=0`, with HID
down/up and motion records. USB IRQ reached 366 hits with `empty=0
missed=0`. No touch/USB fallback or kernel panic appeared in this live
capture. It has no key IRQ event records, so it does not add new evidence
for key operation. **This proves the GIC software path only.** Next test:
`out-bs/boot-16k-0114a13-touch-irq.img` with `sweettouchirq=2` and key
polling; capture USB live log. Check real `SWEET TOUCH IRQ: armed`/`hit`
records, touch HID events and absence of fallback before trying
`touch-irq-all`.

**0114a13 build artifacts received; touch IRQ phone test pending:**
`logs/xnu-buildtree-0114a13.gz` and `logs/xnu-kernel-0114a13.gz` are present.
The build tree contains the `sweettouchirq` modes and touch IRQ probe code;
its 30 staged BearSSL files are byte-identical to the vendored copies.
The kernel manifest matches SHA-256
`d7e44e609cf826741e0f4651af5834b68a4e2e73902f57d42810a5174b8ed10a`,
and the kernel contains the touch, USB and key-mux IRQ markers. The 0114a13
build log was not downloaded, so compiler warnings were not reviewed.
`tools/mk-mh2lm-event-matrix.sh ... 0114a13 --touch-irq` verified the
manifest and each FAT ramdisk copy, then created eight `fastboot boot`
images in `out-bs/`. Test in order:
`boot-16k-0114a13-touch-irq-gic.img` (software pending only),
`boot-16k-0114a13-touch-irq.img` (real touch IRQ, key polling), then
`boot-16k-0114a13-touch-irq-all.img` (touch+USB+three-key IRQ). Advance
only after the preceding stage boots and its SWEETLOG passes review.
No 0114a13 phone boot or touch IRQ hardware delivery has yet been observed.

**0114a12 BearSSL CI blocker fixed for 0114a13:**
`0020-stage-bearssl.sh` now stages the same 30 BearSSL files from
`xnu/patches/bearssl-vendor/` instead of fetching from the failing upstream
Git server. Both CI workflows copy that directory into `dxb/patches/`.
The vendor tree matches two previous local CI-staged copies byte-for-byte;
it includes the MIT license and the existing `KERNEL` limits.h adaptation.
See `xnu/patches/BEARSSL-VENDOR.md` for provenance and the tree fingerprint.
The user-cloned `ref/BearSSL` mirror lacks pinned commit `7bea48e5`; no
substitute version was introduced. A simulated CI copy and stager run
produced a byte-identical source tree to the prior successful build; both
workflow YAML files parsed, `bash -n` and `git diff --check` passed. The
subsequent 0114a13 build artifacts are present and include the staged
BearSSL tree. The 0114a13 build log was not provided.

**0114a12 CI infrastructure failure before 0137 compile:**
`logs/xnu-build-log-0114a12.txt` ends while dxb runs the existing
`xnu/patches/0020-stage-bearssl.sh`. Its pinned BearSSL fetch from
`https://www.bearssl.org/git/BearSSL` receives HTTP 502 and `git fetch`
exits 128. The job stops before applying the XNU patch files or compiling
`sweet_touch_tcm.c`; this log contains no 0137 compiler/linker error.
The stager was unchanged by 0137 and has worked in earlier builds.
The vendor fix above removed the unavailable Git fetch. The subsequent
0114a13 artifacts include the 0137 source; phone behavior remains untested.

**0137 touch IRQ implementation (0114a13 artifact received; phone test pending):**
The active 0114a11 TouchComm driver polls GPIO122 every 10 ms; the target
MH2LM touch DTS has `interrupt-parent=&tlmm`, GPIO122 low level, and the
SM8150 TLMM node advertises GIC SPI 208 (candidate XNU INTID 240).
GPIO122 is absent from the SoC direct-connect table, so 0137 stages the
TLMM summary path. See `docs/TOUCH-IRQ-HARDWARE-BASELINE.md` for the
register layout, evidence limits, and test criteria.

`xnu/patches/0137-mh2lm-touch-tlmm-irq.patch` adds `sweettouchirq=0`
(default 10 ms polling), `1` (GIC software pending with TLMM GPIO122
disabled), and `2` (probe-gated real level-low TLMM IRQ). The GIC top half
masks SPI 240 and wakes the existing TouchComm thread. That thread reads
the message, clears GPIO122 interrupt status, rearms SPI 240 and uses a
200 ms ATTN-only recovery check without idle I2C reads. Bounded empty,
missed, I2C-error and ATTN-without-progress paths disable pin/GIC IRQ
and restore the 10 ms poller. `tools/mk-mh2lm-event-matrix.sh --touch-irq`
adds GIC probe, touch IRQ with key polling, and touch+USB+key IRQ profiles
from one kernel artifact; CDC-ACM remains enabled in all three.

Both modified files compile as arm64 Mach-O objects with CI-derived flags;
`_sweet_touch_irq` is defined and referenced with the same C ABI. The
patch applied with `patch -p1 --dry-run` on the exact 0114a11 buildtree,
and the applied files byte-match locally compiled sources. The packaging
script passes `bash -n` and rejects the older 0114a11 kernel artifact
because it lacks the touch IRQ marker. **0114a13 contains the 0137 code,
but has no phone result.** Test GIC-only before real TLMM IRQ and the
three-driver combination. Do not treat a software pending hit alone as
proof of real GPIO122 delivery.

**0114a11 USB+key IRQ parallel phone result; second child target reached:**
`logs/sweetlog-0114a11-usb-irq-keys.bin` SHA256
`43191eead2b3c3a054db873f3bd5c09268e2d072485826bf665f9cecf6cc3a1b`
is a fresh version-1 SWEETLOG (4 MiB ring, total 265217, flush 10,
no wrap) with `sweetusbirq=2 sweetkeysirq=3`. The matching live ACM
capture `logs/live-0114a11-usb-irq-keys.txt` SHA256
`56b2723cf1dc4b1dc008e7b55a094b2257a7bb389ccaaf4104b64cebbee1ad8d`
contains the persistent log's final 4 KiB as a byte-exact substring.
The phone reaches SpringBoard; the ACM port opens (`dtr=1`), console TX
reaches 214/215 with zero errors, USB IRQ counters reach 470 with
`empty=0 missed=0`, and a later hardware hit is printed at IRQ 512.
USB resets twice and reconfigures ACM twice (no repeated-reconnect trial).
No USB IRQ fallback appears.

The SPI 513 mux arms three routes and logs real hits for Power (6),
Volume Down (4) and Volume Up (7), with corresponding HID down/up records
for all three. No mux fallback or stuck-pending message appears, and live
USB output continues while keys are exercised. One Volume Up release
contains `SWEET KEYS: read error pon-ok=0 gpio-ok=1 g2-ok=1
arb-status=0x1`; the Volume Up GPIO sample and HID release still succeed.
Keep this isolated PMIC PON read failure in the record for later monitoring;
it did not prevent this key event or trigger a mux fallback. The user
previously confirmed the repeated reconnect failure also exists with
the old polled gadget and did not request a separate A/B test.

The USB IRQ child goal is met on the tested scope: real device IRQ drives
event consumption, live ACM output and all three key IRQ routes operate
concurrently without conflict. Next planned child goal is touch IRQ;
the active 0114a11 driver is `sweet_touch_tcm.c` (TouchComm ready in this
log), which samples active-low TLMM GPIO122 every 10 ms and forces a read
every tenth tick. The target touch DTS specifies `interrupt-parent=&tlmm`
and `<122 0x2008>`; `sm8150-pinctrl.dtsi` lists a TLMM summary GIC SPI 208
(candidate INTID 240), but GPIO122's actual TLMM/PDC routing still needs
verification before arming. Stage touch IRQ behind a boot argument, keeping
the currently working touch path as fallback.
The separate USB gadget repeated-reconnect defect remains open.

**0114a11 real DWC3 IRQ first phone result (prior stage):**
`logs/sweetlog-0114a11-usb-irq.bin` SHA256
`eb2274f725285048fd27e1c2acde92e68ca052443d47e1a30c1e97ae4731de47`
is a fresh version-1 SWEETLOG (ring 4 MiB, total 224043 bytes, flush 11,
no wrap) with `sweetusbirq=2`. The INTID 165 software-pending gate passes
(`hits=1 clear=1 pending=0 active=0`), DWC3 unmask reads back `size=0x1000`,
and real IRQ hit lines run from 1 through 16 and again at 256. Periodic
counts rise through 82, 147, 229, 247 with `empty=0 missed=0 count=0`;
there is no fallback or panic. The first two enumerations reach
`ACM endpoints configured` and the log includes SpringBoard activity.
This proves real DWC3 event IRQ delivery, drain/ack and rearm.

The USB host behavior has a known reconnect limitation: after the second ACM
configuration, the log contains 13 additional bus reset/connect cycles
(15 total, versus 2 in the GIC-only/polled 0114a11 run), no subsequent
setup request or ACM configuration, and the last counters show
`configured=0 dtr=0 TX=0/2`. The earlier 0114a10 polled run had DTR=1
and TX completions; both 0114a11 captures had DTR=0, so host ACM usage
in this trial is unknown. `ep0 cmd 0x6 status 1` is `STARTTRANSFER`
reporting no transfer resource per Linux DWC3 command-status handling;
it appears after reset but also occurred in the healthy polled run and
is not alone proof of cause.
The user confirms repeated unplug/replug near the end and reports Windows
eventually displayed "unrecognized USB device". The user also confirms that
the same intermittent failure had already occurred after many reconnects
with the old 1 ms polling driver. Treat this as a pre-existing USB gadget
reconnect defect, not evidence of a new IRQ regression; the requested
polling A/B retest is cancelled.
The pre-existing `ud_event()` reset path only clears DCFG address and three
software flags, then calls `ud_start_setup()`; it does not end old transfers
or reset EP0/other endpoint resources as the reference Linux DWC3 reset
handler does. That is a concrete candidate for a reconnect bug, not yet a
proved cause. Track endpoint reset cleanup as separate USB gadget work.
The real IRQ path itself passed its first phone test (real hit/ack/rearm,
no fallback); next boot `out-bs/boot-16k-0114a11-usb-irq-keys.img` with
`sweetusbirq=2 sweetkeysirq=3`. Keep the cable connected for this IRQ
concurrency test, confirm live USB console if available, exercise all three
keys, and pull a fresh SWEETLOG. Require both `SWEET USB IRQ: hit` and
`SWEET MUX: hit route=` with no IRQ fallback or key-function regression.

**0114a11 GIC-only phone result:**
`logs/sweetlog-0114a11-usb-irq-gic.bin` SHA256
`2513fb16c130de05b93c59f7870d544f24379f730ede815f4b88812ea1ba3ad7`
is a fresh version-1 SWEETLOG (ring 4 MiB, total 239345 bytes, flush 9,
no wrap) with `sweetusbirq=1`. It reports `GIC intid=165 ctlr=0x12
group-read=0 cfg=0 pri=0xa0 route=0`, then `software-pending probe
hits=1 clear=1 pending=0 active=0` and `probe-only mode; DWC3 interrupt
masked, 1 ms polling retained`. USB reaches `ACM endpoints configured`,
with 263 processed events, and SpringBoard subsequently initiates a normal
shutdown. The final console counters show `configured 1, dtr 0, TX 0/2`;
the host did not open the ACM port during this capture, so this log does
not establish live USB output. It does establish the GIC software-pending
path and continued USB enumeration under polling. There is no USB IRQ
fallback line. Next test: `out-bs/boot-16k-0114a11-usb-irq.img` with
`sweetusbirq=2`; inspect real `SWEET USB IRQ: hit` lines, event drain,
enumeration/reconnect, and fallback counters before the key-combined image.

**0114a11 DWC3 IRQ artifact checkpoint (real IRQ phone test pending):**
The user pushed 0136 commit `9bbb03c` and downloaded
`logs/xnu-kernel-0114a11.gz` (SHA256
`fce642c79442fe143310f40daf08bc60309206e6b305ed2c93ec096801231238`)
and `logs/xnu-buildtree-0114a11.gz` (SHA256
`76d24fd0f6a193f90b6bff963c2af17fe1a0f48d3cdc2a427d16f73733192632`).
The kernel manifest verifies Mach-O SHA256
`99048ff11ca5a6ab3f0917faed55497aecd9e1042d037c92e843d2dbfc05b0b6`;
both modified buildtree files byte-match the locally compiled 0136 sources.
The linked kernel exports `_sweet_usb_irq` and `_sweet_spmi_mux_irq`.
No `xnu-build-log-0114a11.txt` was downloaded, so the CI command transcript
was not reviewed; the complete linked kernel is present.

`tools/mk-mh2lm-event-matrix.sh logs/xnu-kernel-0114a11.gz 0114a11
--usb-irq` generated and byte-verified three staged `fastboot boot` images:

- `out-bs/boot-16k-0114a11-usb-irq-gic.img` SHA256
  `be260789db02702a38beff6a288b5b3a2f36f833c0c1957dcc2edd47241f56c6`:
  `sweetusbirq=1`, DWC3 IRQ masked, GIC-only probe and normal USB polling.
- `out-bs/boot-16k-0114a11-usb-irq.img` SHA256
  `c534b80d77b016b6b749094ab83d62e34c06f33c3dacff0ea177ea434b17063a`:
  `sweetusbirq=2`, probe-gated real DWC3 IRQ.
- `out-bs/boot-16k-0114a11-usb-irq-keys.img` SHA256
  `061c02ad01f1bffd953cf916aa6239f412dd278e5c87ae1dfe52aca040ff3ef8`:
  `sweetusbirq=2 sweetkeysirq=3`, both IRQ routes concurrently.

Boot only the GIC image first. Require a fresh log with
`software-pending probe hits=1 clear=1 pending=0 active=0`, a normal desktop,
and working CDC-ACM before testing the real DWC3 IRQ image. `group-read=0`
alone is expected in the non-secure view. Stop on failure and retain the
fresh SWEETLOG. The GIC-only phone result above now supersedes the
earlier absence of a phone result.

**0136 implementation (historical local checkpoint):**
`xnu/patches/0136-mh2lm-dwc3-irq.patch` adds optional `sweetusbirq=1`
(GIC INTID 165 software-pending probe, DWC3 masked) and `sweetusbirq=2`
(probe-gated real DWC3 IRQ). Default mode 0 retains the proven 1 ms USB
polling path. The top half masks SPI 165 and wakes the existing USB event
worker; that worker drains/acks `GEVNTCOUNT` and rearms the SPI. A 200 ms
recovery check and bounded missing/empty IRQ thresholds fall back to the
old poller. `tools/mk-mh2lm-event-matrix.sh --usb-irq` packages GIC-only,
USB IRQ, and USB IRQ plus `sweetkeysirq=3` images from one CI artifact.
The exact 0114a10 buildtree accepted the patch with `patch -p1`, and the
resulting files byte-match the locally compiled sources. Both changed C
files compiled as arm64 Mach-O objects with CI-derived 0114a7 flags;
`_sweet_usb_irq` is defined in `sweet_usb.c` and referenced by `sleh.c`.
`bash -n`, the non-patch staged whitespace check, and a `patch -p1` dry run
passed. At this point no CircleCI build or phone test had run for 0136;
the 0114a11 artifact verification above supersedes the build status.

**Current checkpoint (0114a10 three-key IRQ routes passed on phone):**
Power, Volume Down and Volume Up each have real SPI 513 route hits through
`SweetSpmiInterruptMux`; USB CDC-ACM streamed concurrently. Mode 3 still
has a 200 ms recovery poll, and the default boot mode still has 20 ms
polling. Next major child target is DWC3 USB INTID 165 IRQ; use
`docs/USB-IRQ-HARDWARE-BASELINE.md` and `docs/IRQ-BRINGUP-LESSONS.md`.
The remaining material in this section is the historical 0114a sequence.

**Historical 0114a7 checkpoint:** 0114a7 closed the PON/GPIO6 route and
peripheral read-only
preflight; both EE0 APIDs are accessible, but their PMIC interrupt gates
remain off and keys still poll every 20 ms. The target SM8150 Linux source
maps PDC pin 1 to GIC INTID 513. Patch 0133 adds independent read-only
snapshots of PDC pin 1 (`sweetkeysirqdiag=1024`) and GIC SPI 513 (`2048`),
with a combined `3072` profile. The patch applied cleanly to the 0114a7
buildtree and passed a local single-file syntax check. The user then pushed
it and downloaded the 0114a8 buildtree/kernel artifacts. The 0114a8
buildtree contains the probe source, the kernel manifest matches SHA256
`accebe637ca034a8353d980ffd1c83c591b226b99200b529b3313593fb05ea1b`,
and the kernel binary contains the PDC/GIC diagnostic strings. The CircleCI
build log was not supplied; the kernel artifact itself is complete. The
PDC-only image was subsequently tested below.

The reusable mistakes and gate order learned during 0114a–0114a10 are in
`docs/IRQ-BRINGUP-LESSONS.md` (artifact provenance, fresh logs, EE ownership,
GIC delivery versus IGROUPR readback, staged hardware enable, IRQ evidence,
and USB logging). Apply it before the DWC3 and touch IRQ work. It is a
process record; it does not change the already tested 0114a10 kernel.

`tools/mk-mh2lm-event-matrix.sh logs/xnu-kernel-0114a8.gz 0114a8
--key-irq-upstream` generated four temporary images from this single kernel:
`out-bs/boot-16k-0114a8-baseline.img`
(SHA256 `47ffac3c1374108cfdf7756ae3ae473599adb6f679b1f1ae046fdafd7f8a5171`),
`out-bs/boot-16k-0114a8-key-irq-pdc.img`
(`7030ffc88a7cb1311090d3441a5c8e58aecae7608d30fcf1f54b5c34b76dc7e4`),
`out-bs/boot-16k-0114a8-key-irq-gic.img`
(`b49fd5e5dec10e1ef02b8ab0dc3b3254946c7c3c2b852cc16d7f4dc4631bfc73`),
and `out-bs/boot-16k-0114a8-key-irq-upstream.img`
(`5646f803be60b784791c588ab234da57cacd191c30eed3d69e6c56150a99b1ec`).
The four images differ only in boot arguments. **Correction:** the source
mapping, register offsets, and INTID were already known before 0133; I
incorrectly treated a full set of upstream idle snapshots as a prerequisite
to implementation. The newly available
`logs/sweetlog-0114a8-key-irq-pdc.bin` (SHA256
`ec2e2e4b155bb7bdaa9e0f079e7d9ce5e9f3c7123c3098c3c7e88a2176a94521`,
SWEETLOG total 222784, flush 10) is a fresh `sweetkeysirqdiag=1024` boot,
reaches SpringBoard and reads `pin=1 intid=513 enable-bank=0 enabled=0
config=0x4`. This confirms the PDC pin is currently disabled and configured
active-high. Together with 0114a7's PMIC gate=0 readings, this explains why
no key IRQ can yet arrive. The GIC-only and combined read-only images need
not be tested: current `pe_fiq.c` configures GIC global forwarding and the
timer/SGI, but does not configure SPI 513; the new driver can set and read
back its own Group 1, level, priority, route and enable state. No further
0114a8 boot is required for that work.

Patch `0134-mh2lm-spmi-interrupt-mux.patch` now stages the current-XNU
SPI 513 callback, one provider-less software `IOInterruptEventSource` on
an `IOWorkLoop`, and a table of Power/Volume Down/Volume Up PMIC routes.
Only EE0 IRQ-owned APIDs are mapped; the GIC top half masks before EOI and
queues the worker, which scans/acks routed pending bits and wakes the
existing key worker. `sweetkeysirq=0` (default) retains the 20 ms polling
baseline and does not call mux setup. Explicit modes: `1` tests only the
software handoff, with no PMIC/PDC/GIC gate writes; `2` arms Power only,
while all keys keep 20 ms polling; `3` arms all three keys with a 200 ms
recovery poll. Hardware modes configure both-edge PMIC key latches, the
GIC SPI 513 Group 1/level/priority/boot-CPU route, and PDC pin 1 only
after the software event source exists. Each failed readback/ack/stuck
pending path masks GIC/PDC and disables the PMIC gates, reverting to
20 ms polling. A 256-interrupt lifetime cap is a test-stage storm guard.
The internal dispatch is a route table with callback/context, not a
feature switch. The exact 0114a8 buildtree accepted 0134 in `patch --dry-run`;
the changed key C file, `sleh.c`, and new IOKit C++ file passed local
single-file compilation against 0114a7 CI flags after the final route-table
refinement; all three emitted Mach-O objects with matching C ABI symbols.
The non-MH2LM stub also passed a local syntax/object check. The user pushed
0134 and downloaded `logs/xnu-kernel-0114a9.gz` and
`logs/xnu-buildtree-0114a9.gz`. The 0114a9 buildtree includes the mux source;
the kernel contains `_sweet_spmi_mux_irq`, `_sweet_spmi_mux_start`, and the
key notify/fallback symbols. Its manifest verifies kernel SHA256
`a1e8d68b5f972d8672cb1a76a4735adfc6c868b5e68863e89e30b9b81892cd4d`.
The downloaded `logs/xnu-build-log-0114a9.txt` is zero bytes, so no CI log
detail could be reviewed. The complete kernel artifact establishes that a
kernel was linked. Device behavior remains unverified.

`tools/mk-mh2lm-event-matrix.sh logs/xnu-kernel-0114a9.gz 0114a9
--key-irq-mux` generated and byte-verified these four relevant images from
the same kernel, all Android boot header v1 / 2048-byte pages:

- `out-bs/boot-16k-0114a9-baseline.img` SHA256
  `c505be3f6a931fae61027166e641030cd639b216ad1bfd16e43edaf3881e519f`;
  no `sweetkeysirq` argument.
- `out-bs/boot-16k-0114a9-key-irq-mux-soft.img` SHA256
  `2c0c5c7e6b2940e66bd0d609b562a5c222357d369ab17ffd8868c96517b0156f`;
  `sweetkeysirq=1`, software event handoff only.
- `out-bs/boot-16k-0114a9-key-irq-power.img` SHA256
  `d03ebaf06022fee346c8b90944db5cec17b08de64c1c6cdb87308f35b8116c2a`;
  `sweetkeysirq=2`, single Power hardware IRQ and 20 ms poll fallback.
- `out-bs/boot-16k-0114a9-key-irq-all.img` SHA256
  `134ba3c98c3f4acc52790bcce6581af7b299df4eafbfaebae58d6755a8d6a8f1`;
  `sweetkeysirq=3`, three routes and 200 ms recovery polling.

Test each with `fastboot boot` only, in that order. Extract a distinct fresh
SWEETLOG after each stage, confirming its boot arg and later boot content.
Stop on LG BSP return, lost key function, `SWEET MUX` fallback/stuck/ack
failure, or absent `SWEET MUX: hit route=power` after repeated Power presses
in mode 2. Only test mode 3 after mode 2 reports route hits and normal
press/release. The local documentation-only commit for this packaging does
not need to be pushed for the phone test.

**0114a9 stage 3 result (2026-11-04):**
`logs/sweetlog-0114a9-key-irq-power.bin` SHA256
`fc4638d2d7aa8cf4c87521313a9dd7d9e93888078c0d90dcb401cc3ad3814ce6`
(SWEETLOG total 249181, flush 28) confirms `sweetkeysirq=2` and reaches
SpringBoard. The mux prints `GIC intid=513 group=0 cfg=0 pri=0xa0 route=0:0`,
then `PDC polarity or GIC config check failed; polling retained`; the key
driver prints `mux mode=2 unavailable; 20 ms polling retained`. Multiple
Power down/up reports follow, but there is no `armed` or `hit route=power`.
Thus the **Power IRQ test did not pass**: key input works through polling
fallback. The GIC Group 1 bit readback alone caused `configureGic()` to
return false before PMIC/PDC gates were opened. Earlier shim 2c found
GICD/GICR IGROUPR bits reading zero from the non-secure view even while
Group 1 timer/SGI delivery worked; this makes that readback a questionable
health check, but does not establish that SPI 513 is deliverable. Check the
GIC security/group semantics and design a bounded diagnostic before changing
the gate sequence. **Do not boot stage 4** with 0114a9; stage 2 need not be
repeated to diagnose this stage 3 failure. No hardware IRQ acceptance
criterion is met yet.

**Logging for the next key-IRQ iteration:** The 0114a9 key profiles used
`sweetusb=0` solely because the packaging script inherited an old isolation
setting. CDC-ACM output was already validated on MH2LM in 0112m10/m11 and
0113 USB and USB+battery tests. It is the normal live console for subsequent
key-IRQ work, not another test target. The current DWC3 event path is polled
and has no known SPI 513 routing conflict with the PMIC key mux. The
`--key-irq-mux` packaging profiles now use `sweetusb=2 sweetusbstep=6
sweetvbus=1` by default. Start `tools/sweet-usbacm-log.sh` before
`fastboot boot` and read `/dev/ttyACM*` as soon as it enumerates. Keep
the persistent SWEETLOG for the pre-enumeration interval or if USB never
comes up. Do not schedule a separate USB verification round for this work.

**0135 next kernel change and 0114a10 packaging (no phone result yet):**
`xnu/patches/0135-mh2lm-spmi-gic-delivery-probe.patch` keeps the SPI 513
Group 1 write but no longer treats the non-secure IGROUPR readback alone as
proof of delivery. Before any PMIC/PDC gate opens, it requires PDC pin 1 and
SPI 513 to be idle, configures the SPI, software-pends it for at most 100 ms,
and accepts the hardware key mode only if `sleh_irq` sees exactly one hit and
pending/active state clears. Otherwise it masks/clears SPI 513 and retains
20 ms polling. This directly tests the current blocker while preserving the
existing bounded mux worker and key fallback. The patch applies byte-for-byte
to the downloaded 0114a9 buildtree source; its changed C++ file produced an
arm64 Mach-O object with `_sweet_spmi_mux_irq` and `_sweet_spmi_mux_start`
using local CI-derived flags. The user pushed commit `58b714d` and downloaded
`logs/xnu-kernel-0114a10.gz` (SHA256
`073f36d69a6e1ab682fe53c84365f42f1b4b028f49950f26819f0b6a5f96582c`)
and `logs/xnu-buildtree-0114a10.gz` (SHA256
`7dab8fa6e5c799466911e3e63c7d87b9c63bf2ebf9ab0928000bbf85ddde3089`).
The buildtree's mux source byte-matches the 0135 local version, and the kernel
manifest verifies kernel SHA256
`58b7eb8921289b925f4b7f544ad46ff099a53ee7b715ce21f6045b8af2b4428d`.
The kernel contains the new probe strings, the mux entry symbols and the
previously validated ACM code. No build log was supplied; kernel/buildtree
artifacts are complete.

`tools/mk-mh2lm-event-matrix.sh logs/xnu-kernel-0114a10.gz 0114a10
--key-irq-mux` generated byte-verified temporary boot images. The next image
is `out-bs/boot-16k-0114a10-key-irq-power.img` SHA256
`9b238a33de3a4c4ab238cc85a9e0a26ade1a5f65846a607007f9fa55586b95c3`.
Its unpacked TOML contains `sweetusb=2 sweetusbstep=6 sweetvbus=1
sweetkeys=1 sweetkeysirq=2`. The already prepared three-key image
`out-bs/boot-16k-0114a10-key-irq-all.img` SHA256
`64a9a6184bcadb274260fdf6cf3e681c8e1c60fbffcf43628c1958badd2e81fe`
uses the same USB arguments and `sweetkeysirq=3`. Use `fastboot boot` only.
Start the existing USB reader before
boot and retain SWEETLOG as the early-boot fallback. First boot the Power
profile. Success requires `GIC software-pending
probe hits=1 clear=1`, `SWEET MUX: armed mode=2`, and real
`hit route=power` records with normal press/release; only then boot the
three-key profile. Probe failure or absent route hits means stop and inspect
that log, not repeat the software-only mode.

**0114a10 Power phone result (2026-11-04):**
`logs/sweetlog-0114a10-key-irq-power.bin` SHA256
`89f49b75c75deac3de517494385ecaaf37cd78a2daba17c6f8815719bd4f45ef`
(SWEETLOG total 234692, flush 13) confirms `sweetkeysirq=2` and reaches
SpringBoard. SPI 513 reports `ctlr=0x12 group-read=0 cfg=0 pri=0xa0
route=0:0`, then `GIC software-pending probe hits=1 clear=1 pending=0
active=0`. The Power PMIC route is APID 248 bit 0, and the mux arms with
`pdc=0x2`. There are 16 `hit route=power` records, matching 8 alternating
Power down/up reports; no mux fallback, stuck-pending fault, IRQ limit or
kernel panic is present. This **proves the SPI 513 callback, mux workloop and
Power PMIC IRQ route operate on hardware** despite the non-secure IGROUPR
readback of zero. Mode 2 still retains 20 ms key polling, so this log alone
does not establish event-only key latency. The USB live capture
`logs/live-0114a10-key-irq-power.txt` SHA256
`63edc16b7c6bf56d595f121c9c1eb7790a3f983d5d0283deea0b264f0726d9e2`
contains the same route hits; ACM reaches `console=224582 bytes`, TX
`196/197`, `err=0`, `configured=1`, `dtr=1`. The final live text shows
normal userspace teardown and `PSCI SYSTEM_OFF`.

**0114a10 three-key phone result (2026-11-04):**
`logs/sweetlog-0114a10-key-irq-all.bin` SHA256
`6d8dc10c01ff8084dd4c50acae3ab9741329559ea5a3fbab51ef684f0a20a83a`
(SWEETLOG total 283673, flush 14) confirms `sweetkeysirq=3`, the same
SPI 513 probe `hits=1 clear=1 pending=0 active=0`, all three PMIC routes,
and `armed mode=3 pdc=0x2`. The first 32 recorded route hits comprise
Power 9, Volume Down 14 and Volume Up 9; all three produce physical IRQs
through the GIC callback and mux worker. Across the full capture, key state
reports are alternating and balanced: Power 7 down/up pairs, Volume Up 8,
Volume Down 13. The route log deliberately prints only hits 1–32 and then
every 64th hit (`routeCount <= 32 || (routeCount & 63) == 0`), so absence of
later individual `hit` lines is not evidence of a stall. No mux fallback,
stuck IRQ, storm limit or kernel panic appears. SpringBoard runs and the
capture ends with normal userspace teardown and `PSCI SYSTEM_OFF`.
`logs/live-0114a10-key-irq-all.txt` SHA256
`a6608efbaa50a44e1833b933df5cb0a8f71b61d4160203c0a756896cb363e534`
contains the same IRQ hits; USB ACM reaches `console=266746 bytes`, TX
`333/334`, `err=0`, `configured=1`, `dtr=1`. The hardware three-route mux
path is validated for this session. Mode 3 retains a 200 ms recovery poll,
so later key state reports alone cannot prove every transition was woken
by IRQ; this is not an event-only latency measurement.

The SpringBoard warning `CATransformLayer ... changing property
allowsGroupBlending in transform-only layer, will have no effect` occurs
27 times in this capture and already occurred 7 times in the 0114a9 Power
capture, before its hardware IRQ could arm. It is a CoreAnimation property
warning from SpringBoard: that setting is ignored for transform-only
layers. It is not a mux/GIC error and does not by itself establish a
framebuffer defect. Do not treat it as a new 0114a10 IRQ regression.

**Implementation roadmap:** `docs/IRQ-FRAMEBUFFER-PLAN.md` records the agreed
two-stage order: first make the existing key, DWC3 USB and touch paths
interrupt-driven through the XNU-native design, then write the detailed
framebuffer repair plan. It defines the `SweetSpmiInterruptMux` route-table
boundary and the hardware acceptance criteria; keep measured observations and
test results in this file.

### IRQ migration / sub-goal 1: key-route preflight history (2026-11-03 onward)

- **0128 staged locally, not yet through CI or hardware:**
  `0128-mh2lm-spmi-mux-key-preflight.patch` is the first deliberately
  non-invasive step toward `SweetSpmiInterruptMux`. With the opt-in boot
  argument `sweetkeysirqdiag=1`, it records the existing MH2LM PON and PM8150
  GPIO6 routes: APID mapping, PMIC-arb ownership/owner bitmap, channel
  access-enable/status, and raw peripheral RT/type/polarity/enable/latch
  bytes. It does not attach GIC SPI 481, configure PDC, enable or clear a PMIC
  IRQ, or change the established 20 ms key polling path.
- **Why this comes first:** confirmed hardware facts are PON APID 248 and
  PM8150 GPIO6 APID 261 from the 0112m20/0113 logs, plus the SM8150 DTS route
  `spmi@c440000` -> GIC SPI 481. The key sources' interrupt-gate state has not
  yet been captured on MH2LM. Do not infer it from the charger APID 266/269
  diagnostic or from UniXNU's different-board PDC constants.
- **First test after a successful CI artifact:** build a temporary boot image
  from the usual GUI/iOS-rw baseline with `sweetkeys=1 sweetkeysirqdiag=1`
  added, boot with `fastboot boot` only, confirm normal Power/volume operation,
  export the persistent log, and retain the `SWEET SPMI IRQ: keys-*` plus
  `SWEET KEYS IRQ CFG:` lines with the kernel/image SHA256. This test proves
  route facts only; it must not be called an IRQ-delivery test.
- **0114a artifact packaged, hardware test pending (2026-11-03):** CI log
  `logs/xnu-build-log-0114a.txt` applied 0128 and reached `XNU Build Done!`.
  The verified artifact kernel SHA256 is
  `91551d3768fe2de7034a3701493556724466173804e745c0d90e538991745903`.
  `tools/mk-mh2lm-event-matrix.sh` now accepts the opt-in
  `--key-irq-preflight` profile; it generated
  `out-bs/boot-16k-0114a-key-irq-preflight.img` (SHA256
  `de8aeff864334ff77487e23f9aaabeee5aff53a19aa65b1c5ca89008f1d064d0`)
  from that same kernel. Its boot args explicitly keep `sweetusb=0` and add
  only `sweetkeys=1 sweetkeysirqdiag=1` to the known GUI/iOS-rw baseline.
  Boot it temporarily with `fastboot boot`; persistent-log extraction remains
  the required result channel.
- **0114a diagnostic boundary (2026-11-03):** the 0114a baseline image, which
  contains the same new kernel but omits `sweetkeysirqdiag`, reaches iOS.
  The all-at-once diagnostic image returns to the LG BSP. The file initially
  exported as `sweetlog-0114a-keyirq.bin` is byte-for-byte identical to
  `sweetlog-0113-usb-battery-ui.bin` (SHA256
  `3a43214a0dcf445b72c43dba6966aef25d00770afee08d9d1d2bf170745d51b0`),
  so it predates 0114a and cannot identify a failing MMIO read. Follow-up 0129
  changes the diagnostic to isolated read-only stages: helper init, PON route,
  GPIO6 route, PON peripheral bytes and GPIO6 peripheral bytes. One future CI
  artifact will be packaged into those five stages plus a full mask-31 image;
  do not repeat the 0114a all-at-once diagnostic.
- **0114a2 key IRQ stage result (2026-11-03):** the new `key-irq-init` image
  (`sweetkeysirqdiag=1`) reached iOS. Decoded
  `logs/sweetlog-0114a2-keyini.bin` is a fresh 4 MiB SWEETLOG ring (flush
  #13), with `SWEET KEYS: ... pon-apid=248 ...` followed by
  `SWEET KEYS IRQ: read-only diagnostic mask=0x1`; normal key polling also
  produced key events. The next image, `key-irq-route-pon`
  (`sweetkeysirqdiag=2`), returned directly to the LG BSP. This moves the
  boundary to the PON-specific PMIC-arb route probe; GPIO6 and the PMIC
  peripheral interrupt bytes have not been reached. Linux DTS confirms the
  PMIC-arb `chnls` window is 0x0c600000/0x02000000, so the next probe splits
  the PON route into metadata, intr owner bitmap, channel mapping, `ACC_ENABLE`,
  and `IRQ_STATUS` reads (0130/0131) instead of guessing whether the mapping or
  a register access faults.
- **0114a3 artifact provenance and result (2026-11-04):** the generated
  `key-irq-pon-route-map` image was named according to the local 0131 test
  matrix, but `logs/xnu-buildtree-0114a3.gz` contains only the 0130 source:
  `sweet_spmi.h` has no `SWEET_SPMI_IRQ_DIAG_ROUTE_CHANNEL`, and
  `sweet_keys.c` still maps `sweetkeysirqdiag=128` to `ACC_ENABLE` and has no
  `512`/`IRQ_STATUS` stage. The local `c32f76b` commit was therefore not in
  the cloud build; the artifact corresponds to the pushed `dcdf1c9` line.
  Hardware result: masks 32 and 64 reached iOS, while the image labelled
  `...route-map.img` returned to the LG BSP. Because that image actually
  performed the old channel mapping plus `ACC_ENABLE` read, it does **not**
  prove that the physical channel-window map `0x0d580000` is invalid. Push
  `c32f76b` and rebuild before interpreting the pure map stage; do not change
  the address constants based on 0114a3.
- The file later exported as `logs/sweetlog-0114a3-key-irq-pon-route-map.bin`
  is byte-for-byte identical to `...route-owner.bin` (both SHA256
  `6c7e097a1e38f2574bfec403b7124c9af6f1a0f49213c24aa7af1e1f3f7e1c72`).
  It contains `sweetkeysirqdiag=64` and provides no failure-stage trace.
  The source probe iterates all three PPID `0x8` matches seen in the 0114a3
  logs: APIDs 1, 19 and 248. A channel-map failure alone therefore does not
  identify APID 248 or physical address `0x0d580000` as the failing access.
- **0114a5 artifact ready for the pure-map test (2026-11-04):**
  `logs/xnu-build-log-0114a5.txt` applies 0131 and reports `XNU Build Done!`;
  the extracted `logs/xnu-buildtree-0114a5.gz` confirms `128` now selects
  `ROUTE_CHANNEL` (map only), `256` selects `ACC_ENABLE`, and `512` selects
  `IRQ_STATUS`. `tools/mk-mh2lm-event-matrix.sh` generated the three
  corresponding temporary images under `out-bs/` from kernel SHA256
  `b5db4442bda8999263dd24079149f849e20abce2447a624614879a327ee7f2f3`.
  Boot `0114a5-baseline`, then `0114a5-key-irq-pon-route-map`. Stop after the
  map result: the old `128` probe already showed that the subsequent
  map-plus-`ACC_ENABLE` path returns to the LG BSP. If pure mapping succeeds,
  first restrict future register reads to the EE0-owned, IRQ-owned APID 248;
  the existing diagnostic otherwise reaches APIDs 1 and 19 (owners 1 and 4)
  before APID 248. Compare any exported SWEETLOG's boot arguments and hash
  against the preceding capture before interpreting it.
- **0114a5 hardware result (2026-11-04):** both `0114a5-baseline` and
  `0114a5-key-irq-pon-route-map` reached iOS. Their exported 4 MiB SWEETLOG
  files are distinct (SHA256 `12b4b92769b8466052190788466bb18a57ee6cfa8dcfc532fcbb31ea0030eaf6`
  and `1881e0f9edb0c23ebb7da0377e4c1266485954b008fa5221f2e5622dcecbd548`);
  the second records `sweetkeysirqdiag=128` and `route-parts=0x10`. Its
  complete route output covers APIDs 1, 19 and 248, proving all three
  `ml_io_map()` calls returned and logging continued. The earlier failure
  therefore occurs in the channel register read path. It does not yet prove
  which APID caused the abort. Runtime ownership is APID 1 -> EE1, 19 ->
  EE4, and 248 -> EE0 with IRQ-owner bit set. Linux PMIC-arb v5 selects the
  current EE's APID for duplicate PPIDs and skips IRQ status for foreign IRQ
  owners. Patch 0132 applies that rule to channel register diagnostics,
  logging foreign APIDs as skipped. It is read-only and keeps key polling;
  once compiled, test `256` (EE0 `ACC_ENABLE`) then `512` (EE0 `IRQ_STATUS`)
  with `fastboot boot`, stopping if either returns to the LG BSP. A success
  would support, but not alone prove, that 0114a3 failed on an inaccessible
  foreign EE channel.
- **0114a6 download provenance (2026-11-04):** all three files labelled
  `0114a6` are byte-for-byte identical to the corresponding `0114a5` files:
  build log SHA256 `7395210776bb6691473c43727452803fec0f05e2cade0a7e2b011c74a2d569f5`,
  buildtree SHA256 `f70a60a3ab3fadab7ee822596ead939c63f775c7e62e492b009d35780bb5dae7`,
  kernel gzip SHA256 `4a7813229260e18025c0087f393e589f1bb89aee29b006b710974ce572e4ca73`.
  The build log ends its applied-patch list at 0131; the extracted
  `sweet_spmi.c` lacks 0132's `channel skipped` logic. Do not package these
  files as 0132 test images. Retrieve the CI artifacts from a run whose build
  log explicitly applies `0132-mh2lm-key-irq-owned-channel-probe.patch`,
  and verify the extracted source before testing `256` or `512`.
- **0114a7 artifact ready (2026-11-04):** the build log applies 0132 and
  reaches `XNU Build Done!`; the extracted buildtree contains the EE0/IRQ
  owner check and `channel skipped` log before `ml_io_map()`. Its kernel gzip
  SHA256 is `9e8ba6faa035078e6c7026173f6a5363ca4f6dc0f755461bfb79eb32960fde19`,
  distinct from 0114a5/0114a6. The image builder extracted kernel SHA256
  `f535798978d9592557cf23713c1a56ea73e909b809bb7c33e95cd1bc22ec156f`
  and produced `out-bs/boot-16k-0114a7-key-irq-pon-route-enable.img`
  (SHA256 `e127399e64764a36c81a7f3c623c5cd38d619d8267b33be4fd760ff7997ae520`)
  and `...route-status.img` (SHA256
  `395b77b4e219c384c790e56d4dd1869212257c86317b03c3fa67a7b6b0010568`).
  Test enable (`sweetkeysirqdiag=256`) first, then status (`512`) only if
  enable reaches iOS. Both remain read-only; stop at any LG BSP return.
- **0114a7 channel-read results (2026-11-04):** both `key-irq-pon-route-enable`
  and `key-irq-pon-route-status` reached iOS. Their fresh, distinct SWEETLOGs
  have SHA256 `50cb880bb154d14dd7b4ac33812c61ae925b70229c02fe268c0a1b4576619366`
  and `c9f01ad7af4eafc74b816000118fbe4c9e4c1b2223a58f0557a5995333fd3e3a`.
  Boot arguments contain `sweetkeysirqdiag=256` and `512` respectively;
  the log records `route-parts=0x4` and `0x8`, skips APIDs 1/19 (EE1/EE4),
  then completes APID 248 (EE0, IRQ owner) with `acc-enable=0` and
  `irq-status=0`. The PON channel's access enable is currently off; this
  remains a read-only probe, not an IRQ-delivery test. Because the old
  diagnostic failed only when it also read all duplicate APID channels,
  a foreign-EE channel read is the leading cause of the earlier LG BSP
  return, but there is no failure trace identifying APID 1 versus 19.
  Using the same verified 0114a7 kernel, `--key-irq-diag-matrix` generated
  temporary `key-irq-route-pon` (mask 2), `key-irq-route-volume-up` (4),
  `key-irq-cfg-pon` (8), `key-irq-cfg-volume-up` (16), and `key-irq-full`
  (31) images. Test in that order, stopping at any LG BSP return; the final
  combined image is only for after the four isolated stages pass.
- **0114a7 complete read-only key IRQ preflight (2026-11-04):** all five
  follow-up images reached iOS, and their distinct fresh SWEETLOGs contain
  their respective boot arguments `sweetkeysirqdiag=2/4/8/16/31`. Full
  PON route (`2`) skips EE1 APID 1 and EE4 APID 19, then reads EE0 APID 248;
  full GPIO6 route (`4`) reads EE0 APID 261. Both report IRQ owner=1,
  owner bitmap=0, `ACC_ENABLE=0`, and `IRQ_STATUS=0`. PON peripheral config
  (`8`) reports `rt=0x4 edge=0x1 high=0 low=0x3 enabled=0 latched=0` with
  `read-ok=1`; GPIO6 (`16`) reports `rt=0x1 edge=0 high=0 low=0 enabled=0
  latched=0` with `read-ok=1`. The combined `31` image repeats all these
  values and reaches iOS. The five log SHA256s in test order are
  `71dd4517767a407e16ddabebfb7e57868f5e1ca8f601d653debadaa0b6bc2bb7`,
  `e07fcd98ed9c14238ae62e63efefc8e8e7172683c993a5848b83a1a2940ff88c`,
  `415bd9a4e5fafb99f5e269ecb886c2d3c4854da2a9b4a3057e584b3966e6f4c8`,
  `66635d49588a518d5dea6d9213d95e521706c045dba17a19f59719acc8626d13`,
  and `09dfae5868044a39442580f62c6276da00859114f2e9f31dba1b5045e08ed7c1`.
  The old LG BSP return is thus strongly localized to channel-register
  reads on foreign-EE duplicate PON APIDs, not to channel mapping or the
  EE0 APID 248 address. No old failure log identifies whether APID 1 or 19
  was the exact aborting read. This closes the read-only route preflight,
  not the key IRQ goal: arbiter and peripheral IRQ enable bits are still 0,
  with no GIC callback, PMIC mux, or IRQ-produced HID events yet.
- **Key IRQ implementation readiness audit (2026-11-04):** the hardware
  route is now specific: SM8150 `pdc-sm8150.c` maps PDC pin 1
  (`ee0_apps_hlos_spmi_periph_irq`) to GIC INTID 513. PDC uses enable
  bank `0x0b220000 + 0x10` bit 1 and pin config `0x0b220000 + 0x114`;
  neither value had been read on MH2LM at that checkpoint. Linux PMIC-arb v5 writes
  32-bit `BIT(irq)` to the APID channel `IRQ_CLEAR` at `+0x108`, then
  writes the PMIC peripheral `LATCHED_CLR` at `+0x14`; its unmask path
  writes channel `ACC_ENABLE` at `+0x100` and clears the old latch before
  setting `EN_SET` at `+0x15`. These are source facts, not yet validated
  hardware writes. Critically, the 0114a7 *actual* kernel contains
  built-in `SweetCPU`, `sleh_irq` and `pe_fiq`; it does not contain the
  UniXNU reference driver's `armcpu_spi_attach` symbol. The existing
  `sleh_irq` passes non-timer/non-SGI INTIDs to an optional per-CPU
  interrupt handler and EOIs afterward. Implement a single SPI 513
  registration/dispatch path in the current XNU before claiming that API.
  The 0114a8 PDC-only log subsequently read pin 1 as disabled and
  active-high (`enable-bank=0 config=0x4`). A separate idle GIC snapshot is
  not a prerequisite: implement a boot-arg-gated mux/one-source Power IRQ
  test with explicit GIC SPI 513 configuration/readback, a bounded callback,
  pending acknowledgement and polling fallback. Do not arm PMIC/PDC/GIC
  gates from the 0114a7 read-only images.

### Kernel replacement regression baseline (measured through 2026-11-03)

Use this compact checklist when replacing the XNU base, rebasing the MH2LM
patch series, or changing a low-level driver.  The detailed chronology below
remains the authority for test setup and interpretation.  Raw captures named
here live under `/home/shenli/work1/logs/` and are deliberately not committed;
preserve them alongside any replacement-kernel test results.

- **Known-good 0112k6 battery baseline:** `kernel.development.mh2lm` SHA256
  `916a874c723847728aa9fb5d38461e944a8dc9f1199f9e5331930d143f4c45e5`;
  reproducible temporary-boot image
  `out-bs/boot-16k-0112k6-battery-policy-log.img` SHA256
  `fb1b1e8f7bee624d10a30dbfb9190ba19c17b6167dd8a9e0469c339dc1051f66`.
  It uses Android boot header v1, 2048-byte pages and the established GUI/iOS
  read-write baseline plus `sweetbattery=1 sweetbatt=1 sweetbattdiag=1
  sweetbattirqdiag=1`.  Test with `fastboot boot` only.
- **CPU, MMU and interrupt floor:** EL1 entry is with the MMU off; the target
  supports 16K and 4K translation granules.  The validated 16K mapping
  baseline is T0SZ 28, MAIR `0x44ff00`, TCR `0x1409CB51C` and a 40-bit PA
  range.  Framebuffer fill improved from 110 ms (MMU off) to 3.5 ms with
  Normal-NC mapping; the memory loop improved from 70 ms to 0.7 ms.  Any
  replacement that regresses these numbers needs MMU/cache investigation
  before higher-level driver work.
- **Interrupt/SMP floor:** GIC Group 1 is proven on hardware: vtimer INTID 19
  and ptimer INTID 18 each delivered 30 IRQs in 300 ms, and SGI1-to-self
  delivered 10/10.  The timer frequency is 19.2 MHz.  PSCI 1.1 uses SMC and
  `CPU_ON` brings up all seven secondary CPUs (eight cores total).  Do not
  substitute XNU's default Group-0/FIQ or HVC assumptions.
- **USB CDC-ACM baseline:** `sweetlog-0112m11.bin` demonstrates concurrent
  USB logging and battery service: host DTR asserted, `console=222691` bytes,
  TX `237/238`, and zero TX errors.  The controlled VBUS/device-mode setup
  uses the USB-specific image/arguments documented in the 0112m chronology;
  it is a separate test mode, not a default battery-test setting.
- **Input acquisition cost and behavior:** hardware key delivery to IOHID is
  edge-based after acquisition, but `sweet_keys.c` currently polls PMIC PON
  plus GPIO6 every 20 ms (about 100 PMIC reads/second).  TouchComm currently
  samples ATTN GPIO122 every 10 ms and forces a transfer every tenth pass.
  These are established functional fallbacks, not the desired final
  interrupt-driven input design; record responsiveness and CPU impact before
  changing either cadence.
- **Battery/UI data quality:** the published battery percentage must have
  `src=gauge`, not voltage-curve fallback, when the paired raw MSOC bytes are
  valid.  The validated conversion maps raw `0xa5..0xa8` to 65--66%; it
  removed the former cable-induced voltage-curve jump.  Status updates are
  presently a read-only two-second fallback poll.  PMIC event preflight found
  USBIN_PLUGIN at APID 269/bit 4 and CHG_STATE_CHANGE at APID 266/bit 1, but
  both peripheral and PMIC-arb gates are disabled, so no source reaches GIC
  SPI 481.  Do not enable those gates until a real GIC consumer, latch
  acknowledgment order and workloop handoff are implemented together.
- **Physical charging evidence:** `sweetlog-0112m19.bin` records 65 matching
  active-charge samples: `external=1`, `charging=1`, CHGR state `0x3`
  (`FULLON_CHARGE`), enable `0x1`, and roughly -2.2 to -2.3 A battery current
  (negative is charge current for this Qualcomm fuel gauge).  A contiguous
  active set rises from about 4198 to 4304 mV and 63% to 66% MSOC.  Input
  policy is `path=0x95`, valid USB, no input suspend, 1500 mA ICL, AICL
  enabled/rerun, and APSD DTC-done.  This is real PMIC charging under a
  read-only XNU diagnostic build, not a UI-only indication.  It still needs
  repeated long plug/unplug and locked-screen tests before claiming stable
  behavior across all sources, temperatures and sleep states.

**Regression rule:** compare values and state transitions, not merely whether
iOS reaches the desktop.  Keep new raw logs with their kernel/image SHA256,
boot arguments, cable state and elapsed test time; the persistent ring is
circular, so its exported line order alone cannot prove a transition order.

### MH2LM USB compile-gate fix (2026-11-02)
- **Root cause:** the 0112--0114 DWC3 CDC-ACM implementation was present in
  `bsd/dev/sweet_usb.c`, but MH2LM set `SWEET_DEV_USB=0` in the board block
  added by 0103. Its runtime therefore compiled only the deliberate stub:
  `SWEET USB: no driver for this board yet`. This matches
  `sweetlog-batt1.bin`, `sweetlog-usb-batt-visible.bin`, and
  `sweetlog-0112e.bin`; none contains the DWC3/gadget/ACM lines. It is not
  an EP0, DMA, VBUS, or battery failure.
- **Change committed locally (push pending network recovery):** commit
  `04863c0` (`mh2lm: enable guarded USB probe`) makes 0103 set
  `SWEET_DEV_USB=1` for MH2LM, retaining `SWEET_DEV_USB_VBUS_DEFAULT=0`.
  New 0117 makes the USB progress marker a no-op when
  `SWEET_DEV_MARK_PA=0`, so the initial probe never maps or writes physical
  address zero while no verified persistent marker address exists for MH2LM.
- **Local validation:** updated source/header in
  `/tmp/work1-xnu-buildtree-0112.GdVHWS`, compiled `bsd/dev/sweet_usb.c` as
  ARM64 with `ARM64_BOARD_CONFIG_MH2LM` and
  `ARM64_BOARD_CONFIG_SWEET`. `/tmp/mh2lm-sweet_usb-full.o` contains
  `_sweet_usb_probe`, `_sweet_usb_gadget_start`, `_ud_bringup_thread`, plus
  `SWEET USB: GDSC usb30_prim`, `gadget step 0: DMA`, and
  `ACM endpoints configured`; it does **not** contain the stub string. The
  0112 build-log artifact is empty, so `tools/xnu-cc.py` could not recover a
  CI compile command; the direct compiler invocation used the generated
  MH2LM headers and matched include roots instead.
- **0112f CI diagnosis and local correction:** CircleCI itself was unchanged.
  The build log at `/home/shenli/work1/logs/xnu-build-log-0112f.txt` showed
  `0103-mh2lm-board.patch` rejected as `corrupt patch at line 465`. The
  expanded MH2LM USB comment added one line but its unified-diff hunk count
  remained `+20,75`; this made the following hunk look like malformed patch
  syntax. 0103 has been corrected to `+20,76` and its following offset to
  `+248,14`. The CI script then skipped 0104--0111 as a consequence of the
  missing board patch and finally stopped at `Unsupported
  CURRENT_MACHINE_CONFIG MH2LM`; those are downstream errors, not separate
  source or CircleCI failures. Every `xnu/patches/*.patch` now passes
  `git apply --numstat` parsing locally.
- **0112g CI result (2026-11-03):** the XNU build completed and produced
  `kernel.development.mh2lm` (SHA256
  `7fcd288c07f7a5313644d5134931079218b71c51f661666e57f10cf8f20d4b81`).
  It was compiled with both `ARM64_BOARD_CONFIG_MH2LM` and
  `ARM64_BOARD_CONFIG_SWEET`; the kernel contains `SWEET USB: GDSC
  usb30_prim`, `gadget step 0: DMA`, and `ACM endpoints configured`, with no
  `no driver for this board yet` string. Thus the guarded USB probe and full
  CDC-ACM branch are genuinely in this artifact.
- **0112g patch-chain follow-up:** CI did apply 0103, 0104 and 0112--0117,
  but it skipped 0105 because that patch still replaced the pre-USB 0103
  comment verbatim. Its absence made 0108--0111 skip in turn because their
  TCM/display source context was missing. The 0105 hunk now matches the
  post-0103 text and narrowly adds `SWEET_DEV_TOUCH_TCM=1`; on a clean 0112g
  buildtree, 0105 then applies followed by 0108, 0109 and 0110, and 0111
  passes `git apply --check`. Run one more CI build before using the full
  feature kernel; the 0112g kernel itself is sufficient only for the narrow
  `sweetusb=1` read-only USB probe.
- **0112m CI result (2026-11-03, ready for the next test image):** all of
  0103--0117 applied in order, including 0105 and the previously skipped
  0108--0111 display/backlight sequence; the log ends `XNU Build Done!`.
  `kernel.development.mh2lm` SHA256 is
  `a0b532b10b7afda8e27872d7af7bfe2abab3bd8cbeaba5a95426f4c5bedcab6d`.
  Extracted buildtree confirms `SWEET_DEV_TOUCH_TCM=1`,
  `SWEET_DEV_USB=1`, `sweet_display_probe.c` in `bsd/conf/files`, and
  AppleARMBacklight. The final kernel contains the expected DWC3 probe,
  gadget DMA and CDC-ACM strings, plus the SID2 battery and display strings;
  it contains no USB stub string. Use this artifact for the disposable first
  USB test with only `sweetusb=1`; do not enable gadget mode or VBUS writes
  yet.
- **Next after CI:** boot a disposable image with only `sweetusb=1` (keep
  battery arguments off) and pull the persistent ring log. Expected first
  lines are GCC/GDSC/CBCR and DWC3 state, not gadget writes. Only after that
  test `sweetusb=2 sweetusbstep=1` through `sweetusbstep=6`, one step per
  boot; do not set `sweetvbus` until the read-only probe is confirmed.
- **0112m USB-probe image packaged (2026-11-03):**
  `/home/shenli/work1/out-bs/boot-16k-0112m-usbprobe.img` is an Android boot
  image (header v1, 2048-byte page size), built from the verified 0112m kernel
  above and `adt-16k-3g.adt`. Its SHA256 is
  `f114a3c2806236bf1a9fb3b4eac373d225f294f3d45b87cfa804fc7e23083e11`.
  Its UniXNU arguments retain the established 0112f GUI/iOS-rw baseline and
  add only `sweetusb=1`; they contain no `sweetusb=2`, `sweetvbus`,
  `sweetbattery`, or `sweetbatt`. It is strictly for `fastboot boot`, not
  flashing. The reproducible local packager is
  `/home/shenli/work1/out-bs/mk16k-0112m-usbprobe.sh`.
- **0112m m0 hardware result / m1 follow-up (2026-11-03):**
  `logs/sweetlog-0112m0.bin` proves the intended read-only USB probe reached
  the live controller: `usb30_prim` GDSC is on, all sampled clocks are on, and
  DWC3 returns `GSNPSID=0x33313130` with valid device/PHY/QSCRATCH registers.
  The boot reached SpringBoard and launchd later recorded a normal shutdown
  request. Its lost hardware-key shortcut has a packaging-only cause:
  `sweetkeys=0` was inadvertently retained, and the log explicitly says
  `SWEET KEYS: disabled (sweetkeys=0)`. It is not a USB or key-driver
  regression; 0104 supplies MH2LM's read-only SPMI key mapping and defaults
  keys to enabled. The next disposable image is
  `/home/shenli/work1/out-bs/boot-16k-0112m-usbprobe-m1.img` (SHA256
  `c758ab790783839026f12c28700937217a2f4ddcef69e43e150f3e1d8a688f44`),
  made by `out-bs/mk16k-0112m-usbprobe-m1.sh`. It retains only
  `sweetusb=1`, removes `sweetkeys=0`, and has no gadget/VBUS/battery boot
  arguments. Verify that the log shows key initialization plus power and
  volume-down transitions; keep gadget mode disabled for that boot.
- **0112m m1 hardware result / m2 step-1 follow-up (2026-11-03):**
  `logs/sweetlog-0112m1.bin` confirms that m1 restored the read-only key
  driver: it identifies the live PON and GPIO6 APIDs, verifies the GPIO is an
  enabled input, and records both Power (`key=0`, HID usage `0x30`) and
  Volume Down (`key=2`, HID usage `0xea`) press/release pairs. SpringBoard
  again initiated a normal shutdown. The USB state probe is unchanged and
  healthy. The next disposable image is
  `/home/shenli/work1/out-bs/boot-16k-0112m-usbprobe-m2-step1.img` (SHA256
  `003eaae281a634f52282ccc32e93128797da4537454b9a885056c92053ccc02c`),
  made by `out-bs/mk16k-0112m-usbprobe-m2-step1.sh`. It adds only
  `sweetusb=2 sweetusbstep=1`: after the default 15-second deferred worker
  delay it tests DMA allocation, DCTL soft reset, event-buffer programming,
  and EP0 configuration, then stops before the `DALEPENA` write. It has no
  `sweetvbus`, `sweetusbdma`, battery, or key override; no enumeration is
  expected in this stage.
- **0112m m2 hardware result / m3 step-4 follow-up (2026-11-03):**
  `logs/sweetlog-0112m2.bin` completed the intended pre-enable gadget path
  without destabilizing iOS or the keys. The worker allocated DMA at
  `0xc43ac000` (below 4 GiB), DCTL soft reset completed in 1 ms, and
  `GEVNTADRHI` retained all bits of a `0xffffffff` write, proving the
  controller accepts 64-bit event-buffer addresses. The programmed event
  address and size read back as `0x00000000c43ac000` and `0x80001000`, EP0
  setup completed, and the driver stopped exactly before step 4. The next
  disposable image is
  `/home/shenli/work1/out-bs/boot-16k-0112m-usbprobe-m3-step4.img` (SHA256
  `f2acb8fc9d042c75d65b985abaf98c050c04b46c2c1317c969eb778f3a6bf1e3`),
  made by `out-bs/mk16k-0112m-usbprobe-m3-step4.sh`. It changes only
  `sweetusbstep=4`, so it writes `DALEPENA=0x3` for EP0 and then stops before
  QSCRATCH/VBUS or RUN_STOP. Enumeration is still not expected.
- **0112m m3 hardware result / m4 step-5-read follow-up (2026-11-03):**
  `logs/sweetlog-0112m3.bin` confirms the `DALEPENA=0x3` EP0 enable completed
  and the worker stopped exactly before step 5. The m3 gadget path therefore
  reached every planned pre-QSCRATCH state without affecting SpringBoard or
  the restored key driver. The next disposable image is
  `/home/shenli/work1/out-bs/boot-16k-0112m-usbprobe-m4-step5-read.img`
  (SHA256 `3d5019c18d49ec3c0c5491b91c3e1aceabad2e1d4173797c3bd8e0094a599bd1`),
  made by `out-bs/mk16k-0112m-usbprobe-m4-step5-read.sh`. It changes only
  `sweetusbstep=5`, causing the driver to read and print QSCRATCH state with
  the MH2LM default VBUS mode 0. It has no `sweetvbus` argument, so neither
  QSCRATCH nor PHY VBUS override is written; it stops before RUN_STOP and no
  enumeration is expected.
- **0112m m4 hardware result / m5 step-6 follow-up (2026-11-03):**
  `logs/sweetlog-0112m4.bin` reached and completed the QSCRATCH inspection at
  VBUS mode 0: `HS_PHY_CTRL=0x10000000`, `SS_PHY_CTRL=0x00000000`, and
  `GENERAL_CFG=0x00000004`. It then stopped before step 6; there were no
  override-write lines, while keys and normal SpringBoard shutdown remained
  healthy. The next disposable image is
  `/home/shenli/work1/out-bs/boot-16k-0112m-usbprobe-m5-step6-run.img`
  (SHA256 `b4dcf0135f6e39dd6da8fc69b04e1a75c285911038cd049288e008c9ef559598`),
  made by `out-bs/mk16k-0112m-usbprobe-m5-step6-run.sh`. It changes only
  `sweetusbstep=6`: DWC3 is switched to device mode/run, EP0 is armed after
  the controller runs, and the polled event/log threads start. No `sweetvbus`
  argument is passed, so VBUS mode remains 0; enumeration is not required for
  this test.
- **0112m m5 hardware result / m6 HS-VBUS pre-run follow-up (2026-11-03):**
  `logs/sweetlog-0112m5.bin` confirms DWC3 reaches device running state with
  VBUS mode 0: `PRTCAPDIR` was already 2, `RUN_STOP` cleared the halt state
  immediately (`DSTS=0x00920001`), and EP0 was armed only after that point.
  The polled thread reported no events, configuration, DTR, or console bytes,
  as expected without a valid session; iOS, keys, and normal shutdown remained
  healthy. The next disposable image is
  `/home/shenli/work1/out-bs/boot-16k-0112m-usbprobe-m6-vbus1-pre-run.img`
  (SHA256 `51a6a2786fc2a2679053bcb513a48254f2cdff0a6a4213acd4782ad4dcfd1c72`),
  made by `out-bs/mk16k-0112m-usbprobe-m6-vbus1-pre-run.sh`. It sets only
  `sweetvbus=1` with `sweetusbstep=5`: the HS QSCRATCH VBUS-valid bits are
  written and read back, but the driver stops before RUN_STOP. It does not
  change SS PHY state and no enumeration is expected.
- **0112m m6 hardware result / m7 HS-VBUS run follow-up (2026-11-03):**
  `logs/sweetlog-0112m6.bin` proves the first QSCRATCH HS override is accepted:
  with VBUS mode 1, the driver wrote the HS VBUS-valid bits and read back
  `HS_PHY_CTRL=0x10100000`, then stopped before step 6. SpringBoard, key
  press/release events, and normal shutdown all remained healthy. The next
  disposable image is
  `/home/shenli/work1/out-bs/boot-16k-0112m-usbprobe-m7-vbus1-run.img`
  (SHA256 `ee827dd2ff0c7bcbbbdcde3c995d779d1ffb8819836b7ff1144596b1093e03af`),
  made by `out-bs/mk16k-0112m-usbprobe-m7-vbus1-run.sh`. It retains the same
  HS-only `sweetvbus=1` and changes `sweetusbstep=6`, allowing DWC3 to run and
  arm EP0 after a valid session. SuperSpeed PHY remains untouched. Enumeration
  or device events may begin in this stage; the persistent ring remains the
  fallback diagnostic path.
- **0112m m7 hardware result: CDC-ACM works end-to-end (2026-11-03):**
  `logs/sweetlog-0112m7.bin` is the first complete MH2LM USB-device success.
  With `sweetvbus=1`, DWC3 ran, reported bus reset and connect-done, served
  device/configuration/string descriptors, accepted `SET_ADDRESS(2)` and
  `SET_CONFIGURATION(1)`, and printed `ACM endpoints configured`. The host
  then sent the normal ACM line-coding/control requests; the recorded
  `configured=1`, `events=125`, and `console=8192 bytes` prove the bulk-IN
  console path is active. `dtr=0` is not a device fault: the host closed the
  port after enumeration. Reuse the m7 image for live logging; start
  `tools/sweet-usbacm-log.sh logs/live-0112m7.txt` first, then `fastboot boot`
  m7 in another terminal. The tool is intentionally long-lived across the
  USB disconnect/reconnect caused by `fastboot boot`: it waits before the node
  exists, reopens it after delayed enumeration, and resumes waiting after a
  later disconnect. Once present, it opens the TTY read/write, explicitly
  asserts DTR/RTS with `TIOCMBIS`, sets raw mode, prints live console bytes,
  and appends them to the selected file. Set
  `SWEET_ACM_DEVICE=/dev/ttyACM<N>` if
  another ACM device is already attached. No further kernel or SS-PHY change
  is needed to validate live logging; persistent-ring extraction remains the
  recovery path for failures before enumeration.

- **0112m m8 result / CDC bulk-IN TX fix staged (2026-11-03):**
  `logs/sweetlog-0112m8.bin` rules out the host listener and enumeration as
  the reason that `logs/live-0112m7.txt` remained empty. The device recorded
  `ACM endpoints configured`, then `port opened by the host`; its periodic
  state reached `configured=1, dtr=1`. The reader therefore opened the
  correct `/dev/ttyACM*` node and asserted DTR successfully. The failure is
  wholly on the gadget TX path: `console` stops at exactly 8192 bytes and no
  EP3 (`0x81`, bulk-IN) completion advances `log_pos` or clears `tx_busy`.
  `0113-mh2lm-usb-cdc-acm-tx.patch` is the narrow follow-up: the observed
  high-speed bulk endpoints and descriptors use a consistent 512-byte MPS;
  the console worker avoids an exact multiple of that MPS; a bus reset clears
  stale configured/DTR/TX state; serial-state notifications are sent only on
  a state change rather than requeued forever; and the first three TX submit
  and completion events plus periodic `complete/submitted/error` counts are
  logged. It applies cleanly on the exact 0112m build tree and
  `bsd/dev/sweet_usb.c` compiles with the saved CI arm64 command
  (`0 of 1 files failed`). The next hardware log must show at least
  `console TX submit 8191 bytes` followed by `console TX complete 1`; only
  after that should the host terminal be expected to receive console text.
  No CircleCI workflow change is involved.

- **0112m m9 hardware result: isolated battery service passes (2026-11-03):**
  `logs/sweetlog-0112m9.bin` came from the disposable
  `out-bs/boot-16k-0112m-battery-read.img` image (SHA256
  `51e9ec489fb53b1a8aa9672b09bf190c0d18cd4b04a2588608088af3367acf4d`).
  Its boot arguments contain only the established GUI/iOS-rw baseline plus
  `sweetbattery=1 sweetbatt=1`: no USB gadget/VBUS arguments and no key
  override. PM8150B USID 2 responded consistently: `SOC=0xB8` with
  `SOC_READY`, VBATT `0x811e/0x811c` = `4034mV`, and every poll published
  `78%`. USB VBUS is present (`usb=0x10`), while charger status `0x47` has a
  low state value outside the driver's charging/full ranges, so the published
  state `external=1, charging=0, full=0` is expected rather than a failure.
  SpringBoard, backboardd, touch, power and volume-key events continued after
  the battery polls; there is no battery-path timeout or kernel panic. Battery
  reads are therefore hardware-validated and are not the cause of the earlier
  desktop-entry or USB-live-log issue. Keep this read-only path unchanged
  while the CDC-ACM 0113 build is tested.

- **0112m m10/m11 hardware result: USB live logging and battery coexist (2026-11-03):**
  `logs/sweetlog-0112m10.bin` validates the 0113 CDC bulk-IN TX fix on
  hardware: after the host opens the ACM port, the device records TX
  completions and reaches `console=216199 bytes`, `TX 157/158`, `err 0`, with
  `configured=1, dtr=1`. This replaces the previous exact-8192-byte stall;
  it demonstrates the fix bundle as a whole, not a claim that one individual
  change alone was causal. The combined disposable image
  `out-bs/boot-16k-0112m8-usb-battery-m11.img` (SHA256
  `68f78ce1644da831aa67293f423b4d4cd2f09e4f751d7af8bf816d2099ffd023`)
  then passed with only `sweetusb=2 sweetusbstep=6 sweetvbus=1
  sweetbattery=1 sweetbatt=1`; it deliberately excludes `sweetkeys=` and
  `sweetusbdma`. In `logs/sweetlog-0112m11.bin`, PM8150B SID2 stayed
  `SOC_READY=1` at `78%`, `4034-4035mV`, `external=1`, while USB reached
  `console=222691 bytes`, `TX 237/238`, `err 0`, with host DTR asserted.
  SpringBoard, touch and key events continued after both services started;
  there is no `panic(cpu)`, kernel-trap, or debugger signature, and the live
  log ends in normal userspace teardown followed by `PSCI SYSTEM_OFF`. USB
  live logging and the read-only battery service are now integration-validated;
  do not add driver changes merely for cleanup before a new functional target
  requires them.

- **0112m m12 battery-UI compatibility diagnosis (2026-11-03):**
  the diagnostic image retained the m11 USB/battery baseline and added only
  `sweetiolog=powerd,PowerUIAgent`. In both
  `logs/live-0112m12.txt` and `logs/sweetlog-0112m12.bin`, `powerd` and
  `PowerUIAgent` match the running `SweetBattery` service through
  `IOPMPowerSource`; the PM8150B data path and published source properties are
  therefore not the reason no battery state is visible in the UI. `powerd`
  nevertheless repeatedly performs an exact `AppleSmartBatteryManager` match
  and finds none. Patch `0118-mh2lm-battery-ui-class.patch` adds that runtime
  class as a behaviour-free subclass of `SweetBattery` and changes only the
  built-in personality's class/category strings. It does not add a second
  controller, alter PMIC reads, or add any power/charging write path. The next
  CI kernel must validate that exact match and the visible battery UI before
  considering a separate `AppleARMPMUPowerSource` compatibility layer.

- **0112m m13 battery-UI open diagnosis (2026-11-03):** the 0118 class alias
  succeeded: `powerd` matched `AppleSmartBatteryManager` and `PowerUIAgent`
  matched it through `IOPMPowerSource`. The same boot's PM8150B samples
  published real data (`78%`, `4032-4034mV`, external power present, neither
  charging nor full), but every `powerd` `IOServiceOpen(type 0)` returned
  `0xe00002c7` (`kIOReturnUnsupported`). This is the default `IOService`
  result when no `IOUserClient` is available, so userland never consumed the
  otherwise correct source properties and the on-screen full/charging glyph
  remained static. Patch `0119-mh2lm-battery-userclient-trace.patch` installs
  the smallest read-only compatibility surface: a user client on
  `AppleSmartBatteryManager` that accepts the open and logs the first call of
  each external-method selector, while unknown selectors still return
  `kIOReturnUnsupported` and no PMIC write path is added. For that ABI probe,
  boot without `sweetusb`, `sweetusbstep`, or `sweetvbus`: `sweetvbus=1`
  forces the USB VBUS override and cannot validate a physical unplug. After
  the desktop appears, unplug, wait at least 35-45 seconds for the battery
  poll, then retrieve the persistent log after shutdown/recovery. The first
  0119 CI attempt (0112k) reached the final link but failed because this XNU
  configuration does not link the base
  `IOUserClient::exportObjectToClient(task_t, OSObject *, io_object_t *)`
  implementation. The user client now explicitly overrides that vtable slot,
  releases the consumed object, and returns `kIOReturnUnsupported`; a replay
  of the 0112k `SweetBattery.cpp` compile command succeeds, and its object no
  longer imports that missing base symbol.

- **0112m m14 UI success, charging still unproven (2026-11-03):**
  `logs/sweetlog-0112m14.bin` confirms the 0119 user client works: `powerd`
  opens `AppleSmartBatteryManager` with `0x0`, and physical cable changes are
  visible in PM8150B's USBIN status (`usb=0x10` / external=1 when connected,
  `usb=0x26` / external=0 when disconnected). The UI is therefore consuming
  live source state rather than a fixed full/charging icon. It does **not**
  prove the battery is charging: every connected sample reports
  `chgr=0x47`, whose low three bits are SMB5 `DISABLE_CHARGE` (7), and
  `charging=0`; the FG SOC byte is invalid (`0xb9`) so displayed percentage is
  voltage-curve fallback. Voltage movement around 4.01-4.03 V is insufficient
  to infer current direction. Patch `0120-mh2lm-battery-charge-diagnostics.patch`
  adds an opt-in `sweetbattdiag=1` probe only. It logs SMB5 status/enable/pause/
  inhibit gates and a matching PM8150B IBATT/current-copy pair (488.281 uA per
  signed raw step; negative is charging in the Qualcomm FG convention), while
  leaving all published IOPM properties unchanged and performing no PMIC write.

- **0112k2 m15 charging diagnosis and UI latency root cause (2026-11-03):**
  `logs/sweetlog-0112m15.bin` contains the 0120 read-only diagnostics with
  `config-ok=1` and `current-ok=1`. PM8150B sees physical USB insertion and
  removal (`usb=0x10`/`st5=0x80` versus `usb=0x26`/`st5=0x40`), but every
  sample remains charger state 7 (`DISABLE_CHARGE`) with `en=0` and `smben=0`.
  It never reports a negative IBATT: connected samples are zero or positive
  (including `+297851 uA`), while unplugged samples are consistently about
  `+456000 uA`. Under the Qualcomm FG convention used by the diagnostic,
  negative current is charging, so this proves that the existing UI is showing
  VBUS presence only; it is not physically charging the pack. The same build
  tree shows `SweetBattery` performs `updateStatus()` after each poll, but
  arms its sole observer timer at 30,000 ms. The observed several-second to
  tens-of-seconds icon delay is therefore a polling-latency issue, not an iOS
  UI cache failure. Patch `0121-mh2lm-battery-poll-cadence.patch` reduces only
  that read-only polling interval to two seconds. It deliberately does not
  enable charging or change PMIC state; a later, separate write experiment
  must be derived from the MH2LM UEFI charger configuration rather than the
  status-bar workaround.

- **PMIC event-driven battery preflight staged, not yet hardware-tested
  (2026-11-03):** `0122-mh2lm-battery-irq-preflight.patch` adds an opt-in
  `sweetbattirqdiag=1` probe for the eventual event-driven path. It does not
  register SM8150 GIC SPI 481 and it performs no PMIC write, enable or clear.
  On the next combined charging test it will only log the PM8150B USBIN
  (`PPID 0x213`) and CHGR (`0x210`) APID mappings, write owner, IRQ-owner bit,
  EE0 owner bitmap bit, and v5 channel enable/status words. This establishes
  whether XNU can safely receive `USBIN_PLUGIN` and `CHG_STATE_CHANGE` before
  adding a handler that schedules a normal `SweetBattery::poll()` on the
  workloop. The 2-second 0121 timer is a temporary local fallback, not the
  intended final event mechanism.

- **0112m m16 capacity-jump root cause and 0123 follow-up (2026-11-03):**
  `logs/sweetlog-0112m16.bin` proves the visible jumps are not real stored
  energy changes. The PM8150B returns raw monotonic-SOC bytes such as
  `0xa8..0xac`; 0120 rejects every value above 100 as invalid, then 0121
  publishes the voltage-curve fallback every two seconds. A cable changes the
  terminal voltage from about 3858 mV (curve 51%) to 3935 mV (curve 65%), so
  the UI jumps while the pack has not gained 14%. Linux
  `fg_get_msoc_raw()/fg_get_msoc()` instead accepts matching 0x4009/0x400a
  bytes, maps 0->0 and 0xff->100, and nearest-rounds 1..254 to 1..99
  (therefore `0xa8 -> 66%`). Patch
  `0123-mh2lm-battery-msoc-irq-lines.patch` mirrors that bounded,
  read-only pair check and scaling; voltage remains only a failed-gauge
  fallback. With `sweetbattirqdiag=1`, it also logs read-only USBIN_PLUGIN
  bit 4 and CHG_STATE_CHANGE bit 1 configuration as
  `SWEET BATT IRQ CFG` (rt/edge/high/low/enabled/latched/read-ok). It does
  **not** register SPI 481, clear a latch, enable a PMIC line, or write a
  charging register. The next boot must use
  `sweetbattery=1 sweetbatt=1 sweetbattdiag=1 sweetbattirqdiag=1`, without
  `sweetusb`, `sweetvbus`, or `sweetusbstep`. Success is repeated
  `src=gauge` samples such as `msoc=0xa8 scaled=66%` that stay stable
  across physical insertion/removal; the curve field may change but must not
  become the published value while the MSOC pair is valid. This still does
  not prove charging: connected m15/m16 samples remain state 7
  (`DISABLE_CHARGE`), `en=0`, `smben=0`, with no stable negative IBATT.
  Continue treating `SWEET BATT DIAG` state/en/smben/ibatt/signed/ua fields,
  not the UI icon or VBUS detection, as charging evidence.

- **0112k4 build and validation image prepared (2026-11-03):** CircleCI
  applied 0123 and linked `kernel.development.mh2lm` successfully. The
  downloaded artifact's `SHA256SUMS` and extracted kernel agree on
  `5d47d8ab485ec2e55f8e1cd8679ecbd57e9c393765789e87bccf82ddd98690e8`;
  the downloaded build tree contains `readBytePair`, `scaleMsoc`, and
  `SWEET BATT IRQ CFG` in final `SweetBattery.cpp`. The reproducible
  packer is `out-bs/mk16k-0112k4-battery-msoc-irq.sh`; it produced
  `out-bs/boot-16k-0112k4-battery-msoc-irq.img`
  (SHA256
  `b80b3157fb7838152c15189be8c481e339359ef91e589826e1d3dad15ba9cb04`).
  Its ramdisk was unpacked after packaging and byte-matches the source
  ramdisk; the embedded kernel and ADT were separately read back and match.
  The entry has only the established GUI/iOS-rw baseline plus
  `sweetbattery=1 sweetbatt=1 sweetbattdiag=1 sweetbattirqdiag=1`; it
  rejects `sweetusb`, `sweetvbus`, `sweetusbstep`, and
  `sweetkeys` arguments. Boot it with `fastboot boot` only, not a
  partition flash. On the phone, leave it disconnected for a short baseline,
  insert the cable, wait 10 seconds, remove it, wait 10 seconds, then export
  the persistent log after shutdown/recovery. Capture both every
  `SWEET BATTERY SID2` line and the two boot-time `SWEET BATT IRQ CFG`
  lines.

- **0112m m17: MSOC fixed; PMIC event gates remain closed (2026-11-03):**
  `logs/sweetlog-0112m17.bin` validates 0123 on hardware. The new-format
  samples (`msoc=`, rather than legacy `soc=`) retain
  `src=gauge`: raw 0xa7..0xa5 all scale and publish 65%, despite the
  voltage curve moving roughly 43..65% across normal discharge and physical
  USB insertion. Connected samples raise VBATT to about 3939 mV and set
  `usb=0x10 external=1`; unplugged samples return to `usb=0x26
  external=0`. The published gauge value does not take the former
  voltage-curve 51->65% jump. A few legacy `soc=... src=curve` records in
  the circular persistent log predate 0123: the final 0123 build tree cannot
  emit that old format, so do not use those residue lines to judge m17.
  There is no panic/trap signature and SpringBoard, touch and keys remain
  live.

  The IRQ preflight gives a stop condition for direct event work:
  USBIN_PLUGIN bit 4 is edge/high/low configured and CHG_STATE_CHANGE bit 1
  is edge/high configured, but both PMIC peripheral `enabled=0` and their
  PMIC-arb routes have `acc-enable=0`; latches and arb status are zero.
  Mapping/ownership are otherwise valid (USBIN APID 269 and CHGR APID 266,
  both IRQ-owned by EE0). Therefore no PMIC event can reach GIC SPI 481 yet.
  Do not enable either gate until a real SPI consumer, PMIC-arb enable
  sequence, and peripheral-latch acknowledgment ordering have been designed
  together. The 2-second polling fallback remains required.

  Physical charging is still unproven and currently contradicted: all sampled
  states remain 7 (`DISABLE_CHARGE`) with `en=0` and `smben=0`.
  Connected `st5=0x80` distinguishes cable presence, but only two tiny
  negative IBATT readings (-976 and -6347 uA) occur amid otherwise disabled
  samples and are not meaningful charge current. The IRQ-CFG logger currently
  prints a literal `\\n` after each line because its new C string contains
  a doubled backslash; this is cosmetic and must be corrected in the next
  functional battery patch, not in a standalone rebuild.

- **0124 PM8150B policy diagnostic staged, read-only (2026-11-03):** the
  MH2LM Lineage SMB5 driver configures connector mode, Type-C/USB input
  policy, AICL and thermal controls before it releases its default
  `chg_disable_votable` vote. Its eventual write to `CHARGING_ENABLE_CMD`
  (`0x1042`, bit 0) is therefore not a standalone charger-init recipe. Patch
  `0124-mh2lm-battery-policy-diagnostics.patch` adds an opt-in
  `SWEET BATT POLICY` record whenever `sweetbattdiag=1` is active. It reads
  only the PM8150B power path, APSD, USB input-suspend/current-limit/AICL,
  Type-C sink/state/misc, and SMB_EN command/select registers. The decoded
  fields give `use-usb`, `valid-input`, `path-suspend`, `cmd-suspend`, ICL in
  mA, APSD done/enumeration, AICL enable/ADC/rerun/collapse policy, Type-C
  state/CC/VBUS attachment and SMB_EN selection/override state. It also fixes
  0123's literal `\\n` in `SWEET BATT IRQ CFG`. It is committed locally as
  `eab8cac` (`battery: diagnose PM8150B charging policy`); it is not pushed.

  The patch has no SPMI write, PMIC IRQ enable/clear, GIC handler, charger
  state change or published-IOPM change. Its local syntax check uses the
  saved 0112k4 CircleCI iokit command and generated MH2LM iokit build
  directory, with the checked source temporarily substituted and restored:
  `0 of 1 files failed`. The CI log uses `/Users/distiller/project/dxb` while
  the local helper expects the older runner path, and this host supplies
  `/usr/bin/clang++` rather than the helper's historical `~/swift` compiler;
  the validation used temporary copies to adapt those two environment paths,
  not a repository tool change.

  **Next hardware image:** boot with the established GUI/iOS-rw baseline plus
  `sweetbattery=1 sweetbatt=1 sweetbattdiag=1 sweetbattirqdiag=1`, and no
  `sweetusb`, `sweetvbus` or `sweetusbstep`. Capture two or more policy lines
  unplugged, then insert a known-good charger for at least 10 seconds and
  capture five or more connected lines, then unplug for another 10 seconds.
  The test asks only whether the input path and Type-C/AICL preconditions are
  already valid while charge stays disabled. It does not prove charging and
  does not authorize any PMIC write. A separate controller design is required
  even if every input precondition is healthy.

- **0112k5 build and policy-validation image prepared (2026-11-03):** CircleCI
  applied 0124 and completed with `XNU Build Done!`. The artifact manifest and
  extracted `kernel.development.mh2lm` agree on SHA256
  `8ea83fd6eb451a5dea7438da871a8e5931ef28649dadf9401b98a1723a370e81`.
  The final kernel contains both `SWEET BATT IRQ CFG` and the complete
  `SWEET BATT POLICY` format string, including the decoded AICL and Type-C
  fields. The reproducible packer is
  `out-bs/mk16k-0112k5-battery-policy.sh`; it produced
  `out-bs/boot-16k-0112k5-battery-policy.img` (SHA256
  `6bf81694cd6b02923531b33d037bc9c3650471d3d8fa27b6dc884463694d172b`).
  It is Android boot header v1 with a 2048-byte page size. Its embedded kernel
  blob and 32 MiB ramdisk were both extracted from the completed image and
  byte-match their verified packaging inputs.

  The image retains the established GUI/iOS-rw baseline plus only
  `sweetbattery=1 sweetbatt=1 sweetbattdiag=1 sweetbattirqdiag=1`; it rejects
  `sweetusb`, `sweetvbus`, `sweetusbstep`, `sweetkeys` and `sweettouch`
  overrides. Use `fastboot boot` only, never a partition flash. For the test,
  collect at least two unplugged `SWEET BATT POLICY` records, then connect a
  known-good charger for at least 10 seconds and collect at least five
  connected records, then disconnect it for another 10 seconds. Preserve the
  matching `SWEET BATT DIAG`, `SWEET BATTERY SID2` and boot-time IRQ-CFG lines.

- **0112m18: USB input policy is ready; charger remains deliberately disabled
  (2026-11-03):** `logs/sweetlog-0112m18.bin` validates the 0124 read-only
  policy probe. Unplugged records consistently show `path=0x2`, no selected
  or valid USB input, while the gauge stays stable at 62--63%. After cable
  insertion, `path=0x95` shows both USB selection and valid input; neither the
  power-path nor USB command suspension bit is set, ICL is 1500 mA, and AICL
  is enabled with periodic rerun and collapse suspend. APSD raw values are
  `0x1` and `0x21`: bit 0 is the Lineage SMB5 DTC-complete bit, and `0x21`
  additionally carries its bit-5 slow-plugin timeout indication. Therefore
  the old logger's `done=0` is a decoding bug, not failed charger detection.

  The charger itself is not proven enabled: connected `SID2` samples continue
  to publish `charging=0`, and the representative matching diagnostic state
  is 7 (`DISABLE_CHARGE`) with `en=0` and `smben=0`. The occasional tiny
  negative current samples are not evidence of actual charging. `cfg2=0x2c`
  has its charger-inhibit bit clear, so the observed blocker is the disabled
  charger policy/enable path, not missing VBUS, input suspension, zero ICL or
  disabled AICL. The Linux SMB5 driver only releases its default disable vote
  after wider controller initialization; do not write `CHARGING_ENABLE_CMD`
  as an isolated next step.

  The long 0124 policy line is often interleaved before SMB_EN's select/config
  fields, so it cannot yet distinguish a software-disabled command from an
  external SMB_EN selection. Patch
  `0125-mh2lm-battery-policy-log-format.patch` fixes the APSD DTC bit decode,
  exposes slow-plugin timeout separately, and splits the record into short
  INPUT, TYPEC and SMB lines. It remains read-only and makes no PMIC, IRQ,
  charging or IOPM state change. It is committed locally as `f84bba8`
  (`battery: correct PM8150B policy diagnostics`) and is not pushed. Next
  test: collect the three short records both unplugged and plugged, especially
  the complete `SWEET BATT SMB` record, before choosing a safe controller
  design.

- **0112k6 build and short-record validation image prepared (2026-11-03):**
  CircleCI applied 0125 and completed with `XNU Build Done!`. The extracted
  `kernel.development.mh2lm` SHA256 is
  `916a874c723847728aa9fb5d38461e944a8dc9f1199f9e5331930d143f4c45e5`,
  matching its artifact manifest. The final kernel contains `SWEET BATT
  INPUT`, `SWEET BATT TYPEC` and `SWEET BATT SMB`, with no legacy long-policy
  string required for this test.

  The reproducible packer is
  `out-bs/mk16k-0112k6-battery-policy-log.sh`; it produced
  `out-bs/boot-16k-0112k6-battery-policy-log.img` (SHA256
  `fb1b1e8f7bee624d10a30dbfb9190ba19c17b6167dd8a9e0469c339dc1051f66`).
  It is Android boot header v1 with a 2048-byte page size; its embedded loader
  and 32 MiB ramdisk both byte-match the verified packaging inputs. The boot
  arguments retain the established GUI/iOS-rw baseline plus only
  `sweetbattery=1 sweetbatt=1 sweetbattdiag=1 sweetbattirqdiag=1`; they reject
  USB/VBUS/step, key and touch overrides. Use `fastboot boot` only, never a
  partition flash. Collect the short INPUT, TYPEC and SMB records while
  unplugged, after 10+ seconds connected to a known-good charger, and after
  another 10+ seconds unplugged. Preserve the matching SID2 and DIAG records.

- **0112m19: physical PM8150B charging confirmed (2026-11-03):**
  `logs/sweetlog-0112m19.bin` validates real charging under the 0112k6
  read-only kernel. In 65 matching samples, SID2 reports `external=1` and
  `charging=1`; CHGR status is `0x3`, which is SMB5 `FULLON_CHARGE`, and the
  command register reads `en=0x1`. The fuel-gauge current is repeatedly about
  `-2.2` to `-2.3 A` (for example raw `0xed5f`, signed `-4769`,
  `-2328612 uA`), using the already-validated Qualcomm FG sign convention in
  which negative current charges the pack. This is corroborated by VBATT
  climbing through the contiguous sample set from about 4198 mV to 4304 mV
  and raw MSOC advancing from 63% to 66%. The UI is therefore not faking this
  charging state, and XNU made no PMIC write to obtain it.

  Active-charge samples also retain the expected ready input policy:
  `path=0x95`, USB selected and valid, neither suspend bit set, 1500 mA ICL,
  AICL enabled/rerun enabled, and APSD `0x1` with `dtc-done=1`. The persistent
  ring also contains cable-present noncharging samples (`state=7`,
  `charging=0`, near-zero current). Correction on 2026-11-06: the current
  m19 header is NOT wrapped; its ordered timeline and en1 before insertion
  are now documented in docs/BATTERY-CHARGING-HISTORY.md. Broad source and
  temperature stability was not established by that capture.

  The short records fixed the APSD decode and log interleaving, but every
  `SWEET BATT SMB` line has `read-ok=0`. Its zeroed override/select fields are
  therefore invalid, not evidence that SMB_EN is clear: the current boolean
  read chain stops at an earlier failing Type-C/MISC access. Any later
  diagnostic refinement must report individual read successes; this is not a
  blocker for the now-confirmed charge path. Do not add a charger-enable
  write. Next work is to establish repeatable charge/noncharge transition
  behavior and, separately, finish event-driven battery state notification.

- **0112m20: stable charge retained through display-off testing (2026-11-03):**
  **2026-11-06 identity warning:** this paragraph describes the historical
  SHA below; the current file differs and equals 0113-battery-ui. Keep this
  as an archived observation, not a freshly revalidated m20 result.
  `logs/sweetlog-0112m20.bin` (SHA256
  `ef270d05c9d4f770299d679bd8c4e332edcacf21dba271d015dd2b4e3342414a`)
  strengthens the m19 result. It contains 83 matched active-charge records:
  `external=1`, `charging=1`, CHGR state 3 (`FULLON_CHARGE`) and a negative
  matched FG current in every record. The retained sequence includes 64
  consecutive state-3 samples, one isolated non-atomic-looking read where
  USB/CHGR report zero while the matching FG current is still -2.31 A, then
  19 more state-3 samples before the later noncharging records. The following
  sample immediately returns to the normal active-charge state, so this lone
  mixed register snapshot is not evidence of a real charge stop. With the
  current two-second observer cadence, the first run alone covers roughly 128
  seconds of continuous retained evidence; circular-log wrap means it cannot
  measure the user's full wall-clock charge duration.

  Gauge-derived capacity moves monotonically from 66% (`msoc=0xa9`) to 69%
  (`0xb1`) while VBATT rises from 4107 mV to 4347 mV. The first active sample
  is a normal current ramp at about -1.19 A; the remaining steady samples are
  generally near -2.2 to -2.3 A (83-sample mean about -2.23 A). After the
  later cable-removal region, matched records return to `external=0`, state 7
  and positive discharge current. There is no retained panic, kernel-trap or
  TouchComm I2C-error signature.

  The log also records 51 active-charge samples while `SWEET FB: display off`
  is in effect, about 102 seconds at the observer cadence. This validates that
  the current framebuffer-black display-off implementation does not stop PMIC
  charging. It is **not** evidence of deep suspend or PMIC wake capability:
  the present display-off path leaves the system running, and USB/charging
  PMIC IRQ gates remain disabled. The missing plug-in charging animation is
  therefore a separate event/UI-compatibility problem, not a charge-current
  failure.

- **0126 staged: lock-screen power transition trace (2026-11-03):**
  `0126-mh2lm-battery-ui-transition-trace.patch` is an opt-in, read-only
  diagnostic that has passed `git apply --check` against the exact post-0125
  source tree and a recovered CI-argument local syntax check for
  `SweetBattery.cpp`. With the existing battery baseline plus
  `sweetbattuidiag=1`, it emits a compact `SWEET BATT UI` record only when the
  externally visible published state changes (external power, charge-capable,
  charging, full), immediately before and after the existing `updateStatus()`
  notification. It also records every `AppleSmartBatteryManager` user-client
  external method with selector and scalar argument. Without that boot
  argument, the prior once-per-selector user-client logging remains unchanged.
  The patch makes no PMIC write, does not enable or clear an IRQ, does not
  alter user-client return values, and does not request display wake.

  After the CI kernel is available, make a temporary boot image from the
  established GUI/iOS-rw battery baseline with only
  `sweetbattery=1 sweetbatt=1 sweetbattdiag=1 sweetbattirqdiag=1
  sweetbattuidiag=1` as the battery additions. Exclude USB/VBUS/step, key and
  touch overrides. Test through `fastboot boot` only: wait for the desktop;
  press Power to turn the framebuffer off; wait three seconds; connect a
  known-good charger; wait 8--10 seconds; remove it; wait another 8--10
  seconds; then export the persistent log. Correlate the new `SWEET BATT UI`
  and `SWEET BATT UC` records with existing `SWEET FB: display off/on` lines.
  This controlled trace decides whether the next change belongs in event
  delivery, the private user-client ABI, or power-adapter detail publication;
  do not force a charging animation as a substitute for that result.

  **One-build local test matrix:** after this patch reaches a successful CI
  artifact, do not request separate cloud builds for each combination. Run
  `tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-LABEL.gz
  LABEL` once. It verifies the artifact's own kernel SHA256 manifest plus the
  `SWEET BATT UI` and CDC-ACM strings, then emits five reproducible temporary
  Android boot-header-v1 images from the same kernel: `baseline` (explicit
  `sweetusb=0`), `battery-ui` (the read-only lock-screen trace),
  `battery-ui-preserve-off` (the same trace plus the 0127 framebuffer
  preservation experiment), `usb` (established ACM
  `sweetusb=2 sweetusbstep=6 sweetvbus=1`), and `usb-battery-ui` (both paths
  together). All retain the normal enabled key driver and are for `fastboot
  boot` only. This matrix isolates a failure first, then provides the
  framebuffer A/B and combined USB/battery regression results without a second
  CI compile. A later real key/USB/battery IRQ implementation must use
  separate opt-in boot arguments and extend this generator rather than
  replacing these known-good fallback profiles.

- **0113 CI artifact and one-build matrix prepared (2026-11-03):** CircleCI
  applied `0126-mh2lm-battery-ui-transition-trace.patch` and completed with
  `XNU Build Done!`. The extracted `kernel.development.mh2lm` SHA256 is
  `eb96b9c3a0bb90dfd27e77ea4dba452c3b5f754e5898e2bb187f6f2ae92f06d8`,
  matching `logs/xnu-kernel-0113.gz`'s manifest. The kernel contains both the
  0126 `SWEET BATT UI` strings and `SWEET USB: ACM endpoints configured`.
  `tools/mk-mh2lm-event-matrix.sh logs/xnu-kernel-0113.gz 0113` generated and
  read back all four temporary Android boot-header-v1, 2048-byte-page images:
  `out-bs/boot-16k-0113-baseline.img` SHA256
  `aef23ad75a847aa7b35ceff059a21006c0ab0968c152b4d4886eab68c1482376`,
  `out-bs/boot-16k-0113-battery-ui.img` SHA256
  `86e0afda7b3396e537313d71591f704855b940559bbadabcf814b7f6dc60e6c7`,
  `out-bs/boot-16k-0113-usb.img` SHA256
  `9cc0d60a82cb60dec9a31e819a9ae0bc823f3882487df584e9a7d38b061d927f`,
  and `out-bs/boot-16k-0113-usb-battery-ui.img` SHA256
  `9315e91c21e110681b58867260a5182c80fb448da3e35e4e8b07948c79d8fa84`.
  All four embed the same verified 0113 kernel. The first hardware test is
  `battery-ui`: after desktop, Power to framebuffer-off, wait three seconds,
  connect a known-good charger for 8--10 seconds, remove it for 8--10 seconds,
  then pull the persistent log. Leave the USB and combined images for the
  corresponding single-path and two-path regression tests; use `fastboot boot`
  only, never flash.

- **0113 battery-UI hardware result: plug/unplug wakes the display, but the
  lock screen is only partially composed (2026-11-03):**
  `logs/sweetlog-0113-battery-ui.bin` SHA256
  `6fffa51f4d791c9eb233fa82ad96a33024d4ace23ca641cb99a1ec0c47ea0c47`
  validates the end-to-end generic power-source path. While display-off was
  active, a cable insertion produced `SWEET BATT UI event=2` with
  `external=1 capable=1`, followed immediately by `updateStatus`, a `powerd`
  open plus selector-4 call, SpringBoard surfaces for the charging display,
  `SWEET FB: display on`, and a CoreBrightness fade to 1088. A later cable
  removal produced event 5 (`external=0 capable=0`) and another display-on
  sequence. Thus the large charging glyph and wake seen on hardware are real
  power-event results, not an explicit driver-side display request. The short
  plug/unplug checks did not yet enter active charge (`charging=0` throughout),
  so they are evidence for state notification and wake only, not a new
  charging-current conclusion.

  The resulting screen is not a complete normal lock screen: status-bar and
  home-indicator content appears but the centre/clock region remains black.
  This matches the current `SweetFramebuffer` design, not a battery-state
  failure. `RequestPowerChange(0)` fades every panel pixel to black; on wake,
  the backend replays only the latest swap's enabled layers directly into the
  panel. It neither retains all current layer state nor clears/recomposes a
  complete frame. A lock-screen partial update can therefore repaint its small
  status/home layers while the clock's layer is absent from that same swap and
  stays black. Do not try to fix this by faking more battery notifications.
  `powerd` selector 4 with argument 2 remains unsupported and needs later ABI
  work, but it is not preventing the observed wake.

- **0113 baseline, USB and combined regression result (2026-11-03):** the
  remaining three images were booted with `fastboot boot` only. Their raw ring
  captures are `logs/sweetlog-0113-baseline.bin` SHA256
  `00dfda1ed35d36650aab51fbd1ee5b54b03817a74eb61ce099bbbfe950d26e69`,
  `logs/sweetlog-0113-usb.bin` SHA256
  `e4ac5c5b38b837ca16421760b3b6c7ab084081847d63b91a0d0e7cf265b5d5c2`,
  and `logs/sweetlog-0113-usb-battery-ui.bin` SHA256
  `3a43214a0dcf445b72c43dba6966aef25d00770afee08d9d1d2bf170745d51b0`.
  The `panic` text in each capture is only the existing `panic_screen=1` boot
  argument, `DumpPanic` service name and early uninitialized-panic-buffer
  notices; none contains a kernel trap, data abort or panic. All three reach a
  normal SpringBoard-requested shutdown.

  The baseline preserves repeated `SWEET FB: display off/on` plus real
  CoreBrightness fade pairs, 183 touch reports/events with zero I2C errors,
  and Power/volume press/release delivery. The USB-only live capture
  `logs/live-0113-usb.txt` SHA256
  `12c8e85df5cba12e0773a94e19184c6ab4ef1154a0fa89fdf832041001bc4043`
  reaches 243,493 console bytes, TX `159/160`, zero errors, 449 DWC3 events,
  `configured=1` and `dtr=1`. This is a clean regression pass for the existing
  polling CDC-ACM path.

  The combined live capture `logs/live-0113-usb-battery-ui.txt` SHA256
  `289b05e7afd66dead1775c37ce73dbd1dc523d2f72deb545d6a2f99acff4294f`
  records multiple unplug/plug transitions: battery UI events 2--7 alternate
  `external/capable` between 0 and 1 and each is followed by `updateStatus`.
  Reconnecting the same USB cable naturally causes DWC3 bus reset, ACM
  configuration and host DTR close/open cycles; after those cycles console
  output continues to 291,302 bytes, TX `69/75`, zero errors and 928 events.
  The short combined sequence did not enter active charge (`charging=0`), so
  it validates UI/USB coexistence and recovery, not charging-current behavior.
  A transient key GPIO/PMIC read error appears near the later multi-key use in
  each profile, but the associated releases, touch traffic and orderly
  shutdown continue; retain it as a baseline observation for the future
  interrupt-driven key implementation rather than treating it as a 0113
  regression.

  These results close the 0113 functional matrix: they demonstrate that 0126
  battery tracing did not regress the existing polling display, key, touch or
  CDC-ACM paths. They do **not** validate USB, key or battery IRQ/event mode;
  all three remain polling fallbacks until a separate preflight, acknowledge
  ordering and workloop-handoff implementation is tested.

- **0127 staged: conservative framebuffer preservation experiment
  (2026-11-03):** `0127-mh2lm-framebuffer-preserve-off.patch` replaces the
  previously proposed immediate retained-layer compositor repair with a much
  narrower, reversible first test. Under opt-in `sweetfbpreserveoff=1`,
  `SweetFramebuffer` retains the physical scanout pixels while its existing
  `RequestPowerChange(0)` state is active; it does not change power requests,
  vsync delivery, pending-swap processing, IOSurface lifetimes, or PMIC and
  battery code. MH2LM's real `AppleARMBacklight` remains active: CoreBrightness
  sets DCS `0x51` to its lowest safe brightness level, so the physical panel
  can be black without erasing the last complete CPU-composited frame. Without
  the new boot argument, the existing seven-vsync fade/clear path is unchanged.
  The kernel logs `SWEET FB: preserving framebuffer contents while display is off` when
  the test mode is active.

  The patch is intentionally GPU-compatible: `swap()` remains the existing
  CoreAnimation protocol receiver and `present()` remains a CPU backend; no
  new software compositor is introduced. A future GPU backend can replace the
  presentation implementation while preserving the user-client and display
  power contracts. Do not port Hakios wholesale: its matching `0x46c`/four
  layer protocol and non-destructive `RequestPowerChange` behavior are useful
  references, but its `MiniIOSurface`, multi-swap queue and presenter thread
  are a separate display stack. UniXNU's shadow buffer is likewise a useful
  later pattern, not a drop-in solution: it targets `IOMobileFramebufferLegacy`
  and GOP/single-frame scanout rather than this four-layer CPU path.

  **Decision tree after the first 0127 hardware test:**
  1. If the charger plug/unplug wake restores a complete clock/lock screen,
     keep the non-destructive off behavior and repeat normal lock/unlock,
     timed auto-lock and long charge tests before making it the default.
  2. If only some frames remain incomplete, implement a bounded full-panel
     shadow buffer as a CPU-path fallback. At 1080 x 2400 x 4 bytes it costs
     10,368,000 bytes (about 9.89 MiB); it must have explicit validity and
     invalidation rules so it is removable once a GPU scanout backend exists.
  3. Do not retain/recompose four CoreAnimation layers in the kernel and do not
     import Hakios's full presenter before GPU design work establishes surface
     ownership, synchronization fences and scanout constraints.

  **First 0127 test:** add only `sweetfbpreserveoff=1` to the established
  0113 battery-UI image arguments after the next successful CI artifact is
  packaged. Boot with `fastboot boot` only. Confirm the boot log contains the
  preserve marker, then execute both directions: (a) Power/auto-lock, wait
  three seconds, insert charger, wait for the charging glyph/wake, then remove
  it; (b) connect charger, turn the screen off, then remove the charger. In
  each case record whether the central clock, status bar and home indicator are
  all present, whether a later normal Power wake remains correct, and export
  the persistent log. The expected trace still contains `SWEET FB: display
  off/on`; the absence of pixel clearing is controlled only by the boot
  argument.

### Session handoff (2026-10-21 22:20)
- **0087 verified on hardware.** Booted `img-gpu-0087.img` (`sweetgpu=3`) at
  18:05; iOS reached SpringBoard (SpringBoard/AKS/touch lines), then
  Power+Volume-Down -> OrangeFox -> `tools/sweet-log-pull.sh
  logs/gpu-0087-2026-10-21.txt` (ring flush #19). ARC dump:
  ```
  SWEET RPMH: arc gfx.lvl addr=0x30050 levels=16: 0 16 48 64 128 192 224 256 320 384 416 0 0 0 0 0
  SWEET RPMH: arc cx.lvl addr=0x30000 levels=16: 0 16 64 128 192 256 384 416 0 0 0 0 0 0 0 0
  SWEET RPMH: arc mx.lvl addr=0x30010 levels=16: 0 16 128 192 256 320 384 416 0 0 0 0 0 0 0 0
  ```
  (11/8/8 valid entries; the rest is zero padding). The 0086 GX sequence still
  failed and collapsed GX; the rest of the boot was unaffected.
- **Vote semantics established:** CPU-side RPMh votes for `.lvl` ARC
  resources use the **corner index** (`rpmhpd.c`: `cmd.data = corner`), not the
  GMU HFI packed `(level<<16)|(sindex<<8)|pindex`. `gfx.lvl` enable corner is
  1 (level 16); `mx.lvl` index 1 is also level 16. The packed form is only for
  GMU HFI votes.
- **0088 verified on hardware (2026-10-21 18:34).** Patch commit `27f2a05`,
  CI run `39583450528` (GUACAMOLE, DEVELOPMENT) green. Image
  `/tmp/opencode/img-gpu-0088.img` (SHA256 `b59d0441c9eb881e22897b4756363068c7f3c8c6b7ed2deab33068c42a060e3b`,
  `sweetgpu=4`); log `logs/gpu-0088-2026-10-21.txt` (ring flush #9):
  ```
  SWEET GPU: stage=4
  SWEET RPMH: gfx.lvl ARC corner 1 on ACTIVE vote complete
  SWEET GPU: gx before gdscr=0x222001 cfg=0x88400
  SWEET GPU: gx after gdscr=0xf8222000 cfg=0x70400
  SWEET GPU: rbbm status=0 int=0 status3=0
  SWEET GPU: cp always-on hi=0 lo=0 (no register response)
  SWEET GPU: gx collapsed gdscr=0x222001 cfg=0x88400
  SWEET RPMH: gfx.lvl ARC corner 0 off ACTIVE vote complete
  ```
  `gdscr` BIT(31)=1 and `cfg` BIT(16)=1: **the `gfx.lvl` vote is the missing
  GX GDSC input**. Corner 1 (level 16) was enough; corners 2/3 were not tried.
  GX is collapsed and the rail voted off afterwards; iOS still reaches
  SpringBoard and display/backboardd work. RBBM/CP read 0 because the GPU
  core/GMU clocks are still off, not because GX is unpowered.
- **0089 verified (2026-10-21 18:48).** Patch `3d492f1`, CI `39586065583`,
  image `img-gpu-0089.img` (`sweetgpu=5`); log `logs/gpu-0089-2026-10-21.txt`.
  With GX powered, the probe enables the GMU clocks and reads the block:
  ```
  gmu clocks before cx=0x80004220 gx=0x80000000 bcr=0 rcg=0x80000000
  gpu_cc_cx_gmu_clk 0x80004220 -> 0x4221
  gpu_cc_gx_gmu_clk 0x80000000 -> 0x1
  gmu dtcm-last=0xe7faf6c2 fw-init=0 boot=0 reset=0x1
  gmu intr=0 gmu2host=0 hfi=0 general7=0 fence=0
  gmu clocks after cx=0x80004220 gx=0x80000000
  ```
  Both GMU clocks can be enabled and the GMU register block is readable. The
  CM3 is halted with `SYSRESET=1`; no firmware is running. Clocks are
  restored and iOS still reaches SpringBoard.
- **0090 verified (19:02).** Patch `56630f4`, CI `39587451727`, image
  `img-gpu-0090.img` (`sweetgpu=6`); log `logs/gpu-0090-2026-10-21.txt`:
  ```
  smmu id0=0x4c017e06 id1=0x20000005 id2=0x5555 scr0=0x2d0406 acr=0x1c gfsr=0
  smmu pgshift=12 gr-page=8 cb=5 smr=6 clientpd=0
  smmu sid0..sid6 s2cr type=2 (fault); sid7 s2cr cb=5 (>= NUMCB)
  ```
  The GPU SMMU is **active** (`clientpd=0`) with 4K pages, 8 GR pages and 5
  context banks, but SIDs 4/5 (GMU) are not mapped. No GMU icache/dcache/HFI
  translation exists yet. SpringBoard still boots.
- **Device:** GM1910 `d3109478` is in OrangeFox recovery with root adb; only
  `fastboot boot` images were used, no partition was written.
- **GMU cold-boot chain 0091-0102 (all bounded; iOS reached SpringBoard after
  every boot):**
  - **0091:** `a640_gmu.bin` embedded as a payload RAMDisk and read from
    `/chosen/memory-map`; ITCM/DTCM blocks loaded through the GMU register
    windows; CM3 reset; `FW_INIT_RESULT=0`. Log `logs/gpu-0091-2026-10-21.txt`.
  - **0092:** minimal GPU-SMMU stage-1 (4K tables, 2MB blocks) mapping
    IOVA 0..2MB and 0x60000000..; the first 2MB allocation was not 2MB
    PA-aligned, fixed by allocating 4MB and using an aligned 2MB subrange.
    icache/dcache blocks copied; CBAR/SCTLR/TTBR/TCR/S2CR read back correctly;
    `FW_INIT_RESULT=0`. Log `logs/gpu-0092b-2026-10-21.txt`.
  - **0093:** `GPU_CC_GMU_CLK_SRC` was **root-off** (`0x80000000`). Enabled
    its GCC parent and set 200MHz (`cfg=0x602`, `cmd=0x80000000->0x2`);
    `FW_INIT_RESULT=0`. Log `logs/gpu-0093-2026-10-21.txt`.
  - **0094:** GMU cache/bus/power-collapse config and `PWR_COL_CP_MSG`;
    `FW_INIT_RESULT=0`. Log `logs/gpu-0094-2026-10-21.txt`.
  - **0095:** minimal 2-queue HFI table (`QTBL_ADDR=0x60001000`, `INFO=1`)
    and `GMU_RPMH_CTRL=0xf11`; `FW_INIT_RESULT=0`.
    Log `logs/gpu-0095-2026-10-21.txt`.
  - **0096 diagnostics:** `ITCM[0]=0x43f80` (firmware initial SP, so the load
    is correct), `SYSRESET 1->0`, `BOOT_CONFIG=2`, `GEN7=1`, `cx.lvl`
    corner 2 on. Still `FW_INIT_RESULT=0`. Log `logs/gpu-0096-...`.
  - **0097:** GMU BCR pulse (`0->0`, no effect) and both GPLL0 parent
    branches (`0x1605d7f->0x161dd7f`); still `FW_INIT_RESULT=0`.
    Log `logs/gpu-0097-2026-10-21.txt`.
  - **0098 (XO clock source):** RCG `cfg=0` (XO 19.2MHz), root enabled
    (`cmd=0x80000000->0x2`), poll 5s; still `FW_INIT_RESULT=0`, no
    interrupts. This **rules out the GPLL0/200MHz parent as the blocker**:
    the CM3 has a live XO clock and still does not execute. Log
    `logs/gpu-0098-2026-10-21.txt`.
  - **0099:** loaded the RSCC and both PDC sequencer uCodes with the a640
    constants (PDC address `0x30090`, TCS3 second data `3`, the five RSC and
    five PDC words), using RSCC `0x18220000`, PDC cfg `0xb290000` and PDC seq
    `0xb490000`. Still `init=0`; no ACK handshake at this point.
  - **0100:** vendor a640 `CM3_CFG=0x4052` (clear the NMI bit) and
    `AHB_FENCE_RANGE_0=0x80a008a0`; still `init=0`.
  - **0101:** added vendor `CX_GMU_WFI_CONFIG=0` before reset; superseded.
  - **0102 (gated, log `logs/gpu-0102-2026-10-21.txt`):** prints the block
    table, clears/reads SMMU faults and only resets with RSCC ACK. Result:
    ```
    fw head=0/0x30/0xefbeadde/0xefbeadde
    0x0+0x30 ITCM; 0x200+0x140 ITCM; 0x400+0x38bc ITCM
    0x4000+0x3a58 DDR; 0x40000+0x1b20 DTCM; 0x43fe0+0x14 DTCM
    0x57000+0x308 DDR
    smmu faults start gfsr=0 cb0=0x400
    rscc no ack=0
    GMU boot gated layout=0 rscc=0; not resetting CM3
    ```
    The CM3 was **not reset**; iOS still reached SpringBoard. The block table
    proves `ITCM[0]=0x43f80` is the first block's data0 (initial SP), not a
    block header.
  - **CB0 FSR `0x400`** is outside `ARM_SMMU_FSR_FAULT`
    (TF/AFF/PF/EF/UUT/TLBMCF/TLBLKF/ASF), so it is likely a reserved/RO bit,
    not a real fault. The gate must use the fault mask and read
    `CB_FAR`/`FSYNR0`/`FSYNR1` before deciding.
  - **RSCC no ACK is the real blocker:** the RSC/GPU DRV does not answer
    `RSCC_CONTROL_REQ=BIT(1)`. Need the correct GPU RSCC base/DRV and the
    RSC TCS0..3/DRV status; do not release CM3 until ACK=1 and
    `SEQ_BUSY_DRV0==0`.
  - **User constraints (2026-10-21):** A640 firmware is block-based, not the
    legacy whole-file ITCM load. ITCM 0..16 KiB, DTCM 0x40000..16 KiB,
    icache 0x4000 (256K-16K), dcache 0x44000 (256K-16K). Do not change GMU
    frequency (0098 ruled it out); do not touch SPTP; RSCC/PDC must use the
    a640 constants; gate on real SMMU faults and the RSCC ACK.
  - **Next:** mask CB0 FSR with `ARM_SMMU_FSR_FAULT`, log FAR/FSYNR; read
    RSC TCS0..3/DRV status and locate the GPU RSCC; only then re-enable the
    reset path. Patches `0099`-`0102` are committed; image
    `/tmp/opencode/img-gpu-0102.img`.

### 2026-10-21: UniXNU/UEFI boot option (assessment)
- The known guacamole UEFI is the Renegade/edk2-msm OnePlus 7 Pro (SM8150) WoA port.
  UniXNU's chain is: stock ABL -> BootShim -> UEFI firmware volume -> UniXNU
  boot manager (`\EFI\UniXNU\unixnu.toml`) -> DarwinBoot -> XNU.
- DarwinBoot is the standardized replacement for our shim: it reads
  `kernel.macho`, `kernel.macho.adt` (Apple ADT sidecar) and optional trust
  cache, builds `DARWIN_BOOT_ARGS`, exits boot services, disables the MMU and
  jumps to XNU in EL1. The UEFI must provide GOP, simple-text input, simple
  file system, block IO and (optionally) serial/ACPI.
- Port cost for guacamole: a device repo `board.toml` (firmware config/DSC/FDF,
  boot image parameters, payload/ADT tool, userdata windows), a prelinked
  `kernel.macho`, and an ADT sidecar with `/chosen/memory-map` placeholders.
  Our `tools/mkadt.py`/`shim/out/guacamole.adt` already generate the required
  nodes; the missing pieces are the placeholders and the DarwinBoot/prelink
  packaging. Our UFS/display/touch/keys drivers can stay.
- The A640 GMU/RSCC/SMMU blocker is orthogonal: UEFI changes the loader and
  platform init, not the GPU power sequence. NativeAdreno is a740; our a640
  CB0/RSCC work is still required.
- Decision: keep the shim as the working iOS/GPU path; evaluate the guacamole
  UEFI in parallel with `fastboot boot` only (no partition writes). Then use
  UniXNU's payload-only mode (DarwinBoot loads our kernel + ADT) before
  considering a full device-repo/persistent migration. Do not replace the
  working boot chain until the GMU blocker is fixed.

### 2026-10-21: TWRP/AVB constraints on guacamole, official SM8150 source cross-check
- **`fastboot boot` is the only safe recovery path; flashing TWRP to boot_a does not work.**
  Observed today on GM1910:
  - The 2020 TWRP 3.3.2B image (`recovery-TWRP-...img`, 96 MiB) is rejected by ABL with
    `Failed to load/authenticate boot image: Load Error`.
  - `TWRP+CN+EN.img` (guacamole, 96 MiB, AVB footer `AVBf` at byte 100663232) is accepted
    by `fastboot boot` and starts TWRP (adb states `sideload`, then `recovery` after the
    sideload session ends). TWRP's main-menu adbd is expected to be root; the `recovery`
    state was seen but shell was not exercised before it rebooted.
  - `fastboot flash boot_a TWRP+CN+EN.img` reports OKAY but the next boot stays in fastboot:
    the flashed recovery image is not booted from the boot partition (AVB/vbmeta mismatch).
    `backup/boot_b.img` was flashed back to boot_a and fastboot is healthy again. Do not flash
    recovery images to boot_a; keep using `fastboot boot`.
  - A rebuilt TWRP image (mkbootimg v2, same kernel/dtb/recovery_dtbo, ramdisk with
    `ro.adb.secure=0` + host `/adb_keys`, 89.8 MiB, no AVB footer) is rejected by
    `fastboot boot` with the same Load Error. The accepted 96 MiB image is the signed one;
    unsigned large images do not pass ABL authentication. The 29 MiB shim images are fine.
- **Offline source cross-check** (`OnePlusOSS/android_kernel_oneplus_sm8150`, branch
  `oneplus/SM8150_P_9.0`, sparse clone under `/tmp/oneplusoss-kernel`):
  - `sm8150-gpu.dtsi`: GPU `qcom,kgsl-3d0@2C00000` (`chipid 0x06040000`, IRQ 300),
    GMU `0x2c6a000 + 0x30000` with PDC at `0xb280000` / `0xb480000` (IRQ 304/305),
    dedicated SMMU `0x2ca0000` with SIDs 0 (gfx3d_user), 2 (gfx3d_secure),
    4 (gmu_user), 5 (gmu_kernel); zap is `a640_zap`, PAS id 13.
  - GDSC: CX `0x2c9106c` + HW ctrl `0x2c91540` BIT(31); GX `0x2c9100c` + domain
    `0x2c91508` + SW reset `0x2c91008`. GPUCC BCRs: GX `0x1008`, CX `0x1068`, GMU `0x111c`,
    RCG `0x1120`; `gpu_cc_ahb_clk` enable is `0x1078` BIT(0) (CLK_IS_CRITICAL).
  - GCC GPU branches: cfg AHB `0x71004` (critical), memnoc `0x7100c` (voted),
    snoc_dvm `0x71018`, **ddrss_gpu_axi `0x71154` (voted, used by both the GPU and the
    GPU SMMU)**. Patch 0084 currently enables the first three; add `0x71154` before the
    first GPUCC read in a follow-up patch.
- **TWRP log pull works and the iOS root is intact (2026-10-21 00:42).**
  `tools/sweet-log-pull.sh /tmp/sweetlog-before.txt` from the TWRP main-menu adb
  (`adb` state `recovery`, uid 0) read `/dev/block/sda19` (243,561,607,168 bytes).
  Ring header: version 1, 4 MiB, 320,834 bytes written, flush #218. The decoded
  log is the last iOS boot: 8/8 CPUs, `SWEET UFS: "userdata" ... 232278 MiB,
  read-write`, `AppleCLCD 1440x3120 ... presented as 1125x2436`, 1401 `exec`
  lines, SpringBoard/backboardd, personas 99/100/199/200/1000, display on/off,
  touch `id=ac3761 events=96`, keys 1/0. No `SWEET GPU` lines (the boot predates
  the 0084 probe).
  HFSX volume header at userdata+1024: signature `0x4858`, version 5, block 4096,
  4,194,304 blocks (16 GiB), free 996,504, files 186,411, folders 139,830.
- **GPUCC probe boot did not wedge the bus (2026-10-21 00:44-00:49).**
  `/tmp/opencode/img-t30.img` (`sweetgpu=1 sweetlogdump=1`) loaded via
  `fastboot boot` (OKAY). A 4-minute USB monitor saw no transition at all:
  no EDL `05c6:900e`, no Android `22d9:2769`, no fastboot `18d1:d00d`. So the
  GCC-gated GPUCC reads in patch 0084 are survivable; the phone is expected to
  be in the iOS userland. The `SWEET GPU:` register lines are in the persistent
  ring, but reading them needs another TWRP session (main-menu adb, uid 0).
  Next: boot TWRP, run `tools/sweet-log-pull.sh`, inspect `SWEET GPU`.
  `/tmp/opencode/img-t31.img` is the normal GUI control boot. Stock boot_a is
  restored; userdata untouched.
- **0084 GPUCC probe result (img-t30, TWRP pull 2026-10-21 00:50).**
  `logs/gpu-0084-2026-10-21.txt` (local, gitignored):
  ```
  SWEET GPU: gcc_gpu_cfg_ahb_clk 0x1 -> 0x1
  SWEET GPU: gcc_gpu_memnoc_gfx_clk 0x80000221 -> 0x80000221 (still halted)
  SWEET GPU: gcc_gpu_snoc_dvm_gfx_clk 0x80000001 -> 0x80000001 (still halted)
  SWEET GPU: gpucc gx_cfg=0x88400 cx_hw=0x5ff4
  SWEET GPU: gpucc bcr gx=0 cx=0 gmu=0 rcg=0x80000000
  SWEET GPU: gpucc gdscr gx=0x222001 cx=0x222001
  ```
  Decode with the official `drivers/clk/qcom/gdsc.c`: `PWR_ON_MASK=BIT(31)`,
  `SW_COLLAPSE_MASK=BIT(0)`, so `0x222001` = SW collapse (off) + retain-periph
  (BIT(13)) + the default wait fields (BIT(21), BIT(13)). Both CX and GX GDSCs
  are **off**; `cx_hw=0x5ff4` (BIT(31)=0) agrees. The two GCC branches are
  `BRANCH_VOTED`; their bit0 was already 1, so the writes change nothing and the
  CLK_OFF status is the missing interconnect/RPMh vote, not a branch disable.
  The GPUCC block is readable without wedging the bus. Next step is the RPMh
  CX/GX rail vote plus GDSC power-up (and `GCC_DDRSS_GPU_AXI_CLK` `0x71154`),
  not more clock pokes.
- **GPU stage 0.2 implemented: CX power-up + RBBM probe (patch 0085).**
  `xnu/patches/0085-sweet-guacamole-gpu-cx-power.patch` extends
  `bsd/dev/sweet_gpu.c` with `sweetgpu=2`; commit `211b1b6`, CI run
  `39463156162` (GUACAMOLE, DEVELOPMENT). Sequence:
  - enable `GCC_DDRSS_GPU_AXI_CLK` `0x71154` after the three 0084 branches;
    abort before touching GPUCC if `gcc_gpu_cfg_ahb_clk` still reads gated;
  - enable the CX GDSC like mainline/VENDOR `gdsc.c`: refuse if
    `HW_CONTROL` is set, clear `SW_COLLAPSE` in `0x2c9106c`, barrier + 1 us,
    poll `0x2c91540` BIT(31) for up to 2 ms;
  - only after CX reports on, enable `gpu_cc_ahb_clk` `0x1078` and
    `gpu_cc_cxo_clk` `0x109c`, then (only if AHB is on) map
    `0x2c00000+0x40000` and read `RBBM_STATUS` (dword `0x210`),
    `RBBM_INT_0_STATUS` (`0x201`), `RBBM_STATUS3` (`0x213`) and the CP
    always-on counter (`0x980/0x981`).
  - GX is deliberately untouched: mainline's GX GDSC `power_on` is a no-op and
    the GX head switch is GMU-owned; its `gfx.lvl` rail is an RPMh ARC vote.
  - Known gaps: vendor `gpu_cx_gdsc` has `mboxes = <&qmp_aop 0>` and
    `gdsc-regulator.c` sends `{class: clock, res: gpu_noc_wa}` before clearing
    collapse. XNU has no AOP mailbox driver, so 0085 does not send it; this is
    the first suspect if CX power-up times out or the bus misbehaves. ARC
    metadata (`gfx.lvl`/`cx.lvl`/`mx.lvl`) is also not inspected yet, and the
    two voted GCC branches stay halted without interconnect votes.
  - **Hardware-verified 2026-10-21 01:13-01:17** (img-gpu-0085, `sweetgpu=2
    sweetlogdump=1`, iPhone X 16.7.16 root). The kernel reached SpringBoard and
    the ring contains:
    ```
    SWEET GPU: stage=2
    SWEET GPU: gcc_ddrss_gpu_axi_clk 0 -> 0x1
    SWEET GPU: cx before gdscr=0x222001 hw=0x5ff4
    SWEET GPU: cx after gdscr=0xf8222000 hw=0x80001fe0
    SWEET GPU: gpu_cc_ahb_clk 0x1 -> 0x1
    SWEET GPU: gpu_cc_cxo_clk 0x1 -> 0x1
    SWEET GPU: rbbm status=0 int=0 status3=0
    SWEET GPU: cp always-on hi=0 lo=0 (no register response)
    ```
    `hw` BIT(31) is set, so the CX domain powered up and stayed up; the AOP
    `gpu_noc_wa` QMP was not needed for this step on this unit. The all-zero
    RBBM/CP reads are expected while GX is off (the GPU core is in the GX
    domain and the GMU owns that head switch); they are not evidence of a
    register-bus failure. The two voted GCC branches remain halted, as before.
  - Next kernel step: power GX through the GMU path (or a separate, bounded
    GX experiment with the vendor SW-reset/clamp sequence), then load
    `a640_gmu.bin`, program the GMU SMMU contexts and start the CM3. The
    `gfx.lvl` ARC vote and the AOP QMP remain unverified and become relevant
    before any GX power state is left enabled.
- **GX GDSC alone cannot power the GPU core (patch 0086, 2026-10-21 01:28).**
  `xnu/patches/0086-sweet-guacamole-gpu-gx-probe.patch` adds `sweetgpu=3`:
  vendor GX SW-reset (0x1008), AON GMEM reset (0x1508 BIT(4)), deassert clamp
  (BIT(0)), clear `SW_COLLAPSE` (0x100c), poll 0x1010 BIT(16); it collapses GX
  again after the read. Hardware log (img-gpu-0086):
  ```
  SWEET GPU: gx before gdscr=0x222001 cfg=0x88400
  SWEET GPU: gx after gdscr=0x8222000 cfg=0x1080400 (power-up incomplete)
  SWEET GPU: gpucc gx_cfg=0x1080400 cx_hw=0x80001fe0
  SWEET GPU: gpucc gdscr gx=0x8222001 cx=0xf8222000
  ```
  PWR_UP never asserted; RBBM still read 0. The board booted to SpringBoard
  and the collapse path ran, so the failure is bounded. This is consistent with
  the missing `gfx.lvl` ARC/regulator vote, not with a bad GDSC write.
- **Next: read the ARC tables without voting (patch 0087).**
  `xnu/patches/0087-sweet-guacamole-gpu-arc-dump.patch` adds a read-only
  command-DB parser for `gfx.lvl`, `cx.lvl`, `mx.lvl` (slave ARC 3, aux u16
  level tables) and calls it from `sweetgpu=3` before the GX attempt. It does
  not write RPMh. The next hardware boot records the real resource addresses
  and level tables; only then can a dependent GX/MX vote be designed.

### 2026-10-20 (later): Setup-intact root image, ADT --dump trap, display geometry
- **Setup image built and flashed.** `iosroot-build/build-root-setup.sh` copies the iOS 16.7.16
  root (16 GiB raw HFSX, 12 GiB used) and `tools/sweet-root-tweaks.py --keep-setup` applies the
  usual tweaks *without* the purplebuddy SetupDone prefs (launchd 7 × `_PanicOnCrash` renamed,
  2 CommCenter jobs removed, `/private/var/containers` created, VirtualAudio off, MediaToolbox
  recorder off, migration/springboard prefs). The fresh image has no mobileactivationd system
  container, so the data-ark step is skipped (`warning: ... skipped data ark`) - activation still
  depends on the ADT Hactivation flags. `fsck.hfsplus -f -y` once (Linux free-block-count quirk),
  then `img2simg ... 4096` -> 12,145,291,576 B `.simg`, flashed to userdata in 412 s.
  Note `--keep-setup` must come after the image path; the tweaks script is flag-aware now.
- **`tools/mkadt.py FILE --dump` used to overwrite FILE.** The dump path rebuilt the tree with the
  *default* device and wrote it before dumping, so dumping `shim/out/guacamole.adt` silently
  replaced the guacamole profile with sweet's `sweet-fbcon` (1080x2400, stride 4320,
  reg 0x1700000) - that stale ADT was then restored from a backup and used for the t19 boot.
  Fixed: `--dump` on an existing file now parses/dumps it without rebuilding. Current
  `shim/out/guacamole.adt`: 1440x3120, stride 5760, reg 0x2400000, no `/product`
  (sha256 597ce111...).
- **Display chain facts** (to kill the "display geometry" theory): `AppleCLCD` takes
  base/width/height/stride from the boot args `Video` struct (shim `FB_BASE 0x9c000000`,
  `FB_W 1440`, `FB_H 3120`, stride 5760), not from `sweet-fbcon`; that ADT node only feeds the
  kprintf fbcon (`pe_serial.c fbcon_probe`). The letterbox math for a panel larger than the
  1125x2436 canvas is correct: `fOffX = (1125 - 1440) / 2 = -157`, and `present()` does
  `dx -= fOffX` (so +157) and clips, i.e. the canvas lands centered with 157/158 px side bars.
  The black screen is therefore not stride or offset: the GUI never presents because the user
  session is dead (usermanagerd falls back to loginwindow, SpringBoard SIGSEGV).
- **SpringBoard's "bounding path bitmap" crash is missing `/product`, not personas.** On the fresh
  Setup image SpringBoard still crash-loops with `NSInternalInconsistencyException: Failed to load
  bounding path bitmap data from the asset manager for asset name: BoundingPathBitmap-1125x2436-
  375x812-3.00x` (UIKitCore). The asset name comes from `-[NSString stringWithFormat:]` with
  `BoundingPathBitmap-%ldx%ld-%ldx%ld-%.2fx`; that rendition *exists* in
  `UIKitCore.framework/BoundingPathData.bundle/Assets.car`. The lookup chain (UIKitCore
  `___UIKitSharedBoundingPathDataManager` -> `-[_UIAssetManager dataNamed:]` ->
  `-[CUICatalog dataWithName:]` -> `_dataWithName:deviceIdiom:deviceSubtype:memoryClass:
  graphicsClass:...`) filters renditions by the device artwork attributes, which MobileGestalt
  answers from `/product` (`artwork-device-idiom=phone`, `artwork-device-subtype=0x984`,
  `device-perf-memory-class=4`, `artwork-scale-factor=3`, `graphics-featureset-class=APPLE4`).
  With `/product` removed the attributes are unknown and the lookup returns nil, so the exception
  is fatal. sweet never hit this because its ADT always carried the D22AP `/product`.
  `/product` is restored; the `graphics-featureset-class=APPLE4` Metal worry has to be re-tested
  on top of the *correct* fbcon ADT (the earlier "garbled" run used the stale sweet fbcon).
- **Volume keys on guacamole (patch 0083).** The key driver was compiled for guacamole but only
  polled the PON, so power worked and both volume keys were dead. PM8150 volume up/down are GPIO6
  and GPIO7 (LineageOS 18821 DTS, both active low). The missing piece was the SPMI arbiter
  addressing: a peripheral is selected by PPID = `(SID << 8) | (reg >> 8)` and an observer
  command only carries the register's low byte, so every PMIC GPIO *pin* owns a 0x100-byte page
  and needs its own APID lookup. GPIO6 RT_STS `0xc510` -> PPID `0x00c5`, GPIO7 `0xc610` ->
  `0x00c6`, both read at offset `0x10` bit 0. (sweet's `0x4c1` = PM6150L SID 4 + page `0xc1`
  follows the same formula.) The driver now finds both APIDs, reads MODE_CTL/EN_CTL/SUBTYPE to
  confirm an enabled digital input (the ABL configures these for its own key menu and the PMIC
  keeps it across `fastboot boot`), withholds a pin that fails the check instead of writing the
  PMIC, and feeds `sweet_hid_key(1/2)`. Boot log: `SWEET KEYS: volup-ppid=0xc5 ... input=1`.
  The patch stack was replay-validated: 0001..0083 reproduces the staging tree exactly.
  **Verified on the phone 2026-10-20 (img-t28): both keys register (`key=1` / `key=2`) and drive
  the iOS volume HUD, and the ABL's input/pull-up configuration survived `fastboot boot` as
  expected (`input=1` for both pins).
- **GPU pre-flight (2026-10-20 late), and a hard lesson.** The live DT gives the real GPU topology:
  `/soc/qcom,kgsl-3d0@2C00000` (`qcom,chipid = 0x6040001`, windows `0x2c00000`+0x40000,
  `0x2c61000`+0x800, `0x6900000`+0x44000, `0x780000`+0x6fff, IRQ SPI 300), GPUCC
  `qcom,gpucc-sm8150` at `0x2c90000`, and a **dedicated GPU SMMU** at `0x2ca0000`
  (`qcom,qsmmuv500`, TBUs at `0x2cc5000`/`0x2cc9000`, `qcom,kgsl-smmu-v2` contexts with
  **SID 0 = gfx3d_user, SID 2 = gfx3d_secure**) - not the apps SMMU at 0x15000000. Firmware
  extracted to `research/firmware/sm8150/` (`a640_gmu.bin`, `a630_sqe.fw`, `a640_zap.mbn`,
  the last one ELF32 QUALCOMM DSP6; linux-firmware mirror).
  **Do not read GPUCC from the shim.** The ABL leaves the GPU's AHB clock gated; a read of
  `0x2c90000` in the shim wedged the interconnect and the watchdog dropped the phone into EDL
  (USB `05c6:900e`, recovered with power+volup+voldown). This is the same class of trap as the
  UFS register probe on this board. Any GPUCC/GPU access must first enable GCC
  `gpu_cfg_ahb_clk`, the GPUCC AHB clock and vote the CX/GX rails (RPMh) - i.e. the power-up
  sequence and the first register reads are one step, never a blind read.
- Payloads rebuilt with the corrected ADT + `/product`: `/tmp/opencode/img-t26.img` (console +
  `sweetlogdump=1`) and `img-t27.img` (noconsole, `sweetfb_hz=30`). Booting t26 on the Setup image
  reached the **real Setup wizard**: Hello -> language -> region -> "App & Data" with all five
  choices rendered by SpringBoard (touch works). Tapping "不传输 App 与数据" then hangs on an
  unlabelled spinner, so the wizard was not completed; the Setup image is still on the phone's
  userdata partition unless the skip image was re-flashed.
- **2026-10-20 evening: the skip-setup image boots to a usable home screen.** Confirmed on the
  phone: userdata = `iosroot-build/ios16-root-guac.simg` (the original Setup-skipped image),
  payload = `/tmp/opencode/img-t27.img` (t18 personas kernel + corrected guacamole ADT with the
  full D22AP `/product`, `sweetfb_noconsole=1 sweetfb_hz=30`) -> SpringBoard home screen, icons
  present, apps open, touch works, no console overlay. The same image previously died on the
  bounding-path exception, so the missing `/product` was the single cause of the
  "icon store fails / SpringBoard SIGSEGV / loginwindow fallback" chain; personas alone were not
  enough. The Setup image is no longer on the phone (userdata was re-flashed with the skip image).
  Open items: verify auto-lock/wake (power button), long-run stability and frame rate, and decide
  whether the mobile session exists in this image (apps launching implies it does).

### 2026-10-19 (late): XNU BOOTS ON THE GM1910
- CI via the fork **wochgn/ios-guacamole** (renamed from `wochgn/ios-sweet` on 2026-10-20;
  the account has no push rights to dipa-studio;
  `gh` device-flow login). Four GUACAMOLE builds to green, each fixing a real porting bug:
  header export (`pexpert/pexpert/arm64/Makefile` PRIVATE_KERNELFILES missed GUACAMOLE.h /
  sweet_dev.h), arch filter (`MakeInc.def` must keep GUACAMOLE on plain arm64, not arm64e),
  build glue (`bsd/conf`, `libkern/conf` Makefile.template gated on `SWEET`), and the
  platform/CPU IONameMatch. Artifact: `kernel.development.guacamole` (run 39205264491,
  SHA256 afb6d16e…), 17 MB.
- **Boot 1** (no rd=): `panic: Unable to find driver for this platform` - KernelConfigTables
  matched 'xiaomi,sweet' / 'qcom,kryo470'; the guacamole ADT carries 'oneplus,guacamole' /
  'qcom,kryo485'. Fixed board-aware in patch 0082.
- **Boot 2** (no rd=, cpus=1): platform expert registered, SweetCPU + PSCI, workqueue
  self-tests PASSED, ifnet(lo0/gif0/stf0), HFS-627.100.6 + pthreads-114.100.2 + AMFI +
  Sandbox, HID buttons/touch, battery, and
  **`SWEET FB: AppleCLCD 1440x3120 stride 5760 at 0x9c000000, default surface 1`** - the
  display service runs on the phone. Then silence: the kernel sits in IOFindBSDRoot's
  silent wait for a root device (arm64 suppresses the "Still waiting" print; no rd= was
  given). Expected state, not a hang.
- Next: UFS test boot with `rd=ufsroot0` **read-only** (no ufsrw/rootrw - Android's userdata
  must not be written until the user approves replacing it). If the driver's first register
  reads hang (the shim probe did), the SM8150 UFS host needs its GCC clocks/GDSC powered up
  by the driver instead of assuming ABL left it running.
- **UFS WORKS on the phone** (boot with `rd=ufsroot0`, read-only): the driver read the GPT
  and registered `userdata` (`first block 0x6c1800, 252270 MiB, read-only`), `BSD root:
  ufsroot0`; the HFS mount then fails on Android's ext4/FBE data (`unknown Volume Signature
  : 2527`, errno 19) and bsd_init retries forever - expected, nothing written. The earlier
  shim freeze was only the MMU-off/strongly-ordered probe, not the host state. The stubs
  print as designed (`SWEET TOUCH/USB: no driver for this board yet`).
- Next test (non-destructive): RAM-disk HFS+ root. `mkfs` tooling on this machine is not
  packaged; the Debian Linux port (glaubitz/hfsprogs-debian) builds with a one-line stub
  `include/sys/sysctl.h` (glibc dropped the header; the LINUX paths never call it):
  `make -C BlocksRunTime -f Makefile.lnx && make all` -> newfs_hfs/fsck_hfs. Test volume:
  `truncate -s 32M root.img; newfs_hfs -v SweetRoot -s -J root.img` (HFSX+journal),
  packaged with `mkpayload.py --ramdisk root.img` and booted with `rd=md0`; the ADT carries
  /chosen/memory-map RAMDisk = <pa,size> for IOFindBSDRoot. This exercises vfs/HFS/exec
  without touching the phone's storage; with no /sbin/launchd the expected end is a
  load_init_program panic.
- **Boot 3 (md0, 2026-10-19 late): the whole storage path is proven on the phone.**
  `Added memory device md0`, `BSD root: md0 major 1 minor 0`, `hfs: Initializing the
  journal`, **`hfs: mounted SweetRoot on device b(1, 0)`**, then
  `load_init_program: attempting to load /sbin/launchd` ->
  `panic: Process 1 exec of /sbin/launchd failed, errno 2` (expected: the test volume has
  no userland). XNU on the GM1910 now does platform -> UFS -> vfs -> HFS+ journaled mount ->
  exec. What remains for a real userland is the iPhone X 16.7.16 root filesystem.
- Next big step: build that root image (IPSW -> HFSX) and put it in `userdata`; **flashing
  userdata erases Android on this phone - the user must approve explicitly** (it is the same
  approach as sweet, but this is a different device than the 2026-10-15 approval).
  Tooling path chosen: IPSW (unzip) -> OS.dmg (APFS) read without root (7z or apfs-fuse if
  available/buildable) -> write files into an HFSX image with the `hfsplus` userspace tools
  (hpcopy; no loop mount, no root) -> img2simg -> fastboot flash userdata. mkiosroot.sh
  currently assumes sudo + loop mount, so either the user provides sudo on this machine or
  the tooling is switched to the userspace path.
- **SMP works on the phone: 8/8 cores** (boot with `cpus=8`, md0 root): PSCI CPU_ON rc=0 for
  every secondary, `SWEET CPU: cpuN mpidr=0xN00 came alive; SGI1 OK` for cpu1..cpu7 and
  `SWEET CPU: 8/8 CPUs initialized, SGI1 verified`. The MPIDRs match the ADT exactly
  (0x400/0x500/0x600/0x700 = the A76 cluster + prime). The launchd panic still ends the boot
  (no userland on the test volume), and the secondaries finish starting after it.
- **USB prepared for guacamole**: GCC registers from mainline gcc-sm8150.c - GDSCR 0xf004,
  CBCRs master/sleep/utmi 0xf010/0xf014/0xf018 (same as sweet), cfg-noc 0xf078, aggre 0xf07c,
  USB3 PHY aux 0xf050, clkref 0x8c008 (no ahb2phy on this SoC); the driver table reads them
  from sweet_dev.h and `SWEET_DEV_USB` is now 1 for guacamole, so `sweetusb=1` (state probe)
  runs by default and `sweetusb=2` starts the CDC-ECM gadget + the UDP live log (5140) once
  the host side has an address on the new interface (needs one sudo command on this PC, or
  the user configures it).
- Touch groundwork: `sweet_rpmh_vreg_enable` now takes the board's rail via
  `SWEET_DEV_TOUCH_VREG_NAME`/`_MV` (guacamole `ldoa17`, 3008 mV; the RPMh name is resolved
  in the command DB) and SweetHIDTouch clamps to the board's panel size. The S6SY761 driver
  file (GENI SE17 + sec_ts protocol) is the next kernel item after USB.
- **USB on the GM1910: deep-dive results (2026-10-19 evening).** The controller is powered
  (GDSC on) and all seven GCC clocks are on; the DWC3 registers are readable; the femto-v2
  HS PHY is out of POR (read via the mainline offsets; SM8150 uses phy-qcom-snps-femto-v2.c,
  not the SM6150 qusb2-v2 layout). Chain of findings, each from a boot + video:
  1. `DEPCMD STARTTRANSFER` issued at bring-up **wedges the bus** (hang -> APSS watchdog ->
     hard reset -> ABL lands in EDL, `05c6:900e`). Fixed: arm ep0 only on USB reset /
     XferNotReady (SWEET_DEV_USB_ARM_ON_EVENT; sweet keeps its bring-up arm).
  2. With the arm fixed and no VBUS override the kernel survives, but the host sees nothing:
     the controller has no VBUS session, so it never pulls D+ up.
  3. Enabling the QSCRATCH override: the **wrapper at 0xa6f8800 reads fine but any write
     hangs the bus** (HS-only as well as HS+SS; the SS PHY is the USB3/DP combo ABL drives
     for the display, but HS alone is fatal too).
  4. The **PHY-side override** (femto v2 COMMON1 bit4 + CTRL1 bit0, byte writes) also hangs
     the bus: the PHY window is read-only in this state as well.
  Conclusion: forcing VBUS from the kernel needs the PMIC/Type-C/PD path (how ABL itself
  enumerates) - parked for now. The screen + phone-camera video remain the log channel;
  a cheaper next step for real logs would be a RAM console ring at SWEET_DEV_MARK_PA that
  the shim dumps on the next boot. USB progress markers in IMEM/DRAM did NOT survive
  (0x8f0 read back 0; the DRAM word read back stale 0x124000), so the video is the tool.
- USB code now has: `sweetusbstep=N` (stop before step N), `sweetvbus=0..3`, event-driven
  arm, per-step prints, and the `sweetusb=1` read-only state probe with the correct PHY
  registers.
- **Touch bring-up log (guacamole S6SY761), first boot 2026-10-19 evening.** The driver
  reached the clock step and bailed with "GCC branch 0x1e0xx halted". Root cause: the
  branch check list was copied from sweet's SM6150 driver (`0x18004/0x18008/0x1800c/
  0x18010/0x18144`) and blindly shifted to the wrap2 range. On SM8150 only
  `0x1e004` (m_ahb), `0x1e008` (s_ahb) and `0x1e144` (s0) are wrap2 clock registers
  (mainline and the vendor gcc-sm8150 agree); `0x1e00c`/`0x1e010` belong to no clock and
  read back with bit 31 set, which aborted the probe before any SE access (no EDL, no
  SE/IC prints - consistent with the code path). The status semantics are
  `clk_branch2_check_halt`: CLK_OFF bit 31 clear, or NOC FSM bits 30:28 == 0b010, and the
  s_ahb branch has a hardware clock gating bit (0x1e008 bit 1) that skips the check.
  Verified GCC facts for SE17: wrap2 = `qcom,qupv3_2_geni_se@cc0000`, se-clk =
  GCC_QUPV3_WRAP2_S0_CLK (0x52014 BIT(4)), m-ahb BIT(2), s-ahb BIT(1), RCG cmd 0x1e148.
  The driver now prints each branch value with a tag (ok/fsm-on/hwcg/HALTED) and
  `sweetforce=1` proceeds when one still reads halted.
- **Second touch boot (img-t3, 2026-10-19 evening).** The branch fix worked: `br 0x1e004=0 ok`,
  `br 0x1e008=0x2 hwcg`, `br 0x1e144=0x4220 ok`; the SE read back `fw=0x303 proto=3 status=0
  tx=0x20102964 rx=0x20104900`, packing readback matched, so the GENI engine itself is healthy.
  The IC still did not answer (`err=60` = ETIMEDOUT on every register) because the 2.8 V rail
  was never enabled: `SWEET RPMH: cmd-db locator=0x... invalid` - the guacamole
  `SWEET_DEV_RPMH_DICT_PA` in sweet_dev.h was `0x0c3f0000c` (3.29 GB, one extra zero) while the
  live tree's `qcom,cmd-db@c3f000c` is `reg=<0xc3f000c 0x8>` (the 8-byte locator holding
  {db pa, size}); the read landed in DRAM and produced garbage. Fixed the constant, made the
  cmd-db version/flags checks advisory (mainline only validates the magic 0x0c0330db), and added
  explicit SE17 I2C pin routing (gpio55/56 as qup17, 2 mA, per qupv3_se17_i2c_active) plus a
  post-configure pin readback. cmd-db facts verified: header 144 B, entry 24 B, hw types
  ARC=3/VRM=4/BCM=5, rail resource name "ldoa17" (parent node rpmh-regulator-ldoa17),
  DT init voltage 3008 mV.
- **ROOT CAUSE of the dead touch pins (found 2026-10-19 late evening): the SM8150 TLMM is
  four tile arrays, not one flat `0x1000*id` array.** The `pinctrl@03000000` region
  (size 0xdc2000) holds west (+0x100000), east (+0x500000), north (+0x900000) and
  south (+0xD00000) arrays with pins at `tile + 0x1000*gpio`. The vendor driver says so
  (`ctl_reg = base + REG_SIZE*id` with those four bases; `UFS_RESET(..., 0xdb6000)` =
  south + reset offset; SDC2 at 0x9b2000 = north + offset) and mainline indexes through
  per-tile regs as well (`msm_readl_*` -> `pctrl->regs[g->tile] + reg`, tiles from named
  resources). Addressing the region without the tile offset is a hole: every read returns
  0, every write is dropped, no fault and no EDL - which is exactly what the shim probe
  showed (`TLMM pins 0`, `wr old=0 new=0`), and why the 1.8 V enable (gpio119), the reset
  (gpio54) and the SE17 mux (gpio55/56, all SOUTH) never took effect while GCC/SE/RPMh
  (different regions) were fine. Touch pin tiles: panel reset gpio6, touch 54/55/56/119/122
  are SOUTH; the shim's gpio104 test pin is NORTH. Both the kernel driver and the shim
  probe now carry the tile map.
- **iOS userland on guacamole: personas, scenes, and the /product trap (2026-10-20).** The icon
  exception (`Failed to load bounding path bitmap data ... headboardPathBitmaps-1125x2436-375x812
  -3.0x`) that kept SpringBoard at the Apple logo had a concrete cause: `persona()` (syscall 494)
  lives in `bsd/kern/sys_persona.c`, which is `optional config_personas`, and the sweet MASTER never
  enabled it, so `usermanagerd` could not create personas 99/199 and every per-user XPC service
  (including the icon renderer) failed. Enabled it (`options config_personas` in
  `config/MASTER.arm64.sweet`); the t18 kernel contains `-DCONFIG_PERSONAS` and
  `_kpersona_alloc_syscall`. With that build SpringBoard stops crash-looping on a warm cache and
  launches scenes (`SpringBoard: launching lp root scene`, `PersonaSettingsSubscriber` runs). A
  freshly flashed userdata still crashes once while the icon cache is rebuilt, so the first boot
  needs several minutes (the sweet images had all been booted before, which hid this).
  The `/product` node was added to the ADT from the real D22AP tree (`ipsw img4 im4p extract
  Firmware/all_flash/DeviceTree.d22ap.im4p` -> `research/ipsw-x/d22ap.adt`, whose /product the
  sweeps in `tools/mkadt.py` already consumed); it makes the userland report
  `graphics-featureset-class = APPLE4`, so the window server takes the Metal/AGX path, which this
  port cannot render yet - the screen goes to garbage/letterbox. Reverted: keep the product node
  out until a software/CoreGraphics path is forced (see the CA_* knobs in the notes above).
- **🎉 TOUCH WORKS ON GUACAMOLE (2026-10-19 ~23:25 local, image t10).** Coordinates appear
  on screen when touching (`SWEET TOUCH: f0 down x,y` / `f0 up`), so the full chain is up:
  TLMM tile addressing -> pin config (reset 54, 1.8 V enable 119, IRQ 122, bus 55/56) ->
  RPMh ldoa17 3008 mV -> vendor reset timing -> boot-complete event -> DEVICE_ID "671" ->
  SENSE_ON -> sec_ts event decoding -> sweet_hid_touch(). Two platform traps had to be
  fixed on the way: (1) the SM8150 TLMM is four tile arrays at +0x100000/+0x500000/
  +0x900000/+0xD00000 inside 0x03000000 (touch pins SOUTH), and one 14 MiB mapping does
  not fit, so each tile is mapped separately; (2) SE17 has GENI_IF_DISABLE_RO (0x64) bit0
  set - its FIFO interface is fused off and only GPI DMA could drive it - so the transport
  is now a GPIO bit-bang I2C (about 100 kHz, open-drain emulation, SDA/SCL auto-swapped on
  the first NAK). The SE registers are only printed for the record. Note the current
  guacamole boot has no root (the UFS HFS+ mount fails with unknown volume signature), so
  no userland consumes the HID events yet; the driver side is complete.

- Boot recipe used: `python3 tools/mkadt.py shim/out/guacamole.adt --device guacamole --highmem`;
  `python3 tools/mkpayload.py xnu/out-guacamole/kernel.development.guacamole shim/out/guacamole.adt
  shim/out/payload-X.bin --cmdline '<args>'`; `(cd shim && BOARD=guacamole
  DTB=../dtb/boot_unpacked/dtb ./build.sh ../shim/out/payload-X.bin)`; then
  `fastboot boot shim/out/shim-boot.img`. The kernel is pushed but **not yet** in
  dipa-studio: the work lives on the fork (`git remote fork`).

### 2026-10-19 (night): PROBE BOOT VERIFIED on the GM1910
- Three `fastboot boot shim/out/shim-boot.img` runs on the phone. **The live-FDT
  report renders on the panel** - the ABL splash handover (0x9C000000, 1440x3120)
  and the whole shim console path work on the OnePlus 7 Pro.
- Run 1 froze after the report: the UFS register probe stalls the bus while ABL has
  the host gated, and the watchdog reset the phone (~2 min). Fix: `PROBE_UFS 0` in
  `shim/boards/guacamole.h` (sweet keeps 1) - the xnu UFS driver reports the same
  state later.
- Added a visible 180 s countdown (`PROBE idle Ns of 180s, then reset`) plus `con_at`
  to console.c, so a probe run is self-documenting. Runs 2/3: countdown reaches ~170 s,
  the shim's own reset fires (IMEM reason + PSCI SYSTEM_RESET), phone returns to
  Android in ~3.5 min total, `sys.boot.reason = bootloader`.
- Also fixed in `shim/probe.c`: per-node `#address-cells/#size-cells` inheritance
  (Qualcomm `/soc` is 1 cell), live node names `qcom,gcc` / `ufsphy_mem` / `msm-eud`,
  and the CPU reg length quirk (`/cpus` declares 1 cell but writes 2).
- Boot/CI state: the repo is still local-only (3 commits unpushed; no token here);
  the next step is the `machine_config=GUACAMOLE` CI build, then
  `mkadt --device guacamole --highmem` (upper bank 0x180000000+4 GiB) + payload +
  `fastboot boot` with `cpus=1`.

### 2026-10-19 (evening): GM1910 connected - live-FDT audit, probe image ready
- Phone: OnePlus 7 Pro **GM1910** (China), OOS 12 `GM1910_11_H.40`, slot `_b`,
  **bootloader unlocked** (`ro.boot.flash.locked 0`, orange), Magisk root over adb.
- Pulled read-only into the gitignored dirs: live FDT `dtb/guacamole-live.dtb`
  (749 KB, ABL-patched + dtbo overlays), `backup/boot_b.img` (96 MB) with
  `dtb/boot_unpacked/` (stock DTB container, 12 MB), `backup/dtbo_b.img` (24 MB;
  the panel + `Samsung_18821@48`/`sec-s6sy761` touch node live in the **dtbo**, not
  in the base DTB).
- New `tools/fdt-audit.py` (DTB parser + check table vs `tools/devices.py` and the
  driver constants; per-parent #address/#size-cells). Run:
  `python3 tools/fdt-audit.py dtb/guacamole-live.dtb --device guacamole` -> **20/20 match**.
- Corrections it forced (all applied): memory really is
  `0x80000000+0x3bb00000`, `0xc0000000+0xc0000000`, `0x180000000+0x100000000`
  (2 GiB mem-offline window at 0x100000000 and a 69 MiB hole are NOT memory) -
  `devices.py` now uses the upper bank 0x180000000+4 GiB; TLMM is `0x03000000`;
  RPMh cmd-db `0xc3f0000c`; touch bus `i2c@0xc80000` confirmed. Everything else
  (splash 0x9C000000+36 MiB, GIC, timer, UFS+PHY, SPMI, USB trio, GCC, IMEM,
  PS_HOLD, MPIDRs, VPPI19) matched as built.
- `shim/probe.c` fixed the same way (per-node cell sizes; live-tree node names
  `qcom,gcc`, `ufsphy_mem`, `msm-eud` now matched). Host-rendered the report from
  the **real** live FDT: every line decodes correctly.
- **Probe boot image is built**: `shim/out/shim-boot.img` (BOARD=guacamole,
  stock DTB). Next action: `fastboot boot shim/out/shim-boot.img` (needs fastboot
  mode; not yet tested - fastboot was not entered from this machine).
- Still open: kernel CI run (`machine_config=GUACAMOLE`, needs the repo owner or a
  fork/token), then the payload boot with `cpus=1`; S6SY761 driver, volume-key
  APIDs, USB GCC offsets, smb5 as before.

### 2026-10-19: guacamole port scaffold (OnePlus 7 Pro, SM8150), earlier workstation note below
- New: `tools/devices.py` (per-board profile) + `tools/mkadt.py --device sweet|guacamole`;
  `shim/boards/*.h` + `BOARD=guacamole shim/build.sh` (also `DTB=` and `UFS_BASE` moved there);
  kernel patch **0082-sweet-guacamole-board.patch**: `MACHINE_CONFIG=GUACAMOLE`
  (`makedefs/MakeInc.def`, `board_config.h`, `GUACAMOLE.h`, `MASTER.arm64.guacamole`),
  per-board constants in `pexpert/pexpert/arm64/sweet_dev.h`, early-fb PA/geometry and
  HID panel size parameterized, touch/USB stubs for guacamole (SWEET_DEV_TOUCH/USB 0),
  `SweetFramebuffer` now letterboxes the iPhone X 1125x2436 canvas on a larger panel.
  Staging scripts 0019/0020/0026 accept `GUACAMOLE`. Docs: `GUACAMOLE.md`, README section.
- Verified locally: sweet ADT byte-identical before/after the mkadt refactor (4 fixed-seed
  combos); sweet shim `out/Image` byte-identical (modulo the new BOARD banner line) and
  guacamole shim builds; full replay (stage + 85 patches + 0082, CI order) applies cleanly
  and the result equals the development tree.
- **Bootable loader for the first phone contact**: `shim/probe.c` - with no payload (or a
  failed one) the shim prints the live-FDT bring-up report: DTB/root identity, MEM banks,
  RSVD carve-outs (splash 0x9C000000+36 MiB), GIC/TMR/SPMI/UFS/USB/DWC3/HSPHY/QMPPHY/TLMM/
  GCC/RSC/IMEM/PSHOLD, /chosen, CPUS MPIDRs, pre-console FB pixel stats, then the UFS
  probe; self-reset after ~3 min. Rows/cols adapt to the panel (guacamole 58x64,
  sweet 43x50). Host-renderable without a phone: `tools/mkfdt-test.py` +
  `shim/out/host_test FILE.dtb` -> `out/probe.ppm` (both boards rendered and checked;
  an unrelated 327 KB MT6895 tree was fed in once as a foreign-tree robustness input for
  the parser only - no non-Qualcomm DTB is used by any build or shipped image).
  `shim/build.sh` with no payload argument builds exactly this probe image.
- Values: board facts from the LineageOS oneplus_sm8150 DTS (project dir 18821) and the
  mainline guacamole DTS - splash `cont_splash_region` 0x9C000000 (36 MiB, mainline uses
  the same range as a simple-framebuffer), panel S6E3HC2 1440x3120, touch **Samsung
  S6SY761** @0x48 on GENI SE17 (`i2c@0xc80000`), keys on PM8150 GPIO6/7 (volume) + PON
  power, PMIC SIDs (8150=0, 8150B=2, 8150L=4, 8009=0xa), touch rail `ldoa17`, usb0
  a600000 + hsphy 88e2000 + QMP 88e8000 + QSCRATCH a6f8800. SoC-level: GIC 17a00000/
  17a60000, CNTFRQ 19.2 MHz, VTIMER PPI3 = 19, UFS ufshc@1d84000 (HS-G4, 2 lanes),
  RPMh RSC 18220000 / TCS 18220d00, MPIDR 0x0..0x700, PSCI smc, PS_HOLD c264000,
  **IMEM 146bf000** (shim restart_reason 0x146bf65c - differs from sweet's 146aa000).
  `--dram-size` added to mkadt (SKU: 0x180000000 = 8 GB, 0x280000000 = 12 GB; default
  stays sweet's conservative 0xfcc00000, which fits every SKU).
- Still unmapped: volume-key APID selection (keys poll power only), S6SY761 driver,
  SM8150 GCC GDSC/CBCR offsets (USB kept off), RPMh command-DB address (touch rail
  `ldoa17` vote), smb5 battery registers. All listed with next steps in `GUACAMOLE.md`.
- Next for a real try: build `xnu.yml` with `machine_config=GUACAMOLE` (fork or the repo
  owner), dump the guacamole stock DTB and audit the remaining *(verify)* values, then
  `mkadt --device guacamole --highmem --dram-size <SKU>` + payload + `BOARD=guacamole`
  shim and `fastboot boot` with `cpus=1`.

### 2026-10-19: workstation setup only (`invalidname` machine), no phone work
- Repo verified complete: 229 commits, tree clean, HEAD `fdad0de` = `origin/main` (no partial clone, no LFS).
- Pinned third-party sources downloaded into `research/` (gitignored), at the exact refs CI stages:
  xnu-8796.141.3 `1b191cb5` (tag `xnu-8796.141.3`), libpthread-514.100.2 `a6c70ca0`,
  hfs-627.100.6 `31d5ec23`, IOHIDFamily-1915.140.3 `2365347f`, BearSSL `7bea48e5`,
  darwin-xnu-build `66390652` (build harness). Details + recreate commands: `research/DOWNLOAD-MANIFEST.md`.
- Checked patch 0058 against the IOHIDFamily clone: 14 files byte-identical, the rest differ
  only in kernel include paths/`SWEET` comments, `IOHIDEventService.h` is the project's minimal
  replacement, `SweetHID.cpp`/`files.arm64`/`KernelConfigTables.cpp` are project files.
- This machine has no GitHub token and no `gh` (artifact download needs auth), and no phone
  work artifacts (`/run/media/dipa/...` does not exist here). No kernel build/boot attempted.

### >>> UPDATE 2026-10-18 (read first, then the 10-17 handoff below) <<<

**STATE at end of 2026-10-18 (evening):**
- On the phone: userdata = **image v15** (`/run/media/dipa/android/ios-sweet-ipsw/ios16-root-20H392-v15.img/.simg`; = v14 + `/private/var/preferences/SystemConfiguration/preferences.plist` with one network service "USB Ethernet": Interface DeviceName ue0 Type Ethernet, IPv4 Manual 192.168.7.2/24 router 192.168.7.1, DNS 1.1.1.1 8.8.8.8, set "Automatic" as CurrentSet). Fallback image v14 (v12/v13 deleted).
- Kernels: **0078** = all 8 cores (`cpus=8`, cpu6/7 = A76, homogeneous SMP; mkadt now emits 8 CPU nodes; ADT `shim/out/sweet-0078hi.adt`); **0079** = display off/on via IOMFB selector 12 kern_RequestPowerChange(state) (fade over 6 vsyncs, then black; verified: power button gives "SWEET FB: display off/on"); **0080** = `SWEET NET: <proc> route add/delete/change ...` and `... add/delete address ...` trace (built, `shim/out/shim-boot-0080s.img`, NOT booted yet).
- **Internet over USB: not working yet.** Host side is ready (temporary iptables: FORWARD accept for 192.168.7.0/24 + MASQUERADE, comment "ios-sweet"; lost on host reboot). With v15 the phone sent no DNS and no outbound traffic (only mDNS/ARP) -> configd probably did not apply the service (or did, without default route/DNS). Next: boot 0080s on v15, watch `SWEET NET` lines (does configd add 192.168.7.2 / default route on ue0?). If configd ignores ue0 (not an IOKit IONetworkInterface; configd 1241.140.6 source in `research/configd-1241`, SCNetworkInterface createInterface needs IONetworkInterface -> controller (IOMACAddress, SelfNamed) -> bus), add IOKit stand-ins and/or rename the ifnet to en*.
- Live log listener (`nc -ukl 5140`) may still be running, writing `logs/live-0078s.txt`.

**LIVE LOG over USB (0077, boot 0077s): no more recovery round-trips.** Boot an image with `sweetusb=2`, run `tools/sweet-usbnet.sh logs/live-NAME.txt` on the PC: it waits for the phone's CDC-ECM interface, sets 192.168.7.1/24 (sudo) and prints/saves the kernel console the phone streams every 100 ms as UDP to 192.168.7.1:5140 (starts with the last 64 KiB). `ping 192.168.7.2` checks the link. Current image: `shim/out/shim-boot-0077s.img` (kernel 0077, ADT sweet-0069hi, cpus=6, sweetfb_noconsole=1, sweetfb_hz=30, sweetusb=2). The recovery pull (`tools/sweet-log-pull.sh`) is still the way after a panic/hang before USB comes up.

**🎉 USB networking works (boot 0076s, 18:24 local): `ping 192.168.7.2` from the PC answers in ~1 ms, 0 % loss (200-packet burst, 1500-byte frames, 512/1024-byte frames).** 0075: CDC-ECM function (bulk 0x81/0x01, interrupt 0x82 NETWORK_CONNECTION; host MAC 02:00:5e:77:ee:01) + kernel ethernet ifnet `ue0` (legacy output, header strip like if_fake) with 192.168.7.2/24 set from the kernel (proto_plumb + SIOCAIFADDR on an in-kernel socket). 0076: received frames into one cluster mbuf (mbuf_allocpacket gave a first mbuf of length 0 -> panic "Failed mbuf validity check: len -14"). Host side (Linux cdc_ether, interface `enp0s20f0u3u1`): `nmcli dev set IF managed no; sudo ip addr add 192.168.7.1/24 dev IF; sudo ip link set IF up`. Boot image `shim/out/shim-boot-0076s.img` (`sweetusb=2`). iOS itself does not see ue0 yet (configd needs an IONetworkInterface in IOKit).

**USB device mode works (boot 0074s, 17:45 local): the host enumerates the phone at high speed as `1209:0001 ios-sweet "Sweet iOS (USB bring-up)"`.** 0072 read-only probe: ABL leaves the USB30_PRIM GDSC and all 7 USB clocks on, DWC3 v3.20a (GSNPSID 0x5533320a) in device mode (GCTL 0x00112000), halted (DCTL RUN_STOP 0, DSTS 0x00d3de04), event buffer still at ABL's 0xfdfc8000, QUSB2 PHY alive. 0073 (`sweetusb=2`): DCTL soft reset, own event buffer (polled, interrupt masked), ep0 DEPSTARTCFG/SETEPCONFIG/SETTRANSFRESOURCE, setup TRB, RUN_STOP; control-transfer state machine as Linux ep0.c; descriptors: vendor class, no data endpoints. It ran but no event came: **0074** forces the VBUS session in the Qualcomm wrapper (QSCRATCH 0xa6f8800: HS_PHY_CTRL |= UTMI_OTG_VBUS_VALID(20)|SW_SESSVLD_SEL(28), SS_PHY_CTRL |= LANE0_PWR_PRESENT(24), as Linux dwc3_qcom_vbus_override_enable) -> connect + enumeration. Image `shim/out/shim-boot-0074s.img`. Black Phone app: main thread waits in -[VMVoicemailManager fetchAccounts] (sync XPC; voicemail needs the modem).

**Navigation works (boot 0071s on v14, log `logs/boot-0071s.txt`):** swipe up from the bottom edge returns to the home screen; app switcher works. Two fixes: **0070** pid_hibernate (syscall 435 was nosys on the macOS config -> SIGSYS killed SpringBoard in -[SBToAppsWorkspaceTransaction _didComplete] when an app left the foreground; the only CONFIG_EMBEDDED-only entry in syscalls.master); **0071** touches starting within 48 px of an edge carry kIOHIDDigitizerEventFromEdgeTip (UIKit `_UIEventHIDTouchEdgeTypeForHIDEvent` reads the digitizer EventMask 0xb0007 bits 0x800/0x1000/0x2000/0x40000). Diagnosed, not fixed: Settings (Preferences) SIGTRAP in PodcastsUI func_1cdc47c58: force-unwrap of `URL.volumeTotalCapacity` (nil on our HFS+ root). Phone/Photos/Messages show black (MobilePhone launches, no crash) - next: stack samples via boot-arg `sweetsample=MobilePhone,MobileSlideShow,MobileSMS,SpringBoard` (image `shim/out/shim-boot-0071s.img`).

**🎉🎉 MILESTONE 2026-10-18 ~15:30 local: iOS 16.7 SPRINGBOARD HOME SCREEN ON THE PHONE** (boot `shim/out/shim-boot-0069s.img` = kernel 0069 + ADT `shim/out/sweet-0069hi.adt`, image **v14**; log `logs/boot-0069s-v14-HOMESCREEN.txt`). Home screen with a few apps, Settings opens, Control Center / lock screen / notification center work, touch + buttons work, 6 A55 cores (`cpus=6`), `sweetfb_noconsole=1 sweetfb_hz=30`. Known issue: cannot leave Settings (home gesture / AssistiveTouch home do nothing) - to investigate.
What it took today (in order): 0058 HID services (Apple IOHIDFamily-1915 user client), 0059 iPhone X identity (hw.product etc.), 0060 SPMI keys + GENI touch, 0061 1125x2436 presented display, 0062 RPMh L11C touch power + IOPMPowerSource battery, 0063 GENI FIFO packing (Goodix GT9896), 0064 PRIV_WORK_INTERVAL grant (SpringBoard "Failed to create work interval"), 0065 SMP via PSCI (6 A55), 0066 crash map regions, 0067 console mute without cons_ops write, 0068 IOSurface lookup of an already-mapped surface returns base -1 (backboardd snapshot crash), 0069 UFS log flush 10 s. Image v13: CommCenter removed from launchd.plist, BKNoWatchdogs. **Image v14** = v13 + Setup skipped (`/var/mobile/Library/Preferences/com.apple.purplebuddy.plist` SetupDone/SetupFinishedAllSteps true, SetupVersion 11, daemonizeNeedsToRun false; purplebuddy.notbackedup mini-buddy flags false; managedconfiguration.notbackedup LockdownActivationIndicatesCloudConfigurationAvailable false; springboard MCForceShowCloudConfigurationUI false) + mobileactivationd data ark seeded (`/private/var/containers/Data/System/D90C94C4-F94E-4FCF-8A28-A93BE3846D8E/Library/internal/data_ark.plist`, -ActivationState Activated etc.) + MediaToolbox D22/D221/Default SubServerConfiguration StartRecorderServer=false (mediaserverd crash loop: portrait-filter prewarm called NULL GL). **Activation:** ADT /chosen certificate-production-status 0, effective-production-status-ap 0, certificate-security-mode 1, effective-security-mode-sep 1 -> mobileactivationd's native Hactivation branch (Codex `codex/activation/report.md`; alternative 4-byte daemon patch documented there). Fallback image v13; v12 deleted.

**🎉 TOUCH WORKS (boot 0063u on image v13, 2026-10-18 ~13:15 local):** tapping (i) on Setup's Hello opens the device-info sheet; swipe-up is recognised (spinner, then back to Hello - under investigation). Chain: RPMh L11C vote (0062, `bsd/dev/sweet_rpmh.c`) -> GT9896 @0x5d on GENI SE7 (0063 fixed FIFO packing: register address went out byte-swapped) -> sweet_hid_touch -> SweetHIDTouch -> backboardd. Battery icon OK (0062 SweetBattery IOPMPowerSource, static 100 %). **Image v13** (= v12 + CommCenter jobs removed from launchd.plist + `BKNoWatchdogs`=true and `FBLaunchWatchdogExceptions{com.apple.purplebuddy:100}` in /var/mobile/Library/Preferences/com.apple.springboard.plist): Setup no longer killed by the scene-create watchdog (it blocked in CoreTelephony getSubscriptionInfo -> CommCenter, Codex `codex/setup/report.md`; that report also lists how to skip Setup: SetupDone/SetupFinishedAllSteps/SetupVersion=11 in com.apple.purplebuddy.plist + activation check). v9 deleted, fallback = v12. Input latency ~1 min with 1 CPU -> Codex task 11 = SMP via PSCI (`codex/smp/`).

**INPUT WORKS (boot 0060u on a freshly flashed v12, log `logs/boot-0060u.txt`):** power, volume up, volume down reach iOS (PMIC -> sweet_keys.c -> SweetHIDButtons -> backboardd). Power locks/unlocks (screen "off" shows the kernel console because the panel never really turns off). **Device identity fixed: iPhone X** (home indicator, Face-ID-style Setup). Earlier test (0059u, `sweethidtest=15` fake Home presses) advanced Setup from Hello to the language list.
- **0058** HID: `iokit/Kernel/arm/hid/`: IOHIDEventServiceUserClient + IOHIDEventServiceQueue + IOHIDEvent compiled UNCHANGED from Apple IOHIDFamily-1915.140.3 (= exact version of the iOS 16.7 IOHIDLib, clone in `research/iohid-1915`) over our minimal IOHIDEventService (SweetHID.cpp: SweetHIDButtons consumer page 0x0C power 0x30 / vol 0xE9/0xEA / menu 0x40; SweetHIDTouch digitizer collections). C hooks `sweet_hid_key(key, down)` (0 power, 1 vol+, 2 vol-, 3 home) and `sweet_hid_touch(finger, down, x, y)`. HID logs go to IOLog. Boot-arg `sweethidtest=N` = fake Home press every N s.
- **0059** identity: hw.machine/hw.model/hw.target/hw.product from the ADT (macOS-config kernel used to answer "iPad8,6"/"Redmi Note 10 Pro"). mkadt: root compatible `D22AP|iPhone10,3|xiaomi,sweet|...`, model iPhone10,3, target-type D22, `/product` copied from the real D22AP device tree (`research/ipsw-x/d22ap.adt`, extracted from the IPSW, NOT in git), `/buttons home-button-type 2`. Codex report `codex/mg/report.md`: MG ProductType = sysctl **hw.product** (unrecognized -> iPod), HWModelStr = hw.target, HomeButtonType = `/buttons/home-button-type`; disk cache `/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist` (keyed by build + UUID only -> stale iPod answers survived; reflashed clean v12 to clear it).
- **0060** drivers by Codex (`codex/input/hw-report.md`): `bsd/dev/sweet_keys.c` (SPMI arbiter v5 observer reads, PON 0x810 bits KPDPWR/RESIN SID0, PM6150L GPIO2 SID4 0xc110; works), `bsd/dev/sweet_touch.c` (GENI SE7 I2C @0xa84000, starts 45 s after boot). Touch result: SE7 fw ok (proto 3) but **0x5d and 0x38 both time out -> touch IC unpowered** (L11C LDO needs an RPMh vote). Codex task 8 = RPMh client (`codex/rpmh/`), task 7 = battery (`codex/battery/`).
- **0061** (CI 39117078444): red status bar "rdar:45025538" = UIKit "No status bar visual provider for screen" (360x800 pt is no real iPhone). AppleCLCD now presents **1125x2436** (iPhone X) and shows the centered 1080x2400; touch mapped the same; ADT canvas 1125x2436. `sweetfb_native=1` reverts.
- Images: `shim/out/shim-boot-0060b.img` (0060 + /buttons ADT `sweet-0060hi.adt`). Next: 0061 image with ADT from current mkadt.

### >>> HANDOFF 2026-10-17 late (read this first) <<<

**STATE: iOS 16.7 Setup.app "Hello" screen is ON THE PHONE** (boot `shim/out/shim-boot-0057u.img` on image v12). Photo `/run/media/dipa/android/ios-sweet-work/MILESTONE-setup-hello-2026-10-17.jpg`. SpringBoard + backboardd + Setup run; nothing crashes in the GUI path. Remaining crash/relaunch loops: installcoordinationd (should be fixed by 0057, unverified), mediaserverd (VirtualAudio removed in v12, unverified), mobileactivationd (~100/boot, not analysed), bluetoothd + fairplayd.H2 relaunch constantly (no hardware).

**What is on the phone now:** userdata = image **v12** (`/run/media/dipa/android/ios-sweet-ipsw/ios16-root-20H392-v12.img` + `.simg` + `.sha256`; v9 kept as fallback, v8/v10/v11 deleted or superseded). Kernel **0057** (`xnu/out-0057`, CI `39067532732`). ADT **`shim/out/sweet-0057hi.adt`** (QEMU twin `research/q0057-hi.adt`). Boot image **`shim/out/shim-boot-0057u.img`**. Boot args: `debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1`.

**Build a phone image** (kernel K, ADT A): `python3 tools/mkpayload.py $K $A shim/out/payload-X.bin --cmdline "$B" && (cd shim && ./build.sh ../shim/out/payload-X.bin && cp out/shim-boot.img out/shim-boot-X.img)` then check `python3 tools/unpack_bootv2.py shim/out/shim-boot-X.img shim/out/check-X && cmp shim/out/check-X/ramdisk shim/out/payload-X.bin`. QEMU smoke test: `python3 tools/mkpayload.py $K research/q0057-hi.adt $S/csq/payloadX.bin --ramdisk $S/csq/root-fork.img --cmdline "debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=md0"` + `PATH=~/swift/usr/bin:$PATH python3 tools/qemu-sweet.py ... --secs 40 --kernel $K --fail-on-panic` (S = scratchpad; root-fork.img lives in the session scratchpad csq/ dir and may be gone in a new session).

**Kernel workflow:** edit `research/bt28/xnu/...` (keep the pre-edit copy in `research/preNNNN/`), syntax check `python3 tools/xnu-cc.py research/run28.log research/bt28 <file>`, make `xnu/patches/NNNN-*.patch` with `diff -u pre bt28 | sed` path rewrite, add new/changed files to `research/check-patch-order.py` list, run it (must say applied + MATCH), commit + push -> GitHub Actions `xnu` builds (~8 min) -> `gh run download <id> -R dipa-studio/ios-sweet -n xnu-kernel -D xnu/out-NNNN`. **Repo is PUBLIC since 2026-10-17** (private macOS runner minutes ran out: "recent account payments have failed"); public = free runners. Commit trailer: only `Co-Authored-By: dipa-studio <183933817+dipa-studio@users.noreply.github.com>`.

**Phone workflow (user does buttons, Claude runs commands):**
- Boot test kernel: phone in fastboot -> `fastboot boot shim/out/shim-boot-X.img`. NEVER `fastboot getvar`.
- **Read logs WITHOUT filming:** after a test the user holds Power+VolDown -> fastboot -> `fastboot boot recovery/recovery.img` (OrangeFox, temp) -> wait for `adb shell true` -> `tools/sweet-log-pull.sh /run/media/dipa/android/ios-sweet-work/logs/<name>.txt` (reads the 4 MiB console ring the kernel keeps in the last 1025 blocks of userdata; patch 0047). Then `adb reboot bootloader` brings the phone back to fastboot without the user.
- **Flash a root image:** `fastboot flash -S 512M userdata <img>.simg` (7 min, 25 chunks). After a big flash the Xiaomi ABL fastboot hangs: next `fastboot boot` stalls at "Sending" -> the USER must re-enter fastboot by hand (Power+VolDown). Do not retry commands against the hung ABL. A flash right after `adb reboot bootloader` once failed on chunk 1 (USB protocol error): just retry. User must NOT unplug during flash (once interrupted at 25/25 -> reflash).
- **Edit the phone root without reflashing (same-length only):** find the file's raw offset in the local image (it is one extent; search the image for its first 4 KiB), compare with `adb exec-out dd if=/dev/block/sda34 ...`, then `adb push` changed 4 KiB blocks and `dd of=/dev/block/sda34 seek=... conv=notrunc,fsync`, read back. Recovery kernel has no hfsplus.
- **Pull the whole phone root** (12 min): `adb exec-out "dd if=/dev/block/sda34 bs=1048576 count=16384" > img`; mount ro with udisksctl or `sudo mount -t hfsplus -o loop,ro`. To write into a pulled image: `sudo fsck.hfsplus -fy` first (volume is never cleanly unmounted -> Linux mounts ro even with -o force), then `sudo mount -t hfsplus -o loop,force`.
- Making images: copy latest img with `cp --sparse=always`, mount rw with force, change, `sync; umount`, `sha256sum`, `img2simg`. Keep only the latest image + one fallback on the SSD (/home is ~98% full; big work files live in `/run/media/dipa/android/ios-sweet-work/`, old kernels/images archived in `.../ios-sweet-work/archive/`).

**Diagnostics built into the kernel (all SWEET-only):** persistent console log (0047, boot-arg sweetlog=0 off; flush every ~1 s); `|name| text` copies of /dev/null writes for processes in `sweetnull` (default backboardd,SpringBoard; 0048); `IOK name:` IOKit lookup/open trace for `sweetiolog` (default backboardd,SpringBoard; 0049/0054, 400 lines max); `SWEET Sandbox: proc op N` (0050); `STR ...` heap text dump on SIGABRT/SIGTRAP for `sweetscan` (default backboardd,SpringBoard; 0051/0053) - this is how NSException/BSAssert reasons were found; `SMP name tN pc .. bt ..` user stack sampler for `sweetsample` (default SpringBoard,backboardd; first at 90 s then every 120 s, 40 samples, 16 frames; 0054/0055); `SWEET FB: N swaps` every 1000 swaps; `CRASH name ...` crash reports (0043/0044). Symbolicate cache addresses with `ipsw dyld a2s research/dsc/dyld_shared_cache_arm64 <addr - slide>`; main-binary addresses: addr - mh + 0x100000000.

**Patches added today (0046-0057, all CI-built):** 0046 IOSurface (IOCoreSurfaceRoot) + IOMobileFramebuffer (AppleCLCD on /arm-io/disp0) + arm-io nubs; 0047 persistent log; 0048 /dev/null tee; 0049 IOKit trace; 0050 permissive "Sandbox" MAC policy; 0051/0053 heap text on crash; 0052 RENAME_SWAP via exchangedata on HFS; 0054/0055 sampler, swap counter, AKS selectors 35 (extended device state = 17) and 101; 0056 panel copy from the vsync timer at 10 Hz (sweetfb_hz) instead of every swap; 0057 no config_ext_resolver (ACL group entries resolve without memberd). ADT (tools/mkadt.py): /arm-io/disp0, /chosen/display-scale 3, /chosen/unique-chip-id (synthetic 0x5357454554000001) + chip-id 0x8015 -> IOPlatformUUID A9683613-3704-53F2-95D0-98A8FB574E78. Image v9-v12: /private/var/containers/{Bundle,Data/System,Shared/SystemGroup,Temp}; launchd.plist StandardOut/ErrorPath renamed to ...PatX (pointing jobs at /dev/console stops them launching: EACCES); v12: LastSystemVersion=20H392 in plain /var/mobile/Library/Preferences/com.apple.migration.plist (stops the 1-hour data migration that ran every boot), VirtualAudio.plugin moved to /Library/Audio/Plug-Ins/HAL.disabled/.

**Codex helper** (`codex exec --skip-git-repo-check -m gpt-6.1-sol -c model_reasoning_effort=high -s workspace-write -C /run/media/dipa/android/ios-sweet-work/codex -o OUT "$(cat task.md)" < /dev/null`, run detached with setsid nohup; config already defaults to gpt-6.1-sol high). Reports in `/run/media/dipa/android/ios-sweet-work/codex/`: mediaserverd-report.md, installcoordinationd-report.md (-> 0057), ufs-performance.md (ranked UFS speed plan), **ufs/0058-sweet-ufs-faster.patch** (READY, not yet applied: ufslogsecs default 10 s log flush batching, optional ufscache=1 directional cache ops, optional ufswait=1 spin-then-sleep; syntax-checked, host-stub verified; see ufs/README.md). Apply it as patch 0058 after review.

**Claude safety-classifier note:** several messages about QuartzCore renderer selection / writing an HFS reader tool were cut off by an automatic classifier (false positives, user agrees). Keep those topics short or let them be; they are not blockers now (rendering already works).

**NEXT STEPS (in order):**
1. **Input** (Setup stuck on Hello): touch + power + volume as IOHIDEventService/IOHIDDevice services backboardd matches (log shows backboardd matching IOHIDDevice with DeviceUsagePairs and IOService with HIDServiceSupport=true). Phone hardware: Goodix/Focaltech touch over I2C/SPI (check live DT `dtb/`), buttons via PMIC (pon) and GPIO keys. Start with a fake HID service (e.g. power button via a timer or boot-arg) to learn the ABI, then real drivers.
2. **Device identity**: MobileGestalt reports iPod + Home button. Add /product node / product-type (iPhone10,3, D22AP), device class iPhone, home-button-less (Face ID) properties in mkadt; find exact MG keys (MGIOMFBSupport.c, `IODeviceTree:/product` lookups seen in backboardd trace).
3. **Battery**: AppleSmartBattery/IOPMPowerSource style service (status bar shows empty red battery).
4. Apply Codex 0058 (UFS speed), consider cpus>1, check whether installcoordinationd/mediaserverd loops are gone, analyse mobileactivationd.
5. Hide kernel console over the GUI when not debugging: boot-arg sweetfb_noconsole=1 (but then panics are not on screen).

- **🎉 MILESTONE: iOS GUI ON THE PHONE - Setup.app "Hello" screen (2026-10-17 ~21:30 UTC, boot 0057u on image v12).** Photo: `/run/media/dipa/android/ios-sweet-work/MILESTONE-setup-hello-2026-10-17.jpg` (animated "dobar d(an)" hello lettering, status bar "iPod" + battery icon, bottom "Pritisnite Home za otvaranje" = "Press Home to open" (locale hr), (i) button). Rendered by iOS 16.7 SpringBoard/Setup through CoreAnimation -> backboardd -> kernel IOSurface (IOCoreSurfaceRoot) -> kernel AppleCLCD swaps -> ABL panel memory. What made it: data migration finally skipped (v12: `LastSystemVersion` 20H392 in the plain `/var/mobile/Library/Preferences/com.apple.migration.plist`; ADT `/chosen/unique-chip-id` + `chip-id` for IOPlatformUUID), 0057 (no ext resolver -> ACL containers work), plus everything before (0046 display/IOSurface, 0050 Sandbox policy, display-scale 3, containers dirs, RENAME_SWAP, AKS 35/101, 10 Hz present).
  **Missing (next work, in rough order):**
  - **Input: no touch, no power button, no volume buttons** (no HID drivers; backboardd looks for IOHIDDevice / HIDServiceSupport services). Setup is stuck on "Hello".
  - **Device identity: it thinks it is an iPod with a Home button** (status bar "iPod", "Press Home to open"): MobileGestalt has no product/model identity (no /product node, product-type, etc.) - give it iPhone10,3 / home-button-less (Face ID class) identity.
  - **No battery detection** (no battery/charger/PMIC service; status bar battery shows empty/red).
  - Locale came up Croatian (hr) - harmless.
  - Performance: cpus=1, polled UFS (Codex UFS patch in progress: `codex/ufs/`), backboardd presents at 60 Hz.
- **README refreshed (2026-10-17):** read the full notes and updated the public overview through patch 0045, v7 hardware observations and prepared v8 diagnostics. Documents real Apple userspace, the missing display/IOSurface GUI work, limited security services, highmem, persistent userdata root, current tooling and build examples. Documentation only; no kernel build, tests, flashing or phone boot performed.

- **v11 still migrated (boot 0056u, 20:32 UTC, `logs/boot-0056u-v11.txt`).** On iOS kCFPreferencesCurrentHost lands in the plain plist: all DMEnvironment setters (setLastBuildVersionPref, setUserDataDispositionPref, ...) use CurrentHost and the phone's `/var/mobile/Library/Preferences/com.apple.migration.plist` holds DMUserDataDisposition; datamigrator's XPC service runs with `_RoleAccount: mobile`. **Image v12** = v11 + `LastSystemVersion` = 20H392 added to that plain `com.apple.migration.plist` (keeps DMUserDataDisposition, 501:501 0600) + `/Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin` moved to `/Library/Audio/Plug-Ins/HAL.disabled/` (Codex: stops the mediaserverd SIGSEGV loop; untested). **installcoordinationd loop explained (Codex `codex/installcoordinationd-report.md`):** os_crash "Failed to create data directory" EACCES for `/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.installcoordinationd/Library/InstallCoordination`: `Library`/`Library/Caches` in the container are ACL-only (uid/gid 0xfffffffe, mode 0000, ACE for group GUID ...012B = gid 299 systemusers); with `config_ext_resolver` the kernel asks an identity resolver (memberd/opendirectoryd) that iOS does not run -> EWOULDBLOCK -> ACE skipped -> EACCES. **Patch 0057**: drop config_ext_resolver from FILESYS_BASE (compatibility UID/GID<->GUID mapping is used instead), likely fixes other ACL-protected containers too. Codex task 3: UFS driver performance design (`codex/ufs-performance.md`).
- **mediaserverd SIGSEGV loop explained (Codex report `/run/media/dipa/android/ios-sweet-work/codex/mediaserverd-report.md`):** the fault (FAR 0x390) is in the HAL plug-in `/Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin/VirtualAudio` (ApplyBeamFormerPreference <- VirtualAudio_PlugIn::SetPropertyData <- HALS_AHPPlugIn::ObjectSetPropertyData <- AudioObjectSetPropertyData <- MediaExperience vaemVADSerializationListener): the default virtual audio device ('vdef', VirtualAudioDevice_Default) does not exist (no audio hardware/driver, no /product/audio). Proposed workaround for GUI bring-up: move VirtualAudio.plugin out of /Library/Audio/Plug-Ins/HAL/ in the next root image (untested). v11 reflashed OK after an interrupted flash (USB unplugged at chunk 25/25). Codex task 2: installcoordinationd crash loop.
- **ROOT CAUSE of the endless migration: no IOPlatformUUID (2026-10-17).** datamigrator's `-[DMMigrator isMigrationNeededWithBuildVersion:lastBuildVersion:clientPID:]` gets lastBuildVersion from `-[DMEnvironment lastBuildVersionPref]` = `CFPreferencesCopyValue("LastSystemVersion", "com.apple.migration", kCFPreferencesCurrentUser, kCFPreferencesCurrentHost)` -> a ByHost file. The phone had `/var/mobile/Library/Preferences/com.apple.migration.plist` (only DMUserDataDisposition) and NO `Preferences/ByHost` directory at all. On arm XNU publishes IOPlatformUUID (gethostuuid) only from `/chosen/unique-chip-id` (8 bytes) + `/chosen/chip-id` (4 bytes) (IOPlatformExpertDevice::generatePlatformUUID, called from IOPlatformExpert::start); the SWEET ADT had neither, so every CurrentHost preference write failed. Fix: mkadt adds `unique-chip-id` = 0x5357454554000001 (synthetic) and `chip-id` = 0x8015 -> UUID `A9683613-3704-53F2-95D0-98A8FB574E78` (same SHA1 namespace computation as the kernel). ADT `shim/out/sweet-0057hi.adt`, boot image `shim/out/shim-boot-0056u.img` (0056 kernel + 0057hi ADT, QEMU OK). **Image v11** = v10 + `Preferences/ByHost/com.apple.migration.<UUID upper and lower>.plist` {LastSystemVersion: 20H392} (501:501, dir 0755 files 0600). Flashing v11 2026-10-17 ~20:10 UTC. Codex (gpt-6.1-sol high) analysing the mediaserverd SIGSEGV loop in `/run/media/dipa/android/ios-sweet-work/codex/` (report `mediaserverd-report.md`).
- **v10 did NOT stop the migration (boot 0056n on v10, 19:18 UTC, `logs/boot-0056n-v10.txt`):** datamigrator + migrationpluginwrapper ran again, backboardd drew the progress UI (13000+ swaps in ~5 min). So `com.apple.DataMigration.plist`/LastSystemVersion is not the deciding input. What is known about the decision (datamigrator XPC service `DataMigration.framework/XPCServices/com.apple.datamigrator.xpc/com.apple.datamigrator`, strings): `Migration sentinel present. Migration necessary.` (sentinel `/private/var/datamigrator/migrate`, created and removed by datamigrator itself; absent in v10), `System build version changed from %@ to %@. Migration necessary.` / `...unchanged... not necessary`, `Disposition dictionary with build version %@ is out of date relative to current build version %@`, `SyntheticDidUpgrade`, DMUserDataDispositionManager; previous build source not found yet (not in the binary's plain strings; likely in DataMigration.framework's DMUserDataDispositionManager / preferences domain `com.apple.migration`). backboardd calls `DMPerformMigrationIfNeeded` and holds SpringBoard in `checkInAndWaitForDataMigration` until it returns. NEXT: find where the previous build version / disposition dictionary is stored (disassemble DataMigration.framework DMUserDataDispositionManager + datamigrator's isMigrationNeeded path), write it into the image; or get datamigrator's os_log out (OS_ACTIVITY_DT_MODE needs a launchd env change).
- **Data migration never records completion -> skip it (image v10, 2026-10-17).** Boot 0056n (18:16 UTC, `logs/boot-0056n.txt`, 30 min): 10 Hz panel copies, backboardd still presents at ~60 Hz (107k swaps), SpringBoard still in `BKSSystemShellService _checkIn` at sample 14. Every migration run takes 1+ hour on cpus=1 and repeats each boot. DataMigration decides from `/var/mobile/Library/Preferences/com.apple.DataMigration.plist` key `LastSystemVersion` (vs ProductBuildVersion 20H392); the pulled phone root (21:01 local) had NO such file. **v10** = that pulled phone state + fsck.hfsplus -fy (volume was marked not cleanly unmounted -> Linux mounts it read-only even with -o force; fsck found only hard-link counts + minor volume header, repaired) + `com.apple.DataMigration.plist` {LastSystemVersion: 20H392} (501:501 0600). `/run/media/dipa/android/ios-sweet-ipsw/ios16-root-20H392-v10.img` (+ .sha256, .simg); v8 files deleted, v9 kept as fallback.
- **Repo made PUBLIC (2026-10-17, user request) so GitHub-hosted macOS runners are free:** the 0056 CI run was refused ("recent account payments have failed or your spending limit needs to be increased"). Checked before: no device dumps, serials, IMEI or Apple binaries in the history (.gitignore keeps backup/, dtb/, recovery/, research/ out); commit author emails are public now. 0056 rerun `39056660104` passed -> `xnu/out-0056`, QEMU OK, image `shim/out/shim-boot-0056n.img` booted 18:16 UTC.
- **Boot 0055n (17:16 UTC, ~65 min, `logs/boot-0055n.txt`): migration bar reached 99% in ~30 min (was ~60), then crawled.** Root cause of the slowness: backboardd presents the progress UI at ~58 Hz (**167,000 swaps in ~48 min**) and AppleCLCD copied the full 10 MB frame into the panel on every swap -> ~600 MB/s memcpy on the single CPU (cpus=1), starving everything (polled UFS I/O too). Sampler (16 frames): **SpringBoard waits in `-[BKSSystemShellService _checkIn]`** (synchronous BSXPC call to backboardd, from FBSystemShellInitialize <- SBSystemAppMain) - backboardd holds the system shell check-in until the data migration ends, so the migration is the gate to SpringBoard UI. **Patch 0056**: swap only records the latest swap record; the vsync timer copies it to the panel every 60/sweetfb_hz ticks (default 10 Hz); `SWEET FB: N swaps` every 1000.
- **Boot 0054n (16:53 UTC, ~12 min, `logs/boot-0054n.txt`, patch 0054 sampler):** the data migration runs AGAIN on every boot (it completed once in the 0053s run) - completion is not being recorded. backboardd keeps swapping the progress UI (3700+ swaps in ~10 min, `SWEET FB: N swaps`). Sample during migration: SpringBoard main thread blocked in a synchronous XPC call (`mach_msg2_trap <- dispatch_mach_send_and_wait_for_reply <- xpc_connection_send_message_with_reply_sync <- -[BSXPCServiceConnectionMessage _sendSynchronously:] <- BSXPCServiceConnectionProxy ...`). Unsupported AKS selectors seen: 101 `aks_assert_consume`, 93 `aks_get_internal_info`, 34 `_aks_stash_load` (datamigrator), **35 `aks_get_extended_device_state`** (SpringBoard, chronod, remindd, searchd, symptomsd...). **Patch 0055**: AKS 35 answered like 17 (same DER device state), 101 succeeds; sampler backtraces 16 frames, 40 samples (~80 min).
- **MILESTONE: FIRST iOS PIXELS ON THE PANEL (boot 0053s long run, 15:33 UTC, ~60 min; photos IMG_20261017_183029/183222/183419; log `/run/media/dipa/android/ios-sweet-work/logs/boot-0053s-long.txt`, 407 KB, 1428 flushes).** The iOS data-migration boot screen (Apple logo + progress bar) was shown and the bar advanced to full in ~60 minutes. It is drawn by **backboardd through our kernel display path**: `SWEET SURF: backboardd create id 2/3 1080x2400 fmt 0x42475241 (BGRA) bpe 4 bpr 4352 size 10452992 keys 11`, IOSurface sels 9, 27, 5, 34 and traps 2/3 (lock/unlock), IOMFB SwapBegin (4) / SwapEnd (5, 1132-byte struct) / GetLayerDefaultSurface (3) / 68 / 55, then `SWEET FB: swap N layer 0 surface 2|3 1080x2400 ... a 0,0 1080x2400 b 0,0 1080x2400` alternating (double buffering; only the first 16 swaps are logged). Triggered by `com.apple.datamigrator` (AKS sels 101/93/34 return unsupported) and `com.apple.migrationpluginwrapper`. Probably ProgressUI-style CoreGraphics drawing into IOSurfaces (no GPU used). After the migration: SpringBoard and backboardd stay up (exec once each, no crash), nothing new drawn; crash loops only for mediaserverd, installcoordinationd, mobileactivationd (~108 each in the hour); bluetoothd and fairplayd.H2 relaunch constantly (no hardware). Kernel console text overlays the image (sweetfb_noconsole=1 would hide it).
- **MILESTONE: SpringBoard and backboardd both stay up (boot 0053s on v9, 15:29 UTC, `logs/boot-0053s.txt`).** With /chosen/display-scale = 3: `exec SpringBoard` once, `exec backboardd` once, no CRASH for either; MobileGestaltHelper reads IOMFB size/dot pitch; backboardd opens AppleCLCD (sels 57,15,22,29,33,79,74 as before). No IOSurface creates and no swaps: backboardd keeps polling `IOProviderClass IOAcceleratorES` (-> 0 found) and AGXAccelerator/IOAcceleratorESLegacy - no GPU, so no CoreAnimation renderer yet. Log goes quiet after ~47 s (only 47 flushes): the system idles. NEXT WALL: CoreAnimation rendering without a GPU (CPU path) so the window server produces frames.
- **SpringBoard's invalid mode = scale 0 (boot 0053n, 15:19 UTC, `logs/boot-0053n.txt`, heap scan):** `FBSDisplayMode cannot be configured as requested : <FBSDisplayMode: ...; infxinf@0x (1080x2400/0) 60Hz sRGB SDR>` - pixel size and 60 Hz are fine, scale is 0. libMobileGestalt (MGIOMFBSupport.c, sub_19c560a54) reads `display-scale` (u32, default 0) and `display-rotation` from **IODeviceTree:/chosen**, pixel size via IOMobileFramebufferGetDisplaySize (MobileGestaltHelper sel 8/28 in the log). mkadt now adds `/chosen/display-scale` = 3 (1080x2400 @3x = 360x800 pt). ADT `shim/out/sweet-0054hi.adt` (QEMU `research/q0054-hi.adt`), image `shim/out/shim-boot-0053s.img` = 0053 kernel + 0054hi ADT. Possible catch: MobileGestalt may have cached scale 0 in `/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/...`.
- **MILESTONE: backboardd survives and opens the display (boot 0052n on v9, 15:07 UTC, `logs/boot-0052n.txt`).** No `CRASH backboardd`. It walks `IODeviceTree:/arm-io` children, matches IOMobileFramebuffer, `open AppleCLCD type 0 -> 0x0`, calls IOMFB selectors 57, 15, 22, 29, 33, 79 (GetBlock, out 1580/40), 74 (SupportedFrameInfo), 12, 6, 27, 17, 84, 50; opens IOCoreSurfaceRoot (sel 13); looks for AGXAccelerator / IOAcceleratorES / IOAcceleratorESLegacy (none, repeatedly), `IODeviceTree:/product` (none, repeatedly), `/buttons`, aop-smart-cover, IOPMrootDomain. No swaps yet. **SpringBoard now gets past display discovery** and dies (sig 5) in `-[FBSDisplayMode _initWithWidth:height:preferredScale:scaleOverride:refreshRate:gamut:hdr:rotation:virtual:validityCheck:]+580` <- `_initWithCADisplayMode:scale:rotation:` <- `FBSDisplayConfiguration _initWithImmutableDisplay:` <- `FBSDisplaySource initWithDisplay:` <- `FBSDisplayMonitor` (slide 0x14504000): "FBSDisplayMode cannot be configured as requested". Checks: width/height >= 2, preferredScale > 0, scaleOverride >= 0, refreshRate non-zero, valid gamut/HDR/rotation. Suspects: refresh rate 0 (IOMFB timing blocks answered with zeros) and/or scale 0 (MobileGestalt main-screen-scale; no /product node in the ADT). **Patch 0053**: heap text scan also on SIGTRAP and for SpringBoard (default sweetscan list backboardd,SpringBoard) to read the exact mode description.
- **Image v9 (2026-10-17) = v8 + `/private/var/containers/{Bundle,Data/System,Shared/SystemGroup,Temp}` (root:wheel 0755; on iOS these live on the data volume, absent from the rootfs) + launchd.plist with StandardOutPath/StandardErrorPath renamed to ...PatX (the in-place phone fix; daemons' stdout reaches the log via the 0048 /dev/null tee).** `/run/media/dipa/android/ios-sweet-ipsw/ios16-root-20H392-v9.img` SHA256 aa9f9139...6e67, `.simg` 12,145,303,864 bytes; written through `sudo mount -t hfsplus -o loop,force`. **Patch 0052** (`xnu/patches/0052-sweet-hfs-rename-swap.patch`, CI `39042761826` -> `xnu/out-0052`, QEMU OK, image `shim/out/shim-boot-0052n.img`): vn_rename falls back to VNOP_EXCHANGE when VNOP_RENAMEX(RENAME_SWAP) returns ENOTSUP for two regular files on one volume (HFS exchange swaps data and cnode descriptors = swap semantics). Flashing v9 started 2026-10-17 ~15:00 UTC.
- **backboardd EXCEPTION FOUND (boot 0051n, 14:41 UTC, `logs/boot-0051n.txt`, patch 0051 heap scan):** `*** Terminating app due to uncaught exception 'NSInvalidArgumentException', reason: '*** -[NSURL initFileURLWithPath:]: nil string parameter'`. First-throw stack (slide 0x337f0000): `+[NSURL fileURLWithPath:]` <- `-[BSSystemContainerForCurrentProcessPathProvider libraryPath]` <- `cachesPath` <- backboardd main+0x1d858 (StartWindowServer, shader cache path). BaseBoard: libraryPath = `BSSystemSharedResourcesDirectoryForIdentifier` = `BSSystemSharedDirectoryForIdentifier(id)` + "/Library"; that calls `container_system_path_for_identifier` (libsystem_containermanager -> `_container_legacy_lookup` -> containermanagerd XPC), which returned NULL (BaseBoard logs "Error getting shared system container ..."). With 0050 the sandbox denials are gone. containermanagerd.log.0 in the pulled (pre-0050) image: "Did not receive any persona attributes", "Expected to find at least one persona", notify_register_dispatch failed 9; `/private/var/containers` does NOT exist in the root image (on iOS it lives on the data volume). usermanagerd does create kernel personas 99/199/1000, but fails to save `/private/var/keybags/usersession.kb` and `persona.kb`: "Failed to atomically swap writing file: Operation not supported (45)" (HFS has no RENAME_SWAP). NEXT: create `/private/var/containers` (and check what containermanagerd needs for system group containers), fix or emulate RENAME_SWAP on HFS, then re-check backboardd.
- **Disk housekeeping (2026-10-17):** /home was at 4 GiB free. Moved (not deleted) `xnu/out-0010..0044` and old `shim/out/*.img`/`payload-*.bin` (everything before 0046) to `/run/media/dipa/android/ios-sweet-work/archive/{xnu,shim-out}` (6.3 GiB); /home now 8.7 GiB free. Large work files (phone-root.img 16 GiB, logs, tracev3 dumps) live in `/run/media/dipa/android/ios-sweet-work/`. **Patch 0051** (`xnu/patches/0051-sweet-crash-heap-strings.patch`, CI `39041440935`): on SIGABRT of a process in boot-arg `sweetscan` (default backboardd) the crash log prints heap text runs as `STR ...` (to find the NSException reason). Boot 0051n 14:41 UTC.
- **Boot 0049n (14:11 UTC, `logs/boot-0049n.txt`, patch 0049 trace): backboardd's IOKit lookups before it dies** (each run): `IODeviceTree:/arm-io/pearl-sep` -> none, matching ApplePearlSEPDriver -> none, `IOService:/IOResources/AppleKeyStore` open OK, matching IOHIDDevice (DeviceUsagePairs) / `HIDServiceSupport` notifications, `usb-drd,t8027` notification; **never any IOMobileFramebuffer / display lookup** - the exception happens before display detection. launchd (tracev3) shows backboardd's mach lookups denied with `error = 159: Sandbox restriction`: com.apple.mobilegestalt.xpc, com.apple.iohideventsystem, com.apple.system.notification_center, com.apple.logd, containermanagerd, ACCHWComponentAuthService, oscard. libsystem_sandbox uses `__mac_syscall("Sandbox", op, arg)` (ops: 1 init, 2 check, 3 note, 4 container path, 5/6/7/8 extensions, 0x15 check_bulk, ...); with no policy named "Sandbox" every call gets ENOPOLICY. **Patch 0050** (`xnu/patches/0050-sweet-sandbox-policy.patch`): built-in permissive MAC policy "Sandbox" in sweet_amfi.c (mpo_policy_syscall returns 0, container/extension ops ENOENT; logs first use of each op as `SWEET Sandbox: proc op N`).
- **Phone root pulled (16 GiB, 12 min, `adb exec-out dd if=/dev/block/sda34`) to `/run/media/dipa/android/ios-sweet-work/phone-root.img` (loop-mounted ro at /run/media/dipa/iOSRoot).** No .ips crash reports. logd tracev3 (Persist, LZ4 `bv41` blocks, decompressed strings in `ios-sweet-work/tracev3/all.txt`): /private/var/db/uuidtext is EMPTY so only launchd's own messages are readable. Findings: v8 launch failure = `Service could not initialize: posix_spawn(/usr/libexec/backboardd), error 0xd - Permission denied` (xpcproxy exit 78) - /dev/console not openable for the mobile user; backboardd[98] spawned `com.apple.MTLCompilerService` before SIGABRT; **launchd denies many mach lookups with `error = 159: Sandbox restriction`** (mediaserverd -> com.apple.backboard.display.services, remoted, mobiletimerd -> sleepd, identityservicesd -> apsd) and **tccd exits OS_REASON_SANDBOX "profile not found"** - no Sandbox kext/profiles on SWEET; likely cause of many daemon crash loops (next wall after the display). CF's uncaught-exception text never reaches stderr for daemons (no CF handler; libc++abi prints only the type). **Patch 0049** (`xnu/patches/0049-sweet-iokit-trace.patch`): `IOK name: ...` console lines for registry path lookups, child iterators, matching (with result count/class), matching notifications and IOServiceOpen of the processes in boot-arg `sweetiolog` (default backboardd), max 400 lines.
- **CONFIRMED: launchd StandardOut/ErrorPath=/dev/console stops backboardd from launching.** After the in-place rename (boot 0047n-b, log `logs/boot-0047n-b.txt`) backboardd launches again and dies as on v7: sig 6, `___pthread_kill <- abort <- demangling_terminate_handler <- _objc_terminate <- std::terminate <- _dispatch_client_callout <- _dispatch_once_callout <- main+0x9334` (window-server dispatch_once). It never opened AppleCLCD or IOCoreSurfaceRoot (no `SWEET FB/SURF: backboardd` lines) -> throws before/at display detection. **Patch 0048** (`xnu/patches/0048-sweet-devnull-tee.patch`, CI `39037749668` -> `xnu/out-0048`, image `shim/out/shim-boot-0048n.img`): writes to /dev/null by processes named in boot-arg `sweetnull` (default `backboardd,SpringBoard`) are printed as `|name| text`. Boot 0048n (13:48 UTC, log `logs/boot-0048n.txt`): only `|backboardd| libc++abi: terminating due to uncaught exception of type NSException` (CF's reason line goes to os_log, not stderr, despite CFLOG_FORCE_STDERR/OS_ACTIVITY_DT_MODE). Next: pull the whole 16 GiB root from recovery (`adb exec-out dd if=/dev/block/sda34`) to `/run/media/dipa/android/ios-sweet-work/phone-root.img` and read ReportCrash .ips files / logd tracev3 for the exception reason.
- **Persistent log WORKS (boot 0047n on v8, 13:31 UTC; pulled from OrangeFox: 70 KB, 111 flushes, `/run/media/dipa/android/ios-sweet-work/logs/boot-0047n.txt`).** In 3 min **backboardd was never exec'd** (v7 ran it), ~70 bare `exec xpcproxy`, SpringBoard loops on FBSDisplayMonitor (no display), mediaserverd sig 11 loop, locationd, lockdownd sig 5. Suspect the v8 plist (StandardOut/ErrorPath=/dev/console on backboardd). **In-place plist edit from recovery (no reflash):** launchd.plist is one extent at image/partition byte offset 0x1e025f000 (195 blocks; verified bytes identical on phone vs local v8 mount); renamed StandardOutPath->StandardOutPatX and StandardErrorPath->StandardErrorPatX (same length, both jobs) in blocks 68 and 105 via `adb push` + `dd of=/dev/block/sda34 seek=... conv=notrunc,fsync`, read back VERIFIED. Phone now differs from local v8 image in those 2 blocks (files in `/run/media/dipa/android/ios-sweet-work/`). Test boot 0047n 13:38:42 UTC. Recovery kernel has no hfsplus (raw block edits only).
- **Patch 0047: persistent console log (CI `39036478663` -> `xnu/out-0047`, QEMU no panic; phone image `shim/out/shim-boot-0047n.img` = 0047 + sweet-0046hi.adt + 0045n cmdline).** `_cnputs` (all console output: kernel printf + daemons' /dev/console) also appends to a 4 MiB ring in sweet_ufs.c; after the UFS partition is ready and writable (ufsrw=1) a thread writes new ring blocks + header to the LAST 1025 blocks (4 KiB) of userdata every ~1 s and SYNCHRONIZE CACHE (prints `SWEET LOG: console log to the last 4100 KiB ...`). Read: phone to fastboot -> `fastboot boot recovery/recovery.img` (OrangeFox) -> `tools/sweet-log-pull.sh OUT.txt` (adb dd of that area + `tools/sweet-log.py`). Not yet verified on the phone. Panics after the last flush are not in it (screen still shows them). 2x font attempt reverted: XNU's v_scale only scales the progress icon.
- **Boot 0046n on v8 (13:05 UTC, VID_20261017_150522_HSR_120):** phone prints `SWEET SURF: IOCoreSurfaceRoot registered` and `SWEET FB: AppleCLCD 1080x2400 stride 4320 at 0x9c000000, default surface 1` (nubs + match work on hardware). Visible userspace: early boot tasks, launchd_cache_loader, `exec SpringBoard`; no `exec backboardd` / `SWEET FB: backboardd` line seen in the readable frames (video blurry). Frame 00:47: lower half of the screen shows OLDER console text (16:01:12) below newer lines -> unconfirmed hint that a swap copied an old snapshot of the panel (CA reads the default/displayed surface = panel memory). SpringBoard still dies in `-[FBSDisplayMonitor _initWithBookendObserver:transformer:]+1160` (sig 5, slide 0x30960000). **Shim: 2x console font** (`shim/loader.c` Video.v_depth scale field = 1) so phone videos are readable; image `shim/out/shim-boot-0046b.img` = 0046n + 2x font.
- **Patch 0046: built-in IOSurface + IOMobileFramebuffer (2026-10-17, offline, NOT phone-tested).** `xnu/patches/0046-sweet-iosurface-iomfb.patch`: new `iokit/Kernel/arm/SweetSurface.{h,cpp}` (IOCoreSurfaceRoot + IOSurfaceRootUserClient: sel 13/0/4/34/35, traps 0-5, wired buffers mapped into the caller, per-client refs) and `SweetFramebuffer.cpp` (AppleCLCD : IOMobileFramebuffer on /arm-io/disp0: sizes, swap = CPU copy of enabled layers into the ABL panel memory, 60 Hz vsync via sendAsyncResult64WithOptions(CanDrop), panel memory published as the default IOSurface, kernel text console paused on the first swap only with `sweetfb_noconsole=1` (default keeps the console: pausing switches it to serial and would hide panics)); SweetPlatformExpert creates nubs for /arm-io children (not gic) attached to the platform expert (IOPlatformDevice::compareName casts its provider to IOPlatformExpert, so nubs under the arm-io nub never matched - first CI build showed that in QEMU). mkadt: /arm-io/disp0 {device_type display-subsystem, compatible sweet,display, canvas-width 1080, canvas-height 2400}. CI `39021572169` -> `xnu/out-0046`. **QEMU** (`research/q0046-hi.adt`, RAM disk init, msgbuf): `SWEET SURF: IOCoreSurfaceRoot registered`, `SWEET FB: AppleCLCD 1080x2400 stride 4320 at 0x9c000000, default surface 1`, init alive, no panic. Phone image **`shim/out/shim-boot-0046n.img`** (SHA256 9c4d2fe9..., CI `39024108588` with the review fixes: swap bounds check, zeroed 64-bit async ref, port release, 1.5 GiB wired budget, ref leak/race fixes; QEMU re-checked) = 0046 kernel + `shim/out/sweet-0046hi.adt` + the 0045n cmdline; boot it on image v8 (backboardd/SpringBoard log to console) and film. Expected new console lines: `SWEET SURF: <proc> create id ...`, `SWEET FB: backboardd sel N ...`, `SWEET FB: swap ...`. Open question for the next wall: QuartzCore's IOMFB server builds its renderer from Metal first (`CAMetalContextCreate`), GLES only when enabled; how the CPU renderer gets selected on a device without a GPU is not yet established.
- **IOSurface values ABI (offline RE, `/run/media/dipa/android/ios-sweet-work/iosurface-values.md`) - TODO in SweetSurface (0046 only accepts and drops values, so IOSurfaceCopyValue always returns NULL):** sel 9 SetValue in = {u32 id, u32 pad, IOCFSerialize([value, key]) or ([dict] = merge)}, sel 0xb RemoveValue in = {u32 id, u32 pad, key\0} (empty key = remove all); both out 4 bytes = new generation, client accepts only (gen+2)|1. Kernel after each change: gen = (gen+2)|1 (odd, never 0) -> state+0x20, serialized size (>=1) -> state+0x28. sel 0xa in {u32 id, u32 0, u8 0}, out {u32 ?, u32 gen, serialized non-bulk dict}; too small -> 0xe00002db + needed size (client retries x16). sel 0x1b in 0x88: bulk struct 0x70 @0, u64 valid size @0x70, u64 changed mask @0x78, u32 id @0x80 -> copy masked fields into state+0x30 (bit0 32B @0, bit1 16B @0x20, bit2 8B @0x30, bits3-10 1B @0x38..0x3f, bits11-14 24B @0x40/4B @0x58/8B @0x5c/1B @0x64).
- **IOMFB display matching confirmed (offline RE, `/run/media/dipa/android/ios-sweet-work/iomfb-match.md`):** the framework never matches on "AppleCLCD"; any registered IOMobileFramebuffer subclass works. `iomfb_match_callback` reads only `dfr`/`AppleTV`/`external` (CFBoolean, service itself, no parents); internal main display = all absent/false (type 0, name "primary"). Display count = /arm-io children whose device_type starts with "display-subsystem" (count 0 -> CreateDisplayList returns NULL; more nodes than services -> 30 s wait). IOMobileFramebufferOpen fails only on IOServiceOpen failure; canvas-width/height read as 4-byte CFData from the IOService-plane parent (optional). CopyProperty = IORegistryEntrySearchCFProperty on the service only. Patch 0046 (disp0 node, `external`=false, AppleCLCD : IOMobileFramebuffer) fits all of this.
- **IOMobileFramebuffer swap + notification ABI (2026-10-17, offline).** Swap record = fb+0x18, **0x46c bytes**, zeroed by SwapBegin and sent whole by `kern_SwapEnd` = IOConnectCallStructMethod(sel 5, rec, 0x46c, no output). **SwapBegin** = sel 4, no input, 1 scalar out = swap id (stored at rec+0x50). `kern_SwapSetLayer(fb, layer<4, IOSurfaceRef, CGRect a, CGRect b, flags)`: rec+0x54+4*layer = **IOSurface id**; rec+0x104 bit layer = layer enabled (rec+0x108 bit cleared); rect a (int x,y,w,h, scaled) at rec+0x64+16*layer, rect b at rec+0xc4+16*layer; rec+0x114+4*layer = transform from flags&0xf; flag bits 5/31/6/7/8/9 set bytes rec+0x2ca/0x2cc/0x2e3/0x2e5/0x2e6/0x467+layer. A NULL surface only sets the enable bit with zero id. SwapWait = sel 6 scalars [swap id, timeout, 0].
  - **Notifications**: `IOMobileFramebufferScheduleWithDispatchQueue`/`GetRunLoopSource` create an IONotificationPort and `IOConnectSetNotificationPort(conn, type, port, 0)` (kernel `registerNotificationPort`). `kern_EnableNotifications(fb, type, cb, ctx)` = **sel 72 scalars [NotifyCallbackFuncs[type], fb, type, 0]** -> the kernel keeps an async reference {port, func, refcon=fb} per type and fires it with `sendAsyncResult64`. Types: 0 HotPlug, 1 Power, 3 FrameInfo (args[0] = mask of 31-bit keys, args[1] u32, then values), 4 CRC, **5 Vsync**: 6 args, each a 32-bit half in a u64 slot: (ts lo, ts hi), (t2 lo, hi) passed through the time-ingest helper, (t3 lo, hi); the framework then calls the client's vsync callback(fb, ts, t2', t3, ctx). Vsync callbacks are dropped while the notification port has queued messages.

- **IOSurface.framework user-client ABI reversed (2026-10-17, offline, step 2 of the GUI plan).** `ipsw dyld disass research/dsc/dyld_shared_cache_arm64 --image IOSurface` (selector scan script: scratchpad `selscan.py`).
  - Connect (`_iosConnectInitalize`, pthread_once): `IOServiceGetMatchingService(IOServiceNameMatching("IOCoreSurfaceRoot"))`, retried with sleep(1) up to 241 times (a missing service stalls every client ~4 min, no crash); `IOServiceOpen(type 0)`; then **sel 13** (no inputs, struct out 0x18 bytes = `_ioSurfaceLimits`) and requires u16 at out+0x16 == 1 (ABI version); out+0 = offset/plane-base alignment mask, out+4 = bytes-per-row alignment mask (used as (x+m)&~m), +0..+0x14 also copied into IOSurface remote-method init, otherwise logs and closes the connection.
  - **Create = sel 0**: scalar in [parent id or 0], struct in = `IOCFSerialize(props, kIOCFSerializeToBinary)` (OSUnserializeBinary format; keys IOSurfaceWidth/Height/BytesPerRow/BytesPerElement/PixelFormat/AllocSize/PlaneInfo/CacheMode/IsGlobal/...), struct out **0xa68 bytes** ("lock result"). **Lookup = sel 4**: scalar in [id], same struct out. LookupFromMachPort = sel 0x22 (scalar in [port name]); CreateMachPort = sel 0x23 (scalar in [id, options], scalar out [port name]).
  - Lock result (copied to client+0x68, size 0x68 + planes*0x80, min 0xe8): 0x00 u64 base address **mapped in the caller's task**; 0x08 u64 task VA of a per-surface shared state page (+0 u64 registry id, +8/+0xa u16 bound accel ids, +0xc u32 seed, +0x14 u8 tiled, +0x20 u32 value generation, +0x24 YCbCr matrix, +0x28 u32 non-bulk values size, +0x2d u8 displayable, +0x2e u8 sysmem-only, +0x30 0x70 bytes bulk attachments); 0x10 u64 task VA of timestamp page (4 u64 indexed timestamps, +0x20 detach code); 0x18 u32 surface id; 0x1c u32 alloc size; 0x20 width; 0x24 height; 0x28 bytes per row; 0x2c buffer offset; 0x30 pixel format; 0x34 u32 (dcache flush length); 0x38 plane count; 0x40 u16 bytes/element; 0x42 u8 element width; 0x43 u8 element height; 0x44 cache mode; 0x4c u8 subsampling; 0x4d u8 prohibit-use count; 0x50 parent id; 0x54 slice count; 0x58 u64 trace id; 0x60 u64 protection options; planes[n] at 0x68+n*0x80 (+0 width, +4 height, +8 bytes/row, +0xc offset, +0x14 size, +0x18 u16 bpe, +0x1a/+0x1b u8 element w/h, +0x1c u8 component count, +0x1d address format, +0x54 u8 compression type (non-zero -> no CPU base address)).
  - **Traps** (IOConnectTrapN -> `IOUserClient::getTargetAndTrapForIndex`): 0 IncrementUseCount(id, category), 1 DecrementUseCount(id, category), 2 **Lock**(id, options, &seed_out), 3 **Unlock**(id, options, &seed_out), 4 Release(id), 5 Release after a failed create/lookup setup (id).
  - Other selectors: 5 SetYCbCrMatrix, 6 WrapClientImage, 7 WrapClientMemory, 9 SetValue (serialized), 0xa UpdateNonBulkValueCache (struct in 9), 0xb RemoveValue, 0xc BindAccel, 0x11 SetSurfaceNotify (async), 0x12 RemoveSurfaceNotify, 0x13 Log, 0x14 SetPurgeable, 0x15 SetOwnership, 0x16 SetTiled, 0x18 SetTimestamp, 0x19 GetTileFormat, 0x1a CopyDataValueBytes, 0x1b SetBulkAttachments (struct 0x88), 0x1e/0x2e-0x31 (via outlined helpers), 0x1f SetCompressedTileDataRegionMemoryUsed, 0x20 GetGraphicsCommPageAddress, 0x21 SetIndexedTimestamp, 0x24-0x26/0x2d IOSurfaceAccelerator, 0x27-0x29 shared events, 0x2a SignalEvent, 0x2b Set/InitDetachModeCode, 0x2c SetOwnershipIdentity, 0x32-0x34 data properties.
  - => SWEET kernel service: class/name **IOCoreSurfaceRoot** + IOSurfaceRootUserClient; minimum = sel 13, 0, 4, 9/0xa/0xb (values), traps 0-5, 0x22/0x23 later for cross-process surfaces (CA sends surfaces to backboardd by mach port). Memory: IOBufferMemoryDescriptor per surface, mapped into each task with createMappingInTask; ids from a global table.

- **Image v8: daemon logs on the console.** `tools/patch-launchd-plist.py IN OUT LABEL...` rewrites launchd.plist (binary, plistlib): renames every `_PanicOnCrash` to `_PanicOnCrasX` and gives the listed jobs `StandardOutPath`/`StandardErrorPath` `/dev/console` (devfs console is 0622, writable by mobile) plus `OS_ACTIVITY_DT_MODE=YES` (os_log mirrored to stderr) and `CFLOG_FORCE_STDERR=1`. v8 = v7 + that plist for com.apple.backboardd and com.apple.SpringBoard, written through the mount (796310 bytes, root:80 0644), SHA256 in `ios16-root-20H392-v8.img.sha256`, `.simg` ready; boot with `shim/out/shim-boot-0045n.img`. Goal: see backboardd's uncaught exception text and the window server's display probing. ODDITY (unresolved): the v5 raw-image rename at offset 0x1e025f000 is present in the v5-v7 image bytes, but the plist read through a Linux mount of v7 still had the original `_PanicOnCrash` keys; filefrag shows the file as one extent. Never patch file contents through raw offsets again; write through the mount.
- **IOMobileFramebuffer.framework reversed (step 1 of the GUI plan).** `ipsw dyld disass research/dsc/dyld_shared_cache_arm64 --image IOMobileFramebuffer`.
  - Display discovery (`IOMobileFramebufferCreateDisplayList` -> `getDisplayListNumber`): iterates children of `IODeviceTree:/arm-io`, counts nodes whose `device_type` is "display-subsystem" or "ext-display-subsystem" (or `compatible` "paravirtualizedgraphics,gpu"); then `iomfb_populate_display_infos` adds an IOServiceAddMatchingNotification(IOServiceFirstMatch, **IOServiceMatching("IOMobileFramebuffer")**) and runs the run loop up to 30 s until that many services matched. `iomfb_match_callback` reads service properties `dfr`, `AppleTV`, `external` (CFBoolean) and names displays (external ones "%s-%d").
  - `IOMobileFramebufferOpen(service, task, type, &fb)`: `__ioMobileFramebufferAlloc` (CFPreferences com.apple.iokit.IOMobileGraphicsFamily: canvas_width/height, override_display_width/height, override_panel_width/height, benchmark, enable_ktrace), `IOServiceOpen(service, task, type)` (GetMainDisplay passes type 0), `getEDTProperty` reads `canvas-width`/`canvas-height` from the service's IODeviceTree parent, then fills a table of `kern_*` function pointers.
  - User client selectors (`kern_*` -> IOConnectCall*): 3 _kern_GetLayerDefaultSurface[ScalarMethod_stub]; 4 _kern_SwapBegin[ScalarMethod_stub]; 5 _kern_SwapEnd[StructMethod_stub]; 6 _kern_SwapWaitWithTimeout[ScalarMethod_stub] _kern_SwapWait[ScalarMethod_stub]; 7 _kern_GetID[ScalarMethod_stub]; 8 _kern_GetDisplaySize[ScalarMethod_stub]; 10 _kern_SetTVOutMode[ScalarMethod_stub]; 11 _kern_SetWSSInfo[ScalarMethod_stub]; 12 _kern_RequestPowerChange[ScalarMethod_stub]; 13 _kern_EnableStatistics[ScalarMethod_stub]; 14 _kern_SetFlags[ScalarMethod_stub]; 15 _kern_SetDebugFlags[ScalarMethod_stub]; 16 _kern_SetTVOutSignalType[ScalarMethod_stub]; 17 _kern_SetGammaTable[StructMethod_stub]; 18 _IOMobileFramebufferIsMainDisplay[ScalarMethod_stub]; 19 _kern_SetWhiteOnBlackMode[ScalarMethod_stub]; 20 _kern_SwapSignal[ScalarMethod_stub]; 22 _kern_SetDisplayDevice[ScalarMethod_stub]; 23 _kern_SetDigitalOutMode[ScalarMethod_stub]; 24 _kern_GetHDCPDownstreamState[StructMethod_stub]; 25 _kern_GetDigitalOutState[ScalarMethod_stub]; 26 _kern_printDisplayRegs[ScalarMethod_stub]; 27 _kern_GetGammaTable[StructMethod_stub]; 28 _kern_GetDotPitch[ScalarMethod_stub]; 29 _kern_GetDisplayArea[StructMethod_stub]; 30 _kern_EnableDisableDithering[ScalarMethod_stub]; 31 _kern_SetUnderrunColor[ScalarMethod_stub]; 33 _kern_EnableDisableVideoPowerSavings[ScalarMethod_stub]; 34 _kern_SetVideoDACGain[ScalarMethod_stub]; 35 _kern_SetLine21Data[ScalarMethod_stub]; 41 _IOMobileFramebufferEnableMirroring[ScalarMethod_stub]; 42 _IOMobileFrameBufferGetMirroringCapability[StructMethod_stub]; 43 _IOMobileFramebufferSetRenderingAngle[StructMethod_stub]; 44 _IOMobileFramebufferSetMirrorContentRegion[StructMethod_stub]; 47 _kern_HDCPSendRequest[ScalarMethod_stub]; 48 _kern_HDCPGetReply[ScalarMethod_stub]; 49 _kern_SurfaceIsReplaceable[ScalarMethod_stub]; 50 _kern_SetBrightnessCorrection[ScalarMethod_stub]; 51 _kern_SetColorRemapMode[ScalarMethod_stub]; 52 _kern_SwapCancel[ScalarMethod_stub]; 53 _IOMobileFramebufferSetCanvasSize[ScalarMethod_stub]; 54 _IOMobileFramebufferGetMatrix[Method_stub]; 55 _IOMobileFramebufferSetMatrix[Method_stub]; 56 _IOMobileFramebufferKernelTests[StructMethod_stub]; 57 _kern_GetColorRemapMode[ScalarMethod_stub]; 66 _kern_SPLCSetBrightness[ScalarMethod_stub]; 67 _kern_SPLCGetBrightness[ScalarMethod_stub]; 68 _kern_SetParameter[Method_stub]; 69 _kern_SwapSetColorMatrix[StructMethod_stub]; 72 _kern_DisableNotifications[ScalarMethod_stub] _kern_EnableNotifications[ScalarMethod_stub]; 73 _kern_ChangeFrameInfo[ScalarMethod_stub]; 74 _kern_SupportedFrameInfo[Method_stub]; 75 _kern_FactoryPortal[StructMethod_stub]; 77 _kern_GetCurrentAbsoluteTime[ScalarMethod_stub]; 78 _kern_SetBlock[Method_stub]; 79 _kern_GetBlock[Method_stub]; 80 _kern_GetLinkQuality[ScalarMethod_stub]; 81 _kern_SwapCancelAll[ScalarMethod_stub]; 82 _kern_GetProtectionOptions[ScalarMethod_stub]; 83 _kern_CopyLayerDisplayedSurface[ScalarMethod_stub]; 84 _kern_SetContrast[StructMethod_stub]; 85 _kern_GetBufBlock[Method_stub]; 86 _IOMFBGainEncoderFinishEncoding[Method_stub]; UNKNOWN {}
  - => Kernel needs: an `/arm-io` child node with device_type "display-subsystem" (+ canvas-width/height) and an IOService of class **IOMobileFramebuffer** (subclass name match) attached under it, opened with type 0, implementing at least GetDisplaySize(8), IsMainDisplay(18), GetID(7), GetLayerDefaultSurface(3), SwapBegin(4)/SwapEnd(5 struct)/SwapWait(6), Enable/DisableNotifications(72), frame info(73/74), CopyLayerDisplayedSurface(83), plus IOSurface for the swapped buffers. NEXT: CoreAnimation's AppleDisplay/IOMFBDisplay usage of these (constructor, first swap, vsync), then IOSurface.framework's IOSurfaceRoot selectors.
- **GUI PLAN (decided 2026-10-16): CPU rendering + kernel IOMobileFramebuffer.** User: CPU rendering is fine for now (goal = prove the GUI runs), may later research an Apple-style GPU driver for the Adreno 618 -> keep display output and renderer separable. QuartzCore findings (symbols from the cache's .symbols subcache: `ipsw dyld symaddr research/dsc/dyld_shared_cache_arm64 --image QuartzCore`; strings: `ipsw dyld macho ... QuartzCore --strings`):
  - Renderers: `sw_new_context` (CA::OGL::SWContext, full CPU renderer incl. blur, images, IOSurface-backed surfaces, make_buffer_current(WindowServer::Surface)), `gles_new_context`, `metal_new_context`, `new_null_context`. Env knobs include CA_ENABLE_OGL, CA_DISPLAY_TYPE, CA_FORCE_LOCAL_SERVER, CA_FORCE_TIMER, CA_CANVAS_WIDTH/HEIGHT, CA_MAIN_DISPLAY_SCALE/ORIENTATION, CA_DISABLE_RENDER, CA_ALWAYS_RENDER (selection logic not traced yet).
  - `-[CAWindowServer _detectDisplays]` (0x1884e3150) iterates static tables `open_funcs`/`count_funcs` = AppleCLCDDisplay, AppleRGBOutDisplay, AppleWirelessDisplay only; each `count()` = `display_count_of_type(IOMFBDisplayType)` over **`IOMobileFramebufferCreateDisplayList()`**, `open(i)` = `create_display_of_type` -> `CA::WindowServer::AppleDisplay(CFString name, ..., IOMFBDisplayType)` (an IOMFBDisplay driven by IOMFBServer: vsync_callback, frame_info_callback, swaps). `CA::WindowServer::VirtualDisplay` exists but is never auto-detected (explicit API only). => the main display MUST come from an IOMobileFramebuffer kernel service.
  - NEXT: reverse IOMobileFramebuffer.framework: IOMobileFramebufferCreateDisplayList / IOMobileFramebufferOpen (service matching, display type property), the user-client selectors used by IOMFBServer (display size/modes, swap begin/set-layer/end with IOSurface ids, vsync notifications), then the IOSurface kernel side (IOSurfaceRoot user client: create/lookup/lock, memory mapping) that both IOMFB swaps and CA surfaces need. Kernel: SWEET IOMobileFramebuffer service that copies the swapped IOSurface to the 1080x2400 framebuffer at 0x9c000000.
- **logd fixed; backboardd = DISPLAY WALL (boot 0045n on v7, 21:08:57 UTC, frames VID_20261016_230850 00:55-01:28, slide 0x4c3ec000, `research/crash-0045n-v7.txt`).** No `CRASH logd` any more; new daemons run (runningboardd, WirelessRadioManagerd, networkserviceproxy, dasd, cloudpaird, magicswitchd, atc, askpermissiond, ManagedSettingsAgent, containermanagerd uses AKS 0/17). New: locationd sig 11 at CFRunLoopAddSource+132 (NULL source) from its main+0x2af138 in a dispatch_once. **backboardd** sig 6: abort <- std::terminate <- _objc_terminate <- dispatch_once <- main+0x9334 = the "start %{public}@..." wrapper (0x100009264) for **"window server"** (block 0x10000a770 -> dispatch_once body 0x10001d7f0 = StartWindowServer): MTLSetShaderCachePath, `[CAWindowServer serverWithOptions:@{kCAWindowServerDisableUpdatesOnMainDisplay: @YES}]`, `setRendererFlags:`, **`mainDisplay`** -> `displayId` -> `[CADisplay displayWithDisplayId:]`, setWindowServerDisplay:, bounds, pointScale, currentMode, preferredScale, nativeOrientation, touchScales... With no display, mainDisplay is nil and it throws. (HID system and GS start fine before it.) SpringBoard dies on the same missing display (FBSDisplayMonitor). QuartzCore display backends (from the cache's .symbols subcache, `ipsw dyld str`): `CA::WindowServer::AppleCLCDDisplay` (IOMobileFramebuffer hardware) and **`CA::WindowServer::VirtualDisplay`** (open(), new_server, clone_update). `CA_DISPLAY_TYPE` and `CA_FORCE_LOCAL_SERVER` strings exist in QuartzCore but have no direct xref (likely read via a table of CA_* env names). NEXT: find how CA::WindowServer::Server enumerates displays (IOMobileFramebuffer services vs VirtualDisplay / CA_DISPLAY_TYPE) and choose: (a) kernel IOMobileFramebuffer service + IOSurface, or (b) a VirtualDisplay rendered by CA's CPU path (CA::OGL::SW) and copied to the framebuffer.
- **watchdogd + CommCenter + identityservicesd fixed (boot 0045n on v6, 20:41:30 UTC, frames VID_20261016_224133 00:39-01:13, slide 0x20fec000).** `SWEET WDT: watchdogd sel 0 in 0/0 out 1/0 -> ok`, no watchdogd/CommCenter/identityservicesd crashes (CommCenter/identityservicesd now call AKS 0/17/7). usermanagerd boot task runs (UMD_INIT creates primary session uid 501 "mobile", personas 99 System and 199 Default registered with the kernel); `Failed to atomically swap writing file /private/var//keybags/usersession.kb: Operation not supported (45)` (HFS lacks the atomic swap rename; non-fatal so far). Remaining: SpringBoard (FBSDisplayMonitor, no display), backboardd (uncaught exception), mediaserverd, lockdownd (crash inside obfuscated code, main+0x4c1b4?), logd. **logd**: main+0x18160 = `BUG IN LIBTRACE: Failed to migrate directory` from 0x10001801c(dir 0): 0x100016538 requires the dir to exist with uid/gid/mode from the table at 0x10002df14 (0: /private/var/db/diagnostics 272:272 0751, 1: /private/var/db/uuidtext 272:272 0755, 2: /private/var/db/diagnostics/timesync 272:272 0755) plus a `.migrated` file, else XPC "operation 6" to a helper and crash. **Image v7** = v6 + those dirs/markers (also in mkiosroot.sh), SHA256 in `ios16-root-20H392-v7.img.sha256`, `.simg` ready. Boot with `shim/out/shim-boot-0045n.img`.
- **Exit-reason text works (boot 0044n, frames VID_20261016_221726 00:32-00:53, slide 0x22d5c000).** CommCenter/identityservicesd: `couldn't dlopen libobjc-trampolines.dylib: dlopen(/usr/lib/libobjc-trampolines.dylib, 0x0106): tried: '/usr/lib/libobjc-trampolines.dylib' (no such file, not in dyld cache)`: the file only exists in the OS cryptex and dyld does not search cryptexes at ignition_level=1. 19 cryptex files have no root counterpart (OS: DriverKit dyld cache + .symbols, VisualTestKit, libffi-trampolines, libobjc-trampolines, libstdc++ 6.0.9 + 2 symlinks; App: SafariSupport.bundle, AuthenticationServicesAgent, com.apple.Safari.History, passwordbreachd, safarifetcherd, webbookmarksd, webinspectord, webpushd) -> hard-linked (symlinks copied: Linux hfsplus cannot hard-link symlinks); `tools/mkiosroot.sh` does it generically. watchdogd still crashed: the `-wdt_no_pmu` / `/arm-io/wdt no-pmu` check (0x10000a01c) only runs when os_variant_has_internal_diagnostics; otherwise 0x100009b18 looks up IOServiceMatching("IOService") + IOPropertyMatch {IOPMUPrimary: 1} (NSConstantDictionary) and crashes if absent, then IORegistryEntrySetCFProperty(boot stage 0xff) (failure only logged). Patch **0045**: IOWatchdog sets IOPMUPrimary = 1 and accepts setProperties; CI `38961814194` -> `xnu/out-0045`. **Image v6** = v5 + the 19 cryptex links, SHA256 7c610c6a...297a, `ios16-root-20H392-v6.simg` (full flash needed). Boot image `shim/out/shim-boot-0045n.img` (0045 + sweet-0044hi.adt). Still open: SpringBoard (no display), backboardd (uncaught exception, reason none), mediaserverd (Fig NULL dict), logd (main+0x18160), lockdownd (main+0x4c1b4).
- **System survives daemon crashes (boot 0043p, video frames VID_20261016_215006 00:25/00:42/00:54).** With the v5 plist patch no panic: launchd keeps launching (runningboardd, powerd, tccd, healthd, peopled, cryptexd, routined, aksd, mediaremoted, familynotificationd, nanoprefsyncd, navd, cloudpaird, passd, amsaccountsd, accessoryupdaterd, wcd ...). Shared cache slide that boot 0x406b4000 (`research/crash-0043p.txt`). watchdogd still crashed at main+0xb82c from the PMU step: `-wdt_no_pmu` is only honored when os_variant_has_internal_diagnostics; customer builds read `no-pmu` from IODeviceTree:/arm-io/wdt (IORegistryEntrySearchCFProperty) -> mkadt adds /arm-io/wdt {no-pmu}. **cryptexd** sig 6: abort_with_payload <- os_crash_fmt <- `__darwin_arm_secure_boot_chip.cold.7` ("panic: failed to get crypto algorithm") <- `_img4_chip_select_personalized_ap`: reads /chosen use-ddi-secure-boot, allow-ecid-mismatch, crypto-hash-method ("sha1" | "sha2-384"), /chosen/asmb lp-smb0/1 -> mkadt adds crypto-hash-method "sha2-384". **identityservicesd** sig 6 = same `_objc_fatal` in imp_implementationWithBlock as CommCenter (block trampolines; message needed). lockdownd sig 5 at main+0x4c1b4. Patch **0044**: crash report adds `CRASH name reason ns N code X: text` (exit reason description = abort_with_payload text). CI `38959096811` -> `xnu/out-0044`, QEMU OK. Image `shim/out/shim-boot-0044n.img` = 0044 + `shim/out/sweet-0044hi.adt` (wdt no-pmu, crypto-hash-method) + cmdline (wdt=-1, no -wdt_no_pmu).
- **First symbolicated daemon crashes (boot 0043u on journaled v4, 19:38:53 UTC; video frame chat image 36; transcribed in `research/crash-0043u.txt`; shared cache slide 0x40d60000 for every process).** (Gemini's video transcription invented values, use frames only.) `tools/sweet-crash.py research/dsc/dyld_shared_cache_arm64 x research/crash-0043u.txt`:
  - **watchdogd** pc main+0xb82c (`_os_crash` trap helper) from 0x100009bb0 = set PMU boot stage via IORegistry (no PMU) -> fix: boot-args `wdt=-1 -wdt_no_pmu` (Apple's own test mode writes "%s wdt=-1 -wdt_no_pmu"; watchdogd logs "skipping setting PMU boot stage because -wdt_no_pmu found in boot-args or no-pmu found in EDT"). Kernel effect of wdt=-1: sched hygiene debug off, zone_exhausted_timeout -1 (harmless).
  - **SpringBoard** sig 5 in `-[FBSDisplayMonitor _initWithBookendObserver:transformer:]+1160` <- `+[FBDisplayManager sharedInstance]` <- `FBSystemShellInitialize` <- `SBSystemAppMain`: no main display. GUI WALL: display service (IOMobileFramebuffer / CoreAnimation display).
  - **backboardd** sig 6: abort <- std::terminate <- _objc_terminate (uncaught exception; message not shown).
  - **CommCenter** sig 6: `_objc_fatal` in `imp_implementationWithBlock` (from `+[_PFRoutines initialize]`, CoreData): objc block trampolines cannot be allocated -> likely kernel gap (trampoline page remap); will hit many processes.
  - **mediaserverd** sig 11: CFDictionaryGetValue(NULL) in FigNotificationListenerStartNotifications (routing session manager).
  - **logd** sig 5 at its main+0x18160 (bt main+0x9dfc); lockdownd sig 5 (not yet symbolicated).
  - Image v5 = v4 + launchd.plist (`/System/Library/xpc/launchd.plist`, contiguous at image offset 0x1e025f000, 1456980 bytes) with the 7 `_PanicOnCrash` keys renamed `_PanicOnCrasX` in place (raw image patch, no metadata change) -> `diff-v4-v5.simg` = 7 blocks; v5 SHA256 78f96974...185a. Boot image `shim/out/shim-boot-0043p.img` = 0043 + `wdt=-1 -wdt_no_pmu`.
- **0043 console crash reports (QEMU verified); image `shim/out/shim-boot-0043u.img`; NEXT: flash v4 + boot 0043u.** Boot 0042u (video photo IMG_20261016_211646): short exec lines work; watchdogd crashes with NO `SWEET WDT:` line (fails before its first IOWatchdog call: lookup or IOServiceOpen, or elsewhere); lockdownd, SpringBoard, mediaserverd, CommCenter, identityservicesd, backboardd, watchdogd all crash repeatedly ("Too many corpses being created" = corpse limit, no crash reports). AKS callers: lockdownd (0, 7), imagent/sleepd (0, 17, 7), backboardd (0, 17), UserEventAgent (0, 7, 102, 103 -> unsupported). Patch **0043**: `sweet_crash_log` (osfmk/kern/exception.c, called from proc_prepareexit for crash exits on the current proc) prints `CRASH name[pid] sig S code (exc<<32|code) sub X slide SR mh MH`, `CRASH name pc lr sp fp far`, `CRASH name bt` (10 fp frames); AKS sel 103 notify_state_is_set -> 0. CI `38954520419` -> `xnu/out-0043`. QEMU test: init forks, child `brk #1`: `CRASH launchd[2] sig 5 code 0x600000001 sub 0x1000004f0 slide 0x0`, pc correct, parent keeps running (pid 1 crashes panic before proc_prepareexit, so test with a child). `tools/sweet-crash.py CACHE ROOT [report]` symbolicates typed-in CRASH lines (`ipsw dyld a2s` with addr - slide; main binary offsets = addr - mh + 0x100000000).
- **v3 reflashed 18:50-18:55 UTC (`research/flash-userdata-v3b.log`, 24/24 OKAY); 0042u booted 19:01:17 UTC on the clean disk (`research/boot0042u-2.log`), SLOW MO VID pending (watchdogd failure reason).** ABL hangs on the first `fastboot boot` after every big flash; user re-enters fastboot. User gave standing approval to reflash/boot without asking (memory standing-flash-approval). Commits now end with `Co-Authored-By: dipa-studio <183933817+dipa-studio@users.noreply.github.com>` (user request; never a Claude trailer).
  - watchdogd flow: connect_to_kext (0x100005408): IOServiceGetMatchingService("IOWatchdog") fails -> "failed to discover" + _os_crash; IOServiceOpen(type 1) fails -> "IOServiceOpen failed with error : 0x%x" + _os_crash. Then sel 0 (out 1 scalar): 0 -> "userspace monitoring disabled", 0x100009bb0, sleep 2, exit(0); nonzero -> "userspace monitoring enabled", normal operation.
  - **Image v4 (JOURNALED):** `tools/mkiosroot.sh` now does `mkfs.hfsplus -J`, mounts with `-o loop,force`, adds the /System/Library/Caches/com.apple.dyld hard links (44) and /private/var/keybags/systembag.kb (0700/0600 root). Built from the IPSW dmgs (OS 087-86698-044, SystemOS 087-86593-047, AppOS 087-86715-047) -> `/run/media/dipa/android/ios-sweet-ipsw/ios16-root-20H392-v4.img` SHA256 fba4aeae...ec0f9 + `.simg`. Header: attr 0x80002100 (unmounted, journaled), journal info block 129, journal 8 MiB at 532480 flagged need-init. Linux `fsck.hfsplus -n` reports "Volume header needs minor repair" on any fresh journaled volume even after -y (quirk). QEMU: 64 MB journaled test root (`rd=md0 rootrw=1`, 0042 kernel): `hfs: Initializing the journal (joffset 0x3000 sz 0x80000)` -> mounted -> init runs, no panic. NEXT: flash v4 (full), boot 0042u.
- **SPRINGBOARD STARTS (boot 0041u 18:39:53 UTC, photo IMG_20261016_204142).** backboardd, bluetoothd, fairplayd.H2, SpringBoard (pid 111, crashed: "Too many corpses"), xpcproxy run; `SWEET AKS: backboardd sel 0 -> 0`, `sel 17 in 1/0 out 0/4096 -> 0`. Panic: watchdogd[113] exited, namespace 2 subcode 5 (SIGTRAP = its _os_crash): watchdogd crashes on purpose when IOServiceGetMatchingService("IOWatchdog") fails ("connect_to_kext() unexpectedly failed") or IOServiceOpen(type 1) fails ("IOServiceOpen failed with error"); IOWatchdog DOES register (QEMU msgbuf: `SWEET WDT: IOWatchdog registered`), cause not on screen yet -> need video. Note `wdt_test=no_user_checkin` makes watchdogd log "detected testing boot-args to not run, exiting" (path not reached here?). **HFS CORRUPTION:** `hfs_swap_BTNode: offset #0 invalid (0xBC1D) (blockSize 0x2000 numRecords 81)`, `node=19677 fileID=4` (catalog), `hfs_mark_inconsistent` repeated. Likely cause: non-journaled HFS + unclean panics while writing (0038e wrote a 2.7 GB core then panicked; every boot since ends in panic). Plan: reflash v3 (clean), longer term HFS journaling. Patch **0042**: one short `exec NAME` console line instead of the 3-line "Allowing spawn of iOS binary" notice; CI `38949451276` -> `xnu/out-0042`; image `shim/out/shim-boot-0042u.img` (same cmdline as 0041u).
- **LAUNCHD DAEMONS RUN (boot 0039u, 18:29:13 UTC, photo IMG_20261016_203052).** With `launchd_unsecure_cache=1` all boot tasks pass and launchd starts LaunchDaemons (pids > 100: identityservicesd 114 and lockdownd 106 crash, "Too many corpses being created"). Fatal: `watchdogd: ERROR: failed to discover watchdog KEXT service` -> exit (namespace 2 = signal, subcode 5) -> `_PanicOnCrash` panic. watchdogd: IOServiceMatching("IOWatchdog"), IOServiceOpen type 1, IOConnectCallScalarMethod sel 0/1 (check in)/3/4 (disable/enable userspace monitoring)/5, IOConnectCallStructMethod sel 2 (1 KiB in) and 9 (small out); VM path ("detected virtual machine environment and no watchdog KEXT found, exiting...") only when it detects a VM. Console also flooded with `hfs: set hardlink bit on vol=iOSRoot cnid=321318` (Linux-made hard links lack kHFSHasLinkChainBit; repaired only in memory on each lookup). Patches **0040** (`iokit/Kernel/arm/SweetWatchdog.cpp`: IOWatchdog + IOWatchdogUserClient, all selectors succeed with zeroed outputs, each logged once as `SWEET WDT: ...`) and **0041** (sweet_hfs hfs_catalog.c: the three link-repair printfs limited to 8 per boot); CI `38948245372` PASSED -> `xnu/out-0041` (QEMU regression OK). Image `shim/out/shim-boot-0041u.img` (0041 + sweet-0038hi.adt + ufsrw rootrw + launchd_unsecure_cache=1) booted 18:39:53 UTC (`research/boot0041u.log`), photo pending.
- **v3 FLASHED + keybag passes; next wall AMFI (launchd_cache_loader).** User said "flash, nothing to lose": `fastboot -S 512M flash userdata ios16-root-20H392-v3.simg` 17:51:52-17:57:01 UTC, 24/24 chunks OKAY (`research/flash-userdata-v3.log`). userdata = v3 (SHA256 fa84274c...22df) until the next rw boot. ABL fastboot then hung ("Sending" forever, `reboot bootloader` timed out) until the user re-entered fastboot by hand (common Xiaomi ABL crash). Boot 0038e 18:06:15 UTC (`research/boot0038e-3.log`), photos IMG_20261016_200812/200838: keybag OK, then boot tasks tzinit (`Bad zoneinfo link`, resets to system partition, OK), finish-restore, finish-demo-restore, sysstatuscheck, prng_seedctl (`failed to load kernel prng seed`, `failed to load virtual random (-147)`, AllowCrash), auearlyboot, then **launchd_cache_loader: `abort: Unable to find AMFI`** -> 2.7 GB core written to /cores in 91 s -> launchd panic `launchd_cache_loader[22] exited -- exit reason namespace 7 subcode 0x1`. launchd_cache_loader: SecStaticCodeCheckValidity of /System/Library/xpc/launchd.plist with detached .sig, then IOServiceMatching("AppleMobileFileIntegrity") + IOServiceOpen type 0 + IOConnectCallMethod sel 6 (1 scalar = 1, struct in = cdhash) = trust the cdhash, then sends the cache to launchd. Boot-arg `launchd_unsecure_cache=1` skips signature + AMFI ("Using unsecure cache"); the value is parsed with strtoul base 10 (helper 0x100002da8), so a path = 0 = off: boot 0039w (18:17:13 UTC, `=/System/Library/xpc/launchd.plist`, photo IMG_20261016_202008) still aborted `Unable to find AMFI` (core dump now skipped: `Failed to generate core file ... error 14`, so 0039 works). Image `shim/out/shim-boot-0039u.img` uses `launchd_unsecure_cache=1`. Patch **0039**: do_coredump = 0 on SWEET (kern.coredump sysctl still works); CI `38945267341` -> `xnu/out-0039` (_do_coredump now in __common). Image `shim/out/shim-boot-0039w.img` = 0039 + sweet-0038hi.adt + ufsrw rootrw + `launchd_unsecure_cache=/System/Library/xpc/launchd.plist`. TODO later: real AppleMobileFileIntegrity IOService (sel 6 trust cdhash etc.).
- **Image v3 ready, awaiting user "flash".** Boot 0038w 17:38:10 UTC, photo (chat image 30): `SWEET AKS: keybagd sel 0 -> 0x0`, `sel 14 out 1/0 -> 0x0` (x2), `MKB_INIT: mount is with NO CPROTECT ... does not support Data Protection`, `System Keybag loaded`; then keybagd still opens /private/var/keybags/systembag.kb (0x100012d3c, for BackupKeyBagKeys) -> ENOENT, `no-effaceable-storage` not set -> `Can't load the keybag. tears in rain... Time to die` -> `FATAL KEYBAG ERROR: kb_load` -> reboot -> Halt/Restart Timed Out panic. Fix: ADT `/defaults/no-effaceable-storage` (mkadt.py; `shim/out/sweet-0038hi.adt`, `research/q0038-hi.adt`, QEMU OK) + file `/private/var/keybags/systembag.kb` (0600 root, dir 0700; XML plist {KeyBagKeys: data}). **v3 image** `/run/media/dipa/android/ios-sweet-ipsw/ios16-root-20H392-v3.img` = v2 + that file, fsck OK, SHA256 fa84274c...22df; `ios16-root-20H392-v3.simg` (img2simg: zero blocks become FILL chunks, so a full flash overwrites every block the rw boots changed). Flash: `fastboot -S 512M flash userdata ios16-root-20H392-v3.simg`, then boot `shim/out/shim-boot-0038e.img` (0038 + sweet-0038hi.adt + ufsrw=1 rootrw=1).
- **0038 AppleKeyStore (CI 38938744130 PASSED -> `xnu/out-0038`; QEMU msgbuf: `SWEET AKS: AppleKeyStore registered (no passcode, unlocked)`; phone image `shim/out/shim-boot-0038w.img` booted 17:38:10 UTC, `research/boot0038w.log`, photo pending).** QEMU full kernel log trick (fb.txt is only the last screen): `llvm-nm KERNEL | grep _msgbufp` -> `--hmp "x /1gx ADDR"` gives struct msgbuf address (static, 0xfffffe0007b56678 on 0038) -> `--hmp "x /3gx STRUCT"` gives bufc (3rd quad) -> `--hmp "memsave BUFC 0x20000 \"FILE\""` (quote the file name), `tr -d "\0" < FILE`. Same inputs give the same addresses. Boot 0037w 17:08:35 UTC, photo IMG_20261016_191052: bindfs fixed mount; mount-phase-1/2 pass; `cc_fips_test` boot task: all corecrypto FIPS POST tests PASSED in userspace; then keybag boot task `keybagd --init`: `MKB_INIT: FATAL KEYBAG ERROR: failed to load system bag`, `Could not get options entry from the device tree` (x2, IODeviceTree:/options = NVRAM, non-fatal), `REBOOTING INTO RECOVERY MODE` -> reboot -> panic `Halt/Restart Timed Out @IOPlatformExpert.cpp:884` (no restart path on SWEET).
  - keybagd (static libaks): load_devicebag (0x100012c30) calls aks_get_system (sel 14, 0-1 scalar in, 1 out) and loads /private/var/keybags/systembag.kb only if the result is 0xe00002f0 (NotFound); any other error -> fatal; SUCCESS -> skip the file (userspace-reboot path). systembag.kb is a plist {KeyBagKeys}; decrypted with effaceable storage unless IODeviceTree:/defaults has `no-effaceable-storage`. Image has no /private/var/keybags. has_data_protection: statfs of /private/var; no cprotect on our HFS -> device-bag volume mapping skipped.
  - libaks connection: IORegistryEntryFromPath / IOServiceMatching("AppleKeyStore"), IOServiceOpen type 0, then selector 0 (no args) must succeed.
  - **AppleKeyStore selector map (iOS 16.7 AppleKeyStore.framework, via `ipsw dyld disass`):** 0 open, 2 create_bag, 3 save_bag, 4 unload_bag, 5 set_system_with_passcode, 6 load_bag, 7 get_lock_state (1 in, 1 out; bit 2 = unlocked since boot), 9 unlock_device, 10 wrap_key, 11 unwrap_key, 12 unlock_bag/test_stashed_kek, 13 lock_bag, 14 get_system, 15 change_secret_se, 16 internal_state, 17 get_device_state (1 in, struct out <=4K: DER SET of SEQUENCE{UTF8 key, INTEGER}: ss state, sls lock_state (MKBGetDeviceLockState returns it), sb backoff, sbo, sfa failed attempts, sfao, sgs generation, sas assertions, sgpe grace, srcd countdown, sr reserved), 18 recover_with_escrow_bag, 19 obliterate_class_d, 20 drain_backup_keys, 21/22 set/clear_backup_bag, 23 get_bag_uuid, 24 rewrap_key_for_backup, 26/27 assert_hold/drop, 30/31 set/get_configuration, 33-37 stash, 38 auth_token_create, 40 generation, 41 fdr_hmac_data, 42 verify_password, 43 _aks_operation, 44 remote_session, 49/50 signing keys, 51/53/54 stash enable/persist/escrow, 55 unload_session_bags, 57/58 remote peer, 59-62 smartcard, 63 migrate_s_key, 65 set_keybag_for_volume_with_cookie_persona, 66/67 sync bags, 68 copy_volume_cookie_persona, 69-73 se_*, 74-79/84/85/99 fv_* (FileVault/VEK), 80/81 absinthe, 82 make_public_backup_bag, 83 drop_auxiliary_auth, 86 kext get/set options, 87 invalidate_bag, 88-91/94/95/106/107 se_*, 92 lower_iteration_count, 93 get_internal_info, 96-98/117 keybag_persona create/list/delete, 100/101 assert promote/consume, 102/103 notify_state set/is_set, 104 fairplay, 105 change_secret_epilogue, 108 delete_xart_leak, 110-113 backup volume, 114 sealed_hashes_set, 115 prewarm_sps, 118-128 AKSIdentity*/AKSGetLastUser.
  - Patch **0038**: `iokit/Kernel/arm/SweetKeyStore.cpp` (class AppleKeyStore on IOResources, IOUserClientClass AppleKeyStoreUserClient; personality in KernelConfigTables.cpp): 0/4/5/9/12/13/26/27/55/102 success, 14 -> handle 0, 7 -> 0x4, 17 -> DER state (ss 4, sls 0), others kIOReturnUnsupported; logs `SWEET AKS: <proc> sel N arg0 in a/b out c/d -> ret` for every call.
  - Tools: shared cache copied to `research/dsc/` (git-ignored; ipsw needs a writable dir for its .a2s symbol cache); `ipsw dyld disass research/dsc/dyld_shared_cache_arm64 --image AppleKeyStore|MobileKeyBag`; scratchpad `annot2.py BIN DIS FIXUPS` annotates llvm-objdump output with strings and import names (`ipsw macho info --fixups`).
- **0037 bindfs (phone boot 0037w pending photo).** Boot 0036mw 16:57:47 UTC, photo IMG_20261016_190102: WRITABLE ROOT WORKS: `/dev/ufsroot0 on / (hfs, local, noatime)` after mount_hfs update (GetMasterBlock EBUSY harmless), coredump of mount written to /cores (rw). Then `mount: failed to mount /private/preboot/5CBE3601.../usr/standalone/firmware -> ...: Operation not supported by device (19)` -> exit 74 -> launchd panic `mount[2] exited`. Cause: the macOS-derived config had nullfs but no bindfs (ENODEV = unknown fs type). Caller passes env 0 -> bind not required, ENOENT (missing source dir) tolerated. Patch **0037** adds `bindfs` to FILESYS_BASE in MASTER.arm64.sweet; CI `38936550483` -> `xnu/out-0037` (bindfs_vfsops present; QEMU probe regression OK). Image `shim/out/shim-boot-0037w.img` (0037 + sweet-0036hi.adt + ufsrw=1 rootrw=1) booted 17:08:35 UTC (`research/boot0037w.log`). NOTE: userdata has now been modified by rw boots; it no longer equals the v2 image, so sparsediff with v2 as OLD is not safe any more (full reflash or a new baseline needed).
- **0036 phone: launchd boot tasks run; mount -P 1 needs /chosen/boot-manifest-hash.** Boot 0036hr 16:15:32 UTC, photo IMG_20261016_185236: platform check passes; fsck boot task runs (`fsck: not booting main or diagnostic OS. Skipping fsck on OS container`); mount-phase-1: `failed to get filesystem info` x2 (no IODeviceTree:/filesystems/fstab, non-fatal), `mount: data volume missing, but not required in env: 0`, mount_hfs update of /: `GetMasterBlock: Error 16 opening /dev/ufsroot0` (EBUSY, mounted block dev, non-fatal) then mount() EROFS (read-only boot) -> `mount: / failed with 1`; then `failed to get boot-manifest-hash` -> errx exit 66 -> launchd panic `mount[3] exited`. /sbin/mount: fn 0x100005f90 reads IODeviceTree:/chosen boot-manifest-hash (hex), bindfs-mounts /private/preboot/<HEX>/usr/standalone/firmware (ENOENT tolerated unless required). `tools/mkadt.py` now adds `boot-manifest-hash` = SHA-384("sweet boot manifest") (5CBE3601...5205). ADTs `shim/out/sweet-0036hi.adt`, `research/q0036-hi.adt` (QEMU probe OK, csops 0x26000001). Image `shim/out/shim-boot-0036mw.img` (0036 + new ADT + ufsrw=1 rootrw=1), booted 16:57:47 UTC (`research/boot0036mw.log`): FIRST WRITABLE BOOT of userdata (restore source: v2 image on SSD).
- **0036 CS_SIGNED (phone boot 0036hr pending photo).** Phone boot 0035hr 2026-10-16 15:57:02 UTC (`research/boot0035hr.log`, photo IMG_20261016_180249): highmem works on hardware; shared cache maps (trust-cache warnings only), Apple launchd runs (`Darwin Bootstrapper Version 7.0.0`, `entering ondemand mode`), starts boot task fsck, then userspace panic `boot task is not a platform binary (fsck, /sbin/fsck)`. launchd+0x3fea0: `csops(pid, CS_OPS_STATUS)` must have bit 0x04000000. Cause: nothing set CS_SIGNED (AMFI does it in vnode_check_signature), so csproc_get_blob() returns NULL for every process and csops never reports CS_PLATFORM_BINARY (also breaks per-process cdhash/entitlement queries). QEMU probe init (`csops` status printer, scratchpad csq/) on 0035: `0x02000001`; patch **0036** (`*cs_flags |= CS_SIGNED` in the SWEET AMFI hook), CI `38929635791` -> `xnu/out-0036`: `0x26000001`. Images `shim/out/shim-boot-0036hr.img` / `-0036hw.img` (same cmdlines as 0035). 0036hr booted 16:15:32 UTC (`research/boot0036hr.log`). Next launchd boot tasks (embedded __config plist): fsck -qL and mount -P 1/2 (RequireSuccess); no /etc/fstab on the image.
- **0035 ENTITLEMENTS + PLATFORM BINARIES (QEMU verified).** `xnu/patches/0035-sweet-amfi-entitlements-platform-binaries.patch`: new `iokit/Kernel/arm/SweetEntitlements.cpp` (OSObject subclass: OSDictionary from OSUnserializeXML of the signature's XML entitlements + original XML for CS_OPS_ENTITLEMENTS_BLOB); `bsd/kern/sweet_amfi.c` answers queryEntitlementBoolean/String(+WithProc, string also matches array members), copyEntitlementAsOSObject(+WithProc, retained), asdict (retained), get_xml (kalloc_data copy); built-in MAC policy "SWEET AMFI" (registered from bsd_init before vfs/exec) with mpo_vnode_check_signature: every signed binary on the MNT_ROOTFS mount gets CS_PLATFORM_BINARY (stand-in for AMFI's static trust cache: iOS system binaries are ad-hoc, cd_flags 0x2, platform byte 0), and attaches entitlements ONLY for the writable copy (cs_blob != csb_ro_addr; revalidation passes the RO blob). LESSON: first version attached in adjustContextWithoutMonitor -> QEMU `unable to adjust OSEntitlements context without monitor: 53` because ubc_cs_convert_to_multilevel_hash (4K-page signatures on 16K kernel) sets csb_entitlements_blob = NULL; ALL iOS 16 binaries (launchd, backboardd, SpringBoard) use 4K code pages, so this would have broken launchd on the phone. Boot-arg `sweet_amfi_debug=1` logs `SWEET AMFI: <ident>: N entitlements (test: ...)`. `tools/mkinit.py OBJ OUT [ENTS.plist]` adds an XML entitlements blob (special slot -5; default output byte-identical). QEMU `research/q0035-ent` (RAM disk `research/root-hfs-ent.img`, init with 3 test entitlements, highmem): `org.sweet.init: 3 entitlements (test: bool 1 string 1 array 1)` then init PID 1. MAC policies loaded: 0034 = 1, 0035 = 2 (QMP `mac_policy_list`). CI `38909807230` -> `xnu/out-0035`. Phone images: `shim/out/shim-boot-0035hr.img` (highmem, ro), `-0035hw.img` (highmem, +ufsrw=1 rootrw=1), `-0035r.img` (low mem fallback).
- **PHONE TEST ORDER (updated):** 0035hr -> if instant reset, 0035r -> then 0035hw.

- **HIGHMEM: XNU in the upper DDR bank, ~3.95 GiB (was 1.2 GiB).** Live FDT memory = <0x80000000 2G> + <0x1_00000000 0xfcc00000>; all fixed reserved-memory carve-outs are in the low bank (highest: ramdump_fb 0xaf000000, rmtfs ~0xfde01000), dynamic pools have alloc-ranges <= 0xffffffff, UFS node has no `iommus` (Linux DMAs anywhere). `tools/mkadt.py --highmem` sets /chosen dram-base 0x100000000 dram-size 0xfcc00000; `tools/mkpayload.py` now takes phys_base/mem_size from the ADT (default payloads byte-identical, verified); `tools/qemu-sweet.py` sizes -m from the payload (8G for highmem). Patch **0034** (`0034-sweet-ufs-dma-above-4g.patch`): UFS refuses DMA memory above 4G without UFSHCI CAP.64AS (bit 24), logs `DMA at 0x...`. CI `38905889883` success -> `xnu/out-0034`. **QEMU verified** (`research/q0034-hi`, ADT `research/q0034-hi.adt` --qemu --highmem, HFS RAM disk): phys_base 0x100000000, init PID 1 heartbeat, no panic; `max_mem` = `mem_size` = 0xfcc00000 read via QMP (`research/q0034-hi-mem`). Phone images: `shim/out/shim-boot-0034hr.img` (read-only) / `-0034hw.img` (+ufsrw=1 rootrw=1), ADT `shim/out/sweet-0034hi.adt` (asmb SIP + sweet-ufs + highmem). PHONE RISK: unknown runtime TZ/XPU protection in the upper bank would show as an instant reset; fall back to 0033r/w. Watchdog ruled out for the overnight reset: msm-watchdog bark is 20 s (qcom,bark-time 0x4e20) yet panic screens stay up for many minutes.

- **Offline research 2026-10-16 (user away, phone unavailable). Images rebuilt** `shim/out/shim-boot-0033r.img` / `-0033w.img` now also carry `wdt_test=no_user_checkin`. NEXT ON PHONE: boot 0033r (expect past in6_nigroup), then 0033w (first writes: rw root fixes HFS link-chain bits itself and gives /var for launchd sockets /var/run/syslog, /var/run/lockdown.sock).
- **launchd.plist** (`/System/Library/xpc/launchd.plist`, compiled; 532 daemons, 66 RunAtLoad/KeepAlive, list in scratchpad `early-daemons.txt`). `_PanicOnCrash` daemons: containermanagerd/usermanagerd (InternalOnly), lockdownd/remoted/trustd/remotepairingdeviced (AutomationOnly), **watchdogd unconditional** (KeepAlive SuccessfulExit=false). watchdogd needs IOKit `IOWatchdog` kext service; if absent it exits only when `kern.hv_vmm_present` (ADT `/defaults/vmm-present`, hvg_sysctl.c) or with boot-arg `wdt_test=no_user_checkin` ("detected testing boot-args to not run, exiting"; reads kern.bootargs). Using the boot-arg (vmm-present would change many daemons; keep as an option).
- **GUI path research (QuartzCore in the shared cache, `ipsw dyld str CACHE STR`, ~10 s/search):** CA::WindowServer has IOMFBServer/IOMFBDisplay (Apple DCP/IOMobileFramebuffer), AppleCLCDDisplay, AppleRGBOutDisplay, **VirtualServer/VirtualDisplay** and **MemorySurface**, EAGLServer. Renderers: CA::OGL::MetalContext, GLContext and a **CPU software rasterizer CA::OGL::SW** (samplers/blend modes; "image is too large for software renderer"). Env knobs: `CA_ENABLE_OGL`, `CA_DISPLAY_TYPE`, `CA_FORCE_LOCAL_SERVER` (249 CA_* vars). Hypothesis (unverified): backboardd's render server could run a VirtualServer/MemorySurface with the SW renderer and we copy the surface to the ABL framebuffer -> needs disassembly of CA::WindowServer server creation. Expected other GUI walls: IOSurface (closed-source kext; needs a reimplementation), IOMobileFramebuffer user client, IOHID touch events, AppleKeyStore/keybag (SEP) for lock state, MobileGestalt answers (AppleCLCD etc.).

- **0032r RESULT (boot 05:07:50 UTC, `research/boot0032r.log`, photo IMG_20261016_071200): SHARED CACHE MAPPED, LAUNCHD RUNS.** With SIP relaxed (ADT asmb) the `not in trust cache` lines are only warnings for every subcache (.31 ... .42.dyldlinkedit); no SIP rejection; libSystem loads; launchd configures networking (`skip attaching fsw to lo0 using legacy TX model`) -> kernel data abort far 0x28 in `SHA1Init` (symbolized as sha1_init) from `in6_nigroup` (IPv6 on lo0): g_crypto_funcs NULL. Kernel addresses are unslid: `~/swift/usr/bin/llvm-symbolizer --obj=KERNEL ADDR` works directly.
- **0032** BearSSL SHA-1 (`0032-sweet-bearssl-sha1.patch`), **0033** BearSSL SHA-384/512 (`0033-sweet-bearssl-sha384-sha512.patch`, BearSSL ctx stored in SHA512_CTX, static-asserted to fit; ubc_subr.c uses SHA-384 for code directories). `0020-stage-bearssl.sh` now also stages hash/sha1.c, hash/sha2big.c, codec/{dec,enc}{32,64}be.c. CI `38854909053` success (0032-only run `38854802073` superseded). Kernel `xnu/out-0033` (SHA1Init -> _br_sha1_init verified by disassembly; nm symbols have a leading underscore). Images (ADT sweet-0032): `shim/out/shim-boot-0033r.img` (read-only), `shim-boot-0033w.img` (+ufsrw=1 rootrw=1). Still NULL g_crypto_funcs users: des, chacha20poly1305, rsa, aesxts, rand, flow_divert.
- `research/check-patch-order.py` now skips patches touching CI-staged sweet_hfs sources (0031) instead of stopping.

- **0031r RESULT (boot 05:03:52 UTC 2026-10-16, `research/boot0031r.log`, photo IMG_20261016_070508): SHARED CACHE FOUND.** dyld now opens `dyld_shared_cache_arm64` (v2 image hard links), kernel refuses to map it: `shared_region: ... map(...'dyld_shared_cache_arm64'): not in trust cache` -> `vnode is not SIP-protected` (vm_unix.c: macOS arm64 path needs trust cache, else root-owned + SF_RESTRICTED unless CSR_ALLOW_UNRESTRICTED_FS) -> `dyld cache '(null)' not loaded: syscall to map cache into shared region failed` -> libSystem not loaded -> coredump to /cores fails error 30 (EROFS) -> panic initproc failed -> "MACH Reboot" (spins; screen kept). Lots of `hfs: set hardlink bit on vol=iOSRoot cnid=... linkid=...` spam (Linux-made hard links lack the link-chain bit; read-only mount cannot persist the fix). FIX: ADT `/chosen/asmb` with `lp-sip0`=0x6f (u64; incl. CSR_ALLOW_UNRESTRICTED_FS) and `lp-sip1`=1 (unauthenticated root), `tools/mkadt.py` -> `shim/out/sweet-0032.adt`. Images with kernel 0031: `shim/out/shim-boot-0032r.img` (read-only) and `shim-boot-0032w.img` (+ufsrw=1 rootrw=1).

- **2026-10-16 morning:** userdata now = **v2** image (`diff-v1-v2.simg` flashed 21:06:50 UTC 2026-10-15, OKAY, `research/flash-userdata-diff-v2.log`); future diffs use `ios16-root-20H392-v2.img` as OLD. Booted `shim-boot-0029k.img` (ignition_level=1) right after (`research/boot0029k-v2.log`); **no photo** (user asleep). Next morning the phone was in PixelOS recovery = some hardware reset (ABL then booted Android, which cannot use userdata). OBSERVATION: our kernel cannot reset the SoC itself (`PE_halt_restart` is never set; reboot/halt print "MACH Reboot"/"CPU halted" and spin), so the reset came from outside XNU: Qualcomm watchdog or a TZ/XPU violation. Unknown which; re-boot 0029k and watch/video until the reset. Do NOT tap factory reset/format data in recovery (would wipe the iOS image).
- **0030** (`xnu/patches/0030-sweet-ufs-writes.patch`, CI `38852655542`): UFS WRITE(10) + read-modify-write for partial 4 KiB blocks + SYNCHRONIZE CACHE (DKIOCSYNCHRONIZE), only with boot-arg `ufsrw=1`; `ufs_write_blocks` re-checks partition bounds before every write (only place data goes out).
- **0031** (`xnu/patches/0031-sweet-read-write-root.patch`, CI `38852837176`): boot-arg `rootrw=1` clears MNT_RDONLY before the root mount (no data volume on sweet: /private/var is on the root; image has no /etc/fstab); Apple HFS accepts a dirty non-journaled ROOT read-write on SWEET (logs "mounting dirty non-journaled root read-write"). Later: journaled HFS for crash safety. Kernel `xnu/out-0031` (CI `38852837176` success, 5m31s; 0030 run cancelled by the newer push). Boot images (phone ADT `sweet-0028.adt`, ramdisk byte-matched): `shim/out/shim-boot-0031r.img` = 0029k args (read-only, ignition_level=1) and `shim/out/shim-boot-0031w.img` = same + `ufsrw=1 rootrw=1`. PLAN: boot 0031r first and film until the reset (diagnose overnight reset), then 0031w (first ever writes to userdata; v2 image on the SSD is the restore source). `research/check-patch-order.py` cannot check 0031 (staged sweet_hfs files are not in its index); CI stage scripts run first, so CI is the check.

- **ignition_level=1 result** (boot 21:01:39 UTC, `research/boot0029k.log`, photo IMG_20261015_230308): preboot/cryptex stages skipped (need >= 2/3), `dylib_cache: finding shared cache on system volume: ignition level = 1, search root fd = 4, subpath = System/Library/Caches/com.apple.dyld/` -> not found -> `ignition boot failed: 19` -> no shared cache -> libSystem not loaded. FIX IN PROGRESS: image v2 = copy of the flashed image + `/System/Library/Caches/com.apple.dyld/` with HARD LINKS to the 44 cryptex cache files (no extra space; avoids symlink-following doubts), then `tools/sparsediff.py ios16-root-20H392.img ios16-root-20H392-v2.img diff.simg` and flash only the diff. Boot with `ignition_level=1` (shim-boot-0029k.img).
- **ignition_level=0 result** (boot 20:58:07 UTC, `research/boot0029j.log`, photo IMG_20261015_230037): all stages skipped as "ignition level insufficient" (hello/preboot/cryptex1 sniff >= 2, select os cryptex path/graft >= 3, **dyld_cache >= 1**), `ignition boot failed: 89`, "ignition partially disabled", then same `Library not loaded: /usr/lib/libSystem.B.dylib (no such file, no dyld cache)`. => dyld gets the shared cache directory from ignition's dyld_cache stage. Next: `ignition_level=1` (`shim/out/shim-boot-0029k.img`) to skip preboot/cryptex stages but run dyld_cache; its log "finding shared cache on %s ... subpath = %s" will show where it searches. If it searches rootfs /System/Library/Caches/com.apple.dyld, add a symlink there (image rebuild; consider a diff-only sparse flash tool to avoid rewriting 12 GB).
- **APPLE dyld/launchd RUNNING (libignition) with `unentitled_ios_sim_launch=1`** (boot 20:53:24 UTC, `research/boot0029i.log`, photo IMG_20261015_225617): kernel prints "Allowing spawn of iOS binary ... boot-arg set to true"; `shared_region: ... check_np ... vm_shared_region_start_address() failed` (no region yet, normal); dyld's built-in **libignition** (Darwin Ignition Sequence 1.0.0, libignition-51~1974; prints rd=ufsroot0, hardware target sweet, ignition level 0x5, platform 0x2, boot spec local, 7 stages): `hello from launchd.1` -> `preboot: mounting preboot` -> `failed to lookup preboot: 25` (ENOTTY: looks for a separate Preboot volume by role) -> ignition fails -> dyld `Library not loaded: /usr/lib/libSystem.B.dylib ... (no such file, no dyld cache)` -> launchd exit -> panic initproc failed. dyld boot-args found via strings: `ignition_level`, `ignition_force_dylib_root` (forced rootfs/livefs/cryptex fallback), `ignition_halt_after`, `ignition_live_app_graft`; dyld cache search paths include /System/Cryptexes/OS, /System/Volumes/Preboot/Cryptexes/OS/, /private/preboot/Cryptexes/OS/ (present in our image). Next try `ignition_level=0` (`shim/out/shim-boot-0029j.img`).
- **APPLE launchd EXECUTED FROM UFS (2026-10-15).** Flashed iOS image: `img2simg` -> `ios16-root-20H392.simg` (12.1 GB) then `fastboot -S 512M flash userdata ...simg` (24 chunks, 307 s, `research/flash-userdata-ios16.log`; a raw-image flash was killed by earlyoom at 2.6 GB RSS - always flash the sparse image). ABL fastboot hung on getvar after the flash; power+vol-down fixed it. Boot 0029u 20:49:04 UTC (`research/boot0029u-ios16.log`), photo IMG_20261015_225134: `hfs: mounted iOSRoot` -> load_init_program /sbin/launchd -> panic `unexpected SIGKILL of launchd (CS_KILLED) with reason -- namespace 9 code 0xe` = OS_REASON_EXEC / EXEC_EXIT_REASON_WRONG_PLATFORM: our kernel is XNU_TARGET_OS_OSX, iOS-platform binaries are "alien" and refused unless spawn attr platform set or DEVELOPMENT TUNABLE `unentitled_ios_sim_launch`. Next try: same kernel, cmdline + `unentitled_ios_sim_launch=1` (`shim/out/shim-boot-0029i.img`). LONGER TERM: macOS-config kernel marks iOS maps alien (vm_map_mark_alien) and differs from iOS in many policies; an iPhoneOS-config kernel build may eventually be needed.
- **iOS 16 ROOT IMAGE BUILT:** `/run/media/dipa/android/ios-sweet-ipsw/ios16-root-20H392.img` (16 GiB case-sensitive HFS+, label iOSRoot, 12 GB used, fsck OK after `fsck.hfsplus -y` rewrote 32 non-ASCII localized WorkflowKit Shortcut names Linux decomposes differently). Built by `tools/mkiosroot.sh OS.dmg SYSTEMOS.dmg APPOS.dmg OUT.img [SIZE]` (needs sudo; apfs-fuse as root; OS dmg is UDIF -> link must end .dmg). Contents: iPhone X 20H392 OS root + SystemOS cryptex at /private/preboot/Cryptexes/OS (so /System/Cryptexes/OS -> cache, 44 files, dyld_v1 arm64) + AppOS at .../App. /sbin/launchd and /usr/lib/dyld root:wheel 0755. NEXT: (confirm) `fastboot flash userdata ios16-root-20H392.img` (auto-sparse), boot 0029u (rd=ufsroot0) -> Apple launchd attempt.
- **MILESTONE ON HARDWARE: BOOT FROM INTERNAL FLASH.** 0029 CI `38797924912` PASSED, `xnu/out-0029` SHA256 `a68bc847...2a37`. Image `shim/out/shim-boot-0029u.img` (payload-0029u = 0029 kernel + `shim/out/sweet-0028.adt` + `rd=ufsroot0`, NO RAM disk). Boots 20:01:34 and 20:12:37 UTC (`research/boot0029u*.log`), photo IMG_20261015_221403: `SWEET UFS: "userdata" on LUN 0: first block 0x2b8c00, 109643 MiB, read-only` -> `BSD root: ufsroot0, major 1, minor 0` -> `hfs: mounted SweetRoot on device b(1, 0)` -> `SWEET: root volume is not authenticated (error 25); continuing unsealed` -> `load_init_program ... /sbin/launchd` -> `*** SWEET userspace: init running as PID 1 ***` -> heartbeat (ran for minutes unattended). userdata currently holds the 8 MB `research/root-hfs.img`. NEXT: build a multi-GB HFS+ image with the iOS 16 (iPhone X 20H392) userland (OS rootfs + SystemOS cryptex contents incl. dyld shared cache at /System/Library/Caches/com.apple.dyld; preserve modes/owners/symlinks; decompress LZBITMAP via apfs-fuse), flash it to userdata (sparse, needs user OK), then try Apple's dyld/launchd. Driver is read-only (root mounts RDONLY), fine for first launchd attempts.
- **userdata FLASHED with the 8 MB HFS test volume** (user said "flash it", 2026-10-15 19:53:17 UTC, `research/flash-userdata-hfs-test.log`: `fastboot flash userdata research/root-hfs.img` OKAY; pre-checks: one sweet device, partition-size:userdata 0x1AC4BFB000, partition-type f2fs, image fsck OK, SHA256 3626ea69...). Android userdata is now destroyed (expected/approved). Boot 0028u at 19:53:20 (`research/boot0028u-hfs.log`), photo IMG_20261015_215541: `BSD root: ufsroot0` -> **`hfs: mounted SweetRoot on device b(1, 0)` from internal UFS** -> panic `rootvp not authenticated after mounting @bsd_init.c:931` (arm64 macOS-config requires FSIOC_KERNEL_ROOTAUTH sealed root for non-ramdisk roots). Patch **0029** (`dd1c284`): SWEET logs `SWEET: root volume is not authenticated (error N); continuing unsealed` instead of panicking (Apple configs keep the panic; pivot_root still requires auth). CI **`38797924912`** running. Next: download out-0029, phone image with sweet-0028.adt + rd=ufsroot0 (no RAM disk), boot -> expect init from UFS.
- **UFS ROOT DRIVER VERIFIED ON HARDWARE (read-only).** 0028 CI `38796139570` PASSED, `xnu/out-0028` SHA256 `8b981640...9da6`; QEMU HFS RAM-disk regression `research/q0028-hfs` still runs init. Phone boot 2026-10-15 19:50:24 UTC (`research/boot0028u.log`, image `shim/out/shim-boot-0028u.img`, phone ADT `shim/out/sweet-0028.adt` with /sweet-ufs, NO RAM disk, `rd=ufsroot0`), photos IMG_20261015_215151/215220: `SWEET UFS: "userdata" on LUN 0: first block 0x2b8c00, 109643 MiB, read-only` -> `BSD root: ufsroot0, major 1, minor 0` -> `hfs_ValidateHFSPlusVolumeHeader: unknown Volume Signature : f188` (Android-encrypted data, as expected) -> `cannot mount root, errno = 19`, kernel retries mountroot (no panic). Nothing written to flash. NEXT (destructive, needs user confirmation right before): `fastboot flash userdata research/root-hfs.img` (8 MB HFS with test init) then boot rd=ufsroot0 -> expect init from internal flash.
- **PATCH 0028 (`33aceac`): XNU read-only UFS root device**, CI **`38796139570`** running (next artifacts xnu/out-0028, research/bt29, run29.log). `bsd/dev/sweet_ufs.c`: reuses ABL's UFSHCI state (like the shim probe), takes over UTRL at a kmem_alloc_contig region (DMA layout: UTRL 0x0, UCD 0x1000, 1 MiB bounce 0x4000), verifies UTRLBA readback before any doorbell, IE=0 polled, one SCSI cmd at a time on LUN0 (TUR x3, READ(10) via bounce, PRDT 256 KiB chunks), flush_dcache64 by PA around each command (host not dma-coherent per DT), finds GPT partition by name (`ufsroot=` boot-arg, default `userdata`), exposes `/dev/ufsroot0`/`rufsroot0` (512-byte logical sectors, physical 4 KiB, DKIOC* ioctls), refuses writes (EROFS). IOKitBSDInit: `rd=ufsroot0` -> sweet_ufs_rootdev() (lazy init) else panic. ADT: `tools/mkadt.py` adds `/sweet-ufs` reg <0x1d84000 0x3000> for the PHONE only (not --qemu). Local compile OK, imports resolve. Sandbox research/bt28 (pre0028 originals). FIRST PHONE TEST PLAN (non-destructive): boot 0028 with phone ADT + `rd=ufsroot0` and NO RAM disk: expect `SWEET UFS: "userdata" on LUN 0: first block 0x2b8c00, 109643 MiB` + `BSD root: ufsroot0`, then root mount failure (userdata is still Android-encrypted) -> panic. That validates the driver before flashing an HFS image into userdata (needs explicit user confirmation at that time).
- **SUPERSEDES the 64 GiB iosroot GPT plan: user said "u can delete android" (2026-10-15).** Use the existing `userdata` partition (LUN0 sda34, 4K LBA 0x2B8C00, 0x1AC4B MiB ~107 GiB) directly as the iOS root: NO GPT changes. Keep bootloaders, recovery (OrangeFox), modem/persist/fsg/modemst untouched. Write the HFS image later with `fastboot flash userdata` (sparse, auto-split under the 768 MiB limit). Android will no longer boot normally (expected; reinstallable). Order: XNU UFS driver first, read-only against the current GPT; flash only once it can locate userdata. Nothing erased yet.
- **DECIDED (user, 2026-10-15): 64 GiB `iosroot` on LUN0 (sda), taken from userdata (wipes Android user data; Android/super kept).** Planned GPT (4K LBAs): userdata 0x2B8C00..0xD7D6FF (~43 GiB, keep type/unique GUIDs and attributes); iosroot 0xD7D700..0x1D7D6FF (0x1000000 blocks = 64 GiB, 1 MiB aligned); backup GPT area unchanged. Procedure: user backs up phone data -> boot OrangeFox recovery (adb) -> back up GPT (sgdisk --backup + raw dd of LBA 0-5 and last 6 blocks) to research/ -> sgdisk edit -> Format Data -> verify Android boots to setup. NOT EXECUTED YET.
- **INTERNAL UFS WORKS FROM THE SHIM (read-only), user authorized internal-UFS approach incl. flashing (2026-10-15, "i allow everything").** Shim probe `shim/ufs.c` (commit `12ed3ed` + filter): ufshc@1d84000 is UFSHCI 2.1 (ver 0x210, cap 0x1587031F, 32 slots), ABL leaves HCE=1, HCS=0x10F (device present, lists ready, UIC ready); probe takes over UTRL at its own buffer (verifies UTRLBA readback before any doorbell), TEST UNIT READY/READ CAPACITY/READ(10) all succeed. LUN0 = 0x1D7D800 x 4 KiB (~126.6 GB), valid GPT (64 entries at LBA 2). Photos IMG_20261015_212916/213325/213429. Big LUN0 partitions (start in 4K LBAs / MiB): super 0xCA00/0x2200 (8.5 GiB), logdump 0x22CA00/64, minidump 0x230A00/128, rawdump 0x238A00/128, recovery 0x240C00/128, cache 0x248C00/384 ext4, exaid 0x260C00/384, cust 0x278C00/1024 ext4, **userdata 0x2B8C00/0x1AC4B MiB (~107 GiB), fs unreadable = encrypted (metadata encryption)**. cache..cust contiguous = only 1.75 GiB (too small for iOS ~10+ GiB). Plan needing user consent: shrink userdata in GPT (wipes Android data) and add an `iosroot` partition at the end of LUN0. LUNs 1-5 not listed yet (sdb..sdf backed up in backup/lun).
- **fastboot limit measured (2026-10-15, read-only getvar):** `max-download-size: 805306368` (768 MiB); `max-fetch-size` unsupported; `is-userspace: no` (ABL fastboot). A `fastboot boot` image (shim+kernel+RAM disk) must be < 768 MiB, so the 2.5 GiB iOS shared cache cannot ride in the boot RAM disk even if XNU gets all RAM. Real userland needs storage access (UFS driver, or USB host) or a much smaller cache. Unlocking full RAM is still needed later (GUI).
- **iOS 16 USERLAND SOURCE (Claude, 2026-10-15).** Chose **iPhone X (iPhone10,3/10,6, D22/D221AP)** iOS **16.7.16 (20H392)** over iPhone 8: same A11 (no PAC -> plain arm64, runs on Kryo 470/ARMv8.2), last iOS for A11; X userland is home-button-less and tall-screen (1125x2436 vs sweet 1080x2400), better fit for later GUI. Note the X has its OWN SystemOS cryptex (different digest from iPhone 8's). IPSW URL in `research/ipsw-x/URL.txt` (+ BuildManifest). Images on the user's external SSD (NOT in git, do not format that disk: it holds the user's photos): `/run/media/dipa/android/ios-sweet-ipsw/iPhone10,3_16.7.16_20H392/` — `087-86593-047.dmg` = Cryptex1,SystemOS (2,743,074,816 B), `087-86698-044.dmg` = OS rootfs (4,730,814,044 B), both zip CRC-32 verified, plus Firmware/*.root_hash/.trustcache/.mtree. AppOS cryptex is 087-86715-047.dmg (10 MB, same as iPhone 8).
  - Images are raw APFS; files use LZBITMAP (decmpfs type 14). 7z cannot decode. Tools: `ipsw` 3.1.738 (AUR ipsw-bin, user-approved) needs apfs-fuse on Linux; **apfs-fuse** built in user space (system cmake broken: cmake 4.4.4 wants libjsoncpp.so.27, jsoncpp 1.9.6 installed = partial upgrade, NOT fixed) with pip cmake + `-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS="-include cstdint"`, installed to `~/.local/bin/apfs-fuse` (+apfs-dump-quick). Mount via a comma-free symlink (apfs-fuse splits options on ',' in `iPhone10,3`; exFAT has no symlinks -> put link elsewhere): `apfs-fuse LINK/087-86698-044.dmg MNT` -> `MNT/root/...`; unmount `fusermount3 -u MNT`.
  - Verified from headers: `dyld_shared_cache_arm64` magic `dyld_v1   arm64`; `/sbin/launchd`, `/usr/lib/dyld` = cputype 0x100000c subtype 0 (plain arm64). libSystem exists only inside the cache.
  - **BLOCKER for real userland via RAM disk:** shared cache = 3.10 GiB in 44 files (~2.5 GiB without the 592 MB `.symbols`), but the kernel manages only 1.20 GiB (0xb0000000..0xfd000000). Options: (A) give XNU the phone's full ~6 GB (banks 0x80000000+2G and 0x100000000+0xfcc00000, carve-out holes need pmap support) AND check fastboot max-download-size for a multi-GB boot image; (B) storage driver (Qualcomm UFS) to read a root from flash (no flashing allowed: would need a file/partition strategy); (C) run against individual dylibs extracted from the cache (uncertain).
- **MILESTONE ON HARDWARE: HFS+ ROOT FILESYSTEM -> PID 1.** Boot 2026-10-15 18:22:12 UTC (`research/boot0027b.log`, image `shim/out/shim-boot-0027b.img`), photo `IMG_20261015_202334.jpg`: `BSD root: md0` -> `hfs: mounted SweetRoot on device b(1, 0)` -> `load_init_program: attempting to load /sbin/launchd` -> `*** SWEET userspace: init running as PID 1 ***` -> repeated `SWEET init: still alive`, no panic. Kernel 0027 (built-in Apple OSS HFS + BearSSL MD5), init with SP alignment fix. NEXT: real iOS 16 userland on the HFS image (dyld, libSystem/dyld shared cache, launchd). Known gaps: entitlements (none granted), SHA-1/SHA-384/512 providers, no storage driver (RAM disk only), root mounted read-only.
- **PHONE HFS ROOT (0027) + SIGBUS ROOT-CAUSED.** Phone boot 2026-10-15 17:49:06 UTC (`research/boot0027.log`, photo `IMG_20261015_200031.jpg`): `hfs: mounted SweetRoot on device b(1, 0)` (HFS works on hardware), init printed its first line, then `pid 1 exited -- exit reason namespace 2 subcode 0xa` = SIGNAL/SIGBUS -> `initproc failed to start` panic. Diagnostic boot 18:08:13 UTC (`research/boot0027d.log`, image `shim/out/shim-boot-0027d.img`, diag init `research/diag-init` from scratch diag-init.S with [a][b][c] markers, `cs_debug=1`; photo `IMG_20261015_201547.jpg`): output `PID [a][b]` then SIGBUS -> fault at first `strb w21,[sp]`; no code-signing messages. Cause: kernel enters a static binary with sp at argc, not necessarily 16-aligned (depends on arg/apple string lengths, differs mockfs vs HFS); EL0 SP alignment check -> EXC_ARM_SP_ALIGN -> SIGBUS (sleh handle_sp_align). QEMU does not enforce SP alignment. NOT a kernel bug: real binaries start in dyld which aligns. Fix `1a8fc15`: sweet-init aligns sp at entry. Rebuilt `research/root-hfs.img`, QEMU `research/q0027-hfs2` OK. Phone image `shim/out/shim-boot-0027b.img` SHA256 `098ba46c...c5c` (payload-0027b, ramdisk match check-0027b) ready, awaiting FASTBOOT. Lesson: Assembly `;` is a comment in Apple arm64 asm (use .macro, not ;-joined #define).
- **HFS ROOT WORKS (QEMU).** 0027 CI `38779441895` PASSED, `xnu/out-0027` SHA256 `a8643886...5f6a`. QEMU `research/q0027-hfs` (RAM disk = `research/root-hfs.img`, rd=md0): `BSD root: md0` -> `hfs: mounted SweetRoot on device b(1, 0)` -> `load_init_program: attempting to load /sbin/launchd` -> `*** SWEET userspace: init running as PID 1 ***` + heartbeat, no panic. Phone image `shim/out/shim-boot-0027.img` (payload-0027 = 0027 kernel + sweet-0021.adt + root-hfs.img + rd=md0, ramdisk byte-match check-0027) ready; awaiting FASTBOOT for phone test.
- **HFS ROOT WORK (Claude), patches 0026-0027.**
  - 0026 (`f0e990c` + link fix `64c96f4`): Apple OSS **hfs-627.100.6** (commit `31d5ec238dfab485e78a9a30b8b92d13e8914f03`, paired with xnu-8796.141.3 in macos-135 release.json) staged by `xnu/patches/0026-stage-hfs.sh` into `bsd/sweet_hfs` (core + hfs_encodings; AppleKeyStore include/key prototypes guarded by CONFIG_PROTECT; SDK TargetConditionals copied). New config option `sweet_hfs` (MASTER + MASTER.arm64.sweet); 40 `optional sweet_hfs` files; Makefile.template foreach adds `_CFLAGS_ADD = -include sweet_hfs_compat.h -I...` (MUST come after kernel -D flags; compat undefs BSD/XNU_KERNEL_PRIVATE and the CONFIG_* HFS tests so kext-config.h decides: macOS profile, no content protection). `sweet_hfs_iokit.c` replaces hfs_iokit.cpp/hfs_encodings_kext.cpp: `sweet_hfs_register()` called in bsd_init after vfsinit (encodings init, vfs_fsadd VFS_TBLCANMOUNTROOT, zones, sysctls); is_ejectable 0, iterate_media none, serial KERN_NOT_SUPPORTED, OSKextGetCurrentLoadTag -> kOSKextInvalidLoadTag (renamed via compat macro). hfs_encodings' own `hfs_allocated` renamed per object (separate kexts on Apple; first CI `38777325964` failed on that duplicate at link). mockfs now validates Mach-O magic (its TODO) so non-Mach-O roots fall through to HFS (new fs entries come AFTER mockfs in vfsconf). CI `38778133720` PASSED, `xnu/out-0026` SHA256 `0b49170c...701a`, prints `SWEET HFS: hfs-627.100.6 registered`.
  - HFS+ image: `research/root-hfs.img` (8 MB, mkfs.hfsplus from AUR hfsprogs installed with user approval; loop-mounted with sudo; /sbin/launchd = research/sweet-init, /dev; fsck.hfsplus OK). QEMU `research/q0026-hfs`: `BSD root: md0` then data abort in MD5Init from hfs_getvoluuid (mount computes volume UUID).
  - 0027 (`14bf2b7`): 0020 stager also copies BearSSL hash/md5.c + codec/dec32le.c/enc32le.c; `corecrypto_md5.c` SWEET branch converts MD5_CTX <-> br_md5_context around br_md5_* calls; wrappers sweet_md5.c/sweet_dec32le.c/sweet_enc32le.c. Host test 40/40 match md5sum. CI **`38779441895`** running.
  - Local tooling: `tools/xnu-cc.py` has `XNU_CC_POST` (flags after logged ones). Sandbox `research/bt28/xnu` = 0025 CI tree + 0026/0027 edits; originals `research/pre0026`, `research/pre0027`. HFS local objects `research/hfs-objs`. Checker entries updated to bt28.
  - SHA-1 / SHA-384/512 still have no provider.
- **MILESTONE ON HARDWARE: USERSPACE PID 1 RUNS ON THE PHONE.** Boot 2026-10-15 17:16:00 UTC (`research/boot0025.log`: send OKAY0.410s, boot OKAY0.135s), image `shim/out/shim-boot-0025.img` SHA256 `9dbfa0b1...89db0`. Photo `IMG_20261015_191733.jpg`: `BSD root: md0` -> `load_init_program: attempting to load /sbin/launchd` -> `*** SWEET userspace: init running as PID 1 ***` -> repeated `SWEET init: still alive` (5 s heartbeat), no panic. Kernel 0025 + mockfs RAM disk + built-in no-entitlements AMFI. No flashing performed.
- **NEXT (toward real iOS userland):** real root filesystem with real binaries. Options: (a) build Apple OSS `hfs` (macOS 13.5 tag `hfs-627.100.6`) into SWEET like libpthread, then an HFS RAM disk image; (b) iOS 16 dyld + libSystem/dyld shared cache from the IPSW (needs APFS or repacking onto HFS). Expected hurdles: entitlements (sweet_amfi grants none), SHA-1/MD5/SHA-384/512 providers (BearSSL), missing kexts/daemons. Current init is a static test program, not launchd.
- **MILESTONE: USERSPACE RUNS (QEMU).** 0025 CI `38774788048` PASSED, `xnu/out-0025` SHA256 `5496069b...4877`. QEMU `research/q0025-rd` (150 s, no panic): `BSD root: md0` -> `load_init_program: attempting to load /sbin/launchd` -> `*** SWEET userspace: init running as PID 1 ***` -> `SWEET init: still alive` every 5 s. Exec, ad-hoc SHA-256 code signature, syscalls, devfs /dev/console, select timeouts all work. Phone image prepared: `shim/out/shim-boot-0025.img` 16,515,072 bytes SHA256 `9dbfa0b15140c0bd733a16c4f727f6a7df44a8741f98d8e266d5f2fa4f689db0` (payload-0025 = 0025 kernel + sweet-0021.adt + research/sweet-init + rd=md0; ramdisk byte-match check-0025). Waiting for user to put phone in FASTBOOT.
- **0024 CI `38773277811` PASSED**, `xnu/out-0024` SHA256 `a0ba964b...4348`. QEMU `research/q0024-rd` and **PHONE (boot 2026-10-15 17:05:55 UTC, `research/boot0024.log`, image `shim/out/shim-boot-0024.img` SHA256 `cf3dac86...6964`, 16,510,976 bytes, payload `shim/out/payload-0024.bin` = 0024 kernel + `shim/out/sweet-0021.adt` + RAM disk `research/sweet-init` + rd=md0; ramdisk byte-match `check-0024`)** agree: `Added memory device md0`, `BSD root: md0`, `load_init_program: attempting to load /sbin/launchd`, then data abort in accelerate_entitlement_queries (PC 0x...77d2458, FAR 0x1a0) = NULL AMFI interface. Photo `IMG_20261015_190715.jpg`. RAM disk root is physically verified.
- **0025 (`2518c79`)**: new `bsd/kern/sweet_amfi.c` registers a built-in AMFI interface at EARLY_BOOT/MIDDLE: no entitlements (adjustContextWithoutMonitor succeeds without creating a context, queries KERN_DENIED, copies NULL/KERN_NOT_FOUND, get_xml/transmuted false); WithMonitor KERN_NOT_SUPPORTED; OSEntitlements_query and query_context_to_object panic explicitly (CoreEntitlements closed; no kernel caller); TrustCache version 0 (0015 keys on trust_cache_rt==NULL, img4 absent -> still unavailable); CoreEntitlements table zero (PMAP_CS only). Compiles locally; checker updated (bt27 entries). CI **`38774788048`** running. Next artifacts `xnu/out-0025`, `research/bt29`, `research/run29.log`. Real iOS binaries will need real entitlements later (no OSS CoreEntitlements exists).
- **0021-0024 RAM-disk-root iteration (Claude), QEMU only so far, no new phone boot since 0020.** Each kernel tested with `tools/mkpayload.py KERNEL research/q0021.adt OUT --ramdisk research/sweet-init --cmdline "debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=md0"` then `tools/qemu-sweet.py OUT DIR --secs 150 --kernel KERNEL --fail-on-panic` (PATH needs ~/swift/usr/bin for llvm-nm). Fresh ADTs: `research/q0021.adt` (QEMU), `shim/out/sweet-0021.adt` (phone).
  - 0021 CI `38769532000` PASSED, `xnu/out-0021` SHA256 `2b7500a1...962e`, `research/bt25`/`run25.log`. QEMU `research/q0021-rd`: md0 root, then assert `iomediaClass` in IOService::publishHiddenMedia.
  - 0022 (`20ece9c`, publishHiddenMedia without IOMedia class) CI `38770666158` PASSED, `xnu/out-0022` SHA256 `a4d0e787...30c5`, `research/bt26`/`run26.log`. QEMU `research/q0022-rd`: data abort in SHA256_Init (NULL g_crypto_funcs) from vm_kernel_addrhash_internal.
  - 0023 (`c8089d1`, libkern SHA256_Init/Update/Final use built-in ccsha256_ltc_di + ccdigest_init/update on SWEET; also serves code-signing SHA-256; SHA-1/MD5/SHA-384/512 still have NO provider - BearSSL from 0020 could supply them later) CI `38771876423` PASSED, `xnu/out-0023` SHA256 `d763fed7...1003`, `research/bt27`/`run27.log`. QEMU `research/q0023-rd`: **mockfs mounted md0, `load_init_program: attempting to load /sbin/launchd`**, then panic `mockfs_fsnode_vnode failed to create fictitious pages ... rvalue = 5` (mockfs.c:351).
  - 0024 (`3293c80`): mockfs used memdev mi_base (4K page number of a VIRTUAL kernel address for IOKit RAMDisk, truncated to 32 bits) << PAGE_SHIFT as a physical address. Now zero-copy pager mapping only when mi_phys (with 4K units); virtual md uses pagein/blockmap/strategy -> memdev bcopy. memdev's own mdPhys path assumes 4K ppnums (unsafe on 16K arm64), so do NOT switch the RAMDisk to physical. CI **`38773277811`** running. Next artifacts `xnu/out-0024`, `research/bt28`, `research/run28.log`.
  - Sandboxes: bt25/bt26/bt27 contain the next patch's edited files; pristine copies in `research/pre0022`..`pre0024`.
- **PATCH 0021 (Claude): RAM disk root via mockfs -> first userspace program.** Commits `9c37771` (initial; CI `38768500280` FAILED: mockfs_fsnode.c kfree_type(mockfs_fsnode_t) typed-alloc static assert; local xnu-cc stubs `__builtin_xnu_types_compatible` so it can't catch this) and `193772f` (fix), CI **`38769532000`** running: https://github.com/dipa-studio/ios-sweet/actions/runs/38769532000. Next artifacts: `xnu/out-0021`, `research/bt25`, `research/run25.log`.
  - Kernel: `config/MASTER.arm64.sweet` FILESYS_BASE += `mockfs` (XNU's built-in FS: root device is itself a Mach-O, served as /sbin/launchd, plus /dev for devfs). Upstream mockfs bit-rot fixed: kalloc_type/kfree_type used the POINTER typedef mockfs_fsnode_t (nodes were 8 bytes) -> `struct mockfs_fsnode`. `IOService::setRootMedia` asserted IOBlockStorageDriver class exists (IOStorageFamily absent): SWEET-only, a missing class means root=NULL (same as IOSetImageBoot); Apple path keeps assert. All 21 patches apply (`research/check-patch-order.py`); 5 TUs compile locally with -DMOCKFS (`research/mockfs-objs`), imports resolve vs 0020 kernel. Sandbox `research/bt24/xnu` now holds 0021 versions of those 3 files; pristine 0020 copies in `research/pre0021`.
  - Tools: `tools/mkpayload.py --ramdisk IMAGE` appends IMAGE as an extra payload segment right after the kernel, moves kernel_end past it (shim unchanged: ADT+boot_args follow it, image stays below topOfKernelData), and adds ADT `/chosen/memory-map` `RAMDisk`=<phys,size>. Boot with `rd=md0`. Without --ramdisk output is byte-identical to existing 0020 QEMU and phone payloads (verified). `tools/sweet-init.S` + `tools/mkinit.py`: PIC static init (open /dev/console, print PID, heartbeat every 5 s via select, never exits) wrapped into a static arm64 Mach-O (__PAGEZERO, __TEXT@0x100000000, LC_UNIXTHREAD, ad-hoc SHA-256 CodeDirectory v0x20400, 4K pages). Build: `~/swift/usr/bin/clang -target arm64-apple-ios16.0 -c tools/sweet-init.S -o X.o && tools/mkinit.py X.o research/sweet-init` (ld64.lld cannot link static iOS). Signature not verified by a real codesign; the kernel will tell.
  - QEMU with 0020 kernel + RAM disk (`research/q0020-rd.bin`, rd=md0, 150 s `research/q0020-rd150`): `Added memory device md0`, **`BSD root: md0, major 1, minor 0`** (plumbing works), then the setRootMedia assert (now fixed in 0021). NOTE: QEMU needs >60 s to reach IOFindBSDRoot (60 s run showed nothing after stf0; likely RTC/inittodr wait), so use --secs 150.
  - NEXT: download 0021, QEMU 150 s with ramdisk; expect mockfs mount then init exec -> `*** SWEET userspace: init running as PID 1 ***`. Then phone boot with same payload (phone ADT shim/out/sweet-0020.adt pattern + --ramdisk + rd=md0).
- **0020 PHONE RESULT (user transcription of screen, 2026-10-15): KERNEL INIT COMPLETE, WAITING FOR ROOT.** After Skywalk self-tests PASSED, kminit, ifnet_attach lo0/gif0/stf0, screen prints `BSD`, then `Waiting on <dict><key>IOProviderClass</key><string>IOMedia</string><key>Content</key><string>Apple_HFS</string></dict>` and `Failed to write boot-picker-bringup-reason to NVRAM.` No panic. This is IOFindBSDRoot waiting for a root device (none exists); NVRAM warning is expected (no NVRAM driver). QEMU `cpu_idle` snapshot is presumably the same wait (not GDB-confirmed; milestone GDB run was cancelled by user since phone answered it). Draft `research/check-milestones20.gdb` (hbreaks inittodr/vfs_mountroot/IOFindBSDRoot/bsd_utaskbootstrap) left uncommitted/ignored.
- **NEXT MILESTONE: root filesystem -> launchd.** Plan: (1) RAM disk: shim places an image in RAM, ADT /chosen/memory-map `RAMDisk` entry, boot-arg `rd=md0` (XNU md driver). (2) HFS is a separate kext on Apple; build Apple OSS `hfs` (tag paired with macOS 13.5 in distribution-macOS macos-135 release.json) into SWEET like libpthread 0019. APFS is closed. (3) Minimal HFS image with iOS 16 launchd (later: dyld/shared cache; AMFI/code-signing absent will be a later hurdle). User's end goal is full iOS GUI.
- **0020 (AES) VERIFIED + PHONE BOOTED (Claude took over after Codex usage limit).** Actions `38765422261` (`ab9e23f`) PASSED; kernel `xnu/out-0020/kernel.development.sweet` checksum OK; buildtree `research/bt24`, log `research/run24.log`. Actual-kernel AES checks PASS (`research/check-aes20.gdb` / `.log`, 12 extra CBC cases match OpenSSL). 45s QEMU `research/q0020`: no panic, zero unknown glyphs, past old TCP/AES panic -> Skywalk self-tests PASSED -> bsd_autoconf kminit -> ifnet_attach lo0/gif0/stf0; final snapshot PC in `cpu_idle` (idle or waiting; NOT yet diagnosed, could be root-device wait). Image `shim/out/shim-boot-0020.img` 16,461,824 bytes SHA256 `775200cb02d95fc750981516b79d91dcdc96184bbe67a38f9d57c59b4a391828`, ramdisk byte-matches `payload-0020.bin` (`check-0020`). **Phone temporarily booted 2026-10-15 16:10:00 UTC**: one FASTBOOT product sweet, send OKAY0.407s, boot OKAY0.134s, total0.549s, exit0 (`research/boot0020.log`). Awaiting user photo; do not reboot the phone before it. NEXT: read photo; diagnose what QEMU waits on after stf0 (GDB backtrace of threads / longer run). Codex's STOP request below is complete.
- User requests STOP after completing the current0020 AES iteration (Actions/QEMU/phone), then leave a precise committed handoff for another AI. User says FASTBOOT ready again after0019 photo. Do not start a new blocker fix after this iteration.
- Goal: iOS16-generation XNU on Redmi Note10Pro sweet/SM7150. Always commit, use gh/Actions for kernel builds, keep this handoff current. Temporary fastboot boot only; preserve Android/backups. No userland/root filesystem boot yet.
- **Patch0020 update:** initial `e71098d` / Actions `38765012466` superseded by scalar-only compile flags. Added -mgeneral-regs-only to all six provider/BearSSL TUs and corecrypto_aes.c; object disassembly now confirms no SIMD. Updated implementation `ab9e23f`, Actions `38765422261` queued/running: https://github.com/dipa-studio/ios-sweet/actions/runs/38765422261. Do not launch another run. portable BearSSL AES CBC and legacy one-block ECB wired directly into SWEET corecrypto_aes.c. Genuine bitslice implementation, no CPU AES/SIMD requirement, heap or fake global crypto table. Source fetched by `0020-stage-bearssl.sh` from official BearSSL git at immutable `7bea48e5e850ab4cafbe68d3765cdaba13a86d6f`; MIT license retained, downloaded source stays out of git. Kernel limits include adapter only. All128/192/256-bit keys, CBC encrypt/decrypt, copied contexts, in-place/out-of-place; zero block count and size overflow handled, invalid keys clear context. Seven TUs compile to arm64 objects (`research/aes-objs`), imports resolve, all20 patches apply and sources match (`research/patch0020-check.log`). Sandbox `research/bt23/xnu`, originals `research/pre0020`. NEXT: Actions then actual-kernel NIST/random CBC/ECB checks and >=45s QEMU before another phone boot. Next artifacts out-0020 / bt24 / run24.log. 0020 updated CI pending. Local sources/checks match the scalar-only patch.
- **Latest built kernel0019**, implementation `256d3f0`, patches0001-0019. Actions `38763072907` PASSED (4m5s): https://github.com/dipa-studio/ios-sweet/actions/runs/38763072907. Kernel `xnu/out-0019/kernel.development.sweet`, SHA256 `a27df7568f03fb541cf0e8024c0a594fa178a85054fcf9eba29158cbd4c5ecc0`; checksum verified. Log/source `research/run23.log` / `research/bt23` (no nested .git). Seven pthread source/build/header files and staged adaptations byte-match local compile inputs.
- **0019 hardware photo received** `IMG_20261015_175306.jpg`: pthread registration visible, passes mbinit and DLIL checksum/scheduling, same AES fault as QEMU: PC`0xfffffe00079174e0` (`aes_encrypt_key128`), LR`0xfffffe000767c3a4` (`tcp_init`), FAR0x78, x8=0. Disassembly proves NULL g_crypto_funcs->ccaes_cbc_encrypt. Photo processed; no more0019 observation pending. General crypto table remains absent, other hash/mode operations may still fault after0020. DO NOT bypass TCP or install fake crypto. XNU already contains SHA256/HMAC implementations; AES source there is a dispatch wrapper. Full future provider interface libkern/libkern/crypto/register_crypto.h. Apple/corecrypto default README describes evaluation-only licensing, so no external corecrypto was fetched or staged; BearSSL selected for this portable AES integration.
- **0019 pthread FIXED in QEMU and observed beyond its init on phone**: 45s QEMU `research/q0019`, zero unknown glyphs, then AES panic. Actual-kernel API checks PASS `research/check-pthread19.gdb` / `.log`, guest `research/q0019-api`: function/callback table identity, actual provider pthread_init returns, locks/global hash/threadcall/typed zones initialized, thread_selfid via syscall shim + buffer bounds, process hash init/delete via shims restores NULL. Monitor quit prints expected remote-disconnect after PASS. All fixture guests stopped.
- **0019 provider**: genuine Apple libpthread-514.100.2 commit `a6c70ca0e544347a0377babf748f831577ec24f9`, exact paired release with xnu8796.141.3 in distribution-macOS/macos-135/release.json (`research/macos-135-release.json`). 0019 shell stage runs before patches (workflow copies *.sh); three SWEET wrapper TUs use kext BSD header view, vm_fault_external, renamed provider-side pthread_kern/current_uthread; register real function table at EARLY_BOOT SECOND, real pthread_init later. Stager applies bounded address casts, zero user addresses, proper panic format. SWEET opaque ksyn storage enlarged48->56 for trailing kwe_uth, checked with provider static assertion. Local pre-CI TargetConditionals fixture was syntax-only; artifact bt23 carries actual SDK header and CI is authoritative.
- **Phone0019 temporarily booted** 2026-10-15 15:51:36 UTC: exactly one FASTBOOT/product sweet verified; send OKAY0.406s, boot OKAY0.134s, total0.546s, exit0 (`research/boot0019.log`). Image `shim/out/shim-boot-0019.img`, 16,457,728 bytes, SHA256 `19429a57e486b856880df9e27327ed488e0139345311fdb0350974e72103a9a1`. Phone ADT/payload `shim/out/sweet-0019.adt` / `payload-0019.bin` timer19, unpacked ramdisk byte-match (`check-0019`). EntryPA0xb146b488, kernelendPA0xb2000000, boot_argsPA0xb2004000/top0xb2008000. Last user FASTBOOT+USB reply preceded this boot; do not assume phone is still in FASTBOOT.
- **CPU0018 physically verified** by `IMG_20261015_173951 (2).jpg`: boot CPU0 registered, real SGI1 verified, initialization complete -> BSD/MAC/mbinit -> old pthread-table panic. SweetCPU uses legacy IOCPU, ml_processor_register/processor_start/initCPU, real SGI1 clears SIGPdisabled before max CPUs completion; no forced flags/skipped waits. CPU APIs and two-CPU rejection passed (`research/check-cpu18.gdb`, `q0018-api`, `q0018-smp`). Single core; SMP/suspend unsupported. CURRENT SWEET has NO USE_APPLEARMSMP.
- QEMU next run: generate fresh `tools/mkadt.py research/q0020.adt --qemu` (timer27), mkpayload with `debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress`, then `tools/qemu-sweet.py PAYLOAD OUT --secs 45 --kernel KERNEL --fail-on-panic`. 12s can miss later panics because IORTC waits30s. Runner snapshot only: panic exit2, blank/incomplete exit3; absence of panic does not mean complete boot. Qualcomm XPU/ABL/panel and phone SMC PSCI require hardware checks.
- Local checker `tools/xnu-cc.py research/run23.log research/bt23 FILTER --per-file`, XNU_CC_OBJDIR for objects. For new BearSSL TUs add XNU_CC_EXTRA with -I bt23/xnu/libkern/crypto/sweet_bearssl/{inc,src}. Patch checker `research/check-patch-order.py` uses temporary upstream index; source fixtures/artifacts ignored under research. GDB mask registers to unsigned64, use function-entry manual-call return traps, discard manipulated guests. No QEMU guest intentionally left running.

## Session log and earlier handoffs (superseded by the active summary above)

### Prior 0019 transition notes (superseded)

- Goal: iOS 16 generation XNU on sweet. Always commit completed work, use gh/Actions for kernel builds, keep this handoff current. Temporary fastboot boot only; preserve Android/backups.
- **0019 build PASSED** Actions `38763072907` (4m5s), implementation `256d3f0`. Kernel `xnu/out-0019/kernel.development.sweet`, SHA256 `a27df7568f03fb541cf0e8024c0a594fa178a85054fcf9eba29158cbd4c5ecc0`; checksum verified. Log/source `research/run23.log` / `research/bt23`; all seven patched files and staged provider adaptations match local compile inputs. Prefer bt23 as the next baseline.
- **0019 pthread FIXED in QEMU**: 45s run `research/q0019` registers provider, passes mbinit, DLIL SUM16 checks and DLIL thread scheduling. Actual-kernel GDB checks `research/check-pthread19.gdb` / `.log` PASS: function/callback table identity, real _pthread_init returns, locks/global hash/cleanup threadcall/typed zones non-NULL, thread_selfid via actual syscall shim with output bounds, process hash init/delete through actual shims restores NULL. Guest stopped after tests; no fixture left running. GDB monitor quit prints expected remote disconnect after PASS.
- **Current next blocker:** QEMU0019 panic data abort FAR0x78 at `aes_encrypt_key128` PC`0xfffffe00079174e0`, called by `tcp_init` LR`0xfffffe000767c3a4`. Disassembly confirms `g_crypto_funcs == NULL`, loading ccaes_cbc_encrypt at+0x78. General crypto registration is still absent; DO NOT bypass TCP or supply fake crypto. Next integrate a genuine compatible crypto implementation/provider (AES first needed here, other digest/mode APIs also used). XNU contains SHA256/HMAC but no AES implementation; its corecrypto_aes.c is a dispatch wrapper. Full crypto table is in libkern/libkern/crypto/register_crypto.h.
- **0019 phone image prepared and verified**, user reports FASTBOOT+USB again; exactly one device/product sweet verified. `shim/out/shim-boot-0019.img`, 16,457,728 bytes, SHA256 `19429a57e486b856880df9e27327ed488e0139345311fdb0350974e72103a9a1`; unpacked ramdisk matches phone payload (`shim/out/check-0019`, `payload-0019.bin`, `sweet-0019.adt`, timer19). EntryPA0xb146b488; kernelendPA0xb2000000; boot_argsPA0xb2004000/top0xb2008000. Usual debug cmdline cpus=1/-noprogress. **Temporarily booted 2026-10-15 15:51:36 UTC**: send OKAY0.406s, boot OKAY0.134s, total0.546s, exit0 (`research/boot0019.log`). User asked for photo after45s. Await the photo; DO NOT reboot again before observation. Bootloader acceptance only; no0019 physical progress observed yet.
- **Hardware photo confirms 0018 progress** (`IMG_20261015_173951 (2).jpg`): real phone prints `SWEET CPU: boot CPU 0 registered, SGI1 verified, initialization complete`, enters BSD/MAC, reaches `mbinit: done`, then the same missing-pthread-function-table panic as QEMU at pthread_shims.c:318. CPU completion/interrupt delivery is now physically observed. The photo was received; no further phone boot performed yet.
- **Patch0019 committed/pushed `256d3f0`; Actions `38763072907` queued/running:** https://github.com/dipa-studio/ios-sweet/actions/runs/38763072907. Do not launch a duplicate. genuine Apple OSS libpthread-514.100.2 provider (immutable commit `a6c70ca0e544347a0377babf748f831577ec24f9`), paired with xnu-8796.141.3 by Apple's `distribution-macOS/macos-135/release.json` (`research/macos-135-release.json`). `0019-stage-libpthread.sh` fetches/stages it before patches; workflow now copies shell scripts too. SWEET-only wrappers compile all three real kernel sources, rename provider-side symbols, use kext header view and vm_fault_external, register the real function table at EARLY_BOOT SECOND. Existing pthread_init then runs the actual provider initialization. No pthread wait/syscall is bypassed. Source staging includes bounded explicit address casts/zero user addresses and panic-format fixes. SWEET kernel opaque ksyn storage enlarged from48 to56 bytes to contain this provider's trailing kwe_uth, with a provider size assertion. Local compile of three provider TUs plus pthread_shims/kern_fork passed; imports resolve and provider exports do not collide with existing kernel symbols. All19 patches apply and changed sources match (`research/patch0019-check.log`). Source sandbox `research/bt22/xnu`, originals `research/pre0019`, objects `research/pthread-objs`. Local TargetConditionals fixture only has target macros for syntax; CI stages the real macOS SDK header and is authoritative. Next Actions artifacts: out-0019 / bt23 / run23.log; run QEMU >=45 seconds. 0019 CI is pending; no 0019 QEMU run yet.
- **Latest kernel implementation `e7d9550`, patches0001-0018; CI `38760004398` PASSED (3m56s)**: https://github.com/dipa-studio/ios-sweet/actions/runs/38760004398. No build is pending. Kernel `xnu/out-0018/kernel.development.sweet`, SHA256 `b3c8d59acc88f93a2c836eb2527a05ed4fbaac7c580a2ea8c3b795137f4aadf1`; checksum verified. Source/log `research/bt22` / `research/run22.log` (baseline for patch0019; no nested .git). New driver/config sources match CI exactly.
- **CPU wait FIXED in QEMU**: SweetCPU uses legacy IOCPU, validates single boot CPU, ml_processor_register -> processor_start -> initCPU real SGI1 -> scheduler/IOKit completion -> ml_set_max_cpus. It does NOT force a flag or skip a wait. CPU APIs passed: Running CPU0, processor_array[0] association, SGI signalling enabled, name, unsupported secondary-start error (`research/check-cpu18.gdb` / `.log`, `research/q0018-api`). Single-core only; SMP and suspend unsupported. Two-CPU topology fails explicitly (`research/q0018-smp`).
- **Next blocker reproduced in 45s QEMU** (`research/q0018-long`, zero unknown glyphs): BSD initialization reaches MAC framework, network mcache and `mbinit: done`, then **`pthread kernel extension not loaded (function table is NULL). @pthread_shims.c:318`**. Do not bypass pthread_init: find/integrate the matching open-source libpthread kernel implementation and register its real function table. A 12s run `research/q0018` looked panic-free before the 30s IORTC wait elapsed; use >=45s or GDB checkpoints for the next iteration. No userland/root filesystem boot yet.
- **Phone 0018 TEMPORARILY BOOTED** 2026-10-15 **15:31:15 UTC**: exactly one FASTBOOT device verified product sweet; send OKAY0.400s, boot OKAY0.134s, total0.539s, exit0. Local log `research/boot0018.log`. This confirms bootloader acceptance only. User was asked for a photo; await it, DO NOT reboot the phone again before their observation. No flashing performed.
- Current hardware image `shim/out/shim-boot-0018.img`, **16,437,248 bytes**, SHA256 **`b7a23595d143a76c72c9fe2cfcc5d77a8f16987b5af1a2703bbffdfa6922908c`**. Phone ADT/payload `shim/out/sweet-0018.adt` / `shim/out/payload-0018.bin` (timer19), unpacked ramdisk exactly matches payload (`shim/out/check-0018`). EntryPA0xb146b488; kernelendPA0xb1ffc000; boot_argsPA0xb2000000/top0xb2004000. Command line `debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress`.
- QEMU payload/ADT `research/q0018.bin` / `research/q0018.adt` (timer27). Run `python3 tools/qemu-sweet.py research/q0018.bin research/NEW-OUT --secs 45 --kernel xnu/out-0018/kernel.development.sweet --fail-on-panic`; fresh ADT seed for a new payload. Output fb.png/fb.txt/regs.txt/qemu.log/result.json. Visible panic exit2; blank/incomplete decode exit3; no_panic_observed is snapshot-only, not successful boot. -noprogress prevents the graphical indicator corrupting console text. Qualcomm XPU/bootloader/panel and SMC PSCI still need physical validation.
- Patches0014-0017 fixed PRNG registration/direct SHA256, explicit unavailable trust-cache service with rejected loads/no matches, built-in SweetPlatformExpert, and timer PPI from ADT (phone19/default19, QEMU27). All their checks/artifacts are preserved in the history below. CURRENT SWEET has NO USE_APPLEARMSMP; do not assume gCPUIC/gAIC/gPMGR AppleARMSMP globals exist.
- Local single-file checker: tools/xnu-cc.py research/run22.log research/bt22 FILTER --per-file, output via XNU_CC_OBJDIR. Patch checker `research/check-patch-order.py` applies all18 via temporary index, matches sandbox sources. Sandbox for0018 `research/bt21/xnu`, originals `research/pre0018`, objects `research/cpu-objs`. First compiler issues were missing machine/atomic.h and nonexistent getNameSymbol; fixed and both TUs compiled successfully before CI. Fixtures/artifacts under research are local-only/ignored.
- GDB: mask int(register)&((1<<64)-1) for unsigned kernel pointers; use function-entry manual-call return traps, discard manipulated guests. No QEMU fixture is intentionally left running.

### Earlier active handoff snapshot (superseded)


- **0018 milestone:** Actions `38760004398` PASSED (3m56s), checksums passed, new CPU/build files byte-match CI tree. Kernel `xnu/out-0018/kernel.development.sweet`, SHA256 `b3c8d59acc88f93a2c836eb2527a05ed4fbaac7c580a2ea8c3b795137f4aadf1`; log/tree `research/run22.log` / `research/bt22`. QEMU `research/q0018` 12s: boot CPU registered, SGI1 verified, CPU initialization complete, **BSD initialization entered** (copyright, MAC framework, buffer headers), no panic/zero unknown glyphs. Direct CPU API checks PASS (`research/check-cpu18.gdb`, `.log`, `research/q0018-api`): CPU0 Running, processor matches processor_array[0], real SGI enabled, CPU name correct, secondary-start returns KERN_NOT_SUPPORTED. Two-CPU ADT correctly panics before registration (`research/q0018-smp`). Longer 45s run `research/q0018-long` still running; next BSD wait/fault not yet identified.
- **Prepared 0018 phone image:** `shim/out/shim-boot-0018.img`, 16,437,248 bytes, SHA256 `b7a23595d143a76c72c9fe2cfcc5d77a8f16987b5af1a2703bbffdfa6922908c`. Phone ADT/payload `shim/out/sweet-0018.adt` / `shim/out/payload-0018.bin`, usual debug args with cpus=1 and -noprogress. Unpacked ramdisk byte-compared (`shim/out/check-0018`). Entry PA remains 0xb146b488; kernel end PA 0xb1ffc000, boot_args 0xb2000000/top0xb2004000. User reports FASTBOOT+USB ready; read-only check found exactly one product sweet. NEXT: temporary fastboot boot of this verified image, record send/boot results, then await hardware photo without rebooting again.
- Patch 0018 committed/pushed **`e7d9550`**; Actions **`38760004398`** queued/running: https://github.com/dipa-studio/ios-sweet/actions/runs/38760004398. Do not launch a duplicate run.
- Active continuation: patch **0018** implemented in `iokit/Kernel/arm/SweetCPU.cpp` + arm64 source list + exact qcom,kryo470 personality. Keeps legacy IOCPU, accepts ONLY the single boot CPU, fills ml_processor_info from parsed topology with CPU object as cpu_id, calls ml_processor_register and processor_start. initCPU sends SGI1 via ICC_SGI1R_EL1 and waits (1-second deadline) for real cpu_signal_handler to clear SIGPdisabled. Only after successful startup calls ml_cpu_init_completed/cpusRunning and publishes completion/max CPUs. Secondary startup/suspend are explicitly unsupported. Local C++ compile passed for both TUs (`research/cpu-objs`), all imports resolve, object contains dsb ishst / ICC_SGI1R / isb; all18 patches apply in order and match sources. Sandbox `research/bt21/xnu`, originals `research/pre0018`; next CI artifacts should use `xnu/out-0018`, `research/bt22`, `research/run22.log`. User has the phone available; first run QEMU, then prepare/temporarily boot useful hardware progress.
- Goal: iOS 16 generation XNU on Redmi Note 10 Pro / sweet. User wants persistent progress, commits, kernel builds via `gh` Actions, and this handoff kept current across agent switches. Temporary `fastboot boot` only; preserve Android and backups.
- Latest kernel implementation: **`aa3f991`**, patches **0001-0017**. All four new Actions builds PASSED: 0014 `38748543058` (4m0s), 0015 `38749549779` (4m3s), 0016 `38750733996` (4m3s), 0017 **`38751507437` (4m7s)**. Latest: https://github.com/dipa-studio/ios-sweet/actions/runs/38751507437. No build is pending.
- **Current blocker: CPU initialization has no SWEET CPU driver.** QEMU registers SweetPlatformExpert, then kernel_bootstrap_thread waits in **kperf_init_early -> ml_wait_max_cpus**. Confirmed via actual-kernel GDB call stack (`research/locate-cpu-wait2.log` / `.symbols`) and normal-run `max_cpus_initialized == 2` (MAX_CPUS_WAIT). Do not force the flag or remove the wait. Next implement real CPU registration and completion using XNU's CPU/processor interfaces. CURRENT SWEET uses legacy IOCPU (`USE_APPLEARMSMP` is absent); enabling AppleARMSMP is a separate architectural change requiring GIC/PMGR services. No userland/root filesystem boot yet.
- Fixed this session: PC=0 unregistered PRNG callback (0014, built-in HMAC-SHA256 provider + direct entropy/write_random hashing); absent Image4/AMFI trust-cache service (0015, unavailable runtime, loads rejected and queries no-match); missing platform driver (0016, SweetPlatformExpert exact personality); incorrect QEMU timer IRQ (0017, validated ADT PPI property, phone default19/QEMU27).
- **Latest kernel** `xnu/out-0017/kernel.development.sweet`, SHA256 `055a80190ab998b4df9203f1102072dcc78faad1779a537a514e522b682b582f`. Buildtree/log **`research/bt21` / `research/run21.log`**; all nine changed C/C++/header/config files byte-match local checked sources. Checksums passed. Entry PA `0xb146b488`, kernel end PA `0xb1ff4000`. Preferred baseline for patch 0018: `research/bt21/xnu` (no nested .git).
- **QEMU verified**: `research/q0017-noprogress` ran **30 seconds**, no visible panic, **zero unknown glyphs**, IRQ exceptions returned normally (4 IRQs, tickless idle). It still waits for CPU completion, so this is NOT a completed boot. Earlier 15s run `research/q0017` captured 122 IRQs, timer value27, GICR_ISENABLER0 `0x08000002`, and CPU WAIT; graphical progress overlay corrupted 13 glyphs. Add **`-noprogress`** to keep the console readable. New runner classifies incomplete/blank decoding explicitly (exit3 with --fail-on-panic), visible panic exits2. Snapshot-only; use regs/GDB to diagnose stalls.
- Current QEMU payload/ADT: **`research/q0017-noprogress.bin` / `research/q0017.adt`**. Run: `python3 tools/qemu-sweet.py research/q0017-noprogress.bin research/NEW-OUT --secs 30 --kernel xnu/out-0017/kernel.development.sweet --fail-on-panic`. Generate fresh seed with mkadt for a new payload. QEMU virt models generic ARM execution, not Qualcomm XPU/bootloader/panel or the phone's SMC PSCI conduit.
- **Prepared phone image**: **`shim/out/shim-boot-0017.img`**, 16,416,768 bytes, SHA256 `51ff7fe30b4a6668c3452ffe1da922c5ac30cb8de60625c2a652587ecbd0ec92`. Phone ADT/payload `shim/out/sweet-0017.adt` / `shim/out/payload-0017.bin` (timer19). Unpacked ramdisk exactly matches payload (`shim/out/check-0017`). Command line **`debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress`**. NOT sent to phone this session: known CPU-driver wait should be addressed in QEMU first. Last actual phone boot remains 0013; user's latest photo `IMG_20261015_160033.jpg` confirmed the old PRNG PC=0 panic.
- Direct QEMU/GDB API checks: PRNG 600-byte chunking, buffer bounds, changing output, zero-length reads/writes, user reseed, contended refresh, generator1 init/output PASS again on latest0017 (`research/check-prng17.gdb` / `.log`, guest `research/q0017-api`). Trust-cache unavailable-service/argument/output cases PASS on0015 (`research/check-trustcache.gdb` / `.log`). Platform registration + target/product normal/truncated/zero/null/bounds PASS on0016 (`research/check-platform.gdb`, `research/check-platform3.log`). Different platform ID correctly retains IOPanicPlatform fallback (`research/q0016-negative`). Timer PPI32 and size2 correctly panic (`research/q0017-badppi`, `research/q0017-badsize`); omitted property selects19 (`research/q0017-default`). These are API/bring-up checks, not crypto certification or SMP scheduling validation.
- Local checker **`research/check-patch-order.py`** verifies all17 patches and changed source equality using a temporary upstream git index. Fixtures/logs/artifacts under research are local-only and ignored. For GDB unsigned kernel pointers, mask `int(register_value) & ((1<<64)-1)`; x30/x29 may be rendered signed. Stop at a function entry for manual-call return traps, and discard each manipulated guest after API tests. All fixture QEMU guests have been stopped.


- User wants persistent progress toward iOS 16, commits for completed changes, builds through `gh` / GitHub Actions.
- Keep this file current at every milestone so another agent can resume immediately, including when usage limits interrupt a session.
- Patch 0017 committed/pushed `aa3f991`, Actions `38751507437` running: https://github.com/dipa-studio/ios-sweet/actions/runs/38751507437. Next artifacts: xnu/out-0017, research/bt21, research/run21.log. Do not duplicate.
- Patch 0016 kernel downloaded/checksum verified: SHA256 `d06336c54baa06e0df6561a8bd7df98a9b9f7502c9381c62d1a44c045cd1ce46`, `xnu/out-0016/kernel.development.sweet`; buildtree/log `research/bt20` / `research/run20.log`. QEMU `research/q0016` (10s) prints platform expert registered, zero unknown glyphs, no panic observed BUT stalls (timer mapping still wrong until 0017). Direct actual-kernel GDB tests passed for registered instance and target/product names, including maxLength=1/0 and NULL, sentinels intact (`research/check-platform.gdb`, `research/check-platform3.log`, `research/q0016-api3`). First two fixture attempts used premature/wrong return breakpoints; discarded, corrected fixture uses start-return observation then a function-entry return trap. No phone boot yet.
- Patch 0016 CI `38750733996` PASSED (4m3s); artifacts not yet downloaded/booted. Patch 0017 prepared to fix QEMU timer fidelity BEFORE interpreting future no-panic/stall runs: actual `qemu-system-aarch64 -M virt,gic-version=3,dumpdtb=research/qemu-virt.dtb ...` timer property has virtual PPI 11 => INTID 27, while phone uses INTID 19. Kernel formerly hardcoded 19. New optional /arm-io/gic virtual-timer-intid property selects the PPI (validate exact u32 and 16..31); missing property retains phone default 19. One RO-late global drives GIC enable and sleh_irq dispatch. tools/mkadt.py emits phone19/QEMU27. Both C TUs compile (`research/timer-objs`), ADT serialize/parse checks confirm values/addresses, all 17 patches apply and match. Sandbox `research/bt19/xnu`, originals `research/pre0017`; initial local sleh compile failed due read-only exported header not updated, fixed export and BOTH recompiled successfully. CI exports normally.
- Architecture correction for next agent: CURRENT SWEET board_config does NOT define USE_APPLEARMSMP; it compiles the legacy IOCPU path, not cpu_boot_thread/IOPMGR AppleARMSMP. Existing roadmap mentions AppleARMSMP as a future approach. Do not look for gCPUIC/gAIC/gPMGR globals in the current kernel: they are absent. Determine the actual CPU-init wait after platform registration before choosing the next driver architecture.
- Patch 0016 committed/pushed as `0dcdf5c`; Actions `38750733996` running: https://github.com/dipa-studio/ios-sweet/actions/runs/38750733996. Next artifacts: `xnu/out-0016`, `research/bt20`, `research/run20.log`.
- Patch 0016 implemented: new `iokit/Kernel/arm/SweetPlatformExpert.cpp`, arm64 source list, built-in personality in KernelConfigTables.cpp guarded by ARM64_BOARD_CONFIG_SWEET. Matches only xiaomi,sweet at score 10000, keeps IOPanicPlatform fallback for other hardware. Uses IODTPlatformExpert base initialization/power domain and createNubs CPU logical-ID annotation; overrides top-level publishing because base requires nonexistent Apple NVRAM /options. Publishes real CPU/top-level nodes, provides bounded target/product names, and registers the platform service. GIC/PMGR and arm-io child-bus drivers remain separate work. Both C++ TUs compile (`research/platform-objs`), all imports resolve against 0015/own objects, all 16 patches apply and sources match. Sandbox `research/bt19/xnu`; originals `research/pre0016`. NEXT: full Actions/QEMU; inspect CPU boot waits after the platform registers, do not equate absence of panic with successful boot.
- Patch 0015 CI `38749549779` PASSED (4m3s), checksum verified. Kernel `xnu/out-0015/kernel.development.sweet`, SHA256 `ca1b424a596c7058550ad1659080a333e7643270f019656401d80bf2b876645f`; buildtree/log `research/bt19` / `research/run19.log`. QEMU `research/q0015` (10s, zero unknown glyphs) passes trust-cache handling and machine lockdown, then panics `Unable to find driver for this platform: "xiaomi,sweet" @IOPlatformExpert.cpp:2319` (IOPanicPlatform fallback). Next concrete work: add a built-in SWEET IODTPlatformExpert personality/class; keep the fallback for mismatched platforms. No phone boot yet.
- Direct trust-cache API checks PASSED in the actual 0015 kernel under QEMU/GDB (`research/check-trustcache.gdb`, `research/check-trustcache.log`, `research/q0015-api`): runtime pointers NULL, hash/UUID queries return NOT_FOUND, typed/untyped/legacy loads return NOT_SUPPORTED, capabilities outputs zero and bounds preserved, invalid-argument cases still reject. Second PRNG check also verifies contended refresh returns immediately (`research/check-prng2.log`, `research/q0014-api2`).
- Patch 0015 committed/pushed `8f851d4`, Actions run `38749549779` running: https://github.com/dipa-studio/ios-sweet/actions/runs/38749549779. Do not duplicate.
- Direct PRNG API checks passed in the REAL 0014 kernel under QEMU/GDB, stopped before trust-cache init (`research/check-prng.gdb`, log `research/check-prng.log`, guest `research/q0014-api`). Invoked read_random for 600 bytes (chunking and buffer sentinel intact), repeated output differed, zero-length read/write succeeded, write_random(64-byte input) succeeded, secondary generator 1 initialized and generated distinct output. QEMU log has no exceptions during these checks; guest was discarded after the tests. This tests API behavior, not a cryptographic certification or SMP scheduling.
- Patch 0015 implemented in `bsd/kern/kern_trustcache.c`: SWEET with missing Image4/AMFI marks both runtime pointers NULL and logs service unavailability. Existing static loader handles NULL. Public load APIs return KERN_NOT_SUPPORTED; hash/UUID queries return KERN_NOT_FOUND; capabilities outputs are zero with KERN_NOT_SUPPORTED. Invalid-input checks remain, and Apple paths/available-provider initialization retain existing behavior. No trust is granted. Local arm64 compile passed (`research/trustcache-objs`), all 15 patches apply and sources match. Sandbox `research/bt18/xnu`, baseline `research/pre0015`. Next: Actions/QEMU and direct unavailable-service API checks using GDB.
- Patch 0014 CI `38748543058` PASSED (4m0s), artifact checksum passed. Kernel `xnu/out-0014/kernel.development.sweet`, SHA256 `741de20c851755d333759d7e668346f3edb4f325efac4cc7c142d80d1f5ff75c`; buildtree/log `research/bt18` / `research/run18.log`. QEMU run `research/q0014` (10s, zero unknown glyphs) prints `SWEET PRNG: HMAC-SHA256 registered`, passes commpage initialization, loads vnguard policy, THEN panics `image4 interface not available @kern_trustcache.c:253`. PC=0 fault FIXED IN QEMU. No new phone boot performed.
- Next work: SWEET currently has neither AppleImage4 nor AMFI. `trust_cache_runtime_init` unconditionally requires both; unlike BCM2837, SWEET enters this startup path. Investigate an explicitly unavailable trust-cache runtime with rejected loads/no query matches while providers are absent; preserve all Apple-platform checks. Merely skipping the startup call would leave later AMFI/IMG4 dereferences unsafe. Existing load_static_trust_cache already handles trust_cache_rt == NULL.
- QEMU runner now supports `--kernel MATCHING_KERNEL --fail-on-panic`: runtime font extraction from the payload produces exact `fb.txt`, plus `result.json`; a visible panic returns 2. Baseline 0013 re-run `research/q0013-text` detects the same NULL callback panic with zero unknown glyphs. Synthetic glyph roundtrip (including literal `?`) and invalid-VA rejection passed. Screen rows remain circular, not a full log; `no_panic_observed` does not prove successful boot. README has the invocation.
- Patch 0014 committed/pushed as `be61f99`; Actions run `38748543058` is running: https://github.com/dipa-studio/ios-sweet/actions/runs/38748543058. Do not start a duplicate build.
- 2026-10-15 active: hardware photo `IMG_20261015_160033.jpg` confirms the same PC=0 instruction-fetch abort as QEMU (LR `0xfffffe0007443fe4` = read_random). Independently reproduced with kernel 0013 in `research/q0013` (8-second run); prior allocator fix works. Latest tooling commit reviewed: `8d66d2d`.
- Patch 0014 implemented: registers a built-in HMAC-SHA256 DRBG provider at EARLY_BOOT/FIRST, before commpage creation. A shared mutex serializes output/reseed across CPUs (correctness first; no per-CPU scaling yet), initgen validates CPU identities, refresh uses try-lock and the existing entropy filter/health tests. SWEET entropy accumulation and write_random now use the built-in SHA256 directly; no incomplete g_crypto_funcs table is registered. The existing bootloader-seed requirement remains enforced.
- Local validation: both PRNG translation units compile to arm64 objects in `research/prng-objs`; no SHA256 wrapper imports remain. All 14 patches apply in CI order and resulting PRNG sources match local inputs. Sandbox `research/bt17/xnu`, originals `research/pre0014`, checker `research/check-patch-order.py`. NEXT: Actions kernel build, download to `xnu/out-0014`, run the QEMU payload before requesting another phone boot. Baseline QEMU ADT/payload are `research/q0013.adt` / `research/q0013.bin`.
- 2026-10-15 08:15 QEMU EMULATION WORKS (`tools/qemu-sweet.py`, see "QEMU emulation" below). Patch 0013 VERIFIED in QEMU: boot passes kmem_crypto_init and ulock_initialize. NEXT BLOCKER (reproduced in QEMU, expected on hardware too): `pmap_create_commpages -> random() -> RandomULong -> read_random -> prng_funcs.refresh == NULL` (instruction fetch abort at pc 0, lr read_random+0x34). Nobody calls `register_and_init_prng` (closed corecrypto kext does it on Apple). Fix = patch 0014: SWEET-registered cckprng_funcs provider (osfmk/prng/prng_random.c); note random_cpu_init also panics while !prng_ready. The 07:26 hardware boot of 0013 will most likely show this same panic.
- Previous hardware boot: patch 0013 (`a3a46e2`) CI `38686345689` PASSED (3m44s), image `shim/out/shim-boot-0013.img` was temporarily booted on 2026-10-15 at 05:26 UTC: send OKAY (0.407s), boot OKAY (0.134s), total 0.546s. Superseded by the hardware/QEMU PC=0 observation and patch 0014 work above.
- Latest kernel implementation commit: `8f851d4`. Latest successful kernel CI: `38749549779` (patch 0015), https://github.com/dipa-studio/ios-sweet/actions/runs/38749549779.
- CURRENT image: `shim/out/shim-boot-0013.img`, 16,412,672 bytes, SHA256 `110d1c46aee4b60253db24b6535e9781c4cf0fd25ca2a70e9016cf81378cea25`. Kernel `xnu/out-0013/kernel.development.sweet`, SHA256 `f0b1d375995316c550ce7014ff234d8271e9b9ac2857795ceff41b76ba2cd740`, plain arm64. CI log confirms 0013 applied; checksums passed. Entry PA `0xb146b488`, kernel end PA `0xb1ff4000`.
- Current log/buildtree: `research/run17.log`, `research/bt17` (extracted, no nested .git; preferred baseline for patch 0014). Payload/ADT: `shim/out/payload-0013.bin`, `shim/out/sweet-0013.adt` (includes /defaults). Unpacked ramdisk byte-compared with payload in `shim/out/check-0013`.
- Patch-0012 original image: `shim/out/shim-boot-0012.img`, 16,412,672 bytes, SHA256 `cf4b8ec4a65a51888d71c17d09cd0831d6b761b90e2531fdb8e575c32f4fd24a`. Kernel `xnu/out-0012/kernel.development.sweet`, SHA256 `69b78f49f6459cc63a0af44e7cf73c1779ef72130ef72b12ae86c88183ffc742`; checksums passed and sweet_early_relocate symbol present. Entry PA remains `0xb146b488`, kernel end PA `0xb1ff4000`.
- Patch-0012 log/tree: `research/run16.log`, `research/bt16` (extracted, no nested .git; preferred baseline for next patch). CI log confirms 0012 applied. Payload `shim/out/payload-0012.bin`, ADT `shim/out/sweet-0012.adt`; packed ramdisk byte-compared with payload in `shim/out/check-0012`.
- Active payload command line: `debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam`.
- Latest observed photo: `/home/dipa/Downloads/IMG_20261015_071346.jpg` (local only). Stages 9 (kernel framebuffer VA), 10 (TTBR0 off/TLB flush), 11 (physmap table pointers) and 12 (calling pmap_bootstrap) all visible. Persistent diagnostics are VERIFIED ON HARDWARE.
- pmap.c:1987 is the assert after SecureDTLookupEntry(NULL, "/defaults", &entry). The pmap-max-asids property itself is optional; an empty /defaults uses XNU's MAX_ASIDS. Do not bypass the assertion or guess an ASID limit.
- Previous payload-only fix image: `shim/out/shim-boot-0012-defaults.img`, SHA256 `3ff61a268853c6ea7cbf284833f1346a1d791980ca366b6e1a215a86e040512b` (16,412,672 bytes). Same kernel/CI as 0012; ADT 2516 bytes (`shim/out/sweet-0012-defaults.adt`), root /defaults present with no ASID override. Payload `shim/out/payload-0012-defaults.bin`; unpacked ramdisk exactly matches payload (`shim/out/check-0012-defaults`). This boot reached the allocator crypto NULL-pointer fault below.
- Latest hardware photo: `/home/dipa/Downloads/IMG_20261015_071608.jpg` (local only). Normal serial framebuffer console works. VM bootstrap/zone initialization, logging, scheduler, IPC, KDP and ulock_initialize print before the kmem_crypto_init data abort. gPanicBase warnings are expected because PLATFORM_PANIC_LOG_DISABLED is configured, and are not the faulting call.
- Faulting instruction: `ldr x0, [x8, #0x150]` in crypto_random_kmem_ctx_size; x8 = NULL g_crypto_funcs. Source: libkern/crypto/corecrypto_rand.c and osfmk/vm/vm_kern.c:2448. rand.h explicitly permits NON-cryptographic kmem RNGs, but requires no locking in generation and context size <=256. Do not substitute read_random here: the global PRNG provider is also unregistered and kmem generation must remain lock-free. early_random may seed contexts during single-CPU bootstrap; it uses the built-in HMAC-SHA256 DRBG with the host-generated ADT seed.
- Patch 0013 keeps the replacement entirely private to vm_kern.c: SplitMix64 state is 8 bytes per CPU/sizeclass, initialized using early_random. Uniform selection uses rejection sampling to avoid modulo bias. Other platforms retain the original corecrypto calls; SWEET's generic crypto APIs/registration remain untouched. Reference: https://prng.di.unimi.it/splitmix64.c (Sebastiano Vigna, public domain).
- Validation: vm_kern.c compiles to arm64 (`research/kmem-rng-objs`, using `research/run16.log` + `research/bt16`). Undefined symbols include early_random and NO crypto_random_* references. All 13 patches apply in CI order and match the checked source. Original saved as `research/vm_kern.pre0013.c`.
- RNG host checks: extracted actual new helper in `research/kmem-rng-check.c`; 16,384 outputs match the author's downloaded reference (`research/splitmix64-reference.c`), 147,456 bounded outputs satisfy their limits (including 1 and UINT64_MAX), and a forced rejection advances correctly. Full kernel crypto services and the registered kernel PRNG provider remain future work; this patch only supplies the allocator RNG contract.
- Patch-0011 artifacts: kernel `xnu/out-0011/kernel.development.sweet`, log `research/run15.log`, extracted buildtree `research/bt15` (no nested .git). CI log confirms patch 0011 applied; artifact checksums passed. Kernel is plain arm64, entry VA `0xfffffe000746b488` -> PA `0xb146b488`, kernel end PA still `0xb1ff4000`.
- Patch-0011 image: 16,412,672 bytes, SHA256 `29ad5cbfc752473de4af2c471a24a8b34f92da4bff5afec65c614b90cd3ad53a`. Kernel SHA256 `8ac3a956894b09f7bd0d890de8c5de38c65513287feb1e1bcd12db8ca772ee5c`. Payload/ADT in `shim/out/payload-0011.bin`, `shim/out/sweet-0011.adt`. Header unpacked/checked in `shim/out/check-0011`.
- First patch-0011 attempt timed out waiting for FASTBOOT; superseded by the successful send/boot recorded above. Do not repeat the boot before the user reports the current screen.
- Patch-0011 photo: `/home/dipa/Downloads/IMG_20261015_070218.jpg` (local only). Visible stages 1-8 and old red shim text; entry PA B146B488, boot args PA B1FF8000 / top B1FFC000. No evidence yet locating execution after stage 8.
- Patch-0012 sandbox: `research/bt15/xnu` (NO nested .git). Proposed permanent debug VA: VM_MIN_KERNEL_ADDRESS + ARM_TT_L2_SIZE, in the reserved gap below gVirtBase and above low globals; map the same PA framebuffer as Normal-NC in the final TTBR1 before arm_vm_physmap_init changes phystokv(). Keep the early panic sink until normal kprintf startup.
- Patch 0012 validation: all 4 changed C translation units compiled to arm64 objects in `research/post-ttbr0-objs`; internal header export added to osfmk/arm64/Makefile and recreated locally in EXPORT_HDRS for pe_serial.c. All 12 patches apply in CI order and match all 6 edited files. Baselines are saved under `research/pre0012/`.
- Actual mapping-helper host model (`research/early-map-check.c`, barrier stubbed only for host compilation) confirms: VA `0xfffffe0002000000`, L1 index 2016, L2 index 1; descriptor `0x006000009c000605` = framebuffer PA, Normal-NC, outer-shareable, EL1 RW, PXN+UXN. All other L2 entries preserved. The gap is excluded by ARM_LARGE_MEMORY's pmap_virtual_region(0).
- Next boot command line MUST add `-enable_kprintf_spam`: DEVELOPMENT disables kprintf_output by default even when DB_KPRT is set (`pexpert/arm/pe_kprintf.c`). Planned command line: `debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam`.

### Patch 0012 screen legend (after original stages 1-8)
- 9: kernel framebuffer VA live; 10: TTBR0 off / TLB flushed; 11: physmap pointers rebound.
- 12: calling pmap_bootstrap; 13: pmap_bootstrap returned; 14: early I/O page tables ready; 15: arm_vm_init returned.
- 16: calling serial_init; 17: fbcon config read / mapping; 18: fbcon mapping returned; 19: serial_init returned; 20: kprintf startup complete.
- Normal serial initialization clears the screen, so earlier stages may disappear before 19. If serial exists, early console disables after 20; otherwise early panic output stays enabled.
- Local source sandbox: `research/bt13/xnu` (git baseline `623b060`, patches 0001-0008 already in baseline; uncommitted export edits correspond to patch 0009). Preserve those edits when producing patch 0010.
- Previous compile log/tree: `research/run13.log`, `research/bt13`; previous kernel: `xnu/out/kernel.development.sweet`. New successful log/tree: `research/run14.log`, `research/bt14`; new kernel: `xnu/out-0010/kernel.development.sweet`.
- Current device access: patch-0012 image was temporarily started from FASTBOOT; no adb connection is expected. Never flash partitions for bring-up; use `fastboot boot` with the shim image.
- Patch `0010` lives in `xnu/patches/0010-sweet-early-framebuffer-debug.patch`. Its exact compiled sources are under `research/bt13/xnu`; generated objects are in `research/early-objs`.
- Local checks passed: all 5 changed/new translation units compiled to arm64 Mach-O objects; new console only imports `_doprnt` and `iso_font` (both exist in the previous kernel). All 10 patches applied to a temporary upstream git index in CI order, and all 7 resulting changed source files matched the local compile inputs.
- Reproduce changed-file compilation: `XNU_CC_OBJDIR="$PWD/research/early-objs" python3 tools/xnu-cc.py research/run13.log research/bt13 arm_init.c arm_vm_init.c kern/debug.c arm64/start.s osfmk/arm64/sweet_early.c --per-file` (create the output directory first). The helper now accepts explicit new source paths absent from the old artifact's Makefiles and rejects empty selections.
- Device command result: `adb reboot bootloader`, then `timeout 30s fastboot boot shim/out/shim-boot-0010.img`: send OKAY (16028 KB), boot OKAY, total 0.541s. This confirms bootloader acceptance only, not XNU progress.
- The successful boot above was on 2026-10-14. On 2026-10-15, `timeout 45s fastboot boot ...` timed out waiting for a device; no image was sent. Boot image SHA256 rechecked and unchanged. No new build is needed to repeat the trial.
- Earlier retry: `timeout 90s fastboot boot shim/out/shim-boot-0010.img` timed out (exit 124). This is superseded by the successful 2026-10-15 repeat boot recorded above; do not repeat it until the user reports the screen.
- User observation confirms MMU-on C entry, platform expert, CPU topology/bootstrap, startup bootstrap and cpu_init all complete. Failure is within arm_vm_physmap_init (non-XNU_MONITOR branch), before stage 8 / TTBR0 teardown. Old red shim text remains behind the early console; this is expected because only written rows are cleared.
- New edit/compile sandbox is `research/bt14/xnu` (extracted artifact, NO nested .git; `git diff` there would act on the parent project). Use a saved baseline file / generated unified diff, or explicitly initialize a local baseline before editing. `research/bt13/xnu` does have its own baseline git repo.
- Exact user-reported panic: `arm_vm_init.c:1606 Assertion failed: (temp_ptov_table[ptov_index - 1].va + temp_ptov_table[ptov_index - 1].len) <= physmap_end`.
- Root cause: MacOSX build sets `ARM_LARGE_MEMORY=1`; `XNU_MONITOR` is off. Block alignment in arm_vm_physmap_slide can add gaps, but the non-PPL large-memory physmap_end omitted the padding already present in the small-memory branch. Patch 0011 reserves PTOV_TABLE_SIZE*ARM_TT_TWIG_SIZE (8*32MB) in both the aperture bound and its L1 allocation; monitor/PPL builds retain zero padding. The assertion is preserved.
- Validation: `arm_vm_init.c` compiled to an arm64 object using `research/run14.log` + `research/bt14` (object in `research/physmap-objs`). All 11 patches apply in CI order and produce exactly that source. Host harness `research/physmap-check.c` extracts the real XNU slide function with page-table-write stubs: all 65,536 page-aligned slides within 1GB checked against the current kernel memory layout; old bound fails 65,504 cases, padded bound fails zero; worst overrun 0x1ffc000 bytes. Baseline source saved as `research/arm_vm_init.pre0011.c`.
- CI: https://github.com/dipa-studio/ios-sweet/actions/runs/38638702083 (success; artifacts and full log downloaded).
- New image: `shim/out/shim-boot-0010.img` (16,412,672 bytes, SHA256 `ed2c270e0081bad5ff517be415f9bfe8805463aea7866732203fa97ac737e64e`). Previous image preserved as `shim/out/shim-boot-pre0010.img`.
- New kernel SHA256: `3ee05270cc291c49589a101678f904b1b69cb2f2575e05398bce8a4e052f409a`. Artifact checksums passed; arm64 (not arm64e), early-console symbols present. Payload entry VA `0xfffffe000746a488` -> PA `0xb146a488`; kernel end PA `0xb1ff4000`. Command line remains `debug=0xc serial=3 cpus=1 keepsyms=1`.

### Patch 0010 screen legend
- Bottom-left white 64x32 tile: assembly entered `start_first_cpu`, before page tables/MMU.
- Text `SWEET EARLY 1`: entered `arm_init` with MMU on; 2: platform expert done; 3: CPU topology done; 4: CPU bootstrap done; 5: startup bootstrap done; 6: CPU init done; 7: entering `arm_vm_init`; 8: leaving bootstrap TTBR0.
- `SWEET EARLY PANIC:` prints a copy of panic arguments before normal panic handling needs per-CPU/thread state.
- In patches 0010/0011 the console disabled before TTBR0 invalidation; patch 0012 replaces that with a persistent kernel VA alias and diagnostics through normal serial/kprintf startup (see its legend above).
- Screen contents require a physical observation from the user: adb cannot capture a framebuffer after Android has been replaced by a bare-metal kernel.

## QEMU emulation (2026-10-15)
- `tools/mkadt.py OUT.adt --qemu` = same ADT with QEMU virt GICD 0x08000000 / GICR 0x080a0000 / CNTFRQ 62.5 MHz.
- `tools/qemu-sweet.py PAYLOAD OUTDIR [--secs N] [--hmp "x /8gx ADDR"] [--gdb]`: host-side copy of shim/loader.c
  (segments + ADT + boot_args at their PAs, 6-instruction stub sets x0 and jumps), `-M virt,gic-version=3 -cpu cortex-a76`
  (same ID_AA64MMFR0 as Kryo 470), 4G RAM so 0x9c000000 fb and 0xb0000000 kernel are plain RAM.
  Outputs `fb.png` (the screen), `regs.txt`, `qemu.log` (exceptions with ESR/FAR/ELR). `--gdb` halts with a stub on :1234
  (system `gdb` + `set architecture aarch64`, `hbreak *VA` works with the MMU on).
- Not emulated: Qualcomm XPUs, ABL, real panel, PSCI conduit (QEMU virt uses hvc). Hardware still decides those.
- Example: `python3 tools/mkadt.py /tmp/q.adt --qemu && python3 tools/mkpayload.py xnu/out-0013/kernel.development.sweet /tmp/q.adt /tmp/pq.bin --cmdline 'debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam' && python3 tools/qemu-sweet.py /tmp/pq.bin /tmp/q`

## Layout
- `backup/`  partition dumps + full sdb..sdf LUNs, `SHA256SUMS` (all verified on-device == local)
- `dtb/`     live FDT (from OrangeFox), PixelOS boot.img dtb, dtbo, dts versions
- `shim/`    bare-metal boot shim (`./build.sh` -> `out/shim-boot.img`)
- `research/` mainline sm7150 dts, XNU pe_fiq.c etc, PocketDarwin patches
- `tools/unpack_bootv2.py`

## Device access
- No root in PixelOS 17 (user build, adb root disabled). Root = `fastboot boot recovery/recovery.img` (OrangeFox R12.0_2, temp boot, not flashed).
- `fastboot boot` works for stock boot.img, OrangeFox, and our shim.
- Shim resets itself via PSCI SYSTEM_RESET; IMEM restart_reason 0x77665500 is seen
  (bootreason=bootloader) but ABL still boots Android, so it does not stop in fastboot.

## Hardware facts (live DT + shim measurements)
- Framebuffer (ABL cont splash): 0x9c000000, 1080x2400, stride 4320, a8r8g8b8, panel is DSC *video* mode (no kick needed).
- RAM: 0x80000000+2G, 0x100000000+0xfcc00000. Live reserved-memory = PixelOS base dtb + sweet dtbo fixups.
- GICv3: GICD 0x17a00000, GICR 0x17a60000 (stride 0x20000, 8 PEs). GICD_TYPER 0x007B041D: SecurityExtn=1 (DS=0).
  ABL leaves GICD_CTLR = 0x10 (ARE_NS on, groups off). Writing 0x13 reads back 0x12: EnableGrp1A (bit1) is the one that matters.
- GIC Group 1 VERIFIED (shim 2c): vtimer INTID 19 = 30 IRQs/300ms, ptimer INTID 18 = 30, SGI 1 to self = 10/10.
  ICC_SRE=1 already, ICC_CTLR 0x8440, priority/enable writes stick. GICR_IGROUPR0 and GICD_IGROUPRn read 0 from NS
  even though Group 1 delivery works (RAZ in NS view) - do not use them as a health check.
  INTID 28 shows pending constantly (unknown PPI 12).
- Timer 19.2 MHz. Qualcomm PPI numbering: secure-phys/phys/virt/hyp = PPI 1/2/3/0 -> vtimer INTID 19, ptimer 18 (NOT 27/30).
- PSCI 1.1 via SMC, SMCCC 1.1. CPU_ON ok; SYSTEM_SUSPEND and SYSTEM_RESET2 unsupported.
- SMP: CPU_ON works for all 7 secondaries (MPIDR 0x81000n00).
- Entry EL1, MMU off, x0 = DTB (magic ok). ID_AA64MMFR0 = ...101122: 16K granule YES, 4K YES.
- No serial console: debug UART SE is in SPI mode on sweet. Screen is the only debug channel.

## XNU implications
- XNU GICv3 path (pexpert/arm/pe_fiq.c, VMAPPLE) puts the timer (PPI 27) in Group 0 = FIQ. With SecurityExtn=1,
  Group 0 is secure -> impossible from NS EL1. Need: Group 1 IRQ, ICC_IGRPEN1, INTID 19, and the timer handled in the IRQ path.
- XNU VMAPPLE PSCI uses `hvc`; sweet needs `smc`.
- PocketDarwin NonAppleGenericCpu.patch only swaps -mcpu=apple-h7 for -march=armv8-a.

## Milestone 3 (2026-10-14): 16K MMU verified
- ABL loads the Image at 0x80080000 (base 0x80000000 + 0x80000) regardless of text_offset=0.
- 16K granule, T0SZ=28 (L2 start, 32MB blocks), MAIR 0x44ff00, TCR 0x1409CB51C, PARange=2 (40-bit PA).
- Only own image pages WB (Qualcomm XPUs: never map carve-outs cacheable); fb Normal-NC; rest Device.
- AT S1E1R checks all as expected; unmapped -> translation fault L2.
- Fill fb: 110 ms (MMU off) -> 3.5 ms (NC). Memory loop 70 ms -> 0.7 ms. vtimer + SGI IRQs still fine with MMU on.

## XNU loader design notes (xnu-8796)
- Entry: `_start` -> `start_first_cpu`, x0 = boot_args (PA), MMU off, EL1. boot_args: Revision/Version 2,
  virtBase/physBase/memSize/topOfKernelData, Boot_Video (fb 0x9c000000, rowBytes 4320, 1080x2400, depth 32),
  deviceTreeP/Length (ADT), CommandLine[608], bootFlags, memSizeActual.
- start.s maps [kernel mach header .. physBase+memSize) with cacheable BLOCKS. On Qualcomm every XPU carve-out
  must therefore lie below the kernel: use physBase = 0xb0000000 (above ramdump_fb 0xaf000000+16M,
  cdsp_sec, qseecom), end before rmtfs 0xfde01000 -> memSize ~0x4d000000 for bring-up.
- Base ADT: iPhone 8 d20ap DeviceTree from iOS 16.7.16 IPSW (tools/remotezip.py + tools/adt.py; LZFSE payload).
  Template values are empty: the loader must fill chosen/*, memory-map, cpu*/reg etc. like iBoot.
- XNU build: GitHub Actions (dipa-studio/ios-sweet, xnu.yml) via blacktop/darwin-xnu-build, MACOS_VERSION=13.5.
  New-clang fixes in xnu/patches; Xcode 16 iig is incompatible (IORPC.kernelContent) -> macos-14 runner, oldest Xcode.

## Kernel architecture plan (SWEET machine config)
- First stock build (VMAPPLE) is arm64e (PAC). Kryo 470 = ARMv8.2, no PAC -> SWEET must build plain arm64
  (HAS_APPLE_PAC is only defined for __arm64e__, so arm64 drops PAC automatically).
- Template: VMAPPLE (16K pages, GICv3 code in pe_fiq.c) minus APPLEVIRTUALPLATFORM hypercalls/paravirt PAC,
  plus BCM2837 (generic ARM: no APPLE_ARM64_ARCH_FAMILY Apple sysregs). Do not link the KDK lib<cfg>.os.*.a.
- Timer: XNU expects it on FIQ (sleh_fiq -> rtclock_intr). SWEET: GIC Group 1, so sleh_irq must ack ICC_IAR1,
  route INTID 19 to the same timer path, SGIs to cpu_signal_handler, EOI via ICC_EOIR1.
- SMP/interrupts: open-source AppleARMSMP (USE_APPLEARMSMP) needs an IOInterruptController (AIC role) and an
  IOPMGR (CPU power -> PSCI CPU_ON via SMC). Plan: built-in IOKit classes (SweetGIC, SweetPMGR, platform
  expert) compiled into the kernel and matched via iokit/KernelConfigTables.cpp -> no closed kexts needed.
- Debug: xnu patch 0003 sweet-fbcon = kprintf drawn on the framebuffer (ADT node "sweet-fbcon").
- ADT /arm-io ranges must not map to parent 0 (pe_arm_get_soc_base_phys()==0 disables serial): use parent 0x1000000.

## Status 2026-10-14 late: first XNU boot attempt
- SWEET kernel builds (CI run 38635343732, patches 0001-0009). Verified offline: arm64, no PAC, no >v8.2 code,
  70300 classic relocs all in range (slide 0 = no loader work), entry = __start.
- Boot: shim loader worked (initrd 0x846F0000-0x855FE3E0, BOOTARG 0xB1FF8000, JUMP 0xB146A488) but screen stayed
  red -> xnu died/hung before serial_init (fbcon). kprintf is a no-op before STARTUP_SUB_KPRINTF.
- Historical next-step plan (patch 0010, now implemented and built; see active handoff above): early fb debug channel
  1. start.s start_first_cpu (after mov x20,x0 / mov x21,#0): PA marker band (MMU off, direct store to 0x9c000000).
  2. start.s after both create_bootstrap_mapping calls: add ONE block entry to the existing V=P L2 table
     (x25 + 2*PGBYTES; do NOT call create_bootstrap_mapping again: fb and RAM share L1 entry 0, it would replace
     the L2 table). Entry = TYPE_BLOCK|VALID|SH(OUTER)|ATTRINDX(CACHE_ATTRINDX_WRITECOMB=1)|AF|PNX|NX|0x9c000000,
     index = (va & ARM_TT_L2_INDEX_MASK) >> ARM_TT_L2_SHIFT.
  3. new osfmk/arm64/sweet_early.c (+files.arm64 entry): sweet_early_mark(n) colored bands at screen bottom and
     sweet_early_putc using iso_font via VA=PA while the bootstrap TTBR0 is live; disable before
     arm_vm_init's set_mmu_ttb(invalid_ttep) (arm_vm_init.c:2148).
  4. markers in arm_init after PE_init_platform, ml_parse_cpu_topology, cpu_bootstrap, kernel_startup_bootstrap,
     cpu_init, before arm_vm_init.
  5. panic hook at top of panic_trap_to_debugger: va_copy + _doprnt(fmt, &copy, sweet_early_putc, 16).
  Verify with tools/xnu-cc.py (compile+symbols) and patch-order simulation before the single CI run.

## IRQ 硬件基线（2026-11-03）
- 新增 `docs/KEY-IRQ-HARDWARE-BASELINE.md`，集中记录 MH2LM Power、Volume Down、
  Volume Up 的 GIC、DTS、PMIC PON/GPIO6、PMIC-arb v5 资料、已证实的 IRQ 位以及
  禁止推测写入的未知项。三个按键都通过 PMIC-arb 的 DTS SPI 481（XNU INTID 513）
  汇聚；SoC TLMM 不属于按键 IRQ 链路。
- DTS 已确认 PON KPDPWR=bit0、RESIN=bit1、PM8150 GPIO6=bit0；实际 APID 分别为
  PON 248、GPIO6 261。`RT_STS` 的实时电平已在 `sweetlog-0113-baseline.bin` 通过
  三键按下/松开验证。
- 仍不得 arm：PON/GPIO6 现有 gate/latch/owner 状态、PMIC-arb v5 清除顺序、PDC
  input 和 GIC 上游 trigger 仍需 0128 的只读 `sweetkeysirqdiag=1` 预检。0128 输出的
  `latched` 是 `LATCHED_STS (+0x18)`，不是写入用的 `LATCHED_CLR (+0x14)`。

## 0120a6 电池证据与共享 mux 故障（2026-11-07）

`sweetlog-0120a6-battery-evidence.bin`：11 次 FG profile tuple 全部
`valid=1/error=0/match=1`，Tocad 24 字节前缀一致，integrity `0x13`、nominal
4027 mAh。输入快照没有 DCP/CDP，只见电脑 SDP、UNKNOWN 和普通充电器 OCP，
策略保持 `sink-ok=0`，没有控制写入；这轮仍是诊断证据，不是充电验收。

第二次电脑 USB 插入只有重复 bus reset/connected，没有 ACM endpoint 配置，按已知
DWC3 gadget 枚举缺陷单独跟踪。日志末尾 ADC5 token 373 已得到有效、恢复完成的
样本，随后 PMIC mux 报 `handled=1 pending=1` 并终止 keys/battery/ADC consumer。
0120a6 没有记录故障后的 route/APID/status/ack 读回，暂不能判断 ADC、按键或电池
route；下一轮先补充这些取证并实现按 group 隔离候选，避免共享 mux 的一个 pending
位停掉无关 consumer。
