# TRAE_CONTINUATION_CONTEXT — 自动续接专用

> 新会话/续接第一步：读本文件 + PROJECT_CONTEXT.md + CURRENT_STAGE.md + ARCHITECTURE.md，
> 然后执行 git status / git log -5 --oneline，确认一致后继续。

## 当前续接状态（2026-10-01）
- **Stage26 Production Login & Character Lobby V0.26（running）**；开发分支
  `stage26-production-client-entry`（基线 6f02d42）。目标：LegendClient.exe 升级为
  正式玩家入口（启动画面→连接检查→登录/注册→角色大厅→创建/删除/选择角色→
  进入世界加载→游戏）。默认启动绝不自动注册/登录/建角/进世界（AutoEnter 仅
  CI/Smoke/开发，env 保留）。客户端 ClientFlowState 状态机集中管理；全部新 UI
  简体中文统一 UiTheme/LegendClientUi；角色名唯一 DB UNIQUE + Migration；
  每账号 4 角色上限服务器权威；删除二次确认；PlayerFacingErrorCatalog 中文映射；
  11 exe 不变，测试并入 3 套 CTest；完成后 PR + main CI 全绿再封板。
  详见 CURRENT_STAGE.md 与阶段指令（42 条）。
- **Stage25.6 Server Management GUI 已封板（completed）**：六服务器全简体中文
  GUI 管理台（SDL3+Dear ImGui 五页签+状态栏，默认无 CMD，--console 供 CI/开发，
  WM_CLOSE 优雅停机）；LegendServerAdminUi + Tools/UiCore 共享 UI 基建；
  Studio 服务器中心全部实测可用。PR #1 已合并 main（bd8eabc）。
- Stage25.5 基线：六服务拓扑、InternalProtocol v1、World 持久化 DbServer RPC
  收口、11 exe CI；StatusSnapshotCheck 慢机竞态已修复。
- 最终基线验证：main CI 七步骤全绿（Run #71 = 36865757905）。
- 现有 `LegendMapEditor.exe` = "传奇游戏开发工具 - LegendGame Studio"：
  中文主题/DPI/菜单/内容树/地图 Canvas/Asset Browser/Animation Preview/
  Quest Flow/BOSS/Chapter/Validation/Process Status/服务器中心。
- 下一步：执行 Stage26 设计与实现（见 CURRENT_STAGE.md running 段）。

## 基本信息
- GitHub repo: https://github.com/wq199096-tech/LegendGame（branch: main）
- 本地: 仓库根 = `d:\LegendGame-main\LegendGame`（构建目录 `build/`；构建用 VS BuildTools
  自带 CMake 3.31；git 代理 repo-local http://127.0.0.1:10809，代理间歇可用）
- 阶段24 起始 HEAD: **1b43b61c494346411e0943fabfd01db520a6878a**
  （`docs: finalize stage 23 context`）

## Stage24 状态：completed
- **Stage24 completed**（Client Visual Runtime & Asset Pipeline V0.24）
- 交付摘要（细节见 CURRENT_STAGE.md / ARCHITECTURE.md）：
  - Data/Assets 4 JSON（asset_manifest/animations/visual_entities/effects）+
    Data/World 第 6 文件 visual_maps.json + maps/monsters/portals 视觉字段
  - Client/Visuals（VisualAssetData/AnimationPlayer/VisualDataCatalog/Font/VisualRuntime）
    + Client/Assets/AssetManager；在线模式真实资源渲染（离线路径不变）
  - 实体视觉：玩家 8 方向 6 动作/史莱姆/NPC/Portal/名字板/HP 条/伤害飘字/技能 VFX
  - HUD：Portrait/HP/MP/SkillBar(CD+Mana)/地图名/Quest Tracker；F9 统计/F10 热重载
  - Editor：visualId/visualMapId ComboBox + Visual Map 编辑 + 第一帧 Preview +
    Validate Assets
  - 52 张开发占位 PNG（Tools/GenerateDevAssets.ps1）
  - 测试：AssetManifest/Animation/VisualDefinition/ClientSmoke Checks 并入
    LegendWorldTests（三套件纪律不变）；Client 15s 冒烟（LEGEND_CLIENT_VISUAL_SMOKE=1）

## 本阶段调试教训（防止重演）
- Windows 宏 `DrawText` 重命名成员 → 文本 API 用 DrawString/DrawStringShadow
- PowerShell 5.1 无 BOM 中文 .ps1 按 GBK 误读；`New-Object X(表达式, 表达式)` 参数模式
  解析陷阱 → `[X]::new(...)`
- MSVC /MP 并行 PDB 竞争 C1041 → 串行或清残留 cl.exe
- 命名空间别名（using legend::world::MapVisualDefinition）只在声明所在命名空间可见，
  跨命名空间成员函数体内仍需限定
- stb 双头文件纪律：stb_image 实现仅在 ResourceManager.cpp；stb_truetype 实现仅在
  Font.cpp；其它 TU 只 include 声明

## 测试状态（本地最终）
- 8 exe 全绿；NetworkTests 0 fail；AccountTests 0 fail；WorldTests 0 fail
- Runtime smoke：三服务器真实 Data/World（含 visual_maps.json）启动校验通过；
  Data hash e35cc124ea138c24（与阶段23 一致 = 视觉字段向后兼容）
- Client Smoke：LEGEND_CLIENT_VISUAL_SMOKE=1 → 15s 存活干净退出，全部里程碑标记通过

## 下一步
- 等待用户阶段25 指令。**不自动进入阶段25，不修改任何游戏业务代码。**
- **建议用户人工视觉验收**（在线实际操作）：`LEGEND_CLIENT_AUTO_ENTER=1` 即可自动
  登录进世界（TRAE 已用此链路截图验证静态画面）；移动/战斗/技能特效/伤害飘字/
  相机跟随等动态表现建议手动游玩确认。

## Actions 状态
- Stage21 run #47（36609036863 / e362dfa）= success
- Stage22 run #48（36611118905 / b611b53）= success
- Stage23 run #49（36630085904 / d7a8e63）= success
- **Stage24 run 36673795651（9a45ddc）= success**（含 Mesa llvmpipe Client Visual Smoke）
