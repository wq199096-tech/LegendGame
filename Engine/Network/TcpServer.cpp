#include "Engine/Network/TcpServer.h"

namespace legend::net {

TcpServer::TcpServer(NetworkService& service)
    : m_service(service),
      m_io(service.Io()),
      m_acceptor(m_io),
      m_nextSocket(m_io) {}

TcpServer::~TcpServer() {
    // 阶段9.3 UAF 修复：销毁兜底——未显式 Stop 时同样摘除连接回调，
    // 防挂起 Fail 触达已释放的 TcpServer（RemoveConnection UAF）
    Stop();
}

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
    // 阶段9.3 UAF 修复：shared_from_this 保活——挂起的 async_accept 完成回调
    // 可能晚于持有者（LoginServer/GatewayServer）析构才执行（IOCP abort 路径）
    auto self = shared_from_this();
    m_acceptor.async_accept(m_nextSocket, [self](std::error_code ec) {
        if (!self->m_accepting.load()) {
            return; // Stop 中
        }
        if (!ec) {
            const std::uint64_t id = self->AllocateConnectionId();
            auto connection = std::make_shared<TcpConnection>(std::move(self->m_nextSocket), id);
            // 阶段9.1指令二/三：TcpServer 不再 Start（业务层唯一 Start 决定 handler）；
            // 内部 close observer 负责连接表清理（指令一百一十七）
            connection->SetInternalCloseHandler(
                [self](std::uint64_t closedId) { self->RemoveConnection(closedId); });
            self->m_connections[id] = connection;
            if (self->m_onAccept) {
                self->m_onAccept(connection); // 业务层自行调用一次 Start
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
    // 阶段9.3：摘除业务 accept 回调（其捕获 LoginServer/GatewayServer 的 this，
    // 持有者销毁后不得触达；挂起 accept 由 self 保活并在此安全丢弃）
    m_onAccept = nullptr;
    std::error_code ignored;
    m_acceptor.close(ignored);
    for (auto& [id, connection] : m_connections) {
        // 阶段9.3 UAF 修复：先经 strand 摘除连接全部回调再关闭——
        // Server（LoginServer/GatewayServer）销毁后，挂起的 Fail 不得再
        // 触达已释放的 TcpServer（RemoveConnection heap-use-after-free）
        // Stop3: synchronous close - FIN is on the wire when Stop returns.
        connection->CloseBlocking();
    }
    m_connections.clear();
}

} // namespace legend::net
