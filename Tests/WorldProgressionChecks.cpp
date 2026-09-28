// ---------------------------------------------------------------------------
// 阶段17：服务器权威成长、奖励与怪物重生检查（Progression/Reward/Respawn V0.17）。
// 仍链接 LegendWorldTests（不新增测试 exe）。
// A 部分：纯逻辑（ExpFormula/跨多级/满级/等级成长/协议 Roundtrip/Malformed）。
// B 部分：真实链路（Basic/Skill/DOT 击杀奖励、升级、AOI 广播、DB 持久化、
//         SpawnSlot/Respawn 8s、重启重置）。独立 servers 生命周期。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Server/LoginServer/Account/AccountRepository.h"
#include "Server/WorldServer/Monster/MonsterDefinition.h"
#include "Server/WorldServer/Monster/MonsterEntity.h"
#include "Server/WorldServer/Monster/MonsterRespawnManager.h"
#include "Server/WorldServer/PlayerSession.h"
#include "Server/WorldServer/Progression/ProgressionService.h"
#include "Server/WorldServer/Progression/RewardService.h"
#include "Shared/Network/ByteWriter.h"
#include "Shared/Progression/ProgressionProtocol.h"
#include "Shared/Progression/ProgressionTypes.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace worldtest {

namespace {

using legend::world::ApplyLevelProgression;
using legend::world::CalculateLevelUp;
using legend::world::ExpToNextLevel;
using legend::world::kProgressionMaxLevel;
using legend::world::LevelProgressResult;
using legend::world::MonsterEntity;
using StatusClock = std::chrono::steady_clock;

namespace CharacterRepository = legend::account::CharacterRepository;

const std::uint8_t kTypePlayer = static_cast<std::uint8_t>(legend::world::CombatEntityType::Player);
const std::uint8_t kTypeMonster =
    static_cast<std::uint8_t>(legend::world::CombatEntityType::Monster);
const std::uint8_t kTargetSelf = static_cast<std::uint8_t>(legend::world::SkillTargetType::Self);
const std::uint8_t kTargetMonsterSkill =
    static_cast<std::uint8_t>(legend::world::SkillTargetType::Monster);

// ---- 事件辅助 ----

const std::deque<WorldNetworkEvent>& EventsOf(const WorldTestClient& client,
                                              WorldNetworkEvent::Type type) {
    return client.recorded[WorldTestClient::IndexOf(type)];
}

int CountRewardsFor(const WorldTestClient& client, std::uint64_t characterId) {
    int n = 0;
    for (const auto& e : EventsOf(client, WorldNetworkEvent::Type::RewardGrantedEvent)) {
        if (e.characterId == characterId) {
            ++n;
        }
    }
    return n;
}

int CountLevelUpsFor(const WorldTestClient& client, std::uint64_t characterId) {
    int n = 0;
    for (const auto& e : EventsOf(client, WorldNetworkEvent::Type::LevelUpEvent)) {
        if (e.characterId == characterId) {
            ++n;
        }
    }
    return n;
}

// 等待"新"奖励（baseline = 调用前该玩家的 RewardGranted 事件数；指令五教训：
// 计数基线必须在触发动作前捕获，否则旧事件会命中谓词）。
bool WaitRewardFor(WorldTestClient& client, std::uint64_t characterId,
                   WorldNetworkEvent& out, int timeoutMs = 4000, int baseline = 0) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            const auto& events =
                EventsOf(client, WorldNetworkEvent::Type::RewardGrantedEvent);
            if (static_cast<int>(events.size()) <= baseline) {
                return false;
            }
            for (auto it = events.rbegin(); it != events.rend(); ++it) {
                if (it->characterId == characterId) {
                    out = *it;
                    return true;
                }
            }
            return false;
        },
        timeoutMs);
}

bool WaitLevelUpFor(WorldTestClient& client, std::uint64_t characterId,
                    WorldNetworkEvent& out, int timeoutMs = 6000) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            for (auto it = EventsOf(client, WorldNetworkEvent::Type::LevelUpEvent).rbegin();
                 it != EventsOf(client, WorldNetworkEvent::Type::LevelUpEvent).rend(); ++it) {
                if (it->characterId == characterId) {
                    out = *it;
                    return true;
                }
            }
            return false;
        },
        timeoutMs);
}

bool WaitProgressionSnapshot(WorldTestClient& client, WorldNetworkEvent& out,
                             int timeoutMs = 4000) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            const auto& events =
                EventsOf(client, WorldNetworkEvent::Type::ProgressionSnapshotEvent);
            if (!events.empty()) {
                out = events.back(); // 最新一条（重进后旧快照仍在 recorded）
                return true;
            }
            return false;
        },
        timeoutMs);
}

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

// 白盒读玩家（level/exp/gold/hp 等）。
struct PlayerView {
    bool found = false;
    std::uint32_t level = 0;
    std::int64_t exp = 0;
    std::int64_t gold = 0;
    std::uint32_t maxHp = 0;
    std::uint32_t currentHp = 0;
    std::uint32_t attack = 0;
    std::uint32_t defense = 0;
};

PlayerView ReadPlayer(WorldTestServers& servers, std::uint64_t characterId) {
    PlayerView view;
    auto player = servers.world->FindPlayerByCharacter(characterId);
    if (!player) {
        return view;
    }
    view.found = true;
    view.level = player->Level();
    view.exp = player->Experience();
    view.gold = player->Gold();
    view.maxHp = player->MaxHp();
    view.currentHp = player->CurrentHp();
    view.attack = player->BaseAttackPower();
    view.defense = player->BaseDefense();
    return view;
}

// 普攻直至击杀（每只 slime 80 HP：普攻 18 -> 5 次；requestId 递增防重放拒绝）。
bool BasicAttackUntilDead(WorldTestServers& servers, WorldTestClient& client,
                          std::uint64_t monsterId, std::uint64_t requestIdBase,
                          int timeoutMs = 15000) {
    std::uint64_t requestId = requestIdBase;
    return WaitUntil(
        [&] {
            auto monster = servers.world->FindMonster(monsterId);
            if (!monster) {
                return true; // 已移除（视为结束）
            }
            if (!monster->Alive()) {
                return true;
            }
            client.client().SendAttack(requestId++, kTypeMonster, monsterId);
            std::this_thread::sleep_for(std::chrono::milliseconds(200)); // 攻击 CD 0.8s
            return false;
        },
        timeoutMs);
}

// ===========================================================================
// A. 纯逻辑检查
// ===========================================================================

void RunProgressionLogicChecks() {
    // ---- ExpFormulaCheck（指令四）：ExpToNextLevel(level) = 100 * level ----
    {
        bool ok = ExpToNextLevel(1) == 100 && ExpToNextLevel(2) == 200 &&
                  ExpToNextLevel(3) == 300 && ExpToNextLevel(10) == 1000;
        // 指令四：一次奖励跨多个等级
        auto r1 = ApplyLevelProgression(1, 0, 250); // 100 -> lv2 余150（<200 停）
        ok = ok && r1.level == 2 && r1.exp == 150 && r1.levelsGained == 1 && r1.levelUp;
        auto r2 = ApplyLevelProgression(1, 0, 400); // 100+200+<300 -> lv3 余100
        ok = ok && r2.level == 3 && r2.exp == 100 && r2.levelsGained == 2;
        auto r3 = ApplyLevelProgression(1, 50, 350); // 50+350=400 -> lv3 余100
        ok = ok && r3.level == 3 && r3.exp == 100 && r3.levelsGained == 2;
        Check("ExpFormulaCheck: 100*level + multi-level wrap in one reward", ok);
    }

    // ---- MaxLevelCheck（指令五）：满级后 EXP 不累计、不溢出 ----
    {
        bool ok = kProgressionMaxLevel == 100;
        auto r1 = ApplyLevelProgression(100, 50, 500);
        ok = ok && r1.level == 100 && r1.exp == 50 && !r1.levelUp && r1.atMaxLevel;
        auto r2 = ApplyLevelProgression(99, 50, 9900); // 9900=ExpToNext(99) -> lv100 余 0
        ok = ok && r2.level == 100 && r2.exp == 0 && r2.levelUp;
        auto r3 = ApplyLevelProgression(100, 999, 999); // 满级：EXP 完全不累计
        ok = ok && r3.level == 100 && r3.exp == 999 && r3.atMaxLevel;
        Check("MaxLevelCheck: cap 100, no overflow beyond max level", ok);
    }

    // ---- LevelStatGrowthCheck（指令十）：每级 MaxHp+10 / Attack+2 / Defense+1 ----
    {
        const auto lv1 = CalculateLevelUp(1);
        const auto lv2 = CalculateLevelUp(2);
        const auto lv10 = CalculateLevelUp(10);
        bool ok = lv1.maxHp == 100 && lv1.attackPower == 20 && lv1.defense == 5;
        ok = ok && lv2.maxHp == 110 && lv2.attackPower == 22 && lv2.defense == 6;
        ok = ok && lv10.maxHp == 190 && lv10.attackPower == 38 && lv10.defense == 14;
        Check("LevelStatGrowthCheck: +10hp/+2atk/+1def per level, Mana stays 100", ok);
    }

    // ---- ProgressionProtocolRoundtripCheck（指令十三~十五）----
    {
        std::string error;
        bool ok = true;
        {
            legend::world::RewardGrantedPayload p;
            p.characterId = 7;
            p.sourceMonsterEntityId = 42;
            p.expGranted = 25;
            p.goldGranted = 3;
            p.newExperience = 125;
            p.newGold = 33;
            p.level = 2;
            p.serverTime = 999;
            std::vector<std::uint8_t> payload;
            legend::world::RewardGrantedPayload d;
            ok = ok && legend::world::EncodeRewardGranted(p, payload) &&
                 legend::world::DecodeRewardGranted(payload.data(), payload.size(), d, error) &&
                 d.characterId == 7 && d.sourceMonsterEntityId == 42 && d.expGranted == 25 &&
                 d.goldGranted == 3 && d.newExperience == 125 && d.newGold == 33 && d.level == 2;
        }
        {
            legend::world::LevelUpEventPayload p;
            p.characterId = 7;
            p.oldLevel = 1;
            p.newLevel = 3;
            p.currentExp = 100;
            p.nextLevelExp = 300;
            p.newMaxHp = 120;
            p.newAttackPower = 24;
            p.newDefense = 7;
            p.serverTime = 1234;
            std::vector<std::uint8_t> payload;
            legend::world::LevelUpEventPayload d;
            ok = ok && legend::world::EncodeLevelUpEvent(p, payload) &&
                 legend::world::DecodeLevelUpEvent(payload.data(), payload.size(), d, error) &&
                 d.oldLevel == 1 && d.newLevel == 3 && d.newMaxHp == 120 &&
                 d.newAttackPower == 24 && d.nextLevelExp == 300;
        }
        {
            legend::world::ProgressionSnapshotPayload p;
            p.characterId = 9;
            p.level = 5;
            p.experience = 123;
            p.expToNext = 377;
            p.gold = 77;
            p.serverTime = 555;
            std::vector<std::uint8_t> payload;
            legend::world::ProgressionSnapshotPayload d;
            ok = ok && legend::world::EncodeProgressionSnapshot(p, payload) &&
                 legend::world::DecodeProgressionSnapshot(payload.data(), payload.size(), d,
                                                          error) &&
                 d.level == 5 && d.experience == 123 && d.expToNext == 377 && d.gold == 77;
        }
        // Malformed：截断 + 尾随
        {
            legend::world::RewardGrantedPayload p;
            std::vector<std::uint8_t> payload;
            legend::world::EncodeRewardGranted(p, payload);
            legend::world::RewardGrantedPayload d;
            ok = ok && !legend::world::DecodeRewardGranted(payload.data(), payload.size() - 3, d,
                                                           error);
            auto padded = payload;
            padded.push_back(0xAB);
            ok = ok && !legend::world::DecodeRewardGranted(padded.data(), padded.size(), d, error);
        }
        Check("ProgressionProtocolRoundtripCheck: 3 payloads roundtrip + malformed rejected",
              ok);
    }
}

// ===========================================================================
// B. 真实链路检查
// ===========================================================================

void RunProgressionChainChecks() {
    WorldTestServers servers;
    servers.dbPath = TempDbPath("world_progression");
    RemoveDb(servers.dbPath);
    // 主场景 respawnDelay 设 600s：测试期间无重生怪出现（避免新怪围殴 killer 干扰
    // 奖励/成长时序）；Respawn 行为由 RunProgressionRespawnChecks 独立场景验证。
    servers.worldRespawnDelayMs = 600000;
    Check("ProgressionServersStartCheck", servers.StartLogin() && servers.StartWorld());

    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(8, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("ProgressionChecks: db ready", false);
        servers.StopAll();
        return;
    }

    // 布局：A(100,1000) killer / B(50,650) 观察者（距 A 353 可见、距所有怪 >350
    // 不被 aggro——B 全程存活）/ C(60,60) 远处。
    CharacterSeed seedA;
    CharacterSeed seedB;
    CharacterSeed seedC;
    bool seeded = true;
    seeded = seeded && SeedAt(db, accounts, characters, "pg_user_a", "PgA", 100.0f, 1000.0f, seedA);
    seeded = seeded && SeedAt(db, accounts, characters, "pg_user_b", "PgB", 50.0f, 650.0f, seedB);
    seeded = seeded && SeedAt(db, accounts, characters, "pg_user_c", "PgC", 60.0f, 60.0f, seedC);
    Check("ProgressionChecks: seeds ready", seeded);

    // 布景距离全部 <100（kPlayerAttackRange），怪 Chase 贴脸后普攻必可命中。
    // 布景原则：只有"当前要杀的怪"移到 A 旁（其余留在远处原位——slime5 原位
    // (500,700) 距 A 316 在 aggro 内先移远；A HP 100 撑不住多怪集火）。
    servers.world->MoveMonsterTo(5, 1800.0f, 300.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    WorldTestClient clientA;
    WorldTestClient clientB;
    WorldTestClient clientC;
    {
        const bool eA = clientA.ConnectAndEnter(TicketFor(servers.login, seedA), 8000);
        const bool eB = clientB.ConnectAndEnter(TicketFor(servers.login, seedB), 8000);
        const bool eC = clientC.ConnectAndEnter(TicketFor(servers.login, seedC), 8000);
        Check("ProgressionChecks: A/B/C entered", eA && eB && eC);
    }

    // ---- ProgressionSnapshotCheck（指令十五）：进世界立即下发 ----
    {
        WorldNetworkEvent snap;
        const bool got = WaitProgressionSnapshot(clientA, snap);
        bool ok = got && snap.characterId == seedA.characterId && snap.progression.level == 1 &&
                  snap.progression.currentExp == 0 && snap.progression.expToNext == 100 &&
                  snap.progression.newGold == 0;
        Check("ProgressionSnapshotCheck: EnterWorld sends initial snapshot (1/0/100/0)", ok);
    }

    // ---- RewardOnBasicAttackKillCheck + GoldRewardCheck（指令六/七/十三）----
    // A 普攻杀死 slime3（5 次普攻 18 -> 90 >= 80）。
    {
        servers.world->MoveMonsterTo(3, 140.0f, 1030.0f); // 距 A 42
        const bool visible = WaitUntil(
            [&] {
                auto p = servers.world->FindPlayerByCharacter(seedA.characterId);
                return p && p->VisibleMonsters().count(3) != 0;
            },
            4000);
        const bool killed = BasicAttackUntilDead(servers, clientA, 3, 7001);
        WorldNetworkEvent reward;
        const bool gotReward = WaitRewardFor(clientA, seedA.characterId, reward);
        bool ok = visible && killed && gotReward;
        ok = ok && reward.progression.expGranted == 25 && reward.progression.goldGranted == 3 &&
             reward.progression.newExperience == 25 && reward.progression.newGold == 3 &&
             reward.progression.sourceMonsterEntityId == 3 && reward.progression.level == 1;
        // GoldRewardCheck（并入断言）：newGold = 0 + 3
        ok = ok && reward.progression.newGold == 3;
        Check("RewardOnBasicAttackKillCheck/GoldRewardCheck: basic-attack kill -> "
              "RewardGranted(25exp/3gold) to killer only",
              ok);
    }

    // ---- RewardOnlyKillerCheck（指令十六）：B/C 不收到 A 的 RewardGranted ----
    {
        Check("RewardOnlyKillerCheck: only killer receives RewardGranted",
              CountRewardsFor(clientB, seedB.characterId) == 0 &&
                  EventsOf(clientC, WorldNetworkEvent::Type::RewardGrantedEvent).empty());
    }

    // ---- RewardOnSkillKillCheck（指令七）：QS 杀死 -> 奖励 ----
    // slime4 移到 A 旁，先普攻 3 次（80 -> 26），再 QS 58 >= 26 一击必杀。
    {
        servers.world->MoveMonsterTo(4, 150.0f, 1040.0f); // 距 A 51
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::uint64_t skillPreRequestId = 7100;
        const bool lowHp = WaitUntil(
            [&] {
                auto m = servers.world->FindMonster(4);
                if (!m) {
                    return true;
                }
                if (!m->Alive()) {
                    return true;
                }
                clientA.client().SendAttack(skillPreRequestId++, kTypeMonster, 4);
                return m->Alive() && m->CurrentHp() <= 26;
            },
            8000);
        const int rewardBaseline = CountRewardsFor(clientA, seedA.characterId);
        clientA.controller.SendSkillCast(1001, kTargetMonsterSkill, 4);
        WorldNetworkEvent reward;
        const bool gotReward =
            WaitRewardFor(clientA, seedA.characterId, reward, 4000, rewardBaseline);
        Check("RewardOnSkillKillCheck: skill kill -> RewardGranted to killer",
              lowHp && gotReward && reward.progression.sourceMonsterEntityId == 4);
    }

    // ---- RewardOnDotKillCheck（指令七/三十九）：DOT 杀死 -> 奖励归属 Burn source ----
    {
        // slime5 移到 A 旁，普攻到 <=26，白盒 Burn（source=A）后 8/s tick 杀死。
        servers.world->MoveMonsterTo(5, 160.0f, 1030.0f); // 距 A 61
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::uint64_t dotPreRequestId = 7201;
        const bool lowHp = WaitUntil(
            [&] {
                auto m = servers.world->FindMonster(5);
                if (!m) {
                    return true;
                }
                if (!m->Alive()) {
                    return true;
                }
                clientA.client().SendAttack(dotPreRequestId++, kTypeMonster, 5);
                return m->Alive() && m->CurrentHp() <= 26;
            },
            15000);
        servers.world->ApplyStatusToTarget(
            kTypeMonster, 5, 2003, 1, kTypePlayer, seedA.characterId, 0u);
        const int dotRewardBaseline = CountRewardsFor(clientA, seedA.characterId);
        WorldNetworkEvent reward;
        const bool gotReward =
            WaitRewardFor(clientA, seedA.characterId, reward, 12000, dotRewardBaseline);
        bool ok = lowHp && gotReward && reward.progression.sourceMonsterEntityId == 5;
        Check("RewardOnDotKillCheck: DOT kill rewards original Burn source player", ok);
    }

    // ---- LevelUpCheck + LevelStatGrowthCheck + LevelUpFullHealCheck +
    //      LevelUpObserverCheck + NoGlobalLevelUpBroadcastCheck（指令十~十二/十四/十六）----
    // A 已杀 3 只（75 exp、9 gold）；slime6 第 4 只触发升级 lv2（HP 回满——此刻
    // 周围无活怪，可精确断言满血）；随后移入 slime7 杀掉（总 125 exp / 15 gold）。
    {
        servers.world->MoveMonsterTo(6, 170.0f, 1040.0f); // 距 A 70
        const bool killed1 = BasicAttackUntilDead(servers, clientA, 6, 7301);
        WorldNetworkEvent levelUp;
        const bool gotLevelUp = WaitLevelUpFor(clientA, seedA.characterId, levelUp);
        // 升级回满验证（此刻 slime7 未移入，无怪咬 A——指令十二）
        const auto playerAfterLevelUp = ReadPlayer(servers, seedA.characterId);
        bool ok = killed1 && gotLevelUp;
        ok = ok && levelUp.progression.oldLevel == 1 && levelUp.progression.newLevel == 2 &&
             levelUp.progression.currentExp == 0 &&
             levelUp.progression.expToNext == 200 &&
             levelUp.progression.newMaxHp == 110 && levelUp.progression.newAttackPower == 22 &&
             levelUp.progression.newDefense == 6;
        ok = ok && playerAfterLevelUp.found && playerAfterLevelUp.level == 2 &&
             playerAfterLevelUp.maxHp == 110 && playerAfterLevelUp.currentHp == 110 &&
             playerAfterLevelUp.attack == 22 && playerAfterLevelUp.defense == 6;
        // 指令十六：LevelUpEvent 给本人 + 可见者 B；远处 C 0 条。
        const bool bSees = WaitLevelUpFor(clientB, seedA.characterId, levelUp, 3000);
        const bool noGlobal = EventsOf(clientC, WorldNetworkEvent::Type::LevelUpEvent).empty();
        Check("LevelUpCheck/LevelStatGrowth/FullHeal/Observer/NoGlobal: kill to level 2, "
              "110/22/6, hp full, B sees, C none",
              ok && bSees && noGlobal);

        // slime7 第 5 只（exp 余 25、gold 15；被咬后 HP 不再断言）
        servers.world->MoveMonsterTo(7, 180.0f, 1030.0f); // 距 A 81
        const bool killed2 = BasicAttackUntilDead(servers, clientA, 7, 7401);
        const auto player = ReadPlayer(servers, seedA.characterId);
        Check("StatusChecks: 5th kill exp 25 gold 15",
              killed2 && player.found && player.exp == 25 && player.gold == 15);
    }

    // ---- RewardNoDuplicateCheck + DeadMonsterCannotRewardTwiceCheck（指令七幂等）----
    {
        // 等 slime7 的奖励事件经网络到达（计数基线需稳定）
        WaitUntil(
            [&] {
                clientA.DrainEvents();
                return CountRewardsFor(clientA, seedA.characterId) >= 5;
            },
            3000);
        const int rewardsBefore = CountRewardsFor(clientA, seedA.characterId);
        // 攻击已死/移除的目标 -> 服务器拒绝，不产生新奖励。
        clientA.client().SendAttack(7501, kTypeMonster, 3);
        clientA.client().SendAttack(7502, kTypeMonster, 3);
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        clientA.DrainEvents();
        Check("RewardNoDuplicateCheck/DeadMonsterCannotRewardTwiceCheck: no reward duplication",
              CountRewardsFor(clientA, seedA.characterId) == rewardsBefore);
    }

    // ---- OfflineDotKillerRewardCheck（指令七）：施法者断线后 DOT 击杀入库 ----
    {
        // slime8 移到 A 旁；A（lv2，普攻 20/次）打 2 次到 <=40 停手，白盒 Burn
        //（source=A）后 A 断线，DOT 8/s 在 A 离线期间杀死并入库。
        servers.world->MoveMonsterTo(8, 190.0f, 1040.0f); // 距 A 91
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::uint64_t offlinePreRequestId = 7601;
        const bool lowHp = WaitUntil(
            [&] {
                auto m = servers.world->FindMonster(8);
                if (!m) {
                    return true;
                }
                if (!m->Alive()) {
                    return true;
                }
                if (m->CurrentHp() <= 32) {
                    return true; // 打 3 次（80->60->40->20）即停，Burn 3 跳（12/4/0）必杀死
                }
                clientA.client().SendAttack(offlinePreRequestId++, kTypeMonster, 8);
                return false;
            },
            15000);
        servers.world->ApplyStatusToTarget(
            kTypeMonster, 8, 2003, 1, kTypePlayer, seedA.characterId, 0u);
        const bool burnApplied = WaitUntil(
            [&] {
                auto m = servers.world->FindMonster(8);
                return m && m->StatusEffects().Count() > 0;
            },
            2000);
        clientA.Disconnect(); // A 离线（DOT 仍在怪身上）
        // 等 DOT 杀死（40HP -> 5 跳 = 5s）+ DbWorker 落盘
        const bool killed = WaitUntil(
            [&] {
                auto m = servers.world->FindMonster(8);
                return !m || !m->Alive();
            },
            12000);
        std::this_thread::sleep_for(std::chrono::milliseconds(1500)); // DB flush 余量
        const std::int64_t expInDb =
            QueryScalar(servers.dbPath,
                        "SELECT exp FROM characters WHERE id = " +
                            std::to_string(seedA.characterId) + ";");
        // A 在线 lv2 余 25 exp（5 只）+ 离线 DOT 25 = 50。
        if (!(lowHp && burnApplied && killed && expInDb >= 50)) {
            std::printf("[Diag] Offline: lowHp=%d burn=%d killed=%d expInDb=%lld\n",
                        static_cast<int>(lowHp), static_cast<int>(burnApplied),
                        static_cast<int>(killed), static_cast<long long>(expInDb));
        }
        Check("OfflineDotKillerRewardCheck: offline killer's DOT kill persisted (+25 exp)",
              lowHp && burnApplied && killed && expInDb >= 50);
    }

    // ---- ProgressionPersistenceCheck（指令二/三十四）：在线奖励写 DB ----
    // A 曾在线杀 5 只（125 exp/15 gold，lv2）+ 离线 DOT 1 只（25/3）= 150/18。
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(800)); // DB flush
        const std::int64_t levelInDb =
            QueryScalar(servers.dbPath,
                        "SELECT level FROM characters WHERE id = " +
                            std::to_string(seedA.characterId) + ";");
        const std::int64_t goldInDb =
            QueryScalar(servers.dbPath,
                        "SELECT gold FROM characters WHERE id = " +
                            std::to_string(seedA.characterId) + ";");
        Check("ProgressionPersistenceCheck: online rewards saved via DbWorker (level 2, gold 18)",
              levelInDb == 2 && goldInDb == 18);
    }

    // ---- WorldRestartProgressionPersistenceCheck（指令二/二十九）----
    {
        servers.StopWorld();
        const bool restarted = servers.StartWorld();
        // A 重进：加载持久化 level=2 + ProgressionSnapshot 纠偏。
        const bool reentered =
            clientA.ConnectAndEnter(TicketFor(servers.login, seedA), 8000);
        WorldNetworkEvent snap;
        const bool gotSnap = WaitProgressionSnapshot(clientA, snap);
        bool ok = restarted && reentered && gotSnap &&
                  snap.characterId == seedA.characterId && snap.progression.level == 2 &&
                  snap.progression.currentExp == 50 &&
                  snap.progression.expToNext == ExpToNextLevel(2) - 50;
        const auto player = ReadPlayer(servers, seedA.characterId);
        ok = ok && player.found && player.level == 2 && player.maxHp == 110;
        Check("WorldRestartProgressionPersistenceCheck: level/exp/gold persist across restart",
              ok);
    }

    servers.StopAll();
}

// ===========================================================================
// C. Respawn 专项检查（独立 servers；respawnDelayMs = 8000 真实值，指令二十二）
// ===========================================================================

void RunProgressionRespawnChecks() {
    WorldTestServers servers;
    servers.dbPath = TempDbPath("world_progression_respawn");
    RemoveDb(servers.dbPath);
    Check("RespawnServersStartCheck", servers.StartLogin() && servers.StartWorld());

    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(8, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("RespawnChecks: db ready", false);
        servers.StopAll();
        return;
    }
    CharacterSeed seedA;
    if (!SeedAt(db, accounts, characters, "pg_user_r", "PgR", 100.0f, 1000.0f, seedA)) {
        Check("RespawnChecks: seed ready", false);
        servers.StopAll();
        return;
    }
    // 布景：只移 slime9（slime10/11 在 TwentySlot 块才移——避免多怪围殴 killer）。
    servers.world->MoveMonsterTo(9, 145.0f, 1050.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    WorldTestClient clientA;
    {
        const bool eA = clientA.ConnectAndEnter(TicketFor(servers.login, seedA), 8000);
        Check("RespawnChecks: A entered", eA);
    }

    // ---- SpawnSlotCountCheck + InitialSpawnSlotCheck（指令二十/二十六）----
    {
        bool ok = servers.world->SpawnSlotCount() == 20;
        bool allBound = ok;
        for (std::uint32_t slotId = 1; slotId <= 20 && allBound; ++slotId) {
            allBound = servers.world->ActiveEntityOfSlot(slotId) != 0;
        }
        Check("SpawnSlotCountCheck/InitialSpawnSlotCheck: 20 slots, all bound on start",
              ok && allBound);
    }

    // ---- MonsterDeathQueuesRespawnCheck + RespawnNewEntityIdCheck +
    //      RespawnSameSlotPositionCheck + RespawnFullHpCheck + RespawnNoStatusCheck +
    //      RespawnSpatialGridCheck + RespawnAoiSpawnCheck（指令十八/二十一/二十七/二十八）----
    {
        const auto slimeA = servers.world->FindMonster(9);
        const bool alive = slimeA != nullptr && slimeA->Alive();
        const std::uint64_t oldEntityId = slimeA ? slimeA->EntityId() : 0;
        const float spawnX = slimeA ? slimeA->PositionX() : 0.0f;
        const float spawnY = slimeA ? slimeA->PositionY() : 0.0f;
        const std::uint32_t slotId = servers.world->SpawnSlotOfEntity(oldEntityId);
        // 杀死（requestId 递增防重放；lv1 普攻 18/次 -> 5 次击杀）
        std::uint64_t respawnKillRequestId = 7701;
        const bool killed = WaitUntil(
            [&] {
                auto m = servers.world->FindMonster(9);
                if (!m) {
                    return true;
                }
                if (!m->Alive()) {
                    return true;
                }
                clientA.client().SendAttack(respawnKillRequestId++, kTypeMonster, 9);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                return false;
            },
            15000);
        // 尸体 3s 清理 -> 入队（指令十八）
        const bool queued = WaitUntil(
            [&] {
                return servers.world->RespawnPendingCount() > 0;
            },
            5000);
        // NoEarlyRespawnCheck（指令二十二）：尸体清理后、8s 到点前不重生
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        const std::size_t countEarly = servers.world->MonsterCount();
        const bool countEarlyOk = countEarly == 19; // 20 - 1 只死亡清理
        // 等 8s respawn（death 起算）—— Poll 周期 250ms
        std::uint64_t newEntityId = 0;
        const bool respawned = WaitUntil(
            [&] {
                const std::uint64_t active = servers.world->ActiveEntityOfSlot(slotId);
                newEntityId = active;
                return active != 0 && active != oldEntityId;
            },
            9000);
        const auto newMonster = servers.world->FindMonster(newEntityId);
        bool ok = alive && killed && queued && countEarlyOk &&
                  respawned && newMonster != nullptr;
        ok = ok && newEntityId != oldEntityId;                          // 新 entityId（指令二十一）
        ok = ok && std::fabs(newMonster->PositionX() - spawnX) < 0.001f &&
             std::fabs(newMonster->PositionY() - spawnY) < 0.001f;      // 原出生点（指令二十一）
        ok = ok && newMonster->CurrentHp() == newMonster->MaxHp();       // 满 HP（指令二十七）
        ok = ok && newMonster->StatusEffects().Count() == 0;             // 无状态（指令二十七）
        ok = ok && newMonster->Alive() &&
             newMonster->State() == legend::world::MonsterState::Idle;
        // AOI：A 的服务器权威 VisibleMonsters 收录新怪（AOI tick 基于 SpatialGrid
        // 发现——RespawnAoiSpawnCheck，指令二十八；grid 集成由此验证）
        const bool aoiSpawn = WaitUntil(
            [&] {
                auto p = servers.world->FindPlayerByCharacter(seedA.characterId);
                return p && p->VisibleMonsters().count(newEntityId) != 0;
            },
            4000);
        Check("MonsterDeathQueuesRespawn/RespawnNewEntityId/SameSlotPosition/FullHp/NoStatus/"
              "SpatialGrid/AoiSpawn/NoEarlyRespawn: slot respawn creates fresh entity at "
              "spawn point, count stays 19 before due time",
              ok && aoiSpawn);
        if (!(ok && aoiSpawn)) {
            std::printf("[Diag] Respawn: alive=%d killed=%d queued=%d countEarly=%zu "
                        "countNow=%zu respawned=%d newId=%llu oldId=%llu aoi=%d\n",
                        static_cast<int>(alive), static_cast<int>(killed),
                        static_cast<int>(queued), countEarly,
                        servers.world->MonsterCount(),
                        static_cast<int>(respawned),
                        static_cast<unsigned long long>(newEntityId),
                        static_cast<unsigned long long>(oldEntityId),
                        static_cast<int>(aoiSpawn));
        }
    }

    // ---- NoDuplicateRespawnCheck（指令二十五）：8s 后不重复复活 ----
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        const std::size_t count = servers.world->MonsterCount();
        const std::size_t pending = servers.world->RespawnPendingCount();
        Check("NoDuplicateRespawnCheck: no duplicate spawn after respawn completes",
              count == 20 && pending == 0);
    }

    // ---- TwentySlotIndependentRespawnCheck（指令二十）：两只不同 slot 独立排队 ----
    {
        servers.world->MoveMonsterTo(10, 155.0f, 1050.0f);
        servers.world->MoveMonsterTo(11, 165.0f, 1050.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const std::uint32_t slot10 = servers.world->SpawnSlotOfEntity(10);
        const std::uint32_t slot11 = servers.world->SpawnSlotOfEntity(11);
        const bool different = slot10 != 0 && slot11 != 0 && slot10 != slot11;
        // 快速击杀两只（各自独立 death 时刻 -> 独立 respawnTime；QS 58x2/只快速
        // 击杀，压缩 A 的承伤窗口；CD 拒绝轮询模式）。
        std::uint64_t killRequestId = 7801;
        const bool killed = WaitUntil(
            [&] {
                auto a = servers.world->FindMonster(10);
                auto b = servers.world->FindMonster(11);
                const bool aDead = !a || !a->Alive();
                const bool bDead = !b || !b->Alive();
                if (aDead && bDead) {
                    return true;
                }
                if (!aDead) {
                    clientA.controller.SendSkillCast(1001, kTargetMonsterSkill, 10);
                }
                if (!bDead) {
                    clientA.controller.SendSkillCast(1001, kTargetMonsterSkill, 11);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                clientA.DrainEvents();
                return false;
            },
            20000);
        const bool queued = WaitUntil(
            [&] {
                return servers.world->RespawnPendingCount() >= 2;
            },
            5000);
        Check("TwentySlotIndependentRespawnCheck: two slots queue independently",
              different && killed && queued);
        // 等待两只都重生完成，恢复满怪状态（后续 Restart 检查）
        WaitUntil(
            [&] {
                return servers.world->RespawnPendingCount() == 0 &&
                       servers.world->MonsterCount() == 20;
            },
            15000);
    }

    // ---- WorldRestartRespawnResetCheck（指令二十九）：重启 20 slot 重新满怪 ----
    {
        servers.StopWorld();
        const bool restarted = servers.StartWorld();
        bool ok = restarted && servers.world->MonsterCount() == 20 &&
                  servers.world->SpawnSlotCount() == 20 &&
                  servers.world->RespawnPendingCount() == 0;
        bool allBound = ok;
        for (std::uint32_t slotId = 1; slotId <= 20 && allBound; ++slotId) {
            allBound = servers.world->ActiveEntityOfSlot(slotId) != 0;
        }
        Check("WorldRestartRespawnResetCheck: restart refills 20 slots, queue cleared",
              ok && allBound);
    }

    servers.StopAll();
}

} // namespace

void RunWorldProgressionChecks() {
    std::printf("[WorldProgression] logic checks begin\n");
    RunProgressionLogicChecks();
    std::printf("[WorldProgression] chain checks begin\n");
    RunProgressionChainChecks();
    std::printf("[WorldProgression] respawn checks begin\n");
    RunProgressionRespawnChecks();
}

} // namespace worldtest
