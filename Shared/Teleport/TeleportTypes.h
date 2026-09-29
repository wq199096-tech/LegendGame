#pragma once

#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20：Teleport 公共类型（指令六十四）。
// ---------------------------------------------------------------------------
enum class TeleportResultCode : std::uint8_t {
    Success = 0,
    MalformedRequest = 1,
    NotInWorld = 2,
    Dead = 3,
    SessionNotFound = 4,
    SessionExpired = 5,
    TeleportNotFound = 6,
    NotOfferedByNpc = 7, // 该 NPC 不提供该 teleportId（指令六十四）
    LevelTooLow = 8,
    NotEnoughGold = 9,
    DuplicateRequest = 10,
    InternalError = 11,
};

inline const char* TeleportResultCodeName(std::uint8_t code) {
    switch (static_cast<TeleportResultCode>(code)) {
        case TeleportResultCode::Success: return "Success";
        case TeleportResultCode::MalformedRequest: return "MalformedRequest";
        case TeleportResultCode::NotInWorld: return "NotInWorld";
        case TeleportResultCode::Dead: return "Dead";
        case TeleportResultCode::SessionNotFound: return "SessionNotFound";
        case TeleportResultCode::SessionExpired: return "SessionExpired";
        case TeleportResultCode::TeleportNotFound: return "TeleportNotFound";
        case TeleportResultCode::NotOfferedByNpc: return "NotOfferedByNpc";
        case TeleportResultCode::LevelTooLow: return "LevelTooLow";
        case TeleportResultCode::NotEnoughGold: return "NotEnoughGold";
        case TeleportResultCode::DuplicateRequest: return "DuplicateRequest";
        case TeleportResultCode::InternalError: return "InternalError";
    }
    return "Unknown";
}

} // namespace legend::world
