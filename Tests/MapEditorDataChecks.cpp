// ---------------------------------------------------------------------------
// 阶段22：MapEditorDataChecks —— World Editor 文档模型检查（22.19：加入现有
// LegendWorldTests，不新增第四套 CTest）。纯逻辑，无 servers。
// 覆盖：默认世界 / Mutate·Undo·Redo（100 步上限）/ Duplicate 自动 ID /
// Remove / Validation Error 禁存 / Roundtrip / Backup 轮换。
// GUI 需人工确认项记录在 README（22.20）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Tools/MapEditor/Source/WorldDocument.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace worldtest {

namespace {

using legend::editor::WorldDocument;
namespace fs = std::filesystem;

void RunMapEditorDataLogicChecks() {
    // ---- 默认世界（22.18 迁移数据作为 New World 起点）----
    {
        WorldDocument doc;
        doc.NewFromDefaults();
        Check("MapEditorData: default world has 3 maps / 4 npcs / 3 spawns / 4 portals",
              doc.Data().maps.size() == 3 && doc.Data().npcs.size() == 4 &&
                  doc.Data().monsterSpawns.size() == 3 && doc.Data().portals.size() == 4);
        Check("MapEditorData: default world validates clean", !doc.HasErrors());
        Check("MapEditorData: NewFromDefaults marks dirty", doc.IsDirty());
    }

    // ---- Mutate / Undo / Redo ----
    {
        WorldDocument doc;
        doc.NewFromDefaults();
        doc.Mutate([](legend::world::WorldDataSet& d) { d.maps[0].name = "Renamed"; });
        Check("MapEditorData: mutate applies", doc.Data().maps[0].name == "Renamed");
        Check("MapEditorData: undo available after mutate", doc.CanUndo());
        doc.Undo();
        Check("MapEditorData: undo restores previous value",
              doc.Data().maps[0].name == "Greenfield Village");
        doc.Redo();
        Check("MapEditorData: redo reapplies change",
              doc.Data().maps[0].name == "Renamed");
    }

    // ---- Undo 上限 100 步（22.4）----
    {
        WorldDocument doc;
        doc.NewFromDefaults();
        for (int i = 0; i < 130; ++i) {
            doc.Mutate([i](legend::world::WorldDataSet& d) {
                d.maps[0].maxX = 2000.0f + static_cast<float>(i);
            });
        }
        int undos = 0;
        while (doc.Undo()) {
            ++undos;
        }
        Check("MapEditorData: undo stack capped at 100 steps", undos == 100);
        Check("MapEditorData: oldest change (0) is lost beyond cap",
              doc.Data().maps[0].maxX == 2000.0f + 29.0f);
    }

    // ---- Duplicate：自动分配新的可用 ID（22.13）----
    {
        WorldDocument doc;
        doc.NewFromDefaults();
        doc.SetSelection(WorldDocument::ObjectType::Npc, 5001);
        const bool npcDup = doc.DuplicateSelected() && doc.FindNpc(5005) != nullptr &&
                            doc.Data().npcs.size() == 5;
        doc.SetSelection(WorldDocument::ObjectType::Spawn, 2001);
        // 阶段25：默认新增 boss spawn 3002 -> duplicate 自动分配 3003。
        const bool spawnDup = doc.DuplicateSelected() && doc.FindSpawn(3003) != nullptr &&
                              doc.Data().monsterSpawns.size() == 4;
        doc.SetSelection(WorldDocument::ObjectType::Portal, 8001);
        const bool portalDup =
            doc.DuplicateSelected() && doc.FindPortal(8005) != nullptr;
        Check("MapEditorData: duplicate assigns unused ids (npc/spawn/portal)",
              npcDup && spawnDup && portalDup);
        Check("MapEditorData: duplicates keep world valid", !doc.HasErrors());
    }

    // ---- RemoveSelected ----
    {
        WorldDocument doc;
        doc.NewFromDefaults();
        doc.SetSelection(WorldDocument::ObjectType::Npc, 5002);
        const bool removed =
            doc.RemoveSelected() && doc.FindNpc(5002) == nullptr &&
            doc.Data().npcs.size() == 3;
        Check("MapEditorData: remove selected npc", removed);
        // NPC 移除时其内嵌对话一并移除（dialogueId = npcDefinitionId）。
        bool dialogueGone = true;
        for (const auto& dialogue : doc.Data().dialogues) {
            if (dialogue.dialogueId == 5002) {
                dialogueGone = false;
            }
        }
        Check("MapEditorData: removing npc drops its dialogue", dialogueGone);
    }

    // ---- Validation Error 禁存 / Undo 清错 / Roundtrip / Backup（22.13~22.15）----
    {
        WorldDocument doc;
        doc.NewFromDefaults();
        const fs::path dir = fs::temp_directory_path() / "legend_editor_doc_test_world";
        std::error_code ec;
        fs::remove_all(dir, ec);
        doc.SetDirectory(dir.string());
        std::string error;
        Check("MapEditorData: first save succeeds (atomic write)", doc.Save(error));

        doc.Mutate([](legend::world::WorldDataSet& d) {
            d.monsterSpawns[0].centerX = 2500.0f; // map2 maxX=2000 → zone 越界
        });
        Check("MapEditorData: out-of-bounds spawn zone flagged", doc.HasErrors());
        Check("MapEditorData: save blocked while errors present", !doc.Save(error));
        doc.Undo();
        Check("MapEditorData: undo clears validation error", !doc.HasErrors());
        Check("MapEditorData: save ok again after undo", doc.Save(error));

        // Roundtrip：重新 Load，数据一致（22.10）。
        WorldDocument reloaded;
        const bool roundtrip =
            reloaded.Load(dir.string(), error) && reloaded.Data().maps.size() == 3 &&
            reloaded.Data().monsterSpawns.size() == 3 &&
            reloaded.Data().monsterSpawns[0].centerX < 2000.0f;
        Check("MapEditorData: reload roundtrip keeps data", roundtrip);

        // Backup：第二次保存后 .backup 出现且可解析（22.15）。
        reloaded.Mutate([](legend::world::WorldDataSet& d) { d.maps[0].name = "Backup Probe"; });
        Check("MapEditorData: second save rotates backup", reloaded.Save(error));
        const fs::path backup = dir / ".backup";
        int backupFiles = 0;
        if (fs::exists(backup)) {
            for (const auto& entry : fs::directory_iterator(backup)) {
                if (entry.path().extension() == ".json") {
                    ++backupFiles;
                }
            }
        }
        Check("MapEditorData: backup directory populated", backupFiles >= 1);
        fs::remove_all(dir, ec);
    }

    // ---- Load 校验拦截（坏数据 → Load 失败）----
    {
        WorldDocument doc;
        const fs::path dir = fs::temp_directory_path() / "legend_editor_doc_bad_world";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        std::string error;
        // 从默认世界导出后破坏一处（NPC 引用不存在的地图）再保存会拒——直接
        // 手写坏 maps.json 验证 Load 路径。
        std::ofstream mapsFile(dir / "maps.json", std::ios::binary);
        mapsFile << "{\n  \"schemaVersion\": 1,\n  \"maps\": [\n    {\"mapId\": 1, "
                    "\"name\": \"Broken\", \"type\": \"Town\", \"minX\": 0.0, \"minY\": 0.0, "
                    "\"maxX\": -5.0, \"maxY\": 0.0, \"spawnX\": 0.0, \"spawnY\": 0.0, "
                    "\"respawnX\": 0.0, \"respawnY\": 0.0}\n  ]\n}\n";
        mapsFile.close();
        // 缺 npcs/monster_spawns/portals → Load 失败（文件缺失）。
        Check("MapEditorData: load fails on incomplete data dir", !doc.Load(dir.string(), error));
        fs::remove_all(dir, ec);
    }
}

} // namespace

// 由 WorldChecks.cpp 调用（阶段22）。
void RunMapEditorDataChecks() {
    std::printf("[MapEditorData] logic checks begin\n");
    RunMapEditorDataLogicChecks();
    std::printf("[MapEditorData] checks complete (failures so far = %d)\n", g_failures);
}

} // namespace worldtest
