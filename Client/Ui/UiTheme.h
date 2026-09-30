#pragma once

#include "Engine/Math/Color.h"

#include <cstdint>

namespace legend::ui {

// ---------------------------------------------------------------------------
// 阶段25 指令六十一：UiThemeDefinition —— 集中管理 UI 颜色/尺寸（以后整体换皮）。
// 指令六十三：参考分辨率 1920×1080，UI 按窗口大小缩放（不硬编码屏幕绝对坐标）。
// 指令六十：UI 颜色/尺寸不散落 GameScene.cpp。
// ---------------------------------------------------------------------------
struct UiTheme {
    // 参考分辨率（UI 布局坐标系）。
    static constexpr float kReferenceWidth = 1920.0f;
    static constexpr float kReferenceHeight = 1080.0f;

    // ---- 面板 ----
    math::Color panelBackground{0.08f, 0.10f, 0.14f, 0.82f};
    math::Color panelBorder{0.55f, 0.45f, 0.20f, 0.95f};
    math::Color panelSlot{0.16f, 0.19f, 0.26f, 0.95f};
    math::Color panelSlotSelected{0.30f, 0.38f, 0.55f, 1.0f};

    // ---- 文本 ----
    math::Color textPrimary{0.92f, 0.90f, 0.85f, 1.0f};
    math::Color textDim{0.62f, 0.62f, 0.66f, 1.0f};
    math::Color textGold{1.0f, 0.84f, 0.35f, 1.0f};
    math::Color textError{1.0f, 0.42f, 0.35f, 1.0f};
    math::Color textSuccess{0.55f, 0.95f, 0.55f, 1.0f};
    math::Color textWarning{1.0f, 0.75f, 0.30f, 1.0f};

    // ---- 条 ----
    math::Color hpFill{0.78f, 0.20f, 0.22f, 1.0f};
    math::Color manaFill{0.22f, 0.42f, 0.90f, 1.0f};
    math::Color expFill{0.85f, 0.68f, 0.20f, 1.0f};
    math::Color barBack{0.12f, 0.12f, 0.16f, 0.9f};
    math::Color bossHpFill{0.70f, 0.15f, 0.45f, 1.0f};

    // ---- 按钮（指令六十一：Button states）----
    math::Color buttonNormal{0.22f, 0.26f, 0.36f, 0.95f};
    math::Color buttonHover{0.32f, 0.38f, 0.52f, 1.0f};
    math::Color buttonPressed{0.16f, 0.19f, 0.27f, 1.0f};
    math::Color buttonDisabled{0.16f, 0.17f, 0.20f, 0.70f};

    // ---- Toast（指令五十六）----
    math::Color toastInfo{0.25f, 0.45f, 0.75f, 0.9f};
    math::Color toastSuccess{0.22f, 0.60f, 0.30f, 0.9f};
    math::Color toastWarning{0.75f, 0.55f, 0.15f, 0.9f};
    math::Color toastError{0.75f, 0.22f, 0.18f, 0.9f};

    // ---- 地图横幅 / 升级 ----
    math::Color bannerText{0.95f, 0.92f, 0.80f, 1.0f};
    math::Color levelUpText{1.0f, 0.85f, 0.25f, 1.0f};

    // ---- 尺寸（参考分辨率坐标；渲染时乘 scale）----
    float hudBarWidth = 260.0f;
    float hudBarHeight = 14.0f;
    float skillSlotSize = 52.0f;
    float inventoryCellSize = 44.0f;
    float toastWidth = 420.0f;

    // UI 缩放：min(w/refW, h/refH)，clamp 0.5~2.0（1280×720 → ~0.67，
    // 1920×1080 → 1.0，2560×1440 → ~1.19；指令六十二：三种分辨率可用）。
    static float ScaleFor(float viewportWidth, float viewportHeight) {
        const float sx = viewportWidth / kReferenceWidth;
        const float sy = viewportHeight / kReferenceHeight;
        float s = sx < sy ? sx : sy;
        if (s < 0.5f) {
            s = 0.5f;
        }
        if (s > 2.0f) {
            s = 2.0f;
        }
        return s;
    }
};

// 默认主题（阶段25 开发占位皮肤；换正式皮肤 = 替换 UiTheme 数据，不改渲染代码）。
const UiTheme& DefaultUiTheme();

} // namespace legend::ui
