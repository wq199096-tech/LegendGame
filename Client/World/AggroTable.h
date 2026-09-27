#pragma once

#include <cstdint>
#include <vector>

#include "Engine/Entity/EntityId.h"

namespace legend::world {

using legend::entity::EntityId;          // 仇恨表键使用实体 ID
using legend::entity::kInvalidEntityId;  // 空目标哨兵

// 基础仇恨框架：EntityId -> threat 值。
// 本阶段目标选择简化为：Player 进入 Aggro 范围 -> AddThreat(playerId, 1.0)。
// 复杂战斗仇恨（伤害贡献/嘲讽等）后续阶段扩展。
class AggroTable {
public:
    void AddThreat(EntityId id, float amount);
    void Remove(EntityId id);
    void Clear() { m_entries.clear(); }

    // 最高仇恨目标（相同值取先加入者）；空表返回 kInvalidEntityId
    EntityId GetHighestThreat() const;

    bool Empty() const { return m_entries.empty(); }
    std::size_t Count() const { return m_entries.size(); }
    // 指定目标的当前仇恨值（不存在返回 0）
    float GetThreat(EntityId id) const;

private:
    struct Entry {
        EntityId id = 0;
        float threat = 0.0f;
    };
    std::vector<Entry> m_entries;
};

} // namespace legend::world
