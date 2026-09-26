#pragma once

#include "Engine/Core/Application.h"

// LegendGame 客户端应用
class LegendApp final : public legend::Application {
protected:
    const char* GetWindowTitle() const override;
    bool OnInitialize(legend::Engine& engine) override;
    void OnUpdate(legend::Engine& engine, float deltaTime) override;
};
