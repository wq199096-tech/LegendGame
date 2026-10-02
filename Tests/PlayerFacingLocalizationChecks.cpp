// ---------------------------------------------------------------------------
// Stage27 中文化专项：PlayerFacingLocalizationChecks —— 玩家可见文本简体中文
// 验收（并入 LegendWorldTests，不新增第 4 个测试 exe）。
//   1. PlayerFacingText 映射纯函数（物品类型/装备槽/任务目标动词/状态/地图/职业）
//   2. Data JSON 玩家可见 name/description/dialogue 必须含中文（maps/npcs/
//      dialogues/items/monsters/skills/statuses/teleports/shops/quests）
//   3. 内部 ID/键（mapId/iconKey/visualMapId 等稳定标识）不受中文化影响
// ---------------------------------------------------------------------------

#include "Tests/WorldTestHarness.h"

#include "Client/Ui/PlayerFacingText.h"
#include "Shared/GameData/GameDataJson.h"
#include "Shared/WorldData/WorldDataJson.h"

namespace worldtest {
namespace {

using namespace legend;

// ---- 1. 映射纯函数 ----
void RunPlayerFacingTextMappingChecks() {
    Check("ZhCnMappingChecks: item type names",
          std::string(ui::ItemTypeName("Weapon")) == "武器" &&
          std::string(ui::ItemTypeName("Armor")) == "护甲" &&
          std::string(ui::ItemTypeName("Material")) == "材料" &&
          std::string(ui::ItemTypeName("Consumable")) == "消耗品" &&
          std::string(ui::ItemTypeName("Quest")) == "任务" &&
          std::string(ui::ItemTypeName("None")) == "物品" &&
          std::string(ui::ItemTypeName("Bogus")) == "物品");
    Check("ZhCnMappingChecks: equip slot names",
          std::string(ui::EquipSlotName("Weapon")) == "武器" &&
          std::string(ui::EquipSlotName("Armor")) == "护甲" &&
          std::string(ui::EquipSlotName("None")) == "无");
    Check("ZhCnMappingChecks: quest objective verbs",
          std::string(ui::QuestObjectiveVerb("KillMonster")) == "击败" &&
          std::string(ui::QuestObjectiveVerb("CollectItem")) == "收集" &&
          std::string(ui::QuestObjectiveVerb("ReachLevel")) == "等级达到" &&
          std::string(ui::QuestObjectiveVerb("ReachArea")) == "前往" &&
          std::string(ui::QuestObjectiveVerb("Unknown")) == "进度");
    Check("ZhCnMappingChecks: status kind names",
          std::string(ui::StatusKindName("Buff")) == "增益" &&
          std::string(ui::StatusKindName("Debuff")) == "减益");
    Check("ZhCnMappingChecks: map type names",
          std::string(ui::MapTypeName("Town")) == "城镇" &&
          std::string(ui::MapTypeName("Field")) == "野外");
    Check("ZhCnMappingChecks: class names",
          std::string(ui::ClassName(1)) == "战士" &&
          std::string(ui::ClassName(2)) == "法师" &&
          std::string(ui::ClassName(3)) == "道士");
    Check("ZhCnMappingChecks: contains cjk helper",
          ui::ContainsCjk("绿野村") && !ui::ContainsCjk("Greenfield") &&
          !ui::ContainsCjk(""));
}

// ---- 2. Data JSON 中文名（加载真实 Data 目录） ----
void RunDataZhCnChecks() {
    // World Data（maps/npcs/dialogues）。
    {
        legend::world::WorldDataSet world;
        std::string error;
        Check("ZhCnDataChecks: load Data/World",
              legend::world::LoadWorldData(LEGEND_SOURCE_DIR "/Data/World", world, error));
        std::size_t total = 0;
        std::size_t cjk = 0;
        auto countName = [&total, &cjk](const std::string& name, const char* what) {
            ++total;
            if (ui::ContainsCjk(name)) {
                ++cjk;
            } else {
                LOG_WARN(std::string("[ZhCn] ") + what + " not chinese: '" + name + "'");
            }
        };
        for (const auto& map : world.maps) {
            countName(map.name, "map name");
        }
        for (const auto& npc : world.npcs) {
            countName(npc.name, "npc name");
        }
        for (const auto& dialogue : world.dialogues) {
            countName(dialogue.title, "dialogue title");
            countName(dialogue.text, "dialogue text");
        }
        Check("ZhCnDataChecks: world data player text all chinese",
              total >= 3 + 4 + 4 * 2 && cjk == total);
        // 内部稳定标识不受影响（抽查）。
        bool idsOk = false;
        for (const auto& map : world.maps) {
            if (map.mapId == 1) {
                idsOk = map.visualMapId == "vmap_greenfield";
            }
        }
        Check("ZhCnDataChecks: map ids/visualMapId unchanged", idsOk);
    }
    // Game Data（items/monsters/skills/statuses/shops/quests/teleports）。
    {
        legend::world::GameDataSet game;
        std::string error;
        Check("ZhCnDataChecks: load Data/Game",
              legend::world::LoadGameData(LEGEND_SOURCE_DIR "/Data/Game", game, error));
        std::size_t total = 0;
        std::size_t cjk = 0;
        auto countName = [&total, &cjk](const std::string& name, const char* what) {
            ++total;
            if (ui::ContainsCjk(name)) {
                ++cjk;
            } else {
                LOG_WARN(std::string("[ZhCn] ") + what + " not chinese: '" + name + "'");
            }
        };
        for (const auto& item : game.items) {
            countName(item.name, "item name");
        }
        for (const auto& monster : game.monsters) {
            countName(monster.name, "monster name");
        }
        for (const auto& skill : game.skills) {
            countName(skill.name, "skill name");
        }
        for (const auto& status : game.statuses) {
            countName(status.name, "status name");
        }
        for (const auto& shop : game.shops) {
            countName(shop.name, "shop name");
        }
        for (const auto& teleport : game.teleports) {
            countName(teleport.name, "teleport name");
        }
        for (const auto& quest : game.quests) {
            countName(quest.name, "quest name");
            countName(quest.description, "quest description");
        }
        // 任务 6 个 + 描述 6 个 + 物品 7 + 怪 2 + 技能 5 + 状态 5 + 商店 1 + 传送 2。
        Check("ZhCnDataChecks: game data player text all chinese",
              total >= 7 + 2 + 5 + 5 + 1 + 2 + 6 * 2 && cjk == total);
        // iconKey/id 不受影响（抽查）。
        bool iconOk = false;
        for (const auto& item : game.items) {
            if (item.definitionId == 3001) {
                iconOk = item.iconKey == "item_rusty_sword";
            }
        }
        bool guardianOk = false;
        for (const auto& monster : game.monsters) {
            if (monster.monsterTypeId == 2001) {
                guardianOk = monster.visualId == "ancient_guardian";
            }
        }
        Check("ZhCnDataChecks: item iconKeys/monster visualIds unchanged",
              iconOk && guardianOk);
    }
}

} // namespace（anonymous）

// Stage27 中文化专项入口（WorldChecks.cpp main 调度）。
int RunPlayerFacingLocalizationChecks() {
    RunPlayerFacingTextMappingChecks();
    RunDataZhCnChecks();
    return 0;
}

} // namespace worldtest
