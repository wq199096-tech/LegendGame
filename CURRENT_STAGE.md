# CURRENT_STAGE — 阶段22 完成（待 Actions 确认）

## 当前阶段
- **阶段22 —— LegendGame World Editor V1 + Data Driven World V0.22**（无人值守模式）
- 前置确认：阶段21 已封板（e362dfa 修复；run 36609036863 = success；3285bf4 主体提交）
- 下一阶段：阶段23（Game Data / Content Editor V1）——阶段22 全绿后自动进入
- 全局收尾：阶段23 全绿后输出最终汇报并停止，不进入阶段24

## 阶段目标（摘要）
游戏世界从 C++ 硬编码升级为「PC 可视化世界编辑器 + JSON 数据驱动」：
地图/NPC/怪物刷怪点/Portal/出生点/复活点不再需要改 C++。

## Todo（阶段22）
- [x] 阶段21 CI 收尾（DotKill drop 可见性轮询化 → run 36609036863 success）
- [x] Shared：WorldDataJson（Load/Validate/Save/Roundtrip/MakeDefaultWorldData）+ MonsterSpawnDefinition + GenerateSpawnPoints
- [x] Registry 数据驱动：Map/Npc/Portal + MonsterSpawnRegistry（LoadFromDefinitions/LoadDefaults；Instance() 调用面零改动）
- [x] WorldServer：worldDataDir + Start 加载/校验/拒绝启动 + SpawnInitialMonsters 数据驱动（slotId 全局递增）
- [x] World Editor V1：双工作区 + 三栏布局 + Canvas（Pan/Zoom/拾取/拖拽）+ Inspector + Validation + Undo 100 步 + 原子保存 + backup 轮换 + Launch WorldServer + smoke
- [x] Data/World 5 个 JSON 入库（.gitignore 修正 `/data/* + !/Data/**`——Windows 大小写陷阱）
- [x] Tests：WorldDataChecks + MapEditorDataChecks 并入 LegendWorldTests（WorldChecks main 调用）
- [x] 本地全量：WorldTests 526 PASS / 0 FAIL（含新检查）；Editor smoke PASSED；8 exe 构建中
- [ ] Actions 绿（feat(editor): add data-driven world editor v1）

## 关键实现决策（防止后续重写）
- 单一事实来源：MakeDefaultWorldData()（Shared/WorldData/WorldDataJson.cpp）——出厂默认数据
  = Data/World 初始 JSON = 测试 fixture；Registry 硬编码全部移除
- 数据注入模式：`static LoadFromDefinitions(...)` + `static LoadDefaults()` 经
  `Mutable()`（const_cast Instance 同一实例）——Instance() 返回 const& 的 17 处调用零改动
- 加载策略（23.23 兼容）：worldDataDir 存在 → Load+Validate（失败拒绝启动，不静默回退）；
  不存在 → 出厂默认 + LOG_WARN（开发/测试环境；CI ctest 工作目录无 Data/World）
- GenerateSpawnPoints：spawnId 派生种子的 xorshift64*，圆内 sqrt 均匀分布——
  Editor/Server/Tests 三方一致；zone 参数必须保证「入口 AOI 600 内有怪 + 避开 Portal 100」
  （Map2 zone 900,950 r400：入口最近点 489；Map3 zone 950,820 r350：入口最近点 589——
  Exec 复现算法验证；首版 zone 1050,1150/1200,950 导致入口无怪 → MapIsolation/PortalSuccess
  3 FAIL 教训）
- WorldDocument（编辑器文档模型）：Mutate() 是唯一修改通道（自动压 undo 栈 + Revalidate）；
  拖拽 release 才 Mutate（防 undo 洪泛）；Validation fail-fast（Error 禁存三处一致）
- EditorApp `editor::` 必须写 `legend::editor::`（全局命名空间下 editor:: 不解析——
  曾经 100+ 编译错误的根因）；NOMINMAX 必须在 windows.h 前

## 本阶段调试教训（重要）
- **Windows git 大小写陷阱**：`.gitignore` 的 `data/` 会误吞 `Data/`（core.ignorecase=true）
  → 必须用 `/data/* + !/Data/ + !/Data/**` 组合（`!/Data/` 单独不够——check-ignore -v 验证）
- **zone 布点必须用算法复现验证**（不能只看圆心距）：首版 Map2 zone 圆心距入口 832，
  最近展开点 612 > AOI 600 → 切图后入口无怪 → 3 个阶段21 链路检查 FAIL
- backup 轮换是 per-file（每文件 10 份）——断言按 `maps.*.json` 统计而非全目录
- 并行 msbuild 竞争同一 PDB → cl 进程死锁（8 个僵尸）→ Stop-Process 后重编
- 后台 build job 用 `> log 2>&1` 落盘 + Select-String 查错（老教训持续有效）

## 本地全量测试（阶段22 当前）
- LegendWorldTests：526 PASS / 0 FAIL（含 WorldDataChecks 45+ / MapEditorDataChecks 14 新检查）
- Editor world smoke：PASSED（exit 0）
- Network/Account：全量构建后重跑中

## 阻塞项
- 无

## 当前 Actions
- 阶段21：run 36609036863（e362dfa）= **success**（封板）
- 阶段22：待提交 feat(editor): add data-driven world editor v1
