// 阶段7：Equipment Core System 自检实现（14 个新 Check，独立文件避免 GameScene 膨胀）。
// 全部结果写日志 [XxxCheck] ... -> PASS/FAIL + completed, failures = N。
// 组件级 Check 用局部 ItemDatabase/Inventory/EquipmentComponent/PlayerStatsComponent
// （与 PlayerCharacter::EquipInstance 相同调用序列，语义等价）；
// 真实世界集成（Loot 链路）用 m_worldActors（与自动时间线一致）。
#include "Client/Source/GameScene.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Client/Character/PlayerCharacter.h"
#include "Client/Loot/LootManager.h"
#include "Client/World/MonsterCharacter.h"
#include "Client/World/MonsterDefinition.h"
#include "Client/World/MonsterSpawner.h"
#include "Client/World/WorldActorManager.h"
#include "Engine/Combat/CombatSystem.h"
#include "Engine/Core/Engine.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Item/EquipmentComponent.h"
#include "Engine/Item/EquipmentSystem.h"
#include "Engine/Item/ItemDatabase.h"
#include "Engine/Item/ItemDefinition.h"
#include "Engine/Item/ItemInstance.h"
#include "Engine/Item/Inventory.h"

using legend::Engine;

namespace {

// 统一 Check 输出
void LogEquipCheck(const char* tag, const std::string& name, bool pass, int& failures) {
    LOG_INFO(std::string("[") + tag + "] " + name + " -> " + (pass ? "PASS" : "FAIL"));
    if (!pass) {
        ++failures;
    }
}

// 程序化测试物品库（不依赖 JSON）：阶段6 材料/消耗品 + 阶段7 九件装备。
// 数值与 Assets/Items/items.json 一致（EquipmentStatsCheck 用同一组数）。
legend::item::ItemDatabase MakeEquipmentTestDatabase() {
    legend::item::ItemDatabase database; // 程序化填充（绕过 JSON）
    auto addItem = [&database](const std::string& id, legend::item::ItemType type, int maxStack,
                               bool hasEquipment, legend::item::EquipmentSlotType slot,
                               float attack, float defense, float maxHp) {
        legend::item::ItemDefinition def;
        def.id = id;
        def.name = id;
        def.type = type;
        def.maxStack = maxStack;
        def.hasEquipment = hasEquipment;
        if (hasEquipment) {
            def.equipment.slot = slot;
            def.equipment.attackBonus = attack;
            def.equipment.defenseBonus = defense;
            def.equipment.maxHpBonus = maxHp;
        }
        // 直接注入内部表（测试专用构造接口）
        database.AddTestItem(std::move(def));
    };
    // 阶段6 物品
    addItem("small_potion", legend::item::ItemType::Consumable, 20, false,
            legend::item::EquipmentSlotType::Weapon, 0, 0, 0);
    addItem("slime_gel", legend::item::ItemType::Material, 50, false,
            legend::item::EquipmentSlotType::Weapon, 0, 0, 0);
    // 阶段7 九件装备（数值与 items.json 一致）
    addItem("wooden_sword", legend::item::ItemType::Equipment, 1, true,
            legend::item::EquipmentSlotType::Weapon, 12.0f, 0.0f, 0.0f);
    addItem("iron_sword", legend::item::ItemType::Equipment, 1, true,
            legend::item::EquipmentSlotType::Weapon, 25.0f, 0.0f, 0.0f);
    addItem("cloth_helmet", legend::item::ItemType::Equipment, 1, true,
            legend::item::EquipmentSlotType::Helmet, 0.0f, 5.0f, 0.0f);
    addItem("iron_helmet", legend::item::ItemType::Equipment, 1, true,
            legend::item::EquipmentSlotType::Helmet, 0.0f, 12.0f, 0.0f);
    addItem("cloth_armor", legend::item::ItemType::Equipment, 1, true,
            legend::item::EquipmentSlotType::Armor, 0.0f, 10.0f, 100.0f);
    addItem("iron_armor", legend::item::ItemType::Equipment, 1, true,
            legend::item::EquipmentSlotType::Armor, 0.0f, 20.0f, 150.0f);
    addItem("simple_necklace", legend::item::ItemType::Equipment, 1, true,
            legend::item::EquipmentSlotType::Necklace, 3.0f, 0.0f, 0.0f);
    addItem("copper_ring", legend::item::ItemType::Equipment, 1, true,
            legend::item::EquipmentSlotType::Ring, 5.0f, 0.0f, 0.0f);
    addItem("leather_boots", legend::item::ItemType::Equipment, 1, true,
            legend::item::EquipmentSlotType::Boots, 0.0f, 3.0f, 0.0f);
    return database;
}

// 便捷：往背包塞一件装备实例并返回 instanceId
legend::item::ItemInstanceId InsertEquipment(legend::item::Inventory& inventory,
                                             const legend::item::ItemDefinition& def) {
    legend::item::ItemInstance instance;
    instance.instanceId = legend::item::ItemInstanceIdAllocator::Next();
    instance.definitionId = def.id;
    instance.quantity = 1;
    if (!inventory.AddInstance(instance, &def)) {
        return 0; // 满包等
    }
    return instance.instanceId;
}

} // namespace

// ==================== [EquipmentDefinitionCheck] ====================

void GameScene::RunEquipmentDefinitionCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogEquipCheck("EquipmentDefinitionCheck", name, pass, failures);
    };
    const auto database = MakeEquipmentTestDatabase();
    const legend::item::ItemDefinition* sword = database.Get("wooden_sword");
    check("wooden_sword present", sword != nullptr);
    if (sword != nullptr) {
        check("type Equipment", sword->type == legend::item::ItemType::Equipment);
        check("slot Weapon", sword->hasEquipment &&
                                 sword->equipment.slot == legend::item::EquipmentSlotType::Weapon);
        check("maxStack == 1", sword->maxStack == 1);
        check("attackBonus == 12", std::fabs(sword->equipment.attackBonus - 12.0f) < 0.001f);
    }
    // 非法：Equipment maxStack=20 → Invalid（IsEquipmentValid false）
    legend::item::ItemDefinition badStack;
    badStack.id = "bad_stack_sword";
    badStack.name = "BadStack";
    badStack.type = legend::item::ItemType::Equipment;
    badStack.maxStack = 20;
    badStack.hasEquipment = true;
    badStack.equipment.slot = legend::item::EquipmentSlotType::Weapon;
    badStack.equipment.attackBonus = 10.0f;
    check("equipment maxStack=20 -> invalid", !badStack.IsEquipmentValid());
    // 非法：Equipment 无 equipment 块 → Invalid
    legend::item::ItemDefinition noBlock;
    noBlock.id = "no_block_sword";
    noBlock.name = "NoBlock";
    noBlock.type = legend::item::ItemType::Equipment;
    noBlock.maxStack = 1;
    noBlock.hasEquipment = false;
    check("equipment without block -> invalid", !noBlock.IsEquipmentValid());
    // 非法：负 bonus → EquipmentData invalid
    legend::item::ItemDefinition negative;
    negative.id = "negative_sword";
    negative.name = "Negative";
    negative.type = legend::item::ItemType::Equipment;
    negative.maxStack = 1;
    negative.hasEquipment = true;
    negative.equipment.slot = legend::item::EquipmentSlotType::Weapon;
    negative.equipment.attackBonus = -1.0f;
    check("negative attackBonus -> invalid", !negative.equipment.IsValid());
    // JSON 解析路径：正式 items.json 9 件装备全部加载成功
    legend::item::ItemDatabase official;
    check("official items.json loads",
          official.LoadFromFile(Engine::Get().GetResources().GetAssetRoot() +
                                "/Items/items.json"));
    check("official has 9 equipment items",
          [ &official ] {
              int equipmentCount = 0;
              for (const auto& [id, def] : official.GetAll()) {
                  if (def.type == legend::item::ItemType::Equipment) {
                      ++equipmentCount;
                  }
              }
              return equipmentCount >= 9;
          }());
    LOG_INFO("[EquipmentDefinitionCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [EquipmentSlotCheck] ====================

void GameScene::RunEquipmentSlotCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogEquipCheck("EquipmentSlotCheck", name, pass, failures);
    };
    // 槽位字符串解析：6 合法 + 未知拒绝（不默认 Weapon）
    legend::item::EquipmentSlotType parsed = legend::item::EquipmentSlotType::Weapon;
    check("parse Weapon", legend::item::ParseEquipmentSlotType("Weapon", parsed) &&
                              parsed == legend::item::EquipmentSlotType::Weapon);
    check("parse Helmet", legend::item::ParseEquipmentSlotType("Helmet", parsed) &&
                              parsed == legend::item::EquipmentSlotType::Helmet);
    check("parse Armor", legend::item::ParseEquipmentSlotType("Armor", parsed) &&
                             parsed == legend::item::EquipmentSlotType::Armor);
    check("parse Necklace", legend::item::ParseEquipmentSlotType("Necklace", parsed) &&
                                parsed == legend::item::EquipmentSlotType::Necklace);
    check("parse Ring", legend::item::ParseEquipmentSlotType("Ring", parsed) &&
                            parsed == legend::item::EquipmentSlotType::Ring);
    check("parse Boots", legend::item::ParseEquipmentSlotType("Boots", parsed) &&
                             parsed == legend::item::EquipmentSlotType::Boots);
    check("unknown slot string -> invalid",
          !legend::item::ParseEquipmentSlotType("Wooden_Sword", parsed));
    check("empty slot string -> invalid",
          !legend::item::ParseEquipmentSlotType("", parsed));
    // 槽位匹配：definition.slot 决定槽位（Weapon 定义进 Weapon 槽）——防御性验证
    const auto database = MakeEquipmentTestDatabase();
    const legend::item::ItemDefinition* helmet = database.Get("cloth_helmet");
    check("helmet slot is Helmet",
          helmet != nullptr && helmet->hasEquipment &&
              helmet->equipment.slot == legend::item::EquipmentSlotType::Helmet);
    LOG_INFO("[EquipmentSlotCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [InventoryInstanceCheck] ====================

void GameScene::RunInventoryInstanceCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogEquipCheck("InventoryInstanceCheck", name, pass, failures);
    };
    const auto database = MakeEquipmentTestDatabase();
    legend::item::Inventory inventory;
    const legend::item::ItemDefinition* sword = database.Get("wooden_sword");

    // TakeInstance：不存在 -> nullopt
    check("take missing -> nullopt",
          !inventory.TakeInstance(999999).has_value());

    // AddInstance：保留 instanceId、占一格
    const legend::item::ItemInstanceId idA = InsertEquipment(inventory, *sword);
    check("add instance ok", idA != 0 && inventory.GetUsedSlots() == 1);
    const legend::item::ItemInstance* found = inventory.FindByInstanceId(idA);
    check("find by instanceId", found != nullptr && found->instanceId == idA &&
                                    found->definitionId == "wooden_sword");

    // TakeInstance：移除 + usedSlots 减少 + 返回原实例
    auto taken = inventory.TakeInstance(idA);
    check("take returns original instance",
          taken.has_value() && taken->instanceId == idA &&
              taken->definitionId == "wooden_sword");
    check("usedSlots decreased", inventory.GetUsedSlots() == 0);
    check("slot emptied", inventory.FindByInstanceId(idA) == nullptr);

    // Equipment quantity != 1 -> 拒绝（阶段7 指令六十八）
    legend::item::ItemInstance badQty;
    badQty.instanceId = legend::item::ItemInstanceIdAllocator::Next();
    badQty.definitionId = "wooden_sword";
    badQty.quantity = 2;
    check("equipment quantity != 1 rejected",
          !inventory.AddInstance(badQty, sword));

    // 满包 AddInstance -> false（不生成新 ID）
    legend::item::ItemDefinition unique;
    unique.id = "instance_fill_unique";
    unique.name = "Filler";
    unique.maxStack = 1;
    for (std::size_t i = 0; i < inventory.GetCapacity(); ++i) {
        (void)inventory.AddItem(unique, 1);
    }
    legend::item::ItemInstance overflow;
    overflow.instanceId = legend::item::ItemInstanceIdAllocator::Next();
    overflow.definitionId = "wooden_sword";
    overflow.quantity = 1;
    const legend::item::ItemInstanceId overflowId = overflow.instanceId;
    check("full inventory AddInstance -> false",
          !inventory.AddInstance(overflow, sword));
    check("instance id not consumed on failure",
          inventory.FindByInstanceId(overflowId) == nullptr);
    LOG_INFO("[InventoryInstanceCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [EquipCheck] / [UnequipCheck] ====================

void GameScene::RunEquipCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogEquipCheck("EquipCheck", name, pass, failures);
    };
    const auto database = MakeEquipmentTestDatabase();
    legend::item::Inventory inventory;
    legend::item::EquipmentComponent equipment;
    PlayerStatsComponent stats;
    legend::combat::CombatStats finalStats;
    finalStats.maxHp = 500.0f;
    finalStats.hp = 500.0f;
    finalStats.attack = 80.0f;
    finalStats.defense = 20.0f;
    stats.Initialize(finalStats);

    const legend::item::ItemDefinition* sword = database.Get("wooden_sword");
    const legend::item::ItemInstanceId idA = InsertEquipment(inventory, *sword);

    // Equip（与 PlayerCharacter::EquipInstance 相同调用序列）
    const auto equipResult =
        legend::item::EquipmentSystem::Equip(database, inventory, equipment, idA);
    check("equip success", equipResult.success);
    check("inventory no longer has A", inventory.FindByInstanceId(idA) == nullptr);
    const legend::item::ItemInstance* equipped = equipment.GetEquipped(
        legend::item::EquipmentSlotType::Weapon);
    check("weapon slot holds A", equipped != nullptr && equipped->instanceId == idA);
    check("definitionId unchanged",
          equipped != nullptr && equipped->definitionId == "wooden_sword");
    stats.RecalculateFinalStats(finalStats, equipment, database);
    check("final attack 80 -> 92", std::fabs(finalStats.attack - 92.0f) < 0.001f);
    LOG_INFO("[EquipCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunUnequipCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogEquipCheck("UnequipCheck", name, pass, failures);
    };
    const auto database = MakeEquipmentTestDatabase();
    legend::item::Inventory inventory;
    legend::item::EquipmentComponent equipment;
    PlayerStatsComponent stats;
    legend::combat::CombatStats finalStats;
    finalStats.maxHp = 500.0f;
    finalStats.hp = 500.0f;
    finalStats.attack = 80.0f;
    finalStats.defense = 20.0f;
    stats.Initialize(finalStats);

    const legend::item::ItemDefinition* sword = database.Get("wooden_sword");
    const legend::item::ItemInstanceId idA = InsertEquipment(inventory, *sword);
    (void)legend::item::EquipmentSystem::Equip(database, inventory, equipment, idA);
    stats.RecalculateFinalStats(finalStats, equipment, database);

    // Unequip：槽空、Inventory 重新出现 A、instanceId 仍 A、attack 恢复
    const auto unequipResult = legend::item::EquipmentSystem::Unequip(
        database, inventory, equipment, legend::item::EquipmentSlotType::Weapon);
    check("unequip success", unequipResult.success);
    check("weapon slot empty",
          equipment.GetEquipped(legend::item::EquipmentSlotType::Weapon) == nullptr);
    const legend::item::ItemInstance* returned = inventory.FindByInstanceId(idA);
    check("inventory holds A again", returned != nullptr && returned->instanceId == idA);
    stats.RecalculateFinalStats(finalStats, equipment, database);
    check("final attack restored to 80", std::fabs(finalStats.attack - 80.0f) < 0.001f);
    LOG_INFO("[UnequipCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [EquipmentSwapCheck] ====================

void GameScene::RunEquipmentSwapCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogEquipCheck("EquipmentSwapCheck", name, pass, failures);
    };
    const auto database = MakeEquipmentTestDatabase();
    legend::item::Inventory inventory;
    legend::item::EquipmentComponent equipment;

    const legend::item::ItemDefinition* wooden = database.Get("wooden_sword");
    const legend::item::ItemDefinition* iron = database.Get("iron_sword");
    const legend::item::ItemInstanceId idA = InsertEquipment(inventory, *wooden); // A
    const legend::item::ItemInstanceId idB = InsertEquipment(inventory, *iron);   // B

    (void)legend::item::EquipmentSystem::Equip(database, inventory, equipment, idA);

    // Swap：Equip B -> 槽=B、包含 A 无 B、两个 instanceId 不变
    const auto swapResult =
        legend::item::EquipmentSystem::Equip(database, inventory, equipment, idB);
    check("swap equip success", swapResult.success);
    const legend::item::ItemInstance* weaponSlot = equipment.GetEquipped(
        legend::item::EquipmentSlotType::Weapon);
    check("weapon slot == B", weaponSlot != nullptr && weaponSlot->instanceId == idB);
    check("inventory holds A", inventory.FindByInstanceId(idA) != nullptr);
    check("inventory no longer has B", inventory.FindByInstanceId(idB) == nullptr);
    check("both instanceIds preserved",
          weaponSlot != nullptr && weaponSlot->instanceId == idB &&
              inventory.FindByInstanceId(idA)->instanceId == idA);
    LOG_INFO("[EquipmentSwapCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [FullInventoryUnequipCheck] / [FullInventorySwapCheck] ====================

void GameScene::RunFullInventoryUnequipCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogEquipCheck("FullInventoryUnequipCheck", name, pass, failures);
    };
    const auto database = MakeEquipmentTestDatabase();
    legend::item::Inventory inventory;
    legend::item::EquipmentComponent equipment;

    const legend::item::ItemDefinition* sword = database.Get("wooden_sword");
    const legend::item::ItemInstanceId idA = InsertEquipment(inventory, *sword);
    (void)legend::item::EquipmentSystem::Equip(database, inventory, equipment, idA);

    // 填满背包
    legend::item::ItemDefinition filler;
    filler.id = "full_unequip_filler";
    filler.name = "Filler";
    filler.maxStack = 1;
    for (std::size_t i = inventory.GetUsedSlots(); i < inventory.GetCapacity(); ++i) {
        (void)inventory.AddItem(filler, 1);
    }
    check("inventory full", inventory.IsFull());

    // Unequip：背包满 -> 失败、装备留槽、背包数量不变
    const std::size_t usedBefore = inventory.GetUsedSlots();
    const auto unequipResult = legend::item::EquipmentSystem::Unequip(
        database, inventory, equipment, legend::item::EquipmentSlotType::Weapon);
    check("unequip fails when full", !unequipResult.success);
    const legend::item::ItemInstance* stillEquipped = equipment.GetEquipped(
        legend::item::EquipmentSlotType::Weapon);
    check("weapon still equipped (A kept)", stillEquipped != nullptr &&
                                               stillEquipped->instanceId == idA);
    check("inventory count unchanged", inventory.GetUsedSlots() == usedBefore);
    LOG_INFO("[FullInventoryUnequipCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunFullInventorySwapCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogEquipCheck("FullInventorySwapCheck", name, pass, failures);
    };
    const auto database = MakeEquipmentTestDatabase();
    legend::item::Inventory inventory;
    legend::item::EquipmentComponent equipment;

    const legend::item::ItemDefinition* wooden = database.Get("wooden_sword");
    const legend::item::ItemDefinition* iron = database.Get("iron_sword");
    const legend::item::ItemInstanceId idA = InsertEquipment(inventory, *wooden);
    (void)legend::item::EquipmentSystem::Equip(database, inventory, equipment, idA);

    // 背包填满（B 也占一格——B 在背包中）
    const legend::item::ItemInstanceId idB = InsertEquipment(inventory, *iron);
    legend::item::ItemDefinition filler;
    filler.id = "full_swap_filler";
    filler.name = "Filler";
    filler.maxStack = 1;
    for (std::size_t i = inventory.GetUsedSlots(); i < inventory.GetCapacity(); ++i) {
        (void)inventory.AddItem(filler, 1);
    }
    check("inventory full with B inside", inventory.IsFull() &&
                                             inventory.FindByInstanceId(idB) != nullptr);

    // Swap（Equip B）：B 腾出的原位置接收 A -> 必须成功（重要边界）
    const std::size_t usedBefore = inventory.GetUsedSlots();
    const auto swapResult =
        legend::item::EquipmentSystem::Equip(database, inventory, equipment, idB);
    check("full-inventory swap succeeds (B vacates a slot)", swapResult.success);
    const legend::item::ItemInstance* weaponSlot = equipment.GetEquipped(
        legend::item::EquipmentSlotType::Weapon);
    check("weapon == B", weaponSlot != nullptr && weaponSlot->instanceId == idB);
    check("A returned to inventory", inventory.FindByInstanceId(idA) != nullptr);
    check("inventory still full (same count)", inventory.GetUsedSlots() == usedBefore &&
                                                   inventory.IsFull());
    check("no instance lost",
          inventory.FindByInstanceId(idA) != nullptr &&
              inventory.FindByInstanceId(idB) == nullptr && weaponSlot != nullptr);
    LOG_INFO("[FullInventorySwapCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [EquipmentUniqueInstanceCheck] ====================

void GameScene::RunEquipmentUniqueInstanceCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogEquipCheck("EquipmentUniqueInstanceCheck", name, pass, failures);
    };
    const auto database = MakeEquipmentTestDatabase();
    legend::item::Inventory inventory;
    legend::item::EquipmentComponent equipment;

    const legend::item::ItemDefinition* wooden = database.Get("wooden_sword");
    const legend::item::ItemDefinition* iron = database.Get("iron_sword");
    const legend::item::ItemInstanceId idA = InsertEquipment(inventory, *wooden);
    const legend::item::ItemInstanceId idB = InsertEquipment(inventory, *iron);

    // 扫描函数：收集 Inventory + Equipment 全部 instanceId，检查重复
    auto collectIds = [&inventory, &equipment]() {
        std::vector<legend::item::ItemInstanceId> ids;
        for (std::size_t i = 0; i < inventory.GetCapacity(); ++i) {
            const legend::item::ItemInstance* slot = inventory.GetSlot(i);
            if (slot != nullptr) {
                ids.push_back(slot->instanceId);
            }
        }
        equipment.ForEachEquipped([&ids](const legend::item::ItemInstance& instance) {
            ids.push_back(instance.instanceId);
        });
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    auto allUnique = [](const std::vector<legend::item::ItemInstanceId>& ids) {
        return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
    };

    check("unique before equip", allUnique(collectIds()));
    (void)legend::item::EquipmentSystem::Equip(database, inventory, equipment, idA);
    check("unique after equip", allUnique(collectIds()));
    (void)legend::item::EquipmentSystem::Equip(database, inventory, equipment, idB);
    check("unique after swap", allUnique(collectIds()));
    (void)legend::item::EquipmentSystem::Unequip(database, inventory, equipment,
                                                 legend::item::EquipmentSlotType::Weapon);
    check("unique after unequip", allUnique(collectIds()));
    LOG_INFO("[EquipmentUniqueInstanceCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [EquipmentStatsCheck] / [EquipmentHpClampCheck] ====================

void GameScene::RunEquipmentStatsCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogEquipCheck("EquipmentStatsCheck", name, pass, failures);
    };
    const auto database = MakeEquipmentTestDatabase();
    legend::item::Inventory inventory;
    legend::item::EquipmentComponent equipment;
    PlayerStatsComponent stats;
    legend::combat::CombatStats finalStats;
    finalStats.maxHp = 500.0f;
    finalStats.hp = 400.0f; // 当前 HP 400（穿装备不自动补满）
    finalStats.attack = 80.0f;
    finalStats.defense = 20.0f;
    stats.Initialize(finalStats);

    // 装备 Sword +12 ATK / Helmet +5 DEF / Armor +100 HP +10 DEF
    const auto equipSword = legend::item::EquipmentSystem::Equip(
        database, inventory, equipment,
        InsertEquipment(inventory, *database.Get("wooden_sword")));
    const auto equipHelmet = legend::item::EquipmentSystem::Equip(
        database, inventory, equipment,
        InsertEquipment(inventory, *database.Get("cloth_helmet")));
    const auto equipArmor = legend::item::EquipmentSystem::Equip(
        database, inventory, equipment,
        InsertEquipment(inventory, *database.Get("cloth_armor")));
    check("all three equipped", equipSword.success && equipHelmet.success && equipArmor.success);
    stats.RecalculateFinalStats(finalStats, equipment, database);
    check("final HP 600 (base 500 + armor 100)",
          std::fabs(finalStats.maxHp - 600.0f) < 0.001f);
    check("final ATK 92 (base 80 + sword 12)",
          std::fabs(finalStats.attack - 92.0f) < 0.001f);
    check("final DEF 35 (base 20 + helmet 5 + armor 10)",
          std::fabs(finalStats.defense - 35.0f) < 0.001f);
    check("current HP unchanged by equipping (no auto-heal)",
          std::fabs(finalStats.hp - 400.0f) < 0.001f);

    // 卸掉 Armor：HP 500 / ATK 92 / DEF 25
    const auto unequipArmor = legend::item::EquipmentSystem::Unequip(
        database, inventory, equipment, legend::item::EquipmentSlotType::Armor);
    check("armor unequipped", unequipArmor.success);
    stats.RecalculateFinalStats(finalStats, equipment, database);
    check("after unequip HP 500", std::fabs(finalStats.maxHp - 500.0f) < 0.001f);
    check("after unequip ATK 92 (sword kept)",
          std::fabs(finalStats.attack - 92.0f) < 0.001f);
    check("after unequip DEF 25 (helmet kept)",
          std::fabs(finalStats.defense - 25.0f) < 0.001f);
    LOG_INFO("[EquipmentStatsCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunEquipmentHpClampCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogEquipCheck("EquipmentHpClampCheck", name, pass, failures);
    };
    const auto database = MakeEquipmentTestDatabase();
    legend::item::Inventory inventory;
    legend::item::EquipmentComponent equipment;
    PlayerStatsComponent stats;
    legend::combat::CombatStats finalStats;
    finalStats.maxHp = 500.0f;
    finalStats.hp = 580.0f; // 当前 HP 580（大于 base maxHp——测试 clamp 场景）
    finalStats.attack = 80.0f;
    finalStats.defense = 20.0f;
    stats.Initialize(finalStats);

    // 穿 Armor +100：maxHp 600，HP 580 < 600 保持（不自动补满）
    const auto equipArmor = legend::item::EquipmentSystem::Equip(
        database, inventory, equipment,
        InsertEquipment(inventory, *database.Get("cloth_armor")));
    check("armor equipped", equipArmor.success);
    stats.RecalculateFinalStats(finalStats, equipment, database);
    if (finalStats.hp > finalStats.maxHp) {
        finalStats.hp = finalStats.maxHp; // PlayerCharacter::RecalculateCombatStats 的 clamp
    }
    check("equipped maxHp 600", std::fabs(finalStats.maxHp - 600.0f) < 0.001f);
    check("hp 580 kept (below new max, no heal)", std::fabs(finalStats.hp - 580.0f) < 0.001f);

    // 卸 Armor：maxHp 回 500 -> HP clamp 500（不丢 500 以外的量，也不补满）
    const auto unequipArmor = legend::item::EquipmentSystem::Unequip(
        database, inventory, equipment, legend::item::EquipmentSlotType::Armor);
    check("armor unequipped", unequipArmor.success);
    stats.RecalculateFinalStats(finalStats, equipment, database);
    if (finalStats.hp > finalStats.maxHp) {
        finalStats.hp = finalStats.maxHp;
    }
    check("unequipped maxHp 500", std::fabs(finalStats.maxHp - 500.0f) < 0.001f);
    check("hp clamped to 500", std::fabs(finalStats.hp - 500.0f) < 0.001f);
    LOG_INFO("[EquipmentHpClampCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [LevelEquipmentCheck] ====================
// Base 500/80/20 + Sword +12 -> final 500/92/20；升级 base->520/85/22 -> final 520/97/22。
// 装备加成既不丢失也不重复累计。

void GameScene::RunLevelEquipmentCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogEquipCheck("LevelEquipmentCheck", name, pass, failures);
    };
    const auto database = MakeEquipmentTestDatabase();
    legend::item::Inventory inventory;
    legend::item::EquipmentComponent equipment;
    PlayerStatsComponent stats;
    legend::combat::CombatStats finalStats;
    finalStats.maxHp = 500.0f;
    finalStats.hp = 500.0f;
    finalStats.attack = 80.0f;
    finalStats.defense = 20.0f;
    stats.Initialize(finalStats);

    // 装备 Sword
    const auto equipSword = legend::item::EquipmentSystem::Equip(
        database, inventory, equipment,
        InsertEquipment(inventory, *database.Get("wooden_sword")));
    check("sword equipped", equipSword.success);
    stats.RecalculateFinalStats(finalStats, equipment, database);
    check("level1 final 500/92/20",
          std::fabs(finalStats.maxHp - 500.0f) < 0.001f &&
              std::fabs(finalStats.attack - 92.0f) < 0.001f &&
              std::fabs(finalStats.defense - 20.0f) < 0.001f);

    // 升级：base -> 520/85/22，装备加成保持 -> final 520/97/22
    stats.ApplyLevelGrowth(legend::progression::GrowthConfig{}, 1);
    stats.RecalculateFinalStats(finalStats, equipment, database);
    check("after level up base 520/85/22",
          std::fabs(stats.GetBaseStats().maxHp - 520.0f) < 0.001f &&
              std::fabs(stats.GetBaseStats().attack - 85.0f) < 0.001f &&
              std::fabs(stats.GetBaseStats().defense - 22.0f) < 0.001f);
    check("after level up final 520/97/22 (equipment kept, not doubled)",
          std::fabs(finalStats.maxHp - 520.0f) < 0.001f &&
              std::fabs(finalStats.attack - 97.0f) < 0.001f &&
              std::fabs(finalStats.defense - 22.0f) < 0.001f);

    // 卸下 Sword：final 回 base（520/85/22），无漂移
    const auto unequip = legend::item::EquipmentSystem::Unequip(
        database, inventory, equipment, legend::item::EquipmentSlotType::Weapon);
    check("sword unequipped", unequip.success);
    stats.RecalculateFinalStats(finalStats, equipment, database);
    check("unequip after level up -> final == base (520/85/22)",
          std::fabs(finalStats.maxHp - 520.0f) < 0.001f &&
              std::fabs(finalStats.attack - 85.0f) < 0.001f &&
              std::fabs(finalStats.defense - 22.0f) < 0.001f);
    LOG_INFO("[LevelEquipmentCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [EquipmentLootCheck] ====================
// 真实链路：Test Loot Override（100% wooden_sword x1）-> DeathEvent -> RewardSystem ->
// LootRoll -> GroundLoot -> Pickup -> Inventory（wooden_sword quantity 1、maxStack 语义）。

void GameScene::RunEquipmentLootCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogEquipCheck("EquipmentLootCheck", name, pass, failures);
    };
    if (m_player == nullptr) {
        check("player available", false);
        LOG_INFO("[EquipmentLootCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* slime = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Slime") {
            slime = monster;
            break;
        }
    }
    if (slime == nullptr) {
        check("alive slime available", false);
        LOG_INFO("[EquipmentLootCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    // Test Loot Override（仅测试模式；测后还原）
    const std::vector<legend::world::LootEntry> originalLoot =
        m_worldActors.GetSpawner().GetLootEntries(slime->GetMonsterTemplateId());
    std::vector<legend::world::LootEntry> guaranteed;
    legend::world::LootEntry entry;
    entry.itemId = "wooden_sword";
    entry.chance = 1.0f;
    entry.min = 1;
    entry.max = 1;
    guaranteed.push_back(entry);
    m_worldActors.GetSpawner().SetTestLootOverride(slime->GetMonsterTemplateId(), guaranteed);

    const std::size_t groundBefore = m_worldActors.GetLoot().GetCount();
    const int bagPotionsBefore = m_player->GetInventory().GetItemCount("wooden_sword");
    const auto savedPlayerPos = m_player->GetPosition();

    // 致死 + 消费奖励（真实 DeathEvent -> RewardSystem -> LootRoll -> GroundLoot）
    m_player->SetPosition(slime->GetPosition() + legend::math::Vector2(45.0f, 0.0f));
    legend::combat::DamageEvent lethal;
    lethal.sourceId = m_player->GetId();
    lethal.targetId = slime->GetId();
    lethal.rawDamage = 99999.0f;
    lethal.finalDamage = 99999.0f;
    check("lethal applied", m_worldActors.GetCombatSystem().ApplyDamage(lethal));
    check("reward processed", m_worldActors.ProcessDeathRewards() == 1);
    check("ground loot generated (+1)", m_worldActors.GetLoot().GetCount() == groundBefore + 1);

    // 拾取（真实 Pickup 链路）
    m_player->SetPosition(m_worldActors.GetLoot().GetAll()[groundBefore].position);
    const int picked = m_worldActors.GetLoot().PickupNearest(
        m_player->GetPosition(), 80.0f, m_player->GetInventory(),
        m_worldActors.GetItemDatabase());
    check("picked 1", picked == 1);
    const int bagPotionsAfter = m_player->GetInventory().GetItemCount("wooden_sword");
    check("inventory wooden_sword quantity 1", bagPotionsAfter == bagPotionsBefore + 1);
    // instance 语义：Equipment maxStack=1（每格一件）
    bool singleInstance = true;
    const legend::item::Inventory& bag = m_player->GetInventory();
    for (std::size_t i = 0; i < bag.GetCapacity(); ++i) {
        const legend::item::ItemInstance* slot = bag.GetSlot(i);
        if (slot != nullptr && slot->definitionId == "wooden_sword" && slot->quantity != 1) {
            singleInstance = false;
        }
    }
    check("equipment instances quantity == 1 (maxStack semantics)", singleInstance);
    check("ground loot consumed", m_worldActors.GetLoot().GetCount() == groundBefore);

    // 还原
    m_worldActors.GetSpawner().SetTestLootOverride(slime->GetMonsterTemplateId(), originalLoot);
    m_player->SetPosition(savedPlayerPos);
    LOG_INFO("[EquipmentLootCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [EquipmentComparisonCheck] ====================
// 只输出差值（不给 Better 结论——阶段7 指令三十五）。

void GameScene::RunEquipmentComparisonCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogEquipCheck("EquipmentComparisonCheck", name, pass, failures);
    };
    const auto database = MakeEquipmentTestDatabase();
    const legend::item::ItemDefinition* wooden = database.Get("wooden_sword");
    const legend::item::ItemDefinition* iron = database.Get("iron_sword");
    const legend::item::ItemDefinition* clothHelmet = database.Get("cloth_helmet");
    const legend::item::ItemDefinition* ironHelmet = database.Get("iron_helmet");

    // Iron Sword (+25) vs 当前 Wooden Sword (+12) -> attackDelta = +13
    const auto swordDelta = legend::item::EquipmentSystem::Compare(*iron, wooden);
    check("iron vs wooden attackDelta +13",
          std::fabs(swordDelta.attackDelta - 13.0f) < 0.001f);
    check("iron vs wooden defenseDelta 0", std::fabs(swordDelta.defenseDelta) < 0.001f);
    check("iron vs wooden maxHpDelta 0", std::fabs(swordDelta.maxHpDelta) < 0.001f);

    // 候选 Helmet vs 当前 Helmet：DEF/HP 差值正确
    const auto helmetDelta = legend::item::EquipmentSystem::Compare(*ironHelmet, clothHelmet);
    check("iron vs cloth helmet defenseDelta +7",
          std::fabs(helmetDelta.defenseDelta - 7.0f) < 0.001f);
    check("helmet maxHpDelta 0", std::fabs(helmetDelta.maxHpDelta) < 0.001f);

    // 空槽（current == nullptr）：delta == candidate 全量
    const auto vsEmpty = legend::item::EquipmentSystem::Compare(*wooden, nullptr);
    check("vs empty slot attackDelta +12", std::fabs(vsEmpty.attackDelta - 12.0f) < 0.001f);
    LOG_INFO("[EquipmentComparisonCheck] completed, failures = " + std::to_string(failures));
}
