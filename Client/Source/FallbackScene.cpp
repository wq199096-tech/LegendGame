#include "Client/Source/FallbackScene.h"

#include <memory>

#include "Engine/Core/Engine.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Math/Color.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/SpriteRenderer.h"

void FallbackScene::OnLoad() {
    LOG_ERROR("FallbackScene active: map could not be loaded. Check Logs/latest.log for details.");

    auto& resources = legend::Engine::Get().GetResources();
    auto placeholder = resources.GetPlaceholderTexture();
    auto backdrop = resources.CreateSolidTexture(
        "internal/fallback_bg", 64, legend::math::Color::FromRGBA8(30, 30, 34));

    if (backdrop) {
        auto* bg = CreateGameObject("Backdrop");
        auto* sprite = bg->AddComponent<legend::scene::SpriteRenderer>();
        sprite->SetTexture(backdrop);
        sprite->SetRenderOrder(-100);
        bg->GetTransform().SetPosition({320.0f, 180.0f});
        bg->GetTransform().SetScale({20.0f, 12.0f});
    }
    if (placeholder) {
        auto* marker = CreateGameObject("ErrorMarker");
        auto* sprite = marker->AddComponent<legend::scene::SpriteRenderer>();
        sprite->SetTexture(placeholder);
        sprite->SetRenderOrder(0);
        marker->GetTransform().SetPosition({320.0f, 180.0f});
        marker->GetTransform().SetScale({3.0f, 3.0f});
    }
}

void FallbackScene::Update(float deltaTime) {
    (void)deltaTime; // 无逻辑：等待用户退出
}
