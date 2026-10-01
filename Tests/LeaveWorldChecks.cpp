// Stage26 指令十七/二十七：主动离开世界（LeaveWorld）链路测试。
// 覆盖：GatewaySession 状态回退（纯逻辑）+ 真实服务 E2E（进入 → 主动离开 →
// 位置保存 → 重新选角签发新 ticket → 再次进入）。
// NOTE：WorldTestServers 的 E2E 拓扑为内联路由（Gateway worldPort=0，客户端直连
// World 端口），Gateway 的 world 代理回退（CloseWorldProxy -> MarkLeftWorld）由
// GatewaySessionLeftWorldStateCheck 以状态机单测覆盖；正式拓扑的代理关闭回调路径
// 与该状态机转换一一对应。

#include "Tests/WorldTestHarness.h"

#include "Server/Gateway/GatewaySession.h"

namespace {
using namespace worldtest;

void RunGatewaySessionLeftWorldStateCheck() {
    // GatewaySession 仅用于状态机验证（MarkXxx 不触碰连接对象）。
    legend::net::TcpConnectionPtr nullConnection;
    legend::gateway::GatewaySession session(nullConnection, 7001);

    session.MarkAuthenticated(123);
    Check("GatewayLeftWorldStateCheck: Authenticated after login",
          session.State() == legend::gateway::GatewaySessionState::Authenticated);
    session.MarkCharacterSelected(456);
    Check("GatewayLeftWorldStateCheck: CharacterSelected after select",
          session.State() == legend::gateway::GatewaySessionState::CharacterSelected);
    session.MarkInWorld();
    Check("GatewayLeftWorldStateCheck: InWorld after world channel",
          session.State() == legend::gateway::GatewaySessionState::InWorld);
    session.MarkLeftWorld();
    Check("GatewayLeftWorldStateCheck: back to Authenticated after LeaveWorld (stage26)",
          session.State() == legend::gateway::GatewaySessionState::Authenticated);
    // 回退后必须仍可路由角色消息（重拉列表/重新选角）。
    Check("GatewayLeftWorldStateCheck: character messages routable after leave",
          session.CanRoute(static_cast<std::uint16_t>(legend::network::MessageId::CharacterListRequest)) &&
              session.CanRoute(static_cast<std::uint16_t>(
                  legend::network::MessageId::CharacterSelectRequest)));
    // 世界消息在回退后必须被拒绝（不再 InWorld）。
    Check("GatewayLeftWorldStateCheck: world messages rejected after leave",
          !session.CanRoute(static_cast<std::uint16_t>(legend::network::MessageId::PlayerMoveInput)));
}

void RunLeaveWorldReenterCheck(WorldTestServers& servers) {
    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    Check("LeaveWorldChecks: db ready", dbOk);
    if (!dbOk) {
        return;
    }
    AccountService accounts(5, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);

    CharacterSeed hero;
    const bool seeded = SeedAccountAndCharacter(db, accounts, characters, "leave_user", "LeaveHero", hero);
    Check("LeaveWorldChecks: seed ready", seeded);

    // ---- 第一次进入世界 ----
    std::string ticket1;
    if (seeded && servers.login) {
        ticket1 = servers.login->Tickets().Create(hero.accountId, hero.characterId, 60.0);
    }
    WorldTestClient first;
    const bool entered1 = !ticket1.empty() && first.ConnectAndEnter(ticket1, 8000);
    Check("LeaveWorldChecks: first enter world success", entered1 && first.controller.IsWorldReady());

    // ---- 主动离开（指令十七）----
    bool leaveOk = false;
    if (entered1) {
        first.controller.SendLeaveWorld();
        leaveOk = first.WaitEvent(WorldNetworkEvent::Type::LeaveWorldSuccess, 8000);
    }
    Check("LeaveWorldChecks: LeaveWorldResponse success received", leaveOk);
    // 客户端在收到响应后本地断开 world 连接（服务器同样会关）。
    Check("LeaveWorldChecks: world flow disconnected after leave",
          first.controller.State() == WorldFlowState::Disconnected);

    // 位置已保存（SavePlayerPositionNow 同步写；未出生哨兵 -1 必须被清除）。
    const double savedX = QueryScalarDouble(
        servers.dbPath, "SELECT position_x FROM characters WHERE id=" +
                            std::to_string(hero.characterId) + ";");
    Check("LeaveWorldChecks: position persisted after leave (no spawn sentinel)",
          savedX >= 0.0);

    // ---- 重新选角 + 再次进入（SelectionTicket 一次性：必须用新 ticket）----
    std::string ticket2;
    if (seeded && servers.login) {
        ticket2 = servers.login->Tickets().Create(hero.accountId, hero.characterId, 60.0);
    }
    WorldTestClient second;
    const bool entered2 = !ticket2.empty() && second.ConnectAndEnter(ticket2, 8000);
    Check("LeaveWorldChecks: re-enter world with new ticket after leave",
          entered2 && second.controller.IsWorldReady());
    second.Disconnect();
    WaitUntil([&] { second.DrainEvents(); return true; }, 200);
    first.Disconnect();
    WaitUntil([&] { first.DrainEvents(); return true; }, 200);
}

} // namespace

// 由 WorldChecks.cpp 的 main 调用（声明位于 worldtest 命名空间）。
namespace worldtest {
int RunLeaveWorldChecks(WorldTestServers& servers) {
    RunGatewaySessionLeftWorldStateCheck();
    RunLeaveWorldReenterCheck(servers);
    return 0;
}
} // namespace worldtest
