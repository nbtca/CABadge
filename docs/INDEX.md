# 复刻资料索引

当前项目版本 v1.1.0，内部固件 7.9.2-memory，硬件 c7c59dff。主分支保留硬件复刻、板端构建、安装使用和必要维护资料。

|任务|入口|
|---|---|
|从哪里开始|[项目说明](../README.md)|
|下载和烧录|[发布与升级](RELEASE-v1.1.0.md)|
|自行编译|[BUILD](BUILD.md)|
|采购、PCB 与装配|[硬件](../hardware/README.md)、[J1 更正](J1-CONNECTOR.md)、[焊接检查](../焊接与分阶段上电检查_首板.md)|
|使用无线与壁纸|[板端服务](../firmware-v7/usb_screen/README.md)|
|使用应用|[APPS](../firmware-v7/APPS.md)、[GROK](../firmware-v7/GROK.md)|
|界面维护|[DESIGN](../DESIGN.md)、[架构](../UI_ARCHITECTURE_V7.md)、[交接](../firmware-v7/HANDOFF.md)|
|验证、限制及许可|[首板记录](../首板测试记录.md)、[已知问题](KNOWN_ISSUES.md)、[第三方来源](THIRD_PARTY.md)|

旧模拟器、像素投屏、阶段报告和废弃测试已从当前树删除。首板测试历史与已发布标签保留；需要旧文件时从下方快照或 v1.0/v1.1.0 标签获取，不将旧工具混入当前构建。

[精简前完整仓库](https://github.com/nbtca/CABadge/tree/d1fe9716b383dcb6fd292d683265beacd92fde64)

生产目录是冻结校验集合，包含 MANIFEST 引用的制造、验证及导出源快照，整组保留。硬件库、模型、素材源与许可证均为完整复刻或维护依赖，不按文件名相似判断重复。

仓库自检：`python docs/check_repository.py`；只做结构/语法/链接/制造哈希检查，不连接设备。
