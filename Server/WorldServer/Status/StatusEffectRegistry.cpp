#include "Server/WorldServer/Status/StatusEffectRegistry.h"

#include "Shared/GameData/GameDataJson.h"

namespace legend::world {

StatusEffectRegistry::StatusEffectRegistry() {
    // 阶段23 23.22：构造即装载出厂默认（保持"构造后可用"语义）；Start 覆盖注入。
    LoadDefaults();
}

const StatusEffectDefinition* StatusEffectRegistry::FindEffect(StatusEffectId effectId) const {
    for (const auto& effect : m_effects) {
        if (effect.effectId == effectId) {
            return &effect;
        }
    }
    return nullptr;
}

void StatusEffectRegistry::LoadFromDefinitions(
    std::vector<StatusEffectDefinition> effects) {
    m_effects = std::move(effects);
}

void StatusEffectRegistry::LoadDefaults() {
    LoadFromDefinitions(MakeDefaultGameData().statuses);
}

} // namespace legend::world
