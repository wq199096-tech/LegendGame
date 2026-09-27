#include "Engine/Combat/CombatTarget.h"

#include "Engine/Entity/ActorRegistry.h"
#include "Engine/Entity/Character.h"

namespace legend::combat {

bool CombatTarget::IsValid(const legend::entity::ActorRegistry& registry) const {
    return Resolve(registry) != nullptr;
}

legend::entity::Character* CombatTarget::Resolve(
    const legend::entity::ActorRegistry& registry) const {
    if (m_handle.IsEmpty()) {
        return nullptr;
    }
    legend::entity::Character* actor = m_handle.Resolve(registry);
    if (actor == nullptr) {
        return nullptr;
    }
    // 目标必须战斗存活：死亡播放动画期间不可被选中/攻击
    if (!actor->IsCombatAlive()) {
        return nullptr;
    }
    return actor;
}

} // namespace legend::combat
