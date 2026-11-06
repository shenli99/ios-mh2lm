# iOS 16 on sweet (Redmi Note 10 Pro, SM7150) - working notes

## Agent handoff / active work (2026-11-06, latest)

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
