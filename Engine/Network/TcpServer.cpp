#include "Engine/Network/TcpServer.h"

namespace legend::net {

TcpServer::TcpServer(NetworkService& service)
    : m_service(service),
      m_io(service.Io()),
      m_acceptor(m_io),
      m_nextSocket(m_io) {}

bool TcpServer::Listen(std::uint16_t port, std::string& error) {
    try {
        asio::ip::tcp::endpoint endpoint(asio::ip::make_address("127.0.0.1"), port);
        m_acceptor.open(endpoint.protocol());
        // 指令一百三十四：reuse_address（测试重复运行不 address already in use）
        m_acceptor.set_option(asio::socket_base::reuse_address(true));
        m_acceptor.bind(endpoint);
        m_acceptor.listen(asio::socket_base::max_listen_connections);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

void TcpServer::StartAccepting(AcceptHandler onAccept) {
    m_onAccept = std::move(onAccept);
    m_accepting.store(true);
    DoAccept();
}

void TcpServer::DoAccept() {
    if (!m_accepting.load()) {
        return;
    }
    auto self = this;
    m_acceptor.async_accept(m_nextSocket, [self](std::error_code ec) {
        if (!self->m_accepting.load()) {
            return; // Stop 中
        }
        if (!ec) {
            const std::uint64_t id = self->AllocateConnectionId();
            auto connection = std::make_shared<TcpConnection>(std::move(self->m_nextSocket), id);
            // Close 时从连接表移除（指令一百一十七：DisconnectCleanup）
            connection->Start(
                [self](const legend::network::Packet& packet) {
                    // Packet 回调由 Server 子类注册；此处基类只做 accept
                    (void)packet;
                },
                [self](std::uint64_t closedId, const std::error_code& closeEc) {
                    self->RemoveConnection(closedId);
                    if (self->m_onAccept) {
                        // 复用 onAccept 通道通知关闭由子类实现——基类仅移除
                    }
                    (void)closeEc;
                });
            self->m_connections[id] = connection;
            if (self->m_onAccept) {
                self->m_onAccept(connection);
            }
        }
        self->m_nextSocket = asio::ip::tcp::socket(self->m_io);
        self->DoAccept(); // 继续 accept 循环
    });
}

std::uint64_t TcpServer::AllocateConnectionId() {
    return m_nextConnectionId.fetch_add(1);
}

void TcpServer::RemoveConnection(std::uint64_t id) {
    m_connections.erase(id);
}

void TcpServer::Stop() {
    m_accepting.store(false);
    std::error_code ignored;
    m_acceptor.close(ignored);
    for (auto& [id, connection] : m_connections) {
        connection->Close();
    }
    m_connections.clear();
}

} // namespace legend::net
