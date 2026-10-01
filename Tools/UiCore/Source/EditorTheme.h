#pragma once

struct ImGuiIO;
struct ImVec4;

namespace legend::editor::theme {

// LegendGame Studio 的统一视觉入口。scale 使用窗口所在显示器的 DPI 比例。
void Apply(float scale);

// 运行时加载系统中文字体，不将字体文件提交到仓库。
// 顺序：微软雅黑 -> 黑体 -> 宋体；全部不可用时保留 ImGui 默认字体。
bool LoadChineseFont(ImGuiIO& io, float scale);

float Scale();
float Px(float logicalPixels);

const ImVec4& Gold();
const ImVec4& Blue();
const ImVec4& Success();
const ImVec4& Warning();
const ImVec4& Error();
const ImVec4& MutedText();

} // namespace legend::editor::theme
