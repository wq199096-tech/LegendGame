# LegendGame — 自研 PC MMORPG 项目

自研 Windows PC 2D/2.5D MMORPG。当前阶段：**Map System V0.2 + Map Editor Prototype**。

- 语言：C++20
- 构建：CMake + FetchContent（自动下载 SDL3 / nlohmann-json / Dear ImGui）
- 渲染：OpenGL 3.3 Core（自带精简 GL 函数加载器，不依赖 GLEW/GLAD）
- 地图：数据驱动（JSON），Tile/Object/Collision/Occlusion 四层，Chunk 可视剔除 + SpriteBatch 批渲染 + Y-Sort
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

## Engine V0.2 操作说明

| 按键 | 功能 |
| --- | --- |
| W / A / S / D | 移动玩家（分轴碰撞，可沿墙滑动；不能穿水/墙/建筑/石头） |
| 方向键 | 自由移动摄像机（Camera Follow 关闭时） |
| 鼠标滚轮 | 摄像机缩放（0.25x ~ 4.0x，任意模式下可用） |
| F | 开启 / 关闭摄像机跟随玩家（默认开启） |
| F1 | 切换碰撞 Debug 可视化（阻挡 Tile 红色半透明覆盖） |
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

## 日志

运行日志输出到控制台并写入 `Logs/latest.log`（目录不存在时自动创建），
包含启动、初始化、资源加载、错误与关闭全流程记录。

## 本阶段明确不包含

账号 / 登录 / 网关 / 数据库 / 正式地图 / 怪物AI / 装备 / 背包 / 技能 / 战斗 /
聊天 / 商城 / 任务 / 公会 / 交易 / 组队 / 排行榜等 MMORPG 玩法功能，
将在引擎稳定后的后续阶段逐步实现。
