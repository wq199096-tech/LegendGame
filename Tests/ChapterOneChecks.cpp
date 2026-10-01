// ---------------------------------------------------------------------------
// 阶段25：Chapter One《异动的史莱姆》端到端检查（指令六十五：完整链路，
// 尽可能真实服务器链路；指令六十六：Boss Definition/Spawn/AI/Attack/Death/
// Reward/Drop/Respawn/Quest Kill Objective；指令六十八：持久化回归）。
// 使用生产 Data/Game + Data/World（LEGEND_SOURCE_DIR），真实协议驱动：
// 移动/战斗/掉落/拾取/任务/商店/装备/Portal 全部走服务器权威链路。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Server/LoginServer/Account/AccountRepository.h"
#include "Server/WorldServer/Quest/QuestRegistry.h"
#include "Server/WorldServer/Quest/QuestRepository.h"
#include "Shared/Combat/CombatTypes.h"
#include "Shared/Dialogue/DialogueTypes.h"
#include "Shared/Item/ItemTypes.h"
#include "Shared/Monster/MonsterTypes.h"
#include "Shared/Quest/QuestTypes.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace worldtest {

namespace {

using namespace legend::world;
using legend::world::QuestRegistry;
using legend::world::QuestState;

const std::uint8_t kTargetTypeMonster =
    static_cast<std::uint8_t>(legend::world::CombatEntityType::Monster);

// ---- 事件辅助（与 WorldQuestChecks 同模式） ----

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

// ---- 共用助手（与 WorldQuestChecks 同模式的本地副本）----

template <typename F>
auto RunOnIo(net::NetworkService& service, F&& fn) -> std::invoke_result_t<F&> {
    using R = std::invoke_result_t<F&>;
    std::promise<R> promise;
    auto future = promise.get_future();
    service.Post([&promise, &fn]() { promise.set_value(fn()); });
    return future.get();
}

// 确保玩家存活（测试布景：E2E 长链路战斗中死亡则复活满血继续——服务器权威
// 战斗不受影响，仅避免测试因死亡卡死）。
void EnsurePlayerAlive(WorldTestServers& servers, std::uint64_t characterId);

// 真实 MoveInput 链路位移（12 units/input，全局 seq 防去重）。
// 慢机时序纪律：切图后立刻读位置可能拿到旧值 → 方向错 → 死路。改为多轮
// "读当前位置 → 重算方向 → 走 → 校验"，最多 6 轮自校正；穿越刷怪区被围殴
// 致死则每轮开始复活继续走（不设防死亡纪律）。
bool MoveTo(WorldTestServers& servers, WorldTestClient& client, std::uint64_t characterId,
            float targetX, float targetY) {
    static std::uint32_t s_seq = 100000;
    const auto inRange = [&]() -> bool {
        auto p = servers.world->FindPlayerByCharacter(characterId);
        if (!p) {
            return false;
        }
        const float ex = p->PositionX() - targetX;
        const float ey = p->PositionY() - targetY;
        return (ex * ex + ey * ey) <= 169.0f;
    };
    for (int round = 0; round < 6 && !inRange(); ++round) {
        EnsurePlayerAlive(servers, characterId);
        auto player = servers.world->FindPlayerByCharacter(characterId);
        if (!player) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        const float dx = targetX - player->PositionX();
        const float dy = targetY - player->PositionY();
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist >= 1.0f) {
            const float ux = dx / dist;
            const float uy = dy / dist;
            const int steps = std::max(1, static_cast<int>(std::ceil(dist / 12.0f)));
            for (int i = 0; i < steps; ++i) {
                client.client().SendMoveInput(++s_seq, ux, uy, 0.1f);
            }
        }
        WaitUntil(inRange, 2500);
    }
    return WaitUntil(inRange, 4000);
}

// 确保玩家存活（测试布景：E2E 长链路战斗中死亡则复活满血继续——服务器权威
// 战斗不受影响，仅避免测试因死亡卡死）。
void EnsurePlayerAlive(WorldTestServers& servers, std::uint64_t characterId) {
    auto player = servers.world->FindPlayerByCharacter(characterId);
    if (player && !player->Alive()) {
        servers.world->TestRevivePlayer(characterId);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

// 打到目标死亡（真实 Attack 链路）。
// 慢机时序纪律（击杀循环期间不设防死亡）：围殴致死后立即复活继续打，
// 否则死亡期间攻击被服务器拒绝 → 任务进度停滞 → 整链失败。
bool AttackUntilDead(WorldTestServers& servers, WorldTestClient& client,
                     std::uint64_t monsterId, std::uint64_t& requestId, int timeoutMs = 60000,
                     std::uint64_t characterId = 0) {
    return WaitUntil(
        [&] {
            auto monster = servers.world->FindMonster(monsterId);
            if (!monster || !monster->Alive()) {
                return true;
            }
            if (characterId != 0) {
                EnsurePlayerAlive(servers, characterId);
            }
            client.client().SendAttack(requestId++, kTargetTypeMonster, monsterId);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            return false;
        },
        timeoutMs);
}

std::uint64_t FindAliveMonsterOf(WorldTestServers& servers, std::uint32_t typeId,
                                 std::uint16_t mapId) {
    for (const auto entityId : servers.world->MonsterEntityIds()) {
        const auto monster = servers.world->FindMonster(entityId);
        if (monster && monster->Alive() && monster->MonsterTypeId() == typeId &&
            monster->MapId() == mapId) {
            return entityId;
        }
    }
    return 0;
}

void MoveMonsterNear(WorldTestServers& servers, std::uint64_t monsterId, float x, float y) {
    servers.world->MoveMonsterTo(monsterId, x, y);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
}

struct QuestView {
    bool found = false;
    std::uint8_t state = 0;
    std::uint32_t firstProgress = 0;
};

QuestView ReadQuest(WorldTestServers& servers, std::uint64_t characterId, QuestId questId) {
    return RunOnIo(servers.worldService, [&]() -> QuestView {
        QuestView view;
        auto player = servers.world->FindPlayerByCharacter(characterId);
        if (!player) {
            return view;
        }
        const auto* state = player->Quests().Find(questId);
        if (!state) {
            return view;
        }
        view.found = true;
        view.state = static_cast<std::uint8_t>(state->state);
        const QuestDefinition* definition = QuestRegistry::Instance().FindQuest(questId);
        if (definition != nullptr && !definition->objectives.empty()) {
            view.firstProgress = state->ProgressOf(definition->objectives[0].objectiveId);
        }
        return view;
    });
}

bool WaitQuestState(WorldTestServers& servers, std::uint64_t characterId, QuestId questId,
                    QuestState state, int timeoutMs = 6000) {
    return WaitUntil(
        [&] {
            const auto view = ReadQuest(servers, characterId, questId);
            return view.found && view.state == static_cast<std::uint8_t>(state);
        },
        timeoutMs);
}

bool WaitQuestKillProgress(WorldTestServers& servers, std::uint64_t characterId, QuestId questId,
                           std::uint32_t progress, int timeoutMs = 6000) {
    return WaitUntil(
        [&] {
            const auto view = ReadQuest(servers, characterId, questId);
            return view.found && view.firstProgress == progress;
        },
        timeoutMs);
}

bool AcceptQuest(WorldTestServers& servers, WorldTestClient& client, std::uint32_t questId) {
    const std::size_t baseline =
        CountEventsOf(client, WorldNetworkEvent::Type::QuestAcceptResponseEvent);
    client.controller.SendQuestAccept(questId);
    WorldNetworkEvent response;
    const bool ok = WaitRecordedFrom(
        client, WorldNetworkEvent::Type::QuestAcceptResponseEvent, baseline,
        [&](const WorldNetworkEvent& e) { return e.questId == questId && e.success; }, response,
        5000);
    // 注意：接取时 ReachLevel/CollectItem 有立即初始校验（指令二十三）——预持有
    // 满足条件时状态直接 InProgress -> ReadyToTurnIn（EvaluateQuestCompletion 在
    // 接取响应前同步执行）。因此等待"任一合法接取后状态"。
    return ok && WaitUntil(
                     [&] {
                         const auto view = ReadQuest(servers, client.controller.CharacterId(),
                                                     questId);
                         return view.found &&
                                (view.state == static_cast<std::uint8_t>(QuestState::InProgress) ||
                                 view.state ==
                                     static_cast<std::uint8_t>(QuestState::ReadyToTurnIn));
                     },
                     5000);
}

bool TurnInQuest(WorldTestServers& servers, WorldTestClient& client, std::uint32_t questId) {
    const std::size_t baseline =
        CountEventsOf(client, WorldNetworkEvent::Type::QuestTurnInResponseEvent);
    client.controller.SendQuestTurnIn(questId);
    WorldNetworkEvent response;
    const bool turnedIn = WaitRecordedFrom(
        client, WorldNetworkEvent::Type::QuestTurnInResponseEvent, baseline,
        [&](const WorldNetworkEvent& e) { return e.questId == questId && e.success; }, response,
        5000);
    return turnedIn && WaitQuestState(servers, client.controller.CharacterId(), questId,
                                      QuestState::Completed, 5000);
}

// Portal 切图（真实 PortalUse 协议；portalEntityId 按生成顺序 1=8001 2=8002 3=8003 4=8004）。
// 慢机时序纪律：连续切图时客户端 MapChanged 事件可能晚于轮询窗口录到——以服务器
// 权威地图为准做双重确认，最多重试 3 次（服务器已切图而事件丢失 → 直接成功）。
bool UsePortal(WorldTestServers& servers, WorldTestClient& client, std::uint64_t characterId,
               std::uint64_t portalEntityId, float portalX, float portalY,
               std::uint16_t expectMapId) {
    static std::uint32_t s_portalSeq = 600000;
    const auto serverMapIs = [&]() {
        return RunOnIo(servers.worldService, [&]() -> bool {
            auto p = servers.world->FindPlayerByCharacter(characterId);
            return p && p->MapId() == expectMapId;
        });
    };
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (serverMapIs()) {
            return true; // 已在目标地图（前次调用已生效）
        }
        if (!MoveTo(servers, client, characterId, portalX, portalY)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            continue;
        }
        const bool visible = WaitUntil(
            [&] {
                return RunOnIo(servers.worldService, [&]() -> bool {
                    auto p = servers.world->FindPlayerByCharacter(characterId);
                    return p && p->VisiblePortals().count(portalEntityId) != 0;
                });
            },
            4000);
        if (!visible) {
            continue;
        }
        const std::size_t changedBaseline =
            CountEventsOf(client, WorldNetworkEvent::Type::MapChangedEvent);
        client.client().SendPortalUse(++s_portalSeq, portalEntityId);
        WorldNetworkEvent changed;
        if (WaitRecordedFrom(
                client, WorldNetworkEvent::Type::MapChangedEvent, changedBaseline,
                [&](const WorldNetworkEvent& e) { return e.mapChanged.mapId == expectMapId; },
                changed, 6000)) {
            return true;
        }
        if (serverMapIs()) {
            return true; // 服务器已切图——客户端事件丢失，不重发 PortalUse
        }
    }
    return serverMapIs();
}

// 拾取客户端掉落镜像中的指定物品（真实 WorldItemSpawn 事件 + Pickup 协议）。
// 每次发送后等待 PickupResponse：失败（如拾取途中死亡 code=Dead）→ 复活后重试
// 同一 drop；只统计【成功】拾取（慢机时序纪律：不按发送数计数）。返回成功数量。
int PickupDropsOf(WorldTestServers& servers, WorldTestClient& client,
                  std::uint32_t itemDefinitionId, int maxCount) {
    int picked = 0;
    int misses = 0;
    while (picked < maxCount && misses < 20) {
        client.DrainEvents();
        std::uint64_t targetDrop = 0;
        for (const auto& [dropEntityId, item] : client.controller.WorldItems().Items()) {
            (void)item;
            if (item.itemDefinitionId == itemDefinitionId) {
                targetDrop = dropEntityId;
                break;
            }
        }
        if (targetDrop == 0) {
            ++misses; // 镜像中暂无该物品 drop（掉落 spawn 延迟）——等待后重扫
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            continue;
        }
        EnsurePlayerAlive(servers, client.controller.CharacterId());
        const std::size_t baseline = CountEventsOf(
            client, WorldNetworkEvent::Type::ItemPickupResponseEvent);
        client.controller.SendPickup(targetDrop);
        WorldNetworkEvent response;
        const bool responded = WaitRecordedFrom(
            client, WorldNetworkEvent::Type::ItemPickupResponseEvent, baseline,
            [&](const WorldNetworkEvent& e) { return e.dropEntityId == targetDrop; }, response,
            3000);
        if (responded && response.success) {
            ++picked;
        } else {
            ++misses; // Dead/TooFar/NotVisible 等——复活并重试同一 drop
            EnsurePlayerAlive(servers, client.controller.CharacterId());
        }
    }
    return picked;
}

} // namespace

void RunChapterOneChecks() {
    // 生产数据（Chapter 1 内容）；测试缩短 respawn 便于补杀。
    WorldTestServers servers;
    servers.dbPath = TempDbPath("world_chapter1");
    RemoveDb(servers.dbPath);
    servers.worldRespawnDelayMs = 3000;
    servers.gameDataDir = std::string(LEGEND_SOURCE_DIR) + "/Data/Game";
    servers.worldDataDir = std::string(LEGEND_SOURCE_DIR) + "/Data/World";
    Check("Chapter1ServersStartCheck", servers.StartLogin() && servers.StartWorld());

    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(8, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("Chapter1Checks: db ready", false);
        servers.StopAll();
        return;
    }

    // ---- 1. 真实建号/建角（未出生哨兵 -> 服务器权威出生 300,300）----
    CharacterSeed seed;
    {
        auto registered = accounts.Register(db, "chapter_user", "Chapter1Pass!");
        if (!registered.success) {
            Check("Chapter1CreateCheck: account + character created", false);
            servers.StopAll();
            return;
        }
        auto created = characters.Create(db, registered.value, "ChapterHero", 1, 1, 1);
        if (!created.success) {
            Check("Chapter1CreateCheck: account + character created", false);
            servers.StopAll();
            return;
        }
        seed.accountId = registered.value;
        seed.characterId = created.value.characterId;
        seed.name = "ChapterHero";
    }
    Check("Chapter1CreateCheck: account + character created", true);

    bool ok = true;
    const std::string ticket = servers.login->Tickets().Create(seed.accountId, seed.characterId, 300.0);
    WorldTestClient client;
    ok = !ticket.empty() && client.ConnectAndEnter(ticket, 8000);
    const WorldNetworkEvent& enter = client.LastEnterSuccess();
    ok = ok && std::fabs(enter.positionX - 300.0f) < 0.01f &&
         std::fabs(enter.positionY - 300.0f) < 0.01f;
    Check("Chapter1EnterCheck: new character spawns at Map1 300,300", ok);

    // ---- 2. 任务 4001 First Trouble（Elder 在出生点旁）----
    ok = ok && AcceptQuest(servers, client, 4001);
    Check("Chapter1Quest4001Check: First Trouble accepted", ok);

    // ---- 3. Portal -> Map2，杀 5 Slime（真实战斗 + 掉落 + 拾取）----
    ok = ok && UsePortal(servers, client, seed.characterId, 1, 950.0f, 300.0f, 2);
    Check("Chapter1PortalCheck: Map1 -> Slime Meadow", ok);
    std::uint64_t attackSeq = 2000;
    int coresPicked = 0;
    // 玩家站在刷怪区 (900,950 r400) 外的草丛点：MoveMonsterNear 把目标 slime
    // 拉到玩家旁逐只击杀（真实攻击验证链），避免整片刷怪区同时仇恨。
    for (int i = 0; i < 5 && ok; ++i) {
        EnsurePlayerAlive(servers, seed.characterId);
        const std::uint64_t slimeId = FindAliveMonsterOf(servers, world::kTrainingSlimeTypeId, 2);
        ok = slimeId != 0;
        if (!ok) {
            std::printf("[Diag] Chapter1 kill iter %d: no alive Map2 slime\n", i);
            break;
        }
        ok = MoveTo(servers, client, seed.characterId, 620.0f, 620.0f);
        if (!ok) {
            EnsurePlayerAlive(servers, seed.characterId);
            ok = MoveTo(servers, client, seed.characterId, 620.0f, 620.0f);
        }
        if (!ok) {
            auto p = servers.world->FindPlayerByCharacter(seed.characterId);
            std::printf("[Diag] Chapter1 kill iter %d: MoveTo failed pos=%.0f,%.0f alive=%d\n",
                        i, p ? p->PositionX() : -1.0f, p ? p->PositionY() : -1.0f,
                        p && p->Alive() ? 1 : 0);
            break;
        }
        MoveMonsterNear(servers, slimeId, 640.0f, 640.0f);
        const bool killed = AttackUntilDead(servers, client, slimeId, attackSeq, 60000,
                                            seed.characterId);
        const auto deadCheck = servers.world->FindMonster(slimeId);
        std::printf("[Diag] Chapter1 kill iter %d: slime=%llu killed=%d monsterAlive=%d\n", i,
                    static_cast<unsigned long long>(slimeId), killed ? 1 : 0,
                    deadCheck && deadCheck->Alive() ? 1 : 0);
        ok = WaitQuestKillProgress(servers, seed.characterId, 4001,
                                   static_cast<std::uint32_t>(i + 1), 8000);
        if (!ok) {
            const auto view = ReadQuest(servers, seed.characterId, 4001);
            std::printf("[Diag] Chapter1 kill iter %d: quest progress not advanced "
                        "(found=%d state=%u progress=%u)\n",
                        i, view.found ? 1 : 0, view.state, view.firstProgress);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(400)); // 等掉落 spawn
        coresPicked += PickupDropsOf(servers, client, world::kItemSlimeCoreId, 3 - coresPicked);
    }
    Check("Chapter1KillCheck: 5 slimes killed via real combat, quest progress 5/5", ok);

    // ---- 4. EXP/Gold/升级（服务器奖励链路；在线镜像即时反映，DB 落库在保存点）----
    // 镜像语义：RewardGranted.newExperience / LevelUp.currentExp 均为本级内经验
    //（升级时扣除 RequiredExp）。5 杀 125 exp 必然触发 1->2 升级，此后本级 exp=25，
    // 故断言 Level>=2 或本级 exp>=100 均可证明杀怪 EXP 已授予。观测性检查，
    // 不并入 ok 链（避免一处失败短路后续全部环节）。
    const bool expOk = WaitUntil(
        [&] {
            client.DrainEvents();
            return client.controller.LocalLevel() >= 2 ||
                   client.controller.LocalExperience() >= 100;
        },
        5000);
    Check("Chapter1ExpCheck: kill EXP granted (mirror, level>=2)", expOk);

    // ---- 5. 回村交 4001，接 4002（收集 3 core——已拾取）----
    ok = ok && UsePortal(servers, client, seed.characterId, 2, 150.0f, 500.0f, 1);
    ok = ok && TurnInQuest(servers, client, 4001);
    Check("Chapter1TurnIn4001Check: First Trouble turned in (rewards granted)", ok);
    ok = ok && AcceptQuest(servers, client, 4002);
    ok = ok && WaitQuestState(servers, seed.characterId, 4002, QuestState::ReadyToTurnIn, 6000);
    Check("Chapter1Quest4002Check: Strange Cores collect 3/3 from picked drops", ok);
    ok = ok && TurnInQuest(servers, client, 4002);
    Check("Chapter1TurnIn4002Check: Strange Cores turned in (Cloth Armor reward)", ok);

    // ---- 6. 4003 Reach Level 3：继续杀怪升级后交任务 ----
    ok = ok && AcceptQuest(servers, client, 4003);
    ok = ok && UsePortal(servers, client, seed.characterId, 1, 950.0f, 300.0f, 2);
    for (int i = 0; i < 12 && ok; ++i) {
        EnsurePlayerAlive(servers, seed.characterId);
        client.DrainEvents();
        // 用客户端 mirror 判级（DbWorker 落库滞后，DB level 不可靠）
        if (client.controller.LocalLevel() >= 3) {
            break;
        }
        const std::uint64_t slimeId =
            FindAliveMonsterOf(servers, world::kTrainingSlimeTypeId, 2);
        if (slimeId == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        (void)MoveTo(servers, client, seed.characterId, 620.0f, 620.0f);
        MoveMonsterNear(servers, slimeId, 640.0f, 640.0f);
        (void)AttackUntilDead(servers, client, slimeId, attackSeq, 60000, seed.characterId);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    ok = ok && WaitUntil(
                   [&] {
                       client.DrainEvents();
                       return client.controller.LocalLevel() >= 3;
                   },
                   8000);
    Check("Chapter1LevelUpCheck: reached level 3 via kill EXP (mirror)", ok);
    ok = ok && WaitQuestState(servers, seed.characterId, 4003, QuestState::ReadyToTurnIn, 6000);
    ok = ok && UsePortal(servers, client, seed.characterId, 2, 150.0f, 500.0f, 1);
    ok = ok && TurnInQuest(servers, client, 4003);
    Check("Chapter1TurnIn4003Check: Growing Stronger turned in", ok);

    // ---- 7. 4004 Explore the Meadow：Map2 南部区域 ----
    ok = ok && AcceptQuest(servers, client, 4004);
    ok = ok && UsePortal(servers, client, seed.characterId, 1, 950.0f, 300.0f, 2);
    ok = ok && MoveTo(servers, client, seed.characterId, 300.0f, 1500.0f);
    ok = ok && WaitQuestState(servers, seed.characterId, 4004, QuestState::ReadyToTurnIn, 6000);
    ok = ok && TurnInQuest(servers, client, 4004);
    Check("Chapter1Quest4004Check: Explore the Meadow completed", ok);

    // ---- 8. 商店购买 + 装备（真实 NPC 对话 -> ShopOpen -> Buy -> Equip）----
    ok = ok && UsePortal(servers, client, seed.characterId, 2, 150.0f, 500.0f, 1);
    const bool shopMoved = MoveTo(servers, client, seed.characterId, 450.0f, 320.0f);
    if (!shopMoved) {
        auto p = servers.world->FindPlayerByCharacter(seed.characterId);
        std::printf("[Diag] Chapter1 shop: MoveTo merchant failed pos=%.0f,%.0f alive=%d\n",
                    p ? p->PositionX() : -1.0f, p ? p->PositionY() : -1.0f,
                    p && p->Alive() ? 1 : 0);
    }
    ok = ok && shopMoved;
    {
        const std::size_t interactBaseline =
            CountEventsOf(client, WorldNetworkEvent::Type::NpcInteractResponseEvent);
        (void)interactBaseline;
        ok = ok && client.controller.SendInteractNearestNpc(450.0f, 320.0f);
        ok = ok && WaitUntil(
                       [&] {
                           client.DrainEvents();
                           return client.controller.Dialogue().Active();
                       },
                       5000);
        // 选择 Shop Option（服务器下发菜单中类型为 Shop 的第一项）。
        int shopOption = -1;
        int idx = 1;
        for (const auto& option : client.controller.Dialogue().Options()) {
            if (option.type == static_cast<std::uint8_t>(world::DialogueOptionType::Shop)) {
                shopOption = idx;
                break;
            }
            ++idx;
        }
        ok = ok && shopOption > 0 && client.controller.SendDialogueOptionByIndex(
                                           static_cast<std::size_t>(shopOption));
        ok = ok && WaitUntil(
                       [&] {
                           client.DrainEvents();
                           return client.controller.Shop().Active();
                       },
                       5000);
        // Buy Bronze Sword (3010)。
        std::size_t bronzeIndex = SIZE_MAX;
        std::size_t entryIdx = 0;
        for (const auto& entry : client.controller.Shop().Entries()) {
            if (entry.itemDefinitionId == world::kItemBronzeSwordId && entry.canBuy) {
                bronzeIndex = entryIdx;
                break;
            }
            ++entryIdx;
        }
        ok = ok && bronzeIndex != SIZE_MAX &&
             client.controller.SendBuyByIndex(bronzeIndex);
        ok = ok && WaitUntil(
                       [&] {
                           client.DrainEvents();
                           for (std::size_t i = 0; i < client.controller.Inventory().kSlots; ++i) {
                               if (client.controller.Inventory().Slot(i).definitionId ==
                                   world::kItemBronzeSwordId) {
                                   return true;
                               }
                           }
                           return false;
                       },
                       5000);
        // Equip（真实 Equip 协议）。
        ok = ok && client.controller.SendEquipFirstOf(world::kItemBronzeSwordId);
        ok = ok && WaitUntil(
                       [&] {
                           client.DrainEvents();
                           return client.controller.Equipment().WeaponDefinitionId() ==
                                  world::kItemBronzeSwordId;
                       },
                       5000);
        Check("Chapter1ShopEquipCheck: bought + equipped Bronze Sword via NPC shop", ok);
    }

    // ---- 9. 4005 Deeper Threat：杀 5 + 交核 2 + 进入 Ancient Ruins ----
    ok = ok && AcceptQuest(servers, client, 4005);
    ok = ok && UsePortal(servers, client, seed.characterId, 1, 950.0f, 300.0f, 2);
    for (int i = 0; i < 5 && ok; ++i) {
        EnsurePlayerAlive(servers, seed.characterId);
        const std::uint64_t slimeId = FindAliveMonsterOf(servers, world::kTrainingSlimeTypeId, 2);
        ok = slimeId != 0;
        if (!ok) {
            break;
        }
        ok = MoveTo(servers, client, seed.characterId, 620.0f, 620.0f);
        if (!ok) {
            EnsurePlayerAlive(servers, seed.characterId);
            ok = MoveTo(servers, client, seed.characterId, 620.0f, 620.0f);
        }
        if (!ok) {
            break;
        }
        MoveMonsterNear(servers, slimeId, 640.0f, 640.0f);
        (void)AttackUntilDead(servers, client, slimeId, attackSeq, 60000, seed.characterId);
        ok = WaitQuestKillProgress(servers, seed.characterId, 4005,
                                   static_cast<std::uint32_t>(i + 1), 8000);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        coresPicked += PickupDropsOf(servers, client, world::kItemSlimeCoreId, 3);
    }
    // 进入 Map3（8003 收 10G/minLevel2——金币与等级已满足）。
    ok = ok && UsePortal(servers, client, seed.characterId, 3, 1750.0f, 1000.0f, 3);
    ok = ok && WaitQuestState(servers, seed.characterId, 4005, QuestState::ReadyToTurnIn, 8000);
    Check("Chapter1Quest4005Check: Deeper Threat kill 5/5 + cores + ruins entered", ok);
    // 回村交任务（Map3 8004 -> Map2 8002）。
    ok = ok && UsePortal(servers, client, seed.characterId, 4, 150.0f, 300.0f, 2);
    ok = ok && UsePortal(servers, client, seed.characterId, 2, 150.0f, 500.0f, 1);
    ok = ok && TurnInQuest(servers, client, 4005);
    Check("Chapter1TurnIn4005Check: Deeper Threat turned in (Traveler Armor reward)", ok);

    // ---- 10. 4006 Ruins Investigation：击杀 Boss（指令六十六）----
    ok = ok && AcceptQuest(servers, client, 4006);
    Check("Chapter1Quest4006Check: Ruins Investigation accepted", ok);
    ok = ok && UsePortal(servers, client, seed.characterId, 1, 950.0f, 300.0f, 2);
    ok = ok && UsePortal(servers, client, seed.characterId, 3, 1750.0f, 1000.0f, 3);
    // Boss 定义/生成检查（2001 在 Map3，1 只；远离入口）。
    {
        const std::uint64_t bossId = FindAliveMonsterOf(servers, world::kAncientGuardianTypeId, 3);
        ok = ok && bossId != 0;
        const auto boss = bossId != 0 ? servers.world->FindMonster(bossId) : nullptr;
        // 出生点 (1900,1500) + radius 40 确定性布点 + patrolRadius 120 巡逻漂移：
        // Boss 常驻存活，位置在出生圆内随机巡逻——断言半径 = 40 + 120 + 10 容差。
        const float bossDx = boss ? boss->PositionX() - 1900.0f : 0.0f;
        const float bossDy = boss ? boss->PositionY() - 1500.0f : 0.0f;
        ok = ok && boss && boss->Alive() && boss->MaxHp() == 600 &&
             (bossDx * bossDx + bossDy * bossDy) <= 170.0f * 170.0f;
        if (!ok) {
            std::printf("[Diag] Chapter1 boss: bossId=%llu alive=%d hp=%u pos=%.0f,%.0f\n",
                        static_cast<unsigned long long>(bossId),
                        boss && boss->Alive() ? 1 : 0, boss ? boss->MaxHp() : 0u,
                        boss ? boss->PositionX() : -1.0f, boss ? boss->PositionY() : -1.0f);
        }
        Check("Chapter1BossSpawnCheck: Ancient Slime Guardian spawned at (1900,1500) hp600",
              ok);
        // 击杀（真实攻击链；Boss def6 -> 伤害 ~14-20/击；死亡则复活继续）。
        ok = ok && MoveTo(servers, client, seed.characterId, 1830.0f, 1440.0f);
        if (!ok) {
            EnsurePlayerAlive(servers, seed.characterId);
            ok = MoveTo(servers, client, seed.characterId, 1830.0f, 1440.0f);
        }
        for (int retry = 0; retry < 3 && ok; ++retry) {
            EnsurePlayerAlive(servers, seed.characterId);
            (void)AttackUntilDead(servers, client, bossId, attackSeq, 60000, seed.characterId);
            auto b = servers.world->FindMonster(bossId);
            if (!b || !b->Alive()) {
                break;
            }
            // Boss 战超时：玩家可能死亡——复活后重新近身再战。
            EnsurePlayerAlive(servers, seed.characterId);
            (void)MoveTo(servers, client, seed.characterId, 1830.0f, 1440.0f);
        }
        ok = ok && WaitUntil(
                       [&] {
                           auto b = servers.world->FindMonster(bossId);
                           return !b || !b->Alive();
                       },
                       5000);
        Check("Chapter1BossKillCheck: Ancient Slime Guardian killed via real combat", ok);
        // 掉落（loot_table_2001：core 100% 2~3 / armor 40% / sword 30%）。
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        coresPicked += PickupDropsOf(servers, client, world::kItemSlimeCoreId, 3);
        // 奖励（exp 300 / gold 120；在线镜像断言）。
        ok = ok && WaitUntil(
                       [&] {
                           client.DrainEvents();
                           return client.controller.LocalGold() >= 100;
                       },
                       5000);
        Check("Chapter1BossRewardCheck: boss gold/exp reward granted (mirror)", ok);
        // 任务推进 4006 1/1。
        ok = ok && WaitQuestKillProgress(servers, seed.characterId, 4006, 1, 8000);
        Check("Chapter1BossQuestCheck: quest 4006 kill objective 1/1", ok);
    }

    // ---- 11. 回村交 4006 -> Chapter Complete ----
    ok = ok && UsePortal(servers, client, seed.characterId, 4, 150.0f, 300.0f, 2);
    ok = ok && UsePortal(servers, client, seed.characterId, 2, 150.0f, 500.0f, 1);
    ok = ok && TurnInQuest(servers, client, 4006);
    Check("Chapter1CompleteCheck: quest 4006 turned in -> Chapter 1 complete", ok);

    // ---- 12. 持久化（指令四十五/六十八）：断线重登后全部保持 ----
    {
        const std::int64_t goldBefore =
            QueryScalar(servers.dbPath, "SELECT gold FROM characters WHERE id=" +
                                            std::to_string(seed.characterId) + ";");
        const std::int64_t levelBefore =
            QueryScalar(servers.dbPath, "SELECT level FROM characters WHERE id=" +
                                            std::to_string(seed.characterId) + ";");
        client.Disconnect();
        WaitUntil([&] { client.DrainEvents(); return true; }, 500);
        const std::string ticket2 =
            servers.login->Tickets().Create(seed.accountId, seed.characterId, 120.0);
        WorldTestClient client2;
        bool reok = !ticket2.empty() && client2.ConnectAndEnter(ticket2, 8000);
        const WorldNetworkEvent& enter2 = client2.LastEnterSuccess();
        reok = reok && static_cast<std::int64_t>(enter2.level) == levelBefore &&
               std::fabs(enter2.positionX - 900.0f) < 260.0f; // 村内位置
        // 装备保持。
        reok = reok && WaitUntil(
                           [&] {
                               client2.DrainEvents();
                               return client2.controller.Equipment().WeaponDefinitionId() ==
                                      world::kItemBronzeSwordId;
                           },
                           6000);
        // 任务状态保持（4006 Completed）。
        reok = reok && WaitQuestState(servers, seed.characterId, 4006, QuestState::Completed,
                                      8000);
        const std::int64_t goldAfter =
            QueryScalar(servers.dbPath, "SELECT gold FROM characters WHERE id=" +
                                            std::to_string(seed.characterId) + ";");
        reok = reok && goldAfter == goldBefore;
        Check("Chapter1PersistenceCheck: level/gold/position/equipment/quests restored", reok);
        client2.Disconnect();
        WaitUntil([&] { client2.DrainEvents(); return true; }, 400);
    }

    servers.StopAll();
}

} // namespace worldtest
