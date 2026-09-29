// ---------------------------------------------------------------------------
// 阶段22：WorldDataChecks —— Data/World JSON 数据层检查（22.19：加入现有
// LegendWorldTests，不新增第四套 CTest）。
// A 部分（纯逻辑）：MakeDefaultWorldData / Load / Validate 全规则 /
//                   Save 原子性 / Roundtrip / Backup / 错误日志格式（22.21）。
// B 部分（真实链路）：WorldServer 从真实 Data 目录启动（22.11/22.12）——
//                   数据生效（NPC/Portal/Monster 数量）+ 坏数据拒绝启动（23.23）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Server/WorldServer/Map/MapRegistry.h"
#include "Server/WorldServer/Monster/MonsterSpawnRegistry.h"
#include "Server/WorldServer/Npc/NpcRegistry.h"
#include "Server/WorldServer/Portal/PortalRegistry.h"
#include "Shared/WorldData/WorldDataJson.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>

namespace worldtest {

namespace {

namespace fs = std::filesystem;
using namespace legend::world;

// ---- 辅助：写一个完整合法的临时世界数据目录 ----
bool WriteDefaultWorld(const fs::path& dir, std::string& error) {
    WorldDataSet data = MakeDefaultWorldData();
    return SaveWorldData(dir.string(), data, error);
}

bool WriteTextFile(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << text;
    return true;
}

void RunWorldDataLogicChecks() {
    // ---- 默认世界 + 数量（22.18 迁移）----
    {
        WorldDataSet data = MakeDefaultWorldData();
        Check("WorldData: default data has 3 maps / 4 npcs / 2 spawns / 4 portals",
              data.maps.size() == 3 && data.npcs.size() == 4 &&
                  data.monsterSpawns.size() == 2 && data.portals.size() == 4 &&
                  data.dialogues.size() == 4);
        std::string error;
        Check("WorldData: default data validates", ValidateWorldData(data, error));
    }

    // ---- Roundtrip：Save → Load → 等值（22.10）----
    {
        const fs::path dir = fs::temp_directory_path() / "legend_worlddata_roundtrip";
        std::error_code ec;
        fs::remove_all(dir, ec);
        WorldDataSet data = MakeDefaultWorldData();
        std::string error;
        Check("WorldData: save default world", SaveWorldData(dir.string(), data, error));
        WorldDataSet loaded;
        Check("WorldData: load default world", LoadWorldData(dir.string(), loaded, error));
        bool equal = loaded.maps.size() == data.maps.size() &&
                     loaded.npcs.size() == data.npcs.size() &&
                     loaded.monsterSpawns.size() == data.monsterSpawns.size() &&
                     loaded.portals.size() == data.portals.size() &&
                     loaded.dialogues.size() == data.dialogues.size();
        for (std::size_t i = 0; equal && i < data.maps.size(); ++i) {
            equal = loaded.maps[i].mapId == data.maps[i].mapId &&
                    loaded.maps[i].name == data.maps[i].name &&
                    loaded.maps[i].type == data.maps[i].type &&
                    loaded.maps[i].maxX == data.maps[i].maxX &&
                    loaded.maps[i].respawnY == data.maps[i].respawnY;
        }
        for (std::size_t i = 0; equal && i < data.portals.size(); ++i) {
            equal = loaded.portals[i].portalId == data.portals[i].portalId &&
                    loaded.portals[i].goldCost == data.portals[i].goldCost &&
                    loaded.portals[i].destinationMapId == data.portals[i].destinationMapId;
        }
        Check("WorldData: roundtrip preserves definitions", equal);
        // 原子保存：正式文件存在且无 .tmp 残留（22.14）。
        bool noTmpLeft = true;
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() == ".tmp") {
                noTmpLeft = false;
            }
        }
        Check("WorldData: atomic save leaves no temp files", noTmpLeft);
        fs::remove_all(dir, ec);
    }

    // ---- Validate 规则逐项（22.12）----
    {
        std::string error;
        const auto mutate = [](WorldDataSet data,
                               const std::function<void(WorldDataSet&)>& op) {
            op(data);
            return data;
        };
        const auto base = [] { return MakeDefaultWorldData(); };

        Check("WorldData: duplicate mapId rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.maps.push_back(d.maps[0]);
              }), error));
        Check("WorldData: duplicate npcId rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.npcs.push_back(d.npcs[0]);
              }), error));
        Check("WorldData: duplicate portalId rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.portals.push_back(d.portals[0]);
              }), error));
        Check("WorldData: npc on missing map rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.npcs[0].mapId = 99;
              }), error));
        Check("WorldData: portal destination on missing map rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.portals[0].destinationMapId = 99;
              }), error));
        Check("WorldData: spawn on missing map rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.monsterSpawns[0].mapId = 99;
              }), error));
        Check("WorldData: npc out of bounds rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.npcs[0].spawnX = 9999.0f;
              }), error));
        Check("WorldData: portal destination out of bounds rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.portals[0].destinationX = 9999.0f;
              }), error));
        Check("WorldData: spawn zone out of map bounds rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.monsterSpawns[0].centerX = 2600.0f;
              }), error));
        Check("WorldData: map respawn out of bounds rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.maps[0].respawnX = 5000.0f;
              }), error));
        Check("WorldData: zero count rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.monsterSpawns[0].count = 0;
              }), error));
        Check("WorldData: zero respawnSeconds rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.monsterSpawns[0].respawnSeconds = 0;
              }), error));
        Check("WorldData: zero interactionRadius rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.portals[0].interactionRadius = 0.0f;
              }), error));
        Check("WorldData: minLevel < 1 rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.portals[0].minLevel = 0;
              }), error));
        Check("WorldData: missing town map rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.maps.erase(d.maps.begin());
              }), error));
        Check("WorldData: npc dialogue missing rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.dialogues.clear();
              }), error));
        Check("WorldData: empty dialogue text rejected",
              !ValidateWorldData(mutate(base(), [](WorldDataSet& d) {
                  d.dialogues[0].text.clear();
              }), error));
    }

    // ---- Load 错误路径（bad JSON / 缺文件 / schema / 字段类型；22.21 错误格式）----
    {
        const fs::path dir = fs::temp_directory_path() / "legend_worlddata_bad";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        WorldDataSet loaded;
        std::string error;
        Check("WorldData: load fails on missing dir", !LoadWorldData(dir.string(), loaded, error));
        Check("WorldData: error names the file", error.find("world_manifest.json") !=
                                                     std::string::npos);

        std::string writeError;
        WriteDefaultWorld(dir, writeError);
        // 坏 JSON 语法。
        WriteTextFile(dir / "maps.json", "{ this is not json");
        Check("WorldData: bad JSON syntax rejected", !LoadWorldData(dir.string(), loaded, error));
        Check("WorldData: bad syntax error mentions maps.json",
              error.find("maps.json") != std::string::npos);
        // schemaVersion 不匹配。
        WriteDefaultWorld(dir, writeError);
        WriteTextFile(dir / "portals.json",
                      "{\n  \"schemaVersion\": 99,\n  \"portals\": []\n}\n");
        Check("WorldData: unknown schemaVersion rejected",
              !LoadWorldData(dir.string(), loaded, error));
        // 字段类型错。
        WriteDefaultWorld(dir, writeError);
        WriteTextFile(dir / "npcs.json",
                      "{\n  \"schemaVersion\": 1,\n  \"npcs\": [\n    "
                      "{\"npcDefinitionId\": 5001, \"name\": 42}\n  ]\n}\n");
        Check("WorldData: wrong field type rejected", !LoadWorldData(dir.string(), loaded, error));
        fs::remove_all(dir, ec);
    }

    // ---- Backup 轮换上限（22.15：最多 10 份）----
    {
        const fs::path dir = fs::temp_directory_path() / "legend_worlddata_backup";
        std::error_code ec;
        fs::remove_all(dir, ec);
        WorldDataSet data = MakeDefaultWorldData();
        std::string error;
        SaveWorldData(dir.string(), data, error);
        for (int i = 0; i < 14; ++i) {
            data.maps[0].name = "Map " + std::to_string(i);
            if (!SaveWorldData(dir.string(), data, error)) {
                break;
            }
        }
        // 轮换是 per-file（22.15：每文件保留最近 10 个版本）——按 maps.*.json 统计。
        const fs::path backup = dir / ".backup";
        int backupFiles = 0;
        if (fs::exists(backup)) {
            for (const auto& entry : fs::directory_iterator(backup)) {
                const std::string fileName = entry.path().filename().string();
                if (fileName.rfind("maps.", 0) == 0 && entry.path().extension() == ".json") {
                    ++backupFiles;
                }
            }
        }
        Check("WorldData: backup rotation capped at 10", backupFiles <= 10 && backupFiles >= 8);
        fs::remove_all(dir, ec);
    }
}

void RunWorldDataChainChecks() {
    // ---- WorldServer 从真实 Data 目录启动（22.19：WorldServer 读取真实 Data）----
    {
        const fs::path dir = fs::temp_directory_path() / "legend_worlddata_live";
        std::error_code ec;
        fs::remove_all(dir, ec);
        WorldDataSet data = MakeDefaultWorldData();
        // 可观察的数据改动：Map2 spawn count 20 -> 25。
        data.monsterSpawns[0].count = 25;
        std::string error;
        Check("WorldData: write live world data", SaveWorldData(dir.string(), data, error));

        WorldTestServers servers;
        servers.worldDataDir = dir.string();
        const bool started = servers.StartLogin() && servers.StartWorld();
        Check("WorldData: server starts with real Data dir", started);
        if (started) {
            // slots = legacy(20, harness 默认开) + Map2(25) + Map3(10) = 55。
            Check("WorldData: spawn slots follow JSON counts",
                  servers.world->SpawnSlotCount() == 55);
        }
        servers.StopAll();
        fs::remove_all(dir, ec);
    }

    // ---- 坏数据 → 明确拒绝启动（22.12/23.23：不静默回退）----
    {
        const fs::path dir = fs::temp_directory_path() / "legend_worlddata_corrupt";
        std::error_code ec;
        fs::remove_all(dir, ec);
        std::string writeError;
        WriteDefaultWorld(dir, writeError);
        WriteTextFile(dir / "portals.json",
                      "{\n  \"schemaVersion\": 1,\n  \"portals\": [\n    "
                      "{\"portalId\": 8001, \"sourceMapId\": 1, \"x\": 1000.0, \"y\": 300.0, "
                      "\"interactionRadius\": 100.0, \"destinationMapId\": 42, "
                      "\"destinationX\": 10.0, \"destinationY\": 10.0, \"minLevel\": 1, "
                      "\"goldCost\": 0, \"enabled\": true}\n  ]\n}\n");

        WorldTestServers servers;
        servers.worldDataDir = dir.string();
        const bool started = servers.StartLogin() && servers.StartWorld();
        Check("WorldData: server rejects broken world data", !started);
        servers.StopAll();
        fs::remove_all(dir, ec);
    }

    // ---- Registry 注入路径（LoadDefaults / LoadFromDefinitions）----
    {
        MapRegistry::LoadDefaults();
        NpcRegistry::LoadDefaults();
        MonsterSpawnRegistry::LoadDefaults();
        PortalRegistry::LoadDefaults();
        Check("WorldData: registry defaults loaded",
              MapRegistry::Instance().Count() == 3 &&
                  NpcRegistry::Instance().Count() == 4 &&
                  MonsterSpawnRegistry::Instance().Count() == 2 &&
                  PortalRegistry::Instance().Count() == 4);
        Check("WorldData: registry lookups intact",
              MapRegistry::Instance().FindMap(2) != nullptr &&
                  NpcRegistry::Instance().FindNpc(5003) != nullptr &&
                  MonsterSpawnRegistry::Instance().FindSpawn(3001) != nullptr &&
                  PortalRegistry::Instance().FindPortal(8003) != nullptr);
        std::string error;
        Check("WorldData: spawn registry validates against maps",
              MonsterSpawnRegistry::Instance().ValidateSpawns(MapRegistry::Instance(), error));
    }
}

} // namespace

// 由 WorldChecks.cpp 调用（阶段22）。
void RunWorldDataChecks() {
    std::printf("[WorldData] logic checks begin\n");
    RunWorldDataLogicChecks();
    std::printf("[WorldData] chain checks begin\n");
    RunWorldDataChainChecks();
    std::printf("[WorldData] checks complete (failures so far = %d)\n", g_failures);
}

} // namespace worldtest
