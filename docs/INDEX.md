# 仓库文档索引

当前项目发布 v1.1.0，内部固件 7.9.2-memory。两套编号分别表示 GitHub 发布与开发固件，不应批量把历史版本替换成当前版本。

|用途|入口|
|---|---|
|项目与复刻|[README](../README.md)|
|当前安装/分区升级|[v1.1.0](RELEASE-v1.1.0.md)|
|构建与工具边界|[BUILD](BUILD.md)|
|产品规范与架构|[DESIGN](../DESIGN.md)、[UI 架构](../UI_ARCHITECTURE_V7.md)|
|当前固件与交接|[firmware-v7](../firmware-v7/README.md)、[HANDOFF](../firmware-v7/HANDOFF.md)|
|手机/USB/无线|[服务说明](../firmware-v7/usb_screen/README.md)|
|应用与表情|[APPS](../firmware-v7/APPS.md)、[GROK](../firmware-v7/GROK.md)|
|验证与限制|[首板记录](../首板测试记录.md)、[KNOWN_ISSUES](KNOWN_ISSUES.md)|
|硬件与采购|[hardware](../hardware/README.md)、[J1 更正](J1-CONNECTOR.md)|
|第三方来源|[THIRD_PARTY](THIRD_PARTY.md)|
|仓库检查范围与遗留项|[维护检查](REPOSITORY_CHECK.md)|

## 历史资料

DESIGN、UI_ARCHITECTURE_V7 和 HANDOFF 已重写为当前入口，原文通过各自末尾固定提交链接保留。MOTION_RESEARCH_V7、各阶段 REPORT/REVIEW、WIRELESS_P0、BASELINE.json 和 v1.0 验证清单保存当时的方案/配置/证据，不能当作当前配置。早期 Carousel、USB 像素镜像、授权码、单图存储等说明不覆盖现固件。

GitHub 不包含 outputs 原始日志、旧 firmware 工程、vendor 与虚拟环境。指向这些内容的旧链接已改成明确的本地路径说明；不代表补做过历史测试。制造文件、素材、源哈希与历史 verification.json 不随文档更新改写。

正式发布标签与附件保持冻结；本次文档维护在 main 上继续，不移动 v1.1.0 标签。
