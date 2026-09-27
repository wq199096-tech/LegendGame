#include "Client/Character/PlayerController.h"

#include <SDL3/SDL_scancode.h>

#include "Engine/Input/InputManager.h"
#include "Engine/Map/Map.h"

void PlayerController::Update(legend::input::InputManager& input,
                              legend::entity::CharacterController& controller,
                              legend::entity::Character& character, const legend::map::Map& map,
                              float deltaTime) {
    // 阶段8 MovementLock：Attacking/HitReact/Dead/SkillCasting 状态下不接受移动
    // （GameScene 分支已跳过本调用，此处为第二道防线，指令二十三）
    if (character.GetActionState() != legend::entity::CharacterActionState::Normal) {
        return;
    }
    // WASD 八方向输入
    float inputX = (input.IsKeyDown(SDL_SCANCODE_D) ? 1.0f : 0.0f) -
                   (input.IsKeyDown(SDL_SCANCODE_A) ? 1.0f : 0.0f);
    float inputY = (input.IsKeyDown(SDL_SCANCODE_S) ? 1.0f : 0.0f) -
                   (input.IsKeyDown(SDL_SCANCODE_W) ? 1.0f : 0.0f);

    if (m_virtualInput.LengthSq() > 0.0f) {
        inputX = m_virtualInput.x;
        inputY = m_virtualInput.y;
    }

    controller.Move(character, {inputX, inputY}, deltaTime, map);
}
