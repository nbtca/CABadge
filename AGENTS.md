# 首板测试记录维护

## 项目文件存储位置

- 用户明确要求项目文件使用 F 盘，避免占满 C 盘。源码及发布产物留在 `F:\计协吧唧`；需要纯英文路径的编译暂存、工具链、缓存和临时文件使用 `F:\CABadgeBuild`。
- 构建进程的 `TEMP` / `TMP` 指向 `F:\CABadgeBuild\temp`。PlatformIO 使用 `F:\CABadgeBuild\platformio`；Zig 全局缓存使用 `F:\CABadgeBuild\zig-cache`。
- C 盘既有 `JXBadge`、`.platformio` 和 Zig 缓存路径仅保留指向 F 盘的兼容目录联接；不要在 C 盘重新建立实体副本。调整共享路径前检查活动构建，并保留其他对话的工作文件。

## 测试记录

- 主测试文档是项目根目录 `首板测试记录.md`。用户要求后续测试同时更新此文件。
- 处理本项目新的实板测试日志、测量值、测试失败、维修或复测结果时，先阅读相关既有记录；在分析结果的同一轮更新对应项目状态、证据和修订记录。
- 延续同一文档，不生成相互冲突的测试总结。历史失败保留，修复后追加复测结果；仅发布新测试固件时应记待测，不能预先判实板通过。
- 分清直接日志、用户测量反馈和本机代码检查；不把参考电压写成实测值，不虚构未提供的数据、持续时间或测试覆盖范围。
- 更新当前实测状态后，按需要同步 `firmware-v7/HANDOFF.md` 的最新摘要；历史发布产物的 verification.json 保留发布时含义。
- 已打板 PCB `c7c59dff` 是硬件基线。测量说明使用板上真实丝印 VBUS、3V3、GND、BAT、PWR、EN，不用 TP 编号要求用户找点。

## 实体固件验证（2026-09-21 用户更新）

- 用户明确要求不再用模拟器验收画面；固件编译后直接烧录到实板测试。保留必要的 DMA 生命周期、协议和错误路径代码检查，不把模拟器运行作为交付前置条件。
- 当前双绘图缓冲/异步 DMA 工作继续维护 `firmware-v7`；版本由 `firmware-v7/version.json` 定义，状态以根 README 与 `firmware-v7/HANDOFF.md` 为准。7.3.3 是历史起点，不作为回退要求；不切到旧 `firmware-article` 的 8.0，历史源码和发布包各自独立。
- 防撕裂是实体显示验收条件。保留传输顺序和缓冲所有权；TE 未接，不能凭双缓冲或软件测试承诺无扫描撕裂，需记录实屏目视反馈。

## 仓库维护与发布

- 通用开发、个人定制和结构整理使用独立分支；并行任务使用独立工作树，保留其他任务的未提交文件。
- 构建环境入口为 `tools/bootstrap.ps1`，默认 `CABADGE_BUILD_ROOT=F:\CABadgeBuild`。旧 `firmware/bringup/.venv` 不是当前构建依赖。
- 宿主检查在 `firmware-v7/tests/host/`；仓库门禁为 `tools/repository/check_repository.py` 与 `test_repository.py`。离线检查不打开设备。
- 发布使用 `tools/release/prepare.py`，核对源码提交、版本、文件指纹及具体 R2 修订。不得移动既有标签或把旧附件冒充新构建。
- `hardware/pcb/current`、V1 归档与冻结制造集合各自保持职责；KiCad/EDA 不自动互相覆盖。完整模型库与历史验证材料不能按相同文件名批量删除。
