#include "Shared/WorldData/WorldDataJson.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace legend::world {

namespace {

using nlohmann::json;

namespace fs = std::filesystem;

// ---- 字段严格读取 helper（22.21：错误必须带 文件/类型/ID/字段/原因）----

bool ReadInt(const json& obj, const char* key, std::int64_t& out, const std::string& ctx,
             std::string& error) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_integer()) {
        error = ctx + ": missing/invalid integer field '" + key + "'";
        return false;
    }
    out = it->get<std::int64_t>();
    return true;
}

bool ReadUint(const json& obj, const char* key, std::uint64_t& out, const std::string& ctx,
              std::string& error) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_unsigned()) {
        error = ctx + ": missing/invalid unsigned field '" + key + "'";
        return false;
    }
    out = it->get<std::uint64_t>();
    return true;
}

bool ReadFloat(const json& obj, const char* key, float& out, const std::string& ctx,
               std::string& error) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number()) {
        error = ctx + ": missing/invalid number field '" + key + "'";
        return false;
    }
    out = static_cast<float>(it->get<double>());
    return true;
}

bool ReadBool(const json& obj, const char* key, bool& out, const std::string& ctx,
              std::string& error) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_boolean()) {
        error = ctx + ": missing/invalid boolean field '" + key + "'";
        return false;
    }
    out = it->get<bool>();
    return true;
}

bool ReadString(const json& obj, const char* key, std::string& out, const std::string& ctx,
                std::string& error) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        error = ctx + ": missing/invalid string field '" + key + "'";
        return false;
    }
    out = it->get<std::string>();
    return true;
}

bool ReadUintArray(const json& obj, const char* key, std::vector<std::uint32_t>& out,
                   const std::string& ctx, std::string& error) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_array()) {
        error = ctx + ": missing/invalid array field '" + key + "'";
        return false;
    }
    out.clear();
    for (const auto& e : *it) {
        if (!e.is_number_unsigned()) {
            error = ctx + ": non-unsigned element in '" + key + "'";
            return false;
        }
        out.push_back(e.get<std::uint32_t>());
    }
    return true;
}

// ---- 枚举字符串映射（编辑器 ComboBox 与 JSON 一致）----

bool MapTypeFromString(const std::string& text, MapType& out) {
    if (text == "Town") { out = MapType::Town; return true; }
    if (text == "Field") { out = MapType::Field; return true; }
    if (text == "Dungeon") { out = MapType::Dungeon; return true; }
    return false;
}

bool NpcTypeFromString(const std::string& text, NpcType& out) {
    if (text == "QuestGiver") { out = NpcType::QuestGiver; return true; }
    if (text == "Merchant") { out = NpcType::Merchant; return true; }
    if (text == "Teleporter") { out = NpcType::Teleporter; return true; }
    if (text == "MultiFunction") { out = NpcType::MultiFunction; return true; }
    return false;
}

std::string MapTypeToString(MapType type) { return MapTypeName(type); }

std::string NpcTypeToString(NpcType type) {
    // 局部映射（NpcTypes.h 的 NpcTypeName 声明无 Shared 实现——避免链接依赖）。
    switch (type) {
        case NpcType::QuestGiver: return "QuestGiver";
        case NpcType::Merchant: return "Merchant";
        case NpcType::Teleporter: return "Teleporter";
        case NpcType::MultiFunction: return "MultiFunction";
    }
    return "Unknown";
}

// ---- 读取单文件 ----

bool ReadJsonFile(const std::string& dir, const char* name, json& out, std::string& error) {
    const fs::path path = fs::path(dir) / name;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = std::string(name) + ": file not found in '" + dir + "'";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    out = json::parse(text, nullptr, false);
    if (out.is_discarded()) {
        error = std::string(name) + ": invalid JSON syntax";
        return false;
    }
    if (!out.is_object()) {
        error = std::string(name) + ": top-level must be an object";
        return false;
    }
    return true;
}

bool ReadSchemaVersion(const json& obj, const char* name, std::string& error) {
    const auto it = obj.find("schemaVersion");
    if (it == obj.end() || !it->is_number_integer() || it->get<int>() != kWorldDataSchemaVersion) {
        error = std::string(name) + ": unsupported schemaVersion (expected " +
                std::to_string(kWorldDataSchemaVersion) + ")";
        return false;
    }
    return true;
}

bool ParseMaps(const json& root, WorldDataSet& out, std::string& error) {
    const auto it = root.find("maps");
    if (it == root.end() || !it->is_array()) {
        error = "maps.json: missing/invalid array 'maps'";
        return false;
    }
    for (const auto& e : *it) {
        if (!e.is_object()) {
            error = "maps.json: non-object element in 'maps'";
            return false;
        }
        MapDefinition map;
        std::uint64_t mapId = 0;
        std::string typeText;
        if (!ReadUint(e, "mapId", mapId, "maps.json: Map", error) ||
            !ReadString(e, "name", map.name, "maps.json: Map", error) ||
            !ReadString(e, "type", typeText, "maps.json: Map " + std::to_string(mapId), error) ||
            !ReadFloat(e, "minX", map.minX, "maps.json: Map " + std::to_string(mapId), error) ||
            !ReadFloat(e, "minY", map.minY, "maps.json: Map " + std::to_string(mapId), error) ||
            !ReadFloat(e, "maxX", map.maxX, "maps.json: Map " + std::to_string(mapId), error) ||
            !ReadFloat(e, "maxY", map.maxY, "maps.json: Map " + std::to_string(mapId), error) ||
            !ReadFloat(e, "spawnX", map.spawnX, "maps.json: Map " + std::to_string(mapId), error) ||
            !ReadFloat(e, "spawnY", map.spawnY, "maps.json: Map " + std::to_string(mapId), error) ||
            !ReadFloat(e, "respawnX", map.respawnX,
                       "maps.json: Map " + std::to_string(mapId), error) ||
            !ReadFloat(e, "respawnY", map.respawnY,
                       "maps.json: Map " + std::to_string(mapId), error)) {
            return false;
        }
        if (!MapTypeFromString(typeText, map.type)) {
            error = "maps.json: Map " + std::to_string(mapId) + ": unknown type '" + typeText + "'";
            return false;
        }
        map.mapId = static_cast<std::uint16_t>(mapId);
        out.maps.push_back(map);
    }
    return true;
}

bool ParseNpcs(const json& root, WorldDataSet& out, std::string& error) {
    const auto it = root.find("npcs");
    if (it == root.end() || !it->is_array()) {
        error = "npcs.json: missing/invalid array 'npcs'";
        return false;
    }
    for (const auto& e : *it) {
        if (!e.is_object()) {
            error = "npcs.json: non-object element in 'npcs'";
            return false;
        }
        NpcDefinition npc;
        std::string typeText;
        {
            std::uint64_t npcId = 0;
            if (!ReadUint(e, "npcDefinitionId", npcId, "npcs.json: Npc", error)) {
                return false;
            }
            if (npcId == 0 || npcId > 4294967295ull) {
                error = std::string("npcs.json: Npc ") + std::to_string(npcId) +
                        ": npcDefinitionId out of range";
                return false;
            }
            npc.npcDefinitionId = static_cast<std::uint32_t>(npcId);
        }
        const std::string ctx = "npcs.json: Npc " + std::to_string(npc.npcDefinitionId);
        bool enabled = true;
        std::vector<std::uint32_t> questIds;
        if (!ReadString(e, "name", npc.name, ctx, error) ||
            !ReadString(e, "type", typeText, ctx, error) ||
            !ReadFloat(e, "x", npc.spawnX, ctx, error) ||
            !ReadFloat(e, "y", npc.spawnY, ctx, error) ||
            !ReadFloat(e, "interactionRange", npc.interactionRange, ctx, error)) {
            return false;
        }
        // mapId 是 uint16——读 uint64 再窄化（带范围检查）。
        {
            std::uint64_t mapId64 = 0;
            if (!ReadUint(e, "mapId", mapId64, ctx, error)) {
                return false;
            }
            if (mapId64 == 0 || mapId64 > 65535) {
                error = ctx + ": mapId out of range";
                return false;
            }
            npc.mapId = static_cast<std::uint16_t>(mapId64);
        }
        std::uint64_t dialogueId = 0;
        std::uint64_t shopId = 0;
        std::uint64_t teleportId = 0;
        std::uint64_t visualId = 0;
        std::string dialogueTitle;
        std::string dialogueText;
        if (!ReadUint(e, "dialogueId", dialogueId, ctx, error) ||
            !ReadUint(e, "shopId", shopId, ctx, error) ||
            !ReadUint(e, "teleportId", teleportId, ctx, error) ||
            !ReadUintArray(e, "questIds", questIds, ctx, error) ||
            !ReadUint(e, "visualId", visualId, ctx, error) ||
            !ReadBool(e, "enabled", enabled, ctx, error)) {
            return false;
        }
        // dialogueTitle/dialogueText：dialogueId != 0 时必填（聚合 DialogueDefinition）。
        if (dialogueId != 0) {
            if (!ReadString(e, "dialogueTitle", dialogueTitle, ctx, error) ||
                !ReadString(e, "dialogueText", dialogueText, ctx, error)) {
                return false;
            }
        }
        if (!NpcTypeFromString(typeText, npc.npcType)) {
            error = ctx + ": unknown type '" + typeText + "'";
            return false;
        }
        npc.dialogueId = static_cast<std::uint32_t>(dialogueId);
        npc.shopId = static_cast<std::uint32_t>(shopId);
        npc.teleportId = static_cast<std::uint32_t>(teleportId);
        npc.visualId = static_cast<std::uint32_t>(visualId);
        npc.questIds = questIds;
        npc.enabled = enabled;
        out.npcs.push_back(npc);
        if (dialogueId != 0) {
            out.dialogues.push_back({static_cast<std::uint32_t>(dialogueId),
                                     std::move(dialogueTitle), std::move(dialogueText)});
        }
    }
    return true;
}

bool ParseSpawns(const json& root, WorldDataSet& out, std::string& error) {
    const auto it = root.find("monsterSpawns");
    if (it == root.end() || !it->is_array()) {
        error = "monster_spawns.json: missing/invalid array 'monsterSpawns'";
        return false;
    }
    for (const auto& e : *it) {
        if (!e.is_object()) {
            error = "monster_spawns.json: non-object element in 'monsterSpawns'";
            return false;
        }
        MonsterSpawnDefinition spawn;
        std::uint64_t spawnId = 0;
        {
            std::uint64_t idTmp = 0;
            if (!ReadUint(e, "spawnId", idTmp, "monster_spawns.json: MonsterSpawn", error)) {
                return false;
            }
            spawnId = idTmp;
        }
        const std::string ctx = "monster_spawns.json: MonsterSpawn " + std::to_string(spawnId);
        std::uint64_t mapId64 = 0;
        std::uint64_t monsterId = 0;
        std::uint64_t count = 0;
        std::uint64_t respawnSeconds = 0;
        bool enabled = true;
        if (!ReadUint(e, "mapId", mapId64, ctx, error) ||
            !ReadUint(e, "monsterDefinitionId", monsterId, ctx, error) ||
            !ReadFloat(e, "centerX", spawn.centerX, ctx, error) ||
            !ReadFloat(e, "centerY", spawn.centerY, ctx, error) ||
            !ReadFloat(e, "radius", spawn.radius, ctx, error) ||
            !ReadUint(e, "count", count, ctx, error) ||
            !ReadUint(e, "respawnSeconds", respawnSeconds, ctx, error) ||
            !ReadBool(e, "enabled", enabled, ctx, error)) {
            return false;
        }
        if (mapId64 == 0 || mapId64 > 65535) {
            error = ctx + ": mapId out of range";
            return false;
        }
        spawn.spawnId = static_cast<std::uint32_t>(spawnId);
        spawn.mapId = static_cast<std::uint16_t>(mapId64);
        spawn.monsterDefinitionId = static_cast<std::uint32_t>(monsterId);
        spawn.count = static_cast<std::uint32_t>(count);
        spawn.respawnSeconds = static_cast<std::uint32_t>(respawnSeconds);
        spawn.enabled = enabled;
        out.monsterSpawns.push_back(spawn);
    }
    return true;
}

bool ParsePortals(const json& root, WorldDataSet& out, std::string& error) {
    const auto it = root.find("portals");
    if (it == root.end() || !it->is_array()) {
        error = "portals.json: missing/invalid array 'portals'";
        return false;
    }
    for (const auto& e : *it) {
        if (!e.is_object()) {
            error = "portals.json: non-object element in 'portals'";
            return false;
        }
        PortalDefinition portal;
        std::uint64_t portalId = 0;
        {
            std::uint64_t idTmp = 0;
            if (!ReadUint(e, "portalId", idTmp, "portals.json: Portal", error)) {
                return false;
            }
            portalId = idTmp;
        }
        const std::string ctx = "portals.json: Portal " + std::to_string(portalId);
        std::uint64_t sourceMapId = 0;
        std::uint64_t destinationMapId = 0;
        std::uint64_t minLevel = 0;
        std::int64_t goldCost = 0;
        bool enabled = true;
        if (!ReadUint(e, "sourceMapId", sourceMapId, ctx, error) ||
            !ReadFloat(e, "x", portal.x, ctx, error) ||
            !ReadFloat(e, "y", portal.y, ctx, error) ||
            !ReadFloat(e, "interactionRadius", portal.interactionRadius, ctx, error) ||
            !ReadUint(e, "destinationMapId", destinationMapId, ctx, error) ||
            !ReadFloat(e, "destinationX", portal.destinationX, ctx, error) ||
            !ReadFloat(e, "destinationY", portal.destinationY, ctx, error) ||
            !ReadUint(e, "minLevel", minLevel, ctx, error) ||
            !ReadInt(e, "goldCost", goldCost, ctx, error) ||
            !ReadBool(e, "enabled", enabled, ctx, error)) {
            return false;
        }
        if (sourceMapId == 0 || sourceMapId > 65535 || destinationMapId == 0 ||
            destinationMapId > 65535) {
            error = ctx + ": mapId out of range";
            return false;
        }
        portal.portalId = static_cast<std::uint32_t>(portalId);
        portal.sourceMapId = static_cast<std::uint16_t>(sourceMapId);
        portal.destinationMapId = static_cast<std::uint16_t>(destinationMapId);
        portal.minLevel = static_cast<std::uint32_t>(minLevel);
        portal.goldCost = static_cast<std::uint32_t>(goldCost);
        portal.enabled = enabled;
        out.portals.push_back(portal);
    }
    return true;
}

// ---- 序列化 ----

json MapToJson(const MapDefinition& map) {
    return json{
        {"mapId", map.mapId},
        {"name", map.name},
        {"type", MapTypeToString(map.type)},
        {"minX", map.minX},   {"minY", map.minY},
        {"maxX", map.maxX},   {"maxY", map.maxY},
        {"spawnX", map.spawnX}, {"spawnY", map.spawnY},
        {"respawnX", map.respawnX}, {"respawnY", map.respawnY},
    };
}

json NpcToJson(const NpcDefinition& npc, const DialogueDefinition* dialogue) {
    json j{
        {"npcDefinitionId", npc.npcDefinitionId},
        {"name", npc.name},
        {"type", NpcTypeToString(npc.npcType)},
        {"mapId", npc.mapId},
        {"x", npc.spawnX},
        {"y", npc.spawnY},
        {"interactionRange", npc.interactionRange},
        {"dialogueId", npc.dialogueId},
        {"shopId", npc.shopId},
        {"teleportId", npc.teleportId},
        {"questIds", npc.questIds},
        {"visualId", npc.visualId},
        {"enabled", npc.enabled},
    };
    if (dialogue != nullptr) {
        j["dialogueTitle"] = dialogue->title;
        j["dialogueText"] = dialogue->text;
    }
    return j;
}

json SpawnToJson(const MonsterSpawnDefinition& spawn) {
    return json{
        {"spawnId", spawn.spawnId},
        {"mapId", spawn.mapId},
        {"monsterDefinitionId", spawn.monsterDefinitionId},
        {"centerX", spawn.centerX},
        {"centerY", spawn.centerY},
        {"radius", spawn.radius},
        {"count", spawn.count},
        {"respawnSeconds", spawn.respawnSeconds},
        {"enabled", spawn.enabled},
    };
}

json PortalToJson(const PortalDefinition& portal) {
    return json{
        {"portalId", portal.portalId},
        {"sourceMapId", portal.sourceMapId},
        {"x", portal.x},
        {"y", portal.y},
        {"interactionRadius", portal.interactionRadius},
        {"destinationMapId", portal.destinationMapId},
        {"destinationX", portal.destinationX},
        {"destinationY", portal.destinationY},
        {"minLevel", portal.minLevel},
        {"goldCost", portal.goldCost},
        {"enabled", portal.enabled},
    };
}

// 22.15：保存前轮换备份（<dir>/.backup/<stem>.<1..10>.json，保留最近 10 个版本）。
void BackupFile(const std::string& dir, const char* name) {
    std::error_code ec;
    const fs::path src = fs::path(dir) / name;
    if (!fs::exists(src, ec)) {
        return;
    }
    const fs::path backupDir = fs::path(dir) / ".backup";
    fs::create_directories(backupDir, ec);
    if (ec) {
        return; // 备份失败不阻塞保存（WriteAtomic 才是数据安全线）
    }
    std::set<int> used;
    for (const auto& entry : fs::directory_iterator(backupDir, ec)) {
        const std::string fileName = entry.path().filename().string();
        const std::string stem = fs::path(name).stem().string();
        const std::string prefix = stem + ".";
        if (fileName.rfind(prefix, 0) != 0) {
            continue;
        }
        const std::string tail = fileName.substr(prefix.size());
        const auto dot = tail.find('.');
        if (dot == std::string::npos) {
            continue;
        }
        try {
            used.insert(std::stoi(tail.substr(0, dot)));
        } catch (...) {
            // 非法备份名忽略
        }
    }
    int slot = 1;
    for (; slot <= 10; ++slot) {
        if (used.count(slot) == 0) {
            break;
        }
    }
    if (slot > 10) {
        slot = 1; // 满 10 份：覆盖最旧槽位
    }
    const std::string backupName =
        fs::path(name).stem().string() + "." + std::to_string(slot) + ".json";
    fs::copy_file(src, backupDir / backupName, fs::copy_options::overwrite_existing, ec);
}

// 22.14：serialize → temp → reparse 验证 → rename replace。
bool WriteAtomic(const std::string& dir, const char* name, const json& value,
                 std::string& error) {
    const std::string text = value.dump(2) + "\n";
    json check = json::parse(text, nullptr, false);
    if (check.is_discarded()) {
        error = std::string(name) + ": reparse failed after serialize (internal bug)";
        return false;
    }
    const fs::path finalPath = fs::path(dir) / name;
    const fs::path tmpPath = fs::path(dir) / (std::string(name) + ".tmp");
    {
        std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = std::string(name) + ": cannot create temp file '" + tmpPath.string() + "'";
            return false;
        }
        file << text;
        file.flush();
        if (!file) {
            error = std::string(name) + ": failed writing temp file";
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmpPath, finalPath, ec);
    if (ec) {
        // Windows 个别文件系统 rename 覆盖失败 → 降级 remove+rename。
        ec.clear();
        fs::remove(finalPath, ec);
        ec.clear();
        fs::rename(tmpPath, finalPath, ec);
        if (ec) {
            error = std::string(name) + ": replace failed: " + ec.message();
            fs::remove(tmpPath, ec);
            return false;
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// LoadWorldData
// ---------------------------------------------------------------------------
bool LoadWorldData(const std::string& dir, WorldDataSet& out, std::string& error) {
    out = WorldDataSet{};

    json manifest;
    if (!ReadJsonFile(dir, "world_manifest.json", manifest, error) ||
        !ReadSchemaVersion(manifest, "world_manifest.json", error)) {
        return false;
    }
    const auto filesIt = manifest.find("files");
    if (filesIt != manifest.end() && filesIt->is_array()) {
        for (const auto& f : *filesIt) {
            if (f.is_string()) {
                out.manifest.files.push_back(f.get<std::string>());
            }
        }
    }

    json mapsRoot;
    if (!ReadJsonFile(dir, "maps.json", mapsRoot, error) ||
        !ReadSchemaVersion(mapsRoot, "maps.json", error) || !ParseMaps(mapsRoot, out, error)) {
        return false;
    }

    json npcsRoot;
    if (!ReadJsonFile(dir, "npcs.json", npcsRoot, error) ||
        !ReadSchemaVersion(npcsRoot, "npcs.json", error) || !ParseNpcs(npcsRoot, out, error)) {
        return false;
    }

    json spawnsRoot;
    if (!ReadJsonFile(dir, "monster_spawns.json", spawnsRoot, error) ||
        !ReadSchemaVersion(spawnsRoot, "monster_spawns.json", error) ||
        !ParseSpawns(spawnsRoot, out, error)) {
        return false;
    }

    json portalsRoot;
    if (!ReadJsonFile(dir, "portals.json", portalsRoot, error) ||
        !ReadSchemaVersion(portalsRoot, "portals.json", error) ||
        !ParsePortals(portalsRoot, out, error)) {
        return false;
    }

    out.manifest.schemaVersion = kWorldDataSchemaVersion;
    return true;
}

// ---------------------------------------------------------------------------
// ValidateWorldData（22.12 全规则；Server 启动与 Editor Validation 共用）
// ---------------------------------------------------------------------------
bool ValidateWorldData(const WorldDataSet& data, std::string& error) {
    if (data.manifest.schemaVersion != kWorldDataSchemaVersion) {
        error = "world_manifest.json: unsupported schemaVersion " +
                std::to_string(data.manifest.schemaVersion);
        return false;
    }

    // ---- maps ----
    std::set<std::uint16_t> mapIds;
    for (const auto& map : data.maps) {
        const std::string ctx = "maps.json: Map " + std::to_string(map.mapId);
        if (map.mapId == 0) {
            error = ctx + ": mapId must be > 0";
            return false;
        }
        if (!mapIds.insert(map.mapId).second) {
            error = ctx + ": duplicate mapId";
            return false;
        }
        if (!(map.minX < map.maxX) || !(map.minY < map.maxY)) {
            error = ctx + ": invalid bounds";
            return false;
        }
        if (!map.InBounds(map.spawnX, map.spawnY)) {
            error = ctx + ": spawn out of bounds";
            return false;
        }
        if (!map.InBounds(map.respawnX, map.respawnY)) {
            error = ctx + ": respawn out of bounds";
            return false;
        }
    }
    if (mapIds.count(kTownMapId) == 0) {
        error = "maps.json: town map missing (mapId " + std::to_string(kTownMapId) + " required)";
        return false;
    }
    auto mapExists = [&mapIds](std::uint16_t mapId) { return mapIds.count(mapId) != 0; };

    // ---- npcs ----
    std::set<std::uint32_t> npcIds;
    for (const auto& npc : data.npcs) {
        const std::string ctx = "npcs.json: Npc " + std::to_string(npc.npcDefinitionId);
        if (npc.npcDefinitionId == 0) {
            error = ctx + ": npcDefinitionId must be > 0";
            return false;
        }
        if (!npcIds.insert(npc.npcDefinitionId).second) {
            error = ctx + ": duplicate npcDefinitionId";
            return false;
        }
        if (!mapExists(npc.mapId)) {
            error = ctx + ": map " + std::to_string(npc.mapId) + " does not exist";
            return false;
        }
        const MapDefinition* map = nullptr;
        for (const auto& m : data.maps) {
            if (m.mapId == npc.mapId) {
                map = &m;
                break;
            }
        }
        if (map != nullptr && !map->InBounds(npc.spawnX, npc.spawnY)) {
            error = ctx + ": spawn out of bounds";
            return false;
        }
        if (!(npc.interactionRange > 0.0f)) {
            error = ctx + ": invalid interactionRange";
            return false;
        }
        std::set<std::uint32_t> questSeen;
        for (const std::uint32_t questId : npc.questIds) {
            if (!questSeen.insert(questId).second) {
                error = ctx + ": duplicate quest " + std::to_string(questId);
                return false;
            }
        }
    }

    // ---- dialogues ----
    std::set<std::uint32_t> dialogueIds;
    for (const auto& dialogue : data.dialogues) {
        const std::string ctx = "npcs.json: Dialogue " + std::to_string(dialogue.dialogueId);
        if (dialogue.dialogueId == 0) {
            error = ctx + ": dialogueId must be > 0";
            return false;
        }
        if (!dialogueIds.insert(dialogue.dialogueId).second) {
            error = ctx + ": duplicate dialogueId";
            return false;
        }
        if (dialogue.title.empty() || dialogue.text.empty()) {
            error = ctx + ": dialogue title/text must not be empty";
            return false;
        }
    }
    for (const auto& npc : data.npcs) {
        if (npc.dialogueId != 0 && dialogueIds.count(npc.dialogueId) == 0) {
            error = "npcs.json: Npc " + std::to_string(npc.npcDefinitionId) + ": dialogue " +
                    std::to_string(npc.dialogueId) + " does not exist";
            return false;
        }
    }

    // ---- monster_spawns ----
    std::set<std::uint32_t> spawnIds;
    for (const auto& spawn : data.monsterSpawns) {
        const std::string ctx =
            "monster_spawns.json: MonsterSpawn " + std::to_string(spawn.spawnId);
        if (spawn.spawnId == 0) {
            error = ctx + ": spawnId must be > 0";
            return false;
        }
        if (!spawnIds.insert(spawn.spawnId).second) {
            error = ctx + ": duplicate spawnId";
            return false;
        }
        if (!mapExists(spawn.mapId)) {
            error = ctx + ": map " + std::to_string(spawn.mapId) + " does not exist";
            return false;
        }
        if (spawn.monsterDefinitionId == 0) {
            error = ctx + ": monsterDefinitionId must be > 0";
            return false;
        }
        if (spawn.radius < 0.0f || spawn.count == 0 || spawn.count > 1000) {
            error = ctx + ": invalid radius/count";
            return false;
        }
        if (spawn.respawnSeconds == 0) {
            error = ctx + ": invalid respawnSeconds (must be >= 1)";
            return false;
        }
        const MapDefinition* map = nullptr;
        for (const auto& m : data.maps) {
            if (m.mapId == spawn.mapId) {
                map = &m;
                break;
            }
        }
        if (map != nullptr) {
            const float minX = spawn.centerX - spawn.radius;
            const float maxX = spawn.centerX + spawn.radius;
            const float minY = spawn.centerY - spawn.radius;
            const float maxY = spawn.centerY + spawn.radius;
            if (minX < map->minX || maxX > map->maxX || minY < map->minY || maxY > map->maxY) {
                error = ctx + ": spawn zone out of map bounds";
                return false;
            }
        }
    }

    // ---- portals ----
    std::set<std::uint32_t> portalIds;
    for (const auto& portal : data.portals) {
        const std::string ctx = "portals.json: Portal " + std::to_string(portal.portalId);
        if (portal.portalId == 0) {
            error = ctx + ": portalId must be > 0";
            return false;
        }
        if (!portalIds.insert(portal.portalId).second) {
            error = ctx + ": duplicate portalId";
            return false;
        }
        if (!mapExists(portal.sourceMapId)) {
            error = ctx + ": source map " + std::to_string(portal.sourceMapId) +
                    " does not exist";
            return false;
        }
        if (!mapExists(portal.destinationMapId)) {
            error = ctx + ": destination map " + std::to_string(portal.destinationMapId) +
                    " does not exist";
            return false;
        }
        const MapDefinition* source = nullptr;
        const MapDefinition* destination = nullptr;
        for (const auto& m : data.maps) {
            if (m.mapId == portal.sourceMapId) {
                source = &m;
            }
            if (m.mapId == portal.destinationMapId) {
                destination = &m;
            }
        }
        if (source != nullptr && !source->InBounds(portal.x, portal.y)) {
            error = ctx + ": source position out of bounds";
            return false;
        }
        if (destination != nullptr && !destination->InBounds(portal.destinationX,
                                                             portal.destinationY)) {
            error = ctx + ": destination position out of bounds";
            return false;
        }
        if (!(portal.interactionRadius > 0.0f)) {
            error = ctx + ": invalid interactionRadius";
            return false;
        }
        if (portal.minLevel < 1) {
            error = ctx + ": invalid minLevel (must be >= 1)";
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// SaveWorldData（22.14 原子保存 + 22.15 备份）
// ---------------------------------------------------------------------------
bool SaveWorldData(const std::string& dir, const WorldDataSet& data, std::string& error) {
    if (!ValidateWorldData(data, error)) {
        return false; // Error 存在禁止保存（22.13）
    }
    std::error_code ec;
    fs::create_directories(dir, ec);

    // 备份旧文件（22.15）。
    BackupFile(dir, "maps.json");
    BackupFile(dir, "npcs.json");
    BackupFile(dir, "monster_spawns.json");
    BackupFile(dir, "portals.json");
    BackupFile(dir, "world_manifest.json");

    WorldDataSet normalized = data;
    normalized.manifest.schemaVersion = kWorldDataSchemaVersion;
    normalized.manifest.files = {"maps.json", "npcs.json", "monster_spawns.json",
                                 "portals.json"};

    json manifestJson{
        {"schemaVersion", normalized.manifest.schemaVersion},
        {"files", normalized.manifest.files},
    };
    json mapsJson{
        {"schemaVersion", kWorldDataSchemaVersion},
        {"maps", json::array()},
    };
    for (const auto& map : normalized.maps) {
        mapsJson["maps"].push_back(MapToJson(map));
    }
    json npcsJson{
        {"schemaVersion", kWorldDataSchemaVersion},
        {"npcs", json::array()},
    };
    for (const auto& npc : normalized.npcs) {
        const DialogueDefinition* dialogue = nullptr;
        for (const auto& d : normalized.dialogues) {
            if (d.dialogueId == npc.dialogueId) {
                dialogue = &d;
                break;
            }
        }
        npcsJson["npcs"].push_back(NpcToJson(npc, dialogue));
    }
    json spawnsJson{
        {"schemaVersion", kWorldDataSchemaVersion},
        {"monsterSpawns", json::array()},
    };
    for (const auto& spawn : normalized.monsterSpawns) {
        spawnsJson["monsterSpawns"].push_back(SpawnToJson(spawn));
    }
    json portalsJson{
        {"schemaVersion", kWorldDataSchemaVersion},
        {"portals", json::array()},
    };
    for (const auto& portal : normalized.portals) {
        portalsJson["portals"].push_back(PortalToJson(portal));
    }

    return WriteAtomic(dir, "maps.json", mapsJson, error) &&
           WriteAtomic(dir, "npcs.json", npcsJson, error) &&
           WriteAtomic(dir, "monster_spawns.json", spawnsJson, error) &&
           WriteAtomic(dir, "portals.json", portalsJson, error) &&
           WriteAtomic(dir, "world_manifest.json", manifestJson, error);
}

// ---------------------------------------------------------------------------
// MakeDefaultWorldData（22.18：阶段21 硬编码世界迁移；单一事实来源）
// ---------------------------------------------------------------------------
WorldDataSet MakeDefaultWorldData() {
    WorldDataSet data;
    data.manifest.schemaVersion = kWorldDataSchemaVersion;
    data.manifest.files = {"maps.json", "npcs.json", "monster_spawns.json", "portals.json"};

    // 阶段21 指令三：3 张地图。
    MapDefinition map1;
    map1.mapId = 1;
    map1.name = "Greenfield Village";
    map1.type = MapType::Town;
    map1.minX = 0.0f;
    map1.minY = 0.0f;
    map1.maxX = 2000.0f;
    map1.maxY = 2000.0f;
    map1.spawnX = 300.0f;
    map1.spawnY = 300.0f;
    map1.respawnX = 300.0f;
    map1.respawnY = 300.0f;
    data.maps.push_back(map1);

    MapDefinition map2;
    map2.mapId = 2;
    map2.name = "Slime Meadow";
    map2.type = MapType::Field;
    map2.minX = 0.0f;
    map2.minY = 0.0f;
    map2.maxX = 2000.0f;
    map2.maxY = 2000.0f;
    map2.spawnX = 200.0f;
    map2.spawnY = 500.0f;
    map2.respawnX = 200.0f;
    map2.respawnY = 500.0f;
    data.maps.push_back(map2);

    MapDefinition map3;
    map3.mapId = 3;
    map3.name = "Ancient Ruins";
    map3.type = MapType::Field;
    map3.minX = 0.0f;
    map3.minY = 0.0f;
    map3.maxX = 2400.0f;
    map3.maxY = 1800.0f;
    map3.spawnX = 200.0f;
    map3.spawnY = 300.0f;
    map3.respawnX = 200.0f;
    map3.respawnY = 300.0f;
    data.maps.push_back(map3);

    // 阶段20 指令九：NPC 5001~5004（含对话文本）。
    data.npcs.push_back(
        {5001, "Village Elder", NpcType::QuestGiver, 1, 300.0f, 300.0f, 120.0f, 5001, 0, 0,
         {4001, 4002, 4003, 4005}, 5001});
    data.npcs.push_back(
        {5002, "General Merchant", NpcType::Merchant, 1, 450.0f, 300.0f, 120.0f, 5002, 6001, 0,
         {}, 5002});
    data.npcs.push_back(
        {5003, "Wayfarer", NpcType::Teleporter, 1, 600.0f, 300.0f, 120.0f, 5003, 0, 7001, {},
         5003});
    data.npcs.push_back(
        {5004, "Explorer Guide", NpcType::MultiFunction, 1, 750.0f, 300.0f, 120.0f, 5004, 0,
         7002, {4004}, 5004});
    data.dialogues.push_back(
        {5001, "Village Elder", "The slimes have been restless lately. Will you help us?"});
    data.dialogues.push_back({5002, "General Merchant", "Welcome! Finest goods in the village."});
    data.dialogues.push_back({5003, "Wayfarer", "I can take you anywhere, for a price."});
    data.dialogues.push_back(
        {5004, "Explorer Guide", "Looking for adventure? The far plains await."});

    // 阶段21 指令十一 → 阶段22 22.18 数据驱动：Map2 x20 / Map3 x10。
    // zone 参数保证：入口 spawn AOI（600）内有怪 + 避开 Portal 交互半径（100）。
    MonsterSpawnDefinition map2Spawn;
    map2Spawn.spawnId = 2001;
    map2Spawn.mapId = 2;
    map2Spawn.monsterDefinitionId = kTrainingSlimeTypeId;
    map2Spawn.centerX = 900.0f;
    map2Spawn.centerY = 950.0f;
    map2Spawn.radius = 400.0f;
    map2Spawn.count = 20;
    map2Spawn.respawnSeconds = 8;
    map2Spawn.enabled = true;
    data.monsterSpawns.push_back(map2Spawn);

    MonsterSpawnDefinition map3Spawn;
    map3Spawn.spawnId = 3001;
    map3Spawn.mapId = 3;
    map3Spawn.monsterDefinitionId = kTrainingSlimeTypeId;
    map3Spawn.centerX = 950.0f;
    map3Spawn.centerY = 820.0f;
    map3Spawn.radius = 350.0f;
    map3Spawn.count = 10;
    map3Spawn.respawnSeconds = 8;
    map3Spawn.enabled = true;
    data.monsterSpawns.push_back(map3Spawn);

    // 阶段21 指令十六：Portal 8001~8004。
    data.portals.push_back({8001, 1, 1000.0f, 300.0f, 100.0f, 2, 200.0f, 500.0f, 1, 0, true});
    data.portals.push_back({8002, 2, 150.0f, 500.0f, 100.0f, 1, 900.0f, 300.0f, 1, 0, true});
    data.portals.push_back({8003, 2, 1800.0f, 1000.0f, 100.0f, 3, 200.0f, 300.0f, 2, 10, true});
    data.portals.push_back({8004, 3, 150.0f, 300.0f, 100.0f, 2, 1700.0f, 1000.0f, 1, 0, true});

    return data;
}

} // namespace legend::world
