#include "Client/Visuals/VisualRuntime.h"

#include "Client/Ui/CharacterVisualCatalog.h"
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
#include "Shared/Item/ItemTypes.h"
#include "Shared/Monster/MonsterTypes.h"
#include "Shared/Quest/QuestTypes.h"
#include "Shared/Skill/SkillTypes.h"
#include "Shared/Shop/ShopTypes.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

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

// 阶段25：本地客户端设置文件（savedata/client_settings.json；指令五十一/六）。
constexpr const char* kClientSettingsPath = "savedata/client_settings.json";

// 常见服务器错误码 -> 玩家可读 Toast 文案（指令五十五：不能只打印 Console）。
std::string SkillErrorText(std::uint8_t code) {
    switch (static_cast<world::SkillResultCode>(code)) {
        case world::SkillResultCode::Cooldown: return "Skill is on cooldown";
        case world::SkillResultCode::NotEnoughMana: return "Not enough mana";
        case world::SkillResultCode::OutOfRange: return "Target too far";
        case world::SkillResultCode::InvalidTarget: return "Invalid target";
        case world::SkillResultCode::TargetDead: return "Target is dead";
        case world::SkillResultCode::AlreadyCasting: return "Already casting";
        default: return "Cannot use skill";
    }
}

std::string ItemErrorText(std::uint8_t code) {
    switch (static_cast<world::ItemResultCode>(code)) {
        case world::ItemResultCode::InventoryFull: return "Inventory full";
        case world::ItemResultCode::TooFar: return "Too far away";
        case world::ItemResultCode::NotVisible: return "Item not visible";
        case world::ItemResultCode::OwnerLocked: return "Item is owned by another player";
        case world::ItemResultCode::DropNotFound: return "Item is gone";
        case world::ItemResultCode::Dead: return "Cannot do that while dead";
        default: return "Item action failed";
    }
}

std::string QuestErrorText(std::uint8_t code) {
    switch (static_cast<world::QuestResultCode>(code)) {
        case world::QuestResultCode::LevelTooLow: return "Level too low for this quest";
        case world::QuestResultCode::PrerequisiteNotMet: return "Complete the previous quest first";
        case world::QuestResultCode::AlreadyAccepted: return "Quest already accepted";
        case world::QuestResultCode::AlreadyCompleted: return "Quest already completed";
        case world::QuestResultCode::QuestLogFull: return "Quest log is full";
        case world::QuestResultCode::NotAccepted: return "Quest not accepted";
        case world::QuestResultCode::NotReady: return "Quest objectives not complete";
        case world::QuestResultCode::InventoryFull: return "Inventory full (reward lost?)";
        default: return "Quest action failed";
    }
}

std::string ShopErrorText(std::uint8_t code) {
    switch (static_cast<world::ShopResultCode>(code)) {
        case world::ShopResultCode::TooFar: return "Too far from merchant";
        case world::ShopResultCode::CannotBuy: return "Cannot buy this item";
        case world::ShopResultCode::SessionExpired: return "Shop session expired";
        case world::ShopResultCode::ItemNotInShop: return "Item not sold here";
        default: return "Shop action failed";
    }
}

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

    // 阶段25：物品展示目录（items.json 展示字段；失败不致命——空名/fallback 图标）。
    {
        std::string itemError;
        if (!m_itemDisplay.Load(dataRoot, itemError)) {
            LOG_WARN("[VisualRuntime] item display catalog unavailable: " + itemError);
        }
    }
    // 阶段25：章节展示目录（chapters.json；失败不致命——无 Chapter Complete 提示）。
    {
        std::string chapterError;
        if (!m_chapterDisplay.Load(dataRoot, chapterError)) {
            LOG_WARN("[VisualRuntime] chapter display catalog unavailable: " + chapterError);
        }
    }
    // 阶段25：技能槽显示状态（1001/1002/1003；展示值来自 skills.json，服务器仍权威）。
    {
        const std::uint32_t slotSkills[3] = {world::kSkillIdQuickStrike, world::kSkillIdFireBolt,
                                             world::kSkillIdWhirlwind};
        const char* slotIcons[3] = {"ui_icon_skill_1001", "ui_icon_skill_1002",
                                    "ui_icon_skill_1003"};
        for (int i = 0; i < 3; ++i) {
            m_skillSlots[i].unlocked = true;
            m_skillSlots[i].iconAsset = slotIcons[i];
            const visual::SkillDisplay* display = m_catalog->FindSkillDisplay(slotSkills[i]);
            if (display != nullptr) {
                m_skillSlots[i].name = display->name;
                m_skillSlots[i].manaCost = display->manaCost;
                m_skillSlots[i].cooldownTotal = display->cooldownSeconds;
            }
        }
    }
    LoadClientSettings();

    // 阶段25 指令四十八：Audio Runtime（失败不致命——静默 fallback）。
    if (!m_audio.Initialize()) {
        LOG_WARN("[VisualRuntime] audio runtime unavailable — running silent.");
    } else {
        m_audio.SetVolumes(m_masterVolume, m_musicVolume, m_sfxVolume);
    }

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
    m_audio.Shutdown();
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

void VisualRuntime::OnWorldEvent(const WorldNetworkEvent& event,
                                 const WorldClientController& world) {
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
            // 阶段25 指令三十六：地图名横幅（淡入淡出约 2s）。
            if (!event.mapChanged.mapName.empty()) {
                m_mapBanner.Show(event.mapChanged.mapName);
            }
            // 阶段25 指令四十九：每图 BGM。
            m_audio.PlayBgmForMap(event.mapChanged.mapId);
            break;
        }
        case WorldNetworkEvent::Type::RewardGrantedEvent: {
            // 阶段25 指令二十五/二十六：+X Gold / +X EXP 反馈。
            if (event.progression.goldGranted > 0) {
                m_toasts.Push(ui::ToastLevel::Success,
                              "+" + std::to_string(event.progression.goldGranted) + " Gold");
                m_audio.PlaySfx(SfxId::Gold);
            }
            if (event.progression.expGranted > 0) {
                m_toasts.Push(ui::ToastLevel::Info,
                              "+" + std::to_string(event.progression.expGranted) + " EXP");
            }
            break;
        }
        case WorldNetworkEvent::Type::LevelUpEvent: {
            // 阶段25 指令二十三：屏幕中央 LEVEL UP!（光环为本地视觉，不影响服务器）。
            m_levelUpFx.Trigger();
            m_audio.PlaySfx(SfxId::LevelUp);
            m_toasts.Push(ui::ToastLevel::Success,
                          "Level Up! Now level " + std::to_string(event.progression.level));
            break;
        }
        case WorldNetworkEvent::Type::QuestStateChangedEvent: {
            // 阶段25 指令二十二/四十七：任务完成反馈 + Chapter 完成判定。
            const auto newState = static_cast<world::QuestState>(event.questState);
            if (newState == world::QuestState::ReadyToTurnIn) {
                m_toasts.Push(ui::ToastLevel::Warning, "Quest Ready to Turn In");
            } else if (newState == world::QuestState::Completed) {
                m_toasts.Push(ui::ToastLevel::Success, "Quest Complete");
                m_audio.PlaySfx(SfxId::QuestComplete);
                // 阶段25：Chapter Complete 数据驱动（chapters.json finalQuestId 命中）。
                std::string chapterTitle;
                if (m_chapterDisplay.ChapterCompletion(event.questId, chapterTitle)) {
                    m_toasts.Push(ui::ToastLevel::Success,
                                  "Chapter Complete: " + chapterTitle);
                    m_toasts.Push(ui::ToastLevel::Info, "More content coming soon");
                }
            }
            break;
        }
        case WorldNetworkEvent::Type::QuestRewardGrantedEvent: {
            // 阶段25 指令二十二：Turn In 成功 -> Quest Complete + Rewards。
            std::string rewardText = "Rewards:";
            if (event.questRewardExp > 0) {
                rewardText += " " + std::to_string(event.questRewardExp) + " EXP";
            }
            if (event.questRewardGold > 0) {
                rewardText += " " + std::to_string(event.questRewardGold) + " Gold";
            }
            if (event.questRewardItemDefinitionId != 0) {
                const std::string itemName =
                    m_itemDisplay.DisplayName(event.questRewardItemDefinitionId);
                rewardText += " " + (itemName.empty() ? "Item" : itemName);
            }
            m_toasts.Push(ui::ToastLevel::Success, rewardText);
            break;
        }
        case WorldNetworkEvent::Type::SkillCastResponseEvent: {
            // 服务器接受施法 → HUD CD 显示（展示值来自本地 skills.json，服务器仍权威）。
            if (event.accepted && event.skillId != 0) {
                const visual::SkillDisplay* display = m_catalog->FindSkillDisplay(event.skillId);
                if (display != nullptr && display->cooldownSeconds > 0.0f) {
                    m_skillCooldowns[event.skillId] = display->cooldownSeconds;
                }
            } else if (!event.accepted) {
                // 阶段25 指令二十九/三十：Mana 不足 / CD 反馈（Toast + 槽位红闪）。
                m_toasts.Push(ui::ToastLevel::Error, SkillErrorText(event.skillResultCode));
                m_audio.PlaySfx(SfxId::Error);
                for (auto& slot : m_skillSlots) {
                    slot.errorFlash = 0.6f;
                }
            }
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
            // 阶段25 指令三十四：本地玩家攻击 Boss -> 顶部 Boss Bar（远离/死亡自动隐藏）。
            if (event.attackerType == kEntityTypePlayer &&
                event.attackerId == m_localCharacterId &&
                event.targetType == kEntityTypeMonster) {
                const auto* monster = world.RemoteMonsters().Find(event.targetId);
                if (monster != nullptr &&
                    monster->MonsterTypeId() == world::kAncientGuardianTypeId) {
                    m_bossBar.ShowBoss(event.targetId, monster->Name(),
                                       event.targetHpAfter != 0 ? event.targetHpAfter
                                                                : monster->CurrentHp(),
                                       event.targetMaxHp != 0 ? event.targetMaxHp
                                                              : monster->MaxHp());
                }
            }
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
        case WorldNetworkEvent::Type::InventoryDeltaEvent: {
            // 阶段25 指令五十：拾取 SFX（opcode 1 = Set/新增）。
            if (event.inventoryOpcode == 1) {
                m_audio.PlaySfx(SfxId::ItemPickup);
            }
            break;
        }
        case WorldNetworkEvent::Type::HealthSnapshot: {
            // 阶段25：Boss Bar 血量跟随（服务器快照纠偏）。
            if (m_bossBar.Visible() && event.entityType == kEntityTypeMonster &&
                event.entityId == m_bossBar.EntityId()) {
                m_bossBar.UpdateBoss(event.entityId, event.currentHp, event.maxHp);
                if (!event.alive || event.currentHp == 0) {
                    m_bossBar.Hide();
                }
            }
            break;
        }
        case WorldNetworkEvent::Type::MonsterDeath: {
            // 阶段25：Boss 死亡 -> 隐藏 Boss Bar。
            if (m_bossBar.Visible() && event.monsterEntityId == m_bossBar.EntityId()) {
                m_bossBar.Hide();
            }
            break;
        }
        case WorldNetworkEvent::Type::SkillCastStartedEvent: {
            // 阶段25 指令五十：技能 SFX（仅本地玩家施法，避免嘈杂）。
            if (event.characterId == m_localCharacterId) {
                if (event.skillId == world::kSkillIdQuickStrike) {
                    m_audio.PlaySfx(SfxId::SwordAttack);
                } else if (event.skillId == world::kSkillIdFireBolt) {
                    m_audio.PlaySfx(SfxId::FireBolt);
                } else if (event.skillId == world::kSkillIdWhirlwind) {
                    m_audio.PlaySfx(SfxId::Whirlwind);
                }
            }
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
    m_worldTimeSeconds += deltaTime;
    // 阶段25：UI 瞬态推进（Toast/Banner/LevelUp/BossBar/技能槽/设置持久化）。
    UpdateUiState(world, deltaTime);

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
            // Stage26 指令十一：大厅选中的造型优先（服务器持久化 visualId）；
            // 无覆盖时回退 classId 推导（AutoEnter/旧链路）。
            if (m_localVisualOverride != 0) {
                local.visualId = legend::ui::CharacterVisualEntityName(m_localVisualOverride);
                ApplyEntityDefinition(local);
            } else {
                const int classId = m_localClassId != 0 ? m_localClassId : 1;
                const visual::VisualEntityDef* def = m_catalog->FindPlayerEntityByClass(classId);
                if (def != nullptr) {
                    local.visualId = def->visualId;
                    ApplyEntityDefinition(local);
                }
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
    // 阶段26 指令十一：造型底色（def tint "#RRGGBB[AA]"，空=白）——死亡/受击覆盖。
    if (!def->tint.empty()) {
        bool ok = true;
        const auto clamp01 = [](int v) { return std::clamp(v, 0, 255) / 255.0f; };
        const auto hexVal = [&](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        const std::string& hex = def->tint;
        if ((hex.size() == 7 || hex.size() == 9) && hex[0] == '#') {
            auto pairAt = [&](std::size_t i) -> int {
                const int hi = hexVal(hex[i]);
                const int lo = hexVal(hex[i + 1]);
                if (hi < 0 || lo < 0) {
                    ok = false;
                    return 0;
                }
                return hi * 16 + lo;
            };
            const int r = pairAt(1), g = pairAt(3), b = pairAt(5);
            tint = ok ? Color(clamp01(r), clamp01(g), clamp01(b),
                              hex.size() == 9 ? clamp01(pairAt(7)) : 1.0f)
                      : Color(1.0f, 1.0f, 1.0f, 1.0f);
        } else {
            ok = false;
        }
        (void)ok; // 非法 tint 回退白色
    }
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
// ---------------------------------------------------------------------------
// 阶段25 指令五十三/五十四：Loading 覆盖层（登录→选角→进世界 链路提示）。
// ---------------------------------------------------------------------------

void VisualRuntime::RenderLoading(float viewportWidth, float viewportHeight) {
    if (!m_ready || m_spriteShader == nullptr || !m_text.IsReady()) {
        return;
    }
    m_uiDrawCallBase = m_uiBatch.GetDrawCallCount();
    m_identityCamera.SetPosition({viewportWidth * 0.5f, viewportHeight * 0.5f});
    m_identityCamera.SetZoom(1.0f);
    m_uiBatch.Begin(*m_spriteShader, m_identityCamera, viewportWidth, viewportHeight);
    const ui::UiTheme& theme = ui::DefaultUiTheme();
    const float scale = ui::UiTheme::ScaleFor(viewportWidth, viewportHeight);
    // 半透明暗幕 + Loading...（资源预载快时不强制黑屏——轻提示）。
    m_uiBatch.DrawQuad(*m_whiteTexture, {viewportWidth * 0.5f, viewportHeight * 0.5f},
                       {viewportWidth / 64.0f, viewportHeight / 64.0f}, 0.0f,
                       Color(0.0f, 0.0f, 0.0f, 0.45f));
    m_text.DrawStringShadow(m_uiBatch, {viewportWidth * 0.5f, viewportHeight * 0.5f},
                            "Loading...", 26.0f * scale, theme.textPrimary, true, true);
    m_uiBatch.End();
    m_stats.drawCalls += m_uiBatch.GetDrawCallCount() - m_uiDrawCallBase;
}

// ---------------------------------------------------------------------------
// 阶段25 正式 UI（指令十六~六十三）：HUD V2 / SkillBar / Tracker / BossBar /
// MiniMap / Toast / MapBanner / LevelUp / Dialogue / Shop / Inventory /
// Character / Settings。布局全部经 UiTheme 参考分辨率（1920×1080）缩放。
// ---------------------------------------------------------------------------

void VisualRuntime::LoadClientSettings() {
    std::ifstream file(kClientSettingsPath, std::ios::binary);
    if (!file) {
        return; // 首次启动：默认值（新手提示未读过 -> 显示）
    }
    auto text = std::string((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    auto json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return;
    }
    const auto readF = [&](const char* key, float fallback) {
        const auto it = json.find(key);
        return it != json.end() && it->is_number() ? it->get<float>() : fallback;
    };
    const auto readB = [&](const char* key, bool fallback) {
        const auto it = json.find(key);
        return it != json.end() && it->is_boolean() ? it->get<bool>() : fallback;
    };
    const auto readI = [&](const char* key, int fallback) {
        const auto it = json.find(key);
        return it != json.end() && it->is_number_integer() ? it->get<int>() : fallback;
    };
    m_masterVolume = readF("masterVolume", m_masterVolume);
    m_musicVolume = readF("musicVolume", m_musicVolume);
    m_sfxVolume = readF("sfxVolume", m_sfxVolume);
    m_fullscreen = readB("fullscreen", m_fullscreen);
    m_resolutionIndex = readI("resolutionIndex", m_resolutionIndex);
    m_tutorialShown = readB("tutorialShown", m_tutorialShown);
}

void VisualRuntime::SaveClientSettings() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories("savedata", ec);
    nlohmann::json json;
    json["masterVolume"] = m_masterVolume;
    json["musicVolume"] = m_musicVolume;
    json["sfxVolume"] = m_sfxVolume;
    json["fullscreen"] = m_fullscreen;
    json["resolutionIndex"] = m_resolutionIndex;
    json["tutorialShown"] = m_tutorialShown;
    std::ofstream file(kClientSettingsPath, std::ios::binary | std::ios::trunc);
    if (file) {
        file << json.dump(2);
    }
    m_settingsDirty = false;
}

std::vector<VisualRuntime::UiRequest> VisualRuntime::DrainUiRequests() {
    std::vector<UiRequest> out;
    out.swap(m_uiRequests);
    return out;
}

void VisualRuntime::PushErrorToast(std::uint16_t errorCode) {
    // 常见服务器错误（指令五十五）：TooFar/NotVisible/Cooldown/NoMana/InventoryFull/
    // NotEnoughGold/LevelTooLow -> 短 Toast。
    switch (errorCode) {
        case 8: m_toasts.Push(ui::ToastLevel::Error, "Too far away"); break;      // TooFar
        case 7: m_toasts.Push(ui::ToastLevel::Error, "Not visible"); break;       // NotVisible
        case 10: m_toasts.Push(ui::ToastLevel::Error, "Inventory full"); break;   // InventoryFull
        case 4: m_toasts.Push(ui::ToastLevel::Error, "Level too low"); break;     // LevelTooLow
        case 11: m_toasts.Push(ui::ToastLevel::Error, "Not enough gold"); break;  // NotEnoughGold
        default: break;
    }
}

void VisualRuntime::UpdateUiState(const WorldClientController& world, float deltaTime) {
    m_toasts.Update(deltaTime);
    m_mapBanner.Update(deltaTime);
    m_levelUpFx.Update(deltaTime);
    m_inventoryClickTimer += deltaTime;
    // 指令五十一：音量实时生效（Settings 拖动立即应用）。
    m_audio.SetVolumes(m_masterVolume, m_musicVolume, m_sfxVolume);
    for (auto& slot : m_skillSlots) {
        if (slot.errorFlash > 0.0f) {
            slot.errorFlash -= deltaTime;
        }
    }
    // 技能槽 CD 值（m_skillCooldowns 以 skillId 为键——按槽位映射）。
    const std::uint32_t slotSkills[3] = {world::kSkillIdQuickStrike, world::kSkillIdFireBolt,
                                         world::kSkillIdWhirlwind};
    for (int i = 0; i < 3; ++i) {
        const auto it = m_skillCooldowns.find(slotSkills[i]);
        m_skillSlots[i].cooldownRemaining = it != m_skillCooldowns.end() ? it->second : 0.0f;
        m_skillSlots[i].cooldownTotal =
            m_skillSlots[i].cooldownTotal > 0.0f ? m_skillSlots[i].cooldownTotal : 1.0f;
    }
    // Boss Bar 距离隐藏（指令三十四：距离过远隐藏）。
    if (m_bossBar.Visible()) {
        const auto* monster = world.RemoteMonsters().Find(m_bossBar.EntityId());
        if (monster == nullptr) {
            m_bossBar.Hide();
        } else {
            const float dx = monster->RenderX() - m_localVisualX;
            const float dy = monster->RenderY() - m_localVisualY;
            if (dx * dx + dy * dy > 1400.0f * 1400.0f) {
                m_bossBar.Hide();
            } else {
                m_bossBar.UpdateBoss(m_bossBar.EntityId(), monster->CurrentHp(),
                                     monster->MaxHp());
            }
        }
    }
    // 设置持久化（变更后延迟保存由 SetXxx 触发；此处只在 dirty 时落盘一次/秒级）。
    if (m_settingsDirty) {
        SaveClientSettings();
    }
}

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
    const float scale = ui::UiTheme::ScaleFor(viewportWidth, viewportHeight); // 指令六十二/六十三

    RenderHudV2(world, playerName, viewportWidth, viewportHeight, scale, haveText);
    RenderSkillBar(world, viewportWidth, viewportHeight, scale, haveText);
    RenderTracker(world, viewportWidth, scale, haveText);
    RenderMinimap(world, viewportWidth, viewportHeight, scale, haveText);
    RenderGlobalOverlays(world, viewportWidth, viewportHeight, scale, haveText);

    // ---- 窗口（Dialogue/Shop/Inventory/Character/Settings；鼠标交互）----
    {
        // 鼠标（raw 像素）→ 参考分辨率坐标。
        const math::Vector2 refMouse(m_lastMouseX / scale, m_lastMouseY / scale);
        RenderWindows(world, viewportWidth, viewportHeight, scale, haveText, refMouse,
                      m_lastMouseClicked);
        m_lastMouseClicked = false; // 消费本帧点击
    }

    // ---- F9：性能/视觉统计（指令四十二/五十七：默认关闭，F9 开发开关）----
    if (mapDebug && haveText) {
        const float s = scale;
        char statsText[192];
        std::snprintf(statsText, sizeof(statsText),
                      "Sprites %d | DrawCalls %d | Textures %d | FX %d",
                      m_stats.visibleSprites, m_stats.drawCalls, m_stats.texturesLoaded,
                      m_stats.effectsActive);
        m_text.DrawString(m_uiBatch, {12.0f * s, viewportHeight - 76.0f * s}, statsText,
                          13.0f * s, kTextGold);
        char posText[96];
        std::snprintf(posText, sizeof(posText), "Pos %.0f,%.0f | map %u",
                      world.ServerPositionX(), world.ServerPositionY(),
                      static_cast<unsigned>(world.MapModel().CurrentMapId()));
        m_text.DrawString(m_uiBatch, {12.0f * s, viewportHeight - 58.0f * s}, posText,
                          13.0f * s, kTextGold);
    }

    m_uiBatch.End();
    m_stats.drawCalls += m_uiBatch.GetDrawCallCount() - m_uiDrawCallBase;
}

void VisualRuntime::RenderHudV2(const WorldClientController& world, const std::string& playerName,
                                float viewportWidth, float viewportHeight, float scale,
                                bool haveText) {
    (void)viewportHeight;
    const ui::UiTheme& theme = ui::DefaultUiTheme();
    const float s = scale;

    // ---- 左上：头像 + Name + Lv + HP/Mana/EXP + Gold/Atk/Def（指令二十七 V2）----
    const float panelW = 250.0f * s;
    const float panelH = 128.0f * s;
    DrawPanel(m_uiBatch, {8.0f, 8.0f}, panelW, panelH, 0.75f);
    {
        const int classId = m_localClassId != 0 ? m_localClassId : 1;
        const visual::VisualEntityDef* def = m_catalog->FindPlayerEntityByClass(classId);
        if (def != nullptr && !def->portraitAsset.empty()) {
            auto portrait = m_assets.GetTexture(def->portraitAsset, nullptr, "portrait");
            if (portrait != nullptr && portrait->IsValid()) {
                const float iconScale = 56.0f * s / static_cast<float>(portrait->GetWidth());
                m_uiBatch.DrawQuad(*portrait, {44.0f * s, 50.0f * s}, {iconScale, iconScale},
                                   0.0f, Color(1, 1, 1, 1));
            }
        }
        if (haveText) {
            const std::string displayName =
                playerName.empty() ? (m_localName.empty() ? std::string("Player") : m_localName)
                                   : playerName;
            m_text.DrawString(m_uiBatch, {84.0f * s, 14.0f * s}, displayName, 15.0f * s,
                              theme.textPrimary);
            char line[128];
            std::snprintf(line, sizeof(line), "Lv.%u", static_cast<unsigned>(world.LocalLevel()));
            m_text.DrawString(m_uiBatch, {84.0f * s, 34.0f * s}, line, 13.0f * s, theme.textDim);
        }
        // HP / Mana / EXP 三条（指令二十七：EXP Bar）。
        const float barX = 84.0f * s;
        const float barW = 156.0f * s;
        const float hpPct =
            world.LocalMaxHp() > 0
                ? static_cast<float>(world.LocalCurrentHp()) / static_cast<float>(world.LocalMaxHp())
                : 0.0f;
        DrawBar(m_uiBatch, {barX, 54.0f * s}, barW, 13.0f * s, hpPct, theme.hpFill);
        if (haveText) {
            char hpText[64];
            std::snprintf(hpText, sizeof(hpText), "HP %u/%u",
                          static_cast<unsigned>(world.LocalCurrentHp()),
                          static_cast<unsigned>(world.LocalMaxHp()));
            m_text.DrawString(m_uiBatch, {barX + 6.0f * s, 55.0f * s}, hpText, 10.0f * s,
                              theme.textPrimary);
        }
        const float manaPct =
            world.LocalMaxMana() > 0
                ? static_cast<float>(world.LocalCurrentMana()) /
                      static_cast<float>(world.LocalMaxMana())
                : 0.0f;
        DrawBar(m_uiBatch, {barX, 72.0f * s}, barW, 11.0f * s, manaPct, theme.manaFill);
        if (haveText) {
            char manaText[64];
            std::snprintf(manaText, sizeof(manaText), "MP %u/%u",
                          static_cast<unsigned>(world.LocalCurrentMana()),
                          static_cast<unsigned>(world.LocalMaxMana()));
            m_text.DrawString(m_uiBatch, {barX + 6.0f * s, 72.5f * s}, manaText, 9.0f * s,
                              theme.textPrimary);
        }
        const float expPct = world.LocalExpToNext() > 0
                                 ? static_cast<float>(world.LocalExperience()) /
                                       static_cast<float>(world.LocalExpToNext())
                                 : 0.0f;
        DrawBar(m_uiBatch, {barX, 88.0f * s}, barW, 7.0f * s, expPct, theme.expFill);
        // Gold / Attack / Defense（指令十五：装备后立即变化——数值来自服务器事件镜像）。
        if (haveText) {
            char line2[128];
            std::snprintf(line2, sizeof(line2), "Gold %lld   ATK %u   DEF %u",
                          static_cast<long long>(world.LocalGold()),
                          static_cast<unsigned>(world.LocalAttackPower()),
                          static_cast<unsigned>(world.LocalDefensePower()));
            m_text.DrawString(m_uiBatch, {20.0f * s, 108.0f * s}, line2, 12.0f * s,
                              theme.textGold);
        }
    }
}

void VisualRuntime::RenderSkillBar(const WorldClientController& world, float viewportWidth,
                                   float viewportHeight, float scale, bool haveText) {
    (void)world;
    const ui::UiTheme& theme = ui::DefaultUiTheme();
    const float s = scale;
    const float slotSize = 52.0f * s;
    const float gap = 8.0f * s;
    const float totalW = slotSize * 3.0f + gap * 2.0f;
    const float startX = (viewportWidth - totalW) * 0.5f;
    const float slotY = viewportHeight - slotSize - 12.0f * s;
    for (int i = 0; i < 3; ++i) {
        const ui::SkillSlotState& slot = m_skillSlots[i];
        const Vector2 topLeft(startX + static_cast<float>(i) * (slotSize + gap), slotY);
        DrawPanel(m_uiBatch, topLeft, slotSize, slotSize, 0.8f);
        auto icon = m_assets.GetTexture(slot.iconAsset, nullptr, "skillbar");
        if (icon != nullptr && icon->IsValid()) {
            const float iconScale = (slotSize - 10.0f * s) / static_cast<float>(icon->GetWidth());
            m_uiBatch.DrawQuad(*icon, topLeft + Vector2(slotSize * 0.5f, slotSize * 0.5f),
                               {iconScale, iconScale}, 0.0f, Color(1, 1, 1, 1));
        }
        // Mana 不足：蓝色遮罩（服务器仍权威）。
        if (slot.ManaInsufficient(world.LocalCurrentMana())) {
            m_uiBatch.DrawQuad(*m_whiteTexture,
                               topLeft + Vector2(slotSize * 0.5f, slotSize * 0.5f),
                               {(slotSize - 4.0f * s) / 64.0f, (slotSize - 4.0f * s) / 64.0f},
                               0.0f, Color(0.20f, 0.35f, 0.85f, 0.45f));
        }
        // CD 遮罩（自上而下收缩）+ 倒计时文本（指令二十八：槽位视觉倒计时）。
        if (slot.cooldownRemaining > 0.0f && slot.cooldownTotal > 0.0f) {
            const float pct = std::clamp(slot.cooldownRemaining / slot.cooldownTotal, 0.0f, 1.0f);
            const float maskH = (slotSize - 4.0f * s) * pct;
            m_uiBatch.DrawQuad(*m_whiteTexture,
                               topLeft + Vector2(slotSize * 0.5f, 2.0f * s + maskH * 0.5f),
                               {(slotSize - 4.0f * s) / 64.0f, maskH / 64.0f}, 0.0f,
                               Color(0.05f, 0.05f, 0.08f, 0.65f));
            if (haveText) {
                char cdText[32];
                std::snprintf(cdText, sizeof(cdText), "%.1f", slot.cooldownRemaining);
                m_text.DrawString(m_uiBatch,
                                  topLeft + Vector2(slotSize * 0.5f, slotSize * 0.4f), cdText,
                                  13.0f * s, theme.textPrimary, false, true);
            }
        }
        // 指令二十九/三十：失败红闪。
        if (slot.errorFlash > 0.0f) {
            const float a = std::clamp(slot.errorFlash / 0.6f, 0.0f, 1.0f) * 0.5f;
            m_uiBatch.DrawQuad(*m_whiteTexture,
                               topLeft + Vector2(slotSize * 0.5f, slotSize * 0.5f),
                               {(slotSize - 2.0f * s) / 64.0f, (slotSize - 2.0f * s) / 64.0f},
                               0.0f, Color(1.0f, 0.15f, 0.1f, a));
        }
        if (haveText) {
            char keyText[8];
            std::snprintf(keyText, sizeof(keyText), "%d", i + 1);
            m_text.DrawString(m_uiBatch, topLeft + Vector2(4.0f * s, 2.0f * s), keyText,
                              11.0f * s, theme.textGold);
        }
    }
}

void VisualRuntime::RenderTracker(const WorldClientController& world, float viewportWidth,
                                  float scale, bool haveText) {
    const ui::UiTheme& theme = ui::DefaultUiTheme();
    const float s = scale;
    // 指令二十一：右侧 Tracker 正式化，最多 3 个 Active Quest。
    std::vector<ui::QuestTrackerEntry> entries;
    for (const auto& [questId, state] : world.Quests().All()) {
        if (state.state != world::QuestState::InProgress &&
            state.state != world::QuestState::ReadyToTurnIn) {
            continue;
        }
        ui::QuestTrackerEntry entry;
        entry.questId = questId;
        const visual::QuestDisplay* display = m_catalog->FindQuestDisplay(questId);
        entry.title = display != nullptr ? display->name : ("Quest " + std::to_string(questId));
        entry.readyToTurnIn = state.state == world::QuestState::ReadyToTurnIn;
        // 目标文案（展示字段来自 quests.json；进度来自服务器事件镜像）。
        if (display != nullptr && !display->objectives.empty()) {
            const auto& objective = display->objectives.front();
            if (!state.objectives.empty()) {
                const auto& progress = state.objectives.begin()->second;
                entry.current = progress.current;
                entry.required = progress.required;
            }
            if (objective.type == "KillMonster") {
                entry.objectiveText = "Slay";
            } else if (objective.type == "CollectItem") {
                entry.objectiveText = "Collect";
            } else if (objective.type == "ReachLevel") {
                entry.objectiveText = "Reach level";
            } else if (objective.type == "ReachArea") {
                entry.objectiveText = "Explore";
            } else {
                entry.objectiveText = "Progress";
            }
        }
        entries.push_back(std::move(entry));
        if (entries.size() >= 3) {
            break;
        }
    }
    if (entries.empty()) {
        return;
    }
    const float trackerX = viewportWidth - 262.0f * s;
    float trackerY = 60.0f * s;
    DrawPanel(m_uiBatch, {trackerX, trackerY - 6.0f * s}, 250.0f * s, 26.0f * s, 0.6f);
    if (haveText) {
        m_text.DrawString(m_uiBatch, {trackerX + 8.0f * s, trackerY - 4.0f * s},
                          "Quest Tracker", 13.0f * s, theme.textGold);
    }
    trackerY += 24.0f * s;
    for (const auto& entry : entries) {
        char line[192];
        if (entry.readyToTurnIn) {
            std::snprintf(line, sizeof(line), "%s — Ready to Turn In!", entry.title.c_str());
        } else {
            std::snprintf(line, sizeof(line), "%s\n  %s %u/%u", entry.title.c_str(),
                          entry.objectiveText.c_str(), static_cast<unsigned>(entry.current),
                          static_cast<unsigned>(entry.required));
        }
        const float rowH = 34.0f * s;
        DrawPanel(m_uiBatch, {trackerX, trackerY - 4.0f * s}, 250.0f * s, rowH, 0.45f);
        if (haveText) {
            // 单行显示（标题 + 进度），换行手动画第二行。
            const std::string text(line);
            const auto nl = text.find('\n');
            m_text.DrawString(m_uiBatch, {trackerX + 8.0f * s, trackerY},
                              text.substr(0, nl == std::string::npos ? text.size() : nl),
                              12.0f * s,
                              entry.readyToTurnIn ? theme.textGold : theme.textPrimary);
            if (nl != std::string::npos) {
                m_text.DrawString(m_uiBatch, {trackerX + 20.0f * s, trackerY + 16.0f * s},
                                  text.substr(nl + 1), 11.0f * s, theme.textDim);
            }
        }
        trackerY += rowH + 4.0f * s;
    }
}

void VisualRuntime::RenderMinimap(const WorldClientController& world, float viewportWidth,
                                  float viewportHeight, float scale, bool haveText) {
    (void)haveText;
    const ui::UiTheme& theme = ui::DefaultUiTheme();
    const float s = scale;
    // 指令三十七/三十八：右上小地图（玩家/NPC/Portal/任务 NPC；只显示 AOI 已知实体）。
    const float size = 150.0f * s;
    const float x0 = viewportWidth - size - 12.0f;
    const float y0 = 40.0f * s;
    DrawPanel(m_uiBatch, {x0, y0}, size, size, 0.6f);
    m_uiBatch.DrawQuad(*m_whiteTexture, {x0 + size * 0.5f, y0 + size * 0.5f},
                       {size / 64.0f, size / 64.0f}, 0.0f, Color(0.10f, 0.16f, 0.10f, 0.85f));

    const ui::MinimapModel model = BuildMinimapModel(world);
    auto toScreen = [&](float wx, float wy) {
        return Vector2(x0 + model.NormalizeX(wx) * size, y0 + model.NormalizeY(wy) * size);
    };
    // NPC（任务 NPC 金色/其余白点）。
    for (const auto& blip : model.npcs) {
        const Vector2 p = toScreen(blip.x, blip.y);
        m_uiBatch.DrawQuad(*m_whiteTexture, p, {3.0f * s / 64.0f, 3.0f * s / 64.0f}, 0.0f,
                           blip.isQuestGiver ? theme.textGold : theme.textPrimary);
    }
    // Portal（蓝色点）。
    for (const auto& blip : model.portals) {
        const Vector2 p = toScreen(blip.x, blip.y);
        m_uiBatch.DrawQuad(*m_whiteTexture, p, {4.0f * s / 64.0f, 4.0f * s / 64.0f}, 0.0f,
                           theme.manaFill);
    }
    // 玩家（绿色点）。
    const Vector2 player = toScreen(model.playerX, model.playerY);
    m_uiBatch.DrawQuad(*m_whiteTexture, player, {4.0f * s / 64.0f, 4.0f * s / 64.0f}, 0.0f,
                       theme.textSuccess);
}

void VisualRuntime::RenderGlobalOverlays(const WorldClientController& world,
                                         float viewportWidth, float viewportHeight, float scale,
                                         bool haveText) {
    const ui::UiTheme& theme = ui::DefaultUiTheme();
    const float s = scale;
    // ---- Boss Bar（指令三十四：顶部 Boss Name + HP Bar）----
    if (m_bossBar.Visible()) {
        const float w = 520.0f * s;
        const float x = (viewportWidth - w) * 0.5f;
        const float y = 18.0f * s;
        DrawPanel(m_uiBatch, {x, y}, w, 40.0f * s, 0.75f);
        if (haveText) {
            m_text.DrawString(m_uiBatch, {viewportWidth * 0.5f, y + 4.0f * s}, m_bossBar.Name(),
                              14.0f * s, theme.textPrimary, false, true);
        }
        DrawBar(m_uiBatch, {x + 10.0f * s, y + 24.0f * s}, w - 20.0f * s, 10.0f * s,
                m_bossBar.HpPct(), theme.bossHpFill);
    }
    // ---- Map Enter Banner（指令三十六）----
    if (m_mapBanner.Active() && haveText) {
        const float a = m_mapBanner.Alpha();
        m_text.DrawStringShadow(m_uiBatch, {viewportWidth * 0.5f, viewportHeight * 0.30f},
                                m_mapBanner.Text(), 34.0f * s,
                                Color(theme.bannerText.r, theme.bannerText.g,
                                      theme.bannerText.b, a),
                                true, true);
    }
    // ---- Level Up（指令二十三：屏幕中央 LEVEL UP! 1.5~2s）----
    if (m_levelUpFx.Active() && haveText) {
        const float a = m_levelUpFx.Alpha();
        m_text.DrawStringShadow(m_uiBatch, {viewportWidth * 0.5f, viewportHeight * 0.42f},
                                "LEVEL UP!", 42.0f * s,
                                Color(theme.levelUpText.r, theme.levelUpText.g,
                                      theme.levelUpText.b, a),
                                true, true);
    }
    // ---- Toast（指令五十六：右下角，最大 5 条，自动淡出）----
    if (haveText) {
        float ty = viewportHeight - 100.0f * s;
        for (const auto& toast : m_toasts.Active()) {
            const Color base = toast.level == ui::ToastLevel::Success ? theme.toastSuccess
                               : toast.level == ui::ToastLevel::Warning ? theme.toastWarning
                               : toast.level == ui::ToastLevel::Error   ? theme.toastError
                                                                        : theme.toastInfo;
            const float alpha = toast.Alpha();
            const float w = 340.0f * s;
            const float h = 22.0f * s;
            m_uiBatch.DrawQuad(*m_whiteTexture,
                               {viewportWidth - w * 0.5f - 12.0f * s, ty + h * 0.5f},
                               {w / 64.0f, h / 64.0f}, 0.0f,
                               Color(base.r, base.g, base.b, 0.85f * alpha));
            m_text.DrawString(m_uiBatch, {viewportWidth - w - 22.0f * s, ty + 4.0f * s},
                              toast.text, 12.0f * s,
                              Color(theme.textPrimary.r, theme.textPrimary.g,
                                    theme.textPrimary.b, alpha));
            ty -= (h + 4.0f * s);
        }
    }
    // ---- 新手提示（指令六：第一次进入显示，仅一次，本地保存）----
    if (haveText && !m_tutorialShown && world.IsWorldReady()) {
        const char* hintLines[] = {
            "WASD / Arrow Keys = Move",
            "E = Interact    F = Portal",
            "1 / 2 / 3 = Skills",
            "I = Inventory   C = Character",
        };
        const float boxW = 300.0f * s;
        const float boxH = 96.0f * s;
        const float bx = (viewportWidth - boxW) * 0.5f;
        const float by = viewportHeight * 0.60f;
        DrawPanel(m_uiBatch, {bx, by}, boxW, boxH, 0.8f);
        float ly = by + 12.0f * s;
        for (const char* lineText : hintLines) {
            m_text.DrawString(m_uiBatch, {bx + 24.0f * s, ly}, lineText, 14.0f * s,
                              theme.textPrimary);
            ly += 22.0f * s;
        }
        // 20 秒后或按任意移动键（Update 侧判定）自动消失并保存。
        if (m_worldTimeSeconds > 20.0f) {
            m_tutorialShown = true;
            m_settingsDirty = true;
        }
    }
}

// ---- MiniMap 数据（指令三十七/三十八：只显示 AOI 已知实体 + 当前图过滤）----
ui::MinimapModel VisualRuntime::BuildMinimapModel(const WorldClientController& world) const {
    ui::MinimapModel model;
    const auto& mapModel = world.MapModel();
    model.playerX = world.ServerPositionX();
    model.playerY = world.ServerPositionY();
    model.minX = mapModel.MinX();
    model.minY = mapModel.MinY();
    model.maxX = mapModel.MaxX();
    model.maxY = mapModel.MaxY();
    const std::uint16_t mapId = mapModel.CurrentMapId();
    for (const auto& [npcEntityId, npc] : world.Npcs().All()) {
        if (npc.mapId != mapId) {
            continue;
        }
        model.npcs.push_back({npc.x, npc.y,
                              npc.questMarker != world::NpcQuestMarker::None});
    }
    for (const auto& [portalEntityId, portal] : world.Portals().All()) {
        if (portal.mapId != mapId || !portal.active) {
            continue;
        }
        model.portals.push_back({portal.x, portal.y, false});
    }
    return model;
}

void VisualRuntime::HandleUiMouse(const WorldClientController& world,
                                  const math::Vector2& refMouse, bool clicked, float scale) {
    (void)world;
    (void)refMouse;
    (void)clicked;
    (void)scale;
    // 预留：复杂命中测试集中在 RenderWindows 内联处理。
}

void VisualRuntime::RenderWindows(const WorldClientController& world, float viewportWidth,
                                  float viewportHeight, float scale, bool haveText,
                                  const math::Vector2& refMouse, bool mouseClicked) {
    const ui::UiTheme& theme = ui::DefaultUiTheme();
    const float s = scale;
    (void)viewportWidth;
    (void)viewportHeight;

    // 参考坐标 -> 屏幕像素。
    auto P = [&](float x, float y) { return Vector2(x * s, y * s); };
    auto inRect = [&](float x, float y, float w, float h) {
        return refMouse.x >= x && refMouse.x <= x + w && refMouse.y >= y && refMouse.y <= y + h;
    };
    auto button = [&](float x, float y, float w, float h, const std::string& label,
                      bool enabled) {
        const bool hover = enabled && inRect(x, y, w, h);
        const Color c = !enabled ? theme.buttonDisabled
                                 : hover ? theme.buttonHover : theme.buttonNormal;
        m_uiBatch.DrawQuad(*m_whiteTexture, P(x + w * 0.5f, y + h * 0.5f),
                           {w * s / 64.0f, h * s / 64.0f}, 0.0f, c);
        if (haveText && !label.empty()) {
            m_text.DrawString(m_uiBatch, P(x + 8.0f, y + h * 0.5f - 7.0f), label, 13.0f * s,
                              enabled ? theme.textPrimary : theme.textDim);
        }
        return enabled && hover && mouseClicked;
    };
    auto windowBg = [&](float x, float y, float w, float h, const std::string& title) {
        DrawPanel(m_uiBatch, P(x, y), w * s, h * s, 0.88f);
        if (haveText && !title.empty()) {
            m_text.DrawString(m_uiBatch, P(x + 12.0f, y + 8.0f), title, 16.0f * s,
                              theme.textGold);
        }
    };
    auto drawIcon = [&](const std::string& assetId, float cx, float cy, float sizePx) {
        if (assetId.empty()) {
            return;
        }
        auto tex = m_assets.GetTexture(assetId, nullptr, "ui-item");
        if (tex != nullptr && tex->IsValid()) {
            const float k = sizePx * s / static_cast<float>(tex->GetWidth());
            m_uiBatch.DrawQuad(*tex, P(cx, cy), {k, k}, 0.0f, Color(1, 1, 1, 1));
        } else {
            m_uiBatch.DrawQuad(*m_whiteTexture, P(cx, cy), {sizePx * s / 64.0f, sizePx * s / 64.0f},
                               0.0f, theme.panelSlotSelected);
        }
    };

    // ================= Dialogue（指令二十：NPC Name / Text / Options）=================
    if (world.Dialogue().Active()) {
        const float w = 460.0f;
        const float h = 300.0f;
        const float x = 60.0f;
        const float y = 380.0f;
        windowBg(x, y, w, h, world.Dialogue().Title());
        if (haveText) {
            m_text.DrawString(m_uiBatch, P(x + 12.0f, y + 34.0f), world.Dialogue().Text(),
                              13.0f * s, theme.textPrimary);
        }
        float oy = y + 90.0f;
        int idx = 1;
        for (const auto& option : world.Dialogue().Options()) {
            std::string label = std::to_string(idx) + ". " + option.label;
            // 指令七：交互提示语义（Shop/Travel/Quest）已在服务器 label 中。
            if (button(x + 14.0f, oy, w - 28.0f, 30.0f, label, true)) {
                UiRequest req;
                req.kind = UiRequest::Kind::DialogueOption;
                req.index = idx;
                m_uiRequests.push_back(req);
            }
            oy += 36.0f;
            ++idx;
            if (idx > 9) {
                break;
            }
        }
    }

    // ================= Shop（指令十九：Icon/Name/Price/Buy + Gold）=================
    if (world.Shop().Active()) {
        const float w = 420.0f;
        const float h = 420.0f;
        const float x = 940.0f;
        const float y = 180.0f;
        windowBg(x, y, w, h, "Shop");
        char gold[64];
        std::snprintf(gold, sizeof(gold), "Gold: %lld",
                      static_cast<long long>(world.LocalGold()));
        if (haveText) {
            m_text.DrawString(m_uiBatch, P(x + w - 140.0f, y + 10.0f), gold, 14.0f * s,
                              theme.textGold);
        }
        float ey = y + 40.0f;
        int entryIdx = 0;
        for (const auto& entry : world.Shop().Entries()) {
            const bool selected = m_selectedShopIndex == entryIdx;
            const bool hover = inRect(x + 10.0f, ey, w - 20.0f, 34.0f);
            if (hover && mouseClicked) {
                m_selectedShopIndex = entryIdx;
                mouseClicked = false;
            }
            m_uiBatch.DrawQuad(*m_whiteTexture, P(x + 10.0f + (w - 20.0f) * 0.5f, ey + 17.0f),
                               {(w - 20.0f) * s / 64.0f, 34.0f * s / 64.0f}, 0.0f,
                               selected ? theme.panelSlotSelected : theme.panelSlot);
            drawIcon(m_itemDisplay.IconAsset(entry.itemDefinitionId), x + 28.0f, ey + 17.0f, 24.0f);
            if (haveText) {
                char line[160];
                std::snprintf(line, sizeof(line), "%s  -  %u G%s",
                              m_itemDisplay.DisplayName(entry.itemDefinitionId).c_str(),
                              entry.buyPrice,
                              entry.canBuy ? "" : "  (cannot buy)");
                m_text.DrawString(m_uiBatch, P(x + 48.0f, ey + 9.0f), line, 13.0f * s,
                                  theme.textPrimary);
            }
            ey += 38.0f;
            ++entryIdx;
            if (entryIdx >= 8) {
                break;
            }
        }
        // Buy 选中条目（服务器权威）。
        if (button(x + w - 150.0f, y + h - 46.0f, 130.0f, 32.0f, "Buy", m_selectedShopIndex >= 0)) {
            UiRequest req;
            req.kind = UiRequest::Kind::ShopBuy;
            req.index = m_selectedShopIndex;
            m_uiRequests.push_back(req);
        }
        // Sell：卖出背包选中物品（Material/装备皆可——服务器验证价格）。
        if (button(x + w - 290.0f, y + h - 46.0f, 130.0f, 32.0f, "Sell",
                   m_selectedInventorySlot >= 0)) {
            const auto& slot = world.Inventory().Slot(
                static_cast<std::size_t>(m_selectedInventorySlot));
            if (slot.instanceId != 0) {
                UiRequest req;
                req.kind = UiRequest::Kind::ShopSell;
                req.instanceId = slot.instanceId;
                m_uiRequests.push_back(req);
            }
        }
    }

    // ================= Inventory（指令十七/十八：40 格 + Tooltip + 双击装备）====
    if (m_inventoryVisible) {
        const float w = 470.0f;
        const float h = 330.0f;
        const float x = 940.0f;
        const float y = 180.0f;
        windowBg(x, y, w, h, "Inventory (I)");
        const float cell = 44.0f;
        const float gap = 5.0f;
        for (std::size_t i = 0; i < ui::InventoryUiModel::kSlots; ++i) {
            const int col = static_cast<int>(i % 8);
            const int row = static_cast<int>(i / 8);
            const float cx = x + 14.0f + col * (cell + gap);
            const float cy = y + 40.0f + row * (cell + gap);
            const auto& slot = world.Inventory().Slot(i);
            const bool selected = m_selectedInventorySlot == static_cast<int>(i);
            const bool hover = inRect(cx, cy, cell, cell);
            Color bg = theme.panelSlot;
            const ui::ItemDisplay* display =
                slot.quantity > 0 ? m_itemDisplay.Find(slot.definitionId) : nullptr;
            if (display != nullptr && display->isEquipment()) {
                bg = Color(bg.r, bg.g * 0.8f, bg.b * 0.6f, bg.a); // 装备边框感（暖色）
            }
            m_uiBatch.DrawQuad(*m_whiteTexture, P(cx + cell * 0.5f, cy + cell * 0.5f),
                               {cell * s / 64.0f, cell * s / 64.0f}, 0.0f,
                               selected ? theme.panelSlotSelected : bg);
            if (slot.quantity > 0) {
                drawIcon(m_itemDisplay.IconAsset(slot.definitionId), cx + cell * 0.5f,
                         cy + cell * 0.5f, 28.0f);
                if (haveText && slot.quantity > 1) {
                    char qty[16];
                    std::snprintf(qty, sizeof(qty), "%u", slot.quantity);
                    m_text.DrawString(m_uiBatch, P(cx + cell - 16.0f, cy + cell - 16.0f), qty,
                                      11.0f * s, theme.textPrimary, false, true);
                }
            }
            if (hover && mouseClicked) {
                if (m_lastClickedInventorySlot == static_cast<int>(i) &&
                    m_inventoryClickTimer < 0.4f) {
                    // 指令十八：双击 Equip（服务器依旧权威）。
                    if (slot.quantity > 0 && display != nullptr && display->isEquipment()) {
                        UiRequest req;
                        req.kind = UiRequest::Kind::EquipDefinition;
                        req.definitionId = slot.definitionId;
                        m_uiRequests.push_back(req);
                    }
                    m_lastClickedInventorySlot = -1;
                } else {
                    m_selectedInventorySlot = static_cast<int>(i);
                    m_lastClickedInventorySlot = static_cast<int>(i);
                    m_inventoryClickTimer = 0.0f;
                }
                mouseClicked = false;
            }
        }
        // Tooltip（指令十七：Name/Type/Attack/Defense/Sell Price）。
        if (m_selectedInventorySlot >= 0) {
            const auto& slot =
                world.Inventory().Slot(static_cast<std::size_t>(m_selectedInventorySlot));
            if (slot.quantity > 0) {
                const ui::ItemDisplay* display = m_itemDisplay.Find(slot.definitionId);
                if (display != nullptr) {
                    const float tw = 200.0f;
                    const float th = 110.0f;
                    const float tx = x - tw - 12.0f;
                    const float ty = y + 40.0f;
                    windowBg(tx, ty, tw, th, display->name);
                    if (haveText) {
                        char line[128];
                        std::snprintf(line, sizeof(line), "Type: %s (%s)",
                                      display->type.c_str(), display->equipSlot.c_str());
                        m_text.DrawString(m_uiBatch, P(tx + 12.0f, ty + 34.0f), line, 12.0f * s,
                                          theme.textDim);
                        std::snprintf(line, sizeof(line), "ATK +%u   DEF +%u",
                                      display->attackBonus, display->defenseBonus);
                        m_text.DrawString(m_uiBatch, P(tx + 12.0f, ty + 54.0f), line, 12.0f * s,
                                          theme.textPrimary);
                        std::uint32_t sellPrice = 0;
                        bool havePrice = false;
                        for (const auto& entry : world.Shop().Entries()) {
                            if (entry.itemDefinitionId == slot.definitionId) {
                                sellPrice = entry.sellPrice;
                                havePrice = true;
                            }
                        }
                        std::snprintf(line, sizeof(line), "Sell: %s",
                                      havePrice ? std::to_string(sellPrice).c_str() : "N/A");
                        m_text.DrawString(m_uiBatch, P(tx + 12.0f, ty + 74.0f), line, 12.0f * s,
                                          theme.textGold);
                    }
                }
            }
        }
    }

    // ================= Character Panel（指令十六：C 键）=================
    if (m_characterVisible) {
        const float w = 320.0f;
        const float h = 400.0f;
        const float x = 60.0f;
        const float y = 140.0f;
        windowBg(x, y, w, h, "Character (C)");
        const int classId = m_localClassId != 0 ? m_localClassId : 1;
        const char* className = classId == 1 ? "Warrior" : classId == 2 ? "Mage" : "Taoist";
        if (haveText) {
            char line[160];
            std::snprintf(line, sizeof(line), "%s  (%s)",
                          m_localName.empty() ? "Player" : m_localName.c_str(), className);
            m_text.DrawString(m_uiBatch, P(x + 14.0f, y + 32.0f), line, 14.0f * s,
                              theme.textPrimary);
            std::snprintf(line, sizeof(line), "Level %u    EXP %lld/%lld",
                          static_cast<unsigned>(world.LocalLevel()),
                          static_cast<long long>(world.LocalExperience()),
                          static_cast<long long>(world.LocalExpToNext()));
            m_text.DrawString(m_uiBatch, P(x + 14.0f, y + 56.0f), line, 12.0f * s,
                              theme.textDim);
            std::snprintf(line, sizeof(line), "HP %u/%u   MP %u/%u",
                          static_cast<unsigned>(world.LocalCurrentHp()),
                          static_cast<unsigned>(world.LocalMaxHp()),
                          static_cast<unsigned>(world.LocalCurrentMana()),
                          static_cast<unsigned>(world.LocalMaxMana()));
            m_text.DrawString(m_uiBatch, P(x + 14.0f, y + 76.0f), line, 12.0f * s,
                              theme.textDim);
            std::snprintf(line, sizeof(line), "Attack %u (+%u equip)   Defense %u (+%u equip)",
                          static_cast<unsigned>(world.LocalAttackPower()),
                          static_cast<unsigned>(world.Equipment().AttackBonus()),
                          static_cast<unsigned>(world.LocalDefensePower()),
                          static_cast<unsigned>(world.Equipment().DefenseBonus()));
            m_text.DrawString(m_uiBatch, P(x + 14.0f, y + 96.0f), line, 12.0f * s,
                              theme.textDim);
            std::snprintf(line, sizeof(line), "Gold %lld",
                          static_cast<long long>(world.LocalGold()));
            m_text.DrawString(m_uiBatch, P(x + 14.0f, y + 116.0f), line, 13.0f * s,
                              theme.textGold);
        }
        // Weapon / Armor 槽（点击 = Unequip 请求）。
        auto equipSlotRow = [&](float ry, const char* slotLabel, std::uint32_t definitionId,
                                std::uint8_t slotCode) {
            const bool filled = definitionId != 0;
            const bool hover = inRect(x + 14.0f, ry, w - 28.0f, 36.0f);
            m_uiBatch.DrawQuad(*m_whiteTexture, P(x + 14.0f + (w - 28.0f) * 0.5f, ry + 18.0f),
                               {(w - 28.0f) * s / 64.0f, 36.0f * s / 64.0f}, 0.0f,
                               filled ? theme.panelSlot : Color(0.10f, 0.11f, 0.14f, 0.8f));
            if (filled) {
                drawIcon(m_itemDisplay.IconAsset(definitionId), x + 32.0f, ry + 18.0f, 26.0f);
            }
            if (haveText) {
                char line[160];
                std::snprintf(line, sizeof(line), "%s: %s", slotLabel,
                              filled ? m_itemDisplay.DisplayName(definitionId).c_str()
                                     : "(empty)");
                m_text.DrawString(m_uiBatch, P(x + 52.0f, ry + 10.0f), line, 12.0f * s,
                                  filled ? theme.textPrimary : theme.textDim);
            }
            // 指令十八：装备面板点击 = Unequip（服务器权威）。
            if (filled && hover && mouseClicked) {
                UiRequest req;
                req.kind = UiRequest::Kind::UnequipSlot;
                req.equipmentSlot = slotCode;
                m_uiRequests.push_back(req);
                mouseClicked = false;
            }
        };
        equipSlotRow(y + 150.0f, "Weapon", world.Equipment().WeaponDefinitionId(), 1);
        equipSlotRow(y + 192.0f, "Armor", world.Equipment().ArmorDefinitionId(), 2);
    }

    // ================= Settings（指令五十二：Esc —— Resolution/Fullscreen/音量）====
    if (m_settingsVisible) {
        const float w = 420.0f;
        const float h = 330.0f;
        const float x = 750.0f;
        const float y = 300.0f;
        windowBg(x, y, w, h, "Settings (Esc)");
        // Resolution（1280x720 / 1600x900 / 1920x1080 循环；指令六十二）。
        {
            static const char* kRes[3] = {"1280 x 720", "1600 x 900", "1920 x 1080"};
            char resLabel[64];
            std::snprintf(resLabel, sizeof(resLabel), "Resolution: %s",
                          kRes[m_resolutionIndex < 0 || m_resolutionIndex > 2 ? 1
                                                                              : m_resolutionIndex]);
            if (button(x + 20.0f, y + 44.0f, 240.0f, 32.0f, resLabel, true)) {
                UiRequest req;
                req.kind = UiRequest::Kind::WindowResolution;
                req.index = (m_resolutionIndex + 1) % 3;
                m_uiRequests.push_back(req);
            }
        }
        // Fullscreen / Windowed。
        if (button(x + 20.0f, y + 86.0f, 240.0f, 32.0f,
                   m_fullscreen ? "Mode: Fullscreen" : "Mode: Windowed", true)) {
            UiRequest req;
            req.kind = UiRequest::Kind::WindowFullscreen;
            req.index = m_fullscreen ? 0 : 1;
            m_uiRequests.push_back(req);
        }
        // 音量（点击条设置值；指令五十一）。
        auto volumeBar = [&](float by, const char* label, float& value) {
            if (haveText) {
                m_text.DrawString(m_uiBatch, P(x + 20.0f, by + 4.0f), label, 13.0f * s,
                                  theme.textPrimary);
            }
            const float barX = x + 150.0f;
            const float barW = 220.0f;
            const float barH = 18.0f;
            m_uiBatch.DrawQuad(*m_whiteTexture, P(barX + barW * 0.5f, by + barH * 0.5f),
                               {barW * s / 64.0f, barH * s / 64.0f}, 0.0f, theme.barBack);
            m_uiBatch.DrawQuad(*m_whiteTexture, P(barX + barW * value * 0.5f, by + barH * 0.5f),
                               {barW * value * s / 64.0f, barH * s / 64.0f}, 0.0f,
                               theme.expFill);
            if (inRect(barX, by, barW, barH) && mouseClicked) {
                value = std::clamp((refMouse.x - barX) / barW, 0.0f, 1.0f);
                m_settingsDirty = true;
                mouseClicked = false;
            }
        };
        volumeBar(y + 140.0f, "Master", m_masterVolume);
        volumeBar(y + 172.0f, "Music", m_musicVolume);
        volumeBar(y + 204.0f, "SFX", m_sfxVolume);
        if (button(x + 20.0f, y + h - 46.0f, 120.0f, 32.0f, "Resume", true)) {
            m_settingsVisible = false;
        }
    }
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

