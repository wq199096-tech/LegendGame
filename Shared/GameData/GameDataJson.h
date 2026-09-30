#pragma once

#include "Shared/Item/ItemDefinition.h"
#include "Shared/Monster/LootTableDefinition.h"
#include "Shared/Monster/MonsterDefinition.h"
#include "Shared/Quest/QuestDefinition.h"
#include "Shared/Shop/ShopDefinition.h"
#include "Shared/Skill/SkillDefinition.h"
#include "Shared/Status/StatusEffectDefinition.h"
#include "Shared/Teleport/TeleportDefinition.h"
#include "Shared/WorldData/WorldDataJson.h"

#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段23 指令 23.2/23.20/23.21：GameData —— Data/Game JSON 数据层（Shared：
// WorldServer / LegendMapEditor / Tests 三方共用）。
//
// 文件清单（每个文件 schemaVersion=1；manifest 另含 contentVersion）：
//   game_manifest.json / items.json / monsters.json / skills.json /
//   statuses.json / quests.json / shops.json / teleports.json / loot_tables.json
//
// 交叉引用校验（23.11 核心）需要 World 数据（Quest 引用 NPC、Teleport 引用
// Map、Spawn 引用 Monster）——ValidateGameData(game, world)。
// ---------------------------------------------------------------------------
inline constexpr int kGameDataSchemaVersion = 1;
inline constexpr int kGameDataContentVersion = 1;

struct GameDataManifest {
    int schemaVersion = kGameDataSchemaVersion;
    int contentVersion = kGameDataContentVersion; // 23.20
    std::vector<std::string> files;
};

// 章节定义（阶段25：章节展示元数据——Chapter Complete 判定/编辑器 Chapter
// Editor/Quest Flow 用；服务器逻辑不消费，加载可选）。
struct ChapterDefinition {
    std::uint32_t chapterId = 0;
    std::string title;
    std::uint32_t finalQuestId = 0;      // 该任务 Completed = 章节完成
    std::vector<std::uint32_t> questIds; // 章节任务链（编辑器 Quest Flow 用）
};

struct GameDataSet {
    GameDataManifest manifest;
    std::vector<ItemDefinition> items;
    std::vector<MonsterDefinition> monsters;
    std::vector<SkillDefinition> skills;
    std::vector<StatusEffectDefinition> statuses;
    std::vector<QuestDefinition> quests;
    std::vector<ShopDefinition> shops;
    std::vector<TeleportDefinition> teleports;
    std::vector<LootTableDefinition> lootTables;
    std::vector<ChapterDefinition> chapters;
};

// 加载目录下全部 9 个文件（任一缺失/JSON 非法/字段类型错 → false + error）。
// chapters.json 为可选文件（存在则加载+校验；缺失 → 空 chapters，不报错）。
bool LoadGameData(const std::string& dir, GameDataSet& out, std::string& error);

// 23.11/23.14：全量校验（含交叉引用；world 参数提供 NPC/Map/Spawn 引用源）。
// crossReference=false 时跳过 NPC/Map/Spawn 交叉段（保存路径内部校验用——
// 完整交叉校验由调用方（WorldServer Start / Editor 全量 Validate）负责）。
bool ValidateGameData(const GameDataSet& game, const WorldDataSet& world, std::string& error,
                      bool crossReference = true);

// 23.14 原子保存 + 23.15 备份轮换（复用 WorldDataJson 的 WriteAtomic/BackupFile 语义）。
bool SaveGameData(const std::string& dir, const GameDataSet& data, std::string& error);

// 23.24 迁移源：阶段15~20 全部硬编码定义 → 初始 GameDataSet（单一事实）。
GameDataSet MakeDefaultGameData();

} // namespace legend::world
