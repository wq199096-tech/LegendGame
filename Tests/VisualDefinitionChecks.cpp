// ---------------------------------------------------------------------------
// 阶段24：VisualDefinitionChecks —— visual_entities.json / effects.json /
// visual_maps.json（MapVisualDefinition）+ VisualDataCatalog 检查
//（指令四十五/四十六：visual entity validation / effect validation /
// map visual validation / fallback handling / missing asset ref）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Client/Visuals/VisualDataCatalog.h"
#include "Shared/WorldData/WorldDataJson.h"

#include <string>

#ifndef LEGEND_SOURCE_DIR
#define LEGEND_SOURCE_DIR "."
#endif

namespace worldtest {

namespace {

using namespace legend::visual;

void RunVisualDefinitionLogicChecks() {
    const std::string sourceDir = LEGEND_SOURCE_DIR;

    // ---- 真实数据全量加载（catalog.Load 覆盖 7 个文件的视觉/展示字段）----
    VisualDataCatalog catalog;
    {
        std::string error;
        const bool loaded = catalog.Load(sourceDir + "/Data", error);
        Check("VisualDef: repo Data/ loads via catalog", loaded);
        if (loaded) {
            Check("VisualDef: entities >= 9 (指令三十七)",
                  catalog.Entities().entities.size() >= 9);
            Check("VisualDef: effects >= 4", catalog.Effects().effects.size() >= 4);
            Check("VisualDef: map visuals == 3", catalog.MapVisuals().size() == 3);
            std::string validateError;
            Check("VisualDef: full ValidateVisualData clean",
                  ValidateVisualData(catalog.Manifest(), catalog.Animations(),
                                     catalog.Entities(), catalog.Effects(), validateError));
        }
    }

    // ---- visual entity validation ----
    {
        const VisualEntityDef* warrior = catalog.FindEntity("player_warrior");
        Check("VisualDef: player_warrior exists with 6 slots",
              warrior != nullptr && warrior->animations.size() == 6);
        Check("VisualDef: player class lookup (1/2/3 ok, 9 fallback nullptr)",
              catalog.FindPlayerEntityByClass(1) != nullptr &&
                  catalog.FindPlayerEntityByClass(2) != nullptr &&
                  catalog.FindPlayerEntityByClass(3) != nullptr &&
                  catalog.FindPlayerEntityByClass(9) == nullptr);
        Check("VisualDef: npc serverVisualId lookup (5001/5004)",
              catalog.FindNpcEntityByServerVisualId(5001) != nullptr &&
                  catalog.FindNpcEntityByServerVisualId(5004) != nullptr &&
                  catalog.FindNpcEntityByServerVisualId(9999) == nullptr);
        Check("VisualDef: portal_default exists",
              catalog.FindEntity("portal_default") != nullptr);
        Check("VisualDef: missing entity fallback (nullptr)",
              catalog.FindEntity("ghost_entity") == nullptr);
    }

    // ---- 缺失必填槽位 / 未知 kind / 重复 visualId / serverVisualId 规则 ----
    {
        VisualEntitySet set = catalog.Entities();
        if (!set.entities.empty()) {
            VisualEntitySet missingSlot = set;
            missingSlot.entities.front().animations.erase("idle");
            std::string error;
            Check("VisualDef: missing idle slot rejected",
                  !ValidateVisualEntitySet(missingSlot, error));

            VisualEntitySet badKind = set;
            badKind.entities.front().kind = "Vehicle";
            std::string error2;
            Check("VisualDef: unknown kind rejected", !ValidateVisualEntitySet(badKind, error2));

            VisualEntitySet dup = set;
            dup.entities.push_back(dup.entities.front());
            std::string error3;
            Check("VisualDef: duplicate visualId rejected",
                  !ValidateVisualEntitySet(dup, error3));

            VisualEntitySet npcNoId = set;
            for (auto& entity : npcNoId.entities) {
                if (entity.kind == "Npc") {
                    entity.serverVisualId = 0;
                    break;
                }
            }
            std::string error4;
            Check("VisualDef: npc without serverVisualId rejected",
                  !ValidateVisualEntitySet(npcNoId, error4));
        }
    }

    // ---- ResolveVisualEntities：缺失 animation 引用（missing asset ref 检查）----
    {
        VisualEntitySet broken = catalog.Entities();
        bool broke = false;
        for (auto& entity : broken.entities) {
            auto it = entity.animations.find("idle");
            if (it != entity.animations.end()) {
                it->second = "ghost_clip";
                broke = true;
                break;
            }
        }
        std::string error;
        Check("VisualDef: unknown animationId rejected by resolve",
              broke && !ResolveVisualEntities(broken, catalog.Animations(),
                                              catalog.Manifest(), error));
    }

    // ---- effect validation ----
    {
        EffectSet effects = catalog.Effects();
        std::string validateError;
        std::string resolveError;
        Check("VisualDef: repo effects validate clean",
              ValidateEffectSet(effects, validateError) &&
                  ResolveEffects(effects, catalog.Manifest(), resolveError));
        Check("VisualDef: FindEffect hit/miss",
              catalog.FindEffect("fx_fire_bolt_impact") != nullptr &&
                  catalog.FindEffect("fx_ghost") == nullptr);

        EffectSet badFps = effects;
        if (!badFps.effects.empty()) {
            badFps.effects.front().fps = -1.0f;
            std::string error;
            Check("VisualDef: invalid effect fps rejected", !ValidateEffectSet(badFps, error));
        }
        EffectSet dup = effects;
        if (!dup.effects.empty()) {
            dup.effects.push_back(dup.effects.front());
            std::string error;
            Check("VisualDef: duplicate effectId rejected", !ValidateEffectSet(dup, error));
        }
        EffectSet missingSheet = effects;
        if (!missingSheet.effects.empty()) {
            missingSheet.effects.front().spriteSheetAssetId = "no_such_sheet";
            std::string error;
            Check("VisualDef: effect missing sheet rejected by resolve",
                  !ResolveEffects(missingSheet, catalog.Manifest(), error));
        }
    }

    // ---- map visual validation（WorldData 第 6 文件）----
    {
        legend::world::WorldDataSet world;
        std::string error;
        const bool loaded = LoadWorldData(sourceDir + "/Data/World", world, error);
        Check("VisualDef: repo Data/World (6 files incl visual_maps) loads", loaded);
        if (loaded) {
            Check("VisualDef: repo world validates clean",
                  ValidateWorldData(world, error));
            Check("VisualDef: maps carry visualMapId bindings",
                  world.maps.size() == 3 && !world.maps[0].visualMapId.empty());
            Check("VisualDef: visual map layers are Ground/Decoration/Object/Foreground",
                  world.visualMaps.size() == 3 &&
                      world.visualMaps[0].layers.size() == 4 &&
                      world.visualMaps[0].layers[0].name == "Ground" &&
                      world.visualMaps[0].layers[3].name == "Foreground");
            Check("VisualDef: catalog map visual lookup by id",
                  catalog.FindMapVisual("vmap_slime_meadow") != nullptr);
            Check("VisualDef: map display name lookup",
                  catalog.MapDisplayName(1) == "绿野村" &&
                  catalog.MapDisplayName(99) == "未知地图");
        }

        // 引用不存在的 visualMapId → 全量校验拒绝。
        legend::world::WorldDataSet badRef = world;
        if (!badRef.maps.empty()) {
            badRef.maps[0].visualMapId = "vmap_ghost";
            std::string validateError;
            Check("VisualDef: map visualMapId dangling reference rejected",
                  !ValidateWorldData(badRef, validateError));
        }
        // placement assetId 为空 → 拒绝。
        legend::world::WorldDataSet badPlacement = world;
        if (!badPlacement.visualMaps.empty() &&
            badPlacement.visualMaps[0].layers.size() > 1) {
            badPlacement.visualMaps[0].layers[1].placements.push_back({"", 1.0f, 1.0f});
            std::string validateError;
            Check("VisualDef: empty placement assetId rejected",
                  !ValidateWorldData(badPlacement, validateError));
        }
        // 非法 tileSize → 拒绝。
        legend::world::WorldDataSet badTile = world;
        if (!badTile.visualMaps.empty()) {
            badTile.visualMaps[0].tileSize = 0.0f;
            std::string validateError;
            Check("VisualDef: invalid tileSize rejected",
                  !ValidateWorldData(badTile, validateError));
        }
        // 层名错误 → 拒绝。
        legend::world::WorldDataSet badLayer = world;
        if (!badLayer.visualMaps.empty() && badLayer.visualMaps[0].layers.size() == 4) {
            badLayer.visualMaps[0].layers[1].name = "Middle";
            std::string validateError;
            Check("VisualDef: wrong layer name/order rejected",
                  !ValidateWorldData(badLayer, validateError));
        }
        // MakeDefaultWorldData 与仓库 JSON 一致（单一事实来源）。
        const legend::world::WorldDataSet defaults = legend::world::MakeDefaultWorldData();
        Check("VisualDef: default world has 3 visual maps + portal visualId",
              defaults.visualMaps.size() == 3 && defaults.portals.size() == 4 &&
                  !defaults.portals[0].visualId.empty());
    }

    // ---- 展示目录 fallback ----
    {
        Check("VisualDef: monster visualId fallback (unknown id -> empty)",
              catalog.MonsterVisualId(9999).empty() &&
                  catalog.MonsterVisualId(1) == "training_slime");
        Check("VisualDef: portal visualId fallback (unknown id -> empty=default)",
              catalog.PortalVisualId(9999).empty() && catalog.PortalVisualId(8001) == "portal_default");
        Check("VisualDef: skill display lookup hit/miss",
              catalog.FindSkillDisplay(1001) != nullptr &&
                  catalog.FindSkillDisplay(9999) == nullptr);
        Check("VisualDef: quest display lookup hit/miss",
              catalog.FindQuestDisplay(4001) != nullptr &&
                  catalog.FindQuestDisplay(9999) == nullptr);
    }
}

} // namespace

// 由 WorldChecks.cpp 调用（阶段24）。
void RunVisualDefinitionChecks() {
    std::printf("[VisualDefinition] checks begin\n");
    RunVisualDefinitionLogicChecks();
    std::printf("[VisualDefinition] checks complete (failures so far = %d)\n", g_failures);
}

} // namespace worldtest
