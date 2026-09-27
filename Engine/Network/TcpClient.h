#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpConnection.h"

#include <functional>
#include <string>

namespace legend::net {

// 阶段9 指令三十四/三十五/三十六/三十七：TcpClient——异步 Connect
//（async_resolve + async_connect，绝不阻塞调用线程）+ 5 秒连接超时。
class TcpClient : public std::enable_shared_from_this<TcpClient> {
public:
    using ConnectedHandler = std::function<void(TcpConnectionPtr)>;
    using FailureHandler = std::function<void(const std::error_code&)>;

    explicit TcpClient(NetworkService& service);

    void Connect(const std::string& host, std::uint16_t port,
                 ConnectedHandler onConnected, FailureHandler onFailure);
    void Cancel();

private:
    void StartConnectTimer();
    void HandleResolve(const std::error_code& ec,
                       asio::ip::tcp::resolver::results_type results);
    void HandleConnect(const std::error_code& ec,
                       const asio::ip::tcp::endpoint& endpoint);
    void TryNextEndpoint();
    void Fail(const std::error_code& ec);

    NetworkService& m_service;
    asio::io_context& m_io;
    asio::ip::tcp::resolver m_resolver;
    asio::ip::tcp::socket m_socket;
    asio::steady_timer m_connectTimer;
    asio::ip::tcp::resolver::results_type m_endpoints;
    std::size_t m_endpointIndex = 0;
    ConnectedHandler m_onConnected;
    FailureHandler m_onFailure;
    std::atomic<bool> m_pending{false};
    std::uint64_t m_nextConnectionId = 1; // Client 侧单连接，ID 固定 1
};

} // namespace legend::net
