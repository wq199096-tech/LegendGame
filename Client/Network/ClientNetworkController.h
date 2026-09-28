#pragma once

#include "Client/Account/AccountClientController.h"
#include "Client/Account/CharacterSelectionController.h"
#include "Client/Network/GameNetworkClient.h"
#include "Client/WorldNetwork/WorldClientController.h"

#include "Engine/Input/InputManager.h"

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
    static constexpr const char* kDevUsername = "dev_user";
    static constexpr const char* kDevPassword = "DevPass123!";
};

} // namespace legend::client
