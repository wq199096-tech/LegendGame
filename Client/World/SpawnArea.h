#pragma once

#include <random>

#include "Engine/Animation/AnimationLoader.h"
#include "Engine/Entity/CharacterController.h"
#include "Engine/Map/MapTypes.h"
#include "Engine/Math/Vector2.h"

namespace legend::map {
class Map;
}

namespace legend::world {

// 出生区域工具：在圆形区域内随机寻找合法 walkable 位置。
// 判定复用 CharacterController::IsPositionBlocked（Terrain/Manual/Object 三源合成），
// 不能生成到 Water / Wall / Building / Blocked Tile。
namespace SpawnArea {

// 在 center 附近 radius 范围内随机找一个可行走位置；超过 maxAttempts 失败返回 false（不死循环）
bool FindWalkablePosition(const map::Map& map, const entity::CharacterFootprint& footprint,
                          const math::Vector2& center, float radius, std::mt19937& rng,
                          int maxAttempts, math::Vector2& outPosition);

// 按 map.json 出生区域数据查找（center/radius 来自 MapSpawnArea）
inline bool FindWalkableSpawnPosition(const map::Map& map,
                                      const entity::CharacterFootprint& footprint,
                                      const map::MapSpawnArea& area, std::mt19937& rng,
                                      math::Vector2& outPosition) {
    return FindWalkablePosition(map, footprint, {area.x, area.y}, area.radius, rng, 40,
                                outPosition);
}

} // namespace SpawnArea

} // namespace legend::world
