#pragma once

#include <cstdint>
#include <string>

#include "Engine/Math/Vector2.h"

namespace legend::world {

// 掉落实体 ID：独立分配器（不与 EntityId / ItemInstanceId 混用）
using LootEntityId = std::uint64_t;

class LootEntityIdAllocator {
public:
    static LootEntityId Next() {
        return ++m_next;
    }
    static void Reset(LootEntityId start = 1) {
        m_next = start > 0 ? start : 1;
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
