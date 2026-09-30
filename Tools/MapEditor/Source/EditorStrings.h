#pragma once

#include <string>
#include <string_view>

namespace legend::editor::strings {

inline constexpr const char* AppTitle = "传奇游戏开发工具 - LegendGame Studio";
inline constexpr const char* ProjectName = "LegendGame";

namespace menu {
inline constexpr const char* File = "文件";
inline constexpr const char* Edit = "编辑";
inline constexpr const char* View = "视图";
inline constexpr const char* World = "世界";
inline constexpr const char* GameData = "游戏数据";
inline constexpr const char* Assets = "视觉资源";
inline constexpr const char* Chapter = "章节";
inline constexpr const char* Run = "运行";
inline constexpr const char* Help = "帮助";
} // namespace menu

namespace panel {
inline constexpr const char* Content = "内容列表";
inline constexpr const char* Inspector = "属性编辑";
inline constexpr const char* Validation = "内容检查";
inline constexpr const char* Console = "控制台";
inline constexpr const char* Process = "运行状态";
inline constexpr const char* Assets = "资源浏览器";
inline constexpr const char* Animation = "动画预览";
inline constexpr const char* QuestFlow = "任务流程";
} // namespace panel

namespace category {
inline constexpr const char* Maps = "地图";
inline constexpr const char* Npcs = "NPC";
inline constexpr const char* Spawns = "怪物刷新点";
inline constexpr const char* Bosses = "BOSS";
inline constexpr const char* Portals = "传送门";
inline constexpr const char* SafeZones = "安全区域";
inline constexpr const char* MapVisuals = "地图视觉";
inline constexpr const char* Items = "物品";
inline constexpr const char* Monsters = "怪物";
inline constexpr const char* Skills = "技能";
inline constexpr const char* Statuses = "状态效果";
inline constexpr const char* Quests = "任务";
inline constexpr const char* Shops = "商店";
inline constexpr const char* Teleports = "传送";
inline constexpr const char* LootTables = "掉落表";
inline constexpr const char* Chapters = "章节";
} // namespace category

namespace field {
inline constexpr const char* Name = "名称";
inline constexpr const char* Description = "描述";
inline constexpr const char* Level = "等级";
inline constexpr const char* Enabled = "启用";
inline constexpr const char* Position = "位置";
inline constexpr const char* Visual = "外观";
inline constexpr const char* MapId = "地图ID";
inline constexpr const char* MapName = "地图名称";
inline constexpr const char* MapType = "地图类型";
inline constexpr const char* Bounds = "地图边界";
inline constexpr const char* Spawn = "出生点";
inline constexpr const char* Respawn = "复活点";
inline constexpr const char* NpcId = "NPC ID";
inline constexpr const char* MonsterId = "怪物ID";
inline constexpr const char* BossId = "BOSS ID";
inline constexpr const char* PortalId = "传送门ID";
inline constexpr const char* QuestId = "任务ID";
inline constexpr const char* ItemId = "物品ID";
inline constexpr const char* SkillId = "技能ID";
inline constexpr const char* StatusId = "状态ID";
inline constexpr const char* ShopId = "商店ID";
inline constexpr const char* LootTableId = "掉落表ID";
} // namespace field

// 显示层中文名，不修改服务器 Definition，也不改变 JSON 数据兼容性。
inline std::string ContentName(std::string_view english) {
    struct Pair { std::string_view en; std::string_view zh; };
    static constexpr Pair names[] = {
        {"Greenfield Village", "翠野村"}, {"Slime Meadow", "史莱姆草地"},
        {"Ancient Ruins", "古代遗迹"}, {"Village Elder", "村庄长老"},
        {"General Merchant", "杂货商人"}, {"Wayfarer", "旅行者"},
        {"Explorer Guide", "探险向导"}, {"Training Slime", "训练史莱姆"},
        {"Ancient Slime Guardian", "远古史莱姆守卫"},
        {"Rusty Sword", "生锈的剑"}, {"Cloth Armor", "布甲"},
        {"Slime Core", "史莱姆核心"}, {"Bronze Sword", "青铜剑"},
        {"Apprentice Staff", "学徒法杖"}, {"Spirit Talisman", "灵符"},
        {"Traveler Armor", "旅行者护甲"}, {"Quick Strike", "迅捷斩"},
        {"Fire Bolt", "火焰弹"}, {"Whirlwind", "旋风斩"},
        {"Battle Focus", "战斗专注"}, {"Crippling Strike", "致残打击"},
        {"Armor Break", "破甲"}, {"Burn", "灼烧"}, {"Poison", "中毒"},
        {"Slow", "减速"}, {"First Trouble", "初现异常"},
        {"Strange Cores", "奇怪的核心"}, {"Growing Stronger", "成长之路"},
        {"Explore the Meadow", "探索草地"}, {"Deeper Threat", "更深的威胁"},
        {"Ruins Investigation", "遗迹调查"}, {"Far Plains", "远方平原"},
        {"Village Square", "村庄广场"}, {"Training Slime Loot", "训练史莱姆掉落"},
        {"Ancient Slime Guardian Loot", "远古守卫掉落"},
        {"The Restless Slimes", "第一章：异动的史莱姆"},
    };
    for (const auto& item : names) {
        if (item.en == english) return std::string(item.zh) + "  " + std::string(english);
    }
    return std::string(english);
}

inline std::string SearchAlias(std::string_view english) {
    const std::string display = ContentName(english);
    return display == english ? std::string{} : display;
}

} // namespace legend::editor::strings
