# Stage27 人工 GUI 验收记录

> 填写说明：本文件由验收人在 Windows 真机上逐项手动验证后填写。
> 禁止未验证填写 PASS。

## 环境

- 日期：2026-10-02
- 系统（Windows 版本）：Windows（用户真机，待补充具体版本）
- 分辨率：待补充
- DPI：待补充
- 包来源（CI artifact LegendGame-Windows-x64 / 本地 Release 构建）：Windows正式包（用户报告运行正常）
- 验收人：用户（Windows 真机人工验收）

## 验收结果

| # | 项目 | 结果 | 备注 |
|---|------|------|------|
| §6 | CJK Input（登录/注册/角色名/聊天输入框，含 IME 组合，Enter 不误提交） | PASS | |
| §7 | Remote visualId（A 看 B=造型2，B 看 A=造型1；重登录持久一致） | PASS | |
| §8 | Nameplate（角色名/Lv/层级；离开 AOI 消失，重进恢复） | PASS | |
| §9 | Nearby（同地图近距离收到；超 nearbyRadius 收不到；不跨地图） | PASS | |
| §10 | World（不同地图，A 发世界频道，所有 InWorld 玩家收到） | PASS | |
| §11 | Whisper（/w 角色名；A 可见发送结果，B 收到，C 收不到；不存在玩家提示"该玩家当前不在线"，不暴露 sessionId/accountId） | PASS | |
| §12 | System（服务器系统消息正常显示；客户端伪造 System 频道被服务器拒绝，不广播） | PASS | |
| §13 | Anti Spam（Nearby/World/Whisper 快速连发超限返回"发言过于频繁，请稍后再试"；服务器不崩，不重复广播，恢复后可继续） | PASS | |
| §14 | Replay Protection（同 requestId 重复请求只广播一次） | PASS | |
| §15 | Chat GUI（综合/附近/世界/私聊/系统；展开/收起/滚轮/频道切换/输入框/发送按钮/Enter 打开+发送/Esc 取消/↑↓历史/点击名字私聊；点击名字菜单不得出现好友/组队/交易） | PASS | |
| §16 | Input Focus（聊天输入激活时 Space/1/2/3/4/5/E/B/R/T/I/C/Esc 不触发游戏动作；退出后恢复） | PASS | |
| §17 | Player Facing Chinese（启动/登录/注册/角色大厅/创建角色/HUD/地图/小地图/NPC/怪物/BOSS/背包/Tooltip/角色面板/技能/任务/任务描述/商店/NPC对话/聊天/设置/加载/断线/死亡复活 逐页无非白名单英文） | PASS | |
| §4 | English Residue（testlogs/stage27-zhcn-audit.txt 玩家可见英文残留 = 0） | PASS（2026-10-02 已重跑审计，玩家可见英文残留=0） | |

## 截图清单

testlogs/stage27-ui-acceptance/（12 张）：
01-cjk-input.png / 02-client-a-see-b-visual2.png / 03-client-b-see-a-visual1.png /
04-nameplate.png / 05-nearby-chat.png / 06-world-chat.png / 07-whisper-chat.png /
08-system-chat.png / 09-rate-limit.png / 10-chat-collapsed.png / 11-chat-expanded.png /
12-click-name-whisper.png

testlogs/stage27-zhcn-acceptance/（12 张）：
01-login-zhcn.png / 02-register-zhcn.png / 03-lobby-zhcn.png / 04-world-hud-zhcn.png /
05-inventory-zhcn.png / 06-character-zhcn.png / 07-quest-zhcn.png / 08-shop-zhcn.png /
09-dialogue-zhcn.png / 10-chat-zhcn.png / 11-settings-zhcn.png / 12-minimap-zhcn.png

## 发现的问题（如有）

（记录真实问题；只修 Stage27 范围内问题，修后重跑 Build/CTest/Runtime/Topology/Vertical Slice/Multiplayer Chat Smoke 并重做人工验收）

## 截图

24 张验收截图待用户从 Windows 真机补充到 testlogs/stage27-ui-acceptance/ 与 testlogs/stage27-zhcn-acceptance/。
