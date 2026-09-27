#pragma once

#include <cstdint>

namespace legend::entity {

// 世界角色类型标识（枚举判断，禁止用 name 字符串判断）
// 本阶段实际使用：Player / NPC / Monster；Pet / Summon 为后续扩展预留
enum class ActorType : uint8_t {
    Player = 0,
    NPC = 1,
    Monster = 2,
    Pet = 3,
    Summon = 4,
};

} // namespace legend::entity
