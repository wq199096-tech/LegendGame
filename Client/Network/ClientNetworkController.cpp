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

ClientNetworkController::ClientNetworkController() = default;

void ClientNetworkController::Update(legend::input::InputManager& input, float deltaTime) {
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

    // 阶段9 指令八十四：F10 保留 LegacyDevLogin 测试（test/dev_token 链路验收）
    // 改为 Shift+F10；F10 = 阶段10 开发账号自动登录（指令六十一）
    if (input.IsKeyPressed(SDL_SCANCODE_F10)) {
        if (input.IsKeyDown(SDL_SCANCODE_LSHIFT) || input.IsKeyDown(SDL_SCANCODE_RSHIFT)) {
            if (m_client->State() == NetworkState::Ready && !m_client->IsAuthenticated()) {
                LOG_INFO("[Network] Sending test login request (Shift+F10).");
                m_client->SendLogin("test", "dev_token");
            }
        } else {
            // 阶段10 指令六十一：自动登录开发账号（不存在则先注册；正式流程测试
            // 不依赖 test/dev_token——账号链路测试全部走 Register/Login）
            if (m_client->State() == NetworkState::Ready &&
                m_account.State() == AccountFlowState::Unauthenticated) {
                LOG_INFO("[Account] Dev auto login requested (F10).");
                m_devLoginStage = DevLoginStage::LoggingIn;
                m_account.SendAccountLogin(kDevUsername, kDevPassword);
            }
        }
    }

    // 阶段10 指令六十：F11 = Account Debug（与 F8 Network Debug 无冲突）
    if (input.IsKeyPressed(SDL_SCANCODE_F11)) {
        m_accountDebugVisible = !m_accountDebugVisible;
        LOG_INFO(std::string("[Account] Debug overlay: ") +
                 (m_accountDebugVisible ? "enabled (F11)" : "disabled (F11)"));
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
        const NetworkEvent& event = events.front();
        // 阶段10 指令五十七：Account 事件先喂账号控制器（状态机更新）
        m_account.HandleEvent(event);
        HandleEvent(event);
        events.pop_front();
    }

    // 指令六十四：心跳超时检测
    m_client->UpdateHeartbeat(deltaTime);

    // F10 开发自动登录重试（注册 -> 登录）
    UpdateDevAutoLogin(deltaTime);
}

void ClientNetworkController::UpdateDevAutoLogin(float) {
    if (m_devLoginStage == DevLoginStage::Idle || m_devLoginStage == DevLoginStage::Done) {
        return;
    }
    if (m_client->State() != NetworkState::Ready) {
        m_devLoginStage = DevLoginStage::Idle;
        return;
    }
    if (m_devLoginStage == DevLoginStage::LoggingIn &&
        m_account.State() == AccountFlowState::Unauthenticated) {
        // 登录失败 -> 尝试注册一次再登录（开发账号不存在时自动创建）
        const std::uint16_t code = m_account.LastErrorCode();
        if (code == static_cast<std::uint16_t>(legend::account::AccountErrorCode::InvalidCredentials)) {
            LOG_INFO("[Account] Dev account missing; registering (F10).");
            m_devLoginStage = DevLoginStage::Registering;
            m_account.SendRegister(kDevUsername, kDevPassword);
            return;
        }
        m_devLoginStage = DevLoginStage::Done;
        return;
    }
    if (m_devLoginStage == DevLoginStage::Registering &&
        m_account.State() == AccountFlowState::Unauthenticated) {
        LOG_INFO("[Account] Dev account ready; logging in (F10).");
        m_devLoginStage = DevLoginStage::LoggingIn;
        m_account.SendAccountLogin(kDevUsername, kDevPassword);
        return;
    }
    if (m_account.State() == AccountFlowState::Authenticated) {
        m_devLoginStage = DevLoginStage::Done;
        // 登录成功后自动拉角色列表（无角色时提示创建——正式 UI 后续单独阶段）
        if (m_account.HasSession()) {
            m_account.SendCharacterList(m_account.SessionToken());
        }
    }
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
            m_account.OnNetworkDisconnected();
            m_devLoginStage = DevLoginStage::Idle;
            break;
        case NetworkEvent::Type::HandshakeSuccess:
            LOG_INFO("[Network] Handshake accepted connection=" +
                     std::to_string(m_client->ServerConnectionId()) + ".");
            m_account.SetState(AccountFlowState::Unauthenticated);
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
        default:
            break; // Account 响应已由 AccountClientController 处理
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

std::string ClientNetworkController::AccountStatusText() const {
    // 阶段10 指令六十：F11 Account Debug（Console/标题显示，不做 UI 美术）
    std::string text = " | Acc: ";
    text += AccountFlowStateName(m_account.State());
    text += " #";
    text += std::to_string(m_account.AccountId());
    text += m_account.HasSession() ? " session" : " no-session";
    text += " chars=";
    text += std::to_string(m_account.Characters().size());
    if (m_account.HasSelectedCharacter()) {
        text += " sel=";
        text += std::to_string(m_account.SelectedCharacterId());
    }
    if (m_account.LastErrorCode() != 0) {
        text += " err=";
        text += std::to_string(m_account.LastErrorCode());
    }
    return text;
}

} // namespace legend::client
