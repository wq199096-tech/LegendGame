#include "Server/LoginServer/LoginServer.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"
#include "Shared/Network/MessageId.h"

namespace legend::login {

using legend::network::ByteReader;
using legend::network::ByteWriter;
using legend::network::MessageId;
using legend::network::Packet;
namespace net = legend::net;

namespace {
// 指令五十三：阶段9 临时认证（仅用于网络链路验收；后续阶段接数据库）
constexpr const char* kTestUsername = "test";
constexpr const char* kTestToken = "dev_token";
// 指令五十四：测试账号成功固定值
constexpr std::uint32_t kTestAccountId = 1001;
constexpr const char* kTestDisplayName = "TestPlayer";
} // namespace

LoginServer::LoginServer(net::NetworkService& service)
    : m_service(service), m_server(service) {}

bool LoginServer::Start(std::string& error) {
    if (!m_server.Listen(m_config.listenPort, error)) {
        return false;
    }
    m_server.StartAccepting(
        [this](net::TcpConnectionPtr connection) { OnGatewayAccepted(std::move(connection)); });
    return true;
}

void LoginServer::Stop() {
    m_stopped.store(true);
    m_server.Stop();
    m_gateways.clear();
}

void LoginServer::OnGatewayAccepted(net::TcpConnectionPtr connection) {
    const std::uint64_t id = connection->Id();
    GatewayLink link;
    link.connection = connection;
    m_gateways[id] = link; // 指令九十一：[Login] Gateway connected.
    connection->Start(
        [this, id](const Packet& packet) { OnGatewayPacket(id, packet); },
        [this, id](std::uint64_t, const std::error_code&) { OnGatewayClosed(id); });
}

void LoginServer::OnGatewayClosed(std::uint64_t connectionId) {
    m_gateways.erase(connectionId);
    if (m_hooks.onGatewayConnectionChanged) {
        m_hooks.onGatewayConnectionChanged(false);
    }
}

void LoginServer::OnGatewayPacket(std::uint64_t connectionId, const Packet& packet) {
    switch (static_cast<MessageId>(packet.header.messageId)) {
        case MessageId::ClientHello: {
            // Gateway -> Login 的握手：直接接受（内网服务间信任；版本校验同 Client 链）
            ByteReader reader(packet.payload.data(), packet.payload.size());
            reader.ReadUInt16();
            std::string build;
            std::string name;
            if (!reader.ReadString(build) || !reader.ReadString(name)) {
                if (auto it = m_gateways.find(connectionId); it != m_gateways.end()) {
                    it->second.connection->Close();
                    m_gateways.erase(it);
                }
                return;
            }
            if (auto it = m_gateways.find(connectionId); it != m_gateways.end()) {
                it->second.handshakeDone = true;
            }
            return;
        }
        case MessageId::GatewayLoginForward:
            HandleAuthRequest(connectionId, packet);
            return;
        default:
            return;
    }
}

void LoginServer::HandleAuthRequest(std::uint64_t gatewayConnectionId, const Packet& packet) {
    auto it = m_gateways.find(gatewayConnectionId);
    if (it == m_gateways.end()) {
        return;
    }
    ByteReader reader(packet.payload.data(), packet.payload.size());
    const std::uint64_t requestId = reader.ReadUInt64();
    const std::uint64_t clientConnectionId = reader.ReadUInt64();
    std::string username;
    std::string token;
    if (!reader.ReadString(username) || !reader.ReadString(token) || !reader.IsValid()) {
        return; // 畸形请求：丢弃（Gateway 侧有 pending 超时兜底）
    }

    // 指令五十三：test/dev_token 成功（accountId=1001），其它失败
    const bool success = username == kTestUsername && token == kTestToken;
    const std::uint32_t accountId = success ? kTestAccountId : 0;
    const std::string displayName = success ? kTestDisplayName : "";

    // 指令九十一：[Login] Auth request/Auth success（不打印 token，指令五十二）
    Packet response;
    response.header.messageId = static_cast<std::uint16_t>(MessageId::LoginGatewayResponse);
    ByteWriter writer(response.payload);
    writer.WriteUInt64(requestId);
    writer.WriteUInt64(clientConnectionId);
    writer.WriteBool(success);
    writer.WriteUInt32(accountId);
    writer.WriteString(displayName);
    writer.WriteString(success ? "ok" : "invalid username or token");
    it->second.connection->Send(response);
    if (m_hooks.onAuthResult) {
        m_hooks.onAuthResult(requestId, username, success, accountId);
    }
}

} // namespace legend::login
