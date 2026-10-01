#include "Client/Flow/ClientFlowController.h"

#include "Client/Flow/ClientLoginStore.h"
#include "Client/Ui/CharacterVisualCatalog.h"
#include "Engine/Debug/Logger.h"

namespace legend::flow {

namespace {
using legend::client::AccountFlowState;
using legend::client::NetworkState;
using legend::client::WorldFlowState;

constexpr float kBootSplashSeconds = 1.2f;    // 启动画面最短停留
constexpr float kConnectTimeoutSeconds = 6.0f; // 连接超时 -> 断线页
constexpr std::size_t kNameMaxCodepoints = 12;

bool IsNetReady(NetworkState state) {
    return state == NetworkState::Ready;
}

bool IsNetDown(NetworkState state) {
    return state == NetworkState::Disconnected || state == NetworkState::Failed;
}
} // namespace

ClientFlowController::ClientFlowController(legend::client::GameNetworkClient& net,
                                           legend::client::AccountClientController& account,
                                           legend::client::WorldClientController& world,
                                           legend::client::CharacterSelectionController& selection)
    : m_net(net), m_account(account), m_world(world), m_selection(selection) {
    // 只回填账号名（绝无密码/Token，指令三十）。
    m_model.accountName = ClientLoginStore::LoadLastAccountName();
}

void ClientFlowController::GoState(ClientFlowState state) {
    if (m_state == state) {
        return;
    }
    LOG_INFO("[Flow] " + std::string(ClientFlowStateName(m_state)) + " -> " +
             ClientFlowStateName(state));
    m_state = state;
    m_model.state = state;
    m_model.busyElapsed = 0.0f;
    m_model.deleteConfirmOpen = false;
    m_model.deleteConfirmText.clear();
    // 页面切换重置焦点。
    switch (state) {
        case ClientFlowState::Login:
            m_model.focusedField = FlowField::LoginAccount;
            break;
        case ClientFlowState::Register:
            m_model.focusedField = FlowField::RegAccount;
            break;
        case ClientFlowState::CharacterCreate:
            m_model.focusedField = FlowField::CreateName;
            m_model.newName.clear();
            m_model.newVisualId = 1;
            break;
        default:
            m_model.focusedField = FlowField::None;
            break;
    }
}

void ClientFlowController::MarkError() {
    const std::uint16_t code = m_account.LastErrorCode();
    if (code != 0) {
        m_model.lastErrorCode = code;
        m_model.lastErrorMessage = m_account.LastError();
    }
}

void ClientFlowController::DeriveState() {
    const NetworkState net = m_net.State();
    const AccountFlowState account = m_account.State();
    const WorldFlowState world = m_world.State();
    const bool inWorldReady = m_world.IsWorldReady();

    // 错误码变化时同步到模型（页面内嵌提示）。
    if (m_account.LastErrorCode() != 0 &&
        m_account.LastErrorCode() != m_model.lastErrorCode) {
        MarkError();
    }

    switch (m_state) {
        case ClientFlowState::Boot: {
            // 观测模式（AutoEnter）不主动连接；普通模式 Splash 后自动连接。
            if (!m_observerOnly && m_bootElapsed >= kBootSplashSeconds && !m_connectRequested &&
                IsNetDown(net)) {
                m_connectRequested = true;
                m_connectDeadline =
                    std::chrono::steady_clock::now() +
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<float>(kConnectTimeoutSeconds));
                LOG_INFO("[Flow] Boot -> connecting gateway.");
                m_net.Connect(m_net.GetConfig().gatewayHost, m_net.GetConfig().gatewayPort);
                GoState(ClientFlowState::Connecting);
            } else if (m_observerOnly && !IsNetDown(net)) {
                GoState(ClientFlowState::Connecting);
            } else if (!m_observerOnly && !IsNetDown(net)) {
                // F9 等外部路径已连接（罕见）：直接跟进。
                m_connectRequested = true;
                GoState(ClientFlowState::Connecting);
            }
            break;
        }
        case ClientFlowState::Connecting: {
            if (IsNetReady(net) && account == AccountFlowState::Unauthenticated) {
                m_model.lastErrorCode = 0;
                m_model.lastErrorMessage.clear();
                GoState(ClientFlowState::Login);
            } else if (IsNetDown(net) && m_connectRequested) {
                // Stage25.5 教训：超时用墙钟（dt 在低帧率/测试快放下与真实时间脱钩）。
                if (std::chrono::steady_clock::now() >= m_connectDeadline) {
                    MarkError();
                    GoState(ClientFlowState::Disconnected);
                }
            } else if (IsNetReady(net) && account != AccountFlowState::Unauthenticated &&
                       account != AccountFlowState::Disconnected) {
                // AutoEnter/dev 链路可能已推进（观测模式）：直接去大厅加载。
                GoState(ClientFlowState::CharacterLobby);
            }
            break;
        }
        case ClientFlowState::Login: {
            if (account == AccountFlowState::Registering) {
                GoState(ClientFlowState::Register);
            } else if (account == AccountFlowState::Authenticated ||
                       account == AccountFlowState::LoadingCharacters) {
                if (!m_observerOnly && account == AccountFlowState::Authenticated &&
                    m_account.HasSession() && !m_listRequested) {
                    m_listRequested = true;
                    m_account.SendCharacterList(m_account.SessionToken());
                }
                m_model.loginUserName = m_model.accountName; // 大厅顶部显示
                // 登录成功：清内存中的密码（不保留明文）。
                m_model.password.clear();
                GoState(ClientFlowState::CharacterLobby);
            } else if (IsNetDown(net)) {
                MarkError();
                GoState(ClientFlowState::Disconnected);
            }
            break;
        }
        case ClientFlowState::Register: {
            // 页面停留期 account 一直是 Unauthenticated（不能以此弹回）；
            // 只在观测到 Registering 后回到 Unauthenticated 才判定"注册往返完成"。
            if (account == AccountFlowState::Registering) {
                m_sawRegistering = true;
            }
            if (m_sawRegistering && account == AccountFlowState::Unauthenticated) {
                // 注册成功（或失败）：回登录页，账号名带入，错误码随带展示。
                m_sawRegistering = false;
                MarkError();
                m_model.accountName = m_model.regAccount;
                m_model.regPassword.clear();
                m_model.regPassword2.clear();
                GoState(ClientFlowState::Login);
            } else if (account == AccountFlowState::Authenticated ||
                       account == AccountFlowState::LoadingCharacters ||
                       account == AccountFlowState::CharacterListReady) {
                // dev/AutoEnter 链路在注册页期间完成登录：直接进大厅。
                GoState(ClientFlowState::CharacterLobby);
            } else if (IsNetDown(net)) {
                GoState(ClientFlowState::Disconnected);
            }
            break;
        }
        case ClientFlowState::CharacterLobby: {
            if (account == AccountFlowState::SelectingCharacter ||
                account == AccountFlowState::CharacterSelected) {
                GoState(ClientFlowState::EnteringWorld);
            } else if (account == AccountFlowState::Unauthenticated) {
                GoState(ClientFlowState::Login);
            } else if (IsNetDown(net)) {
                MarkError();
                GoState(ClientFlowState::Disconnected);
            }
            break;
        }
        case ClientFlowState::CharacterCreate: {
            // 页面停留期 account 一直是 CharacterListReady（不能以此弹回）；
            // 只在观测到 CreatingCharacter 后回落才判定"创建往返完成"。
            if (account == AccountFlowState::CreatingCharacter) {
                m_sawCreating = true;
            }
            if (m_sawCreating &&
                (account == AccountFlowState::CharacterListReady ||
                 account == AccountFlowState::Authenticated)) {
                // 创建完成（或失败）：带结果回大厅。
                m_sawCreating = false;
                MarkError();
                GoState(ClientFlowState::CharacterLobby);
            } else if (IsNetDown(net)) {
                GoState(ClientFlowState::Disconnected);
            }
            break;
        }
        case ClientFlowState::EnteringWorld: {
            if (inWorldReady) {
                GoState(ClientFlowState::InWorld);
            } else if (world == WorldFlowState::Failed) {
                // ticket 校验失败/世界未就绪：带错误回大厅（可重新进入）。
                m_model.lastErrorCode = 0;
                m_model.lastErrorMessage = m_world.LastError();
                MarkError();
                GoState(ClientFlowState::CharacterLobby);
            } else if (account == AccountFlowState::CharacterListReady ||
                       account == AccountFlowState::Authenticated) {
                // 选角失败回退。
                MarkError();
                GoState(ClientFlowState::CharacterLobby);
            } else if (IsNetDown(net)) {
                GoState(ClientFlowState::Disconnected);
            }
            break;
        }
        case ClientFlowState::InWorld: {
            if (!inWorldReady) {
                if (m_leaveRequested) {
                    // 主动离开：拉最新角色列表回大厅（Gateway 主连接保留）。
                    m_leaveRequested = false;
                    m_model.lastErrorCode = 0;
                    m_model.lastErrorMessage.clear();
                    if (m_account.HasSession()) {
                        m_account.SendCharacterList(m_account.SessionToken());
                    }
                    GoState(ClientFlowState::CharacterLobby);
                } else if (IsNetDown(net)) {
                    MarkError();
                    GoState(ClientFlowState::Disconnected);
                } else {
                    // 世界连接意外断开（网关仍在）：带错误回大厅。
                    m_model.lastErrorCode = 0;
                    m_model.lastErrorMessage = m_world.LastError().empty()
                                                   ? "与世界的连接已断开"
                                                   : m_world.LastError();
                    if (m_account.HasSession()) {
                        m_account.SendCharacterList(m_account.SessionToken());
                    }
                    GoState(ClientFlowState::CharacterLobby);
                }
            }
            break;
        }
        case ClientFlowState::Disconnected: {
            if (IsNetReady(net) && account == AccountFlowState::Unauthenticated) {
                m_model.lastErrorCode = 0;
                m_model.lastErrorMessage.clear();
                GoState(ClientFlowState::Login);
            }
            break;
        }
        case ClientFlowState::FatalError:
            break; // 终态（V0 无自动恢复）
    }

    m_prevAccountState = account;
    m_prevWorldState = world;
}

void ClientFlowController::Update(float deltaTime) {
    m_lastDt = deltaTime;
    m_model.busyElapsed += deltaTime;
    if (m_state == ClientFlowState::Boot) {
        m_bootElapsed += deltaTime;
    }

    DeriveState();

    // 模型同步（大厅列表/用户名/busy）。
    m_model.characters = m_account.Characters();
    m_model.busy = false;
    switch (m_account.State()) {
        case AccountFlowState::Connecting:
        case AccountFlowState::Handshake:
        case AccountFlowState::LoggingIn:
        case AccountFlowState::Registering:
        case AccountFlowState::LoadingCharacters:
        case AccountFlowState::CreatingCharacter:
        case AccountFlowState::DeletingCharacter:
        case AccountFlowState::SelectingCharacter:
            m_model.busy = true;
            break;
        default:
            break;
    }
    if (m_state == ClientFlowState::Connecting) {
        m_model.busy = true;
    }
    if (m_state == ClientFlowState::EnteringWorld) {
        m_model.busy = true;
    }
}

// ---------------------------------------------------------------------------
// 意图入口
// ---------------------------------------------------------------------------

void ClientFlowController::RequestRetryConnect() {
    if (m_state != ClientFlowState::Disconnected || m_observerOnly) {
        return;
    }
    m_connectRequested = true;
    m_connectDeadline =
        std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<float>(kConnectTimeoutSeconds));
    m_model.lastErrorCode = 0;
    m_model.lastErrorMessage.clear();
    m_net.Connect(m_net.GetConfig().gatewayHost, m_net.GetConfig().gatewayPort);
    GoState(ClientFlowState::Connecting);
}

void ClientFlowController::RequestLogin() {
    if (m_state != ClientFlowState::Login || m_model.busy || !IsNetReady(m_net.State())) {
        return;
    }
    if (m_model.accountName.empty() || m_model.password.empty()) {
        return;
    }
    m_model.lastErrorCode = 0;
    m_model.lastErrorMessage.clear();
    m_account.SendAccountLogin(m_model.accountName, m_model.password);
}

void ClientFlowController::RequestOpenRegister() {
    if (m_state != ClientFlowState::Login || m_model.busy) {
        return;
    }
    m_sawRegistering = false; // 页面切换：清"已提交"观测位
    m_model.lastErrorCode = 0;
    m_model.lastErrorMessage.clear();
    GoState(ClientFlowState::Register);
}

void ClientFlowController::RequestRegister() {
    if (m_state != ClientFlowState::Register || m_model.busy || !IsNetReady(m_net.State())) {
        return;
    }
    if (m_model.regAccount.empty() || m_model.regPassword.empty()) {
        return;
    }
    if (m_model.regPassword != m_model.regPassword2) {
        m_model.lastErrorCode = 0;
        m_model.lastErrorMessage = "两次输入的密码不一致";
        return;
    }
    m_model.lastErrorCode = 0;
    m_model.lastErrorMessage.clear();
    m_account.SendRegister(m_model.regAccount, m_model.regPassword);
}

void ClientFlowController::RequestCancelRegister() {
    if (m_state != ClientFlowState::Register || m_model.busy) {
        return;
    }
    m_model.regPassword.clear();
    m_model.regPassword2.clear();
    m_model.lastErrorCode = 0;
    m_model.lastErrorMessage.clear();
    GoState(ClientFlowState::Login);
}

void ClientFlowController::RequestOpenCreate() {
    if (m_state != ClientFlowState::CharacterLobby || m_model.busy) {
        return;
    }
    if (m_model.characters.size() >= legend::ui::kMaxCharactersPerAccountUi) {
        m_model.lastErrorCode = 32; // CharacterLimitReached（本地预判，服务器仍权威）
        m_model.lastErrorMessage.clear();
        return;
    }
    m_sawCreating = false; // 页面切换：清"已提交"观测位
    m_model.lastErrorCode = 0;
    m_model.lastErrorMessage.clear();
    GoState(ClientFlowState::CharacterCreate);
}

void ClientFlowController::RequestCancelCreate() {
    if (m_state != ClientFlowState::CharacterCreate || m_model.busy) {
        return;
    }
    m_sawCreating = false;
    m_model.lastErrorCode = 0;
    m_model.lastErrorMessage.clear();
    GoState(ClientFlowState::CharacterLobby);
}

void ClientFlowController::RequestSubmitCreate() {
    if (m_state != ClientFlowState::CharacterCreate || m_model.busy || !IsNetReady(m_net.State())) {
        return;
    }
    if (!legend::account::IsValidCharacterName(m_model.newName)) {
        m_model.lastErrorCode = 0;
        m_model.lastErrorMessage = "角色名不符合要求（2~12 个汉字、字母或数字）";
        return;
    }
    std::uint16_t visualId = m_model.newVisualId;
    if (visualId < legend::ui::kCharacterVisualIdMin ||
        visualId > legend::ui::kCharacterVisualIdMax) {
        visualId = 1;
    }
    m_model.lastErrorCode = 0;
    m_model.lastErrorMessage.clear();
    // classId 固定 1（Stage26 造型语义：visualId 决定外观实体）。
    m_account.SendCreateCharacter(m_account.SessionToken(), m_model.newName, 1, 1, visualId);
}

void ClientFlowController::RequestStartDelete(int index) {
    if (m_state != ClientFlowState::CharacterLobby || m_model.busy) {
        return;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= m_model.characters.size()) {
        return;
    }
    m_model.selectedIndex = index;
    m_model.deleteConfirmOpen = true;
    m_model.deleteConfirmText.clear();
    m_model.focusedField = FlowField::DeleteConfirm;
    m_model.lastErrorCode = 0;
    m_model.lastErrorMessage.clear();
}

void ClientFlowController::RequestConfirmDelete() {
    if (m_state != ClientFlowState::CharacterLobby || m_model.busy || !m_model.deleteConfirmOpen) {
        return;
    }
    if (m_model.selectedIndex < 0 ||
        static_cast<std::size_t>(m_model.selectedIndex) >= m_model.characters.size()) {
        return;
    }
    const auto& character = m_model.characters[static_cast<std::size_t>(m_model.selectedIndex)];
    // 指令二十六：删除必须重输角色名（归属二次确认；服务器仍会验证所有权）。
    if (m_model.deleteConfirmText != character.name) {
        m_model.lastErrorCode = 0;
        m_model.lastErrorMessage = "输入的角色名不一致";
        return;
    }
    m_model.lastErrorCode = 0;
    m_model.lastErrorMessage.clear();
    m_account.SendDeleteCharacter(m_account.SessionToken(), character.characterId);
}

void ClientFlowController::RequestCancelDelete() {
    m_model.deleteConfirmOpen = false;
    m_model.deleteConfirmText.clear();
    m_model.focusedField = FlowField::None;
}

void ClientFlowController::RequestEnterWorld(int index) {
    if (m_state != ClientFlowState::CharacterLobby || m_model.busy ||
        !IsNetReady(m_net.State())) {
        return;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= m_model.characters.size()) {
        return;
    }
    m_model.selectedIndex = index;
    const auto& character = m_model.characters[static_cast<std::size_t>(index)];
    m_model.lastErrorCode = 0;
    m_model.lastErrorMessage.clear();
    m_selection.RequestSelect(m_account, character.characterId);
}

void ClientFlowController::RequestSelectCharacter(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= m_model.characters.size()) {
        return;
    }
    m_model.selectedIndex = index;
}

void ClientFlowController::RequestLeaveWorld() {
    if (m_state != ClientFlowState::InWorld || m_leaveRequested) {
        return;
    }
    m_leaveRequested = true;
    m_world.SendLeaveWorld();
}

void ClientFlowController::RequestReturnToLogin() {
    if (m_state != ClientFlowState::CharacterLobby || m_model.busy) {
        return;
    }
    m_listRequested = false;
    m_net.Disconnect(true);
    m_model.password.clear();
    GoState(ClientFlowState::Disconnected);
}

void ClientFlowController::RequestFocusField(FlowField field) {
    m_model.focusedField = field;
}

void ClientFlowController::HandleEsc() {
    switch (m_state) {
        case ClientFlowState::Register:
            RequestCancelRegister();
            break;
        case ClientFlowState::CharacterCreate:
            RequestCancelCreate();
            break;
        case ClientFlowState::CharacterLobby:
            if (m_model.deleteConfirmOpen) {
                RequestCancelDelete();
            }
            break;
        default:
            break;
    }
}

void ClientFlowController::FeedTextInput(const std::vector<std::string>& utf8Chunks,
                                         bool backspace) {
    std::string* target = nullptr;
    std::size_t maxCodepoints = 64;
    switch (m_model.focusedField) {
        case FlowField::LoginAccount:
            target = &m_model.accountName;
            maxCodepoints = 32;
            break;
        case FlowField::LoginPassword:
            target = &m_model.password;
            maxCodepoints = 64;
            break;
        case FlowField::RegAccount:
            target = &m_model.regAccount;
            maxCodepoints = 32;
            break;
        case FlowField::RegPassword:
            target = &m_model.regPassword;
            maxCodepoints = 64;
            break;
        case FlowField::RegPassword2:
            target = &m_model.regPassword2;
            maxCodepoints = 64;
            break;
        case FlowField::CreateName:
            target = &m_model.newName;
            maxCodepoints = kNameMaxCodepoints;
            break;
        case FlowField::DeleteConfirm:
            target = &m_model.deleteConfirmText;
            maxCodepoints = 12;
            break;
        case FlowField::None:
            return;
    }
    if (target == nullptr) {
        return;
    }
    if (backspace) {
        FlowTextBackspace(*target);
    }
    for (const auto& chunk : utf8Chunks) {
        FlowTextAppend(*target, chunk, maxCodepoints);
    }
}

} // namespace legend::flow
