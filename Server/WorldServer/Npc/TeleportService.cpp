#include "Server/WorldServer/Npc/TeleportService.h"

namespace legend::world {

const TeleportRegistry& TeleportRegistry::Instance() {
    static const TeleportRegistry registry;
    return registry;
}

TeleportRegistry::TeleportRegistry() {
    // 指令五十九/六十：7001 Wayfarer（→1500,1500 费 20G）/ 7002 Explorer Guide（→300,300 免费）。
    TeleportDefinition far;
    far.teleportId = 7001;
    far.name = "Far Plains";
    far.destinationMapId = 1;
    far.destinationX = 1500.0f;
    far.destinationY = 1500.0f;
    far.goldCost = 20;
    far.minLevel = 1;
    m_teleports.push_back(far);

    TeleportDefinition home;
    home.teleportId = 7002;
    home.name = "Village Square";
    home.destinationMapId = 1;
    home.destinationX = 300.0f;
    home.destinationY = 300.0f;
    home.goldCost = 0;
    home.minLevel = 1;
    m_teleports.push_back(home);
}

const TeleportDefinition* TeleportRegistry::FindTeleport(std::uint32_t teleportId) const {
    for (const auto& teleport : m_teleports) {
        if (teleport.teleportId == teleportId) {
            return &teleport;
        }
    }
    return nullptr;
}

} // namespace legend::world
