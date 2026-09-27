#pragma once

#include <cstdint>

namespace legend::network {

// 阶段9 指令十二：MessageId 枚举（阶段9 最小集合，编号可扩展）。
enum class MessageId : std::uint16_t {
    None = 0,
    ClientHello = 1,
    ServerHello = 2,
    LoginRequest = 10,
    LoginResponse = 11,
    HeartbeatPing = 20,
    HeartbeatPong = 21,
    DisconnectNotice = 30,
    GatewayLoginForward = 100,
    LoginGatewayResponse = 101,
    ErrorResponse = 255,
};

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
        case MessageId::GatewayLoginForward:
        case MessageId::LoginGatewayResponse:
        case MessageId::ErrorResponse:
            return true;
        case MessageId::None:
            return false;
    }
    return false;
}

const char* MessageIdName(std::uint16_t id);

} // namespace legend::network
