#include "Server/WorldServer/Portal/PortalManager.h"

#include "Server/WorldServer/Portal/PortalRegistry.h"
#include "Server/WorldServer/Portal/PortalSpatialGrid.h"

namespace legend::world {

std::size_t PortalManager::SpawnFromRegistry() {
    const auto& registry = PortalRegistry::Instance();
    for (const auto& definition : registry.AllPortals()) {
        PortalEntity entity;
        entity.entityId = m_nextEntityId++;
        entity.definition = &definition;
        m_portals.emplace(entity.entityId, std::move(entity));
    }
    return m_portals.size();
}

const PortalDefinition* PortalManager::Find(std::uint64_t portalEntityId) const {
    const auto it = m_portals.find(portalEntityId);
    return it != m_portals.end() ? it->second.definition : nullptr;
}

void PortalManager::AddToGrid(PortalSpatialGrid& grid) const {
    for (const auto& [portalEntityId, entity] : m_portals) {
        if (entity.definition != nullptr && entity.definition->enabled) {
            grid.AddPortal(entity.entityId, entity.definition->x, entity.definition->y);
        }
    }
}

} // namespace legend::world
