#include "Engine/Entity/TargetHandle.h"

#include "Engine/Entity/ActorRegistry.h"
#include "Engine/Entity/Character.h"

namespace legend::entity {

bool TargetHandle::IsValid(const ActorRegistry& registry) const {
    if (IsEmpty()) {
        return false;
    }
    const Character* actor = registry.Get(m_id);
    return actor != nullptr && actor->IsActive();
}

Character* TargetHandle::Resolve(const ActorRegistry& registry) const {
    if (IsEmpty()) {
        return nullptr;
    }
    Character* actor = registry.Get(m_id);
    if (actor == nullptr || !actor->IsActive()) {
        return nullptr;
    }
    return actor;
}

} // namespace legend::entity