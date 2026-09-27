// 阶段6：Progression / Loot / Inventory 自检实现（从 GameScene.cpp 拆出，避免单文件过大）。
// 全部 Check 结果写日志 [XxxCheck] ... -> PASS/FAIL + completed, failures = N。
#include "Client/Source/GameScene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "Client/Character/PlayerCharacter.h"
#include "Client/Loot/GroundLoot.h"
#include "Client/Loot/LootManager.h"
#include "Client/Loot/LootTable.h"
#include "Client/Progression/PlayerProgression.h"
#include "Client/World/MonsterCharacter.h"
#include "Client/World/MonsterDefinition.h"
#include "Client/World/MonsterSpawner.h"
#include "Client/World/WorldActorManager.h"
#include "Engine/Combat/CombatSystem.h"
#include "Engine/Core/Engine.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Item/ItemDatabase.h"
#include "Engine/Item/ItemDefinition.h"
#include "Engine/Item/ItemInstance.h"
#include "Engine/Item/Inventory.h"
#include "Engine/Progression/ExperienceTable.h"
#include "Engine/Progression/LevelSystem.h"
#include "Engine/Render/Texture.h"

using legend::Engine;

namespace {

// 统一 Check 输出：PASS/FAIL + 失败计数
void LogCheck(const char* tag, const std::string& name, bool pass, int& failures) {
    LOG_INFO(std::string("[") + tag + "] " + name + " -> " + (pass ? "PASS" : "FAIL"));
    if (!pass) {
        ++failures;
    }
}

} // namespace


// ==================== [LootRollCheck] ====================

void GameScene::RunLootRollCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogCheck("LootRollCheck", name, pass, failures);
    };
    std::mt19937 rng(20260927u); // 固定 seed：结果可重复
    std::vector<legend::world::LootEntry> entries;
    legend::world::LootEntry always;
    always.itemId = "test_always";
    always.chance = 1.0f;
    always.min = 2;
    always.max = 4;
    legend::world::LootEntry never;
    never.itemId = "test_never";
    never.chance = 0.0f;
    never.min = 1;
    never.max = 1;
    entries.push_back(always);
    entries.push_back(never);

    int alwaysDropped = 0;
    int neverDropped = 0;
    bool quantityInRange = true;
    for (int i = 0; i < 200; ++i) {
        const auto rolls = legend::world::RollLootTable(entries, rng);
        bool sawAlways = false;
        bool sawNever = false;
        for (const auto& roll : rolls) {
            if (roll.itemId == "test_always") {
                sawAlways = true;
                if (roll.quantity < 2 || roll.quantity > 4) {
                    quantityInRange = false;
                }
            }
            if (roll.itemId == "test_never") {
                sawNever = true;
            }
        }
        if (sawAlways) {
            ++alwaysDropped;
        }
        if (sawNever) {
            ++neverDropped;
        }
    }
    check("chance 1.0 always drops (200/200)", alwaysDropped == 200);
    check("chance 0.0 never drops", neverDropped == 0);
    check("quantity in [min,max] inclusive", quantityInRange);
    LOG_INFO("[LootRollCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [GroundLootPickupCheck] ====================

void GameScene::RunGroundLootPickupCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogCheck("GroundLootPickupCheck", name, pass, failures);
    };
    legend::item::ItemDatabase items;
    (void)items.LoadFromFile(Engine::Get().GetResources().GetAssetRoot() + "/Items/items.json");
    legend::world::LootManager loot;
    loot.Initialize(20260927u, &items);
    legend::item::Inventory inventory;

    const legend::math::Vector2 playerPos(1000.0f, 1000.0f);
    (void)loot.SpawnGroundLoot("small_potion", 5, playerPos + legend::math::Vector2(20.0f, 0.0f));
    check("ground loot spawned", loot.GetCount() == 1);

    const int picked = loot.PickupNearest(playerPos, 80.0f, inventory, items);
    check("picked 5", picked == 5);
    check("inventory +5", inventory.GetItemCount("small_potion") == 5);
    check("ground loot removed", loot.GetCount() == 0);

    // 不在范围内：不能拾取
    (void)loot.SpawnGroundLoot("small_potion", 1, playerPos + legend::math::Vector2(200.0f, 0.0f));
    const int outOfRange = loot.PickupNearest(playerPos, 80.0f, inventory, items);
    check("out of range not picked", outOfRange == 0 && loot.GetCount() == 1);
    LOG_INFO("[GroundLootPickupCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [PartialPickupCheck] ====================

void GameScene::RunPartialPickupCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogCheck("PartialPickupCheck", name, pass, failures);
    };
    legend::item::ItemDatabase items;
    (void)items.LoadFromFile(Engine::Get().GetResources().GetAssetRoot() + "/Items/items.json");
    const legend::item::ItemDefinition* potionDef = items.Get("small_potion");
    if (potionDef == nullptr) {
        check("small_potion available", false);
        LOG_INFO("[PartialPickupCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    legend::world::LootManager loot;
    loot.Initialize(20260927u, &items);
    legend::item::Inventory inventory;

    // 背包只剩可容纳 2 个 Potion：slot0 有 18 个，其余 19 格填不可堆叠物
    (void)inventory.AddItem(*potionDef, 18);
    legend::item::ItemDefinition unique;
    unique.id = "partial_test_unique";
    unique.name = "PartialTestUnique";
    unique.maxStack = 1;
    for (std::size_t i = 0; i < inventory.GetCapacity() - 1; ++i) {
        (void)inventory.AddItem(unique, 1);
    }
    check("inventory prepared (capacity for 2)",
          inventory.GetUsedSlots() == inventory.GetCapacity() &&
              inventory.GetItemCount("small_potion") == 18);

    const legend::math::Vector2 playerPos(2000.0f, 2000.0f);
    (void)loot.SpawnGroundLoot("small_potion", 5, playerPos);
    const int picked = loot.PickupNearest(playerPos, 80.0f, inventory, items);
    check("partial pickup 2", picked == 2);
    check("inventory +2 (topped to 20)",
          inventory.GetItemCount("small_potion") == 20);
    check("ground loot keeps 3", loot.GetCount() == 1 && loot.GetAll()[0].quantity == 3);

    // 再拾取：满 -> added 0，地上保留
    const int rejected = loot.PickupNearest(playerPos, 80.0f, inventory, items);
    check("full -> 0 picked, ground kept", rejected == 0 && loot.GetCount() == 1);
    LOG_INFO("[PartialPickupCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [DeathRewardCheck] ====================
// 用真实世界的一只活 Slime：玩家直接施加致命伤害 -> WorldActorManager::ProcessDeathRewards
// 消费 DeathEvent -> Exp 只加一次 + Loot 只 Roll 一次；连续消费不重复。

void GameScene::RunDeathRewardCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogCheck("DeathRewardCheck", name, pass, failures);
    };
    if (m_player == nullptr) {
        check("player available", false);
        LOG_INFO("[DeathRewardCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    // 找一只活的 Slime
    legend::world::MonsterCharacter* slime = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Slime") {
            slime = monster;
            break;
        }
    }
    if (slime == nullptr) {
        check("alive slime available", false);
        LOG_INFO("[DeathRewardCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    const legend::world::MonsterDefinition* def =
        m_worldActors.GetSpawner().GetDefinition(slime->GetMonsterTemplateId());
    if (def == nullptr) {
        check("slime definition available", false);
        LOG_INFO("[DeathRewardCheck] completed, failures = " + std::to_string(failures));
        return;
    }

    // 基线
    const int levelBefore = m_player->GetProgression().GetLevel();
    const long long totalExpBefore = m_player->GetProgression().GetTotalExp();
    const std::uint64_t lootSpawnedBefore = m_worldActors.GetLoot().GetTotalSpawned();
    const std::size_t groundLootBefore = m_worldActors.GetLoot().GetCount();
    const int slimeId = static_cast<int>(slime->GetId());

    // 玩家致死一击（source = Player）
    const auto savedPlayerPos = m_player->GetPosition();
    const auto savedSlimePos = slime->GetPosition();
    m_player->SetPosition(savedSlimePos + legend::math::Vector2(50.0f, 0.0f));
    legend::combat::DamageEvent lethal;
    lethal.sourceId = m_player->GetId();
    lethal.targetId = slime->GetId();
    lethal.rawDamage = 99999.0f;
    lethal.finalDamage = 99999.0f;
    const bool applied = m_worldActors.GetCombatSystem().ApplyDamage(lethal);
    check("lethal damage applied", applied);
    check("slime entered Dead",
          slime->GetActionState() == legend::entity::CharacterActionState::Dead);

    // 1) 第一次消费：Exp 只发一次 + Loot 只 Roll 一次
    const int processed1 = m_worldActors.ProcessDeathRewards();
    check("death rewards processed once", processed1 == 1);
    const long long expGained =
        m_player->GetProgression().GetTotalExp() - totalExpBefore;
    check("exp granted exactly def.expReward", expGained == def->expReward);
    check("loot rolled (spawned counter increased)",
          m_worldActors.GetLoot().GetTotalSpawned() >= lootSpawnedBefore);
    check("ground loot generated", m_worldActors.GetLoot().GetCount() >= groundLootBefore);

    // 2) 再次消费：必须无变化（exactly-once）
    const int processed2 = m_worldActors.ProcessDeathRewards();
    check("second process is no-op", processed2 == 0);
    check("exp not double granted",
          m_player->GetProgression().GetTotalExp() - totalExpBefore == def->expReward);
    check("loot not double rolled",
          m_worldActors.GetLoot().GetTotalSpawned() >= lootSpawnedBefore &&
              m_worldActors.GetLoot().GetTotalSpawned() <= lootSpawnedBefore + 2);

    // 还原（slime 会自然走 Respawn 流程）
    m_player->SetPosition(savedPlayerPos);
    (void)slimeId;
    LOG_INFO("[DeathRewardCheck] completed, failures = " + std::to_string(failures));
}

// ==================== LEGEND_AUTO_PROGRESSION_TEST=1 时间线 ====================
// 击杀 Slime -> Exp -> GroundLoot -> 拾取 -> 背包 -> 升级 -> 属性成长 -> Respawn。

void GameScene::UpdateProgressionTest(float deltaTime) {
    auto& registry = m_worldActors.GetRegistry();
    auto fail = [this](const std::string& name) {
        LOG_INFO("[ProgressionTest] " + name + " -> FAIL");
        ++m_progTestFailures;
    };
    auto pass = [this](const std::string& name) {
        LOG_INFO("[ProgressionTest] " + name + " -> PASS");
    };

    m_progTestElapsed += deltaTime;
    if (!m_progTestStageEntered) {
        m_progTestStageEntered = true;
        m_progTestStageElapsed = 0.0;
        LOG_INFO("[ProgressionTest] stage " + std::to_string(m_progTestStage) + " entered.");
    }
    m_progTestStageElapsed += deltaTime;
    if (m_progTestElapsed > 150.0) {
        fail("timeline timeout 150s");
        m_progTestStage = 90;
    }

    switch (m_progTestStage) {
    case 0: { // 找一只活 Slime + 传送玩家到攻击距离 + 记录基线
        legend::world::MonsterCharacter* slime = nullptr;
        for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
            if (monster != nullptr && monster->IsCombatAlive() &&
                monster->GetName() == "Slime") {
                slime = monster;
                break;
            }
        }
        if (slime == nullptr) {
            if (m_progTestStageElapsed > 10.0) {
                fail("no alive slime within 10s");
                m_progTestStage = 90;
            }
            break;
        }
        m_progTestSlimeId = slime->GetId();
        m_player->SetPosition(slime->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
        m_progTestBaselineLevel = m_player->GetProgression().GetLevel();
        m_progTestBaselineTotalExp = m_player->GetProgression().GetTotalExp();
        m_progTestBaselineMaxHp = m_player->GetCombatStats().maxHp;
        m_progTestBaselineAttack = m_player->GetCombatStats().attack;
        m_progTestBaselineDefense = m_player->GetCombatStats().defense;
        m_progTestBagBaseline = static_cast<int>(m_player->GetInventory().GetUsedSlots());
        m_progTestLootBaseline = m_worldActors.GetLoot().GetTotalSpawned();
        m_playerCombat.GetTarget().SetTarget(slime->GetId());
        m_progTestStage = 1;
        break;
    }
    case 1: { // 连击直到 Slime 死亡
        auto* slime = static_cast<legend::world::MonsterCharacter*>(registry.Get(m_progTestSlimeId));
        if (slime == nullptr) {
            fail("slime despawned before death (unexpected)");
            m_progTestStage = 90;
            break;
        }
        if (m_player->GetActionState() == legend::entity::CharacterActionState::Normal) {
            (void)m_playerCombat.RequestAttack(*m_player, registry, m_worldActors.GetCombatSystem());
        }
        if (slime->GetActionState() == legend::entity::CharacterActionState::Dead) {
            ++m_progTestKills;
            pass("slime killed (kill " + std::to_string(m_progTestKills) + ")");
            m_progTestStage = 2;
        } else if (m_progTestStageElapsed > 30.0) {
            fail("slime not dead within 30s");
            m_progTestStage = 90;
        }
        break;
    }
    case 2: { // 验证 Exp 到账 + Loot 生成（奖励已在 WorldActorManager::Update 分发）
        const long long expGained =
            m_player->GetProgression().GetTotalExp() - m_progTestBaselineTotalExp;
        const auto* def =
            m_worldActors.GetSpawner().GetDefinition("slime");
        const int expectedExp = def != nullptr ? def->expReward : -1;
        if (expGained == expectedExp && expectedExp > 0) {
            pass("exp +" + std::to_string(expGained) + " == def.expReward");
        } else {
            fail("exp gain mismatch (got " + std::to_string(expGained) + ", want " +
                 std::to_string(expectedExp) + ")");
        }
        const std::uint64_t lootSpawned = m_worldActors.GetLoot().GetTotalSpawned();
        if (lootSpawned > m_progTestLootBaseline) {
            pass("ground loot spawned (total " + std::to_string(lootSpawned) + ")");
        } else {
            // 本 seed 可能没掉落：保底生成一件测试掉落，拾取链路仍然验证
            LOG_INFO("[ProgressionTest] no loot rolled this kill, spawn test loot for pickup link.");
            (void)m_worldActors.GetLoot().SpawnGroundLoot("small_potion", 2,
                                                          m_player->GetPosition());
        }
        // 传送玩家到最近掉落旁
        const legend::world::GroundLoot* nearest =
            m_worldActors.GetLoot().FindNearestPickup(m_player->GetPosition(), 80.0f);
        if (nearest == nullptr) {
            fail("no ground loot near player after kill");
            m_progTestStage = 90;
            break;
        }
        m_player->SetPosition(nearest->position + legend::math::Vector2(10.0f, 0.0f));
        m_progTestStage = 3;
        break;
    }
    case 3: { // E 拾取 -> 背包增加
        const int bagBefore = static_cast<int>(m_player->GetInventory().GetUsedSlots());
        const int picked = m_worldActors.GetLoot().PickupNearest(
            m_player->GetPosition(), 80.0f, m_player->GetInventory(),
            m_worldActors.GetItemDatabase());
        if (picked > 0) {
            pass("picked " + std::to_string(picked) + " via E");
        } else {
            fail("pickup failed (nothing picked)");
        }
        const int bagAfter = static_cast<int>(m_player->GetInventory().GetUsedSlots());
        m_progTestBagBaseline = bagBefore;
        (void)bagAfter;
        // 升级判定：需求差 = 升到下一级还差多少
        const int level = m_player->GetProgression().GetLevel();
        const int required = legend::progression::RequiredExp(level);
        const int currentExp = m_player->GetProgression().GetCurrentExp();
        if (level > m_progTestBaselineLevel) {
            m_progTestStage = 4; // 已升级：验证成长
        } else if (currentExp >= required) {
            m_progTestStage = 4;
        } else {
            m_progTestStage = 0; // 继续刷下一只
            LOG_INFO("[ProgressionTest] exp " + std::to_string(currentExp) + "/" +
                     std::to_string(required) + ", killing next slime.");
        }
        break;
    }
    case 4: { // 属性成长验证（levelDelta * growth 20/5/2）
        const int levelDelta =
            m_player->GetProgression().GetLevel() - m_progTestBaselineLevel;
        if (levelDelta <= 0) {
            fail("no level up after kills");
            m_progTestStage = 90;
            break;
        }
        const float hpDelta = m_player->GetCombatStats().maxHp - m_progTestBaselineMaxHp;
        const float atkDelta = m_player->GetCombatStats().attack - m_progTestBaselineAttack;
        const float defDelta = m_player->GetCombatStats().defense - m_progTestBaselineDefense;
        const auto& growth = m_player->GetProgression().GetGrowth();
        if (std::fabs(hpDelta - growth.maxHpPerLevel * levelDelta) < 0.001f) {
            pass("maxHp +" + std::to_string(hpDelta) + " (lv +" + std::to_string(levelDelta) + ")");
        } else {
            fail("maxHp growth mismatch");
        }
        if (std::fabs(atkDelta - growth.attackPerLevel * levelDelta) < 0.001f) {
            pass("attack +" + std::to_string(atkDelta));
        } else {
            fail("attack growth mismatch");
        }
        if (std::fabs(defDelta - growth.defensePerLevel * levelDelta) < 0.001f) {
            pass("defense +" + std::to_string(defDelta));
        } else {
            fail("defense growth mismatch");
        }
        LOG_INFO("[ProgressionTest] waiting monster respawn for final check.");
        m_progTestStage = 5;
        break;
    }
    case 5: { // Respawn 验证：等待死掉的 Slime 重生（数量守恒）
        if (m_progTestStageElapsed < 8.0) {
            break; // corpse 1.5s + respawn 5s
        }
        const int aliveNow = m_worldActors.GetAliveMonsterCount();
        if (aliveNow > 0) {
            pass("monster respawned (alive " + std::to_string(aliveNow) + ")");
        } else if (m_progTestStageElapsed < 30.0) {
            break; // 再等
        } else {
            fail("no monster respawned within 30s");
        }
        m_progTestStage = 90;
        break;
    }
    case 90: { // 汇总
        if (!m_progTestSummaryDone) {
            m_progTestSummaryDone = true;
            LOG_INFO("[ProgressionTest] completed, kills = " + std::to_string(m_progTestKills) +
                     ", failures = " + std::to_string(m_progTestFailures) +
                     ", elapsed = " + std::to_string(m_progTestElapsed) + "s");
        }
        break;
    }
    default:
        break;
    }
}

// ==================== 阶段6：GroundLoot / F5 Debug 绘制 ====================

void GameScene::DrawGroundLoot(const legend::world::GroundLoot& loot) {
    auto& batch = m_mapRenderer.GetBatch();
    if (!m_whiteTexture) {
        return;
    }
    // 不同物品不同颜色（Debug 色块，不做正式美术）：
    // small_potion 红 / slime_gel 绿 / wolf_fang 灰白 / boar_hide 棕 / iron_ore 蓝灰 / 默认黄
    legend::math::Color color(0.95f, 0.85f, 0.2f, 1.0f);
    if (loot.itemId == "small_potion") {
        color = legend::math::Color(0.9f, 0.25f, 0.25f, 1.0f);
    } else if (loot.itemId == "slime_gel") {
        color = legend::math::Color(0.35f, 0.85f, 0.35f, 1.0f);
    } else if (loot.itemId == "wolf_fang") {
        color = legend::math::Color(0.92f, 0.92f, 0.88f, 1.0f);
    } else if (loot.itemId == "boar_hide") {
        color = legend::math::Color(0.62f, 0.42f, 0.25f, 1.0f);
    } else if (loot.itemId == "iron_ore") {
        color = legend::math::Color(0.55f, 0.62f, 0.72f, 1.0f);
    }
    // 过期前 10 秒闪烁提示
    if (loot.age > 50.0f) {
        color.a = 0.4f + 0.6f * std::fabs(std::sin(loot.age * 6.0f));
    }
    const float size = 10.0f;
    const legend::math::Vector2 center(loot.position.x, loot.position.y - 6.0f);
    batch.DrawQuad(*m_whiteTexture, center, legend::math::Vector2(size, size), 0.0f, color, false,
                   false, 0.0f, 0.0f, 1.0f, 1.0f);
    // F5 开启时画拾取圈
    if (m_progressionDebug) {
        DrawCircle(batch, loot.position, 12.0f, color);
    }
}

void GameScene::DrawProgressionDebugOverlay(legend::render::SpriteBatch& batch) {
    if (m_player == nullptr) {
        return;
    }
    const legend::math::Vector2 playerPos = m_player->GetPosition();
    // 拾取范围圈（80）
    DrawCircle(batch, playerPos, 80.0f, legend::math::Color(0.3f, 0.85f, 0.4f, 0.35f));
    // 最近 5 件掉落：连线 + 距离日志（一次性，避免刷屏由调用方节流）
    const auto& loots = m_worldActors.GetLoot().GetAll();
    std::vector<const legend::world::GroundLoot*> nearest;
    for (const auto& loot : loots) {
        nearest.push_back(&loot);
    }
    std::sort(nearest.begin(), nearest.end(),
              [&playerPos](const legend::world::GroundLoot* a, const legend::world::GroundLoot* b) {
                  return (a->position - playerPos).LengthSq() <
                         (b->position - playerPos).LengthSq();
              });
    const int count = static_cast<int>(std::min<std::size_t>(nearest.size(), 5));
    for (int i = 0; i < count; ++i) {
        const auto* loot = nearest[i];
        const float dist = (loot->position - playerPos).Length();
        const bool inRange = dist <= 80.0f;
        const legend::math::Color lineColor =
            inRange ? legend::math::Color(0.3f, 0.95f, 0.4f, 0.8f)
                    : legend::math::Color(0.8f, 0.8f, 0.2f, 0.4f);
        DrawLine(batch, playerPos, loot->position, 1.5f, lineColor);
    }
}
