#include "Server/WorldServer/Status/StatusEffectRegistry.h"

namespace legend::world {

StatusEffectRegistry::StatusEffectRegistry() {
    // 阶段16：硬编码五个状态效果（指令一/七~十一）。
    m_effects.push_back(kBattleFocusDefinition);
    m_effects.push_back(kArmorBreakDefinition);
    m_effects.push_back(kBurnDefinition);
    m_effects.push_back(kPoisonDefinition);
    m_effects.push_back(kSlowDefinition);
}

const StatusEffectDefinition* StatusEffectRegistry::FindEffect(StatusEffectId effectId) const {
    for (const auto& effect : m_effects) {
        if (effect.effectId == effectId) {
            return &effect;
        }
    }
    return nullptr;
}

} // namespace legend::world
