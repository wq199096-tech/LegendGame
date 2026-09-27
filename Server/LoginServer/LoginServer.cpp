#include "Server/LoginServer/LoginServer.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"
#include "Shared/Network/MessageId.h"
#include "Shared/Network/Protocol.h"

namespace legend::login {

using legend::network::ClientHelloPayload;
using legend::network::DecodeClientHello;
using legend::network::DecodeGatewayLoginForward;
using legend::network::EncodeLoginGatewayResponse;
using legend::network::EncodeServerHello;
using legend::network::GatewayLoginForwardPayload;
using legend::network::kProtocolVersion;
using legend::network::LoginErrorCode;
using legend::network::LoginGatewayResponsePayload;
using legend::network::MessageId;
using legend::network::Packet;
using legend::network::ServerHelloPayload;
namespace net = legend::net;

namespace {
// 指令五十三：阶段9 临时认证（仅用于网络链路验收；后续阶段接数据库）
constexpr const char* kTestUsername = "test";
constexpr const char* kTestToken = "dev_token";
// 指令五十四：测试账号成功固定值（指令十二：accountId 全链 uint64）
constexpr std::uint64_t kTestAccountId = 1001;
constexpr const char* kTestDisplayName = "TestPlayer";
} // namespace

LoginServer::LoginServer(net::NetworkService& service)
    : m_service(service),
      m_server(std::make_shared<net::TcpServer>(service)) {}

bool LoginServer::Start(std::string& error) {
    if (!m_server->Listen(m_config.listenPort, error)) {
        return false;
    }
    m_server->StartAccepting(
        [this](net::TcpConnectionPtr connection) { OnGatewayAccepted(std::move(connection)); });
    return true;
}

void LoginServer::Stop() {
    m_stopped.store(true);
    m_server->Stop();
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
            // 阶段9.3指令七/八：Shared Protocol 握手门——DecodeClientHello，
            // 禁止手写 ByteReader；重复 ClientHello（已握手）关闭
            if (it->second.handshakeDone) {
                it->second.connection->Close();
                m_gateways.erase(it);
                return;
            }
            ClientHelloPayload hello;
            std::string decodeError;
            if (!DecodeClientHello(packet.payload.data(), packet.payload.size(), hello,
                                   decodeError)) {
                // 畸形 ClientHello -> 协议错误，关闭内部连接
                it->second.connection->Close();
                m_gateways.erase(it);
                return;
            }
            // 指令八：构造 ServerHelloPayload + EncodeServerHello
            ServerHelloPayload serverHello;
            serverHello.protocolVersion = kProtocolVersion;
            serverHello.connectionId = connectionId;
            serverHello.serverName = "LegendLoginServer";
            if (hello.protocolVersion != kProtocolVersion) {
                // 指令八：版本错误 -> ServerHello accepted=false + CloseAfterFlush
                serverHello.accepted = false;
                serverHello.message = "protocol version mismatch";
                Packet out;
                out.header.messageId = static_cast<std::uint16_t>(MessageId::ServerHello);
                if (EncodeServerHello(serverHello, out.payload)) {
                    it->second.connection->Send(out);
                }
                it->second.connection->CloseAfterFlush();
                m_gateways.erase(it);
                return;
            }
            serverHello.accepted = true;
            serverHello.message = "welcome";
            Packet out;
            out.header.messageId = static_cast<std::uint16_t>(MessageId::ServerHello);
            if (!EncodeServerHello(serverHello, out.payload)) {
                it->second.connection->Close();
                m_gateways.erase(it);
                return;
            }
            it->second.connection->Send(out);
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
    LoginGatewayResponsePayload out;
    out.requestId = requestId;
    out.clientConnectionId = clientConnectionId;
    out.success = success;
    out.accountId = success ? kTestAccountId : 0;
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
}

} // namespace legend::login
