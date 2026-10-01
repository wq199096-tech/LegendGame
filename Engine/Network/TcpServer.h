#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpConnection.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace legend::net {

// 阶段9 指令三十八/三十九/二十七：TcpServer——async_accept 循环 +
// ConnectionId 单调分配（不用裸 socket 指针）+ 优雅 Stop。
// 阶段9.3 UAF 修复：enable_shared_from_this——挂起的 async_accept 完成
// 回调持 shared_ptr 保活，Server（LoginServer/GatewayServer）先于 Service
// 销毁时 abort 完成路径不得触达已释放对象。
class TcpServer : public std::enable_shared_from_this<TcpServer> {
public:
    using AcceptHandler = std::function<void(TcpConnectionPtr)>;
    using CloseHandler = std::function<void(std::uint64_t, const std::error_code&)>;

    explicit TcpServer(NetworkService& service);
    // 阶段9.3 UAF 修复：销毁兜底（与 Stop 相同的回调摘除语义，防挂起 Fail 触达已释放 Server）
    ~TcpServer();

    // 监听（reuse_address：阶段9 指令一百三十四，测试重复运行不 address_in_use）
    bool Listen(std::uint16_t port, std::string& error);
    // 开始 accept 循环（Listen 成功后调用）
    void StartAccepting(AcceptHandler onAccept);
    // 指令三十九：停止 accept + 关闭全部连接 + io stop 由 Service 负责
    void Stop();

    std::uint64_t AllocateConnectionId();
    // Stage25.6 服务器管理台：进程生命周期累计收发包数（连接关闭时并入，单调递增）
    std::uint64_t PacketsReceived() const { return m_totalPacketsRx.load(std::memory_order_relaxed); }
    std::uint64_t PacketsSent() const { return m_totalPacketsTx.load(std::memory_order_relaxed); }

private:
    void DoAccept();
    void RemoveConnection(std::uint64_t id);

    NetworkService& m_service;
    asio::io_context& m_io;
    asio::ip::tcp::acceptor m_acceptor;
    asio::ip::tcp::socket m_nextSocket;
    AcceptHandler m_onAccept;
    std::atomic<std::uint64_t> m_nextConnectionId{1}; // 指令二十七：单调分配
    // 阶段10：m_connections 由 io 线程（accept/内部关闭回调）与持有者线程
    //（Stop）并发访问——加锁（Stop 遍历 vs RemoveConnection erase 竞态修复）
    std::mutex m_connectionsMutex;
    std::map<std::uint64_t, TcpConnectionPtr> m_connections;
    std::atomic<bool> m_accepting{false};
    std::atomic<std::uint64_t> m_totalPacketsRx{0}; // Stage25.6 管理台埋点
    std::atomic<std::uint64_t> m_totalPacketsTx{0};
};

} // namespace legend::net
