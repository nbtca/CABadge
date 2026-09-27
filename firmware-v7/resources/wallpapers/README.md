# 壁纸素材

`badge_wallpaper.rgb565`（晴日）和 `badge_ribbons.rgb565`（流光）是 360×360、小端 RGB565 素材，由 `generate_assets.py` 生成。

从 7.9.11-wallpaper 起，它们不再编译进应用，也不是不可删除的内置条目。需要时通过同一 USB 上传协议导入 `wallpaper` 分区，和其他壁纸共用 31 个存储槽、缩略图 LRU、选择及删除路径。

在项目根目录执行：

```powershell
python tools/install_wallpapers.py --port COM3
```

工具按库中 CRC 跳过已有图片，导入后恢复原选择；第一次安装可加 `--select sunny`。容量不足时不删除任何现有壁纸。此工具只在显式执行时导入，固件启动不会重新安装已删除图片。

图库可以为空；空库显示空白背景和上传提示。手机管理的“使用空白背景”只取消当前显示，不清空图库。旧库数据格式及已有壁纸编号保持兼容，应用升级不要擦除 `wallpaper` 分区。
