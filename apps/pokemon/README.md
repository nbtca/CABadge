# 宝可梦对战

CABadge `7.9.25-poke-duel` 的应用源码快照，适配 360×360 圆屏触摸。
支持妙蛙种子、小火龙、杰尼龟、皮卡丘，固定 Lv.50，四技能、PP、属性克制、
速度/优先级、命中、暴击、挣扎反伤；提供单机电脑与两块吧唧的 BLE 对战。
这是伤害型技能子集，不包含完整原作、养成或队伍轮换。

## 文件

- `ui/pokemon.c/.h`：选择、房间、战斗界面、动画及联机状态机。
- `ui/pokemon_core.h`：战斗规则、物种/技能数据、20 字节协议编解码。
- `ui/peer_link.h`：房间连接、收发及连接代次接口。
- `ui/pokemon_art.c/.h`、`ui/pokemon_sprites/`：四只精灵正反面图、来源及指纹。
- `usb_screen/device/src/ble_service.c`：本应用实际使用的 NimBLE 传输实现。
  此文件是共享 BLE 服务快照，包含原连接管理/心率接口，不应与固件中的同名文件同时编译。
- `ui/badge_ui.h`、`ui/heart_rate.h`、`management.h`、`management_protocol.h`：宿主接口声明。

## 接入 CABadge

将 `ui/pokemon.c` 和 `ui/pokemon_art.c` 加入宿主的组件源列表，包含 `ui`。
在 GUI 任务调用 `pokemon_open(parent, back)`，约 33ms 调用
`pokemon_poll(应用当前可见且未息屏)`；离开时调用 `pokemon_close()`。
`pokemon_info()` 提供诊断状态。不要从射频回调操作 LVGL。

单机仍需要 `peer_link_*` 和 `ble_service_snapshot()` 的接口实现，因为它们与联机共用控制器。
双机使用随包 `ble_service.c` 的 `peer_link_*` 实现，需要宿主原有 management 服务、
GUI/BLE 状态桥接及初始化/轮询。此共享服务应替换或人工合入旧服务，保留宿主自身的启动与休眠处理。
宿主需提供本包 `badge_ui.h` 所列新版 `pokemon_room` 设备标志及其余接口、
含应用中文字符的 `font14/font18/font24` 和点击保护；本包不携带名片或字体全集。
这些声明不会自动实现硬件服务。

ESP-IDF/NimBLE 配置至少保留 central、observer、peripheral 角色、
`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2`；本地基线使用外部 PSRAM 分配。
房主确认状态；客机重试命令，状态/心跳补发，旧会话/回合及重复动作被拒绝。
退出、息屏、断线或超时结束对局。使用前断开占用中的其他 BLE 连接。

## 检查与素材

在仓库根目录运行：

```powershell
# 使用既有构建环境；也可用 CABADGE_ZIG 指定 Zig 可执行文件。
python -B apps/pokemon/tests/host/test_pokemon.py
python -B apps/pokemon/generate_pokemon_assets.py
```

宿主检查调用本目录生产规则和控制器，覆盖坏包、重试、丢状态、重复动作、超时、
动画和单机；不代表真实射频或实屏验收。素材生成需要 Pillow。
用户在 2026-10-28 对两块实物反馈“单机正常”“联机测试我自己做了 一切正常”；
未取得逐回合日志或长稳/异常恢复专项验收。

素材版权与参考代码来源见 [CREDITS](ui/pokemon_sprites/CREDITS.md)。
CABadge MIT 代码许可证不授予宝可梦图像和角色的权利。
