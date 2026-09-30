#pragma once

#include "Client/Visuals/VisualAssetData.h"
#include "Shared/WorldMap/MapVisualDefinition.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace legend::visual {

// Shared 视觉定义引入本命名空间（visual_maps.json 结构体）。
using legend::world::MapVisualDefinition;
using legend::world::MapVisualLayer;
using legend::world::MapVisualPlacement;

// ---------------------------------------------------------------------------
// 阶段24：客户端展示层数据目录（Display-Only Catalog）。
//
// 纪律：Client 永不使用本地数据决定服务器权威结果——本目录只加载【视觉/展示】
// 所需字段（技能名/图标/CD 显示、任务标题、visualId 引用、地图视觉层），
// 全部游戏数值仍以服务器事件为准。
//
// 加载内容：
//   Data/Assets/*.json（4 文件，视觉资产域）
//   Data/Game/skills.json（name/manaCost/cooldownMs —— HUD 展示）
//   Data/Game/quests.json（questId/name —— Quest Tracker 标题）
//   Data/Game/monsters.json（monsterDefinitionId -> visualId）
//   Data/World/maps.json（mapId -> name/visualMapId）
//   Data/World/portals.json（portalId -> visualId）
//   Data/World/visual_maps.json（MapVisualDefinition）
// ---------------------------------------------------------------------------
struct SkillDisplay {
    std::uint32_t skillId = 0;
    std::string name;
    std::uint32_t manaCost = 0;
    float cooldownSeconds = 0.0f;
    std::string iconAsset; // HUD SkillBar 图标（visualRuntime 内置映射；此处预留）
};

struct QuestObjectiveDisplay {
    std::string type; // KillMonster / CollectItem / ReachLevel / ReachArea（展示用）
    std::uint32_t requiredCount = 0;
};

struct QuestDisplay {
    std::uint32_t questId = 0;
    std::string name;
    // 阶段25 指令二十一：Tracker 目标文案（展示字段；进度仍全部来自服务器事件）。
    std::vector<QuestObjectiveDisplay> objectives;
};

struct MapDisplay {
    std::uint16_t mapId = 0;
    std::string name;
    std::string visualMapId;
};

class VisualDataCatalog {
public:
    // 定位 Data 根目录（依次尝试 cwd、exe 目录、exe 上级目录的 Data/）。
    // 返回形如 "<root>/Data" 的目录；找不到返回空串。
    static std::string FindDataRoot();

    // dataRoot = 含 World/ Game/ 子目录的 Data 目录。任一文件缺失/非法 → false + error。
    // 视觉数据加载失败属于可恢复错误（fallback 视觉可用），调用方决定是否降级。
    bool Load(const std::string& dataRoot, std::string& error);

    const AssetManifest& Manifest() const { return m_manifest; }
    const AnimationSet& Animations() const { return m_animations; }
    const VisualEntitySet& Entities() const { return m_entities; }
    const EffectSet& Effects() const { return m_effects; }
    const std::vector<MapVisualDefinition>& MapVisuals() const { return m_mapVisuals; }

    const VisualEntityDef* FindEntity(const std::string& visualId) const;
    // NpcSpawn 协议数字 visualId -> 视觉实体。
    const VisualEntityDef* FindNpcEntityByServerVisualId(int serverVisualId) const;
    const AnimationClipDef* FindClip(const std::string& animationId) const;
    const EffectDef* FindEffect(const std::string& effectId) const;
    const MapVisualDefinition* FindMapVisual(const std::string& visualMapId) const;
    const AssetManifestEntry* FindAsset(const std::string& assetId) const;

    // Player classId -> 视觉实体（1/2/3 -> player_warrior/mage/taoist）。
    const VisualEntityDef* FindPlayerEntityByClass(int classId) const;

    // ---- 展示查询 ----
    const SkillDisplay* FindSkillDisplay(std::uint32_t skillId) const;
    const QuestDisplay* FindQuestDisplay(std::uint32_t questId) const;
    // MonsterDefinition monsterTypeId -> visualId（无记录返回 ""）。
    std::string MonsterVisualId(std::uint32_t monsterTypeId) const;
    // Portal portalId -> visualId（无记录返回 "" = portal_default）。
    std::string PortalVisualId(std::uint32_t portalId) const;
    // mapId -> 展示名（无记录返回 "未知地图"）。
    std::string MapDisplayName(std::uint16_t mapId) const;
    const MapDisplay* FindMapDisplay(std::uint16_t mapId) const;

private:
    AssetManifest m_manifest;
    AnimationSet m_animations;
    VisualEntitySet m_entities;
    EffectSet m_effects;
    std::vector<MapVisualDefinition> m_mapVisuals;

    std::map<std::uint32_t, SkillDisplay> m_skills;
    std::map<std::uint32_t, QuestDisplay> m_quests;
    std::map<std::uint32_t, std::string> m_monsterVisuals;
    std::map<std::uint32_t, std::string> m_portalVisuals;
    std::map<std::uint16_t, MapDisplay> m_maps;
};

} // namespace legend::visual
