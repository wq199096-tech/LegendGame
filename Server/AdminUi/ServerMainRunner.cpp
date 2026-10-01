#include "Server/AdminUi/ServerMainRunner.h"

#include "Server/AdminUi/ServerAdminApp.h"

#include "Engine/Debug/Logger.h"

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

namespace legend::admin {

namespace {

// 双击 exe 启动时 CWD=exe 目录（Build/bin），而 Config/servers.json 相对仓库根。
// 从 exe 目录向上最多三级寻找包含 Config/servers.json 的目录并切换（已处于
// 仓库根时立即返回，不影响 CI / 开发脚本）。
void FixupWorkingDirectory() {
    std::error_code ec;
    if (std::filesystem::exists("Config/servers.json", ec)) {
        return; // CWD 已正确（命令行/Studio/CI 启动）
    }
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
        return;
    }
    const std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    std::filesystem::path candidate = exeDir;
    for (int i = 0; i < 3; ++i) {
        candidate = candidate.parent_path();
        if (candidate.empty()) {
            return;
        }
        if (std::filesystem::exists(candidate / "Config" / "servers.json", ec)) {
            SetCurrentDirectoryW(candidate.c_str());
            return;
        }
    }
}

// --console：GUI 子系统 exe 无父控制台时附加/分配一个（开发模式看得见日志；
// CI 不读 stdout，不受影响）。
void SetupConsole() {
    if (GetConsoleWindow() != nullptr) {
        return; // 从 CMD/PowerShell 直接启动：已有控制台
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (!AllocConsole()) {
            return; // Session 0 等场景分配失败：静默继续（日志仍落文件）
        }
    }
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    freopen("CONIN$", "r", stdin);
}

void PrintUsage(ServerRole role) {
    std::printf(
        "Usage: Legend%s.exe [--config Config/servers.json] [--console] [--hidden] "
        "[服务专属参数...]\n"
        "  默认：打开 Windows GUI 管理窗口（无控制台）。\n"
        "  --console  控制台模式（开发/CI/无人值守）。\n"
        "  --hidden   GUI 模式但隐藏窗口（由 LegendGame Studio 服务器中心管理）。\n",
        ServiceKey(role));
}

} // namespace

int RunServerMain(int argc, char** argv, ServerRole role, ServerMainFn serverMain) {
    ServerMainParams params;
    bool help = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            params.configPath = argv[++i];
        } else if (arg == "--console") {
            params.console = true;
        } else if (arg == "--hidden") {
            params.hidden = true;
        } else if (arg == "--help" || arg == "-h") {
            help = true;
        }
        // 其余参数原样透传给服务主函数自行解析
    }

    if (params.console) {
        if (help) {
            PrintUsage(role);
            return 0;
        }
        FixupWorkingDirectory();
        SetupConsole();
        ServerAdminContext context;
        context.SetConfigPath(params.configPath);
        return serverMain(params, context, argc, argv);
    }

    // ---- GUI 管理台模式（默认；双击 exe 即此路径）----
    if (help) {
        // GUI 模式下 --help 打印用法后退出
        PrintUsage(role);
        return 0;
    }
    FixupWorkingDirectory();

    ServerAdminContext context;
    context.SetConfigPath(params.configPath);

    // 先初始化 GUI：失败（如 Session 0 无桌面）时回退控制台模式直跑，
    // 保证 CI / 无人值守环境不因缺桌面而挂掉。
    ServerAdminApp app(role, context, params.hidden);
    std::string error;
    if (!app.Initialize(error)) {
        std::fprintf(stderr, "[AdminUI] %s\n[AdminUI] Falling back to console mode.\n",
                     error.c_str());
        ServerMainParams consoleParams;
        consoleParams.configPath = params.configPath;
        consoleParams.console = true;
        return serverMain(consoleParams, context, argc, argv);
    }

    params.externalStop = &context.StopFlag();
    int workerExitCode = 0;
    std::thread worker([&context, &params, argc, argv, serverMain, &workerExitCode]() {
        workerExitCode = serverMain(params, context, argc, argv);
        context.MarkWorkerDone(workerExitCode);
    });

    app.Run();
    if (worker.joinable()) {
        worker.join();
    }
    app.Shutdown();
    return workerExitCode;
}

} // namespace legend::admin
