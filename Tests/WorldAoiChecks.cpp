// ---------------------------------------------------------------------------
// 阶段12 指令七十四~一百零四：AOI & Multiplayer Replication 检查。
// 仍链接 LegendWorldTests（不新增第四个测试 exe，指令七十四）。
// 纯逻辑检查（SpatialGrid/resolver/RemotePlayerManager/插值/畸形协议）无需服务器；
// 真实链路检查由 RunWorldAoiChecks() 自管 servers 生命周期（与阶段11链路串行复用端口）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Server/WorldServer/AOI/WorldSpatialGrid.h"

#include <algorithm>
#include <unordered_set>

namespace worldtest {

namespace {

using legend::world::AoiCandidate;
using legend::world::AoiDelta;
using legend::world::PlayerSession;
using legend::world::ResolveAoiVisibility;
using legend::world::WorldSpatialGrid;
using RemotePlayerManagerT = legend::client::RemotePlayerManager;
namespace CharacterRepository = legend::account::CharacterRepository;

// 离线构造 PlayerSession（纯逻辑测试用，不进服务器）。
std::shared_ptr<PlayerSession> MakePlayer(std::uint64_t characterId, std::uint16_t mapId, float x,
                                          float y) {
    return std::make_shared<PlayerSession>(0, characterId, characterId,
                                           "P" + std::to_string(characterId), 1, 1, 1, mapId, x,
                                           y);
}

// ===========================================================================
// A. 纯逻辑检查（无服务器）
// ===========================================================================

void RunAoiLogicChecks() {
    // ---- SpatialGridCheck（指令七十五）：Add/Query/UpdateCell/Remove ----
    {
        WorldSpatialGrid grid;
        auto a = MakePlayer(1, 1, 100.0f, 100.0f);
        auto b = MakePlayer(2, 1, 200.0f, 100.0f);
        auto c = MakePlayer(3, 1, 1500.0f, 1500.0f);
        grid.AddPlayer(a);
        grid.AddPlayer(b);
        grid.AddPlayer(c);
        bool ok = grid.PlayerCount() == 3 && grid.Contains(1) && grid.Contains(2) && grid.Contains(3);
        auto nearby = grid.QueryNearbyPlayers(100.0f, 100.0f, 700.0f, 1); // 排除自己（near 是 Windows 宏，禁用）
        ok = ok && nearby.size() == 1 && nearby[0].player->CharacterId() == 2;
        // UpdatePlayerCell：b 移动 -> cell 变化 -> 新位置可查、旧位置消失
        b->SetPosition(900.0f, 100.0f); // 距 a 800 > 700 查询半径
        grid.UpdatePlayerCell(b);
        ok = ok && grid.QueryNearbyPlayers(100.0f, 100.0f, 700.0f, 1).empty();
        auto nearby3 = grid.QueryNearbyPlayers(600.0f, 100.0f, 700.0f, 2);
        ok = ok && nearby3.size() == 1 && nearby3[0].player->CharacterId() == 1;
        // Remove
        grid.RemovePlayer(1);
        ok = ok && !grid.Contains(1) && grid.PlayerCount() == 2;
        Check("SpatialGridCheck: add/query/updateCell/remove", ok);
    }

    // ---- NearbyCandidateCheck（指令七十六）：跨 cell 边界仍可查 ----
    {
        WorldSpatialGrid grid;
        auto a = MakePlayer(1, 1, 399.0f, 399.0f); // cell(0,0)
        auto b = MakePlayer(2, 1, 401.0f, 401.0f); // cell(1,1)
        grid.AddPlayer(a);
        grid.AddPlayer(b);
        auto nearby = grid.QueryNearbyPlayers(399.0f, 399.0f, 700.0f, 1);
        Check("NearbyCandidateCheck: cross-cell neighbor found",
              nearby.size() == 1 && nearby[0].player->CharacterId() == 2);
    }

    // ---- FarCandidateCheck（指令七十七）：远处玩家不在候选 ----
    {
        WorldSpatialGrid grid;
        auto a = MakePlayer(1, 1, 100.0f, 100.0f);
        auto c = MakePlayer(3, 1, 1200.0f, 100.0f); // 距离 1100 > 700
        grid.AddPlayer(a);
        grid.AddPlayer(c);
        auto nearby = grid.QueryNearbyPlayers(100.0f, 100.0f, 700.0f, 1);
        Check("FarCandidateCheck: far player not in candidates", nearby.empty());
    }

    // ---- MalformedSpawnProtocolCheck（指令一百零二）----
    {
        world::PlayerSpawnPayload spawn;
        spawn.characterId = 7;
        spawn.name = "SpawnHero";
        spawn.classId = 1;
        spawn.gender = 2;
        spawn.level = 3;
        spawn.mapId = 1;
        spawn.positionX = 12.0f;
        spawn.positionY = 34.0f;
        spawn.serverTime = 999;
        std::vector<std::uint8_t> payload;
        std::string error;
        bool ok = world::EncodePlayerSpawn(spawn, payload);
        world::PlayerSpawnPayload decoded;
        ok = ok && world::DecodePlayerSpawn(payload.data(), payload.size(), decoded, error) &&
             decoded.characterId == 7 && decoded.name == "SpawnHero";
        // 截断 -> malformed
        std::vector<std::uint8_t> truncated(payload.begin(), payload.end() - 5);
        ok = ok && !world::DecodePlayerSpawn(truncated.data(), truncated.size(), decoded, error);
        // trailing bytes -> malformed
        std::vector<std::uint8_t> trailing = payload;
        trailing.push_back(0xFF);
        ok = ok && !world::DecodePlayerSpawn(trailing.data(), trailing.size(), decoded, error);
        Check("MalformedSpawnProtocolCheck: truncated/trailing rejected", ok);
    }

    // ---- MalformedBatchCheck（指令一百零三）：count 超剩余 payload -> 拒绝 ----
    {
        std::vector<std::uint8_t> payload;
        {
            network::ByteWriter w(payload);
            w.WriteUInt64(12345); // serverTime
            w.WriteUInt16(5);     // count=5 但 0 条 entry
        }
        world::RemotePlayerBatchSnapshotPayload batch;
        std::string error;
        const bool ok = !world::DecodeRemotePlayerBatchSnapshot(payload.data(), payload.size(),
                                                                batch, error);
        Check("MalformedBatchCheck: count beyond payload rejected", ok);
    }

    // ---- BatchCountLimitCheck（指令一百零四）：count>128 拒绝；128 可解 ----
    {
        bool ok = true;
        {
            std::vector<std::uint8_t> payload;
            {
                network::ByteWriter w(payload);
                w.WriteUInt64(1);
                w.WriteUInt16(129); // 超上限
            }
            world::RemotePlayerBatchSnapshotPayload batch;
            std::string error;
            ok = ok && !world::DecodeRemotePlayerBatchSnapshot(payload.data(), payload.size(),
                                                               batch, error);
        }
        {
            world::RemotePlayerBatchSnapshotPayload big;
            big.serverTime = 77;
            for (std::uint16_t i = 0; i < 128; ++i) {
                big.players.push_back({static_cast<std::uint64_t>(100 + i),
                                       static_cast<float>(i), 0.0f, i});
            }
            std::vector<std::uint8_t> payload;
            ok = ok && world::EncodeRemotePlayerBatchSnapshot(big, payload);
            world::RemotePlayerBatchSnapshotPayload decoded;
            std::string error;
            ok = ok && world::DecodeRemotePlayerBatchSnapshot(payload.data(), payload.size(),
                                                              decoded, error) &&
                 decoded.players.size() == 128 && decoded.players[127].characterId == 227;
        }
        Check("BatchCountLimitCheck: count=129 rejected, 128 decodes", ok);
    }

    // ---- HysteresisCheck（指令七/八十一）：590/650/710 滞回曲线 ----
    {
        auto b = MakePlayer(2, 1, 590.0f, 0.0f);
        std::vector<AoiCandidate> candidates{{b, 590.0f * 590.0f}};
        std::unordered_set<std::uint64_t> visible;
        const float enter = 600.0f;
        const float leave = 700.0f;
        bool ok = true;
        // 1) 590 -> spawn
        AoiDelta delta = ResolveAoiVisibility(candidates, 1, visible, enter, leave, 128);
        ok = ok && delta.spawns.size() == 1 && delta.despawns.empty();
        visible.insert(2);
        // 2) 650 -> stay（滞回：不重复 spawn、不 despawn）
        candidates[0].distanceSquared = 650.0f * 650.0f;
        delta = ResolveAoiVisibility(candidates, 1, visible, enter, leave, 128);
        ok = ok && delta.spawns.empty() && delta.despawns.empty();
        // 3) 710 -> despawn（>700）
        candidates[0].distanceSquared = 710.0f * 710.0f;
        delta = ResolveAoiVisibility(candidates, 1, visible, enter, leave, 128);
        ok = ok && delta.spawns.empty() && delta.despawns.size() == 1 && delta.despawns[0] == 2;
        visible.erase(2);
        // 4) 回 650 -> 仍不可见（需 <=600 才重进）
        candidates[0].distanceSquared = 650.0f * 650.0f;
        delta = ResolveAoiVisibility(candidates, 1, visible, enter, leave, 128);
        ok = ok && delta.spawns.empty() && delta.despawns.empty();
        // 5) 回 590 -> 重新 spawn
        candidates[0].distanceSquared = 590.0f * 590.0f;
        delta = ResolveAoiVisibility(candidates, 1, visible, enter, leave, 128);
        ok = ok && delta.spawns.size() == 1 && delta.despawns.empty();
        Check("HysteresisCheck: 590 spawn / 650 stay / 710 despawn / 650 invisible / 590 respawn",
              ok);
    }

    // ---- EnterRadiusCheck（指令八十）：599 spawn，601 不 spawn ----
    {
        auto b = MakePlayer(2, 1, 599.0f, 0.0f);
        std::vector<AoiCandidate> candidates{{b, 599.0f * 599.0f}};
        std::unordered_set<std::uint64_t> visible;
        AoiDelta delta = ResolveAoiVisibility(candidates, 1, visible, 600.0f, 700.0f, 128);
        bool ok = delta.spawns.size() == 1;
        candidates[0].distanceSquared = 601.0f * 601.0f; // 未可见状态
        delta = ResolveAoiVisibility(candidates, 1, visible, 600.0f, 700.0f, 128);
        ok = ok && delta.spawns.empty();
        Check("EnterRadiusCheck: 599 spawns, 601 not", ok);
    }

    // ---- VisibleLimitCheck（指令九十五/二十七）：150 候选 -> 128，近优先 ----
    {
        std::vector<AoiCandidate> candidates;
        for (int i = 0; i < 150; ++i) {
            const float dist = 10.0f + 3.0f * static_cast<float>(i); // 10..457 全部 <=600
            candidates.push_back({MakePlayer(static_cast<std::uint64_t>(1000 + i), 1, dist, 0.0f),
                                  dist * dist});
        }
        std::unordered_set<std::uint64_t> visible;
        AoiDelta delta = ResolveAoiVisibility(candidates, 1, visible, 600.0f, 700.0f, 128);
        bool ok = delta.spawns.size() == 128;
        if (ok) {
            std::unordered_set<std::uint64_t> spawned;
            for (const auto& p : delta.spawns) {
                spawned.insert(p->CharacterId());
            }
            ok = spawned.count(1000) != 0;                       // 最近必在
            for (int i = 128; i < 150; ++i) {                    // 最远的 22 个不在
                ok = ok && spawned.count(static_cast<std::uint64_t>(1000 + i)) == 0;
            }
        }
        Check("VisibleLimitCheck: 150 candidates -> 128 spawns, nearest first", ok);
    }

    // ---- VisibleStableOrderCheck（指令九十六/二十八）：同距离 characterId 升序 ----
    {
        std::vector<AoiCandidate> candidates;
        const std::uint64_t ids[] = {400, 200, 300, 100}; // 打乱输入顺序
        for (const auto id : ids) {
            candidates.push_back({MakePlayer(id, 1, 100.0f, 0.0f), 100.0f * 100.0f});
        }
        std::unordered_set<std::uint64_t> visible;
        AoiDelta delta = ResolveAoiVisibility(candidates, 1, visible, 600.0f, 700.0f, 128);
        bool ok = delta.spawns.size() == 4;
        if (ok) {
            ok = delta.spawns[0]->CharacterId() == 100 && delta.spawns[1]->CharacterId() == 200 &&
                 delta.spawns[2]->CharacterId() == 300 && delta.spawns[3]->CharacterId() == 400;
        }
        Check("VisibleStableOrderCheck: equal distance ordered by characterId asc", ok);
    }

    // ---- DifferentMapCheck（指令二十/九十四）：跨地图绝不互相可见 ----
    {
        auto other = MakePlayer(2, 2, 100.0f, 0.0f); // mapId=2
        std::vector<AoiCandidate> candidates{{other, 100.0f * 100.0f}};
        std::unordered_set<std::uint64_t> visible;
        AoiDelta delta = ResolveAoiVisibility(candidates, 1, visible, 600.0f, 700.0f, 128);
        Check("DifferentMapCheck: mapId=2 candidate never spawns", delta.spawns.empty());
    }

    // ---- DuplicateSpawnClientCheck（指令八十九/三十四）：重复 spawn 不重复实体 ----
    {
        RemotePlayerManagerT manager;
        world::PlayerSpawnPayload spawn;
        spawn.characterId = 5;
        spawn.name = "First";
        spawn.positionX = 1.0f;
        spawn.positionY = 1.0f;
        manager.HandleSpawn(spawn);
        spawn.name = "Second";
        spawn.positionX = 2.0f;
        spawn.positionY = 2.0f;
        manager.HandleSpawn(spawn);
        const auto* entity = manager.Find(5);
        Check("DuplicateSpawnClientCheck: duplicate spawn keeps single entity",
              manager.Count() == 1 && entity != nullptr && entity->Name() == "Second" &&
                  entity->ServerX() == 2.0f);
    }

    // ---- UnknownSnapshotCheck（指令八十七/四十八）：未知 characterId 丢弃 ----
    {
        RemotePlayerManagerT manager;
        world::RemotePlayerBatchSnapshotPayload batch;
        batch.serverTime = 1;
        batch.players.push_back({999, 1.0f, 1.0f, 0});
        manager.HandleBatch(batch);
        Check("UnknownSnapshotCheck: unknown id snapshot dropped, no entity created",
              manager.Count() == 0 && manager.Find(999) == nullptr);
    }

    // ---- DespawnCleanupClientCheck（指令八十八/三十五）----
    {
        RemotePlayerManagerT manager;
        world::PlayerSpawnPayload spawn;
        spawn.characterId = 6;
        spawn.name = "Gone";
        manager.HandleSpawn(spawn);
        manager.HandleDespawn(6);
        Check("DespawnCleanupClientCheck: despawn removes entity",
              manager.Count() == 0 && manager.Find(6) == nullptr);
    }

    // ---- InterpolationCheck（指令三十七/九十）：渐进插值不瞬移 ----
    {
        legend::client::RemotePlayerEntity entity;
        world::PlayerSpawnPayload spawn;
        spawn.characterId = 8;
        spawn.name = "Lerp";
        spawn.positionX = 0.0f;
        spawn.positionY = 0.0f;
        entity.ApplySpawn(spawn);
        entity.ApplySnapshot(100.0f, 0.0f, 1);
        entity.UpdateInterpolation(0.1f); // t = 1-exp(-1.2) ~= 0.699
        const float first = entity.RenderX();
        bool ok = first > 50.0f && first < 100.0f; // 渐进，不瞬移
        for (int i = 0; i < 30; ++i) {
            entity.UpdateInterpolation(0.1f);
        }
        ok = ok && std::fabs(entity.RenderX() - 100.0f) < 1.0f;
        Check("InterpolationCheck: render approaches server position gradually", ok);
    }

    // ---- TeleportCorrectionCheck（指令三十八/九十一）：>300 直接 snap ----
    {
        legend::client::RemotePlayerEntity entity;
        world::PlayerSpawnPayload spawn;
        spawn.characterId = 9;
        spawn.name = "Snap";
        spawn.positionX = 0.0f;
        spawn.positionY = 0.0f;
        entity.ApplySpawn(spawn);
        entity.ApplySnapshot(400.0f, 0.0f, 1);
        entity.UpdateInterpolation(0.016f);
        Check("TeleportCorrectionCheck: >300 snaps to server position",
              entity.RenderX() == 400.0f && entity.RenderY() == 0.0f);
    }
}

// ===========================================================================
// B. 真实链路检查（RunWorldAoiChecks 自管 servers）
// ===========================================================================

// 建号建角并预置位置
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

// 扫描 batch 事件：返回某 characterId 的最新 entry 位置（-1 表示未出现）。
float LatestBatchXFor(WorldTestClient& client, std::uint64_t characterId) {
    client.DrainEvents();
    float x = -1.0f;
    for (const auto& ev :
         client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::RemotePlayerBatchSnapshot)]) {
        for (const auto& entry : ev.batchPlayers) {
            if (entry.characterId == characterId) {
                x = entry.positionX;
            }
        }
    }
    return x;
}

int CountRecordedFor(const WorldTestClient& client, WorldNetworkEvent::Type type,
                     std::uint64_t characterId) {
    int n = 0;
    for (const auto& e : client.recorded[WorldTestClient::IndexOf(type)]) {
        if (e.characterId == characterId) {
            ++n;
        }
    }
    return n;
}

bool WaitCountAtLeast(WorldTestClient& client, WorldNetworkEvent::Type type,
                      std::uint64_t characterId, int minimum, int timeoutMs) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            return CountRecordedFor(client, type, characterId) >= minimum;
        },
        timeoutMs);
}

} // namespace

void RunWorldAoiChecks() {
    // A. 纯逻辑检查（SpatialGrid/resolver/RemotePlayerManager/插值/畸形协议）
    RunAoiLogicChecks();

    // B. 真实链路检查
    WorldTestServers servers;
    servers.dbPath = TempDbPath("world_aoi");
    RemoveDb(servers.dbPath);
    Check("AoiServersStartCheck", servers.StartLogin() && servers.StartWorld());

    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(5, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("AoiChecks: db ready", false);
        servers.StopAll();
        return;
    }

    const int spawnIdx = WorldTestClient::IndexOf(WorldNetworkEvent::Type::PlayerSpawn);
    const int despawnIdx = WorldTestClient::IndexOf(WorldNetworkEvent::Type::PlayerDespawn);

    // ---- InitialSpawnCheck（指令十八/五十四/七十八）：进入附近 -> 双向 Spawn ----
    CharacterSeed seedA;
    CharacterSeed seedB;
    CharacterSeed seedC;
    CharacterSeed seedD;
    bool seeded = SeedAt(db, accounts, characters, "aoi_user_a", "AoiA", 100.0f, 100.0f, seedA) &&
                  SeedAt(db, accounts, characters, "aoi_user_b", "AoiB", 150.0f, 100.0f, seedB) &&
                  SeedAt(db, accounts, characters, "aoi_user_c", "AoiC", 1000.0f, 1000.0f, seedC) &&
                  SeedAt(db, accounts, characters, "aoi_user_d", "AoiD", 160.0f, 100.0f, seedD);
    Check("AoiChecks: seeds ready (A/B/C/D)", seeded);

    WorldTestClient clientA;
    WorldTestClient clientB;
    const std::string ticketA = seeded ? TicketFor(servers.login, seedA) : std::string();
    const std::string ticketB = seeded ? TicketFor(servers.login, seedB) : std::string();
    const bool enterA = !ticketA.empty() && clientA.ConnectAndEnter(ticketA, 8000);
    const bool enterB = !ticketB.empty() && clientB.ConnectAndEnter(ticketB, 8000);
    Check("InitialSpawnCheck: both entered", seeded && enterA && enterB);
    bool ok = enterA && enterB;
    ok = ok && WaitCountAtLeast(clientB, WorldNetworkEvent::Type::PlayerSpawn, seedA.characterId,
                                1, 3000); // B 收到 A spawn（进入初始可见性）
    ok = ok && WaitCountAtLeast(clientA, WorldNetworkEvent::Type::PlayerSpawn, seedB.characterId,
                                1, 3000); // A 收到 B spawn
    Check("InitialSpawnCheck: mutual spawn on nearby enter", ok);

    // ---- NoSpawnFarCheck（指令五十五/七十九）：距离 >700 无 Spawn ----
    {
        const std::string ticketC = seeded ? TicketFor(servers.login, seedC) : std::string();
        WorldTestClient clientC;
        const bool enterC = !ticketC.empty() && clientC.ConnectAndEnter(ticketC, 8000);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200)); // >=6 个 AOI tick
        clientA.DrainEvents();
        clientC.DrainEvents();
        Check("NoSpawnFarCheck: far players never spawn each other",
              enterC && CountRecordedFor(clientA, WorldNetworkEvent::Type::PlayerSpawn,
                                         seedC.characterId) == 0 &&
                  CountRecordedFor(clientC, WorldNetworkEvent::Type::PlayerSpawn,
                                   seedA.characterId) == 0);
        clientC.Disconnect();
        WaitUntil([&] { clientC.DrainEvents(); return true; }, 300);
    }

    // ---- SpawnExactlyOnceCheck（指令五十/六十一/八十二）----
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(700)); // >=3 个 AOI tick
        clientA.DrainEvents();
        clientB.DrainEvents();
        Check("SpawnExactlyOnceCheck: stays visible, spawn count still 1",
              CountRecordedFor(clientA, WorldNetworkEvent::Type::PlayerSpawn,
                               seedB.characterId) == 1 &&
                  CountRecordedFor(clientB, WorldNetworkEvent::Type::PlayerSpawn,
                                   seedA.characterId) == 1);
    }

    // ---- DespawnExactlyOnceCheck（指令五十一/六十二/八十三）：远离 >700 ----
    std::uint32_t seqB = 0;
    {
        for (int i = 0; i < 60; ++i) {
            clientB.client().SendMoveInput(++seqB, 1.0f, 0.0f, 0.1f); // B.x: 150 -> 870
        }
        ok = WaitCountAtLeast(clientA, WorldNetworkEvent::Type::PlayerDespawn, seedB.characterId,
                              1, 3000);
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 再等数个 tick
        clientA.DrainEvents();
        bool exactlyOnce = ok && CountRecordedFor(clientA, WorldNetworkEvent::Type::PlayerDespawn,
                                                  seedB.characterId) == 1;
        if (exactlyOnce) {
            const auto& ev = clientA.recorded[despawnIdx].front();
            exactlyOnce = ev.despawnReason ==
                          static_cast<std::uint8_t>(world::PlayerDespawnReason::LeftAOI);
        }
        Check("DespawnExactlyOnceCheck: leave AOI -> single LeftAOI despawn", exactlyOnce);
    }

    // ---- ReenterSpawnCheck（指令五十二/八十四）：回进 AOI 重新 Spawn（count=2）----
    {
        for (int i = 0; i < 60; ++i) {
            clientB.client().SendMoveInput(++seqB, -1.0f, 0.0f, 0.1f); // B.x: 870 -> 150
        }
        ok = WaitCountAtLeast(clientA, WorldNetworkEvent::Type::PlayerSpawn, seedB.characterId,
                              2, 3000);
        Check("ReenterSpawnCheck: re-enter -> second spawn", ok);
    }

    // ---- RemoteSnapshotCheck（指令二十九/三十一/八十五）：batch 与权威位置一致 ----
    {
        clientB.client().SendMoveInput(++seqB, 1.0f, 0.0f, 0.1f); // B.x: 150 -> 162
        WorldNetworkEvent ownSnap;
        ok = clientB.WaitForSnapshotAfterSeq(seqB, ownSnap);
        // 等 A 的 batch 中出现 B 的新位置（>155 排除旧值 150）
        bool gotRemote = false;
        float remoteX = -1.0f;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        while (std::chrono::steady_clock::now() < deadline) {
            remoteX = LatestBatchXFor(clientA, seedB.characterId);
            if (remoteX > 155.0f) {
                gotRemote = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        ok = ok && gotRemote && std::fabs(remoteX - ownSnap.positionX) < 0.01f;
        Check("RemoteSnapshotCheck: batch position matches authoritative position", ok);
    }

    // ---- RemoteSnapshotNoSelfCheck（指令三十/八十六）：batch 不含接收者自己 ----
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        clientA.DrainEvents();
        clientB.DrainEvents();
        bool noSelf = true;
        for (const auto& ev : clientA
                                  .recorded[WorldTestClient::IndexOf(
                                      WorldNetworkEvent::Type::RemotePlayerBatchSnapshot)]) {
            for (const auto& entry : ev.batchPlayers) {
                if (entry.characterId == seedA.characterId) {
                    noSelf = false;
                }
            }
        }
        for (const auto& ev : clientB
                                  .recorded[WorldTestClient::IndexOf(
                                      WorldNetworkEvent::Type::RemotePlayerBatchSnapshot)]) {
            for (const auto& entry : ev.batchPlayers) {
                if (entry.characterId == seedB.characterId) {
                    noSelf = false;
                }
            }
        }
        Check("RemoteSnapshotNoSelfCheck: batch never contains receiver", noSelf);
    }

    // ---- DisconnectDespawnCheck（指令十九/九十二）：断线 -> Disconnected despawn ----
    {
        clientB.Disconnect();
        ok = WaitCountAtLeast(clientA, WorldNetworkEvent::Type::PlayerDespawn, seedB.characterId,
                              2, 3000); // 第一次 LeftAOI 已记录，本次 Disconnected
        bool reasonOk = false;
        if (ok) {
            for (const auto& ev : clientA.recorded[despawnIdx]) {
                if (ev.characterId == seedB.characterId &&
                    ev.despawnReason ==
                        static_cast<std::uint8_t>(world::PlayerDespawnReason::Disconnected)) {
                    reasonOk = true;
                }
            }
        }
        clientA.DrainEvents();
        Check("DisconnectDespawnCheck: despawn(Disconnected) received, manager cleaned",
              ok && reasonOk && clientA.controller.RemotePlayers().Find(seedB.characterId) == nullptr);
        WaitUntil([&] { return servers.world->PlayerCount() == 1; }, 3000);
    }

    // ---- GhostPlayerCheck（指令五十九/六十/九十三）：无 ghost 残留 ----
    {
        const std::string ticketD = seeded ? TicketFor(servers.login, seedD) : std::string();
        WorldTestClient clientD;
        const bool enterD = !ticketD.empty() && clientD.ConnectAndEnter(ticketD, 8000);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200)); // >=6 个 AOI tick
        clientD.DrainEvents();
        // D 在 B 旧位置附近：若 B 残留 grid/map，D 会收到 B 的 spawn
        Check("GhostPlayerCheck: no ghost after disconnect",
              enterD &&
                  CountRecordedFor(clientD, WorldNetworkEvent::Type::PlayerSpawn,
                                   seedB.characterId) == 0 &&
                  servers.world != nullptr && servers.world->PlayerCount() == 2);
        // D 与 A 互见（顺带验证断线后世界继续服务）
        clientD.Disconnect();
        WaitUntil([&] { clientD.DrainEvents(); return true; }, 300);
    }

    // ---- MultiPlayerMovementReplicationCheck（指令九十八）：10 客户端互见 + 移动同步 ----
    {
        constexpr int kGroup = 10;
        std::vector<CharacterSeed> seeds(kGroup);
        bool groupSeeded = true;
        for (int i = 0; i < kGroup; ++i) {
            groupSeeded =
                groupSeeded &&
                SeedAt(db, accounts, characters, "aoi_m_" + std::to_string(i),
                       "AoiM" + std::to_string(i), 800.0f + 10.0f * i, 800.0f, seeds[i]);
        }
        std::vector<std::unique_ptr<WorldTestClient>> clients(kGroup);
        for (auto& client : clients) {
            client = std::make_unique<WorldTestClient>();
        }
        std::atomic<int> ready{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < kGroup; ++i) {
            threads.emplace_back([&clients, &seeds, &servers, &ready, i, &groupSeeded] {
                const std::string ticket = groupSeeded ? TicketFor(servers.login, seeds[i])
                                                       : std::string();
                auto& client = *clients[i];
                if (!ticket.empty() && client.ConnectAndEnter(ticket, 15000)) {
                    ++ready;
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        // 每个客户端看到其他 9 个（组内相互距离 <=90 <600；远离 A/D）
        bool allSee = ready == kGroup;
        if (allSee) {
            allSee = WaitUntil(
                [&] {
                    bool done = true;
                    for (auto& client : clients) {
                        client->DrainEvents();
                        if (client->controller.RemotePlayers().Count() < 9) {
                            done = false;
                        }
                    }
                    return done;
                },
                8000);
        }
        // M0 移动 -> 其他 9 个收到位置同步
        clients[0]->controller.SendMoveInput(1.0f, 0.0f, 0.1f); // x: 800 -> 812
        bool replicated = allSee;
        for (int i = 1; i < kGroup && replicated; ++i) {
            replicated = WaitUntil(
                [&] {
                    const float x = LatestBatchXFor(*clients[i], seeds[0].characterId);
                    return x > 805.0f; // 排除旧值 800
                },
                4000);
        }
        Check("MultiPlayerMovementReplicationCheck: 10 clients see 9, move replicated",
              groupSeeded && replicated);
        for (auto& client : clients) {
            client->Disconnect();
        }
        WaitUntil([&] { return true; }, 500);
    }

    // ---- AOISeparationCheck（指令九十九）：两组 >700 互不可见 ----
    {
        constexpr int kGroup = 10;
        std::vector<CharacterSeed> groupP(kGroup);
        std::vector<CharacterSeed> groupQ(kGroup);
        bool groupSeeded = true;
        for (int i = 0; i < kGroup; ++i) {
            groupSeeded =
                groupSeeded &&
                SeedAt(db, accounts, characters, "aoi_p_" + std::to_string(i),
                       "AoiP" + std::to_string(i), 1000.0f + 10.0f * i, 1000.0f, groupP[i]) &&
                SeedAt(db, accounts, characters, "aoi_q_" + std::to_string(i),
                       "AoiQ" + std::to_string(i), 1700.0f + 10.0f * i, 1700.0f, groupQ[i]);
        }
        std::vector<std::unique_ptr<WorldTestClient>> clientsP(kGroup);
        std::vector<std::unique_ptr<WorldTestClient>> clientsQ(kGroup);
        std::atomic<int> ready{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < kGroup; ++i) {
            clientsP[i] = std::make_unique<WorldTestClient>();
            clientsQ[i] = std::make_unique<WorldTestClient>();
            threads.emplace_back([&clientsP, &clientsQ, &groupP, &groupQ, &servers, &ready, i,
                                  &groupSeeded] {
                const std::string tp = groupSeeded ? TicketFor(servers.login, groupP[i])
                                                   : std::string();
                const std::string tq = groupSeeded ? TicketFor(servers.login, groupQ[i])
                                                   : std::string();
                if (!tp.empty() && clientsP[i]->ConnectAndEnter(tp, 15000)) {
                    ++ready;
                }
                if (!tq.empty() && clientsQ[i]->ConnectAndEnter(tq, 15000)) {
                    ++ready;
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1500)); // 等多个 AOI tick
        bool separated = groupSeeded && ready == 2 * kGroup;
        if (separated) {
            std::unordered_set<std::uint64_t> idsP;
            std::unordered_set<std::uint64_t> idsQ;
            for (const auto& s : groupP) {
                idsP.insert(s.characterId);
            }
            for (const auto& s : groupQ) {
                idsQ.insert(s.characterId);
            }
            for (int i = 0; i < kGroup && separated; ++i) {
                clientsP[i]->DrainEvents();
                clientsQ[i]->DrainEvents();
                if (clientsP[i]->controller.RemotePlayers().Count() != 9) {
                    separated = false;
                }
                if (clientsQ[i]->controller.RemotePlayers().Count() != 9) {
                    separated = false;
                }
                for (const auto& [id, entity] : clientsP[i]->controller.RemotePlayers().All()) {
                    if (idsQ.count(id) != 0) {
                        separated = false;
                    }
                }
                for (const auto& [id, entity] : clientsQ[i]->controller.RemotePlayers().All()) {
                    if (idsP.count(id) != 0) {
                        separated = false;
                    }
                }
            }
        }
        Check("AOISeparationCheck: two groups >700 never see each other", separated);

        // ---- AOIMergeCheck（指令一百）：两组接近 -> 互相 Spawn ----
        for (int i = 0; i < kGroup; ++i) {
            for (int k = 0; k < 90; ++k) {
                clientsQ[i]->client().SendMoveInput(
                    static_cast<std::uint32_t>(k + 1), -1.0f, -1.0f, 0.1f); // 90*8.49 -> ~936
            }
        }
        bool merged = separated;
        if (merged) {
            merged = WaitUntil(
                [&] {
                    bool done = true;
                    for (int i = 0; i < kGroup; ++i) {
                        clientsP[i]->DrainEvents();
                        if (clientsP[i]->controller.RemotePlayers().Count() < 15) {
                            done = false;
                        }
                        clientsQ[i]->DrainEvents();
                        if (clientsQ[i]->controller.RemotePlayers().Count() < 15) {
                            done = false;
                        }
                    }
                    return done;
                },
                8000);
        }
        Check("AOIMergeCheck: approaching groups spawn mutually", merged);

        // ---- AOISplitCheck（指令一百零一）：远离 -> 互相 Despawn ----
        for (int i = 0; i < kGroup; ++i) {
            for (int k = 0; k < 90; ++k) {
                clientsQ[i]->client().SendMoveInput(
                    static_cast<std::uint32_t>(100 + k), 1.0f, 1.0f, 0.1f); // 回 ~1700
            }
        }
        bool split = merged;
        if (split) {
            split = WaitUntil(
                [&] {
                    bool done = true;
                    for (int i = 0; i < kGroup; ++i) {
                        clientsP[i]->DrainEvents();
                        if (clientsP[i]->controller.RemotePlayers().Count() != 9) {
                            done = false;
                        }
                        clientsQ[i]->DrainEvents();
                        if (clientsQ[i]->controller.RemotePlayers().Count() != 9) {
                            done = false;
                        }
                    }
                    return done;
                },
                8000);
        }
        Check("AOISplitCheck: separating groups despawn mutually", split);

        for (int i = 0; i < kGroup; ++i) {
            clientsP[i]->Disconnect();
            clientsQ[i]->Disconnect();
        }
        WaitUntil([&] { return true; }, 500);
    }

    clientA.Disconnect();
    WaitUntil([&] { clientA.DrainEvents(); return true; }, 300);
    db.Close();
    servers.StopAll();
}

} // namespace worldtest
