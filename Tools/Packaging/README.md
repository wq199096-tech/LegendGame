# Windows 打包（Tools/Packaging）

本目录提供 LegendGame Windows x64 可分发包的构建脚本。
CI 入口：`.github/workflows/windows-package.yml`（main push 与 v* tag push 触发）。

## 文件

- `PackageWindows.ps1` —— 打包主脚本（PowerShell 7+，Windows）。
  参数全部相对仓库根，无硬编码绝对路径；幂等；任何缺失立即非 0 退出。

## 本地用法（Windows 开发机）

```powershell
# 先做 Release 构建
cmake -S . -B build
cmake --build build --config Release --parallel

# 打包
pwsh -File Tools/Packaging/PackageWindows.ps1 `
  -BuildBinDir Build/bin/Release `
  -DistDir dist/LegendGame-Windows-x64 `
  -Version dev `
  -CommitSha (git rev-parse HEAD)
```

产物：`dist/LegendGame-Windows-x64/` —— 8 个正式 EXE、运行时 DLL
（SDL3/libsodium/VC++ redist CRT，经 vswhere 定位）、`Data/`、`Config/`、
`Assets/` 全量目录、`build-info.txt`、`Start-LegendGame.bat`、
`Start-Client.bat`、`Stop-Local-Servers.bat`。

## 设计要点

- DLL 白名单制：只拷贝 `SDL3.dll`、`libsodium.dll` 与 VC++ CRT，
  CI 专用的 Mesa 软件 GL（`opengl32.dll`）绝不进入用户包。
- 用户包不依赖源码目录 / build 目录 / VS / Git / CMake。
- 启动器 `.bat` 全部 `cd /d %~dp0`，路径相对化；服务器默认 GUI 模式。
- `Stop-Local-Servers.bat` 只关闭本目录下进程的窗口（优雅退出），
  不做 `taskkill /F`。
- 中文 JSON/资源：7z 按字节原样打包，不经过 PowerShell 文本管线，
  不会损坏 UTF-8 内容。
