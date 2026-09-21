# JXBadge 烧录助手

适用于已打板 c7c59dff 的 ESP32-S3、16 MB Flash。原生 Windows 窗口，封装现有 esptool 5.4.0 和 pyserial 流程。发布目录中的 `JXBadgeFlasher.exe` 可直接双击，不需要打开终端或另装 Python；移动到其他位置时保留整个 `JXBadgeFlasher` 文件夹。

## 使用

1. 接电脑 USB，打开板上电源，屏幕按当前维修状态保持拔除。关闭之前运行的串口脚本。
2. 双击应用，选串口（通常 COM3）。默认固件为 **Wi-Fi + BLE 连接测试**；也能选择其他三版已有诊断固件。
3. 点击 **烧录并监听**。如果本应用正在监听，会先释放串口。校验文件后烧录，成功后自动打开串口；按照提示按一次板上 RESET。
4. 无线版运行到输入提示时，填写热点名称和密码，点击 **发送热点信息**。密码隐藏显示、不保存到文件；固件请求密码后应用自动发送，无需在终端键入。
5. 查看日志。**保存日志**只导出当前显示内容；超过 12,000 行会裁掉较早的约 2,000 行，长时间测试应及时分段保存。
6. **仅打开串口**不会烧录；**关闭串口**只停止监听，不关板子电源。退出应用也不会让固件停止运行。

手机 BLE 测试继续使用 nRF Connect，连接 `JXBadge-Test`。这个桌面应用不充当 BLE 客户端。

## 固件与失败处理

- 发布包内置的是本次四版固件的快照。以后重新编译，点击 **选择固件目录** 选中新的完整输出目录即可，不需要重装应用。
- 固件目录须含 `bootloader.bin`、`partitions.bin`、`firmware.bin` 和 `verification.json`，清单写明 PCB 和三个文件 SHA-256；应用核对文件哈希、应用分区容量和固定写入地址。
- 烧录地址沿用 `0x0 / 0x8000 / 0x10000`，115200 波特率；不会自动整片擦除。现有诊断固件每次启动仍会擦写专用最后 4 KB 测试区。
- 缺文件、哈希不符或分区不合适会在打开串口前拒绝烧录。烧录失败后不自动开始监听，不把失败显示为成功。
- 串口占用：关闭其他串口工具，再重新打开。进入下载模式失败：按住 BOOT，按下并松开 RESET，再松开 BOOT，然后重试。
- 烧录期间应用关闭按钮会提示等待，避免误关打断；尚未提供中途取消写入功能。USB 拔出错误会显示在日志里。
- 本程序只适配这个项目的三文件发布格式，不接受任意地址的通用烧录操作，也不自动烧 eFuse 或备份整个 Flash。

## 开发与验证

源码：`tools/flasher/app.py`。依赖为本机已有的 Python/Tkinter、esptool、pyserial；打包使用 PyInstaller。

```powershell
python tools/flasher/test_app.py
python tools/flasher/app.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/flasher/build.ps1
```

`--smoke-test <JSON路径>` 验证原生控件、四个固件包、热点发送状态与密码遮蔽，自动退出；不打开串口、不烧录。模拟 I/O 测试不能替代用户实际烧录；最新验证状态见同目录 `validation.json`（发布时生成）和项目根目录 `首板测试记录.md`。
