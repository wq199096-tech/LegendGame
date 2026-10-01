// ---------------------------------------------------------------------------
// 阶段16：状态效果检查（Status Effect Core V0.16）。
// 仍链接 LegendWorldTests（不新增测试 exe，指令八十四）。
// A 部分：纯逻辑（StatusDefinition/StatusProtocol/容器策略/DerivedStats/
//         Tick catch-up/客户端 Remote 容器）无需服务器。
// B 部分：真实链路自管 servers（独立生命周期，端口 17240/17241/17242 串行复用）。
// 白盒施加经 worldService.Post 投递 io 线程执行（状态容器非线程安全，与
// Status Tick 100ms 串行化——阶段15 MoveMonsterTo 同一模式）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Shared/Monster/MonsterDefinition.h"
#include "Server/WorldServer/Monster/MonsterEntity.h"
#include "Server/WorldServer/PlayerSession.h"
#include "Server/WorldServer/Status/StatusEffectContainer.h"
#include "Server/WorldServer/Status/StatusEffectRegistry.h"
#include "Server/WorldServer/Status/StatusEffectService.h"
#include "Shared/Combat/CombatTypes.h"
#include "Shared/Monster/MonsterTypes.h"
#include "Shared/Network/ByteWriter.h"
#include "Shared/Skill/SkillDefinition.h"
#include "Shared/Skill/SkillTypes.h"
#include "Shared/Status/StatusEffectDefinition.h"
#include "Shared/Status/StatusEffectProtocol.h"
#include "Shared/Status/StatusEffectTypes.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <type_traits>
#include <utility>

namespace worldtest {

namespace {

using legend::world::ActiveStatusEffect;
using legend::world::ApplyEffect;
using legend::world::CombatEntityType;
using legend::world::ComputeDotDamage;
using legend::world::ExpireEffects;
using legend::world::kSkillIdBattleFocus;
using legend::world::kSkillIdCripplingStrike;
using legend::world::kSkillIdFireBolt;
using legend::world::kSkillIdQuickStrike;
using legend::world::kSkillIdWhirlwind;
using legend::world::kStatusEffectIdArmorBreak;
using legend::world::kStatusEffectIdBattleFocus;
using legend::world::kStatusEffectIdBurn;
using legend::world::kStatusEffectIdPoison;
using legend::world::kStatusEffectIdSlow;
using legend::world::MonsterEntity;
using legend::world::MonsterState;
using legend::world::RecalculateDerivedStats;
using legend::world::StatusApplyContext;
using legend::world::StatusApplyOutcome;
using legend::world::StatusApplyResult;
using legend::world::StatusEffectContainer;
using legend::world::StatusEffectId;
using legend::world::StatusEffectRegistry;
using legend::world::StatusRemovedReason;
using legend::world::TickEffect;

const std::uint8_t kTypePlayer = static_cast<std::uint8_t>(CombatEntityType::Player);
const std::uint8_t kTypeMonster = static_cast<std::uint8_t>(CombatEntityType::Monster);
// SkillTargetType::Self = 3（SkillCastRequest 的 targetType 语义，与 CombatEntityType 无关）。
const std::uint8_t kTargetSelf = static_cast<std::uint8_t>(legend::world::SkillTargetType::Self);
const std::uint8_t kTargetMonsterSkill =
    static_cast<std::uint8_t>(legend::world::SkillTargetType::Monster); // = 1
const std::uint8_t kSourceStatus =
    static_cast<std::uint8_t>(legend::world::CombatSource::StatusEffect);
const std::uint8_t kSourceSkill = static_cast<std::uint8_t>(legend::world::CombatSource::Skill);
const std::uint8_t kSourceBasic =
    static_cast<std::uint8_t>(legend::world::CombatSource::BasicAttack);

using StatusClock = std::chrono::steady_clock;

namespace CharacterRepository = legend::account::CharacterRepository;

// ---- io 线程白盒辅助（与 Status Tick 100ms 串行化）----

template <typename F>
auto RunOnWorldIo(net::NetworkService& service, F&& fn) -> std::invoke_result_t<F&> {
    using R = std::invoke_result_t<F&>;
    if constexpr (std::is_void_v<R>) {
        std::promise<void> promise;
        auto future = promise.get_future();
        service.Post([&promise, &fn]() {
            fn();
            promise.set_value();
        });
        future.get();
    } else {
        std::promise<R> promise;
        auto future = promise.get_future();
        service.Post([&promise, &fn]() { promise.set_value(fn()); });
        return future.get();
    }
}

// 白盒施加（source = 玩家 characterId；sourceSkillId=0 区别技能链路）。
StatusApplyOutcome ApplyWhitebox(WorldTestServers& servers, std::uint8_t targetType,
                                 std::uint64_t targetId, StatusEffectId effectId,
                                 std::uint64_t sourceCharacterId) {
    return RunOnWorldIo(servers.worldService, [&] {
        return servers.world->ApplyStatusToTarget(targetType, targetId, effectId, 1, kTypePlayer,
                                                  sourceCharacterId, 0u);
    });
}

// 白盒读怪物视图（单次 io 投递取多字段，避免多次往返）。
struct MonsterView {
    bool found = false;
    bool alive = false;
    std::uint32_t hp = 0;
    std::uint32_t defense = 0;
    std::uint32_t attack = 0;
    float moveSpeed = 0.0f;
    std::size_t statusCount = 0;
    std::uint8_t probeStacks = 0;
    std::int64_t probeNextTickMs = -1; // probe effect 的 nextTick 相对毫秒（-1 = 无该状态）
};

MonsterView ReadMonster(WorldTestServers& servers, std::uint64_t entityId,
                        StatusEffectId probeEffect = 0) {
    return RunOnWorldIo(servers.worldService, [&] {
        MonsterView view;
        auto monster = servers.world->FindMonster(entityId);
        if (!monster) {
            return view;
        }
        view.found = true;
        view.alive = monster->Alive();
        view.hp = monster->CurrentHp();
        view.defense = monster->EffectiveDefense();
        view.attack = monster->EffectiveAttackPower();
        view.moveSpeed = monster->EffectiveMoveSpeed();
        view.statusCount = monster->StatusEffects().Count();
        if (probeEffect != 0) {
            const auto* effect = monster->StatusEffects().Find(probeEffect);
            if (effect) {
                view.probeStacks = effect->Stacks();
                view.probeNextTickMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(effect->NextTickTime() -
                                                                          StatusClock::now())
                        .count();
            }
        }
        return view;
    });
}

// 白盒读玩家视图。
struct PlayerView {
    bool found = false;
    std::uint32_t attack = 0;
    std::uint32_t defense = 0;
    float moveSpeed = 0.0f;
    std::size_t statusCount = 0;
    std::uint32_t mana = 0;
    bool alive = false;
};

PlayerView ReadPlayer(WorldTestServers& servers, std::uint64_t characterId) {
    return RunOnWorldIo(servers.worldService, [&] {
        PlayerView view;
        auto player = servers.world->FindPlayerByCharacter(characterId);
        if (!player) {
            return view;
        }
        view.found = true;
        view.attack = player->EffectiveAttackPower();
        view.defense = player->EffectiveDefense();
        view.moveSpeed = player->EffectiveMoveSpeed();
        view.statusCount = player->StatusEffects().Count();
        view.mana = player->CurrentMana();
        view.alive = player->Alive();
        return view;
    });
}

// ---- 事件辅助 ----

int CountStatusEventsFor(const WorldTestClient& client, WorldNetworkEvent::Type type,
                         std::uint32_t effectId, std::uint8_t targetType, std::uint64_t targetId,
                         int reason = -1) {
    int n = 0;
    for (const auto& e :
         client.recorded[WorldTestClient::IndexOf(type)]) {
        const auto& s = e.status;
        if (s.effectId != effectId || s.targetType != targetType ||
            (targetId != 0 && s.targetEntityId != targetId)) {
            continue;
        }
        if (type == WorldNetworkEvent::Type::StatusRemovedEvent && reason >= 0 &&
            static_cast<int>(s.reason) != reason) {
            continue;
        }
        ++n;
    }
    return n;
}

int CountDotCombatEventsFor(const WorldTestClient& client, std::uint32_t sourceId,
                            std::uint64_t targetId, std::uint64_t attackerId = 0) {
    int n = 0;
    for (const auto& e :
         client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::CombatEvent)]) {
        if (e.sourceType == kSourceStatus && (sourceId == 0 || e.sourceId == sourceId) &&
            (targetId == 0 || e.targetId == targetId) &&
            (attackerId == 0 || e.attackerId == attackerId)) {
            ++n;
        }
    }
    return n;
}

bool WaitStatusEvent(WorldTestClient& client, WorldNetworkEvent::Type type,
                     const std::function<bool(const WorldNetworkEvent&)>& pred,
                     WorldNetworkEvent& out, int timeoutMs = 3000) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            const auto& events = client.recorded[WorldTestClient::IndexOf(type)];
            for (auto it = events.rbegin(); it != events.rend(); ++it) {
                if (pred(*it)) {
                    out = *it;
                    return true;
                }
            }
            return false;
        },
        timeoutMs);
}

// 顺序等待下一条 DOT CombatEvent（seenCount 基线在发送/施加前捕获）。
bool WaitDotCombatEvent(WorldTestClient& client, std::uint32_t sourceId, std::uint64_t targetId,
                        int& seenCount, WorldNetworkEvent& out, int timeoutMs = 4000) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            const auto& events =
                client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::CombatEvent)];
            for (int i = seenCount; i < static_cast<int>(events.size()); ++i) {
                const auto& e = events[static_cast<std::size_t>(i)];
                if (e.sourceType == kSourceStatus && e.sourceId == sourceId &&
                    e.targetId == targetId) {
                    out = e;
                    seenCount = i + 1;
                    return true;
                }
            }
            seenCount = static_cast<int>(events.size());
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

bool WaitMonsterVisible(WorldTestServers& servers, std::uint64_t characterId,
                        std::uint64_t monsterId, int timeoutMs = 4000) {
    return WaitUntil(
        [&] {
            auto player = servers.world->FindPlayerByCharacter(characterId);
            return player != nullptr && player->VisibleMonsters().count(monsterId) != 0;
        },
        timeoutMs);
}

// ===========================================================================
// A. 纯逻辑检查（无服务器）
// ===========================================================================

void RunStatusLogicChecks() {
    // ---- StatusDefinitionCheck（指令八十五：5 个 Definition 全字段）----
    {
        StatusEffectRegistry registry;
        bool ok = registry.FindEffect(9999) == nullptr;
        const auto* bf = registry.FindEffect(kStatusEffectIdBattleFocus);
        ok = ok && bf != nullptr && bf->name == "Battle Focus" &&
             bf->category == legend::world::StatusEffectCategory::Buff &&
             bf->durationMs == 10000u && bf->tickIntervalMs == 0u && bf->maxStacks == 1 &&
             bf->stackPolicy == legend::world::StatusEffectStackPolicy::RefreshDuration &&
             bf->attackFlatModifier == 10 && bf->defenseFlatModifier == 0 &&
             bf->moveSpeedMultiplier == 1.0f && bf->dotDamagePerStack == 0u;
        const auto* ab = registry.FindEffect(kStatusEffectIdArmorBreak);
        ok = ok && ab != nullptr && ab->name == "Armor Break" &&
             ab->category == legend::world::StatusEffectCategory::Debuff &&
             ab->durationMs == 8000u && ab->tickIntervalMs == 0u && ab->maxStacks == 3 &&
             ab->stackPolicy == legend::world::StatusEffectStackPolicy::AddStackRefresh &&
             ab->attackFlatModifier == 0 && ab->defenseFlatModifier == -2 &&
             ab->moveSpeedMultiplier == 1.0f && ab->dotDamagePerStack == 0u;
        const auto* burn = registry.FindEffect(kStatusEffectIdBurn);
        ok = ok && burn != nullptr && burn->durationMs == 8000u &&
             burn->tickIntervalMs == 2000u && burn->maxStacks == 1 &&
             burn->stackPolicy == legend::world::StatusEffectStackPolicy::RefreshDuration &&
             burn->dotDamagePerStack == 8u;
        const auto* poison = registry.FindEffect(kStatusEffectIdPoison);
        ok = ok && poison != nullptr && poison->durationMs == 6000u &&
             poison->tickIntervalMs == 1000u && poison->maxStacks == 3 &&
             poison->stackPolicy == legend::world::StatusEffectStackPolicy::AddStackRefresh &&
             poison->dotDamagePerStack == 4u;
        const auto* slow = registry.FindEffect(kStatusEffectIdSlow);
        ok = ok && slow != nullptr && slow->durationMs == 5000u && slow->tickIntervalMs == 0u &&
             slow->maxStacks == 1 &&
             slow->stackPolicy == legend::world::StatusEffectStackPolicy::RefreshDuration &&
             slow->moveSpeedMultiplier == 0.6f && slow->dotDamagePerStack == 0u;
        Check("StatusDefinitionCheck: 5 definitions config + unknown rejected", ok);
    }

    // ---- StatusProtocolRoundtripCheck（指令五十二~五十五）----
    {
        std::string error;
        bool ok = true;
        {
            legend::world::StatusEffectAppliedPayload p;
            p.instanceId = 41;
            p.effectId = 2003;
            p.targetType = kTypeMonster;
            p.targetEntityId = 9;
            p.sourceType = kTypePlayer;
            p.sourceEntityId = 1001;
            p.sourceSkillId = 1002;
            p.stacks = 1;
            p.durationMs = 8000;
            p.remainingMs = 7500;
            p.serverTime = 123456;
            std::vector<std::uint8_t> payload;
            legend::world::StatusEffectAppliedPayload d;
            ok = ok && legend::world::EncodeStatusEffectApplied(p, payload) &&
                 legend::world::DecodeStatusEffectApplied(payload.data(), payload.size(), d,
                                                          error) &&
                 d.instanceId == 41 && d.effectId == 2003 && d.targetType == kTypeMonster &&
                 d.targetEntityId == 9 && d.sourceEntityId == 1001 && d.sourceSkillId == 1002 &&
                 d.durationMs == 8000 && d.remainingMs == 7500 && d.serverTime == 123456;
        }
        {
            legend::world::StatusEffectUpdatedPayload p;
            p.instanceId = 42;
            p.effectId = 2002;
            p.targetType = kTypeMonster;
            p.targetEntityId = 3;
            p.stacks = 3;
            p.remainingMs = 7900;
            p.serverTime = 777;
            std::vector<std::uint8_t> payload;
            legend::world::StatusEffectUpdatedPayload d;
            ok = ok && legend::world::EncodeStatusEffectUpdated(p, payload) &&
                 legend::world::DecodeStatusEffectUpdated(payload.data(), payload.size(), d,
                                                          error) &&
                 d.instanceId == 42 && d.stacks == 3 && d.remainingMs == 7900;
        }
        {
            legend::world::StatusEffectRemovedPayload p;
            p.instanceId = 43;
            p.effectId = 2001;
            p.targetType = kTypePlayer;
            p.targetEntityId = 1001;
            p.reason = static_cast<std::uint8_t>(StatusRemovedReason::Expired);
            p.serverTime = 999;
            std::vector<std::uint8_t> payload;
            legend::world::StatusEffectRemovedPayload d;
            ok = ok && legend::world::EncodeStatusEffectRemoved(p, payload) &&
                 legend::world::DecodeStatusEffectRemoved(payload.data(), payload.size(), d,
                                                          error) &&
                 d.instanceId == 43 &&
                 d.reason == static_cast<std::uint8_t>(StatusRemovedReason::Expired);
        }
        {
            legend::world::StatusEffectSnapshotPayload p;
            p.targetType = kTypeMonster;
            p.targetEntityId = 5;
            p.serverTime = 1234;
            for (int i = 0; i < 3; ++i) {
                legend::world::StatusEffectSnapshotEntry entry;
                entry.instanceId = static_cast<std::uint64_t>(50 + i);
                entry.effectId = static_cast<std::uint32_t>(2001 + i);
                entry.stacks = static_cast<std::uint8_t>(i + 1);
                entry.remainingMs = static_cast<std::uint32_t>(5000 - i);
                p.effects.push_back(entry);
            }
            std::vector<std::uint8_t> payload;
            legend::world::StatusEffectSnapshotPayload d;
            ok = ok && legend::world::EncodeStatusEffectSnapshot(p, payload) &&
                 legend::world::DecodeStatusEffectSnapshot(payload.data(), payload.size(), d,
                                                           error) &&
                 d.targetEntityId == 5 && d.effects.size() == 3 && d.effects[2].effectId == 2003 &&
                 d.effects[2].stacks == 3;
        }
        Check("StatusProtocolRoundtripCheck: Applied/Updated/Removed/Snapshot roundtrip", ok);
    }

    // ---- MalformedStatusChecks（指令八十二/一百二十八/一百二十九）----
    {
        std::string error;
        bool ok = true;
        {
            legend::world::StatusEffectAppliedPayload p;
            p.instanceId = 1;
            p.effectId = 2001;
            std::vector<std::uint8_t> payload;
            legend::world::EncodeStatusEffectApplied(p, payload);
            legend::world::StatusEffectAppliedPayload d;
            ok = ok && !legend::world::DecodeStatusEffectApplied(payload.data(), payload.size() - 4,
                                                                 d, error);
            auto padded = payload;
            padded.push_back(0xAB);
            ok = ok && !legend::world::DecodeStatusEffectApplied(padded.data(), padded.size(), d,
                                                                 error);
        }
        {
            legend::world::StatusEffectUpdatedPayload p;
            std::vector<std::uint8_t> payload;
            legend::world::EncodeStatusEffectUpdated(p, payload);
            legend::world::StatusEffectUpdatedPayload d;
            ok = ok && !legend::world::DecodeStatusEffectUpdated(payload.data(), payload.size() - 2,
                                                                 d, error);
            legend::world::StatusEffectRemovedPayload r;
            std::vector<std::uint8_t> rpayload;
            legend::world::EncodeStatusEffectRemoved(r, rpayload);
            legend::world::StatusEffectRemovedPayload rd;
            ok = ok && !legend::world::DecodeStatusEffectRemoved(rpayload.data(),
                                                                 rpayload.size() - 1, rd, error);
        }
        {
            // Snapshot：count 大于实际 payload -> 拒绝（指令一百二十九）
            std::vector<std::uint8_t> bad;
            legend::network::ByteWriter w(bad);
            w.WriteUInt8(kTypeMonster);
            w.WriteUInt64(5);
            w.WriteUInt64(1234);
            w.WriteUInt16(5); // 声明 5 条
            w.WriteUInt64(50);
            w.WriteUInt32(2001);
            w.WriteUInt8(1);
            w.WriteUInt32(1000); // 只有 1 条
            legend::world::StatusEffectSnapshotPayload d;
            ok = ok && !legend::world::DecodeStatusEffectSnapshot(bad.data(), bad.size(), d, error);
            std::vector<std::uint8_t> truncated(bad.begin(), bad.begin() + 15);
            ok = ok && !legend::world::DecodeStatusEffectSnapshot(truncated.data(),
                                                                  truncated.size(), d, error);
        }
        Check("MalformedStatusChecks: truncated/trailing/count>payload rejected", ok);
    }

    // ---- StatusSnapshotCountLimitCheck（指令五十六/一百二十七：32 成功 / 33 拒绝）----
    {
        std::string error;
        bool ok = true;
        {
            legend::world::StatusEffectSnapshotPayload p;
            p.targetType = kTypePlayer;
            p.targetEntityId = 1;
            p.effects.resize(legend::world::kStatusEffectMaxSnapshotCount);
            std::vector<std::uint8_t> payload;
            legend::world::StatusEffectSnapshotPayload d;
            ok = ok && legend::world::EncodeStatusEffectSnapshot(p, payload) &&
                 legend::world::DecodeStatusEffectSnapshot(payload.data(), payload.size(), d,
                                                           error) &&
                 d.effects.size() == 32;
        }
        {
            legend::world::StatusEffectSnapshotPayload p;
            p.effects.resize(33);
            std::vector<std::uint8_t> payload;
            legend::world::StatusEffectSnapshotPayload d;
            ok = ok && !legend::world::EncodeStatusEffectSnapshot(p, payload) && payload.empty();
            std::vector<std::uint8_t> bad;
            legend::network::ByteWriter w(bad);
            w.WriteUInt8(kTypePlayer);
            w.WriteUInt64(1);
            w.WriteUInt64(0);
            w.WriteUInt16(33);
            w.WriteUInt64(1);
            w.WriteUInt32(2001);
            w.WriteUInt8(1);
            w.WriteUInt32(1000);
            ok = ok && !legend::world::DecodeStatusEffectSnapshot(bad.data(), bad.size(), d, error);
        }
        Check("StatusSnapshotCountLimitCheck: 32 accepted, 33 encode/decode rejected", ok);
    }

    // ---- StatusContainerStrategyCheck（指令四十四~四十八/七十二：stack/refresh 策略）----
    {
        StatusEffectRegistry registry;
        StatusEffectContainer container;
        const auto now = StatusClock::now();
        bool ok = true;
        // Battle Focus：Applied -> Refreshed（stacks 仍 1、expire 刷新、instanceId 保留）
        StatusApplyContext ctx;
        ctx.effectId = kStatusEffectIdBattleFocus;
        ctx.sourceType = kTypePlayer;
        ctx.sourceEntityId = 77;
        ctx.targetType = kTypePlayer;
        ctx.targetEntityId = 1001;
        auto o1 = ApplyEffect(container, registry, ctx, 1, now);
        ok = ok && o1.result == StatusApplyResult::Applied && o1.effect->Stacks() == 1;
        const auto expire1 = o1.effect->ExpireTime();
        const auto instance1 = o1.effect->InstanceId();
        auto o2 = ApplyEffect(container, registry, ctx, 2, now + std::chrono::seconds(5));
        ok = ok && o2.result == StatusApplyResult::Refreshed && o2.effect->Stacks() == 1 &&
             o2.effect->InstanceId() == instance1 && o2.effect->ExpireTime() > expire1;
        // Armor Break：1 -> 2 -> 3 -> AtMax（duration 刷新、stacks 不变、nextTick 概念无）
        StatusApplyContext abCtx;
        abCtx.effectId = kStatusEffectIdArmorBreak;
        abCtx.sourceType = kTypePlayer;
        abCtx.sourceEntityId = 77;
        abCtx.targetType = kTypeMonster;
        abCtx.targetEntityId = 9;
        auto a1 = ApplyEffect(container, registry, abCtx, 3, now);
        ok = ok && a1.result == StatusApplyResult::Applied && a1.effect->Stacks() == 1;
        const auto abExpire1 = a1.effect->ExpireTime();
        auto a2 = ApplyEffect(container, registry, abCtx, 4, now + std::chrono::seconds(1));
        ok = ok && a2.result == StatusApplyResult::StackAdded && a2.effect->Stacks() == 2;
        auto a3 = ApplyEffect(container, registry, abCtx, 5, now + std::chrono::seconds(2));
        ok = ok && a3.result == StatusApplyResult::StackAdded && a3.effect->Stacks() == 3;
        auto a4 = ApplyEffect(container, registry, abCtx, 6, now + std::chrono::seconds(3));
        ok = ok && a4.result == StatusApplyResult::AtMaxStacksRefreshed &&
             a4.effect->Stacks() == 3 && a4.effect->ExpireTime() > abExpire1;
        ok = ok && container.Count() == 2; // BF + AB 并存（key=effectId）
        // Burn：Refreshed 且 nextTick 重置（指令四十六）
        StatusApplyContext burnCtx;
        burnCtx.effectId = kStatusEffectIdBurn;
        burnCtx.sourceType = kTypePlayer;
        burnCtx.sourceEntityId = 77;
        burnCtx.targetType = kTypeMonster;
        burnCtx.targetEntityId = 9;
        auto b1 = ApplyEffect(container, registry, burnCtx, 7, now);
        const auto burnTick1 = b1.effect->NextTickTime();
        auto b2 = ApplyEffect(container, registry, burnCtx, 8, now + std::chrono::seconds(1));
        ok = ok && b1.result == StatusApplyResult::Applied &&
             b2.result == StatusApplyResult::Refreshed && b2.effect->Stacks() == 1 &&
             b2.effect->NextTickTime() > burnTick1; // 重置为 now+2s
        // Poison：StackAdded 且 nextTick 不重置（指令四十七/一百零一）
        StatusApplyContext poisonCtx;
        poisonCtx.effectId = kStatusEffectIdPoison;
        poisonCtx.sourceType = kTypePlayer;
        poisonCtx.sourceEntityId = 77;
        poisonCtx.targetType = kTypeMonster;
        poisonCtx.targetEntityId = 9;
        auto p1 = ApplyEffect(container, registry, poisonCtx, 9, now);
        const auto poisonTick1 = p1.effect->NextTickTime();
        auto p2 = ApplyEffect(container, registry, poisonCtx, 10, now + std::chrono::seconds(1));
        ok = ok && p1.result == StatusApplyResult::Applied &&
             p2.result == StatusApplyResult::StackAdded && p2.effect->Stacks() == 2 &&
             p2.effect->NextTickTime() == poisonTick1; // 不重置
        auto p3 = ApplyEffect(container, registry, poisonCtx, 11, now + std::chrono::seconds(1));
        auto p4 = ApplyEffect(container, registry, poisonCtx, 12, now + std::chrono::seconds(1));
        ok = ok && p3.result == StatusApplyResult::StackAdded && p3.effect->Stacks() == 3 &&
             p4.result == StatusApplyResult::AtMaxStacksRefreshed && p4.effect->Stacks() == 3;
        // Slow：Refreshed，stacks 仍 1（指令四十八）
        StatusApplyContext slowCtx;
        slowCtx.effectId = kStatusEffectIdSlow;
        slowCtx.sourceType = kTypePlayer;
        slowCtx.sourceEntityId = 77;
        slowCtx.targetType = kTypeMonster;
        slowCtx.targetEntityId = 9;
        auto s1 = ApplyEffect(container, registry, slowCtx, 13, now);
        auto s2 = ApplyEffect(container, registry, slowCtx, 14, now + std::chrono::seconds(1));
        ok = ok && s1.result == StatusApplyResult::Applied &&
             s2.result == StatusApplyResult::Refreshed && s2.effect->Stacks() == 1;
        // 多来源覆盖（指令七十二）：最新 source 覆盖
        ok = ok && s2.effect->SourceEntityId() == 77;
        StatusApplyContext otherSource = slowCtx;
        otherSource.sourceEntityId = 88;
        auto s3 = ApplyEffect(container, registry, otherSource, 15, now + std::chrono::seconds(1));
        ok = ok && s3.effect->SourceEntityId() == 88;
        // ExpireEffects（指令七十八）：到期移除，未到期保留
        auto expired = ExpireEffects(container, now + std::chrono::seconds(60));
        ok = ok && expired.size() == 5 && container.Empty();
        Check("StatusContainerStrategyCheck: refresh/stack/max/burn-reset/poison-keep/"
              "source-override/expire",
              ok);
    }

    // ---- StatusOnDeadTargetCheck（指令一百一十二：死亡/非法/未知不进容器）----
    {
        StatusEffectRegistry registry;
        StatusEffectContainer container;
        const auto now = StatusClock::now();
        StatusApplyContext ctx;
        ctx.effectId = kStatusEffectIdBurn;
        ctx.targetType = kTypeMonster;
        ctx.targetEntityId = 9;
        ctx.targetAlive = false;
        auto dead = ApplyEffect(container, registry, ctx, 1, now);
        bool ok = dead.result == StatusApplyResult::TargetDead && container.Empty();
        ctx.targetAlive = true;
        ctx.targetValid = false;
        auto invalid = ApplyEffect(container, registry, ctx, 2, now);
        ok = ok && invalid.result == StatusApplyResult::InvalidTarget && container.Empty();
        ctx.targetValid = true;
        ctx.effectId = 9999;
        auto unknown = ApplyEffect(container, registry, ctx, 3, now);
        ok = ok && unknown.result == StatusApplyResult::UnknownEffect && container.Empty();
        Check("StatusOnDeadTargetCheck: dead/invalid/unknown effect rejected, container empty",
              ok);
    }

    // ---- DerivedStatsCheck（指令二十六~二十八/一百零七/一百零八/一百零九）----
    {
        StatusEffectRegistry registry;
        StatusEffectContainer container;
        const auto now = StatusClock::now();
        bool ok = true;
        // Battle Focus：20 + 10 = 30
        StatusApplyContext bf;
        bf.effectId = kStatusEffectIdBattleFocus;
        bf.sourceType = kTypePlayer;
        bf.targetType = kTypePlayer;
        bf.targetEntityId = 1;
        ApplyEffect(container, registry, bf, 1, now);
        auto stats = RecalculateDerivedStats(20, 5, 120.0f, container, registry);
        ok = ok && stats.attackPower == 30 && stats.defense == 5 && stats.moveSpeed == 120.0f;
        // Armor Break 3 层 on base 2：max(0, 2-6) = 0（不能负，指令一百零八）
        StatusEffectContainer container2;
        StatusApplyContext ab;
        ab.effectId = kStatusEffectIdArmorBreak;
        ab.sourceType = kTypePlayer;
        ab.targetType = kTypeMonster;
        ab.targetEntityId = 9;
        ApplyEffect(container2, registry, ab, 2, now);
        ApplyEffect(container2, registry, ab, 3, now);
        ApplyEffect(container2, registry, ab, 4, now);
        stats = RecalculateDerivedStats(10, 2, 80.0f, container2, registry);
        ok = ok && stats.defense == 0 && stats.attackPower == 10;
        // Slow：80 x 0.6 = 48（DerivedMoveSpeedCheck）
        ApplyEffect(container2, registry, ab, 5, now); // 第 4 次 -> AtMax 仍 3 层
        StatusApplyContext slow;
        slow.effectId = kStatusEffectIdSlow;
        slow.sourceType = kTypePlayer;
        slow.targetType = kTypeMonster;
        slow.targetEntityId = 9;
        ApplyEffect(container2, registry, slow, 6, now);
        stats = RecalculateDerivedStats(10, 2, 80.0f, container2, registry);
        ok = ok && std::fabs(stats.moveSpeed - 48.0f) < 0.001f;
        // DOT 伤害：Burn 1 层 8；Poison 3 层 12
        const auto* burnDef = registry.FindEffect(kStatusEffectIdBurn);
        const auto* poisonDef = registry.FindEffect(kStatusEffectIdPoison);
        ok = ok && ComputeDotDamage(*burnDef, 1) == 8 && ComputeDotDamage(*poisonDef, 3) == 12;
        Check("DerivedStatsCheck: attack 30 / defense clamp 0 / moveSpeed 48 / dot per stack", ok);
    }

    // ---- StatusTickCatchupCheck（指令四十二/四十三/一百三十六）----
    {
        StatusEffectRegistry registry;
        const auto* burn = registry.FindEffect(kStatusEffectIdBurn);
        bool ok = burn != nullptr;
        const auto now = StatusClock::now();
        ActiveStatusEffect effect(1, kStatusEffectIdBurn, kTypePlayer, 77, 0, kTypeMonster, 9, 1,
                                  now, now + std::chrono::seconds(8), now + std::chrono::seconds(2));
        ok = ok && TickEffect(effect, *burn, now) == 0; // 未到 nextTick
        ok = ok && TickEffect(effect, *burn, now + std::chrono::seconds(2)) == 1; // 第 1 跳
        ok = ok && effect.NextTickTime() == now + std::chrono::seconds(4);
        // 卡顿补跳：一次最多 3（指令一百三十六）
        const auto now2 = StatusClock::now();
        ActiveStatusEffect lagged(2, kStatusEffectIdBurn, kTypePlayer, 77, 0, kTypeMonster, 9, 1,
                                  now2, now2 + std::chrono::seconds(8),
                                  now2 - std::chrono::seconds(10)); // nextTick 远在过去
        const std::uint32_t ticks = TickEffect(lagged, *burn, now2);
        ok = ok && ticks == legend::world::kStatusDotMaxCatchUpTicks; // 最多 3
        ok = ok && lagged.NextTickTime() > now2;                      // 推进到未来
        // nextTick > expire：过期窗口不产生跳（指令四十二）
        const auto now3 = StatusClock::now();
        ActiveStatusEffect lateTick(3, kStatusEffectIdBurn, kTypePlayer, 77, 0, kTypeMonster, 9, 1,
                                    now3, now3 + std::chrono::seconds(1),
                                    now3 + std::chrono::seconds(2));
        ok = ok && TickEffect(lateTick, *burn, now3 + std::chrono::seconds(3)) == 0;
        ok = ok && legend::world::kStatusDotMaxCatchUpTicks == 3;
        Check("StatusTickCatchupCheck: due-tick rules + max 3 catch-up + future push", ok);
    }

    // ---- 客户端 Remote 容器（指令六十一/一百二十三~一百二十六）----
    {
        using legend::client::RemoteStatusEffect;
        using legend::client::RemoteStatusEffectContainer;
        RemoteStatusEffectContainer clientContainer;
        RemoteStatusEffect a;
        a.instanceId = 1;
        a.effectId = 2001;
        a.stacks = 1;
        a.remainingMs = 10000;
        a.durationMs = 10000;
        a.sourceEntityId = 77;
        clientContainer.Apply(a);
        clientContainer.Apply(a); // 重复 Applied 同 instanceId -> 仍 1 个（指令一百二十三）
        bool ok = clientContainer.Count() == 1;
        clientContainer.Update(999, 3, 5000); // 未知 instanceId Updated -> 忽略（指令一百二十四）
        ok = ok && clientContainer.Count() == 1 && clientContainer.Find(1) != nullptr;
        clientContainer.Remove(999); // 未知 Removed -> 忽略不 Crash（指令一百二十五）
        ok = ok && clientContainer.Count() == 1;
        // Snapshot 以服务器列表为准（指令一百二十六）：多余删除、缺少创建
        RemoteStatusEffect b;
        b.instanceId = 2;
        b.effectId = 2002;
        b.stacks = 2;
        b.remainingMs = 7000;
        std::vector<RemoteStatusEffect> serverList;
        serverList.push_back(b);
        clientContainer.SnapshotReplace(serverList);
        ok = ok && clientContainer.Count() == 1 && clientContainer.Find(1) == nullptr &&
             clientContainer.Find(2) != nullptr && clientContainer.Find(2)->stacks == 2;
        // 模拟客户端状态被改坏（丢包场景）-> 下一次 Snapshot 恢复（指令一百二十二）
        RemoteStatusEffect corrupt;
        corrupt.instanceId = 2;
        corrupt.effectId = 2002;
        corrupt.stacks = 99;
        corrupt.remainingMs = 1;
        std::vector<RemoteStatusEffect> bad;
        bad.push_back(corrupt);
        bad.push_back(b);
        clientContainer.SnapshotReplace(bad);
        ok = ok && clientContainer.Count() == 1 && clientContainer.Find(2)->stacks == 2;
        clientContainer.Update(2, 3, 6000);
        ok = ok && clientContainer.Find(2)->stacks == 3;
        Check("ClientRemoteStatusContainerCheck: dedupe/unknown-ignore/snapshot-replace/correction",
              ok);
    }
}

// ===========================================================================
// B. 真实链路检查
// ===========================================================================

void RunStatusChainChecks() {
    WorldTestServers servers;
    servers.dbPath = TempDbPath("world_status_chain");
    RemoveDb(servers.dbPath);
    Check("StatusServersStartCheck", servers.StartLogin() && servers.StartWorld());

    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(8, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("StatusChecks: db ready", false);
        servers.StopAll();
        return;
    }

    // ---- 布局：A(100,1000) 主角 / B(130,1030) 观察者 / C(60,60) 远处 /
    //      W(1000,1000) Whirlwind+断线 DOT / D(560,1560) 死亡场景 ----
    // 怪分配：slime3 -> (160,1040) QS 目标 / slime8 -> (250,1030) FB 目标（BurnKill）/
    // slime10 -> (170,1040) 普攻+CS 目标 / slime5 -> (1060,1030) W 的 WW 目标 /
    // slime6 -> (600,1900) 暂存（观察者复制阶段移到 B 旁）/
    // slime7 -> (1900,1900) Poison 3 层满血 expire / slime11 -> (600,1400) Slow Patrol 测速 /
    // slime9 原位 (1500,500) Burn 4 跳 / slime12 原位 (1700,500) AB expire /
    // slime13~16 D 的死亡场景 / slime17 -> (700,950) 断线 DOT。
    CharacterSeed seedA;
    CharacterSeed seedB;
    CharacterSeed seedC;
    CharacterSeed seedW;
    CharacterSeed seedD;
    bool seeded = true;
    seeded = seeded && SeedAt(db, accounts, characters, "st_user_a", "StA", 100.0f, 1000.0f, seedA);
    seeded = seeded && SeedAt(db, accounts, characters, "st_user_b", "StB", 130.0f, 1030.0f, seedB);
    seeded = seeded && SeedAt(db, accounts, characters, "st_user_c", "StC", 60.0f, 60.0f, seedC);
    seeded = seeded && SeedAt(db, accounts, characters, "st_user_w", "StW", 1000.0f, 1000.0f, seedW);
    seeded = seeded && SeedAt(db, accounts, characters, "st_user_d", "StD", 560.0f, 1560.0f, seedD);
    Check("StatusChecks: seeds ready", seeded);

    servers.world->MoveMonsterTo(3, 160.0f, 1040.0f);
    servers.world->MoveMonsterTo(8, 250.0f, 1030.0f);
    servers.world->MoveMonsterTo(10, 170.0f, 1040.0f);
    servers.world->MoveMonsterTo(5, 1060.0f, 1030.0f);
    servers.world->MoveMonsterTo(6, 600.0f, 1900.0f);
    servers.world->MoveMonsterTo(7, 1900.0f, 1900.0f);
    servers.world->MoveMonsterTo(11, 600.0f, 1400.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Post 队列排空

    WorldTestClient clientA;
    WorldTestClient clientB;
    WorldTestClient clientC;
    WorldTestClient clientW;
    {
        const bool eA = clientA.ConnectAndEnter(TicketFor(servers.login, seedA), 8000);
        const bool eB = clientB.ConnectAndEnter(TicketFor(servers.login, seedB), 8000);
        const bool eC = clientC.ConnectAndEnter(TicketFor(servers.login, seedC), 8000);
        const bool eW = clientW.ConnectAndEnter(TicketFor(servers.login, seedW), 8000);
        Check("StatusChecks: A/B/C/W entered", eA && eB && eC && eW);
    }
    // D 延迟进入（其场景为被围殴致死，检查在尾部用事件缓存验证）。
    WorldTestClient clientD;

    // ---- BattleFocusApplyCheck（指令八十六/一百零七）----
    // ---- BattleFocusDamageCheck + SkillUsesDerivedCheck（指令八十七/一百一十一）----
    // ---- QS 命中自动施加 Armor Break（指令十八）----
    {
        const bool visible = WaitMonsterVisible(servers, seedA.characterId, 3);
        clientA.controller.SendSkillCast(kSkillIdBattleFocus, kTargetSelf, 0);
        WorldNetworkEvent applied;
        const bool gotApplied = WaitStatusEvent(
            clientA, WorldNetworkEvent::Type::StatusAppliedEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.effectId == 2001 && e.status.targetType == kTypePlayer &&
                       e.status.targetEntityId == seedA.characterId &&
                       e.status.sourceSkillId == kSkillIdBattleFocus;
            },
            applied);
        bool ok = visible && gotApplied && applied.status.stacks == 1 &&
                  applied.status.durationMs == 10000;
        auto player = ReadPlayer(servers, seedA.characterId);
        ok = ok && player.found && player.attack == 30 && player.mana == 85; // 100-15
        ok = ok && clientA.controller.LocalStatusEffects().Count() == 1;
        Check("BattleFocusApplyCheck: 1004 self-cast -> Applied(2001,1), attack 20->30, mana 85",
              ok);

        // QuickStrike 带 BF：30+30-2 = 58（指令三十二/八十七）
        clientA.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonsterSkill, 3);
        WorldNetworkEvent impact;
        const bool gotImpact = WaitStatusEvent(
            clientA, WorldNetworkEvent::Type::SkillImpact,
            [&](const WorldNetworkEvent& e) {
                return e.skillId == kSkillIdQuickStrike && !e.impactTargets.empty() &&
                       e.impactTargets[0].entityId == 3;
            },
            impact, 4000);
        ok = gotImpact && impact.impactTargets[0].damage == 58 &&
             impact.impactTargets[0].hpAfter == 22;
        WorldNetworkEvent abApplied;
        const bool gotAb = WaitStatusEvent(
            clientA, WorldNetworkEvent::Type::StatusAppliedEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.effectId == 2002 && e.status.targetEntityId == 3 &&
                       e.status.sourceSkillId == kSkillIdQuickStrike;
            },
            abApplied);
        ok = ok && gotAb && abApplied.status.stacks == 1 &&
             abApplied.status.sourceEntityId == seedA.characterId;
        const auto monster3 = ReadMonster(servers, 3, kStatusEffectIdArmorBreak);
        ok = ok && monster3.statusCount == 1 && monster3.probeStacks == 1 &&
             monster3.defense == 0; // 2 - 2 = 0
        Check("BattleFocusDamageCheck: QS 58 dmg with BF + ArmorBreak auto-applied (defense 2->0)",
              ok);
    }

    // ---- ArmorBreakStackCheck（指令九十~九十二）：白盒 +1 -> 2 -> 3 -> AtMax ----
    {
        const auto s2 = ApplyWhitebox(servers, kTypeMonster, 3, kStatusEffectIdArmorBreak,
                                      seedA.characterId);
        bool ok = s2.result == StatusApplyResult::StackAdded;
        WorldNetworkEvent updated;
        const bool got2 = WaitStatusEvent(
            clientA, WorldNetworkEvent::Type::StatusUpdatedEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.effectId == 2002 && e.status.targetEntityId == 3 &&
                       e.status.stacks == 2;
            },
            updated);
        const auto s3 = ApplyWhitebox(servers, kTypeMonster, 3, kStatusEffectIdArmorBreak,
                                      seedA.characterId);
        const auto s4 = ApplyWhitebox(servers, kTypeMonster, 3, kStatusEffectIdArmorBreak,
                                      seedA.characterId);
        WorldNetworkEvent maxUpdated;
        const bool gotMax = WaitStatusEvent(
            clientA, WorldNetworkEvent::Type::StatusUpdatedEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.effectId == 2002 && e.status.targetEntityId == 3 &&
                       e.status.stacks == 3 && e.status.remainingMs > 7800; // duration 刷新
            },
            maxUpdated, 3000);
        ok = ok && got2 && s3.result == StatusApplyResult::StackAdded &&
             s4.result == StatusApplyResult::AtMaxStacksRefreshed && gotMax;
        if (!ok) {
            std::printf("[Diag] ABStack: s2=%u s3=%u s4=%u got2=%d gotMax=%d\n",
                        static_cast<unsigned>(s2.result), static_cast<unsigned>(s3.result),
                        static_cast<unsigned>(s4.result), static_cast<int>(got2),
                        static_cast<int>(gotMax));
        }
        const auto monster3 = ReadMonster(servers, 3);
        ok = ok && monster3.statusCount == 1 && monster3.defense == 0; // 满 3 层仍 clamp 0
        // 指令一百二十（Refresh 复制）由 slime6 的 Observer 块断言——此处 B 早期
        // AOI 滞后（VisibleMonsters 未含 slime3）收不到白盒 Updated，属预期。
        Check("ArmorBreakStackCheck: 1->2->3->AtMax(3, refresh) + clamp 0", ok);
    }

    // ---- BattleFocusRefreshCheck（指令八十八）：BF 过期前白盒刷新 -> Updated ~10s ----
    {
        const auto refresh = ApplyWhitebox(servers, kTypePlayer, seedA.characterId,
                                           kStatusEffectIdBattleFocus, seedA.characterId);
        bool ok = refresh.result == StatusApplyResult::Refreshed;
        WorldNetworkEvent updated;
        const bool got = WaitStatusEvent(
            clientA, WorldNetworkEvent::Type::StatusUpdatedEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.effectId == 2001 && e.status.targetEntityId == seedA.characterId &&
                       e.status.stacks == 1 && e.status.remainingMs > 9000;
            },
            updated);
        ok = ok && got;
        // 指令一百二十（Refresh 复制 A/B 都收 Updated）由 slime6 的 Observer 块覆盖
        //（B 对 A 的 VisiblePlayers 亦受 AOI tick 滞后影响，此处不断言 B）。
        Check("BattleFocusRefreshCheck: stacks stay 1, remaining back to ~10s", ok);
    }

    // ---- BasicAttackUsesDerivedCheck（指令一百一十）：BF 中普攻 30-2=28 ----
    {
        const bool visible = WaitMonsterVisible(servers, seedA.characterId, 10);
        clientA.client().SendAttack(8801, kTypeMonster, 10);
        WorldNetworkEvent combat;
        int seen = 0;
        bool ok = visible;
        const bool got = WaitUntil(
            [&] {
                clientA.DrainEvents();
                const auto& events =
                    clientA
                        .recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::CombatEvent)];
                for (int i = seen; i < static_cast<int>(events.size()); ++i) {
                    const auto& e = events[static_cast<std::size_t>(i)];
                    if (e.sourceType == kSourceBasic && e.attackerId == seedA.characterId &&
                        e.targetId == 10) {
                        combat = e;
                        seen = i + 1;
                        return true;
                    }
                }
                seen = static_cast<int>(events.size());
                return false;
            },
            4000);
        ok = ok && got && combat.damage == 28; // 30(BF) - 2 = 28（非 20-2=18）
        Check("BasicAttackUsesDerivedCheck: basic attack uses effective attack (28 = 30-2)", ok);
    }

    // ---- 白盒批量布状态（后续等待窗口的时间基线）----
    // slime9：Burn 1 层（4 跳 2/4/6/8s，满血 80 -> 48）
    // slime7：Poison 3 层（12/s，6 跳 80 -> 8 撑到 expire）
    // slime11：Slow（Patrol 测速）
    // slime12：Armor Break 3 层（8s expire -> defense 2 恢复）
    {
        ApplyWhitebox(servers, kTypeMonster, 9, kStatusEffectIdBurn, seedA.characterId);
        const auto p1 = ApplyWhitebox(servers, kTypeMonster, 7, kStatusEffectIdPoison,
                                      seedA.characterId);
        bool ok = p1.result == StatusApplyResult::Applied;
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        const auto p2 = ApplyWhitebox(servers, kTypeMonster, 7, kStatusEffectIdPoison,
                                      seedA.characterId);
        const auto p3 = ApplyWhitebox(servers, kTypeMonster, 7, kStatusEffectIdPoison,
                                      seedA.characterId);
        ok = ok && p2.result == StatusApplyResult::StackAdded &&
             p3.result == StatusApplyResult::StackAdded;
        // AddStack 不重置 nextTick（指令一百零一）：首层 nextTick ≈ +1000ms，400ms 流逝后
        // 若被重置应 >1000ms——实测必须 <=700ms
        const auto tick3 = ReadMonster(servers, 7, kStatusEffectIdPoison);
        ok = ok && tick3.probeStacks == 3 && tick3.probeNextTickMs >= 0 &&
             tick3.probeNextTickMs <= 700;
        const auto slow1 =
            ApplyWhitebox(servers, kTypeMonster, 11, kStatusEffectIdSlow, seedA.characterId);
        ok = ok && slow1.result == StatusApplyResult::Applied;
        for (int i = 0; i < 3; ++i) {
            ApplyWhitebox(servers, kTypeMonster, 12, kStatusEffectIdArmorBreak, seedA.characterId);
        }
        const auto slime12 = ReadMonster(servers, 12);
        ok = ok && slime12.defense == 0 && slime12.statusCount == 1;
        Check("StatusWhiteboxBatchCheck: burn/poison(3 stacks, nextTick kept)/slow/armorbreak",
              ok);
    }

    // ---- SlowAiMovementCheck（指令三十/七十四/一百零三白盒部分/一百零四）----
    {
        const auto slime11 = ReadMonster(servers, 11);
        bool ok = slime11.found && std::fabs(slime11.moveSpeed - 48.0f) < 0.001f; // 80 -> 48
        const bool patrolling = WaitUntil(
            [&] {
                auto m = servers.world->FindMonster(11);
                return m && m->State() == MonsterState::Patrol;
            },
            8000);
        float moved = 0.0f;
        if (patrolling) {
            const auto start = RunOnWorldIo(servers.worldService, [&] {
                auto m = servers.world->FindMonster(11);
                return std::pair<float, float>(m ? m->PositionX() : 0.0f,
                                               m ? m->PositionY() : 0.0f);
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            const auto end = RunOnWorldIo(servers.worldService, [&] {
                auto m = servers.world->FindMonster(11);
                return std::pair<float, float>(m ? m->PositionX() : 0.0f,
                                               m ? m->PositionY() : 0.0f);
            });
            moved = std::sqrt((end.first - start.first) * (end.first - start.first) +
                              (end.second - start.second) * (end.second - start.second));
        }
        ok = ok && patrolling && moved > 15.0f && moved < 70.0f; // slow ~48/s，normal 80 排除
        Check("SlowAiMovementCheck: effective moveSpeed 48, patrol step clearly slower than 80",
              ok);
    }

    // ---- BurnApplyCheck 真实链路（指令九十四/一百三十）：A 的 FireBolt -> slime8 ----
    {
        const bool visible = WaitMonsterVisible(servers, seedA.characterId, 8);
        clientA.controller.SendSkillCast(kSkillIdFireBolt, kTargetMonsterSkill, 8);
        WorldNetworkEvent applied;
        const bool gotApplied = WaitStatusEvent(
            clientA, WorldNetworkEvent::Type::StatusAppliedEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.effectId == 2003 && e.status.targetEntityId == 8 &&
                       e.status.sourceSkillId == kSkillIdFireBolt;
            },
            applied, 6000); // FireBolt 1s 读条
        bool ok = visible && gotApplied && applied.status.stacks == 1;
        WorldNetworkEvent dot;
        int seen = 0;
        const bool gotDot = WaitDotCombatEvent(clientA, 2003, 8, seen, dot, 4000);
        ok = ok && gotDot && dot.damage == 8 && dot.attackerId == seedA.characterId &&
             dot.sourceId == 2003 && dot.sourceType == kSourceStatus;
        const auto monster8 = ReadMonster(servers, 8);
        // A 带 BF：FB 伤害 = 40+30-2 = 68（80->12）；DOT tick 后更低。断言 Burn 存在 + 掉血。
        ok = ok && monster8.found && monster8.alive && monster8.hp < 12 &&
             monster8.statusCount == 1;
        Check("BurnApplyCheck: FireBolt applies Burn, DOT tick -8 (CombatEvent StatusEffect/2003)",
              ok);
    }

    // ---- PoisonApplyCheck 真实链路（指令九十八首层）：W 的 Whirlwind -> slime5 ----
    {
        const bool positioned = WaitUntil(
            [&] {
                auto player = servers.world->FindPlayerByCharacter(seedW.characterId);
                auto monster = servers.world->FindMonster(5);
                if (!player || !monster) {
                    return false;
                }
                const float dx = player->PositionX() - monster->PositionX();
                const float dy = player->PositionY() - monster->PositionY();
                return player->VisibleMonsters().count(5) != 0 &&
                       (dx * dx + dy * dy) <= 160.0f * 160.0f;
            },
            6000);
        clientW.controller.SendSkillCast(kSkillIdWhirlwind, kTargetSelf, 0);
        WorldNetworkEvent applied;
        const bool gotApplied = WaitStatusEvent(
            clientW, WorldNetworkEvent::Type::StatusAppliedEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.effectId == 2004 && e.status.targetEntityId == 5 &&
                       e.status.sourceSkillId == kSkillIdWhirlwind;
            },
            applied, 4000);
        bool ok = positioned && gotApplied && applied.status.stacks == 1;
        const auto monster5 = ReadMonster(servers, 5);
        ok = ok && monster5.hp == 37 && monster5.statusCount == 1; // 80 - 43(WW)
        WorldNetworkEvent dot;
        int seen = 0;
        const bool gotDot = WaitDotCombatEvent(clientW, 2004, 5, seen, dot, 4000);
        ok = ok && gotDot && dot.damage == 4 && dot.sourceId == 2004; // 首层 4
        Check("PoisonApplyCheck: Whirlwind applies Poison(1) to slime5, DOT tick 4 dmg", ok);
    }

    // ---- SlowApplyCheck 真实链路（指令一百零三）：A 的 CripplingStrike -> slime10 ----
    {
        clientA.controller.SendSkillCast(kSkillIdCripplingStrike, kTargetMonsterSkill, 10);
        WorldNetworkEvent applied;
        const bool gotApplied = WaitStatusEvent(
            clientA, WorldNetworkEvent::Type::StatusAppliedEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.effectId == 2005 && e.status.targetEntityId == 10 &&
                       e.status.sourceSkillId == kSkillIdCripplingStrike;
            },
            applied, 4000);
        bool ok = gotApplied && applied.status.stacks == 1;
        const auto monster10 = ReadMonster(servers, 10);
        // slime10 已被普攻 28（80->52）：CS 38 -> 14；Slow 80->48
        ok = ok && monster10.hp == 14 && std::fabs(monster10.moveSpeed - 48.0f) < 0.001f;
        Check("SlowApplyCheck: CripplingStrike slows slime10 (80->48), 38 dmg", ok);
    }

    // ---- 玩家死亡场景：D 进场 + BF（死亡清状态，指令一百一十五）----
    {
        const bool entered = clientD.ConnectAndEnter(TicketFor(servers.login, seedD), 8000);
        clientD.controller.SendSkillCast(kSkillIdBattleFocus, kTargetSelf, 0);
        WorldNetworkEvent applied;
        const bool gotApplied = WaitStatusEvent(
            clientD, WorldNetworkEvent::Type::StatusAppliedEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.effectId == 2001 && e.status.targetEntityId == seedD.characterId;
            },
            applied, 5000);
        Check("StatusChecks: D entered + Battle Focus applied", entered && gotApplied);
    }

    // ---- SkillKillNoStatusApply + StatusClearOnMonsterDeath（指令一百一十三/一百一十四）----
    // slime3（HP 22，带 AB 满 3 层）白盒补 Burn/Poison/Slow 后 QS#2 直接杀死：
    // 4 个 Removed(TargetDied) + 容器空 + 死亡不施加新 AB。
    {
        ApplyWhitebox(servers, kTypeMonster, 3, kStatusEffectIdBurn, seedA.characterId);
        ApplyWhitebox(servers, kTypeMonster, 3, kStatusEffectIdPoison, seedA.characterId);
        ApplyWhitebox(servers, kTypeMonster, 3, kStatusEffectIdSlow, seedA.characterId);
        const auto before = ReadMonster(servers, 3);
        Check("StatusChecks: slime3 carries 4 statuses before death", before.statusCount == 4);

        clientA.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonsterSkill, 3);
        int removedAll = 0;
        int abApplied = 0;
        // 等待：服务器死亡 + 4 条 Removed(TargetDied) 全部经网络到达 A。
        const bool died = WaitUntil(
            [&] {
                clientA.DrainEvents();
                auto m = servers.world->FindMonster(3);
                const bool dead = !m || !m->Alive();
                abApplied =
                    CountStatusEventsFor(clientA, WorldNetworkEvent::Type::StatusAppliedEvent,
                                         2002, kTypeMonster, 3);
                removedAll = 0;
                for (const auto effectId : {2002u, 2003u, 2004u, 2005u}) {
                    removedAll += CountStatusEventsFor(
                        clientA, WorldNetworkEvent::Type::StatusRemovedEvent, effectId,
                        kTypeMonster, 3, static_cast<int>(StatusRemovedReason::TargetDied));
                }
                return dead && removedAll == 4;
            },
            6000);
        bool ok = died && abApplied == 1; // 只有 QS#1 的那次（QS#2 杀死不再施加）
        const auto after = ReadMonster(servers, 3);
        ok = ok && after.statusCount == 0;
        if (!ok) {
            std::printf("[Diag] KillNoStatus: died=%d abApplied=%d removedAll=%d afterCount=%zu "
                        "found=%d\n",
                        static_cast<int>(died), abApplied, removedAll, after.statusCount,
                        static_cast<int>(after.found));
        }
        Check("SkillKillNoStatusApplyCheck: killing blow applies no status; "
              "StatusClearOnMonsterDeathCheck: 4x Removed(TargetDied), container empty",
              ok);

        // QS#3 清掉 slime10（解除 A 的第三只围殴怪；带 Slow 死亡同验证死亡清理）
        std::this_thread::sleep_for(std::chrono::milliseconds(1600)); // QS CD 1.5s
        clientA.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonsterSkill, 10);
        const bool slime10Dead = WaitUntil(
            [&] {
                auto m = servers.world->FindMonster(10);
                return m && !m->Alive();
            },
            6000);
        Check("StatusChecks: slime10 killed to clear aggro", slime10Dead);
    }

    // ---- BurnRefreshCheck（指令九十六）：白盒重施 Burn -> nextTick 重置 ----
    // 用独立怪 slime18（不干扰 slime9 的 4 跳验证）。
    {
        ApplyWhitebox(servers, kTypeMonster, 18, kStatusEffectIdBurn, seedA.characterId);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const auto refresh =
            ApplyWhitebox(servers, kTypeMonster, 18, kStatusEffectIdBurn, seedA.characterId);
        const auto after = ReadMonster(servers, 18, kStatusEffectIdBurn);
        bool ok = refresh.result == StatusApplyResult::Refreshed &&
                  after.probeNextTickMs > 1500; // 重置为 now+2s（>1.5s 证明非旧节奏）
        ok = ok && after.statusCount == 1 && after.probeStacks == 1;
        Check("BurnRefreshCheck: re-apply refreshes duration + nextTick reset to now+2s", ok);
    }

    // ---- ObserverReplication + NoGlobal（指令一百一十八/一百一十九）----
    // slime6 移到 B 旁；A 白盒施 Burn -> A/B 都收到 Applied；C 0 状态事件。
    {
        servers.world->MoveMonsterTo(6, 220.0f, 970.0f); // A 距 120 / B 距 91（B 更近 -> 追 B）
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const bool bSees = WaitMonsterVisible(servers, seedB.characterId, 6);
        const auto applied =
            ApplyWhitebox(servers, kTypeMonster, 6, kStatusEffectIdBurn, seedA.characterId);
        bool ok = applied.result == StatusApplyResult::Applied && bSees;
        WorldNetworkEvent aEvent;
        const bool aGot = WaitStatusEvent(
            clientA, WorldNetworkEvent::Type::StatusAppliedEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.effectId == 2003 && e.status.targetEntityId == 6;
            },
            aEvent);
        WorldNetworkEvent bEvent;
        const bool bGot = WaitStatusEvent(
            clientB, WorldNetworkEvent::Type::StatusAppliedEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.effectId == 2003 && e.status.targetEntityId == 6;
            },
            bEvent);
        ok = ok && aGot && bGot && bEvent.status.sourceEntityId == seedA.characterId;
        const int cStatus =
            static_cast<int>(clientC.recorded[WorldTestClient::IndexOf(
                WorldNetworkEvent::Type::StatusAppliedEvent)]
                                 .size()) +
            static_cast<int>(clientC.recorded[WorldTestClient::IndexOf(
                WorldNetworkEvent::Type::StatusUpdatedEvent)]
                                 .size());
        Check("StatusObserverReplicationCheck: A+B both receive Applied; C receives none",
              ok && cStatus == 0);

        // ---- StatusRefreshReplicationCheck（指令一百二十）：刷新 -> A/B 都收 Updated ----
        {
            const auto refresh =
                ApplyWhitebox(servers, kTypeMonster, 6, kStatusEffectIdBurn, seedA.characterId);
            WorldNetworkEvent aUpdated;
            const bool aUpd = WaitStatusEvent(
                clientA, WorldNetworkEvent::Type::StatusUpdatedEvent,
                [&](const WorldNetworkEvent& e) {
                    return e.status.effectId == 2003 && e.status.targetEntityId == 6;
                },
                aUpdated);
            WorldNetworkEvent bUpdated;
            const bool bUpd = WaitStatusEvent(
                clientB, WorldNetworkEvent::Type::StatusUpdatedEvent,
                [&](const WorldNetworkEvent& e) {
                    return e.status.effectId == 2003 && e.status.targetEntityId == 6;
                },
                bUpdated);
            Check("StatusRefreshReplicationCheck: refresh -> A+B both receive Updated",
                  refresh.result == StatusApplyResult::Refreshed && aUpd && bUpd);
        }
    }

    // ---- StatusSnapshot 系列（指令五十九/六十/一百一十六/一百一十七）----
    {
        WorldNetworkEvent snap;
        const bool got = WaitStatusEvent(
            clientA, WorldNetworkEvent::Type::StatusSnapshotEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.targetType == kTypeMonster && e.status.targetEntityId == 6;
            },
            snap, 6000);
        bool ok = got && !snap.status.snapshotEffects.empty();
        if (ok) {
            ok = ok && snap.status.snapshotEffects[0].effectId == 2003 &&
                 snap.status.snapshotEffects[0].stacks == 1;
        }
        // A 自身快照到达（Player targetType；BF 若已过期则 effects 为空属合法）
        WorldNetworkEvent selfSnap;
        const bool selfGot = WaitStatusEvent(
            clientA, WorldNetworkEvent::Type::StatusSnapshotEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.targetType == kTypePlayer &&
                       e.status.targetEntityId == seedA.characterId;
            },
            selfSnap, 6000);
        ok = ok && selfGot;
        // C：只收到自己（+可见实体）快照——Monster 快照的 target 必须在 C 的服务器
        // 可见集内（指令一百一十七/六十：绝不发全世界状态）。
        // 阶段21：历史快照不参与校验——多地图布局下怪物游荡会穿越 AOI 边界，
        // "当时可见"无法回溯验证；只校验本检查期间（baseline 后）新到达的快照
        //（C 自身快照 2s 周期必达，窗口内必有新样本）。baseline 用 counts（DrainEvents 递增）。
        // 阶段25.5 慢机加固：可见集取"窗口前 + 窗口后"两次采样的并集——怪物在
        // 采样间隙游荡进入可见集会造成假阳性泄漏（CI 慢机实测触发）；并集不削弱
        // 泄漏检测（跨地图/远处怪永远不会出现在两次采样内），仅消除进入竞态。
        const auto sampleVisible = [&seedC](WorldTestServers& servers_) {
            return RunOnWorldIo(servers_.worldService, [&] {
                auto p = servers_.world->FindPlayerByCharacter(seedC.characterId);
                if (!p) {
                    return std::vector<std::uint64_t>{};
                }
                return std::vector<std::uint64_t>(p->VisibleMonsters().begin(),
                                                  p->VisibleMonsters().end());
            });
        };
        const std::vector<std::uint64_t> cVisibleBefore = sampleVisible(servers);
        const int cSnapBaseline = clientC.counts[WorldTestClient::IndexOf(
            WorldNetworkEvent::Type::StatusSnapshotEvent)];
        clientC.DrainEvents();
        // 等至少一条新快照（2s 快照周期），保证校验窗口非空。
        const bool gotNewSnap = WaitUntil(
            [&] {
                clientC.DrainEvents();
                return clientC.counts[WorldTestClient::IndexOf(
                           WorldNetworkEvent::Type::StatusSnapshotEvent)] > cSnapBaseline;
            },
            4000);
        const std::vector<std::uint64_t> cVisibleAfter = sampleVisible(servers);
        std::vector<std::uint64_t> cVisible = cVisibleBefore;
        for (const auto entityId : cVisibleAfter) {
            if (std::find(cVisible.begin(), cVisible.end(), entityId) == cVisible.end()) {
                cVisible.push_back(entityId);
            }
        }
        bool noFar = true;
        const auto& cSnapEvents = clientC.recorded[WorldTestClient::IndexOf(
            WorldNetworkEvent::Type::StatusSnapshotEvent)];
        for (int i = cSnapBaseline; i < static_cast<int>(cSnapEvents.size()); ++i) {
            const auto& e = cSnapEvents[static_cast<std::size_t>(i)];
            if (e.status.targetType == kTypeMonster &&
                std::find(cVisible.begin(), cVisible.end(), e.status.targetEntityId) ==
                    cVisible.end()) {
                noFar = false; // 快照包含 C 看不见的怪 -> 全图广播泄漏
            }
        }
        ok = ok && noFar && gotNewSnap;
        if (!ok) {
            int cMonsterSnap = 0;
            for (const auto& e : clientC.recorded[WorldTestClient::IndexOf(
                                     WorldNetworkEvent::Type::StatusSnapshotEvent)]) {
                if (e.status.targetType == kTypeMonster) {
                    ++cMonsterSnap;
                }
            }
            std::printf("[Diag] Snapshot: got=%d effects=%zu selfGot=%d cMonsterSnap=%d\n",
                        static_cast<int>(got), snap.status.snapshotEffects.size(),
                        static_cast<int>(selfGot), cMonsterSnap);
        }
        Check("StatusSnapshotCheck: 2s snapshot for self+visible monsters only; far client none",
              ok);
    }

    // ---- DOT NoGlobal / Observer CombatEvent（指令一百三十一/一百三十二）----
    {
        // slime6 的 Burn（A 施加）tick：B 可见 -> 收到；C 不可见 -> 0 条
        const int cDot = CountDotCombatEventsFor(clientC, 0, 0); // 任意 StatusEffect 来源
        bool bDotOk = false;
        WaitUntil(
            [&] {
                clientB.DrainEvents();
                return CountDotCombatEventsFor(clientB, 2003, 6, seedA.characterId) >= 1;
            },
            5000);
        bDotOk = CountDotCombatEventsFor(clientB, 2003, 6, seedA.characterId) >= 1;
        Check("DOTNoGlobalCombatBroadcastCheck/ObserverCombatCheck: observer receives DOT, "
              "far client none",
              cDot == 0 && bDotOk);
    }

    // ---- DOTAfterCasterDisconnect（指令一百三十三）：W 施 Burn 后断线，killer 仍 W ----
    {
        servers.world->MoveMonsterTo(17, 700.0f, 950.0f); // W 距 304（FB 500 内）/ B 距 570（可见）
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const bool wSees = WaitMonsterVisible(servers, seedW.characterId, 17);
        clientW.controller.SendSkillCast(kSkillIdFireBolt, kTargetMonsterSkill, 17);
        WorldNetworkEvent applied;
        const bool gotApplied = WaitStatusEvent(
            clientW, WorldNetworkEvent::Type::StatusAppliedEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.effectId == 2003 && e.status.targetEntityId == 17;
            },
            applied, 6000);
        int seen = 0;
        WorldNetworkEvent dot;
        const bool firstTick = WaitDotCombatEvent(clientW, 2003, 17, seen, dot, 4000);
        const bool wReady = gotApplied && firstTick && wSees;
        clientW.Disconnect();
        // 断线后 DOT 继续：第二跳 + 击杀跳 killer=W（B 是唯一观察者）
        bool ok = wReady;
        int seenB = 0;
        bool bTickAfterQuit = false;
        const bool gotBTick = WaitUntil(
            [&] {
                clientB.DrainEvents();
                const auto& events =
                    clientB
                        .recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::CombatEvent)];
                for (int i = seenB; i < static_cast<int>(events.size()); ++i) {
                    const auto& e = events[static_cast<std::size_t>(i)];
                    if (e.sourceType == kSourceStatus && e.sourceId == 2003 &&
                        e.targetId == 17 && e.attackerId == seedW.characterId) {
                        bTickAfterQuit = true;
                    }
                }
                seenB = static_cast<int>(events.size());
                return bTickAfterQuit;
            },
            8000);
        ok = ok && gotBTick;
        const bool death = WaitUntil(
            [&] {
                clientB.DrainEvents();
                for (const auto& e : clientB.recorded[WorldTestClient::IndexOf(
                                         WorldNetworkEvent::Type::MonsterDeath)]) {
                    if (e.monsterEntityId == 17 && e.characterId == seedW.characterId) {
                        return true;
                    }
                }
                return false;
            },
            8000);
        const auto monster17 = ReadMonster(servers, 17);
        ok = ok && death && monster17.found && !monster17.alive;
        Check("DOTAfterCasterDisconnectCheck: Burn persists after caster quit, killer still W",
              ok);
    }

    // ---- 等待窗口：各状态到期验证（事件大多已发生在 recorded 缓存中）----
    {
        // 1) BurnTick 4 跳（slime9 满血 80：72/64/56/48，指令九十五）
        {
            int seen = 0;
            std::vector<std::uint32_t> hpSequence;
            for (int i = 0; i < 4; ++i) {
                WorldNetworkEvent dot;
                if (!WaitDotCombatEvent(clientA, 2003, 9, seen, dot, 6000)) {
                    break;
                }
                hpSequence.push_back(dot.targetHpAfter);
            }
            bool burnOk = hpSequence.size() == 4 && hpSequence[0] == 72 && hpSequence[1] == 64 &&
                          hpSequence[2] == 56 && hpSequence[3] == 48;
            const auto slime9 = ReadMonster(servers, 9);
            burnOk = burnOk && slime9.hp == 48 && slime9.alive;
            Check("BurnTickCheck: 4 ticks at 2/4/6/8s (80->48), stops after expire", burnOk);
        }
        // 2) Poison 3 层 12/s x6 + expire 停止（slime7：80 -> 8，指令九十九/一百零二）
        {
            int seen = 0;
            std::vector<std::uint32_t> damages;
            for (int i = 0; i < 6; ++i) {
                WorldNetworkEvent dot;
                if (!WaitDotCombatEvent(clientA, 2004, 7, seen, dot, 5000)) {
                    break;
                }
                damages.push_back(dot.damage);
            }
            bool poisonOk = damages.size() == 6;
            for (const auto d : damages) {
                poisonOk = poisonOk && d == 12; // 3 层 x 4
            }
            const auto slime7a = ReadMonster(servers, 7);
            poisonOk = poisonOk && slime7a.hp == 8;
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            const auto slime7b = ReadMonster(servers, 7);
            poisonOk = poisonOk && slime7b.hp == 8 && slime7b.statusCount == 0;
            Check("PoisonDamageExpireCheck: 3 stacks 12/s x6 ticks, expire stops damage", poisonOk);
        }
        // 3) BurnKill（slime8：A 的 FireBolt 链路；12 -> 4 -> 死 killer=A，指令九十七）
        //    实体 3s 后被清理移除——FindMonster 为 null 亦视为已死。
        {
            const bool killed = WaitUntil(
                [&] {
                    clientA.DrainEvents(); // MonsterDeath/Removed 进 recorded
                    auto m = servers.world->FindMonster(8);
                    const bool dead = !m || !m->Alive();
                    const int deathCount = CountMonsterDeathsFor(clientA, 8);
                    const int removed = CountStatusEventsFor(
                        clientA, WorldNetworkEvent::Type::StatusRemovedEvent, 2003, kTypeMonster,
                        8, static_cast<int>(StatusRemovedReason::TargetDied));
                    return dead && deathCount >= 1 && removed >= 1;
                },
                8000);
            const bool gotDeath = true; // 死亡事件已并入 killed 等待
            const auto monster8 = ReadMonster(servers, 8);
            bool burnOk = killed && gotDeath && (!monster8.found || !monster8.alive);
            burnOk = burnOk && monster8.statusCount == 0;
            Check("BurnKillCheck: last tick kills slime8, MonsterDeath killer=A, status cleared",
                  burnOk);
        }
        // 4) ArmorBreak expire（slime12）：8s 后状态消失 + defense 2 恢复（指令九十三）。
        // slime12 无玩家可见 -> Expired 广播无接收者（指令五十七：只发可见），
        // 事件断言由 slime6（A/B 可见）的 StatusRemoveReplicationCheck 覆盖。
        {
            const bool expired = WaitUntil(
                [&] {
                    const auto view = ReadMonster(servers, 12);
                    return view.found && view.statusCount == 0 && view.defense == 2;
                },
                12000);
            Check("ArmorBreakExpireCheck: 8s expire -> status gone, defense 0->2", expired);
        }
        // 5) Slow expire（slime11）：5s 后状态消失 + speed 恢复 80（指令七十五/一百零六）
        {
            const bool expired = WaitUntil(
                [&] {
                    const auto view = ReadMonster(servers, 11);
                    return view.found && view.statusCount == 0 &&
                           std::fabs(view.moveSpeed - 80.0f) < 0.001f;
                },
                10000);
            Check("SlowExpireCheck: 5s expire -> status gone, speed 48->80", expired);
        }
        // 6) slime6 Burn expire：A/B 都收到 Removed(Expired)（指令一百二十一；A/B 可见）
        {
            bool aRemoved = false;
            bool bRemoved = false;
            const bool waited = WaitUntil(
                [&] {
                    clientA.DrainEvents();
                    clientB.DrainEvents();
                    aRemoved = aRemoved || CountStatusEventsFor(
                                              clientA, WorldNetworkEvent::Type::StatusRemovedEvent,
                                              2003, kTypeMonster, 6,
                                              static_cast<int>(StatusRemovedReason::Expired)) >= 1;
                    bRemoved = bRemoved || CountStatusEventsFor(
                                              clientB, WorldNetworkEvent::Type::StatusRemovedEvent,
                                              2003, kTypeMonster, 6,
                                              static_cast<int>(StatusRemovedReason::Expired)) >= 1;
                    return aRemoved && bRemoved;
                },
                12000);
            const auto slime6 = ReadMonster(servers, 6);
            Check("StatusRemoveReplicationCheck: Burn expired -> A+B both Removed(Expired)",
                  waited && aRemoved && bRemoved && slime6.statusCount == 0);
        }
    }

    // ---- BattleFocusExpireCheck（指令七十六/八十九）：10s 后 Removed(Expired) + Attack 20 ----
    {
        WorldNetworkEvent removedEvent;
        const bool removed = WaitStatusEvent(
            clientA, WorldNetworkEvent::Type::StatusRemovedEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.effectId == 2001 && e.status.targetEntityId == seedA.characterId &&
                       e.status.reason ==
                           static_cast<std::uint8_t>(StatusRemovedReason::Expired);
            },
            removedEvent, 12000);
        const auto player = ReadPlayer(servers, seedA.characterId);
        bool ok = removed && player.found && player.attack == 20 && player.statusCount == 0;
        ok = ok && clientA.controller.LocalStatusEffects().Count() == 0;
        Check("BattleFocusExpireCheck: 10s expire -> Removed(Expired), attack 30->20", ok);
    }

    // ---- StatusClearOnPlayerDeathCheck（指令三十九/一百一十五）：D 被围殴致死 ----
    {
        WorldNetworkEvent death;
        const bool died = WaitStatusEvent(clientD, WorldNetworkEvent::Type::PlayerDeath,
                                          [](const WorldNetworkEvent&) { return true; }, death,
                                          30000);
        WorldNetworkEvent removed;
        const bool gotRemoved = WaitStatusEvent(
            clientD, WorldNetworkEvent::Type::StatusRemovedEvent,
            [&](const WorldNetworkEvent& e) {
                return e.status.effectId == 2001 &&
                       e.status.targetEntityId == seedD.characterId &&
                       e.status.reason ==
                           static_cast<std::uint8_t>(StatusRemovedReason::TargetDied);
            },
            removed, 3000);
        const auto player = ReadPlayer(servers, seedD.characterId);
        bool ok = died && gotRemoved && player.found && player.statusCount == 0;
        ok = ok && clientD.controller.LocalStatusEffects().Count() == 0;
        Check("StatusClearOnPlayerDeathCheck: D dies -> BF Removed(TargetDied), container empty",
              ok);
    }

    // ---- WorldRestartStatusClear + NoStatusPersistence + TimerStop（指令一百三十四~一百三十七）----
    {
        servers.StopWorld(); // m_statusTimer cancel（指令一百三十七）——无悬挂回调 Crash
        const bool restarted = servers.StartWorld();
        bool ok = restarted;
        const auto ids = servers.world->MonsterEntityIds();
        ok = ok && ids.size() == 51; // 阶段25：50 slime + Boss
        bool allClear = true;
        for (const auto id : ids) {
            const auto view = ReadMonster(servers, id);
            if (!view.found || view.statusCount != 0) {
                allClear = false;
                break;
            }
        }
        ok = ok && allClear;
        // A 重进无状态（指令七十九）
        clientA.Disconnect();
        const bool reenter = clientA.ConnectAndEnter(TicketFor(servers.login, seedA), 8000);
        const auto player = ReadPlayer(servers, seedA.characterId);
        ok = ok && reenter && player.found && player.statusCount == 0 && player.attack == 20;
        Check("WorldRestartStatusClearCheck: restart clears all monster/player statuses", ok);

        const std::int64_t tables =
            QueryScalar(servers.dbPath,
                        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND "
                        "name IN ('status_effect','buff','debuff')");
        Check("NoStatusPersistenceCheck: no status/buff/debuff tables in SQLite", tables == 0);
    }
}

} // namespace

void RunWorldStatusChecks() {
    std::printf("[WorldStatus] logic checks begin\n");
    RunStatusLogicChecks();
    std::printf("[WorldStatus] chain checks begin\n");
    RunStatusChainChecks();
}

} // namespace worldtest
