#include "Server/AdminUi/ServerAdminApp.h"

#include "Tools/UiCore/Source/EditorTheme.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <SDL3/SDL.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace legend::admin {

namespace theme = legend::editor::theme; // 与 Studio 共用同一主题（Tools/UiCore）

namespace {

ImVec4 LevelColor(int level) {
    switch (level) {
        case 0: return theme::MutedText();                       // Debug
        case 2: return theme::Warning();                         // Warning
        case 3: return theme::Error();                           // Error
        default: return ImVec4(0.91f, 0.91f, 0.92f, 1.0f);       // Info
    }
}

bool ContainsIgnoreCase(const std::string& haystack, const std::string& needleLower) {
    if (needleLower.empty()) {
        return true;
    }
    std::string lower;
    lower.reserve(haystack.size());
    for (char c : haystack) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lower.find(needleLower) != std::string::npos;
}

std::string NowString() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

} // namespace

ServerAdminApp::ServerAdminApp(ServerRole role, ServerAdminContext& context, bool hidden)
    : m_role(role), m_context(context), m_hidden(hidden) {
    m_snapshot.serviceCnName = ServiceCnName(role);
    m_logSearch[0] = '\0';
}

ServerAdminApp::~ServerAdminApp() {
    Shutdown();
}

bool ServerAdminApp::Initialize(std::string& error) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        error = std::string("SDL_Init 失败: ") + SDL_GetError();
        return false;
    }
    m_sdlInitialized = true;

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    // SDL3 标题为 UTF-8；Win32 FindWindowW 定位标题见 WindowTitle()（两者文案一致）
    const std::string title =
        std::string("LegendGame ") + ServiceCnName(m_role) + " 管理台";
    SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
        (m_hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_HIGH_PIXEL_DENSITY));
    m_window = SDL_CreateWindow(title.c_str(), 1120, 720, flags);
    if (!m_window) {
        error = std::string("创建管理窗口失败: ") + SDL_GetError();
        return false;
    }

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        error = std::string("创建 OpenGL 上下文失败: ") + SDL_GetError();
        return false;
    }
    SDL_GL_MakeCurrent(m_window, m_glContext);
    m_vsync = SDL_GL_SetSwapInterval(1);

    // ---- ImGui 初始化（与 Studio 同序、同主题、同中文字体链）----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    m_dpiScale = std::clamp(SDL_GetWindowDisplayScale(m_window), 1.0f, 2.0f);
    theme::Apply(m_dpiScale);
    theme::LoadChineseFont(io, m_dpiScale);
    ImGui_ImplSDL3_InitForOpenGL(m_window, m_glContext);
    ImGui_ImplOpenGL3_Init("#version 330");
    m_imguiInitialized = true;

    m_lastCollect = std::chrono::steady_clock::now() - std::chrono::milliseconds(600);
    return true;
}

void ServerAdminApp::Run() {
    bool windowCloseRequested = false;
    for (;;) {
        PumpEvents(windowCloseRequested);
        if (windowCloseRequested && !m_context.StopRequested()) {
            // 关窗 = Graceful Shutdown（绝不 TerminateProcess）
            m_context.RequestStop();
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastCollect >= std::chrono::milliseconds(500)) {
            CollectTick();
            m_lastCollect = now;
        }
        if (now - m_lastLogTick >= std::chrono::milliseconds(250)) {
            LogTick();
            m_lastLogTick = now;
        }

        // 退出条件：用户已请求关窗 且 worker 完成优雅停机
        if (windowCloseRequested && m_context.WorkerDone()) {
            break;
        }
        // worker 已完成且窗口从未请求关闭（如启动失败退出）——保持窗口展示日志，
        // 等用户手动关窗（不得无声闪退吞掉错误信息）。
        if (windowCloseRequested) {
            m_context.RequestStop(); // 持续保旗（防止竞态清除）
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        DrawUi();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(m_window);
        if (!m_vsync) {
            SDL_Delay(16);
        }
    }
    m_context.Logs().Detach();
}

void ServerAdminApp::Shutdown() {
    if (m_imguiInitialized) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        m_imguiInitialized = false;
    }
    if (m_glContext) {
        SDL_GL_DestroyContext(m_glContext);
        m_glContext = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_sdlInitialized) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        m_sdlInitialized = false;
    }
}

void ServerAdminApp::PumpEvents(bool& quitRequested) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
             event.window.windowID == SDL_GetWindowID(m_window))) {
            quitRequested = true;
        }
    }
}

void ServerAdminApp::CollectTick() {
    if (auto provider = m_context.StatsProvider()) {
        ServiceSnapshot next = provider->Collect();
        next.serviceCnName = ServiceCnName(m_role);
        next.warnCount = m_context.Logs().WarnCount();
        next.errorCount = m_context.Logs().ErrorCount();

        // 性能页速率（相邻两次 Collect 差分）
        if (m_hasRateBase) {
            const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                            m_rateSince)
                                  .count();
            if (dt > 0.0) {
                m_requestRate =
                    static_cast<double>(next.requestCount - m_prevRequests) / dt;
                m_rxRate = static_cast<double>(next.packetsReceived - m_prevPacketsRx) / dt;
                m_txRate = static_cast<double>(next.packetsSent - m_prevPacketsTx) / dt;
            }
        }
        m_prevRequests = next.requestCount;
        m_prevPacketsRx = next.packetsReceived;
        m_prevPacketsTx = next.packetsSent;
        m_rateSince = std::chrono::steady_clock::now();
        m_hasRateBase = true;

        m_snapshot = std::move(next);
        m_hasSnapshot = true;
    }
    LogTick();
}

void ServerAdminApp::LogTick() {
    m_logView = m_context.Logs().Snapshot();
    m_searchLower.clear();
    for (char c : m_logSearch) {
        m_searchLower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
}

void ServerAdminApp::DrawUi() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus |
                                       ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##ServerAdminHost", nullptr, hostFlags);
    ImGui::PopStyleVar();

    if (ImGui::BeginTabBar("##AdminTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("概览")) {
            DrawOverview();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("连接")) {
            DrawConnections();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("性能")) {
            DrawPerformance();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("日志")) {
            DrawLogs();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("配置")) {
            DrawConfig();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    DrawStatusBar();
    ImGui::End();
}

std::string ServerAdminApp::FormatUptime(double seconds) const {
    if (seconds < 0.0) {
        return "--";
    }
    const auto total = static_cast<long long>(seconds);
    const long long days = total / 86400;
    const long long hours = (total % 86400) / 3600;
    const long long minutes = (total % 3600) / 60;
    const long long secs = total % 60;
    char buf[64];
    if (days > 0) {
        std::snprintf(buf, sizeof(buf), "%lld天 %02lld:%02lld:%02lld", days, hours, minutes, secs);
    } else {
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", hours, minutes, secs);
    }
    return buf;
}

std::string ServerAdminApp::FormatBytes(std::uint64_t bytes) const {
    char buf[64];
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        std::snprintf(buf, sizeof(buf), "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ull * 1024ull) {
        std::snprintf(buf, sizeof(buf), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ull) {
        std::snprintf(buf, sizeof(buf), "%.2f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return buf;
}

const std::string* ServerAdminApp::FindExtra(const std::string& key) const {
    if (!m_hasSnapshot) {
        return nullptr;
    }
    for (const auto& [k, v] : m_snapshot.extra) {
        if (k == key) {
            return &v;
        }
    }
    return nullptr;
}

void ServerAdminApp::DrawOverview() {
    ImGui::BeginChild("##overview", ImGui::GetContentRegionAvail(), 0);

    // 服务标题 + 状态
    ImGui::TextUnformatted(m_snapshot.serviceCnName.c_str());
    ImGui::SameLine();
    const bool closing = m_context.StopRequested();
    const bool exited = m_context.WorkerDone();
    if (exited) {
        const int code = m_context.WorkerExitCode();
        ImGui::TextColored(theme::Error(), code == 0 ? "已退出 (码 %d)" : "启动失败 (码 %d)", code);
    } else if (closing) {
        ImGui::TextColored(theme::Warning(), "正在优雅关闭…");
    } else if (m_hasSnapshot && m_snapshot.running) {
        ImGui::TextColored(theme::Success(), "运行中");
    } else {
        ImGui::TextColored(theme::Warning(), "启动中…");
    }
    ImGui::Separator();

    if (ImGui::BeginTable("##ovBasic", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("服务名称");
        ImGui::TableSetupColumn("启动时间");
        ImGui::TableSetupColumn("运行时长");
        ImGui::TableSetupColumn("监听 IP");
        ImGui::TableSetupColumn("端口");
        ImGui::TableSetupColumn("当前连接数");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(m_snapshot.serviceCnName.c_str());
        ImGui::TableNextColumn();
        if (m_hasSnapshot && m_snapshot.startTime.time_since_epoch().count() > 0) {
            const std::time_t t = std::chrono::system_clock::to_time_t(m_snapshot.startTime);
            std::tm tm{};
            localtime_s(&tm, &t);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
            ImGui::TextUnformatted(buf);
        } else {
            ImGui::TextDisabled("--");
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(m_hasSnapshot ? FormatUptime(m_snapshot.uptimeSeconds).c_str() : "--");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(m_hasSnapshot ? m_snapshot.listenIp.c_str() : "--");
        ImGui::TableNextColumn();
        ImGui::Text("%u", m_hasSnapshot ? m_snapshot.listenPort : 0);
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(m_snapshot.connectionCount));
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::BeginTable("##ovCounters", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("请求数量");
        ImGui::TableSetupColumn("警告数");
        ImGui::TableSetupColumn("错误数");
        ImGui::TableSetupColumn("收发包", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(m_snapshot.requestCount));
        ImGui::TableNextColumn();
        ImGui::TextColored(theme::Warning(), "%llu", static_cast<unsigned long long>(m_snapshot.warnCount));
        ImGui::TableNextColumn();
        ImGui::TextColored(theme::Error(), "%llu", static_cast<unsigned long long>(m_snapshot.errorCount));
        ImGui::TableNextColumn();
        const std::string* rx = FindExtra("性能·收包数");
        const std::string* tx = FindExtra("性能·发包数");
        ImGui::Text("%s / %s", rx ? rx->c_str() : "--", tx ? tx->c_str() : "--");
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("依赖服务状态");
    DrawDependenciesTable();

    // 专属数据（概览页 = 无前缀键）
    ImGui::Spacing();
    ImGui::TextUnformatted("服务专属数据");
    bool any = false;
    if (ImGui::BeginTable("##ovExtra", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("项目", ImGuiTableColumnFlags_WidthFixed, theme::Px(220));
        ImGui::TableSetupColumn("数值");
        for (const auto& [key, value] : m_snapshot.extra) {
            if (key.rfind("性能·", 0) == 0 || key.rfind("会话状态·", 0) == 0) {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(key.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value.c_str());
            any = true;
        }
        ImGui::EndTable();
    }
    if (!any) {
        ImGui::TextDisabled("（暂无）");
    }
    ImGui::EndChild();
}

void ServerAdminApp::DrawDependenciesTable() {
    if (ImGui::BeginTable("##deps", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("依赖服务", ImGuiTableColumnFlags_WidthFixed, theme::Px(220));
        ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, theme::Px(120));
        ImGui::TableSetupColumn("说明");
        ImGui::TableHeadersRow();
        for (const auto& dep : m_snapshot.dependencies) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(dep.name.c_str());
            ImGui::TableNextColumn();
            if (dep.healthy && !dep.degraded) {
                ImGui::TextColored(theme::Success(), "正常");
            } else if (dep.healthy && dep.degraded) {
                ImGui::TextColored(theme::Warning(), "降级");
            } else {
                ImGui::TextColored(theme::Error(), "不可用");
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(dep.detail.c_str());
        }
        ImGui::EndTable();
    }
}

void ServerAdminApp::DrawConnections() {
    ImGui::BeginChild("##connections", ImGui::GetContentRegionAvail(), 0);
    ImGui::Text("当前连接数：%llu",
                static_cast<unsigned long long>(m_snapshot.connectionCount));
    ImGui::Text("请求数量：%llu", static_cast<unsigned long long>(m_snapshot.requestCount));
    ImGui::Spacing();
    ImGui::TextUnformatted("依赖服务状态");
    DrawDependenciesTable();
    ImGui::Spacing();
    ImGui::TextUnformatted("会话状态分布");
    bool any = false;
    if (ImGui::BeginTable("##cnSession", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, theme::Px(220));
        ImGui::TableSetupColumn("数量");
        for (const auto& [key, value] : m_snapshot.extra) {
            if (key.rfind("会话状态·", 0) != 0) {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(key.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value.c_str());
            any = true;
        }
        ImGui::EndTable();
    }
    if (!any) {
        ImGui::TextDisabled("（本服务无会话状态分层数据）");
    }
    ImGui::EndChild();
}

void ServerAdminApp::DrawPerformance() {
    ImGui::BeginChild("##performance", ImGui::GetContentRegionAvail(), 0);
    if (ImGui::BeginTable("##perf", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("指标", ImGuiTableColumnFlags_WidthFixed, theme::Px(260));
        ImGui::TableSetupColumn("数值");
        auto row = [](const char* name, const std::string& value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value.c_str());
        };
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f 请求/秒", m_requestRate);
        row("请求速率", buf);
        std::snprintf(buf, sizeof(buf), "%.1f 包/秒", m_rxRate);
        row("收包速率", buf);
        std::snprintf(buf, sizeof(buf), "%.1f 包/秒", m_txRate);
        row("发包速率", buf);
        if (const std::string* v = FindExtra("性能·Tick平均(ms)")) {
            row("Tick 平均耗时", *v);
        }
        if (const std::string* v = FindExtra("性能·Tick最大(ms)")) {
            row("Tick 最大耗时", *v);
        }
        if (const std::string* v = FindExtra("性能·Queue长度")) {
            row("Queue 长度", *v);
        }
        if (const std::string* v = FindExtra("性能·收包数")) {
            row("累计收包数", *v);
        }
        if (const std::string* v = FindExtra("性能·发包数")) {
            row("累计发包数", *v);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void ServerAdminApp::DrawLogs() {
    // 工具栏：过滤 / 搜索 / 暂停滚动 / 清空显示（全部真实现）
    ImGui::SetNextItemWidth(theme::Px(120));
    if (ImGui::BeginCombo("##logfilter",
                          m_logFilter == 0 ? "全部"
                          : m_logFilter == 1 ? "信息"
                          : m_logFilter == 2 ? "警告"
                                             : "错误")) {
        if (ImGui::Selectable("全部", m_logFilter == 0)) m_logFilter = 0;
        if (ImGui::Selectable("信息", m_logFilter == 1)) m_logFilter = 1;
        if (ImGui::Selectable("警告", m_logFilter == 2)) m_logFilter = 2;
        if (ImGui::Selectable("错误", m_logFilter == 3)) m_logFilter = 3;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(theme::Px(240));
    ImGui::InputTextWithHint("##logsearch", "搜索日志…", m_logSearch, sizeof(m_logSearch));
    ImGui::SameLine();
    ImGui::Checkbox("暂停滚动", &m_pauseScroll);
    ImGui::SameLine();
    if (ImGui::Button("清空显示")) {
        m_context.Logs().Clear();
        m_logView.clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d 行", static_cast<int>(m_logView.size()));

    ImGui::BeginChild("##logscroll", ImGui::GetContentRegionAvail(),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_FrameStyle);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    const bool atBottom = ImGui::GetScrollY() + ImGui::GetWindowHeight() >=
                          ImGui::GetScrollMaxY() - 4.0f;
    for (const auto& line : m_logView) {
        if (m_logFilter == 1 && line.level > 1) continue;
        if (m_logFilter == 2 && line.level != 2) continue;
        if (m_logFilter == 3 && line.level != 3) continue;
        if (!m_searchLower.empty() && !ContainsIgnoreCase(line.text, m_searchLower)) continue;
        ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(line.level));
        ImGui::TextUnformatted(line.text.c_str(), line.text.c_str() + line.text.size());
        ImGui::PopStyleColor();
    }
    if (!m_pauseScroll && atBottom) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

void ServerAdminApp::DrawConfig() {
    ImGui::BeginChild("##config", ImGui::GetContentRegionAvail(), 0);
    if (ImGui::BeginTable("##cfg", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("配置项", ImGuiTableColumnFlags_WidthFixed, theme::Px(220));
        ImGui::TableSetupColumn("值");
        auto row = [](const char* name, const std::string& value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value.c_str());
        };
        row("配置文件", m_context.ConfigPath());
        row("服务标识", ServiceKey(m_role));
        row("服务名称", ServiceCnName(m_role));
        row("监听地址", m_hasSnapshot
                            ? m_snapshot.listenIp + ":" + std::to_string(m_snapshot.listenPort)
                            : std::string("--"));
        row("运行模式", m_hidden ? "GUI（隐藏窗口，由服务器中心管理）" : "GUI（管理窗口）");
        row("本地日志目录", "Logs/" + std::string(ServiceKey(m_role)));
        if (const std::string* v = FindExtra("配置·DB路径")) {
            row("数据库路径", *v);
        }
        if (const std::string* v = FindExtra("配置·日志根目录")) {
            row("服务日志根目录", *v);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    // 真实按钮：打开配置文件 / 打开本地日志目录
    if (ImGui::Button("打开配置文件")) {
        const std::string& path = m_context.ConfigPath();
        const std::wstring wpath(path.begin(), path.end());
        HINSTANCE result = ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr,
                                         SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            m_context.Logs().Push(3, "[AdminUI] 打开配置文件失败: " + path + "\n");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("打开本地日志目录")) {
        const std::string dir = "Logs/" + std::string(ServiceKey(m_role));
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::wstring wdir(dir.begin(), dir.end());
        ShellExecuteW(nullptr, L"open", wdir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::EndChild();
}

void ServerAdminApp::DrawStatusBar() {
    const float statusHeight = theme::Px(26.0f);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.y > statusHeight * 2.0f) {
        ImGui::BeginChild("##statusbar", ImVec2(0, statusHeight),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    } else {
        ImGui::BeginChild("##statusbar", ImVec2(0, statusHeight), ImGuiChildFlags_Borders);
    }
    const bool exited = m_context.WorkerDone();
    const bool closing = m_context.StopRequested();
    if (exited) {
        ImGui::TextColored(theme::Error(), "%s | 已退出(码 %d)", m_snapshot.serviceCnName.c_str(),
                           m_context.WorkerExitCode());
    } else if (closing) {
        ImGui::TextColored(theme::Warning(), "%s | 正在优雅关闭…（等待在途任务完成）",
                           m_snapshot.serviceCnName.c_str());
    } else {
        ImGui::TextColored(theme::Success(), "%s | 运行中 | 运行时长 %s", m_snapshot.serviceCnName.c_str(),
                           m_hasSnapshot ? FormatUptime(m_snapshot.uptimeSeconds).c_str() : "--");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("| 警告 %llu | 错误 %llu | %s",
                        static_cast<unsigned long long>(m_snapshot.warnCount),
                        static_cast<unsigned long long>(m_snapshot.errorCount),
                        NowString().c_str());
    ImGui::EndChild();
}

} // namespace legend::admin
