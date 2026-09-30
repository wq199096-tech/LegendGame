#pragma once

#include "Shared/Dialogue/DialogueDefinition.h"
#include "Shared/Monster/MonsterSpawnDefinition.h"
#include "Shared/Npc/NpcDefinition.h"
#include "Shared/Portal/PortalDefinition.h"
#include "Shared/WorldMap/MapDefinition.h"
#include "Shared/WorldMap/MapVisualDefinition.h"

#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段22 指令 22.9~22.16：WorldData —— Data/World JSON 数据层（Shared：
// WorldServer / LegendMapEditor / Tests 三方共用，保证 Load/Validate/Save
// Roundtrip 一致）。
//
// 文件清单（每个文件 schemaVersion=1）：
//   world_manifest.json / maps.json / npcs.json /
//   monster_spawns.json / portals.json / visual_maps.json【阶段24】
//
// NPC 对话文本内嵌在 npcs.json（dialogueId = npcDefinitionId，阶段20 语义）。
// visual_maps.json【阶段24】：地图视觉定义（背景/四层），纯视觉数据，
// 服务器加载仅为校验一致性；渲染消费方为 Client 与 Editor。
// ---------------------------------------------------------------------------
inline constexpr int kWorldDataSchemaVersion = 1;

struct WorldDataManifest {
    int schemaVersion = kWorldDataSchemaVersion;
    std::vector<std::string> files; // 22.9：manifest 声明的数据文件
};

struct WorldDataSet {
    WorldDataManifest manifest;
    std::vector<MapDefinition> maps;
    std::vector<NpcDefinition> npcs;
    std::vector<DialogueDefinition> dialogues; // dialogueId = npc.npcDefinitionId
    std::vector<MonsterSpawnDefinition> monsterSpawns;
    std::vector<PortalDefinition> portals;
    std::vector<MapVisualDefinition> visualMaps; // 阶段24：地图视觉定义
};

// 加载目录下全部 6 个文件（任一缺失/JSON 非法/字段类型错 → false + error）。
// 不做跨文件校验——校验统一走 ValidateWorldData（22.12）。
bool LoadWorldData(const std::string& dir, WorldDataSet& out, std::string& error);

// 22.12 启动/编辑器共用的全量校验：schemaVersion、ID 唯一、引用存在、
// 地图边界、坐标越界、负数费用、非法等级/半径/count/respawn、Town 图存在。
bool ValidateWorldData(const WorldDataSet& data, std::string& error);

// 22.14 原子保存：serialize → temp 文件 → 重新 parse+Validate → replace；
// 22.15 保存前将旧文件轮换备份到 <dir>/.backup/（保留最近 10 个版本）。
bool SaveWorldData(const std::string& dir, const WorldDataSet& data, std::string& error);

// 22.18：阶段21 硬编码世界 → 初始 WorldDataSet（maps 1~3 / npcs 5001~5004 /
// spawns Map2 x20 + Map3 x10 / portals 8001~8004）。服务器默认数据与
// Data/World 初始 JSON 的唯一来源（单一事实）。
WorldDataSet MakeDefaultWorldData();

} // namespace legend::world
