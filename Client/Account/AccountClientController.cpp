#include "Client/Account/AccountClientController.h"

#include "Engine/Debug/Logger.h"

namespace legend::client {

const char* AccountFlowStateName(AccountFlowState state) {
    switch (state) {
        case AccountFlowState::Disconnected: return "Disconnected";
        case AccountFlowState::Connecting: return "Connecting";
        case AccountFlowState::Handshake: return "Handshake";
        case AccountFlowState::Unauthenticated: return "Unauthenticated";
        case AccountFlowState::Registering: return "Registering";
        case AccountFlowState::LoggingIn: return "LoggingIn";
        case AccountFlowState::Authenticated: return "Authenticated";
        case AccountFlowState::LoadingCharacters: return "LoadingCharacters";
        case AccountFlowState::CharacterListReady: return "CharacterListReady";
        case AccountFlowState::CreatingCharacter: return "CreatingCharacter";
        case AccountFlowState::DeletingCharacter: return "DeletingCharacter";
        case AccountFlowState::SelectingCharacter: return "SelectingCharacter";
        case AccountFlowState::CharacterSelected: return "CharacterSelected";
        case AccountFlowState::Failed: return "Failed";
    }
    return "Unknown";
}

AccountClientController::AccountClientController(GameNetworkClient& client) : m_client(client) {}

void AccountClientController::SetState(AccountFlowState state) {
    if (m_state != state) {
        LOG_DEBUG(std::string("[Account] state ") + AccountFlowStateName(m_state) + " -> " +
                  AccountFlowStateName(state));
        m_state = state;
    }
}

void AccountClientController::Fail(AccountFlowState backState, std::uint16_t errorCode,
                                   const std::string& message) {
    m_lastErrorCode = errorCode;
    m_lastError = message;
    SetState(backState);
}

std::uint64_t AccountClientController::NextRequestId() {
    return m_nextRequestId++;
}

// 阶段10 指令一百一十：未连接（Gateway/LoginServer 离线）时所有请求安全 no-op，
// 状态机不动（不得卡死/不得进入中间态）。
bool AccountClientController::CanSend() const {
    return m_client.State() == NetworkState::Ready && m_client.ServerConnectionId() != 0;
}

void AccountClientController::OnNetworkDisconnected() {
    // 阶段10 指令一百一十：Gateway/LoginServer 关闭 -> 状态 Disconnected，不卡死。
    // 保留 token：重连后可 SessionResume（指令三十三）。
    SetState(AccountFlowState::Disconnected);
}

void AccountClientController::SendRegister(const std::string& username,
                                           const std::string& password) {
    if (!CanSend()) {
        return;
    }
    legend::account::RegisterRequestPayload request;
    request.requestId = NextRequestId();
    request.username = username;
    request.password = password;
    m_lastRequestId = request.requestId;
    std::vector<std::uint8_t> payload;
    if (!legend::account::EncodeRegisterRequest(request, payload)) {
        return;
    }
    m_client.SendAccountPacket(static_cast<std::uint16_t>(legend::network::MessageId::RegisterRequest),
                               payload);
    SetState(AccountFlowState::Registering);
}

void AccountClientController::SendAccountLogin(const std::string& username,
                                               const std::string& password) {
    if (!CanSend()) {
        return;
    }
    legend::account::AccountLoginRequestPayload request;
    request.requestId = NextRequestId();
    request.username = username;
    request.password = password;
    m_lastRequestId = request.requestId;
    std::vector<std::uint8_t> payload;
    if (!legend::account::EncodeAccountLoginRequest(request, payload)) {
        return;
    }
    m_client.SendAccountPacket(
        static_cast<std::uint16_t>(legend::network::MessageId::AccountLoginRequest), payload);
    SetState(AccountFlowState::LoggingIn);
}

void AccountClientController::SendSessionResume(const std::string& token) {
    if (!CanSend()) {
        return;
    }
    legend::account::SessionResumeRequestPayload request;
    request.requestId = NextRequestId();
    request.sessionToken = token;
    m_lastRequestId = request.requestId;
    std::vector<std::uint8_t> payload;
    if (!legend::account::EncodeSessionResumeRequest(request, payload)) {
        return;
    }
    m_client.SendAccountPacket(
        static_cast<std::uint16_t>(legend::network::MessageId::SessionResumeRequest), payload);
    SetState(AccountFlowState::LoggingIn);
}

void AccountClientController::SendCharacterList(const std::string& token) {
    if (!CanSend()) {
        return;
    }
    legend::account::CharacterListRequestPayload request;
    request.requestId = NextRequestId();
    request.sessionToken = token;
    m_lastRequestId = request.requestId;
    std::vector<std::uint8_t> payload;
    if (!legend::account::EncodeCharacterListRequest(request, payload)) {
        return;
    }
    m_client.SendAccountPacket(
        static_cast<std::uint16_t>(legend::network::MessageId::CharacterListRequest), payload);
    SetState(AccountFlowState::LoadingCharacters);
}

void AccountClientController::SendCreateCharacter(const std::string& token, const std::string& name,
                                                  std::uint16_t classId, std::uint16_t gender) {
    if (!CanSend()) {
        return;
    }
    legend::account::CharacterCreateRequestPayload request;
    request.requestId = NextRequestId();
    request.sessionToken = token;
    request.name = name;
    request.classId = classId;
    request.gender = gender;
    m_lastRequestId = request.requestId;
    std::vector<std::uint8_t> payload;
    if (!legend::account::EncodeCharacterCreateRequest(request, payload)) {
        return;
    }
    m_client.SendAccountPacket(
        static_cast<std::uint16_t>(legend::network::MessageId::CharacterCreateRequest), payload);
    SetState(AccountFlowState::CreatingCharacter);
}

void AccountClientController::SendDeleteCharacter(const std::string& token,
                                                  std::uint64_t characterId) {
    if (!CanSend()) {
        return;
    }
    legend::account::CharacterDeleteRequestPayload request;
    request.requestId = NextRequestId();
    request.sessionToken = token;
    request.characterId = characterId;
    m_lastRequestId = request.requestId;
    std::vector<std::uint8_t> payload;
    if (!legend::account::EncodeCharacterDeleteRequest(request, payload)) {
        return;
    }
    m_client.SendAccountPacket(
        static_cast<std::uint16_t>(legend::network::MessageId::CharacterDeleteRequest), payload);
    SetState(AccountFlowState::DeletingCharacter);
}

void AccountClientController::SendSelectCharacter(const std::string& token,
                                                  std::uint64_t characterId) {
    if (!CanSend()) {
        return;
    }
    legend::account::CharacterSelectRequestPayload request;
    request.requestId = NextRequestId();
    request.sessionToken = token;
    request.characterId = characterId;
    m_lastRequestId = request.requestId;
    std::vector<std::uint8_t> payload;
    if (!legend::account::EncodeCharacterSelectRequest(request, payload)) {
        return;
    }
    m_client.SendAccountPacket(
        static_cast<std::uint16_t>(legend::network::MessageId::CharacterSelectRequest), payload);
    SetState(AccountFlowState::SelectingCharacter);
}

void AccountClientController::HandleEvent(const NetworkEvent& event) {
    switch (event.type) {
        case NetworkEvent::Type::RegisterResponse: {
            // 指令二十四：注册失败不踢线，可重试（保持 Unauthenticated）
            if (event.success) {
                m_accountId = event.accountId;
                m_lastErrorCode = 0;
                m_lastError.clear();
                SetState(AccountFlowState::Unauthenticated);
                LOG_INFO("[Account] Register success account=" + std::to_string(event.accountId));
            } else {
                Fail(AccountFlowState::Unauthenticated, event.errorCode, event.message);
                LOG_WARN("[Account] Register failed: " + event.message);
            }
            return;
        }
        case NetworkEvent::Type::AccountLoginResponse: {
            if (event.success) {
                m_accountId = event.accountId;
                m_sessionToken = event.sessionToken;
                m_sessionExpiresAt = event.expiresAt;
                m_lastErrorCode = 0;
                m_lastError.clear();
                SetState(AccountFlowState::Authenticated);
                // 指令六十九：日志只含 accountId，绝不含 token
                LOG_INFO("[Account] Login success account=" + std::to_string(event.accountId));
            } else {
                Fail(AccountFlowState::Unauthenticated, event.errorCode, event.message);
                LOG_WARN("[Account] Login failed: " + event.message);
            }
            return;
        }
        case NetworkEvent::Type::SessionResumeResponse: {
            if (event.success) {
                m_accountId = event.accountId;
                m_sessionExpiresAt = event.expiresAt;
                m_lastErrorCode = 0;
                m_lastError.clear();
                SetState(AccountFlowState::Authenticated);
                LOG_INFO("[Account] Session resume success account=" +
                         std::to_string(event.accountId));
            } else {
                // 指令七十九：过期/无效 -> 丢弃旧 token，重新登录
                m_sessionToken.clear();
                Fail(AccountFlowState::Unauthenticated, event.errorCode, event.message);
                LOG_WARN("[Account] Session resume failed: " + event.message);
            }
            return;
        }
        case NetworkEvent::Type::CharacterListResponse: {
            if (event.success) {
                m_characters = event.characters;
                m_lastErrorCode = 0;
                m_lastError.clear();
                SetState(AccountFlowState::CharacterListReady);
            } else {
                Fail(AccountFlowState::Authenticated, event.errorCode, event.message);
            }
            return;
        }
        case NetworkEvent::Type::CharacterCreateResponse: {
            if (event.success) {
                m_characters.push_back(event.character);
                m_lastErrorCode = 0;
                m_lastError.clear();
                SetState(AccountFlowState::CharacterListReady);
                LOG_INFO("[Account] Character created id=" +
                         std::to_string(event.character.characterId) + " name=" +
                         event.character.name);
            } else {
                Fail(AccountFlowState::CharacterListReady, event.errorCode, event.message);
            }
            return;
        }
        case NetworkEvent::Type::CharacterDeleteResponse: {
            if (event.success) {
                // 阶段10 指令四十五：soft delete -> 列表移除
                for (auto it = m_characters.begin(); it != m_characters.end(); ++it) {
                    if (it->characterId == event.characterId) {
                        m_characters.erase(it);
                        break;
                    }
                }
                if (m_selectedCharacterId == event.characterId) {
                    m_selectedCharacterId = 0;
                    m_selectedCharacter = {};
                    m_selectionTicket.clear();
                }
                m_lastErrorCode = 0;
                m_lastError.clear();
                SetState(AccountFlowState::CharacterListReady);
            } else {
                Fail(AccountFlowState::CharacterListReady, event.errorCode, event.message);
            }
            return;
        }
        case NetworkEvent::Type::CharacterSelectResponse: {
            if (event.success) {
                m_selectedCharacter = event.character;
                m_selectedCharacterId = event.character.characterId;
                m_selectionTicket = event.selectionTicket;
                m_lastErrorCode = 0;
                m_lastError.clear();
                SetState(AccountFlowState::CharacterSelected);
                // 阶段10 指令一百一十二：只进入 CharacterSelected，不进 WorldServer
                LOG_INFO("[Account] Character selected id=" +
                         std::to_string(event.character.characterId));
            } else {
                Fail(AccountFlowState::CharacterListReady, event.errorCode, event.message);
            }
            return;
        }
        default:
            return;
    }
}

} // namespace legend::client
