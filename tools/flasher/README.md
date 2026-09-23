# 当前工作台使用的烧录模块

`app.py` 提供固件文件/哈希/硬件标识检查和 esptool 烧录函数，由 `firmware-v7/workbench.py` 导入。它不是可以整目录删除的旧工具。

复刻者使用 [当前安装说明](../../docs/RELEASE-v1.1.0.md) 或 `firmware-v7/run.cmd`。app.py 保留早期独立窗口代码，但其中诊断预设不作为本版入口；此次只清理无依赖文件，未重写已工作的烧录函数。

旧 EXE 打包脚本和依赖历史诊断包的测试已移除；完整历史仍在 Git 标签中。需要 Tkinter、pyserial 和 esptool，具体环境见 [BUILD](../../docs/BUILD.md)。
