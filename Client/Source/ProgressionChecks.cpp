// 阶段6：Progression / Loot / Inventory 自检实现（从 GameScene.cpp 拆出，避免单文件过大）。
// 全部 Check 结果写日志 [XxxCheck] ... -> PASS/FAIL + completed, failures = N。
#include "Client/Source/GameScene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
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

    // 阶段6.1：同一 seed 连续运行结果必须一致（item + 数量逐项相等）
    auto runOnce = [entries](unsigned int seed) {
        std::mt19937 rng(seed);
        return legend::world::RollLootTable(entries, rng);
    };
    const auto firstRun = runOnce(12345u);
    const auto secondRun = runOnce(12345u);
    bool reproducible = firstRun.size() == secondRun.size();
    for (std::size_t i = 0; reproducible && i < firstRun.size(); ++i) {
        reproducible = firstRun[i].itemId == secondRun[i].itemId &&
                       firstRun[i].quantity == secondRun[i].quantity;
    }
    check("fixed seed 12345 reproducible (item+qty identical)",
          reproducible && !firstRun.empty());
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

    // 阶段6.1：LootEntityIdAllocator Reset(nextValue) 语义 —— Reset(9) 后 Next()==9
    legend::world::LootEntityIdAllocator::Reset(9);
    check("loot entity allocator Reset(9) -> Next()==9",
          legend::world::LootEntityIdAllocator::Next() == 9);
    check("loot entity allocator Next() increments",
          legend::world::LootEntityIdAllocator::Next() == 10);
    legend::world::LootEntityIdAllocator::Reset(1); // 还原默认起点

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

    // 1) 第一次消费：Exp 只发一次（Loot 是否 Roll 取决于随机概率——必掉验证归
    //    [DeathLootIntegrationCheck] 的 Test Override；本 Check 只验证 exactly-once）
    const int processed1 = m_worldActors.ProcessDeathRewards();
    check("death rewards processed once", processed1 == 1);
    const legend::progression::ExperienceValue expAfterFirst =
        m_player->GetProgression().GetTotalExp();
    const std::uint64_t lootSpawnedAfterFirst = m_worldActors.GetLoot().GetTotalSpawned();
    const std::size_t groundLootAfterFirst = m_worldActors.GetLoot().GetCount();
    check("exp granted exactly def.expReward",
          expAfterFirst - totalExpBefore == def->expReward);
    check("loot counter never decreases", lootSpawnedAfterFirst >= lootSpawnedBefore);

    // 2) 再次消费：必须与第一次之后完全一致（阶段6.1 严格相等断言，无任何新增）
    const int processed2 = m_worldActors.ProcessDeathRewards();
    const legend::progression::ExperienceValue expAfterSecond =
        m_player->GetProgression().GetTotalExp();
    const std::uint64_t lootSpawnedAfterSecond = m_worldActors.GetLoot().GetTotalSpawned();
    const std::size_t groundLootAfterSecond = m_worldActors.GetLoot().GetCount();
    check("second process is no-op", processed2 == 0);
    check("exp exactly-once (afterSecond == afterFirst)", expAfterSecond == expAfterFirst);
    check("loot exactly-once (spawned afterSecond == afterFirst)",
          lootSpawnedAfterSecond == lootSpawnedAfterFirst);
    check("ground count unchanged", groundLootAfterSecond == groundLootAfterFirst);

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
        m_progTestGroundBaseline = m_worldActors.GetLoot().GetCount();
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
        // 阶段6.1：Test Override LootTable（slime 必掉 small_potion x2）——
        // 真实 Death -> Reward -> Roll -> GroundLoot 链路必须发生，禁止 SpawnGroundLoot 兜底掩盖
        if (lootSpawned <= m_progTestLootBaseline) {
            fail("REAL monster loot did NOT roll (integration broken, fallback forbidden)");
            m_progTestStage = 90;
            break;
        }
        pass("Real monster loot integration PASS (total " + std::to_string(lootSpawned) + ")");
        // 验证新增 GroundLoot 内容（Override：small_potion x2）
        const auto& allLoot = m_worldActors.GetLoot().GetAll();
        bool realLootOk = false;
        for (std::size_t i = m_progTestGroundBaseline; i < allLoot.size(); ++i) {
            if (allLoot[i].itemId == "small_potion" && allLoot[i].quantity == 2) {
                realLootOk = true;
            }
        }
        if (realLootOk) {
            pass("real loot content (small_potion x2)");
        } else {
            fail("real loot content mismatch (want small_potion x2)");
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
            pass("picked " + std::to_string(picked) +
                 " via E (from real monster loot, not isolated spawn)");
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

// ==================== [Experience64Check] ====================
// 阶段6.1：64 位经验安全——高等级 RequiredExp 真实值（无 INT32 截断）、
// lv49 边界升级、一次 3e9+ 大经验连续升级无溢出/无负数。

void GameScene::RunExperience64Check() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogCheck("Experience64Check", name, pass, failures);
    };
    using namespace legend::progression;

    // 1) 高等级经验需求全部 > 0（真实 int64 值，无 INT32 截断）
    const ExperienceValue req40 = RequiredExp(40);
    const ExperienceValue req41 = RequiredExp(41);
    const ExperienceValue req42 = RequiredExp(42);
    const ExperienceValue req43 = RequiredExp(43);
    const ExperienceValue req49 = RequiredExp(49);
    check("RequiredExp(40) > 0 real value", req40 > 0);
    check("RequiredExp(41) > 0 real value", req41 > 0);
    check("RequiredExp(42) > 0 real value", req42 > 0);
    check("RequiredExp(43) > 0 real value", req43 > 0);
    check("RequiredExp(49) > 0 real value", req49 > 0);
    // 单调递增
    check("required exp strictly increasing",
          req40 < req41 && req41 < req42 && req42 < req43 && req43 < req49);
    // Level43+ 超过 INT32_MAX（int32 会截断的级别，int64 必须无损）
    constexpr ExperienceValue kInt32Max = 2147483647LL;
    check("RequiredExp(43) exceeds INT32_MAX (int64 safe)", req43 > kInt32Max);
    check("RequiredExp(49) far exceeds INT32_MAX", req49 > kInt32Max * 10);

    // 2) Level49 差 50 exp -> 加 100 -> 安全升 50（无负数/溢出/UB）
    int level = 49;
    ExperienceValue currentExp = req49 - 50;
    ExperienceValue totalExp = 0;
    auto events = LevelSystem::AddExperience(level, currentExp, totalExp, 100);
    check("lv49 (req-50) +100 -> level 50",
          level == 50 && events.size() == 1 && events[0].oldLevel == 49 &&
              events[0].newLevel == 50);
    check("lv50 currentExp == 0 (capped)", currentExp == 0);
    check("totalExp accumulated exactly", totalExp == 100);

    // 3) 一次加入超过 3,000,000,000 EXP：不溢出、按规则连续升级（与独立模拟逐项对照）、
    //    currentExp 始终非负且小于当前级需求
    level = 1;
    currentExp = 0;
    totalExp = 0;
    constexpr ExperienceValue kHugeExp = 3500000000LL; // > 3e9
    events = LevelSystem::AddExperience(level, currentExp, totalExp, kHugeExp);
    // 独立模拟：同规则手算期望级别与余量（不依赖被测实现）
    ExperienceValue simulatedRemainder = kHugeExp;
    int expectedLevel = 1;
    while (expectedLevel < kMaxLevel && simulatedRemainder >= RequiredExp(expectedLevel)) {
        simulatedRemainder -= RequiredExp(expectedLevel);
        ++expectedLevel;
    }
    check("3.5e9 exp -> level matches independent simulation", level == expectedLevel);
    check("3.5e9 exp -> remainder matches simulation (non-negative)",
          currentExp == simulatedRemainder && currentExp >= 0);
    check("3.5e9 exp -> remainder < next requirement",
          level >= kMaxLevel || currentExp < RequiredExp(level));
    check("3.5e9 exp -> totalExp exact", totalExp == kHugeExp);
    check("level up events == levels gained",
          events.size() == static_cast<std::size_t>(expectedLevel - 1));

    // 4) 天文数字经验（1e14 >> 全级总需求）：封顶 50、currentExp 归 0、事件数 == 49
    level = 1;
    currentExp = 0;
    totalExp = 0;
    events = LevelSystem::AddExperience(level, currentExp, totalExp, 100000000000000LL);
    check("1e14 exp -> capped at level 50", level == kMaxLevel && currentExp == 0);
    check("1e14 exp -> exactly 49 level up events", events.size() == 49);

    LOG_INFO("[Experience64Check] completed, failures = " + std::to_string(failures));
}

// ==================== [ItemDatabaseFailureCheck] ====================
// 阶段6.1：加载失败路径——不存在的文件返回 false、空库 Exists()==false；
// 正式 items.json 加载成功且 >= 5 item。（临时实例，不影响全局 ItemDatabase）

void GameScene::RunItemDatabaseFailureCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogCheck("ItemDatabaseFailureCheck", name, pass, failures);
    };
    // 1) 不存在的文件：返回 false，不崩溃
    legend::item::ItemDatabase missing;
    const bool loadedMissing =
        missing.LoadFromFile(Engine::Get().GetResources().GetAssetRoot() +
                             "/Items/__no_such_items_file__.json");
    check("missing file -> LoadFromFile false", !loadedMissing);
    check("empty database -> Exists false", !missing.Exists("small_potion"));
    check("empty database -> Get nullptr", missing.Get("small_potion") == nullptr);
    check("empty database -> Count 0", missing.Count() == 0);

    // 2) 非法 JSON：返回 false
    legend::item::ItemDatabase malformed;
    const std::string malformedPath = Engine::Get().GetResources().GetAssetRoot() +
                                      "/Items/__malformed_test__.json";
    {
        std::ofstream out(malformedPath);
        out << "{ this is not valid json !!!";
    }
    const bool loadedMalformed = malformed.LoadFromFile(malformedPath);
    check("malformed json -> LoadFromFile false", !loadedMalformed);
    std::error_code ec;
    std::filesystem::remove(malformedPath, ec); // 清理临时文件
    check("temp malformed file removed", !std::filesystem::exists(malformedPath));

    // 3) 正式文件：加载成功且 >= 5 item
    legend::item::ItemDatabase official;
    const bool loadedOfficial = official.LoadFromFile(
        Engine::Get().GetResources().GetAssetRoot() + "/Items/items.json");
    check("official items.json -> LoadFromFile true", loadedOfficial);
    check("official items.json >= 5 items", official.Count() >= 5);
    check("official has small_potion", official.Exists("small_potion"));

    LOG_INFO("[ItemDatabaseFailureCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [DeathLootIntegrationCheck] ====================
// 阶段6.1：真实链路集成——Test Override LootTable（small_potion chance1.0 min2 max2）：
// Monster DeathEvent(killer=Player) -> RewardSystem 消费 -> Exp 增加 + Loot 真实 Roll ->
// GroundLoot 真实生成（itemId/quantity 精确验证）；二次消费 Exp/Loot 均不增加（exactly-once）。

void GameScene::RunDeathLootIntegrationCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogCheck("DeathLootIntegrationCheck", name, pass, failures);
    };
    if (m_player == nullptr) {
        check("player available", false);
        LOG_INFO("[DeathLootIntegrationCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    // 找一只活 Slime
    legend::world::MonsterCharacter* slime = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Slime") {
            slime = monster;
            break;
        }
    }
    if (slime == nullptr) {
        check("alive slime available", false);
        LOG_INFO("[DeathLootIntegrationCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    const legend::world::MonsterDefinition* def =
        m_worldActors.GetSpawner().GetDefinition(slime->GetMonsterTemplateId());
    if (def == nullptr) {
        check("slime definition available", false);
        LOG_INFO("[DeathLootIntegrationCheck] completed, failures = " + std::to_string(failures));
        return;
    }

    // Test Override：必掉 small_potion x2（保存原表，测试后还原）
    const std::vector<legend::world::LootEntry> originalLoot =
        m_worldActors.GetSpawner().GetLootEntries(slime->GetMonsterTemplateId());
    std::vector<legend::world::LootEntry> guaranteed;
    legend::world::LootEntry guaranteedEntry;
    guaranteedEntry.itemId = "small_potion";
    guaranteedEntry.chance = 1.0f;
    guaranteedEntry.min = 2;
    guaranteedEntry.max = 2;
    guaranteed.push_back(guaranteedEntry);
    m_worldActors.GetSpawner().SetTestLootOverride(slime->GetMonsterTemplateId(), guaranteed);

    // 基线
    const legend::progression::ExperienceValue expBefore =
        m_player->GetProgression().GetTotalExp();
    const std::uint64_t lootSpawnedBefore = m_worldActors.GetLoot().GetTotalSpawned();
    const std::size_t groundLootBefore = m_worldActors.GetLoot().GetCount();

    // 玩家致死一击（真实 DamageEvent -> Alive->Dead -> DeathEvent）
    const auto savedPlayerPos = m_player->GetPosition();
    const auto deathPosition = slime->GetPosition();
    m_player->SetPosition(deathPosition + legend::math::Vector2(45.0f, 0.0f));
    legend::combat::DamageEvent lethal;
    lethal.sourceId = m_player->GetId();
    lethal.targetId = slime->GetId();
    lethal.rawDamage = 99999.0f;
    lethal.finalDamage = 99999.0f;
    const bool applied = m_worldActors.GetCombatSystem().ApplyDamage(lethal);
    check("lethal damage applied", applied);
    check("monster entered Dead",
          slime->GetActionState() == legend::entity::CharacterActionState::Dead);

    // 1) 第一次消费：Exp 增加 + 真实 Roll + GroundLoot 真实生成
    const int processed1 = m_worldActors.ProcessDeathRewards();
    check("death rewards processed once", processed1 == 1);
    const legend::progression::ExperienceValue expAfterFirst =
        m_player->GetProgression().GetTotalExp();
    check("exp increased by def.expReward",
          expAfterFirst - expBefore == def->expReward);
    const std::uint64_t lootSpawnedAfterFirst = m_worldActors.GetLoot().GetTotalSpawned();
    const std::size_t groundLootAfterFirst = m_worldActors.GetLoot().GetCount();
    check("real loot rolled (spawned +1)", lootSpawnedAfterFirst == lootSpawnedBefore + 1);
    check("ground loot spawned (+1)", groundLootAfterFirst == groundLootBefore + 1);
    // 新增 GroundLoot 内容精确验证（Override：small_potion x2，位置 = 死亡位置）
    bool contentOk = false;
    const auto& allLoot = m_worldActors.GetLoot().GetAll();
    for (std::size_t i = groundLootBefore; i < allLoot.size(); ++i) {
        if (allLoot[i].itemId == "small_potion" && allLoot[i].quantity == 2 &&
            std::fabs(allLoot[i].position.x - deathPosition.x) < 1.0f &&
            std::fabs(allLoot[i].position.y - deathPosition.y) < 1.0f) {
            contentOk = true;
        }
    }
    check("ground loot is small_potion x2 at death position", contentOk);

    // 2) 再次消费：Exp 与 Loot 均不增加（exactly-once 严格相等）
    const int processed2 = m_worldActors.ProcessDeathRewards();
    const legend::progression::ExperienceValue expAfterSecond =
        m_player->GetProgression().GetTotalExp();
    const std::uint64_t lootSpawnedAfterSecond = m_worldActors.GetLoot().GetTotalSpawned();
    const std::size_t groundLootAfterSecond = m_worldActors.GetLoot().GetCount();
    check("second process is no-op", processed2 == 0);
    check("exp exactly-once", expAfterSecond == expAfterFirst);
    check("loot exactly-once (spawned)", lootSpawnedAfterSecond == lootSpawnedAfterFirst);
    check("loot exactly-once (ground count)", groundLootAfterSecond == groundLootAfterFirst);

    // 还原原掉落表与玩家位置（slime 自然走 Respawn）
    m_worldActors.GetSpawner().SetTestLootOverride(slime->GetMonsterTemplateId(), originalLoot);
    m_player->SetPosition(savedPlayerPos);
    LOG_INFO("[DeathLootIntegrationCheck] completed, failures = " + std::to_string(failures));
}

// ==================== LEGEND_AUTO_EQUIPMENT_TEST=1 时间线 ====================
// 真实流程：Monster Death -> Loot(Override) -> Pickup -> Inventory -> Equip -> Stats ->
// Swap -> Unequip -> instanceId 全程保持 -> Respawn。禁止绕过 Loot/Inventory 塞槽。

void GameScene::UpdateEquipmentTest(float deltaTime) {
    auto& registry = m_worldActors.GetRegistry();
    auto fail = [this](const std::string& name) {
        LOG_INFO("[EquipmentTest] " + name + " -> FAIL");
        ++m_equipTestFailures;
    };
    auto pass = [this](const std::string& name) {
        LOG_INFO("[EquipmentTest] " + name + " -> PASS");
    };
    auto check = [&pass, &fail](const std::string& name, bool ok) {
        if (ok) {
            pass(name);
        } else {
            fail(name);
        }
    };

    m_equipTestElapsed += deltaTime;
    if (!m_equipTestStageEntered) {
        m_equipTestStageEntered = true;
        m_equipTestStageElapsed = 0.0;
        LOG_INFO("[EquipmentTest] stage " + std::to_string(m_equipTestStage) + " entered.");
    }
    m_equipTestStageElapsed += deltaTime;
    // timeout 只触发一次（避免 case 90 后每帧刷 FAIL）
    if (m_equipTestElapsed > 120.0 && m_equipTestStage != 90) {
        fail("timeline timeout 120s");
        m_equipTestStage = 90;
    }

    // 便捷：设置 slime 测试掉落（wooden/iron sword 各 x1 必掉）
    auto setSwordOverride = [this](const char* itemId) {
        std::vector<legend::world::LootEntry> table;
        legend::world::LootEntry entry;
        entry.itemId = itemId;
        entry.chance = 1.0f;
        entry.min = 1;
        entry.max = 1;
        table.push_back(entry);
        m_worldActors.GetSpawner().SetTestLootOverride("slime", table);
    };
    // 便捷：找最近活 slime 并传送玩家到攻击距离
    auto teleportToSlime = [this, &registry]() -> legend::world::MonsterCharacter* {
        legend::world::MonsterCharacter* slime = nullptr;
        for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
            if (monster != nullptr && monster->IsCombatAlive() &&
                monster->GetName() == "Slime") {
                slime = monster;
                break;
            }
        }
        if (slime != nullptr) {
            m_equipTestSlimeId = slime->GetId();
            m_player->SetPosition(slime->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
            m_playerCombat.GetTarget().SetTarget(slime->GetId());
        }
        return slime;
    };

    switch (m_equipTestStage) {
    case 0: { // 初始化 + Override wooden_sword + 选怪
        if (m_player == nullptr) {
            fail("player missing");
            m_equipTestStage = 90;
            break;
        }
        setSwordOverride("wooden_sword");
        m_equipTestAttackBase = m_player->GetCombatStats().attack;
        if (teleportToSlime() == nullptr) {
            if (m_equipTestStageElapsed > 10.0) {
                fail("no alive slime within 10s");
                m_equipTestStage = 90;
            }
            break;
        }
        m_equipTestStage = 1;
        break;
    }
    case 1: { // 击杀（真实攻击链路）
        auto* slime =
            static_cast<legend::world::MonsterCharacter*>(registry.Get(m_equipTestSlimeId));
        if (slime == nullptr) {
            fail("slime despawned before death");
            m_equipTestStage = 90;
            break;
        }
        if (m_player->GetActionState() == legend::entity::CharacterActionState::Normal) {
            (void)m_playerCombat.RequestAttack(*m_player, registry,
                                               m_worldActors.GetCombatSystem());
        }
        if (slime->GetActionState() == legend::entity::CharacterActionState::Dead) {
            pass("monster killed (wooden_sword override)");
            m_equipTestStage = 2;
        } else if (m_equipTestStageElapsed > 30.0) {
            fail("slime not dead within 30s");
            m_equipTestStage = 90;
        }
        break;
    }
    case 2: { // 验证真实 Equipment GroundLoot 生成（wooden_sword x1）
        const auto& loots = m_worldActors.GetLoot().GetAll();
        bool found = false;
        for (const auto& loot : loots) {
            if (loot.itemId == "wooden_sword" && loot.quantity == 1) {
                found = true;
            }
        }
        if (!found) {
            fail("no wooden_sword ground loot after kill");
            m_equipTestStage = 90;
            break;
        }
        pass("real equipment ground loot generated");
        // 传送到掉落旁
        for (const auto& loot : loots) {
            if (loot.itemId == "wooden_sword") {
                m_player->SetPosition(loot.position + legend::math::Vector2(10.0f, 0.0f));
                break;
            }
        }
        m_equipTestStage = 3;
        break;
    }
    case 3: { // 自动拾取 -> 记录 wooden_sword instanceId（A）
        const int picked = m_worldActors.GetLoot().PickupNearest(
            m_player->GetPosition(), 80.0f, m_player->GetInventory(),
            m_worldActors.GetItemDatabase());
        if (picked != 1) {
            fail("pickup failed (picked " + std::to_string(picked) + ")");
            m_equipTestStage = 90;
            break;
        }
        pass("picked wooden_sword");
        // 找背包中的 wooden_sword instanceId
        m_equipTestSwordA = 0;
        auto& bag = m_player->GetInventory();
        for (std::size_t i = 0; i < bag.GetCapacity(); ++i) {
            const auto* slot = bag.GetSlot(i);
            if (slot != nullptr && slot->definitionId == "wooden_sword") {
                m_equipTestSwordA = slot->instanceId;
            }
        }
        if (m_equipTestSwordA == 0) {
            fail("wooden_sword instance missing after pickup");
            m_equipTestStage = 90;
            break;
        }
        pass("wooden_sword instanceId A=" + std::to_string(m_equipTestSwordA));
        m_equipTestStage = 4;
        break;
    }
    case 4: { // Equip A -> 槽 instanceId 一致 + final attack = before + 12（相对差值，
              // 不用静态快照——中途升级会让 base attack 漂移）
        const float attackBefore = m_player->GetCombatStats().attack;
        const auto result = m_player->EquipInstance(m_equipTestSwordA);
        if (!result.success) {
            fail("equip A failed: " + result.reason);
            m_equipTestStage = 90;
            break;
        }
        const auto* weapon =
            m_player->GetEquipment().GetEquipped(legend::item::EquipmentSlotType::Weapon);
        check("weapon slot holds A (instanceId unchanged)",
              weapon != nullptr && weapon->instanceId == m_equipTestSwordA);
        check("final attack == before + 12",
              std::fabs(m_player->GetCombatStats().attack - (attackBefore + 12.0f)) < 0.001f);
        m_equipTestStage = 5;
        break;
    }
    case 5: { // Override 换 iron_sword + 传送下一只 slime
        setSwordOverride("iron_sword");
        if (teleportToSlime() == nullptr) {
            if (m_equipTestStageElapsed > 15.0) {
                fail("no second slime within 15s");
                m_equipTestStage = 90;
            }
            break;
        }
        m_equipTestStage = 6;
        break;
    }
    case 6: { // 击杀第二只（iron_sword）
        auto* slime =
            static_cast<legend::world::MonsterCharacter*>(registry.Get(m_equipTestSlimeId));
        if (slime == nullptr) {
            fail("second slime despawned before death");
            m_equipTestStage = 90;
            break;
        }
        if (m_player->GetActionState() == legend::entity::CharacterActionState::Normal) {
            (void)m_playerCombat.RequestAttack(*m_player, registry,
                                               m_worldActors.GetCombatSystem());
        }
        if (slime->GetActionState() == legend::entity::CharacterActionState::Dead) {
            pass("second monster killed (iron_sword override)");
            m_equipTestStage = 7;
        } else if (m_equipTestStageElapsed > 30.0) {
            fail("second slime not dead within 30s");
            m_equipTestStage = 90;
        }
        break;
    }
    case 7: { // 验证 iron_sword 掉落 + 拾取（B）
        const auto& loots = m_worldActors.GetLoot().GetAll();
        bool found = false;
        for (const auto& loot : loots) {
            if (loot.itemId == "iron_sword" && loot.quantity == 1) {
                found = true;
                m_player->SetPosition(loot.position + legend::math::Vector2(10.0f, 0.0f));
            }
        }
        if (!found) {
            fail("no iron_sword ground loot");
            m_equipTestStage = 90;
            break;
        }
        const int picked = m_worldActors.GetLoot().PickupNearest(
            m_player->GetPosition(), 80.0f, m_player->GetInventory(),
            m_worldActors.GetItemDatabase());
        if (picked != 1) {
            fail("iron_sword pickup failed");
            m_equipTestStage = 90;
            break;
        }
        m_equipTestSwordB = 0;
        auto& bag = m_player->GetInventory();
        for (std::size_t i = 0; i < bag.GetCapacity(); ++i) {
            const auto* slot = bag.GetSlot(i);
            if (slot != nullptr && slot->definitionId == "iron_sword") {
                m_equipTestSwordB = slot->instanceId;
            }
        }
        if (m_equipTestSwordB == 0) {
            fail("iron_sword instance missing");
            m_equipTestStage = 90;
            break;
        }
        pass("iron_sword picked, instanceId B=" + std::to_string(m_equipTestSwordB));
        m_equipTestStage = 8;
        break;
    }
    case 8: { // Swap：Equip B -> 槽=B、A 回背包（instanceId 不变）、attack +13（25-12 相对差）
        const float attackBefore = m_player->GetCombatStats().attack;
        const auto result = m_player->EquipInstance(m_equipTestSwordB);
        if (!result.success) {
            fail("swap to B failed: " + result.reason);
            m_equipTestStage = 90;
            break;
        }
        const auto* weapon =
            m_player->GetEquipment().GetEquipped(legend::item::EquipmentSlotType::Weapon);
        check("swap: weapon slot == B",
              weapon != nullptr && weapon->instanceId == m_equipTestSwordB);
        check("swap: A returned to inventory (instanceId kept)",
              m_player->GetInventory().FindByInstanceId(m_equipTestSwordA) != nullptr);
        check("swap: final attack == before + 13 (25 - 12)",
              std::fabs(m_player->GetCombatStats().attack - (attackBefore + 13.0f)) < 0.001f);
        m_equipTestStage = 9;
        break;
    }
    case 9: { // Unequip -> attack 回 base（相对 -25）、B 回背包
        const float attackBefore = m_player->GetCombatStats().attack;
        const auto result =
            m_player->UnequipSlot(legend::item::EquipmentSlotType::Weapon);
        if (!result.success) {
            fail("unequip failed: " + result.reason);
            m_equipTestStage = 90;
            break;
        }
        check("unequip: weapon slot empty",
              m_player->GetEquipment().GetEquipped(
                  legend::item::EquipmentSlotType::Weapon) == nullptr);
        check("unequip: B returned to inventory (instanceId kept)",
              m_player->GetInventory().FindByInstanceId(m_equipTestSwordB) != nullptr);
        check("unequip: final attack == before - 25 (back to unequipped level)",
              std::fabs(m_player->GetCombatStats().attack - (attackBefore - 25.0f)) < 0.001f);
        m_equipTestStage = 10;
        break;
    }
    case 10: { // instanceId 全程唯一 + 恢复 slime 正式掉落表 + Respawn 验证
        std::vector<legend::item::ItemInstanceId> ids;
        auto& bag = m_player->GetInventory();
        for (std::size_t i = 0; i < bag.GetCapacity(); ++i) {
            const auto* slot = bag.GetSlot(i);
            if (slot != nullptr) {
                ids.push_back(slot->instanceId);
            }
        }
        m_player->GetEquipment().ForEachEquipped(
            [&ids](const legend::item::ItemInstance& instance) {
                ids.push_back(instance.instanceId);
            });
        std::sort(ids.begin(), ids.end());
        check("instanceIds unique across inventory+equipment",
              std::adjacent_find(ids.begin(), ids.end()) == ids.end());
        check("A and B instanceIds preserved end-to-end",
              std::find(ids.begin(), ids.end(), m_equipTestSwordA) != ids.end() &&
                  std::find(ids.begin(), ids.end(), m_equipTestSwordB) != ids.end());
        // 还原正式掉落表（monster.json 原表）
        m_worldActors.GetSpawner().SetTestLootOverride("slime", {});
        pass("loot override restored to official table");
        LOG_INFO("[EquipmentTest] waiting monster respawn for final check.");
        m_equipTestStage = 11;
        break;
    }
    case 11: { // Respawn 正常
        if (m_equipTestStageElapsed < 8.0) {
            break; // corpse 1.5s + respawn 5s
        }
        const int aliveNow = m_worldActors.GetAliveMonsterCount();
        if (aliveNow > 0) {
            pass("monster respawned (alive " + std::to_string(aliveNow) + ")");
        } else if (m_equipTestStageElapsed < 30.0) {
            break;
        } else {
            fail("no monster respawned within 30s");
        }
        m_equipTestStage = 90;
        break;
    }
    case 90: { // 汇总
        if (!m_equipTestSummaryDone) {
            m_equipTestSummaryDone = true;
            LOG_INFO("[EquipmentTest] completed, failures = " +
                     std::to_string(m_equipTestFailures) + ", elapsed = " +
                     std::to_string(m_equipTestElapsed) + "s");
        }
        break;
    }
    default:
        break;
    }
}

// ==================== F6：Equipment Debug 覆盖层 ====================
// 无文字系统：几何显示（6 槽方块：有装备亮绿/空灰；Base->Final ATK 差值连线）。

void GameScene::DrawEquipmentDebugOverlay(legend::render::SpriteBatch& batch) {
    if (m_player == nullptr || !m_whiteTexture) {
        return;
    }
    const legend::math::Vector2 playerPos = m_player->GetPosition();
    const auto& equipment = m_player->GetEquipment();
    // 6 槽横排小方块（头顶上方）：有装备亮绿、空槽灰
    for (int i = 0; i < legend::item::kEquipmentSlotCount; ++i) {
        const auto slot = static_cast<legend::item::EquipmentSlotType>(i);
        const bool hasItem = !equipment.IsSlotEmpty(slot);
        const legend::math::Color color = hasItem
                                              ? legend::math::Color(0.3f, 0.95f, 0.4f, 0.9f)
                                              : legend::math::Color(0.5f, 0.5f, 0.5f, 0.35f);
        const legend::math::Vector2 center(playerPos.x + (i - 2.5f) * 18.0f,
                                           playerPos.y - 70.0f);
        batch.DrawQuad(*m_whiteTexture, center, legend::math::Vector2(14.0f, 14.0f), 0.0f,
                       color, false, false, 0.0f, 0.0f, 1.0f, 1.0f);
    }
    // Base ATK -> Final ATK 差值竖线（右侧）：差值越大越高
    const float baseAtk = m_player->GetBaseCombatStats().attack;
    const float finalAtk = m_player->GetCombatStats().attack;
    const float atkBonus = finalAtk - baseAtk;
    const float barHeight = std::clamp(atkBonus, 0.0f, 50.0f);
    if (barHeight > 0.5f) {
        const legend::math::Vector2 barBase(playerPos.x + 60.0f, playerPos.y - 40.0f);
        batch.DrawQuad(*m_whiteTexture,
                       legend::math::Vector2(barBase.x, barBase.y - barHeight * 0.5f),
                       legend::math::Vector2(6.0f, barHeight), 0.0f,
                       legend::math::Color(0.95f, 0.75f, 0.2f, 0.85f), false, false, 0.0f,
                       0.0f, 1.0f, 1.0f);
    }
}

// ==================== 窗口标题 Equipment 段（在 LogMapStats 追加） ====================
// 由 GameScene::LogMapStats 调用：返回 " | Equip: n/6"（有装备槽数）。
std::string GameScene::GetEquipmentStatusText() const {
    if (m_player == nullptr) {
        return std::string();
    }
    return " | Equip: " + std::to_string(m_player->GetEquipment().GetEquippedCount()) + "/" +
           std::to_string(legend::item::kEquipmentSlotCount);
}
