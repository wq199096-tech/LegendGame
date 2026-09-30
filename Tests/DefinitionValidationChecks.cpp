// ---------------------------------------------------------------------------
// 阶段23：DefinitionValidationChecks —— 定义级校验 + GameDataDocument 模型检查
// （23.25：并入现有 LegendWorldTests；23.13/23.14/23.15 文档模型行为）。
// 覆盖：坏枚举字符串 / 负数 / 超范围 / 未知 schema 的 Load 拒绝（23.21 错误格式）/
// GameDataDocument Mutate·Undo·Redo / Duplicate 建议 ID / Search / Save 禁用 /
// Reload From Disk。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Tools/MapEditor/Source/GameDataDocument.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace legend::world;

namespace worldtest {

namespace {

namespace fs = std::filesystem;
using legend::editor::GameDataDocument;

bool WriteDefaultGame(const fs::path& dir, std::string& error) {
    return SaveGameData(dir.string(), MakeDefaultGameData(), error);
}

void RunDefinitionValidationLogicChecks() {
    // ---- 坏枚举字符串（23.25：坏枚举）----
    {
        const fs::path dir = fs::temp_directory_path() / "legend_defval_enum";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        std::string error;
        WriteDefaultGame(dir, error);
        std::ofstream bad(dir / "items.json", std::ios::binary);
        bad << "{\n  \"schemaVersion\": 1,\n  \"items\": [\n    "
               "{\"itemDefinitionId\": 3001, \"name\": \"X\", \"type\": \"Potion\", "
               "\"maxStack\": 1, \"equipSlot\": \"None\", \"attackBonus\": 0, "
               "\"defenseBonus\": 0, \"canBuy\": true, \"canSell\": true, "
               "\"iconKey\": \"\", \"enabled\": true}\n  ]\n}\n";
        bad.close();
        GameDataSet loaded;
        Check("DefValidation: unknown item type string rejected",
              !LoadGameData(dir.string(), loaded, error));
        Check("DefValidation: error mentions items.json",
              error.find("items.json") != std::string::npos);
        fs::remove_all(dir, ec);
    }

    // ---- 负数攻击力（23.25：负数）----
    {
        const fs::path dir = fs::temp_directory_path() / "legend_defval_negative";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        std::string error;
        WriteDefaultGame(dir, error);
        std::ofstream bad(dir / "items.json", std::ios::binary);
        bad << "{\n  \"schemaVersion\": 1,\n  \"items\": [\n    "
               "{\"itemDefinitionId\": 3001, \"name\": \"X\", \"type\": \"Weapon\", "
               "\"maxStack\": 1, \"equipSlot\": \"Weapon\", \"attackBonus\": -3, "
               "\"defenseBonus\": 0, \"canBuy\": true, \"canSell\": true, "
               "\"iconKey\": \"\", \"enabled\": true}\n  ]\n}\n";
        bad.close();
        GameDataSet loaded;
        Check("DefValidation: negative attackBonus rejected (integer type guard)",
              !LoadGameData(dir.string(), loaded, error));
        fs::remove_all(dir, ec);
    }

    // ---- 超范围（23.25：超范围——manaCost > 999 / maxTargets > 16）----
    {
        const GameDataSet base = MakeDefaultGameData();
        GameDataSet data = base;
        data.skills[0].manaCost = 5000;
        std::string error;
        Check("DefValidation: manaCost > 999 rejected",
              !ValidateGameData(data, MakeDefaultWorldData(), error));
        data = base;
        data.skills[0].maxTargets = 99;
        Check("DefValidation: maxTargets > 16 rejected",
              !ValidateGameData(data, MakeDefaultWorldData(), error));
        data = base;
        data.statuses[0].maxStacks = 500;
        Check("DefValidation: status maxStacks > 99 rejected",
              !ValidateGameData(data, MakeDefaultWorldData(), error));
    }
}

void RunGameDataDocumentChecks() {
    // ---- 默认加载 / Mutate / Undo / Redo ----
    {
        GameDataDocument doc;
        doc.NewFromDefaults();
        Check("DefValidation: document defaults validate clean", !doc.HasErrors());
        doc.Mutate([](GameDataSet& d) { d.items[0].name = "Renamed"; });
        Check("DefValidation: mutate applies", doc.Data().items[0].name == "Renamed");
        doc.Undo();
        Check("DefValidation: undo restores", doc.Data().items[0].name == "Rusty Sword");
        doc.Redo();
        Check("DefValidation: redo reapplies", doc.Data().items[0].name == "Renamed");
    }

    // ---- 23.13 Duplicate 建议 ID（不产生重复）----
    {
        GameDataDocument doc;
        doc.NewFromDefaults();
        doc.SetSelection(GameDataDocument::ObjectType::Item, 3001);
        // 阶段25：默认 7 物品（3010~3013 新装备）-> duplicate 后 8；
        // SuggestItemId = max(3013)+1 = 3014（23.13 max+1 语义）。
        Check("DefValidation: duplicate item suggests 3014",
              doc.DuplicateSelected() && doc.FindItem(3014) != nullptr &&
                  doc.Data().items.size() == 8);
        doc.SetSelection(GameDataDocument::ObjectType::LootTable, 1);
        // 阶段25：默认含 boss 掉落表 2001 -> duplicate 表 1 建议 max+1 = 2002。
        Check("DefValidation: duplicate lootTable suggests 2002",
              doc.DuplicateSelected() && doc.FindLootTable(2002) != nullptr);
        Check("DefValidation: duplicates keep data valid", !doc.HasErrors());
    }

    // ---- 23.12 Search ----
    {
        GameDataDocument doc;
        doc.NewFromDefaults();
        Check("DefValidation: search by id matches",
              doc.MatchesSearch(GameDataDocument::ObjectType::Item, 3001, "Rusty Sword", "3001"));
        Check("DefValidation: search by name matches",
              doc.MatchesSearch(GameDataDocument::ObjectType::Item, 3001, "Rusty Sword", "rusty"));
        Check("DefValidation: search miss",
              !doc.MatchesSearch(GameDataDocument::ObjectType::Item, 3002, "Cloth Armor", "sword"));
    }

    // ---- Save 禁用（Error 时）/ Roundtrip / Reload（23.15）----
    {
        GameDataDocument doc;
        doc.NewFromDefaults();
        const fs::path dir = fs::temp_directory_path() / "legend_defval_doc";
        std::error_code ec;
        fs::remove_all(dir, ec);
        doc.SetDirectory(dir.string());
        std::string error;
        Check("DefValidation: clean save ok", doc.Save(error));

        doc.Mutate([](GameDataSet& d) { d.monsters[0].lootTableId = 999; });
        Check("DefValidation: broken loot ref flagged", doc.HasErrors());
        Check("DefValidation: save blocked on error", !doc.Save(error));
        doc.Undo();
        Check("DefValidation: undo clears error", !doc.HasErrors());

        doc.Mutate([](GameDataSet& d) { d.skills[0].manaCost = 77; });
        Check("DefValidation: save with edit", doc.Save(error));

        GameDataDocument reloaded;
        Check("DefValidation: reload roundtrip (23.28 semantics)",
              reloaded.Load(dir.string(), (fs::temp_directory_path() / "legend_defval_world")
                                               .string(),
                            error) ||
                  reloaded.Load(dir.string(),
                                std::string("Data/World"), error));
        // Skill 1001 mana 77（无 C++ 修改的数值变更——23.28）。
        bool manaPersisted = false;
        for (const auto& skill : reloaded.Data().skills) {
            if (skill.skillId == 1001 && skill.manaCost == 77) {
                manaPersisted = true;
            }
        }
        Check("DefValidation: edited manaCost persisted (edit skill w/o C++)", manaPersisted);

        // 23.15 Reload From Disk。
        reloaded.Mutate([](GameDataSet& d) { d.items[0].name = "Dirty Edit"; });
        Check("DefValidation: reload from disk discards dirty edits",
              reloaded.ReloadFromDisk(error) && reloaded.Data().items[0].name == "Rusty Sword" &&
                  !reloaded.IsDirty());
        fs::remove_all(dir, ec);
    }
}

} // namespace

// 由 WorldChecks.cpp 调用（阶段23）。
void RunDefinitionValidationChecks() {
    std::printf("[DefValidation] logic checks begin\n");
    RunDefinitionValidationLogicChecks();
    std::printf("[DefValidation] document checks begin\n");
    RunGameDataDocumentChecks();
    std::printf("[DefValidation] checks complete (failures so far = %d)\n", g_failures);
}

} // namespace worldtest
