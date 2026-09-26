# LegendGame — 自研 PC MMORPG 项目

自研 Windows PC 2D/2.5D MMORPG。当前阶段：**Engine V0.1**（自研客户端引擎）。

- 语言：C++20
- 构建：CMake + FetchContent（自动下载 SDL3，无需手动安装依赖）
- 渲染：OpenGL 3.3 Core（自带精简 GL 函数加载器，不依赖 GLEW/GLAD）
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
```

建议在工程根目录下运行（这样能读到 `Assets/`，日志写在 `Logs/latest.log`）。
资源缺失时引擎会自动使用程序生成的占位纹理，不会崩溃。

## Engine V0.1 操作说明

| 按键 | 功能 |
| --- | --- |
| W / A / S / D | 移动玩家（支持斜向，速度已归一化：200 units/s） |
| 方向键 | 自由移动摄像机（Camera Follow 关闭时） |
| 鼠标滚轮 | 摄像机缩放（0.25x ~ 4.0x） |
| F | 开启 / 关闭摄像机跟随玩家（默认开启） |
| ESC | 退出程序 |

窗口：1280x720，标题实时显示 FPS（每 0.5 秒刷新）。
测试世界 3000x3000，程序启动后自动进入 TestScene。

## 日志

运行日志输出到控制台并写入 `Logs/latest.log`（目录不存在时自动创建），
包含启动、初始化、资源加载、错误与关闭全流程记录。

## 本阶段明确不包含

账号 / 登录 / 网关 / 数据库 / 正式地图 / 怪物AI / 装备 / 背包 / 技能 / 战斗 /
聊天 / 商城 / 任务 / 公会 / 交易 / 组队 / 排行榜等 MMORPG 玩法功能，
将在引擎稳定后的后续阶段逐步实现。
