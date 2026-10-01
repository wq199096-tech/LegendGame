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

    // Stage26 指令三十五：Esc 双绑定收编——全局 Esc 退出移除。
    // Esc 语义归流程/场景层：流程页 = 返回上级（GameScene::Update），游戏内 =
    // Settings 开关。退出程序走窗口关闭按钮（SDL QUIT 事件，Engine 已处理）。
    (void)engine;
}
