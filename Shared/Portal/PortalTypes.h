#pragma once

#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段21：Portal 类型（指令十四~二十二）。
// ---------------------------------------------------------------------------

// PortalDespawn.reason。
enum class PortalDespawnReason : std::uint8_t {
    LeftAOI = 1,
    ServerCleanup = 2,
};

inline const char* PortalDespawnReasonName(std::uint8_t reason) {
    switch (static_cast<PortalDespawnReason>(reason)) {
        case PortalDespawnReason::LeftAOI: return "LeftAOI";
        case PortalDespawnReason::ServerCleanup: return "ServerCleanup";
    }
    return "Unknown";
}

// PortalUseResponse.resultCode（指令二十一/二十二验证链）。
enum class PortalResultCode : std::uint8_t {
    Success = 0,
    PlayerNotFound = 1,
    Dead = 2,
    PortalNotFound = 3,
    PortalDisabled = 4,
    WrongMap = 5,
    NotVisible = 6,
    TooFar = 7,
    LevelTooLow = 8,
    NotEnoughGold = 9,
    DuplicateRequest = 10,
    TransitionInProgress = 11,
    MapNotFound = 12,
    MalformedRequest = 13,
};

inline const char* PortalResultCodeName(std::uint8_t code) {
    switch (static_cast<PortalResultCode>(code)) {
        case PortalResultCode::Success: return "Success";
        case PortalResultCode::PlayerNotFound: return "PlayerNotFound";
        case PortalResultCode::Dead: return "Dead";
        case PortalResultCode::PortalNotFound: return "PortalNotFound";
        case PortalResultCode::PortalDisabled: return "PortalDisabled";
        case PortalResultCode::WrongMap: return "WrongMap";
        case PortalResultCode::NotVisible: return "NotVisible";
        case PortalResultCode::TooFar: return "TooFar";
        case PortalResultCode::LevelTooLow: return "LevelTooLow";
        case PortalResultCode::NotEnoughGold: return "NotEnoughGold";
        case PortalResultCode::DuplicateRequest: return "DuplicateRequest";
        case PortalResultCode::TransitionInProgress: return "TransitionInProgress";
        case PortalResultCode::MapNotFound: return "MapNotFound";
        case PortalResultCode::MalformedRequest: return "MalformedRequest";
    }
    return "Unknown";
}

} // namespace legend::world
