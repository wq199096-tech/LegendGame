#include "Engine/Core/Engine.h"

#include "Shared/Version.h"

#include <SDL3/SDL.h>

#include "Engine/Debug/Logger.h"

namespace legend {

Engine& Engine::Get() {
    static Engine instance;
    return instance;
}

bool Engine::Initialize(const std::string& windowTitle, int windowWidth, int windowHeight) {
    if (m_initialized) {
        LOG_WARN("Engine::Initialize called twice, ignored.");
        return true;
    }

    debug::Logger::Init("Logs");
    LOG_INFO("==================================================");
    LOG_INFO(std::string("LegendGame Engine V") + LEGEND_ENGINE_VERSION + " starting up.");
    LOG_INFO("==================================================");

    if (!m_window.Create(windowTitle, windowWidth, windowHeight)) {
        LOG_ERROR("Engine initialization failed: could not create window.");
        return false;
    }

    if (!m_renderer.Initialize(m_window.GetHandle())) {
        LOG_ERROR("Engine initialization failed: could not initialize renderer.");
        return false;
    }

    m_resources.Initialize("Assets");

    m_title = windowTitle;
    m_window.SetTitle(m_title);

    m_initialized = true;
    m_running = true;
    LOG_INFO("Engine initialized.");
    return true;
}

void Engine::Run() {
    if (!m_initialized) {
        LOG_ERROR("Engine::Run called before successful initialization.");
        return;
    }

    LOG_INFO("Entering main loop.");

    Timer frameTimer;
    frameTimer.Reset();

    while (m_running) {
        // 1. 输入：轮询全部 SDL 事件并交给 InputManager
        m_input.BeginFrame();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                LOG_INFO("Quit requested (window closed).");
                m_running = false;
            } else {
                m_input.ProcessEvent(event);
            }
        }

        // 2. DeltaTime（限制最大步长，防止断点/卡顿导致跳帧）
        double deltaTime = frameTimer.GetElapsedSeconds();
        frameTimer.Reset();
        if (deltaTime > 0.25) {
            deltaTime = 0.25;
        }
        if (deltaTime < 0.0) {
            deltaTime = 0.0;
        }

        // 3. 应用层逻辑 -> 场景更新
        if (m_updateCallback) {
            m_updateCallback(static_cast<float>(deltaTime));
        }
        m_scenes.Update(static_cast<float>(deltaTime));

        // 4. 渲染 + Present
        m_scenes.Render(m_renderer, m_camera);
        m_renderer.EndFrame();

        // 5. 清除本帧输入状态
        m_input.EndFrame();

        // 6. FPS 统计（约每 0.5 秒刷新窗口标题）
        m_fpsAccumulated += deltaTime;
        ++m_fpsFrames;
        if (m_fpsAccumulated >= 0.5) {
            UpdateFpsWindowTitle(m_fpsAccumulated);
            m_fpsAccumulated = 0.0;
            m_fpsFrames = 0;
        }
    }

    LOG_INFO("Leaving main loop.");
}

void Engine::UpdateFpsWindowTitle(double elapsedSeconds) {
    const int fps = static_cast<int>(m_fpsFrames / elapsedSeconds + 0.5);
    m_window.SetTitle(m_title + " | FPS: " + std::to_string(fps));
}

void Engine::Shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Engine shutting down.");
    m_scenes.Shutdown();
    m_resources.Shutdown();
    m_renderer.Shutdown();
    m_window.Destroy();

    m_initialized = false;
    m_running = false;
    debug::Logger::Shutdown();
}

} // namespace legend
