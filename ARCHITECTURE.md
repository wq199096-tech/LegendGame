# ARCHITECTURE — LegendGame 模块架构

> 目录结构 / 模块职责 / 协议 / 服务关系 / 世界系统。随阶段持续更新。

## 目录
```
Shared/          协议与定义层（Server/Client 共用，仅依赖标准库）
  Network/       ByteReader/ByteWriter/PacketCodec/Protocol/MessageId
  Account/       账号协议（40~61 + 内部 100~121）
  World/         World 基础（200~221, 2550）
  Monster/       怪物协议（240~242）
  Combat/        战斗协议（250~256）
  Skill/         技能协议（260~266）
  Status/        状态协议（270~273）
  Progression/   成长协议（280~282）
  Item/          物品协议（290~300）
  Quest/         任务协议（310~319）
  Npc/           NPC 协议（320~326）【阶段20】
  Dialogue/      对话协议【阶段20】
  Shop/          商店协议【阶段20】
  Teleport/      传送协议【阶段20】
Engine/          引擎（渲染/输入/网络 TcpServer/TcpClient/Logger）
Server/
  Gateway/       GatewayServer/Session（信封转发）
  LoginServer/   LoginServer + Account/(AccountService/CharacterService/SessionService/
                 TicketStore/AccountRepository/DbWorker/Database/DatabaseSchema)
  WorldServer/   WorldServer（编排中枢）+ WorldSession/WorldManager/WorldMapManager/PlayerSession
    AOI/WorldSpatialGrid      玩家/怪物 Uniform Grid（cellSize 400）
    Monster/                  Definition/Entity/Manager/AI/SpatialGrid/RespawnManager
    Combat/                   DamageCalculator/CombatService
    Skill/                    SkillRegistry/SkillService
    Status/                   StatusEffectRegistry/Service/Container
    Progression/              ProgressionService(纯逻辑)/RewardService(事件构建)
    Item/                     ItemRegistry/DropRoller/WorldItemDrop/InventoryContainer/
                              EquipmentService/InventoryRepository
    Quest/                    QuestRegistry/QuestService/QuestRepository/PlayerQuestContainer
    Npc/                      NpcRegistry/NpcEntity/NpcManager/NpcSpatialGrid/
                              NpcInteractionService/DialogueService/ShopService/TeleportService【阶段20】
Client/
  Source/        LegendApp/GameScene（主循环/渲染/Debug overlay）
  Network/       GameNetworkClient/ClientNetworkController（账号链路）
  Account/       AccountClientController/CharacterSelectionController
  WorldNetwork/  WorldNetworkClient/WorldClientController/RemotePlayer*/RemoteMonster*/
                 RemoteItemModels/RemoteStatusEffect*/ClientQuestModel/RemoteNpc*/【阶段20】
  Character|Combat|Skill|World|Loot|Progression/   本地单机框架（阶段1~8 遗产，联机路径旁路）
Tests/           WorldTestHarness.h + WorldChecks/WorldAoiChecks/WorldMonsterChecks/
                 WorldCombatChecks/WorldSkillChecks/WorldStatusChecks/WorldProgressionChecks/
                 WorldInventoryChecks/WorldQuestChecks/【WorldNpcChecks 阶段20】
Apps/            各进程 main
.github/workflows/windows-build.yml
PROJECT_CONTEXT.md / CURRENT_STAGE.md / ARCHITECTURE.md / TRAE_CONTINUATION_CONTEXT.md
```

## MessageId 分段
| 段 | 用途 |
|---|---|
| 1~61 | 基础握手/心跳/账号（Gateway 7300 ↔ Login 7100；内部 100~121 Ticket） |
| 200~221 | World 基础（握手/EnterWorld/移动/快照） |
| 230~233 | 玩家 AOI 同步（Spawn/Despawn/RemoteSnapshot/Batch） |
| 240~242 | 怪物（Spawn/Despawn/Batch） |
| 250~256 | 战斗（Attack/CombatEvent/HealthSnapshot/MonsterDeath/PlayerDeath） |
| 260~266 | 技能（Cast/Started/Completed/Cancelled/Impact/Mana） |
| 270~273 | 状态效果（Applied/Updated/Removed/Snapshot） |
| 280~282 | 成长（RewardGranted/LevelUp/ProgressionSnapshot） |
| 290~300 | 物品（WorldItemSpawn/Despawn/Pickup/InventorySnapshot/Delta/Equip/Unequip/EquipmentSnapshot） |
| 310~319 | 任务（Accept/TurnIn/Abandon 请求响应 + ProgressUpdated/StateChanged/Snapshot/RewardGranted） |
| 320+ | NPC（NpcSpawn=320/NpcDespawn=321/Interact=322/323/DialogueOption=324/Payload=325/Marker=326/ShopOpen=327/328/Buy=329/330/Sell=331/332/Teleport=333/334）【阶段20】 |

## 服务关系
```
Client --账号--> Gateway 7300 --转发--> LoginServer 7100（DB: accounts/sessions）
Client --ticket--> WorldServer 7200 --ConsumeTicket(120/121)--> LoginServer
WorldServer --共享 SQLite--> character_*/inventory_items/character_equipment/character_quests*
LoginServer --Ticket--> Client --直连 7200--> WorldServer（世界链路）
WorldServer 内部：io 线程（游戏逻辑/广播） + DbWorker 线程（全部 SQLite 写）
```

## 数据库 Schema（当前 v4）
- accounts / characters(+gold) / sessions / inventory_items / character_equipment /
  character_quests(PK character_id,quest_id) / character_quest_objectives(PK 三列)
- Definition 类（Monster/Skill/Status/Item/Quest/NPC/Shop/Teleport/Dialogue）全部代码硬编码

## 世界系统要点（WorldServer）
- **AOI**：玩家/怪物/掉落/NPC 各自可见集合（visiblePlayers/visibleMonsters/visibleItemDrops/visibleNpcs），
  Enter 600 / Leave 700 滞回；玩家 AOI tick 200ms、怪物 AI 200ms、掉落 cleanup 500ms、
  HP 纠偏 1s、Mana 快照 1s、状态 100ms、状态快照 2s、Respawn 250ms（delay 8s）、
  Progression 快照 30s、Quest 快照 10s
- **Combat**：验证链 AttackerDead→NotInWorld→TargetDead→InvalidTarget→DifferentMap→OutOfRange→Cooldown；
  有效攻击属性 = Base/Level + Equipment + Status
- **Quest**：QuestService 纯规则；Quest Repository DB；Accept 校验链→初始校验→事件推进
  （Kill 最终归属/Collect 持有量型/ReachLevel/ReachArea）→ ReadyToTurnIn → TurnIn 原子事务
  （Completed+成长+物品）；离线 killer 经 OfflineAdvanceKill DB 推进
- **Inventory**：40 格、堆叠 99（Material）、装备码 slot_index=1001/1002、remove-before-grant 拾取、
  DB 失败回滚
- **NPC【阶段20】**：静态 4 NPC（不移动/不死亡/不参与战斗）；NpcSpatialGrid cellSize 400；
  Interact 验证链（存在/Alive/active/同图/visibleNpcs/距离≤120）→ DialogueSession（30s TTL，
  走远/死亡/断线失效）→ 一层菜单 Options（Quest/Shop/Teleport/Close，≤16）→ 全部复用
  QuestService/ShopService/TeleportService；Marker per-player（None<InProgress<Available<ReadyToTurnIn）

## 关键类速查
- PlayerSession（io 线程权威状态：位置/HP/Mana/技能/状态/成长/背包/装备/任务容器/可见集合/请求历史）
- WorldServer（编排：全部 Handle* 入口 + Send* 广播 + Schedule* tick + Test* 白盒）
- DbWorker（Post/Flush；FIFO；Flush 等 队列空 && activeTasks==0）
- WorldTestHarness（WorldTestServers: StartLogin/StartWorld(dbPath/respawnDelay/questSnapshotIntervalMs 等可配)；WorldTestClient: recorded[64] 事件队列/ConnectAndEnter/WaitEvent）
