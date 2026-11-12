# 0209：display NoC 准入失败诊断

**0121a22产物已核对并打包，待实机：** 78源码匹配0209（a325219），114 source/173 binary markers通过。
固件ABI608、ADT和605字节retained参数保持0121a21；boot/FAT回读通过。
本轮只启动retained约90秒取首条CLOCK/NOC gate、CONTEXT和原NoC错误；仍属诊断，未证明修复。
镜像路径、校验值和USB命令见[0121a22交付](PANEL-D1-0121A22.md)。不需再次云编译；接管前其他显示测试继续暂停。


2026-11-12。本地验证完成，尚未云编译或实机。接
[0121a21实测](PANEL-D1-0121A21.md#实测结果apps-rpmh通过display-noc准入失败)：
apps RPMh 的10次真实完成IRQ、四LDO/ARC/CN0及core profile2通过；下一步display NoC返回
NotReady/data ACK mask0，但原日志没有区分RSC clock与RSC/wrapper条件。

0209为这个首错补分支证据，**不放宽准入，不宣称已修复NoC或完整接管面板**。

## 新日志及判断

| 日志 | 含义 |
|---|---|
| `SWEET MDSS NOC ADMIT: gate=clock-retain` | RSC时钟请求拒绝；结合紧前的CLOCK gate区分原因 |
| `gate=clock-status` | clock token查询失败或clock lease Failed/Stopped，记录token/state/mask/result |
| `gate=clock-service` | provider接受状态、失活、clock fault或clock token不符合 |
| `gate=clock-prerequisites` | 域/总线/core等前置失败；紧前CLOCK gate包含原始检查值 |
| `gate=clock-lease` | RSC clock lease不是Held或mask不匹配，含查询返回值 |
| `gate=clock-parent / clock-parent-drift / clock-branch` | root状态/来源、父配置变化或分支enable/OFF不符合，含branch及当次值 |
| `gate=rsc-layout` | 原始RSC id/config与major/count/NCPT不符合 |
| `gate=wrapper-override / wrapper-power / wrapper-control` | wrapper override/电源/控制保护触发，记录三个既有读值 |
| `gate=sequencer-busy / solver-status` | 对应RSC +0x404或+0xd0非零，记录当次值 |
| `SWEET MDSS NOC CONTEXT:` | 失败时软件上下文，关联原token/rc/ACK错误记录 |

clock helper新增内部`ClockDiagnostics`枚举：Silent保持运行时默认安静，Admission保留首次warm
启动的完整信息，Failure仅为NoC输出失败检查。NoC时钟worker及最终检查也记录原始branch
核验失败，成功timer不重复输出。现有ERROR无条件输出，原成功INFO仍受日志等级控制。

CONTEXT包含phase、RSC clock token、apps CN0 token、register-acked、cursor/count、group、TCS、
executing、operation/posted以及clock worker状态，**只读取软件字段，不补读硬件**。
phase沿原枚举：0 Idle、1 AppsRaise、2 Clock、3 Data、4 ClockRelease、5 AppsLower。
未开始group时部分TCS/group字段可能尚无本轮含义，必须结合phase/executing/operation判断。
register-acked=1表示配置CN0已完成，不能代替display-data ACK。

## 实现边界

生产补丁仅修改`SweetMDSS.cpp`的私有声明及`SweetMDSSClock.inc`、`SweetMDSSNoC.inc`。
没有新增成员变量、寄存器读取或写入、启动参数、timer轮数/期限、触发、ACK、solver/电源控制。
NoC原有复合判断拆成相同顺序的条件，仅记录原检查已取得的值，不在失败后重新取样。

依旧先通过clock/domain/lease/parent/branch，再访问display RSC；RSC layout通过才读wrapper。
wrapper gate拒绝后不读sequencer/solver，sequencer busy拒绝后不读solver。
Failure模式复用原clock helper，因此不为详细日志越过原short-circuit边界。
失败收尾新增一次CONTEXT（Aborted不输出该增量），晚timer保持静默；无自动恢复、降档或清状态。

apps RPMh IRQ路径及typed consumer接口保持0208。USB/按键/触摸/电池/SPMI mux、固件ABI608、
ADT与605字节retained参数、CircleCI均未修改。

## 验证

- 新专项运行真实NoC/clock provider及MMIO对端：29个准入场景、3条完整失败路径。
  诊断版与0208基线**完整MMIO轨迹及结果逐字节相同**，并检查具体gate、日志条数、
  不越过clock失败访问RSC、不越过wrapper/seq失败读solver、无故障后payload写及晚timer静默。
- 10套真实源码ASan/UBSan回归通过：display-clocks/core/noc、managed-display、panel-lifecycle、
  link-power-cycle、combined-suspend、supply-lifecycle、resource-parking、profile-lifecycle。
- MDSS使用真实XNU/KDK头的ARM64单文件对象前后编译通过；warning1538→1543，新增5条
  unsafe-buffer-usage提示来自诊断日志中的既有有界数组索引，未屏蔽，无新增格式/编译错误。
- 0185–0209严格累计应用无offset/fuzz；78源码字节一致，114现有CI source marker通过。
- 本地底稿`scratch/framebuffer-0209/`：verification/tests/cumulative-replay/warning-comparison JSON、
  两份对象编译日志及专项测试输出。单文件/模拟对端不代表整核链接或硬件验收。
- CircleCI blob保持`da0b6c0947a66f03491b3a9023590b49665292df`。

## 下一轮

用户push并云编译，下载kernel/buildtree后核对0209源码及`SWEET MDSS NOC ADMIT:`、
`SWEET MDSS NOC CONTEXT:`，沿用ABI608及0121a21 ADT/参数重新打包新retained镜像。
仅启动等约90秒取完整日志，重点是第一条CLOCK/NOC gate、紧接CONTEXT和原NoC token错误。
本轮仍可能静帧；若接管成功也保留完整日志再决定显示测试。不重复刷0121a21或identity，
完整接管前亮度/OFFON/长测/反复USB及combined/supply继续暂停。
