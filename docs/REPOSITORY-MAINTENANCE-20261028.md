# 仓库维护整理总结

本轮于 2026-10-28 开始，2026-10-29 完成验证与发布。整理基线为 `80ebcf2e0e8097f491eb1191270c9ca48186b9f6`（已更正 R2 按钮）。实施提交为 `2c0355739e99450a81e52a8c5a4755ecd217f1f9`，CI 运行时路径修正为 `229f360832419b7ec07c00a80018aedf57490052`，Windows 原字节检出与测试盘符修正为 `cf40069db4739980f99f2c63d8cd429e865077f8`。

## 完成的八项整理

| 建议 | 完成内容 |
| --- | --- |
| 维护文档与当前状态一致 | 修正 AGENTS 的 HANDOFF 路径，更新当前架构、构建和应用入口；首板测试历史保留原意。 |
| 明确源码、附件和硬件版本 | README 增加版本对应表；公开源码仍为 7.9.23，正式附件仍为 v1.2.0；历史测试标签与二进制不对应的问题明示，既有标签没有移动。 |
| 集中构建环境 | tools/bootstrap.ps1 和固定依赖安装到 CABADGE_BUILD_ROOT，默认 F:\CABadgeBuild；工作台和探针复用中央 Python。 |
| 统一构建输入与版本 | version.json 同时生成 UI/CMake 版本；同一清单负责复制与指纹。按工作树、变体、每次调用隔离暂存与输出，姓名定制必须显式命名。 |
| 增加发布检查与打包 | tools/release 核对 R2 实际零件、原生归档、清单覆盖、固件提交身份与输入/产物指纹；拒绝脏源码、错版和输入目录内输出，生成确定性 ZIP 与校验文件。 |
| 按职责整理目录 | 宿主检查迁至 firmware-v7/tests/host，仓库工具迁至 tools/repository，架构文档迁至 docs/architecture，修复消费者和本地链接。 |
| 增加自动检查 | .github/workflows/repository.yml 使用只读权限，执行仓库、发布、构建配置、工作台及 framing 离线检查。最终线上结果见下表。 |
| 大型 CAD 附件使用 Release | 原字节上传至独立外壳 Release，更新首选下载入口；原仓库 ZIP 路径保留兼容，没有重写历史或声称仓库体积已经缩小。 |

## 当前目录与入口

```text
firmware-v7/                  公开固件源码、工作台和版本定义
  version.json                唯一固件版本定义
  tests/host/                 20 个原有宿主检查及构建配置检查
hardware/
  pcb/current/                V2.1 KiCad + EDA 原生重画工程
  pcb/archive/V1_c7c59dff/     V1 首板，仅 KiCad
  enclosure/releases/         V11 R2 打印件、图纸与装配资料
  fabrication/                原冻结制造集合
docs/
  architecture/               设计与界面架构
  BUILD.md                    环境与编译
  DEVELOPMENT.md              分支、工作树和定制构建
  INDEX.md                    按任务导航
tools/
  bootstrap.ps1               中央环境安装
  repository/                 仓库完整性和回归
  release/                    发布检查与交付包
.github/workflows/            自动离线检查
```

旧 `docs/check_repository.py` 保留为很小的兼容入口，因为冻结 V1 归档中的原说明仍引用此命令；实现只有 tools/repository 一份。

## 验证结果

| 验证层 | 结果与范围 |
| --- | --- |
| 仓库完整性 | 583 个受跟踪文件、43 份 Markdown、239 个本地链接、43 个 Python、34 个 JSON、5 个归档及全部受跟踪清单通过。 |
| 回归与发布检查 | 仓库回归 8 项、发布 19 项、构建配置 7 项、工作台 4 项及 framing 全部通过。 |
| 原有宿主检查 | 20 个原有脚本通过；新暂存布局下 EAF Direct 再次通过 24/210/300 帧参考资源比较。 |
| 实际固件编译 | 从干净提交 2c03557 构建成功，158.62 秒；RAM 49,856 B，Flash 3,983,159 B，BIN 3,983,584 B；镜像校验和和验证哈希有效，内部版本 7.9.23-timetiles-fix。 |
| 源码身份 | source_commit 为 2c03557 完整 SHA，source_dirty=false；4,726 项源码输入、4,727 项暂存输入和四个主要产物指纹全部匹配，正式发布检查通过。 |
| R2 外壳 | 实际 23 个有效载荷文件、SolidWorks 9 个成员、STEP 4 个成员及归档 CRC/清单通过；两次生成同内容交付包，SHA-256 一致。 |
| GitHub CI | [cf40069 线上运行 40740181027 全部通过](https://github.com/nbtca/CABadge/actions/runs/40740181027)；所有检查步骤均成功。 |
| 原工作区保护 | 589 个记录路径中 518 份原有文件的字节和 71 个原缺失路径的状态均匹配，零变化；PCB、冻结制造集合、首板测试记录与整理基线无差异。 |

本地干净构建输入摘要：`512d31df3b7f38d1db650b83228544198a334214bdc16a1617d4e0ba6c0440f1`。

## 发布与本地交付

GitHub 大型 CAD 首选入口：[V11 Button R2 外壳 Release](https://github.com/nbtca/CABadge/releases/tag/enclosure-v11-button-r2-20261028)。此标签固定到 `80ebcf2`，没有替换既有固件附件，latest 正式 Release 仍为 v1.2.0。

| Release 附件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| CABadge_V11_V2_1-SolidWorks.zip | 47,833,489 | `12e5f4ce611d5f4b196df5c89f23883bf315529270c6a119923270b7759a81d0` |
| CABadge_V11_V2_1-STEP.zip | 26,842,833 | `e9dbc0c08fc6a267ecb3e3fd9e1e8d30e21f633bb575eb2e26382d96192f8a7b` |

GitHub 服务端提供的 SHA-256 和字节数均与本地原文件相同。完整附件下载复核遇到网络超时，没有把该步骤记作通过。

本地交付目录：`F:\计协吧唧\outputs\repository-maintenance-20261028\`。

| 文件 | 内容 | SHA-256 |
| --- | --- | --- |
| CABadge_V11_R2_verified.zip | 25 个成员，含 23 份有效载荷与两份根清单 | `53469b75f2801e8040d84b95c9ca3daf45a9b41c2ed3a5998da88d031dca9646` |
| CABadge_7.9.23_clean-source_2c03557.zip | 8 个成员，含本轮干净构建及源码身份记录；未替换正式固件 | `00038f7190cd528486502860ca8009d66fe9f4c9bd699c3ddbc1c2df9f20b52f` |

两包均有旁置 `.sha256` 文件并通过 ZIP CRC。详细操作证据保存在 `F:\CABadgeBuild\temp\cabadge-repository-maintenance-20261028\`；构建和宿主日志保存在其上级 temp 目录的 repository-maintenance 文件中。

## 保留范围、失败与复测

原目录 `F:\计协吧唧` 保留在 `codex/local-dev-20261028`，未提交的 7.9.25 / m1ngsama 工作保持原样。公开整理位于 `F:\计协吧唧\.worktrees\repository-maintenance` 的 `codex/repository-maintenance` 分支，已同步 GitHub main。完整未提交文件备份及原指纹留在上述证据目录，不通过重置原目录来合并本轮整理。

PCB 原生 EDA 重画工程、KiCad、库、V1 归档、冻结制造包及原有外壳几何字节均保留。旧转换版 EDA 没有恢复或新增归档。没有继续 BOM 核对，没有连接或烧录设备，也没有改变实测结论。

旧脚本探测曾误触共享构建暂存，已按上次成功构建的 46 项指纹恢复；原源码与已有固件附件未改。独立审查另发现复用暂存可能残留已删除输入，新增回归先复现失败，再修为每次全新暂存并复测通过。第一次线上 CI 因 job.env 不允许 runner.temp 表达式而启动失败，已改为执行步骤中写入 GITHUB_ENV，并保留失败运行 [40739261709](https://github.com/nbtca/CABadge/actions/runs/40739261709)。第二次运行 [40739527493](https://github.com/nbtca/CABadge/actions/runs/40739527493) 因 Windows 自动换行转换导致冻结文件校验失败；已设置检出前禁用转换，并让测试临时路径使用运行环境盘符，原文件和校验值均未修改。

这次验证覆盖构建、仓库结构、发布文件和离线契约。没有新增显示、手势、撕裂、供电、无线或长稳实体验收；外壳状态仍为 BUTTON_CAD_PASS_PRINTED_FIT_PENDING，打印后装配与手感待验证。

需要撤销本轮公开整理时，依据上述实施及 CI 修正提交逐项 revert，再通过同一仓库/发布检查；不要 reset 保留私人开发修改的原目录，也不要改写既有标签或冻结包。
