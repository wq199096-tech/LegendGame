#pragma once

#include <cstdint>

namespace legend::network {

// 阶段9 指令十二：MessageId 枚举（阶段9 最小集合 + 阶段10 Account 集合）。
enum class MessageId : std::uint16_t {
    None = 0,
    ClientHello = 1,
    ServerHello = 2,
    // 阶段9：LegacyDevLogin（阶段10 保留兼容，正式流程改用 AccountLoginRequest）
    LoginRequest = 10,
    LoginResponse = 11,
    HeartbeatPing = 20,
    HeartbeatPong = 21,
    DisconnectNotice = 30,
    // 阶段10 指令十九：Account / Character
    RegisterRequest = 40,
    RegisterResponse = 41,
    AccountLoginRequest = 42,
    AccountLoginResponse = 43,
    CharacterListRequest = 50,
    CharacterListResponse = 51,
    CharacterCreateRequest = 52,
    CharacterCreateResponse = 53,
    CharacterDeleteRequest = 54,
    CharacterDeleteResponse = 55,
    CharacterSelectRequest = 56,
    CharacterSelectResponse = 57,
    SessionResumeRequest = 60,
    SessionResumeResponse = 61,
    // 阶段9 内部（Gateway <-> LoginServer）
    GatewayLoginForward = 100,
    LoginGatewayResponse = 101,
    // 阶段10 内部（Gateway <-> LoginServer 账号信封，Client 不感知）
    GatewayAccountForward = 102,
    GatewayAccountResponse = 103,
    ErrorResponse = 255,
};

// 阶段10 指令五十四：Gateway 只转发 Account 消息，不解析业务 payload。
inline bool IsAccountMessageId(std::uint16_t id) {
    switch (static_cast<MessageId>(id)) {
        case MessageId::RegisterRequest:
        case MessageId::AccountLoginRequest:
        case MessageId::CharacterListRequest:
        case MessageId::CharacterCreateRequest:
        case MessageId::CharacterDeleteRequest:
        case MessageId::CharacterSelectRequest:
        case MessageId::SessionResumeRequest:
            return true;
        default:
            return false;
    }
}

// 阶段10 指令一百零二：写请求（同 Session 未完成时拒绝重复）。
inline bool IsAccountWriteMessageId(std::uint16_t id) {
    switch (static_cast<MessageId>(id)) {
        case MessageId::RegisterRequest:
        case MessageId::AccountLoginRequest:
        case MessageId::CharacterCreateRequest:
        case MessageId::CharacterDeleteRequest:
        case MessageId::CharacterSelectRequest:
        case MessageId::SessionResumeRequest:
            return true;
        default:
            return false;
    }
}

// Account 请求 -> 对应 Response MessageId（Gateway 超时合成错误响应用）。
inline std::uint16_t AccountResponseMessageId(std::uint16_t requestId) {
    switch (static_cast<MessageId>(requestId)) {
        case MessageId::RegisterRequest: return static_cast<std::uint16_t>(MessageId::RegisterResponse);
        case MessageId::AccountLoginRequest: return static_cast<std::uint16_t>(MessageId::AccountLoginResponse);
        case MessageId::CharacterListRequest: return static_cast<std::uint16_t>(MessageId::CharacterListResponse);
        case MessageId::CharacterCreateRequest: return static_cast<std::uint16_t>(MessageId::CharacterCreateResponse);
        case MessageId::CharacterDeleteRequest: return static_cast<std::uint16_t>(MessageId::CharacterDeleteResponse);
        case MessageId::CharacterSelectRequest: return static_cast<std::uint16_t>(MessageId::CharacterSelectResponse);
        case MessageId::SessionResumeRequest: return static_cast<std::uint16_t>(MessageId::SessionResumeResponse);
        default: return 0;
    }
}

// 阶段9 指令七十：未知 messageId 视为 Protocol Error（不要静默忽略）。
inline bool IsValidMessageId(std::uint16_t id) {
    switch (static_cast<MessageId>(id)) {
        case MessageId::ClientHello:
        case MessageId::ServerHello:
        case MessageId::LoginRequest:
        case MessageId::LoginResponse:
        case MessageId::HeartbeatPing:
        case MessageId::HeartbeatPong:
        case MessageId::DisconnectNotice:
        case MessageId::RegisterRequest:
        case MessageId::RegisterResponse:
        case MessageId::AccountLoginRequest:
        case MessageId::AccountLoginResponse:
        case MessageId::CharacterListRequest:
        case MessageId::CharacterListResponse:
        case MessageId::CharacterCreateRequest:
        case MessageId::CharacterCreateResponse:
        case MessageId::CharacterDeleteRequest:
        case MessageId::CharacterDeleteResponse:
        case MessageId::CharacterSelectRequest:
        case MessageId::CharacterSelectResponse:
        case MessageId::SessionResumeRequest:
        case MessageId::SessionResumeResponse:
        case MessageId::GatewayLoginForward:
        case MessageId::LoginGatewayResponse:
        case MessageId::GatewayAccountForward:
        case MessageId::GatewayAccountResponse:
        case MessageId::ErrorResponse:
            return true;
        case MessageId::None:
            return false;
    }
    return false;
}

const char* MessageIdName(std::uint16_t id);

} // namespace legend::network
