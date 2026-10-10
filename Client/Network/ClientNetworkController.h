#pragma once

#include "Client/Account/AccountClientController.h"
#include "Client/Account/CharacterSelectionController.h"
#include "Client/Network/GameNetworkClient.h"
#include "Client/WorldNetwork/WorldClientController.h"

#include "Engine/Input/InputManager.h"

#include <chrono>
#include <memory>
#include <string>

namespace legend::client {

// 阶段9 指令八十一~八十四：ClientNetworkController——GameScene 只调用
// Update()（指令六：网络逻辑绝不进 GameScene）。
// F8 = Network Debug 状态 / F9 = Connect/Reconnect / F10 = 测试 LoginRequest
// 阶段10：F10 = 自动登录开发账号（指令六十一）/ F11 = Account Debug（指令六十）。
// 阶段11：F12 = World Debug（指令八十）；CharacterSelected 后自动进世界。
class ClientNetworkController {
public:
    ClientNetworkController();

    // 每帧调用：处理 F8/F9/F10/F11/F12 + 消费网络事件（打日志）+ 心跳超时检测
    void Update(legend::input::InputManager& input, float deltaTime);

    GameNetworkClient& Client() { return *m_client; }
    const GameNetworkClient& Client() const { return *m_client; }
    AccountClientController& Account() { return m_account; }
    const AccountClientController& Account() const { return m_account; }
    WorldClientController& World() { return m_world; }
    const WorldClientController& World() const { return m_world; }
    CharacterSelectionController& CharacterSelection() { return m_characterSelection; }
    bool DebugVisible() const { return m_debugVisible; }
    bool AccountDebugVisible() const { return m_accountDebugVisible; }
    bool WorldDebugVisible() const { return m_worldDebugVisible; }
    // 阶段24：LEGEND_CLIENT_AUTO_ENTER=1 自动进世界链路是否启用（本地视觉冒烟用）。
    bool AutoEnterEnabled() const { return m_autoEnter; }
    // F8 状态文本（指令八十三：State/Gateway/ConnectionId/Handshake/Auth/Account/RTT/Last Error）
    std::string StatusText() const;
    // F11 Account Debug 文本（指令六十）
    std::string AccountStatusText() const;
    // F12 World Debug 文本（指令八十）
    std::string WorldStatusText() const;

private:
    void HandleEvent(const NetworkEvent& event);
    void UpdateDevAutoLogin(float deltaTime);
    void UpdateWorldFlow();
    void UpdateWorldMoveInput(legend::input::InputManager& input, float deltaTime);
    // 阶段24：LEGEND_CLIENT_AUTO_ENTER=1 自动进世界（登录→建角/选角→EnterWorld）。
    void UpdateAutoEnter();
    // Stage25.5 CI stabilization：AutoEnter 墙钟节流（原实现每帧 -1/60 为帧率依赖，
    // llvmpipe 软渲染低 FPS 下 30s 冒烟窗口内走不完链路；墙钟在任意帧率下行为一致）。
    void AutoEnterWait(float seconds);
    bool AutoEnterGate() const;
    // 瞬时错误（依赖服务未 Healthy / Db 冷启动 / RPC 超时）→ 可重试；业务性拒绝不重试。
    static bool IsRetryableAccountCode(std::uint16_t code);

    std::shared_ptr<GameNetworkClient> m_client = std::make_shared<GameNetworkClient>();
    AccountClientController m_account{*m_client};
    WorldClientController m_world;
    CharacterSelectionController m_characterSelection;
    bool m_debugVisible = false;
    bool m_accountDebugVisible = false;
    bool m_worldDebugVisible = false;
    AccountFlowState m_prevAccountState = AccountFlowState::Disconnected;

    // F10 开发自动登录（指令六十一：注册 -> 登录；仅测试环境便捷用）
    enum class DevLoginStage { Idle, Registering, LoggingIn, Done };
    DevLoginStage m_devLoginStage = DevLoginStage::Idle;
    bool m_devRegisterAttempted = false; // Stage25.5：注册回退有界（每会话最多一次）
    static constexpr const char* kDevUsername = "dev_user";
    static constexpr const char* kDevPassword = "DevPass123!";

public:
    // Stage27 Multiplayer Chat Smoke：env 覆盖开发账号（两个冒烟客户端各用独立
    // 账号，避免同角色重复上线拒绝）。LEGEND_CLIENT_DEV_USER / LEGEND_CLIENT_DEV_PASS。
    static const char* DevUsername() {
        const char* env = SDL_getenv("LEGEND_CLIENT_DEV_USER");
        return env != nullptr && env[0] != '\0' ? env : kDevUsername;
    }
    static const char* DevPassword() {
        const char* env = SDL_getenv("LEGEND_CLIENT_DEV_PASS");
        return env != nullptr && env[0] != '\0' ? env : kDevPassword;
    }

private:

    // 阶段24：自动进世界（LEGEND_CLIENT_AUTO_ENTER=1；本地视觉冒烟链路）
    bool m_autoEnter = false;
    std::chrono::steady_clock::time_point m_autoEnterReadyAt{}; // 墙钟节流（非帧率）
    bool m_autoEnterCreated = false;   // 已尝试建角（防重复提交）
    int m_autoEnterCreateAttempts = 0; // 建角重试计数（瞬时失败有界重试）
    bool m_autoEnterLoggedIn = false;  // 已触发登录
    static constexpr int kAutoEnterMaxCreateAttempts = 5;
};

} // namespace legend::client
