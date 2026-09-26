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
        debug::Logger::Shutdown();
        return false;
    }

    if (!m_renderer.Initialize(m_window.GetHandle())) {
        LOG_ERROR("Engine initialization failed: could not initialize renderer.");
        m_window.Destroy();
        debug::Logger::Shutdown();
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
        m_totalElapsed += deltaTime;

        // 3. 应用层逻辑 -> 场景更新
        if (m_updateCallback) {
            m_updateCallback(static_cast<float>(deltaTime));
        }
        m_scenes.Update(static_cast<float>(deltaTime));

        // 4. 渲染：设置每帧状态 -> 绘制场景 -> Present
        m_renderer.BeginFrame(m_camera);
        m_scenes.Render(m_renderer, m_camera);
        HandleDebugCapture();
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
    std::string title = m_title;
    if (!m_statusText.empty()) {
        title += " | " + m_statusText;
    }
    title += " | FPS: " + std::to_string(static_cast<int>(m_fpsFrames / elapsedSeconds + 0.5));
    m_window.SetTitle(title);
}

void Engine::HandleDebugCapture() {
    if (!m_shotTimesParsed) {
        m_shotTimesParsed = true;
        const char* screenshotPath = SDL_getenv("LEGEND_AUTO_SCREENSHOT");
        if (screenshotPath == nullptr || screenshotPath[0] == '\0') {
            return;
        }
        m_screenshotBase = screenshotPath;
        std::string times = "2.5";
        const char* timesEnv = SDL_getenv("LEGEND_AUTO_SHOT_TIMES");
        if (timesEnv != nullptr && timesEnv[0] != '\0') {
            times = timesEnv;
        }
        size_t start = 0;
        while (start <= times.size()) {
            const size_t comma = times.find(',', start);
            const std::string token = times.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start);
            try {
                const double t = std::stod(token);
                if (t > 0.0) {
                    m_pendingShots.push_back(t);
                }
            } catch (...) {
                // 忽略非法时刻
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
        m_totalShotCount = static_cast<int>(m_pendingShots.size());
        if (m_totalShotCount > 0) {
            LOG_INFO("Debug screenshots scheduled: " + std::to_string(m_totalShotCount));
        }
    }
    if (m_pendingShots.empty()) {
        return;
    }

    bool captured = false;
    while (!m_pendingShots.empty() && m_totalElapsed >= m_pendingShots.front()) {
        m_pendingShots.pop_front();
        ++m_shotIndex;

        std::string path = m_screenshotBase;
        if (m_totalShotCount > 1) {
            const size_t dot = path.find_last_of('.');
            const std::string suffix = "_" + std::to_string(m_shotIndex);
            path = (dot == std::string::npos)
                       ? path + suffix
                       : path.substr(0, dot) + suffix + path.substr(dot);
        }
        if (m_renderer.CaptureScreenshot(path)) {
            LOG_INFO("Debug screenshot saved: " + path);
        } else {
            LOG_ERROR(std::string("Debug screenshot failed: ") + path);
        }
        captured = true;
    }

    if (captured && m_pendingShots.empty()) {
        if (SDL_getenv("LEGEND_AUTO_QUIT") != nullptr) {
            LOG_INFO("LEGEND_AUTO_QUIT detected, quitting after screenshots.");
            m_running = false;
        }
    }
}

void Engine::Shutdown() {
    if (m_shutdownCompleted) {
        return;
    }
    m_shutdownCompleted = true;

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
