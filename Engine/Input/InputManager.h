#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>

#include <array>
#include <string>
#include <vector>

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

    // ---- Stage26 指令三十一：文本输入（登录/注册/角色名；SDL 文本事件透传）----
    // 开启后 SDL 开始派发 SDL_EVENT_TEXT_INPUT（Windows IME 中文输入可用）；
    // 嵌套计数允许页面/调试窗口共存。
    // 目标窗口必须显式注入（Engine 初始化时）——SDL_StartTextInput 需要窗口，
    // 依赖 GetKeyboardFocus 会在首帧焦点未建立时静默失效。
    void SetTargetWindow(SDL_Window* window) { m_targetWindow = window; }
    void BeginTextInput();
    void EndTextInput();
    bool IsTextInputActive() const { return m_textInputDepth > 0; }
    // 本帧收到的 UTF-8 文本片段（IME 提交后的完整字符串；每帧清空）。
    const std::vector<std::string>& FrameTextInput() const { return m_frameTextInput; }

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

    int m_textInputDepth = 0;
    std::vector<std::string> m_frameTextInput;
    SDL_Window* m_targetWindow = nullptr;
};

} // namespace legend::input
