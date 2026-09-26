#include "Engine/Core/Application.h"

#include "Engine/Core/Engine.h"
#include "Engine/Debug/Logger.h"

namespace legend {

int Application::Run() {
    if (!Engine::Get().Initialize(GetWindowTitle(), 1280, 720)) {
        LOG_ERROR("Application aborted: engine initialization failed.");
        return 1;
    }

    Engine& engine = Engine::Get();
    if (!OnInitialize(engine)) {
        LOG_ERROR("Application aborted: application initialization failed.");
        engine.Shutdown();
        return 1;
    }

    engine.SetUpdateCallback([this](float deltaTime) {
        OnUpdate(Engine::Get(), deltaTime);
    });

    engine.Run();
    engine.Shutdown();
    return 0;
}

} // namespace legend
