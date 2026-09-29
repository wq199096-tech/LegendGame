#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段21：Portal 协议 payload。
// MessageId：PortalSpawn=340 / PortalDespawn=341 / PortalUseRequest=342 /
// PortalUseResponse=343。
// Client 只能发 PortalUseRequest（requestId + portalEntityId，指令二十——
// 目标 map/坐标/费用全部服务器权威）。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0（指令五十七）。
// ---------------------------------------------------------------------------

// PortalUseRequest(342)（指令二十）。
struct PortalUseRequestPayload {
    std::uint64_t requestId = 0;
    std::uint64_t portalEntityId = 0;
};

// PortalUseResponse(343)（指令二十二）。
struct PortalUseResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint8_t resultCode = 0; // PortalResultCode
    std::uint16_t sourceMapId = 1;
    std::uint16_t destinationMapId = 1;
    float destinationX = 0.0f;
    float destinationY = 0.0f;
    std::uint32_t goldCost = 0;
    std::int64_t newGold = 0;
};

bool EncodePortalSpawn(std::vector<std::uint8_t>& out, std::uint64_t portalEntityId,
                       std::uint32_t portalId, const std::string& name, std::uint16_t mapId,
                       float x, float y, float interactionRadius,
                       std::uint16_t destinationMapId, const std::string& destinationName);
bool DecodePortalSpawn(const std::uint8_t* data, std::size_t size,
                       std::uint64_t& portalEntityId, std::uint32_t& portalId, std::string& name,
                       std::uint16_t& mapId, float& x, float& y, float& interactionRadius,
                       std::uint16_t& destinationMapId, std::string& destinationName,
                       std::string& error);
bool EncodePortalDespawn(std::vector<std::uint8_t>& out, std::uint64_t portalEntityId,
                         std::uint8_t reason);
bool DecodePortalDespawn(const std::uint8_t* data, std::size_t size,
                         std::uint64_t& portalEntityId, std::uint8_t& reason, std::string& error);
bool EncodePortalUseRequest(const PortalUseRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodePortalUseRequest(const std::uint8_t* data, std::size_t size,
                            PortalUseRequestPayload& out, std::string& error);
bool EncodePortalUseResponse(const PortalUseResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodePortalUseResponse(const std::uint8_t* data, std::size_t size,
                             PortalUseResponsePayload& out, std::string& error);

} // namespace legend::world
