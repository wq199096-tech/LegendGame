#include "Client/Visuals/VisualRuntime.h"

#include "Client/Ui/ChatUiTheme.h"
#include "Client/WorldNetwork/WorldClientController.h"
#include "Engine/Debug/Logger.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>

// ---------------------------------------------------------------------------
// Stage27 指令十九~二十六：聊天窗口渲染（实现 VisualRuntime::RenderChatWindow；
// 与 FlowPages.cpp 同模式拆分）。左下角半透明窗口：频道标签 / 滚动历史 /
// 输入栏 / 发送按钮 / 点击名字私聊小菜单。颜色统一走 ChatUiTheme（指令二十一）。
// ---------------------------------------------------------------------------

namespace legend::client {

namespace {

using legend::math::Color;
using legend::math::Vector2;

struct FRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool Contains(const Vector2& p) const {
        return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }
};

constexpr int kChatActionSendDraft = 1; // UiRequest{Chat} -> GameScene 发送草稿

} // namespace

void VisualRuntime::RenderChatWindow(const WorldClientController& world,
                                     float viewportWidth, float viewportHeight) {
    if (!m_ready || m_spriteShader == nullptr) {
        return;
    }
    ChatModel& chat = m_chat;
    const ui::ChatUiTheme& theme = ui::DefaultChatUiTheme();
    const float scale = ui::UiTheme::ScaleFor(viewportWidth, viewportHeight);
    const bool haveText = m_text.IsReady();
    const Vector2 refMouse(m_lastMouseX / scale, m_lastMouseY / scale);
    const float refW = viewportWidth / scale;
    const float refH = viewportHeight / scale;

    auto quad = [&](const FRect& r, const Color& c) {
        m_uiBatch.DrawQuad(*m_whiteTexture,
                           {(r.x + r.w * 0.5f) * scale, (r.y + r.h * 0.5f) * scale},
                           {r.w * scale / 64.0f, r.h * scale / 64.0f}, 0.0f, c);
    };
    auto drawText = [&](const Vector2& pos, const std::string& text, float size, const Color& c,
                        bool center = false) {
        if (haveText && !text.empty()) {
            m_text.DrawString(m_uiBatch, {pos.x * scale, pos.y * scale}, text, size * scale, c,
                              false, center);
        }
    };
    auto measure = [&](const std::string& text, float size) -> float {
        return haveText ? m_text.MeasureText(text, size * scale) / scale : 0.0f;
    };

    // ---- 窗口矩形（左下角，留边距）----
    const float margin = 8.0f;
    const float winW = chat.Expanded() ? theme.expandedW : theme.collapsedW;
    const float winH = chat.Expanded() ? theme.expandedH : theme.collapsedH;
    const FRect win{margin, refH - margin - winH, winW, winH};

    // ---- 折叠态：只画标题条（指令二十：默认小尺寸，点击展开）----
    const FRect toggleBar{win.x, win.y, win.w, theme.collapsedH};
    if (!chat.Expanded()) {
        quad(toggleBar, theme.panelBack);
        drawText({win.x + 10.0f, win.y + 5.0f}, "聊天", 14.0f, theme.tabActive);
        drawText({win.x + win.w - 20.0f, win.y + 5.0f}, "展 开", 12.0f, theme.tabInactive, true);
        if (m_lastMouseClicked && toggleBar.Contains(refMouse)) {
            chat.ToggleExpanded();
        }
        return;
    }

    // ---- 展开态底板 + 金边 ----
    quad(win, theme.panelBack);
    const float border = 1.5f * scale;
    quad({win.x, win.y, win.w, border / scale}, theme.panelBorder);
    quad({win.x, win.y + win.h - border / scale, win.w, border / scale}, theme.panelBorder);
    quad({win.x, win.y, border / scale, win.h}, theme.panelBorder);
    quad({win.x + win.w - border / scale, win.y, border / scale, win.h}, theme.panelBorder);

    // ---- 频道标签行（指令十九：综合/附近/世界/私聊/系统）----
    const FRect tabBar{win.x + 6.0f, win.y + 5.0f, win.w - 12.0f, theme.tabBarH};
    {
        const float tabW = tabBar.w / 5.0f;
        for (std::uint8_t tab = 0; tab < 5; ++tab) {
            const FRect tabRect{tabBar.x + tab * tabW, tabBar.y, tabW, tabBar.h};
            const bool active = chat.ActiveTab() == tab;
            if (active) {
                quad(tabRect, Color(theme.buttonHover.r, theme.buttonHover.g,
                                    theme.buttonHover.b, 0.55f));
            }
            drawText({tabRect.x + tabW * 0.5f, tabRect.y + 4.0f}, ui::ChatUiTheme::TabLabel(tab),
                     14.0f, active ? theme.tabActive : theme.tabInactive, true);
            if (m_lastMouseClicked && tabRect.Contains(refMouse)) {
                chat.SetActiveTab(tab);
                chat.ResetScroll();
            }
        }
    }

    // ---- 历史区（滚动；鼠标滚轮支持，指令二十）----
    const FRect historyRect{win.x + 8.0f, tabBar.y + tabBar.h + 4.0f, win.w - 16.0f,
                            win.h - (tabBar.h + 14.0f) - theme.inputRowH};
    const auto messages = chat.VisibleMessages(chat.ActiveTab());
    int visibleLines = std::max(1, static_cast<int>(historyRect.h / theme.lineH));
    int maxOffset = std::max(0, static_cast<int>(messages.size()) - visibleLines);
    int offset = std::min(chat.ScrollOffset(), maxOffset);
    // 鼠标滚轮（在历史区内）：上滚 = 向上翻历史。
    if (m_lastMouseWheelDelta != 0.0f && historyRect.Contains(refMouse)) {
        chat.ScrollBy(m_lastMouseWheelDelta > 0.0f ? 2 : -2);
    }
    // 新消息到达且贴底时保持贴底：offset 由模型管理，这里只在贴底时重置。
    if (chat.ScrollOffset() == 0) {
        offset = 0;
    }

    std::vector<std::pair<FRect, std::string>> nameRects; // 点击名字 -> 私聊菜单
    const int startIndex = std::max(0, static_cast<int>(messages.size()) - visibleLines - offset);
    const int endIndex = std::min(static_cast<int>(messages.size()), startIndex + visibleLines);
    float lineY = historyRect.y;
    for (int i = startIndex; i < endIndex; ++i) {
        const ChatEntry* entry = messages[static_cast<std::size_t>(i)];
        Color color = theme.nearbyColor;
        std::string prefix;
        if (entry->isError) {
            color = theme.errorColor;
            prefix.clear();
        } else {
            switch (entry->channel) {
                case 1: color = theme.nearbyColor; prefix = "[附近] "; break;
                case 2: color = theme.worldColor; prefix = "[世界] "; break;
                case 3: color = theme.whisperColor; prefix = "[私聊] "; break;
                case 4: color = theme.systemColor; prefix = "[系统] "; break;
                default: break;
            }
        }
        float penX = historyRect.x + 4.0f;
        const float textY = lineY + 2.0f;
        if (entry->isError) {
            drawText({penX, textY}, entry->text, theme.textFontSize, color);
        } else if (entry->channel == 4) {
            drawText({penX, textY}, prefix + entry->text, theme.textFontSize, color);
        } else {
            // 指令二十二：[附近] 界面英雄：你好 / [私聊] A → B：你好
            drawText({penX, textY}, prefix, theme.textFontSize, color);
            penX += measure(prefix, theme.textFontSize);
            if (!entry->senderName.empty()) {
                // 指令二十三：聊天记录里的角色名可点击 -> 私聊小菜单。
                drawText({penX, textY}, entry->senderName, theme.nameFontSize, theme.tabActive);
                nameRects.emplace_back(
                    FRect{penX, textY, measure(entry->senderName, theme.nameFontSize),
                          theme.lineH},
                    entry->senderName);
                penX += measure(entry->senderName, theme.nameFontSize);
            }
            std::string middle;
            if (!entry->targetName.empty()) {
                middle = " → " + entry->targetName;
            }
            drawText({penX, textY}, middle + "：" + entry->text, theme.textFontSize, color);
        }
        lineY += theme.lineH;
    }

    // ---- 输入行（指令二十：Enter 打开/发送；Esc 取消）----
    const FRect inputRect{win.x + 8.0f, win.y + win.h - theme.inputRowH - 8.0f,
                          win.w - 16.0f - 56.0f, theme.inputRowH};
    const FRect sendRect{inputRect.x + inputRect.w + 6.0f, inputRect.y, 50.0f, theme.inputRowH};
    if (chat.InputOpen()) {
        quad(inputRect, theme.inputBack);
        std::string shown;
        Color textColor = theme.inputText;
        if (chat.DraftChannel() == 3 && !chat.WhisperTarget().empty()) {
            shown = "[私聊 " + chat.WhisperTarget() + "] ";
        } else {
            shown = std::string("[") + ui::ChatUiTheme::ChannelTag(chat.DraftChannel()) + "] ";
        }
        const float prefixW = measure(shown, theme.textFontSize);
        shown += chat.Draft() + m_chatComposition;
        drawText({inputRect.x + 6.0f, inputRect.y + 5.0f}, shown, theme.textFontSize, textColor);
        // 光标闪烁。
        if (std::fmod(m_worldTimeSeconds, 1.2f) < 0.6f) {
            const float textW = measure(chat.Draft() + m_chatComposition, theme.textFontSize);
            quad({inputRect.x + 6.0f + prefixW + textW, inputRect.y + 5.0f, 2.0f,
                  theme.inputRowH - 10.0f},
                 theme.inputText);
        }
        // 发送按钮（真实逻辑：UiRequest -> GameScene 发送，指令十：不许假按钮）。
        const bool hover = sendRect.Contains(refMouse);
        quad(sendRect, hover ? theme.buttonHover : theme.buttonNormal);
        drawText({sendRect.x + sendRect.w * 0.5f, sendRect.y + 5.0f}, "发送", 14.0f,
                 theme.tabActive, true);
        if (m_lastMouseClicked && hover) {
            UiRequest req;
            req.kind = UiRequest::Kind::Chat;
            req.index = kChatActionSendDraft;
            m_uiRequests.push_back(req);
        }
    } else {
        // 收起输入：提示行（点击也可打开输入）。
        drawText({inputRect.x + 6.0f, inputRect.y + 6.0f}, "按 Enter 聊天", 13.0f,
                 theme.hintText);
        if (m_lastMouseClicked && inputRect.Contains(refMouse)) {
            chat.OpenInput(chat.DefaultChannel());
        }
    }

    // ---- 点击名字 -> 私聊小菜单（指令二十三：第一版只有"私聊"）----
    if (m_lastMouseClicked) {
        for (const auto& [rect, name] : nameRects) {
            if (rect.Contains(refMouse) && name != m_localName) {
                chat.OpenWhisperMenu(name);
                break;
            }
        }
    }
    if (chat.WhisperMenuOpen()) {
        const FRect popup{refMouse.x, refMouse.y - 40.0f, 90.0f, 34.0f};
        quad(popup, theme.popupBack);
        quad({popup.x, popup.y, popup.w, 1.5f / scale}, theme.panelBorder);
        drawText({popup.x + 12.0f, popup.y + 5.0f}, "私聊 " + chat.WhisperMenuTarget(), 13.0f,
                 theme.whisperColor);
        if (m_lastMouseClicked && popup.Contains(refMouse)) {
            chat.OpenInput(static_cast<std::uint8_t>(chat::ChatChannel::Whisper),
                           chat.WhisperMenuTarget());
            chat.CloseWhisperMenu();
        } else if (m_lastMouseClicked && !popup.Contains(refMouse)) {
            chat.CloseWhisperMenu();
        }
    }
}

} // namespace legend::client
