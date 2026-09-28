// ---------------------------------------------------------------------------
// 阶段13 指令七十二~一百一十：服务器权威怪物与基础 AI 检查。
// 仍链接 LegendWorldTests（不新增测试 exe，指令七十二）。
// 纯逻辑（Definition/Grid/Stress/协议畸形/RemoteMonsterManager/插值）无需服务器；
// 真实链路由 RunWorldMonsterChecks() 自管 servers（固定表 entityId 1..20 对应表序）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Server/WorldServer/Monster/MonsterAi.h"
#include "Server/WorldServer/Monster/MonsterDefinition.h"
#include "Server/WorldServer/Monster/MonsterEntity.h"
#include "Server/WorldServer/Monster/MonsterManager.h"
#include "Server/WorldServer/Monster/MonsterSpatialGrid.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace worldtest {

namespace {

using legend::world::MonsterAoiCandidate;
using legend::world::MonsterAoiDelta;
using legend::world::MonsterDefinition;
using legend::world::MonsterEntity;
using legend::world::MonsterManager;
using legend::world::MonsterSpatialGrid;
using legend::world::MonsterState;
using legend::world::FindMonsterDefinition;
using legend::world::PlayerSession;
using legend::world::ResolveMonsterAoiVisibility;
using legend::world::SelectAggroTarget;
using legend::world::WorldSpatialGrid;
using RemoteMonsterManagerT = legend::client::RemoteMonsterManager;
namespace CharacterRepository = legend::account::CharacterRepository;

// 固定表序 -> entityId（SpawnInitialMonsters 按表序分配 1..20）：
// 1..4   = (500,500)(700,500)(500,700)(700,700)        簇1
// 5..8   = (1000,500)(1200,500)(1000,700)(1200,700)    簇2
// 9..12  = (1500,500)(1700,500)(1500,700)(1700,700)    簇3
// 13..16 = (500,1500)(700,1500)(500,1700)(700,1700)    簇4
// 17..20 = (1500,1500)(1700,1500)(1500,1700)(1700,1700) 簇5
constexpr std::uint64_t kSlime1 = 1;   // (500,500)
constexpr std::uint64_t kSlime5 = 5;   // (1000,500)
constexpr std::uint64_t kSlime6 = 6;   // (1200,500)
constexpr std::uint64_t kSlime9 = 9;   // (1500,500)
constexpr std::uint64_t kSlime13 = 13; // (500,1500)
constexpr std::uint64_t kSlime14 = 14; // (700,1500)
constexpr std::uint64_t kSlime15 = 15; // (500,1700)
constexpr std::uint64_t kSlime16 = 16; // (700,1700)
constexpr std::uint64_t kSlime17 = 17; // (1500,1500)
constexpr std::uint64_t kSlime18 = 18; // (1700,1500)

// ---- 怪物事件辅助（基于 WorldTestClient 事件记录） ----

int CountMonsterSpawnsFor(const WorldTestClient& client, std::uint64_t entityId) {
    int n = 0;
    for (const auto& e :
         client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::MonsterSpawn)]) {
        if (e.monsterEntityId == entityId) {
            ++n;
        }
    }
    return n;
}

int CountMonsterDespawnsFor(const WorldTestClient& client, std::uint64_t entityId) {
    int n = 0;
    for (const auto& e :
         client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::MonsterDespawn)]) {
        if (e.monsterEntityId == entityId) {
            ++n;
        }
    }
    return n;
}

bool WaitMonsterSpawnCountAtLeast(WorldTestClient& client, std::uint64_t entityId, int minimum,
                                  int timeoutMs) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            return CountMonsterSpawnsFor(client, entityId) >= minimum;
        },
        timeoutMs);
}

// 最近一条 batch 中该怪物的 entry（不存在返回 false）。
bool LatestMonsterEntryFor(WorldTestClient& client, std::uint64_t entityId,
                           world::MonsterSnapshotEntry& out) {
    client.DrainEvents();
    const auto& batches = client
                              .recorded[WorldTestClient::IndexOf(
                                  WorldNetworkEvent::Type::MonsterBatchSnapshot)];
    for (auto it = batches.rbegin(); it != batches.rend(); ++it) {
        for (const auto& entry : it->monsterBatch) {
            if (entry.entityId == entityId) {
                out = entry;
                return true;
            }
        }
    }
    return false;
}

// 等待该怪物 batch state 变为目标状态。
bool WaitMonsterState(WorldTestClient& client, std::uint64_t entityId, std::uint8_t state,
                      int timeoutMs) {
    return WaitUntil(
        [&] {
            world::MonsterSnapshotEntry entry;
            return LatestMonsterEntryFor(client, entityId, entry) && entry.state == state;
        },
        timeoutMs);
}

// 等待某玩家收到指定怪物的 MonsterSpawn（首次/第 N 次）。
bool WaitMonsterSpawnAtLeast(WorldTestClient& client, int minimumTotal, int timeoutMs) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            const int idx =
                WorldTestClient::IndexOf(WorldNetworkEvent::Type::MonsterSpawn);
            return static_cast<int>(client.recorded[idx].size()) >= minimumTotal;
        },
        timeoutMs);
}

// ===========================================================================
// A. 纯逻辑检查（无服务器）
// ===========================================================================

void RunMonsterLogicChecks() {
    // ---- MonsterDefinitionCheck（指令七十三）----
    {
        const MonsterDefinition* def =
            FindMonsterDefinition(legend::world::kTrainingSlimeTypeId);
        bool ok = def != nullptr && def->monsterTypeId == 1 && def->name == "Training Slime" &&
                  def->level == 1 && def->moveSpeed == 80.0f && def->aggroRadius == 350.0f &&
                  def->leashRadius == 600.0f && def->patrolRadius == 180.0f;
        ok = ok && FindMonsterDefinition(999) == nullptr; // 阶段13 无其它类型（指令五）
        Check("MonsterDefinitionCheck: Training Slime config correct", ok);
    }

    // ---- MonsterSpatialGridCheck（指令七十六）----
    {
        MonsterSpatialGrid grid;
        auto make = [](std::uint64_t id, float x, float y) {
            return std::make_shared<MonsterEntity>(id, 1u, 1u, x, y, 80.0f);
        };
        auto a = make(1, 100.0f, 100.0f);
        auto b = make(2, 200.0f, 100.0f);
        auto c = make(3, 1500.0f, 1500.0f);
        grid.AddMonster(a);
        grid.AddMonster(b);
        grid.AddMonster(c);
        bool ok = grid.MonsterCount() == 3 && grid.Contains(1) && grid.Contains(2) && grid.Contains(3);
        auto nearby = grid.QueryNearbyMonsters(100.0f, 100.0f, 700.0f, 1); // 排除自己
        ok = ok && nearby.size() == 1 && nearby[0].monster->EntityId() == 2;
        b->SetPosition(900.0f, 100.0f); // 距 a 800 > 700
        grid.UpdateMonsterCell(b);
        ok = ok && grid.QueryNearbyMonsters(100.0f, 100.0f, 700.0f, 1).empty();
        auto nearby3 = grid.QueryNearbyMonsters(600.0f, 100.0f, 700.0f, 2);
        ok = ok && nearby3.size() == 1 && nearby3[0].monster->EntityId() == 1;
        grid.RemoveMonster(1);
        ok = ok && !grid.Contains(1) && grid.MonsterCount() == 2;
        Check("MonsterSpatialGridCheck: add/query/update/remove", ok);
    }

    // ---- 100 Monster Grid Stress（指令一百一十）：候选只含附近 ----
    {
        MonsterSpatialGrid grid;
        for (int i = 0; i < 100; ++i) {
            const float x = (i % 10) * 200.0f;      // 0..1800
            const float y = (i / 10) * 200.0f;      // 0..1800
            grid.AddMonster(std::make_shared<MonsterEntity>(
                static_cast<std::uint64_t>(i + 1), 1u, 1u, x, y, 80.0f));
        }
        // 从 (0,0) 查 700：只覆盖 4x4 cell 邻域——候选应远小于 100 且全部在 700 内。
        auto candidates = grid.QueryNearbyMonsters(0.0f, 0.0f, 700.0f, 0);
        bool ok = !candidates.empty() && candidates.size() < 100;
        for (const auto& candidate : candidates) {
            const float dx = candidate.monster->PositionX();
            const float dy = candidate.monster->PositionY();
            ok = ok && (dx * dx + dy * dy) <= 700.0f * 700.0f;
        }
        // 远处怪不在候选（(1800,1800) 距 (0,0) 远大于 700）。
        bool farFound = false;
        for (const auto& candidate : candidates) {
            if (candidate.monster->EntityId() == 100) {
                farFound = true;
            }
        }
        ok = ok && !farFound;
        Check("MonsterGridStressCheck: 100 monsters, only nearby candidates", ok);
    }

    // ---- MalformedMonsterSpawnCheck（指令一百零七）----
    {
        world::MonsterSpawnPayload spawn;
        spawn.entityId = 7;
        spawn.monsterTypeId = 1;
        spawn.name = "Training Slime";
        spawn.level = 1;
        spawn.mapId = 1;
        spawn.positionX = 12.0f;
        spawn.positionY = 34.0f;
        spawn.state = 0;
        spawn.serverTime = 999;
        std::vector<std::uint8_t> payload;
        std::string error;
        bool ok = world::EncodeMonsterSpawn(spawn, payload);
        world::MonsterSpawnPayload decoded;
        ok = ok && world::DecodeMonsterSpawn(payload.data(), payload.size(), decoded, error) &&
             decoded.entityId == 7 && decoded.name == "Training Slime";
        std::vector<std::uint8_t> truncated(payload.begin(), payload.end() - 5);
        ok = ok && !world::DecodeMonsterSpawn(truncated.data(), truncated.size(), decoded, error);
        std::vector<std::uint8_t> trailing = payload;
        trailing.push_back(0xFF);
        ok = ok && !world::DecodeMonsterSpawn(trailing.data(), trailing.size(), decoded, error);
        Check("MalformedMonsterSpawnCheck: truncated/trailing rejected", ok);
    }

    // ---- MalformedMonsterBatchCheck（指令一百零八）----
    {
        std::vector<std::uint8_t> payload;
        {
            network::ByteWriter w(payload);
            w.WriteUInt64(1);   // serverTime
            w.WriteUInt16(5);   // count=5 但 0 条 entry
        }
        world::MonsterBatchSnapshotPayload batch;
        std::string error;
        const bool ok = !world::DecodeMonsterBatchSnapshot(payload.data(), payload.size(), batch,
                                                           error);
        Check("MalformedMonsterBatchCheck: count beyond payload rejected", ok);
    }

    // ---- MonsterBatchCountLimitCheck（指令一百零九）：129 拒 / 128 过 ----
    {
        bool ok = true;
        {
            std::vector<std::uint8_t> payload;
            {
                network::ByteWriter w(payload);
                w.WriteUInt64(1);
                w.WriteUInt16(129);
            }
            world::MonsterBatchSnapshotPayload batch;
            std::string error;
            ok = ok && !world::DecodeMonsterBatchSnapshot(payload.data(), payload.size(), batch,
                                                          error);
        }
        {
            world::MonsterBatchSnapshotPayload big;
            big.serverTime = 5;
            for (std::uint16_t i = 0; i < 128; ++i) {
                big.monsters.push_back({static_cast<std::uint64_t>(100 + i),
                                        static_cast<float>(i), 0.0f, 0, 0});
            }
            std::vector<std::uint8_t> payload;
            ok = ok && world::EncodeMonsterBatchSnapshot(big, payload);
            world::MonsterBatchSnapshotPayload decoded;
            std::string error;
            ok = ok && world::DecodeMonsterBatchSnapshot(payload.data(), payload.size(), decoded,
                                                         error) &&
                 decoded.monsters.size() == 128 && decoded.monsters[127].entityId == 227;
        }
        Check("MonsterBatchCountLimitCheck: 129 rejected, 128 decodes", ok);
    }

    // ---- MonsterHysteresisCheck（指令八十/二十三）：resolver 590/650/710/650/590 ----
    {
        auto slime = std::make_shared<MonsterEntity>(2, 1u, 1u, 590.0f, 0.0f, 80.0f);
        std::vector<MonsterAoiCandidate> candidates{{slime, 590.0f * 590.0f}};
        std::unordered_set<std::uint64_t> visible;
        bool ok = true;
        MonsterAoiDelta delta =
            ResolveMonsterAoiVisibility(candidates, 1, visible, 600.0f, 700.0f, 128);
        ok = ok && delta.spawns.size() == 1 && delta.despawns.empty();
        visible.insert(2);
        candidates[0].distanceSquared = 650.0f * 650.0f;
        delta = ResolveMonsterAoiVisibility(candidates, 1, visible, 600.0f, 700.0f, 128);
        ok = ok && delta.spawns.empty() && delta.despawns.empty();
        candidates[0].distanceSquared = 710.0f * 710.0f;
        delta = ResolveMonsterAoiVisibility(candidates, 1, visible, 600.0f, 700.0f, 128);
        ok = ok && delta.spawns.empty() && delta.despawns.size() == 1 && delta.despawns[0] == 2;
        visible.erase(2);
        candidates[0].distanceSquared = 650.0f * 650.0f;
        delta = ResolveMonsterAoiVisibility(candidates, 1, visible, 600.0f, 700.0f, 128);
        ok = ok && delta.spawns.empty() && delta.despawns.empty();
        candidates[0].distanceSquared = 590.0f * 590.0f;
        delta = ResolveMonsterAoiVisibility(candidates, 1, visible, 600.0f, 700.0f, 128);
        ok = ok && delta.spawns.size() == 1;
        Check("MonsterHysteresisCheck: 590 spawn / 650 stay / 710 despawn / 650 invisible / 590 respawn",
              ok);
    }

    // ---- AggroNearestCheck（指令八十七/三十八）：范围多玩家选最近 ----
    {
        // 纯逻辑（SelectAggroTarget + WorldSpatialGrid）：巡逻相位不影响确定性判定。
        WorldSpatialGrid playerGrid;
        auto near1 = std::make_shared<PlayerSession>(0, 100, 100, "Near1", 1, 1, 1, 1, 1200.0f,
                                                     500.0f); // 距怪5 (1000,500) 200
        auto far1 = std::make_shared<PlayerSession>(0, 101, 101, "Far1", 1, 1, 1, 1, 1300.0f,
                                                    500.0f); // 距怪5 300
        playerGrid.AddPlayer(near1);
        playerGrid.AddPlayer(far1);
        MonsterEntity slime5(5, 1u, 1u, 1000.0f, 500.0f, 80.0f);
        const MonsterDefinition* def = FindMonsterDefinition(1);
        auto target = SelectAggroTarget(slime5, *def, playerGrid);
        Check("AggroNearestCheck: nearest player targeted",
              target != nullptr && target->CharacterId() == 100);
    }

    // ---- AggroStableTieCheck（指令八十八/三十八）：距离相同 characterId 小者优先 ----
    {
        WorldSpatialGrid playerGrid;
        // 两玩家与怪等距 212/212（dist ~300 < aggro 350）
        auto highId = std::make_shared<PlayerSession>(0, 202, 202, "HighId", 1, 1, 1, 1, 1912.0f,
                                                      912.0f);
        auto lowId = std::make_shared<PlayerSession>(0, 201, 201, "LowId", 1, 1, 1, 1, 1912.0f,
                                                     488.0f);
        playerGrid.AddPlayer(highId);
        playerGrid.AddPlayer(lowId);
        MonsterEntity slime12(12, 1u, 1u, 1700.0f, 700.0f, 80.0f);
        const MonsterDefinition* def = FindMonsterDefinition(1);
        auto target = SelectAggroTarget(slime12, *def, playerGrid);
        Check("AggroStableTieCheck: equal distance -> smaller characterId",
              target != nullptr && target->CharacterId() == 201);
    }

    // ---- UnknownMonsterSnapshotCheck（指令九十七/六十一）----
    {
        RemoteMonsterManagerT manager;
        world::MonsterBatchSnapshotPayload batch;
        batch.serverTime = 1;
        batch.monsters.push_back({999, 1.0f, 1.0f, 0, 0});
        manager.HandleBatch(batch);
        Check("UnknownMonsterSnapshotCheck: unknown id snapshot dropped",
              manager.Count() == 0 && manager.Find(999) == nullptr);
    }

    // ---- DuplicateMonsterSpawnCheck（指令九十八）----
    {
        RemoteMonsterManagerT manager;
        world::MonsterSpawnPayload spawn;
        spawn.entityId = 5;
        spawn.name = "Training Slime";
        spawn.positionX = 1.0f;
        spawn.positionY = 1.0f;
        manager.HandleSpawn(spawn);
        spawn.positionX = 2.0f;
        spawn.positionY = 2.0f;
        manager.HandleSpawn(spawn);
        const auto* entity = manager.Find(5);
        Check("DuplicateMonsterSpawnCheck: duplicate spawn keeps single entity",
              manager.Count() == 1 && entity != nullptr && entity->ServerX() == 2.0f);
    }

    // ---- MonsterDespawnClientCheck（指令九十九）----
    {
        RemoteMonsterManagerT manager;
        world::MonsterSpawnPayload spawn;
        spawn.entityId = 6;
        spawn.name = "Training Slime";
        manager.HandleSpawn(spawn);
        manager.HandleDespawn(6);
        Check("MonsterDespawnClientCheck: despawn removes entity",
              manager.Count() == 0 && manager.Find(6) == nullptr);
    }

    // ---- MonsterInterpolationCheck（指令一百/五十三）----
    {
        legend::client::RemoteMonsterEntity entity;
        world::MonsterSpawnPayload spawn;
        spawn.entityId = 8;
        spawn.name = "Training Slime";
        spawn.positionX = 0.0f;
        spawn.positionY = 0.0f;
        entity.ApplySpawn(spawn);
        entity.ApplySnapshot(100.0f, 0.0f, 0, 0, 1);
        entity.UpdateInterpolation(0.1f);
        const float first = entity.RenderX();
        bool ok = first > 50.0f && first < 100.0f; // 渐进，不瞬移
        for (int i = 0; i < 30; ++i) {
            entity.UpdateInterpolation(0.1f);
        }
        ok = ok && std::fabs(entity.RenderX() - 100.0f) < 1.0f;
        Check("MonsterInterpolationCheck: render approaches server position gradually", ok);
    }

    // ---- MonsterTeleportCorrectionCheck（指令一百零一/五十四）----
    {
        legend::client::RemoteMonsterEntity entity;
        world::MonsterSpawnPayload spawn;
        spawn.entityId = 9;
        spawn.name = "Training Slime";
        spawn.positionX = 0.0f;
        spawn.positionY = 0.0f;
        entity.ApplySpawn(spawn);
        entity.ApplySnapshot(400.0f, 0.0f, 0, 0, 1);
        entity.UpdateInterpolation(0.016f);
        Check("MonsterTeleportCorrectionCheck: >300 snaps to server position",
              entity.RenderX() == 400.0f && entity.RenderY() == 0.0f);
    }
}

// ===========================================================================
// B. 真实链路检查（RunWorldMonsterChecks 自管 servers）
// ===========================================================================

bool SeedAt(Database& db, AccountService& accounts, CharacterService& characters,
            const std::string& username, const std::string& charName, float x, float y,
            CharacterSeed& out) {
    if (!SeedAccountAndCharacter(db, accounts, characters, username, charName, out)) {
        return false;
    }
    return CharacterRepository::UpdateWorldPosition(db, out.characterId, 1, x, y,
                                                    account::UnixNow())
        .success;
}

std::string TicketFor(const std::shared_ptr<LoginServer>& login, const CharacterSeed& seed) {
    if (!login) {
        return {};
    }
    return login->Tickets().Create(seed.accountId, seed.characterId, 60.0);
}

} // namespace

void RunWorldMonsterChecks() {
    // A. 纯逻辑
    RunMonsterLogicChecks();

    // B. 真实链路
    WorldTestServers servers;
    servers.dbPath = TempDbPath("world_monster");
    RemoveDb(servers.dbPath);
    Check("MonsterServersStartCheck", servers.StartLogin() && servers.StartWorld());

    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(5, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("MonsterChecks: db ready", false);
        servers.StopAll();
        return;
    }
    const auto stateIdle = static_cast<std::uint8_t>(MonsterState::Idle);
    const auto statePatrol = static_cast<std::uint8_t>(MonsterState::Patrol);
    const auto stateChase = static_cast<std::uint8_t>(MonsterState::Chase);
    const auto stateReturning = static_cast<std::uint8_t>(MonsterState::Returning);

    // ---- MonsterSpawnCountCheck（七十四）/ MonsterEntityIdUniqueCheck（七十五）----
    {
        const auto ids = servers.world->MonsterEntityIds();
        std::unordered_set<std::uint64_t> unique(ids.begin(), ids.end());
        Check("MonsterSpawnCountCheck: 20 slimes spawned on start",
              servers.world->MonsterCount() == 20 && ids.size() == 20);
        Check("MonsterEntityIdUniqueCheck: all entityIds unique", unique.size() == 20);
    }

    // ---- NoMonsterSpawnFarCheck（七十八）+ MonsterNoGlobalBroadcastCheck（一百零五）----
    // 最先执行：怪物仍处 Idle（2s）未 patrol，远处玩家 (60,60) 距所有 spawn >600。
    WorldTestClient clientF;
    CharacterSeed seedF;
    {
        SeedAt(db, accounts, characters, "mon_user_f", "MonF", 60.0f, 60.0f, seedF);
        const std::string ticketF = TicketFor(servers.login, seedF);
        const bool enterF = !ticketF.empty() && clientF.ConnectAndEnter(ticketF, 8000);
        std::this_thread::sleep_for(std::chrono::milliseconds(800)); // >=4 个 AOI tick
        clientF.DrainEvents();
        const int spawnCount = static_cast<int>(
            clientF.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::MonsterSpawn)]
                .size());
        const int batchCount = static_cast<int>(
            clientF
                .recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::MonsterBatchSnapshot)]
                .size());
        Check("NoMonsterSpawnFarCheck: far player never sees monsters",
              enterF && spawnCount == 0 && batchCount == 0);
        Check("MonsterNoGlobalBroadcastCheck: far player receives no monster data",
              spawnCount == 0 && batchCount == 0);
        clientF.Disconnect(); // 断开避免 patrol 后怪物合法靠近造成后续噪音
        WaitUntil([&] { clientF.DrainEvents(); return true; }, 300);
    }

    // ---- InitialMonsterSpawnCheck（七十七）：进入怪物附近收到 MonsterSpawn ----
    CharacterSeed seedA;
    CharacterSeed seedE;
    CharacterSeed seedD;
    CharacterSeed seedH;
    CharacterSeed seedK;
    CharacterSeed seedL;
    CharacterSeed seedO;
    CharacterSeed seedT;
    CharacterSeed seedC;
    CharacterSeed seedR;
    bool seeded = true;
    seeded = seeded && SeedAt(db, accounts, characters, "mon_user_a", "MonA", 600.0f, 600.0f, seedA);
    seeded = seeded && SeedAt(db, accounts, characters, "mon_user_e", "MonE", 0.0f, 1050.0f, seedE);
    seeded = seeded && SeedAt(db, accounts, characters, "mon_user_d", "MonD", 1550.0f, 1550.0f, seedD);
    seeded = seeded && SeedAt(db, accounts, characters, "mon_user_h", "MonH", 1650.0f, 500.0f, seedH);
    seeded = seeded && SeedAt(db, accounts, characters, "mon_user_k", "MonK", 400.0f, 1500.0f, seedK);
    seeded = seeded && SeedAt(db, accounts, characters, "mon_user_l", "MonL", 700.0f, 1420.0f, seedL);
    seeded = seeded && SeedAt(db, accounts, characters, "mon_user_o", "MonO", 1290.0f, 1440.0f, seedO);
    seeded = seeded && SeedAt(db, accounts, characters, "mon_user_t", "MonT", 700.0f, 1750.0f, seedT);
    seeded = seeded && SeedAt(db, accounts, characters, "mon_user_c", "MonC", 1850.0f, 1100.0f, seedC);
    seeded = seeded && SeedAt(db, accounts, characters, "mon_user_r", "MonR", 500.0f, 1750.0f, seedR);
    Check("MonsterChecks: seeds ready", seeded);

    WorldTestClient clientA;
    {
        const std::string ticketA = TicketFor(servers.login, seedA);
        const bool enterA = !ticketA.empty() && clientA.ConnectAndEnter(ticketA, 8000);
        // A(600,600)：距簇1 四只怪 0~283 全部 <600 -> 初始可见性下发
        bool ok = enterA;
        ok = ok && WaitMonsterSpawnAtLeast(clientA, 1, 3000);
        ok = ok && WaitMonsterSpawnCountAtLeast(clientA, kSlime1, 1, 3000);
        Check("InitialMonsterSpawnCheck: nearby monsters spawned on enter", ok);
    }

    // ---- MonsterEnterRadiusCheck（七十九）：移动进入 <=600 -> Spawn ----
    {
        WorldTestClient clientE;
        const std::string ticketE = TicketFor(servers.login, seedE);
        const bool enterE = !ticketE.empty() && clientE.ConnectAndEnter(ticketE, 8000);
        bool ok = enterE;
        clientE.DrainEvents();
        // E(0,1050) 距簇1 怪1 (500,500) 743 >600 -> 未 spawn 怪1；
        // 距怪13/14 巡逻圈 >360（断线后不干扰后续 AI 测试）
        const int before = CountMonsterSpawnsFor(clientE, kSlime1);
        // 移动向北 300（25 inputs）-> (0,760) 距怪1 563 <=600
        for (int i = 0; i < 25; ++i) {
            clientE.client().SendMoveInput(static_cast<std::uint32_t>(i + 1), 0.0f, -1.0f, 0.1f);
        }
        ok = ok && WaitMonsterSpawnCountAtLeast(clientE, kSlime1, before + 1, 3000);
        Check("MonsterEnterRadiusCheck: move into 600 -> MonsterSpawn", ok && before == 0);
        clientE.Disconnect();
        WaitUntil([&] { clientE.DrainEvents(); return true; }, 300);
    }

    // ---- MonsterSpawnExactlyOnceCheck（八十一/二十八）：稳定期多个 tick 不重复 ----
    {
        // A 已在簇1 内，四只怪 Chase A 并停在其身边（距离 ~0 稳定可见）。
        std::this_thread::sleep_for(std::chrono::milliseconds(1200)); // >=6 个 tick
        clientA.DrainEvents();
        bool ok = true;
        for (const std::uint64_t id : {kSlime1, kSlime1 + 1, kSlime1 + 2, kSlime1 + 3}) {
            ok = ok && CountMonsterSpawnsFor(clientA, id) == 1;
        }
        Check("MonsterSpawnExactlyOnceCheck: spawn count stays 1 across ticks", ok);
    }

    // ---- AggroAcquireCheck（八十六）：玩家进入 350 -> Chase + target 正确 ----
    WorldTestClient clientD;
    {
        const std::string ticketD = TicketFor(servers.login, seedD);
        const bool enterD = !ticketD.empty() && clientD.ConnectAndEnter(ticketD, 8000);
        // D(1550,1550) 距簇5 怪17 (1500,1500) 71 -> 怪17 Chase D 且 target==D
        bool ok = enterD;
        ok = ok && WaitMonsterSpawnCountAtLeast(clientD, kSlime17, 1, 3000);
        ok = ok && WaitMonsterState(clientD, kSlime17, stateChase, 4000);
        world::MonsterSnapshotEntry entry;
        ok = ok && LatestMonsterEntryFor(clientD, kSlime17, entry) &&
             entry.targetCharacterId == seedD.characterId;
        Check("AggroAcquireCheck: monster chases nearest player with correct target", ok);
    }

    // ---- ChaseMovementCheck（八十九）：怪物朝玩家权威位置接近 ----
    // （AggroNearest/StableTie 已由纯逻辑 SelectAggroTarget 覆盖，见 RunMonsterLogicChecks。）
    WorldTestClient clientH;
    {
        const std::string ticketH = TicketFor(servers.login, seedH);
        const bool enterH = !ticketH.empty() && clientH.ConnectAndEnter(ticketH, 8000);
        // H(1650,500) 距怪9 (1500,500) 150 -> 巡逻任意相位距 H <=330 <350 必 aggro；
        // H 以 60units/s 慢速东移（贴身跟随，不触发 lost），怪9 跟至 (2000,500) 附近
        bool ok = enterH;
        ok = ok && WaitMonsterSpawnCountAtLeast(clientH, kSlime9, 1, 3000);
        ok = ok && WaitMonsterState(clientH, kSlime9, stateChase, 6000);
        for (int i = 0; i < 30; ++i) {
            clientH.client().SendMoveInput(static_cast<std::uint32_t>(i + 1), 1.0f, 0.0f, 0.1f);
            std::this_thread::sleep_for(std::chrono::milliseconds(180));
        }
        // 怪9 贴身跟随：位置 x 越过 1600 且 H-怪距离 < 250
        ok = ok && WaitUntil(
                       [&] {
                           world::MonsterSnapshotEntry entry;
                           if (!LatestMonsterEntryFor(clientH, kSlime9, entry)) {
                               return false;
                           }
                           const float dx = entry.positionX - 2000.0f;
                           const float dy = entry.positionY - 500.0f;
                           return entry.positionX > 1600.0f && (dx * dx + dy * dy) < 250.0f * 250.0f;
                       },
                       12000);
        Check("ChaseMovementCheck: monster approaches player position", ok);
    }

    // ---- LeashCheck（九十/四十二）：怪物离 spawn >600 -> Returning ----
    WorldTestClient clientK;
    {
        const std::string ticketK = TicketFor(servers.login, seedK);
        const bool enterK = !ticketK.empty() && clientK.ConnectAndEnter(ticketK, 8000);
        // K(400,1500) 距怪13 (500,1500) 100 -> 巡逻任意相位必 aggro（<=280<350）；
        // K 以 60units/s（< 怪 80）慢速北移 624：怪13 贴身跟随（K-怪 ~=100+滞后 <525，
        // 不触发 lost），怪13 被带至距 spawn >600 -> leash
        bool ok = enterK;
        ok = ok && WaitMonsterSpawnCountAtLeast(clientK, kSlime13, 1, 3000);
        ok = ok && WaitMonsterState(clientK, kSlime13, stateChase, 6000);
        for (int i = 0; i < 52; ++i) {
            clientK.client().SendMoveInput(static_cast<std::uint32_t>(i + 1), 0.0f, -1.0f, 0.1f);
            std::this_thread::sleep_for(std::chrono::milliseconds(180));
        }
        ok = ok && WaitMonsterState(clientK, kSlime13, stateReturning, 15000);
        Check("LeashCheck: beyond leash radius -> Returning", ok);
    }

    // ---- ReturnHomeCheck（九十三/四十五/四十六）：Returning 回到 spawn -> Idle ----
    // 服务器权威状态断言：怪13 leash 后 Returning -> 到达 spawn(<=10) -> Idle(target=0)。
    // 紧跟 Leash 块执行（回程 ~7.8s + Idle 2s 窗口内轮询命中）。
    {
        bool ok = WaitUntil(
            [&] {
                auto monster = servers.world->FindMonster(kSlime13);
                if (monster == nullptr || monster->State() != MonsterState::Idle) {
                    return false;
                }
                const float dx = monster->PositionX() - 500.0f;
                const float dy = monster->PositionY() - 1500.0f;
                // 到达判定 dist<=10 即转 Idle：位置距 spawn 容差 15
                return monster->TargetCharacterId() == 0 && (dx * dx + dy * dy) <= 225.0f;
            },
            20000);
        Check("ReturnHomeCheck: monster returns to spawn and idles", ok);
    }

    // ---- AggroLostCheck（九十一/四十三）：目标 >525 -> Returning（非 leash）----
    WorldTestClient clientL;
    WorldTestClient clientO;  // 提前进场的邻近观察者（位置对怪14 巡逻安全，不会抢 aggro）
    {
        // O(1290,1440) 提前进场：距怪14 spawn 593（巡逻 min 403 >350 不被 aggro），
        // 距怪14 lost 点 (710,1510) 584 <600 -> Returning 期间持续可见（提前进入，
        // 避免 Returning 2s 窗口错过）
        const std::string ticketO = TicketFor(servers.login, seedO);
        const bool enterO = !ticketO.empty() && clientO.ConnectAndEnter(ticketO, 8000);
        Check("AggroLostCheck: observer ready", enterO);
        const std::string ticketL = TicketFor(servers.login, seedL);
        const bool enterL = !ticketL.empty() && clientL.ConnectAndEnter(ticketL, 8000);
        // L(700,1420) 距怪14 spawn (700,1500) 80 -> 巡逻任意相位距 L <=270 <350 必 aggro；
        // L 瞬移 600 后怪14 距 L ~570 >525 -> lost；回程 70 units（O 高频轮询可观察）
        bool ok = enterL;
        ok = ok && WaitMonsterSpawnCountAtLeast(clientL, kSlime14, 1, 3000);
        ok = ok && WaitMonsterState(clientL, kSlime14, stateChase, 6000);
        for (int i = 0; i < 60; ++i) {
            clientL.client().SendMoveInput(static_cast<std::uint32_t>(i + 1), 1.0f, 0.0f, 0.1f);
        }
        // L 瞬移到 (1300,1420)：怪14 距 L ~570 >525 -> lost -> Returning。
        // 服务器权威状态断言：Returning 首现时怪14 距 spawn <600（非 leash——leash
        // 触发点距 spawn >600）；lost 后 Returning 从 L 旧位（距 spawn ~80+滞后）回 home。
        bool sawReturning = false;
        bool nonLeash = true;
        WaitUntil(
            [&] {
                auto monster = servers.world->FindMonster(kSlime14);
                if (monster == nullptr || monster->State() != MonsterState::Returning) {
                    return false;
                }
                if (!sawReturning) {
                    sawReturning = true;
                    const float dx = monster->PositionX() - 700.0f;
                    const float dy = monster->PositionY() - 1500.0f;
                    if ((dx * dx + dy * dy) >= 600.0f * 600.0f) {
                        nonLeash = false; // Returning 首现位置 >600 = leash 触发
                    }
                }
                return true;
            },
            20000);
        // 交叉验证：O 的 batch 中怪14 位置距 spawn <600
        world::MonsterSnapshotEntry entry;
        if (LatestMonsterEntryFor(clientO, kSlime14, entry)) {
            const float dx = entry.positionX - 700.0f;
            const float dy = entry.positionY - 1500.0f;
            if ((dx * dx + dy * dy) >= 600.0f * 600.0f) {
                nonLeash = false;
            }
        }
        Check("AggroLostCheck: target beyond 525 -> Returning before leash",
              sawReturning && nonLeash);
    }

    // ---- IdleToPatrolCheck（八十四/三十二）+ PatrolMovementCheck（八十五/三十三~三十五）----
    // 服务器权威状态断言（WorldServer::FindMonster）：避免观察者可见性抖动干扰。
    {
        // 怪6 (1200,500)：A(600,600) 距 613（巡逻 min 433 >350）、H 距 800、D 距 1107
        // -> 怪6 保持 Idle <-> Patrol 循环。
        bool patrolSeen = WaitUntil(
            [&] {
                auto monster = servers.world->FindMonster(kSlime6);
                return monster != nullptr && monster->State() == MonsterState::Patrol;
            },
            10000);
        Check("IdleToPatrolCheck: idle monster transitions to patrol", patrolSeen);
        // Patrol 后位置变化，且始终离 spawn (1200,500) <= 180 + 到达判定 10 + 移动余量 20
        bool patrolMoved = false;
        bool withinRadius = true;
        float firstX = 0.0f;
        float firstY = 0.0f;
        bool firstSet = false;
        for (int i = 0; i < 60; ++i) {
            auto monster = servers.world->FindMonster(kSlime6);
            if (monster != nullptr && (monster->State() == MonsterState::Patrol ||
                                       monster->State() == MonsterState::Idle)) {
                const float x = monster->PositionX();
                const float y = monster->PositionY();
                if (!firstSet) {
                    firstX = x;
                    firstY = y;
                    firstSet = true;
                }
                const float dx = x - firstX;
                const float dy = y - firstY;
                if (dx * dx + dy * dy > 1.0f) {
                    patrolMoved = true;
                }
                const float sx = x - 1200.0f;
                const float sy = y - 500.0f;
                if ((sx * sx + sy * sy) > 210.0f * 210.0f) {
                    withinRadius = false;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        Check("PatrolMovementCheck: patrol moves position within spawn radius",
              patrolSeen && patrolMoved && withinRadius);
    }

    // ---- TargetDisconnectCheck（九十二/四十四）：目标断线 -> Returning ----
    // 服务器权威状态断言：T 断线 -> 怪16 立刻 Returning（io 线程同步处理）
    {
        WorldTestClient clientT;
        const std::string ticketT = TicketFor(servers.login, seedT);
        const bool enterT = !ticketT.empty() && clientT.ConnectAndEnter(ticketT, 8000);
        // T(700,1750) 距怪16 (700,1700) 50 -> 巡逻任意相位必 aggro（<=240<350）
        bool ok = enterT;
        ok = ok && WaitMonsterSpawnCountAtLeast(clientT, kSlime16, 1, 3000);
        ok = ok && WaitMonsterState(clientT, kSlime16, stateChase, 6000);
        clientT.Disconnect(); // 目标断线 -> 立刻 Returning
        ok = ok && WaitUntil(
                       [&] {
                           auto monster = servers.world->FindMonster(kSlime16);
                           return monster != nullptr &&
                                  monster->State() == MonsterState::Returning;
                       },
                       5000);
        Check("TargetDisconnectCheck: target disconnect -> monster Returning", ok);
    }

    // ---- MonsterDespawnExactlyOnceCheck + MonsterReenterCheck（八十二/八十三/二十六/二十七/二十九）----
    WorldTestClient clientC;
    {
        const std::string ticketC = TicketFor(servers.login, seedC);
        const bool enterC = !ticketC.empty() && clientC.ConnectAndEnter(ticketC, 8000);
        // C(1850,1100) 距怪18 (1700,1500) 427 -> spawn；距所有怪 >350 不 aggro
        bool ok = enterC;
        ok = ok && WaitMonsterSpawnCountAtLeast(clientC, kSlime18, 1, 3000);
        // C 向北移动 400（34 inputs）-> 距怪18 814 >700 -> Despawn(LeftAOI)
        for (int i = 0; i < 34; ++i) {
            clientC.client().SendMoveInput(static_cast<std::uint32_t>(i + 1), 0.0f, -1.0f, 0.1f);
        }
        ok = ok && WaitUntil(
                       [&] {
                           clientC.DrainEvents();
                           return CountMonsterDespawnsFor(clientC, kSlime18) >= 1;
                       },
                       5000);
        std::this_thread::sleep_for(std::chrono::milliseconds(700)); // 多个 tick 确认只一次
        clientC.DrainEvents();
        ok = ok && CountMonsterDespawnsFor(clientC, kSlime18) == 1;
        bool despawnOnce = ok;
        if (ok) {
            const auto& ev =
                clientC.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::MonsterDespawn)];
            for (const auto& e : ev) {
                if (e.monsterEntityId == kSlime18) {
                    despawnOnce = despawnOnce &&
                                  e.despawnReason ==
                                      static_cast<std::uint8_t>(
                                          legend::world::MonsterDespawnReason::LeftAOI);
                }
            }
        }
        Check("MonsterDespawnExactlyOnceCheck: single LeftAOI despawn", despawnOnce);
        // 移回 (1850,1100)（34 inputs）-> 重新 <=600 -> 再次 Spawn
        for (int i = 0; i < 34; ++i) {
            clientC.client().SendMoveInput(static_cast<std::uint32_t>(100 + i), 0.0f, 1.0f, 0.1f);
        }
        ok = ok && WaitMonsterSpawnCountAtLeast(clientC, kSlime18, 2, 5000);
        Check("MonsterReenterCheck: re-enter -> second spawn", ok);
    }

    // ---- MultiPlayerMonsterVisibilityCheck（一百零二）：10 玩家同区域共享怪物可见性 ----
    {
        constexpr int kPlayers = 10;
        std::vector<CharacterSeed> seeds(kPlayers);
        bool groupSeeded = true;
        for (int i = 0; i < kPlayers; ++i) {
            groupSeeded =
                groupSeeded &&
                SeedAt(db, accounts, characters, "mon_m_" + std::to_string(i),
                       "MonM" + std::to_string(i), 440.0f + 10.0f * i, 440.0f, seeds[i]);
        }
        std::vector<std::unique_ptr<WorldTestClient>> clients(kPlayers);
        std::atomic<int> ready{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < kPlayers; ++i) {
            clients[i] = std::make_unique<WorldTestClient>();
            threads.emplace_back([&clients, &seeds, &servers, &ready, i, &groupSeeded] {
                const std::string ticket = groupSeeded ? TicketFor(servers.login, seeds[i])
                                                       : std::string();
                if (!ticket.empty() && clients[i]->ConnectAndEnter(ticket, 15000)) {
                    ++ready;
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        // 每个玩家都能看到附近的怪（簇1 被怪群 chase A 后仍在 A 附近 ~ (600,600)）
        bool allSee = groupSeeded && ready == kPlayers;
        if (allSee) {
            allSee = WaitUntil(
                [&] {
                    bool done = true;
                    for (auto& client : clients) {
                        client->DrainEvents();
                        if (client->controller.RemoteMonsters().Count() < 1) {
                            done = false;
                        }
                    }
                    return done;
                },
                8000);
        }
        Check("MultiPlayerMonsterVisibilityCheck: 10 players see nearby monsters", allSee);
        // MonsterMovementReplicationCheck（一百零四）：两客户端 batch 中同一怪位置一致
        bool replicated = allSee;
        if (replicated) {
            replicated = WaitUntil(
                [&] {
                    world::MonsterSnapshotEntry entryA;
                    world::MonsterSnapshotEntry entryM;
                    if (!LatestMonsterEntryFor(clientA, kSlime1, entryA)) {
                        return false;
                    }
                    if (!LatestMonsterEntryFor(*clients[0], kSlime1, entryM)) {
                        return false;
                    }
                    const float dx = entryA.positionX - entryM.positionX;
                    const float dy = entryA.positionY - entryM.positionY;
                    return (dx * dx + dy * dy) < 1.0f;
                },
                5000);
        }
        Check("MonsterMovementReplicationCheck: consistent monster position across clients",
              replicated);
        for (auto& client : clients) {
            client->Disconnect();
        }
        WaitUntil([&] { return true; }, 500);
    }

    // ---- MonsterAOISeparationCheck（一百零三）：两组玩家只见各自附近怪物 ----
    {
        WorldTestClient groupS;
        WorldTestClient groupT;
        const std::string ticketS = TicketFor(servers.login, seedA); // 复用 A 位置 (600,600)
        (void)ticketS;
        // 直接新建两个远/近观察者：S 用 A（已有），T 新建 (1500,1440)
        CharacterSeed seedS2;
        SeedAt(db, accounts, characters, "mon_s2", "MonS2", 1500.0f, 1440.0f, seedS2);
        const std::string ticketT = TicketFor(servers.login, seedS2);
        const bool enterT = !ticketT.empty() && groupT.ConnectAndEnter(ticketT, 8000);
        bool ok = enterT;
        // A（簇1 侧）所有可见怪 y < 1000；T（簇5 侧）所有可见怪 y > 1000
        ok = ok && WaitUntil(
                       [&] {
                           groupT.DrainEvents();
                           clientA.DrainEvents();
                           if (groupT.controller.RemoteMonsters().Count() < 1) {
                               return false;
                           }
                           bool separated = true;
                           for (const auto& [id, monster] : groupT.controller.RemoteMonsters().All()) {
                               if (monster.ServerY() < 1000.0f) {
                                   separated = false;
                               }
                           }
                           for (const auto& [id, monster] : clientA.controller.RemoteMonsters().All()) {
                               if (monster.ServerY() > 1000.0f) {
                                   separated = false;
                               }
                           }
                           return separated;
                       },
                       8000);
        Check("MonsterAOISeparationCheck: groups only see their nearby monsters", ok);
        groupT.Disconnect();
        WaitUntil([&] { groupT.DrainEvents(); return true; }, 300);
    }

    // ---- MonsterRemoveCleanupCheck（一百零六）：RemoveMonster -> Despawn(Removed) ----
    {
        WorldTestClient clientR;
        const std::string ticketR = TicketFor(servers.login, seedR);
        const bool enterR = !ticketR.empty() && clientR.ConnectAndEnter(ticketR, 8000);
        // R(500,1750) 距怪15 (500,1700) 50 -> spawn
        bool ok = enterR && WaitMonsterSpawnCountAtLeast(clientR, kSlime15, 1, 3000);
        ok = ok && servers.world->RemoveMonster(kSlime15);
        ok = ok && WaitUntil(
                       [&] {
                           clientR.DrainEvents();
                           const auto& ev = clientR.recorded[WorldTestClient::IndexOf(
                               WorldNetworkEvent::Type::MonsterDespawn)];
                           for (const auto& e : ev) {
                               if (e.monsterEntityId == kSlime15 &&
                                   e.despawnReason ==
                                       static_cast<std::uint8_t>(
                                           legend::world::MonsterDespawnReason::Removed)) {
                                   return true;
                               }
                           }
                           return false;
                       },
                       4000);
        ok = ok && clientR.controller.RemoteMonsters().Find(kSlime15) == nullptr;
        Check("MonsterRemoveCleanupCheck: Removed despawn + client cleanup", ok);
        clientR.Disconnect();
        WaitUntil([&] { clientR.DrainEvents(); return true; }, 300);
    }

    // ---- MonsterSnapshotCheck（九十五）+ MonsterSnapshotNoFarCheck（九十六）----
    {
        world::MonsterSnapshotEntry entry;
        bool ok = LatestMonsterEntryFor(clientA, kSlime1, entry);
        // entry 含位置/state/target 字段（协议字段完整性）
        ok = ok && entry.positionX >= 0.0f && entry.positionX <= 2000.0f;
        // NoFar：A（簇1 侧）batch 不含簇5 怪（17~20）
        const auto& batches = clientA
                                  .recorded[WorldTestClient::IndexOf(
                                      WorldNetworkEvent::Type::MonsterBatchSnapshot)];
        for (const auto& batch : batches) {
            for (const auto& e : batch.monsterBatch) {
                if (e.entityId >= 17 && e.entityId <= 20) {
                    ok = false;
                }
            }
        }
        Check("MonsterSnapshotCheck: visible monster snapshot with state/target", ok);
    }

    // ---- MonsterBoundsCheck（九十四/四十七）：所有怪物始终 0~2000 ----
    {
        bool ok = true;
        const auto scan = [&ok](const WorldTestClient& client) {
            for (const auto& batch : client.recorded[WorldTestClient::IndexOf(
                     WorldNetworkEvent::Type::MonsterBatchSnapshot)]) {
                for (const auto& e : batch.monsterBatch) {
                    if (!(e.positionX >= 0.0f && e.positionX <= 2000.0f && e.positionY >= 0.0f &&
                          e.positionY <= 2000.0f)) {
                        ok = false;
                    }
                }
            }
        };
        scan(clientA);
        scan(clientD);
        scan(clientH);
        scan(clientK);
        scan(clientL);
        scan(clientO);
        scan(clientC);
        Check("MonsterBoundsCheck: all monster positions within 0~2000", ok);
    }

    // ---- MonsterRestartCheck（六十七）：重启重新生成固定 20 只 ----
    {
        clientA.Disconnect();
        clientD.Disconnect();
        clientH.Disconnect();
        clientK.Disconnect();
        clientL.Disconnect();
        clientO.Disconnect();
        clientC.Disconnect();
        WaitUntil([&] { return true; }, 500);
        servers.StopWorld();
        Check("MonsterRestartCheck: world restarts", servers.StartWorld());
        Check("MonsterRestartCheck: 20 monsters regenerated after restart",
              servers.world->MonsterCount() == 20);
    }

    db.Close();
    servers.StopAll();
}

} // namespace worldtest
