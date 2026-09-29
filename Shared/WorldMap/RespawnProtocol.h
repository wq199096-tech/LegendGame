#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段21：复活协议（指令三十三/三十四/四十二）。
// MessageId：RespawnRequest=346 / RespawnResponse=347 / PlayerRespawned=348。
// Client 死亡状态下只能发 RespawnRequest（R=CurrentMap / T=Town 由 Client 决定
// mode，服务器全部重验——指令三十二/三十七/三十八/五十八）。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0（指令五十七）。
// ---------------------------------------------------------------------------

// RespawnMode（指令三十四）：只允许 1/2（指令五十八）。
enum class RespawnMode : std::uint8_t {
    CurrentMap = 1,
    Town = 2,
};

inline const char* RespawnModeName(std::uint8_t mode) {
    switch (static_cast<RespawnMode>(mode)) {
        case RespawnMode::CurrentMap: return "CurrentMap";
        case RespawnMode::Town: return "Town";
    }
    return "Unknown";
}

// RespawnResponse.resultCode（指令五十八/九十二：mode 非法/太早/钱不足均拒绝）。
enum class RespawnResultCode : std::uint8_t {
    Success = 0,
    PlayerNotFound = 1,
    NotDead = 2,
    TooEarly = 3,
    InvalidMode = 4,
    NotEnoughGold = 5,
    DuplicateRequest = 6,
    TransitionInProgress = 7,
    MapNotFound = 8,
    InternalError = 9,
};

inline const char* RespawnResultCodeName(std::uint8_t code) {
    switch (static_cast<RespawnResultCode>(code)) {
        case RespawnResultCode::Success: return "Success";
        case RespawnResultCode::PlayerNotFound: return "PlayerNotFound";
        case RespawnResultCode::NotDead: return "NotDead";
        case RespawnResultCode::TooEarly: return "TooEarly";
        case RespawnResultCode::InvalidMode: return "InvalidMode";
        case RespawnResultCode::NotEnoughGold: return "NotEnoughGold";
        case RespawnResultCode::DuplicateRequest: return "DuplicateRequest";
        case RespawnResultCode::TransitionInProgress: return "TransitionInProgress";
        case RespawnResultCode::MapNotFound: return "MapNotFound";
        case RespawnResultCode::InternalError: return "InternalError";
    }
    return "Unknown";
}

// RespawnRequest(346)（指令三十三）。
struct RespawnRequestPayload {
    std::uint64_t requestId = 0;
    std::uint8_t respawnMode = 0; // RespawnMode：只允许 1/2（指令五十八）
};

// RespawnResponse(347)。
struct RespawnResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint8_t resultCode = 0; // RespawnResultCode
    std::uint16_t mapId = 1;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t goldCost = 0;
    std::int64_t newGold = 0;
};

// PlayerRespawned(348)（指令四十二：只发本人；同图他人经 PlayerSpawn/快照恢复 Alive）。
struct PlayerRespawnedPayload {
    std::uint16_t mapId = 1;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t hp = 0;
    std::uint32_t maxHp = 0;
    std::uint32_t mana = 0;
    std::uint32_t maxMana = 0;
    std::int64_t gold = 0;
    std::uint64_t serverTime = 0;
};

bool EncodeRespawnRequest(const RespawnRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeRespawnRequest(const std::uint8_t* data, std::size_t size, RespawnRequestPayload& out,
                          std::string& error);
bool EncodeRespawnResponse(const RespawnResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeRespawnResponse(const std::uint8_t* data, std::size_t size, RespawnResponsePayload& out,
                           std::string& error);
bool EncodePlayerRespawned(const PlayerRespawnedPayload& p, std::vector<std::uint8_t>& out);
bool DecodePlayerRespawned(const std::uint8_t* data, std::size_t size, PlayerRespawnedPayload& out,
                           std::string& error);

} // namespace legend::world
