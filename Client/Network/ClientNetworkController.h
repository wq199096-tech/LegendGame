#pragma once

#include "Client/Network/GameNetworkClient.h"

#include "Engine/Input/InputManager.h"

#include <memory>
#include <string>

namespace legend::client {

// 阶段9 指令八十一~八十四：ClientNetworkController——GameScene 只调用
// Update()（指令六：网络逻辑绝不进 GameScene）。
// F8 = Network Debug 状态 / F9 = Connect/Reconnect / F10 = 测试 LoginRequest。
class ClientNetworkController {
public:
    // 每帧调用：处理 F8/F9/F10 + 消费网络事件（打日志）+ 心跳超时检测
    void Update(legend::input::InputManager& input, float deltaTime);

    GameNetworkClient& Client() { return *m_client; }
    const GameNetworkClient& Client() const { return *m_client; }
    bool DebugVisible() const { return m_debugVisible; }
    // F8 状态文本（指令八十三：State/Gateway/ConnectionId/Handshake/Auth/Account/RTT/Last Error）
    std::string StatusText() const;

private:
    void HandleEvent(const NetworkEvent& event);

    std::shared_ptr<GameNetworkClient> m_client = std::make_shared<GameNetworkClient>();
    bool m_debugVisible = false;
};

} // namespace legend::client
