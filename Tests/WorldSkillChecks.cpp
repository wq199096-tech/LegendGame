// ---------------------------------------------------------------------------
// 阶段15：技能、施法与战斗表现同步检查（Skill & Ability Replication Core V0.15）。
// 仍链接 LegendWorldTests（不新增测试 exe，指令九十九）。
// A 部分：纯逻辑（SkillDefinition/SkillProtocol/ValidateSkillCast/ResolveAoeTargets/
//         Mana 下限）无需服务器。
// B 部分：真实链路自管 servers（主场景/Whirlwind/MaxTarget/持久化边界四个独立
//         servers 生命周期，与阶段12~14 相同端口串行复用 17240/17241/17242）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Shared/Monster/MonsterDefinition.h"
#include "Server/WorldServer/Monster/MonsterEntity.h"
#include "Server/WorldServer/PlayerSession.h"
#include "Server/WorldServer/Skill/SkillRegistry.h"
#include "Server/WorldServer/Skill/SkillService.h"
#include "Shared/Combat/CombatTypes.h"
#include "Shared/Monster/MonsterTypes.h"
#include "Shared/Skill/SkillProtocol.h"
#include "Shared/Skill/SkillTypes.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_set>

namespace worldtest {

namespace {

using legend::world::CalculateSkillDamage;
using legend::world::kFireBoltDefinition;
using legend::world::kPlayerMaxMana;
using legend::world::kQuickStrikeDefinition;
using legend::world::kSkillIdBattleFocus;
using legend::world::kSkillIdCripplingStrike;
using legend::world::kSkillIdFireBolt;
using legend::world::kSkillIdQuickStrike;
using legend::world::kSkillIdWhirlwind;
using legend::world::kSkillImpactMaxTargets;
using legend::world::kStatusEffectIdArmorBreak;
using legend::world::kStatusEffectIdBattleFocus;
using legend::world::kStatusEffectIdBurn;
using legend::world::kStatusEffectIdPoison;
using legend::world::kStatusEffectIdSlow;
using legend::world::kWhirlwindDefinition;
using legend::world::MonsterAoiCandidate;
using legend::world::MonsterState;
using legend::world::PendingSkillCast;
using legend::world::PlayerSession;
using legend::world::ResolveAoeTargets;
using legend::world::SkillCastContext;
using legend::world::SkillCastRequestPayload;
using legend::world::SkillCastResponsePayload;
using legend::world::SkillCancelReason;
using legend::world::SkillDefinition;
using legend::world::SkillImpactEventPayload;
using legend::world::SkillImpactTarget;
using legend::world::SkillRegistry;
using legend::world::SkillResultCode;
using legend::world::SkillTargetType;
using legend::world::ValidateSkillCast;
using RemoteMonsterEntityT = legend::client::RemoteMonsterEntity;
namespace CharacterRepository = legend::account::CharacterRepository;

const std::uint8_t kTargetMonster = static_cast<std::uint8_t>(SkillTargetType::Monster);
const std::uint8_t kTargetSelf = static_cast<std::uint8_t>(SkillTargetType::Self);
const std::uint8_t kSourceSkill = static_cast<std::uint8_t>(legend::world::CombatSource::Skill);

// ---- 技能事件辅助 ----

int CountSkillEvents(const WorldTestClient& client, WorldNetworkEvent::Type type) {
    return static_cast<int>(client.recorded[WorldTestClient::IndexOf(type)].size());
}

int CountSkillEventsFor(const WorldTestClient& client, WorldNetworkEvent::Type type,
                        std::uint64_t characterId) {
    int n = 0;
    for (const auto& e : client.recorded[WorldTestClient::IndexOf(type)]) {
        if (e.characterId == characterId) {
            ++n;
        }
    }
    return n;
}

// 等待指定 requestId 的 SkillCastResponse（expectAccepted<0 不过滤）。
bool WaitSkillResponse(WorldTestClient& client, std::uint64_t requestId,
                       WorldNetworkEvent& out, int timeoutMs = 3000, int expectAccepted = -1) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            const auto& events =
                client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::SkillCastResponseEvent)];
            for (auto it = events.rbegin(); it != events.rend(); ++it) {
                if (it->requestId == requestId &&
                    (expectAccepted < 0 || static_cast<int>(it->accepted ? 1 : 0) == expectAccepted)) {
                    out = *it;
                    return true;
                }
            }
            return false;
        },
        timeoutMs);
}

// 等待指定 requestId 的 AttackResponse（普攻回执；阶段14 协议复用）。
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

// 等待施法者（characterId）的下一个 Started 事件（requestId 窗口：取 afterCount
// 之后新增的第一条，避免旧 cast 干扰）。
bool WaitSkillStarted(WorldTestClient& client, std::uint64_t casterId, int& seenCount,
                      WorldNetworkEvent& out, int timeoutMs = 3000) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            const auto& events =
                client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::SkillCastStartedEvent)];
            if (static_cast<int>(events.size()) > seenCount) {
                // 从尾部找该施法者最新事件
                for (auto it = events.rbegin(); it != events.rend(); ++it) {
                    if (it->characterId == casterId) {
                        out = *it;
                        break;
                    }
                }
                seenCount = static_cast<int>(events.size());
                return true;
            }
            return false;
        },
        timeoutMs);
}

// 等待指定 castId 的 Completed / Cancelled / Impact 事件。
bool WaitSkillEventForCast(WorldTestClient& client, WorldNetworkEvent::Type type,
                           std::uint64_t castId, WorldNetworkEvent& out,
                           int timeoutMs = 5000) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            const auto& events = client.recorded[WorldTestClient::IndexOf(type)];
            for (auto it = events.rbegin(); it != events.rend(); ++it) {
                if (it->castId == castId) {
                    out = *it;
                    return true;
                }
            }
            return false;
        },
        timeoutMs);
}

// 等待施法者对某技能来源的 CombatEvent（sourceType=Skill, sourceId=skillId）。
bool WaitSkillCombatEvent(WorldTestClient& client, std::uint64_t casterId,
                          std::uint32_t skillId, int& seenCount, WorldNetworkEvent& out,
                          int timeoutMs = 5000) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            const auto& events =
                client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::CombatEvent)];
            for (int i = seenCount; i < static_cast<int>(events.size()); ++i) {
                const auto& e = events[static_cast<std::size_t>(i)];
                if (e.sourceType == kSourceSkill && e.sourceId == skillId &&
                    e.attackerId == casterId) {
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

int CountSkillCombatEventsFor(const WorldTestClient& client, std::uint64_t casterId,
                              std::uint64_t targetId) {
    int n = 0;
    for (const auto& e :
         client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::CombatEvent)]) {
        if (e.sourceType == kSourceSkill && e.attackerId == casterId &&
            (targetId == 0 || e.targetId == targetId)) {
            ++n;
        }
    }
    return n;
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

// 等待服务器侧施法者与怪距离进入区间且该怪已进入 visibleMonsters（AOI tick
// 200ms 滞后——施法前必须确认可见性，否则合法请求会被 InvalidTarget 拒绝）。
bool WaitMonsterDistance(WorldTestServers& servers, std::uint64_t characterId,
                         std::uint64_t monsterId, float minDist, float maxDist,
                         int timeoutMs = 3000) {
    return WaitUntil(
        [&] {
            auto player = servers.world->FindPlayerByCharacter(characterId);
            auto monster = servers.world->FindMonster(monsterId);
            if (!player || !monster) {
                return false;
            }
            if (player->VisibleMonsters().count(monsterId) == 0) {
                return false; // 尚未进入可见集
            }
            const float dx = player->PositionX() - monster->PositionX();
            const float dy = player->PositionY() - monster->PositionY();
            const float dist = std::sqrt(dx * dx + dy * dy);
            return dist >= minDist && dist <= maxDist;
        },
        timeoutMs);
}

// 等待所有指定怪进入施法者 radius（Whirlwind 布景）。
bool WaitAllMonstersWithin(WorldTestServers& servers, std::uint64_t characterId,
                           const std::vector<std::uint64_t>& monsterIds, float radius,
                           int timeoutMs = 8000) {
    return WaitUntil(
        [&] {
            auto player = servers.world->FindPlayerByCharacter(characterId);
            if (!player) {
                return false;
            }
            for (const auto id : monsterIds) {
                auto monster = servers.world->FindMonster(id);
                if (!monster) {
                    return false;
                }
                const float dx = player->PositionX() - monster->PositionX();
                const float dy = player->PositionY() - monster->PositionY();
                if (dx * dx + dy * dy > radius * radius) {
                    return false;
                }
            }
            return true;
        },
        timeoutMs);
}

// ===========================================================================
// A. 纯逻辑检查（无服务器）
// ===========================================================================

void RunSkillLogicChecks() {
    // ---- SkillDefinitionCheck（指令一百/八/九/十；阶段16 指令十九/二十：+1004/1005）----
    {
        SkillRegistry registry;
        bool ok = registry.Count() == 5;
        const auto* qs = registry.FindSkill(kSkillIdQuickStrike);
        ok = ok && qs != nullptr && qs->name == "Quick Strike" &&
             qs->castType == legend::world::SkillCastType::Instant &&
             qs->targetType == SkillTargetType::Monster &&
             qs->cooldownSeconds == 1.5f && qs->castTimeSeconds == 0.0f &&
             qs->manaCost == 10 && qs->range == 120.0f && qs->baseDamage == 30 &&
             qs->aoeRadius == 0.0f && qs->maxTargets == 1;
        const auto* fb = registry.FindSkill(kSkillIdFireBolt);
        ok = ok && fb != nullptr && fb->name == "Fire Bolt" &&
             fb->castType == legend::world::SkillCastType::CastTime &&
             fb->targetType == SkillTargetType::Monster &&
             fb->cooldownSeconds == 3.0f && fb->castTimeSeconds == 1.0f &&
             fb->manaCost == 20 && fb->range == 500.0f && fb->baseDamage == 40 &&
             fb->aoeRadius == 0.0f && fb->maxTargets == 1;
        const auto* ww = registry.FindSkill(kSkillIdWhirlwind);
        ok = ok && ww != nullptr && ww->name == "Whirlwind" &&
             ww->castType == legend::world::SkillCastType::Instant &&
             ww->targetType == SkillTargetType::Self &&
             ww->cooldownSeconds == 5.0f && ww->manaCost == 25 && ww->range == 0.0f &&
             ww->baseDamage == 25 && ww->aoeRadius == 160.0f && ww->maxTargets == 16;
        // 阶段16：Battle Focus（1004 Self 无伤害）+ Crippling Strike（1005 Slow）
        const auto* bf = registry.FindSkill(kSkillIdBattleFocus);
        ok = ok && bf != nullptr && bf->name == "Battle Focus" &&
             bf->castType == legend::world::SkillCastType::Instant &&
             bf->targetType == SkillTargetType::Self &&
             bf->cooldownSeconds == 8.0f && bf->manaCost == 15 && bf->baseDamage == 0 &&
             bf->applyStatusEffectId == kStatusEffectIdBattleFocus;
        const auto* cs = registry.FindSkill(kSkillIdCripplingStrike);
        ok = ok && cs != nullptr && cs->name == "Crippling Strike" &&
             cs->castType == legend::world::SkillCastType::Instant &&
             cs->targetType == SkillTargetType::Monster &&
             cs->cooldownSeconds == 4.0f && cs->manaCost == 15 && cs->range == 120.0f &&
             cs->baseDamage == 10 && cs->applyStatusEffectId == kStatusEffectIdSlow;
        // 阶段16：技能-状态关联（指令十八）
        ok = ok && qs->applyStatusEffectId == kStatusEffectIdArmorBreak &&
             qs->applyStatusStacks == 1 && fb->applyStatusEffectId == kStatusEffectIdBurn &&
             ww->applyStatusEffectId == kStatusEffectIdPoison;
        ok = ok && registry.FindSkill(999999) == nullptr;
        Check("SkillDefinitionCheck: QS/FB/WW/BF/CS config + status links + unknown", ok);
    }

    // ---- SkillProtocolRoundtripCheck（指令二十二~三十/八十六）----
    {
        std::string error;
        bool ok = true;
        {
            SkillCastRequestPayload req;
            req.requestId = 5001;
            req.skillId = kSkillIdFireBolt;
            req.targetType = kTargetMonster;
            req.targetEntityId = 42;
            std::vector<std::uint8_t> payload;
            SkillCastRequestPayload decoded;
            ok = ok && legend::world::EncodeSkillCastRequest(req, payload) &&
                 legend::world::DecodeSkillCastRequest(payload.data(), payload.size(), decoded,
                                                       error) &&
                 decoded.requestId == 5001 && decoded.skillId == kSkillIdFireBolt &&
                 decoded.targetType == kTargetMonster && decoded.targetEntityId == 42;
        }
        {
            SkillCastResponsePayload resp;
            resp.requestId = 5002;
            resp.skillId = kSkillIdQuickStrike;
            resp.accepted = false;
            resp.resultCode = static_cast<std::uint8_t>(SkillResultCode::NotEnoughMana);
            resp.currentMana = 5;
            resp.message = "NotEnoughMana";
            std::vector<std::uint8_t> payload;
            SkillCastResponsePayload decoded;
            ok = ok && legend::world::EncodeSkillCastResponse(resp, payload) &&
                 legend::world::DecodeSkillCastResponse(payload.data(), payload.size(), decoded,
                                                        error) &&
                 !decoded.accepted && decoded.currentMana == 5 && decoded.message == "NotEnoughMana";
        }
        {
            legend::world::SkillCastStartedPayload started;
            started.castId = 7;
            started.casterCharacterId = 11;
            started.skillId = kSkillIdFireBolt;
            started.targetType = kTargetMonster;
            started.targetEntityId = 3;
            started.castTimeMs = 1000;
            started.serverTime = 999;
            std::vector<std::uint8_t> payload;
            legend::world::SkillCastStartedPayload decoded;
            ok = ok && legend::world::EncodeSkillCastStarted(started, payload) &&
                 legend::world::DecodeSkillCastStarted(payload.data(), payload.size(), decoded,
                                                       error) &&
                 decoded.castId == 7 && decoded.castTimeMs == 1000;
        }
        {
            legend::world::SkillCastCompletedPayload completed;
            completed.castId = 8;
            completed.casterCharacterId = 11;
            completed.skillId = kSkillIdQuickStrike;
            completed.targetType = kTargetMonster;
            completed.targetEntityId = 3;
            completed.serverTime = 1234;
            std::vector<std::uint8_t> payload;
            legend::world::SkillCastCompletedPayload decoded;
            ok = ok && legend::world::EncodeSkillCastCompleted(completed, payload) &&
                 legend::world::DecodeSkillCastCompleted(payload.data(), payload.size(), decoded,
                                                         error) &&
                 decoded.castId == 8 && decoded.targetEntityId == 3;
        }
        {
            legend::world::SkillCastCancelledPayload cancelled;
            cancelled.castId = 9;
            cancelled.casterCharacterId = 11;
            cancelled.skillId = kSkillIdFireBolt;
            cancelled.reason = static_cast<std::uint8_t>(SkillCancelReason::Moved);
            cancelled.serverTime = 777;
            std::vector<std::uint8_t> payload;
            legend::world::SkillCastCancelledPayload decoded;
            ok = ok && legend::world::EncodeSkillCastCancelled(cancelled, payload) &&
                 legend::world::DecodeSkillCastCancelled(payload.data(), payload.size(), decoded,
                                                         error) &&
                 decoded.castId == 9 &&
                 decoded.reason == static_cast<std::uint8_t>(SkillCancelReason::Moved);
        }
        {
            SkillImpactEventPayload impact;
            impact.castId = 10;
            impact.skillId = kSkillIdWhirlwind;
            impact.casterCharacterId = 11;
            impact.serverTime = 555;
            for (int i = 0; i < 3; ++i) {
                SkillImpactTarget t;
                t.entityType = 2;
                t.entityId = static_cast<std::uint64_t>(100 + i);
                t.damage = 43;
                t.hpAfter = 37;
                t.maxHp = 80;
                t.killed = false;
                impact.targets.push_back(t);
            }
            std::vector<std::uint8_t> payload;
            SkillImpactEventPayload decoded;
            ok = ok && legend::world::EncodeSkillImpactEvent(impact, payload) &&
                 legend::world::DecodeSkillImpactEvent(payload.data(), payload.size(), decoded,
                                                       error) &&
                 decoded.castId == 10 && decoded.targets.size() == 3 &&
                 decoded.targets[2].entityId == 102 && decoded.targets[2].hpAfter == 37;
        }
        {
            legend::world::ManaSnapshotPayload mana;
            mana.currentMana = 70;
            mana.maxMana = 100;
            mana.serverTime = 888;
            std::vector<std::uint8_t> payload;
            legend::world::ManaSnapshotPayload decoded;
            ok = ok && legend::world::EncodeManaSnapshot(mana, payload) &&
                 legend::world::DecodeManaSnapshot(payload.data(), payload.size(), decoded,
                                                   error) &&
                 decoded.currentMana == 70 && decoded.maxMana == 100;
        }
        Check("SkillProtocolRoundtripCheck: all 7 payloads roundtrip", ok);
    }

    // ---- MalformedSkillCastRequestCheck / MalformedSkillCastStartedCheck /
    //      MalformedSkillImpactCheck / SkillImpactTargetLimitCheck（指令八十六/
    //      一百三十五~一百三十八）----
    {
        std::string error;
        bool ok = true;
        // 截断 SkillCastRequest（缺尾部字节）
        {
            SkillCastRequestPayload req;
            req.requestId = 1;
            req.skillId = 1001;
            req.targetType = kTargetMonster;
            req.targetEntityId = 2;
            std::vector<std::uint8_t> payload;
            legend::world::EncodeSkillCastRequest(req, payload);
            SkillCastRequestPayload decoded;
            ok = ok && !legend::world::DecodeSkillCastRequest(payload.data(), payload.size() - 3,
                                                              decoded, error);
            // 尾部多余字节
            auto padded = payload;
            padded.push_back(0xAB);
            ok = ok && !legend::world::DecodeSkillCastRequest(padded.data(), padded.size(),
                                                              decoded, error);
        }
        // 截断 SkillCastStarted
        {
            legend::world::SkillCastStartedPayload started;
            started.castId = 1;
            std::vector<std::uint8_t> payload;
            legend::world::EncodeSkillCastStarted(started, payload);
            legend::world::SkillCastStartedPayload decoded;
            ok = ok && !legend::world::DecodeSkillCastStarted(payload.data(), payload.size() - 5,
                                                              decoded, error);
        }
        // Impact：count 大于 payload / count 超上限 17 拒绝；16 成功
        {
            SkillImpactEventPayload impact;
            impact.castId = 2;
            impact.skillId = 1003;
            impact.targets.resize(20); // encode 时超过 16 直接拒绝
            std::vector<std::uint8_t> payload;
            ok = ok && !legend::world::EncodeSkillImpactEvent(impact, payload) &&
                 payload.empty();
            // 手工构造 count=17 的合法前缀 payload：header + count=17 + 1 个 target
            std::vector<std::uint8_t> bad;
            legend::network::ByteWriter w(bad);
            w.WriteUInt64(2);
            w.WriteUInt32(1003);
            w.WriteUInt64(11);
            w.WriteUInt64(1);
            w.WriteUInt16(17); // 超上限
            w.WriteUInt8(2);
            w.WriteUInt64(1);
            w.WriteUInt32(1);
            w.WriteUInt32(1);
            w.WriteUInt32(1);
            w.WriteBool(false);
            SkillImpactEventPayload decoded;
            ok = ok && !legend::world::DecodeSkillImpactEvent(bad.data(), bad.size(), decoded,
                                                              error);
            // count=1 但 payload 截断 -> 失败
            std::vector<std::uint8_t> truncated(bad.begin(), bad.begin() + 20);
            truncated[14] = 0; // count 高字节
            truncated[15] = 1; // count 低字节 = 1
            ok = ok && !legend::world::DecodeSkillImpactEvent(truncated.data(), truncated.size(),
                                                              decoded, error);
        }
        {
            // count = 16 全量成功
            SkillImpactEventPayload impact;
            impact.castId = 3;
            impact.skillId = 1003;
            impact.targets.resize(kSkillImpactMaxTargets);
            std::vector<std::uint8_t> payload;
            SkillImpactEventPayload decoded;
            ok = ok && legend::world::EncodeSkillImpactEvent(impact, payload) &&
                 legend::world::DecodeSkillImpactEvent(payload.data(), payload.size(), decoded,
                                                       error) &&
                 decoded.targets.size() == kSkillImpactMaxTargets;
        }
        Check("MalformedSkillChecks: truncated/trailing/over-limit rejected, 16 accepted", ok);
    }

    // ---- ValidateSkillCastCheck（指令二十一/三十九/八十九~九十三/四十八/四十九）----
    {
        SkillCastContext ctx;
        ctx.requestTargetType = SkillTargetType::Monster;
        ctx.definitionTargetType = SkillTargetType::Monster;
        ctx.distanceSquared = 50.0f * 50.0f;
        ctx.rangeSquared = 120.0f * 120.0f;
        ctx.currentMana = 100;
        ctx.manaCost = 10;
        bool ok = ValidateSkillCast(ctx) == SkillResultCode::Success;
        ctx.casterAlive = false;
        ok = ok && ValidateSkillCast(ctx) == SkillResultCode::CasterDead;
        ctx.casterAlive = true;
        ctx.casterInWorld = false;
        ok = ok && ValidateSkillCast(ctx) == SkillResultCode::NotInWorld;
        ctx.casterInWorld = true;
        ctx.alreadyCasting = true;
        ok = ok && ValidateSkillCast(ctx) == SkillResultCode::AlreadyCasting;
        ctx.alreadyCasting = false;
        ctx.targetAlive = false;
        ok = ok && ValidateSkillCast(ctx) == SkillResultCode::TargetDead;
        ctx.targetAlive = true;
        ctx.targetVisible = false;
        ok = ok && ValidateSkillCast(ctx) == SkillResultCode::InvalidTarget;
        ctx.targetVisible = true;
        ctx.sameMap = false;
        ok = ok && ValidateSkillCast(ctx) == SkillResultCode::DifferentMap;
        ctx.sameMap = true;
        ctx.distanceSquared = 150.0f * 150.0f;
        ok = ok && ValidateSkillCast(ctx) == SkillResultCode::OutOfRange;
        ctx.distanceSquared = 50.0f * 50.0f;
        ctx.cooldownReady = false;
        ok = ok && ValidateSkillCast(ctx) == SkillResultCode::Cooldown;
        ctx.cooldownReady = true;
        ctx.currentMana = 5;
        ok = ok && ValidateSkillCast(ctx) == SkillResultCode::NotEnoughMana;
        // targetType 伪造：请求类型与定义不一致 -> InvalidTarget（无 PvP，指令四十八）
        ctx.currentMana = 100;
        ctx.requestTargetType = SkillTargetType::Ground;
        ok = ok && ValidateSkillCast(ctx) == SkillResultCode::InvalidTarget;
        ctx.requestTargetType = SkillTargetType::None;
        ok = ok && ValidateSkillCast(ctx) == SkillResultCode::InvalidTarget;
        Check("ValidateSkillCastCheck: full chain covers all failure codes", ok);
    }

    // ---- SkillDamageFormulaCheck（指令三十七/三十八）----
    {
        bool ok = CalculateSkillDamage(30, 20, 2) == 48; // Quick Strike
        ok = ok && CalculateSkillDamage(40, 20, 2) == 58; // Fire Bolt
        ok = ok && CalculateSkillDamage(25, 20, 2) == 43; // Whirlwind
        ok = ok && CalculateSkillDamage(1, 1, 100) == 1;  // 保底 1
        Check("SkillDamageFormulaCheck: 30+20-2=48, 40+20-2=58, 25+20-2=43, floor 1", ok);
    }

    // ---- ResolveAoeTargetsCheck（指令四十四/四十五/四十七/八十五/一百二十一/一百二十二）----
    {
        std::vector<MonsterAoiCandidate> candidates;
        auto makeMonster = [](std::uint64_t id, std::uint16_t mapId, bool alive) {
            auto m = std::make_shared<legend::world::MonsterEntity>(id, 1u, mapId, 0.0f, 0.0f,
                                                                    80.0f);
            m->InitializeCombat(80);
            if (!alive) {
                m->ApplyDamage(80); // alive=false
            }
            return m;
        };
        // id1 距离 50 / id2 距离 50（同距离 -> entityId 升序）/ id3 距离 100 /
        // id4 死亡 距离 10 / id5 跨地图 / id6 距离 200（超半径）
        auto m1 = makeMonster(1, 1, true);
        m1->SetPosition(50.0f, 0.0f);
        auto m2 = makeMonster(2, 1, true);
        m2->SetPosition(-50.0f, 0.0f);
        auto m3 = makeMonster(3, 1, true);
        m3->SetPosition(100.0f, 0.0f);
        auto m4 = makeMonster(4, 1, false);
        m4->SetPosition(10.0f, 0.0f);
        auto m5 = makeMonster(5, 2, true);
        m5->SetPosition(30.0f, 0.0f);
        auto m6 = makeMonster(6, 1, true);
        m6->SetPosition(200.0f, 0.0f);
        candidates.push_back({m6, 200.0f * 200.0f});
        candidates.push_back({m4, 10.0f * 10.0f});
        candidates.push_back({m3, 100.0f * 100.0f});
        candidates.push_back({m2, 50.0f * 50.0f});
        candidates.push_back({m5, 30.0f * 30.0f});
        candidates.push_back({m1, 50.0f * 50.0f});
        const auto targets = ResolveAoeTargets(candidates, 1, 160.0f, 16);
        bool ok = targets.size() == 3; // 1,2,3（4 死亡 / 5 跨图 / 6 超半径）
        ok = ok && targets[0]->EntityId() == 1 && targets[1]->EntityId() == 2 &&
             targets[2]->EntityId() == 3; // 同距离 entityId 升序（稳定）
        // maxTargets 截断：上限 2 -> 只留 1,2
        const auto capped = ResolveAoeTargets(candidates, 1, 160.0f, 2);
        ok = ok && capped.size() == 2 && capped[0]->EntityId() == 1 &&
             capped[1]->EntityId() == 2;
        // maxTargets=0 防御 -> 上限 16
        const auto safe = ResolveAoeTargets(candidates, 1, 160.0f, 0);
        ok = ok && safe.size() == 3;
        Check("ResolveAoeTargetsCheck: alive/sameMap/radius filter + stable order + cap", ok);
    }

    // ---- SkillManaUnderflowCheck（指令一百四十一）----
    {
        PlayerSession player(0, 1, 2, "Mana", 1, 1, 1, 1, 0.0f, 0.0f);
        bool ok = player.CurrentMana() == kPlayerMaxMana && player.MaxMana() == 100;
        ok = ok && player.ConsumeMana(60) && player.CurrentMana() == 40;
        ok = ok && !player.ConsumeMana(60) && player.CurrentMana() == 40; // 拒绝，不变
        ok = ok && player.ConsumeMana(40) && player.CurrentMana() == 0;   // 精确归零
        ok = ok && !player.ConsumeMana(1) && player.CurrentMana() == 0;   // 永不为负
        Check("SkillManaUnderflowCheck: mana never negative, exact-zero allowed", ok);
    }

    // ---- SkillRequestReplayPureCheck（指令七十四）----
    {
        PlayerSession player(0, 1, 2, "Replay", 1, 1, 1, 1, 0.0f, 0.0f);
        player.RememberSkillRequest(77);
        bool ok = player.IsRecentSkillRequest(77);
        ok = ok && !player.IsRecentSkillRequest(78);
        // 环形容量：64 个之后 77 被挤出
        for (std::uint64_t i = 100; i < 100 + legend::world::kAttackRequestHistorySize; ++i) {
            player.RememberSkillRequest(i);
        }
        ok = ok && !player.IsRecentSkillRequest(77);
        Check("SkillRequestReplayPureCheck: 64-entry ring remembers accepted requests", ok);
    }

    // ---- CastingStatePureCheck（指令十四/十五）----
    {
        PlayerSession player(0, 1, 2, "Cast", 1, 1, 1, 1, 0.0f, 0.0f);
        bool ok = !player.IsCasting();
        PendingSkillCast cast;
        cast.active = true;
        cast.castId = 9;
        cast.skillId = kSkillIdFireBolt;
        cast.targetEntityId = 3;
        cast.castCompleteTime = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        player.SetCasting(cast);
        ok = ok && player.IsCasting() && player.Casting().castId == 9;
        player.ClearCasting();
        ok = ok && !player.IsCasting();
        // 技能冷却：IsSkillReady / StartSkillCooldown（steady_clock 权威，指令九十五）
        const auto now = std::chrono::steady_clock::now();
        ok = ok && player.IsSkillReady(kSkillIdQuickStrike, now);
        player.StartSkillCooldown(kSkillIdQuickStrike, 1.5f);
        ok = ok && !player.IsSkillReady(kSkillIdQuickStrike, now);
        ok = ok && player.IsSkillReady(kSkillIdFireBolt, now); // 每技能独立 CD（指令一百四十二）
        Check("CastingStatePureCheck: single cast slot + per-skill cooldown", ok);
    }
}

// ===========================================================================
// B1. 主场景真实链路（QuickStrike / FireBolt / Mana / 观察者 / 断线 / 死亡）
// ===========================================================================

void RunSkillChainChecksMain() {
    WorldTestServers servers;
    servers.dbPath = TempDbPath("world_skill_main");
    RemoveDb(servers.dbPath);
    Check("SkillServersStartCheck", servers.StartLogin() && servers.StartWorld());

    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(8, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("SkillChecks: db ready", false);
        servers.StopAll();
        return;
    }
    // ---- 布局：A 主施法者(100,1000 安静角落，远离未用怪物仇恨) /
    //      B 观察者(120,1020) / C 远处(60,60) / H 断线场景(1100,600) /
    //      D 死亡场景(1650,1650)。A 中途重进世界重置 HP（HP 不持久化）。 ----
    CharacterSeed seedA;
    CharacterSeed seedB;
    CharacterSeed seedC;
    CharacterSeed seedD;
    CharacterSeed seedH;
    bool seeded = true;
    seeded = seeded && SeedAt(db, accounts, characters, "sk_user_a", "SkA", 100.0f, 1000.0f, seedA);
    seeded = seeded && SeedAt(db, accounts, characters, "sk_user_b", "SkB", 120.0f, 1020.0f, seedB);
    seeded = seeded && SeedAt(db, accounts, characters, "sk_user_c", "SkC", 60.0f, 60.0f, seedC);
    seeded = seeded && SeedAt(db, accounts, characters, "sk_user_d", "SkD", 1650.0f, 1650.0f, seedD);
    seeded = seeded && SeedAt(db, accounts, characters, "sk_user_h", "SkH", 1100.0f, 600.0f, seedH);
    Check("SkillChecks: seeds ready", seeded);

    WorldTestClient clientC;
    WorldTestClient clientB;
    {
        const bool enterC = clientC.ConnectAndEnter(TicketFor(servers.login, seedC), 8000);
        const bool enterB = clientB.ConnectAndEnter(TicketFor(servers.login, seedB), 8000);
        Check("SkillChecks: C/B entered", enterC && enterB);
        // H 延迟进入（其断线场景在块尾——提前进入会被 cluster 2 怪围殴致死）。
    }
    // C（60,60）远离所有怪（最近簇 >600）：AOI 0 可见怪。

    // ---- QuickStrikeInvisibleTargetCheck（指令一百零五）：目标不在 visibleMonsters
    //      -> InvalidTarget（C 从未见过任何怪）----
    {
        clientC.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonster, 13);
        WorldNetworkEvent resp;
        const bool got = WaitSkillResponse(clientC, clientC.controller.LastSkillRequestId(), resp);
        bool ok = got && !resp.accepted &&
                  resp.skillResultCode == static_cast<std::uint8_t>(SkillResultCode::InvalidTarget);
        ok = ok && resp.currentMana == 100; // Mana 不扣
        Check("QuickStrikeInvisibleTargetCheck: invisible target -> InvalidTarget, mana kept", ok);
    }

    // ---- QuickStrikeOutOfRangeCheck（指令一百零四）：C 附近放一只怪（>120 可见）----
    {
        servers.world->MoveMonsterTo(5, 260.0f, 60.0f); // 距 C 200（>120；<600 可见）
        // 该怪 leash 回家（距 spawn (1000,500) 已 861 > 600）——距离只会增大。
        // 必须等 AOI tick 把它加进 C 的 visibleMonsters（否则 InvalidTarget）。
        const bool visible = WaitUntil(
            [&] {
                auto player = servers.world->FindPlayerByCharacter(seedC.characterId);
                return player != nullptr && player->VisibleMonsters().count(5) != 0;
            },
            3000);
        clientC.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonster, 5);
        WorldNetworkEvent resp;
        const bool got = WaitSkillResponse(clientC, clientC.controller.LastSkillRequestId(), resp);
        bool ok = visible && got && !resp.accepted &&
                  resp.skillResultCode == static_cast<std::uint8_t>(SkillResultCode::OutOfRange);
        ok = ok && resp.currentMana == 100;
        Check("QuickStrikeOutOfRangeCheck: >120 -> OutOfRange, no mana/cd cost", ok);
    }

    // ---- A 进入世界（迟进避免被围殴致死）----
    WorldTestClient clientA;
    {
        const bool enterA = clientA.ConnectAndEnter(TicketFor(servers.login, seedA), 8000);
        Check("SkillChecks: attacker A entered", enterA);
    }
    WorldTestClient clientD; // D 场景时才进入

    // ---- QuickStrikeSuccessCheck（指令一百零二）：slime3 移到 A 旁（72 units）----
    std::uint64_t aCastId = 0;
    {
        servers.world->MoveMonsterTo(3, 160.0f, 1040.0f);
        const bool positioned = WaitMonsterDistance(servers, seedA.characterId, 3, 60.0f, 120.0f);
        clientA.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonster, 3);
        WorldNetworkEvent resp;
        const bool got = WaitSkillResponse(clientA, clientA.controller.LastSkillRequestId(), resp,
                                           3000, 1);
        WorldNetworkEvent started;
        int seenStarted = 0;
        const bool gotStarted = got && WaitSkillStarted(clientA, seedA.characterId, seenStarted,
                                                        started);
        aCastId = gotStarted ? started.castId : 0;
        WorldNetworkEvent completed;
        const bool gotCompleted =
            gotStarted && WaitSkillEventForCast(clientA, WorldNetworkEvent::Type::SkillCastCompletedEvent,
                                                aCastId, completed);
        WorldNetworkEvent impact;
        const bool gotImpact =
            gotCompleted && WaitSkillEventForCast(clientA, WorldNetworkEvent::Type::SkillImpact,
                                                  aCastId, impact);
        WorldNetworkEvent combat;
        int seenCombat = 0;
        const bool gotCombat =
            gotImpact && WaitSkillCombatEvent(clientA, seedA.characterId, kSkillIdQuickStrike,
                                              seenCombat, combat);
        auto monster3 = servers.world->FindMonster(3);
        bool ok = got && gotStarted && gotCompleted && gotImpact && gotCombat;
        ok = ok && started.skillId == kSkillIdQuickStrike && started.castTimeMs == 0;
        ok = ok && impact.impactTargets.size() == 1 &&
             impact.impactTargets[0].damage == 48 && impact.impactTargets[0].hpAfter == 32 &&
             impact.impactTargets[0].maxHp == 80 && !impact.impactTargets[0].killed;
        ok = ok && combat.sourceType == kSourceSkill && combat.sourceId == kSkillIdQuickStrike &&
             combat.damage == 48 && combat.targetHpAfter == 32;
        ok = ok && resp.currentMana == 90; // 100 -> 90（指令十九：接受即扣）
        ok = ok && monster3 != nullptr && monster3->CurrentHp() == 32;
        Check("QuickStrikeSuccessCheck: 1001 accepted, 48 dmg 80->32, mana 100->90, "
              "Started+Completed+Impact+CombatEvent(Skill,1001)",
              ok);

        // ---- SkillObserverReplicationCheck（指令一百二十七）：B 能看到 A 的施法 ----
        {
            WorldNetworkEvent bStarted;
            const bool bStartedGot = WaitSkillEventForCast(
                clientB, WorldNetworkEvent::Type::SkillCastStartedEvent, aCastId, bStarted, 3000);
            WorldNetworkEvent bCompleted;
            const bool bCompletedGot = WaitSkillEventForCast(
                clientB, WorldNetworkEvent::Type::SkillCastCompletedEvent, aCastId, bCompleted, 3000);
            WorldNetworkEvent bImpact;
            const bool bImpactGot = WaitSkillEventForCast(clientB, WorldNetworkEvent::Type::SkillImpact,
                                                          aCastId, bImpact, 3000);
            const bool bCombat = CountSkillCombatEventsFor(clientB, seedA.characterId, 3) >= 1;
            Check("SkillObserverReplicationCheck: observer B sees Started/Completed/Impact/Combat",
                  bStartedGot && bCompletedGot && bImpactGot && bCombat);
        }
        // ---- NoDuplicateSkillBroadcastCheck（指令一百二十九）：A 本人各只收 1 份 ----
        {
            const int aStarted = CountSkillEventsFor(clientA,
                                                     WorldNetworkEvent::Type::SkillCastStartedEvent,
                                                     seedA.characterId);
            const int aCompleted = CountSkillEventsFor(
                clientA, WorldNetworkEvent::Type::SkillCastCompletedEvent, seedA.characterId);
            Check("NoDuplicateSkillBroadcastCheck: caster receives exactly one copy",
                  aStarted == 1 && aCompleted == 1);
        }
        // ---- NoGlobalSkillBroadcastCheck（指令一百二十八/五十四）：远处 C 0 技能事件 ----
        {
            const int cStarted = CountSkillEvents(clientC,
                                                  WorldNetworkEvent::Type::SkillCastStartedEvent);
            const int cCompleted = CountSkillEvents(
                clientC, WorldNetworkEvent::Type::SkillCastCompletedEvent);
            const int cImpact = CountSkillEvents(clientC, WorldNetworkEvent::Type::SkillImpact);
            const int cCombat = CountSkillCombatEventsFor(clientC, seedA.characterId, 0);
            Check("NoGlobalSkillBroadcastCheck: far client receives 0 skill events",
                  cStarted == 0 && cCompleted == 0 && cImpact == 0 && cCombat == 0);
        }
    }

    // ---- QuickStrikeCooldownCheck（指令一百零三）+ SkillKillMonsterCheck（指令一百二十五）----
    {
        auto monster3 = servers.world->FindMonster(3);
        const std::uint32_t hpBefore = monster3 ? monster3->CurrentHp() : 0;
        clientA.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonster, 3);
        WorldNetworkEvent resp;
        const bool got = WaitSkillResponse(clientA, clientA.controller.LastSkillRequestId(), resp);
        bool ok = got && !resp.accepted &&
                  resp.skillResultCode == static_cast<std::uint8_t>(SkillResultCode::Cooldown);
        ok = ok && resp.currentMana == 90; // CD 拒绝不扣 Mana
        auto monster3b = servers.world->FindMonster(3);
        ok = ok && monster3b != nullptr && monster3b->CurrentHp() == hpBefore; // HP 不变
        Check("QuickStrikeCooldownCheck: instant recast -> Cooldown, hp/mana unchanged", ok);

        // 等 1.5s CD 过后再放：QS#1 命中自动施加 Armor Break（阶段16 指令十八），
        // slime defense 2->0 -> QS#2 伤害 30+20-0 = 50 >= 32 -> 击杀
        std::this_thread::sleep_for(std::chrono::milliseconds(1600));
        clientA.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonster, 3);
        WorldNetworkEvent resp2;
        const bool got2 = WaitSkillResponse(clientA, clientA.controller.LastSkillRequestId(), resp2,
                                            3000, 1);
        WorldNetworkEvent impact2;
        const bool gotImpact = WaitUntil(
            [&] {
                clientA.DrainEvents();
                const auto& events =
                    clientA.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::SkillImpact)];
                for (auto it = events.rbegin(); it != events.rend(); ++it) {
                    if (it->skillId == kSkillIdQuickStrike && !it->impactTargets.empty() &&
                        it->impactTargets[0].entityId == 3) {
                        impact2 = *it;
                        return true;
                    }
                }
                return false;
            },
            3000);
        bool killedOk = got2 && gotImpact && impact2.impactTargets.size() == 1 &&
                        impact2.impactTargets[0].damage == 50 &&
                        impact2.impactTargets[0].killed;
        const bool gotDeath = WaitUntil(
            [&] {
                clientA.DrainEvents();
                return CountMonsterDeathsFor(clientA, 3) >= 1;
            },
            3000);
        auto monster3c = servers.world->FindMonster(3);
        killedOk = killedOk && gotDeath && monster3c != nullptr && !monster3c->Alive() &&
                   monster3c->State() == MonsterState::Dead;
        Check("SkillKillMonsterCheck: QS#2 kills slime (50 dmg after ArmorBreak), Death, Dead",
              killedOk);
        //Mana bookkeeping：90 -> 80
        // ---- 3 秒清理（指令一百二十五：复用阶段14 生命周期）----
        const bool removed = WaitUntil(
            [&] {
                clientA.DrainEvents();
                return CountMonsterRemovedDespawnsFor(clientA, 3) >= 1;
            },
            5000);
        Check("SkillKillMonsterCheck: despawn(Removed) after 3s cleanup", removed);
    }

    // ---- FireBolt 系列（指令一百零八~一百一十六/四十~四十三）----
    std::uint64_t fb1CastId = 0;
    {
        servers.world->MoveMonsterTo(4, 150.0f, 970.0f);
        const bool positioned = WaitMonsterDistance(servers, seedA.characterId, 4, 40.0f, 120.0f);
        // 注意：seenStarted 必须在发送前计数（Response/Started 可能一起到达）。
        int seenStarted = CountSkillEventsFor(clientA,
                                              WorldNetworkEvent::Type::SkillCastStartedEvent,
                                              seedA.characterId);
        // FireBoltStartCheck：accepted + Started(castTimeMs=1000)，Mana 80 -> 60
        clientA.controller.SendSkillCast(kSkillIdFireBolt, kTargetMonster, 4);
        WorldNetworkEvent resp;
        const bool got = WaitSkillResponse(clientA, clientA.controller.LastSkillRequestId(), resp,
                                           3000, 1);
        WorldNetworkEvent started;
        const bool gotStarted = got && WaitSkillStarted(clientA, seedA.characterId, seenStarted,
                                                        started);
        fb1CastId = gotStarted ? started.castId : 0;
        bool ok = got && gotStarted;
        ok = ok && started.skillId == kSkillIdFireBolt && started.castTimeMs == 1000 &&
             started.targetEntityId == 4;
        ok = ok && resp.currentMana == 60; // 80 -> 60
        auto casterA = servers.world->FindPlayerByCharacter(seedA.characterId);
        ok = ok && casterA != nullptr && casterA->IsCasting(); // 服务器进入 Casting
        if (!ok) {
            std::printf("[Diag] FBStart: got=%d gotStarted=%d skillId=%u castTimeMs=%u "
                        "target=%llu mana=%u casting=%d positioned=%d\n",
                        static_cast<int>(got), static_cast<int>(gotStarted), started.skillId,
                        started.castTimeMs, static_cast<unsigned long long>(started.targetEntityId),
                        resp.currentMana, casterA ? static_cast<int>(casterA->IsCasting()) : -1,
                        static_cast<int>(positioned));
        }
        Check("FireBoltStartCheck: accepted, Started(castTimeMs=1000), mana 80->60, casting",
              ok && positioned);

        // FireBoltNoEarlyDamageCheck（指令一百零九）：500ms 后目标 HP 不变
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto monster4 = servers.world->FindMonster(4);
        Check("FireBoltNoEarlyDamageCheck: no damage at 500ms (hp still 80)",
              monster4 != nullptr && monster4->CurrentHp() == 80);

        // AlreadyCastingCheck（指令一百一十五）：FireBolt 施法中再放 QS -> AlreadyCasting
        clientA.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonster, 4);
        WorldNetworkEvent busyResp;
        const bool gotBusy = WaitSkillResponse(clientA, clientA.controller.LastSkillRequestId(),
                                               busyResp, 3000, 0);
        bool busyOk = gotBusy && busyResp.skillResultCode ==
                                       static_cast<std::uint8_t>(SkillResultCode::AlreadyCasting);
        busyOk = busyOk && busyResp.currentMana == 60; // 不扣
        Check("AlreadyCastingCheck: second skill during cast -> AlreadyCasting", busyOk);

        // BasicAttackWhileCastingCheck（指令一百一十六/七十一）：Space 普攻 -> Busy
        clientA.client().SendAttack(9001, static_cast<std::uint8_t>(legend::world::CombatEntityType::Monster), 4);
        WorldNetworkEvent atkResp;
        const bool gotAtk = WaitAttackResponse(clientA, 9001, atkResp, 3000, 0);
        const bool atkBusy = gotAtk && atkResp.resultCode ==
                                            static_cast<std::uint8_t>(legend::world::CombatResultCode::Busy);
        Check("BasicAttackWhileCastingCheck: basic attack during cast -> Busy", atkBusy);

        // SkillMoveCancelZeroInputCheck（指令一百三十三）：方向 0,0 不取消
        for (int i = 0; i < 5; ++i) {
            clientA.client().SendMoveInput(static_cast<std::uint32_t>(7000 + i), 0.0f, 0.0f, 0.05f);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        auto casterA2 = servers.world->FindPlayerByCharacter(seedA.characterId);
        bool stillCasting = casterA2 != nullptr && casterA2->IsCasting() &&
                            casterA2->Casting().castId == fb1CastId;
        Check("SkillMoveCancelZeroInputCheck: zero-direction input does not cancel", stillCasting);

        // FireBoltMoveCancelCheck（指令一百一十一/十六/四十一）：有效移动 -> Cancelled(Moved)
        for (int i = 0; i < 3; ++i) {
            clientA.client().SendMoveInput(static_cast<std::uint32_t>(7100 + i), 1.0f, 0.0f, 0.05f);
        }
        WorldNetworkEvent cancelled;
        const bool gotCancelled = WaitSkillEventForCast(
            clientA, WorldNetworkEvent::Type::SkillCastCancelledEvent, fb1CastId, cancelled, 3000);
        bool cancelOk = gotCancelled &&
                        cancelled.cancelReason ==
                            static_cast<std::uint8_t>(SkillCancelReason::Moved);
        auto monster4b = servers.world->FindMonster(4);
        cancelOk = cancelOk && monster4b != nullptr && monster4b->CurrentHp() == 80; // 无伤害
        cancelOk = cancelOk && cancelled.skillId == kSkillIdFireBolt;
        Check("FireBoltMoveCancelCheck: move cancels cast (Moved), no damage", cancelOk);

        // Mana 不返还（指令十九/九十四）+ Cooldown 不返还（指令二十/九十四）：
        // 立即重放 FireBolt -> Cooldown（不是 NotEnoughMana——Mana 只是不退）
        clientA.controller.SendSkillCast(kSkillIdFireBolt, kTargetMonster, 4);
        WorldNetworkEvent cdResp;
        const bool gotCd = WaitSkillResponse(clientA, clientA.controller.LastSkillRequestId(),
                                             cdResp, 3000, 0);
        bool cdOk = gotCd && cdResp.skillResultCode ==
                                      static_cast<std::uint8_t>(SkillResultCode::Cooldown) &&
                    cdResp.currentMana == 60; // 保持已扣状态
        Check("FireBoltMoveCancelCheck: mana kept (no refund) + cooldown persists", cdOk);
    }

    // ---- FireBoltCompleteCheck（指令一百一十/四十）：CD 后完整施法 -> 58 伤害 ----
    {
        // FB CD 3s（从 FB#1 接受时启动）；等待可施后重试（轮询响应 accepted）
        bool accepted = false;
        WorldNetworkEvent resp;
        std::uint64_t fb2RequestId = 0;
        int seenStarted = CountSkillEventsFor(clientA,
                                              WorldNetworkEvent::Type::SkillCastStartedEvent,
                                              seedA.characterId); // 首次发送前计数
        for (int attempt = 0; attempt < 10 && !accepted; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            clientA.controller.SendSkillCast(kSkillIdFireBolt, kTargetMonster, 4);
            fb2RequestId = clientA.controller.LastSkillRequestId();
            accepted = WaitSkillResponse(clientA, fb2RequestId, resp, 300, 1);
        }
        WorldNetworkEvent started;
        const bool gotStarted = accepted && WaitSkillStarted(clientA, seedA.characterId,
                                                             seenStarted, started);
        const std::uint64_t fb2CastId = gotStarted ? started.castId : 0;
        WorldNetworkEvent completed;
        const bool gotCompleted = WaitSkillEventForCast(
            clientA, WorldNetworkEvent::Type::SkillCastCompletedEvent, fb2CastId, completed, 4000);
        WorldNetworkEvent impact;
        const bool gotImpact = WaitSkillEventForCast(clientA, WorldNetworkEvent::Type::SkillImpact,
                                                     fb2CastId, impact, 3000);
        bool ok = accepted && gotStarted && gotCompleted && gotImpact;
        ok = ok && impact.impactTargets.size() == 1 &&
             impact.impactTargets[0].damage == 58 && impact.impactTargets[0].hpAfter == 22;
        auto monster4 = servers.world->FindMonster(4);
        ok = ok && monster4 != nullptr && monster4->CurrentHp() == 22;
        ok = ok && resp.currentMana == 40; // 60 -> 40
        Check("FireBoltCompleteCheck: ~1s cast then 58 dmg 80->22, mana 60->40", ok);
    }

    // ---- A 重进世界：重置 HP（被Adjacent怪持续消耗）与 Mana 基线（不持久化）----
    {
        clientA.Disconnect();
        WaitUntil([&] { clientA.DrainEvents(); return true; }, 300);
        const std::string ticket = TicketFor(servers.login, seedA);
        const bool reentered = !ticket.empty() && clientA.ConnectAndEnter(ticket, 8000);
        Check("SkillChecks: A re-entered (hp/mana reset)", reentered);
    }

    // ---- FireBoltTargetOutOfRangeCheck（指令一百一十四/四十三）：施法中目标被移出 500 ----
    {
        bool accepted = false;
        WorldNetworkEvent resp;
        std::uint64_t fb3CastId = 0;
        int seenStarted = CountSkillEventsFor(clientA,
                                              WorldNetworkEvent::Type::SkillCastStartedEvent,
                                              seedA.characterId); // 首次发送前计数
        for (int attempt = 0; attempt < 12 && !accepted; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            clientA.controller.SendSkillCast(kSkillIdFireBolt, kTargetMonster, 4);
            const auto requestId = clientA.controller.LastSkillRequestId();
            accepted = WaitSkillResponse(clientA, requestId, resp, 300, 1);
            if (accepted) {
                WorldNetworkEvent started;
                WaitSkillStarted(clientA, seedA.characterId, seenStarted, started);
                fb3CastId = started.castId;
            }
        }
        // 施法中把目标移到 ~640（>500，<700 仍可见；leash 回家方向不缩短 A 距离）
        servers.world->MoveMonsterTo(4, 100.0f, 1640.0f);
        WorldNetworkEvent cancelled;
        const bool gotCancelled = WaitSkillEventForCast(
            clientA, WorldNetworkEvent::Type::SkillCastCancelledEvent, fb3CastId, cancelled, 5000);
        bool ok = accepted && gotCancelled &&
                  cancelled.cancelReason ==
                      static_cast<std::uint8_t>(SkillCancelReason::TargetInvalid);
        auto monster4 = servers.world->FindMonster(4);
        // 阶段16 指令十八：FB#2 命中自动施加 Burn——DOT tick 是合法服务器行为，
        // "不伤害"断言放宽为 Burn tick 后的合法值（22 -> 14 -> 6）。
        const std::uint32_t hp4 = monster4 ? monster4->CurrentHp() : 0u;
        ok = ok && monster4 != nullptr && (hp4 == 22 || hp4 == 14 || hp4 == 6);
        ok = ok && resp.currentMana == 80; // 100 -> 80（已扣，不返还）
        Check("FireBoltTargetOutOfRangeCheck: target >500 at complete -> TargetInvalid cancel", ok);
    }

    // ---- FireBoltTargetDeadCheck（指令一百一十三/四十二）：施法中目标被 B 打死 ----
    {
        // 等待 A 的 FB CD（FB#3 接受后 3s）完全过期，保证 A 的首次尝试即可被接受。
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        servers.world->MoveMonsterTo(1, 160.0f, 1020.0f);
        const bool positionedB = WaitMonsterDistance(servers, seedB.characterId, 1, 30.0f, 120.0f);
        // B QS#1：slime1 80 -> 32（B Mana 100 -> 90）
        clientB.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonster, 1);
        WorldNetworkEvent bResp;
        const bool bHit = WaitSkillResponse(clientB, clientB.controller.LastSkillRequestId(),
                                            bResp, 3000, 1);
        // B QS#2 固定在 T0+1.6s 击杀（CD 1.5s 已过）。
        // A 的 FireBolt 重试首拍在 T0+0.8s：完成时间 >= T0+1.8 > 1.6 -> 击杀必落在
        // 施法窗口内（若 CD 更晚才就绪，则目标先死、完成重验同样 TargetInvalid）。
        std::thread([&clientB]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1600));
            clientB.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonster, 1);
        }).detach();
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        bool aAccepted = false;
        WorldNetworkEvent aResp;
        std::uint64_t fb4CastId = 0;
        int seenStartedA = CountSkillEventsFor(clientA,
                                               WorldNetworkEvent::Type::SkillCastStartedEvent,
                                               seedA.characterId); // 首次发送前计数
        for (int attempt = 0; attempt < 12 && !aAccepted; ++attempt) {
            clientA.controller.SendSkillCast(kSkillIdFireBolt, kTargetMonster, 1);
            aAccepted = WaitSkillResponse(clientA, clientA.controller.LastSkillRequestId(), aResp,
                                          300, 1);
            if (aAccepted) {
                WorldNetworkEvent aStarted;
                WaitSkillStarted(clientA, seedA.characterId, seenStartedA, aStarted);
                fb4CastId = aStarted.castId;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
            }
        }
        // 等待 slime1 死亡（B 的 QS#2）
        const bool slimeDead = WaitUntil(
            [&] {
                auto monster = servers.world->FindMonster(1);
                return monster != nullptr && !monster->Alive();
            },
            5000);
        WorldNetworkEvent cancelled;
        const bool gotCancelled = WaitSkillEventForCast(
            clientA, WorldNetworkEvent::Type::SkillCastCancelledEvent, fb4CastId, cancelled, 5000);
        bool ok = positionedB && bHit && aAccepted && slimeDead && gotCancelled &&
                  cancelled.cancelReason ==
                      static_cast<std::uint8_t>(SkillCancelReason::TargetInvalid);
        // 不能重复杀：MonsterDeath 恰好 1 次
        ok = ok && CountMonsterDeathsFor(clientA, 1) == 1;
        ok = ok && aResp.currentMana == 60; // 100 -20(FB#3) -20(FB#4) = 60
        if (!ok) {
            auto monster1 = servers.world->FindMonster(1);
            std::printf("[Diag] TargetDead: positionedB=%d bHit=%d aAccepted=%d slimeDead=%d "
                        "gotCancelled=%d reason=%u mana=%u m1hp=%u deaths=%d\n",
                        static_cast<int>(positionedB), static_cast<int>(bHit),
                        static_cast<int>(aAccepted), static_cast<int>(slimeDead),
                        static_cast<int>(gotCancelled), cancelled.cancelReason,
                        aResp.currentMana,
                        monster1 ? monster1->CurrentHp() : 0u,
                        CountMonsterDeathsFor(clientA, 1));
        }
        Check("FireBoltTargetDeadCheck: target killed mid-cast -> TargetInvalid, no corpse hit, "
              "single MonsterDeath",
              ok);
    }

    // ---- CasterDisconnectDuringCastCheck（指令一百四十五）：施法中断线不 Crash 不伤害 ----
    {
        // H 延迟进入（ Fresh HP；cluster 2 怪 5~8 会在数秒内围殴致死）。
        WorldTestClient clientH;
        const bool entered = clientH.ConnectAndEnter(TicketFor(servers.login, seedH), 8000);
        servers.world->MoveMonsterTo(5, 1160.0f, 640.0f);
        const bool positioned =
            entered && WaitMonsterDistance(servers, seedH.characterId, 5, 40.0f, 120.0f);
        clientH.controller.SendSkillCast(kSkillIdFireBolt, kTargetMonster, 5);
        WorldNetworkEvent resp;
        const bool accepted = WaitSkillResponse(clientH, clientH.controller.LastSkillRequestId(),
                                                resp, 3000, 1);
        clientH.Disconnect();
        WaitUntil([&] { clientH.DrainEvents(); return true; }, 300);
        // 等 1.6s（> castTime + tick）：目标 HP 不变（断线后不得造成伤害）
        std::this_thread::sleep_for(std::chrono::milliseconds(1600));
        auto monster5 = servers.world->FindMonster(5);
        bool ok = positioned && accepted && monster5 != nullptr && monster5->CurrentHp() == 80;
        // WorldServer 继续服务其它 Client：A 仍能收到快照
        ok = ok && WaitUntil([&] {
            clientA.DrainEvents();
            return !clientA.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::PositionSnapshot)]
                        .empty();
        }, 2000);
        Check("CasterDisconnectDuringCastCheck: pending cast cleared, no damage, server alive", ok);
    }

    // ---- FireBoltDeadCancelCheck（指令一百一十二/十七）+ DeadPlayerSkillBlockedCheck
    //      （指令一百三十二/九十三）：D 进入 cluster 5 被围殴致死 ----
    {
        const bool enterD = clientD.ConnectAndEnter(TicketFor(servers.login, seedD), 8000);
        Check("SkillChecks: D entered (death scenario)", enterD);
        const bool positioned = WaitMonsterDistance(servers, seedD.characterId, 17, 20.0f, 300.0f);
        // 等怪物贴身开打（D HP < 100）——4 只怪攻击同相位（间隔 1.2s > 施法 1.0s），
        // 必须在受击后延迟 ~500ms 再施法，致死跳（下一跳 = 施法开始 +0.7s）才会
        // 落在施法窗口（+1.0s 完成）之内。
        const bool firstHit = WaitUntil(
            [&] {
                auto p = servers.world->FindPlayerByCharacter(seedD.characterId);
                return p != nullptr && p->CurrentHp() < 100;
            },
            12000);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        // D 开始 FireBolt（目标追击中的 slime17）
        clientD.controller.SendSkillCast(kSkillIdFireBolt, kTargetMonster, 17);
        WorldNetworkEvent resp;
        const bool accepted = WaitSkillResponse(clientD, clientD.controller.LastSkillRequestId(),
                                                resp, 3000, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // 施法已开始
        // 白盒扣血到 5（模拟濒死），下一跳怪物攻击致死
        auto playerD = servers.world->FindPlayerByCharacter(seedD.characterId);
        bool lowHp = false;
        if (playerD) {
            playerD->ApplyDamage(playerD->CurrentHp() - 5u);
            lowHp = playerD->CurrentHp() == 5;
        }
        const bool died = WaitUntil(
            [&] {
                auto p = servers.world->FindPlayerByCharacter(seedD.characterId);
                return p != nullptr && !p->Alive();
            },
            8000);
        WorldNetworkEvent cancelled;
        const bool gotCancelled = WaitUntil(
            [&] {
                clientD.DrainEvents();
                const auto& events = clientD.recorded[WorldTestClient::IndexOf(
                    WorldNetworkEvent::Type::SkillCastCancelledEvent)];
                for (auto it = events.rbegin(); it != events.rend(); ++it) {
                    if (it->characterId == seedD.characterId &&
                        it->cancelReason == static_cast<std::uint8_t>(SkillCancelReason::Dead)) {
                        cancelled = *it;
                        return true;
                    }
                }
                return false;
            },
            5000);
        auto monster17 = servers.world->FindMonster(17);
        bool ok = positioned && firstHit && accepted && lowHp && died && gotCancelled &&
                  cancelled.skillId == kSkillIdFireBolt;
        ok = ok && monster17 != nullptr && monster17->CurrentHp() == 80; // 无技能伤害
        if (!ok) {
            std::printf("[Diag] DeadCancel: positioned=%d firstHit=%d accepted=%d lowHp=%d "
                        "died=%d gotCancelled=%d skillId=%u m17hp=%u\n",
                        static_cast<int>(positioned), static_cast<int>(firstHit),
                        static_cast<int>(accepted), static_cast<int>(lowHp),
                        static_cast<int>(died), static_cast<int>(gotCancelled),
                        cancelled.skillId, monster17 ? monster17->CurrentHp() : 0u);
        }
        Check("FireBoltDeadCancelCheck: caster death cancels cast (Dead), no skill damage", ok);

        // 死亡玩家施法 -> CasterDead（Mana 不扣）
        clientD.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonster, 17);
        WorldNetworkEvent deadResp;
        const bool gotDead = WaitSkillResponse(clientD, clientD.controller.LastSkillRequestId(),
                                               deadResp, 3000, 0);
        bool deadOk = gotDead && deadResp.skillResultCode ==
                                          static_cast<std::uint8_t>(SkillResultCode::CasterDead);
        deadOk = deadOk && deadResp.currentMana == 80; // FireBolt 已扣 20，之后不变
        Check("DeadPlayerSkillBlockedCheck: dead player -> CasterDead, mana unchanged", deadOk);
        clientD.Disconnect();
        WaitUntil([&] { clientD.DrainEvents(); return true; }, 300);
    }

    // ---- SkillCastSpamCheck（指令一百四十）：100 个 FireBolt 只有一个进入 Casting ----
    {
        // 先排空 B 的积压事件（前置检查的 Started 可能仍在其网络队列中），
        // 否则 startedDelta 基线污染。
        clientB.DrainEvents();
        servers.world->MoveMonsterTo(2, 160.0f, 1080.0f);
        const bool positionedB = WaitMonsterDistance(servers, seedB.characterId, 2, 30.0f, 120.0f);
        const int startedBefore = CountSkillEventsFor(clientB,
                                                      WorldNetworkEvent::Type::SkillCastStartedEvent,
                                                      seedB.characterId);
        for (int i = 0; i < 100; ++i) {
            clientB.controller.SendSkillCast(kSkillIdFireBolt, kTargetMonster, 2);
        }
        // 收第一个 Response（accepted）+ 等 cast 完成影响
        bool firstAccepted = false;
        int acceptedCount = 0;
        int rejectedCasting = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        while (std::chrono::steady_clock::now() < deadline &&
               (acceptedCount == 0 || rejectedCasting < 99)) {
            clientB.DrainEvents();
            const auto& responses = clientB.recorded[WorldTestClient::IndexOf(
                WorldNetworkEvent::Type::SkillCastResponseEvent)];
            acceptedCount = 0;
            rejectedCasting = 0;
            for (const auto& e : responses) {
                if (e.skillId != kSkillIdFireBolt) {
                    continue;
                }
                if (e.accepted) {
                    ++acceptedCount;
                } else if (e.skillResultCode ==
                           static_cast<std::uint8_t>(SkillResultCode::AlreadyCasting)) {
                    ++rejectedCasting;
                }
            }
            if (acceptedCount >= 1 && rejectedCasting >= 99) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // 等 cast 完成（~1.2s）后 B 收到恰好 1 份新 Started
        std::this_thread::sleep_for(std::chrono::milliseconds(1400));
        const int startedAfter = CountSkillEventsFor(clientB,
                                                     WorldNetworkEvent::Type::SkillCastStartedEvent,
                                                     seedB.characterId);
        auto monster2 = servers.world->FindMonster(2);
        bool ok = positionedB && acceptedCount == 1 && rejectedCasting >= 90 &&
                  (startedAfter - startedBefore) == 1;
        ok = ok && monster2 != nullptr && monster2->CurrentHp() == 22; // 80 - 58
        if (!ok) {
            std::printf("[Diag] Spam: positionedB=%d accepted=%d rejected=%d startedDelta=%d "
                        "m2hp=%u\n",
                        static_cast<int>(positionedB), acceptedCount, rejectedCasting,
                        startedAfter - startedBefore,
                        monster2 ? monster2->CurrentHp() : 0u);
        }
        Check("SkillCastSpamCheck: 100 FireBolt -> exactly 1 casting, 58 dmg applied once", ok);
    }

    // ---- NotEnoughManaCheck（指令一百零六/八十九）：白盒耗蓝后施法拒绝 ----
    {
        auto playerB = servers.world->FindPlayerByCharacter(seedB.characterId);
        bool drained = false;
        if (playerB) {
            // B Mana 60（Spam 用掉 20）-> 再耗 55 -> 5
            drained = playerB->ConsumeMana(55) && playerB->CurrentMana() == 5;
        }
        clientB.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonster, 2);
        WorldNetworkEvent resp;
        const bool got = WaitSkillResponse(clientB, clientB.controller.LastSkillRequestId(), resp,
                                           3000, 0);
        bool ok = drained && got &&
                  resp.skillResultCode == static_cast<std::uint8_t>(SkillResultCode::NotEnoughMana);
        ok = ok && resp.currentMana == 5;
        auto monster2 = servers.world->FindMonster(2);
        ok = ok && monster2 != nullptr && monster2->CurrentHp() == 22; // 无伤害
        Check("NotEnoughManaCheck: mana<cost -> NotEnoughMana, no damage, mana kept", ok);
    }

    // ---- SkillRequestSpamCheck（指令一百三十九/七十四）：100 个 QuickStrike 受 CD 限 ----
    {
        // A Mana = 60：刷 100 个 QS -> 仅第 1 个被接受（60->50），其余 99 个在 1.5s CD
        // 内到达 -> Cooldown。恰一次伤害（48 >= 22 -> slime2 被击杀）；Mana 不为负。
        // 阶段19：ManaSnapshot 以刷包前基线过滤（否则 events.back() 可能取到刷包前
        // mana=60 的旧快照——CI 慢机上为真实竞态）。
        const int deathsBefore = CountMonsterDeathsFor(clientA, 2);
        const std::size_t manaBaseline = clientA.recorded[WorldTestClient::IndexOf(
            WorldNetworkEvent::Type::ManaSnapshot)].size();
        for (int i = 0; i < 100; ++i) {
            clientA.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonster, 2);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        clientA.DrainEvents(); // 排空后再统计 MonsterDeath（事件可能仍在网络队列）
        auto monster2 = servers.world->FindMonster(2);
        const int deathsAfter = CountMonsterDeathsFor(clientA, 2);
        bool ok = monster2 != nullptr && !monster2->Alive() && monster2->CurrentHp() == 0;
        ok = ok && deathsAfter == deathsBefore + 1; // 恰一次伤害（一次击杀）
        // Mana 不为负：基线之后的 ManaSnapshot（A Mana = 50，恰好一次扣费）。
        WorldNetworkEvent snap;
        const bool gotSnap = WaitUntil(
            [&] {
                clientA.DrainEvents();
                const auto& events =
                    clientA.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::ManaSnapshot)];
                for (std::size_t i = manaBaseline; i < events.size(); ++i) {
                    if (events[i].currentMana == 50) {
                        snap = events[i];
                        return true;
                    }
                }
                return false;
            },
            3000);
        ok = ok && gotSnap && snap.currentMana == 50; // 60 - 10，不为负
        if (!ok) {
            auto monster2b = servers.world->FindMonster(2);
            std::printf("[Diag] RequestSpam: gotSnap=%d snapMana=%u m2alive=%d m2hp=%u "
                        "deathsBefore=%d deathsAfter=%d manaBaseline=%zu\n",
                        static_cast<int>(gotSnap), snap.currentMana,
                        monster2b ? static_cast<int>(monster2b->Alive()) : -1,
                        monster2b ? monster2b->CurrentHp() : 0u, deathsBefore, deathsAfter,
                        manaBaseline);
        }
        Check("SkillRequestSpamCheck: 100 requests rate-limited by 1.5s cd -> exactly 1 hit, "
              "mana 60->50 (no underflow)",
              ok);
    }

    // ---- ManaSnapshotCheck + ManaNoRegenCheck（指令一百三十/一百三十一/六十七/六十八）----
    {
        const std::size_t before = clientA.recorded[WorldTestClient::IndexOf(
            WorldNetworkEvent::Type::ManaSnapshot)].size();
        std::this_thread::sleep_for(std::chrono::milliseconds(2300));
        clientA.DrainEvents();
        const auto& events =
            clientA.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::ManaSnapshot)];
        bool ok = events.size() - before >= 2; // 2.3s 内至少 2 份（1s 周期）
        bool constant = true;
        for (std::size_t i = before; i < events.size(); ++i) {
            if (events[i].currentMana != 50 || events[i].maxManaVal != 100) {
                constant = false; // 无 Regen（Mana 恒 50/100）
            }
        }
        Check("ManaSnapshotCheck: ~1s snapshots to owner with correct value", ok && constant);
        Check("ManaNoRegenCheck: mana never regenerates", ok && constant);
    }

    // ---- CasterReconnectCheck（指令一百四十六）：重进世界 Mana=100、CD 清空 ----
    {
        clientA.Disconnect();
        WaitUntil([&] { clientA.DrainEvents(); return true; }, 300);
        const std::string ticket = TicketFor(servers.login, seedA);
        const bool reentered = !ticket.empty() && clientA.ConnectAndEnter(ticket, 8000);
        bool ok = reentered;
        ok = ok && clientA.LastEnterSuccess().currentMana == 100 &&
             clientA.LastEnterSuccess().maxManaVal == 100; // Mana 不持久化
        auto casterA = servers.world->FindPlayerByCharacter(seedA.characterId);
        ok = ok && casterA != nullptr && !casterA->IsCasting(); // 施法状态清空
        // CD 清空：QS 立即可用（阶段16 指令十八：FB#2 给 slime4 施加了 Burn，其死于
        // DOT 并被 3s 清理移除——改用未被动过的 slime16 验证 accepted）。
        servers.world->MoveMonsterTo(16, 140.0f, 980.0f);
        const bool positioned = WaitMonsterDistance(servers, seedA.characterId, 16, 20.0f, 120.0f);
        clientA.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonster, 16);
        WorldNetworkEvent resp;
        const bool accepted = WaitSkillResponse(clientA, clientA.controller.LastSkillRequestId(),
                                                resp, 3000, 1);
        ok = ok && positioned && accepted; // 冷却已清空（否则 Cooldown）
        Check("CasterReconnectCheck: re-enter resets mana=100, casting=false, cooldowns cleared",
              ok);
    }

    clientA.Disconnect();
    clientB.Disconnect();
    clientC.Disconnect();
    WaitUntil([&] { return true; }, 300);
    servers.StopAll();
}

// ===========================================================================
// B2. Whirlwind 场景（单体/多目标/CD 独立性/多杀）
// ===========================================================================

void RunWhirlwindChecks() {
    WorldTestServers servers;
    servers.dbPath = TempDbPath("world_skill_whirl");
    RemoveDb(servers.dbPath);
    Check("WhirlwindServersStartCheck", servers.StartLogin() && servers.StartWorld());

    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(4, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("WhirlwindChecks: db ready", false);
        servers.StopAll();
        return;
    }
    CharacterSeed seedW;
    CharacterSeed seedW2;
    CharacterSeed seedW3;
    bool seeded = true;
    seeded = seeded && SeedAt(db, accounts, characters, "sk_user_w", "SkW", 1050.0f, 550.0f, seedW);
    seeded = seeded && SeedAt(db, accounts, characters, "sk_user_w2", "SkW2", 1590.0f, 1550.0f, seedW2);
    seeded = seeded && SeedAt(db, accounts, characters, "sk_user_w3", "SkW3", 1610.0f, 1530.0f, seedW3);
    Check("WhirlwindChecks: seeds ready", seeded);

    // W2/W3 延迟进入（cluster 5 被围殴致死前才进入各自场景）
    WorldTestClient clientW;
    {
        const bool e1 = clientW.ConnectAndEnter(TicketFor(servers.login, seedW), 8000);
        Check("WhirlwindChecks: W entered", e1);
    }

    // ---- WhirlwindSingleCheck（指令一百一十七）：范围内 1 只 -> 43 伤害 ----
    {
        // W 出生点半径 160 内还有 slime5(71)/slime7(158) 的 spawn——先移出半径
        //（置于 aggro 350 外），保证施法窗口内 Impact 恰 1 目标（slime6 为目标）。
        servers.world->MoveMonsterTo(5, 1000.0f, 1000.0f);
        servers.world->MoveMonsterTo(7, 1300.0f, 800.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        servers.world->MoveMonsterTo(6, 1110.0f, 550.0f);
        const bool positioned = WaitMonsterDistance(servers, seedW.characterId, 6, 40.0f, 120.0f);
        clientW.controller.SendSkillCast(kSkillIdWhirlwind, kTargetSelf, 0);
        WorldNetworkEvent resp;
        const bool accepted = WaitSkillResponse(clientW, clientW.controller.LastSkillRequestId(),
                                                resp, 3000, 1);
        WorldNetworkEvent started;
        int seenStarted = 0;
        const bool gotStarted = accepted && WaitSkillStarted(clientW, seedW.characterId,
                                                             seenStarted, started);
        const std::uint64_t castId = gotStarted ? started.castId : 0;
        WorldNetworkEvent impact;
        const bool gotImpact = WaitSkillEventForCast(clientW, WorldNetworkEvent::Type::SkillImpact,
                                                     castId, impact, 3000);
        bool ok = positioned && accepted && gotStarted && gotImpact;
        ok = ok && started.skillId == kSkillIdWhirlwind && started.castTimeMs == 0; // Instant
        ok = ok && impact.impactTargets.size() == 1 &&
             impact.impactTargets[0].entityId == 6 && impact.impactTargets[0].damage == 43 &&
             impact.impactTargets[0].hpAfter == 37 && impact.impactTargets[0].maxHp == 80;
        auto monster6 = servers.world->FindMonster(6);
        ok = ok && monster6 != nullptr && monster6->CurrentHp() == 37;
        ok = ok && resp.currentMana == 75; // 100 -> 25 消耗
        // SkillCombatEventSourceCheck（指令一百二十四）：sourceType=Skill, sourceId=1003
        WorldNetworkEvent combat;
        int seenCombat = 0;
        const bool gotCombat = WaitSkillCombatEvent(clientW, seedW.characterId,
                                                    kSkillIdWhirlwind, seenCombat, combat);
        ok = ok && gotCombat && combat.sourceType == kSourceSkill &&
             combat.sourceId == kSkillIdWhirlwind && combat.targetId == 6;
        // 无目标玩家伤害（W 自身 HP 只受怪物攻击，技能不打玩家——指令十/四十八）
        Check("WhirlwindSingleCheck: 1 slime in range takes 43 (80->37), CombatEvent(Skill,1003)",
              ok);
    }

    // ---- SkillCooldownIndependentCheck（指令一百四十二）：Whirlwind CD 中 FireBolt 可用 ----
    {
        servers.world->MoveMonsterTo(7, 1100.0f, 570.0f);
        const bool positioned = WaitMonsterDistance(servers, seedW.characterId, 7, 40.0f, 500.0f);
        // W 的 Whirlwind 刚放（CD 5s 未过）——FireBolt（独立 CD）应立即被接受
        clientW.controller.SendSkillCast(kSkillIdFireBolt, kTargetMonster, 7);
        WorldNetworkEvent resp;
        const bool accepted = WaitSkillResponse(clientW, clientW.controller.LastSkillRequestId(),
                                                resp, 3000, 1);
        bool ok = positioned && accepted;
        ok = ok && resp.currentMana == 55; // 75 -> 55（FireBolt 消耗 20）
        Check("SkillCooldownIndependentCheck: FireBolt usable while Whirlwind on cooldown", ok);

        // 等 FireBolt 完成 -> slime7 80-58=22
        const bool hitDone = WaitUntil(
            [&] {
                auto monster = servers.world->FindMonster(7);
                return monster != nullptr && monster->CurrentHp() == 22;
            },
            4000);

        // ---- SkillAndBasicAttackCooldownIndependentCheck（指令一百四十三/七十一）：
        //      施法结束后普攻立即可用（技能 CD 不改普攻 CD；Casting 才阻止普攻）----
        clientW.client().SendAttack(9101,
                                    static_cast<std::uint8_t>(legend::world::CombatEntityType::Monster),
                                    7);
        WorldNetworkEvent atkResp;
        const bool atkOk = WaitAttackResponse(clientW, 9101, atkResp, 3000, 1);
        auto monster7 = servers.world->FindMonster(7);
        bool ok2 = hitDone && atkOk; // attack accepted（未被技能 CD 阻断）
        ok2 = ok2 && monster7 != nullptr && monster7->CurrentHp() == 4; // 22 - 18 普攻
        Check("SkillAndBasicAttackCooldownIndependentCheck: basic attack works after cast "
              "(18 dmg 22->4)",
              ok2);
    }

    // ---- WhirlwindMultiTargetCheck（指令一百一十八）：5 只各 43 伤害 + Impact count=5 ----
    // W2/W3 先后进入（W2 承担仇恨；W3 必须在 W2 被围殴致死前完成 MultiKill——
    // W2 死亡会导致怪物 Returning 离开 W3 的 AOE 半径）。
    WorldTestClient clientW2;
    {
        const bool e2 = clientW2.ConnectAndEnter(TicketFor(servers.login, seedW2), 8000);
        Check("WhirlwindChecks: W2 entered", e2);
    }
    WorldTestClient clientW3;
    {
        const bool e3 = clientW3.ConnectAndEnter(TicketFor(servers.login, seedW3), 8000);
        Check("WhirlwindChecks: W3 entered", e3);
    }
    {
        // cluster 5：slime17~20（spawn 固定）+ slime13 移入
        servers.world->MoveMonsterTo(13, 1560.0f, 1560.0f);
        const bool converged =
            WaitAllMonstersWithin(servers, seedW2.characterId, {13, 17, 18, 19, 20}, 160.0f);
        clientW2.controller.SendSkillCast(kSkillIdWhirlwind, kTargetSelf, 0);
        WorldNetworkEvent resp;
        const bool accepted = WaitSkillResponse(clientW2, clientW2.controller.LastSkillRequestId(),
                                                resp, 3000, 1);
        WorldNetworkEvent started;
        int seenStarted = 0;
        const bool gotStarted = accepted && WaitSkillStarted(clientW2, seedW2.characterId,
                                                             seenStarted, started);
        const std::uint64_t castId = gotStarted ? started.castId : 0;
        WorldNetworkEvent impact;
        const bool gotImpact = WaitSkillEventForCast(clientW2, WorldNetworkEvent::Type::SkillImpact,
                                                     castId, impact, 3000);
        bool ok = converged && accepted && gotStarted && gotImpact;
        ok = ok && impact.impactTargets.size() == 5;
        if (ok) {
            for (const auto& t : impact.impactTargets) {
                ok = ok && t.damage == 43 && t.hpAfter == 37 && t.maxHp == 80;
            }
        }
        // 服务器侧 5 只全部 37
        for (const auto id : {13u, 17u, 18u, 19u, 20u}) {
            auto monster = servers.world->FindMonster(id);
            ok = ok && monster != nullptr && monster->CurrentHp() == 37;
        }
        ok = ok && resp.currentMana == 75;
        // 每目标独立 CombatEvent（5 条，sourceId=1003）
        const int combatCount = CountSkillCombatEventsFor(clientW2, seedW2.characterId, 0);
        ok = ok && combatCount >= 5;
        Check("WhirlwindMultiTargetCheck: 5 monsters each 43 dmg, Impact count=5, "
              "independent CombatEvents",
              ok);
    }

    // ---- MultiKillWhirlwindCheck（指令一百二十六/八十四）：W3 的 Whirlwind 同时杀 5 ----
    {
        // 等 5 只残血怪收敛到 W3 的 100 范围内（追击 W2 中，停止距离 ~60），
        // 保证一次 AOE 全中；此时 W2 仍存活、怪物仍处 Chase，不会 Returning。
        const bool converged =
            WaitAllMonstersWithin(servers, seedW3.characterId, {13, 17, 18, 19, 20}, 100.0f);
        // 5 只 37 HP <= 43 -> 全部击杀
        clientW3.controller.SendSkillCast(kSkillIdWhirlwind, kTargetSelf, 0);
        WorldNetworkEvent resp;
        const bool accepted =
            converged && WaitSkillResponse(clientW3, clientW3.controller.LastSkillRequestId(),
                                           resp, 3000, 1);
        int deaths = 0;
        const bool allDead = WaitUntil(
            [&] {
                deaths = 0;
                for (const auto id : {13u, 17u, 18u, 19u, 20u}) {
                    auto monster = servers.world->FindMonster(id);
                    if (monster != nullptr && !monster->Alive()) {
                        ++deaths;
                    }
                }
                return deaths == 5;
            },
            5000);
        bool ok = accepted && allDead;
        // 各自独立 MonsterDeath（>=5 条事件；W3 必在观察者内）
        int deathEvents = 0;
        for (const auto id : {13u, 17u, 18u, 19u, 20u}) {
            deathEvents += CountMonsterDeathsFor(clientW3, id);
        }
        ok = ok && deathEvents >= 5;
        // 不 Crash：服务器继续服务
        ok = ok && WaitUntil([&] { clientW3.DrainEvents(); return true; }, 500);
        Check("MultiKillWhirlwindCheck: 5 simultaneous kills, independent MonsterDeaths, no crash",
              ok);
    }

    clientW.Disconnect();
    clientW2.Disconnect();
    clientW3.Disconnect();
    WaitUntil([&] { return true; }, 300);
    servers.StopAll();
}

// ===========================================================================
// B3. Whirlwind 上限/排序/死怪跳过（20 只满编独立 servers）
// ===========================================================================

void RunWhirlwindMaxTargetChecks() {
    WorldTestServers servers;
    servers.dbPath = TempDbPath("world_skill_maxtarget");
    RemoveDb(servers.dbPath);
    Check("MaxTargetServersStartCheck", servers.StartLogin() && servers.StartWorld());

    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(2, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("MaxTargetChecks: db ready", false);
        servers.StopAll();
        return;
    }
    CharacterSeed seedM;
    if (!SeedAt(db, accounts, characters, "sk_user_m", "SkM", 1550.0f, 1550.0f, seedM)) {
        Check("MaxTargetChecks: seed ready", false);
        servers.StopAll();
        return;
    }
    WorldTestClient clientM;
    const bool entered = clientM.ConnectAndEnter(TicketFor(servers.login, seedM), 8000);
    Check("MaxTargetChecks: M entered", entered);

    // ---- WhirlwindMaxTargetCheck（指令一百一十九/八十五）+ WhirlwindStableTieCheck
    //      （指令一百二十二/四十五）：20 只同扇区排布，距离随 entityId 严格递增
    //      且全部 > attackRange(60) -> AI 同速同向位移保序，命中 = entityId 最小 16 只 ----
    {
        // 距离 70..153（步长 ~4.37，全部 <=160 且 >60）：id 越小越近。
        for (int i = 0; i < 20; ++i) {
            const float angle = static_cast<float>(i) * 0.31415926535f; // 18° 步进
            const float dist = 70.0f + static_cast<float>(i) * (83.0f / 19.0f);
            const float x = 1550.0f + dist * std::cos(angle);
            const float y = 1550.0f + dist * std::sin(angle);
            servers.world->MoveMonsterTo(static_cast<std::uint64_t>(i + 1), x, y);
        }
        // 50ms < AI tick 200ms：布景已生效且最多一次等量位移（保序）。
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        clientM.controller.SendSkillCast(kSkillIdWhirlwind, kTargetSelf, 0);
        WorldNetworkEvent resp;
        const bool accepted = WaitSkillResponse(clientM, clientM.controller.LastSkillRequestId(),
                                                resp, 3000, 1);
        WorldNetworkEvent started;
        int seenStarted = 0;
        const bool gotStarted = accepted && WaitSkillStarted(clientM, seedM.characterId,
                                                             seenStarted, started);
        const std::uint64_t castId = gotStarted ? started.castId : 0;
        WorldNetworkEvent impact;
        const bool gotImpact = WaitSkillEventForCast(clientM, WorldNetworkEvent::Type::SkillImpact,
                                                     castId, impact, 3000);
        bool ok = accepted && gotStarted && gotImpact && impact.impactTargets.size() == 16;
        std::set<std::uint64_t> hitIds;
        if (ok) {
            for (const auto& t : impact.impactTargets) {
                hitIds.insert(t.entityId);
            }
            // 距离并列 -> entityId 升序：命中 1..16，排除 17..20
            for (std::uint64_t id = 1; id <= 16; ++id) {
                ok = ok && hitIds.count(id) == 1;
            }
            for (std::uint64_t id = 17; id <= 20; ++id) {
                ok = ok && hitIds.count(id) == 0;
            }
        }
        Check("WhirlwindMaxTargetCheck: 20 equidistant -> 16 nearest (entityId tie-break), "
              "16-target cap",
              ok);
    }

    // ---- WhirlwindDeadSkipCheck（指令一百二十二/四十七）：死亡怪不进目标集合 ----
    {
        // 白盒置死 slime2（alive=false）；若 3s 清理已先行移除则视为同样满足
        //（死亡怪绝不进目标集合）。M 被围殴消耗严重 -> 断线重进重置 HP/Mana/CD
        //（不持久化边界），随后立即施法（无需等 5s CD）。
        auto slime2 = servers.world->FindMonster(2);
        bool killed = false;
        if (slime2) {
            slime2->ApplyDamage(slime2->CurrentHp());
            killed = !slime2->Alive();
        } else {
            killed = true; // 已被 3s 清理移除（stale deadSince），同样不会进目标集合
        }
        clientM.Disconnect();
        WaitUntil([&] { clientM.DrainEvents(); return true; }, 300);
        const std::string ticket = TicketFor(servers.login, seedM);
        const bool reentered = !ticket.empty() && clientM.ConnectAndEnter(ticket, 8000);
        // 重新排布：存活怪（slime2 已死不布景）距离随 id 严格递增（70..148，全 <=160
        // 且 >60）-> 命中 = 除 2 外的最小 16 只（18/19/20 被上限截断）。
        int slot = 0;
        for (int i = 0; i < 20; ++i) {
            const std::uint64_t id = static_cast<std::uint64_t>(i + 1);
            if (id == 2) {
                continue;
            }
            const float angle = static_cast<float>(slot) * 0.31415926535f;
            const float dist = 70.0f + static_cast<float>(slot) * (78.0f / 18.0f);
            servers.world->MoveMonsterTo(id, 1550.0f + dist * std::cos(angle),
                                         1550.0f + dist * std::sin(angle));
            ++slot;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        int seenStarted = CountSkillEventsFor(clientM,
                                              WorldNetworkEvent::Type::SkillCastStartedEvent,
                                              seedM.characterId); // 发送前计数
        clientM.controller.SendSkillCast(kSkillIdWhirlwind, kTargetSelf, 0);
        WorldNetworkEvent resp;
        const bool accepted =
            reentered && WaitSkillResponse(clientM, clientM.controller.LastSkillRequestId(), resp,
                                           3000, 1);
        WorldNetworkEvent started;
        const bool gotStarted = accepted && WaitSkillStarted(clientM, seedM.characterId,
                                                             seenStarted, started);
        const std::uint64_t castId = gotStarted ? started.castId : 0;
        WorldNetworkEvent impact;
        const bool gotImpact = WaitSkillEventForCast(clientM, WorldNetworkEvent::Type::SkillImpact,
                                                     castId, impact, 3000);
        bool ok = killed && accepted && gotStarted && gotImpact;
        std::set<std::uint64_t> hitIds;
        if (ok) {
            for (const auto& t : impact.impactTargets) {
                hitIds.insert(t.entityId);
            }
        }
        ok = ok && hitIds.size() == 16 && hitIds.count(2) == 0; // 死怪被跳过
        ok = ok && hitIds.count(1) == 1 && hitIds.count(3) == 1; // 顺位补入 1,3,4...
        Check("WhirlwindDeadSkipCheck: dead monster excluded from target set", ok);
    }

    clientM.Disconnect();
    WaitUntil([&] { return true; }, 300);
    servers.StopAll();
}

// ===========================================================================
// B4. 持久化边界（指令一百四十七）：重启 Mana 恢复 / CD 清空 / Pending 消失
// ===========================================================================

void RunSkillPersistenceChecks() {
    std::string dbPath;
    CharacterSeed seedR;
    {
        WorldTestServers servers;
        servers.dbPath = TempDbPath("world_skill_persist");
        dbPath = servers.dbPath;
        RemoveDb(servers.dbPath);
        Check("PersistServersStartCheck", servers.StartLogin() && servers.StartWorld());
        Database db;
        std::string error;
        const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
        AccountService accounts(2, 60);
        CharacterService characters(account::kMaxCharactersPerAccount);
        if (!dbOk || !SeedAt(db, accounts, characters, "sk_user_r", "SkR", 600.0f, 600.0f, seedR)) {
            Check("SkillPersistenceBoundaryCheck: seed ready", false);
            servers.StopAll();
            return;
        }
        WorldTestClient clientR;
        const bool entered = clientR.ConnectAndEnter(TicketFor(servers.login, seedR), 8000);
        // 靠近 slime1 -> QS 消耗 Mana（100 -> 90）+ 启动 CD
        servers.world->MoveMonsterTo(1, 640.0f, 580.0f);
        const bool positioned = WaitMonsterDistance(servers, seedR.characterId, 1, 20.0f, 120.0f);
        clientR.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonster, 1);
        WorldNetworkEvent resp;
        const bool accepted = WaitSkillResponse(clientR, clientR.controller.LastSkillRequestId(),
                                                resp, 3000, 1);
        Check("SkillPersistenceBoundaryCheck: first session cast (mana 90)",
              entered && positioned && accepted && resp.currentMana == 90);
        clientR.Disconnect();
        WaitUntil([&] { return true; }, 300);
        servers.StopAll(); // WorldServer 重启（runtime only 状态全丢）
    }
    {
        WorldTestServers servers; // 全新 services（模拟重启后的进程）
        servers.dbPath = dbPath;  // 同一 DB（位置/账号持久化，战斗状态不持久化）
        Check("PersistServersRestartCheck", servers.StartLogin() && servers.StartWorld());
        WorldTestClient clientR2;
        const bool reentered = clientR2.ConnectAndEnter(TicketFor(servers.login, seedR), 8000);
        bool ok = reentered;
        ok = ok && clientR2.LastEnterSuccess().currentMana == 100; // Mana 恢复 100
        auto playerR = servers.world->FindPlayerByCharacter(seedR.characterId);
        ok = ok && playerR != nullptr && !playerR->IsCasting(); // Pending Cast 消失
        // CD 清空：QS 立即成功
        servers.world->MoveMonsterTo(1, 640.0f, 580.0f);
        const bool positioned = WaitMonsterDistance(servers, seedR.characterId, 1, 20.0f, 120.0f);
        clientR2.controller.SendSkillCast(kSkillIdQuickStrike, kTargetMonster, 1);
        WorldNetworkEvent resp;
        const bool accepted = WaitSkillResponse(clientR2, clientR2.controller.LastSkillRequestId(),
                                                resp, 3000, 1);
        ok = ok && positioned && accepted && resp.currentMana == 90;
        Check("SkillPersistenceBoundaryCheck: restart restores mana=100, clears cds/casts", ok);
        clientR2.Disconnect();
        WaitUntil([&] { return true; }, 300);
        servers.StopAll();
    }
}

} // namespace

// ---------------------------------------------------------------------------
// 阶段15 入口（WorldChecks.cpp main 调用）。
// ---------------------------------------------------------------------------
void RunWorldSkillChecks() {
    // A. 纯逻辑
    RunSkillLogicChecks();
    // B1. 主场景真实链路
    RunSkillChainChecksMain();
    // B2. Whirlwind 场景
    RunWhirlwindChecks();
    // B3. 上限/稳定序/死怪跳过
    RunWhirlwindMaxTargetChecks();
    // B4. 持久化边界
    RunSkillPersistenceChecks();
}

} // namespace worldtest
