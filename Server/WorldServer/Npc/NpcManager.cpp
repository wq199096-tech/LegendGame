#include "Server/WorldServer/Npc/NpcManager.h"

#include "Server/WorldServer/Npc/NpcRegistry.h"
#include "Server/WorldServer/Npc/NpcSpatialGrid.h"

namespace legend::world {

std::size_t NpcManager::SpawnFromRegistry() {
    const auto& registry = NpcRegistry::Instance();
    for (const auto& definition : registry.AllNpcs()) {
        // 阶段22 22.6：disabled 的 NPC 定义不生成实体。
        if (!definition.enabled) {
            continue;
        }
        NpcEntity entity(m_nextEntityId++, &definition);
        m_npcs.emplace(entity.EntityId(), std::move(entity));
    }
    return m_npcs.size();
}

const NpcEntity* NpcManager::Find(std::uint64_t npcEntityId) const {
    const auto it = m_npcs.find(npcEntityId);
    return it != m_npcs.end() ? &it->second : nullptr;
}

void NpcManager::AddToGrid(NpcSpatialGrid& grid) const {
    for (const auto& [npcEntityId, entity] : m_npcs) {
        if (entity.Active()) {
            grid.AddNpc(entity.EntityId(), entity.X(), entity.Y());
        }
    }
}

} // namespace legend::world
