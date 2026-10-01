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
  GameData/      GameDataJson（Data/Game 10 文件 Load/Validate/Save/交叉校验/默认；
                 chapters.json 为可选展示文件【阶段23/25】）
  Monster/       MonsterDefinition/LootTableDefinition/MonsterSpawnDefinition 定义【阶段23】
Tools/
  MapEditor/     LegendGame World Editor（TileMap + World 双工作区；WorldDocument +
                 GameDataDocument 文档模型；Validation 禁存；Asset Browser /
                 Animation Preview / Quest Flow / Boss Editor / Chapter Editor /
                 Quest Area Map Picker / Validate All / Launch Full Game /
                 Stop Local Game / Process Status）【阶段22/23/25】
Data/
  World/         world_manifest/maps/npcs/monster_spawns/portals/visual_maps.json
                 （6 文件，schemaVersion=1；visual_maps 为地图视觉定义【阶段24】）
  Game/          game_manifest/items/monsters/skills/statuses/quests/shops/teleports/
                 loot_tables/chapters.json（10 文件，schemaVersion=1 + contentVersion；
                 chapters.json 为章节展示元数据，加载可选【阶段25】）
  Assets/        asset_manifest/animations/visual_entities/effects.json
                 （4 文件，Client 视觉资产域【阶段24】）
Engine/          引擎（渲染/输入/网络 TcpServer/TcpClient/Logger）
Server/
  Common/        ServerConfig + PersistenceClient + LogClient（超时/心跳/指数退避）
  Gateway/       GatewayServer/Session（统一接入、状态机、Login/Character/World 路由）
  Character/     CharacterServer（角色 List/Create/Delete/Select + SelectionTicket）
  Db/            DbServer（SQLite/Schema/DbWorker 的正式访问入口）
  Log/           LogServer（内部审计队列 + 每日 JSONL）
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
  Source/        LegendApp/GameScene（主循环/渲染/Debug overlay；在线模式渲染由 VisualRuntime 接管【阶段24】；
                 阶段25 UI 键位 I/C/Esc + UiRequest 分发 + VS Smoke）
  Assets/        AssetManager（manifest 驱动纹理缓存/Fallback/F10 热重载）【阶段24】
  Audio/         AudioRuntime（SDL3 程序化合成 BGM/SFX；Settings 音量实时生效）【阶段25】
  Ui/            LegendClientUi 静态库：UiTheme（1920×1080 参考分辨率缩放）+ UiModels
                 （Toast/MapBanner/BossBar/SkillSlot/HUD/Tracker/Inventory/Shop/Dialogue/
                 CharacterPanel/Minimap/LevelUpFx）+ ItemDisplayCatalog（items.json 展示字段）
                 【阶段25；WorldTests UiModelChecks 共用】
  Visuals/       VisualAssetData（Data/Assets 4 文件解析/校验）+ AnimationPlayer（统一）+
                 VisualDataCatalog（展示目录；阶段25 增加 Quest 目标展示字段）+ Font（stb_truetype UTF-8 文本）+
                 VisualRuntime（地图视觉/Y排序实体/技能 VFX/伤害飘字/名字板/HUD；
                 阶段25 扩展：正式 UI 全套窗口/Toast/Banner/BossBar/MiniMap/Loading/客户端设置持久化）【阶段24/25】
  Network/       GameNetworkClient/ClientNetworkController（账号链路）
  Account/       AccountClientController/CharacterSelectionController
  WorldNetwork/  WorldNetworkClient/WorldClientController/RemotePlayer*/RemoteMonster*/
                 RemoteItemModels/RemoteStatusEffect*/ClientQuestModel/RemoteNpc*/【阶段20】
                 （阶段25：LocalAttackPower/LocalDefensePower 展示镜像 + SendBuyByIndex）
  Character|Combat|Skill|World|Loot|Progression/   本地单机框架（阶段1~8 遗产，联机路径旁路）
Tests/           WorldTestHarness.h + WorldChecks/WorldAoiChecks/WorldMonsterChecks/
                 WorldCombatChecks/WorldSkillChecks/WorldStatusChecks/WorldProgressionChecks/
                 WorldInventoryChecks/WorldQuestChecks/WorldNpcChecks/
                 WorldDataChecks/MapEditorDataChecks/
                 GameDataChecks/DefinitionValidationChecks/
                 AssetManifestChecks/AnimationChecks/VisualDefinitionChecks/ClientSmokeChecks
                 【阶段24】/ VerticalSliceChecks（新角色出生）+ UiModelChecks + ChapterOneChecks
                 （Chapter One 端到端：真实生产数据全链路）【阶段25】（全部并入 LegendWorldTests）
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
Client
  |
  v
Gateway 7300
  |-- Login/Session ------> LoginServer 7100 ----\
  |-- Character/Select --> CharacterServer 7400 --+--> DbServer 7500 --> SQLite/WAL
  `-- World/Game --------> WorldServer 7200 -----/

LoginServer / CharacterServer / WorldServer --> LogServer 7600 --> Logs/Services/YYYY-MM-DD/*.jsonl
```

- 客户端只连接 Gateway；世界频道由 Gateway 为每个客户端建立独立后端连接并透明代理。
- 正式 LoginServer 的注册、Argon2id 登录和 Session 校验通过共享 `PersistenceClient` 访问 DbServer。
- CharacterServer 独占角色列表/创建/删除/选择及 Ticket 签发，WorldServer 只消费 Ticket。
- 内部协议版本独立为 1，含 ServiceType/InstanceId 握手、requestId、统一错误码、Heartbeat、严格解码和 1 MiB 上限。
- `Config/servers.json` 是本地服务拓扑单一入口；默认端口 7100/7200/7300/7400/7500/7600。
- **阶段25.5：World 持久化全部经 DbServer RPC 收口**——进世界三段加载
  （LoadCharacterFull → LoadInventory → LoadQuestState，单连接 FIFO 保序）与全部写操作
  （位置/成长/金币/拾取/装备/卸下/任务接取-推进-提交-弃置/离线推进/商店买卖）通过
  共享 `PersistenceClient` 执行，事务（Equip/Unequip/TurnIn/ShopBuy/ShopSell）在 DbServer
  DB Worker 内原子提交；World io 线程零 SQLite。dbPort=0 的 legacy 本地 DB 路径仅为
  隔离测试保留（WorldTestHarness 单测夹具），同一 Repository 层两路共用，禁止新增
  World 侧 SQL。DbServer 不可用时 World 进入 Persistence Degraded（写失败回滚内存并
  ERROR 日志，下个周期重试；进世界加载失败明确拒绝）。

## 数据库 Schema（当前 v5）
- accounts / characters(+gold) / sessions / inventory_items / character_equipment /
  character_quests(PK character_id,quest_id) / character_quest_objectives(PK 三列)
- Migration 5 为 `characters` 增加 `record_version`，供 Db RPC 做乐观并发保护。
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
- **Editor Studio UI【Stage25 Patch】**：单一 `LegendMapEditor.exe` 品牌升级为 LegendGame
  Studio。`EditorTheme` 是颜色/间距/圆角/控件状态单一入口；`EditorStrings` 管理简体中文
  UI 常量、生产内容显示名与搜索别名（不写回 Definition）。ImGui 初始化时根据
  `SDL_GetWindowDisplayScale` 缩放，并运行时探测 msyh/simhei/simsun + ChineseFull glyph。
  主布局 = 中文菜单 + 快捷工具栏 + 可拖动 Content/Workspace/Inspector + 可拖动底部
  Validation/Console/Process + 状态栏；Canvas 直接消费 WorldData visualMaps 和
  VisualDataCatalog/纹理缓存绘制真实地图视觉，Debug Overlay 独立开关。
- **Visual Runtime【阶段24】**：LegendClient 在线模式默认画面走真实资源渲染（离线 Debug 路径不变）。
  分层：Client/Visuals/VisualAssetData（Data/Assets 4 JSON 解析+校验+交叉引用）→
  VisualDataCatalog（展示目录：技能名/CD/任务标题/monster+portal visualId/map visualMapId/
  visual_maps；Data root 自动定位）→ AssetManager（manifest 驱动纹理缓存 + 紫棋盘 Fallback +
  F10 热重载失败保留旧资源）→ VisualRuntime（渲染编排）。渲染顺序 = Ground→Decoration→
  Y排序世界实体（含 Object 层，2.5D）→Foreground→Effects→名字板/血条/飘字→HUD。
  实体视觉 = 统一 AnimationPlayer（帧网格：行=Direction8 顺序 8 方向、列=帧）；
  玩家 8 方向 Idle/Walk/Attack/Cast/Hit/Death；Monster/NPC/Portal 由 visualId 引用；
  NPC 数值 visualId（协议不变）经 visual_entities serverVisualId 别名映射。
  技能 VFX：1001 刀光/1002 Projectile+Impact（视觉 only，伤害服务器权威）/1003 旋风。
  事件入口 = WorldClientController::SetVisualEventHook（只读转发，不修改镜像）。
  Font = stb_truetype 动态字形图集 + UTF-8（系统字体回退链：Assets/Fonts→msyh→simhei→arial）。
  F9 = 性能统计面板（Sprites/DrawCalls/Textures/FX）；F10 = 热重载（Dev AutoLogin 迁至 Ctrl+F10）。
  WorldServer 亦加载校验 visual_maps.json（第 6 World 文件；视觉字段不入 hash 语义）
  Client Smoke（指令四十七）= LEGEND_CLIENT_VISUAL_SMOKE=1 → 15s 存活干净退出，
  [VisualSmoke] 里程碑标记由 CTest ClientSmokeChecks 断言

## 关键类速查
- PlayerSession（io 线程权威状态：位置/HP/Mana/技能/状态/成长/背包/装备/任务容器/可见集合/请求历史）
- WorldServer（编排：全部 Handle* 入口 + Send* 广播 + Schedule* tick + Test* 白盒）
- DbWorker（Post/Flush；FIFO；Flush 等 队列空 && activeTasks==0）
- WorldTestHarness（WorldTestServers: StartLogin/StartWorld(dbPath/respawnDelay/questSnapshotIntervalMs 等可配)；WorldTestClient: recorded[64] 事件队列/ConnectAndEnter/WaitEvent）
- WorldDocument/GameDataDocument（Editor 文档模型：Mutate/Undo/Validation/Duplicate/Search/
  Suggest*Id/Find*；GameDataDocument 持 WorldDataSet 作交叉引用源）
- VisualRuntime（阶段24：client 视觉编排。OnWorldEvent 只读事件消费/Update 实体+特效+飘字/
  RenderWorld+RenderOverlays+RenderHUD；EntityVisual per-id 统一 AnimationPlayer）
- AssetManager（阶段24：manifest 纹理缓存。GetTexture 永不返回 nullptr——缺失走紫棋盘
  Fallback + 去重日志；ReloadAll 失败保留旧纹理）
- VisualDataCatalog（阶段24：展示目录。FindDataRoot 定位 Data/；Load = Data/Assets 4 文件
  + skills/quests/monsters/maps/portals/visual_maps 展示字段；仅视觉/展示用途）
- AnimationPlayer（阶段24：统一播放器。Play/Stop/SetDirection/Update/CurrentFrame UV；
  帧 UV = 行(方向)×列(帧) 网格；同一 clip 重 Play 保持进度）
- LoadWorldData/ValidateWorldData/MakeDefaultWorldData + LoadGameData/ValidateGameData/
  MakeDefaultGameData（Shared 数据层；SaveGameData 内部校验 crossReference=false）
