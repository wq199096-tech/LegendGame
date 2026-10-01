#include "Server/Character/CharacterServer.h"

#include "Engine/Debug/Logger.h"
#include "Shared/Account/AccountError.h"
#include "Shared/Account/AccountProtocol.h"
#include "Shared/InternalProtocol/InternalProtocol.h"
#include "Shared/InternalProtocol/PersistenceMessages.h"
#include "Shared/Network/MessageId.h"
#include "Shared/Network/Protocol.h"
#include "Shared/World/WorldError.h"
#include "Shared/World/WorldProtocol.h"
#include "Shared/World/WorldTypes.h"

#include <chrono>

namespace legend::character {

using legend::account::AccountEnvelope;
using legend::account::AccountErrorCode;
using legend::internal::DbOperation;
using legend::internal::InternalErrorCode;
using legend::network::MessageId;
using legend::network::Packet;

namespace {
std::uint16_t AccountCode(InternalErrorCode code) {
    switch (code) {
        case InternalErrorCode::NotAuthenticated:
            return static_cast<std::uint16_t>(AccountErrorCode::SessionInvalid);
        case InternalErrorCode::CharacterNotFound:
            return static_cast<std::uint16_t>(AccountErrorCode::CharacterNotFound);
        case InternalErrorCode::CharacterOwnershipMismatch:
            return static_cast<std::uint16_t>(AccountErrorCode::CharacterNotOwned);
        case InternalErrorCode::DatabaseUnavailable: case InternalErrorCode::Timeout:
        case InternalErrorCode::ServiceUnavailable:
            return static_cast<std::uint16_t>(AccountErrorCode::ServiceUnavailable);
        default: return static_cast<std::uint16_t>(AccountErrorCode::InternalError);
    }
}
}

CharacterServer::CharacterServer(legend::net::NetworkService& service)
    : m_service(service), m_server(std::make_shared<legend::net::TcpServer>(service)) {}

bool CharacterServer::Start(std::string& error) {
    if (!m_stopped.exchange(false)) return true;
    legend::server::PersistenceClient::Config persistence;
    persistence.host = m_config.dbHost; persistence.port = m_config.dbPort;
    persistence.serviceType = legend::internal::ServiceType::CharacterServer;
    persistence.instanceId = "character-1"; persistence.serviceToken = m_config.serviceToken;
    persistence.requestTimeout = m_config.dbTimeout;
    m_persistence = std::make_shared<legend::server::PersistenceClient>(m_service, persistence);
    m_persistence->Start();
    legend::server::LogClient::Config logging;
    logging.host=m_config.logHost;logging.port=m_config.logPort;
    logging.serviceType=legend::internal::ServiceType::CharacterServer;
    logging.instanceId="character-1";logging.serviceToken=m_config.serviceToken;
    m_log=std::make_shared<legend::server::LogClient>(m_service,std::move(logging));m_log->Start();
    if (!m_server->Listen(m_config.listenPort, error)) {
        m_persistence->Stop(); m_stopped.store(true); return false;
    }
    m_server->StartAccepting([self = shared_from_this()](legend::net::TcpConnectionPtr connection) {
        self->OnAccepted(std::move(connection));
    });
    return true;
}

void CharacterServer::Stop() {
    if (m_stopped.exchange(true)) return;
    m_server->Stop();
    { std::lock_guard<std::mutex> lock(m_mutex); m_links.clear(); }
    if (m_persistence) m_persistence->Stop();
    if (m_log) m_log->Stop();
}

bool CharacterServer::IsPersistenceAvailable() const {
    return m_persistence && m_persistence->IsAvailable();
}
std::size_t CharacterServer::ConnectionCount() const {
    std::lock_guard<std::mutex> lock(m_mutex); return m_links.size();
}

void CharacterServer::OnAccepted(legend::net::TcpConnectionPtr connection) {
    const auto id = connection->Id();
    { std::lock_guard<std::mutex> lock(m_mutex); m_links[id] = {connection, false}; }
    auto self = shared_from_this();
    connection->Start([self, id](const Packet& packet) { self->OnPacket(id, packet); },
                      [self, id](std::uint64_t, const std::error_code&) { self->OnClosed(id); });
}
void CharacterServer::OnClosed(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex); m_links.erase(id);
}

void CharacterServer::OnPacket(std::uint64_t id, const Packet& packet) {
    bool authenticated = false;
    { std::lock_guard<std::mutex> lock(m_mutex); auto it = m_links.find(id);
      if (it == m_links.end()) return; authenticated = it->second.authenticated; }
    const auto message = static_cast<MessageId>(packet.header.messageId);
    if (!authenticated) {
        bool accepted = false;
        Packet response;
        if (message == MessageId::ClientHello) {
            legend::network::ClientHelloPayload hello; std::string error;
            if (legend::network::DecodeClientHello(packet.payload.data(), packet.payload.size(), hello, error)) {
                accepted = hello.protocolVersion == legend::network::kProtocolVersion &&
                    (hello.clientName == legend::world::kServiceNameGateway ||
                     hello.clientName == legend::world::kServiceNameWorldServer);
            }
            legend::network::ServerHelloPayload ack{accepted, legend::network::kProtocolVersion,
                id, "LegendCharacterServer", accepted ? "welcome" : "service rejected"};
            response.header.messageId = static_cast<std::uint16_t>(MessageId::ServerHello);
            legend::network::EncodeServerHello(ack, response.payload);
        } else if (message == MessageId::InternalServiceHandshake) {
            legend::internal::ServiceHandshake hello; std::string error;
            accepted = legend::internal::DecodeServiceHandshake(packet.payload.data(), packet.payload.size(), hello, error) &&
                (hello.serviceType == legend::internal::ServiceType::Gateway ||
                 hello.serviceType == legend::internal::ServiceType::WorldServer) &&
                (m_config.serviceToken.empty() || hello.serviceToken == m_config.serviceToken);
            legend::internal::ServiceHandshakeAck ack{accepted,
                accepted ? InternalErrorCode::Ok : InternalErrorCode::ProtocolMismatch,
                accepted ? "ready" : "service rejected"};
            response.header.messageId = static_cast<std::uint16_t>(MessageId::InternalServiceHandshakeAck);
            legend::internal::EncodeServiceHandshakeAck(ack, response.payload);
        } else { OnClosed(id); return; }
        Send(id, response);
        if (accepted) { std::lock_guard<std::mutex> lock(m_mutex); auto it = m_links.find(id);
            if (it != m_links.end()) it->second.authenticated = true; }
        return;
    }
    if (message == MessageId::GatewayAccountForward) {
        AccountEnvelope envelope; std::string error;
        if (legend::account::DecodeAccountEnvelope(packet.payload.data(), packet.payload.size(), envelope, error))
            HandleAccount(id, envelope);
        return;
    }
    if (message == MessageId::ConsumeSelectionTicketRequest) { HandleTicket(id, packet); return; }
    if (message == MessageId::InternalHeartbeat) return;
    OnClosed(id);
}

void CharacterServer::ValidateSession(const std::string& token, AccountCallback callback) {
    legend::internal::ValidateSessionRequest request{token}; std::vector<std::uint8_t> payload;
    legend::internal::EncodeValidateSessionRequest(request, payload);
    m_persistence->AsyncRequest(DbOperation::ValidateSession, std::move(payload),
        [callback = std::move(callback)](legend::internal::DbResponse response) mutable {
            if (response.errorCode != InternalErrorCode::Ok) {
                callback(false, 0, AccountCode(response.errorCode), response.message); return;
            }
            legend::internal::ValidateSessionResult result; std::string error;
            if (!legend::internal::DecodeValidateSessionResult(response.payload.data(), response.payload.size(), result, error)) {
                callback(false, 0, static_cast<std::uint16_t>(AccountErrorCode::InternalError), error); return;
            }
            callback(true, result.accountId, 0, "ok");
        });
}

void CharacterServer::HandleAccount(std::uint64_t linkId, const AccountEnvelope& envelope) {
    const auto message = static_cast<MessageId>(envelope.innerMessageId);
    auto self = shared_from_this(); std::string error;
    if (message == MessageId::CharacterListRequest) {
        legend::account::CharacterListRequestPayload request;
        if (!legend::account::DecodeCharacterListRequest(envelope.innerPayload.data(), envelope.innerPayload.size(), request, error)) return;
        ValidateSession(request.sessionToken, [self, linkId, envelope, request](bool ok, std::uint64_t accountId, std::uint16_t code, std::string message) mutable {
            if (!ok) { legend::account::CharacterListResponsePayload out{request.requestId, false, {}, code, std::move(message)};
                std::vector<std::uint8_t> bytes; legend::account::EncodeCharacterListResponse(out, bytes);
                self->SendAccount(linkId, envelope.requestId, envelope.clientConnectionId,
                    static_cast<std::uint16_t>(MessageId::CharacterListResponse), std::move(bytes)); return; }
            std::vector<std::uint8_t> payload; legend::internal::EncodeCharacterListQuery({accountId}, payload);
            self->m_persistence->AsyncRequest(DbOperation::LoadCharacterList, std::move(payload),
                [self, linkId, envelope, request](legend::internal::DbResponse db) mutable {
                    legend::account::CharacterListResponsePayload out; out.requestId = request.requestId;
                    out.success = db.errorCode == InternalErrorCode::Ok; out.errorCode = out.success ? 0 : AccountCode(db.errorCode);
                    out.message = out.success ? "ok" : db.message; std::string decode;
                    if (out.success && !legend::internal::DecodeCharacterList(db.payload.data(), db.payload.size(), out.characters, decode)) {
                        out.success = false; out.errorCode = static_cast<std::uint16_t>(AccountErrorCode::InternalError); out.message = decode; }
                    std::vector<std::uint8_t> bytes; legend::account::EncodeCharacterListResponse(out, bytes);
                    self->SendAccount(linkId, envelope.requestId, envelope.clientConnectionId,
                        static_cast<std::uint16_t>(MessageId::CharacterListResponse), std::move(bytes));
                });
        }); return;
    }
    if (message == MessageId::CharacterCreateRequest) {
        legend::account::CharacterCreateRequestPayload request;
        if (!legend::account::DecodeCharacterCreateRequest(envelope.innerPayload.data(), envelope.innerPayload.size(), request, error)) return;
        ValidateSession(request.sessionToken, [self, linkId, envelope, request](bool ok, std::uint64_t accountId, std::uint16_t code, std::string message) mutable {
            if (!ok) { legend::account::CharacterCreateResponsePayload out; out.requestId=request.requestId; out.errorCode=code; out.message=std::move(message);
                std::vector<std::uint8_t> bytes; legend::account::EncodeCharacterCreateResponse(out, bytes); self->SendAccount(linkId,envelope.requestId,envelope.clientConnectionId,static_cast<std::uint16_t>(MessageId::CharacterCreateResponse),std::move(bytes)); return; }
            std::vector<std::uint8_t> payload; legend::internal::EncodeCharacterCreateCommand({accountId,request.name,request.classId,request.gender},payload);
            self->m_persistence->AsyncRequest(DbOperation::CreateCharacter,std::move(payload),[self,linkId,envelope,request,accountId](legend::internal::DbResponse db) mutable {
                legend::account::CharacterCreateResponsePayload out; out.requestId=request.requestId; out.success=db.errorCode==InternalErrorCode::Ok; out.errorCode=out.success?0:AccountCode(db.errorCode); out.message=out.success?"ok":db.message; std::string decode;
                if(out.success&&!legend::internal::DecodeCharacterSummary(db.payload.data(),db.payload.size(),out.character,decode)){out.success=false;out.errorCode=static_cast<std::uint16_t>(AccountErrorCode::InternalError);out.message=decode;}if(out.success)self->Audit(legend::internal::LogEventType::CharacterCreate,accountId,out.character.characterId,"character created");
                std::vector<std::uint8_t> bytes; legend::account::EncodeCharacterCreateResponse(out,bytes); self->SendAccount(linkId,envelope.requestId,envelope.clientConnectionId,static_cast<std::uint16_t>(MessageId::CharacterCreateResponse),std::move(bytes)); });
        }); return;
    }
    if (message == MessageId::CharacterDeleteRequest) {
        legend::account::CharacterDeleteRequestPayload request;
        if (!legend::account::DecodeCharacterDeleteRequest(envelope.innerPayload.data(), envelope.innerPayload.size(), request, error)) return;
        ValidateSession(request.sessionToken,[self,linkId,envelope,request](bool ok,std::uint64_t accountId,std::uint16_t code,std::string message) mutable {
            if(!ok){legend::account::CharacterDeleteResponsePayload out{request.requestId,false,request.characterId,code,std::move(message)};std::vector<std::uint8_t> bytes;legend::account::EncodeCharacterDeleteResponse(out,bytes);self->SendAccount(linkId,envelope.requestId,envelope.clientConnectionId,static_cast<std::uint16_t>(MessageId::CharacterDeleteResponse),std::move(bytes));return;}
            std::vector<std::uint8_t> payload;legend::internal::EncodeCharacterCommand({accountId,request.characterId},payload);
            self->m_persistence->AsyncRequest(DbOperation::DeleteCharacter,std::move(payload),[self,linkId,envelope,request,accountId](legend::internal::DbResponse db) mutable {legend::account::CharacterDeleteResponsePayload out{request.requestId,db.errorCode==InternalErrorCode::Ok,request.characterId,static_cast<std::uint16_t>(db.errorCode==InternalErrorCode::Ok?0:AccountCode(db.errorCode)),db.errorCode==InternalErrorCode::Ok?"ok":db.message};if(out.success)self->Audit(legend::internal::LogEventType::CharacterDelete,accountId,request.characterId,"character deleted");std::vector<std::uint8_t> bytes;legend::account::EncodeCharacterDeleteResponse(out,bytes);self->SendAccount(linkId,envelope.requestId,envelope.clientConnectionId,static_cast<std::uint16_t>(MessageId::CharacterDeleteResponse),std::move(bytes));});
        }); return;
    }
    if (message == MessageId::CharacterSelectRequest) {
        legend::account::CharacterSelectRequestPayload request;
        if (!legend::account::DecodeCharacterSelectRequest(envelope.innerPayload.data(), envelope.innerPayload.size(), request, error)) return;
        ValidateSession(request.sessionToken,[self,linkId,envelope,request](bool ok,std::uint64_t accountId,std::uint16_t code,std::string message) mutable {
            if(!ok){legend::account::CharacterSelectResponsePayload out;out.requestId=request.requestId;out.errorCode=code;out.message=std::move(message);std::vector<std::uint8_t> bytes;legend::account::EncodeCharacterSelectResponse(out,bytes);self->SendAccount(linkId,envelope.requestId,envelope.clientConnectionId,static_cast<std::uint16_t>(MessageId::CharacterSelectResponse),std::move(bytes));return;}
            std::vector<std::uint8_t> payload;legend::internal::EncodeCharacterCommand({accountId,request.characterId},payload);
            self->m_persistence->AsyncRequest(DbOperation::LoadCharacterState,std::move(payload),[self,linkId,envelope,request,accountId](legend::internal::DbResponse db) mutable {legend::account::CharacterSelectResponsePayload out;out.requestId=request.requestId;out.success=db.errorCode==InternalErrorCode::Ok;out.errorCode=out.success?0:AccountCode(db.errorCode);out.message=out.success?"ok":db.message;std::string decode;if(out.success&&legend::internal::DecodeCharacterSummary(db.payload.data(),db.payload.size(),out.character,decode)){out.selectionTicket=self->m_tickets.Create(accountId,out.character.characterId,self->m_config.ticketTtlSeconds);self->Audit(legend::internal::LogEventType::CharacterSelect,accountId,out.character.characterId,"character selected");}else if(out.success){out.success=false;out.errorCode=static_cast<std::uint16_t>(AccountErrorCode::InternalError);out.message=decode;}std::vector<std::uint8_t> bytes;legend::account::EncodeCharacterSelectResponse(out,bytes);self->SendAccount(linkId,envelope.requestId,envelope.clientConnectionId,static_cast<std::uint16_t>(MessageId::CharacterSelectResponse),std::move(bytes));});
        }); return;
    }
}

void CharacterServer::HandleTicket(std::uint64_t id, const Packet& packet) {
    legend::world::ConsumeSelectionTicketRequestPayload request; std::string error;
    if(!legend::world::DecodeConsumeSelectionTicketRequest(packet.payload.data(),packet.payload.size(),request,error))return;
    const auto outcome=m_tickets.ConsumeForWorld(request.selectionTicket);
    legend::world::ConsumeSelectionTicketResponsePayload response; response.requestId=request.requestId;response.success=outcome.success;response.accountId=outcome.accountId;response.characterId=outcome.characterId;response.errorCode=outcome.success?static_cast<std::uint16_t>(legend::world::WorldErrorCode::None):static_cast<std::uint16_t>(legend::world::WorldErrorCode::InvalidTicket);response.message=outcome.success?"ok":"ticket invalid";
    Packet out;out.header.messageId=static_cast<std::uint16_t>(MessageId::ConsumeSelectionTicketResponse);if(legend::world::EncodeConsumeSelectionTicketResponse(response,out.payload))Send(id,out);
}

void CharacterServer::SendAccount(std::uint64_t id,std::uint64_t requestId,std::uint64_t clientId,std::uint16_t responseId,std::vector<std::uint8_t> payload){AccountEnvelope envelope{requestId,clientId,responseId,std::move(payload)};Packet packet;packet.header.messageId=static_cast<std::uint16_t>(MessageId::GatewayAccountResponse);if(legend::account::EncodeAccountEnvelope(envelope,packet.payload))Send(id,packet);}
void CharacterServer::Send(std::uint64_t id,const Packet& packet){legend::net::TcpConnectionPtr connection;{std::lock_guard<std::mutex>lock(m_mutex);auto it=m_links.find(id);if(it!=m_links.end())connection=it->second.connection;}if(connection)connection->Send(packet);}

void CharacterServer::Audit(legend::internal::LogEventType type,std::uint64_t accountId,std::uint64_t characterId,std::string message){if(!m_log)return;legend::internal::LogEvent event;event.timestampMs=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());event.service=legend::internal::ServiceType::CharacterServer;event.level=2;event.eventType=type;event.accountId=accountId;event.characterId=characterId;event.message=std::move(message);event.extraJson="{}";m_log->Emit(std::move(event));}

} // namespace legend::character
