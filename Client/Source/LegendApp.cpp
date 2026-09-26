#include "Client/Source/LegendApp.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>

#include "Client/Source/TestScene.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Core/Engine.h"
#include "Shared/Version.h"

const char* LegendApp::GetWindowTitle() const {
    return LEGEND_GAME_TITLE;
}

bool LegendApp::OnInitialize(legend::Engine& engine) {
    engine.GetScenes().SetScene(std::make_shared<TestScene>());
    return true;
}

void LegendApp::OnUpdate(legend::Engine& engine, float deltaTime) {
    (void)deltaTime;

    if (engine.GetInput().IsKeyPressed(SDL_SCANCODE_ESCAPE)) {
        LOG_INFO("ESC pressed. Exiting test program.");
        engine.Quit();
    }
}
