#pragma once

#include "Engine/Entity/CharacterController.h"

namespace legend::input {
class InputManager;
}

namespace legend::map {
class Map;
}

// 玩家控制器：读取 InputManager 生成 moveVector，交给 CharacterController。
// 管线：InputManager -> PlayerController -> CharacterController -> Character -> Map Collision -> Position
class PlayerController {
public:
    // 自动化测试钩子：非零时替代真实键盘输入
    void SetVirtualInput(const legend::math::Vector2& input) { m_virtualInput = input; }

    void Update(legend::input::InputManager& input, legend::entity::CharacterController& controller,
                legend::entity::Character& character, const legend::map::Map& map, float deltaTime);

private:
    legend::math::Vector2 m_virtualInput{0.0f, 0.0f};
};
