#pragma once

#include "Shared/Teleport/TeleportTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20 指令六十三/六十八：Teleport 协议。
// MessageId：TeleportRequest=333 / TeleportResponse=334。
// Client 只能选择 NPC 提供的 Teleport Option（指令六十二）——绝不能直接发坐标。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0（指令八十八）。
// ---------------------------------------------------------------------------

// TeleportRequest(333)。
struct TeleportRequestPayload {
    std::uint64_t requestId = 0;
    std::uint64_t dialogueSessionId = 0;
    std::uint32_t teleportId = 0;
};

// TeleportResponse(334)（指令六十八）。
struct TeleportResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint8_t resultCode = 0; // TeleportResultCode
    std::uint16_t mapId = 1;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t goldCost = 0;
    std::int64_t newGold = 0;
};

bool EncodeTeleportRequest(const TeleportRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeTeleportRequest(const std::uint8_t* data, std::size_t size, TeleportRequestPayload& out,
                           std::string& error);
bool EncodeTeleportResponse(const TeleportResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeTeleportResponse(const std::uint8_t* data, std::size_t size,
                            TeleportResponsePayload& out, std::string& error);

} // namespace legend::world
