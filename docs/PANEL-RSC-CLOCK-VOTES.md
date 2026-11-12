# 0210：RSC 分支自身投票与受控 AHB 父时钟

**0121a23产物/镜像已核对，待实机：** 78源码匹配0210（7d35162），114 source/177 binary markers通过。
ABI608固件、ADT和605字节retained参数保持a22；boot/FAT回读通过。本轮只启动retained约90秒，
验证RSC clock Held/mask0x381，取NoC后续首错或新DMA证据；尚未证明完整接管成功。
镜像路径和取日志命令见[0121a23交付](PANEL-D1-0121A23.md)。不需再次云编译，其他显示测试继续暂停。


2026-11-12。接 [0121a22 实测](PANEL-D1-0121A22.md#实测首错定位到-rsc-non_gdsc_ahb-分支准入)。
针对 `branch-unowned branch=7 cbcr=0 source=4 link=0`：原分支控制只允许从已停止的XO父源恢复，
没有对接 core profile2 已建立的 PLL1/75MHz AHB；同时把 NON_GDSC_AHB 的共享投票状态当作独占 halt 状态。
本补丁实现该路径，尚未云编译或实机，不能据组件测试宣布完整面板接管成功。

## 架构和权限

仍由 SweetMDSS 唯一 IOService 所有者、IOCommandGate、IOWorkLoop 和原 clock timer 处理。
NoC 内部通过原 retainLocalClocks / localClockStatus / releaseLocalClocks 获取、查询、撤回 token。
不新建旁路 clock 服务、不向 consumer 暴露 MMIO、不改 apps RPMh IRQ 或 display TCS 命令格式。

固定分支表显式标记 `voted` 和 `managedAHB`，不从本次观测值推测分支类型：

| 分支 | CBCR | 类型 | 新增父源权限 |
|---|---:|---|---|
| NON_GDSC_AHB / branch7 | DISPCC+0x4004 | voted，LG BRANCH_VOTED | core2 受控 PLL1 AHB |
| RSCC_AHB / branch8 | DISPCC+0x400c | halt，LG BRANCH_HALT | 同上 |
| RSCC_VSYNC / branch9 | DISPCC+0x4008 | halt，LG BRANCH_HALT | 保持原XO策略 |

依据 LG `drivers/clk/qcom/dispcc-sm8150.c:1408/:1480/:1498` 及 `clk-branch.c:79/:114`。
AHB parent map3 的 source4 为 PLL1 main，root +0x22bc，core2 配置为75MHz。
其他分支未获得开启任意PLL或PHY父源的权限；已运行的继承分支继续按原语义保留。

PLL1新权限必须同时满足：表声明 managedAHB、source4、AHB受控CFG、core profile2、实际coreReady，
以及通过 SweetRPMh typed 状态接口核验的 ARC Turbo/三路ACK/成功/非uncertain/enable、
当前CN0完成/成功/非uncertain且至少300000000 bytes/s。硬件当前运行不能代替上游票据。

NoC新增 `fNoCRegisterToken` 保存其最近一次确认的实际CN0 token：
新请求受理时清除旧凭据，确认Complete后保存；原 in-flight `fNoCAppsToken` 生命周期不变。
不使用已经被NoC请求替换的旧 `fCoreCN0`，也不只依赖一个 software ready 标志。
worker等待、获取最终核验及实际NoC准入中，自己取得的分支仍需满足父配置和票据约束。

## 获取、撤回与失败

- 全部分支预检完成才开始写入。普通halt分支仍要求原停止状态；voted分支允许自身enable0、
  aggregate ON或OFF，但须真正取得自己的enable票。写入仅RMW bit0，保留其他位。
- 每次写前复核父源、物理空闲和分支条件；完成须自身enable读回且CLK_OFF清除。
  posted write、自身票或aggregate ON单独均不能产生Held。
- 释放按引用计数逆序处理，只撤自己新增、且最后引用的enable；继承票和其他用户引用不撤。
  普通halt仍等待实际OFF；voted读回自身enable清除后，以原异步timer等待至少10us，
  不强求aggregate OFF，不强制清除其他投票或关闭父源。
- `fClockVoteSettleAt`只记录本次清除读回后的稳定期限；晚到/过早回调不能跳过该期限。
  `fClockReleaseMask`保存本次拟撤的分支，获取和释放完成时都整体复核，防止早处理的分支后来漂移。
- 失败仍关写入、保留不确定资源，不回滚或修复继承路径；释放失败保留本次撤回的所有权账本。
  stop只关闭准入、取消事件并标Stopped，不把停止服务当作DMA退出或自动撤票。
- 单分支10ms、整次200ms期限不增加，无忙等、sleep、额外扫描轮数或静默fallback。

新增INFO `SWEET MDSS CLOCK: voted branch=... enable=...`说明请求已发出而非已经完成。
ERROR仍无条件输出，NoC错误CONTEXT新增纯软件 `register-token`；保留0209具体分支错误。
无新映射/未知MMIO探测；新增操作只使用已声明的DISPCC分支、既有coreReady检查和typed票据查询。
未改固件ABI608、ADT、启动参数、USB、按键、触摸、电池、SPMI mux或CircleCI。

## 本地验证

- 新专项43组场景ASan/UBSan通过：真实cbcr0及附带其他位的aggregate ON/OFF、受控PLL1 AHB、
  无权限的继承PLL/PHY、ARC/CN0凭据缺失/过期/不确定、父源和原始CFG漂移、写入前状态变化、
  丢失enable或物理ON、获取最终复核、实际DATA撤回/CN0 Off/新凭据重新获取、共享引用、
  voted自身撤票与至少10us settle、外部aggregate持续ON、普通HALT保持ON的失败、释放最终复核及stop/晚回调。
- 原10套显示/生命周期回归通过；0209的29准入场景+3完整失败路径通过，共12套。
- 真实XNU/KDK ARM64 MDSS对象基线及修复版编译通过，warning1543→1547，新增4条
  unsafe-buffer-usage数组索引提示，未屏蔽；无新增格式或编译错误。
- 0185–0210严格累计应用无offset/fuzz，78源码字节一致，114原CI source markers通过。
- 打包脚本bash语法检查及旧a22 kernel拒绝检查通过；旧kernel在输出镜像前被明确拒绝，未生成冒充修复的boot。
- CircleCI blob保持 `da0b6c0947a66f03491b3a9023590b49665292df`。

底稿 `scratch/framebuffer-0210/`：verification/tests/cumulative-replay/warning-comparison JSON、
编译及各套测试日志、matrix-old-kernel-rejection.txt。专项
`tools/tests/test-mh2lm-rsc-clock-votes.py` 使用实际core/NoC/clock provider，MMIO和硬件完成是独立对端。
本地单文件对象和模拟硬件不代表整核链接、真实电压、共享投票者身份或实机完整接管。

## 下一步

用户push并云编译，下载新kernel/buildtree后核对0210，仍用原ABI608、0121a21/22 ADT和605字节参数打retained。
打包工具现在要求0209及0210 binary markers，会拒绝旧kernel冒充新修复，不改变任何启动参数。
只启动约90秒导完整USB或持久日志：检查RSC clock Held/mask0x381，再看NoC wrapper/solver/TCS及后续首次错误或新DMA。
这次尚未观测到的硬件条件仍可能成为后续首错；不为通过测试删保护、延长期限或强行触发。
完整接管前亮度、OFFON、长测、反复USB及combined/supply仍暂停，旧a22和identity不重测。
