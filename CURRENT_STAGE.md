# CURRENT_STAGE — 阶段20 进行中

## 当前阶段
- **阶段20 —— NPC / Dialogue / Shop / Teleport / Quest Interaction Core V0.20**
- 前置确认：阶段19已修复（HEAD a674786，Actions run 36527767372 = success，三套件 0 failures）✔

## 阶段目标（指令摘要）
玩家靠近 NPC → 服务器验证可交互 → Interact → 服务器返回对话（一层菜单）→
Quest 接取/提交（复用阶段19 QuestService）/ 打开商店 / 传送 → 服务器权威执行。

固定 4 NPC：5001 Village Elder(QuestGiver 300,300 任务4001/4002/4003/4005)、
5002 General Merchant(Merchant 450,300 Shop6001)、5003 Wayfarer(Teleporter 600,300 Teleport7001)、
5004 Explorer Guide(MultiFunction 750,300 Quest4004+Teleport7002)。interactionRange=120。

固定内容：Shop6001（Core buy10/sell3、Sword 100/30、Armor 120/40）；
Teleport7001（→1500,1500 费20G）、7002（→300,300 免费）；QuestDefinition 增加 startNpcDefinitionId/turnInNpcDefinitionId
（4001/4002/4003/4005=5001/5001，4004=5004/5004）。

## Todo（按实现顺序）
- [x] 前置确认阶段19
- [x] 创建 4 个上下文文件（本文件 + PROJECT_CONTEXT/ARCHITECTURE/TRAE_CONTINUATION_CONTEXT）
- [x] Shared：Npc/Dialogue/Shop/Teleport（NpcTypes/NpcDefinition/NpcProtocol/NpcError；DialogueTypes/DialogueDefinition/DialogueProtocol；ShopTypes/ShopDefinition/ShopProtocol；TeleportTypes/TeleportDefinition/TeleportProtocol）+ MessageId 320~334 + QuestDefinition 增加 start/turnInNpcDefinitionId + SkillCancelReason::Teleported
- [x] Server：NpcRegistry/NpcEntity/NpcManager/NpcSpatialGrid + WorldServer 启动生成 4 NPC
- [x] NPC AOI（cellSize400、Enter600/Leave700、PlayerSession.visibleNpcs、NpcSpawn=320/NpcDespawn=321 单 NPC 包）
- [x] Interact（322/323）：验证链（Player 存在/Alive/NPC 存在 active/同图/visibleNpcs/距离≤interactionRange）→ Dialogue Session（uint64 单调，30s TTL，走远/死亡/断线失效）
- [x] Dialogue（324 OptionRequest/325 DialoguePayload）：一层菜单 ≤16 Option（optionId/type[Quest|Shop|Teleport|Close]/referenceId/label）；Village Elder 按任务状态动态生成（不显示不满足前置的 Accept）
- [x] Quest NPC 集成：复用 QuestService（阶段19 packet handler 抽出 AcceptQuestForPlayer/BeginQuestTurnIn 核心）；Marker（None<InProgress<Available<ReadyToTurnIn，per-player）→ NpcQuestMarkerUpdate=326
- [x] Shop（327 OpenRequest/328 OpenResponse/329 Buy/330 BuyResp/331 Sell/332 SellResp）：必须有效 Dialogue Session；ShopSession TTL 30s；Buy 校验（session/NPC/同图/距离/canBuy/quantity 1~99 装备=1/Gold 足够/背包空间/uint64 防溢出）→ 原子事务（Gold 扣 + Inventory 写）；Sell 从背包实例取 definitionId、canSell、装备不能卖、Material 部分卖；购买触发 OnInventoryChanged；ReadyToTurnIn 冻结不回退
- [x] Teleport（333 Request/334 Response）：有效 Dialogue Session + NPC 提供该 teleportId + Alive + Gold + minLevel；扣 Gold → 权威位置更新 → WorldMapManager/SpatialGrid/AOI 重置（旧区 Despawn、visible* 全清、重初始化）→ 立即发权威位置快照 → QuestService::OnPlayerMoved（可完成 Explorer）→ Cast 取消（SkillCancelReason::Teleported）→ 关闭 Dialogue/Shop Session
- [x] Client：RemoteNpcEntity/RemoteNpcManager、ClientDialogueModel/ClientShopModel；GameScene Debug（NPC Quad+名字+Marker）；E 交互（优先 NPC≤120 否则拾取）、对话 Options 数字键 1~9、Shop B 买 / S 卖
- [x] Tests/WorldNpcChecks.cpp（纯逻辑 + 真实链路全部 Check）+ 加入 LegendWorldTests
- [x] README（NPC & Interaction Core V0.20 章节 + 按键表）
- [x] 提交 `feat(world): add npc dialogue shop teleport and quest interaction core`（8032ee0 已推送）
- [ ] **Actions 绿（run 36545873665 = failure，修复中——见"CI 失败修复"）**

## CI 失败修复（round1 run 36545873665 / round2 run 36558569975 均 failure，修复中）
- round1（8032ee0）5 FAIL：DotKill/PickupDb/DropExpire（A 未 Buff → CI 慢机攻击循环拉长 →
  A 被围殴致死 → 级联，已修复验证）+ DialogueSessionTtl/MoveOutOfRange（单次交互超时）。
- round2（759f7d9）仅剩 Dialogue 2 FAIL。[Diag] 实锤：重进世界后商人 5002（entity 2，
  cell(1,0)）12s+ 不进 visibleNpcs；长老 5001（cell(0,0)）正常。服务器静态排查无果
  （QueryRange/NpcEntity/Grid/Manager/Start/Stop/AOI tick 全部正确；种子位 (360,300)）。
- round3：重进后 move+wait（(372,306)+600ms，同死亡检查通过模式）+ 可见性等待 8s +
  失败时打印服务器状态（pos/map/visibleNpcs）+ workflow diag 上限 12。详见 TRAE_CONTINUATION_CONTEXT。

## 本阶段编译修复记录
- NpcError.h 缺 `#include "Shared/Npc/NpcTypes.h"`（Client 侧 81 个 C2065/C2653）
- WorldNpcChecks.cpp：`far`/`home` 是 Windows 宏（minwindef.h）→ 改名 farTp/homeTp；
  多余 `} // namespace` 提前闭合 worldtest → 删除；`bool ok` 重定义 → 改赋值；
  FindPlayerByCharacter 返回 shared_ptr 不能用 `const auto*` → 改 `const auto`
- RunOnWorldIo 补 void 特化（if constexpr std::is_void_v<R>）
- **关键**：WorldTestClient::lastEvent[64] 按值数组 × 膨胀后的 WorldNetworkEvent（阶段20 新增
  Dialogue/Shop/Teleport payload）≈ 100KB/客户端 → Monster 套件 12 个栈上客户端函数序言
  __chkstk 直接打穿 1MB 主线程栈（0xC00000FD，任何语句执行前崩溃）→ lastEvent 改 std::vector 堆分配

## 本地全量测试（阶段20 最终）
- LegendWorldTests：**434 PASS / 0 FAIL**（含 WorldNpcChecks 纯逻辑+真实链路全部检查）
- LegendNetworkTests：**0 FAIL**；LegendAccountTests：**0 FAIL**（回归无破坏）
- 修复历程：首轮 0xC00000FD 栈溢出（harness lastEvent 数组→堆）→ 19 FAIL（requestId 复用/
  死亡级联/Marker 基线/TTL 干扰）→ 5 FAIL（传送扣金未落库/NotVisible 竞态/位置设计）→
  1 FAIL（DeathInvalidates 未复活）→ **0 FAIL**

## 已完成
- 阶段19 确认修复（本文件顶部）
- 阶段20 全部代码/测试/README；本地三套件全绿

## 阻塞项
- 无

## 当前 Actions
- 最新：run 36527767372（a674786）= **success**（阶段20 提交后待更新）
