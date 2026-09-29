#include "Server/WorldServer/Portal/PortalRegistry.h"

#include "Server/WorldServer/Map/MapRegistry.h"
#include "Shared/WorldData/WorldDataJson.h"

namespace legend::world {

const PortalRegistry& PortalRegistry::Instance() {
    static const PortalRegistry registry;
    return registry;
}

PortalRegistry& PortalRegistry::Mutable() {
    return const_cast<PortalRegistry&>(Instance());
}

PortalRegistry::PortalRegistry() {
    // 阶段22 22.11：硬编码迁入 Data/World（出厂数据由 MakeDefaultWorldData 提供）。
}

const PortalDefinition* PortalRegistry::FindPortal(std::uint32_t portalId) const {
    for (const auto& portal : m_portals) {
        if (portal.portalId == portalId) {
            return &portal;
        }
    }
    return nullptr;
}

bool PortalRegistry::ValidatePortals(const MapRegistry& maps, std::string& error) const {
    for (std::size_t i = 0; i < m_portals.size(); ++i) {
        const PortalDefinition& portal = m_portals[i];
        for (std::size_t j = i + 1; j < m_portals.size(); ++j) {
            if (m_portals[j].portalId == portal.portalId) {
                error = "duplicate portalId " + std::to_string(portal.portalId);
                return false;
            }
        }
        const MapDefinition* source = maps.FindMap(portal.sourceMapId);
        if (source == nullptr) {
            error = "portal " + std::to_string(portal.portalId) + " source map missing";
            return false;
        }
        const MapDefinition* destination = maps.FindMap(portal.destinationMapId);
        if (destination == nullptr) {
            error = "portal " + std::to_string(portal.portalId) + " destination map missing";
            return false;
        }
        if (!source->InBounds(portal.x, portal.y)) {
            error = "portal " + std::to_string(portal.portalId) + " source out of bounds";
            return false;
        }
        if (!destination->InBounds(portal.destinationX, portal.destinationY)) {
            error = "portal " + std::to_string(portal.portalId) + " destination out of bounds";
            return false;
        }
        if (portal.interactionRadius <= 0.0f) {
            error = "portal " + std::to_string(portal.portalId) + " invalid radius";
            return false;
        }
    }
    return true;
}

void PortalRegistry::LoadFromDefinitions(std::vector<PortalDefinition> portals) {
    Mutable().m_portals = std::move(portals);
}

void PortalRegistry::LoadDefaults() {
    LoadFromDefinitions(MakeDefaultWorldData().portals);
}

} // namespace legend::world
