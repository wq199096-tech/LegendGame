#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20 指令十二/十三/十八/二十/三十三：NPC 协议 payload。
// MessageId：NpcSpawn=320 / NpcDespawn=321 / NpcInteractRequest=322 /
// NpcInteractResponse=323 / NpcQuestMarkerUpdate=326。
// Client 只能发 NpcInteractRequest（requestId + npcEntityId，指令二/十八）；
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0（指令八十八）。
// ---------------------------------------------------------------------------

// NpcInteractRequest(322)（指令十八）。
struct NpcInteractRequestPayload {
    std::uint64_t requestId = 0;
    std::uint64_t npcEntityId = 0;
};

// NpcInteractResponse(323)（指令二十）。
struct NpcInteractResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint8_t resultCode = 0; // NpcResultCode
    std::uint64_t npcEntityId = 0;
    std::uint64_t dialogueSessionId = 0; // 成功时有效
    std::uint32_t dialogueId = 0;
    std::string message;
};

// NpcQuestMarkerUpdate(326)（指令三十三）：per-player，只发本人。
struct NpcQuestMarkerUpdatePayload {
    std::uint64_t npcEntityId = 0;
    std::uint32_t npcDefinitionId = 0;
    std::uint8_t marker = 0; // NpcQuestMarker
};

bool EncodeNpcSpawn(std::vector<std::uint8_t>& out, std::uint64_t npcEntityId,
                    std::uint32_t npcDefinitionId, const std::string& name, std::uint16_t mapId,
                    float x, float y, std::uint8_t npcType, std::uint32_t visualId);
bool DecodeNpcSpawn(const std::uint8_t* data, std::size_t size, std::uint64_t& npcEntityId,
                    std::uint32_t& npcDefinitionId, std::string& name, std::uint16_t& mapId,
                    float& x, float& y, std::uint8_t& npcType, std::uint32_t& visualId,
                    std::string& error);
bool EncodeNpcDespawn(std::vector<std::uint8_t>& out, std::uint64_t npcEntityId,
                      std::uint8_t reason);
bool DecodeNpcDespawn(const std::uint8_t* data, std::size_t size, std::uint64_t& npcEntityId,
                      std::uint8_t& reason, std::string& error);
bool EncodeNpcInteractRequest(const NpcInteractRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeNpcInteractRequest(const std::uint8_t* data, std::size_t size,
                              NpcInteractRequestPayload& out, std::string& error);
bool EncodeNpcInteractResponse(const NpcInteractResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeNpcInteractResponse(const std::uint8_t* data, std::size_t size,
                               NpcInteractResponsePayload& out, std::string& error);
bool EncodeNpcQuestMarkerUpdate(const NpcQuestMarkerUpdatePayload& p,
                                std::vector<std::uint8_t>& out);
bool DecodeNpcQuestMarkerUpdate(const std::uint8_t* data, std::size_t size,
                                NpcQuestMarkerUpdatePayload& out, std::string& error);

} // namespace legend::world
