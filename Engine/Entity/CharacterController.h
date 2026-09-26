#pragma once

#include "Engine/Entity/Character.h"
#include "Engine/Entity/Direction8.h"
#include "Engine/Math/Vector2.h"

namespace legend::map {
class Map;
}

namespace legend::entity {

// 角色控制器：移动 / 分轴地图碰撞 / 朝向与 moving 状态更新。
// 输入方向由上层（PlayerController）提供，本类不读取键盘。
class CharacterController {
public:
    // inputDirection：原始输入向量（可为零）；position 语义为 Feet Position
    void Move(Character& character, const math::Vector2& inputDirection, float deltaTime,
              const map::Map& map) const;

    // 指定 Feet Position 处，角色 footprint 是否被阻挡
    bool IsPositionBlocked(const map::Map& map, const CharacterFootprint& footprint,
                           const math::Vector2& feetPosition) const;
};

} // namespace legend::entity
