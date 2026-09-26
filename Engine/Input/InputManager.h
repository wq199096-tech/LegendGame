#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>

#include <array>

#include "Engine/Math/Vector2.h"

namespace legend::input {

// 统一输入管理：所有 SDL 事件先进入这里，游戏逻辑只查询状态
class InputManager {
public:
    // 每帧开始时调用（事件轮询前）
    void BeginFrame();
    // 每条 SDL 事件调用
    void ProcessEvent(const SDL_Event& event);
    // 每帧结束时调用（清除“本帧按下/释放”状态）
    void EndFrame();

    bool IsKeyDown(SDL_Scancode key) const { return m_keyDown[key]; }
    bool IsKeyPressed(SDL_Scancode key) const { return m_keyPressed[key]; }   // 本帧按下
    bool IsKeyReleased(SDL_Scancode key) const { return m_keyReleased[key]; } // 本帧释放

    bool IsMouseButtonDown(Uint8 button) const;
    bool IsMouseButtonPressed(Uint8 button) const;
    bool IsMouseButtonReleased(Uint8 button) const;

    const math::Vector2& GetMousePosition() const { return m_mousePosition; }
    float GetMouseWheelDelta() const { return m_mouseWheelDelta; } // 本帧滚轮增量

private:
    static constexpr int kMouseButtonCount = 4; // SDL 按钮 1=左 2=中 3=右

    std::array<bool, SDL_SCANCODE_COUNT> m_keyDown{};
    std::array<bool, SDL_SCANCODE_COUNT> m_keyPressed{};
    std::array<bool, SDL_SCANCODE_COUNT> m_keyReleased{};

    std::array<bool, kMouseButtonCount> m_mouseDown{};
    std::array<bool, kMouseButtonCount> m_mousePressed{};
    std::array<bool, kMouseButtonCount> m_mouseReleased{};

    math::Vector2 m_mousePosition{0.0f, 0.0f};
    float m_mouseWheelDelta = 0.0f;
};

} // namespace legend::input
