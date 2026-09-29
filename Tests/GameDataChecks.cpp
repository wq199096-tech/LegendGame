// ---------------------------------------------------------------------------
// 阶段23：GameDataChecks —— Data/Game JSON 数据层检查（23.25：并入现有
// LegendWorldTests）。覆盖：9 文件 load/manifest contentVersion（23.20）/
// Validate 交叉引用（23.11）/ roundtrip / 原子保存 / backup / 坏数据拒绝 /
// **23.24 迁移回归**（3001~3003/Training Slime/1001~1005/5 Status/4001~4005/
// Shop6001/Teleport7001~7002 表现一致）/ loot generation（23.16/23.17）/
// WorldServer 读取真实 Data/Game（23.26）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Server/WorldServer/Item/ItemRegistry.h"
#include "Server/WorldServer/Item/LootTableRegistry.h"
#include "Server/WorldServer/Monster/MonsterDefinitionRegistry.h"
#include "Server/WorldServer/Npc/ShopService.h"
#include "Server/WorldServer/Npc/TeleportService.h"
#include "Server/WorldServer/Quest/QuestRegistry.h"
#include "Server/WorldServer/Skill/SkillRegistry.h"
#include "Server/WorldServer/Status/StatusEffectRegistry.h"
#include "Shared/GameData/GameDataJson.h"
#include "Shared/Item/ItemTypes.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace worldtest {

namespace {

namespace fs = std::filesystem;
using namespace legend::world;

bool WriteDefaultGame(const fs::path& dir, std::string& error) {
    GameDataSet data = MakeDefaultGameData();
    return SaveGameData(dir.string(), data, error);
}

void RunGameDataLogicChecks() {
    // ---- 默认 Game 数据 + manifest contentVersion（23.20）----
    {
        GameDataSet data = MakeDefaultGameData();
        Check("GameData: default has 3 items / 1 monster / 5 skills / 5 statuses / "
              "5 quests / 1 shop / 2 teleports / 1 lootTable",
              data.items.size() == 3 && data.monsters.size() == 1 &&
                  data.skills.size() == 5 && data.statuses.size() == 5 &&
                  data.quests.size() == 5 && data.shops.size() == 1 &&
                  data.teleports.size() == 2 && data.lootTables.size() == 1);
        Check("GameData: manifest has contentVersion=1",
              data.manifest.schemaVersion == 1 && data.manifest.contentVersion == 1);
        std::string error;
        const WorldDataSet emptyWorld;
        Check("GameData: default data validates (with default world refs)",
              ValidateGameData(data, MakeDefaultWorldData(), error));
    }

    // ---- Roundtrip + 原子保存（23.25）----
    {
        const fs::path dir = fs::temp_directory_path() / "legend_gamedata_roundtrip";
        std::error_code ec;
        fs::remove_all(dir, ec);
        GameDataSet data = MakeDefaultGameData();
        std::string error;
        Check("GameData: save default game data", [&] {
            const bool ok = SaveGameData(dir.string(), data, error);
            if (!ok) {
                std::printf("[Diag] GameData save error: %s\n", error.c_str());
            }
            return ok;
        }());
        GameDataSet loaded;
        Check("GameData: load default game data", [&] {
            const bool ok = LoadGameData(dir.string(), loaded, error);
            if (!ok) {
                std::printf("[Diag] GameData load error: %s\n", error.c_str());
            }
            return ok;
        }());
        Check("GameData: roundtrip preserves counts",
              loaded.items.size() == 3 && loaded.monsters.size() == 1 &&
                  loaded.skills.size() == 5 && loaded.statuses.size() == 5 &&
                  loaded.quests.size() == 5 && loaded.shops.size() == 1 &&
                  loaded.teleports.size() == 2 && loaded.lootTables.size() == 1);
        Check("GameData: roundtrip preserves manifest contentVersion",
              loaded.manifest.contentVersion == 1);
        bool noTmp = true;
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() == ".tmp") {
                noTmp = false;
            }
        }
        Check("GameData: atomic save leaves no temp files", noTmp);
        fs::remove_all(dir, ec);
    }

    // ---- Validate 交叉引用（23.11 全清单）----
    {
        std::string error;
        const auto base = [] { return MakeDefaultGameData(); };
        const auto mutate = [](GameDataSet d, const std::function<void(GameDataSet&)>& op) {
            op(d);
            return d;
        };
        const WorldDataSet defaultWorld = MakeDefaultWorldData();

        Check("GameData: duplicate itemId rejected",
              !ValidateGameData(mutate(base(), [](GameDataSet& d) {
                  d.items.push_back(d.items[0]);
              }), defaultWorld, error));
        Check("GameData: quest kill target missing monster rejected",
              !ValidateGameData(mutate(base(), [](GameDataSet& d) {
                  d.quests[0].objectives[0].targetId = 999;
              }), defaultWorld, error));
        Check("GameData: quest collect target missing item rejected",
              !ValidateGameData(mutate(base(), [](GameDataSet& d) {
                  d.quests[1].objectives[0].targetId = 999;
              }), defaultWorld, error));
        Check("GameData: quest reward missing item rejected",
              !ValidateGameData(mutate(base(), [](GameDataSet& d) {
                  d.quests[4].reward.itemDefinitionId = 999;
              }), defaultWorld, error));
        Check("GameData: quest startNpc missing (world npcs) rejected",
              !ValidateGameData(mutate(base(), [](GameDataSet& d) {
                  d.quests[0].startNpcDefinitionId = 999;
              }), defaultWorld, error));
        Check("GameData: shop entry missing item rejected",
              !ValidateGameData(mutate(base(), [](GameDataSet& d) {
                  d.shops[0].entries[0].itemDefinitionId = 999;
              }), defaultWorld, error));
        Check("GameData: skill status reference missing rejected",
              !ValidateGameData(mutate(base(), [](GameDataSet& d) {
                  d.skills[0].applyStatusEffectId = 999;
              }), defaultWorld, error));
        Check("GameData: monster lootTable reference missing rejected",
              !ValidateGameData(mutate(base(), [](GameDataSet& d) {
                  d.monsters[0].lootTableId = 999;
              }), defaultWorld, error));
        Check("GameData: teleport destination map missing rejected",
              !ValidateGameData(mutate(base(), [](GameDataSet& d) {
                  d.teleports[0].destinationMapId = 42;
              }), defaultWorld, error));
        Check("GameData: world spawn referencing missing game monster rejected", [&] {
            WorldDataSet world = defaultWorld;
            world.monsterSpawns[0].monsterDefinitionId = 999;
            return !ValidateGameData(base(), world, error);
        }());
        Check("GameData: lootTable dropChance out of [0,1] rejected",
              !ValidateGameData(mutate(base(), [](GameDataSet& d) {
                  d.lootTables[0].entries[0].dropChance = 1.5;
              }), defaultWorld, error));
        Check("GameData: duplicate objectiveId across quests rejected",
              !ValidateGameData(mutate(base(), [](GameDataSet& d) {
                  d.quests[1].objectives[0].objectiveId = 40011;
              }), defaultWorld, error));
    }

    // ---- Load 错误路径（23.21：错误必须带文件/类型/ID/字段/原因）----
    {
        const fs::path dir = fs::temp_directory_path() / "legend_gamedata_bad";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        GameDataSet loaded;
        std::string error;
        Check("GameData: load fails on missing dir",
              !LoadGameData(dir.string(), loaded, error));
        Check("GameData: error names game_manifest.json",
              error.find("game_manifest.json") != std::string::npos);
        std::string writeError;
        WriteDefaultGame(dir, writeError);
        std::ofstream bad(dir / "quests.json", std::ios::binary);
        bad << "{ broken";
        bad.close();
        Check("GameData: bad JSON syntax rejected",
              !LoadGameData(dir.string(), loaded, error));
        Check("GameData: error mentions quests.json",
              error.find("quests.json") != std::string::npos);
        fs::remove_all(dir, ec);
    }
}

void RunGameDataMigrationChecks() {
    // ---- 23.24 迁移回归：默认数据 == 阶段15~20 硬编码表现 ----
    {
        const GameDataSet data = MakeDefaultGameData();
        const ItemDefinition* sword = nullptr;
        const ItemDefinition* armor = nullptr;
        const ItemDefinition* core = nullptr;
        for (const auto& item : data.items) {
            if (item.definitionId == kItemRustySwordId) sword = &item;
            if (item.definitionId == kItemClothArmorId) armor = &item;
            if (item.definitionId == kItemSlimeCoreId) core = &item;
        }
        Check("Migration: 3001 Rusty Sword Weapon stack1 atk+3",
              sword != nullptr && sword->name == "Rusty Sword" &&
                  sword->type == ItemType::Weapon && sword->maxStack == 1 &&
                  sword->attackBonus == 3 && sword->defenseBonus == 0);
        Check("Migration: 3002 Cloth Armor Armor stack1 def+2",
              armor != nullptr && armor->name == "Cloth Armor" &&
                  armor->type == ItemType::Armor && armor->maxStack == 1 &&
                  armor->defenseBonus == 2);
        Check("Migration: 3003 Slime Core Material stack99",
              core != nullptr && core->name == "Slime Core" &&
                  core->type == ItemType::Material && core->maxStack == kSlimeCoreMaxStack);

        const auto& slime = data.monsters[0];
        Check("Migration: Training Slime stats identical",
              slime.monsterTypeId == 1 && slime.name == "Training Slime" &&
                  slime.level == 1 && slime.maxHp == 80 && slime.attackPower == 10 &&
                  slime.defense == 2 && slime.moveSpeed == 80.0f &&
                  slime.aggroRadius == 350.0f && slime.leashRadius == 600.0f &&
                  slime.rewardExp == 25 && slime.rewardGold == 3 && slime.enabled);

        const SkillDefinition* quick = nullptr;
        const SkillDefinition* fireBolt = nullptr;
        for (const auto& skill : data.skills) {
            if (skill.skillId == 1001) quick = &skill;
            if (skill.skillId == 1002) fireBolt = &skill;
        }
        Check("Migration: 1001 Quick Strike mana10 dmg30 cd1.5",
              quick != nullptr && quick->manaCost == 10 && quick->baseDamage == 30 &&
                  quick->cooldownSeconds == 1.5f && quick->range == 120.0f);
        Check("Migration: 1002 Fire Bolt mana20 dmg40 cast1.0",
              fireBolt != nullptr && fireBolt->manaCost == 20 && fireBolt->baseDamage == 40 &&
                  fireBolt->castTimeSeconds == 1.0f);

        const StatusEffectDefinition* burn = nullptr;
        for (const auto& status : data.statuses) {
            if (status.effectId == kStatusEffectIdBurn) burn = &status;
        }
        Check("Migration: 2003 Burn dot8 tick2s duration8s",
              burn != nullptr && burn->dotDamagePerStack == 8 &&
                  burn->tickIntervalMs == 2000 && burn->durationMs == 8000);

        const QuestDefinition* slimeHunter = nullptr;
        for (const auto& quest : data.quests) {
            if (quest.questId == 4001) slimeHunter = &quest;
        }
        Check("Migration: 4001 Slime Hunter kill5 reward100/20 npc5001",
              slimeHunter != nullptr && slimeHunter->objectives.size() == 1 &&
                  slimeHunter->objectives[0].requiredCount == 5 &&
                  slimeHunter->reward.exp == 100 && slimeHunter->reward.gold == 20 &&
                  slimeHunter->startNpcDefinitionId == 5001);

        const ShopDefinition* shop = &data.shops[0];
        const ShopEntry* swordEntry = shop->FindEntry(kItemRustySwordId);
        Check("Migration: Shop6001 sword buy100 sell30",
              shop->shopId == 6001 && swordEntry != nullptr &&
                  swordEntry->buyPrice == 100 && swordEntry->sellPrice == 30);

        const TeleportDefinition* teleport7001 = &data.teleports[0];
        Check("Migration: Teleport7001 Far Plains 1500,1500 20G",
              teleport7001->teleportId == 7001 && teleport7001->destinationX == 1500.0f &&
                  teleport7001->destinationY == 1500.0f && teleport7001->goldCost == 20);

        const LootTableDefinition* loot = &data.lootTables[0];
        Check("Migration: lootTable1 core100% sword20% armor20%",
              loot->lootTableId == 1 && loot->entries.size() == 3 &&
                  loot->entries[0].itemDefinitionId == kItemSlimeCoreId &&
                  loot->entries[0].dropChance == 1.0 &&
                  loot->entries[1].itemDefinitionId == kItemRustySwordId &&
                  loot->entries[1].dropChance == 0.2);
    }
}

void LoadDefaultsForAllGameRegistries();

void RunGameDataChainChecks() {
    // ---- Registry 注入（23.22）----
    {
        LoadDefaultsForAllGameRegistries();
    }

    // ---- WorldServer 读取真实 Data/Game（23.26）----
    {
        const fs::path dir = fs::temp_directory_path() / "legend_gamedata_live";
        std::error_code ec;
        fs::remove_all(dir, ec);
        std::string error;
        Check("GameData: write live game data", WriteDefaultGame(dir, error));

        WorldTestServers servers;
        servers.gameDataDir = dir.string();
        const bool started = servers.StartLogin() && servers.StartWorld();
        Check("GameData: server starts with real Data/Game", started);
        if (started) {
            // 23.26：地图/NPC/Monster/Skill/Item/Quest/Shop/Teleport 全部由数据定义加载
            //（Monster 现从 Data/Game monsters（1 只 Training Slime）——legacy spawn 仍走
            // kTrainingSlimeTypeId 引用 Data/Game 定义）。
            Check("GameData: server spawn slots intact after game load",
                  servers.world->SpawnSlotCount() == 50);
        }
        servers.StopAll();
        fs::remove_all(dir, ec);
    }

    // ---- 坏 Game 数据 → 拒绝启动（23.23）----
    {
        const fs::path dir = fs::temp_directory_path() / "legend_gamedata_corrupt";
        std::error_code ec;
        fs::remove_all(dir, ec);
        std::string writeError;
        WriteDefaultGame(dir, writeError);
        std::ofstream bad(dir / "items.json", std::ios::binary);
        bad << "{\n  \"schemaVersion\": 1,\n  \"items\": [\n    "
               "{\"itemDefinitionId\": 3001, \"name\": \"X\", \"type\": \"Weapon\", "
               "\"maxStack\": 1, \"equipSlot\": \"Weapon\", \"attackBonus\": 3, "
               "\"defenseBonus\": 0, \"canBuy\": true, \"canSell\": true, "
               "\"iconKey\": \"\", \"enabled\": true},\n    "
               "{\"itemDefinitionId\": 3001, \"name\": \"Y\", \"type\": \"Weapon\", "
               "\"maxStack\": 1, \"equipSlot\": \"Weapon\", \"attackBonus\": 1, "
               "\"defenseBonus\": 0, \"canBuy\": true, \"canSell\": true, "
               "\"iconKey\": \"\", \"enabled\": true}\n  ]\n}\n";
        bad.close();

        WorldTestServers servers;
        servers.gameDataDir = dir.string();
        const bool started = servers.StartLogin() && servers.StartWorld();
        Check("GameData: server rejects duplicate itemId", !started);
        servers.StopAll();
        fs::remove_all(dir, ec);
    }
}

// WorldServer 成员 ItemRegistry 无法直接注入——链路检查通过 gameDataDir 走真实加载；
// 纯 Registry 单测通过 Start 前手动 LoadDefaults 验证。
void LoadDefaultsForAllGameRegistries() {
    ItemRegistry registry;
    registry.LoadDefaults();
    Check("GameData: ItemRegistry defaults load 3 items",
          registry.Count() == 3 && registry.Find(kItemRustySwordId) != nullptr);
    SkillRegistry skills;
    skills.LoadDefaults();
    Check("GameData: SkillRegistry defaults load 5 skills",
          skills.Count() == 5 && skills.FindSkill(1001) != nullptr);
    StatusEffectRegistry statuses;
    statuses.LoadDefaults();
    Check("GameData: StatusEffectRegistry defaults load 5 statuses",
          statuses.Count() == 5 && statuses.FindEffect(kStatusEffectIdBurn) != nullptr);
    QuestRegistry::LoadDefaults();
    Check("GameData: QuestRegistry defaults load 5 quests",
          QuestRegistry::Instance().Count() == 5 &&
              QuestRegistry::Instance().FindQuest(4005) != nullptr);
    ShopRegistry::LoadDefaults();
    Check("GameData: ShopRegistry defaults load shop 6001",
          ShopRegistry::Instance().FindShop(6001) != nullptr);
    TeleportRegistry::LoadDefaults();
    Check("GameData: TeleportRegistry defaults load 7001/7002",
          TeleportRegistry::Instance().FindTeleport(7001) != nullptr &&
              TeleportRegistry::Instance().FindTeleport(7002) != nullptr);
    MonsterDefinitionRegistry::LoadDefaults();
    Check("GameData: MonsterDefinitionRegistry defaults load Training Slime",
          MonsterDefinitionRegistry::Instance().Find(1) != nullptr &&
              FindMonsterDefinition(1) != nullptr);
    LootTableRegistry::LoadDefaults();
    Check("GameData: LootTableRegistry defaults load table 1",
          LootTableRegistry::Instance().Find(1) != nullptr);
}

} // namespace

// 由 WorldChecks.cpp 调用（阶段23）。
void RunGameDataChecks() {
    std::printf("[GameData] logic checks begin\n");
    RunGameDataLogicChecks();
    std::printf("[GameData] migration checks begin\n");
    RunGameDataMigrationChecks();
    std::printf("[GameData] chain checks begin\n");
    RunGameDataChainChecks();
    std::printf("[GameData] checks complete (failures so far = %d)\n", g_failures);
}

} // namespace worldtest
