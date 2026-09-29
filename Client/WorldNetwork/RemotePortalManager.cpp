#include "Client/WorldNetwork/RemotePortalManager.h"

#include "Engine/Debug/Logger.h"

namespace legend::client {

void RemotePortalManager::HandleSpawn(std::uint64_t portalEntityId, std::uint32_t portalId,
                                      const std::string& name, std::uint16_t mapId, float x,
                                      float y, float interactionRadius,
                                      std::uint16_t destinationMapId,
                                      const std::string& destinationName) {
    RemotePortalEntity& portal = m_portals[portalEntityId];
    portal.portalEntityId = portalEntityId;
    portal.portalId = portalId;
    portal.name = name;
    portal.mapId = mapId;
    portal.x = x;
    portal.y = y;
    portal.interactionRadius = interactionRadius;
    portal.destinationMapId = destinationMapId;
    portal.destinationName = destinationName;
    portal.active = true;
    LOG_DEBUG("[Portal] spawn entity=" + std::to_string(portalEntityId) + " id=" +
              std::to_string(portalId) + " -> " + destinationName);
}

bool RemotePortalManager::HandleDespawn(std::uint64_t portalEntityId) {
    const bool removed = m_portals.erase(portalEntityId) != 0;
    if (removed) {
        LOG_DEBUG("[Portal] despawn entity=" + std::to_string(portalEntityId));
    }
    return removed;
}

} // namespace legend::client
