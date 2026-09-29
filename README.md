# LegendGame — 自研 PC MMORPG 项目

自研 Windows PC 2D/2.5D MMORPG。当前阶段：**Quest Core V0.19**。

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

**服务器权威战斗与伤害（阶段14：Server-authoritative Combat & Damage Core V0.14）**：

- **战斗 100% WorldServer 权威**：Client 只发"我想攻击谁"（PlayerAttackRequest 只含
  requestId/targetEntityType/targetEntityId），禁止传攻击坐标/hitbox/damage；伤害/命中/扣血/
  死亡全部由服务器决定；Client 不做本地伤害预测（按 Space 不先扣血，等 CombatEvent）
- **CombatService / DamageCalculator**（`Server/WorldServer/Combat/`）：纯函数验证链
  （AttackerDead → NotInWorld → TargetDead → InvalidTarget(不可见) → DifferentMap →
  OutOfRange → Cooldown）；伤害公式 `damage = max(1, attackPower - defense)`，不随机不暴击
- **Player 战斗属性（固定默认）**：maxHp=100 / attackPower=20 / defense=5 / attackRange=100 /
  attackCooldown=0.8s；**Training Slime 战斗属性**：maxHp=80 / attackPower=10 / defense=2 /
  attackRange=60 / attackCooldown=1.2s（玩家打史莱姆 18/次，史莱姆打玩家 5/次）
- **攻击验证细节**：目标必须在攻击者 visibleMonsters（防远程作弊，AOI 离开立即失效）；
  每玩家最近 64 个攻击 requestId 防网络重放（重复 → DuplicateRequest 只扣一次血）；
  冷却用 WorldServer steady_clock（Client 时间不可信）；连续 100 连发仍受 CD 限制；
  targetType=Player 阶段14 一律 InvalidTarget（不做 PvP，禁止 Monster 互打）
- **CombatEvent 广播**：eventId 为服务器单调 uint64（客户端每实体 lastCombatEventId
  乱序旧包忽略）；广播范围 = 目标怪观察者 ∪ 攻击者本人（怪物攻击玩家 = 受害者观察者 ∪
  受害者本人），去重、不全世界广播；EntityHealthSnapshot 每 1s 纠偏（单条）
- **Monster AI 扩展**：Chase 距目标 <= attackRange(60) 停止移动（不贴脸穿模），由 AI Tick
  触发普通攻击（1.2s CD）；杀死玩家 → target 清空 Returning；目标死亡/断线 → Returning
- **死亡生命周期**：怪 HP<=0 → alive=false / state=Dead / AI 停止 / MonsterDeath 广播 →
  3 秒后统一 AI Tick 清理（不建 per-monster timer）→ MonsterDespawn(Removed)；
  **不 Respawn（杀一只少一只）**；玩家 HP<=0 → alive=false / PlayerDeath 广播 / 不自动复活 /
  MoveInput 服务器忽略；战斗状态（玩家/怪物 HP）不持久化，重启恢复满血
- **协议**：PlayerAttackRequest=250 / PlayerAttackResponse=251 / CombatEvent=252 /
  EntityHealthSnapshot=253 / MonsterDeath=254 / PlayerDeath=256（255 已被 ErrorResponse 占用）；
  MonsterSpawn/PlayerSpawn/EnterWorldResponse/MonsterSnapshotEntry 增加 currentHp/maxHp/alive；
  World 协议版本 1 → 2；所有 Decode 严格 IsValid && Remaining==0
- **客户端**：Space = Debug 攻击最近可见存活怪（服务器重新验证）；F12 增加 PlayerHP/Alive/
  前 4 只怪 HP；死亡实体 Debug Quad 变灰（不做死亡动画资源）；RemoteMonster/RemotePlayer
  实体携带 HP（CombatEvent/HealthSnapshot 更新）；`[Combat] Player #X hit Monster #Y for Z` 日志
- **明确不做**（后续阶段）：No Skills / No Magic / No Projectile / No Critical / No Dodge /
  No Block / No Equipment bonus / No Buff / No DOT / No Threat list / No PvP / No Drop /
  No Loot / No EXP / No Level up / No Respawn / No Boss / No Hit VFX / No Floating damage UI
- **验收**：LegendWorldTests 165 Check 全绿（DamageFormula/攻击链/距离/冷却/重放/刷包/
  击杀/死亡广播/清理/多客户端复制/断线/持久化边界等）；双 Client 同区域看到相同 HP 变化

**技能、施法与战斗表现同步（阶段15：Skill & Ability Replication Core V0.15）**：

- **技能效果 100% WorldServer 权威**：Client 只发"我要释放哪个技能，以及目标是谁"
  （SkillCastRequest 只含 requestId/skillId/targetType/targetEntityId，禁止传伤害/Mana/
  CD/CastTime/AOE 位置/命中结果）；伤害/命中/冷却完成/Mana 消耗/施法完成时间/AOE 命中
  目标/死亡全部由服务器决定；Client 不做本地伤害预测（进度条仅展示，完成必须等
  SkillCastCompleted/SkillImpactEvent）
- **三个固定测试技能**（SkillRegistry 硬编码，不上 JSON 不进数据库，Definition 不可由
  Client 上传）：1001 Quick Strike（Instant 单体，CD 1.5s，耗蓝 10，距离 120，伤害 30）/
  1002 Fire Bolt（CastTime 1s，CD 3s，耗蓝 20，距离 500，伤害 40）/
  1003 Whirlwind（Instant 自体 AOE，半径 160，最多 16 目标，伤害 25，只打 Monster）
- **SkillService / SkillRegistry**（`Server/WorldServer/Skill/`）：纯函数验证链
  （CasterDead → NotInWorld → AlreadyCasting → TargetDead → InvalidTarget(不可见/类型伪造)
  → DifferentMap → OutOfRange → Cooldown → NotEnoughMana）；技能伤害公式
  `damage = max(1, baseDamage + attackPower - defense)`（48/58/43），不随机不暴击；
  普通攻击继续走阶段14 公式（18/次）互不影响
- **Mana（阶段15 不持久化、无 Regen）**：默认 100/100；只有 WorldServer 扣 Mana（Client
  不能发送剩余 Mana）；**施法被服务器正式接受时扣**，之后因移动/死亡/目标消失取消一律
  不返还；currentMana 永不为负；每 1s ManaSnapshot 纠偏（本人）；EnterWorldResponse 携带
  currentMana/maxMana
- **Cooldown / CastingState**：每技能独立 nextReadyTime（steady_clock 权威，Client 时间
  不可信）；服务器正式接受施法时启动（不等命中）；同一时间只能施放一个（Casting 期间
  一切新技能 → AlreadyCasting；普通攻击 → Busy）；castId 为服务器单调 uint64（与 Client
  requestId 严格区分）
- **施法生命周期**：Instant 立即结算但同样发送 SkillCastStarted(castTimeMs=0)→Completed→
  Impact→CombatEvent（统一协议）；Cast-Time 由 WorldServer 统一 Skill Tick（50ms，与
  AI Tick 200ms 分离，不建 per-cast 线程）完成时重验目标（exists/alive/sameMap/visible/
  range），失败 → SkillCastCancelled(TargetInvalid) 不造成伤害；有效移动 → Cancelled(Moved)
  （direction 0,0 不取消）；死亡 → Cancelled(Dead)；断线 → Pending Cast 随 PlayerSession
  清除；重进世界 Mana=100 / CD 清空（不持久化边界）
- **AOE 目标解析**：以 Caster 服务器权威位置查 MonsterSpatialGrid（禁止全 Monster 遍历）；
  过滤 alive=false / 跨地图；distanceSquared 升序 + entityId 升序稳定排序；最多 16 目标；
  不伤玩家（无 PvP）
- **广播范围**：Started/Completed/Cancelled = Caster ∪ 能看到 Caster 的玩家；Impact =
  Caster ∪ 能看到 Caster 的玩家 ∪ 能看到至少一个受影响怪物的玩家（去重，不全世界广播）；
  CombatEvent 复用阶段14 范围并扩展 sourceType(BasicAttack=1/Skill=2) + sourceId(skillId)
- **技能击杀**：复用阶段14 KillMonster（state=Dead/target 清空/MonsterDeath/3 秒清理），
  不写第二套死亡代码；No Loot / No EXP；多个死亡各自独立 MonsterDeath
- **协议**：SkillCastRequest=260 / SkillCastResponse=261 / SkillCastStarted=262 /
  SkillCastCompleted=263 / SkillCastCancelled=264 / SkillImpactEvent=265 / ManaSnapshot=266；
  SkillImpact 最多 16 目标（Decode 超过拒绝）；所有 Decode 严格 IsValid && Remaining==0；
  World 协议版本 2 → 3（旧版本握手拒绝）
- **客户端**：Debug 键 1/2/3（QuickStrike/FireBolt/Whirlwind；1/2 自动选最近可见存活怪，
  服务器重新验证；3 无目标 Self）；Space 保留普攻；F12 增加 Mana current/max 与
  Casting/SkillId/CastProgress；施法中 Caster 头顶蓝色 Debug 标记（正式技能美术后续阶段）；
  RemotePlayerEntity 携带 casting/castingSkillId/castStartServerTime/castDurationMs
- **明确不做**（后续阶段）：No Buff/Debuff/DOT/HOT/Stun/Root/Knockback/Shield/Heal/Summon/
  Pet/Projectile 物理/技能树/天赋/职业成长/装备加成/技能升级/PvP Skill/Boss 技能/怪物技能/
  Combo/Channeling/蓄力/持续施法/打断抗性/元素系统/Mana Regen/掉落/经验
- **验收**：LegendWorldTests 新增 WorldSkillChecks（SkillDefinition/协议 Roundtrip/畸形包/
  AOE 排序与上限/Mana 下限/施法链/移动与死亡取消/目标重验/观察者复制/无全局广播/无重复
  广播/Mana 快照/刷包限流/断线/重进/持久化边界等）全部 0 failures

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

**Buff / Debuff / 状态效果系统（阶段16：Status Effect Core V0.16）**：

- **所有状态效果 100% WorldServer 权威**：Client 不能发送 ApplyStatusRequest（阶段16 状态
  只能由 WorldServer 技能逻辑或测试白盒施加）；是否有 Buff/持续时间/剩余时间/层数/属性加成/
  DOT 伤害/减速倍率/什么时候结束全部服务器决定；Client 只接收 StatusApplied(270)/
  StatusUpdated(271)/StatusRemoved(272)/StatusSnapshot(273) 做展示（remainingMs 仅 UI 倒计时，
  不影响战斗数值；真正移除必须等 StatusRemoved 或 Snapshot 纠偏）
- **五种固定状态**（StatusEffectRegistry 硬编码，`Shared/Status/`+`Server/WorldServer/Status/`）：
  2001 Battle Focus（Buff，Attack +10，10s，不叠层，重复刷新）/ 2002 Armor Break（Debuff，
  Defense -2/层，8s，最多 3 层，AddStackRefresh，满层刷新 duration）/ 2003 Burn（DOT 8 伤害
  /2s，8s 共 4 跳，不叠层，重复刷新 duration 且 nextTick 重置 now+2s）/ 2004 Poison（DOT 4
  伤害/层/s，6s 共 6 跳，最多 3 层，AddStackRefresh 不重置 nextTick）/ 2005 Slow（MoveSpeed
  ×0.6，5s，不叠层，重复刷新）
- **stack/refresh 规则**（StatusEffectService 纯函数）：RefreshDuration=stacks 不变+expire 刷新；
  AddStackRefresh=未满层 stacks+1+expire 刷新（nextTick 不重置）、满层仅刷新 duration；
  最新 source 覆盖旧 source（同 effectId 同一目标只保留一个 Active 实例，key=effectId）
- **DOT Tick 规则**：统一 100ms Status Tick 扫描（不建 per-status Timer）；`now >= nextTick 且
  nextTick <= expireTime` 才结算；服务器卡顿一次最多补 3 跳（kStatusDotMaxCatchUpTicks），
  超出把 nextTick 推进到未来；到期（Expired）立即移除，绝不继续掉血
- **Derived Stats**（Base/Derived 分离，状态变化时重算、不每帧）：effectiveAttack = base +
  Σ attackFlat×stacks；effectiveDefense = max(0, base + Σ defenseFlat×stacks) 不能负；
  effectiveMoveSpeed = base × Π moveSpeedMultiplier（乘法叠加）；普攻/技能伤害、Monster AI
  移动、玩家移动全部接入 Effective 值
- **技能关联状态**：QuickStrike→Armor Break+1、FireBolt→Burn、Whirlwind→Poison+1（目标未死
  才施加，击杀不施加）；新增技能 1004 Battle Focus（Self，CD 8s，耗蓝 15，无伤害）/
  1005 Crippling Strike（单体，CD 4s，距离 120，伤害 10，施加 Slow）；客户端技能键 1~5
- **DOT 击杀与死亡清理**：DOT 击杀复用 KillMonster（MonsterDeath killer=原始施加者 characterId，
  断线后仍保留）；死亡逐个广播 StatusRemoved(TargetDied) 并清空容器；玩家死亡清全部状态；
  状态 runtime-only 不持久化（重启清空、不写 SQLite）
- **AOI 同步**：Monster 状态发给当前可见玩家+source 本人；Player 状态发给本人+可见者；
  远处 Client 收不到任何状态事件/快照；每 2s StatusSnapshot（上限 32 条）只发自身+可见实体，
  用于纠偏（客户端多余删除、缺少创建）；同 instanceId Applied 去重，未知 Updated/Removed 忽略
- **客户端**：RemoteStatusEffectContainer（RemotePlayerEntity/RemoteMonsterEntity 各一份）+
  WorldClientController 本地玩家 LocalStatusEffects；Debug 键 4/5 施放 Battle Focus/
  Crippling Strike；F12 显示 Self/Monster 状态（含层数与剩余秒），Debug 颜色标记
  （Buff 金/紫/橙/绿/蓝）
- **同 Effect 多来源**：阶段16 同一 effectId 只保留一个实例，后施加者覆盖 source（README 明确
  记录；独立来源 Stack 留待后续阶段）

**服务器权威成长、奖励与怪物重生（阶段17：Progression/Reward/Respawn Core V0.17）**：

- **服务器权威成长**：EXP/Gold/Level 只由 WorldServer 决定（Client 不能发送"我要多少经验"）；
  DB 持久化（Migration 2：characters.gold 列，旧角色默认 0）；在线击杀走 PlayerSession 结算
  后经 DbWorker 落盘，离线 killer（DOT 击杀后断线）奖励照常入库不丢失
- **EXP 公式**：ExpToNextLevel(level) = 100 × level（线性）；Level cap = 100（满级后 EXP 不
  累计、不溢出、不再升级）；一次大额奖励自动跨多级（while 循环结算）
- **Training Slime 奖励**：每只 EXP +25 / Gold +3（MonsterDefinition 数据驱动）；击杀归属 =
  最后造成致死伤害的 Player（Basic/Skill/DOT sourceEntityId 统一）
- **Level 属性成长**：每级 MaxHp +10 / Attack +2 / Defense +1（lv1 = 100/20/5）；升级立即
  回满 HP，Derived Stats 经统一重算入口刷新（不覆盖状态加成）；LevelUpEvent(281) 发本人+
  附近可见玩家
- **协议**：RewardGranted(280)（只发本人）/ LevelUpEvent(281) / ProgressionSnapshot(282)
  （进世界下发 + 每 30s 纠偏；重进世界加载持久化 level/exp/gold）
- **Monster SpawnSlot**：map1 固定 20 个 SpawnSlot（slotId 1~20，位置表即
  `Shared/Monster/MonsterTypes.h` kInitialMonsterSpawnTable）；启动全满、slot 与 entity
  双向绑定
- **Respawn 8s**：怪物死亡 3s 尸体清理后进入 runtime Respawn Queue（respawnTime 从
  MonsterDeath 起算，尸体 3s 包含在 8s 内）；统一 Respawn Tick 250ms 轮询（不建 per-怪
  Timer）；到点生成全新 MonsterEntity（新 entityId、满 HP、无状态、Idle、target=0、原 slot
  出生点），正确加入 MonsterManager + MonsterSpatialGrid，AOI 下个 tick 通知附近玩家
- **防重复复活**：NotifyEntityRemoved 仅在 slot 无 active entity 且未入队时入队；Poll 时 slot
  仍被占用则放弃本次重生；重启 Respawn Queue 清空、20 slot 全部重新满怪（runtime-only 不
  持久化）
- **MoveMonsterTo 语义**：测试布景搬移怪物 = "重新安家"，同步更新 Respawn slot 出生点
  （respawn 发生在当前 home，与 SpatialGrid/AOI 一致）

**服务器权威掉落、背包与装备（阶段18：Loot/Inventory/Equipment Core V0.18）**：

- **100% 服务器权威**：掉落结果/物品实例/拾取/背包/装备/属性加成全部由 WorldServer 决定；
  Client 只发 requestId + dropEntityId/slotIndex（不能上报 definitionId/quantity/position/
  instanceId），背包/装备只是服务器 Snapshot/Delta 的镜像
- **3 个 ItemDefinition**（`Shared/Item/` + `Server/WorldServer/Item/ItemRegistry` 硬编码）：
  3001 Rusty Sword（Weapon，Attack +3）/ 3002 Cloth Armor（Armor，Defense +2）/
  3003 Slime Core（Material，堆叠 99）；协议 290~300（WorldItemSpawn/Despawn、
  ItemPickup、InventorySnapshot/Delta、Equip/Unequip、EquipmentSnapshot）
- **Drop Table**（Training Slime）：Slime Core 100% ×1 / Rusty Sword 20% / Cloth Armor 20%
  （Gold 走阶段17）；DropRoller 可注入 RNG（生产 mt19937_64，测试固定 seed 可复现，
  CI 不因概率失败）；掉落位置 = 死亡位置 + 轻微确定性偏移
- **Owner Lock 10s + TTL 60s**：击杀者（含离线/DOT source）独占 10s 后公共拾取；60s 无人
  拾取服务器删除（Expired）；统一 Drop cleanup Tick 500ms（不建 per-drop Timer）；
  World Drop runtime-only 不持久化（重启清空、NoWorldDropPersistence）
- **Pickup range 100**：拾取校验链 = alive → 防重放(64) → drop 存在 → visibleItemDrops
  （服务器 AOI 权威）→ 同图 → 距离 ≤100 → owner 允许 → 背包有空间；remove-before-grant
  （single-thread world authority + claim 原子）防两玩家同帧争抢同一 Drop（必有一个失败）；
  DB 失败自动回滚恢复 Drop（不能吞物品）
- **Inventory 40 格**（InventoryContainer）：服务器权威；Slime Core 同 definition 优先
  堆叠（≤99，溢出新格）；只并入已持久化堆叠（避免 DB 回填前竞争）；进世界下发完整
  InventorySnapshot，变更走 InventoryDelta（Set/Remove）
- **SQLite 持久化**（Migration 3：inventory_items + character_equipment，旧库自动升级）：
  instance_id = INTEGER PRIMARY KEY AUTOINCREMENT（持久唯一，重启不碰撞）；装备中物品
  slot_index = 1000+槽位；Equip/Unequip/Pickup 一律事务化（prepared statements + FK +
  BEGIN IMMEDIATE）；DbWorker 异步落库（io 线程禁止同步 SQLite）
- **Weapon/Armor 装备槽**（EquipmentService 纯逻辑 + WorldServer 编排）：Equip 原子替换
  （旧装备回新装备腾出的原槽）；Unequip 背包满失败且装备保持不变；装备 quantity 恒 1；
  Material 拒绝装备
- **Derived Stats 集成**：EffectiveAttack = Base/Level + Equipment + Status（统一
  RecalculateDerivedStats 入口，不覆盖状态加成）；lv1 + 剑 +3 + Battle Focus +10 = 33；
  普攻与技能（QuickStrike 等）全部使用 Effective 值；装备/卸下即时重算
- **防重放**：最近 64 个成功 Pickup/Equip/Unequip requestId（失败请求允许重试；重复请求
  不重复获得物品/不重复变更装备）
- **客户端镜像**（`Client/WorldNetwork/RemoteItemModels.h`）：RemoteWorldItemManager/
  ClientInventoryModel/ClientEquipmentModel——只展示服务器 Spawn 过的掉落（Despawn 即移除，
  不凭空创建）；Debug 键 E 拾取最近 100 内掉落 / I 输出背包 / 6/7 装备剑·甲 / 8/9 卸下
  Weapon·Armor；F12 显示 Inventory Used/40、Weapon/Armor、Effective Attack/Defense

**服务器权威任务（阶段19：Quest Core V0.19）**：

- **100% WorldServer 权威**（指令二）：Client 只能发 Accept/TurnIn/Abandon 三个请求
  （requestId + questId，协议 310~319）；任务是否完成/杀怪数量/物品数量/等级/区域到达/
  奖励内容/任务状态/任务进度全部由服务器决定，Client 只是事件镜像（不能本地杀怪 +1）
- **QuestRegistry**（`Server/WorldServer/Quest/`，硬编码 5 个固定测试任务，Definition 绝不入库）：
  - 4001 Slime Hunter：Kill Training Slime ×5 → EXP100/Gold20（无前置）
  - 4002 Core Collector：Collect Slime Core ×3 → EXP80/Gold10（前置 4001）
  - 4003 Growing Warrior：Reach Level 3 → Gold50（前置 4001）
  - 4004 Explorer：Reach Area map1 (1500,1500) r=100 → EXP50/Gold10（无前置）
  - 4005 Slime Cleanup：多目标 Kill ×3 + Collect ×2 → EXP150/Gold30/Rusty Sword ×1（前置 4002）
  - 启动校验：questId/objectiveId 唯一、前置存在、requiredCount>0、奖励 Item 存在 ItemRegistry
- **QuestService 纯逻辑**（规则）+ **QuestRepository**（prepared statements，DB Worker 线程）+
  **PlayerQuestContainer**（PlayerSession 内，不散落 WorldServer 多个 map）+ WorldServer 只编排
- **4 种 Objective**：KillMonster（只有最终 killer 计数；AOE 多杀每只分别 +1；DOT 归 source player；
  进度封顶 required）；CollectItem（"当前拥有数量"型——拾取增加/删除降低，进度 = min(拥有, required)，
  背包变化即重算）；ReachLevel（升级事件与接取时校验；progress 0/1）；ReachArea（只在服务器权威
  MoveInput 位置真正变化后检查，distanceSquared ≤ radius² 且同图；一次性，不重复写 DB）
- **QuestState 状态机**：NotAccepted → InProgress → ReadyToTurnIn →（玩家主动 TurnIn）→ Completed；
  Abandon 只允许 InProgress/ReadyToTurnIn（进度清零、记录保留 state=Abandoned、可重接——重接按
  持有量重新初始校验）；Completed 永久保留，不能再接/再领/Abandon
- **Accept 校验链**：存在 → alive → minLevel → 前置 Completed（ReadyToTurnIn/Abandoned 不算）→
  无同任务 InProgress/Ready → 未 Completed（不可重复）→ 进行中 ≤20（QuestLogFull）；
  接取时 ReachLevel/Collect 立即初始校验（背包已有 3 Core → 立即 3/3 Ready）
- **TurnIn 奖励**（复用阶段17 ProgressionService/InventoryService，不写第三套经验金币）：
  先预检背包空间（满 → InventoryFull，任务仍 Ready，不先发 EXP/Gold）→ 原子 DB 事务
  （Quest Completed + level/exp/gold 写回 + 物品入库一次提交）→ 成功后更新内存并广播
  QuestRewardGranted（exp/gold/item/newLevel/newExperience/newGold，只发本人）；奖励 EXP 可跨级升级
  （LevelUpEvent 照常），升级再推进其它 ReachLevel 任务（任务 A 奖励升级 → 任务 B 更新）
- **防重放**：最近 64 个成功 Quest requestId（Accept/TurnIn/Abandon 统一历史）；TurnIn 在 DB 提交前
  即缓存 requestId——100 连发只奖励一次；Completed 再 TurnIn → AlreadyCompleted
- **离线 DOT Kill 推进**（指令六十二）：killer 离线时 QuestRepository.OfflineAdvanceKill 在 DB 直接
  推进（不加载假 PlayerSession）；离线完成第 5 只 → DB 直接 ReadyToTurnIn，重登靠 QuestSnapshot 恢复
  （离线不发网络事件）
- **QuestSnapshot**：进世界下发全部 InProgress/ReadyToTurnIn/Completed（questId + state + 目标进度），
  每 10s 本人纠偏（测试可配 300ms）；快照上限 256 Quest/16 Objective，Encode 截断保护 + Decode 严格拒绝
- **SQLite**（Migration 4：character_quests + character_quest_objectives，旧库 v3 自动升级不丢数据；
  character_id FK → characters.id；DB 只存角色任务状态，name/description/目标定义绝不入库）
- **客户端镜像**（`Client/WorldNetwork/ClientQuestModel`）：状态/进度只来自
  QuestProgressUpdated/QuestStateChanged/QuestSnapshot（本地篡改被下一 Snapshot 纠正）；
  名称用 Shared 展示元数据（SharedQuestDisplayName，规则仍在服务器）；
  Debug：F8 任务面板 + Ctrl+1~5 接取 / Shift+1~5 提交 / Alt+1~5 放弃（4001~4005）

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
| F8 | Quest Debug（任务状态/目标进度面板；Ctrl+1~5 接取 4001~4005 / Shift+1~5 提交 / Alt+1~5 放弃） |
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
