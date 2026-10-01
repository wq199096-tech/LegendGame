#pragma once

#include "Server/AdminUi/IServiceProviderStats.h"
#include "Server/AdminUi/LogCapture.h"
#include "Server/AdminUi/ServerRoles.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace legend::admin {

// 传给各服务器主函数的运行参数（RunServerMain 预扫描命令行后填充）
struct ServerMainParams {
    std::string configPath = "Config/servers.json";
    bool console = false;   // --console：开发/CI/无人值守控制台模式
    bool hidden = false;    // --hidden：GUI 模式但窗口隐藏（Studio 服务器中心启动用）
    // 优雅停机请求（GUI 关窗/Studio 停止时置位；主循环轮询）
    std::atomic<bool>* externalStop = nullptr;
};

// 服务器进程上下文：worker 线程（服务器生命周期）与 GUI 线程之间的桥。
class ServerAdminContext {
public:
    // worker：服务器对象创建后注册统计适配器（GUI 线程读取）
    void SetStatsProvider(std::shared_ptr<IServiceProviderStats> provider) {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats = std::move(provider);
    }
    std::shared_ptr<IServiceProviderStats> StatsProvider() const {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        return m_stats;
    }

    LogCapture& Logs() { return m_logs; }
    void AttachLogCapture() { m_logs.Attach(); }

    // 优雅停机请求（GUI 关窗 -> RequestStop；服务器主循环轮询 StopRequested）
    void RequestStop() { m_stop.store(true); }
    bool StopRequested() const { return m_stop.load(); }
    // worker 线程把该旗子挂进 ServerMainParams::externalStop
    std::atomic<bool>& StopFlag() { return m_stop; }

    // worker 状态（GUI 线程轮询）
    void MarkWorkerDone(int exitCode) {
        m_exitCode = exitCode;
        m_workerDone.store(true);
    }
    bool WorkerDone() const { return m_workerDone.load(); }
    int WorkerExitCode() const { return m_exitCode; }

    const std::string& ConfigPath() const { return m_configPath; }
    void SetConfigPath(std::string path) { m_configPath = std::move(path); }

private:
    LogCapture m_logs;
    mutable std::mutex m_statsMutex;
    std::shared_ptr<IServiceProviderStats> m_stats;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_workerDone{false};
    int m_exitCode = 0;
    std::string m_configPath;
};

// 各服务器主函数签名：完整生命周期（解析剩余参数 -> Logger::Init ->
// NetworkService -> Server.Start -> 轮询停机 -> 优雅停机序 -> 返回退出码）。
// GUI 模式下在 worker 线程执行；必须轮询 params.externalStop。
using ServerMainFn = int (*)(const ServerMainParams&, ServerAdminContext&, int, char**);

// 六服务器统一入口：
// - 预扫描 --config/--console/--hidden/--help（其余 argv 原样透传）；
// - --console：附加/分配控制台后主线程直跑（保留 asio signal_set 停机，CI 兼容）；
// - 默认：GUI 管理窗口（worker 线程跑服务器，主线程泵 SDL/ImGui；
//   关窗 = Graceful Shutdown，绝不 TerminateProcess）。
// - 启动前把 CWD 修正到包含 Config/servers.json 的目录（双击 exe 兼容）。
int RunServerMain(int argc, char** argv, ServerRole role, ServerMainFn serverMain);

} // namespace legend::admin
