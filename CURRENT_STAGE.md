# CURRENT_STAGE — 阶段21 完成（待 Actions 确认）

## 当前阶段
- **阶段21 —— Multi-Map World / Portal / Respawn Core V0.21**
- 前置确认：阶段20 已封板（bddd8b1；run #42 = success）✔
- 指令全文：119 条（会话输入/Trae long-text）

## 阶段目标（摘要）
3 张服务器权威地图（Map1 Greenfield Village / Map2 Slime Meadow / Map3 Ancient Ruins）+
固定 Portal 8001~8004 + 统一 MapTransitionService（Portal/NPC 传送/复活共用）+
死亡-复活完整链（3 秒门槛/10G-免费双模式/状态清空/3 秒保护）+ 持久化与非法存档修正。

## Todo（全部完成）
- [x] 前置确认（git/Actions/阶段20封板）
- [x] 勘察（MessageId/WorldMapManager/PlayerSession/Monster/Drop/Teleport/EnterWorld/Client/Tests）
- [x] Shared：WorldMap（MapTypes/MapDefinition/MapProtocol）+ RespawnProtocol + Portal 三件套 + MessageId 340~348
- [x] Server：MapRegistry/MapTransitionService(friend)/RespawnService/PortalRegistry/PortalManager/PortalSpatialGrid
- [x] Server：多地图 WorldMapManager（按图边界钳制）/按图怪物生成（legacy 开关）/AOI 同图过滤
- [x] Server：Portal AOI/Use 验证链/MapChanged/MapSnapshot/死亡复活链/保护/持久化修正/装备死亡拒绝
- [x] Client：RemotePortal/ClientWorldMapModel/F·R·T 键/F9 面板/死亡 Overlay/切图镜像清理
- [x] Tests/WorldMapChecks.cpp（纯逻辑 + 真实链路 40+ 检查）+ CMake + README + 按键表
- [x] 本地全量测试 0 FAIL（Network 12.3s / Account 16.8s / World 390.2s = 100% passed）→ 8 exe 齐全
- [x] 提交 `feat(world): add multi-map portal and player respawn core` → push
- [ ] Actions 绿（提交后轮询中——完成后本文件已同步，见 TRAE_CONTINUATION_CONTEXT 的最终 HEAD）

## 关键实现决策（防止后续重写）
- MapTransitionService 持 WorldServer&（friend）——12 步序列 + OnTargetPlayerRemoved（指令四十九：
  切图时原图追击怪物立刻丢目标 Returning）+ Quest OnPlayerMoved；NPC 7001/7002 与
  TestTeleportPlayer 白盒全部改走它（指令五十三/一百零二）；同图切换也走完整序列
- mapTransitionInProgress 并发保护（指令六十）；TransitionPlayer 失败路径：地图不存在/越界/并发
- 多地图怪物：RespawnManager::InitializeFromPoints（slotId 分段 1=legacy map1 / 100+=map2 / 200+=map3）；
  legacyMap1TestSpawn 生产 false / 测试 harness true（阶段13~20 套件兼容——旧数量断言 20→50/19→49）
- enter 修正（指令二十九）：非法 mapId→Town spawn；越界位置→该图 spawn；修正后立即重存
- 复活：RespawnService::PlanRespawn 纯规则（mode/死亡/3s/Gold）→ WorldServer 扣金→清状态容器→
  Revive（HP/Mana 满）→TransitionPlayer→SetRespawnProtection(3s)→**RespawnResponse 成功回执**→
  PlayerRespawned+HP/Mana 快照（初版漏发成功 RespawnResponse——测试抓出已修）
- 保护：respawnProtectedUntil runtime flag；TryMonsterAttack 伤害前检查；攻击/施法立即 Clear
- MessageId 340~348：PortalSpawn/Despawn/UseReq/UseResp/MapChanged/MapSnapshot/RespawnReq/Resp/PlayerRespawned

## 本阶段调试教训（重要）
- **测试移动后立即交互/使用 Portal 会撞 AOI tick（200ms）NotVisible 竞态**——发送前必须等
  服务端 visibleXxx 收录（WaitUntil server-side）；本阶段 PortalSuccess/Duplicate 均栽过
- **respawn 时序**：DeathBlocks 等待耗时不可控 → 每个 respawn 检查前重新 MarkDead 重置 3s 时钟
- **Stage20 教训重演**：`Select-Object -First N` 实时截断管道会杀 ctest → 输出必须落盘文件再 grep
- 旧套件兼容：多地图布局改的每处数量断言（20→50）都要逐一排查（Monster/Combat/Status/Progression）

## 本地全量测试（阶段21 最终）
- LegendWorldTests：全部 PASS（含 WorldMapChecks 纯逻辑+真实链路 40+ 检查），390s
- LegendNetworkTests：PASS（12.3s）；LegendAccountTests：PASS（16.8s）
- 8 exe 全部构建 ✓

## 阻塞项
- 无

## 当前 Actions
- 提交后轮询中（结果见 TRAE_CONTINUATION_CONTEXT 最终记录）
