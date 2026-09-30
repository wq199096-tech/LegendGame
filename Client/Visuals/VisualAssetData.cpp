#include "Client/Visuals/VisualAssetData.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace legend::visual {

namespace {

using nlohmann::json;

bool ReadSchemaVersion(const json& root, const char* fileName, int& out, std::string& error) {
    const auto it = root.find("schemaVersion");
    if (it == root.end() || !it->is_number_integer()) {
        error = std::string(fileName) + ": missing/invalid integer field 'schemaVersion'";
        return false;
    }
    out = it->get<int>();
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

bool ReadInt(const json& obj, const char* key, int& out, const std::string& ctx,
             std::string& error) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_integer()) {
        error = ctx + ": missing/invalid integer field '" + key + "'";
        return false;
    }
    out = it->get<int>();
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

// 帧网格容量校验（Resolve 共用）：cols >= frameCount 且 rows >= directionCount。
bool CheckFrameGrid(const AnimationClipDef& clip, const std::string& fileName,
                    std::string& error) {
    const std::string ctx = fileName + ": animation '" + clip.animationId + "'";
    if (clip.sheetWidth <= 0 || clip.sheetHeight <= 0) {
        error = ctx + ": sheet has no declared size";
        return false;
    }
    if (clip.frameWidth <= 0 || clip.frameHeight <= 0) {
        error = ctx + ": invalid frame size";
        return false;
    }
    if (clip.sheetWidth % clip.frameWidth != 0 || clip.sheetHeight % clip.frameHeight != 0) {
        error = ctx + ": sheet size " + std::to_string(clip.sheetWidth) + "x" +
                std::to_string(clip.sheetHeight) + " not divisible by frame " +
                std::to_string(clip.frameWidth) + "x" + std::to_string(clip.frameHeight);
        return false;
    }
    const int cols = clip.sheetWidth / clip.frameWidth;
    const int rows = clip.sheetHeight / clip.frameHeight;
    if (cols < clip.frameCount) {
        error = ctx + ": sheet columns " + std::to_string(cols) + " < frameCount " +
                std::to_string(clip.frameCount);
        return false;
    }
    if (rows < clip.directionCount) {
        error = ctx + ": sheet rows " + std::to_string(rows) + " < directionCount " +
                std::to_string(clip.directionCount);
        return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// asset_manifest.json
// ---------------------------------------------------------------------------

bool IsValidAssetType(const std::string& type) {
    return type == kAssetTypeTexture || type == kAssetTypeSprite ||
           type == kAssetTypeSpriteSheet || type == kAssetTypeFont ||
           type == kAssetTypeEffectTexture;
}

bool IsRelativeAssetPath(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (path.front() == '/' || path.front() == '\\') {
        return false;
    }
    if (path.size() >= 2 && path[1] == ':') { // D:\xxx / C:/xxx
        return false;
    }
    if (path.find("..") != std::string::npos) {
        return false;
    }
    return true;
}

bool LoadAssetManifest(const std::string& filePath, AssetManifest& out, std::string& error) {
    out = AssetManifest{};
    json root;
    if (!ReadJsonFile(filePath, root, error) ||
        !ReadSchemaVersion(root, filePath.c_str(), out.schemaVersion, error)) {
        return false;
    }
    const auto it = root.find("assets");
    if (it == root.end() || !it->is_array()) {
        error = filePath + ": missing/invalid array 'assets'";
        return false;
    }
    for (const auto& e : *it) {
        if (!e.is_object()) {
            error = filePath + ": non-object element in 'assets'";
            return false;
        }
        AssetManifestEntry entry;
        if (!ReadString(e, "assetId", entry.assetId, filePath, error) ||
            !ReadString(e, "type", entry.type, filePath, error) ||
            !ReadString(e, "path", entry.path, filePath, error) ||
            !ReadInt(e, "width", entry.width, filePath, error) ||
            !ReadInt(e, "height", entry.height, filePath, error) ||
            !ReadFloat(e, "pivotX", entry.pivotX, filePath, error) ||
            !ReadFloat(e, "pivotY", entry.pivotY, filePath, error) ||
            !ReadBool(e, "enabled", entry.enabled, filePath, error)) {
            return false;
        }
        out.assets.push_back(std::move(entry));
    }
    return true;
}

bool ValidateAssetManifest(const AssetManifest& manifest, std::string& error) {
    if (manifest.schemaVersion != 1) {
        error = "asset_manifest.json: unsupported schemaVersion " +
                std::to_string(manifest.schemaVersion);
        return false;
    }
    std::set<std::string> ids;
    for (const auto& entry : manifest.assets) {
        const std::string ctx = "asset_manifest.json: asset '" + entry.assetId + "'";
        if (entry.assetId.empty()) {
            error = "asset_manifest.json: assetId must not be empty";
            return false;
        }
        if (!ids.insert(entry.assetId).second) {
            error = ctx + ": duplicate assetId";
            return false;
        }
        if (!IsValidAssetType(entry.type)) {
            error = ctx + ": unknown type '" + entry.type + "'";
            return false;
        }
        if (!IsRelativeAssetPath(entry.path)) {
            error = ctx + ": path must be relative (got '" + entry.path + "')";
            return false;
        }
        const bool isFont = entry.type == kAssetTypeFont;
        if (!isFont && (entry.width <= 0 || entry.height <= 0)) {
            error = ctx + ": invalid width/height";
            return false;
        }
        if (!(entry.pivotX >= 0.0f && entry.pivotX <= 1.0f) ||
            !(entry.pivotY >= 0.0f && entry.pivotY <= 1.0f)) {
            error = ctx + ": pivot must be within [0,1]";
            return false;
        }
    }
    return true;
}

const AssetManifestEntry* FindAsset(const AssetManifest& manifest, const std::string& assetId) {
    for (const auto& entry : manifest.assets) {
        if (entry.assetId == assetId) {
            return &entry;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// animations.json
// ---------------------------------------------------------------------------

bool LoadAnimations(const std::string& filePath, AnimationSet& out, std::string& error) {
    out = AnimationSet{};
    json root;
    if (!ReadJsonFile(filePath, root, error) ||
        !ReadSchemaVersion(root, filePath.c_str(), out.schemaVersion, error)) {
        return false;
    }
    const auto it = root.find("clips");
    if (it == root.end() || !it->is_array()) {
        error = filePath + ": missing/invalid array 'clips'";
        return false;
    }
    for (const auto& e : *it) {
        if (!e.is_object()) {
            error = filePath + ": non-object element in 'clips'";
            return false;
        }
        AnimationClipDef clip;
        if (!ReadString(e, "animationId", clip.animationId, filePath, error) ||
            !ReadString(e, "spriteSheetAssetId", clip.spriteSheetAssetId, filePath, error) ||
            !ReadInt(e, "frameWidth", clip.frameWidth, filePath, error) ||
            !ReadInt(e, "frameHeight", clip.frameHeight, filePath, error) ||
            !ReadInt(e, "frameCount", clip.frameCount, filePath, error) ||
            !ReadFloat(e, "fps", clip.fps, filePath, error) ||
            !ReadBool(e, "loop", clip.loop, filePath, error) ||
            !ReadInt(e, "directionCount", clip.directionCount, filePath, error)) {
            return false;
        }
        out.clips.push_back(std::move(clip));
    }
    return true;
}

bool ValidateAnimationSet(const AnimationSet& set, std::string& error) {
    if (set.schemaVersion != 1) {
        error = "animations.json: unsupported schemaVersion " + std::to_string(set.schemaVersion);
        return false;
    }
    std::set<std::string> ids;
    for (const auto& clip : set.clips) {
        const std::string ctx = "animations.json: animation '" + clip.animationId + "'";
        if (clip.animationId.empty()) {
            error = "animations.json: animationId must not be empty";
            return false;
        }
        if (!ids.insert(clip.animationId).second) {
            error = ctx + ": duplicate animationId";
            return false;
        }
        if (clip.spriteSheetAssetId.empty()) {
            error = ctx + ": spriteSheetAssetId must not be empty";
            return false;
        }
        if (!(clip.fps > 0.0f)) {
            error = ctx + ": invalid fps (must be > 0)";
            return false;
        }
        if (clip.frameCount <= 0) {
            error = ctx + ": invalid frameCount (must be > 0)";
            return false;
        }
        if (clip.directionCount != 1 && clip.directionCount != 4 && clip.directionCount != 8) {
            error = ctx + ": invalid directionCount " + std::to_string(clip.directionCount) +
                    " (allowed: 1/4/8)";
            return false;
        }
        if (clip.frameWidth <= 0 || clip.frameHeight <= 0) {
            error = ctx + ": invalid frame size";
            return false;
        }
    }
    return true;
}

bool ResolveAnimations(const AnimationSet& set, const AssetManifest& manifest,
                       std::string& error) {
    for (const auto& clip : set.clips) {
        const AssetManifestEntry* sheet = FindAsset(manifest, clip.spriteSheetAssetId);
        if (sheet == nullptr) {
            error = "animations.json: animation '" + clip.animationId +
                    "' references missing asset '" + clip.spriteSheetAssetId + "'";
            return false;
        }
        if (sheet->type != kAssetTypeSpriteSheet && sheet->type != kAssetTypeEffectTexture) {
            error = "animations.json: animation '" + clip.animationId + "' asset '" +
                    clip.spriteSheetAssetId + "' is not a SpriteSheet/EffectTexture";
            return false;
        }
        // 拷贝出回填副本写回（set 为 const —— 约定 Resolve 只在加载后调用一次，
        // 这里用 const_cast 避免双重接口；字段仅回填不改语义）。
        auto& mutableClip = const_cast<AnimationClipDef&>(clip);
        mutableClip.sheetWidth = sheet->width;
        mutableClip.sheetHeight = sheet->height;
        if (!CheckFrameGrid(mutableClip, "animations.json", error)) {
            return false;
        }
    }
    return true;
}

const AnimationClipDef* FindClip(const AnimationSet& set, const std::string& animationId) {
    for (const auto& clip : set.clips) {
        if (clip.animationId == animationId) {
            return &clip;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// visual_entities.json
// ---------------------------------------------------------------------------

bool IsValidVisualKind(const std::string& kind) {
    return kind == "Player" || kind == "Monster" || kind == "Npc" || kind == "Portal";
}

bool RequiredAnimationSlots(const std::string& kind, std::vector<std::string>& outSlots) {
    outSlots.clear();
    if (kind == "Player") {
        outSlots = {"idle", "walk", "attack", "cast", "hit", "death"};
    } else if (kind == "Monster") {
        outSlots = {"idle", "walk", "attack", "hit", "death"};
    } else if (kind == "Npc" || kind == "Portal") {
        outSlots = {"idle"};
    } else {
        return false;
    }
    return true;
}

bool LoadVisualEntities(const std::string& filePath, VisualEntitySet& out, std::string& error) {
    out = VisualEntitySet{};
    json root;
    if (!ReadJsonFile(filePath, root, error) ||
        !ReadSchemaVersion(root, filePath.c_str(), out.schemaVersion, error)) {
        return false;
    }
    const auto it = root.find("entities");
    if (it == root.end() || !it->is_array()) {
        error = filePath + ": missing/invalid array 'entities'";
        return false;
    }
    for (const auto& e : *it) {
        if (!e.is_object()) {
            error = filePath + ": non-object element in 'entities'";
            return false;
        }
        VisualEntityDef entity;
        if (!ReadString(e, "visualId", entity.visualId, filePath, error) ||
            !ReadString(e, "kind", entity.kind, filePath, error) ||
            !ReadFloat(e, "scale", entity.scale, filePath, error)) {
            return false;
        }
        // 可选：classId（Player）/ serverVisualId（Npc）/ portraitAsset。
        const auto classIt = e.find("classId");
        if (classIt != e.end()) {
            if (!classIt->is_number_integer()) {
                error = filePath + ": entity '" + entity.visualId +
                        "': classId must be an integer";
                return false;
            }
            entity.classId = classIt->get<int>();
        }
        const auto serverIt = e.find("serverVisualId");
        if (serverIt != e.end()) {
            if (!serverIt->is_number_integer()) {
                error = filePath + ": entity '" + entity.visualId +
                        "': serverVisualId must be an integer";
                return false;
            }
            entity.serverVisualId = serverIt->get<int>();
        }
        const auto portraitIt = e.find("portraitAsset");
        if (portraitIt != e.end()) {
            if (!portraitIt->is_string()) {
                error = filePath + ": entity '" + entity.visualId +
                        "': portraitAsset must be a string";
                return false;
            }
            entity.portraitAsset = portraitIt->get<std::string>();
        }
        const auto animsIt = e.find("animations");
        if (animsIt == e.end() || !animsIt->is_object()) {
            error = filePath + ": entity '" + entity.visualId +
                    "': missing/invalid object 'animations'";
            return false;
        }
        for (auto animIt = animsIt->begin(); animIt != animsIt->end(); ++animIt) {
            if (!animIt.value().is_string()) {
                error = filePath + ": entity '" + entity.visualId + "': animation slot '" +
                        animIt.key() + "' must be a string";
                return false;
            }
            entity.animations[animIt.key()] = animIt.value().get<std::string>();
        }
        out.entities.push_back(std::move(entity));
    }
    return true;
}

bool ValidateVisualEntitySet(const VisualEntitySet& set, std::string& error) {
    if (set.schemaVersion != 1) {
        error = "visual_entities.json: unsupported schemaVersion " +
                std::to_string(set.schemaVersion);
        return false;
    }
    std::set<std::string> ids;
    std::set<int> serverVisualIds;
    for (const auto& entity : set.entities) {
        const std::string ctx = "visual_entities.json: entity '" + entity.visualId + "'";
        if (entity.visualId.empty()) {
            error = "visual_entities.json: visualId must not be empty";
            return false;
        }
        if (!ids.insert(entity.visualId).second) {
            error = ctx + ": duplicate visualId";
            return false;
        }
        if (!IsValidVisualKind(entity.kind)) {
            error = ctx + ": unknown kind '" + entity.kind + "'";
            return false;
        }
        if (!(entity.scale > 0.0f)) {
            error = ctx + ": invalid scale (must be > 0)";
            return false;
        }
        std::vector<std::string> required;
        RequiredAnimationSlots(entity.kind, required);
        for (const auto& slot : required) {
            const auto it = entity.animations.find(slot);
            if (it == entity.animations.end() || it->second.empty()) {
                error = ctx + ": missing required animation slot '" + slot + "'";
                return false;
            }
        }
        if (entity.kind == "Npc") {
            if (entity.serverVisualId <= 0) {
                error = ctx + ": Npc entity requires serverVisualId > 0";
                return false;
            }
            if (!serverVisualIds.insert(entity.serverVisualId).second) {
                error = ctx + ": duplicate serverVisualId " +
                        std::to_string(entity.serverVisualId);
                return false;
            }
        }
    }
    return true;
}

bool ResolveVisualEntities(const VisualEntitySet& set, const AnimationSet& animations,
                           const AssetManifest& manifest, std::string& error) {
    for (const auto& entity : set.entities) {
        const std::string ctx = "visual_entities.json: entity '" + entity.visualId + "'";
        for (const auto& [slot, animationId] : entity.animations) {
            if (FindClip(animations, animationId) == nullptr) {
                error = ctx + ": slot '" + slot + "' references missing animation '" +
                        animationId + "'";
                return false;
            }
        }
        if (!entity.portraitAsset.empty() && FindAsset(manifest, entity.portraitAsset) == nullptr) {
            error = ctx + ": references missing portrait asset '" + entity.portraitAsset + "'";
            return false;
        }
    }
    return true;
}

const VisualEntityDef* FindVisualEntity(const VisualEntitySet& set, const std::string& visualId) {
    for (const auto& entity : set.entities) {
        if (entity.visualId == visualId) {
            return &entity;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// effects.json
// ---------------------------------------------------------------------------

bool LoadEffects(const std::string& filePath, EffectSet& out, std::string& error) {
    out = EffectSet{};
    json root;
    if (!ReadJsonFile(filePath, root, error) ||
        !ReadSchemaVersion(root, filePath.c_str(), out.schemaVersion, error)) {
        return false;
    }
    const auto it = root.find("effects");
    if (it == root.end() || !it->is_array()) {
        error = filePath + ": missing/invalid array 'effects'";
        return false;
    }
    for (const auto& e : *it) {
        if (!e.is_object()) {
            error = filePath + ": non-object element in 'effects'";
            return false;
        }
        EffectDef effect;
        if (!ReadString(e, "effectId", effect.effectId, filePath, error) ||
            !ReadString(e, "spriteSheetAssetId", effect.spriteSheetAssetId, filePath, error) ||
            !ReadInt(e, "frameWidth", effect.frameWidth, filePath, error) ||
            !ReadInt(e, "frameHeight", effect.frameHeight, filePath, error) ||
            !ReadInt(e, "frameCount", effect.frameCount, filePath, error) ||
            !ReadFloat(e, "fps", effect.fps, filePath, error) ||
            !ReadFloat(e, "duration", effect.duration, filePath, error) ||
            !ReadBool(e, "loop", effect.loop, filePath, error) ||
            !ReadFloat(e, "scale", effect.scale, filePath, error) ||
            !ReadFloat(e, "offsetX", effect.offsetX, filePath, error) ||
            !ReadFloat(e, "offsetY", effect.offsetY, filePath, error)) {
            return false;
        }
        out.effects.push_back(std::move(effect));
    }
    return true;
}

bool ValidateEffectSet(const EffectSet& set, std::string& error) {
    if (set.schemaVersion != 1) {
        error = "effects.json: unsupported schemaVersion " + std::to_string(set.schemaVersion);
        return false;
    }
    std::set<std::string> ids;
    for (const auto& effect : set.effects) {
        const std::string ctx = "effects.json: effect '" + effect.effectId + "'";
        if (effect.effectId.empty()) {
            error = "effects.json: effectId must not be empty";
            return false;
        }
        if (!ids.insert(effect.effectId).second) {
            error = ctx + ": duplicate effectId";
            return false;
        }
        if (effect.spriteSheetAssetId.empty()) {
            error = ctx + ": spriteSheetAssetId must not be empty";
            return false;
        }
        if (effect.frameCount <= 0) {
            error = ctx + ": invalid frameCount (must be > 0)";
            return false;
        }
        if (!(effect.fps > 0.0f)) {
            error = ctx + ": invalid fps (must be > 0)";
            return false;
        }
        if (!(effect.duration >= 0.0f)) {
            error = ctx + ": invalid duration (must be >= 0; 0 = frame-driven)";
            return false;
        }
        if (!(effect.scale > 0.0f)) {
            error = ctx + ": invalid scale (must be > 0)";
            return false;
        }
        if (effect.frameWidth <= 0 || effect.frameHeight <= 0) {
            error = ctx + ": invalid frame size";
            return false;
        }
    }
    return true;
}

bool ResolveEffects(const EffectSet& set, const AssetManifest& manifest, std::string& error) {
    for (const auto& effect : set.effects) {
        const AssetManifestEntry* sheet = FindAsset(manifest, effect.spriteSheetAssetId);
        if (sheet == nullptr) {
            error = "effects.json: effect '" + effect.effectId +
                    "' references missing asset '" + effect.spriteSheetAssetId + "'";
            return false;
        }
        if (sheet->type != kAssetTypeSpriteSheet && sheet->type != kAssetTypeEffectTexture) {
            error = "effects.json: effect '" + effect.effectId + "' asset '" +
                    effect.spriteSheetAssetId + "' is not a SpriteSheet/EffectTexture";
            return false;
        }
        auto& mutableEffect = const_cast<EffectDef&>(effect);
        mutableEffect.sheetWidth = sheet->width;
        mutableEffect.sheetHeight = sheet->height;
        if (mutableEffect.sheetWidth % effect.frameWidth != 0 ||
            mutableEffect.sheetHeight % effect.frameHeight != 0) {
            error = "effects.json: effect '" + effect.effectId +
                    "': sheet size not divisible by frame size";
            return false;
        }
        const int cols = mutableEffect.sheetWidth / effect.frameWidth;
        if (cols < effect.frameCount) {
            error = "effects.json: effect '" + effect.effectId + "': sheet columns " +
                    std::to_string(cols) + " < frameCount " +
                    std::to_string(effect.frameCount);
            return false;
        }
    }
    return true;
}

const EffectDef* FindEffect(const EffectSet& set, const std::string& effectId) {
    for (const auto& effect : set.effects) {
        if (effect.effectId == effectId) {
            return &effect;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 全量校验（指令三十六）
// ---------------------------------------------------------------------------

bool ValidateVisualData(const AssetManifest& manifest, const AnimationSet& animations,
                        const VisualEntitySet& entities, const EffectSet& effects,
                        std::string& error) {
    return ValidateAssetManifest(manifest, error) && ValidateAnimationSet(animations, error) &&
           ValidateVisualEntitySet(entities, error) && ValidateEffectSet(effects, error) &&
           ResolveAnimations(animations, manifest, error) &&
           ResolveEffects(effects, manifest, error) &&
           ResolveVisualEntities(entities, animations, manifest, error);
}

} // namespace legend::visual
