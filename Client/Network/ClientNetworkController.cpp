#include "Client/Network/ClientNetworkController.h"

#include "Engine/Debug/Logger.h"

namespace legend::client {

namespace {
const char* StateName(NetworkState state) {
    switch (state) {
        case NetworkState::Disconnected: return "Disconnected";
        case NetworkState::Connecting: return "Connecting";
        case NetworkState::Connected: return "Connected";
        case NetworkState::Handshaking: return "Handshaking";
        case NetworkState::Ready: return "Ready";
        case NetworkState::Failed: return "Failed";
    }
    return "Unknown";
}
} // namespace

void ClientNetworkController::Update(legend::input::InputManager& input, float deltaTime) {
    (void)deltaTime;

    // 指令八十四：F9 = Connect / Reconnect Gateway（失败不阻塞主循环，指令一百一十九）
    if (input.IsKeyPressed(SDL_SCANCODE_F9)) {
        const NetworkState state = m_client->State();
        if (state == NetworkState::Disconnected || state == NetworkState::Failed) {
            LOG_INFO("[Network] Connecting to gateway " + m_client->GetConfig().gatewayHost + ":" +
                     std::to_string(m_client->GetConfig().gatewayPort) + "...");
            m_client->Connect(m_client->GetConfig().gatewayHost, m_client->GetConfig().gatewayPort);
        } else {
            LOG_INFO("[Network] Disconnect requested (F9).");
            m_client->Disconnect(true);
        }
    }

    // 指令八十四：F10 = 发送测试 LoginRequest（test/dev_token，仅开发链路验收）
    if (input.IsKeyPressed(SDL_SCANCODE_F10)) {
        if (m_client->State() == NetworkState::Ready && !m_client->IsAuthenticated()) {
            LOG_INFO("[Network] Sending test login request (F10).");
            m_client->SendLogin("test", "dev_token");
        }
    }

    // 指令八十三：F8 = Network Debug（切换时输出一次状态）
    if (input.IsKeyPressed(SDL_SCANCODE_F8)) {
        m_debugVisible = !m_debugVisible;
        LOG_INFO(std::string("[Network] Debug overlay: ") +
                 (m_debugVisible ? "enabled (F8)" : "disabled (F8)"));
    }

    // 指令三十一：主线程消费事件队列（绝不跨线程操作游戏对象）
    std::deque<NetworkEvent> events;
    m_client->PollEvents(events);
    while (!events.empty()) {
        HandleEvent(events.front());
        events.pop_front();
    }

    // 指令六十四：心跳超时检测
    m_client->UpdateHeartbeat(deltaTime);
}

void ClientNetworkController::HandleEvent(const NetworkEvent& event) {
    switch (event.type) {
        case NetworkEvent::Type::Connected:
            // 指令九十一：[Network] Connected to gateway.
            LOG_INFO("[Network] Connected to gateway.");
            break;
        case NetworkEvent::Type::ConnectFailed:
            LOG_WARN("[Network] Connect failed: " + event.message);
            break;
        case NetworkEvent::Type::Disconnected:
            LOG_INFO("[Network] Disconnected: " + event.message);
            break;
        case NetworkEvent::Type::HandshakeSuccess:
            LOG_INFO("[Network] Handshake accepted connection=" +
                     std::to_string(m_client->ServerConnectionId()) + ".");
            break;
        case NetworkEvent::Type::HandshakeFailed:
            LOG_WARN("[Network] Handshake rejected: " + event.message);
            break;
        case NetworkEvent::Type::LoginResponse:
            if (event.loginSuccess) {
                LOG_INFO("[Network] Login success account=" + std::to_string(event.accountId) +
                         " display=" + event.displayName + ".");
            } else {
                LOG_WARN("[Network] Login failed: " + event.message);
            }
            break;
        case NetworkEvent::Type::HeartbeatTimeout:
            LOG_WARN("[Network] Heartbeat timeout: " + event.message);
            break;
    }
}

std::string ClientNetworkController::StatusText() const {
    std::string text = "Net: ";
    text += StateName(m_client->State());
    if (m_client->State() == NetworkState::Ready) {
        text += " #";
        text += std::to_string(m_client->ServerConnectionId());
        if (m_client->IsAuthenticated()) {
            text += " auth";
        }
        if (m_client->LastRttMs() >= 0.0f) {
            text += " rtt=";
            text += std::to_string(static_cast<int>(m_client->LastRttMs()));
            text += "ms";
        }
    } else if (m_client->State() == NetworkState::Failed) {
        text += " (" + m_client->LastError() + ")";
    }
    return text;
}

} // namespace legend::client
