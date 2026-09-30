// ---------------------------------------------------------------------------
// 阶段24：ClientSmokeChecks —— 真实启动 LegendClient.exe 冒烟（指令四十七）。
// 自动启动客户端，LEGEND_CLIENT_VISUAL_SMOKE=1 模式下运行 15 秒后干净退出：
//   - 进程退出码必须为 0（无崩溃）
//   - Logs/latest.log 必须包含里程碑标记：
//     gl-context-ready / shader-compiled / asset-manifest-loaded /
//     game-scene-started / pass
//   说明：Engine::Initialize 顺序为 Window→GL Context→Shader→Resources→Scene，
//   因此 "game-scene-started" 蕴含 窗口+GL+Shader 全部成功（无弱化）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef LEGEND_SOURCE_DIR
#define LEGEND_SOURCE_DIR "."
#endif

namespace worldtest {

namespace {

namespace fs = std::filesystem;

std::string ReadFileText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::string();
    }
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

bool Contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

void RunClientSmokeLogicChecks() {
    // ---- 定位 LegendClient.exe（与测试 exe 同目录：Build/bin/Debug）----
    char testExePath[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, testExePath, MAX_PATH);
    const fs::path clientExe =
        fs::path(testExePath).parent_path() / "LegendClient.exe";
    std::error_code ec;
    Check("ClientSmoke: LegendClient.exe present", fs::exists(clientExe, ec));
    if (!fs::exists(clientExe, ec)) {
        return;
    }

    const std::string sourceDir = LEGEND_SOURCE_DIR;
    const fs::path logPath = fs::path(sourceDir) / "Logs" / "latest.log";

    // 清理上次日志（防旧标记污染——与测试事件基线同一纪律）。
    std::error_code removeEc;
    fs::remove(logPath, removeEc);

    // 子进程继承环境（LEGEND_CLIENT_VISUAL_SMOKE=1 → 15s 后干净退出）。
    SetEnvironmentVariableA("LEGEND_CLIENT_VISUAL_SMOKE", "1");

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string cmd = "\"" + clientExe.string() + "\"";
    const BOOL ok =
        CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr,
                       sourceDir.c_str(), &si, &pi);
    Check("ClientSmoke: client process started", ok);
    if (!ok) {
        SetEnvironmentVariableA("LEGEND_CLIENT_VISUAL_SMOKE", nullptr);
        return;
    }

    // 至少 15s 存活（等退出；上限 90s 防挂死）。
    const DWORD waitResult = WaitForSingleObject(pi.hProcess, 90000);
    DWORD exitCode = 0xFFFFFFFF;
    if (waitResult == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    } else if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1); // 挂死：强杀并判失败
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    SetEnvironmentVariableA("LEGEND_CLIENT_VISUAL_SMOKE", nullptr);

    Check("ClientSmoke: client exited cleanly (exit code 0 within 90s)",
          waitResult == WAIT_OBJECT_0 && exitCode == 0);

    // [Diag] 诊断行（CI 失败注解通道用）。
    std::printf("[Diag] ClientSmoke waitResult=%lu exitCode=%lu\n",
                static_cast<unsigned long>(waitResult), static_cast<unsigned long>(exitCode));

    // ---- 里程碑标记（日志）----
    const std::string log = ReadFileText(logPath);
    if (log.empty()) {
        Check("ClientSmoke: smoke log readable", false);
        return;
    }
    std::printf("[Diag] ClientSmoke log size=%zu head=%.180s\n", log.size(),
                log.c_str());
    Check("ClientSmoke: gl-context-ready", Contains(log, "[VisualSmoke] gl-context-ready"));
    Check("ClientSmoke: shader-compiled", Contains(log, "[VisualSmoke] shader-compiled"));
    Check("ClientSmoke: asset-manifest-loaded",
          Contains(log, "[VisualSmoke] asset-manifest-loaded"));
    Check("ClientSmoke: default-texture-ready",
          Contains(log, "[VisualSmoke] default-texture-ready"));
    Check("ClientSmoke: game-scene-started", Contains(log, "[VisualSmoke] game-scene-started"));
    Check("ClientSmoke: pass marker (15s alive, clean quit)",
          Contains(log, "[VisualSmoke] pass"));

    // [Diag] 输出关键行（CI 失败注解通道用）。
    std::printf("[Diag] ClientSmoke log size=%zu\n", log.size());
    if (!Contains(log, "[VisualSmoke] gl-context-ready")) {
        std::printf("[Diag] ClientSmoke: gl-context-ready missing (GPU-less environment?)\n");
    }
}

} // namespace

// 由 WorldChecks.cpp 调用（阶段24）。
void RunClientSmokeChecks() {
    std::printf("[ClientSmoke] checks begin\n");
    RunClientSmokeLogicChecks();
    std::printf("[ClientSmoke] checks complete (failures so far = %d)\n", g_failures);
}

} // namespace worldtest
