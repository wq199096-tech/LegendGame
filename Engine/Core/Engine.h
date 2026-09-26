#pragma once

#include <functional>
#include <string>

#include "Engine/Core/Timer.h"
#include "Engine/Core/Window.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Render/Camera2D.h"
#include "Engine/Render/Renderer.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Scene/SceneManager.h"

namespace legend {

// 引擎核心：持有全部子系统，负责生命周期与主循环
// main -> Application::Run -> Engine::Initialize -> Run(主循环) -> Shutdown
class Engine {
public:
    static Engine& Get();

    bool Initialize(const std::string& windowTitle, int windowWidth, int windowHeight);
    void Run();
    void Shutdown();

    void Quit() { m_running = false; }
    bool IsRunning() const { return m_running; }

    Window& GetWindow() { return m_window; }
    render::Renderer& GetRenderer() { return m_renderer; }
    input::InputManager& GetInput() { return m_input; }
    resource::ResourceManager& GetResources() { return m_resources; }
    scene::SceneManager& GetScenes() { return m_scenes; }
    render::Camera2D& GetCamera() { return m_camera; }

    // 应用层每帧回调（在场景更新前调用）
    void SetUpdateCallback(std::function<void(float)> callback) {
        m_updateCallback = std::move(callback);
    }

    // 附加状态文本（显示在窗口标题 FPS 之前，如地图统计）
    void SetStatusText(const std::string& text) { m_statusText = text; }

private:
    Engine() = default;

    void UpdateFpsWindowTitle(double elapsedSeconds);
    // 调试验证：环境变量 LEGEND_AUTO_SCREENSHOT 触发截图，LEGEND_AUTO_QUIT 触发退出
    void HandleDebugCapture();

    Window m_window;
    render::Renderer m_renderer;
    input::InputManager m_input;
    resource::ResourceManager m_resources;
    scene::SceneManager m_scenes;
    render::Camera2D m_camera;

    std::function<void(float)> m_updateCallback;
    std::string m_title;
    std::string m_statusText;
    bool m_initialized = false;
    bool m_running = false;
    bool m_shutdownCompleted = false;

    double m_totalElapsed = 0.0;
    bool m_debugCaptureDone = false;

    double m_fpsAccumulated = 0.0;
    int m_fpsFrames = 0;
};

} // namespace legend
