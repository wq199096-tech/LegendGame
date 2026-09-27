#pragma once

#include <cstdint>

#include "Engine/Entity/EntityId.h"
#include "Engine/Math/Vector2.h"

namespace legend::combat {

// 死亡事件：HP 从 >0 变成 0 的那次 Damage 中产生一次（不在每帧检查中重复产生）。
// WorldActorManager/RewardSystem 一次消费（exactly-once）：
// victim=Monster + killer=Player -> Exp + Loot。
// position 为 victim 死亡时刻 Feet 位置（避免上层 Resolve 时对象已注销）。
struct DeathEvent {
    legend::entity::EntityId victimId = legend::entity::kInvalidEntityId;
    legend::entity::EntityId killerId = legend::entity::kInvalidEntityId;
    legend::math::Vector2 position{0.0f, 0.0f};
    std::uint64_t sequence = 0;
};

} // namespace legend::combat
