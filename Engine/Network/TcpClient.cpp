#include "Engine/Network/TcpClient.h"

#include <chrono>

namespace legend::net {

namespace {
// 指令三十七：Client Connect 5 秒超时
constexpr auto kConnectTimeout = std::chrono::seconds(5);
} // namespace

TcpClient::TcpClient(NetworkService& service)
    : m_service(service),
      m_io(service.Io()),
      m_resolver(m_io),
      m_socket(m_io),
      m_connectTimer(m_io) {}

void TcpClient::Connect(const std::string& host, std::uint16_t port,
                        ConnectedHandler onConnected, FailureHandler onFailure) {
    if (m_pending.exchange(true)) {
        return; // 已有连接尝试进行中
    }
    m_onConnected = std::move(onConnected);
    m_onFailure = std::move(onFailure);
    m_endpointIndex = 0;
    m_socket = asio::ip::tcp::socket(m_io); // 复位旧 socket

    auto self = shared_from_this();
    auto resolver = std::make_shared<asio::ip::tcp::resolver>(m_io);
    // 指令三十五：async_resolve + async_connect，绝不阻塞
    resolver->async_resolve(host, std::to_string(port),
                            [this, self, resolver](std::error_code ec,
                                                   asio::ip::tcp::resolver::results_type results) {
                                if (ec) {
                                    Fail(ec); // 指令三十六：DNS 失败 -> Failed，不 Crash
                                    return;
                                }
                                m_endpoints = results;
                                StartConnectTimer();
                                TryNextEndpoint();
                            });
}

void TcpClient::Cancel() {
    if (m_pending.exchange(false)) {
        std::error_code ignored;
        m_socket.close(ignored);
        m_connectTimer.cancel();
    }
}

void TcpClient::StartConnectTimer() {
    m_connectTimer.expires_after(kConnectTimeout);
    auto self = shared_from_this();
    m_connectTimer.async_wait([self](const std::error_code& ec) {
        if (ec) {
            return; // timer 被 cancel（连接成功/手动取消）
        }
        // 超时：cancel socket -> 连接回调收到 operation_aborted -> Fail
        self->Fail(std::make_error_code(std::errc::timed_out));
    });
}

void TcpClient::TryNextEndpoint() {
    if (m_endpointIndex >= m_endpoints.size()) {
        Fail(std::make_error_code(std::errc::host_unreachable));
        return;
    }
    auto it = m_endpoints.begin();
    std::advance(it, m_endpointIndex);
    ++m_endpointIndex;
    auto self = shared_from_this();
    m_socket.async_connect(*it, [self, this](std::error_code ec) {
        if (ec) {
            if (ec == asio::error::operation_aborted) {
                return; // timer 已处理
            }
            TryNextEndpoint(); // 多 endpoint 逐个尝试
            return;
        }
        m_connectTimer.cancel();
        m_socket.set_option(asio::ip::tcp::no_delay(true));
        auto connection = std::make_shared<TcpConnection>(std::move(m_socket),
                                                          m_nextConnectionId);
        m_pending.store(false);
        if (m_onConnected) {
            m_onConnected(connection);
        }
    });
}

void TcpClient::Fail(const std::error_code& ec) {
    if (!m_pending.exchange(false)) {
        return;
    }
    m_connectTimer.cancel();
    std::error_code ignored;
    m_socket.close(ignored);
    if (m_onFailure) {
        m_onFailure(ec);
    }
}

} // namespace legend::net
