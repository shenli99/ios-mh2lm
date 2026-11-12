# PD3：供电共享关系与最后一票关闭保护（0199）

**0200供电依赖校验增量（2026-11-12，本地通过，未实机）：** Pins 新增实时 reset-low
查询，RPMh 新增仅 VDDI/VPNL 的 typed panel vote/取消接口，逐写核对同代 Pins/Touch token，
保留依赖引用，异常停止并保留部分事务；不撤共享 L3/L5。22套检查及八个真实 arm64
单文件对象通过。**尚未接入协调器供电释放/恢复，未新增档位，mode0–5正常路径不变。**
下一步接 LG 顺序、部分撤票账本及联合完成证据；E7/E8仍未实机。详见
[接口、锁顺序、失败边界及验证](PANEL-SUPPLY-FENCE-D1.md)。无新 FD/boot、push/CI/设备操作，
CircleCI 未改，暂不为接口单开云编译。以下旧阶段按各自范围保留，当前以此段及NOTES为准。

2026-11-12，接0198。核对LG MH2LM参考源码后，明确了不能把四路显示供电一起撤回：
**DSI host的L3和PHY的L5也给UFS及USB PHY供电。** XNU这些驱动当前没有在SweetRPMh登记
各自的依赖票，显示consumer成为软件账本的最后一票，不代表它是硬件唯一使用者。

本轮交付`0199-mh2lm-shared-rail-disable-guard.patch`：在原RPMh provider内保护这两路的最后
aggregate disable，并提供typed元数据。未新增休眠档位、未实际撤电；mode0–5的正常执行路径保持。
仅本地验证，未实机，没有修复或归因任何旧USB/充电异常。

## 供电关系：参考源码证据

参考仓库`/home/shenli/work1/ref/android_kernel_lge_sm8150`，基线
`d4f335e13d73c4aa2af85581a6ec11b670ee5b94`。下表是源码连接，不是本轮电压测量；
没有重新验证旧解码DT的提取来源，且其中UFS节点为disabled、缺少这些overlay属性，
不能用该旧文件否定板级overlay或宣称已拿到当前设备最终生效DT。

| 资源 | 显示用途 | 其它源码使用者 | 当前结论 |
|---|---|---|---|
| `ldoa14` / PM8150 L14 | panel VDDI，1.8V | `sde_dsi`/`sde_dsi1`亦有`vddio-supply`同名映射 | 这些是主/次DSI节点，不能误写为DP；次路是否active须另证。未证明面板与触摸物理隔离 |
| `ldoa17` / PM8150 L17 | panel VPNL，3.0V | MH2LM touch节点未列同名regulator；使用GPIO电源控制 | 尚未找到另一个本板显式使用者；这不是独占或可安全断电证明 |
| `ldoc3` / PM8150L L3 | DSI host analog，1.2V | UFS `vdda-pll`；USB QMP `core`；DP `vdda-1p2`，还有CSI/其它SoC候选节点 | 已确认跨功能共享，阻止最后XNU票下发disable |
| `ldoa5` / PM8150 L5 | DSI PHY analog，0.88V | UFS `vdda-phy`且`vdda-phy-always-on`；USB HS/QMP `vdd`；DP `vdda-0p9` | 已确认跨功能共享，阻止最后XNU票下发disable |

准确来源：

- `arch/arm64/boot/dts/lge/sm8150-mh2lm/sm8150-mh2lm_common/sm8150-mh2lm_common-storage.dtsi:15`，
  UFS PHY两路供电、always-on及90200/19000µA需求。
- `arch/arm64/boot/dts/qcom/sm8150-usb.dtsi:138`，HS PHY L5/880mV；`:160`，QMP L5/L3。
- `arch/arm64/boot/dts/qcom/sm8150-sde.dtsi:423`起的host/PHY节点，以及`:583`的DP节点。
- `arch/arm64/boot/dts/qcom/sm8150-regulator.dtsi:199`和`:753`，相同command DB资源的
  L5/L3 provider、ACTIVE/SLEEP别名与proxy配置；不能将Linux proxy flag当作XNU实际持票证明。
- `arch/arm64/boot/dts/qcom/sm8150-sde-display-lge.dtsi:142`和`:173`，L14在主/次DSI节点。
- `arch/arm64/boot/dts/lge/sm8150_common/sm8150_common-panel.dtsi:37`的VDDI/VPNL表，
  `:219`资源映射和`:485`起四版R66456覆盖。

DP/CSI/备用端口的定义不等于本机全部启用；UFS承载当前iOS根卷，USB用于现有外设及日志路径，
不能因显示静止就替它们断电。没有用推测的baseline票伪造这些驱动已经登记。

## 偏压与触摸：已明确和仍未知的部分

四版allowlist R66456 profile均选`dsi_panel_pwr_supply_sm8150`，只有VDDI/VPNL，带
`lge,use-labibb`和`lge,reset-after-ddvd`。LG override实际调用的是该`power_info`，
**打印“LABIBB”不代表访问了独立LAB/IBB peripheral。**
common-pm将`pm8150l_lcdb`禁用；四版panel文件尾部的PMI8998 LABIBB片段在注释内。
当前没有新增PMIC偏压写入依据，也不能从disabled节点推断板上不存在外部偏压器件。

MH2LM的`sm8150-mh2lm-touch-common.dtsi`选择GPIO152(VDD)、59(VCL)、54(reset)、122(IRQ)。
`touch_hwif.c:99/119`在GPIO有效时直接走GPIO，regulator仅为fallback；
`synaptics/s3618/touch_s3618.c:3370`的独立power-off先reset低、10ms、VDD关、10ms、VCL关，
power-on按实际代码先VCL、10ms、VDD、10ms、reset高。注释有歧义时按代码记录，不擅自换序。
这些GPIO不同于panel reset6/TE8，但GPIO下游的物理供电来源仍未确认。
现有XNU touch quiesce只是IRQ/线程/HID静止，不是deep sleep、供电隔离或断电恢复证明。

LG完整panel下电顺序也不能混用通用weak实现：对于这些`lp11-init` profile，
`dsi_display_unprepare()`先调LG `dsi_panel_unprepare()`，其中`dsi_panel_reset_off()`在完整
power分支把reset拉低并等待；随后才停host/link/PHY/core，最后
`dsi_panel_post_unprepare()` → `dsi_panel_power_off()`关闭所选regulator表。
`dsi_pwr_enable_regulator()`按表逆序disable，panel两路为VPNL→VDDI。
当前0198没有执行这个reset-low/rail-off尾段，不能把已有Dormant完成当作它已发生。

## provider实现与可观察行为

沿原`SweetRPMh : IOService`唯一apps-DRV writer、gate、TCS账本和typed
`resourceInfo()/requestVote()/copyStatus()`，没有全局实例桥、原始地址接口或另一个锁。

`SweetRailInfo`追加`lastDisableRequiresHandoff`：Host/PHY为true，VDDI/VPNL为false。
false仅表示没有本次已确认的跨功能禁关规则，不是关闭许可、物理独占或rail-off证据。
provider发布`SweetRPMhLastDisableBlockedMask=0xc`，其中bit2/bit3是typed Host/PHY枚举。
ADT及`SweetRPMhABI=1`未变；新增字段是同内核一起编译的内部typed接口，未提供用户ABI。

聚合所有已有consumer票后，若target将disabled、该rail已经被本provider取得并仍enabled，
且规则要求handoff，则返回`kIOReturnNotPermitted`，输出token保持零。检查发生在分配token、
修改请求状态/consumer账本/aggregate/旧status、选择TCS和任何MMIO读写之前。

规则不改变：

- 尚未取得过的继承rail上，空撤票仍是原无硬件动作的no-op。
- 仍有另一个enabled consumer时，撤自己的票仍按原聚合/ACK路径执行。
- VDDI/VPNL的原typed disable及fresh ACK路径保留；是否应由面板请求，由协调器的实际前提决定。
- 拒绝后旧票、旧完成token及status不变；`detachConsumer()`仍返回Busy，不假装已经撤票。
- startup/metadata不投票，stop/取消仍保票，无自动降压/disable/重试或虚构永久baseline consumer。

这只是明确拒绝不安全的最后disable，不是完整的继承电源状态导入、共享电压/功耗管理或IOPM。
将来若要撤显示的最后模拟电源票，须让真实UFS/USB等持有自己的依赖票，或通过统一provider
完成可验证的ownership handoff；当前没有绕过参数。普通显示休眠可先保留L3/L5。

## 验证与下一步

严格重放0199到0198verify，2份源码变化、114原CI source marker通过。
21套本地检查通过，20实际源码ASan/UBSan加Python ADT检查，无最终报告；
RPMh测试覆盖只读元数据、四路/all8consumer聚合、已知共享最后撤票重复拒绝、无token/账本/status/
MMIO副作用、非最后撤票及detach、无历史no-op、面板两路真正disable必须fresh ACK。
原mode0–5面板/链路/资源/FB回归通过。LSan ptrace限制关闭，另核对对象/映射/通知/分配归零。

真实XNU/KDK arm64 RPMh/MDSS/FB的base+final六个单文件对象编译通过。
RPMh warnings1321→1325，新增4处有界数组提示；rail经枚举范围检查，sharedMask循环r<4。
MDSS1527、FB1397不变。没有抑制警告；不是整核链接或实机验证。
底稿`/home/shenli/work1/scratch/framebuffer-0199/`含连续性、严格应用、测试、对象/符号、警告审查、
源码来源SHA256与交付结果。无新FD/boot、push/CI/设备操作，CircleCI未改。

下一步在保留L3/L5的前提下，设计面板两路VDDI/VPNL的受控撤票/恢复：补reset-low真实证明、
LG时序、同代touch fence、部分释放账本与失败停止，再衔接现有DATA/Link/Host恢复。
E7复位后触摸、E8物理共享/偏压、真实rail-off、冷NoC/MDP与ABL退出仍未验收。
未知项不通过扫描PMIC或启动Android解决；后续实机仍须纳入0195实际DarwinBoot身份链。
