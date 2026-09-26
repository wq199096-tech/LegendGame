#include "Engine/Core/Window.h"

#include "Engine/Debug/Logger.h"

#include <SDL3/SDL.h>

namespace legend {

bool Window::Create(const std::string& title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR(std::string("SDL initialization failed: ") + SDL_GetError());
        return false;
    }
    LOG_INFO("SDL initialized.");

    // OpenGL 属性必须在创建窗口之前设置
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    m_window = SDL_CreateWindow(title.c_str(), width, height,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (m_window == nullptr) {
        LOG_ERROR(std::string("Window creation failed: ") + SDL_GetError());
        return false;
    }

    m_width = width;
    m_height = height;
    m_title = title;
    LOG_INFO("Window created: " + title +
             " (" + std::to_string(width) + "x" + std::to_string(height) + ")");
    return true;
}

void Window::Destroy() {
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        SDL_Quit();
        LOG_INFO("Window destroyed, SDL quit.");
    }
}

void Window::SetTitle(const std::string& title) {
    m_title = title;
    if (m_window != nullptr) {
        SDL_SetWindowTitle(m_window, title.c_str());
    }
}

} // namespace legend
