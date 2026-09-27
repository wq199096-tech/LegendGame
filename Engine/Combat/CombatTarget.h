#pragma once

#include "Engine/Entity/EntityId.h"
#include "Engine/Entity/TargetHandle.h"

namespace legend::entity {
class ActorRegistry;
class Character;
}

namespace legend::combat {

// 战斗目标句柄：内部仍是 EntityId，通过 ActorRegistry 解析（无裸指针）。
// 有效性 = 目标存在 + active + 战斗存活（alive）。
// NPC 战斗未启用（CombatEnabled=false）时永远无效。
class CombatTarget {
public:
    void SetTarget(legend::entity::EntityId id) { m_handle.Set(id); }
    void ClearTarget() { m_handle.Clear(); }
    legend::entity::EntityId GetTargetId() const { return m_handle.GetId(); }
    bool IsEmpty() const { return m_handle.IsEmpty(); }

    // 目标存在 + active + alive（可被攻击）
    bool IsValid(const legend::entity::ActorRegistry& registry) const;
    // 解析有效目标；无效返回 nullptr（调用方必须判空）
    legend::entity::Character* Resolve(const legend::entity::ActorRegistry& registry) const;

private:
    legend::entity::TargetHandle m_handle;
};

} // namespace legend::combat
