# TRAE_CONTINUATION_CONTEXT — 自动续接专用

> 新会话/续接第一步：读本文件 + PROJECT_CONTEXT.md + CURRENT_STAGE.md + ARCHITECTURE.md，
> 然后执行 git status / git log -5 --oneline，确认一致后继续。

## 基本信息
- GitHub repo: https://github.com/wq199096-tech/LegendGame（branch: main）
- 本地: `d:\LegendGame-main`（ZIP 快照重建的 git 仓库；push 走 SOCKS5 10808；git 身份已配置）
- 当前阶段: **阶段21 —— Multi-Map World / Portal / Respawn Core V0.21 编码/测试完成，提交待 Actions**
- 阶段禁止项: 副本/Dungeon（仅预留枚举）/尸体系统/新怪/新正式 Quest/PvP
- 最终 commit msg: `feat(world): add multi-map portal and player respawn core`
- 完成后停止，不进入阶段22（下一阶段待新指令）

## 阶段21 完成内容（119 条指令全清单对应）
- Shared：WorldMap 四件套 + RespawnProtocol + Portal 三件套 + MessageId 340~348
- Server：Map/Portal 六新文件 + MapTransitionService（统一切换，NPC 传送/白盒全部改走）+
  多地图 WorldMapManager（按图边界）+ 按图怪物（legacyMap1TestSpawn 兼容开关）+
  Portal AOI/验证链 + 死亡复活链（3s/10G/Town 免费/状态清空/保护）+ 持久化修正
- Client：RemotePortalManager/ClientWorldMapModel + F（Portal）/R·T（复活）/F9（Map 面板）+
  死亡 Debug Overlay + MapChanged 清空全部镜像（防 Ghost）
- Tests：WorldMapChecks.cpp（40+ 检查）+ 旧套件数量断言 20→50/19→49 适配

## 本阶段调试教训（防止重演）
- 测试移动后立即交互/用 Portal → AOI tick NotVisible 竞态 → 发送前等服务端 visibleXxx 收录
- respawn 3s 时钟：每个 respawn 检查前重新 MarkDead（DeathBlocks 等待耗时不可控）
- 技能 ID 是 1001~1005（非 1~5）；7001 NPC 传送需 20G（测试角色先 TestSetPlayerGold）
- 切图必须调 OnTargetPlayerRemoved（原图追击怪丢目标）——MapTransitionService 已内置
- `Select-Object -First N` 实时截断管道杀 ctest（PROJECT_CONTEXT 旧教训再次踩中）→ 落盘再 grep
- GitHub 每步仅保留前 10 条 error 注解；[FAIL] 行内嵌诊断 + [Diag] 通道（阶段20 建立的方法论全程有效）

## 测试状态
- 本地三套件 100% passed（Network 12.3s / Account 16.8s / World 390.2s，含 WorldMapChecks）
- 8 exe 全部构建 ✓；CI 提交后轮询中

## 下一步
1. push 后等 Actions 绿（注解通道排查：curl --socks5-hostname 127.0.0.1:10808）
2. 绿后最终汇报 51 项（MapRegistry…最终 Commit SHA/Known Issues）；完成后停止，不进入阶段22

## Actions 状态
- 阶段21 提交后待更新（阶段20 基线：run #42 = success）
