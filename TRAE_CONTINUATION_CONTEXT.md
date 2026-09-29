# TRAE_CONTINUATION_CONTEXT — 自动续接专用

> 新会话/续接第一步：读本文件 + PROJECT_CONTEXT.md + CURRENT_STAGE.md + ARCHITECTURE.md，
> 然后执行 git status / git log -5 --oneline，确认一致后继续。

## 基本信息
- GitHub repo: https://github.com/wq199096-tech/LegendGame（branch: main）
- HEAD SHA: a674786d3c1b66ec59ec761b328347b5cf698147（阶段19 收尾提交；阶段20 代码尚未正式提交）
- 当前阶段: **阶段20 —— NPC / Dialogue / Shop / Teleport / Quest Interaction Core V0.20，编码全部完成，本地验证中**
- 阶段目标: 见 CURRENT_STAGE.md（Todo 除"提交/Actions"外全部勾选）
- 阶段禁止项: 复杂剧情分支/Lua 脚本/动态 NPC/NPC 战斗 AI/NPC 死亡/护送/摆摊/拍卖行/玩家交易/
  仓库/强化/锻造/耐久/公会/转职/声望/每日商店/动态物价/共享 NPC 状态/语音/Cutscene/电影镜头
- 最终 commit msg: `feat(world): add npc dialogue shop teleport and quest interaction core`
- 完成后停止，不进入阶段21

## 已完成任务
- Shared 四模块（Npc/Dialogue/Shop/Teleport，MessageId 320~334）+ QuestDefinition start/turnInNpc + SkillCancelReason::Teleported
- Server 六模块（NpcRegistry/NpcEntity/NpcManager/NpcSpatialGrid/NpcInteractionService/ShopService/TeleportService）+ WorldServer 编排（AOI/Interact/Dialogue/Shop/T teleports/Marker）
- Client（RemoteNpcManager/ClientNpcModels/事件/Send/GameScene E·数字键·B·S/F8）
- Tests/WorldNpcChecks.cpp + CMake + WorldChecks 调用
- README V0.20 章节 + CURRENT_STAGE/TRAE_CONTINUATION_CONTEXT 更新

## 关键实现决策（防止后续重写）
- Quest 复用：WorldServer::AcceptQuestForPlayer(player,questId) / BeginQuestTurnIn(player,questId,requestId,sendResponsePacket)
  （requestId!=0 才缓存/回响应包）——Dialogue Option 与阶段19 packet handler 共用
- Marker 刷新点：Accept/TurnIn(DB 回调)/Abandon + HandleQuestObjectiveChanges 的 stateChanges 非空时
  （Kill/Collect/Reach 推进 → ReadyToTurnIn 也必须刷新——首轮测试发现的服务器缺口，已修）
- 会话 TTL：npcSessionTtlSeconds 默认 30s；对话 option 成功 TouchDialogueSession（滑动续期），
  ShopSession openedAt 不刷新（打开后 30s 固定）
- Buy/Sell：requestId 在 DB 提交前缓存（防连发穿透）；BEGIN IMMEDIATE 事务
- Teleport：验证全过才 RememberNpcRequest；AOI 重置 + CloseNpcSessions + 立即位置快照 + HandleQuestPlayerMoved

## 测试基建要点（WorldTestHarness.h）
- **WorldTestClient::lastEvent 已改 std::vector 堆分配**（WorldNetworkEvent 因阶段20 payload 膨胀至 ~KB，
  64×按值 ≈100KB/客户端；Monster 套件 12 个栈上客户端在函数序言 __chkstk 就打穿 1MB 默认主线程栈
  → 0xC00000FD，先于任何 printf 崩溃。勿改回数组！）
- WorldTestServers 新增 npcSessionTtlSeconds（默认 30）→ world->GetConfig() 透传
- WorldServer 白盒新增 TestRevivePlayer(characterId)（PlayerSession::Revive 复活满血，防死亡级联）

## 当前失败测试 / 最近错误 / 已尝试修复（首轮 run5：19 FAIL → 已修待验证）
- InteractAndWaitDialogue 固定伪 requestId → 后续 Interact 全 DuplicateRequest → 改 static 递增
- 杀怪循环 A 在 (360,300) 被巡逻怪游荡围殴致死 → 后续全 Dead 级联 →
  改安全流程：A 移 (180,60) 安全点、逐只移怪 (240,60) 贴身击杀、杀完 TestRevive + 移回
- 传送落点 (1500,1500) 在怪簇 5 中心；WrongNpc/Duplicate 检查需要 A 与 Elder/Wayfarer 交互 →
  WrongNpc 前移回 (360,300)、Duplicate 前移 (560,300)（Wayfarer range 40）
- Marker 检查 baseline 事后取样（含 -1/-8 倒推）→ 事件已到而错过 → 全部改动作前取样
- TTL：全套件不再用 0.6s；TTL 专用检查前重启世界（StopWorld→0.6→StartWorld→re-enter）
- NpcError.h 缺 include NpcTypes.h（Client 侧 81 错）；`far`/`home` 是 Windows 宏改 farTp/homeTp；
  NpcChecks 多余 `} // namespace`；bool ok 重定义；shared_ptr 不能 `const auto*`

## 下一步
1. run6 结果：0 FAIL → 更新 CURRENT_STAGE 测试结果；有 FAIL → 继续修（注意上表已修项勿回退）
2. 回归 Network/Account 两套件（ctest 三套）
3. WIP 提交（含 4 个上下文文件）→ 正式 commit `feat(world): add npc dialogue shop teleport and quest interaction core`
4. push（SOCKS5：`git -c http.proxy=socks5h://127.0.0.1:10808 -c https.proxy=socks5h://127.0.0.1:10808 push origin main`）
5. 等 Actions 绿（无 token 时用 `::error::` 注解通道查失败：curl.exe --socks5-hostname 127.0.0.1:10808）
6. 最终汇报 45 项（含自动续接情况）；完成后停止，不进入阶段21

## git status（写入时点）
- 分支 main，与 origin/main 同步于 a674786；阶段20 全部改动未提交
- 未跟踪新文件：PROJECT_CONTEXT.md / CURRENT_STAGE.md / ARCHITECTURE.md / TRAE_CONTINUATION_CONTEXT.md

## Actions 状态
- 最新 run 36527767372（a674786）= success（阶段20 提交后更新）
