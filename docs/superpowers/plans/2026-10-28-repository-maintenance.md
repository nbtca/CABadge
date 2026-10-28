# Repository Maintenance Implementation Plan

> Execute the approved tasks in the isolated F-drive worktree. Use an independent code review before integration.

**Goal:** 完成用户批准的仓库结构与发布维护整理，保留现行硬件和私有开发工作，交付可验证总结。

**Architecture:** 保留硬件布局和现有固件功能。构建参数集中、路径按职责迁移；发布工具验证已有工程交付物，不重新设计 CAD 或固件功能。

**Tech Stack:** Python 3.10、PowerShell 7、PlatformIO 6.2.0、ESP-IDF 6.1.0、GitHub Actions。

**Spec:** [已批准范围](../specs/2026-10-28-repository-maintenance.md)

## Global Constraints

源文件与交付物在 F 盘；旧标签/固件附件/实测结论不改写；保护 PCB 与冻结制造文件；不操作设备。原主工作区仅切到本地开发分支并备份，内容逐字节保留。

## Task 1: Isolation and baseline

- [x] 记录 80ebcf2、备份未提交文件及指纹，原目录切至 codex/local-dev-20261028。
- [x] 在 F 盘创建 codex/repository-maintenance 工作树。
- [x] 运行 `python tools/repository/check_repository.py` 和 `python tools/repository/test_repository.py`，基线通过。

## Task 2: Build and host-check ownership

Files: firmware-v7/usb_screen/build.ps1、device/CMakeLists.txt、device/src/CMakeLists.txt、ui/badge_ui.h/.c、firmware-v7/version.json、tools/bootstrap.ps1、tools/requirements-dev.txt、firmware-v7/tests/host/。

- [x] 为版本/变体/构建配置和不覆盖通用产物的边界先写可运行检查；验证失败。
- [x] 从一份版本配置生成头文件与 PROJECT_VER；同一源清单用于复制和指纹，记录 source_commit / source_dirty。
- [x] 环境集中至 CABADGE_BUILD_ROOT；bootstrap 安装已验证版本的构建、测试及工作台依赖。原 F 路径仍是默认。
- [x] 迁移宿主测试，修正资源根目录和 Zig 定位；运行全部公开基线宿主检查。
- [x] 运行实际板端构建，验证产物清单与源码身份；不烧录。

## Task 3: Release ownership and reproducible packaging

Files: tools/release/prepare.py、tools/release/test_prepare.py、tools/release/README.md。

- [x] 先写错误 R2、破损归档、漏件、修改字节、错误源码提交、脏源码拒绝用例，验证失败。
- [x] 实现 enclosure / firmware 验证与确定性整包导出；禁止输出覆盖输入。
- [x] 验证实际 V11 R2，导出交付包，核对 ZIP 成员和 SHA256。
- [x] 专用硬件 Release 固定到实际 R2 提交，上传两个原字节 CAD ZIP 与校验值，核对 GitHub 服务端 SHA-256 和字节数；完整附件下载超时，未声称下载复核通过。

## Task 4: Navigation, CI and final delivery

Files: AGENTS.md、README.md、docs/INDEX.md、docs/BUILD.md、docs/architecture/、tools/repository/、.github/workflows/repository.yml、.gitignore、V11 下载说明与清单、docs/REPOSITORY-MAINTENANCE-20261028.md。

- [x] 移动架构与仓库检查，修复全部受跟踪链接和命令；保留历史记录原意。
- [x] 修复维护入口，记录源码/附件/PCB/外壳各自版本与历史标签例外。
- [x] CI 运行仓库自检、仓库回归、发布工具测试及可离线检查，权限仅 contents:read。
- [x] 独立代码审查后修复问题，核对全差异和保护集合，提交并推送 main。
- [x] 确认线上 CI、下载指纹与主分支提交，写完整总结，核对原工作区 589 份文件指纹。

## Verification commands

```powershell
python tools/repository/check_repository.py
python tools/repository/test_repository.py
python tools/release/test_prepare.py
python tools/release/prepare.py enclosure hardware/enclosure/releases/V11_V2_1_20261020 --check
python firmware-v7/tests/host/test_workbench.py
pwsh -NoProfile -File firmware-v7/usb_screen/build.ps1
```

其他宿主测试逐个从 firmware-v7/tests/host 运行；构建使用隔离暂存和输出，报告保留失败及复测证据。

## 完成记录

2026-10-29：实施 2c03557，CI 修正 229f360 / cf40069；干净提交构建、发布检查与线上运行 40740181027 通过。详细证据、原工作区保护及下载复核限制见 [完成总结](../../REPOSITORY-MAINTENANCE-20261028.md)。
