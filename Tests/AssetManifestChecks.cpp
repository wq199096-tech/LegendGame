// ---------------------------------------------------------------------------
// 阶段24：AssetManifestChecks —— asset_manifest.json 数据层检查
//（指令四十五/四十六：加入现有 LegendWorldTests，不新增第四套 CTest）。
// 覆盖：manifest load / duplicate assetId / relative path / absolute path
// rejection / type 校验 / pivot 校验 / FindAsset / 真实仓库 manifest 全量校验。
// 纯数据层（无 SDL/GL），复用 worldtest::Check。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Client/Visuals/VisualAssetData.h"

#include <string>

#ifndef LEGEND_SOURCE_DIR
#define LEGEND_SOURCE_DIR "."
#endif

namespace worldtest {

namespace {

using namespace legend::visual;

void RunAssetManifestLogicChecks() {
    // ---- 真实仓库 manifest 加载 + 全量校验 ----
    AssetManifest manifest;
    const std::string manifestPath =
        std::string(LEGEND_SOURCE_DIR) + "/Data/Assets/asset_manifest.json";
    {
        std::string error;
        const bool loaded = LoadAssetManifest(manifestPath, manifest, error);
        Check("AssetManifest: repo manifest loads", loaded);
        if (loaded) {
            Check("AssetManifest: schemaVersion = 1", manifest.schemaVersion == 1);
            Check("AssetManifest: asset count >= 40", manifest.assets.size() >= 40);
            std::string validateError;
            Check("AssetManifest: repo manifest validates clean",
                  ValidateAssetManifest(manifest, validateError));
            bool allPathsRelative = true;
            bool allTypesValid = true;
            for (const auto& entry : manifest.assets) {
                if (!IsRelativeAssetPath(entry.path)) {
                    allPathsRelative = false;
                }
                if (!IsValidAssetType(entry.type)) {
                    allTypesValid = false;
                }
            }
            Check("AssetManifest: all paths relative (no D:\\/C:\\/..)", allPathsRelative);
            Check("AssetManifest: all types known", allTypesValid);
            Check("AssetManifest: FindAsset hit",
                  FindAsset(manifest, "warrior_idle_sheet") != nullptr);
            Check("AssetManifest: FindAsset miss returns nullptr",
                  FindAsset(manifest, "no_such_asset") == nullptr);
        } else {
            Check("AssetManifest: load error reported", !error.empty());
        }
    }

    // ---- duplicate assetId 拒绝 ----
    {
        AssetManifest dup = manifest;
        if (!dup.assets.empty()) {
            dup.assets.push_back(dup.assets.front());
            std::string error;
            Check("AssetManifest: duplicate assetId rejected",
                  !ValidateAssetManifest(dup, error) && error.find("duplicate") != std::string::npos);
        }
    }

    // ---- 绝对路径拒绝（指令三：禁止 D:\xxx / C:\xxx / ".."）----
    {
        AssetManifest abs = manifest;
        abs.assets.push_back({"bad_abs_win", "Texture", "C:\\evil\\x.png", 8, 8, 0.5f, 0.5f, true});
        std::string error;
        Check("AssetManifest: absolute path C:\\ rejected",
              !ValidateAssetManifest(abs, error));
        AssetManifest abs2 = manifest;
        abs2.assets.push_back({"bad_abs_unc", "Texture", "/root/x.png", 8, 8, 0.5f, 0.5f, true});
        std::string error2;
        Check("AssetManifest: leading '/' rejected", !ValidateAssetManifest(abs2, error2));
        AssetManifest escape = manifest;
        escape.assets.push_back({"bad_escape", "Texture", "../x.png", 8, 8, 0.5f, 0.5f, true});
        std::string error3;
        Check("AssetManifest: '..' escape rejected", !ValidateAssetManifest(escape, error3));
    }

    // ---- 非法类型 / pivot / 宽高 ----
    {
        AssetManifest bad = manifest;
        bad.assets.push_back({"bad_type", "Mesh", "Assets/x.png", 8, 8, 0.5f, 0.5f, true});
        std::string error;
        Check("AssetManifest: unknown type rejected", !ValidateAssetManifest(bad, error));
        AssetManifest badPivot = manifest;
        badPivot.assets.push_back({"bad_pivot", "Texture", "Assets/x.png", 8, 8, 1.5f, 0.5f, true});
        std::string error2;
        Check("AssetManifest: pivot out of range rejected",
              !ValidateAssetManifest(badPivot, error2));
        AssetManifest badDims = manifest;
        badDims.assets.push_back({"bad_dims", "Texture", "Assets/x.png", 0, 8, 0.5f, 0.5f, true});
        std::string error3;
        Check("AssetManifest: non-positive dims rejected",
              !ValidateAssetManifest(badDims, error3));
    }

    // ---- 缺失文件加载失败（带错误信息）----
    {
        AssetManifest missing;
        std::string error;
        Check("AssetManifest: missing file fails cleanly",
              !LoadAssetManifest("no_such_dir/asset_manifest.json", missing, error) &&
                  !error.empty());
    }
}

} // namespace

// 由 WorldChecks.cpp 调用（阶段24）。
void RunAssetManifestChecks() {
    std::printf("[AssetManifest] checks begin\n");
    RunAssetManifestLogicChecks();
    std::printf("[AssetManifest] checks complete (failures so far = %d)\n", g_failures);
}

} // namespace worldtest
