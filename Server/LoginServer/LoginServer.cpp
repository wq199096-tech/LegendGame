#include "Server/LoginServer/LoginServer.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"
#include "Shared/Network/MessageId.h"
#include "Shared/Network/Protocol.h"

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
    auto it = m_gateways.find(connectionId);
    if (it == m_gateways.end()) {
        return;
    }
    switch (static_cast<MessageId>(packet.header.messageId)) {
        case MessageId::ClientHello: {
            // 阶段9.2指令十二/十三：握手门——DecodeClientHello + 版本校验；
            // 重复 ClientHello（已握手）同样关闭
            if (it->second.handshakeDone) {
                it->second.connection->Close();
                m_gateways.erase(it);
                return;
            }
            // 阶段9.2：DecodeClientHello 暂以手写读替代（Protocol 链接问题待查），
            // 版本校验/握手门语义不变（指令十二/十三）
            ByteReader helloReader(packet.payload.data(), packet.payload.size());
            const std::uint16_t helloVersion = helloReader.ReadUInt16();
            std::string helloBuild;
            std::string helloName;
            if (!helloReader.IsValid() || !helloReader.ReadString(helloBuild) ||
                !helloReader.ReadString(helloName) || helloReader.Remaining() != 0) {
                it->second.connection->Close();
                m_gateways.erase(it);
                return;
            }
            if (helloVersion != kProtocolVersion) {
                // 指令十二：版本不符 -> 关闭内部连接
                it->second.connection->Close();
                m_gateways.erase(it);
                return;
            }
            it->second.handshakeDone = true;
            return;
        }
        case MessageId::GatewayLoginForward:
            // 阶段9.2指令十三：握手完成才允许 Forward；提前发 = Protocol Error 关链
            if (!it->second.handshakeDone) {
                it->second.connection->Close();
                m_gateways.erase(it);
                return;
            }
            HandleAuthRequest(connectionId, packet);
            return;
        default:
            // 阶段9.2指令十一：未知 MessageId -> 关闭 Gateway link（不静默吞）
            it->second.connection->Close();
            m_gateways.erase(it);
            return;
    }
}

void LoginServer::HandleAuthRequest(std::uint64_t gatewayConnectionId, const Packet& packet) {
    auto it = m_gateways.find(gatewayConnectionId);
    if (it == m_gateways.end()) {
        return;
    }
    // 阶段9.2指令二/十六：DecodeGatewayLoginForward（Shared Protocol）
    GatewayLoginForwardPayload forward;
    std::string decodeError;
    if (!DecodeGatewayLoginForward(packet.payload.data(), packet.payload.size(), forward,
                                   decodeError)) {
        return; // 畸形请求：丢弃（Gateway 侧有 pending 超时兜底）
    }
    const std::uint64_t requestId = forward.requestId;
    const std::uint64_t clientConnectionId = forward.clientConnectionId;
    const std::string& username = forward.username;
    const std::string& token = forward.token;

    // 指令五十三 + 阶段9.2指令六：errorCode=None/InvalidCredentials（u64 accountId）
    const bool success = username == kTestUsername && token == kTestToken;
    const LoginGatewayResponsePayload response;
    LoginGatewayResponsePayload out;
    out.requestId = requestId;
    out.clientConnectionId = clientConnectionId;
    out.success = success;
    out.accountId = success ? static_cast<std::uint64_t>(kTestAccountId) : 0;
    out.displayName = success ? kTestDisplayName : "";
    out.errorCode = success ? static_cast<std::uint16_t>(LoginErrorCode::None)
                            : static_cast<std::uint16_t>(LoginErrorCode::InvalidCredentials);
    out.message = success ? "ok" : "invalid username or token";
    Packet responsePacket;
    responsePacket.header.messageId = static_cast<std::uint16_t>(MessageId::LoginGatewayResponse);
    if (!EncodeLoginGatewayResponse(out, responsePacket.payload)) {
        return;
    }
    it->second.connection->Send(responsePacket);
    if (m_hooks.onAuthResult) {
        m_hooks.onAuthResult(requestId, username, success, out.accountId);
    }
    (void)response;
}

} // namespace legend::login
