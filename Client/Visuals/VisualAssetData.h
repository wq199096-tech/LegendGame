#pragma once

// ---------------------------------------------------------------------------
// 阶段24：视觉资产数据层（Data/Assets 4 JSON 的解析 + 校验 + 统一 AnimationPlayer）。
// 纯数据/逻辑层：只依赖 nlohmann_json 与标准库（不依赖 SDL/OpenGL），
// LegendClient / LegendMapEditor / LegendWorldTests 三方共用。
//
// 文件清单（schemaVersion=1）：
//   asset_manifest.json / animations.json / visual_entities.json / effects.json
// 地图视觉（visual_maps.json）在 Shared/WorldData（MapVisualDefinition）。
// ---------------------------------------------------------------------------

#include <map>
#include <string>
#include <vector>

namespace legend::visual {

// ---- asset_manifest.json ----

// 资源类型（指令三）：Texture / Sprite / SpriteSheet / Font / EffectTexture
inline constexpr const char* kAssetTypeTexture = "Texture";
inline constexpr const char* kAssetTypeSprite = "Sprite";
inline constexpr const char* kAssetTypeSpriteSheet = "SpriteSheet";
inline constexpr const char* kAssetTypeFont = "Font";
inline constexpr const char* kAssetTypeEffectTexture = "EffectTexture";

bool IsValidAssetType(const std::string& type);

// 相对路径校验：禁止绝对路径（D:\xxx / C:\xxx / 以 '/' 或 '\\' 开头 / 含 ".."）。
// 返回 true = 合法相对路径。
bool IsRelativeAssetPath(const std::string& path);

struct AssetManifestEntry {
    std::string assetId;
    std::string type;
    std::string path; // 相对路径（相对仓库/工作目录根）
    int width = 0;
    int height = 0;
    float pivotX = 0.5f;
    float pivotY = 0.5f;
    bool enabled = true;
};

struct AssetManifest {
    int schemaVersion = 1;
    std::vector<AssetManifestEntry> assets;
};

// 单文件解析（JSON 语法/字段类型错误 → false + error）。不做跨字段校验。
bool LoadAssetManifest(const std::string& filePath, AssetManifest& out, std::string& error);

// 结构校验：schemaVersion、assetId 非空唯一、type 合法、相对路径、宽高>0（Font 除外）、
// pivot 在 [0,1]、enabled 字段存在。
bool ValidateAssetManifest(const AssetManifest& manifest, std::string& error);

const AssetManifestEntry* FindAsset(const AssetManifest& manifest, const std::string& assetId);

// ---- animations.json ----

struct AnimationClipDef {
    std::string animationId;
    std::string spriteSheetAssetId;
    int frameWidth = 0;
    int frameHeight = 0;
    int frameCount = 0;
    float fps = 0.0f;
    bool loop = true;
    int directionCount = 1; // 1 / 4 / 8
    // Resolve 时由 manifest 回填（校验/UV 计算用；JSON 不含这两字段）
    int sheetWidth = 0;
    int sheetHeight = 0;
};

struct AnimationSet {
    int schemaVersion = 1;
    std::vector<AnimationClipDef> clips;
};

bool LoadAnimations(const std::string& filePath, AnimationSet& out, std::string& error);

// 结构校验（不含 manifest 交叉引用）：animationId 唯一非空、fps>0、frameCount>0、
// directionCount ∈ {1,4,8}、frameWidth/Height>0。
bool ValidateAnimationSet(const AnimationSet& set, std::string& error);

// 交叉校验 + 回填 sheetWidth/sheetHeight：spriteSheetAssetId 必须存在于 manifest 且为
// SpriteSheet/EffectTexture 类型；帧网格必须能容纳 frameCount×directionCount
//（cols = sheetWidth/frameWidth >= frameCount 且 rows = sheetHeight/frameHeight >= directionCount）。
bool ResolveAnimations(const AnimationSet& set, const AssetManifest& manifest,
                       std::string& error);

const AnimationClipDef* FindClip(const AnimationSet& set, const std::string& animationId);

// ---- visual_entities.json ----

// 实体种类（指令三十七）：Player / Monster / Npc / Portal
bool IsValidVisualKind(const std::string& kind);

// 动画槽位（kind → 必填槽位）：
//   Player: idle walk attack cast hit death
//   Monster: idle walk attack hit death
//   Npc: idle
//   Portal: idle
bool RequiredAnimationSlots(const std::string& kind, std::vector<std::string>& outSlots);

struct VisualEntityDef {
    std::string visualId;
    std::string kind;
    float scale = 1.0f;
    int classId = 0;        // Player：职业 id（1=Warrior 2=Mage 3=Taoist；0=n/a）
    int serverVisualId = 0; // Npc：NpcSpawn 协议中的数字 visualId（0=n/a）
    std::map<std::string, std::string> animations; // 槽位 -> animationId
    std::string portraitAsset;                     // Player HUD 头像（可空）
    // 阶段26 指令十一：可选着色 "#RRGGBB[AA]"（造型差异化；空 = 白色不染色）。
    std::string tint;
};

struct VisualEntitySet {
    int schemaVersion = 1;
    std::vector<VisualEntityDef> entities;
};

bool LoadVisualEntities(const std::string& filePath, VisualEntitySet& out, std::string& error);

// 结构校验：visualId 唯一非空、kind 合法、必填槽位齐全、scale>0、
// Npc serverVisualId 唯一且 >0。
bool ValidateVisualEntitySet(const VisualEntitySet& set, std::string& error);

// 交叉校验：所有 animationId 必须存在于 AnimationSet；portraitAsset 存在于 manifest。
bool ResolveVisualEntities(const VisualEntitySet& set, const AnimationSet& animations,
                           const AssetManifest& manifest, std::string& error);

const VisualEntityDef* FindVisualEntity(const VisualEntitySet& set, const std::string& visualId);

// ---- effects.json ----

struct EffectDef {
    std::string effectId;
    std::string spriteSheetAssetId;
    int frameWidth = 0;
    int frameHeight = 0;
    int frameCount = 0;
    float fps = 0.0f;
    float duration = 0.0f; // 秒；0 = 纯帧驱动（frameCount/fps）
    bool loop = false;
    float scale = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    // Resolve 回填
    int sheetWidth = 0;
    int sheetHeight = 0;
};

struct EffectSet {
    int schemaVersion = 1;
    std::vector<EffectDef> effects;
};

bool LoadEffects(const std::string& filePath, EffectSet& out, std::string& error);

bool ValidateEffectSet(const EffectSet& set, std::string& error);

bool ResolveEffects(const EffectSet& set, const AssetManifest& manifest, std::string& error);

const EffectDef* FindEffect(const EffectSet& set, const std::string& effectId);

// ---- 全量校验（Editor "Assets Validation" 与测试共用；指令三十六）----
// 依次执行各文件结构校验 + 全部交叉引用校验。error 首错即返（带 文件/ID/原因）。
bool ValidateVisualData(const AssetManifest& manifest, const AnimationSet& animations,
                        const VisualEntitySet& entities, const EffectSet& effects,
                        std::string& error);

} // namespace legend::visual
