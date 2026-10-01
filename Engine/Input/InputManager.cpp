#include "Engine/Input/InputManager.h"

#include "Engine/Debug/Logger.h"

namespace legend::input {

void InputManager::BeginFrame() {
    float x = 0.0f;
    float y = 0.0f;
    SDL_GetMouseState(&x, &y);
    m_mousePosition = {x, y};
    m_frameTextInput.clear();
}

void InputManager::ProcessEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            if (!m_keyDown[event.key.scancode]) {
                m_keyPressed[event.key.scancode] = true;
            }
            m_keyDown[event.key.scancode] = true;
            break;

        case SDL_EVENT_KEY_UP:
            m_keyDown[event.key.scancode] = false;
            m_keyReleased[event.key.scancode] = true;
            break;

        case SDL_EVENT_TEXT_INPUT:
            // Stage26 指令三十一：文本会话开启时透传 IME 提交文本（登录/角色名）。
            if (m_textInputDepth > 0 && event.text.text != nullptr && event.text.text[0] != '\0') {
                m_frameTextInput.emplace_back(event.text.text);
                LOG_INFO("[TextInput] frame text received (depth=" +
                         std::to_string(m_textInputDepth) + ")");
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button >= 1 && event.button.button < kMouseButtonCount) {
                if (!m_mouseDown[event.button.button]) {
                    m_mousePressed[event.button.button] = true;
                }
                m_mouseDown[event.button.button] = true;
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button >= 1 && event.button.button < kMouseButtonCount) {
                m_mouseDown[event.button.button] = false;
                m_mouseReleased[event.button.button] = true;
            }
            break;

        case SDL_EVENT_MOUSE_MOTION:
            m_mousePosition = {event.motion.x, event.motion.y};
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            m_mouseWheelDelta += event.wheel.y;
            break;

        default:
            break;
    }
}

void InputManager::EndFrame() {
    m_keyPressed.fill(false);
    m_keyReleased.fill(false);
    m_mousePressed.fill(false);
    m_mouseReleased.fill(false);
    m_mouseWheelDelta = 0.0f;
}

bool InputManager::IsMouseButtonDown(Uint8 button) const {
    return button >= 1 && button < kMouseButtonCount && m_mouseDown[button];
}

bool InputManager::IsMouseButtonPressed(Uint8 button) const {
    return button >= 1 && button < kMouseButtonCount && m_mousePressed[button];
}

bool InputManager::IsMouseButtonReleased(Uint8 button) const {
    return button >= 1 && button < kMouseButtonCount && m_mouseReleased[button];
}

void InputManager::BeginTextInput() {
    ++m_textInputDepth;
    if (m_textInputDepth == 1) {
        // SDL3：文本输入按窗口开启（优先注入的主窗口，回退键盘焦点窗口）。
        SDL_Window* window = m_targetWindow != nullptr ? m_targetWindow : SDL_GetKeyboardFocus();
        if (window != nullptr) {
            SDL_StartTextInput(window);
            LOG_INFO("[TextInput] session started (active=" +
                     std::string(SDL_TextInputActive(window) ? "1" : "0") + ")");
        } else {
            LOG_WARN("[TextInput] start skipped: no target window");
        }
    }
}

void InputManager::EndTextInput() {
    if (m_textInputDepth > 0) {
        --m_textInputDepth;
        if (m_textInputDepth == 0) {
            SDL_Window* window = m_targetWindow != nullptr ? m_targetWindow : SDL_GetKeyboardFocus();
            if (window != nullptr) {
                SDL_StopTextInput(window);
            }
            m_frameTextInput.clear();
        }
    }
}

} // namespace legend::input
