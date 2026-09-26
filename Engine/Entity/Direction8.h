#pragma once

#include <cstdint>

#include "Engine/Math/Vector2.h"

namespace legend::entity {

// 8 方向枚举（y 轴向下：North = -y）
enum class Direction8 : uint8_t {
    South = 0,
    SouthWest,
    West,
    NorthWest,
    North,
    NorthEast,
    East,
    SouthEast,
};

// 枚举的小写名称（用于动画 Clip 名：idle_south / walk_northeast ...）
const char* Direction8Name(Direction8 direction);

// 向量 -> 8 方向；零向量返回 fallback（保留上一次朝向）
Direction8 DirectionFromVector(const math::Vector2& vector, Direction8 fallback);

} // namespace legend::entity
