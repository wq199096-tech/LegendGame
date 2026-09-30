// ---------------------------------------------------------------------------
// 阶段19：服务器权威任务检查（Quest Core V0.19）。
// 仍链接 LegendWorldTests（不新增第四个测试 exe，指令八十四）。
// A 部分：纯逻辑（QuestDefinition/Registry 校验/协议 roundtrip + malformed/
//         QuestService 规则：Accept 初始校验/Kill 封顶/多目标/Abandon 校验/
//         ClientQuestModel 纠偏）。
// B 部分：真实链路（Accept 校验链/Kill 归属/进度/ReadyToTurnIn/TurnIn 奖励/
//         防重放/Abandon/持久化/重启/离线 DOT kill/周期快照/只发本人/背包满）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Client/WorldNetwork/ClientQuestModel.h"
#include "Server/LoginServer/Account/AccountRepository.h"
#include "Server/WorldServer/Item/InventoryRepository.h"
#include "Server/WorldServer/Item/ItemRegistry.h"
#include "Shared/Monster/MonsterDefinition.h"
#include "Server/WorldServer/Monster/MonsterEntity.h"
#include "Server/WorldServer/Quest/PlayerQuestContainer.h"
#include "Server/WorldServer/Quest/QuestRegistry.h"
#include "Server/WorldServer/Quest/QuestRepository.h"
#include "Server/WorldServer/Quest/QuestService.h"
#include "Server/WorldServer/Status/StatusEffectRegistry.h"
#include "Shared/Item/ItemTypes.h"
#include "Shared/Quest/QuestError.h"
#include "Shared/Quest/QuestProtocol.h"
#include "Shared/Quest/QuestTypes.h"
#include "Shared/Status/StatusEffectTypes.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include <type_traits>

namespace worldtest {

namespace {

using namespace legend::world;
using legend::client::ClientQuestModel;
using legend::world::QuestRegistry;
using legend::world::QuestResultCode;
using legend::world::QuestService;
using legend::world::QuestState;
namespace CharacterRepository = legend::account::CharacterRepository;

const std::uint8_t kTypePlayer = static_cast<std::uint8_t>(legend::world::CombatEntityType::Player);
const std::uint8_t kTypeMonster =
    static_cast<std::uint8_t>(legend::world::CombatEntityType::Monster);

// ---- 事件辅助（与 WorldInventoryChecks 同模式） ----

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
    return login->Tickets().Create(seed.accountId, seed.characterId, 300.0);
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

// 瞬移玩家（MoveInput 差量，12 units/input；seq 全局递增防去重）。
bool TeleportPlayer(WorldTestClient& client, WorldTestServers& servers,
                    std::uint64_t characterId, float targetX, float targetY) {
    static std::uint32_t s_teleportSeq = 400000;
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
            std::max(1, static_cast<int>(std::ceil(dist / 12.0f)));
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
            return (ex * ex + ey * ey) <= 169.0f;
        },
        3000);
}

// 普攻击杀（slime 80 HP：普攻 18 -> 5 次；requestId 递增防重放）。
bool BasicAttackUntilDead(WorldTestServers& servers, WorldTestClient& client,
                          std::uint64_t monsterId, std::uint64_t& requestId,
                          int timeoutMs = 15000) {
    (void)servers;
    return WaitUntil(
        [&] {
            auto monster = servers.world->FindMonster(monsterId);
            if (!monster || !monster->Alive()) {
                return true;
            }
            client.client().SendAttack(requestId++, kTypeMonster, monsterId);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            return false;
        },
        timeoutMs);
}

// io 线程白盒辅助（与 Status Tick/游戏逻辑串行化）。
template <typename F>
auto RunOnWorldIo(net::NetworkService& service, F&& fn) -> std::invoke_result_t<F&> {
    using R = std::invoke_result_t<F&>;
    std::promise<R> promise;
    auto future = promise.get_future();
    service.Post([&promise, &fn]() { promise.set_value(fn()); });
    return future.get();
}

// 阶段25：跨图 Portal 链路（4004/4005 区域在 Map2/Map3）——teleport 到 portal 旁 ->
// 等 VisiblePortals 收录 -> SendPortalUse -> 等 MapChanged(targetMap)。
// portalEntityId 按生成顺序：8001->1 / 8002->2 / 8003->3。
bool UsePortalTo(WorldTestServers& servers, WorldTestClient& client, std::uint64_t characterId,
                 std::uint64_t portalEntityId, float portalX, float portalY,
                 std::uint16_t expectMapId) {
    static std::uint32_t s_portalSeq = 500000;
    if (!TeleportPlayer(client, servers, characterId, portalX, portalY)) {
        return false;
    }
    const bool visible = WaitUntil(
        [&] {
            return RunOnWorldIo(servers.worldService, [&]() -> bool {
                auto p = servers.world->FindPlayerByCharacter(characterId);
                return p && p->VisiblePortals().count(portalEntityId) != 0;
            });
        },
        3000);
    if (!visible) {
        return false;
    }
    const std::size_t changedBaseline =
        CountEventsOf(client, WorldNetworkEvent::Type::MapChangedEvent);
    client.client().SendPortalUse(++s_portalSeq, portalEntityId);
    WorldNetworkEvent changed;
    return WaitRecordedFrom(
        client, WorldNetworkEvent::Type::MapChangedEvent, changedBaseline,
        [&](const WorldNetworkEvent& e) { return e.mapChanged.mapId == expectMapId; }, changed,
        5000);
}

struct QuestView {
    bool found = false;
    std::uint8_t state = 0;
    std::uint32_t firstProgress = 0;
    std::uint32_t secondProgress = 0;
};

QuestView ReadQuestView(WorldTestServers& servers, std::uint64_t characterId, QuestId questId) {
    return RunOnWorldIo(servers.worldService, [&]() -> QuestView {
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
            if (definition->objectives.size() > 1) {
                view.secondProgress = state->ProgressOf(definition->objectives[1].objectiveId);
            }
        }
        return view;
    });
}

bool WaitQuestState(WorldTestServers& servers, std::uint64_t characterId, QuestId questId,
                    QuestState state, int timeoutMs = 5000) {
    return WaitUntil(
        [&] {
            const auto view = ReadQuestView(servers, characterId, questId);
            return view.found && view.state == static_cast<std::uint8_t>(state);
        },
        timeoutMs);
}

bool WaitQuestProgress(WorldTestServers& servers, std::uint64_t characterId, QuestId questId,
                       std::uint32_t progress, int timeoutMs = 5000) {
    return WaitUntil(
        [&] {
            const auto view = ReadQuestView(servers, characterId, questId);
            return view.found && view.firstProgress == progress;
        },
        timeoutMs);
}

// 移动 slime 到指定点（测试布景）。
void MoveSlimeNear(WorldTestServers& servers, std::uint64_t monsterId, float x, float y) {
    servers.world->MoveMonsterTo(monsterId, x, y);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

// 找一只 alive 的怪。
std::uint64_t FindAliveSlime(WorldTestServers& servers) {
    for (const auto entityId : servers.world->MonsterEntityIds()) {
        const auto monster = servers.world->FindMonster(entityId);
        if (monster && monster->Alive()) {
            return entityId;
        }
    }
    return 0;
}

std::int64_t QueryCharacterColumn(const std::string& dbPath, std::uint64_t characterId,
                                  const char* column) {
    return QueryScalar(dbPath,
                       std::string("SELECT ") + column + " FROM characters WHERE id = " +
                           std::to_string(static_cast<long long>(characterId)) + ";");
}

// ===========================================================================
// A. 纯逻辑检查
// ===========================================================================

void RunQuestLogicChecks() {
    std::string error;

    // ---- QuestDefinitionCheck（指令八十五；阶段25：Chapter 1 任务链 4001~4006）----
    {
        const auto& registry = QuestRegistry::Instance();
        bool ok = registry.Count() == 6;
        const auto* q4001 = registry.FindQuest(4001);
        ok = ok && q4001 != nullptr && q4001->name == "First Trouble" && q4001->minLevel == 1 &&
             q4001->prerequisiteQuestId == 0 && !q4001->repeatable &&
             q4001->objectives.size() == 1 && q4001->objectives[0].objectiveId == 40011 &&
             q4001->objectives[0].type == QuestObjectiveType::KillMonster &&
             q4001->objectives[0].targetId == kTrainingSlimeTypeId &&
             q4001->objectives[0].requiredCount == 5 && q4001->reward.exp == 100 &&
             q4001->reward.gold == 20 && q4001->reward.itemDefinitionId == 0;
        const auto* q4002 = registry.FindQuest(4002);
        ok = ok && q4002 != nullptr && q4002->name == "Strange Cores" &&
             q4002->prerequisiteQuestId == 4001 && q4002->objectives.size() == 1 &&
             q4002->objectives[0].objectiveId == 40021 &&
             q4002->objectives[0].type == QuestObjectiveType::CollectItem &&
             q4002->objectives[0].targetId == kItemSlimeCoreId &&
             q4002->objectives[0].requiredCount == 3 && q4002->reward.exp == 80 &&
             q4002->reward.gold == 30 &&
             q4002->reward.itemDefinitionId == kItemClothArmorId;
        const auto* q4003 = registry.FindQuest(4003);
        ok = ok && q4003 != nullptr && q4003->name == "Growing Stronger" &&
             q4003->prerequisiteQuestId == 4001 && q4003->objectives.size() == 1 &&
             q4003->objectives[0].objectiveId == 40031 &&
             q4003->objectives[0].type == QuestObjectiveType::ReachLevel &&
             q4003->objectives[0].targetId == 3 && q4003->objectives[0].requiredCount == 1 &&
             q4003->reward.exp == 0 && q4003->reward.gold == 80;
        const auto* q4004 = registry.FindQuest(4004);
        ok = ok && q4004 != nullptr && q4004->name == "Explore the Meadow" &&
             q4004->prerequisiteQuestId == 4001 && q4004->objectives.size() == 1 &&
             q4004->objectives[0].objectiveId == 40041 &&
             q4004->objectives[0].type == QuestObjectiveType::ReachArea &&
             q4004->objectives[0].mapId == 2 &&
             std::abs(q4004->objectives[0].areaX - 300.0f) < 0.01f &&
             std::abs(q4004->objectives[0].areaY - 1500.0f) < 0.01f &&
             std::abs(q4004->objectives[0].areaRadius - 200.0f) < 0.01f &&
             q4004->reward.exp == 60 && q4004->reward.gold == 10;
        const auto* q4005 = registry.FindQuest(4005);
        ok = ok && q4005 != nullptr && q4005->name == "Deeper Threat" &&
             q4005->minLevel == 3 &&
             q4005->prerequisiteQuestId == 4002 && q4005->objectives.size() == 3 &&
             q4005->objectives[0].objectiveId == 40051 &&
             q4005->objectives[0].type == QuestObjectiveType::KillMonster &&
             q4005->objectives[0].requiredCount == 5 &&
             q4005->objectives[1].objectiveId == 40052 &&
             q4005->objectives[1].type == QuestObjectiveType::CollectItem &&
             q4005->objectives[1].requiredCount == 2 &&
             q4005->objectives[2].objectiveId == 40053 &&
             q4005->objectives[2].type == QuestObjectiveType::ReachArea &&
             q4005->objectives[2].mapId == 3 &&
             q4005->reward.exp == 200 &&
             q4005->reward.gold == 50 &&
             q4005->reward.itemDefinitionId == kItemTravelerArmorId &&
             q4005->reward.itemQuantity == 1;
        const auto* q4006 = registry.FindQuest(4006);
        ok = ok && q4006 != nullptr && q4006->name == "Ruins Investigation" &&
             q4006->minLevel == 3 && q4006->prerequisiteQuestId == 4005 &&
             q4006->objectives.size() == 1 &&
             q4006->objectives[0].objectiveId == 40061 &&
             q4006->objectives[0].type == QuestObjectiveType::KillMonster &&
             q4006->objectives[0].targetId == kAncientGuardianTypeId &&
             q4006->objectives[0].requiredCount == 1 && q4006->reward.exp == 500 &&
             q4006->reward.gold == 200 &&
             q4006->reward.itemDefinitionId == kItemBronzeSwordId;
        ok = ok && registry.FindQuest(9999) == nullptr;
        Check("QuestDefinitionCheck: Chapter 1 quests 4001~4006 configured per spec", ok);
    }

    // ---- QuestRegistryValidationCheck（指令八十六） ----
    {
        ItemRegistry itemRegistry;
        std::string validationError;
        const bool ok = QuestRegistry::Instance().ValidateDefinitions(&itemRegistry,
                                                                      validationError) &&
                        validationError.empty();
        if (!ok) {
            std::printf("[Diag] QuestRegistry validation error: %s\n", validationError.c_str());
        }
        Check("QuestRegistryValidationCheck: unique ids/objectives, prereqs exist, "
              "reward item in ItemRegistry",
              ok);
    }

    // ---- QuestProtocolRoundtripCheck（指令八十一） ----
    {
        bool ok = true;
        std::vector<std::uint8_t> bytes;
        {
            QuestAcceptRequestPayload p;
            p.requestId = 7;
            p.questId = 4001;
            QuestAcceptRequestPayload d;
            ok = ok && EncodeQuestAcceptRequest(p, bytes) &&
                 DecodeQuestAcceptRequest(bytes.data(), bytes.size(), d, error) &&
                 d.requestId == 7 && d.questId == 4001;
        }
        {
            QuestAcceptResponsePayload p;
            p.requestId = 8;
            p.questId = 4002;
            p.success = true;
            p.resultCode = 1;
            p.serverTime = 99;
            QuestAcceptResponsePayload d;
            ok = ok && EncodeQuestAcceptResponse(p, bytes) &&
                 DecodeQuestAcceptResponse(bytes.data(), bytes.size(), d, error) &&
                 d.requestId == 8 && d.questId == 4002 && d.success && d.resultCode == 1 &&
                 d.serverTime == 99;
        }
        {
            QuestTurnInRequestPayload p;
            p.requestId = 9;
            p.questId = 4003;
            QuestTurnInRequestPayload d;
            ok = ok && EncodeQuestTurnInRequest(p, bytes) &&
                 DecodeQuestTurnInRequest(bytes.data(), bytes.size(), d, error) &&
                 d.requestId == 9 && d.questId == 4003;
        }
        {
            QuestTurnInResponsePayload p;
            p.requestId = 10;
            p.questId = 4004;
            p.success = false;
            p.resultCode = 8;
            p.serverTime = 100;
            QuestTurnInResponsePayload d;
            ok = ok && EncodeQuestTurnInResponse(p, bytes) &&
                 DecodeQuestTurnInResponse(bytes.data(), bytes.size(), d, error) &&
                 d.requestId == 10 && d.questId == 4004 && !d.success && d.resultCode == 8;
        }
        {
            QuestAbandonRequestPayload p;
            p.requestId = 11;
            p.questId = 4005;
            QuestAbandonRequestPayload d;
            ok = ok && EncodeQuestAbandonRequest(p, bytes) &&
                 DecodeQuestAbandonRequest(bytes.data(), bytes.size(), d, error) &&
                 d.requestId == 11 && d.questId == 4005;
        }
        {
            QuestAbandonResponsePayload p;
            p.requestId = 12;
            p.questId = 4001;
            p.success = true;
            p.resultCode = 0;
            p.serverTime = 101;
            QuestAbandonResponsePayload d;
            ok = ok && EncodeQuestAbandonResponse(p, bytes) &&
                 DecodeQuestAbandonResponse(bytes.data(), bytes.size(), d, error) &&
                 d.requestId == 12 && d.questId == 4001 && d.success && d.resultCode == 0;
        }
        {
            QuestProgressUpdatedPayload p;
            p.questId = 4001;
            p.objectiveId = 40011;
            p.current = 3;
            p.required = 5;
            p.questState = 1;
            p.serverTime = 102;
            QuestProgressUpdatedPayload d;
            ok = ok && EncodeQuestProgressUpdated(p, bytes) &&
                 DecodeQuestProgressUpdated(bytes.data(), bytes.size(), d, error) &&
                 d.questId == 4001 && d.objectiveId == 40011 && d.current == 3 &&
                 d.required == 5 && d.questState == 1;
        }
        {
            QuestStateChangedPayload p;
            p.questId = 4002;
            p.oldState = 1;
            p.newState = 2;
            p.serverTime = 103;
            QuestStateChangedPayload d;
            ok = ok && EncodeQuestStateChanged(p, bytes) &&
                 DecodeQuestStateChanged(bytes.data(), bytes.size(), d, error) &&
                 d.questId == 4002 && d.oldState == 1 && d.newState == 2;
        }
        {
            QuestSnapshotPayload p;
            p.characterId = 42;
            QuestSnapshotEntryData quest;
            quest.questId = 4005;
            quest.state = 1;
            QuestSnapshotObjectiveData o1;
            o1.objectiveId = 40051;
            o1.current = 1;
            o1.required = 3;
            quest.objectives.push_back(o1);
            QuestSnapshotObjectiveData o2;
            o2.objectiveId = 40052;
            o2.current = 2;
            o2.required = 2;
            quest.objectives.push_back(o2);
            p.quests.push_back(quest);
            p.serverTime = 104;
            QuestSnapshotPayload d;
            ok = ok && EncodeQuestSnapshot(p, bytes) &&
                 DecodeQuestSnapshot(bytes.data(), bytes.size(), d, error) &&
                 d.characterId == 42 && d.quests.size() == 1 &&
                 d.quests[0].objectives.size() == 2 && d.quests[0].objectives[1].current == 2;
        }
        {
            QuestRewardGrantedPayload p;
            p.questId = 4005;
            p.exp = 150;
            p.gold = 30;
            p.itemDefinitionId = 3001;
            p.itemQuantity = 1;
            p.newLevel = 3;
            p.newExperience = 25;
            p.newGold = 65;
            p.serverTime = 105;
            QuestRewardGrantedPayload d;
            ok = ok && EncodeQuestRewardGranted(p, bytes) &&
                 DecodeQuestRewardGranted(bytes.data(), bytes.size(), d, error) &&
                 d.questId == 4005 && d.exp == 150 && d.gold == 30 &&
                 d.itemDefinitionId == 3001 && d.newExperience == 25 && d.newGold == 65;
        }
        Check("QuestProtocolRoundtripCheck: all 10 quest payloads encode/decode", ok);
    }

    // ---- MalformedQuestProtocolCheck（指令一百二十八） ----
    {
        bool ok = true;
        {
            QuestProgressUpdatedPayload p;
            std::vector<std::uint8_t> payload;
            (void)EncodeQuestProgressUpdated(p, payload);
            for (std::size_t cut = 0; cut < payload.size(); ++cut) {
                QuestProgressUpdatedPayload d;
                ok = ok && !DecodeQuestProgressUpdated(payload.data(), cut, d, error);
            }
            std::vector<std::uint8_t> trailing = payload;
            trailing.push_back(0xAB);
            QuestProgressUpdatedPayload d2;
            ok = ok && !DecodeQuestProgressUpdated(trailing.data(), trailing.size(), d2, error);
        }
        {
            // QuestSnapshot count > 256（指令八十三）拒绝。
            std::vector<std::uint8_t> bytes2;
            const auto append = [&bytes2](std::uint64_t v, std::size_t n) {
                for (std::size_t i = 0; i < n; ++i) {
                    bytes2.push_back(static_cast<std::uint8_t>(
                        v >> (8 * (n - 1 - i))));
                }
            };
            append(1, 8);    // characterId
            append(257, 2);  // quest count > 256
            append(4001, 4); // questId
            bytes2.push_back(1);  // state
            bytes2.push_back(1);  // objective count
            append(40011, 4);
            append(0, 4);
            append(5, 4);
            append(0, 8);    // serverTime
            QuestSnapshotPayload d;
            ok = ok && !DecodeQuestSnapshot(bytes2.data(), bytes2.size(), d, error);

            // objective count > 16 拒绝。
            std::vector<std::uint8_t> bytes3;
            append(1, 8);
            append(1, 2);
            append(4001, 4);
            bytes3.push_back(1);
            bytes3.push_back(17);
            for (int i = 0; i < 17; ++i) {
                append(40011, 4);
                append(0, 4);
                append(5, 4);
            }
            append(0, 8);
            QuestSnapshotPayload d3;
            ok = ok && !DecodeQuestSnapshot(bytes3.data(), bytes3.size(), d3, error);
        }
        Check("MalformedQuestProtocolCheck: truncated/trailing/count-overflow all rejected", ok);
    }

    // ---- QuestService：Accept 初始校验 + Kill 封顶 + 多目标 + ReachArea ----
    {
        const auto& registry = QuestRegistry::Instance();
        PlayerQuestContainer quests;
        auto changes = QuestService::AcceptQuest(registry, quests, 4002, 1,
                                                 [](std::uint32_t) { return 3u; }, 1000);
        bool ok = changes.size() == 1 && changes[0].newProgress == 3;
        auto stateChanges = QuestService::EvaluateQuestCompletion(registry, quests, 1100);
        ok = ok && stateChanges.size() == 1 &&
             stateChanges[0].newState == QuestState::ReadyToTurnIn;
        Check("CollectInitialLogicCheck: accept with 3 owned cores -> instant 3/3 Ready", ok);

        PlayerQuestContainer levelQuests;
        auto levelChanges = QuestService::AcceptQuest(registry, levelQuests, 4003, 3, {}, 1200);
        ok = levelChanges.size() == 1 && levelChanges[0].newProgress == 1;
        auto levelReady = QuestService::EvaluateQuestCompletion(registry, levelQuests, 1250);
        ok = ok && levelReady.size() == 1 &&
             levelReady[0].newState == QuestState::ReadyToTurnIn;
        PlayerQuestContainer level2Quests;
        auto level2Changes = QuestService::AcceptQuest(registry, level2Quests, 4003, 2, {}, 1300);
        ok = ok && level2Changes.empty() &&
             level2Quests.Find(4003)->state == QuestState::InProgress;
        auto upChanges = QuestService::OnPlayerLevelChanged(registry, level2Quests, 3);
        ok = ok && upChanges.size() == 1 && upChanges[0].newProgress == 1;
        auto upReady = QuestService::EvaluateQuestCompletion(registry, level2Quests, 1350);
        ok = ok && upReady.size() == 1 && upReady[0].newState == QuestState::ReadyToTurnIn;
        Check("ReachLevelLogicCheck: level3 accept instant-ready; level2 completes on levelup",
              ok);

        PlayerQuestContainer killQuests;
        (void)QuestService::AcceptQuest(registry, killQuests, 4001, 1, {}, 1400);
        for (int i = 0; i < 5; ++i) {
            (void)QuestService::OnMonsterKilled(registry, killQuests, kTrainingSlimeTypeId);
        }
        auto readyChanges = QuestService::EvaluateQuestCompletion(registry, killQuests, 1450);
        ok = killQuests.Find(4001)->ProgressOf(40011) == 5 && readyChanges.size() == 1;
        auto afterChanges =
            QuestService::OnMonsterKilled(registry, killQuests, kTrainingSlimeTypeId);
        ok = ok && afterChanges.empty() && killQuests.Find(4001)->ProgressOf(40011) == 5;
        PlayerQuestContainer killQuests2;
        (void)QuestService::AcceptQuest(registry, killQuests2, 4001, 1, {}, 1500);
        auto miss = QuestService::OnMonsterKilled(registry, killQuests2, 999);
        ok = ok && miss.empty() && killQuests2.Find(4001)->ProgressOf(40011) == 0;
        Check("KillCapLogicCheck: kill progress caps at required; non-target type ignored", ok);

        PlayerQuestContainer multi;
        (void)QuestService::AcceptQuest(registry, multi, 4005, 1,
                                        [](std::uint32_t) { return 1u; }, 1600);
        for (int i = 0; i < 5; ++i) {
            (void)QuestService::OnMonsterKilled(registry, multi, kTrainingSlimeTypeId);
        }
        auto multiChanges = QuestService::EvaluateQuestCompletion(registry, multi, 1650);
        ok = multiChanges.empty() && multi.Find(4005)->ProgressOf(40051) == 5 &&
             multi.Find(4005)->ProgressOf(40052) == 1 &&
             multi.Find(4005)->ProgressOf(40053) == 0 &&
             multi.Find(4005)->state == QuestState::InProgress;
        auto collectChanges =
            QuestService::OnInventoryChanged(registry, multi, [](std::uint32_t) { return 2u; });
        ok = ok && collectChanges.size() == 1;
        // 阶段25：4005 三目标（kill/collect/area）——区域未完成前不 Ready。
        auto stillNotReady = QuestService::EvaluateQuestCompletion(registry, multi, 1660);
        ok = ok && stillNotReady.empty();
        auto areaChanges =
            QuestService::OnPlayerMoved(registry, multi, 3, 200.0f, 300.0f);
        ok = ok && areaChanges.size() == 1;
        auto multiReady = QuestService::EvaluateQuestCompletion(registry, multi, 1700);
        ok = ok && multiReady.size() == 1 &&
             multiReady[0].newState == QuestState::ReadyToTurnIn;
        Check("MultiObjectiveLogicCheck: quest stays InProgress until ALL objectives met", ok);

        PlayerQuestContainer area;
        (void)QuestService::AcceptQuest(registry, area, 4004, 1, {}, 1800);
        auto wrongMap = QuestService::OnPlayerMoved(registry, area, 1, 300.0f, 1500.0f);
        auto tooFar = QuestService::OnPlayerMoved(registry, area, 2, 700.0f, 1500.0f);
        ok = wrongMap.empty() && tooFar.empty() && area.Find(4004)->ProgressOf(40041) == 0;
        auto inside = QuestService::OnPlayerMoved(registry, area, 2, 350.0f, 1520.0f);
        ok = ok && inside.size() == 1 && inside[0].newProgress == 1;
        auto areaAgain = QuestService::OnPlayerMoved(registry, area, 2, 300.0f, 1500.0f);
        ok = ok && areaAgain.empty();
        Check("ReachAreaLogicCheck: map/radius gating, one-shot completion, no duplicate", ok);

        PlayerQuestContainer validate;
        auto turnInNotAccepted = QuestService::ValidateTurnIn(validate, 4001, registry);
        (void)QuestService::AcceptQuest(registry, validate, 4001, 1, {}, 1900);
        auto turnInNotReady = QuestService::ValidateTurnIn(validate, 4001, registry);
        for (int i = 0; i < 5; ++i) {
            (void)QuestService::OnMonsterKilled(registry, validate, kTrainingSlimeTypeId);
        }
        (void)QuestService::EvaluateQuestCompletion(registry, validate, 1950);
        auto turnInReady = QuestService::ValidateTurnIn(validate, 4001, registry);
        validate.MutableFind(4001)->state = QuestState::Completed;
        auto turnInCompleted = QuestService::ValidateTurnIn(validate, 4001, registry);
        auto abandonCompleted = QuestService::ValidateAbandon(validate, 4001, registry);
        validate.MutableFind(4001)->state = QuestState::InProgress;
        auto abandonInProgress = QuestService::ValidateAbandon(validate, 4001, registry);
        ok = turnInNotAccepted == QuestResultCode::NotAccepted &&
             turnInNotReady == QuestResultCode::NotReady &&
             turnInReady == QuestResultCode::Success &&
             turnInCompleted == QuestResultCode::AlreadyCompleted &&
             abandonCompleted == QuestResultCode::AlreadyCompleted &&
             abandonInProgress == QuestResultCode::Success;
        Check("TurnInAbandonValidationLogicCheck: state machine gates enforced", ok);
    }

    // ---- ClientQuestModel 纠偏（指令一百二十四） ----
    {
        ClientQuestModel model;
        std::vector<QuestSnapshotEntryData> snapshot;
        QuestSnapshotEntryData entry;
        entry.questId = 4001;
        entry.state = 1;
        QuestSnapshotObjectiveData objective;
        objective.objectiveId = 40011;
        objective.current = 2;
        objective.required = 5;
        entry.objectives.push_back(objective);
        snapshot.push_back(entry);
        model.ApplySnapshot(snapshot);
        model.ApplyProgress(4001, 40011, 99, 5);
        const bool tampered = model.Find(4001)->objectives.at(40011).current == 99;
        model.ApplySnapshot(snapshot);
        const bool ok = tampered && model.Find(4001)->objectives.at(40011).current == 2 &&
                        model.Find(4001)->state == QuestState::InProgress;
        Check("QuestSnapshotCorrectionLogicCheck: client tamper corrected by next snapshot", ok);
    }
}

// ===========================================================================
// B. 真实链路检查
// ===========================================================================

void RunQuestChainChecks() {
    WorldTestServers servers;
    servers.dbPath = TempDbPath("world_quest");
    RemoveDb(servers.dbPath);
    servers.worldRespawnDelayMs = 60000;   // 测试期间无重生怪干扰
    servers.questSnapshotIntervalMs = 300; // 指令一百二十三：缩短周期验证
    Check("QuestServersStartCheck", servers.StartLogin() && servers.StartWorld());

    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(8, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("QuestChecks: db ready", false);
        servers.StopAll();
        return;
    }
    // ---- 布局：A 主角 / B 第二玩家（ReachLevel 链）/ C 离线 DOT / E 任务栏上限
    //      （全部安全区：距最近 slime 巡逻边界 > aggro 350） ----
    CharacterSeed seedA;
    CharacterSeed seedB;
    CharacterSeed seedC;
    CharacterSeed seedE;
    bool seeded = true;
    seeded = seeded && SeedAt(db, accounts, characters, "qst_user_a", "QstA", 60.0f, 60.0f, seedA);
    seeded = seeded && SeedAt(db, accounts, characters, "qst_user_b", "QstB", 40.0f, 80.0f, seedB);
    seeded = seeded && SeedAt(db, accounts, characters, "qst_user_c", "QstC", 60.0f, 940.0f, seedC);
    seeded = seeded && SeedAt(db, accounts, characters, "qst_user_e", "QstE", 80.0f, 40.0f, seedE);
    Check("QuestChecks: seeds ready", seeded);

    WorldTestClient clientA;
    WorldTestClient clientB;
    WorldTestClient clientC;
    WorldTestClient clientE;
    {
        const std::string ticketA = TicketFor(servers.login, seedA);
        const std::string ticketB = TicketFor(servers.login, seedB);
        const std::string ticketE = TicketFor(servers.login, seedE);
        const bool entered =
            !ticketA.empty() && clientA.ConnectAndEnter(ticketA, 8000) &&
            !ticketB.empty() && clientB.ConnectAndEnter(ticketB, 8000) &&
            !ticketE.empty() && clientE.ConnectAndEnter(ticketE, 8000);
        Check("QuestChecks: three clients entered", entered);
    }
    clientA.DrainEvents();
    clientB.DrainEvents();
    clientE.DrainEvents();

    // ---- QuestSnapshotCheck（指令一百二十二）：进世界下发任务快照 ----
    {
        WorldNetworkEvent snap;
        const bool gotSnapshot = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::QuestSnapshotEvent, 0,
            [](const WorldNetworkEvent&) { return true; }, snap, 4000);
        Check("QuestSnapshotCheck: enter world sends quest snapshot", gotSnapshot);
    }

    // ---- AcceptQuestCheck（指令八十七） ----
    {
        const std::size_t responseBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent);
        const std::size_t stateBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestStateChangedEvent);
        clientA.controller.SendQuestAccept(4001);
        WorldNetworkEvent response;
        bool ok = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent, responseBaseline,
            [&](const WorldNetworkEvent& e) {
                return e.questId == 4001 && e.success &&
                       e.questResultCode == static_cast<std::uint8_t>(QuestResultCode::Success);
            },
            response);
        WorldNetworkEvent stateEvent;
        ok = ok &&
             WaitRecordedFrom(clientA, WorldNetworkEvent::Type::QuestStateChangedEvent,
                              stateBaseline,
                              [&](const WorldNetworkEvent& e) {
                                  return e.questId == 4001 && e.questOldState == 0 &&
                                         e.questState ==
                                             static_cast<std::uint8_t>(QuestState::InProgress);
                              },
                              stateEvent);
        ok = ok && WaitQuestState(servers, seedA.characterId, 4001, QuestState::InProgress);
        Check("AcceptQuestCheck: accept 4001 -> InProgress + StateChanged + response", ok);
    }

    // ---- UnknownQuestCheck / AlreadyAcceptedCheck / PrerequisiteCheck ----
    {
        clientA.controller.SendQuestAccept(4001);
        WorldNetworkEvent dup;
        bool ok = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent,
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent) - 1,
            [&](const WorldNetworkEvent& e) {
                return e.questId == 4001 && !e.success &&
                       e.questResultCode ==
                           static_cast<std::uint8_t>(QuestResultCode::AlreadyAccepted);
            },
            dup);
        clientA.controller.SendQuestAccept(999999);
        WorldNetworkEvent unknown;
        ok = ok &&
             WaitRecordedFrom(
                 clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent,
                 CountEventsOf(clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent) - 1,
                 [&](const WorldNetworkEvent& e) {
                     return e.questId == 999999 && !e.success &&
                            e.questResultCode ==
                                static_cast<std::uint8_t>(QuestResultCode::UnknownQuest);
                 },
                 unknown);
        clientA.controller.SendQuestAccept(4002);
        WorldNetworkEvent prereq2;
        ok = ok &&
             WaitRecordedFrom(
                 clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent,
                 CountEventsOf(clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent) - 1,
                 [&](const WorldNetworkEvent& e) {
                     return e.questId == 4002 && !e.success &&
                            e.questResultCode ==
                                static_cast<std::uint8_t>(QuestResultCode::PrerequisiteNotMet);
                 },
                 prereq2);
        clientA.controller.SendQuestAccept(4003);
        WorldNetworkEvent prereq3;
        ok = ok &&
             WaitRecordedFrom(
                 clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent,
                 CountEventsOf(clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent) - 1,
                 [&](const WorldNetworkEvent& e) {
                     return e.questId == 4003 && !e.success &&
                            e.questResultCode ==
                                static_cast<std::uint8_t>(QuestResultCode::PrerequisiteNotMet);
                 },
                 prereq3);
        clientA.controller.SendQuestAccept(4005);
        WorldNetworkEvent prereq5;
        ok = ok &&
             WaitRecordedFrom(
                 clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent,
                 CountEventsOf(clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent) - 1,
                 [&](const WorldNetworkEvent& e) { return e.questId == 4005 && !e.success; },
                 prereq5);
        Check("UnknownQuestCheck/AlreadyAcceptedCheck/PrerequisiteCheck: accept gates "
              "(4002/4003/4005 all gated by prerequisite)",
              ok);
    }

    // ---- KillOnlyKillerCheck（九十三）+ KillProgressCheck（九十二） ----
    {
        const std::size_t baseline =
            CountEventsOf(clientB, WorldNetworkEvent::Type::QuestAcceptResponseEvent);
        clientB.controller.SendQuestAccept(4001);
        WorldNetworkEvent bAccept;
        const bool bAccepted = WaitRecordedFrom(
            clientB, WorldNetworkEvent::Type::QuestAcceptResponseEvent, baseline,
            [&](const WorldNetworkEvent& e) { return e.questId == 4001 && e.success; },
            bAccept);
        Check("QuestChecks: B accepted 4001", bAccepted);

        const std::uint64_t slimeId = FindAliveSlime(servers);
        const bool aMoved = slimeId != 0 &&
                            TeleportPlayer(clientA, servers, seedA.characterId, 180.0f, 60.0f);
        MoveSlimeNear(servers, slimeId, 240.0f, 60.0f);
        std::uint64_t attackSeq = 2000;
        (void)BasicAttackUntilDead(servers, clientA, slimeId, attackSeq);
        const bool aProgressed = WaitQuestProgress(servers, seedA.characterId, 4001, 1, 6000);
        const auto bView = ReadQuestView(servers, seedB.characterId, 4001);
        Check("KillProgressCheck/KillOnlyKillerCheck: killer +1, other player not advanced",
              aMoved && aProgressed && bView.found && bView.firstProgress == 0);
    }

    // ---- 杀 2~5 -> ReadyToTurnIn（一百零九）+ KillNoDuplicateCheck（九十七） ----
    {
        bool allProgressed = true;
        for (std::uint32_t expected = 2; expected <= 5; ++expected) {
            const std::uint64_t slimeId = FindAliveSlime(servers);
            MoveSlimeNear(servers, slimeId, 240.0f, 60.0f);
            std::uint64_t attackSeq = 3000 + expected * 100;
            (void)BasicAttackUntilDead(servers, clientA, slimeId, attackSeq);
            allProgressed = allProgressed &&
                            WaitQuestProgress(servers, seedA.characterId, 4001, expected, 6000);
        }
        Check("KillProgressCheck/KillNoDuplicateCheck: 5 kills -> exactly 5/5, one per kill",
              allProgressed);
        const bool ready = WaitQuestState(servers, seedA.characterId, 4001,
                                          QuestState::ReadyToTurnIn, 4000);
        Check("ReadyToTurnInCheck: all objectives met -> ReadyToTurnIn (not Completed)", ready);
    }

    // ---- NoAutoRewardCheck（一百一十） ----
    {
        const std::int64_t goldBefore =
            QueryCharacterColumn(servers.dbPath, seedA.characterId, "gold");
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        const std::int64_t goldAfter =
            QueryCharacterColumn(servers.dbPath, seedA.characterId, "gold");
        // 5 杀 x3 gold = 15（杀怪奖励已入库；任务奖励未发）。
        Check("NoAutoRewardCheck: ReadyToTurnIn grants nothing (gold stays 15)",
              goldBefore == 15 && goldAfter == 15);
    }

    // ---- KillAfterObjectiveCompleteCheck（九十八）----
    //（第 6 杀：进度保持 5/5；EXP/Gold 照常 +25/+3 -> TurnIn 断言用 250/38）----
    {
        const std::uint64_t slimeId = FindAliveSlime(servers);
        MoveSlimeNear(servers, slimeId, 240.0f, 60.0f);
        std::uint64_t attackSeq = 4200;
        (void)BasicAttackUntilDead(servers, clientA, slimeId, attackSeq);
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        const auto view = ReadQuestView(servers, seedA.characterId, 4001);
        Check("KillAfterObjectiveCompleteCheck: extra kill keeps progress at 5/5",
              view.found && view.firstProgress == 5);
    }

    // ---- TurnInRewardCheck（一百一十一）+ 防重放/再领/Completed 门禁
    //（一百一十四~一百一十九）+ QuestRewardOwnerOnlyCheck（一百二十七） ----
    {
        const std::size_t rewardBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestRewardGrantedEvent);
        const std::size_t turnInBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent);
        clientA.controller.SendQuestTurnIn(4001);
        WorldNetworkEvent reward;
        bool ok = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::QuestRewardGrantedEvent, rewardBaseline,
            [&](const WorldNetworkEvent& e) {
                return e.questId == 4001 && e.questRewardExp == 100 && e.questRewardGold == 20;
            },
            reward);
        WorldNetworkEvent turnIn;
        ok = ok &&
             WaitRecordedFrom(clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent,
                              turnInBaseline,
                              [&](const WorldNetworkEvent& e) {
                                  return e.questId == 4001 && e.success;
                              },
                              turnIn);
        ok = ok && WaitQuestState(servers, seedA.characterId, 4001, QuestState::Completed);
        // DB：EXP/Gold（6 杀 = L2/150 exp + 18 gold；任务 +100 exp -> L2/250？否——
        // ExpToNextLevel(2)=200：L2/50 + 100 = L2/150。gold 18+20=38。
        const std::int64_t expRow = QueryCharacterColumn(servers.dbPath, seedA.characterId, "exp");
        const std::int64_t goldRow = QueryCharacterColumn(servers.dbPath, seedA.characterId, "gold");
        ok = ok && expRow == 150 && goldRow == 38 && reward.progression.newLevel == 2;
        if (!ok) {
            std::printf("[Diag] TurnInReward: exp=%lld gold=%lld level=%u\n",
                        static_cast<long long>(expRow), static_cast<long long>(goldRow),
                        reward.progression.newLevel);
        }
        Check("TurnInRewardCheck: EXP+100/Gold+20 persisted; quest EXP triggered LevelUp", ok);

        // 同 requestId 重放 -> DuplicateRequest。
        const std::uint64_t replayId = clientA.controller.LastQuestRequestId();
        clientA.client().SendQuestTurnIn(replayId, 4001);
        WorldNetworkEvent dupResponse;
        bool dupOk = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent,
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent),
            [&](const WorldNetworkEvent& e) {
                return e.questId == 4001 && !e.success &&
                       e.questResultCode ==
                           static_cast<std::uint8_t>(QuestResultCode::DuplicateRequest);
            },
            dupResponse);
        // 新 requestId 再领 -> AlreadyCompleted。
        clientA.controller.SendQuestTurnIn(4001);
        WorldNetworkEvent again;
        dupOk = dupOk &&
                WaitRecordedFrom(
                    clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent,
                    CountEventsOf(clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent),
                    [&](const WorldNetworkEvent& e) {
                        return e.questId == 4001 && !e.success &&
                               e.questResultCode ==
                                   static_cast<std::uint8_t>(QuestResultCode::AlreadyCompleted);
                    },
                    again);
        // Completed 不能再接 / 不能 Abandon。
        clientA.controller.SendQuestAccept(4001);
        WorldNetworkEvent reaccept;
        dupOk = dupOk &&
                WaitRecordedFrom(
                    clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent,
                    CountEventsOf(clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent),
                    [&](const WorldNetworkEvent& e) {
                        return e.questId == 4001 && !e.success &&
                               e.questResultCode ==
                                   static_cast<std::uint8_t>(QuestResultCode::AlreadyCompleted);
                    },
                    reaccept);
        clientA.controller.SendQuestAbandon(4001);
        WorldNetworkEvent abandon;
        dupOk = dupOk &&
                WaitRecordedFrom(
                    clientA, WorldNetworkEvent::Type::QuestAbandonResponseEvent,
                    CountEventsOf(clientA, WorldNetworkEvent::Type::QuestAbandonResponseEvent),
                    [&](const WorldNetworkEvent& e) { return e.questId == 4001 && !e.success; },
                    abandon);
        Check("TurnInDuplicateRequestCheck/TurnInCompletedAgainCheck/"
              "CompletedCannotReacceptCheck/CompletedCannotAbandonCheck",
              dupOk);

        // QuestRewardOwnerOnlyCheck（一百二十七）。
        clientB.DrainEvents();
        Check("QuestRewardOwnerOnlyCheck: B received zero quest reward events",
              CountEventsOf(clientB, WorldNetworkEvent::Type::QuestRewardGrantedEvent) == 0);
    }

    // ---- QuestProgressNoGlobalBroadcastCheck / QuestStateNoGlobalBroadcastCheck
    //（一百二十五/一百二十六）：B 收不到 A 的进度/状态事件 ----
    {
        clientB.DrainEvents();
        int bProgressEventsFor4001 = 0;
        for (const auto& e : clientB.recorded[WorldTestClient::IndexOf(
                 WorldNetworkEvent::Type::QuestProgressUpdatedEvent)]) {
            if (e.questId == 4001) {
                ++bProgressEventsFor4001;
            }
        }
        int bForeignStateEvents = 0;
        for (const auto& e : clientB.recorded[WorldTestClient::IndexOf(
                 WorldNetworkEvent::Type::QuestStateChangedEvent)]) {
            // B 自己只发过 4001 的 InProgress；其余任何状态事件都是他人广播。
            if (!(e.questId == 4001 &&
                  e.questState == static_cast<std::uint8_t>(QuestState::InProgress))) {
                ++bForeignStateEvents;
            }
        }
        const auto bView = ReadQuestView(servers, seedB.characterId, 4001);
        Check("QuestProgressNoGlobalBroadcastCheck/QuestStateNoGlobalBroadcastCheck: "
              "B receives none of A's progress/state events",
              bView.found && bView.firstProgress == 0 && bProgressEventsFor4001 == 0 &&
                  bForeignStateEvents == 0);
    }

    // ---- ReachLevelProgressCheck（一百零四）：A 在 Level2 接 4003 -> 0/1 ----
    {
        const std::size_t baseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent);
        clientA.controller.SendQuestAccept(4003);
        WorldNetworkEvent response;
        bool ok = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent, baseline,
            [&](const WorldNetworkEvent& e) { return e.questId == 4003 && e.success; },
            response);
        ok = ok &&
             WaitQuestState(servers, seedA.characterId, 4003, QuestState::InProgress, 4000);
        const auto view = ReadQuestView(servers, seedA.characterId, 4003);
        ok = ok && view.found && view.firstProgress == 0;
        Check("ReachLevelProgressCheck: level-2 character accepting 4003 -> InProgress 0/1", ok);
    }

    // ---- CollectInitialCheck（九十九）/ CollectPickupCheck（一百）/
    //      CollectStackCheck（一百零一）：A 接 4002 -> 拾取 0->1->2->3 ----
    {
        const std::size_t baseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent);
        clientA.controller.SendQuestAccept(4002);
        WorldNetworkEvent response;
        bool ok = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent, baseline,
            [&](const WorldNetworkEvent& e) { return e.questId == 4002 && e.success; },
            response);
        ok = ok && WaitQuestState(servers, seedA.characterId, 4002, QuestState::InProgress);
        const auto view0 = ReadQuestView(servers, seedA.characterId, 4002);
        ok = ok && view0.found && view0.firstProgress == 0;
        for (std::uint32_t expected = 1; expected <= 3 && ok; ++expected) {
            const bool spawned = servers.world->TestSpawnDrop(140.0f, 60.0f, 1,
                                                              seedA.characterId,
                                                              kItemSlimeCoreId);
            WorldNetworkEvent spawnEvent;
            ok = ok && spawned &&
                 WaitRecordedFrom(
                     clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent,
                     CountEventsOf(clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent),
                     [&](const WorldNetworkEvent& e) {
                         return e.itemDefinitionId == kItemSlimeCoreId;
                     },
                     spawnEvent, 4000);
            clientA.controller.SendPickup(spawnEvent.dropEntityId);
            ok = ok && WaitQuestProgress(servers, seedA.characterId, 4002, expected, 6000);
        }
        Check("CollectInitialCheck/CollectPickupCheck/CollectStackCheck: pickup drives "
              "collect progress 0->1->2->3 via owned quantity",
              ok);
        const bool ready =
            WaitQuestState(servers, seedA.characterId, 4002, QuestState::ReadyToTurnIn, 4000);
        Check("CollectReadyCheck: 3/3 cores -> ReadyToTurnIn", ready);
        const std::size_t turnInBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent);
        clientA.controller.SendQuestTurnIn(4002);
        WorldNetworkEvent turnIn;
        const bool turnedIn = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent, turnInBaseline,
            [&](const WorldNetworkEvent& e) { return e.questId == 4002 && e.success; },
            turnIn);
        Check("QuestChecks: 4002 turned in", turnedIn);
        // ---- QuestRewardLevelUpCheck（一百零五）：TurnIn 4002 奖励 EXP (+80) 使
        //      L2/150 -> L3/30 —— 升级事件推进 InProgress 的 4003 ReachLevel
        //      （指令五十七：任务奖励升级推进其它任务）----
        bool leveledOk = turnedIn;
        leveledOk = leveledOk && WaitUntil(
            [&] {
                return QueryCharacterColumn(servers.dbPath, seedA.characterId, "level") == 3;
            },
            4000);
        leveledOk = leveledOk &&
                    WaitQuestState(servers, seedA.characterId, 4003, QuestState::ReadyToTurnIn, 4000);
        Check("QuestRewardLevelUpCheck: turn-in EXP levels 2->3, advances ReachLevel 4003", leveledOk);
    }

    // ---- ReachAreaCheck（一百零六）+ ReachAreaServerAuthorityCheck（一百零七）
    //      （阶段25：4004 区域在 Map2 南部 (300,1500)——Map1 位置永不完成，走 Portal）----
    {
        const std::size_t baseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent);
        clientA.controller.SendQuestAccept(4004);
        WorldNetworkEvent response;
        bool ok = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent, baseline,
            [&](const WorldNetworkEvent& e) { return e.questId == 4004 && e.success; },
            response);
        // Map1 内真实移动不完成（区域在 Map2——服务器权威位置，Client 不能伪造）。
        (void)TeleportPlayer(clientA, servers, seedA.characterId, 100.0f, 100.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const auto farView = ReadQuestView(servers, seedA.characterId, 4004);
        ok = ok && farView.found && farView.firstProgress == 0;
        // Portal 8001 (1000,300) -> Map2 (200,500) -> 真实移动进入 (300,1500) r=200 -> 1/1。
        ok = ok && UsePortalTo(servers, clientA, seedA.characterId, 1, 950.0f, 300.0f, 2);
        ok = ok && TeleportPlayer(clientA, servers, seedA.characterId, 300.0f, 1500.0f);
        ok = ok && WaitQuestProgress(servers, seedA.characterId, 4004, 1, 6000);
        ok = ok && WaitQuestState(servers, seedA.characterId, 4004, QuestState::ReadyToTurnIn);
        Check("ReachAreaCheck/ReachAreaServerAuthorityCheck: only server-authoritative "
              "cross-map position completes ReachArea",
              ok);
        const std::size_t turnInBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent);
        clientA.controller.SendQuestTurnIn(4004);
        WorldNetworkEvent turnIn;
        ok = ok &&
             WaitRecordedFrom(clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent,
                              turnInBaseline,
                              [&](const WorldNetworkEvent& e) {
                                  return e.questId == 4004 && e.success;
                              },
                              turnIn);
        // 返回 Map1（后续 4005 杀怪）：Portal 8002 (150,500) -> Map1 (900,300)。
        ok = ok && UsePortalTo(servers, clientA, seedA.characterId, 2, 150.0f, 500.0f, 1);
        Check("QuestChecks: 4004 turned in + returned to Map1", ok);
    }

    // ---- MultiObjectiveCheck 链路（一百零八）+ QuestItemRewardCheck（一百一十二）
    //      （阶段25：4005 = 杀 5 Slime + 交 2 Core + 进入 Ancient Ruins 入口区域）----
    {
        const std::size_t baseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent);
        clientA.controller.SendQuestAccept(4005);
        WorldNetworkEvent response;
        bool ok = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::QuestAcceptResponseEvent, baseline,
            [&](const WorldNetworkEvent& e) { return e.questId == 4005 && e.success; },
            response);
        // A 背包已有 3 core -> collect 2/2 立即；kill 0/5 + area 0/1 -> InProgress。
        ok = ok && WaitQuestState(servers, seedA.characterId, 4005, QuestState::InProgress);
        for (int i = 0; i < 5 && ok; ++i) {
            const std::uint64_t slimeId = FindAliveSlime(servers);
            MoveSlimeNear(servers, slimeId, 960.0f, 320.0f); // 玩家附近（Map1 900,300 旁）
            std::uint64_t attackSeq = 6000 + i * 100;
            (void)BasicAttackUntilDead(servers, clientA, slimeId, attackSeq);
            const auto view = ReadQuestView(servers, seedA.characterId, 4005);
            ok = view.found && view.firstProgress == i + 1;
        }
        // 进入 Map3：Portal 8001 -> Map2 -> Portal 8003 (1750,1000 旁) -> Map3 (200,300)。
        ok = ok && UsePortalTo(servers, clientA, seedA.characterId, 1, 950.0f, 300.0f, 2);
        ok = ok && UsePortalTo(servers, clientA, seedA.characterId, 3, 1750.0f, 1000.0f, 3);
        ok = ok &&
             WaitQuestState(servers, seedA.characterId, 4005, QuestState::ReadyToTurnIn, 5000);
        Check("MultiObjectiveCheck: 4005 ReadyToTurnIn when kill 5/5 AND core 2/2 AND "
              "Ancient Ruins entered",
              ok);

        const std::size_t deltaBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::InventoryDeltaEvent);
        const std::size_t turnInBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent);
        clientA.controller.SendQuestTurnIn(4005);
        WorldNetworkEvent turnIn;
        ok = ok &&
             WaitRecordedFrom(clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent,
                              turnInBaseline,
                              [&](const WorldNetworkEvent& e) {
                                  return e.questId == 4005 && e.success;
                              },
                              turnIn);
        WorldNetworkEvent delta;
        ok = ok &&
             WaitRecordedFrom(clientA, WorldNetworkEvent::Type::InventoryDeltaEvent,
                              deltaBaseline,
                              [&](const WorldNetworkEvent& e) {
                                  return e.itemDefinitionId == kItemTravelerArmorId &&
                                         e.inventoryOpcode == 1;
                              },
                              delta);
        const std::int64_t armorRows =
            QueryScalar(servers.dbPath,
                        std::string("SELECT COUNT(*) FROM inventory_items WHERE character_id = ") +
                            std::to_string(static_cast<long long>(seedA.characterId)) +
                            " AND item_definition_id = " + std::to_string(kItemTravelerArmorId) +
                            ";");
        Check("QuestItemRewardCheck: TurnIn 4005 grants Traveler Armor (delta + DB row)",
              ok && armorRows >= 1);
        // 回 Map1（后续离线/上限等检查的布景位置在 Map1）：Map3 8004(150,300)->Map2，
        // Map2 8002(150,500)->Map1。
        ok = ok && UsePortalTo(servers, clientA, seedA.characterId, 4, 150.0f, 300.0f, 2);
        ok = ok && UsePortalTo(servers, clientA, seedA.characterId, 2, 150.0f, 500.0f, 1);
        Check("QuestChecks: 4005 turned in + returned to Map1", ok);
    }

    // ---- QuestItemInventoryFullCheck（一百一十三） ----
    {
        servers.world->TestFillInventory(seedA.characterId);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        // 阶段25：4005 三目标（kill5/collect2/area1）全部就绪 -> ReadyToTurnIn。
        servers.world->TestSeedQuestProgress(
            seedA.characterId, 4005, {{40051, 5}, {40052, 2}, {40053, 1}},
            QuestState::ReadyToTurnIn);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const std::int64_t goldBefore =
            QueryCharacterColumn(servers.dbPath, seedA.characterId, "gold");
        const std::int64_t expBefore = QueryCharacterColumn(servers.dbPath, seedA.characterId, "exp");
        const std::size_t turnInBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent);
        clientA.controller.SendQuestTurnIn(4005);
        WorldNetworkEvent response;
        bool ok = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent, turnInBaseline,
            [&](const WorldNetworkEvent& e) {
                return e.questId == 4005 && !e.success &&
                       e.questResultCode ==
                           static_cast<std::uint8_t>(QuestResultCode::InventoryFull);
            },
            response);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const auto view = ReadQuestView(servers, seedA.characterId, 4005);
        const std::int64_t goldAfter = QueryCharacterColumn(servers.dbPath, seedA.characterId, "gold");
        const std::int64_t expAfter = QueryCharacterColumn(servers.dbPath, seedA.characterId, "exp");
        ok = ok && view.found &&
             view.state == static_cast<std::uint8_t>(QuestState::ReadyToTurnIn) &&
             goldBefore == goldAfter && expBefore == expAfter;
        Check("QuestItemInventoryFullCheck: full bag -> InventoryFull, quest stays Ready, "
              "no partial rewards",
              ok);
        servers.world->TestSeedQuestProgress(
            seedA.characterId, 4005, {{40051, 5}, {40052, 2}, {40053, 1}},
            QuestState::Completed);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // ---- QuestRequestSpamCheck（一百二十九）：A 的 4003 处于 ReadyToTurnIn ----
    {
        const auto view = ReadQuestView(servers, seedA.characterId, 4003);
        const bool readyBefore =
            view.found && view.state == static_cast<std::uint8_t>(QuestState::ReadyToTurnIn);
        if (readyBefore) {
            const std::size_t turnInBaseline =
                CountEventsOf(clientA, WorldNetworkEvent::Type::QuestTurnInResponseEvent);
            const std::size_t rewardBaseline =
                CountEventsOf(clientA, WorldNetworkEvent::Type::QuestRewardGrantedEvent);
            for (int i = 0; i < 100; ++i) {
                clientA.client().SendQuestTurnIn(777777, 4003);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            clientA.DrainEvents();
            int successCount = 0;
            const auto& responses = clientA.recorded[WorldTestClient::IndexOf(
                WorldNetworkEvent::Type::QuestTurnInResponseEvent)];
            for (auto it = responses.begin() + static_cast<std::ptrdiff_t>(turnInBaseline);
                 it != responses.end(); ++it) {
                if (it->questId == 4003 && it->success) {
                    ++successCount;
                }
            }
            const auto& rewards = clientA.recorded[WorldTestClient::IndexOf(
                WorldNetworkEvent::Type::QuestRewardGrantedEvent)];
            int rewardCount = 0;
            for (auto it = rewards.begin() + static_cast<std::ptrdiff_t>(rewardBaseline);
                 it != rewards.end(); ++it) {
                if (it->questId == 4003) {
                    ++rewardCount;
                }
            }
            Check("QuestRequestSpamCheck: 100x TurnIn -> exactly 1 success / 1 reward",
                  successCount == 1 && rewardCount == 1);
        } else {
            Check("QuestRequestSpamCheck: 100x TurnIn -> exactly 1 success / 1 reward", false);
        }
    }

    // ---- B 的 ReachLevel 链（一百零四/一百三十二）：杀 5 升 2 -> TurnIn 4001
    //      （+100 exp -> L2/125）-> 接 4003 (0/1) -> 杀 3 升 3（事件推进 Ready，
    //      指令五十六）----
    {
        bool ok = true;
        for (std::uint32_t expected = 1; expected <= 5 && ok; ++expected) {
            const std::uint64_t slimeId = FindAliveSlime(servers);
            MoveSlimeNear(servers, slimeId, 100.0f, 80.0f);
            ok = ok && TeleportPlayer(clientB, servers, seedB.characterId, 60.0f, 80.0f);
            std::uint64_t attackSeq = 8000 + expected * 100;
            (void)BasicAttackUntilDead(servers, clientB, slimeId, attackSeq);
            ok = ok && WaitQuestProgress(servers, seedB.characterId, 4001, expected, 6000);
        }
        // B：5 杀 = L2/25。接 4003（前置 4001 未 Completed -> 应拒绝）。
        clientB.controller.SendQuestAccept(4003);
        WorldNetworkEvent prereq;
        ok = ok &&
             WaitRecordedFrom(
                 clientB, WorldNetworkEvent::Type::QuestAcceptResponseEvent,
                 CountEventsOf(clientB, WorldNetworkEvent::Type::QuestAcceptResponseEvent),
                 [&](const WorldNetworkEvent& e) {
                     return e.questId == 4003 && !e.success &&
                            e.questResultCode ==
                                static_cast<std::uint8_t>(QuestResultCode::PrerequisiteNotMet);
                 },
                 prereq, 4000);
        // TurnIn 4001（+100 exp -> L2/125）。
        const std::size_t turnInBaseline =
            CountEventsOf(clientB, WorldNetworkEvent::Type::QuestTurnInResponseEvent);
        clientB.controller.SendQuestTurnIn(4001);
        WorldNetworkEvent turnIn;
        ok = ok &&
             WaitRecordedFrom(clientB, WorldNetworkEvent::Type::QuestTurnInResponseEvent,
                              turnInBaseline,
                              [&](const WorldNetworkEvent& e) {
                                  return e.questId == 4001 && e.success;
                              },
                              turnIn);
        // B 满足前置：接 4003（L2 -> 0/1 InProgress）。
        clientB.controller.SendQuestAccept(4003);
        WorldNetworkEvent bAccept3;
        ok = ok &&
             WaitRecordedFrom(
                 clientB, WorldNetworkEvent::Type::QuestAcceptResponseEvent,
                 CountEventsOf(clientB, WorldNetworkEvent::Type::QuestAcceptResponseEvent),
                 [&](const WorldNetworkEvent& e) { return e.questId == 4003 && e.success; },
                 bAccept3, 4000);
        ok = ok &&
             WaitQuestState(servers, seedB.characterId, 4003, QuestState::InProgress, 4000);
        // 杀 3 只（75 exp -> L2/200 -> L3）：升级事件推进 4003 -> Ready（指令五十六）。
        for (int i = 0; i < 3 && ok; ++i) {
            const std::uint64_t slimeId = FindAliveSlime(servers);
            MoveSlimeNear(servers, slimeId, 100.0f, 80.0f);
            std::uint64_t attackSeq = 8700 + i * 100;
            (void)BasicAttackUntilDead(servers, clientB, slimeId, attackSeq);
        }
        ok = ok && WaitUntil(
                       [&] {
                           return QueryCharacterColumn(servers.dbPath, seedB.characterId,
                                                       "level") == 3;
                       },
                       4000) &&
             WaitQuestState(servers, seedB.characterId, 4003, QuestState::ReadyToTurnIn, 4000);
        Check("ReachLevelKillLevelUpCheck: level2 accept 0/1 -> kill levelup 3 -> ReachLevel "
              "ready (level event integration)",
              ok);
    }

    // ---- AbandonCheck（一百一十六）/ ReacceptAfterAbandonCheck（一百一十七）：
    //      B 接 4002 -> 拾 1 core (1/3) -> Abandon（进度清零）-> 重接（按持有量重算）----
    {
        bool ok = true;
        // 先接 4002（前置 4001 已 Completed）。
        clientB.controller.SendQuestAccept(4002);
        ok = ok && WaitQuestState(servers, seedB.characterId, 4002, QuestState::InProgress, 5000);
        ok = ok && servers.world->TestSpawnDrop(60.0f, 80.0f, 1, seedB.characterId,
                                                kItemSlimeCoreId);
        WorldNetworkEvent spawnEvent;
        ok = ok &&
             WaitRecordedFrom(
                 clientB, WorldNetworkEvent::Type::WorldItemSpawnEvent,
                 CountEventsOf(clientB, WorldNetworkEvent::Type::WorldItemSpawnEvent),
                 [&](const WorldNetworkEvent& e) {
                     return e.itemDefinitionId == kItemSlimeCoreId;
                 },
                 spawnEvent, 4000);
        clientB.controller.SendPickup(spawnEvent.dropEntityId);
        const bool picked = WaitQuestProgress(servers, seedB.characterId, 4002, 1, 6000);
        const std::size_t baseline =
            CountEventsOf(clientB, WorldNetworkEvent::Type::QuestAbandonResponseEvent);
        clientB.controller.SendQuestAbandon(4002);
        WorldNetworkEvent response;
        ok = ok && picked &&
             WaitRecordedFrom(clientB, WorldNetworkEvent::Type::QuestAbandonResponseEvent,
                              baseline,
                              [&](const WorldNetworkEvent& e) {
                                  std::printf("[Diag] Abandon: abandon response success=%d code=%u\n",
                                              static_cast<int>(e.success),
                                              static_cast<unsigned>(e.questResultCode));
                                  return e.questId == 4002 && e.success;
                              },
                              response);
        ok = ok && WaitQuestState(servers, seedB.characterId, 4002, QuestState::Abandoned);
        clientB.controller.SendQuestAccept(4002);
        WorldNetworkEvent reaccept;
        ok = ok &&
             WaitRecordedFrom(
                 clientB, WorldNetworkEvent::Type::QuestAcceptResponseEvent,
                 CountEventsOf(clientB, WorldNetworkEvent::Type::QuestAcceptResponseEvent),
                 [&](const WorldNetworkEvent& e) {
                     if (e.questId == 4002 && !e.success) {
                         std::printf("[Diag] Abandon: re-accept rejected code=%u\n",
                                     static_cast<unsigned>(e.questResultCode));
                     }
                     return e.questId == 4002 && e.success;
                 },
                 reaccept);
        ok = ok && WaitQuestState(servers, seedB.characterId, 4002, QuestState::InProgress);
        const auto after = ReadQuestView(servers, seedB.characterId, 4002);
        if (!ok || after.firstProgress != 1) {
            std::printf("[Diag] Abandon: ok=%d picked=%d afterState=%d afterProgress=%u\n",
                        static_cast<int>(ok), static_cast<int>(picked),
                        after.found ? static_cast<int>(after.state) : -1,
                        after.found ? after.firstProgress : 0u);
        }
        // Abandon 清零；重接后按"当前拥有量"初始校验 -> 背包仍有 1 core -> 1/3。
        Check("AbandonCheck/ReacceptAfterAbandonCheck: abandon clears, re-accept re-evaluates "
              "owned quantity",
              ok && after.found && after.firstProgress == 1);
    }

    // ---- QuestPersistenceCheck（一百二十）：B 拾 1 core -> 1/3 断线重登仍 1/3 ----
    {
        bool ok = servers.world->TestSpawnDrop(60.0f, 80.0f, 1, seedB.characterId,
                                               kItemSlimeCoreId);
        WorldNetworkEvent spawnEvent;
        ok = ok &&
             WaitRecordedFrom(
                 clientB, WorldNetworkEvent::Type::WorldItemSpawnEvent,
                 CountEventsOf(clientB, WorldNetworkEvent::Type::WorldItemSpawnEvent),
                 [&](const WorldNetworkEvent& e) {
                     return e.itemDefinitionId == kItemSlimeCoreId;
                 },
                 spawnEvent, 4000);
        clientB.controller.SendPickup(spawnEvent.dropEntityId);
        // 背包已有 1 core（Abandon 检查遗留）-> 再拾 1 枚 -> 2/3。
        ok = ok && WaitQuestProgress(servers, seedB.characterId, 4002, 2, 6000);
        clientB.Disconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        const std::int64_t dbProgress =
            QueryScalar(servers.dbPath,
                        "SELECT progress FROM character_quest_objectives WHERE character_id = " +
                            std::to_string(static_cast<long long>(seedB.characterId)) +
                            " AND quest_id = 4002 AND objective_id = 40021;");
        const std::string ticketB = TicketFor(servers.login, seedB);
        const bool reentered = !ticketB.empty() && clientB.ConnectAndEnter(ticketB, 8000);
        bool snapshotOk = false;
        if (reentered) {
            snapshotOk = WaitUntil(
                [&] {
                    clientB.DrainEvents();
                    const auto view = ReadQuestView(servers, seedB.characterId, 4002);
                    return view.found && view.firstProgress == 2;
                },
                5000);
        }
        Check("QuestPersistenceCheck: disconnect/relogin keeps 2/3 (DB + snapshot)",
              ok && dbProgress == 2 && reentered && snapshotOk);
    }

    // ---- QuestWorldRestartPersistenceCheck（一百二十一） ----
    {
        clientB.Disconnect();
        servers.StopWorld();
        const bool restarted = servers.StartWorld();
        const std::string ticketB = TicketFor(servers.login, seedB);
        const bool reentered =
            restarted && !ticketB.empty() && clientB.ConnectAndEnter(ticketB, 8000);
        bool questRestored = false;
        if (reentered) {
            questRestored = WaitUntil(
                [&] {
                    clientB.DrainEvents();
                    const auto view = ReadQuestView(servers, seedB.characterId, 4002);
                    return view.found && view.firstProgress == 2 &&
                           view.state == static_cast<std::uint8_t>(QuestState::InProgress);
                },
                5000);
        }
        const std::int64_t aCompleted =
            QueryScalar(servers.dbPath,
                        "SELECT COUNT(*) FROM character_quests WHERE character_id = " +
                            std::to_string(static_cast<long long>(seedA.characterId)) +
                            " AND quest_id = 4001 AND state = 3;");
        Check("QuestWorldRestartPersistenceCheck: quest progress survives world restart",
              restarted && reentered && questRestored && aCompleted == 1);
    }

    // ---- QuestSnapshotPeriodicCheck（一百二十三）+ 快照内容（一百二十二） ----
    {
        const std::size_t baseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestSnapshotEvent);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        clientA.DrainEvents();
        const std::size_t after =
            CountEventsOf(clientA, WorldNetworkEvent::Type::QuestSnapshotEvent);
        Check("QuestSnapshotPeriodicCheck: periodic snapshot correction (~300ms test / 10s prod)",
              after - baseline >= 2);
        bool contentOk = false;
        WorldNetworkEvent snap;
        if (FindRecordedFrom(clientA, WorldNetworkEvent::Type::QuestSnapshotEvent, baseline,
                             [](const WorldNetworkEvent&) { return true; }, snap)) {
            int completed = 0;
            int active = 0; // InProgress 或 ReadyToTurnIn
            for (const auto& quest : snap.questSnapshot) {
                if (quest.state == static_cast<std::uint8_t>(QuestState::Completed)) {
                    ++completed;
                }
                if (quest.state == static_cast<std::uint8_t>(QuestState::InProgress) ||
                    quest.state == static_cast<std::uint8_t>(QuestState::ReadyToTurnIn)) {
                    ++active;
                }
            }
            // A：4001/4002/4004/4005 Completed + 4003（TurnIn 后 Completed 或 Ready）。
            contentOk = completed >= 4 && (completed + active) >= 5;
        }
        Check("QuestSnapshotCheck: snapshot contains completed and active quests", contentOk);
    }

    // ---- QuestLogLimitCheck（九十一）：E + 20 个白盒 active -> 第 21 个 QuestLogFull ----
    {
        // 世界重启后所有会话已失效——必须先 drain 事件刷新状态再按需重进。
        clientE.DrainEvents();
        bool eReady = clientE.controller.IsWorldReady();
        if (!eReady) {
            const std::string ticketE2 = TicketFor(servers.login, seedE);
            eReady = !ticketE2.empty() && clientE.ConnectAndEnter(ticketE2, 8000);
            std::printf("[Diag] QuestLogLimit: E re-entered=%d\n", static_cast<int>(eReady));
        }
        for (QuestId fake = 9001; fake <= 9020; ++fake) {
            servers.world->TestSeedQuestProgress(seedE.characterId, fake, {},
                                                 QuestState::InProgress);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        const std::size_t baseline =
            CountEventsOf(clientE, WorldNetworkEvent::Type::QuestAcceptResponseEvent);
        clientE.controller.SendQuestAccept(4001);
        WorldNetworkEvent response;
        const auto activeCount = RunOnWorldIo(servers.worldService, [&] {
            auto player = servers.world->FindPlayerByCharacter(seedE.characterId);
            return player ? player->Quests().CountActive() : std::size_t{0};
        });
        bool ok = eReady &&
                  WaitRecordedFrom(
                      clientE, WorldNetworkEvent::Type::QuestAcceptResponseEvent, baseline,
                      [&](const WorldNetworkEvent& e) {
                          static bool printed = false;
                          if (!printed) {
                              printed = true;
                              std::printf("[Diag] QuestLogLimit: first response success=%d code=%u\n",
                                          static_cast<int>(e.success),
                                          static_cast<unsigned>(e.questResultCode));
                          }
                          return !e.success &&
                                 e.questResultCode ==
                                     static_cast<std::uint8_t>(QuestResultCode::QuestLogFull);
                      },
                      response, 5000);
        if (!ok) {
            std::printf("[Diag] QuestLogLimit: eReady=%d activeCount=%zu\n",
                        static_cast<int>(eReady), activeCount);
        }
        Check("QuestLogLimitCheck: 20 active quests -> 21st rejected QuestLogFull", ok);
    }

    // ---- OfflineDotKillQuestCheck（九十六） ----
    {
        const std::string ticketC = TicketFor(servers.login, seedC);
        const bool entered = !ticketC.empty() && clientC.ConnectAndEnter(ticketC, 8000);
        bool ok = entered;
        bool acceptOk = false;
        bool killsOk = false;
        bool lowHpOk = false;
        bool burnOk = false;
        if (entered) {
            clientC.controller.SendQuestAccept(4001);
            acceptOk = WaitQuestState(servers, seedC.characterId, 4001, QuestState::InProgress, 5000);
            ok = ok && acceptOk;
            // 真实杀 4 只（DB 逐次推进）。
            killsOk = true;
            for (std::uint32_t expected = 1; expected <= 4 && ok; ++expected) {
                const std::uint64_t slimeId = FindAliveSlime(servers);
                MoveSlimeNear(servers, slimeId, 120.0f, 940.0f);
                ok = ok && TeleportPlayer(clientC, servers, seedC.characterId, 60.0f, 940.0f);
                std::uint64_t attackSeq = 9000 + expected * 100;
                (void)BasicAttackUntilDead(servers, clientC, slimeId, attackSeq);
                const bool progressed =
                    WaitQuestProgress(servers, seedC.characterId, 4001, expected, 6000);
                killsOk = killsOk && progressed;
                ok = ok && progressed;
            }
            // 第 5 只：打到低血量（普攻 0.8s CD——按服务器实际 HP 循环）+ Burn(8/2s)。
            // 注意：C 杀 4 只后已 L2（攻击 22-2=20/刀）——停在 3 刀（HP=20），
            // 不能打第 4 刀（直接击杀则失去离线 DOT 语义）。
            const std::uint64_t slimeId = FindAliveSlime(servers);
            MoveSlimeNear(servers, slimeId, 120.0f, 940.0f);
            std::uint64_t attackSeq = 9600;
            lowHpOk = WaitUntil(
                [&] {
                    const auto monster = servers.world->FindMonster(slimeId);
                    if (!monster || !monster->Alive()) {
                        return true; // 已死（意外）：终止（lowHpOk 保持 false）
                    }
                    if (monster->CurrentHp() <= 24) {
                        lowHpOk = true;
                        return true;
                    }
                    clientC.client().SendAttack(attackSeq++, kTypeMonster, slimeId);
                    std::this_thread::sleep_for(std::chrono::milliseconds(850)); // 攻击 CD 0.8s
                    return false;
                },
                12000);
            ok = ok && lowHpOk;
            {
                const auto burnOutcome = RunOnWorldIo(servers.worldService, [&] {
                    return servers.world->ApplyStatusToTarget(kTypeMonster, slimeId,
                                                              kStatusEffectIdBurn, 1, kTypePlayer,
                                                              seedC.characterId, 0u);
                });
                burnOk = burnOutcome.result == StatusApplyResult::Applied ||
                         burnOutcome.result == StatusApplyResult::Refreshed;
                std::printf("[Diag] OfflineDot burn result=%u\n",
                            static_cast<unsigned>(burnOutcome.result));
                ok = ok && burnOk;
            }
            // C 断线（Burn 仍在怪身上）。
            clientC.Disconnect();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            const bool died = WaitUntil(
                [&] {
                    const auto monster = servers.world->FindMonster(slimeId);
                    return monster && !monster->Alive();
                },
                20000);
            // DB：4001 -> ReadyToTurnIn（指令六十二/六十三：不加载假 PlayerSession）。
            bool dbReady = false;
            ok = ok && died &&
                 WaitUntil(
                     [&] {
                         const std::int64_t state = QueryScalar(
                             servers.dbPath,
                             "SELECT state FROM character_quests WHERE character_id = " +
                                 std::to_string(static_cast<long long>(seedC.characterId)) +
                                 " AND quest_id = 4001;");
                         dbReady = state == static_cast<std::int64_t>(QuestState::ReadyToTurnIn);
                         return dbReady;
                     },
                     5000);
            // 重登：Snapshot 显示 ReadyToTurnIn（指令六十三/六十四）。
            const std::string ticketC2 = TicketFor(servers.login, seedC);
            const bool reentered = !ticketC2.empty() && clientC.ConnectAndEnter(ticketC2, 8000);
            bool snapshotReady = false;
            if (reentered) {
                snapshotReady = WaitUntil(
                    [&] {
                        clientC.DrainEvents();
                        for (const auto& e : clientC.recorded[WorldTestClient::IndexOf(
                                 WorldNetworkEvent::Type::QuestSnapshotEvent)]) {
                            for (const auto& quest : e.questSnapshot) {
                                if (quest.questId == 4001 &&
                                    quest.state ==
                                        static_cast<std::uint8_t>(QuestState::ReadyToTurnIn)) {
                                    return true;
                                }
                            }
                        }
                        return false;
                    },
                    5000);
            }
            if (!ok || !dbReady || !reentered || !snapshotReady) {
                const std::int64_t dbState = QueryScalar(
                    servers.dbPath,
                    "SELECT state FROM character_quests WHERE character_id = " +
                        std::to_string(static_cast<long long>(seedC.characterId)) +
                        " AND quest_id = 4001;");
                const std::int64_t dbProgress = QueryScalar(
                    servers.dbPath,
                    "SELECT progress FROM character_quest_objectives WHERE character_id = " +
                        std::to_string(static_cast<long long>(seedC.characterId)) +
                        " AND quest_id = 4001 AND objective_id = 40011;");
                std::printf("[Diag] OfflineDot: ok=%d acceptOk=%d killsOk=%d lowHpOk=%d burnOk=%d "
                            "died=%d dbReady=%d reentered=%d snap=%d dbState=%lld dbProgress=%lld\n",
                            static_cast<int>(ok), static_cast<int>(acceptOk),
                            static_cast<int>(killsOk), static_cast<int>(lowHpOk),
                            static_cast<int>(burnOk), static_cast<int>(died),
                            static_cast<int>(dbReady), static_cast<int>(reentered),
                            static_cast<int>(snapshotReady),
                            static_cast<long long>(dbState),
                            static_cast<long long>(dbProgress));
            }
            Check("OfflineDotKillQuestCheck: offline DOT killer advances quest via DB -> "
                  "ReadyToTurnIn restored on relogin",
                  ok && dbReady && reentered && snapshotReady);
        } else {
            Check("OfflineDotKillQuestCheck: offline DOT killer advances quest via DB -> "
                  "ReadyToTurnIn restored on relogin",
                  false);
        }
    }

    // ---- Progression 回归（一百三十二）：杀怪 EXP/Gold 继续正常 ----
    {
        const std::int64_t expBefore = QueryCharacterColumn(servers.dbPath, seedB.characterId, "exp");
        const std::uint64_t slimeId = FindAliveSlime(servers);
        MoveSlimeNear(servers, slimeId, 100.0f, 80.0f);
        (void)TeleportPlayer(clientB, servers, seedB.characterId, 60.0f, 80.0f);
        std::uint64_t attackSeq = 11000;
        (void)BasicAttackUntilDead(servers, clientB, slimeId, attackSeq);
        const bool progressed = WaitUntil(
            [&] {
                return QueryCharacterColumn(servers.dbPath, seedB.characterId, "exp") >=
                       expBefore + 25;
            },
            6000);
        Check("ProgressionRegressionCheck: kills still grant EXP (25 per slime)", progressed);
    }

    // ---- QuestDbMigrationCheck（一百三十）+ QuestNoDefinitionPersistenceCheck（一百三十一）----
    {
        const std::string oldDbPath = TempDbPath("world_quest_migration_v3");
        RemoveDb(oldDbPath);
        bool ok = true;
        {
            Database legacy;
            ok = legacy.Open(oldDbPath, error);
            const char* legacySql[] = {
                "CREATE TABLE schema_version (id INTEGER PRIMARY KEY CHECK(id = 1), "
                "version INTEGER NOT NULL);",
                "INSERT INTO schema_version (id, version) VALUES (1, 3);",
                "CREATE TABLE accounts (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT "
                "NULL UNIQUE COLLATE NOCASE, password_hash TEXT NOT NULL, created_at INTEGER NOT "
                "NULL, last_login_at INTEGER, status INTEGER NOT NULL DEFAULT 0, "
                "failed_login_count INTEGER NOT NULL DEFAULT 0, locked_until INTEGER);",
                "CREATE TABLE characters (id INTEGER PRIMARY KEY AUTOINCREMENT, account_id "
                "INTEGER NOT NULL, name TEXT NOT NULL UNIQUE, class_id INTEGER NOT NULL, gender "
                "INTEGER NOT NULL, level INTEGER NOT NULL DEFAULT 1, exp INTEGER NOT NULL DEFAULT "
                "0, map_id INTEGER NOT NULL DEFAULT 1, position_x REAL NOT NULL DEFAULT 0, "
                "position_y REAL NOT NULL DEFAULT 0, created_at INTEGER NOT NULL, "
                "last_played_at INTEGER, deleted INTEGER NOT NULL DEFAULT 0, gold INTEGER NOT "
                "NULL DEFAULT 0, FOREIGN KEY(account_id) REFERENCES accounts(id));",
                "CREATE TABLE sessions (id INTEGER PRIMARY KEY AUTOINCREMENT, account_id INTEGER "
                "NOT NULL, session_token_hash TEXT NOT NULL UNIQUE, created_at INTEGER NOT NULL, "
                "expires_at INTEGER NOT NULL, revoked INTEGER NOT NULL DEFAULT 0);",
                "CREATE TABLE inventory_items (instance_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "character_id INTEGER NOT NULL, item_definition_id INTEGER NOT NULL, quantity "
                "INTEGER NOT NULL, slot_index INTEGER NOT NULL, created_at INTEGER NOT NULL);",
                "INSERT INTO accounts (username, password_hash, created_at) VALUES "
                "('legacy_user', 'x', 1);",
                "INSERT INTO characters (account_id, name, class_id, gender, level, exp, "
                "created_at) VALUES (1, 'LegacyHero', 1, 1, 7, 300, 1);",
                "INSERT INTO inventory_items (character_id, item_definition_id, quantity, "
                "slot_index, created_at) VALUES (1, 3003, 5, 0, 1);",
            };
            for (const char* sql : legacySql) {
                ok = ok && legacy.Execute(sql, error);
            }
            legacy.Close();
        }
        {
            Database upgraded;
            ok = ok && upgraded.Open(oldDbPath, error) && InitializeSchema(upgraded, error);
            const std::int64_t version =
                QueryScalar(oldDbPath, "SELECT version FROM schema_version WHERE id = 1;");
            const std::int64_t characterRows =
                QueryScalar(oldDbPath, "SELECT COUNT(*) FROM characters;");
            const std::int64_t itemRows =
                QueryScalar(oldDbPath, "SELECT COUNT(*) FROM inventory_items;");
            const std::int64_t levelRow =
                QueryScalar(oldDbPath, "SELECT level FROM characters WHERE id = 1;");
            const std::int64_t questRows =
                QueryScalar(oldDbPath, "SELECT COUNT(*) FROM character_quests;");
            const std::int64_t objectiveRows =
                QueryScalar(oldDbPath, "SELECT COUNT(*) FROM character_quest_objectives;");
            ok = ok && version == 4 && characterRows == 1 && itemRows == 1 && levelRow == 7 &&
                 questRows == 0 && objectiveRows == 0;
            upgraded.Close();
        }
        {
            // 数据库不保存任务定义（指令六十九/一百三十一）。
            Database schemaProbe;
            ok = ok && schemaProbe.Open(servers.dbPath, error);
            bool hasDefinitionColumn = false;
            {
                account::Statement stmt;
                ok = ok && stmt.Prepare(schemaProbe.Handle(),
                                        "PRAGMA table_info(character_quests);", error);
                while (stmt.Step(error)) {
                    const std::string column = stmt.ColumnText(1);
                    if (column == "name" || column == "description" ||
                        column == "objective_definition") {
                        hasDefinitionColumn = true;
                    }
                }
                ok = ok && error.empty() && !hasDefinitionColumn;
            }
            schemaProbe.Close();
        }
        RemoveDb(oldDbPath);
        Check("QuestDbMigrationCheck/QuestNoDefinitionPersistenceCheck: v3 -> v4 keeps data, "
              "quest tables added, no definitions in DB",
              ok);
    }

    clientA.Disconnect();
    clientB.Disconnect();
    clientE.Disconnect();
    servers.StopAll();
}
} // namespace

void RunWorldQuestChecks() {
    std::printf("[WorldQuest] logic checks begin\n");
    RunQuestLogicChecks();
    std::printf("[WorldQuest] chain checks begin\n");
    RunQuestChainChecks();
    std::printf("[WorldQuest] done (%d failures so far)\n", g_failures);
}

} // namespace worldtest
