# LegendGame — 自研 PC MMORPG 项目

自研 Windows PC 2D/2.5D MMORPG。当前阶段：**Server-authoritative Monster & AI Core V0.13**。

- 语言：C++20
- 构建：CMake + FetchContent（自动下载 SDL3 / nlohmann-json / Dear ImGui / asio / SQLite3 / libsodium）
- 渲染：OpenGL 3.3 Core（自带精简 GL 函数加载器，不依赖 GLEW/GLAD）
- 网络：WinSock2 + asio standalone（TcpConnection/TcpClient/TcpServer，Gateway + LoginServer + WorldServer）
- 账号：SQLite3 持久化（账号/角色/Session，WAL 多进程并发），密码 Argon2id（libsodium），Session Token CSPRNG
- 世界：SelectionTicket 一次性交接 + 权威位置移动 + 位置周期/断线/关服保存
- 地图：数据驱动（JSON），Tile/Object/Collision/Occlusion 四层，Chunk 可视剔除 + SpriteBatch 批渲染 + Y-Sort
- 角色：Entity/Character/Controller 体系，8 方向移动，SpriteSheet + UVRect 动画（Idle/Walk × 8 方向 = 16 Clip）
- 图片解码：stb_image.h（单头文件，公有领域，位于 ThirdParty/stb）
- 平台：Windows 10 / 11

## 目录结构

```
LegendGame/
├─ Engine/          自研引擎（Core/Render/Resource/Scene/Input/Math/Debug/Animation/Audio）
├─ Client/          游戏客户端程序（LegendClient.exe）
├─ Shared/          引擎与客户端共用定义
├─ Assets/          游戏资源（Characters/Monsters/Maps/Effects/UI/Audio）
├─ Tools/           编辑工具（后续阶段）
├─ Config/          配置文件（后续阶段）
├─ ThirdParty/      第三方库预留目录（当前由 FetchContent 管理）
└─ Build/           构建输出
```

## 构建步骤（Windows）

前置要求：

1. Visual Studio 2022 Build Tools（勾选 “使用 C++ 的桌面开发” 工作负载，含 MSVC 与 Windows SDK）
2. CMake 3.24+

首次配置与编译：

```bat
cd LegendGame
cmake -S . -B Build
cmake --build Build --config Debug --parallel
```

> 第一次 Configure 时会自动从 GitHub 下载 SDL3 源码包并编译。

## 已知问题：中文路径（重要）

MSVC 的 CL.exe 在**含中文的构建路径**下会产生 MSB8084 结构化输出错误（如 `d:\传奇1`）。
如果工程放在含中文/非 ASCII 字符的目录下，请先创建一个 ASCII 路径的目录联接，再通过联接路径构建：

```bat
mklink /J d:\LegendGame d:\传奇1\LegendGame
cd /d d:\LegendGame
cmake -S . -B Build
cmake --build Build --config Debug --parallel
```

联接指向同一物理目录，通过任一路径访问到的文件完全一致。

## 运行

```bat
cd LegendGame
Build\bin\Debug\LegendClient.exe
Build\bin\Debug\LegendMapEditor.exe
```

建议在工程根目录下运行（这样能读到 `Assets/`，日志写在 `Logs/latest.log`）。

## 账号与角色系统（阶段10：Account & Character Core V0.10）

**服务器启动顺序**：

```bat
Build\bin\Debug\LegendLoginServer.exe          :: 127.0.0.1:7100
Build\bin\Debug\LegendGateway.exe              :: 127.0.0.1:7000
Build\bin\Debug\LegendWorldServer.exe          :: 127.0.0.1:7200（阶段11）
Build\bin\Debug\LegendClient.exe
```

**角色进入世界流程（阶段11）**：

1. Client → Gateway 注册/登录（AccountLoginRequest）
2. 角色列表 / 创建角色 / CharacterSelect → 返回一次性 SelectionTicket（60s）
3. Client 直连 WorldServer（7200）→ WorldHandshake → EnterWorldRequest（只带 ticket）
4. WorldServer → LoginServer 内部协议 ConsumeSelectionTicket（一次性消费，返回 accountId/characterId）
5. WorldServer 从 SQLite 加载角色 → PlayerSession → 进入 MapInstance(mapId=1)
6. Client 收到 EnterWorldResponse → WorldReady（本地参考位置 = 服务器位置）
7. 移动：Client 只发方向输入（PlayerMoveInput，dt clamp 0.1s、方向 Normalize、边界 Clamp 0~2000）
8. WorldServer 每 100ms 下发 PlayerPositionSnapshot（权威位置 + lastProcessedInputSequence）
9. 下线/异常断线/关服：保存 mapId/position/last_played_at（周期 30s + 断线 + 关服 Flush）
10. 重新登录 → CharacterSelect → 新 Ticket → EnterWorld → 位置从上次保存点恢复

**端口**：7000 Gateway / 7100 Login / 7200 World。

**WorldServer 细节**：SQLite WAL 多进程并发（Login 写 / World 读+位置更新）；
同角色不允许重复在线（CharacterAlreadyOnline）；Ticket 重放/过期 → 统一 InvalidTicket；
Login 链路断开时已在线玩家不受影响，WorldServer 自动重连。

**多玩家可见性与同步（阶段12：AOI & Multiplayer Replication Core V0.12）**：

- **AOI 权威在 WorldServer**：Client 不计算谁可见，只根据服务器 Spawn/Snapshot/Despawn 维护远程玩家
- **Uniform Grid / Spatial Hash Grid**（`Server/WorldServer/AOI/WorldSpatialGrid`）：cellSize=400，
  地图 2000×2000 约 5×5 cells；查询跨 cell（±2 格覆盖半径）；距离比较只用 distanceSquared（无 sqrt）
- **可见半径滞回**：EnterRadius=600（<=600 进入可见）/ LeaveRadius=700（已可见 >700 才离开），防边界抖动
- **AOI Tick**：200ms 一次（WorldServer m_aoiTimer），对每个玩家：Grid 候选 → 排除自己/跨地图 →
  滞回判定 → 与服务器权威 visiblePlayers（上限 128，距离近优先，同距离 characterId 升序）比较 → 只发
  Spawn/Despawn 差量（exactly-once；离开再进入重新 Spawn）
- **协议**：PlayerSpawn=230（全元数据）/ PlayerDespawn=231（reason：LeftAOI=1 / Disconnected=2 /
  ChangedMap=3 / ServerCleanup=4）/ RemotePlayerSnapshot=232 / RemotePlayerBatchSnapshot=233
  （serverTime + count + entries，单批 ≤128 超出拆包；count>128 或超剩余 payload 拒绝解码）
- **远程位置同步**：复用 100ms 快照 timer——自己的 PlayerPositionSnapshot + 可见玩家的 batch 快照
  （不含接收者自己，位置来自服务器权威 PlayerSession）
- **断线清理**：Grid/MapManager/可见集全清，所有能看到该玩家的收到 Despawn(Disconnected)，无 ghost
- **客户端**：RemotePlayerEntity/RemotePlayerManager（network thread → event queue → main thread），
  render = lerp(render, server, 1-exp(-12·dt)) 线性插值，位置差 >300 直接 snap（teleport correction）；
  远程玩家不接受本地输入，移动方向/亮暗色由服务器位置差估算（不同步动画帧/技能）；
  F12 World Debug 增加 remotes=N / batch=M / 远程玩家名列表
- **双开验收**：两个 LegendClient 不同账号进入同一地图，靠近互见、移动同步、走远消失、走近重现

**服务器权威怪物与基础 AI（阶段13：Server-authoritative Monster & AI Core V0.13）**：

- **怪物完全由 WorldServer 权威控制**：Client 不能生成/决定位置/AI 状态/目标，只根据
  MonsterSpawn / MonsterBatchSnapshot / MonsterDespawn 显示
- **MonsterDefinition**（`Server/WorldServer/Monster/`）：硬编码 Training Slime（monsterTypeId=1，
  level=1，moveSpeed=80，aggroRadius=350，leashRadius=600，patrolRadius=180）；不上 JSON 数据库
- **MonsterEntity / MonsterManager / MonsterSpatialGrid**：entityId 为 WorldServer 单调计数器；
  空间网格 cellSize=400（与玩家 AOI 同构）；怪物 runtime only（不进数据库，重启重新生成）
- **固定初始布局**：map1 固定 20 只 Training Slime（5 簇 x 4 只固定位置表，测试可复现）
- **AI 状态机**（AI Tick 200ms，World io 线程，dt clamp 0.25s）：
  Idle(固定 2s) → Patrol（spawn 附近 <=180 确定性目标点，entityId+patrolSequence 决定角度，
  到达 <=10 回 Idle）→ 玩家进入 aggro 350（经玩家 SpatialGrid 查询，最近优先/同距离
  characterId 最小）→ Chase（朝玩家权威位置 80 units/s 直线移动）→ 离 spawn>600（Leash）/
  目标>525（aggro*1.5）/ 目标断线 → Returning → 回到 spawn(<=10) → Idle。
  **禁止 Attack/Cast/Hit/Dead——怪物追到玩家身边也不攻击（Combat not implemented yet）**
- **怪物 AOI**：EnterRadius=600 / LeaveRadius=700 滞回（与玩家一致）；每玩家服务器权威
  visibleMonsters 集合；Spawn/Despawn exactly-once；离开再进入重新 Spawn
- **协议**：MonsterSpawn=240 / MonsterDespawn=241（LeftAOI/Removed/ChangedMap/ServerCleanup）/
  MonsterBatchSnapshot=242（serverTime+count+entries，单批 <=128 超出拆包，count>128 拒解码）；
  每 100ms 复用快照 timer 下发 visibleMonsters batch（不含远处怪物，禁止全图广播）
- **客户端**：RemoteMonsterEntity / RemoteMonsterManager（network thread → event queue →
  main thread）；插值 1-exp(-12dt)，位置差 >300 直接 snap；state 切换调试表现（不同步动画帧/
  技能）；Debug Quad 红色系（Idle 暗红/Patrol 橙/Chase 亮红/Returning 黄）与远程玩家绿色区分；
  F12 增加 monsters=N / mbatch=M
- **双开验收**：两个 Client 进入同一地图可见附近 Training Slime；怪物 Idle→Patrol，玩家靠近
  Chase，玩家跑远 Returning 回出生点 Idle；两个 Client 看到同一怪物位置与状态一致

**Account Database（SQLite3）**：

- 数据库文件：`data/legend_account.db`（`data/` 目录不存在时自动创建；Schema 自动初始化到 version 1）
- 表：`accounts` / `characters` / `sessions`（软删除：`characters.deleted=1`，物理行保留）
- **删除该数据库文件会清空全部账号/角色/Session 数据**
- 密码使用 **Argon2id**（libsodium `crypto_pwhash`，hash 内含 salt 与参数），绝不保存明文
- Session Token：256-bit CSPRNG（`randombytes_buf`），数据库只保存 SHA-256 hash，有效期默认 24 小时
- 连续登录失败 5 次锁定 60 秒；每账号最多 4 个角色；角色选择返回 60 秒一次性 selectionTicket

> **DEV ONLY — DO NOT EXPOSE TO INTERNET**：当前 TCP 链路无 TLS，密码明文过网，
> 全部服务仅绑定 127.0.0.1，仅供本机开发。公网部署必须等后续 TLS 阶段。

**客户端调试键（阶段10）**：

| 按键 | 功能 |
| --- | --- |
| F8 | Network Debug（阶段9 保留） |
| F9 | 连接 / 断开 Gateway |
| F10 | 自动登录开发账号 dev_user（不存在则自动注册；正式 Account 测试不依赖此快捷键） |
| Shift+F10 | LegacyDevLogin 测试（test/dev_token，阶段9 兼容链路） |
| F11 | Account Debug（AccountState/AccountId/Session/角色数/选中角色/最近错误） |

**测试**：

```bat
cd Build
ctest -C Debug --output-on-failure     :: NetworkTests（阶段9 回归）+ AccountTests（阶段10）
```

LegendAccountTests 覆盖：Schema/Migration、注册（含大小写不敏感重名）、Argon2id 密码哈希、
登录失败计数与锁定、Session 创建/恢复/过期/吊销、角色列表/创建/上限/删除（软删除）/归属校验/选择、
SelectionTicket 一次性消费与过期、畸形包容错、并发注册/并发同名注册/并发建角（事务上限）、
断线期间 DB 操作安全、服务器重启持久化、日志不落密码/Token/Ticket。


## Engine V0.2 操作说明

| 按键 | 功能 |
| --- | --- |
| W / A / S / D | 移动玩家（分轴碰撞，可沿墙滑动；不能穿水/墙/建筑/石头） |
| 方向键 | 自由移动摄像机（Camera Follow 关闭时） |
| 鼠标滚轮 | 摄像机缩放（0.25x ~ 4.0x，任意模式下可用） |
| F | 开启 / 关闭摄像机跟随玩家（跟随脚底 Feet Position） |
| F1 | 切换碰撞 Debug 可视化（Terrain/Manual/Object 合成结果，红色半透明） |
| F2 | 切换角色 Debug（脚底碰撞盒黄色矩形 + Feet 十字 + 标题显示 Direction/Clip/Frame） |
| TAB | 选择最近活怪（1000 内）；鼠标左键点怪选中 / 点空地清目标 |
| Space | 攻击当前目标（attack_hit 事件造成伤害） |
| E | 拾取最近地上掉落（80 内；背包满时保留地上不丢失） |
| F3 | AI Debug（Aggro/Leash 圈、Home 十字、目标连线） |
| F4 | Combat Debug（目标/冷却/血条/攻击距离圈） |
| F5 | Progression/Loot Debug（拾取范围圈、最近 5 件掉落连线） |
| F6 | Equipment Debug（6 槽状态方块 + Base/Final ATK 差值条；开启时输出槽位清单） |
| 1 | Power Slash（单体 180% Attack，MP15，CD4s，射程95） |
| 2 | Whirlwind（自身 AOE 120% Attack 半径120，MP25，CD6s，无需目标） |
| 3 | Piercing Strike（单体中距 150% Attack，MP20，CD5s，射程160） |
| 4 | Heavy Strike（单体近战 220% Attack，MP30，CD8s，射程90） |
| M | Debug：Mana 恢复满（日志 `[Skill] mana restored to 100/100`） |
| F7 | Skill Debug（蓝色 Mana 条 + 4 技能槽 CD 比例方块；标题显示 MP/各槽 CD/当前施法） |
| Z | 装备背包中第一件 Equipment（SkillCasting 中拒绝） |
| X | 卸下 Weapon（背包满时失败、装备留槽不丢失；SkillCasting 中拒绝） |
| ESC | 退出程序 |

窗口标题实时显示 `Map: TestMap | Chunks: 20 | Tiles: 5120 | DC: 56 | FPS: 60`，
用于验证 Chunk 剔除与批渲染（5120 个可见 Tile 只有几十次 DrawCall）。

## Map Editor（LegendMapEditor.exe）

- **File**：New Map / Open Map / Save / Save As（与游戏共用 `Engine/Map/MapLoader`）
- **View**：Collision Overlay 开关、Grid 开关、重置缩放
- **Palette**：Ground（绘制 Grass/Dirt/Stone/Water，左键绘制、拖动连刷）、
  Collision（左键设阻挡、右键清除）、Objects（放 Tree/Rock/Building，点选后可删除）
- **视口**：滚轮缩放、右/中键拖动平移

## 地图文件格式（Assets/Maps/TestMap/map.json）

```json
{
  "version": 1,
  "name": "TestMap",
  "tileSize": 64,
  "width": 100,
  "height": 100,
  "layers": [
    { "name": "Ground",    "type": "tile",      "visible": true, "data": [ ...10000 个 TileID... ] },
    { "name": "Objects",   "type": "object",    "visible": true, "objects": [ { "id":1, "name":"...", "textureId":"tree", "x":..., "y":..., "width":96, "height":96, "rotation":0, "renderOrder":0, "blocking":true, "occluder":true } ] },
    { "name": "Collision", "type": "collision", "visible": true, "data": [ ...10000 个 0/1... ] },
    { "name": "Occlusion", "type": "occlusion", "visible": true, "objects": [ 1, 2 ] }
  ]
}
```

- Tile ID：0=Empty 1=Grass 2=Dirt 3=Stone 4=Water（Water 默认阻挡）
- 坐标：世界坐标（像素）↔ Tile（64px）↔ Chunk（16x16 Tile = 1024px），负数/越界安全
- 编辑器保存后，客户端直接重新加载，无需重新编译

## 角色资源（Assets/Characters/TestHero/）

- `character.json`：名称 / SpriteSheet 路径 / 帧尺寸 / 视觉尺寸 / footprint（脚底碰撞区域）/ pivot（脚底锚点）/ 移动速度
- `animations.json`：16 个 Clip（idle_/walk_ × 8 方向），每帧 `{index, duration}`
- `sprites/hero_debug.png`：程序生成的 Debug SpriteSheet（6 帧 × 8 方向，48 帧，每方向独立颜色与朝向箭头）

坐标语义：`Character.Position = 脚底点（Feet）`，精灵按 pivot 向上绘制；
移动管线：`InputManager → PlayerController → CharacterController → Character → Map Collision → Position`。

Tools/gen_hero_sprites.ps1 可重新生成角色资源，Tools/gen_world_sprites.ps1 可重新生成 NPC/怪物资源。

## 世界角色（阶段4：World Actor System V0.4）

- Engine/Entity：`ActorType`（Player/NPC/Monster/Pet/Summon）、`EntityIdAllocator`（统一递增 ID）、
  `ActorRegistry`（Register/Unregister/Get/GetByType/FindInRadius）、`TargetHandle`（EntityId 句柄，注销后安全失效，不留野指针）
- Client/World：`NPCCharacter`（静态站立 + 固定朝向）、`MonsterCharacter`（模板数据 + 出生信息 + 目标句柄 + AI 参数）、
  `MonsterSpawner`（monster.json 模板缓存 + 按 map.json 生成）、`MonsterAIController`（Idle/Wander/Chase/ReturnHome）、
  `AggroTable`（基础仇恨框架）、`WorldActorManager`（所有权 + 注册表 + 统一更新 + 渲染收集）
- 数据驱动：`Assets/Monsters/monster.json`（aggro/leash/wander/stop/resume 参数）；
  `map.json` 顶层 `monsterSpawns`（模板/中心/数量/半径）与 `npcSpawns`（名称/character.json/朝向），旧地图缺字段不报错，编辑器保存原样写回
- AI 规则：感知 0.15s 节流 + DistanceSquared；Chase 复用 CharacterController 分轴碰撞；
  stop/resume 距离滞回（贴近停步，拉开 resume 才继续）；距 home > leashRange 清目标回出生点；
  回家途中不再 Aggro；Wander 持续走向单个目标（到达/超时/持续撞墙放弃）
- 调试：F3 显示最近 6 只怪的 Aggro 圈（黄）/ Leash 圈（红）/ Home 十字（绿）/ Wander 目标（蓝）/ 目标连线（橙）；
  窗口标题统计 `Actors/Visible/AI/Scans`
- 自动验收：`LEGEND_AUTO_AI_TEST=1` 时间线（Aggro -> Leash -> Wander），输出 [AggroCheck] / [LeashCheck] / [WanderCheck]；
  静态自检 [ActorRegistryCheck] / [TargetHandleCheck] / [SpawnerCheck]（Requested 17 / Spawned 17 / Failed 0）
- 测试地图：TestMap 生成 3 NPC（Guard/Merchant/Villager）+ 17 怪物（Slime x8 / Wolf x5 / Boar x4）
- `LEGEND_AI_SEED` 固定随机种子，可复现 AI 行为

## 日志

运行日志输出到控制台并写入 `Logs/latest.log`（目录不存在时自动创建），
包含启动、初始化、资源加载、错误与关闭全流程记录。

## 本阶段明确不包含

账号 / 登录 / 网关 / 数据库 / 正式地图 / 正式战斗（攻击 / 伤害 / HP / 技能 / 死亡 / 掉落）/ 装备 / 背包 /
聊天 / 商城 / 任务 / 公会 / 交易 / 组队 / 排行榜等 MMORPG 玩法功能，
将在引擎稳定后的后续阶段逐步实现。

## 战斗系统（阶段5：Combat Core System V0.5）

- Engine/Combat：`CombatStats`（maxHp/hp/attack/defense/attackRange/attackInterval，TakeDamage 下限1/Heal上限/IsAlive）、
  `CombatTarget`（EntityId 句柄，存在+active+alive 才有效）、`CombatResolver`（max(1, Attack-Defense)）、
  `CombatSystem`（ValidateAttack/ResolveAttack/ApplyDamage，统一伤害入口 + DamageEvent 队列）
- 动作状态：`CharacterActionState` Normal/Attacking/HitReact/Dead；Movement Lock（非 Normal 禁止移动）；
  动画优先级 Dead > HitReact > Attacking > Walk > Idle（NonLoop 播完自动恢复）
- 动画事件：AnimationFrame `event` 字段（如 attack_hit），`AnimationPlayer::ConsumeEvents()` 每帧消费一次
- 数据驱动：character.json `combat` 块（Player 500/80/20/90/0.8）；monster.json `combat` 块
  （Slime 120/20/5/65/1.5，Wolf 180/35/8/70/1.2，Boar 260/45/15/75/1.6）；map.json `respawnSeconds`（缺省 5）
- 目标选择：鼠标左键点怪（视觉 AABB HitTest）/ TAB 最近活怪（1000 内）/ 点空地清目标；NPC 不参战
- 仇恨：受击 `OnDamaged(sourceId, damage)` -> AddThreat，被打必反击
- 死亡/重生：HP=0 -> Dead（active 保持播放死亡动画）-> Corpse 1.5s -> Registry 注销 -> respawnSeconds 后
  原 SpawnArea 重生（新 EntityId，数量守恒）；Player 死亡 2s 后 Debug 复活（回出生点满血）
- 调试：F4（HP/Target/冷却/ActionState、攻击距离圈、最近 3 只怪状态）；选中目标红圈；怪物头顶血条；
  窗口标题 `HP: x/y`
- 自动验收：LEGEND_AUTO_COMBAT_TEST=1（选怪/连击/反击/死亡/despawn/重生/玩家复活）；静态自检
  [CombatStatsCheck]/[CombatResolverCheck]/[AttackCooldownCheck]/[AttackRangeCheck]/[AnimationEventCheck]/[DeathCheck]
- 动画资源：SpriteSheet 扩展为 15列x8行=120帧（idle2/walk4/attack4/hit1/death4 每方向行）

## 阶段6：Progression + Loot + Inventory Core V0.6

- 经验成长：`Engine/Progression`（ExperienceTable 统一 `100 * 1.5^(level-1)`，MAX_LEVEL=50 满级封顶
  currentExp 归 0；LevelSystem 支持一次大量经验连续升级）；`PlayerProgression` 组件挂在 PlayerCharacter
  （组合非继承），升级属性成长 character.json `growth` 块（MaxHP+20 且当前 HP 同步 +20 / Attack+5 / Defense+2）
- 死亡奖励：CombatSystem 在 HP>0 -> 0 的那次伤害产生一次 `DeathEvent`（victimId/killerId/position），
  `RewardSystem` 一次消费（exactly-once）：killer=Player 时发 `rewards.exp` 并 Roll 掉落（奖励与 Respawn 分离）
- 物品/背包：`Engine/Item`（ItemDefinition / ItemInstance / ItemDatabase 加载 `Assets/Items/items.json` /
  Inventory 20 格，Stack 先补未满同 id 栈再占空格，`InventoryAddResult{requested,added,remaining}` 满包不丢物）
- 掉落：monster.json `loot[]`（chance 0~1、min/max inclusive，非法 entry 跳过）；`LootManager` 掷落 +
  TTL 60s 过期（LEGEND_LOOT_TTL 可配）+ LEGEND_LOOT_SEED 可重复；GroundLoot 非 Character、不阻挡、
  进统一 Y-Sort 队列（Debug 色块渲染）
- 调试：E 拾取；F5 成长/掉落 Debug；窗口标题 `Lv / EXP / Bag`
- 自动验收：LEGEND_AUTO_PROGRESSION_TEST=1（击杀 -> Exp -> GroundLoot -> 拾取 -> 升级 -> 成长 -> Respawn）；
  静态自检 [ExperienceCheck]/[LevelGrowthCheck]/[ItemDatabaseCheck]/[InventoryStackCheck]/[InventoryFullCheck]/
  [LootRollCheck]/[GroundLootPickupCheck]/[PartialPickupCheck]/[DeathRewardCheck]
