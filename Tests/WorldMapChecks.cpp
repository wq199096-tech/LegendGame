// ---------------------------------------------------------------------------
// 阶段21：多地图 / Portal / 复活检查（Multi-Map / Portal / Respawn Core V0.21）。
// 仍链接 LegendWorldTests（不新增第四个测试 exe，指令一百一十）。
// A 部分：纯逻辑（MapRegistry/PortalRegistry/Map·Portal·Respawn 协议 roundtrip/
//         malformed）。
// B 部分：真实链路（跨地图隔离/边界钳制/Portal AOI+验证链/统一 MapTransitionService/
//         MapChanged/MapSnapshot/持久化修正/死亡-复活全链/保护）。
// 回归：Quest/Shop/Combat/Skill/Status/Progression/Inventory 由既有套件在同一 exe
// 覆盖（指令一百零三~一百零九）；NPC Teleport 统一路径见 NpcTeleportRegressionCheck。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Server/LoginServer/Account/AccountRepository.h"
#include "Server/WorldServer/Item/ItemRegistry.h"
#include "Server/WorldServer/Map/MapRegistry.h"
#include "Server/WorldServer/Map/RespawnService.h"
#include "Server/WorldServer/Monster/MonsterRespawnManager.h"
#include "Server/WorldServer/PlayerSession.h"
#include "Server/WorldServer/Portal/PortalRegistry.h"
#include "Shared/Item/ItemTypes.h"
#include "Shared/Monster/MonsterTypes.h"
#include "Shared/Portal/PortalProtocol.h"
#include "Shared/WorldMap/MapProtocol.h"
#include "Shared/WorldMap/RespawnProtocol.h"

#include <cmath>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include <type_traits>

namespace worldtest {

namespace {

using namespace legend::world;
using legend::world::MapRegistry;
using legend::world::PortalRegistry;
namespace CharacterRepository = legend::account::CharacterRepository;

const std::uint8_t kTypePlayer = static_cast<std::uint8_t>(legend::world::CombatEntityType::Player);
const std::uint8_t kTypeMonster =
    static_cast<std::uint8_t>(legend::world::CombatEntityType::Monster);

std::size_t CountEventsOf(WorldTestClient& client, WorldNetworkEvent::Type type) {
    return client.recorded[WorldTestClient::IndexOf(type)].size();
}

bool FindRecordedFrom(WorldTestClient& client, WorldNetworkEvent::Type type,
                      std::size_t baseline,
                      const std::function<bool(const WorldNetworkEvent&)>& pred,
                      WorldNetworkEvent& out) {
    const auto& events = client.recorded[WorldTestClient::IndexOf(type)];
    if (events.size() <= baseline) {
        return false;
    }
    for (auto it = events.begin() + static_cast<std::ptrdiff_t>(baseline); it != events.end();
         ++it) {
        if (pred(*it)) {
            out = *it;
            return true;
        }
    }
    return false;
}

bool WaitRecordedFrom(WorldTestClient& client, WorldNetworkEvent::Type type,
                      std::size_t baseline,
                      const std::function<bool(const WorldNetworkEvent&)>& pred,
                      WorldNetworkEvent& out, int timeoutMs = 5000) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            return FindRecordedFrom(client, type, baseline, pred, out);
        },
        timeoutMs);
}

std::string TicketFor(const std::shared_ptr<LoginServer>& login, const CharacterSeed& seed) {
    if (!login) {
        return {};
    }
    return login->Tickets().Create(seed.accountId, seed.characterId, 600.0);
}

// 按地图落点的建角（阶段21：Map2/Map3 玩家布景）。
bool SeedAtOnMap(Database& db, AccountService& accounts, CharacterService& characters,
                 const std::string& username, const std::string& charName, std::uint16_t mapId,
                 float x, float y, CharacterSeed& out) {
    if (!SeedAccountAndCharacter(db, accounts, characters, username, charName, out)) {
        return false;
    }
    return CharacterRepository::UpdateWorldPosition(db, out.characterId, mapId, x, y,
                                                    account::UnixNow())
        .success;
}

template <typename F>
auto RunOnWorldIo(net::NetworkService& service, F&& fn) -> std::invoke_result_t<F&> {
    using R = std::invoke_result_t<F&>;
    std::promise<R> promise;
    auto future = promise.get_future();
    service.Post([&promise, &fn]() {
        if constexpr (std::is_void_v<R>) {
            fn();
            promise.set_value();
        } else {
            promise.set_value(fn());
        }
    });
    return future.get();
}

// 服务器权威步进移动（阶段20 同款；阶段21 边界钳制按 MapDefinition）。
bool MovePlayerTo(WorldTestClient& client, WorldTestServers& servers, std::uint64_t characterId,
                  float targetX, float targetY) {
    static std::uint32_t s_moveSeq = 300000;
    auto player = servers.world->FindPlayerByCharacter(characterId);
    if (!player) {
        return false;
    }
    const float dx = targetX - player->PositionX();
    const float dy = targetY - player->PositionY();
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist >= 1.0f) {
        const float ux = dx / dist;
        const float uy = dy / dist;
        const int steps = static_cast<int>(std::ceil(dist / 12.0f));
        for (int i = 0; i < steps; ++i) {
            client.client().SendMoveInput(++s_moveSeq, ux, uy, 0.1f);
        }
    }
    return WaitUntil(
        [&] {
            auto p = servers.world->FindPlayerByCharacter(characterId);
            if (!p) {
                return false;
            }
            const float ex = p->PositionX() - targetX;
            const float ey = p->PositionY() - targetY;
            return (ex * ex + ey * ey) <= 169.0f;
        },
        6000);
}

std::int64_t QueryCharacterColumn(const std::string& dbPath, std::uint64_t characterId,
                                  const char* column) {
    return QueryScalar(dbPath,
                       std::string("SELECT ") + column + " FROM characters WHERE id = " +
                           std::to_string(static_cast<long long>(characterId)) + ";");
}

// 找某张地图上的一只活怪（Map2/Map3 布景用）。
std::uint64_t FindMonsterOnMap(WorldTestServers& servers, std::uint16_t mapId) {
    return RunOnWorldIo(servers.worldService, [&]() -> std::uint64_t {
        for (const auto& entityId : servers.world->MonsterEntityIds()) {
            auto monster = servers.world->FindMonster(entityId);
            if (monster && monster->MapId() == mapId && monster->Alive()) {
                return entityId;
            }
        }
        return 0;
    });
}

} // namespace

// ===========================================================================
// A. 纯逻辑检查
// ===========================================================================

void RunWorldMapLogicChecks() {
    // ---- MapRegistryCheck（指令三/五/六）----
    {
        const auto& registry = MapRegistry::Instance();
        bool ok = registry.Count() == 3;
        const MapDefinition* map1 = registry.FindMap(1);
        ok = ok && map1 != nullptr && map1->name == "Greenfield Village" &&
             map1->type == MapType::Town && map1->maxX == 2000.0f && map1->maxY == 2000.0f &&
             map1->spawnX == 300.0f && map1->spawnY == 300.0f && map1->respawnX == 300.0f &&
             map1->respawnY == 300.0f;
        const MapDefinition* map2 = registry.FindMap(2);
        ok = ok && map2 != nullptr && map2->name == "Slime Meadow" && map2->type == MapType::Field &&
             map2->spawnX == 200.0f && map2->spawnY == 500.0f && map2->respawnX == 200.0f &&
             map2->respawnY == 500.0f;
        const MapDefinition* map3 = registry.FindMap(3);
        ok = ok && map3 != nullptr && map3->name == "Ancient Ruins" && map3->type == MapType::Field &&
             map3->maxX == 2400.0f && map3->maxY == 1800.0f && map3->spawnX == 200.0f &&
             map3->spawnY == 300.0f;
        ok = ok && registry.FindMap(999) == nullptr && registry.TownMap() == map1;
        Check("MapRegistryCheck: Map1/2/3 defined per spec", ok);
    }

    // ---- MapBoundsValidationCheck（指令七：启动校验规则）----
    {
        std::string error;
        const bool ok =
            MapRegistry::Instance().ValidateMaps(error) && error.empty() &&
            PortalRegistry::Instance().ValidatePortals(MapRegistry::Instance(), error) &&
            error.empty();
        Check("MapBoundsValidationCheck: map+portal registries validate", ok);
    }

    // ---- PortalRegistryCheck（指令十六/七十）----
    {
        const auto& registry = PortalRegistry::Instance();
        bool ok = registry.Count() == 4;
        const PortalDefinition* p8001 = registry.FindPortal(8001);
        ok = ok && p8001 != nullptr && p8001->sourceMapId == 1 && p8001->x == 1000.0f &&
             p8001->y == 300.0f && p8001->destinationMapId == 2 &&
             p8001->destinationX == 200.0f && p8001->destinationY == 500.0f &&
             p8001->minLevel == 1 && p8001->goldCost == 0 && p8001->enabled;
        const PortalDefinition* p8002 = registry.FindPortal(8002);
        ok = ok && p8002 != nullptr && p8002->sourceMapId == 2 && p8002->x == 150.0f &&
             p8002->y == 500.0f && p8002->destinationMapId == 1 &&
             p8002->destinationX == 900.0f && p8002->destinationY == 300.0f;
        const PortalDefinition* p8003 = registry.FindPortal(8003);
        ok = ok && p8003 != nullptr && p8003->sourceMapId == 2 && p8003->x == 1800.0f &&
             p8003->y == 1000.0f && p8003->destinationMapId == 3 &&
             p8003->minLevel == 2 && p8003->goldCost == 10;
        const PortalDefinition* p8004 = registry.FindPortal(8004);
        ok = ok && p8004 != nullptr && p8004->sourceMapId == 3 && p8004->x == 150.0f &&
             p8004->y == 300.0f && p8004->destinationMapId == 2 &&
             p8004->destinationX == 1700.0f && p8004->destinationY == 1000.0f &&
             p8004->goldCost == 0;
        ok = ok && registry.FindPortal(9999) == nullptr;
        Check("PortalRegistryCheck: 8001~8004 configured per spec", ok);
    }

    // ---- MapProtocolRoundtripCheck（指令二十六/二十七/五十七）----
    {
        MapChangedPayload changed;
        changed.mapId = 2;
        changed.mapName = "Slime Meadow";
        changed.x = 200.0f;
        changed.y = 500.0f;
        changed.serverTime = 123456;
        std::vector<std::uint8_t> buffer;
        MapChangedPayload decoded;
        std::string error;
        bool ok = EncodeMapChanged(changed, buffer) &&
                  DecodeMapChanged(buffer.data(), buffer.size(), decoded, error) &&
                  decoded.mapId == 2 && decoded.mapName == "Slime Meadow" &&
                  decoded.x == 200.0f && decoded.serverTime == 123456;
        MapSnapshotPayload snapshot;
        snapshot.mapId = 3;
        snapshot.mapName = "Ancient Ruins";
        snapshot.maxX = 2400.0f;
        snapshot.maxY = 1800.0f;
        snapshot.respawnX = 200.0f;
        snapshot.respawnY = 300.0f;
        snapshot.serverTime = 42;
        buffer.clear();
        MapSnapshotPayload decodedSnap;
        ok = ok && EncodeMapSnapshot(snapshot, buffer) &&
             DecodeMapSnapshot(buffer.data(), buffer.size(), decodedSnap, error) &&
             decodedSnap.mapId == 3 && decodedSnap.mapName == "Ancient Ruins" &&
             decodedSnap.maxX == 2400.0f && decodedSnap.respawnY == 300.0f;
        Check("MapProtocolRoundtripCheck: MapChanged/MapSnapshot encode-decode", ok);
    }

    // ---- MapProtocolMalformedCheck（指令五十七：IsValid + Remaining==0）----
    {
        MapSnapshotPayload snapshot;
        snapshot.mapId = 1;
        snapshot.mapName = "Greenfield Village";
        std::vector<std::uint8_t> buffer;
        (void)EncodeMapSnapshot(snapshot, buffer);
        MapSnapshotPayload out;
        std::string error;
        const bool truncated = !DecodeMapSnapshot(buffer.data(), buffer.size() - 3, out, error);
        const bool trailing = !DecodeMapSnapshot(buffer.data(), buffer.size(), out, error) ||
                              !DecodeMapSnapshot(buffer.data(), buffer.size() + 1, out, error);
        Check("MapProtocolMalformedCheck: truncated/trailing payload rejected", truncated && trailing);
    }

    // ---- PortalProtocolRoundtripCheck（指令二十/二十一/二十二）----
    {
        std::vector<std::uint8_t> buffer;
        std::string error;
        std::uint64_t entityId = 0;
        std::uint32_t portalId = 0;
        std::string name;
        std::uint16_t mapId = 0;
        float x = 0, y = 0, radius = 0;
        std::uint16_t destMap = 0;
        std::string destName;
        bool ok = EncodePortalSpawn(buffer, 7, 8001, "Portal 8001", 1, 1000.0f, 300.0f, 100.0f, 2,
                                    "Slime Meadow") &&
                  DecodePortalSpawn(buffer.data(), buffer.size(), entityId, portalId, name, mapId,
                                    x, y, radius, destMap, destName, error) &&
                  entityId == 7 && portalId == 8001 && destMap == 2 &&
                  destName == "Slime Meadow" && radius == 100.0f;
        buffer.clear();
        std::uint64_t despawnId = 0;
        std::uint8_t despawnReason = 0;
        ok = ok && EncodePortalDespawn(buffer, 7, 1) &&
             DecodePortalDespawn(buffer.data(), buffer.size(), despawnId, despawnReason, error) &&
             despawnId == 7 && despawnReason == 1;
        PortalUseRequestPayload useReq;
        useReq.requestId = 99;
        useReq.portalEntityId = 3;
        PortalUseRequestPayload decodedReq;
        buffer.clear();
        ok = ok && EncodePortalUseRequest(useReq, buffer) &&
             DecodePortalUseRequest(buffer.data(), buffer.size(), decodedReq, error) &&
             decodedReq.requestId == 99 && decodedReq.portalEntityId == 3;
        PortalUseResponsePayload useResp;
        useResp.requestId = 99;
        useResp.success = true;
        useResp.resultCode = 0;
        useResp.destinationMapId = 2;
        useResp.destinationX = 200.0f;
        useResp.destinationY = 500.0f;
        useResp.goldCost = 10;
        useResp.newGold = 90;
        PortalUseResponsePayload decodedResp;
        buffer.clear();
        ok = ok && EncodePortalUseResponse(useResp, buffer) &&
             DecodePortalUseResponse(buffer.data(), buffer.size(), decodedResp, error) &&
             decodedResp.success && decodedResp.goldCost == 10 && decodedResp.newGold == 90;
        Check("PortalProtocolRoundtripCheck: Spawn/Despawn/Use encode-decode", ok);
    }

    // ---- RespawnProtocolRoundtripCheck（指令三十三/三十四/四十二/五十八）----
    {
        std::vector<std::uint8_t> buffer;
        std::string error;
        RespawnRequestPayload req;
        req.requestId = 5;
        req.respawnMode = 2;
        RespawnRequestPayload decodedReq;
        bool ok = EncodeRespawnRequest(req, buffer) &&
                  DecodeRespawnRequest(buffer.data(), buffer.size(), decodedReq, error) &&
                  decodedReq.requestId == 5 && decodedReq.respawnMode == 2;
        RespawnResponsePayload resp;
        resp.requestId = 5;
        resp.success = true;
        resp.resultCode = 0;
        resp.mapId = 2;
        resp.x = 200.0f;
        resp.y = 500.0f;
        resp.goldCost = 10;
        resp.newGold = 40;
        RespawnResponsePayload decodedResp;
        buffer.clear();
        ok = ok && EncodeRespawnResponse(resp, buffer) &&
             DecodeRespawnResponse(buffer.data(), buffer.size(), decodedResp, error) &&
             decodedResp.mapId == 2 && decodedResp.newGold == 40;
        PlayerRespawnedPayload respawned;
        respawned.mapId = 1;
        respawned.hp = 100;
        respawned.maxHp = 100;
        respawned.mana = 100;
        respawned.maxMana = 100;
        respawned.gold = 90;
        respawned.serverTime = 777;
        PlayerRespawnedPayload decodedRespawned;
        buffer.clear();
        ok = ok && EncodePlayerRespawned(respawned, buffer) &&
             DecodePlayerRespawned(buffer.data(), buffer.size(), decodedRespawned, error) &&
             decodedRespawned.hp == 100 && decodedRespawned.gold == 90;
        // 指令三十四：RespawnService 纯规则——mode 语义非法拒绝。
        Check("RespawnProtocolRoundtripCheck: Request/Response/PlayerRespawned encode-decode", ok);
    }
}

// ===========================================================================
// B. 真实链路检查
// ===========================================================================

void RunWorldMapChainChecks() {
    WorldTestServers servers;
    servers.dbPath = TempDbPath("world_map");
    RemoveDb(servers.dbPath);
    Check("MapServersStartCheck", servers.StartLogin() && servers.StartWorld());
    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(8, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    CharacterSeed seedA; // Map1
    CharacterSeed seedB; // Map2
    const bool seeded = dbOk &&
                        SeedAtOnMap(db, accounts, characters, "map_user_a", "MapA", 1, 300.0f,
                                    300.0f, seedA) &&
                        SeedAtOnMap(db, accounts, characters, "map_user_b", "MapB", 2, 200.0f,
                                    500.0f, seedB);
    WorldTestClient clientA;
    WorldTestClient clientB;
    const bool entered = seeded &&
                         clientA.ConnectAndEnter(TicketFor(servers.login, seedA), 8000) &&
                         clientB.ConnectAndEnter(TicketFor(servers.login, seedB), 8000);
    Check("MapScenarioSetupCheck", entered);
    if (!entered) {
        servers.StopAll();
        return;
    }
    clientA.DrainEvents();
    clientB.DrainEvents();
    // CI/慢机防护：A 多次穿越 Map1 史莱姆簇（Portal 来回移动），未buff会被围殴致死。
    servers.world->TestBuffPlayerHp(seedA.characterId, 1000000);
    servers.world->TestBuffPlayerHp(seedB.characterId, 1000000);

    // ---- EnterMapSnapshotCheck（指令二十七）：进入世界收到本人地图 MapSnapshot ----
    {
        WorldNetworkEvent snapA;
        WorldNetworkEvent snapB;
        const bool okA = WaitRecordedFrom(clientA, WorldNetworkEvent::Type::MapSnapshotEvent, 0,
                                          [](const WorldNetworkEvent& e) {
                                              return e.mapSnapshot.mapId == 1;
                                          },
                                          snapA, 3000);
        const bool okB = WaitRecordedFrom(clientB, WorldNetworkEvent::Type::MapSnapshotEvent, 0,
                                          [](const WorldNetworkEvent& e) {
                                              return e.mapSnapshot.mapId == 2;
                                          },
                                          snapB, 3000);
        Check("EnterMapSnapshotCheck: MapSnapshot delivered per player map", okA && okB);
    }

    // ---- MapIsolationPlayerCheck（指令十/六十六/八十一）：跨图玩家互不可见 ----
    //（A(300,300)/B(200,500) 平面距离 224 < 600——若 AOI 不隔离必然互见）
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        clientA.DrainEvents();
        clientB.DrainEvents();
        const auto [seenAofB, seenBofA] = RunOnWorldIo(servers.worldService, [&]() {
            auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
            auto b = servers.world->FindPlayerByCharacter(seedB.characterId);
            return std::pair<bool, bool>(
                a && a->VisiblePlayers().count(seedB.characterId) != 0,
                b && b->VisiblePlayers().count(seedA.characterId) != 0);
        });
        Check("MapIsolationPlayerCheck: cross-map players never visible", !seenAofB && !seenBofA);
    }

    // ---- MapIsolationMonsterCheck（指令十/四十八）：怪物按地图隔离 ----
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        clientA.DrainEvents();
        clientB.DrainEvents();
        const std::size_t aSpawns = CountEventsOf(clientA, WorldNetworkEvent::Type::MonsterSpawn);
        const std::size_t bSpawns = CountEventsOf(clientB, WorldNetworkEvent::Type::MonsterSpawn);
        const auto [aOnMap2, bOnMap1] = RunOnWorldIo(servers.worldService, [&]() {
            auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
            auto b = servers.world->FindPlayerByCharacter(seedB.characterId);
            int a2 = 0;
            int b1 = 0;
            if (a) {
                for (const auto& entityId : a->VisibleMonsters()) {
                    auto monster = servers.world->FindMonster(entityId);
                    if (monster && monster->MapId() == 2) {
                        ++a2;
                    }
                }
            }
            if (b) {
                for (const auto& entityId : b->VisibleMonsters()) {
                    auto monster = servers.world->FindMonster(entityId);
                    if (monster && monster->MapId() == 1) {
                        ++b1;
                    }
                }
            }
            return std::pair<int, int>(a2, b1);
        });
        Check("MapIsolationMonsterCheck: Map1 sees legacy slimes only, Map2 sees Map2 slimes only",
              aSpawns > 0 && bSpawns > 0 && aOnMap2 == 0 && bOnMap1 == 0);
    }

    // ---- MapIsolationNpcCheck（指令十二）：NPC 全部在 Map1，Map2 不可见 ----
    {
        const std::size_t aNpcSpawns = CountEventsOf(clientA, WorldNetworkEvent::Type::NpcSpawnEvent);
        const std::size_t bNpcSpawns = CountEventsOf(clientB, WorldNetworkEvent::Type::NpcSpawnEvent);
        Check("MapIsolationNpcCheck: Map1 sees 4 NPCs, Map2 sees none",
              aNpcSpawns == 4 && bNpcSpawns == 0);
    }

    // ---- MapIsolationDropCheck（指令十三）：Drop 只存在于所属地图 ----
    {
        servers.world->TestSpawnDrop(200.0f, 520.0f, 1, 0, kItemSlimeCoreId); // Map1 的掉落
        WorldNetworkEvent aSpawn;
        const bool aSaw = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent, 0,
            [](const WorldNetworkEvent& e) { return e.itemDefinitionId == kItemSlimeCoreId; },
            aSpawn, 3000);
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        clientB.DrainEvents();
        WorldNetworkEvent bSpawnProbe;
        const bool bSaw = FindRecordedFrom(clientB, WorldNetworkEvent::Type::WorldItemSpawnEvent, 0,
                                           [](const WorldNetworkEvent& e) {
                                               return e.itemDefinitionId == kItemSlimeCoreId;
                                           },
                                           bSpawnProbe);
        const std::uint64_t dropId = RunOnWorldIo(servers.worldService, [&]() -> std::uint64_t {
            for (std::uint64_t id = servers.world->NextItemDropIdForTest(); id > 0; --id) {
                const auto* drop = servers.world->FindItemDrop(id - 1);
                if (drop != nullptr) {
                    return drop->dropEntityId;
                }
            }
            return 0;
        });
        clientB.controller.SendPickup(dropId);
        WorldNetworkEvent failEvent;
        const bool bPickupRejected = WaitRecordedFrom(
            clientB, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
            [](const WorldNetworkEvent& e) { return !e.success; }, failEvent, 3000);
        Check("MapIsolationDropCheck: Map1 drop invisible/unpickable from Map2",
              aSaw && !bSaw && bPickupRejected);
    }

    // ---- PortalSpawnCheck（指令十七/七十一）：AOI 内才 Spawn（含目标地图名）----
    {
        MovePlayerTo(clientA, servers, seedA.characterId, 950.0f, 300.0f);
        WorldNetworkEvent portalSpawn;
        const bool aGot = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::PortalSpawnEvent, 0,
            [](const WorldNetworkEvent& e) { return e.portalId == 8001; }, portalSpawn, 4000);
        Check("PortalSpawnCheck: Portal 8001 spawns to AOI player with destination name",
              aGot && portalSpawn.portalDestinationMapId == 2 &&
                  portalSpawn.portalDestinationName == "Slime Meadow");
    }    // ---- PortalLeaveAoiCheck（指令七十二）：走出 700 -> Despawn(LeftAOI) ----
    {
        MovePlayerTo(clientA, servers, seedA.characterId, 290.0f, 300.0f); // 距 8001 = 710 > 700
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        clientA.DrainEvents();
        WorldNetworkEvent despawn;
        const bool left = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::PortalDespawnEvent, 0,
            [](const WorldNetworkEvent& e) { return e.portalDespawnReason == 1; }, despawn, 4000);
        Check("PortalLeaveAoiCheck: leaving 700 despawns portal (LeftAOI)", left);
    }

    // ---- PortalTooFarCheck（指令七十四/二十一）：可见但超出交互半径拒绝 ----
    {
        MovePlayerTo(clientA, servers, seedA.characterId, 880.0f, 300.0f); // 距 8001 = 120 > 100
        const auto portalEntity1 = RunOnWorldIo(servers.worldService, [&]() -> std::uint64_t {
            for (std::uint64_t id = 1; id <= 4; ++id) {
                const auto* portal = servers.world->FindPortal(id);
                if (portal != nullptr && portal->portalId == 8001) {
                    return id;
                }
            }
            return 0;
        });
        // 等 AOI tick 收录（服务端 visiblePortals）——防 NotVisible 竞态。
        const bool visible = WaitUntil([&] {
            return RunOnWorldIo(servers.worldService, [&]() -> bool {
                auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
                return a && a->VisiblePortals().count(portalEntity1) != 0;
            });
        }, 3000);
        clientA.client().SendPortalUse(900001, portalEntity1);
        WorldNetworkEvent response;
        const bool rejected = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::PortalUseResponseEvent, 0,
            [](const WorldNetworkEvent& e) {
                return !e.portalUse.success &&
                       e.portalUse.resultCode ==
                           static_cast<std::uint8_t>(PortalResultCode::TooFar);
            },
            response, 4000);
        Check("PortalTooFarCheck: use beyond interaction radius rejected (TooFar)",
              visible && rejected);
    }

    // ---- PortalWrongMapCheck（指令七十五/二十一）：跨图 portalEntityId 拒绝 ----
    {
        const auto portalEntity1 = RunOnWorldIo(servers.worldService, [&]() -> std::uint64_t {
            for (std::uint64_t id = 1; id <= 4; ++id) {
                const auto* portal = servers.world->FindPortal(id);
                if (portal != nullptr && portal->portalId == 8001) {
                    return id;
                }
            }
            return 0;
        });
        clientB.client().SendPortalUse(900002, portalEntity1); // B 在 Map2 用 Map1 的门
        WorldNetworkEvent response;
        const bool rejected = WaitRecordedFrom(
            clientB, WorldNetworkEvent::Type::PortalUseResponseEvent, 0,
            [](const WorldNetworkEvent& e) {
                return !e.portalUse.success &&
                       e.portalUse.resultCode ==
                           static_cast<std::uint8_t>(PortalResultCode::WrongMap);
            },
            response, 4000);
        Check("PortalWrongMapCheck: cross-map portal use rejected (WrongMap)", rejected);
    }

    // ---- PortalInvisibleCheck（指令七十六/二十一）：同图未收录（不可见）拒绝 ----
    {
        // A 主动走出 700（距 8001 = 710），等服务端 Despawn 后再使用。
        MovePlayerTo(clientA, servers, seedA.characterId, 290.0f, 300.0f);
        const auto portalEntity1 = RunOnWorldIo(servers.worldService, [&]() -> std::uint64_t {
            for (std::uint64_t id = 1; id <= 4; ++id) {
                const auto* portal = servers.world->FindPortal(id);
                if (portal != nullptr && portal->portalId == 8001) {
                    return id;
                }
            }
            return 0;
        });
        const bool serverInvisible = WaitUntil([&] {
            return RunOnWorldIo(servers.worldService, [&]() -> bool {
                auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
                return a && a->VisiblePortals().count(portalEntity1) == 0;
            });
        }, 3000);
        clientA.client().SendPortalUse(900003, portalEntity1);
        WorldNetworkEvent response;
        const bool rejected = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::PortalUseResponseEvent, 0,
            [](const WorldNetworkEvent& e) {
                return !e.portalUse.success &&
                       e.portalUse.resultCode ==
                           static_cast<std::uint8_t>(PortalResultCode::NotVisible);
            },
            response, 4000);
        Check("PortalInvisibleCheck: not-in-visiblePortals use rejected (NotVisible)",
              serverInvisible && rejected);
    }

    // ---- PortalSuccessCheck + MapChangedCheck + OldAoiClearedCheck + NewAoiBuiltCheck
    //      （指令二十二~二十九/八十一/八十二/八十三）：8001 -> Map2 统一切换 ----
    {
        MovePlayerTo(clientA, servers, seedA.characterId, 950.0f, 300.0f);
        const std::size_t monsterBaselineA =
            CountEventsOf(clientA, WorldNetworkEvent::Type::MonsterSpawn);
        // 等 AOI tick 重新收录 portal（Invisible 检查把 A 移出了 700——回来后需重收录）。
        WaitUntil([&] {
            return RunOnWorldIo(servers.worldService, [&]() -> bool {
                auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
                return a && a->VisiblePortals().count(1) != 0;
            });
        }, 3000);
        clientA.client().SendPortalUse(900010, 1); // entity 1 = portal 8001
        WorldNetworkEvent response;
        const bool responseOk = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::PortalUseResponseEvent, 0,
            [](const WorldNetworkEvent& e) {
                return e.portalUse.success &&
                       e.portalUse.destinationMapId == 2 &&
                       e.portalUse.destinationX == 200.0f && e.portalUse.destinationY == 500.0f;
            },
            response, 5000);
        WorldNetworkEvent changed;
        const bool changedOk = WaitRecordedFrom(
                        clientA, WorldNetworkEvent::Type::MapChangedEvent, 0,
                        [](const WorldNetworkEvent& e) {
                            return e.mapChanged.mapId == 2 &&
                                   e.mapChanged.mapName == "Slime Meadow" &&
                                   e.mapChanged.x == 200.0f && e.mapChanged.y == 500.0f;
                        },
                        changed, 5000);
        // Map2 无 NPC：visibleNpcs 全清（OldAoiCleared，指令八十一）。
        const auto [npcCount, map1Monsters, map2Monsters] =
            RunOnWorldIo(servers.worldService, [&]() {
                auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
                int old = 0;
                int fresh = 0;
                if (a) {
                    for (const auto& entityId : a->VisibleMonsters()) {
                        auto monster = servers.world->FindMonster(entityId);
                        if (monster && monster->MapId() == 1) {
                            ++old;
                        }
                        if (monster && monster->MapId() == 2) {
                            ++fresh;
                        }
                    }
                    return std::tuple<std::size_t, int, int>(a->VisibleNpcs().size(), old, fresh);
                }
                return std::tuple<std::size_t, int, int>(std::size_t{0}, -1, -1);
            });
        // NewAoiBuilt（指令八十二）：客户端收到 Map2 的怪物 Spawn（>0 且全部在切图后）。
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        clientA.DrainEvents();
        const std::size_t newSpawns =
            CountEventsOf(clientA, WorldNetworkEvent::Type::MonsterSpawn) - monsterBaselineA;
        std::printf("[Diag] PortalSuccess: resp=%d changed=%d npcCount=%zu map1=%d map2=%d "
                    "newSpawns=%zu\n",
                    responseOk ? 1 : 0, changedOk ? 1 : 0, npcCount, map1Monsters,
                    map2Monsters, newSpawns);
        clientA.DrainEvents();
        {
            const int respIdx =
                WorldTestClient::IndexOf(WorldNetworkEvent::Type::PortalUseResponseEvent);
            std::printf("[Diag] PortalRespDump: idx=%d recorded=%zu count=%zu\n", respIdx,
                        clientA.recorded[respIdx].size(),
                        static_cast<std::size_t>(clientA.counts[respIdx]));
        }
        for (const auto& r : clientA.recorded[WorldTestClient::IndexOf(
                 WorldNetworkEvent::Type::PortalUseResponseEvent)]) {
            std::printf("[Diag] PortalUseResp: req=%llu success=%d code=%u destMap=%u "
                        "dest=(%.0f,%.0f) cost=%u\n",
                        static_cast<unsigned long long>(r.portalUse.requestId),
                        r.portalUse.success ? 1 : 0,
                        static_cast<unsigned>(r.portalUse.resultCode),
                        static_cast<unsigned>(r.portalUse.destinationMapId),
                        r.portalUse.destinationX, r.portalUse.destinationY,
                        static_cast<unsigned>(r.portalUse.goldCost));
        }
        const bool ok = responseOk && changedOk;
        Check("PortalSuccessCheck/MapChangedCheck/OldAoiClearedCheck/NewAoiBuiltCheck: "
              "unified transition to Map2 with clean AOI rebuild",
              ok && npcCount == 0 && map1Monsters == 0 && map2Monsters > 0 && newSpawns > 0);
    }

    // ---- MapPersistenceCheck（指令二十八/八十四）：断线重登仍在 Map2/原位置 ----
    {
        clientA.Disconnect();
        // 等服务器真正移除会话（防"character already online"重连竞态）。
        WaitUntil([&] {
            return RunOnWorldIo(servers.worldService, [&]() -> bool {
                return servers.world->FindPlayerByCharacter(seedA.characterId) == nullptr;
            });
        }, 3000);
        const std::string ticket = TicketFor(servers.login, seedA);
        const bool reentered = clientA.ConnectAndEnter(ticket, 8000);
        clientA.DrainEvents();
        servers.world->TestBuffPlayerHp(seedA.characterId, 1000000); // 重进后重加
        const auto [mapId, x, y] = RunOnWorldIo(servers.worldService, [&]() {
            auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
            return std::tuple<std::uint16_t, float, float>(
                a ? a->MapId() : 0, a ? a->PositionX() : -1.0f, a ? a->PositionY() : -1.0f);
        });
        Check("MapPersistenceCheck: reconnect restores Map2 + persisted position",
              reentered && mapId == 2 && std::fabs(x - 200.0f) < 1.0f &&
                  std::fabs(y - 500.0f) < 1.0f);
    }

    // ---- PortalDuplicateCheck（指令五十九/八十）：同 requestId 只执行一次 ----
    {
        // A 在 Map2 (200,500)，Portal 8002 (150,500) 距离 50，免费，Level1。
        MovePlayerTo(clientA, servers, seedA.characterId, 160.0f, 500.0f);
        const auto portalEntity2 = RunOnWorldIo(servers.worldService, [&]() -> std::uint64_t {
            for (std::uint64_t id = 1; id <= 4; ++id) {
                const auto* portal = servers.world->FindPortal(id);
                if (portal != nullptr && portal->portalId == 8002) {
                    return id;
                }
            }
            return 0;
        });
        // 等 AOI tick 收录（移动后需 tick 重收录——防 NotVisible 竞态）。
        WaitUntil([&] {
            return RunOnWorldIo(servers.worldService, [&]() -> bool {
                auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
                return a && a->VisiblePortals().count(portalEntity2) != 0;
            });
        }, 3000);
        const std::size_t changedBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::MapChangedEvent);
        clientA.client().SendPortalUse(900020, portalEntity2);
        clientA.client().SendPortalUse(900020, portalEntity2); // 原样重放
        WorldNetworkEvent dup;
        const bool dupRejected = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::PortalUseResponseEvent, 0,
            [](const WorldNetworkEvent& e) {
                return !e.portalUse.success &&
                       e.portalUse.resultCode ==
                           static_cast<std::uint8_t>(PortalResultCode::DuplicateRequest);
            },
            dup, 5000);
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        clientA.DrainEvents();
        const std::size_t changedCount =
            CountEventsOf(clientA, WorldNetworkEvent::Type::MapChangedEvent) - changedBaseline;
        const auto mapId = RunOnWorldIo(servers.worldService, [&]() -> std::uint16_t {
            auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
            return a ? a->MapId() : 0;
        });
        Check("PortalDuplicateCheck: replayed requestId -> DuplicateRequest, single transition",
              dupRejected && changedCount == 1 && mapId == 1);
    }

    // ---- PortalLevelCheck + PortalGoldCheck（指令七十八/七十九）：8003 等级/金币 ----
    {
        // A 现在在 Map1 (900,300)（8002 落点）。回 Map2 -> 8003（Level2 + 10G）。
        servers.world->TestTeleportPlayer(seedA.characterId, 2, 1700.0f, 1000.0f);
        WaitUntil([&] {
            auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
            return a && a->MapId() == 2;
        }, 4000);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        clientA.DrainEvents();
        const auto portalEntity3 = RunOnWorldIo(servers.worldService, [&]() -> std::uint64_t {
            for (std::uint64_t id = 1; id <= 4; ++id) {
                const auto* portal = servers.world->FindPortal(id);
                if (portal != nullptr && portal->portalId == 8003) {
                    return id;
                }
            }
            return 0;
        });
        clientA.client().SendPortalUse(900030, portalEntity3);
        WorldNetworkEvent levelFail;
        const bool levelRejected = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::PortalUseResponseEvent, 0,
            [](const WorldNetworkEvent& e) {
                return !e.portalUse.success &&
                       e.portalUse.resultCode ==
                           static_cast<std::uint8_t>(PortalResultCode::LevelTooLow);
            },
            levelFail, 4000);
        servers.world->TestSetPlayerLevel(seedA.characterId, 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        clientA.client().SendPortalUse(900031, portalEntity3);
        WorldNetworkEvent goldFail;
        const bool goldRejected = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::PortalUseResponseEvent, 0,
            [](const WorldNetworkEvent& e) {
                return !e.portalUse.success &&
                       e.portalUse.resultCode ==
                           static_cast<std::uint8_t>(PortalResultCode::NotEnoughGold);
            },
            goldFail, 4000);
        servers.world->TestSetPlayerGold(seedA.characterId, 100);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        clientA.client().SendPortalUse(900032, portalEntity3);
        WorldNetworkEvent success;
        const bool portalOk = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::PortalUseResponseEvent, 0,
            [](const WorldNetworkEvent& e) { return e.portalUse.success; }, success, 5000);
        WaitUntil([&] {
            auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
            return a && a->MapId() == 3;
        }, 4000);
        const auto [goldAfter, xAfter] = RunOnWorldIo(servers.worldService, [&]() {
            auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
            return std::pair<std::int64_t, float>(a ? a->Gold() : -1,
                                                  a ? a->PositionX() : -1.0f);
        });
        Check("PortalLevelCheck/PortalGoldCheck: 8003 gates Level2 + 10G, then Map3",
              levelRejected && goldRejected && portalOk && goldAfter == 90 &&
                  std::fabs(xAfter - 200.0f) < 1.0f);
    }

    // ---- MapBoundsCheck（指令八）：Map3 0~2400 x 0~1800 服务器钳制 ----
    {
        servers.world->TestTeleportPlayer(seedA.characterId, 3, 2390.0f, 1790.0f);
        WaitUntil([&] {
            auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
            return a && a->MapId() == 3;
        }, 4000);
        static std::uint32_t boundSeq = 400000;
        for (int i = 0; i < 12; ++i) {
            clientA.client().SendMoveInput(++boundSeq, 1.0f, 1.0f, 0.1f);
        }
        WaitUntil([&] {
            auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
            if (!a) {
                return false;
            }
            const bool xOk = a->PositionX() <= 2400.0f;
            const bool yOk = a->PositionY() <= 1800.0f;
            const bool settled = a->PositionX() >= 2395.0f || a->PositionY() >= 1785.0f;
            return xOk && yOk && settled;
        }, 3000);
        const auto [x, y] = RunOnWorldIo(servers.worldService, [&]() {
            auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
            return std::pair<float, float>(a ? a->PositionX() : -1.0f,
                                           a ? a->PositionY() : -1.0f);
        });
        Check("MapBoundsCheck: movement clamped to Map3 bounds (2400x1800)",
              x <= 2400.0f && y <= 1800.0f && x >= 2380.0f);
    }

    // ---- MapPersistenceCheck（重启）/WorldRestartMapPersistenceCheck（指令二十八/八十五）----
    {
        // A 已在 Map3 (200,300 附近)：断线 -> 重启世界 -> 重登 -> 仍 Map3。
        clientA.Disconnect();
        clientB.Disconnect();
        WaitUntil([&] {
            return RunOnWorldIo(servers.worldService, [&]() -> bool {
                return servers.world->FindPlayerByCharacter(seedA.characterId) == nullptr &&
                       servers.world->FindPlayerByCharacter(seedB.characterId) == nullptr;
            });
        }, 3000);
        servers.StopWorld();
        const bool restarted = servers.StartWorld();
        const std::string ticketA = TicketFor(servers.login, seedA);
        const bool reentered = restarted && clientA.ConnectAndEnter(ticketA, 8000);
        clientA.DrainEvents();
        servers.world->TestBuffPlayerHp(seedA.characterId, 1000000); // 重进后重加
        const auto mapIdA = RunOnWorldIo(servers.worldService, [&]() -> std::uint16_t {
            auto a = servers.world->FindPlayerByCharacter(seedA.characterId);
            return a ? a->MapId() : 0;
        });
        // B（Map2）同样重连恢复（重启前 B 在 Map2 (200,500)）。
        const std::string ticketB = TicketFor(servers.login, seedB);
        const bool reenteredB = clientB.ConnectAndEnter(ticketB, 8000);
        clientB.DrainEvents();
        servers.world->TestBuffPlayerHp(seedB.characterId, 1000000); // 重进后重加
        const auto mapIdB = RunOnWorldIo(servers.worldService, [&]() -> std::uint16_t {
            auto b = servers.world->FindPlayerByCharacter(seedB.characterId);
            return b ? b->MapId() : 0;
        });
        Check("WorldRestartMapPersistenceCheck: restart restores persisted Map3/Map2",
              reentered && reenteredB && mapIdA == 3 && mapIdB == 2);
    }

    // ---- InvalidPersistedMapCheck + InvalidPersistedPositionCheck（指令二十九/八十六/八十七）----
    {
        CharacterSeed seedC;
        CharacterSeed seedD;
        const bool seeded = SeedAtOnMap(db, accounts, characters, "map_user_c", "MapC", 1, 300.0f,
                                        300.0f, seedC) &&
                            SeedAtOnMap(db, accounts, characters, "map_user_d", "MapD", 1, 300.0f,
                                        300.0f, seedD);
        // C：非法 mapId=999；D：越界位置 99999,99999（mapId 合法=1）。
        (void)QueryScalar(servers.dbPath,
                          "UPDATE characters SET map_id = 999 WHERE id = " +
                              std::to_string(static_cast<long long>(seedC.characterId)) + ";");
        (void)QueryScalar(servers.dbPath,
                          "UPDATE characters SET position_x = 99999, position_y = 99999 WHERE id = " +
                              std::to_string(static_cast<long long>(seedD.characterId)) + ";");
        WorldTestClient clientC;
        WorldTestClient clientD;
        const bool entered = seeded &&
                             clientC.ConnectAndEnter(TicketFor(servers.login, seedC), 8000) &&
                             clientD.ConnectAndEnter(TicketFor(servers.login, seedD), 8000);
        clientC.DrainEvents();
        clientD.DrainEvents();
        const auto [mapC, xC, yC, mapD, xD, yD] = RunOnWorldIo(servers.worldService, [&]() {
            auto c = servers.world->FindPlayerByCharacter(seedC.characterId);
            auto d = servers.world->FindPlayerByCharacter(seedD.characterId);
            return std::tuple<std::uint16_t, float, float, std::uint16_t, float, float>(
                c ? c->MapId() : 0, c ? c->PositionX() : -1, c ? c->PositionY() : -1,
                d ? d->MapId() : 0, d ? d->PositionX() : -1, d ? d->PositionY() : -1);
        });
        Check("InvalidPersistedMapCheck/InvalidPersistedPositionCheck: invalid DB map/position "
              "corrected on enter",
              entered && mapC == 1 && std::fabs(xC - 300.0f) < 1.0f && std::fabs(yC - 300.0f) < 1.0f &&
                  mapD == 1 && std::fabs(xD - 300.0f) < 1.0f && std::fabs(yD - 300.0f) < 1.0f);

        // ---- 死亡/复活全链（用 C：Map1，金币 50）----
        servers.world->TestSetPlayerGold(seedC.characterId, 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        servers.world->TestMarkPlayerDead(seedC.characterId);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // DeathBlocksActionCheck（指令三十二/八十八）：Move/Attack/Skill/Portal/Pickup 均拒。
        {
            static std::uint32_t deadSeq = 500000;
            clientC.client().SendMoveInput(++deadSeq, 1.0f, 0.0f, 0.1f);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            const auto [movedX, aliveFlag] = RunOnWorldIo(servers.worldService, [&]() {
                auto c = servers.world->FindPlayerByCharacter(seedC.characterId);
                return std::pair<float, bool>(c ? c->PositionX() : -1.0f,
                                              c ? c->Alive() : true);
            });
            // 攻击/技能目标用 C 自己 visibleMonsters 里的怪（验证链在 Dead 之前
            // 还有目标可见性校验——不可见目标会先回 InvalidTarget 而非 Dead）。
            const std::uint64_t visibleMonster =
                RunOnWorldIo(servers.worldService, [&]() -> std::uint64_t {
                    auto c = servers.world->FindPlayerByCharacter(seedC.characterId);
                    if (!c || c->VisibleMonsters().empty()) {
                        return 0;
                    }
                    return *c->VisibleMonsters().begin();
                });
            clientC.client().SendAttack(910001, kTypeMonster, visibleMonster);
            WorldNetworkEvent attackFail;
            const bool attackDead = WaitRecordedFrom(
                clientC, WorldNetworkEvent::Type::AttackResponse, 0,
                [](const WorldNetworkEvent& e) {
                    return !e.success &&
                           e.resultCode == static_cast<std::uint8_t>(CombatResultCode::AttackerDead);
                },
                attackFail, 3000);
            clientC.client().SendSkillCast(910002, kSkillIdQuickStrike, kTypeMonster,
                                           visibleMonster);
            WorldNetworkEvent skillFail;
            const bool skillDead = WaitRecordedFrom(
                clientC, WorldNetworkEvent::Type::SkillCastResponseEvent, 0,
                [](const WorldNetworkEvent& e) {
                    return !e.accepted &&
                           e.skillResultCode ==
                               static_cast<std::uint8_t>(SkillResultCode::CasterDead);
                },
                skillFail, 3000);
            clientC.client().SendPortalUse(910003, 1);
            WorldNetworkEvent portalFail;
            const bool portalDead = WaitRecordedFrom(
                clientC, WorldNetworkEvent::Type::PortalUseResponseEvent, 0,
                [](const WorldNetworkEvent& e) {
                    return !e.portalUse.success &&
                           e.portalUse.resultCode ==
                               static_cast<std::uint8_t>(PortalResultCode::Dead);
                },
                portalFail, 3000);
            clientC.controller.SendPickup(1);
            WorldNetworkEvent pickupFail;
            const bool pickupDead = WaitRecordedFrom(
                clientC, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
                [](const WorldNetworkEvent& e) {
                    return !e.success &&
                           e.itemResultCode == static_cast<std::uint8_t>(ItemResultCode::Dead);
                },
                pickupFail, 3000);
            Check("DeathBlocksActionCheck: dead player cannot move/attack/cast/use portal/pickup",
                  !aliveFlag && std::fabs(movedX - 300.0f) < 1.0f && attackDead && skillDead &&
                      portalDead && pickupDead);
            std::printf("[Diag] DeathBlocks: move=%d attack=%d skill=%d portal=%d pickup=%d "
                        "visibleMonster=%llu\n",
                        std::fabs(movedX - 300.0f) < 1.0f ? 1 : 0, attackDead ? 1 : 0,
                        skillDead ? 1 : 0, portalDead ? 1 : 0, pickupDead ? 1 : 0,
                        static_cast<unsigned long long>(visibleMonster));
            clientC.DrainEvents();
            for (const auto& r : clientC.recorded[WorldTestClient::IndexOf(
                     WorldNetworkEvent::Type::SkillCastResponseEvent)]) {
                std::printf("[Diag] SkillResp: req=%llu accepted=%d code=%u\n",
                            static_cast<unsigned long long>(r.requestId), r.accepted ? 1 : 0,
                            static_cast<unsigned>(r.skillResultCode));
            }
        }

        // RespawnTooEarlyCheck（指令三十七/八十九）。重新 MarkDead 重置 3s 时钟
        //（DeathBlocks 的等待耗时不可控——服务器权威判断以 deadSince 为准）。
        {
            servers.world->TestMarkPlayerDead(seedC.characterId);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            clientC.client().SendRespawn(910010,
                                         static_cast<std::uint8_t>(RespawnMode::CurrentMap));
            WorldNetworkEvent early;
            const bool tooEarly = WaitRecordedFrom(
                clientC, WorldNetworkEvent::Type::RespawnResponseEvent, 0,
                [](const WorldNetworkEvent& e) {
                    return !e.respawn.success &&
                           e.respawn.resultCode ==
                               static_cast<std::uint8_t>(RespawnResultCode::TooEarly);
                },
                early, 3000);
            Check("RespawnTooEarlyCheck: respawn within 3s rejected (TooEarly)", tooEarly);
        }

        // RespawnCurrentMapCheck + Gold + Health/Mana + StatusClear（指令三十五/三十八/
        // 三十九/四十/九十/九十一/九十四/九十五/九十六）。
        {
            // 死亡期间施加 Burn（死亡不清的容器在复活时必须被清——指令四十）。
            servers.world->ApplyStatusToTarget(kTypePlayer, seedC.characterId, 2003, 1, kTypePlayer,
                                               seedC.characterId, 0u);
            std::this_thread::sleep_for(std::chrono::milliseconds(3400)); // 越过 3s 门槛
            clientC.client().SendRespawn(910011,
                                         static_cast<std::uint8_t>(RespawnMode::CurrentMap));
            WorldNetworkEvent respawned;
            bool ok = WaitRecordedFrom(
                clientC, WorldNetworkEvent::Type::PlayerRespawnedEvent, 0,
                [](const WorldNetworkEvent& e) {
                    return e.playerRespawned.mapId == 1 &&
                           std::fabs(e.playerRespawned.x - 300.0f) < 1.0f &&
                           e.playerRespawned.hp == e.playerRespawned.maxHp &&
                           e.playerRespawned.mana == e.playerRespawned.maxMana;
                },
                respawned, 5000);
            WorldNetworkEvent response;
            ok = ok && WaitRecordedFrom(
                            clientC, WorldNetworkEvent::Type::RespawnResponseEvent, 0,
                            [](const WorldNetworkEvent& e) { return e.respawn.success; },
                            response, 3000);
            const auto [goldAfter, statusCount, aliveNow] =
                RunOnWorldIo(servers.worldService, [&]() {
                    auto c = servers.world->FindPlayerByCharacter(seedC.characterId);
                    return std::tuple<std::int64_t, std::size_t, bool>(
                        c ? c->Gold() : -1,
                        c ? c->StatusEffects().All().size() : std::size_t{99}, c && c->Alive());
                });
            std::printf("[Diag] CurrentMap: gold=%lld status=%zu alive=%d pos=(%.0f,%.0f)\n",
                        static_cast<long long>(goldAfter), statusCount, aliveNow ? 1 : 0,
                        0.0, 0.0);
            clientC.DrainEvents();
            for (const auto& r : clientC.recorded[WorldTestClient::IndexOf(
                     WorldNetworkEvent::Type::RespawnResponseEvent)]) {
                std::printf("[Diag] RespawnResp: req=%llu success=%d code=%u map=%u gold=%lld\n",
                            static_cast<unsigned long long>(r.respawn.requestId),
                            r.respawn.success ? 1 : 0, static_cast<unsigned>(r.respawn.resultCode),
                            static_cast<unsigned>(r.respawn.mapId),
                            static_cast<long long>(r.respawn.newGold));
            }
            for (const auto& r : clientC.recorded[WorldTestClient::IndexOf(
                     WorldNetworkEvent::Type::PlayerRespawnedEvent)]) {
                std::printf("[Diag] PlayerRespawned: map=%u x=%.1f hp=%u/%u mana=%u/%u\n",
                            static_cast<unsigned>(r.playerRespawned.mapId), r.playerRespawned.x,
                            r.playerRespawned.hp, r.playerRespawned.maxHp, r.playerRespawned.mana,
                            r.playerRespawned.maxMana);
            }
            Check("RespawnCurrentMapCheck/Gold/Health/Mana/StatusClear: 10G cost, full restore, "
                  "status container empty",
                  ok && goldAfter == 40 && statusCount == 0 && aliveNow);
        }

        // RespawnProtectionCheck + CancelOnAttack（指令四十五~四十七/九十七/九十八）。
        {
            const std::uint64_t map1Slime = FindMonsterOnMap(servers, 1);
            servers.world->TestBuffPlayerHp(seedC.characterId, 1000000); // 防护期后不被秒杀
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            servers.world->MoveMonsterTo(map1Slime, 315.0f, 300.0f); // 贴脸
            // 保护期 3s 内：怪物攻击无伤害（HP 不掉）。
            std::this_thread::sleep_for(std::chrono::milliseconds(2500));
            const auto [hpProtected, slimeHp1, protectedFlag] =
                RunOnWorldIo(servers.worldService, [&]() {
                    auto c = servers.world->FindPlayerByCharacter(seedC.characterId);
                    auto slime = servers.world->FindMonster(map1Slime);
                    return std::tuple<std::uint32_t, std::uint32_t, bool>(
                        c ? c->CurrentHp() : 0, slime ? slime->CurrentHp() : 0,
                        c && c->IsRespawnProtected(std::chrono::steady_clock::now()));
                });
            // F 攻击：保护立即失效（指令四十六）；随后怪物可伤害 C。
            clientC.client().SendAttack(910020, kTypeMonster, map1Slime);
            std::this_thread::sleep_for(std::chrono::milliseconds(2500));
            const auto [hpAfter, slimeHp2] = RunOnWorldIo(servers.worldService, [&]() {
                auto c = servers.world->FindPlayerByCharacter(seedC.characterId);
                auto slime = servers.world->FindMonster(map1Slime);
                return std::pair<std::uint32_t, std::uint32_t>(
                    c ? c->CurrentHp() : 0, slime ? slime->CurrentHp() : 0);
            });
            Check("RespawnProtectionCheck/CancelOnAttackCheck: monster damage blocked in 3s "
                  "window, cancelled by player attack",
                  hpProtected == 1000000 && slimeHp2 < slimeHp1 && hpAfter < hpProtected);
            std::printf("[Diag] Protection: hpProtected=%u slimeHp1=%u hpAfter=%u slimeHp2=%u "
                        "protectedFlag=%d\n",
                        hpProtected, slimeHp1, hpAfter, slimeHp2, protectedFlag ? 1 : 0);
        }

        // MonsterTargetLostOnMapChangeCheck（指令四十八/四十九/一百）：切图失去目标。
        //（C 复活后活着 @Map1(300,300)；怪物贴脸 Chase C -> C 切图 -> 目标丢失。）
        {
            const std::uint64_t map1Slime = FindMonsterOnMap(servers, 1);
            servers.world->MoveMonsterTo(map1Slime, 315.0f, 300.0f); // C(300,300) 贴脸
            const bool chased = WaitUntil([&] {
                auto slime = servers.world->FindMonster(map1Slime);
                return slime && slime->State() == MonsterState::Chase &&
                       slime->TargetCharacterId() == seedC.characterId;
            }, 5000);
            servers.world->TestTeleportPlayer(seedC.characterId, 2, 200.0f, 500.0f); // C 切图
            const bool lost = WaitUntil([&] {
                auto slime = servers.world->FindMonster(map1Slime);
                return slime && slime->TargetCharacterId() == 0 &&
                       (slime->State() == MonsterState::Returning ||
                        slime->State() == MonsterState::Idle);
            }, 4000);
            servers.world->MoveMonsterTo(map1Slime, 500.0f, 500.0f);
            Check("MonsterTargetLostOnMapChangeCheck: monster drops target on map change",
                  chased && lost);
        }

        // RespawnNoGoldCheck + RespawnTownCheck + RespawnDuplicateRequestCheck
        //（指令三十八/五十九/九十二/九十三/九十九）。
        {
            servers.world->TestSetPlayerGold(seedC.characterId, 5);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            servers.world->TestMarkPlayerDead(seedC.characterId);
            std::this_thread::sleep_for(std::chrono::milliseconds(3400));
            clientC.client().SendRespawn(910030,
                                         static_cast<std::uint8_t>(RespawnMode::CurrentMap));
            WorldNetworkEvent noGold;
            const bool currentRejected = WaitRecordedFrom(
                clientC, WorldNetworkEvent::Type::RespawnResponseEvent, 0,
                [](const WorldNetworkEvent& e) {
                    return !e.respawn.success &&
                           e.respawn.resultCode ==
                               static_cast<std::uint8_t>(RespawnResultCode::NotEnoughGold);
                },
                noGold, 4000);
            clientC.client().SendRespawn(910031, static_cast<std::uint8_t>(RespawnMode::Town));
            const std::size_t respawnedBaseline =
                CountEventsOf(clientC, WorldNetworkEvent::Type::PlayerRespawnedEvent);
            WorldNetworkEvent townOk;
            bool townRespawned = WaitRecordedFrom(
                clientC, WorldNetworkEvent::Type::PlayerRespawnedEvent, respawnedBaseline,
                [](const WorldNetworkEvent& e) {
                    return e.playerRespawned.mapId == 1 &&
                           std::fabs(e.playerRespawned.x - 300.0f) < 1.0f;
                },
                townOk, 5000);
            clientC.client().SendRespawn(910031, static_cast<std::uint8_t>(RespawnMode::Town));
            WorldNetworkEvent dupRespawn;
            const bool dupRejected = WaitRecordedFrom(
                clientC, WorldNetworkEvent::Type::RespawnResponseEvent, 0,
                [](const WorldNetworkEvent& e) {
                    return !e.respawn.success &&
                           e.respawn.resultCode ==
                               static_cast<std::uint8_t>(RespawnResultCode::DuplicateRequest);
                },
                dupRespawn, 4000);
            const auto goldAfter = RunOnWorldIo(servers.worldService, [&]() -> std::int64_t {
                auto c = servers.world->FindPlayerByCharacter(seedC.characterId);
                return c ? c->Gold() : -1;
            });
            Check("RespawnNoGoldCheck/RespawnTownCheck/RespawnDuplicateRequestCheck: "
                  "CurrentMap gated by gold, Town free to Map1(300,300), replay rejected",
                  currentRejected && townRespawned && dupRejected && goldAfter == 5);
            std::printf("[Diag] NoGold/Town/Dup: current=%d town=%d dup=%d gold=%lld\n",
                        currentRejected ? 1 : 0, townRespawned ? 1 : 0, dupRejected ? 1 : 0,
                        static_cast<long long>(goldAfter));
            clientC.Disconnect();
        }
    }

    // ---- NpcTeleportRegressionCheck（指令五十三/一百零二）：统一 MapTransitionService ----
    {
        // B 拉回 Map1 Wayfarer 旁 -> 7001 传送（同图，统一服务）。7001 需 20G。
        servers.world->TestSetPlayerGold(seedB.characterId, 100);
        servers.world->TestTeleportPlayer(seedB.characterId, 1, 560.0f, 300.0f);
        WaitUntil([&] {
            auto b = servers.world->FindPlayerByCharacter(seedB.characterId);
            return b && b->MapId() == 1;
        }, 4000);
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        clientB.DrainEvents();
        static std::uint64_t s_interactId = 800000;
        std::printf("[Diag] NpcReg pre-send: ready=%d map=%u\n",
                    clientB.controller.Client().State() ==
                            legend::client::WorldFlowState::WorldReady
                        ? 1
                        : 0,
                    0u);
        clientB.client().SendNpcInteract(++s_interactId, 3); // entity 3 = Wayfarer 5003
        WorldNetworkEvent dialogue;
        bool ok = WaitRecordedFrom(
            clientB, WorldNetworkEvent::Type::NpcInteractResponseEvent, 0,
            [](const WorldNetworkEvent& e) { return e.success && e.dialogueSessionId != 0; },
            dialogue, 5000);
        if (!ok) {
            // 诊断：列出收到的 NpcInteractResponse 结果码。
            clientB.DrainEvents();
            const auto& responses = clientB.recorded[WorldTestClient::IndexOf(
                WorldNetworkEvent::Type::NpcInteractResponseEvent)];
            for (const auto& r : responses) {
                std::printf("[Diag] NpcReg interact resp: success=%d code=%u session=%llu\n",
                            r.success ? 1 : 0, static_cast<unsigned>(r.resultCode),
                            static_cast<unsigned long long>(r.dialogueSessionId));
            }
        }
        if (ok) {
            // 从 DialoguePayload 里找 Teleport 7001 的 optionId（菜单动态生成）。
            clientB.DrainEvents();
            std::uint32_t teleportOptionId = 0;
            const auto& payloads = clientB.recorded[WorldTestClient::IndexOf(
                WorldNetworkEvent::Type::DialoguePayloadEvent)];
            for (auto it = payloads.rbegin(); it != payloads.rend(); ++it) {
                if (it->dialoguePayload.dialogueSessionId != dialogue.dialogueSessionId) {
                    continue;
                }
                for (const auto& option : it->dialoguePayload.options) {
                    std::printf("[Diag] NpcReg option: id=%u type=%u ref=%u label=%s\n",
                                static_cast<unsigned>(option.optionId),
                                static_cast<unsigned>(option.type),
                                static_cast<unsigned>(option.referenceId),
                                option.label.c_str());
                    if (option.referenceId == 7001) {
                        teleportOptionId = option.optionId;
                    }
                }
                if (teleportOptionId != 0) {
                    break;
                }
            }
            clientB.client().SendDialogueOption(++s_interactId, dialogue.dialogueSessionId,
                                                teleportOptionId);
            WorldNetworkEvent teleport;
            ok = WaitRecordedFrom(
                clientB, WorldNetworkEvent::Type::TeleportResponseEvent, 0,
                [](const WorldNetworkEvent& e) {
                    return e.teleport.success && std::fabs(e.teleport.x - 1500.0f) < 1.0f &&
                           std::fabs(e.teleport.y - 1500.0f) < 1.0f;
                },
                teleport, 5000);
        }
        Check("NpcTeleportRegressionCheck: NPC teleport works via unified MapTransitionService",
              ok);
    }

    clientA.Disconnect();
    clientB.Disconnect();
    servers.StopAll();
}

// RunWorldMapChecks 由 WorldChecks.cpp 调用（阶段21）。
void RunWorldMapChecks() {
    std::printf("[WorldMap] logic checks begin\n");
    RunWorldMapLogicChecks();
    std::printf("[WorldMap] chain checks begin\n");
    RunWorldMapChainChecks();
    std::printf("[WorldMap] checks complete (failures so far = %d)\n", g_failures);
}

} // namespace worldtest
