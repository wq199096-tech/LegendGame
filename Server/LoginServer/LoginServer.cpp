#include "Server/LoginServer/LoginServer.h"

#include "Engine/Debug/Logger.h"
#include "Server/LoginServer/Account/Database/DatabaseSchema.h"
#include "Shared/Account/AccountProtocol.h"
#include "Shared/Account/AccountTypes.h"
#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

#include <filesystem>

namespace legend::login {

using legend::account::AccountErrorCode;
using legend::account::AccountLoginRequestPayload;
using legend::account::AccountLoginResponsePayload;
using legend::account::CharacterCreateRequestPayload;
using legend::account::CharacterCreateResponsePayload;
using legend::account::CharacterDeleteRequestPayload;
using legend::account::CharacterDeleteResponsePayload;
using legend::account::CharacterListRequestPayload;
using legend::account::CharacterListResponsePayload;
using legend::account::CharacterSelectRequestPayload;
using legend::account::CharacterSelectResponsePayload;
using legend::account::RegisterRequestPayload;
using legend::account::RegisterResponsePayload;
using legend::account::SessionResumeRequestPayload;
using legend::account::SessionResumeResponsePayload;
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
namespace account = legend::account;

// 阶段10：Account payload 编解码（Shared/Account/AccountProtocol.h）
using account::EncodeAccountLoginResponse;
using account::EncodeCharacterCreateResponse;
using account::EncodeCharacterDeleteResponse;
using account::EncodeCharacterListResponse;
using account::EncodeCharacterSelectResponse;
using account::EncodeRegisterResponse;
using account::EncodeSessionResumeResponse;

namespace {
// 指令五十三：阶段9 临时认证（仅用于网络链路验收；LegacyDevLogin 兼容保留）
constexpr const char* kTestUsername = "test";
constexpr const char* kTestToken = "dev_token";
// 指令五十四：测试账号成功固定值（指令十二：accountId 全链 uint64）
constexpr std::uint64_t kTestAccountId = 1001;
constexpr const char* kTestDisplayName = "TestPlayer";
} // namespace

LoginServer::LoginServer(net::NetworkService& service)
    : m_service(service),
      m_server(std::make_shared<net::TcpServer>(service)),
      m_accountService(m_config.maxFailedLogins, m_config.lockoutSeconds) {}

bool LoginServer::Start(std::string& error) {
    // 阶段10 指令四：data 目录不存在则自动创建；数据库初始化自动建 Schema。
    std::error_code fsError;
    const std::filesystem::path dbPath(m_config.databasePath);
    if (dbPath.has_parent_path()) {
        std::filesystem::create_directories(dbPath.parent_path(), fsError);
    }
    if (!m_database.Open(m_config.databasePath, error)) {
        // 指令九十八：打开/损坏失败 -> 启动失败（不得静默另建覆盖）
        error = "database open failed: " + error;
        return false;
    }
    if (!account::InitializeSchema(m_database, error)) {
        error = "database schema init failed: " + error;
        m_database.Close();
        return false;
    }
    m_dbWorker.Start();
    if (!m_server->Listen(m_config.listenPort, error)) {
        m_dbWorker.Stop();
        m_database.Close();
        return false;
    }
    m_server->StartAccepting(
        [this](net::TcpConnectionPtr connection) { OnGatewayAccepted(std::move(connection)); });
    return true;
}

void LoginServer::Stop() {
    m_stopped.store(true);
    // 指令六十六：停接受 -> 断 Gateway -> DB Worker flush -> close SQLite。
    m_server->Stop();
    {
        std::lock_guard<std::mutex> lock(m_gatewaysMutex);
        m_gateways.clear();
    }
    m_dbWorker.Stop(); // 等待队列内任务执行完（结果 post 回 io 后由 m_stopped 丢弃）
    m_database.Close();
}

void LoginServer::OnGatewayAccepted(net::TcpConnectionPtr connection) {
    const std::uint64_t id = connection->Id();
    GatewayLink link;
    link.connection = connection;
    {
        std::lock_guard<std::mutex> lock(m_gatewaysMutex);
        m_gateways[id] = link; // 指令九十一：[Login] Gateway connected.
    }
    // 阶段10 UAF 修复：回调持 shared_from_this 保活——持有者销毁后挂起回调
    // 仍安全执行（对象存活至回调结束），不再出现悬空 this。
    auto self = shared_from_this();
    connection->Start(
        [self, id](const Packet& packet) { self->OnGatewayPacket(id, packet); },
        [self, id](std::uint64_t, const std::error_code&) { self->OnGatewayClosed(id); });
}

void LoginServer::OnGatewayClosed(std::uint64_t connectionId) {
    {
        std::lock_guard<std::mutex> lock(m_gatewaysMutex);
        m_gateways.erase(connectionId);
    }
    if (m_hooks.onGatewayConnectionChanged) {
        m_hooks.onGatewayConnectionChanged(false);
    }
}

void LoginServer::OnGatewayPacket(std::uint64_t connectionId, const Packet& packet) {
    std::shared_ptr<legend::net::TcpConnection> connection;
    bool handshakeDone = false;
    {
        std::lock_guard<std::mutex> lock(m_gatewaysMutex);
        auto it = m_gateways.find(connectionId);
        if (it == m_gateways.end()) {
            return;
        }
        connection = it->second.connection;
        handshakeDone = it->second.handshakeDone;
    }
    switch (static_cast<MessageId>(packet.header.messageId)) {
        case MessageId::ClientHello: {
            // 阶段9.3指令七/八：Shared Protocol 握手门——DecodeClientHello，
            // 禁止手写 ByteReader；重复 ClientHello（已握手）关闭
            if (handshakeDone) {
                connection->Close();
                std::lock_guard<std::mutex> lock(m_gatewaysMutex);
                m_gateways.erase(connectionId);
                return;
            }
            ClientHelloPayload hello;
            std::string decodeError;
            if (!DecodeClientHello(packet.payload.data(), packet.payload.size(), hello,
                                   decodeError)) {
                // 畸形 ClientHello -> 协议错误，关闭内部连接
                connection->Close();
                std::lock_guard<std::mutex> lock(m_gatewaysMutex);
                m_gateways.erase(connectionId);
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
                    connection->Send(out);
                }
                connection->CloseAfterFlush();
                std::lock_guard<std::mutex> lock(m_gatewaysMutex);
                m_gateways.erase(connectionId);
                return;
            }
            serverHello.accepted = true;
            serverHello.message = "welcome";
            Packet out;
            out.header.messageId = static_cast<std::uint16_t>(MessageId::ServerHello);
            if (!EncodeServerHello(serverHello, out.payload)) {
                connection->Close();
                std::lock_guard<std::mutex> lock(m_gatewaysMutex);
                m_gateways.erase(connectionId);
                return;
            }
            connection->Send(out);
            {
                std::lock_guard<std::mutex> lock(m_gatewaysMutex);
                auto it = m_gateways.find(connectionId);
                if (it != m_gateways.end()) {
                    it->second.handshakeDone = true;
                }
            }
            return;
        }
        case MessageId::GatewayLoginForward:
            // 阶段9.2指令十三：握手完成才允许 Forward；提前发 = Protocol Error 关链
            if (!handshakeDone) {
                connection->Close();
                std::lock_guard<std::mutex> lock(m_gatewaysMutex);
                m_gateways.erase(connectionId);
                return;
            }
            HandleAuthRequest(connectionId, packet);
            return;
        case MessageId::GatewayAccountForward:
            // 阶段10 指令五十四/五十五：账号链路统一入口
            if (!handshakeDone) {
                connection->Close();
                std::lock_guard<std::mutex> lock(m_gatewaysMutex);
                m_gateways.erase(connectionId);
                return;
            }
            HandleAccountForward(connectionId, packet);
            return;
        default:
            // 阶段9.2指令十一：未知 MessageId -> 关闭 Gateway link（不静默吞）
            connection->Close();
            std::lock_guard<std::mutex> lock(m_gatewaysMutex);
            m_gateways.erase(connectionId);
            return;
    }
}

void LoginServer::HandleAuthRequest(std::uint64_t gatewayConnectionId, const Packet& packet) {
    std::shared_ptr<legend::net::TcpConnection> connection;
    {
        std::lock_guard<std::mutex> lock(m_gatewaysMutex);
        auto it = m_gateways.find(gatewayConnectionId);
        if (it == m_gateways.end()) {
            return;
        }
        connection = it->second.connection;
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
    connection->Send(responsePacket);
    if (m_hooks.onAuthResult) {
        m_hooks.onAuthResult(requestId, username, success, out.accountId);
    }
}

// ---------------------------------------------------------------------------
// 阶段10：账号链路（指令六十三：Network 线程 -> DB task queue -> DB 线程 ->
// post 回 io_context -> 发送 Response；Client 断开时 Response 丢弃即可，指令一百）
// ---------------------------------------------------------------------------

void LoginServer::SendAccountResponse(std::uint64_t gatewayConnectionId, std::uint64_t requestId,
                                      std::uint64_t clientConnectionId,
                                      std::uint16_t innerMessageId,
                                      std::vector<std::uint8_t> innerPayload) {
    std::shared_ptr<legend::net::TcpConnection> connection;
    {
        std::lock_guard<std::mutex> lock(m_gatewaysMutex);
        auto it = m_gateways.find(gatewayConnectionId);
        if (it == m_gateways.end()) {
            return; // Gateway 已断开：丢弃
        }
        connection = it->second.connection;
    }
    legend::account::AccountEnvelope envelope;
    envelope.requestId = requestId;
    envelope.clientConnectionId = clientConnectionId;
    envelope.innerMessageId = innerMessageId;
    envelope.innerPayload = std::move(innerPayload);
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::GatewayAccountResponse);
    if (!legend::account::EncodeAccountEnvelope(envelope, out.payload)) {
        return;
    }
    connection->Send(out);
}

void LoginServer::HandleAccountForward(std::uint64_t gatewayConnectionId, const Packet& packet) {
    legend::account::AccountEnvelope envelope;
    std::string decodeError;
    if (!legend::account::DecodeAccountEnvelope(packet.payload.data(), packet.payload.size(),
                                                envelope, decodeError)) {
        // 指令九十二：畸形包只丢弃（连接保持），Gateway 侧有 pending 超时兜底
        LOG_WARN("[Login] Malformed account envelope from gateway #" +
                 std::to_string(gatewayConnectionId) + ": " + decodeError);
        return;
    }
    const std::uint64_t requestId = envelope.requestId;
    const std::uint64_t clientConnectionId = envelope.clientConnectionId;

    // DB Worker 任务闭包统一形态：捕获必要参数，执行 DB 操作，post 结果回 io。
    // 阶段10 UAF 修复：io 完成回调持 shared_from_this 保活（self），
    // 持有者销毁后回调仍安全（m_stopped -> 丢弃）。
    auto postDbTask = [this, self = shared_from_this(), gatewayConnectionId, requestId,
                       clientConnectionId](auto&& fn, std::uint16_t innerMessageId) {
        m_dbWorker.Post([self, gatewayConnectionId, requestId, clientConnectionId, innerMessageId,
                         fn]() mutable {
            auto payload = fn(); // DB 操作（在 DB 线程执行）
            self->m_service.Post([self, gatewayConnectionId, requestId, clientConnectionId,
                                  innerMessageId, payload = std::move(payload)]() mutable {
                if (self->m_stopped.load()) {
                    return; // 停机中：丢弃（指令一百）
                }
                self->SendAccountResponse(gatewayConnectionId, requestId, clientConnectionId,
                                          innerMessageId, std::move(payload));
            });
        });
    };

    switch (static_cast<MessageId>(envelope.innerMessageId)) {
        case MessageId::RegisterRequest: {
            RegisterRequestPayload request;
            if (!legend::account::DecodeRegisterRequest(
                    envelope.innerPayload.data(), envelope.innerPayload.size(), request,
                    decodeError)) {
                LOG_WARN("[Login] Malformed RegisterRequest dropped.");
                return;
            }
            LOG_INFO("[Login] Register request id=" + std::to_string(requestId) + " user=" +
                     request.username);
            postDbTask(
                [this, clientRequestId = request.requestId, username = request.username,
                 password = request.password]() {
                    RegisterResponsePayload response;
                    response.requestId = clientRequestId; // Client 侧 id 原样回传
                    auto created = m_accountService.Register(m_database, username, password);
                    response.success = created.success;
                    response.accountId = created.success ? created.value : 0;
                    response.errorCode = static_cast<std::uint16_t>(created.errorCode);
                    response.message = created.success ? "ok" : std::string(
                        legend::account::AccountErrorCodeName(static_cast<std::uint16_t>(created.errorCode)));
                    if (!created.success) {
                        // 指令六十八：SQLite 细节只进服务端日志
                        LOG_WARN("[Login] Register failed for user=" + username + " code=" +
                                 std::string(legend::account::AccountErrorCodeName(
                                     static_cast<std::uint16_t>(created.errorCode))) +
                                 " detail=" + created.errorMessage);
                    }
                    std::vector<std::uint8_t> payload;
                    EncodeRegisterResponse(response, payload);
                    if (m_hooks.onAccountResult) {
                        m_hooks.onAccountResult(
                            static_cast<std::uint16_t>(MessageId::RegisterResponse),
                            response.accountId, response.success);
                    }
                    return payload;
                },
                static_cast<std::uint16_t>(MessageId::RegisterResponse));
            return;
        }
        case MessageId::AccountLoginRequest: {
            AccountLoginRequestPayload request;
            if (!legend::account::DecodeAccountLoginRequest(
                    envelope.innerPayload.data(), envelope.innerPayload.size(), request,
                    decodeError)) {
                LOG_WARN("[Login] Malformed AccountLoginRequest dropped.");
                return;
            }
            LOG_INFO("[Login] Account login request id=" + std::to_string(requestId) + " user=" +
                     request.username);
            postDbTask(
                [this, clientRequestId = request.requestId, username = request.username,
                 password = request.password]() {
                    AccountLoginResponsePayload response;
                    response.requestId = clientRequestId; // Client 侧 id 原样回传
                    auto outcome =
                        m_accountService.Login(m_database, username, password,
                                               m_config.sessionTtlSeconds);
                    response.success = outcome.success;
                    response.accountId = outcome.success ? outcome.value.accountId : 0;
                    response.sessionToken = outcome.success ? outcome.value.sessionToken : "";
                    response.expiresAt = outcome.success ? outcome.value.sessionExpiresAt : 0;
                    response.errorCode = static_cast<std::uint16_t>(outcome.errorCode);
                    response.message = outcome.success
                                           ? "ok"
                                           : std::string(legend::account::AccountErrorCodeName(
                                                 static_cast<std::uint16_t>(outcome.errorCode)));
                    if (!outcome.success) {
                        // 指令六十九：日志绝不含 password / token
                        LOG_WARN("[Login] Login failed for user=" + username + " code=" +
                                 std::string(legend::account::AccountErrorCodeName(
                                     static_cast<std::uint16_t>(outcome.errorCode))) +
                                 " detail=" + outcome.errorMessage);
                    }
                    std::vector<std::uint8_t> payload;
                    EncodeAccountLoginResponse(response, payload);
                    if (m_hooks.onAccountResult) {
                        m_hooks.onAccountResult(
                            static_cast<std::uint16_t>(MessageId::AccountLoginResponse),
                            response.accountId, response.success);
                    }
                    return payload;
                },
                static_cast<std::uint16_t>(MessageId::AccountLoginResponse));
            return;
        }
        case MessageId::SessionResumeRequest: {
            SessionResumeRequestPayload request;
            if (!legend::account::DecodeSessionResumeRequest(
                    envelope.innerPayload.data(), envelope.innerPayload.size(), request,
                    decodeError)) {
                LOG_WARN("[Login] Malformed SessionResumeRequest dropped.");
                return;
            }
            postDbTask(
                [this, clientRequestId = request.requestId, token = request.sessionToken]() {
                    SessionResumeResponsePayload response;
                    response.requestId = clientRequestId; // Client 侧 id 原样回传
                    auto resumed = m_sessionService.Resume(m_database, token);
                    response.success = resumed.success;
                    response.accountId = resumed.success ? resumed.value.accountId : 0;
                    response.expiresAt = resumed.success ? resumed.value.expiresAt : 0;
                    response.errorCode = static_cast<std::uint16_t>(resumed.errorCode);
                    response.message = resumed.success ? "ok" : "session resume failed";
                    std::vector<std::uint8_t> payload;
                    EncodeSessionResumeResponse(response, payload);
                    if (m_hooks.onAccountResult) {
                        m_hooks.onAccountResult(
                            static_cast<std::uint16_t>(MessageId::SessionResumeResponse),
                            response.accountId, response.success);
                    }
                    return payload;
                },
                static_cast<std::uint16_t>(MessageId::SessionResumeResponse));
            return;
        }
        case MessageId::CharacterListRequest: {
            CharacterListRequestPayload request;
            if (!legend::account::DecodeCharacterListRequest(
                    envelope.innerPayload.data(), envelope.innerPayload.size(), request,
                    decodeError)) {
                LOG_WARN("[Login] Malformed CharacterListRequest dropped.");
                return;
            }
            postDbTask(
                [this, clientRequestId = request.requestId, token = request.sessionToken]() {
                    CharacterListResponsePayload response;
                    response.requestId = clientRequestId; // Client 侧 id 原样回传
                    auto resumed = m_sessionService.Resume(m_database, token);
                    if (!resumed.success) {
                        response.success = false;
                        response.errorCode = static_cast<std::uint16_t>(resumed.errorCode);
                        response.message = "session invalid";
                    } else {
                        auto characters = m_characterService.List(m_database, resumed.value.accountId);
                        response.success = characters.success;
                        response.errorCode = static_cast<std::uint16_t>(characters.errorCode);
                        response.message = characters.success ? "ok" : "character list failed";
                        if (characters.success) {
                            response.characters = std::move(characters.value);
                        }
                    }
                    std::vector<std::uint8_t> payload;
                    EncodeCharacterListResponse(response, payload);
                    if (m_hooks.onAccountResult) {
                        m_hooks.onAccountResult(
                            static_cast<std::uint16_t>(MessageId::CharacterListResponse),
                            0, response.success);
                    }
                    return payload;
                },
                static_cast<std::uint16_t>(MessageId::CharacterListResponse));
            return;
        }
        case MessageId::CharacterCreateRequest: {
            CharacterCreateRequestPayload request;
            if (!legend::account::DecodeCharacterCreateRequest(
                    envelope.innerPayload.data(), envelope.innerPayload.size(), request,
                    decodeError)) {
                LOG_WARN("[Login] Malformed CharacterCreateRequest dropped.");
                return;
            }
            LOG_INFO("[Login] Character create request id=" + std::to_string(requestId) +
                     " name=" + request.name + " class=" + std::to_string(request.classId));
            postDbTask(
                [this, clientRequestId = request.requestId, token = request.sessionToken,
                 name = request.name, classId = request.classId, gender = request.gender]() {
                    CharacterCreateResponsePayload response;
                    response.requestId = clientRequestId; // Client 侧 id 原样回传
                    auto resumed = m_sessionService.Resume(m_database, token);
                    if (!resumed.success) {
                        response.success = false;
                        response.errorCode = static_cast<std::uint16_t>(resumed.errorCode);
                        response.message = "session invalid";
                    } else {
                        auto created =
                            m_characterService.Create(m_database, resumed.value.accountId, name,
                                                      classId, gender);
                        response.success = created.success;
                        response.errorCode = static_cast<std::uint16_t>(created.errorCode);
                        response.message =
                            created.success ? "ok" : std::string(legend::account::AccountErrorCodeName(
                                                          static_cast<std::uint16_t>(created.errorCode)));
                        if (created.success) {
                            response.character = created.value;
                        } else {
                            LOG_WARN("[Login] Character create failed detail=" +
                                     created.errorMessage);
                        }
                    }
                    std::vector<std::uint8_t> payload;
                    EncodeCharacterCreateResponse(response, payload);
                    if (m_hooks.onAccountResult) {
                        m_hooks.onAccountResult(
                            static_cast<std::uint16_t>(MessageId::CharacterCreateResponse),
                            response.character.characterId, response.success);
                    }
                    return payload;
                },
                static_cast<std::uint16_t>(MessageId::CharacterCreateResponse));
            return;
        }
        case MessageId::CharacterDeleteRequest: {
            CharacterDeleteRequestPayload request;
            if (!legend::account::DecodeCharacterDeleteRequest(
                    envelope.innerPayload.data(), envelope.innerPayload.size(), request,
                    decodeError)) {
                LOG_WARN("[Login] Malformed CharacterDeleteRequest dropped.");
                return;
            }
            postDbTask(
                [this, clientRequestId = request.requestId, token = request.sessionToken,
                 characterId = request.characterId]() {
                    CharacterDeleteResponsePayload response;
                    response.requestId = clientRequestId; // Client 侧 id 原样回传
                    auto resumed = m_sessionService.Resume(m_database, token);
                    if (!resumed.success) {
                        response.success = false;
                        response.characterId = characterId;
                        response.errorCode = static_cast<std::uint16_t>(resumed.errorCode);
                        response.message = "session invalid";
                    } else {
                        auto deleted =
                            m_characterService.Delete(m_database, resumed.value.accountId, characterId);
                        response.success = deleted.success;
                        response.characterId = characterId;
                        response.errorCode = static_cast<std::uint16_t>(deleted.errorCode);
                        response.message =
                            deleted.success ? "ok" : std::string(legend::account::AccountErrorCodeName(
                                                          static_cast<std::uint16_t>(deleted.errorCode)));
                    }
                    std::vector<std::uint8_t> payload;
                    EncodeCharacterDeleteResponse(response, payload);
                    if (m_hooks.onAccountResult) {
                        m_hooks.onAccountResult(
                            static_cast<std::uint16_t>(MessageId::CharacterDeleteResponse),
                            characterId, response.success);
                    }
                    return payload;
                },
                static_cast<std::uint16_t>(MessageId::CharacterDeleteResponse));
            return;
        }
        case MessageId::CharacterSelectRequest: {
            CharacterSelectRequestPayload request;
            if (!legend::account::DecodeCharacterSelectRequest(
                    envelope.innerPayload.data(), envelope.innerPayload.size(), request,
                    decodeError)) {
                LOG_WARN("[Login] Malformed CharacterSelectRequest dropped.");
                return;
            }
            postDbTask(
                [this, clientRequestId = request.requestId, token = request.sessionToken,
                 characterId = request.characterId]() {
                    CharacterSelectResponsePayload response;
                    response.requestId = clientRequestId; // Client 侧 id 原样回传
                    auto resumed = m_sessionService.Resume(m_database, token);
                    if (!resumed.success) {
                        response.success = false;
                        response.errorCode = static_cast<std::uint16_t>(resumed.errorCode);
                        response.message = "session invalid";
                    } else {
                        auto selected =
                            m_characterService.Select(m_database, resumed.value.accountId, characterId);
                        response.success = selected.success;
                        response.errorCode = static_cast<std::uint16_t>(selected.errorCode);
                        response.message =
                            selected.success ? "ok" : std::string(legend::account::AccountErrorCodeName(
                                                           static_cast<std::uint16_t>(selected.errorCode)));
                        if (selected.success) {
                            response.character = selected.value;
                            // 阶段10 指令五十一：60 秒一次性 ticket（内存存储）
                            response.selectionTicket =
                                m_ticketStore.Create(resumed.value.accountId, characterId,
                                                     m_config.ticketTtlSeconds);
                        }
                    }
                    std::vector<std::uint8_t> payload;
                    EncodeCharacterSelectResponse(response, payload);
                    if (m_hooks.onAccountResult) {
                        m_hooks.onAccountResult(
                            static_cast<std::uint16_t>(MessageId::CharacterSelectResponse),
                            response.character.characterId, response.success);
                    }
                    return payload;
                },
                static_cast<std::uint16_t>(MessageId::CharacterSelectResponse));
            return;
        }
        default:
            // 指令九十二：未知内层消息 -> 丢弃（不断 Gateway 链）
            LOG_WARN("[Login] Unknown account inner message id " +
                     std::to_string(envelope.innerMessageId) + " dropped.");
            return;
    }
}

} // namespace legend::login
