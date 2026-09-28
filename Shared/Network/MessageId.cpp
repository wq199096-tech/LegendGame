#include "Shared/Network/MessageId.h"

namespace legend::network {

const char* MessageIdName(std::uint16_t id) {
    switch (static_cast<MessageId>(id)) {
        case MessageId::None: return "None";
        case MessageId::ClientHello: return "ClientHello";
        case MessageId::ServerHello: return "ServerHello";
        case MessageId::LoginRequest: return "LoginRequest(LegacyDevLogin)";
        case MessageId::LoginResponse: return "LoginResponse";
        case MessageId::HeartbeatPing: return "HeartbeatPing";
        case MessageId::HeartbeatPong: return "HeartbeatPong";
        case MessageId::DisconnectNotice: return "DisconnectNotice";
        case MessageId::RegisterRequest: return "RegisterRequest";
        case MessageId::RegisterResponse: return "RegisterResponse";
        case MessageId::AccountLoginRequest: return "AccountLoginRequest";
        case MessageId::AccountLoginResponse: return "AccountLoginResponse";
        case MessageId::CharacterListRequest: return "CharacterListRequest";
        case MessageId::CharacterListResponse: return "CharacterListResponse";
        case MessageId::CharacterCreateRequest: return "CharacterCreateRequest";
        case MessageId::CharacterCreateResponse: return "CharacterCreateResponse";
        case MessageId::CharacterDeleteRequest: return "CharacterDeleteRequest";
        case MessageId::CharacterDeleteResponse: return "CharacterDeleteResponse";
        case MessageId::CharacterSelectRequest: return "CharacterSelectRequest";
        case MessageId::CharacterSelectResponse: return "CharacterSelectResponse";
        case MessageId::SessionResumeRequest: return "SessionResumeRequest";
        case MessageId::SessionResumeResponse: return "SessionResumeResponse";
        case MessageId::GatewayLoginForward: return "GatewayLoginForward";
        case MessageId::LoginGatewayResponse: return "LoginGatewayResponse";
        case MessageId::GatewayAccountForward: return "GatewayAccountForward";
        case MessageId::GatewayAccountResponse: return "GatewayAccountResponse";
        case MessageId::ConsumeSelectionTicketRequest: return "ConsumeSelectionTicketRequest";
        case MessageId::ConsumeSelectionTicketResponse: return "ConsumeSelectionTicketResponse";
        case MessageId::WorldClientHello: return "WorldClientHello";
        case MessageId::WorldServerHello: return "WorldServerHello";
        case MessageId::EnterWorldRequest: return "EnterWorldRequest";
        case MessageId::EnterWorldResponse: return "EnterWorldResponse";
        case MessageId::WorldDisconnectNotice: return "WorldDisconnectNotice";
        case MessageId::PlayerMoveInput: return "PlayerMoveInput";
        case MessageId::PlayerPositionSnapshot: return "PlayerPositionSnapshot";
        case MessageId::PlayerSpawn: return "PlayerSpawn";
        case MessageId::PlayerDespawn: return "PlayerDespawn";
        case MessageId::RemotePlayerSnapshot: return "RemotePlayerSnapshot";
        case MessageId::RemotePlayerBatchSnapshot: return "RemotePlayerBatchSnapshot";
        case MessageId::WorldErrorResponse: return "WorldErrorResponse";
        case MessageId::ErrorResponse: return "ErrorResponse";
    }
    return "Unknown";
}

} // namespace legend::network
