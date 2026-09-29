#pragma once

#include "Shared/Npc/NpcTypes.h"

#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20：NPC 枚举名称映射（inline；Client 头文件即用）。
// ---------------------------------------------------------------------------

inline const char* NpcTypeName(std::uint8_t type) {
    switch (static_cast<NpcType>(type)) {
        case NpcType::QuestGiver: return "QuestGiver";
        case NpcType::Merchant: return "Merchant";
        case NpcType::Teleporter: return "Teleporter";
        case NpcType::MultiFunction: return "MultiFunction";
    }
    return "Unknown";
}

inline const char* NpcDespawnReasonName(std::uint8_t reason) {
    switch (static_cast<NpcDespawnReason>(reason)) {
        case NpcDespawnReason::LeftAOI: return "LeftAOI";
        case NpcDespawnReason::ServerCleanup: return "ServerCleanup";
    }
    return "Unknown";
}

inline const char* NpcQuestMarkerName(std::uint8_t marker) {
    switch (static_cast<NpcQuestMarker>(marker)) {
        case NpcQuestMarker::None: return "None";
        case NpcQuestMarker::InProgress: return "InProgress";
        case NpcQuestMarker::Available: return "Available";
        case NpcQuestMarker::ReadyToTurnIn: return "ReadyToTurnIn";
    }
    return "Unknown";
}

inline const char* NpcResultCodeName(std::uint8_t code) {
    switch (static_cast<NpcResultCode>(code)) {
        case NpcResultCode::Success: return "Success";
        case NpcResultCode::MalformedRequest: return "MalformedRequest";
        case NpcResultCode::NotInWorld: return "NotInWorld";
        case NpcResultCode::Dead: return "Dead";
        case NpcResultCode::NpcNotFound: return "NpcNotFound";
        case NpcResultCode::NpcNotActive: return "NpcNotActive";
        case NpcResultCode::WrongMap: return "WrongMap";
        case NpcResultCode::NotVisible: return "NotVisible";
        case NpcResultCode::TooFar: return "TooFar";
        case NpcResultCode::DuplicateRequest: return "DuplicateRequest";
        case NpcResultCode::SessionNotFound: return "SessionNotFound";
        case NpcResultCode::SessionExpired: return "SessionExpired";
        case NpcResultCode::InternalError: return "InternalError";
    }
    return "Unknown";
}

} // namespace legend::world
