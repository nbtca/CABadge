# 当前工作台使用的烧录模块

`app.py` 检查固件目录的清单、哈希、硬件标识和分区大小，再调用 esptool 烧录。它由 `firmware-v7/workbench.py` 导入，本身没有独立窗口。

复刻者使用[当前安装说明](../../docs/RELEASE-v1.2.0.md)或工作台。早期 bring-up 诊断预设和独立烧录窗口已删除，需要时从 Git 历史恢复。依赖 pyserial 和 esptool，环境见 [BUILD](../../docs/BUILD.md)。
