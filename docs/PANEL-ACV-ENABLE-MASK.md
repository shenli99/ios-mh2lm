# ACV 开关型 BCM 修复（0205）

**0121a18实测/0206增量：** ACV width0/mask8元数据已通过；首次生命周期准入仍NotReady，
触摸worker稍后才ready，原framebuffer一次性请求永久失败。0206仅协调器接收首次Takeover后
异步等待worker，真实quiesce证明前不改显示/投票；超时/退出仍失败。待push云编译，不复测旧镜像。
见[日志、复现及修复边界](PANEL-TOUCH-STARTUP-WAIT.md)。以下0121a18待测步骤已完成。

**0121a18已核对并打包：** 78源码与0205一致、114 source/155 binary markers及镜像回读通过。
待启动 `boot-16k-0121a18-abi608-panel-retained.img`，先取一分钟启动日志及画面，确认ACV准入
和managed扫描接管。ABI608固件、605字节参数及ADT保持；不重测旧诊断，combined/supply暂停。
详见[镜像、命令和日志目标](PANEL-D1-0121A18.md)。以下push/编译步骤已完成，实机尚待验收。

2026-11-12。接0121a17真实日志：ACV `address=0x5000c/unit=1/width=0/VCD=11`。
本次按用户明确要求采用 `0x8` 开关掩码，修正统一带宽资源模型；不把真实width填成4或16。
代码已本地验证，尚未由新云端内核或设备验收，不能称完整面板接管已成功。

## 依据与范围

用户提供[Linux补丁讨论](https://lkml.indiana.edu/hypermail/linux/kernel/2311.3/02570.html)，
引用ACV width0使普通缩放分子为0，downstream使用enable_mask规避。
本环境下载网页失败（Operation not permitted），本记录仅引用用户提供的片段，未通读补丁正文，
也未独立核实所有RPMh SoC均用相同mask的范围。

本地LG/Lineage两树的 `arch/arm64/boot/dts/qcom/sm8150-bus.dtsi`：
apps ACV和display ACV_DISPLAY节点都没有enable-mask/bmask属性；display仍查询DB key `ACV`。
`drivers/soc/qcom/msm_bus/msm_bus_arb_rpmh.c` 定义
`BCM_TCS_CMD_ACV_APPS 0x8`，`bcm_update_acv_req()`明确使用X=0、Y=mask。
原LG仅apps节点进此分支，display走普通循环并可保留前一个BCM的累计值。
本次是**按用户决定将display ACV建模为mask8资源**，不是声称原LG display已有完全相同实现，
也不声称与未取得正文的Linux补丁逐位一致。
旧“display分支普通缩放，所以不能用mask8”的实现结论由本次策略取代。
历史源码复现保留于[width来源核对](PANEL-BCM-WIDTH-SOURCES.md)，不照搬跨BCM累计副作用。

## 实现与所有权

交付补丁：`xnu/patches/0205-mh2lm-acv-enable-mask.patch`，只更改三份XNU源：
`SweetRPMh.h`、`SweetRPMh.cpp`、`SweetMDSSNoC.inc`。

- RPMh的资源描述表明确ACV `enableMask=0x8`，其他五个BCM为0。
  `SweetBCMInfo`在provider内部保留真实width/unit/VCD及独立mask；不增加raw寄存器接口。
- 查表仅对明确有mask的资源允许width0；unit非零、VCD<16、reserved为0、type、地址对齐、
  aux恰好8字节及边界、名称唯一和地址不冲突检查保持。ACV即使将来width非零也仍走开关模型。
- typed `encode()`：ACV聚合EBI average或peak任一非零时X=0/Y=8，均零时X=0/Y=0；
  不乘width/unit、不继承MC0结果。普通资源仍按原向上舍入、14bit溢出拒绝规则编码。
  `scale()`明确拒绝mask资源，防止再次误走带宽公式。
- MDSS仍是唯一display RSC writer；RPMh只提供元数据，apps端没有新增ACV写入。
  多lease聚合、VCD排序、每VCD末命令COMMIT、fresh ACK及部分完成账本沿用原状态机。
  本机VCD11的ACV独立提交：ON数据字 `0x60000008`，OFF数据字 `0x40000000`，
  分别含VALID/COMMIT与仅COMMIT，不能把OFF命令非零的COMMIT位误认为仍持有票。
- 多个DATA用户仍有任一EBI需求时保持开启；仅带宽数值变化不重写ACV；最后DATA撤回时关闭，
  即使config-only lease仍持有CN0。重新申请恢复mask8。空账本不擅撤继承硬件票。

成功INFO诊断现在增加 `enable-mask`，本机预期为：
`name=ACV metadata ready address=0x5000c unit=1 width=0 vcd=11 enable-mask=0x8`。
ERROR规则保持，无新增高频日志或boot参数。

## 本地验证

- BCM元数据26场景：真实零宽模型、非零宽ACV仍用mask、五个普通BCM零宽拒绝，
  unit/VCD/reserved/type/aux/范围/重名/地址alias拒绝及一次查表缓存；无投票写入。
- 实际NoC writer模拟：width0/unit1/VCD11以及非零width ACV；独立解码发出的命令，
  检查ON/OFF/ON数据字、独立VCD11 COMMIT、双DATA lease、config floor与无重复ACV写入；
  同时确认apps端无ACV命令。原普通BCM缩放、fresh ACK、故障/超时/stop用例继续通过。
- ASan/UBSan回归范围：BCM、RPMh资源、NoC、managed display、display core、统一生命周期、
  DATA parking、combined suspend及supply lifecycle。
- RPMh与MDSS用真实CI/XNU/KDK头作修改前后ARM64单文件编译；均成功，无新增warning，
  本地clang的原有warning数量分别为RPMh1326→1326、MDSS1539→1538。
- 0185–0205累计严格应用，无offset/fuzz；78份源码逐字节一致，114项现有CI源码marker通过。
  CircleCI配置blob仍为 `da0b6c0947a66f03491b3a9023590b49665292df`。

底稿 `scratch/framebuffer-0205/`（verification/tests/cumulative-replay/warning-comparison及编译日志），
不把host模拟当成实机ACK或DDR/面板工作证明。

## 下一步

用户push本地提交并云编译。下载新kernel/buildtree后核对0205，再以原ABI608固件及605字节参数
打新retained镜像；不添加参数、不覆盖0121a17。先确认ACV metadata不再拒绝、managed投票/接管
的阶段日志和画面；若成功再安排同镜像亮度/OFFON。combined/supply暂缓，不重复旧诊断档或长测。
本轮未修改CircleCI/固件/USB/输入/电池，未push、触发CI或操作设备。
