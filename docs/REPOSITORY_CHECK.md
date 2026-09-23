# 仓库维护检查 — 2026-09-23

基于正式发布 v1.1.0 / 固件 7.9.2-memory。开始时枚举全部 385 个已跟踪文件，其中 Markdown 40 份；本轮补充索引、此记录和可重复检查脚本。这里的全文件检查指仓库结构/语法/资源完整性及当前文档一致性，不是所有源码逻辑、电子设计或外部链接的完整功能验收。

## 已更新

- 当前 README、固件 README、板端/手机管理说明、构建与下载路径统一为 v1.1.0 / 7.9.2-memory。
- 更正根 README 后半段旧烧录版本、旧包路径和已取消的背光测试上限；明确硬件仍是 c7c59dff。
- BlueMap 文档改为实际 16 KiB HTTP/Pngle 流式采样链路；区别 1 MiB 接收上限与实际分配。
- 阶段报告/设计稿明确历史地位；首板记录首页新增当前状态入口，历史失败和原始数字保留。
- 扫描得到 111 个非仓库可达的本地 Markdown 目标：真实源码的冒号行号改为文件链接并保留历史行号说明；未上传 outputs、旧 firmware、vendor、分析报告改为本地出处文本，不制造下载链接。
- 旧 GitHub 仓库所有者链接改到 nbtca/CABadge；历史 v1.0 版本号及发布附件保持原意。

## 实际检查

- 原有 32 个 Python 文件 AST 语法检查通过；新增检查脚本也参与最终检查。
- 21 个 JSON 类文件（含 KiCad 工程及 Gerber job）解析通过。
- 56 张 PNG/WebP 资源由 Pillow verify 检查通过；两个制造 ZIP CRC 检查通过。
- 三个 PowerShell 脚本 Parser 检查无语法错误。
- 制造 MANIFEST 所列文件逐项字节数/SHA-256 检查；Markdown 本地目标按 Git 跟踪清单核对，避免只在开发机存在的路径伪通过。
- 硬件 fp-lib-table/sym-lib-table 使用 KIPRJMOD 相对库路径；当前固件版本、构建导出目录、工作台默认目录及 IDF 组件版本已核对一致。
- GitHub v1.1.0 Release 确认为非草稿、非预发布。没有重新检查所有第三方网站可用性。

复查命令（仓库内任意位置运行此脚本均可）：

```powershell
python docs/check_repository.py
```

脚本仅检查 Git 已跟踪文件；新增文件先 git add 再运行。图片与 PowerShell Parser 检查属于本次额外检查，不是该脚本的依赖。

## 已明确的遗留边界

- firmware-v7/CMakeLists.txt 是旧桌面目标，缺少当前 apps/cache/compositor 接线，cJSON 仍指向旧开发机路径。未声称最新版桌面构建可用，不作为板端入口。
- 若干历史 test/probe 依赖旧对象结构、旧 worker 或旧版本集合；仅语法通过不代表全套运行通过。旧 test_display_worker 已删除，历史报告中的执行记录不改写。
- 板端 build.ps1 仍有 Windows/F 盘与 Python 环境固定路径；依赖恢复步骤已说明，未验证干净机器重建。
- BASELINE.json、v1.0 验证清单、硬件冻结源/生产文件、素材哈希均保留历史身份；不把历史哈希更新成新文件哈希。
- J1 采购规格按现有更正，未提供的厂家型号/尺寸图不补造；不重新做 KiCad ERC/DRC 或硬件改版。
- 首板原始日志/RAM_AUDIT 等本地资料未全部发布。手机管理开放热点、第三方游戏素材授权边界见已知问题和第三方说明。

本轮只更新文档与检查脚本，不修改固件 C/H、显示参数、分区、资源或发布 BIN，不编译、不烧录、不执行设备 probe；v1.1.0 标签及附件冻结不动。
