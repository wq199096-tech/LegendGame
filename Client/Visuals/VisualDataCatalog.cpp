#include "Client/Visuals/VisualDataCatalog.h"

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

#include <filesystem>
#include <fstream>
#include <set>

namespace legend::visual {

namespace {

namespace fs = std::filesystem;

using nlohmann::json;

bool ReadJsonFile(const std::string& filePath, json& out, std::string& error) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        error = filePath + ": file not found";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    out = json::parse(text, nullptr, false);
    if (out.is_discarded()) {
        error = filePath + ": invalid JSON syntax";
        return false;
    }
    if (!out.is_object()) {
        error = filePath + ": top-level must be an object";
        return false;
    }
    return true;
}

std::string ReadStringField(const json& obj, const char* key, const std::string& fallback) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        return fallback;
    }
    return it->get<std::string>();
}

} // namespace

std::string VisualDataCatalog::FindDataRoot() {
    // 候选：cwd/Data、exe 目录/Data、exe 目录上级/Data（构建树布局 Build/bin/Debug）。
    auto hasWorldManifest = [](const fs::path& dataDir) {
        std::error_code ec;
        return fs::exists(dataDir / "World" / "world_manifest.json", ec);
    };

    std::error_code ec;
    if (hasWorldManifest("Data")) {
        return (fs::path("Data")).string();
    }

    char exePathBuf[MAX_PATH] = {0};
    std::string exeDir;
#ifdef _WIN32
    const DWORD len = GetModuleFileNameA(nullptr, exePathBuf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        exeDir = fs::path(exePathBuf).parent_path().string();
    }
#endif
    if (!exeDir.empty()) {
        const fs::path candidate1 = fs::path(exeDir) / "Data";
        if (hasWorldManifest(candidate1)) {
            return candidate1.string();
        }
        const fs::path candidate2 = fs::path(exeDir).parent_path() / "Data";
        if (hasWorldManifest(candidate2)) {
            return candidate2.string();
        }
        const fs::path candidate3 = fs::path(exeDir).parent_path().parent_path() / "Data";
        if (hasWorldManifest(candidate3)) {
            return candidate3.string();
        }
    }
    if (hasWorldManifest("../Data")) {
        return std::string("../Data");
    }
    return std::string();
}

bool VisualDataCatalog::Load(const std::string& dataRoot, std::string& error) {
    const fs::path root(dataRoot);

    // ---- Data/Assets 视觉资产域（4 文件 + Resolve）----
    const std::string manifestPath = (root / "Assets" / "asset_manifest.json").string();
    const std::string animPath = (root / "Assets" / "animations.json").string();
    const std::string entitiesPath = (root / "Assets" / "visual_entities.json").string();
    const std::string effectsPath = (root / "Assets" / "effects.json").string();
    if (!LoadAssetManifest(manifestPath, m_manifest, error) ||
        !LoadAnimations(animPath, m_animations, error) ||
        !LoadVisualEntities(entitiesPath, m_entities, error) ||
        !LoadEffects(effectsPath, m_effects, error)) {
        return false;
    }
    if (!ValidateVisualData(m_manifest, m_animations, m_entities, m_effects, error)) {
        return false;
    }

    // ---- Data/Game 展示字段（skills/quests/monsters）----
    {
        json rootJson;
        if (!ReadJsonFile((root / "Game" / "skills.json").string(), rootJson, error)) {
            return false;
        }
        const auto skillsIt = rootJson.find("skills");
        if (skillsIt == rootJson.end() || !skillsIt->is_array()) { error = "skills.json: missing array 'skills'"; return false; }
        for (const auto& e : *skillsIt) {
            SkillDisplay display;
            display.skillId = e.value("skillId", 0u);
            display.name = ReadStringField(e, "name", "");
            display.manaCost = static_cast<std::uint32_t>(e.value("manaCost", 0u));
            display.cooldownSeconds =
                static_cast<float>(e.value("cooldownMs", 0u)) / 1000.0f;
            m_skills[display.skillId] = display;
        }
    }
    {
        json rootJson;
        if (!ReadJsonFile((root / "Game" / "quests.json").string(), rootJson, error)) {
            return false;
        }
        const auto questsIt = rootJson.find("quests");
        if (questsIt == rootJson.end() || !questsIt->is_array()) { error = "quests.json: missing array 'quests'"; return false; }
        for (const auto& e : *questsIt) {
            QuestDisplay display;
            display.questId = e.value("questId", 0u);
            display.name = ReadStringField(e, "name", "");
            m_quests[display.questId] = display;
        }
    }
    {
        json rootJson;
        if (!ReadJsonFile((root / "Game" / "monsters.json").string(), rootJson, error)) {
            return false;
        }
        const auto monstersIt = rootJson.find("monsters");
        if (monstersIt == rootJson.end() || !monstersIt->is_array()) { error = "monsters.json: missing array 'monsters'"; return false; }
        for (const auto& e : *monstersIt) {
            const auto id = static_cast<std::uint32_t>(e.value("monsterDefinitionId", 0u));
            m_monsterVisuals[id] = ReadStringField(e, "visualId", "");
        }
    }

    // ---- Data/World 展示/视觉字段（maps/portals/visual_maps）----
    {
        json rootJson;
        if (!ReadJsonFile((root / "World" / "maps.json").string(), rootJson, error)) {
            return false;
        }
        const auto mapsIt = rootJson.find("maps");
        if (mapsIt == rootJson.end() || !mapsIt->is_array()) { error = "maps.json: missing array 'maps'"; return false; }
        for (const auto& e : *mapsIt) {
            MapDisplay display;
            display.mapId = static_cast<std::uint16_t>(e.value("mapId", 0u));
            display.name = ReadStringField(e, "name", "");
            display.visualMapId = ReadStringField(e, "visualMapId", "");
            m_maps[display.mapId] = display;
        }
    }
    {
        json rootJson;
        if (!ReadJsonFile((root / "World" / "portals.json").string(), rootJson, error)) {
            return false;
        }
        const auto portalsIt = rootJson.find("portals");
        if (portalsIt == rootJson.end() || !portalsIt->is_array()) { error = "portals.json: missing array 'portals'"; return false; }
        for (const auto& e : *portalsIt) {
            const auto id = static_cast<std::uint32_t>(e.value("portalId", 0u));
            m_portalVisuals[id] = ReadStringField(e, "visualId", "");
        }
    }
    {
        json rootJson;
        if (!ReadJsonFile((root / "World" / "visual_maps.json").string(), rootJson, error)) {
            return false;
        }
        const auto it = rootJson.find("visualMaps");
        if (it == rootJson.end() || !it->is_array()) {
            error = "visual_maps.json: missing/invalid array 'visualMaps'";
            return false;
        }
        for (const auto& e : *it) {
            MapVisualDefinition visual;
            visual.visualMapId = ReadStringField(e, "visualMapId", "");
            visual.backgroundAsset = ReadStringField(e, "backgroundAsset", "");
            visual.tileSize = static_cast<float>(e.value("tileSize", 64.0));
            const auto layersIt = e.find("layers");
            if (layersIt != e.end() && layersIt->is_array()) {
                for (const auto& layerJson : *layersIt) {
                    MapVisualLayer layer;
                    layer.name = ReadStringField(layerJson, "name", "");
                    layer.assetId = ReadStringField(layerJson, "assetId", "");
                    const auto placesIt = layerJson.find("placements");
                    if (placesIt != layerJson.end() && placesIt->is_array()) {
                        for (const auto& p : *placesIt) {
                            MapVisualPlacement placement;
                            placement.assetId = ReadStringField(p, "assetId", "");
                            placement.x = static_cast<float>(p.value("x", 0.0));
                            placement.y = static_cast<float>(p.value("y", 0.0));
                            layer.placements.push_back(std::move(placement));
                        }
                    }
                    visual.layers.push_back(std::move(layer));
                }
            }
            m_mapVisuals.push_back(std::move(visual));
        }
    }
    return true;
}

const VisualEntityDef* VisualDataCatalog::FindEntity(const std::string& visualId) const {
    return FindVisualEntity(m_entities, visualId); // 全局函数（visual 命名空间）
}

const VisualEntityDef* VisualDataCatalog::FindNpcEntityByServerVisualId(
    int serverVisualId) const {
    for (const auto& entity : m_entities.entities) {
        if (entity.kind == "Npc" && entity.serverVisualId == serverVisualId) {
            return &entity;
        }
    }
    return nullptr;
}

const VisualEntityDef* VisualDataCatalog::FindPlayerEntityByClass(int classId) const {
    for (const auto& entity : m_entities.entities) {
        if (entity.kind == "Player" && entity.classId == classId) {
            return &entity;
        }
    }
    return nullptr;
}

const AnimationClipDef* VisualDataCatalog::FindClip(const std::string& animationId) const {
    return legend::visual::FindClip(m_animations, animationId);
}

const EffectDef* VisualDataCatalog::FindEffect(const std::string& effectId) const {
    return legend::visual::FindEffect(m_effects, effectId);
}

const MapVisualDefinition* VisualDataCatalog::FindMapVisual(
    const std::string& visualMapId) const {
    for (const auto& visual : m_mapVisuals) {
        if (visual.visualMapId == visualMapId) {
            return &visual;
        }
    }
    return nullptr;
}

const AssetManifestEntry* VisualDataCatalog::FindAsset(const std::string& assetId) const {
    return legend::visual::FindAsset(m_manifest, assetId);
}

const SkillDisplay* VisualDataCatalog::FindSkillDisplay(std::uint32_t skillId) const {
    const auto it = m_skills.find(skillId);
    return it == m_skills.end() ? nullptr : &it->second;
}

const QuestDisplay* VisualDataCatalog::FindQuestDisplay(std::uint32_t questId) const {
    const auto it = m_quests.find(questId);
    return it == m_quests.end() ? nullptr : &it->second;
}

std::string VisualDataCatalog::MonsterVisualId(std::uint32_t monsterTypeId) const {
    const auto it = m_monsterVisuals.find(monsterTypeId);
    return it == m_monsterVisuals.end() ? std::string() : it->second;
}

std::string VisualDataCatalog::PortalVisualId(std::uint32_t portalId) const {
    const auto it = m_portalVisuals.find(portalId);
    return it == m_portalVisuals.end() ? std::string() : it->second;
}

std::string VisualDataCatalog::MapDisplayName(std::uint16_t mapId) const {
    const auto it = m_maps.find(mapId);
    return it == m_maps.end() ? std::string("Unknown Map") : it->second.name;
}

const MapDisplay* VisualDataCatalog::FindMapDisplay(std::uint16_t mapId) const {
    const auto it = m_maps.find(mapId);
    return it == m_maps.end() ? nullptr : &it->second;
}

} // namespace legend::visual
