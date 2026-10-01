# CURRENT_STAGE — Stage25.6 Server Management GUI V0.256

## Stage25.6（2026-10-01）

- 状态：**implementation WIP preserved（本地 GUI 工作已存在，正在整合；未经人工视觉
  验收，未经 CI 验证，不得宣称封板 / not yet final accepted）**。
  按用户要求：完成前不报告"服务器PC可视化完成"。
- 整合方式：WIP 已备份于 `backup/stage25.6-wip`（f200669c），cherry-pick 至
  `stage25.6-integration`（基于 Stage25.5 c140e00）。在 Stage25.5 CI 修复全绿之前
  **暂停 Stage25.6 开发**（用户指令：CI 全绿才能继续 Stage25.6）。
- 交付内容（用户 15 条硬性要求逐项落实）：
  - **统一 Admin UI 框架**：新静态库 `LegendServerAdminUi`（Server/AdminUi/）——
    ServerAdminApp（SDL3+Dear ImGui+EditorTheme，概览/连接/性能/日志/配置五页签+状态栏，
    全简体中文）、ServerMainRunner（统一入口/参数预扫描/GUI+worker 双线程/优雅停机）、
    LogCapture（Logger sink→4096 行环形缓冲→实时日志面板：级别过滤/搜索/暂停滚动/清空）。
  - **UI 基建共享**：legend_imgui + EditorTheme/EditorStrings 抽取至 `Tools/UiCore`，
    Studio 与六服务器共链（一套主题/中文字体链/DPI，零重复实现）。
  - **六个服务器 exe**：`WIN32_EXECUTABLE + /ENTRY:mainCRTStartup`——双击打开
    「LegendGame <服务中文名> 管理台」GUI 窗口（无 CMD）；`--console` 开发/CI 模式
    （保留 asio signal_set，AttachConsole/AllocConsole 兜底）；`--hidden` 隐藏窗口
    （Studio 服务器中心启动用）。关窗=WM_CLOSE→Graceful Shutdown，绝不 TerminateProcess。
  - **每服专属指标**（CollectStats 快照，GUI 线程 500ms 拉取，原子计数）：
    Login(网关连接/认证成功失败/Session 创建/Ticket 三计数/Db 状态)、
    Character(角色增删列表选择计数/Ticket/Db 状态)、Gateway(状态机分层
    Connected→InWorld 计数/收发包/Login+Character+World 状态)、
    World(在线玩家/Map1-3 人数/怪物/NPC/掉落/Tick avg+max ms/Persistence Degraded/Db/Log)、
    Db(SQLite 状态/DB 路径/DbWorker/查询/写入/事务/失败/Queue 长度/运行时 Migration 版本)、
    Log(接收/写入/Queue/丢弃/当前文件/文件大小/fallback 状态)。
  - **埋点**：Logger::SetSink；TcpConnection/TcpServer 收发包计数（关闭并入，单调）；
    PersistenceClient 请求统计+连续失败 Degraded 判定；DbWorker QueueLength/IsRunning；
    TicketStore 计数；World 100ms tick 计时；DbServer 运行时 ReadSchemaVersion（公开 API）。
  - **Studio 服务器中心**：运行菜单新增「服务器中心」——集中显示 Db/Log/Login/Character/
    World/Gateway（端口/PID/状态/操作列），启动全部/停止全部/单独启停/打开对应管理窗口
    （FindWindowW 固定标题）/启动客户端，全部按钮真实接线；重复启动防护、优雅停机状态
    （优雅停止中…/未响应）。「启动完整游戏」改为 --hidden 静默拉起六服务 +
    CREATE_NO_WINDOW 隐藏客户端控制台（不再弹 CMD）；StopLocalGame 删除 TerminateProcess，
    全部改 EnumWindows/FindWindowW + WM_CLOSE 优雅停机。
  - **CI**：两处 smoke 六进程 ArgumentList 追加 `--console`（GUI 子系统无人值守兼容）。
- 本地验证：
  - Debug 构建 **11/11 exe** 全绿（含六服务器 GUI 子系统入口）。
  - 3 套 CTest 串行干净运行 **全 PASS 0 failures**（Network/Account/World；
    注：并行多实例会因测试端口 7100-7600 争用产生假失败，须串行）。
  - Gateway GUI 试点：双击等价启动→窗口出现/无 CMD→7300 就绪→WM_CLOSE→exit 0+
    latest.log「Shutting down」；DbServer --hidden 试点：MainWindowHandle=0/7500 就绪/
    隐藏窗口 WM_CLOSE→exit 0+「graceful shutdown」；6 服务 --console 拓扑冒烟通过。
- 待办（不阻塞代码封板）：人工 GUI 视觉验收清单（五页签数据/日志过滤搜索暂停清空/
  服务器中心启停全链）；git 提交与 GitHub Actions CI（本机无 git）。

---

## 历史：Stage25.5 Server Architecture Completion V0.255

## Stage25.5（2026-10-01）— feature-complete, CI stabilization still required

- 状态：**feature-complete, CI stabilization still required**。最新 CI
  **Run #67 = 36846123700 FAILURE：失败点为「Run runtime tests (CTest hard gate)」**，
  Runtime gate / Server topology smoke / Vertical Slice full 7-process chain
  均因 CTest 失败被跳过。前一状态 Run #66 = 36827770559 FAILURE：仅
  Vertical Slice runtime smoke (full 7-process chain) 失败，其余步骤全绿。
  CI 全绿之前不得标记 completed；不进入 Stage26。
- 稳定化修复（仅动失败链路，不重构已通过的 Db/Character/Log/Gateway/
  Persistence RPC/Topology smoke）：
  - Client AutoEnter 墙钟节流：原实现每帧 -1/60 为帧率依赖，CI llvmpipe 软渲染
    2~5 FPS 下 30s 冒烟窗口内走不完 登录→选角→进世界 链（entered-world 标记
    永不出现）。改为 steady_clock 墙钟，任意帧率行为一致。
  - Client AutoEnter 瞬时失败重试：登录/拉列表/建角遇可重试错误
    （DatabaseError/InternalError/RequestPending/RequestTimeout/ServiceUnavailable，
    即依赖服务尚未 Healthy）退避 2s 重试（建角有界 5 次），替换原一次性标志位死锁。
  - Client 进世界失败重试：EnterWorld 失败（ticket 一次性作废）→ 重新选角换新
    ticket 重试，替换原 Failed 态死等。
  - StatusSnapshotCheck 慢机加固（Run #67 CTest hard gate 失败点，诊断
    `[Diag] Snapshot: got=1 effects=0`）：怪物 6 在 Burn 施加前生成的旧空快照
    仍留在客户端队列且更新，快照等待反向扫描先命中它 → effects=0 假失败。
    修复：检查前白盒重施 Burn（保证效果存活）+ 内容断言（2003/1 层）并入
    等待谓词（跳过旧空快照；服务器快照漏带效果时 6s 内无匹配照样失败，
    断言强度不变）。修复后本地 CTest 3/3 PASS。
  - CI Vertical Slice 步骤失败诊断：任何失败路径 dump 客户端+六服务日志尾部 30 行
    （不改变断言强度/不加 sleep/不 continue-on-error）。
- 正式进程 8 个：LegendClient / LegendMapEditor(Studio) / LegendLoginServer /
  LegendCharacterServer / LegendGateway / LegendWorldServer / LegendDbServer /
  LegendLogServer；开发测试程序 3 个；总构建 exe **11 个**。
- CODEX 接管（handoff）：Config/servers.json、Shared/InternalProtocol v1、
  PersistenceClient/LogClient/ServerConfig、Db/Character/Log 三服务与 11 exe CI、
  Gateway 状态机、Login/Character 迁移、Studio 七进程启动均已由 CODEX 完成（未提交）。
  TRAE 接管后先以 `wip(stage25.5): preserve codex server architecture work`
  Checkpoint（2d48702）保护成果，再补完全部剩余工作。
- **TRAE 补完：World 持久化 RPC 迁移**——
  - DbOperation 19~32（ItemInsert/EquipItem/UnequipItem/QuestInsert/QuestTurnIn/
    QuestAbandon/QuestObjective/QuestState/OfflineKill/ShopBuy/ShopSell/SaveGold/
    AddRewards/LoadCharacterFull）+ PersistenceMessages 严格编解码；
  - DbServer Execute 补齐全部世界持久化操作（原子事务在 Db DB Worker 内执行）；
  - 新 LegendWorldData 静态库：InventoryRepository（InsertOrMergeItem/
    ShopBuy/ShopSell 事务）+ QuestRepository，WorldCore 与 DbCore 共用（无两套 SQL）；
  - WorldServer 双模式（dbPort=0 legacy 隔离测试 / dbPort!=0 DbServer RPC）：
    进世界三段加载链（LoadCharacterFull→LoadInventory→LoadQuestState，FIFO 保序）、
    位置/成长/金币/离线奖励、拾取/装备/卸下、任务接取/推进/提交/弃置/离线击杀、
    商店买卖全部双模式收口；Stop 时 PersistenceClient::Drain（≤5s）等待在途存档；
  - DbServer 不可用 → Persistence Degraded：写失败回滚内存 + ERROR 日志 + 周期重试，
    进世界加载失败明确拒绝（不静默丢存档）。
- 修复：OfflineKillWrite 语义（false=无可推进任务，非错误）；MSVC 参数求值顺序
  陷阱（lambda move init-capture 先于参数拷贝 → 空 RPC 载荷，改为拷贝捕获）。
- 测试：仍 3 套 CTest；DbServerChecks 扩展真实 RPC 世界操作全序列
  （位置/成长/金币/物品插入+堆叠/装备/卸下/任务四流程/离线推进/商店买卖/turn-in/
  abandon/畸形请求拒绝）；PersistenceRpcChecks 扩展 World 编解码 roundtrip + 截断拒绝。
- 本地最终验证：完整 Debug 构建 11/11 exe；NetworkTests / AccountTests / WorldTests
  **全 PASS 0 failures**；六服务拓扑 Smoke 通过；七进程 Vertical Slice Smoke
  （entered-world 经 World→DbServer RPC）通过。

---

## 历史：Stage25 Chinese Studio UI Patch（completed）

## Stage25 Chinese Studio UI Patch（2026-09-30）
- **状态：completed**；未进入 Stage26，未修改服务器业务、协议、数据库或客户端战斗逻辑。
- `LegendMapEditor.exe` 统一品牌为“传奇游戏开发工具 - LegendGame Studio”，脏状态标题追加 `*`。
- 新增 `EditorTheme` 统一深色高级灰主题（#17191D / #1E2126 / #24282F）、金色选择、
  蓝色状态及成功/警告/错误语义色；新增 `EditorStrings` 集中中文菜单、面板、分类、字段与
  生产内容显示名，不修改 Definition JSON。
- 中文字体运行时回退链：`msyh.ttc -> simhei.ttf -> simsun.ttc`，加载 ChineseFull glyph；
  SDL 显示器 DPI 比例驱动字体、间距和面板尺寸（100%~200%）。
- UI：全中文九类菜单、实用快捷工具栏、可拖动三栏与底部区域、工作区 Tab、左侧统一搜索、
  右侧中文属性编辑、底部“内容检查/控制台/运行状态”、最底状态栏、中文删除确认。
- 地图 Canvas 直接复用 `visual_maps.json` + 纹理缓存绘制真实地面、道路、树木、花草、岩石等；
  保留 NPC/刷新点/BOSS/传送门/任务区域/SafeZone 编辑叠加，普通刷新点蓝色、BOSS 橙金、
  SafeZone 绿色、Portal 紫蓝、NPC 选中金色；调试叠加默认关闭。
- 资源浏览器增加搜索、分类与小/中/大缩略图；动画预览增加中文播放/暂停/上一帧/下一帧、
  八方向与帧/FPS；BOSS、Quest Flow、Chapter、Validation、Process Status 全部中文接线。
- 中文搜索别名仅作用于 Studio 显示层，例如“史莱姆”可匹配 `Training Slime` 与
  `Ancient Slime Guardian`；新增 WorldTests 回归断言。
- 本地验证：LegendMapEditor 编译与 `LEGEND_EDITOR_SMOKE=world` 通过；日志确认中文字体、
  Data/World、Data/Game、Data/Assets 加载成功；完整 Debug build 通过；8/8 exe；
  NetworkTests / AccountTests / WorldTests / CTest 全绿。
- 截图：当前 Codex 会话未暴露 Windows 原生窗口绑定，无法可靠保存 GUI 截图；已用真实 EXE
  smoke 与运行日志验收。人工视觉仍建议在 125% DPI、1280×720、1920×1080 各复查一次。
- Git / Actions：功能提交与 CI 结果见本文件末尾最新封板记录。

## Stage25 Chinese Studio UI Patch 封板
- 功能提交：**b7be2dc24731d5e91605389776c9785d3ed7b656**
  `feat(editor): redesign studio with simplified chinese ui`
- GitHub Actions：**run 36733983651 = success**
- CI 明细：Configure CMake / Build all targets / Verify 8 executables /
  CTest hard gate / Runtime gate / Vertical Slice runtime smoke 全部 SUCCESS。

## 当前阶段
- **阶段25 —— First Playable Vertical Slice V0.25：已完成（completed）**
- 起始 HEAD：**dcbe5b0e3e4b623e79a28452fac0d76a24edbe6d**（`docs(stage24): record ci run success and final shas`）
- Checkpoint：**6afa4b0**（`wip(stage25): checkpoint spawn-fix and chapter1 data`）
- 最终功能提交：**e4653305f5e8c85cceec97ae546605d10a37245e**
  `feat(game): add first playable vertical slice`（60 files, +5911/-464）
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
  HUD/Tracker/Inventory/Shop/Minimap/ItemCatalog/ChapterCatalog/Theme 12 组）/
  ChapterOneChecks.cpp（真实生产数据 E2E 全链：出生→接 4001→Portal→杀 5→掉落拾取→
  升级→交任务→商店买+装 Bronze Sword→4005→Boss 击杀→4006→Chapter Complete→
  持久化）；出厂默认内容变更同步 20+ 既有断言（未删测试/未弱化规则）
- **World Editor 升级（PC 端可视化要求）**：LegendMapEditor.exe 新增 Asset Browser
  （缩略图+详情）、Animation Preview（clip 帧播放/Play-Pause/方向行）、Quest Flow
  （章节→任务链视图）、Boss Editor（怪物+掉落表+刷新绑定+画布定位）、Chapter Editor
  （chapters.json 数据层：Shared 加载/校验/保存 + 编辑面板 + 客户端 Chapter Complete
  数据驱动）、Quest Area Map Picker（ReachArea 画布点选 + 圈层绘制）、Validate All、
  Launch Full Game（四进程链）、Stop Local Game、Process Status
- **关键修复（本轮实测发现）**：(1) MapTransitionService 发包顺序——MapChanged/
  MapSnapshot 必须先于 AOI 初始广播（客户端收到 MapChanged 清空镜像，Spawn 先到
  会被整批丢弃 → 切图后 NPC/Portal/怪物镜像永久为空，对话/商店/MiniMap 失效）；
  (2) 全仓双重 BOM（5 文件）导致 MSVC 编译失败；(3) ChapterOne E2E 慢机时序加固
  （击杀循环死亡即复活 / 拾取按成功计数 / MoveTo 多轮自校正 / UsePortal 服务器
  权威地图双确认 / Boss 巡逻半径感知断言）

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
- **Stage25：run 36714901307（e465330）= success**
  ——Configure / Build / Mesa llvmpipe / Verify 8 exe / CTest 硬门禁 /
  Vertical Slice runtime smoke (4-process chain) 全部 SUCCESS
