# CURRENT_STAGE — 阶段25 完成（Stage25 completed）

## 当前阶段
- **阶段25 —— First Playable Vertical Slice V0.25：已完成（completed）**
- 起始 HEAD：**dcbe5b0e3e4b623e79a28452fac0d76a24edbe6d**（`docs(stage24): record ci run success and final shas`）
- Checkpoint：**6afa4b0**（`wip(stage25): checkpoint spawn-fix and chapter1 data`）
- 最终功能提交：**（见 TRAE_RUN_STATE.json finalSha）**
  `feat(game): add first playable vertical slice`
- 本地验证：8 exe 全绿；NetworkTests 0 fail；AccountTests 0 fail；WorldTests
  **0 fail（含阶段25 新增 VerticalSlice/UiModel/ChapterOne E2E 检查）**

## 阶段25 交付清单（指令逐项）
- **新角色出生修复（指令四）**：CreateCharacter 写未出生哨兵 (-1,-1) → WorldServer
  EnterWorld 新角色分支（服务器权威）落 Map1 300,300 并立即持久化；Client 永不修正；
  测试 NewCharacterSpawnCheck（出生/持久化/重登保持）+ WorldFullChainCheck 312 断言更新
- **Chapter 1《异动的史莱姆》（指令三/八/九）**：任务链重组 4001 First Trouble →
  4002 Strange Cores（Cloth Armor 奖励）→ 4003 Growing Stronger → 4004 Explore the
  Meadow（Map2 南部区域）→ 4005 Deeper Threat（杀5+核2+进入 Map3 复合）→ 4006
  Ruins Investigation（Boss 击杀，Bronze Sword 奖励）；Elder 承接 4006；生产 JSON
  与出厂默认数据（MakeDefaultGameData/WorldData + 全 Registry LoadDefaults）完全同步
- **第一只 Boss（指令十/十一/十二）**：monsterDefinitionId=2001 Ancient Slime
  Guardian（L4/HP600/攻22/慢速/大体型 visual scale 1.6）；Map3 (1900,1500) 单只生成
  （入口 AOI 之外）；respawn 45s（per-spawn 数据驱动，复用 Monster Spawn/Respawn 体系）；
  loot_table_2001（core 100% 2~3 / Cloth Armor 40% / Bronze Sword 30%）
- **新装备（指令十三/十四/十五）**：3010 Bronze Sword / 3011 Apprentice Staff /
  3012 Spirit Talisman / 3013 Traveler Armor（保留 3001~3003）；商店上架；
  装备攻防立即生效展示（Progression 事件镜像 Base 值 + Equipment 镜像加成）；
  7 张物品占位图标（GenerateDevAssets.ps1 扩展 + asset_manifest 注册）
- **正式 UI（指令十六~二十二/二十七/三十七/五十五~五十七/六十~六十三）**：
  新静态库 LegendClientUi（UiTheme 1920×1080 参考分辨率缩放 + UiModels +
  ItemDisplayCatalog）；VisualRuntime 全套：HUD V2（头像/名/Lv/HP/MP/EXP 条/Gold/
  ATK/DEF）、SkillBar（CD 倒计时/Mana 遮罩/失败红闪）、Quest Tracker（≤3，进度全部
  服务器事件）、Character Panel（C）、Inventory 40 格（I，图标/数量/装备边框/Tooltip/
  双击装备）、Dialogue 窗口（NPC 名/文本/选项点击）、Shop 窗口（图标/价格/Buy/Sell/
  Gold）、MiniMap（玩家/NPC/任务 NPC/Portal，AOI 权威）、Toast（≤5 自动淡出 4 级）、
  Map Enter Banner、Boss Bar、LEVEL UP! 特效、新手提示（仅一次本地保存）、
  Settings（Esc：Resolution/Fullscreen/三音量，savedata/client_settings.json）；
  F9 默认关闭（正式画面无坐标/FPS/entityId）；UiRequest 队列分发（服务器依旧权威）
- **反馈（指令二十三~三十/三十一~三十六）**：伤害飘字/受击闪白/LevelUp 光柱文案/
  Gold+EXP Toast/Gold SFX/技能 SFX+Impact/NotEnoughMana+Cooldown Toast/目标反馈/
  Portal/横幅/MiniMap/BossBar
- **地图与保护（指令三十九~四十三）**：三图 Decoration 布景重排（村庄道路/花木/
  废墟廊道/Boss 石圈/Map2 分区地标）；Map1 SafeZone metadata（maps.json safeZone，
  无 PvP 逻辑）；Map2/Map3 进图 2s EntryProtection（复用 RespawnProtection 时间戳，
  MapTransitionService 统一施加，主动攻击/施法取消）
- **音频（指令四十八~五十一）**：Client/Audio/AudioRuntime（SDL3 F32 立体声混音器，
  程序化合成——3 图 BGM 循环乐句 + 10 SFX：SwordAttack/FireBolt/Whirlwind/
  MonsterHit/ItemPickup/QuestComplete/LevelUp/UiClick/Gold/Error；零版权，替换正式
  资源不动接口）；音量 Master/Music/SFX 实时生效 + 本地持久化
- **加载/错误 UX（指令五十三~五十五）**：World 连接管线 Loading 覆盖层；服务器错误
  码 → Toast（技能/物品/任务/商店专用文案 + TooFar/InventoryFull/LevelTooLow 通用）
- **冒烟（指令六十九/七十）**：保留 15s Visual Smoke；新增 [VsSmoke] 30s AutoEnter
  模式（LEGEND_CLIENT_VS_SMOKE=1，entered-world/pass 标记）；CI 新增硬步骤
  「Vertical Slice runtime smoke (4-process chain)」：Login/Gateway/World/Client
  全链 world-ready + 干净退出
- **持久化（指令四十四/四十五/六十八）**：ChapterOneChecks 断线重登恢复
  Level/Gold/位置/装备/任务状态
- **测试（指令六十四~六十八）**：仍 3 套 CTest；新增 VerticalSliceChecks.cpp
  （NewCharacterSpawnCheck）/ UiModelChecks.cpp（Toast/Banner/BossBar/SkillSlot/
  HUD/Tracker/Inventory/Shop/Minimap/ItemCatalog/Theme 9 组）/ ChapterOneChecks.cpp
  （真实生产数据 E2E 全链：出生→接 4001→Portal→杀 5→掉落拾取→升级→交任务→商店
  买+装 Bronze Sword→4005→Boss 击杀→4006→Chapter Complete→持久化）；
  出厂默认内容变更同步 20+ 既有断言（未删测试/未弱化规则）

## 本地最终验证
- build 8 exe 全绿（exit 0 / 0 error）；8/8 exe 齐全（无第 9 个）
- NetworkTests PASS；AccountTests PASS；WorldTests 全 PASS（0 failures）
- Runtime smoke：三服务器真实 Data/World+Data/Game 启动校验通过
- Client 15s Visual Smoke + 30s VS Smoke（4 进程链）通过

## 关键实现决策（防止后续重写）
- **出生哨兵**：(-1,-1) 表示"未出生"；WorldServer 检测 positionX/Y < 0 → 地图出生点
  + 立即持久化；(0,0) 不做特殊处理（历史库兼容，测试 SeedAccountAndCharacter 显式钉位）
- **出厂默认 = 生产 JSON**：Chapter1 内容同步进 MakeDefaultGameData/MakeDefaultWorldData
  ——机制测试（缺目录 fallback）与内容测试（真实 Data 目录）双轨，避免内容改动破坏
  机制回归
- **UI 纪律**：Client UI 只发意图（UiRequest 队列，GameScene 每帧 Drain 后转发），
  一切数值/状态仍来自服务器事件镜像；展示字段（items.json/quests.json）走
  ItemDisplayCatalog/VisualDataCatalog，Client 不链接 Server Registry
- **EntryProtection 复用**：MapTransitionService 对非 Town 落点 SetRespawnProtection(2s)，
  不新增第二套保护系统
- **音频线程模型**：SDL_AudioStream 回调 + mutex 保护 voice 列表；程序化合成缓冲
  一次性生成，Mix 只做累加+软限幅
- **VsSmoke 放 CI 独立步骤**（需真实三服务器；不放 CTest——WorldTests 用测试端口，
  客户端连生产端口）

## 阻塞项
- 无

## 下一步
- **阶段25 完成，停止。等待用户阶段26 指令（不自动进入）。**
- 人工视觉验收清单（指令七十一）：登录 → 进入村庄（HUD/NPC/对话/任务标记）→
  I/C/Esc 窗口 → 商店买卖/装备（攻防变化）→ Portal 至 Map2（横幅/BGM/保护）→
  杀怪（技能/CD/掉落/拾取/EXP/Gold/升级特效）→ 4002~4005 → Map3 → Boss（BossBar/
  掉落/4006）→ Chapter Complete → 断线重登持久化。

## Actions 状态
- **Stage25：run（见 TRAE_RUN_STATE.json actionsRunId / finalSha）= success**
  ——Configure / Build / Mesa llvmpipe / Verify 8 exe / CTest 硬门禁 /
  Vertical Slice runtime smoke (4-process chain) 全部 SUCCESS
