#pragma once

#include <cstdint>

namespace legend::world {

// 阶段11 指令六十六：World 错误码（uint16 wire）。
// Ticket 类错误（InvalidTicket/ExpiredTicket/ConsumedTicket）对 Client 统一
// 折叠为 InvalidTicket（指令六十七），精确原因只进服务端日志。
enum class WorldErrorCode : std::uint16_t {
    None = 0,
    InvalidTicket = 1,
    ExpiredTicket = 2,
    ConsumedTicket = 3,
    CharacterNotFound = 4,
    CharacterAlreadyOnline = 5,
    DatabaseError = 6,
    ServiceUnavailable = 7,
    ProtocolError = 8,
    InternalError = 9,
};

inline const char* WorldErrorCodeName(std::uint16_t code) {
    switch (static_cast<WorldErrorCode>(code)) {
        case WorldErrorCode::None: return "None";
        case WorldErrorCode::InvalidTicket: return "InvalidTicket";
        case WorldErrorCode::ExpiredTicket: return "ExpiredTicket";
        case WorldErrorCode::ConsumedTicket: return "ConsumedTicket";
        case WorldErrorCode::CharacterNotFound: return "CharacterNotFound";
        case WorldErrorCode::CharacterAlreadyOnline: return "CharacterAlreadyOnline";
        case WorldErrorCode::DatabaseError: return "DatabaseError";
        case WorldErrorCode::ServiceUnavailable: return "ServiceUnavailable";
        case WorldErrorCode::ProtocolError: return "ProtocolError";
        case WorldErrorCode::InternalError: return "InternalError";
    }
    return "Unknown";
}

} // namespace legend::world
