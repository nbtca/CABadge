# 发布文件的验证与打包

`prepare.py` 只验证、封装已有交付物，不打开 CAD、连接设备或上传文件。包内通过不等于电路、打印或实物功能验收。

## V11 按钮 R2

```powershell
python tools/release/prepare.py enclosure hardware/enclosure/releases/V11_V2_1_20261020 --check
python tools/release/prepare.py enclosure hardware/enclosure/releases/V11_V2_1_20261020 --output outputs/releases/CABadge_V11_Button_R2.zip
```

工具检查完整 MANIFEST / SHA256SUMS、归档 CRC、原验证记录中的 STL / STEP / SLDPRT 指纹、八部件原生装配和旧按钮残留。不能仅重算清单就把旧按钮作为 R2 发布。新 ZIP 固定成员顺序和时间戳，同时生成 `.zip.sha256`；不覆盖已有输出，也不允许输出落入输入目录。

维护下载说明或增加说明文件后，使用 `python tools/release/prepare.py enclosure hardware/enclosure/releases/V11_V2_1_20261020 --refresh-manifest` 更新清单。工具先核对 R2 设计指纹和归档成员，错误修订不能通过重算清单变为已核验版本。

本工具以已有 CAD 模型包为维护输入，不重新构造机械几何。修改原生设计后，仍需在对应 CAD 软件中保存、回读、完成适用的几何检查，再更新设计验证记录。原验证状态 `BUTTON_CAD_PASS_PRINTED_FIT_PENDING` 保留。

## 固件

从干净的源码提交构建，再验证并打包：

```powershell
pwsh -NoProfile -File firmware-v7/usb_screen/build.ps1
python tools/release/prepare.py firmware <构建输出目录> --source-ref HEAD --check
python tools/release/prepare.py firmware <构建输出目录> --source-ref HEAD --output outputs/releases/CABadge-firmware.zip
```

验证要求四份 BIN/ELF 指纹正确，`source_commit` 等于所选源码提交，`source_dirty=false`，工作树干净，版本等于该提交的 `firmware-v7/version.json` 加显式变体后缀。脏源码构建可用于开发验证，但不能经此工具准备正式发布。

发布者为同一源码提交创建新标签，上传对应附件与校验值；不移动历史标签。旧 `v1.3.0-rc.1` 的自动源码 ZIP 与其固件附件不对应，历史例外见[项目版本表](../../README.md#版本与资料对应)。工具不替代工作台的 ESP 镜像、分区、目标板与烧录前检查，也不修改工作台正式默认。

## 大型 CAD 附件

当前两个 V11 CAD ZIP 的首选下载是 [V11 按钮 R2 Release](https://github.com/nbtca/CABadge/releases/tag/enclosure-v11-button-r2-20261028)，固定到原 R2 提交 `80ebcf2`。原仓库 ZIP 路径保留兼容，附件字节与其一致；后续大型版本优先添加独立 Release 附件，不重写 Git 历史。

新版本发布前先执行上述检查，使用明确的新标签和附件名，上传后重新下载核对指纹。复用已有标签前先核对附件，禁止用无条件覆盖选项替换未知文件。

回归：`python tools/release/test_prepare.py`。测试使用临时合成文件，不访问设备。
