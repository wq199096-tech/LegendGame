#pragma once

// ---------------------------------------------------------------------------
// Stage27 指令二十一：ChatUiTheme —— 聊天窗口配色/尺寸统一表（参考分辨率
// 1920x1080，随 UiTheme::ScaleFor 缩放）。频道颜色不散落写死，全部集中这里；
// 具体色值体系沿用 UiTheme 的深色金边风格。
// ---------------------------------------------------------------------------

#include "Engine/Math/Color.h"

#include <cstdint>

namespace legend::ui {

struct ChatUiTheme {
    // ---- 窗口 ----
    math::Color panelBack{0.06f, 0.07f, 0.11f, 0.62f};   // 半透明底板（指令十九）
    math::Color panelBorder{0.78f, 0.62f, 0.28f, 0.85f}; // 金边
    math::Color tabActive{0.92f, 0.78f, 0.38f, 1.0f};    // 选中标签（金）
    math::Color tabInactive{0.58f, 0.62f, 0.70f, 1.0f};
    math::Color inputBack{0.10f, 0.12f, 0.17f, 0.95f};   // 输入栏底
    math::Color inputText{0.92f, 0.94f, 0.97f, 1.0f};
    math::Color hintText{0.50f, 0.54f, 0.62f, 0.9f};
    math::Color buttonNormal{0.16f, 0.18f, 0.26f, 0.95f};
    math::Color buttonHover{0.24f, 0.27f, 0.38f, 1.0f};
    math::Color popupBack{0.08f, 0.09f, 0.14f, 0.96f};   // 点击名字小菜单

    // ---- 频道颜色（指令二十一示例；统一走 UiTheme 体系）----
    math::Color nearbyColor{0.86f, 0.88f, 0.92f, 1.0f};  // 附近：普通浅色
    math::Color worldColor{0.96f, 0.80f, 0.35f, 1.0f};   // 世界：金色
    math::Color whisperColor{0.80f, 0.60f, 0.95f, 1.0f}; // 私聊：紫色
    math::Color systemColor{0.55f, 0.90f, 0.55f, 1.0f};  // 系统：绿色
    math::Color errorColor{0.95f, 0.42f, 0.40f, 1.0f};   // 错误：红色

    // ---- 布局（参考分辨率像素；渲染时乘 scale）----
    float collapsedW = 210.0f;
    float collapsedH = 26.0f;
    float expandedW = 460.0f;
    float expandedH = 300.0f;
    float tabBarH = 26.0f;
    float inputRowH = 30.0f;
    float lineH = 19.0f;
    float textFontSize = 14.0f;
    float nameFontSize = 14.0f;
    int maxVisibleLines = 10;

    // 频道标签文案（指令十九：综合 显示所有）。
    static const char* TabLabel(std::uint8_t tab);
    // 指令二十二：消息格式前缀（[附近]/[世界]/[私聊]/[系统]）。
    static const char* ChannelTag(std::uint8_t channel);
};

ChatUiTheme& DefaultChatUiTheme();

} // namespace legend::ui
