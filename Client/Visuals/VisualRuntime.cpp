#include "Client/Visuals/VisualRuntime.h"

#include "Client/WorldNetwork/RemoteMonsterManager.h"
#include "Client/WorldNetwork/RemotePlayerManager.h"
#include "Client/WorldNetwork/WorldClientController.h"
#include "Client/WorldNetwork/WorldNetworkClient.h"
#include "Engine/Core/Engine.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Entity/Direction8.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Render/Camera2D.h"
#include "Engine/Render/Shader.h"
#include "Shared/Combat/CombatTypes.h"
#include "Shared/Skill/SkillTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace legend::client {

namespace {

using legend::math::Color;
using legend::math::Vector2;

constexpr float kLocalSmoothRate = 12.0f;   // 1-exp(-k*dt)，与 RemotePlayerEntity 一致
constexpr float kLocalSnapDist = 300.0f;    // 超过直接 snap（传送）
constexpr float kProjectileSpeed = 420.0f;  // 视觉飞行速度（world units/s）
constexpr float kProjectileMaxLife = 2.5f;
constexpr float kWhirlwindMaxLife = 1.5f;

// CombatEntityType
constexpr std::uint8_t kEntityTypePlayer = 1;
constexpr std::uint8_t kEntityTypeMonster = 2;

// Quest 展示色
const Color kHpFill(0.86f, 0.22f, 0.22f, 0.95f);
const Color kHpFillLow(0.95f, 0.55f, 0.15f, 0.95f);
const Color kManaFill(0.25f, 0.50f, 0.95f, 0.95f);
const Color kBarBack(0.05f, 0.05f, 0.08f, 0.85f);
const Color kPanelBack(0.08f, 0.09f, 0.13f, 0.62f);
const Color kTextWhite(0.96f, 0.96f, 0.96f, 1.0f);
const Color kTextGold(1.0f, 0.85f, 0.25f, 1.0f);
const Color kTextGray(0.62f, 0.65f, 0.70f, 1.0f);

} // namespace

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------

bool VisualRuntime::Initialize(legend::resource::ResourceManager& resources,
                               legend::render::Shader& spriteShader,
                               const std::string& dataRootOverride) {
    m_resources = &resources;
    m_spriteShader = &spriteShader;

    std::string dataRoot = dataRootOverride;
    if (dataRoot.empty()) {
        dataRoot = legend::visual::VisualDataCatalog::FindDataRoot();
    }
    if (dataRoot.empty()) {
        LOG_WARN("[VisualRuntime] Data root not found — visual runtime disabled (fallback).");
        return false;
    }

    m_catalog = std::make_unique<legend::visual::VisualDataCatalog>();
    std::string error;
    if (!m_catalog->Load(dataRoot, error)) {
        LOG_ERROR("[VisualRuntime] visual data load failed: " + error);
        m_catalog.reset();
        return false;
    }

    m_assets.Initialize(resources, m_catalog->Manifest());
    if (!m_text.Initialize(std::string(), 32)) {
        LOG_WARN("[VisualRuntime] text renderer unavailable — nameplates/HUD text disabled.");
    }
    if (!m_uiBatch.Initialize(4096)) {
        LOG_ERROR("[VisualRuntime] ui batch init failed.");
        return false;
    }
    m_whiteTexture =
        resources.CreateSolidTexture("internal/vr_white", 64, Color(1.0f, 1.0f, 1.0f, 1.0f));

    m_ready = true;
    LOG_INFO("[VisualRuntime] ready. assets=" + std::to_string(m_catalog->Manifest().assets.size()) +
             " clips=" + std::to_string(m_catalog->Animations().clips.size()) +
             " entities=" + std::to_string(m_catalog->Entities().entities.size()) +
             " effects=" + std::to_string(m_catalog->Effects().effects.size()) +
             " mapVisuals=" + std::to_string(m_catalog->MapVisuals().size()) +
             " font=" + m_text.FontSource());
    // 阶段24 指令四十七：Client Smoke 里程碑标记（CI/本地冒烟 grep 用）。
    LOG_INFO("[VisualSmoke] asset-manifest-loaded assets=" +
             std::to_string(m_catalog->Manifest().assets.size()));
    LOG_INFO("[VisualSmoke] default-texture-ready");
    return true;
}

void VisualRuntime::Shutdown() {
    m_uiBatch.Shutdown();
    m_text.Shutdown();
    m_effects.clear();
    m_damageNumbers.clear();
    m_playerVisuals.clear();
    m_monsterVisuals.clear();
    m_npcVisuals.clear();
    m_portalVisuals.clear();
    m_catalog.reset();
    m_ready = false;
}

// ---------------------------------------------------------------------------
// 事件钩子（只读消费；指令十三/二十四：Client 只做表现）
// ---------------------------------------------------------------------------

void VisualRuntime::ClearTransientVisuals() {
    m_effects.clear();
    m_damageNumbers.clear();
    m_playerVisuals.clear();
    m_monsterVisuals.clear();
    m_npcVisuals.clear();
    m_portalVisuals.clear();
    m_localHasServerPos = false;
    m_currentMapVisual = nullptr;
    m_currentVisualMapId.clear();
}

void VisualRuntime::OnWorldEvent(const WorldNetworkEvent& event) {
    if (!m_ready) {
        return;
    }
    switch (event.type) {
        case WorldNetworkEvent::Type::EnterWorldSuccess: {
            // 新角色进入：重置本地状态（classId/名字随 Spawn/Enter 事件携带）。
            ClearTransientVisuals();
            m_localCharacterId = event.characterId;
            m_localName = event.characterName;
            m_localClassId = event.classId;
            break;
        }
        case WorldNetworkEvent::Type::Disconnected: {
            ClearTransientVisuals();
            m_localCharacterId = 0;
            break;
        }
        case WorldNetworkEvent::Type::MapChangedEvent: {
            // 指令四十八（阶段48 验收）：MapChanged 清场。
            ClearTransientVisuals();
            break;
        }
        case WorldNetworkEvent::Type::PositionSnapshot: {
            // 本地服务器位置（平滑在 Update 中做）。
            if (m_localHasServerPos) {
                const float dx = event.positionX - m_localLastServerX;
                const float dy = event.positionY - m_localLastServerY;
                if (dx * dx + dy * dy > 1.0f) {
                    // 方向由移动向量决定（指令十四：8 方向）
                    const legend::entity::Direction8 dir =
                        legend::entity::DirectionFromVector(Vector2(dx, dy),
                                                            legend::entity::Direction8::South);
                    const int dirIdx = static_cast<int>(dir);
                    auto it = m_playerVisuals.find(m_localCharacterId);
                    if (it != m_playerVisuals.end()) {
                        it->second.player.SetDirection(dirIdx);
                        it->second.direction = dirIdx;
                    }
                }
            }
            m_localLastServerX = event.positionX;
            m_localLastServerY = event.positionY;
            m_localHasServerPos = true;
            break;
        }
        case WorldNetworkEvent::Type::CombatEvent: {
            // 伤害飘字（指令二十七）+ 攻击/受击动画。
            const bool attackerIsPlayer = event.attackerType == kEntityTypePlayer;
            const bool targetIsPlayer = event.targetType == kEntityTypePlayer;
            // 攻击者动画（普攻/技能命中瞬间都视作攻击表现）。
            if (attackerIsPlayer) {
                auto it = m_playerVisuals.find(event.attackerId);
                if (it != m_playerVisuals.end()) {
                    it->second.attackHold = 0.35f;
                    // 面向目标
                    const auto targetIt =
                        event.targetType == kEntityTypeMonster
                            ? m_monsterVisuals.find(event.targetId)
                            : m_playerVisuals.end();
                    if (targetIt != m_monsterVisuals.end() && targetIt->second.hasPrev) {
                        // 方向在 Update 由位置差分统一处理；此处跳过
                    }
                }
            } else if (event.attackerType == kEntityTypeMonster) {
                auto it = m_monsterVisuals.find(event.attackerId);
                if (it != m_monsterVisuals.end()) {
                    it->second.attackHold = 0.4f;
                }
            }
            // 受击动画 + 飘字（位置：目标镜像 render 位置；本地用平滑位置）
            if (targetIsPlayer) {
                const bool isSelf = event.targetId == m_localCharacterId;
                auto it = m_playerVisuals.find(event.targetId);
                if (it != m_playerVisuals.end()) {
                    it->second.hitHold = 0.3f;
                }
                Vector2 pos(m_localLastServerX, m_localLastServerY);
                if (!isSelf && it != m_playerVisuals.end() && it->second.hasPrev) {
                    pos = Vector2(it->second.prevX, it->second.prevY);
                }
                const bool isDot = event.sourceType ==
                                   static_cast<std::uint8_t>(world::CombatSource::StatusEffect);
                SpawnDamageNumber(pos + Vector2(0.0f, -70.0f), event.damage, isDot, isSelf);
            } else if (event.targetType == kEntityTypeMonster) {
                auto it = m_monsterVisuals.find(event.targetId);
                if (it != m_monsterVisuals.end()) {
                    it->second.hitHold = 0.3f;
                    if (it->second.hasPrev) {
                        const bool isDot =
                            event.sourceType ==
                            static_cast<std::uint8_t>(world::CombatSource::StatusEffect);
                        SpawnDamageNumber(
                            Vector2(it->second.prevX, it->second.prevY) + Vector2(0.0f, -40.0f),
                            event.damage, isDot, false);
                    }
                }
            }
            break;
        }
        case WorldNetworkEvent::Type::SkillCastResponseEvent: {
            // 服务器接受施法 → HUD CD 显示（展示值来自本地 skills.json，服务器仍权威）。
            if (event.accepted && event.skillId != 0) {
                const visual::SkillDisplay* display = m_catalog->FindSkillDisplay(event.skillId);
                if (display != nullptr && display->cooldownSeconds > 0.0f) {
                    m_skillCooldowns[event.skillId] = display->cooldownSeconds;
                }
            }
            break;
        }
        case WorldNetworkEvent::Type::SkillCastStartedEvent: {
            // 施法者位置：本地取平滑位置；远程取镜像 render 位置。
            Vector2 from(m_localVisualX, m_localVisualY);
            if (event.characterId != m_localCharacterId) {
                auto it = m_playerVisuals.find(event.characterId);
                if (it == m_playerVisuals.end() || !it->second.hasPrev) {
                    break; // 无 caster 镜像（不该发生）
                }
                from = Vector2(it->second.prevX, it->second.prevY);
            }
            if (event.skillId == world::kSkillIdQuickStrike) {
                // 指令二十五：近战刀光
                SpawnEffect("fx_quick_strike_slash", from, event.castId);
                auto it = m_playerVisuals.find(event.characterId);
                if (it != m_playerVisuals.end()) {
                    it->second.attackHold = 0.35f;
                }
            } else if (event.skillId == world::kSkillIdFireBolt) {
                // 指令二十六：Projectile 视觉（伤害仍由服务器决定）
                Vector2 to = from;
                bool haveTarget = false;
                if (event.skillTargetType ==
                        static_cast<std::uint8_t>(world::SkillTargetType::Monster) &&
                    event.targetEntityId != 0) {
                    const auto* monster = m_monsterVisuals.count(event.targetEntityId)
                                              ? &m_monsterVisuals.at(event.targetEntityId)
                                              : nullptr;
                    if (monster != nullptr && monster->hasPrev) {
                        to = Vector2(monster->prevX, monster->prevY - 24.0f);
                        haveTarget = true;
                    }
                }
                if (!haveTarget) {
                    to = from + Vector2(120.0f, 0.0f); // 无目标镜像：向面朝方向短射
                }
                SpawnProjectile("fx_fire_bolt", from + Vector2(0.0f, -28.0f), to, event.castId);
            } else if (event.skillId == world::kSkillIdWhirlwind) {
                // 指令二十五：角色中心 AOE 旋转效果
                SpawnEffect("fx_whirlwind", from, event.castId);
            }
            break;
        }
        case WorldNetworkEvent::Type::SkillCastCompletedEvent: {
            // Whirlwind 结束移除（loop 效果随 Completed 停止）。
            for (auto& effect : m_effects) {
                if (effect.castId == event.castId && effect.looping && !effect.projectile) {
                    effect.looping = false;
                    effect.elapsed = std::max(effect.elapsed,
                                              effect.totalDuration - 1.0f / 60.0f);
                }
            }
            break;
        }
        case WorldNetworkEvent::Type::SkillCastCancelledEvent: {
            m_effects.erase(std::remove_if(m_effects.begin(), m_effects.end(),
                                           [&](const ActiveEffect& e) {
                                               return e.castId == event.castId;
                                           }),
                            m_effects.end());
            break;
        }
        case WorldNetworkEvent::Type::SkillImpact: {
            // 指令二十六：Impact → 命中特效（首个目标位置）。
            if (event.skillId == world::kSkillIdFireBolt) {
                // 移除对应 Projectile（视觉提前结束）
                m_effects.erase(std::remove_if(m_effects.begin(), m_effects.end(),
                                               [&](const ActiveEffect& e) {
                                                   return e.castId == event.castId &&
                                                          e.projectile;
                                               }),
                                m_effects.end());
                Vector2 impactPos(0.0f, 0.0f);
                bool have = false;
                for (const auto& target : event.impactTargets) {
                    const auto it = m_monsterVisuals.find(target.entityId);
                    if (it != m_monsterVisuals.end() && it->second.hasPrev) {
                        impactPos = Vector2(it->second.prevX, it->second.prevY - 24.0f);
                        have = true;
                        break;
                    }
                }
                if (have) {
                    SpawnEffect("fx_fire_bolt_impact", impactPos, 0);
                }
            }
            break;
        }
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// 实体视觉状态
// ---------------------------------------------------------------------------

VisualRuntime::EntityVisual& VisualRuntime::EnsureVisual(
    std::unordered_map<std::uint64_t, EntityVisual>& map, std::uint64_t id, EntityKind kind,
    const std::string& visualId) {
    auto it = map.find(id);
    if (it == map.end()) {
        EntityVisual ev;
        ev.kind = kind;
        ev.visualId = visualId;
        if (!visualId.empty()) {
            ApplyEntityDefinition(ev);
        }
        it = map.emplace(id, std::move(ev)).first;
    }
    return it->second;
}

void VisualRuntime::ApplyEntityDefinition(EntityVisual& ev) {
    const visual::VisualEntityDef* def =
        ev.visualId.empty() ? nullptr : m_catalog->FindEntity(ev.visualId);
    if (def == nullptr) {
        return;
    }
    // 槽位 -> clip 查找表（指向 catalog 持有的定义，无拷贝）。
    std::map<std::string, const visual::AnimationClipDef*> clips;
    for (const auto& [slot, animId] : def->animations) {
        const visual::AnimationClipDef* clip = m_catalog->FindClip(animId);
        if (clip != nullptr) {
            clips[slot] = clip;
        }
    }
    ev.player.SetClips(&clips);
    ev.slot.clear(); // 强制重新 Play（clip 集可能变化）
}

void VisualRuntime::ApplySlot(EntityVisual& ev, const char* slotName) {
    if (ev.slot == slotName) {
        return; // 已在该槽位（Play 同名保持进度）
    }
    if (ev.player.Play(slotName)) {
        ev.slot = slotName;
    } else {
        // 槽位缺失 → fallback idle（visual 定义不完整时不崩溃）
        if (ev.player.Play("idle")) {
            ev.slot = "idle";
        }
    }
}

void VisualRuntime::UpdateEntityVisual(EntityVisual& ev, float dt, bool moving, bool casting,
                                       bool alive) {
    if (ev.attackHold > 0.0f) {
        ev.attackHold -= dt;
    }
    if (ev.hitHold > 0.0f) {
        ev.hitHold -= dt;
    }
    ev.dead = !alive;
    if (!alive) {
        ApplySlot(ev, "death");
    } else if (ev.hitHold > 0.0f) {
        ApplySlot(ev, "hit");
    } else if (ev.attackHold > 0.0f) {
        ApplySlot(ev, "attack");
    } else if (casting) {
        ApplySlot(ev, "cast");
    } else if (moving) {
        ApplySlot(ev, "walk");
    } else {
        ApplySlot(ev, "idle");
    }
    ev.player.Update(dt);
}

void VisualRuntime::Update(const WorldClientController& world, float deltaTime) {
    if (!m_ready) {
        return;
    }

    // ---- 本地玩家平滑位置（指令十三：服务器权威 + 插值）----
    if (world.IsWorldReady()) {
        if (!m_loggedWorldReady) {
            m_loggedWorldReady = true;
            // 阶段24 指令四十七/四十八：在线视觉冒烟里程碑（进世界成功）。
            LOG_INFO("[VisualSmoke] world-ready map=" +
                     std::to_string(world.MapModel().CurrentMapId()) +
                     " name=" + world.MapModel().CurrentMapName());
        }
        const float targetX = world.ServerPositionX();
        const float targetY = world.ServerPositionY();
        if (!m_localHasServerPos) {
            m_localVisualX = targetX;
            m_localVisualY = targetY;
            m_localLastServerX = targetX;
            m_localLastServerY = targetY;
            m_localHasServerPos = true;
        } else {
            const float alpha = 1.0f - std::exp(-kLocalSmoothRate * deltaTime);
            m_localVisualX += (targetX - m_localVisualX) * alpha;
            m_localVisualY += (targetY - m_localVisualY) * alpha;
            const float dx = targetX - m_localVisualX;
            const float dy = targetY - m_localVisualY;
            if (dx * dx + dy * dy > kLocalSnapDist * kLocalSnapDist) {
                m_localVisualX = targetX;
                m_localVisualY = targetY;
            }
        }

        // 本地玩家视觉
        auto& local = EnsureVisual(m_playerVisuals, world.CharacterId(),
                                   EntityKind::LocalPlayer, std::string());
        if (local.visualId.empty()) {
            const int classId = m_localClassId != 0 ? m_localClassId : 1;
            const visual::VisualEntityDef* def = m_catalog->FindPlayerEntityByClass(classId);
            if (def != nullptr) {
                local.visualId = def->visualId;
                ApplyEntityDefinition(local);
            }
        }
        // 方向：服务器位置差分
        const float srvDx = targetX - m_localLastServerX;
        const float srvDy = targetY - m_localLastServerY;
        const bool localMoving = world.IsWorldReady() &&
                                 (srvDx * srvDx + srvDy * srvDy > 0.5f);
        if (localMoving) {
            const legend::entity::Direction8 dir = legend::entity::DirectionFromVector(
                Vector2(srvDx, srvDy),
                static_cast<legend::entity::Direction8>(local.direction));
            local.direction = static_cast<int>(dir);
            local.player.SetDirection(local.direction);
        }
        UpdateEntityVisual(local, deltaTime, localMoving, world.LocalCasting(),
                           world.LocalAlive());
        local.prevX = m_localVisualX;
        local.prevY = m_localVisualY;
        local.hasPrev = true;

        // ---- 远程玩家 ----
        for (const auto& [characterId, remote] : world.RemotePlayers().All()) {
            auto& ev = EnsureVisual(m_playerVisuals, characterId, EntityKind::RemotePlayer,
                                    std::string());
            if (ev.visualId.empty()) {
                const visual::VisualEntityDef* def = m_catalog->FindPlayerEntityByClass(
                    remote.ClassId() != 0 ? static_cast<int>(remote.ClassId()) : 1);
                if (def != nullptr) {
                    ev.visualId = def->visualId;
                    ApplyEntityDefinition(ev);
                }
            }
            // 方向（render 位置差分）
            if (ev.hasPrev) {
                const float dx = remote.RenderX() - ev.prevX;
                const float dy = remote.RenderY() - ev.prevY;
                if (dx * dx + dy * dy > 0.5f) {
                    const legend::entity::Direction8 dir = legend::entity::DirectionFromVector(
                        Vector2(dx, dy), static_cast<legend::entity::Direction8>(ev.direction));
                    ev.direction = static_cast<int>(dir);
                    ev.player.SetDirection(ev.direction);
                }
            }
            UpdateEntityVisual(ev, deltaTime, remote.IsMoving(), remote.Casting(),
                               remote.Alive());
            ev.prevX = remote.RenderX();
            ev.prevY = remote.RenderY();
            ev.hasPrev = true;
        }

        // ---- 怪物（指令十九）----
        for (const auto& [entityId, monster] : world.RemoteMonsters().All()) {
            std::string visualId;
            if (m_catalog != nullptr) {
                visualId = m_catalog->MonsterVisualId(monster.MonsterTypeId());
            }
            auto& ev = EnsureVisual(m_monsterVisuals, entityId, EntityKind::Monster, visualId);
            if (ev.hasPrev) {
                const float dx = monster.RenderX() - ev.prevX;
                const float dy = monster.RenderY() - ev.prevY;
                if (dx * dx + dy * dy > 0.5f) {
                    const legend::entity::Direction8 dir = legend::entity::DirectionFromVector(
                        Vector2(dx, dy), static_cast<legend::entity::Direction8>(ev.direction));
                    ev.direction = static_cast<int>(dir);
                    ev.player.SetDirection(ev.direction);
                }
            }
            const bool moving = monster.Alive() &&
                                (monster.State() == 1 /*Patrol*/ || monster.State() == 2 /*Chase*/ ||
                                 monster.State() == 3 /*Return*/);
            UpdateEntityVisual(ev, deltaTime, moving, false, monster.Alive());
            ev.prevX = monster.RenderX();
            ev.prevY = monster.RenderY();
            ev.hasPrev = true;
        }

        // ---- NPC（指令二十：Idle 动画）----
        for (const auto& [npcEntityId, npc] : world.Npcs().All()) {
            std::string visualId;
            if (m_catalog != nullptr) {
                const visual::VisualEntityDef* def =
                    m_catalog->FindNpcEntityByServerVisualId(static_cast<int>(npc.visualId));
                if (def != nullptr) {
                    visualId = def->visualId;
                }
            }
            auto& ev = EnsureVisual(m_npcVisuals, npcEntityId, EntityKind::Npc, visualId);
            UpdateEntityVisual(ev, deltaTime, false, false, npc.alive);
            ev.prevX = npc.x;
            ev.prevY = npc.y;
            ev.hasPrev = true;
        }

        // ---- Portal（指令二十一：循环动画）----
        for (const auto& [portalEntityId, portal] : world.Portals().All()) {
            std::string visualId;
            if (m_catalog != nullptr) {
                visualId = m_catalog->PortalVisualId(portal.portalId);
                if (visualId.empty()) {
                    visualId = "portal_default";
                }
            }
            auto& ev = EnsureVisual(m_portalVisuals, portalEntityId, EntityKind::Portal,
                                    visualId);
            UpdateEntityVisual(ev, deltaTime, false, false, portal.active);
            ev.prevX = portal.x;
            ev.prevY = portal.y;
            ev.hasPrev = true;
        }

        PruneVisuals(world);
    }

    // ---- 特效/飘字/CD 推进 ----
    for (auto& effect : m_effects) {
        effect.elapsed += deltaTime;
        if (effect.projectile) {
            const float t = std::min(effect.elapsed / std::max(effect.travelTime, 0.001f), 1.0f);
            effect.pos = effect.from + (effect.to - effect.from) * t;
        }
    }
    m_effects.erase(std::remove_if(m_effects.begin(), m_effects.end(),
                                   [](const ActiveEffect& e) {
                                       if (e.projectile) {
                                           return e.elapsed > kProjectileMaxLife;
                                       }
                                       if (e.looping) {
                                           return false;
                                       }
                                       return e.elapsed >= e.totalDuration;
                                   }),
                    m_effects.end());
    for (auto& number : m_damageNumbers) {
        number.age += deltaTime;
        number.pos = number.pos + number.velocity * deltaTime;
    }
    m_damageNumbers.erase(std::remove_if(m_damageNumbers.begin(), m_damageNumbers.end(),
                                         [](const DamageNumber& n) { return n.age >= n.life; }),
                          m_damageNumbers.end());
    for (auto it = m_skillCooldowns.begin(); it != m_skillCooldowns.end();) {
        it->second -= deltaTime;
        if (it->second <= 0.0f) {
            it = m_skillCooldowns.erase(it);
        } else {
            ++it;
        }
    }

    // 当前地图视觉解析（MapChanged 后自动重解析）
    const std::uint16_t mapId = world.MapModel().CurrentMapId();
    const visual::MapDisplay* mapDisplay = m_catalog->FindMapDisplay(mapId);
    const std::string visualMapId =
        mapDisplay != nullptr ? mapDisplay->visualMapId : std::string();
    if (visualMapId != m_currentVisualMapId) {
        m_currentVisualMapId = visualMapId;
        m_currentMapVisual = visualMapId.empty()
                                 ? nullptr
                                 : m_catalog->FindMapVisual(visualMapId);
    }
}

void VisualRuntime::PruneVisuals(const WorldClientController& world) {
    // 清理已 Despawn 的镜像（死亡怪物保留到 Despawn——服务器权威）。
    for (auto it = m_playerVisuals.begin(); it != m_playerVisuals.end();) {
        if (it->first == m_localCharacterId ||
            world.RemotePlayers().Find(it->first) != nullptr) {
            ++it;
        } else {
            it = m_playerVisuals.erase(it);
        }
    }
    for (auto it = m_monsterVisuals.begin(); it != m_monsterVisuals.end();) {
        if (world.RemoteMonsters().Find(it->first) != nullptr) {
            ++it;
        } else {
            it = m_monsterVisuals.erase(it);
        }
    }
    for (auto it = m_npcVisuals.begin(); it != m_npcVisuals.end();) {
        if (world.Npcs().All().count(it->first) != 0) {
            ++it;
        } else {
            it = m_npcVisuals.erase(it);
        }
    }
    for (auto it = m_portalVisuals.begin(); it != m_portalVisuals.end();) {
        if (world.Portals().All().count(it->first) != 0) {
            ++it;
        } else {
            it = m_portalVisuals.erase(it);
        }
    }
}

// ---------------------------------------------------------------------------
// 特效 / 飘字
// ---------------------------------------------------------------------------

void VisualRuntime::SpawnEffect(const std::string& effectId, const Vector2& pos,
                                std::uint64_t castId) {
    const visual::EffectDef* def = m_catalog->FindEffect(effectId);
    if (def == nullptr) {
        return;
    }
    ActiveEffect effect;
    effect.def = def;
    effect.castId = castId;
    effect.pos = pos + Vector2(def->offsetX, def->offsetY);
    effect.totalDuration =
        def->duration > 0.0f ? def->duration
                             : static_cast<float>(def->frameCount) / std::max(def->fps, 0.001f);
    effect.looping = def->loop;
    m_effects.push_back(std::move(effect));
}

void VisualRuntime::SpawnProjectile(const std::string& effectId, const Vector2& from,
                                    const Vector2& to, std::uint64_t castId) {
    const visual::EffectDef* def = m_catalog->FindEffect(effectId);
    if (def == nullptr) {
        return;
    }
    ActiveEffect effect;
    effect.def = def;
    effect.castId = castId;
    effect.projectile = true;
    effect.from = from;
    effect.to = to;
    const float dist = std::sqrt((to.x - from.x) * (to.x - from.x) +
                                 (to.y - from.y) * (to.y - from.y));
    effect.travelTime = std::clamp(dist / kProjectileSpeed, 0.12f, 1.2f);
    effect.elapsed = 0.0f;
    effect.totalDuration = kProjectileMaxLife;
    effect.looping = true;
    effect.pos = from;
    m_effects.push_back(std::move(effect));
}

void VisualRuntime::SpawnDamageNumber(const Vector2& pos, std::uint32_t amount, bool isDot,
                                      bool onSelf) {
    if (amount == 0) {
        return;
    }
    DamageNumber number;
    number.pos = pos + Vector2(0.0f, -6.0f);
    number.velocity = Vector2(0.0f, -34.0f);
    number.age = 0.0f;
    number.life = 0.9f;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(amount));
    number.text = buffer;
    if (onSelf) {
        number.color = Color(1.0f, 0.28f, 0.25f, 1.0f); // 受到伤害：红
    } else if (isDot) {
        number.color = Color(0.75f, 0.45f, 0.95f, 1.0f); // DOT：紫
    } else {
        number.color = Color(1.0f, 0.96f, 0.75f, 1.0f); // 普通伤害：淡金
    }
    m_damageNumbers.push_back(std::move(number));
}

void VisualRuntime::DrawEffects(legend::render::SpriteBatch& batch) {
    for (const auto& effect : m_effects) {
        if (effect.def == nullptr) {
            continue;
        }
        const visual::EffectDef& def = *effect.def;
        const float frameDuration = 1.0f / std::max(def.fps, 0.001f);
        int frameIndex = static_cast<int>(effect.elapsed / frameDuration);
        if (def.loop) {
            frameIndex %= def.frameCount;
        } else {
            frameIndex = std::min(frameIndex, def.frameCount - 1);
        }
        const float u0 = static_cast<float>(frameIndex * def.frameWidth) /
                         static_cast<float>(def.sheetWidth);
        const float u1 = static_cast<float>((frameIndex + 1) * def.frameWidth) /
                         static_cast<float>(def.sheetWidth);
        const float v0 = 0.0f;
        const float v1 = 1.0f;
        auto texture = m_assets.GetTexture(def.spriteSheetAssetId, nullptr, "effect");
        if (texture == nullptr) {
            continue;
        }
        const float scaleX = static_cast<float>(def.frameWidth) * def.scale /
                             static_cast<float>(def.sheetWidth);
        const float scaleY = static_cast<float>(def.frameHeight) * def.scale /
                             static_cast<float>(def.sheetHeight);
        batch.DrawQuad(*texture, effect.pos, {scaleX, scaleY}, 0.0f,
                       Color(1.0f, 1.0f, 1.0f, 1.0f), false, false, u0, v0, u1, v1);
        ++m_stats.visibleSprites;
    }
}

// ---------------------------------------------------------------------------
// 实体绘制 + 世界层
// ---------------------------------------------------------------------------

void VisualRuntime::DrawEntitySprite(legend::render::SpriteBatch& batch, EntityVisual& ev,
                                     const Vector2& feet, float scaleBoost) {
    const visual::VisualEntityDef* def =
        ev.visualId.empty() ? nullptr : m_catalog->FindEntity(ev.visualId);
    const auto frame = ev.player.CurrentFrame();
    if (def == nullptr || !frame.valid || frame.clip == nullptr) {
        // Fallback 色块（资源/定义缺失时不黑屏；保留 Debug 语义）
        Color fallback(0.35f, 0.95f, 0.45f, 0.9f);
        if (ev.kind == EntityKind::Monster) {
            fallback = Color(0.95f, 0.30f, 0.25f, 0.9f);
        } else if (ev.kind == EntityKind::Npc) {
            fallback = Color(0.30f, 0.55f, 0.95f, 0.9f);
        } else if (ev.kind == EntityKind::Portal) {
            fallback = Color(0.35f, 0.85f, 0.95f, 0.85f);
        }
        batch.DrawQuad(*m_whiteTexture, feet + Vector2(0.0f, -24.0f), {48.0f / 64.0f, 48.0f / 64.0f},
                       0.0f, fallback);
        ++m_stats.visibleSprites;
        return;
    }

    AssetManager::TextureInfo info;
    auto texture = m_assets.GetTexture(frame.clip->spriteSheetAssetId, &info, "entity");
    if (texture == nullptr || !texture->IsValid()) {
        return;
    }
    const float fw = static_cast<float>(frame.clip->frameWidth) * def->scale * scaleBoost;
    const float fh = static_cast<float>(frame.clip->frameHeight) * def->scale * scaleBoost;
    // DrawQuad 语义：世界尺寸 = 纹理整图尺寸 × scale（UV 子矩形不改变几何）。
    const float scaleX = fw / static_cast<float>(texture->GetWidth());
    const float scaleY = fh / static_cast<float>(texture->GetHeight());
    // pivot（manifest，帧内归一化）：feet 为锚点。
    const Vector2 center = feet + Vector2((0.5f - info.pivotX) * fw, (0.5f - info.pivotY) * fh);

    Color tint(1.0f, 1.0f, 1.0f, 1.0f);
    if (ev.dead || !ev.player.IsPlaying() ||
        (frame.clip != nullptr && frame.clip->animationId.find("death") != std::string::npos)) {
        tint = Color(0.55f, 0.55f, 0.60f, 0.95f); // 死亡灰
    } else if (ev.hitHold > 0.0f) {
        tint = Color(1.0f, 0.55f, 0.45f, 1.0f); // 受击红闪
    }
    if (!info.fallback) {
        batch.DrawQuad(*texture, center, {scaleX, scaleY}, 0.0f, tint, false, false, frame.u0,
                       frame.v0, frame.u1, frame.v1);
    } else {
        // fallback 纹理没有帧网格 UV——整图绘制
        batch.DrawQuad(*texture, center, {scaleX, scaleY}, 0.0f, tint);
    }
    ++m_stats.visibleSprites;
}

namespace {

struct SortItem {
    float sortY = 0.0f;
    int order = 0;
    int kind = 0; // 0=placement 1=player 2=monster 3=npc 4=portal
    const void* ptr = nullptr;
};

bool SortItemCompare(const SortItem& a, const SortItem& b) {
    if (a.sortY != b.sortY) {
        return a.sortY < b.sortY;
    }
    return a.order < b.order;
}

} // namespace

void VisualRuntime::DrawPlacements(legend::render::SpriteBatch& batch,
                                   const MapVisualDefinition* visual, int layerIndex,
                                   float viewLeft, float viewTop, float viewRight,
                                   float viewBottom) {
    if (visual == nullptr || layerIndex >= static_cast<int>(visual->layers.size())) {
        return;
    }
    const MapVisualLayer& layer = visual->layers[static_cast<size_t>(layerIndex)];
    for (const auto& placement : layer.placements) {
        if (placement.x < viewLeft - 128.0f || placement.x > viewRight + 128.0f ||
            placement.y < viewTop - 192.0f || placement.y > viewBottom + 128.0f) {
            continue; // 视口剔除
        }
        const visual::AssetManifestEntry* entry = m_catalog->FindAsset(placement.assetId);
        if (entry == nullptr) {
            continue;
        }
        auto texture = m_assets.GetTexture(entry->assetId, nullptr, "placement");
        if (texture == nullptr || !texture->IsValid()) {
            continue;
        }
        const float scaleX = static_cast<float>(entry->width) /
                             static_cast<float>(texture->GetWidth());
        const float scaleY = static_cast<float>(entry->height) /
                             static_cast<float>(texture->GetHeight());
        const Vector2 center =
            Vector2(placement.x, placement.y) +
            Vector2((0.5f - entry->pivotX) * entry->width, (0.5f - entry->pivotY) * entry->height);
        batch.DrawQuad(*texture, center, {scaleX, scaleY}, 0.0f, Color(1, 1, 1, 1));
        ++m_stats.visibleSprites;
    }
}

int VisualRuntime::RenderWorld(legend::render::SpriteBatch& batch,
                               const legend::render::Camera2D& camera, float viewLeft,
                               float viewTop, float viewRight, float viewBottom,
                               const WorldClientController& world) {
    (void)camera;
    m_stats.visibleSprites = 0;
    m_stats.texturesLoaded = m_assets.LoadedCount();
    m_stats.effectsActive = static_cast<int>(m_effects.size());
    m_drawCallBase = batch.GetDrawCallCount();
    if (!m_ready || m_whiteTexture == nullptr) {
        return 0;
    }

    // ---- 1. Ground：地图背景平铺 / Fallback Grid（指令十二：绝不黑屏）----
    const MapVisualDefinition* visual = m_currentMapVisual;
    if (visual != nullptr) {
        const float ts = visual->tileSize > 0.0f ? visual->tileSize : 64.0f;
        auto tex = m_assets.GetTexture(visual->backgroundAsset, nullptr, "ground");
        if (tex != nullptr && tex->IsValid()) {
            const int tx0 = static_cast<int>(std::floor(viewLeft / ts)) - 1;
            const int tx1 = static_cast<int>(std::floor(viewRight / ts)) + 1;
            const int ty0 = static_cast<int>(std::floor(viewTop / ts)) - 1;
            const int ty1 = static_cast<int>(std::floor(viewBottom / ts)) + 1;
            const float scaleX = ts / static_cast<float>(tex->GetWidth());
            const float scaleY = ts / static_cast<float>(tex->GetHeight());
            for (int ty = ty0; ty <= ty1; ++ty) {
                for (int tx = tx0; tx <= tx1; ++tx) {
                    const Vector2 center((static_cast<float>(tx) + 0.5f) * ts,
                                         (static_cast<float>(ty) + 0.5f) * ts);
                    batch.DrawQuad(*tex, center, {scaleX, scaleY}, 0.0f, Color(1, 1, 1, 1));
                }
            }
            m_stats.visibleSprites += (tx1 - tx0 + 1) * (ty1 - ty0 + 1);
        }
    } else {
        // Fallback Grid：双绿棋盘 + 每 4 格边界线（开发用统一背景）
        const float ts = 64.0f;
        const int tx0 = static_cast<int>(std::floor(viewLeft / ts)) - 1;
        const int tx1 = static_cast<int>(std::floor(viewRight / ts)) + 1;
        const int ty0 = static_cast<int>(std::floor(viewTop / ts)) - 1;
        const int ty1 = static_cast<int>(std::floor(viewBottom / ts)) + 1;
        const Color a(0.32f, 0.46f, 0.30f, 1.0f);
        const Color b(0.27f, 0.40f, 0.26f, 1.0f);
        for (int ty = ty0; ty <= ty1; ++ty) {
            for (int tx = tx0; tx <= tx1; ++tx) {
                const Vector2 center((static_cast<float>(tx) + 0.5f) * ts,
                                     (static_cast<float>(ty) + 0.5f) * ts);
                const bool even = ((tx & 1) ^ (ty & 1)) == 0;
                batch.DrawQuad(*m_whiteTexture, center, {ts / 64.0f, ts / 64.0f}, 0.0f,
                               even ? a : b);
            }
        }
        const Color line(0.55f, 0.75f, 0.50f, 0.25f);
        for (int tx = tx0 - tx0 % 4; tx <= tx1; tx += 4) {
            batch.DrawQuad(*m_whiteTexture, {static_cast<float>(tx) * ts, (viewTop + viewBottom) * 0.5f},
                           {2.0f / 64.0f, (viewBottom - viewTop) / 64.0f}, 0.0f, line);
        }
        for (int ty = ty0 - ty0 % 4; ty <= ty1; ty += 4) {
            batch.DrawQuad(*m_whiteTexture, {(viewLeft + viewRight) * 0.5f, static_cast<float>(ty) * ts},
                           {(viewRight - viewLeft) / 64.0f, 2.0f / 64.0f}, 0.0f, line);
        }
        m_stats.visibleSprites += (tx1 - tx0 + 1) * (ty1 - ty0 + 1);
    }

    // ---- 2. Decoration（实体之下）----
    DrawPlacements(batch, visual, 1, viewLeft, viewTop, viewRight, viewBottom);

    // ---- 3. Y-Sort：Object 层物件 + 全部世界实体（指令十一：按 Y 排序 2.5D）----
    std::vector<SortItem> items;
    if (visual != nullptr && visual->layers.size() > 2) {
        for (const auto& placement : visual->layers[2].placements) {
            if (placement.x < viewLeft - 128.0f || placement.x > viewRight + 128.0f ||
                placement.y < viewTop - 192.0f || placement.y > viewBottom + 128.0f) {
                continue;
            }
            items.push_back({placement.y, 0, 0, &placement});
        }
    }
    for (const auto& [characterId, ev] : m_playerVisuals) {
        (void)characterId;
        if (ev.hasPrev) {
            items.push_back({ev.prevY, 1, 1, &ev});
        }
    }
    for (const auto& [entityId, ev] : m_monsterVisuals) {
        (void)entityId;
        if (ev.hasPrev) {
            items.push_back({ev.prevY, 2, 2, &ev});
        }
    }
    for (const auto& [npcId, ev] : m_npcVisuals) {
        (void)npcId;
        if (ev.hasPrev) {
            items.push_back({ev.prevY, 3, 3, &ev});
        }
    }
    for (const auto& [portalId, ev] : m_portalVisuals) {
        (void)portalId;
        if (ev.hasPrev) {
            items.push_back({ev.prevY, 4, 4, &ev});
        }
    }
    std::sort(items.begin(), items.end(), SortItemCompare);
    for (const auto& item : items) {
        switch (item.kind) {
            case 0: {
                const auto* placement = static_cast<const MapVisualPlacement*>(item.ptr);
                const visual::AssetManifestEntry* entry =
                    m_catalog->FindAsset(placement->assetId);
                if (entry == nullptr) {
                    break;
                }
                auto texture = m_assets.GetTexture(entry->assetId, nullptr, "object");
                if (texture == nullptr || !texture->IsValid()) {
                    break;
                }
                const float scaleX =
                    static_cast<float>(entry->width) / static_cast<float>(texture->GetWidth());
                const float scaleY =
                    static_cast<float>(entry->height) / static_cast<float>(texture->GetHeight());
                const Vector2 center =
                    Vector2(placement->x, placement->y) +
                    Vector2((0.5f - entry->pivotX) * entry->width,
                            (0.5f - entry->pivotY) * entry->height);
                batch.DrawQuad(*texture, center, {scaleX, scaleY}, 0.0f, Color(1, 1, 1, 1));
                ++m_stats.visibleSprites;
                break;
            }
            case 1:
                DrawEntitySprite(batch, *static_cast<EntityVisual*>(const_cast<void*>(item.ptr)),
                                 Vector2(static_cast<const EntityVisual*>(item.ptr)->prevX,
                                         static_cast<const EntityVisual*>(item.ptr)->prevY));
                break;
            case 2:
                DrawEntitySprite(batch, *static_cast<EntityVisual*>(const_cast<void*>(item.ptr)),
                                 Vector2(static_cast<const EntityVisual*>(item.ptr)->prevX,
                                         static_cast<const EntityVisual*>(item.ptr)->prevY));
                break;
            case 3:
                DrawEntitySprite(batch, *static_cast<EntityVisual*>(const_cast<void*>(item.ptr)),
                                 Vector2(static_cast<const EntityVisual*>(item.ptr)->prevX,
                                         static_cast<const EntityVisual*>(item.ptr)->prevY));
                break;
            case 4:
                DrawEntitySprite(batch, *static_cast<EntityVisual*>(const_cast<void*>(item.ptr)),
                                 Vector2(static_cast<const EntityVisual*>(item.ptr)->prevX,
                                         static_cast<const EntityVisual*>(item.ptr)->prevY),
                                 1.0f);
                break;
            default:
                break;
        }
    }

    // ---- 4. Foreground（实体之上）----
    DrawPlacements(batch, visual, 3, viewLeft, viewTop, viewRight, viewBottom);

    // ---- 5. World Effects（技能 VFX；指令二十四渲染顺序）----
    DrawEffects(batch);

    m_stats.drawCalls = batch.GetDrawCallCount() - m_drawCallBase;
    return m_stats.visibleSprites;
}

// ---------------------------------------------------------------------------
// 名字板 / 头顶血条 / 伤害飘字（世界空间；指令二十二/二十三/二十七）
// ---------------------------------------------------------------------------

void VisualRuntime::DrawBar(legend::render::SpriteBatch& batch, const Vector2& topLeft,
                            float width, float height, float pct, const Color& fill) {
    batch.DrawQuad(*m_whiteTexture, topLeft + Vector2(width * 0.5f, height * 0.5f),
                   {width / 64.0f, height / 64.0f}, 0.0f, kBarBack);
    const float fillW = width * std::clamp(pct, 0.0f, 1.0f);
    if (fillW > 0.5f) {
        batch.DrawQuad(*m_whiteTexture, topLeft + Vector2(fillW * 0.5f, height * 0.5f),
                       {fillW / 64.0f, height / 64.0f}, 0.0f, fill);
    }
}

void VisualRuntime::DrawPanel(legend::render::SpriteBatch& batch, const Vector2& topLeft,
                              float width, float height, float alpha) {
    batch.DrawQuad(*m_whiteTexture, topLeft + Vector2(width * 0.5f, height * 0.5f),
                   {width / 64.0f, height / 64.0f}, 0.0f,
                   Color(kPanelBack.r, kPanelBack.g, kPanelBack.b, kPanelBack.a * alpha));
}

void VisualRuntime::RenderOverlays(legend::render::SpriteBatch& batch,
                                   const WorldClientController& world) {
    if (!m_ready || m_whiteTexture == nullptr) {
        return;
    }
    const bool haveText = m_text.IsReady();

    // ---- 远程玩家：名字板 + HP（指令十八/二十二/二十三；本地走 HUD）----
    for (const auto& [characterId, remote] : world.RemotePlayers().All()) {
        if (!remote.Alive() || characterId == m_localCharacterId) {
            continue;
        }
        const Vector2 feet(remote.RenderX(), remote.RenderY());
        const float hpPct = remote.MaxHp() > 0
                                ? static_cast<float>(remote.CurrentHp()) /
                                      static_cast<float>(remote.MaxHp())
                                : 0.0f;
        DrawBar(batch, feet + Vector2(-22.0f, -78.0f), 44.0f, 5.0f, hpPct,
                hpPct > 0.35f ? Color(0.35f, 0.85f, 0.35f, 0.95f) : kHpFill);
        if (haveText) {
            m_text.DrawStringShadow(batch, feet + Vector2(0.0f, -96.0f), remote.Name(), 13.0f,
                                  kTextWhite, false, true);
        }
    }

    // ---- 怪物：名字 + 等级 + HP（指令十九/二十二/二十三）----
    for (const auto& [entityId, monster] : world.RemoteMonsters().All()) {
        if (!monster.Alive()) {
            continue;
        }
        const Vector2 feet(monster.RenderX(), monster.RenderY());
        char label[96];
        std::snprintf(label, sizeof(label), "%s Lv.%u", monster.Name().c_str(),
                      static_cast<unsigned>(monster.Level()));
        if (haveText) {
            m_text.DrawStringShadow(batch, feet + Vector2(0.0f, -52.0f), label, 12.0f,
                                  Color(1.0f, 0.72f, 0.55f, 1.0f), false, true);
        }
        const float hpPct = monster.MaxHp() > 0
                                ? static_cast<float>(monster.CurrentHp()) /
                                      static_cast<float>(monster.MaxHp())
                                : 0.0f;
        DrawBar(batch, feet + Vector2(-20.0f, -40.0f), 40.0f, 4.0f, hpPct, kHpFill);
    }

    // ---- NPC：名字 + Quest Marker（指令二十）----
    for (const auto& [npcEntityId, npc] : world.Npcs().All()) {
        if (!npc.alive) {
            continue;
        }
        const Vector2 feet(npc.x, npc.y);
        if (haveText) {
            m_text.DrawStringShadow(batch, feet + Vector2(0.0f, -92.0f), npc.name, 13.0f,
                                  kTextGold, false, true);
        }
        // Quest Marker：!（可接）/ 灰点（进行中）/ ?（可交付）
        switch (npc.questMarker) {
            case world::NpcQuestMarker::Available:
                if (haveText) {
                    m_text.DrawStringShadow(batch, feet + Vector2(0.0f, -116.0f), "!", 22.0f,
                                          kTextGold, false, true);
                }
                break;
            case world::NpcQuestMarker::ReadyToTurnIn:
                if (haveText) {
                    m_text.DrawStringShadow(batch, feet + Vector2(0.0f, -116.0f), "?", 22.0f,
                                          kTextGold, false, true);
                }
                break;
            case world::NpcQuestMarker::InProgress:
                batch.DrawQuad(*m_whiteTexture, feet + Vector2(0.0f, -108.0f),
                               {6.0f / 64.0f, 6.0f / 64.0f}, 0.0f,
                               Color(0.65f, 0.68f, 0.72f, 0.95f));
                break;
            case world::NpcQuestMarker::None:
            default:
                break;
        }
    }

    // ---- Portal：目标地名（指令二十一）----
    for (const auto& [portalEntityId, portal] : world.Portals().All()) {
        const Vector2 feet(portal.x, portal.y);
        if (haveText) {
            m_text.DrawStringShadow(batch, feet + Vector2(0.0f, 18.0f),
                                  portal.destinationName.empty() ? portal.name
                                                                 : portal.destinationName,
                                  12.0f, Color(0.75f, 0.92f, 1.0f, 0.95f), false, true);
        }
    }

    // ---- 伤害飘字（指令二十七）----
    if (haveText) {
        for (const auto& number : m_damageNumbers) {
            const float alpha = 1.0f - (number.age / number.life);
            m_text.DrawStringShadow(
                batch, number.pos, number.text, 16.0f,
                Color(number.color.r, number.color.g, number.color.b, number.color.a * alpha),
                false, true);
        }
    }
}

// ---------------------------------------------------------------------------
// HUD（屏幕空间；指令二十九/三十二/三十三）
// ---------------------------------------------------------------------------

void VisualRuntime::RenderHUD(const WorldClientController& world, const std::string& playerName,
                              float viewportWidth, float viewportHeight, bool mapDebug) {
    if (!m_ready || m_spriteShader == nullptr) {
        return;
    }
    m_uiDrawCallBase = m_uiBatch.GetDrawCallCount();
    // Camera2D 位置语义 = 视口中心的世界坐标 → identity 变换需将位置设为视口中心
    //（否则屏幕坐标整体偏移 viewportCenter，HUD 面板跑到画面中央）。
    m_identityCamera.SetPosition({viewportWidth * 0.5f, viewportHeight * 0.5f});
    m_identityCamera.SetZoom(1.0f);
    m_uiBatch.Begin(*m_spriteShader, m_identityCamera, viewportWidth, viewportHeight);
    const bool haveText = m_text.IsReady();

    // ---- 左上：头像 + 名字 + 等级 + HP/Mana ----
    DrawPanel(m_uiBatch, {8.0f, 8.0f}, 240.0f, 92.0f);
    {
        const int classId = m_localClassId != 0 ? m_localClassId : 1;
        const visual::VisualEntityDef* def = m_catalog->FindPlayerEntityByClass(classId);
        if (def != nullptr && !def->portraitAsset.empty()) {
            auto portrait = m_assets.GetTexture(def->portraitAsset, nullptr, "portrait");
            if (portrait != nullptr && portrait->IsValid()) {
                const float scale = 56.0f / static_cast<float>(portrait->GetWidth());
                m_uiBatch.DrawQuad(*portrait, {40.0f, 44.0f}, {scale, scale}, 0.0f,
                                   Color(1, 1, 1, 1));
            }
        }
        if (haveText) {
            const std::string displayName =
                playerName.empty() ? (m_localName.empty() ? std::string("Player") : m_localName)
                                   : playerName;
            m_text.DrawString(m_uiBatch, {78.0f, 14.0f}, displayName, 15.0f, kTextWhite);
            char levelText[64];
            std::snprintf(levelText, sizeof(levelText), "Lv.%u  Gold %lld",
                          static_cast<unsigned>(world.LocalLevel()),
                          static_cast<long long>(world.LocalGold()));
            m_text.DrawString(m_uiBatch, {78.0f, 34.0f}, levelText, 12.0f, kTextGray);
        }
        const float hpPct =
            world.LocalMaxHp() > 0
                ? static_cast<float>(world.LocalCurrentHp()) / static_cast<float>(world.LocalMaxHp())
                : 0.0f;
        DrawBar(m_uiBatch, {78.0f, 54.0f}, 160.0f, 12.0f, hpPct,
                hpPct > 0.35f ? Color(0.30f, 0.80f, 0.35f, 0.95f) : kHpFill);
        if (haveText) {
            char hpText[64];
            std::snprintf(hpText, sizeof(hpText), "HP %u/%u",
                          static_cast<unsigned>(world.LocalCurrentHp()),
                          static_cast<unsigned>(world.LocalMaxHp()));
            m_text.DrawString(m_uiBatch, {84.0f, 55.0f}, hpText, 11.0f, kTextWhite);
        }
        const float manaPct =
            world.LocalMaxMana() > 0
                ? static_cast<float>(world.LocalCurrentMana()) /
                      static_cast<float>(world.LocalMaxMana())
                : 0.0f;
        DrawBar(m_uiBatch, {78.0f, 72.0f}, 160.0f, 10.0f, manaPct, kManaFill);
        if (haveText) {
            char manaText[64];
            std::snprintf(manaText, sizeof(manaText), "MP %u/%u",
                          static_cast<unsigned>(world.LocalCurrentMana()),
                          static_cast<unsigned>(world.LocalMaxMana()));
            m_text.DrawString(m_uiBatch, {84.0f, 72.5f}, manaText, 10.0f, kTextWhite);
        }
    }

    // ---- 底部：Skill Bar 1001/1002/1003（指令三十二）----
    {
        const std::uint32_t slotSkills[3] = {world::kSkillIdQuickStrike, world::kSkillIdFireBolt,
                                             world::kSkillIdWhirlwind};
        const char* slotIcons[3] = {"ui_icon_skill_1001", "ui_icon_skill_1002",
                                    "ui_icon_skill_1003"};
        const float slotSize = 50.0f;
        const float gap = 8.0f;
        const float totalW = slotSize * 3.0f + gap * 2.0f;
        const float startX = (viewportWidth - totalW) * 0.5f;
        const float slotY = viewportHeight - slotSize - 12.0f;
        for (int i = 0; i < 3; ++i) {
            const Vector2 topLeft(startX + static_cast<float>(i) * (slotSize + gap), slotY);
            DrawPanel(m_uiBatch, topLeft, slotSize, slotSize, 0.75f);
            auto icon = m_assets.GetTexture(slotIcons[i], nullptr, "skillbar");
            if (icon != nullptr && icon->IsValid()) {
                const float iconSize = slotSize - 10.0f;
                const float scale = iconSize / static_cast<float>(icon->GetWidth());
                m_uiBatch.DrawQuad(*icon, topLeft + Vector2(slotSize * 0.5f, slotSize * 0.5f),
                                   {scale, scale}, 0.0f, Color(1, 1, 1, 1));
            }
            // Mana 不足：蓝色遮罩（服务器仍权威决定能否释放）
            const visual::SkillDisplay* display = m_catalog->FindSkillDisplay(slotSkills[i]);
            if (display != nullptr &&
                world.LocalCurrentMana() < display->manaCost) {
                m_uiBatch.DrawQuad(*m_whiteTexture,
                                   topLeft + Vector2(slotSize * 0.5f, slotSize * 0.5f),
                                   {(slotSize - 4.0f) / 64.0f, (slotSize - 4.0f) / 64.0f}, 0.0f,
                                   Color(0.20f, 0.35f, 0.85f, 0.45f));
            }
            // CD 遮罩（自上而下收缩）
            const auto cdIt = m_skillCooldowns.find(slotSkills[i]);
            if (cdIt != m_skillCooldowns.end() && display != nullptr &&
                display->cooldownSeconds > 0.0f) {
                const float pct =
                    std::clamp(cdIt->second / display->cooldownSeconds, 0.0f, 1.0f);
                const float maskH = (slotSize - 4.0f) * pct;
                m_uiBatch.DrawQuad(*m_whiteTexture,
                                   topLeft + Vector2(slotSize * 0.5f, 2.0f + maskH * 0.5f),
                                   {(slotSize - 4.0f) / 64.0f, maskH / 64.0f}, 0.0f,
                                   Color(0.05f, 0.05f, 0.08f, 0.65f));
                if (haveText) {
                    char cdText[32];
                    std::snprintf(cdText, sizeof(cdText), "%.1f", cdIt->second);
                    m_text.DrawString(m_uiBatch, topLeft + Vector2(slotSize * 0.5f, slotSize * 0.4f),
                                    cdText, 13.0f, kTextWhite, false, true);
                }
            }
            if (haveText) {
                char keyText[8];
                std::snprintf(keyText, sizeof(keyText), "%d", i + 1);
                m_text.DrawString(m_uiBatch, topLeft + Vector2(4.0f, 2.0f), keyText, 11.0f,
                                kTextGold);
            }
        }
    }

    // ---- 右上：地图名（指令三十三）----
    if (haveText) {
        const std::string& mapName = world.MapModel().CurrentMapName();
        m_text.DrawStringShadow(m_uiBatch, {viewportWidth - 12.0f, 12.0f},
                              mapName.empty() ? std::string("Unknown Map") : mapName, 18.0f,
                              kTextWhite, true, false);
    }

    // ---- 右侧：Quest Tracker（指令二十九）----
    if (!world.Quests().All().empty()) {
        float trackerY = 60.0f;
        const float trackerX = viewportWidth - 262.0f;
        int shown = 0;
        for (const auto& [questId, state] : world.Quests().All()) {
            if (state.state != world::QuestState::InProgress &&
                state.state != world::QuestState::ReadyToTurnIn) {
                continue;
            }
            if (shown == 0) {
                DrawPanel(m_uiBatch, {trackerX, trackerY - 6.0f}, 250.0f, 26.0f, 0.6f);
                if (haveText) {
                    m_text.DrawString(m_uiBatch, {trackerX + 8.0f, trackerY - 4.0f},
                                    "Quest Tracker", 13.0f, kTextGold);
                }
                trackerY += 24.0f;
            }
            const visual::QuestDisplay* display = m_catalog->FindQuestDisplay(questId);
            std::uint32_t currentSum = 0;
            std::uint32_t requiredSum = 0;
            for (const auto& [objectiveId, progress] : state.objectives) {
                (void)objectiveId;
                currentSum += progress.current;
                requiredSum += progress.required;
            }
            char line[160];
            std::snprintf(line, sizeof(line), "%s  %u/%u",
                          display != nullptr ? display->name.c_str() : "Quest",
                          static_cast<unsigned>(currentSum), static_cast<unsigned>(requiredSum));
            DrawPanel(m_uiBatch, {trackerX, trackerY - 4.0f}, 250.0f, 24.0f, 0.45f);
            if (haveText) {
                m_text.DrawString(m_uiBatch, {trackerX + 8.0f, trackerY},
                                line, 12.0f,
                                state.state == world::QuestState::ReadyToTurnIn ? kTextGold
                                                                                : kTextWhite);
            }
            trackerY += 26.0f;
            if (++shown >= 6) {
                break;
            }
        }
    }

    // ---- F9：性能/视觉统计（指令四十二）----
    if (mapDebug && haveText) {
        char statsText[192];
        std::snprintf(statsText, sizeof(statsText),
                      "Sprites %d | DrawCalls %d | Textures %d | FX %d",
                      m_stats.visibleSprites, m_stats.drawCalls, m_stats.texturesLoaded,
                      m_stats.effectsActive);
        m_text.DrawString(m_uiBatch, {12.0f, viewportHeight - 76.0f}, statsText, 13.0f,
                        kTextGold);
        char posText[96];
        std::snprintf(posText, sizeof(posText), "Pos %.0f,%.0f | map %u",
                      world.ServerPositionX(), world.ServerPositionY(),
                      static_cast<unsigned>(world.MapModel().CurrentMapId()));
        m_text.DrawString(m_uiBatch, {12.0f, viewportHeight - 58.0f}, posText, 13.0f, kTextGold);
    }

    m_uiBatch.End();
    m_stats.drawCalls += m_uiBatch.GetDrawCallCount() - m_uiDrawCallBase;
}

// ---------------------------------------------------------------------------
// F10 热重载（指令三十四）
// ---------------------------------------------------------------------------

void VisualRuntime::ReloadAssets() {
    if (!m_ready) {
        return;
    }
    LOG_INFO("[VisualRuntime] F10 asset reload requested.");
    auto fresh = std::make_unique<legend::visual::VisualDataCatalog>();
    const std::string root = legend::visual::VisualDataCatalog::FindDataRoot();
    std::string error;
    if (root.empty() || !fresh->Load(root, error)) {
        // 失败保留旧资源（禁止崩溃/黑屏）
        LOG_ERROR("[VisualRuntime] reload failed — keeping old assets. reason: " + error);
        return;
    }
    m_catalog = std::move(fresh);
    m_assets.Initialize(*m_resources, m_catalog->Manifest());
    m_currentMapVisual = nullptr;
    m_currentVisualMapId.clear();
    // 重建实体 clip 表（visualId 定义可能变化）
    for (auto& [id, ev] : m_playerVisuals) {
        ApplyEntityDefinition(ev);
    }
    for (auto& [id, ev] : m_monsterVisuals) {
        ApplyEntityDefinition(ev);
    }
    for (auto& [id, ev] : m_npcVisuals) {
        ApplyEntityDefinition(ev);
    }
    for (auto& [id, ev] : m_portalVisuals) {
        ApplyEntityDefinition(ev);
    }
    const int textures = m_assets.ReloadAll();
    LOG_INFO("[VisualRuntime] reload done: manifest/animations/entities/effects reloaded, " +
             std::to_string(textures) + " texture(s).");
}

} // namespace legend::client

