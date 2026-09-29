#include "Server/WorldServer/Npc/TeleportService.h"

#include "Shared/GameData/GameDataJson.h"

namespace legend::world {

const TeleportRegistry& TeleportRegistry::Instance() {
    static const TeleportRegistry registry;
    return registry;
}

TeleportRegistry& TeleportRegistry::Mutable() {
    return const_cast<TeleportRegistry&>(Instance());
}

TeleportRegistry::TeleportRegistry() {
    // 阶段23 23.22：构造即填充出厂默认（直接填充——见 QuestRegistry 构造注释）。
    m_teleports = MakeDefaultGameData().teleports;
}

const TeleportDefinition* TeleportRegistry::FindTeleport(std::uint32_t teleportId) const {
    for (const auto& teleport : m_teleports) {
        if (teleport.teleportId == teleportId) {
            return &teleport;
        }
    }
    return nullptr;
}

void TeleportRegistry::LoadFromDefinitions(std::vector<TeleportDefinition> teleports) {
    Mutable().m_teleports = std::move(teleports);
}

void TeleportRegistry::LoadDefaults() {
    LoadFromDefinitions(MakeDefaultGameData().teleports);
}

} // namespace legend::world
