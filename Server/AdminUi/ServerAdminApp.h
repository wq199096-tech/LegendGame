#pragma once

#include "Server/AdminUi/IServiceProviderStats.h"
#include "Server/AdminUi/LogCapture.h"
#include "Server/AdminUi/ServerMainRunner.h"
#include "Server/AdminUi/ServerRoles.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;
typedef struct SDL_GLContextState* SDL_GLContext;

namespace legend::admin {

// Stage25.6 服务器管理台 GUI（六服共用，SDL3 + Dear ImGui + EditorTheme）。
// 主线程泵 SDL/ImGui；服务器生命周期在 worker 线程（ServerMainRunner 编排）。
class ServerAdminApp {
public:
    ServerAdminApp(ServerRole role, ServerAdminContext& context, bool hidden);
    ~ServerAdminApp();

    bool Initialize(std::string& error);
    // 阻塞主循环：关窗 -> 优雅停机请求 -> 等 worker join -> 返回
    void Run();
    void Shutdown();

private:
    void PumpEvents(bool& quitRequested);
    void CollectTick();   // 每 500ms：provider->Collect + 日志快照刷新
    void LogTick();       // 每 250ms：日志环形缓冲快照
    void DrawUi();
    void DrawOverview();
    void DrawConnections();
    void DrawPerformance();
    void DrawLogs();
    void DrawConfig();
    void DrawStatusBar();
    void DrawDependenciesTable();

    std::string FormatUptime(double seconds) const;
    std::string FormatBytes(std::uint64_t bytes) const;
    const std::string* FindExtra(const std::string& key) const;

    ServerRole m_role;
    ServerAdminContext& m_context;
    bool m_hidden = false;

    SDL_Window* m_window = nullptr;
    SDL_GLContext m_glContext = nullptr;
    bool m_sdlInitialized = false;
    bool m_imguiInitialized = false;
    float m_dpiScale = 1.0f;
    bool m_vsync = false;

    ServiceSnapshot m_snapshot;              // 最近一次统计快照（GUI 线程独占）
    bool m_hasSnapshot = false;
    std::chrono::steady_clock::time_point m_lastCollect{};
    std::chrono::steady_clock::time_point m_lastLogTick{};

    // 速率计算（性能页）
    bool m_hasRateBase = false;
    std::uint64_t m_prevRequests = 0;
    std::uint64_t m_prevPacketsRx = 0;
    std::uint64_t m_prevPacketsTx = 0;
    std::chrono::steady_clock::time_point m_rateSince{};
    double m_requestRate = 0.0;
    double m_rxRate = 0.0;
    double m_txRate = 0.0;

    // 日志页状态
    std::vector<LogCapture::Line> m_logView;
    int m_logFilter = 0;                     // 0 全部 / 1 信息 / 2 警告 / 3 错误
    char m_logSearch[128] = "";
    bool m_pauseScroll = false;
    std::string m_searchLower;
};

} // namespace legend::admin
