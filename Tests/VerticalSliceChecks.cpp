// ---------------------------------------------------------------------------
// 阶段25：Vertical Slice 检查（First Playable V0.25）。
// 仍链接 LegendWorldTests（不新增第四个测试 exe）。
// 当前：指令四 —— 新角色出生检查（服务器权威 Map1 300,300，Client 永不修正）。
// 后续追加：Chapter One 服务器链路（任务/掉落/成长/商店/Boss）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Server/LoginServer/Account/AccountRepository.h"

#include <cmath>
#include <string>

namespace worldtest {

namespace {

// 真实建号/建角（不钉位置 —— 需要未出生哨兵 (-1,-1) 走服务器权威出生路径）。
bool SeedNewCharacter(Database& db, AccountService& accounts, CharacterService& characters,
                      const std::string& username, const std::string& charName,
                      CharacterSeed& out) {
    auto registered = accounts.Register(db, username, "SlicePass123!");
    if (!registered.success) {
        return false;
    }
    auto created = characters.Create(db, registered.value, charName, 1, 1);
    if (!created.success) {
        return false;
    }
    out.accountId = registered.value;
    out.characterId = created.value.characterId;
    out.name = charName;
    return true;
}

void RunNewCharacterSpawnChecks(WorldTestServers& servers) {
    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(5, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("NewCharacterSpawnCheck: db ready", false);
        return;
    }

    CharacterSeed seed;
    bool ok = SeedNewCharacter(db, accounts, characters, "slice_user_1", "SliceHero", seed);
    Check("NewCharacterSpawnCheck: character created", ok);

    std::string ticket;
    if (ok && servers.login) {
        ticket = servers.login->Tickets().Create(seed.accountId, seed.characterId, 60.0);
    }
    WorldTestClient client;
    ok = ok && !ticket.empty() && client.ConnectAndEnter(ticket, 8000);
    const WorldNetworkEvent& enter = client.LastEnterSuccess();
    ok = ok && std::fabs(enter.positionX - 300.0f) < 0.01f &&
         std::fabs(enter.positionY - 300.0f) < 0.01f && enter.mapId == 1;
    Check("NewCharacterSpawnCheck: spawn at Map1 300,300 (server authoritative)", ok);

    // 落地后立即持久化（哨兵不得残留；重登不得再次触发出生修正）
    const bool persisted = WaitUntil(
        [&] {
            return std::fabs(QueryScalarDouble(servers.dbPath,
                                               "SELECT position_x FROM characters WHERE id=" +
                                                   std::to_string(seed.characterId) + ";") -
                              300.0) < 0.01;
        },
        3000);
    Check("NewCharacterSpawnCheck: spawn position persisted immediately", persisted);
    ok = ok && persisted;

    client.Disconnect();
    WaitUntil([&] { client.DrainEvents(); return true; }, 300);

    std::string ticket2;
    if (servers.login) {
        ticket2 = servers.login->Tickets().Create(seed.accountId, seed.characterId, 60.0);
    }
    WorldTestClient client2;
    ok = ok && !ticket2.empty() && client2.ConnectAndEnter(ticket2, 8000);
    const WorldNetworkEvent& enter2 = client2.LastEnterSuccess();
    ok = ok && std::fabs(enter2.positionX - 300.0f) < 0.01f &&
         std::fabs(enter2.positionY - 300.0f) < 0.01f;
    Check("NewCharacterSpawnCheck: re-enter keeps spawn position", ok);
    client2.Disconnect();
    WaitUntil([&] { client2.DrainEvents(); return true; }, 300);
}

} // namespace

void RunVerticalSliceChecks() {
    WorldTestServers servers;
    servers.dbPath = TempDbPath("world_slice");
    RemoveDb(servers.dbPath);
    Check("SliceServersStartCheck", servers.StartLogin() && servers.StartWorld());
    RunNewCharacterSpawnChecks(servers);
    servers.StopAll();
}

} // namespace worldtest
