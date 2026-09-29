#include "Client/WorldNetwork/RemoteNpcManager.h"

#include "Engine/Debug/Logger.h"

namespace legend::client {

void RemoteNpcManager::HandleSpawn(std::uint64_t npcEntityId, std::uint32_t npcDefinitionId,
                                   const std::string& name, std::uint16_t mapId, float x, float y,
                                   world::NpcType type, std::uint32_t visualId) {
    RemoteNpcEntity& npc = m_npcs[npcEntityId];
    npc.npcEntityId = npcEntityId;
    npc.npcDefinitionId = npcDefinitionId;
    npc.name = name;
    npc.mapId = mapId;
    npc.x = x;
    npc.y = y;
    npc.type = type;
    npc.visualId = visualId;
    npc.alive = true;
    LOG_DEBUG("[Npc] NpcSpawn #" + std::to_string(npcEntityId) + " " + name);
}

bool RemoteNpcManager::HandleDespawn(std::uint64_t npcEntityId) {
    // 指令四十八类比：Despawn 即移除（不保留幽灵 NPC）。
    const bool removed = m_npcs.erase(npcEntityId) != 0;
    if (removed) {
        LOG_DEBUG("[Npc] NpcDespawn #" + std::to_string(npcEntityId));
    }
    return removed;
}

void RemoteNpcManager::ApplyMarker(std::uint64_t npcEntityId, world::NpcQuestMarker marker) {
    const auto it = m_npcs.find(npcEntityId);
    if (it == m_npcs.end()) {
        LOG_DEBUG("[Npc] marker for unknown npc " + std::to_string(npcEntityId) + " ignored");
        return;
    }
    it->second.questMarker = marker;
}

} // namespace legend::client
