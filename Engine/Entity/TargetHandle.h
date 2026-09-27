#pragma once

#include "Engine/Entity/EntityId.h"

namespace legend::entity {

class ActorRegistry;
class Character;

// 目标句柄：只持有 EntityId，通过 ActorRegistry 解析为实际 Actor。
// 目标被删除/注销后自动失效，不留野指针（为未来网络对象删除预留安全性）。
class TargetHandle {
public:
    TargetHandle() = default;
    explicit TargetHandle(EntityId id) : m_id(id) {}

    void Set(EntityId id) { m_id = id; }
    void Clear() { m_id = kInvalidEntityId; }

    EntityId GetId() const { return m_id; }
    bool IsEmpty() const { return m_id == kInvalidEntityId; }

    // 目标存在且 active
    bool IsValid(const ActorRegistry& registry) const;
    // 解析目标 Actor（id 无效 / 未注册 / inactive 返回 nullptr）
    Character* Resolve(const ActorRegistry& registry) const;

private:
    EntityId m_id = kInvalidEntityId;
};

} // namespace legend::entity
