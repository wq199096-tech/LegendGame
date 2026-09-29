#pragma once

#include "Client/WorldNetwork/RemoteNpcEntity.h"
#include "Shared/Npc/NpcTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace legend::client {

// ---------------------------------------------------------------------------
// 阶段20 指令十五：RemoteNpcManager —— 只维护服务器 Spawn 过的 NPC
//（Despawn 即移除，不凭空创建；Marker 经 NpcQuestMarkerUpdate 更新）。
// 主线程独占（阶段12 指令四十七同模式）。
// ---------------------------------------------------------------------------
class RemoteNpcManager {
public:
    void Clear() { m_npcs.clear(); }
    std::size_t Count() const { return m_npcs.size(); }
    const RemoteNpcEntity* Find(std::uint64_t npcEntityId) const {
        const auto it = m_npcs.find(npcEntityId);
        return it != m_npcs.end() ? &it->second : nullptr;
    }
    const std::unordered_map<std::uint64_t, RemoteNpcEntity>& All() const { return m_npcs; }

    // NpcSpawn(320)：不存在创建 / 已存在更新（字段完整覆盖）。
    void HandleSpawn(std::uint64_t npcEntityId, std::uint32_t npcDefinitionId,
                     const std::string& name, std::uint16_t mapId, float x, float y,
                     world::NpcType type, std::uint32_t visualId);
    // NpcDespawn(321)：立即移除（不保留幽灵 NPC）。
    bool HandleDespawn(std::uint64_t npcEntityId);
    // NpcQuestMarkerUpdate(326)：per-player Marker 更新（未知 NPC 忽略）。
    void ApplyMarker(std::uint64_t npcEntityId, world::NpcQuestMarker marker);

private:
    std::unordered_map<std::uint64_t, RemoteNpcEntity> m_npcs;
};

} // namespace legend::client
