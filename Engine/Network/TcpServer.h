#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpConnection.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>

namespace legend::net {

// 阶段9 指令三十八/三十九/二十七：TcpServer——async_accept 循环 +
// ConnectionId 单调分配（不用裸 socket 指针）+ 优雅 Stop。
class TcpServer {
public:
    using AcceptHandler = std::function<void(TcpConnectionPtr)>;
    using CloseHandler = std::function<void(std::uint64_t, const std::error_code&)>;

    explicit TcpServer(NetworkService& service);

    // 监听（reuse_address：阶段9 指令一百三十四，测试重复运行不 address_in_use）
    bool Listen(std::uint16_t port, std::string& error);
    // 开始 accept 循环（Listen 成功后调用）
    void StartAccepting(AcceptHandler onAccept);
    // 指令三十九：停止 accept + 关闭全部连接 + io stop 由 Service 负责
    void Stop();

    std::uint64_t AllocateConnectionId();

private:
    void DoAccept();
    void RemoveConnection(std::uint64_t id);

    NetworkService& m_service;
    asio::io_context& m_io;
    asio::ip::tcp::acceptor m_acceptor;
    asio::ip::tcp::socket m_nextSocket;
    AcceptHandler m_onAccept;
    std::atomic<std::uint64_t> m_nextConnectionId{1}; // 指令二十七：单调分配
    std::map<std::uint64_t, TcpConnectionPtr> m_connections; // strand 内访问
    std::atomic<bool> m_accepting{false};
};

} // namespace legend::net
