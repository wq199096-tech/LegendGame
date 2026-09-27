#include "Engine/Entity/ActorRegistry.h"

#include <cmath>

#include "Engine/Debug/Logger.h"
#include "Engine/Entity/Character.h"

namespace legend::entity {

void ActorRegistry::Register(Character* actor) {
    if (actor == nullptr) {
        LOG_WARN("ActorRegistry: ignored null register.");
        return;
    }
    if (Get(actor->GetId()) != nullptr) {
        LOG_WARN("ActorRegistry: actor id " + std::to_string(actor->GetId()) +
                 " already registered, ignored.");
        return;
    }
    m_actors.push_back(actor);
}

void ActorRegistry::Unregister(EntityId id) {
    for (std::size_t i = 0; i < m_actors.size(); ++i) {
        if (m_actors[i]->GetId() == id) {
            m_actors.erase(m_actors.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

Character* ActorRegistry::Get(EntityId id) const {
    for (Character* actor : m_actors) {
        if (actor->GetId() == id) {
            return actor;
        }
    }
    return nullptr;
}

std::vector<Character*> ActorRegistry::GetByType(ActorType type) const {
    std::vector<Character*> result;
    for (Character* actor : m_actors) {
        // 只返回活跃 Actor：inactive 不得进入类型查询（目标/遍历语义）
        if (actor == nullptr || !actor->IsActive()) {
            continue;
        }
        if (actor->GetActorType() == type) {
            result.push_back(actor);
        }
    }
    return result;
}

std::vector<Character*> ActorRegistry::FindInRadius(const math::Vector2& position,
                                                    float radius) const {
    std::vector<Character*> result;
    if (radius <= 0.0f) {
        return result;
    }
    const float radiusSq = radius * radius;
    for (Character* actor : m_actors) {
        // 只返回活跃 Actor：inactive 不能进入附近目标查询
        if (actor == nullptr || !actor->IsActive()) {
            continue;
        }
        const math::Vector2 delta = actor->GetPosition() - position;
        if (delta.LengthSq() < radiusSq) {
            result.push_back(actor);
        }
    }
    return result;
}

} // namespace legend::entity
