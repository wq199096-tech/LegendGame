#include "Server/WorldServer/Portal/PortalRegistry.h"

#include "Server/WorldServer/Map/MapRegistry.h"

namespace legend::world {

const PortalRegistry& PortalRegistry::Instance() {
    static const PortalRegistry registry;
    return registry;
}

PortalRegistry::PortalRegistry() {
    // 指令十六：固定 4 个 Portal（interactionRadius 统一 100——AOI 600 内可见，
    // 进入交互半径后按 F 触发，不自动传送）。
    PortalDefinition p8001;
    p8001.portalId = 8001;
    p8001.sourceMapId = 1;
    p8001.x = 1000.0f;
    p8001.y = 300.0f;
    p8001.interactionRadius = 100.0f;
    p8001.destinationMapId = 2;
    p8001.destinationX = 200.0f;
    p8001.destinationY = 500.0f;
    p8001.minLevel = 1;
    p8001.goldCost = 0;
    p8001.enabled = true;
    m_portals.push_back(p8001);

    PortalDefinition p8002;
    p8002.portalId = 8002;
    p8002.sourceMapId = 2;
    p8002.x = 150.0f;
    p8002.y = 500.0f;
    p8002.interactionRadius = 100.0f;
    p8002.destinationMapId = 1;
    p8002.destinationX = 900.0f;
    p8002.destinationY = 300.0f;
    p8002.minLevel = 1;
    p8002.goldCost = 0;
    p8002.enabled = true;
    m_portals.push_back(p8002);

    PortalDefinition p8003;
    p8003.portalId = 8003;
    p8003.sourceMapId = 2;
    p8003.x = 1800.0f;
    p8003.y = 1000.0f;
    p8003.interactionRadius = 100.0f;
    p8003.destinationMapId = 3;
    p8003.destinationX = 200.0f;
    p8003.destinationY = 300.0f;
    p8003.minLevel = 2;
    p8003.goldCost = 10;
    p8003.enabled = true;
    m_portals.push_back(p8003);

    PortalDefinition p8004;
    p8004.portalId = 8004;
    p8004.sourceMapId = 3;
    p8004.x = 150.0f;
    p8004.y = 300.0f;
    p8004.interactionRadius = 100.0f;
    p8004.destinationMapId = 2;
    p8004.destinationX = 1700.0f;
    p8004.destinationY = 1000.0f;
    p8004.minLevel = 1;
    p8004.goldCost = 0;
    p8004.enabled = true;
    m_portals.push_back(p8004);
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

} // namespace legend::world
