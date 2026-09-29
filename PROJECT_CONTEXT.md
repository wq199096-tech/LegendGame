# PROJECT_CONTEXT — LegendGame 长期稳定上下文

> 本文件是长期稳定信息（不随阶段内进度变化）。新会话/续接时**必读**。

## 仓库
- GitHub: https://github.com/wq199096-tech/LegendGame（远端 main 分支，唯一分支）
- 本地仓库根（中文路径）: `d:\传奇1\传奇1\传奇1\传奇1\LegendGame`
- 构建目录: `D:\LegendGame`（**独立完整副本，非联接**——源码改动后必须 robocopy 镜像同步过去再构建；
  同步后源文件 mtime 早于旧 obj 会被 msbuild 跳过 → touch 全部源文件；Build 缓存若被旧 CMake 配置过需删 CMakeCache/CMakeFiles 重配）

## 技术栈
- C++20 / CMake ≥3.24 / Visual Studio 2022 BuildTools（MSVC）/ Debug 构建
- SDL3（窗口/输入）+ OpenGL 3.3 Core（渲染）+ Standalone Asio（网络）+ SQLite3（amalgamation 静态库）+ libsodium（Argon2id/随机数）
- 依赖全部 FetchContent 自动下载

## 进程架构
- **LegendClient**：SDL3 + OpenGL 客户端。Gateway 直连账号链路（7100/7400 经 GatewayServer 7300），
  选择角色拿 SelectionTicket 后**直连 WorldServer**（127.0.0.1:7200）
- **LegendGateway**：账号消息信封转发（不解析业务 payload）
- **LegendLoginServer**：账号/角色/Session/SelectionTicket（7100）；账号 DB（WAL）
- **LegendWorldServer**：世界权威（7200）；内部连接 LoginServer 消费 Ticket；独立 DbWorker 单线程 DB（io 线程禁止同步 SQLite）
- 同一 SQLite 数据库（data/legend_account.db），Login/World 共享

## 数据库
- schema_version 单行表（id=1 主键 + UPSERT），`kCurrentSchemaVersion` 在 DatabaseSchema.h
- Migration 1: accounts/characters/sessions；2: characters.gold；3: inventory_items/character_equipment；
  4: character_quests/character_quest_objectives（阶段19）
- 只存玩家数据（账号/角色/成长/背包/装备/任务状态）；一切 Definition（怪物/技能/状态/物品/任务/NPC/商店/传送）代码硬编码，绝不入库

## 测试体系
- 三套 CTest（禁止新增第四个）：LegendNetworkTests / LegendAccountTests / LegendWorldTests（后缀追加源文件）
- WorldTests 端口 17240(World)/17241(Login)/17242(Gateway)；测试 DB: testdata/world_test_<pid>/*.db
- 共享测试基建：Tests/WorldTestHarness.h（worldtest namespace：WorldTestServers/WorldTestClient/Check/WaitUntil/TempDbPath）
- GitHub Actions: .github/workflows/windows-build.yml（Configure → Build → Verify 8 exe → CTest 硬门禁，禁止 continue-on-error；
  失败时上传 ctest.log artifact + `::error::` 注解输出 FAIL 行——注解 API 公开可读）
- CI 日志/logs/artifacts API 无 token 均返回 "Must have admin rights"，只能靠注解通道拿失败信息
- git 推送：直连被重置时走 SOCKS5：`git -c http.proxy=socks5h://127.0.0.1:10808 -c https.proxy=socks5h://127.0.0.1:10808 push origin main`；
  API 轮询：`curl.exe --socks5-hostname 127.0.0.1:10808`

## 阶段历史（全部封板）
- 阶段1~8：引擎/地图/角色/战斗框架/技能本地框架（V0.1~V0.8，单机本地）
- 阶段9 Network Core V0.9：四进程网络、握手/心跳、Gateway 转发（基线 4142fe1）
- 阶段10 Account & Character Core V0.10：账号/角色/Session/Ticket（70b2492）
- 阶段10.1：DB 基础设施修复（schema_version 单行化、DbWorker Flush 等，e4c1301）
- 阶段11 World & Character Handoff Core V0.11：WorldServer/Ticket 消费/权威位置/持久化（99d79f3）
- 阶段12 AOI & Multiplayer Replication V0.12：Uniform Grid AOI/多人同步（0dcc309）
- 阶段13 Monster & AI V0.13：服务器权威怪/AI 状态机（1aa9e4c）
- 阶段14 Combat & Damage V0.14：攻击验证链/HP/死亡（49269c1）
- 阶段15 Skill & Ability Replication V0.15：5 技能/Cast/Mana/CD
- 阶段16 Status Effect V0.16：Burn/Poison/Slow/ArmorBreak/BattleFocus/DOT
- 阶段17 Progression/Reward/Respawn V0.17：EXP/Gold/LevelUp/Respawn 8s/离线奖励入库
- 阶段18 Loot/Inventory/Equipment V0.18：掉落/拾取/40 格背包/装备/快照（1042124）
- 阶段19 Quest Core V0.19：服务器权威任务/4 种 Objective/Accept-TurnIn-Abandon/离线 DOT 推进/
  Snapshot 10s 纠偏（ea97880 + 稳定化 a674786，Actions run 36527767372 = success）

## 关键架构原则
1. **100% 服务器权威**：Client 只表达意图（requestId + 最小参数），所有数值/状态/结果由服务器重新验证；
   Client 只是服务器状态的事件镜像（Snapshot/Delta 纠偏，绝不本地推算）
2. **协议安全**：所有 Decode 必须 reader.IsValid() && Remaining()==0；数量/数组上限严格拒绝；
   Malformed 回错误码不断开（Crash 防护）
3. **防重放**：每类请求最近 64 个成功 requestId（失败允许重试）；重复请求不重复结算
4. **DB 纪律**：io 线程禁止同步 SQLite；一切写经 DbWorker Post（FIFO 单线程）；关键链路事务化
   （BEGIN IMMEDIATE/COMMIT/ROLLBACK）；Definition 永不入库
5. **AOI**：Enter 600/Leave 700 滞回，只广播给能看到的人（无全图广播）
6. **职责分离**：Registry（定义）/Service（纯逻辑规则）/Repository（DB）/Manager·Container（状态）/WorldServer（编排）
7. **MessageId 分段**：1~199 基础+账号；200~219 World 基础；230~233 玩家同步；240~242 怪物；250~256 战斗；
   260~266 技能；270~273 状态；280~282 成长；290~300 物品背包装备；310~319 任务；**320+ NPC（阶段20）**
8. **测试事件断言**：必须记录事件基线（recorded 队列基线索引）后再等待/统计，防止旧事件污染

## 编码/构建纪律（本项目实测教训）
- 中文路径 MSVC 报 MSB8084 → 构建/测试一律在 `D:\LegendGame`
- `near`/`far` 是 Windows 宏，变量不能叫这些名
- PowerShell 对原生 exe 用 `Select-Object -First N` 会掐管道杀进程
- Edit 工具偶发"报成功但未落盘"→改后 shell 复读验证
- PowerShell 大文件手术用 `[System.IO.File]::ReadAllLines/WriteAllLines` + 行号锚点
