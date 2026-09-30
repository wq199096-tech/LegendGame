# CURRENT_STAGE — 阶段24 完成（Stage24 completed）

## 当前阶段
- **阶段24 —— Client Visual Runtime & Asset Pipeline V0.24：已完成（completed）**
- 起始 HEAD：**1b43b61c494346411e0943fabfd01db520a6878a**（`docs: finalize stage 23 context`）
- 最终功能提交：**c9cac3fe5a6e68d399c3d1b1e941bf0b368b64c2**
  `feat(client): add visual runtime and asset pipeline v1`（100 文件，+11591/-160）
- 后续 CI/测试基建修复：ed89d99 / c4a1f0b / 65dd21d / d36308a / 9121b52 / d9ab4ca / 9a45ddc
- 本地验证：8 exe 全绿；NetworkTests 0 fail；AccountTests 0 fail；WorldTests
  **全部 PASS（含阶段24 新增 4 组检查与 Client 15s 冒烟）**

## 阶段24 交付清单（指令逐项）
- **Asset Manifest（指令三/四）**：Data/Assets/{asset_manifest,animations,visual_entities,
  effects}.json（4 文件 schemaVersion=1）+ Client/Visuals/VisualAssetData 解析与校验
  （assetId 唯一/相对路径强制（拒绝 `C:\`、`/`、`..`）/类型枚举/pivot 域宽高校验）+
  Client/Assets/AssetManager（缓存/缺失紫棋盘 Fallback + 去重日志/F10 热重载失败保留旧资源）
- **SpriteRenderer（指令七）**：复用引擎 SpriteBatch 批渲染（UV/pivot/flip/tint），F9 统计
- **坐标统一（指令八/九）**：Camera2D::WorldToScreen/ScreenToWorld 复用；在线相机平滑跟随
  服务器权威本地玩家（首帧 snap 防拖影），按 MapSnapshot 边界 clamp
- **地图视觉（指令十/十一/十二）**：Data/World 第 6 文件 visual_maps.json
  （visualMapId/backgroundAsset/tileSize/固定四层）+ maps.json 增 visualMapId +
  Client 地图渲染 Ground→Decoration→Y排序实体(含 Object 层 2.5D)→Foreground→Effects→
  名字板/飘字→HUD；无视觉定义的地图渲染 Fallback Grid（绝不黑屏）
- **实体视觉（指令十三~二十三）**：统一 AnimationPlayer（禁止三套播放器）；玩家 8 方向
  （Direction8 行序）Idle/Walk/Attack/Cast/Hit/Death；本地与远程玩家共用 PlayerVisual
  （服务器权威位置 + 1-exp(-12dt) 插值 + 方向差分）；Monster visualId（monsters.json 新字段）
  → training_slime 正式 sprite（5 状态 8 方向）；NPC visualId（既有数值协议经
  visual_entities serverVisualId 别名映射）Idle 动画 + 名字 + 任务 Marker（!/灰点/?）；
  Portal 环形帧动画 + 目标地名；Nameplate 统一渲染（玩家/NPC/怪物 Lv.X）；头顶 HP 条
  （远程玩家+怪物，NPC 无）；MonsterDeath→Death 动画持帧，Despawn 移除
- **技能 VFX/飘字（指令二十四~二十八）**：Client/Visuals effects.json 4 特效；
  1001 刀光 / 1002 Projectile+Impact（纯视觉，伤害仍服务器权威）/ 1003 旋风；
  CombatEvent → 伤害飘字（普通金/DOT 紫/自身受伤红，上浮渐隐）
- **HUD（指令二十九~三十三）**：左上 Portrait+Name+Lv+Gold+HP/MP 条；底部 SkillBar 1001/1002/1003
  （图标+按键+CD 遮罩+Mana 不足遮罩；服务器仍权威）；右上地图名（MapChanged 同步）；
  右侧 Quest Tracker（标题来自 quests.json 展示字段，进度全部来自服务器事件）
- **字体（指令三十一）**：stb_truetype 动态字形图集 + UTF-8 解码（缺字形方框占位）；
  字体链 Assets/Fonts→msyh.ttc→simhei→arial（中文可渲染，正式字体接入保留接口）
- **热重载/统计（指令三十四/四十二）**：F10 Reload Visual Assets（manifest/动画/特效/纹理，
  失败保留旧资源；Dev AutoLogin→Ctrl+F10 让位）；F9 面板 FPS(标题栏)+Sprites/DrawCalls/
  Textures/FX
- **Editor（指令三十六/四十/四十一）**：visualId ComboBox（NPC 数字别名/Monster/Portal）+
  地图 visualMapId ComboBox + Visual Map 编辑器（背景资产/tileSize/四层 placements 增删改）+
  Inspector 第一帧 Visual Preview + 菜单 Validate Assets（结构+交叉引用+文件存在性）
- **资源与逻辑分离（指令三十九）**：换角色/地图美术只需替换 Assets/ + Data/Assets/ + Data/World
  visual_maps，不改服务器/Combat/Skill/Quest 代码
- **测试（指令四十五/四十六/四十七）**：AssetManifestChecks/AnimationChecks/
  VisualDefinitionChecks/ClientSmokeChecks 并入 LegendWorldTests（三套件纪律不变）；
  Client Smoke = LEGEND_CLIENT_VISUAL_SMOKE=1 真实启动 15s 干净退出 +
  [VisualSmoke] 里程碑断言（gl-context/shader/manifest/texture/scene-started/pass）
- **开发占位资产（指令三十八）**：Tools/GenerateDevAssets.ps1（GDI+）生成 52 张
  有辨识度 PNG（蓝战士/紫法师/绿道士 8 方向人形、绿史莱姆、4 职能 NPC、Portal 漩涡、
  4 特效、3 地块、5 地物、头像/技能图标）

## 本地最终验证
- build 8 exe 全绿（exit 0 / 0 error）；8/8 exe 齐全
- NetworkTests PASS；AccountTests PASS；WorldTests 全 PASS（0 failures）
- Runtime smoke：Login(7100)/Gateway(7300)/World(7200) 真实 Data/World（6 文件含
  visual_maps.json）启动校验通过；Data hash e35cc124ea138c24（与阶段23 一致——视觉字段
  不改变语义，向后兼容）
- Client 离线渲染截图正常（旧 Debug 路径不受影响）；在线画面（真实资源/相机跟随/技能
  特效/HUD）**需要用户人工视觉验收**（Client 无自动登录链路；自动化仅覆盖 15s 启动存活）

## 关键实现决策（防止后续重写）
- **在线/离线双路径**：GameScene::Render 仅当 worldReady && VisualRuntime::IsReady 走新
  渲染；离线 LEGEND_AUTO_* 自检链全部保留原路径（回归零风险）
- **visualId 双轨**：NPC 协议为数值 visualId（阶段20 既有）→ visual_entities.json Npc 实体
  用 serverVisualId 别名；Monster/Portal 为字符串 visualId（新字段，缺省 fallback）
- **Client 展示数据纪律**：VisualDataCatalog 仅加载展示字段（技能名/CD 显示/任务标题/
  visualId 映射）；一切服务器权威结果（伤害/CD 判定/掉落/任务推进）仍全部来自服务器事件
- **visual_maps.json 为 WorldData 第 6 文件**：WorldServer 启动加载+校验（错误拒绝启动）；
  视觉字段不改变 Data hash 语义（e35cc124 不变）
- stb_truetype.h vendored 至 ThirdParty/stb（实现仅 Font.cpp 一处；stb_image 实现仍在
  ResourceManager.cpp，AssetManager/Editor 仅引用声明）
- PowerToys 教训追加：`New-Object X(a*b, c*d)` 参数模式解析陷阱 → 一律 `[X]::new(...)`；
  `.ps1` 含中文必须 UTF-8 **带 BOM**（否则 PS5.1 按 GBK 误读炸解析）；Windows 宏
  `DrawText` 会重命名成员 → 文本 API 命名 DrawString/DrawStringShadow

## 阻塞项
- 无

## 下一步
- **阶段24 完成，停止。等待用户阶段25 指令（不自动进入）。**
- 视觉验收：TRAE 已通过 `LEGEND_CLIENT_AUTO_ENTER=1` 自动登录进世界并截图验证静态画面
  （地图视觉/props/NPC 名字板/HUD/SkillBar/地图名）。动态表现（行走动画、战斗、技能
  特效、伤害飘字、相机跟随、MapChanged 清场）建议用户手动游玩确认。

## Actions 状态
- **Stage24：run 36673795651（9a45ddc）= success** ——Configure / Build / Mesa 软件 GL /
  Verify 8 exe / CTest 硬门禁（含 Client Visual Smoke）/ Runtime gate 全部 SUCCESS
- CI 修复链（3 次迭代，均为环境/测试基建，未弱化门禁）：
  1. runner 无 GL 3.3 → 安装 mesa-dist-win 26.2.3 软件 OpenGL（opengl32.dll + lib*.dll）
  2. 软件渲染低 FPS 钳制游戏时间 → 冒烟退出改用 SDL_GetTicks 墙钟
  3. Mesa 26.x 默认 D3D12 后端首帧崩溃 0x80070057 → 冒烟子进程强制
     `GALLIUM_DRIVER=llvmpipe`；同时禁用 vsync + 轮询式等待（心跳 [Diag]）
