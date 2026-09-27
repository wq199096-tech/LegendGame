#pragma once

#include <cstdint>
#include <string>

#include "Engine/Math/Vector2.h"

namespace legend::world {

// 掉落实体 ID 类型：独立分配器（不与 EntityId / ItemInstanceId 混用）
using LootEntityId = std::uint64_t;

// Reset 语义（阶段6.1统一）：Reset(nextValue) —— 重置后下一次 Next() 返回 nextValue 本身
// （nextValue 视为"尚未分配"）。Reset(1) -> Next() == 1 -> 2 -> 3 ...；默认起点 1。
class LootEntityIdAllocator {
public:
    static LootEntityId Next() {
        return ++m_next;
    }
    // nextValue < 1 时按 1 处理（0 保留为无效实体）
    static void Reset(LootEntityId nextValue = 1) {
        m_next = nextValue > 0 ? nextValue - 1 : 0;
    }

private:
    inline static LootEntityId m_next = 0; // 0 保留（无效实体）
};

// 地上掉落物：不是 Character（不进 ActorRegistry、不阻挡角色、Player 可踩过）。
// 由 LootManager 统一拥有/更新/过期/拾取/渲染收集。
struct GroundLoot {
    LootEntityId lootEntityId = 0;
    std::string itemId;
    int quantity = 0;
    math::Vector2 position{0.0f, 0.0f}; // Monster Feet Position（死亡位置）
    float age = 0.0f;                   // 存活秒数（TTL 计时，屏幕外也继续计时）
    bool pickupEnabled = true;
};

} // namespace legend::world
