# 第三方来源

本项目 MIT 只覆盖有权授予的原创代码；第三方组件及素材遵循各自许可。

- LVGL 9.4.0：MIT，上游 https://github.com/lvgl/lvgl，构建下载，未整库入库。
- ESP-IDF / Espressif Adapter 与 LCD 驱动：保留组件各自许可，版本见 dependencies.lock。
- Pngle / miniz：随 firmware-v7/usb_screen/device/src/pngle/ 保存许可证、来源及源码哈希；https://github.com/kikuchan/pngle。
- Grok 表情轮廓：来源 https://github.com/tycoding/grok-ball；原 MIT LICENSE 与来源记录保留在 ui/grok_source/。
- FluidBox 求解器及五种渲染模式：来源 https://github.com/eMUQI/esp-mosaico-fluidbox，固定提交 `75d0c076b3f5b3529253f9238bc936875c47a159`；MIT，衍生自 V4C38/esp32-fluidbox。原 LICENSE、NOTICE 保留在 firmware-v7/ui/fluidbox/；本移植没有使用上游 CO5300/BMI270/TE 驱动。
- TimeTiles 恢复材料及字形/配色：来源为用户提供的 `reverse/mosaico-b359b358`，原应用 SHA256 `db1e45bf8801060a03a6ec25420cc354c31dd6a6732437fa4064badbd64771d0`。两个可读计时辅助函数原样复制，字体/掩码/图标/主题直接提取，其余按恢复行为重建。网页曾列 Apache-2.0，但未取得原作者源码及独立 LICENSE，不将这些材料一并宣称为本项目 MIT。提取地址、哈希、移植范围见 `firmware-v7/TIMETILES.md` 及 `outputs/timetiles-port-20261014/reference/`。
- 奶蛙矿工规则/素材：来源 https://github.com/66970010-boop/frog-miner；来源及哈希保留在 ui/app_assets/source.json。当前工程未保存可确认的独立素材授权文本，不声称这些角色素材受本项目 MIT 授权。
- 字体：Noto Sans SC / SIL OFL，由 setup_deps.py 获取原 OFL 文本。其他既有图片与协会标识保留原权利归属。
