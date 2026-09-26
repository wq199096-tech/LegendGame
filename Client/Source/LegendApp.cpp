#include "Client/Source/LegendApp.h"

#include <SDL3/SDL.h>

#include <memory>

#include "Client/Source/FallbackScene.h"
#include "Client/Source/GameScene.h"
#include "Engine/Core/Engine.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Map/MapLoader.h"
#include "Shared/Version.h"

const char* LegendApp::GetWindowTitle() const {
    return LEGEND_GAME_TITLE;
}

bool LegendApp::OnInitialize(legend::Engine& engine) {
    // 地图路径允许环境变量覆盖（自动化验收用）
    const char* mapPathEnv = SDL_getenv("LEGEND_CLIENT_MAP");
    const std::string mapPath = (mapPathEnv != nullptr && mapPathEnv[0] != '\0')
                                    ? std::string(mapPathEnv)
                                    : std::string("Assets/Maps/TestMap/map.json");

    auto map = legend::map::MapLoader::Load(mapPath);
    if (!map) {
        LOG_ERROR("Failed to load map '" + mapPath + "'. Entering fallback scene.");
        engine.GetScenes().SetScene(std::make_shared<FallbackScene>());
        return true;
    }

    engine.GetScenes().SetScene(std::make_shared<GameScene>(std::move(map)));
    return true;
}

void LegendApp::OnUpdate(legend::Engine& engine, float deltaTime) {
    (void)deltaTime;

    if (engine.GetInput().IsKeyPressed(SDL_SCANCODE_ESCAPE)) {
        LOG_INFO("ESC pressed. Exiting test program.");
        engine.Quit();
    }
}
