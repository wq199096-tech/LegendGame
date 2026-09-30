#include "Tools/MapEditor/Source/EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <filesystem>

namespace legend::editor::theme {
namespace {

float g_scale = 1.0f;

constexpr ImVec4 Rgb(int r, int g, int b, int a = 255) {
    return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                  static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f);
}

const ImVec4 kGold = Rgb(216, 170, 85);
const ImVec4 kBlue = Rgb(77, 159, 234);
const ImVec4 kSuccess = Rgb(88, 183, 120);
const ImVec4 kWarning = Rgb(217, 154, 71);
const ImVec4 kError = Rgb(216, 91, 91);
const ImVec4 kMutedText = Rgb(169, 175, 184);

} // namespace

void Apply(float scale) {
    g_scale = std::clamp(scale, 1.0f, 2.0f);
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);

    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.WindowRounding = 4.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = Rgb(232, 233, 235);
    c[ImGuiCol_TextDisabled] = Rgb(111, 118, 128);
    c[ImGuiCol_WindowBg] = Rgb(23, 25, 29);
    c[ImGuiCol_ChildBg] = Rgb(30, 33, 38);
    c[ImGuiCol_PopupBg] = Rgb(36, 40, 47);
    c[ImGuiCol_Border] = Rgb(58, 64, 74);
    c[ImGuiCol_BorderShadow] = Rgb(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = Rgb(42, 47, 55);
    c[ImGuiCol_FrameBgHovered] = Rgb(50, 56, 68);
    c[ImGuiCol_FrameBgActive] = Rgb(58, 64, 74);
    c[ImGuiCol_TitleBg] = Rgb(30, 33, 38);
    c[ImGuiCol_TitleBgActive] = Rgb(36, 40, 47);
    c[ImGuiCol_TitleBgCollapsed] = Rgb(30, 33, 38);
    c[ImGuiCol_MenuBarBg] = Rgb(23, 25, 29);
    c[ImGuiCol_ScrollbarBg] = Rgb(23, 25, 29);
    c[ImGuiCol_ScrollbarGrab] = Rgb(58, 64, 74);
    c[ImGuiCol_ScrollbarGrabHovered] = Rgb(72, 80, 92);
    c[ImGuiCol_ScrollbarGrabActive] = Rgb(216, 170, 85);
    c[ImGuiCol_CheckMark] = kGold;
    c[ImGuiCol_SliderGrab] = kGold;
    c[ImGuiCol_SliderGrabActive] = Rgb(240, 198, 106);
    c[ImGuiCol_Button] = Rgb(42, 47, 55);
    c[ImGuiCol_ButtonHovered] = Rgb(50, 56, 68);
    c[ImGuiCol_ButtonActive] = Rgb(216, 170, 85);
    c[ImGuiCol_Header] = Rgb(42, 47, 55);
    c[ImGuiCol_HeaderHovered] = Rgb(50, 56, 68);
    c[ImGuiCol_HeaderActive] = Rgb(71, 61, 42);
    c[ImGuiCol_Separator] = Rgb(58, 64, 74);
    c[ImGuiCol_SeparatorHovered] = Rgb(216, 170, 85);
    c[ImGuiCol_SeparatorActive] = Rgb(240, 198, 106);
    c[ImGuiCol_ResizeGrip] = Rgb(58, 64, 74, 120);
    c[ImGuiCol_ResizeGripHovered] = Rgb(216, 170, 85, 190);
    c[ImGuiCol_ResizeGripActive] = Rgb(240, 198, 106);
    c[ImGuiCol_Tab] = Rgb(30, 33, 38);
    c[ImGuiCol_TabHovered] = Rgb(50, 56, 68);
    c[ImGuiCol_TabActive] = Rgb(71, 61, 42);
    c[ImGuiCol_TabUnfocused] = Rgb(30, 33, 38);
    c[ImGuiCol_TabUnfocusedActive] = Rgb(42, 47, 55);
    c[ImGuiCol_TableHeaderBg] = Rgb(36, 40, 47);
    c[ImGuiCol_TableBorderStrong] = Rgb(58, 64, 74);
    c[ImGuiCol_TableBorderLight] = Rgb(50, 56, 68);
    c[ImGuiCol_TableRowBg] = Rgb(30, 33, 38);
    c[ImGuiCol_TableRowBgAlt] = Rgb(36, 40, 47, 180);
    c[ImGuiCol_TextSelectedBg] = Rgb(216, 170, 85, 80);
    c[ImGuiCol_NavHighlight] = kGold;
    c[ImGuiCol_DragDropTarget] = Rgb(240, 198, 106);

    style.ScaleAllSizes(g_scale);
}

bool LoadChineseFont(ImGuiIO& io, float scale) {
    const std::array<const char*, 3> candidates = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
    };
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.PixelSnapH = false;
    const float size = 16.0f * std::clamp(scale, 1.0f, 2.0f);
    for (const char* path : candidates) {
        if (!std::filesystem::exists(path)) {
            continue;
        }
        if (io.Fonts->AddFontFromFileTTF(path, size, &config,
                                         io.Fonts->GetGlyphRangesChineseFull()) != nullptr) {
            return true;
        }
    }
    io.Fonts->AddFontDefault();
    return false;
}

float Scale() { return g_scale; }
float Px(float logicalPixels) { return logicalPixels * g_scale; }
const ImVec4& Gold() { return kGold; }
const ImVec4& Blue() { return kBlue; }
const ImVec4& Success() { return kSuccess; }
const ImVec4& Warning() { return kWarning; }
const ImVec4& Error() { return kError; }
const ImVec4& MutedText() { return kMutedText; }

} // namespace legend::editor::theme
