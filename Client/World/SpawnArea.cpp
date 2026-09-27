#include "Client/World/SpawnArea.h"

#include <cmath>

#include "Engine/Debug/Logger.h"
#include "Engine/Map/Map.h"

namespace legend::world {

namespace SpawnArea {

bool FindWalkablePosition(const map::Map& map, const entity::CharacterFootprint& footprint,
                          const math::Vector2& center, float radius, std::mt19937& rng,
                          int maxAttempts, math::Vector2& outPosition) {
    if (maxAttempts <= 0) {
        maxAttempts = 1;
    }
    const entity::CharacterController controller;
    std::uniform_real_distribution<float> offset(-1.0f, 1.0f);
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        // 圆形均匀分布：随机角度 + sqrt(random)*radius
        const float angle = offset(rng) * 3.14159265f; // [-pi, pi)
        const float dist = std::sqrt((offset(rng) + 1.0f) * 0.5f) * radius;
        const math::Vector2 candidate(center.x + std::cos(angle) * dist,
                                      center.y + std::sin(angle) * dist);
        if (!controller.IsPositionBlocked(map, footprint, candidate)) {
            outPosition = candidate;
            return true;
        }
    }
    return false;
}

} // namespace SpawnArea

} // namespace legend::world
