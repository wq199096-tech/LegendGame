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

    // 阶段11 指令八十：F12 = World Debug
    if (input.IsKeyPressed(SDL_SCANCODE_F12)) {
        m_worldDebugVisible = !m_worldDebugVisible;
        LOG_INFO(std::string("[World] Debug overlay: ") +
                 (m_worldDebugVisible ? "enabled (F12)" : "disabled (F12)"));
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

    // 阶段11 指令四十八：world 网络线程 -> 事件队列 -> 主线程控制器
    std::deque<WorldNetworkEvent> worldEvents;
    m_world.Client().PollEvents(worldEvents);
    while (!worldEvents.empty()) {
        m_world.HandleEvent(worldEvents.front());
        worldEvents.pop_front();
    }
    // 阶段12 指令三十七/四十七：主线程远程玩家插值
    m_world.UpdateRemotePlayers(deltaTime);
    // 阶段13 指令五十三/六十：主线程远程怪物插值
    m_world.UpdateRemoteMonsters(deltaTime);

    // 指令六十四：心跳超时检测
    m_client->UpdateHeartbeat(deltaTime);
    m_world.Client().UpdateHeartbeat(deltaTime);

    // F10 开发自动登录重试（注册 -> 登录）
    UpdateDevAutoLogin(deltaTime);

    // 阶段11：CharacterSelected -> 自动连世界；WorldReady -> 发送移动输入
    UpdateWorldFlow();
    UpdateWorldMoveInput(input, deltaTime);
}

void ClientNetworkController::UpdateWorldFlow() {
    const AccountFlowState accountState = m_account.State();
    // 阶段11 指令四十六：CharacterSelect 成功（拿到 ticket）-> 连接 WorldServer
    if (accountState == AccountFlowState::CharacterSelected &&
        m_prevAccountState != AccountFlowState::CharacterSelected) {
        if (!m_account.SelectionTicket().empty()) {
            m_world.EnterWorldWithTicket(m_account.SelectionTicket());
        }
    }
    if (accountState != AccountFlowState::CharacterSelected &&
        m_prevAccountState == AccountFlowState::CharacterSelected) {
        // 重新选择/删除角色等 -> 退出当前世界连接（需重新走 CharacterSelect）
        m_world.Disconnect();
    }
    m_prevAccountState = accountState;
}

void ClientNetworkController::UpdateWorldMoveInput(legend::input::InputManager& input,
                                                   float deltaTime) {
    if (!m_world.IsWorldReady()) {
        return;
    }
    // 阶段11 指令三十四/三十五：只发送输入方向（WASD），禁止绝对坐标。
    float dx = 0.0f;
    float dy = 0.0f;
    if (input.IsKeyDown(SDL_SCANCODE_D) || input.IsKeyDown(SDL_SCANCODE_RIGHT)) {
        dx += 1.0f;
    }
    if (input.IsKeyDown(SDL_SCANCODE_A) || input.IsKeyDown(SDL_SCANCODE_LEFT)) {
        dx -= 1.0f;
    }
    if (input.IsKeyDown(SDL_SCANCODE_S) || input.IsKeyDown(SDL_SCANCODE_DOWN)) {
        dy += 1.0f;
    }
    if (input.IsKeyDown(SDL_SCANCODE_W) || input.IsKeyDown(SDL_SCANCODE_UP)) {
        dy -= 1.0f;
    }
    if (dx == 0.0f && dy == 0.0f) {
        return;
    }
    m_world.SendMoveInput(dx, dy, deltaTime);
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
            // 阶段11：Gateway/Login 断开 -> 世界连接同步断开（需重新选角）
            m_world.Disconnect();
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

std::string ClientNetworkController::WorldStatusText() const {
    // 阶段11 指令八十：F12 World Debug（WorldState/WorldConnectionId/
    // SelectedCharacterId/MapId/ServerPosition/LastInputSequence/RTT/LastWorldError）
    std::string text = " | World: ";
    text += WorldFlowStateName(m_world.State());
    if (m_world.WorldConnectionId() != 0) {
        text += " #";
        text += std::to_string(m_world.WorldConnectionId());
    }
    text += " char=";
    text += std::to_string(m_world.CharacterId());
    text += " map=";
    text += std::to_string(m_world.MapId());
    text += " pos=(";
    text += std::to_string(static_cast<int>(m_world.ServerPositionX()));
    text += ",";
    text += std::to_string(static_cast<int>(m_world.ServerPositionY()));
    text += ")";
    text += " seq=";
    text += std::to_string(m_world.LastSentInputSequence());
    // 阶段12 指令七十二：F12 增加 RemotePlayers / LastRemoteBatchSize。
    text += " remotes=";
    text += std::to_string(m_world.RemotePlayers().Count());
    text += " batch=";
    text += std::to_string(m_world.LastRemoteBatchSize());
    // 阶段13 指令五十八：F12 增加 VisibleMonsters / LastMonsterBatchSize / MonsterCount。
    text += " monsters=";
    text += std::to_string(m_world.RemoteMonsters().Count());
    text += " mbatch=";
    text += std::to_string(m_world.LastMonsterBatchSize());
    // 阶段14 指令七十一：F12 增加本地 PlayerHP / Alive。
    text += " hp=";
    text += std::to_string(m_world.LocalCurrentHp());
    text += "/";
    text += std::to_string(m_world.LocalMaxHp());
    text += " alive=";
    text += m_world.LocalAlive() ? "1" : "0";
    // 阶段15 指令六十三/六十四：F12 增加 Mana 与 Casting 状态。
    text += " mana=";
    text += std::to_string(m_world.LocalCurrentMana());
    text += "/";
    text += std::to_string(m_world.LocalMaxMana());
    if (m_world.LocalCasting()) {
        text += " casting=skill:";
        text += std::to_string(m_world.ActiveSkillId());
        text += " cast:";
        text += std::to_string(static_cast<int>(m_world.CastProgress() * 100.0f));
        text += "%%";
    }
    // 阶段14 指令七十：前 4 只怪 HP current/max（不做正式血条 UI）。
    if (m_world.RemoteMonsters().Count() > 0) {
        text += " mHP[";
        int listed = 0;
        for (const auto& [id, monster] : m_world.RemoteMonsters().All()) {
            if (listed++ >= 4) {
                break;
            }
            if (listed > 1) {
                text += ",";
            }
            text += "#" + std::to_string(id) + " " + std::to_string(monster.CurrentHp()) + "/" +
                    std::to_string(monster.MaxHp()) + (monster.Alive() ? "" : "D");
        }
        text += "]";
    }
    // 阶段16 指令六十七：F12 增加 Self Effects 与 Monster Effects（仅 Debug 文本）。
    if (!m_world.LocalStatusEffects().All().empty()) {
        text += " SelfFx[";
        int listed = 0;
        for (const auto& [instanceId, effect] : m_world.LocalStatusEffects().All()) {
            if (listed++ >= 4) {
                break;
            }
            if (listed > 1) {
                text += ",";
            }
            text += std::to_string(effect.effectId) + "x" + std::to_string(effect.stacks) + " " +
                    std::to_string(effect.remainingMs / 1000) + "s";
        }
        text += "]";
    }
    {
        int listed = 0;
        for (const auto& [id, monster] : m_world.RemoteMonsters().All()) {
            if (monster.StatusEffects().Count() == 0) {
                continue;
            }
            if (listed++ >= 2) {
                break;
            }
            text += " mFx#" + std::to_string(id) + "[";
            int fx = 0;
            for (const auto& [instanceId, effect] : monster.StatusEffects().All()) {
                if (fx++ >= 3) {
                    break;
                }
                if (fx > 1) {
                    text += ",";
                }
                text += std::to_string(effect.effectId) + "x" + std::to_string(effect.stacks);
            }
            text += "]";
        }
    }
    // 阶段12 指令四十四：Debug 列出前 4 个远程玩家名（无世界内文字渲染器，
    // 名字随 F12 面板显示；正式头顶 UI 后续单独阶段）。
    if (m_world.RemotePlayers().Count() > 0) {
        text += " [";
        int listed = 0;
        for (const auto& [id, remote] : m_world.RemotePlayers().All()) {
            if (listed++ >= 4) {
                break;
            }
            if (listed > 1) {
                text += ",";
            }
            text += remote.Name();
        }
        text += "]";
    }
    if (m_world.RttMs() >= 0.0f) {
        text += " rtt=";
        text += std::to_string(static_cast<int>(m_world.RttMs()));
        text += "ms";
    }
    if (!m_world.LastError().empty()) {
        text += " err=" + m_world.LastError();
    }
    return text;
}

} // namespace legend::client
