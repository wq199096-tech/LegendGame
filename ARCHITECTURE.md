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
  WorldData/     WorldDataJson（Data/World 5 文件 Load/Validate/Save/默认）+ AtomicFile
                 （原子写/backup 轮换）+ MonsterSpawnDefinition（GenerateSpawnPoints）【阶段22】
  GameData/      GameDataJson（Data/Game 9 文件 Load/Validate/Save/交叉校验/默认）【阶段23】
  Monster/       MonsterDefinition/LootTableDefinition/MonsterSpawnDefinition 定义【阶段23】
Tools/
  MapEditor/     LegendGame World Editor（TileMap + World 双工作区；WorldDocument +
                 GameDataDocument 文档模型；Validation 禁存；Launch WorldServer）【阶段22/23】
Data/
  World/         world_manifest/maps/npcs/monster_spawns/portals.json（5 文件，schemaVersion=1）【阶段22】
  Game/          game_manifest/items/monsters/skills/statuses/quests/shops/teleports/
                 loot_tables.json（9 文件，schemaVersion=1 + contentVersion）【阶段23】
Engine/          引擎（渲染/输入/网络 TcpServer/TcpClient/Logger）
Server/
  Gateway/       GatewayServer/Session（信封转发）
  LoginServer/   LoginServer + Account/(AccountService/CharacterService/SessionService/
                 TicketStore/AccountRepository/DbWorker/Database/DatabaseSchema)
  WorldServer/   WorldServer（编排中枢）+ WorldSession/WorldManager/WorldMapManager/PlayerSession
    AOI/WorldSpatialGrid      玩家/怪物 Uniform Grid（cellSize 400）
    Monster/                  Definition/Entity/Manager/AI/SpatialGrid/RespawnManager/
                              MonsterSpawnRegistry/MonsterDefinitionRegistry【阶段22/23】
    Combat/                   DamageCalculator/CombatService
    Skill/                    SkillRegistry/SkillService
    Status/                   StatusEffectRegistry/Service/Container
    Progression/              ProgressionService(纯逻辑)/RewardService(事件构建)
    Item/                     ItemRegistry/DropRoller/WorldItemDrop/InventoryContainer/
                              EquipmentService/InventoryRepository/LootTableRegistry【阶段23】
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
                 WorldInventoryChecks/WorldQuestChecks/【WorldNpcChecks 阶段20】/
                 WorldDataChecks/MapEditorDataChecks【阶段22】/
                 GameDataChecks/DefinitionValidationChecks【阶段23】（全部并入 LegendWorldTests）
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
- Definition 类（Map/NPC/MonsterSpawn/Portal/Item/Monster/Skill/Status/Quest/Shop/Teleport/
  LootTable）**不入库**——阶段22/23 起数据驱动：Data/World + Data/Game JSON 为单一事实来源，
  WorldServer Start 加载+校验（错误拒绝启动），8 类 Game Registry + 4 类 World Registry
  经 LoadFromDefinitions 注入（构造默认兜底防 magic-static 死锁）；掉落由服务器读
  LootTable 生成（Client 永不决定）

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

- **Multi-Map / Portal / Respawn【阶段21】**：WorldMapManager 按图管理（边界校验）+
  MapTransitionService 统一切换（NPC 传送/Portal/白盒全部走此服务；切图清全部镜像防 Ghost、
  OnTargetPlayerRemoved 丢追击怪目标）；Portal AOI/验证链（同图/距离/存在）F 交互；
  死亡复活链（3s 等待/10G 或 Town 免费/状态清空/保护期）R·T；MessageId 340~348；
  Client RemotePortalManager/ClientWorldModel/Map 面板 F9；怪物按图管理 + legacyMap1TestSpawn 兼容
- **数据驱动【阶段22/23】**：Start 顺序 = World 数据（Data/World 存在→Load+Validate+注入，
  缺失→出厂默认+WARN）→ Game 数据（LoadGameData→worldView 汇回→ValidateGameData 交叉→
  注入 8 Registry；失败拒绝启动）→ Data hash 打印（FNV-1a over Registry 计数）；
  SpawnInitialMonsters = legacy 20 点 + MonsterSpawnRegistry 展开（GenerateSpawnPoints
  xorshift64*，slotId 全局递增）；GenerateMonsterDrops = Monster lootTableId → LootTable 读表
- **Editor【阶段22/23】**：LegendGame World Editor + Game Data Content Editor（Tools/MapEditor）
  ——TileMap 工作区（Legacy 地图编辑）+ World 工作区（World Tree/Canvas Pan-Zoom 拾取拖拽/Inspector/
  Validation/Console）+ Data（Content）工作区（Game Tree 带搜索/8 类型 Inspector+Preview/
  Duplicate/Search/Game Data Registry 唯一 ID）；文档模型 Mutate() 唯一修改通道 + Undo/Redo 100 步快照；
  保存 = serialize→temp→reparse→replace 原子写 + .backup 10 份轮换；Validation Error 禁存；
  LEGEND_EDITOR_SMOKE=world 无头冒烟

## 关键类速查
- PlayerSession（io 线程权威状态：位置/HP/Mana/技能/状态/成长/背包/装备/任务容器/可见集合/请求历史）
- WorldServer（编排：全部 Handle* 入口 + Send* 广播 + Schedule* tick + Test* 白盒）
- DbWorker（Post/Flush；FIFO；Flush 等 队列空 && activeTasks==0）
- WorldTestHarness（WorldTestServers: StartLogin/StartWorld(dbPath/respawnDelay/questSnapshotIntervalMs 等可配)；WorldTestClient: recorded[64] 事件队列/ConnectAndEnter/WaitEvent）
- WorldDocument/GameDataDocument（Editor 文档模型：Mutate/Undo/Validation/Duplicate/Search/
  Suggest*Id/Find*；GameDataDocument 持 WorldDataSet 作交叉引用源）
- LoadWorldData/ValidateWorldData/MakeDefaultWorldData + LoadGameData/ValidateGameData/
  MakeDefaultGameData（Shared 数据层；SaveGameData 内部校验 crossReference=false）
