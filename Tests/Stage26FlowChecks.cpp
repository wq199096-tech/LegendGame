// Stage26 指令三十六~四十一：Production Login & Character Lobby 测试。
// 覆盖：纯逻辑（UTF-8 文本编辑/造型映射/错误文案）+ 完整流程 E2E
//（注册 -> 登录 -> 空列表 -> 建角(中文+造型) -> 进世界 -> 主动离开回大厅 ->
//  重进 -> 服务器重启后角色仍在 -> 第五角色本地预检 -> 跨账号删除拒绝）。

#include "Tests/WorldTestHarness.h"

#include "Client/Account/AccountClientController.h"
#include "Client/Account/CharacterSelectionController.h"
#include "Client/Flow/ClientFlowController.h"
#include "Client/Network/GameNetworkClient.h"
#include "Client/Ui/CharacterVisualCatalog.h"
#include "Client/Ui/FlowUiModel.h"
#include "Client/Ui/PlayerFacingErrorCatalog.h"
#include "Client/WorldNetwork/WorldClientController.h"

namespace {

using namespace worldtest;

using legend::client::AccountClientController;
using legend::client::CharacterSelectionController;
using legend::client::GameNetworkClient;
using legend::client::WorldClientController;
using legend::flow::ClientFlowController;
using legend::flow::ClientFlowState;
using legend::flow::ClientFlowStateName;
using legend::flow::FlowTextAppend;
using legend::flow::FlowTextBackspace;
using legend::flow::FlowTextCodepointCount;
using legend::ui::CharacterVisualDisplayName;
using legend::ui::CharacterVisualEntityName;
using legend::ui::PlayerFacingAccountErrorText;
namespace account = legend::account;

// ---------------------------------------------------------------------------
// 纯逻辑：文本编辑 / 目录映射 / 错误文案
// ---------------------------------------------------------------------------

void RunFlowPureChecks() {
    // ---- FlowUtf8EditingCheck：码点安全追加/退格 ----
    {
        std::string text;
        FlowTextAppend(text, "abc", 12);
        FlowTextAppend(text, "英雄", 12);
        Check("FlowUtf8EditingCheck: ascii + cjk append",
              FlowTextCodepointCount(text) == 5 && text == "abc英雄");
        FlowTextBackspace(text); // 删除整只"雄"
        Check("FlowUtf8EditingCheck: backspace removes whole codepoint",
              text == "abc英");
        // 截断的 UTF-8 尾部必须被丢弃（绝不能拼出非法序列）。
        std::string truncated = "x";
        FlowTextAppend(truncated, "\xE4\xB8", 12); // "中"的前两个字节
        Check("FlowUtf8EditingCheck: truncated utf8 rejected", truncated == "x");
        // 上限：3 个码点后拒绝追加。
        std::string capped;
        FlowTextAppend(capped, "英雄男", 3);
        FlowTextAppend(capped, "女", 3);
        Check("FlowUtf8EditingCheck: codepoint limit enforced",
              FlowTextCodepointCount(capped) == 3 && capped == "英雄男");
        // 空串退格安全。
        std::string empty;
        FlowTextBackspace(empty);
        Check("FlowUtf8EditingCheck: backspace on empty safe", empty.empty());
    }

    // ---- FlowStateNameCheck：所有状态名非空 ----
    {
        bool allNamed = true;
        const ClientFlowState states[] = {
            ClientFlowState::Boot,      ClientFlowState::Connecting,
            ClientFlowState::Login,     ClientFlowState::Register,
            ClientFlowState::CharacterLobby, ClientFlowState::CharacterCreate,
            ClientFlowState::EnteringWorld,  ClientFlowState::InWorld,
            ClientFlowState::Disconnected,   ClientFlowState::FatalError,
        };
        for (const auto state : states) {
            const char* name = ClientFlowStateName(state);
            if (name == nullptr || name[0] == '\0') {
                allNamed = false;
            }
        }
        Check("FlowStateNameCheck: every state has a name", allNamed);
    }

    // ---- FlowErrorCatalogCheck：玩家可读文案（全简体中文，未知码有兜底）----
    {
        bool allNonEmpty = true;
        const std::uint16_t codes[] = {1, 2, 3, 4, 5, 10, 11, 12, 13,
                                       20, 30, 31, 32, 33, 34, 40, 41, 42};
        for (const std::uint16_t code : codes) {
            const char* text = PlayerFacingAccountErrorText(code);
            if (text == nullptr || text[0] == '\0') {
                allNonEmpty = false;
            }
        }
        Check("FlowErrorCatalogCheck: all known codes mapped", allNonEmpty);
        Check("FlowErrorCatalogCheck: zero code empty + unknown fallback",
              PlayerFacingAccountErrorText(0)[0] == '\0' &&
                  PlayerFacingAccountErrorText(99)[0] != '\0');
    }

    // ---- FlowVisualCatalogCheck：造型 1/2/3 映射 + 兜底 ----
    {
        Check("FlowVisualCatalogCheck: visual 1/2/3 entities",
              std::string(CharacterVisualEntityName(1)) == "player_warrior" &&
                  std::string(CharacterVisualEntityName(2)) == "player_mage" &&
                  std::string(CharacterVisualEntityName(3)) == "player_taoist");
        Check("FlowVisualCatalogCheck: invalid visual falls back",
              std::string(CharacterVisualEntityName(0)) == "player_warrior" &&
                  std::string(CharacterVisualEntityName(99)) == "player_warrior");
        Check("FlowVisualCatalogCheck: display names",
              std::string(CharacterVisualDisplayName(1)) == "造型一" &&
                  std::string(CharacterVisualDisplayName(2)) == "造型二" &&
                  std::string(CharacterVisualDisplayName(3)) == "造型三");
    }
}

// ---------------------------------------------------------------------------
// E2E：完整玩家流程（真实 Login + World + Gateway 内联拓扑）
// ---------------------------------------------------------------------------

struct FlowTestClient {
    std::shared_ptr<GameNetworkClient> net = std::make_shared<GameNetworkClient>();
    AccountClientController account{*net};
    std::unique_ptr<WorldClientController> world;
    CharacterSelectionController selection;
    std::unique_ptr<ClientFlowController> flow;

    void Init() {
        world = std::make_unique<WorldClientController>();
        world->SetWorldEndpoint("127.0.0.1", kWorldPort);
        net->SetConnectEndpoint("127.0.0.1", kGatewayPort); // 内联拓扑网关端口
        flow = std::make_unique<ClientFlowController>(*net, account, *world, selection);
        flow->SetObserverOnly(false); // 完整玩家模式（非 AutoEnter 观测）
    }

    void DrainEvents() {
        std::deque<legend::client::NetworkEvent> events;
        net->PollEvents(events);
        for (auto& e : events) {
            // 镜像生产 ClientNetworkController::HandleEvent 的接线：
            // 握手成功 -> Unauthenticated（flow 只做状态推导）。
            if (e.type == legend::client::NetworkEvent::Type::HandshakeSuccess) {
                account.SetState(AccountFlowState::Unauthenticated);
            }
            account.HandleEvent(e);
        }
        std::deque<legend::client::WorldNetworkEvent> worldEvents;
        world->Client().PollEvents(worldEvents);
        for (auto& e : worldEvents) {
            world->HandleEvent(e);
        }
    }

    // 每帧推进（dt=0.1：Boot 1.2s = 12 tick；连接超时 6s 不会被误触发）。
    void Tick() {
        DrainEvents();
        flow->Update(0.1f);
        // 镜像生产：心跳由 ClientNetworkController::Update 每帧泵入。
        net->UpdateHeartbeat(0.1f);
        world->Client().UpdateHeartbeat(0.1f);
    }
};

// 大厅"稳态"：状态=CharacterLobby 且服务器回合已结束（列表已就绪、不 busy）。
// 页面动作只允许在稳态发起（与生产 GameScene 的"Update 后分发"帧序一致）。
bool WaitLobbySteady(FlowTestClient& c, int timeoutMs = 15000) {
    return WaitUntil(
        [&] {
            c.Tick();
            return c.flow->State() == ClientFlowState::CharacterLobby &&
                   c.account.State() == AccountFlowState::CharacterListReady &&
                   !c.flow->Model().busy;
        },
        timeoutMs);
}

// 进世界辅助：flow 到 EnteringWorld 后由"上层"（模拟 ClientNetworkController
// 的 UpdateWorldFlow）用一次性 ticket 调 EnterWorldWithTicket。
// 关键：必须等**本次选角新签发**的 ticket（SelectionTicket 与调用前不同）——
// 离开后 account 仍持有上次已消费的旧票，立即用旧票必然 InvalidTicket。
bool DriveEnterWorld(FlowTestClient& c, bool& worldEnterAttempted) {
    const std::string previousTicket = c.account.SelectionTicket();
    const bool selecting = WaitUntil(
        [&] {
            c.Tick();
            if (!worldEnterAttempted &&
                c.flow->State() == ClientFlowState::EnteringWorld &&
                c.account.HasSelectedCharacter() &&
                !c.account.SelectionTicket().empty() &&
                c.account.SelectionTicket() != previousTicket) {
                worldEnterAttempted = true;
                c.world->EnterWorldWithTicket(c.account.SelectionTicket());
            }
            return c.flow->State() == ClientFlowState::InWorld;
        },
        20000);
    return selecting && c.world->IsWorldReady();
}

void RunFlowLifecycleE2E(WorldTestServers& servers) {
    // ---- 注册 -> 登录 -> 空列表 -> 建角（中文名 + 造型二）----
    FlowTestClient c1;
    c1.Init();
    const bool login = WaitUntil(
        [&] {
            c1.Tick();
            return c1.flow->State() == ClientFlowState::Login;
        },
        15000);
    Check("Stage26FlowChecks: Boot auto-connect -> Login page", login);
    if (!login) {
        return;
    }
    // 默认启动绝不自动登录（Login 页等待输入）。
    c1.flow->Model().regAccount = "flow_user1";
    c1.flow->Model().regPassword = "FlowPass123!";
    c1.flow->Model().regPassword2 = "FlowPass123!";
    c1.flow->RequestOpenRegister();
    Check("Stage26FlowChecks: open register page",
          c1.flow->State() == ClientFlowState::Register);
    c1.flow->RequestRegister();
    const bool registered = WaitUntil(
        [&] {
            c1.Tick();
            return c1.flow->State() == ClientFlowState::Login;
        },
        15000);
    Check("Stage26FlowChecks: register done -> back to login page", registered);

    c1.flow->Model().accountName = "flow_user1";
    c1.flow->Model().password = "FlowPass123!";
    c1.flow->RequestLogin();
    const bool lobby1 = WaitLobbySteady(c1);
    Check("Stage26FlowChecks: login -> character lobby (empty list)",
          lobby1 && c1.flow->Model().characters.empty());
    if (!lobby1) {
        return;
    }

    // 创建角色：中文名 + 造型二（visualId=2 持久化）。
    c1.flow->RequestOpenCreate();
    Check("Stage26FlowChecks: open create page",
          c1.flow->State() == ClientFlowState::CharacterCreate);
    c1.flow->Model().newName = "流英雄";
    c1.flow->Model().newVisualId = 2;
    c1.flow->RequestSubmitCreate();
    const bool created = WaitLobbySteady(c1);
    Check("Stage26FlowChecks: create CJK character with visualId 2",
          created && c1.flow->Model().characters.size() == 1 &&
              c1.flow->Model().characters[0].name == "流英雄" &&
              c1.flow->Model().characters[0].visualId == 2);
    if (!created) {
        return;
    }

    // ---- 进世界 -> 主动离开 -> 回大厅（列表保留）----
    bool worldEnter1 = false;
    c1.flow->RequestEnterWorld(0); // 大厅点"进入游戏"（index 0 = 流英雄）
    const bool inWorld1 = DriveEnterWorld(c1, worldEnter1);
    Check("Stage26FlowChecks: enter world after select", inWorld1);
    if (!inWorld1) {
        return;
    }
    c1.flow->RequestLeaveWorld();
    const bool backToLobby = WaitLobbySteady(c1);
    Check("Stage26FlowChecks: leave world -> back to lobby (character kept)",
          backToLobby && c1.flow->Model().characters.size() == 1 &&
              c1.flow->Model().characters[0].name == "流英雄");

    // ---- 重新进入（新 ticket；角色数据仍在）----
    bool worldEnter2 = false;
    c1.flow->RequestEnterWorld(0);
    const bool inWorld2 = DriveEnterWorld(c1, worldEnter2);
    Check("Stage26FlowChecks: re-enter world after leave", inWorld2);

    // ---- 服务器重启后角色仍在（同 dbPath）----
    c1.flow->RequestLeaveWorld();
    const bool leaveBeforeRestart = WaitLobbySteady(c1);
    c1.net->Disconnect(true);
    WaitUntil([&] { c1.Tick(); return true; }, 200);
    servers.StopAll();
    Check("Stage26FlowChecks: servers restart after stop", servers.StartLogin() &&
                                                             servers.StartWorld() &&
                                                             servers.StartGateway());
    FlowTestClient c2;
    c2.Init();
    const bool login2 = WaitUntil(
        [&] {
            c2.Tick();
            return c2.flow->State() == ClientFlowState::Login;
        },
        15000);
    c2.flow->Model().accountName = "flow_user1";
    c2.flow->Model().password = "FlowPass123!";
    c2.flow->RequestLogin();
    const bool lobby2 = WaitLobbySteady(c2);
    Check("Stage26FlowChecks: character persists after server restart",
          leaveBeforeRestart && login2 && lobby2 &&
              c2.flow->Model().characters.size() == 1 &&
              c2.flow->Model().characters[0].name == "流英雄");

    // ---- 第五角色：本地预检拒绝（服务器权威上限已在 AccountChecks 覆盖）----
    const std::uint64_t heroId = lobby2 && !c2.flow->Model().characters.empty()
                                     ? c2.flow->Model().characters[0].characterId
                                     : 0;
    bool fifthRejected = false;
    for (int i = 2; i <= 4 && lobby2; ++i) {
        c2.flow->RequestOpenCreate();
        if (c2.flow->State() != ClientFlowState::CharacterCreate) {
            break;
        }
        c2.flow->Model().newName = "英雄" + std::to_string(i);
        c2.flow->Model().newVisualId = 1;
        c2.flow->RequestSubmitCreate();
        const bool done = WaitLobbySteady(c2);
        if (!done) {
            break;
        }
    }
    if (lobby2) {
        c2.flow->RequestOpenCreate(); // 已有 4 个 -> 本地预检拒绝
        fifthRejected = c2.flow->State() == ClientFlowState::CharacterLobby &&
                        c2.flow->Model().lastErrorCode == 32;
    }
    Check("Stage26FlowChecks: 5th character rejected (limit precheck)", fifthRejected);

    // ---- 跨账号删除拒绝（服务器权威：CharacterNotOwned）----
    {
        FlowTestClient c3;
        c3.Init();
        const bool login3 = WaitUntil(
            [&] {
                c3.Tick();
                return c3.flow->State() == ClientFlowState::Login;
            },
            15000);
        c3.flow->Model().regAccount = "flow_user2";
        c3.flow->Model().regPassword = "FlowPass123!";
        c3.flow->Model().regPassword2 = "FlowPass123!";
        c3.flow->RequestOpenRegister();
        c3.flow->RequestRegister();
        WaitUntil(
            [&] {
                c3.Tick();
                return c3.flow->State() == ClientFlowState::Login;
            },
            15000);
        c3.flow->Model().accountName = "flow_user2";
        c3.flow->Model().password = "FlowPass123!";
        c3.flow->RequestLogin();
        const bool lobby3 = WaitLobbySteady(c3);
        bool rejected = false;
        if (lobby3 && heroId != 0) {
            // 正式 UI 不可能发起该请求（列表只含本人角色）；直接驱动控制器
            // 验证服务器权威所有权校验（CharacterNotOwned=34）。
            c3.account.SendDeleteCharacter(c3.account.SessionToken(), heroId);
            rejected = WaitUntil(
                [&] {
                    c3.DrainEvents();
                    return c3.account.LastErrorCode() ==
                           static_cast<std::uint16_t>(
                               account::AccountErrorCode::CharacterNotOwned);
                },
                15000);
        }
        Check("Stage26FlowChecks: cross-account delete rejected (34)", rejected);
        c3.net->Disconnect(true);
        WaitUntil([&] { c3.Tick(); return true; }, 200);
    }
}

} // namespace

// 由 WorldChecks.cpp 的 main 调用（声明位于 worldtest 命名空间）。
namespace worldtest {
int RunStage26FlowChecks(WorldTestServers& servers) {
    RunFlowPureChecks();
    RunFlowLifecycleE2E(servers);
    return 0;
}
} // namespace worldtest
