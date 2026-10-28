# 开发分支与构建变体

公开源码、正式固件附件、测试附件、PCB 和外壳分别选择版本，统一入口见[项目版本表](../README.md#版本与资料对应)。本地 7.9.25 / m1ngsama 属于未发布开发工作，不随仓库结构整理合入公开固件。

## 任务隔离

开始修改前查看 `git status`。一个任务使用一个分支；同时修改不同任务时使用独立工作树。当前项目内 `.worktrees/` 已忽略，可在 F 盘原仓库执行：

```powershell
git worktree add .worktrees/my-task -b codex/my-task main
```

工作树不复制原目录未提交的修改。需要保留的本地工作先保留在其原分支和工作目录，不通过清空工作区来获得“干净”。提交时明确列出本任务文件，核对暂存差异后提交。源文件和交付物在 F 盘；环境、缓存、构建暂存默认在 `F:\CABadgeBuild`。

## 通用与定制构建

固件版本仅在 [version.json](../firmware-v7/version.json) 定义。构建生成 C 头文件与 CMake 版本，并记录源码提交、是否有未提交修改、构建参数及输入指纹。

```powershell
# 只读解析配置，不创建暂存、不编译
pwsh -NoProfile -File firmware-v7/usb_screen/build.ps1 -ConfigurationOnly

# 通用版本保持 Egger；定制构建必须命名，使用独立输出
pwsh -NoProfile -File firmware-v7/usb_screen/build.ps1 -BuildVariant m1ngsama -MemberName m1ngsama
```

姓名参数使用可打印 ASCII，禁止引号和反斜杠；较长姓名采用现有小号字和标签截断，实际完整性仍需目视确认。构建按工作树和变体隔离，每次使用全新暂存目录，保留此前构建证据，输出目录带配置及时间戳，不覆盖原通用或历史固件包。构建后先核对输出内的 `verification.json`，再按任务决定是否安排设备测试。

安装环境及普通构建见 [BUILD](BUILD.md)，发布门禁见[发布工具](../tools/release/README.md)。仓库自动检查只执行离线检查，不连接、烧录或验收实物。
