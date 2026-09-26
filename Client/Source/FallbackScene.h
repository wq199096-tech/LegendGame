#pragma once

#include "Engine/Scene/Scene.h"

// 地图加载失败时的安全回退场景：不崩溃，给出明显错误视觉提示
class FallbackScene final : public legend::scene::Scene {
public:
    FallbackScene() : legend::scene::Scene("FallbackScene") {}

    void OnLoad() override;
    void Update(float deltaTime) override;
};
