#pragma once

#include <string>

struct SDL_Window;

namespace legend {

// 对 SDL3 窗口的薄封装
class Window {
public:
    bool Create(const std::string& title, int width, int height);
    void Destroy();

    SDL_Window* GetHandle() const { return m_window; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    const std::string& GetTitle() const { return m_title; }

    void SetTitle(const std::string& title);

private:
    SDL_Window* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    std::string m_title;
};

} // namespace legend
