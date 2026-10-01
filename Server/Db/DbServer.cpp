#include "Server/Db/DbServer.h"

#include "Engine/Debug/Logger.h"
#include "Server/LoginServer/Account/Database/DatabaseSchema.h"
#include "Shared/InternalProtocol/PersistenceMessages.h"
#include "Shared/Network/MessageId.h"

#include <filesystem>

namespace legend::db {

using namespace legend::internal;
using legend::network::MessageId;
using legend::network::Packet;

namespace {
InternalErrorCode MapAccountError(legend::account::AccountErrorCode code) {
    using A = legend::account::AccountErrorCode;
    switch (code) {
        case A::CharacterNotFound: return InternalErrorCode::CharacterNotFound;
        case A::CharacterNotOwned: return InternalErrorCode::CharacterOwnershipMismatch;
        case A::SessionInvalid: return InternalErrorCode::NotAuthenticated;
        case A::DatabaseError: return InternalErrorCode::DatabaseUnavailable;
        default: return InternalErrorCode::InvalidRequest;
    }
}
}

DbServer::DbServer(legend::net::NetworkService& service)
    : m_service(service), m_server(std::make_shared<legend::net::TcpServer>(service)) {}

bool DbServer::Start(std::string& error) {
    if (!m_stopped.exchange(false)) return true;
    const std::filesystem::path path(m_config.databasePath);
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    if (!m_database.Open(m_config.databasePath, error)) {
        m_stopped.store(true); return false;
    }
    if (!legend::account::InitializeSchema(m_database, error)) {
        m_database.Close(); m_stopped.store(true); return false;
    }
    m_worker.Start();
    if (!m_server->Listen(m_config.listenPort, error)) {
        m_worker.Stop(); m_database.Close(); m_stopped.store(true); return false;
    }
    m_server->StartAccepting([self = shared_from_this()](legend::net::TcpConnectionPtr connection) {
        self->OnAccepted(std::move(connection));
    });
    m_health.store(ServiceHealth::Healthy);
    return true;
}

void DbServer::Stop() {
    if (m_stopped.exchange(true)) return;
    m_health.store(ServiceHealth::Unavailable);
    m_server->Stop();
    { std::lock_guard<std::mutex> lock(m_mutex); m_links.clear(); }
    m_worker.Stop();
    m_database.Close();
}

std::size_t DbServer::ConnectionCount() const {
    std::lock_guard<std::mutex> lock(m_mutex); return m_links.size();
}

void DbServer::OnAccepted(legend::net::TcpConnectionPtr connection) {
    const auto id = connection->Id();
    { std::lock_guard<std::mutex> lock(m_mutex); m_links[id] = {connection, false}; }
    auto self = shared_from_this();
    connection->Start([self, id](const Packet& packet) { self->OnPacket(id, packet); },
                      [self, id](std::uint64_t, const std::error_code&) { self->OnClosed(id); });
}

void DbServer::OnClosed(std::uint64_t connectionId) {
    std::lock_guard<std::mutex> lock(m_mutex); m_links.erase(connectionId);
}

void DbServer::OnPacket(std::uint64_t connectionId, const Packet& packet) {
    bool authenticated = false;
    { std::lock_guard<std::mutex> lock(m_mutex); const auto it = m_links.find(connectionId);
      if (it == m_links.end()) return; authenticated = it->second.authenticated; }
    const auto id = static_cast<MessageId>(packet.header.messageId);
    if (!authenticated) {
        if (id != MessageId::InternalServiceHandshake) { OnClosed(connectionId); return; }
        ServiceHandshake handshake; std::string error;
        ServiceHandshakeAck ack;
        const bool decoded = DecodeServiceHandshake(packet.payload.data(), packet.payload.size(), handshake, error);
        const bool allowed = decoded && (handshake.serviceType == ServiceType::LoginServer ||
            handshake.serviceType == ServiceType::CharacterServer ||
            handshake.serviceType == ServiceType::WorldServer) &&
            (m_config.serviceToken.empty() || handshake.serviceToken == m_config.serviceToken);
        ack.accepted = allowed;
        ack.errorCode = allowed ? InternalErrorCode::Ok : InternalErrorCode::ProtocolMismatch;
        ack.message = allowed ? "ready" : "service identity rejected";
        Packet response; response.header.messageId = static_cast<std::uint16_t>(MessageId::InternalServiceHandshakeAck);
        EncodeServiceHandshakeAck(ack, response.payload); Send(connectionId, response);
        if (allowed) { std::lock_guard<std::mutex> lock(m_mutex); auto it = m_links.find(connectionId);
            if (it != m_links.end()) it->second.authenticated = true; }
        return;
    }
    if (id == MessageId::InternalHeartbeat) {
        Heartbeat heartbeat; std::string error;
        if (!DecodeHeartbeat(packet.payload.data(), packet.payload.size(), heartbeat, error)) OnClosed(connectionId);
        else Send(connectionId, packet);
        return;
    }
    if (id != MessageId::InternalDbRequest) { OnClosed(connectionId); return; }
    DbRequest request; std::string error;
    if (!DecodeDbRequest(packet.payload.data(), packet.payload.size(), request, error)) {
        OnClosed(connectionId); return;
    }
    HandleRequest(connectionId, std::move(request));
}

void DbServer::HandleRequest(std::uint64_t connectionId, DbRequest request) {
    auto self = shared_from_this();
    m_worker.Post([self, connectionId, request = std::move(request)]() mutable {
        auto response = self->Execute(request);
        self->m_service.Post([self, connectionId, response = std::move(response)]() mutable {
            Packet packet; packet.header.messageId = static_cast<std::uint16_t>(MessageId::InternalDbResponse);
            if (EncodeDbResponse(response, packet.payload)) self->Send(connectionId, packet);
        });
    });
}

DbResponse DbServer::Execute(const DbRequest& request) {
    DbResponse response; response.requestId = request.requestId;
    response.recordVersion = m_revision.load();
    std::string error;
    if (request.expectedVersion != 0 && request.expectedVersion != response.recordVersion) {
        response.errorCode = InternalErrorCode::PersistenceConflict;
        response.message = "record version conflict";
        return response;
    }
    switch (request.operation) {
        case DbOperation::SaveAccount: {
            AccountCredentials command;
            if (!DecodeAccountCredentials(request.payload.data(), request.payload.size(), command, error)) break;
            auto result = m_accountService.Register(m_database, command.username, command.password);
            if (!result.success) { response.errorCode = MapAccountError(result.errorCode);
                response.message = result.errorMessage; return response; }
            EncodeAccountRegisterResult({result.value}, response.payload);
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::LoadAccount: {
            AccountCredentials query;
            if (!DecodeAccountCredentials(request.payload.data(), request.payload.size(), query, error)) break;
            auto result = m_accountService.Login(m_database, query.username, query.password, 24 * 60 * 60);
            if (!result.success) { response.errorCode = MapAccountError(result.errorCode);
                response.message = result.errorMessage; return response; }
            EncodeAccountLoginResult({result.value.accountId, result.value.sessionToken,
                                      result.value.sessionExpiresAt}, response.payload);
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::ValidateSession: {
            ValidateSessionRequest query;
            if (!DecodeValidateSessionRequest(request.payload.data(), request.payload.size(), query, error)) break;
            auto result = m_sessionService.Resume(m_database, query.sessionToken);
            if (!result.success) { response.errorCode = MapAccountError(result.errorCode);
                response.message = result.errorMessage; return response; }
            EncodeValidateSessionResult({result.value.accountId, result.value.expiresAt}, response.payload);
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::LoadCharacterList: {
            CharacterListQuery query;
            if (!DecodeCharacterListQuery(request.payload.data(), request.payload.size(), query, error)) break;
            auto result = m_characterService.List(m_database, query.accountId);
            if (!result.success) { response.errorCode = MapAccountError(result.errorCode);
                response.message = result.errorMessage; return response; }
            EncodeCharacterList(result.value, response.payload);
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::CreateCharacter: {
            CharacterCreateCommand command;
            if (!DecodeCharacterCreateCommand(request.payload.data(), request.payload.size(), command, error)) break;
            auto result = m_characterService.Create(m_database, command.accountId, command.name,
                                                    command.classId, command.gender);
            if (!result.success) { response.errorCode = MapAccountError(result.errorCode);
                response.message = result.errorMessage; return response; }
            EncodeCharacterSummary(result.value, response.payload);
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::DeleteCharacter: {
            CharacterCommand command;
            if (!DecodeCharacterCommand(request.payload.data(), request.payload.size(), command, error)) break;
            auto result = m_characterService.Delete(m_database, command.accountId, command.characterId);
            if (!result.success) { response.errorCode = MapAccountError(result.errorCode);
                response.message = result.errorMessage; return response; }
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::LoadCharacterState: {
            CharacterCommand command;
            if (!DecodeCharacterCommand(request.payload.data(), request.payload.size(), command, error)) break;
            auto result = m_characterService.Select(m_database, command.accountId, command.characterId);
            if (!result.success) { response.errorCode = MapAccountError(result.errorCode);
                response.message = result.errorMessage; return response; }
            EncodeCharacterSummary(result.value, response.payload);
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        default:
            response.errorCode = InternalErrorCode::InvalidRequest;
            response.message = "database operation not implemented";
            return response;
    }
    response.errorCode = InternalErrorCode::InvalidRequest;
    response.message = error.empty() ? "malformed operation payload" : error;
    return response;
}

void DbServer::Send(std::uint64_t connectionId, const Packet& packet) {
    legend::net::TcpConnectionPtr connection;
    { std::lock_guard<std::mutex> lock(m_mutex); const auto it = m_links.find(connectionId);
      if (it != m_links.end()) connection = it->second.connection; }
    if (connection) connection->Send(packet);
}

} // namespace legend::db
