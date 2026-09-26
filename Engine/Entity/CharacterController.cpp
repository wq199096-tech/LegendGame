#include "Engine/Entity/CharacterController.h"

#include "Engine/Map/Map.h"

namespace legend::entity {

bool CharacterController::IsPositionBlocked(const map::Map& map, const CharacterFootprint& footprint,
                                            const math::Vector2& feetPosition) const {
    const float centerX = feetPosition.x + footprint.offsetX;
    const float centerY = feetPosition.y + footprint.offsetY;
    const float halfW = footprint.width * 0.5f;
    const float halfH = footprint.height * 0.5f;
    // 脚底 AABB 四角采样
    return map.IsWorldBlocked(centerX - halfW, centerY - halfH) ||
           map.IsWorldBlocked(centerX + halfW, centerY - halfH) ||
           map.IsWorldBlocked(centerX - halfW, centerY + halfH) ||
           map.IsWorldBlocked(centerX + halfW, centerY + halfH);
}

void CharacterController::Move(Character& character, const math::Vector2& inputDirection,
                               float deltaTime, const map::Map& map) const {
    math::Vector2 direction = inputDirection;
    const bool hasInput = direction.LengthSq() > 0.0f;

    // 输入非零时朝向必须更新（即使撞墙不能移动）
    if (hasInput) {
        character.SetDirection(DirectionFromVector(direction, character.GetDirection()));
        direction = direction.Normalized();
    }

    const float step = character.GetMoveSpeed() * deltaTime;
    const math::Vector2 feet = character.GetPosition();
    math::Vector2 moved = feet;
    bool actuallyMoved = false;

    if (hasInput) {
        // X/Y 分轴碰撞：斜向撞墙时沿墙滑动
        const math::Vector2 stepX(moved.x + direction.x * step, moved.y);
        if (!IsPositionBlocked(map, character.GetFootprint(), stepX)) {
            moved = stepX;
            actuallyMoved = true;
        }
        const math::Vector2 stepY(moved.x, moved.y + direction.y * step);
        if (!IsPositionBlocked(map, character.GetFootprint(), stepY)) {
            moved = stepY;
            actuallyMoved = true;
        }
    }

    character.SetPosition(moved);
    character.SetVelocity(actuallyMoved ? direction * character.GetMoveSpeed()
                                        : math::Vector2{0.0f, 0.0f});
    // 撞墙原地时 moving=false -> Idle，避免墙边原地踏步
    character.SetMoving(actuallyMoved);
}

} // namespace legend::entity
