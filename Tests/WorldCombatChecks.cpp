// ---------------------------------------------------------------------------
// 阶段14 指令九十六~一百三十：服务器权威战斗与伤害检查。
// 仍链接 LegendWorldTests（不新增测试 exe，指令九十六）。
// 纯逻辑（DamageCalculator/ValidateAttack/协议畸形/Remote HP/事件乱序）无需服务器；
// 真实链路由 RunWorldCombatChecks() 自管 servers（固定表 entityId 1..20 对应表序）。
// 端口/DB 与阶段12/13 相同（WorldTestHarness：17240/17241/17242、testdata/world_test_<pid>）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Server/WorldServer/Combat/CombatService.h"
#include "Server/WorldServer/Combat/DamageCalculator.h"
#include "Server/WorldServer/Monster/MonsterDefinition.h"
#include "Server/WorldServer/Monster/MonsterEntity.h"
#include "Server/WorldServer/PlayerSession.h"
#include "Shared/Combat/CombatProtocol.h"
#include "Shared/Combat/CombatTypes.h"
#include "Shared/Monster/MonsterTypes.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace worldtest {

namespace {

using legend::world::AttackContext;
using legend::world::CalculateDamage;
using legend::world::CombatEntityType;
using legend::world::CombatEventPayload;
using legend::world::CombatResultCode;
using legend::world::FindMonsterDefinition;
using legend::world::IsAttackOffCooldown;
using legend::world::MonsterState;
using legend::world::PlayerSession;
using legend::world::ValidateAttack;
using RemoteMonsterEntityT = legend::client::RemoteMonsterEntity;
using RemoteMonsterManagerT = legend::client::RemoteMonsterManager;
using RemotePlayerEntityT = legend::client::RemotePlayerEntity;
using RemotePlayerManagerT = legend::client::RemotePlayerManager;
namespace CharacterRepository = legend::account::CharacterRepository;

// 固定表序 -> entityId（SpawnInitialMonsters 按表序分配 1..20）：
// 1..4 = (500,500)(700,500)(500,700)(700,700) 簇1；
// 5..8 = (1000,500)(1200,500)(1000,700)(1200,700) 簇2；
// 13/14 = (500,1500)(700,1500)；17..20 = (1500,1500)(1700,1500)(1500,1700)(1700,1700) 簇5。
constexpr std::uint64_t kSlime3 = 3;   // (500,700) —— A 的攻击目标
constexpr std::uint64_t kSlime4 = 4;   // (700,700) —— OutOfRange/MultiClient 目标
constexpr std::uint64_t kSlime13 = 13; // (500,1500) —— 不可见目标
constexpr std::uint64_t kSlime17 = 17; // (1500,1500) —— D 场景
constexpr std::uint64_t kSlime5 = 5;   // (1000,500) —— H 断线场景

const std::uint8_t kTypePlayer = static_cast<std::uint8_t>(CombatEntityType::Player);
const std::uint8_t kTypeMonster = static_cast<std::uint8_t>(CombatEntityType::Monster);

// ---- Combat 事件辅助 ----

int CountCombatEvents(const WorldTestClient& client, std::uint64_t attackerId,
                      std::uint64_t targetId) {
    int n = 0;
    for (const auto& e :
         client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::CombatEvent)]) {
        if (e.attackerId == attackerId && e.targetId == targetId) {
            ++n;
        }
    }
    return n;
}

int CountCombatEventsAgainstPlayer(const WorldTestClient& client, std::uint64_t victimId) {
    int n = 0;
    for (const auto& e :
         client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::CombatEvent)]) {
        if (e.targetType == kTypePlayer && e.targetId == victimId) {
            ++n;
        }
    }
    return n;
}

const WorldNetworkEvent* LastCombatEventFor(const WorldTestClient& client,
                                            std::uint64_t attackerId, std::uint64_t targetId) {
    const auto& events =
        client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::CombatEvent)];
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        if (it->attackerId == attackerId && it->targetId == targetId) {
            return &*it;
        }
    }
    return nullptr;
}

int CountMonsterDeathsFor(const WorldTestClient& client, std::uint64_t entityId) {
    int n = 0;
    for (const auto& e :
         client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::MonsterDeath)]) {
        if (e.monsterEntityId == entityId) {
            ++n;
        }
    }
    return n;
}

int CountMonsterRemovedDespawnsFor(const WorldTestClient& client, std::uint64_t entityId) {
    int n = 0;
    for (const auto& e :
         client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::MonsterDespawn)]) {
        if (e.monsterEntityId == entityId &&
            e.despawnReason ==
                static_cast<std::uint8_t>(legend::world::MonsterDespawnReason::Removed)) {
            ++n;
        }
    }
    return n;
}

bool LastMonsterBatchEntry(const WorldTestClient& client, std::uint64_t entityId,
                           legend::world::MonsterSnapshotEntry& out) {
    const auto& batches =
        client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::MonsterBatchSnapshot)];
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

// 攻击响应：等待指定 requestId 的 AttackResponse（expectSuccess>=0 时按
// success 过滤，避免拿到同 requestId 的旧条目）。
bool WaitAttackResponse(WorldTestClient& client, std::uint64_t requestId,
                        WorldNetworkEvent& out, int timeoutMs = 3000, int expectSuccess = -1) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            const auto& events =
                client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::AttackResponse)];
            for (auto it = events.rbegin(); it != events.rend(); ++it) {
                if (it->requestId == requestId &&
                    (expectSuccess < 0 || static_cast<int>(it->success ? 1 : 0) == expectSuccess)) {
                    out = *it;
                    return true;
                }
            }
            return false;
        },
        timeoutMs);
}

// 等待攻击者对目标的下一条 CombatEvent（expectedHpAfter 过滤旧伤害，
// hpAfter==UINT32_MAX 表示不过滤）。
bool WaitCombatEvent(WorldTestClient& client, std::uint64_t attackerId, std::uint64_t targetId,
                     WorldNetworkEvent& out, int timeoutMs = 3000,
                     std::uint32_t expectedHpAfter = 0xFFFFFFFFu) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            const auto& events =
                client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::CombatEvent)];
            for (auto it = events.rbegin(); it != events.rend(); ++it) {
                if (it->attackerId == attackerId && it->targetId == targetId &&
                    (expectedHpAfter == 0xFFFFFFFFu || it->targetHpAfter == expectedHpAfter)) {
                    out = *it;
                    return true;
                }
            }
            return false;
        },
        timeoutMs);
}

// ===========================================================================
// A. 纯逻辑检查（无服务器）
// ===========================================================================

void RunCombatLogicChecks() {
    // ---- DamageFormulaCheck（指令九十七/二十四/二十五/二十六/三十六）----
    {
        bool ok = CalculateDamage(20, 2) == 18; // 指令九十七：20-2=18
        ok = ok && CalculateDamage(10, 5) == 5; // 指令九十七：10-5=5
        ok = ok && CalculateDamage(1, 100) == 1; // 指令九十七：低攻击保底 1
        ok = ok && CalculateDamage(5, 5) == 1;   // 相等也保底 1
        Check("DamageFormulaCheck: 20-2=18, 10-5=5, 1-100=1 (max floor)", ok);
    }

    // ---- CombatValidateAttackCheck（指令三十七/一百二十）----
    {
        const auto now = std::chrono::steady_clock::now();
        AttackContext ctx;
        ctx.now = now;
        ctx.lastAttackTime = now - std::chrono::milliseconds(2000);
        ctx.attackCooldownSeconds = 0.8f;
        ctx.attackRangeSquared = 100.0f * 100.0f;
        ctx.distanceSquared = 50.0f * 50.0f;
        Check("CombatValidateAttackCheck: valid attack -> Success",
              ValidateAttack(ctx) == CombatResultCode::Success);
        ctx.attackerAlive = false;
        Check("CombatValidateAttackCheck: dead attacker -> AttackerDead",
              ValidateAttack(ctx) == CombatResultCode::AttackerDead);
        ctx.attackerAlive = true;
        ctx.attackerInWorld = false;
        Check("CombatValidateAttackCheck: not in world -> NotInWorld",
              ValidateAttack(ctx) == CombatResultCode::NotInWorld);
        ctx.attackerInWorld = true;
        ctx.targetAlive = false;
        Check("CombatValidateAttackCheck: dead target -> TargetDead",
              ValidateAttack(ctx) == CombatResultCode::TargetDead);
        ctx.targetAlive = true;
        ctx.targetVisible = false;
        Check("CombatValidateAttackCheck: invisible target -> InvalidTarget",
              ValidateAttack(ctx) == CombatResultCode::InvalidTarget);
        ctx.targetVisible = true;
        ctx.sameMap = false;
        // 指令一百二十：DifferentMap 真实链路无法构造（阶段14 只有 map1），纯逻辑覆盖。
        Check("DifferentMapCombatCheck: cross-map attack rejected",
              ValidateAttack(ctx) == CombatResultCode::DifferentMap);
        ctx.sameMap = true;
        ctx.distanceSquared = 150.0f * 150.0f;
        Check("CombatValidateAttackCheck: beyond range -> OutOfRange",
              ValidateAttack(ctx) == CombatResultCode::OutOfRange);
        ctx.distanceSquared = 50.0f * 50.0f;
        ctx.lastAttackTime = now - std::chrono::milliseconds(200);
        Check("CombatValidateAttackCheck: too fast -> Cooldown",
              ValidateAttack(ctx) == CombatResultCode::Cooldown);
    }

    // ---- PlayerCombatStatsCheck（指令四）----
    {
        PlayerSession player(0, 1, 2, "Stats", 1, 1, 1, 1, 0.0f, 0.0f);
        bool ok = player.MaxHp() == 100 && player.CurrentHp() == 100 &&
                  player.AttackPower() == 20 && player.Defense() == 5 &&
                  player.AttackRange() == 100.0f && player.AttackCooldownSeconds() == 0.8f &&
                  player.Alive();
        // 指令二十四：扣血 + 致死
        ok = ok && !player.ApplyDamage(30) && player.CurrentHp() == 70;
        ok = ok && player.ApplyDamage(100) && !player.Alive() && player.CurrentHp() == 0;
        Check("PlayerCombatStatsCheck: default 100/100/20/5/100/0.8s + damage/death", ok);
    }

    // ---- MonsterCombatStatsCheck（指令五/六）----
    {
        const auto* def = FindMonsterDefinition(legend::world::kTrainingSlimeTypeId);
        bool ok = def != nullptr && def->maxHp == 80 && def->attackPower == 10 &&
                  def->defense == 2 && def->attackRange == 60.0f &&
                  def->attackCooldownSeconds == 1.2f;
        legend::world::MonsterEntity slime(1, 1u, 1u, 0.0f, 0.0f, 80.0f);
        slime.InitializeCombat(def->maxHp);
        ok = ok && slime.CurrentHp() == 80 && slime.MaxHp() == 80 && slime.Alive();
        ok = ok && !slime.ApplyDamage(18) && slime.CurrentHp() == 62;
        ok = ok && slime.Alive(); // 未致死仍存活
        Check("MonsterCombatStatsCheck: Slime 80/10/2/60/1.2s + entity HP", ok);
    }

    // ---- CombatProtocolRoundtripCheck（指令八十二/九十三/九十四）----
    {
        std::string error;
        bool ok = true;
        {
            world::PlayerAttackRequestPayload req;
            req.requestId = 77;
            req.targetEntityType = kTypeMonster;
            req.targetEntityId = 9;
            std::vector<std::uint8_t> payload;
            world::PlayerAttackRequestPayload decoded;
            ok = ok && world::EncodePlayerAttackRequest(req, payload) &&
                 world::DecodePlayerAttackRequest(payload.data(), payload.size(), decoded, error) &&
                 decoded.requestId == 77 && decoded.targetEntityType == kTypeMonster &&
                 decoded.targetEntityId == 9;
        }
        {
            world::PlayerAttackResponsePayload resp;
            resp.requestId = 78;
            resp.success = false;
            resp.resultCode = static_cast<std::uint8_t>(CombatResultCode::OutOfRange);
            resp.targetEntityId = 9;
            resp.message = "OutOfRange";
            std::vector<std::uint8_t> payload;
            world::PlayerAttackResponsePayload decoded;
            ok = ok && world::EncodePlayerAttackResponse(resp, payload) &&
                 world::DecodePlayerAttackResponse(payload.data(), payload.size(), decoded,
                                                   error) &&
                 decoded.requestId == 78 && !decoded.success && decoded.message == "OutOfRange";
        }
        {
            CombatEventPayload ev;
            ev.eventId = 1001;
            ev.attackerType = kTypePlayer;
            ev.attackerId = 11;
            ev.targetType = kTypeMonster;
            ev.targetId = 3;
            ev.damage = 18;
            ev.targetHpAfter = 62;
            ev.targetMaxHp = 80;
            ev.killed = false;
            ev.serverTime = 555;
            std::vector<std::uint8_t> payload;
            CombatEventPayload decoded;
            ok = ok && world::EncodeCombatEvent(ev, payload) &&
                 world::DecodeCombatEvent(payload.data(), payload.size(), decoded, error) &&
                 decoded.eventId == 1001 && decoded.damage == 18 && decoded.targetHpAfter == 62 &&
                 decoded.targetMaxHp == 80 && !decoded.killed;
        }
        {
            world::EntityHealthSnapshotPayload snap;
            snap.entityType = kTypeMonster;
            snap.entityId = 3;
            snap.currentHp = 44;
            snap.maxHp = 80;
            snap.alive = true;
            snap.serverTime = 666;
            std::vector<std::uint8_t> payload;
            world::EntityHealthSnapshotPayload decoded;
            ok = ok && world::EncodeEntityHealthSnapshot(snap, payload) &&
                 world::DecodeEntityHealthSnapshot(payload.data(), payload.size(), decoded,
                                                   error) &&
                 decoded.entityId == 3 && decoded.currentHp == 44 && decoded.alive;
        }
        {
            world::MonsterDeathPayload death;
            death.entityId = 3;
            death.killerCharacterId = 11;
            death.serverTime = 777;
            std::vector<std::uint8_t> payload;
            world::MonsterDeathPayload decoded;
            ok = ok && world::EncodeMonsterDeath(death, payload) &&
                 world::DecodeMonsterDeath(payload.data(), payload.size(), decoded, error) &&
                 decoded.entityId == 3 && decoded.killerCharacterId == 11;
        }
        {
            world::PlayerDeathPayload death;
            death.characterId = 22;
            death.killerType = kTypeMonster;
            death.killerId = 3;
            death.serverTime = 888;
            std::vector<std::uint8_t> payload;
            world::PlayerDeathPayload decoded;
            ok = ok && world::EncodePlayerDeath(death, payload) &&
                 world::DecodePlayerDeath(payload.data(), payload.size(), decoded, error) &&
                 decoded.characterId == 22 && decoded.killerId == 3;
        }
        Check("CombatProtocolRoundtripCheck: 6 payloads roundtrip", ok);
    }

    // ---- MalformedAttackRequestCheck（指令八十二/一百二十一）----
    {
        world::PlayerAttackRequestPayload req;
        req.requestId = 1;
        req.targetEntityType = kTypeMonster;
        req.targetEntityId = 2;
        std::vector<std::uint8_t> payload;
        std::string error;
        const bool encoded = world::EncodePlayerAttackRequest(req, payload);
        std::vector<std::uint8_t> truncated(payload.begin(), payload.end() - 3);
        world::PlayerAttackRequestPayload decoded;
        bool ok = encoded &&
                  !world::DecodePlayerAttackRequest(truncated.data(), truncated.size(), decoded,
                                                    error);
        std::vector<std::uint8_t> trailing = payload;
        trailing.push_back(0xFF);
        ok = ok && !world::DecodePlayerAttackRequest(trailing.data(), trailing.size(), decoded,
                                                     error);
        Check("MalformedAttackRequestCheck: truncated/trailing rejected", ok);
    }

    // ---- MalformedCombatEventCheck（指令一百二十二）----
    {
        CombatEventPayload ev;
        ev.eventId = 1;
        std::vector<std::uint8_t> payload;
        std::string error;
        world::EncodeCombatEvent(ev, payload);
        std::vector<std::uint8_t> truncated(payload.begin(), payload.end() - 5);
        CombatEventPayload decoded;
        const bool ok =
            !world::DecodeCombatEvent(truncated.data(), truncated.size(), decoded, error);
        Check("MalformedCombatEventCheck: truncated rejected", ok);
    }

    // ---- MalformedHealthSnapshotCheck（指令一百二十三）----
    {
        world::EntityHealthSnapshotPayload snap;
        snap.entityId = 1;
        std::vector<std::uint8_t> payload;
        std::string error;
        world::EncodeEntityHealthSnapshot(snap, payload);
        std::vector<std::uint8_t> truncated(payload.begin(), payload.end() - 4);
        world::EntityHealthSnapshotPayload decoded;
        const bool ok =
            !world::DecodeEntityHealthSnapshot(truncated.data(), truncated.size(), decoded, error);
        Check("MalformedHealthSnapshotCheck: truncated rejected", ok);
    }

    // ---- CombatEventOrderingCheck（指令六十七/一百一十八）----
    {
        RemoteMonsterEntityT entity;
        world::MonsterSpawnPayload spawn;
        spawn.entityId = 3;
        spawn.currentHp = 80;
        spawn.maxHp = 80;
        entity.ApplySpawn(spawn);
        entity.ApplyCombatEvent(100, 62, false);
        bool ok = entity.CurrentHp() == 62 && entity.Alive();
        // 旧包（eventId 更小）忽略——HP 不回退
        entity.ApplyCombatEvent(99, 80, false);
        ok = ok && entity.CurrentHp() == 62;
        // 相同 eventId 也忽略
        entity.ApplyCombatEvent(100, 44, false);
        ok = ok && entity.CurrentHp() == 62;
        // 新包生效 + killed
        entity.ApplyCombatEvent(101, 0, true);
        ok = ok && entity.CurrentHp() == 0 && !entity.Alive();
        Check("CombatEventOrderingCheck: stale/equals eventId ignored, newer applied", ok);
    }

    // ---- RemoteHpUpdateCheck（指令六十三~六十五/七十二/七十三）----
    {
        RemoteMonsterManagerT monsters;
        world::MonsterSpawnPayload spawn;
        spawn.entityId = 5;
        spawn.currentHp = 80;
        spawn.maxHp = 80;
        monsters.HandleSpawn(spawn);
        monsters.HandleCombatEvent(1, kTypeMonster, 5, 62, false);
        bool ok = monsters.Find(5) != nullptr && monsters.Find(5)->CurrentHp() == 62;
        // Player 目标的 CombatEvent 不影响怪物
        monsters.HandleCombatEvent(2, kTypePlayer, 5, 0, true);
        ok = ok && monsters.Find(5)->CurrentHp() == 62;
        // MonsterDeath -> alive=false 但保留实体（直到 Despawn）
        monsters.HandleDeath(5);
        ok = ok && monsters.Find(5) != nullptr && !monsters.Find(5)->Alive();
        Check("RemoteMonsterHpUpdateCheck: combat event updates HP + death keeps entity", ok);

        RemotePlayerManagerT players;
        world::PlayerSpawnPayload playerSpawn;
        playerSpawn.characterId = 77;
        players.HandleSpawn(playerSpawn);
        players.HandleCombatEvent(kTypePlayer, 77, 95, 100, false);
        ok = players.Find(77) != nullptr && players.Find(77)->CurrentHp() == 95 &&
             players.Find(77)->Alive();
        players.HandleDeath(77);
        ok = ok && !players.Find(77)->Alive();
        // HealthSnapshot 纠偏
        players.ApplyHealthSnapshot(77, 100, 100, true);
        ok = ok && players.Find(77)->CurrentHp() == 100 && players.Find(77)->Alive();
        Check("RemotePlayerHpUpdateCheck: combat event + death + snapshot correction", ok);
    }

    // ---- MonsterAttackCooldownPureCheck（指令四十六）----
    {
        const auto now = std::chrono::steady_clock::now();
        Check("MonsterAttackCooldownPureCheck: cooldown gate uses steady_clock delta",
              !IsAttackOffCooldown(now, now - std::chrono::milliseconds(600), 1.2f) &&
                  IsAttackOffCooldown(now, now - std::chrono::milliseconds(1300), 1.2f));
    }
}

// ===========================================================================
// B. 真实链路检查（RunWorldCombatChecks 自管 servers）
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
    return login->Tickets().Create(seed.accountId, seed.characterId, 120.0);
}

// 等待服务器内玩家 HP 达到阈值（怪物攻击链路）。
bool WaitPlayerHpAtMost(WorldTestServers& servers, std::uint64_t characterId,
                        std::uint32_t maxHp, int timeoutMs) {
    return WaitUntil(
        [&] {
            auto player = servers.world->FindPlayerByCharacter(characterId);
            return player != nullptr && player->CurrentHp() <= maxHp;
        },
        timeoutMs);
}

// 瞬移玩家到目标点（按服务器权威位置计算差量，12 units/input；
// MoveInput 无绝对坐标——指令三十四，测试只复用客户端协议路径）。
// seq 全局递增：服务器 ApplyMoveInput 按 inputSequence 去重，重复 seq 会被拒绝。
bool TeleportPlayer(WorldTestClient& client, WorldTestServers& servers,
                    std::uint64_t characterId, float targetX, float targetY) {
    static std::uint32_t s_teleportSeq = 60000;
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
        const int steps =
            std::max(1, static_cast<int>(std::ceil(dist / 12.0f))); // 过冲 <=12 units
        for (int i = 0; i < steps; ++i) {
            client.client().SendMoveInput(++s_teleportSeq, ux, uy, 0.1f);
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
            return (ex * ex + ey * ey) <= 169.0f; // <=13 units（步进余量）
        },
        3000);
}

} // namespace

void RunWorldCombatChecks() {
    // A. 纯逻辑
    RunCombatLogicChecks();

    // B. 真实链路
    WorldTestServers servers;
    servers.dbPath = TempDbPath("world_combat");
    RemoveDb(servers.dbPath);
    Check("CombatServersStartCheck", servers.StartLogin() && servers.StartWorld());

    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(5, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("CombatChecks: db ready", false);
        servers.StopAll();
        return;
    }
    // ---- 布局：A 攻击者 / B 观察者 / F 远处 / D 围殴牺牲品 / H 断线场景 ----
    CharacterSeed seedA;
    CharacterSeed seedB;
    CharacterSeed seedF;
    CharacterSeed seedD;
    CharacterSeed seedH;
    bool seeded = true;
    seeded = seeded && SeedAt(db, accounts, characters, "cbt_user_a", "CbtA", 600.0f, 1060.0f, seedA);
    seeded = seeded && SeedAt(db, accounts, characters, "cbt_user_b", "CbtB", 620.0f, 1040.0f, seedB);
    seeded = seeded && SeedAt(db, accounts, characters, "cbt_user_f", "CbtF", 60.0f, 60.0f, seedF);
    seeded = seeded && SeedAt(db, accounts, characters, "cbt_user_d", "CbtD", 1600.0f, 1600.0f, seedD);
    seeded = seeded && SeedAt(db, accounts, characters, "cbt_user_h", "CbtH", 1100.0f, 600.0f, seedH);
    Check("CombatChecks: seeds ready", seeded);

    WorldTestClient clientD;
    WorldTestClient clientH;
    WorldTestClient clientB;
    WorldTestClient clientF;
    {
        // A 延迟进入（D 场景之后）——避免 A 在等待期间被巡逻怪 aggro 围殴致死。
        const std::string ticketD = TicketFor(servers.login, seedD);
        const bool enterD = !ticketD.empty() && clientD.ConnectAndEnter(ticketD, 8000);
        const std::string ticketH = TicketFor(servers.login, seedH);
        const bool enterH = !ticketH.empty() && clientH.ConnectAndEnter(ticketH, 8000);
        const std::string ticketB = TicketFor(servers.login, seedB);
        const bool enterB = !ticketB.empty() && clientB.ConnectAndEnter(ticketB, 8000);
        const std::string ticketF = TicketFor(servers.login, seedF);
        const bool enterF = !ticketF.empty() && clientF.ConnectAndEnter(ticketF, 8000);
        Check("CombatChecks: four clients entered", enterD && enterH && enterB && enterF);
    }

    // ---- MonsterAttackPlayerCheck（指令一百零五/二十六）：Slime 进入 60 自动攻击
    //      Player 100->95（服务器权威扣血 + CombatEvent）----
    WorldNetworkEvent firstHitOnD;
    bool firstHit = WaitCombatEvent(clientD, kSlime17, seedD.characterId, firstHitOnD, 12000);
    bool ok = firstHit && firstHitOnD.attackerType == kTypeMonster &&
              firstHitOnD.damage == 5 && firstHitOnD.targetHpAfter == 95 &&
              firstHitOnD.targetMaxHp == 100;
    ok = ok && WaitPlayerHpAtMost(servers, seedD.characterId, 95, 500);
    Check("MonsterAttackPlayerCheck: slime auto-attacks player 100->95", ok);

    // ---- MonsterAttackCooldownCheck（指令一百零六/四十六）：1.2s 内不能重复扣血 ----
    {
        // 第一跳后 1.0s 窗口内 victim 事件受 CD 限制（4 怪围攻各自 1.2s CD 错开，
        // 上限 ~4 跳 + 轮询误差余量；若 CD 失效会是 4*(1.0/0.2)=20 跳）。
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        clientD.DrainEvents();
        const int hits = CountCombatEventsAgainstPlayer(clientD, seedD.characterId);
        Check("MonsterAttackCooldownCheck: hit rate limited by 1.2s cooldown",
              hits >= 1 && hits <= 6);
    }

    // ---- MonsterStopsToAttackCheck（指令一百零七/四十三/四十四）：进入 60 后停止移动
    //      不穿过玩家（贴身距离稳定在攻击半径内且位置冻结）----
    {
        const bool closedIn = WaitUntil(
            [&] {
                auto monster = servers.world->FindMonster(kSlime17);
                auto player = servers.world->FindPlayerByCharacter(seedD.characterId);
                if (!monster || !player || monster->State() != MonsterState::Chase) {
                    return false;
                }
                const float dx = monster->PositionX() - player->PositionX();
                const float dy = monster->PositionY() - player->PositionY();
                return (dx * dx + dy * dy) <= 70.0f * 70.0f; // 攻击半径 60 + 判定余量
            },
            15000);
        // 贴身后位置稳定（不再向玩家中心穿越）
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        auto monster = servers.world->FindMonster(kSlime17);
        auto player = servers.world->FindPlayerByCharacter(seedD.characterId);
        bool stable = false;
        if (monster && player) {
            const float dx = monster->PositionX() - player->PositionX();
            const float dy = monster->PositionY() - player->PositionY();
            stable = (dx * dx + dy * dy) <= 70.0f * 70.0f && (dx * dx + dy * dy) > 0.0f;
        }
        Check("MonsterStopsToAttackCheck: monster stops within attack range, no pass-through",
              closedIn && stable);
    }

    // ---- MonsterKillPlayerCheck（指令一百零八）：多次攻击 HP 到 0 + PlayerDeath ----
    {
        bool killed = WaitUntil(
            [&] {
                auto player = servers.world->FindPlayerByCharacter(seedD.characterId);
                return player != nullptr && !player->Alive();
            },
            30000);
        WorldNetworkEvent deathEvent;
        const bool gotDeath = clientD.PeekEvent(WorldNetworkEvent::Type::PlayerDeath, deathEvent,
                                                5000);
        ok = killed && gotDeath && deathEvent.characterId == seedD.characterId &&
             deathEvent.attackerType == kTypeMonster;
        Check("MonsterKillPlayerCheck: repeated attacks kill player + PlayerDeath broadcast", ok);
    }

    // ---- PlayerDiesWhileTargetedCheck（指令一百二十九）：所有以其为 target 的怪 Returning ----
    {
        const bool allReturning = WaitUntil(
            [&] {
                // 怪17~20：target 清空 且 非 Chase
                for (std::uint64_t id = 17; id <= 20; ++id) {
                    auto monster = servers.world->FindMonster(id);
                    if (monster == nullptr) {
                        return false;
                    }
                    if (monster->TargetCharacterId() != 0 ||
                        monster->State() == MonsterState::Chase) {
                        return false;
                    }
                }
                return true;
            },
            8000);
        Check("PlayerDiesWhileTargetedCheck: all monsters targeting victim -> Returning",
              allReturning);
    }

    // ---- DeadPlayerMoveBlockedCheck（指令一百零九/五十八）：死亡玩家 MoveInput 位置不变 ----
    {
        auto player = servers.world->FindPlayerByCharacter(seedD.characterId);
        bool okBlocked = player != nullptr && !player->Alive();
        const float x0 = player ? player->PositionX() : 0.0f;
        const float y0 = player ? player->PositionY() : 0.0f;
        for (int i = 0; i < 20; ++i) {
            clientD.client().SendMoveInput(static_cast<std::uint32_t>(i + 1), 1.0f, 0.0f, 0.1f);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        player = servers.world->FindPlayerByCharacter(seedD.characterId);
        okBlocked = okBlocked && player != nullptr && player->PositionX() == x0 &&
                    player->PositionY() == y0;
        Check("DeadPlayerMoveBlockedCheck: dead player MoveInput ignored", okBlocked);
        clientD.Disconnect();
        WaitUntil([&] { clientD.DrainEvents(); return true; }, 300);
    }

    // ---- PlayerAttackSuccessCheck（指令九十八/二十五）：A 靠近 Slime3 攻击 80->62 ----
    // A 此刻才进世界（D 场景已结束）——读服务器怪3 实际位置瞬移贴身（巡逻离开
    // spawn 后固定坐标不可靠），压缩被围殴窗口。
    WorldTestClient clientA;
    {
        const std::string ticketA = TicketFor(servers.login, seedA);
        const bool enterA = !ticketA.empty() && clientA.ConnectAndEnter(ticketA, 8000);
        Check("CombatChecks: attacker entered", enterA);
        auto monster3 = servers.world->FindMonster(kSlime3);
        const bool teleported = monster3 != nullptr &&
                                TeleportPlayer(clientA, servers, seedA.characterId,
                                               monster3->PositionX() + 40.0f,
                                               monster3->PositionY());
        Check("PlayerAttackSuccessCheck: attacker teleported next to slime", teleported);
        // 等怪3 进入 A 的可见集（AOI tick 200ms）
        const bool visible = WaitUntil(
            [&] {
                auto player = servers.world->FindPlayerByCharacter(seedA.characterId);
                return player != nullptr && player->VisibleMonsters().count(kSlime3) != 0;
            },
            3000);
        Check("PlayerAttackSuccessCheck: target monster visible", visible);
        clientA.controller.SendAttack(kSlime3); // requestId=1
        WorldNetworkEvent response;
        const bool gotResponse = WaitAttackResponse(clientA, 1, response, 3000, 1);
        WorldNetworkEvent hit;
        const bool gotHit = WaitCombatEvent(clientA, seedA.characterId, kSlime3, hit, 3000, 62);
        auto monster = servers.world->FindMonster(kSlime3);
        ok = gotResponse && response.success &&
             response.resultCode == static_cast<std::uint8_t>(CombatResultCode::Success) &&
             gotHit && hit.damage == 18 && hit.targetHpAfter == 62 && hit.targetMaxHp == 80 &&
             hit.attackerType == kTypePlayer && hit.targetType == kTypeMonster && !hit.killed &&
             monster != nullptr && monster->CurrentHp() == 62;
        Check("PlayerAttackSuccessCheck: attack accepted, damage 18, HP 80->62", ok);
    }

    // ---- CombatEventCheck（指令一百一十四/四十/四十二）字段与广播范围 ----
    {
        const WorldNetworkEvent* hitA = LastCombatEventFor(clientA, seedA.characterId, kSlime3);
        // B 也能看到怪3（观察者）-> 收到同一 eventId
        bool observerGot = WaitUntil(
            [&] {
                clientB.DrainEvents();
                for (const auto& e : clientB
                         .recorded[WorldTestClient::IndexOf(
                             WorldNetworkEvent::Type::CombatEvent)]) {
                    if (e.attackerId == seedA.characterId && e.targetId == kSlime3) {
                        return true;
                    }
                }
                return false;
            },
            3000);
        // 攻击者同时是观察者：只收一次（去重）
        const int attackerCopies = CountCombatEvents(clientA, seedA.characterId, kSlime3);
        Check("CombatEventCheck: fields (attacker/target/damage/hpAfter/killed)",
              hitA != nullptr && hitA->damage == 18 && hitA->targetHpAfter == 62 &&
                  hitA->targetMaxHp == 80 && !hitA->killed);
        Check("CombatObserverCheck: nearby third player receives CombatEvent", observerGot);
        Check("CombatNoDuplicateBroadcastCheck: attacker-as-observer receives exactly once",
              attackerCopies == 1);
        // 指令一百一十五：远处 F 看不到战斗双方 -> 不收 CombatEvent
        clientF.DrainEvents();
        const int fEvents = static_cast<int>(clientF
                                                 .recorded[WorldTestClient::IndexOf(
                                                     WorldNetworkEvent::Type::CombatEvent)]
                                                 .size());
        Check("NoGlobalCombatBroadcastCheck: far client receives no CombatEvent", fEvents == 0);
    }

    // ---- AttackCooldownCheck（指令一百零一/二十九）：立即第二次 Cooldown，0.8s 后成功 ----
    {
        clientA.controller.SendAttack(kSlime3); // requestId=2
        WorldNetworkEvent response;
        const bool gotCooldown = WaitAttackResponse(clientA, 2, response);
        bool okCd = gotCooldown && !response.success &&
                    response.resultCode == static_cast<std::uint8_t>(CombatResultCode::Cooldown);
        auto monster = servers.world->FindMonster(kSlime3);
        okCd = okCd && monster != nullptr && monster->CurrentHp() == 62; // 无伤害
        Check("AttackCooldownCheck: immediate second attack -> Cooldown, HP unchanged", okCd);
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        clientA.controller.SendAttack(kSlime3); // requestId=3
        WorldNetworkEvent hit;
        const bool gotHit = WaitCombatEvent(clientA, seedA.characterId, kSlime3, hit, 3000, 44);
        monster = servers.world->FindMonster(kSlime3);
        okCd = okCd && gotHit && hit.targetHpAfter == 44 && monster != nullptr &&
               monster->CurrentHp() == 44;
        Check("AttackCooldownCheck: after 0.8s attack succeeds again (62->44)", okCd);
    }

    // ---- DuplicateAttackRequestCheck（指令一百零二/三十/三十一）：同 requestId 只扣一次 ----
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(900)); // CD ready
        clientA.client().SendAttack(9001, kTypeMonster, kSlime3);
        WorldNetworkEvent response;
        const bool first = WaitAttackResponse(clientA, 9001, response, 3000, 1);
        const bool firstOk = first && response.success;
        WorldNetworkEvent firstHit;
        const bool firstHitOk =
            WaitCombatEvent(clientA, seedA.characterId, kSlime3, firstHit, 3000, 26);
        // 立即重放同 requestId（服务器已记录 -> DuplicateRequest）
        clientA.client().SendAttack(9001, kTypeMonster, kSlime3);
        WorldNetworkEvent dupResponse;
        const bool dup = WaitAttackResponse(clientA, 9001, dupResponse, 2000, 0);
        auto monster = servers.world->FindMonster(kSlime3);
        bool okDup = firstOk && firstHitOk && dup && !dupResponse.success &&
                     dupResponse.resultCode ==
                         static_cast<std::uint8_t>(CombatResultCode::DuplicateRequest) &&
                     monster != nullptr && monster->CurrentHp() == 26; // 44-18 只扣一次
        Check("DuplicateAttackRequestCheck: replayed requestId -> DuplicateRequest, HP once",
              okDup);
    }

    // ---- AttackSpamCheck（指令一百一十九/八十七）：100 连发仍受 CD 限制 ----
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(900)); // CD ready
        const int before = CountCombatEvents(clientA, seedA.characterId, kSlime3);
        for (std::uint64_t i = 10001; i < 10101; ++i) {
            clientA.client().SendAttack(i, kTypeMonster, kSlime3);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        clientA.DrainEvents();
        const int after = CountCombatEvents(clientA, seedA.characterId, kSlime3);
        auto monster = servers.world->FindMonster(kSlime3);
        // 100 发在 300ms 内：最多 1 跳（26 -> 8）；若 CD 失效会连扣多跳。
        Check("AttackSpamCheck: 100 requests -> at most one damage tick",
              after - before <= 1 && monster != nullptr && monster->CurrentHp() >= 8);
    }

    // ---- PlayerKillMonsterCheck（指令一百一十/五十三/一百二十八）：连续攻击到死 ----
    {
        // HP=8：一发 18 -> 0 dead
        std::this_thread::sleep_for(std::chrono::milliseconds(900)); // CD ready
        clientA.controller.SendAttack(kSlime3); // requestId=4
        bool dead = WaitUntil(
            [&] {
                auto monster = servers.world->FindMonster(kSlime3);
                return monster != nullptr && !monster->Alive() &&
                       monster->State() == MonsterState::Dead;
            },
            5000);
        auto monster = servers.world->FindMonster(kSlime3);
        // 指令一百二十八：Chase 中被杀 -> target 清空 + AI 停止
        const bool targetCleared =
            dead && monster != nullptr && monster->TargetCharacterId() == 0 &&
            monster->State() == MonsterState::Dead;
        Check("PlayerKillMonsterCheck: repeated normal attacks kill slime (state=Dead)", dead);
        Check("MonsterDiesWhileChasingCheck: killed while chasing -> target cleared, AI stopped",
              targetCleared);
    }

    // ---- AttackDeadMonsterCheck（指令一百零三/二十一）：Dead 目标再攻 -> TargetDead ----
    {
        clientA.controller.SendAttack(kSlime3); // requestId=5
        WorldNetworkEvent response;
        const bool got = WaitAttackResponse(clientA, 5, response);
        Check("AttackDeadMonsterCheck: attacking dead monster -> TargetDead",
              got && !response.success &&
                  response.resultCode == static_cast<std::uint8_t>(CombatResultCode::TargetDead));
    }

    // ---- MonsterDeathBroadcastCheck（指令一百一十一/五十）：A/B 都收到 MonsterDeath ----
    {
        bool aGot = WaitUntil(
            [&] {
                clientA.DrainEvents();
                return CountMonsterDeathsFor(clientA, kSlime3) >= 1;
            },
            3000);
        bool bGot = WaitUntil(
            [&] {
                clientB.DrainEvents();
                return CountMonsterDeathsFor(clientB, kSlime3) >= 1;
            },
            3000);
        WorldNetworkEvent deathEvent;
        for (const auto& e :
             clientA.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::MonsterDeath)]) {
            if (e.monsterEntityId == kSlime3) {
                deathEvent = e;
            }
        }
        Check("MonsterDeathBroadcastCheck: both observers receive MonsterDeath(killer=A)",
              aGot && bGot && deathEvent.monsterEntityId == kSlime3 &&
                  deathEvent.characterId == seedA.characterId);
    }

    // ---- DeadMonsterAiStopCheck（指令一百一十三/五十六）：死亡后位置不再变化 ----
    // 服务器内状态断言（阶段13 教训：观察者 batch 存在可见性/时序抖动，Dead 怪
    // 3s 清理窗口内观察者 batch 不可靠；位置冻结语义由服务器权威状态验证）。
    {
        auto monster = servers.world->FindMonster(kSlime3);
        const bool deadNow = monster != nullptr && !monster->Alive() &&
                             monster->State() == MonsterState::Dead;
        const float x1 = monster ? monster->PositionX() : 0.0f;
        const float y1 = monster ? monster->PositionY() : 0.0f;
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        monster = servers.world->FindMonster(kSlime3);
        const bool frozen = deadNow && monster != nullptr &&
                            monster->PositionX() == x1 && monster->PositionY() == y1 &&
                            !monster->Alive() && monster->State() == MonsterState::Dead;
        Check("DeadMonsterAiStopCheck: dead monster position frozen (server state)", frozen);
    }

    // ---- A 脱离接触：瞬移到 (60,60) 安全点（怪2/4 追击将触发 lost/leash Returning），
    //      避免后续场景期间被围殴致死 ----
    {
        TeleportPlayer(clientA, servers, seedA.characterId, 60.0f, 60.0f);
        // 等追击怪脱离（lost 525 / leash 600 -> Returning）
        WaitUntil(
            [&] {
                for (const auto id : servers.world->MonsterEntityIds()) {
                    auto monster = servers.world->FindMonster(id);
                    if (monster == nullptr) {
                        continue;
                    }
                    const float dx = monster->PositionX() - 60.0f;
                    const float dy = monster->PositionY() - 60.0f;
                    if (dx * dx + dy * dy < 400.0f * 400.0f) {
                        return false; // 仍有怪逼近
                    }
                }
                return true;
            },
            15000);
    }

    // ---- AttackOutOfRangeCheck（指令九十九/十八/三十三）：可见但 >100 -> 拒绝 ----
    {
        // 从安全点瞬移到距怪4 ~160 的点（迭代对准怪4 实时位置，>100 且 <600 可见）。
        bool moved = false;
        bool inWindow = false;
        for (int attempt = 0; attempt < 3 && !inWindow; ++attempt) {
            auto monster4 = servers.world->FindMonster(kSlime4);
            if (!monster4) {
                break;
            }
            float tx = monster4->PositionX() + 160.0f;
            if (tx > 1990.0f) {
                tx = monster4->PositionX() - 160.0f;
            }
            moved = TeleportPlayer(clientA, servers, seedA.characterId, tx,
                                   monster4->PositionY());
            auto attacker = servers.world->FindPlayerByCharacter(seedA.characterId);
            auto monsterNow = servers.world->FindMonster(kSlime4);
            if (!moved || !attacker || !monsterNow) {
                continue;
            }
            const float dx = attacker->PositionX() - monsterNow->PositionX();
            const float dy = attacker->PositionY() - monsterNow->PositionY();
            const float dsq = dx * dx + dy * dy;
            inWindow = dsq > 110.0f * 110.0f && dsq < 600.0f * 600.0f;
        }
        // Teleport 后等怪4 进入 A 的 visibleMonsters（AOI tick 200ms）再攻击，
        // 否则 visible 集仍是从安全点出发时的旧集合 -> InvalidTarget。
        const bool m4Visible = WaitUntil(
            [&] {
                auto player = servers.world->FindPlayerByCharacter(seedA.characterId);
                return player != nullptr && player->VisibleMonsters().count(kSlime4) != 0;
            },
            3000);
        clientA.controller.SendAttack(kSlime4); // requestId=6
        WorldNetworkEvent response;
        const bool got = WaitAttackResponse(clientA, 6, response, 3000, 0);
        auto monster = servers.world->FindMonster(kSlime4);
        Check("AttackOutOfRangeCheck: visible target beyond 100 -> OutOfRange, HP unchanged",
              moved && inWindow && m4Visible && got && !response.success &&
                  response.resultCode == static_cast<std::uint8_t>(CombatResultCode::OutOfRange) &&
                  monster != nullptr && monster->CurrentHp() == 80);
    }

    // ---- AttackInvisibleTargetCheck（指令一百/三十二/三十三/八十八）：目标不在
    //      visibleMonsters -> InvalidTarget ----
    {
        // 动态选一只距 A >700 的怪（簇3 无玩家，位置稳定在 spawn±190）。
        std::uint64_t farId = 0;
        auto attacker = servers.world->FindPlayerByCharacter(seedA.characterId);
        if (attacker) {
            for (const auto id : servers.world->MonsterEntityIds()) {
                if (id == kSlime4) {
                    continue;
                }
                auto monster = servers.world->FindMonster(id);
                if (!monster) {
                    continue;
                }
                const float dx = monster->PositionX() - attacker->PositionX();
                const float dy = monster->PositionY() - attacker->PositionY();
                if (dx * dx + dy * dy > 700.0f * 700.0f) {
                    farId = id;
                    break;
                }
            }
        }
        clientA.controller.SendAttack(farId); // requestId=7
        WorldNetworkEvent response;
        const bool got = WaitAttackResponse(clientA, 7, response, 3000, 0);
        Check("AttackInvisibleTargetCheck: non-visible monster -> InvalidTarget",
              farId != 0 && got && !response.success &&
                  response.resultCode ==
                      static_cast<std::uint8_t>(CombatResultCode::InvalidTarget));
    }

    // ---- HealthSnapshotCheck（指令一百一十七/六十八/六十九）：1s 纠偏 ----
    {
        const bool got = WaitUntil(
            [&] {
                clientA.DrainEvents();
                for (const auto& e : clientA
                         .recorded[WorldTestClient::IndexOf(
                             WorldNetworkEvent::Type::HealthSnapshot)]) {
                    if (e.entityType == kTypeMonster && e.entityId == kSlime4 &&
                        e.currentHp == 80 && e.maxHp == 80 && e.alive) {
                        return true;
                    }
                }
                return false;
            },
            4000);
        // 自身纠偏也存在
        bool selfSnap = false;
        auto player = servers.world->FindPlayerByCharacter(seedA.characterId);
        for (const auto& e :
             clientA.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::HealthSnapshot)]) {
            if (e.entityType == kTypePlayer && e.entityId == seedA.characterId && player &&
                e.currentHp == player->CurrentHp() && e.maxHp == player->MaxHp()) {
                selfSnap = true;
            }
        }
        Check("HealthSnapshotCheck: 1s correction snapshot with correct HP", got && selfSnap);
    }

    // ---- MultiClientCombatReplicationCheck（指令一百二十四）：10 Client 全收 CombatEvent ----
    {
        constexpr int kClients = 10;
        // A 先瞬移贴身怪4（读服务器实时位置），再以怪4 为圆心 450 环带延迟 seed
        // 10 观察者（距怪4 400~500：>350 不抢 aggro，<600 可见 -> 属于观察者集合）。
        auto monster4 = servers.world->FindMonster(kSlime4);
        const bool closeToM4 =
            monster4 != nullptr &&
            TeleportPlayer(clientA, servers, seedA.characterId, monster4->PositionX() + 40.0f,
                           monster4->PositionY());
        // 等 AOI 更新（A 从安全点过来，visibleMonsters 需要一个 tick）
        const bool m4Visible2 = WaitUntil(
            [&] {
                auto player = servers.world->FindPlayerByCharacter(seedA.characterId);
                return player != nullptr && player->VisibleMonsters().count(kSlime4) != 0;
            },
            3000);
        std::vector<CharacterSeed> seeds(kClients);
        bool groupSeeded = closeToM4 && monster4 != nullptr;
        const float cx = monster4 ? monster4->PositionX() : 0.0f;
        const float cy = monster4 ? monster4->PositionY() : 0.0f;
        for (int i = 0; i < kClients && groupSeeded; ++i) {
            const float angle = static_cast<float>(i) * 36.0f * 3.14159265f / 180.0f;
            const float x = std::clamp(cx + 450.0f * std::cos(angle), 10.0f, 1990.0f);
            const float y = std::clamp(cy + 450.0f * std::sin(angle), 10.0f, 1990.0f);
            groupSeeded = groupSeeded &&
                          SeedAt(db, accounts, characters, "cbt_m_" + std::to_string(i),
                                 "CbtM" + std::to_string(i), x, y, seeds[i]);
        }
        std::vector<std::unique_ptr<WorldTestClient>> clients(kClients);
        std::atomic<int> ready{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < kClients; ++i) {
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
        bool okMulti = groupSeeded && ready == kClients && m4Visible2;
        // A 贴身怪4 后攻击一次
        okMulti = okMulti && closeToM4;
        if (okMulti) {
            clientA.controller.SendAttack(kSlime4); // requestId=8
        }
        for (auto& client : clients) {
            const bool got = WaitUntil(
                [&] {
                    client->DrainEvents();
                    for (const auto& e :
                         client->recorded[WorldTestClient::IndexOf(
                             WorldNetworkEvent::Type::CombatEvent)]) {
                        if (e.attackerId == seedA.characterId && e.targetId == kSlime4) {
                            return true;
                        }
                    }
                    return false;
                },
                5000);
            okMulti = okMulti && got;
        }
        Check("MultiClientCombatReplicationCheck: 10 clients all receive CombatEvent", okMulti);
        for (auto& client : clients) {
            client->Disconnect();
        }
        WaitUntil([&] { return true; }, 500);
    }

    // ---- TargetDisconnectDuringCombatCheck（指令一百二十七/四十四）：战斗中玩家断线
    //      -> 怪 Returning，不 Crash ----
    {
        // H(1100,600) 被簇2 围攻（前面已进世界）——等 H 至少被击中一次后断线。
        const bool hitH = WaitUntil(
            [&] {
                clientH.DrainEvents();
                return CountCombatEventsAgainstPlayer(clientH, seedH.characterId) >= 1;
            },
            15000);
        clientH.Disconnect();
        bool returning = false;
        if (hitH) {
            returning = WaitUntil(
                [&] {
                    for (std::uint64_t id = 5; id <= 8; ++id) {
                        auto monster = servers.world->FindMonster(id);
                        if (monster == nullptr ||
                            (monster->TargetCharacterId() != 0 &&
                             monster->TargetCharacterId() == seedH.characterId) ||
                            (monster->State() == MonsterState::Chase &&
                             monster->TargetCharacterId() == seedH.characterId)) {
                            return false;
                        }
                    }
                    // 怪5~8 全部不再以 H 为 target
                    for (std::uint64_t id = 5; id <= 8; ++id) {
                        auto monster = servers.world->FindMonster(id);
                        if (monster != nullptr && monster->TargetCharacterId() != 0) {
                            return false; // 均需 Returning/空 target
                        }
                        if (monster != nullptr &&
                            monster->State() != MonsterState::Returning &&
                            monster->State() != MonsterState::Idle &&
                            monster->State() != MonsterState::Patrol) {
                            return false;
                        }
                    }
                    return true;
                },
                8000);
        }
        Check("TargetDisconnectDuringCombatCheck: player disconnect -> monster Returning, no crash",
              hitH && returning);
    }

    // ---- MonsterDeathCleanupCheck（指令一百一十二/五十四/五十五/七十七）：死亡 3s 后清理 ----
    {
        // 怪3 死亡 + 3s -> 服务器移除；B 仍在怪3 观察者集合（距死亡位置 <700），
        // 收到 Despawn(Removed)（A 已脱离到安全点，不在观察者集合）。
        bool removed = WaitUntil(
            [&] {
                clientB.DrainEvents();
                return servers.world->FindMonster(kSlime3) == nullptr &&
                       servers.world->MonsterCount() == 49 &&
                       CountMonsterRemovedDespawnsFor(clientB, kSlime3) >= 1;
            },
            10000);
        // 客户端实体同步清理（B 侧）
        clientB.DrainEvents();
        const bool clientCleaned = clientB.controller.RemoteMonsters().Find(kSlime3) == nullptr;
        Check("MonsterDeathCleanupCheck: 3s after death removed from manager + Despawn(Removed)",
              removed);
        Check("MonsterDeathCleanupCheck: client remote entity removed after despawn",
              clientCleaned);
        // 指令五十五：不复活（杀一只少一只）
        Check("MonsterDeathCleanupCheck: no respawn (kill one, one less)",
              servers.world->MonsterCount() == 49);
    }

    // ---- CombatPersistenceBoundaryCheck（指令一百三十/八十/八十一）：重启恢复默认 ----
    {
        clientA.Disconnect();
        clientB.Disconnect();
        clientF.Disconnect();
        clientH.Disconnect();
        WaitUntil([&] { return true; }, 500);
        servers.StopWorld();
        const bool restarted = servers.StartWorld();
        bool monstersFull = restarted && servers.world->MonsterCount() == 50;
        if (monstersFull) {
            for (std::uint64_t id = 1; id <= 50; ++id) {
                auto monster = servers.world->FindMonster(id);
                if (monster == nullptr || !monster->Alive() || monster->CurrentHp() != 80 ||
                    monster->State() != MonsterState::Idle) {
                    monstersFull = false;
                }
            }
        }
        Check("CombatPersistenceBoundaryCheck: restart -> 20 full-HP idle monsters", monstersFull);
        // A 重进：HP 恢复默认 100（战斗状态不持久化）
        const std::string ticketA2 = TicketFor(servers.login, seedA);
        WorldTestClient clientA2;
        const bool enterA2 = !ticketA2.empty() && clientA2.ConnectAndEnter(ticketA2, 8000);
        bool hpReset = enterA2;
        auto player = servers.world->FindPlayerByCharacter(seedA.characterId);
        hpReset = hpReset && player != nullptr && player->CurrentHp() == 100 &&
                  player->MaxHp() == 100 && player->Alive();
        // EnterWorldResponse HP 字段
        const auto& enters =
            clientA2.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::EnterWorldSuccess)];
        if (!enters.empty()) {
            hpReset = hpReset && enters.front().currentHp == 100 && enters.front().alive;
        }
        Check("CombatPersistenceBoundaryCheck: player HP restored to 100 after restart", hpReset);
        clientA2.Disconnect();
        WaitUntil([&] { return true; }, 300);
    }

    db.Close();
    servers.StopAll();
}

} // namespace worldtest
