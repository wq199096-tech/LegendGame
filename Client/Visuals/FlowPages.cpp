#include "Client/Ui/FlowUiModel.h"

#include "Client/Ui/CharacterVisualCatalog.h"
#include "Client/Ui/PlayerFacingErrorCatalog.h"
#include "Client/Ui/UiTheme.h"
#include "Client/Visuals/VisualRuntime.h"
#include "Engine/Debug/Logger.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>

// ---------------------------------------------------------------------------
// Stage26 指令二十一~二十九：玩家流程页渲染（Boot/Connecting/Login/Register/
// CharacterLobby/CharacterCreate/EnteringWorld/Disconnected/FatalError）。
// 全部使用 UiTheme 参考分辨率（1920×1080）+ ScaleFor 缩放；简体中文文案；
// 交互经 UiRequest{FlowUi} 回 GameScene -> ClientFlowController（服务器权威）。
// ---------------------------------------------------------------------------

namespace legend::client {

namespace {

using legend::math::Color;
using legend::math::Vector2;
using FlowKind = legend::flow::FlowUiAction::Kind;
using legend::flow::ClientFlowState;
using legend::flow::FlowField;

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool Contains(const Vector2& p) const {
        return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }
};

// UiRequest 快捷构造。
VisualRuntime::UiRequest FlowRequest(FlowKind kind, int index = 0) {
    VisualRuntime::UiRequest req;
    req.kind = VisualRuntime::UiRequest::Kind::FlowUi;
    req.flowAction = static_cast<std::uint8_t>(kind);
    req.index = index;
    return req;
}

} // namespace

// ---------------------------------------------------------------------------
// 立绘（指令二十七）：identity 相机 + 手工 EntityVisual + DrawEntitySprite。
// ---------------------------------------------------------------------------

void VisualRuntime::RenderCharacterPortrait(const std::string& visualId,
                                            const Vector2& topLeft, float width, float height) {
    if (!m_ready || m_spriteShader == nullptr) {
        return;
    }
    // 墙钟推进（流程页不走 Update(dt)）。
    const float nowMs = static_cast<float>(SDL_GetTicks());
    float dt = m_portraitLastMs == 0.0f ? 0.0f : (nowMs - m_portraitLastMs) / 1000.0f;
    m_portraitLastMs = nowMs;
    dt = std::clamp(dt, 0.0f, 0.1f);

    if (m_portraitVisualId != visualId) {
        m_portraitVisual = EntityVisual{};
        m_portraitVisual.kind = EntityKind::LocalPlayer;
        m_portraitVisual.visualId = visualId;
        ApplyEntityDefinition(m_portraitVisual);
        m_portraitVisualId = visualId;
        m_portraitVisual.direction = 0; // 朝向镜头（South）
        m_portraitVisual.player.SetDirection(0);
    }
    UpdateEntityVisual(m_portraitVisual, dt, false, false, true);
    // 脚底锚点：矩形底部中央；base 帧高 ~128px -> scaleBoost 按高度拟合。
    const Vector2 feet(topLeft.x + width * 0.5f, topLeft.y + height * 0.94f);
    const float boost = std::clamp(height / 150.0f, 0.6f, 4.0f);
    DrawEntitySprite(m_uiBatch, m_portraitVisual, feet, boost);
}

void VisualRuntime::SetLocalPlayerVisualOverride(std::uint16_t visualId) {
    if (m_localVisualOverride != visualId) {
        m_localVisualOverride = visualId;
        LOG_INFO("[Flow] local player visual override = " + std::to_string(visualId));
    }
}

// ---------------------------------------------------------------------------
// 流程页
// ---------------------------------------------------------------------------

void VisualRuntime::RenderFlowPages(const legend::flow::FlowUiModel& model, float viewportWidth,
                                    float viewportHeight) {
    if (!m_ready || m_spriteShader == nullptr) {
        return;
    }
    m_uiDrawCallBase = m_uiBatch.GetDrawCallCount();
    m_identityCamera.SetPosition({viewportWidth * 0.5f, viewportHeight * 0.5f});
    m_identityCamera.SetZoom(1.0f);
    m_uiBatch.Begin(*m_spriteShader, m_identityCamera, viewportWidth, viewportHeight);

    const ui::UiTheme& theme = ui::DefaultUiTheme();
    const float scale = ui::UiTheme::ScaleFor(viewportWidth, viewportHeight);
    const bool haveText = m_text.IsReady();
    const Vector2 refMouse(m_lastMouseX / scale, m_lastMouseY / scale);

    // ---- 全屏底色（深蓝黑）+ 顶部/底部装饰线 ----
    m_uiBatch.DrawQuad(*m_whiteTexture, {viewportWidth * 0.5f, viewportHeight * 0.5f},
                       {viewportWidth / 64.0f, viewportHeight / 64.0f}, 0.0f,
                       Color(0.045f, 0.055f, 0.09f, 1.0f));
    m_uiBatch.DrawQuad(*m_whiteTexture, {viewportWidth * 0.5f, 6.0f},
                       {viewportWidth / 64.0f, 12.0f / 64.0f}, 0.0f,
                       Color(theme.panelBorder.r, theme.panelBorder.g, theme.panelBorder.b, 0.6f));
    m_uiBatch.DrawQuad(*m_whiteTexture,
                       {viewportWidth * 0.5f, viewportHeight - 6.0f},
                       {viewportWidth / 64.0f, 12.0f / 64.0f}, 0.0f,
                       Color(theme.panelBorder.r, theme.panelBorder.g, theme.panelBorder.b, 0.6f));

    // ---- 通用绘制助手（参考分辨率坐标；内部乘 scale）----
    auto drawPanel = [&](const Rect& r, float alpha) {
        DrawPanel(m_uiBatch, {r.x * scale, r.y * scale}, r.w * scale, r.h * scale, alpha);
        // 描边（金色细线）
        const float t = 2.0f * scale;
        m_uiBatch.DrawQuad(*m_whiteTexture, {r.x * scale + r.w * scale * 0.5f, r.y * scale},
                           {r.w * scale / 64.0f, t / 64.0f}, 0.0f, theme.panelBorder);
        m_uiBatch.DrawQuad(*m_whiteTexture,
                           {r.x * scale + r.w * scale * 0.5f, (r.y + r.h) * scale},
                           {r.w * scale / 64.0f, t / 64.0f}, 0.0f, theme.panelBorder);
        m_uiBatch.DrawQuad(*m_whiteTexture, {r.x * scale, r.y * scale + r.h * scale * 0.5f},
                           {t / 64.0f, r.h * scale / 64.0f}, 0.0f, theme.panelBorder);
        m_uiBatch.DrawQuad(*m_whiteTexture,
                           {(r.x + r.w) * scale, r.y * scale + r.h * scale * 0.5f},
                           {t / 64.0f, r.h * scale / 64.0f}, 0.0f, theme.panelBorder);
    };

    auto drawText = [&](const Vector2& pos, const std::string& text, float size,
                        const Color& color, bool center = false, bool right = false) {
        if (haveText) {
            m_text.DrawString(m_uiBatch, {pos.x * scale, pos.y * scale}, text, size * scale,
                              color, right, center);
        }
    };

    auto drawButton = [&](const Rect& r, const std::string& label, bool enabled) -> bool {
        const bool hovered = r.Contains(refMouse);
        Color fill = enabled ? (hovered ? theme.buttonHover : theme.buttonNormal)
                             : theme.buttonDisabled;
        m_uiBatch.DrawQuad(*m_whiteTexture,
                           {r.x * scale + r.w * scale * 0.5f, r.y * scale + r.h * scale * 0.5f},
                           {r.w * scale / 64.0f, r.h * scale / 64.0f}, 0.0f, fill);
        const Color textColor =
            enabled ? (hovered ? theme.textGold : theme.textPrimary) : theme.textDim;
        drawText({r.x + r.w * 0.5f, r.y + r.h * 0.5f}, label, 17.0f, textColor, true);
        const bool clicked = enabled && hovered && m_lastMouseClicked;
        if (clicked) {
            LOG_INFO("[Flow] button clicked: " + label);
        }
        return clicked;
    };

    auto drawTextField = [&](const Rect& r, const std::string& text, bool masked, bool focused,
                             const std::string& placeholder, FlowField field) {
        Color fill(0.10f, 0.12f, 0.17f, 0.95f);
        m_uiBatch.DrawQuad(*m_whiteTexture,
                           {r.x * scale + r.w * scale * 0.5f, r.y * scale + r.h * scale * 0.5f},
                           {r.w * scale / 64.0f, r.h * scale / 64.0f}, 0.0f, fill);
        // 边框：聚焦金色/普通灰。
        const Color border = focused ? theme.textGold : Color(0.30f, 0.33f, 0.42f, 0.9f);
        const float t = 2.0f * scale;
        m_uiBatch.DrawQuad(*m_whiteTexture, {r.x * scale + r.w * scale * 0.5f, r.y * scale},
                           {r.w * scale / 64.0f, t / 64.0f}, 0.0f, border);
        m_uiBatch.DrawQuad(*m_whiteTexture,
                           {r.x * scale + r.w * scale * 0.5f, (r.y + r.h) * scale},
                           {r.w * scale / 64.0f, t / 64.0f}, 0.0f, border);
        m_uiBatch.DrawQuad(*m_whiteTexture, {r.x * scale, r.y * scale + r.h * scale * 0.5f},
                           {t / 64.0f, r.h * scale / 64.0f}, 0.0f, border);
        m_uiBatch.DrawQuad(*m_whiteTexture,
                           {(r.x + r.w) * scale, r.y * scale + r.h * scale * 0.5f},
                           {t / 64.0f, r.h * scale / 64.0f}, 0.0f, border);

        const std::string shown =
            masked ? std::string(std::min<std::size_t>(text.size(), 24), '*') : text;
        if (!shown.empty()) {
            drawText({r.x + 12.0f, r.y + r.h * 0.5f}, shown, 16.0f, theme.textPrimary, false,
                     false);
        } else if (!placeholder.empty()) {
            drawText({r.x + 12.0f, r.y + r.h * 0.5f}, placeholder, 15.0f, theme.textDim);
        }
        // 光标（聚焦时闪烁：0.6s 周期）。
        if (focused) {
            const bool on = std::fmod(model.busyElapsed, 1.2f) < 0.6f;
            if (on) {
                const float textW = shown.empty()
                                        ? 0.0f
                                        : (haveText ? m_text.MeasureText(shown, 16.0f * scale)
                                                    : 0.0f);
                const float cursorX = (r.x + 12.0f) * scale + textW + 2.0f * scale;
                m_uiBatch.DrawQuad(*m_whiteTexture,
                                   {cursorX, r.y * scale + r.h * scale * 0.5f},
                                   {2.0f / 64.0f, (r.h - 16.0f) * scale / 64.0f}, 0.0f,
                                   theme.textPrimary);
            }
        }
        // 点击聚焦。
        if (m_lastMouseClicked && r.Contains(refMouse)) {
            m_uiRequests.push_back(FlowRequest(FlowKind::FocusField, static_cast<int>(field)));
        }
    };

    auto drawError = [&](const Rect& r, const legend::flow::FlowUiModel& m) {
        std::string message;
        if (m.lastErrorCode != 0) {
            message = ui::PlayerFacingAccountErrorText(m.lastErrorCode);
        } else if (!m.lastErrorMessage.empty()) {
            message = m.lastErrorMessage;
        }
        if (!message.empty()) {
            drawText({r.x + r.w * 0.5f, r.y}, message, 15.0f, theme.textError, true);
        }
    };

    auto drawTitle = [&](const std::string& main, const std::string& sub) {
        drawText({viewportWidth / (2.0f * scale), 150.0f}, main, 42.0f, theme.textGold, true);
        if (!sub.empty()) {
            drawText({viewportWidth / (2.0f * scale), 210.0f}, sub, 18.0f, theme.textDim, true);
        }
    };

    // busy 提示（页脚）：转点动画。
    auto drawBusy = [&](const std::string& label) {
        const int dots = static_cast<int>(std::fmod(model.busyElapsed, 3.0)) + 1;
        drawText({viewportWidth / (2.0f * scale), viewportHeight / scale - 60.0f},
                 label + std::string(static_cast<std::size_t>(dots), '.'), 18.0f,
                 theme.textWarning, true);
    };

    switch (model.state) {
        // ---- 启动画面（指令二十一）----
        case ClientFlowState::Boot: {
            drawText({viewportWidth / (2.0f * scale), viewportHeight / (2.0f * scale) - 40.0f},
                     "传 奇 之 旅", 56.0f, theme.textGold, true);
            drawText({viewportWidth / (2.0f * scale), viewportHeight / (2.0f * scale) + 30.0f},
                     "LegendGame", 22.0f, theme.textDim, true);
            drawText({viewportWidth / (2.0f * scale), viewportHeight / scale - 80.0f},
                     "正在启动...", 15.0f, theme.textDim, true);
            break;
        }
        // ---- 连接中 ----
        case ClientFlowState::Connecting: {
            drawTitle("连接服务器", "");
            drawBusy("正在连接服务器");
            break;
        }
        // ---- 登录页（指令二十三）----
        case ClientFlowState::Login: {
            drawTitle("登录账号", "");
            const Rect panel{viewportWidth / scale * 0.5f - 260.0f, 300.0f, 520.0f, 400.0f};
            drawPanel(panel, 0.72f);
            drawText({panel.x + 40.0f, panel.y + 36.0f}, "账号名", 16.0f, theme.textPrimary);
            const Rect accountRect{panel.x + 40.0f, panel.y + 64.0f, panel.w - 80.0f, 48.0f};
            drawTextField(accountRect, model.accountName, false,
                          model.focusedField == FlowField::LoginAccount, "请输入账号名",
                          FlowField::LoginAccount);
            drawText({panel.x + 40.0f, panel.y + 140.0f}, "密码", 16.0f, theme.textPrimary);
            const Rect passwordRect{panel.x + 40.0f, panel.y + 168.0f, panel.w - 80.0f, 48.0f};
            drawTextField(passwordRect, model.password, true,
                          model.focusedField == FlowField::LoginPassword, "请输入密码",
                          FlowField::LoginPassword);
            const Rect loginRect{panel.x + 40.0f, panel.y + 248.0f, panel.w - 80.0f, 52.0f};
            if (drawButton(loginRect, model.busy ? "登录中..." : "登 录", !model.busy)) {
                m_uiRequests.push_back(FlowRequest(FlowKind::SubmitLogin));
            }
            const Rect registerRect{panel.x + 40.0f, panel.y + 320.0f, panel.w - 80.0f, 44.0f};
            if (drawButton(registerRect, "没有账号？去注册", !model.busy)) {
                m_uiRequests.push_back(FlowRequest(FlowKind::GoRegister));
            }
            drawError({panel.x, panel.y + panel.h + 14.0f, panel.w, 30.0f}, model);
            break;
        }
        // ---- 注册页（指令二十四）----
        case ClientFlowState::Register: {
            drawTitle("注册新账号", "");
            const Rect panel{viewportWidth / scale * 0.5f - 260.0f, 280.0f, 520.0f, 470.0f};
            drawPanel(panel, 0.72f);
            drawText({panel.x + 40.0f, panel.y + 30.0f}, "账号名", 16.0f, theme.textPrimary);
            const Rect accountRect{panel.x + 40.0f, panel.y + 56.0f, panel.w - 80.0f, 46.0f};
            drawTextField(accountRect, model.regAccount, false,
                          model.focusedField == FlowField::RegAccount, "4~24 位字母/数字/下划线",
                          FlowField::RegAccount);
            drawText({panel.x + 40.0f, panel.y + 124.0f}, "密码", 16.0f, theme.textPrimary);
            const Rect passwordRect{panel.x + 40.0f, panel.y + 150.0f, panel.w - 80.0f, 46.0f};
            drawTextField(passwordRect, model.regPassword, true,
                          model.focusedField == FlowField::RegPassword, "至少 6 位",
                          FlowField::RegPassword);
            drawText({panel.x + 40.0f, panel.y + 218.0f}, "确认密码", 16.0f, theme.textPrimary);
            const Rect password2Rect{panel.x + 40.0f, panel.y + 244.0f, panel.w - 80.0f, 46.0f};
            drawTextField(password2Rect, model.regPassword2, true,
                          model.focusedField == FlowField::RegPassword2, "再输入一次密码",
                          FlowField::RegPassword2);
            const Rect submitRect{panel.x + 40.0f, panel.y + 316.0f, panel.w - 80.0f, 52.0f};
            if (drawButton(submitRect, model.busy ? "注册中..." : "注 册", !model.busy)) {
                m_uiRequests.push_back(FlowRequest(FlowKind::SubmitRegister));
            }
            const Rect backRect{panel.x + 40.0f, panel.y + 388.0f, panel.w - 80.0f, 44.0f};
            if (drawButton(backRect, "返回登录（Esc）", !model.busy)) {
                m_uiRequests.push_back(FlowRequest(FlowKind::CancelRegister));
            }
            drawError({panel.x, panel.y + panel.h + 14.0f, panel.w, 30.0f}, model);
            break;
        }
        // ---- 角色大厅（指令二十五/二十六/二十七）----
        case ClientFlowState::CharacterLobby: {
            drawText({viewportWidth / (2.0f * scale), 60.0f}, "角 色 大 厅", 36.0f,
                     theme.textGold, true);
            drawText({viewportWidth / (2.0f * scale), 106.0f},
                     "账号: " + model.loginUserName, 15.0f, theme.textDim, true);

            // 角色卡片（最多 4 槽；水平排布）。
            constexpr int kSlotCount = 4;
            const float cardW = 300.0f;
            const float cardH = 400.0f;
            const float gap = 40.0f;
            const float totalW = kSlotCount * cardW + (kSlotCount - 1) * gap;
            const float startX = viewportWidth / scale * 0.5f - totalW * 0.5f;
            const float cardY = 170.0f;
            for (int i = 0; i < kSlotCount; ++i) {
                const Rect card{startX + i * (cardW + gap), cardY, cardW, cardH};
                const bool hasCharacter =
                    i < static_cast<int>(model.characters.size());
                const bool selected = model.selectedIndex == i && hasCharacter;
                drawPanel(card, selected ? 0.85f : 0.55f);
                if (selected) {
                    // 选中高亮描边（双线）。
                    drawPanel(Rect{card.x - 4.0f, card.y - 4.0f, card.w + 8.0f, card.h + 8.0f},
                              0.10f);
                }
                if (hasCharacter) {
                    const auto& character = model.characters[static_cast<std::size_t>(i)];
                    // 立绘（identity 相机 + 实体 idle 动画）。
                    RenderCharacterPortrait(
                        ui::CharacterVisualEntityName(character.visualId),
                        {card.x, card.y}, card.w, card.h - 96.0f);
                    drawText({card.x + card.w * 0.5f, card.y + card.h - 84.0f},
                             character.name, 20.0f, theme.textPrimary, true);
                    drawText({card.x + card.w * 0.5f, card.y + card.h - 52.0f},
                             std::string(ui::CharacterVisualDisplayName(character.visualId)) +
                                 "  Lv." + std::to_string(character.level) + "  " +
                                 ui::CharacterGenderDisplayName(character.gender),
                             14.0f, theme.textDim, true);
                    // 点击卡片 = 选中。
                    if (m_lastMouseClicked && card.Contains(refMouse)) {
                        m_uiRequests.push_back(FlowRequest(FlowKind::SelectCharacter, i));
                    }
                } else {
                    drawText({card.x + card.w * 0.5f, card.y + card.h * 0.5f - 20.0f},
                             "空角色栏", 18.0f, theme.textDim, true);
                    const Rect createSmall{card.x + 60.0f, card.y + card.h * 0.5f + 20.0f,
                                           card.w - 120.0f, 44.0f};
                    if (drawButton(createSmall, "+ 创建角色", !model.busy)) {
                        m_uiRequests.push_back(FlowRequest(FlowKind::OpenCreate));
                    }
                }
            }

            // 底部操作行。
            const float buttonY = cardY + cardH + 36.0f;
            const Rect enterRect{startX, buttonY, 200.0f, 52.0f};
            const bool hasSelection = model.selectedIndex >= 0 &&
                                      model.selectedIndex <
                                          static_cast<int>(model.characters.size());
            if (drawButton(enterRect, "进入游戏",
                           hasSelection && !model.busy)) {
                m_uiRequests.push_back(FlowRequest(FlowKind::EnterWorld, model.selectedIndex));
            }
            const Rect createRect{startX + 220.0f, buttonY, 200.0f, 52.0f};
            if (drawButton(createRect, "创建角色", !model.busy)) {
                m_uiRequests.push_back(FlowRequest(FlowKind::OpenCreate));
            }
            const Rect deleteRect{startX + 440.0f, buttonY, 200.0f, 52.0f};
            if (drawButton(deleteRect, "删除角色", hasSelection && !model.busy)) {
                m_uiRequests.push_back(FlowRequest(FlowKind::StartDelete,
                                                   model.selectedIndex));
            }
            const Rect logoutRect{startX + 660.0f, buttonY, 200.0f, 52.0f};
            if (drawButton(logoutRect, "退出登录", !model.busy)) {
                m_uiRequests.push_back(FlowRequest(FlowKind::ReturnToLogin));
            }
            drawText({viewportWidth / scale - 20.0f, buttonY + 60.0f},
                     "共 " + std::to_string(model.characters.size()) + "/4 个角色", 14.0f,
                     theme.textDim, false, true);

            // 删除二次确认浮层（指令二十六：重输角色名）。
            if (model.deleteConfirmOpen) {
                // 全屏遮罩。
                m_uiBatch.DrawQuad(*m_whiteTexture,
                                   {viewportWidth * 0.5f, viewportHeight * 0.5f},
                                   {viewportWidth / 64.0f, viewportHeight / 64.0f}, 0.0f,
                                   Color(0.0f, 0.0f, 0.0f, 0.55f));
                const Rect modal{viewportWidth / scale * 0.5f - 280.0f, 380.0f, 560.0f, 300.0f};
                drawPanel(modal, 0.95f);
                drawText({modal.x + modal.w * 0.5f, modal.y + 34.0f}, "确认删除角色", 22.0f,
                         theme.textError, true);
                std::string confirmName;
                if (model.selectedIndex >= 0 &&
                    model.selectedIndex < static_cast<int>(model.characters.size())) {
                    confirmName =
                        model.characters[static_cast<std::size_t>(model.selectedIndex)].name;
                }
                drawText({modal.x + modal.w * 0.5f, modal.y + 74.0f},
                         "将永久删除角色 [" + confirmName + "]，该操作不可恢复。", 15.0f,
                         theme.textWarning, true);
                drawText({modal.x + modal.w * 0.5f, modal.y + 100.0f}, "请输入角色名以确认：",
                         15.0f, theme.textPrimary, true);
                const Rect inputRect{modal.x + 60.0f, modal.y + 126.0f, modal.w - 120.0f, 46.0f};
                drawTextField(inputRect, model.deleteConfirmText, false,
                              model.focusedField == FlowField::DeleteConfirm, "角色名",
                              FlowField::DeleteConfirm);
                const Rect yesRect{modal.x + 60.0f, modal.y + 200.0f, 200.0f, 50.0f};
                const bool confirmReady = model.deleteConfirmText == confirmName;
                if (drawButton(yesRect, model.busy ? "删除中..." : "确认删除",
                               confirmReady && !model.busy)) {
                    m_uiRequests.push_back(FlowRequest(FlowKind::ConfirmDelete));
                }
                const Rect noRect{modal.x + modal.w - 260.0f, modal.y + 200.0f, 200.0f, 50.0f};
                if (drawButton(noRect, "取消（Esc）", !model.busy)) {
                    m_uiRequests.push_back(FlowRequest(FlowKind::CancelDelete));
                }
                drawError({modal.x, modal.y + modal.h + 12.0f, modal.w, 30.0f}, model);
            } else {
                drawError({0.0f, buttonY + 70.0f, viewportWidth / scale, 30.0f}, model);
            }
            if (model.busy) {
                drawBusy("正在处理");
            }
            break;
        }
        // ---- 创建角色（指令二十八）----
        case ClientFlowState::CharacterCreate: {
            drawTitle("创建角色", "选择造型并输入角色名");
            const Rect panel{viewportWidth / scale * 0.5f - 400.0f, 260.0f, 800.0f, 540.0f};
            drawPanel(panel, 0.72f);

            drawText({panel.x + 50.0f, panel.y + 40.0f}, "角色名（2~12 个汉字、字母或数字）",
                     16.0f, theme.textPrimary);
            const Rect nameRect{panel.x + 50.0f, panel.y + 70.0f, panel.w - 100.0f, 48.0f};
            drawTextField(nameRect, model.newName, false,
                          model.focusedField == FlowField::CreateName, "请输入角色名",
                          FlowField::CreateName);

            drawText({panel.x + 50.0f, panel.y + 150.0f}, "造型", 16.0f, theme.textPrimary);
            const float portraitW = 200.0f;
            const float portraitH = 240.0f;
            const float portraitGap = 40.0f;
            const float portraitsX = panel.x + panel.w * 0.5f -
                                     (3.0f * portraitW + 2.0f * portraitGap) * 0.5f;
            for (int visual = 1; visual <= 3; ++visual) {
                const Rect card{portraitsX + static_cast<float>(visual - 1) *
                                               (portraitW + portraitGap),
                                panel.y + 180.0f, portraitW, portraitH};
                const bool selected = model.newVisualId == static_cast<std::uint16_t>(visual);
                drawPanel(card, selected ? 0.85f : 0.55f);
                if (selected) {
                    drawPanel(Rect{card.x - 4.0f, card.y - 4.0f, card.w + 8.0f, card.h + 8.0f},
                              0.10f);
                }
                RenderCharacterPortrait(ui::CharacterVisualEntityName(
                                            static_cast<std::uint16_t>(visual)),
                                        {card.x, card.y}, card.w, card.h - 40.0f);
                drawText({card.x + card.w * 0.5f, card.y + card.h - 24.0f},
                         ui::CharacterVisualDisplayName(static_cast<std::uint16_t>(visual)),
                         16.0f, selected ? theme.textGold : theme.textDim, true);
                if (m_lastMouseClicked && card.Contains(refMouse)) {
                    m_uiRequests.push_back(FlowRequest(FlowKind::SelectVisual, visual - 1));
                }
            }

            const Rect submitRect{panel.x + 50.0f, panel.y + 450.0f, 300.0f, 52.0f};
            if (drawButton(submitRect, model.busy ? "创建中..." : "创 建", !model.busy)) {
                m_uiRequests.push_back(FlowRequest(FlowKind::SubmitCreate));
            }
            const Rect backRect{panel.x + panel.w - 350.0f, panel.y + 450.0f, 300.0f, 52.0f};
            if (drawButton(backRect, "返回大厅（Esc）", !model.busy)) {
                m_uiRequests.push_back(FlowRequest(FlowKind::CancelCreate));
            }
            drawError({panel.x, panel.y + panel.h + 14.0f, panel.w, 30.0f}, model);
            break;
        }
        // ---- 进入世界加载 ----
        case ClientFlowState::EnteringWorld: {
            drawTitle("正在进入世界", "");
            drawBusy("加载地图与角色数据");
            break;
        }
        // ---- 断线页（指令二十二）----
        case ClientFlowState::Disconnected: {
            drawTitle("连接已断开", "与服务器的连接中断");
            const Rect panel{viewportWidth / scale * 0.5f - 280.0f, 400.0f, 560.0f, 220.0f};
            drawPanel(panel, 0.72f);
            std::string reason = model.lastErrorMessage;
            if (model.lastErrorCode != 0) {
                reason = ui::PlayerFacingAccountErrorText(model.lastErrorCode);
            }
            drawText({panel.x + panel.w * 0.5f, panel.y + 46.0f},
                     reason.empty() ? "网络连接异常，请重试。" : reason, 16.0f,
                     theme.textError, true);
            const Rect retryRect{panel.x + 130.0f, panel.y + 100.0f, 300.0f, 52.0f};
            if (drawButton(retryRect, "重新连接", true)) {
                m_uiRequests.push_back(FlowRequest(FlowKind::RetryConnect));
            }
            break;
        }
        // ---- 致命错误 ----
        case ClientFlowState::FatalError: {
            drawTitle("发生致命错误", "请查看日志文件（logs/）");
            const Rect panel{viewportWidth / scale * 0.5f - 280.0f, 420.0f, 560.0f, 160.0f};
            drawPanel(panel, 0.72f);
            drawText({panel.x + panel.w * 0.5f, panel.y + 60.0f},
                     model.lastErrorMessage.empty() ? "客户端无法继续运行。"
                                                    : model.lastErrorMessage,
                     16.0f, theme.textError, true);
            break;
        }
        case ClientFlowState::InWorld:
            break; // 不应到达（GameScene 早退分支只在非 InWorld 调用）
    }

    m_lastMouseClicked = false; // 消费本帧点击
    m_uiBatch.End();
    m_stats.drawCalls += m_uiBatch.GetDrawCallCount() - m_uiDrawCallBase;
}

} // namespace legend::client
