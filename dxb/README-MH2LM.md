# dxb（vendored）· MH2LM 完整源码分支

本目录是 [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build) 在
`663906528d1cd5ea8fde7de6f16de73db9511549`（MIT License，见本目录 `LICENSE`）的副本，
为 **full-source 分支** 提供原样的 macOS 构建流程。与上游的唯一区别：

| 位置 | 改动 | 原因 |
|---|---|---|
| `get_xnu()` | 不再 `git clone`；直接使用仓库内的 `dxb/xnu` | 源码树随分支提交 |
| `patches()` | 不再执行 dxb sed、staging 脚本或 `.patch`；改为校验树已是应用后状态 | CI 不再应用补丁 |
| `kernel_version_env()` | 用 `XNU_SOURCE_TAG` / `XNU_SOURCE_COMMIT_DATE` 常量代替 `git describe` | 提交树没有 git 元数据，版本横幅必须保持 `xnu-8796.141.3` / `Mon Aug 14 05:06:07 UTC 2023` |
| `clean()` | 不再删除 `xnu/` | 防止误删已提交源码 |
| libfirehose 编译 | `ARCHS="x86_64 arm64e arm64"`（原 CI 用 sed 补 arm64） | 固定进脚本，不再运行时改 |

`build.sh` 其余部分（KDK/SDK、依赖项目、fakeroot、编译、打包）与上游保持一致；
依赖项目（bootstrap_cmds、dtrace、AvailabilityVersions、Libsystem、libplatform、
libdispatch）仍按 Apple release 标签在构建时获取，它们不属于补丁步骤。

## dxb/xnu 的组成

```
xnu-8796.141.3（apple-oss-distributions/xnu tag 1b191cb58250d0705d8a51287127505aa4bc0789）
  → dxb 的 5 处 sed 构建修正（make_symbol_aliasing.sh、Libsyscall.xcconfig、MakeInc.def×2、MakeInc.cmd×2）
  → 阶段脚本：libpthread-514.100.2、BearSSL、hfs-627.100.6 拷入源码树
  → ios-mh2lm/xnu/patches/0001..0229 全部补丁
  → dxb/patches 中可应用的 4 个（kas_info、machine_routines、skywalk_ventura、syntax_checker_ventura；
     TrustCache、iobuffermemd_monterey、skywalk_sonoma、syntax_checker_sonoma 在原版流程中同样被 --check 拒绝）
```

该结果是 **从 tag 逐补丁重放** 得到，并与 0121a39 CI 构建树逐文件比较一致（仅
`libsyscall/Libsyscall.xcodeproj/project.xcworkspace` 为构建生成目录，未提交；
`TargetConditionals.h` 两份 SDK 头按 APSL 2.0 随树提交，使树可独立编译）。

## 使用

```sh
git switch full-source
# 直接修改 dxb/xnu/**；没有 patch 需要生成
git -C /home/shenli/work1/ref/ios-mh2lm push origin full-source   # 用户操作
```

CircleCI 读取本分支的 `.github/workflows/circleci.yml`：跳过补丁步骤，其余参数、
`require_marker` 检查与打包产物形式与主线一致。产物版本横幅由 workflow 的
`KERNEL_BUILD_DATE` / `KERNEL_BUILD_OBJROOT` 环境变量固定。

## 边界

- 只有 **这一个分支** 采用完整树；`main` 仍是补丁仓库，二者不自动同步。
- 本分支不再生成 `NNNN-*.patch`；`xnu/patches/` 仅作历史参考。
- 离线构建仍需要网络：KDK、Apple 依赖项目源码、Homebrew 工具。
- 没有新增硬件或性能结论；构建产物仍需按构建指南核对源码、标记与 SHA。